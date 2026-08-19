// =============================================================================
// adc.h — Bloque ADC: ADC1/2/3 + registros comunes (APB2; plan P1) [IR, §12.13]
//
// Entradas analógicas en float: los canales externos leen la tensión del nodo
// del pad (via pin_mux.analog(), modo analógico); canales internos: sensor de
// temperatura, VREFINT y VBAT/2 (CCR). Conversión SAR con tiempo de muestreo
// SMPR y reloj ADCCLK = PCLK2/ADCPRE (máx 36 MHz).
// =============================================================================
#ifndef STM32_PERIPH_ADC_H
#define STM32_PERIPH_ADC_H

#include "../common/periph_base.h"
#include "../common/analog_net.h"
#include <array>

namespace stm32 {

class AdcBlock : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};                    // IRQ 18 (común)
    sc_core::sc_out<bool> dma_req_adc1{"dma_req_adc1"},
                          dma_req_adc2{"dma_req_adc2"},
                          dma_req_adc3{"dma_req_adc3"};
    sc_core::sc_in<double> vdda{"vdda"};                 // PowerPads
    sc_core::sc_in<double> vref{"vref"};
    // Triggers de temporizadores (TRGO/CC) y EXTI11/EXTI15 [IR, §12.13]
    sc_core::sc_vector<sc_core::sc_in<bool>> trig_regular;   // [8]
    sc_core::sc_vector<sc_core::sc_in<bool>> trig_injected;  // [8]

    AdcBlock(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::ADC_B, 0x400),
          trig_regular("trig_regular", 8), trig_injected("trig_injected", 8) {
        ch_.fill(nullptr);
        SC_HAS_PROCESS(AdcBlock);
        SC_THREAD(convert_proc);
    }

    // Registro de canales externos (elaboración; lo llama el top):
    // ch 0..15 -> nodo analógico del pad correspondiente [IR, §12.13-E].
    void bind_channel(unsigned ch, analog_net_if& net) { ch_[ch] = &net; }

protected:
    std::array<analog_net_if*, 16> ch_;
    void convert_proc() {
        // TODO(F5): secuenciador SQR/JSQR, sample&hold SMPR, aproximaciones
        //           sucesivas: code = round(V/Vref * 4095) con alineación,
        //           modos dual/triple (CCR.MULTI), watchdog analógico.
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ADC_H
