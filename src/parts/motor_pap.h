// =============================================================================
// motor_pap.h — Un motor PASO A PASO híbrido bipolar de dos fases
//
// (Fase F1a de doc/analisis_cnc.md, §5.1.)
//
// El de las impresoras y las fresadoras de sobremesa: NEMA 17 o 23, 200 pasos
// por vuelta (1,8°), dos bobinas, A y B, cada una entre dos hilos. Es el
// modelo clásico, con Nr = pasos/4 dientes de rotor:
//
//     va = R·ia + L·dia/dt − Km·ω·sin(Nr·θ)
//     vb = R·ib + L·dib/dt + Km·ω·cos(Nr·θ)
//     T  = −Km·ia·sin(Nr·θ) + Km·ib·cos(Nr·θ) − Td·sin(4·Nr·θ)
//
// con Td el par de retención sin corriente (detent). Todo lo que importa sale
// de ahí sin programarlo aparte: la curva par-velocidad, que cae cuando la
// f.e.m. y L·di/dt se comen la tensión del bus; la PÉRDIDA DE PASOS por
// aceleración o carga excesivas; la resonancia; y el retraso del rotor tras el
// campo, que crece con la carga. Con la corriente en la A, el rotor va a θ = 0;
// en la B, un paso más allá, 1,8° a la DERECHA (el ángulo positivo).
//
// CADA BOBINA SE CONECTA COMO ESTÉ CABLEADA. Se mira en cada vuelta:
//
//   * entre las dos salidas de una misma fase de un driver de CORRIENTE
//     IMPUESTA (DriverPaP; common/analog_net.h, FaseCorriente): si la tensión
//     que haría falta, R·i* + f.e.m., cabe en el tope de ±Vbus, la corriente
//     es la consigna -lo que consigue un chopper bueno-; si no, la tensión se
//     queda en el tope y la corriente va por detrás. Con el driver suelto, la
//     corriente vuelve al bus por sus diodos, contra Vbus + 2·Vd, hasta
//     acabarse. Las dos salidas al revés: la consigna con el signo cambiado;
//   * entre dos nodos de Thevenin -un L298, unas fuentes-: como el MotorCC,
//     una f.e.m. con R y L en serie (RamaFem);
//   * con un extremo al aire, o entre salidas de dos fases o dos drivers: sin
//     corriente, y lo segundo se avisa.
//
// LO MECÁNICO es de su cadena (parts/cadena.h): el motor pone el par, la
// inercia de su rotor, un rozamiento seco y uno viscoso, y la carga de su
// mando. Su par depende del ángulo, así que la cadena integra también el
// ángulo con Runge-Kutta.
//
// LOS PASOS PERDIDOS. El motor sabe dónde tendría que estar: donde lo llevan
// las corrientes que le piden -la consigna, o lo que dejarían pasar las ramas
// de Thevenin-, Nr·θ* = atan2(ib*, ia*), seguido vuelta a vuelta. La primera
// vez que tiene corriente se queda con esa referencia; desde ahí, el RETRASO
// es la diferencia, en pasos enteros, y un rotor que se descuelga cae en el
// equilibrio de al lado: cuatro pasos (una vuelta eléctrica) por cada vez. Eso
// son los PASOS PERDIDOS, y una alarma.
//
// EL MODELO IDEAL (`modelo="ideal"`): cada paso es exactamente un paso, sin
// corrientes ni pérdidas. El rotor va a donde dicen las consignas. Cuesta casi
// nada, y vale para probar firmware deprisa; el dinámico es el de omisión.
// =============================================================================
#ifndef STM32_PARTS_MOTOR_PAP_H
#define STM32_PARTS_MOTOR_PAP_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"
#include "cadena.h"
#include "motor_cc.h"      // RamaFem

namespace stm32 {

struct ConfigMotorPaP {
    // Las cifras de la hoja de datos, de la conexión que se dice
    double par_retencion_ncm = 25.0;   // con las dos fases a la corriente nominal
    double corriente_a = 1.3;          // la nominal, por fase
    double r_ohm = 2.4;                // por fase
    double l_mh = 2.8;                 // por fase
    // "bipolar": las cifras son las de cada bobina tal cual. "serie" y
    // "paralelo": un motor de OCHO hilos, y las cifras de arriba son las de
    // media bobina, las de unipolar, que es como las da Nanotec
    std::string conexion = "bipolar";
    bool   retencion_una_fase = false; // el par de retención, medido con una fase
    double km = 0.0;                   // N·m/A; 0: del par de retención
    double detent_ncm = 1.5;           // sin corriente
    double inercia_gcm2 = 34.0;        // la del rotor
    double rozamiento_ncm = 0.5;       // seco, de sus rodamientos
    double viscoso_mnms = 0.1;         // mN·m por rad/s
    unsigned pasos = 200;              // por vuelta, enteros
    bool   ideal = false;              // cada paso, un paso: sin corrientes
    double posicion_inicial = 0.0;     // grados
};

SC_MODULE(MotorPaP), public ExtPartBase, public Actuador {
    static constexpr double PI = 3.14159265358979323846;
    // El paso de integración: más largo que el del MotorCC, porque el chopper
    // ya se ha quitado de en medio -la corriente sigue a su consigna sin
    // integrar nada- y lo que queda es la mecánica y las subidas de la
    // corriente contra el tope, de un kHz y pico
    static constexpr double PASO = 10e-6;
    static constexpr double TOL = 1e-6;            // A: la corriente ya está en la consigna

    MotorPaP(sc_core::sc_module_name nm, analog_net_if& a1, analog_net_if& a2,
             analog_net_if& b1, analog_net_if& b2, const ConfigMotorPaP& c = ConfigMotorPaP())
        : sc_core::sc_module(nm), ExtPartBase("MotorPaP", nm), c_(c),
          propia_(c.posicion_inicial * PI / 180.0) {
        bob_[0].p = &a1; bob_[0].m = &a2;
        bob_[1].p = &b1; bob_[1].m = &b2;
        const int ia1 = add_pin("a1", a1, "motor_pap");
        const int ia2 = add_pin("a2", a2, "motor_pap");
        const int ib1 = add_pin("b1", b1, "motor_pap");
        const int ib2 = add_pin("b2", b2, "motor_pap");
        bob_[0].rama.reset(new RamaFem(a1, ia1, a2, ia2));
        bob_[1].rama.reset(new RamaFem(b1, ib1, b2, ib2));
        // Las cifras de cada fase, según la conexión
        r_ = c_.r_ohm;
        l_ = c_.l_mh * 1e-3;
        i_nom_ = c_.corriente_a;
        if (c_.conexion == "serie")        { r_ *= 2.0; l_ *= 4.0; i_nom_ /= std::sqrt(2.0); }
        else if (c_.conexion == "paralelo") { r_ *= 0.5;            i_nom_ *= std::sqrt(2.0); }
        t_ret_ = c_.par_retencion_ncm * 1e-2;
        km_ = c_.km > 0.0 ? c_.km
            : t_ret_ / (i_nom_ * (c_.retencion_una_fase ? 1.0 : std::sqrt(2.0)));
        td_ = c_.detent_ncm * 1e-2;
        j_ = c_.inercia_gcm2 * 1e-7;
        tf_ = c_.rozamiento_ncm * 1e-2;
        b_ = c_.viscoso_mnms * 1e-3;
        nr_ = c_.pasos / 4.0;
        propia_.pon_actuador(this);
        propia_.cierra();
        SC_HAS_PROCESS(MotorPaP);
        SC_THREAD(motor);
    }
    ~MotorPaP() override {
        for (Bobina& b : bob_) b.rama->suelta();
    }

    // --- Desde C++ -------------------------------------------------------------
    double rpm() const {
        return c_.ideal ? w_ideal_ * 60.0 / (2.0 * PI) : cadena_->w() * 60.0 / (2.0 * PI);
    }
    double angulo() const {
        double g = std::fmod(cadena_->th() * 180.0 / PI, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    double vueltas() const { return cadena_->th() / (2.0 * PI); }
    // En pasos enteros desde el arranque
    double posicion_pasos() const { return cadena_->th() * 2.0 * nr_ / PI; }
    double corriente_ma(int k) const { return (c_.ideal ? estable_de(k) : i_[k]) * 1e3; }
    // El par que da ahora, sin el rozamiento
    double par() const { return par_de(i_[0], i_[1], cadena_->th()); }
    // El retraso del rotor tras donde lo llevan las corrientes, en pasos
    // enteros, de -2 a 2; y los pasos perdidos, de cuatro en cuatro
    double retraso() const { return ref_ ? retraso_ : 0.0; }
    long   pasos_perdidos() const { return perdidos_; }
    bool   bloqueado() const override { return bloq_; }
    void   bloquea(bool si) { bloq_ = si; cadena_->toca(); }
    double carga() const { return carga_; }
    void   pon_carga(double f) { carga_ = std::max(0.0, std::min(1.0, f)); cadena_->toca(); }
    double km() const { return km_; }
    double r_fase() const { return r_; }
    double l_fase() const { return l_; }
    double corriente_nominal() const { return i_nom_; }
    double par_retencion() const { return t_ret_; }
    bool   ideal() const { return c_.ideal; }
    const char* modo(int k) const {
        switch (bob_[k].modo) {
            case Corriente: return "corriente";
            case Tension:   return "tension";
            case Mal:       return "mal";
            default:        return "suelta";
        }
    }
    const ConfigMotorPaP& config() const { return c_; }

    // --- Lo que deja ver y tocar -----------------------------------------------
    unsigned n_observables() const override { return 8; }
    Observable observable(unsigned i) const override {
        const float im = float(i_nom_ * 1500.0);
        switch (i) {
            case 0: return {"angulo", "°", 0.f, 360.f, true};
            case 1: return {"rpm", "rpm", -3000.f, 3000.f, false};
            case 2: return {"corriente_a", "mA", -im, im, false};
            case 3: return {"corriente_b", "mA", -im, im, false};
            case 4: return {"par", "N·m", -float(t_ret_ * 1.5), float(t_ret_ * 1.5), false};
            case 5: return {"retraso", "pasos", -2.f, 2.f, false};
            case 6: return {"pasos_perdidos", "", -1e6f, 1e6f, false};
            default: return {"pierde_pasos", "", 0.f, 1.f, true, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(angulo());
            case 1: return float(rpm());
            case 2: return float(corriente_ma(0));
            case 3: return float(corriente_ma(1));
            case 4: return float(par());
            case 5: return float(retraso());
            case 6: return float(perdidos_);
            default: return perdidos_ != 0 ? 1.f : 0.f;
        }
    }
    unsigned n_mandos() const override { return 2; }
    Mando mando(unsigned i) const override {
        if (i == 0) return {"bloquear", Mando::Interruptor, 0.f, 1.f};
        return {"carga", Mando::Continuo, 0.f, 100.f};
    }
    float valor_mando(unsigned i) const override {
        return i == 0 ? (bloq_ ? 1.f : 0.f) : float(carga_ * 100.0);
    }
    void acciona(unsigned i, float v) override {
        if (i == 0) bloquea(v >= 0.5f);
        else pon_carga(double(v) / 100.0);
    }

    // =========================================================================
    // COMO ACTUADOR: las corrientes de las dos bobinas, y el par
    // =========================================================================
    unsigned n_e() const override { return c_.ideal ? 0 : 2; }
    void lee_e(double* e) const override { if (!c_.ideal) { e[0] = i_[0]; e[1] = i_[1]; } }
    void pon_e(const double* e) override { if (!c_.ideal) { i_[0] = e[0]; i_[1] = e[1]; } }
    bool depende_del_angulo() const override { return !c_.ideal; }
    void deriva(const double* e, double w, double th, double* de, double& par) const override {
        if (c_.ideal) { par = 0.0; return; }
        const double s = std::sin(nr_ * th), co = std::cos(nr_ * th);
        const double fem[2] = {-km_ * w * s, km_ * w * co};
        for (int k = 0; k < 2; ++k) de[k] = di(k, e[k], fem[k]);
        // sin(4x) = 4·s·c·(c² - s²): sin otra llamada a sin
        par = -km_ * e[0] * s + km_ * e[1] * co - td_ * 4.0 * s * co * (co * co - s * s);
    }
    bool con_marcas() const override { return false; }
    // El régimen de cada bobina en este paso, que no cambia hasta el siguiente:
    // sigue a su consigna, sube o baja contra el tope, se queda en el tope
    // porque no llega, o vuelve por los diodos
    void prepara(double* e, double w, double th) const override {
        if (c_.ideal) return;
        const double s = std::sin(nr_ * th), co = std::cos(nr_ * th);
        const double fem[2] = {-km_ * w * s, km_ * w * co};
        for (int k = 0; k < 2; ++k) {
            const Bobina& b = bob_[k];
            if (b.modo == Suelta || b.modo == Mal) e[k] = 0.0;
            if (b.modo != Corriente) { reg_[k] = Nada; continue; }
            if (!b.activa) {
                reg_[k] = e[k] == 0.0 ? Nada : Diodos;
                u_[k] = -signo(e[k]) * (b.vbus + 2.0 * b.vd);
                continue;
            }
            const double hace_falta = r_ * b.obj + fem[k];
            if (std::fabs(e[k] - b.obj) <= TOL) {
                if (std::fabs(hace_falta) <= b.vbus) { reg_[k] = Sigue; e[k] = b.obj; }
                else { reg_[k] = Tope; u_[k] = signo(hace_falta) * b.vbus; }
            } else {
                reg_[k] = Tope;
                u_[k] = e[k] < b.obj ? b.vbus : -b.vbus;
            }
        }
    }
    // La corriente que cruza su consigna se queda en ella -el chopper la
    // sujeta-; la que vuelve por los diodos, al acabarse, se queda en cero
    void recorta(const double* e0, double* e) const override {
        if (c_.ideal) return;
        for (int k = 0; k < 2; ++k) {
            const Bobina& b = bob_[k];
            if (b.modo != Corriente || reg_[k] == Sigue || reg_[k] == Nada) continue;
            const double obj = b.activa ? b.obj : 0.0;
            if ((e0[k] - obj) * (e[k] - obj) <= 0.0) e[k] = obj;
        }
    }
    double paso_max() const override {
        if (c_.ideal) return Actuador::INF;
        double h = Actuador::INF;
        for (const Bobina& b : bob_) {
            if (b.modo == Corriente) h = std::min(h, std::min(PASO, 0.2 * l_ / r_));
            else if (b.modo == Tension) h = std::min(h, std::min(PASO, 0.2 * l_ / b.rt));
        }
        return h;
    }
    bool inerte() const override {
        if (c_.ideal) return true;
        if (td_ != 0.0) return false;
        for (int k = 0; k < 2; ++k)
            if (bob_[k].modo == Tension || (bob_[k].modo == Corriente && (bob_[k].activa ||
                                                                           i_[k] != 0.0)))
                return false;
        return true;
    }
    bool asentado(const double* e) const override {
        if (c_.ideal) return true;
        for (int k = 0; k < 2; ++k) {
            const Bobina& b = bob_[k];
            switch (b.modo) {
                case Corriente:
                    if (b.activa) {
                        if (std::fabs(e[k] - b.obj) > TOL || std::fabs(r_ * b.obj) > b.vbus)
                            return false;
                    } else if (e[k] != 0.0) {
                        return false;
                    }
                    break;
                case Tension:
                    if (std::fabs(e[k] - b.vth / b.rt) >= 1e-7) return false;
                    break;
                default:
                    if (e[k] != 0.0) return false;
            }
        }
        return true;
    }
    double par_asentado(double th) const override {
        if (c_.ideal) return 0.0;
        return par_de(estable_de(0), estable_de(1), th);
    }
    void limpia(double* e) const override { asienta(e); }
    void asienta(double* e) const override {
        if (c_.ideal) return;
        for (int k = 0; k < 2; ++k) e[k] = estable_de(k);
    }
    bool asentando(const double*, const double* de) const override {
        if (c_.ideal) return false;
        return std::fabs(de[0]) * Cadena::REFRESCO_I > 1e-3 ||
               std::fabs(de[1]) * Cadena::REFRESCO_I > 1e-3;
    }
    // Lo que entra en cada bobina desde su driver, paso a paso
    void hecho(const double* e0, const double* e, double w, double th, double h) override {
        if (c_.ideal) return;
        const double s = std::sin(nr_ * th), co = std::cos(nr_ * th);
        const double fem[2] = {-km_ * w * s, km_ * w * co};
        for (int k = 0; k < 2; ++k) {
            Bobina& b = bob_[k];
            if (b.modo != Corriente || !b.fc) continue;
            // Por el balance de la bobina, que vale aunque la corriente haya
            // llegado a su consigna a mitad del paso: lo que se queda en R, lo
            // que va a la inductancia y lo que se convierte en par
            const double i0 = e0[k], i1 = e[k];
            const double i2 = (i0 * i0 + i0 * i1 + i1 * i1) / 3.0;
            b.fc->energia += r_ * i2 * h + 0.5 * l_ * (i1 * i1 - i0 * i0) +
                             fem[k] * 0.5 * (i0 + i1) * h;
            b.fc->i2s += i2 * h;
        }
    }
    double inercia() const override { return j_; }
    double rozamiento() const override { return tf_ + carga_ * t_ret_; }
    double viscoso() const override { return b_; }
    double posicion_inicial() const override { return c_.posicion_inicial * PI / 180.0; }
    std::string nombre_actuador() const override { return pieza(); }

    void regimen() override {
        const double t = sc_core::sc_time_stamp().to_seconds();
        // El modelo ideal no integra: lo que gasta es R·i², con la corriente
        // que había desde la última vez
        if (c_.ideal)
            for (Bobina& b : bob_)
                if (b.modo == Corriente && b.fc && b.activa) {
                    const double dt = t - t_regimen_;
                    b.fc->energia += r_ * b.obj * b.obj * dt;
                    b.fc->i2s += b.obj * b.obj * dt;
                }
        t_regimen_ = t;
        for (int k = 0; k < 2; ++k) mira_bobina(k);
        sigue_consigna();
        // Lo que se deja en el driver: la tensión que pone y la corriente
        const double th = cadena_->th(), w = cadena_->w();
        const double fem[2] = {-km_ * w * std::sin(nr_ * th), km_ * w * std::cos(nr_ * th)};
        for (int k = 0; k < 2; ++k) {
            Bobina& b = bob_[k];
            if (b.modo != Corriente) continue;
            double i = i_[k], v = 0.0;
            if (c_.ideal) { i = b.activa ? b.obj : 0.0; v = r_ * i; }
            else          v = tension_aplicada(k, i_[k], fem[k]);
            b.fc->v_carga = b.signo * v;
            b.fc->i_carga = b.signo * i;
            b.fc->con_carga = true;
            b.fc->t_carga = t;
        }
    }
    void eventos(sc_core::sc_event_or_list& l) override {
        for (Bobina& b : bob_) {
            l |= b.p->value_changed_event();
            l |= b.m->value_changed_event();
            for (const analog_net_if* n : {b.p, b.m})
                for (const SalidaCorriente& s : n->salidas_corriente())
                    if (s.fase && s.fase->ev) l |= *s.fase->ev;
        }
        l |= evento_conexion();
    }

private:
    enum Modo { Suelta, Tension, Corriente, Mal };
    enum Regimen { Nada, Sigue, Tope, Diodos };
    struct Bobina {
        analog_net_if *p = nullptr, *m = nullptr;
        std::unique_ptr<RamaFem> rama;
        Modo modo = Suelta;
        // Con corriente impuesta: la fase del driver, el signo y lo que manda
        FaseCorriente* fc = nullptr;
        int signo = 1;
        double obj = 0.0, vbus = 0.0, vd = 0.7;
        bool activa = false;
        // Con Thevenin: lo que ven sus bornes
        double vth = 0.0, rt = 1.0;
    };
    static int signo(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

    double par_de(double ia, double ib, double th) const {
        return -km_ * ia * std::sin(nr_ * th) + km_ * ib * std::cos(nr_ * th) -
               td_ * std::sin(4.0 * nr_ * th);
    }
    // La corriente a la que va la bobina k si nada cambia
    double estable_de(int k) const {
        const Bobina& b = bob_[k];
        if (b.modo == Corriente) return b.activa ? b.obj : 0.0;
        if (b.modo == Tension) return b.vth / b.rt;
        return 0.0;
    }
    // La tensión que pone el driver en la bobina k, con su corriente i y su f.e.m.
    double tension_aplicada(int k, double i, double fem) const {
        const Bobina& b = bob_[k];
        if (!b.activa) return i == 0.0 ? 0.0 : -signo(i) * (b.vbus + 2.0 * b.vd);
        const double hace_falta = r_ * b.obj + fem;
        if (std::fabs(i - b.obj) <= TOL) {
            if (std::fabs(hace_falta) <= b.vbus) return hace_falta;
            return signo(hace_falta) * b.vbus;
        }
        return i < b.obj ? b.vbus : -b.vbus;
    }
    // di/dt de la bobina k, con el régimen de este paso
    double di(int k, double i, double fem) const {
        const Bobina& b = bob_[k];
        switch (b.modo) {
            case Corriente:
                if (reg_[k] == Sigue || reg_[k] == Nada) return 0.0;
                return (u_[k] - r_ * i - fem) / l_;
            case Tension:
                return (b.vth - fem - b.rt * i) / l_;
            default:
                return 0.0;
        }
    }

    // Cómo está cableada la bobina k, y lo que ve
    void mira_bobina(int k) {
        Bobina& b = bob_[k];
        const std::vector<SalidaCorriente>& sp = b.p->salidas_corriente();
        const std::vector<SalidaCorriente>& sm = b.m->salidas_corriente();
        const char* nb = k == 0 ? "A" : "B";
        Modo m = Suelta;
        if (!conectada_) {
            m = Suelta;
        } else if (sp.empty() && sm.empty()) {
            double vth = 0, rth = 0;
            if (b.rama->fuera(vth, rth)) {
                m = Tension;
                b.vth = vth;
                b.rt = r_ + rth;
            }
        } else if (sp.size() == 1 && sm.size() == 1 && sp[0].fase == sm[0].fase &&
                   sp[0].polo == -sm[0].polo) {
            m = Corriente;
            b.fc = sp[0].fase;
            b.signo = sp[0].polo;
            b.obj = b.signo * b.fc->i;
            b.vbus = b.fc->vbus;
            b.vd = b.fc->vd;
            b.activa = b.fc->activa && b.vbus > 0.0;
        } else {
            m = Mal;
            char t[240];
            if (sp.size() > 1 || sm.size() > 1)
                std::snprintf(t, sizeof t, "%s: la bobina %s va a un nodo con dos salidas de "
                              "driver: estan en corto", pieza().c_str(), nb);
            else if (sp.empty() || sm.empty())
                std::snprintf(t, sizeof t, "%s: la bobina %s tiene un extremo en la salida de "
                              "un driver y el otro en otra cosa: no hay corriente",
                              pieza().c_str(), nb);
            else
                std::snprintf(t, sizeof t, "%s: la bobina %s va entre la fase %d de %s y la "
                              "fase %d de %s: cada bobina, entre las dos salidas de una fase",
                              pieza().c_str(), nb, sp[0].fase->numero, sp[0].fase->quien,
                              sm[0].fase->numero, sm[0].fase->quien);
            avisa(std::string("mal_") + nb, t);
        }
        if (m != Corriente && b.fc) {
            b.fc->con_carga = false;
            b.fc = nullptr;
        }
        // La rama: con Thevenin, la que hace pasar la corriente de la bobina
        // -una f.e.m. que deja en los bornes Vth - Rth·i-; al aire, la f.e.m.
        // del giro en el extremo suelto; con corriente impuesta o mal
        // cableada, nada: los nodos son del driver
        if (m == Tension) {
            b.rama->resuelve(true, b.vth - i_[k] * b.rt, r_);
        } else if (m == Suelta) {
            const double th = cadena_->th(), w = cadena_->w();
            const double fem = k == 0 ? -km_ * w * std::sin(nr_ * th)
                                      : km_ * w * std::cos(nr_ * th);
            b.rama->resuelve(conectada_, fem, r_);
        } else {
            b.rama->suelta();
        }
        if (m == Suelta || m == Mal) i_[k] = 0.0;
        b.modo = m;
        // Una bobina al aire con la otra movida
        const Bobina& o = bob_[1 - k];
        if (m == Suelta && conectada_ && (o.modo == Corriente || o.modo == Tension)) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: la bobina %s esta al aire: con una fase sola el "
                          "motor no gira, solo se queda en un sitio", pieza().c_str(), nb);
            avisa(std::string("aire_") + nb, t);
        }
    }

    // Dónde lo llevan las corrientes que le piden, vuelta a vuelta, y lo que
    // va por detrás; y el rotor del modelo ideal
    void sigue_consigna() {
        const double a = estable_de(0), b = estable_de(1);
        const bool hay = a != 0.0 || b != 0.0;
        if (hay) {
            const double phi = std::atan2(b, a);
            if (!ref_) {
                phi_ = phi;
                // La primera vez, el equilibrio más cercano
                const double nth = nr_ * cadena_->th();
                if (c_.ideal) off_ = phi - nth;
                else off_ = 2.0 * PI * std::round((phi - nth) / (2.0 * PI));
                ref_ = true;
            } else {
                double d = phi - std::remainder(phi_, 2.0 * PI);
                d = std::remainder(d, 2.0 * PI);
                phi_ += d;
            }
        }
        if (c_.ideal && ref_ && hay && !bloq_) {
            const double th = (phi_ - off_) / nr_;
            const double t = sc_core::sc_time_stamp().to_seconds();
            if (th != cadena_->th()) {
                const double dt = t - t_ideal_;
                w_ideal_ = dt > 0.0 ? (th - cadena_->th()) / dt : 0.0;
                t_ideal_ = t;
                cadena_->impone(th);
            } else if (t - t_ideal_ > 0.05) {
                w_ideal_ = 0.0;
            }
        }
        if (ref_) {
            const double lag = phi_ - off_ - nr_ * cadena_->th();
            const long vueltas = std::lround(lag / (2.0 * PI));
            retraso_ = (lag - 2.0 * PI * double(vueltas)) / (PI / 2.0);
            const long antes = perdidos_;
            perdidos_ = 4 * vueltas;
            if (perdidos_ != antes && perdidos_ != 0) {
                char t[200];
                std::snprintf(t, sizeof t, "%s: ha perdido pasos: el rotor va %ld pasos por "
                              "detras de donde lo llevan las corrientes", pieza().c_str(),
                              perdidos_);
                avisa("perdidos", t);
            }
        }
    }

    void motor() {
        if (en_transmision()) return;
        propia_.corre();
    }
    bool en_transmision() const { return cadena_ != &propia_; }
    void avisa(const std::string& que, const std::string& t) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("motor_pap", t.c_str());
    }

    ConfigMotorPaP c_;
    Bobina bob_[2];
    double r_ = 1.0, l_ = 1e-3, i_nom_ = 1.0, t_ret_ = 0.25, km_ = 0.1, td_ = 0.0, j_ = 1e-5,
           tf_ = 0.0, b_ = 0.0, nr_ = 50.0;
    Cadena propia_;
    double i_[2] = {0.0, 0.0};
    // El régimen de cada bobina en el paso de ahora (véase prepara)
    mutable int reg_[2] = {Nada, Nada};
    mutable double u_[2] = {0.0, 0.0};
    // Los pasos
    bool ref_ = false;
    double phi_ = 0.0, off_ = 0.0, retraso_ = 0.0;
    long perdidos_ = 0;
    double w_ideal_ = 0.0, t_ideal_ = 0.0, t_regimen_ = 0.0;
    // Los mandos
    bool bloq_ = false;
    double carga_ = 0.0;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_MOTOR_PAP_H
