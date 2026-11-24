// =============================================================================
// exporta_piezas.h — Las fichas de la factoría, como DATO: `--exporta-piezas`
//
// Fase B1 de `doc/analisis_build.md`. `mcu-sim-build` necesita saber de cada
// tipo de pieza lo mismo que `sim --help TIPO` cuenta a una persona: qué
// terminales tiene, cuáles hacen falta, qué atributos admite y con qué
// omisión. Esa ficha ya existe y es la ÚNICA fuente (part_help.h): viaja con
// el registro de la pieza. Aquí no se escribe otra; se VUELCA esa, una pieza
// por fichero:
//
//   mcu-sim --exporta-piezas biblioteca/piezas     ->  Led.pieza.xml, ...
//
// LO ÚNICO QUE NO ES COPIAR: LOS TERMINALES. La ficha los nombra para una
// persona, y de sus doscientas filas unas cuarenta agrupan varios en una:
//
//   anodo o catodo          dos terminales, cualquiera de los dos
//   in1, in2, in3           una lista
//   in1 .. in4, d0 ... d15  un rango, con dos o tres puntos
//   d0 ... dN, l0, l1, ...  un rango ABIERTO: cuántos, lo dice la placa
//   1..N, 1..N o su nombre  los pines de un conector, por número (o nombre)
//
// `analiza_terminales` entiende esas formas y solo esas, y una fila que no
// entiende es un ERROR, no una fila que se salta: la suite (T133) recorre
// todas las de todas las piezas. Así una ficha nueva no puede escribirse de
// otra manera sin que se note, y la ficha sigue siendo la fuente única, sin
// un segundo campo «para máquinas» que pueda discrepar del texto. Es la
// opción que el análisis propone en su §4.2.
//
// EL FORMATO DEL FICHERO (`version="1"`):
//
//   <pieza tipo="Led" version="1">
//     <resumen>...</resumen>
//     <terminales>
//       <grupo fila="anodo o catodo" cuando="uno de los dos, o los dos"
//              alternativos="si">
//         <terminal nombre="anodo"/>
//         <terminal nombre="catodo"/>
//         <texto>...</texto>
//       </grupo>
//       <grupo fila="d0 ... dN" cuando="al menos uno">
//         <serie prefijo="d" desde="0" abierta="si"/>
//         <texto>...</texto>
//       </grupo>
//     </terminales>
//     <atributo nombre="vf" omision="2.0">...</atributo>
//     <atributo nombre="puente" obligatorio="si">...</atributo>
//     <referencia nombre="hilo">...</referencia>
//     <nota>...</nota>
//     <ejemplo>...</ejemplo>
//   </pieza>
//
// `alternativos="si"` es una fila con «o»: los terminales son maneras de
// llamar a la patilla, y la ficha (`cuando`) dice si valen a la vez. Una
// pieza sin patillas (`Postura`) lleva `<sin_terminales>` con su porqué
// en lugar de `<terminales>`. Una `<serie>` con `prefijo=""` son los pines
// de un conector por su número, y `nombres="si"` dice que además se pueden
// llamar por su nombre. Lo que no hay es el `cpp()` de la ficha: es la API
// de C++ de la pieza, y a un editor de placas no le sirve.
//
// El volcado es DETERMINISTA —las piezas por orden alfabético, los
// atributos en el orden de la ficha, sin fechas ni versión del programa—
// para que lo versionado en `src/biblioteca/piezas/` solo cambie cuando
// cambia una ficha, y `make biblioteca-comprueba` lo compare byte a byte.
// =============================================================================
#ifndef STM32_PARTS_EXPORTA_PIEZAS_H
#define STM32_PARTS_EXPORTA_PIEZAS_H

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "part_factory.h"
#include "part_help.h"
#include "xml_min.h"

namespace stm32 {

// Un rango de terminales: `prefijo` + número, de `desde` a `hasta`. Abierta,
// `hasta` no se sabe por la ficha: lo dice la placa (cuántos hilos de datos
// lleva un bus, cuántos pines un conector).
struct SerieTerminales {
    std::string prefijo;
    long        desde   = 0;
    long        hasta   = -1;
    bool        abierta = false;
};

// Lo que dice una fila `.pin(...)` de una ficha, ya entendido.
struct TerminalesDeFila {
    std::vector<std::string>     nombres;   // los que se nombran uno a uno
    std::vector<SerieTerminales> series;    // y los rangos
    bool        tambien_por_nombre = false; // «1..N o su nombre»
    bool        alternativos = false;       // «anodo o catodo»: el uno o el otro
    std::string error;                      // vacío si se entiende

    bool vale() const { return error.empty(); }
    // Cuántos terminales nombra, si se sabe; -1 si alguna serie es abierta
    long cuantos() const {
        long n = long(nombres.size());
        for (const SerieTerminales& s : series) {
            if (s.abierta) return -1;
            n += s.hasta - s.desde + 1;
        }
        return n;
    }
};

namespace detalle_exporta {

inline bool es_nombre(const std::string& t) {
    if (t.empty()) return false;
    for (char c : t)
        if (!(std::islower(static_cast<unsigned char>(c)) ||
              std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}

// `d15` -> ("d", 15). false si no acaba en cifras. Con `abierto`, también
// acepta `dN` y `N`: el final de un rango que no se sabe.
inline bool parte(const std::string& t, std::string& prefijo, long& n, bool& abierto) {
    abierto = false;
    if (!t.empty() && t.back() == 'N') {
        prefijo = t.substr(0, t.size() - 1);
        abierto = true;
        return prefijo.empty() || es_nombre(prefijo);
    }
    size_t i = t.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(t[i - 1]))) --i;
    if (i == t.size()) return false;                 // no acaba en cifra
    if (t.size() - i > 6) return false;
    prefijo = t.substr(0, i);
    if (!prefijo.empty() && !es_nombre(prefijo)) return false;
    n = std::stol(t.substr(i));
    return true;
}

} // namespace detalle_exporta

// Entiende el nombre de una fila de terminales de una ficha (`.pin(NOMBRE,
// ...)`). Las formas, arriba; cualquier otra cosa es un error que dice por qué.
inline TerminalesDeFila analiza_terminales(const std::string& fila) {
    using namespace detalle_exporta;
    TerminalesDeFila r;
    std::string f = fila;

    // «... o su nombre»: los de un conector, que se llaman también por nombre
    const std::string su_nombre = " o su nombre";
    if (f.size() > su_nombre.size() &&
        f.compare(f.size() - su_nombre.size(), su_nombre.size(), su_nombre) == 0) {
        r.tambien_por_nombre = true;
        f.erase(f.size() - su_nombre.size());
    }

    // Las piezas: ` o ` y las comas separan; `..` y `...` son el rango, con
    // espacios alrededor o sin ellos (`1..N`, `in1 .. in4`). Se trocea en
    // nombres y marcas de rango, en orden.
    std::vector<std::string> trozos;   // "…" es la marca de rango
    std::string cur;
    auto suelta = [&] { if (!cur.empty()) { trozos.push_back(cur); cur.clear(); } };
    for (size_t i = 0; i < f.size();) {
        const char c = f[i];
        if (c == '.') {
            size_t j = i;
            while (j < f.size() && f[j] == '.') ++j;
            if (j - i < 2 || j - i > 3) {
                r.error = "un punto suelto: los rangos van con .. o ...";
                return r;
            }
            suelta();
            trozos.push_back("…");
            i = j;
        } else if (c == ',' || c == ' ') {
            suelta();
            if (c == ',') trozos.push_back(",");
            ++i;
        } else {
            cur.push_back(c);
            ++i;
        }
    }
    suelta();
    // ` o ` es una coma más: dos nombres alternativos son dos terminales
    for (std::string& t : trozos) if (t == "o") { t = ","; r.alternativos = true; }

    // Ahora: nombre (, nombre)* con, en cualquier sitio, `a … b` (rango) o un
    // `…` final detrás de una lista (`l0, l1, ...`: rango abierto)
    std::vector<std::string> items;    // sin las comas
    for (size_t k = 0; k < trozos.size(); ++k) {
        if (trozos[k] == ",") {
            if (k == 0 || k + 1 == trozos.size() || trozos[k + 1] == ",") {
                r.error = "una coma sin nombre a un lado";
                return r;
            }
            continue;
        }
        items.push_back(trozos[k]);
    }
    if (items.empty()) {
        r.error = "la fila no nombra ningun terminal";
        return r;
    }

    for (size_t k = 0; k < items.size(); ++k) {
        const std::string& t = items[k];
        if (t == "…") {
            // `l0, l1, ...`: rango abierto que sigue a los dos anteriores
            if (k + 1 == items.size()) {
                if (r.nombres.size() < 2) {
                    r.error = "una lista que acaba en ... necesita dos nombres delante";
                    return r;
                }
                std::string p0, p1; long n0 = 0, n1 = 0; bool a0, a1;
                const std::string& u0 = r.nombres[r.nombres.size() - 2];
                const std::string& u1 = r.nombres.back();
                if (!parte(u0, p0, n0, a0) || !parte(u1, p1, n1, a1) || a0 || a1 ||
                    p0 != p1 || n1 != n0 + 1) {
                    r.error = "una lista que acaba en ... tiene que llevar delante "
                              "dos nombres seguidos, como l0, l1";
                    return r;
                }
                r.nombres.resize(r.nombres.size() - 2);
                SerieTerminales s;
                s.prefijo = p0; s.desde = n0; s.abierta = true;
                r.series.push_back(s);
                continue;
            }
            r.error = "un rango sin principio: falta el nombre antes de ..";
            return r;
        }
        // ¿Es el principio de un rango `a … b`?
        if (k + 2 < items.size() && items[k + 1] == "…" && items[k + 2] != "…") {
            std::string pa, pb; long na = 0, nb = 0; bool aa, ab;
            if (!parte(t, pa, na, aa) || aa) {
                r.error = "el rango empieza por `" + t + "`, que no acaba en numero";
                return r;
            }
            if (!parte(items[k + 2], pb, nb, ab)) {
                r.error = "el rango acaba en `" + items[k + 2] +
                          "`, que no es un numero ni N";
                return r;
            }
            if (pa != pb) {
                r.error = "los dos extremos del rango no tienen el mismo nombre: `" +
                          t + "` y `" + items[k + 2] + "`";
                return r;
            }
            if (!ab && nb < na) {
                r.error = "el rango va hacia atras";
                return r;
            }
            SerieTerminales s;
            s.prefijo = pa; s.desde = na;
            if (ab) s.abierta = true; else s.hasta = nb;
            r.series.push_back(s);
            k += 2;
            continue;
        }
        if (!es_nombre(t)) {
            r.error = "`" + t + "` no es un nombre de terminal: minusculas, cifras y _";
            return r;
        }
        r.nombres.push_back(t);
    }
    if (r.tambien_por_nombre &&
        (r.series.size() != 1 || !r.series[0].prefijo.empty() || !r.nombres.empty())) {
        r.error = "`o su nombre` solo va detras de un rango por numero, como 1..N";
        return r;
    }
    // Un mismo terminal dos veces es una errata
    for (size_t a = 0; a < r.nombres.size(); ++a)
        for (size_t b = a + 1; b < r.nombres.size(); ++b)
            if (r.nombres[a] == r.nombres[b]) {
                r.error = "`" + r.nombres[a] + "` sale dos veces";
                return r;
            }
    return r;
}

// La ficha de un tipo, en XML. Vacío si la ficha tiene una fila de terminales
// que no se entiende, y `error` dice cuál.
inline std::string pieza_xml(const std::string& tipo, const Ayuda& a, std::string* error = nullptr) {
    const auto esc = [](const std::string& s) { return xml_escapa(s); };
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    // Sin dos guiones seguidos: dentro de un comentario XML no se pueden
    // escribir, y `--help` los lleva
    x += "<!-- La ficha de " + tipo + ", la misma que imprime la ayuda de mcu-sim.\n"
         "     La genera mcu-sim con su opcion exporta-piezas: no se edita a mano,\n"
         "     se regenera con `make biblioteca` (doc/analisis_build.md, B1). -->\n";
    x += "<pieza tipo=\"" + esc(tipo) + "\" version=\"1\">\n";
    x += "  <resumen>" + esc(a.resumen()) + "</resumen>\n";
    if (!a.pines().empty()) {
        x += "  <terminales>\n";
        for (const Ayuda::Fila& f : a.pines()) {
            const TerminalesDeFila t = analiza_terminales(f.nombre);
            if (!t.vale()) {
                if (error) *error = tipo + ": la fila de terminales `" + f.nombre + "`: " + t.error;
                return std::string();
            }
            x += "    <grupo fila=\"" + esc(f.nombre) + "\" cuando=\"" + esc(f.apostilla) + "\"";
            if (t.alternativos) x += " alternativos=\"si\"";
            x += ">\n";
            for (const std::string& n : t.nombres)
                x += "      <terminal nombre=\"" + esc(n) + "\"/>\n";
            for (const SerieTerminales& s : t.series) {
                x += "      <serie prefijo=\"" + esc(s.prefijo) + "\" desde=\"" +
                     std::to_string(s.desde) + "\"";
                if (s.abierta) x += " abierta=\"si\"";
                else           x += " hasta=\"" + std::to_string(s.hasta) + "\"";
                if (t.tambien_por_nombre) x += " nombres=\"si\"";
                x += "/>\n";
            }
            x += "      <texto>" + esc(f.texto) + "</texto>\n";
            x += "    </grupo>\n";
        }
        x += "  </terminales>\n";
    } else {
        x += "  <sin_terminales>" + esc(a.sin_terminales()) + "</sin_terminales>\n";
    }
    for (const Ayuda::Fila& f : a.atributos()) {
        x += "  <atributo nombre=\"" + esc(f.nombre) + "\"";
        if (f.apostilla == "(obligatorio)") x += " obligatorio=\"si\"";
        else                                x += " omision=\"" + esc(f.apostilla) + "\"";
        x += ">" + esc(f.texto) + "</atributo>\n";
    }
    for (const Ayuda::Fila& f : a.refs())
        x += "  <referencia nombre=\"" + esc(f.nombre) + "\">" + esc(f.texto) + "</referencia>\n";
    for (const std::string& n : a.notas())
        x += "  <nota>" + esc(n) + "</nota>\n";
    if (!a.ejemplo().empty())
        x += "  <ejemplo>" + esc(a.ejemplo()) + "</ejemplo>\n";
    x += "</pieza>\n";
    return x;
}

// Escribe un `<Tipo>.pieza.xml` por cada tipo de la factoría en `dir`, que
// tiene que existir. Devuelve el error, o vacío; en `n`, cuántos escribió.
// Se para en el primero que falle: una biblioteca a medias no sirve.
inline std::string exporta_piezas(const std::string& dir, unsigned* n = nullptr) {
    unsigned escritos = 0;
    for (const std::string& tipo : Fabrica::tipos()) {
        const Ayuda* a = Fabrica::ayuda(tipo);
        if (!a) return tipo + ": registrado sin ficha";
        std::string err;
        const std::string x = pieza_xml(tipo, *a, &err);
        if (x.empty()) return err;
        const std::string ruta = dir + "/" + tipo + ".pieza.xml";
        std::FILE* f = std::fopen(ruta.c_str(), "wb");
        if (!f) return "no se puede escribir " + ruta;
        const bool ok = std::fwrite(x.data(), 1, x.size(), f) == x.size();
        if (std::fclose(f) != 0 || !ok) return "no se pudo escribir entero " + ruta;
        ++escritos;
    }
    if (n) *n = escritos;
    return std::string();
}

} // namespace stm32
#endif // STM32_PARTS_EXPORTA_PIEZAS_H
