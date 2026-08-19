// =============================================================================
// usart.h — USART/UART (6 instancias) [IR, §12.4]
// AF: tx/rx/ck/cts/rts como señales digitales registradas en pin_mux.
// =============================================================================
#ifndef STM32_PERIPH_USART_H
#define STM32_PERIPH_USART_H

#include "../common/periph_base.h"

namespace stm32 {

class Usart : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // Señales AF (el top registra los endpoints en pin_mux)
    sc_core::sc_signal<bool> tx_out{"tx_out"}, tx_oe{"tx_oe"};
    sc_core::sc_signal<bool> rx_in{"rx_in"};
    sc_core::sc_signal<bool> ck_out{"ck_out"};                 // solo USART
    sc_core::sc_signal<bool> cts_in{"cts_in"}, rts_out{"rts_out"};

    // has_sync=false para UART4/5 (sin CK/CTS/RTS)
    Usart(sc_core::sc_module_name nm, uint32_t base, bool has_sync = true)
        : BusSlave(nm, base, 0x400), has_sync_(has_sync) {
        SC_HAS_PROCESS(Usart);
        SC_THREAD(shift_proc);
    }

protected:
    bool has_sync_;
    void shift_proc() {
        // TODO(F4): baud desde BRR/OVER8 con pclk_hz; registros SR/DR/CR1-3
        //           [IR, §12.4.3]; bit-level en tx_out/rx_in; IRQ y DRQ.
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_USART_H
