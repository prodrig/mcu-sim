// =============================================================================
// ws2812.h — Una tira de LED WS2812 / WS2812B (NeoPixel): `n` LED en cadena
//
// E8 de doc/analisis_esp32_riscv.md [hoja de datos de WS2812B, Worldsemi]. Un
// hilo, DIN: cada bit es un pulso alto seguido de uno bajo, 1,25 us en total;
// alto 0,4 us es un 0 y 0,8 us es un 1 (aquí: más de 0,6 us de alto, un 1).
// Cada LED se queda con los primeros 24 bits que le llegan (G, R, B, el bit
// más alto primero) y pasa el resto al siguiente; más de 50 us en bajo es el
// RESET: el color recibido se enciende y la cadena vuelve a empezar. Los
// pulsos fuera de tolerancia no se miran: se toma la regla de los 0,6 us; un
// pulso alto de más de 5 us no es un bit y borra lo recibido [de modelo].
//
// Observables: el color de cada LED (0xRRGGBB) y lo encendido (la media de R,
// G y B, de 0 a 1).
// =============================================================================
#ifndef STM32_PARTS_WS2812_H
#define STM32_PARTS_WS2812_H

#include <systemc>
#include <cstdint>
#include <string>
#include <vector>
#include "../common/analog_net.h"
#include "part_base.h"

namespace stm32 {

SC_MODULE(Ws2812), public ExtPartBase {
    Ws2812(sc_core::sc_module_name nm, analog_net_if& din, unsigned n = 1, double vdd = 5.0)
        : sc_core::sc_module(nm), ExtPartBase("Ws2812", nm), din_(din), vdd_(vdd),
          color_(n ? n : 1, 0u), pend_(n ? n : 1, 0u) {
        for (unsigned i = 0; i < color_.size(); ++i) {
            nombres_.push_back("color" + std::to_string(i));
            nombres_.push_back("luz" + std::to_string(i));
        }
        add_ref("din", din);
        SC_HAS_PROCESS(Ws2812);
        SC_METHOD(flanco);
        sensitive << din.value_changed_event();
        dont_initialize();
    }
    unsigned n() const { return unsigned(color_.size()); }
    uint32_t color(unsigned i) const { return i < color_.size() ? color_[i] : 0u; }   // 0xRRGGBB
    uint64_t tramas() const { return tramas_; }

    unsigned   n_observables() const override { return 2 * n(); }
    Observable observable(unsigned i) const override {
        if (i % 2 == 0) return {nombres_[i].c_str(), "0xRRGGBB", 0.f, 16777215.f, false};
        return {nombres_[i].c_str(), "", 0.f, 1.f, true};
    }
    float valor_observable(unsigned i) const override {
        const uint32_t c = color(i / 2);
        if (i % 2 == 0) return float(c);
        return float(((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / (3.f * 255.f);
    }

private:
    analog_net_if& din_;
    double vdd_;
    std::vector<uint32_t> color_, pend_;
    std::vector<std::string> nombres_;
    sc_core::sc_time t_sube_ = sc_core::SC_ZERO_TIME, t_baja_ = sc_core::SC_ZERO_TIME;
    bool alto_ = false;
    unsigned bits_ = 0;                  // bits recibidos desde el último RESET
    uint64_t tramas_ = 0;

    void flanco() {
        // VIH es 0,7 VDD (3,5 V a 5 V): un ESP32 a 3,3 V queda justo, y en
        // la práctica funciona; aquí, el umbral en 1,5 V
        const bool v = din_.voltage() > 1.5;
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        if (v && !alto_) {
            // ¿RESET? (más de 50 us en bajo)
            if (bits_ && (ahora - t_baja_).to_seconds() > 50e-6) enciende();
            t_sube_ = ahora;
        } else if (!v && alto_) {
            const double alto = (ahora - t_sube_).to_seconds();
            // Un pulso alto de más de 5 us no es un bit (el pin que sube al
            // encender el micro, por ejemplo): se descarta lo que hubiera
            if (alto > 5e-6) {
                bits_ = 0;
                t_baja_ = ahora;
                alto_ = v;
                sc_core::next_trigger(din_.value_changed_event());
                return;
            }
            const bool uno = alto > 0.6e-6;
            const unsigned led = bits_ / 24;
            if (led < pend_.size()) {
                if (bits_ % 24 == 0) pend_[led] = 0;
                pend_[led] = (pend_[led] << 1) | (uno ? 1u : 0u);
            }
            ++bits_;
            t_baja_ = ahora;
            // El RESET se ve también sin otro pulso: se mira dentro de 50 us
            sc_core::next_trigger(sc_core::sc_time(60, sc_core::SC_US), din_.value_changed_event());
            alto_ = v;
            return;
        } else if (!v && !alto_ && bits_ && (ahora - t_baja_).to_seconds() > 50e-6) {
            enciende();
        }
        alto_ = v;
        sc_core::next_trigger(din_.value_changed_event());
    }
    void enciende() {
        const unsigned leds = std::min<unsigned>(bits_ / 24, unsigned(color_.size()));
        for (unsigned i = 0; i < leds; ++i) {
            const uint32_t g = (pend_[i] >> 16) & 0xFF, r = (pend_[i] >> 8) & 0xFF, b = pend_[i] & 0xFF;
            color_[i] = (r << 16) | (g << 8) | b;
        }
        bits_ = 0;
        ++tramas_;
    }
};

} // namespace stm32
#endif // STM32_PARTS_WS2812_H
