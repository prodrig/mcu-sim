// =============================================================================
// lee_vec.h — lee un fichero de vectores de verif/vectores (*.vec)
//
// El formato es el de `comprueba_vectores.py`: bloques `[caso]` con líneas
// `campo = valor`; `#` empieza un comentario. Cada caso es un mapa de campo a
// valor, sin interpretar: cada banco coge los campos que entiende. Lo usa
// `teste4` para pasar todos los vectores por el CCM del MSP432E4 (T7).
// =============================================================================
#ifndef STM32_VERIF_LEE_VEC_H
#define STM32_VERIF_LEE_VEC_H

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace vec {

using Caso = std::map<std::string, std::string>;

inline std::vector<Caso> lee(const std::string& ruta) {
    std::vector<Caso> v;
    std::ifstream f(ruta);
    if (!f) return v;
    auto recorta = [](std::string s) {
        const size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        const size_t b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    std::string l;
    while (std::getline(f, l)) {
        const std::string t = recorta(l);
        if (t.empty() || t[0] == '#') continue;
        if (t == "[caso]") { v.emplace_back(); continue; }
        const size_t eq = t.find('=');
        if (eq == std::string::npos || v.empty()) continue;
        v.back()[recorta(t.substr(0, eq))] = recorta(t.substr(eq + 1));
    }
    return v;
}

inline std::string campo(const Caso& c, const char* k) {
    const auto i = c.find(k);
    return i == c.end() ? std::string() : i->second;
}

inline std::vector<uint8_t> hex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        v.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
    return v;
}

inline std::string a_hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

} // namespace vec
#endif // STM32_VERIF_LEE_VEC_H
