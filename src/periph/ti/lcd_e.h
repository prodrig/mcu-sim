// =============================================================================
// lcd_e.h — El controlador LCD_E de las FR4xx (M7 de doc/analisis_msp430.md)
// [UG FR2 SLAU445I cap. 17]
//
//   +00 LCDCTL0    LCDDIV (15-11, 7 al reset), LCDSSEL (7-6: XT1CLK, ACLK,
//                  VLOCLK), LCDMX (5-3: estático a 8-mux), LCDSON (2), LCDLP
//                  (1), LCDON (0)
//   +02 LCDCTL1    LCDBLKONIE, LCDBLKOFFIE, LCDFRMIE (10-8) y sus IFG (2-0)
//   +04 LCDBLKCTL  LCDBLKPRE (4-2), LCDBLKMOD (1-0)
//   +06 LCDMEMCTL  LCDCLRBM (2), LCDCLRM (1), LCDDISP (0)
//   +08 LCDVCTL    LCDCPFSEL (15-12), VLCD (11-8), LCDCPEN, LCDREFEN,
//                  LCDSELVDD (7-5), LCDREFMODE (0)
//   +0A..+10       LCDPCTL0-3: el pin Lx es del LCD
//   +14..+1A       LCDCSSEL0-3: el pin Lx es un COM
//   +1E LCDIV      4 LCDBLKOFFIFG, 6 LCDBLKONIFG, 8 LCDFRMIFG
//   +20..+3F       LCDM0-31 (la memoria)
//   +40..+5F       LCDBM0-31 (la de parpadeo, o la segunda)
//
// EL TIEMPO [§17.2.4]: fLCD = fSOURCE / ((LCDDIV + 1) × MUXDIVIDER), con el
// divisor de la tabla 17-2 (64, 32, 16, 16, 12, 8, 8, 8); cada pulso de fLCD
// es una FASE, y una trama tiene 2 × mux fases (fFRAME = fLCD / (2 mux)).
//
// LAS FORMAS DE ONDA [§17.2.10-15]: con 1/3 de polarización (VLCD, 2/3, 1/3
// y 0), en la fase del COMk ese COM va a un extremo (VLCD o 0) y los demás a
// 1/3 o 2/3; un segmento encendido va al extremo contrario y uno apagado, al
// nivel intermedio del lado del COM. Estático: el COM0 entre VLCD y 0, y el
// segmento encendido en contra. [de modelo] Las ondas estándar cambian de
// polaridad dentro de la ranura de cada COM (fase par A, impar B); las de
// bajo consumo (LCDLP) la cambian por trama; 1/3 de polarización en todos
// los modos (en 8-mux la hoja recomienda 1/4: aquí no se distingue).
//
// LA MEMORIA [§17.2.1, §17.2.3]: hasta 4-mux, cada LCDMx lleva dos pines
// (Lx par en el nibble bajo, impar en el alto; el bit es el COM); de 5-mux
// arriba, uno. Si el pin es COM (LCDCSSEL), sus bits dicen qué COM es. Lo que
// se muestra depende del parpadeo (LCDBLKMOD) y de LCDDISP; LCDSON = 0 apaga
// todos los segmentos. LCDCLRM y LCDCLRBM borran en el borde de la trama.
// Escribir en la memoria borra las tres IFG.
//
// LA TENSIÓN [§17.2.7]: VLCD de la bomba de carga con la referencia interna
// (LCDCPEN y LCDREFEN: 2,60 V + 0,06 V × VLCDx), o VCC (LCDSELVDD); con R33
// externa, [de modelo] VCC.
//
// El SoC le da la frecuencia de la fuente, recoge las tensiones de los pines
// (v_pin) en cada fase (al_fase) y lo pone a cero con el dominio de LPM3.5:
// el LCD_E sigue en LPM3.5 y sus registros también.
// =============================================================================
#ifndef STM32_PERIPH_TI_LCD_E_H
#define STM32_PERIPH_TI_LCD_E_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <systemc>

namespace stm32 {

SC_MODULE(LcdE) {
    static constexpr unsigned NL = 64;           // L0-L63 como mucho
    // La frecuencia de LCDSSEL (0 XT1CLK, 1 ACLK, 2 VLOCLK); 0: parado
    std::function<double(unsigned)> f_fuente;
    std::function<double()> vcc;
    std::function<void()> al_cambiar;           // las IFG o las IE
    std::function<void()> al_fase;              // otra fase: el SoC repinta los pines
    unsigned npines = 40;                       // los Lx que tiene el chip

    SC_CTOR(LcdE) { SC_THREAD(hilo); reset(); }

    void reset() {
        ctl0_ = 0x3800; ctl1_ = 0; blkctl_ = 0; memctl_ = 0; vctl_ = 0;
        std::fill(pctl_, pctl_ + 4, 0); std::fill(cssel_, cssel_ + 4, 0);
        std::fill(m_, m_ + 32, 0); std::fill(bm_, bm_ + 32, 0);
        fase_ = 0; trama_ = 0; blk_cuenta_ = 0; blk_apagado_ = false; mem_b_ = false;
        recalcula();
    }
    // Vuelve a mirar la fuente (el SoC, cuando cambian sus relojes)
    void recalcula() { ev_.notify(sc_core::SC_ZERO_TIME); }

    bool on() const { return ctl0_ & 1u; }
    unsigned fuente() const { return (ctl0_ >> 6) & 3u; }
    unsigned mux() const { return ((ctl0_ >> 3) & 7u) + 1u; }
    bool pin_lcd(unsigned l) const { return l < npines && ((pctl_[l / 16] >> (l % 16)) & 1u); }
    bool es_com(unsigned l) const { return l < npines && ((cssel_[l / 16] >> (l % 16)) & 1u); }
    double f_lcd() const {
        static const unsigned DIV[8] = {64, 32, 16, 16, 12, 8, 8, 8};
        const double f = f_fuente ? f_fuente(fuente()) : 0.0;
        return f > 0.0 ? f / (double(((ctl0_ >> 11) & 31u) + 1u) * DIV[mux() - 1]) : 0.0;
    }
    double vlcd() const {
        const double vc = vcc ? vcc() : 3.3;
        if ((vctl_ & 0x0080) && (vctl_ & 0x0040)) return 2.60 + 0.06 * double((vctl_ >> 8) & 15u);
        return vc;
    }
    bool pide() const { return (ctl1_ & (ctl1_ >> 8) & 7u) != 0; }
    uint64_t tramas() const { return trama_; }

    // La tensión del pin Lx en esta fase; < 0: no la da el LCD (alta impedancia)
    double v_pin(unsigned l) const {
        if (!pin_lcd(l) || !on() || !corre_) return -1.0;
        const unsigned mx = mux();
        const bool lp = ctl0_ & 0x0002;
        const unsigned ranura = lp ? fase_ % mx : fase_ / 2;          // el COM de esta fase
        const bool b = lp ? (fase_ / mx) & 1u : fase_ & 1u;           // la polaridad (A: falso)
        const double v = vlcd();
        if (es_com(l)) {
            const unsigned c = com_de(l);
            const bool sel = c == ranura;
            if (mx == 1) return b ? 0.0 : v;
            if (sel) return b ? 0.0 : v;
            return b ? 2.0 * v / 3.0 : v / 3.0;
        }
        const bool encendido = segmento(l, ranura);
        if (mx == 1) return encendido == b ? v : 0.0;                 // en contra del COM si está encendido
        if (encendido) return b ? v : 0.0;
        return b ? v / 3.0 : 2.0 * v / 3.0;
    }
    // El COM del pin Lx (su configuración en LCDMx) [§17.2.3.2]
    unsigned com_de(unsigned l) const {
        const unsigned mx = mux();
        if (mx <= 4) {
            const uint8_t nib = uint8_t((m_[(l / 2) & 31] >> ((l & 1u) * 4)) & 0x0F);
            for (unsigned c = 0; c < 4; ++c) if (nib & (1u << c)) return c;
            return 99;
        }
        const uint8_t b = m_[l & 31];
        for (unsigned c = 0; c < 8; ++c) if (b & (1u << c)) return c;
        return 99;
    }
    // ¿Está encendido el segmento del pin Lx con el COMc, en lo que se muestra?
    bool segmento(unsigned l, unsigned c) const {
        if (!(ctl0_ & 0x0004)) return false;                           // LCDSON
        const unsigned mx = mux();
        if (c >= mx) return false;
        auto bit = [&](const uint8_t* mem) {
            if (mx <= 4) return bool((mem[(l / 2) & 31] >> ((l & 1u) * 4 + c)) & 1u);
            return bool((mem[l & 31] >> c) & 1u);
        };
        switch (blkctl_ & 3u) {
        case 1: return bit(m_) && !(blk_apagado_ && mx <= 4 && bit(bm_));
        case 2: return bit(m_) && !blk_apagado_;
        case 3: return mx <= 4 ? bit(mem_b_ ? bm_ : m_) : bit(m_);
        default: return bit((memctl_ & 1u) && mx <= 4 ? bm_ : m_);
        }
    }

    uint16_t lee(uint32_t off, bool dbg) {
        off &= 0x7E;
        if (off >= 0x20 && off < 0x40) return uint16_t(m_[off - 0x20] | (m_[off - 0x1F] << 8));
        if (off >= 0x40 && off < 0x60) return uint16_t(bm_[off - 0x40] | (bm_[off - 0x3F] << 8));
        switch (off) {
        case 0x00: return ctl0_;
        case 0x02: return ctl1_;
        case 0x04: return blkctl_;
        case 0x06: return uint16_t(memctl_ | (((blkctl_ & 3u) == 3 && mem_b_) ? 1u : 0u));
        case 0x08: return vctl_;
        case 0x0A: case 0x0C: case 0x0E: case 0x10: return pctl_[(off - 0x0A) / 2];
        case 0x14: case 0x16: case 0x18: case 0x1A: return cssel_[(off - 0x14) / 2];
        case 0x1E: {
            // la más prioritaria: BLKOFF (4), BLKON (6), FRM (8); leer la borra
            const uint16_t p = uint16_t(ctl1_ & (ctl1_ >> 8) & 7u);
            uint16_t v = 0, b = 0;
            if (p & 2u) { v = 4; b = 2; } else if (p & 4u) { v = 6; b = 4; } else if (p & 1u) { v = 8; b = 1; }
            if (v && !dbg) { ctl1_ = uint16_t(ctl1_ & ~b); aviso(); }
            return v;
        }
        default: return 0;
        }
    }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        off &= 0x7E;
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        if (off >= 0x20 && off < 0x60) {
            uint8_t* mem = off < 0x40 ? m_ : bm_;
            const unsigned i = (off & 0x1F);
            if (m & 0x00FF) mem[i] = uint8_t(v);
            if (m & 0xFF00) mem[i + 1] = uint8_t(v >> 8);
            if (ctl1_ & 7u) { ctl1_ = uint16_t(ctl1_ & ~7u); aviso(); }   // escribir la memoria borra las IFG
            repinta();
            return;
        }
        switch (off) {
        case 0x00: {
            const bool antes = on();
            ctl0_ = uint16_t(mz(ctl0_) & 0xF8FFu);
            if (on() != antes) { fase_ = 0; blk_cuenta_ = 0; }
            recalcula(); repinta();
            return;
        }
        case 0x02: ctl1_ = uint16_t(mz(ctl1_) & 0x0707u); aviso(); return;
        case 0x04:
            blkctl_ = uint16_t(mz(blkctl_) & 0x001Fu);
            if ((blkctl_ & 3u) == 0) { blk_cuenta_ = 0; blk_apagado_ = false; mem_b_ = false; }
            if ((blkctl_ & 3u) != 0) memctl_ = uint16_t(memctl_ & ~1u);
            repinta();
            return;
        case 0x06: {
            uint16_t n = uint16_t(mz(memctl_) & 0x0007u);
            if ((blkctl_ & 3u) != 0 || mux() > 4) n = uint16_t(n & ~1u);
            memctl_ = n;
            // sin el LCD en marcha no hay borde de trama: se borra ya [de modelo]
            if (!corre_) borra_pendiente();
            repinta();
            return;
        }
        case 0x08: vctl_ = uint16_t(mz(vctl_) & 0xFFE1u); repinta(); return;
        case 0x0A: case 0x0C: case 0x0E: case 0x10: pctl_[(off - 0x0A) / 2] = mz(pctl_[(off - 0x0A) / 2]); repinta(); return;
        case 0x14: case 0x16: case 0x18: case 0x1A: cssel_[(off - 0x14) / 2] = mz(cssel_[(off - 0x14) / 2]); repinta(); return;
        case 0x1E: if (ctl1_ & 7u) { ctl1_ = uint16_t(ctl1_ & ~7u); aviso(); } return;   // escribir borra todas
        default: return;
        }
    }
    uint8_t mem(unsigned i) const { return i < 32 ? m_[i] : 0; }
    uint8_t bmem(unsigned i) const { return i < 32 ? bm_[i] : 0; }

private:
    uint16_t ctl0_ = 0x3800, ctl1_ = 0, blkctl_ = 0, memctl_ = 0, vctl_ = 0;
    uint16_t pctl_[4] = {}, cssel_[4] = {};
    uint8_t m_[32] = {}, bm_[32] = {};
    unsigned fase_ = 0;
    uint64_t trama_ = 0;
    unsigned blk_cuenta_ = 0;
    bool blk_apagado_ = false, mem_b_ = false, corre_ = false;
    sc_core::sc_event ev_;

    void aviso() { if (al_cambiar) al_cambiar(); }
    void repinta() { if (al_fase) al_fase(); }
    void borra_pendiente() {
        if (memctl_ & 2u) { std::fill(m_, m_ + 32, 0); memctl_ = uint16_t(memctl_ & ~2u); }
        if (memctl_ & 4u) { if (mux() <= 4) std::fill(bm_, bm_ + 32, 0); memctl_ = uint16_t(memctl_ & ~4u); }
    }
    // El borde de la trama: LCDCLRM/LCDCLRBM, el parpadeo y LCDFRMIFG
    void borde() {
        ++trama_;
        borra_pendiente();
        const unsigned modo = blkctl_ & 3u;
        if (modo) {
            // medio periodo de BLKCLK en fases: (LCDMX + 1) × 2^(PRE + 2) / 2... de fLCD
            const unsigned medio = std::max(1u, (mux() * (1u << (((blkctl_ >> 2) & 7u) + 2))) / 2);
            blk_cuenta_ += 2 * mux();
            if (blk_cuenta_ >= medio) {
                blk_cuenta_ = 0;
                if (modo == 3) { mem_b_ = !mem_b_; ctl1_ = uint16_t(ctl1_ | (mem_b_ ? 4u : 2u)); }
                else if (!(modo == 1 && mux() > 4)) {
                    blk_apagado_ = !blk_apagado_;
                    ctl1_ = uint16_t(ctl1_ | (blk_apagado_ ? 2u : 4u));
                }
            }
        }
        ctl1_ = uint16_t(ctl1_ | 1u);
        aviso();
    }
    double f_act_ = 0.0;
    sc_core::sc_time t_sig_{sc_core::SC_ZERO_TIME};
    void hilo() {
        using namespace sc_core;
        for (;;) {
            const double f = on() ? f_lcd() : 0.0;
            if (f <= 0.0) {
                if (corre_) { corre_ = false; repinta(); }
                wait(ev_);
                continue;
            }
            const sc_time ahora = sc_time_stamp(), p(1.0 / f, SC_SEC);
            if (!corre_) { corre_ = true; fase_ = 0; f_act_ = f; t_sig_ = ahora + p; repinta(); }
            else if (f != f_act_) { f_act_ = f; t_sig_ = ahora + p; }
            if (ahora >= t_sig_) {
                fase_ = (fase_ + 1) % (2 * mux());
                t_sig_ += p;
                if (t_sig_ <= ahora) t_sig_ = ahora + p;
                if (fase_ == 0) borde();
                repinta();
                continue;
            }
            wait(t_sig_ - ahora, ev_);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_LCD_E_H
