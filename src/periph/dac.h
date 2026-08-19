// =============================================================================
// dac.h — DAC de 2 canales (APB1) [IR, §12.14]
// Salidas analógicas: el DAC se registra como driver Thevenin de los nodos de
// PA4 (OUT1) y PA5 (OUT2) con Rout según buffer on/off.
// =============================================================================
#ifndef STM32_PERIPH_DAC_H
#define STM32_PERIPH_DAC_H

#include "../common/periph_base.h"
#include "../common/analog_net.h"

namespace stm32 {

class Dac : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};                    // underrun -> IRQ 54 (TIM6_DAC)
    sc_core::sc_out<bool> dma_req_ch1{"dma_req_ch1"}, dma_req_ch2{"dma_req_ch2"};
    sc_core::sc_in<double> vref{"vref"};
    sc_core::sc_vector<sc_core::sc_in<bool>> trig;       // [8] TIM TRGO / EXTI9 / SW

    Dac(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::DAC_B, 0x400), trig("trig", 8) {
        SC_HAS_PROCESS(Dac);
        SC_METHOD(update_out);
        sensitive << ev_data_;
        dont_initialize();
    }

    void bind_out(unsigned ch, analog_net_if& net) {     // PA4 / PA5
        net_[ch] = &net;
        id_[ch] = net.register_driver(ch == 0 ? "dac1" : "dac2");
    }

protected:
    analog_net_if* net_[2] = {nullptr, nullptr};
    int id_[2] = {-1, -1};
    sc_core::sc_event ev_data_;
    void update_out() {
        // TODO(F5): V = DOR/4095 * VREF; Rout ~15k sin buffer / ~50 con buffer;
        //           t_SETTLING; ondas ruido/triángulo (WAVE/MAMP) [IR, §12.14]
    }
};

} // namespace stm32
#endif // STM32_PERIPH_DAC_H
