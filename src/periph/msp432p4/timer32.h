// =============================================================================
// timer32.h — El Timer32 del MSP432P4 (fase T3)
//
// Dos contadores descendentes de 32 bits (o 16), de ARM: el «Dual-Input
// Timer» SP804, con el reloj de MCLK [H: Timer32_Type, TIMER32_1 en
// 0x4000_C000 y TIMER32_2 en +0x20; ARM DDI 0271 para el comportamiento;
// segunda fuente, DriverLib timer32.c]:
//
//   LOAD     escribirlo carga el contador YA, y es el valor de recarga
//   BGLOAD   el valor de recarga, sin tocar la cuenta en curso
//   VALUE    la cuenta
//   CONTROL  ONESHOT, SIZE (16/32), PRESCALE (/1, /16, /256), IE, MODE
//            (0: libre, vuelve a 0xFFFFFFFF; 1: periódico, recarga LOAD),
//            ENABLE
//   INTCLR   escribir lo que sea borra la interrupción
//   RIS/MIS  la bandera, cruda y con IE
//
// Al llegar a cero levanta la bandera; en periódico recarga en el tic
// siguiente (el periodo son LOAD + 1 tics), en libre sigue desde el máximo y
// en un disparo se queda en cero. CONTROL sale del reset con IE a 1 (0x20),
// como el SP804. [ARM DDI 0271; P-16 r]
//
// Tres interrupciones: T32_INT1, T32_INT2 y T32_INTC, que es la O de las dos.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_TIMER32_H
#define STM32_PERIPH_MSP432P4_TIMER32_H

#include <cmath>
#include <functional>
#include "../../common/periph_base.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class Timer32P4 : public BusSlave {
public:
    sc_core::sc_out<bool> irq1{"irq1"}, irq2{"irq2"}, irqc{"irqc"};
    std::function<double()> f_mclk;

    enum : uint32_t { ONESHOT = 0x01, SIZE = 0x02, IE = 0x20, MODE = 0x40, ENABLE = 0x80 };

    explicit Timer32P4(sc_core::sc_module_name nm) : BusSlave(nm, p4::TIMER32_B, 0x400) {
        SC_HAS_PROCESS(Timer32P4);
        SC_THREAD(cuenta_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    void recalcula() { for (T& t : t_) { al_dia(t); ancla(t); } ev_.notify(sc_core::SC_ZERO_TIME); }
    uint32_t valor(unsigned i) { al_dia(t_[i]); return t_[i].cuenta; }
    uint64_t vencimientos(unsigned i) const { return t_[i].n_venc; }

protected:
    struct T {
        uint32_t load = 0, bgload = 0, control = IE, cuenta = 0xFFFFFFFFu;
        bool ris = false, parado_en_cero = false;
        sc_core::sc_time t_ref{sc_core::SC_ZERO_TIME};
        double f = 0.0;
        uint64_t n_venc = 0;
    } t_[2];
    bool o_[3] = {}, repulso_ = false;
    sc_core::sc_event ev_, pub_ev_;

    static uint32_t tope(const T& t) { return (t.control & SIZE) ? 0xFFFFFFFFu : 0xFFFFu; }
    static unsigned pre(const T& t) {
        switch ((t.control >> 2) & 3u) { case 1: return 16; case 2: return 256; default: return 1; }
    }
    bool corre(const T& t) const { return (t.control & ENABLE) && !t.parado_en_cero && t.f > 0.0; }
    void ancla(T& t) {
        const double f = (f_mclk ? f_mclk() : 0.0) / pre(t);
        if (f != t.f || t.f <= 0.0) t.t_ref = sc_core::sc_time_stamp();
        t.f = f;
    }
    uint64_t tics(const T& t) const {
        if (!corre(t)) return 0;
        const sc_core::sc_time a = sc_core::sc_time_stamp();
        if (a <= t.t_ref) return 0;
        return uint64_t(std::floor((a - t.t_ref).to_seconds() * t.f + 1e-6));
    }
    sc_core::sc_time t_tic(const T& t, uint64_t k) const {
        return t.t_ref + sc_core::sc_time(double(k) * 1e12 / t.f, sc_core::SC_PS);
    }
    // Avanza la cuenta hasta ahora, venciendo las veces que toque
    void al_dia(T& t) {
        uint64_t n = tics(t);
        while (n > 0 && corre(t)) {
            const uint64_t a_cero = t.cuenta;          // tics hasta llegar a 0
            if (a_cero == 0) {
                // En cero: el siguiente tic recarga (o da la vuelta)
                t.cuenta = (t.control & MODE) ? (t.load & tope(t)) : tope(t);
                t.t_ref = t_tic(t, 1);
                --n;
                continue;
            }
            if (n < a_cero) { t.cuenta = uint32_t(t.cuenta - n); t.t_ref = t_tic(t, n); break; }
            t.cuenta = 0;
            t.t_ref = t_tic(t, a_cero);
            n -= a_cero;
            vence(t);
        }
    }
    void vence(T& t) {
        ++t.n_venc;
        t.ris = true;
        if (t.control & ONESHOT) t.parado_en_cero = true;
        actualiza_irq();
    }
    void cuenta_proc() {
        for (;;) {
            sc_core::sc_time prox = sc_core::SC_ZERO_TIME;
            bool hay = false;
            for (T& t : t_) {
                al_dia(t);
                if (!corre(t)) continue;
                const uint64_t d = t.cuenta ? t.cuenta : uint64_t((t.control & MODE) ?
                                       (t.load & tope(t)) : tope(t)) + 1u;
                const sc_core::sc_time e = t_tic(t, d);
                if (!hay || e < prox) { prox = e; hay = true; }
            }
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (!hay) { wait(ev_); continue; }
            if (prox > ahora) wait(prox - ahora, ev_);
            else wait(sc_core::SC_ZERO_TIME);
        }
    }
    void reset_proc() {
        if (rst_n.read()) return;
        for (T& t : t_) t = T();
        actualiza_irq();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void actualiza_irq() {
        o_[0] = t_[0].ris && (t_[0].control & IE);
        o_[1] = t_[1].ris && (t_[1].control & IE);
        o_[2] = o_[0] || o_[1];
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() {
        if (repulso_) {
            repulso_ = false;
            bool otra = false;
            sc_core::sc_out<bool>* l[3] = {&irq1, &irq2, &irqc};
            for (unsigned i = 0; i < 3; ++i) {
                if (o_[i] && l[i]->read()) { l[i]->write(false); otra = true; }
                else l[i]->write(o_[i]);
            }
            if (otra) pub_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        irq1.write(o_[0]); irq2.write(o_[1]); irqc.write(o_[2]);
    }

    uint32_t reg_read(uint32_t off) override {
        if (off >= 0x40) return 0;
        T& t = t_[off >= 0x20 ? 1 : 0];
        switch (off & 0x1Fu) {
            case 0x00: return t.load;
            case 0x04: al_dia(t); return t.cuenta;
            case 0x08: return t.control;
            case 0x10: return t.ris ? 1u : 0u;
            case 0x14: return (t.ris && (t.control & IE)) ? 1u : 0u;
            case 0x18: return t.bgload;
            default:   return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (off >= 0x40) return;
        T& t = t_[off >= 0x20 ? 1 : 0];
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        al_dia(t);
        switch (off & 0x1Fu) {
            case 0x00:                                  // LOAD: carga ya
                // El contador recibe LOS 32 BITS, también con SIZE = 0: el
                // tamaño se aplica al contar (y al escribir CONTROL con SIZE =
                // 0). Truncar aquí hacía que LOAD escrito con CONTROL = 0 y
                // luego SIZE = 1 -lo que hace un retardo con el temporizador
                // parado- contara solo los 16 bits bajos. [ARM DDI 0271, Load]
                t.load = (t.load & ~m) | (v & m);
                t.bgload = t.load;
                t.cuenta = (t.control & ENABLE) ? (t.load & tope(t)) : t.load;
                t.parado_en_cero = false;
                t.t_ref = sc_core::sc_time_stamp();
                break;
            case 0x08: {
                const uint32_t antes = t.control;
                t.control = ((t.control & ~m) | (v & m)) & 0xEFu;
                if (!(antes & ENABLE) && (t.control & ENABLE)) {
                    t.parado_en_cero = false;
                    t.t_ref = sc_core::sc_time_stamp();
                }
                if (!(t.control & SIZE)) t.cuenta &= 0xFFFFu;
                ancla(t);
                actualiza_irq();
                break;
            }
            case 0x0C:                                  // INTCLR
                t.ris = false;
                repulso_ = true;
                actualiza_irq();
                break;
            case 0x18:                                  // BGLOAD: solo la recarga
                t.bgload = (t.bgload & ~m) | (v & m);
                t.load = t.bgload;
                break;
            default: return;
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_TIMER32_H
