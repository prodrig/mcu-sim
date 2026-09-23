// ===========================================================================
// common/guarda_tick.h — la guarda de redondeo de los contadores que se
//                        interpolan a partir del tiempo simulado.
//
// EL PATRON. Varios periféricos no cuentan flanco a flanco —sería carísimo—
// sino que calculan cuántos ticks han pasado:
//
//     const double dt = (sc_time_stamp() - t_ref_).to_seconds();
//     const uint64_t n = uint64_t(dt * f);        // o dt / periodo
//
// EL PROBLEMA. Ese `double` vale, en este modelo, hasta decenas de millones
// de ticks, y un `double` tiene 2,2e-16 de precisión RELATIVA: a 11,6
// millones de ticks son unos **3e-9 ticks de error absoluto**. Cuando el
// instante cae justo en un tick —que es el caso normal, porque los relojes
// son periódicos— el producto no vale `n` exacto sino `n ± 3e-9`, y el
// truncamiento lo convierte en `n` o en `n-1` según de qué lado caiga.
//
// DE QUÉ LADO CAE NO ES ESTABLE. Depende de la biblioteca: `to_seconds()` de
// 69 000 000 000 ps devuelve
//
//     SystemC 2.3.4 → 0,069000000000000005773   (por arriba)
//     SystemC 3.0.2 → 0,068999999999999991895   (por abajo)
//
// Un ULP, en direcciones opuestas. Con eso, el mismo modelo cuenta un tick
// distinto según la versión de SystemC. Dos consecuencias reales, las dos
// encontradas el 2026-09-23:
//
//   * el **SysTick** se quedaba un tick corto con la 3.0.2 y `tick_proc`
//     entraba en un bucle de ciclos delta infinito: la suite no terminaba;
//   * el **IWDG** ladraba **un tick antes de su plazo** con la 2.3.4 —a los
//     200,0 ms de un plazo de 201,0— y nadie lo vio porque la comprobación
//     que lo mira tiene un 5 % de tolerancia.
//
// O sea que el número que el proyecto llevaba meses llamando «invariante»
// llevaba dentro un error de redondeo.
//
// LA GUARDA. Sumar 1e-6 de tick antes de truncar. No es un número mágico: es
// cinco órdenes de magnitud mayor que el error del `double` (3e-9) y seis
// órdenes MENOR que un tick, así que absorbe el ruido de representación y no
// puede adelantar un tick de verdad. A 168 MHz, 1e-6 de tick son seis
// milésimas de picosegundo: por debajo de la resolución del simulador, que es
// el picosegundo. No puede observarlo nadie.
//
// Lo que había antes en `scs.h` era `0.5e-9`, **más pequeño que el propio
// error que pretendía absorber**, y en `watchdog.h` no había nada.
// ===========================================================================
#ifndef MCU_SIM_COMMON_GUARDA_TICK_H
#define MCU_SIM_COMMON_GUARDA_TICK_H

namespace stm32 {

constexpr double GUARDA_TICK = 1e-6;

}  // namespace stm32

#endif  // MCU_SIM_COMMON_GUARDA_TICK_H
