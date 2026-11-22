// =============================================================================
// tmp235.h — El sensor de temperatura analógico TMP235 (M7 de
// doc/analisis_msp430.md): el U1 de la LP-MSP430FR2476
//
// Con VDD, su salida es VOUT = 500 mV + 10 mV/°C × T, de -40 a 150 °C (de
// 100 mV a 2 V) [hoja del TMP235, SBOS902, «VOUT = TC × (TA - TINFL) +
// VOFFS» con TC 10 mV/°C, VOFFS 500 mV y TINFL 0 °C en ese tramo]. Sin VDD
// (J9 quitado), la salida queda suelta.
//
// Cómo: un equivalente de Thévenin en la salida, con la resistencia de
// salida de un seguidor pequeño [de modelo: 10 ohm; la hoja da la carga
// capacitiva que aguanta, no una resistencia]. Se rehace cuando cambian la
// temperatura o VDD.
//
// Mando: temperatura (°C). Observables: temperatura y vout (V).
// =============================================================================
#ifndef STM32_PARTS_TMP235_H
#define STM32_PARTS_TMP235_H

#include <systemc>
#include "../common/analog_net.h"
#include "part_base.h"

namespace stm32 {

SC_MODULE(Tmp235), public ExtPartBase {
    static constexpr double R_SAL = 10.0;
    Tmp235(sc_core::sc_module_name nm, analog_net_if& vdd, analog_net_if& vout, double grados = 25.0)
        : sc_core::sc_module(nm), ExtPartBase("Tmp235", nm), vdd_(vdd), o_(vout), t_(grados) {
        add_ref("vdd", vdd);
        id_o_ = add_pin("vout", vout, "tmp");
        SC_HAS_PROCESS(Tmp235);
        SC_THREAD(run);
    }
    void pon_temperatura(double t) { t_ = t < -40 ? -40 : t > 150 ? 150 : t; ev_.notify(sc_core::SC_ZERO_TIME); }
    double temperatura() const { return t_; }
    bool alimentado() const { return !vdd_.floating() && vdd_.voltage() > 2.3f; }   // VDD de 2,3 a 5,5 V
    double vout() const { return 0.5 + 0.010 * t_; }

    unsigned n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        return i == 0 ? Observable{"temperatura", "C", -40.f, 150.f, true} : Observable{"vout", "V", 0.f, 2.f, false};
    }
    float valor_observable(unsigned i) const override { return i == 0 ? float(t_) : float(vout()); }
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override { return {"temperatura", Mando::Continuo, -40.f, 150.f}; }
    float valor_mando(unsigned) const override { return float(t_); }
    void acciona(unsigned, float v) override { pon_temperatura(v); }

private:
    analog_net_if &vdd_, &o_;
    int id_o_ = -1;
    double t_;
    sc_core::sc_event ev_;
    float v_ = 1e30f;
    void run() {
        for (;;) {
            if (!conectada_ || !alimentado()) { o_.set_hiz(id_o_); v_ = 1e30f; }
            else {
                const float v = float(vout());
                if (v != v_) { v_ = v; o_.set_drive(id_o_, v, float(R_SAL)); }
            }
            wait(ev_ | vdd_.value_changed_event() | evento_conexion());
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_TMP235_H
