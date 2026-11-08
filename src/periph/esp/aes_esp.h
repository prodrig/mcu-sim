// =============================================================================
// aes_esp.h — El acelerador AES de los ESP32-C3 (modo típico, sin DMA)
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §18; hwcrypto_reg.h de ESP-IDF]:
// KEY_0-7 (0x00-0x1C), TEXT_IN_0-3 (0x20), TEXT_OUT_0-3 (0x30), MODE (0x40:
// 0 cifrar AES-128, 2 cifrar AES-256, 4 descifrar AES-128, 6 descifrar
// AES-256), TRIGGER (0x48) y STATE (0x4C: 1 mientras trabaja, 0 en reposo).
// Las palabras van en el orden de bytes de la memoria (byte 0 en el bit 0),
// como las escribe aes_hal. Un bloque tarda 11 ciclos del APB (128) o 15
// (256) [de modelo]. El algoritmo es el de periph/cryp_algo.h (el del CRYP de
// los STM32), comprobado con los vectores de FIPS-197.
// LO QUE NO SE MODELA: el modo DMA (DMA_ENABLE: avisa y no hace nada; lo usa
// mbedTLS en bloques grandes), AES-192 (el C3 no lo tiene).
// =============================================================================
#ifndef STM32_PERIPH_ESP_AES_ESP_H
#define STM32_PERIPH_ESP_AES_ESP_H

#include <cstdint>
#include "../../common/periph_base.h"
#include "../cryp_algo.h"

namespace stm32 {

class AesEsp : public BusSlave {
public:
    double hz_apb = 80e6;
    explicit AesEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) { reset(); }
    void reset() { for (auto& w : k_) w = 0; for (auto& w : in_) w = 0; for (auto& w : out_) w = 0; modo_ = dma_ = 0; bloques_ = 0; }
    unsigned bloques() const { return bloques_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x20) return k_[off / 4];
        if (off < 0x30) return in_[(off - 0x20) / 4];
        if (off < 0x40) return out_[(off - 0x30) / 4];
        if (off == 0x40) return modo_;
        if (off == 0x4C) return sc_core::sc_time_stamp() < fin_ ? 1u : 0u;
        if (off == 0x90) return dma_;
        if (off == 0xAC) return 0x20191210u;
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off < 0x20) { k_[off / 4] = v; return; }
        if (off < 0x30) { in_[(off - 0x20) / 4] = v; return; }
        if (off == 0x40) { modo_ = v & 7u; return; }
        if (off == 0x48 && (v & 1u)) { bloque(); return; }
        if (off == 0x90) {
            dma_ = v & 1u;
            if (dma_) SC_REPORT_WARNING("aes", "el modo DMA del AES del C3 no se modela (E8): no hace nada");
        }
    }

private:
    uint32_t k_[8] = {}, in_[4] = {}, out_[4] = {}, modo_ = 0, dma_ = 0;
    unsigned bloques_ = 0;
    sc_core::sc_time fin_ = sc_core::SC_ZERO_TIME;
    static void a_bytes(const uint32_t* w, unsigned n, uint8_t* b) {
        for (unsigned i = 0; i < n; ++i) b[i] = uint8_t(w[i / 4] >> (8 * (i % 4)));
    }
    void bloque() {
        if (modo_ == 1 || modo_ == 5 || modo_ > 6) { SC_REPORT_WARNING("aes", "AES-192: el C3 no lo tiene"); return; }
        const unsigned nk = (modo_ & 2u) ? 32u : 16u;
        uint8_t kb[32], ib[16], ob[16];
        a_bytes(k_, nk, kb);
        a_bytes(in_, 16, ib);
        ClaveAes k;
        expande_aes(kb, nk, k);
        if (modo_ & 4u) descifra_aes(k, ib, ob); else cifra_aes(k, ib, ob);
        for (unsigned i = 0; i < 4; ++i)
            out_[i] = uint32_t(ob[4 * i]) | uint32_t(ob[4 * i + 1]) << 8 | uint32_t(ob[4 * i + 2]) << 16 | uint32_t(ob[4 * i + 3]) << 24;
        ++bloques_;
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time((nk == 32 ? 15.0 : 11.0) / hz_apb, sc_core::SC_SEC);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_AES_ESP_H
