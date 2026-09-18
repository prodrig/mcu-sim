// =============================================================================
// conectividad.h — QUÉ MAESTRO ALCANZA A QUÉ ESCLAVO, como dato
//
// Vive en su propio fichero, y no dentro de `ahb_matrix.h`, por la misma razón
// por la que el encapsulado vive en `pins/encapsulado.h` y no dentro del mux:
// **es un rasgo del chip, no una pieza del modelo**. El descriptor (`McuCaps`)
// lo lleva, el usuario lo elige con `--mcu`, y la matriz lo recibe hecho por su
// constructor. Aquí no hay un solo `sc_module`: son tablas.
// =============================================================================
#ifndef STM32_BUS_CONECTIVIDAD_H
#define STM32_BUS_CONECTIVIDAD_H

#include <initializer_list>
#include <cstdint>
#include "../common/ahb_types.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// LA CONECTIVIDAD DE LA MATRIZ, COMO DATO
//
// Qué maestro alcanza a qué esclavo [IR, §6.2]. Era una función con las nueve
// filas escritas dentro; ahora es un `struct` que la matriz recibe por el
// constructor, igual que se hizo con el encapsulado y con el mapa de memoria.
//
// Una fila por maestro, un bit por esclavo. Y de ahí sale gratis algo que hará
// falta enseguida: **un maestro cuya fila es cero es un maestro QUE NO EXISTE
// en ese chip**. El F446 tiene siete maestros y no ocho porque le falta el del
// Ethernet [RM0390, §2.1]; describirlo es poner su fila a cero, no tocar el
// `enum`. Es la misma idea que «un bloque de RAM que no existe es un tamaño a
// cero».
// ---------------------------------------------------------------------------
struct Conectividad {
    uint8_t alcanza[unsigned(BusMaster::N_MASTERS)];   // bit s = llega al esclavo s

    bool puede(BusMaster m, BusSlaveId s) const {
        return ((alcanza[unsigned(m)] >> unsigned(s)) & 1u) != 0;
    }
    // ¿Este maestro existe en el chip? Si no alcanza a nadie, no está.
    bool hay_maestro(BusMaster m) const { return alcanza[unsigned(m)] != 0; }
    unsigned n_maestros() const {
        unsigned n = 0;
        for (unsigned m = 0; m < unsigned(BusMaster::N_MASTERS); ++m)
            if (alcanza[m]) ++n;
        return n;
    }
};

// El juego de bits de una fila, para poder escribir la tabla con los nombres de
// los esclavos en vez de con hexadecimal.
constexpr uint8_t esclavos(std::initializer_list<BusSlaveId> ss) {
    uint8_t m = 0;
    for (BusSlaveId s : ss) m = uint8_t(m | (1u << unsigned(s)));
    return m;
}

// --- La tabla del STM32F405xx/07xx: ocho maestros [RM0090, §2.1] ------------
inline constexpr Conectividad CONN_STM32F407VG { {
    /* CORE_IBUS   */ esclavos({BusSlaveId::FLASH_ICODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::FSMC_EXT}),
    /* CORE_DBUS   */ esclavos({BusSlaveId::FLASH_DCODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::FSMC_EXT}),
    /* CORE_SBUS   */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG, BusSlaveId::AHB2_SEG,
                                BusSlaveId::FSMC_EXT}),
    /* DMA1_MEM    */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG, BusSlaveId::AHB2_SEG,
                                BusSlaveId::FSMC_EXT}),
    // DMA2 alcanza ADEMÁS la Flash por el bus DCode. La tabla de [IR, §6.2]
    // pone "No" en esa celda, pero §11.1.1 del mismo informe dice
    // explícitamente que DMA2 «soporta transferencias memoria-a-memoria y
    // acceso a la memoria Flash», que es lo que hace el silicio y de lo que
    // depende el caso de uso clásico Flash -> SRAM. La contradicción se
    // resuelve a favor de §11.1.1 [doc/stm32f4xx/stm32f407vg_fase4_dma.md, §9].
    /* DMA2_MEM    */ esclavos({BusSlaveId::FLASH_DCODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::AHB1_SEG,
                                BusSlaveId::AHB2_SEG, BusSlaveId::FSMC_EXT}),
    /* DMA2_PERIPH */ esclavos({BusSlaveId::FLASH_DCODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::AHB1_SEG,
                                BusSlaveId::AHB2_SEG, BusSlaveId::FSMC_EXT}),
    /* ETH_DMA     */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG}),
    /* OTG_HS_DMA  */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG})
} };
// La columna «CCM RAM» de la tabla solo tiene 'Sí' en el D-Bus, y la CCM no es
// esclavo de la matriz —[RM0090, §2.1]: «not part of the bus matrix and can be
// accessed only through the CPU»—: se resuelve en el router del núcleo.

// --- La tabla del STM32F446: SIETE maestros [RM0390, §2.1] ------------------
//
// «Seven masters», y la resta cuadra exactamente: **se va el DMA del Ethernet y
// no se va nada más**. Los índices de las subsecciones lo corroboran desde el
// otro lado —RM0090 §2.1 tiene una subsección «Ethernet DMA bus» que RM0390 no
// tiene, y la numeración salta en consecuencia—.
//
// Describirlo es UNA FILA A CERO. No hay que tocar el `enum BusMaster`, ni
// renumerar nada, ni tener dos matrices: el maestro que no existe no alcanza a
// nadie, `hay_maestro()` lo dice, y `n_maestros()` devuelve siete.
//
// EL SÉPTIMO ESCLAVO NO ES EL MISMO, y esto sí fue un hallazgo de la fase 0:
// RM0390 §2.1 dice también «Seven slaves», pero el séptimo es **«FMC /
// QUADSPI»** —los dos comparten un único puerto de la matriz— mientras que en
// el F407 es el FSMC a secas. La consecuencia para el plan es que el QUADSPI,
// cuando llegue en la fase 4, **no añade un octavo esclavo**: entra por
// `FSMC_EXT`, que a partir de ese día será el puerto de memoria externa y no
// «el del FSMC». En un F446RE, además, el FMC no tiene pines y la ventana es
// espacio reservado, así que hoy esa columna está apagada de todos modos.
inline constexpr Conectividad CONN_STM32F446 { {
    /* CORE_IBUS   */ esclavos({BusSlaveId::FLASH_ICODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::FSMC_EXT}),
    /* CORE_DBUS   */ esclavos({BusSlaveId::FLASH_DCODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::FSMC_EXT}),
    /* CORE_SBUS   */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG, BusSlaveId::AHB2_SEG,
                                BusSlaveId::FSMC_EXT}),
    /* DMA1_MEM    */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG, BusSlaveId::AHB2_SEG,
                                BusSlaveId::FSMC_EXT}),
    /* DMA2_MEM    */ esclavos({BusSlaveId::FLASH_DCODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::AHB1_SEG,
                                BusSlaveId::AHB2_SEG, BusSlaveId::FSMC_EXT}),
    /* DMA2_PERIPH */ esclavos({BusSlaveId::FLASH_DCODE, BusSlaveId::SRAM1,
                                BusSlaveId::SRAM2, BusSlaveId::AHB1_SEG,
                                BusSlaveId::AHB2_SEG, BusSlaveId::FSMC_EXT}),
    /* ETH_DMA     */ 0,          // no existe en el F446
    /* OTG_HS_DMA  */ esclavos({BusSlaveId::SRAM1, BusSlaveId::SRAM2,
                                BusSlaveId::AHB1_SEG})
} };

} // namespace stm32
#endif // STM32_BUS_CONECTIVIDAD_H
