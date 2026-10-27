// =============================================================================
// puente_h.h — Un doble puente en H, el L298, y el regulador lineal que lo
//              alimenta en los módulos L298N
//
//   L298        dos puentes en H completos para motores de continua (o un
//               paso a paso bipolar) [ST, L298 «Dual full-bridge driver»,
//               DS0218 Rev 5]: cuatro salidas de hasta 2 A, con su caída
//   Regulador   un regulador lineal de tres patillas, como el 78M05 del
//               módulo [ST, L78M «Positive voltage regulators»]: la tensión
//               de salida, si la entrada le deja margen
//
// LOS DOS SON FUENTES CON CARGA. Lo que entregan por la salida es un
// equivalente de Thevenin -no una rama hasta su alimentación-, y lo que eso
// cuesta lo piden a su entrada como una carga (RamaDosNodos) de la corriente
// que sale más la propia: como el Servo con su VCC. Así una batería floja se
// hunde con los motores, y con ella lo que les llega.
// =============================================================================
#ifndef STM32_PARTS_PUENTE_H_H
#define STM32_PARTS_PUENTE_H_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// EL L298: dos puentes, A (ENA, IN1, IN2 -> OUT1, OUT2) y B (ENB, IN3, IN4 ->
// OUT3, OUT4) [DS0218, tabla de la figura 6]:
//
//     EN = alto   IN1 alto, IN2 bajo   OUT1 a Vs, OUT2 a masa: hacia delante
//                 IN1 bajo, IN2 alto   al revés
//                 IN1 = IN2            los dos a Vs o los dos a masa: el motor
//                                      en corto, FRENA
//     EN = bajo                        las dos al aire: el motor va LIBRE
//
// Las entradas son TTL: bajo hasta 1,5 V y alto desde 2,3 V -vale con los
// 3,3 V de un STM32-, de alta impedancia. Entre los dos, la entrada se queda
// como estaba, y se avisa.
//
// LAS SALIDAS no llegan a Vs ni a masa: son Darlington, y caen lo que dice la
// hoja de datos [DS0218, VCEsat(H) y VCEsat(L)]: arriba 1,35 V típicos con
// 1 A y 2 V con 2 A; abajo 1,2 V y 1,7 V. En línea recta, eso es una fuente
// de Vs - 0,7 V con 0,65 ohm arriba y de 0,7 V con 0,5 ohm abajo: con un
// motor de 160 mA, 2,6 V menos de los que da la batería, y casi 3,2 con 1 A.
// Es lo que más sorprende del L298 a quien lo usa por primera vez.
//
// LA LÓGICA va a VSS, de 4,5 a 7 V: con menos, las salidas se quedan al aire
// y se avisa. VS es la de los motores, hasta 46 V: de ahí sale lo que va a
// las salidas altas, más 13 mA con algún puente habilitado (4 sin ninguno);
// la lógica gasta 24 mA (6 sin habilitar) [DS0218, Is e Iss].
//
// Lo que NO hace: las patillas de sensado de corriente, SENSE A y SENSE B,
// van a masa por dentro -en el módulo, con un puente-; ni la temperatura ni
// su protección, que el L298 no tiene: pasar de 2 A por salida se avisa, y
// lo que disipa se deja ver.
// ---------------------------------------------------------------------------
struct ConfigL298 {
    double vh0 = 0.7, rh = 0.65;     // la salida alta: Vs - vh0, con rh
    double vl0 = 0.7, rl = 0.5;      // la baja: vl0 sobre masa, con rl
    double vil = 1.5, vih = 2.3;     // las entradas
    double vss_min = 4.5;            // la lógica
};

SC_MODULE(L298), public ExtPartBase {
    L298(sc_core::sc_module_name nm, analog_net_if& vs, analog_net_if& vss, analog_net_if& gnd,
         analog_net_if* en[2], analog_net_if* in[4], analog_net_if* out[4],
         const ConfigL298& c = ConfigL298())
        : sc_core::sc_module(nm), ExtPartBase("L298", nm), vs_(&vs), vss_(&vss), gnd_(&gnd),
          c_(c) {
        const int ivs = add_pin("vs", vs, "l298_vs");
        const int ivss = add_pin("vss", vss, "l298_vss");
        const int ig = add_pin("gnd", gnd, "l298_gnd");
        const int ig2 = add_drv("gnd", "l298_gnd");
        carga_vs_.reset(new RamaDosNodos(vs, ivs, gnd, ig));
        carga_vss_.reset(new RamaDosNodos(vss, ivss, gnd, ig2));
        static const char* nen[2] = {"ena", "enb"};
        static const char* nin[4] = {"in1", "in2", "in3", "in4"};
        static const char* nout[4] = {"out1", "out2", "out3", "out4"};
        for (int k = 0; k < 2; ++k) {
            en_[k] = en[k];
            add_ref(nen[k], *en[k]);
        }
        for (int k = 0; k < 4; ++k) {
            in_[k] = in[k];
            add_ref(nin[k], *in[k]);
            out_[k] = out[k];
            id_out_[k] = add_pin(nout[k], *out[k], "l298_out");
        }
        SC_HAS_PROCESS(L298);
        SC_THREAD(puente);
    }
    ~L298() override {
        carga_vs_->suelta();
        carga_vss_->suelta();
        for (int k = 0; k < 4; ++k) out_[k]->set_hiz(id_out_[k]);
    }

    // --- Desde C++ -----------------------------------------------------------
    bool logica() const { return logica_; }
    bool habilitado(int p) const { return logica_ && en_v_[p]; }
    // Lo que hace cada puente: "adelante", "atras", "freno", "libre"
    const char* estado(int p) const {
        if (!habilitado(p)) return "libre";
        const bool x = in_v_[2 * p], y = in_v_[2 * p + 1];
        return x == y ? "freno" : (x ? "adelante" : "atras");
    }
    // La corriente que sale por cada salida AHORA, en A (negativa si entra):
    // con PWM, a saltos
    double corriente_salida(int k) const { return conduce_[k] ? i_out_[k] : 0.0; }
    // La del motor de cada puente, la que sale por OUT1 (OUT3), y lo que se
    // pide a VS y VSS y lo que se disipa: sus MEDIAS de los ultimos
    // milisegundos (un filtro de primer orden de MEDIA_S), lo que marcaria un
    // polimetro
    double corriente_ma(int p) const { return media(med_[p], corriente_salida(2 * p)) * 1e3; }
    double corriente_vs_ma() const { return media(med_[2], i_vs_) * 1e3; }
    double corriente_vss_ma() const { return i_vss_ * 1e3; }
    double disipacion() const { return media(med_[3], p_); }       // W
    double disipacion_max() const { return p_max_; }

    unsigned n_observables() const override { return 5; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return {"corriente_a", "mA", -2000.f, 2000.f, false};
            case 1: return {"corriente_b", "mA", -2000.f, 2000.f, false};
            case 2: return {"corriente_vs", "mA", 0.f, 4000.f, false};
            case 3: return {"disipacion", "W", 0.f, 25.f, false};
            default: return {"logica", "", 0.f, 1.f, false};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(corriente_ma(0));
            case 1: return float(corriente_ma(1));
            case 2: return float(corriente_vs_ma());
            case 3: return float(disipacion());
            default: return logica_ ? 1.f : 0.f;
        }
    }

private:
    double v(const analog_net_if* n) const { return double(n->voltage()) - double(gnd_->voltage()); }
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    static constexpr double MEDIA_S = 5e-3;
    double media(double med, double x) const {
        return x + (med - x) * std::exp(-(ahora() - t_med_) / MEDIA_S);
    }
    void apunta_medias() {
        med_[0] = media(med_[0], corriente_salida(0));
        med_[1] = media(med_[1], corriente_salida(2));
        med_[2] = media(med_[2], i_vs_);
        med_[3] = media(med_[3], p_);
        t_med_ = ahora();
    }

    // Una entrada TTL: con su nivel dudoso, se queda como estaba
    bool nivel(analog_net_if* n, bool antes, const char* nombre) {
        const double x = v(n);
        if (x <= c_.vil) return false;
        if (x >= c_.vih) return true;
        char b[200];
        std::snprintf(b, sizeof b, "%s: %.2f V en %s, entre los %.1f y los %.1f V de una "
                      "entrada TTL: ni alto ni bajo, y se queda como estaba", pieza().c_str(),
                      x, nombre, c_.vil, c_.vih);
        avisa(std::string("dudoso_") + nombre, b);
        return antes;
    }

    void puente() {
        static const char* nen[2] = {"ENA", "ENB"};
        static const char* nin[4] = {"IN1", "IN2", "IN3", "IN4"};
        for (;;) {
            apunta_medias();
            const double vss = v(vss_), vs = v(vs_);
            const bool logica = conectada_ && vss >= c_.vss_min;
            if (conectada_ && !logica && vss > 0.5) {
                char b[160];
                std::snprintf(b, sizeof b, "%s: %.2f V en VSS, y la logica del L298 necesita "
                              "de %.1f a 7 V: las salidas se quedan al aire", pieza().c_str(),
                              vss, c_.vss_min);
                avisa("vss", b);
            }
            logica_ = logica;
            for (int p = 0; p < 2; ++p) en_v_[p] = nivel(en_[p], en_v_[p], nen[p]);
            for (int k = 0; k < 4; ++k) in_v_[k] = nivel(in_[k], in_v_[k], nin[k]);
            // Las salidas
            const double vg = double(gnd_->voltage());
            bool cambia = false;
            for (int k = 0; k < 4; ++k) {
                const bool on = habilitado(k / 2);
                conduce_[k] = on;
                alta_[k] = in_v_[k];
                float vd = 0.f, rd = R_HIZ;
                if (on && alta_[k]) { vd = float(vg + std::max(0.0, vs - c_.vh0)); rd = float(c_.rh); }
                else if (on)        { vd = float(vg + c_.vl0); rd = float(c_.rl); }
                if (vd == v_drv_[k] && rd == r_drv_[k]) continue;
                v_drv_[k] = vd;
                r_drv_[k] = rd;
                cambia = true;
                if (!on) out_[k]->set_hiz(id_out_[k]);
                else out_[k]->set_drive(id_out_[k], vd, rd);
            }
            // Con una salida recien cambiada, el nodo aun no se ha resuelto:
            // lo que sale por ella se mira en la vuelta siguiente
            if (cambia) {
                ev_revisa_.notify(sc_core::SC_ZERO_TIME);
                wait(despierta());
                continue;
            }
            // Lo que sale por cada una, lo que piden VS y VSS y lo que se
            // queda en el chip
            double i_alta = 0.0, p = 0.0;
            for (int k = 0; k < 4; ++k) {
                // current(): lo que el driver mete en el nodo
                i_out_[k] = conduce_[k] ? double(out_[k]->current(id_out_[k])) : 0.0;
                if (!conduce_[k]) continue;
                const double vo = double(out_[k]->voltage()) - vg;
                if (alta_[k]) {
                    if (i_out_[k] > 0) { i_alta += i_out_[k]; p += (vs - vo) * i_out_[k]; }
                } else if (i_out_[k] < 0) {
                    p += vo * -i_out_[k];
                }
                if (std::fabs(i_out_[k]) > 2.0) {
                    char b[160];
                    std::snprintf(b, sizeof b, "%s: %.2f A por OUT%d, y cada salida del L298 "
                                  "aguanta 2 A", pieza().c_str(), std::fabs(i_out_[k]), k + 1);
                    avisa("sobrecorriente", b);
                }
            }
            const bool alguno = habilitado(0) || habilitado(1);
            const double iq_vs = logica ? (alguno ? 13e-3 : 4e-3) : 0.0;
            const double iq_vss = logica ? (alguno ? 24e-3 : 6e-3) : 0.0;
            i_vs_ = i_alta + iq_vs;
            i_vss_ = iq_vss;
            p_ = p + vs * iq_vs + vss * iq_vss;
            p_max_ = std::max(p_max_, media(med_[3], p_));
            // Las cargas: solo si cambian de verdad, o se reajustarian en bucle
            pide(*carga_vs_, r_vs_, vs, i_vs_);
            pide(*carga_vss_, r_vss_, vss, i_vss_);
            wait(despierta());
        }
    }
    // Una carga de i amperios a la tension v, como una resistencia
    void pide(RamaDosNodos& rama, double& r_ant, double v, double i) {
        if (!conectada_ || v < 0.05 || i <= 1e-6) {
            if (r_ant != 0.0) { rama.suelta(); r_ant = 0.0; }
            return;
        }
        const double r = v / i;
        if (r_ant != 0.0 && std::fabs(r - r_ant) < 1e-3 * r_ant) return;
        r_ant = r;
        rama.resuelve(true, r);
    }
    const sc_core::sc_event_or_list& despierta() {
        if (!despierta_) {
            despierta_.reset(new sc_core::sc_event_or_list());
            *despierta_ |= vs_->value_changed_event();
            *despierta_ |= vss_->value_changed_event();
            *despierta_ |= gnd_->value_changed_event();
            for (int k = 0; k < 2; ++k) *despierta_ |= en_[k]->value_changed_event();
            for (int k = 0; k < 4; ++k) {
                *despierta_ |= in_[k]->value_changed_event();
                *despierta_ |= out_[k]->value_changed_event();
            }
            *despierta_ |= evento_conexion();
            *despierta_ |= ev_revisa_;
        }
        return *despierta_;
    }
    void avisa(const std::string& que, const std::string& texto) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("l298", texto.c_str());
    }

    analog_net_if *vs_, *vss_, *gnd_;
    analog_net_if *en_[2], *in_[4], *out_[4];
    int id_out_[4];
    ConfigL298 c_;
    std::unique_ptr<RamaDosNodos> carga_vs_, carga_vss_;
    double r_vs_ = 0.0, r_vss_ = 0.0;
    bool logica_ = false, en_v_[2] = {false, false}, in_v_[4] = {false, false, false, false};
    bool conduce_[4] = {false, false, false, false}, alta_[4] = {false, false, false, false};
    double i_out_[4] = {0, 0, 0, 0}, i_vs_ = 0.0, i_vss_ = 0.0, p_ = 0.0, p_max_ = 0.0;
    float v_drv_[4] = {0.f, 0.f, 0.f, 0.f}, r_drv_[4] = {R_HIZ, R_HIZ, R_HIZ, R_HIZ};
    double med_[4] = {0, 0, 0, 0}, t_med_ = 0.0;
    sc_core::sc_event ev_revisa_;
    std::unique_ptr<sc_core::sc_event_or_list> despierta_;
    std::set<std::string> dichos_;
};

// ---------------------------------------------------------------------------
// UN REGULADOR LINEAL de tres patillas, como el 78M05: entrada, salida y masa.
// Da `v` voltios mientras la entrada le deje `caida` de margen; con menos, lo
// que pueda -la entrada menos la caída-, y lo deja ver como una alarma,
// `sin_margen`. Lo que sale por la salida lo pide a la entrada, más su
// corriente de reposo `iq_ma`. Pasar de `limite_ma` se avisa [L78M: 500 mA,
// caída de 2 V, 6 mA de reposo como mucho].
// ---------------------------------------------------------------------------
SC_MODULE(Regulador), public ExtPartBase {
    Regulador(sc_core::sc_module_name nm, analog_net_if& entrada, analog_net_if& salida,
              analog_net_if& gnd, double v = 5.0, double caida = 2.0, double limite_ma = 500.0,
              double iq_ma = 5.0, double r_salida = 0.1)
        : sc_core::sc_module(nm), ExtPartBase("Regulador", nm), in_(&entrada), out_(&salida),
          gnd_(&gnd), v_(v), caida_(caida), lim_(limite_ma * 1e-3), iq_(iq_ma * 1e-3),
          r_(std::max(0.1, r_salida)) {
        const int ii = add_pin("entrada", entrada, "regulador_in");
        id_out_ = add_pin("salida", salida, "regulador_out");
        const int ig = add_pin("gnd", gnd, "regulador_gnd");
        carga_.reset(new RamaDosNodos(entrada, ii, gnd, ig));
        SC_HAS_PROCESS(Regulador);
        SC_THREAD(regula);
    }
    ~Regulador() override { carga_->suelta(); out_->set_hiz(id_out_); }

    double tension() const { return double(out_->voltage()) - double(gnd_->voltage()); }
    double corriente_ma() const { return i_ * 1e3; }
    bool   sin_margen() const { return sin_margen_; }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"tension", "V", 0.f, float(v_ * 1.2), false};
        if (i == 1) return {"corriente", "mA", 0.f, float(lim_ * 1e3), false};
        return {"sin_margen", "", 0.f, 1.f, true, true};
    }
    float valor_observable(unsigned i) const override {
        if (i == 0) return float(tension());
        if (i == 1) return float(corriente_ma());
        return sin_margen_ ? 1.f : 0.f;
    }

private:
    void regula() {
        for (;;) {
            const double vg = double(gnd_->voltage());
            const double vin = double(in_->voltage()) - vg;
            if (!conectada_ || vin < 0.5) {
                out_->set_hiz(id_out_);
                sin_margen_ = false;
                i_ = 0.0;
                if (r_ant_ != 0.0) { carga_->suelta(); r_ant_ = 0.0; }
            } else {
                const double vo = std::min(v_, std::max(0.0, vin - caida_));
                // Sin margen: cuando lo que da se queda 50 mV corto
                const bool sm = vin - caida_ < v_ - 0.05;
                if (sm && !sin_margen_ && !dicho_margen_) {
                    dicho_margen_ = true;
                    char b[200];
                    std::snprintf(b, sizeof b, "%s: %.2f V a la entrada, y para dar %.2f V "
                                  "necesita %.2f: da %.2f", pieza().c_str(), vin, v_,
                                  v_ + caida_, vo);
                    SC_REPORT_WARNING("regulador", b);
                }
                sin_margen_ = sm;
                if (std::fabs(vg + vo - vo_ant_) > 1e-4 || !dando_) {
                    out_->set_drive(id_out_, float(vg + vo), float(r_));
                    vo_ant_ = vg + vo;
                    dando_ = true;
                    // El nodo aun no se ha resuelto: la corriente, en la
                    // vuelta siguiente
                    ev_revisa_.notify(sc_core::SC_ZERO_TIME);
                    wait(in_->value_changed_event() | out_->value_changed_event() |
                         gnd_->value_changed_event() | evento_conexion() | ev_revisa_);
                    continue;
                }
                i_ = std::max(0.0, double(out_->current(id_out_)));
                if (i_ > lim_ && !dicho_limite_) {
                    dicho_limite_ = true;
                    char b[160];
                    std::snprintf(b, sizeof b, "%s: %.0f mA a la salida, y el regulador da "
                                  "%.0f", pieza().c_str(), i_ * 1e3, lim_ * 1e3);
                    SC_REPORT_WARNING("regulador", b);
                }
                const double r = vin / (i_ + iq_);
                if (r_ant_ == 0.0 || std::fabs(r - r_ant_) > 1e-3 * r_ant_) {
                    r_ant_ = r;
                    carga_->resuelve(true, r);
                }
            }
            if (!conectada_ || vin < 0.5) dando_ = false;
            wait(in_->value_changed_event() | out_->value_changed_event() |
                 gnd_->value_changed_event() | evento_conexion() | ev_revisa_);
        }
    }

    analog_net_if *in_, *out_, *gnd_;
    int id_out_;
    double v_, caida_, lim_, iq_, r_;
    std::unique_ptr<RamaDosNodos> carga_;
    double r_ant_ = 0.0, vo_ant_ = -1.0, i_ = 0.0;
    bool sin_margen_ = false, dando_ = false;
    sc_core::sc_event ev_revisa_;
    bool dicho_margen_ = false, dicho_limite_ = false;
};

} // namespace stm32

#endif // STM32_PARTS_PUENTE_H_H
