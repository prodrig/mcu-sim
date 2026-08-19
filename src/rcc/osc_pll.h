// =============================================================================
// osc_pll.h — Osciladores (HSI/HSE/LSI/LSE) y PLLs (Main, PLLI2S)
//
// Todos generan onda cuadrada digital (ClockGen) [premisa del proyecto].
// Cada fuente modela su tiempo de estabilización antes de activar su flag RDY
// [IR, §4.2]. HSE y LSE observan sus pads (PH0/PH1, PC14/PC15) por la ruta
// analógica del pin_mux; la comprobación eléctrica del cristal es de fase F3.
//
// Fase F1: el control es por MÉTODO (enable/configure) en lugar de por puerto
// de entrada. Motivo: el RCC escribe estos controles desde el b_transport de un
// registro y necesita el efecto (y la frecuencia resultante) dentro del mismo
// delta; con sc_signal el valor no sería visible hasta el delta siguiente y el
// árbol de reloj quedaría un ciclo por detrás del banco de registros.
// Los flags RDY y los relojes sí salen por puertos, que es como los ve el resto
// del modelo.
// =============================================================================
#ifndef STM32_RCC_OSC_PLL_H
#define STM32_RCC_OSC_PLL_H

#include <systemc>
#include "../common/clock_gen.h"
#include "../common/analog_net.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// Oscilador genérico (HSI, HSE, LSI, LSE) [IR, §4.2]
// ---------------------------------------------------------------------------
SC_MODULE(Oscillator) {
    sc_core::sc_out<bool>   ready{"ready"};    // bit xxxRDY tras t_startup
    sc_core::sc_out<bool>   clk{"clk"};
    sc_core::sc_out<double> freq_hz{"freq_hz"};

    double nominal_hz  = 16e6;                  // HSI 16M, LSI 32k, ...
    double t_startup_s = 4e-6;                  // [IR, §4.2 tabla]
    bool   bypass      = false;                 // HSEBYP / LSEBYP
    analog_net_if* xtal_in = nullptr;           // solo HSE/LSE (via pin_mux)

    SC_CTOR(Oscillator) : gen_("gen") {
        gen_.clk(clk);
        gen_.freq_hz(freq_hz);
        SC_THREAD(ctrl_proc);
    }

    // --- Control desde el banco de registros del RCC ------------------------
    void enable(bool en) {
        if (en == on_) return;
        on_ = en;
        ctrl_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool   enabled()  const { return on_; }
    bool   is_ready() const { return ready_; }
    double out_hz()   const { return ready_ ? nominal_hz : 0.0; }
    // Evento de cambio de estado (RDY): el RCC recalcula el árbol al recibirlo.
    const sc_core::sc_event& state_event() const { return state_ev_; }

private:
    ClockGen gen_;
    bool on_ = false, ready_ = false;
    sc_core::sc_event ctrl_ev_, state_ev_;

    void ctrl_proc() {
        ready.write(false);
        for (;;) {
            wait(ctrl_ev_);
            if (on_) {
                // TODO(F3): HSE/LSE -> comprobar presencia de cristal/reloj
                //           externo en xtal_in (nivel y régimen de conmutación).
                wait(sc_core::sc_time(t_startup_s, sc_core::SC_SEC));
                if (!on_) continue;              // se apagó durante el arranque
                gen_.set_freq(nominal_hz);
                ready_ = true;
            } else {
                gen_.set_freq(0.0);
                ready_ = false;
            }
            ready.write(ready_);
            state_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
};

// ---------------------------------------------------------------------------
// PLL principal y PLLI2S [IR, §4.3]
//   fVCO_IN  = fPLL_IN / M      (debe quedar entre 1 y 2 MHz)
//   fVCO_OUT = fVCO_IN  * N     (debe quedar entre 100 y 432 MHz)
//   fP       = fVCO_OUT / P     (SYSCLK, máx 168 MHz)  [P = 2,4,6,8]
//   fQ       = fVCO_OUT / Q     (PLL48CK: USB OTG FS, RNG, SDIO) [Q = 2..15]
// En el PLLI2S la salida "P" del modelo representa la salida R (2..7).
// ---------------------------------------------------------------------------
SC_MODULE(Pll) {
    sc_core::sc_out<bool>   ready{"ready"};     // PLLRDY / PLLI2SRDY
    sc_core::sc_out<bool>   clk_p{"clk_p"};     // salida P (SYSCLK) / R (I2S)
    sc_core::sc_out<double> clk_p_hz{"clk_p_hz"};
    sc_core::sc_out<bool>   clk_q{"clk_q"};     // salida Q (PLL48CK)
    sc_core::sc_out<double> clk_q_hz{"clk_q_hz"};

    // ⚠ NO DISPONIBLE EN LAS FUENTES: tiempo de enganche del PLL. Parámetro
    // del modelo (200 us es el orden de magnitud habitual del bloque).
    double t_lock_s = 200e-6;

    SC_CTOR(Pll) : gp_("gp"), gq_("gq") {
        gp_.clk(clk_p); gp_.freq_hz(clk_p_hz);
        gq_.clk(clk_q); gq_.freq_hz(clk_q_hz);
        SC_THREAD(ctrl_proc);
    }

    // Programación desde RCC_PLLCFGR / RCC_PLLI2SCFGR [IR, §4.5.2, §4.11.2]
    void configure(unsigned m, unsigned n, unsigned p, unsigned q) {
        if (m == m_ && n == n_ && p == p_ && q == q_) return;
        m_ = m; n_ = n; p_ = p; q_ = q;
        restart();
    }
    void set_ref_hz(double hz) {
        if (hz == ref_hz_) return;
        ref_hz_ = hz;
        restart();
    }
    void enable(bool en) {
        if (en == on_) return;
        on_ = en;
        restart();
    }
    bool   enabled()  const { return on_; }
    bool   is_ready() const { return ready_; }
    double vco_in_hz()  const { return m_ ? ref_hz_ / m_ : 0.0; }
    double vco_out_hz() const { return vco_in_hz() * n_; }
    double out_p_hz()   const { return (ready_ && p_) ? vco_out_hz() / p_ : 0.0; }
    double out_q_hz()   const { return (ready_ && q_) ? vco_out_hz() / q_ : 0.0; }
    // Rangos legales [IR, §4.3.1]
    bool ranges_ok() const {
        const double vi = vco_in_hz(), vo = vco_out_hz();
        return vi >= 1e6 && vi <= 2e6 && vo >= 100e6 && vo <= 432e6;
    }
    const sc_core::sc_event& state_event() const { return state_ev_; }

private:
    ClockGen gp_, gq_;
    unsigned m_ = 16, n_ = 192, p_ = 2, q_ = 4;
    double   ref_hz_ = 0.0;
    bool     on_ = false, ready_ = false;
    sc_core::sc_event ctrl_ev_, state_ev_;

    void restart() { ctrl_ev_.notify(sc_core::SC_ZERO_TIME); }

    void ctrl_proc() {
        ready.write(false);
        for (;;) {
            wait(ctrl_ev_);
            if (ready_) {                        // cualquier cambio pierde el lock
                ready_ = false; ready.write(false);
                gp_.set_freq(0.0); gq_.set_freq(0.0);
                state_ev_.notify(sc_core::SC_ZERO_TIME);
            }
            if (!on_ || ref_hz_ <= 0.0 || m_ == 0 || n_ == 0) continue;
            if (!ranges_ok())
                SC_REPORT_WARNING("pll",
                    "VCO fuera de rango: entrada 1-2 MHz, salida 100-432 MHz [IR, 4.3.1]");
            wait(sc_core::sc_time(t_lock_s, sc_core::SC_SEC));
            if (!on_ || ref_hz_ <= 0.0) continue;
            ready_ = true;
            gp_.set_freq(p_ ? vco_out_hz() / p_ : 0.0);
            gq_.set_freq(q_ ? vco_out_hz() / q_ : 0.0);
            ready.write(true);
            state_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }
};

} // namespace stm32
#endif // STM32_RCC_OSC_PLL_H
