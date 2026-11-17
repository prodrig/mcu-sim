// =============================================================================
// rtc_fr.h — El RTC contador de las FR2xx/FR4xx (M6 de doc/analisis_msp430.md)
// [UG FR2 SLAU445I cap. 15]
//
//   +00 RTCCTL   RTCSS (13-12: nada, el del chip, XT1CLK, VLOCLK), RTCPS
//                (10-8: /1, /10, /100, /1000, /16, /64, /256, /1024), RTCSR
//                (6, se escribe), RTCIE (1), RTCIFG (0, se borra leyendo RTCIV)
//   +04 RTCIV    2: el desbordamiento
//   +08 RTCMOD   el módulo (0xBEEF al reset); pasa a la sombra en cada
//                desbordamiento y con RTCSR
//   +0C RTCCNT   el contador (solo lectura)
//
// El contador cuenta de 0 a la sombra y vuelve a 0: el desbordamiento cada
// sombra+1 pulsos del reloj dividido (cada pulso con 0x0000 o 0x0001)
// [UG FR2 §15.2.1, §15.2.3, figura 15-2]. Sigue en LPM3.5 (el dominio del
// RTC no se apaga): el SoC solo lo pone a cero con un BOR que no sea el
// despertar de LPMx.5.
//
// Como el RTC_A (rtc_a.h), no cuenta pulso a pulso: el contador se calcula
// del tiempo con la frecuencia que da el SoC, y un hilo espera al siguiente
// desbordamiento. `al_desbordar` es el disparo hacia fuera (el ADC, CCI1B
// del Timer0_A3).
// =============================================================================
#ifndef STM32_PERIPH_TI_RTC_FR_H
#define STM32_PERIPH_TI_RTC_FR_H

#include <cmath>
#include <cstdint>
#include <functional>
#include <systemc>

namespace stm32 {

SC_MODULE(RtcFr) {
    // La frecuencia de la fuente RTCSS (1 la del chip, 2 XT1CLK, 3 VLOCLK)
    std::function<double(unsigned)> f_fuente;
    std::function<void()> al_cambiar;          // RTCIFG o RTCIE
    std::function<void()> al_desbordar;        // el disparo hacia otros módulos
    sc_core::sc_event ev_desborde;

    SC_CTOR(RtcFr) { SC_THREAD(hilo); reset(); }

    void reset() {
        ctl_ = 0; mod_ = 0xBEEF; sombra_ = 0xBEEF; base_ = 0; t0_ = sc_core::sc_time_stamp(); f_ = 0;
        n_desb_ = 0;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    uint16_t lee(uint32_t off, bool dbg) {
        switch (off & 0x0E) {
        case 0x0: return ctl_;
        case 0x4: if (ctl_ & 1u) { if (!dbg) { ctl_ = uint16_t(ctl_ & ~1u); aviso(); } return 2; } return 0;
        case 0x8: return mod_;
        case 0xC: return uint16_t(cuenta());
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        const uint16_t n = uint16_t((lee(off, true) & ~m) | (v & m));
        switch (off & 0x0E) {
        case 0x0: {
            al_dia();
            ctl_ = uint16_t((ctl_ & 1u) | (n & 0x3702u));
            if (n & 0x0040) { base_ = 0; sombra_ = mod_; }           // RTCSR
            recalcula();
            aviso();
            break;
        }
        case 0x4: if (m) { ctl_ = uint16_t(ctl_ & ~1u); aviso(); } break;   // escribir RTCIV lo borra
        case 0x8: mod_ = n; break;
        default: break;
        }
    }
    // La frecuencia del contador ha podido cambiar (la fuente, los LPM)
    void recalcula() {
        al_dia();
        const unsigned ss = (ctl_ >> 12) & 3u;
        static const double PS[8] = {1, 10, 100, 1000, 16, 64, 256, 1024};
        const double fs = ss == 0 ? 0.0 : (f_fuente ? f_fuente(ss) : 0.0);
        f_ = fs / PS[(ctl_ >> 8) & 7u];
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool pide() const { return (ctl_ & 0x0003) == 0x0003; }
    bool ifg() const { return ctl_ & 1u; }
    bool activo() const { return ((ctl_ >> 12) & 3u) != 0; }
    unsigned fuente() const { return (ctl_ >> 12) & 3u; }
    uint64_t desbordamientos() const { return n_desb_; }

private:
    uint16_t ctl_ = 0, mod_ = 0xBEEF, sombra_ = 0xBEEF;
    uint32_t base_ = 0;                          // el contador en t0_
    sc_core::sc_time t0_;
    double f_ = 0;
    uint64_t n_desb_ = 0;
    sc_core::sc_event ev_;
    uint32_t periodo() const { return sombra_ <= 1 ? 1u : uint32_t(sombra_) + 1u; }
    uint64_t pulsos() const {
        if (f_ <= 0.0) return 0;
        return uint64_t(std::floor((sc_core::sc_time_stamp() - t0_).to_seconds() * f_ + 1e-9));
    }
    uint32_t cuenta() const {
        const uint64_t c = base_ + pulsos();
        return uint32_t(c % periodo());
    }
    // La cuenta, anclada a ahora (antes de cambiar la frecuencia)
    void al_dia() {
        if (f_ <= 0.0) { t0_ = sc_core::sc_time_stamp(); return; }
        const uint64_t p = pulsos();
        base_ = uint32_t((base_ + p) % periodo());
        t0_ += sc_core::sc_time(double(p) / f_, sc_core::SC_SEC);
    }
    void aviso() { if (al_cambiar) al_cambiar(); }
    void hilo() {
        for (;;) {
            if (f_ <= 0.0) { wait(ev_); continue; }
            const uint64_t c = base_ + pulsos();
            const uint64_t faltan = periodo() - (c % periodo());
            const sc_core::sc_time cuando = t0_ + sc_core::sc_time(double(c - base_ + faltan) / f_, sc_core::SC_SEC);
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (cuando > ahora) { wait(cuando - ahora, ev_); if (sc_core::sc_time_stamp() < cuando) continue; }
            // el desbordamiento: a cero, la sombra se recarga, RTCIFG
            t0_ = cuando; base_ = 0; sombra_ = mod_;
            ctl_ |= 1u;
            ++n_desb_;
            ev_desborde.notify(sc_core::SC_ZERO_TIME);
            if (al_desbordar) al_desbordar();
            aviso();
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_RTC_FR_H
