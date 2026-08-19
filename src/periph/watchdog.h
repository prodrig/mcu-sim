// =============================================================================
// watchdog.h — WWDG (PCLK1/4096) e IWDG (LSI, dominio independiente)
// [IR, §12.10/12.11]. Ambos generan petición de reset hacia RCC.reset_ctrl.
// =============================================================================
#ifndef STM32_PERIPH_WATCHDOG_H
#define STM32_PERIPH_WATCHDOG_H

#include "../common/periph_base.h"

namespace stm32 {

class Wwdg : public BusSlave {
public:
    sc_core::sc_out<bool> irq_ewi{"irq_ewi"};       // IRQ 0 (aviso temprano)
    sc_core::sc_out<bool> rst_req{"rst_req"};       // -> RCC
    sc_core::sc_in<bool>  freeze{"freeze"};         // DBGMCU
    Wwdg(sc_core::sc_module_name nm) : BusSlave(nm, addr::WWDG_B, 0x400) {
        SC_HAS_PROCESS(Wwdg);
        SC_METHOD(count_proc);
        sensitive << clk.pos();
        dont_initialize();
    }
protected:
    void count_proc() {
        // TODO(F5): T[6:0] descendente PCLK1/4096/2^WDGTB; reset si T pasa a
        //           0x3F o refresco con T > W [IR, §12.10]
    }
};

class Iwdg : public BusSlave {
public:
    sc_core::sc_in<bool>  lsi_clk{"lsi_clk"};       // reloj propio (LSI)
    sc_core::sc_out<bool> rst_req{"rst_req"};       // -> RCC
    sc_core::sc_in<bool>  freeze{"freeze"};
    sc_core::sc_in<bool>  hw_start{"hw_start"};     // option bit WDG_SW=0
    Iwdg(sc_core::sc_module_name nm) : BusSlave(nm, addr::IWDG_B, 0x400) {
        SC_HAS_PROCESS(Iwdg);
        SC_METHOD(count_proc);
        sensitive << lsi_clk.pos();
        dont_initialize();
    }
protected:
    void count_proc() {
        // TODO(F5): KR llaves 0xAAAA/0x5555/0xCCCC, PR/RLR con sincronización
        //           de dominio (SR.PVU/RVU), downcounter 12 bits [IR, §12.11]
    }
};

} // namespace stm32
#endif // STM32_PERIPH_WATCHDOG_H
