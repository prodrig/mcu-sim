// =============================================================================
// bignum_esp.h — Aritmética de números grandes para el RSA y el DS de los ESP32
//
// Números en base b = 2^32, palabra menos significativa primero (como en
// RSA_X_MEM y compañía [TRM-C3 §20.3.1]). Solo lo que hace el hardware:
//
//   mont(A, B)   A·B·R^-1 mod M, con R = b^n y M' = -M^-1 mod b: el producto
//                de Montgomery (CIOS) con la resta final. Con M' mal
//                calculado da basura, como el silicio: el hardware no
//                comprueba M' ni r, los usa.
//   mul(A, B)    el producto entero de 2n palabras.
// =============================================================================
#ifndef STM32_PERIPH_ESP_BIGNUM_ESP_H
#define STM32_PERIPH_ESP_BIGNUM_ESP_H

#include <cstdint>
#include <vector>

namespace stm32 {
namespace bignum {

using Num = std::vector<uint32_t>;

// T >= M en n+1 palabras (T[n] es el acarreo)
inline bool mayor_igual(const Num& t, const Num& m, unsigned n) {
    if (t[n]) return true;
    for (unsigned i = n; i-- > 0;) {
        if (t[i] != m[i]) return t[i] > m[i];
    }
    return true;
}

// A·B·R^-1 mod M (CIOS)
inline Num mont(const Num& a, const Num& b, const Num& m, uint32_t mp, unsigned n) {
    Num t(n + 2, 0);
    for (unsigned i = 0; i < n; ++i) {
        uint64_t c = 0;
        for (unsigned j = 0; j < n; ++j) {
            const uint64_t s = uint64_t(t[j]) + uint64_t(a[j]) * b[i] + c;
            t[j] = uint32_t(s);
            c = s >> 32;
        }
        uint64_t s = uint64_t(t[n]) + c;
        t[n] = uint32_t(s);
        t[n + 1] = uint32_t(s >> 32);
        const uint32_t q = t[0] * mp;
        s = uint64_t(t[0]) + uint64_t(q) * m[0];
        c = s >> 32;
        for (unsigned j = 1; j < n; ++j) {
            s = uint64_t(t[j]) + uint64_t(q) * m[j] + c;
            t[j - 1] = uint32_t(s);
            c = s >> 32;
        }
        s = uint64_t(t[n]) + c;
        t[n - 1] = uint32_t(s);
        t[n] = t[n + 1] + uint32_t(s >> 32);
    }
    if (mayor_igual(t, m, n)) {
        int64_t br = 0;
        for (unsigned j = 0; j < n; ++j) {
            const int64_t d = int64_t(t[j]) - int64_t(m[j]) - br;
            t[j] = uint32_t(d);
            br = d < 0 ? 1 : 0;
        }
    }
    t.resize(n);
    return t;
}

// El producto entero: 2n palabras
inline Num mul(const Num& a, const Num& b, unsigned n) {
    Num z(2 * n, 0);
    for (unsigned i = 0; i < n; ++i) {
        uint64_t c = 0;
        for (unsigned j = 0; j < n; ++j) {
            const uint64_t s = uint64_t(z[i + j]) + uint64_t(a[j]) * b[i] + c;
            z[i + j] = uint32_t(s);
            c = s >> 32;
        }
        z[i + n] = uint32_t(c);
    }
    return z;
}

inline bool bit(const Num& y, unsigned i) { return (y[i / 32] >> (i % 32)) & 1u; }

// X^Y mod M como lo hace el acelerador: X a la forma de Montgomery con r
// (= R^2 mod M), Z = R mod M, cuadrados y productos de izquierda a derecha y,
// al final, fuera de la forma de Montgomery. `desde`: el primer bit de Y que
// se mira (SEARCH_POS, o N-1); `cte`: tiempo constante (también el producto
// de los bits a 0, que se descarta). Devuelve los productos de Montgomery
// hechos (el tiempo), y deja en `xm` la X en forma de Montgomery (la que
// queda en RSA_X_MEM).
inline unsigned modexp(const Num& x, const Num& y, const Num& m, const Num& r, uint32_t mp, unsigned n,
                       int desde, bool cte, Num& z, Num& xm) {
    Num uno(n, 0);
    uno[0] = 1;
    xm = mont(x, r, m, mp, n);
    z = mont(r, uno, m, mp, n);                       // R mod M
    unsigned k = 2;
    bool empezado = cte;
    for (int i = desde; i >= 0; --i) {
        const bool b = bit(y, unsigned(i));
        if (!empezado && !b) continue;                // sin tiempo constante: los ceros de arriba no cuestan
        empezado = true;
        z = mont(z, z, m, mp, n); ++k;
        if (b) { z = mont(z, xm, m, mp, n); ++k; }
        else if (cte) { (void)mont(z, xm, m, mp, n); ++k; }
    }
    z = mont(z, uno, m, mp, n); ++k;
    return k;
}

} // namespace bignum
} // namespace stm32
#endif // STM32_PERIPH_ESP_BIGNUM_ESP_H
