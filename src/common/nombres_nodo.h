// =============================================================================
// nombres_nodo.h — El nombre de ESQUEMÁTICO de un nodo analógico
//
// Dentro del modelo un nodo de pin se llama `net_A5` y vive colgado del PinMux
// de su MCU, así que su nombre jerárquico completo es `u0.pinmux.net_A5`. Fuera
// —en el netlist, en el XML, en el inventario— se llama `PA5`, que es como lo
// llama el esquemático y como lo escribe quien describe la placa.
//
// Traducir de lo uno a lo otro parece trivial y tiene una trampa. La primera
// versión usaba `sc_object::basename()`, que devuelve el nombre SIN la
// jerarquía: con un solo MCU funciona, y con dos el pad PA5 de `u0` y el de
// `u1` se vuelcan LOS DOS como `PA5`. No es un error que salte: es un volcado
// que miente, que es peor.
//
// De ahí las dos cosas que hay aquí:
//
//   * `nombre_nodo()` parte del nombre JERÁRQUICO y, cuando hay más de un MCU,
//     cualifica el pad con el nombre del módulo que lo contiene: `u0.PA5`. Es
//     exactamente la regla de compatibilidad del análisis
//     (doc/stm32f4xx/stm32f407vg_multi_mcu.md, §3): con un MCU el nombre desnudo es
//     suficiente y no hace falta migrar nada; con dos deja de serlo.
//
//   * `n_mcus()`, que es cómo se sabe cuántos hay. Y aquí hay un matiz que
//     costó un rato entender: lo que crea ambigüedad no es cuántos MCU se han
//     CONSTRUIDO, sino cuántos están EN LA PLACA. Un chip que existe como
//     objeto y no se ha dado de alta en ningún `NodeMap` no puede hacer
//     ambiguo el nombre `PA5`, porque ninguna pieza se le puede soldar.
//
//     La distinción apareció con el F446: el banco de pruebas construye uno
//     entero para hacerle preguntas —qué decodifica, qué IDCODE tiene— sin
//     ponerlo en la placa, y contando construcciones eso renombraba los 154
//     pads del F407 a `dut.PA0` y dejaba la placa del banco sin validar. Se
//     cuenta, por tanto, en `NodeMap::registra_mcu()`, que es exactamente el
//     momento en que un chip se sube a una placa, y por puntero, para que dar
//     de alta el mismo chip dos veces —con prefijo y desnudo, que es lo que
//     hace `sim` cuando solo hay uno— siga contando uno.
//
// Vive en `common/` y no en `parts/` porque lo necesitan los dos lados: las
// piezas externas para nombrar sus terminales, y los pines para contarse.
// =============================================================================
#ifndef STM32_COMMON_NOMBRES_NODO_H
#define STM32_COMMON_NOMBRES_NODO_H

#include <systemc>
#include <string>
#include <set>
#include "analog_net.h"

namespace stm32 {

// Los MCU que están EN UNA PLACA, por puntero a su PinMux. Los apunta
// `NodeMap::registra_mcu()`; nadie más debería tocarlo.
inline std::set<const void*>& mcus_en_placa() {
    static std::set<const void*> s;
    return s;
}
inline void apunta_mcu_en_placa(const void* pinmux) {
    mcus_en_placa().insert(pinmux);
}
inline unsigned n_mcus() { return unsigned(mcus_en_placa().size()); }

// Los segmentos de un nombre jerárquico de SystemC, del último hacia atrás.
// `k = 0` es la hoja, `k = 1` su padre, `k = 2` el abuelo. Cadena vacía si no
// hay tantos.
inline std::string segmento_desde_el_final(const std::string& s, unsigned k) {
    size_t fin = s.size();               // un pasado del final del segmento
    for (;;) {
        if (fin == 0) return std::string();
        const size_t p   = s.rfind('.', fin - 1);
        const size_t ini = (p == std::string::npos) ? 0 : p + 1;
        if (k == 0) return s.substr(ini, fin - ini);
        if (p == std::string::npos) return std::string();
        fin = p;
        --k;
    }
}

// El nombre con el que este nodo sale al netlist.
//
//   dut.pinmux.net_A5   ->  PA5          (un solo MCU)
//   u1.pinmux.net_A5    ->  u1.PA5       (dos o más)
//   tb.n_can            ->  n_can        (un nodo que no es un pad)
//   dut.pwr_pads.vdd    ->  vdd
inline std::string nombre_nodo(const analog_net_if& n) {
    const sc_core::sc_object* o = dynamic_cast<const sc_core::sc_object*>(&n);
    if (!o) return "?";
    const std::string full = o->name();
    const std::string hoja = segmento_desde_el_final(full, 0);
    if (hoja.rfind("net_", 0) != 0) return hoja;   // no es un pad: su nombre basta
    const std::string pad = "P" + hoja.substr(4);
    if (n_mcus() <= 1) return pad;                 // sin ambigüedad posible
    // El MCU es el abuelo del nodo: <mcu>.pinmux.net_A5
    const std::string mcu = segmento_desde_el_final(full, 2);
    return mcu.empty() ? pad : mcu + "." + pad;
}

} // namespace stm32
#endif // STM32_COMMON_NOMBRES_NODO_H
