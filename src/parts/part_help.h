// =============================================================================
// part_help.h — La AYUDA de una pieza: qué es y qué atributos admite
//
// `sim --help Led` tiene que contar lo que hace un LED y qué se le puede
// escribir en el XML. La pregunta no es cómo imprimir eso —eso es fácil— sino
// DÓNDE VIVE el texto, y hay dos respuestas con consecuencias muy distintas:
//
//   * una tabla aparte, en el fichero que imprime la ayuda. Es lo cómodo de
//     escribir y lo que se queda obsoleto: el día que alguien añade un
//     parámetro al creador, la tabla no se entera, y una ayuda que miente es
//     peor que no tenerla, porque el que la lee ya no vuelve al código;
//
//   * el texto VIAJANDO CON EL REGISTRO, en la misma llamada que da de alta la
//     pieza en la factoría. Entonces no hay dos sitios que puedan discrepar:
//     hay uno.
//
// Esto es lo segundo. `REGISTRA_PARTE` pasa a tener tres argumentos —tipo,
// ayuda y creador— y el de en medio no es opcional, de modo que **no se puede
// registrar una pieza sin explicarla**: no es una convención que haya que
// recordar, es lo que exige la macro. Y como el compilador no puede juzgar si
// un texto dice algo, `Fabrica::sin_documentar()` enumera las que se han
// registrado con una ayuda vacía o de adorno, y la suite lo comprueba (T126).
//
// La ayuda se escribe encadenando, y no es un capricho de estilo: un
// constructor con seis listas de inicialización dentro de una macro es un
// campo de minas de comas, mientras que `.pin(...).atr(...)` las lleva todas
// dentro de paréntesis, donde el preprocesador no las toca.
//
//   Ayuda("Diodo con su resistencia en serie...")
//     .pin("anodo", "uno de los dos", "La patilla que va al pin...")
//     .atr("vf", "2.0", "Tension directa del diodo, en voltios...")
//     .nota("El caso del LED azul: con vf=3,0 sobre 3,3 V...")
//     .cpp("on(), current()")
//     .ejemplo("<componente tipo=\"Led\" id=\"LD4\" ...>")
//
// El formateo es deliberadamente pobre —ochenta columnas, sin color y sin
// tabuladores— porque esto sale por la salida estándar de una consola que
// puede ser la de Windows. Por la misma razón el texto va SIN ACENTOS, como
// todos los mensajes del programa: el catálogo con tipografía de verdad es
// `doc/parts.md`, y la ayuda termina remitiendo a él.
// =============================================================================
#ifndef STM32_PARTS_PART_HELP_H
#define STM32_PARTS_PART_HELP_H

#include <string>
#include <vector>

namespace stm32 {

class Ayuda {
public:
    struct Fila {
        std::string nombre;    // `anodo`, `vf`, `hilo`
        std::string apostilla; // "obligatorio", "opcional", "2.0", "0x50"...
        std::string texto;     // qué es y qué cambia
    };

    Ayuda() = default;
    explicit Ayuda(const char* resumen) : resumen_(resumen ? resumen : "") {}

    // Un terminal. `cuando` dice si hace falta: "obligatorio", "opcional",
    // "al menos uno", "uno de los dos"... Es texto libre a propósito, porque
    // "obligatorio/opcional" no describe bien ni al LED ni a un bus indexado.
    Ayuda& pin(const char* n, const char* cuando, const char* q) {
        pines_.push_back({n, cuando, q});
        return *this;
    }
    // Un atributo del XML. `omision` es el valor que toma si no se escribe;
    // la cadena vacía significa que no hay omisión y es obligatorio.
    Ayuda& atr(const char* n, const char* omision, const char* q) {
        attrs_.push_back({n, omision && *omision ? omision : "(obligatorio)", q});
        return *this;
    }
    // Una referencia a otra instancia: `<ref nombre="hilo" componente="bus1"/>`.
    Ayuda& ref(const char* n, const char* q) {
        refs_.push_back({n, "(obligatorio)", q});
        return *this;
    }
    Ayuda& nota(const char* t)    { notas_.push_back(t); return *this; }
    Ayuda& cpp(const char* t)     { cpp_ = t;      return *this; }
    Ayuda& ejemplo(const char* t) { ejemplo_ = t;  return *this; }

    // Lo que la suite exige, y el mínimo honesto: que haya un resumen que
    // diga algo -no "Un Led."- y que los terminales estén enumerados. Los
    // atributos no se exigen porque hay piezas que no tienen ninguno, y las
    // notas menos todavía; los terminales sí, porque una pieza sin terminales
    // no se puede conectar a nada.
    bool completa() const {
        return resumen_.size() >= 40 && !pines_.empty();
    }

    const std::string& resumen() const { return resumen_; }
    const std::vector<Fila>& pines() const { return pines_; }
    const std::vector<Fila>& atributos() const { return attrs_; }
    const std::vector<Fila>& refs() const { return refs_; }

    // La página entera, ya formateada, lista para escupir por stdout.
    std::string texto(const std::string& tipo) const;

private:
    std::string resumen_, cpp_, ejemplo_;
    std::vector<Fila> pines_, attrs_, refs_;
    std::vector<std::string> notas_;
};

// ---------------------------------------------------------------------------
// El formateo
// ---------------------------------------------------------------------------

namespace detalle_ayuda {

inline const unsigned ANCHO = 78;

// Parte un párrafo en líneas de `ancho` columnas con sangría `sangria`,
// cortando por espacios. Dos cosas que respeta, y las dos porque el texto lo
// escribe una persona en un literal de C++:
//
//   * LOS SALTOS DE LÍNEA QUE YA TRAE, que es como se escribe una nota de
//     varios párrafos;
//   * LAS LÍNEAS QUE EMPIEZAN POR UN ESPACIO, que se copian tal cual. Es la
//     escapatoria para meter una tablita alineada dentro de una nota —la de
//     «pulsado XOR normalmente cerrado» del Button— sin que el envoltorio la
//     convierta en un párrafo y le destroce las columnas.
inline std::string envuelve(const std::string& t, unsigned sangria,
                            unsigned ancho = ANCHO) {
    if (t.empty()) return std::string();
    const std::string pad(sangria, ' ');
    std::string salida;
    size_t i = 0;
    while (i <= t.size()) {
        size_t j = t.find('\n', i);
        if (j == std::string::npos) j = t.size();
        const std::string ln = t.substr(i, j - i);
        i = j + 1;
        if (ln.empty())   { salida += "\n";           continue; }
        if (ln[0] == ' ') { salida += pad + ln + "\n"; continue; }
        std::string linea;
        size_t k = 0;
        while (k < ln.size()) {
            while (k < ln.size() && ln[k] == ' ') ++k;
            size_t m = k;
            while (m < ln.size() && ln[m] != ' ') ++m;
            const std::string palabra = ln.substr(k, m - k);
            if (palabra.empty()) break;
            if (linea.empty()) linea = pad + palabra;
            else if (linea.size() + 1 + palabra.size() <= ancho)
                linea += " " + palabra;
            else { salida += linea + "\n"; linea = pad + palabra; }
            k = m;
        }
        if (!linea.empty()) salida += linea + "\n";
    }
    return salida;
}

// Un trozo de texto que se copia TAL CUAL, solo sangrado: los ejemplos de
// XML. Envolverlos sería destruirlos —la sangría de un `<pin>` dentro de su
// `<componente>` es la mitad de lo que el ejemplo enseña—, y es la razón de que
// esto no pase por `envuelve`.
inline std::string literal(const std::string& t, unsigned sangria) {
    const std::string pad(sangria, ' ');
    std::string s;
    size_t i = 0;
    while (i <= t.size()) {
        size_t j = t.find('\n', i);
        if (j == std::string::npos) j = t.size();
        s += pad + t.substr(i, j - i) + "\n";
        i = j + 1;
    }
    return s;
}

// Una entrada de tabla: el nombre y su apostilla en una línea, y debajo el
// texto sangrado. Dos líneas por entrada en vez de columnas alineadas porque
// las descripciones de aquí son párrafos, no celdas, y una columna de 60
// caracteres no da para ellas.
inline std::string fila(const Ayuda::Fila& f, const char* etiqueta_omision) {
    std::string s = "  " + f.nombre;
    if (!f.apostilla.empty()) {
        s += std::string(f.nombre.size() < 16 ? 16 - f.nombre.size() : 2, ' ');
        if (f.apostilla[0] == '(') s += f.apostilla;
        else s += std::string(etiqueta_omision) + f.apostilla;
    }
    s += "\n";
    return s + envuelve(f.texto, 6);
}

} // namespace detalle_ayuda

inline std::string Ayuda::texto(const std::string& tipo) const {
    using namespace detalle_ayuda;
    std::string s = tipo + "\n" + std::string(tipo.size(), '=') + "\n\n";
    s += envuelve(resumen_, 2);

    if (!ejemplo_.empty()) s += "\n" + literal(ejemplo_, 2);

    if (!pines_.empty()) {
        s += "\nTERMINALES  (<pin nombre=\"...\" nodo=\"...\"/>)\n";
        for (const Fila& f : pines_) s += fila(f, "");
    }
    if (!attrs_.empty()) {
        s += "\nATRIBUTOS DEL XML  (<componente ... nombre=\"valor\">)\n";
        for (const Fila& f : attrs_) s += fila(f, "por omision ");
    }
    if (!refs_.empty()) {
        s += "\nREFERENCIAS  (<ref nombre=\"...\" componente=\"...\"/>)\n";
        for (const Fila& f : refs_) s += fila(f, "");
    }
    for (const std::string& n : notas_) s += "\n" + envuelve(n, 2);
    if (!cpp_.empty()) {
        s += "\nDESDE C++\n";
        s += envuelve(cpp_, 2);
    }

    // El pie es igual para todas, y dice lo que de otro modo habria que
    // repetir veintidos veces: los tres atributos comunes y la trampa de los
    // numeros, que es el fallo silencioso mas caro de este formato.
    s += "\nTODO COMPONENTE ADMITE ADEMAS\n";
    s += envuelve("tipo e id son obligatorios; conectada=\"no\" lo construye "
                  "DESOLDADO -se monta, pero con todos sus drivers en alta "
                  "impedancia-, que es como se describe un montaje que hoy no "
                  "esta puesto sin borrarlo del fichero.", 2);
    s += "\n" + envuelve("Los numeros NO admiten sufijos de unidad: 8e6, no 8M -que "
                  "se lee 8-. Los booleanos se escriben si o no. Un atributo "
                  "mal escrito no da error: se guarda, no lo lee nadie y la "
                  "pieza usa su valor por omision.", 2);
    s += "\n" + envuelve("El catalogo completo, con tablas y ejemplos, esta en "
                         "doc/parts.md. `sim --help` sin mas lista todos los "
                         "tipos.", 2);
    return s;
}

} // namespace stm32
#endif // STM32_PARTS_PART_HELP_H
