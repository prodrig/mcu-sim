// =============================================================================
// c3_sistema.h — SYSTEM, RTC_CNTL, eFuse y los bloques que solo guardan
//
// E3 de doc/analisis_esp32_riscv.md. Lo que en el C3 hace de RCC, PWR y
// opciones, cada cosa en su bloque [TRM-C3 §6, §9, §4, §16]:
//
//   SystemC3   0x600C_0000  el reloj de la CPU y del APB:
//                SYSCLK_CONF.SOC_CLK_SEL (0 XTAL, 1 PLL, 2 RC_FAST) y
//                PRE_DIV_CNT; CPU_PER_CONF.PLL_FREQ_SEL (320/480 MHz) y
//                CPUPERIOD_SEL. Con el PLL: 80 o 160 MHz y el APB a 80;
//                con XTAL o RC_FAST, la CPU y el APB a XTAL/(PRE_DIV_CNT+1).
//                Tras el reset, XTAL/2 = 20 MHz [TRM-C3 tabla 6.2-2].
//                CPU_INTR_FROM_CPU_0-3: las cuatro interrupciones por
//                software (fuentes 50-53).
//                PERIP_CLK_EN0/1 y PERIP_RST_EN0/1 se guardan: el reloj de
//                cada periférico NO se corta todavía (limitaciones()).
//   RtcCntlC3  0x6000_8000  la causa del reset (RESET_STATE: 1 al
//                encender, 3 por software del sistema, 12 por software de la
//                CPU, 7/8 por los MWDT...), el reset por software
//                (OPTIONS0.SW_SYS_RST, SW_PROCPU_RST), FORCE_DOWNLOAD_BOOT
//                (OPTION1) y los STORE0-7, que sobreviven al reset por
//                software. El RWDT (WDTCONFIG0-4, WDTFEED, WDTWPROTECT) con
//                sus cuatro etapas en ciclos del reloj lento y el modo
//                flashboot del arranque por la ROM. El super watchdog (SWD)
//                y el brownout: guardan, sin contar ni vigilar la tensión. El
//                resto de RTC_CNTL, con sus valores de reset.
//   EfuseC3    0x6000_8800  los registros de lectura (RD_*, 0x2C-0x17B) de
//                una imagen: por omisión, una MAC 7C:DF:A1:00:C3:01, la
//                revisión del chip del descriptor y los bloques v1.3; o un
//                fichero de hasta 336 bytes con las palabras en el orden de
//                los registros (BLOCK0, BLOCK1, BLOCK2-10) [TRM-C3 §4.4]. La
//                orden de leer (CMD con OP_CODE 0x5AA5) acaba en el acto con
//                READ_DONE; la de programar no programa (aviso). El resto del
//                controlador, con sus valores de reset (regs_c3.inc).
//   BloqueGuardado  guarda lo que se escribe y lo devuelve (SYSCON, PMS,
//                ASSIST_DEBUG...): lo que ESP-IDF configura y E3 no usa.
// =============================================================================
#ifndef STM32_SOC_ESP32C3_C3_SISTEMA_H
#define STM32_SOC_ESP32C3_C3_SISTEMA_H

#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../../common/periph_base.h"
#include "../esp/regs_esp.h"

// Los registros y sus valores de reset, de las cabeceras de ESP-IDF
namespace stm32 { namespace c3 {
#include "regs_c3.inc"
} } // namespace stm32::c3

namespace stm32 {

inline uint32_t mascara_be(uint32_t be) {
    return (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) | (be & 4 ? 0xFF0000u : 0) |
           (be & 8 ? 0xFF000000u : 0);
}

class BloqueGuardado : public BusSlave {
public:
    BloqueGuardado(sc_core::sc_module_name nm, uint32_t base, uint32_t size,
                   std::map<uint32_t, uint32_t> reset = {})
        : BusSlave(nm, base, size), reset_(std::move(reset)), r_(reset_) {}
    uint32_t peek(uint32_t off) const { auto i = r_.find(off); return i == r_.end() ? 0 : i->second; }
    unsigned accesos() const { return n_; }
    void reset() { r_ = reset_; }
protected:
    uint32_t reg_read(uint32_t off) override { ++n_; return peek(off); }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        ++n_;
        const uint32_t m = mascara_be(be);
        r_[off] = (peek(off) & ~m) | (v & m);
    }
private:
    std::map<uint32_t, uint32_t> reset_, r_;
    unsigned n_ = 0;
};

class SystemC3 : public BusSlave {
public:
    // El reloj ha cambiado: (CPU, APB) en Hz
    std::function<void(double, double)> al_cambiar_reloj;
    std::function<void(unsigned n, bool nivel)> irq_sw;     // FROM_CPU_0..3
    double hz_xtal = 40e6, hz_rc_fast = 17.5e6;

    SystemC3(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        reset_regs();
    }
    void reset_regs() {
        cpu_per_conf_ = 0x0Cu;
        sysclk_conf_ = 0x1u | (40u << 12);
        perip_clk_en0_ = 0xF9C1E06Fu; perip_clk_en1_ = 0x0u;
        perip_rst_en0_ = 0x0u;        perip_rst_en1_ = 0x7Eu;
        for (uint32_t& x : from_cpu_) x = 0;
        otros_.clear();
        recalcula();
    }
    double hz_cpu() const { return hz_cpu_; }
    double hz_apb() const { return hz_apb_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x08: return cpu_per_conf_;
        case 0x10: return perip_clk_en0_;
        case 0x14: return perip_clk_en1_;
        case 0x18: return perip_rst_en0_;
        case 0x1C: return perip_rst_en1_;
        case 0x28: case 0x2C: case 0x30: case 0x34: return from_cpu_[(off - 0x28) / 4];
        case 0x58: return sysclk_conf_;
        case 0xFFC: return 0x2007150u;
        default: { auto i = otros_.find(off); return i == otros_.end() ? 0 : i->second; }
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = mascara_be(be);
        auto w = [&](uint32_t& r) { r = (r & ~m) | (v & m); };
        switch (off) {
        case 0x08: w(cpu_per_conf_); recalcula(); return;
        case 0x10: w(perip_clk_en0_); return;
        case 0x14: w(perip_clk_en1_); return;
        case 0x18: w(perip_rst_en0_); return;
        case 0x1C: w(perip_rst_en1_); return;
        case 0x28: case 0x2C: case 0x30: case 0x34: {
            const unsigned n = (off - 0x28) / 4;
            const bool antes = from_cpu_[n] & 1u;
            w(from_cpu_[n]);
            from_cpu_[n] &= 1u;
            if (bool(from_cpu_[n]) != antes && irq_sw) irq_sw(n, from_cpu_[n]);
            return;
        }
        case 0x58: {
            const uint32_t xf = sysclk_conf_ & (0x7Fu << 12);
            w(sysclk_conf_);
            sysclk_conf_ = (sysclk_conf_ & ~(0x7Fu << 12)) | xf;    // CLK_XTAL_FREQ es RO
            recalcula();
            return;
        }
        default: { uint32_t& r = otros_[off]; r = (r & ~m) | (v & m); return; }
        }
    }

private:
    uint32_t cpu_per_conf_ = 0, sysclk_conf_ = 0, perip_clk_en0_ = 0, perip_clk_en1_ = 0,
             perip_rst_en0_ = 0, perip_rst_en1_ = 0, from_cpu_[4] = {};
    std::map<uint32_t, uint32_t> otros_;
    double hz_cpu_ = 20e6, hz_apb_ = 20e6;

    void recalcula() {
        const unsigned sel = (sysclk_conf_ >> 10) & 3u;
        const unsigned div = (sysclk_conf_ & 0x3FFu) + 1u;
        if (sel == 1) {
            const bool pll480 = (cpu_per_conf_ >> 2) & 1u;
            const unsigned ps = cpu_per_conf_ & 3u;
            hz_cpu_ = ps >= 1 ? 160e6 : 80e6;
            if (!pll480 && ps >= 1) hz_cpu_ = 160e6;           // 320/2
            hz_apb_ = 80e6;
        } else {
            hz_cpu_ = (sel == 2 ? hz_rc_fast : hz_xtal) / double(div);
            hz_apb_ = hz_cpu_;
        }
        if (al_cambiar_reloj) al_cambiar_reloj(hz_cpu_, hz_apb_);
    }
};

class RtcCntlC3 : public BloqueEsp {
public:
    // Reset del sistema (true) o de la CPU (false) pedido por software
    std::function<void(bool sistema)> reset_sw;
    // Lo que pide el RWDT al acabar una etapa: 1 interrupción, 2 la CPU, 3
    // el sistema, 4 el RTC (todo)
    std::function<void(unsigned accion)> rwdt;
    // El reloj lento [Hz]: el RC_SLOW nominal (SOC_CLK_RC_SLOW_FREQ_APPROX)
    double hz_lento = 136e3;
    // ¿Arranca el RWDT en modo flashboot? Solo cuando el chip arranca por la
    // ROM (ver la cabecera de timg_esp.h: lo mismo con el MWDT0)
    bool flashboot = false;
    // ¿Ha arrancado desde la Flash? (GPIO9 alto): solo entonces cuenta el
    // modo flashboot
    void pon_modo_flash(bool si) { modo_flash_ = si; ev_wdt_.notify(sc_core::SC_ZERO_TIME); }

    explicit RtcCntlC3(sc_core::sc_module_name nm, uint32_t base)
        : BloqueEsp(nm, base, 0x800, c3::REGS_RTC_CNTL) {
        SC_HAS_PROCESS(RtcCntlC3);
        SC_THREAD(rwdt_proc);
    }

    // Reset de chip (al encender, o el del RWDT): todo, STORE incluidos
    void reset_chip() {
        reset_regs();
        causa_ = 1;
        if (!flashboot) pon(WDT0, r(WDT0) & ~(1u << 12));
        reinicia();
    }
    // Cualquier reset: lo que no sobrevive
    void anota_causa(unsigned c) { causa_ = c & 0x3Fu; }
    unsigned causa() const { return causa_; }
    bool force_download() const { return r(0xF4) & 1u; }
    void baja_force_download() { pon(0xF4, r(0xF4) & ~1u); }
    uint32_t store(unsigned i) const {
        static const uint32_t off[8] = {0x50, 0x54, 0x58, 0x5C, 0xB8, 0xBC, 0xC0, 0xC4};
        return r(off[i & 7]);
    }
    uint64_t disparos_rwdt() const { return n_rwdt_; }
    bool rwdt_activo() const { return (r(WDT0) >> 31) || (((r(WDT0) >> 12) & 1u) && modo_flash_); }

protected:
    enum : uint32_t { WDT0 = 0x90, WDT1 = 0x94, WDTFEED = 0xA4, WDTWPROT = 0xA8 };
    static constexpr uint32_t CLAVE = 0x50D83AA1u;
    bool lee(uint32_t off, uint32_t& v) override {
        if (off == 0x38) { v = (r(0x38) & ~0xFFFu) | causa_ | (causa_ << 6); return true; }  // PROCPU y APPCPU
        return false;
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        if (off == 0x00) {
            if (v & m & (1u << 31)) { if (reset_sw) reset_sw(true); return true; }
            if (v & m & (1u << 5))  { if (reset_sw) reset_sw(false); return true; }
        }
        // El RWDT: WDTCONFIG0-4 y WDTFEED solo con la clave en WDTWPROTECT
        if (off >= WDT0 && off <= WDTFEED) {
            if (r(WDTWPROT) != CLAVE) return true;
            if (off == WDTFEED) { if (v & m & (1u << 31)) reinicia(); return true; }
            guarda(off, v, m);
            reinicia();
            return true;
        }
        return false;
    }

private:
    unsigned causa_ = 1, etapa_ = 0;
    bool modo_flash_ = false;
    uint64_t n_rwdt_ = 0;
    sc_core::sc_event ev_wdt_;
    // Una etapa cuenta desde que empieza: alimentar o reconfigurar la vuelve a
    // empezar; otros avisos (el modo de arranque) no
    sc_core::sc_time inicio_;
    void reinicia() { etapa_ = 0; inicio_ = sc_core::sc_time_stamp(); ev_wdt_.notify(sc_core::SC_ZERO_TIME); }
    // [TRM-C3 §12.3] Cuatro etapas: WDTCONFIG1-4 ciclos del reloj lento; la
    // acción de cada una en WDTCONFIG0 (STG0 30:28 ... STG3 21:19). En
    // flashboot sin EN, la etapa 0 resetea el RTC
    void rwdt_proc() {
        for (;;) {
            if (!rwdt_activo() || etapa_ > 3) { wait(ev_wdt_); inicio_ = sc_core::sc_time_stamp(); continue; }
            const unsigned e = etapa_;
            const uint32_t ciclos = r(WDT1 + 4 * e);
            const sc_core::sc_time fin = inicio_ + sc_core::sc_time(double(ciclos ? ciclos : 1u) / hz_lento, sc_core::SC_SEC);
            if (sc_core::sc_time_stamp() < fin) { wait(fin - sc_core::sc_time_stamp(), ev_wdt_); continue; }
            const bool fb = !(r(WDT0) >> 31);
            const unsigned accion = fb ? (e == 0 ? 4u : 0u) : (r(WDT0) >> (28 - 3 * e)) & 7u;
            etapa_ = e + 1;
            inicio_ = sc_core::sc_time_stamp();
            if (accion) { ++n_rwdt_; if (rwdt) rwdt(accion); }
        }
    }
};

// SYSCON (APB_CTRL), 0x6002_6000: lo de ESP-IDF con sus valores de reset, y
// el generador de números aleatorios, RND_DATA (0xB0). En el silicio lo
// alimenta el ruido del SAR ADC y de la radio; aquí es un xorshift32 con una
// semilla fija, para que cada simulación sea igual a la anterior [de modelo].
// Basta para lo que lo usa el arranque: el bootloader saca de ahí la máscara
// con que ofusca la RAM mientras carga, y repite hasta que no sale cero.
class SysconC3 : public BloqueEsp {
public:
    explicit SysconC3(sc_core::sc_module_name nm, uint32_t base)
        : BloqueEsp(nm, base, 0x1000, c3::REGS_SYSCON) {}
    void reset() { reset_regs(); }
protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off != 0xB0) return false;
        x_ ^= x_ << 13; x_ ^= x_ >> 17; x_ ^= x_ << 5;
        v = x_;
        return true;
    }
private:
    uint32_t x_ = 0xC3C3A5A5u;
};

// El bus I2C interno con los registros analógicos (REGI2C) y el control de
// los bloques analógicos, en 0x6000_E000 [regi2c_defs.h de ESP-IDF; la
// codificación, del código de la ROM, rom_chip_i2c_writeReg]:
//   0x00-0x0C  una palabra por maestro: [7:0] el bloque (BBPLL 0x66, BIAS
//              0x6A, DIG_REG 0x6D, ULP_CAL 0x61, SAR_ADC 0x69...), [15:8] el
//              registro, [23:16] el dato, bit 24 escribir, bit 25 ocupado. La
//              operación acaba en el acto: ocupado siempre a 0; una lectura
//              deja el dato en [23:16].
//   0x40-0x48  ANA_CONF0, ANA_CONFIG y ANA_CONFIG2 (encender y apagar el
//              BBPLL, el SAR...): guardan.
// Los registros analógicos GUARDAN lo que se les escribe y empiezan a cero:
// no hay PLL que enganchar ni calibración que hacer. Lo que tarde de verdad
// enganchar el BBPLL, no está (`limitaciones`).
class RegI2cC3 : public BusSlave {
public:
    explicit RegI2cC3(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x100) {}
    void reset() { r_.clear(); ana_.clear(); }
    uint8_t analogico(uint8_t bloque, uint8_t reg) const {
        auto i = ana_.find(uint16_t(bloque << 8 | reg));
        return i == ana_.end() ? 0 : i->second;
    }
    unsigned operaciones() const { return n_; }
protected:
    uint32_t reg_read(uint32_t off) override {
        auto i = r_.find(off);
        return i == r_.end() ? 0u : i->second;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = mascara_be(be);
        v = (reg_read(off) & ~m) | (v & m);
        if (off < 0x10) {
            ++n_;
            const uint16_t k = uint16_t((v & 0xFFu) << 8 | ((v >> 8) & 0xFFu));
            if (v & (1u << 24)) ana_[k] = uint8_t(v >> 16);
            else v = (v & ~0x00FF0000u) | (uint32_t(ana_.count(k) ? ana_[k] : 0) << 16);
            v &= ~(1u << 25);
        }
        r_[off] = v;
    }
private:
    std::map<uint32_t, uint32_t> r_;
    std::map<uint16_t, uint8_t> ana_;
    unsigned n_ = 0;
};

class EfuseC3 : public BloqueEsp {
public:
    static constexpr uint32_t RD_INI = 0x2C, RD_FIN = 0x17C;
    enum : uint32_t { CONF = 0x1CC, STATUS = 0x1D0, CMD = 0x1D4, INT_RAW = 0x1D8, INT_ST = 0x1DC,
                      INT_ENA = 0x1E0, INT_CLR = 0x1E4 };
    // `rev`: la del chip (4 = v0.4, 101 = v1.1)
    EfuseC3(sc_core::sc_module_name nm, uint32_t base, unsigned rev)
        : BloqueEsp(nm, base, 0x800, c3::REGS_EFUSE), rev_(rev) {
        pon_omision();
    }
    void reset() { reset_regs(); }
    // La imagen de omisión: la MAC 7C:DF:A1:00:C3:01, la revisión del chip y
    // la versión de los bloques (v1.3, la de los módulos de serie), y lo
    // demás a cero [esp_efuse_table.csv de ESP-IDF].
    void pon_omision() {
        w_.assign((RD_FIN - RD_INI) / 4, 0u);
        // MAC: RD_MAC_SPI_SYS_0 = los cuatro bytes bajos (en el orden de
        // ESP-IDF, el último byte de la MAC abajo)
        w_[(0x44 - RD_INI) / 4] = 0xA100C301u;
        w_[(0x48 - RD_INI) / 4] = 0x00007CDFu;
        // BLOCK1 (0x44): WAFER_VERSION_MINOR_LO bits 114-116, BLK_VERSION_MINOR
        // 120-122, WAFER_VERSION_MINOR_HI 183, WAFER_VERSION_MAJOR 184-185
        const unsigned may = rev_ / 100, men = rev_ % 100;
        w_[(0x50 - RD_INI) / 4] = ((men & 7u) << 18) | (3u << 24);
        w_[(0x58 - RD_INI) / 4] = (((men >> 3) & 1u) << 23) | ((may & 3u) << 24);
        // BLOCK2 (0x5C): BLK_VERSION_MAJOR bits 128-129 = 1 (con calibración)
        w_[(0x6C - RD_INI) / 4] = 1u;
        ruta_.clear();
    }
    // "" si se ha leído
    std::string carga(const std::string& ruta) {
        std::FILE* f = std::fopen(ruta.c_str(), "rb");
        if (!f) return "no se puede leer " + ruta;
        std::vector<uint32_t> w((RD_FIN - RD_INI) / 4, 0u);
        unsigned char b[4];
        size_t i = 0;
        while (i < w.size() && std::fread(b, 1, 4, f) == 4) {
            w[i++] = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        }
        std::fclose(f);
        if (i == 0) return ruta + " está vacío";
        w_ = w;
        ruta_ = ruta;
        return "";
    }
    uint32_t rd(uint32_t off) const {
        return off >= RD_INI && off < RD_FIN ? w_[(off - RD_INI) / 4] : 0u;
    }
    // La revisión que dice la imagen (100 * mayor + menor)
    unsigned revision() const {
        const unsigned lo = (rd(0x50) >> 18) & 7u, hi = (rd(0x58) >> 23) & 1u, may = (rd(0x58) >> 24) & 3u;
        return 100 * may + (hi << 3) + lo;
    }
    const std::string& ruta() const { return ruta_; }
    // Un bit de la imagen de lectura, puesto o quitado (como quemarlo, para
    // las opciones de montaje: jtag="pines")
    void pon_bit(uint32_t off, unsigned bit, bool v) {
        if (off < RD_INI || off >= RD_FIN) return;
        uint32_t& w = w_[(off - RD_INI) / 4];
        w = v ? (w | (1u << bit)) : (w & ~(1u << bit));
    }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        if (off >= RD_INI && off < RD_FIN) { v = rd(off); return true; }
        if (off == STATUS) { v = 1u; return true; }                  // STATE: en reposo
        if (off == INT_ST) { v = r(INT_RAW) & r(INT_ENA); return true; }
        return false;
    }
    // CMD: READ_CMD (bit 0) con CONF.OP_CODE = 0x5AA5 «vuelve a leer» los
    // eFuse (aquí, la imagen ya está) y deja READ_DONE; PGM_CMD (bit 1) con
    // 0x5A5A no programa nada (lo dice una vez) y deja PGM_DONE.
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
    unsigned rev_;
    std::vector<uint32_t> w_;
    std::string ruta_;
};

} // namespace stm32
#endif // STM32_SOC_ESP32C3_C3_SISTEMA_H
