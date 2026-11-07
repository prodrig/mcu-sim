// =============================================================================
// depuracion.h — Dónde se engancha el depurador, para cualquier núcleo (E0.1)
//
// Hasta E0 de doc/analisis_esp32_riscv.md, `DebugAttach` vivía en el motor de
// GDB (common/gdb_rsp.h) y `DebugCaps` en la macro del Cortex-M4F
// (core/cortex_m4f.h). Ninguno de los dos es de ARM: dicen si el núcleo saca
// sus pines de depuración o lleva el stub dentro, y en qué puerto escucha. Pero
// por estar donde estaban, `soc/mcu_if.h` -que es de todos los chips- tenía que
// incluir el Cortex entero para nombrarlos, y un chip RISC-V habría arrastrado
// un núcleo ARM solo por eso.
//
// Aquí no hay más que los dos tipos y sus dos valores con nombre, tal cual
// estaban. Los dos ficheros de antes lo incluyen, así que nadie más tiene que
// cambiar un `#include`.
// =============================================================================
#ifndef STM32_COMMON_DEPURACION_H
#define STM32_COMMON_DEPURACION_H

namespace stm32 {

// -----------------------------------------------------------------------------
// Dónde se engancha el depurador. Es el parámetro que elige entre los dos stubs
// (véase doc/stm32f4xx/stm32f407vg_fase6_gdb2.md).
//
//   Pines    : el núcleo EXPONE sus pines de depuración (SWCLK/SWDIO/JTDI/SWO/
//              NJTRST en un Cortex-M; TCK/TMS/TDI/TDO en un RISC-V) y quien
//              quiera depurar se conecta por fuera, como un ST-LINK. Es el
//              modelo fiel: cada acceso cuesta decenas de flancos simulados.
//   Interno  : el núcleo RESERVA esos pines (nadie más puede usarlos, igual
//              que en el silicio) pero no los usa, y crea dentro de sí un stub
//              que habla con el DAP -o con el Debug Module de un RISC-V- por
//              llamada de función. Es el modelo rápido: un acceso es una
//              transacción TLM.
// -----------------------------------------------------------------------------
enum class DebugAttach { Pines, Interno };

// -----------------------------------------------------------------------------
// Rasgos de depuración del núcleo: cómo se llega a él.
//
// Misma receta que en el resto del modelo (UsartCaps, TimCaps, SpiCaps...): un
// struct constexpr de rasgos, unas instancias con nombre y una clase base no
// plantilla que los toma POR VALOR, de modo que se puede elegir en tiempo de
// compilación (con el alias) o en tiempo de ejecución (con el constructor).
//
// El criterio para elegir está en doc/stm32f4xx/stm32f407vg_fase6_gdb2.md.
// -----------------------------------------------------------------------------
struct DebugCaps {
    DebugAttach attach;      // pines expuestos o reservados
    unsigned    puerto;      // puerto TCP del stub interno (0 = no escuchar)
};
inline constexpr DebugCaps DBG_PINES  { DebugAttach::Pines,   0    };
inline constexpr DebugCaps DBG_INTERNO{ DebugAttach::Interno, 3333 };

} // namespace stm32
#endif // STM32_COMMON_DEPURACION_H
