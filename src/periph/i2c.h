// =============================================================================
// i2c.h — I2C1/2/3 [IR, §12.6]. SDA/SCL son open-drain (lo impone el pad).
// =============================================================================
#ifndef STM32_PERIPH_I2C_H
#define STM32_PERIPH_I2C_H

#include "../common/periph_base.h"

namespace stm32 {

class I2c : public BusSlave {
public:
    sc_core::sc_out<bool> irq_ev{"irq_ev"}, irq_er{"irq_er"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // AF (open-drain: oe=1 conduce 0; '1' = liberar línea)
    sc_core::sc_signal<bool> scl_out{"scl_out"}, scl_oe{"scl_oe"}, scl_in{"scl_in"};
    sc_core::sc_signal<bool> sda_out{"sda_out"}, sda_oe{"sda_oe"}, sda_in{"sda_in"};
    sc_core::sc_signal<bool> smba_out{"smba_out"}, smba_in{"smba_in"};

    I2c(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x400) {
        SC_HAS_PROCESS(I2c);
        SC_THREAD(fsm_proc);
    }

protected:
    void fsm_proc() {
        // TODO(F5): FSM Start/Addr/Ack/Data/Stop con eventos EV5-EV9, clock
        //           stretching, multimaster/arbitraje, CCR/TRISE [IR, §12.6.3]
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_I2C_H
