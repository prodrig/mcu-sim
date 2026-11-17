// =============================================================================
// rtc_a.h — El RTC_A de los MSP430 5xx/6xx [UG5 SLAU208Q cap. 15] (M4 de
// doc/analisis_msp430.md)
//
// MODO CALENDARIO (RTCMODE = 1). El calendario, los preescaladores, la
// alarma y el evento son los del RTC_C del MSP432P4 (rtc_c.h); lo que cambia
// está en los registros de arriba:
//
//   +00 RTCCTL01  el byte bajo, RTCCTL0: RDYIFG, AIFG, TEVIFG y sus IE (los
//                 mismos bits que el RTC_C, sin OF); el alto, RTCCTL1: TEV,
//                 SSEL, RDY, MODE, HOLD y BCD, que en el RTC_C son el CTL13.
//                 Sin llave: se escribe directamente. Tras el reset, RTCCTL1
//                 = 0x40 (HOLD, y MODE = 0: modo contador) [UG5 §15.3]
//   +02 RTCCTL23  la calibración (RTCCAL, RTCCALS, RTCCALF): se guarda
//   +08 PS0CTL, +0A PS1CTL, +0C PS, +10 TIM0, +12 TIM1, +14 DATE, +16 YEAR,
//   +18 AMINHR, +1A ADOWDAY: como en el RTC_C
//   +0E RTCIV     2 RDY, 4 TEV, 6 A, 8 RT0PS, 0x0A RT1PS [UG5 §15.3.14]
//
// El calendario cuenta ACLK (32 768 Hz: un segundo); RTCSSEL no vale.
//
// MODO CONTADOR (RTCMODE = 0) [UG5 §15.2.1]: un contador de 32 bits
// (RTCNT1-RTCNT4, los bytes de +10 a +13) y dos preescaladores de 8 bits
// independientes:
//
//   RT0PS  cuenta ACLK o SMCLK (RT0SSEL, bit 14 de RTCPS0CTL) salvo con
//          RT0PSHOLD (bit 8); su salida es la entrada ÷ 2^(RT0PSDIV+1)
//   RT1PS  cuenta ACLK, SMCLK o la salida de RT0PS (RT1SSEL, bits 14-15),
//          salvo con RT1PSHOLD; su salida, ÷ 2^(RT1PSDIV+1)
//   RTCNT  cuenta ACLK, SMCLK o la salida de RT1PS (RTCSSEL), salvo con
//          RTCHOLD
//
// RTCTEVIFG se pone al desbordar 8, 16, 24 o 32 bits del contador (RTCTEV);
// RTxPSIFG, cada 2^(RTxIP+1) cuentas de su preescalador. RTCPS se lee y se
// escribe (RT0PS en el byte bajo, RT1PS en el alto).
//
// LO QUE NO ESTÁ: la calibración, que se guarda y no hace nada.
// =============================================================================
#ifndef STM32_PERIPH_TI_RTC_A_H
#define STM32_PERIPH_TI_RTC_A_H

#include <cmath>
#include <cstdio>
#include <functional>
#include "rtc_c.h"

namespace stm32 {

class RtcA : public RtcCP4 {
public:
    std::function<double()> f_smclk;                 // para el modo contador

    explicit RtcA(sc_core::sc_module_name nm, uint32_t base, uint32_t size = 0x20) : RtcCP4(nm, base, size) {
        SC_HAS_PROCESS(RtcA);
        SC_METHOD(reset_a); sensitive << rst_n;
        SC_THREAD(contador_proc);
        a_valores();
    }
    bool calendario() const { return calendario_; }
    // El contador de 32 bits, como lo vería el programa
    uint32_t contador() { cnt_al_dia(); return uint32_t(entero(c_)); }
    void recalcula_a() { cnt_al_dia(); recalcula(); ev_cnt_.notify(sc_core::SC_ZERO_TIME); }

protected:
    bool calendario_ = false, hold_ = true;
    uint16_t cal_ = 0;
    uint16_t pc0_ = 0, pc1_ = 0;                     // RTCPSxCTL, los bits altos (SSEL, PSDIV, PSHOLD)
    // El modo contador: cuentas (con su parte fraccionaria) hasta t_cnt_
    double c_ = 0.0, p0_ = 0.0, p1_ = 0.0;
    double fc_ = 0.0, f0_ = 0.0, f1_ = 0.0;          // a qué ritmo corren ahora
    sc_core::sc_time t_cnt_;
    sc_core::sc_event ev_cnt_;

    void a_valores() {
        calendario_ = false; hold_ = true; cal_ = 0;
        pc0_ = pc1_ = 0;
        c_ = p0_ = p1_ = 0.0;
        t_cnt_ = sc_core::sc_time_stamp();
        ctl13_ = uint16_t(HOLD | MODE | RDY);
    }
    void reset_a() { if (!rst_n.read()) { a_valores(); ev_cnt_.notify(sc_core::SC_ZERO_TIME); } }

    static uint64_t entero(double x) { return uint64_t(std::floor(x + 1e-6)); }
    double aclk() const { return f_bclk ? f_bclk() : 32768.0; }
    double smclk() const { return f_smclk ? f_smclk() : 1e6; }
    // Los ritmos de la cadena en modo contador
    void ritmos() {
        if (calendario_) { fc_ = f0_ = f1_ = 0.0; return; }
        f0_ = (pc0_ & 0x0100) ? 0.0 : ((pc0_ & 0x4000) ? smclk() : aclk());
        const double out0 = f0_ / double(2u << ((pc0_ >> 11) & 7u));
        double in1 = 0.0;
        switch ((pc1_ >> 14) & 3u) {
            case 0: in1 = aclk(); break;
            case 1: in1 = smclk(); break;
            default: in1 = out0; break;
        }
        f1_ = (pc1_ & 0x0100) ? 0.0 : in1;
        const double out1 = f1_ / double(2u << ((pc1_ >> 11) & 7u));
        double inc = 0.0;
        switch ((ctl13_ >> 2) & 3u) {
            case 0: inc = aclk(); break;
            case 1: inc = smclk(); break;
            default: inc = out1; break;
        }
        fc_ = hold_ ? 0.0 : inc;
    }
    // Lleva las cuentas hasta ahora y marca lo que haya pasado
    void cnt_al_dia() {
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        const double dt = (ahora - t_cnt_).to_seconds();
        t_cnt_ = ahora;
        if (!calendario_ && dt > 0) {
            const uint64_t c0 = entero(c_), a0 = entero(p0_), a1 = entero(p1_);
            c_ += dt * fc_; p0_ += dt * f0_; p1_ += dt * f1_;
            const uint64_t c1 = entero(c_), b0 = entero(p0_), b1 = entero(p1_);
            const unsigned bits = 8u * (((ctl13_ & 3u)) + 1u);
            if ((c1 >> bits) != (c0 >> bits)) { ctl0_ |= TEVIFG; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
            const unsigned ip0 = ((ps0ctl_ >> 2) & 7u) + 1u, ip1 = ((ps1ctl_ >> 2) & 7u) + 1u;
            if ((b0 >> ip0) != (a0 >> ip0)) { ps0ctl_ |= PSIFG; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
            if ((b1 >> ip1) != (a1 >> ip1)) { ps1ctl_ |= PSIFG; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
            // El contador es de 32 bits; los preescaladores, de 8
            if (c_ >= 4294967296.0) c_ -= 4294967296.0 * std::floor(c_ / 4294967296.0);
            if (p0_ >= 256.0) p0_ -= 256.0 * std::floor(p0_ / 256.0);
            if (p1_ >= 256.0) p1_ -= 256.0 * std::floor(p1_ / 256.0);
        }
        ritmos();
    }
    // Lo siguiente que pide interrupción, en segundos (o infinito)
    double proximo_cnt() const {
        double t = 1e30;
        auto hasta = [&](double x, double f, unsigned bits) {
            if (f <= 0) return;
            const uint64_t sig = ((entero(x) >> bits) + 1u) << bits;
            const double falta = double(sig) - x;
            t = std::min(t, falta / f);
        };
        if (ctl0_ & TEVIE) hasta(c_, fc_, 8u * ((ctl13_ & 3u) + 1u));
        if (ps0ctl_ & PSIE) hasta(p0_, f0_, ((ps0ctl_ >> 2) & 7u) + 1u);
        if (ps1ctl_ & PSIE) hasta(p1_, f1_, ((ps1ctl_ >> 2) & 7u) + 1u);
        return t;
    }
    void contador_proc() {
        for (;;) {
            cnt_al_dia();
            const double t = proximo_cnt();
            if (t > 1e29) { wait(ev_cnt_); continue; }
            wait(sc_core::sc_time(t, sc_core::SC_SEC) + sc_core::sc_time(1, sc_core::SC_PS), ev_cnt_);
        }
    }

    uint16_t ctl1() const {
        return uint16_t((ctl13_ & 0x9Fu & ~HOLD) | (calendario_ ? MODE : 0) | (hold_ ? HOLD : 0));
    }
    uint16_t lee16(uint32_t off, bool dbg) override {
        if (!calendario_) {
            if (!dbg) cnt_al_dia();
            switch (off) {
                case 0x08: return uint16_t((pc0_ & 0xFFE0u) | (ps0ctl_ & 0x1Fu));
                case 0x0A: return uint16_t((pc1_ & 0xFFE0u) | (ps1ctl_ & 0x1Fu));
                case 0x0C: return uint16_t((entero(p0_) & 0xFFu) | ((entero(p1_) & 0xFFu) << 8));
                case 0x10: return uint16_t(entero(c_));
                case 0x12: return uint16_t(entero(c_) >> 16);
                case 0x14: case 0x16: case 0x18: case 0x1A: return 0;
                default: break;
            }
        }
        switch (off) {
            case 0x00: if (!dbg) al_dia(); return uint16_t((ctl0_ & 0x77u) | (ctl1() << 8));
            case 0x02: return cal_;
            case 0x08: return uint16_t((pc0_ & 0xFFE0u) | (ps0ctl_ & 0x1Fu));
            case 0x0A: return uint16_t((pc1_ & 0xFFE0u) | (ps1ctl_ & 0x1Fu));
            case 0x04: case 0x06: case 0x1C: case 0x1E: return 0;
            case 0x0E: if (dbg) return 0; al_dia(); if (!calendario_) cnt_al_dia(); return iv_a();
            default: return RtcCP4::lee16(off, dbg);
        }
    }
    uint16_t iv_a() {
        struct { uint16_t* r; uint16_t f, e, v; } t[5] = {
            {&ctl0_, RDYIFG, RDYIE, 0x02}, {&ctl0_, TEVIFG, TEVIE, 0x04}, {&ctl0_, AIFG, AIE, 0x06},
            {&ps0ctl_, PSIFG, PSIE, 0x08}, {&ps1ctl_, PSIFG, PSIE, 0x0A}};
        for (auto& x : t)
            if ((*x.r & x.f) && (*x.r & x.e)) {
                *x.r &= uint16_t(~x.f);
                repulso_ = true;
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return x.v;
            }
        return 0;
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        cnt_al_dia();
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        switch (off) {
            case 0x00: {
                al_dia();
                if (m & 0x00FFu) ctl0_ = uint16_t((ctl0_ & ~0x77u & 0xFFu) | (v & 0x77u));
                if (m & 0xFF00u) {
                    const uint16_t b = uint16_t(v >> 8);
                    calendario_ = (b & MODE) != 0;
                    hold_ = (b & HOLD) != 0;
                    // El RTC_C lleva el calendario; parado si no es calendario
                    ctl13_ = uint16_t((b & 0x8Fu) | MODE | RDY | ((hold_ || !calendario_) ? HOLD : 0));
                    ev_.notify(sc_core::SC_ZERO_TIME);
                }
                ritmos();
                ev_cnt_.notify(sc_core::SC_ZERO_TIME);
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x02: cal_ = uint16_t(((cal_ & ~m) | (v & m)) & 0x03BFu); return;
            case 0x04: case 0x06: case 0x1C: case 0x1E: return;
            case 0x08: case 0x0A: {
                uint16_t& pc = off == 0x08 ? pc0_ : pc1_;
                const uint16_t w = mz(uint16_t((pc & 0xFFE0u) | ((off == 0x08 ? ps0ctl_ : ps1ctl_) & 0x1Fu)));
                pc = uint16_t(w & (off == 0x08 ? 0x7900u : 0xF900u));
                RtcCP4::escribe16(off, uint16_t(w & 0x1Fu), 0xFFFFu);
                ritmos();
                ev_cnt_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            default: break;
        }
        if (!calendario_) {
            switch (off) {
                case 0x0C: {
                    const uint16_t w = mz(uint16_t((entero(p0_) & 0xFFu) | ((entero(p1_) & 0xFFu) << 8)));
                    p0_ = double(w & 0xFFu); p1_ = double(w >> 8);
                    ev_cnt_.notify(sc_core::SC_ZERO_TIME);
                    return;
                }
                case 0x10: case 0x12: {
                    const unsigned sh = off == 0x10 ? 0u : 16u;
                    uint32_t c = uint32_t(entero(c_));
                    const uint16_t w = mz(uint16_t(c >> sh));
                    c = (c & ~(0xFFFFu << sh)) | (uint32_t(w) << sh);
                    c_ = double(c);
                    ev_cnt_.notify(sc_core::SC_ZERO_TIME);
                    return;
                }
                case 0x14: case 0x16: case 0x18: case 0x1A: return;
                default: break;
            }
        }
        RtcCP4::escribe16(off, v, m);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_RTC_A_H
