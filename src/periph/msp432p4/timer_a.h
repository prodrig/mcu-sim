// =============================================================================
// timer_a.h — El Timer_A del MSP432P4 (fase T3)
//
// El temporizador de 16 bits del MSP430, cuatro veces (TA0..TA3), cada uno
// con cinco registros de captura/comparación [H: Timer_A_Type; TRM SLAU356,
// cap. 19; segunda fuente: DriverLib timer_a.c y Tock timer.rs]:
//
//   CTL     TASSEL (TAxCLK, ACLK, SMCLK, INCLK), ID (/1../8), MC (parado,
//           ascendente hasta CCR0, continuo hasta 0xFFFF, ascendente/
//           descendente), TACLR, TAIE, TAIFG
//   EX0     TAIDEX: un segundo divisor /1../8
//   R       TAR, el contador
//   CCTLn   CAP, CM (flanco), CCIS (CCInA, CCInB, GND, VCC), SCS, SCCI,
//           OUTMOD (los ocho modos de salida), CCIE, CCI, OUT, COV, CCIFG
//   CCRn    el valor de comparación, o lo capturado
//   IV      CCR1..CCR4 y TAIFG, en ese orden; leerlo borra el que dice
//
// DOS INTERRUPCIONES por temporizador, como en el chip: TAx_0, solo de CCR0, y
// TAx_N, de los demás y de TAIFG. En el MSP432 el CCIFG de CCR0 NO se borra
// solo al atender la interrupción (en el MSP430 sí): lo borra el firmware,
// que es lo que hacen los ejemplos de DriverLib. [1F; P-16 r]
//
// CÓMO CUENTA: sin simular cada tic. El estado (TAR, sentido) se ancla en un
// instante y en un tic; los tics siguientes caen en t_ref + k / f, y el hilo
// solo se despierta en los que pasa algo -una comparación, el paso por cero,
// el techo-. Leer TAR calcula dónde va. Con TASSEL = TAxCLK, en cambio, cada
// flanco de subida del pin es un tic de la fuente, y se cuenta uno a uno.
//
// LAS SALIDAS cambian en el tic en que TAR LLEGA al valor: el modo 7
// (reset/set) en modo ascendente pone la salida a 1 cuando TAR llega a CCR0 y
// a 0 cuando llega a CCRn, y por eso con CCRn = 0 queda un pulso de un tic,
// que es la rareza conocida del silicio. [TRM, «Output Example—Timer in Up
// Mode»; P-16 r]
//
// LO QUE NO ESTÁ: las entradas CCInB internas (ACLK, las salidas de los
// comparadores...) y INCLK no están conectadas: leen 0. [P-16 r]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_TIMER_A_H
#define STM32_PERIPH_MSP432P4_TIMER_A_H

#include <cmath>
#include <functional>
#include "bus16.h"
#include "dio.h"

namespace stm32 {

class TimerAP4 : public Bus16P4 {
public:
    static constexpr unsigned NCC = 5;
    sc_core::sc_out<bool> irq0{"irq0"}, irqn{"irqn"};
    // Las salidas OUTn y las entradas CCInA, para los pines; y TAxCLK
    sc_core::sc_signal<bool> out[NCC], cci_a[NCC], cci_b[NCC], taclk{"taclk"};

    // La frecuencia de la fuente: 1 ACLK, 2 SMCLK (0 y 3 no son de reloj interno)
    std::function<double(unsigned)> f_fuente;

    enum : uint16_t {
        IFG = 0x0001, IE = 0x0002, CLR = 0x0004,
        CC_IFG = 0x0001, CC_COV = 0x0002, CC_OUT = 0x0004, CC_CCI = 0x0008,
        CC_IE = 0x0010, CC_CAP = 0x0100, CC_SCCI = 0x0400, CC_SCS = 0x0800
    };

    TimerAP4(sc_core::sc_module_name nm, uint32_t base) : Bus16P4(nm, base, 0x400) {
        SC_HAS_PROCESS(TimerAP4);
        SC_THREAD(cuenta_proc);
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        SC_METHOD(captura_proc);
        for (unsigned n = 0; n < NCC; ++n) sensitive << cci_a[n] << cci_b[n];
        dont_initialize();
        SC_METHOD(taclk_proc); sensitive << taclk.posedge_event(); dont_initialize();
    }

    // Las funciones de pin: TAx.n (PxDIR decide: salida OUTn o entrada CCInA)
    FuncionP4 fn_ta(unsigned n) {
        FuncionP4 f; f.out = &out[n]; f.oe = nullptr; f.in = &cci_a[n];
        f.idle_in = false; f.nombre = "TA";
        return f;
    }
    FuncionP4 fn_taclk() {
        FuncionP4 f; f.in = &taclk; f.idle_in = false; f.nombre = "TACLK";
        return f;
    }
    // Lo que el SoC llama al cambiar un reloj
    void recalcula() { al_dia(); ancla_frecuencia(); ev_.notify(sc_core::SC_ZERO_TIME); }

    // --- Lo que mira el banco (y el ADC14, que se dispara con OUT1/OUT2) ----
    uint16_t tar()  { al_dia(); return tar_; }
    bool     salida(unsigned n) const { return o_out_[n]; }
    uint16_t ccr(unsigned n) const { return ccr_[n]; }
    double   f_tic() const { return f_; }
    uint64_t comparaciones() const { return n_cmp_; }
    uint64_t capturas() const { return n_cap_; }
    // Un cambio de una salida (para quien se dispare con ella)
    std::function<void(unsigned n, bool v)> al_cambiar_salida;

protected:
    uint16_t ctl_ = 0, ex0_ = 0, cctl_[NCC] = {}, ccr_[NCC] = {};
    // El estado anclado: TAR y sentido en el tic `t_ref_`
    uint16_t tar_ = 0;
    bool     baja_ = false;                      // up/down: contando hacia abajo
    sc_core::sc_time t_ref_{sc_core::SC_ZERO_TIME};
    double   f_ = 0.0;                           // tics por segundo (0: parado)
    unsigned div_ext_ = 0;                       // TAxCLK: flancos dentro del divisor
    bool     o_out_[NCC] = {}, o_irq0_ = false, o_irqn_ = false, repulso_ = false;
    sc_core::sc_event ev_, pub_ev_;
    uint64_t n_cmp_ = 0, n_cap_ = 0;

    unsigned mc() const { return (ctl_ >> 4) & 3u; }
    unsigned tassel() const { return (ctl_ >> 8) & 3u; }
    unsigned divisor() const { return (1u << ((ctl_ >> 6) & 3u)) * ((ex0_ & 7u) + 1u); }
    unsigned outmod(unsigned n) const { return (cctl_[n] >> 5) & 7u; }
    bool cap(unsigned n) const { return (cctl_[n] & CC_CAP) != 0; }
    // ¿Cuenta? Con MC = 1 o 3 y CCR0 = 0, el temporizador se para
    bool cuenta() const {
        if (mc() == 0) return false;
        if ((mc() == 1 || mc() == 3) && ccr_[0] == 0) return false;
        return true;
    }
    bool interno() const { return tassel() == 1 || tassel() == 2; }

    void reset_proc() {
        if (rst_n.read()) return;
        ctl_ = 0; ex0_ = 0;
        for (unsigned n = 0; n < NCC; ++n) { cctl_[n] = 0; ccr_[n] = 0; o_out_[n] = false; }
        tar_ = 0; baja_ = false; div_ext_ = 0;
        t_ref_ = sc_core::sc_time_stamp(); f_ = 0.0;
        actualiza_irq();
        ev_.notify(sc_core::SC_ZERO_TIME);
    }

    // --- El reloj ---------------------------------------------------------------
    sc_core::sc_time periodo() const { return sc_core::sc_time(1e12 / f_, sc_core::SC_PS); }
    sc_core::sc_time t_tic(uint64_t k) const {
        return t_ref_ + sc_core::sc_time(double(k) * 1e12 / f_, sc_core::SC_PS);
    }
    // Con la configuración nueva, el ritmo nuevo desde ahora
    // (con el mismo ritmo, el ancla no se toca: la fase de los tics se mantiene)
    void ancla_frecuencia() {
        const double f_src = (interno() && f_fuente) ? f_fuente(tassel()) : 0.0;
        const double f = (cuenta() && interno() && f_src > 0.0) ? f_src / divisor() : 0.0;
        if (f != f_ || f_ <= 0.0) t_ref_ = sc_core::sc_time_stamp();
        f_ = f;
    }
    // Los tics enteros que han pasado desde el ancla
    uint64_t tics_hasta(const sc_core::sc_time& t) const {
        if (f_ <= 0.0 || t <= t_ref_) return 0;
        const double x = (t - t_ref_).to_seconds() * f_;
        return uint64_t(std::floor(x + 1e-6));
    }

    // --- El recorrido de TAR ------------------------------------------------------
    // Cuántos tics faltan para que TAR LLEGUE a `v` desde el estado anclado
    // (0 si nunca, en este modo)
    uint64_t distancia(uint16_t v) const {
        const unsigned c = ccr_[0];
        switch (mc()) {
            case 1: {                                   // ascendente: 0..c
                if (v > c) return 0;
                if (tar_ > c) return uint64_t(v) + 1u;  // CCR0 por debajo: vuelve a 0
                const uint64_t p = c + 1u;
                return ((uint64_t(v) + p - tar_ - 1u) % p) + 1u;
            }
            case 2: {                                   // continuo
                const uint64_t d = (uint64_t(v) + 65536u - tar_) % 65536u;
                return d ? d : 65536u;
            }
            case 3: {                                   // ascendente/descendente
                if (v > c) return 0;
                const uint64_t P = 2u * c;
                const uint64_t fase = baja_ ? (P - tar_) % P : tar_;
                uint64_t mejor = 0;
                const uint64_t q1 = v, q2 = (P - v) % P;
                for (uint64_t q : {q1, q2}) {
                    const uint64_t d = ((q + P - fase - 1u) % P) + 1u;
                    if (!mejor || d < mejor) mejor = d;
                }
                return mejor;
            }
            default: return 0;
        }
    }
    // Avanza `k` tics sin mirar nada
    void salta(uint64_t k) {
        const unsigned c = ccr_[0];
        switch (mc()) {
            case 1:
                if (tar_ > c) { if (k == 0) return; tar_ = 0; --k; }
                tar_ = uint16_t((uint64_t(tar_) + k) % (c + 1u));
                break;
            case 2:
                tar_ = uint16_t((uint64_t(tar_) + k) % 65536u);
                break;
            case 3: {
                const uint64_t P = 2u * c;
                uint64_t fase = baja_ ? (P - tar_) % P : tar_;
                fase = (fase + k) % P;
                if (fase <= c) { tar_ = uint16_t(fase); baja_ = fase == c; }
                else { tar_ = uint16_t(P - fase); baja_ = true; }
                if (tar_ == 0) baja_ = false;
                if (tar_ == c) baja_ = true;
                break;
            }
            default: break;
        }
    }
    // Los tics hasta el próximo valor que importa
    uint64_t proximo() const {
        uint64_t m = distancia(0);
        auto mira = [&](uint64_t d) { if (d && (!m || d < m)) m = d; };
        mira(distancia(ccr_[0]));
        for (unsigned n = 1; n < NCC; ++n) if (!cap(n)) mira(distancia(ccr_[n]));
        if (mc() == 2) mira(distancia(0xFFFF));
        return m;
    }

    // Lo que pasa en el tic en que TAR llega a su valor actual, venido de `antes`
    void en_tic(uint16_t antes) {
        const unsigned c = ccr_[0];
        bool cambio = false;
        // TAIFG: el paso por cero
        bool taifg = false;
        if (tar_ == 0) {
            if (mc() == 1 && antes != 0) taifg = true;
            if (mc() == 2 && antes == 0xFFFF) taifg = true;
            if (mc() == 3 && antes == 1) taifg = true;
        }
        if (mc() == 3 && c == 0) taifg = false;
        // EQU0 y EQUn
        const bool equ0 = tar_ == c;
        bool equ[NCC] = {};
        for (unsigned n = 0; n < NCC; ++n) equ[n] = !cap(n) && tar_ == ccr_[n];
        for (unsigned n = 0; n < NCC; ++n) {
            if (!equ[n]) continue;
            ++n_cmp_;
            cctl_[n] |= CC_IFG;
            // SCCI: la entrada, retenida en la comparación
            if (entrada(n)) cctl_[n] |= CC_SCCI; else cctl_[n] &= uint16_t(~CC_SCCI);
            cambio = true;
        }
        for (unsigned n = 0; n < NCC; ++n) {
            const unsigned m = outmod(n);
            bool o = o_out_[n];
            if (m == 0) continue;
            if (equ[n]) {
                switch (m) {
                    case 1: case 3: o = true; break;
                    case 2: case 4: case 6: o = !o; break;
                    case 5: case 7: o = false; break;
                }
            }
            if (equ0 && n != 0) {
                switch (m) {
                    case 2: case 3: o = false; break;
                    case 6: case 7: o = true; break;
                    default: break;
                }
            }
            pon_salida(n, o);
        }
        if (taifg) { ctl_ |= IFG; cambio = true; }
        if (cambio) actualiza_irq();
    }
    void pon_salida(unsigned n, bool o) {
        if (o == o_out_[n]) return;
        o_out_[n] = o;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        if (al_cambiar_salida) al_cambiar_salida(n, o);
    }

    // Procesa todos los tics hasta ahora: los que tienen algo, uno a uno
    void al_dia() {
        if (f_ <= 0.0 || !cuenta()) return;
        const sc_core::sc_time ahora = sc_core::sc_time_stamp();
        uint64_t n = tics_hasta(ahora);
        while (n > 0) {
            const uint64_t d = proximo();
            if (!d || d > n) { salta(n); t_ref_ = t_tic(n); break; }
            salta(d - 1);
            const uint16_t prev = tar_;
            salta(1);
            t_ref_ = t_tic(d);
            n -= d;
            en_tic(prev);
        }
    }

    void cuenta_proc() {
        for (;;) {
            al_dia();
            if (f_ <= 0.0 || !cuenta()) { wait(ev_); continue; }
            const uint64_t d = proximo();
            if (!d) { wait(ev_); continue; }
            const sc_core::sc_time t = t_tic(d);
            const sc_core::sc_time ahora = sc_core::sc_time_stamp();
            if (t > ahora) wait(t - ahora, ev_);
        }
    }

    // --- TAxCLK: cada flanco de subida es un tic de la fuente -----------------------
    void taclk_proc() {
        if (tassel() != 0 || !cuenta()) return;
        if (++div_ext_ < divisor()) return;
        div_ext_ = 0;
        const uint16_t antes = tar_;
        salta(1);
        en_tic(antes);
    }

    // --- Captura -------------------------------------------------------------------
    bool entrada(unsigned n) const {
        switch ((cctl_[n] >> 12) & 3u) {
            case 0: return cci_a[n].read();
            case 1: return cci_b[n].read();
            case 2: return false;
            default: return true;
        }
    }
    bool ant_[NCC] = {};
    void captura_proc() {
        for (unsigned n = 0; n < NCC; ++n) {
            const bool v = entrada(n);
            if (v != ant_[n]) flanco(n, v);
            ant_[n] = v;
        }
    }
    void flanco(unsigned n, bool sube) {
        if (!cap(n)) return;
        const unsigned cm = (cctl_[n] >> 14) & 3u;
        const bool vale = (cm == 1 && sube) || (cm == 2 && !sube) || cm == 3;
        if (!vale) return;
        al_dia();
        ++n_cap_;
        if (cctl_[n] & CC_IFG) cctl_[n] |= CC_COV;
        ccr_[n] = tar_;
        cctl_[n] |= CC_IFG;
        actualiza_irq();
    }

    // --- Interrupciones ------------------------------------------------------------
    void actualiza_irq() {
        o_irq0_ = (cctl_[0] & CC_IFG) && (cctl_[0] & CC_IE);
        bool nn = (ctl_ & IFG) && (ctl_ & IE);
        for (unsigned n = 1; n < NCC; ++n) if ((cctl_[n] & CC_IFG) && (cctl_[n] & CC_IE)) nn = true;
        o_irqn_ = nn;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void borrado() { repulso_ = true; actualiza_irq(); }
    void pub_proc() {
        for (unsigned n = 0; n < NCC; ++n) out[n].write(o_out_[n]);
        if (repulso_) {
            repulso_ = false;
            bool otra = false;
            if (o_irq0_ && irq0.read()) { irq0.write(false); otra = true; } else irq0.write(o_irq0_);
            if (o_irqn_ && irqn.read()) { irqn.write(false); otra = true; } else irqn.write(o_irqn_);
            if (otra) pub_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        irq0.write(o_irq0_);
        irqn.write(o_irqn_);
    }
    uint16_t iv(bool dbg) {
        for (unsigned n = 1; n < NCC; ++n)
            if ((cctl_[n] & CC_IFG) && (cctl_[n] & CC_IE)) {
                if (!dbg) { cctl_[n] &= uint16_t(~CC_IFG); borrado(); }
                return uint16_t(2u * n);
            }
        if ((ctl_ & IFG) && (ctl_ & IE)) {
            if (!dbg) { ctl_ &= uint16_t(~IFG); borrado(); }
            return 0x0E;
        }
        return 0;
    }

    // --- Registros -----------------------------------------------------------------
    uint16_t lee16(uint32_t off, bool dbg) override {
        if (off == 0x00) return ctl_;
        if (off >= 0x02 && off <= 0x0A) {
            const unsigned n = (off - 0x02) / 2;
            uint16_t v = uint16_t(cctl_[n] & ~CC_CCI);
            if (entrada(n)) v |= CC_CCI;
            return v;
        }
        if (off == 0x10) { al_dia(); return tar_; }
        if (off >= 0x12 && off <= 0x1A) return ccr_[(off - 0x12) / 2];
        if (off == 0x20) return ex0_;
        if (off == 0x2E) return iv(dbg);
        return 0;
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        al_dia();
        if (off == 0x00) {
            const uint16_t nuevo = mz(ctl_);
            ctl_ = uint16_t(nuevo & 0x03F3u);            // TACLR no se guarda
            if (nuevo & CLR) { tar_ = 0; baja_ = false; div_ext_ = 0; }
            // TAR cuando se arranca en up/down por debajo: sube
            ancla_frecuencia();
            actualiza_irq();
        } else if (off >= 0x02 && off <= 0x0A) {
            const unsigned n = (off - 0x02) / 2;
            const uint16_t antes = cctl_[n];
            cctl_[n] = uint16_t(mz(cctl_[n]) & ~CC_CCI);
            // CCIS entre GND y VCC: un flanco por programa (captura por software)
            const bool e0 = ((antes >> 12) & 3u) >= 2, e1 = ((cctl_[n] >> 12) & 3u) >= 2;
            if (e0 && e1 && (((antes >> 12) & 3u) != ((cctl_[n] >> 12) & 3u))) {
                const bool v1 = ((cctl_[n] >> 12) & 3u) == 3u;
                flanco(n, v1);
            }
            ant_[n] = entrada(n);
            if (outmod(n) == 0) pon_salida(n, (cctl_[n] & CC_OUT) != 0);
            actualiza_irq();
        } else if (off == 0x10) {
            tar_ = mz(tar_);
            t_ref_ = sc_core::sc_time_stamp();
        } else if (off >= 0x12 && off <= 0x1A) {
            ccr_[(off - 0x12) / 2] = mz(ccr_[(off - 0x12) / 2]);
            if (off == 0x12) ancla_frecuencia();     // CCR0 = 0 para o arranca
        } else if (off == 0x20) {
            ex0_ = uint16_t(mz(ex0_) & 7u);
            ancla_frecuencia();
        } else {
            return;
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_TIMER_A_H
