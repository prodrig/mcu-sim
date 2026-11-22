// =============================================================================
// tics.h — Cuántos tics de un reloj caben en un tiempo simulado, con enteros
//
// Un contador que se calcula del tiempo simulado (el `mtime` de un CLINT, el
// de un temporizador que nadie incrementa) tiene que dar floor(dt · hz). Con
// `dt.to_seconds() * hz` en coma flotante, en el tic justo el producto puede
// salir 99,999999 en lugar de 100 y truncarse a 99, y SI sale así depende de
// cómo calcule SystemC `to_seconds()`: la 2.3 y la 3.0 no lo hacen igual. El
// resultado era que `testrv` acababa 30 ns antes con SystemC 3.0.2 que con la
// 2.3.4 (el CI de ubuntu-latest, ya Ubuntu 26.04, y de macOS, 2026-11-16).
//
// Aquí todo va en unidades de la resolución de SystemC (`sc_time::value()`) y
// en aritmética entera de 128 bits, así que el resultado es exacto y el mismo
// con cualquier SystemC. Solo una frecuencia con decimales (que no sea un
// número entero de Hz) vuelve a la coma flotante.
// =============================================================================
#ifndef STM32_COMMON_TICS_H
#define STM32_COMMON_TICS_H

#include <systemc>
#include <cmath>
#include <cstdint>

namespace stm32 {

// Unidades de la resolución de SystemC en un segundo (1e12 con la de 1 ps)
inline uint64_t unidades_por_segundo() {
    static const uint64_t r = sc_core::sc_time(1, sc_core::SC_SEC).value();
    return r;
}

// ¿Es `hz` un número entero de Hz (y cabe en 64 bits)?
inline bool hz_entero(double hz) { return hz > 0 && hz < 1.8e19 && std::floor(hz) == hz; }

// floor(dt · hz): los tics enteros de un reloj de `hz` en `dt`
inline uint64_t tics_en(const sc_core::sc_time& dt, double hz) {
    if (!(hz > 0)) return 0;
    if (!hz_entero(hz)) return uint64_t(dt.to_seconds() * hz);
    using u128 = unsigned __int128;
    return uint64_t(u128(dt.value()) * u128(uint64_t(hz)) / unidades_por_segundo());
}

// El tiempo más corto en el que un reloj de `hz` da `n` tics: ceil(n / hz),
// redondeado hacia arriba a la resolución (así, en ese instante, `tics_en` ya
// los cuenta). Satura en `tope` para no desbordar.
inline sc_core::sc_time tiempo_de_tics(uint64_t n, double hz,
                                       const sc_core::sc_time& tope = sc_core::sc_time(1e6, sc_core::SC_SEC)) {
    if (!(hz > 0)) return tope;
    if (!hz_entero(hz)) {
        const double s = double(n) / hz;
        return s < tope.to_seconds() ? sc_core::sc_time(s, sc_core::SC_SEC) : tope;
    }
    using u128 = unsigned __int128;
    const u128 h = uint64_t(hz);
    const u128 v = (u128(n) * unidades_por_segundo() + h - 1) / h;
    return v < u128(tope.value()) ? sc_core::sc_time::from_value(uint64_t(v)) : tope;
}

} // namespace stm32
#endif
