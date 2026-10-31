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
        add_ref("u", u);
        add_ref("v", v);
        add_ref("w", w);
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
    double corriente() const { return i_; }                   // A, la del eje q
    double par() const { return kt_ * i_; }                   // N·m, sin el rozamiento
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
    unsigned n_observables() const override { return tiene_encoder() ? 8 : 7; }
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
            default: return {"cuenta", "", -1e9f, 1e9f, false};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(angulo());
            case 1: return float(rpm());
            case 2: return float(i_);
            case 3: return float(par());
            case 4: return float(v_aplicada_);
            case 5: return float(hall());
            case 6: return std::fabs(i_) > c_.corriente_pico ? 1.f : 0.f;
            default: return float(q_);
        }
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
    unsigned n_e() const override { return 1; }
    void lee_e(double* e) const override { e[0] = i_; }
    void pon_e(const double* e) override { i_ = e[0]; }
    void deriva(const double* e, double w, double, double* de, double& par) const override {
        de[0] = (reg_ == Sigue || reg_ == Nada) ? 0.0 : (u_ - r_ * e[0] - kt_ * w) / l_;
        par = kt_ * e[0];
    }
    // El régimen del paso, que no cambia hasta el siguiente (como el de una
    // bobina de un paso a paso: parts/motor_pap.h)
    void prepara(double* e, double w, double) const override {
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
        if (modo_ != Corriente || reg_ == Sigue || reg_ == Nada) return;
        // Solo si la CRUZA: la que sale de la consigna empujada por el tope -la
        // f.e.m. se come la tensión- se va, y no vuelve
        const double obj = activa_ ? obj_ : 0.0;
        if ((e0[0] - obj) * (e[0] - obj) < 0.0) e[0] = obj;
    }
    double paso_max() const override {
        return modo_ == Corriente ? std::min(PASO, 0.2 * l_ / r_) : Actuador::INF;
    }
    bool inerte() const override { return modo_ != Corriente || (!activa_ && i_ == 0.0); }
    bool asentado(const double* e) const override {
        if (modo_ != Corriente) return e[0] == 0.0;
        if (!activa_) return e[0] == 0.0;
        return std::fabs(e[0] - obj_) <= TOL && std::fabs(r_ * obj_) <= vbus_;
    }
    double par_asentado(double) const override {
        return (modo_ == Corriente && activa_) ? kt_ * obj_ : 0.0;
    }
    void limpia(double* e) const override { asienta(e); }
    void asienta(double* e) const override { e[0] = (modo_ == Corriente && activa_) ? obj_ : 0.0; }
    bool asentando(const double*, const double* de) const override {
        return std::fabs(de[0]) * Cadena::REFRESCO_I > 1e-3;
    }
    bool estable(const double*, const double* de) const override { return std::fabs(de[0]) < 1e-6; }
    // El próximo cambio de los Hall o del encoder, con la velocidad de ahora
    double hasta_marca(double th, double w) const override {
        if (!tiene_hall() || w == 0.0) return Actuador::INF;
        const int s = signo(w);
        double t = std::fabs(((s > 0 ? sector_ + 1 : sector_) * dh_ - th) / w);
        if (tiene_encoder()) t = std::min(t, std::fabs(((s > 0 ? q_ + 1 : q_) * dq_ - th) / w));
        return std::max(1e-9, t);
    }
    bool cambia(const double*, const double*, double th, double w) const override {
        if (!tiene_hall()) return false;
        if (tramo(th, dh_, signo(w)) != sector_) return true;
        return tiene_encoder() && tramo(th, dq_, signo(w)) != q_;
    }
    // Lo que entra desde el controlador, paso a paso, por el balance de la bobina
    void hecho(const double* e0, const double* e, double w, double, double h) override {
        if (modo_ != Corriente || !fc_) return;
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
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_MOTOR_BLDC_H
