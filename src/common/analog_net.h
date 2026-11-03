// =============================================================================
// analog_net.h — Nodo analógico de pin: resolución de tensión/corriente (float)
//
// Cada pin físico del encapsulado es un AnalogNet. Los conectados (pad del MCU,
// circuito externo del testbench, DAC, osciladores...) se registran como
// "drivers" y publican un equivalente Thevenin {V_drv, R_out}. Alta impedancia
// se expresa con R_out = R_HIZ. El canal resuelve en cada actualización:
//
//     V_pin = sum(V_i/R_i) / sum(1/R_i)         (divisor resistivo)
//     I_i   = (V_i - V_pin) / R_i               (corriente que aporta cada uno)
//
// La conversión a digital (0/1/X con histéresis Schmitt y detección de rango)
// la hace el Pad (pins/pad.h), no este canal. [IR, §2.4; plan P5]
// =============================================================================
#ifndef STM32_COMMON_ANALOG_NET_H
#define STM32_COMMON_ANALOG_NET_H

#include <systemc>
#include <vector>
#include <limits>
#include <cmath>

namespace stm32 {

struct AnalogDrive {
    float v_drv = 0.0f;                       // tensión Thevenin [V]
    float r_out = 1.0e12f;                    // resistencia de salida [ohm]
};

constexpr float R_HIZ = 1.0e12f;              // alta impedancia efectiva

// Por debajo de esta conductancia total se considera que el nodo está
// flotante: ningún driver lo sujeta y su tensión no es observable. Equivale a
// que todos los drivers presenten más de 1 Gohm [IR, §2.4; premisa de pines].
constexpr double G_FLOAT = 1.0e-9;            // 1/(1 Gohm)

// ---------------------------------------------------------------------------
// UNA FASE DE CORRIENTE IMPUESTA (fase F1a de la CNC, doc/analisis_cnc.md
// §5.2). Un driver de paso a paso de verdad es un CHOPPER: conmuta su puente a
// 20-40 kHz para que la corriente de cada fase siga a la consigna. Simularlo
// conmutando costaría cientos de miles de sucesos por segundo, así que cada
// fase del driver no es un Thevenin sino esto: «quiero `i` amperios, y puedo
// poner entre mis dos salidas de -vbus a +vbus». Lo DECLARA en los dos nodos de
// sus salidas -un polo en cada uno-, como `declara_reloj` declara una
// frecuencia, y la carga que encuentre esas dos salidas en sus bornes -una
// bobina- integra su corriente sabiéndolo: si la tensión que haría falta cabe
// en el tope, la corriente es la consigna; si no, la tensión se queda en el
// tope y la corriente va por detrás. Lo que gasta, la carga lo deja aquí, y el
// driver lo pide a su alimentación.
// ---------------------------------------------------------------------------
struct FaseCorriente {
    // Lo que manda el driver
    double i = 0.0;          // A: la consigna; sale por el polo + y vuelve por el -
    double vbus = 0.0;       // V: lo más que pone entre los dos polos, en un sentido o en otro
    bool   activa = false;   // el puente conduce: habilitado, con tensión y sin protección
    double vd = 0.7;         // V: sus diodos; suelto, la corriente vuelve al bus contra vbus + 2·vd
    const sc_core::sc_event* ev = nullptr;   // lo notifica el driver cuando cambia algo
    const char* quien = "";  // el driver, para los mensajes
    int    numero = 0;       // la fase, para los mensajes: 1, 2...
    // Lo que deja la carga: lo de ahora, y lo ACUMULADO hasta `t_carga` -la
    // energía que ha entrado en la bobina y la integral de i²-, de donde el
    // driver saca lo que ha gastado de media entre dos miradas: con un chopper
    // lo de ahora salta de signo dentro de cada paso
    double v_carga = 0.0;    // V del polo + al -
    double i_carga = 0.0;    // A del polo + al -
    bool   con_carga = false;
    double energia = 0.0;    // J, del driver a la bobina (negativa si vuelve)
    double i2s = 0.0;        // A²·s
    double t_carga = 0.0;    // s: hasta cuándo valen las dos
};
struct SalidaCorriente {
    FaseCorriente* fase = nullptr;
    int polo = 0;            // +1: por aquí sale la consigna; -1: por aquí vuelve
};

class analog_net_if : virtual public sc_core::sc_interface {
public:
    virtual int   register_driver(const char* name)            = 0;
    virtual void  set_drive(int id, float v_drv, float r_out)  = 0;
    virtual void  set_hiz(int id)                              = 0;
    virtual float voltage() const                              = 0; // V del nodo
    virtual float current(int id) const                        = 0; // I del driver
    // Conductancia total del nodo [S] y condición de nodo flotante (alta Z).
    virtual double conductance() const                         = 0;
    virtual bool  floating() const                             = 0;
    // Tensión que tendría el nodo SIN la aportación de un driver concreto. La
    // necesita cualquier pieza que quiera saber qué está haciendo el resto del
    // nodo sin contarse a sí misma: por ejemplo, un hilo de bus de colector
    // abierto que propaga el cero de un pin a los demás sin realimentarse.
    virtual float voltage_excluding(int id, bool& floating_out) const = 0;
    virtual const sc_core::sc_event& value_changed_event() const = 0;
    // UN RELOJ SIN ONDA (fase S1 del F303). Una fuente de reloj puede DECIR
    // su frecuencia en el nodo en vez de dar sus flancos: es el `ExtClock` con
    // onda="no", que hace el mismo trabajo para un oscilador en modo bypass
    // (`rcc/osc_pll.h`) sin costar dos sucesos por periodo. Cero: nadie dice
    // nada, y quien quiera la frecuencia la mide, como siempre.
    virtual double reloj_declarado() const { return 0.0; }
    virtual void   declara_reloj(double) {}
    // LAS SALIDAS DE CORRIENTE IMPUESTA que hay en el nodo (véase arriba). Lo
    // normal es ninguna, o una; dos son dos drivers en corto
    virtual const std::vector<SalidaCorriente>& salidas_corriente() const {
        static const std::vector<SalidaCorriente> ninguna;
        return ninguna;
    }
    virtual void declara_salida_corriente(const SalidaCorriente&) {}
};

class AnalogNet : public sc_core::sc_prim_channel, public analog_net_if {
public:
    explicit AnalogNet(const char* nm) : sc_core::sc_prim_channel(nm) {}

    // El nombre de ESQUEMÁTICO, cuando no se puede usar como nombre de
    // SystemC: el pin 17 de un conector se llama `CN7.17`, y el punto es el
    // separador de jerarquía (SystemC lo cambiaría por `_` con un aviso). Quien
    // crea el nodo -el `NodeMap` de la placa- lo crea con el nombre saneado y
    // apunta aquí el de verdad, que es el que se vuelca y el que se lee en los
    // mensajes. Vacío: el nombre es el de SystemC.
    std::string nombre_esq;
    double reloj_declarado() const override { return reloj_hz_; }
    void   declara_reloj(double hz) override { reloj_hz_ = hz; }
    const std::vector<SalidaCorriente>& salidas_corriente() const override { return salidas_; }
    void declara_salida_corriente(const SalidaCorriente& s) override { salidas_.push_back(s); }

    int register_driver(const char* /*name*/) override {
        drv_.push_back(AnalogDrive{});
        return static_cast<int>(drv_.size()) - 1;
    }
    void set_drive(int id, float v, float r) override {
        drv_[static_cast<size_t>(id)] = AnalogDrive{v, (r < 0.1f ? 0.1f : r)};
        request_update();
    }
    void set_hiz(int id) override { set_drive(id, 0.0f, R_HIZ); }

    float voltage() const override { return v_pin_; }
    float current(int id) const override {
        const AnalogDrive& d = drv_[static_cast<size_t>(id)];
        return (d.v_drv - v_pin_) / d.r_out;
    }
    double conductance() const override { return g_tot_; }
    bool   floating()    const override { return g_tot_ < G_FLOAT; }
    float voltage_excluding(int id, bool& floating_out) const override {
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < drv_.size(); ++i) {
            if (int(i) == id) continue;
            num += drv_[i].v_drv / drv_[i].r_out;
            den += 1.0 / drv_[i].r_out;
        }
        floating_out = (den < G_FLOAT);
        return floating_out ? v_pin_ : float(num / den);
    }
    // La corriente de un driver con la tensión del nodo RESUELTA AHORA, sin el
    // umbral de 0,1 mV con el que `update()` decide si el nodo ha cambiado.
    // Con una fuente de 0,1 ohm ese umbral es 1 mA: un consumo de microamperios
    // no mueve `voltage()` y `current()` daría cero. Es para MEDIR (el LDO de
    // una sonda XDS110, EnergyTrace); no cambia el nodo ni avisa a nadie.
    double corriente_exacta(int id) const {
        double num = 0.0, den = 0.0;           // en double desde el principio:
        for (const AnalogDrive& d : drv_) {    // el cociente en float ya tiene un
            num += double(d.v_drv) / double(d.r_out);   // error de 1 uA con 0,1 ohm
            den += 1.0 / double(d.r_out);
        }
        if (den < G_FLOAT) return 0.0;
        const AnalogDrive& d = drv_[static_cast<size_t>(id)];
        return (double(d.v_drv) - num / den) / double(d.r_out);
    }
    // Corriente total que entra al nodo desde los drivers cuyo id no se pasa;
    // sirve al encapsulado para acumular el consumo por VDD/VSS [IR, §2.4].
    float abs_current(int id) const { return std::fabs(current(id)); }
    const sc_core::sc_event& value_changed_event() const override { return ev_; }

protected:
    void update() override {
        // Resolución del nodo: superposición de equivalentes Thevenin.
        double num = 0.0, den = 0.0;
        for (const AnalogDrive& d : drv_) { num += d.v_drv / d.r_out; den += 1.0 / d.r_out; }
        g_tot_ = den;
        // Un nodo sin ningún driver que lo sujete queda flotante: su tensión no
        // está definida; se conserva la última resuelta para no inventar un 0 V.
        const float v = (den >= G_FLOAT) ? static_cast<float>(num / den) : v_pin_;
        const bool  fl = (den < G_FLOAT);
        if (std::fabs(v - v_pin_) > 1e-4f || fl != float_prev_) {
            v_pin_ = v; float_prev_ = fl;
            ev_.notify(sc_core::SC_ZERO_TIME);
        }
    }

private:
    double reloj_hz_ = 0.0;
    std::vector<SalidaCorriente> salidas_;
    std::vector<AnalogDrive> drv_;
    float             v_pin_ = 0.0f;
    double            g_tot_ = 0.0;
    bool              float_prev_ = true;
    sc_core::sc_event ev_;
};

} // namespace stm32
#endif // STM32_COMMON_ANALOG_NET_H
