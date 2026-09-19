// =============================================================================
// prueba_hash.cpp — los vectores oficiales contra el núcleo de MD5 y SHA-1
//
// NO necesita SystemC: comprueba `periph/hash_algo.h`, que es aritmética pura.
// Lee `verif/vectores/hash.vec` —los 23 casos que trajo la fase 0, con su
// procedencia— y los pasa por el núcleo del modelo.
//
// Es la mitad de la verificación de la fase 2. La otra mitad, la del protocolo
// de registros -`DATATYPE`, `NBLW`, `DCAL`, las cuatro fases del HMAC-, está en
// el banco `top/sc_main_f417.cpp`, que sí levanta una simulación. Se separan
// porque son dos preguntas distintas: aquí, «¿sale el resumen correcto?»; allí,
// «¿sale por donde el firmware lo pide?».
//
//   make hash                # los casos rápidos
//   make hash V=--lentos     # también el del millón de letras
// =============================================================================
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "../periph/hash_algo.h"

using namespace stm32;

namespace {

struct Caso {
    std::string id, algoritmo, clave, mensaje, salida, texto, lento, fuente;
    unsigned    repite = 1;
};

std::vector<uint8_t> de_hex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        v.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
    return v;
}

std::string a_hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

std::vector<Caso> lee(const std::string& ruta) {
    std::ifstream f(ruta);
    if (!f) { std::printf("no se puede abrir %s\n", ruta.c_str()); std::exit(2); }
    std::vector<Caso> v;
    std::string l;
    while (std::getline(f, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.empty()) continue;
        size_t p = l.find_first_not_of(" \t");
        if (p == std::string::npos || l[p] == '#') continue;
        if (l.substr(p) == "[caso]") { v.push_back(Caso{}); continue; }
        const size_t eq = l.find('=');
        if (eq == std::string::npos || v.empty()) continue;
        std::string k = l.substr(0, eq), val = l.substr(eq + 1);
        auto recorta = [](std::string& s) {
            const size_t a = s.find_first_not_of(" \t");
            if (a == std::string::npos) { s.clear(); return; }
            const size_t b = s.find_last_not_of(" \t");
            s = s.substr(a, b - a + 1);
        };
        recorta(k); recorta(val);
        Caso& c = v.back();
        if      (k == "id")        c.id = val;
        else if (k == "algoritmo") c.algoritmo = val;
        else if (k == "clave")     c.clave = val;
        else if (k == "mensaje")   c.mensaje = val;
        else if (k == "salida")    c.salida = val;
        else if (k == "texto")     c.texto = val;
        else if (k == "lento")     c.lento = val;
        else if (k == "fuente")    c.fuente = val;
        else if (k == "repite")    c.repite = unsigned(std::stoul(val));
    }
    return v;
}

} // namespace

int main(int argc, char** argv) {
    const bool lentos  = argc > 1 && std::string(argv[1]) == "--lentos";
    const bool detalle = [&]{ for (int i = 1; i < argc; ++i)
                                  if (std::string(argv[i]) == "--detalle") return true;
                              return false; }();

    const auto casos = lee("verif/vectores/hash.vec");
    unsigned ok = 0, fallo = 0, saltado = 0;

    for (const Caso& c : casos) {
        if (c.lento == "si" && !lentos) { ++saltado; continue; }

        const auto unidad = de_hex(c.mensaje);
        std::vector<uint8_t> m;
        m.reserve(unidad.size() * c.repite);
        for (unsigned r = 0; r < c.repite; ++r)
            m.insert(m.end(), unidad.begin(), unidad.end());

        uint8_t d[20] = {0};
        unsigned n = 0;
        const bool es_md5 = c.algoritmo.find("md5") != std::string::npos;
        const AlgoHash a  = es_md5 ? AlgoHash::Md5 : AlgoHash::Sha1;

        if (c.algoritmo == "md5" || c.algoritmo == "sha1") {
            Resumen r; r.inicia(a);
            r.bytes(m.data(), m.size());
            r.termina();
            r.digest(d); n = r.tam_digest();
        } else {
            const auto k = de_hex(c.clave);
            hmac(a, k.data(), k.size(), m.data(), m.size(), d);
            n = es_md5 ? 16u : 20u;
        }

        const std::string got = a_hex(d, n);
        if (got == c.salida) {
            ++ok;
            if (detalle) std::printf("  [ok    ] %-22s %s\n", c.id.c_str(), got.c_str());
        } else {
            ++fallo;
            std::printf("  [FALLO ] %-22s\n             esperado %s\n"
                        "             obtenido %s\n",
                        c.id.c_str(), c.salida.c_str(), got.c_str());
        }
    }

    std::printf("\nRESULTADO %u ok, %u fallos, %u saltados"
                " (de %zu casos de hash.vec)\n",
                ok, fallo, saltado, casos.size());
    if (saltado)
        std::printf("Los saltados son los marcados `lento = si`; "
                    "`make hash V=--lentos` los corre tambien.\n");
    return fallo ? 1 : 0;
}
