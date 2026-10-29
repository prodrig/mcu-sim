// =============================================================================
// controlador_motor.h — Un CONTROLADOR DE MOTOR inteligente, de reloj y
//                       dirección en lazo cerrado, con salida de freno
//
// (Fase F1c de doc/analisis_cnc.md, §5.5, la primera capa.)
//
// Los controladores de Nanotec -el C5-E y sus parientes- mueven un BLDC (o un
// paso a paso) a partir de su encoder o de sus Hall. Desde fuera, con su modo
// de RELOJ Y DIRECCIÓN, son un driver de pasos más: cada flanco de subida de
// STEP es un paso hacia donde diga DIR, y el firmware de la CNC no cambia. Por
// dentro, una CASCADA DE LAZOS sobre la posición que miden:
//
//     θ*   = pasos · 2π / pasos_vuelta                  (la consigna, en el motor)
//     ω*   = kp_pos·(θ* − θ) + dθ*/dt                    (posición, con prealimentación)
//     i_q* = kp_vel·(ω* − ω) + ki_vel·∫(ω* − ω)          (velocidad, PI)
//
// cada `lazo_us` -1 ms-, y el de corriente es el del motor: la CORRIENTE
// IMPUESTA CON TOPE DE TENSIÓN (parts/motor_bldc.h), con el tope en la tensión
// de alimentación y la consigna limitada a `corriente_max`. Sus salidas U, V y
// W se declaran en los nodos y el motor comprueba que vaya cada una a la suya.
//
// LA POSICIÓN, de los Hall (6·p cambios por vuelta: 18 en un motor de tres
// pares de polos, 20° cada uno) o del encoder (4·ppr), y la VELOCIDAD, del
// tiempo entre cambios (el método M/T: con muchos cambios por periodo, cuántos;
// con pocos, cuánto tardan, y entre uno y otro, como mucho lo que da no haber
// visto el siguiente). Entre dos cambios la posición se INTERPOLA con la
// velocidad, sin salir del tramo. Con la consigna quieta y el error dentro de
// medio tramo, el lazo de posición no empuja: un lazo sobre una medida a saltos
// oscilaría entre dos tramos.
//
// LO QUE LE DISTINGUE DE UN PASO A PASO SIN LAZO: NO PIERDE PASOS SIN
// ENTERARSE. Cada paso cuenta. Si el motor no puede seguir -una carga
// demasiado grande, una rampa demasiado corta-, el ERROR DE SEGUIMIENTO crece;
// mientras no pase de `error_max_grados` durante `error_ms`, lo recupera; si
// pasa, SE PARA, LO DICE -un aviso y la salida de error- y deja de seguir pasos
// hasta que se le quite y se le vuelva a dar EN.
//
// EL FRENO. La salida `freno` alimenta un freno de seguridad -el de un eje Z,
// que sin corriente cae-: con EN, primero corriente en el motor, después se
// suelta el freno -la salida a VCC- y, pasados `freno_ms`, ya en marcha. Al
// quitar EN o al fallar, al revés: se echa el freno y, pasados `freno_ms`, se
// quita la corriente. Así el eje no cae en ningún momento. (El freno mecánico
// es de la fase F5: aquí solo la salida.)
//
// Lo que NO hace: el bus de campo (F6), los finales de carrera, la puesta en
// marcha automática que mide el motor, el I²t de la corriente de pico y la
// energía que vuelve al frenar (F5). Sus ganancias de omisión son las de un
// DB59L024035-A suelto: con una carga de mucha inercia hay que subirlas.
// =============================================================================
#ifndef STM32_PARTS_CONTROLADOR_MOTOR_H
#define STM32_PARTS_CONTROLADOR_MOTOR_H

#include <systemc>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include "ext_parts.h"

namespace stm32 {

struct ConfigControladorMotor {
    std::string nombre = "tipo C5-E";   // para los mensajes
    double vcc_min = 12.0, vcc_max = 48.0;   // V: el margen
    double vcc_apaga = 10.0;                 // V: por debajo, se apaga
    double corriente_max = 10.0;             // A: el tope de la consigna del eje q
    unsigned pasos_vuelta = 2000;            // pasos de STEP por vuelta del motor
    bool   encoder = false;                  // realimentación: el encoder o los Hall
    unsigned pares_polos = 3;                // los del motor, para contar los Hall
    unsigned ppr = 1000;                     // las líneas del encoder
    double kp_pos = 20.0;                    // 1/s
    double kp_vel = 0.015;                   // A por rad/s
    double ki_vel = 1.0;                     // A por rad
    double rpm_max = 4000.0;
    double error_max_grados = 360.0;         // en el motor
    double error_ms = 10.0;
    double freno_ms = 100.0;
    double lazo_us = 1000.0;
    double umbral = 2.5;                     // V: las entradas (con 0,25 de histéresis)
    double rds = 0.03;                       // ohm: lo que se queda en el puente (estimado)
    double i_reposo = 0.08;                  // A de VCC en reposo (estimado)
};

SC_MODULE(ControladorMotor), public ExtPartBase {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr float R_PULLUP = 4700.0f, R_ENTRADA = 10000.0f;
    static constexpr double V_SENSORES = 5.0;

    ControladorMotor(sc_core::sc_module_name nm, const ConfigControladorMotor& c,
                     analog_net_if& vcc, analog_net_if& gnd, analog_net_if& u, analog_net_if& v,
                     analog_net_if& w, analog_net_if& step, analog_net_if& dir, analog_net_if& en,
                     analog_net_if* v5, analog_net_if* h1, analog_net_if* h2, analog_net_if* h3,
                     analog_net_if* enc_a, analog_net_if* enc_b, analog_net_if* freno,
                     analog_net_if* error)
        : sc_core::sc_module(nm), ExtPartBase("ControladorMotor", nm), c_(c), vcc_(&vcc),
          gnd_(&gnd), v5_(v5), freno_(freno), error_(error) {
        const int ivc = add_pin("vcc", vcc, "controlador");
        const int ig = add_pin("gnd", gnd, "controlador");
        carga_vcc_.reset(new RamaDosNodos(vcc, ivc, gnd, ig));
        const int iu = add_pin("u", u, "controlador_out");
        const int iv = add_pin("v", v, "controlador_out");
        const int iw = add_pin("w", w, "controlador_out");
        fase_.reset(new RamaCorriente(u, iu, v, iv, pieza().c_str(), 1));
        fase_->pon_tercera(w, iw);
        // Las entradas de mando, con su resistencia a masa por dentro
        auto entrada = [&](Entrada& e, analog_net_if* n, const char* nombre) {
            e.n = n;
            e.nombre = nombre;
            e.id = add_pin(nombre, *n, "controlador_in");
            n->set_drive(e.id, 0.0f, R_ENTRADA);
        };
        entrada(step_, &step, "step");
        entrada(dir_, &dir, "dir");
        entrada(en_, &en, "en");
        // Las de los sensores, con su pull-up a los 5 V de dentro
        analog_net_if* s[5] = {h1, h2, h3, enc_a, enc_b};
        const char* ns[5] = {"h1", "h2", "h3", "enc_a", "enc_b"};
        for (int k = 0; k < 5; ++k)
            if (s[k]) { sen_[k].n = s[k]; sen_[k].nombre = ns[k]; sen_[k].id = add_pin(ns[k], *s[k], "controlador_sensor"); }
        if (v5_) id_v5_ = add_pin("v5", *v5_, "controlador_5v");
        if (freno_) id_freno_ = add_pin("freno", *freno_, "controlador_freno");
        if (error_) id_error_ = add_pin("error", *error_, "controlador_error");
        delta_ = c_.encoder ? 2.0 * PI / (4.0 * c_.ppr) : 2.0 * PI / (6.0 * c_.pares_polos);
        lazo_ = c_.lazo_us * 1e-6;
        SC_HAS_PROCESS(ControladorMotor);
        SC_METHOD(vigila);
    }
    ~ControladorMotor() override {
        carga_vcc_->suelta();
        fase_->suelta();
    }

    // --- Desde C++ -----------------------------------------------------------
    enum Estado { Apagado, Deshabilitado, Soltando, Marcha, Echando, Error };
    Estado estado() const { return estado_; }
    const char* texto_estado() const {
        switch (estado_) {
            case Apagado:       return "sin tension";
            case Deshabilitado: return "deshabilitado";
            case Soltando:      return "soltando el freno";
            case Marcha:        return "en marcha";
            case Echando:       return "echando el freno";
            default:            return "ERROR";
        }
    }
    long   pasos() const { return pasos_; }
    long   pasos_ignorados() const { return ignorados_; }
    double posicion_grados() const { return th_med_ * 180.0 / PI; }       // la medida
    double consigna_grados() const { return th_ref_ * 180.0 / PI; }
    double error_grados() const { return (th_ref_ - th_med_) * 180.0 / PI; }
    double error_maximo_grados() const { return err_max_visto_ * 180.0 / PI; }
    double rpm_medidas() const { return w_med_ * 60.0 / (2.0 * PI); }
    double corriente_q() const { return fase_->consigna(); }
    double corriente_vcc_ma() const { return i_vcc_ * 1e3; }
    bool   freno_suelto() const { return freno_on_; }
    bool   en_error() const { return estado_ == Error || (estado_ == Echando && fallo_); }
    const std::string& causa() const { return causa_; }
    const ConfigControladorMotor& config() const { return c_; }

    unsigned n_observables() const override { return 8; }
    Observable observable(unsigned i) const override {
        const float im = float(c_.corriente_max);
        switch (i) {
            case 0: return {"estado", "", 0.f, 5.f, false};
            case 1: return {"pasos", "", -1e9f, 1e9f, false};
            case 2: return {"error_seguimiento", "°", -float(c_.error_max_grados),
                            float(c_.error_max_grados), false};
            case 3: return {"corriente_q", "A", -im, im, false};
            case 4: return {"rpm", "rpm", -float(c_.rpm_max), float(c_.rpm_max), false};
            case 5: return {"corriente_vcc", "mA", 0.f, 15000.f, true};
            case 6: return {"freno_suelto", "", 0.f, 1.f, true};
            default: return {"error", "", 0.f, 1.f, true, true};
        }
    }
    float valor_observable(unsigned i) const override {
        switch (i) {
            case 0: return float(estado_);
            case 1: return float(pasos_);
            case 2: return float(error_grados());
            case 3: return float(corriente_q());
            case 4: return float(rpm_medidas());
            case 5: return float(i_vcc_ * 1e3);
            case 6: return freno_on_ ? 1.f : 0.f;
            default: return en_error() ? 1.f : 0.f;
        }
    }

private:
    struct Entrada {
        analog_net_if* n = nullptr;
        const char* nombre = "";
        int id = -1;
        bool nivel = false;
    };
    static double ahora() { return sc_core::sc_time_stamp().to_seconds(); }
    static int signo(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
    double vg() const { return double(gnd_->voltage()); }

    // Una entrada digital, con su histéresis
    bool lee(Entrada& e) {
        const double x = double(e.n->voltage()) - vg();
        if (e.nivel && x < c_.umbral - 0.25) e.nivel = false;
        else if (!e.nivel && x > c_.umbral + 0.25) e.nivel = true;
        return e.nivel;
    }

    void vigila() {
        const double t = ahora();
        const double vcc = conectada_ ? double(vcc_->voltage()) - vg() : 0.0;
        const bool potencia = conectada_ && vcc >= c_.vcc_apaga;
        if (conectada_ && vcc > 0.5 && vcc < c_.vcc_min) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.1f V en VCC, y el controlador %s pide de %.0f a %.0f V%s",
                          pieza().c_str(), vcc, c_.nombre.c_str(), c_.vcc_min, c_.vcc_max,
                          potencia ? "" : ": se apaga");
            avisa("vcc_bajo", b);
        }
        if (vcc > c_.vcc_max * 1.05) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.1f V en VCC, y el controlador %s aguanta %.0f",
                          pieza().c_str(), vcc, c_.nombre.c_str(), c_.vcc_max);
            avisa("vcc_alto", b);
        }
        // Al encenderse, los pull-ups de los sensores todavía no se ven en los
        // nodos: se leen desde la próxima vez
        const bool recien = alimenta(potencia);
        if (!potencia) {
            if (estado_ != Apagado) { estado_ = Apagado; fallo_ = false; }
        } else if (estado_ == Apagado) {
            estado_ = Deshabilitado;
        }
        // Los sensores: los cambios, con su instante
        if (potencia && !recien) sensores(t);
        // Las entradas de mando
        const bool en = potencia && lee(en_);
        const bool dir = potencia && lee(dir_);
        const bool step_antes = step_.nivel;
        const bool step = potencia && lee(step_);
        if (step && !step_antes) paso(dir);
        // EN: habilitar, deshabilitar y quitar el error
        if (potencia) {
            if (en && estado_ == Deshabilitado) habilita(t);
            else if (!en && (estado_ == Soltando || estado_ == Marcha)) deshabilita(t, false, "");
            else if (!en && estado_ == Error) { estado_ = Deshabilitado; fallo_ = false; }
        }
        // El lazo, cada lazo_us
        while (activo() && t >= t_lazo_ - 1e-12) {
            lazo(t_lazo_);
            t_lazo_ += lazo_;
        }
        manda(vcc);
        cuentas(t, vcc);
        salidas(vcc);
        const bool vivo = activo() || fase_->i_carga() != 0.0;
        if (activo()) next_trigger(sc_core::sc_time(std::max(1e-9, t_lazo_ - t), sc_core::SC_SEC),
                                   despierta());
        else if (vivo) next_trigger(sc_core::sc_time(1e-3, sc_core::SC_SEC), despierta());
        else next_trigger(despierta());
    }
    bool activo() const { return estado_ == Soltando || estado_ == Marcha || estado_ == Echando; }

    // Los 5 V de los sensores y sus pull-ups, solo cuando cambian
    bool alimenta(bool on) {
        if (on == alimentado_) return false;
        alimentado_ = on;
        m_enc_ = s_hall_ = -1;
        t_alimenta_ = ahora();
        const float v = float(vg() + V_SENSORES);
        if (v5_) { if (on) v5_->set_drive(id_v5_, v, 1.0f); else v5_->set_hiz(id_v5_); }
        for (Entrada& s : sen_)
            if (s.n) { if (on) s.n->set_drive(s.id, v, R_PULLUP); else s.n->set_hiz(s.id); }
        return on;
    }

    // Los Hall o el encoder: cuenta los tramos, apunta el instante del último
    // cambio y su frontera
    void sensores(double t) {
        long n = cuenta_;
        bool cambio = false;
        if (c_.encoder) {
            if (!sen_[3].n || !sen_[4].n) return;
            const bool a = lee(sen_[3]), b = lee(sen_[4]);
            const int m = a ? (b ? 2 : 1) : (b ? 3 : 0);       // 00, 10, 11, 01
            // Recién encendidos, los sensores se asientan: se toma lo que digan
            if (m_enc_ < 0 || t - t_alimenta_ < 1e-3) { m_enc_ = m; return; }
            const int d = ((m - m_enc_) % 4 + 4) % 4;
            if (d == 1) { ++n; cambio = true; }
            else if (d == 3) { --n; cambio = true; }
            else if (d == 2) avisa("salto_enc", pieza() + ": el encoder salta dos cuartos de golpe: "
                                   "va demasiado deprisa o le falta un hilo");
            m_enc_ = m;
        } else {
            if (!sen_[0].n || !sen_[1].n || !sen_[2].n) return;
            const unsigned h = (lee(sen_[0]) ? 4u : 0u) | (lee(sen_[1]) ? 2u : 0u) |
                               (lee(sen_[2]) ? 1u : 0u);
            static const int sector[8] = {-1, 5, 3, 4, 1, 0, 2, -1};
            const int s = sector[h];
            hall_ = h;
            if (s < 0) return;                          // se mira en el lazo
            if (s_hall_ < 0 || t - t_alimenta_ < 1e-3) { s_hall_ = s; return; }
            const int d = ((s - s_hall_) % 6 + 6) % 6;
            if (d == 1) { ++n; cambio = true; }
            else if (d == 5) { --n; cambio = true; }
            else if (d != 0) avisa("salto_hall", pieza() + ": los Hall saltan de un sector a otro "
                                   "que no es el de al lado: ¿estan cruzados?");
            s_hall_ = s;
        }
        if (!cambio) return;
        const bool sube = n > cuenta_;
        cuenta_ = n;
        th_borde_ = double(sube ? n : n + 1) * delta_;
        t_borde_ = t;
        ++cambios_;
    }

    // Un flanco de STEP
    void paso(bool dir) {
        if (estado_ == Soltando || estado_ == Marcha) {
            pasos_ += dir ? 1 : -1;
            if (estado_ == Soltando)
                avisa("pasos_soltando", pieza() + ": llegan pasos antes de que se suelte el freno "
                      "(EN hace " + std::to_string(int(c_.freno_ms)) + " ms que hay que esperar): "
                      "se cuentan, y el motor los sigue");
            return;
        }
        ++ignorados_;
        avisa(std::string("pasos_") + texto_estado(), pieza() + ": llegan pasos con el controlador " +
              texto_estado() + ": no se siguen");
    }

    // La posición medida, interpolada dentro del tramo, y la velocidad
    void mide(double t) {
        // M/T: con cambios desde la última vez, la pendiente entre fronteras
        if (cambios_ != cambios_ref_) {
            if (t_ref_ >= 0.0 && t_borde_ > t_ref_) w_med_ = (th_borde_ - th_ref_borde_) / (t_borde_ - t_ref_);
            else w_med_ = 0.0;
            t_ref_ = t_borde_;
            th_ref_borde_ = th_borde_;
            cambios_ref_ = cambios_;
        } else if (t_ref_ >= 0.0) {
            // Sin cambios: como mucho, lo que da no haber visto el siguiente
            const double dt = t - t_borde_;
            if (dt > 0.2) w_med_ = 0.0;
            else if (dt > 0.0 && std::fabs(w_med_) * dt > delta_) w_med_ = signo(w_med_) * delta_ / dt;
        }
        const double lo = double(cuenta_) * delta_, hi = lo + delta_;
        const double base = t_ref_ >= 0.0 ? th_borde_ : lo + 0.5 * delta_;
        th_med_ = std::max(lo, std::min(hi, base + w_med_ * (t - t_borde_)));
    }

    void habilita(double t) {
        estado_ = Soltando;
        fallo_ = false;
        causa_.clear();
        t_estado_ = t;
        t_lazo_ = t;
        mide(t);
        th_base_ = th_med_;
        pasos_base_ = pasos_;
        th_ref_ = th_ant_ = th_med_;
        integral_ = 0.0;
        i_ = 0.0;
        t_error_ = -1.0;
        freno_on_ = false;           // primero corriente; el freno, en la próxima vuelta
    }
    void deshabilita(double t, bool fallo, const std::string& causa) {
        estado_ = Echando;
        fallo_ = fallo;
        if (fallo) causa_ = causa;
        t_estado_ = t;
        freno_on_ = false;           // se echa el freno; la corriente sigue freno_ms
    }

    void lazo(double t) {
        mide(t);
        // El estado: el freno, sus tiempos. Con la corriente ya puesta, se suelta;
        // pasados freno_ms, en marcha
        if (estado_ == Soltando && t > t_estado_) freno_on_ = true;
        if (estado_ == Soltando && t - t_estado_ >= c_.freno_ms * 1e-3 - 1e-12) estado_ = Marcha;
        if (estado_ == Echando && t - t_estado_ >= c_.freno_ms * 1e-3 - 1e-12) {
            estado_ = fallo_ ? Error : Deshabilitado;
            i_ = 0.0;
            integral_ = 0.0;
            return;
        }
        // Los Hall, que tienen que tener sentido: un código imposible que dura
        // un milisegundo (al arrancar, los sensores tardan unos ciclos delta)
        const bool hall_mal = !c_.encoder && (hall_ == 0 || hall_ == 7);
        if (!hall_mal) t_hall_mal_ = -1.0;
        else if (t_hall_mal_ < 0.0) t_hall_mal_ = t;
        if (hall_mal && t - t_hall_mal_ >= 1e-3 - 1e-12 && estado_ != Echando) {
            char b[260];
            std::snprintf(b, sizeof b, "%s: los Hall dicen %u%u%u, que no existe: un hilo suelto o "
                          "sin alimentacion. Se para", pieza().c_str(), (hall_ >> 2) & 1,
                          (hall_ >> 1) & 1, hall_ & 1);
            avisa("hall", b);
            deshabilita(t, true, "Hall");
        }
        // La consigna: cada paso cuenta
        if (estado_ != Echando)
            th_ref_ = th_base_ + double(pasos_ - pasos_base_) * 2.0 * PI / double(c_.pasos_vuelta);
        const double w_ff = (th_ref_ - th_ant_) / lazo_;
        th_ant_ = th_ref_;
        const double e = th_ref_ - th_med_;
        err_max_visto_ = std::max(err_max_visto_, std::fabs(e));
        // El error de seguimiento
        if (estado_ != Echando) {
            if (std::fabs(e) > c_.error_max_grados * PI / 180.0) {
                if (t_error_ < 0.0) t_error_ = t;
                if (t - t_error_ >= c_.error_ms * 1e-3 - 1e-12) {
                    char b[300];
                    std::snprintf(b, sizeof b, "%s: error de seguimiento: el motor va %.0f° %s de "
                                  "la consigna (el limite es %.0f° durante %.0f ms). Se para, "
                                  "echa el freno y lo dice por su salida de error", pieza().c_str(),
                                  std::fabs(e) * 180.0 / PI, e > 0 ? "por detras" : "por delante",
                                  c_.error_max_grados, c_.error_ms);
                    avisa("seguimiento", b);
                    deshabilita(t, true, "seguimiento");
                }
            } else {
                t_error_ = -1.0;
            }
        }
        // Posición -> velocidad -> corriente
        const double e_c = (w_ff == 0.0 && std::fabs(e) < 0.5 * delta_) ? 0.0 : e;
        const double w_max = c_.rpm_max * 2.0 * PI / 60.0;
        const double w_ref = std::max(-w_max, std::min(w_max, c_.kp_pos * e_c + w_ff));
        const double ev = w_ref - w_med_;
        const double integral = integral_ + c_.ki_vel * ev * lazo_;
        double i = c_.kp_vel * ev + integral;
        const double im = c_.corriente_max;
        if (i > im) i = im;
        else if (i < -im) i = -im;
        else integral_ = integral;           // sin acumular en el tope
        i_ = i;
    }

    void manda(double vcc) {
        fase_->manda(activo() ? i_ : 0.0, std::max(0.0, vcc), activo());
        fase_->muestra(vg());
    }

    // Lo que gasta de VCC: lo que entra en el motor, lo que se queda en el
    // puente y el reposo, de media
    void cuentas(double t, double vcc) {
        if (fase_->t_carga() > t_cuentas_ + 1e-12) {
            const double dt = fase_->t_carga() - t_cuentas_;
            pc_ = (fase_->energia() - e_ant_) / dt;
            pt_ = (fase_->i2s() - q_ant_) / dt * c_.rds;
            t_cuentas_ = fase_->t_carga();
            e_ant_ = fase_->energia();
            q_ant_ = fase_->i2s();
        } else if (!activo() && fase_->i_carga() == 0.0) {
            pc_ = pt_ = 0.0;
        }
        const double iq = estado_ != Apagado ? c_.i_reposo : 0.0;
        const double i_ahora = vcc > 0.5 ? std::max(0.0, pc_ + pt_) / vcc + iq : 0.0;
        i_vcc_ = i_ahora + (i_vcc_ - i_ahora) * std::exp(-(t - t_ivcc_) / 5e-3);
        t_ivcc_ = t;
        if (!conectada_ || vcc < 0.05 || i_vcc_ <= 1e-6) {
            if (r_vcc_ != 0.0) { carga_vcc_->suelta(); r_vcc_ = 0.0; }
            return;
        }
        const double r = vcc / i_vcc_;
        if (r_vcc_ != 0.0 && std::fabs(r - r_vcc_) < 1e-3 * r_vcc_) return;
        r_vcc_ = r;
        carga_vcc_->resuelve(true, r);
    }

    // El freno -a VCC, suelto- y el error -a masa, con error-
    void salidas(double vcc) {
        if (freno_) {
            const int f = (freno_on_ && activo()) ? 1 : 0;
            if (f != freno_puesto_ || (f && std::fabs(vcc - v_freno_) > 0.05)) {
                freno_puesto_ = f;
                v_freno_ = vcc;
                if (f) freno_->set_drive(id_freno_, float(vg() + vcc), 1.0f);
                else freno_->set_hiz(id_freno_);
            }
            if (!activo()) freno_on_ = false;
        }
        if (error_) {
            const int e = en_error() ? 1 : 0;
            if (e != error_puesto_) {
                error_puesto_ = e;
                if (e) error_->set_drive(id_error_, float(vg()), 10.0f);
                else error_->set_hiz(id_error_);
            }
        }
    }

    const sc_core::sc_event_or_list& despierta() {
        if (!despierta_) {
            despierta_.reset(new sc_core::sc_event_or_list());
            *despierta_ |= vcc_->value_changed_event();
            *despierta_ |= gnd_->value_changed_event();
            for (Entrada* e : {&step_, &dir_, &en_}) *despierta_ |= e->n->value_changed_event();
            for (const Entrada& s : sen_) if (s.n) *despierta_ |= s.n->value_changed_event();
            *despierta_ |= evento_conexion();
        }
        return *despierta_;
    }
    void avisa(const std::string& que, const std::string& texto) {
        if (!dichos_.insert(que).second) return;
        SC_REPORT_WARNING("controlador_motor", texto.c_str());
    }

    ConfigControladorMotor c_;
    analog_net_if *vcc_, *gnd_, *v5_, *freno_, *error_;
    int id_v5_ = -1, id_freno_ = -1, id_error_ = -1;
    std::unique_ptr<RamaCorriente> fase_;
    std::unique_ptr<RamaDosNodos> carga_vcc_;
    Entrada step_, dir_, en_;
    Entrada sen_[5];                   // H1, H2, H3, A y B
    bool alimentado_ = false;
    double t_alimenta_ = 0.0;
    // El estado
    Estado estado_ = Apagado;
    bool fallo_ = false;
    std::string causa_;
    double t_estado_ = 0.0, t_lazo_ = 0.0, lazo_ = 1e-3;
    // Los pasos
    long pasos_ = 0, pasos_base_ = 0, ignorados_ = 0;
    // La medida
    double delta_ = 1.0;
    long cuenta_ = 0;
    int m_enc_ = -1, s_hall_ = -1;
    unsigned hall_ = 0;
    unsigned long cambios_ = 0, cambios_ref_ = 0;
    double th_borde_ = 0.0, t_borde_ = 0.0, t_ref_ = -1.0, th_ref_borde_ = 0.0;
    double th_med_ = 0.0, w_med_ = 0.0;
    // Los lazos
    double th_base_ = 0.0, th_ref_ = 0.0, th_ant_ = 0.0, integral_ = 0.0, i_ = 0.0;
    double t_error_ = -1.0, err_max_visto_ = 0.0, t_hall_mal_ = -1.0;
    // Lo que gasta
    double t_cuentas_ = 0.0, e_ant_ = 0.0, q_ant_ = 0.0, pc_ = 0.0, pt_ = 0.0, i_vcc_ = 0.0,
           t_ivcc_ = 0.0, r_vcc_ = 0.0;
    // Las salidas
    bool freno_on_ = false;
    int freno_puesto_ = -1, error_puesto_ = -1;
    double v_freno_ = 0.0;
    std::unique_ptr<sc_core::sc_event_or_list> despierta_;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_CONTROLADOR_MOTOR_H
