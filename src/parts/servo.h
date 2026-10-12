// =============================================================================
// servo.h — Un servo de modelismo, como el SG90: tres hilos y un pulso
//
// Un motor con su reductora y, dentro, un potenciómetro y un circuito que
// compara la posición del eje con la que pide el pulso de la señal: la ANCHURA
// del pulso, que llega cada ~20 ms, es la posición [SG90, TowerPro; Handson
// Technology, «SG90 Micro Servo»]. Tres contactos:
//
//   vcc     la alimentación del motor y del circuito: 4,0 a 7,2 V en el SG90
//   gnd     la masa
//   pwm     la señal: un pulso alto cada `periodo_ms`, y su anchura es la
//           posición. Entrada de alta impedancia; vale con 3,3 V
//
// LO QUE HACE, como el de verdad:
//
//   * mide la anchura de cada pulso, y la lleva a un ángulo: `pulso_min_ms`
//     es `angulo_min` y `pulso_max_ms` es `angulo_max`, en línea recta. Un
//     pulso fuera de ese rango lleva al extremo -más allá están los topes de
//     la reductora- y se dice. Uno fuera de lo que el circuito reconoce como
//     pulso (0,3 a 3 ms) se ignora, y se dice;
//   * va hacia el ángulo pedido a su VELOCIDAD, que depende de la TENSIÓN:
//     `velocidad="0.12@4.8 0.11@6"` son los segundos por cada 60 grados a
//     cada tensión, y entre ellas, en línea recta; con un solo punto, la
//     velocidad es proporcional a la tensión. Por debajo de `v_min` no puede
//     moverse;
//   * tiene BANDA MUERTA: una diferencia de pulso menor que `banda_muerta_us`
//     no lo mueve (10 µs en el SG90);
//   * si deja de llegar señal -tres periodos sin pulso-, el analógico SUELTA
//     el motor y se queda donde esté (`sin_senal="suelta"`); uno digital lo
//     mantiene (`"mantiene"`);
//   * gasta lo que gasta: poco quieto, más moviéndose, y mucho si se le
//     sujeta el eje -el mando `bloquear`, que es la mano que lo sujeta-. Es
//     una carga de verdad sobre su VCC: una fuente floja se hunde, y con ella
//     la velocidad;
//   * y al arrancar, el eje está DONDE SE QUEDÓ: en un ángulo cualquiera del
//     rango (`posicion_inicial="aleatoria"`), distinto cada vez salvo que se
//     fije la `semilla`. El primer pulso lo lleva a su sitio, y se ve llegar.
//
// DE GIRO CONTINUO (`continuo="si"`): el pulso ya no es una posición sino una
// velocidad. El del centro del rango lo para; `pulso_max_ms`, a toda
// velocidad en el sentido de las agujas del reloj; `pulso_min_ms`, al
// contrario. El ángulo da vueltas, de 0 a 360.
//
// El ÁNGULO va en grados, positivos en el sentido de las agujas del reloj
// mirando el eje desde arriba. La ventana gira el aspa con él (el efecto
// `angulo`). El dibujo -el cuerpo, de color `color`, y el aspa, de tipo
// `aspa`- lo eligen las variantes de la placa (common/svg_variantes.h).
// =============================================================================
#ifndef STM32_PARTS_SERVO_H
#define STM32_PARTS_SERVO_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "ext_parts.h"

namespace stm32 {

// Los parámetros del servo, con los valores del SG90
struct ConfigServo {
    double periodo_ms = 20.0;          // el periodo que espera de la señal
    double angulo_min = -90.0, angulo_max = 90.0;
    double pulso_min_ms = 1.0, pulso_max_ms = 2.0;
    bool   continuo = false;
    // Segundos por cada 60 grados, a cada tensión: {tensión, segundos}
    std::vector<std::pair<double, double>> velocidad{{4.8, 0.12}};
    double v_min = 4.0, v_max = 7.2;   // donde se mueve, y lo que aguanta
    double banda_muerta_us = 10.0;
    bool   mantiene = false;           // sin señal: false suelta, true mantiene
    double i_reposo_ma = 6.0, i_marcha_ma = 150.0, i_bloqueo_ma = 650.0;
    bool   inicial_aleatoria = true;
    double posicion_inicial = 0.0;
    uint32_t semilla = 0;              // 0: distinta cada vez
};

SC_MODULE(Servo), public ExtPartBase {
    static constexpr double V_NOMINAL = 5.0;      // a la que se calcula la carga

    Servo(sc_core::sc_module_name nm, analog_net_if& vcc, analog_net_if& gnd,
          analog_net_if& pwm, const ConfigServo& c = ConfigServo())
        : sc_core::sc_module(nm), ExtPartBase("Servo", nm),
          vcc_(&vcc), gnd_(&gnd), pwm_(&pwm), c_(c) {
        const int iv = add_pin("vcc", vcc, "servo_vcc");
        const int ig = add_pin("gnd", gnd, "servo_gnd");
        add_ref("pwm", pwm);
        alim_.reset(new RamaDosNodos(vcc, iv, gnd, ig));
        // Dónde se quedó el eje la última vez
        if (c_.continuo) {
            pos_ = 0.0;
        } else if (c_.inicial_aleatoria) {
            // Del mt19937, que es el mismo en todas partes, a mano: la
            // distribución de la biblioteca no lo es, y con la misma semilla
            // tiene que salir el mismo ángulo en Linux y en Windows
            std::mt19937 g(c_.semilla ? c_.semilla : std::random_device{}());
            const double u = double(g()) / 4294967296.0;            // [0, 1)
            pos_ = std::round((c_.angulo_min + u * (c_.angulo_max - c_.angulo_min)) * 10.0) / 10.0;
        } else {
            pos_ = std::max(c_.angulo_min, std::min(c_.angulo_max, c_.posicion_inicial));
        }
        inicial_ = pos_;
        objetivo_ = pos_;
        SC_HAS_PROCESS(Servo);
        SC_THREAD(electrica);
        SC_THREAD(motor);
        SC_METHOD(senal);
        sensitive << pwm_->value_changed_event();
        dont_initialize();
    }
    ~Servo() override { alim_->suelta(); }

    // --- Desde C++ -------------------------------------------------------------
    double angulo() const { return angulo_en(ahora()); }      // ° (continuo: 0..360)
    double objetivo() const { return objetivo_; }
    double inicial() const { return inicial_; }
    double pulso_us() const { return pulso_us_; }
    double periodo_ms() const { return periodo_medido_ms_; }
    bool   moviendose() const { return omega_ != 0.0; }
    bool   con_senal() const { return con_senal_; }
    double tension() const { return v_; }
    double corriente_ma() const { return i_ma_; }
    double rpm() const { return omega_ / 6.0; }
    uint64_t pulsos() const { return n_pulsos_; }
    void bloquea(bool si) { bloqueado_ = si; cambia(); }
    bool bloqueado() const { return bloqueado_; }

    // --- Lo que deja ver y tocar --------------------------------------------------
    unsigned   n_observables() const override { return 4; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return c_.continuo ? Observable{"angulo", "°", 0.f, 360.f, true}
                                       : Observable{"angulo", "°", float(c_.angulo_min),
                                                    float(c_.angulo_max), true};
            case 1: return {"pulso", "us", 0.f, 3000.f, false};
            case 2: return {"corriente", "mA", 0.f, float(c_.i_bloqueo_ma), false};
            default: {
                const float m = float(60.0 / tiempo60(c_.velocidad.front().first) / 6.0);
                return {"rpm", "rpm", -m, m, false};
            }
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(angulo());
            case 1: return float(pulso_us_);
            case 2: return float(i_ma_);
            default: return float(rpm());
        }
    }
    unsigned n_mandos() const override { return 1; }
    Mando    mando(unsigned) const override { return {"bloquear", Mando::Interruptor, 0.f, 1.f}; }
    float    valor_mando(unsigned) const override { return bloqueado_ ? 1.f : 0.f; }
    void     acciona(unsigned, float v) override { bloquea(v >= 0.5f); }

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }

    // Segundos por 60 grados a la tensión v: entre los puntos dados, en línea
    // recta; fuera, el más cercano. Con uno solo, proporcional a la tensión.
    double tiempo60(double v) const {
        const auto& p = c_.velocidad;
        if (p.size() == 1) return p[0].second * p[0].first / std::max(v, 0.1);
        if (v <= p.front().first) return p.front().second;
        if (v >= p.back().first) return p.back().second;
        for (std::size_t k = 1; k < p.size(); ++k)
            if (v <= p[k].first) {
                const double f = (v - p[k - 1].first) / (p[k].first - p[k - 1].first);
                return p[k - 1].second + f * (p[k].second - p[k - 1].second);
            }
        return p.back().second;
    }
    // La velocidad máxima a la tensión de ahora, en °/s
    double vel_max() const { return 60.0 / tiempo60(v_); }

    // Dónde está el eje en el instante t
    double angulo_en(double t) const {
        double p = pos_ + omega_ * (t - t0_);
        if (c_.continuo) {
            p = std::fmod(p, 360.0);
            return p < 0 ? p + 360.0 : p;
        }
        if (omega_ > 0) p = std::min(p, objetivo_);
        if (omega_ < 0) p = std::max(p, objetivo_);
        return p;
    }

    // =========================================================================
    // Lo eléctrico: la carga y la tensión
    // =========================================================================
    void electrica() {
        for (;;) {
            const double i = !conectada_ ? 0.0
                           : bloqueado_ && quiere_moverse() ? c_.i_bloqueo_ma
                           : omega_ != 0.0 ? c_.i_marcha_ma : c_.i_reposo_ma;
            alim_->resuelve(conectada_ && i > 0, V_NOMINAL / (std::max(i, 0.001) * 1e-3));
            const double v = double(vcc_->voltage()) - double(gnd_->voltage());
            i_ma_ = conectada_ && v > 0.5 ? alim_->corriente() * 1e3 : 0.0;
            if (i_ma_ < 0) i_ma_ = 0;
            if (std::fabs(v - v_) > 0.01) {
                v_ = v;
                if (v_ > c_.v_max + 0.05) {
                    char b[160];
                    std::snprintf(b, sizeof b, "%.2f V en VCC, y el servo aguanta %.1f V", v_,
                                  c_.v_max);
                    avisa("sobretension", b);
                }
                replanifica();
            }
            wait(vcc_->value_changed_event() | gnd_->value_changed_event() | ev_carga_ |
                 evento_conexion());
        }
    }

    // =========================================================================
    // La señal: la anchura de cada pulso
    // =========================================================================
    void senal() {
        const double v = double(pwm_->voltage()) - double(gnd_->voltage());
        const bool alto = alto_ ? v > 0.8 : v > 1.6;    // con histéresis
        if (alto == alto_) return;
        alto_ = alto;
        const double t = ahora();
        if (alto) {
            if (hay_subida_) {
                periodo_medido_ms_ = (t - t_subida_) * 1e3;
                if (periodo_medido_ms_ < 0.5 * c_.periodo_ms ||
                    periodo_medido_ms_ > 2.0 * c_.periodo_ms) {
                    char b[160];
                    std::snprintf(b, sizeof b, "la senal llega cada %.2f ms, y el servo espera "
                                  "unos %.1f ms", periodo_medido_ms_, c_.periodo_ms);
                    avisa("periodo", b);
                }
            }
            t_subida_ = t;
            hay_subida_ = true;
            return;
        }
        if (!hay_subida_) return;
        const double us = (t - t_subida_) * 1e6;
        if (us < 300.0 || us > 3000.0) {
            char b[160];
            std::snprintf(b, sizeof b, "un pulso de %.0f us: el servo solo reconoce pulsos de "
                          "300 a 3000 us, y lo ignora", us);
            avisa("pulso_raro", b);
            return;
        }
        ++n_pulsos_;
        ultimo_pulso_ = t;
        const bool volvio = !con_senal_;
        con_senal_ = true;
        // La banda muerta: lo que se parece a lo de antes no mueve nada
        if (hay_pulso_ && std::fabs(us - pulso_us_) < c_.banda_muerta_us && objetivo_fijado_) {
            if (volvio) cambia();                         // vuelve a ir hacia él
            else ev_cambio_.notify(sc_core::SC_ZERO_TIME); // el plazo de la señal se renueva
            return;
        }
        pulso_us_ = us;
        hay_pulso_ = true;
        const double pmin = c_.pulso_min_ms * 1e3, pmax = c_.pulso_max_ms * 1e3;
        if (us < pmin - c_.banda_muerta_us || us > pmax + c_.banda_muerta_us) {
            char b[200];
            std::snprintf(b, sizeof b, "un pulso de %.0f us, fuera del rango de %.0f a %.0f us: "
                          "el servo se queda en el extremo", us, pmin, pmax);
            avisa("fuera_de_rango", b);
        }
        const double f = (std::max(pmin, std::min(pmax, us)) - pmin) / (pmax - pmin);
        if (c_.continuo) {
            consigna_vel_ = (2.0 * f - 1.0);         // -1 .. +1
            if (std::fabs(us - (pmin + pmax) / 2.0) < c_.banda_muerta_us) consigna_vel_ = 0.0;
        } else {
            objetivo_pedido_ = c_.angulo_min + f * (c_.angulo_max - c_.angulo_min);
        }
        objetivo_fijado_ = true;
        cambia();
    }

    // =========================================================================
    // El motor
    // =========================================================================
    // Si el circuito empuja el motor: con un ángulo pedido y con señal -o
    // siendo digital-. Es lo que gasta un servo bloqueado
    bool quiere_moverse() const {
        if (!objetivo_fijado_ || !(con_senal_ || c_.mantiene)) return false;
        if (c_.continuo) return consigna_vel_ != 0.0;
        return std::fabs(objetivo_pedido_ - angulo_en(ahora())) > 1e-9;
    }
    void cambia() {
        replanifica();
        ev_cambio_.notify(sc_core::SC_ZERO_TIME);
    }
    // Se congela la posición de ahora y se decide qué hace el motor desde aquí
    void replanifica() {
        const double t = ahora();
        const double p = angulo_en(t);
        pos_ = p;
        t0_ = t;
        const bool puede = conectada_ && !bloqueado_ && v_ >= c_.v_min &&
                           (con_senal_ || c_.mantiene) && objetivo_fijado_;
        if (!puede) {
            omega_ = 0.0;
        } else if (c_.continuo) {
            omega_ = consigna_vel_ * vel_max();
        } else {
            objetivo_ = objetivo_pedido_;
            const double d = objetivo_ - pos_;
            omega_ = std::fabs(d) < 1e-9 ? 0.0 : (d > 0 ? vel_max() : -vel_max());
        }
        // La carga, por si ha cambiado: quieto, en marcha o bloqueado. Siempre,
        // y no solo al arrancar o parar: soltar el eje con la tensión hundida
        // por debajo de v_min no lo pone en marcha, pero la carga sí cambia
        ev_carga_.notify(sc_core::SC_ZERO_TIME);
    }

    void motor() {
        for (;;) {
            // El próximo instante en que algo cambia solo: la llegada, o que
            // se acabe el plazo de la señal
            double plazo = -1.0;
            const double t = ahora();
            if (omega_ != 0.0 && !c_.continuo)
                plazo = std::fabs(objetivo_ - angulo_en(t)) / std::fabs(omega_);
            if (con_senal_) {
                const double fin = ultimo_pulso_ + 3.0 * c_.periodo_ms * 1e-3 - t;
                plazo = plazo < 0 ? fin : std::min(plazo, fin);
            }
            // Nunca menos de un nanosegundo: un plazo que redondea a cero sería
            // un bucle de ciclos delta
            if (plazo < 0) wait(ev_cambio_);
            else wait(sc_core::sc_time(std::max(plazo, 1e-9), sc_core::SC_SEC), ev_cambio_);
            const double ya = ahora();
            if (con_senal_ && ya - ultimo_pulso_ >= 3.0 * c_.periodo_ms * 1e-3 - 1e-12) {
                con_senal_ = false;           // sin señal: el analógico suelta
                hay_subida_ = false;          // y el periodo se vuelve a medir
                avisa("sin_senal", c_.mantiene
                      ? "sin senal en el PWM: el servo digital mantiene el ultimo angulo"
                      : "sin senal en el PWM: el servo analogico suelta el motor y se queda "
                        "donde esta");
            }
            // Ha llegado: lo que falta se recorre en menos de un nanosegundo
            if (omega_ != 0.0 && !c_.continuo &&
                std::fabs(objetivo_ - angulo_en(ya)) <= std::fabs(omega_) * 1e-9 + 1e-9) {
                pos_ = objetivo_;
                t0_ = ya;
                omega_ = 0.0;
                ev_carga_.notify(sc_core::SC_ZERO_TIME);
            }
            replanifica();
        }
    }

    void avisa(const std::string& que, const std::string& texto) {
        if (texto.empty() || !dichos_.insert(que).second) return;
        SC_REPORT_WARNING("servo", (pieza() + ": " + texto).c_str());
    }

    analog_net_if *vcc_, *gnd_, *pwm_;
    ConfigServo c_;
    std::unique_ptr<RamaDosNodos> alim_;

    // El eje: en t0_ estaba en pos_, y va a omega_ °/s hacia objetivo_
    double pos_ = 0.0, t0_ = 0.0, omega_ = 0.0, objetivo_ = 0.0, inicial_ = 0.0;
    double objetivo_pedido_ = 0.0, consigna_vel_ = 0.0;
    bool   objetivo_fijado_ = false, bloqueado_ = false;
    // La señal
    bool   alto_ = false, hay_subida_ = false, hay_pulso_ = false, con_senal_ = false;
    double t_subida_ = 0.0, ultimo_pulso_ = 0.0, pulso_us_ = 0.0, periodo_medido_ms_ = 0.0;
    uint64_t n_pulsos_ = 0;
    // Lo eléctrico
    double v_ = 0.0, i_ma_ = 0.0;
    sc_core::sc_event ev_cambio_, ev_carga_;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_SERVO_H
