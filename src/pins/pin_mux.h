// =============================================================================
// pin_mux.h — Multiplexor de funciones alternativas y ruta analógica
//
// Contiene los pads de los 9 puertos GPIO (A..I, 16 pines cada uno; bonded
// según el pinout LQFP100 [IR, §2.1]) y encamina:
//   * GPIO <-> pad          : estructural (bundles PadDrive / din), en el top.
//   * Periférico <-> pad    : por REGISTRO en elaboración (connect_af): cada
//     periférico entrega sus señales digitales (out/oe/in) y el pin_mux las
//     conmuta según AFRL/AFRH+MODER que le comunica el puerto GPIO.
//   * Ruta analógica        : ADC/DAC/HSE/LSE reciben el AnalogNet del pad.
// [plan P5/P7; IR, §3, §15.4]
// =============================================================================
#ifndef STM32_PINS_PIN_MUX_H
#define STM32_PINS_PIN_MUX_H

#include <systemc>
#include <array>
#include <map>
#include "../common/analog_net.h"
#include "pad.h"

namespace stm32 {

constexpr unsigned N_GPIO_PORTS = 9;   // A..I
constexpr unsigned N_PORT_PINS  = 16;

// Extremo digital de una función alternativa de un periférico.
struct AfEndpoint {
    sc_core::sc_signal<bool>* out = nullptr;  // valor que conduce el periférico
    sc_core::sc_signal<bool>* oe  = nullptr;  // el periférico controla dirección
    sc_core::sc_signal<bool>* in  = nullptr;  // entrada hacia el periférico
};

SC_MODULE(PinMux) {
    // Nodos analógicos y pads de todos los pines de puerto (creados aquí).
    std::array<std::array<AnalogNet*, N_PORT_PINS>, N_GPIO_PORTS> net{};
    std::array<std::array<Pad*,       N_PORT_PINS>, N_GPIO_PORTS> pad{};

    // Bundles hacia los puertos GPIO (los conecta el top).
    sc_core::sc_vector<sc_core::sc_signal<PadDrive>> gpio_drive;  // GPIO -> pad
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_din;     // pad  -> GPIO
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_din_ok;
    sc_core::sc_vector<sc_core::sc_signal<bool>>     pad_oor;

    // Selección AF vigente por pin (la escribe el puerto GPIO: MODER+AFR).
    // af = 0..15; 0xFF = pin en modo GPIO/analógico (sin AF). TODO(F3).
    std::array<std::array<uint8_t, N_PORT_PINS>, N_GPIO_PORTS> af_sel{};

    SC_CTOR(PinMux)
        : gpio_drive("gpio_drive", N_GPIO_PORTS * N_PORT_PINS),
          pad_din("pad_din", N_GPIO_PORTS * N_PORT_PINS),
          pad_din_ok("pad_din_ok", N_GPIO_PORTS * N_PORT_PINS),
          pad_oor("pad_oor", N_GPIO_PORTS * N_PORT_PINS) {
        char nm[16];
        for (unsigned p = 0; p < N_GPIO_PORTS; ++p)
            for (unsigned i = 0; i < N_PORT_PINS; ++i) {
                const unsigned k = p * N_PORT_PINS + i;
                std::snprintf(nm, sizeof nm, "net_%c%u", 'A' + p, i);
                net[p][i] = new AnalogNet(nm);
                std::snprintf(nm, sizeof nm, "pad_%c%u", 'A' + p, i);
                pad[p][i] = new Pad(nm);
                pad[p][i]->drive(gpio_drive[k]);
                pad[p][i]->din(pad_din[k]);
                pad[p][i]->din_valid(pad_din_ok[k]);
                pad[p][i]->out_of_range(pad_oor[k]);
                pad[p][i]->net(*net[p][i]);
                pad[p][i]->bonded = is_bonded_lqfp100(p, i);
            }
    }

    // --- API de registro (elaboración, la llama el top) ---------------------
    // Función alternativa digital: (puerto, pin, af) -> señales del periférico.
    void connect_af(unsigned port, unsigned pin, uint8_t af, const AfEndpoint& ep) {
        af_tab_[key(port, pin, af)] = ep;     // el mux se aplica en F3 (TODO)
    }
    // Ruta analógica (ADC/DAC/HSE/LSE): devuelve el nodo del pin.
    analog_net_if& analog(unsigned port, unsigned pin) { return *net[port][pin]; }

    // Pines soldados en LQFP100: puertos A..E completos y PH0/PH1 [IR, §2.1].
    static bool is_bonded_lqfp100(unsigned port, unsigned pin) {
        if (port <= 4) return true;                       // A..E
        if (port == 7) return pin <= 1;                   // PH0, PH1
        return false;                                     // F, G, resto H, I
    }

private:
    static unsigned key(unsigned p, unsigned i, uint8_t af) {
        return (p << 12) | (i << 4) | af;
    }
    std::map<unsigned, AfEndpoint> af_tab_;
    // TODO(F3): proceso de conmutación out/oe/in según af_sel y af_tab_,
    //           tabla completa AF0-AF15 [IR, §2.1 y §15.4].
};

} // namespace stm32
#endif // STM32_PINS_PIN_MUX_H
