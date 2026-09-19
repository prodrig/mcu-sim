// =============================================================================
// hash_algo.h — MD5 y SHA-1, el núcleo aritmético del procesador de resumen
//
// Esto NO es SystemC y no sabe nada de registros ni de buses: son los dos
// algoritmos y la mecánica de relleno, puestos aparte a propósito. `hash.h`
// —el periférico— los usa; la prueba `verif/prueba_hash.cpp` los usa sin
// arrancar una simulación; y el día que haga falta un SHA-2 para otra familia,
// entra aquí y no toca el periférico.
//
// SE CALCULA, NO SE LLAMA. Igual que la unidad CRC calcula el polinomio bit a
// bit en vez de usar una tabla o una biblioteca [periph/crc.h], aquí están las
// dos rondas escritas. Un simulador didáctico que delegara el cifrado o el
// resumen en OpenSSL no estaría modelando el periférico: estaría tapándolo. Lo
// que sí hace el proyecto es COMPROBAR el resultado contra dos
// implementaciones independientes, y eso está en `verif/vectores/`.
//
// LA PARTE QUE NO SE VE Y DA LA LATA: el F415/F417 acepta mensajes cuya
// longitud NO es múltiplo de ocho bits. `HASH_STR.NBLW` dice cuántos bits de la
// última palabra escrita valen —de 0 a 31—, y el relleno se hace a partir de
// ahí. Por eso el acumulador de aquí abajo no trabaja con bytes sino con BITS:
// un `std::string` de bytes no sabría representar un mensaje de 27 bits, y el
// silicio sí. [RM0090 Rev 22, §25.3.4 y §25.4.4]
//
// EL CONVENIO DEL RESULTADO. Los `HASH_HRx`, leídos como bytes en BIG-ENDIAN,
// son la secuencia del resumen. Para SHA-1 eso es el estado tal cual; para MD5
// hay que dar la vuelta a cada palabra, porque MD5 publica su resumen en
// little-endian. No es una interpretación: es lo que hace el HAL de ST, que lee
// `__REV(HASH->HR[i])` para las dos [stm32f4xx_hal_hash.c, HASH_GetDigest], y
// es lo que hace que el resumen del modelo se compare con el de `sha1sum` sin
// más ceremonia.
// =============================================================================
#ifndef STM32_PERIPH_HASH_ALGO_H
#define STM32_PERIPH_HASH_ALGO_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace stm32 {

enum class AlgoHash { Sha1, Md5 };

// ---------------------------------------------------------------------------
// Las dos rondas. Cada una toma un bloque de 512 bits —64 bytes en el orden del
// bit-string, que es el orden en que el mensaje entra por `HASH_DIN`— y avanza
// el estado.
// ---------------------------------------------------------------------------

inline uint32_t rotl32(uint32_t x, unsigned n) {
    return n == 0 ? x : uint32_t((x << n) | (x >> (32 - n)));
}

// --- SHA-1 [FIPS PUB 180-2] -------------------------------------------------
// Cinco palabras de estado, ochenta rondas, cuatro constantes. Las palabras del
// bloque se leen en BIG-ENDIAN, que es el orden del bit-string.
inline void sha1_bloque(uint32_t h[5], const uint8_t b[64]) {
    uint32_t w[80];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t(b[4*i]) << 24) | (uint32_t(b[4*i+1]) << 16) |
               (uint32_t(b[4*i+2]) << 8) | uint32_t(b[4*i+3]);
    for (unsigned i = 16; i < 80; ++i)
        w[i] = rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (unsigned i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20)      { f = (bb & c) | (~bb & d);            k = 0x5A827999u; }
        else if (i < 40) { f = bb ^ c ^ d;                      k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d);   k = 0x8F1BBCDCu; }
        else             { f = bb ^ c ^ d;                      k = 0xCA62C1D6u; }
        const uint32_t t = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(bb, 30); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

// --- MD5 [RFC 1321] ---------------------------------------------------------
// Cuatro palabras de estado, sesenta y cuatro rondas. Las palabras del bloque
// se leen en LITTLE-ENDIAN: es la otra convención, y confundirla es el error
// clásico. La tabla `T` es la del propio RFC —floor(2^32 · |sin(i+1)|)— y va
// escrita y no calculada con `sin()` para no depender de la biblioteca
// matemática de cada máquina; si una constante estuviera mal, MD5("abc") no
// saldría, y eso lo comprueba `verif/vectores/hash.vec`.
inline void md5_bloque(uint32_t h[4], const uint8_t b[64]) {
    static const uint32_t T[64] = {
        0xD76AA478u,0xE8C7B756u,0x242070DBu,0xC1BDCEEEu,
        0xF57C0FAFu,0x4787C62Au,0xA8304613u,0xFD469501u,
        0x698098D8u,0x8B44F7AFu,0xFFFF5BB1u,0x895CD7BEu,
        0x6B901122u,0xFD987193u,0xA679438Eu,0x49B40821u,
        0xF61E2562u,0xC040B340u,0x265E5A51u,0xE9B6C7AAu,
        0xD62F105Du,0x02441453u,0xD8A1E681u,0xE7D3FBC8u,
        0x21E1CDE6u,0xC33707D6u,0xF4D50D87u,0x455A14EDu,
        0xA9E3E905u,0xFCEFA3F8u,0x676F02D9u,0x8D2A4C8Au,
        0xFFFA3942u,0x8771F681u,0x6D9D6122u,0xFDE5380Cu,
        0xA4BEEA44u,0x4BDECFA9u,0xF6BB4B60u,0xBEBFBC70u,
        0x289B7EC6u,0xEAA127FAu,0xD4EF3085u,0x04881D05u,
        0xD9D4D039u,0xE6DB99E5u,0x1FA27CF8u,0xC4AC5665u,
        0xF4292244u,0x432AFF97u,0xAB9423A7u,0xFC93A039u,
        0x655B59C3u,0x8F0CCC92u,0xFFEFF47Du,0x85845DD1u,
        0x6FA87E4Fu,0xFE2CE6E0u,0xA3014314u,0x4E0811A1u,
        0xF7537E82u,0xBD3AF235u,0x2AD7D2BBu,0xEB86D391u };
    static const unsigned S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21 };

    uint32_t m[16];
    for (unsigned i = 0; i < 16; ++i)
        m[i] = uint32_t(b[4*i]) | (uint32_t(b[4*i+1]) << 8) |
               (uint32_t(b[4*i+2]) << 16) | (uint32_t(b[4*i+3]) << 24);

    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t f; unsigned g;
        if (i < 16)      { f = (bb & c) | (~bb & d);      g = i; }
        else if (i < 32) { f = (d & bb) | (~d & c);       g = (5*i + 1) % 16; }
        else if (i < 48) { f = bb ^ c ^ d;                g = (3*i + 5) % 16; }
        else             { f = c ^ (bb | ~d);             g = (7*i) % 16; }
        const uint32_t t = d;
        d = c; c = bb;
        bb = bb + rotl32(a + f + T[i] + m[g], S[i]);
        a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d;
}

// ---------------------------------------------------------------------------
// EL ACUMULADOR: mensaje a bits, bloques de 512 y relleno automático
//
// Reproduce lo que hace el bloque por su cuenta, que es más de lo que parece:
// contar bits, trocear en bloques de 512, meter el «1» donde diga `NBLW`,
// rellenar con ceros hasta 448 mod 512 y cerrar con la longitud en 64 bits.
// [RM0090 Rev 22, §25.3.4]
//
// LA PALABRA PENDIENTE no es un capricho de implementación: es el registro
// `HASH_DIN` del silicio. Una palabra escrita se queda ahí —y `DINNE` lo dice—
// hasta que llega la siguiente o hasta que `DCAL` la recorta a `NBLW` bits.
// Sin ese pendiente no se podría recortar la última palabra de un bloque que ya
// estaría procesado.
// ---------------------------------------------------------------------------
class Resumen {
public:
    void inicia(AlgoHash a) {
        algo_ = a;
        if (a == AlgoHash::Sha1) {
            h_[0] = 0x67452301u; h_[1] = 0xEFCDAB89u; h_[2] = 0x98BADCFEu;
            h_[3] = 0x10325476u; h_[4] = 0xC3D2E1F0u;
        } else {
            h_[0] = 0x67452301u; h_[1] = 0xEFCDAB89u; h_[2] = 0x98BADCFEu;
            h_[3] = 0x10325476u; h_[4] = 0u;
        }
        n_buf_ = 0; total_bits_ = 0; parcial_ = 0; n_parcial_ = 0;
        hay_pend_ = false; pend_ = 0; bloques_ = 0; cerrado_ = false;
    }

    // Una palabra del bit-string, ya pasada por el intercambio de `DATATYPE`.
    void palabra(uint32_t w) {
        if (hay_pend_) mete_bits(pend_, 32);
        pend_ = w; hay_pend_ = true;
    }

    // `DCAL`: `nblw` bits de la última palabra escrita valen. Cero significa
    // que vale ENTERA, que es lo que hace el HAL de ST -`8 * (tamaño % 4)`-
    // cuando el mensaje mide un múltiplo de cuatro bytes.
    void final(unsigned nblw) {
        if (hay_pend_) {
            mete_bits(pend_, nblw == 0 ? 32u : nblw);
            hay_pend_ = false;
        }
        rellena_();
        cerrado_ = true;
    }

    // Sin pasar por el protocolo de registros: para la prueba y para las
    // vueltas internas del HMAC.
    void bytes(const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) mete_bits(uint32_t(p[i]) << 24, 8);
    }
    void termina() { rellena_(); cerrado_ = true; }

    // --- Resultado ----------------------------------------------------------
    unsigned palabras() const { return algo_ == AlgoHash::Sha1 ? 5u : 4u; }
    // El valor de `HASH_HRx`: big-endian = la secuencia del resumen.
    uint32_t hr(unsigned i) const {
        if (i >= 5) return 0;
        if (algo_ == AlgoHash::Sha1) return h_[i];
        return __builtin_bswap32(h_[i]);     // MD5 publica en little-endian
    }
    void digest(uint8_t* out) const {
        for (unsigned i = 0; i < palabras(); ++i) {
            const uint32_t v = hr(i);
            out[4*i+0] = uint8_t(v >> 24); out[4*i+1] = uint8_t(v >> 16);
            out[4*i+2] = uint8_t(v >> 8);  out[4*i+3] = uint8_t(v);
        }
    }
    unsigned tam_digest() const { return palabras() * 4; }

    // --- Lo que el periférico necesita saber --------------------------------
    uint64_t bloques() const { return bloques_; }   // para cobrar los ciclos
    bool     hay_pendiente() const { return hay_pend_; }  // es `DINNE`
    bool     cerrado() const { return cerrado_; }
    uint64_t bits() const { return total_bits_; }

private:
    // Mete los `n` bits ALTOS de `w` (1..32) en el bit-string.
    void mete_bits(uint32_t w, unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            const unsigned bit = (w >> (31 - i)) & 1u;
            parcial_ = uint8_t((parcial_ << 1) | bit);
            if (++n_parcial_ == 8) { empuja_byte_(parcial_); parcial_ = 0; n_parcial_ = 0; }
            ++total_bits_;
        }
    }
    void empuja_byte_(uint8_t b) {
        buf_[n_buf_++] = b;
        if (n_buf_ == 64) { procesa_(); n_buf_ = 0; }
    }
    void procesa_() {
        if (algo_ == AlgoHash::Sha1) sha1_bloque(h_, buf_); else md5_bloque(h_, buf_);
        ++bloques_;
    }
    void rellena_() {
        const uint64_t L = total_bits_;
        // El «1», en la posición que toque dentro del byte a medias.
        parcial_ = uint8_t((parcial_ << 1) | 1u);
        ++n_parcial_;
        parcial_ = uint8_t(parcial_ << (8 - n_parcial_));
        empuja_byte_(parcial_);
        parcial_ = 0; n_parcial_ = 0;
        // Ceros hasta dejar sitio a los ocho bytes de la longitud.
        while (n_buf_ != 56) empuja_byte_(0);
        // La longitud: big-endian en SHA-1, little-endian en MD5.
        for (unsigned i = 0; i < 8; ++i) {
            const unsigned desp = (algo_ == AlgoHash::Sha1) ? (56 - 8*i) : (8*i);
            empuja_byte_(uint8_t(L >> desp));
        }
    }

    AlgoHash algo_ = AlgoHash::Sha1;
    uint32_t h_[5] = {0,0,0,0,0};
    uint8_t  buf_[64] = {0};
    unsigned n_buf_ = 0;
    uint64_t total_bits_ = 0;
    uint8_t  parcial_ = 0;
    unsigned n_parcial_ = 0;
    uint32_t pend_ = 0;
    bool     hay_pend_ = false;
    uint64_t bloques_ = 0;
    bool     cerrado_ = false;
};

// ---------------------------------------------------------------------------
// HMAC, tal y como lo describe el propio manual:
//
//   HMAC(m) = H[ (K' XOR 0x5C) | H[ (K' XOR 0x36) | m ] ]
//
// con `K'` la clave rellenada a 64 bytes, o el RESUMEN de la clave si mide más
// de 64 —que es exactamente lo que dice el bit `LKEY` del `HASH_CR`—.
// [RM0090 Rev 22, §25.3.6]
//
// Aquí está como función porque la prueba de los vectores la necesita entera;
// el periférico NO la llama: reproduce las cuatro fases con sus `DCAL`, que es
// lo que hace el silicio y lo que el firmware ve.
// ---------------------------------------------------------------------------
inline void clave_hmac(AlgoHash a, const uint8_t* k, size_t nk, uint8_t kp[64]) {
    std::memset(kp, 0, 64);
    if (nk > 64) {                       // LKEY = 1: la clave se resume
        Resumen r; r.inicia(a); r.bytes(k, nk); r.termina();
        r.digest(kp);
    } else {
        std::memcpy(kp, k, nk);
    }
}

inline void hmac(AlgoHash a, const uint8_t* k, size_t nk,
                 const uint8_t* m, size_t nm, uint8_t* out) {
    uint8_t kp[64], ipad[64], opad[64];
    clave_hmac(a, k, nk, kp);
    for (unsigned i = 0; i < 64; ++i) {
        ipad[i] = uint8_t(kp[i] ^ 0x36u);
        opad[i] = uint8_t(kp[i] ^ 0x5Cu);
    }
    Resumen dentro; dentro.inicia(a);
    dentro.bytes(ipad, 64); dentro.bytes(m, nm); dentro.termina();
    uint8_t d[20]; dentro.digest(d);

    Resumen fuera; fuera.inicia(a);
    fuera.bytes(opad, 64); fuera.bytes(d, dentro.tam_digest()); fuera.termina();
    fuera.digest(out);
}

} // namespace stm32
#endif // STM32_PERIPH_HASH_ALGO_H
