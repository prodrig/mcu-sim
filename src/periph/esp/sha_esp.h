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
// BUSY sigue a 1 ese tiempo y el resultado ya está en H_MEM.
//
// E9: EL MODO DMA [TRM-C3 §19.4, TRM-C6 §22.4; sha_hal.c y esp_sha_dma de
// ESP-IDF, que lo usa para todo lo que no sea un bloque suelto]:
// DMA_BLOCK_NUM (0x0C, 6 bits), DMA_START (0x1C, desde el estado inicial) y
// DMA_CONTINUE (0x20, desde H_MEM) sacan esos bloques de 64 bytes del canal
// de salida de la GDMA que sirve al SHA (periférico 7) y los procesan; BUSY
// dura lo que todos. CLEAR_IRQ (0x24) e IRQ_ENA (0x28): la interrupción al
// acabar. Lo que no se modela: SHA-512/t (ni el C3 ni el C6 lo tienen).
// =============================================================================
#ifndef STM32_PERIPH_ESP_SHA_ESP_H
#define STM32_PERIPH_ESP_SHA_ESP_H

#include <cstdint>
#include "../../common/periph_base.h"
#include "../hash_algo.h"
#include "gdma_esp.h"
#include <functional>

namespace stm32 {

class ShaEsp : public BusSlave {
public:
    double hz_apb = 20e6;
    GdmaEsp* gdma = nullptr;                   // el modo DMA
    std::function<void(bool)> irq;
    static constexpr unsigned PERI_GDMA = 7;
    explicit ShaEsp(sc_core::sc_module_name nm, uint32_t base, uint32_t date = 0x20190402u)
        : BusSlave(nm, base, 0x1000), date_(date) {
        SC_HAS_PROCESS(ShaEsp);
        SC_METHOD(fin_proc);
        sensitive << ev_fin_;
        dont_initialize();
        reset();
    }
    void reset() {
        modo_ = 0;
        for (uint32_t& w : h_) w = 0;
        for (uint32_t& w : m_) w = 0;
        fin_ = sc_core::SC_ZERO_TIME;
        bloques_ = 0;
        nblq_ = ena_ = 0; int_ = false;
        evalua();
    }
    unsigned bloques_dma() const { return bloques_dma_; }
    unsigned bloques() const { return bloques_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == 0x00) return modo_;
        if (off == 0x18) return sc_core::sc_time_stamp() < fin_ ? 1u : 0u;
        if (off == 0x2C) return date_;
        if (off == 0x0C) return nblq_;
        if (off == 0x28) return ena_;
        if (off >= 0x40 && off < 0x60) return bswap(h_[(off - 0x40) / 4]);
        if (off >= 0x80 && off < 0xC0) return m_[(off - 0x80) / 4];
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x00: modo_ = v & 7u; return;
        case 0x10: bloque(true); return;
        case 0x14: bloque(false); return;
        case 0x0C: nblq_ = v & 0x3Fu; return;
        case 0x1C: case 0x20: por_dma(off == 0x1C); return;
        case 0x24: if (v & 1u) { int_ = false; evalua(); } return;
        case 0x28: ena_ = v & 1u; evalua(); return;
        default:
            if (off >= 0x40 && off < 0x60) h_[(off - 0x40) / 4] = bswap(v);
            else if (off >= 0x80 && off < 0xC0) m_[(off - 0x80) / 4] = v;
            return;
        }
    }

private:
    uint32_t date_, modo_ = 0, h_[8], m_[16], nblq_ = 0, ena_ = 0;
    sc_core::sc_time fin_;
    unsigned bloques_ = 0, bloques_dma_ = 0;
    bool int_ = false, irq_ = false;
    sc_core::sc_event ev_fin_;
    void evalua() {
        const bool n = int_ && (ena_ & 1u);
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    void fin_proc() { int_ = true; evalua(); }
    void por_dma(bool inicio) {
        if (modo_ > 2) { SC_REPORT_WARNING("sha", "modo de SHA que el C3 y el C6 no tienen"); return; }
        if (!gdma) { SC_REPORT_WARNING("sha", "el modo DMA sin GDMA"); return; }
        const AlgoHash a = modo_ == 0 ? AlgoHash::Sha1 : modo_ == 1 ? AlgoHash::Sha224 : AlgoHash::Sha256;
        if (inicio) estado_inicial(a, h_);
        unsigned n = 0;
        for (unsigned k = 0; k < nblq_; ++k) {
            uint8_t b[64];
            if (gdma->saca(PERI_GDMA, b, 64) < 64) { SC_REPORT_WARNING("sha", "la GDMA no tiene los bloques que dice DMA_BLOCK_NUM"); break; }
            bloque_hash(a, h_, b);
            ++n;
        }
        bloques_ += n;
        bloques_dma_ += n;
        const sc_core::sc_time t((modo_ == 0 ? 80.0 : 64.0) * std::max(1u, n) / hz_apb, sc_core::SC_SEC);
        fin_ = sc_core::sc_time_stamp() + t;
        ev_fin_.notify(t);
    }
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
