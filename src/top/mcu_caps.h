// =============================================================================
// mcu_caps.h — El DESCRIPTOR de un microcontrolador
//
// Junta en una sola pieza lo que distingue a un chip de otro de la misma
// familia: los rasgos del núcleo (core/core_caps.h), los del mapa de memoria
// (mem/mem_caps.h) y los límites de reloj. Es lo que hasta ahora estaba
// repartido entre constantes globales de `common/ahb_types.h`, argumentos de
// constructor y números escritos al vuelo.
//
// LO QUE ESTO ES Y LO QUE NO ES. Con un descriptor se construye un STM32F407VG,
// y con otro se construiría un F405 —el mismo silicio sin Ethernet ni cámara—,
// un F415 o un F407 de otro encapsulado. Lo que NO se construye cambiando
// números es un chip de otra familia: un F446 tiene otro árbol de reloj y
// periféricos que aquí no existen, y describirlo con un `McuCaps` distinto no
// lo convertiría en un F446, lo convertiría en un F407 con etiqueta falsa. La
// frontera exacta está en el campo `familia`: dos chips con la misma familia se
// distinguen con este struct; dos familias distintas necesitan modelo nuevo.
//
// Por eso este fichero declara UN descriptor completo —el del F407VG— y dos de
// laboratorio que la suite usa para comprobar que las piezas se recombinan
// (T127). No hay aquí ningún chip que el proyecto afirme modelar y no modele.
// =============================================================================
#ifndef STM32_TOP_MCU_CAPS_H
#define STM32_TOP_MCU_CAPS_H

#include <string>
#include <vector>
#include "../core/core_caps.h"
#include "../mem/mem_caps.h"
#include "../pins/encapsulado.h"

namespace stm32 {

// Los topes de reloj por dominio. No son decoración: el RCC avisa cuando el
// firmware programa un árbol que los pasa, y ese aviso es de los que ahorran
// una tarde —un F401 a 168 MHz no existe, y el modelo tiene que decirlo—.
struct LimitesReloj {
    double sysclk_max, hclk_max, pclk1_max, pclk2_max;
};

// Los periféricos que NO lleva todo el mundo. Son tres, y no es una lista
// abierta a propósito: el resto del juego —los catorce temporizadores, los seis
// puertos serie, los tres ADC, el bxCAN, el SDIO, los dos OTG— está en TODOS
// los F405/F407, del LQFP64 al UFBGA176, y ponerlos como opción sugeriría que
// hay chips de esta familia sin ellos. No los hay.
//
//   eth   Ethernet MAC. Es LA diferencia entre un F405 y un F407, junto con
//         la cámara. [DS8626, tabla 2]
//   dcmi  Interfaz de cámara paralelo. La otra mitad de esa diferencia.
//   fsmc  Controlador de memoria externa. Lo llevan todos MENOS el LQFP64,
//         que sencillamente no tiene pines donde sacar el bus.
//
// Ojo con lo que NO hace falta modelar aquí: el datasheet dice que en LQFP100
// y WLCSP90 el FSMC está «restringido». Esa restricción es que no salen todas
// las líneas de dirección y datos, y eso YA lo dice el encapsulado —los pads
// que no están, no están—. Un booleano más sería describir dos veces lo mismo
// y arriesgarse a que las dos descripciones no coincidan.
struct Periferia {
    bool eth;
    bool dcmi;
    bool fsmc;
};

struct McuCaps {
    const char*  nombre;       // "STM32F407VG", el que se escribe en el XML
    const char*  familia;      // "STM32F4": la frontera de lo recombinable
    CoreCaps     nucleo;
    MemCaps      memoria;
    LimitesReloj reloj;
    Encapsulado  enc;          // qué pads salen al plástico
    Periferia    perif;        // qué bloques lleva este miembro de la familia
};

inline constexpr LimitesReloj RELOJ_STM32F407VG {
    F_SYSCLK_MAX, F_HCLK_MAX, F_PCLK1_MAX, F_PCLK2_MAX
};

// ---------------------------------------------------------------------------
// LA FAMILIA STM32F405/407, ENTERA
//
// Once referencias, y todas salen del MISMO silicio. Lo que las distingue son
// tres cosas y ninguna más [DS8626, tabla 2]:
//
//   EL DÍGITO 5 O 7. Un F405 es un F407 SIN Ethernet y SIN cámara. Eso es todo:
//   mismo núcleo, misma memoria, mismos temporizadores, mismos puertos serie.
//   (El F415/F417, que aquí no están, son los mismos con el acelerador
//   criptográfico; no se declaran porque ese bloque no está modelado y un
//   descriptor no lo haría aparecer.)
//
//   LA LETRA DEL ENCAPSULADO. R = LQFP64, O = WLCSP90, V = LQFP100,
//   Z = LQFP144, I = LQFP176/UFBGA176. Decide qué pads salen al plástico, y de
//   rebote si hay bus externo: en el LQFP64 no hay dónde sacarlo.
//
//   LA ÚLTIMA LETRA, EL TAMAÑO DE FLASH. E = 512 KB (ocho sectores),
//   G = 1 MB (doce). La RAM no cambia: los 192+4 KB están en todas.
//
// Lo que se gana con esto es muy concreto para un alumno: un firmware enlazado
// para 1 MB deja de caber en un `...E` y el modelo lo dice; `PE2` deja de
// existir en un LQFP64 y el modelo lo dice; y un programa que inicializa el
// Ethernet en un F405 se encuentra con que ese periférico no está, igual que en
// la tarjeta.
// ---------------------------------------------------------------------------

// Las dos geometrías de Flash de la familia. La de 1 MB ya estaba; la de
// 512 KB son sus ocho primeros sectores, y es una tabla y no media tabla
// porque el sector 7 termina donde termina la Flash.
inline constexpr FlashSector SECTORES_512K[] = {
    {0x08000000, 0x04000}, {0x08004000, 0x04000},   // 0,1 : 16 KB
    {0x08008000, 0x04000}, {0x0800C000, 0x04000},   // 2,3 : 16 KB
    {0x08010000, 0x10000},                          // 4   : 64 KB
    {0x08020000, 0x20000}, {0x08040000, 0x20000},   // 5,6 : 128 KB
    {0x08060000, 0x20000}                           // 7   : 128 KB
};
inline constexpr MapaFlash FLASH_512K {
    addr::FLASH_BASE, 0x00080000u,                  // 512 KB
    SECTORES_512K, 8,
    addr::SYSMEM_BASE, addr::SYSMEM_SIZE,
    addr::OTP_BASE,    addr::OTP_SIZE,
    addr::OPT_BASE,    addr::OPT_SIZE,
    30e6, 5
};
inline constexpr MemCaps MEM_512K { FLASH_512K, RAM_STM32F407VG };

// Los tres juegos de periféricos que hay en la familia.
inline constexpr Periferia PERIF_F407     { true,  true,  true  };  // ETH + camara + FSMC
inline constexpr Periferia PERIF_F405     { false, false, true  };  // sin ETH ni camara
inline constexpr Periferia PERIF_F405_R64 { false, false, false };  // ademas, sin bus externo

// El constructor que evita repetir once veces los mismos cinco campos. Todos
// los miembros comparten núcleo, RAM y topes de reloj; lo que se pasa es lo
// único que cambia.
constexpr McuCaps mcu_f4(const char* nombre, const MemCaps& mem,
                         const Encapsulado& enc, const Periferia& per) {
    return McuCaps{ nombre, "STM32F4", CORE_STM32F407VG, mem,
                    RELOJ_STM32F407VG, enc, per };
}

// --- STM32F405: sin Ethernet y sin camara -----------------------------------
inline constexpr McuCaps MCU_STM32F405RG =
    mcu_f4("STM32F405RG", MEM_STM32F407VG, ENC_LQFP64,  PERIF_F405_R64);
inline constexpr McuCaps MCU_STM32F405OG =
    mcu_f4("STM32F405OG", MEM_STM32F407VG, ENC_WLCSP90, PERIF_F405);
inline constexpr McuCaps MCU_STM32F405VG =
    mcu_f4("STM32F405VG", MEM_STM32F407VG, ENC_LQFP100, PERIF_F405);
inline constexpr McuCaps MCU_STM32F405ZG =
    mcu_f4("STM32F405ZG", MEM_STM32F407VG, ENC_LQFP144, PERIF_F405);
inline constexpr McuCaps MCU_STM32F405OE =
    mcu_f4("STM32F405OE", MEM_512K,        ENC_WLCSP90, PERIF_F405);

// --- STM32F407: con Ethernet y camara ---------------------------------------
inline constexpr McuCaps MCU_STM32F407VE =
    mcu_f4("STM32F407VE", MEM_512K,        ENC_LQFP100, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407VG =
    mcu_f4("STM32F407VG", MEM_STM32F407VG, ENC_LQFP100, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407ZE =
    mcu_f4("STM32F407ZE", MEM_512K,        ENC_LQFP144, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407ZG =
    mcu_f4("STM32F407ZG", MEM_STM32F407VG, ENC_LQFP144, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407IE =
    mcu_f4("STM32F407IE", MEM_512K,        ENC_LQFP176, PERIF_F407);
inline constexpr McuCaps MCU_STM32F407IG =
    mcu_f4("STM32F407IG", MEM_STM32F407VG, ENC_LQFP176, PERIF_F407);

// ---------------------------------------------------------------------------
// EL CATÁLOGO
//
// Una lista y dos funciones. No hace falta la factoría con auto-registro que
// hace falta para las piezas de placa, y conviene decir por qué: aquí los once
// miembros son EL MISMO MODELO con distintos rasgos, no once clases. La
// factoría hará falta el día que haya un chip de otra familia, que es otro
// modelo de verdad. [doc/stm32f407vg_multi_mcu.md, §6.1]
// ---------------------------------------------------------------------------
inline const McuCaps* const CATALOGO_MCU[] = {
    &MCU_STM32F405RG, &MCU_STM32F405OG, &MCU_STM32F405VG, &MCU_STM32F405ZG,
    &MCU_STM32F405OE,
    &MCU_STM32F407VE, &MCU_STM32F407VG, &MCU_STM32F407ZE, &MCU_STM32F407ZG,
    &MCU_STM32F407IE, &MCU_STM32F407IG
};
inline constexpr unsigned N_CATALOGO_MCU =
    sizeof(CATALOGO_MCU) / sizeof(CATALOGO_MCU[0]);

// Busca por nombre SIN distinguir mayúsculas: quien escribe `--mcu stm32f405rg`
// en una consola no está describiendo nada, está pidiendo. Devuelve nullptr si
// no existe, y el que llama tiene que decirlo — nunca montar otro en su lugar.
inline const McuCaps* mcu_por_nombre(const std::string& n) {
    for (const McuCaps* m : CATALOGO_MCU) {
        const char* a = m->nombre;
        size_t i = 0;
        for (; i < n.size() && a[i]; ++i) {
            char x = a[i], y = n[i];
            if (x >= 'a' && x <= 'z') x = char(x - 'a' + 'A');
            if (y >= 'a' && y <= 'z') y = char(y - 'a' + 'A');
            if (x != y) break;
        }
        if (i == n.size() && a[i] == '\0') return m;
    }
    return nullptr;
}

inline std::string mcus_como_texto() {
    std::string s;
    for (const McuCaps* m : CATALOGO_MCU) {
        if (!s.empty()) s += ", ";
        s += m->nombre;
    }
    return s;
}

} // namespace stm32
#endif // STM32_TOP_MCU_CAPS_H
