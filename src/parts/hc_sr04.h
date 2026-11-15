// =============================================================================
// hc_sr04.h — El medidor de distancia por ultrasonidos HC-SR04
//
// E11 de doc/analisis_esp32_riscv.md: el que miden los ejemplos
// mcpwm_capture_hc_sr04 (con la captura del MCPWM) y gptimer_capture_hc_sr04
// (con el ETM y la captura del GPTimer) de ESP-IDF. [Hoja de datos del
// HC-SR04, ElecFreaks]:
//   * TRIG: un pulso alto de al menos 10 µs empieza una medida;
//   * al bajar TRIG, el módulo manda ocho periodos de 40 kHz (200 µs) y pone
//     ECHO alto; ECHO baja cuando vuelve el eco: su anchura es el tiempo de
//     ida y vuelta del sonido, 2·d / 343 m/s (58 µs por centímetro);
//   * sin eco (más allá de unos 4 m), ECHO se queda alto 38 ms;
//   * ECHO sale a 5 V en el módulo de verdad; aquí, a `vdd` (3,3 V: el que se
//     vende para 3,3 V, o con su divisor) [de modelo].
// `distancia_cm` es el mando: la distancia al obstáculo.
// =============================================================================
#ifndef STM32_PARTS_HC_SR04_H
#define STM32_PARTS_HC_SR04_H

#include <systemc>
#include "ext_parts.h"

namespace stm32 {

class HcSr04 : public sc_core::sc_module, public ExtPartBase {
public:
    double distancia_cm;
    static constexpr double V_SONIDO = 343.0;         // m/s, a 20 °C

    HcSr04(sc_core::sc_module_name nm, analog_net_if& trig, analog_net_if& echo,
           double distancia = 20.0, double vdd = 3.3)
        : sc_core::sc_module(nm), ExtPartBase("HcSr04", nm), distancia_cm(distancia),
          trig_(&trig), echo_(&echo), vdd_(vdd) {
        add_pin("trig", trig, "hc_sr04");
        id_echo_ = add_pin("echo", echo, "hc_sr04");
        echo_->set_drive(id_echo_, 0.0f, 50.0f);
        SC_HAS_PROCESS(HcSr04);
        SC_THREAD(proc);
    }
    ~HcSr04() override { echo_->set_hiz(id_echo_); }
    void set_enabled(bool on) override {
        conectada_ = on;
        if (on) echo_->set_drive(id_echo_, 0.0f, 50.0f); else echo_->set_hiz(id_echo_);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    unsigned medidas() const { return n_; }
    // El ancho de ECHO para la distancia de ahora
    sc_core::sc_time eco() const {
        if (distancia_cm > 400.0 || distancia_cm < 0) return sc_core::sc_time(38, sc_core::SC_MS);
        return sc_core::sc_time(2.0 * distancia_cm / 100.0 / V_SONIDO, sc_core::SC_SEC);
    }

    unsigned   n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"medidas", "", 0.f, 1e6f, false}; }
    float valor_observable(unsigned) const override { return float(n_); }
    unsigned n_mandos() const override { return 1; }
    Mando    mando(unsigned) const override { return {"distancia_cm", Mando::Continuo, 2.f, 400.f}; }
    float valor_mando(unsigned) const override { return float(distancia_cm); }
    void  acciona(unsigned, float v) override { distancia_cm = v; }

private:
    analog_net_if *trig_, *echo_;
    double vdd_;
    int id_echo_ = -1;
    unsigned n_ = 0;
    sc_core::sc_event ev_;
    bool alto(analog_net_if& n) const { return !n.floating() && n.voltage() > 0.5f * float(vdd_); }

    void proc() {
        for (;;) {
            // Un pulso alto de al menos 10 µs en TRIG
            wait(trig_->value_changed_event() | ev_);
            if (!conectada_ || !alto(*trig_)) continue;
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            while (alto(*trig_)) wait(trig_->value_changed_event());
            if (sc_core::sc_time_stamp() - t0 < sc_core::sc_time(10, sc_core::SC_US) || !conectada_) continue;
            wait(200, sc_core::SC_US);                         // la ráfaga de 40 kHz
            ++n_;
            echo_->set_drive(id_echo_, float(vdd_), 50.0f);
            wait(eco());
            if (conectada_) echo_->set_drive(id_echo_, 0.0f, 50.0f);
        }
    }
};

} // namespace stm32
#endif // STM32_PARTS_HC_SR04_H
