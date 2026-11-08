// =============================================================================
// twai_esp.h — El TWAI de los ESP32 (el del C3): un SJA1000 en modo PeliCAN
//              con los registros a 4 bytes, sobre el motor CAN de bit común
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §28; twai_reg.h y twai_ll.h de
// ESP-IDF; hoja del SJA1000, §6]. El protocolo -relleno, CRC, asentimiento,
// arbitraje, banderas de error, TEC/REC, bus-off- es el de CanMotor, el mismo
// del bxCAN del STM32 y del C_CAN del MSP432E4; aquí va el mapa de registros:
//
//   0x00 MODE       RESET_MODE (0), LISTEN_ONLY (1), SELF_TEST (2), RX_FILTER (3:
//                   1 = un filtro de 32 bits, 0 = dos)
//   0x04 CMD        TX_REQ (0), ABORT_TX (1), RELEASE_BUF (2), CLR_OVERRUN (3),
//                   SELF_RX_REQ (4: manda y se recibe a sí mismo)
//   0x08 STATUS     RX_BUF (0), OVERRUN (1), TX_BUF (2: libre), TX_COMPLETE (3),
//                   RX (4), TX (5), ERR (6: un contador en el límite), BUS_OFF (7)
//   0x0C INT_RAW    RX (0), TX (1), ERR_WARN (2), OVERRUN (3), ERR_PASSIVE (5),
//                   ARB_LOST (6), BUS_ERR (7). Se borra al leerlo, salvo RX,
//                   que sigue a la FIFO
//   0x10 INT_ENA    0x18/0x1C BUS_TIMING_0/1: BRP (13 bits), SJW, TSEG1, TSEG2
//   0x2C ARB_LOST_CAP  0x30 ERR_CODE_CAP  0x34 ERR_WARNING_LIMIT (96)
//   0x38 RX_ERR_CNT 0x3C TX_ERR_CNT (escribibles en modo reset)
//   0x40-0x70 DATA_0..12: en modo reset, el código y la máscara del filtro
//                   (DATA_0-3 y DATA_4-7); en marcha, escribir es el buffer de
//                   transmisión y leer, el marco de cabeza de la FIFO
//   0x74 RX_MESSAGE_CNT  0x7C CLOCK_DIVIDER
//
// El tiempo de bit: el TWAI cuenta con APB/2, así que tq = 2 (BRP + 1) / APB
// (twai_ll.h escribe brp/2 - 1); ts1 = TSEG1 + 1, ts2 = TSEG2 + 1.
//
// La FIFO de recepción, de 64 bytes como en el SJA1000: cada marco ocupa
// 3 + DLC bytes (estándar) o 5 + DLC (extendido); si no cabe, OVERRUN.
//
// El bus-off pone RESET_MODE, como el SJA1000; al quitarlo, los contadores
// vuelven a cero (sin las 128 x 11 recesivas de la recuperación).
//
// Los pines: la señal TWAI_TX (74) de la matriz, recesiva a 1, y la TWAI_RX
// (74), que llega por `rx_pin`.
// =============================================================================
#ifndef STM32_PERIPH_ESP_TWAI_ESP_H
#define STM32_PERIPH_ESP_TWAI_ESP_H

#include <systemc>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include "../can_motor.h"

namespace stm32 {

class TwaiEsp : public CanMotor {
public:
    std::function<void(bool)> irq;
    std::function<void(bool)> salida;            // a TWAI_TX de la matriz
    sc_core::sc_in<bool> rx_pin{"rx_pin"};       // de TWAI_RX de la matriz

    enum : uint32_t {
        MODE_RESET = 1, MODE_LOM = 2, MODE_STM = 4, MODE_AFM = 8,
        CMD_TR = 1, CMD_AT = 2, CMD_RRB = 4, CMD_CDO = 8, CMD_SRR = 16,
        ST_RBS = 1, ST_DOS = 2, ST_TBS = 4, ST_TCS = 8, ST_RS = 16, ST_TS = 32, ST_ES = 64, ST_BS = 128,
        I_RX = 1, I_TX = 2, I_EW = 4, I_DO = 8, I_EP = 32, I_AL = 64, I_BE = 128
    };

    explicit TwaiEsp(sc_core::sc_module_name nm, uint32_t base) : CanMotor(nm, base, 0x1000) {
        SC_HAS_PROCESS(TwaiEsp);
        freeze(s_cero_);
        SC_THREAD(bit_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(rx_proc);    sensitive << rx_pin;
        reset();
    }
    void reset() {
        mode_ = MODE_RESET; ena_ = 0; raw_ = 0; bt0_ = bt1_ = 0; ewl_ = 96; cdr_ = 0;
        alc_ = ecc_ = 0;
        for (auto& b : acc_) b = 0;
        for (auto& b : txb_) b = 0;
        fifo_.clear(); fifo_bytes_ = 0;
        pend_ = false; srr_ = false; tcs_ = true; dos_ = false; es_ = false; ep_ = false;
        tec_ = rec_ = 0; lec_ = 0; boff_ = false;
        estado_ = IDLE; tx_activo_ = -1;
        o_tx_ = true; o_oe_ = false; o_irq_ = false;
        publish();
        if (irq) irq(false);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // Para el banco
    unsigned en_fifo() const { return unsigned(fifo_.size()); }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off >= 0x40 && off <= 0x70) {
            const unsigned i = (off - 0x40) / 4;
            if (mode_ & MODE_RESET) return i < 8 ? acc_[i] : 0;
            return fifo_.empty() ? 0 : fifo_.front().b[i];
        }
        switch (off) {
        case 0x00: return mode_;
        case 0x08: return status();
        case 0x0C: {
            const uint32_t v = raw_ | (fifo_.empty() ? 0 : I_RX);
            raw_ = 0;
            evalua();
            return v;
        }
        case 0x10: return ena_;
        case 0x18: return bt0_;
        case 0x1C: return bt1_;
        case 0x2C: return alc_;
        case 0x30: return ecc_;
        case 0x34: return ewl_;
        case 0x38: return rec_;
        case 0x3C: return tec_;
        case 0x74: return unsigned(fifo_.size());
        case 0x7C: return cdr_;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off >= 0x40 && off <= 0x70) {
            const unsigned i = (off - 0x40) / 4;
            if (mode_ & MODE_RESET) { if (i < 8) acc_[i] = uint8_t(v); }
            else txb_[i] = uint8_t(v);
            return;
        }
        switch (off) {
        case 0x00: {
            const bool estaba = mode_ & MODE_RESET;
            mode_ = v & 0xFu;
            if (estaba && !(mode_ & MODE_RESET) && boff_) { boff_ = false; tec_ = rec_ = 0; es_ = ep_ = false; }
            if (mode_ & MODE_RESET) { pend_ = false; estado_ = IDLE; tx_activo_ = -1; }
            ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        case 0x04:
            if (mode_ & MODE_RESET) return;
            if (v & (CMD_TR | CMD_SRR)) {
                pend_ = true; srr_ = v & CMD_SRR; tcs_ = false;
                ev_.notify(sc_core::SC_ZERO_TIME);
            }
            if (v & CMD_AT) {
                // Cancela lo que no ha empezado; lo que está en el hilo acaba
                if (tx_activo_ < 0) { pend_ = false; }
                else abortar_ = true;
            }
            if ((v & CMD_RRB) && !fifo_.empty()) {
                fifo_bytes_ -= fifo_.front().n;
                fifo_.pop_front();
            }
            if (v & CMD_CDO) dos_ = false;
            evalua();
            return;
        case 0x10: ena_ = v & 0x1FFu; evalua(); return;
        case 0x18: if (mode_ & MODE_RESET) bt0_ = v & 0xFFFFu; return;
        case 0x1C: if (mode_ & MODE_RESET) bt1_ = v & 0xFFu; return;
        case 0x34: if (mode_ & MODE_RESET) ewl_ = v & 0xFFu; return;
        case 0x38: if (mode_ & MODE_RESET) rec_ = v & 0xFFu; return;
        case 0x3C: if (mode_ & MODE_RESET) tec_ = v & 0xFFu; return;
        case 0x7C: cdr_ = v & 0x1FFu; return;
        default: return;
        }
    }

private:
    struct Marco { uint8_t b[13]; unsigned n; };
    uint32_t mode_ = MODE_RESET, ena_ = 0, raw_ = 0, bt0_ = 0, bt1_ = 0, ewl_ = 96, cdr_ = 0;
    uint32_t alc_ = 0, ecc_ = 0;
    uint8_t acc_[8] = {}, txb_[13] = {};
    std::deque<Marco> fifo_;
    unsigned fifo_bytes_ = 0;
    bool pend_ = false, srr_ = false, tcs_ = true, dos_ = false, abortar_ = false;
    bool es_ = false, ep_ = false, o_irq_ = false;
    sc_core::sc_signal<bool> s_cero_{"s_cero"};

    uint32_t status() const {
        uint32_t s = 0;
        if (!fifo_.empty()) s |= ST_RBS;
        if (dos_) s |= ST_DOS;
        if (!pend_ && tx_activo_ < 0) s |= ST_TBS;
        if (tcs_) s |= ST_TCS;
        if (estado_ == RX) s |= ST_RS;
        if (tx_activo_ >= 0) s |= ST_TS;
        if (tec_ >= ewl_ || rec_ >= ewl_) s |= ST_ES;
        if (boff_) s |= ST_BS;
        return s;
    }
    void evalua() {
        const bool n = ((raw_ | (fifo_.empty() ? 0 : I_RX)) & ena_) != 0;
        if (n != o_irq_) { o_irq_ = n; if (irq) irq(n); }
    }
    // Los cambios de ERR_ST y de error pasivo dan sus interrupciones
    void estados() {
        const bool es = tec_ >= ewl_ || rec_ >= ewl_ || boff_;
        if (es != es_) { es_ = es; raw_ |= I_EW; }
        const bool ep = error_pasivo() && !boff_;
        if (ep != ep_) { ep_ = ep; raw_ |= I_EP; }
    }
    void pub_proc() {
        tx_out.write(o_tx_); tx_oe.write(o_oe_);
        if (salida) salida(o_oe_ ? o_tx_ : true);
    }
    void rx_proc() { rx_in.write(rx_pin.read()); }

    // ---- Los ganchos del motor --------------------------------------------
    bool     cm_parado() const override { return mode_ & MODE_RESET; }
    double   cm_tq() const override {
        const double f = clk_hz.read();
        return f > 0.0 ? 2.0 * double((bt0_ & 0x1FFFu) + 1u) / f : 0.0;
    }
    unsigned cm_ts1() const override { return (bt1_ & 0xFu) + 1u; }
    unsigned cm_ts2() const override { return ((bt1_ >> 4) & 0x7u) + 1u; }
    bool     cm_bucle() const override { return false; }
    bool     cm_silencio() const override { return mode_ & MODE_LOM; }
    int      cm_siguiente() const override { return pend_ && !(mode_ & MODE_LOM) ? 0 : -1; }
    CanFrame cm_marco(unsigned) const override {
        CanFrame f;
        f.ide = txb_[0] & 0x80u;
        f.rtr = txb_[0] & 0x40u;
        f.dlc = uint8_t(std::min(8u, txb_[0] & 0xFu));
        unsigned p;
        if (f.ide) {
            f.id = (uint32_t(txb_[1]) << 21) | (uint32_t(txb_[2]) << 13) | (uint32_t(txb_[3]) << 5) | (txb_[4] >> 3);
            p = 5;
        } else {
            f.id = (uint32_t(txb_[1]) << 3) | (txb_[2] >> 5);
            p = 3;
        }
        for (unsigned k = 0; k < 8; ++k) f.data[k] = (p + k < 13) ? txb_[p + k] : 0;
        return f;
    }
    void cm_tx_empieza() override { abortar_ = false; }
    void cm_arbitraje_perdido(unsigned) override {
        // El SJA1000 reintenta solo; ALC guarda el bit en que perdió
        alc_ = 0; raw_ |= I_AL;
        if (abortar_) { pend_ = false; abortar_ = false; }
        evalua();
    }
    void cm_tx_ok(unsigned) override {
        if (srr_) mete(cm_marco(0));
        pend_ = false; srr_ = false; abortar_ = false; tcs_ = true;
        ++n_tx_;
        raw_ |= I_TX;
        estados();
        evalua();
    }
    void cm_tx_error(unsigned) override {
        if (abortar_) { pend_ = false; abortar_ = false; raw_ |= I_TX; }
    }
    void cm_rx_ok(const CanFrame& f) override {
        if (!pasa(f)) return;
        mete(f);
        estados();
        evalua();
    }
    void cm_eco() override { estados(); evalua(); }
    void cm_error() override {
        // ERR_CODE_CAP: el tipo en los bits 6-7 (0 bit, 1 forma, 2 relleno, 3 otro)
        const uint32_t tipo = lec_ == LEC_STUFF ? 2u : lec_ == LEC_FORM ? 1u
                            : (lec_ == LEC_BIT0 || lec_ == LEC_BIT1) ? 0u : 3u;
        ecc_ = (tipo << 6) | (fue_emisor_ ? 0u : (1u << 5));
        raw_ |= I_BE;
        estados();
        evalua();
    }
    void cm_bus_off() override {
        mode_ |= MODE_RESET;
        pend_ = false;
        estados();
        evalua();
    }

    // La FIFO: el marco en el formato de los DATA
    void mete(const CanFrame& f) {
        Marco m{};
        m.b[0] = uint8_t((f.ide ? 0x80u : 0u) | (f.rtr ? 0x40u : 0u) | (f.dlc & 0xFu));
        unsigned p;
        if (f.ide) {
            m.b[1] = uint8_t(f.id >> 21); m.b[2] = uint8_t(f.id >> 13);
            m.b[3] = uint8_t(f.id >> 5);  m.b[4] = uint8_t((f.id & 0x1Fu) << 3);
            p = 5;
        } else {
            m.b[1] = uint8_t(f.id >> 3); m.b[2] = uint8_t((f.id & 7u) << 5);
            p = 3;
        }
        const unsigned nd = f.rtr ? 0u : std::min<unsigned>(8u, f.dlc);
        for (unsigned k = 0; k < nd; ++k) m.b[p + k] = f.data[k];
        m.n = p + nd;
        if (fifo_bytes_ + m.n > 64) { dos_ = true; raw_ |= I_DO; return; }
        fifo_bytes_ += m.n;
        fifo_.push_back(m);
        ++n_rx_;
    }
    // El filtro de aceptación [SJA1000, §6.4.15]: un bit de máscara a 1 es «no importa»
    bool pasa(const CanFrame& f) const {
        uint8_t m[4] = {0, 0, 0, 0};          // los bytes que se comparan
        if (f.ide) {
            m[0] = uint8_t(f.id >> 21); m[1] = uint8_t(f.id >> 13); m[2] = uint8_t(f.id >> 5);
            m[3] = uint8_t(((f.id & 0x1Fu) << 3) | (f.rtr ? 4u : 0u));
        } else {
            m[0] = uint8_t(f.id >> 3);
            m[1] = uint8_t(((f.id & 7u) << 5) | (f.rtr ? 0x10u : 0u));
            m[2] = f.dlc > 0 && !f.rtr ? f.data[0] : 0; m[3] = f.dlc > 1 && !f.rtr ? f.data[1] : 0;
        }
        auto casa = [&](unsigned i, uint8_t bits, unsigned code, unsigned mask) {
            return ((m[i] ^ acc_[code]) & ~acc_[mask] & bits) == 0;
        };
        if (mode_ & MODE_AFM) {
            // Un filtro: 4 bytes (en estándar, los 4 bits bajos de m[1] no cuentan)
            const uint8_t b1 = f.ide ? 0xFF : 0xF0;
            const uint8_t b3 = f.ide ? 0xFC : 0xFF;
            const unsigned n = f.ide ? 4u : (f.dlc >= 2 ? 4u : f.dlc == 1 ? 3u : 2u);
            bool ok = casa(0, 0xFF, 0, 4) && casa(1, b1, 1, 5);
            if (n > 2) ok = ok && casa(2, 0xFF, 2, 6);
            if (n > 3) ok = ok && casa(3, b3, 3, 7);
            return ok;
        }
        // Dos filtros: los 16 bits altos del identificador con cada uno
        // (en estándar, el filtro 1 mira además el primer dato)
        bool f1, f2;
        if (f.ide) {
            f1 = casa(0, 0xFF, 0, 4) && casa(1, 0xFF, 1, 5);
            f2 = casa(0, 0xFF, 2, 6) && casa(1, 0xFF, 3, 7);
        } else {
            f1 = casa(0, 0xFF, 0, 4) && casa(1, 0xF0, 1, 5) &&
                 (f.dlc == 0 || f.rtr ||
                  (((uint8_t(acc_[1] << 4) | (acc_[3] & 0xF)) ^ m[2]) & ~(uint8_t(acc_[5] << 4) | (acc_[7] & 0xF))) == 0);
            f2 = casa(0, 0xFF, 2, 6) && casa(1, 0xF0, 3, 7);
        }
        return f1 || f2;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_TWAI_ESP_H
