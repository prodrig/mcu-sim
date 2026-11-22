// =============================================================================
// jumper_quitable.h — Un Jumper cuyos puentes se quitan y se ponen con la
// simulación en marcha (M12 de doc/analisis_msp430.md)
//
// Un Jumper normal (`quitable="no"`) es el mismo nodo a los dos lados de cada
// puente: el netlist los une antes de construir, como un <hilo>, y no se mueve.
// Con `quitable="si"` el netlist NO los une: cada puente es un contacto entre
// los dos nodos (0,02 ohmios, el de un Interruptor), con un mando `A-B` de
// tipo interruptor que la ventana pinta como una casilla. Es el bloque de
// aislamiento de la eZ-FET de las LaunchPad (J101, J13...): quitar SBW RST y
// SBW TST aísla el chip de la sonda, quitar 3V3 lo deja sin alimentar desde
// el USB, quitar RXD/TXD corta el backchannel [SLAU772A §2.2.3].
//
// El dibujo sigue enseñando los puentes del XML (`J101@1-2+3-4...`): es la
// placa como sale de fábrica.
// =============================================================================
#ifndef STM32_PARTS_JUMPER_QUITABLE_H
#define STM32_PARTS_JUMPER_QUITABLE_H

#include <systemc>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "maquina.h"

namespace stm32 {

SC_MODULE(JumperQuitable), public ExtPartBase {
    struct Puente { std::string a, b; analog_net_if* na; analog_net_if* nb; };
    JumperQuitable(sc_core::sc_module_name nm, unsigned filas, unsigned columnas, bool zigzag,
                   const std::vector<std::pair<std::string, analog_net_if*>>& pines,
                   const std::vector<Puente>& puentes, double r = 0.02)
        : sc_core::sc_module(nm), ExtPartBase("Jumper", nm), filas_(filas), columnas_(columnas),
          zigzag_(zigzag), r_(r) {
        for (const Puente& p : puentes) {
            if (!p.na || !p.nb) continue;
            const int ia = add_pin(p.a, *p.na, "jumper");
            const int ib = add_pin(p.b, *p.nb, "jumper");
            marca_paso(p.a); marca_paso(p.b);
            c_.emplace_back(new Contacto(*p.na, ia, *p.nb, ib));
            nombres_.push_back(p.a + "-" + p.b);
            puesto_.push_back(true);
        }
        for (const auto& p : pines) {
            if (!p.second || busca_pin(p.first)) continue;
            add_ref(p.first, *p.second);
            marca_paso(p.first);
        }
        SC_HAS_PROCESS(JumperQuitable);
        SC_THREAD(vigila);
    }
    ~JumperQuitable() override { for (auto& c : c_) c->suelta(); }

    unsigned filas() const    { return filas_; }
    unsigned columnas() const { return columnas_; }
    unsigned n_pines() const  { return filas_ * columnas_; }
    bool     zigzag() const   { return zigzag_; }
    unsigned n_puentes() const { return unsigned(c_.size()); }
    bool puesto(unsigned k) const { return k < puesto_.size() && puesto_[k]; }
    void pon(unsigned k, bool si) {
        if (k >= puesto_.size() || puesto_[k] == si) return;
        puesto_[k] = si;
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    unsigned n_mandos() const override { return n_puentes(); }
    Mando mando(unsigned k) const override { return {nombres_[k].c_str(), Mando::Interruptor, 0.f, 1.f}; }
    float valor_mando(unsigned k) const override { return puesto(k) ? 1.f : 0.f; }
    void acciona(unsigned k, float v) override { pon(k, v >= 0.5f); }

private:
    bool busca_pin(const std::string& n) const {
        for (const std::string& x : nombres_)
            if (x.substr(0, x.find('-')) == n || x.substr(x.find('-') + 1) == n) return true;
        return false;
    }
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= evento_conexion();
        l |= ev_;
        for (auto& c : c_) c->eventos(l);
        for (;;) {
            for (size_t k = 0; k < c_.size(); ++k) c_[k]->pon(conectada_ && puesto_[k], r_);
            wait(l);
        }
    }
    unsigned filas_, columnas_;
    bool zigzag_;
    double r_;
    std::vector<std::unique_ptr<Contacto>> c_;
    std::vector<std::string> nombres_;
    std::vector<bool> puesto_;
    sc_core::sc_event ev_;
};

} // namespace stm32
#endif
