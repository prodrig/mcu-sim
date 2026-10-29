// =============================================================================
// maquina.h — Lo que rodea a los ejes de una fresadora: finales de carrera,
//             la sonda de herramienta, la seta de emergencia y las cotas
//
// (Fase F3 de doc/analisis_cnc.md, §7.)
//
//   FinalCarrera  un microinterruptor que pisa un carro (parts/transmision.h)
//                 al pasar por una posición: sus contactos COM, NO y NC. Es
//                 el homing ($H) y los límites duros
//   Sonda         la placa de contacto del cero de Z (G38.2): la fresa toca la
//                 placa cuando el carro Z baja hasta su altura, y cierra el
//                 circuito entre las dos pinzas
//   Interruptor   uno o dos contactos que se accionan a mano, normalmente
//                 cerrados o abiertos, con enclavamiento o sin él: una SETA DE
//                 EMERGENCIA, que corta la potencia de los drivers y avisa al
//                 MCU con su segundo contacto
//   Cotas         lo que enseña el control numérico: la posición de los tres
//                 carros, X, Y y Z, en mm. Para el display del dibujo y para
//                 trasladar la herramienta en el alzado con dos ejes a la vez
//
// Los tres primeros son CONTACTOS: ramas entre dos nodos (RamaDosNodos), que
// se vuelven a resolver cada vez que cambia uno de sus nodos o lo que los
// acciona. El carro avisa a los que lo miran cada vez que se mueve.
// =============================================================================
#ifndef STM32_PARTS_MAQUINA_H
#define STM32_PARTS_MAQUINA_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"
#include "transmision.h"

namespace stm32 {

// Un contacto entre dos nodos: cerrado con r ohmios, o abierto
class Contacto {
public:
    Contacto(analog_net_if& a, int id_a, analog_net_if& b, int id_b) : a_(&a), b_(&b),
        rama_(a, id_a, b, id_b) {}
    void pon(bool cerrado, double r) { rama_.resuelve(cerrado, r); }
    void suelta() { rama_.suelta(); }
    void eventos(sc_core::sc_event_or_list& l) const {
        l |= a_->value_changed_event();
        l |= b_->value_changed_event();
    }
private:
    analog_net_if *a_, *b_;
    RamaDosNodos rama_;
};

// ---------------------------------------------------------------------------
// UN FINAL DE CARRERA
// ---------------------------------------------------------------------------
struct ConfigFinalCarrera {
    double posicion_mm = 1.0;       // donde se pisa
    bool   lado_max = false;        // false: pisado por debajo; true: por encima
    double histeresis_mm = 0.02;    // lo que hay que volver para soltarlo
    double r_contacto = 0.05;
};

SC_MODULE(FinalCarrera), public ExtPartBase {
    FinalCarrera(sc_core::sc_module_name nm, Carro& carro, const ConfigFinalCarrera& c,
                 analog_net_if& com, analog_net_if* no, analog_net_if* nc)
        : sc_core::sc_module(nm), ExtPartBase("FinalCarrera", nm), carro_(&carro), c_(c) {
        const int ic = add_pin("com", com, "final");
        if (no) no_.reset(new Contacto(com, ic, *no, add_pin("no", *no, "final")));
        if (nc) nc_.reset(new Contacto(com, add_drv("com", "final"), *nc, add_pin("nc", *nc, "final")));
        carro.vigilado();
        SC_HAS_PROCESS(FinalCarrera);
        SC_THREAD(vigila);
    }
    ~FinalCarrera() override {
        if (no_) no_->suelta();
        if (nc_) nc_->suelta();
    }
    bool pisado() const { return pisado_; }
    unsigned veces() const { return veces_; }
    const ConfigFinalCarrera& config() const { return c_; }
    const Carro& carro() const { return *carro_; }

    unsigned n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"pisado", "", 0.f, 1.f, true, true}; }
    float valor_observable(unsigned) const override { return pisado_ ? 1.f : 0.f; }

private:
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= carro_->evento_mueve();
        l |= evento_conexion();
        if (no_) no_->eventos(l);
        if (nc_) nc_->eventos(l);
        for (;;) {
            const double x = carro_->posicion_mm();
            const double h = pisado_ ? c_.histeresis_mm : 0.0;
            const bool p = c_.lado_max ? x >= c_.posicion_mm - h : x <= c_.posicion_mm + h;
            if (p && !pisado_) ++veces_;
            pisado_ = p;
            if (no_) no_->pon(conectada_ && p, c_.r_contacto);
            if (nc_) nc_->pon(conectada_ && !p, c_.r_contacto);
            wait(l);
        }
    }
    Carro* carro_;
    ConfigFinalCarrera c_;
    std::unique_ptr<Contacto> no_, nc_;
    bool pisado_ = false;
    unsigned veces_ = 0;
};

// ---------------------------------------------------------------------------
// LA SONDA DE HERRAMIENTA: una placa de contacto sobre la pieza
// ---------------------------------------------------------------------------
SC_MODULE(Sonda), public ExtPartBase {
    Sonda(sc_core::sc_module_name nm, Carro& carro, double altura_mm, analog_net_if& placa,
          analog_net_if& fresa, bool puesta = true)
        : sc_core::sc_module(nm), ExtPartBase("Sonda", nm), carro_(&carro), altura_(altura_mm),
          puesta_(puesta),
          contacto_(placa, add_pin("placa", placa, "sonda"), fresa, add_pin("fresa", fresa, "sonda")) {
        carro.vigilado();
        SC_HAS_PROCESS(Sonda);
        SC_THREAD(vigila);
    }
    ~Sonda() override { contacto_.suelta(); }
    bool toca() const { return toca_; }
    double altura_mm() const { return altura_; }
    bool puesta() const { return puesta_; }
    void pon(bool si) { puesta_ = si; ev_.notify(sc_core::SC_ZERO_TIME); }

    unsigned n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"toca", "", 0.f, 1.f, true}; }
    float valor_observable(unsigned) const override { return toca_ ? 1.f : 0.f; }
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override { return {"puesta", Mando::Interruptor, 0.f, 1.f}; }
    float valor_mando(unsigned) const override { return puesta_ ? 1.f : 0.f; }
    void acciona(unsigned, float v) override { pon(v >= 0.5f); }

private:
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= carro_->evento_mueve();
        l |= evento_conexion();
        l |= ev_;
        contacto_.eventos(l);
        for (;;) {
            toca_ = puesta_ && carro_->posicion_mm() <= altura_;
            contacto_.pon(conectada_ && toca_, 0.5);
            wait(l);
        }
    }
    Carro* carro_;
    double altura_;
    bool puesta_, toca_ = false;
    Contacto contacto_;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// UN INTERRUPTOR A MANO: una seta de emergencia, un pulsador de marcha
// ---------------------------------------------------------------------------
SC_MODULE(Interruptor), public ExtPartBase {
    // `nc[k]`: el contacto k está cerrado en reposo
    Interruptor(sc_core::sc_module_name nm, analog_net_if& a1, analog_net_if& b1,
                analog_net_if* a2, analog_net_if* b2, bool nc1, bool nc2, bool enclavado,
                double r_contacto = 0.02)
        : sc_core::sc_module(nm), ExtPartBase("Interruptor", nm), enclavado_(enclavado),
          r_(r_contacto) {
        nc_[0] = nc1;
        nc_[1] = nc2;
        c_[0].reset(new Contacto(a1, add_pin("a1", a1, "interruptor"), b1, add_pin("b1", b1, "interruptor")));
        if (a2 && b2)
            c_[1].reset(new Contacto(*a2, add_pin("a2", *a2, "interruptor"), *b2,
                                     add_pin("b2", *b2, "interruptor")));
        SC_HAS_PROCESS(Interruptor);
        SC_THREAD(vigila);
    }
    ~Interruptor() override { for (auto& c : c_) if (c) c->suelta(); }
    bool pulsado() const { return pulsado_; }
    void pulsa(bool si) { pulsado_ = si; ev_.notify(sc_core::SC_ZERO_TIME); }
    bool enclavado() const { return enclavado_; }

    unsigned n_observables() const override { return 1; }
    Observable observable(unsigned) const override { return {"pulsado", "", 0.f, 1.f, true, true}; }
    float valor_observable(unsigned) const override { return pulsado_ ? 1.f : 0.f; }
    unsigned n_mandos() const override { return 1; }
    Mando mando(unsigned) const override {
        return {"pulsar", enclavado_ ? Mando::Interruptor : Mando::Boton, 0.f, 1.f};
    }
    float valor_mando(unsigned) const override { return pulsado_ ? 1.f : 0.f; }
    void acciona(unsigned, float v) override { pulsa(v >= 0.5f); }

private:
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= evento_conexion();
        l |= ev_;
        for (auto& c : c_) if (c) c->eventos(l);
        for (;;) {
            for (int k = 0; k < 2; ++k)
                if (c_[k]) c_[k]->pon(conectada_ && (pulsado_ != nc_[k]), r_);
            wait(l);
        }
    }
    std::unique_ptr<Contacto> c_[2];
    bool nc_[2] = {true, true};
    bool enclavado_, pulsado_ = false;
    double r_;
    sc_core::sc_event ev_;
};

// ---------------------------------------------------------------------------
// LAS COTAS: X, Y y Z de tres carros, en mm
// ---------------------------------------------------------------------------
class Cotas : public ExtPartBase {
public:
    Cotas(const char* nombre, const Carro* x, const Carro* y, const Carro* z)
        : ExtPartBase("Cotas", nombre) { c_[0] = x; c_[1] = y; c_[2] = z; }
    double cota(int k) const { return c_[k] ? c_[k]->posicion_mm() : 0.0; }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        static const char* n[3] = {"x", "y", "z"};
        const float r = c_[i] ? float(c_[i]->config().recorrido_mm) : 100.f;
        return {n[i], "mm", -r, r, true};
    }
    float valor_observable(unsigned i) const override { return float(cota(int(i))); }
private:
    const Carro* c_[3];
};

} // namespace stm32

#endif // STM32_PARTS_MAQUINA_H
