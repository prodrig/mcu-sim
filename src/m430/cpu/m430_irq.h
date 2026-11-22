// =============================================================================
// m430_irq.h — Las interrupciones de un MSP430, vistas desde el núcleo
//
// M1 de doc/analisis_msp430.md, §3.4. Vectorizadas y con prioridad fija: la
// posición en la tabla (cuanto más alta la dirección, más prioridad; el reset
// en 0xFFFE es la más alta). El SoC sabe quién pide cada vector y qué está
// enmascarado; el núcleo le pregunta, entre instrucción e instrucción, por el
// vector de más prioridad que puede entrar:
//
//   * `pendiente(gie)`: la dirección del vector (0xFFxx), o 0 si no hay. Con
//     GIE a 0 solo cuentan las NMI (el NMI del 2xx, y SYSSNIV/SYSUNIV en 5xx
//     y FR): eso lo decide el SoC, que es quien sabe cuáles son;
//   * `acepta(vector)`: el núcleo ha entrado. Las fuentes de un solo origen
//     bajan su bandera aquí [UG2 §2.2.3.1, paso 5]; las de varios, no.
//
// Las NMI se piden aunque GIE esté a 0, y despiertan al núcleo de cualquier
// modo de bajo consumo, como las demás.
// =============================================================================
#ifndef STM32_M430_CPU_M430_IRQ_H
#define STM32_M430_CPU_M430_IRQ_H

#include <cstdint>

namespace stm32 {

struct m430_irq_if {
    virtual ~m430_irq_if() = default;
    virtual uint16_t pendiente(bool gie) = 0;
    virtual void acepta(uint16_t vector) = 0;
    // RETI ejecutada (el ICC de las FR235x saca su pila virtual) [UG FR2 §5.2.5]
    virtual void al_reti() {}
};

} // namespace stm32
#endif // STM32_M430_CPU_M430_IRQ_H
