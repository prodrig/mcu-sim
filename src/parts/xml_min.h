// =============================================================================
// xml_min.h — Lector de XML mínimo y ESTRICTO
//
// (Paso 3 de la ruta de adopción del esquema XML+SVG de QtSysC; véase
//  doc/stm32f407vg_parts_paso3.md.)
//
// POR QUÉ NO TINYXML2 NI EXPAT. Este proyecto tiene UNA dependencia —SystemC— y
// se compila con un Makefile de veinte líneas en cualquier máquina con g++. Un
// analizador de XML de propósito general trae consigo XML entero: espacios de
// nombres, DTD, entidades definidas por el usuario, CDATA, instrucciones de
// proceso. De todo eso, el formato del netlist no usa nada.
//
// Escribir un analizador de XML a mano es normalmente un error: el que lo hace
// acaba con algo permisivo que acepta ficheros mal formados y hace lo que le
// parece. Aquí se evita ese error por la vía de ser ESTRICTO: cualquier cosa
// que este lector no entienda es un error con línea y columna, nunca una
// suposición. El subconjunto aceptado es exactamente:
//
//   * declaración <?xml ...?> opcional, y comentarios <!-- ... -->
//   * elementos con atributos entrecomillados (comillas simples o dobles)
//   * elementos vacíos <e/> y elementos con hijos <e>...</e>
//   * las cinco entidades de la norma: &lt; &gt; &amp; &quot; &apos;
//   * texto entre elementos, que se ignora si es solo espacios y es un ERROR
//     si no lo es (el formato del netlist no tiene contenido textual, así que
//     texto suelto significa que alguien se ha equivocado)
//
// Y no acepta: espacios de nombres, DTD, CDATA, instrucciones de proceso,
// atributos sin comillas, atributos repetidos ni etiquetas descasadas. En todos
// esos casos dice qué esperaba y dónde.
// =============================================================================
#ifndef STM32_PARTS_XML_MIN_H
#define STM32_PARTS_XML_MIN_H

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace stm32 {

struct XmlNodo {
    std::string                                      nombre;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<XmlNodo>                             hijos;
    unsigned                                         linea = 0;

    const std::string* attr(const std::string& k) const {
        for (const auto& a : attrs) if (a.first == k) return &a.second;
        return nullptr;
    }
    std::string attr_o(const std::string& k, const char* omision = "") const {
        const std::string* v = attr(k);
        return v ? *v : std::string(omision);
    }
    bool tiene(const std::string& k) const { return attr(k) != nullptr; }
};

class XmlLector {
public:
    // Devuelve true si ha podido leer. Si no, `error()` dice qué pasó y dónde.
    bool parse(const std::string& texto) {
        s_ = texto; i_ = 0; linea_ = 1; col_ = 1; err_.clear();
        raiz_ = XmlNodo{};
        salta_prologo();
        if (err_.empty() && !lee_elemento(raiz_)) fija_error_si_vacio("se esperaba un elemento");
        if (err_.empty()) {
            salta_ignorable();
            if (i_ < s_.size()) error("sobra texto despues del elemento raiz");
        }
        return err_.empty();
    }
    bool parse_fichero(const std::string& ruta) {
        std::ifstream f(ruta.c_str(), std::ios::binary);
        if (!f) { err_ = "no se puede abrir el fichero: " + ruta; return false; }
        std::ostringstream os; os << f.rdbuf();
        if (!parse(os.str())) { err_ = ruta + ": " + err_; return false; }
        return true;
    }
    const XmlNodo&     raiz()  const { return raiz_; }
    const std::string& error() const { return err_; }

private:
    // --- Recorrido ----------------------------------------------------------
    char  ch()  const { return i_ < s_.size() ? s_[i_] : '\0'; }
    char  ch(size_t k) const { return i_ + k < s_.size() ? s_[i_ + k] : '\0'; }
    bool  fin() const { return i_ >= s_.size(); }
    void  avanza() {
        if (fin()) return;
        if (s_[i_] == '\n') { ++linea_; col_ = 1; } else ++col_;
        ++i_;
    }
    bool mira(const char* t) const {
        return s_.compare(i_, std::strlen(t), t) == 0;
    }
    void come(size_t n) { for (size_t k = 0; k < n; ++k) avanza(); }

    void error(const std::string& q) {
        if (!err_.empty()) return;                 // se conserva el primero
        char buf[64];
        std::snprintf(buf, sizeof buf, "linea %u, columna %u: ", linea_, col_);
        err_ = std::string(buf) + q;
    }
    void fija_error_si_vacio(const std::string& q) { error(q); }

    void salta_espacios() {
        while (!fin() && std::isspace(static_cast<unsigned char>(ch()))) avanza();
    }
    // Espacios y comentarios: lo que puede aparecer entre cualquier par de cosas.
    void salta_ignorable() {
        for (;;) {
            salta_espacios();
            if (mira("<!--")) {
                come(4);
                while (!fin() && !mira("-->")) avanza();
                if (fin()) { error("comentario sin cerrar"); return; }
                come(3);
                continue;
            }
            return;
        }
    }
    void salta_prologo() {
        salta_ignorable();
        if (mira("<?")) {
            come(2);
            while (!fin() && !mira("?>")) avanza();
            if (fin()) { error("declaracion <?xml sin cerrar"); return; }
            come(2);
            salta_ignorable();
        }
        // Lo que el formato NO admite, dicho por su nombre en vez de por un
        // fallo raro doscientas lineas mas abajo.
        if (mira("<!DOCTYPE")) error("no se admiten DTD");
    }

    bool es_nombre_ini(char c) const {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    bool es_nombre(char c) const {
        return std::isalnum(static_cast<unsigned char>(c)) ||
               c == '_' || c == '-' || c == '.' || c == '+';
    }
    bool lee_nombre(std::string& out) {
        if (!es_nombre_ini(ch())) return false;
        out.clear();
        while (!fin() && es_nombre(ch())) { out.push_back(ch()); avanza(); }
        if (ch() == ':') { error("no se admiten espacios de nombres"); return false; }
        return true;
    }

    // Las cinco entidades de la norma, y ninguna mas.
    bool decodifica(std::string& out) {
        come(1);                                   // el '&'
        std::string e;
        while (!fin() && ch() != ';' && e.size() < 8) { e.push_back(ch()); avanza(); }
        if (ch() != ';') { error("entidad sin ';'"); return false; }
        come(1);
        if      (e == "lt")   out.push_back('<');
        else if (e == "gt")   out.push_back('>');
        else if (e == "amp")  out.push_back('&');
        else if (e == "quot") out.push_back('"');
        else if (e == "apos") out.push_back('\'');
        else { error("entidad desconocida: &" + e + ";"); return false; }
        return true;
    }

    bool lee_valor(std::string& out) {
        const char q = ch();
        if (q != '"' && q != '\'') { error("el valor de un atributo va entre comillas"); return false; }
        come(1);
        out.clear();
        while (!fin() && ch() != q) {
            if (ch() == '<') { error("'<' dentro del valor de un atributo"); return false; }
            if (ch() == '&') { if (!decodifica(out)) return false; continue; }
            out.push_back(ch()); avanza();
        }
        if (fin()) { error("valor de atributo sin cerrar"); return false; }
        come(1);
        return true;
    }

    bool lee_elemento(XmlNodo& n) {
        salta_ignorable();
        if (!err_.empty()) return false;
        if (ch() != '<') return false;
        if (ch(1) == '/') return false;            // es un cierre, no un elemento
        n.linea = linea_;
        come(1);
        if (!lee_nombre(n.nombre)) { error("nombre de elemento no valido"); return false; }

        // Atributos
        for (;;) {
            salta_espacios();
            if (ch() == '/' || ch() == '>') break;
            std::string k;
            if (!lee_nombre(k)) { error("se esperaba un atributo o el fin de la etiqueta"); return false; }
            if (n.tiene(k)) { error("atributo repetido: " + k); return false; }
            salta_espacios();
            if (ch() != '=') { error("se esperaba '=' tras el atributo " + k); return false; }
            come(1);
            salta_espacios();
            std::string v;
            if (!lee_valor(v)) return false;
            n.attrs.emplace_back(k, v);
        }

        if (ch() == '/') {                          // elemento vacio <e/>
            come(1);
            if (ch() != '>') { error("se esperaba '>' tras '/'"); return false; }
            come(1);
            return true;
        }
        come(1);                                    // el '>'

        // Contenido: hijos, y texto que solo puede ser espacios
        for (;;) {
            const size_t antes = i_;
            salta_ignorable();
            if (!err_.empty()) return false;
            if (fin()) { error("falta </" + n.nombre + ">"); return false; }
            if (ch() == '<' && ch(1) == '/') break;
            if (ch() == '<') {
                XmlNodo h;
                if (!lee_elemento(h)) { fija_error_si_vacio("elemento mal formado"); return false; }
                n.hijos.push_back(std::move(h));
                continue;
            }
            // Cualquier otra cosa es texto suelto, y en este formato eso es un
            // error: no hay ningun elemento con contenido textual.
            error("texto suelto dentro de <" + n.nombre + ">");
            (void)antes;
            return false;
        }
        come(2);                                    // "</"
        std::string cierre;
        if (!lee_nombre(cierre)) { error("cierre mal formado"); return false; }
        if (cierre != n.nombre) {
            error("cierra </" + cierre + "> lo que abrio <" + n.nombre + ">");
            return false;
        }
        salta_espacios();
        if (ch() != '>') { error("se esperaba '>' en el cierre"); return false; }
        come(1);
        return true;
    }

    std::string s_, err_;
    size_t      i_ = 0;
    unsigned    linea_ = 1, col_ = 1;
    XmlNodo     raiz_;
};

// Escapado para el volcado. Solo lo que hace falta en un valor de atributo.
inline std::string xml_escapa(const std::string& v) {
    std::string o;
    for (char c : v) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default:   o.push_back(c);
        }
    }
    return o;
}

} // namespace stm32
#endif // STM32_PARTS_XML_MIN_H
