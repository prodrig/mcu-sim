// =============================================================================
// m430_estado.h — Los registros de un MSP430 y lo que necesita de fuera
//
// M1 de doc/analisis_msp430.md, §3.1. R0 es el PC, R1 el SP, R2 el SR (y el
// generador de constantes CG1), R3 el generador de constantes CG2. Son de 16
// bits en la CPU y de 20 en la CPUX y la CPUXV2.
//
// Lo que el núcleo pide al SoC:
//   * `m430_mem_if`: leer y escribir bytes y palabras (la CPU nunca pide una
//     palabra en dirección impar: pierde el bit 0 [UG2 §1.4.5]). Las
//     palabras de 20 bits son dos accesos de palabra;
//   * `m430_irq_if` (m430_irq.h): qué vector está pendiente.
// =============================================================================
#ifndef STM32_M430_CPU_M430_ESTADO_H
#define STM32_M430_CPU_M430_ESTADO_H

#include <cstdint>

namespace stm32 {

// Los bits del SR [UG2 figura 3-6]
enum : uint16_t {
    SR_C = 0x0001, SR_Z = 0x0002, SR_N = 0x0004, SR_GIE = 0x0008,
    SR_CPUOFF = 0x0010, SR_OSCOFF = 0x0020, SR_SCG0 = 0x0040, SR_SCG1 = 0x0080,
    SR_V = 0x0100,
    SR_LPM = SR_CPUOFF | SR_OSCOFF | SR_SCG0 | SR_SCG1,
    SR_BITS = 0x01FF,            // los demás son reservados [de modelo: se leen a 0]
};

enum class M430Acceso : uint8_t { Busqueda, Lectura, Escritura, Pila, Vector };

struct m430_mem_if {
    virtual ~m430_mem_if() = default;
    // n = 1 o 2. false: no hay nada en esa dirección (el laboratorio avisa)
    virtual bool lee(uint32_t a, unsigned n, uint32_t& v, M430Acceso t) = 0;
    virtual bool escribe(uint32_t a, unsigned n, uint32_t v, M430Acceso t) = 0;
};

struct M430Estado {
    uint32_t r[16] = {};
    uint32_t& pc() { return r[0]; }
    uint32_t& sp() { return r[1]; }
    uint16_t sr() const { return uint16_t(r[2]); }
};

} // namespace stm32
#endif // STM32_M430_CPU_M430_ESTADO_H
