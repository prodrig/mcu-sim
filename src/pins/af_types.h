// =============================================================================
// af_types.h — Tipos comunes del multiplexor de funciones alternativas
//
// Se separan del pin_mux para que el puerto GPIO pueda comunicar su selección
// (MODER + AFRL/AFRH) sin depender del módulo que contiene los pads.
// [IR, §3.3.3, §3.4.9, §2.1]
// =============================================================================
#ifndef STM32_PINS_AF_TYPES_H
#define STM32_PINS_AF_TYPES_H

#include <systemc>
#include <cstdint>

namespace stm32 {

// Valor de af_sel cuando el pin NO está en modo función alternativa.
constexpr uint8_t AF_NONE = 0xFF;

// Extremo digital de una función alternativa de un periférico.
//   out/oe : los lee el mux (varios pines pueden compartirlos sin conflicto).
//   in     : lo ESCRIBE el mux; cada señal de entrada debe pertenecer a un solo
//            periférico, y el mux garantiza un único escritor por señal.
//   idle_in: valor entregado al periférico cuando ningún pin le encamina la
//            entrada (p. ej. '1' en un RX de USART, que reposa en alto).
struct AfEndpoint {
    sc_core::sc_signal<bool>* out = nullptr;
    sc_core::sc_signal<bool>* oe  = nullptr;
    sc_core::sc_signal<bool>* in  = nullptr;
    bool idle_in = true;
};

// Interfaz que el puerto GPIO usa para publicar su selección de AF por pin.
class af_sel_if {
public:
    virtual ~af_sel_if() {}
    // af = 0..15 (modo AF) o AF_NONE (entrada/salida GPIO o analógico).
    virtual void set_af(unsigned port, unsigned pin, uint8_t af) = 0;
};

} // namespace stm32
#endif // STM32_PINS_AF_TYPES_H
