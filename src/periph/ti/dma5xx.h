// =============================================================================
// dma5xx.h — El controlador DMA de los MSP430 5xx/6xx (DMAX_3, tres
// canales) [UG5 SLAU208Q cap. 11] (M4 de doc/analisis_msp430.md)
//
//   +00 DMACTL0 (TSEL de 0 y 1)  +02 DMACTL1 (TSEL de 2)  +08 DMACTL4
//   +0E DMAIV
//   +10·(n+1) DMAnCTL  +2/+4 DMAnSA  +6/+8 DMAnDA  +A DMAnSZ
//
// DMAnCTL: DMAREQ (el disparo por software), DMAABORT, DMAIE, DMAIFG, DMAEN,
// DMALEVEL, DMASRCBYTE, DMADSTBYTE, DMASRCINCR, DMADSTINCR y DMADT (los ocho
// modos: único, bloque, ráfaga, y los tres repetidos).
//
// Los disparos (`dispara(tsel)`) los da el SoC por flanco: su tabla de
// TSEL es del chip [DS «DMA Trigger Assignments»]. El 0 es DMAREQ y el 30, el
// fin del canal anterior (DMA(n-1)IFG), que se resuelven aquí. Cada
// transferencia pasa por el bus del chip (`lee`/`escribe`, con efectos: leer
// RXBUF borra RXIFG, como en el chip) y cuesta dos ciclos de MCLK; con un
// bloque, la CPU espera [§11.2.6] — aquí, el canal no suelta el bus hasta
// acabar el bloque, y el SoC para la CPU mientras (`ocupado`).
//
// Prioridad: el canal 0 primero, salvo ROUNDROBIN. ENNMI y DMARMWDIS se
// guardan.
// =============================================================================
#ifndef STM32_PERIPH_TI_DMA5XX_H
#define STM32_PERIPH_TI_DMA5XX_H

#include <systemc>
#include <functional>
#include <deque>

namespace stm32 {

SC_MODULE(Dma5xx) {
    static constexpr unsigned NCH = 3;
    std::function<uint16_t(uint32_t a, bool byte)> lee;
    std::function<void(uint32_t a, uint16_t v, bool byte)> escribe;
    std::function<double()> f_mclk;
    std::function<void()> al_cambiar;            // DMAIFG: que el SoC arbitre
    std::function<void(bool)> ocupado;           // la CPU espera mientras

    SC_CTOR(Dma5xx) { SC_THREAD(proc); reset(); }
    void reset() {
        for (auto& c : ch_) c = Canal();
        ctl0_ = ctl1_ = ctl4_ = 0;
        cola_.clear();
    }

    uint16_t lee16(uint32_t off, bool dbg) {
        switch (off) {
        case 0x00: return ctl0_;
        case 0x02: return ctl1_;
        case 0x08: return ctl4_;
        case 0x0E: return iv(dbg);
        default: break;
        }
        if (off < 0x10 || off >= 0x10 + 0x10 * NCH) return 0;
        const Canal& c = ch_[(off - 0x10) / 0x10];
        switch (off & 0x0F) {
        case 0x0: return c.ctl;
        case 0x2: return uint16_t(c.sa);
        case 0x4: return uint16_t(c.sa >> 16);
        case 0x6: return uint16_t(c.da);
        case 0x8: return uint16_t(c.da >> 16);
        case 0xA: return c.sz;
        default: return 0;
        }
    }
    void escribe16(uint32_t off, uint16_t v) {
        switch (off) {
        case 0x00: ctl0_ = uint16_t(v & 0x1F1F); return;
        case 0x02: ctl1_ = uint16_t(v & 0x001F); return;
        case 0x08: ctl4_ = uint16_t(v & 0x0007); return;
        default: break;
        }
        if (off < 0x10 || off >= 0x10 + 0x10 * NCH) return;
        const unsigned n = (off - 0x10) / 0x10;
        Canal& c = ch_[n];
        switch (off & 0x0F) {
        case 0x0: {
            const bool en_antes = (c.ctl & EN) != 0;
            c.ctl = uint16_t(v & ~REQ);
            if (v & ABORT) c.ctl = uint16_t(c.ctl & ~ABORT);
            if ((c.ctl & EN) && !en_antes) arma(c);              // al habilitar, se cargan los contadores
            if ((v & REQ) && (c.ctl & EN) && tsel(n) == 0) pide(n);
            if (al_cambiar) al_cambiar();
            return;
        }
        case 0x2: c.sa = (c.sa & 0xF0000u) | v; return;
        case 0x4: c.sa = (c.sa & 0x0FFFFu) | (uint32_t(v & 0xF) << 16); return;
        case 0x6: c.da = (c.da & 0xF0000u) | v; return;
        case 0x8: c.da = (c.da & 0x0FFFFu) | (uint32_t(v & 0xF) << 16); return;
        case 0xA: c.sz = v; c.sz_t = v; return;
        default: return;
        }
    }
    // Un disparo (por flanco) de la fuente `t` de la tabla del chip
    void dispara(unsigned t) {
        for (unsigned n = 0; n < NCH; ++n)
            if ((ch_[n].ctl & EN) && tsel(n) == t && t != 0) pide(n);
    }
    bool pide_irq() const {
        for (const Canal& c : ch_) if ((c.ctl & IFG) && (c.ctl & IE)) return true;
        return false;
    }
    uint64_t transferencias() const { return n_tr_; }

private:
    enum : uint16_t { REQ = 0x0001, ABORT = 0x0002, IE = 0x0004, IFG = 0x0008, EN = 0x0010,
                      LEVEL = 0x0020, SRCBYTE = 0x0040, DSTBYTE = 0x0080 };
    struct Canal {
        uint16_t ctl = 0, sz = 0, sz_t = 0;
        uint32_t sa = 0, da = 0, sa_t = 0, da_t = 0;
    };
    Canal ch_[NCH];
    uint16_t ctl0_ = 0, ctl1_ = 0, ctl4_ = 0;
    std::deque<unsigned> cola_;
    sc_core::sc_event ev_;
    uint64_t n_tr_ = 0;

    unsigned tsel(unsigned n) const {
        return n == 0 ? (ctl0_ & 0x1F) : n == 1 ? ((ctl0_ >> 8) & 0x1F) : (ctl1_ & 0x1F);
    }
    unsigned dt(const Canal& c) const { return (c.ctl >> 12) & 7u; }
    bool hay_bloque_en_cola() const {
        for (unsigned k : cola_) if ((ch_[k].ctl & EN) && (dt(ch_[k]) & 3u)) return true;
        return false;
    }
    void arma(Canal& c) { c.sa_t = c.sa; c.da_t = c.da; c.sz_t = c.sz; }
    // Un bloque para a la CPU desde el disparo: la instrucción que lo pide es
    // la última antes del bloque [UG5 §11.2.6]
    void pide(unsigned n) {
        cola_.push_back(n);
        if ((dt(ch_[n]) & 3u) && ocupado) ocupado(true);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    uint16_t iv(bool dbg) {
        for (unsigned n = 0; n < NCH; ++n)
            if ((ch_[n].ctl & IFG) && (ch_[n].ctl & IE)) {
                if (!dbg) { ch_[n].ctl = uint16_t(ch_[n].ctl & ~IFG); if (al_cambiar) al_cambiar(); }
                return uint16_t(2 * (n + 1));
            }
        return 0;
    }
    static uint32_t avanza(uint32_t a, unsigned modo, bool byte) {
        const uint32_t d = byte ? 1u : 2u;
        if (modo == 2) return (a - d) & 0xFFFFFu;
        if (modo == 3) return (a + d) & 0xFFFFFu;
        return a;
    }
    // Una transferencia del canal n; true si el bloque o la cuenta acaban
    bool una(unsigned n) {
        Canal& c = ch_[n];
        const bool sb = (c.ctl & SRCBYTE) != 0, db = (c.ctl & DSTBYTE) != 0;
        uint16_t v = lee ? lee(c.sa_t, sb) : 0;
        if (sb && !db) v &= 0x00FF;                              // byte a palabra: el alto a 0
        if (escribe) escribe(c.da_t, db ? uint16_t(v & 0xFF) : v, db);
        ++n_tr_;
        c.sa_t = avanza(c.sa_t, (c.ctl >> 8) & 3u, sb);
        c.da_t = avanza(c.da_t, (c.ctl >> 10) & 3u, db);
        if (c.sz_t) --c.sz_t;
        return c.sz_t == 0;
    }
    void fin(unsigned n) {
        Canal& c = ch_[n];
        c.ctl |= IFG;
        const bool repite = dt(c) >= 4;
        if (repite) arma(c);
        else c.ctl = uint16_t(c.ctl & ~EN);
        if (al_cambiar) al_cambiar();
        // El 30: el fin del canal anterior dispara al siguiente
        const unsigned sig = (n + 1) % NCH;
        if ((ch_[sig].ctl & EN) && tsel(sig) == 30) pide(sig);
    }
    void proc() {
        for (;;) {
            while (cola_.empty()) wait(ev_);
            const unsigned n = cola_.front();
            cola_.pop_front();
            // Desarmado mientras esperaba: la CPU no se queda esperándolo
            if (!(ch_[n].ctl & EN)) { if (ocupado && !hay_bloque_en_cola()) ocupado(false); continue; }
            const double f = f_mclk ? f_mclk() : 1e6;
            const sc_core::sc_time t2(f > 0 ? 2.0 / f : 2e-6, sc_core::SC_SEC);
            const unsigned modo = dt(ch_[n]) & 3u;              // 0 único, 1 bloque, 2-3 ráfaga
            if (modo == 0) {
                wait(t2);
                if (una(n)) fin(n);
                continue;
            }
            if (ocupado) ocupado(true);
            bool acabado = false;
            while (!acabado && (ch_[n].ctl & EN)) { wait(t2); acabado = una(n); }
            if (ocupado) ocupado(false);
            if (acabado) fin(n);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_DMA5XX_H
