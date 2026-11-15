// =============================================================================
// systimer_esp.h — El SYSTIMER de Espressif [TRM-C3 §10]
//
// E3 de doc/analisis_esp32_riscv.md, §4. Dos contadores de 52 bits (UNIT0 y
// UNIT1) a 16 MHz (CNT_CLK = XTAL / 2,5) y tres comparadores (COMP0-2), cada
// uno en modo OBJETIVO (una alarma cuando el contador llega a t) o PERIODO
// (alarmas en t1 + n·δt, δt de 26 bits).
//
//   CONF         CLK_EN (31), UNIT0_WORK_EN (30), UNIT1_WORK_EN (29), los de
//                parada con la CPU (28-25: guardados, sin efecto: no hay
//                depuración que pare el contador todavía) y TARGETx_WORK_EN
//                (24-22)
//   UNITn_OP     UPDATE (30) copia el contador a VALUE_HI/LO y pone
//                VALUE_VALID (29)
//   UNITn_LOAD   copia LOAD_HI/LO al contador
//   TARGETx_*    el objetivo; TARGETx_CONF: PERIOD (25:0), PERIOD_MODE (30),
//                TIMER_UNIT_SEL (31). COMPx_LOAD pasa al comparador el
//                objetivo y el periodo; el modo y la unidad mandan en cuanto
//                se escriben (ESP-IDF elige el modo periodo DESPUÉS del
//                COMPx_LOAD: systimer_hal_select_alarm_mode)
//   INT_*        una alarma por comparador, de NIVEL hasta que se borra; van a
//                las fuentes 37, 38 y 39 de la matriz del C3
//
// El contador se calcula del tiempo simulado: no hay un proceso por tic.
// Un objetivo que ya ha pasado da la alarma en el acto [TRM-C3 §10.4.2].
// =============================================================================
#ifndef STM32_PERIPH_ESP_SYSTIMER_ESP_H
#define STM32_PERIPH_ESP_SYSTIMER_ESP_H

#include <systemc>
#include <cmath>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class SystimerEsp : public BusSlave {
public:
    std::function<void(unsigned comp, bool nivel)> irq;
    double hz_cnt = 16e6;

    SystimerEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(SystimerEsp);
        for (unsigned i = 0; i < 3; ++i)
            sc_core::sc_spawn(sc_core::sc_bind(&SystimerEsp::comp_proc, this, i));
        reset_regs();
    }

    void reset() { reset_regs(); publica(); cambia(); }
    static constexpr uint64_t M52 = (uint64_t(1) << 52) - 1;
    uint64_t valor(unsigned u) const {
        const Unidad& x = u_[u & 1];
        if (!trabaja(u)) return x.base;
        const double dt = (sc_core::sc_time_stamp() - x.t).to_seconds();
        return (x.base + uint64_t(std::floor(dt * hz_cnt + 1e-6))) & M52;
    }
    uint32_t int_raw() const { return raw_; }
    uint64_t alarmas(unsigned c) const { return c < 3 ? n_alarmas_[c] : 0; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return conf_;
        case 0x04: return u_[0].op;
        case 0x08: return u_[1].op;
        case 0x0C: return uint32_t(u_[0].load >> 32);
        case 0x10: return uint32_t(u_[0].load);
        case 0x14: return uint32_t(u_[1].load >> 32);
        case 0x18: return uint32_t(u_[1].load);
        case 0x1C: case 0x24: case 0x2C: return uint32_t(c_[(off - 0x1C) / 8].target >> 32);
        case 0x20: case 0x28: case 0x30: return uint32_t(c_[(off - 0x20) / 8].target);
        case 0x34: case 0x38: case 0x3C: return c_[(off - 0x34) / 4].conf;
        case 0x40: return uint32_t(u_[0].snap >> 32);
        case 0x44: return uint32_t(u_[0].snap);
        case 0x48: return uint32_t(u_[1].snap >> 32);
        case 0x4C: return uint32_t(u_[1].snap);
        case 0x64: return ena_;
        case 0x68: return raw_;
        case 0x70: return raw_ & ena_;
        case 0xFC: return 0x2006171u;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        v &= m;
        auto hi = [&](uint64_t& r) { r = (r & 0xFFFFFFFFull) | (uint64_t((uint32_t(r >> 32) & ~m) | v) & 0xFFFFFu) << 32; };
        auto lo = [&](uint64_t& r) { r = (r & ~0xFFFFFFFFull) | ((uint32_t(r) & ~m) | v); };
        switch (off) {
        case 0x00: {
            // Al parar o arrancar un contador, se fija su valor
            const uint64_t v0 = valor(0), v1 = valor(1);
            conf_ = (conf_ & ~m) | v;
            u_[0].base = v0; u_[0].t = sc_core::sc_time_stamp();
            u_[1].base = v1; u_[1].t = sc_core::sc_time_stamp();
            cambia();
            return;
        }
        case 0x04: case 0x08: {
            Unidad& x = u_[(off - 4) / 4];
            if (v & (1u << 30)) { x.snap = valor((off - 4) / 4); x.op |= 1u << 29; }
            return;
        }
        case 0x0C: hi(u_[0].load); return;
        case 0x10: lo(u_[0].load); return;
        case 0x14: hi(u_[1].load); return;
        case 0x18: lo(u_[1].load); return;
        case 0x1C: case 0x24: case 0x2C: hi(c_[(off - 0x1C) / 8].target_reg); return;
        case 0x20: case 0x28: case 0x30: lo(c_[(off - 0x20) / 8].target_reg); return;
        case 0x34: case 0x38: case 0x3C: {
            Comp& c = c_[(off - 0x34) / 4];
            const bool antes = (c.conf_reg >> 30) & 1u;
            c.conf_reg = (c.conf_reg & ~m) | v;
            // Al pasar a periodo con la alarma ya dada, se rearma desde ella
            if (!antes && ((c.conf_reg >> 30) & 1u) && !c.armada && (c.conf & 0x3FFFFFFu)) {
                c.proxima = (c.proxima + (c.conf & 0x3FFFFFFu)) & M52;
                c.armada = true;
            }
            cambia();
            return;
        }
        case 0x50: case 0x54: case 0x58:
            if (v & 1u) carga_comp((off - 0x50) / 4);
            return;
        case 0x5C: case 0x60:
            if (v & 1u) {
                Unidad& x = u_[(off - 0x5C) / 4];
                x.base = x.load & M52; x.t = sc_core::sc_time_stamp();
                cambia();
            }
            return;
        case 0x64: ena_ = (ena_ & ~m) | (v & 7u); publica(); return;
        case 0x6C: raw_ &= ~(v & 7u); publica(); return;
        default: return;
        }
    }

private:
    struct Unidad { uint64_t base = 0, load = 0, snap = 0; sc_core::sc_time t; uint32_t op = 0; };
    struct Comp {
        uint64_t target_reg = 0, target = 0;
        uint32_t conf_reg = 0, conf = 0;
        uint64_t proxima = 0;      // la siguiente alarma, en cuentas
        bool armada = false;
    };
    Unidad u_[2];
    Comp c_[3];
    uint32_t conf_ = 0, ena_ = 0, raw_ = 0, irq_ = 0;
    uint64_t n_alarmas_[3] = {};
    sc_core::sc_event ev_cambio_;

    void reset_regs() {
        conf_ = 0x46000000u;
        ena_ = raw_ = 0;
        for (Unidad& x : u_) x = Unidad{};
        for (Comp& c : c_) c = Comp{};
    }
    bool trabaja(unsigned u) const {
        return (conf_ >> (30 - (u & 1))) & 1u;
    }
    bool comp_en(unsigned c) const { return (conf_ >> (24 - c)) & 1u; }
    unsigned unidad_de(unsigned c) const { return (c_[c].conf_reg >> 31) & 1u; }
    bool periodo(unsigned c) const { return (c_[c].conf_reg >> 30) & 1u; }

    void carga_comp(unsigned i) {
        Comp& c = c_[i];
        c.target = c.target_reg & M52;
        c.conf = c.conf_reg;
        const bool per = periodo(i);
        const uint64_t p = c.conf & 0x3FFFFFFu;
        c.proxima = per ? ((valor(unidad_de(i)) + p) & M52) : c.target;
        c.armada = !per || p != 0;
        cambia();
    }
    void cambia() { ev_cambio_.notify(sc_core::SC_ZERO_TIME); }
    void publica() {
        const uint32_t n = raw_ & ena_;
        for (unsigned i = 0; i < 3; ++i) {
            const bool a = (n >> i) & 1u, b = (irq_ >> i) & 1u;
            if (a != b && irq) irq(i, a);
        }
        irq_ = n;
    }

    void comp_proc(unsigned i) {
        for (;;) {
            Comp& c = c_[i];
            const unsigned u = unidad_de(i);
            if (!c.armada || !comp_en(i) || !trabaja(u)) { wait(ev_cambio_); continue; }
            const uint64_t ahora = valor(u);
            // ¿Ha pasado ya? (dentro de media vuelta de 52 bits)
            const uint64_t d = (ahora - c.proxima) & M52;
            if (d < (uint64_t(1) << 51)) {
                raw_ |= 1u << i;
                ++n_alarmas_[i];
                publica();
                if (periodo(i)) {
                    const uint64_t p = c.conf & 0x3FFFFFFu;
                    c.proxima = (c.proxima + p) & M52;
                } else {
                    c.armada = false;
                }
                continue;
            }
            const uint64_t falta = (c.proxima - ahora) & M52;
            const double s = double(falta) / hz_cnt;
            wait(sc_core::sc_time(s, sc_core::SC_SEC) + sc_core::sc_time(1, sc_core::SC_NS), ev_cambio_);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_SYSTIMER_ESP_H
