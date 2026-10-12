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
// Y EL GIRO: el mismo dibujo, girado un cuarto, media o tres cuartos de
// vuelta en el sentido de las agujas del reloj, como se haya montado la placa
// -`giro="90"` en ella o en su <placa id> de un sistema-. `gira_svg` cambia
// el viewBox y el tamaño de la raíz y mete todo lo de dentro en un <g> con la
// transformación. Los ids no cambian, y la ventana encuentra cada pieza
// donde ha quedado: para ella es un SVG con un grupo más.
//
// Se hace sobre el TEXTO, sin rehacer el XML, para que el dibujo llegue tal
// cual lo escribió su autor -comentarios, espacios, el orden de los atributos-
// salvo lo quitado: se busca cada `id="PIEZA@valor"`, se retrocede hasta el `<`
// de su elemento y se avanza hasta su cierre, contando los anidados del mismo
// nombre. Un SVG que no es XML bien formado se manda como venía.
// =============================================================================
#ifndef STM32_COMMON_SVG_VARIANTES_H
#define STM32_COMMON_SVG_VARIANTES_H

#include <cstdio>
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

// El dibujo girado `grados` (90, 180 o 270) en el sentido de las agujas del
// reloj. Hace falta que la raíz <svg> tenga viewBox: es lo que se gira. Con
// 0 grados, el mismo; si no se puede, el mismo y en `error` por qué.
inline std::string gira_svg(const std::string& svg, int grados, std::string* error = nullptr) {
    using namespace detalle_svg;
    if (error) error->clear();
    if (grados % 360 == 0) return svg;
    auto falla = [&](const char* por) {
        if (error) *error = por;
        return svg;
    };
    if (grados != 90 && grados != 180 && grados != 270)
        return falla("el giro es de 90, 180 o 270 grados");
    // La raíz: el primer <svg fuera de los comentarios
    size_t p = 0, ini = std::string::npos;
    while (p < svg.size()) {
        const size_t lt = svg.find('<', p);
        if (lt == std::string::npos) break;
        if (svg.compare(lt, 4, "<!--") == 0) {
            const size_t f = svg.find("-->", lt + 4);
            if (f == std::string::npos) break;
            p = f + 3;
            continue;
        }
        if (es_etiqueta(svg, lt + 1, "svg")) { ini = lt; break; }
        p = lt + 1;
    }
    if (ini == std::string::npos) return falla("no tiene raiz <svg>");
    const size_t g = fin_etiqueta(svg, ini + 4);
    const size_t cierre = svg.rfind("</svg>");
    if (g == std::string::npos || cierre == std::string::npos || cierre < g)
        return falla("la raiz <svg> no se cierra");
    std::string tag = svg.substr(ini, g - ini);      // sin el '>'
    // El valor de un atributo de la etiqueta, y dónde está
    auto atributo = [&](const char* n, size_t& a, size_t& b) {
        const std::string k = std::string(" ") + n + "=";
        size_t i = tag.find(k);
        if (i == std::string::npos) {
            const std::string k2 = std::string("\n") + n + "=";
            i = tag.find(k2);
        }
        if (i == std::string::npos) return false;
        a = i + k.size();
        if (a >= tag.size() || (tag[a] != '"' && tag[a] != '\'')) return false;
        b = tag.find(tag[a], a + 1);
        if (b == std::string::npos) return false;
        ++a;
        return true;
    };
    size_t va, vb;
    if (!atributo("viewBox", va, vb)) return falla("la raiz <svg> no tiene viewBox");
    double x0 = 0, y0 = 0, w = 0, h = 0;
    {
        std::string v = tag.substr(va, vb - va);
        for (char& c : v) if (c == ',') c = ' ';
        if (std::sscanf(v.c_str(), "%lf %lf %lf %lf", &x0, &y0, &w, &h) != 4 || w <= 0 || h <= 0)
            return falla("el viewBox no son cuatro numeros");
    }
    auto num = [](double d) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", d);
        return std::string(b);
    };
    const bool cuarto = grados != 180;
    const double nw = cuarto ? h : w, nh = cuarto ? w : h;
    tag.replace(va, vb - va, "0 0 " + num(nw) + " " + num(nh));
    // El tamaño: ancho y alto se cambian el uno por el otro
    if (cuarto) {
        size_t wa, wb, ha, hb;
        const bool hay_w = atributo("width", wa, wb), hay_h = atributo("height", ha, hb);
        if (hay_w && hay_h) {
            const std::string sw = tag.substr(wa, wb - wa), sh = tag.substr(ha, hb - ha);
            // El que va detrás primero, para no mover al otro
            if (wa > ha) { tag.replace(wa, wb - wa, sh); tag.replace(ha, hb - ha, sw); }
            else         { tag.replace(ha, hb - ha, sw); tag.replace(wa, wb - wa, sh); }
        }
    }
    // Lo de dentro: (x, y) del dibujo -> girado, en el nuevo viewBox
    std::string tr;
    if (grados == 90)       tr = "translate(" + num(h) + " 0) rotate(90)";
    else if (grados == 180) tr = "translate(" + num(w) + " " + num(h) + ") rotate(180)";
    else                    tr = "translate(0 " + num(w) + ") rotate(270)";
    if (x0 != 0 || y0 != 0) tr += " translate(" + num(-x0) + " " + num(-y0) + ")";
    return svg.substr(0, ini) + tag + ">\n<g transform=\"" + tr + "\">" +
           svg.substr(g + 1, cierre - g - 1) + "</g>\n" + svg.substr(cierre);
}

} // namespace stm32
#endif // STM32_COMMON_SVG_VARIANTES_H
