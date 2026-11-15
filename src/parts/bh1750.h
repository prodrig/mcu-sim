// =============================================================================
// bh1750.h — El sensor de luz BH1750 (ROHM), por I2C
//
// E10 de doc/analisis_esp32_riscv.md: el que lee el ejemplo lp_i2c del núcleo
// LP de ESP-IDF. [Hoja de datos de ROHM, BH1750FVI]:
//   * la dirección 0x23 (ADDR a GND) o 0x5C (ADDR a VCC);
//   * se le escriben órdenes de UN byte: 0x00 apagar, 0x01 encender, 0x07
//     reset del registro de datos, 0x10/0x11/0x13 medida continua (H, H2 y L),
//     0x20/0x21/0x23 una sola medida (y se apaga);
//   * se le leen DOS bytes, el alto primero: la cuenta, que es lux · 1,2 (en
//     modo H; en H2, el doble), saturada a 65535;
//   * apagado o sin medir, la cuenta es 0. La medida está en el acto (el
//     sensor de verdad tarda 120 ms en H y 16 ms en L) [de modelo].
// La máquina de bits es la de I2cReg16 (boosterpack.h).
// =============================================================================
#ifndef STM32_PARTS_BH1750_H
#define STM32_PARTS_BH1750_H

#include <algorithm>
#include <cstdint>
#include "ext_parts.h"

namespace stm32 {

class Bh1750 : public sc_core::sc_module, public I2cPart {
public:
    double lux;

    Bh1750(sc_core::sc_module_name nm, analog_net_if& scl, analog_net_if& sda, uint8_t dir = 0x23,
           double lux_ini = 300.0, double vdd = 3.3)
        : sc_core::sc_module(nm), I2cPart(scl, sda, "Bh1750", "i2c_sensor", nm, vdd), lux(lux_ini), dir_(dir) {
        SC_HAS_PROCESS(Bh1750);
        SC_METHOD(edge_proc);
        sensitive << scl_->value_changed_event() << sda_->value_changed_event();
        dont_initialize();
    }
    bool encendido() const { return on_; }
    unsigned ordenes() const { return n_ord_; }
    unsigned lecturas() const { return n_rd_; }
    uint16_t cuenta() const {
        if (!on_ || !midiendo_) return 0;
        const double c = lux * 1.2 * (h2_ ? 2.0 : 1.0);
        return uint16_t(std::min(65535.0, std::max(0.0, c + 0.5)));
    }

private:
    enum St { IDLE, ADDR, ACK_HOLD, RX, TX, MACK };
    uint8_t dir_;
    St st_ = IDLE;
    unsigned bit_ = 0, ntx_ = 0, n_ord_ = 0, n_rd_ = 0;
    uint8_t sh_ = 0;
    uint16_t dato_ = 0;
    bool c_prev_ = true, d_prev_ = true, lee_ = false, ack_next_ = false;
    bool on_ = false, midiendo_ = false, h2_ = false;

    void orden(uint8_t o) {
        ++n_ord_;
        switch (o) {
        case 0x00: on_ = false; break;
        case 0x01: on_ = true; break;
        case 0x07: midiendo_ = false; break;
        case 0x10: case 0x13: case 0x20: case 0x23: if (on_) { midiendo_ = true; h2_ = false; } break;
        case 0x11: case 0x21: if (on_) { midiendo_ = true; h2_ = true; } break;
        default: break;            // MTreg (0x40-0x7F): no cambia la cuenta aquí
        }
    }
    uint8_t siguiente_tx() {
        if (ntx_++ % 2 == 0) { dato_ = cuenta(); return uint8_t(dato_ >> 8); }
        ++n_rd_;
        return uint8_t(dato_);
    }
    void edge_proc() {
        const bool c = scl(), d = sda();
        const bool cp = c_prev_, dp = d_prev_;
        c_prev_ = c; d_prev_ = d;
        if (c && cp) {
            if (dp && !d) { st_ = ADDR; bit_ = 0; sh_ = 0; release_sda(); return; }
            if (!dp && d) { st_ = IDLE; release_sda(); return; }
        }
        if (c && !cp) {
            switch (st_) {
            case ADDR:
                sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                if (++bit_ == 8) {
                    if ((sh_ >> 1) == dir_) { lee_ = (sh_ & 1u) != 0; ack_next_ = true; }
                    else { st_ = IDLE; ack_next_ = false; }
                }
                break;
            case RX:
                sh_ = uint8_t((sh_ << 1) | (d ? 1u : 0u));
                if (++bit_ == 8) { orden(sh_); ack_next_ = true; }
                break;
            case ACK_HOLD:
                if (lee_) { st_ = TX; bit_ = 0; ntx_ = 0; sh_ = siguiente_tx(); }
                else      { st_ = RX; bit_ = 0; sh_ = 0; }
                break;
            case TX:
                if (bit_ >= 8) st_ = MACK;
                break;
            case MACK:
                if (d) { st_ = IDLE; release_sda(); }
                else   { st_ = TX; bit_ = 0; sh_ = siguiente_tx(); }
                break;
            default: break;
            }
            return;
        }
        if (!c && cp) {
            if (ack_next_) { ack_next_ = false; pull_sda(); st_ = ACK_HOLD; return; }
            switch (st_) {
            case RX:   if (bit_ == 0) release_sda(); break;
            case TX:   if (bit_ < 8) { drive_sda((sh_ >> (7 - bit_)) & 1u); ++bit_; } break;
            case MACK: release_sda(); break;
            default: break;
            }
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_BH1750_H
