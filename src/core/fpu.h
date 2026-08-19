// =============================================================================
// fpu.h — Unidad de coma flotante FPv4-SP
//
// Unidad funcional invocada por la CPU (llamada a método, no bus). Ejecuta las
// instrucciones V* de [II, §5] sobre RegFile.s[]/fpscr respetando FPSCR
// (RMode/FZ/DN) y los flags acumulativos IEEE 754. La gestión de contexto
// (CPACR, FPCCR/FPCAR, lazy stacking) es de SCS+CPU [IR, §8.12/8.13].
// Latencias: VDIV/VSQRT 14 ciclos, no bloqueantes para enteros [IR, §8.9].
// =============================================================================
#ifndef STM32_CORE_FPU_H
#define STM32_CORE_FPU_H

#include <systemc>
#include "cpu.h"

namespace stm32 {

SC_MODULE(Fpu) {
    sc_core::sc_in<bool> fclk{"fclk"};
    sc_core::sc_in<bool> rst_n{"rst_n"};
    sc_core::sc_out<bool> irq_fpu{"irq_fpu"};   // IRQ 81 (excepciones FP) [IR, §9.1.2]

    SC_CTOR(Fpu) {}

    // API hacia la CPU (fase F2): ejecuta una V*; devuelve ciclos consumidos.
    // TODO(F2): unsigned execute(uint32_t hw1, uint32_t hw2, RegFile& reg);
    // TODO(F2): comprobación CPACR -> UsageFault NOCP la hace la CPU antes.
};

} // namespace stm32
#endif // STM32_CORE_FPU_H
