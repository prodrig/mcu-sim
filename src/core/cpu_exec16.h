// =============================================================================
// cpu_exec16.h — Ejecución de las instrucciones Thumb de 16 bits [II, §1]
//
// Incluido desde cpu.h. Cada sección del documento de instrucciones se
// corresponde con un bloque de este fichero.
// =============================================================================
#ifndef STM32_CORE_CPU_EXEC16_H
#define STM32_CORE_CPU_EXEC16_H

namespace stm32 {

inline void Cpu::exec_16(uint32_t hw) {
    const unsigned op = (hw >> 10) & 0x3Fu;
    // "setflags" implícito: activo fuera de un bloque IT [II, §0.2]
    const bool S = !reg.in_it_block();

    // -----------------------------------------------------------------------
    // §1.1 Desplazamiento por inmediato y suma/resta de 3 operandos
    // Todas sus codificaciones tienen bits [15:13] = 000; el grupo 001 es el
    // de inmediato de 8 bits de §1.2.
    // -----------------------------------------------------------------------
    if ((hw >> 13) == 0) {
        const unsigned opc = (hw >> 11) & 0x3u;
        const unsigned rd = hw & 7u, rn = (hw >> 3) & 7u;
        if (opc != 3) {                                   // LSL/LSR/ASR #imm5
            const unsigned imm5 = (hw >> 6) & 0x1Fu;
            const ImmShift sh = decode_imm_shift(opc, imm5);
            const ShiftResult r = shift_c(rd_(rn), sh.type, sh.amount, reg.c());
            set_reg(rd, r.result);
            alu_flags_nzc(r.result, r.carry, S);
            return;
        }
        // ADD/SUB registro o inmediato de 3 bits
        const bool sub = (hw >> 9) & 1u;
        const bool imm = (hw >> 10) & 1u;
        const uint32_t op2 = imm ? uint32_t((hw >> 6) & 7u) : rd_((hw >> 6) & 7u);
        const AddResult a = sub ? add_with_carry(rd_(rn), ~op2, true)
                                : add_with_carry(rd_(rn), op2, false);
        set_reg(rd, a.result);
        alu_flags_nzcv(a.result, a.carry, a.overflow, S);
        return;
    }

    // -----------------------------------------------------------------------
    // §1.2 MOV/CMP/ADD/SUB con inmediato de 8 bits
    // -----------------------------------------------------------------------
    if ((hw >> 13) == 1) {
        const unsigned opc = (hw >> 11) & 3u;
        const unsigned rdn = (hw >> 8) & 7u;
        const uint32_t imm = hw & 0xFFu;
        switch (opc) {
            case 0:                                       // MOV Rd,#imm8
                set_reg(rdn, imm);
                if (S) { reg.set_nz(imm); }
                return;
            case 1: {                                     // CMP Rn,#imm8
                const AddResult a = add_with_carry(rd_(rdn), ~imm, true);
                alu_flags_nzcv(a.result, a.carry, a.overflow, true);
                return;
            }
            case 2: {                                     // ADD Rdn,#imm8
                const AddResult a = add_with_carry(rd_(rdn), imm, false);
                set_reg(rdn, a.result);
                alu_flags_nzcv(a.result, a.carry, a.overflow, S);
                return;
            }
            default: {                                    // SUB Rdn,#imm8
                const AddResult a = add_with_carry(rd_(rdn), ~imm, true);
                set_reg(rdn, a.result);
                alu_flags_nzcv(a.result, a.carry, a.overflow, S);
                return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // §1.3 Procesamiento de datos registro-registro (010000)
    // -----------------------------------------------------------------------
    if (op == 0x10) {
        const unsigned o4 = (hw >> 6) & 0xFu;
        const unsigned rdn = hw & 7u, rm = (hw >> 3) & 7u;
        const uint32_t a = rd_(rdn), b = rd_(rm);
        switch (o4) {
            case 0x0: { const uint32_t r = a & b; set_reg(rdn, r); alu_flags_nzc(r, reg.c(), S); break; }
            case 0x1: { const uint32_t r = a ^ b; set_reg(rdn, r); alu_flags_nzc(r, reg.c(), S); break; }
            case 0x2: case 0x3: case 0x4: case 0x7: {     // LSL/LSR/ASR/ROR por registro
                static const ShiftType tt[16] = {SRT_LSL,SRT_LSL,SRT_LSL,SRT_LSR,SRT_ASR,
                                                 SRT_LSL,SRT_LSL,SRT_ROR,SRT_LSL,SRT_LSL,
                                                 SRT_LSL,SRT_LSL,SRT_LSL,SRT_LSL,SRT_LSL,SRT_LSL};
                const ShiftResult r = shift_c(a, tt[o4], b & 0xFFu, reg.c());
                set_reg(rdn, r.result);
                alu_flags_nzc(r.result, r.carry, S);
                break;
            }
            case 0x5: { const AddResult r = add_with_carry(a, b, reg.c());
                        set_reg(rdn, r.result); alu_flags_nzcv(r.result, r.carry, r.overflow, S); break; }
            case 0x6: { const AddResult r = add_with_carry(a, ~b, reg.c());
                        set_reg(rdn, r.result); alu_flags_nzcv(r.result, r.carry, r.overflow, S); break; }
            case 0x8: { const uint32_t r = a & b; alu_flags_nzc(r, reg.c(), true); break; }   // TST
            case 0x9: { const AddResult r = add_with_carry(~b, 0, true);                       // RSB #0
                        set_reg(rdn, r.result); alu_flags_nzcv(r.result, r.carry, r.overflow, S); break; }
            case 0xA: { const AddResult r = add_with_carry(a, ~b, true);                       // CMP
                        alu_flags_nzcv(r.result, r.carry, r.overflow, true); break; }
            case 0xB: { const AddResult r = add_with_carry(a, b, false);                       // CMN
                        alu_flags_nzcv(r.result, r.carry, r.overflow, true); break; }
            case 0xC: { const uint32_t r = a | b; set_reg(rdn, r); alu_flags_nzc(r, reg.c(), S); break; }
            case 0xD: {                                                                        // MUL: solo NZ
                const uint32_t r = a * b;
                set_reg(rdn, r);
                if (S) reg.set_nz(r);
                break;
            }
            case 0xE: { const uint32_t r = a & ~b; set_reg(rdn, r); alu_flags_nzc(r, reg.c(), S); break; }
            default:  { const uint32_t r = ~b; set_reg(rdn, r); alu_flags_nzc(r, reg.c(), S); break; }
        }
        return;
    }

    // -----------------------------------------------------------------------
    // §1.4 Datos especiales y salto por registro (010001)
    // -----------------------------------------------------------------------
    if (op == 0x11) {
        const unsigned o2 = (hw >> 8) & 3u;
        const unsigned rm = (hw >> 3) & 0xFu;
        const unsigned rdn = (hw & 7u) | (((hw >> 7) & 1u) << 3);
        switch (o2) {
            case 0: {                                    // ADD Rdn,Rm (hi)
                const uint32_t r = rd_(rdn) + rd_(rm);
                if (rdn == 15) { alu_write_pc(r); cycles_ = 3; }
                else set_reg(rdn, r);
                return;
            }
            case 1: {                                    // CMP Rn,Rm (hi)
                const AddResult a = add_with_carry(rd_(rdn), ~rd_(rm), true);
                alu_flags_nzcv(a.result, a.carry, a.overflow, true);
                return;
            }
            case 2: {                                    // MOV Rd,Rm (hi)
                const uint32_t v = rd_(rm);
                if (rdn == 15) { alu_write_pc(v); cycles_ = 3; }
                else set_reg(rdn, v);
                return;
            }
            default: {                                   // BX / BLX
                if ((hw >> 7) & 1u) {                    // BLX Rm
                    const uint32_t target = rd_(rm);
                    reg.r[14] = (cur_pc_ + 2u) | 1u;
                    bx_write_pc(target);
                } else {                                 // BX Rm
                    bx_write_pc(rd_(rm));
                }
                cycles_ = 3;
                return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // §1.5 Cargas y almacenamientos de 16 bits
    // -----------------------------------------------------------------------
    if ((hw >> 11) == 0x09) {                            // LDR Rt,[PC,#imm8*4]
        const unsigned rt = (hw >> 8) & 7u;
        const uint32_t a = ((cur_pc_ + 4u) & ~3u) + ((hw & 0xFFu) << 2);
        uint32_t v = 0;
        if (mem_read(a, 4, v)) set_reg(rt, v);
        cycles_ = 2;
        return;
    }
    if ((hw >> 12) == 0x5) {                             // formas con registro
        const unsigned o3 = (hw >> 9) & 7u;
        const unsigned rt = hw & 7u, rn = (hw >> 3) & 7u, rm = (hw >> 6) & 7u;
        const uint32_t a = rd_(rn) + rd_(rm);
        uint32_t v = 0;
        switch (o3) {
            case 0: mem_write(a, 4, rd_(rt)); break;                        // STR
            case 1: mem_write(a, 2, rd_(rt)); break;                        // STRH
            case 2: mem_write(a, 1, rd_(rt)); break;                        // STRB
            case 3: if (mem_read(a, 1, v)) set_reg(rt, sign_extend(v, 8)); break;  // LDRSB
            case 4: if (mem_read(a, 4, v)) set_reg(rt, v); break;           // LDR
            case 5: if (mem_read(a, 2, v)) set_reg(rt, v); break;           // LDRH
            case 6: if (mem_read(a, 1, v)) set_reg(rt, v); break;           // LDRB
            default: if (mem_read(a, 2, v)) set_reg(rt, sign_extend(v, 16)); break; // LDRSH
        }
        cycles_ = 2;
        return;
    }
    if ((hw >> 13) == 0x3) {                             // imm5, palabra/byte
        const bool load = (hw >> 11) & 1u;
        const bool byte = (hw >> 12) & 1u;
        const unsigned rt = hw & 7u, rn = (hw >> 3) & 7u;
        const unsigned imm5 = (hw >> 6) & 0x1Fu;
        const uint32_t a = rd_(rn) + (byte ? imm5 : (imm5 << 2));
        uint32_t v = 0;
        if (load) { if (mem_read(a, byte ? 1 : 4, v)) set_reg(rt, v); }
        else      { mem_write(a, byte ? 1 : 4, rd_(rt)); }
        cycles_ = 2;
        return;
    }
    if ((hw >> 12) == 0x8) {                             // STRH/LDRH imm5*2
        const bool load = (hw >> 11) & 1u;
        const unsigned rt = hw & 7u, rn = (hw >> 3) & 7u;
        const uint32_t a = rd_(rn) + (((hw >> 6) & 0x1Fu) << 1);
        uint32_t v = 0;
        if (load) { if (mem_read(a, 2, v)) set_reg(rt, v); }
        else      { mem_write(a, 2, rd_(rt)); }
        cycles_ = 2;
        return;
    }
    if ((hw >> 12) == 0x9) {                             // STR/LDR [SP,#imm8*4]
        const bool load = (hw >> 11) & 1u;
        const unsigned rt = (hw >> 8) & 7u;
        const uint32_t a = reg.r[13] + ((hw & 0xFFu) << 2);
        uint32_t v = 0;
        if (load) { if (mem_read(a, 4, v)) set_reg(rt, v); }
        else      { mem_write(a, 4, rd_(rt)); }
        cycles_ = 2;
        return;
    }

    // -----------------------------------------------------------------------
    // §1.6 Generación de direcciones y ajuste de SP
    // -----------------------------------------------------------------------
    if ((hw >> 12) == 0xA) {
        const unsigned rd = (hw >> 8) & 7u;
        const uint32_t imm = (hw & 0xFFu) << 2;
        if ((hw >> 11) & 1u) set_reg(rd, reg.r[13] + imm);          // ADD Rd,SP,#imm
        else                 set_reg(rd, ((cur_pc_ + 4u) & ~3u) + imm);  // ADR
        return;
    }

    // -----------------------------------------------------------------------
    // §1.7 Misceláneas (1011)
    // -----------------------------------------------------------------------
    if ((hw >> 12) == 0xB) {
        // ADD/SUB SP,SP,#imm7*4
        if ((hw & 0xFF00u) == 0xB000u) {
            const uint32_t imm = (hw & 0x7Fu) << 2;
            reg.r[13] = ((hw >> 7) & 1u) ? (reg.r[13] - imm) : (reg.r[13] + imm);
            return;
        }
        // CBZ / CBNZ
        if ((hw & 0xF500u) == 0xB100u) {
            const unsigned rn = hw & 7u;
            const uint32_t imm = (((hw >> 9) & 1u) << 6) | (((hw >> 3) & 0x1Fu) << 1);
            const bool nz = (hw >> 11) & 1u;
            if ((rd_(rn) == 0) != nz) branch_to(cur_pc_ + 4u + imm);
            return;
        }
        // Extensiones de signo/cero
        if ((hw & 0xFF00u) == 0xB200u) {
            const unsigned rd = hw & 7u, rm = (hw >> 3) & 7u;
            const uint32_t v = rd_(rm);
            switch ((hw >> 6) & 3u) {
                case 0: set_reg(rd, sign_extend(v & 0xFFFFu, 16)); break;  // SXTH
                case 1: set_reg(rd, sign_extend(v & 0xFFu, 8)); break;     // SXTB
                case 2: set_reg(rd, v & 0xFFFFu); break;                   // UXTH
                default: set_reg(rd, v & 0xFFu); break;                    // UXTB
            }
            return;
        }
        // REV / REV16 / REVSH
        if ((hw & 0xFF00u) == 0xBA00u) {
            const unsigned rd = hw & 7u, rm = (hw >> 3) & 7u;
            const uint32_t v = rd_(rm);
            switch ((hw >> 6) & 3u) {
                case 0: set_reg(rd, rev32(v)); break;
                case 1: set_reg(rd, rev16_pair(v)); break;
                case 3: set_reg(rd, revsh(v)); break;
                default: undefined(); break;
            }
            return;
        }
        // PUSH / POP
        if ((hw & 0xFE00u) == 0xB400u || (hw & 0xFE00u) == 0xBC00u) {
            const bool pop = (hw >> 11) & 1u;
            uint32_t list = hw & 0xFFu;
            if ((hw >> 8) & 1u) list |= pop ? (1u << 15) : (1u << 14);
            const unsigned n = bit_count(list);
            cycles_ = 1 + n;
            if (pop) {
                uint32_t a = reg.r[13];
                for (unsigned i = 0; i < 15; ++i)
                    if ((list >> i) & 1u) {
                        uint32_t v = 0;
                        if (!mem_read(a, 4, v, true)) return;
                        reg.r[i] = v; a += 4;
                    }
                reg.r[13] = a + (((list >> 15) & 1u) ? 4u : 0u);
                if ((list >> 15) & 1u) {
                    uint32_t v = 0;
                    if (!mem_read(a, 4, v, true)) return;
                    load_write_pc(v);
                    cycles_ += 2;
                }
            } else {
                uint32_t a = reg.r[13] - 4u * n;
                reg.r[13] = a;
                for (unsigned i = 0; i < 15; ++i)
                    if ((list >> i) & 1u) {
                        if (!mem_write(a, 4, reg.r[i], true)) return;
                        a += 4;
                    }
            }
            return;
        }
        // CPS
        if ((hw & 0xFFE0u) == 0xB660u) {
            const bool disable = (hw >> 4) & 1u;
            if (reg.privileged()) {
                if (hw & 2u) reg.primask   = disable ? 1u : 0u;
                if (hw & 1u) reg.faultmask = disable ? 1u : 0u;
            }
            return;
        }
        if ((hw & 0xFF00u) == 0xBE00u) {          // BKPT
            // Con el depurador habilitado (DHCSR.C_DEBUGEN) el nucleo SE PARA
            // en la propia instruccion: la direccion de retorno de depuracion
            // es la del BKPT, no la siguiente [IR, §13.4]. Es tambien lo que
            // hace un comparador del FPB, porque inyecta este mismo opcode.
            if (dbg && dbg->dbg_enabled()) {
                dbg->dbg_request_halt(DFSR_BKPT);
                next_pc_ = cur_pc_;               // no avanza
                return;
            }
            // Sin depurador conectado se comporta como HardFault de depuración
            sys->set_hfsr(1u << 31);              // HFSR.DEBUGEVT
            take_fault(EXC_HARDFAULT, 0, false, 0);
            return;
        }
        if ((hw & 0xFF00u) == 0xBF00u) {          // IT y hints
            const unsigned mask = hw & 0xFu;
            if (mask != 0) {                      // IT
                reg.set_itstate(uint8_t(hw & 0xFFu));
                it_just_set_ = true;
                return;
            }
            switch ((hw >> 4) & 0xFu) {
                case 0: break;                                        // NOP
                case 1: break;                                        // YIELD
                case 2:                                               // WFE
                    if (event_reg_ || event_in.read()) { event_reg_ = false; }
                    else { sleeping_state_ = true; sleep_wfe_ = true;
                           o_sleepdeep_ = sys->scr_sleepdeep(); publish(); }
                    return;
                case 3:                                               // WFI
                    if (!sys->any_pending()) {
                        sleeping_state_ = true;
                        sleep_wfe_ = false;
                        o_sleepdeep_ = sys->scr_sleepdeep();
                        publish();
                    }
                    return;
                case 4:                                               // SEV
                    event_reg_ = true;
                    o_event_ = true; publish();
                    o_event_ = false; publish();
                    return;
                default: break;
            }
            return;
        }
        undefined();
        return;
    }

    // -----------------------------------------------------------------------
    // §1.8 Múltiples, saltos y SVC
    // -----------------------------------------------------------------------
    if ((hw >> 12) == 0xC) {                     // STMIA / LDMIA
        const bool load = (hw >> 11) & 1u;
        const unsigned rn = (hw >> 8) & 7u;
        const uint32_t list = hw & 0xFFu;
        uint32_t a = rd_(rn);
        const unsigned n = bit_count(list);
        cycles_ = 1 + n;
        for (unsigned i = 0; i < 8; ++i)
            if ((list >> i) & 1u) {
                if (load) { uint32_t v = 0; if (!mem_read(a, 4, v, true)) return; reg.r[i] = v; }
                else      { if (!mem_write(a, 4, reg.r[i], true)) return; }
                a += 4;
            }
        // Write-back: en LDM solo si Rn no está en la lista [II, §1.8]
        if (!load || !((list >> rn) & 1u)) set_reg(rn, a);
        return;
    }
    if ((hw >> 12) == 0xD) {
        const unsigned cond = (hw >> 8) & 0xFu;
        if (cond == 0xE) { undefined(); return; }            // UDF
        if (cond == 0xF) {                                   // SVC
            // SVCall no es un fault, pero comparte el camino de escalado: si su
            // prioridad no puede apropiarse de la ejecución actual, se produce
            // HardFault [IR, §9.5].
            aborted_        = true;
            ret_next_instr_ = true;      // se retorna a la instrucción siguiente
            fault_exc_      = sys->raise_fault(EXC_SVCALL, 0, false, 0, reg);
            return;
        }
        if (condition_passed(cond, reg)) {
            branch_to(cur_pc_ + 4u + sign_extend(hw & 0xFFu, 8) * 2u);
            cycles_ = 3;
        }
        return;
    }
    if ((hw >> 11) == 0x1C) {                                // B (imm11)
        branch_to(cur_pc_ + 4u + sign_extend(hw & 0x7FFu, 11) * 2u);
        cycles_ = 3;
        return;
    }
    undefined();
}

} // namespace stm32
#endif // STM32_CORE_CPU_EXEC16_H
