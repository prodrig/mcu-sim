// =============================================================================
// transmision.h — LO QUE HAY ENTRE EL MOTOR Y LA CARGA: reductora, acople,
//                 husillo con su tuerca, carro y freno
//
// (Fase F2 de doc/analisis_cnc.md, §6.)
//
// Son ELEMENTOS de una cadena (parts/cadena.h): una <transmision> los pone en
// orden detrás de sus motores, del motor hacia la carga, y la cadena refleja
// cada uno en el eje de los motores y lo integra todo junto. Cada uno dice su
// relación -vueltas de su entrada por cada una de su salida-, su inercia, sus
// rozamientos, su rendimiento y, si los tiene, su holgura, sus topes y su
// carga activa.
//
//   Reductora   una relación, un rendimiento directo y otro inverso -cero o
//               menos en un sinfín que se sujeta solo-, una holgura en minutos
//               de arco, la inercia de su entrada y un par máximo que se avisa
//   Acople      una inercia y, si acaso, una holgura; y los dos diámetros, que
//               tienen que encajar con el eje de delante y el de detrás
//   Husillo     de rosca trapezoidal, ACME o de bolas. El RENDIMIENTO NO SE
//               DA: sale del ángulo de la hélice y del rozamiento de la tuerca,
//               y con él el inverso y el AUTOBLOQUEO. Su inercia, de su
//               diámetro y su largo. La holgura de su tuerca. Avisa de pasar
//               de su velocidad crítica y, si es de bolas, de su DN
//   Carro       las guías, el carro y lo que lleva: una masa, un rozamiento,
//               un recorrido con topes duros y, si es vertical, su peso. Su
//               entrada es LINEAL, en metros: la salida del husillo
//   Freno       de muelle: frena sin corriente y se suelta con la suya. Una
//               bobina de V²/P ohmios, un retardo al soltar y otro al echar, y
//               un par de retención que entra en la cadena como rozamiento
//
// Unidades: los ejes giratorios en rad y N·m; la entrada de un carro, en m y N.
// La relación de un husillo es 2π/avance: radianes por metro.
// =============================================================================
#ifndef STM32_PARTS_TRANSMISION_H
#define STM32_PARTS_TRANSMISION_H

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

namespace transmision {
constexpr double PI = 3.14159265358979323846;
constexpr double ACERO = 7850.0;           // kg/m³
constexpr double G = 9.81;
inline double arcmin(double a) { return a / 60.0 * PI / 180.0; }
}

// Lo común: los avisos, una vez cada uno
class ElementoConAvisos : public ExtPartBase, public ElementoMecanico {
public:
    ElementoConAvisos(const char* tipo, const char* nombre) : ExtPartBase(tipo, nombre) {}
    std::string nombre_elemento() const override { return pieza(); }
protected:
    void avisa(const std::string& que, const std::string& t) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("transmision", t.c_str());
    }
    std::set<std::string> dichos_;
};

// ---------------------------------------------------------------------------
// UNA REDUCTORA
// ---------------------------------------------------------------------------
struct ConfigReductora {
    double relacion = 3.0;
    double rendimiento = 0.9;
    double rendimiento_inverso = -1.0;      // < -0.5: el mismo que el directo
    double holgura_arcmin = 0.0;
    double inercia_gcm2 = 0.0;              // en su entrada
    double par_max_nm = 0.0;                // en su salida; 0, sin límite
    double entrada_mm = 0.0, salida_mm = 0.0;
};

class Reductora : public ElementoConAvisos {
public:
    Reductora(const char* nombre, const ConfigReductora& c)
        : ElementoConAvisos("Reductora", nombre), c_(c) {}
    double relacion() const override { return c_.relacion; }
    double rendimiento() const override { return c_.rendimiento; }
    double rendimiento_inverso() const override {
        return c_.rendimiento_inverso < -0.5 ? c_.rendimiento : c_.rendimiento_inverso;
    }
    double inercia() const override { return c_.inercia_gcm2 * 1e-7; }
    double holgura() const override { return transmision::arcmin(c_.holgura_arcmin); }
    double diametro_entrada() const override { return c_.entrada_mm; }
    double diametro_salida() const override { return c_.salida_mm; }
    bool quiere_esfuerzo() const override { return c_.par_max_nm > 0.0; }
    void esfuerzo(double par) override {
        par_salida_ = par * c_.relacion * c_.rendimiento;
        if (c_.par_max_nm > 0.0 && std::fabs(par_salida_) > c_.par_max_nm) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: %.2f N·m en su salida, y aguanta %.2f",
                          pieza().c_str(), std::fabs(par_salida_), c_.par_max_nm);
            avisa("par", t);
        }
    }
    void mueve(double th, double w) override { th_ = th; w_ = w; }
    double rpm_salida() const { return w_ / c_.relacion * 60.0 / (2.0 * transmision::PI); }
    double par_salida() const { return par_salida_; }
    const ConfigReductora& config() const { return c_; }

    unsigned n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"angulo", "°", 0.f, 360.f, true};
        return {"rpm", "rpm", -3000.f, 3000.f, false};
    }
    float valor_observable(unsigned i) const override {
        if (i == 1) return float(rpm_salida());
        double g = std::fmod(th_ / c_.relacion * 180.0 / transmision::PI, 360.0);
        return float(g < 0 ? g + 360.0 : g);
    }
private:
    ConfigReductora c_;
    double th_ = 0.0, w_ = 0.0, par_salida_ = 0.0;
};

// ---------------------------------------------------------------------------
// UN ACOPLE
// ---------------------------------------------------------------------------
struct ConfigAcople {
    std::string tipo = "espiral";           // espiral, mordaza, fuelle
    double inercia_gcm2 = 2.0;
    double holgura_arcmin = 0.0;
    double d1_mm = 0.0, d2_mm = 0.0;        // el del motor y el del husillo
};

class Acople : public ElementoConAvisos {
public:
    Acople(const char* nombre, const ConfigAcople& c) : ElementoConAvisos("Acople", nombre), c_(c) {}
    double inercia() const override { return c_.inercia_gcm2 * 1e-7; }
    double holgura() const override { return transmision::arcmin(c_.holgura_arcmin); }
    double diametro_entrada() const override { return c_.d1_mm; }
    double diametro_salida() const override { return c_.d2_mm; }
    void mueve(double th, double) override { th_ = th; }
    const ConfigAcople& config() const { return c_; }
    unsigned n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"angulo", "°", 0.f, 360.f, true}; }
    float valor_observable(unsigned) const override {
        double g = std::fmod(th_ * 180.0 / transmision::PI, 360.0);
        return float(g < 0 ? g + 360.0 : g);
    }
private:
    ConfigAcople c_;
    double th_ = 0.0;
};

// ---------------------------------------------------------------------------
// UN HUSILLO CON SU TUERCA
//
//     λ  = atan(avance / (π·d_m))                 el ángulo de la hélice
//     φ' = atan(μ / cos(α/2))                     el de rozamiento, con el flanco
//     η  = tan λ / tan(λ + φ')                    directo: el motor empuja la carga
//     ηi = tan(λ − φ') / tan λ                    inverso: la carga mueve al motor
//
// con d_m el diámetro medio -el nominal menos medio paso, en una rosca; el
// nominal, en uno de bolas- y α el ángulo de la rosca: 30° trapezoidal, 29°
// ACME, 0 de bolas. Con ηi <= 0 -φ' >= λ-, la carga no puede mover el motor:
// SE SUJETA SOLO. La inercia, la de un cilindro macizo de acero de su
// diámetro y su largo.
//
// LA VELOCIDAD CRÍTICA: el husillo vibra como una cuerda, n_c = f·1,2·10⁵·d_r/L²
// rpm (d_r, el de la raíz, y L, en m), con f según sus apoyos: 0,36 fijo-libre,
// 1 apoyado-apoyado, 1,47 fijo-apoyado, 2,24 fijo-fijo. Y en uno de bolas, el
// DN, d·n <= 70 000 (mm·rpm).
// ---------------------------------------------------------------------------
struct ConfigHusillo {
    std::string rosca = "Tr";               // Tr, ACME o bolas
    double diametro_mm = 8.0;
    double avance_mm = 8.0;                 // lo que avanza la tuerca por vuelta
    double paso_mm = 2.0;                   // el de la rosca (avance/entradas)
    double mu = 0.15;
    double longitud_mm = 350.0;
    std::string apoyos = "fijo-apoyado";
    double holgura_mm = 0.0;
    double extremo_mm = 0.0;                // el del extremo que va al acople; 0, el nominal
};

class Husillo : public ElementoConAvisos {
public:
    Husillo(const char* nombre, const ConfigHusillo& c) : ElementoConAvisos("Husillo", nombre), c_(c) {
        using namespace transmision;
        const bool bolas = c_.rosca == "bolas";
        const double alfa = bolas ? 0.0 : (c_.rosca == "ACME" ? 29.0 : 30.0) * PI / 180.0;
        dm_ = bolas ? c_.diametro_mm : c_.diametro_mm - 0.5 * c_.paso_mm;
        lambda_ = std::atan(c_.avance_mm / (PI * dm_));
        phi_ = std::atan(c_.mu / std::cos(alfa / 2.0));
        eta_ = std::tan(lambda_) / std::tan(lambda_ + phi_);
        etai_ = std::tan(lambda_ - phi_) / std::tan(lambda_);
        const double d = c_.diametro_mm * 1e-3, L = c_.longitud_mm * 1e-3;
        j_ = ACERO * PI * d * d * d * d * L / 32.0;
        const double dr = (bolas ? c_.diametro_mm - 3.0 : c_.diametro_mm - c_.paso_mm) * 1e-3;
        double f = 1.0;
        if (c_.apoyos == "fijo-libre") f = 0.36;
        else if (c_.apoyos == "fijo-apoyado") f = 1.47;
        else if (c_.apoyos == "fijo-fijo") f = 2.24;
        n_critica_ = f * 1.2e5 * dr / (L * L);
    }
    double relacion() const override { return 2.0 * transmision::PI / (c_.avance_mm * 1e-3); }
    double rendimiento() const override { return eta_; }
    double rendimiento_inverso() const override { return etai_; }
    double inercia() const override { return j_; }
    double holgura() const override { return c_.holgura_mm * 1e-3; }
    double diametro_entrada() const override { return c_.extremo_mm > 0 ? c_.extremo_mm : c_.diametro_mm; }
    void mueve(double th, double w) override {
        th_ = th;
        w_ = w;
        const double rpm = std::fabs(w) * 60.0 / (2.0 * transmision::PI);
        if (rpm > n_critica_) {
            char t[260];
            std::snprintf(t, sizeof t, "%s: a %.0f rpm, por encima de su velocidad critica, %.0f "
                          "(%s de %.0f mm, apoyos %s): vibra como una cuerda", pieza().c_str(),
                          rpm, n_critica_, c_.rosca.c_str(), c_.longitud_mm, c_.apoyos.c_str());
            avisa("critica", t);
        }
        if (c_.rosca == "bolas" && rpm * c_.diametro_mm > 70000.0) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: d·n = %.0f, y un husillo de bolas aguanta 70000",
                          pieza().c_str(), rpm * c_.diametro_mm);
            avisa("dn", t);
        }
    }
    // Para los informes
    double lambda_grados() const { return lambda_ * 180.0 / transmision::PI; }
    double rendimiento_directo() const { return eta_; }
    double rendimiento_inv() const { return etai_; }
    bool   se_sujeta() const { return etai_ <= 0.0; }
    double velocidad_critica() const { return n_critica_; }
    double rpm() const { return w_ * 60.0 / (2.0 * transmision::PI); }
    // La fuerza que da un par T en el husillo, empujando la carga
    double fuerza_de(double par) const { return 2.0 * transmision::PI * eta_ * par / (c_.avance_mm * 1e-3); }
    const ConfigHusillo& config() const { return c_; }

    unsigned n_observables() const override { return 2; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"angulo", "°", 0.f, 360.f, true};
        return {"rpm", "rpm", -3000.f, 3000.f, false};
    }
    float valor_observable(unsigned i) const override {
        if (i == 1) return float(rpm());
        double g = std::fmod(th_ * 180.0 / transmision::PI, 360.0);
        return float(g < 0 ? g + 360.0 : g);
    }
private:
    ConfigHusillo c_;
    double dm_ = 7.0, lambda_ = 0.0, phi_ = 0.0, eta_ = 0.5, etai_ = 0.0, j_ = 0.0, n_critica_ = 1e9;
    double th_ = 0.0, w_ = 0.0;
};

// ---------------------------------------------------------------------------
// UN CARRO: guías, carro y lo que lleva
// ---------------------------------------------------------------------------
struct ConfigCarro {
    double masa_kg = 1.0;
    double rozamiento_n = 5.0;              // seco, de sus guías
    double viscoso_ns_m = 0.0;
    double recorrido_mm = 300.0;
    double posicion_mm = 0.0;               // donde empieza
    bool   vertical = false;                // con su peso; positivo, hacia arriba
    std::string eje = "x";
};

class Carro : public ElementoConAvisos {
public:
    Carro(const char* nombre, const ConfigCarro& c) : ElementoConAvisos("Carro", nombre), c_(c) {
        x_ = c_.posicion_mm * 1e-3;
    }
    double inercia() const override { return c_.masa_kg; }           // kg: su entrada es lineal
    double rozamiento() const override { return c_.rozamiento_n + roz_corte_; }
    double viscoso() const override { return c_.viscoso_ns_m; }
    double carga_activa() const override {
        return (c_.vertical ? -c_.masa_kg * transmision::G : 0.0) + fuerza_;
    }
    bool limites(double& lo, double& hi) const override {
        lo = 0.0;
        hi = c_.recorrido_mm * 1e-3;
        return true;
    }
    double posicion_inicial() const override { return c_.posicion_mm * 1e-3; }
    void tope(int cual) override {
        if (cual != 0 && !tope_ && std::fabs(v_ant_) > 1e-4) {
            char t[200];
            std::snprintf(t, sizeof t, "%s: el carro %s choca con el tope %s a %.1f mm/s",
                          pieza().c_str(), c_.eje.c_str(), cual < 0 ? "de abajo" : "de arriba",
                          std::fabs(v_ant_) * 1e3);
            avisa(std::string("tope") + (cual < 0 ? "0" : "1"), t);
            choques_++;
        }
        tope_ = cual != 0;
    }
    void mueve(double x, double v) override {
        if (ev_mueve_ && std::fabs(x - x_avisado_) > 1e-7) {
            x_avisado_ = x;
            ev_mueve_->notify(sc_core::SC_ZERO_TIME);
        }
        x_ = x;
        v_ = v;
        v_ant_ = v;
    }
    // Lo que lo mira -un final de carrera, una sonda- se entera de que se ha
    // movido por este suceso (F3, parts/maquina.h)
    void vigilado() {
        if (!ev_mueve_) ev_mueve_.reset(new sc_core::sc_event());
    }
    const sc_core::sc_event& evento_mueve() const { return *ev_mueve_; }
    double posicion_mm() const { return x_ * 1e3; }
    double velocidad_mm_s() const { return v_ * 1e3; }
    bool   en_tope() const { return tope_; }
    unsigned choques() const { return choques_; }
    double fuerza() const { return fuerza_; }
    void   pon_fuerza(double f) { fuerza_ = f; if (cadena_) cadena_->toca(); }
    // La resistencia del corte (F4, parts/pieza.h): un rozamiento seco MÁS,
    // en N, que se opone al movimiento y no empuja nunca un carro parado
    double rozamiento_corte() const { return roz_corte_; }
    void   pon_rozamiento_corte(double n) {
        roz_corte_ = n > 0.0 ? n : 0.0;
        if (cadena_) cadena_->toca();
    }
    const ConfigCarro& config() const { return c_; }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        const float r = float(c_.recorrido_mm);
        if (i == 0) return {"posicion", "mm", -r, r, true};
        if (i == 1) return {"velocidad", "mm/s", -200.f, 200.f, false};
        return {"en_tope", "", 0.f, 1.f, true, true};
    }
    float valor_observable(unsigned i) const override {
        if (i == 0) return float(posicion_mm());
        if (i == 1) return float(velocidad_mm_s());
        return tope_ ? 1.f : 0.f;
    }
    // La fuerza de fuera, la del corte (F4): se opone si es negativa
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override { return {"fuerza", Mando::Continuo, -500.f, 500.f}; }
    float valor_mando(unsigned) const override { return float(fuerza_); }
    void acciona(unsigned, float v) override { pon_fuerza(double(v)); }
private:
    ConfigCarro c_;
    double x_ = 0.0, v_ = 0.0, v_ant_ = 0.0, fuerza_ = 0.0, x_avisado_ = -1.0, roz_corte_ = 0.0;
    bool tope_ = false;
    std::unique_ptr<sc_core::sc_event> ev_mueve_;
    unsigned choques_ = 0;
};

// ---------------------------------------------------------------------------
// UN FRENO DE MUELLE: frena sin corriente y se suelta con la suya
// ---------------------------------------------------------------------------
struct ConfigFreno {
    double v_nominal = 24.0;
    double potencia_w = 8.0;
    double par_ncm = 50.0;                  // de retención
    double suelta_ms = 30.0;                // estimados: las fichas no los dan
    double echa_ms = 20.0;
    double eje_mm = 0.0;
};

SC_MODULE(Freno), public ElementoConAvisos {
    Freno(sc_core::sc_module_name nm, analog_net_if& a, analog_net_if& b, const ConfigFreno& c)
        : sc_core::sc_module(nm), ElementoConAvisos("Freno", nm), a_(&a), b_(&b), c_(c) {
        const int ia = add_pin("a", a, "freno");
        const int ib = add_pin("b", b, "freno");
        bobina_.reset(new RamaDosNodos(a, ia, b, ib));
        r_ = c_.v_nominal * c_.v_nominal / c_.potencia_w;
        SC_HAS_PROCESS(Freno);
        SC_METHOD(vigila);
        sensitive << a.value_changed_event() << b.value_changed_event() << ev_;
    }
    ~Freno() override { bobina_->suelta(); }

    double rozamiento() const override { return suelto_ ? 0.0 : c_.par_ncm * 1e-2; }
    double diametro_entrada() const override { return c_.eje_mm; }
    double diametro_salida() const override { return c_.eje_mm; }
    void mueve(double, double w) override {
        if (!suelto_ && w != 0.0 && !patina_) {
            patina_ = true;
            char t[200];
            std::snprintf(t, sizeof t, "%s: el freno esta echado y el eje gira: algo empuja con "
                          "mas de %.0f N·cm", pieza().c_str(), c_.par_ncm);
            avisa("patina", t);
        }
        if (suelto_ || w == 0.0) patina_ = false;
    }
    bool   suelto() const { return suelto_; }
    double corriente_ma() const { return conectada_ ? std::fabs(v_bobina()) / r_ * 1e3 : 0.0; }
    double r_bobina() const { return r_; }
    const ConfigFreno& config() const { return c_; }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        if (i == 0) return {"suelto", "", 0.f, 1.f, true};
        if (i == 1) return {"corriente", "mA", 0.f, 1000.f, false};
        return {"patina", "", 0.f, 1.f, true, true};
    }
    float valor_observable(unsigned i) const override {
        if (i == 0) return suelto_ ? 1.f : 0.f;
        if (i == 1) return float(corriente_ma());
        return patina_ ? 1.f : 0.f;
    }
private:
    double v_bobina() const { return double(a_->voltage()) - double(b_->voltage()); }
    void vigila() {
        bobina_->resuelve(conectada_, r_);
        const double t = sc_core::sc_time_stamp().to_seconds();
        const double v = conectada_ ? std::fabs(v_bobina()) : 0.0;
        // Se suelta con el 70 % de su tensión, y se echa por debajo del 25 %
        const bool quiere = suelto_ ? v >= 0.25 * c_.v_nominal : v >= 0.7 * c_.v_nominal;
        if (quiere != objetivo_) { objetivo_ = quiere; t_cambio_ = t; }
        if (objetivo_ != suelto_) {
            const double espera = (objetivo_ ? c_.suelta_ms : c_.echa_ms) * 1e-3;
            if (t - t_cambio_ >= espera - 1e-12) {
                suelto_ = objetivo_;
                if (cadena_) cadena_->toca();
            } else {
                ev_.notify(sc_core::sc_time(espera - (t - t_cambio_), sc_core::SC_SEC));
            }
        }
    }
    analog_net_if *a_, *b_;
    ConfigFreno c_;
    std::unique_ptr<RamaDosNodos> bobina_;
    double r_ = 72.0, t_cambio_ = 0.0;
    bool suelto_ = false, objetivo_ = false, patina_ = false;
    sc_core::sc_event ev_;
};

} // namespace stm32

#endif // STM32_PARTS_TRANSMISION_H
