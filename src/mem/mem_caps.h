// =============================================================================
// mem_caps.h — Los RASGOS de las memorias: lo que distingue a un F407 de un F401
//
// Dos chips de la misma familia llevan el mismo controlador de Flash, la misma
// SRAM y el mismo mapa de memoria. Lo que cambia son los TAMAÑOS —y, con
// ellos, el mapa de sectores de la Flash, que no es proporcional: un F407 de
// 1 MB tiene doce sectores de cuatro tamaños distintos, y un F401 de 512 KB
// tiene seis—.
//
// Esto es lo que antes estaba como `constexpr` global en `common/ahb_types.h`
// y ahora es un DATO DE INSTANCIA. Las BASES no se mueven de allí, y es a
// propósito: `0x0800_0000` para la Flash y `0x2000_0000` para la SRAM no son
// decisiones de ST, son la arquitectura, y un STM32 que las cambiara dejaría de
// ser un STM32. Lo que varía es cuánto hay a partir de ahí.
//
// LA REGLA QUE HACE QUE ESTO NO ROMPA NADA: la instancia `MEM_STM32F407VG` de
// más abajo repite EXACTAMENTE los valores que había antes, y es el valor por
// omisión de todos los constructores. Un modelo que no diga nada monta el F407
// de siempre, byte a byte.
// =============================================================================
#ifndef STM32_MEM_MEM_CAPS_H
#define STM32_MEM_MEM_CAPS_H

#include <cstdint>
#include "../common/ahb_types.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// La Flash y lo que la acompaña en la región de código
// ---------------------------------------------------------------------------
struct MapaFlash {
    uint32_t base, size;              // Flash principal
    const FlashSector* sectores;      // la tabla, que NO es proporcional
    unsigned n_sectores;
    uint32_t sysmem_base, sysmem_size;   // el cargador de fábrica
    uint32_t otp_base, otp_size;
    uint32_t opt_base, opt_size;         // bytes de opción

    // La curva de estados de espera. En toda la familia F4 la regla es la
    // misma —un estado de espera más por cada tanto de frecuencia— y lo que
    // cambia entre chips y entre rangos de VDD es ese "tanto" y el techo.
    // Guardar los dos números en vez de la tabla es lo que permite que un chip
    // distinto no necesite otra función.
    double   hz_por_estado_espera;
    unsigned max_estados_espera;

    // El sector que contiene esa dirección, o -1. Es la misma cuenta que hacía
    // `flash_sector_of()`, ahora sobre la tabla de ESTE chip.
    int sector_de(uint32_t a) const {
        for (unsigned s = 0; s < n_sectores; ++s)
            if (a >= sectores[s].base && a < sectores[s].base + sectores[s].size)
                return int(s);
        return -1;
    }
    // La LATENCY mínima legal para una frecuencia de HCLK dada [IR, §5.2.2].
    unsigned latencia_minima(double hclk_hz) const {
        unsigned ws = 0;
        while (ws < max_estados_espera &&
               hclk_hz > double(ws + 1) * hz_por_estado_espera) ++ws;
        return ws;
    }
    uint32_t fin() const { return base + size; }
};

// ---------------------------------------------------------------------------
// Las RAM
//
// Un tamaño a cero significa QUE ESE BLOQUE NO EXISTE, no que sea vacío: hay
// F4 sin CCM y sin SRAM2, y describirlos es poner un cero aquí, no borrar una
// línea del modelo.
// ---------------------------------------------------------------------------
struct MapaRam {
    uint32_t sram1_base, sram1_size;
    uint32_t sram2_base, sram2_size;   // size 0 = no hay SRAM2
    uint32_t ccm_base,   ccm_size;     // size 0 = no hay CCM
    uint32_t bkp_base,   bkp_size;     // size 0 = no hay SRAM de backup

    bool hay_sram2() const { return sram2_size != 0; }
    bool hay_ccm()   const { return ccm_size   != 0; }
    bool hay_bkp()   const { return bkp_size   != 0; }
    // El final de la SRAM contigua vista por el firmware. En el F407 la SRAM1 y
    // la SRAM2 son contiguas y el enlazador las trata como un solo bloque de
    // 128 KB, que es de donde sale el valor inicial del puntero de pila.
    uint32_t fin_sram() const {
        const uint32_t f1 = sram1_base + sram1_size;
        return hay_sram2() ? sram2_base + sram2_size : f1;
    }
    uint32_t total() const {
        return sram1_size + sram2_size + ccm_size + bkp_size;
    }
};

struct MemCaps {
    MapaFlash flash;
    MapaRam   ram;
};

// ---------------------------------------------------------------------------
// El STM32F407VG: los mismos números que había en ahb_types.h, ni uno distinto
// ---------------------------------------------------------------------------
inline constexpr MapaFlash FLASH_STM32F407VG {
    addr::FLASH_BASE, addr::FLASH_SIZE,
    FLASH_SECTORS, N_FLASH_SECTORS,
    addr::SYSMEM_BASE, addr::SYSMEM_SIZE,
    addr::OTP_BASE,    addr::OTP_SIZE,
    addr::OPT_BASE,    addr::OPT_SIZE,
    30e6, 5                       // 168 MHz / 30 = seis tramos, LATENCY 0..5
};

inline constexpr MapaRam RAM_STM32F407VG {
    addr::SRAM1_BASE,   addr::SRAM1_SIZE,      // 112 KB
    addr::SRAM2_BASE,   addr::SRAM2_SIZE,      //  16 KB
    addr::CCM_BASE,     addr::CCM_SIZE,        //  64 KB
    addr::BKPSRAM_BASE, addr::BKPSRAM_SIZE     //   4 KB
};

inline constexpr MemCaps MEM_STM32F407VG { FLASH_STM32F407VG, RAM_STM32F407VG };

// ---------------------------------------------------------------------------
// Un mapa de LABORATORIO, para la verificación
//
// 256 KB en seis sectores —cuatro de 16 KB, uno de 64 y uno de 128— y un techo
// de 84 MHz. No es ningún chip concreto y no pretende serlo: es la forma que
// tiene la geometría de una Flash de la familia cuando el chip es más pequeño,
// y existe para que la suite pueda comprobar (T127) que el controlador de
// Flash de este proyecto obedece al mapa que se le da y no al que tiene
// escrito dentro. Que borre el sector 5 y borre 128 KB —y no 128 KB en la
// dirección del F407— es la prueba de que la pieza es reutilizable.
// ---------------------------------------------------------------------------
inline constexpr FlashSector SECTORES_LAB_256K[] = {
    {0x08000000, 0x04000}, {0x08004000, 0x04000},   // 0,1 : 16 KB
    {0x08008000, 0x04000}, {0x0800C000, 0x04000},   // 2,3 : 16 KB
    {0x08010000, 0x10000},                          // 4   : 64 KB
    {0x08020000, 0x20000}                           // 5   : 128 KB
};
inline constexpr MapaFlash FLASH_LAB_256K {
    addr::FLASH_BASE, 0x00040000u,                  // 256 KB
    SECTORES_LAB_256K, 6,
    addr::SYSMEM_BASE, addr::SYSMEM_SIZE,
    addr::OTP_BASE,    addr::OTP_SIZE,
    addr::OPT_BASE,    addr::OPT_SIZE,
    30e6, 2                                         // techo de 84 MHz: 0..2 WS
};
// Y una RAM sin CCM ni SRAM2, que es lo que llevan los F4 pequeños.
inline constexpr MapaRam RAM_LAB_64K {
    addr::SRAM1_BASE, 0x00010000u,   // 64 KB y nada mas
    0, 0,                            // sin SRAM2
    0, 0,                            // sin CCM
    0, 0                             // sin SRAM de backup
};

} // namespace stm32
#endif // STM32_MEM_MEM_CAPS_H
