// =============================================================================
// ds_esp.h — El periférico de firma digital RSA (RSA_DS) de los ESP32-C3
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §22; ds_ll.h y hwcrypto_reg.h de
// ESP-IDF; gen_digital_signature_tests.py de ESP-IDF, que dice cómo se
// construye el texto cifrado C]. Z = X^Y mod M con una clave privada que el
// software solo tiene cifrada:
//
//   Y_MEM 0x000 · M_MEM 0x200 · RB_MEM 0x400 · BOX_MEM 0x600 (12 palabras) ·
//   IV 0x630-0x63C · X_MEM 0x800 · Z_MEM 0xA00 (96 palabras cada una) ·
//   SET_START 0xE00 · SET_ME 0xE04 · SET_FINISH 0xE08 · QUERY_BUSY 0xE0C ·
//   QUERY_KEY_WRONG 0xE10 · QUERY_CHECK 0xE14 · DATE 0xE20
//
// QUERY_CHECK y DATE, donde los pone ESP-IDF: el resumen de registros del TRM
// dice 0x814 y 0x820, que caen dentro de X_MEM.
//
// LA CLAVE: al SET_START, el DS pide al HMAC la RSA_DS_KEY, que es la de
// bajada con el propósito 7 (o 5): HMAC-SHA-256(clave del eFuse, 32 x 0xFF).
// Si el HMAC no la tiene, QUERY_BUSY se queda a 1 y QUERY_KEY_WRONG a 0 («no
// se ha llamado al HMAC»), hasta SET_FINISH.
//
// EL CÁLCULO (SET_ME, §22.3.4): P = AES-256-CBC-DEC(C, RSA_DS_KEY, IV), con
// C = Y_MEM || M_MEM || RB_MEM || BOX_MEM (1200 bytes en el orden de la
// memoria) y P = Y || M || r || MD || M' || L || β, todos en little endian.
// Comprueba MD = SHA-256(Y || M || r || M' || L || IV) (bit 0 de QUERY_CHECK
// si no casa: no calcula nada) y β = 8 x 0x08 (bit 1: avisa, pero calcula).
// Con n = L + 1 palabras, Z = X^Y mod M por Montgomery con la r y la M' de
// dentro (bignum_esp.h), con tiempo constante, como el RSA.
//
// EL TIEMPO [de modelo]: el de los productos de Montgomery del RSA (n(n+6)
// ciclos del APB cada uno), más 16 ciclos por bloque de AES y 64 por bloque
// de SHA-256. La clave: 16 ciclos. SET_FINISH borra memorias y registros.
// =============================================================================
#ifndef STM32_PERIPH_ESP_DS_ESP_H
#define STM32_PERIPH_ESP_DS_ESP_H

#include <cstdint>
#include <cstring>
#include <functional>
#include "../../common/periph_base.h"
#include "../hash_algo.h"
#include "../cryp_algo.h"
#include "bignum_esp.h"

namespace stm32 {

class DsEsp : public BusSlave {
public:
    double hz_apb = 80e6;
    // La RSA_DS_KEY del HMAC: false si no la hay
    std::function<bool(uint8_t*)> clave;

    explicit DsEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() {
        borra();
        n_firmas_ = 0;
    }
    unsigned firmas() const { return n_firmas_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off >= 0xA00 && off < 0xA00 + 4 * 96) return hecho() ? z_[(off - 0xA00) / 4] : 0u;
        switch (off) {
        case 0xE0C: return ocupado() ? 1u : 0u;
        case 0xE10: return 0;                     // no se ha llamado al HMAC (o lo ha hecho bien)
        case 0xE14: return hecho() ? check_ : 0u;
        case 0xE20: return date_;
        default: return 0;                        // las memorias de entrada son de solo escritura
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t m) override {
        auto pon = [&](uint32_t& w) {
            for (unsigned k = 0; k < 4; ++k)
                if (m & (1u << k)) w = (w & ~(0xFFu << (8 * k))) | (v & (0xFFu << (8 * k)));
        };
        if (off < 0x180) { pon(c_[off / 4]); return; }
        if (off >= 0x200 && off < 0x380) { pon(c_[96 + (off - 0x200) / 4]); return; }
        if (off >= 0x400 && off < 0x580) { pon(c_[192 + (off - 0x400) / 4]); return; }
        if (off >= 0x600 && off < 0x630) { pon(c_[288 + (off - 0x600) / 4]); return; }
        if (off >= 0x630 && off < 0x640) { pon(iv_[(off - 0x630) / 4]); return; }
        if (off >= 0x800 && off < 0x980) { pon(x_[(off - 0x800) / 4]); return; }
        switch (off) {
        case 0xE00: if (v & 1u) arranca(); return;
        case 0xE04: if (v & 1u) firma(); return;
        case 0xE08: if (v & 1u) borra(); return;
        case 0xE20: date_ = v; return;
        default: return;
        }
    }

private:
    enum class Estado { Reposo, SinClave, Listo, Firmando };
    Estado estado_ = Estado::Reposo;
    uint32_t c_[300], iv_[4], x_[96], z_[96];
    uint8_t k_[32];
    uint32_t check_ = 0, date_ = 0x20200618u;
    unsigned n_firmas_ = 0;
    sc_core::sc_time fin_;

    bool ocupado() const { return estado_ == Estado::SinClave || sc_core::sc_time_stamp() < fin_; }
    bool hecho() const { return estado_ == Estado::Firmando && !ocupado(); }
    void cuesta(double ciclos) {
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time(ciclos / hz_apb, sc_core::SC_SEC);
    }
    void borra() {
        std::memset(c_, 0, sizeof c_);
        std::memset(iv_, 0, sizeof iv_);
        std::memset(x_, 0, sizeof x_);
        std::memset(z_, 0, sizeof z_);
        std::memset(k_, 0, sizeof k_);
        check_ = 0;
        estado_ = Estado::Reposo;
        fin_ = sc_core::SC_ZERO_TIME;
    }
    void arranca() {
        if (clave && clave(k_)) { estado_ = Estado::Listo; cuesta(16); }
        else estado_ = Estado::SinClave;
    }
    static uint32_t le32(const uint8_t* p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    void firma() {
        if (estado_ != Estado::Listo || ocupado()) {
            SC_REPORT_WARNING("ds", "SET_ME sin la clave del HMAC lista: se ignora");
            return;
        }
        // Descifrar: AES-256-CBC
        uint8_t c[1200], p[1200], iv[16], prev[16];
        for (unsigned i = 0; i < 1200; ++i) c[i] = uint8_t(c_[i / 4] >> (8 * (i % 4)));
        for (unsigned i = 0; i < 16; ++i) iv[i] = uint8_t(iv_[i / 4] >> (8 * (i % 4)));
        ClaveAes ka;
        expande_aes(k_, 32, ka);
        std::memcpy(prev, iv, 16);
        for (unsigned b = 0; b < 75; ++b) {
            descifra_aes(ka, c + 16 * b, p + 16 * b);
            for (unsigned i = 0; i < 16; ++i) p[16 * b + i] ^= prev[i];
            std::memcpy(prev, c + 16 * b, 16);
        }
        // Comprobar: MD y el relleno
        const uint8_t* box = p + 1152;                    // MD (32) | M' | L | β (8)
        uint8_t md_in[1152 + 8 + 16], md[32];
        std::memcpy(md_in, p, 1152);
        std::memcpy(md_in + 1152, box + 32, 8);
        std::memcpy(md_in + 1160, iv, 16);
        sha256(md_in, sizeof md_in, md);
        const bool md_mal = std::memcmp(md, box, 32) != 0;
        bool relleno_mal = false;
        for (unsigned i = 0; i < 8; ++i) relleno_mal |= box[40 + i] != 0x08;
        check_ = (md_mal ? 1u : 0u) | (relleno_mal ? 2u : 0u);
        double ciclos = 75 * 16 + 19 * 64;
        std::memset(z_, 0, sizeof z_);
        const uint32_t mp = le32(box + 32), L = le32(box + 36);
        if (!md_mal && L < 96) {
            const unsigned n = L + 1;
            bignum::Num y(n), m(n), r(n), x(n), z, xm;
            for (unsigned i = 0; i < n; ++i) {
                y[i] = le32(p + 4 * i);
                m[i] = le32(p + 384 + 4 * i);
                r[i] = le32(p + 768 + 4 * i);
                x[i] = x_[i];
            }
            const unsigned k = bignum::modexp(x, y, m, r, mp, n, int(32 * n) - 1, true, z, xm);
            for (unsigned i = 0; i < n; ++i) z_[i] = z[i];
            ciclos += double(k) * n * (n + 6);
            ++n_firmas_;
        }
        estado_ = Estado::Firmando;
        cuesta(ciclos);
    }
    static void sha256(const uint8_t* d, size_t len, uint8_t out[32]) {
        uint32_t h[8];
        uint8_t b[64];
        estado_inicial(AlgoHash::Sha256, h);
        size_t i = 0;
        for (; i + 64 <= len; i += 64) bloque_hash(AlgoHash::Sha256, h, d + i);
        const size_t r = len - i;
        std::memset(b, 0, 64);
        std::memcpy(b, d + i, r);
        b[r] = 0x80;
        if (r >= 56) { bloque_hash(AlgoHash::Sha256, h, b); std::memset(b, 0, 64); }
        const uint64_t bits = uint64_t(len) * 8;
        for (unsigned k = 0; k < 8; ++k) b[63 - k] = uint8_t(bits >> (8 * k));
        bloque_hash(AlgoHash::Sha256, h, b);
        for (unsigned k = 0; k < 32; ++k) out[k] = uint8_t(h[k / 4] >> (24 - 8 * (k % 4)));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_DS_ESP_H
