// =============================================================================
// prueba_cryp.cpp — los vectores oficiales contra el núcleo de AES y DES
//
// La pareja de `prueba_hash.cpp`, y con el mismo reparto: esto NO necesita
// SystemC y comprueba `periph/cryp_algo.h`, que es aritmética pura. Lee
// `verif/vectores/cryp.vec` —los 16 casos que trajo la fase 0, con su
// procedencia— y los pasa por el núcleo en las DOS direcciones: cifrar la
// entrada tiene que dar la salida, y descifrar la salida tiene que devolver la
// entrada.
//
// El ENCADENADO —CBC y CTR— se hace aquí, igual que lo hará el periférico,
// porque en el silicio esos XOR los hace el bloque alrededor del núcleo y no el
// núcleo. Que el mismo encadenado esté escrito dos veces no es duplicación
// tonta: esta prueba valida la aritmética sin bus de por medio, y el banco
// valida el camino por el bus. Si las dos coinciden con el NIST, las dos están
// bien.
//
//   make cryp
// =============================================================================
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include "../periph/cryp_algo.h"

using namespace stm32;

namespace {

struct Caso {
    std::string id, algoritmo, clave, iv, entrada, salida, nota, fuente;
};

std::vector<uint8_t> de_hex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        v.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
    return v;
}
std::string a_hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}
uint64_t a_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}
void de_u64(uint64_t v, uint8_t* p) {
    for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(v >> (56 - 8*i));
}

std::vector<Caso> lee() {
    std::ifstream f("verif/vectores/cryp.vec");
    if (!f) { std::printf("no se puede abrir verif/vectores/cryp.vec "
                          "(ejecuta desde src/)\n"); std::exit(2); }
    std::vector<Caso> v;
    std::string l;
    while (std::getline(f, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (l.empty()) continue;
        const size_t p = l.find_first_not_of(" \t");
        if (p == std::string::npos || l[p] == '#') continue;
        if (l.substr(p) == "[caso]") { v.push_back(Caso{}); continue; }
        const size_t eq = l.find('=');
        if (eq == std::string::npos || v.empty()) continue;
        std::string k = l.substr(0, eq), val = l.substr(eq + 1);
        auto rec = [](std::string& s) {
            const size_t a = s.find_first_not_of(" \t");
            if (a == std::string::npos) { s.clear(); return; }
            s = s.substr(a, s.find_last_not_of(" \t") - a + 1);
        };
        rec(k); rec(val);
        Caso& c = v.back();
        if      (k == "id")        c.id = val;
        else if (k == "algoritmo") c.algoritmo = val;
        else if (k == "clave")     c.clave = val;
        else if (k == "iv")        c.iv = val;
        else if (k == "entrada")   c.entrada = val;
        else if (k == "salida")    c.salida = val;
        else if (k == "nota")      c.nota = val;
        else if (k == "fuente")    c.fuente = val;
    }
    return v;
}

// ---------------------------------------------------------------------------
// El encadenado, que es lo que el periférico pone alrededor del núcleo
// ---------------------------------------------------------------------------
std::vector<uint8_t> procesa(const Caso& c, const std::vector<uint8_t>& in, bool descifra) {
    const auto clave = de_hex(c.clave);
    const auto iv    = de_hex(c.iv);
    std::vector<uint8_t> out(in.size());

    const bool aes = c.algoritmo.rfind("aes", 0) == 0;
    const std::string modo = c.algoritmo.substr(c.algoritmo.find('-') + 1);

    if (aes) {
        ClaveAes k;
        expande_aes(clave.data(), unsigned(clave.size()), k);
        uint8_t enc[16], prev[16] = {0};
        if (!iv.empty()) std::memcpy(prev, iv.data(), 16);
        for (size_t i = 0; i < in.size(); i += 16) {
            if (modo == "ecb") {
                if (descifra) descifra_aes(k, &in[i], &out[i]);
                else          cifra_aes(k, &in[i], &out[i]);
            } else if (modo == "cbc") {
                if (!descifra) {
                    uint8_t x[16];
                    for (unsigned j = 0; j < 16; ++j) x[j] = uint8_t(in[i+j] ^ prev[j]);
                    cifra_aes(k, x, &out[i]);
                    std::memcpy(prev, &out[i], 16);
                } else {
                    uint8_t x[16];
                    descifra_aes(k, &in[i], x);
                    for (unsigned j = 0; j < 16; ++j) out[i+j] = uint8_t(x[j] ^ prev[j]);
                    std::memcpy(prev, &in[i], 16);
                }
            } else {                            // CTR: cifrar y descifrar es lo mismo
                cifra_aes(k, prev, enc);
                for (unsigned j = 0; j < 16; ++j) out[i+j] = uint8_t(in[i+j] ^ enc[j]);
                for (int j = 15; j >= 0; --j) if (++prev[j]) break;
            }
        }
        return out;
    }

    // DES y TDES: bloques de 64 bits
    const bool tdes = c.algoritmo.rfind("tdes", 0) == 0;
    des::Subclaves k1, k2, k3;
    if (tdes) {
        des::expande(a_u64(&clave[0]),  k1);
        des::expande(a_u64(&clave[8]),  k2);
        des::expande(a_u64(&clave[16]), k3);
    } else {
        des::expande(a_u64(&clave[0]), k1);
    }
    uint64_t prev = iv.empty() ? 0 : a_u64(iv.data());
    for (size_t i = 0; i < in.size(); i += 8) {
        const uint64_t m = a_u64(&in[i]);
        uint64_t r;
        if (modo == "ecb") {
            r = tdes ? cifra_tdes(k1, k2, k3, m, descifra) : des::bloque(k1, m, descifra);
        } else if (!descifra) {
            const uint64_t x = m ^ prev;
            r = tdes ? cifra_tdes(k1, k2, k3, x, false) : des::bloque(k1, x, false);
            prev = r;
        } else {
            const uint64_t x = tdes ? cifra_tdes(k1, k2, k3, m, true) : des::bloque(k1, m, true);
            r = x ^ prev;
            prev = m;
        }
        de_u64(r, &out[i]);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const bool detalle = argc > 1 && std::string(argv[1]) == "--detalle";
    const auto casos = lee();
    unsigned ok = 0, fallo = 0;

    for (const Caso& c : casos) {
        const auto ent = de_hex(c.entrada);
        const auto sal = de_hex(c.salida);

        const std::string cifrado = a_hex(procesa(c, ent, false));
        if (cifrado == c.salida) {
            ++ok;
            if (detalle) std::printf("  [ok    ] %-14s cifrar\n", c.id.c_str());
        } else {
            ++fallo;
            std::printf("  [FALLO ] %s / cifrar\n             esperado %s\n"
                        "             obtenido %s\n",
                        c.id.c_str(), c.salida.c_str(), cifrado.c_str());
        }

        // La vuelta. En CTR es la MISMA operacion, y comprobarlo tambien vale.
        const std::string claro = a_hex(procesa(c, sal, true));
        if (claro == c.entrada) {
            ++ok;
            if (detalle) std::printf("  [ok    ] %-14s descifrar\n", c.id.c_str());
        } else {
            ++fallo;
            std::printf("  [FALLO ] %s / descifrar\n             esperado %s\n"
                        "             obtenido %s\n",
                        c.id.c_str(), c.entrada.c_str(), claro.c_str());
        }
    }

    std::printf("\nRESULTADO %u ok, %u fallos (de %zu casos de cryp.vec, "
                "en las dos direcciones)\n", ok, fallo, casos.size());
    return fallo ? 1 : 0;
}
