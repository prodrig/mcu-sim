// =============================================================================
// sram.h — Memorias RAM del sistema: SRAM1, SRAM2, BKPSRAM y CCM
//
// Sram: bloque genérico parametrizable (SRAM1/112K, SRAM2/16K, BKPSRAM/4K).
// Ccm : 64 KB conectados exclusivamente al D-bus del núcleo — NO cuelga de la
//       matriz; el acceso de cualquier otro maestro falla en la propia matriz.
// [IR, §5.3; plan P3]
//
// Fase F1:
//   * accesos byte-exactos de cualquier longitud, con byte enables;
//   * DMI para acelerar el fetch de la CPU en fases posteriores;
//   * transport_dbg (carga de imágenes y comprobaciones del banco de pruebas);
//   * latencia 0 estados de espera (SRAM a HCLK) y respuesta OKAY;
//   * la BKPSRAM se alimenta del dominio de backup: mantiene su contenido a
//     través del reset de sistema (solo se borra al bajar el nivel RDP) [IR, §4.1.3].
// =============================================================================
#ifndef STM32_MEM_SRAM_H
#define STM32_MEM_SRAM_H

#include <vector>
#include <cstring>
#include <algorithm>
#include "../common/periph_base.h"

namespace stm32 {

class Sram : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<Sram> tsk{"tsk"};
    sc_core::sc_in<bool> hclk{"hclk"};
    sc_core::sc_in<bool> rst_n{"rst_n"};

    Sram(sc_core::sc_module_name nm, uint32_t base, uint32_t size)
        : sc_core::sc_module(nm), base_(base), mem_(size, 0) {
        tsk.register_b_transport(this, &Sram::bt);
        tsk.register_transport_dbg(this, &Sram::dbg);
        tsk.register_get_direct_mem_ptr(this, &Sram::dmi);
    }

    uint32_t base() const { return base_; }
    uint32_t size() const { return uint32_t(mem_.size()); }

    // --- Carga de imágenes (banco de pruebas / cargador de firmware) --------
    bool load(const uint8_t* data, size_t n, uint32_t offset = 0) {
        if (uint64_t(offset) + n > mem_.size()) return false;
        std::memcpy(mem_.data() + offset, data, n);
        return true;
    }
    // Acceso directo para verificación (sin bus, sin tiempo)
    uint8_t  peek8 (uint32_t off) const { return mem_[off]; }
    uint32_t peek32(uint32_t off) const {
        uint32_t v = 0; std::memcpy(&v, mem_.data() + off, 4); return v;
    }
    void poke32(uint32_t off, uint32_t v) { std::memcpy(mem_.data() + off, &v, 4); }
    // El apagado del dominio de 1,2 V se lleva por delante el contenido: al
    // volver de Standby la SRAM NO conserva nada [IR, §14.5.2]. El informe
    // pide reinicializar a los valores de reset o a 0x00; se usa 0x00, que es
    // lo que hace que un firmware que confie en datos viejos falle aqui igual
    // que fallaria en la placa.
    void pierde_contenido(uint8_t v = 0) { std::fill(mem_.begin(), mem_.end(), v); }

protected:
    uint32_t base_;
    std::vector<uint8_t> mem_;

    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint64_t a   = gp.get_address();
        const unsigned len = gp.get_data_length();
        if (a < base_ || (a - base_) + len > mem_.size()) {
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (gp.get_streaming_width() != 0 && gp.get_streaming_width() < len) {
            gp.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        const uint32_t off = uint32_t(a - base_);
        unsigned char* d   = gp.get_data_ptr();
        const unsigned char* be = gp.get_byte_enable_ptr();
        const unsigned bel = gp.get_byte_enable_length();

        if (gp.is_read()) {
            if (!be) std::memcpy(d, mem_.data() + off, len);
            else for (unsigned i = 0; i < len; ++i)
                     if (be[i % bel] == TLM_BYTE_ENABLED) d[i] = mem_[off + i];
        } else {
            if (!be) std::memcpy(mem_.data() + off, d, len);
            else for (unsigned i = 0; i < len; ++i)
                     if (be[i % bel] == TLM_BYTE_ENABLED) mem_[off + i] = d[i];
        }
        // SRAM a HCLK: 0 estados de espera; la fase de dirección la anota la
        // matriz. No se añade tiempo aquí.
        gp.set_dmi_allowed(true);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    unsigned dbg(tlm::tlm_generic_payload& gp) {
        const uint64_t a = gp.get_address();
        if (a < base_ || (a - base_) >= mem_.size()) return 0;
        const uint32_t off = uint32_t(a - base_);
        const unsigned len = unsigned(std::min<uint64_t>(gp.get_data_length(),
                                                         mem_.size() - off));
        if (gp.is_read()) std::memcpy(gp.get_data_ptr(), mem_.data() + off, len);
        else              std::memcpy(mem_.data() + off, gp.get_data_ptr(), len);
        return len;
    }

    bool dmi(tlm::tlm_generic_payload& gp, tlm::tlm_dmi& d) {
        const uint64_t a = gp.get_address();
        if (a < base_ || (a - base_) >= mem_.size()) return false;
        d.set_dmi_ptr(mem_.data());
        d.set_start_address(base_);
        d.set_end_address(base_ + uint32_t(mem_.size()) - 1);
        d.set_granted_access(tlm::tlm_dmi::DMI_ACCESS_READ_WRITE);
        d.set_read_latency(sc_core::SC_ZERO_TIME);
        d.set_write_latency(sc_core::SC_ZERO_TIME);
        return true;
    }
};

// CCM: mismo comportamiento, pero su único camino es el D-bus del núcleo
// [IR, §5.3; §6.1.2 "Restricción crítica"].
class Ccm : public Sram {
public:
    explicit Ccm(sc_core::sc_module_name nm)
        : Sram(nm, addr::CCM_BASE, addr::CCM_SIZE) {}
};

// BKPSRAM: 4 KB en AHB1, alimentada por VBAT. Se modela como Sram cuyo reset
// es el del dominio de backup y cuyo contenido no se borra con el reset de
// sistema [IR, §4.1.3, §5.3].
class BkpSram : public Sram {
public:
    explicit BkpSram(sc_core::sc_module_name nm)
        : Sram(nm, addr::BKPSRAM_BASE, addr::BKPSRAM_SIZE) {}
    // Borrado por bajada del nivel de protección de lectura [IR, §4.1.3].
    void erase_on_rdp_change() { std::fill(mem_.begin(), mem_.end(), 0); }
};

} // namespace stm32
#endif // STM32_MEM_SRAM_H
