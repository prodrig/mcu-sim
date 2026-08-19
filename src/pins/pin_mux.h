// =============================================================================
// pin_mux.h — Multiplexor de funciones alternativas y ruta analógica
//
// Contiene los pads de los 9 puertos GPIO (A..I, 16 pines cada uno; bonded
// según el pinout LQFP100 [IR, §2.1]) y encamina:
//   * GPIO  -> pad          : configuración (MODER/OTYPER/OSPEEDR/PUPDR) y, en
//     modo GPIO de salida, el dato del ODR;
//   * AF    <-> pad         : por REGISTRO en elaboración (connect_af). Cuando
//     el pin está en modo AF y AFRL/AFRH selecciona esa función, el periférico
//     gobierna out/oe y recibe la entrada; en cualquier otro caso recibe su
//     valor de reposo [IR, §3.3.3];
//   * ruta analógica        : ADC/DAC/HSE/LSE reciben el AnalogNet del pin.
//
// Reglas de escritura (SystemC solo admite un escritor por sc_signal):
//   * cada pad tiene UN proceso de salida, que elige entre GPIO y AF;
//   * cada señal de entrada de periférico tiene UN proceso, compartido por
//     todos los pines que puedan encaminarla.
// [plan P5/P7; IR, §3, §15.4]
// =============================================================================
#ifndef STM32_PINS_PIN_MUX_H
#define STM32_PINS_PIN_MUX_H

#include <systemc>
#include <sysc/kernel/sc_spawn.h>
#include <array>
#include <map>
#include <vector>
#include "../common/analog_net.h"
#include "af_types.h"
#include "pad.h"

namespace stm32 {

constexpr unsigned N_GPIO_PORTS = 9;   // A..I
constexpr unsigned N_PORT_PINS  = 16;

SC_MODULE(PinMux), public af_sel_if {
    // Nodos analógicos y pads de todos los pines de puerto (creados aquí).
    std::array<std::array<AnalogNet*, N_PORT_PINS>, N_GPIO_PORTS> net{};
    std::array<std::array<Pad*,       N_PORT_PINS>, N_GPIO_PORTS> pad{};

    // Bundles hacia los puertos GPIO (los conecta el top).
    sc_core::sc_vector<sc_core::sc_signal<PadDrive>> gpio_drive;  // GPIO -> mux
    sc_core::sc_vector<sc_core::sc_signal<PadDrive>> pad_drive;   // mux  -> pad
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_din;     // pad  -> GPIO
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_din_ok;
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_oor;

    SC_CTOR(PinMux)
        : gpio_drive("gpio_drive", N_GPIO_PORTS * N_PORT_PINS),
          pad_drive("pad_drive",   N_GPIO_PORTS * N_PORT_PINS),
          pad_din("pad_din",       N_GPIO_PORTS * N_PORT_PINS),
          pad_din_ok("pad_din_ok", N_GPIO_PORTS * N_PORT_PINS),
          pad_oor("pad_oor",       N_GPIO_PORTS * N_PORT_PINS) {
        char nm[16];
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                const unsigned k = idx(p, i);
                af_sel_[p][i] = AF_NONE;
                std::snprintf(nm, sizeof nm, "net_%c%u", 'A' + p, i);
                net[p][i] = new AnalogNet(nm);
                std::snprintf(nm, sizeof nm, "pad_%c%u", 'A' + p, i);
                pad[p][i] = new Pad(nm);
                pad[p][i]->drive(pad_drive[k]);
                pad[p][i]->din(pad_din[k]);
                pad[p][i]->din_valid(pad_din_ok[k]);
                pad[p][i]->out_of_range(pad_oor[k]);
                pad[p][i]->net(*net[p][i]);
                pad[p][i]->bonded = is_bonded_lqfp100(p, i);
                // PC13/PC14/PC15 pasan por el conmutador de potencia del dominio
                // de backup: 3 mA máximos y 2 MHz [IR, §2.1 nota 2].
                if (p == 2 && i >= 13) pad[p][i]->i_max = 3e-3;
            }
    }

    ~PinMux() override {
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) { delete pad[p][i]; delete net[p][i]; }
    }

    // --- API de registro (elaboración, la llama el top) ---------------------
    // Función alternativa digital: (puerto, pin, af) -> señales del periférico.
    void connect_af(unsigned port, unsigned pin, uint8_t af, const AfEndpoint& ep) {
        af_tab_[key(port, pin, af)] = ep;
        if (ep.in) in_users_[ep.in].push_back(Src{port, pin, af, ep.idle_in});
    }
    // Registra la misma AF en todos los pines de todos los puertos (EVENTOUT).
    void connect_af_all(uint8_t af, const AfEndpoint& ep) {
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) connect_af(p, i, af, ep);
    }
    // Ruta analógica (ADC/DAC/HSE/LSE): devuelve el nodo del pin.
    analog_net_if& analog(unsigned port, unsigned pin) { return *net[port][pin]; }

    // --- Selección de AF (la publica el puerto GPIO) ------------------------
    void set_af(unsigned port, unsigned pin, uint8_t af) override {
        if (af_sel_[port][pin] == af) return;
        af_sel_[port][pin] = af;
        af_ev_[idx(port, pin)].notify(sc_core::SC_ZERO_TIME);
    }
    uint8_t af_of(unsigned port, unsigned pin) const { return af_sel_[port][pin]; }

    // Corriente total absorbida/entregada por el encapsulado a través de los
    // pines de puerto. El límite acumulado en VDD/VSS es 240 mA [IR, §2.4].
    double total_pin_current() const {
        double s = 0.0;
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i)
                if (pad[p][i]->bonded) s += std::fabs(double(pad[p][i]->current()));
        return s;
    }

    // Pines soldados en LQFP100: puertos A..E completos y PH0/PH1 [IR, §2.1].
    static bool is_bonded_lqfp100(unsigned port, unsigned pin) {
        if (port <= 4) return true;                       // A..E
        if (port == 7) return pin <= 1;                   // PH0, PH1
        return false;                                     // F, G, resto H, I
    }

    static unsigned idx(unsigned p, unsigned i) { return p * N_PORT_PINS + i; }

    // -----------------------------------------------------------------------
    // Elaboración de los procesos del mux: uno por pad (salida) y uno por
    // señal de entrada de periférico. Se crean aquí porque las AF se registran
    // durante el enlazado del top, después del constructor.
    // -----------------------------------------------------------------------
    void end_of_elaboration() override {
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) spawn_out_proc(p, i);
        for (const auto& kv : in_users_) spawn_in_proc(kv.first, kv.second);
    }

private:
    struct Src { unsigned port, pin; uint8_t af; bool idle; };

    static unsigned key(unsigned p, unsigned i, uint8_t af) {
        return (p << 12) | (i << 4) | af;
    }

    const AfEndpoint* find_af(unsigned p, unsigned i, uint8_t af) const {
        if (af == AF_NONE) return nullptr;
        auto it = af_tab_.find(key(p, i, af));
        return (it == af_tab_.end()) ? nullptr : &it->second;
    }

    // ---- Salida: GPIO o periférico, según MODER/AFRx ----------------------
    void spawn_out_proc(unsigned p, unsigned i) {
        const unsigned k = idx(p, i);
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        o.set_sensitivity(&gpio_drive[k].value_changed_event());
        o.set_sensitivity(&af_ev_[k]);
        for (uint8_t a = 0; a < 16; ++a) {
            const AfEndpoint* ep = find_af(p, i, a);
            if (!ep) continue;
            if (ep->out) o.set_sensitivity(&ep->out->value_changed_event());
            if (ep->oe)  o.set_sensitivity(&ep->oe->value_changed_event());
        }
        sc_core::sc_spawn([this, p, i, k] { drive_pad(p, i, k); },
                          ("mux_out_" + std::to_string(k)).c_str(), &o);
        drive_pad(p, i, k);           // valor inicial coherente con el reset
    }

    void drive_pad(unsigned p, unsigned i, unsigned k) {
        PadDrive d = gpio_drive[k].read();       // configuración del puerto
        const uint8_t a = af_sel_[p][i];
        if (a != AF_NONE) {
            const AfEndpoint* ep = find_af(p, i, a);
            if (ep) {                            // el periférico manda
                d.out = ep->out ? ep->out->read() : false;
                d.oe  = ep->oe  ? ep->oe->read()  : false;
            } else {                             // AF no modelada: alta impedancia
                d.out = false;
                d.oe  = false;
            }
        }
        pad_drive[k].write(d);
    }

    // ---- Entrada: un proceso por señal de periférico ----------------------
    void spawn_in_proc(sc_core::sc_signal<bool>* sig, const std::vector<Src>& src) {
        sc_core::sc_spawn_options o;
        o.spawn_method();
        o.dont_initialize();
        for (const Src& s : src) {
            o.set_sensitivity(&pad_din[idx(s.port, s.pin)].value_changed_event());
            o.set_sensitivity(&af_ev_[idx(s.port, s.pin)]);
        }
        sc_core::sc_spawn([this, sig, src] { feed_in(sig, src); },
                          sc_core::sc_gen_unique_name("mux_in"), &o);
        feed_in(sig, src);
    }

    void feed_in(sc_core::sc_signal<bool>* sig, const std::vector<Src>& src) {
        for (const Src& s : src)
            if (af_sel_[s.port][s.pin] == s.af) {
                sig->write(pad_din[idx(s.port, s.pin)].read());
                return;
            }
        sig->write(src.empty() ? true : src.front().idle);
    }

    std::map<unsigned, AfEndpoint> af_tab_;
    std::map<sc_core::sc_signal<bool>*, std::vector<Src>> in_users_;
    std::array<std::array<uint8_t, N_PORT_PINS>, N_GPIO_PORTS> af_sel_{};
    sc_core::sc_event af_ev_[N_GPIO_PORTS * N_PORT_PINS];
};

} // namespace stm32
#endif // STM32_PINS_PIN_MUX_H
