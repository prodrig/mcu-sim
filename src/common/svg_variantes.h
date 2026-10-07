// =============================================================================
// svg_variantes.h — Las VARIANTES de un dibujo de placa
//
// Hay piezas cuyo aspecto depende de cómo están montadas, y no de lo que pasa
// en la simulación: un jumper enseña su puente donde lo han puesto. El dibujo
// las lleva TODAS, cada una con un id `PIEZA@valor` -`JP1@5V-VCC`,
// `JP1@VCC-3.3V`, `JP1@no`-, y antes de mandarlo a la ventana se dejan solo las
// que corresponden a la placa: de las de una pieza con variante, la suya, y las
// demás fuera, con todo lo que llevan dentro. La ventana recibe un SVG normal y
// no sabe nada de esto (Netlist::variante dice la de cada pieza).
//
// Y LOS RÓTULOS: un texto del dibujo que dice algo que solo se sabe al lanzar
// -el puerto de un puente serie, que la línea de órdenes puede cambiar-. Un
// elemento con id `PIEZA#campo` -`VCP#destino`- y solo texto dentro se queda
// con el texto que le toca (`pon_rotulos`); lo que lleva escrito es lo que se
// ve si nadie lo cambia.
//
// Se hace sobre el TEXTO, sin rehacer el XML, para que el dibujo llegue tal
// cual lo escribió su autor -comentarios, espacios, el orden de los atributos-
// salvo lo quitado: se busca cada `id="PIEZA@valor"`, se retrocede hasta el `<`
// de su elemento y se avanza hasta su cierre, contando los anidados del mismo
// nombre. Un SVG que no es XML bien formado se manda como venía.
// =============================================================================
#ifndef STM32_COMMON_SVG_VARIANTES_H
#define STM32_COMMON_SVG_VARIANTES_H

#include <map>
#include <string>

namespace stm32 {

namespace detalle_svg {

// Desde `p` (justo después del nombre de un elemento), el final de su
// etiqueta de apertura: la posición del '>', respetando las comillas.
inline size_t fin_etiqueta(const std::string& s, size_t p) {
    char q = 0;
    for (; p < s.size(); ++p) {
        const char c = s[p];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '>') return p;
    }
    return std::string::npos;
}

// ¿Empieza en `p` la etiqueta `<nombre` o `</nombre`, y no `<nombrelargo`?
inline bool es_etiqueta(const std::string& s, size_t p, const std::string& nombre) {
    if (s.compare(p, nombre.size(), nombre) != 0) return false;
    const size_t f = p + nombre.size();
    return f < s.size() && (s[f] == ' ' || s[f] == '>' || s[f] == '/' || s[f] == '\t' ||
                            s[f] == '\n' || s[f] == '\r');
}

// El final -una posición más allá del último carácter- del elemento que empieza
// en `ini` (su '<'), o npos si no se encuentra.
inline size_t fin_elemento(const std::string& s, size_t ini) {
    size_t p = ini + 1;
    while (p < s.size() && s[p] != ' ' && s[p] != '>' && s[p] != '/' && s[p] != '\t' &&
           s[p] != '\n' && s[p] != '\r')
        ++p;
    const std::string nombre = s.substr(ini + 1, p - ini - 1);
    size_t g = fin_etiqueta(s, p);
    if (g == std::string::npos) return g;
    if (s[g - 1] == '/') return g + 1;                     // <x .../>
    int prof = 1;
    p = g + 1;
    while (p < s.size()) {
        const size_t lt = s.find('<', p);
        if (lt == std::string::npos) return lt;
        if (s.compare(lt, 4, "<!--") == 0) {              // un comentario
            const size_t f = s.find("-->", lt + 4);
            if (f == std::string::npos) return f;
            p = f + 3;
            continue;
        }
        if (s.compare(lt, 9, "<![CDATA[") == 0) {
            const size_t f = s.find("]]>", lt + 9);
            if (f == std::string::npos) return f;
            p = f + 3;
            continue;
        }
        const bool cierre = lt + 1 < s.size() && s[lt + 1] == '/';
        const size_t pn = lt + (cierre ? 2 : 1);
        const size_t fe = fin_etiqueta(s, pn);
        if (fe == std::string::npos) return fe;
        if (es_etiqueta(s, pn, nombre)) {
            if (cierre) {
                if (--prof == 0) return fe + 1;
            } else if (s[fe - 1] != '/') {
                ++prof;
            }
        }
        p = fe + 1;
    }
    return std::string::npos;
}

} // namespace detalle_svg

// Devuelve el dibujo sin las variantes que no tocan. `variante_de` va del id de
// la pieza EN SU PLACA (`JP1`) a su variante (`5V-VCC`). Un `PIEZA@valor` de
// una pieza que no está en el mapa se queda: no es de nadie que se sepa.
// `quitadas`, si se pide, cuenta los elementos quitados.
inline std::string quita_variantes(const std::string& svg,
                                   const std::map<std::string, std::string>& variante_de,
                                   unsigned* quitadas = nullptr) {
    using namespace detalle_svg;
    if (quitadas) *quitadas = 0;
    if (variante_de.empty()) return svg;
    std::string s = svg;
    size_t p = 0;
    for (;;) {
        const size_t a = s.find("id=", p);
        if (a == std::string::npos) break;
        p = a + 3;
        // Un atributo `id` entero: precedido de un blanco, y entre comillas
        if (a == 0 || !(s[a - 1] == ' ' || s[a - 1] == '\t' || s[a - 1] == '\n' ||
                        s[a - 1] == '\r'))
            continue;
        if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) continue;
        const size_t f = s.find(s[p], p + 1);
        if (f == std::string::npos) break;
        const std::string id = s.substr(p + 1, f - p - 1);
        const size_t arroba = id.find('@');
        if (arroba == std::string::npos) continue;
        const auto it = variante_de.find(id.substr(0, arroba));
        if (it == variante_de.end() || id.substr(arroba + 1) == it->second) continue;
        const size_t lt = s.rfind('<', a);
        if (lt == std::string::npos) continue;
        const size_t fin = fin_elemento(s, lt);
        if (fin == std::string::npos) return svg;          // roto: tal cual
        // Y la sangría y el salto de línea que lo acompañan, si los tiene
        size_t ini = lt;
        while (ini > 0 && (s[ini - 1] == ' ' || s[ini - 1] == '\t')) --ini;
        size_t fin2 = fin;
        if (ini > 0 && s[ini - 1] == '\n' && fin2 < s.size() && s[fin2] == '\n') ++fin2;
        else ini = lt;
        s.erase(ini, fin2 - ini);
        if (quitadas) ++*quitadas;
        p = ini;
    }
    return s;
}

// Pone el texto de los rótulos: `texto_de` va del id del elemento
// (`VCP#destino`) a lo que tiene que decir. Solo en un elemento sin otros
// dentro -un <text> con su texto-; uno con hijos se deja como está.
inline std::string pon_rotulos(const std::string& svg,
                               const std::map<std::string, std::string>& texto_de) {
    using namespace detalle_svg;
    std::string s = svg;
    for (const auto& kv : texto_de) {
        for (const char q : {'"', '\''}) {
            const std::string aguja = std::string("id=") + q + kv.first + q;
            const size_t a = s.find(aguja);
            if (a == std::string::npos) continue;
            const size_t lt = s.rfind('<', a);
            if (lt == std::string::npos) break;
            size_t p = lt + 1;
            while (p < s.size() && s[p] != ' ' && s[p] != '>' && s[p] != '/' &&
                   s[p] != '\t' && s[p] != '\n' && s[p] != '\r')
                ++p;
            const size_t g = fin_etiqueta(s, p);
            if (g == std::string::npos || s[g - 1] == '/') break;
            const size_t fin = fin_elemento(s, lt);
            if (fin == std::string::npos) break;
            const size_t cierre = s.rfind("</", fin);
            if (cierre == std::string::npos || cierre <= g) break;
            if (s.find('<', g + 1) != cierre) break;            // tiene hijos
            std::string t;
            for (char c : kv.second) {
                if (c == '&') t += "&amp;";
                else if (c == '<') t += "&lt;";
                else if (c == '>') t += "&gt;";
                else t += c;
            }
            s.replace(g + 1, cierre - g - 1, t);
            break;
        }
    }
    return s;
}

} // namespace stm32
#endif // STM32_COMMON_SVG_VARIANTES_H
