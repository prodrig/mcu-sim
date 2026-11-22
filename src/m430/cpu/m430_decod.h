// =============================================================================
// m430_decod.h — Qué instrucción es una palabra de MSP430 (con la de
// extensión delante, si la hay)
//
// M1 de doc/analisis_msp430.md. Los formatos [UG2 §3.4.3 y cap. 4; SLAU391F
// §1.5 y la tabla 1-20], con las codificaciones de la CPUX contrastadas con
// lo que ensambla y desensambla msp430-elf-as/objdump (binutils 2.34 del
// msp430-gcc 9.3.1):
//
//   0000 ssss oooo dddd   instrucciones de direcciones (CPUX): MOVA, CMPA,
//                         ADDA, SUBA (oooo = el modo, tabla 1-16)
//   0000 nnoo 010w dddd   RRCM/RRAM/RLAM/RRUM (oo), n-1 (nn), w = 1: .W
//   0001 00oo obaa dddd   formato II: RRC, SWPB, RRA, SXT, PUSH, CALL, RETI
//   0001 0011 mmmm xxxx   CALLA (CPUX; 0x1300 es RETI)
//   0001 010w nnnn dddd   PUSHM (w = 1: .W), dddd = el más alto
//   0001 011w nnnn dddd   POPM, dddd = el más bajo
//   0001 1...             palabra de extensión (CPUX)
//   001c ccoo oooo oooo   saltos
//   oooo ssss abaa dddd   formato I (oooo de 4 a 15)
// =============================================================================
#ifndef STM32_M430_CPU_M430_DECOD_H
#define STM32_M430_CPU_M430_DECOD_H

#include <cstdint>

namespace stm32 {

enum class M430Clase : uint8_t {
    F1, F2, Salto, Direccion, RotM, PushM, PopM, Calla, Reti, Ilegal
};

// Formato I
enum : uint8_t { F1_MOV = 4, F1_ADD, F1_ADDC, F1_SUBC, F1_SUB, F1_CMP, F1_DADD, F1_BIT,
                 F1_BIC, F1_BIS, F1_XOR, F1_AND };
// Formato II
enum : uint8_t { F2_RRC = 0, F2_SWPB, F2_RRA, F2_SXT, F2_PUSH, F2_CALL };

struct M430Ins {
    M430Clase clase = M430Clase::Ilegal;
    uint8_t op = 0;          // F1: 4..15; F2: 0..5; salto: la condición; dirección: bits 7-4; RotM: 0..3
    uint8_t src = 0, dst = 0, as = 0, ad = 0;
    bool    bw = false;      // .B
    int16_t salto = 0;       // en palabras
    uint8_t n = 1;           // RotM, PushM, PopM: cuántos
    bool    w16 = false;     // RotM, PushM, PopM: .W (si no, .A)
    // La palabra de extensión
    bool    ext = false, al = false, zc = false, rep_reg = false;
    uint8_t rep = 0;         // registro: n-1 o el registro
    uint32_t src_hi = 0, dst_hi = 0;
    bool    ext_reg = false; // la extensión es la de modo registro
    // El tamaño del dato: 1, 2 o 4 (20 bits, .A)
    unsigned tam() const {
        if (!ext) return bw ? 1u : 2u;
        if (al) return bw ? 1u : 2u;
        return 4u;           // A/L = 0: .A (con B/W = 1; SWPBX.A y SXTX.A, con 0)
    }
};

// ¿Es una palabra de extensión?
inline bool m430_es_ext(uint16_t w) { return (w & 0xF800u) == 0x1800u; }

// Decodifica `w` (el código de operación); si `ext` es true, `x` es la palabra
// de extensión que lo precede. `cpux`: si la CPU tiene las instrucciones de 20 bits.
inline M430Ins m430_decodifica(uint16_t w, bool cpux, bool ext = false, uint16_t x = 0) {
    M430Ins i;
    const unsigned g = w >> 12;
    if (g >= 4) {                                         // formato I
        i.clase = M430Clase::F1;
        i.op = uint8_t(g);
        i.src = uint8_t((w >> 8) & 0xF);
        i.ad = uint8_t((w >> 7) & 1);
        i.bw = (w >> 6) & 1;
        i.as = uint8_t((w >> 4) & 3);
        i.dst = uint8_t(w & 0xF);
    } else if (g >= 2) {                                  // saltos
        i.clase = M430Clase::Salto;
        i.op = uint8_t((w >> 10) & 7);
        int v = w & 0x3FF;
        if (v & 0x200) v -= 0x400;
        i.salto = int16_t(v);
    } else if (g == 1) {
        if ((w & 0xFC00u) == 0x1000u) {                   // formato II, RETI, CALLA
            const unsigned o = (w >> 7) & 7;
            if (o == 6) {
                if (w == 0x1300u) { i.clase = M430Clase::Reti; }
                else if (cpux && (w & 0x00C0u) != 0) {    // 0x1340-0x13FF
                    i.clase = M430Clase::Calla;
                    i.op = uint8_t((w >> 4) & 0xF);
                    i.dst = uint8_t(w & 0xF);
                }
            } else if (o == 7) {
                // 0x1380-0x13FF: CALLA (CPUX)
                if (cpux) {
                    i.clase = M430Clase::Calla;
                    i.op = uint8_t((w >> 4) & 0xF);
                    i.dst = uint8_t(w & 0xF);
                }
            } else {
                i.clase = M430Clase::F2;
                i.op = uint8_t(o);
                i.bw = (w >> 6) & 1;
                i.as = uint8_t((w >> 4) & 3);
                i.dst = uint8_t(w & 0xF);
                i.src = i.dst;
                // SWPB, SXT y CALL no tienen .B
                if (i.bw && (o == F2_SWPB || o == F2_SXT || o == F2_CALL)) i.clase = M430Clase::Ilegal;
            }
        } else if (cpux && (w & 0xFC00u) == 0x1400u) {    // PUSHM / POPM
            i.clase = (w & 0x0200u) ? M430Clase::PopM : M430Clase::PushM;
            i.w16 = (w >> 8) & 1;
            i.n = uint8_t(((w >> 4) & 0xF) + 1);
            i.dst = uint8_t(w & 0xF);
        }
    } else if (cpux) {                                    // 0x0xxx: direcciones y RxxM
        if ((w & 0x00E0u) == 0x0040u) {
            i.clase = M430Clase::RotM;
            i.op = uint8_t((w >> 8) & 3);
            i.n = uint8_t(((w >> 10) & 3) + 1);
            i.w16 = (w >> 4) & 1;
            i.dst = uint8_t(w & 0xF);
        } else {
            i.clase = M430Clase::Direccion;
            i.op = uint8_t((w >> 4) & 0xF);
            i.src = uint8_t((w >> 8) & 0xF);
            i.dst = uint8_t(w & 0xF);
        }
    }
    if (ext) {
        // Solo los formatos I y II llevan extensión (y no CALL ni RETI)
        if (i.clase != M430Clase::F1 && !(i.clase == M430Clase::F2 && i.op != F2_CALL)) {
            i.clase = M430Clase::Ilegal;
            return i;
        }
        i.ext = true;
        i.al = (x >> 6) & 1;
        // ¿Extensión de modo registro? [SLAU391F tablas 1-11 y 1-12]
        i.ext_reg = (i.clase == M430Clase::F1) ? (i.as == 0 && i.ad == 0) : (i.as == 0);
        if (i.ext_reg) {
            i.zc = (x >> 8) & 1;
            i.rep_reg = (x >> 7) & 1;
            i.rep = uint8_t(x & 0xF);
        } else {
            i.src_hi = (x >> 7) & 0xF;
            i.dst_hi = x & 0xF;
        }
        // A/L = 0 y B/W = 0: reservado, salvo SWPBX.A y SXTX.A
        if (!i.al && !i.bw && !(i.clase == M430Clase::F2 && (i.op == F2_SWPB || i.op == F2_SXT)))
            i.clase = M430Clase::Ilegal;
        // SWPBX y SXTX: .A con B/W = 0
        if (i.clase == M430Clase::F2 && (i.op == F2_SWPB || i.op == F2_SXT) && i.bw)
            i.clase = M430Clase::Ilegal;
    }
    return i;
}

// Cuántas palabras de índice o inmediato siguen al código (sin contar la de
// extensión ni el propio código)
inline unsigned m430_palabras_op(const M430Ins& i) {
    auto src_pal = [](unsigned as, unsigned r) -> unsigned {
        if (as == 1) return r == 3 ? 0u : 1u;            // X(Rn), EDE, &EDE; #1 de CG2
        if (as == 3) return r == 0 ? 1u : 0u;            // #N
        return 0u;
    };
    switch (i.clase) {
    case M430Clase::F1: return src_pal(i.as, i.src) + (i.ad ? 1u : 0u);
    case M430Clase::F2: return src_pal(i.as, i.dst);
    case M430Clase::Direccion: {
        const unsigned o = i.op;
        return (o == 2 || o == 3 || o == 6 || o == 7 || (o >= 8 && o <= 11)) ? 1u : 0u;
    }
    case M430Clase::Calla: {
        const unsigned m = i.op;
        return (m == 5 || m == 8 || m == 9 || m == 11) ? 1u : 0u;
    }
    default: return 0;
    }
}

} // namespace stm32
#endif // STM32_M430_CPU_M430_DECOD_H
