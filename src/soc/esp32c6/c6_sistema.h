// =============================================================================
// c6_sistema.h — Lo de sistema del ESP32-C6: el PCR, el LP_AON y el LP_CLKRST
//
// E9 de doc/analisis_esp32_riscv.md. Las tablas de registros (valor de reset y
// bits escribibles) salen de las cabeceras de ESP-IDF v5.5 con
// soc/esp/gen_regs_esp.py: regs_c6.inc. Lo que tiene comportamiento:
//
//   PCR (0x6009_6000) [TRM-C6 §8.2.4; clk_tree_ll.h de ESP-IDF]
//     SYSCLK_CONF (0x110): SOC_CLK_SEL [17:16] (0 XTAL, 1 PLL, 2 RC_FAST),
//       LS_DIV_NUM [7:0] y HS_DIV_NUM [15:8] (2: el PLL de 480 MHz entre 3, el
//       HP_ROOT de 160 MHz; solo de lectura).
//     CPU_FREQ_CONF (0x118): CPU_LS_DIV_NUM [7:0], CPU_HS_DIV_NUM [15:8] y
//       CPU_HS_120M_FORCE [16] (120 MHz con HS_DIV 0).
//     AHB_FREQ_CONF (0x11C): AHB_LS [7:0] y AHB_HS [15:8] (3 de reset: 40 MHz
//       con el PLL); APB_FREQ_CONF (0x120): APB_DIV_NUM [7:0], del AHB.
//     Con el XTAL (40 MHz) o el RC_FAST (17,5 MHz) se usan los LS; con el PLL,
//       los HS. Tras el reset: XTAL, la CPU, el AHB y el APB a 40 MHz.
//     UARTn_SCLK_CONF (0x004, 0x010): el SCLK de la UART, en el formato del
//       CLK_CONF del C3 ([21:20] 1 PLL_F80M, 2 RC_FAST, 3 XTAL; [19:12]
//       DIV_NUM): la UART lo lee de aquí (UartEsp::pon_c6).
//     TIMERGROUPn_TIMER_CLK_CONF (0x040, 0x04C): [21:20] 0 XTAL, 1 PLL_F80M,
//       2 RC_FAST; el TIMG cuenta ese reloj (su USE_XTAL ya no elige).
//     El resto (los CLK_EN y RST_EN de cada periférico, SYSCLK_FREQ_QUERY...)
//       guarda o contesta su valor de reset: no corta relojes ni resetea.
//   LP_AON (0x600B_1000) [TRM-C6 §8, §9]: STORE0-9 (solo los borra un reset de
//     chip), SYS_CFG.FORCE_DOWNLOAD_BOOT [30] y HPSYS_SW_RESET [31] (un reset
//     del sistema), CPUCORE0_CFG.CPU_CORE0_SW_RESET [28] (uno de la CPU).
//   LP_CLKRST (0x600B_0400): RESET_CAUSE [4:0] con la causa del último reset
//     (reset_reasons.h de ESP-IDF: 1 encendido, 3 sistema por software, 12 la
//     CPU por software...) y CORE0_RESET_FLAG [5].
// =============================================================================
#ifndef STM32_SOC_ESP32C6_C6_SISTEMA_H
#define STM32_SOC_ESP32C6_C6_SISTEMA_H

#include <systemc>
#include <functional>
#include "../esp/regs_esp.h"

namespace stm32 {

namespace c6 {
#include "regs_c6.inc"
} // namespace c6

// --- El PCR: los relojes ----------------------------------------------------------------
class PcrC6 : public BloqueEsp {
public:
    double hz_xtal = 40e6, hz_pll = 480e6, hz_rc_fast = 17.5e6, hz_pll_f80m = 80e6;
    // Cambian la CPU, el AHB o el APB
    std::function<void(double cpu, double ahb, double apb)> al_cambiar_reloj;
    // Cambia el reloj de un grupo de temporizadores (0 o 1)
    std::function<void(unsigned grupo, double hz)> al_cambiar_timg;

    PcrC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x1000, c6::REGS_PCR) {}

    void reset() { reset_regs(); avisa_relojes(); }
    unsigned soc_clk_sel() const { return (r(0x110) >> 16) & 3u; }
    double hz_cpu() const {
        const uint32_t s = r(0x110), c = r(0x118);
        switch (soc_clk_sel()) {
        case 1: {                                        // PLL: HP_ROOT = 480 / (HS_DIV + 1)
            const unsigned hs = (c >> 8) & 0xFFu;
            if (hs == 0 && ((c >> 16) & 1u)) return 120e6;
            return hz_pll / double(((s >> 8) & 0xFFu) + 1u) / double(hs + 1u);
        }
        case 2: return hz_rc_fast / double((s & 0xFFu) + 1u) / double((c & 0xFFu) + 1u);
        default: return hz_xtal / double((s & 0xFFu) + 1u) / double((c & 0xFFu) + 1u);
        }
    }
    double hz_ahb() const {
        const uint32_t s = r(0x110), a = r(0x11C);
        switch (soc_clk_sel()) {
        case 1: return hz_pll / double(((s >> 8) & 0xFFu) + 1u) / double(((a >> 8) & 0xFFu) + 1u);
        case 2: return hz_rc_fast / double((s & 0xFFu) + 1u) / double((a & 0xFFu) + 1u);
        default: return hz_xtal / double((s & 0xFFu) + 1u) / double((a & 0xFFu) + 1u);
        }
    }
    double hz_apb() const { return hz_ahb() / double((r(0x120) & 0xFFu) + 1u); }
    // El SCLK de la UART n, en el formato del CLK_CONF del C3
    uint32_t uart_sclk(unsigned n) const { return r(n ? 0x010 : 0x004) & 0x003FF000u; }
    double hz_timg(unsigned g) const {
        const unsigned sel = (r(g ? 0x04C : 0x040) >> 20) & 3u;
        return sel == 1 ? hz_pll_f80m : sel == 2 ? hz_rc_fast : hz_xtal;
    }

protected:
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        guarda(off, v, m);
        if (off >= 0x110 && off <= 0x120) avisa_relojes();
        if (off == 0x040 || off == 0x04C) { if (al_cambiar_timg) al_cambiar_timg(off == 0x04C, hz_timg(off == 0x04C)); }
        return true;
    }

private:
    void avisa_relojes() {
        if (al_cambiar_reloj) al_cambiar_reloj(hz_cpu(), hz_ahb(), hz_apb());
        if (al_cambiar_timg) { al_cambiar_timg(0, hz_timg(0)); al_cambiar_timg(1, hz_timg(1)); }
    }
};

// --- LP_AON: los STORE y los resets por software ----------------------------------------
class LpAonC6 : public BloqueEsp {
public:
    // sistema = true: HPSYS_SW_RESET; false: CPU_CORE0_SW_RESET
    std::function<void(bool sistema)> reset_sw;

    LpAonC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x400, c6::REGS_LP_AON) {}
    // Un reset de chip: todo, los STORE incluidos (los demás resets no lo tocan)
    void reset_chip() { reset_regs(); }
    uint32_t store(unsigned n) const { return r(4u * n); }
    bool force_download() const { return (r(0x34) >> 30) & 1u; }

protected:
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off == 0x34) {
            guarda(off, v, m);
            if ((v & m) >> 31 && reset_sw) reset_sw(true);
            return true;
        }
        if (off == 0x38) {
            guarda(off, v, m & ~(1u << 28));
            if (((v & m) >> 28) & 1u && reset_sw) reset_sw(false);
            return true;
        }
        return false;
    }
};

// --- LP_CLKRST: la causa del reset ---------------------------------------------------------
class LpClkRstC6 : public BloqueEsp {
public:
    LpClkRstC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x400, c6::REGS_LP_CLKRST) {}
    void reset_chip() { reset_regs(); causa_ = 1; flag_ = true; }
    void anota_causa(unsigned c) { causa_ = c & 0x1Fu; flag_ = true; }
    unsigned causa() const { return causa_; }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off != 0x10) return false;
        v = causa_ | (flag_ ? 1u << 5 : 0u);
        return true;
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off != 0x10) return false;
        v &= m;
        if (v & (1u << 29)) causa_ = 0;
        if (v & (1u << 30)) flag_ = true;
        if (v & (1u << 31)) flag_ = false;
        return true;
    }

private:
    unsigned causa_ = 1;
    bool flag_ = true;
};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_C6_SISTEMA_H
