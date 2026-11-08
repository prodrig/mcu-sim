// =============================================================================
// spi_gp_esp.h — El GP-SPI2 de los ESP32-C3, como maestro
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §27; spi_reg.h de ESP-IDF]. Lo que
// usa `spi_master` (y un ST7735): la transacción de usuario, con sus fases
//
//   orden (USR_COMMAND: USER2 VALUE con su BITLEN + 1), dirección (USR_ADDR:
//   ADDR con USER1.ADDR_BITLEN + 1), espera (USR_DUMMY, ciclos), datos
//   (USR_MOSI y/o USR_MISO: MS_DLEN + 1 bits, de W0-W15 o, con
//   DMA_TX_ENA, de la GDMA: canal con OUT_PERI_SEL = 0)
//
// la arranca CMD.USR (con UPDATE antes, como manda el TRM: aquí no hace
// falta). El reloj: CLK_EQU_SYSCLK (el APB) o APB / ((CLKDIV_PRE + 1) x
// (CLKCNT_N + 1)). Modos 0 y 3 (CK_IDLE_EDGE; CK_OUT_EDGE, el flanco en que
// cambia MOSI). CS0 (señal 68) baja durante la transacción, salvo CS0_DIS;
// SCLK 63, MOSI (D) 65 y MISO (Q) 64. Al acabar, TRANS_DONE (bit 12 de
// DMA_INT_RAW) a la fuente 19. El bit más alto de cada byte primero; los
// bytes de W0-W15, en el orden de la memoria.
// LO QUE NO SE MODELA: el modo esclavo, dual/quad/QPI, los CS 1-5, la
// transferencia segmentada (SEG_TRANS), la DMA de entrada y los retardos de
// muestreo (DIN_MODE/DOUT_MODE).
// =============================================================================
#ifndef STM32_PERIPH_ESP_SPI_GP_ESP_H
#define STM32_PERIPH_ESP_SPI_GP_ESP_H

#include <systemc>
#include <cstdint>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "gdma_esp.h"

namespace stm32 {

class SpiGpEsp : public BusSlave {
public:
    std::function<void(bool)> irq;
    std::function<void(bool clk, bool mosi, bool cs)> salida;
    std::function<bool()> miso;
    GdmaEsp* gdma = nullptr;
    double hz_apb = 80e6;

    explicit SpiGpEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(SpiGpEsp);
        SC_THREAD(trans);
        reset();
    }
    void reset() {
        for (auto& r : r_) r = 0;
        r_[0x0C / 4] = 0x80003043u;     // CLOCK de reset
        r_[0x10 / 4] = 0x800000C0u;     // USER: USR_COMMAND, CS_HOLD/SETUP
        r_[0x18 / 4] = 0x70000000u;     // USER2: 8 bits de orden
        raw_ = ena_ = 0;
        clk_ = false; cs_ = true;
        if (salida) salida(false, false, true);
        evalua();
    }
    uint64_t transacciones() const { return n_trans_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return r_[0] | (ocupado_ ? (1u << 24) : 0u);
        case 0x34: return ena_;
        case 0x3C: return raw_;
        case 0x40: return raw_ & ena_;
        case 0xF0: return 0x2007220u;
        default: return off < 0x100 ? r_[off / 4] : 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
        case 0x00:
            r_[0] = v & ~((1u << 24) | (1u << 23));
            if (v & (1u << 24)) { pedido_ = true; ev_.notify(sc_core::SC_ZERO_TIME); }
            return;
        case 0x34: ena_ = v; evalua(); return;
        case 0x38: raw_ &= ~v; evalua(); return;
        default: if (off < 0x100) r_[off / 4] = v; return;
        }
    }

private:
    uint32_t r_[64] = {};
    uint32_t raw_ = 0, ena_ = 0;
    bool irq_ = false, pedido_ = false, ocupado_ = false, clk_ = false, cs_ = true;
    uint64_t n_trans_ = 0;
    sc_core::sc_event ev_;

    void evalua() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    double hz() const {
        const uint32_t c = r_[0x0C / 4];
        if (c >> 31) return hz_apb;
        return hz_apb / (double(((c >> 18) & 0xFu) + 1u) * double(((c >> 12) & 0x3Fu) + 1u));
    }
    // Un bit: MOSI y SCLK, y lo que se lee de MISO
    bool bit(bool b, bool idle_alto) {
        const sc_core::sc_time m(0.5 / hz(), sc_core::SC_SEC);
        if (salida) salida(idle_alto, b, cs_);          // MOSI cambia con SCLK en reposo
        sc_core::wait(m);
        if (salida) salida(!idle_alto, b, cs_);         // el flanco en que se muestrea
        const bool v = miso ? miso() : false;
        sc_core::wait(m);
        if (salida) salida(idle_alto, b, cs_);
        return v;
    }
    void trans() {
        for (;;) {
            while (!pedido_) sc_core::wait(ev_);
            pedido_ = false;
            ocupado_ = true;
            const uint32_t user = r_[0x10 / 4], user1 = r_[0x14 / 4], user2 = r_[0x18 / 4];
            const bool idle_alto = (r_[0x20 / 4] >> 29) & 1u;
            cs_ = (r_[0x20 / 4] & 1u) != 0;                  // CS0_DIS: no baja
            if (salida) salida(idle_alto, false, cs_);
            sc_core::wait(sc_core::sc_time(1.0 / hz(), sc_core::SC_SEC));
            if ((user >> 31) & 1u) {                         // orden
                const unsigned n = ((user2 >> 28) & 0xFu) + 1u;
                const uint32_t val = user2 & 0xFFFFu;
                for (int i = int(n) - 1; i >= 0; --i) bit((val >> i) & 1u, idle_alto);
            }
            if ((user >> 30) & 1u) {                         // dirección
                const unsigned n = ((user1 >> 27) & 0x1Fu) + 1u;
                const uint32_t a = r_[1];
                for (int i = int(n) - 1; i >= 0; --i) bit((a >> i) & 1u, idle_alto);
            }
            if ((user >> 29) & 1u)                           // espera
                for (unsigned i = 0; i <= (user1 & 0xFFu); ++i) bit(false, idle_alto);
            const bool mosi = (user >> 27) & 1u, miso_f = (user >> 28) & 1u;
            if (mosi || miso_f) {
                const unsigned nbits = (r_[0x1C / 4] & 0x3FFFFu) + 1u;
                const unsigned nb = (nbits + 7) / 8;
                std::vector<uint8_t> tx(nb, 0), rx(nb, 0);
                const bool dma = (r_[0x30 / 4] >> 28) & 1u;
                if (mosi && dma && gdma) gdma->saca(0, tx.data(), nb);
                else if (mosi) for (unsigned i = 0; i < nb && i < 64; ++i) tx[i] = uint8_t(r_[0x98 / 4 + i / 4] >> (8 * (i % 4)));
                for (unsigned k = 0; k < nbits; ++k) {
                    const bool v = bit((tx[k / 8] >> (7 - k % 8)) & 1u, idle_alto);
                    if (v) rx[k / 8] |= uint8_t(0x80u >> (k % 8));
                }
                if (miso_f && !dma)
                    for (unsigned i = 0; i < nb && i < 64; ++i) {
                        uint32_t& w = r_[0x98 / 4 + i / 4];
                        w = (w & ~(0xFFu << (8 * (i % 4)))) | (uint32_t(rx[i]) << (8 * (i % 4)));
                    }
            }
            sc_core::wait(sc_core::sc_time(1.0 / hz(), sc_core::SC_SEC));
            cs_ = true;
            if (salida) salida(idle_alto, false, true);
            ++n_trans_;
            ocupado_ = false;
            raw_ |= 1u << 12;                                // TRANS_DONE
            evalua();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_SPI_GP_ESP_H
