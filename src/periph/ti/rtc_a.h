// =============================================================================
// rtc_a.h — El RTC_A de los MSP430 5xx/6xx, en modo calendario [UG5 SLAU208Q
// cap. 15] (M4 de doc/analisis_msp430.md)
//
// El calendario, los preescaladores, la alarma y el evento son los del RTC_C
// del MSP432P4 (rtc_c.h); lo que cambia está en los registros de arriba:
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
// El calendario cuenta ACLK (32 768 Hz: un segundo); RTCSSEL solo vale en
// modo contador.
//
// LO QUE NO ESTÁ: el modo contador (RTCMODE = 0, RTCNT1-4): con él el reloj
// no corre y lo dice una vez; y la calibración, que se guarda y no hace nada.
// =============================================================================
#ifndef STM32_PERIPH_TI_RTC_A_H
#define STM32_PERIPH_TI_RTC_A_H

#include <cstdio>
#include "rtc_c.h"

namespace stm32 {

class RtcA : public RtcCP4 {
public:
    explicit RtcA(sc_core::sc_module_name nm, uint32_t base, uint32_t size = 0x20) : RtcCP4(nm, base, size) {
        SC_HAS_PROCESS(RtcA);
        SC_METHOD(reset_a); sensitive << rst_n;
        a_valores();
    }
    bool calendario() const { return calendario_; }

protected:
    bool calendario_ = false, hold_ = true, avisado_ = false;
    uint16_t cal_ = 0;

    void a_valores() {
        calendario_ = false; hold_ = true; cal_ = 0;
        ctl13_ = uint16_t(HOLD | MODE | RDY);
    }
    void reset_a() { if (!rst_n.read()) a_valores(); }

    uint16_t ctl1() const {
        return uint16_t((ctl13_ & 0x9Fu & ~HOLD) | (calendario_ ? MODE : 0) | (hold_ ? HOLD : 0));
    }
    uint16_t lee16(uint32_t off, bool dbg) override {
        switch (off) {
            case 0x00: if (!dbg) al_dia(); return uint16_t((ctl0_ & 0x77u) | (ctl1() << 8));
            case 0x02: return cal_;
            case 0x04: case 0x06: case 0x1C: case 0x1E: return 0;
            case 0x0E: if (dbg) return 0; al_dia(); return iv_a();
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
        switch (off) {
            case 0x00: {
                al_dia();
                if (m & 0x00FFu) ctl0_ = uint16_t((ctl0_ & ~0x77u & 0xFFu) | (v & 0x77u));
                if (m & 0xFF00u) {
                    const uint16_t b = uint16_t(v >> 8);
                    calendario_ = (b & MODE) != 0;
                    hold_ = (b & HOLD) != 0;
                    if (!calendario_ && !hold_ && !avisado_) {
                        avisado_ = true;
                        std::fprintf(stderr, "[rtc_a] el modo contador (RTCMODE = 0) no se modela: el reloj no corre\n");
                    }
                    // El RTC_C lleva el calendario; parado si no es calendario
                    ctl13_ = uint16_t((b & 0x8Fu) | MODE | RDY | ((hold_ || !calendario_) ? HOLD : 0));
                    ev_.notify(sc_core::SC_ZERO_TIME);
                }
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
            case 0x02: cal_ = uint16_t(((cal_ & ~m) | (v & m)) & 0x03BFu); return;
            case 0x04: case 0x06: case 0x1C: case 0x1E: return;
            default: RtcCP4::escribe16(off, v, m); return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_RTC_A_H
