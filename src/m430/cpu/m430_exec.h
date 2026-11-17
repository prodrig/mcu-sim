// =============================================================================
// m430_exec.h — El núcleo MSP430: CPU, CPUX y CPUXV2 (M1 de
// doc/analisis_msp430.md, §3)
//
// C++ puro, como el hart RISC-V: no sabe de SystemC. `paso()` ejecuta una
// instrucción (o entra en una interrupción) y dice cuántos ciclos de MCLK ha
// costado según la tabla de su CPU (m430_ciclos.h); la latencia que añada el
// bus la suma NucleoM430.
//
// LO QUE HACE, con su fuente:
//   * los siete modos de direccionamiento y el generador de constantes [UG2
//     §3.3]; en la CPUX, los cálculos de dirección de 16 y 20 bits de las
//     instrucciones MSP430 y MSP430X [SLAU391F §1.4]: un índice sobre un
//     registro por debajo de 64 KB se queda en 64 KB; por encima, suma con
//     signo en 20 bits;
//   * escribir un registro con .B borra sus bits 19:8, con .W los 19:16 [SLAU391F
//     §1.4.1]. El SP y el PC son siempre pares; R3 como destino no guarda nada;
//   * la palabra de extensión: .A, índices, absolutos e inmediatos de 20 bits,
//     la repetición (n o Rn.3:0, solo en modo registro) y ZC [tablas 1-11 y 1-12];
//   * interrupciones: apila PC y SR (en la CPUX, PC.19:16 en SR.15:12),
//     borra el SR (menos SCG0 en la CPUXV2) y salta al vector; RETI deshace
//     [UG2 §2.2.3; SLAU391F §1.2]. La instrucción que sigue a la que pone GIE
//     se ejecuta siempre antes de atender una interrupción [UG2, EINT];
//   * bajo consumo: con CPUOFF el núcleo no ejecuta; una interrupción lo
//     despierta y RETI lo vuelve a dormir si la rutina no ha tocado el SR
//     apilado [§3.4].
//
// Para el laboratorio (M1) hay dos ganchos: `llamada_lab`, que intercepta un
// CALL/CALLA a una dirección (las llamadas al sistema de libgloss en
// 0x180-0x1BF, como el simulador de GDB), y `gancho` en `pc_gancho` (el
// C$$IO$$ de la E/S de TI).
// =============================================================================
#ifndef STM32_M430_CPU_M430_EXEC_H
#define STM32_M430_CPU_M430_EXEC_H

#include <cstdint>
#include <functional>
#include "m430_caps.h"
#include "m430_ciclos.h"
#include "m430_decod.h"
#include "m430_estado.h"
#include "m430_irq.h"
#include "m430_errata.h"

namespace stm32 {

// Lo que deja cada instrucción, para las trazas
struct M430Commit {
    uint32_t pc;       // dónde empezaba (la palabra de extensión, si la hay)
    uint16_t op;       // el código
    unsigned ciclos;
};

class M430Cpu {
public:
    struct Paso {
        unsigned ciclos = 0;
        bool dormido = false;      // CPUOFF: no ha ejecutado nada
        bool irq = false;          // ha entrado en una interrupción
        bool ilegal = false;
    };

    M430Estado e;
    std::function<bool(uint32_t destino)> llamada_lab;
    uint32_t pc_gancho = 0xFFFFFFFFu;
    std::function<void()> gancho;
    std::function<void(uint16_t antes, uint16_t ahora)> al_cambiar_sr;
    std::function<void(const M430Commit&)> traza;
    std::function<void(uint32_t pc, uint16_t op)> al_ilegal;
    M430Errata errata;

    explicit M430Cpu(const M430Caps& caps)
        : caps_(caps), cic_(m430_ciclos(caps.arq)), x_(caps.tiene_20bits()),
          amask_(x_ ? 0xFFFFFu : 0xFFFFu) {
        errata.activas = caps.errata;
    }
    void pon_memoria(m430_mem_if* m) { mem_ = m; }
    void pon_irq(m430_irq_if* i) { irq_ = i; }
    const M430Caps& caps() const { return caps_; }
    const M430Ciclos& tabla() const { return cic_; }

    // El reset (POR, BOR o PUC): SR a 0 y el PC del vector de reset [UG2 §2.2.1]
    void reset() {
        const uint16_t antes = e.sr();
        // Solo SR y PC [UG2 §2.1.2]: los demás registros no tienen reset y
        // conservan lo que tenían (al encender, cero) [de modelo]. Hay código
        // que lo usa sin saberlo: el «blinky» de SLAA754 arranca un mov #SP
        // más allá y vive del SP que quedó del programa anterior
        if (!conserva_registros) for (unsigned i = 0; i < 16; ++i) e.r[i] = 0;
        e.r[2] = 0; e.r[3] = 0;
        const uint16_t v = lee16(0xFFFE, M430Acceso::Vector);
        e.r[0] = v & 0xFFFEu;
        retraso_gie_ = false;
        if (al_cambiar_sr && antes) al_cambiar_sr(antes, 0);
        // «If the reset vector's content is 0FFFFh the device will be disabled
        // for minimum power consumption» [UG2 §2.1.2]: con la Flash borrada,
        // el chip se queda en LPM4 en vez de ejecutar basura
        if (v == 0xFFFFu && vector_vacio_apaga) pon_sr(0x00F0);
    }
    // Ver reset(): el comportamiento de 2xx con el vector de reset borrado
    bool vector_vacio_apaga = true;
    // Ver reset(): los registros de trabajo no se reinician (los SoC de chip lo
    // activan; el laboratorio, no, para que las pruebas diferenciales empiecen
    // de cero como msp430-elf-run y mspdebug)
    bool conserva_registros = false;

    uint32_t pc() const { return e.r[0]; }
    uint16_t sr() const { return uint16_t(e.r[2]); }
    uint32_t reg(unsigned i) const { return e.r[i & 15]; }
    void pon_reg(unsigned i, uint32_t v) {
        i &= 15;
        if (i == 2) { pon_sr(uint16_t(v)); return; }
        if (i == 3) return;
        e.r[i] = v & amask_;
        if (i <= 1) e.r[i] &= ~1u;
    }
    void pon_sr(uint16_t v) {
        const uint16_t antes = sr();
        e.r[2] = v & SR_BITS;
        if (al_cambiar_sr && antes != sr()) al_cambiar_sr(antes, sr());
    }
    bool durmiendo() const { return (sr() & SR_CPUOFF) != 0 && !ignora_cpuoff; }
    // La CPUXv2 bajo JTAG: TCLK es MCLK y CPUOFF no la para (lo pone el núcleo)
    bool ignora_cpuoff = false;
    uint64_t instrucciones() const { return n_ins_; }
    uint64_t ciclos() const { return n_cic_; }

    // Una instrucción, o la entrada en una interrupción
    Paso paso() {
        Paso p;
        // Las interrupciones, entre instrucción e instrucción
        if (irq_ && !retraso_gie_) {
            const uint16_t v = irq_->pendiente((sr() & SR_GIE) != 0);
            if (v) { entra_irq(v); p.irq = true; p.ciclos = cic_.irq; n_cic_ += p.ciclos; return p; }
        }
        retraso_gie_ = false;
        if (durmiendo()) { p.dormido = true; return p; }
        if (e.r[0] == pc_gancho && gancho) gancho();
        p.ciclos = ejecuta(p.ilegal);
        ++n_ins_;
        n_cic_ += p.ciclos;
        return p;
    }

    // Lo que usan la depuración y las pruebas: leer y escribir la memoria como la CPU
    uint32_t lee_mem(uint32_t a, unsigned t) { return lee(a & amask_, t, M430Acceso::Lectura); }
    void escribe_mem(uint32_t a, unsigned t, uint32_t v) { escribe(a & amask_, t, v, M430Acceso::Escritura); }

private:
    const M430Caps& caps_;
    const M430Ciclos& cic_;
    const bool x_;
    const uint32_t amask_;
    m430_mem_if* mem_ = nullptr;
    m430_irq_if* irq_ = nullptr;
    bool retraso_gie_ = false;
    uint64_t n_ins_ = 0, n_cic_ = 0;

    // --- Memoria -----------------------------------------------------------------
    uint32_t lee16(uint32_t a, M430Acceso t) {
        uint32_t v = 0;
        if (mem_) mem_->lee(a & amask_ & ~1u, 2, v, t);
        return v & 0xFFFFu;
    }
    void escribe16(uint32_t a, uint32_t v, M430Acceso t) {
        if (mem_) mem_->escribe(a & amask_ & ~1u, 2, v & 0xFFFFu, t);
    }
    uint32_t lee(uint32_t a, unsigned tam, M430Acceso t) {
        if (tam == 1) {
            uint32_t v = 0;
            if (mem_) mem_->lee(a & amask_, 1, v, t);
            return v & 0xFFu;
        }
        if (tam == 2) return lee16(a, t);
        return lee16(a, t) | ((lee16(a + 2, t) & 0xFu) << 16);
    }
    void escribe(uint32_t a, unsigned tam, uint32_t v, M430Acceso t) {
        if (tam == 1) { if (mem_) mem_->escribe(a & amask_, 1, v & 0xFFu, t); return; }
        escribe16(a, v, t);
        if (tam == 4) escribe16(a + 2, (v >> 16) & 0xFu, t);
    }
    uint16_t busca(M430Acceso t = M430Acceso::Busqueda) {
        const uint16_t w = uint16_t(lee16(e.r[0], t));
        e.r[0] = (e.r[0] + 2) & amask_;
        return w;
    }
    void push(uint32_t v, unsigned tam) {
        e.r[1] = (e.r[1] - (tam == 4 ? 4u : 2u)) & amask_;
        escribe(e.r[1], tam, v, M430Acceso::Pila);
    }
    uint32_t pop(unsigned tam) {
        const uint32_t v = lee(e.r[1], tam, M430Acceso::Pila);
        e.r[1] = (e.r[1] + (tam == 4 ? 4u : 2u)) & amask_;
        return v;
    }

    static uint32_t mascara(unsigned t) { return t == 1 ? 0xFFu : t == 2 ? 0xFFFFu : 0xFFFFFu; }
    static uint32_t msb(unsigned t) { return t == 1 ? 0x80u : t == 2 ? 0x8000u : 0x80000u; }
    static uint32_t sext16(uint32_t w) { return (w & 0x8000u) ? (w | 0xFFFF0000u) : w; }

    // --- Registros -------------------------------------------------------------------
    uint32_t lee_reg(unsigned r, unsigned t) const { return e.r[r] & mascara(t); }
    void escribe_reg(unsigned r, uint32_t v, unsigned t) {
        v &= mascara(t);
        if (r == 3) return;
        if (r == 2) { pon_sr(uint16_t(v)); return; }
        e.r[r] = v & amask_;
        if (r <= 1) e.r[r] &= ~1u;
    }

    // --- Operandos ---------------------------------------------------------------------
    struct Op {
        enum Tipo : uint8_t { Reg, Mem, Const } tipo = Reg;
        uint8_t reg = 0;
        uint32_t dir = 0, k = 0;
        uint8_t modo = MO_RN;      // para la tabla de ciclos
    };

    // La dirección X(Rn) de una instrucción MSP430 (sin extensión) en la CPUX
    uint32_t idx16(uint32_t base, uint32_t x) const {
        if (!x_) return (base + x) & 0xFFFFu;
        if (base < 0x10000u) return (base + x) & 0xFFFFu;                 // §1.4.2.1
        return (base + sext16(x)) & 0xFFFFFu;                             // §1.4.2.2
    }

    Op origen(unsigned as, unsigned r, unsigned t, const M430Ins& in) {
        Op o;
        const bool ext = in.ext;
        switch (as) {
        case 0:
            if (r == 3) { o.tipo = Op::Const; o.k = 0; }
            else { o.tipo = Op::Reg; o.reg = uint8_t(r); }
            o.modo = MO_RN;
            return o;
        case 1:
            if (r == 3) { o.tipo = Op::Const; o.k = 1; o.modo = MO_RN; return o; }
            o.tipo = Op::Mem;
            if (r == 2) {                                                  // &EDE
                const uint32_t w = busca();
                o.dir = ext ? ((in.src_hi << 16) | w) : w;
                o.modo = MO_ABS;
            } else {
                const uint32_t base = r == 0 ? e.r[0] : e.r[r];            // EDE: el PC del índice
                const uint32_t w = busca();
                o.dir = ext ? ((base + ((in.src_hi << 16) | w)) & 0xFFFFFu) : idx16(base, w);
                o.modo = r == 0 ? MO_SIM : MO_IDX;
            }
            return o;
        case 2:
            if (r == 2) { o.tipo = Op::Const; o.k = 4; o.modo = MO_RN; return o; }
            if (r == 3) { o.tipo = Op::Const; o.k = 2; o.modo = MO_RN; return o; }
            if (r == 0) errata.avisa(m430::ERR_CPU22, "CPU22 (@PC como origen)", e.r[0]);
            o.tipo = Op::Mem; o.dir = e.r[r]; o.modo = MO_IND;
            return o;
        default:
            if (r == 2) { o.tipo = Op::Const; o.k = 8; o.modo = MO_RN; return o; }
            if (r == 3) { o.tipo = Op::Const; o.k = 0xFFFFFu; o.modo = MO_RN; return o; }
            if (r == 0) {                                                  // #N
                const uint32_t w = busca();
                o.tipo = Op::Const;
                o.k = ext ? ((in.src_hi << 16) | w) : w;
                o.modo = MO_INM;
                return o;
            }
            o.tipo = Op::Mem; o.dir = e.r[r]; o.modo = MO_INC;
            {
                const unsigned inc = t == 4 ? 4u : (t == 1 && r != 1) ? 1u : 2u;
                e.r[r] = (e.r[r] + inc) & amask_;
            }
            return o;
        }
    }
    Op destino(unsigned ad, unsigned r, const M430Ins& in) {
        Op o;
        if (!ad) { o.tipo = Op::Reg; o.reg = uint8_t(r); o.modo = r == 0 ? MD_PC : MD_RM; return o; }
        o.tipo = Op::Mem;
        if (r == 2) {
            const uint32_t w = busca();
            o.dir = in.ext ? ((in.dst_hi << 16) | w) : w;
            o.modo = MD_ABS;
        } else {
            const uint32_t base = r == 0 ? e.r[0] : r == 3 ? 0u : e.r[r];
            const uint32_t w = busca();
            o.dir = in.ext ? ((base + ((in.dst_hi << 16) | w)) & 0xFFFFFu) : idx16(base, w);
            o.modo = r == 0 ? MD_SIM : MD_IDX;
        }
        return o;
    }
    uint32_t valor(const Op& o, unsigned t) {
        switch (o.tipo) {
        case Op::Const: return o.k & mascara(t);
        case Op::Reg:   return lee_reg(o.reg, t);
        default:        return lee(o.dir, t, M430Acceso::Lectura);
        }
    }
    void guarda(const Op& o, uint32_t v, unsigned t) {
        if (o.tipo == Op::Reg) escribe_reg(o.reg, v, t);
        else if (o.tipo == Op::Mem) escribe(o.dir, t, v & mascara(t), M430Acceso::Escritura);
    }

    // --- Banderas ----------------------------------------------------------------------
    void banderas(uint32_t r, unsigned t, int c, int v) {
        uint16_t s = sr() & uint16_t(~(SR_N | SR_Z | (c >= 0 ? SR_C : 0) | (v >= 0 ? SR_V : 0)));
        if (r & msb(t)) s |= SR_N;
        if (!(r & mascara(t))) s |= SR_Z;
        if (c > 0) s |= SR_C;
        if (v > 0) s |= SR_V;
        pon_sr(s);
    }
    // dst + src + c, con N, Z, C y V
    uint32_t suma(uint32_t d, uint32_t s, uint32_t c, unsigned t) {
        const uint32_t m = mascara(t);
        const uint64_t r = uint64_t(d & m) + (s & m) + c;
        const uint32_t res = uint32_t(r) & m;
        const bool cy = (r >> (t == 1 ? 8 : t == 2 ? 16 : 20)) & 1;
        const bool ov = ((~(d ^ s)) & (d ^ res) & msb(t)) != 0;
        banderas(res, t, cy, ov);
        return res;
    }
    uint32_t dadd(uint32_t d, uint32_t s, unsigned t) {
        const unsigned dig = t == 1 ? 2 : t == 2 ? 4 : 5;
        uint32_t c = (sr() & SR_C) ? 1u : 0u, r = 0;
        for (unsigned i = 0; i < dig; ++i) {
            uint32_t x = ((d >> (4 * i)) & 0xF) + ((s >> (4 * i)) & 0xF) + c;
            if (x > 9) { x -= 10; c = 1; } else c = 0;
            r |= (x & 0xF) << (4 * i);
        }
        banderas(r, t, int(c), -1);                    // V: indefinido; se deja [UG2 DADD]
        return r;
    }

    // --- Interrupciones -------------------------------------------------------------
    void entra_irq(uint16_t vector) {
        const uint32_t pcv = e.r[0];
        const uint16_t s = sr();
        if (x_) {
            push(pcv & 0xFFFFu, 2);
            push(((pcv >> 4) & 0xF000u) | (s & 0x0FFFu), 2);
        } else {
            push(pcv, 2);
            push(s, 2);
        }
        irq_->acepta(vector);
        pon_sr(caps_.irq_conserva_scg0 ? uint16_t(s & SR_SCG0) : uint16_t(0));
        e.r[0] = lee16(vector, M430Acceso::Vector) & 0xFFFEu;
    }

    // --- La instrucción -------------------------------------------------------------
    unsigned ejecuta(bool& ilegal) {
        const uint32_t pc0 = e.r[0];
        uint16_t w = busca(M430Acceso::Instruccion);
        bool ext = false;
        uint16_t xw = 0;
        if (x_ && m430_es_ext(w)) { ext = true; xw = w; w = busca(); }
        const M430Ins in = m430_decodifica(w, x_, ext, xw);
        unsigned c = 0;
        switch (in.clase) {
        case M430Clase::F1:        c = formato1(in); break;
        case M430Clase::F2:        c = formato2(in); break;
        case M430Clase::Salto:     c = salto(in); break;
        case M430Clase::Reti:      c = reti(); break;
        case M430Clase::Direccion: c = direccion(in); break;
        case M430Clase::RotM:      c = rotm(in); break;
        case M430Clase::PushM:     c = pushm(in); break;
        case M430Clase::PopM:      c = popm(in); break;
        case M430Clase::Calla:     c = calla(in); break;
        default:
            ilegal = true;
            if (al_ilegal) al_ilegal(pc0, w);
            c = 1;
            break;
        }
        if (traza) traza(M430Commit{pc0, w, c});
        return c;
    }

    // Repeticiones de una instrucción con extensión de modo registro
    unsigned repeticiones(const M430Ins& in) const {
        if (!in.ext || !in.ext_reg) return 1;
        return (in.rep_reg ? (e.r[in.rep] & 0xFu) : in.rep) + 1u;
    }

    unsigned formato1(const M430Ins& in) {
        const unsigned t = in.tam();
        const unsigned n = repeticiones(in);
        const uint16_t sr_antes = sr();
        Op s = origen(in.as, in.src, t, in);
        Op d = destino(in.ad, in.dst, in);
        const bool mbc = in.op == F1_MOV || in.op == F1_BIT || in.op == F1_CMP;
        for (unsigned k = 0; k < n; ++k) {
            const uint32_t sv = valor(s, t);
            const uint32_t cin = (in.zc) ? 0u : ((sr() & SR_C) ? 1u : 0u);
            uint32_t r = 0;
            bool escribe_d = true;
            switch (in.op) {
            case F1_MOV: r = sv; break;
            case F1_ADD: r = suma(valor(d, t), sv, 0, t); break;
            case F1_ADDC: r = suma(valor(d, t), sv, cin, t); break;
            case F1_SUBC: r = suma(valor(d, t), ~sv & mascara(t), cin, t); break;
            case F1_SUB: r = suma(valor(d, t), ~sv & mascara(t), 1, t); break;
            case F1_CMP: suma(valor(d, t), ~sv & mascara(t), 1, t); escribe_d = false; break;
            case F1_DADD: {
                if (in.zc) pon_sr(uint16_t(sr() & ~SR_C));
                r = dadd(valor(d, t), sv, t);
                break;
            }
            case F1_BIT: { const uint32_t x = valor(d, t) & sv; banderas(x, t, (x & mascara(t)) != 0, 0);
                           escribe_d = false; break; }
            case F1_BIC: r = valor(d, t) & ~sv; break;
            case F1_BIS: r = valor(d, t) | sv; break;
            case F1_XOR: { const uint32_t dv = valor(d, t); r = (dv ^ sv) & mascara(t);
                           banderas(r, t, r != 0, ((dv & sv & msb(t)) != 0)); break; }
            case F1_AND: { r = valor(d, t) & sv & mascara(t); banderas(r, t, r != 0, 0); break; }
            default: break;
            }
            if (escribe_d) guarda(d, r, t);
        }
        // EINT (BIS #8,SR) y cualquier escritura que encienda GIE: la siguiente va antes
        if (!(sr_antes & SR_GIE) && (sr() & SR_GIE)) retraso_gie_ = true;
        // Los ciclos
        const uint8_t mo = s.modo, md = d.modo;
        if (!in.ext) {
            unsigned c = cic_.f1[mo][md];
            if (cic_.f1_mbc_menos1 && mbc && md >= MD_IDX) --c;
            return c;
        }
        if (t == 4) {
            unsigned c = cic_.x1_a[mo][md];
            if (mbc && md >= MD_IDX) c -= 2;
            if (md == MD_PC && mo >= MO_INM && (in.op == F1_MOV || in.op == F1_ADD || in.op == F1_SUB)) --c;
            return c;
        }
        unsigned c = cic_.x1_bw[mo][md];
        if (mo == MO_RN && md == MD_RM && in.ext_reg) return n + 1;       // repetición: n + 1
        if (mbc && md >= MD_IDX) --c;
        if (md == MD_PC && mo >= MO_INM && (in.op == F1_MOV || in.op == F1_ADD || in.op == F1_SUB)) --c;
        return c;
    }

    unsigned formato2(const M430Ins& in) {
        unsigned t = in.tam();
        // SWPBX.A / SXTX.A: A/L = 0, B/W = 0
        if (in.ext && !in.al && !in.bw) t = 4;
        const unsigned n = repeticiones(in);
        Op o = origen(in.as, in.dst, t, in);
        const unsigned mo = o.modo;
        switch (in.op) {
        case F2_RRC: case F2_RRA: case F2_SWPB: case F2_SXT: {
            for (unsigned k = 0; k < n; ++k) {
                const uint32_t v = valor(o, t);
                uint32_t r = 0;
                unsigned t_esc = t;
                if (in.op == F2_RRC) {
                    const uint32_t cin = in.zc ? 0u : ((sr() & SR_C) ? 1u : 0u);
                    r = (v >> 1) | (cin ? msb(t) : 0u);
                    banderas(r, t, int(v & 1u), 0);
                } else if (in.op == F2_RRA) {
                    r = (v >> 1) | (v & msb(t));
                    banderas(r, t, int(v & 1u), 0);
                } else if (in.op == F2_SWPB) {
                    // .A: 19:16 se quedan; .W y sin extensión: palabra [SLAU391F SWPBX]
                    const uint32_t w = v & 0xFFFFu;
                    r = (v & 0xF0000u) | ((w >> 8) | ((w & 0xFFu) << 8));
                } else {                                       // SXT
                    // Registro (CPUX): el bit 7 a 19:8, con o sin .A/.W; memoria:
                    // a 15:8 (o 19:8 con .A) [SLAU391F SXT y SXTX]
                    const uint32_t b = v & 0xFFu;
                    const bool neg = (b & 0x80u) != 0;
                    unsigned tf = t;
                    if (o.tipo == Op::Reg && x_) { r = neg ? (b | 0xFFF00u) : b; tf = t == 4 ? 4u : 2u; t_esc = 4; }
                    else r = neg ? (b | (mascara(t) & ~0xFFu)) : b;
                    banderas(r, tf, (r & mascara(tf)) != 0, 0);
                }
                guarda(o, r, t_esc);
            }
            if (in.ext) {
                if (mo == MO_RN) return 1 + n;
                return t == 4 ? cic_.x2_rot_a[mo] : cic_.x2_rot_bw[mo];
            }
            return cic_.f2_rot[mo];
        }
        case F2_PUSH: {
            // CPU4: PUSH #4 / PUSH #8 por el generador de constantes (R2, As
            // = 10 u 11) da un valor equivocado en la CPU de 2xx; los
            // compiladores lo evitan. AVISA una vez y empuja lo que dice la
            // guía [SLAZ440K CPU4; declarado: TI no dice qué empuja]
            if (in.dst == 2 && in.as >= 2) errata.avisa(m430::ERR_CPU4, "CPU4 (PUSH #4 o #8 por el CG)", e.r[0]);
            const uint32_t v = valor(o, t);
            push(v, t);
            if (in.ext) {
                unsigned c = t == 4 ? cic_.x2_push_a[mo] : cic_.x2_push_bw[mo];
                if (mo == MO_IDX && in.dst == 1) ++c;
                return c;
            }
            unsigned c = cic_.f2_push[mo];
            if (cic_.f2_idx_sp_mas1 && mo == MO_IDX && in.dst == 1) ++c;
            return c;
        }
        case F2_CALL: {
            const uint32_t dst = valor(o, 2);
            unsigned c = cic_.f2_call[mo];
            if (cic_.f2_idx_sp_mas1 && mo == MO_IDX && in.dst == 1) ++c;
            if (llamada_lab && llamada_lab(dst)) return c;
            push(e.r[0] & 0xFFFFu, 2);
            e.r[0] = dst & 0xFFFEu;                            // CALL: 64 KB [SLAU391F §1.3.1]
            return c;
        }
        default: return 1;
        }
    }

    unsigned salto(const M430Ins& in) {
        const uint16_t s = sr();
        bool si = false;
        const bool n = s & SR_N, z = s & SR_Z, c = s & SR_C, v = s & SR_V;
        switch (in.op) {
        case 0: si = !z; break;
        case 1: si = z; break;
        case 2: si = !c; break;
        case 3: si = c; break;
        case 4: si = n; break;
        case 5: si = (n == v); break;
        case 6: si = (n != v); break;
        default: si = true; break;
        }
        if (si) e.r[0] = (e.r[0] + uint32_t(int32_t(in.salto) * 2)) & amask_;
        return cic_.salto;
    }

    unsigned reti() {
        const uint16_t s = uint16_t(pop(2));
        uint32_t pcn = pop(2);
        if (x_) pcn |= uint32_t(s & 0xF000u) << 4;
        pon_sr(uint16_t(s & 0x0FFFu));
        e.r[0] = pcn & amask_ & ~1u;
        if (irq_) irq_->al_reti();
        return cic_.reti;
    }

    // --- CPUX -----------------------------------------------------------------------
    uint32_t suma20(uint32_t d, uint32_t s, uint32_t c) { return suma(d, s, c, 4); }

    unsigned direccion(const M430Ins& in) {
        const unsigned rs = in.src, rd = in.dst;
        const bool a_pc = rd == 0;
        auto pon = [&](uint32_t v) { escribe_reg(rd, v & 0xFFFFFu, 4); };
        switch (in.op) {
        case 0x0: { const uint32_t v = lee(e.r[rs], 4, M430Acceso::Lectura); pon(v);
                    return cic_.mova[MO_IND][a_pc ? MD_PC : MD_RM]; }
        case 0x1: { const uint32_t a = e.r[rs]; e.r[rs] = (e.r[rs] + 4) & 0xFFFFFu;
                    const uint32_t v = lee(a, 4, M430Acceso::Lectura); pon(v);
                    return cic_.mova[MO_INC][a_pc ? MD_PC : MD_RM]; }
        case 0x2: { const uint32_t a = (uint32_t(rs) << 16) | busca(); pon(lee(a, 4, M430Acceso::Lectura));
                    return cic_.mova[MO_ABS][a_pc ? MD_PC : MD_RM]; }
        case 0x3: { const uint32_t base = e.r[rs]; const uint32_t x = busca();
                    const uint32_t a = (base + sext16(x)) & 0xFFFFFu;
                    pon(lee(a, 4, M430Acceso::Lectura));
                    return cic_.mova[rs == 0 ? MO_SIM : MO_IDX][a_pc ? MD_PC : MD_RM]; }
        case 0x6: { const uint32_t a = (uint32_t(rd) << 16) | busca();
                    escribe(a, 4, e.r[rs] & 0xFFFFFu, M430Acceso::Escritura);
                    return cic_.mova[MO_RN][MD_ABS]; }
        case 0x7: { const uint32_t base = e.r[rd]; const uint32_t x = busca();
                    const uint32_t a = (base + sext16(x)) & 0xFFFFFu;
                    escribe(a, 4, e.r[rs] & 0xFFFFFu, M430Acceso::Escritura);
                    return cic_.mova[MO_RN][rd == 0 ? MD_SIM : MD_IDX]; }
        case 0x8: { const uint32_t k = (uint32_t(rs) << 16) | busca(); pon(k);
                    return cic_.mova[MO_INM][a_pc ? MD_PC : MD_RM]; }
        case 0x9: case 0xA: case 0xB: {
            const uint32_t k = (uint32_t(rs) << 16) | busca();
            alu_a(in.op - 0x8, k, rd);
            return cic_.cmpa[1][a_pc ? 1 : 0];
        }
        case 0xC: { pon(e.r[rs]); return cic_.mova[MO_RN][a_pc ? MD_PC : MD_RM]; }
        case 0xD: case 0xE: case 0xF:
            // R3 de origen es el generador de constantes: #2 (INCDA y DECDA
            // de 2 bytes: CCS abre y cierra marcos con 0x03F1/0x03E1, «DECDA
            // SP»/«INCDA SP», en el simpleUsbBackchannel de TI) [UG5 tabla 6-15]
            alu_a(in.op - 0xC, rs == 3 ? 2u : (e.r[rs] & 0xFFFFFu), rd);
            return cic_.cmpa[0][a_pc ? 1 : 0];
        default: return 1;
        }
    }
    // 1 CMPA, 2 ADDA, 3 SUBA (20 bits, con banderas)
    void alu_a(unsigned op, uint32_t s, unsigned rd) {
        const uint32_t d = e.r[rd] & 0xFFFFFu;
        if (op == 2) { escribe_reg(rd, suma20(d, s, 0), 4); return; }
        const uint32_t r = suma20(d, ~s & 0xFFFFFu, 1);
        if (op == 3) escribe_reg(rd, r, 4);
    }

    unsigned rotm(const M430Ins& in) {
        const unsigned t = in.w16 ? 2u : 4u;
        uint32_t v = e.r[in.dst] & mascara(t);
        for (unsigned k = 0; k < in.n; ++k) {
            uint32_t r = 0;
            int c = 0;
            switch (in.op) {
            case 0: c = int(v & 1u); r = (v >> 1) | ((sr() & SR_C) ? msb(t) : 0u); break;      // RRCM
            case 1: c = int(v & 1u); r = (v >> 1) | (v & msb(t)); break;                        // RRAM
            case 2: c = (v & msb(t)) ? 1 : 0; r = (v << 1) & mascara(t); break;                 // RLAM
            default: c = int(v & 1u); r = v >> 1; break;                                        // RRUM
            }
            banderas(r, t, c, in.op == 2 ? -1 : 0);      // RLAM: V indefinido; se deja [SLAU391F RLAM]
            v = r;
        }
        escribe_reg(in.dst, v, t);
        return in.n;
    }

    unsigned pushm(const M430Ins& in) {
        const unsigned t = in.w16 ? 2u : 4u;
        for (unsigned k = 0; k < in.n; ++k) push(e.r[(in.dst - k) & 15] & mascara(t), t);
        return 2 + (t == 4 ? 2 * in.n : in.n);
    }
    unsigned popm(const M430Ins& in) {
        const unsigned t = in.w16 ? 2u : 4u;
        for (unsigned k = 0; k < in.n; ++k) {
            const unsigned r = (in.dst + k) & 15;
            if (r == 2 || r == 0) errata.avisa(m430::ERR_CPU21, "CPU21 (POPM con el SR o el PC)", e.r[0]);
            escribe_reg(r, pop(t), t);
        }
        return 2 + (t == 4 ? 2 * in.n : in.n);
    }

    unsigned calla(const M430Ins& in) {
        uint32_t dst = 0;
        unsigned mo = MO_RN;
        const unsigned r = in.dst;
        switch (in.op) {
        case 0x4: dst = e.r[r]; mo = MO_RN; break;
        case 0x5: { const uint32_t x = busca(); dst = lee((e.r[r] + sext16(x)) & 0xFFFFFu, 4, M430Acceso::Lectura);
                    mo = MO_IDX; break; }
        case 0x6: dst = lee(e.r[r], 4, M430Acceso::Lectura); mo = MO_IND; break;
        case 0x7: { const uint32_t a = e.r[r]; e.r[r] = (e.r[r] + 4) & 0xFFFFFu;
                    dst = lee(a, 4, M430Acceso::Lectura); mo = MO_INC; break; }
        case 0x8: { const uint32_t a = (uint32_t(r) << 16) | busca(); dst = lee(a, 4, M430Acceso::Lectura);
                    mo = MO_ABS; break; }
        case 0x9: { const uint32_t base = e.r[0]; const uint32_t x = busca();
                    dst = lee((base + ((uint32_t(r) << 16) | x)) & 0xFFFFFu, 4, M430Acceso::Lectura);
                    mo = MO_SIM; break; }
        case 0xB: dst = (uint32_t(r) << 16) | busca(); mo = MO_INM; break;
        default: return 1;
        }
        unsigned c = cic_.calla[mo];
        if (mo == MO_IDX && r == 1) ++c;
        if (llamada_lab && llamada_lab(dst & 0xFFFFFu)) return c;
        push(e.r[0] & 0xFFFFFu, 4);
        e.r[0] = dst & 0xFFFFEu;
        return c;
    }
};

} // namespace stm32
#endif // STM32_M430_CPU_M430_EXEC_H
