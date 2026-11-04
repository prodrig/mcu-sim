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
// Lo que el fichero no dice se queda como estaba al construir la memoria. Por
// eso al guardar, una región `omite_borrado` —la Flash de un MCU, que arranca
// borrada— solo escribe los registros que no son todo 0xFF: un MB de Flash con
// un programa de 20 KB son 20 KB de fichero. Las de las piezas, que arrancan
// con un patrón de prueba, se guardan enteras.
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
    std::string nombre;          // "Flash", "EEPROM", "Flash de informacion"
    uint32_t    base = 0;        // donde va en el fichero
    uint32_t    tam  = 0;        // bytes
    bool        omite_borrado = false;   // al guardar, sin los registros todo 0xFF
};

class memoria_nv_if {
public:
    virtual ~memoria_nv_if() = default;
    virtual std::vector<RegionNv> regiones_nv() const = 0;
    // Un byte, por su dirección EN EL FICHERO. false: no es de ninguna región
    virtual bool lee_nv(uint32_t a, uint8_t& v) = 0;
    virtual bool escribe_nv(uint32_t a, uint8_t v) = 0;

    // Lo que es, para los mensajes: "Flash", "Flash y EEPROM", "EEPROM de 64 bytes"
    std::string describe_nv() const {
        std::string s;
        for (const RegionNv& r : regiones_nv())
            s += (s.empty() ? "" : " y ") + r.nombre;
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
    return tipo == "I2cEeprom" || tipo == "SdCard" || tipo == "ExtNand";
}
inline std::string tipos_memoria_nv() { return "I2cEeprom, SdCard y ExtNand"; }

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

// GUARDAR: la memoria entera en el fichero, región a región.
inline std::string guarda_contenido(memoria_nv_if& m, const std::string& ruta, size_t& guardados) {
    std::vector<TrozoFw> t;
    guardados = 0;
    for (const RegionNv& r : m.regiones_nv()) {
        for (uint32_t off = 0; off < r.tam; off += 16) {
            const uint32_t n = std::min<uint32_t>(16, r.tam - off);
            uint8_t b[16];
            bool borrado = true;
            for (uint32_t k = 0; k < n; ++k) {
                b[k] = 0xFF;
                m.lee_nv(r.base + off + k, b[k]);
                borrado = borrado && b[k] == 0xFF;
            }
            if (r.omite_borrado && borrado) continue;
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
