// =============================================================================
// debug.h — Subsistema de depuración CoreSight + DBGMCU
//
// DAP = SWJ-DP (JTAG+SWD conmutables) + AHB-AP (plan P3) [IR, §13.2]. El
// AHB-AP es un iniciador más hacia el router del núcleo (acceso a todo el
// mapa, PPB incluido). Componentes de traza: FPB, DWT, ITM, ETM, TPIU +
// ROM table [IR, §13.3-13.8]. DBGMCU (bloque ST, 0xE0042000) congela
// temporizadores/watchdogs con el núcleo parado [IR, §13.9].
// =============================================================================
#ifndef STM32_CORE_DEBUG_H
#define STM32_CORE_DEBUG_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/ahb_types.h"

namespace stm32 {

// Nº de líneas de congelación DBGMCU_APB1_FZ/APB2_FZ que reparte el top
enum FreezeId : unsigned {
    FZ_TIM2, FZ_TIM3, FZ_TIM4, FZ_TIM5, FZ_TIM6, FZ_TIM7, FZ_TIM12, FZ_TIM13,
    FZ_TIM14, FZ_WWDG, FZ_IWDG, FZ_I2C1, FZ_I2C2, FZ_I2C3, FZ_CAN1, FZ_CAN2,
    FZ_TIM1, FZ_TIM8, FZ_TIM9, FZ_TIM10, FZ_TIM11, FZ_COUNT
};

SC_MODULE(DebugSys) {
    // Interfaz física (via pin_mux AF0: PA13/PA14/PA15/PB3/PB4) [IR, §13.1]
    sc_core::sc_in<bool>  swclk_tck{"swclk_tck"};
    sc_core::sc_in<bool>  swdio_in{"swdio_in"};
    sc_core::sc_out<bool> swdio_out{"swdio_out"};
    sc_core::sc_out<bool> swdio_oe{"swdio_oe"};
    sc_core::sc_in<bool>  jtdi{"jtdi"};
    sc_core::sc_out<bool> jtdo_swo{"jtdo_swo"};       // JTDO o SWO (traza async)
    sc_core::sc_in<bool>  njtrst{"njtrst"};

    // Maestro AHB-AP hacia el router del núcleo
    tlm_utils::simple_initiator_socket<DebugSys> ahb_ap{"ahb_ap"};
    // Esclavo PPB: DHCSR/DCRSR/DCRDR/DEMCR, FPB, DWT, ITM, TPIU, ETM,
    // ROM table y DBGMCU (decode interno por rangos [IR, §13.3])
    tlm_utils::simple_target_socket<DebugSys> ppb{"ppb"};

    // Control del núcleo y congelación de periféricos
    sc_core::sc_out<bool> halt_req{"halt_req"};       // DHCSR.C_HALT -> CPU
    sc_core::sc_in<bool>  halted{"halted"};
    sc_core::sc_vector<sc_core::sc_out<bool>> freeze; // [FreezeId] -> timers/WDG

    SC_CTOR(DebugSys) : freeze("freeze", FZ_COUNT) {
        ppb.register_b_transport(this, &DebugSys::bt);
        SC_METHOD(swj_proc);
        sensitive << swclk_tck.pos() << swclk_tck.neg();
        dont_initialize();
    }

    // -----------------------------------------------------------------------
    // Transactor del AHB-AP (fase F1)
    //
    // Es la vía por la que una sonda de depuración lee y escribe la memoria del
    // sistema: emite sobre el router del núcleo, de modo que ve el mismo mapa
    // que la CPU (CCM incluida, alias de 0x0, PPB y bit-banding) [IR, §13.2].
    // En F6 lo gobernará la FSM SWD/JTAG a través de los registros CSW/TAR/DRW;
    // aquí se expone como API para el banco de pruebas y el cargador.
    // Debe invocarse desde un proceso (consume el tiempo anotado).
    // -----------------------------------------------------------------------
    tlm::tlm_response_status ap_access(bool write, uint64_t a,
                                       unsigned char* d, unsigned len) {
        tlm::tlm_generic_payload gp;
        AhbExt ext;
        ext.master     = BusMaster::CORE_SBUS;   // el AP se presenta como S-bus
        ext.privileged = true;
        gp_setup(gp, write, a, d, len);
        gp.set_extension(&ext);
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        ahb_ap->b_transport(gp, t);
        sc_core::wait(t);
        gp.clear_extension(&ext);
        return gp.get_response_status();
    }
    tlm::tlm_response_status ap_write32(uint64_t a, uint32_t v) {
        unsigned char b[4];
        for (unsigned i = 0; i < 4; ++i) b[i] = uint8_t(v >> (8 * i));
        return ap_access(true, a, b, 4);
    }
    tlm::tlm_response_status ap_read32(uint64_t a, uint32_t& v) {
        unsigned char b[4] = {0, 0, 0, 0};
        const auto r = ap_access(false, a, b, 4);
        v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
            (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        return r;
    }

private:
    void swj_proc() {
        // TODO(F6): FSM SWJ-DP (secuencia de conmutación JTAG<->SWD), protocolo
        //           SWD (paquetes req/ack/data), TAP JTAG; DP regs (IDCODE,
        //           CTRL/STAT, SELECT) y AHB-AP (CSW/TAR/DRW) -> ahb_ap->...
    }
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        // TODO(F6): decode E000EDF0 core-debug / E0000000 ITM / E0001000 DWT /
        //           E0002000 FPB / E0040000 TPIU / E0041000 ETM /
        //           E0042000 DBGMCU / E00FF000 ROM table
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace stm32
#endif // STM32_CORE_DEBUG_H
