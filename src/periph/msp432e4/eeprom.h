// =============================================================================
// eeprom.h — La EEPROM del MSP432E4 (T6)
//
// 6 KB en 96 bloques de 16 palabras, con su propio controlador en
// 0x400A_F000 [H; hw_eeprom.h; segunda fuente: eeprom.c de DriverLib]. No está
// en el mapa de memoria: se llega a ella por una ventana.
//
//   EESIZE           1536 palabras en 96 bloques
//   EEBLOCK/EEOFFSET el bloque y la palabra (0..15) a la que apunta la ventana
//   EERDWR           la palabra: leerla la lee, escribirla la programa
//   EERDWRINC        lo mismo, y OFFSET avanza (de 15 vuelve a 0 SIN cambiar
//                    de bloque: eso lo hace el firmware, como EEPROMProgram)
//   EEDONE           WORKING mientras programa; NOPERM si el bloque está
//                    protegido
//   EESUPP           PRETRY/ERETRY: nunca, en el modelo
//   EEPROT, EEPASSn, EEUNLOCK, EEHIDE   protección por bloque
//   EEDBGME          borrado total con la llave 0xE37B
//
// ES NO VOLÁTIL: un reset no la toca, ni el de su SRx (EEPROMInit lo hace).
// Un reset la deja ocupada (WORKING) unos microsegundos, como el arranque del
// controlador. Los tiempos son de modelo [P-17 g]: 100 µs por palabra; el
// datasheet habla de decenas a cientos según haga falta copiar el bloque.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_EEPROM_H
#define STM32_PERIPH_MSP432E4_EEPROM_H

#include <algorithm>
#include <array>
#include <functional>
#include <vector>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class EepromE4 : public BusSlave {
public:
    enum : uint32_t {
        R_EESIZE = 0x000, R_EEBLOCK = 0x004, R_EEOFFSET = 0x008, R_EERDWR = 0x010,
        R_EERDWRINC = 0x014, R_EEDONE = 0x018, R_EESUPP = 0x01C, R_EEUNLOCK = 0x020,
        R_EEPROT = 0x030, R_EEPASS0 = 0x034, R_EEINT = 0x040, R_EEHIDE0 = 0x050,
        R_EEDBGME = 0x080, R_PP = 0xFC0
    };
    static constexpr uint32_t DONE_WORKING = 1u, DONE_NOPERM = 1u << 4;
    static constexpr double T_PALABRA_S = 100e-6, T_ARRANQUE_S = 5e-6;

    // El fin de una operación, al controlador de la Flash (FCRIS.ERIS)
    std::function<void()> al_terminar;

    EepromE4(sc_core::sc_module_name nm, uint32_t bytes)
        : BusSlave(nm, e4::EEPROM_B, 0x1000), mem_(bytes / 4, 0xFFFFFFFFu),
          bloques_(bytes / 64) {
        SC_HAS_PROCESS(EepromE4);
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(fin_proc); sensitive << fin_ev_; dont_initialize();
        prot_.assign(bloques_, 0);
    }

    uint32_t palabra(unsigned i) const { return i < mem_.size() ? mem_[i] : 0; }
    unsigned escrituras() const { return n_esc_; }

protected:
    std::vector<uint32_t> mem_;
    const unsigned bloques_;
    std::vector<uint32_t> prot_;
    uint32_t block_ = 0, offset_ = 0, int_ = 0, hide_[3] = {};
    bool bloqueado_ = false, no_perm_ = false;
    sc_core::sc_time ocupado_, t_acc_;
    unsigned n_esc_ = 0;
    sc_core::sc_event fin_ev_;

    void reset_proc() {
        if (rst_n.read()) return;
        block_ = offset_ = int_ = 0;
        hide_[0] = hide_[1] = hide_[2] = 0;
        no_perm_ = false;
        bloqueado_ = false;
        ocupado_ = sc_core::sc_time_stamp() + sc_core::sc_time(T_ARRANQUE_S, sc_core::SC_SEC);
    }
    void fin_proc() { if (al_terminar) al_terminar(); }
    bool ocupado() const { return t_acc_ < ocupado_; }
    unsigned indice() const { return (block_ % bloques_) * 16u + (offset_ & 15u); }

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        t_acc_ = sc_core::sc_time_stamp() + t;
        BusSlave::b_transport(gp, t);
    }
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_EESIZE: return (uint32_t(bloques_) << 16) | uint32_t(mem_.size());
            case R_EEBLOCK: return block_;
            case R_EEOFFSET: return offset_;
            case R_EERDWR: return mem_[indice()];
            case R_EERDWRINC: { const uint32_t v = mem_[indice()]; offset_ = (offset_ + 1) & 15u; return v; }
            case R_EEDONE: return (ocupado() ? DONE_WORKING : 0u) | (no_perm_ ? DONE_NOPERM : 0u);
            case R_EESUPP: return 0;
            case R_EEUNLOCK: return bloqueado_ ? 0u : 1u;
            case R_EEPROT: return prot_[block_ % bloques_];
            case R_EEINT: return int_;
            case R_EEHIDE0: return hide_[0];
            case R_EEHIDE0 + 4: return hide_[1];
            case R_EEHIDE0 + 8: return hide_[2];
            case R_PP: return uint32_t(mem_.size() * 4 / 64u) - 1u;   // [1F]
            default: return 0;
        }
    }
    void programa(uint32_t v) {
        // PROT = 2 (RO_LNA_URO): de solo lectura sin contrasena [DL, eeprom.h]
        no_perm_ = (prot_[block_ % bloques_] & 0x3u) == 2u;
        if (!no_perm_) { mem_[indice()] = v; ++n_esc_; }
        ocupado_ = t_acc_ + sc_core::sc_time(T_PALABRA_S, sc_core::SC_SEC);
        fin_ev_.notify(ocupado_ - sc_core::sc_time_stamp());
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t) override {
        switch (off) {
            case R_EEBLOCK: block_ = v & 0xFFFFu; offset_ = 0; return;   // [1F]
            case R_EEOFFSET: offset_ = v & 15u; return;
            case R_EERDWR: if (!ocupado()) programa(v); return;
            case R_EERDWRINC: if (!ocupado()) { programa(v); offset_ = (offset_ + 1) & 15u; } return;
            case R_EEPROT: prot_[block_ % bloques_] = v & 0xBu; return;
            case R_EEUNLOCK: bloqueado_ = false; return;
            case R_EEINT: int_ = v & 1u; return;
            case R_EEHIDE0: hide_[0] = v; return;
            case R_EEHIDE0 + 4: hide_[1] = v; return;
            case R_EEHIDE0 + 8: hide_[2] = v; return;
            case R_EEDBGME:
                if ((v >> 16) == 0xE37Bu && (v & 1u)) {
                    std::fill(mem_.begin(), mem_.end(), 0xFFFFFFFFu);
                    std::fill(prot_.begin(), prot_.end(), 0u);
                    ocupado_ = t_acc_ + sc_core::sc_time(T_PALABRA_S * 16, sc_core::SC_SEC);
                    fin_ev_.notify(ocupado_ - sc_core::sc_time_stamp());
                }
                return;
            default: return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_EEPROM_H
