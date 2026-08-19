// =============================================================================
// exti.h — Controlador EXTI (APB2; plan P2: fuera del SCB) [IR, §9.4]
//
// 23 líneas: 0-15 desde pines GPIO (el mux de puerto lo dictan los registros
// SYSCFG_EXTICR, entrada exticr_sel), 16-22 internas (PVD, RTC, USB, ETH).
// Salidas: IRQs (6,7,8,9,10,23,40 y las dedicadas) y eventos/wakeup a PWR/CPU.
// =============================================================================
#ifndef STM32_PERIPH_EXTI_H
#define STM32_PERIPH_EXTI_H

#include "../common/periph_base.h"
#include "../pins/pin_mux.h"

namespace stm32 {

class Exti : public BusSlave {
public:
    // Entradas de pin: las 16 líneas de cada uno de los 9 puertos.
    sc_core::sc_vector<sc_core::sc_in<bool>> gpio_line;   // [9*16]
    // Selección de puerto por línea 0-15 (SYSCFG_EXTICRx) [IR, §9.4.3]
    sc_core::sc_vector<sc_core::sc_in<uint8_t>> exticr_sel; // [16] 0=A..8=I
    // Líneas internas 16..22
    sc_core::sc_in<bool> l16_pvd{"l16_pvd"}, l17_rtc_alarm{"l17_rtc_alarm"},
                         l18_otgfs_wkup{"l18_otgfs_wkup"}, l19_eth_wkup{"l19_eth_wkup"},
                         l20_otghs_wkup{"l20_otghs_wkup"}, l21_rtc_tamp{"l21_rtc_tamp"},
                         l22_rtc_wkup{"l22_rtc_wkup"};
    // Salidas de interrupción hacia el NVIC
    sc_core::sc_out<bool> irq_exti0{"irq_exti0"}, irq_exti1{"irq_exti1"},
                          irq_exti2{"irq_exti2"}, irq_exti3{"irq_exti3"},
                          irq_exti4{"irq_exti4"}, irq_exti9_5{"irq_exti9_5"},
                          irq_exti15_10{"irq_exti15_10"};
    sc_core::sc_out<bool> irq_pvd{"irq_pvd"}, irq_rtc_alarm{"irq_rtc_alarm"},
                          irq_otgfs_wkup{"irq_otgfs_wkup"}, irq_eth_wkup{"irq_eth_wkup"},
                          irq_otghs_wkup{"irq_otghs_wkup"}, irq_tamp{"irq_tamp"},
                          irq_rtc_wkup{"irq_rtc_wkup"};
    // Eventos y wakeup (WFE / salida de Stop) [IR, §14]
    sc_core::sc_out<bool> event_out{"event_out"};
    sc_core::sc_out<bool> wakeup{"wakeup"};

    Exti(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::EXTI_B, 0x400),
          gpio_line("gpio_line", N_GPIO_PORTS * N_PORT_PINS),
          exticr_sel("exticr_sel", 16) {
        SC_HAS_PROCESS(Exti);
        SC_METHOD(edge_proc);
        for (unsigned i = 0; i < gpio_line.size(); ++i) sensitive << gpio_line[i];
        sensitive << l16_pvd << l17_rtc_alarm << l18_otgfs_wkup << l19_eth_wkup
                  << l20_otghs_wkup << l21_rtc_tamp << l22_rtc_wkup;
        dont_initialize();
    }

protected:
    void edge_proc() {
        // TODO(F4): IMR/EMR/RTSR/FTSR/SWIER/PR con detección de flanco por
        //           línea; agrupación 9_5 y 15_10; PR rc_w1 [IR, §9.4.2]
    }
};

} // namespace stm32
#endif // STM32_PERIPH_EXTI_H
