// =============================================================================
// power_pads.h — Pines de alimentación, NRST y BOOT0
//
// Modela como nodos analógicos: VDD (x5), VSS, VDDA/VSSA, VREF+, VBAT,
// VCAP1/2, NRST y BOOT0. Entrega al PWR/RCC los niveles en float para POR/
// PDR/BOR/PVD y valida rangos [IR, §2.2]. NRST es bidireccional open-drain
// con pull-up interno: los resets internos también lo llevan a 0 [IR, §4.1].
//
// Fase F3 — supervisión de alimentación completa:
//   * POR/PDR con histéresis: el dispositivo sale de reset al superar VPOR y
//     vuelve a reset al caer por debajo de VPDR;
//   * Brown-Out Reset con el nivel programado en los option bytes
//     (OPTCR.BOR_LEV: 00 -> ~2.7 V, 01 -> ~2.4 V, 10 -> ~2.1 V, 11 -> BOR
//     desactivado, en cuyo caso manda el umbral POR/PDR) [IR, §5.7.1];
//   * distinción de la causa: se publica por separado si la caída la ha
//     detectado el POR/PDR o el BOR, que es lo que diferencia los flags
//     PORRSTF y BORRSTF de RCC_CSR [IR, §4.10];
//   * validación de rango de VDD, VDDA y VBAT y de la diferencia VDD-VDDA
//     (máximo 300 mV) [IR, §2.2].
// =============================================================================
#ifndef STM32_PINS_POWER_PADS_H
#define STM32_PINS_POWER_PADS_H

#include <systemc>
#include <cmath>
#include "../common/analog_net.h"

namespace stm32 {

SC_MODULE(PowerPads) {
    // Nodos de encapsulado (el testbench externo se registra como driver)
    AnalogNet vdd{"vdd"}, vss{"vss"}, vdda{"vdda"}, vssa{"vssa"};
    AnalogNet vref_p{"vref_p"}, vbat{"vbat"}, vcap1{"vcap1"}, vcap2{"vcap2"};
    AnalogNet nrst{"nrst"}, boot0{"boot0"};

    // Salidas digitales hacia RCC/PWR
    sc_core::sc_out<bool>   por_ok{"por_ok"};       // alimentación suficiente
    sc_core::sc_out<bool>   bor_trip{"bor_trip"};   // la caída la detectó el BOR
    sc_core::sc_out<bool>   nrst_in_n{"nrst_in_n"}; // nivel del pin NRST (activo bajo)
    sc_core::sc_out<bool>   boot0_lvl{"boot0_lvl"};
    sc_core::sc_out<double> vdd_lvl{"vdd_lvl"};     // para BOR/PVD (PWR)
    sc_core::sc_out<double> vdda_lvl{"vdda_lvl"};   // para ADC/DAC
    sc_core::sc_out<double> vbat_lvl{"vbat_lvl"};   // dominio de backup

    // Entradas
    sc_core::sc_in<bool>    drive_nrst_low{"drive_nrst_low"};  // reset interno
    sc_core::sc_in<uint8_t> bor_lev{"bor_lev"};                // OPTCR[3:2]

    // --- Umbrales [IR, §5.7.1 para BOR_LEV; §2.2 para los rangos] -----------
    // ⚠ NO DISPONIBLE EN LAS FUENTES con valor numérico: las tensiones exactas
    // de POR/PDR y la histéresis del BOR. Se exponen como parámetros con los
    // valores típicos del dispositivo.
    double v_por      = 1.72;   // VDD por encima: el dispositivo arranca
    double v_pdr      = 1.68;   // VDD por debajo: reset de alimentación
    double v_bor[4]   = {2.70, 2.40, 2.10, 0.0};   // BOR_LEV 00,01,10,11(off)
    double v_bor_hyst = 0.10;   // histéresis de los niveles de BOR
    double v_dd_min   = 1.80, v_dd_max = 3.60;     // rango de operación
    double v_bat_min  = 1.65, v_bat_max = 3.60;

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

    // Umbral de caída vigente (el más alto entre PDR y el BOR programado).
    double trip_level() const {
        const unsigned l = bor_lev.read() & 3u;
        return (v_bor[l] > 0.0) ? v_bor[l] : v_pdr;
    }
    bool bor_active() const { return v_bor[bor_lev.read() & 3u] > 0.0; }

private:
    void nrst_drive_proc() {
        if (drive_nrst_low.read()) nrst.set_drive(id_nrst_drv_, 0.0f, 25.0f);
        else                       nrst.set_hiz(id_nrst_drv_);
    }

    void monitor_proc() {
        for (;;) {
            const double v  = vdd.voltage();
            const double va = vdda.voltage();
            const double vb = vbat.voltage();
            vdd_lvl.write(v);
            vdda_lvl.write(va);
            vbat_lvl.write(vb);

            // --- Supervisión POR/PDR/BOR con histéresis ---------------------
            const double v_off = trip_level();
            const double v_on  = bor_active() ? v_off + v_bor_hyst : v_por;
            const bool prev = ok_;
            if (ok_) { if (v < v_off) ok_ = false; }
            else     { if (v > v_on)  ok_ = true;  }
            if (ok_ != prev || !written_) {
                por_ok.write(ok_);
                // La causa distingue PORRSTF de BORRSTF en RCC_CSR [IR, §4.10]
                bor_trip.write(!ok_ && bor_active() && v >= v_pdr);
                written_ = true;
            }

            // --- Validación de rangos [IR, §2.2] ---------------------------
            check_range(v,  v_dd_min,  v_dd_max,  warn_vdd_,  "VDD fuera de 1.8-3.6 V");
            check_range(va, v_dd_min,  v_dd_max,  warn_vdda_, "VDDA fuera de 1.8-3.6 V");
            check_range(vb, v_bat_min, v_bat_max, warn_vbat_, "VBAT fuera de 1.65-3.6 V");
            if (v > 0.5 && va > 0.5 && std::fabs(v - va) > 0.3 && !warn_diff_) {
                warn_diff_ = true;
                SC_REPORT_WARNING("power", "|VDD - VDDA| > 300 mV [IR, 2.2]");
            }

            // NRST y BOOT0 se comparan contra la alimentación real del pad
            const double vref = (v > 0.5) ? v : 3.3;
            nrst_in_n.write(nrst.voltage() > 0.7 * vref);
            boot0_lvl.write(boot0.voltage() > 0.5 * vref);

            wait(nrst.value_changed_event() | vdd.value_changed_event() |
                 boot0.value_changed_event() | vdda.value_changed_event() |
                 vbat.value_changed_event() | bor_lev.value_changed_event());
        }
    }

    static void check_range(double v, double lo, double hi, bool& warned,
                            const char* msg) {
        if (v > 0.5 && (v < lo || v > hi)) {
            if (!warned) { warned = true; SC_REPORT_WARNING("power", msg); }
        } else if (v <= 0.5) {
            warned = false;              // apagado: se rearma el aviso
        }
    }

    int  id_nrst_pu_ = -1, id_nrst_drv_ = -1;
    bool ok_ = false, written_ = false;
    bool warn_vdd_ = false, warn_vdda_ = false, warn_vbat_ = false, warn_diff_ = false;
};

} // namespace stm32
#endif // STM32_PINS_POWER_PADS_H
