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
// es `Freno`, parts/transmision.h.)
//
// LA ENERGÍA QUE VUELVE (fase F5). Cuando el motor frena -o el peso de un Z lo
// arrastra hacia abajo-, lo que entra en él es negativo: el controlador lo
// DEVUELVE a VCC (InyectorCorriente). Con una fuente que no lo absorbe, lo
// recoge el condensador del bus y la tensión sube; por encima de
// `v_sobretension` -un 10 % sobre `vcc_max`- SALTA LA PROTECCIÓN: corta el
// puente en el acto, echa el freno y lo dice, como un error más.
//
// EL BUS DE CAMPO (fase F6). Con `mando="canopen"` no hay STEP, DIR ni EN: se
// le manda por CANopen, por su pin `can` -lleva el transceptor dentro-, como a
// un C5-E con su bus. Es un esclavo CiA 301 (parts/canopen.h: NMT, SDO, PDO,
// SYNC, heartbeat y EMCY) con un subconjunto de CiA 402, el perfil de los
// accionamientos:
//
//   LA MÁQUINA DE ESTADOS del accionamiento, por la palabra de control
//   (0x6040) y dicha en la de estado (0x6041): Switch on disabled → Ready to
//   switch on → Switched on → Operation enabled, con la parada rápida, el
//   fallo y su reinicio. Operation enabled es lo que con EN era habilitar:
//   corriente, se suelta el freno; al salir, se echa y se quita.
//   LOS MODOS (0x6060): el PERFIL DE POSICIÓN (1) -una consigna con su
//   velocidad, su aceleración y su frenada, absoluta o relativa, en el acto o
//   tras la que va, con el saludo de los bits 4 y 12-, el PERFIL DE VELOCIDAD
//   (3) y el HOMING (6): con los métodos 17 y 18 -el final de carrera de abajo
//   o el de arriba: hacia él deprisa, de vuelta despacio, y el cero donde se
//   suelta- y el 35 y 37, el cero donde está.
//   LOS FINALES DE CARRERA, `fin_neg` y `fin_pos`, con pull-up a los 5 V de
//   dentro y activos a nivel bajo: el homing los usa, y fuera de él paran el
//   motor si se va hacia uno pisado (el bit 11 de la palabra de estado).
//   LOS FALLOS van por EMCY: el error de seguimiento (8611), la sobretensión
//   (3210) y los Hall (7300), y el 0000 al quitarlos.
//
// Las posiciones van en PASOS -pasos_vuelta por vuelta del motor-, las
// velocidades en pasos/s y las aceleraciones en pasos/s².
//
// Lo que NO hace: la puesta en marcha automática que mide el motor, el I²t de
// la corriente de pico, los modos síncronos de CiA 402 (CSP, CSV) y el mapeo
// variable de los PDO. Sus ganancias de omisión son las de un DB59L024035-A
// suelto: con una carga de mucha inercia hay que subirlas.
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
#include "canopen.h"

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
    // La protección de sobretensión (F5): por encima, corta el puente en el
    // acto, echa el freno y lo dice. 0: un 10 % por encima de vcc_max
    double v_sobretension = 0.0;
    double sobretension() const { return v_sobretension > 0.0 ? v_sobretension : vcc_max * 1.1; }
    // El bus de campo (F6): por CANopen en vez de por pasos
    bool canopen = false;
    unsigned nodo = 1;                       // 1..127
    double can_kbps = 500.0;
    bool terminador = false;                 // 120 ohm en el hilo
    double arranque_ms = 100.0;              // de la tensión al boot-up
};

SC_MODULE(ControladorMotor), public ExtPartBase {
    static constexpr double PI = 3.14159265358979323846;
    static constexpr float R_PULLUP = 4700.0f, R_ENTRADA = 10000.0f;
    static constexpr double V_SENSORES = 5.0;

    ControladorMotor(sc_core::sc_module_name nm, const ConfigControladorMotor& c,
                     analog_net_if& vcc, analog_net_if& gnd, analog_net_if& u, analog_net_if& v,
                     analog_net_if& w, analog_net_if* step, analog_net_if* dir, analog_net_if* en,
                     analog_net_if* v5, analog_net_if* h1, analog_net_if* h2, analog_net_if* h3,
                     analog_net_if* enc_a, analog_net_if* enc_b, analog_net_if* freno,
                     analog_net_if* error, analog_net_if* can = nullptr,
                     analog_net_if* fin_neg = nullptr, analog_net_if* fin_pos = nullptr)
        : sc_core::sc_module(nm), ExtPartBase("ControladorMotor", nm), c_(c), vcc_(&vcc),
          gnd_(&gnd), v5_(v5), freno_(freno), error_(error), can_(can) {
        const int ivc = add_pin("vcc", vcc, "controlador");
        const int ig = add_pin("gnd", gnd, "controlador");
        carga_vcc_.reset(new RamaDosNodos(vcc, ivc, gnd, ig));
        // Lo que vuelve al bus cuando el motor frena: otro driver en VCC
        iny_vcc_.reset(new InyectorCorriente(vcc, add_drv("vcc", "controlador_bus")));
        const int iu = add_pin("u", u, "controlador_out");
        const int iv = add_pin("v", v, "controlador_out");
        const int iw = add_pin("w", w, "controlador_out");
        fase_.reset(new RamaCorriente(u, iu, v, iv, pieza().c_str(), 1));
        fase_->pon_tercera(w, iw);
        // Las entradas de mando, con su resistencia a masa por dentro
        auto entrada = [&](Entrada& e, analog_net_if* n, const char* nombre) {
            if (!n) return;
            e.n = n;
            e.nombre = nombre;
            e.id = add_pin(nombre, *n, "controlador_in");
            n->set_drive(e.id, 0.0f, R_ENTRADA);
        };
        entrada(step_, step, "step");
        entrada(dir_, dir, "dir");
        entrada(en_, en, "en");
        // Las de los sensores -y los finales de carrera-, con su pull-up a los
        // 5 V de dentro
        analog_net_if* s[7] = {h1, h2, h3, enc_a, enc_b, fin_neg, fin_pos};
        const char* ns[7] = {"h1", "h2", "h3", "enc_a", "enc_b", "fin_neg", "fin_pos"};
        for (int k = 0; k < 7; ++k)
            if (s[k]) { sen_[k].n = s[k]; sen_[k].nombre = ns[k]; sen_[k].id = add_pin(ns[k], *s[k], "controlador_sensor"); }
        // El bus de campo: el transceptor de dentro y, si lo lleva, el terminador
        if (c_.canopen && can_) {
            const int id = add_pin("can", *can_, "controlador_can");
            if (c_.terminador) id_term_ = add_drv("can", "controlador_term");
            puerto_.reset(new PuertoCan("can", *can_, id, c_.can_kbps * 1e3));
            co_.reset(new EsclavoCanOpen(*puerto_, uint8_t(c_.nodo), pieza(), c_.arranque_ms * 1e-3));
            co_->al_cambiar = [this]() { co_cambio_ = true; ev_can_.notify(sc_core::SC_ZERO_TIME); };
            co_->al_reiniciar = [this]() { reinicio_ = true; ev_can_.notify(sc_core::SC_ZERO_TIME); };
            co_->avisa = [this](const std::string& q, const std::string& t) { avisa(q, t); };
            define_402();
            omision_402();
        }
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
        iny_vcc_->suelta();
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
    bool   sobretension() const { return cortado_; }
    unsigned sobretensiones() const { return sobretensiones_; }
    double corriente_devuelta_ma() const { return iny_vcc_->corriente() * 1e3; }
    const ConfigControladorMotor& config() const { return c_; }

    // --- El bus de campo (F6) ------------------------------------------------
    enum Estado402 { NoListo, SwitchOnDisabled, ReadyToSwitchOn, SwitchedOn, OperationEnabled,
                     QuickStopActive, FaultReactionActive, Fault };
    bool canopen() const { return co_ != nullptr; }
    Estado402 estado_402() const { return e402_; }
    static const char* texto_402(Estado402 e) {
        switch (e) {
            case NoListo:             return "Not ready to switch on";
            case SwitchOnDisabled:    return "Switch on disabled";
            case ReadyToSwitchOn:     return "Ready to switch on";
            case SwitchedOn:          return "Switched on";
            case OperationEnabled:    return "Operation enabled";
            case QuickStopActive:     return "Quick stop active";
            case FaultReactionActive: return "Fault reaction active";
            default:                  return "Fault";
        }
    }
    uint16_t statusword() const {
        uint16_t s = 0;
        switch (e402_) {
            case NoListo:             s = 0x0000; break;
            case SwitchOnDisabled:    s = 0x0040; break;
            case ReadyToSwitchOn:     s = 0x0021; break;
            case SwitchedOn:          s = 0x0023; break;
            case OperationEnabled:    s = 0x0027; break;
            case QuickStopActive:     s = 0x0007; break;
            case FaultReactionActive: s = 0x000F; break;
            case Fault:               s = 0x0008; break;
        }
        if (e402_ != NoListo) s |= 0x0200;                       // remoto
        if (potencia_) s |= 0x0010;                              // con tensión
        if (limite_) s |= 0x0800;                                // un final pisado
        if (e402_ == OperationEnabled || e402_ == QuickStopActive) {
            if (modo_ == 1) {
                if (alcanzado()) s |= 0x0400;
                if (sp_ack_) s |= 0x1000;
            } else if (modo_ == 3) {
                if (halt_ ? v_ref_ == 0.0 : v_ref_ == double(int32_t(vel_obj_))) s |= 0x0400;
                if (std::fabs(w_med_) * c_.pasos_vuelta / (2.0 * PI) < 1.0) s |= 0x1000;
            } else if (modo_ == 6) {
                if ((hm_ok_ || hm_err_) && fase_hm_ == 0 && v_ref_ == 0.0) s |= 0x0400;
                if (hm_ok_) s |= 0x1000;
                if (hm_err_) s |= 0x2000;
            }
        }
        return s;
    }
    uint16_t controlword() const { return cw_; }
    int modo() const { return modo_; }
    unsigned estado_nmt() const { return co_ ? co_->estado() : 0u; }
    long posicion_real() const { return long(std::lround(pos_real())); }
    long objetivo() const { return long(std::lround(obj_)); }
    double velocidad_perfil() const { return v_ref_; }       // pasos/s
    bool homing_hecho() const { return hm_ok_; }
    bool en_limite() const { return limite_; }
    uint16_t codigo_error() const { return codigo_error_; }
    const EsclavoCanOpen* esclavo() const { return co_.get(); }
    const PuertoCan* puerto() const { return puerto_.get(); }

    unsigned n_observables() const override { return co_ ? 12 : 8; }
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
            case 7: return {"error", "", 0.f, 1.f, true, true};
            case 8: return {"statusword", "", 0.f, 65535.f, true};
            case 9: return {"nmt", "", 0.f, 127.f, false};
            case 10: return {"posicion", "", -1e9f, 1e9f, true};
            default: return {"modo", "", -1.f, 10.f, false};
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
            case 7: return en_error() ? 1.f : 0.f;
            case 8: return float(statusword());
            case 9: return float(estado_nmt());
            case 10: return float(posicion_real());
            default: return float(modo_);
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
        if (!e.n) return false;
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
        // Por encima de su margen: una fuente de más tensión de la que toca. Lo
        // que sube el bus al frenar no es eso -lo recoge la resistencia de
        // frenado, o salta la protección, que lo dice ella-: se mira con el
        // puente parado, con la fuente sola
        if (vcc > c_.vcc_max * 1.05 && !activo()) {
            char b[200];
            std::snprintf(b, sizeof b, "%s: %.1f V en VCC, y el controlador %s aguanta %.0f",
                          pieza().c_str(), vcc, c_.nombre.c_str(), c_.vcc_max);
            avisa("vcc_alto", b);
        }
        // LA PROTECCIÓN DE SOBRETENSIÓN: el bus ha subido -lo que devuelve el
        // motor al frenar, con una fuente que no lo absorbe y sin resistencia
        // de frenado-. Corta el puente en el acto, echa el freno y lo dice
        if (activo() && !cortado_ && vcc > c_.sobretension()) {
            cortado_ = true;
            ++sobretensiones_;
            char b[300];
            std::snprintf(b, sizeof b, "%s: SOBRETENSION, %.1f V en VCC (salta a %.1f): corta el "
                          "puente, echa el freno y lo dice por su salida de error. La energia que "
                          "devuelve el motor al frenar no tiene a donde ir: una resistencia de "
                          "frenado", pieza().c_str(), vcc, c_.sobretension());
            SC_REPORT_WARNING("controlador", b);
            deshabilita(t, true, "sobretension");
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
        potencia_ = potencia;
        if (co_) {
            // El bus de campo: la palabra de control, en vez de STEP, DIR y EN
            termina_hilo();
            if (potencia && !co_on_) { co_on_ = true; co_->enciende(t); omision_402(); }
            else if (!potencia && co_on_) { co_on_ = false; co_->apaga(); }
            if (potencia && !recien) finales(t);
            procesa_402(t, potencia);
        } else {
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
        }
        // El lazo, cada lazo_us
        while (activo() && t >= t_lazo_ - 1e-12) {
            lazo(t_lazo_);
            t_lazo_ += lazo_;
        }
        if (co_) {
            procesa_402(t, potencia);            // lo que haya cambiado en el lazo
            if (co_on_) co_->tick(t);
        }
        manda(vcc);
        cuentas(t, vcc);
        salidas(vcc);
        const bool vivo = activo() || fase_->i_carga() != 0.0;
        double dt = -1.0;                        // cuándo volver, si no es por un cambio
        if (activo()) dt = std::max(1e-9, t_lazo_ - t);
        else if (vivo) dt = 1e-3;
        if (co_on_) {
            const double p = co_->proximo(t);
            if (p >= 0.0 && (dt < 0.0 || p - t < dt)) dt = std::max(1e-9, p - t);
        }
        if (dt >= 0.0) next_trigger(sc_core::sc_time(dt, sc_core::SC_SEC), despierta());
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
        cortado_ = false;
        if (co_) {
            // Por CANopen, el perfil arranca parado donde está
            p_ref_ = pos_real();
            v_ref_ = 0.0;
            obj_ = p_ref_;
            hecho_ = true;
            hay_pend_ = false;
            fase_hm_ = 0;
        }
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
        // La consigna: cada paso cuenta; por CANopen, la del perfil
        if (estado_ != Echando) {
            if (co_) {
                perfil(t, lazo_);
                th_ref_ = cero_ + p_ref_ * 2.0 * PI / double(c_.pasos_vuelta);
            } else {
                th_ref_ = th_base_ + double(pasos_ - pasos_base_) * 2.0 * PI / double(c_.pasos_vuelta);
            }
        }
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
        const bool puente = activo() && !cortado_;
        fase_->manda(puente ? i_ : 0.0, std::max(0.0, vcc), puente);
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
        // Con el motor frenando, lo que entra en él es NEGATIVO: vuelve al bus
        // (F5). Hasta F4 se quedaba en nada
        const double i_ahora = vcc > 0.5 ? (pc_ + pt_) / vcc + iq : 0.0;
        i_vcc_ = i_ahora + (i_vcc_ - i_ahora) * std::exp(-(t - t_ivcc_) / 5e-3);
        // Con el puente cortado no vuelve nada, ni de media: en el acto
        if (cortado_ && i_vcc_ < 0.0) i_vcc_ = 0.0;
        t_ivcc_ = t;
        if (conectada_ && vcc >= 0.05 && i_vcc_ < -1e-6) {
            if (r_vcc_ != 0.0) { carga_vcc_->suelta(); r_vcc_ = 0.0; }
            iny_vcc_->pon(-i_vcc_);
            return;
        }
        iny_vcc_->suelta();
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

    // =========================================================================
    // EL BUS DE CAMPO (F6): CiA 402 sobre el esclavo CANopen
    // =========================================================================
    double k_paso() const { return 2.0 * PI / double(c_.pasos_vuelta); }   // rad por paso
    double pos_real() const { return (th_med_ - cero_) / k_paso(); }      // en pasos
    bool alcanzado() const {
        if (halt_) return v_ref_ == 0.0;
        return hecho_ && !hay_pend_ && std::fabs(pos_real() - obj_) <= double(ventana_pos_);
    }

    // El terminador de 120 ohm, si lo lleva: mientras esté en la placa
    void termina_hilo() {
        if (id_term_ < 0) return;
        const int q = conectada_ ? 1 : 0;
        if (q == term_puesto_) return;
        term_puesto_ = q;
        if (q) can_->set_drive(id_term_, 3.3f, 120.0f);
        else can_->set_hiz(id_term_);
    }

    // Los finales de carrera, activos a nivel bajo. El cero del homing, en el
    // instante en que se suelta el suyo
    void finales(double t) {
        const bool n0 = fin_n_, p0 = fin_p_;
        fin_n_ = sen_[5].n && !lee(sen_[5]);
        fin_p_ = sen_[6].n && !lee(sen_[6]);
        if (fase_hm_ == 2 && ((metodo_ == 17 && n0 && !fin_n_) || (metodo_ == 18 && p0 && !fin_p_))) {
            mide(t);
            captura();
            fase_hm_ = 3;
        }
        if (limite_ && !fin_n_ && !fin_p_) limite_ = false;
    }
    // La posición de ahora pasa a valer home_offset; el perfil, con ella
    void captura() {
        const double cero = th_med_ - double(int32_t(home_offset_)) * k_paso();
        const double d = (cero_ - cero) / k_paso();
        p_ref_ += d;
        obj_ += d;
        pend_ += d;
        cero_ = cero;
    }
    void cambia_modo(int m) {
        if (m == modo_) return;
        modo_ = m;
        hecho_ = true;
        hay_pend_ = false;
        obj_ = p_ref_;
        fase_hm_ = 0;
        sp_ack_ = false;
    }
    void empieza_homing(double t) {
        hm_ok_ = hm_err_ = false;
        limite_ = false;
        if (metodo_ == 35 || metodo_ == 37) {
            mide(t);
            captura();
            hm_ok_ = true;
            fase_hm_ = 0;
            return;
        }
        if ((metodo_ == 17 && !sen_[5].n) || (metodo_ == 18 && !sen_[6].n)) {
            hm_err_ = true;
            avisa("homing_sin_final", pieza() + ": homing por el metodo " + std::to_string(metodo_) +
                  ", y el controlador no tiene cableado ese final de carrera");
            return;
        }
        fase_hm_ = 1;
    }

    // Los valores de omisión: al encenderse y al reiniciar el nodo
    void omision_402() {
        const uint32_t pv = c_.pasos_vuelta;
        e402_ = NoListo;
        cw_ = cw_ant_ = 0;
        modo_ = 0;
        obj_607a_ = vel_obj_ = home_offset_ = 0;
        v_perfil_ = pv;                    // una vuelta por segundo
        acel_ = decel_ = 10u * pv;
        d_rapida_ = 50u * pv;
        metodo_ = 35;
        v_busca_ = pv / 2u;
        v_cero_ = std::max(1u, pv / 20u);
        a_home_ = 10u * pv;
        ventana_pos_ = std::max(1u, pv / 100u);
        sp_ack_ = halt_ = hm_ok_ = hm_err_ = limite_ = hay_pend_ = false;
        hecho_ = true;
        fase_hm_ = 0;
        cero_ = th_med_;
        p_ref_ = obj_ = pend_ = 0.0;
        v_ref_ = 0.0;
        codigo_error_ = 0;
    }

    // El diccionario de objetos: lo de CiA 402 que hace, y la identidad
    void define_402() {
        EsclavoCanOpen& o = *co_;
        const uint32_t BIEN = canopen::SdoBien, RANGO = canopen::SdoRango;
        o.define(0x1000, 0, 4, 'r', []() { return 0x00020192u; });     // CiA 402, servo
        o.define(0x1018, 0, 1, 'r', []() { return 4u; });
        o.define(0x1018, 1, 4, 'r', []() { return 0u; });              // no es de nadie
        o.define(0x1018, 2, 4, 'r', []() { return 0u; });
        o.define(0x1018, 3, 4, 'r', []() { return 0x00010000u; });
        o.define(0x1018, 4, 4, 'r', [this]() { return uint32_t(c_.nodo); });
        o.define(0x603F, 0, 2, 'r', [this]() { return uint32_t(codigo_error_); });
        o.define(0x6040, 0, 2, 'w', [this]() { return uint32_t(cw_); },
                 [this, BIEN](uint32_t v) { cw_ = uint16_t(v); return BIEN; });
        o.define(0x6041, 0, 2, 'r', [this]() { return uint32_t(statusword()); });
        o.define(0x605A, 0, 2, 'r', []() { return 2u; });              // parada rápida: a SOD
        o.define(0x6060, 0, 1, 'w', [this]() { return uint32_t(uint8_t(modo_)); },
                 [this, BIEN, RANGO](uint32_t v) {
                     const int m = int(int8_t(uint8_t(v)));
                     if (m != 0 && m != 1 && m != 3 && m != 6) return RANGO;
                     cambia_modo(m);
                     return BIEN;
                 });
        o.define(0x6061, 0, 1, 'r', [this]() { return uint32_t(uint8_t(modo_)); });
        o.define(0x6062, 0, 4, 'r', [this]() { return uint32_t(int32_t(std::lround(p_ref_))); });
        o.define(0x6064, 0, 4, 'r', [this]() { return uint32_t(int32_t(posicion_real())); });
        o.define(0x6065, 0, 4, 'w',
                 [this]() { return uint32_t(std::lround(c_.error_max_grados / 360.0 * c_.pasos_vuelta)); },
                 [this, BIEN, RANGO](uint32_t v) {
                     if (!v) return RANGO;
                     c_.error_max_grados = double(v) * 360.0 / double(c_.pasos_vuelta);
                     return BIEN;
                 });
        o.define(0x6066, 0, 2, 'w', [this]() { return uint32_t(std::lround(c_.error_ms)); },
                 [this, BIEN](uint32_t v) { c_.error_ms = double(v); return BIEN; });
        o.define_valor(0x6067, 0, 4, 'w', &ventana_pos_);
        o.define(0x606C, 0, 4, 'r', [this]() { return uint32_t(int32_t(std::lround(w_med_ / k_paso()))); });
        o.define_valor(0x607A, 0, 4, 'w', &obj_607a_);
        o.define_valor(0x607C, 0, 4, 'w', &home_offset_);
        o.define_valor(0x6081, 0, 4, 'w', &v_perfil_);
        auto positivo = [BIEN, RANGO](uint32_t* x) {
            return [x, BIEN, RANGO](uint32_t v) { if (!v) return RANGO; *x = v; return BIEN; };
        };
        o.define(0x6083, 0, 4, 'w', [this]() { return acel_; }, positivo(&acel_));
        o.define(0x6084, 0, 4, 'w', [this]() { return decel_; }, positivo(&decel_));
        o.define(0x6085, 0, 4, 'w', [this]() { return d_rapida_; }, positivo(&d_rapida_));
        o.define(0x6098, 0, 1, 'w', [this]() { return uint32_t(uint8_t(metodo_)); },
                 [this, BIEN, RANGO](uint32_t v) {
                     const int m = int(int8_t(uint8_t(v)));
                     if (m != 17 && m != 18 && m != 35 && m != 37) return RANGO;
                     metodo_ = m;
                     return BIEN;
                 });
        o.define(0x6099, 0, 1, 'r', []() { return 2u; });
        o.define(0x6099, 1, 4, 'w', [this]() { return v_busca_; }, positivo(&v_busca_));
        o.define(0x6099, 2, 4, 'w', [this]() { return v_cero_; }, positivo(&v_cero_));
        o.define(0x609A, 0, 4, 'w', [this]() { return a_home_; }, positivo(&a_home_));
        o.define_valor(0x60FF, 0, 4, 'w', &vel_obj_);
        o.define(0x6502, 0, 4, 'r', []() { return 0x25u; });          // pp, pv y hm
        // Los PDO, con su mapeo fijo
        o.pdo(true, 0, {{0x604000u, 2}, {0x607A00u, 4}}, 255);
        o.pdo(true, 1, {{0x604000u, 2}, {0x60FF00u, 4}}, 255);
        o.pdo(false, 0, {{0x604100u, 2}, {0x606400u, 4}}, 1);
        o.pdo(false, 1, {{0x604100u, 2}, {0x606C00u, 4}}, 1, false);
    }

    // LA MÁQUINA DE ESTADOS de CiA 402, por la palabra de control
    void procesa_402(double t, bool potencia) {
        if (reinicio_) {
            reinicio_ = false;
            omision_402();
            if (estado_ == Soltando || estado_ == Marcha) deshabilita(t, false, "");
        }
        if (!potencia) { e402_ = NoListo; return; }
        if (e402_ == NoListo) {
            if (co_->estado() == canopen::Inicializando) return;
            e402_ = SwitchOnDisabled;
        }
        // Los fallos: la reacción mientras se echa el freno, y el fallo
        if (en_error()) {
            if (e402_ != FaultReactionActive && e402_ != Fault) {
                e402_ = FaultReactionActive;
                codigo_error_ = causa_ == "seguimiento" ? 0x8611 : causa_ == "sobretension" ? 0x3210
                              : 0x7300;
                co_->emcy(codigo_error_, causa_ == "sobretension" ? 0x05 : 0x21);
                fase_hm_ = 0;
                hay_pend_ = false;
            }
            if (estado_ == Error) e402_ = Fault;
        }
        const bool cambio = co_cambio_;
        co_cambio_ = false;
        const uint16_t cw = cw_;
        const bool reset = (cw & 0x80) && !(cw_ant_ & 0x80);
        const bool sube4 = (cw & 0x10) && !(cw_ant_ & 0x10);
        const bool baja4 = !(cw & 0x10) && (cw_ant_ & 0x10);
        cw_ant_ = cw;
        if (e402_ == Fault) {
            if (reset && estado_ == Error) {
                estado_ = Deshabilitado;
                fallo_ = false;
                causa_.clear();
                codigo_error_ = 0;
                co_->emcy(0x0000, 0x00);
                e402_ = SwitchOnDisabled;
            }
            return;
        }
        if (e402_ == FaultReactionActive) return;
        if (cambio) {
            const bool dv = !(cw & 0x02);
            const bool qs = (cw & 0x02) && !(cw & 0x04);
            const bool apaga = (cw & 0x07) == 0x06;
            const bool enciende = (cw & 0x0F) == 0x07;
            const bool habilita_op = (cw & 0x0F) == 0x0F;
            switch (e402_) {
                case SwitchOnDisabled:
                    if (apaga) e402_ = ReadyToSwitchOn;
                    break;
                case ReadyToSwitchOn:
                    if (dv || qs) e402_ = SwitchOnDisabled;
                    else if (enciende) e402_ = SwitchedOn;
                    else if (habilita_op) e402_ = OperationEnabled;
                    break;
                case SwitchedOn:
                    if (dv || qs) e402_ = SwitchOnDisabled;
                    else if (apaga) e402_ = ReadyToSwitchOn;
                    else if (habilita_op) e402_ = OperationEnabled;
                    break;
                case OperationEnabled:
                    if (dv) e402_ = SwitchOnDisabled;
                    else if (qs) e402_ = QuickStopActive;
                    else if (apaga) e402_ = ReadyToSwitchOn;
                    else if (enciende) e402_ = SwitchedOn;
                    break;
                case QuickStopActive:
                    if (dv) e402_ = SwitchOnDisabled;
                    break;
                default: break;
            }
            halt_ = (cw & 0x100) != 0;
        }
        // Lo de dentro sigue a la máquina de estados: con corriente en
        // Operation enabled y en la parada rápida, sin ella en lo demás
        const bool quiere = e402_ == OperationEnabled || e402_ == QuickStopActive;
        if (quiere && estado_ == Deshabilitado) habilita(t);
        else if (!quiere && (estado_ == Soltando || estado_ == Marcha)) deshabilita(t, false, "");
        // Las consignas, con el bit 4
        if (!cambio || e402_ != OperationEnabled) return;
        if (modo_ == 1) {
            if (sube4) {
                const double v = double(int32_t(obj_607a_));
                const double objetivo = (cw & 0x40) ? obj_ + v : v;
                if ((cw & 0x20) || hecho_) {
                    obj_ = objetivo;
                    hecho_ = false;
                    hay_pend_ = false;
                } else {
                    pend_ = objetivo;
                    hay_pend_ = true;
                }
                sp_ack_ = true;
                limite_ = false;
            } else if (baja4 && !hay_pend_) {
                sp_ack_ = false;
            }
        } else if (modo_ == 6) {
            if (sube4) empieza_homing(t);
            else if (baja4 && fase_hm_ != 0) fase_hm_ = 4;      // se interrumpe: a parar
        }
    }

    // EL PERFIL, cada lazo: la consigna de posición y su velocidad, en pasos
    void perfil(double t, double dt) {
        (void)t;
        const double a = double(std::max(1u, acel_)), d = double(std::max(1u, decel_));
        const double qs = double(std::max(1u, d_rapida_));
        double v_cmd = 0.0, ac = a, de = d;
        if (e402_ == QuickStopActive) {
            de = qs;
        } else if (halt_ || e402_ != OperationEnabled) {
            v_cmd = 0.0;
        } else if (modo_ == 1) {
            if (hecho_ && hay_pend_) {
                obj_ = pend_;
                hay_pend_ = false;
                hecho_ = false;
                if (!(cw_ & 0x10)) sp_ack_ = false;
            }
            if (!hecho_) {
                const double e = obj_ - p_ref_;
                v_cmd = signo(e) * std::min(double(v_perfil_), std::sqrt(2.0 * d * std::fabs(e)));
            }
        } else if (modo_ == 3) {
            v_cmd = double(int32_t(vel_obj_));
        } else if (modo_ == 6) {
            ac = de = double(std::max(1u, a_home_));
            const double s = metodo_ == 18 ? 1.0 : -1.0;     // hacia el final
            if (fase_hm_ == 1) {
                v_cmd = s * double(v_busca_);
                if ((metodo_ == 17 && fin_n_) || (metodo_ == 18 && fin_p_)) fase_hm_ = 2;
            }
            if (fase_hm_ == 2) v_cmd = -s * double(v_cero_);
        }
        // Los finales de carrera, fuera del homing: no se va hacia uno pisado
        const bool buscando = modo_ == 6 && (fase_hm_ == 1 || fase_hm_ == 2);
        if (!buscando && ((fin_n_ && (v_cmd < 0.0 || v_ref_ < 0.0)) ||
                          (fin_p_ && (v_cmd > 0.0 || v_ref_ > 0.0)))) {
            if (!limite_)
                avisa("limite", pieza() + ": va hacia un final de carrera pisado: se para");
            limite_ = true;
            v_cmd = 0.0;
            de = qs;
        }
        // La aceleración, con su tope
        const bool acelera = std::fabs(v_cmd) > std::fabs(v_ref_) && v_cmd * v_ref_ >= 0.0;
        const double r = (acelera ? ac : de) * dt;
        const double v = v_ref_ + std::max(-r, std::min(r, v_cmd - v_ref_));
        double p = p_ref_ + 0.5 * (v_ref_ + v) * dt;
        double v1 = v;
        if (modo_ == 1 && !hecho_ && !halt_ && e402_ == OperationEnabled && !limite_) {
            const double e0 = obj_ - p_ref_, e1 = obj_ - p;
            if (e0 == 0.0 || e0 * e1 <= 0.0 || (std::fabs(e1) < 0.5 && std::fabs(v) <= de * dt * 1.5)) {
                p = obj_;
                v1 = 0.0;
                hecho_ = true;
            }
        }
        p_ref_ = p;
        v_ref_ = v1;
        if (v1 == 0.0) {
            if (e402_ == QuickStopActive) e402_ = SwitchOnDisabled;   // y se quita la corriente
            if (modo_ == 6 && fase_hm_ == 3) { fase_hm_ = 0; hm_ok_ = true; }
            else if (modo_ == 6 && fase_hm_ == 4) fase_hm_ = 0;
        }
    }

    const sc_core::sc_event_or_list& despierta() {
        if (!despierta_) {
            despierta_.reset(new sc_core::sc_event_or_list());
            *despierta_ |= vcc_->value_changed_event();
            *despierta_ |= gnd_->value_changed_event();
            for (Entrada* e : {&step_, &dir_, &en_}) if (e->n) *despierta_ |= e->n->value_changed_event();
            for (const Entrada& s : sen_) if (s.n) *despierta_ |= s.n->value_changed_event();
            *despierta_ |= evento_conexion();
            if (co_) *despierta_ |= ev_can_;
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
    std::unique_ptr<InyectorCorriente> iny_vcc_;
    bool cortado_ = false;            // la protección de sobretensión, hasta otro EN
    unsigned sobretensiones_ = 0;
    Entrada step_, dir_, en_;
    Entrada sen_[7];                   // H1, H2, H3, A, B y los dos finales
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
    // El bus de campo (F6)
    analog_net_if* can_ = nullptr;
    std::unique_ptr<PuertoCan> puerto_;
    std::unique_ptr<EsclavoCanOpen> co_;
    sc_core::sc_event ev_can_;
    int id_term_ = -1, term_puesto_ = -1;
    bool co_on_ = false, co_cambio_ = false, reinicio_ = false, potencia_ = false;
    Estado402 e402_ = NoListo;
    uint16_t cw_ = 0, cw_ant_ = 0, codigo_error_ = 0;
    int modo_ = 0, metodo_ = 35, fase_hm_ = 0;
    uint32_t obj_607a_ = 0, vel_obj_ = 0, home_offset_ = 0, v_perfil_ = 0, acel_ = 1, decel_ = 1,
             d_rapida_ = 1, v_busca_ = 1, v_cero_ = 1, a_home_ = 1, ventana_pos_ = 1;
    bool sp_ack_ = false, halt_ = false, hm_ok_ = false, hm_err_ = false, limite_ = false,
         hay_pend_ = false, hecho_ = true, fin_n_ = false, fin_p_ = false;
    double cero_ = 0.0, p_ref_ = 0.0, v_ref_ = 0.0, obj_ = 0.0, pend_ = 0.0;
    std::set<std::string> dichos_;
};

} // namespace stm32

#endif // STM32_PARTS_CONTROLADOR_MOTOR_H
