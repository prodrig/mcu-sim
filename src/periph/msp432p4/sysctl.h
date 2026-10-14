// =============================================================================
// sysctl.h — RSTCTL, SYSCTL y SYSCTL_Boot del MSP432P4: lo que vive en el PPB
//
// Los tres bloques están en la ventana del PPB que ARMv7-M deja al fabricante
// [H: PERIPH_BASE2 + 0x42000, + 0x43000 y SYSCTL_BASE + 0x1000], y el núcleo se
// la pasa al SoC por `ppb_fabricante_if` (T0). Son los registros con los que
// el firmware PIDE resets, AVERIGUA por qué arrancó y CONFIGURA a qué tipo de
// reset lleva el perro guardián.
//
// LOS CUATRO TIPOS DE RESET del MSP432P4, de más a menos profundo:
//
//   POR      la alimentación: todo a cero, estados de reset incluidos
//   reboot   SYSCTL_REBOOT_CTL: como un POR sin cortar la corriente; vuelve a
//            pasar por el código de arranque
//   duro     reinicia el núcleo y los periféricos
//   blando   reinicia solo el núcleo: los periféricos siguen como estaban
//
// QUÉ FUENTE ES QUÉ BIT de HARDRESET_STAT y SOFTRESET_STAT lo dice la hoja de
// datos de cada chip, no el TRM ni DriverLib, que los nombran SRC0..SRC15
// [TI, SDK 3.40, reset.h]. Para el P401R el modelo usa:
//
//   duro    SRC0 SYSRESETREQ   SRC1 WDT_A vencido   SRC2 WDT_A violación
//   blando  SRC0 LOCKUP        SRC1 WDT_A vencido   SRC2 WDT_A violación
//
// [1F: tabla «Reset sources» de SLAS826, sin segunda fuente a mano; P-16 j]
// Las peticiones por software (RESET_REQ) no marcan ningún SRC.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_SYSCTL_H
#define STM32_PERIPH_MSP432P4_SYSCTL_H

#include <cstring>
#include <functional>
#include <string>
#include <tlm>
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

enum class ResetP4 { Por, Reboot, Duro, Blando };

class SysctlP4 {
public:
    // Fuentes con bit propio en HARDRESET/SOFTRESET_STAT
    static constexpr unsigned SRC_SYSRESETREQ = 0, SRC_LOCKUP = 0,
                              SRC_WDT_VENCE = 1, SRC_WDT_VIOLA = 2;

    // El SoC ejecuta los resets; esto solo los pide y los anota.
    std::function<void(ResetP4)> pide_reset;

    SysctlP4(uint32_t sram_size, uint32_t flash_size)
        : sram_size_(sram_size), flash_size_(flash_size) { por(); }

    // --- Lo que el SoC llama al hacer un reset --------------------------------
    // Un POR borra hasta la memoria de por qué se arrancó.
    void por() {
        hard_stat_ = soft_stat_ = 0;
        pss_stat_ = pcm_stat_ = pin_stat_ = reboot_stat_ = cs_stat_ = 0;
        reboot();
    }
    // Reboot y POR dejan SYSCTL como sale de fábrica.
    void reboot() {
        nmi_ = 0;
        wdtreset_ = 0x3u;                 // las dos causas del perro: reset duro
        perihalt_ = 0x00004000u;          // HALT_WDT: el perro se para con el núcleo [1F]
        banken_ = 0xFFu;                  // los 8 bancos de SRAM habilitados
        bankret_ = 0x1u;                  // el banco 0 siempre se retiene
        gltflt_ = 0x1u;
        secdata_ = 0;
        std::memset(boot_, 0, sizeof boot_);
    }
    // Lo que queda anotado de cada reset
    void anota_duro(int src)   { if (src >= 0) hard_stat_ |= 1u << src; }
    void anota_blando(int src) { if (src >= 0) soft_stat_ |= 1u << src; }
    void anota_pin()           { pin_stat_ = 1; }
    void anota_reboot()        { reboot_stat_ = 1; }

    // El tipo de reset que pide el perro, según SYSCTL_WDTRESET_CTL.
    bool wdt_duro(bool por_violacion) const {
        return (wdtreset_ >> (por_violacion ? 1 : 0)) & 1u;
    }
    bool halt_wdt() const { return (perihalt_ >> 14) & 1u; }
    uint32_t hard_stat() const { return hard_stat_; }
    uint32_t soft_stat() const { return soft_stat_; }
    uint32_t pin_stat()  const { return pin_stat_; }
    uint32_t reboot_stat() const { return reboot_stat_; }

    // --- El acceso desde el bus (la ventana del PPB) --------------------------
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint32_t a = uint32_t(gp.get_address());
        const unsigned len = gp.get_data_length();
        if ((a & 3u) + len > 4u) {      // estos registros son de palabra
            gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        uint32_t w = 0;
        if (!lee(a & ~3u, w)) { gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        unsigned char* d = gp.get_data_ptr();
        const unsigned sh = 8u * (a & 3u);
        if (gp.is_read()) {
            for (unsigned i = 0; i < len; ++i) d[i] = uint8_t(w >> (sh + 8 * i));
        } else {
            uint32_t v = w, m = 0;
            for (unsigned i = 0; i < len; ++i) {
                v = (v & ~(0xFFu << (sh + 8 * i))) | (uint32_t(d[i]) << (sh + 8 * i));
                m |= 0xFFu << (sh + 8 * i);
            }
            escribe(a & ~3u, v, m);
        }
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) {
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        b_transport(gp, t);
        return gp.is_response_ok() ? gp.get_data_length() : 0;
    }

    unsigned escrituras_arranque() const { return n_boot_; }

private:
    uint32_t sram_size_, flash_size_;
    uint32_t hard_stat_ = 0, soft_stat_ = 0, pss_stat_ = 0, pcm_stat_ = 0,
             pin_stat_ = 0, reboot_stat_ = 0, cs_stat_ = 0;
    uint32_t nmi_ = 0, wdtreset_ = 0, perihalt_ = 0, banken_ = 0, bankret_ = 0,
             gltflt_ = 0, secdata_ = 0;
    uint32_t boot_[9] = {};
    unsigned n_boot_ = 0;

    bool lee(uint32_t a, uint32_t& v) {
        v = 0;
        if (a >= p4::RSTCTL_B && a < p4::RSTCTL_B + 0x1000u) {
            switch (a - p4::RSTCTL_B) {
                case 0x000: return true;                   // RESET_REQ
                case 0x004: v = hard_stat_;   return true;
                case 0x008: case 0x00C: return true;
                case 0x010: v = soft_stat_;   return true;
                case 0x014: case 0x018: return true;
                case 0x100: v = pss_stat_;    return true;
                case 0x108: v = pcm_stat_;    return true;
                case 0x110: v = pin_stat_;    return true;
                case 0x118: v = reboot_stat_; return true;
                case 0x120: v = cs_stat_;     return true;
                case 0x104: case 0x10C: case 0x114: case 0x11C: case 0x124: return true;
                default: return true;                      // reservado: lee 0
            }
        }
        if (a >= p4::SYSCTL_B && a < p4::SYSCTL_B + 0x1000u) {
            switch (a - p4::SYSCTL_B) {
                case 0x00: return true;                    // REBOOT_CTL
                case 0x04: v = nmi_;      return true;
                case 0x08: v = wdtreset_; return true;
                case 0x0C: v = perihalt_; return true;
                case 0x10: v = sram_size_;  return true;
                case 0x14: v = banken_  | (1u << 16); return true;   // SRAM_RDY
                case 0x18: v = bankret_ | (1u << 16); return true;
                case 0x20: v = flash_size_; return true;
                case 0x30: v = gltflt_;   return true;
                case 0x40: v = secdata_;  return true;
                default: return true;
            }
        }
        if (a >= p4::SYSCTL_BOOT_B && a < p4::PPB_FAB_FIN) {
            const uint32_t o = (a - p4::SYSCTL_BOOT_B) / 4;
            if (o < 9) v = boot_[o];
            return true;
        }
        return false;
    }
    void escribe(uint32_t a, uint32_t v, uint32_t m) {
        auto mz = [&](uint32_t cur) { return (cur & ~m) | (v & m); };
        if (a >= p4::RSTCTL_B && a < p4::RSTCTL_B + 0x1000u) {
            switch (a - p4::RSTCTL_B) {
                case 0x000:                                 // RESET_REQ, con llave
                    if (((v >> 8) & 0xFFu) != 0x69u) return;
                    if (v & 0x2u)      { if (pide_reset) pide_reset(ResetP4::Duro); }
                    else if (v & 0x1u) { if (pide_reset) pide_reset(ResetP4::Blando); }
                    return;
                case 0x008: hard_stat_ &= ~(v & m & 0xFFFFu); return;
                case 0x00C: hard_stat_ |=  (v & m & 0xFFFFu); return;
                case 0x014: soft_stat_ &= ~(v & m & 0xFFFFu); return;
                case 0x018: soft_stat_ |=  (v & m & 0xFFFFu); return;
                case 0x104: if (v & 1u) pss_stat_ = 0;    return;
                case 0x10C: if (v & 1u) pcm_stat_ = 0;    return;
                case 0x114: if (v & 1u) pin_stat_ = 0;    return;
                case 0x11C: if (v & 1u) reboot_stat_ = 0; return;
                case 0x124: if (v & 1u) cs_stat_ = 0;     return;
                default: return;
            }
        }
        if (a >= p4::SYSCTL_B && a < p4::SYSCTL_B + 0x1000u) {
            switch (a - p4::SYSCTL_B) {
                case 0x00:                                  // REBOOT_CTL, con llave
                    if (((v >> 8) & 0xFFu) == 0x69u && (v & 1u) && pide_reset)
                        pide_reset(ResetP4::Reboot);
                    return;
                case 0x04: nmi_      = mz(nmi_) & 0x0000000Fu;  return;
                case 0x08: wdtreset_ = mz(wdtreset_) & 0x3u;    return;
                case 0x0C: perihalt_ = mz(perihalt_) & 0xFFFFu; return;
                // Los bancos se habilitan de abajo arriba: habilitar el n
                // habilita todos los de debajo. El banco 0 no se apaga.
                case 0x14: {
                    const uint32_t b = mz(banken_) & 0xFFu;
                    unsigned top = 0;
                    for (unsigned k = 0; k < 8; ++k) if (b & (1u << k)) top = k;
                    banken_ = (1u << (top + 1)) - 1u;
                    return;
                }
                case 0x18: bankret_ = (mz(bankret_) & 0xFFu) | 1u; return;
                case 0x30: gltflt_  = mz(gltflt_) & 1u;          return;
                case 0x40: secdata_ = mz(secdata_);               return;
                default: return;
            }
        }
        if (a >= p4::SYSCTL_BOOT_B && a < p4::PPB_FAB_FIN) {
            const uint32_t o = (a - p4::SYSCTL_BOOT_B) / 4;
            if (o < 9) { boot_[o] = mz(boot_[o]); ++n_boot_; }
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_SYSCTL_H
