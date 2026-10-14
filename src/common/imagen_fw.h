// =============================================================================
// imagen_fw.h — Leer una imagen de firmware como lista de trozos (dirección, bytes)
//
// Hasta T0 el cargador sabía de dos formatos: el binario crudo (`.bin`), que no
// dice dónde va y se pone en la base de la Flash, e Intel HEX (`.hex`), que la
// Flash del F407 sabe leer ella sola. Faltaban dos que pide cualquier chip:
//
//   * ELF (`.elf`, `.axf` y el `.out` de Code Composer Studio). Es lo que sale
//     del enlazador y lo que GDB lee: con él no hay que convertir nada antes de
//     simular. Se cargan los segmentos PT_LOAD por su dirección FÍSICA (LMA),
//     no la virtual: los datos inicializados viajan en Flash y los copia el
//     arranque a la SRAM, y cargarlos directamente en la SRAM sería hacer en
//     el simulador algo que en la placa no pasa.
//   * TI-TXT (`.txt`): `@dirección` seguido de bytes en hexadecimal y una `q`
//     al final. Es lo que sacan `hex470`/`tiobj2bin`, lo que escribe UniFlash y
//     lo que entiende el BSL de los MSP430 y MSP432. [doc/analisis_msp432.md,
//     §4.2]
//
// Este fichero solo LEE: devuelve los trozos y su dirección, y quien carga
// decide dónde va cada uno —la Flash, la SRAM o un error si no es de ninguna,
// que es lo que hace un programador de verdad—. No depende de SystemC.
// =============================================================================
#ifndef STM32_COMMON_IMAGEN_FW_H
#define STM32_COMMON_IMAGEN_FW_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace stm32 {

struct TrozoFw {
    uint32_t             dir;
    std::vector<uint8_t> datos;
};

enum class FormatoFw { Bin, IntelHex, Elf, TiTxt };

namespace detalle_fw {
inline bool termina_en(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != suf[i]) return false;
    }
    return true;
}
inline bool lee_todo(const std::string& ruta, std::vector<uint8_t>& v) {
    std::FILE* f = std::fopen(ruta.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    v.resize(size_t(n));
    const size_t rd = n ? std::fread(v.data(), 1, size_t(n), f) : 0;
    std::fclose(f);
    return rd == size_t(n);
}
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace detalle_fw

// Por la extensión y, para ELF, también por el número mágico: un `.out` de CCS
// y un ELF renombrado se reconocen igual.
inline FormatoFw formato_fw(const std::string& ruta) {
    using namespace detalle_fw;
    if (termina_en(ruta, ".hex") || termina_en(ruta, ".ihex")) return FormatoFw::IntelHex;
    if (termina_en(ruta, ".txt")) return FormatoFw::TiTxt;
    if (termina_en(ruta, ".elf") || termina_en(ruta, ".out") || termina_en(ruta, ".axf"))
        return FormatoFw::Elf;
    std::FILE* f = std::fopen(ruta.c_str(), "rb");
    if (f) {
        unsigned char m[4] = {0, 0, 0, 0};
        const size_t n = std::fread(m, 1, 4, f);
        std::fclose(f);
        if (n == 4 && m[0] == 0x7F && m[1] == 'E' && m[2] == 'L' && m[3] == 'F')
            return FormatoFw::Elf;
    }
    return FormatoFw::Bin;
}

// ELF32, little endian, ARM. "" si todo bien; si no, el motivo.
inline std::string lee_elf(const std::string& ruta, std::vector<TrozoFw>& t) {
    using namespace detalle_fw;
    t.clear();
    std::vector<uint8_t> f;
    if (!lee_todo(ruta, f)) return "no se puede leer " + ruta;
    if (f.size() < 52 || f[0] != 0x7F || f[1] != 'E' || f[2] != 'L' || f[3] != 'F')
        return ruta + " no es un ELF";
    if (f[4] != 1) return ruta + ": es un ELF de 64 bits, y un Cortex-M es de 32";
    if (f[5] != 1) return ruta + ": es un ELF big endian";
    const uint16_t maquina = le16(&f[18]);
    if (maquina != 40)       // EM_ARM
        return ruta + ": es un ELF para otra arquitectura (e_machine = " +
               std::to_string(maquina) + "), no para ARM";
    const uint32_t phoff = le32(&f[28]);
    const uint16_t phentsize = le16(&f[42]), phnum = le16(&f[44]);
    if (phnum == 0) return ruta + ": el ELF no tiene segmentos que cargar";
    if (phentsize < 32 || uint64_t(phoff) + uint64_t(phnum) * phentsize > f.size())
        return ruta + ": la tabla de segmentos del ELF esta truncada";
    for (unsigned k = 0; k < phnum; ++k) {
        const uint8_t* ph = &f[phoff + size_t(k) * phentsize];
        const uint32_t tipo = le32(ph + 0), off = le32(ph + 4), paddr = le32(ph + 12),
                       filesz = le32(ph + 16);
        if (tipo != 1 || filesz == 0) continue;          // PT_LOAD con contenido
        if (uint64_t(off) + filesz > f.size())
            return ruta + ": un segmento del ELF se sale del fichero";
        TrozoFw tr;
        tr.dir = paddr;
        tr.datos.assign(f.begin() + off, f.begin() + off + filesz);
        t.push_back(std::move(tr));
    }
    if (t.empty()) return ruta + ": el ELF no tiene ningun segmento PT_LOAD con datos";
    return std::string();
}

// TI-TXT. "" si todo bien.
inline std::string lee_titxt(const std::string& ruta, std::vector<TrozoFw>& t) {
    using namespace detalle_fw;
    t.clear();
    std::vector<uint8_t> f;
    if (!lee_todo(ruta, f)) return "no se puede leer " + ruta;
    const std::string s(f.begin(), f.end());
    bool hay_dir = false, fin = false;
    unsigned linea = 1;
    size_t i = 0;
    auto error = [&](const std::string& m) {
        return ruta + ":" + std::to_string(linea) + ": " + m;
    };
    while (i < s.size() && !fin) {
        const char c = s[i];
        if (c == '\n') { ++linea; ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
        if (c == 'q' || c == 'Q') { fin = true; break; }
        if (c == '@') {
            ++i;
            uint32_t a = 0;
            unsigned n = 0;
            while (i < s.size() && hexval(s[i]) >= 0) { a = (a << 4) | uint32_t(hexval(s[i])); ++i; ++n; }
            if (n == 0 || n > 8) return error("direccion '@' mal escrita");
            TrozoFw tr;
            tr.dir = a;
            t.push_back(tr);
            hay_dir = true;
            continue;
        }
        if (hexval(c) >= 0) {
            if (!hay_dir) return error("datos antes de la primera '@direccion'");
            if (i + 1 >= s.size() || hexval(s[i + 1]) < 0)
                return error("un byte se escribe con dos cifras hexadecimales");
            t.back().datos.push_back(uint8_t((hexval(c) << 4) | hexval(s[i + 1])));
            i += 2;
            continue;
        }
        return error(std::string("caracter inesperado '") + c + "'");
    }
    if (!fin) return ruta + ": falta la 'q' que cierra un fichero TI-TXT";
    std::vector<TrozoFw> lleno;
    for (TrozoFw& x : t) if (!x.datos.empty()) lleno.push_back(std::move(x));
    t.swap(lleno);
    if (t.empty()) return ruta + ": el fichero TI-TXT no tiene datos";
    return std::string();
}

// Intel HEX, para los chips que no son un STM32 (el STM32 lo lee su propia
// Flash, `FlashIf::load_ihex_file`, y así sigue). Registros 00 (datos), 01
// (fin), 02 (segmento), 04 (dirección alta); 03 y 05 se aceptan y se ignoran.
inline std::string lee_ihex(const std::string& ruta, std::vector<TrozoFw>& t) {
    using namespace detalle_fw;
    t.clear();
    std::vector<uint8_t> f;
    if (!lee_todo(ruta, f)) return "no se puede leer " + ruta;
    const std::string s(f.begin(), f.end());
    uint32_t alta = 0;
    unsigned linea = 0;
    size_t i = 0;
    bool fin = false;
    while (i < s.size() && !fin) {
        size_t e = s.find('\n', i);
        if (e == std::string::npos) e = s.size();
        std::string l = s.substr(i, e - i);
        i = e + 1;
        ++linea;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.empty()) continue;
        const std::string donde = ruta + ":" + std::to_string(linea) + ": ";
        if (l[0] != ':' || l.size() < 11 || (l.size() % 2) == 0)
            return donde + "no es un registro Intel HEX";
        std::vector<uint8_t> b;
        for (size_t k = 1; k + 1 < l.size(); k += 2) {
            const int h = hexval(l[k]), lo = hexval(l[k + 1]);
            if (h < 0 || lo < 0) return donde + "cifra hexadecimal mala";
            b.push_back(uint8_t(h * 16 + lo));
        }
        uint8_t suma = 0;
        for (uint8_t x : b) suma = uint8_t(suma + x);
        if (suma != 0) return donde + "suma de comprobacion mala";
        const unsigned n = b[0];
        if (b.size() != n + 5u) return donde + "longitud del registro mala";
        const uint32_t dir = (uint32_t(b[1]) << 8) | b[2];
        switch (b[3]) {
            case 0x00: {
                const uint32_t a = alta + dir;
                if (!t.empty() && t.back().dir + t.back().datos.size() == a)
                    t.back().datos.insert(t.back().datos.end(), b.begin() + 4, b.begin() + 4 + n);
                else {
                    TrozoFw tr;
                    tr.dir = a;
                    tr.datos.assign(b.begin() + 4, b.begin() + 4 + n);
                    t.push_back(std::move(tr));
                }
                break;
            }
            case 0x01: fin = true; break;
            case 0x02: alta = ((uint32_t(b[4]) << 8) | b[5]) << 4;  break;
            case 0x04: alta = ((uint32_t(b[4]) << 8) | b[5]) << 16; break;
            case 0x03: case 0x05: break;
            default: return donde + "tipo de registro desconocido";
        }
    }
    if (t.empty()) return ruta + ": el fichero Intel HEX no tiene datos";
    return std::string();
}

} // namespace stm32
#endif // STM32_COMMON_IMAGEN_FW_H
