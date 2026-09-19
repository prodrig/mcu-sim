// =============================================================================
// cryp_algo.h — AES y DES, el núcleo aritmético del acelerador criptográfico
//
// La pareja de `hash_algo.h`, y por la misma razón: esto NO es SystemC y no
// sabe nada de registros ni de buses. Son los dos cifradores de bloque —AES de
// 128, 192 y 256 bits, y DES— y nada más. **El encadenado no está aquí**: ni
// CBC ni CTR, porque en el silicio esos XOR los hace el periférico alrededor
// del núcleo, y aquí se modela igual. `cryp.h` los pone.
//
// SE CALCULA, NO SE LLAMA, como en la unidad CRC y como en el HASH. Y se va un
// paso más allá: **la caja-S del AES tampoco es una tabla copiada**, se genera
// al arrancar desde su definición —el inverso multiplicativo en GF(2^8) más la
// transformación afín de la FIPS 197—. Con eso no hay 256 números que puedan
// estar mal copiados: si la construcción fuera incorrecta, los vectores del
// NIST no saldrían, y eso lo comprueba `make cryp`.
//
// Con DES no se puede hacer lo mismo y conviene decirlo: sus ocho cajas-S y sus
// seis permutaciones son tablas ARBITRARIAS —elegidas en 1976 y publicadas en
// la FIPS 46-3—, no se derivan de nada. Van escritas, y lo único que las
// protege es el vector clásico `"Now is t"` -> `3fa40e8a984d4815`, que está en
// `verif/vectores/cryp.vec` con dos implementaciones independientes detrás.
//
// LO QUE ESTE SILICIO NO TIENE: GCM y CCM son del F42x/F43x [RM0090 Rev 22,
// §23.2], así que aquí no están. No es una simplificación: es que el F415/F417
// no los lleva, y la tabla 114 -que es la suya- termina en el desplazamiento
// 0x4C, sin los dieciséis registros de contexto que harían falta.
// =============================================================================
#ifndef STM32_PERIPH_CRYP_ALGO_H
#define STM32_PERIPH_CRYP_ALGO_H

#include <cstdint>
#include <cstring>

namespace stm32 {

// ===========================================================================
// AES [FIPS PUB 197]
// ===========================================================================

// --- La caja-S, generada y no copiada --------------------------------------
// El byte b se sustituye por afin(inv(b)), con `inv` el inverso multiplicativo
// en GF(2^8) módulo x^8+x^4+x^3+x+1 —y 0 va a 0— y `afin` la transformación
// de la §5.1.1 de la FIPS 197.
struct CajasAes {
    uint8_t s[256], si[256];
    CajasAes() {
        uint8_t p = 1, q = 1;
        do {                                    // p recorre el grupo, q = p^-1
            p = uint8_t(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
            q ^= uint8_t(q << 1);  q ^= uint8_t(q << 2);  q ^= uint8_t(q << 4);
            if (q & 0x80) q ^= 0x09;
            const uint8_t x = uint8_t(q ^ rot_(q,1) ^ rot_(q,2) ^ rot_(q,3) ^ rot_(q,4) ^ 0x63);
            s[p] = x; si[x] = p;
        } while (p != 1);
        s[0] = 0x63; si[0x63] = 0;
    }
    static uint8_t rot_(uint8_t x, unsigned n) { return uint8_t((x << n) | (x >> (8 - n))); }
};
inline const CajasAes& cajas_aes() { static const CajasAes c; return c; }

inline uint8_t xtime(uint8_t x) { return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
inline uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; }
    return r;
}

// La clave expandida: hasta 15 rondas (AES-256), 4 palabras por ronda.
struct ClaveAes {
    uint8_t rk[15][16] = {{0}};
    unsigned nr = 10;
};

inline void expande_aes(const uint8_t* clave, unsigned nbytes, ClaveAes& k) {
    const uint8_t* S = cajas_aes().s;
    const unsigned nk = nbytes / 4;             // 4, 6 u 8 palabras
    k.nr = nk + 6;                              // 10, 12 o 14 rondas
    uint8_t w[60][4];
    for (unsigned i = 0; i < nk; ++i)
        for (unsigned j = 0; j < 4; ++j) w[i][j] = clave[4*i + j];
    uint8_t rcon = 1;
    for (unsigned i = nk; i < 4 * (k.nr + 1); ++i) {
        uint8_t t[4] = { w[i-1][0], w[i-1][1], w[i-1][2], w[i-1][3] };
        if (i % nk == 0) {
            const uint8_t tmp = t[0];
            t[0] = uint8_t(S[t[1]] ^ rcon); t[1] = S[t[2]];
            t[2] = S[t[3]];                 t[3] = S[tmp];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (unsigned j = 0; j < 4; ++j) t[j] = S[t[j]];
        }
        for (unsigned j = 0; j < 4; ++j) w[i][j] = uint8_t(w[i-nk][j] ^ t[j]);
    }
    for (unsigned r = 0; r <= k.nr; ++r)
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned j = 0; j < 4; ++j) k.rk[r][4*c + j] = w[4*r + c][j];
}

inline void suma_ronda_(uint8_t st[16], const uint8_t* rk) {
    for (unsigned i = 0; i < 16; ++i) st[i] ^= rk[i];
}

inline void cifra_aes(const ClaveAes& k, const uint8_t in[16], uint8_t out[16]) {
    const uint8_t* S = cajas_aes().s;
    uint8_t st[16];
    std::memcpy(st, in, 16);
    suma_ronda_(st, k.rk[0]);
    for (unsigned r = 1; r <= k.nr; ++r) {
        for (unsigned i = 0; i < 16; ++i) st[i] = S[st[i]];
        // ShiftRows: el estado va por columnas, la fila j son los bytes 4c+j
        uint8_t t[16];
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned j = 0; j < 4; ++j) t[4*c + j] = st[4*((c + j) & 3) + j];
        if (r != k.nr) {                        // MixColumns, salvo en la última
            for (unsigned c = 0; c < 4; ++c) {
                const uint8_t a0 = t[4*c], a1 = t[4*c+1], a2 = t[4*c+2], a3 = t[4*c+3];
                st[4*c+0] = uint8_t(gmul(a0,2) ^ gmul(a1,3) ^ a2 ^ a3);
                st[4*c+1] = uint8_t(a0 ^ gmul(a1,2) ^ gmul(a2,3) ^ a3);
                st[4*c+2] = uint8_t(a0 ^ a1 ^ gmul(a2,2) ^ gmul(a3,3));
                st[4*c+3] = uint8_t(gmul(a0,3) ^ a1 ^ a2 ^ gmul(a3,2));
            }
        } else {
            std::memcpy(st, t, 16);
        }
        suma_ronda_(st, k.rk[r]);
    }
    std::memcpy(out, st, 16);
}

inline void descifra_aes(const ClaveAes& k, const uint8_t in[16], uint8_t out[16]) {
    const uint8_t* SI = cajas_aes().si;
    uint8_t st[16];
    std::memcpy(st, in, 16);
    suma_ronda_(st, k.rk[k.nr]);
    for (unsigned r = k.nr; r >= 1; --r) {
        uint8_t t[16];
        for (unsigned c = 0; c < 4; ++c)        // InvShiftRows
            for (unsigned j = 0; j < 4; ++j) t[4*((c + j) & 3) + j] = st[4*c + j];
        for (unsigned i = 0; i < 16; ++i) t[i] = SI[t[i]];
        suma_ronda_(t, k.rk[r-1]);
        if (r != 1) {                           // InvMixColumns
            for (unsigned c = 0; c < 4; ++c) {
                const uint8_t a0 = t[4*c], a1 = t[4*c+1], a2 = t[4*c+2], a3 = t[4*c+3];
                st[4*c+0] = uint8_t(gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3,9));
                st[4*c+1] = uint8_t(gmul(a0,9)  ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13));
                st[4*c+2] = uint8_t(gmul(a0,13) ^ gmul(a1,9)  ^ gmul(a2,14) ^ gmul(a3,11));
                st[4*c+3] = uint8_t(gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2,9)  ^ gmul(a3,14));
            }
        } else {
            std::memcpy(st, t, 16);
        }
    }
    std::memcpy(out, st, 16);
}

// ===========================================================================
// DES [FIPS PUB 46-3]
//
// Aquí no hay nada que generar: las ocho cajas-S y las seis permutaciones son
// tablas arbitrarias del diseño original. Van escritas tal cual, numeradas
// desde 1 como en el estándar, y la conversión a índices de C se hace al usarlas
// para no tener que reescribirlas.
// ===========================================================================
namespace des {

inline const uint8_t PC1[56] = {
    57,49,41,33,25,17, 9, 1,58,50,42,34,26,18,
    10, 2,59,51,43,35,27,19,11, 3,60,52,44,36,
    63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
    14, 6,61,53,45,37,29,21,13, 5,28,20,12, 4 };
inline const uint8_t PC2[48] = {
    14,17,11,24, 1, 5, 3,28,15, 6,21,10,
    23,19,12, 4,26, 8,16, 7,27,20,13, 2,
    41,52,31,37,47,55,30,40,51,45,33,48,
    44,49,39,56,34,53,46,42,50,36,29,32 };
inline const uint8_t DESPL[16] = { 1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1 };
inline const uint8_t IP[64] = {
    58,50,42,34,26,18,10, 2,60,52,44,36,28,20,12, 4,
    62,54,46,38,30,22,14, 6,64,56,48,40,32,24,16, 8,
    57,49,41,33,25,17, 9, 1,59,51,43,35,27,19,11, 3,
    61,53,45,37,29,21,13, 5,63,55,47,39,31,23,15, 7 };
inline const uint8_t FP[64] = {
    40, 8,48,16,56,24,64,32,39, 7,47,15,55,23,63,31,
    38, 6,46,14,54,22,62,30,37, 5,45,13,53,21,61,29,
    36, 4,44,12,52,20,60,28,35, 3,43,11,51,19,59,27,
    34, 2,42,10,50,18,58,26,33, 1,41, 9,49,17,57,25 };
inline const uint8_t E[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9,
     8, 9,10,11,12,13,12,13,14,15,16,17,
    16,17,18,19,20,21,20,21,22,23,24,25,
    24,25,26,27,28,29,28,29,30,31,32, 1 };
inline const uint8_t P[32] = {
    16, 7,20,21,29,12,28,17, 1,15,23,26, 5,18,31,10,
     2, 8,24,14,32,27, 3, 9,19,13,30, 6,22,11, 4,25 };
inline const uint8_t S[8][64] = {
 {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7, 0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
   4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
 {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10, 3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
   0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
 {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
   13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7, 1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
 {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
   10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4, 3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
 {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
   4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
 {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11, 10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
   9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6, 4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
 {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
   1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2, 6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
 {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7, 1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
   7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8, 2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11} };

// Aplica una tabla de permutación de `n` posiciones sobre un valor de `ancho`
// bits, con la numeración del estándar: el bit 1 es el MÁS significativo.
inline uint64_t permuta(uint64_t v, const uint8_t* t, unsigned n, unsigned ancho) {
    uint64_t r = 0;
    for (unsigned i = 0; i < n; ++i) {
        const uint64_t bit = (v >> (ancho - t[i])) & 1ull;
        r = (r << 1) | bit;
    }
    return r;
}

struct Subclaves { uint64_t k[16] = {0}; };

inline void expande(uint64_t clave, Subclaves& sk) {
    const uint64_t cd = permuta(clave, PC1, 56, 64);
    uint32_t c = uint32_t(cd >> 28) & 0x0FFFFFFFu;
    uint32_t d = uint32_t(cd)       & 0x0FFFFFFFu;
    for (unsigned i = 0; i < 16; ++i) {
        const unsigned s = DESPL[i];
        c = ((c << s) | (c >> (28 - s))) & 0x0FFFFFFFu;
        d = ((d << s) | (d >> (28 - s))) & 0x0FFFFFFFu;
        sk.k[i] = permuta((uint64_t(c) << 28) | d, PC2, 48, 56);
    }
}

inline uint32_t f(uint32_t r, uint64_t sk) {
    const uint64_t x = permuta(r, E, 48, 32) ^ sk;
    uint32_t out = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned seis = unsigned((x >> (42 - 6*i)) & 0x3Fu);
        const unsigned fila = ((seis >> 4) & 2) | (seis & 1);
        const unsigned col  = (seis >> 1) & 0xF;
        out = (out << 4) | S[i][fila * 16 + col];
    }
    return uint32_t(permuta(out, P, 32, 32));
}

// Un bloque de 64 bits. `descifra` invierte el orden de las subclaves, que es
// la única diferencia entre cifrar y descifrar en DES.
inline uint64_t bloque(const Subclaves& sk, uint64_t in, bool descifra) {
    uint64_t v = permuta(in, IP, 64, 64);
    uint32_t l = uint32_t(v >> 32), r = uint32_t(v);
    for (unsigned i = 0; i < 16; ++i) {
        const uint32_t t = r;
        r = l ^ f(r, sk.k[descifra ? 15 - i : i]);
        l = t;
    }
    return permuta((uint64_t(r) << 32) | l, FP, 64, 64);
}

} // namespace des

// TDES en su forma EDE: cifrar = E(k3, D(k2, E(k1, m))). Con k1 = k3 es el TDES
// de dos claves que el CRYP llama «clave de 128 bits».
inline uint64_t cifra_tdes(const des::Subclaves& k1, const des::Subclaves& k2,
                           const des::Subclaves& k3, uint64_t m, bool descifra) {
    if (!descifra) return des::bloque(k3, des::bloque(k2, des::bloque(k1, m, false), true), false);
    return des::bloque(k1, des::bloque(k2, des::bloque(k3, m, true), false), true);
}

} // namespace stm32
#endif // STM32_PERIPH_CRYP_ALGO_H
