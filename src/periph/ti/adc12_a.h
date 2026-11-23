// =============================================================================
// adc12_a.h — El ADC12_A de los MSP430 5xx/6xx [UG5 SLAU208Q cap. 28]
// (M4 de doc/analisis_msp430.md)
//
//   +00 ADC12CTL0  SC ENC TOVIE OVIE ON REFON REF2_5V MSC SHT0 SHT1
//   +02 ADC12CTL1  BUSY CONSEQ SSEL DIV ISSH SHP SHS CSTARTADD
//   +04 ADC12CTL2  REFBURST REFOUT SR DF RES TCOFF PDIV
//   +0A ADC12IFG   +0C ADC12IE   +0E ADC12IV
//   +10..+1F ADC12MCTLx (bytes): INCH SREF EOS
//   +20..+3E ADC12MEMx
//
// Las cuatro secuencias (canal único, secuencia de CSTARTADD hasta EOS, y
// las repetidas), MSC, 8/10/12 bits, el formato con signo (DF), el muestreo
// por SHP (SHT0 para MEM0-7 y MEM8-15 con SHT1) o por el pulso. Leer MEMx
// borra su IFG; el IV da OV, TOV y los IFGx por orden [§28.3.7]. El disparo
// es ADC12SC (SHS = 0); los del Timer se avisan una vez.
//
// La referencia la da el SoC (`v_ref(sref)`: VR+ según SREF, con el REF del
// chip o el interno de ADC12REFON), y las tensiones de los canales
// (`v_canal`). Una conversión que se sale da 0 o el fondo de escala.
// =============================================================================
#ifndef STM32_PERIPH_TI_ADC12_A_H
#define STM32_PERIPH_TI_ADC12_A_H

#include <systemc>
#include <cmath>
#include <cstdio>
#include <functional>
#include "../../common/verbosidad.h"

namespace stm32 {

SC_MODULE(Adc12A) {
    std::function<double(unsigned)> v_canal;    // la tensión de la entrada INCH
    std::function<double(unsigned)> v_ref;      // VR+ para SREF (VR- = AVSS)
    std::function<double(unsigned)> f_reloj;    // ADC12SSEL -> Hz (0: MODOSC)
    std::function<void()> al_cambiar;           // IFG/IE: que el SoC arbitre
    std::function<void(unsigned)> al_convertir; // el IFGx que se ha puesto (DMA)

    SC_CTOR(Adc12A) {
        SC_THREAD(conv_proc);
        reset();
    }
    void reset() {
        ctl0_ = ctl1_ = 0; ctl2_ = 0x0020; ifg_ = ie_ = 0; ov_ = tov_ = false;
        for (unsigned i = 0; i < 16; ++i) { mctl_[i] = 0; mem_[i] = 0; }
        busy_ = false;
    }

    uint16_t lee16(uint32_t off, bool dbg) {
        if (off >= 0x10 && off < 0x20) {
            const unsigned i = (off - 0x10) & ~1u;
            return uint16_t(mctl_[i] | (mctl_[i + 1] << 8));
        }
        if (off >= 0x20 && off < 0x40) {
            const unsigned i = (off - 0x20) / 2;
            if (!dbg) { ifg_ = uint16_t(ifg_ & ~(1u << i)); avisa(); }
            return mem_[i];
        }
        switch (off) {
        case 0x00: return ctl0_;
        case 0x02: return uint16_t(ctl1_ | (busy_ ? 1 : 0));
        case 0x04: return ctl2_;
        case 0x0A: return ifg_;
        case 0x0C: return ie_;
        case 0x0E: return iv(dbg);
        default: return 0;
        }
    }
    uint8_t lee8(uint32_t off, bool dbg) {
        if (off >= 0x10 && off < 0x20) return mctl_[off - 0x10];
        const uint16_t w = lee16(off & ~1u, dbg);
        return uint8_t((off & 1) ? (w >> 8) : w);
    }
    void escribe16(uint32_t off, uint16_t v) {
        if (off >= 0x10 && off < 0x20) {
            const unsigned i = (off - 0x10) & ~1u;
            if (!(ctl0_ & ENC)) { mctl_[i] = uint8_t(v); mctl_[i + 1] = uint8_t(v >> 8); }
            return;
        }
        if (off >= 0x20 && off < 0x40) return;
        switch (off) {
        case 0x00: {
            // Con ENC puesto, solo cambian SC y ENC (y las IE) [§28.2.2]
            if (ctl0_ & ENC) v = uint16_t((ctl0_ & ~0x000F) | (v & 0x000F));
            const bool sc = (v & SC) && (v & ENC) && (v & ON);
            ctl0_ = uint16_t(v & ~SC);
            if (sc || ((v & ENC) && (v & ON) && shs() != 0)) ev_.notify(sc_core::SC_ZERO_TIME);
            if (sc) dispara_ = true;
            return;
        }
        case 0x02: if (!(ctl0_ & ENC)) ctl1_ = uint16_t(v & ~1u); return;
        case 0x04: if (!(ctl0_ & ENC)) ctl2_ = v; return;
        case 0x0A: ifg_ = v; avisa(); return;
        case 0x0C: ie_ = v; avisa(); return;
        default: return;
        }
    }
    void escribe8(uint32_t off, uint8_t v) {
        if (off >= 0x10 && off < 0x20) { if (!(ctl0_ & ENC)) mctl_[off - 0x10] = v; return; }
        const uint16_t w = lee16(off & ~1u, true);
        escribe16(off & ~1u, (off & 1) ? uint16_t((w & 0x00FF) | (v << 8)) : uint16_t((w & 0xFF00) | v));
    }

    bool pide() const { return (ifg_ & ie_) || (ov_ && (ctl0_ & 0x08)) || (tov_ && (ctl0_ & 0x04)); }
    uint16_t ifg() const { return ifg_; }
    uint16_t mem(unsigned i) const { return mem_[i & 15]; }
    bool encendido() const { return (ctl0_ & ON) != 0; }
    bool refon_interno() const { return (ctl0_ & 0x20) != 0; }
    bool ref2_5() const { return (ctl0_ & 0x40) != 0; }

private:
    enum : uint16_t { SC = 0x01, ENC = 0x02, ON = 0x10, MSC = 0x80 };
    uint16_t ctl0_ = 0, ctl1_ = 0, ctl2_ = 0x0020, ifg_ = 0, ie_ = 0;
    uint8_t mctl_[16] = {};
    uint16_t mem_[16] = {};
    bool ov_ = false, tov_ = false, busy_ = false, dispara_ = false, avisado_shs_ = false;
    sc_core::sc_event ev_;

    unsigned shs() const { return (ctl1_ >> 10) & 3u; }
    void avisa() { if (al_cambiar) al_cambiar(); }
    uint16_t iv(bool dbg) {
        if (ov_ && (ctl0_ & 0x08)) { if (!dbg) { ov_ = false; avisa(); } return 0x02; }
        if (tov_ && (ctl0_ & 0x04)) { if (!dbg) { tov_ = false; avisa(); } return 0x04; }
        const uint16_t p = uint16_t(ifg_ & ie_);
        for (unsigned i = 0; i < 16; ++i)
            if (p & (1u << i)) { if (!dbg) { ifg_ = uint16_t(ifg_ & ~(1u << i)); avisa(); } return uint16_t(6 + 2 * i); }
        return 0;
    }
    double t_conversion(unsigned i) const {
        static const unsigned SHT[16] = {4, 8, 16, 32, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1024, 1024, 1024};
        const unsigned ssel = (ctl1_ >> 3) & 3u, div = ((ctl1_ >> 5) & 7u) + 1u;
        const double f = (f_reloj ? f_reloj(ssel) : 5e6) / div / ((ctl2_ & 0x100) ? 4.0 : 1.0);
        if (f <= 0) return 0;
        const unsigned res = (ctl2_ >> 4) & 3u;
        const unsigned conv = res == 0 ? 9 : res == 1 ? 11 : 13;
        const unsigned sht = (ctl1_ & 0x200) ? SHT[(ctl0_ >> (i < 8 ? 8 : 12)) & 15u] : 4u;
        return double(sht + conv) / f;
    }
    void convierte(unsigned i) {
        const unsigned inch = mctl_[i] & 15u, sref = (mctl_[i] >> 4) & 7u;
        const double vr = v_ref ? v_ref(sref) : 3.3;
        const double v = v_canal ? v_canal(inch) : 0.0;
        const unsigned res = (ctl2_ >> 4) & 3u;
        const unsigned bits = res == 0 ? 8 : res == 1 ? 10 : 12;
        const double fe = double((1u << bits) - 1u);
        long x = vr > 0 ? std::lround(v / vr * fe) : 0;
        if (x < 0) x = 0;
        if (x > long(fe)) x = long(fe);
        uint16_t r = uint16_t(x);
        if (ctl2_ & 0x08) r = uint16_t((uint16_t(x) - (1u << (bits - 1))) << (16 - bits));   // DF: con signo, a la izquierda
        if (ifg_ & (1u << i)) ov_ = true;                        // se pisa sin leer
        mem_[i] = r;
        ifg_ = uint16_t(ifg_ | (1u << i));
        avisa();
        if (al_convertir) al_convertir(i);
    }
    void conv_proc() {
        for (;;) {
            wait(ev_);
            if (!(ctl0_ & ON) || !(ctl0_ & ENC)) continue;
            if (shs() != 0 && !avisado_shs_) {
                avisado_shs_ = true;
                ::stm32::avisa_consola("[adc12_a] el disparo por el Timer (ADC12SHSx) no se modela; vale ADC12SC\n");
            }
            if (!dispara_) continue;
            dispara_ = false;
            busy_ = true;
            const unsigned conseq = (ctl1_ >> 1) & 3u, inicio = (ctl1_ >> 12) & 15u;
            unsigned i = inicio;
            for (;;) {
                const double t = t_conversion(i);
                if (t > 0) wait(sc_core::sc_time(t, sc_core::SC_SEC));
                convierte(i);
                const bool eos = (mctl_[i] & 0x80) != 0;
                const bool enc = (ctl0_ & ENC) && (ctl0_ & ON);
                if (conseq == 0) break;
                if (conseq == 1 || conseq == 3) {
                    if (eos || i == 15) { if (conseq == 1) break; i = inicio; }
                    else ++i;
                }
                if (!enc) break;
                // Sin MSC, cada conversión espera su disparo
                if (!(ctl0_ & MSC)) {
                    do { wait(ev_); } while ((ctl0_ & ON) && !dispara_);
                    if (!(ctl0_ & ON)) break;
                    dispara_ = false;
                }
            }
            busy_ = false;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_ADC12_A_H
