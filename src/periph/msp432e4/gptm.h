// =============================================================================
// gptm.h — Los temporizadores de propósito general del MSP432E4 (T6, entrega 3)
//
// TIMER0..7, de 16/32 bits: cada uno con dos mitades, A y B, que van juntas
// como uno de 32 bits (CFG = 0) o por separado como dos de 16 (CFG = 4) [H;
// hw_timer.h; segunda fuente: timer.c de DriverLib, TimerConfigure y
// compañía]. Lo que hace cada mitad lo dice TnMR:
//
//   TnMR = 1/2  una vez / periódico: cuenta de ILR a 0 (o de 0 a ILR con
//               TnCDIR) y vence (TnTORIS); con TnMIE, avisa al pasar por MATCHR
//               (TnMRIS). En 16 bits, PR es un PREDIVISOR de verdad al contar
//               hacia abajo: un paso cada PR+1 ciclos
//   TnAMS = 1   PWM: cuenta hacia abajo desde PR:ILR (24 bits); la salida CCP
//               sube al recargar y baja al pasar por PMR:MATCHR; TnPWML la
//               invierte
//   TnMR = 3    captura. Con TnCMR = 1, de TIEMPO: en cada flanco de CCP (los
//               que diga TnEVENT) guarda la cuenta en TnR y avisa (CnERIS).
//               Con TnCMR = 0, de FLANCOS: cada flanco resta uno a la cuenta y,
//               al llegar a MATCHR, avisa (CnMRIS), recarga y se para
//
// El reloj es SYSCLK, o ALTCLK con CC.ALTCLK. Con TnOTE y ADCEV, el
// vencimiento dispara el ADC (el SoC lo engancha). Escribir ILR con la mitad
// en marcha recarga la cuenta, salvo con TnILD, que espera al vencimiento.
//
// LO QUE NO ESTÁ: el modo RTC (CFG = 1), las interrupciones de flanco del PWM
// (TnPWMIE), TnWOT/SYNC, TnSNAPS, TnMRSU, las acciones de comparación (TCACT)
// y STALL con el depurador; sus bits se guardan y no hacen nada [P-17 i].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_GPTM_H
#define STM32_PERIPH_MSP432E4_GPTM_H

#include <cmath>
#include <functional>
#include "../../common/periph_base.h"
#include "../../common/guarda_tick.h"
#include "../../soc/msp432e4/e4_desc.h"
#include "gpio.h"

namespace stm32 {

class GptmE4 : public BusSlave {
public:
    enum : uint32_t {
        R_CFG = 0x00, R_TAMR = 0x04, R_TBMR = 0x08, R_CTL = 0x0C, R_SYNC = 0x10,
        R_IMR = 0x18, R_RIS = 0x1C, R_MIS = 0x20, R_ICR = 0x24, R_TAILR = 0x28,
        R_TBILR = 0x2C, R_TAMATCHR = 0x30, R_TBMATCHR = 0x34, R_TAPR = 0x38,
        R_TBPR = 0x3C, R_TAPMR = 0x40, R_TBPMR = 0x44, R_TAR = 0x48, R_TBR = 0x4C,
        R_TAV = 0x50, R_TBV = 0x54, R_TAPS = 0x5C, R_TBPS = 0x60, R_DMAEV = 0x6C,
        R_ADCEV = 0x70, R_PP = 0xFC0, R_CC = 0xFC8
    };
    // Los bits de RIS de la mitad A; los de B, 8 más arriba (salvo RTC)
    static constexpr uint32_t TO = 1u << 0, CM = 1u << 1, CE = 1u << 2, MR = 1u << 4;

    sc_core::sc_out<bool> irq_a{"irq_a"}, irq_b{"irq_b"};
    // CCP0 (A) y CCP1 (B): la salida del PWM y la entrada de captura
    sc_core::sc_signal<bool> ccp_out[2], ccp_in[2], oe[2];
    // El disparo del ADC: un pulso por vencimiento con TnOTE y ADCEV
    std::function<void()> dispara_adc;
    std::function<double(bool alt)> f_reloj;

    GptmE4(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(GptmE4);
        SC_THREAD(mitad_a);
        SC_THREAD(mitad_b);
        SC_METHOD(captura_a); sensitive << ccp_in[0]; dont_initialize();
        SC_METHOD(captura_b); sensitive << ccp_in[1]; dont_initialize();
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    FuncionE4 fn_ccp(unsigned m) {
        FuncionE4 f; f.out = &ccp_out[m & 1u]; f.oe = &oe[m & 1u]; f.in = &ccp_in[m & 1u];
        f.idle_in = false; f.nombre = m ? "TnCCP1" : "TnCCP0";
        return f;
    }
    void recalcula() { for (auto& h : h_) h.ev.notify(sc_core::SC_ZERO_TIME); }
    unsigned vencimientos(unsigned m) const { return h_[m & 1u].n_to; }

protected:
    struct Mitad {
        uint32_t mr = 0, ilr = 0xFFFFFFFFu, match = 0xFFFFFFFFu, pr = 0, pmr = 0;
        bool corre = false;
        double cnt0 = 0;                 // la cuenta en t_ref
        sc_core::sc_time t_ref;
        double tick_s = 0;               // lo que dura un paso
        uint32_t capturado = 0;
        bool ild_pend = false;
        uint32_t ilr_pend = 0;
        unsigned n_to = 0;
        bool o = false;                  // la salida del PWM
        sc_core::sc_event ev;
    };
    uint32_t cfg_ = 0, ctl_ = 0, imr_ = 0, ris_ = 0, cc_ = 0, adcev_ = 0, dmaev_ = 0, sync_ = 0;
    Mitad h_[2];
    sc_core::sc_event pub_ev_;

    bool de32() const { return cfg_ == 0; }
    // Ancho del contador y de la carga, según el modo
    unsigned ancho(unsigned m) const {
        if (de32() && m == 0) return 32;
        const Mitad& h = h_[m];
        const bool pwm = (h.mr & 8u) != 0;
        const bool cap = (h.mr & 3u) == 3u;
        return (pwm || cap || (h.mr & 0x10u)) ? 24u : 16u;
    }
    uint32_t carga(unsigned m) const {
        const Mitad& h = h_[m];
        if (ancho(m) == 32) return h.ilr;
        if (ancho(m) == 24) return ((h.pr & 0xFFu) << 16) | (h.ilr & 0xFFFFu);
        return h.ilr & 0xFFFFu;
    }
    uint32_t casa(unsigned m) const {
        const Mitad& h = h_[m];
        if (ancho(m) == 32) return h.match;
        if (ancho(m) == 24) return ((h.pmr & 0xFFu) << 16) | (h.match & 0xFFFFu);
        return h.match & 0xFFFFu;
    }
    bool sube(unsigned m) const { return (h_[m].mr & 0x10u) != 0; }
    bool habilitada(unsigned m) const { return ctl_ & (m ? (1u << 8) : 1u); }
    bool modo_tiempo(unsigned m) const {           // una vez, periódico o PWM
        const unsigned t = h_[m].mr & 3u;
        return t == 1 || t == 2;
    }
    bool pwm(unsigned m) const { return (h_[m].mr & 8u) && (h_[m].mr & 3u) == 2u; }
    double f() const { return f_reloj ? f_reloj((cc_ & 1u) != 0) : 0.0; }
    // Lo que dura un paso: con predivisor en 16 bits contando hacia abajo
    double tick(unsigned m) const {
        const double fr = f();
        if (fr <= 0) return 0;
        const unsigned div = (ancho(m) == 16 && !sube(m)) ? (h_[m].pr & 0xFFu) + 1u : 1u;
        return double(div) / fr;
    }
    // Pasos dados desde t_ref
    double pasos(unsigned m) const {
        const Mitad& h = h_[m];
        if (!h.corre || h.tick_s <= 0) return 0;
        return std::floor((sc_core::sc_time_stamp() - h.t_ref).to_seconds() / h.tick_s + 1e-9);
    }
    // La cuenta ahora (modos de tiempo)
    uint32_t cuenta(unsigned m) const {
        const Mitad& h = h_[m];
        if (!h.corre || !modo_tiempo(m)) return uint32_t(h.cnt0);
        const double per = double(carga(m)) + 1.0;
        const double k = std::fmod(pasos(m), per);
        if (sube(m)) return uint32_t(std::fmod(h.cnt0 + k, per));
        double v = h.cnt0 - k;
        if (v < 0) v += per;
        return uint32_t(v);
    }
    void ancla(unsigned m) {
        Mitad& h = h_[m];
        h.cnt0 = cuenta(m);
        h.t_ref = sc_core::sc_time_stamp();
        h.tick_s = tick(m);
    }
    void arranca(unsigned m) {
        Mitad& h = h_[m];
        h.corre = true;
        h.t_ref = sc_core::sc_time_stamp();
        h.tick_s = tick(m);
        h.o = !(ctl_ & (m ? (1u << 14) : (1u << 6)));       // PWM: alto (o bajo con PWML)
        h.ev.notify(sc_core::SC_ZERO_TIME);
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void para(unsigned m) {
        ancla(m);
        h_[m].corre = false;
        h_[m].ev.notify(sc_core::SC_ZERO_TIME);
    }

    void valores_de_reset() {
        cfg_ = ctl_ = imr_ = ris_ = cc_ = adcev_ = dmaev_ = sync_ = 0;
        for (Mitad& h : h_) {
            h.mr = 0; h.ilr = 0xFFFFFFFFu; h.match = 0xFFFFFFFFu; h.pr = h.pmr = 0;
            h.corre = false; h.cnt0 = double(0xFFFFFFFFu); h.capturado = 0; h.ild_pend = false;
            h.o = false;
        }
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        recalcula();
    }
    void pub_proc() {
        irq_a.write((ris_ & imr_ & 0x00FFu) != 0);
        irq_b.write((ris_ & imr_ & 0xFF00u) != 0);
        for (unsigned m = 0; m < 2; ++m) {
            const bool sal = pwm(m) && h_[m].corre;
            const bool inv = ctl_ & (m ? (1u << 14) : (1u << 6));
            const bool v = sal ? h_[m].o : inv;
            if (ccp_out[m].read() != v) ccp_out[m].write(v);
            const bool e = pwm(m);
            if (oe[m].read() != e) oe[m].write(e);
        }
    }
    void marca(uint32_t b) { ris_ |= b; pub_ev_.notify(sc_core::SC_ZERO_TIME); }

    // El vencimiento: recarga, dispara, y una vez se para
    void vence(unsigned m) {
        Mitad& h = h_[m];
        ++h.n_to;
        if (h.ild_pend) { h.ilr = h.ilr_pend; h.ild_pend = false; }
        if (!pwm(m)) marca(m ? (TO << 8) : TO);
        if ((ctl_ & (m ? (1u << 13) : (1u << 5))) && (adcev_ & (m ? (1u << 8) : 1u)) && dispara_adc)
            dispara_adc();
        if ((h.mr & 3u) == 1u) {                              // una vez
            ctl_ &= ~(m ? (1u << 8) : 1u);
            h.corre = false;
            h.cnt0 = sube(m) ? double(carga(m)) : 0.0;
            return;
        }
        h.cnt0 = sube(m) ? 0.0 : double(carga(m));
        h.t_ref = sc_core::sc_time_stamp();
        if (pwm(m)) {                                         // recarga: la salida sube
            h.o = !(ctl_ & (m ? (1u << 14) : (1u << 6)));
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void mitad(unsigned m) {
        Mitad& h = h_[m];
        for (;;) {
            if (!h.corre || !modo_tiempo(m) || h.tick_s <= 0) { wait(h.ev); continue; }
            const double per = double(carga(m)) + 1.0;
            const double k = pasos(m);
            // Pasos hasta vencer, y hasta pasar por MATCHR
            double a_venc = sube(m) ? double(carga(m)) - h.cnt0 : h.cnt0;
            a_venc += 1.0;                                    // el vencimiento recarga en el paso siguiente
            a_venc -= k;
            double a_casa = -1;
            const bool mie = (h.mr & 0x20u) || pwm(m);
            if (mie) {
                const double c = double(casa(m));
                double d = sube(m) ? c - h.cnt0 : h.cnt0 - c;
                d -= k;
                while (d < 0) d += per;
                if (d > 0 || (d == 0 && k == 0)) a_casa = d;
            }
            double prox = a_venc;
            bool es_casa = false;
            if (a_casa >= 0 && a_casa < a_venc) { prox = a_casa; es_casa = true; }
            if (prox > 0) {
                const sc_core::sc_time dt(prox * h.tick_s, sc_core::SC_SEC);
                if (dt > sc_core::SC_ZERO_TIME) {
                    wait(dt, h.ev);
                    if (h.ev.triggered()) continue;
                }
            }
            if (es_casa) {
                if (pwm(m)) {
                    h.o = (ctl_ & (m ? (1u << 14) : (1u << 6))) != 0;
                    pub_ev_.notify(sc_core::SC_ZERO_TIME);
                } else {
                    marca(m ? (MR << 8) : MR);
                }
                // No repetir la misma casa: un paso más
                wait(sc_core::sc_time(h.tick_s, sc_core::SC_SEC), h.ev);
                continue;
            }
            vence(m);
        }
    }
    void mitad_a() { mitad(0); }
    void mitad_b() { mitad(1); }

    // Captura: de tiempo (guarda la cuenta) o de flancos (cuenta y casa)
    void captura(unsigned m) {
        Mitad& h = h_[m];
        if (!habilitada(m) || (h.mr & 3u) != 3u) return;
        const unsigned ev = (ctl_ >> (m ? 10 : 2)) & 3u;
        const bool v = ccp_in[m].read();
        const bool vale = ev == 3 ? true : ev == 1 ? !v : v;
        if (!vale) return;
        if (h.mr & 4u) {                                      // de tiempo
            const double fr = f();
            const double per = double(carga(m)) + 1.0;
            const double k = fr > 0 ? std::floor((sc_core::sc_time_stamp() - h.t_ref).to_seconds() * fr + 1e-9) : 0;
            double c = sube(m) ? std::fmod(k, per) : double(carga(m)) - std::fmod(k, per);
            h.capturado = uint32_t(c);
            marca(m ? (CE << 8) : CE);
        } else {                                              // de flancos
            const double c = sube(m) ? h.cnt0 + 1 : h.cnt0 - 1;
            h.cnt0 = c;
            if (uint32_t(c) == casa(m)) {
                marca(m ? (CM << 8) : CM);
                h.cnt0 = sube(m) ? 0.0 : double(carga(m));
                ctl_ &= ~(m ? (1u << 8) : 1u);
            }
        }
    }
    void captura_a() { captura(0); }
    void captura_b() { captura(1); }

    uint32_t reg_read(uint32_t off) override {
        auto valor = [&](unsigned m) -> uint32_t {
            const Mitad& h = h_[m];
            if ((h.mr & 3u) == 3u) return (h.mr & 4u) ? h.capturado : uint32_t(h.cnt0);
            return cuenta(m);
        };
        switch (off) {
            case R_CFG: return cfg_;
            case R_TAMR: return h_[0].mr;
            case R_TBMR: return h_[1].mr;
            case R_CTL: return ctl_;
            case R_SYNC: return sync_;
            case R_IMR: return imr_;
            case R_RIS: return ris_;
            case R_MIS: return ris_ & imr_;
            case R_TAILR: return de32() ? h_[0].ilr : (h_[0].ilr & 0xFFFFu);
            case R_TBILR: return h_[1].ilr & 0xFFFFu;
            case R_TAMATCHR: return de32() ? h_[0].match : (h_[0].match & 0xFFFFu);
            case R_TBMATCHR: return h_[1].match & 0xFFFFu;
            case R_TAPR: return h_[0].pr;
            case R_TBPR: return h_[1].pr;
            case R_TAPMR: return h_[0].pmr;
            case R_TBPMR: return h_[1].pmr;
            case R_TAR: case R_TAV: return valor(0);
            case R_TBR: case R_TBV: return valor(1);
            case R_DMAEV: return dmaev_;
            case R_ADCEV: return adcev_;
            case R_PP: return 0x00000000u;               // 16/32 bits, sin ancho
            case R_CC: return cc_;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        const uint32_t ctl_viejo = ctl_;
        switch (off) {
            case R_CFG: cfg_ = v & 7u; break;
            case R_TAMR: h_[0].mr = v & 0xFFFFu; break;
            case R_TBMR: h_[1].mr = v & 0xFFFFu; break;
            case R_CTL: {
                for (unsigned m = 0; m < 2; ++m) if (h_[m].corre) ancla(m);
                ctl_ = v & 0x6F7Fu;
                for (unsigned m = 0; m < 2; ++m) {
                    const uint32_t en = m ? (1u << 8) : 1u;
                    if ((ctl_ & en) && !(ctl_viejo & en)) {
                        if ((h_[m].mr & 3u) == 3u && !(h_[m].mr & 4u))
                            h_[m].cnt0 = sube(m) ? 0.0 : double(carga(m));
                        else if (!h_[m].corre && modo_tiempo(m) &&
                                 (h_[m].cnt0 > double(carga(m)) || h_[m].cnt0 == 0 || sube(m)))
                            h_[m].cnt0 = sube(m) ? 0.0 : double(carga(m));
                        arranca(m);
                    } else if (!(ctl_ & en) && (ctl_viejo & en)) {
                        para(m);
                    }
                }
                pub_ev_.notify(sc_core::SC_ZERO_TIME);
                break;
            }
            case R_SYNC: sync_ = v; break;
            case R_IMR: imr_ = v & 0x2F3Fu; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_ICR: ris_ &= ~v; pub_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case R_TAILR: case R_TBILR: {
                const unsigned m = off == R_TBILR ? 1u : 0u;
                Mitad& h = h_[m];
                const uint32_t nv = (de32() && m == 0) ? v : (v & 0xFFFFu);
                if (h.corre && (h.mr & 0x100u)) { h.ild_pend = true; h.ilr_pend = nv; break; }
                h.ilr = nv;
                // Escribirla recarga la cuenta (contando hacia abajo)
                if (!sube(m)) { h.cnt0 = double(carga(m)); h.t_ref = sc_core::sc_time_stamp(); }
                h.ev.notify(sc_core::SC_ZERO_TIME);
                break;
            }
            case R_TAMATCHR: h_[0].match = de32() ? v : (v & 0xFFFFu); h_[0].ev.notify(sc_core::SC_ZERO_TIME); break;
            case R_TBMATCHR: h_[1].match = v & 0xFFFFu; h_[1].ev.notify(sc_core::SC_ZERO_TIME); break;
            case R_TAPR: ancla(0); h_[0].pr = v & 0xFFu; h_[0].tick_s = tick(0); h_[0].ev.notify(sc_core::SC_ZERO_TIME); break;
            case R_TBPR: ancla(1); h_[1].pr = v & 0xFFu; h_[1].tick_s = tick(1); h_[1].ev.notify(sc_core::SC_ZERO_TIME); break;
            case R_TAPMR: h_[0].pmr = v & 0xFFu; break;
            case R_TBPMR: h_[1].pmr = v & 0xFFu; break;
            case R_TAV: h_[0].cnt0 = double(v); h_[0].t_ref = sc_core::sc_time_stamp(); h_[0].ev.notify(sc_core::SC_ZERO_TIME); break;
            case R_TBV: h_[1].cnt0 = double(v & 0xFFFFFFu); h_[1].t_ref = sc_core::sc_time_stamp(); h_[1].ev.notify(sc_core::SC_ZERO_TIME); break;
            case R_DMAEV: dmaev_ = v; break;
            case R_ADCEV: adcev_ = v; break;
            case R_CC: for (unsigned m = 0; m < 2; ++m) ancla(m); cc_ = v & 1u;
                       { for (unsigned m = 0; m < 2; ++m) h_[m].tick_s = tick(m); } recalcula(); break;
            default: break;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_GPTM_H
