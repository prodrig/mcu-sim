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
//   LPPERI (0x600B_2800): RNG_DATA (0x08), el generador de números
//     aleatorios; como el del C3 (SysconC3), un xorshift32 de semilla fija,
//     para que cada simulación sea igual [de modelo]: el bootloader saca de ahí
//     la máscara con que ofusca la RAM al cargar y repite hasta que no sale 0.
//   I2C_ANA_MST (0x600A_F800) [regi2c_ctrl_ll.h e i2c_ana_mst_reg.h de
//     ESP-IDF]: el bus I2C interno de los bloques analógicos (BBPLL 0x66,
//     BOD/ULP 0x61, DIG_REG 0x6D...). I2C0_CTRL/I2C1_CTRL: el bloque [7:0], el
//     registro [15:8], el dato [23:16] y WRITE [24]; acaba en el acto (BUSY
//     [25] a 0) y una lectura deja el dato guardado en [23:16]. Los bloques
//     guardan sin hacer nada (el PLL está siempre). ANA_CONF0.BBPLL_CAL_DONE
//     [24] lee 1: la calibración del BBPLL acaba en el acto.
//   eFuse (0x600B_0800) [TRM-C6 §4; efuse_reg.h y esp_efuse_table.csv]: la
//     imagen de lectura (RD_WR_DIS 0x2C a RD_SYS_PART2_DATA7 0x178), de
//     omisión la de un C6 de serie (la MAC, la revisión del chip, el
//     encapsulado, la capacidad de la Flash dentro y la versión de los
//     bloques) o la de un fichero; READ_CMD acaba en el acto con READ_DONE;
//     programar no programa (lo dice).
// =============================================================================
#ifndef STM32_SOC_ESP32C6_C6_SISTEMA_H
#define STM32_SOC_ESP32C6_C6_SISTEMA_H

#include <systemc>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>
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

// --- LPPERI: el RNG ----------------------------------------------------------------------
class LpPeriC6 : public BloqueEsp {
public:
    LpPeriC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x400, c6::REGS_LPPERI) {}
    void reset() { reset_regs(); }
protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off != 0x08) return false;
        x_ ^= x_ << 13; x_ ^= x_ >> 17; x_ ^= x_ << 5;
        v = x_;
        return true;
    }
private:
    uint32_t x_ = 0xC6C6A5A5u;
};

// --- I2C_ANA_MST: el bus de los bloques analógicos ---------------------------------------
class I2cAnaMstC6 : public BloqueEsp {
public:
    I2cAnaMstC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x800, c6::REGS_I2C_ANA_MST) {}
    void reset() { reset_regs(); ana_.clear(); }
    uint8_t analogico(uint8_t bloque, uint8_t reg) const {
        auto i = ana_.find(uint16_t(bloque << 8 | reg));
        return i == ana_.end() ? 0 : i->second;
    }
    unsigned operaciones() const { return n_; }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off == 0x18) { v = r(off) | (1u << 24); return true; }      // BBPLL_CAL_DONE
        return false;
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off != 0x00 && off != 0x04) return false;
        v = (r(off) & ~m) | (v & m);
        ++n_;
        const uint16_t k = uint16_t((v & 0xFFu) << 8 | ((v >> 8) & 0xFFu));
        if (v & (1u << 24)) ana_[k] = uint8_t(v >> 16);
        else v = (v & ~0x00FF0000u) | (uint32_t(analogico(uint8_t(v), uint8_t(v >> 8))) << 16);
        pon(off, v & ~(1u << 25));
        return true;
    }

private:
    std::map<uint16_t, uint8_t> ana_;
    unsigned n_ = 0;
};

// --- eFuse ----------------------------------------------------------------------------
class EfuseC6 : public BloqueEsp {
public:
    static constexpr uint32_t RD_INI = 0x2C, RD_FIN = 0x17C;
    enum : uint32_t { CONF = 0x1CC, STATUS = 0x1D0, CMD = 0x1D4, INT_RAW = 0x1D8, INT_ST = 0x1DC,
                      INT_ENA = 0x1E0, INT_CLR = 0x1E4 };
    // `rev`: la del chip (2 = v0.2); `pkg`: 0 QFN40, 1 QFN32 con la Flash
    // dentro; `flash_mb`: la de dentro (0, 4 u 8)
    EfuseC6(sc_core::sc_module_name nm, uint32_t base, unsigned rev, unsigned pkg, unsigned flash_mb)
        : BloqueEsp(nm, base, 0x800, c6::REGS_EFUSE), rev_(rev), pkg_(pkg), flash_mb_(flash_mb) {
        pon_omision();
    }
    void reset() { reset_regs(); }
    // La imagen de omisión: la MAC 40:4C:CA:00:C6:01, y en BLOCK1 (0x44)
    // WAFER_VERSION_MINOR (bits 114-117), WAFER_VERSION_MAJOR (118-119),
    // PKG_VERSION (120-122), BLK_VERSION_MINOR (123-125: 1) y FLASH_CAP
    // (128-130: 1 4 MB, 4 8 MB) [esp_efuse_table.csv de ESP-IDF]; lo demás, 0.
    void pon_omision() {
        w_.assign((RD_FIN - RD_INI) / 4, 0u);
        w_[(0x44 - RD_INI) / 4] = 0xCA00C601u;
        w_[(0x48 - RD_INI) / 4] = 0x0000404Cu;
        const unsigned may = rev_ / 100, men = rev_ % 100;
        w_[(0x50 - RD_INI) / 4] = ((men & 0xFu) << 18) | ((may & 3u) << 22) | ((pkg_ & 7u) << 24) | (1u << 27);
        w_[(0x54 - RD_INI) / 4] = flash_mb_ == 8 ? 4u : flash_mb_ == 4 ? 1u : 0u;
        ruta_.clear();
    }
    std::string carga(const std::string& ruta) {
        std::FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) return "no se puede leer " + ruta;
        std::vector<uint32_t> w((RD_FIN - RD_INI) / 4, 0u);
        unsigned char b[4];
        size_t i = 0;
        while (i < w.size() && std::fread(b, 1, 4, f) == 4)
            w[i++] = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        std::fclose(f);
        if (i == 0) return ruta + " esta vacio";
        w_ = w;
        ruta_ = ruta;
        return "";
    }
    uint32_t rd(uint32_t off) const { return off >= RD_INI && off < RD_FIN ? w_[(off - RD_INI) / 4] : 0u; }
    unsigned revision() const { return 100 * ((rd(0x50) >> 22) & 3u) + ((rd(0x50) >> 18) & 0xFu); }
    void pon_bit(uint32_t off, unsigned bit, bool v) {
        if (off < RD_INI || off >= RD_FIN) return;
        uint32_t& w = w_[(off - RD_INI) / 4];
        w = v ? (w | (1u << bit)) : (w & ~(1u << bit));
    }
    void pon_palabra(uint32_t off, uint32_t v) { if (off >= RD_INI && off < RD_FIN) w_[(off - RD_INI) / 4] = v; }
    const std::string& ruta() const { return ruta_; }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off >= RD_INI && off < RD_FIN) { v = rd(off); return true; }
        if (off == STATUS) { v = 1u; return true; }                  // STATE: en reposo
        if (off == INT_ST) { v = r(INT_RAW) & r(INT_ENA); return true; }
        if (off == CMD) { v = 0; return true; }                     // la orden acaba en el acto
        return false;
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off == CMD) {
            v &= m;
            const uint32_t op = r(CONF) & 0xFFFFu;
            if ((v & 1u) && op == 0x5AA5u) pon(INT_RAW, r(INT_RAW) | 1u);
            if ((v & 2u) && op == 0x5A5Au) {
                SC_REPORT_WARNING("efuse", "programar eFuses no esta modelado: no cambia nada");
                pon(INT_RAW, r(INT_RAW) | 2u);
            }
            return true;
        }
        if (off == INT_CLR) { pon(INT_RAW, r(INT_RAW) & ~(v & m)); return true; }
        return false;
    }

private:
    unsigned rev_, pkg_, flash_mb_;
    std::vector<uint32_t> w_;
    std::string ruta_;
};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_C6_SISTEMA_H
