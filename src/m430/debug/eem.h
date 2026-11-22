// =============================================================================
// eem.h — El Enhanced Emulation Module de un MSP430 (M2 de doc/analisis_msp430.md, §7.1)
//
// Los registros, en el mapa de EEM_defs.h de la MSP Debug Stack (BSD-3; se
// leen y escriben por IR_EMEX_DATA_EXCHANGE, EemDataExchange.c):
//
//   TBn  0x00 + 8·n   disparadores de bus: VAL, CTL, MSK y CMB
//        CTL: bit 0 MAB/MDB; bits 1, 2, 5 y 6 el tipo de acceso (TRIG_0
//        búsqueda, TRIG_2 no búsqueda, TRIG_3 cualquiera, TRIG_6 lectura,
//        TRIG_7 escritura...); bits 3-4 la comparación (=, >, <, ≠).
//        MSK: los bits a 1 no se comparan. CMB: a qué combinaciones va.
//   BREAKREACT 0x80   qué combinaciones paran la CPU
//   GENCTRL    0x82   EEM_EN, CLEAR_STOP, EMU_CLK_EN, EMU_FEAT_EN, EEM_RST;
//                     E_STOPPED (0x80) dice si la paró el EEM
//   EEMVER     0x86   versión y tamaño [de modelo: 0x0n00, n = tamaño 1..4]
//   GENCLKCTRL 0x88   qué relojes se paran con la CPU parada (STOP_ACLK 0x02,
//                     STOP_SMCLK 0x04, STOP_MCLK 0x08); 0x26 por omisión
//                     en la herramienta (DefaultClkCntrl)
//   MODCLKCTRL0 0x8A  qué módulos se paran (control «extendido»)
//   TRIGFLAG   0x8E   qué disparadores han saltado
//
// Una combinación salta cuando saltan TODOS los disparadores que la tienen en
// su CMB; si la combinación está en BREAKREACT, la CPU se para. Tamaños [UG2
// tabla 28-1]: XS 2 disparadores, S 3, M 5, L 8 (los de registro, el
// secuenciador y el almacenamiento de estado se declaran: contestan y no
// hacen nada).
// =============================================================================
#ifndef STM32_M430_DEBUG_EEM_H
#define STM32_M430_DEBUG_EEM_H

#include <cstdint>
#include <cstdio>

namespace stm32 {

enum class M430Bus : uint8_t { Busqueda, Lectura, Escritura };

class EemM430 {
public:
    enum : uint16_t { EEM_EN = 0x0001, CLEAR_STOP = 0x0002, EEM_RST = 0x0040, E_STOPPED = 0x0080,
                      STOP_ACLK = 0x0002, STOP_SMCLK = 0x0004, STOP_MCLK = 0x0008 };
    static constexpr unsigned MAX_TB = 8;

    explicit EemM430(unsigned n_disparadores = 2, unsigned tam = 1) : n_(n_disparadores < MAX_TB ? n_disparadores : MAX_TB),
                                                                       tam_(tam) { reset(); }
    unsigned disparadores() const { return n_; }

    void reset() {
        for (auto& t : tb_) t = Tb();
        breakreact_ = genctrl_ = trigflag_ = 0;
        genclkctrl_ = 0;
        modclkctrl0_ = 0;
        parado_ = false;
    }

    // --- El acceso por JTAG (EemDataExchange) ----------------------------------
    uint16_t lee(uint8_t dir) const {
        if (dir < 8 * n_ && dir / 8u < MAX_TB) {
            const Tb& t = tb_[dir / 8];
            switch (dir & 7) { case 0: return uint16_t(t.val); case 2: return t.ctl;
                               case 4: return uint16_t(t.msk); case 6: return t.cmb; default: return 0; }
        }
        switch (dir) {
        case 0x80: return breakreact_;
        case 0x82: return uint16_t(genctrl_ | (parado_ ? E_STOPPED : 0));
        case 0x86: return uint16_t(tam_ << 8);
        case 0x88: return genclkctrl_;
        case 0x8A: return modclkctrl0_;
        case 0x8E: return trigflag_;
        default: return 0;
        }
    }
    void escribe(uint8_t dir, uint32_t v) {
        if (dir < 8 * n_ && dir / 8u < MAX_TB) {
            Tb& t = tb_[dir / 8];
            switch (dir & 7) { case 0: t.val = v & 0xFFFFFu; break; case 2: t.ctl = uint16_t(v); break;
                               case 4: t.msk = v & 0xFFFFFu; break; case 6: t.cmb = uint16_t(v); break; default: break; }
            return;
        }
        switch (dir) {
        case 0x80: breakreact_ = uint16_t(v); break;
        case 0x82:
            if (v & CLEAR_STOP) parado_ = false;
            if (v & EEM_RST) { const uint16_t c = genclkctrl_, m = modclkctrl0_; reset(); genclkctrl_ = c; modclkctrl0_ = m; }
            genctrl_ = uint16_t(v & ~(CLEAR_STOP | EEM_RST | E_STOPPED));
            break;
        case 0x88: genclkctrl_ = uint16_t(v); break;
        case 0x8A: modclkctrl0_ = uint16_t(v); break;
        case 0x8E: trigflag_ = uint16_t(trigflag_ & ~v); break;
        default:
            if (!avisado_) { avisado_ = true; std::fprintf(stderr, "[eem] registro 0x%02X declarado (aviso una vez)\n", dir); }
            break;
        }
    }

    // --- Lo que mira el núcleo ---------------------------------------------------
    bool activo() const { return (genctrl_ & EEM_EN) != 0; }
    // Un acceso de bus: ¿salta una combinación que para? (y apunta TRIGFLAG)
    bool acceso(M430Bus tipo, uint32_t mab, uint32_t mdb) {
        if (!activo()) return false;
        uint16_t salta = 0;
        for (unsigned i = 0; i < n_; ++i)
            if (tb_[i].cmb && coincide(tb_[i], tipo, mab, mdb)) salta = uint16_t(salta | (1u << i));
        if (!salta) return false;
        trigflag_ = uint16_t(trigflag_ | salta);
        // Las combinaciones: todas las suyas tienen que saltar
        for (unsigned c = 0; c < 16; ++c) {
            if (!((breakreact_ >> c) & 1u)) continue;
            uint16_t miembros = 0;
            for (unsigned i = 0; i < n_; ++i) if ((tb_[i].cmb >> c) & 1u) miembros = uint16_t(miembros | (1u << i));
            if (miembros && (salta & miembros) == miembros) { parado_ = true; return true; }
        }
        return false;
    }
    bool parado() const { return parado_; }
    void suelta() { parado_ = false; }
    uint16_t genclkctrl() const { return genclkctrl_; }
    uint16_t modclkctrl0() const { return modclkctrl0_; }

private:
    struct Tb { uint32_t val = 0, msk = 0; uint16_t ctl = 0, cmb = 0; };
    Tb tb_[MAX_TB];
    unsigned n_, tam_;
    uint16_t breakreact_ = 0, genctrl_ = 0, trigflag_ = 0, genclkctrl_ = 0, modclkctrl0_ = 0;
    bool parado_ = false, avisado_ = false;

    static bool tipo_vale(uint16_t ctl, M430Bus t) {
        const unsigned k = ((ctl >> 1) & 3u) | (((ctl >> 5) & 3u) << 2);   // TRIG_0..TRIG_F
        const bool f = t == M430Bus::Busqueda, r = t != M430Bus::Escritura, w = t == M430Bus::Escritura;
        switch (k) {
        case 0x0: case 0x1: return f;                // búsqueda (y con retención)
        case 0x2: return !f;
        case 0x3: return true;
        case 0x4: return !f && r;
        case 0x5: return w;
        case 0x6: return r;
        case 0x7: return w;
        case 0x8: case 0xA: return !f;               // sin DMA (no hay DMA en el laboratorio)
        case 0x9: return false;
        case 0xB: return w;
        case 0xC: return !f && r;
        case 0xD: return r;
        default: return false;
        }
    }
    static bool coincide(const Tb& t, M430Bus tipo, uint32_t mab, uint32_t mdb) {
        if (!tipo_vale(t.ctl, tipo)) return false;
        const uint32_t m = ~t.msk & 0xFFFFFu;
        const uint32_t x = ((t.ctl & 1u) ? mdb : mab) & m, v = t.val & m;
        switch ((t.ctl >> 3) & 3u) {
        case 0: return x == v;
        case 1: return x > v;
        case 2: return x < v;
        default: return x != v;
        }
    }
};

} // namespace stm32
#endif // STM32_M430_DEBUG_EEM_H
