// =============================================================================
// ssi.h — Los SSI del MSP432E4, como maestros SPI (T6, entrega 3)
//
// Cuatro, SSI0..3, de 4 KB a partir de 0x4000_8000. Es la PL022 de ARM con
// añadidos de TI (los modos de dos y cuatro hilos de datos) [H; hw_ssi.h;
// segunda fuente: ssi.c de DriverLib]:
//
//   CR0     DSS (4 a 16 bits), FRF (0 = SPI de Freescale), SPO, SPH, SCR
//   CR1     LBM (bucle interno), SSE, MS (esclavo), EOT, MODE
//   DR      la FIFO de 8 de cada lado
//   SR      TFE, TNF, RNE, RFF, BSY
//   CPSR    el predivisor, par de 2 a 254: SSInClk = reloj / (CPSDVSR · (1 + SCR))
//   IM/RIS/MIS/ICR   ROR, RT, RX (FIFO de recepción medio llena), TX (de
//           transmisión medio vacía), EOT
//   CC      SYSCLK (0) o ALTCLK (5)
//
// LOS PINES: SSInClk, SSInFss, SSInXDAT0 (sale) y SSInXDAT1 (entra), en el
// modo «legacy». En el SPI de Freescale, Fss baja mientras haya tramas
// seguidas en la FIFO y sube al acabar; con SPH = 0 sube entre trama y trama
// [ARM DDI 0194].
//
// LO QUE NO ESTÁ: el modo esclavo, los formatos de TI y de Microwire, los modos
// de dos y cuatro hilos, y el tiempo de espera de recepción (RTRIS) [P-17 i].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_SSI_H
#define STM32_PERIPH_MSP432E4_SSI_H

#include <deque>
#include <functional>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"
#include "gpio.h"

namespace stm32 {

class SsiE4 : public BusSlave {
public:
    enum : uint32_t {
        R_CR0 = 0x00, R_CR1 = 0x04, R_DR = 0x08, R_SR = 0x0C, R_CPSR = 0x10, R_IM = 0x14,
        R_RIS = 0x18, R_MIS = 0x1C, R_ICR = 0x20, R_DMACTL = 0x24, R_PP = 0xFC0, R_CC = 0xFC8
    };
    static constexpr uint32_t I_ROR = 1u << 0, I_RT = 1u << 1, I_RX = 1u << 2, I_TX = 1u << 3,
        I_DMARX = 1u << 4, I_DMATX = 1u << 5, I_EOT = 1u << 6;
    static constexpr unsigned FIFO = 8;

    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_signal<bool> sclk{"sclk"}, fss{"fss"}, dat0{"dat0"}, dat1{"dat1"}, uno{"uno"};
    sc_core::sc_signal<bool> dma_rx{"dma_rx"}, dma_tx{"dma_tx"};
    void dma_hecho(bool es_tx) { ris_ |= es_tx ? I_DMATX : I_DMARX; actualiza(); }
    std::function<double(unsigned cs)> f_reloj;

    SsiE4(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(SsiE4);
        SC_THREAD(maestro_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        fss.write(true); uno.write(true);
    }
    FuncionE4 fn_clk()  { FuncionE4 f; f.out = &sclk; f.oe = &uno; f.nombre = "SSInClk"; return f; }
    FuncionE4 fn_fss()  { FuncionE4 f; f.out = &fss;  f.oe = &uno; f.nombre = "SSInFss"; return f; }
    FuncionE4 fn_dat0() { FuncionE4 f; f.out = &dat0; f.oe = &uno; f.nombre = "SSInXDAT0"; return f; }
    FuncionE4 fn_dat1() { FuncionE4 f; f.in = &dat1; f.idle_in = false; f.nombre = "SSInXDAT1"; return f; }
    unsigned tramas() const { return n_; }
    double hz() const {
        const double f = f_reloj ? f_reloj(cc_ & 0xFu) : 0.0;
        const unsigned cps = cpsr_ ? cpsr_ : 2u;
        return f / (double(cps) * (1.0 + double((cr0_ >> 8) & 0xFFu)));
    }

protected:
    uint32_t cr0_ = 0, cr1_ = 0, cpsr_ = 0, im_ = 0, ris_ = 0, dmactl_ = 0, cc_ = 0;
    std::deque<uint16_t> txf_, rxf_;
    bool bsy_ = false;
    bool o_clk_ = false, o_fss_ = true, o_dat_ = false;
    unsigned n_ = 0;
    sc_core::sc_event ev_, pub_ev_;

    unsigned bits() const { return ((cr0_ & 0xFu) < 3u ? 3u : (cr0_ & 0xFu)) + 1u; }
    bool cpol() const { return (cr0_ >> 6) & 1u; }
    bool cpha() const { return (cr0_ >> 7) & 1u; }
    void pon(bool c, bool s, bool d) { o_clk_ = c; o_fss_ = s; o_dat_ = d; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void actualiza() {
        if (rxf_.size() >= FIFO / 2) ris_ |= I_RX; else ris_ &= ~I_RX;
        if (txf_.size() <= FIFO / 2 && !(cr1_ & 0x10u)) ris_ |= I_TX; else if (!(cr1_ & 0x10u)) ris_ &= ~I_TX;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        if (sclk.read() != o_clk_) sclk.write(o_clk_);
        if (fss.read() != o_fss_) fss.write(o_fss_);
        if (dat0.read() != o_dat_) dat0.write(o_dat_);
        irq.write((ris_ & im_) != 0);
        dma_tx.write((dmactl_ & 2u) && (cr1_ & 2u) && txf_.size() < FIFO);
        dma_rx.write((dmactl_ & 1u) && (cr1_ & 2u) && !rxf_.empty());
    }
    void reset_proc() {
        if (rst_n.read()) return;
        cr0_ = cr1_ = cpsr_ = im_ = ris_ = dmactl_ = cc_ = 0;
        txf_.clear(); rxf_.clear(); bsy_ = false;
        pon(false, true, false);
        actualiza();
    }

    void maestro_proc() {
        for (;;) {
            if (!(cr1_ & 2u) || (cr1_ & 4u) || txf_.empty() || hz() <= 0) {
                if (!bsy_) pon(cpol(), true, o_dat_);
                wait(ev_);
                continue;
            }
            bsy_ = true;
            const sc_core::sc_time medio(0.5 / hz(), sc_core::SC_SEC);
            const uint16_t d = txf_.front();
            txf_.pop_front();
            actualiza();
            const unsigned n = bits();
            uint16_t r = 0;
            const bool lbm = (cr1_ & 1u) != 0;
            pon(cpol(), false, (d >> (n - 1)) & 1u);               // Fss baja
            wait(medio);
            for (unsigned i = 0; i < n; ++i) {
                const bool b = (d >> (n - 1 - i)) & 1u;
                bool leido;
                if (!cpha()) {
                    pon(cpol(), false, b);
                    wait(medio);
                    leido = lbm ? b : dat1.read();                 // primer flanco: muestrea
                    pon(!cpol(), false, b);
                    wait(medio);
                } else {
                    pon(!cpol(), false, b);                        // primer flanco: cambia
                    wait(medio);
                    leido = lbm ? b : dat1.read();
                    pon(cpol(), false, b);
                    wait(medio);
                }
                r = uint16_t((r << 1) | (leido ? 1u : 0u));
            }
            ++n_;
            if (rxf_.size() < FIFO) rxf_.push_back(r); else ris_ |= I_ROR;
            // Con SPH = 0 Fss sube entre tramas; con SPH = 1, solo al acabar
            if (!cpha() || txf_.empty()) { pon(cpol(), true, o_dat_); wait(medio); }
            bsy_ = !txf_.empty();
            if (!bsy_ && (cr1_ & 0x10u)) ris_ |= I_EOT | I_TX;
            actualiza();
        }
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR0: return cr0_;
            case R_CR1: return cr1_;
            case R_DR: {
                if (rxf_.empty()) return 0;
                const uint16_t v = rxf_.front();
                rxf_.pop_front();
                actualiza();
                return v;
            }
            case R_SR: {
                uint32_t s = 0;
                if (txf_.empty()) s |= 1u;
                if (txf_.size() < FIFO) s |= 2u;
                if (!rxf_.empty()) s |= 4u;
                if (rxf_.size() >= FIFO) s |= 8u;
                if (bsy_ || !txf_.empty()) s |= 16u;
                return s;
            }
            case R_CPSR: return cpsr_;
            case R_IM: return im_;
            case R_RIS: return ris_;
            case R_MIS: return ris_ & im_;
            case R_DMACTL: return dmactl_;
            case R_PP: return 0x0000000Du;           // FSSHLDFRM, MODE, HSCLK [1F]
            case R_CC: return cc_;
            case 0xFE0: return 0x22; case 0xFE4: return 0x00; case 0xFE8: return 0x18;
            case 0xFEC: return 0x01; case 0xFF0: return 0x0D; case 0xFF4: return 0xF0;
            case 0xFF8: return 0x05; case 0xFFC: return 0xB1;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
            case R_CR0: cr0_ = v & 0xFFFFu; break;
            case R_CR1: cr1_ = v & 0xFFFu; break;
            case R_DR:
                if (txf_.size() < FIFO) txf_.push_back(uint16_t(v & ((1u << bits()) - 1u)));
                ris_ &= ~I_EOT;
                break;
            case R_CPSR: cpsr_ = v & 0xFEu; break;
            case R_IM: im_ = v & 0x7Fu; break;
            case R_ICR: ris_ &= ~(v & (I_ROR | I_RT | I_DMARX | I_DMATX | I_EOT)); break;
            case R_DMACTL: dmactl_ = v & 3u; break;
            case R_CC: cc_ = v & 0xFu; break;
            default: return;
        }
        actualiza();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_SSI_H
