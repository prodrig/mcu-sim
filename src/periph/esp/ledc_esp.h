// =============================================================================
// ledc_esp.h — El LED PWM Controller de los ESP32 (el del C3: 6 canales y 4
//              temporizadores de baja velocidad)
//
// E8 de doc/analisis_esp32_riscv.md [TRM-C3 §31; ledc_reg.h de ESP-IDF]:
//
//   * el reloj: LEDC_CONF.APB_CLK_SEL (1 APB, 2 RC_FAST, 3 XTAL) con CLK_EN;
//     cada temporizador lo divide por CLK_DIV (18 bits, 10.8 en coma fija) y
//     cuenta hasta 2^DUTY_RES; PAUSE lo para y RST lo pone a cero;
//   * cada canal, con su temporizador (TIMER_SEL): la salida sube en HPOINT y
//     baja DUTY tics después (DUTY trae 4 bits de fracción: se usa la parte
//     entera); con SIG_OUT_EN a 0, sale IDLE_LV. Los cambios de HPOINT, DUTY
//     y del temporizador se aplican al empezar el periodo siguiente (PARA_UP,
//     DUTY_START), como en el silicio;
//   * el fundido (CONF1: DUTY_INC, DUTY_NUM, DUTY_CYCLE, DUTY_SCALE): cada
//     DUTY_CYCLE periodos el ciclo sube o baja DUTY_SCALE, DUTY_NUM veces, y
//     al acabar, DUTY_CHNG_END (bits 4-9 de INT_RAW);
//   * las interrupciones: el desbordamiento de cada temporizador (bits 0-3),
//     el fin del fundido y OVF_CNT (bits 10-15, con OVF_NUM), a la fuente 23.
//
// La salida de cada canal es una señal de la matriz GPIO (LEDC_LS_SIG_OUT0-5,
// 45-50), y de ahí a un pad: un LED, un servo, un osciloscopio.
// =============================================================================
#ifndef STM32_PERIPH_ESP_LEDC_ESP_H
#define STM32_PERIPH_ESP_LEDC_ESP_H

#include <systemc>
#include <cmath>
#include <cstdint>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class LedcEsp : public BusSlave {
public:
    static constexpr unsigned N_CANALES = 6, N_TEMP = 4;
    std::function<void(bool)> irq;
    // La salida de un canal (a la señal 45 + canal de la matriz)
    std::function<void(unsigned, bool)> salida;
    double hz_apb = 80e6, hz_rc_fast = 17.5e6, hz_xtal = 40e6;

    explicit LedcEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(LedcEsp);
        for (unsigned c = 0; c < N_CANALES; ++c)
            sc_core::sc_spawn([this, c] { canal_proc(c); });
        reset();
    }
    void reset() {
        for (auto& c : ch_) c = Canal();
        for (auto& t : tim_) t = Temp();
        raw_ = ena_ = 0; conf_ = 0;
        for (auto& e : ev_) e.notify(sc_core::SC_ZERO_TIME);
        evalua();
    }
    // Lo que el banco quiere saber
    double periodo_s(unsigned c) const { const Temp& t = tim_[ch_[c].conf0 & 3u]; return tics(t) * tic_s(t); }
    uint32_t ciclo(unsigned c) const { return ch_[c].duty_act; }
    uint64_t periodos(unsigned c) const { return ch_[c].n_periodos; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x14 * N_CANALES) {
            const Canal& c = ch_[off / 0x14];
            switch (off % 0x14) {
            case 0x00: return c.conf0 & ~(1u << 4);
            case 0x04: return c.hpoint;
            case 0x08: return c.duty;
            case 0x0C: return c.conf1 & ~(1u << 31) ;
            case 0x10: return c.duty_act << 4;
            }
        }
        if (off >= 0xA0 && off < 0xA0 + 8 * N_TEMP) {
            const Temp& t = tim_[(off - 0xA0) / 8];
            if ((off & 7u) == 0) return t.conf & ~(1u << 25);
            return valor(t);
        }
        switch (off) {
        case 0xC0: return raw_;
        case 0xC4: return raw_ & ena_;
        case 0xC8: return ena_;
        case 0xD0: return conf_;
        case 0xFC: return 0x19061700u;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        if (off < 0x14 * N_CANALES) {
            const unsigned n = off / 0x14;
            Canal& c = ch_[n];
            switch (off % 0x14) {
            case 0x00: c.conf0 = v & 0x1FFFFu; if (v & (1u << 4)) c.para_up = true; break;
            case 0x04: c.hpoint = v & 0x3FFFu; break;
            case 0x08: c.duty = v & 0x7FFFFu; break;
            case 0x0C: c.conf1 = v; if (v & (1u << 31)) c.arranca = true; break;
            }
            if (off % 0x14 == 0x00 && !(c.conf0 & 4u)) c.para_up = true;   // SIG_OUT_EN a 0: ya
            ev_[n].notify(sc_core::SC_ZERO_TIME);
            return;
        }
        if (off >= 0xA0 && off < 0xA0 + 8 * N_TEMP && !(off & 7u)) {
            Temp& t = tim_[(off - 0xA0) / 8];
            const bool estaba = (t.conf >> 22) & 1u;
            t.conf = v & 0x3FFFFFFu;
            if (v & (1u << 23)) t.inicio = sc_core::sc_time_stamp();       // RST
            if (!estaba && ((t.conf >> 22) & 1u)) {}                       // PAUSE
            for (auto& e : ev_) e.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        switch (off) {
        case 0xC8: ena_ = v & 0xFFFFu; evalua(); return;
        case 0xCC: raw_ &= ~(v & 0xFFFFu); evalua(); return;
        case 0xD0: conf_ = v; for (auto& e : ev_) e.notify(sc_core::SC_ZERO_TIME); return;
        default: return;
        }
    }

private:
    struct Canal {
        uint32_t conf0 = 0, hpoint = 0, duty = 0, conf1 = 0;
        uint32_t duty_act = 0, hp_act = 0;      // los del periodo en curso
        bool para_up = false, arranca = false, nivel = false;
        // El fundido en curso
        bool fundiendo = false;
        uint32_t fn_inc = 0, fn_num = 0, fn_ciclo = 0, fn_escala = 0, fn_cuenta = 0;
        uint64_t n_periodos = 0;
        unsigned ovf = 0;
    };
    struct Temp {
        uint32_t conf = 0;
        sc_core::sc_time inicio = sc_core::SC_ZERO_TIME;
    };
    Canal ch_[N_CANALES];
    Temp tim_[N_TEMP];
    sc_core::sc_event ev_[N_CANALES];
    uint32_t raw_ = 0, ena_ = 0, conf_ = 0;
    bool irq_ = false;

    void evalua() {
        const bool n = (raw_ & ena_) != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
    double hz_src() const {
        if (!(conf_ >> 31)) return 0.0;
        const unsigned sel = conf_ & 3u;
        return sel == 1 ? hz_apb : sel == 2 ? hz_rc_fast : sel == 3 ? hz_xtal : 0.0;
    }
    static uint32_t tics(const Temp& t) { return 1u << (t.conf & 0xFu); }
    double tic_s(const Temp& t) const {
        const double div = double((t.conf >> 4) & 0x3FFFFu) / 256.0;
        const double f = hz_src();
        return f > 0.0 && div > 0.0 ? div / f : 0.0;
    }
    bool corre(const Temp& t) const { return tic_s(t) > 0.0 && !((t.conf >> 22) & 1u) && !((t.conf >> 23) & 1u); }
    uint32_t valor(const Temp& t) const {
        if (!corre(t)) return 0;
        const double n = (sc_core::sc_time_stamp() - t.inicio).to_seconds() / tic_s(t);
        return uint32_t(std::fmod(n, double(tics(t))));
    }
    void pon(unsigned n, bool v) {
        if (ch_[n].nivel == v) return;
        ch_[n].nivel = v;
        if (salida) salida(n, v);
    }

    // Un canal: periodo a periodo, alineado con su temporizador
    void canal_proc(unsigned n) {
        Canal& c = ch_[n];
        if (salida) salida(n, false);
        for (;;) {
            const Temp& t = tim_[c.conf0 & 3u];
            if (!(c.conf0 & 4u) || !corre(t)) {
                pon(n, (c.conf0 >> 3) & 1u);                     // IDLE_LV
                c.duty_act = c.duty >> 4; c.hp_act = c.hpoint;
                sc_core::wait(ev_[n]);
                continue;
            }
            // El principio del periodo siguiente, en la cuenta del temporizador
            const double ts = tic_s(t), tp = ts * tics(t);
            const double desde = (sc_core::sc_time_stamp() - t.inicio).to_seconds();
            const double k = std::floor(desde / tp + 1e-9);
            const bool frontera = std::fabs(desde / tp - k) < 1e-9;
            // Lo nuevo entra al empezar un periodo (o el primero)
            if ((c.para_up || c.arranca) && (frontera || c.n_periodos == 0)) {
                c.duty_act = c.duty >> 4; c.hp_act = c.hpoint;
                c.para_up = false;
                if (c.arranca) {
                    c.arranca = false;
                    c.fundiendo = ((c.conf1 >> 20) & 0x3FFu) != 0;
                    c.fn_inc = (c.conf1 >> 30) & 1u; c.fn_num = (c.conf1 >> 20) & 0x3FFu;
                    c.fn_ciclo = (c.conf1 >> 10) & 0x3FFu; c.fn_escala = c.conf1 & 0x3FFu;
                    c.fn_cuenta = 0;
                }
            }
            const uint32_t per = tics(t);
            const uint32_t on = std::min(c.hp_act, per), off = std::min(c.hp_act + c.duty_act, per);
            // En tiempo absoluto: el periodo [ps, pe), la subida y la bajada
            const double t_ini = t.inicio.to_seconds();
            const double ps = t_ini + k * tp, pe = ps + tp;
            const double sube = ps + on * ts, baja = ps + off * ts;
            bool cortado = false;
            for (;;) {
                const double ahora = sc_core::sc_time_stamp().to_seconds();
                pon(n, off > on && ahora >= sube - 1e-15 && ahora < baja - 1e-15);
                double sig = pe;
                if (off > on && sube > ahora + 1e-15 && sube < sig) sig = sube;
                if (off > on && baja > ahora + 1e-15 && baja < sig) sig = baja;
                if (sig <= ahora + 1e-15) break;
                sc_core::wait(sc_core::sc_time(sig - ahora, sc_core::SC_SEC), ev_[n]);
                if (ev_[n].triggered()) { cortado = true; break; }
                if (sig == pe) break;
            }
            if (cortado) continue;                               // reconfigurado: otra vez
            if (c.n_periodos == 0 && !frontera) { ++c.n_periodos; continue; }
            // Fin de periodo: desbordamiento, OVF_CNT y el fundido
            ++c.n_periodos;
            raw_ |= 1u << (c.conf0 & 3u);
            if ((c.conf0 >> 15) & 1u) {                          // OVF_CNT_EN
                if (++c.ovf > ((c.conf0 >> 5) & 0x3FFu)) { c.ovf = 0; raw_ |= 1u << (10 + n); }
            }
            if (c.fundiendo && ++c.fn_cuenta >= std::max(1u, c.fn_ciclo)) {
                c.fn_cuenta = 0;
                const uint32_t d = c.duty >> 4;
                const uint32_t nd = c.fn_inc ? std::min<uint32_t>(d + c.fn_escala, 0x7FFFu)
                                             : (d > c.fn_escala ? d - c.fn_escala : 0u);
                c.duty = (nd << 4) | (c.duty & 0xFu);
                c.para_up = true;
                if (--c.fn_num == 0) { c.fundiendo = false; raw_ |= 1u << (4 + n); }
            }
            evalua();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_LEDC_ESP_H
