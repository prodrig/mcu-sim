// =============================================================================
// otg.h — USB OTG FS (AHB2) y OTG HS (AHB1, con DMA interno) [IR, §12.15/12.23]
//
// Clase común parametrizada: el HS añade el puerto maestro AHB (su DMA es el
// 8º maestro de la matriz), la interfaz ULPI y 4 KB de FIFO; el FS usa PHY
// integrado (D+/D- van a la ruta analógica de PA11/PA12: los estados J/K/SE0
// se modelan sobre los nodos float) y 1.25 KB de FIFO.
// =============================================================================
#ifndef STM32_PERIPH_OTG_H
#define STM32_PERIPH_OTG_H

#include "../common/periph_base.h"
#include "../common/analog_net.h"
#include <tlm_utils/simple_initiator_socket.h>

namespace stm32 {

class OtgCtrl : public BusSlave {
public:
    sc_core::sc_out<bool> irq_global{"irq_global"};      // IRQ 67 (FS) / 77 (HS)
    sc_core::sc_out<bool> wkup_line{"wkup_line"};        // EXTI18 (FS) / EXTI20 (HS)
    sc_core::sc_in<bool>  clk48{"clk48"};                // PLL48CK

    OtgCtrl(sc_core::sc_module_name nm, uint32_t base, uint32_t size, bool hs)
        : BusSlave(nm, base, size), hs_(hs) {
        SC_HAS_PROCESS(OtgCtrl);
        SC_THREAD(engine_proc);
    }

    // FS: PHY integrado sobre los nodos analógicos de PA11 (DM) / PA12 (DP)
    void bind_phy(analog_net_if& dm, analog_net_if& dp) {
        dm_ = &dm; dp_ = &dp;
        // TODO(F7): registrar drivers y modelar J/K/SE0, pull-up DP (device)
    }

protected:
    bool hs_;
    analog_net_if *dm_ = nullptr, *dp_ = nullptr;
    void engine_proc() {
        // TODO(F7): grupos de registros core/host/device/FIFOs [IR, §12.15],
        //           FSM OTG (ID/SRP/HNP), endpoints y canales
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

class OtgFs : public OtgCtrl {
public:
    OtgFs(sc_core::sc_module_name nm)
        : OtgCtrl(nm, addr::OTG_FS_B, 0x40000, false) {}
    sc_core::sc_out<bool> irq_wkup{"irq_wkup"};          // IRQ 42 (via EXTI18)
    // ID / VBUS por pads PA10/PA9 (ruta analógica: sensado de VBUS en float)
};

class OtgHs : public OtgCtrl {
public:
    tlm_utils::simple_initiator_socket<OtgHs> dma_m{"dma_m"};  // maestro matriz
    // IRQs dedicadas de endpoint 1 [IR, §12.23]
    sc_core::sc_out<bool> irq_ep1_out{"irq_ep1_out"}, irq_ep1_in{"irq_ep1_in"};
    // Interfaz ULPI (AF10) hacia PHY externo
    sc_core::sc_signal<bool> ulpi_ck_in{"ulpi_ck_in"}, ulpi_dir_in{"ulpi_dir_in"},
                             ulpi_nxt_in{"ulpi_nxt_in"}, ulpi_stp_out{"ulpi_stp_out"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> ulpi_d_out, ulpi_d_oe, ulpi_d_in; // [8]

    OtgHs(sc_core::sc_module_name nm)
        : OtgCtrl(nm, addr::OTG_HS_B, 0x40000, true),
          ulpi_d_out("ulpi_d_out", 8), ulpi_d_oe("ulpi_d_oe", 8),
          ulpi_d_in("ulpi_d_in", 8) {}
};

} // namespace stm32
#endif // STM32_PERIPH_OTG_H
