// =============================================================================
// can.h — bxCAN: CAN1 (maestro, dueño de los 28 bancos de filtros) y CAN2
// [IR, §12.12]. CAN2 accede a los filtros a través de CAN1 (CAN2SB).
// =============================================================================
#ifndef STM32_PERIPH_CAN_H
#define STM32_PERIPH_CAN_H

#include "../common/periph_base.h"

namespace stm32 {

class BxCan : public BusSlave {
public:
    sc_core::sc_out<bool> irq_tx{"irq_tx"}, irq_rx0{"irq_rx0"},
                          irq_rx1{"irq_rx1"}, irq_sce{"irq_sce"};
    sc_core::sc_in<bool>  freeze{"freeze"};              // DBG_CANx_STOP
    // AF
    sc_core::sc_signal<bool> tx_out{"tx_out"}, rx_in{"rx_in"};

    // master=true para CAN1 (posee el banco de filtros compartido)
    BxCan(sc_core::sc_module_name nm, uint32_t base, bool master)
        : BusSlave(nm, base, 0x400), master_(master) {
        SC_HAS_PROCESS(BxCan);
        SC_THREAD(bit_proc);
    }
    // CAN2 -> filtros de CAN1
    void bind_filter_master(BxCan* m) { filter_master_ = m; }

protected:
    bool   master_;
    BxCan* filter_master_ = nullptr;
    void bit_proc() {
        // TODO(F5): bit timing BTR, 3 mailboxes TX con prioridad, 2 FIFOs RX,
        //           filtrado ID/máscara, estados de error TEC/REC [IR, §12.12]
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_CAN_H
