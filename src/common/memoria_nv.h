// =============================================================================
// memoria_nv.h — LA MEMORIA NO VOLÁTIL, de una simulación a la siguiente
//
// Lo que un chip o una pieza guarda sin alimentación —la Flash de un MCU, la
// EEPROM del MSP432E4, una 24Cxx por I2C, una tarjeta SD, una NAND— se perdía
// al acabar la simulación: la siguiente empezaba otra vez de cero. Con el
// atributo `contenido="fichero.hex"` de su `<mcu>` o de su `<componente>` (o
// `--contenido ID=FICHERO`), la memoria SE CARGA de ese fichero al arrancar y
// SE GUARDA en él al acabar, en Intel HEX, que es lo que leen y escriben las
// herramientas de los fabricantes. Si el fichero aún no existe, la memoria
// arranca como siempre y el fichero se crea al acabar. Sin `contenido`, nada
// cambia.
//
// Una memoria dice DÓNDE están sus bytes en el fichero con sus REGIONES: las
// de un MCU, en sus direcciones del mapa de memoria (la Flash de un STM32 en
// 0x0800_0000, la de un MSP432 en 0x0); la EEPROM del E4, que no está en el
// mapa —se lee por registros—, en la ventana de esos registros, 0x400A_F000; y
// la de una pieza, desde 0.
//
// LA REFERENCIA de una región es lo que vale si nadie la ha tocado: ninguna
// (se guarda entera: las piezas, que arrancan con un patrón de prueba), un
// RELLENO (la Flash de un MCU, que arranca borrada a 0xFF) o unos BYTES (los
// de fábrica de los bytes de opción, el BSL que puso `bsl=`...). Al guardar,
// lo que es igual a la referencia no se escribe: un MB de Flash con un
// programa de 20 KB son 20 KB de fichero, y unos bytes de opción de fábrica no
// salen. Lo que el fichero no dice se queda como estaba al construir la
// memoria -que es la referencia-, así que la ida y vuelta es exacta. Una región
// TODO O NADA -un BSL- no se guarda a trozos: si algo difiere, va entera.
//
// Y dos momentos: ANTES DE CARGAR, el chip fija las referencias que dependen
// de cómo se ha montado (el BSL y el TLV con `bsl=` y `tlv=` ya aplicados);
// DESPUÉS, hace con lo cargado lo que haría el chip -recargar sus bytes de
// opción, que el reset de alimentación ya leyó-, y dice lo que haya que decir.
// =============================================================================
#ifndef STM32_COMMON_MEMORIA_NV_H
#define STM32_COMMON_MEMORIA_NV_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "imagen_fw.h"

namespace stm32 {

struct RegionNv {
    enum class Ref : uint8_t { Ninguna, Relleno, Bytes };

    std::string nombre;          // "Flash", "EEPROM", "Bytes de opcion"
    uint32_t    base = 0;        // donde va en el fichero
    uint32_t    tam  = 0;        // bytes
    Ref         ref = Ref::Ninguna;
    uint8_t     relleno = 0xFF;            // con Ref::Relleno
    std::vector<uint8_t> referencia;       // con Ref::Bytes: `tam` bytes
    bool        todo_o_nada = false;       // si algo difiere, se guarda entera

    // Se guarda entera
    static RegionNv entera(std::string n, uint32_t b, uint32_t t) {
        RegionNv r; r.nombre = std::move(n); r.base = b; r.tam = t; return r;
    }
    // Sin lo que este a `rel` (la Flash, a 0xFF)
    static RegionNv borrada(std::string n, uint32_t b, uint32_t t, uint8_t rel = 0xFF) {
        RegionNv r = entera(std::move(n), b, t); r.ref = Ref::Relleno; r.relleno = rel; return r;
    }
    // Sin lo que sea igual a `ref`, byte a byte
    static RegionNv con_referencia(std::string n, uint32_t b, std::vector<uint8_t> ref,
                                   bool todo = false) {
        RegionNv r = entera(std::move(n), b, uint32_t(ref.size()));
        r.ref = Ref::Bytes; r.referencia = std::move(ref); r.todo_o_nada = todo; return r;
    }
    // ¿Es `v` lo que vale el byte `off` sin tocar? Sin referencia, nunca
    bool es_referencia(uint32_t off, uint8_t v) const {
        switch (ref) {
            case Ref::Relleno: return v == relleno;
            case Ref::Bytes:   return off < referencia.size() && v == referencia[off];
            default:           return false;
        }
    }
};

class memoria_nv_if {
public:
    virtual ~memoria_nv_if() = default;
    virtual std::vector<RegionNv> regiones_nv() const = 0;
    // Un byte, por su dirección EN EL FICHERO. false: no es de ninguna región
    virtual bool lee_nv(uint32_t a, uint8_t& v) = 0;
    virtual bool escribe_nv(uint32_t a, uint8_t v) = 0;

    // Antes de cargar: fijar las referencias que dependen del montaje
    virtual void antes_de_cargar_nv() {}
    // Después de cargar: lo que el chip hace con lo cargado. Devuelve los
    // avisos para la consola; un error va en `error`, y la simulación no sigue
    virtual std::vector<std::string> tras_cargar_nv(std::string& error) {
        (void)error;
        return {};
    }

    // `--restore-rdp`: la protección de lectura de un STM32 a la de fábrica,
    // aunque esté en el nivel 2. Devuelve lo que ha hecho, o "" si ya estaba
    virtual bool tiene_rdp() const { return false; }
    virtual std::string restablece_rdp() { return std::string(); }
    // `--restore-otp`: la OTP de un STM32F4 -datos y bytes de bloqueo- a la de
    // fábrica, todo a 0xFF. Devuelve lo que ha hecho, o "" si ya estaba
    virtual bool tiene_otp() const { return false; }
    virtual std::string restablece_otp() { return std::string(); }

    // Lo que es, para los mensajes: "Flash", "Flash y EEPROM", "EEPROM de 64 bytes"
    std::string describe_nv() const {
        const std::vector<RegionNv> r = regiones_nv();
        std::string s;
        for (size_t i = 0; i < r.size(); ++i)
            s += (i == 0 ? "" : i + 1 == r.size() ? " y " : ", ") + r[i].nombre;
        return s;
    }
};

// Intel HEX, de los trozos que se den: registros de 16 bytes, con su registro
// 04 (dirección lineal extendida) cada vez que cambian los 16 bits de arriba.
// Se escribe en un temporal y se renombra: una simulación que muere a medias
// no deja un fichero cortado. Devuelve el error, o "".
inline std::string escribe_ihex(const std::string& ruta, const std::vector<TrozoFw>& t) {
    std::string s;
    auto registro = [&](uint8_t tipo, uint16_t dir, const uint8_t* d, size_t n) {
        char b[16];
        uint8_t suma = uint8_t(n + (dir >> 8) + (dir & 0xFF) + tipo);
        std::snprintf(b, sizeof b, ":%02X%04X%02X", unsigned(n), unsigned(dir), unsigned(tipo));
        s += b;
        for (size_t i = 0; i < n; ++i) {
            std::snprintf(b, sizeof b, "%02X", unsigned(d[i]));
            s += b;
            suma = uint8_t(suma + d[i]);
        }
        std::snprintf(b, sizeof b, "%02X\n", unsigned(uint8_t(0x100 - suma)));
        s += b;
    };
    uint32_t alta = 0xFFFFFFFFu;
    for (const TrozoFw& tr : t) {
        size_t i = 0;
        while (i < tr.datos.size()) {
            const uint32_t a = tr.dir + uint32_t(i);
            if ((a >> 16) != alta) {
                alta = a >> 16;
                const uint8_t h[2] = {uint8_t(alta >> 8), uint8_t(alta)};
                registro(0x04, 0, h, 2);
            }
            // Hasta 16 bytes, sin cruzar una frontera de 64 KB
            size_t n = std::min<size_t>(16, tr.datos.size() - i);
            n = std::min<size_t>(n, 0x10000u - (a & 0xFFFFu));
            registro(0x00, uint16_t(a & 0xFFFFu), tr.datos.data() + i, n);
            i += n;
        }
    }
    registro(0x01, 0, nullptr, 0);
    const std::string tmp = ruta + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return "no se puede escribir " + tmp;
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    if (std::fclose(f) != 0 || !ok) { std::remove(tmp.c_str()); return "no se puede escribir " + tmp; }
    std::remove(ruta.c_str());            // en Windows, rename no pisa
    if (std::rename(tmp.c_str(), ruta.c_str()) != 0) return "no se puede renombrar " + tmp + " a " + ruta;
    return std::string();
}

// Las PIEZAS con memoria no volátil, por su tipo: lo que admite `contenido`
// sin construirla -el netlist lo valida y `--memorias` las lista-. Cada una
// devuelve su `memoria_nv_if` (parts/ext_parts.h); si alguna no lo hiciera,
// `sim` lo diría al montarla con su contenido
inline bool es_memoria_nv(const std::string& tipo) {
    return tipo == "I2cEeprom" || tipo == "SdCard" || tipo == "ExtNand" || tipo == "SpiFlash";
}
inline std::string tipos_memoria_nv() { return "I2cEeprom, SdCard, ExtNand y SpiFlash"; }

inline bool existe_fichero(const std::string& ruta) {
    std::FILE* f = std::fopen(ruta.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// CARGAR: el fichero en la memoria. Un fichero que no existe no es un error
// -la primera vez no lo hay-: `cargados` se queda en 0 y `existia` en false.
// Un byte que no cae en ninguna región sí lo es: el fichero es de otra cosa.
inline std::string carga_contenido(memoria_nv_if& m, const std::string& ruta,
                                   size_t& cargados, bool& existia) {
    cargados = 0;
    existia = existe_fichero(ruta);
    if (!existia) return std::string();
    std::vector<TrozoFw> t;
    const std::string e = lee_ihex(ruta, t, true);
    if (!e.empty()) return e;
    for (const TrozoFw& tr : t)
        for (size_t i = 0; i < tr.datos.size(); ++i) {
            const uint32_t a = tr.dir + uint32_t(i);
            if (!m.escribe_nv(a, tr.datos[i])) {
                char b[16];
                std::snprintf(b, sizeof b, "0x%08X", unsigned(a));
                return ruta + ": " + b + " no es de la memoria (" + m.describe_nv() + ")";
            }
            ++cargados;
        }
    return std::string();
}

// GUARDAR: la memoria en el fichero, región a región, sin lo que es igual a
// su referencia.
inline std::string guarda_contenido(memoria_nv_if& m, const std::string& ruta, size_t& guardados) {
    std::vector<TrozoFw> t;
    guardados = 0;
    for (const RegionNv& r : m.regiones_nv()) {
        // Todo o nada: si nada difiere, nada; si algo, entera
        bool entera = r.ref == RegionNv::Ref::Ninguna;
        if (r.todo_o_nada) {
            for (uint32_t off = 0; off < r.tam && !entera; ++off) {
                uint8_t v = 0xFF;
                m.lee_nv(r.base + off, v);
                entera = !r.es_referencia(off, v);
            }
            if (!entera) continue;
        }
        for (uint32_t off = 0; off < r.tam; off += 16) {
            const uint32_t n = std::min<uint32_t>(16, r.tam - off);
            uint8_t b[16];
            bool de_referencia = true;
            for (uint32_t k = 0; k < n; ++k) {
                b[k] = 0xFF;
                m.lee_nv(r.base + off + k, b[k]);
                de_referencia = de_referencia && r.es_referencia(off + k, b[k]);
            }
            if (!entera && de_referencia) continue;
            const uint32_t a = r.base + off;
            if (!t.empty() && t.back().dir + t.back().datos.size() == a)
                t.back().datos.insert(t.back().datos.end(), b, b + n);
            else {
                TrozoFw tr;
                tr.dir = a;
                tr.datos.assign(b, b + n);
                t.push_back(std::move(tr));
            }
            guardados += n;
        }
    }
    return escribe_ihex(ruta, t);
}

} // namespace stm32
#endif // STM32_COMMON_MEMORIA_NV_H
