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
// ESA ECUACIÓN ES DE SU CADENA (parts/cadena.h). El motor es un ACTUADOR: da
// el par Kt·i y pone su J, su Tf, su Ts y su Tc; la cadena les suma lo que
// tenga detrás -un volante, una reductora, un husillo- y lo integra todo
// junto, con la corriente. Un motor suelto tiene su cadena IMPLÍCITA, de él
// solo, que mueve su propio proceso: es la de siempre, y da lo mismo al
// decimal. Si una <transmision> lo engancha, la mueve el proceso de ella y el
// suyo se queda parado.
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
#include "cadena.h"

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
    double eje_mm = 0.0;           // el diámetro del eje de salida; 0, no se sabe
};

SC_MODULE(MotorCC), public ExtPartBase, public Actuador {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr double PASO = Cadena::PASO;   // s: el paso máximo de integración
    static constexpr double ENC_V_MIN = 2.5;       // V, para que el sensor funcione
    static constexpr double ENC_R_ALTO = 10000.0, ENC_R_BAJO = 100.0;

    MotorCC(sc_core::sc_module_name nm, analog_net_if& a, analog_net_if& b,
            const ConfigMotorCC& c = ConfigMotorCC(), analog_net_if* enc_vcc = nullptr,
            analog_net_if* enc_gnd = nullptr, analog_net_if* enc_a = nullptr,
            analog_net_if* enc_b = nullptr)
        : sc_core::sc_module(nm), ExtPartBase("MotorCC", nm), a_(&a), b_(&b), c_(c),
          propia_(c.posicion_inicial * PI / 180.0) {
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
        rt_ = r_;
        if (c_.ppr) dq_ = 2.0 * PI / (4.0 * c_.ppr);
        q_ = cuarto_de(c_.posicion_inicial * PI / 180.0, 0);
        // Suelto, con su cadena: él solo
        propia_.pon_actuador(this);
        propia_.cierra();
        SC_HAS_PROCESS(MotorCC);
        SC_THREAD(motor);
    }
    ~MotorCC() override {
        rama_->suelta();
        if (sensor_) sensor_->suelta();
        if (ea_) { ea_->set_hiz(id_ea_); eb_->set_hiz(id_eb_); }
    }

    // --- Desde C++ -------------------------------------------------------------
    // Su eje es el de referencia de su cadena
    double rpm() const { return cadena_->w() * 60.0 / (2.0 * PI); }   // del eje de salida
    double rpm_motor() const { return rpm() * c_.reduccion; }
    double angulo() const {                                           // 0..360
        double g = std::fmod(cadena_->th() * 180.0 / PI, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    double vueltas() const { return cadena_->th() / (2.0 * PI); }
    // La corriente y la tension de AHORA, de a a b: con PWM, a saltos -la
    // corriente, menos: la alisa la bobina-
    double corriente_ma() const { return i_ * 1e3; }
    double tension() const { return v_bornes(); }
    // Y su MEDIA de los ultimos milisegundos (un filtro de primer orden de
    // MEDIA_S): lo que marcaria un polimetro. Es lo que se deja ver
    double corriente_media_ma() const { return media(i_med_, i_) * 1e3; }
    double tension_media() const { return media(v_med_, v_bornes()); }
    double fem() const { return ke_ * cadena_->w(); }
    long   cuenta() const { return q_; }            // cuartos de ciclo del encoder
    bool   enc_a() const { return enc_a_; }
    bool   enc_b() const { return enc_b_; }
    bool   tiene_encoder() const { return ea_ != nullptr; }
    bool   bloqueado() const override { return bloq_; }
    void   bloquea(bool si) { bloq_ = si; cadena_->toca(); }
    double carga() const { return carga_; }          // 0..1 del par de bloqueo
    void   pon_carga(double f) {
        carga_ = std::max(0.0, std::min(1.0, f));
        cadena_->toca();
    }
    double r_bobinado() const { return r_; }
    double inductancia() const { return l_; }
    double ke() const { return ke_; }
    double kt() const { return kt_; }
    double j_propia() const { return j_; }          // kg·m², la del motor y su reductora
    const ConfigMotorCC& config() const { return c_; }
    // ¿Va en una <transmision>? Si no, en la suya, él solo
    bool en_transmision() const { return cadena_ != &propia_; }

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

    // =========================================================================
    // EL MOTOR COMO ACTUADOR DE SU CADENA: la corriente de la bobina
    //
    //     L·di/dt = Vth - Ke·w - (R + Rth)·i       (con la rama cerrada)
    //
    // y el par Kt·i. Con la rama abierta, i = 0. Lo mecánico -la inercia, el
    // rozamiento seco, el de arranque y la carga del mando- lo pone en la
    // cadena, que lo integra con lo que tenga detrás, a pasos de 5 us como
    // mucho -y nunca más de una quinta parte de L/(R+Rth)-.
    // =========================================================================
    unsigned n_e() const override { return 1; }
    void lee_e(double* e) const override { e[0] = i_; }
    void pon_e(const double* e) override { i_ = e[0]; }
    void deriva(const double* e, double w, double, double* de, double& par) const override {
        de[0] = conduce_ ? (vth_ - ke_ * w - rt_ * e[0]) / l_ : 0.0;
        par = kt_ * e[0];
    }
    void prepara(double* e, double, double) const override { if (!conduce_) e[0] = 0.0; }
    double paso_max() const override {
        return conduce_ ? std::min(PASO, 0.2 * l_ / rt_) : Actuador::INF;
    }
    bool inerte() const override { return !conduce_; }
    bool asentado(const double* e) const override {
        if (!conduce_) return e[0] == 0.0;
        return std::fabs(e[0] - vth_ / rt_) < 1e-7;
    }
    double par_asentado(double) const override { return conduce_ ? kt_ * (vth_ / rt_) : 0.0; }
    void limpia(double* e) const override { if (!conduce_) e[0] = 0.0; }
    void asienta(double* e) const override { if (conduce_) e[0] = vth_ / rt_; }
    bool asentando(const double*, const double* de) const override {
        return conduce_ && std::fabs(de[0]) * Cadena::REFRESCO_I > 1e-3;
    }
    bool estable(const double*, const double* de) const override {
        return conduce_ && std::fabs(de[0]) < 1e-6;
    }
    // El próximo flanco del encoder, con la velocidad de ahora
    double hasta_marca(double th, double w) const override {
        if (!c_.ppr) return Actuador::INF;
        const int s = signo(w);
        const double frontera = (s > 0 ? q_ + 1 : q_) * dq_;
        return std::max(1e-9, std::fabs(frontera - th) / std::fabs(w));
    }
    // Un flanco del encoder, o que la corriente cambie de sentido -la ven los
    // diodos de un puente-
    bool cambia(const double* e0, const double* e, double th, double w) const override {
        if (c_.ppr && cuarto_de(th, signo(w)) != q_) return true;
        const int i0 = signo(e0[0]);
        return conduce_ && i0 != 0 && signo(e[0]) != i0;
    }
    double inercia() const override { return j_; }
    double rozamiento() const override { return tf_ + t_carga(); }
    double arranque() const override { return ts_ + t_carga(); }
    double posicion_inicial() const override { return c_.posicion_inicial * PI / 180.0; }
    double diametro_eje() const override { return c_.eje_mm; }
    std::string nombre_actuador() const override { return pieza(); }

    void antes() override { apunta_medias(); }
    void regimen() override {
        // Lo que ven los bornes, sin el motor: el régimen desde aquí
        double vth = 0, rth = 0;
        const bool cond = conectada_ && rama_->fuera(vth, rth);
        if (!cond) i_ = 0.0;            // la rama abierta corta la corriente
        conduce_ = cond;
        vth_ = vth;
        rt_ = r_ + rth;
        // La rama: con ella cerrada, la que hace pasar la corriente de la
        // bobina -una f.e.m. que deja en los bornes Vth - Rth·i-; abierta,
        // la f.e.m. del giro
        if (cond) rama_->resuelve(true, vth - i_ * rt_, r_);
        else      rama_->resuelve(conectada_, ke_ * cadena_->w(), r_);
        avisa_tension();
        if (c_.ppr) q_ = cuarto_de(cadena_->th(), signo(cadena_->w()));
        pon_encoder();
    }
    // Lo que lo despierta: los bornes, la conexión y, con encoder, su
    // alimentación. Los mandos, por la cadena
    void eventos(sc_core::sc_event_or_list& l) override {
        l |= a_->value_changed_event();
        l |= b_->value_changed_event();
        l |= evento_conexion();
        if (ev_) {
            l |= ev_->value_changed_event();
            l |= eg_->value_changed_event();
        }
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
        i_med_ = media(i_med_, i_);
        v_med_ = media(v_med_, v_bornes());
        t_med_ = ahora();
    }
    // La tension entre los bornes: con la rama cerrada, la de Thevenin menos lo
    // que cae fuera; abierta, la f.e.m.
    double v_bornes() const {
        return conduce_ ? vth_ - (rt_ - r_) * i_ : ke_ * cadena_->w();
    }
    double t_carga() const { return carga_ * t_bloqueo_; }

    // El cuarto de ciclo del encoder en el que está el ángulo th. En una
    // frontera exacta, el de delante según el sentido
    long cuarto_de(double th, int s) const {
        if (!c_.ppr) return 0;
        return long(std::floor(th / dq_ + s * 1e-7));
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

    // --- El proceso: el de su cadena implícita, si va suelto -------------------
    void motor() {
        if (en_transmision()) return;      // la mueve el proceso de la transmisión
        propia_.corre();
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
    // Las constantes
    double r_ = 4.0, l_ = 1.5e-3, ke_ = 0.2, kt_ = 0.2, tf_ = 0.0, ts_ = 0.0, t_bloqueo_ = 0.0,
           j_ = 1e-3;
    // Su cadena implícita: él solo, con su carga
    Cadena propia_;
    // La corriente de la bobina, de a a b
    double i_ = 0.0;
    // Las medias de la corriente y la tension, hasta t_med_
    double i_med_ = 0.0, v_med_ = 0.0, t_med_ = 0.0;
    // El régimen: lo que ven los bornes
    bool conduce_ = false;
    double vth_ = 0.0, rt_ = 4.0;
    // Los mandos
    bool bloq_ = false;
    double carga_ = 0.0;
    bool dicho_sobretension_ = false;
};

} // namespace stm32

#endif // STM32_PARTS_MOTOR_CC_H
