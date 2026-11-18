// =============================================================================
// adc12_b.h — El ADC12_B de las FR5xx/FR6xx [UG FR5 SLAU367P cap. 34] (M8 de
// doc/analisis_msp430.md)
//
//   +00 ADC12CTL0  SC ENC ON MSC SHT0 SHT1
//   +02 ADC12CTL1  BUSY CONSEQ SSEL DIV ISSH SHP SHS PDIV
//   +04 ADC12CTL2  PWRMD DF RES (8, 10, 12 bits)
//   +06 ADC12CTL3  CSTARTADD BATMAP TCMAP ICH0MAP..ICH3MAP
//   +08 ADC12LO, +0A ADC12HI   la ventana
//   +0C/+0E/+10 ADC12IFGR0-2, +12/+14/+16 ADC12IER0-2, +18 ADC12IV
//   +20..+5E ADC12MCTLx  INCH (5 bits) EOS VRSEL DIF WINC
//   +60..+9E ADC12MEMx
//
// Treinta y dos memorias; las cuatro secuencias como en el ADC12_A (de
// CSTARTADD hasta EOS, o la 31), MSC, SHP con SHT0 (MEM0-7 y 24-31) y SHT1
// (MEM8-23) [§34.2.5], el formato con signo (DF), la ventana (WINC: LO, HI e
// IN) y el diferencial (DIF: el par de canales 2n/2n+1) [de modelo: un
// diferencial da (V+ - V-) / (VR+ - VR-) centrado en la mitad]. Leer MEMx
// borra su IFG; el IV da OV, TOV, HI, LO, IN, IFG0..31 y RDY por orden
// [tabla 34-18].
//
// LOS CANALES: A0-A29 en los pines (`v_canal`), A30 el sensor de
// temperatura con TCMAP y A31 AVCC/2 con BATMAP (los da el SoC por
// `v_canal(30/31)`). VR+ y VR- según VRSEL [tabla 34-16]: AVCC, AVSS, la
// referencia (`v_vref`: la de REF_A), VeREF+ y VeREF- (`v_eref`).
//
// LOS DISPAROS: SHS = 0, el bit SC; los demás, del SoC (`dispara(shs)`, por
// flanco), con su tabla de la hoja de datos.
// =============================================================================
#ifndef STM32_PERIPH_TI_ADC12_B_H
#define STM32_PERIPH_TI_ADC12_B_H

#include <systemc>
#include <cmath>
#include <cstdint>
#include <functional>

namespace stm32 {

SC_MODULE(Adc12B) {
    std::function<double(unsigned)> v_canal;    // la tensión del canal (0-31)
    std::function<double()> v_avcc;
    std::function<double()> v_vref;             // la referencia de REF_A (0: apagada)
    std::function<double(bool)> v_eref;         // VeREF+ (true) y VeREF- (false)
    std::function<double(unsigned)> f_reloj;    // ADC12SSEL -> Hz (0: MODOSC)
    std::function<void()> al_cambiar;           // IFG/IE: que el SoC arbitre
    std::function<void(unsigned)> al_convertir; // el IFGx que se ha puesto (DMA)
    std::function<void(bool)> pide_ref;         // la petición de referencia (REF_A)

    SC_CTOR(Adc12B) {
        SC_THREAD(conv_proc);
        reset();
    }
    void reset() {
        ctl0_ = ctl1_ = ctl3_ = 0; ctl2_ = 0x0020; lo_ = 0; hi_ = 0x0FFF;
        ifg_ = ie_ = 0; ifg2_ = ie2_ = 0;
        for (unsigned i = 0; i < 32; ++i) { mctl_[i] = 0; mem_[i] = 0; }
        busy_ = false;
        if (pide_ref) pide_ref(false);
    }

    uint16_t lee(uint32_t off, bool dbg) {
        off &= ~1u;
        if (off >= 0x20 && off < 0x60) return mctl_[(off - 0x20) / 2];
        if (off >= 0x60 && off < 0xA0) {
            const unsigned i = (off - 0x60) / 2;
            if (!dbg && (ifg_ & (1u << i))) { ifg_ &= ~(1u << i); avisa(); }
            return mem_[i];
        }
        switch (off) {
        case 0x00: return ctl0_;
        case 0x02: return uint16_t(ctl1_ | (busy_ ? 1 : 0));
        case 0x04: return ctl2_;
        case 0x06: return ctl3_;
        case 0x08: return lo_;
        case 0x0A: return hi_;
        case 0x0C: return uint16_t(ifg_);
        case 0x0E: return uint16_t(ifg_ >> 16);
        case 0x10: return ifg2_;
        case 0x12: return uint16_t(ie_);
        case 0x14: return uint16_t(ie_ >> 16);
        case 0x16: return ie2_;
        case 0x18: return iv(dbg);
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        off &= ~1u;
        auto mz = [&](uint16_t r) { return uint16_t((r & ~m) | (v & m)); };
        const bool enc = (ctl0_ & ENC) != 0;
        if (off >= 0x20 && off < 0x60) { if (!enc) mctl_[(off - 0x20) / 2] = uint16_t(mz(mctl_[(off - 0x20) / 2]) & 0x6F9Fu); return; }
        if (off >= 0x60 && off < 0xA0) return;
        switch (off) {
        case 0x00: {
            uint16_t n = mz(ctl0_);
            // Con ENC puesto, solo cambian SC y ENC [§34.2.7]
            if (enc) n = uint16_t((ctl0_ & ~0x0003u) | (n & 0x0003u));
            const bool sc = (n & SC) && (n & ENC) && (n & ON);
            ctl0_ = uint16_t(n & ~SC);
            if (sc && shs() == 0) { dispara_ = true; ev_.notify(sc_core::SC_ZERO_TIME); }
            if (!(ctl0_ & ON) || !(ctl0_ & ENC)) ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        case 0x02: if (!enc) ctl1_ = uint16_t(mz(ctl1_) & 0x7FFEu); return;
        case 0x04: if (!enc) ctl2_ = uint16_t(mz(ctl2_) & 0x0039u); return;
        case 0x06: if (!enc) ctl3_ = uint16_t(mz(ctl3_) & 0x0FDFu); return;
        case 0x08: lo_ = mz(lo_); return;
        case 0x0A: hi_ = mz(hi_); return;
        case 0x0C: ifg_ = (ifg_ & 0xFFFF0000u) | mz(uint16_t(ifg_)); avisa(); return;
        case 0x0E: ifg_ = (ifg_ & 0xFFFFu) | (uint32_t(mz(uint16_t(ifg_ >> 16))) << 16); avisa(); return;
        case 0x10: ifg2_ = uint16_t(mz(ifg2_) & 0x007Eu); avisa(); return;
        case 0x12: ie_ = (ie_ & 0xFFFF0000u) | mz(uint16_t(ie_)); avisa(); return;
        case 0x14: ie_ = (ie_ & 0xFFFFu) | (uint32_t(mz(uint16_t(ie_ >> 16))) << 16); avisa(); return;
        case 0x16: ie2_ = uint16_t(mz(ie2_) & 0x007Eu); avisa(); return;
        default: return;
        }
    }
    // Un flanco de subida del disparo `s` (ADC12SHS)
    void dispara(unsigned s) {
        if (s == 0 || s != shs() || !(ctl0_ & ON) || !(ctl0_ & ENC)) return;
        dispara_ = true;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    bool pide() const { return (ifg_ & ie_) || (ifg2_ & ie2_); }
    uint32_t ifg() const { return ifg_; }
    uint16_t mem(unsigned i) const { return mem_[i & 31]; }
    bool encendido() const { return (ctl0_ & ON) != 0; }
    bool convirtiendo() const { return busy_; }
    unsigned fuente_reloj() const { return (ctl1_ >> 3) & 3u; }
    bool tcmap() const { return (ctl3_ & 0x80) != 0; }
    bool batmap() const { return (ctl3_ & 0x40) != 0; }

private:
    enum : uint16_t { SC = 0x01, ENC = 0x02, ON = 0x10, MSC = 0x80 };
    uint16_t ctl0_ = 0, ctl1_ = 0, ctl2_ = 0x0020, ctl3_ = 0, lo_ = 0, hi_ = 0x0FFF, ifg2_ = 0, ie2_ = 0;
    uint32_t ifg_ = 0, ie_ = 0;
    uint16_t mctl_[32] = {};
    uint16_t mem_[32] = {};
    bool busy_ = false, dispara_ = false;
    sc_core::sc_event ev_;

    unsigned shs() const { return (ctl1_ >> 10) & 7u; }
    void avisa() { if (al_cambiar) al_cambiar(); }
    uint16_t iv(bool dbg) {
        static const struct { uint16_t bit, v; } P[5] = {{0x10, 0x02}, {0x20, 0x04}, {0x08, 0x06}, {0x04, 0x08}, {0x02, 0x0A}};
        for (const auto& x : P)
            if (ifg2_ & ie2_ & x.bit) { if (!dbg) { ifg2_ = uint16_t(ifg2_ & ~x.bit); avisa(); } return x.v; }
        const uint32_t p = ifg_ & ie_;
        for (unsigned i = 0; i < 32; ++i)
            if (p & (1u << i)) { if (!dbg) { ifg_ &= ~(1u << i); avisa(); } return uint16_t(0x0C + 2 * i); }
        if (ifg2_ & ie2_ & 0x40) { if (!dbg) { ifg2_ = uint16_t(ifg2_ & ~0x40u); avisa(); } return 0x4C; }
        return 0;
    }
    unsigned bits() const { const unsigned r = (ctl2_ >> 4) & 3u; return r == 0 ? 8 : r == 1 ? 10 : 12; }
    double t_conversion(unsigned i) const {
        static const unsigned SHT[16] = {4, 8, 16, 32, 64, 96, 128, 192, 256, 384, 512, 512, 512, 512, 512, 512};
        static const unsigned PDIV[4] = {1, 4, 32, 64};
        const unsigned ssel = (ctl1_ >> 3) & 3u, div = ((ctl1_ >> 5) & 7u) + 1u;
        const double f = (f_reloj ? f_reloj(ssel) : 5e6) / div / PDIV[(ctl1_ >> 13) & 3u];
        if (f <= 0) return 0;
        const unsigned conv = bits() + 2;                       // 10, 12, 14 ciclos [§34.2.5.1]
        const bool sht1 = i >= 8 && i < 24;
        const unsigned sht = (ctl1_ & 0x200) ? SHT[(ctl0_ >> (sht1 ? 12 : 8)) & 15u] : 4u;
        return double(sht + conv) / f;
    }
    double vr(bool alta, unsigned sel) const {
        enum { AVCC, AVSS, VREF, EP, EM };
        static const uint8_t MAS[16] = {AVCC, VREF, EM, EP, EP, AVCC, AVCC, VREF, AVCC, AVCC, AVCC, EP, AVCC, VREF, EP, EP};
        static const uint8_t MEN[16] = {AVSS, AVSS, AVSS, AVSS, AVSS, EP, EP, EP, AVSS, VREF, AVSS, VREF, EM, EM, EM, EM};
        const unsigned x = alta ? MAS[sel & 15u] : MEN[sel & 15u];
        switch (x) {
        case AVCC: return v_avcc ? v_avcc() : 3.3;
        case VREF: return v_vref ? v_vref() : 0.0;
        case EP: return v_eref ? v_eref(true) : 0.0;
        case EM: return v_eref ? v_eref(false) : 0.0;
        default: return 0.0;
        }
    }
    void convierte(unsigned i) {
        const uint16_t mc = mctl_[i];
        const unsigned inch = mc & 31u, sel = (mc >> 8) & 15u;
        const double vp = vr(true, sel), vn = vr(false, sel);
        const unsigned b = bits();
        const double fe = double((1u << b) - 1u);
        double v = v_canal ? v_canal(inch) : 0.0;
        double x;
        if (mc & 0x2000) {                                      // DIF: 2n contra 2n+1
            const double v2 = v_canal ? v_canal(inch ^ 1u) : 0.0;
            if (inch & 1u) v = v_canal ? v_canal(inch & ~1u) : 0.0;
            x = vp > vn ? ((v - v2) / (vp - vn) + 1.0) / 2.0 * fe : 0.0;
        } else x = vp > vn ? (v - vn) / (vp - vn) * fe : 0.0;
        long n = std::lround(x);
        if (n < 0) n = 0;
        if (n > long(fe)) n = long(fe);
        uint16_t r = uint16_t(n);
        const bool con_signo = (ctl2_ & 0x08) != 0;
        if (con_signo) r = uint16_t((uint16_t(n) - (1u << (b - 1))) << (16 - b));
        if (mc & 0x4000) {                                      // WINC
            const int32_t ri = con_signo ? int16_t(r) : int32_t(r);
            const int32_t lo = con_signo ? int16_t(lo_) : int32_t(lo_), hi = con_signo ? int16_t(hi_) : int32_t(hi_);
            if (ri > hi) ifg2_ |= 0x08;
            else if (ri < lo) ifg2_ |= 0x04;
            else ifg2_ |= 0x02;
        }
        if (ifg_ & (1u << i)) ifg2_ |= 0x10;                    // OV: se pisa sin leer
        mem_[i] = r;
        ifg_ |= 1u << i;
        avisa();
        if (al_convertir) al_convertir(i);
    }
    bool usa_ref(unsigned i) const {
        const unsigned sel = (mctl_[i] >> 8) & 15u;
        return sel == 1 || sel == 7 || sel == 9 || sel == 11 || sel == 13 || ((mctl_[i] & 31u) == 30 && tcmap());
    }
    void conv_proc() {
        for (;;) {
            wait(ev_);
            if (!(ctl0_ & ON) || !(ctl0_ & ENC) || !dispara_) continue;
            dispara_ = false;
            busy_ = true;
            const unsigned conseq = (ctl1_ >> 1) & 3u, inicio = ctl3_ & 31u;
            unsigned i = inicio;
            if (pide_ref) pide_ref(usa_ref(i));
            for (;;) {
                const double t = t_conversion(i);
                if (t > 0) wait(sc_core::sc_time(t, sc_core::SC_SEC));
                if (!(ctl0_ & ON)) break;
                convierte(i);
                const bool eos = (mctl_[i] & 0x80) != 0;
                if (conseq == 0) break;
                if (conseq == 1 || conseq == 3) {
                    if (eos || i == 31) { if (conseq == 1) break; i = inicio; }
                    else ++i;
                }
                if (!(ctl0_ & ENC)) break;                      // ENC a 0: acaba la secuencia
                if (pide_ref) pide_ref(usa_ref(i));
                // Sin MSC, cada conversión espera su disparo
                if (!(ctl0_ & MSC)) {
                    while ((ctl0_ & ON) && (ctl0_ & ENC) && !dispara_) wait(ev_);
                    if (!(ctl0_ & ON) || !(ctl0_ & ENC)) break;
                    dispara_ = false;
                }
            }
            busy_ = false;
            if (pide_ref) pide_ref(false);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_ADC12_B_H
