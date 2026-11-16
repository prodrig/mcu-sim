// =============================================================================
// comp_b.h — El Comp_B de los MSP430 5xx/6xx [UG5 SLAU208Q cap. 30]
// (M4 de doc/analisis_msp430.md)
//
// Es el Comp_E del MSP432P4 (comp_e.h) con lo que cambia [UG5 §30.3]:
//   * los registros están en los mismos sitios y con los mismos bits:
//     CBCTL0 +00 (IPSEL/IPEN, IMSEL/IMEN), CBCTL1 +02 (CBOUT, OUTPOL, F, IES,
//     SHORT, EX, FDLY, PWRMD, ON, MRVL, MRVS), CBCTL2 +04 (REF0, RSEL, RS,
//     REF1, REFL, REFACC), CBCTL3 +06 (los PD), CBINT +0C y CBIV +0E;
//   * no hay CBRDYIFG ni CBRDYIE: CBINT solo tiene IFG, IIFG y sus IE, y el
//     IV da 2 (CBIFG) y 4 (CBIIFG);
//   * CBREFL = 1 son 1,5 V (2,0 y 2,5 V como en el Comp_E), y la
//     referencia es la del módulo REF, que aquí se da por lista;
//   * los canales son 16; el F5529 saca CB0-CB7 en P6.0-P6.7 y CB8-CB11 en
//     P7.0-P7.3 [DS SLAS590P, «Terminal Functions»].
//
// LO QUE NO ESTÁ: CBOUT hacia P1.6 y hacia la captura del Timer1_A3 (CCI1B),
// y los mismos huecos que el Comp_E (SHORT, los modos de consumo).
// =============================================================================
#ifndef STM32_PERIPH_TI_COMP_B_H
#define STM32_PERIPH_TI_COMP_B_H

#include "comp_e.h"

namespace stm32 {

class CompB : public CompEP4 {
public:
    CompB(sc_core::sc_module_name nm, uint32_t base, uint32_t size = 0x10) : CompEP4(nm, base, size) {
        n_canales = 16;
        v_refl1 = 1.5;
    }

protected:
    uint16_t lee16(uint32_t off, bool dbg) override {
        if (off == 0x0C) return uint16_t(int_ & 0x0303u);
        return CompEP4::lee16(off, dbg);
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        if (off == 0x0C) { v &= 0x0303u; m &= 0x0303u; int_ = uint16_t((int_ & ~0x1313u) | ((int_ & ~m) | (v & m))); irq_ev_.notify(sc_core::SC_ZERO_TIME); return; }
        CompEP4::escribe16(off, v, m);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_COMP_B_H
