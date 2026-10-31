// =============================================================================
// bus_potencia.h — El bus de potencia de una máquina de 48 V: su condensador,
//                  la resistencia de frenado y los optoacopladores
//
// (Fase F5 de doc/analisis_cnc.md, §6.6 y §7.)
//
// AL FRENAR UNA MASA, O AL BAJAR UN Z, EL MOTOR GENERA. El driver devuelve esa
// energía al bus (InyectorCorriente, parts/ext_parts.h), y una fuente
// conmutada no la absorbe (Fuente absorbe="no"): se la queda el condensador
// del bus, y su tensión sube. Si nadie la gasta, salta la protección de
// sobretensión del driver -o, en una máquina sin ella, revienta algo-.
//
//   Condensador     el del bus: C, su resistencia serie (ESR) y la tensión que
//                   aguanta. Integra su carga, exacta entre dos cambios
//   ChopperFrenado  la resistencia de frenado con su comparador: por encima de
//                   v_on la pone en el bus, por debajo de v_off la quita
//   Optoacoplador   un LED y un fototransistor que pasa como mucho CTR veces
//                   la corriente del LED: lo que separa un sensor de 24 V de un
//                   pin de 3,3 V, o un MCU de las entradas de 5 V de un driver
// =============================================================================
#ifndef STM32_PARTS_BUS_POTENCIA_H
#define STM32_PARTS_BUS_POTENCIA_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include "ext_parts.h"

namespace stm32 {

// ---------------------------------------------------------------------------
// EL CONDENSADOR DEL BUS
//
// Un Thevenin en su nodo `mas`: la tensión del condensador, sobre la de
// `menos` -la masa-, tras su ESR. Lo que hay en el nodo sin él es otro Thevenin
// {vx, rx}; mientras no cambie, la tensión va hacia vx con τ = (rx + ESR)·C,
// y eso es exacto para cualquier paso. Se pone al día en cada cambio del nodo
// y, mientras le queda camino, cada τ/5 como mucho -y cada milisegundo como
// poco-: los drivers lo miran a ese ritmo. En equilibrio no cuesta nada.
// ---------------------------------------------------------------------------
SC_MODULE(Condensador), public ExtPartBase {
    Condensador(sc_core::sc_module_name nm, analog_net_if& mas, analog_net_if& menos,
                double c_uf, double esr_ohm, double v_max, double v_inicial)
        : sc_core::sc_module(nm), ExtPartBase("Condensador", nm), mas_(&mas), menos_(&menos),
          c_(c_uf * 1e-6), esr_(std::max(0.1, esr_ohm)), v_max_(v_max), vc_(v_inicial) {
        id_ = add_pin("mas", mas, "condensador");
        add_ref("menos", menos);
        SC_HAS_PROCESS(Condensador);
        SC_THREAD(vigila);
    }
    double tension() const { return vc_; }
    double capacidad_uf() const { return c_ * 1e6; }
    double energia_j() const { return 0.5 * c_ * vc_ * vc_; }
    double v_max() const { return v_max_; }
    double v_pico() const { return v_pico_; }
    // La que entra en él ahora, en A (positiva: se carga)
    double corriente() const { return i_; }

    unsigned n_observables() const override { return 4; }
    Observable observable(unsigned i) const override {
        const float vm = float(std::max(1.0, v_max_ * 1.2));
        switch (i) {
            case 0: return {"tension", "V", 0.f, vm, true};
            case 1: return {"corriente", "A", -50.f, 50.f, false};
            case 2: return {"energia", "J", 0.f, float(0.5 * c_ * vm * vm), false};
            default: return {"sobretension", "", 0.f, 1.f, true, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(vc_);
            case 1: return float(i_);
            case 2: return float(energia_j());
            default: return vc_ > v_max_ ? 1.f : 0.f;
        }
    }

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    // Lo de fuera, sin él: su tensión sobre `menos` y la constante de tiempo
    void mira_fuera() {
        bool solo = false;
        const double vx = double(mas_->voltage_excluding(id_, solo)) - double(menos_->voltage());
        const double g = mas_->conductance() - (puesto_ ? 1.0 / double(float(esr_)) : 0.0);
        if (solo || g <= G_FLOAT) {
            libre_ = true;
            i_ = 0.0;
            return;
        }
        libre_ = false;
        vx_ = vx;
        tau_ = (1.0 / g + esr_) * c_;
        i_ = (vx_ - vc_) / (1.0 / g + esr_);
    }
    void avanza() {
        const double t = ahora();
        if (!libre_ && t > t0_) vc_ = vx_ + (vc_ - vx_) * std::exp(-(t - t0_) / tau_);
        t0_ = t;
        v_pico_ = std::max(v_pico_, vc_);
        if (vc_ > v_max_ && !avisado_) {
            avisado_ = true;
            char b[220];
            std::snprintf(b, sizeof b, "%s: %.1f V, y el condensador aguanta %.0f: en una maquina "
                          "de verdad, se hincha o revienta", pieza().c_str(), vc_, v_max_);
            SC_REPORT_WARNING("condensador", b);
        }
    }
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= mas_->value_changed_event();
        l |= menos_->value_changed_event();
        l |= evento_conexion();
        t0_ = ahora();
        for (;;) {
            if (!conectada_) {
                if (puesto_) { mas_->set_hiz(id_); puesto_ = false; }
                libre_ = true;
                wait(l);
                t0_ = ahora();
                continue;
            }
            avanza();
            const double v = vc_ + double(menos_->voltage());
            if (!puesto_ || std::fabs(v - v_puesta_) > 2e-5) {
                v_puesta_ = v;
                mas_->set_drive(id_, float(v), float(esr_));
                puesto_ = true;
            }
            mira_fuera();
            if (libre_ || std::fabs(vx_ - vc_) < 1e-4) {
                wait(l);
            } else {
                const double paso = std::min(1e-3, std::max(1e-6, 0.2 * tau_));
                wait(sc_core::sc_time(paso, sc_core::SC_SEC), l);
            }
        }
    }
    analog_net_if *mas_, *menos_;
    int id_ = -1;
    double c_, esr_, v_max_, vc_;
    double vx_ = 0.0, tau_ = 1.0, t0_ = 0.0, i_ = 0.0, v_puesta_ = 0.0, v_pico_ = 0.0;
    bool libre_ = true, puesto_ = false, avisado_ = false;
};

// ---------------------------------------------------------------------------
// LA RESISTENCIA DE FRENADO, con su comparador: el «chopper de frenado» de un
// controlador o de un módulo aparte. Con el bus por encima de `v_on`, pone `r`
// entre `mas` y `menos` y gasta lo que sobra; por debajo de `v_off`, la quita.
// Lleva la cuenta de lo que quema y avisa si la media pasa de lo que aguanta.
// ---------------------------------------------------------------------------
SC_MODULE(ChopperFrenado), public ExtPartBase {
    ChopperFrenado(sc_core::sc_module_name nm, analog_net_if& mas, analog_net_if& menos,
                   double r_ohm, double v_on, double v_off, double potencia_w)
        : sc_core::sc_module(nm), ExtPartBase("ChopperFrenado", nm), mas_(&mas), menos_(&menos),
          r_(std::max(0.1, r_ohm)), v_on_(v_on), v_off_(std::min(v_off, v_on)),
          p_max_(potencia_w),
          rama_(mas, add_pin("mas", mas, "chopper"), menos, add_pin("menos", menos, "chopper")) {
        SC_HAS_PROCESS(ChopperFrenado);
        SC_THREAD(vigila);
    }
    ~ChopperFrenado() override { rama_.suelta(); }
    bool   conduce() const { return on_; }
    double energia_j() const { return e_; }
    double potencia_w() const { return p_med_; }
    unsigned veces() const { return veces_; }

    unsigned n_observables() const override { return 4; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return {"conduce", "", 0.f, 1.f, true};
            case 1: return {"potencia", "W", 0.f, float(std::max(10.0, 4 * p_max_)), true};
            case 2: return {"energia", "J", 0.f, 1000.f, false};
            default: return {"caliente", "", 0.f, 1.f, true, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return on_ ? 1.f : 0.f;
            case 1: return float(p_med_);
            case 2: return float(e_);
            default: return caliente_ ? 1.f : 0.f;
        }
    }

private:
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= mas_->value_changed_event();
        l |= menos_->value_changed_event();
        l |= evento_conexion();
        double t0 = ahora(), v0 = 0.0;
        for (;;) {
            const double t = ahora(), dt = t - t0;
            // Lo quemado desde la última vez, con la tensión de entonces; la
            // media, con un segundo
            const double p = on_ ? v0 * v0 / r_ : 0.0;
            e_ += p * dt;
            if (dt > 0.0) {
                const double k = std::exp(-dt / 1.0);
                p_med_ = p + (p_med_ - p) * k;
            }
            t0 = t;
            const double v = conectada_ ? double(mas_->voltage()) - double(menos_->voltage()) : 0.0;
            v0 = v;
            const bool on = conectada_ && (on_ ? v > v_off_ : v > v_on_);
            if (on != on_) {
                on_ = on;
                if (on) { ++veces_; rama_.resuelve(true, r_); }
                else rama_.suelta();
            }
            if (p_med_ > p_max_ && !caliente_) {
                caliente_ = true;
                char b[200];
                std::snprintf(b, sizeof b, "%s: quema %.0f W de media, y la resistencia aguanta "
                              "%.0f", pieza().c_str(), p_med_, p_max_);
                SC_REPORT_WARNING("chopper", b);
            }
            // Mientras quema, cada milisegundo, para la cuenta de lo quemado
            if (on_) wait(sc_core::sc_time(1.0, sc_core::SC_MS), l);
            else wait(l);
        }
    }
    analog_net_if *mas_, *menos_;
    double r_, v_on_, v_off_, p_max_;
    RamaDosNodos rama_;
    bool on_ = false, caliente_ = false;
    double e_ = 0.0, p_med_ = 0.0;
    unsigned veces_ = 0;
};

// ---------------------------------------------------------------------------
// EL OPTOACOPLADOR: un LED entre `anodo` y `catodo` -conduce por encima de
// `vf`, con `r_led` en serie- y un fototransistor entre `colector` y `emisor`
// que deja pasar como mucho CTR veces la corriente del LED. Si lo de fuera
// pide menos, se satura y se queda en `vce_sat`; si pide más, pasa solo eso y
// el colector se queda arriba. Un PC817: CTR de 0,5 a 6, 1 típico.
// ---------------------------------------------------------------------------
SC_MODULE(Optoacoplador), public ExtPartBase {
    Optoacoplador(sc_core::sc_module_name nm, analog_net_if& an, analog_net_if& ca,
                  analog_net_if& co, analog_net_if& em, double vf, double r_led, double ctr,
                  double vce_sat, double if_max_ma)
        : sc_core::sc_module(nm), ExtPartBase("Optoacoplador", nm),
          an_(&an), ca_(&ca), co_(&co), em_(&em), vf_(vf), r_led_(std::max(0.1, r_led)),
          ctr_(ctr), vce_sat_(vce_sat), if_max_(if_max_ma * 1e-3),
          led_(an, add_pin("anodo", an, "opto"), ca, add_pin("catodo", ca, "opto")),
          sal_(co, add_pin("colector", co, "opto"), em, add_pin("emisor", em, "opto")) {
        SC_HAS_PROCESS(Optoacoplador);
        SC_THREAD(vigila);
    }
    ~Optoacoplador() override { led_.suelta(); sal_.suelta(); }
    double led_ma() const { return if_ * 1e3; }
    double salida_ma() const { return cerrada_ ? sal_.corriente() * 1e3 : 0.0; }
    bool   saturado() const { return saturado_; }

    unsigned n_observables() const override { return 3; }
    Observable observable(unsigned i) const override {
        switch (i) {
            case 0: return {"led", "mA", 0.f, float(if_max_ * 1e3), true};
            case 1: return {"salida", "mA", 0.f, 50.f, true};
            default: return {"saturado", "", 0.f, 1.f, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(led_ma());
            case 1: return float(salida_ma());
            default: return saturado_ ? 1.f : 0.f;
        }
    }

private:
    void vigila() {
        sc_core::sc_event_or_list l;
        l |= an_->value_changed_event();
        l |= ca_->value_changed_event();
        l |= co_->value_changed_event();
        l |= em_->value_changed_event();
        l |= evento_conexion();
        for (;;) {
            const bool luz = led_.resuelve_diodo(conectada_, vf_, r_led_);
            if_ = luz ? std::max(0.0, led_.corriente()) : 0.0;
            if (if_ > if_max_ && !avisado_) {
                avisado_ = true;
                char b[200];
                std::snprintf(b, sizeof b, "%s: %.0f mA por el LED, y aguanta %.0f: falta "
                              "resistencia", pieza().c_str(), if_ * 1e3, if_max_ * 1e3);
                SC_REPORT_WARNING("opto", b);
            }
            salida();
            wait(l);
        }
    }
    // La salida: saturada si lo de fuera pide menos de CTR·If; si no, solo eso
    void salida() {
        const double i_max = ctr_ * if_;
        double vc0 = 0.0, ve0 = 0.0, rf = 0.0;
        if (!conectada_ || i_max < 1e-6 || !sal_.fuera(vc0, ve0, rf) || vc0 - ve0 <= 0.0) {
            if (cerrada_) { sal_.suelta(); cerrada_ = false; }
            saturado_ = false;
            return;
        }
        const double dv = vc0 - ve0;
        const double i_sat = (dv - vce_sat_) / rf;
        double r;
        if (i_sat <= i_max) {
            r = vce_sat_ * rf / std::max(dv - vce_sat_, 1e-3);
            saturado_ = true;
        } else {
            r = dv / i_max - rf;
            saturado_ = false;
        }
        r = std::max(0.1, r);
        if (cerrada_ && std::fabs(r - r_) < 1e-3 * r_) return;
        r_ = r;
        cerrada_ = true;
        sal_.resuelve(true, r);
    }
    analog_net_if *an_, *ca_, *co_, *em_;
    double vf_, r_led_, ctr_, vce_sat_, if_max_;
    RamaDosNodos led_, sal_;
    double if_ = 0.0, r_ = 0.0;
    bool cerrada_ = false, saturado_ = false, avisado_ = false;
};

} // namespace stm32

#endif // STM32_PARTS_BUS_POTENCIA_H
