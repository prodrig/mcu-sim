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
//                software. El RWDT y el SWD: guardados, sin contar (E4).
//   EfuseC3    0x6000_8800  los registros de lectura (RD_*, 0x2C-0x17B) de
//                una imagen: por omisión, una MAC 7C:DF:A1:00:C3:01 y el
//                resto a cero; o un fichero de hasta 336 bytes con las
//                palabras en el orden de los registros (BLOCK0, BLOCK1,
//                BLOCK2-10). [TRM-C3 §4.4]. Ni programar ni leer por
//                órdenes: E4.
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

class RtcCntlC3 : public BusSlave {
public:
    // Reset del sistema (true) o de la CPU (false) pedido por software
    std::function<void(bool sistema)> reset_sw;

    explicit RtcCntlC3(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x800) {}

    // Reset de chip (al encender): todo, STORE incluidos
    void reset_chip() { r_.clear(); causa_ = 1; force_dl_ = 0; }
    // Cualquier reset: lo que no sobrevive
    void anota_causa(unsigned c) { causa_ = c & 0x3Fu; }
    unsigned causa() const { return causa_; }
    bool force_download() const { return force_dl_ & 1u; }
    void baja_force_download() { force_dl_ = 0; }
    uint32_t store(unsigned i) const {
        static const uint32_t off[8] = {0x50, 0x54, 0x58, 0x5C, 0xB8, 0xBC, 0xC0, 0xC4};
        auto it = r_.find(off[i & 7]);
        return it == r_.end() ? 0 : it->second;
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == 0x38) return causa_ | (causa_ << 6);      // PROCPU y APPCPU (sin APP)
        if (off == 0xF4) return force_dl_;
        auto i = r_.find(off);
        return i == r_.end() ? 0 : i->second;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = mascara_be(be);
        if (off == 0xF4) { force_dl_ = (force_dl_ & ~m) | (v & m & 1u); return; }
        if (off == 0x00) {
            if (v & m & (1u << 31)) { if (reset_sw) reset_sw(true); return; }
            if (v & m & (1u << 5))  { if (reset_sw) reset_sw(false); return; }
        }
        uint32_t& r = r_[off];
        r = (r & ~m) | (v & m);
    }

private:
    std::map<uint32_t, uint32_t> r_;
    unsigned causa_ = 1;
    uint32_t force_dl_ = 0;
};

class EfuseC3 : public BusSlave {
public:
    static constexpr uint32_t RD_INI = 0x2C, RD_FIN = 0x17C;
    explicit EfuseC3(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x800) {
        pon_omision();
    }
    void pon_omision() {
        w_.assign((RD_FIN - RD_INI) / 4, 0u);
        // MAC 7C:DF:A1:00:C3:01: RD_MAC_SPI_SYS_0 = los cuatro bytes bajos
        // (en el orden de ESP-IDF, el último byte de la MAC abajo)
        w_[(0x44 - RD_INI) / 4] = 0xA100C301u;
        w_[(0x48 - RD_INI) / 4] = 0x00007CDFu;
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
    const std::string& ruta() const { return ruta_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off >= RD_INI && off < RD_FIN) return rd(off);
        if (off == 0x1FC) return 0x2007200u;
        return 0;
    }
    void reg_write(uint32_t, uint32_t, uint32_t) override {}

private:
    std::vector<uint32_t> w_;
    std::string ruta_;
};

} // namespace stm32
#endif // STM32_SOC_ESP32C3_C3_SISTEMA_H
