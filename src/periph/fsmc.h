// =============================================================================
// fsmc.h — Flexible Static Memory Controller (AHB3; plan P2) [IR, §12.18]
//
// Dos caras: esclavo de registros (0xA000 0000) y esclavo de memoria externa
// (0x6000 0000-0x9FFF FFFF, esclavo FSMC de la matriz). El bus externo
// (D0-15, A0-23, NOE/NWE/NEx/NBL/NWAIT/CLK) sale como AF por pin_mux; para
// simular memorias externas el testbench puede conectarse al bus digital o
// directamente al socket ext_mem (modelo de memoria TLM).
// =============================================================================
#ifndef STM32_PERIPH_FSMC_H
#define STM32_PERIPH_FSMC_H

#include "../common/periph_base.h"
#include <tlm_utils/simple_initiator_socket.h>

namespace stm32 {

class Fsmc : public sc_core::sc_module {
public:
    // Único puerto esclavo desde la matriz (S6): cubre los bancos externos
    // 0x6000_0000-0x9FFF_FFFF y los registros en 0xA000_0000 (decode interno).
    tlm_utils::simple_target_socket<Fsmc> mem{"mem"};
    // Punto de conexión para modelos de memoria externa del testbench (TLM)
    tlm_utils::simple_initiator_socket<Fsmc> ext_mem{"ext_mem"};
    sc_core::sc_in<bool>  hclk{"hclk"};
    sc_core::sc_in<bool>  rst_n{"rst_n"};
    sc_core::sc_in<bool>  clk_en{"clk_en"};
    sc_core::sc_out<bool> irq{"irq"};                     // IRQ 48 (NAND ECC...)
    // Bus digital externo (AF12) — señales registradas en pin_mux
    sc_core::sc_vector<sc_core::sc_signal<bool>> ad_out, ad_oe, ad_in;   // D0-15
    sc_core::sc_vector<sc_core::sc_signal<bool>> a_out;                  // A0-23
    sc_core::sc_signal<bool> noe{"noe"}, nwe{"nwe"}, nwait_in{"nwait_in"};
    sc_core::sc_vector<sc_core::sc_signal<bool>> ne;                     // NE1-4

    explicit Fsmc(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm),
          ad_out("ad_out", 16), ad_oe("ad_oe", 16), ad_in("ad_in", 16),
          a_out("a_out", 24), ne("ne", 4) {
        mem.register_b_transport(this, &Fsmc::bt_mem);
    }

private:
    void bt_mem(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        if (gp.get_address() >= addr::FSMC_REGS) {        // registros BCR/BTR/...
            gp.set_response_status(tlm::TLM_OK_RESPONSE); // TODO(F7)
            return;
        }
        // TODO(F7): temporización BTR/BWTR (ADDSET/DATAST/BUSTURN) anotada en t
        //           y reenvío por bancos NE1-4 / NAND
        ext_mem->b_transport(gp, t);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_FSMC_H
