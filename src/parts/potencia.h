// =============================================================================
// potencia.h — Los interruptores de potencia de una máquina: un MOSFET de
//              canal N con su diodo de libre circulación, y el medio puente
//              BTS7960 de los módulos IBT-2
//
// (Fase F1b de doc/analisis_cnc.md, §5.3.)
//
//   Mosfet       un N-MOSFET de lado bajo, como el IRLZ44N de los módulos que
//                gobiernan el cabezal de una fresadora con el PWM de GRBL: un
//                canal entre drenador y fuente que conduce con la tensión de
//                la puerta, y, si el módulo lo lleva, el DIODO DE LIBRE
//                CIRCULACIÓN entre el drenador y el +: sin él, la inductancia
//                del motor se lleva por delante el MOSFET, y se avisa
//   MedioPuente  un medio puente integrado, el BTS7960 de Infineon: IN elige
//                el transistor de arriba o el de abajo, INH lo habilita, y IS
//                saca una corriente proporcional a la de la salida. Dos son
//                un puente en H, el módulo IBT-2 de 43 A
//
// LOS DOS SON COMO EL L298 (parts/puente_h.h), con otras cifras: lo que
// conducen es una resistencia -RDS(on), de miliohmios- y no una caída de
// Darlington, y la corriente de la bobina de un motor que se suelta sigue por
// un diodo hasta acabarse: el del módulo, en el Mosfet; los de sus propios
// transistores, en el BTS7960. El L298 no se toca: así M9 y M10 no se mueven.
// =============================================================================
#ifndef STM32_PARTS_POTENCIA_H
#define STM32_PARTS_POTENCIA_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"

namespace stm32 {

// La corriente que mete un driver en su nodo, sin el umbral con el que el nodo
// decide si ha cambiado: con una salida de miliohmios, 0,1 mV de umbral son
// 14 mA de error, y lo que se pide a la alimentación con esa corriente
// volvería a mover el nodo, en bucle
inline double corriente_de(const analog_net_if* n, int id) {
    if (const AnalogNet* a = dynamic_cast<const AnalogNet*>(n)) return a->corriente_exacta(id);
    return double(n->current(id));
}

// ---------------------------------------------------------------------------
// UN N-MOSFET DE LADO BAJO. El canal entre `drenador` y `fuente` conduce con
// Vgs = V(puerta) - V(fuente) por encima del umbral, con una resistencia que
// baja con la tensión de la puerta:
//
//     RDS = rds_on · (vgs_ref - vth) / (Vgs - vth)
//
// -la de la hoja de datos a vgs_ref, y más cuanto más cerca del umbral: un
// MOSFET que no es de nivel lógico, a 3,3 V, apenas conduce-. La puerta no
// gasta nada (su carga, y con ella los tiempos de conmutación, no se
// modelan).
//
// EL DIODO DE LIBRE CIRCULACIÓN, del drenador (ánodo) a `libre` (cátodo, el +
// del motor), si `diodo` lo dice: cuando el canal se abre llevando la
// corriente de la bobina de un motor, el drenador sube hasta V(libre) + vd y la
// corriente sigue por el diodo, contra la tensión del motor, hasta acabarse.
// Sin él, la bobina sube el drenador hasta la avalancha del MOSFET, y en una
// placa de verdad lo rompe: aquí la corriente se corta, y se avisa.
//
// Lo que disipa es I²·RDS (las pérdidas de conmutación no se cuentan), y pasar
// de `i_max` se avisa. Con menos de `vgs_plena` en la puerta, conduciendo más
// de un amperio, también: no se abre del todo.
// ---------------------------------------------------------------------------
struct ConfigMosfet {
    std::string modelo = "IRLZ44N";
    double vth = 1.5;          // V: el umbral
    double rds_on = 0.025;     // ohm, a vgs_ref
    double vgs_ref = 5.0;      // V
    double vgs_plena = 4.0;    // V: la que garantiza la hoja de datos para rds_on
    double i_max = 47.0;       // A
    bool   diodo = true;       // el de libre circulación del módulo
    double vd = 0.9, rd = 0.05;    // V y ohm: el diodo (un SS54, o un 1N5822)
};

// Los de la hoja de datos [IRLZ44N de Infineon/IR: RDS(on) 22 mohm a 10 V y
// 25 a 5 V, VGS(th) de 1 a 2 V; IRF520 de Vishay: 0,27 ohm a 10 V, VGS(th) de
// 2 a 4 V, el de los módulos azules que no se abren con 5 V]
inline bool modelo_mosfet(const std::string& n, ConfigMosfet& c) {
    if (n == "IRLZ44N") { c.vth = 1.5; c.rds_on = 0.025; c.vgs_ref = 5.0; c.vgs_plena = 4.0; c.i_max = 47.0; return true; }
    if (n == "IRF520")  { c.vth = 3.0; c.rds_on = 0.27;  c.vgs_ref = 10.0; c.vgs_plena = 10.0; c.i_max = 9.2; return true; }
    return false;
}

SC_MODULE(Mosfet), public ExtPartBase {
    Mosfet(sc_core::sc_module_name nm, analog_net_if& drenador, analog_net_if& fuente,
           analog_net_if& puerta, analog_net_if* libre, const ConfigMosfet& c = ConfigMosfet())
        : sc_core::sc_module(nm), ExtPartBase("Mosfet", nm), d_(&drenador), s_(&fuente),
          g_(&puerta), k_(libre), c_(c) {
        // El canal, como una salida del L298: el drenador va a la tensión de
        // la fuente con RDS (o, por el diodo, a la de `libre` más vd). La
        // fuente es la masa del módulo, y no se carga: lo que vuelve por ella
        // lo pone quien la sujeta
        id_ = add_pin("drenador", drenador, "mosfet");
        add_ref("fuente", fuente);
        add_ref("puerta", puerta);
        if (k_) add_ref("libre", *k_);
        SC_HAS_PROCESS(Mosfet);
        SC_THREAD(conmuta);
    }
    ~Mosfet() override { d_->set_hiz(id_); }

    double vgs() const { return double(g_->voltage()) - double(s_->voltage()); }
    bool   conduce() const { return on_; }
    bool   por_el_diodo() const { return libre_; }
    double rds() const { return r_; }
    double corriente() const { return on_ ? i_ : 0.0; }       // A, del drenador a la fuente
    double corriente_diodo() const { return libre_ ? i_ : 0.0; }
    double disipacion() const { return media(med_[1], p_); }
    double corriente_media() const { return media(med_[0], corriente()); }
    const ConfigMosfet& config() const { return c_; }

    unsigned n_observables() const override { return 4; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return {"conduce", "", 0.f, 1.f, true};
            case 1: return {"corriente", "mA", 0.f, float(c_.i_max * 1000.0), false};
            case 2: return {"disipacion", "W", 0.f, 10.f, false};
            default: return {"vgs", "V", 0.f, 12.f, false};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return on_ ? 1.f : 0.f;
            case 1: return float(corriente_media() * 1e3);
            case 2: return float(disipacion());
            default: return float(vgs());
        }
    }

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    static constexpr double MEDIA_S = 5e-3;
    double media(double med, double x) const {
        return x + (med - x) * std::exp(-(ahora() - t_med_) / MEDIA_S);
    }
    void conmuta() {
        for (;;) {
            med_[0] = media(med_[0], corriente());
            med_[1] = media(med_[1], p_);
            t_med_ = ahora();
            // Lo que pasaba hasta ahora: por el canal o por el diodo, lo que
            // SALE del drenador por esta pieza
            i_ = (on_ || libre_) ? -corriente_de(d_, id_) : 0.0;
            const double v = vgs();
            const bool on = conectada_ && v > c_.vth;
            // El diodo se apaga cuando su corriente se acaba; se enciende al
            // abrirse el canal con corriente que entraba por el drenador
            if (libre_ && i_ < 0.0) { libre_ = false; i_ = 0.0; }
            if (on_ && !on && i_ > 0.05) {
                if (c_.diodo && k_) {
                    libre_ = true;
                } else {
                    char b[260];
                    std::snprintf(b, sizeof b, "%s: se abre con %.2f A en el drenador y sin "
                                  "diodo de libre circulacion: la bobina del motor lo sube hasta "
                                  "la avalancha del MOSFET, y en una placa de verdad lo rompe",
                                  pieza().c_str(), i_);
                    avisa("sin_diodo", b);
                }
            }
            if (on) libre_ = false;
            on_ = on;
            r_ = on ? std::max(c_.rds_on * 0.5, c_.rds_on * (c_.vgs_ref - c_.vth) / (v - c_.vth))
                    : 0.0;
            float vd = 0.f, rd = R_HIZ;
            if (on)          { vd = float(s_->voltage()); rd = float(r_); }
            else if (libre_) { vd = float(double(k_->voltage()) + c_.vd); rd = float(c_.rd); }
            if (rd != r_drv_ || std::fabs(vd - v_drv_) > 2e-4f) {
                v_drv_ = vd; r_drv_ = rd;
                if (rd == R_HIZ) d_->set_hiz(id_);
                else d_->set_drive(id_, vd, rd);
            }
            p_ = on ? i_ * i_ * r_ : 0.0;
            if (on && v < c_.vgs_plena && i_ > 1.0 && !dichos_.count("puerta")) {
                char b[240];
                std::snprintf(b, sizeof b, "%s: %.2f V en la puerta, y el %s pide %.1f para abrirse "
                              "del todo: conduce con %.0f mohm y se calienta", pieza().c_str(), v,
                              c_.modelo.c_str(), c_.vgs_plena, r_ * 1e3);
                avisa("puerta", b);
            }
            if (i_ > c_.i_max) {
                char b[200];
                std::snprintf(b, sizeof b, "%s: %.1f A por el drenador, y el %s aguanta %.0f",
                              pieza().c_str(), i_, c_.modelo.c_str(), c_.i_max);
                avisa("sobrecorriente", b);
            }
            if (k_) wait(d_->value_changed_event() | s_->value_changed_event() |
                         g_->value_changed_event() | k_->value_changed_event() | evento_conexion());
            else    wait(d_->value_changed_event() | s_->value_changed_event() |
                         g_->value_changed_event() | evento_conexion());
        }
    }
    void avisa(const std::string& que, const std::string& t) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("mosfet", t.c_str());
    }

    analog_net_if *d_, *s_, *g_, *k_;
    ConfigMosfet c_;
    int id_ = -1;
    bool on_ = false, libre_ = false;
    float v_drv_ = 0.f, r_drv_ = R_HIZ;
    double r_ = 0.0, i_ = 0.0, p_ = 0.0;
    double med_[2] = {0.0, 0.0}, t_med_ = 0.0;
    std::set<std::string> dichos_;
};

// ---------------------------------------------------------------------------
// UN MEDIO PUENTE INTEGRADO: el BTS7960 de Infineon [BTS7960, «High Current
// PN Half Bridge», rev. 1.1]. Con INH alto, IN alto pone la salida en VS por
// el transistor de arriba (7 mohm típicos) e IN bajo, en masa por el de abajo
// (9 mohm); con INH bajo, duerme y la salida se queda al aire. La corriente
// de la bobina de un motor que se suelta sigue por los diodos de sus propios
// transistores -hacia VS o desde masa- hasta acabarse.
//
// IS: una FUENTE DE CORRIENTE de la corriente que sale por el transistor de
// arriba entre `k_is` (8500 típicos), hacia lo que tenga colgado -la
// resistencia que se le pone, que la convierte en tensión-. Al aire, IS se
// queda a VS.
//
// LAS ENTRADAS: las del chip cortan hacia 1,6 V (alto desde 2,1); en el módulo
// IBT-2 pasan antes por un 74HC244 alimentado del VCC del módulo, y con
// `vdd` cortan en la mitad de VCC y piden 0,7·VCC para un alto: con 5 V, los
// 3,3 V de un STM32 quedan por debajo, y se avisa.
//
// Lo que pide a VS es lo que sale por el transistor de arriba, más su reposo;
// lo que disipa, I²·RDS. Pasar de 43 A se avisa (el chip limita hacia ahí).
// ---------------------------------------------------------------------------
struct ConfigMedioPuente {
    double r_alto = 0.007, r_bajo = 0.009;   // ohm
    double k_is = 8500.0;                    // la relación de IS
    double vd = 0.9, rd = 0.02;              // los diodos de sus transistores
    double i_max = 43.0;                     // A
    double iq = 2e-3;                        // A, despierto
};

SC_MODULE(MedioPuente), public ExtPartBase {
    MedioPuente(sc_core::sc_module_name nm, analog_net_if& vs, analog_net_if& gnd,
                analog_net_if& in, analog_net_if& inh, analog_net_if& out, analog_net_if* is,
                analog_net_if* vdd, const ConfigMedioPuente& c = ConfigMedioPuente())
        : sc_core::sc_module(nm), ExtPartBase("MedioPuente", nm), vs_(&vs), gnd_(&gnd),
          in_(&in), inh_(&inh), out_(&out), is_(is), vdd_(vdd), c_(c) {
        const int ivs = add_pin("vs", vs, "bts7960");
        const int ig = add_pin("gnd", gnd, "bts7960");
        carga_.reset(new RamaDosNodos(vs, ivs, gnd, ig));
        add_ref("in", in);
        add_ref("inh", inh);
        id_out_ = add_pin("out", out, "bts7960_out");
        if (is_) id_is_ = add_pin("is", *is_, "bts7960_is");
        if (vdd_) add_ref("vdd", *vdd_);
        SC_HAS_PROCESS(MedioPuente);
        SC_THREAD(puente);
    }
    ~MedioPuente() override {
        carga_->suelta();
        out_->set_hiz(id_out_);
        if (is_) is_->set_hiz(id_is_);
    }

    // "alto", "bajo" o "dormido"
    const char* estado() const { return !despierto_ ? "dormido" : (alto_ ? "alto" : "bajo"); }
    double corriente() const { return conduce_ ? i_ : 0.0; }        // A, la que sale
    double corriente_media() const { return media(med_[0], corriente()); }
    double corriente_is() const { return i_is_; }
    double disipacion() const { return media(med_[1], p_); }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"corriente", "mA", -43000.f, 43000.f, false};
        if (i == 1) return {"disipacion", "W", 0.f, 20.f, false};
        return {"despierto", "", 0.f, 1.f, false};
    }
    float valor_observable(unsigned i) const override {
        if (i == 0) return float(corriente_media() * 1e3);
        if (i == 1) return float(disipacion());
        return despierto_ ? 1.f : 0.f;
    }

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    static constexpr double MEDIA_S = 5e-3;
    double media(double med, double x) const {
        return x + (med - x) * std::exp(-(ahora() - t_med_) / MEDIA_S);
    }
    double v(const analog_net_if* n) const { return double(n->voltage()) - double(gnd_->voltage()); }
    bool nivel(analog_net_if* n, const char* nombre) {
        const double x = v(n);
        double bajo = 1.0, alto = 2.1, corte = 1.6;
        if (vdd_) {
            const double vl = v(vdd_);
            bajo = 0.3 * vl; alto = 0.7 * vl; corte = 0.5 * vl;
        }
        if (x > bajo && x < alto && ahora() > 0.0 && !dichos_.count(std::string("dudoso_") + nombre)) {
            char b[260];
            std::snprintf(b, sizeof b, "%s: %.2f V en %s, y %s pide %.2f V para un alto y %.2f para "
                          "un bajo: fuera de especificacion (lo lee %s)", pieza().c_str(), x, nombre,
                          vdd_ ? "el 74HC244 del modulo, con su VCC," : "el BTS7960", alto, bajo,
                          x >= corte ? "alto" : "bajo");
            avisa(std::string("dudoso_") + nombre, b);
        }
        return x >= corte;
    }
    void puente() {
        for (;;) {
            med_[0] = media(med_[0], corriente());
            med_[1] = media(med_[1], p_);
            t_med_ = ahora();
            const double vg = double(gnd_->voltage());
            const double vs = v(vs_);
            const bool logica = conectada_ && vs > 5.5 && (!vdd_ || v(vdd_) > 2.0);
            despierto_ = logica && nivel(inh_, "INH");
            alto_ = despierto_ && nivel(in_, "IN");
            // Lo que sale ahora, con lo de antes
            i_ = conduce_ ? corriente_de(out_, id_out_) : 0.0;
            // Los diodos: la salida que se suelta llevando corriente
            if (diodo_ != 0 && ((diodo_ < 0 && i_ < 0) || (diodo_ > 0 && i_ > 0))) diodo_ = 0;
            if (despierto_) diodo_ = 0;
            else if (diodo_ == 0 && conduce_ && std::fabs(i_) > 1e-3) diodo_ = i_ > 0 ? -1 : 1;
            conduce_ = despierto_ || diodo_ != 0;
            float vd = 0.f, rd = R_HIZ;
            if (despierto_ && alto_)   { vd = float(vg + vs); rd = float(c_.r_alto); }
            else if (despierto_)       { vd = float(vg);      rd = float(c_.r_bajo); }
            else if (diodo_ > 0)       { vd = float(vg + vs + c_.vd); rd = float(c_.rd); }
            else if (diodo_ < 0)       { vd = float(vg - c_.vd);      rd = float(c_.rd); }
            // Solo si cambia de verdad: VS se mueve un poco con lo que se le
            // pide, y sin el umbral se volvería a poner en bucle
            if (rd != r_drv_ || std::fabs(vd - v_drv_) > 2e-4f) {
                v_drv_ = vd; r_drv_ = rd;
                if (!conduce_) out_->set_hiz(id_out_);
                else out_->set_drive(id_out_, vd, rd);
                ev_.notify(sc_core::SC_ZERO_TIME);
                wait(despierta());
                continue;
            }
            // Lo que pide a VS, lo que disipa, y la corriente de IS
            const double i_alto = (despierto_ && alto_ && i_ > 0.0) ? i_ : 0.0;
            p_ = i_ * i_ * (despierto_ ? (alto_ ? c_.r_alto : c_.r_bajo) : 0.0);
            pide(i_alto + (despierto_ ? c_.iq : 0.0), vs);
            i_is_ = i_alto / c_.k_is;
            pon_is(vs);
            if (std::fabs(i_) > c_.i_max) {
                char b[160];
                std::snprintf(b, sizeof b, "%s: %.1f A por la salida, y el BTS7960 limita hacia los "
                              "%.0f", pieza().c_str(), std::fabs(i_), c_.i_max);
                avisa("sobrecorriente", b);
            }
            wait(despierta());
        }
    }
    // IS es una fuente de corriente: lo que tiene colgado, visto como
    // Thevenin, y la tensión que hace pasar por ahí exactamente i_is_
    void pon_is(double vs) {
        if (!is_) return;
        bool solo = false;
        const double vx = double(is_->voltage_excluding(id_is_, solo));
        const double gx = is_->conductance() - 1.0 / double(r_is_);
        double vv = 0.0;
        float rr = 1e6f;
        if (!conectada_) { is_->set_hiz(id_is_); r_is_ = R_HIZ; return; }
        if (solo || gx <= G_FLOAT) { vv = double(gnd_->voltage()) + vs; rr = 1e6f; }
        else { vv = vx + i_is_ / gx; rr = 1.0f; }
        if (std::fabs(vv - v_is_) < 1e-4 && rr == r_is_) return;
        v_is_ = vv; r_is_ = rr;
        is_->set_drive(id_is_, float(vv), rr);
    }
    // Lo que pide a VS, como una resistencia: solo si cambia en más de un
    // miliamperio o de un 0,1 %, o se reajustaría en bucle con su propia caída
    void pide(double i, double vs) {
        if (!conectada_ || vs < 0.05 || i <= 1e-6) {
            if (r_vs_ != 0.0) { carga_->suelta(); r_vs_ = 0.0; i_vs_ = 0.0; }
            return;
        }
        if (r_vs_ != 0.0 && std::fabs(i - i_vs_) < std::max(1e-3, 1e-3 * i)) return;
        r_vs_ = vs / i;
        i_vs_ = i;
        carga_->resuelve(true, r_vs_);
    }
    const sc_core::sc_event_or_list& despierta() {
        if (!despierta_) {
            despierta_.reset(new sc_core::sc_event_or_list());
            *despierta_ |= vs_->value_changed_event();
            *despierta_ |= gnd_->value_changed_event();
            *despierta_ |= in_->value_changed_event();
            *despierta_ |= inh_->value_changed_event();
            *despierta_ |= out_->value_changed_event();
            if (is_) *despierta_ |= is_->value_changed_event();
            if (vdd_) *despierta_ |= vdd_->value_changed_event();
            *despierta_ |= evento_conexion();
            *despierta_ |= ev_;
        }
        return *despierta_;
    }
    void avisa(const std::string& que, const std::string& t) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("bts7960", t.c_str());
    }

    analog_net_if *vs_, *gnd_, *in_, *inh_, *out_, *is_, *vdd_;
    ConfigMedioPuente c_;
    std::unique_ptr<RamaDosNodos> carga_;
    int id_out_ = -1, id_is_ = -1;
    bool despierto_ = false, alto_ = false, conduce_ = false;
    int diodo_ = 0;
    float v_drv_ = 0.f, r_drv_ = R_HIZ, r_is_ = R_HIZ;
    double v_is_ = -1.0;
    double i_ = 0.0, i_is_ = 0.0, p_ = 0.0, r_vs_ = 0.0, i_vs_ = 0.0;
    double med_[2] = {0.0, 0.0}, t_med_ = 0.0;
    sc_core::sc_event ev_;
    std::unique_ptr<sc_core::sc_event_or_list> despierta_;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_POTENCIA_H
