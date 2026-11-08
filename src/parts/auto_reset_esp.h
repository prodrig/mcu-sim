// =============================================================================
// auto_reset_esp.h — AutoResetEsp: el circuito de reinicio automático de las
//                    placas de Espressif (DTR/RTS del puente USB-UART)
//
// E5 de doc/analisis_esp32_riscv.md (D2). Es lo que hay entre el CP2102N y el
// chip en una DevKitM-1 o una DevKitC [esquemáticos de Espressif]: dos NPN
// cruzados y el RC del pin de habilitación.
//
//   Q1: base a DTR (por 10 kohm), emisor a RTS, colector a EN (CHIP_PU)
//   Q2: base a RTS (por 10 kohm), emisor a DTR, colector a IO0 (GPIO9)
//   R, C: EN sube por R (10 kohm) a VDD y tiene C (1 uF) a masa
//
// Un transistor conduce cuando su base está más de VBE (0,6 V) por encima de
// su emisor. Así, con las líneas en reposo (las dos altas) o activas a la vez
// (las dos bajas) no pasa nada; RTS sola activa (baja) tira de EN; DTR sola
// activa, de IO0. Es lo que hace `esptool` (classic reset):
//
//   DTR=0 RTS=1  ->  EN a masa (reset)
//   DTR=1 RTS=0  ->  EN sube por el RC con IO0 a masa: arranque en descarga
//   DTR=0        ->  IO0 suelto
//
// EL RC ES LO QUE IMPORTA. EN no sube de golpe: sube con tau = R C (10 ms) y
// el chip guarda los pines de arranque al cruzar el umbral de CHIP_PU, así
// que IO0 tiene que seguir abajo un buen rato después de soltar RTS. Aquí el
// condensador es una fuente que sigue la exponencial, con la impedancia de R
// mientras carga; un pulsador de RESET a masa lo descarga, como en la placa.
//
// Los niveles: DTR y RTS son las patillas del puente (PuenteSerie, `dtr` y
// `rts`): altas en reposo, bajas activas, como en un adaptador TTL.
// =============================================================================
#ifndef STM32_PARTS_AUTO_RESET_ESP_H
#define STM32_PARTS_AUTO_RESET_ESP_H

#include <systemc>
#include <cmath>
#include "../common/analog_net.h"
#include "part_base.h"

namespace stm32 {

SC_MODULE(AutoResetEsp), public ExtPartBase {
    AutoResetEsp(sc_core::sc_module_name nm, analog_net_if& dtr, analog_net_if& rts,
                 analog_net_if& en, analog_net_if& io0, double r = 10e3, double c = 1e-6,
                 double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("AutoResetEsp", nm), dtr_(dtr), rts_(rts),
          en_(en), io0_(io0), r_(r), c_(c), vdd_(vdd) {
        add_ref("dtr", dtr);
        add_ref("rts", rts);
        id_en_ = add_pin("en", en, "autoreset_en");
        id_io0_ = add_pin("io0", io0, "autoreset_io0");
        SC_HAS_PROCESS(AutoResetEsp);
        SC_THREAD(run);
    }

    double tau() const { return r_ * c_; }
    double v_en() const { return v_; }
    bool q1() const { return conduce(dtr_, rts_); }     // EN a masa
    bool q2() const { return conduce(rts_, dtr_); }     // IO0 a masa
    uint64_t resets() const { return n_resets_; }

    unsigned   n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"en", "V", 0.f, 3.3f, true};
        if (i == 1) return {"reset", "", 0.f, 1.f, false};
        return {"descarga", "", 0.f, 1.f, false};
    }
    float valor_observable(unsigned i) const override {
        return i == 0 ? float(v_) : i == 1 ? (q1() ? 1.f : 0.f) : (q2() ? 1.f : 0.f);
    }

private:
    static constexpr double VBE = 0.6, R_SAT = 20.0;
    analog_net_if &dtr_, &rts_, &en_, &io0_;
    int id_en_ = -1, id_io0_ = -1;
    double r_, c_, vdd_, v_ = 0.0;
    uint64_t n_resets_ = 0;

    static bool conduce(const analog_net_if& base, const analog_net_if& emisor) {
        return double(base.voltage()) - double(emisor.voltage()) > VBE;
    }
    void run() {
        const sc_core::sc_time paso(tau() / 40.0, sc_core::SC_SEC);
        bool q1_antes = false;
        sc_core::sc_time t_ant = sc_core::sc_time_stamp();
        for (;;) {
            const double dt = (sc_core::sc_time_stamp() - t_ant).to_seconds();
            t_ant = sc_core::sc_time_stamp();
            const bool q1n = q1();
            if (q1n && !q1_antes) ++n_resets_;
            q1_antes = q1n;
            if (q1n) {
                v_ = 0.0;                                       // se descarga por Q1
                en_.set_drive(id_en_, 0.05f, float(R_SAT));
            } else {
                // Algo de fuera (un pulsador) lo tiene abajo: el condensador
                // se va con él
                bool flota = false;
                const double ext = double(en_.voltage_excluding(id_en_, flota));
                if (!flota && ext < v_ - 0.3) v_ = ext;
                else v_ = vdd_ - (vdd_ - v_) * std::exp(-dt / tau());
                en_.set_drive(id_en_, float(v_), float(v_ > 0.99 * vdd_ ? r_ : r_ / 10.0));
            }
            if (q2()) io0_.set_drive(id_io0_, 0.05f, float(R_SAT));
            else io0_.set_hiz(id_io0_);
            if (!q1n && v_ < 0.99 * vdd_) wait(paso, dtr_.value_changed_event() | rts_.value_changed_event());
            else wait(dtr_.value_changed_event() | rts_.value_changed_event() | en_.value_changed_event());
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_AUTO_RESET_ESP_H
