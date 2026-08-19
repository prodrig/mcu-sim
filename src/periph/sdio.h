// =============================================================================
// sdio.h — Interfaz SDIO (APB2; plan P2) [IR, §12.17]
// =============================================================================
#ifndef STM32_PERIPH_SDIO_H
#define STM32_PERIPH_SDIO_H

#include "../common/periph_base.h"

namespace stm32 {

class Sdio : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};                        // IRQ 49
    sc_core::sc_out<bool> dma_req{"dma_req"};                // DMA2 S3C4/S6C4
    sc_core::sc_in<bool>  sdioclk{"sdioclk"};                // PLL48CK
    // AF: CK, CMD (bidir), D0-D7 (bidir)
    sc_core::sc_signal<bool> ck_out{"ck_out"};
    sc_core::sc_signal<bool> cmd_out{"cmd_out"}, cmd_oe{"cmd_oe"}, cmd_in{"cmd_in"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> d_out, d_oe, d_in;   // [8]

    Sdio(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::SDIO_B, 0x400),
          d_out("d_out", 8), d_oe("d_oe", 8), d_in("d_in", 8) {
        SC_HAS_PROCESS(Sdio);
        SC_THREAD(proto_proc);
    }

protected:
    void proto_proc() {
        // TODO(F7): CPSM/DPSM, CLKDIV, FIFO 32 palabras, respuestas R1-R7,
        //           CRC de comando/datos [IR, §12.17]
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_SDIO_H
