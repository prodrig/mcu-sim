// =============================================================================
// pad.h — Pad de E/S: frontera analógico (float) <-> digital (bool)
//
// El GPIO (o el pin_mux, para AF) gobierna el pad con PadDrive; el pad actúa
// como driver Thevenin del AnalogNet y digitaliza la tensión del nodo con un
// trigger Schmitt con histéresis. Señala niveles fuera de rango absoluto,
// nodo flotante y sobrecorriente. Los pads no soldados en el LQFP100 se
// instancian con bonded=false (el nodo existe pero no hay pin físico).
// [IR, §2.4, §3.3, §3.5; plan P5]
//
// Fase F3 — modelo eléctrico completo:
//   * proceso sensible al AnalogNet: la entrada se re-muestrea cuando cambia
//     la tensión del nodo, no solo cuando cambia el gobierno del pad;
//   * trigger Schmitt con histéresis: conmuta en VT+/VT- y mantiene el último
//     nivel dentro de la banda; din_valid señala la banda no garantizada
//     (VIL..VIH de la hoja de características) y el nodo flotante;
//   * impedancia de salida y retardo de propagación según OSPEEDR [IR, §3.4.3];
//   * open-drain real: el N-MOS conduce a 0 y el '1' queda en alta impedancia;
//   * pull-up/pull-down de 40 kohm, forzados a off en modo analógico;
//   * vigilancia de corriente por pin (25 mA, 3 mA en PC13/14/15) [IR, §3.2].
// =============================================================================
#ifndef STM32_PINS_PAD_H
#define STM32_PINS_PAD_H

#include <systemc>
#include <cmath>
#include "../common/analog_net.h"

namespace stm32 {

// Orden de gobierno del pad (lo escribe el puerto GPIO según MODER/OTYPER/...)
struct PadDrive {
    bool    out    = false;   // valor a conducir (ODR o AF out)
    bool    oe     = false;   // output enable (modo salida / AF salida)
    bool    od     = false;   // open-drain (OTYPER)
    uint8_t pupd   = 0;       // 0: ninguna, 1: pull-up, 2: pull-down (PUPDR)
    uint8_t speed  = 0;       // OSPEEDR: 0 low, 1 medium, 2 high, 3 very high
    bool    analog = false;   // modo analógico: buffers y Schmitt desconectados

    bool operator==(const PadDrive& o) const {
        return out == o.out && oe == o.oe && od == o.od && pupd == o.pupd &&
               speed == o.speed && analog == o.analog;
    }
};
inline std::ostream& operator<<(std::ostream& os, const PadDrive& d) {
    return os << "{o" << d.out << " oe" << d.oe << " od" << d.od
              << " p" << int(d.pupd) << " s" << int(d.speed) << " a" << d.analog << "}";
}
inline void sc_trace(sc_core::sc_trace_file* tf, const PadDrive& d, const std::string& n) {
    sc_core::sc_trace(tf, d.out, n + ".out");
    sc_core::sc_trace(tf, d.oe,  n + ".oe");
}

SC_MODULE(Pad) {
    // Lado digital (hacia GPIO / pin_mux)
    sc_core::sc_in<PadDrive> drive{"drive"};
    sc_core::sc_out<bool>    din{"din"};             // salida del Schmitt
    sc_core::sc_out<bool>    din_valid{"din_valid"}; // 0 => nivel no garantizado
    sc_core::sc_out<bool>    out_of_range{"out_of_range"};

    // Lado analógico (nodo del pin físico)
    sc_core::sc_port<analog_net_if> net{"net"};

    // --- Parámetros eléctricos [IR, §2.4, §3.5] (ajustables por instancia) ---
    double vdd    = 3.3;    // alimentación del dominio de E/S
    double r_pull = 40e3;   // pull-up/down interno (30-50 k, típico 40 k)
    bool   bonded = true;   // false: sin pin físico en el encapsulado
    bool   ft_5v  = true;   // tolerante a 5 V (no en modo analógico/oscilador)
    double i_max  = 25e-3;  // corriente máxima por pin (3 mA en PC13/14/15)

    // ⚠ NO DISPONIBLE EN LAS FUENTES con valor numérico: la hoja de
    // características da la *frecuencia máxima de conmutación* por nivel de
    // OSPEEDR (2 / 25 / 50 / 100 MHz [IR, §3.2, §3.4.3]), no la impedancia ni
    // el tiempo de transición. Se modelan como parámetros del modelo:
    //   * r_on_speed: impedancia del driver activo, menor cuanto más rápido;
    //   * t_pd_speed: retardo de propagación de la salida, coherente con la
    //     frecuencia máxima de cada nivel (aprox. 1/(10*f_max)).
    double r_on_speed[4] = {55.0, 40.0, 30.0, 25.0};          // [ohm]
    double t_pd_speed[4] = {50e-9, 4e-9, 2e-9, 1e-9};         // [s]

    SC_CTOR(Pad) {
        SC_THREAD(pad_proc);              // sensibilidad dinámica: net + drive
        SC_METHOD(drive_apply_proc);      // aplica el gobierno tras t_pd
        sensitive << apply_ev_;
        dont_initialize();
    }

    void end_of_elaboration() override {
        id_pad_  = net->register_driver("pad");
        id_pull_ = net->register_driver("pull");
        net->set_hiz(id_pad_);
        net->set_hiz(id_pull_);
    }

    // --- Observación desde el banco de pruebas -------------------------------
    float  voltage()     const { return net->voltage(); }
    float  current()     const { return net->current(id_pad_) + net->current(id_pull_); }
    bool   overcurrent() const { return over_i_; }
    bool   is_floating() const { return net->floating(); }
    // Umbrales del trigger Schmitt vigentes [IR, §3.5]
    double vt_rise() const { return 0.5 * vdd + 0.5 * v_hyst(); }
    double vt_fall() const { return 0.5 * vdd - 0.5 * v_hyst(); }
    double v_hyst()  const { double h = 0.10 * vdd; return h < 0.2 ? 0.2 : h; }

private:
    // -----------------------------------------------------------------------
    // Salida: el gobierno digital se traduce en dos drivers Thevenin sobre el
    // nodo (buffer principal y resistencia de pull). El cambio se aplica tras
    // el retardo de propagación del nivel de OSPEEDR seleccionado.
    // -----------------------------------------------------------------------
    void drive_apply_proc() {
        const PadDrive d = pending_;
        const unsigned s = d.speed & 3u;
        // Buffer principal [IR, §3.3.2]
        if (d.analog || !d.oe)   net->set_hiz(id_pad_);       // entrada/analógico
        else if (d.od && d.out)  net->set_hiz(id_pad_);       // open-drain a '1'
        else                     net->set_drive(id_pad_,
                                     d.out ? float(vdd) : 0.0f,
                                     float(r_on_speed[s]));
        // Pull-up / pull-down: desconectados en modo analógico [IR, §3.3.4]
        if (d.analog || d.pupd == 0 || d.pupd == 3) net->set_hiz(id_pull_);
        else net->set_drive(id_pull_, d.pupd == 1 ? float(vdd) : 0.0f,
                            float(r_pull));
        applied_ = d;
    }

    // -----------------------------------------------------------------------
    // Proceso principal: reacciona al gobierno del GPIO y a la tensión del nodo
    // -----------------------------------------------------------------------
    void pad_proc() {
        din.write(false); din_valid.write(false); out_of_range.write(false);
        pending_ = drive.read();
        apply_ev_.notify(sc_core::SC_ZERO_TIME);
        for (;;) {
            wait(drive.value_changed_event() | net->value_changed_event());
            const PadDrive d = drive.read();
            if (!(d == pending_)) {
                pending_ = d;
                // Retardo de propagación de la salida: solo tiene sentido si el
                // buffer está activo; los cambios de configuración de entrada se
                // aplican sin retardo apreciable.
                const bool out_active = d.oe && !d.analog;
                apply_ev_.notify(out_active
                                     ? sc_core::sc_time(t_pd_speed[d.speed & 3u],
                                                        sc_core::SC_SEC)
                                     : sc_core::SC_ZERO_TIME);
            }
            sample_input();
        }
    }

    // -----------------------------------------------------------------------
    // Entrada: trigger Schmitt con histéresis + validación de rango absoluto
    // -----------------------------------------------------------------------
    void sample_input() {
        const double v  = net->voltage();
        const bool   fl = net->floating();
        const PadDrive d = drive.read();

        // Rango absoluto: los pines FT admiten hasta 5.5 V salvo en modo
        // analógico o de oscilador, donde el límite es VDD+0.3 [IR, §2.1 nota 4]
        const double v_max = (ft_5v && !d.analog) ? 5.5 : vdd + 0.3;
        const bool oor = !fl && (v < -0.3 || v > v_max);
        if (oor != oor_) { oor_ = oor; out_of_range.write(oor); }
        if (oor && !oor_warned_) {
            oor_warned_ = true;
            SC_REPORT_WARNING("pad", "tension fuera del rango absoluto del pin");
        }

        // Vigilancia de corriente por pin [IR, §3.2, §2.4]
        const double i = std::fabs(double(net->current(id_pad_)));
        const bool over = (i > i_max);
        if (over && !over_i_)
            SC_REPORT_WARNING("pad", "corriente de pin por encima del maximo");
        over_i_ = over;

        // Modo analógico: buffer de entrada y Schmitt desconectados; el IDR
        // asociado lee siempre 0 [IR, §3.3.4].
        if (d.analog) {
            if (level_ || !valid_) { level_ = false; din.write(false); }
            if (!valid_) { valid_ = true; din_valid.write(true); }
            return;
        }

        // Nodo flotante (ningún driver lo sujeta): el nivel no es observable.
        // El Schmitt conserva su último estado, pero se marca como no válido.
        if (fl) {
            if (valid_) { valid_ = false; din_valid.write(false); }
            return;
        }

        // Trigger Schmitt: conmuta en VT+ subiendo y en VT- bajando; dentro de
        // la banda mantiene el nivel anterior.
        const bool nl = level_ ? (v > vt_fall()) : (v >= vt_rise());
        if (nl != level_ || !level_written_) {
            level_ = nl; level_written_ = true; din.write(nl);
        }
        // Validez: la hoja de características solo garantiza el nivel por
        // debajo de VIL y por encima de VIH [IR, §3.5].
        const bool ok = (v <= 0.3 * vdd) || (v >= 0.7 * vdd);
        if (ok != valid_) { valid_ = ok; din_valid.write(ok); }
    }

    int  id_pad_ = -1, id_pull_ = -1;
    bool level_ = false, valid_ = false, oor_ = false;
    bool level_written_ = false, oor_warned_ = false, over_i_ = false;
    PadDrive pending_{}, applied_{};
    sc_core::sc_event apply_ev_;
};

} // namespace stm32
#endif // STM32_PINS_PAD_H
