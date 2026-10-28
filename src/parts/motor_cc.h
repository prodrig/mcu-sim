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
// resistencia R y la INDUCTANCIA L del bobinado, entre a y b:
//
//     Va - Vb = E + R·i + L·di/dt
//
// La bobina hace que la corriente no siga a la tensión al momento: con PWM
// sube y baja dentro de cada periodo con la constante L/(R + lo de fuera), y
// cuando el puente suelta los bornes sigue por donde pueda -los diodos de un
// módulo L298N (L298, `diodos="si"`)-; si no hay por dónde, se corta. Para la
// red, el motor es una rama que deja pasar esa corriente: una f.e.m. tal que
// en los bornes quede Vth - Rth·i (RamaFem).
//
// LO MECÁNICO, en el eje de salida, con la reductora dentro:
//
//     J·dw/dt = Kt·i - Tf·sgn(w) - Tc·sgn(w)
//
// con Kt = Ke, un rozamiento seco Tf -el que se lleva la corriente en vacío-,
// uno de arranque Ts que hay que vencer para que se mueva parado -el de
// `i_arranque_ma`- y la carga Tc del mando `carga`, que se opone al giro. J
// sale de la constante de tiempo `tau_ms`.
//
// Las dos ecuaciones se integran juntas, con Runge-Kutta de cuarto orden a
// pasos de 5 us como mucho, pero SystemC no despierta en cada paso: el
// proceso despierta en cada cambio de los bornes, en cada flanco del encoder
// y cuando la corriente cambia de sentido -ensayados de antemano con los
// mismos pasos, así que caen en su instante-, cuando se para, y cada 0,1 ms
// mientras la corriente se asienta o cada milisegundo mientras acelera, para
// que lo que ven los demás siga al motor.
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
    double l_mh = 1.5;             // la inductancia del bobinado
    double reduccion = 48.0;       // solo para decir las rpm del motor
    double v_max = 7.2;            // por encima, se avisa
    unsigned ppr = 0;              // ventanas del disco del encoder; 0, sin él
    double enc_i_ma = 20.0;        // lo que gasta el sensor a 3,3 V
    double posicion_inicial = 0.0; // grados
};

SC_MODULE(MotorCC), public ExtPartBase {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr double PASO = 5e-6;           // s: el paso máximo de integración
    static constexpr double REFRESCO = 1e-3;       // s, mientras acelera o frena
    static constexpr double REFRESCO_I = 1e-4;     // s, mientras la corriente se asienta
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
        l_ = c_.l_mh * 1e-3;
        const double w_vacio = c_.rpm_vacio * 2.0 * PI / 60.0;
        ke_ = (c_.v_nominal - c_.i_vacio_ma * 1e-3 * r_) / w_vacio;
        kt_ = ke_;
        tf_ = kt_ * c_.i_vacio_ma * 1e-3;
        ts_ = kt_ * std::max(c_.i_arranque_ma, c_.i_vacio_ma) * 1e-3;
        t_bloqueo_ = kt_ * c_.i_bloqueo_ma * 1e-3;
        j_ = c_.tau_ms * 1e-3 * ke_ * kt_ / r_;
        x_.th = c_.posicion_inicial * PI / 180.0;
        rt_ = r_;
        if (c_.ppr) dq_ = 2.0 * PI / (4.0 * c_.ppr);
        q_ = cuarto_de(x_.th, 0);
        SC_HAS_PROCESS(MotorCC);
        SC_THREAD(motor);
    }
    ~MotorCC() override {
        rama_->suelta();
        if (sensor_) sensor_->suelta();
        if (ea_) { ea_->set_hiz(id_ea_); eb_->set_hiz(id_eb_); }
    }

    // --- Desde C++ -------------------------------------------------------------
    double rpm() const { return x_.w * 60.0 / (2.0 * PI); }           // del eje de salida
    double rpm_motor() const { return rpm() * c_.reduccion; }
    double angulo() const {                                           // 0..360
        double g = std::fmod(x_.th * 180.0 / PI, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    double vueltas() const { return x_.th / (2.0 * PI); }
    // La corriente y la tension de AHORA, de a a b: con PWM, a saltos -la
    // corriente, menos: la alisa la bobina-
    double corriente_ma() const { return x_.i * 1e3; }
    double tension() const { return v_bornes(); }
    // Y su MEDIA de los ultimos milisegundos (un filtro de primer orden de
    // MEDIA_S): lo que marcaria un polimetro. Es lo que se deja ver
    double corriente_media_ma() const { return media(i_med_, x_.i) * 1e3; }
    double tension_media() const { return media(v_med_, v_bornes()); }
    double fem() const { return ke_ * x_.w; }
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
    double inductancia() const { return l_; }
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
    // La media de x, que vale `ahora_x` desde la ultima vez que se apunto
    double media(double med, double ahora_x) const {
        const double dt = ahora() - t_med_;
        return ahora_x + (med - ahora_x) * std::exp(-dt / MEDIA_S);
    }
    void apunta_medias() {
        i_med_ = media(i_med_, x_.i);
        v_med_ = media(v_med_, v_bornes());
        t_med_ = ahora();
    }
    // La tension entre los bornes: con la rama cerrada, la de Thevenin menos lo
    // que cae fuera; abierta, la f.e.m.
    double v_bornes() const {
        return conduce_ ? vth_ - (rt_ - r_) * x_.i : ke_ * x_.w;
    }

    // El cuarto de ciclo del encoder en el que está el ángulo th. En una
    // frontera exacta, el de delante según el sentido
    long cuarto_de(double th, int s) const {
        if (!c_.ppr) return 0;
        return long(std::floor(th / dq_ + s * 1e-7));
    }

    // =========================================================================
    // EL MOTOR: la corriente por la bobina, la velocidad y el ángulo del eje
    //
    //     L·di/dt = Vth - Ke·w - (R + Rth)·i       (con la rama cerrada)
    //     J·dw/dt = Kt·i - (Tf + Tc)·sgn(w)
    //
    // Con la rama abierta, i = 0. Parado, el eje se queda mientras el par no
    // pase del de arranque; y al pararse, el rozamiento no lo empuja hacia
    // atrás. Se integra con Runge-Kutta de cuarto orden a pasos de 5 us como
    // mucho -y nunca más de una quinta parte de L/(R+Rth)-, que es pura
    // cuenta: SystemC no despierta en cada paso.
    // =========================================================================
    struct Estado { double i = 0.0, w = 0.0, th = 0.0; };
    double t_carga() const { return carga_ * t_bloqueo_; }
    double paso_max() const {
        return conduce_ ? std::min(PASO, 0.2 * l_ / rt_) : 1e-4;
    }
    // Las derivadas, con el sentido s del rozamiento fijo (0: parado)
    void deriva(double i, double w, int s, double& di, double& dw) const {
        di = conduce_ ? (vth_ - ke_ * w - rt_ * i) / l_ : 0.0;
        dw = (s == 0) ? 0.0 : (kt_ * i - (tf_ + t_carga()) * s) / j_;
    }
    // Un paso de h segundos
    Estado paso(Estado x, double h) const {
        if (!conduce_) x.i = 0.0;
        int s = signo(x.w);
        if (bloq_) { x.w = 0.0; s = 0; }
        else if (s == 0 && std::fabs(kt_ * x.i) > ts_ + t_carga()) s = signo(x.i);
        if (!conduce_) {
            // Sin corriente: el rozamiento lo frena en línea recta, exacto
            if (s == 0) return x;
            const double a = -(tf_ + t_carga()) * s / j_;
            double t = h;
            if (-x.w / a < h) t = -x.w / a;
            x.th += x.w * t + 0.5 * a * t * t;
            x.w = (t < h) ? 0.0 : x.w + a * h;
            if (signo(x.w) == -s) x.w = 0.0;
            return x;
        }
        double k1i, k1w, k2i, k2w, k3i, k3w, k4i, k4w;
        deriva(x.i, x.w, s, k1i, k1w);
        deriva(x.i + 0.5 * h * k1i, x.w + 0.5 * h * k1w, s, k2i, k2w);
        deriva(x.i + 0.5 * h * k2i, x.w + 0.5 * h * k2w, s, k3i, k3w);
        deriva(x.i + h * k3i, x.w + h * k3w, s, k4i, k4w);
        const double w0 = x.w;
        x.i += h / 6.0 * (k1i + 2 * k2i + 2 * k3i + k4i);
        x.w += h / 6.0 * (k1w + 2 * k2w + 2 * k3w + k4w);
        if (s != 0 && signo(x.w) != s) x.w = 0.0;     // se para
        x.th += 0.5 * (w0 + x.w) * h;
        return x;
    }
    // ¿Se queda como está? Parado, sin arrancar, con la corriente asentada
    bool quieto(const Estado& x) const {
        if (x.w != 0.0 && !bloq_) return false;
        if (!conduce_) return x.i == 0.0;
        const double i_ss = vth_ / rt_;
        const bool arranca = !bloq_ && std::fabs(kt_ * i_ss) > ts_ + t_carga();
        return !arranca && std::fabs(x.i - i_ss) < 1e-7;
    }
    // Lleva el estado de t0_ al t1 con el régimen que había
    void avanza(double t1) {
        double dt = t1 - t0_;
        t0_ = t1;
        if (dt <= 0.0) return;
        if (quieto(x_)) { if (!conduce_) x_.i = 0.0; return; }
        const double hm = paso_max();
        const long n = std::max(1L, long(std::ceil(dt / hm - 1e-9)));
        const double h = dt / double(n);
        for (long k = 0; k < n; ++k) {
            x_ = paso(x_, h);
            if (quieto(x_)) {
                if (conduce_) x_.i = vth_ / rt_;
                break;
            }
        }
    }
    // Cuándo pasa algo que hay que hacer ver, sin que nadie toque los bornes:
    // un flanco del encoder, que la corriente cambie de sentido -la ven los
    // diodos de un puente-, que se pare o arranque, o el refresco. Se ensaya
    // con los mismos pasos que dará avanza(), así que el instante es exacto
    double proximo() const {
        if (quieto(x_)) return std::numeric_limits<double>::infinity();
        const double hm = paso_max();
        double di, dw;
        deriva(x_.i, x_.w, signo(x_.w), di, dw);
        const bool asentando = conduce_ && std::fabs(di) * REFRESCO_I > 1e-3;
        const double horizonte = asentando ? REFRESCO_I : REFRESCO;
        // Estable del todo: los flancos, con la velocidad de ahora
        if (conduce_ && std::fabs(di) < 1e-6 && std::fabs(dw) < 1e-6 && x_.w != 0.0) {
            if (!c_.ppr) return std::numeric_limits<double>::infinity();
            const int s = signo(x_.w);
            const double frontera = (s > 0 ? q_ + 1 : q_) * dq_;
            return std::max(1e-9, std::fabs(frontera - x_.th) / std::fabs(x_.w));
        }
        Estado x = x_;
        const long q0 = q_;
        const int s0 = signo(x.w), i0 = signo(x.i);
        double t = 0.0;
        while (t < horizonte - 1e-12) {
            const double h = std::min(hm, horizonte - t);
            x = paso(x, h);
            t += h;
            if (c_.ppr && cuarto_de(x.th, signo(x.w)) != q0) return t;
            if (signo(x.w) != s0) return t;
            if (conduce_ && i0 != 0 && signo(x.i) != i0) return t;
            if (quieto(x)) return t;
        }
        return horizonte;
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
            apunta_medias();
            avanza(ahora());
            // Lo que ven los bornes, sin el motor: el régimen desde aquí
            double vth = 0, rth = 0;
            const bool cond = conectada_ && rama_->fuera(vth, rth);
            if (!cond) x_.i = 0.0;          // la rama abierta corta la corriente
            conduce_ = cond;
            vth_ = vth;
            rt_ = r_ + rth;
            // La rama: con ella cerrada, la que hace pasar la corriente de la
            // bobina -una f.e.m. que deja en los bornes Vth - Rth·i-; abierta,
            // la f.e.m. del giro
            if (cond) rama_->resuelve(true, vth - x_.i * rt_, r_);
            else      rama_->resuelve(conectada_, ke_ * x_.w, r_);
            avisa_tension();
            if (c_.ppr) q_ = cuarto_de(x_.th, signo(x_.w));
            pon_encoder();
            // Nunca menos de un nanosegundo: un plazo que redondea a cero
            // sería un bucle de ciclos delta
            const double plazo = proximo();
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

    // La sobretension, de media: la punta de cada periodo de PWM -la de la
    // bobina contra los diodos, la alimentacion entera- no la cuenta
    void avisa_tension() {
        const double v = std::fabs(tension_media());
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
    double r_ = 4.0, l_ = 1.5e-3, ke_ = 0.2, kt_ = 0.2, tf_ = 0.0, ts_ = 0.0, t_bloqueo_ = 0.0,
           j_ = 1e-3;
    // El estado, en t0_
    double t0_ = 0.0;
    Estado x_;
    // Las medias de la corriente y la tension, hasta t_med_
    double i_med_ = 0.0, v_med_ = 0.0, t_med_ = 0.0;
    // El régimen: lo que ven los bornes
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
