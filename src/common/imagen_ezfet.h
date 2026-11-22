// =============================================================================
// imagen_ezfet.h — Las imágenes de firmware de la eZ-FET (M11b de
// doc/analisis_msp430.md): se leen de la copia de la MSP Debug Stack del
// usuario (`--ezfet-imagenes DIR`), no se versionan.
//
// Cada imagen es una cabecera de C generada por srecord (Bios/include/
// eZ_Fet*.h): un `const uint16_t X[] = {...}` con todas las palabras,
// `X_address[]` con la dirección de cada sección y `X_length_of_sections[]`
// con su longitud EN PALABRAS. Es lo mismo que lee `Record` en la DLL
// (DLL430_v3/src/TI/DLL430/Record.cpp) para comprobar las versiones.
//
// Las cinco de una eZ-FET (núcleo, HAL, HIL, canal de comunicación y la capa
// del DC-DC) se cargan juntas en la Flash del F5528: no se pisan.
// =============================================================================
#ifndef STM32_COMMON_IMAGEN_EZFET_H
#define STM32_COMMON_IMAGEN_EZFET_H

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "imagen_fw.h"

namespace stm32 {
namespace ezfet {

// Los números de un `const uint{16,32}_t NOMBRE[] = { ... };`
inline bool lee_tabla(const std::string& txt, const std::string& nombre, std::vector<uint32_t>& v) {
    const std::string clave = " " + nombre + "[]";
    const size_t p = txt.find(clave);
    if (p == std::string::npos) return false;
    const size_t a = txt.find('{', p), b = txt.find('}', p);
    if (a == std::string::npos || b == std::string::npos || b < a) return false;
    v.clear();
    const char* s = txt.c_str() + a + 1;
    const char* fin = txt.c_str() + b;
    while (s < fin) {
        if (std::isxdigit(static_cast<unsigned char>(*s))) {
            char* q = nullptr;
            v.push_back(uint32_t(std::strtoul(s, &q, 0)));
            s = q;
        } else {
            ++s;
        }
    }
    return true;
}

// Una imagen (p. ej. "eZ_FetCore") en trozos de bytes. Devuelve "" o el error
inline std::string lee_imagen(const std::string& ruta, const std::string& nombre, std::vector<TrozoFw>& t) {
    std::ifstream f(ruta);
    if (!f) return ruta + ": no se puede abrir";
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string txt = ss.str();
    std::vector<uint32_t> pal, dir, lon;
    if (!lee_tabla(txt, nombre + "Image", pal) || !lee_tabla(txt, nombre + "Image_address", dir) ||
        !lee_tabla(txt, nombre + "Image_length_of_sections", lon) || dir.size() != lon.size())
        return ruta + ": no tiene la forma de srecord (" + nombre + "Image, _address, _length_of_sections)";
    size_t k = 0;
    for (size_t i = 0; i < dir.size(); ++i) {
        if (k + lon[i] > pal.size()) return ruta + ": las secciones piden más palabras de las que hay";
        TrozoFw x;
        x.dir = dir[i];
        for (uint32_t j = 0; j < lon[i]; ++j, ++k) {
            x.datos.push_back(uint8_t(pal[k]));
            x.datos.push_back(uint8_t(pal[k] >> 8));
        }
        t.push_back(std::move(x));
    }
    return std::string();
}

// Las cinco de la eZ-FET, desde DIR, DIR/include o DIR/Bios/include
inline const char* const IMAGENES[5] = {"eZ_FetCore", "eZ_FetHal", "eZ_FetHil", "eZ_FetComChannel", "eZ_FetDcdc"};

inline std::string busca(const std::string& dir) {
    for (const char* sub : {"", "/include", "/Bios/include"}) {
        const std::string d = dir + sub;
        if (std::ifstream(d + "/eZ_FetCore.h")) return d;
    }
    return std::string();
}

inline std::string lee_todas(const std::string& dir, std::vector<TrozoFw>& t) {
    const std::string d = busca(dir);
    if (d.empty())
        return dir + ": no está eZ_FetCore.h (ni en include/ ni en Bios/include/): "
                     "--ezfet-imagenes pide la carpeta de la MSP Debug Stack";
    for (const char* n : IMAGENES) {
        const std::string e = lee_imagen(d + "/" + n + ".h", n, t);
        if (!e.empty()) return e;
    }
    return std::string();
}

} // namespace ezfet
} // namespace stm32
#endif
