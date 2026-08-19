// =============================================================================
// dma.h — Controladores DMA1/DMA2 [IR, §11]
//
// Esclavo AHB1 (registros) + dos maestros de matriz: puerto de memoria y (solo
// DMA2, único con acceso a periféricos AHB/mem-to-mem) puerto de periféricos.
// DMA1 usa su puerto de periféricos directamente contra el bus APB1 (en el
// modelo: también via matriz->AHB1, la restricción se aplica en la máscara).
// Las peticiones de hardware entran como 64 líneas (8 streams x 8 canales,
// tablas [IR, §11.4]); el mux CHSEL selecciona una por stream.
// =============================================================================
#ifndef STM32_PERIPH_DMA_H
#define STM32_PERIPH_DMA_H

#include "../common/periph_base.h"
#include <tlm_utils/simple_initiator_socket.h>

namespace stm32 {

class DmaCtrl : public BusSlave {
public:
    static constexpr unsigned N_STREAMS = 8, N_CH = 8;

    tlm_utils::simple_initiator_socket<DmaCtrl> mem_m{"mem_m"};     // maestro mem
    tlm_utils::simple_initiator_socket<DmaCtrl> periph_m{"periph_m"};// maestro periph
    // req_in[s*8+c]: petición del periférico mapeado en (stream s, canal c)
    sc_core::sc_vector<sc_core::sc_in<bool>>  req_in;
    sc_core::sc_vector<sc_core::sc_out<bool>> ack_out;              // [64]
    sc_core::sc_vector<sc_core::sc_out<bool>> irq_stream;           // [8]

    DmaCtrl(sc_core::sc_module_name nm, uint32_t base, bool is_dma2)
        : BusSlave(nm, base, 0x400),
          req_in("req_in", N_STREAMS * N_CH), ack_out("ack_out", N_STREAMS * N_CH),
          irq_stream("irq_stream", N_STREAMS), is_dma2_(is_dma2) {
        SC_HAS_PROCESS(DmaCtrl);
        SC_THREAD(engine_proc);
    }

protected:
    bool is_dma2_;
    void engine_proc() {
        // TODO(F4): por stream: SxCR/SxNDTR/SxPAR/SxM0AR/SxM1AR/SxFCR; FIFO 4
        //           palabras, umbrales, ráfagas, circular/doble buffer;
        //           árbitro PL + nº de stream; flags LISR/HISR y errores
        //           TEIF/FEIF/DMEIF [IR, §11.3-11.8]
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_DMA_H
