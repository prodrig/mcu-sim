// =============================================================================
// power_pads.h — Pines de alimentación, NRST y BOOT0
//
// Modela como nodos analógicos: VDD (x5), VSS, VDDA/VSSA, VREF+, VBAT,
// VCAP1/2, NRST y BOOT0. Entrega al PWR/RCC los niveles en float para POR/
// PDR/BOR/PVD y valida rangos [IR, §2.2]. NRST es bidireccional open-drain
// con pull-up interno: los resets internos también lo llevan a 0 [IR, §4.1].
// =============================================================================
#ifndef STM32_PINS_POWER_PADS_H
#define STM32_PINS_POWER_PADS_H

#include <systemc>
#include "../common/analog_net.h"

namespace stm32 {

SC_MODULE(PowerPads) {
    // Nodos de encapsulado (el testbench externo se registra como driver)
    AnalogNet vdd{"vdd"}, vss{"vss"}, vdda{"vdda"}, vssa{"vssa"};
    AnalogNet vref_p{"vref_p"}, vbat{"vbat"}, vcap1{"vcap1"}, vcap2{"vcap2"};
    AnalogNet nrst{"nrst"}, boot0{"boot0"};

    // Salidas digitales hacia RCC/PWR
    sc_core::sc_out<bool>   por_ok{"por_ok"};       // VDD por encima de POR/PDR
    sc_core::sc_out<bool>   nrst_in_n{"nrst_in_n"}; // nivel del pin NRST (activo bajo)
    sc_core::sc_out<bool>   boot0_lvl{"boot0_lvl"};
    sc_core::sc_out<double> vdd_lvl{"vdd_lvl"};     // para BOR/PVD (PWR)
    sc_core::sc_out<double> vdda_lvl{"vdda_lvl"};   // para ADC/DAC

    // Entrada: petición de reset interna (reset_ctrl) -> NRST a 0 (open-drain)
    sc_core::sc_in<bool> drive_nrst_low{"drive_nrst_low"};

    SC_CTOR(PowerPads) {
        SC_THREAD(monitor_proc);
        SC_METHOD(nrst_drive_proc);
        sensitive << drive_nrst_low;
        dont_initialize();
    }

    void end_of_elaboration() override {
        id_nrst_pu_  = nrst.register_driver("pullup");   // pull-up interno 40k
        id_nrst_drv_ = nrst.register_driver("mcu_od");
        nrst.set_drive(id_nrst_pu_, 3.3f, 40e3f);
        nrst.set_hiz(id_nrst_drv_);
    }

private:
    void nrst_drive_proc() {
        if (drive_nrst_low.read()) nrst.set_drive(id_nrst_drv_, 0.0f, 25.0f);
        else                       nrst.set_hiz(id_nrst_drv_);
    }

    void monitor_proc() {
        for (;;) {
            const double v = vdd.voltage();
            vdd_lvl.write(v);
            vdda_lvl.write(vdda.voltage());
            por_ok.write(v > 1.7);                       // umbral POR aprox. TODO(F3)
            nrst_in_n.write(nrst.voltage() > 0.7 * (v > 0.5 ? v : 3.3));
            boot0_lvl.write(boot0.voltage() > 0.5 * (v > 0.5 ? v : 3.3));
            wait(nrst.value_changed_event() | vdd.value_changed_event() |
                 boot0.value_changed_event() | vdda.value_changed_event());
        }
    }

    int id_nrst_pu_ = -1, id_nrst_drv_ = -1;
};

} // namespace stm32
#endif // STM32_PINS_POWER_PADS_H
