// =============================================================================
// ahb_decoder.h — Decodificador de segmento (AHB1 / AHB2 / APB) y puente AHB->APB
//
// AhbDecoder: reparte un rango de direcciones entre N esclavos registrados
// (periféricos AHB del segmento y puentes APB). Acceso a hueco reservado =>
// error de bus [IR, §6.5-nota] — incluidos los rangos de bloques que existen en
// RM0090 pero no en el F407 (LTDC, SAI1, DMA2D, SPI4-6, UART7-10, CRYP/HASH).
//
// AhbApbBridge: esclavo AHB que sincroniza con el dominio PCLK. Cada acceso
// cuesta al menos 2 ciclos PCLK y el maestro AHB queda bloqueado durante ese
// tiempo [IR, §6.4]. La conversión a 32 bits y el reparto entre periféricos los
// hace el AhbDecoder de aguas abajo.
// =============================================================================
#ifndef STM32_BUS_AHB_DECODER_H
#define STM32_BUS_AHB_DECODER_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <vector>
#include <cstdint>

namespace stm32 {

class AhbDecoder : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<AhbDecoder> tsk;
    // Segundo puerto de entrada (opcional): usado por el puerto de periféricos
    // de DMA1, que en el silicio ataca APB1 directamente sin pasar por la matriz.
    tlm_utils::simple_target_socket_optional<AhbDecoder> tsk2;

    uint64_t n_xfer = 0, n_err = 0;     // estadísticas

    explicit AhbDecoder(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm), tsk("tsk"), tsk2("tsk2") {
        tsk.register_b_transport(this, &AhbDecoder::b_transport);
        tsk.register_transport_dbg(this, &AhbDecoder::transport_dbg);
        tsk2.register_b_transport(this, &AhbDecoder::b_transport);
        tsk2.register_transport_dbg(this, &AhbDecoder::transport_dbg);
    }
    ~AhbDecoder() override { for (Entry& e : map_) delete e.isk; }

    // Registra un esclavo aguas abajo; devuelve el socket initiator a bindear.
    tlm_utils::simple_initiator_socket<AhbDecoder>*
    add_slave(const char* name, uint32_t base, uint32_t size) {
        auto* s = new tlm_utils::simple_initiator_socket<AhbDecoder>(name);
        map_.push_back(Entry{base, size, s});
        return s;
    }

    // ¿Está la dirección cubierta por algún esclavo del segmento?
    bool decodes(uint64_t a) const {
        for (const Entry& e : map_)
            if (a >= e.base && a < uint64_t(e.base) + e.size) return true;
        return false;
    }

private:
    struct Entry {
        uint32_t base, size;
        tlm_utils::simple_initiator_socket<AhbDecoder>* isk;
    };
    std::vector<Entry> map_;

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        for (const Entry& e : map_)
            if (a >= e.base && a < uint64_t(e.base) + e.size) {
                ++n_xfer;
                (*e.isk)->b_transport(gp, t);
                return;
            }
        ++n_err;
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);  // reservado
    }

    unsigned transport_dbg(tlm::tlm_generic_payload& gp) {
        const uint64_t a = gp.get_address();
        for (const Entry& e : map_)
            if (a >= e.base && a < uint64_t(e.base) + e.size)
                return (*e.isk)->transport_dbg(gp);
        return 0;
    }
};

// ---------------------------------------------------------------------------
// Puente AHB -> APB1/APB2 [IR, §6.4]
// ---------------------------------------------------------------------------
class AhbApbBridge : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<AhbApbBridge>    ahb{"ahb"};
    tlm_utils::simple_initiator_socket<AhbApbBridge> apb{"apb"};
    sc_core::sc_in<bool>   pclk{"pclk"};
    sc_core::sc_in<double> pclk_hz{"pclk_hz"};   // para anotar la latencia

    uint64_t n_xfer = 0;

    explicit AhbApbBridge(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm) {
        ahb.register_b_transport(this, &AhbApbBridge::b_transport);
        ahb.register_transport_dbg(this, &AhbApbBridge::transport_dbg);
    }

private:
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const double f = pclk_hz.read();
        if (f <= 0.0) {
            // Dominio APB sin reloj: el acceso no obtiene respuesta -> BusFault
            gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        // 2 ciclos PCLK de sincronización por acceso [IR, §6.4]
        t += sc_core::sc_time(2.0e12 / f, sc_core::SC_PS);
        ++n_xfer;
        apb->b_transport(gp, t);
        // TODO(F7): estados de espera exactos según relación HCLK/PCLK en AT.
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) {
        return apb->transport_dbg(gp);
    }
};

} // namespace stm32
#endif // STM32_BUS_AHB_DECODER_H
