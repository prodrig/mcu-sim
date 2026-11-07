// =============================================================================
// timg_esp.h — Un grupo de temporizadores de Espressif (TIMG) [TRM-C3 §11-12]
//
// E3 de doc/analisis_esp32_riscv.md, §4. En el C3 cada grupo tiene UN
// temporizador de propósito general de 54 bits (T0) y un perro guardián
// (MWDT):
//
//   T0     T0CONFIG: EN (31), INCREASE (30), AUTORELOAD (29), DIVIDER
//          (28:13: 0 es 65536, 1 es 2), ALARM_EN (10, se baja al saltar),
//          USE_XTAL (9). La cuenta sale del tiempo simulado. T0UPDATE copia
//          la cuenta a T0LO/HI; T0LOAD carga T0LOADLO/HI. La alarma pone
//          T0_INT_RAW y, con AUTORELOAD, recarga.
//   MWDT   WDTCONFIG0: EN (31), las acciones de las cuatro etapas (30:23:
//          0 nada, 1 interrupción, 2 reset de la CPU, 3 reset del sistema),
//          USE_XTAL (21), FLASHBOOT_MOD_EN (14). WDTCONFIG1: el preescalado
//          (31:16) del reloj (APB o XTAL), en ticks; WDTCONFIG2-5: el tiempo
//          de cada etapa en ticks del preescalado. WDTFEED vuelve a la etapa
//          0; WDTWPROTECT con 0x50D83AA1 abre la escritura de los WDT*.
//   INT    T0 (bit 0) y WDT (bit 1), de nivel, a la matriz.
//   CAL    la calibración del reloj lento (RTCCALICFG, 0x68-0x6C y 0x80): con
//          START (31) cuenta ciclos del XTAL durante CALI_MAX (30:16) ciclos
//          del reloj elegido en CLK_SEL (14:13: 0 RC_SLOW a 136 kHz, 1
//          RC_FAST/256 a 68,4 kHz, 2 XTAL32K, que solo está si la placa lo
//          pone). Al acabar, RDY (15) y el valor en RTCCALICFG1[31:7]; sin
//          reloj, TIMEOUT (RTCCALICFG2[0]). START_CYCLING (12), que sale a 1
//          del reset, da el valor sin esperar. Las frecuencias son las
//          nominales de ESP-IDF (SOC_CLK_*_FREQ_APPROX): sin deriva.
//
// EL ARRANQUE DESDE LA FLASH: el MWDT del grupo 0 sale del reset con
// FLASHBOOT_MOD_EN (14) y cuenta aunque EN esté a 0: si nadie lo para antes,
// la etapa 0 (WDTCONFIG2, 26 000 000 ticks del APB) resetea el sistema. Es la
// red de la ROM y del bootloader, que lo apaga en cuanto arranca
// (bootloader_config_wdt) [TRM-C3 §12.2.2]. `flashboot` dice si este grupo
// lo hace: el 0, y solo cuando el chip arranca por la ROM; con un ELF cargado
// en la SRAM (el arranque directo de E3, que no existe en el silicio) sale
// apagado como en E3. Y el modo flashboot solo cuenta si el chip ha
// arrancado DESDE LA FLASH (GPIO9 alto al soltar CHIP_PU): en el modo
// descarga la ROM espera sin que nadie la resetee (`modo_flash`, que pone
// el chip al guardar los pines de arranque).
// =============================================================================
#ifndef STM32_PERIPH_ESP_TIMG_ESP_H
#define STM32_PERIPH_ESP_TIMG_ESP_H

#include <systemc>
#include <cmath>
#include <functional>
#include "../../common/periph_base.h"

namespace stm32 {

class TimgEsp : public BusSlave {
public:
    std::function<void(unsigned bit, bool nivel)> irq;     // 0 T0, 1 WDT
    std::function<void(bool sistema)> reset_wdt;           // la etapa pide un reset
    double hz_apb = 80e6, hz_xtal = 40e6;
    bool flashboot = false;            // ver la cabecera
    bool modo_flash = false;
    void pon_modo_flash(bool si) { modo_flash = si; ev_wdt_.notify(sc_core::SC_ZERO_TIME); }
    // Los relojes lentos que se pueden calibrar [Hz]; 0: no está
    double hz_rc_slow = 136e3, hz_rc_fast_d256 = 17.5e6 / 256.0, hz_xtal32k = 0;

    TimgEsp(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x1000) {
        SC_HAS_PROCESS(TimgEsp);
        SC_THREAD(alarma_proc);
        SC_THREAD(wdt_proc);
        reset_regs();
    }

    void reset() { reset_regs(); publica(); ev_.notify(sc_core::SC_ZERO_TIME); ev_wdt_.notify(sc_core::SC_ZERO_TIME); }
    static constexpr uint64_t M54 = (uint64_t(1) << 54) - 1;
    static constexpr uint32_t CLAVE_WDT = 0x50D83AA1u;

    double hz_t0() const {
        const unsigned d = (t0conf_ >> 13) & 0xFFFFu;
        const double div = d == 0 ? 65536.0 : d == 1 ? 2.0 : double(d);
        return (((t0conf_ >> 9) & 1u) ? hz_xtal : hz_apb) / div;
    }
    uint64_t cuenta() const {
        if (!(t0conf_ >> 31)) return base_;
        const double dt = (sc_core::sc_time_stamp() - t_).to_seconds();
        const uint64_t n = uint64_t(std::floor(dt * hz_t0() + 1e-6));
        return ((t0conf_ >> 30) & 1u ? base_ + n : base_ - n) & M54;
    }
    uint64_t alarmas() const { return n_alarmas_; }
    unsigned etapa_wdt() const { return etapa_; }
    uint64_t resets_wdt() const { return n_resets_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return t0conf_;
        case 0x04: return uint32_t(snap_);
        case 0x08: return uint32_t(snap_ >> 32) & 0x3FFFFFu;
        case 0x10: return uint32_t(alarma_);
        case 0x14: return uint32_t(alarma_ >> 32) & 0x3FFFFFu;
        case 0x18: return uint32_t(carga_);
        case 0x1C: return uint32_t(carga_ >> 32) & 0x3FFFFFu;
        case 0x48: return wdt0_;
        case 0x4C: return wdt1_;
        case 0x50: case 0x54: case 0x58: case 0x5C: return wdt_etapa_[(off - 0x50) / 4];
        case 0x64: return wprot_;
        case 0x70: return ena_;
        case 0x74: return raw_;
        case 0x78: return raw_ & ena_;
        case 0x68: return cal0_ | (cal_rdy() ? 1u << 15 : 0u);
        case 0x6C: return cal_rdy() ? (cal_valor() << 7) : 0u;
        case 0x80: return (cal2_ & ~1u) | (cal_sin_reloj() ? 1u : 0u);
        case 0xF8: return 0x2006191u;
        case 0xFC: return regclk_;
        default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = (be & 1 ? 0xFFu : 0) | (be & 2 ? 0xFF00u : 0) |
                           (be & 4 ? 0xFF0000u : 0) | (be & 8 ? 0xFF000000u : 0);
        auto w = [&](uint32_t& r) { r = (r & ~m) | (v & m); };
        const bool abierto = wprot_ == CLAVE_WDT;
        switch (off) {
        case 0x00: {
            const uint64_t c = cuenta();
            w(t0conf_);
            base_ = c; t_ = sc_core::sc_time_stamp();
            ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        case 0x0C: snap_ = cuenta(); return;
        case 0x10: alarma_ = (alarma_ & ~uint64_t(m)) | (v & m); ev_.notify(sc_core::SC_ZERO_TIME); return;
        case 0x14: alarma_ = (alarma_ & 0xFFFFFFFFull) | (uint64_t(v & m & 0x3FFFFFu) << 32); ev_.notify(sc_core::SC_ZERO_TIME); return;
        case 0x18: carga_ = (carga_ & ~uint64_t(m)) | (v & m); return;
        case 0x1C: carga_ = (carga_ & 0xFFFFFFFFull) | (uint64_t(v & m & 0x3FFFFFu) << 32); return;
        case 0x20: base_ = carga_ & M54; t_ = sc_core::sc_time_stamp(); ev_.notify(sc_core::SC_ZERO_TIME); return;
        case 0x48: if (abierto) { w(wdt0_); wdt_reinicia(); } return;
        case 0x4C: if (abierto) { w(wdt1_); wdt_reinicia(); } return;
        case 0x50: case 0x54: case 0x58: case 0x5C:
            if (abierto) { w(wdt_etapa_[(off - 0x50) / 4]); wdt_reinicia(); }
            return;
        case 0x60: if (abierto) wdt_reinicia(); return;
        case 0x64: w(wprot_); return;
        case 0x68: {
            const bool antes = cal0_ >> 31;
            w(cal0_);
            cal0_ &= ~(1u << 15);
            if (!antes && (cal0_ >> 31)) {
                const double f = cal_hz();
                cal_fin_ = sc_core::sc_time_stamp() +
                           (f > 0 ? sc_core::sc_time(double((cal0_ >> 16) & 0x7FFFu) / f, sc_core::SC_SEC)
                                  : sc_core::SC_ZERO_TIME);
            }
            return;
        }
        case 0x80: w(cal2_); return;
        case 0x70: w(ena_); publica(); return;
        case 0x7C: raw_ &= ~(v & m); publica(); return;
        case 0xFC: w(regclk_); return;
        default: return;
        }
    }

private:
    uint32_t cal0_ = 0x13000u, cal2_ = 0xFFFFFF98u;
    sc_core::sc_time cal_fin_;
    double cal_hz() const {
        const unsigned sel = (cal0_ >> 13) & 3u;
        return sel == 0 ? hz_rc_slow : sel == 1 ? hz_rc_fast_d256 : hz_xtal32k;
    }
    bool cal_sin_reloj() const { return cal_hz() <= 0 && ((cal0_ >> 31) || (cal0_ & (1u << 12))); }
    bool cal_rdy() const {
        if (cal_hz() <= 0) return false;
        if (cal0_ & (1u << 12)) return true;
        return (cal0_ >> 31) && sc_core::sc_time_stamp() >= cal_fin_;
    }
    uint32_t cal_valor() const {
        const double n = double((cal0_ >> 16) & 0x7FFFu);
        return uint32_t(std::lround(n * hz_xtal / cal_hz())) & 0x1FFFFFFu;
    }
    uint32_t t0conf_ = 0, wdt0_ = 0, wdt1_ = 0, wdt_etapa_[4] = {}, wprot_ = 0,
             ena_ = 0, raw_ = 0, regclk_ = 0, irq_ = 0;
    uint64_t base_ = 0, snap_ = 0, alarma_ = 0, carga_ = 0;
    sc_core::sc_time t_;
    sc_core::sc_event ev_, ev_wdt_;
    unsigned etapa_ = 0;
    uint64_t n_alarmas_ = 0, n_resets_ = 0;

    void reset_regs() {
        t0conf_ = 0x60002000u;
        cal0_ = 0x13000u; cal2_ = 0xFFFFFF98u; cal_fin_ = sc_core::SC_ZERO_TIME;
        wdt0_ = flashboot ? 0x0004C000u : 0u;     // ver la cabecera
        wdt1_ = 1u << 16;
        wdt_etapa_[0] = 26000000u;     // 0x018CBA80 wdt_etapa_[1] = 0x7FFFFFFu;
        wdt_etapa_[2] = 0xFFFFFu;  wdt_etapa_[3] = 0xFFFFFu;
        wprot_ = CLAVE_WDT;
        ena_ = raw_ = 0;
        regclk_ = 1u << 31;
        base_ = snap_ = alarma_ = carga_ = 0;
    }
    void publica() {
        const uint32_t n = raw_ & ena_;
        for (unsigned i = 0; i < 2; ++i) {
            const bool a = (n >> i) & 1u, b = (irq_ >> i) & 1u;
            if (a != b && irq) irq(i, a);
        }
        irq_ = n;
    }

    void alarma_proc() {
        for (;;) {
            if (!(t0conf_ >> 31) || !((t0conf_ >> 10) & 1u)) { wait(ev_); continue; }
            const uint64_t c = cuenta();
            const bool sube = (t0conf_ >> 30) & 1u;
            const bool llega = sube ? c >= alarma_ : c <= alarma_;
            if (llega) {
                raw_ |= 1u;
                ++n_alarmas_;
                t0conf_ &= ~(1u << 10);
                if ((t0conf_ >> 29) & 1u) { base_ = carga_ & M54; t_ = sc_core::sc_time_stamp(); }
                publica();
                continue;
            }
            const uint64_t falta = sube ? alarma_ - c : c - alarma_;
            wait(sc_core::sc_time(double(falta) / hz_t0(), sc_core::SC_SEC) +
                 sc_core::sc_time(1, sc_core::SC_NS), ev_);
        }
    }

    void wdt_reinicia() { etapa_ = 0; ev_wdt_.notify(sc_core::SC_ZERO_TIME); }
    sc_core::sc_time t_etapa(unsigned e) const {
        const double hz = (((wdt0_ >> 21) & 1u) ? hz_xtal : hz_apb) /
                          double(((wdt1_ >> 16) & 0xFFFFu) ? ((wdt1_ >> 16) & 0xFFFFu) : 1u);
        return sc_core::sc_time(double(wdt_etapa_[e] ? wdt_etapa_[e] : 1u) / hz, sc_core::SC_SEC);
    }
    // En marcha: EN, o FLASHBOOT_MOD_EN (y entonces la etapa 0 resetea el
    // sistema, se diga lo que se diga en STG0)
    bool wdt_activo() const { return (wdt0_ >> 31) || (((wdt0_ >> 14) & 1u) && modo_flash); }
    void wdt_proc() {
        for (;;) {
            if (!wdt_activo()) { wait(ev_wdt_); continue; }
            const unsigned e = etapa_;
            if (e > 3) { wait(ev_wdt_); continue; }
            const sc_core::sc_time t0 = sc_core::sc_time_stamp();
            wait(t_etapa(e), ev_wdt_);
            if (sc_core::sc_time_stamp() - t0 < t_etapa(e) || etapa_ != e || !wdt_activo()) continue;
            const bool fb = !(wdt0_ >> 31);
            const unsigned accion = fb ? (e == 0 ? 3u : 0u) : (wdt0_ >> (29 - 2 * e)) & 3u;
            if (accion == 1) { raw_ |= 2u; publica(); }
            else if (accion >= 2) { ++n_resets_; if (reset_wdt) reset_wdt(accion == 3); }
            etapa_ = e + 1;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_ESP_TIMG_ESP_H
