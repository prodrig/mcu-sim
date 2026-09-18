// =============================================================================
// osc_pll.h — Osciladores (HSI/HSE/LSI/LSE) y PLLs (Main, PLLI2S)
//
// Todos generan onda cuadrada digital (ClockGen) [premisa del proyecto].
// Cada fuente modela su tiempo de estabilización antes de activar su flag RDY
// [IR, §4.2]. HSE y LSE observan sus pads (PH0/PH1, PC14/PC15) por la ruta
// analógica del pin_mux: solo arrancan si hay un componente externo conectado
// a OSC_IN, y su desaparición hace caer el oscilador (fase F3).
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
#include <cmath>
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

    // --- Fuente externa (solo HSE y LSE) [IR, §4.2; premisa de pines] -------
    // xtal_in es el nodo analógico de OSC_IN (PH0 para HSE, PC14 para LSE).
    // El oscilador solo arranca si hay algo conectado eléctricamente a ese
    // nodo: un cristal, o un reloj externo en modo bypass. Si el nodo queda en
    // alta impedancia el oscilador no alcanza RDY y, si ya estaba en marcha,
    // se considera fallo de la fuente (lo que dispara el CSS del HSE).
    analog_net_if* xtal_in = nullptr;
    // Nivel digitalizado de OSC_IN. En modo bypass permite medir la frecuencia
    // real del reloj externo inyectado (hasta 50 MHz [IR, §4.2]).
    sc_core::sc_signal<bool>* ext_in = nullptr;
    bool needs_source = false;                  // true en HSE y LSE

    SC_CTOR(Oscillator) : gen_("gen") {
        gen_.clk(clk);
        gen_.freq_hz(freq_hz);
        SC_THREAD(ctrl_proc);
        SC_THREAD(source_proc);
        SC_THREAD(measure_proc);
        // El flag RDY lo pueden cambiar el arranque y la vigilancia de la
        // fuente: un único proceso escribe el puerto (convención del modelo).
        SC_METHOD(pub_proc); sensitive << pub_ev_;
    }

    // ¿Hay algo conectado eléctricamente al pin OSC_IN?
    bool source_present() const {
        if (!needs_source) return true;
        return xtal_in && !xtal_in->floating();
    }
    // ¿Es esa fuente la que este MODO necesita? Que haya algo conectado no
    // basta, porque los dos modos piden cosas distintas y el silicio los
    // distingue [IR, §4.2]:
    //
    //   BYPASS = 0 (cristal): el oscilador EXCITA un resonador. Le vale
    //            cualquier componente pasivo colgado del pin.
    //   BYPASS = 1 (reloj externo): el amplificador esta APAGADO y el pin es
    //            una entrada digital. Hace falta una senal que CONMUTE. Un
    //            cristal pasivo aqui no da nada, xxxRDY no sube nunca y el
    //            firmware se queda esperando -o cae en su Error_Handler-.
    //
    // Esa segunda linea es la que faltaba: se daba por buena cualquier fuente
    // presente y se caia a la frecuencia nominal, con lo que un
    // `RCC_HSE_BYPASS` sobre un cristal FUNCIONABA en el modelo y se habria
    // colgado en la placa. El simulador era mas permisivo que el silicio, que
    // es la direccion de error que no queremos: el alumno lo ve funcionar aqui
    // y fallar alli.
    //
    // La distincion se hace con lo unico que hay: la electricidad del pin. Si
    // conmuta, se ha podido medir su frecuencia; si no, es algo pasivo.
    bool fuente_valida() const {
        if (!needs_source) return true;
        if (!source_present()) return false;
        return bypass ? (meas_hz_ > 0.0) : true;
    }
    // Frecuencia efectiva. En bypass es SIEMPRE la medida en el pin: sin medida
    // no hay reloj, y por eso `fuente_valida()` no deja arrancar. En modo
    // cristal es la NOMINAL del resonante, y no la que se mida en el pin: lo
    // que oscila ahi es el propio amplificador contra el cristal, no una senal
    // ajena, y quien manda es el corte del cuarzo.
    double effective_hz() const { return bypass ? meas_hz_ : nominal_hz; }
    bool failed() const { return failed_; }
    void clear_failed() { failed_ = false; }
    // Generación de la onda cuadrada (véase clock_gen.h): se puede apagar
    // conservando la frecuencia publicada.
    void set_waveform(bool on) { gen_.set_waveform(on); }

    // --- Control desde el banco de registros del RCC ------------------------
    void enable(bool en) {
        if (en == on_) return;
        on_ = en;
        ctrl_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool   enabled()  const { return on_; }
    bool   is_ready() const { return ready_; }
    double out_hz()   const { return ready_ ? effective_hz() : 0.0; }
    // Evento de cambio de estado (RDY): el RCC recalcula el árbol al recibirlo.
    const sc_core::sc_event& state_event() const { return state_ev_; }

private:
    ClockGen gen_;
    bool on_ = false, ready_ = false, failed_ = false;
    double meas_hz_ = 0.0;
    sc_core::sc_event ctrl_ev_, state_ev_, pub_ev_;

    void pub_proc() { ready.write(ready_); }
    void publish()  { pub_ev_.notify(sc_core::SC_ZERO_TIME); }

    void ctrl_proc() {
        for (;;) {
            wait(ctrl_ev_);
            if (on_) {
                // Arranque: el oscilador solo alcanza RDY si tiene fuente. Un
                // HSEON sin cristal deja HSERDY a 0 indefinidamente, que es lo
                // que ve el firmware real cuando falta el componente externo.
                wait(sc_core::sc_time(t_startup_s, sc_core::SC_SEC));
                if (!on_) continue;              // se apagó durante el arranque
                if (!fuente_valida()) { failed_ = true; continue; }
                failed_ = false;
                gen_.set_freq(effective_hz());
                ready_ = true;
            } else {
                gen_.set_freq(0.0);
                ready_ = false;
            }
            publish();
            state_ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

    // Vigilancia de la fuente externa: si desaparece (nodo OSC_IN en alta
    // impedancia) el oscilador cae. Para el HSE esto es lo que detecta el CSS.
    void source_proc() {
        for (;;) {
            if (!needs_source || !xtal_in) { wait(state_ev_); continue; }
            wait(xtal_in->value_changed_event() | state_ev_);
            // Si el pin se queda al aire, la MEDIDA deja de valer. Sin esto
            // la frecuencia medida es pegajosa: se retira el reloj externo, se
            // cuelga otra cosa del mismo pin y el bypass arrancaria con la
            // frecuencia del reloj que ya no esta.
            if (!source_present()) meas_hz_ = 0.0;
            if (on_ && ready_ && !fuente_valida()) {
                // La fuente ha desaparecido: el oscilador cae.
                ready_ = false; failed_ = true;
                gen_.set_freq(0.0);
                publish();
                state_ev_.notify(sc_core::SC_ZERO_TIME);
            } else if (on_ && !ready_ && fuente_valida()) {
                // El componente externo aparece (o vuelve) con xxxON ya a 1:
                // el oscilador reintenta el arranque, como haría el silicio.
                ctrl_ev_.notify(sc_core::SC_ZERO_TIME);
            }
        }
    }

    // Medida del periodo del reloj externo en modo bypass: dos flancos de
    // subida consecutivos en OSC_IN dan la frecuencia inyectada.
    void measure_proc() {
        for (;;) {
            if (!ext_in) { wait(state_ev_); continue; }
            wait(ext_in->posedge_event());
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            wait(ext_in->posedge_event());
            const double dt = (sc_core::sc_time_stamp() - t0).to_seconds();
            if (dt <= 0.0) continue;
            const double hz = 1.0 / dt;
            if (std::fabs(hz - meas_hz_) > 0.001 * hz) {
                const bool primera = (meas_hz_ == 0.0);
                meas_hz_ = hz;
                if (bypass && on_ && ready_) {    // reprograma en caliente
                    gen_.set_freq(effective_hz());
                    state_ev_.notify(sc_core::SC_ZERO_TIME);
                } else if (bypass && on_ && !ready_ && primera) {
                    // En bypass, la fuente valida NO es el nodo: es que el pin
                    // conmute. Acaba de empezar a conmutar con xxxON ya puesto,
                    // asi que el oscilador reintenta el arranque -lo mismo que
                    // hace `source_proc` cuando aparece el componente-.
                    ctrl_ev_.notify(sc_core::SC_ZERO_TIME);
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// PLL principal, PLLI2S y PLLSAI [IR, §4.3; RM0390 §6.2.3]
//   fVCO_IN  = fPLL_IN / M      (debe quedar entre 1 y 2 MHz)
//   fVCO_OUT = fVCO_IN  * N     (debe quedar entre 100 y 432 MHz)
//   fP       = fVCO_OUT / P     (SYSCLK, máx 168 MHz)  [P = 2,4,6,8]
//   fQ       = fVCO_OUT / Q     (PLL48CK: USB OTG FS, RNG, SDIO) [Q = 2..15]
//   fR       = fVCO_OUT / R     (solo en el F446: SPDIF-RX, I2S, SAI) [R = 2..7]
// En el PLLI2S del F407 la salida "P" del modelo representa la salida R.
//
// LA SALIDA R NO TIENE GENERADOR DE ONDA, y es deliberado: las otras dos sacan
// un `sc_signal<bool>` porque alguien mide sus flancos —la P es SYSCLK y la Q
// alimenta al USB—, y de la R, hoy, solo se consulta la FRECUENCIA. Añadirle
// una onda cuadrada sería pagar eventos de simulación por un cable que nadie
// mira. El día que el SAI o el SPDIF-RX la necesiten, se le pone.
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

    // Programación desde RCC_PLLCFGR / RCC_PLLI2SCFGR / RCC_PLLSAICFGR
    // [IR, §4.5.2, §4.11.2; RM0390 §6.3.2]. `r = 0` es «este PLL no saca R»,
    // que es el caso de todos los del F407.
    void configure(unsigned m, unsigned n, unsigned p, unsigned q,
                   unsigned r = 0) {
        if (m == m_ && n == n_ && p == p_ && q == q_ && r == r_) return;
        m_ = m; n_ = n; p_ = p; q_ = q; r_ = r;
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
    double out_r_hz()   const { return (ready_ && r_) ? vco_out_hz() / r_ : 0.0; }
    // Rangos legales [IR, §4.3.1]
    bool ranges_ok() const {
        const double vi = vco_in_hz(), vo = vco_out_hz();
        return vi >= 1e6 && vi <= 2e6 && vo >= 100e6 && vo <= 432e6;
    }
    const sc_core::sc_event& state_event() const { return state_ev_; }
    void set_waveform(bool on) { gp_.set_waveform(on); gq_.set_waveform(on); }

private:
    ClockGen gp_, gq_;
    unsigned m_ = 16, n_ = 192, p_ = 2, q_ = 4, r_ = 0;
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
