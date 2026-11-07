// =============================================================================
// sha_esp.h — El acelerador SHA de los ESP32 (C3: SHA-1, SHA-224, SHA-256)
//
// E4 de doc/analisis_esp32_riscv.md. Lo usan la ROM, para comprobar el
// SHA-256 que esptool añade a la imagen del bootloader, y el bootloader, para
// el de la aplicación [TRM-C3 §19, sha_reg.h de ESP-IDF]:
//
//   MODE (0x00)        0 SHA-1, 1 SHA-224, 2 SHA-256
//   START (0x10)       procesa el bloque de M_MEM desde el estado inicial
//   CONTINUE (0x14)    procesa el bloque desde el estado de H_MEM
//   BUSY (0x18)        1 mientras dura el bloque
//   H_MEM (0x40-0x5F)  el estado, en el orden de bytes del resumen: leído byte
//                      a byte desde la primera palabra es el resumen tal cual
//   M_MEM (0x80-0xBF)  el bloque de 64 bytes, en el orden de la memoria
//
// El relleno lo hace el software, como en el silicio. El bloque tarda 80
// ciclos del APB (SHA-1) o 64 (SHA-224/256) [de modelo: el TRM no lo dice]:
// BUSY sigue a 1 ese tiempo y el resultado ya está en H_MEM. Lo que no se
// modela: el modo DMA (DMA_START/DMA_CONTINUE: necesita la GDMA, E8; avisa y
// no hace nada), SHA-512/t (no está en el C3) y la interrupción.
// =============================================================================
#ifndef STM32_PERIPH_ESP_SHA_ESP_H
#define STM32_PERIPH_ESP_SHA_ESP_H

#include <cstdint>
#include "../../common/periph_base.h"
#include "../hash_algo.h"

namespace stm32 {

class ShaEsp : public BusSlave {
public:
    double hz_apb = 20e6;
    explicit ShaEsp(sc_core::sc_module_name nm, uint32_t base, uint32_t date = 0x20190402u)
        : BusSlave(nm, base, 0x1000), date_(date) { reset(); }
    void reset() {
        modo_ = 0;
        for (uint32_t& w : h_) w = 0;
        for (uint32_t& w : m_) w = 0;
        fin_ = sc_core::SC_ZERO_TIME;
        bloques_ = 0;
    }
    unsigned bloques() const { return bloques_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == 0x00) return modo_;
        if (off == 0x18) return sc_core::sc_time_stamp() < fin_ ? 1u : 0u;
        if (off == 0x2C) return date_;
        if (off >= 0x40 && off < 0x60) return bswap(h_[(off - 0x40) / 4]);
        if (off >= 0x80 && off < 0xC0) return m_[(off - 0x80) / 4];
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x00: modo_ = v & 7u; return;
        case 0x10: bloque(true); return;
        case 0x14: bloque(false); return;
        case 0x1C: case 0x20:
            SC_REPORT_WARNING("sha", "el modo DMA del SHA necesita la GDMA (E8): no hace nada");
            return;
        default:
            if (off >= 0x40 && off < 0x60) h_[(off - 0x40) / 4] = bswap(v);
            else if (off >= 0x80 && off < 0xC0) m_[(off - 0x80) / 4] = v;
            return;
        }
    }

private:
    uint32_t date_, modo_ = 0, h_[8], m_[16];
    sc_core::sc_time fin_;
    unsigned bloques_ = 0;
    static uint32_t bswap(uint32_t v) {
        return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    }
    void bloque(bool inicio) {
        if (modo_ > 2) { SC_REPORT_WARNING("sha", "modo de SHA que el C3 no tiene"); return; }
        const AlgoHash a = modo_ == 0 ? AlgoHash::Sha1 : modo_ == 1 ? AlgoHash::Sha224 : AlgoHash::Sha256;
        if (inicio) estado_inicial(a, h_);
        uint8_t b[64];
        for (unsigned i = 0; i < 64; ++i) b[i] = uint8_t(m_[i / 4] >> (8 * (i & 3)));
        bloque_hash(a, h_, b);
        ++bloques_;
        fin_ = sc_core::sc_time_stamp() + sc_core::sc_time((modo_ == 0 ? 80.0 : 64.0) / hz_apb, sc_core::SC_SEC);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_SHA_ESP_H
