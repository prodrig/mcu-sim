// =============================================================================
// fotodiodo.h — Un fotodiodo en modo fotoconductivo o de tensión cero (M7 de
// doc/analisis_msp430.md): el D1 (SFH 2701) de la MSP-EXP430FR2311
//
// Con luz, el fotodiodo da una corriente del cátodo al ánodo por fuera: la
// que entra por el cátodo, proporcional a la luz (`na_por_lux` nA por lux).
// Es lo que mide el SAC0 de la FR2311 en transimpedancia: el cátodo a OA0-, el
// ánodo a masa y a OA0+, y R3 de OA0O a OA0- [SLAU664B, esquema, hoja 2].
//
// Cómo: un equivalente de Thévenin en el cátodo, V = Vánodo - I·R con R = 1
// Gohm, que saca I del nodo sea cual sea su tensión (dentro de lo que una R
// tan grande deja). Se rehace cuando cambian la luz o la tensión del ánodo.
// [de modelo] La corriente oscura, la capacidad y la respuesta espectral no
// están; la sensibilidad, una por defecto del orden del SFH 2701 a la luz
// ambiente.
//
// Mando: luz (lux, de 0 a 100 000). Observables: luz y corriente (µA).
// =============================================================================
#ifndef STM32_PARTS_FOTODIODO_H
#define STM32_PARTS_FOTODIODO_H

#include <systemc>
#include <cmath>
#include "../common/analog_net.h"
#include "part_base.h"

namespace stm32 {

SC_MODULE(Fotodiodo), public ExtPartBase {
    static constexpr double R_EQ = 1e9;
    Fotodiodo(sc_core::sc_module_name nm, analog_net_if& anodo, analog_net_if& catodo, double na_por_lux = 5.0, double lux = 0.0)
        : sc_core::sc_module(nm), ExtPartBase("Fotodiodo", nm), a_(anodo), k_(catodo),
          s_(na_por_lux * 1e-9), lux_(lux) {
        add_ref("anodo", anodo);
        id_k_ = add_pin("catodo", catodo, "fd");
        SC_HAS_PROCESS(Fotodiodo);
        SC_THREAD(run);
    }
    void pon_luz(double lux) { lux_ = lux < 0 ? 0 : lux; ev_.notify(sc_core::SC_ZERO_TIME); }
    double luz() const { return lux_; }
    double corriente() const { return conectada_ ? s_ * lux_ : 0.0; }

    unsigned n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        return i == 0 ? Observable{"luz", "lx", 0.f, 2000.f, true} : Observable{"corriente", "uA", 0.f, 10.f, false};
    }
    float valor_observable(unsigned i) const override { return i == 0 ? float(lux_) : float(corriente() * 1e6); }
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override { return {"luz", Mando::Continuo, 0.f, 100000.f}; }
    float valor_mando(unsigned) const override { return float(lux_); }
    void acciona(unsigned, float v) override { pon_luz(v); }

private:
    analog_net_if &a_, &k_;
    int id_k_ = -1;
    double s_, lux_;
    sc_core::sc_event ev_;
    float v_ = 1e30f;
    void run() {
        for (;;) {
            if (!conectada_) { k_.set_hiz(id_k_); v_ = 1e30f; }
            else {
                const double va = a_.floating() ? 0.0 : double(a_.voltage());
                const float v = float(va - corriente() * R_EQ);
                if (v != v_) { v_ = v; k_.set_drive(id_k_, v, float(R_EQ)); }
            }
            wait(ev_ | a_.value_changed_event() | evento_conexion());
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_FOTODIODO_H
