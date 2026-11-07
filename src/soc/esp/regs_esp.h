// =============================================================================
// regs_esp.h — Un bloque de registros de Espressif con sus valores de reset
//
// E4 de doc/analisis_esp32_riscv.md. Las tablas salen de las cabeceras de
// ESP-IDF con soc/esp/gen_regs_esp.py: por cada registro, su desplazamiento,
// su valor de reset y los bits que se pueden escribir.
//
// `BloqueEsp` guarda lo que se escribe en los bits escribibles y devuelve el
// resto con su valor de reset. Sirve de base a los modelos (que sobrecargan
// `lee`/`escribe` para lo que tiene comportamiento) y, solo, a los bloques
// DECLARADOS que el arranque de ESP-IDF configura sin que el modelo tenga que
// hacer nada: contestan como el silicio tras el reset, guardan y, si se pide,
// avisan una vez de que no hacen nada más (regla de §8.2: «se modelan con dos
// fuentes o se declaran, y en ningún caso se deja seguir al firmware sin
// decirlo»).
// =============================================================================
#ifndef STM32_SOC_ESP_REGS_ESP_H
#define STM32_SOC_ESP_REGS_ESP_H

#include <cstdint>
#include <map>
#include <string>
#include "../../common/periph_base.h"

namespace stm32 {

struct RegEsp { uint32_t off, reset, escribible; };

inline uint32_t mascara_be_esp(uint32_t be) {
    return (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) | (be & 4 ? 0xFF0000u : 0) |
           (be & 8 ? 0xFF000000u : 0);
}

class BloqueEsp : public BusSlave {
public:
    // `aviso`: si no está vacío, se dice una vez, al primer acceso
    template <size_t N>
    BloqueEsp(sc_core::sc_module_name nm, uint32_t base, uint32_t size, const RegEsp (&t)[N],
              const std::string& aviso = "")
        : BusSlave(nm, base, size), aviso_(aviso) {
        for (const RegEsp& r : t) def_[r.off] = r;
        reset_regs();
    }
    void reset_regs() {
        v_.clear();
        for (const auto& d : def_) v_[d.first] = d.second.reset;
    }
    uint32_t r(uint32_t off) const { auto i = v_.find(off); return i == v_.end() ? 0 : i->second; }
    void pon(uint32_t off, uint32_t v) { v_[off] = v; }
    bool existe(uint32_t off) const { return def_.count(off) != 0; }
    unsigned accesos() const { return n_; }

protected:
    // Para los modelos: devolver true si se han ocupado ellos
    virtual bool lee(uint32_t, uint32_t&) { return false; }
    virtual bool escribe(uint32_t, uint32_t, uint32_t /*mascara*/) { return false; }

    uint32_t reg_read(uint32_t off) override {
        ++n_; avisa();
        uint32_t v = 0;
        if (lee(off, v)) return v;
        return r(off);
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        ++n_; avisa();
        const uint32_t m = mascara_be_esp(be);
        if (escribe(off, v, m)) return;
        auto d = def_.find(off);
        if (d == def_.end()) return;                        // reservado
        const uint32_t w = m & d->second.escribible;
        v_[off] = (r(off) & ~w) | (v & w);
    }
    // Escribe respetando la máscara de lo escribible
    void guarda(uint32_t off, uint32_t v, uint32_t m) {
        auto d = def_.find(off);
        const uint32_t w = m & (d == def_.end() ? 0u : d->second.escribible);
        v_[off] = (r(off) & ~w) | (v & w);
    }

private:
    std::map<uint32_t, RegEsp> def_;
    std::map<uint32_t, uint32_t> v_;
    std::string aviso_;
    bool avisado_ = false;
    unsigned n_ = 0;
    void avisa() {
        if (avisado_ || aviso_.empty()) return;
        avisado_ = true;
        SC_REPORT_WARNING("bloque-declarado", aviso_.c_str());
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP_REGS_ESP_H
