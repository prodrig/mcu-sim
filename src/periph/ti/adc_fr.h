// =============================================================================
// adc_fr.h — El ADC de 10 bits de las FR2xx/FR4xx (M6 de doc/analisis_msp430.md)
// [UG FR2 SLAU445I cap. 21]
//
//   +00 ADCCTL0   ADCSHT (11-8), ADCMSC (7), ADCON (4), ADCENC (1), ADCSC (0)
//   +02 ADCCTL1   ADCSHS (11-10), ADCSHP (9), ADCISSH (8), ADCDIV (7-5),
//                 ADCSSEL (4-3: MODCLK, ACLK, SMCLK), ADCCONSEQ (2-1), ADCBUSY
//   +04 ADCCTL2   ADCPDIV (9-8: /1, /4, /64), ADCRES (5-4: 8 o 10 bits),
//                 ADCDF (3: con signo, a la izquierda), ADCSR (2)
//   +06 ADCLO, +08 ADCHI  la ventana
//   +0A ADCMCTL0  ADCSREF (6-4), ADCINCH (3-0)
//   +12 ADCMEM0   el resultado (leerlo borra ADCIFG0)
//   +1A ADCIE, +1C ADCIFG  TOV 0x20, OV 0x10, HI 0x08, LO 0x04, IN 0x02, IFG0 0x01
//   +1E ADCIV     2 OV, 4 TOV, 6 HI, 8 LO, 0x0A IN, 0x0C IFG0
//
// Cada conversión dura el muestreo (ADCSHP = 1: 4-1024 ciclos de ADCCLK;
// ADCSHP = 0: el pulso, que aquí es inmediato [de modelo]) más 10 o 12 de
// conversión. Los cuatro modos de ADCCONSEQ; en los de secuencia, de INCH
// hacia abajo hasta A0. Disparos: ADCSC o los de fuera (`dispara(shs)`, con
// ADCSHS distinto de 0). El canal y las referencias los da el SoC.
// =============================================================================
#ifndef STM32_PERIPH_TI_ADC_FR_H
#define STM32_PERIPH_TI_ADC_FR_H

#include <cmath>
#include <cstdint>
#include <functional>
#include <systemc>

namespace stm32 {

SC_MODULE(AdcFr) {
    std::function<double(unsigned)> v_canal;   // la tensión de An (12 sensor, 13 1,5 V...)
    std::function<double(unsigned)> v_ref;     // VR+ según ADCSREF
    std::function<double(unsigned)> f_reloj;   // ADCSSEL: 0 MODCLK, 1 ACLK, 2-3 SMCLK
    std::function<void()> al_cambiar;
    std::function<void(bool)> pide_reloj;      // el ADC necesita su reloj (MODOSC en LPM3)

    SC_CTOR(AdcFr) { SC_THREAD(hilo); reset(); }

    void reset() {
        ctl0_ = 0; ctl1_ = 0; ctl2_ = 0x0010; lo_ = 0; hi_ = 0x3FF; mctl_ = 0; mem_ = 0; ie_ = 0; ifg_ = 0;
        ocupado_ = false; pendiente_ = false; leido_ = true; canal_ = 0;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    uint16_t lee(uint32_t off, bool dbg) {
        switch (off & 0x1E) {
        case 0x00: return ctl0_;
        case 0x02: return uint16_t((ctl1_ & ~1u) | (ocupado_ ? 1u : 0u));
        case 0x04: return ctl2_;
        case 0x06: return lo_;
        case 0x08: return hi_;
        case 0x0A: return uint16_t((mctl_ & 0x70u) | (secuencia() && ocupado_ ? canal_ : (mctl_ & 0x0Fu)));
        case 0x12: if (!dbg) { leido_ = true; if (ifg_ & 1u) { ifg_ = uint16_t(ifg_ & ~1u); aviso(); } } return formato(mem_);
        case 0x1A: return ie_;
        case 0x1C: return ifg_;
        case 0x1E: return iv(dbg);
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        auto mz = [&](uint16_t c) { return uint16_t((c & ~m) | (v & m)); };
        const bool enc = ctl0_ & 0x0002;
        switch (off & 0x1E) {
        case 0x00: {
            uint16_t n = mz(ctl0_);
            if (enc) n = uint16_t((ctl0_ & 0x0F90u) | (n & 0x0003u));     // con ENC solo ENC y SC
            ctl0_ = uint16_t(n & 0x0F93u);
            if (!(ctl0_ & 0x0010)) { ctl0_ = uint16_t(ctl0_ & ~0x0002u); ocupado_ = false; }
            if ((ctl0_ & 0x0001) && (ctl0_ & 0x0012) == 0x0012 && shs() == 0) arranca();
            ctl0_ = uint16_t(ctl0_ & ~0x0001u);                            // ADCSC se borra solo
            break;
        }
        case 0x02: if (!enc) ctl1_ = uint16_t(mz(ctl1_) & 0x0FFEu); break;
        case 0x04: if (!enc) ctl2_ = uint16_t(mz(ctl2_) & 0x033Cu); break;
        case 0x06: lo_ = mz(lo_); break;
        case 0x08: hi_ = mz(hi_); break;
        case 0x0A: if (!enc) mctl_ = uint16_t(mz(mctl_) & 0x007Fu); break;
        case 0x12: mem_ = mz(mem_); break;
        case 0x1A: ie_ = uint16_t(mz(ie_) & 0x3Fu); aviso(); break;
        case 0x1C: ifg_ = uint16_t(mz(ifg_) & 0x3Fu); aviso(); break;
        case 0x1E: ifg_ = 0; aviso(); break;
        default: break;
        }
    }
    // Un disparo de fuera: ADCSHS = shs (1 el RTC, 2 TA1.1B en el FR2433)
    void dispara(unsigned s) {
        if (shs() == s && s != 0 && (ctl0_ & 0x0012) == 0x0012) arranca();
    }
    bool pide() const { return (ifg_ & ie_) != 0; }
    bool encendido() const { return ctl0_ & 0x0010; }
    bool convirtiendo() const { return ocupado_; }
    unsigned fuente_reloj() const { return (ctl1_ >> 3) & 3u; }
    uint64_t conversiones() const { return n_conv_; }

private:
    uint16_t ctl0_ = 0, ctl1_ = 0, ctl2_ = 0x10, lo_ = 0, hi_ = 0x3FF, mctl_ = 0, mem_ = 0, ie_ = 0, ifg_ = 0;
    bool ocupado_ = false, pendiente_ = false, leido_ = true;
    unsigned canal_ = 0;
    uint64_t n_conv_ = 0;
    sc_core::sc_event ev_;
    unsigned shs() const { return (ctl1_ >> 10) & 3u; }
    unsigned conseq() const { return (ctl1_ >> 1) & 3u; }
    bool secuencia() const { return conseq() & 1u; }
    bool secuencia_repite() const { return conseq() & 2u; }
    unsigned bits() const { return ((ctl2_ >> 4) & 3u) == 0 ? 8u : 10u; }
    void aviso() { if (al_cambiar) al_cambiar(); }
    void arranca() {
        if (ocupado_) { ifg_ |= 0x20; aviso(); return; }                  // ADCTOVIFG
        pendiente_ = true;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    uint16_t formato(uint16_t r) const {
        if (!(ctl2_ & 0x0008)) return r;                                  // binario, a la derecha
        const unsigned b = bits();
        const int s = int(r) - (1 << (b - 1));
        return uint16_t(uint16_t(s) << (16 - b));
    }
    uint16_t iv(bool dbg) {
        static const uint16_t ORDEN[6] = {0x10, 0x20, 0x08, 0x04, 0x02, 0x01};
        const uint16_t p = uint16_t(ifg_ & ie_);
        for (unsigned i = 0; i < 6; ++i)
            if (p & ORDEN[i]) { if (!dbg) { ifg_ = uint16_t(ifg_ & ~ORDEN[i]); aviso(); } return uint16_t(2 * (i + 1)); }
        return 0;
    }
    double t_conversion() const {
        static const unsigned SHT[16] = {4, 8, 16, 32, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1024, 1024, 1024};
        static const unsigned PD[4] = {1, 4, 64, 1};
        const double f = f_reloj ? f_reloj((ctl1_ >> 3) & 3u) : 0.0;
        if (f <= 0.0) return -1.0;
        const double fa = f / PD[(ctl2_ >> 8) & 3u] / double(((ctl1_ >> 5) & 7u) + 1u);
        const unsigned muestreo = (ctl1_ & 0x0200) ? SHT[(ctl0_ >> 8) & 15u] : 0u;
        return double(muestreo + (bits() == 8 ? 10u : 12u)) / fa;
    }
    void hilo() {
        for (;;) {
            while (!pendiente_) wait(ev_);
            pendiente_ = false;
            ocupado_ = true;
            if (pide_reloj) pide_reloj(true);
            canal_ = mctl_ & 0x0Fu;
            for (;;) {
                double t = t_conversion();
                while (t < 0.0) { wait(sc_core::sc_time(1, sc_core::SC_US)); t = t_conversion(); }
                wait(sc_core::sc_time(t, sc_core::SC_SEC));
                if (!ocupado_ || !(ctl0_ & 0x0010)) break;
                // el resultado
                const double vr = v_ref ? v_ref((mctl_ >> 4) & 7u) : 3.3;
                const double vin = v_canal ? v_canal(canal_) : 0.0;
                const unsigned b = bits(), max = (1u << b) - 1u;
                double x = vr > 0.0 ? vin / vr * double(max) : 0.0;
                if (x < 0) x = 0;
                if (x > double(max)) x = double(max);
                if (!leido_ && (ifg_ & 1u)) ifg_ |= 0x10;                      // ADCOVIFG
                mem_ = uint16_t(std::lround(x));
                leido_ = false;
                ++n_conv_;
                const int16_t r = int16_t(formato(mem_));
                const bool con_signo = ctl2_ & 0x0008;
                const bool sobre = con_signo ? r > int16_t(hi_) : formato(mem_) > hi_;
                const bool bajo = con_signo ? r < int16_t(lo_) : formato(mem_) < lo_;
                ifg_ |= uint16_t(0x01 | (sobre ? 0x08 : 0) | (bajo ? 0x04 : 0) | (!sobre && !bajo ? 0x02 : 0));
                aviso();
                // ¿otra?
                const bool enc = ctl0_ & 0x0002;
                // [de modelo] la secuencia sigue sola también con ADCMSC = 0
                if (secuencia() && canal_ > 0 && enc) { --canal_; continue; }
                if (secuencia_repite() && enc && (ctl0_ & 0x0080)) { canal_ = mctl_ & 0x0Fu; continue; }
                break;
            }
            ocupado_ = false;
            if (pide_reloj) pide_reloj(false);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_ADC_FR_H
