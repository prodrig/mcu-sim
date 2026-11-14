// =============================================================================
// hmac_esp.h — El acelerador HMAC de los ESP32-C3
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §19; hwcrypto_reg.h, hmac_ll.h,
// hmac_hal.c y esp_hmac.c de ESP-IDF]. HMAC-SHA-256 (RFC 2104) con una clave
// de 256 bits de un bloque de eFuse (KEY0-5), que la CPU puede no ver
// (RD_DIS):
//
//   SET_START 0x40 · SET_PARA_PURPOSE 0x44 · SET_PARA_KEY 0x48 ·
//   SET_PARA_FINISH 0x4C · SET_MESSAGE_ONE 0x50 · SET_MESSAGE_ING 0x54 ·
//   SET_MESSAGE_END 0x58 · SET_RESULT_FINISH 0x5C · SET_INVALIDATE_JTAG 0x60 ·
//   SET_INVALIDATE_DS 0x64 · QUERY_ERROR 0x68 · QUERY_BUSY 0x6C ·
//   WR_MESSAGE 0x80-0xBC · RD_RESULT 0xC0-0xDC · SET_MESSAGE_PAD 0xF0 ·
//   ONE_BLOCK 0xF4 · SOFT_JTAG_CTRL 0xF8 · WR_JTAG 0xFC
//
// EL PROPÓSITO (tabla 19.2-1): 8 HMAC_UP (el mensaje lo da el usuario y el
// resultado se lee), 7 DOWN_DIGITAL_SIGNATURE (la clave del DS: HMAC sobre
// 32 bytes 0xFF), 6 DOWN_JTAG (el JTAG: HMAC sobre 32 bytes 0x00) y 5
// DOWN_ALL, que vale para los dos de abajo. La clave elegida tiene que tener
// en su KEY_PURPOSE el mismo valor (o 5, para 6 y 7); si no, QUERY_ERROR = 1
// y no se calcula nada.
//
// EL MENSAJE (subida, §19.2.5): el hardware hace SHA-256 de (K0 ^ ipad)
// seguido de los bloques de 512 bits que escribe el software en WR_MESSAGE,
// cada uno con SET_MESSAGE_ONE. Después: ING (viene otro), END (era el
// último y el relleno lo pone el hardware: la longitud cuenta los 512 bits
// de K0 ^ ipad), PAD (el siguiente es el último, rellenado por el software)
// u ONE_BLOCK (el que acaba de entrar era el último, ya rellenado). Es la
// secuencia de esp_hmac_calculate. El externo, SHA-256(K0 ^ opad || H1),
// sale en RD_RESULT en el orden de bytes del resumen; SET_RESULT_FINISH lo
// borra.
//
// EL JTAG (§19.2.4): con SOFT_JTAG_CTRL, las ocho escrituras de WR_JTAG se
// comparan con el resultado de JTAG, de la última palabra a la primera y
// cada una con sus bytes al revés (lo que hace ets_jtag_enable_temporarily
// de la ROM). Si coinciden, `jtag` avisa (el SoC vuelve a abrir los pines que
// SOFT_DIS_JTAG había cerrado) hasta SET_INVALIDATE_JTAG o el reset.
//
// EL TIEMPO [de modelo]: 64 ciclos del APB por bloque de SHA-256, como el
// SHA. QUERY_BUSY vale 1 ese tiempo.
//
// LO QUE NO SE MODELA: el cálculo automático del DS al soltar el reset (el TRM
// dice que el HMAC lo hace si hay una clave de DS; ESP-IDF lo pide siempre),
// el bloqueo mutuo con el SHA y el DS (el software no debe usarlos a la vez).
// =============================================================================
#ifndef STM32_PERIPH_ESP_HMAC_ESP_H
#define STM32_PERIPH_ESP_HMAC_ESP_H

#include <cstdint>
#include <cstring>
#include <functional>
#include "../../common/periph_base.h"
#include "../hash_algo.h"

namespace stm32 {

class HmacEsp : public BusSlave {
public:
    double hz_apb = 80e6;
    // De los eFuse: la clave n (32 bytes) y su propósito
    std::function<void(unsigned, uint8_t*)> clave;
    std::function<unsigned(unsigned)> proposito;
    // El JTAG reabierto (true) o cerrado otra vez (false)
    std::function<void(bool)> jtag;
    enum : unsigned { P_TODO = 5, P_JTAG = 6, P_DS = 7, P_SUBIDA = 8 };

    explicit HmacEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() {
        std::memset(msg_, 0, sizeof msg_);
        std::memset(res_, 0, sizeof res_);
        proposito_ = clave_n_ = 0;
        error_ = pad_ = compara_ = false;
        estado_ = Estado::Reposo;
        hay_ds_ = hay_jtag_ = false;
        pon_jtag(false);
        fin_ = sc_core::SC_ZERO_TIME;
    }
    // La clave del DS (el resultado de bajada con el propósito 7 o 5): false si no la hay
    bool clave_ds(uint8_t k[32]) const {
        if (!hay_ds_ || ocupado()) return false;
        std::memcpy(k, ds_, 32);
        return true;
    }
    bool jtag_abierto() const { return jtag_ok_; }
    bool ocupado() const { return sc_core::sc_time_stamp() < fin_; }
    unsigned calculos() const { return n_calc_; }
    unsigned aperturas() const { return n_abre_; }

    // HMAC-SHA-256 con una clave de 32 bytes (K0 = clave || 0^256) sobre un
    // mensaje de 32 bytes: el de bajada. Público para las pruebas.
    static void hmac32(const uint8_t k[32], const uint8_t m[32], uint8_t out[32]) {
        uint32_t h[8];
        uint8_t b[64];
        estado_inicial(AlgoHash::Sha256, h);
        for (unsigned i = 0; i < 64; ++i) b[i] = uint8_t((i < 32 ? k[i] : 0) ^ 0x36);
        bloque_hash(AlgoHash::Sha256, h, b);
        std::memset(b, 0, 64);
        std::memcpy(b, m, 32);
        b[32] = 0x80;
        b[62] = 0x03;                                   // 768 bits
        bloque_hash(AlgoHash::Sha256, h, b);
        externo(k, h, out);
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == 0x68) return error_ ? 1u : 0u;
        if (off == 0x6C) return ocupado() ? 1u : 0u;
        if (off >= 0xC0 && off < 0xE0) {
            if (estado_ != Estado::Hecho) return 0;
            const unsigned i = off - 0xC0;
            return uint32_t(res_[i]) | uint32_t(res_[i + 1]) << 8 | uint32_t(res_[i + 2]) << 16 | uint32_t(res_[i + 3]) << 24;
        }
        if (off == 0xF8) return 0x20200618u;            // DATE (comparte dirección con SOFT_JTAG_CTRL)
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t m) override {
        if (off >= 0x80 && off < 0xC0) {
            const unsigned i = off - 0x80;
            for (unsigned k = 0; k < 4; ++k) if (m & (1u << k)) msg_[i + k] = uint8_t(v >> (8 * k));
            return;
        }
        switch (off) {
        case 0x40: if (v & 1u) { estado_ = Estado::Configura; error_ = false; } return;
        case 0x44: proposito_ = v & 0xFu; return;
        case 0x48: clave_n_ = v & 7u; return;
        case 0x4C: if (v & 1u) configura(); return;
        case 0x50: if (v & 1u) mensaje(); return;
        case 0x54: return;                                       // ING: viene otro bloque
        case 0x58: if (v & 1u) relleno_hw(); return;
        case 0x5C:                                                // RESULT_FINISH
            std::memset(res_, 0, sizeof res_);
            estado_ = Estado::Reposo;
            error_ = false;
            return;
        case 0x60: if (v & 1u) { hay_jtag_ = false; pon_jtag(false); } return;
        case 0x64: if (v & 1u) hay_ds_ = false; return;
        case 0xF0: if (v & 1u) pad_ = true; return;
        case 0xF4: if ((v & 1u) && estado_ == Estado::Mensaje) termina(); return;
        case 0xF8: if (v & 1u) { compara_ = true; n_jtag_ = 0; } return;
        case 0xFC:
            if (!compara_) return;
            jtag_w_[n_jtag_++] = v;
            if (n_jtag_ == 8) { compara_ = false; compara_jtag(); }
            return;
        default: return;
        }
    }

private:
    enum class Estado { Reposo, Configura, Mensaje, Hecho };
    Estado estado_ = Estado::Reposo;
    uint8_t msg_[64], res_[32], k_[32], ds_[32], jt_[32];
    uint32_t h_[8], jtag_w_[8];
    unsigned proposito_ = 0, clave_n_ = 0, bloques_ = 0, n_jtag_ = 0, n_calc_ = 0, n_abre_ = 0;
    bool error_ = false, pad_ = false, hay_ds_ = false, hay_jtag_ = false, jtag_ok_ = false, compara_ = false;
    sc_core::sc_time fin_;

    void cuesta(unsigned bloques) {
        const sc_core::sc_time base = ocupado() ? fin_ : sc_core::sc_time_stamp();
        fin_ = base + sc_core::sc_time(64.0 * bloques / hz_apb, sc_core::SC_SEC);
    }
    void pon_jtag(bool v) {
        if (v == jtag_ok_) return;
        jtag_ok_ = v;
        if (v) ++n_abre_;
        if (jtag) jtag(v);
    }
    static void externo(const uint8_t k[32], const uint32_t h1[8], uint8_t out[32]) {
        uint32_t h[8];
        uint8_t b[64];
        estado_inicial(AlgoHash::Sha256, h);
        for (unsigned i = 0; i < 64; ++i) b[i] = uint8_t((i < 32 ? k[i] : 0) ^ 0x5C);
        bloque_hash(AlgoHash::Sha256, h, b);
        std::memset(b, 0, 64);
        for (unsigned i = 0; i < 32; ++i) b[i] = uint8_t(h1[i / 4] >> (24 - 8 * (i % 4)));
        b[32] = 0x80;
        b[62] = 0x03;                                   // 768 bits
        bloque_hash(AlgoHash::Sha256, h, b);
        for (unsigned i = 0; i < 32; ++i) out[i] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
    }
    void configura() {
        if (estado_ != Estado::Configura) {
            SC_REPORT_WARNING("hmac", "SET_PARA_FINISH sin SET_START: se ignora");
            return;
        }
        const unsigned kp = (proposito && clave_n_ < 6) ? proposito(clave_n_) : 0u;
        const bool vale = clave_n_ < 6 &&
            (kp == proposito_ ? (proposito_ >= P_TODO && proposito_ <= P_SUBIDA)
                              : (kp == P_TODO && (proposito_ == P_JTAG || proposito_ == P_DS)));
        if (!vale) { error_ = true; estado_ = Estado::Reposo; return; }
        if (clave) clave(clave_n_, k_); else std::memset(k_, 0, 32);
        ++n_calc_;
        if (proposito_ == P_SUBIDA) {
            uint8_t b[64];
            estado_inicial(AlgoHash::Sha256, h_);
            for (unsigned i = 0; i < 64; ++i) b[i] = uint8_t((i < 32 ? k_[i] : 0) ^ 0x36);
            bloque_hash(AlgoHash::Sha256, h_, b);
            bloques_ = 1;
            pad_ = false;
            estado_ = Estado::Mensaje;
            cuesta(1);
            return;
        }
        // Bajada: la clave del DS (0xFF) y/o el resultado del JTAG (0x00)
        uint8_t m[32];
        if (proposito_ == P_DS || proposito_ == P_TODO) {
            std::memset(m, 0xFF, 32);
            hmac32(k_, m, ds_);
            hay_ds_ = true;
            cuesta(4);
        }
        if (proposito_ == P_JTAG || proposito_ == P_TODO) {
            std::memset(m, 0x00, 32);
            hmac32(k_, m, jt_);
            hay_jtag_ = true;
            cuesta(4);
        }
        estado_ = Estado::Reposo;
    }
    void mensaje() {
        if (estado_ != Estado::Mensaje) {
            SC_REPORT_WARNING("hmac", "SET_MESSAGE_ONE fuera de un calculo de subida: se ignora");
            return;
        }
        bloque_hash(AlgoHash::Sha256, h_, msg_);
        ++bloques_;
        cuesta(1);
        if (pad_) termina();
    }
    void relleno_hw() {
        if (estado_ != Estado::Mensaje) return;
        uint8_t b[64] = {};
        b[0] = 0x80;
        const uint64_t bits = uint64_t(bloques_) * 512u;
        for (unsigned i = 0; i < 8; ++i) b[63 - i] = uint8_t(bits >> (8 * i));
        bloque_hash(AlgoHash::Sha256, h_, b);
        cuesta(1);
        termina();
    }
    void termina() {
        externo(k_, h_, res_);
        estado_ = Estado::Hecho;
        pad_ = false;
        cuesta(2);
    }
    void compara_jtag() {
        bool ok = hay_jtag_;
        for (unsigned k = 0; k < 8 && ok; ++k) {
            const unsigned j = 28 - 4 * k;
            const uint32_t e = uint32_t(jt_[j]) << 24 | uint32_t(jt_[j + 1]) << 16 | uint32_t(jt_[j + 2]) << 8 | jt_[j + 3];
            ok = jtag_w_[k] == e;
        }
        pon_jtag(ok);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_HMAC_ESP_H
