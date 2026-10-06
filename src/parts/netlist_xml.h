// =============================================================================
// netlist_xml.h — El netlist, LEÍDO de un fichero
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f4xx/stm32f407vg_parts_paso3.md.)
//
// Aquí se cierra el círculo que abrió el paso 2. El formato no se ha inventado
// para esta ocasión: es el que `Netlist::volcar_xml` ya escribía, y que salió de
// migrar la placa de verdad. Por eso este fichero es corto — el trabajo difícil
// (qué entidades hacen falta, que las referencias entre instancias no son
// nodos, que el orden de declaración es semántico) ya estaba hecho.
//
//   <placa nombre="...">
//     <mcu tipo="STM32F407VG" id="u0"         un chip: aporta 154 nodos
//          firmware="a.bin" depuracion="dap" puerto_gdb="3333"/>
//     <nodo id="PA5"/>                        un pin: ya existe
//     <nodo id="n_can" externo="si"/>         hay que crearlo
//     <nodo id="n_p" une="PB9 PD3"/>          dos pads SON este nodo
//     <componente tipo="Led" id="LD2" vf="2.0" r="330" conectada="no">
//       <pin nombre="anodo" nodo="PA5"/>
//       <ref nombre="hilo" componente="can_bus"/>
//     </componente>
//   </placa>
//
// Y el DIBUJO de la placa, para la ventana: `ilustracion="x.svg"` en la raíz
// -relativo al fichero de la placa; sin él, el SVG que se llame como ella- y,
// si hace falta, la tabla de enlaces (doc/parts.md §2.6):
//
//   <placa nombre="nucleo-f446re" ilustracion="nucleo_f446re.svg">
//     <ilustracion>
//       <enlace pieza="LD2" elemento="led-verde" efecto="brillo"/>
//     </ilustracion>
//
// `une` es el único atributo que cambia algo del MCU y no solo de la placa: los
// pads que nombra dejan de crear su propio AnalogNet. Por eso hay que leer el
// fichero ANTES de construir el MCU (véase cableado_desde_netlist en netlist.h);
// leer no construye, y esa es justo la propiedad que lo hace posible.
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

#include <map>
#include <string>
#include "netlist.h"
#include "xml_min.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// UNA PLACA DENTRO DE UN <sistema>. Fuera de un sistema todo esto está vacío y
// la placa se lee exactamente como siempre.
//
// Dentro, cada nombre de la placa se CUALIFICA con su id y una barra: el LED
// `LD2` de la placa `A` es `A/LD2`, su chip `u0` es `A/u0` y su nodo `vcc` es
// `A/vcc`. Así dos placas pueden llamar igual a sus cosas -las dos Nucleo de
// un montaje llevan las dos un `LD2`- y el resto del programa sigue viendo un
// netlist plano con nombres únicos. La barra no puede aparecer en un nombre
// de la placa: es del sistema.
//
// Los pines del chip se escriben dentro de la placa como siempre: `PA5` es el
// del único MCU de ESA placa -aunque el sistema lleve varios- y pasa a ser
// `A/u0.PA5`. Con dos chips en la placa hay que decir cuál (`u1.PA5`), y en
// una placa sin MCU un nombre de pin es un error que lo dice.
// ---------------------------------------------------------------------------
struct ContextoPlaca {
    std::string              id;      // "A"; vacío fuera de un sistema
    std::vector<std::string> mcus;    // los ids de SUS chips, sin cualificar
    bool en_sistema() const { return !id.empty(); }
};

// Un nombre de la placa, cualificado. "" en `err` si todo bien.
inline std::string cualifica(const ContextoPlaca& cx, const std::string& n,
                             std::string& err) {
    err.clear();
    if (!cx.en_sistema()) return n;
    if (n.find('/') != std::string::npos) {
        err = "'" + n + "' lleva '/', y dentro de una placa no puede: la barra "
              "separa la placa del nombre, y los nombres de otra placa solo se "
              "usan en el <sistema>";
        return std::string();
    }
    std::string pref, alim;
    unsigned p = 0, i = 0;
    const bool pad = pad_desde_nombre(n, pref, p, i);
    if (!pad && !alim_desde_nombre(n, pref, alim)) return cx.id + "/" + n;
    const std::string local = pad ? nombre_canonico_pad(n) : n;
    if (!pref.empty()) {
        for (const std::string& m : cx.mcus)
            if (m == pref) return cx.id + "/" + local;
        err = "'" + n + "': la placa " + cx.id + " no lleva ningun MCU llamado '" +
              pref + "'";
        return std::string();
    }
    if (cx.mcus.size() == 1) return cx.id + "/" + cx.mcus[0] + "." + local;
    if (cx.mcus.empty())
        err = "'" + n + "' es una patilla de MCU, y la placa " + cx.id +
              " no lleva ninguno";
    else {
        err = "'" + n + "' es ambiguo: la placa " + cx.id + " lleva " +
              std::to_string(cx.mcus.size()) + " MCUs. Escribe ";
        for (size_t k = 0; k < cx.mcus.size(); ++k)
            err += (k ? (k + 1 == cx.mcus.size() ? " o " : ", ") : "") +
                   cx.mcus[k] + "." + local;
    }
    return std::string();
}

// Rellena `nl` a partir del árbol ya analizado. Devuelve "" si todo bien, o el
// primer problema con su línea. `cx` dice si la placa es una de un <sistema>.
// En `ilus`, si se pide, lo que la placa dice de su dibujo: el fichero tal
// como lo escribe y la tabla de enlaces. La ruta la resuelve quien sabe dónde
// está la placa.
inline std::string netlist_desde_xml(Netlist& nl, const XmlNodo& raiz,
                                     std::string* nombre_placa = nullptr,
                                     ContextoPlaca cx = ContextoPlaca(),
                                     Ilustracion* ilus = nullptr) {
    char pos[48];
    auto donde = [&](const XmlNodo& n) {
        std::snprintf(pos, sizeof pos, "linea %u: ", n.linea);
        return std::string(pos);
    };
    if (raiz.nombre != "placa")
        return donde(raiz) + "el elemento raiz es <" + raiz.nombre +
               ">, se esperaba <placa> o <sistema>";
    if (nombre_placa) *nombre_placa = raiz.attr_o("nombre", "placa");
    // Los atributos de la raíz: hasta el dibujo se aceptaba cualquiera y se
    // ignoraba en silencio, que es justo lo que hace que una errata no se vea
    for (const auto& a : raiz.attrs)
        if (a.first != "nombre" && a.first != "ilustracion" &&
            !(cx.en_sistema() && a.first == "id"))
            return donde(raiz) + "<placa>: atributo desconocido: " + a.first +
                   " (lo que va aqui: nombre e ilustracion)";
    Ilustracion il;
    il.declarada = raiz.attr_o("ilustracion", "");
    if (raiz.tiene("ilustracion") && il.declarada.empty())
        return donde(raiz) + "<placa>: ilustracion vacia";
    std::string eq;
    // El nombre local de algo de la placa: un id (no lleva barra) ...
    auto id_de = [&](const std::string& id) -> std::string {
        if (!cx.en_sistema()) return id;
        if (id.find('/') != std::string::npos) {
            eq = "'" + id + "' lleva '/', y un identificador de la placa no puede";
            return std::string();
        }
        return cx.id + "/" + id;
    };
    // ... o un nodo, con las reglas de `cualifica`
    // `cx.mcus` -los chips de ESTA placa, que deciden qué es `PA5`- lo
    // rellena quien llama, antes, porque también lo necesita después.
    auto nodo_de = [&](const std::string& n) { return cualifica(cx, n, eq); };

    // Pasada cero: los MCUs. Van antes que los nodos porque son quienes los
    // aportan: sin saber cuántos hay no se puede decidir si `PD12` designa un
    // pin concreto o es ambiguo.
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre != "mcu") continue;
        if (!h.tiene("tipo")) return donde(h) + "<mcu> sin atributo tipo";
        if (!h.tiene("id"))   return donde(h) + "<mcu> sin atributo id";
        DeclMcu m;
        m.tipo = h.attr_o("tipo");
        m.id   = id_de(h.attr_o("id"));
        if (!eq.empty()) return donde(h) + eq;
        for (const auto& a : h.attrs) {
            if (a.first == "tipo" || a.first == "id") continue;
            if      (a.first == "firmware")   m.firmware   = a.second;
            else if (a.first == "depuracion") m.depuracion = a.second;
            else if (a.first == "puerto_gdb") {
                const long v = std::atol(a.second.c_str());
                if (v < 0 || v > 65535)
                    return donde(h) + "<mcu id=\"" + m.id + "\">: puerto_gdb "
                           "fuera de rango: " + a.second;
                m.puerto_gdb = unsigned(v);
            } else {
                return donde(h) + "<mcu id=\"" + m.id +
                       "\">: atributo desconocido: " + a.first;
            }
        }
        if (!h.hijos.empty())
            return donde(h) + "<mcu id=\"" + m.id + "\">: un MCU no lleva hijos; "
                   "sus pines existen sin declararlos";
        nl.add_mcu(m);
    }

    // Primera pasada: los nodos. Tienen que estar antes de que nadie los
    // mencione, igual que en el modelo.
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre != "nodo") continue;
        if (!h.tiene("id")) return donde(h) + "<nodo> sin atributo id";
        const std::string id = h.attr_o("id");
        const std::string ex = h.attr_o("externo", "no");
        const std::string bs = h.attr_o("bus", "no");
        const std::string un = h.attr_o("une", "");
        if (ex != "si" && ex != "no")
            return donde(h) + "<nodo id=\"" + id + "\">: externo debe ser si o no";
        if (bs != "si" && bs != "no")
            return donde(h) + "<nodo id=\"" + id + "\">: bus debe ser si o no";
        for (const auto& a : h.attrs)
            if (a.first != "id" && a.first != "externo" && a.first != "bus" &&
                a.first != "une")
                return donde(h) + "<nodo id=\"" + id + "\">: atributo desconocido: " +
                       a.first;
        const std::string qid = nodo_de(id);
        if (!eq.empty()) return donde(h) + "<nodo>: " + eq;
        nl.nodo_declarado(qid);
        // En una placa SIN MCU de un sistema, todo nodo es de la placa: el
        // mismo criterio que una placa suelta sin MCU (Netlist::pon_sin_mcu).
        if (ex == "si" || (cx.en_sistema() && cx.mcus.empty())) nl.nodo_externo(qid);
        if (bs == "si") nl.nodo_bus(qid);
        // `une` es la lista de pads que SON este nodo, separados por espacios.
        // No es una conexión más: dice que esos pads no van a crear su propio
        // AnalogNet, y por eso hay que leerlo antes de construir el MCU. La
        // comprobación de que cada nombre es un pad de verdad la hace
        // `Netlist::valida()`; aquí solo se separa la lista.
        if (!un.empty()) {
            std::vector<std::string> pads;
            std::string t;
            for (size_t k = 0; k <= un.size(); ++k) {
                const char c = (k == un.size()) ? ' ' : un[k];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    if (!t.empty()) { pads.push_back(t); t.clear(); }
                } else t.push_back(c);
            }
            if (pads.size() < 2)
                return donde(h) + "<nodo id=\"" + id + "\">: une necesita al menos "
                       "dos pads separados por espacios";
            for (std::string& pd : pads) {
                pd = nodo_de(pd);
                if (!eq.empty()) return donde(h) + "<nodo id=\"" + id + "\">: " + eq;
            }
            nl.nodo_une(qid, pads);         // implica externo: lo crea la placa
        }
    }

    // Segunda pasada: los componentes, EN ORDEN. El orden del fichero es el
    // orden de construccion, y de eso depende que una referencia funcione.
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre == "nodo" || h.nombre == "mcu" || h.nombre == "ilustracion") continue;
        if (h.nombre != "componente")
            return donde(h) + "elemento desconocido dentro de <placa>: <" +
                   h.nombre + ">";
        if (!h.tiene("tipo")) return donde(h) + "<componente> sin atributo tipo";
        if (!h.tiene("id"))   return donde(h) + "<componente> sin atributo id";
        const std::string tipo = h.attr_o("tipo");
        const std::string id   = id_de(h.attr_o("id"));
        if (!eq.empty()) return donde(h) + "<componente>: " + eq;

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
                const std::string nd = nodo_de(c.attr_o("nodo"));
                if (!eq.empty()) return donde(c) + id + ": " + eq;
                in.pin(c.attr_o("nombre").c_str(), nd);
            } else if (c.nombre == "ref") {
                if (!c.tiene("nombre") || !c.tiene("componente"))
                    return donde(c) + id + ": <ref> necesita nombre y componente";
                const std::string rf = id_de(c.attr_o("componente"));
                if (!eq.empty()) return donde(c) + id + ": " + eq;
                in.ref(c.attr_o("nombre").c_str(), rf);
            } else {
                return donde(c) + id + ": elemento desconocido dentro de " +
                       "<componente>: <" + c.nombre + ">";
            }
        }
    }

    // Tercera pasada: la tabla de enlaces del dibujo, con las piezas ya
    // declaradas. Una pieza que no existe es un error de la placa, como
    // cualquier otra referencia rota.
    bool hay_tabla = false;
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre != "ilustracion") continue;
        if (hay_tabla) return donde(h) + "<ilustracion> repetida: una tabla por placa";
        hay_tabla = true;
        if (!h.attrs.empty())
            return donde(h) + "<ilustracion> no lleva atributos: el fichero se dice en "
                   "<placa ilustracion=\"...\">";
        for (const XmlNodo& e : h.hijos) {
            if (e.nombre != "enlace")
                return donde(e) + "dentro de <ilustracion> solo va <enlace>, no <" +
                       e.nombre + ">";
            for (const auto& a : e.attrs)
                if (a.first != "pieza" && a.first != "elemento" && a.first != "efecto")
                    return donde(e) + "<enlace>: atributo desconocido: " + a.first;
            if (!e.tiene("pieza") || !e.tiene("elemento"))
                return donde(e) + "<enlace> necesita pieza= y elemento=";
            EnlaceIlustracion en;
            en.pieza = e.attr_o("pieza");
            en.elemento = e.attr_o("elemento");
            en.efecto = e.attr_o("efecto", "");
            if (!en.efecto.empty() && en.efecto != "brillo" && en.efecto != "hundido" &&
                en.efecto != "ninguno")
                return donde(e) + "<enlace pieza=\"" + en.pieza + "\">: efecto '" +
                       en.efecto + "' desconocido (brillo, hundido o ninguno)";
            const std::string q = id_de(en.pieza);
            if (!eq.empty()) return donde(e) + "<enlace>: " + eq;
            if (!nl.busca(q))
                return donde(e) + "<enlace>: la placa no tiene ninguna pieza '" + en.pieza +
                       "'";
            for (const EnlaceIlustracion& o : il.enlaces)
                if (o.pieza == en.pieza)
                    return donde(e) + "<enlace>: la pieza '" + en.pieza +
                           "' ya esta en la tabla";
            il.enlaces.push_back(en);
        }
    }
    if (ilus) *ilus = il;
    return std::string();
}

// Dónde está un fichero que una placa nombra: relativo a su carpeta, salvo
// que sea absoluto
inline std::string junto_a(const std::string& carpeta, const std::string& f);
// El SVG que se llama como la placa: `placas/nucleo.xml` -> `placas/nucleo.svg`
inline std::string svg_hermano(const std::string& ruta_xml) {
    std::string r = ruta_xml;
    const size_t b = r.find_last_of("/\\");
    const size_t p = r.rfind('.');
    if (p != std::string::npos && (b == std::string::npos || p > b)) r.erase(p);
    return r + ".svg";
}

// Lee un fichero entero. El error, si lo hay, ya lleva fichero y línea.
inline std::string netlist_desde_fichero(Netlist& nl, const std::string& ruta,
                                         std::string* nombre_placa = nullptr) {
    XmlLector lx;
    if (!lx.parse_fichero(ruta)) return lx.error();
    Ilustracion il;
    const std::string e = netlist_desde_xml(nl, lx.raiz(), nombre_placa, ContextoPlaca(), &il);
    if (!e.empty()) return ruta + ": " + e;
    std::string carpeta;
    const size_t b = ruta.find_last_of("/\\");
    if (b != std::string::npos) carpeta = ruta.substr(0, b + 1);
    il.ruta = il.declarada.empty() ? svg_hermano(ruta) : junto_a(carpeta, il.declarada);
    nl.pon_ilustracion(il);
    return std::string();
}

inline std::string netlist_desde_texto(Netlist& nl, const std::string& texto,
                                       std::string* nombre_placa = nullptr) {
    XmlLector lx;
    if (!lx.parse(texto)) return lx.error();
    Ilustracion il;
    const std::string e = netlist_desde_xml(nl, lx.raiz(), nombre_placa, ContextoPlaca(), &il);
    il.ruta = il.declarada;              // sin fichero, respecto a donde se esté
    nl.pon_ilustracion(il);
    return e;
}

// ---------------------------------------------------------------------------
// UN <sistema>: varias placas y cómo se enchufan.
//
//   <sistema nombre="nucleo-y-shield">
//     <placa id="A" fichero="nucleo_f446re.xml"/>     una placa, de su fichero
//     <placa id="B" nombre="shield"> ... </placa>     u otra, escrita aquí
//     <acopla a="A/CN9" b="B/J1"/>                    dos conectores enchufados
//     <acopla a="A/CN5" b="B/J2" espejo="si"/>        cara a cara
//     <hilo a="A/CN10.3" b="B/J3.5"/>                 un cable, pin a pin
//     <mcu ref="A/u0" firmware="demo.bin"/>           lo que el montaje decide
//   </sistema>
//
// LAS PLACAS NO SABEN DÓNDE LAS ENCHUFAN: el fichero de una Nucleo sirve igual
// sola que con cualquier shield encima, y por eso lo que une una placa con
// otra se dice aquí y no dentro de ninguna. Un fichero se resuelve respecto a
// la carpeta del sistema. Lo que el sistema puede cambiar de una placa es, de
// momento, lo de sus chips -firmware, depuración, puerto de GDB-: el montaje
// decide qué programa corre, la placa no.
//
// Leer el sistema APLANA: sale un único `Netlist` con todo lo de todas las
// placas cualificado (`A/LD2`), y los acoples y los hilos apuntados para que
// `resuelve_alias()` los convierta en nodos. Ese es el punto de diseño: del
// lector en adelante, nadie sabe que hay placas.
// ---------------------------------------------------------------------------
inline bool id_de_placa_valido(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}

inline std::string carpeta_de(const std::string& ruta) {
    const size_t p = ruta.find_last_of("/\\");
    return p == std::string::npos ? std::string() : ruta.substr(0, p + 1);
}
inline bool ruta_absoluta(const std::string& r) {
    return !r.empty() && (r[0] == '/' || r[0] == '\\' ||
                          (r.size() > 1 && r[1] == ':'));
}
inline std::string junto_a(const std::string& carpeta, const std::string& f) {
    return ruta_absoluta(f) ? f : carpeta + f;
}

inline std::string sistema_desde_xml(Netlist& nl, const XmlNodo& raiz,
                                     const std::string& carpeta,
                                     std::string* nombre = nullptr) {
    char pos[48];
    auto donde = [&](const XmlNodo& n) {
        std::snprintf(pos, sizeof pos, "linea %u: ", n.linea);
        return std::string(pos);
    };
    if (nombre) *nombre = raiz.attr_o("nombre", "sistema");
    for (const auto& a : raiz.attrs)
        if (a.first != "nombre")
            return donde(raiz) + "<sistema>: atributo desconocido: " + a.first;
    std::vector<PlacaDeSistema> placas;
    std::map<std::string, ContextoPlaca> contexto;

    // Primero TODAS las placas: un acople o un hilo pueden nombrar cualquiera
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre != "placa") continue;
        const std::string id = h.attr_o("id");
        if (!h.tiene("id")) return donde(h) + "<placa> sin atributo id dentro de un <sistema>";
        if (!id_de_placa_valido(id))
            return donde(h) + "<placa id=\"" + id + "\">: el id de una placa son letras, "
                   "cifras, '_' o '-' (ni '/', que separa la placa del nombre, ni '.')";
        if (contexto.count(id)) return donde(h) + "<placa id=\"" + id + "\">: id repetido";
        PlacaDeSistema ps;
        ps.id = id;
        ContextoPlaca cx;
        cx.id = id;
        std::string e;
        if (h.tiene("fichero")) {
            if (!h.hijos.empty())
                return donde(h) + "<placa id=\"" + id + "\">: o fichero=, o la placa "
                       "escrita dentro; las dos cosas no";
            for (const auto& a : h.attrs)
                if (a.first != "id" && a.first != "fichero" && a.first != "ilustracion")
                    return donde(h) + "<placa id=\"" + id + "\" fichero=...>: atributo "
                           "desconocido: " + a.first + " (lo de la placa va en su fichero)";
            ps.fichero = h.attr_o("fichero");
            const std::string ruta = ruta_absoluta(ps.fichero) ? ps.fichero
                                                               : carpeta + ps.fichero;
            XmlLector lx;
            if (!lx.parse_fichero(ruta))
                return donde(h) + "<placa id=\"" + id + "\">: " + lx.error();
            if (lx.raiz().nombre == "sistema")
                return donde(h) + "<placa id=\"" + id + "\">: " + ruta + " es un <sistema>, "
                       "y un sistema dentro de otro todavia no se puede";
            for (const XmlNodo& m : lx.raiz().hijos)
                if (m.nombre == "mcu" && m.tiene("id")) cx.mcus.push_back(m.attr_o("id"));
            e = netlist_desde_xml(nl, lx.raiz(), &ps.nombre, cx, &ps.ilustracion);
            if (!e.empty()) return "placa " + id + " (" + ruta + "): " + e;
            // El dibujo: el que diga el MONTAJE para esta placa -y entonces la
            // tabla de la placa, que es de SU dibujo, no vale-; si no, el que
            // diga la placa, junto a ella; si tampoco, el que se llame como ella
            Ilustracion& il = ps.ilustracion;
            if (h.tiene("ilustracion")) {
                il.declarada = h.attr_o("ilustracion");
                if (il.declarada.empty())
                    return donde(h) + "<placa id=\"" + id + "\">: ilustracion vacia";
                il.ruta = junto_a(carpeta, il.declarada);
                il.enlaces.clear();
            } else if (!il.declarada.empty()) {
                il.ruta = junto_a(carpeta_de(ruta), il.declarada);
            } else {
                il.ruta = svg_hermano(ruta);
            }
        } else {
            for (const XmlNodo& m : h.hijos)
                if (m.nombre == "mcu" && m.tiene("id")) cx.mcus.push_back(m.attr_o("id"));
            e = netlist_desde_xml(nl, h, &ps.nombre, cx, &ps.ilustracion);
            if (!e.empty()) return "placa " + id + ": " + e;
            // Escrita aquí, su dibujo es relativo al sistema, y solo si lo dice
            if (!ps.ilustracion.declarada.empty())
                ps.ilustracion.ruta = junto_a(carpeta, ps.ilustracion.declarada);
            if (!h.tiene("nombre")) ps.nombre = id;
        }
        contexto[id] = cx;
        placas.push_back(ps);
    }
    if (placas.empty()) return donde(raiz) + "un <sistema> sin ninguna <placa>";

    // `A/CN7` -> la placa A y su conector CN7; `A/PA5` -> el pad de A
    auto parte = [&](const std::string& s, std::string& placa,
                     std::string& resto) -> std::string {
        const size_t b = s.find('/');
        if (b == std::string::npos || b == 0 || b + 1 >= s.size())
            return "'" + s + "': se esperaba placa/nombre, por ejemplo A/CN7";
        placa = s.substr(0, b);
        resto = s.substr(b + 1);
        if (!contexto.count(placa)) {
            std::string l;
            for (const PlacaDeSistema& p : placas) l += (l.empty() ? "" : ", ") + p.id;
            return "'" + s + "': no hay ninguna placa '" + placa + "' (hay " + l + ")";
        }
        if (resto.find('/') != std::string::npos)
            return "'" + s + "': una sola barra, entre la placa y el nombre";
        return std::string();
    };
    for (const XmlNodo& h : raiz.hijos) {
        if (h.nombre == "placa") continue;
        if (h.nombre == "acopla") {
            // Dos conectores con a= y b=, o los que sean -una pila PC/104- con
            // conectores="A/J1 B/J1 C/J1". Las dos formas no a la vez.
            for (const auto& a : h.attrs)
                if (a.first != "a" && a.first != "b" && a.first != "espejo" &&
                    a.first != "conectores")
                    return donde(h) + "<acopla>: atributo desconocido: " + a.first;
            const bool lista = h.tiene("conectores");
            if (lista && (h.tiene("a") || h.tiene("b")))
                return donde(h) + "<acopla>: o a= y b=, o conectores=; las dos cosas no";
            if (!lista && (!h.tiene("a") || !h.tiene("b")))
                return donde(h) + "<acopla> necesita a= y b=, los dos conectores, o "
                       "conectores=\"A/J1 B/J1 C/J1\" para una pila";
            const std::string es = h.attr_o("espejo", "no");
            if (es != "si" && es != "no")
                return donde(h) + "<acopla>: espejo debe ser si o no";
            std::vector<std::string> nombres;
            if (lista) {
                const std::string l = h.attr_o("conectores");
                std::string t;
                for (size_t k = 0; k <= l.size(); ++k) {
                    const char c = k == l.size() ? ' ' : l[k];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
                        if (!t.empty()) { nombres.push_back(t); t.clear(); }
                    } else t.push_back(c);
                }
                if (nombres.size() < 2)
                    return donde(h) + "<acopla conectores=...>: hacen falta al menos dos, "
                           "separados por espacios";
            } else {
                nombres = {h.attr_o("a"), h.attr_o("b")};
            }
            std::vector<std::string> q;
            for (const std::string& n : nombres) {
                std::string pl, rs;
                const std::string e = parte(n, pl, rs);
                if (!e.empty()) return donde(h) + "<acopla>: " + e;
                q.push_back(pl + "/" + rs);
            }
            nl.acopla(q, es == "si");
        } else if (h.nombre == "hilo") {
            for (const auto& a : h.attrs)
                if (a.first != "a" && a.first != "b")
                    return donde(h) + "<hilo>: atributo desconocido: " + a.first;
            if (!h.tiene("a") || !h.tiene("b"))
                return donde(h) + "<hilo> necesita a= y b=, los dos nodos";
            std::string q[2];
            const char* lado[2] = {"a", "b"};
            for (int k = 0; k < 2; ++k) {
                std::string pl, rs;
                std::string e = parte(h.attr_o(lado[k]), pl, rs);
                if (e.empty()) q[k] = cualifica(contexto[pl], rs, e);
                if (!e.empty()) return donde(h) + "<hilo>: " + e;
            }
            nl.hilo(q[0], q[1]);
        } else if (h.nombre == "mcu") {
            if (!h.tiene("ref"))
                return donde(h) + "<mcu> dentro de un <sistema> lleva ref=\"placa/id\": "
                       "los chips se declaran en su placa";
            DeclMcu* m = nl.mcu_mut(h.attr_o("ref"));
            if (!m) return donde(h) + "<mcu ref=\"" + h.attr_o("ref") + "\">: no hay ningun "
                        "MCU con ese nombre (se escriben placa/id, como A/u0)";
            for (const auto& a : h.attrs) {
                if (a.first == "ref") continue;
                if      (a.first == "firmware")   m->firmware   = a.second;
                else if (a.first == "depuracion") m->depuracion = a.second;
                else if (a.first == "puerto_gdb") {
                    const long v = std::atol(a.second.c_str());
                    if (v < 0 || v > 65535)
                        return donde(h) + "<mcu ref=...>: puerto_gdb fuera de rango: " +
                               a.second;
                    m->puerto_gdb = unsigned(v);
                } else
                    return donde(h) + "<mcu ref=...>: atributo desconocido: " + a.first +
                           " (el tipo lo dice la placa, o --mcu)";
            }
        } else {
            return donde(h) + "elemento desconocido dentro de <sistema>: <" + h.nombre +
                   ">. Lo que va aqui: <placa>, <acopla>, <hilo> y <mcu ref>";
        }
    }
    nl.pon_placas(placas);
    return std::string();
}

// Una placa o un sistema, segun lo que haya en el fichero.
inline std::string placa_o_sistema_desde_fichero(Netlist& nl, const std::string& ruta,
                                                 std::string* nombre = nullptr) {
    XmlLector lx;
    if (!lx.parse_fichero(ruta)) return lx.error();
    if (lx.raiz().nombre == "sistema") {
        const std::string e = sistema_desde_xml(nl, lx.raiz(), carpeta_de(ruta), nombre);
        return e.empty() ? e : ruta + ": " + e;
    }
    Ilustracion il;
    const std::string e = netlist_desde_xml(nl, lx.raiz(), nombre, ContextoPlaca(), &il);
    if (!e.empty()) return ruta + ": " + e;
    il.ruta = il.declarada.empty() ? svg_hermano(ruta) : junto_a(carpeta_de(ruta), il.declarada);
    nl.pon_ilustracion(il);
    return std::string();
}

} // namespace stm32
#endif // STM32_PARTS_NETLIST_XML_H
