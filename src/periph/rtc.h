// =============================================================================
// rtc.h — RTC del dominio backup (APB1; plan P2: ya no es hijo de RCC)
// [IR, §12.9]. Recibe RTCCLK del mux de RCC; alarmas/wakeup/tamper salen por
// líneas EXTI 17/22/21. Incluye los 20 registros de backup.
// =============================================================================
#ifndef STM32_PERIPH_RTC_H
#define STM32_PERIPH_RTC_H

#include "../common/periph_base.h"

namespace stm32 {

class Rtc : public BusSlave {
public:
    sc_core::sc_in<bool>   rtcclk{"rtcclk"};
    sc_core::sc_in<double> rtcclk_hz{"rtcclk_hz"};
    sc_core::sc_in<bool>   bkp_rst_n{"bkp_rst_n"};   // reset de dominio backup
    sc_core::sc_in<bool>   dbp{"dbp"};               // PWR_CR.DBP (protección)
    sc_core::sc_out<bool>  exti17_alarm{"exti17_alarm"};
    sc_core::sc_out<bool>  exti21_tamp_ts{"exti21_tamp_ts"};
    sc_core::sc_out<bool>  exti22_wakeup{"exti22_wakeup"};
    // AF adicionales: RTC_OUT/RTC_TAMP1/RTC_TS en PC13 [IR, §2.1]
    sc_core::sc_signal<bool> af1_out{"af1_out"}, af1_in{"af1_in"};

    Rtc(sc_core::sc_module_name nm) : BusSlave(nm, addr::RTC_B, 0x400) {
        SC_HAS_PROCESS(Rtc);
        SC_METHOD(tick_proc);
        sensitive << rtcclk.pos();
        dont_initialize();
    }

protected:
    void tick_proc() {
        // TODO(F5): prescalers PREDIV_A/S, calendario BCD, alarmas A/B con
        //           subsegundos, wakeup timer, timestamp/tamper, WPR
        //           (0xCA/0x53), BKPxR [IR, §12.9]
    }
};

} // namespace stm32
#endif // STM32_PERIPH_RTC_H
