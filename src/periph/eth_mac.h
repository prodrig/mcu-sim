// =============================================================================
// eth_mac.h — Ethernet MAC 10/100 con DMA propio (AHB1; plan P1) [IR, §12.16]
//
// Esclavo de configuración (MAC/MMC/PTP/DMA) + maestro AHB (su DMA lee/escribe
// descriptores y buffers en SRAM: maestro ETH_DMA de la matriz). Interfaz PHY
// MII/RMII como señales digitales (AF11) seleccionada por SYSCFG_PMC.
// =============================================================================
#ifndef STM32_PERIPH_ETH_MAC_H
#define STM32_PERIPH_ETH_MAC_H

#include "../common/periph_base.h"
#include <tlm_utils/simple_initiator_socket.h>

namespace stm32 {

class EthMac : public BusSlave {
public:
    tlm_utils::simple_initiator_socket<EthMac> dma_m{"dma_m"};  // maestro matriz
    sc_core::sc_out<bool> irq{"irq"};                 // IRQ 61
    sc_core::sc_out<bool> wkup_line{"wkup_line"};     // EXTI19 (IRQ 62)
    sc_core::sc_in<bool>  mii_rmii_sel{"mii_rmii_sel"}; // SYSCFG_PMC
    // PHY (subconjunto RMII + gestión MDIO; MII completo TODO(F7))
    sc_core::sc_signal<bool> mdc_out{"mdc_out"};
    sc_core::sc_signal<bool> mdio_out{"mdio_out"}, mdio_oe{"mdio_oe"}, mdio_in{"mdio_in"};
    sc_core::sc_signal<bool> ref_clk_in{"ref_clk_in"};   // RMII 50 MHz (PA1)
    sc_core::sc_signal<bool> crs_dv_in{"crs_dv_in"}, tx_en_out{"tx_en_out"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> rxd_in, txd_out;  // [2] RMII

    EthMac(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::ETH_B, 0x1400),
          rxd_in("rxd_in", 2), txd_out("txd_out", 2) {
        SC_HAS_PROCESS(EthMac);
        SC_THREAD(dma_proc);
    }

protected:
    void dma_proc() {
        // TODO(F7): máquina de descriptores (anillo/cadena) via dma_m,
        //           filtrado MAC, FIFOs TX/RX, PTP timestamps [IR, §12.16]
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ETH_MAC_H
