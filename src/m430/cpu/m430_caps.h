// =============================================================================
// m430_caps.h — Lo que distingue un núcleo MSP430 de otro (M1 de
// doc/analisis_msp430.md, §3.1 y §3.2)
//
// Tres CPU con la misma idea y distintas cuentas:
//
//   * CPU    (1xx/2xx/4xx pequeños; el G2553): 16 bits, 64 KB [UG2 cap. 3];
//   * CPUX   (2xx/4xx grandes): 20 bits, 1 MB, la palabra de extensión y las
//            instrucciones de direcciones [UG2 cap. 4];
//   * CPUXV2 (5xx, 6xx y todas las FR): la misma ISA que la CPUX con otros
//            ciclos [SLAU391F, extracto de SLAU208 cap. 1].
//
// Las tablas de ciclos están en m430_ciclos.h; las erratas, en m430_errata.h.
// =============================================================================
#ifndef STM32_M430_CPU_M430_CAPS_H
#define STM32_M430_CPU_M430_CAPS_H

#include <cstdint>

namespace stm32 {

enum class M430Arq : uint8_t { CPU = 0, CPUX = 1, CPUXV2 = 2 };

struct M430Caps {
    const char* nombre;
    M430Arq arq;
    uint32_t errata;        // los bits ERR_* de soc/msp430/desc/desc430.h
    // Al aceptar una interrupción se borra el SR... salvo SCG0 en la CPUXV2:
    // «All bits of SR are cleared except SCG0» [SLAU208 §1.3.4.1, SLAU367
    // §1.3.4.1]; en la CPU y la CPUX de 2xx se borra entero [UG2 §2.2.3.1]
    bool irq_conserva_scg0;
    bool tiene_20bits() const { return arq != M430Arq::CPU; }
};

// Las del laboratorio: las tres CPU sin erratas
inline constexpr M430Caps M430_LAB_CPU    {"lab-cpu",    M430Arq::CPU,    0, false};
inline constexpr M430Caps M430_LAB_CPUX   {"lab-cpux",   M430Arq::CPUX,   0, false};
inline constexpr M430Caps M430_LAB_CPUXV2 {"lab-cpuxv2", M430Arq::CPUXV2, 0, true};

} // namespace stm32
#endif // STM32_M430_CPU_M430_CAPS_H
