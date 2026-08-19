// =============================================================================
// clock_gen.h — Generador de reloj digital con frecuencia reprogramable
//
// sc_clock no permite cambiar el periodo en tiempo de simulación; los relojes
// del F407 sí cambian (conmutación de SW, reprogramación de PLL/prescalers).
// ClockGen genera una onda cuadrada bool y publica su frecuencia en Hz para
// los módulos que anotan tiempos sin contar flancos. freq = 0 => reloj parado
// (gating): la línea queda a nivel bajo. [plan P6; IR, §4]
//
// Fase F1: el nivel se mantiene en una variable interna (no se relee el puerto)
// para que una reprogramación a mitad de semiperiodo no genere un pulso corto
// espurio; al reprogramar, el divisor se reinicia desde el nivel actual, que es
// el comportamiento de un prescaler síncrono que recibe un nuevo módulo.
// =============================================================================
#ifndef STM32_COMMON_CLOCK_GEN_H
#define STM32_COMMON_CLOCK_GEN_H

#include <systemc>

namespace stm32 {

SC_MODULE(ClockGen) {
    sc_core::sc_out<bool>   clk{"clk"};
    sc_core::sc_out<double> freq_hz{"freq_hz"};   // observabilidad / anotación

    SC_CTOR(ClockGen) {
        SC_THREAD(gen_proc);
    }

    // Reprograma la frecuencia (0.0 = parado). Llamable en tiempo de simulación
    // y también durante la elaboración (antes de arrancar el proceso).
    void set_freq(double hz) {
        if (hz < 0.0) hz = 0.0;
        if (hz == hz_) return;                 // sin cambio: no perturbar la fase
        hz_ = hz;
        reconf_.notify(sc_core::SC_ZERO_TIME);
    }
    double freq()    const { return hz_; }
    bool   stopped() const { return hz_ <= 0.0; }

    // Generación de la onda cuadrada. Con `false` el generador sigue
    // publicando freq_hz pero deja la línea quieta.
    //
    // Motivo: emitir la onda de HCLK a 168 MHz cuesta dos activaciones de
    // proceso por periodo (unos 3.4e8 por segundo simulado), lo que domina el
    // tiempo de simulación en cargas largas de CPU. Los bloques que solo
    // necesitan la frecuencia (SysTick, prescalers, anotación de latencias)
    // trabajan con freq_hz y programan sus propios eventos; la onda solo hace
    // falta donde es observable (pines MCO, salidas de GPIO, captura de
    // temporizadores). El banco de pruebas la enciende cuando la mide.
    void set_waveform(bool on) {
        if (on == wave_) return;
        wave_ = on;
        reconf_.notify(sc_core::SC_ZERO_TIME);
    }
    bool waveform() const { return wave_; }

    // Periodo completo del reloj; SC_ZERO_TIME si está parado.
    sc_core::sc_time period() const {
        return hz_ > 0.0 ? sc_core::sc_time(1.0e12 / hz_, sc_core::SC_PS)
                         : sc_core::SC_ZERO_TIME;
    }

private:
    void gen_proc() {
        level_ = false;
        clk.write(false);
        freq_hz.write(hz_);
        for (;;) {
            if (hz_ <= 0.0 || !wave_) {       // parado (gating) o sin onda
                if (level_) { level_ = false; clk.write(false); }
                freq_hz.write(hz_ > 0.0 ? hz_ : 0.0);
                wait(reconf_);
                continue;
            }
            freq_hz.write(hz_);
            const sc_core::sc_time half(0.5e12 / hz_, sc_core::SC_PS);
            level_ = !level_;
            clk.write(level_);
            wait(half, reconf_);              // medio periodo o reprogramación
        }
    }

    double            hz_    = 0.0;
    bool              level_ = false;
    bool              wave_  = true;
    sc_core::sc_event reconf_;
};

// ---------------------------------------------------------------------------
// Prescaler entero: no genera onda propia, solo traduce un factor de división
// a la frecuencia que debe programarse en el ClockGen de destino. Se mantiene
// como función libre porque los divisores del RCC son campos de registro y no
// bloques con interfaz propia.
// ---------------------------------------------------------------------------
inline double clk_divide(double f_in_hz, unsigned div) {
    return (div == 0 || f_in_hz <= 0.0) ? 0.0 : f_in_hz / double(div);
}

} // namespace stm32
#endif // STM32_COMMON_CLOCK_GEN_H
