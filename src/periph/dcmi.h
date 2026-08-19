// =============================================================================
// dcmi.h — Interfaz de cámara digital (AHB2; plan P1/P2) [IR, §12.22]
// =============================================================================
#ifndef STM32_PERIPH_DCMI_H
#define STM32_PERIPH_DCMI_H

#include "../common/periph_base.h"

namespace stm32 {

class Dcmi : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};                     // IRQ 78
    sc_core::sc_out<bool> dma_req{"dma_req"};             // DMA2 S1C1/S7C1
    // AF13: D0-13, HSYNC, VSYNC, PIXCLK (entradas desde el sensor)
    sc_core::sc_vector<sc_core::sc_signal<bool>> d_in;    // [14]
    sc_core::sc_signal<bool> hsync_in{"hsync_in"}, vsync_in{"vsync_in"},
                             pixclk_in{"pixclk_in"};

    Dcmi(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::DCMI_B, 0x400), d_in("d_in", 14) {
        SC_HAS_PROCESS(Dcmi);
        SC_METHOD(capture_proc);
        sensitive << pixclk_in;
        dont_initialize();
    }

protected:
    void capture_proc() {
        // TODO(F7): captura por PIXCLK con polaridades, sincronismo HW o
        //           embebido, crop, JPEG, FIFO 4 palabras -> dma_req
    }
};

} // namespace stm32
#endif // STM32_PERIPH_DCMI_H
