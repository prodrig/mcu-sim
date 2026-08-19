// =============================================================================
// pad.h — Pad de E/S: frontera analógico (float) <-> digital (bool)
//
// El GPIO (o el pin_mux, para AF) gobierna el pad con PadDrive; el pad actúa
// como driver Thevenin del AnalogNet y digitaliza la tensión del nodo con un
// trigger Schmitt (VIH/VIL con histéresis). Señala niveles fuera de rango
// absoluto. Los pads no soldados en el LQFP100 se instancian con bonded=false
// (el nodo existe pero no hay pin físico). [IR, §2.4, §3; plan P5]
// =============================================================================
#ifndef STM32_PINS_PAD_H
#define STM32_PINS_PAD_H

#include <systemc>
#include "../common/analog_net.h"

namespace stm32 {

// Orden de gobierno del pad (lo escribe el puerto GPIO según MODER/OTYPER/...)
struct PadDrive {
    bool    out    = false;   // valor a conducir (ODR o AF out)
    bool    oe     = false;   // output enable (modo salida / AF salida)
    bool    od     = false;   // open-drain (OTYPER)
    uint8_t pupd   = 0;       // 0: ninguna, 1: pull-up, 2: pull-down (PUPDR)
    uint8_t speed  = 0;       // OSPEEDR (afecta a slew/Rout en fases futuras)
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
    sc_core::sc_out<bool>    din{"din"};        // salida del Schmitt (a IDR / AF in)
    sc_core::sc_out<bool>    din_valid{"din_valid"}; // 0 => nivel indeterminado (X)
    sc_core::sc_out<bool>    out_of_range{"out_of_range"};

    // Lado analógico (nodo del pin físico)
    sc_core::sc_port<analog_net_if> net{"net"};

    // Parámetros eléctricos [IR, §2.4] (ajustables por instancia)
    double vdd    = 3.3;    // alimentación del dominio de E/S
    double r_on   = 25.0;   // Rout del driver push-pull activo [ohm] (aprox.)
    double r_pull = 40e3;   // pull-up/down interno
    bool   bonded = true;   // false: sin pin físico en el encapsulado
    bool   ft_5v  = true;   // tolerante a 5 V

    SC_CTOR(Pad) {
        SC_METHOD(update_drive);
        sensitive << drive;
        dont_initialize();
        SC_METHOD(sample_input);      // sensibilidad al nodo: en end_of_elaboration
    }

    void end_of_elaboration() override {
        id_pad_  = net->register_driver("pad");
        id_pull_ = net->register_driver("pull");
        // TODO(F3): proceso dinámico (sc_spawn) sensible a
        //           net->value_changed_event() para re-muestrear la entrada.
    }

private:
    void update_drive() {
        const PadDrive d = drive.read();
        // Driver principal
        if (d.analog || !d.oe)             net->set_hiz(id_pad_);
        else if (d.od && d.out)            net->set_hiz(id_pad_);        // OD: '1' = Hi-Z
        else                               net->set_drive(id_pad_,
                                              d.out ? float(vdd) : 0.0f, float(r_on));
        // Pull-up / pull-down (desconectados en analógico)
        if (d.analog || d.pupd == 0)       net->set_hiz(id_pull_);
        else                               net->set_drive(id_pull_,
                                              d.pupd == 1 ? float(vdd) : 0.0f,
                                              float(r_pull));
    }

    void sample_input() {
        // TODO(F3): sensibilidad a net->value_changed_event() vía sc_spawn y
        //           muestreo síncrono con el reloj AHB del puerto [IR, §3.3.1].
        const double v = bonded ? net->voltage() : 0.0;
        const double vih = 0.7 * vdd, vil = 0.3 * vdd;      // umbrales Schmitt
        out_of_range.write(v < -0.3 || v > (ft_5v ? 5.5 : vdd + 0.3));
        if (drive.read().analog) { din.write(false); din_valid.write(true); return; }
        if (v >= vih)      { din.write(true);  din_valid.write(true); }
        else if (v <= vil) { din.write(false); din_valid.write(true); }
        else               { din_valid.write(false); }      // zona indeterminada (X)
    }

    int id_pad_ = -1, id_pull_ = -1;
};

} // namespace stm32
#endif // STM32_PINS_PAD_H
