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
};

class AnalogNet : public sc_core::sc_prim_channel, public analog_net_if {
public:
    explicit AnalogNet(const char* nm) : sc_core::sc_prim_channel(nm) {}

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
    std::vector<AnalogDrive> drv_;
    float             v_pin_ = 0.0f;
    double            g_tot_ = 0.0;
    bool              float_prev_ = true;
    sc_core::sc_event ev_;
};

} // namespace stm32
#endif // STM32_COMMON_ANALOG_NET_H
