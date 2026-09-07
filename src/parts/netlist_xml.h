// =============================================================================
// netlist_xml.h — El netlist, LEÍDO de un fichero
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso3.md.)
//
// Aquí se cierra el círculo que abrió el paso 2. El formato no se ha inventado
// para esta ocasión: es el que `Netlist::volcar_xml` ya escribía, y que salió de
// migrar la placa de verdad. Por eso este fichero es corto — el trabajo difícil
// (qué entidades hacen falta, que las referencias entre instancias no son
// nodos, que el orden de declaración es semántico) ya estaba hecho.
//
//   <placa nombre="...">
//     <nodo id="PA5"/>                        un pin: ya existe
//     <nodo id="n_can" externo="si"/>         hay que crearlo
//     <componente tipo="Led" id="LD2" vf="2.0" r="330" conectada="no">
//       <pin nombre="anodo" nodo="PA5"/>
//       <ref nombre="hilo" componente="can_bus"/>
//     </componente>
//   </placa>
//
// Todo atributo de <componente> que no sea `tipo`, `id` o `conectada` es un
// PARÁMETRO de la pieza, tal cual, sin lista blanca. Es deliberado: la factoría
// ya decide qué parámetros mira y con qué valor por omisión, y duplicar esa
// lista aquí solo daría dos sitios donde equivocarse.
//
// Leer NO construye. Devuelve un `Netlist` declarado, que hay que validar y
// construir como cualquier otro — y en la elaboración, porque la de SystemC es
// estática.
// =============================================================================
#ifndef STM32_PARTS_NETLIST_XML_H
#define STM32_PARTS_NETLIST_XML_H

#include <string>
#include "netlist.h"
#include "xml_min.h"

namespace stm32 {

// Rellena `nl` a partir del árbol ya analizado. Devuelve "" si todo bien, o el
// primer problema con su línea.
inline std::string netlist_desde_xml(Netlist& nl, const XmlNodo& raiz,
                                     std::string* nombre_placa = nullptr) {
    char pos[48];
    auto donde = [&](const XmlNodo& n) {
        std::snprintf(pos, sizeof pos, "linea %u: ", n.linea);
        return std::string(pos);
    };
    if (raiz.nombre != "placa")
        return donde(raiz) + "el elemento raiz es <" + raiz.nombre +
               ">, se esperaba <placa>";
    if (nombre_placa) *nombre_placa = raiz.attr_o("nombre", "placa");

    // Primera pasada: los nodos. Tienen que estar antes de que nadie los
    // mencione, igual que en el modelo.
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre != "nodo") continue;
        if (!h.tiene("id")) return donde(h) + "<nodo> sin atributo id";
        const std::string id = h.attr_o("id");
        const std::string ex = h.attr_o("externo", "no");
        const std::string bs = h.attr_o("bus", "no");
        if (ex != "si" && ex != "no")
            return donde(h) + "<nodo id=\"" + id + "\">: externo debe ser si o no";
        if (bs != "si" && bs != "no")
            return donde(h) + "<nodo id=\"" + id + "\">: bus debe ser si o no";
        for (const auto& a : h.attrs)
            if (a.first != "id" && a.first != "externo" && a.first != "bus")
                return donde(h) + "<nodo id=\"" + id + "\">: atributo desconocido: " +
                       a.first;
        if (ex == "si") nl.nodo_externo(id);
        if (bs == "si") nl.nodo_bus(id);
    }

    // Segunda pasada: los componentes, EN ORDEN. El orden del fichero es el
    // orden de construccion, y de eso depende que una referencia funcione.
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre == "nodo") continue;
        if (h.nombre != "componente")
            return donde(h) + "elemento desconocido dentro de <placa>: <" +
                   h.nombre + ">";
        if (!h.tiene("tipo")) return donde(h) + "<componente> sin atributo tipo";
        if (!h.tiene("id"))   return donde(h) + "<componente> sin atributo id";
        const std::string tipo = h.attr_o("tipo");
        const std::string id   = h.attr_o("id");

        Instancia& in = nl.add(tipo.c_str(), id.c_str());
        for (const auto& a : h.attrs) {
            if (a.first == "tipo" || a.first == "id") continue;
            if (a.first == "conectada") {
                if (a.second != "si" && a.second != "no")
                    return donde(h) + id + ": conectada debe ser si o no";
                if (a.second == "no") in.desconectada();
                continue;
            }
            in.par(a.first.c_str(), a.second);      // cualquier otro: parametro
        }
        for (const XmlNodo& c : h.hijos) {
            if (c.nombre == "pin") {
                if (!c.tiene("nombre") || !c.tiene("nodo"))
                    return donde(c) + id + ": <pin> necesita nombre y nodo";
                in.pin(c.attr_o("nombre").c_str(), c.attr_o("nodo"));
            } else if (c.nombre == "ref") {
                if (!c.tiene("nombre") || !c.tiene("componente"))
                    return donde(c) + id + ": <ref> necesita nombre y componente";
                in.ref(c.attr_o("nombre").c_str(), c.attr_o("componente"));
            } else {
                return donde(c) + id + ": elemento desconocido dentro de " +
                       "<componente>: <" + c.nombre + ">";
            }
        }
    }
    return std::string();
}

// Lee un fichero entero. El error, si lo hay, ya lleva fichero y línea.
inline std::string netlist_desde_fichero(Netlist& nl, const std::string& ruta,
                                         std::string* nombre_placa = nullptr) {
    XmlLector lx;
    if (!lx.parse_fichero(ruta)) return lx.error();
    const std::string e = netlist_desde_xml(nl, lx.raiz(), nombre_placa);
    return e.empty() ? e : ruta + ": " + e;
}

inline std::string netlist_desde_texto(Netlist& nl, const std::string& texto,
                                       std::string* nombre_placa = nullptr) {
    XmlLector lx;
    if (!lx.parse(texto)) return lx.error();
    return netlist_desde_xml(nl, lx.raiz(), nombre_placa);
}

} // namespace stm32
#endif // STM32_PARTS_NETLIST_XML_H
