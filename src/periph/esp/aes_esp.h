// =============================================================================
// aes_esp.h — El acelerador AES de los ESP32-C3 y C6 (modo típico y modo DMA)
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §18; hwcrypto_reg.h de ESP-IDF]:
// KEY_0-7 (0x00-0x1C), TEXT_IN_0-3 (0x20), TEXT_OUT_0-3 (0x30), MODE (0x40:
// 0 cifrar AES-128, 2 cifrar AES-256, 4 descifrar AES-128, 6 descifrar
// AES-256), TRIGGER (0x48) y STATE (0x4C: 1 mientras trabaja, 0 en reposo).
// Las palabras van en el orden de bytes de la memoria (byte 0 en el bit 0),
// como las escribe aes_hal. Un bloque tarda 11 ciclos del APB (128) o 15
// (256) [de modelo]. El algoritmo es el de periph/cryp_algo.h (el del CRYP de
// los STM32), comprobado con los vectores de FIPS-197.
//
// E9: EL MODO DMA [TRM-C3 §18.4, TRM-C6 §21.4; aes_hal.c y esp_aes.c de
// ESP-IDF, que lo usan siempre, hasta para un bloque]: DMA_ENABLE (0x90),
// BLOCK_MODE (0x94: 0 ECB, 1 CBC, 2 OFB, 3 CTR, 4 CFB8, 5 CFB128), BLOCK_NUM
// (0x98), INC_SEL (0x9C: el contador de CTR, 0 de 32 bits, 1 de 128),
// IV_MEM (0x50-0x5F), INT_CLEAR (0xAC), INT_ENA (0xB0), DATE (0xB4) y
// DMA_EXIT (0xB8). Con DMA_ENABLE, TRIGGER saca BLOCK_NUM·16 bytes del canal
// de salida de la GDMA que sirve al AES (periférico 6), los cifra o descifra
// en ese modo y los mete en su canal de entrada, con EOF al final; IV_MEM
// queda como lo deja el silicio para seguir (el último texto cifrado en CBC
// y CFB, la última salida del bloque en OFB, el contador siguiente en CTR).
// STATE pasa a 1 y, al acabar, a 2 (DONE) y la interrupción si INT_ENA,
// hasta DMA_EXIT, que lo vuelve a 0. Antes, DATE se leía en 0xAC: estaba
// mal (es INT_CLEAR); ahora, en 0xB4.
// LO QUE NO SE MODELA: AES-192 (ni el C3 ni el C6 lo tienen), GCM (tampoco) y
// REMAINDER_BIT_NUM (ESP-IDF rellena el último bloque).
// =============================================================================
#ifndef STM32_PERIPH_ESP_AES_ESP_H
#define STM32_PERIPH_ESP_AES_ESP_H

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../cryp_algo.h"
#include "gdma_esp.h"

namespace stm32 {

class AesEsp : public BusSlave {
public:
    double hz_apb = 80e6;
    GdmaEsp* gdma = nullptr;                   // el modo DMA
    std::function<void(bool)> irq;
    static constexpr unsigned PERI_GDMA = 6;
    uint32_t date = 0x20191210u;
    explicit AesEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(AesEsp);
        SC_METHOD(fin_proc);
        sensitive << ev_fin_;
        dont_initialize();
        reset();
    }
    void reset() {
        for (auto& w : k_) w = 0;
        for (auto& w : in_) w = 0;
        for (auto& w : out_) w = 0;
        for (auto& w : iv_) w = 0;
        modo_ = dma_ = bmodo_ = bnum_ = inc_ = ena_ = 0;
        bloques_ = 0; estado_ = 0; int_ = false;
        evalua();
    }
    unsigned bloques() const { return bloques_; }
    unsigned bloques_dma() const { return bloques_dma_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x20) return k_[off / 4];
        if (off < 0x30) return in_[(off - 0x20) / 4];
        if (off < 0x40) return out_[(off - 0x30) / 4];
        if (off >= 0x50 && off < 0x60) return iv_[(off - 0x50) / 4];
        switch (off) {
        case 0x40: return modo_;
        case 0x4C:
            if (dma_) return estado_;
            return sc_core::sc_time_stamp() < fin_ ? 1u : 0u;
        case 0x90: return dma_;
        case 0x94: return bmodo_;
        case 0x98: return bnum_;
        case 0x9C: return inc_;
        case 0xB0: return ena_;
        case 0xB4: return date;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off < 0x20) { k_[off / 4] = v; return; }
        if (off < 0x30) { in_[(off - 0x20) / 4] = v; return; }
        if (off >= 0x50 && off < 0x60) { iv_[(off - 0x50) / 4] = v; return; }
        switch (off) {
        case 0x40: modo_ = v & 7u; return;
        case 0x48: if (v & 1u) { if (dma_) bloques_por_dma(); else bloque(); } return;
        case 0x90: dma_ = v & 1u; return;
        case 0x94: bmodo_ = v & 7u; return;
        case 0x98: bnum_ = v; return;
        case 0x9C: inc_ = v & 1u; return;
        case 0xAC: if (v & 1u) { int_ = false; evalua(); } return;
        case 0xB0: ena_ = v & 1u; evalua(); return;
        case 0xB4: date = v & 0x3FFFFFFFu; return;
        case 0xB8: if (v & 1u) estado_ = 0; return;
        default: return;
        }
    }

private:
    uint32_t k_[8] = {}, in_[4] = {}, out_[4] = {}, iv_[4] = {};
    uint32_t modo_ = 0, dma_ = 0, bmodo_ = 0, bnum_ = 0, inc_ = 0, ena_ = 0, estado_ = 0;
    unsigned bloques_ = 0, bloques_dma_ = 0;
    bool int_ = false, irq_ = false;
    sc_core::sc_time fin_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_event ev_fin_;
    static void a_bytes(const uint32_t* w, unsigned n, uint8_t* b) {
        for (unsigned i = 0; i < n; ++i) b[i] = uint8_t(w[i / 4] >> (8 * (i % 4)));
    }
    static void a_palabras(const uint8_t* b, uint32_t* w, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            w[i] = uint32_t(b[4 * i]) | uint32_t(b[4 * i + 1]) << 8 | uint32_t(b[4 * i + 2]) << 16 | uint32_t(b[4 * i + 3]) << 24;
    }
    void evalua() {
        const bool n = int_ && (ena_ & 1u);
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    void fin_proc() {
        estado_ = 2;                           // DONE
        int_ = true;
        evalua();
    }
    bool clave(ClaveAes& k, unsigned& nk) const {
        if (modo_ == 1 || modo_ == 5 || modo_ > 6) return false;
        nk = (modo_ & 2u) ? 32u : 16u;
        uint8_t kb[32];
        a_bytes(k_, nk, kb);
        expande_aes(kb, nk, k);
        return true;
    }
    void bloque() {
        ClaveAes k;
        unsigned nk = 0;
        if (!clave(k, nk)) { SC_REPORT_WARNING("aes", "AES-192: ni el C3 ni el C6 lo tienen"); return; }
        uint8_t ib[16], ob[16];
        a_bytes(in_, 16, ib);
        if (modo_ & 4u) descifra_aes(k, ib, ob); else cifra_aes(k, ib, ob);
        a_palabras(ob, out_, 4);
        ++bloques_;
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time((nk == 32 ? 15.0 : 11.0) / hz_apb, sc_core::SC_SEC);
    }
    // El modo DMA: BLOCK_NUM bloques de la GDMA a la GDMA
    void bloques_por_dma() {
        ClaveAes k;
        unsigned nk = 0;
        if (!clave(k, nk)) { SC_REPORT_WARNING("aes", "AES-192: ni el C3 ni el C6 lo tienen"); return; }
        if (!gdma) { SC_REPORT_WARNING("aes", "el modo DMA sin GDMA"); return; }
        const bool des = (modo_ & 4u) != 0;
        uint8_t iv[16];
        a_bytes(iv_, 16, iv);
        std::vector<uint8_t> sal(size_t(bnum_) * 16);
        unsigned hechos = 0;
        for (unsigned b = 0; b < bnum_; ++b) {
            uint8_t x[16], y[16], t[16];
            if (gdma->saca(PERI_GDMA, x, 16) < 16) { SC_REPORT_WARNING("aes", "la GDMA no tiene los bloques que dice BLOCK_NUM"); break; }
            switch (bmodo_) {
            case 0:                                            // ECB
                if (des) descifra_aes(k, x, y); else cifra_aes(k, x, y);
                break;
            case 1:                                            // CBC
                if (des) { descifra_aes(k, x, t); for (unsigned i = 0; i < 16; ++i) y[i] = t[i] ^ iv[i]; std::memcpy(iv, x, 16); }
                else { for (unsigned i = 0; i < 16; ++i) t[i] = x[i] ^ iv[i]; cifra_aes(k, t, y); std::memcpy(iv, y, 16); }
                break;
            case 2:                                            // OFB
                cifra_aes(k, iv, t); std::memcpy(iv, t, 16);
                for (unsigned i = 0; i < 16; ++i) y[i] = x[i] ^ t[i];
                break;
            case 3:                                            // CTR
                cifra_aes(k, iv, t);
                for (unsigned i = 0; i < 16; ++i) y[i] = x[i] ^ t[i];
                for (int i = 15; i >= (inc_ ? 0 : 12); --i) if (++iv[i]) break;
                break;
            case 4:                                            // CFB8: 16 pasos de un byte
                for (unsigned i = 0; i < 16; ++i) {
                    cifra_aes(k, iv, t);
                    y[i] = x[i] ^ t[0];
                    std::memmove(iv, iv + 1, 15);
                    iv[15] = des ? x[i] : y[i];
                }
                break;
            case 5:                                            // CFB128
                cifra_aes(k, iv, t);
                for (unsigned i = 0; i < 16; ++i) y[i] = x[i] ^ t[i];
                std::memcpy(iv, des ? x : y, 16);
                break;
            default:
                SC_REPORT_WARNING("aes", "BLOCK_MODE que el C3 y el C6 no tienen (GCM)");
                std::memcpy(y, x, 16);
                break;
            }
            std::memcpy(&sal[16 * b], y, 16);
            ++hechos;
        }
        a_palabras(iv, iv_, 4);
        if (hechos) gdma->mete(PERI_GDMA, sal.data(), 16 * hechos, true);
        bloques_ += hechos;
        bloques_dma_ += hechos;
        estado_ = 1;
        ev_fin_.notify(sc_core::sc_time((nk == 32 ? 15.0 : 11.0) * std::max(1u, hechos) / hz_apb, sc_core::SC_SEC));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_AES_ESP_H
