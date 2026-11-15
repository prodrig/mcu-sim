// =============================================================================
// c6_lp.h — Lo de bajo consumo del ESP32-C6: el PMU (el núcleo LP y el sueño
//           del HP), el RTC_TIMER (LP_TIMER), el LP IO y el controlador de
//           interrupciones del núcleo LP
//
// E10 de doc/analisis_esp32_riscv.md [TRM-C6 §2 (el LP CPU), §11 (el sueño),
// §12 (el RTC timer), §7.9 (el LP IO); pmu_reg.h, lp_timer_reg.h,
// lp_io_reg.h, lp_core_ll.h, pmu_ll.h, lp_timer_ll.h y rtc_io_ll.h de
// ESP-IDF]. Todo esto vive en el dominio LP: un reset del sistema (el de
// esp_restart, el del despertar del sueño profundo) no lo toca; solo el de
// chip.
//
// EL PMU, lo que hace de verdad (el resto de sus registros guarda):
//   el núcleo LP   LP_CPU_PWR0 (0x17C): SLP_STALL_EN (29), SLP_RESET_EN (30);
//                  LP_CPU_PWR1 (0x180): WAKEUP_EN [15:0] (0 el HP, 1 la LP
//                  UART, 2 el LP IO, 3 el ETM, 4 el LP timer) y SLEEP_REQ
//                  (31, se borra solo). El LP duerme al pedirlo él (o el HP)
//                  y despierta con una fuente habilitada: si SLP_RESET_EN,
//                  desde el reset (0x5000_0080); si no, donde se paró. Tras
//                  el reset de chip, dormido y en reset.
//   HP <-> LP      HP_LP_CPU_COMM (0x184): HP_TRIGGER_LP (31) pone
//                  HP_SW_TRIGGER en las interrupciones del LP (y lo
//                  despierta); LP_TRIGGER_HP (30) despierta al HP de su sueño
//                  (la fuente LP_CORE, 11) o, despierto, pone SW_INT (29) en
//                  las suyas. Los dos se borran solos.
//   el sueño HP    SLP_WAKEUP_CNTL0 (0x120) SLEEP_REQ (31): el HP duerme;
//                  CNTL2 (0x128): las fuentes que lo despiertan (4 el LP
//                  timer, 11 el núcleo LP, 2 el GPIO...); STATUS0 (0x140):
//                  la que lo despertó. Profundo o ligero lo dice LP_AON
//                  STORE9[0] (lo escribe ESP-IDF antes de dormir): el
//                  profundo acaba en un reset del sistema con la causa 5
//                  (CORE_DEEP_SLEEP); el ligero sigue donde estaba, con
//                  SOC_WAKEUP (31) en INT_RAW.
//   interrupciones INT_RAW/ST/ENA/CLR del HP (0x15C-0x168), a la fuente 13
//                  de la matriz; LP_INT_RAW/ST/ENA/CLR (0x16C-0x178), al
//                  núcleo LP (LPPERI_INTERRUPT_SOURCE, bit 5).
//
// EL LP TIMER: un contador de 48 bits a 136 kHz (el RC_SLOW, el mismo que
// calibra el TIMG) desde el reset de chip; UPDATE (0x10, bit 28) lo copia en
// BUF0 y BUF1. Dos alarmas: TAR0 (0x00/0x04) es la del HP (INT_RAW 31,
// SOC_WAKEUP, a la fuente 15, y despierta al HP) y TAR1 (0x08/0x0C) la del
// núcleo LP (LP_INT_RAW 31, y lo despierta). Escribir EN (bit 31 de la parte
// alta, de disparo) arma la alarma con el objetivo de ese momento; salta una
// vez, cuando el contador llega.
//
// EL LP IO (LP_GPIO0-7, los pines 0-7): OUT/ENABLE con W1TS/W1TC, STATUS,
// IN, PINn (INT_TYPE [9:7], WAKEUP_ENABLE 10), GPIOn (FUN_WPD 7, FUN_WPU 8,
// FUN_IE 9, FUN_DRV [11:10], MCU_SEL [14:12]). Un pin es del LP IO si su bit
// de LP_AON_GPIO_MUX (0x28) está a 1; con MCU_SEL = 1, de la LP UART (TX en
// 5, RX en 4) o del LP I2C (SDA 6, SCL 7).
// =============================================================================
#ifndef STM32_SOC_ESP32C6_C6_LP_H
#define STM32_SOC_ESP32C6_C6_LP_H

#include <cmath>
#include <cstdint>
#include <functional>
#include "c6_sistema.h"
#include "../../periph/esp/gpio_esp.h"
#include "../../rv/hart/rv_ifaces.h"

namespace stm32 {

namespace c6 {
// El núcleo LP [TRM-C6 §2; de modelo los tiempos: dos etapas]
inline constexpr RvTiempos RV_TIEMPOS_C6_LP{1, 2, 1, 1, 2, 2, 3, 2, 33, 1, 2, 3, 3, 4, 0};
inline constexpr RvDebugCaps RV_DBG_C6_LP{2, true, 16, 2, false, true, 0x4, 0x20000000u, 0x00001000u};
// Las fuentes de LPPERI_INTERRUPT_SOURCE [lp_core_interrupt.c]
enum : unsigned { LPI_IO = 0, LPI_I2C = 1, LPI_UART = 2, LPI_TIMER = 3, LPI_PMU = 5 };
// Las fuentes que despiertan al núcleo LP (PMU_LP_CPU_WAKEUP_EN) [lp_core_ll.h]
enum : unsigned { DLP_HP = 0, DLP_UART = 1, DLP_IO = 2, DLP_ETM = 3, DLP_TIMER = 4 };
// Y al HP (PMU_SLP_WAKEUP_CNTL2) [pmu_bit_defs.h]
enum : unsigned { DHP_EXT1 = 1, DHP_GPIO = 2, DHP_TIMER = 4, DHP_UART0 = 6, DHP_UART1 = 7, DHP_LP_CORE = 11 };
} // namespace c6

// RV32IMAC solo con M, mie/mip con una sola interrupción (la 30, todas las
// del dominio LP por LPPERI_INTERRUPT_SOURCE), mtvec solo vectorizado (como
// el HP: la 30 va a mtvec + 4·30), contadores estándar, sin PMP ni PMA, 2
// triggers. Sale del reset en 0x5000_0080, detrás de la tabla de vectores.
inline constexpr RvCaps RV_ESP32C6_LP{
    "ESP32-C6 LP", 32,
    RV_EXT_I | RV_EXT_M | RV_EXT_A | RV_EXT_C,
    RV_Z_ICSR | RV_Z_IFENCEI | RV_Z_ICNTR,
    true, 1u << 30, false, 0, RV_MTVEC_VECTORIZADO, 256,
    0x50000080u, 0x50000001u, 0, true, RvDesalineado::Trap,
    0x612u, 0x80000003u, 1u, false, c6::RV_DBG_C6_LP, &c6::RV_TIEMPOS_C6_LP
};

// --- El controlador de interrupciones del núcleo LP ---------------------------------
class LpIrqC6 : public rv_irq_if {
public:
    std::function<uint32_t()> fuentes;          // LPPERI_INTERRUPT_SOURCE
    uint32_t pendientes() override { return fuentes && fuentes() ? (1u << 30) : 0u; }
    bool elige(uint32_t c, unsigned& id) override {
        if (!(c & (1u << 30))) return false;
        id = 30;
        return true;
    }
};

// --- El PMU -----------------------------------------------------------------------------
class PmuC6 : public BloqueEsp {
public:
    enum : uint32_t {
        CNTL0 = 0x120, CNTL2 = 0x128, STATUS0 = 0x140, INT_RAW = 0x15C, INT_ST = 0x160, INT_ENA = 0x164,
        INT_CLR = 0x168, LP_RAW = 0x16C, LP_ST = 0x170, LP_ENA = 0x174, LP_CLR = 0x178,
        LP_PWR0 = 0x17C, LP_PWR1 = 0x180, COMM = 0x184,
    };
    // El SoC: el núcleo LP (duerme / despierta, con reset o sin él) y el HP
    std::function<void(bool reset)> lp_duerme, lp_despierta;
    std::function<void()> hp_duerme;
    std::function<void()> hp_despierta;
    std::function<void(bool)> irq_hp, irq_lp;

    PmuC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x400, c6::REGS_PMU) { reset_chip(); }
    void reset_chip() {
        reset_regs();
        raw_ = lp_raw_ = 0;
        lp_dormido_ = true;
        lp_con_reset_ = true;
        hp_dormido_ = false;
        publica();
    }
    bool lp_dormido() const { return lp_dormido_; }
    bool hp_dormido() const { return hp_dormido_; }
    uint32_t despertares_lp() const { return n_lp_; }

    // Una fuente del dominio LP: despierta al núcleo LP si está habilitada
    void fuente_lp(unsigned bit) {
        if (lp_dormido_ && (r(LP_PWR1) >> bit & 1u)) despierta_lp();
    }
    // Una fuente que despierta al HP dormido
    void fuente_hp(unsigned bit) {
        if (!hp_dormido_ || !(r(CNTL2) >> bit & 1u)) return;
        hp_dormido_ = false;
        pon(STATUS0, 1u << bit);
        raw_ |= 1u << 31;                          // SOC_WAKEUP
        publica();
        if (hp_despierta) hp_despierta();
    }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        switch (off) {
        case INT_RAW: v = raw_; return true;
        case INT_ST:  v = raw_ & r(INT_ENA); return true;
        case LP_RAW:  v = lp_raw_; return true;
        case LP_ST:   v = lp_raw_ & r(LP_ENA); return true;
        case CNTL0: case COMM: v = 0; return true;
        case LP_PWR1: v = r(LP_PWR1) & 0xFFFFu; return true;
        default: return false;
        }
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        v &= m;
        switch (off) {
        case INT_ENA: pon(off, (r(off) & ~m) | v); publica(); return true;
        case INT_CLR: raw_ &= ~v; publica(); return true;
        case INT_RAW: return true;
        case LP_ENA:  pon(off, (r(off) & ~m) | v); publica(); return true;
        case LP_CLR:  lp_raw_ &= ~v; publica(); return true;
        case LP_RAW:  return true;
        case LP_PWR0: pon(off, (r(off) & ~m) | v); return true;
        case CNTL2:   pon(off, (r(off) & ~m) | v); return true;
        case LP_PWR1:
            pon(off, (r(off) & ~(m & 0xFFFFu)) | (v & 0xFFFFu));
            if (v >> 31) duerme_lp();
            return true;
        case COMM:
            if ((v >> 31) & 1u) {                      // HP_TRIGGER_LP
                lp_raw_ |= 1u << 31;
                publica();
                fuente_lp(c6::DLP_HP);
            }
            if ((v >> 30) & 1u) {                      // LP_TRIGGER_HP
                if (hp_dormido_) fuente_hp(c6::DHP_LP_CORE);
                else { raw_ |= 1u << 29; publica(); }
            }
            return true;
        case CNTL0:
            if ((v >> 31) & 1u && !hp_dormido_) {
                hp_dormido_ = true;
                if (hp_duerme) hp_duerme();
            }
            return true;
        default: return false;
        }
    }

private:
    uint32_t raw_ = 0, lp_raw_ = 0, n_lp_ = 0;
    bool lp_dormido_ = true, lp_con_reset_ = true, hp_dormido_ = false, irq_hp_ = false, irq_lp_ = false;

    void duerme_lp() {
        if (lp_dormido_) return;
        lp_dormido_ = true;
        lp_con_reset_ = (r(LP_PWR0) >> 30) & 1u;
        if (lp_duerme) lp_duerme(lp_con_reset_);
    }
    void despierta_lp() {
        lp_dormido_ = false;
        ++n_lp_;
        if (lp_despierta) lp_despierta(lp_con_reset_);
    }
    void publica() {
        const bool h = (raw_ & r(INT_ENA)) != 0, l = (lp_raw_ & r(LP_ENA)) != 0;
        if (h != irq_hp_) { irq_hp_ = h; if (irq_hp) irq_hp(h); }
        if (l != irq_lp_) { irq_lp_ = l; if (irq_lp) irq_lp(l); }
    }
};

// --- El LP timer (RTC_TIMER) ---------------------------------------------------------------
class LpTimerC6 : public BloqueEsp {
public:
    enum : uint32_t {
        TAR0_LO = 0x00, TAR0_HI = 0x04, TAR1_LO = 0x08, TAR1_HI = 0x0C, UPDATE = 0x10, BUF0_LO = 0x14,
        BUF0_HI = 0x18, BUF1_LO = 0x1C, BUF1_HI = 0x20, INT_RAW = 0x28, INT_ST = 0x2C, INT_ENA = 0x30,
        INT_CLR = 0x34, LP_RAW = 0x38, LP_ST = 0x3C, LP_ENA = 0x40, LP_CLR = 0x44,
    };
    double hz = 136e3;                                     // el RC_SLOW
    std::function<void(bool)> irq_hp, irq_lp;
    std::function<void(unsigned)> alarma;                  // 0 la del HP, 1 la del LP

    LpTimerC6(sc_core::sc_module_name nm, uint32_t base) : BloqueEsp(nm, base, 0x400, c6::REGS_LP_TIMER) {
        SC_HAS_PROCESS(LpTimerC6);
        SC_THREAD(alarma_proc);
    }
    void reset_chip() {
        reset_regs();
        t0_ = sc_core::sc_time_stamp();
        raw_ = lp_raw_ = 0;
        armada_[0] = armada_[1] = false;
        publica();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    uint64_t cuenta() const {
        return uint64_t(std::floor((sc_core::sc_time_stamp() - t0_).to_seconds() * hz + 1e-9)) & ((uint64_t(1) << 48) - 1);
    }

protected:
    bool lee(uint32_t off, uint32_t& v) override {
        switch (off) {
        case INT_RAW: v = raw_; return true;
        case INT_ST:  v = raw_ & r(INT_ENA); return true;
        case LP_RAW:  v = lp_raw_; return true;
        case LP_ST:   v = lp_raw_ & r(LP_ENA); return true;
        case UPDATE:  v = r(UPDATE) & ~(1u << 28); return true;
        default: return false;
        }
    }
    bool escribe(uint32_t off, uint32_t v, uint32_t m) override {
        switch (off) {
        case UPDATE:
            guarda(off, v, m & ~(1u << 28));
            if ((v & m) >> 28 & 1u) {
                const uint64_t c = cuenta();
                for (uint32_t b : {BUF0_LO, BUF1_LO}) { pon(b, uint32_t(c)); pon(b + 4, uint32_t(c >> 32) & 0xFFFFu); }
            }
            return true;
        case TAR0_LO: case TAR0_HI: case TAR1_LO: case TAR1_HI: {
            // TAR_EN (31) es un disparo (WT): al escribirlo a 1, la alarma
            // toma el objetivo que haya en ese momento [lp_timer_reg.h]
            pon(off, (r(off) & ~m) | (v & m & ((off & 4u) ? 0xFFFFu : 0xFFFFFFFFu)));
            const unsigned t = off >= TAR1_LO;
            if ((off & 4u) && ((v & m) >> 31)) {
                const uint32_t b = t ? TAR1_LO : TAR0_LO;
                objetivo_[t] = (uint64_t(r(b + 4) & 0xFFFFu) << 32) | r(b);
                armada_[t] = true;
                ev_.notify(sc_core::SC_ZERO_TIME);
            }
            return true;
        }
        case INT_ENA: guarda(off, v, m); publica(); return true;
        case INT_CLR: raw_ &= ~(v & m); publica(); return true;
        case LP_ENA:  guarda(off, v, m); publica(); return true;
        case LP_CLR:  lp_raw_ &= ~(v & m); publica(); return true;
        case INT_RAW: case LP_RAW: return true;
        default: return false;
        }
    }

private:
    sc_core::sc_time t0_ = sc_core::SC_ZERO_TIME;
    sc_core::sc_event ev_;
    uint32_t raw_ = 0, lp_raw_ = 0;
    bool armada_[2] = {false, false}, irq_hp_ = false, irq_lp_ = false;

    uint64_t objetivo_[2] = {0, 0};
    uint64_t objetivo(unsigned t) const { return objetivo_[t]; }
    bool habilitada(unsigned) const { return true; }
    void publica() {
        const bool h = (raw_ & r(INT_ENA)) != 0, l = (lp_raw_ & r(LP_ENA)) != 0;
        if (h != irq_hp_) { irq_hp_ = h; if (irq_hp) irq_hp(h); }
        if (l != irq_lp_) { irq_lp_ = l; if (irq_lp) irq_lp(l); }
    }
    void alarma_proc() {
        for (;;) {
            double prox = -1;
            const uint64_t c = cuenta();
            for (unsigned t = 0; t < 2; ++t) {
                if (!armada_[t] || !habilitada(t)) continue;
                const uint64_t o = objetivo(t);
                if (c >= o) {
                    armada_[t] = false;
                    if (t == 0) raw_ |= 1u << 31; else lp_raw_ |= 1u << 31;
                    publica();
                    if (alarma) alarma(t);
                    continue;
                }
                const double s = double(o - c) / hz;
                if (prox < 0 || s < prox) prox = s;
            }
            if (prox < 0) wait(ev_);
            else {
                // Al tick: sc_time de ps, un poco más allá para que cuente() llegue
                const double hasta = (t0_.to_seconds() + double(c) / hz) + prox;
                const double faltan = hasta - sc_core::sc_time_stamp().to_seconds();
                wait(sc_core::sc_time(std::max(faltan, 0.0) * 1e12 + 1.0, sc_core::SC_PS), ev_);
            }
        }
    }
};

// --- El LP IO ---------------------------------------------------------------------------
class LpIoC6 : public BusSlave {
public:
    static constexpr unsigned N = 8;
    std::function<void(bool)> irq;                 // a LPPERI (fuente 0) y al despertar
    std::function<void()> despierta;               // un pin con WAKEUP_ENABLE se cumple
    std::function<uint32_t()> mux;                 // LP_AON_GPIO_MUX: los pines del LP IO
    std::function<bool(unsigned)> nivel;           // el nivel del pad
    // Lo que sacan los periféricos LP por la función 1 (MCU_SEL = 1)
    std::function<bool(unsigned pin, bool& out, bool& oe)> periferico;

    explicit LpIoC6(sc_core::sc_module_name nm, uint32_t base) : BusSlave(nm, base, 0x400) { reset_chip(); }
    void reset_chip() {
        out_ = oe_ = status_ = 0;
        for (unsigned i = 0; i < N; ++i) { pin_[i] = 0; gpio_[i] = 0; }
        ultimo_ = 0;
        publica();
        if (cambia) cambia();
    }
    std::function<void()> cambia;                  // a la GPIO: los pads del LP han cambiado

    // ¿Gobierna el LP IO el pad p? Y cómo
    bool pad(unsigned p, PadDrive& d) const {
        if (p >= N || !mux || !((mux() >> p) & 1u)) return false;
        const uint32_t g = gpio_[p];
        d.pupd = (g >> 8 & 1u) ? 1 : (g >> 7 & 1u) ? 2 : 0;
        d.speed = uint8_t((g >> 10) & 3u);
        d.od = (pin_[p] >> 2) & 1u;
        bool o = (out_ >> p) & 1u, e = (oe_ >> p) & 1u;
        if (((g >> 12) & 7u) == 1 && periferico) periferico(p, o, e);
        d.out = o;
        d.oe = e;
        return true;
    }
    bool ie(unsigned p) const { return p < N && ((gpio_[p] >> 9) & 1u); }
    // ¿Es el pad p del LP IO con la función f (MCU_SEL)?
    bool funcion(unsigned p, unsigned f) const {
        return p < N && mux && ((mux() >> p) & 1u) && ((gpio_[p] >> 12) & 7u) == f;
    }
    // El pad ha cambiado: las interrupciones y el despertar
    void entradas() {
        const uint32_t in = lee_in(), sube = in & ~ultimo_, baja = ~in & ultimo_;
        bool dsp = false;
        for (unsigned p = 0; p < N; ++p) {
            const unsigned t = (pin_[p] >> 7) & 7u;
            const bool b = (in >> p) & 1u;
            bool salta = false;
            switch (t) {
            case 1: salta = (sube >> p) & 1u; break;
            case 2: salta = (baja >> p) & 1u; break;
            case 3: salta = ((sube | baja) >> p) & 1u; break;
            case 4: salta = !b; break;
            case 5: salta = b; break;
            default: break;
            }
            if (salta) { status_ |= 1u << p; if ((pin_[p] >> 10) & 1u) dsp = true; }
        }
        ultimo_ = in;
        publica();
        if (dsp && despierta) despierta();
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
        case 0x00: return out_;
        case 0x0C: return oe_;
        case 0x18: case 0x68: return status_;
        case 0x24: return lee_in();
        case 0x3FC: return 0x2203110u;
        default: break;
        }
        if (off >= 0x28 && off < 0x48) return pin_[(off - 0x28) / 4];
        if (off >= 0x48 && off < 0x68) return gpio_[(off - 0x48) / 4];
        return 0;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = mascara_be_esp(be);
        v &= m;
        const uint32_t pm = 0xFFu;
        switch (off) {
        case 0x00: out_ = ((out_ & ~m) | v) & pm; break;
        case 0x04: out_ |= v & pm; break;
        case 0x08: out_ &= ~v; break;
        case 0x0C: oe_ = ((oe_ & ~m) | v) & pm; break;
        case 0x10: oe_ |= v & pm; break;
        case 0x14: oe_ &= ~v; break;
        case 0x18: status_ = ((status_ & ~m) | v) & pm; publica(); return;
        case 0x1C: status_ |= v & pm; publica(); return;
        case 0x20: status_ &= ~v; publica(); return;
        default:
            if (off >= 0x28 && off < 0x48) { uint32_t& r = pin_[(off - 0x28) / 4]; r = (r & ~m) | v; }
            else if (off >= 0x48 && off < 0x68) { uint32_t& r = gpio_[(off - 0x48) / 4]; r = (r & ~m) | v; }
            else return;
            break;
        }
        if (cambia) cambia();
    }

private:
    uint32_t out_ = 0, oe_ = 0, status_ = 0, ultimo_ = 0, pin_[N] = {}, gpio_[N] = {};
    bool irq_ = false;
    uint32_t lee_in() const {
        uint32_t v = 0;
        for (unsigned p = 0; p < N; ++p) if (ie(p) && nivel && nivel(p)) v |= 1u << p;
        return v;
    }
    void publica() {
        const bool n = status_ != 0;
        if (n != irq_) { irq_ = n; if (irq) irq(n); }
    }
};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_C6_LP_H
