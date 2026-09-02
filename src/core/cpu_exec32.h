// =============================================================================
// cpu_exec32.h — Ejecución de las instrucciones Thumb-2 de 32 bits [II, §2-§5]
//
// Incluido desde cpu.h. La estructura sigue los grupos del documento:
//   hw1[15:11] = 11101 -> §2 (múltiples, exclusivos/dual, DP registro desplazado)
//   hw1[15:11] = 11110 -> §3 (inmediatos, saltos, MSR/MRS, hints, barreras)
//   hw1[15:11] = 11111 -> §4 (carga/almacenamiento simple, DP registro, MUL/DIV)
//   Coprocesador 10/11 -> §5 (FPv4-SP)
// =============================================================================
#ifndef STM32_CORE_CPU_EXEC32_H
#define STM32_CORE_CPU_EXEC32_H

namespace stm32 {

// ---------------------------------------------------------------------------
// Operación común de las tablas §2.3 y §3.1
// ---------------------------------------------------------------------------
inline void Cpu::dp_op(unsigned op, unsigned rd, unsigned rn, uint32_t operand,
                       bool carry_shift, bool S) {
    const uint32_t a = rd_(rn);
    switch (op) {
        case 0x0: {                                   // AND / TST
            const uint32_t r = a & operand;
            if (rd == 15 && S) { alu_flags_nzc(r, carry_shift, true); }
            else { set_reg(rd, r); alu_flags_nzc(r, carry_shift, S); }
            break;
        }
        case 0x1: {                                   // BIC
            const uint32_t r = a & ~operand;
            set_reg(rd, r); alu_flags_nzc(r, carry_shift, S);
            break;
        }
        case 0x2: {                                   // ORR / MOV (Rn = 1111)
            const uint32_t r = (rn == 15) ? operand : (a | operand);
            set_reg(rd, r); alu_flags_nzc(r, carry_shift, S);
            break;
        }
        case 0x3: {                                   // ORN / MVN (Rn = 1111)
            const uint32_t r = (rn == 15) ? ~operand : (a | ~operand);
            set_reg(rd, r); alu_flags_nzc(r, carry_shift, S);
            break;
        }
        case 0x4: {                                   // EOR / TEQ
            const uint32_t r = a ^ operand;
            if (rd == 15 && S) { alu_flags_nzc(r, carry_shift, true); }
            else { set_reg(rd, r); alu_flags_nzc(r, carry_shift, S); }
            break;
        }
        case 0x8: {                                   // ADD / CMN
            const AddResult x = add_with_carry(a, operand, false);
            if (rd == 15 && S) alu_flags_nzcv(x.result, x.carry, x.overflow, true);
            else { set_reg(rd, x.result); alu_flags_nzcv(x.result, x.carry, x.overflow, S); }
            break;
        }
        case 0xA: {                                   // ADC
            const AddResult x = add_with_carry(a, operand, reg.c());
            set_reg(rd, x.result); alu_flags_nzcv(x.result, x.carry, x.overflow, S);
            break;
        }
        case 0xB: {                                   // SBC
            const AddResult x = add_with_carry(a, ~operand, reg.c());
            set_reg(rd, x.result); alu_flags_nzcv(x.result, x.carry, x.overflow, S);
            break;
        }
        case 0xD: {                                   // SUB / CMP
            const AddResult x = add_with_carry(a, ~operand, true);
            if (rd == 15 && S) alu_flags_nzcv(x.result, x.carry, x.overflow, true);
            else { set_reg(rd, x.result); alu_flags_nzcv(x.result, x.carry, x.overflow, S); }
            break;
        }
        case 0xE: {                                   // RSB
            const AddResult x = add_with_carry(~a, operand, true);
            set_reg(rd, x.result); alu_flags_nzcv(x.result, x.carry, x.overflow, S);
            break;
        }
        default: undefined(); break;
    }
}

// ---------------------------------------------------------------------------
inline void Cpu::exec_32(uint32_t hw1, uint32_t hw2) {
    // Coprocesador 10/11: espacio de la FPU [II, §5]. hw1 en 0xEC00-0xEFFF y
    // hw2[11:8] = 1010 (cp10) o 1011 (cp11).
    if ((hw1 & 0xFC00u) == 0xEC00u && (hw2 & 0x0E00u) == 0x0A00u) {
        if (exec_32_fp(hw1, hw2)) return;
    }
    switch ((hw1 >> 11) & 0x1Fu) {
        case 0x1D:                                     // 11101
            if ((hw1 & 0x0E00u) == 0x0A00u)            // 1110 101x: DP reg. desplazado
                exec_32_dp_reg_shift(hw1, hw2);
            else
                exec_32_ldstm(hw1, hw2);
            break;
        case 0x1E:                                     // 11110
            if (hw2 & 0x8000u) exec_32_branch_misc(hw1, hw2);
            else               exec_32_dp_imm(hw1, hw2);
            break;
        case 0x1F:                                     // 11111
            if ((hw1 & 0x0600u) == 0x0000u)      exec_32_ldst(hw1, hw2);   // 1111 100x
            else if ((hw1 & 0x0700u) == 0x0200u) exec_32_dp_reg(hw1, hw2); // 1111 1010
            else if ((hw1 & 0x0700u) == 0x0300u) exec_32_mul(hw1, hw2);    // 1111 1011
            else undefined();
            break;
        default: undefined(); break;
    }
}

// ===========================================================================
// §2.1 / §2.2 — múltiples, exclusivos, dual y tablas de salto
// ===========================================================================
inline void Cpu::exec_32_ldstm(uint32_t hw1, uint32_t hw2) {
    const unsigned rn = hw1 & 0xFu;
    const bool L = (hw1 >> 4) & 1u;
    const bool W = (hw1 >> 5) & 1u;
    const unsigned op = (hw1 >> 7) & 0x3u;     // hw1[8:7]

    // ---- Carga/almacenamiento múltiple [II, §2.1] --------------------------
    // STM/LDM IA: 1110 1000 10 W L Rn ; STMDB/LDMDB: 1110 1001 00 W L Rn
    // La máscara debe cubrir hw1[7:6]: con 0xFF80 se tragaría TBB/TBH
    // (0xE8DF), que comparte los bits altos.
    if ((hw1 & 0xFFC0u) == 0xE880u || (hw1 & 0xFFC0u) == 0xE900u) {
        const bool db = (hw1 & 0x0100u) != 0;         // STMDB/LDMDB
        const uint32_t list = hw2 & 0xFFFFu;
        const unsigned n = bit_count(list);
        cycles_ = 1 + n;
        uint32_t a = db ? (rd_(rn) - 4u * n) : rd_(rn);
        const uint32_t wb = db ? (rd_(rn) - 4u * n) : (rd_(rn) + 4u * n);
        for (unsigned i = 0; i < 15; ++i)
            if ((list >> i) & 1u) {
                if (L) { uint32_t v = 0; if (!mem_read(a, 4, v, true)) return; reg.r[i] = v; }
                else   { if (!mem_write(a, 4, reg.r[i], true)) return; }
                a += 4;
            }
        if ((list >> 15) & 1u) {
            if (L) { uint32_t v = 0; if (!mem_read(a, 4, v, true)) return; load_write_pc(v); cycles_ += 2; }
            else   { if (!mem_write(a, 4, reg.r[15], true)) return; }
        }
        if (W && !(L && ((list >> rn) & 1u))) set_reg(rn, wb);
        return;
    }

    // ---- Exclusivos, dual y tablas de salto [II, §2.2] ---------------------
    const unsigned rt  = (hw2 >> 12) & 0xFu;
    const unsigned rt2 = (hw2 >> 8) & 0xFu;

    if ((hw1 & 0xFFF0u) == 0xE840u) {                 // STREX Rd,Rt,[Rn,#imm8*4]
        const unsigned rd = (hw2 >> 8) & 0xFu;
        const uint32_t a = rd_(rn) + ((hw2 & 0xFFu) << 2);
        if (excl_valid_ && excl_addr_ == a) {
            if (!mem_write(a, 4, rd_(rt), true, true)) return;
            set_reg(rd, 0);
        } else {
            set_reg(rd, 1);
        }
        excl_valid_ = false;
        return;
    }
    if ((hw1 & 0xFFF0u) == 0xE850u) {                 // LDREX Rt,[Rn,#imm8*4]
        const uint32_t a = rd_(rn) + ((hw2 & 0xFFu) << 2);
        uint32_t v = 0;
        if (!mem_read(a, 4, v, true, true)) return;
        set_reg(rt, v);
        excl_valid_ = true; excl_addr_ = a; excl_size_ = 4;
        return;
    }
    if ((hw1 & 0xFFF0u) == 0xE8C0u || (hw1 & 0xFFF0u) == 0xE8D0u) {
        const unsigned op2 = (hw2 >> 4) & 0xFu;
        const bool load = (hw1 & 0x10u) != 0;
        if (op2 == 0x4 || op2 == 0x5) {               // {LD,ST}REX{B,H}
            const unsigned size = (op2 == 0x4) ? 1u : 2u;
            const uint32_t a = rd_(rn);
            if (load) {
                uint32_t v = 0;
                if (!mem_read(a, size, v, true, true)) return;
                set_reg(rt, v);
                excl_valid_ = true; excl_addr_ = a; excl_size_ = size;
            } else {
                const unsigned rd = hw2 & 0xFu;
                if (excl_valid_ && excl_addr_ == a) {
                    if (!mem_write(a, size, rd_(rt), true, true)) return;
                    set_reg(rd, 0);
                } else set_reg(rd, 1);
                excl_valid_ = false;
            }
            return;
        }
        if (load && (hw2 & 0xFFE0u) == 0xF000u) {     // TBB / TBH
            const unsigned rm = hw2 & 0xFu;
            const bool half = (hw2 >> 4) & 1u;
            uint32_t idx = 0;
            const uint32_t a = half ? (rd_(rn) + 2u * rd_(rm)) : (rd_(rn) + rd_(rm));
            if (!mem_read(a, half ? 2u : 1u, idx)) return;
            branch_to(cur_pc_ + 4u + 2u * idx);
            cycles_ = 4;
            return;
        }
    }
    // CLREX [II, §2.2]
    if (hw1 == 0xF3BFu && (hw2 & 0xFFF0u) == 0x8F20u) { excl_valid_ = false; return; }

    // ---- LDRD / STRD [II, §2.2] -------------------------------------------
    if ((hw1 & 0xFE40u) == 0xE840u) {
        const bool P = (hw1 >> 8) & 1u, U = (hw1 >> 7) & 1u, Wb = (hw1 >> 5) & 1u;
        const uint32_t imm = (hw2 & 0xFFu) << 2;
        const uint32_t base = (rn == 15) ? ((cur_pc_ + 4u) & ~3u) : rd_(rn);
        const uint32_t off_addr = U ? (base + imm) : (base - imm);
        const uint32_t a = P ? off_addr : base;
        if (L) {
            uint32_t v1 = 0, v2 = 0;
            if (!mem_read(a, 4, v1, true) || !mem_read(a + 4, 4, v2, true)) return;
            set_reg(rt, v1); set_reg(rt2, v2);
        } else {
            if (!mem_write(a, 4, rd_(rt), true) || !mem_write(a + 4, 4, rd_(rt2), true)) return;
        }
        if (Wb && rn != 15) set_reg(rn, off_addr);
        cycles_ = 3;
        return;
    }
    (void)op;
    undefined();
}

// ===========================================================================
// §2.3 — Procesamiento de datos con registro desplazado
// ===========================================================================
inline void Cpu::exec_32_dp_reg_shift(uint32_t hw1, uint32_t hw2) {
    const unsigned op = (hw1 >> 5) & 0xFu;
    const bool S  = (hw1 >> 4) & 1u;
    const unsigned rn = hw1 & 0xFu;
    const unsigned rd = (hw2 >> 8) & 0xFu, rm = hw2 & 0xFu;
    const unsigned imm5 = (((hw2 >> 12) & 0x7u) << 2) | ((hw2 >> 6) & 0x3u);
    const unsigned type = (hw2 >> 4) & 0x3u;

    if (op == 0x6) {                                   // PKHBT / PKHTB [II, §2.3]
        const ImmShift sh = decode_imm_shift(type, imm5);
        const uint32_t shifted = shift_c(rd_(rm), sh.type, sh.amount, reg.c()).result;
        const bool tb = (type >> 1) & 1u;
        const uint32_t r = tb ? ((rd_(rn) & 0xFFFF0000u) | (shifted & 0xFFFFu))
                              : ((shifted & 0xFFFF0000u) | (rd_(rn) & 0xFFFFu));
        set_reg(rd, r);
        return;
    }
    const ImmShift sh = decode_imm_shift(type, imm5);
    const ShiftResult sr = shift_c(rd_(rm), sh.type, sh.amount, reg.c());
    dp_op(op, rd, rn, sr.result, sr.carry, S);
}

// ===========================================================================
// §3.1 / §3.2 — Procesamiento de datos con inmediato
// ===========================================================================
inline void Cpu::exec_32_dp_imm(uint32_t hw1, uint32_t hw2) {
    const unsigned rn = hw1 & 0xFu;
    const unsigned rd = (hw2 >> 8) & 0xFu;
    const uint32_t i  = (hw1 >> 10) & 1u;
    const uint32_t imm3 = (hw2 >> 12) & 0x7u;
    const uint32_t imm8 = hw2 & 0xFFu;
    const uint32_t imm12 = (i << 11) | (imm3 << 8) | imm8;

    if (((hw1 >> 9) & 1u) == 0) {                      // §3.1 inmediato modificado
        const unsigned op = (hw1 >> 5) & 0xFu;
        const bool S = (hw1 >> 4) & 1u;
        const ExpandResult e = thumb_expand_imm_c(imm12, reg.c());
        dp_op(op, rd, rn, e.imm32, e.carry, S);
        return;
    }

    // ---- §3.2 inmediato binario plano --------------------------------------
    const unsigned op5 = (hw1 >> 4) & 0x1Fu;
    switch (op5) {
        case 0x00:                                     // ADDW / ADR
            set_reg(rd, (rn == 15) ? (((cur_pc_ + 4u) & ~3u) + imm12) : (rd_(rn) + imm12));
            return;
        case 0x04: {                                   // MOVW
            const uint32_t imm16 = (uint32_t(rn) << 12) | imm12;
            set_reg(rd, imm16);
            return;
        }
        case 0x0A:                                     // SUBW / ADR (resta)
            set_reg(rd, (rn == 15) ? (((cur_pc_ + 4u) & ~3u) - imm12) : (rd_(rn) - imm12));
            return;
        case 0x0C: {                                   // MOVT
            const uint32_t imm16 = (uint32_t(rn) << 12) | imm12;
            set_reg(rd, (rd_(rd) & 0xFFFFu) | (imm16 << 16));
            return;
        }
        case 0x10: case 0x12: {                        // SSAT / SSAT16
            const unsigned satimm = (hw2 & 0x1Fu) + 1u;
            const unsigned shamt = (imm3 << 2) | ((hw2 >> 6) & 3u);
            if (op5 == 0x12 && shamt == 0) {           // SSAT16
                const uint32_t v = rd_(rn);
                const unsigned sat = (hw2 & 0xFu) + 1u;
                const SatResult lo = signed_sat_q(int16_t(v & 0xFFFFu), sat);
                const SatResult hi = signed_sat_q(int16_t(v >> 16), sat);
                set_reg(rd, (uint32_t(uint16_t(hi.result)) << 16) | uint16_t(lo.result));
                if (lo.sat || hi.sat) reg.set_q(true);
                return;
            }
            const ShiftResult sr = shift_c(rd_(rn), (op5 == 0x10) ? SRT_LSL : SRT_ASR,
                                           shamt, reg.c());
            const SatResult s = signed_sat_q(int32_t(sr.result), satimm);
            set_reg(rd, uint32_t(int32_t(s.result)));
            if (s.sat) reg.set_q(true);
            return;
        }
        case 0x14: {                                   // SBFX
            const unsigned lsb = (imm3 << 2) | ((hw2 >> 6) & 3u);
            const unsigned width = (hw2 & 0x1Fu) + 1u;
            if (lsb + width > 32) { undefined(); return; }
            const uint32_t v = (rd_(rn) >> lsb) & ((width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u));
            set_reg(rd, sign_extend(v, width));
            return;
        }
        case 0x16: {                                   // BFI / BFC
            const unsigned lsb = (imm3 << 2) | ((hw2 >> 6) & 3u);
            const unsigned msb = hw2 & 0x1Fu;
            if (msb < lsb) { undefined(); return; }
            const unsigned width = msb - lsb + 1u;
            const uint32_t mask = ((width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u)) << lsb;
            const uint32_t src = (rn == 15) ? 0u : ((rd_(rn) << lsb) & mask);
            set_reg(rd, (rd_(rd) & ~mask) | src);
            return;
        }
        case 0x18: case 0x1A: {                        // USAT / USAT16
            const unsigned satimm = hw2 & 0x1Fu;
            const unsigned shamt = (imm3 << 2) | ((hw2 >> 6) & 3u);
            if (op5 == 0x1A && shamt == 0) {           // USAT16
                const uint32_t v = rd_(rn);
                const unsigned sat = hw2 & 0xFu;
                const SatResult lo = unsigned_sat_q(int16_t(v & 0xFFFFu), sat);
                const SatResult hi = unsigned_sat_q(int16_t(v >> 16), sat);
                set_reg(rd, (uint32_t(uint16_t(hi.result)) << 16) | uint16_t(lo.result));
                if (lo.sat || hi.sat) reg.set_q(true);
                return;
            }
            const ShiftResult sr = shift_c(rd_(rn), (op5 == 0x18) ? SRT_LSL : SRT_ASR,
                                           shamt, reg.c());
            const SatResult s = unsigned_sat_q(int32_t(sr.result), satimm);
            set_reg(rd, uint32_t(s.result));
            if (s.sat) reg.set_q(true);
            return;
        }
        case 0x1C: {                                   // UBFX
            const unsigned lsb = (imm3 << 2) | ((hw2 >> 6) & 3u);
            const unsigned width = (hw2 & 0x1Fu) + 1u;
            if (lsb + width > 32) { undefined(); return; }
            set_reg(rd, (rd_(rn) >> lsb) & ((width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u)));
            return;
        }
        default: undefined(); return;
    }
}

// ===========================================================================
// §3.3 — Saltos, MSR/MRS, hints y barreras
// ===========================================================================
inline void Cpu::exec_32_branch_misc(uint32_t hw1, uint32_t hw2) {
    const unsigned op = (hw1 >> 4) & 0x7Fu;
    const unsigned op2 = (hw2 >> 12) & 0x7u;

    if ((op2 & 0x5u) == 0x0u) {                        // hw2[14:12] = 0x0 / 0x2
        // B<c>.W (T3), MSR, MRS, hints, barreras, UDF.W
        if ((op & 0x38u) != 0x38u) {                   // B<c>.W [II, §3.3]
            const unsigned cond = (hw1 >> 6) & 0xFu;
            const uint32_t S = (hw1 >> 10) & 1u;
            const uint32_t j1 = (hw2 >> 13) & 1u, j2 = (hw2 >> 11) & 1u;
            const uint32_t imm6 = hw1 & 0x3Fu, imm11 = hw2 & 0x7FFu;
            const uint32_t off = sign_extend((S << 20) | (j2 << 19) | (j1 << 18) |
                                             (imm6 << 12) | (imm11 << 1), 21);
            if (condition_passed(cond, reg)) { branch_to(cur_pc_ + 4u + off); cycles_ = 3; }
            return;
        }
        switch (op) {
            case 0x38: case 0x39: {                    // MSR
                const unsigned rn = hw1 & 0xFu;
                const unsigned sysm = hw2 & 0xFFu;
                const unsigned mask = (hw2 >> 10) & 3u;
                const uint32_t v = rd_(rn);
                if (sysm <= 3) {                       // APSR / IAPSR / EAPSR / xPSR
                    if (mask & 2u) reg.xpsr = (reg.xpsr & 0x07FFFFFFu) | (v & 0xF8000000u);
                    if (mask & 1u) reg.set_ge((v >> 16) & 0xFu);
                } else if (reg.privileged()) {
                    switch (sysm) {
                        case 8:  reg.msp = v & ~3u; if (!reg.using_psp()) reg.r[13] = reg.msp; break;
                        case 9:  reg.psp = v & ~3u; if (reg.using_psp()) reg.r[13] = reg.psp; break;
                        case 16: reg.primask = v & 1u; break;
                        case 17: reg.basepri = uint8_t(v & 0xFFu); break;
                        case 18: if (uint8_t(v & 0xFFu) != 0 &&
                                     (reg.basepri == 0 || uint8_t(v & 0xFFu) < reg.basepri))
                                     reg.basepri = uint8_t(v & 0xFFu);          // BASEPRI_MAX
                                 break;
                        case 19: reg.faultmask = v & 1u; break;
                        case 20:
                            reg.store_sp();
                            reg.control = uint8_t(v & (reg.handler_mode ? 0x5u : 0x7u));
                            reg.load_sp();
                            break;
                        default: break;
                    }
                }
                return;
            }
            case 0x3A: {                               // hints y CPS.W
                const unsigned hint = hw2 & 0xFFu;
                switch (hint) {
                    case 0x00: case 0x01: break;                        // NOP / YIELD
                    case 0x02:                                          // WFE
                        if (event_reg_ || event_in.read()) event_reg_ = false;
                        else { sleeping_state_ = true; sleep_wfe_ = true;
                           o_sleepdeep_ = sys->scr_sleepdeep(); publish(); }
                        break;
                    case 0x03:                                          // WFI
                        if (!sys->any_pending()) {
                            sleeping_state_ = true; sleep_wfe_ = false;
                            o_sleepdeep_ = sys->scr_sleepdeep(); publish();
                        }
                        break;
                    case 0x04:                                          // SEV
                        event_reg_ = true;
                        o_event_ = true; publish(); o_event_ = false; publish();
                        break;
                    default: break;                                     // DBG #x
                }
                return;
            }
            case 0x3B:                                 // DSB / DMB / ISB / CLREX
                if ((hw2 & 0x00F0u) == 0x0020u) excl_valid_ = false;    // CLREX
                if ((hw2 & 0x00F0u) == 0x0060u) flush_prefetch();       // ISB
                // DSB/DMB son no-operativas en un modelo de orden estricto
                return;
            case 0x3E: case 0x3F: {                    // MRS
                const unsigned rd = (hw2 >> 8) & 0xFu;
                const unsigned sysm = hw2 & 0xFFu;
                uint32_t v = 0;
                switch (sysm) {
                    case 0: v = reg.xpsr & 0xF80F0000u; break;          // APSR
                    case 1: v = reg.xpsr & 0x000001FFu; break;          // IPSR
                    case 2: v = reg.xpsr & 0x0700FC00u; break;          // EPSR (lee 0)
                    case 3: v = reg.xpsr & 0xF80F01FFu; break;          // xPSR
                    case 5: v = reg.xpsr & 0x000001FFu; break;          // IEPSR
                    case 8: v = reg.using_psp() ? reg.msp : reg.r[13]; break;
                    case 9: v = reg.using_psp() ? reg.r[13] : reg.psp; break;
                    case 16: v = reg.primask; break;
                    case 17: case 18: v = reg.basepri; break;
                    case 19: v = reg.faultmask; break;
                    case 20: v = reg.control; break;
                    default: v = 0; break;
                }
                set_reg(rd, v);
                return;
            }
            default:
                if ((hw1 & 0xFFF0u) == 0xF7F0u && (hw2 & 0xF000u) == 0xA000u) {
                    undefined();                       // UDF.W
                    return;
                }
                undefined();
                return;
        }
    }

    // hw2[14:12] = 0x1 (B.W T4) o 0x5 (BL)
    const uint32_t S = (hw1 >> 10) & 1u;
    const uint32_t imm10 = hw1 & 0x3FFu, imm11 = hw2 & 0x7FFu;
    const uint32_t j1 = (hw2 >> 13) & 1u, j2 = (hw2 >> 11) & 1u;
    const uint32_t i1 = (~(j1 ^ S)) & 1u, i2 = (~(j2 ^ S)) & 1u;
    const uint32_t off = sign_extend((S << 24) | (i1 << 23) | (i2 << 22) |
                                     (imm10 << 12) | (imm11 << 1), 25);
    if ((hw2 >> 14) & 1u) {                            // BL
        reg.r[14] = (cur_pc_ + 4u) | 1u;
    }
    branch_to(cur_pc_ + 4u + off);
    cycles_ = 3;
}

// ===========================================================================
// §4.1 — Carga y almacenamiento simple de 32 bits
// ===========================================================================
inline void Cpu::exec_32_ldst(uint32_t hw1, uint32_t hw2) {
    const unsigned rn = hw1 & 0xFu;
    const unsigned rt = (hw2 >> 12) & 0xFu;
    const bool L  = (hw1 >> 4) & 1u;
    const unsigned sz = (hw1 >> 5) & 0x3u;             // 00 byte, 01 half, 10 word
    const bool Sx = (hw1 >> 8) & 1u;                   // carga con signo
    const bool F  = (hw1 >> 7) & 1u;                   // imm12
    if (sz == 3) { undefined(); return; }
    const unsigned size = 1u << sz;

    uint32_t addr = 0, off_addr = 0, base = 0;
    bool wb = false;

    if (rn == 15) {                                    // literal [II, §4.1]
        if (!L) { undefined(); return; }
        const uint32_t imm12 = hw2 & 0xFFFu;
        base = (cur_pc_ + 4u) & ~3u;
        addr = F ? (base + imm12) : (base - imm12);
    } else if (F) {                                    // inmediato de 12 bits
        addr = rd_(rn) + (hw2 & 0xFFFu);
    } else if ((hw2 & 0x0F00u) == 0x0000u) {           // registro desplazado
        const unsigned rm = hw2 & 0xFu;
        const unsigned imm2 = (hw2 >> 4) & 0x3u;
        addr = rd_(rn) + (rd_(rm) << imm2);
    } else if ((hw2 & 0x0F00u) == 0x0E00u) {           // no privilegiado (T)
        addr = rd_(rn) + (hw2 & 0xFFu);
    } else if (hw2 & 0x0800u) {                        // imm8 con P/U/W
        const bool P = (hw2 >> 10) & 1u, U = (hw2 >> 9) & 1u, W = (hw2 >> 8) & 1u;
        const uint32_t imm8 = hw2 & 0xFFu;
        base = rd_(rn);
        off_addr = U ? (base + imm8) : (base - imm8);
        addr = P ? off_addr : base;
        wb = W;
    } else { undefined(); return; }

    cycles_ = L ? 2 : 1;
    if (L) {
        // Rt = 1111 en cargas de byte/halfword sin signo: PLD/PLI (hint)
        if (rt == 15 && !Sx && sz != 2) { return; }
        uint32_t v = 0;
        if (!mem_read(addr, size, v)) return;
        if (Sx) v = sign_extend(v, 8u * size);
        if (rt == 15) { load_write_pc(v); cycles_ = 4; }
        else set_reg(rt, v);
    } else {
        if (!mem_write(addr, size, rd_(rt))) return;
    }
    if (wb) set_reg(rn, off_addr);
}

// ===========================================================================
// §4.2 — Procesamiento de datos de registro (desplazamientos, extensiones,
//        SIMD paralelas y misceláneas)
// ===========================================================================
inline void Cpu::exec_32_dp_reg(uint32_t hw1, uint32_t hw2) {
    const unsigned rn = hw1 & 0xFu;
    const unsigned rd = (hw2 >> 8) & 0xFu, rm = hw2 & 0xFu;

    // ---- Desplazamientos por registro: 1111 1010 0 type S Rn | 1111 Rd 0000 Rm
    if ((hw1 & 0xFF80u) == 0xFA00u && (hw2 & 0xF0F0u) == 0xF000u) {
        const unsigned type = (hw1 >> 5) & 3u;
        const bool S = (hw1 >> 4) & 1u;
        static const ShiftType tt[4] = {SRT_LSL, SRT_LSR, SRT_ASR, SRT_ROR};
        const ShiftResult r = shift_c(rd_(rn), tt[type], rd_(rm) & 0xFFu, reg.c());
        set_reg(rd, r.result);
        alu_flags_nzc(r.result, r.carry, S);
        return;
    }

    // ---- Extensiones con/sin acumulación: hw2 = 1111 Rd 1 (0) rot2 Rm ------
    if ((hw1 & 0xFF80u) == 0xFA00u && (hw2 & 0xF080u) == 0xF080u) {
        const unsigned op3 = (hw1 >> 4) & 0x7u;
        const unsigned rot = (hw2 >> 4) & 0x3u;
        const uint32_t rotated = ror32(rd_(rm), rot * 8u);
        const uint32_t acc = (rn == 15) ? 0u : rd_(rn);
        switch (op3) {
            case 0: set_reg(rd, acc + sign_extend(rotated & 0xFFFFu, 16)); return;  // SXT(A)H
            case 1: set_reg(rd, acc + (rotated & 0xFFFFu)); return;                 // UXT(A)H
            case 2: {                                                               // SXT(A)B16
                const uint32_t lo = sign_extend(rotated & 0xFFu, 8) & 0xFFFFu;
                const uint32_t hi = sign_extend((rotated >> 16) & 0xFFu, 8) & 0xFFFFu;
                set_reg(rd, ((((acc >> 16) + hi) & 0xFFFFu) << 16) | (((acc & 0xFFFFu) + lo) & 0xFFFFu));
                return;
            }
            case 3: {                                                               // UXT(A)B16
                const uint32_t lo = rotated & 0xFFu, hi = (rotated >> 16) & 0xFFu;
                set_reg(rd, ((((acc >> 16) + hi) & 0xFFFFu) << 16) | (((acc & 0xFFFFu) + lo) & 0xFFFFu));
                return;
            }
            case 4: set_reg(rd, acc + sign_extend(rotated & 0xFFu, 8)); return;     // SXT(A)B
            case 5: set_reg(rd, acc + (rotated & 0xFFu)); return;                   // UXT(A)B
            default: undefined(); return;
        }
    }

    // ---- Aritmética paralela SIMD: 1111 1010 1 op1 Rn | 1111 Rd 0 pfx3 Rm --
    if ((hw1 & 0xFF80u) == 0xFA80u && (hw2 & 0xF080u) == 0xF000u) {
        const unsigned op1 = (hw1 >> 4) & 0x7u;
        const unsigned pfx = (hw2 >> 4) & 0x7u;
        const uint32_t a = rd_(rn), b = rd_(rm);
        const bool sgn = (pfx == 0 || pfx == 1 || pfx == 2);
        const bool sat = (pfx == 1 || pfx == 5);
        const bool halv = (pfx == 2 || pfx == 6);
        const bool writes_ge = (pfx == 0 || pfx == 4);
        auto half = [&](uint32_t v, unsigned i) -> int32_t {
            const uint32_t h = (v >> (16 * i)) & 0xFFFFu;
            return sgn ? int32_t(int16_t(h)) : int32_t(h);
        };
        auto byte = [&](uint32_t v, unsigned i) -> int32_t {
            const uint32_t x = (v >> (8 * i)) & 0xFFu;
            return sgn ? int32_t(int8_t(x)) : int32_t(x);
        };
        auto pack16 = [&](int32_t s, unsigned& ge_bits, unsigned pos) -> uint32_t {
            if (sat) s = int32_t(sgn ? signed_sat_q(s, 16).result : unsigned_sat_q(s, 16).result);
            else if (halv) s >>= 1;
            if (writes_ge && (sgn ? (s >= 0) : (s >= 0x10000 || s >= 0)))
                ge_bits |= (0x3u << (2 * pos));
            return uint32_t(s) & 0xFFFFu;
        };
        unsigned ge = 0;
        uint32_t r = 0;
        switch (op1) {
            case 1: {                                  // suma 16+16
                int32_t s0 = half(a, 0) + half(b, 0), s1 = half(a, 1) + half(b, 1);
                unsigned g = 0;
                const uint32_t l = pack16(s0, g, 0), h = pack16(s1, g, 1);
                if (writes_ge) {
                    ge = 0;
                    if (sgn ? (half(a,0)+half(b,0) >= 0) : (uint32_t(half(a,0))+uint32_t(half(b,0)) >= 0x10000u)) ge |= 0x3u;
                    if (sgn ? (half(a,1)+half(b,1) >= 0) : (uint32_t(half(a,1))+uint32_t(half(b,1)) >= 0x10000u)) ge |= 0xCu;
                }
                r = (h << 16) | l;
                break;
            }
            case 5: {                                  // resta 16-16
                int32_t s0 = half(a, 0) - half(b, 0), s1 = half(a, 1) - half(b, 1);
                unsigned g = 0;
                const uint32_t l = pack16(s0, g, 0), h = pack16(s1, g, 1);
                if (writes_ge) {
                    ge = 0;
                    if (half(a,0) - half(b,0) >= 0) ge |= 0x3u;
                    if (half(a,1) - half(b,1) >= 0) ge |= 0xCu;
                }
                r = (h << 16) | l;
                break;
            }
            case 2: {                                  // ASX
                int32_t s0 = half(a, 0) - half(b, 1), s1 = half(a, 1) + half(b, 0);
                unsigned g = 0;
                const uint32_t l = pack16(s0, g, 0), h = pack16(s1, g, 1);
                if (writes_ge) { ge = 0; if (s0 >= 0) ge |= 0x3u; if (s1 >= 0) ge |= 0xCu; }
                r = (h << 16) | l;
                break;
            }
            case 6: {                                  // SAX
                int32_t s0 = half(a, 0) + half(b, 1), s1 = half(a, 1) - half(b, 0);
                unsigned g = 0;
                const uint32_t l = pack16(s0, g, 0), h = pack16(s1, g, 1);
                if (writes_ge) { ge = 0; if (s0 >= 0) ge |= 0x3u; if (s1 >= 0) ge |= 0xCu; }
                r = (h << 16) | l;
                break;
            }
            case 0: case 4: {                          // suma/resta de 4 bytes
                const bool sub = (op1 == 4);
                ge = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    int32_t s = sub ? (byte(a, i) - byte(b, i)) : (byte(a, i) + byte(b, i));
                    if (writes_ge && s >= 0) ge |= (1u << i);
                    if (sat) s = int32_t(sgn ? signed_sat_q(s, 8).result : unsigned_sat_q(s, 8).result);
                    else if (halv) s >>= 1;
                    r |= (uint32_t(s) & 0xFFu) << (8 * i);
                }
                break;
            }
            default: undefined(); return;
        }
        set_reg(rd, r);
        if (writes_ge) reg.set_ge(ge);
        return;
    }

    // ---- Misceláneas: 1111 1010 1 op1 Rn | 1111 Rd 1 op2 Rm ---------------
    if ((hw1 & 0xFF80u) == 0xFA80u && (hw2 & 0xF080u) == 0xF080u) {
        const unsigned op1 = (hw1 >> 4) & 0x7u;
        const unsigned op2 = (hw2 >> 4) & 0x3u;
        const uint32_t n = rd_(rn), m = rd_(rm);
        switch (op1) {
            case 0: {                                  // QADD / QDADD / QSUB / QDSUB
                int64_t r64;
                bool q = false;
                if (op2 == 0)      r64 = int64_t(int32_t(m)) + int64_t(int32_t(n));
                else if (op2 == 2) r64 = int64_t(int32_t(m)) - int64_t(int32_t(n));
                else {
                    const SatResult d = signed_sat_q(2ll * int64_t(int32_t(n)), 32);
                    if (d.sat) q = true;
                    r64 = (op2 == 1) ? (int64_t(int32_t(m)) + d.result)
                                     : (int64_t(int32_t(m)) - d.result);
                }
                const SatResult s = signed_sat_q(r64, 32);
                set_reg(rd, uint32_t(int32_t(s.result)));
                if (s.sat || q) reg.set_q(true);
                return;
            }
            case 1:                                    // REV / REV16 / RBIT / REVSH
                switch (op2) {
                    case 0: set_reg(rd, rev32(m)); return;
                    case 1: set_reg(rd, rev16_pair(m)); return;
                    case 2: set_reg(rd, rbit32(m)); return;
                    default: set_reg(rd, revsh(m)); return;
                }
            case 2: {                                  // SEL
                const unsigned ge = reg.ge();
                uint32_t r = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    const uint32_t src = ((ge >> i) & 1u) ? n : m;
                    r |= (src & (0xFFu << (8 * i)));
                }
                set_reg(rd, r);
                return;
            }
            case 3:                                    // CLZ
                set_reg(rd, count_leading_zeros(m));
                return;
            default: undefined(); return;
        }
    }
    undefined();
}

// ===========================================================================
// §4.3 / §4.4 — Multiplicación, MAC, multiplicación larga y división
// ===========================================================================
inline void Cpu::exec_32_mul(uint32_t hw1, uint32_t hw2) {
    const unsigned rn = hw1 & 0xFu, rm = hw2 & 0xFu;
    const unsigned ra = (hw2 >> 12) & 0xFu, rd = (hw2 >> 8) & 0xFu;
    const unsigned op3 = (hw1 >> 4) & 0x7u;
    const unsigned op2 = (hw2 >> 4) & 0xFu;
    const uint32_t n = rd_(rn), m = rd_(rm);

    auto s16 = [](uint32_t v, bool top) { return int32_t(int16_t(top ? (v >> 16) : (v & 0xFFFFu))); };

    if ((hw1 & 0xFF80u) == 0xFB00u) {                  // §4.3 multiplicación de 32 bits
        switch (op3) {
            case 0:                                    // MUL / MLA / MLS
                if (op2 == 0) set_reg(rd, (ra == 15) ? (n * m) : (rd_(ra) + n * m));
                else if (op2 == 1) set_reg(rd, rd_(ra) - n * m);
                else undefined();
                return;
            case 1: {                                  // SMUL<x><y> / SMLA<x><y>
                const int32_t p = s16(n, (op2 >> 1) & 1u) * s16(m, op2 & 1u);
                if (ra == 15) set_reg(rd, uint32_t(p));
                else {
                    const int64_t r = int64_t(int32_t(rd_(ra))) + int64_t(p);
                    set_reg(rd, uint32_t(int32_t(r)));
                    if (r != int64_t(int32_t(r))) reg.set_q(true);
                }
                return;
            }
            case 2: {                                  // SMUAD / SMLAD
                const bool x = op2 & 1u;
                const uint32_t mm = x ? ror32(m, 16) : m;
                const int64_t p = int64_t(s16(n, false)) * s16(mm, false) +
                                  int64_t(s16(n, true))  * s16(mm, true);
                const int64_t r = (ra == 15) ? p : (p + int64_t(int32_t(rd_(ra))));
                set_reg(rd, uint32_t(int32_t(r)));
                if (r != int64_t(int32_t(r))) reg.set_q(true);
                return;
            }
            case 3: {                                  // SMULW<y> / SMLAW<y>
                const int64_t p = (int64_t(int32_t(n)) * s16(m, op2 & 1u)) >> 16;
                if (ra == 15) set_reg(rd, uint32_t(int32_t(p)));
                else {
                    const int64_t r = int64_t(int32_t(rd_(ra))) + p;
                    set_reg(rd, uint32_t(int32_t(r)));
                    if (r != int64_t(int32_t(r))) reg.set_q(true);
                }
                return;
            }
            case 4: {                                  // SMUSD / SMLSD
                const bool x = op2 & 1u;
                const uint32_t mm = x ? ror32(m, 16) : m;
                const int64_t p = int64_t(s16(n, false)) * s16(mm, false) -
                                  int64_t(s16(n, true))  * s16(mm, true);
                const int64_t r = (ra == 15) ? p : (p + int64_t(int32_t(rd_(ra))));
                set_reg(rd, uint32_t(int32_t(r)));
                if (ra != 15 && r != int64_t(int32_t(r))) reg.set_q(true);
                return;
            }
            case 5: {                                  // SMMUL / SMMLA
                int64_t acc = (ra == 15) ? 0 : (int64_t(int32_t(rd_(ra))) << 32);
                int64_t r = acc + int64_t(int32_t(n)) * int64_t(int32_t(m));
                if (op2 & 1u) r += 0x80000000ll;        // redondeo
                set_reg(rd, uint32_t(uint64_t(r) >> 32));
                return;
            }
            case 6: {                                  // SMMLS
                int64_t acc = int64_t(int32_t(rd_(ra))) << 32;
                int64_t r = acc - int64_t(int32_t(n)) * int64_t(int32_t(m));
                if (op2 & 1u) r += 0x80000000ll;
                set_reg(rd, uint32_t(uint64_t(r) >> 32));
                return;
            }
            case 7: {                                  // USAD8 / USADA8
                uint32_t sad = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    const int32_t x = int32_t((n >> (8 * i)) & 0xFFu);
                    const int32_t y = int32_t((m >> (8 * i)) & 0xFFu);
                    sad += uint32_t(x > y ? (x - y) : (y - x));
                }
                set_reg(rd, (ra == 15) ? sad : (rd_(ra) + sad));
                return;
            }
            default: undefined(); return;
        }
    }

    // ---- §4.4 multiplicación larga y división ------------------------------
    const unsigned rdlo = (hw2 >> 12) & 0xFu, rdhi = (hw2 >> 8) & 0xFu;
    switch (op3) {
        case 0: {                                      // SMULL
            const int64_t r = int64_t(int32_t(n)) * int64_t(int32_t(m));
            set_reg(rdlo, uint32_t(uint64_t(r)));
            set_reg(rdhi, uint32_t(uint64_t(r) >> 32));
            return;
        }
        case 1: {                                      // SDIV
            cycles_ = 6;
            if (m == 0) {
                if (sys->div_0_trp()) { take_fault(EXC_USAGEFAULT, UF_DIVBYZERO, false, 0); return; }
                set_reg(rdhi, 0);
                return;
            }
            if (n == 0x80000000u && m == 0xFFFFFFFFu) { set_reg(rdhi, 0x80000000u); return; }
            set_reg(rdhi, uint32_t(int32_t(n) / int32_t(m)));
            return;
        }
        case 2: {                                      // UMULL
            const uint64_t r = uint64_t(n) * uint64_t(m);
            set_reg(rdlo, uint32_t(r));
            set_reg(rdhi, uint32_t(r >> 32));
            return;
        }
        case 3: {                                      // UDIV
            cycles_ = 6;
            if (m == 0) {
                if (sys->div_0_trp()) { take_fault(EXC_USAGEFAULT, UF_DIVBYZERO, false, 0); return; }
                set_reg(rdhi, 0);
                return;
            }
            set_reg(rdhi, n / m);
            return;
        }
        case 4: {                                      // SMLAL / SMLAL<x><y> / SMLALD
            const uint64_t acc = (uint64_t(rd_(rdhi)) << 32) | rd_(rdlo);
            int64_t r;
            if ((op2 & 0xCu) == 0x8u) {                // SMLALBB/BT/TB/TT
                r = int64_t(acc) + int64_t(s16(n, (op2 >> 1) & 1u) * s16(m, op2 & 1u));
            } else if ((op2 & 0xEu) == 0xCu) {         // SMLALD{X}
                const uint32_t mm = (op2 & 1u) ? ror32(m, 16) : m;
                r = int64_t(acc) + int64_t(s16(n, false)) * s16(mm, false)
                                 + int64_t(s16(n, true))  * s16(mm, true);
            } else {
                r = int64_t(acc) + int64_t(int32_t(n)) * int64_t(int32_t(m));
            }
            set_reg(rdlo, uint32_t(uint64_t(r)));
            set_reg(rdhi, uint32_t(uint64_t(r) >> 32));
            return;
        }
        case 5: {                                      // SMLSLD{X}
            const uint64_t acc = (uint64_t(rd_(rdhi)) << 32) | rd_(rdlo);
            const uint32_t mm = (op2 & 1u) ? ror32(m, 16) : m;
            const int64_t r = int64_t(acc) + int64_t(s16(n, false)) * s16(mm, false)
                                           - int64_t(s16(n, true))  * s16(mm, true);
            set_reg(rdlo, uint32_t(uint64_t(r)));
            set_reg(rdhi, uint32_t(uint64_t(r) >> 32));
            return;
        }
        case 6: {                                      // UMLAL / UMAAL
            if (op2 == 0x6) {                          // UMAAL
                const uint64_t r = uint64_t(n) * uint64_t(m) + rd_(rdlo) + rd_(rdhi);
                set_reg(rdlo, uint32_t(r));
                set_reg(rdhi, uint32_t(r >> 32));
            } else {
                const uint64_t acc = (uint64_t(rd_(rdhi)) << 32) | rd_(rdlo);
                const uint64_t r = acc + uint64_t(n) * uint64_t(m);
                set_reg(rdlo, uint32_t(r));
                set_reg(rdhi, uint32_t(r >> 32));
            }
            return;
        }
        default: undefined(); return;
    }
}

// ===========================================================================
// §5 — FPv4-SP
// ===========================================================================
inline bool Cpu::exec_32_fp(uint32_t hw1, uint32_t hw2) {
    // Comprobación de acceso al coprocesador [II, §5.1]
    const unsigned cp = sys->cpacr_cp10();
    if (cp == 0 || (cp == 1 && !reg.privileged())) {
        take_fault(EXC_USAGEFAULT, UF_NOCP, false, 0);
        return true;
    }
    if (!lazy_fp_check()) return true;                 // volcado diferido
    reg.set_fpca(true);

    if (exec_32_fp_ldst(hw1, hw2)) return true;
    const FpuCore::Result r = fpu.execute(hw1, hw2, reg);
    if (r.ok) {
        cycles_ = r.cycles;
        if (fpu_mod) fpu_mod->update_flags(reg.fpscr);
        return true;
    }
    return false;
}

inline bool Cpu::exec_32_fp_ldst(uint32_t hw1, uint32_t hw2) {
    // VLDR / VSTR / VLDM / VSTM / VPUSH / VPOP [II, §5.5]
    if ((hw1 & 0xFE00u) != 0xEC00u) return false;
    const unsigned rn = hw1 & 0xFu;
    const bool P = (hw1 >> 8) & 1u, U = (hw1 >> 7) & 1u;
    // P=0,U=0 no es una forma de carga/almacenamiento: corresponde a la
    // transferencia de dos registros del núcleo a dos S [II, §5.4].
    if (!P && !U) return false;
    const bool D = (hw1 >> 6) & 1u, W = (hw1 >> 5) & 1u, L = (hw1 >> 4) & 1u;
    const unsigned Vd = (hw2 >> 12) & 0xFu;
    const unsigned imm8 = hw2 & 0xFFu;
    const bool dbl = ((hw2 >> 8) & 0xFu) == 0xB;       // registros D (64 bits)
    // Sd = Vd:D para registros simples; Dd = D:Vd para los dobles, que ocupan
    // la pareja S[2d], S[2d+1] [II, §5.1].
    const unsigned sd = dbl ? ((((D ? 1u : 0u) << 4) | Vd) * 2u)
                            : ((Vd << 1) | (D ? 1u : 0u));

    if (P && !W) {                                     // VLDR / VSTR
        const uint32_t base = (rn == 15) ? ((cur_pc_ + 4u) & ~3u) : rd_(rn);
        const uint32_t a = U ? (base + imm8 * 4u) : (base - imm8 * 4u);
        const unsigned nw = dbl ? 2u : 1u;             // palabras a transferir
        cycles_ = 1 + nw;
        for (unsigned i = 0; i < nw; ++i) {
            if (sd + i >= 32) break;
            if (L) { uint32_t v = 0; if (!mem_read(a + 4 * i, 4, v, true)) return true;
                     reg.s[sd + i] = v; }
            else   { if (!mem_write(a + 4 * i, 4, reg.s[sd + i], true)) return true; }
        }
        return true;
    }
    // VLDM / VSTM / VPUSH / VPOP
    const unsigned n_regs = dbl ? (imm8 / 2u) : imm8;
    const uint32_t bytes = 4u * (dbl ? imm8 : imm8);
    uint32_t a = U ? rd_(rn) : (rd_(rn) - bytes);
    cycles_ = 1 + n_regs;
    for (unsigned i = 0; i < (dbl ? n_regs * 2u : n_regs); ++i) {
        if (sd + i >= 32) break;
        if (L) { uint32_t v = 0; if (!mem_read(a, 4, v, true)) return true; reg.s[sd + i] = v; }
        else   { if (!mem_write(a, 4, reg.s[sd + i], true)) return true; }
        a += 4;
    }
    if (W) set_reg(rn, U ? (rd_(rn) + bytes) : (rd_(rn) - bytes));
    return true;
}

} // namespace stm32
#endif // STM32_CORE_CPU_EXEC32_H
