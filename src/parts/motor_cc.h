// =============================================================================
// motor_cc.h — Un motor de corriente continua con reductora y, si se le dan sus
//              patillas, un encoder incremental en el eje de salida
//
// El de los coches de juguete y los robots de prácticas: el motorreductor
// amarillo de 3 a 6 V, con su reductora de plástico de 1:48 y el eje doble.
// Las cifras de omisión son las de ese motor [Adafruit 3777, «DC Gearbox Motor
// "TT Motor" - 200RPM - 3 to 6VDC», medidas de la página: 250 rpm y 160 mA en
// vacío y 1,5 A bloqueado a 6 V; 120 rpm y 150 mA a 3 V].
//
//   a, b         los dos bornes del motor: con a más alto que b gira a la
//                DERECHA -en el sentido de las agujas del reloj, mirando el eje
//                de salida desde su extremo-, que es el ángulo positivo
//   enc_vcc,     el ENCODER, opcional: un disco ranurado de `ppr` ventanas
//   enc_gnd,     montado de través en el eje de salida, con dos sensores
//   enc_a,       ópticos desfasados un cuarto de ventana y su comparador. A y B
//   enc_b        dan `ppr` pulsos por vuelta del eje, en cuadratura: girando a
//                la derecha, A va delante -00, 10, 11, 01-; a la izquierda, B.
//                Las salidas son las del comparador, de colector abierto con un
//                pull-up de 10 kohm a enc_vcc en la placa del sensor: altas con
//                10 kohm, bajas con 100 ohm. Sin tensión -menos de 2,5 V entre
//                enc_vcc y enc_gnd- se quedan al aire.
//
// LO ELÉCTRICO: una fuerza contraelectromotriz E = Ke·w en serie con la
// resistencia del bobinado R, entre a y b: Va - Vb = E + R·i. Sin inductancia:
// la corriente sigue a la tensión al momento. Es la simplificación que se
// nota con PWM rápido -en el motor de verdad, la bobina alisa la corriente
// dentro de cada periodo; aquí se corta con él-, y no cambia la media: con el
// mismo ciclo de trabajo, la misma velocidad.
//
// LO MECÁNICO, en el eje de salida, con la reductora dentro:
//
//     J·dw/dt = Kt·i - Tf·sgn(w) - Tc·sgn(w)
//
// con Kt = Ke, un rozamiento seco Tf -el que se lleva la corriente en vacío-,
// uno de arranque Ts que hay que vencer para que se mueva parado -el de
// `i_arranque_ma`- y la carga Tc del mando `carga`, que se opone al giro. J
// sale de la constante de tiempo `tau_ms`. Entre dos cambios de lo que hay en
// los bornes, la tensión de Thevenin que ve el motor es constante, y esa
// ecuación tiene solución exacta -una exponencial, o una recta sin corriente-:
// el eje NO se integra a pasos. Se despierta en cada cambio de los bornes, en
// cada flanco del encoder -calculado, no muestreado-, cuando se para, y cada
// milisegundo mientras acelera o frena, para que la corriente que se ve y la
// f.e.m. que ven los demás sigan al eje.
//
// Las cifras se dan como en una hoja de datos -`v_nominal`, `rpm_vacio`,
// `i_vacio_ma`, `i_bloqueo_ma`- y de ahí sale el resto: R = v_nominal /
// i_bloqueo; Ke = (v_nominal - i_vacio·R) / w_vacio.
// =============================================================================
#ifndef STM32_PARTS_MOTOR_CC_H
#define STM32_PARTS_MOTOR_CC_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// UNA RAMA CON FUENTE entre dos nodos: una f.e.m. `e` en serie con `r`, de a
// a b: Va - Vb = e + r·i. Es la RamaDosNodos de una resistencia con la fuente
// dentro, en los dos sentidos -la del diodo solo conduce en uno-. Con un
// extremo al aire no hay corriente, y ese extremo se queda a la tensión del
// otro más la f.e.m.: el borne suelto de un motor que gira enseña la que
// genera. Con los dos al aire, no hace nada.
// ---------------------------------------------------------------------------
class RamaFem {
public:
    RamaFem(analog_net_if& a, int id_a, analog_net_if& b, int id_b)
        : a_(&a), b_(&b), id_a_(id_a), id_b_(id_b) {}

    // Lo que ve la rama desde fuera: la tensión de Thevenin de a menos la de
    // b, y la suma de sus resistencias. Falso si un extremo está al aire
    bool fuera(double& vth, double& rth) const {
        bool solo_a = false, solo_b = false;
        const double va0 = double(a_->voltage_excluding(id_a_, solo_a));
        const double vb0 = double(b_->voltage_excluding(id_b_, solo_b));
        const double ga  = a_->conductance() - 1.0 / double(ra_);
        const double gb  = b_->conductance() - 1.0 / double(rb_);
        if (solo_a || solo_b || ga <= G_FLOAT || gb <= G_FLOAT) return false;
        vth = va0 - vb0;
        rth = 1.0 / ga + 1.0 / gb;
        return true;
    }

    void resuelve(bool cerrada, double e, double r) {
        if (!cerrada) { suelta(); return; }
        bool solo_a = false, solo_b = false;
        const double va0 = double(a_->voltage_excluding(id_a_, solo_a));
        const double vb0 = double(b_->voltage_excluding(id_b_, solo_b));
        const double ga  = a_->conductance() - 1.0 / double(ra_);
        const double gb  = b_->conductance() - 1.0 / double(rb_);
        const bool flota_a = solo_a || ga <= G_FLOAT, flota_b = solo_b || gb <= G_FLOAT;
        if (flota_a && flota_b) { suelta(); return; }
        if (flota_a || flota_b) {
            if (flota_a) {
                b_->set_hiz(id_b_); rb_ = R_HIZ;
                ra_ = std::max(0.1f, float(r + 1.0 / gb));
                a_->set_drive(id_a_, float(vb0 + e), ra_);
            } else {
                a_->set_hiz(id_a_); ra_ = R_HIZ;
                rb_ = std::max(0.1f, float(r + 1.0 / ga));
                b_->set_drive(id_b_, float(va0 - e), rb_);
            }
            return;
        }
        const double rag = 1.0 / ga, rbg = 1.0 / gb;
        const double i = (va0 - vb0 - e) / (rag + r + rbg);
        const double va = va0 - i * rag, vb = vb0 + i * rbg;
        ra_ = rb_ = std::max(0.1f, float(r));
        a_->set_drive(id_a_, float(vb + e), ra_);
        b_->set_drive(id_b_, float(va - e), rb_);
    }
    void suelta() {
        a_->set_hiz(id_a_); ra_ = R_HIZ;
        b_->set_hiz(id_b_); rb_ = R_HIZ;
    }
    // La corriente de a a b, en A
    double corriente() const { return -double(a_->current(id_a_)); }

private:
    analog_net_if *a_, *b_;
    int   id_a_, id_b_;
    float ra_ = R_HIZ, rb_ = R_HIZ;
};

// Las cifras del motor, con las del motorreductor TT de 1:48
struct ConfigMotorCC {
    double v_nominal = 6.0;        // a la que se dan las demás
    double rpm_vacio = 250.0;      // del eje de salida, en vacío, a v_nominal
    double i_vacio_ma = 160.0;     // en vacío, a v_nominal
    double i_bloqueo_ma = 1500.0;  // con el eje sujeto, a v_nominal
    double i_arranque_ma = 250.0;  // la que hace falta para que arranque parado
    double tau_ms = 50.0;          // constante de tiempo mecánica, en vacío
    double reduccion = 48.0;       // solo para decir las rpm del motor
    double v_max = 7.2;            // por encima, se avisa
    unsigned ppr = 0;              // ventanas del disco del encoder; 0, sin él
    double enc_i_ma = 20.0;        // lo que gasta el sensor a 3,3 V
    double posicion_inicial = 0.0; // grados
};

SC_MODULE(MotorCC), public ExtPartBase {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr double REFRESCO = 1e-3;       // s, mientras acelera o frena
    static constexpr double ENC_V_MIN = 2.5;       // V, para que el sensor funcione
    static constexpr double ENC_R_ALTO = 10000.0, ENC_R_BAJO = 100.0;

    MotorCC(sc_core::sc_module_name nm, analog_net_if& a, analog_net_if& b,
            const ConfigMotorCC& c = ConfigMotorCC(), analog_net_if* enc_vcc = nullptr,
            analog_net_if* enc_gnd = nullptr, analog_net_if* enc_a = nullptr,
            analog_net_if* enc_b = nullptr)
        : sc_core::sc_module(nm), ExtPartBase("MotorCC", nm), a_(&a), b_(&b), c_(c) {
        const int ia = add_pin("a", a, "motor");
        const int ib = add_pin("b", b, "motor");
        rama_.reset(new RamaFem(a, ia, b, ib));
        if (c_.ppr && enc_vcc && enc_gnd && enc_a && enc_b) {
            ev_ = enc_vcc; eg_ = enc_gnd; ea_ = enc_a; eb_ = enc_b;
            const int iv = add_pin("enc_vcc", *ev_, "encoder_vcc");
            const int ig = add_pin("enc_gnd", *eg_, "encoder_gnd");
            id_ea_ = add_pin("enc_a", *ea_, "encoder_a");
            id_eb_ = add_pin("enc_b", *eb_, "encoder_b");
            sensor_.reset(new RamaDosNodos(*ev_, iv, *eg_, ig));
        }
        // Las constantes, de la hoja de datos
        r_ = c_.v_nominal / (c_.i_bloqueo_ma * 1e-3);
        const double w_vacio = c_.rpm_vacio * 2.0 * PI / 60.0;
        ke_ = (c_.v_nominal - c_.i_vacio_ma * 1e-3 * r_) / w_vacio;
        kt_ = ke_;
        tf_ = kt_ * c_.i_vacio_ma * 1e-3;
        ts_ = kt_ * std::max(c_.i_arranque_ma, c_.i_vacio_ma) * 1e-3;
        t_bloqueo_ = kt_ * c_.i_bloqueo_ma * 1e-3;
        j_ = c_.tau_ms * 1e-3 * ke_ * kt_ / r_;
        th_ = c_.posicion_inicial * PI / 180.0;
        if (c_.ppr) dq_ = 2.0 * PI / (4.0 * c_.ppr);
        q_ = cuarto_de(th_, 0);
        SC_HAS_PROCESS(MotorCC);
        SC_THREAD(motor);
    }
    ~MotorCC() override {
        rama_->suelta();
        if (sensor_) sensor_->suelta();
        if (ea_) { ea_->set_hiz(id_ea_); eb_->set_hiz(id_eb_); }
    }

    // --- Desde C++ -------------------------------------------------------------
    double rpm() const { return w_ * 60.0 / (2.0 * PI); }             // del eje de salida
    double rpm_motor() const { return rpm() * c_.reduccion; }
    double angulo() const {                                           // 0..360
        double g = std::fmod(th_ * 180.0 / PI, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    double vueltas() const { return th_ / (2.0 * PI); }
    // La corriente y la tension de AHORA, de a a b: con PWM, a saltos
    double corriente_ma() const { return i_ * 1e3; }
    double tension() const { return ke_ * w_ + r_ * i_; }
    // Y su MEDIA de los ultimos milisegundos (un filtro de primer orden de
    // MEDIA_S): lo que marcaria un polimetro. Es lo que se deja ver
    double corriente_media_ma() const { return media(i_med_, i_) * 1e3; }
    double tension_media() const { return media(v_med_, ke_ * w_ + r_ * i_); }
    double fem() const { return ke_ * w_; }
    long   cuenta() const { return q_; }            // cuartos de ciclo del encoder
    bool   enc_a() const { return enc_a_; }
    bool   enc_b() const { return enc_b_; }
    bool   tiene_encoder() const { return ea_ != nullptr; }
    bool   bloqueado() const { return bloq_; }
    void   bloquea(bool si) { bloq_ = si; ev_mando_.notify(sc_core::SC_ZERO_TIME); }
    double carga() const { return carga_; }          // 0..1 del par de bloqueo
    void   pon_carga(double f) {
        carga_ = std::max(0.0, std::min(1.0, f));
        ev_mando_.notify(sc_core::SC_ZERO_TIME);
    }
    double r_bobinado() const { return r_; }
    double ke() const { return ke_; }
    const ConfigMotorCC& config() const { return c_; }

    // --- Lo que deja ver y tocar -------------------------------------------------
    unsigned n_observables() const override { return ea_ ? 7 : 4; }
    Observable observable(unsigned i) const override {
        const float rmax = float(c_.rpm_vacio * 1.5);
        switch (i) {
            case 0: return {"angulo", "°", 0.f, 360.f, true};
            case 1: return {"rpm", "rpm", -rmax, rmax, false};
            case 2: return {"corriente", "mA", -float(c_.i_bloqueo_ma), float(c_.i_bloqueo_ma), false};
            case 3: return {"tension", "V", -float(c_.v_max), float(c_.v_max), false};
            case 4: return {"cuenta", "", -1e6f, 1e6f, false};
            case 5: return {"enc_a", "", 0.f, 1.f, false};
            default: return {"enc_b", "", 0.f, 1.f, false};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(angulo());
            case 1: return float(rpm());
            case 2: return float(corriente_media_ma());
            case 3: return float(tension_media());
            case 4: return float(q_);
            case 5: return enc_a_ ? 1.f : 0.f;
            default: return enc_b_ ? 1.f : 0.f;
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

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    static int signo(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
    static constexpr double MEDIA_S = 5e-3;
    // La media de x, que vale `ahora` desde la ultima vez que se apunto
    double media(double med, double ahora_x) const {
        const double dt = ahora() - t_med_;
        return ahora_x + (med - ahora_x) * std::exp(-dt / MEDIA_S);
    }
    // Apunta en las medias lo que ha valido cada cosa hasta ahora
    void apunta_medias() {
        i_med_ = media(i_med_, i_);
        v_med_ = media(v_med_, ke_ * w_ + r_ * i_);
        t_med_ = ahora();
    }

    // El cuarto de ciclo del encoder en el que está el ángulo th. En una
    // frontera exacta, el de delante según el sentido: el flanco se calcula
    // para caer ahí
    long cuarto_de(double th, int s) const {
        if (!c_.ppr) return 0;
        return long(std::floor(th / dq_ + s * 1e-7));
    }

    // --- La mecánica: el régimen de ahora ------------------------------------
    // Con el par motor de la tensión que se ve -si conduce- y el sentido s:
    // dw/dt = A - B·w
    struct Regimen { double A, B; };
    double par_motor() const { return conduce_ ? kt_ * vth_ / rt_ : 0.0; }
    double t_carga() const { return carga_ * t_bloqueo_; }
    Regimen regimen(int s) const {
        Regimen g;
        g.A = (par_motor() - (tf_ + t_carga()) * s) / j_;
        g.B = conduce_ ? kt_ * ke_ / (rt_ * j_) : 0.0;
        return g;
    }
    // La velocidad y el ángulo tras t segundos, sin cambiar de sentido
    static void evoluciona(const Regimen& g, double w0, double t, double& w, double& dth) {
        if (g.B > 0) {
            const double winf = g.A / g.B, e = std::exp(-g.B * t);
            w = winf + (w0 - winf) * e;
            dth = winf * t + (w0 - winf) * (1.0 - e) / g.B;
        } else {
            w = w0 + g.A * t;
            dth = w0 * t + 0.5 * g.A * t * t;
        }
    }
    // Cuánto falta para que se pare yendo en el sentido s (infinito si no)
    static double hasta_parar(const Regimen& g, double w0, int s) {
        if (g.B > 0) {
            const double winf = g.A / g.B;
            if (winf * s > 0) return std::numeric_limits<double>::infinity();
            if (winf == 0.0) return std::numeric_limits<double>::infinity();
            return std::log((w0 - winf) / (0.0 - winf)) / g.B;
        }
        if (g.A * s >= 0) return std::numeric_limits<double>::infinity();
        return -w0 / g.A;
    }
    // Si, parado, arranca: hacia dónde (0, no)
    int arranca() const {
        if (bloq_) return 0;
        const double tm = par_motor();
        return std::fabs(tm) > ts_ + t_carga() ? signo(tm) : 0;
    }

    // Lleva el eje del instante t0_ al t1 con el régimen que había
    void avanza(double t1) {
        double dt = t1 - t0_;
        t0_ = t1;
        for (int vuelta = 0; dt > 0 && vuelta < 8; ++vuelta) {
            if (bloq_) { w_ = 0.0; return; }
            int s = signo(w_);
            if (s == 0) {
                s = arranca();
                if (s == 0) return;              // parado, y se queda
            }
            const Regimen g = regimen(s);
            const double tz = hasta_parar(g, w_, s);
            if (tz >= dt) {
                double w, d;
                evoluciona(g, w_, dt, w, d);
                w_ = (signo(w) == -s) ? 0.0 : w;
                th_ += d;
                return;
            }
            double w, d;
            evoluciona(g, w_, tz, w, d);
            th_ += d;
            w_ = 0.0;
            dt -= tz;
        }
    }

    // Lo que tarda en recorrer d radianes en el sentido s, sin pararse
    // (infinito si se para antes). Newton sobre la solución exacta
    double tiempo_para(double d, int s) const {
        const Regimen g = regimen(s);
        const double tz = hasta_parar(g, w_, s);
        auto recorre = [&](double t, double& w) {
            double dth;
            evoluciona(g, w_, t, w, dth);
            return dth * s;
        };
        double w = 0;
        if (std::isfinite(tz) && recorre(tz, w) < d) return std::numeric_limits<double>::infinity();
        double vmax = std::fabs(w_);
        if (g.B > 0 && g.A / g.B * s > 0) vmax = std::max(vmax, std::fabs(g.A / g.B));
        if (vmax <= 0) vmax = std::fabs(g.A) * 1e-3 + 1e-12;
        double t = d / vmax;
        for (int k = 0; k < 40; ++k) {
            const double f = recorre(t, w) - d;
            if (std::fabs(f) < 1e-12) break;
            const double v = std::fabs(w);
            if (v < 1e-12) { t *= 2; continue; }
            double tn = t - f / v;
            if (tn <= 0) tn = t / 2;
            if (std::isfinite(tz)) tn = std::min(tn, tz);
            t = tn;
        }
        return t;
    }

    // --- El encoder ----------------------------------------------------------
    void pon_encoder() {
        if (!ea_) return;
        const bool vivo = conectada_ &&
                          double(ev_->voltage()) - double(eg_->voltage()) >= ENC_V_MIN;
        sensor_->resuelve(conectada_, 3.3 / (c_.enc_i_ma * 1e-3));
        if (!vivo) {
            ea_->set_hiz(id_ea_);
            eb_->set_hiz(id_eb_);
            enc_a_ = enc_b_ = false;
            return;
        }
        // 00, 10, 11, 01: A va delante girando a la derecha
        const long m = ((q_ % 4) + 4) % 4;
        enc_a_ = (m == 1 || m == 2);
        enc_b_ = (m == 2 || m == 3);
        const float vh = ev_->voltage(), vl = eg_->voltage();
        ea_->set_drive(id_ea_, enc_a_ ? vh : vl, float(enc_a_ ? ENC_R_ALTO : ENC_R_BAJO));
        eb_->set_drive(id_eb_, enc_b_ ? vh : vl, float(enc_b_ ? ENC_R_ALTO : ENC_R_BAJO));
    }

    // --- El proceso ----------------------------------------------------------
    void motor() {
        for (;;) {
            const double t = ahora();
            apunta_medias();
            avanza(t);
            // Lo que ven los bornes, sin el motor: el régimen desde aquí
            double vth = 0, rth = 0;
            const bool cond = conectada_ && rama_->fuera(vth, rth);
            conduce_ = cond;
            vth_ = vth;
            rt_ = r_ + rth;
            // La rama con la f.e.m. de ahora, y la corriente
            rama_->resuelve(conectada_, ke_ * w_, r_);
            i_ = cond ? (vth - ke_ * w_) / rt_ : 0.0;
            avisa_tension();
            // El encoder: el cuarto de ahora
            const int s = signo(w_);
            if (c_.ppr) q_ = cuarto_de(th_, s);
            pon_encoder();
            // El próximo despertar: el siguiente flanco, que se pare, o el
            // refresco mientras cambia la velocidad
            double plazo = std::numeric_limits<double>::infinity();
            if (s != 0 && !bloq_) {
                const Regimen g = regimen(s);
                const double tz = hasta_parar(g, w_, s);
                plazo = std::min(plazo, tz);
                const bool estable = g.B > 0 && std::fabs(w_ - g.A / g.B) < 1e-6 * std::fabs(w_);
                if (!estable) plazo = std::min(plazo, REFRESCO);
                if (c_.ppr) {
                    const double frontera = (s > 0 ? q_ + 1 : q_) * dq_;
                    const double d = std::fabs(frontera - th_);
                    plazo = std::min(plazo, tiempo_para(std::max(d, 1e-15), s));
                }
            } else if (s == 0 && !bloq_ && arranca() != 0) {
                plazo = 0.0;                     // arranca ahora mismo
            }
            // Nunca menos de un nanosegundo: un plazo que redondea a cero
            // sería un bucle de ciclos delta
            if (!std::isfinite(plazo)) wait(despierta());
            else wait(sc_core::sc_time(std::max(plazo, 1e-9), sc_core::SC_SEC), despierta());
        }
    }
    // Lo que lo despierta: los bornes, los mandos, la conexión y, con
    // encoder, su alimentación
    const sc_core::sc_event_or_list& despierta() {
        if (!despierta_) {
            despierta_.reset(new sc_core::sc_event_or_list());
            *despierta_ |= a_->value_changed_event();
            *despierta_ |= b_->value_changed_event();
            *despierta_ |= ev_mando_;
            *despierta_ |= evento_conexion();
            if (ev_) {
                *despierta_ |= ev_->value_changed_event();
                *despierta_ |= eg_->value_changed_event();
            }
        }
        return *despierta_;
    }

    void avisa_tension() {
        const double v = std::fabs(tension());
        if (v > c_.v_max + 0.05 && !dicho_sobretension_) {
            dicho_sobretension_ = true;
            char b[160];
            std::snprintf(b, sizeof b, "%s: %.2f V en sus bornes, y el motor aguanta %.1f V",
                          pieza().c_str(), v, c_.v_max);
            SC_REPORT_WARNING("motor", b);
        }
    }

    analog_net_if *a_, *b_;
    ConfigMotorCC c_;
    std::unique_ptr<RamaFem> rama_;
    // El encoder
    analog_net_if *ev_ = nullptr, *eg_ = nullptr, *ea_ = nullptr, *eb_ = nullptr;
    int id_ea_ = -1, id_eb_ = -1;
    std::unique_ptr<RamaDosNodos> sensor_;
    double dq_ = 1.0;
    long q_ = 0;
    bool enc_a_ = false, enc_b_ = false;
    std::unique_ptr<sc_core::sc_event_or_list> despierta_;
    // Las constantes
    double r_ = 4.0, ke_ = 0.2, kt_ = 0.2, tf_ = 0.0, ts_ = 0.0, t_bloqueo_ = 0.0, j_ = 1e-3;
    // El estado: el eje en t0_
    double t0_ = 0.0, w_ = 0.0, th_ = 0.0, i_ = 0.0;
    // Las medias de la corriente y la tension, hasta t_med_
    double i_med_ = 0.0, v_med_ = 0.0, t_med_ = 0.0;
    // El régimen
    bool conduce_ = false;
    double vth_ = 0.0, rt_ = 4.0;
    // Los mandos
    bool bloq_ = false;
    double carga_ = 0.0;
    sc_core::sc_event ev_mando_;
    bool dicho_sobretension_ = false;
};

} // namespace stm32

#endif // STM32_PARTS_MOTOR_CC_H
