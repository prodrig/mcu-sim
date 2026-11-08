// =============================================================================
// rmt_esp.h — El Remote Control Transceiver de los ESP32 (el del C3: 2 canales
//             de transmisión, 0 y 1, y 2 de recepción, 2 y 3)
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §32; rmt_reg.h de ESP-IDF]. Lo que
// usa `led_strip` (WS2812) y un mando a distancia por infrarrojos:
//
//   * la RAM: 4 bloques de 48 palabras en base + 0x400 (el canal n empieza en
//     el bloque n; MEM_SIZE lo alarga a los siguientes). Cada palabra son dos
//     tramos {duración0[14:0], nivel0[15], duración1[30:16], nivel1[31]}; una
//     duración 0 acaba la transmisión;
//   * el reloj: RMT_SYS_CONF (SCLK_SEL 1 APB, 2 RC_FAST, 3 XTAL; SCLK_DIV_NUM
//     + 1) y, por canal, DIV_CNT (0 = 256): un tic;
//   * TX_START manda desde el principio del bloque; al acabar, la salida va a
//     IDLE_OUT_LV si IDLE_OUT_EN, y TX_END (bit n de INT_RAW) a la fuente 28;
//     TX_CONTI_MODE vuelve a empezar; TX_STOP corta.
//
// La salida del canal n es la señal RMT_SIG_OUT0 + n (51, 52) de la matriz.
// LO QUE NO SE MODELA: la recepción (canales 2 y 3: guardan, no miden), la
// portadora, el envoltorio de la memoria (MEM_TX_WRAP_EN y TX_LIM: un envío de
// más de MEM_SIZE x 48 palabras se corta), el modo FIFO (APB_FIFO_MASK) y
// TX_SIM (arrancar los dos a la vez). `led_strip` con más de unos pocos LED
// usa el envoltorio: con el RMT de aquí, hasta ~15 LED por canal.
// =============================================================================
#ifndef STM32_PERIPH_ESP_RMT_ESP_H
#define STM32_PERIPH_ESP_RMT_ESP_H

#include <systemc>
#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class RmtEsp : public BusSlave {
public:
    std::function<void(bool)> irq;
    std::function<void(unsigned, bool)> salida;         // canal 0 o 1
    double hz_apb = 80e6, hz_rc_fast = 17.5e6, hz_xtal = 40e6;

    explicit RmtEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        for (unsigned c = 0; c < 2; ++c) sc_core::sc_spawn([this, c] { tx_proc(c); });
        reset();
    }
    void reset() {
        for (auto& w : ram_) w = 0;
        for (auto& c : conf_) c = 0;
        conf_[0] = conf_[1] = (1u << 16) | (2u << 8) | (1u << 6);    // MEM_SIZE 1, DIV 2, IDLE_OUT_EN
        raw_ = ena_ = 0;
        sys_ = (1u << 24) | (1u << 31);
        for (auto& e : ev_) e.notify(sc_core::SC_ZERO_TIME);
        evalua();
    }
    uint64_t envios(unsigned c) const { return c < 2 ? n_envios_[c] : 0; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off >= 0x400 && off < 0x400 + 4 * 192) return ram_[(off - 0x400) / 4];
        if (off >= 0x10 && off < 0x28) return conf_[(off - 0x10) / 4] & ~((1u << 0) | (1u << 24));
        switch (off) {
        case 0x38: return raw_;
        case 0x3C: return raw_ & ena_;
        case 0x40: return ena_;
        case 0x68: return sys_;
        case 0xCC: return 0x2006230u;
        default: return otros_[(off / 4) & 63];
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off >= 0x400 && off < 0x400 + 4 * 192) { ram_[(off - 0x400) / 4] = v; return; }
        if (off >= 0x10 && off < 0x28) {
            const unsigned i = (off - 0x10) / 4;
            conf_[i] = v;
            if (i < 2) {
                if (v & 1u) { pedido_[i] = true; ev_[i].notify(sc_core::SC_ZERO_TIME); }
                if (v & (1u << 7)) { parar_[i] = true; ev_[i].notify(sc_core::SC_ZERO_TIME); }
            }
            return;
        }
        switch (off) {
        case 0x40: ena_ = v; evalua(); return;
        case 0x44: raw_ &= ~v; evalua(); return;
        case 0x68: sys_ = v; return;
        default: otros_[(off / 4) & 63] = v; return;
        }
    }

private:
    uint32_t ram_[192] = {}, conf_[6] = {}, otros_[64] = {};
    uint32_t raw_ = 0, ena_ = 0, sys_ = 0;
    bool irq_ = false, pedido_[2] = {}, parar_[2] = {}, nivel_[2] = {};
    uint64_t n_envios_[2] = {};
    sc_core::sc_event ev_[2];

    void evalua() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    void pon(unsigned c, bool v) { if (nivel_[c] != v || true) { nivel_[c] = v; if (salida) salida(c, v); } }
    double tic_s(unsigned c) const {
        const unsigned sel = (sys_ >> 24) & 3u;
        const double f = sel == 1 ? hz_apb : sel == 2 ? hz_rc_fast : sel == 3 ? hz_xtal : 0.0;
        const double div = double(((sys_ >> 4) & 0xFFu) + 1u);
        unsigned d = (conf_[c] >> 8) & 0xFFu;
        if (!d) d = 256;
        return f > 0 ? div * d / f : 0.0;
    }
    void reposo(unsigned c) {
        if ((conf_[c] >> 6) & 1u) pon(c, (conf_[c] >> 5) & 1u);
    }
    void tx_proc(unsigned c) {
        pon(c, false);
        for (;;) {
            while (!pedido_[c]) { reposo(c); sc_core::wait(ev_[c]); }
            pedido_[c] = parar_[c] = false;
            const double t = tic_s(c);
            const unsigned bloques = std::max(1u, (conf_[c] >> 16) & 7u);
            const unsigned n = std::min(48u * bloques, 192u - 48u * c);
            bool otra = true;
            while (otra) {
                otra = false;
                bool fin = false;
                for (unsigned k = 0; k < n && !fin && !parar_[c]; ++k) {
                    const uint32_t w = ram_[48 * c + k];
                    for (unsigned h = 0; h < 2 && !fin; ++h) {
                        const uint32_t tr = h ? (w >> 16) : (w & 0xFFFFu);
                        const unsigned dur = tr & 0x7FFFu;
                        if (!dur) { fin = true; break; }
                        pon(c, (tr >> 15) & 1u);
                        sc_core::wait(sc_core::sc_time(dur * t, sc_core::SC_SEC), ev_[c]);
                        if (parar_[c]) break;
                    }
                }
                if (parar_[c]) break;
                ++n_envios_[c];
                raw_ |= 1u << c;                     // TX_END
                evalua();
                if (((conf_[c] >> 3) & 1u) && !pedido_[c]) otra = true;   // TX_CONTI_MODE
            }
            reposo(c);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_RMT_ESP_H
