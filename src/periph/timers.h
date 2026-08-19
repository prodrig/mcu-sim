// =============================================================================
// timers.h — Temporizadores: avanzados (TIM1/8), propósito general (TIM2-5,
//            TIM9-14) y básicos (TIM6/7) [IR, §12.1-12.3/12.8]
//
// Parametrizados por nº de canales, anchura (16/32 bits) y presencia de
// BDTR/RCR (solo avanzados). El reloj de cuenta es TIMCLKx (2xPCLKx si el
// prescaler APB > 1 [IR, §4.4]). Canales de E/S como AfEndpoints (tim_chN).
// =============================================================================
#ifndef STM32_PERIPH_TIMERS_H
#define STM32_PERIPH_TIMERS_H

#include "../common/periph_base.h"

namespace stm32 {

class TimerBase : public BusSlave {
public:
    sc_core::sc_in<bool>  timclk{"timclk"};          // TIMCLK1 o TIMCLK2
    sc_core::sc_in<bool>  freeze{"freeze"};          // DBGMCU_APBx_FZ
    sc_core::sc_out<bool> irq_global{"irq_global"};  // o UP/CC/... según clase
    // Petición de trigger al ADC/DAC (TRGO) y cadena maestro-esclavo ITRx
    sc_core::sc_out<bool> trgo{"trgo"};
    sc_core::sc_vector<sc_core::sc_in<bool>> itr;    // [4] entradas de trigger

    // Señales AF por canal (el top las registra en pin_mux)
    sc_core::sc_vector<sc_core::sc_signal<bool>> ch_out, ch_oe, ch_in;

    TimerBase(sc_core::sc_module_name nm, uint32_t base, unsigned nch,
              bool wide32, bool advanced)
        : BusSlave(nm, base, 0x400), itr("itr", 4),
          ch_out("ch_out", nch ? nch : 1), ch_oe("ch_oe", nch ? nch : 1),
          ch_in("ch_in", nch ? nch : 1),
          nch_(nch), wide32_(wide32), advanced_(advanced) {
        SC_HAS_PROCESS(TimerBase);
        SC_METHOD(count_proc);
        sensitive << timclk.pos();
        dont_initialize();
    }

protected:
    unsigned nch_; bool wide32_, advanced_;
    void count_proc() {
        // TODO(F4): CNT/PSC/ARR, modos up/down/center, captura/comparación,
        //           PWM, dead-time y break (avanzados) [IR, §12.1.4]
    }
    // TODO(F4): mapa de registros completo [IR, §12.1.4-D]
};

// TIM1/TIM8: 4 canales + complementarias, BDTR/RCR, 16 bits, APB2
class TimAdvanced : public TimerBase {
public:
    sc_core::sc_out<bool> irq_brk{"irq_brk"}, irq_up{"irq_up"},
                          irq_trg_com{"irq_trg_com"}, irq_cc{"irq_cc"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> chn_out;  // salidas complementarias
    sc_core::sc_out<bool> dma_up{"dma_up"}, dma_trig{"dma_trig"};
    sc_core::sc_vector<sc_core::sc_out<bool>> dma_cc;      // [4]
    TimAdvanced(sc_core::sc_module_name nm, uint32_t base)
        : TimerBase(nm, base, 4, false, true),
          chn_out("chn_out", 3), dma_cc("dma_cc", 4) {}
};

// TIM2-5 (4 canales; TIM2/5 de 32 bits), TIM9/12 (2), TIM10/11/13/14 (1)
class TimGeneral : public TimerBase {
public:
    sc_core::sc_out<bool> dma_up{"dma_up"};
    sc_core::sc_vector<sc_core::sc_out<bool>> dma_cc;      // [nch]
    TimGeneral(sc_core::sc_module_name nm, uint32_t base, unsigned nch, bool wide32)
        : TimerBase(nm, base, nch, wide32, false), dma_cc("dma_cc", nch) {}
};

// TIM6/7: sin canales; trigger del DAC; IRQ compartida TIM6_DAC [IR, §12.3]
class TimBasic : public TimerBase {
public:
    sc_core::sc_out<bool> dma_up{"dma_up"};
    TimBasic(sc_core::sc_module_name nm, uint32_t base)
        : TimerBase(nm, base, 0, false, false) {}
};

} // namespace stm32
#endif // STM32_PERIPH_TIMERS_H
