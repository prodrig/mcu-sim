// =============================================================================
// motor_bldc.h — Un motor BLDC trifásico con sus sensores Hall y, si se le dan
//                sus patillas, un encoder incremental, movido por un
//                controlador que lo conmuta por dentro
//
// (Fase F1c de doc/analisis_cnc.md, §5.4.)
//
// Un BLDC es un motor de imanes en el rotor y tres bobinas en el estátor, U, V
// y W, que alguien tiene que CONMUTAR: encender la que toca según dónde esté el
// rotor, que dicen sus tres sensores Hall. Con un CONTROLADOR INTELIGENTE
// (parts/controlador_motor.h) eso pasa dentro de él, con FOC o con seis pasos,
// y desde fuera no se ve. Por eso el motor se modela EN EL EJE Q, como un motor
// de continua equivalente, con las cifras de su hoja de datos -las de entre
// dos fases-:
//
//     V = R·i + L·di/dt + Ke·w          T = Kt·i         (Ke = Kt)
//
// LA CORRIENTE i ES LA DEL EJE Q, la que da par, y es la misma rama de
// CORRIENTE IMPUESTA CON TOPE DE TENSIÓN de los paso a paso (§5.2): el
// controlador dice qué corriente quiere y puede poner hasta su tensión de bus;
// si R·i* + Ke·w cabe, la corriente es la consigna; si no, la tensión se queda
// en el tope y la corriente va por detrás -la velocidad máxima, la caída de par
// arriba-. Sin controlador, la corriente vuelve al bus por los diodos de sus
// transistores hasta acabarse.
//
// LOS TRES HILOS U, V Y W SE CABLEAN Y SE COMPRUEBAN: el controlador declara
// sus tres salidas en los nodos -U con el polo +1, V con el -1 y W con el 0-, y
// el motor mira que cada hilo vaya a la suya. Dos cruzados, o uno al aire, o en
// otra cosa, y no hay corriente, y se avisa: con las fases cruzadas la
// conmutación de los Hall no casa con las bobinas, y un BLDC de verdad da
// sacudidas sin girar.
//
// LOS HALL: tres sensores a 120° eléctricos, con `pares_polos` pares de polos,
// 6·p cambios por vuelta. Con el ángulo eléctrico θe = p·θ, H1 está alto de 0°
// a 180°, H2 de 120° a 300° y H3 de 240° a 60°: los sectores 0 a 5 son 101,
// 100, 110, 010, 011, 001. Sus salidas son de COLECTOR ABIERTO, como las de los
// Nanotec: bajan a masa con 100 ohm y suben con el pull-up de quien las lea
// -el controlador lleva los suyos-. Sin tensión -menos de 3,5 V entre hall_vcc y
// hall_gnd-, se quedan al aire.
//
// EL ENCODER, opcional (`ppr`, líneas por vuelta del motor): A y B en
// cuadratura, también de colector abierto, A delante girando a la derecha.
// Cuesta: a 3500 rpm, 1000 líneas son 233 000 flancos por segundo, y cada uno
// es un suceso.
//
// LO MECÁNICO es de su cadena (parts/cadena.h): la inercia del rotor, un
// rozamiento -estimado: la hoja de datos no da la corriente en vacío- y la
// carga del mando, en % de su par nominal.
//
// POR FASES (`fases="si"`, fase F7): sin controlador que lo conmute por dentro,
// con un PUENTE TRIFÁSICO -un L6230, parts/potencia.h- que lo conmuta desde un
// MCU. Entonces son tres fases de verdad, en ESTRELLA, cada una con la mitad
// de la resistencia y de la inductancia de entre dos fases y su f.e.m.:
//
//     Vk - Vn = Rf·ik + Lf·dik/dt + ek       ek = Kf·w·fk(θe)      Σ ik = 0
//
// con fk la FORMA de la f.e.m. -TRAPEZOIDAL, la de un BLDC: plana 120° en +1 y
// 120° en -1, o SENOIDAL- y Kf su constante por fase: Kt/2 en la trapezoidal
// (dos fases en serie dan Kt·i) y Kt/√3 en la senoidal (la de entre dos fases,
// de pico, es Kt·w). El par es Kf·Σ fk·ik. La de U es plana en +1 de 0° a 120°
// eléctricos -los sectores 0 y 1 de los Hall-, la de V va 120° detrás y la de
// W, 240°: con los Hall de siempre, la tabla de seis pasos es U+V-, U+W-, V+W-,
// V+U-, W+U-, W+V- en los sectores 0 a 5.
//
// CADA FASE VE LO DE FUERA como un Thevenin -el transistor del puente que
// conduce, o el diodo por el que sigue la corriente al soltarse-, y la estrella
// pone el neutro Vn donde las corrientes sumen cero. Una fase que no ve nada
// -o solo resistencias de más de 100 ohm, como el divisor que mide su f.e.m.-
// está ABIERTA: sin corriente, y su borne a Vn + ek, que es lo que lee quien
// mire la f.e.m. Con menos de dos fases cerradas, no hay corriente.
//
// Lo que no hace: el diodo de una fase abierta no recorta la f.e.m. que pasa de
// la alimentación, y las inductancias mutuas van dentro de la de cada fase.
// =============================================================================
#ifndef STM32_PARTS_MOTOR_BLDC_H
#define STM32_PARTS_MOTOR_BLDC_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"
#include "cadena.h"

namespace stm32 {

// Las cifras del Nanotec DB59L024035-A [su ficha]: 24 V, 3500 rpm, 9,4 A y
// 0,47 N·m nominales, 28 A de pico, 5 N·cm/A, 0,22 ohm y 0,29 mH entre fases,
// 119 g·cm², tres pares de polos (18 cambios de Hall por vuelta)
struct ConfigMotorBLDC {
    double v_nominal = 24.0;
    double kt_ncm_a = 5.0;            // N·cm/A, la constante de par (y Ke)
    double r_ohm = 0.22;              // entre dos fases
    double l_mh = 0.29;               // entre dos fases
    double corriente_nominal = 9.4;   // A
    double corriente_pico = 28.0;     // A: por encima, se avisa
    double inercia_gcm2 = 119.0;
    double rozamiento_ncm = 1.5;      // seco (estimado)
    double viscoso_mnms = 0.02;       // mN·m por rad/s (estimado)
    unsigned pares_polos = 3;
    unsigned ppr = 0;                 // líneas del encoder por vuelta; 0, sin él
    double posicion_inicial = 0.0;    // grados
    double eje_mm = 0.0;              // el diámetro del eje; 0, no se sabe
    // F7: tres fases de verdad, con un puente trifásico que lo conmuta
    bool fases = false;
    bool senoidal = false;            // la forma de la f.e.m.: trapezoidal o senoidal
};

SC_MODULE(MotorBLDC), public ExtPartBase, public Actuador {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr double PASO = 10e-6;          // s, como el paso a paso
    static constexpr double TOL = 1e-6;            // A: ya está en la consigna
    static constexpr double V_SENSOR = 3.5;        // V: los Hall y el encoder, vivos
    static constexpr float  R_BAJO = 100.0f;       // las salidas de colector abierto

    MotorBLDC(sc_core::sc_module_name nm, analog_net_if& u, analog_net_if& v, analog_net_if& w,
              const ConfigMotorBLDC& c = ConfigMotorBLDC(), analog_net_if* hall_vcc = nullptr,
              analog_net_if* hall_gnd = nullptr, analog_net_if* h1 = nullptr,
              analog_net_if* h2 = nullptr, analog_net_if* h3 = nullptr,
              analog_net_if* enc_a = nullptr, analog_net_if* enc_b = nullptr)
        : sc_core::sc_module(nm), ExtPartBase("MotorBLDC", nm), c_(c),
          propia_(c.posicion_inicial * PI / 180.0) {
        fase_[0] = &u; fase_[1] = &v; fase_[2] = &w;
        if (c_.fases) {
            // Por fases, el motor conduce en sus bornes: una rama por fase
            idf_[0] = add_pin("u", u, "bldc_fase");
            idf_[1] = add_pin("v", v, "bldc_fase");
            idf_[2] = add_pin("w", w, "bldc_fase");
        } else {
            add_ref("u", u);
            add_ref("v", v);
            add_ref("w", w);
        }
        if (hall_vcc && hall_gnd) {
            sv_ = hall_vcc; sg_ = hall_gnd;
            const int iv = add_pin("hall_vcc", *sv_, "bldc_sensor");
            const int ig = add_pin("hall_gnd", *sg_, "bldc_sensor");
            sensor_.reset(new RamaDosNodos(*sv_, iv, *sg_, ig));
            analog_net_if* hs[3] = {h1, h2, h3};
            const char* nh[3] = {"h1", "h2", "h3"};
            for (int k = 0; k < 3; ++k)
                if (hs[k]) { sal_[k].n = hs[k]; sal_[k].id = add_pin(nh[k], *hs[k], "bldc_hall"); }
            if (c_.ppr && enc_a && enc_b) {
                sal_[3].n = enc_a; sal_[3].id = add_pin("enc_a", *enc_a, "bldc_encoder");
                sal_[4].n = enc_b; sal_[4].id = add_pin("enc_b", *enc_b, "bldc_encoder");
            }
        }
        kt_ = c_.kt_ncm_a * 1e-2;
        r_ = c_.r_ohm;
        l_ = c_.l_mh * 1e-3;
        j_ = c_.inercia_gcm2 * 1e-7;
        tf_ = c_.rozamiento_ncm * 1e-2;
        b_ = c_.viscoso_mnms * 1e-3;
        t_nom_ = kt_ * c_.corriente_nominal;
        rf_ = r_ / 2.0;
        lf_ = l_ / 2.0;
        kf_ = c_.senoidal ? kt_ / std::sqrt(3.0) : kt_ / 2.0;
        dh_ = 2.0 * PI / (6.0 * c_.pares_polos);
        if (c_.ppr) dq_ = 2.0 * PI / (4.0 * c_.ppr);
        const double th0 = c_.posicion_inicial * PI / 180.0;
        sector_ = tramo(th0, dh_, 0);
        q_ = c_.ppr ? tramo(th0, dq_, 0) : 0;
        propia_.pon_actuador(this);
        propia_.cierra();
        SC_HAS_PROCESS(MotorBLDC);
        SC_THREAD(motor);
    }
    ~MotorBLDC() override {
        if (c_.fases) for (int k = 0; k < 3; ++k) fase_[k]->set_hiz(idf_[k]);
        if (sensor_) sensor_->suelta();
        for (Salida& s : sal_) if (s.n) s.n->set_hiz(s.id);
    }

    // --- Desde C++ -------------------------------------------------------------
    double rpm() const { return cadena_->w() * 60.0 / (2.0 * PI); }
    double angulo() const {
        double g = std::fmod(cadena_->th() * 180.0 / PI, 360.0);
        return g < 0 ? g + 360.0 : g;
    }
    double vueltas() const { return cadena_->th() / (2.0 * PI); }
    double corriente() const {                                // A, la del eje q
        return c_.fases ? par() / kt_ : i_;                   //  (por fases, la equivalente)
    }
    double par() const {                                      // N·m, sin el rozamiento
        if (!c_.fases) return kt_ * i_;
        double f[3];
        forma(cadena_->th(), f);
        return kf_ * (f[0] * ip_[0] + f[1] * ip_[1] + f[2] * ip_[2]);
    }
    // Por fases: la corriente de cada una -de su borne hacia el neutro- y el neutro
    bool   por_fases() const { return c_.fases; }
    double corriente_fase(int k) const { return ip_[k]; }
    double tension_neutro() const { return vn_; }
    bool   fase_cerrada(int k) const { return con_[k]; }
    double tension() const { return v_aplicada_; }            // V: la que le pone el controlador
    double fem() const { return kt_ * cadena_->w(); }
    int    sector_hall() const { return int(((sector_ % 6) + 6) % 6); }
    unsigned hall() const { return codigo_hall(sector_hall()); }   // H1 H2 H3, de mayor a menor
    long   cuenta() const { return q_; }                     // cuartos del encoder
    bool   tiene_encoder() const { return sal_[3].n != nullptr; }
    bool   tiene_hall() const { return sal_[0].n != nullptr; }
    double kt() const { return kt_; }
    double par_nominal() const { return t_nom_; }
    bool   bloqueado() const override { return bloq_; }
    void   bloquea(bool si) { bloq_ = si; cadena_->toca(); }
    double carga() const { return carga_; }                   // en veces el par nominal
    void   pon_carga(double f) { carga_ = std::max(0.0, std::min(3.0, f)); cadena_->toca(); }
    const char* modo() const {
        if (c_.fases) {
            const int n = int(con_[0]) + int(con_[1]) + int(con_[2]);
            return n == 3 ? "por fases, las tres" : n == 2 ? "por fases, dos" : "por fases, al aire";
        }
        switch (modo_) {
            case Corriente: return "con controlador";
            case Mal:       return "mal cableado";
            default:        return "suelto";
        }
    }
    const ConfigMotorBLDC& config() const { return c_; }

    // El código de los Hall de un sector: H1 en el bit 2, H2 en el 1, H3 en el 0
    static unsigned codigo_hall(int s) {
        static const unsigned tabla[6] = {5, 4, 6, 2, 3, 1};   // 101 100 110 010 011 001
        return tabla[((s % 6) + 6) % 6];
    }

    // --- Lo que deja ver y tocar -------------------------------------------------
    unsigned n_observables() const override {
        return (tiene_encoder() ? 8u : 7u) + (c_.fases ? 3u : 0u);
    }
    Observable observable(unsigned i) const override {
        const float ip = float(c_.corriente_pico);
        switch (i) {
            case 0: return {"angulo", "°", 0.f, 360.f, true};
            case 1: return {"rpm", "rpm", -6000.f, 6000.f, false};
            case 2: return {"corriente", "A", -ip, ip, false};
            case 3: return {"par", "N·m", -float(kt_ * ip), float(kt_ * ip), false};
            case 4: return {"tension", "V", -60.f, 60.f, false};
            case 5: return {"hall", "", 0.f, 7.f, false};
            case 6: return {"sobrecorriente", "", 0.f, 1.f, true, true};
            default: break;
        }
        const unsigned b = tiene_encoder() ? 8u : 7u;
        if (i == 7 && tiene_encoder()) return {"cuenta", "", -1e9f, 1e9f, false};
        static const char* nf[3] = {"corriente_u", "corriente_v", "corriente_w"};
        return {nf[(i - b) % 3], "A", -ip, ip, false};
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(angulo());
            case 1: return float(rpm());
            case 2: return float(corriente());
            case 3: return float(par());
            case 4: return float(v_aplicada_);
            case 5: return float(hall());
            case 6: return pico() > c_.corriente_pico ? 1.f : 0.f;
            default: break;
        }
        const unsigned b = tiene_encoder() ? 8u : 7u;
        if (i == 7 && tiene_encoder()) return float(q_);
        return float(ip_[(i - b) % 3]);
    }
    unsigned n_mandos() const override { return 2; }
    Mando mando(unsigned i) const override {
        if (i == 0) return {"bloquear", Mando::Interruptor, 0.f, 1.f};
        return {"carga", Mando::Continuo, 0.f, 300.f};
    }
    float valor_mando(unsigned i) const override {
        return i == 0 ? (bloq_ ? 1.f : 0.f) : float(carga_ * 100.0);
    }
    void acciona(unsigned i, float v) override {
        if (i == 0) bloquea(v >= 0.5f);
        else pon_carga(double(v) / 100.0);
    }

    // =========================================================================
    // COMO ACTUADOR: la corriente del eje q, y el par Kt·i
    // =========================================================================
    unsigned n_e() const override { return c_.fases ? 3u : 1u; }
    void lee_e(double* e) const override {
        if (c_.fases) { for (int k = 0; k < 3; ++k) e[k] = ip_[k]; return; }
        e[0] = i_;
    }
    void pon_e(const double* e) override {
        if (c_.fases) { for (int k = 0; k < 3; ++k) ip_[k] = e[k]; return; }
        i_ = e[0];
    }
    bool depende_del_angulo() const override { return c_.fases; }
    void deriva(const double* e, double w, double th, double* de, double& par) const override {
        if (c_.fases) { deriva_fases(e, w, th, de, par); return; }
        de[0] = (reg_ == Sigue || reg_ == Nada) ? 0.0 : (u_ - r_ * e[0] - kt_ * w) / l_;
        par = kt_ * e[0];
    }
    // El régimen del paso, que no cambia hasta el siguiente (como el de una
    // bobina de un paso a paso: parts/motor_pap.h)
    void prepara(double* e, double w, double th) const override {
        if (c_.fases) { (void)th; abre(e); return; }
        if (modo_ != Corriente) { e[0] = 0.0; reg_ = Nada; return; }
        if (!activa_) {
            reg_ = e[0] == 0.0 ? Nada : Diodos;
            u_ = -signo(e[0]) * (vbus_ + 2.0 * vd_);
            return;
        }
        const double hace_falta = r_ * obj_ + kt_ * w;
        if (std::fabs(e[0] - obj_) <= TOL) {
            if (std::fabs(hace_falta) <= vbus_) { reg_ = Sigue; e[0] = obj_; }
            else { reg_ = Tope; u_ = signo(hace_falta) * vbus_; }
        } else {
            reg_ = Tope;
            u_ = e[0] < obj_ ? vbus_ : -vbus_;
        }
    }
    void recorta(const double* e0, double* e) const override {
        if (c_.fases) return;
        if (modo_ != Corriente || reg_ == Sigue || reg_ == Nada) return;
        // Solo si la CRUZA: la que sale de la consigna empujada por el tope -la
        // f.e.m. se come la tensión- se va, y no vuelve
        const double obj = activa_ ? obj_ : 0.0;
        if ((e0[0] - obj) * (e[0] - obj) < 0.0) e[0] = obj;
    }
    double paso_max() const override {
        if (c_.fases) {
            if (cerradas() < 2) return Actuador::INF;
            double r = 0.0;
            for (int k = 0; k < 3; ++k) if (con_[k]) r = std::max(r, rth_[k]);
            return std::min(PASO, 0.2 * lf_ / (rf_ + r));
        }
        return modo_ == Corriente ? std::min(PASO, 0.2 * l_ / r_) : Actuador::INF;
    }
    bool inerte() const override {
        if (c_.fases) return cerradas() < 2 && ip_[0] == 0.0 && ip_[1] == 0.0 && ip_[2] == 0.0;
        return modo_ != Corriente || (!activa_ && i_ == 0.0);
    }
    bool asentado(const double* e) const override {
        if (c_.fases) {
            double q[3];
            quietas(q);
            for (int k = 0; k < 3; ++k) if (std::fabs(e[k] - q[k]) > TOL) return false;
            return true;
        }
        if (modo_ != Corriente) return e[0] == 0.0;
        if (!activa_) return e[0] == 0.0;
        return std::fabs(e[0] - obj_) <= TOL && std::fabs(r_ * obj_) <= vbus_;
    }
    double par_asentado(double th) const override {
        if (c_.fases) {
            // Parado, con la corriente de continua de las fases: el par que sujeta
            double q[3], f[3];
            quietas(q);
            forma(th, f);
            return kf_ * (f[0] * q[0] + f[1] * q[1] + f[2] * q[2]);
        }
        return (modo_ == Corriente && activa_) ? kt_ * obj_ : 0.0;
    }
    void limpia(double* e) const override { asienta(e); }
    void asienta(double* e) const override {
        if (c_.fases) { quietas(e); return; }
        e[0] = (modo_ == Corriente && activa_) ? obj_ : 0.0;
    }
    bool asentando(const double*, const double* de) const override {
        if (c_.fases)
            return std::max(std::fabs(de[0]), std::max(std::fabs(de[1]), std::fabs(de[2]))) *
                   Cadena::REFRESCO_I > 1e-3;
        return std::fabs(de[0]) * Cadena::REFRESCO_I > 1e-3;
    }
    bool estable(const double*, const double* de) const override {
        if (c_.fases)
            return std::fabs(de[0]) < 1e-6 && std::fabs(de[1]) < 1e-6 && std::fabs(de[2]) < 1e-6;
        return std::fabs(de[0]) < 1e-6;
    }
    // El próximo cambio de los Hall o del encoder, con la velocidad de ahora
    double hasta_marca(double th, double w) const override {
        if (!tiene_hall() || w == 0.0) return Actuador::INF;
        const int s = signo(w);
        double t = std::fabs(((s > 0 ? sector_ + 1 : sector_) * dh_ - th) / w);
        if (tiene_encoder()) t = std::min(t, std::fabs(((s > 0 ? q_ + 1 : q_) * dq_ - th) / w));
        return std::max(1e-9, t);
    }
    bool cambia(const double* e0, const double* e, double th, double w) const override {
        // Por fases: que la corriente de una fase cambie de sentido -la ven los
        // diodos del puente: una que vuelve por uno se acaba ahí-
        if (c_.fases)
            for (int k = 0; k < 3; ++k)
                if (con_[k] && e0[k] != 0.0 && (e0[k] > 0.0) != (e[k] > 0.0)) return true;
        if (!tiene_hall()) return false;
        if (tramo(th, dh_, signo(w)) != sector_) return true;
        return tiene_encoder() && tramo(th, dq_, signo(w)) != q_;
    }
    // Lo que entra desde el controlador, paso a paso, por el balance de la bobina
    void hecho(const double* e0, const double* e, double w, double, double h) override {
        if (c_.fases || modo_ != Corriente || !fc_) return;
        const double i0 = e0[0], i1 = e[0];
        const double i2 = (i0 * i0 + i0 * i1 + i1 * i1) / 3.0;
        fc_->energia += r_ * i2 * h + 0.5 * l_ * (i1 * i1 - i0 * i0) + kt_ * w * 0.5 * (i0 + i1) * h;
        fc_->i2s += i2 * h;
    }
    double inercia() const override { return j_; }
    double rozamiento() const override { return tf_ + carga_ * t_nom_; }
    double viscoso() const override { return b_; }
    double posicion_inicial() const override { return c_.posicion_inicial * PI / 180.0; }
    double diametro_eje() const override { return c_.eje_mm; }
    std::string nombre_actuador() const override { return pieza(); }

    void regimen() override {
        if (c_.fases) { regimen_fases(); return; }
        mira_fases();
        const double th = cadena_->th(), w = cadena_->w();
        sector_ = tramo(th, dh_, signo(w));
        if (c_.ppr) q_ = tramo(th, dq_, signo(w));
        pon_sensores();
        // Lo que se deja en el controlador: la tensión que pone y la corriente
        v_aplicada_ = 0.0;
        if (modo_ == Corriente) {
            if (!activa_) v_aplicada_ = i_ == 0.0 ? 0.0 : -signo(i_) * (vbus_ + 2.0 * vd_);
            else {
                const double hace_falta = r_ * obj_ + kt_ * w;
                if (std::fabs(i_ - obj_) <= TOL)
                    v_aplicada_ = std::fabs(hace_falta) <= vbus_ ? hace_falta : signo(hace_falta) * vbus_;
                else v_aplicada_ = i_ < obj_ ? vbus_ : -vbus_;
            }
            fc_->v_carga = v_aplicada_;
            fc_->i_carga = i_;
            fc_->con_carga = true;
            fc_->t_carga = sc_core::sc_time_stamp().to_seconds();
        }
        if (std::fabs(i_) > c_.corriente_pico * 1.001) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: %.1f A, y el motor aguanta %.0f de pico", pieza().c_str(),
                          std::fabs(i_), c_.corriente_pico);
            avisa("pico", t);
        }
    }
    void eventos(sc_core::sc_event_or_list& l) override {
        for (analog_net_if* n : fase_) {
            l |= n->value_changed_event();
            for (const SalidaCorriente& s : n->salidas_corriente())
                if (s.fase && s.fase->ev) l |= *s.fase->ev;
        }
        if (sv_) { l |= sv_->value_changed_event(); l |= sg_->value_changed_event(); }
        l |= evento_conexion();
    }

private:
    enum Modo { Suelto, Corriente, Mal };
    enum Regimen { Nada, Sigue, Tope, Diodos };
    struct Salida { analog_net_if* n = nullptr; int id = -1; bool alto = false; };
    static int signo(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
    // El tramo de ancho d en el que está th; en una frontera exacta, el de
    // delante según el sentido
    static long tramo(double th, double d, int s) { return long(std::floor(th / d + s * 1e-7)); }

    // Cómo van cableados U, V y W
    void mira_fases() {
        static const char* nf[3] = {"U", "V", "W"};
        const std::vector<SalidaCorriente>* sc[3];
        for (int k = 0; k < 3; ++k) sc[k] = &fase_[k]->salidas_corriente();
        Modo m = Suelto;
        FaseCorriente* f = nullptr;
        if (!conectada_ || (sc[0]->empty() && sc[1]->empty() && sc[2]->empty())) {
            m = Suelto;
        } else if (sc[0]->size() == 1 && sc[1]->size() == 1 && sc[2]->size() == 1 &&
                   (*sc[0])[0].fase == (*sc[1])[0].fase && (*sc[0])[0].fase == (*sc[2])[0].fase) {
            f = (*sc[0])[0].fase;
            if ((*sc[0])[0].polo == 1 && (*sc[1])[0].polo == -1 && (*sc[2])[0].polo == 0) {
                m = Corriente;
            } else {
                m = Mal;
                static const char* salida[3] = {"V", "W", "U"};      // polo -1, 0, +1
                char t[300];
                std::snprintf(t, sizeof t, "%s: las fases van cruzadas -U a la %s de %s, V a la %s, "
                              "W a la %s-: con los Hall, la conmutacion no casa con las bobinas, y "
                              "un BLDC asi da sacudidas sin girar. Aqui, sin corriente",
                              pieza().c_str(), salida[(*sc[0])[0].polo + 1], f->quien,
                              salida[(*sc[1])[0].polo + 1], salida[(*sc[2])[0].polo + 1]);
                avisa("cruzadas", t);
            }
        } else {
            m = Mal;
            int k = 0;
            while (k < 3 && sc[k]->size() == 1) ++k;
            char t[260];
            if (k < 3 && sc[k]->empty())
                std::snprintf(t, sizeof t, "%s: la fase %s no va a ninguna salida del controlador: "
                              "con dos fases, sin corriente", pieza().c_str(), nf[k]);
            else if (k < 3)
                std::snprintf(t, sizeof t, "%s: la fase %s va a un nodo con dos salidas de "
                              "controlador: estan en corto", pieza().c_str(), nf[k]);
            else
                std::snprintf(t, sizeof t, "%s: U, V y W van a dos controladores distintos",
                              pieza().c_str());
            avisa("fases", t);
        }
        if (m != Corriente && fc_) { fc_->con_carga = false; fc_ = nullptr; }
        if (m == Corriente) {
            fc_ = f;
            obj_ = f->i;
            vbus_ = f->vbus;
            vd_ = f->vd;
            activa_ = f->activa && vbus_ > 0.0;
        } else {
            i_ = 0.0;
            activa_ = false;
        }
        modo_ = m;
    }

    // Los Hall y el encoder: colector abierto, a masa con 100 ohm o al aire
    void pon_sensores() {
        if (!sv_) return;
        const double vs = double(sv_->voltage()) - double(sg_->voltage());
        sensor_->resuelve(conectada_, 250.0);                  // 20 mA a 5 V
        const bool vivo = conectada_ && vs >= V_SENSOR;
        const unsigned h = hall();
        const long m = ((q_ % 4) + 4) % 4;
        const bool niv[5] = {(h & 4) != 0, (h & 2) != 0, (h & 1) != 0, m == 1 || m == 2,
                             m == 2 || m == 3};
        for (int k = 0; k < 5; ++k) {
            Salida& s = sal_[k];
            if (!s.n) continue;
            if (!vivo || niv[k]) s.n->set_hiz(s.id);
            else s.n->set_drive(s.id, sg_->voltage(), R_BAJO);
            s.alto = niv[k];
        }
    }

    // =========================================================================
    // POR FASES (F7)
    // =========================================================================
    static constexpr double R_ABIERTA = 100.0;     // ohm: más, y la fase está abierta

    // La forma de la f.e.m. de cada fase en el ángulo th del eje
    void forma(double th, double* f) const {
        const double te = std::fmod(double(c_.pares_polos) * th, 2.0 * PI);
        for (int k = 0; k < 3; ++k) {
            double x = te - double(k) * 2.0 * PI / 3.0;
            x = std::fmod(x, 2.0 * PI);
            if (x < 0.0) x += 2.0 * PI;
            if (c_.senoidal) { f[k] = std::sin(x + PI / 6.0); continue; }
            const double g = x * 180.0 / PI;
            if (g <= 120.0) f[k] = 1.0;
            else if (g < 180.0) f[k] = 1.0 - 2.0 * (g - 120.0) / 60.0;
            else if (g <= 300.0) f[k] = -1.0;
            else f[k] = -1.0 + 2.0 * (g - 300.0) / 60.0;
        }
    }
    int cerradas() const { return int(con_[0]) + int(con_[1]) + int(con_[2]); }
    // Las fases abiertas, sin corriente; con dos cerradas, una entra y la otra sale
    void abre(double* e) const {
        int n = 0, a = -1, b = -1;
        for (int k = 0; k < 3; ++k) {
            if (!con_[k]) { e[k] = 0.0; continue; }
            ++n;
            if (a < 0) a = k; else b = k;
        }
        if (n < 2) { e[0] = e[1] = e[2] = 0.0; return; }
        if (n == 2) { const double i = 0.5 * (e[a] - e[b]); e[a] = i; e[b] = -i; }
    }
    void deriva_fases(const double* e, double w, double th, double* de, double& par) const {
        double f[3];
        forma(th, f);
        par = kf_ * (f[0] * e[0] + f[1] * e[1] + f[2] * e[2]);
        de[0] = de[1] = de[2] = 0.0;
        const int n = cerradas();
        if (n < 2) return;
        double sk[3] = {0.0, 0.0, 0.0}, vn = 0.0;
        for (int k = 0; k < 3; ++k) {
            if (!con_[k]) continue;
            sk[k] = vth_[k] - (rf_ + rth_[k]) * e[k] - kf_ * w * f[k];
            vn += sk[k];
        }
        vn /= double(n);
        for (int k = 0; k < 3; ++k) if (con_[k]) de[k] = (sk[k] - vn) / lf_;
    }
    // Las corrientes de continua, parado: el neutro donde sumen cero
    void quietas(double* q) const {
        q[0] = q[1] = q[2] = 0.0;
        if (cerradas() < 2) return;
        double sg = 0.0, sv = 0.0, g[3] = {0.0, 0.0, 0.0};
        for (int k = 0; k < 3; ++k) {
            if (!con_[k]) continue;
            g[k] = 1.0 / (rf_ + rth_[k]);
            sg += g[k];
            sv += g[k] * vth_[k];
        }
        const double vn = sv / sg;
        for (int k = 0; k < 3; ++k) if (con_[k]) q[k] = (vth_[k] - vn) * g[k];
    }
    // Lo que ve la fase k fuera de ella: un Thevenin, o nada
    bool fuera(int k, double& vth, double& rth) const {
        bool solo = false;
        const double v0 = double(fase_[k]->voltage_excluding(idf_[k], solo));
        const double g = fase_[k]->conductance() - 1.0 / double(rdrv_[k]);
        if (solo || g <= G_FLOAT) return false;
        vth = v0;
        rth = 1.0 / g;
        return true;
    }
    void pon_fase(int k, bool on, double v, double r) {
        const float rr = on ? float(std::max(1e-3, r)) : R_HIZ;
        if (!on) {
            if (rdrv_[k] == R_HIZ) return;
            fase_[k]->set_hiz(idf_[k]);
            rdrv_[k] = R_HIZ;
            return;
        }
        if (rr == rdrv_[k] && std::fabs(v - udrv_[k]) < 1e-5) return;
        rdrv_[k] = rr;
        udrv_[k] = v;
        fase_[k]->set_drive(idf_[k], float(v), rr);
    }
    void regimen_fases() {
        const double th = cadena_->th(), w = cadena_->w();
        for (int k = 0; k < 3; ++k) {
            double v = 0.0, r = 0.0;
            con_[k] = conectada_ && fuera(k, v, r) && r < R_ABIERTA;
            vth_[k] = v;
            rth_[k] = r;
        }
        abre(ip_);
        double f[3];
        forma(th, f);
        const int n = cerradas();
        // El neutro, y los bornes: los cerrados, a lo que deja fuera la
        // corriente de su bobina; los abiertos, al neutro más su f.e.m.
        double vn = 0.0;
        if (n >= 2) {
            for (int k = 0; k < 3; ++k)
                if (con_[k]) vn += vth_[k] - (rf_ + rth_[k]) * ip_[k] - kf_ * w * f[k];
            vn /= double(n);
        } else if (n == 1) {
            for (int k = 0; k < 3; ++k) if (con_[k]) vn = vth_[k] - kf_ * w * f[k];
        }
        vn_ = vn;
        double vq = 0.0;
        for (int k = 0; k < 3; ++k) {
            if (!conectada_ || n == 0) { pon_fase(k, false, 0.0, 0.0); continue; }
            if (con_[k] && n >= 2) {
                pon_fase(k, true, vth_[k] - (rth_[k] + rf_) * ip_[k], rf_);
                vq += f[k] * (vth_[k] - rth_[k] * ip_[k]);
            } else if (con_[k]) {
                pon_fase(k, false, 0.0, 0.0);
                vq += f[k] * vth_[k];
            } else {
                pon_fase(k, true, vn + kf_ * w * f[k], rf_);
                vq += f[k] * (vn + kf_ * w * f[k]);
            }
        }
        v_aplicada_ = 0.5 * vq;
        sector_ = tramo(th, dh_, signo(w));
        if (c_.ppr) q_ = tramo(th, dq_, signo(w));
        pon_sensores();
        if (pico() > c_.corriente_pico * 1.001) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: %.2f A por una fase, y el motor aguanta %.2f de pico",
                          pieza().c_str(), pico(), c_.corriente_pico);
            avisa("pico", t);
        }
    }
    double pico() const {
        if (!c_.fases) return std::fabs(i_);
        return std::max(std::fabs(ip_[0]), std::max(std::fabs(ip_[1]), std::fabs(ip_[2])));
    }

    void motor() {
        if (cadena_ != &propia_) return;
        propia_.corre();
    }
    void avisa(const std::string& que, const std::string& t) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("motor_bldc", t.c_str());
    }

    ConfigMotorBLDC c_;
    analog_net_if* fase_[3] = {nullptr, nullptr, nullptr};
    analog_net_if *sv_ = nullptr, *sg_ = nullptr;
    std::unique_ptr<RamaDosNodos> sensor_;
    Salida sal_[5];                  // H1, H2, H3, A y B
    double kt_ = 0.05, r_ = 0.22, l_ = 0.29e-3, j_ = 1.19e-5, tf_ = 0.0, b_ = 0.0, t_nom_ = 0.47;
    double dh_ = 1.0, dq_ = 1.0;
    long sector_ = 0, q_ = 0;
    Cadena propia_;
    double i_ = 0.0, v_aplicada_ = 0.0;
    // Lo que manda el controlador
    Modo modo_ = Suelto;
    FaseCorriente* fc_ = nullptr;
    double obj_ = 0.0, vbus_ = 0.0, vd_ = 0.7;
    bool activa_ = false;
    mutable int reg_ = Nada;
    mutable double u_ = 0.0;
    // Los mandos
    bool bloq_ = false;
    double carga_ = 0.0;
    // Por fases (F7)
    int idf_[3] = {-1, -1, -1};
    double ip_[3] = {0.0, 0.0, 0.0};
    double vth_[3] = {0.0, 0.0, 0.0}, rth_[3] = {0.0, 0.0, 0.0};
    bool con_[3] = {false, false, false};
    float rdrv_[3] = {R_HIZ, R_HIZ, R_HIZ};
    double udrv_[3] = {0.0, 0.0, 0.0};
    double rf_ = 0.11, lf_ = 0.145e-3, kf_ = 0.025, vn_ = 0.0;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_MOTOR_BLDC_H
