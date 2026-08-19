// =============================================================================
// fpu.h — Unidad de coma flotante FPv4-SP (precisión simple)
//
// Unidad funcional invocada por la CPU (llamada a método, no bus). Ejecuta las
// instrucciones V* de [II, §5] sobre RegFile.s[]/fpscr respetando FPSCR
// (RMode/FZ/DN) y los flags acumulativos IEEE 754. La gestión de contexto
// (CPACR, FPCCR/FPCAR, lazy stacking) es de SCS+CPU [IR, §8.12/8.13].
// Latencias: VDIV/VSQRT 14 ciclos [IR, §8.9; II, §7].
//
// Nota de modelado: las operaciones se realizan con el `float` del anfitrión,
// que en toda plataforma soportada es IEEE 754 binary32 con el mismo
// comportamiento de NaN, infinitos y redondeo al más cercano. El modo de
// redondeo distinto del predeterminado y flush-to-zero se aplican
// explícitamente. Las FPU con FMA fusionada (VFMA/VFMS/VFNMA/VFNMS) usan
// std::fmaf para no introducir redondeo intermedio [II, §5.2].
// =============================================================================
#ifndef STM32_CORE_FPU_H
#define STM32_CORE_FPU_H

#include <systemc>
#include <cmath>
#include <cfenv>
#include <cstring>
#include "cpu_state.h"

namespace stm32 {

// Bits de FPSCR [II, §5.1; IR, §8.12]
enum FpscrBits : uint32_t {
    FPSCR_IOC = 1u << 0,   // operación inválida
    FPSCR_DZC = 1u << 1,   // división por cero
    FPSCR_OFC = 1u << 2,   // overflow
    FPSCR_UFC = 1u << 3,   // underflow
    FPSCR_IXC = 1u << 4,   // inexacta
    FPSCR_IDC = 1u << 7,   // entrada subnormal
    FPSCR_RMODE = 3u << 22,
    FPSCR_FZ  = 1u << 24,  // flush-to-zero
    FPSCR_DN  = 1u << 25,  // default NaN
    FPSCR_AHP = 1u << 26,
    FPSCR_V   = 1u << 28, FPSCR_C = 1u << 29, FPSCR_Z = 1u << 30, FPSCR_N = 1u << 31
};

// ---------------------------------------------------------------------------
// Núcleo funcional de la FPU (C++ puro; lo invoca la CPU)
// ---------------------------------------------------------------------------
class FpuCore {
public:
    // Resultado de ejecutar una instrucción FP
    struct Result { bool ok = false; unsigned cycles = 1; };

    // Ejecuta una instrucción del espacio de coprocesador 10/11.
    // Devuelve ok=false si la codificación no corresponde a ninguna V*.
    // Las cargas/almacenamientos FP los resuelve la CPU (necesita el bus): esta
    // función cubre aritmética, transferencias y comparaciones.
    Result execute(uint32_t hw1, uint32_t hw2, RegFile& reg);

    // --- Acceso a registros S como float ------------------------------------
    static float  as_f32(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
    static uint32_t as_u32(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

private:
    // Numeración de registros S: Sd = Vd:D, Sn = Vn:N, Sm = Vm:M [II, §5.1]
    static unsigned sreg(unsigned v4, unsigned bit) { return (v4 << 1) | bit; }

    // --- Envoltura de las operaciones con actualización de FPSCR ------------
    float finish(RegFile& reg, float r, float a, float b, bool div = false);
    static bool is_snan(float f) {
        const uint32_t b = as_u32(f);
        return ((b & 0x7F800000u) == 0x7F800000u) && (b & 0x007FFFFFu) &&
               !((b >> 22) & 1u);
    }
    static bool is_subnormal(float f) {
        const uint32_t b = as_u32(f);
        return (b & 0x7F800000u) == 0 && (b & 0x007FFFFFu) != 0;
    }
    float flush_in(RegFile& reg, float f) {         // flush-to-zero de entrada
        if (!(reg.fpscr & FPSCR_FZ)) return f;
        if (is_subnormal(f)) { reg.fpscr |= FPSCR_IDC; return std::signbit(f) ? -0.0f : 0.0f; }
        return f;
    }
    float flush_out(RegFile& reg, float f) {        // flush-to-zero de salida
        if ((reg.fpscr & FPSCR_FZ) && is_subnormal(f)) {
            reg.fpscr |= FPSCR_UFC | FPSCR_IXC;
            return std::signbit(f) ? -0.0f : 0.0f;
        }
        return f;
    }
    static constexpr uint32_t DEFAULT_NAN = 0x7FC00000u;

    void vcmp(RegFile& reg, float a, float b, bool quiet_raises);
    // Expansión del inmediato de VMOV.F32 [II, §5.3]
    static float expand_imm(unsigned imm8);
};

// ---------------------------------------------------------------------------
inline float FpuCore::expand_imm(unsigned imm8) {
    // VFPExpandImm: sign:not(b):bbbbb:cdefgh seguido de 19 ceros
    const uint32_t sign = (imm8 >> 7) & 1u;
    const uint32_t b    = (imm8 >> 6) & 1u;
    const uint32_t cdef = (imm8 >> 0) & 0x3Fu;
    const uint32_t exp  = ((b ? 0u : 1u) << 7) | (b ? 0x7Cu : 0x00u) | ((imm8 >> 4) & 0x3u);
    // exp de 8 bits = NOT(b) : b:b:b:b:b : imm8<5:4>
    const uint32_t e8 = ((b ^ 1u) << 7) | ((b ? 0x1Fu : 0x00u) << 2) | ((imm8 >> 4) & 0x3u);
    (void)exp;
    const uint32_t bits = (sign << 31) | (e8 << 23) | ((cdef & 0xFu) << 19);
    return as_f32(bits);
}

inline float FpuCore::finish(RegFile& reg, float r, float a, float b, bool div) {
    // Flags acumulativos de excepción IEEE 754 [II, §5.1]
    if (std::isnan(r) && !std::isnan(a) && !std::isnan(b)) reg.fpscr |= FPSCR_IOC;
    if (is_snan(a) || is_snan(b)) reg.fpscr |= FPSCR_IOC;
    if (div && b == 0.0f && !std::isnan(a) && std::isfinite(a) && a != 0.0f)
        reg.fpscr |= FPSCR_DZC;
    if (std::isinf(r) && std::isfinite(a) && std::isfinite(b))
        reg.fpscr |= FPSCR_OFC | FPSCR_IXC;
    if (std::isnan(r) && (reg.fpscr & FPSCR_DN)) return as_f32(DEFAULT_NAN);
    return flush_out(reg, r);
}

inline void FpuCore::vcmp(RegFile& reg, float a, float b, bool quiet_raises) {
    uint32_t nzcv;
    if (std::isnan(a) || std::isnan(b)) {
        nzcv = FPSCR_C | FPSCR_V;                          // unordered [II, §5.3]
        if (quiet_raises || is_snan(a) || is_snan(b)) reg.fpscr |= FPSCR_IOC;
    } else if (a == b) {
        nzcv = FPSCR_Z | FPSCR_C;                          // igual
    } else if (a < b) {
        nzcv = FPSCR_N;                                    // menor
    } else {
        nzcv = FPSCR_C;                                    // mayor
    }
    reg.fpscr = (reg.fpscr & ~(FPSCR_N | FPSCR_Z | FPSCR_C | FPSCR_V)) | nzcv;
}

// ---------------------------------------------------------------------------
inline FpuCore::Result FpuCore::execute(uint32_t hw1, uint32_t hw2, RegFile& reg) {
    Result res;
    // Todas las V* aritméticas y de transferencia tienen hw2[11:8] = 1010 (cp10)
    // o 1011 (cp11, transferencias de 64 bits) [II, §5.1].
    const unsigned coproc = (hw2 >> 8) & 0xFu;
    if (coproc != 0xA && coproc != 0xB) return res;

    const unsigned D = (hw1 >> 6) & 1u, N = (hw2 >> 7) & 1u, M = (hw2 >> 5) & 1u;
    const unsigned Vn = hw1 & 0xFu, Vd = (hw2 >> 12) & 0xFu, Vm = hw2 & 0xFu;
    const unsigned sd = sreg(Vd, D), sn = sreg(Vn, N), sm = sreg(Vm, M);

    // ---- 5.4 Transferencias núcleo <-> FP ---------------------------------
    // VMOV Sn,Rt / VMOV Rt,Sn: hw2 = Rt 1010 N 001 0000 (hw2[6:0] = 0010000)
    if ((hw1 & 0xFF00u) == 0xEE00u && ((hw2 & 0x007Fu) == 0x0010u) &&
        ((hw1 & 0x00F0u) == 0x0000u || (hw1 & 0x00F0u) == 0x0010u)) {
        const unsigned rt = (hw2 >> 12) & 0xFu;
        const unsigned sn2 = sreg(hw1 & 0xFu, (hw2 >> 7) & 1u);
        if ((hw1 >> 4) & 1u) reg.r[rt]   = reg.s[sn2];      // VMOV Rt,Sn
        else                 reg.s[sn2]  = reg.r[rt];       // VMOV Sn,Rt
        res.ok = true; return res;
    }
    // VMRS / VMSR [II, §5.4]
    if ((hw1 & 0xFFF0u) == 0xEEF0u && (hw1 & 0xFu) == 0x1u &&
        (hw2 & 0x0FFFu) == 0x0A10u) {                       // VMRS Rt, FPSCR
        const unsigned rt = (hw2 >> 12) & 0xFu;
        if (rt == 15) {                                     // APSR_nzcv
            reg.xpsr = (reg.xpsr & 0x0FFFFFFFu) | (reg.fpscr & 0xF0000000u);
        } else {
            reg.r[rt] = reg.fpscr;
        }
        res.ok = true; return res;
    }
    if ((hw1 & 0xFFF0u) == 0xEEE0u && (hw1 & 0xFu) == 0x1u &&
        (hw2 & 0x0FFFu) == 0x0A10u) {                       // VMSR FPSCR, Rt
        reg.fpscr = reg.r[(hw2 >> 12) & 0xFu];
        res.ok = true; return res;
    }
    // VMOV de dos registros core <-> dos S consecutivos [II, §5.4]
    if ((hw1 & 0xFFE0u) == 0xEC40u && ((hw2 & 0x0FD0u) == 0x0A10u)) {
        const unsigned rt2 = hw1 & 0xFu, rt = (hw2 >> 12) & 0xFu;
        const unsigned m0 = sreg(hw2 & 0xFu, (hw2 >> 5) & 1u);
        if ((hw1 >> 4) & 1u) { reg.r[rt] = reg.s[m0]; reg.r[rt2] = reg.s[m0 + 1]; }
        else                 { reg.s[m0] = reg.r[rt]; reg.s[m0 + 1] = reg.r[rt2]; }
        res.ok = true; return res;
    }
    // VMOV de dos registros core <-> un registro D de 64 bits [II, §5.4].
    // En FPv4-SP los registros D solo existen para transferencias y cargas de
    // 64 bits: Dm = M:Vm y ocupa la pareja S[2m], S[2m+1] (mitad baja primero).
    // La usan las rutinas de doble precisión por software del compilador.
    if ((hw1 & 0xFFE0u) == 0xEC40u && ((hw2 & 0x0FD0u) == 0x0B10u)) {
        const unsigned rt2 = hw1 & 0xFu, rt = (hw2 >> 12) & 0xFu;
        const unsigned dm  = (((hw2 >> 5) & 1u) << 4) | (hw2 & 0xFu);
        const unsigned s0  = dm * 2u;
        if (s0 + 1 >= 32) return res;
        if ((hw1 >> 4) & 1u) { reg.r[rt] = reg.s[s0]; reg.r[rt2] = reg.s[s0 + 1]; }
        else                 { reg.s[s0] = reg.r[rt]; reg.s[s0 + 1] = reg.r[rt2]; }
        res.ok = true; return res;
    }

    // El resto del espacio son operaciones de datos: hw1[15:8] = 0xEE y
    // hw2[4] = 0. El campo hw1[7:4] es opA:D:opB [II, §5.2].
    if ((hw1 & 0xFF00u) != 0xEE00u || (hw2 & 0x0010u) != 0) return res;
    const unsigned opA = (hw1 >> 7) & 1u;
    const unsigned opB = (hw1 >> 4) & 3u;

    // ---- 5.2 Aritmética de 3 registros -------------------------------------
    if (!(opA == 1 && opB == 3)) {
        const unsigned op  = (hw2 >> 6) & 1u;              // hw2[6]
        const float a = flush_in(reg, as_f32(reg.s[sn]));
        const float b = flush_in(reg, as_f32(reg.s[sm]));
        const float d = flush_in(reg, as_f32(reg.s[sd]));
        float r; bool handled = true; unsigned cyc = 1;
        if (opA == 0) {
            switch (opB) {
                case 0: r = op ? (d - a * b) : (d + a * b); break;   // VMLA/VMLS
                case 1: r = op ? (-d - a * b) : (-d + a * b); break; // VNMLA/VNMLS
                case 2: r = op ? -(a * b) : (a * b); break;          // VMUL/VNMUL
                default: r = op ? (a - b) : (a + b); break;          // VADD/VSUB
            }
        } else {
            switch (opB) {
                case 0: r = a / b; cyc = 14; break;                  // VDIV
                case 1: r = op ? std::fmaf(-a, b, -d) : std::fmaf(a, b, -d); break; // VFNMA/VFNMS
                default: r = op ? std::fmaf(-a, b, d) : std::fmaf(a, b, d); break;  // VFMS/VFMA
            }
        }
        if (handled) {
            reg.s[sd] = as_u32(finish(reg, r, a, b, opA == 1 && opB == 0));
            res.ok = true; res.cycles = cyc;
            return res;
        }
    }

    // ---- 5.3 Dos registros / inmediato (opA opB = 1 D 11) ------------------
    {
        const unsigned opc2 = hw1 & 0xFu;
        const unsigned opc3 = (hw2 >> 6) & 3u;
        if (opc3 == 0) {                                   // VMOV.F32 Sd,#imm
            const unsigned imm8 = ((hw1 & 0xFu) << 4) | (hw2 & 0xFu);
            reg.s[sd] = as_u32(expand_imm(imm8));
            res.ok = true;
            return res;
        }
        const float m = flush_in(reg, as_f32(reg.s[sm]));
        switch (opc2) {
            case 0x0:
                if (opc3 == 1) { reg.s[sd] = reg.s[sm]; res.ok = true; }        // VMOV
                else if (opc3 == 3) { reg.s[sd] = reg.s[sm] & 0x7FFFFFFFu; res.ok = true; } // VABS
                break;
            case 0x1:
                if (opc3 == 1) { reg.s[sd] = reg.s[sm] ^ 0x80000000u; res.ok = true; }      // VNEG
                else if (opc3 == 3) {                                                       // VSQRT
                    const float r = std::sqrt(m);
                    if (m < 0.0f) reg.fpscr |= FPSCR_IOC;
                    reg.s[sd] = as_u32(finish(reg, r, m, m));
                    res.ok = true; res.cycles = 14;
                }
                break;
            case 0x4:                                                                       // VCMP{E} Sd,Sm
                vcmp(reg, flush_in(reg, as_f32(reg.s[sd])), m, opc3 == 3);
                res.ok = true;
                break;
            case 0x5:                                                                       // VCMP{E} Sd,#0.0
                vcmp(reg, flush_in(reg, as_f32(reg.s[sd])), 0.0f, opc3 == 3);
                res.ok = true;
                break;
            case 0x8: {                                                                     // VCVT.F32.<S|U>32
                const uint32_t bits = reg.s[sm];
                const float r = (opc3 == 3) ? float(int32_t(bits)) : float(bits);
                reg.s[sd] = as_u32(r);
                res.ok = true;
                break;
            }
            case 0xC: case 0xD: {                                                           // VCVT{R}.<S|U>32.F32
                const bool to_signed = (opc2 == 0xD);
                const bool truncate  = (opc3 == 3);                                         // VCVT trunca
                float x = m;
                if (!truncate) x = std::nearbyint(x);
                else           x = std::trunc(x);
                int64_t iv;
                if (std::isnan(x)) { iv = 0; reg.fpscr |= FPSCR_IOC; }
                else if (to_signed) {
                    if (x >= 2147483648.0f) { iv = 2147483647; reg.fpscr |= FPSCR_IOC; }
                    else if (x < -2147483648.0f) { iv = int64_t(INT32_MIN); reg.fpscr |= FPSCR_IOC; }
                    else iv = int64_t(x);
                } else {
                    if (x >= 4294967296.0f) { iv = 0xFFFFFFFFll; reg.fpscr |= FPSCR_IOC; }
                    else if (x < 0.0f) { iv = 0; reg.fpscr |= FPSCR_IOC; }
                    else iv = int64_t(x);
                }
                if (x != m) reg.fpscr |= FPSCR_IXC;
                reg.s[sd] = uint32_t(iv);
                res.ok = true;
                break;
            }
            default: break;
        }
    }
    return res;
}

// ---------------------------------------------------------------------------
// Envoltura SystemC: mantiene el puerto de IRQ del netlist [IR, §9.1.2].
// ---------------------------------------------------------------------------
SC_MODULE(Fpu) {
    sc_core::sc_in<bool>  fclk{"fclk"};
    sc_core::sc_in<bool>  rst_n{"rst_n"};
    sc_core::sc_out<bool> irq_fpu{"irq_fpu"};   // IRQ 81 (excepciones FP)

    FpuCore core;

    SC_CTOR(Fpu) {
        SC_METHOD(irq_proc);
        sensitive << irq_ev_;
        dont_initialize();
    }

    // La CPU informa del estado de los flags acumulativos tras cada V*.
    // El F407 encamina las excepciones de coma flotante a la IRQ 81; el
    // enmascaramiento por bits de habilitación no existe en FPv4-SP (no hay
    // trampas IEEE), de modo que la línea refleja los flags acumulativos.
    void update_flags(uint32_t fpscr) {
        const bool any = (fpscr & (FPSCR_IOC | FPSCR_DZC | FPSCR_OFC |
                                   FPSCR_UFC | FPSCR_IXC | FPSCR_IDC)) != 0;
        if (any != o_irq_) { o_irq_ = any; irq_ev_.notify(sc_core::SC_ZERO_TIME); }
    }

private:
    bool o_irq_ = false;
    sc_core::sc_event irq_ev_;
    void irq_proc() { irq_fpu.write(o_irq_); }
};

} // namespace stm32
#endif // STM32_CORE_FPU_H
