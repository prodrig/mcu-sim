// =============================================================================
// verbosidad.h — Los AVISOS por la consola: si salen o no
//
// Un aviso es lo que el modelo dice por stderr y NO es un error: lo que un MCU
// tiene declarado y sin modelar (`[ojo]`), una errata que se ha disparado, un
// registro sin módulo, un conflicto eléctrico que no detiene la simulación,
// un puente por TCP sin --tiempo-real... mcu-sim-gui pinta en rojo todo lo que
// llega por stderr, y con tanto aviso los errores de verdad se pierden.
//
// Por eso `mcu-sim` los calla salvo con `--verbose` (top/sim_main.cpp). Los
// errores —lo que acaba en `muere()`, un firmware que no carga, la ventana que
// no contesta— salen SIEMPRE por stderr, con `std::fprintf` como hasta ahora:
// este fichero es solo para avisos.
//
// Por omisión, aquí, los avisos SALEN: los bancos de prueba (`sc_main_*.cpp`)
// no tocan nada y ven lo mismo que antes. Quien decide callarlos es `sim`.
//
// No cambia lo que va a la ventana: los T_AVISO del saludo y de la marcha se
// mandan igual, con o sin --verbose. Esto es solo la consola.
// =============================================================================
#ifndef STM32_COMMON_VERBOSIDAD_H
#define STM32_COMMON_VERBOSIDAD_H

#include <cstdarg>
#include <cstdio>

namespace stm32 {

// true: los avisos salen por stderr. `sim` lo pone a false salvo con --verbose.
inline bool& verbose() {
    static bool v = true;
    return v;
}

// Un aviso por stderr, como `std::fprintf(stderr, ...)`, si `verbose()`.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void avisa_consola(const char* fmt, ...) {
    if (!verbose()) return;
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

} // namespace stm32

#endif // STM32_COMMON_VERBOSIDAD_H
