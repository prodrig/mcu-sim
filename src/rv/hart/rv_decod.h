// =============================================================================
// rv_decod.h — Campos de una instrucción RISC-V y la expansión de las comprimidas
//
// E1 de doc/analisis_esp32_riscv.md, §3.3. Las instrucciones de 16 bits (C)
// no tienen semántica propia: cada una ES una de 32 bits con los campos
// recortados [Unprivileged ISA, cap. 16]. Así que aquí se EXPANDEN a su forma
// de 32 bits y el ejecutor solo conoce esas: la C no duplica nada, y las
// extensiones de comprimidas que vengan (Zcb, Zcmp...) son más expansiones.
//
// Lo que no se puede expandir -los códigos reservados, los que necesitan F o
// D cuando no están, los desplazamientos con shamt[5] en RV32- devuelve
// `false`, y el núcleo lo trata como instrucción ilegal.
//
// Sin SystemC: se prueba con vectores (verif/rv/vectores_c.inc), sacados del
// ensamblador de GNU con y sin `.option rvc` [verif/rv/gen_vectores_c.py].
// =============================================================================
#ifndef STM32_RV_HART_RV_DECOD_H
#define STM32_RV_HART_RV_DECOD_H

#include <cstdint>

namespace stm32 {
namespace rvd {

// --- Campos de una instrucción de 32 bits ------------------------------------
inline unsigned opcode(uint32_t i) { return i & 0x7Fu; }
inline unsigned rd(uint32_t i)     { return (i >> 7) & 0x1Fu; }
inline unsigned rs1(uint32_t i)    { return (i >> 15) & 0x1Fu; }
inline unsigned rs2(uint32_t i)    { return (i >> 20) & 0x1Fu; }
inline unsigned funct3(uint32_t i) { return (i >> 12) & 0x7u; }
inline unsigned funct7(uint32_t i) { return (i >> 25) & 0x7Fu; }
inline uint32_t sext(uint32_t v, unsigned bits) {
    const uint32_t m = 1u << (bits - 1);
    return (v ^ m) - m;
}
inline uint32_t imm_i(uint32_t i) { return sext(i >> 20, 12); }
inline uint32_t imm_s(uint32_t i) { return sext(((i >> 25) << 5) | ((i >> 7) & 0x1Fu), 12); }
inline uint32_t imm_b(uint32_t i) {
    return sext(((i >> 31) << 12) | (((i >> 7) & 1u) << 11) |
                (((i >> 25) & 0x3Fu) << 5) | (((i >> 8) & 0xFu) << 1), 13);
}
inline uint32_t imm_u(uint32_t i) { return i & 0xFFFFF000u; }
inline uint32_t imm_j(uint32_t i) {
    return sext(((i >> 31) << 20) | (((i >> 12) & 0xFFu) << 12) |
                (((i >> 20) & 1u) << 11) | (((i >> 21) & 0x3FFu) << 1), 21);
}

// --- Codificadores, para la expansión -----------------------------------------
inline uint32_t tipo_r(unsigned f7, unsigned rs2_, unsigned rs1_, unsigned f3, unsigned rd_,
                       unsigned op) {
    return (uint32_t(f7) << 25) | (uint32_t(rs2_) << 20) | (uint32_t(rs1_) << 15) |
           (uint32_t(f3) << 12) | (uint32_t(rd_) << 7) | op;
}
inline uint32_t tipo_i(uint32_t imm, unsigned rs1_, unsigned f3, unsigned rd_, unsigned op) {
    return ((imm & 0xFFFu) << 20) | (uint32_t(rs1_) << 15) | (uint32_t(f3) << 12) |
           (uint32_t(rd_) << 7) | op;
}
inline uint32_t tipo_s(uint32_t imm, unsigned rs2_, unsigned rs1_, unsigned f3, unsigned op) {
    return (((imm >> 5) & 0x7Fu) << 25) | (uint32_t(rs2_) << 20) | (uint32_t(rs1_) << 15) |
           (uint32_t(f3) << 12) | ((imm & 0x1Fu) << 7) | op;
}
inline uint32_t tipo_b(uint32_t imm, unsigned rs2_, unsigned rs1_, unsigned f3, unsigned op) {
    return (((imm >> 12) & 1u) << 31) | (((imm >> 5) & 0x3Fu) << 25) | (uint32_t(rs2_) << 20) |
           (uint32_t(rs1_) << 15) | (uint32_t(f3) << 12) | (((imm >> 1) & 0xFu) << 8) |
           (((imm >> 11) & 1u) << 7) | op;
}
inline uint32_t tipo_u(uint32_t imm, unsigned rd_, unsigned op) {
    return (imm & 0xFFFFF000u) | (uint32_t(rd_) << 7) | op;
}
inline uint32_t tipo_j(uint32_t imm, unsigned rd_, unsigned op) {
    return (((imm >> 20) & 1u) << 31) | (((imm >> 1) & 0x3FFu) << 21) |
           (((imm >> 11) & 1u) << 20) | (((imm >> 12) & 0xFFu) << 12) |
           (uint32_t(rd_) << 7) | op;
}

// Los códigos de operación de la ISA base
enum : unsigned {
    OP_LOAD = 0x03, OP_MISC_MEM = 0x0F, OP_IMM = 0x13, OP_AUIPC = 0x17,
    OP_STORE = 0x23, OP_AMO = 0x2F, OP_OP = 0x33, OP_LUI = 0x37,
    OP_BRANCH = 0x63, OP_JALR = 0x67, OP_JAL = 0x6F, OP_SYSTEM = 0x73,
    OP_CUSTOM0 = 0x0B, OP_CUSTOM1 = 0x2B, OP_CUSTOM2 = 0x5B, OP_CUSTOM3 = 0x7B
};

// ¿Es de 16 bits? (los dos bits de abajo distintos de 11)
inline bool es_comprimida(uint32_t parcela) { return (parcela & 3u) != 3u; }

// -----------------------------------------------------------------------------
// La expansión de RV32C. `f` dice si hay F (C.FLW, C.FSW, C.FLWSP, C.FSWSP)
// y `d` si hay D (C.FLD...). Devuelve false si la codificación es reservada,
// no existe en RV32 o necesita una extensión que no está.
// -----------------------------------------------------------------------------
inline bool expande_c(uint16_t c, uint32_t& out, bool f = false, bool d = false) {
    const unsigned op = c & 3u;
    const unsigned f3 = (c >> 13) & 7u;
    const unsigned rdp = 8u + ((c >> 2) & 7u);        // rd' / rs2'
    const unsigned rs1p = 8u + ((c >> 7) & 7u);       // rs1' / rd'
    const unsigned rdf = (c >> 7) & 0x1Fu;            // rd / rs1 completo
    const unsigned rs2f = (c >> 2) & 0x1Fu;           // rs2 completo
    auto b = [&](unsigned n) { return (uint32_t(c) >> n) & 1u; };
    // inmediato de 6 bits con signo: [12|6:2]
    const uint32_t imm6 = sext((b(12) << 5) | ((c >> 2) & 0x1Fu), 6);

    if (c == 0) return false;                         // todo ceros: ilegal
    switch (op) {
    case 0:
        switch (f3) {
        case 0: {                                     // C.ADDI4SPN
            const uint32_t nz = (((c >> 11) & 3u) << 4) | (((c >> 7) & 0xFu) << 6) |
                                (b(6) << 2) | (b(5) << 3);
            if (nz == 0) return false;
            out = tipo_i(nz, 2, 0, rdp, OP_IMM);
            return true;
        }
        case 1:                                       // C.FLD
            if (!d) return false;
            out = tipo_i((((c >> 10) & 7u) << 3) | (((c >> 5) & 3u) << 6), rs1p, 3, rdp, 0x07);
            return true;
        case 2: {                                     // C.LW
            const uint32_t u = (((c >> 10) & 7u) << 3) | (b(6) << 2) | (b(5) << 6);
            out = tipo_i(u, rs1p, 2, rdp, OP_LOAD);
            return true;
        }
        case 3: {                                     // C.FLW (RV32)
            if (!f) return false;
            const uint32_t u = (((c >> 10) & 7u) << 3) | (b(6) << 2) | (b(5) << 6);
            out = tipo_i(u, rs1p, 2, rdp, 0x07);
            return true;
        }
        case 5:                                       // C.FSD
            if (!d) return false;
            out = tipo_s((((c >> 10) & 7u) << 3) | (((c >> 5) & 3u) << 6), rdp, rs1p, 3, 0x27);
            return true;
        case 6: {                                     // C.SW
            const uint32_t u = (((c >> 10) & 7u) << 3) | (b(6) << 2) | (b(5) << 6);
            out = tipo_s(u, rdp, rs1p, 2, OP_STORE);
            return true;
        }
        case 7: {                                     // C.FSW (RV32)
            if (!f) return false;
            const uint32_t u = (((c >> 10) & 7u) << 3) | (b(6) << 2) | (b(5) << 6);
            out = tipo_s(u, rdp, rs1p, 2, 0x27);
            return true;
        }
        default: return false;                        // 4: reservado (Zcb)
        }
    case 1:
        switch (f3) {
        case 0:                                       // C.NOP / C.ADDI
            out = tipo_i(imm6, rdf, 0, rdf, OP_IMM);
            return true;
        case 1: case 5: {                             // C.JAL (RV32) / C.J
            const uint32_t j = sext((b(12) << 11) | (b(11) << 4) | (((c >> 9) & 3u) << 8) |
                                    (b(8) << 10) | (b(7) << 6) | (b(6) << 7) |
                                    (((c >> 3) & 7u) << 1) | (b(2) << 5), 12);
            out = tipo_j(j, f3 == 1 ? 1u : 0u, OP_JAL);
            return true;
        }
        case 2:                                       // C.LI
            out = tipo_i(imm6, 0, 0, rdf, OP_IMM);
            return true;
        case 3:
            if (rdf == 2) {                           // C.ADDI16SP
                const uint32_t nz = sext((b(12) << 9) | (b(6) << 4) | (b(5) << 6) |
                                         (((c >> 3) & 3u) << 7) | (b(2) << 5), 10);
                if (nz == 0) return false;
                out = tipo_i(nz, 2, 0, 2, OP_IMM);
                return true;
            } else {                                  // C.LUI
                const uint32_t nz = sext((b(12) << 17) | (((c >> 2) & 0x1Fu) << 12), 18);
                if (nz == 0) return false;
                out = tipo_u(nz, rdf, OP_LUI);
                return true;
            }
        case 4: {
            const unsigned f2 = (c >> 10) & 3u;
            if (f2 == 0 || f2 == 1) {                 // C.SRLI / C.SRAI
                if (b(12)) return false;              // shamt[5] = 1: no en RV32
                const unsigned sh = (c >> 2) & 0x1Fu;
                out = tipo_i(sh | (f2 == 1 ? 0x400u : 0u), rs1p, 5, rs1p, OP_IMM);
                return true;
            }
            if (f2 == 2) {                            // C.ANDI
                out = tipo_i(imm6, rs1p, 7, rs1p, OP_IMM);
                return true;
            }
            if (b(12)) return false;                  // SUBW/ADDW: RV64
            switch ((c >> 5) & 3u) {
            case 0: out = tipo_r(0x20, rdp, rs1p, 0, rs1p, OP_OP); return true;   // C.SUB
            case 1: out = tipo_r(0x00, rdp, rs1p, 4, rs1p, OP_OP); return true;   // C.XOR
            case 2: out = tipo_r(0x00, rdp, rs1p, 6, rs1p, OP_OP); return true;   // C.OR
            default: out = tipo_r(0x00, rdp, rs1p, 7, rs1p, OP_OP); return true;  // C.AND
            }
        }
        case 6: case 7: {                             // C.BEQZ / C.BNEZ
            const uint32_t o = sext((b(12) << 8) | (((c >> 10) & 3u) << 3) |
                                    (((c >> 5) & 3u) << 6) | (((c >> 3) & 3u) << 1) |
                                    (b(2) << 5), 9);
            out = tipo_b(o, 0, rs1p, f3 == 6 ? 0u : 1u, OP_BRANCH);
            return true;
        }
        }
        return false;
    case 2:
        switch (f3) {
        case 0:                                       // C.SLLI
            if (b(12)) return false;
            out = tipo_i((c >> 2) & 0x1Fu, rdf, 1, rdf, OP_IMM);
            return true;
        case 1:                                       // C.FLDSP
            if (!d) return false;
            out = tipo_i((b(12) << 5) | (((c >> 5) & 3u) << 3) | (((c >> 2) & 7u) << 6),
                         2, 3, rdf, 0x07);
            return true;
        case 2: {                                     // C.LWSP
            if (rdf == 0) return false;
            const uint32_t u = (b(12) << 5) | (((c >> 4) & 7u) << 2) | (((c >> 2) & 3u) << 6);
            out = tipo_i(u, 2, 2, rdf, OP_LOAD);
            return true;
        }
        case 3: {                                     // C.FLWSP (RV32)
            if (!f) return false;
            const uint32_t u = (b(12) << 5) | (((c >> 4) & 7u) << 2) | (((c >> 2) & 3u) << 6);
            out = tipo_i(u, 2, 2, rdf, 0x07);
            return true;
        }
        case 4:
            if (!b(12)) {
                if (rs2f == 0) {                      // C.JR
                    if (rdf == 0) return false;
                    out = tipo_i(0, rdf, 0, 0, OP_JALR);
                } else {                              // C.MV
                    out = tipo_r(0, rs2f, 0, 0, rdf, OP_OP);
                }
                return true;
            }
            if (rs2f == 0) {
                if (rdf == 0) { out = 0x00100073u; return true; }   // C.EBREAK
                out = tipo_i(0, rdf, 0, 1, OP_JALR);                // C.JALR
                return true;
            }
            out = tipo_r(0, rs2f, rdf, 0, rdf, OP_OP);              // C.ADD
            return true;
        case 5:                                       // C.FSDSP
            if (!d) return false;
            out = tipo_s((((c >> 10) & 7u) << 3) | (((c >> 7) & 7u) << 6), rs2f, 2, 3, 0x27);
            return true;
        case 6: {                                     // C.SWSP
            const uint32_t u = (((c >> 9) & 0xFu) << 2) | (((c >> 7) & 3u) << 6);
            out = tipo_s(u, rs2f, 2, 2, OP_STORE);
            return true;
        }
        case 7: {                                     // C.FSWSP (RV32)
            if (!f) return false;
            const uint32_t u = (((c >> 9) & 0xFu) << 2) | (((c >> 7) & 3u) << 6);
            out = tipo_s(u, rs2f, 2, 2, 0x27);
            return true;
        }
        }
        return false;
    default:
        return false;                                 // op = 3: no es de 16 bits
    }
}

} // namespace rvd
} // namespace stm32
#endif // STM32_RV_HART_RV_DECOD_H
