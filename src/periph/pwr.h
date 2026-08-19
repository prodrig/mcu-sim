// =============================================================================
// pwr.h — Controlador de energía (APB1) [IR, §12.21/14]
// Máquina de estados Run/Sleep/Stop/Standby coordinada con CPU (WFI/WFE +
// SLEEPDEEP) y RCC; PVD/BOR desde el nivel real de VDD (PowerPads); DBP
// protege el dominio backup; pin WKUP (PA0) y flags WUF/SBF.
// =============================================================================
#ifndef STM32_PERIPH_PWR_H
#define STM32_PERIPH_PWR_H

#include "../common/periph_base.h"

namespace stm32 {

class Pwr : public BusSlave {
public:
    sc_core::sc_in<double> vdd_lvl{"vdd_lvl"};        // PowerPads
    sc_core::sc_in<bool>   sleeping{"sleeping"};      // CPU (WFI/WFE)
    sc_core::sc_in<bool>   sleepdeep{"sleepdeep"};    // SCB.SCR via CPU
    sc_core::sc_in<bool>   wkup_pin{"wkup_pin"};      // PA0 (EWUP)
    sc_core::sc_in<bool>   exti_wakeup{"exti_wakeup"};// EXTI (salida de Stop)
    sc_core::sc_out<bool>  irq_pvd{"irq_pvd"};        // via EXTI16
    sc_core::sc_out<bool>  dbp{"dbp"};                // acceso dominio backup
    sc_core::sc_out<bool>  standby_req{"standby_req"};// -> RCC (apagar 1.2V)
    sc_core::sc_out<bool>  stop_req{"stop_req"};      // -> RCC (parar relojes)
    sc_core::sc_out<bool>  vos_rdy{"vos_rdy"};

    Pwr(sc_core::sc_module_name nm) : BusSlave(nm, addr::PWR_B, 0x400) {
        SC_HAS_PROCESS(Pwr);
        SC_METHOD(power_fsm);
        sensitive << sleeping << sleepdeep << exti_wakeup << wkup_pin << vdd_lvl;
        dont_initialize();
    }

protected:
    void power_fsm() {
        // TODO(F7): CR (PDDS/LPDS/CWUF/CSBF/PVDE/PLS/DBP/FPDS/VOS) y CSR
        //           (WUF/SBF/PVDO/BRR/EWUP/BRE) [IR, §14.6]; entrada/salida de
        //           Stop y Standby (Standby => reset tipo POR con SBF=1).
    }
};

} // namespace stm32
#endif // STM32_PERIPH_PWR_H
