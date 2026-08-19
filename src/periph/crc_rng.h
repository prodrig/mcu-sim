// =============================================================================
// crc_rng.h — Unidad CRC (AHB1; plan P1) y RNG (AHB2; plan P1)
// [IR, §12.19/12.20]
// =============================================================================
#ifndef STM32_PERIPH_CRC_RNG_H
#define STM32_PERIPH_CRC_RNG_H

#include "../common/periph_base.h"

namespace stm32 {

class CrcUnit : public BusSlave {
public:
    CrcUnit(sc_core::sc_module_name nm) : BusSlave(nm, addr::CRC_B, 0x400) {}
protected:
    uint32_t reg_read(uint32_t) override { return 0xFFFFFFFF; }   // TODO(F5)
    void     reg_write(uint32_t, uint32_t, uint32_t) override {}
    // TODO(F5): CRC-32 polinomio 0x4C11DB7, 1 ciclo AHB/palabra; DR/IDR/CR
};

class Rng : public BusSlave {
public:
    sc_core::sc_out<bool>  irq{"irq"};              // HASH_RNG (IRQ 80)
    sc_core::sc_in<bool>   pll48ck{"pll48ck"};      // reloj propio [IR, §12.19]
    sc_core::sc_in<double> hclk_hz{"hclk_hz"};      // detección CECS (48ck<hclk/16)
    Rng(sc_core::sc_module_name nm) : BusSlave(nm, addr::RNG_B, 0x400) {
        SC_HAS_PROCESS(Rng);
        SC_METHOD(gen_proc);
        sensitive << pll48ck.pos();
        dont_initialize();
    }
protected:
    void gen_proc() {
        // TODO(F5): LFSR/PRNG determinista con semilla configurable (para
        //           reproducibilidad), DRDY cada ~40 ciclos, SEIS/CEIS
    }
};

} // namespace stm32
#endif // STM32_PERIPH_CRC_RNG_H
