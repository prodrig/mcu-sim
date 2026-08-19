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

class analog_net_if : virtual public sc_core::sc_interface {
public:
    virtual int   register_driver(const char* name)            = 0;
    virtual void  set_drive(int id, float v_drv, float r_out)  = 0;
    virtual void  set_hiz(int id)                              = 0;
    virtual float voltage() const                              = 0; // V del nodo
    virtual float current(int id) const                        = 0; // I del driver
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
    const sc_core::sc_event& value_changed_event() const override { return ev_; }

protected:
    void update() override {
        // Resolución del nodo: superposición de equivalentes Thevenin.
        double num = 0.0, den = 0.0;
        for (const AnalogDrive& d : drv_) { num += d.v_drv / d.r_out; den += 1.0 / d.r_out; }
        const float v = (den > 0.0) ? static_cast<float>(num / den) : 0.0f;
        if (std::fabs(v - v_pin_) > 1e-4f) { v_pin_ = v; ev_.notify(sc_core::SC_ZERO_TIME); }
    }

private:
    std::vector<AnalogDrive> drv_;
    float             v_pin_ = 0.0f;
    sc_core::sc_event ev_;
};

} // namespace stm32
#endif // STM32_COMMON_ANALOG_NET_H
