// =============================================================================
// gpio_port.h — Puerto GPIO (AHB1; plan P1) — instanciado 9 veces (A..I)
//
// Registros MODER/OTYPER/OSPEEDR/PUPDR/IDR/ODR/BSRR/LCKR/AFRL/AFRH [IR, §3.4].
// Gobierna los 16 pads del puerto (bundle PadDrive), lee las entradas
// digitalizadas hacia el IDR (muestreo síncrono con HCLK [IR, §3.3.1]), publica
// la selección de AF al pin_mux y las 16 líneas hacia EXTI (el mux de puerto lo
// hace SYSCFG_EXTICR dentro del EXTI/SYSCFG [IR, §9.4.3]).
//
// Fase F3 — implementado:
//   * banco de registros completo con los valores de reset especiales de
//     GPIOA/GPIOB (pines de depuración JTAG/SWD) [IR, §3.4.1, §3.4.3, §3.4.4];
//   * BSRR atómico con prioridad de BS sobre BR [IR, §3.4.7];
//   * máquina de estados del LCKR (secuencia 1-0-1) que congela MODER, OTYPER,
//     OSPEEDR, PUPDR y AFRL/AFRH de los pines bloqueados [IR, §3.4.8];
//   * traducción de la configuración a PadDrive y publicación de af_sel;
//   * IDR muestreado un ciclo de HCLK después del cambio en el pin, sin
//     depender de que la onda cuadrada del reloj esté generándose.
// =============================================================================
#ifndef STM32_PERIPH_GPIO_PORT_H
#define STM32_PERIPH_GPIO_PORT_H

#include "../common/periph_base.h"
#include "../pins/pad.h"
#include "../pins/af_types.h"

namespace stm32 {

class GpioPort : public BusSlave {
public:
    sc_core::sc_vector<sc_core::sc_out<PadDrive>> pad_drive;  // [16] -> pin_mux
    sc_core::sc_vector<sc_core::sc_in<bool>>      pad_din;    // [16] <- pads
    sc_core::sc_vector<sc_core::sc_out<bool>>     exti_line;  // [16] -> EXTI/SYSCFG

    // Multiplexor de AF (lo fija el top). Sin él, el puerto funciona igual pero
    // el modo AF deja el pin en alta impedancia.
    af_sel_if* mux = nullptr;

    // idx: 0=A .. 8=I. Valores de reset especiales de PA/PB (debug) [IR, §3.4.1]
    GpioPort(sc_core::sc_module_name nm, unsigned idx)
        : BusSlave(nm, addr::GPIOA_B + 0x400u * idx, 0x400),
          pad_drive("pad_drive", 16), pad_din("pad_din", 16),
          exti_line("exti_line", 16), idx_(idx) {
        SC_HAS_PROCESS(GpioPort);
        reset_state();
        SC_METHOD(update_pads);
        sensitive << ev_regs_;
        SC_METHOD(pin_in_proc);
        for (unsigned i = 0; i < 16; ++i) sensitive << pad_din[i];
        SC_METHOD(idr_sample_proc);
        sensitive << ev_idr_;
        dont_initialize();
        SC_METHOD(reset_proc);
        sensitive << rst_n;
    }

    // --- Acceso de verificación --------------------------------------------
    uint32_t idr() const { return idr_; }
    uint32_t odr() const { return odr_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case 0x00: return moder_;
            case 0x04: return otyper_;
            case 0x08: return ospeedr_;
            case 0x0C: return pupdr_;
            case 0x10: return idr_;             // solo acceso de palabra
            case 0x14: return odr_;
            case 0x18: return 0;                // BSRR es de solo escritura
            case 0x1C: return lckr_;
            case 0x20: return afrl_;
            case 0x24: return afrh_;
            default:   return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        const uint32_t m = be_mask(be);
        switch (off) {
            case 0x00: moder_   = merge_cfg2(moder_, v, m);  break;
            case 0x04: otyper_  = merge_cfg1(otyper_, v & 0xFFFFu, m); break;
            case 0x08: ospeedr_ = merge_cfg2(ospeedr_, v, m); break;
            case 0x0C: pupdr_   = merge_cfg2(pupdr_, v, m);  break;
            case 0x10: return;                               // IDR de solo lectura
            case 0x14: odr_ = (odr_ & ~(m & 0xFFFFu)) | (v & m & 0xFFFFu); break;
            case 0x18: {                                     // BSRR [IR, §3.4.7]
                const uint32_t w  = v & m;
                const uint32_t bs = w & 0xFFFFu;             // set
                const uint32_t br = (w >> 16) & 0xFFFFu;     // reset
                odr_ = (odr_ & ~br) | bs;                    // BS gana a BR
                break;
            }
            case 0x1C: write_lckr(v & m);                    break;
            case 0x20: afrl_ = merge_afr(afrl_, v, m, 0);    break;
            case 0x24: afrh_ = merge_afr(afrh_, v, m, 8);    break;
            default:   return;
        }
        ev_regs_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    unsigned idx_;
    uint32_t moder_ = 0, otyper_ = 0, ospeedr_ = 0, pupdr_ = 0;
    uint32_t idr_ = 0, odr_ = 0, lckr_ = 0, afrl_ = 0, afrh_ = 0;
    unsigned lck_state_ = 0;        // FSM de la secuencia 1-0-1
    uint32_t lck_pend_  = 0;        // LCK[15:0] de la secuencia en curso
    bool     locked_    = false;    // LCKK activo: configuración congelada
    sc_core::sc_event ev_regs_, ev_idr_;

    static uint32_t be_mask(uint32_t be) {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        return m;
    }

    // ---- Bloqueo por LCKR: los bits de configuración de un pin bloqueado no
    //      se modifican [IR, §3.4.8] ---------------------------------------
    uint32_t lock2() const {                 // máscara de 2 bits/pin bloqueada
        uint32_t r = 0;
        if (!locked_) return 0;
        for (unsigned i = 0; i < 16; ++i) if ((lckr_ >> i) & 1u) r |= 3u << (2 * i);
        return r;
    }
    uint32_t lock1() const {                 // máscara de 1 bit/pin bloqueada
        return locked_ ? (lckr_ & 0xFFFFu) : 0u;
    }
    uint32_t merge_cfg2(uint32_t cur, uint32_t v, uint32_t m) const {
        const uint32_t w = m & ~lock2();
        return (cur & ~w) | (v & w);
    }
    uint32_t merge_cfg1(uint32_t cur, uint32_t v, uint32_t m) const {
        const uint32_t w = m & 0xFFFFu & ~lock1();
        return (cur & ~w) | (v & w);
    }
    uint32_t merge_afr(uint32_t cur, uint32_t v, uint32_t m, unsigned first) const {
        uint32_t w = m;
        if (locked_)
            for (unsigned i = 0; i < 8; ++i)
                if ((lckr_ >> (first + i)) & 1u) w &= ~(0xFu << (4 * i));
        return (cur & ~w) | (v & w);
    }

    // ---- Secuencia de bloqueo LCKK: WR1 / WR0 / WR1 [IR, §3.4.8] ----------
    void write_lckr(uint32_t v) {
        if (locked_) return;                        // ya bloqueado hasta el reset
        const bool     k    = (v >> 16) & 1u;
        const uint32_t lck  = v & 0xFFFFu;
        switch (lck_state_) {
            case 0: if (k) { lck_pend_ = lck; lck_state_ = 1; } break;
            case 1: if (!k && lck == lck_pend_) lck_state_ = 2; else lck_state_ = 0; break;
            case 2:
                if (k && lck == lck_pend_) {        // secuencia correcta
                    lckr_ = lck_pend_ | (1u << 16);
                    locked_ = true;
                }
                lck_state_ = 0;
                break;
            default: lck_state_ = 0; break;
        }
        if (!locked_) lckr_ = (lckr_ & 0x10000u) | lck;   // LCK visible antes del bloqueo
    }

    // ---- Traducción de la configuración a los pads ------------------------
    void update_pads() {
        for (unsigned i = 0; i < 16; ++i) {
            const unsigned mode = (moder_ >> (2 * i)) & 3u;
            PadDrive d;
            d.analog = (mode == 3);
            d.oe     = (mode == 1);                       // salida GPIO
            d.out    = (odr_ >> i) & 1u;
            d.od     = (otyper_ >> i) & 1u;
            d.pupd   = uint8_t((pupdr_ >> (2 * i)) & 3u);
            d.speed  = uint8_t((ospeedr_ >> (2 * i)) & 3u);
            if (d.analog) { d.pupd = 0; d.od = false; }    // [IR, §3.3.4]
            pad_drive[i].write(d);
            if (mux) mux->set_af(idx_, i, mode == 2 ? af_of(i) : AF_NONE);
        }
    }
    uint8_t af_of(unsigned i) const {
        return uint8_t(i < 8 ? (afrl_ >> (4 * i)) & 0xFu
                             : (afrh_ >> (4 * (i - 8))) & 0xFu);
    }

    // ---- Entradas: EXTI es asíncrono; el IDR se muestrea con HCLK ---------
    void pin_in_proc() {
        for (unsigned i = 0; i < 16; ++i) exti_line[i].write(pad_din[i].read());
        // Un ciclo de HCLK de latencia hasta el IDR [IR, §3.3.1]. Se calcula con
        // la frecuencia del dominio en lugar de esperar un flanco, para no
        // obligar a generar la onda cuadrada de HCLK (véase clock_gen.h).
        const double hz = domain_hz();
        ev_idr_.notify(hz > 0.0 ? sc_core::sc_time(1.0e12 / hz, sc_core::SC_PS)
                                : sc_core::SC_ZERO_TIME);
    }
    void idr_sample_proc() {
        uint32_t v = 0;
        for (unsigned i = 0; i < 16; ++i) if (pad_din[i].read()) v |= 1u << i;
        idr_ = v;
    }

    void reset_proc() { if (!rst_n.read()) { reset_state(); ev_regs_.notify(sc_core::SC_ZERO_TIME); } }

    // Valores de reset [IR, §3.4.1, §3.4.3, §3.4.4]
    void reset_state() {
        moder_   = (idx_ == 0) ? 0xA8000000u : (idx_ == 1) ? 0x00000280u : 0u;
        ospeedr_ = (idx_ == 0) ? 0x0C000000u : (idx_ == 1) ? 0x000000C0u : 0u;
        pupdr_   = (idx_ == 0) ? 0x64000000u : (idx_ == 1) ? 0x00000100u : 0u;
        otyper_ = 0; odr_ = 0; lckr_ = 0; afrl_ = 0; afrh_ = 0;
        lck_state_ = 0; lck_pend_ = 0; locked_ = false;
        idr_ = 0;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_GPIO_PORT_H
