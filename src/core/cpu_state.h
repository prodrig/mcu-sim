// =============================================================================
// cpu_state.h — Estado arquitectónico ARMv7E-M y primitivas de la ISA
//
// Contiene solo C++ puro (sin SystemC): el banco de registros [IR, §7.2-§7.4],
// la numeración de excepciones [IR, §9.1.2] y las pseudofunciones comunes del
// conjunto de instrucciones [II, §0.3]: AddWithCarry, Shift_C,
// ThumbExpandImm_C, SignedSatQ/UnsignedSatQ, evaluación de condición.
// Al no depender de SystemC puede probarse de forma aislada.
// =============================================================================
#ifndef STM32_CORE_CPU_STATE_H
#define STM32_CORE_CPU_STATE_H

#include <cstdint>
#include <cstring>

namespace stm32 {

// ---------------------------------------------------------------------------
// Números de excepción [IR, §9.1.2]. IRQn ocupa la posición 16+n.
// ---------------------------------------------------------------------------
enum ExcNum : int {
    EXC_NONE       = 0,     // modo Thread
    EXC_RESET      = 1,
    EXC_NMI        = 2,
    EXC_HARDFAULT  = 3,
    EXC_MEMMANAGE  = 4,
    EXC_BUSFAULT   = 5,
    EXC_USAGEFAULT = 6,
    EXC_SVCALL     = 11,
    EXC_DEBUGMON   = 12,
    EXC_PENDSV     = 14,
    EXC_SYSTICK    = 15,
    EXC_IRQ0       = 16
};

// Prioridades fijas [IR, §9.1.2]
constexpr int PRIO_RESET     = -3;
constexpr int PRIO_NMI       = -2;
constexpr int PRIO_HARDFAULT = -1;
constexpr int PRIO_NONE      = 256;    // "sin excepción activa"

// Bits de EXC_RETURN [IR, §7.5]
constexpr uint32_t EXC_RET_HANDLER_MSP     = 0xFFFFFFF1u;
constexpr uint32_t EXC_RET_THREAD_MSP      = 0xFFFFFFF9u;
constexpr uint32_t EXC_RET_THREAD_PSP      = 0xFFFFFFFDu;
constexpr uint32_t EXC_RET_HANDLER_MSP_FP  = 0xFFFFFFE1u;
constexpr uint32_t EXC_RET_THREAD_MSP_FP   = 0xFFFFFFE9u;
constexpr uint32_t EXC_RET_THREAD_PSP_FP   = 0xFFFFFFEDu;

// ---------------------------------------------------------------------------
// Estado arquitectónico [IR, §7.2-§7.4; II, §0]
// ---------------------------------------------------------------------------
struct RegFile {
    uint32_t r[16]  = {0};         // R0-R15; r[13] es el SP activo, r[15] el PC
    uint32_t msp = 0, psp = 0;     // R13 bancado [IR, §7.2.2]
    uint32_t xpsr = 0x01000000u;   // T = 1 [IR, §7.3.3]
    uint8_t  primask = 0, faultmask = 0, basepri = 0, control = 0;
    bool     handler_mode = false;
    // FPv4-SP [II, §5]
    uint32_t s[32]  = {0};
    uint32_t fpscr  = 0;

    // --- Accesos con nombre -------------------------------------------------
    uint32_t& sp()       { return r[13]; }
    uint32_t& lr()       { return r[14]; }
    uint32_t& pc()       { return r[15]; }
    uint32_t  sp() const { return r[13]; }
    uint32_t  lr() const { return r[14]; }
    uint32_t  pc() const { return r[15]; }

    // --- Flags de APSR [IR, §7.3.1] -----------------------------------------
    bool n() const { return (xpsr >> 31) & 1u; }
    bool z() const { return (xpsr >> 30) & 1u; }
    bool c() const { return (xpsr >> 29) & 1u; }
    bool v() const { return (xpsr >> 28) & 1u; }
    bool q() const { return (xpsr >> 27) & 1u; }
    void set_n(bool b) { set_bit(31, b); }
    void set_z(bool b) { set_bit(30, b); }
    void set_c(bool b) { set_bit(29, b); }
    void set_v(bool b) { set_bit(28, b); }
    void set_q(bool b) { if (b) xpsr |= (1u << 27); }     // sticky [II, §0.4]
    uint32_t ge() const { return (xpsr >> 16) & 0xFu; }
    void set_ge(uint32_t g) { xpsr = (xpsr & ~(0xFu << 16)) | ((g & 0xFu) << 16); }

    void set_nz(uint32_t res) { set_n((res >> 31) & 1u); set_z(res == 0); }

    // --- IPSR / EPSR --------------------------------------------------------
    unsigned ipsr() const { return xpsr & 0x1FFu; }
    void set_ipsr(unsigned e) { xpsr = (xpsr & ~0x1FFu) | (e & 0x1FFu); }
    bool thumb() const { return (xpsr >> 24) & 1u; }
    void set_thumb(bool t) { set_bit(24, t); }

    // ITSTATE se guarda en EPSR: bits [26:25] = IT[1:0], [15:10] = IT[7:2]
    uint8_t itstate() const {
        return uint8_t((((xpsr >> 25) & 0x3u)) | (((xpsr >> 10) & 0x3Fu) << 2));
    }
    void set_itstate(uint8_t it) {
        xpsr = (xpsr & ~((0x3u << 25) | (0x3Fu << 10)))
             | (uint32_t(it & 0x3u) << 25)
             | ((uint32_t(it) >> 2) << 10);
    }
    bool in_it_block() const { return (itstate() & 0xF) != 0; }
    // Última instrucción del bloque IT (mask == 0b1000)
    bool last_in_it_block() const { return (itstate() & 0xF) == 0x8; }
    // Avance de ITSTATE al terminar una instrucción [II, §1.7]
    void it_advance() {
        const uint8_t it = itstate();
        if ((it & 0x7) == 0) set_itstate(0);
        else set_itstate(uint8_t((it & 0xE0) | ((it << 1) & 0x1F)));
    }

    // --- CONTROL [IR, §7.4.4] ------------------------------------------------
    bool npriv() const  { return control & 1u; }
    bool spsel() const  { return (control >> 1) & 1u; }
    bool fpca() const   { return (control >> 2) & 1u; }
    void set_fpca(bool b) { control = uint8_t(b ? (control | 4u) : (control & ~4u)); }
    // Privilegio efectivo: siempre privilegiado en modo Handler [IR, §7.1]
    bool privileged() const { return handler_mode || !npriv(); }

    // Selección de pila activa: Handler siempre MSP [IR, §7.1.2]
    bool using_psp() const { return !handler_mode && spsel(); }
    void load_sp()  { r[13] = using_psp() ? psp : msp; }
    void store_sp() { (using_psp() ? psp : msp) = r[13]; }

private:
    void set_bit(unsigned b, bool v_) {
        xpsr = v_ ? (xpsr | (1u << b)) : (xpsr & ~(1u << b));
    }
};

// ---------------------------------------------------------------------------
// Pseudofunciones comunes [II, §0.3]
// ---------------------------------------------------------------------------
struct AddResult { uint32_t result; bool carry; bool overflow; };

inline AddResult add_with_carry(uint32_t x, uint32_t y, bool carry_in) {
    const uint64_t usum = uint64_t(x) + uint64_t(y) + (carry_in ? 1u : 0u);
    const int64_t  ssum = int64_t(int32_t(x)) + int64_t(int32_t(y)) + (carry_in ? 1 : 0);
    AddResult a;
    a.result   = uint32_t(usum);
    a.carry    = (uint64_t(a.result) != usum);
    a.overflow = (int64_t(int32_t(a.result)) != ssum);
    return a;
}

enum ShiftType : uint8_t { SRT_LSL = 0, SRT_LSR = 1, SRT_ASR = 2, SRT_ROR = 3, SRT_RRX = 4 };

struct ShiftResult { uint32_t result; bool carry; };

inline uint32_t ror32(uint32_t v, unsigned n) {
    n &= 31u;
    return n ? ((v >> n) | (v << (32 - n))) : v;
}

inline ShiftResult shift_c(uint32_t value, ShiftType type, unsigned amount, bool carry_in) {
    ShiftResult s{value, carry_in};
    if (type == SRT_RRX) {
        s.carry  = value & 1u;
        s.result = (uint32_t(carry_in) << 31) | (value >> 1);
        return s;
    }
    if (amount == 0) return s;                 // sin desplazamiento: carry intacto
    switch (type) {
        case SRT_LSL:
            if (amount > 32) { s.result = 0; s.carry = false; }
            else if (amount == 32) { s.carry = value & 1u; s.result = 0; }
            else { s.carry = (value >> (32 - amount)) & 1u; s.result = value << amount; }
            break;
        case SRT_LSR:
            if (amount > 32) { s.result = 0; s.carry = false; }
            else if (amount == 32) { s.carry = (value >> 31) & 1u; s.result = 0; }
            else { s.carry = (value >> (amount - 1)) & 1u; s.result = value >> amount; }
            break;
        case SRT_ASR: {
            const int32_t sv = int32_t(value);
            if (amount >= 32) { s.carry = (value >> 31) & 1u; s.result = uint32_t(sv >> 31); }
            else { s.carry = (value >> (amount - 1)) & 1u; s.result = uint32_t(sv >> int(amount)); }
            break;
        }
        case SRT_ROR: {
            const unsigned m = amount & 31u;
            if (m == 0) { s.result = value; s.carry = (value >> 31) & 1u; }
            else { s.result = ror32(value, m); s.carry = (s.result >> 31) & 1u; }
            break;
        }
        default: break;
    }
    return s;
}

// DecodeImmShift [II, §2.3]: type de 2 bits + cantidad de 5 bits
struct ImmShift { ShiftType type; unsigned amount; };
inline ImmShift decode_imm_shift(unsigned type2, unsigned imm5) {
    switch (type2) {
        case 0: return {SRT_LSL, imm5};
        case 1: return {SRT_LSR, imm5 ? imm5 : 32u};
        case 2: return {SRT_ASR, imm5 ? imm5 : 32u};
        default: return imm5 ? ImmShift{SRT_ROR, imm5} : ImmShift{SRT_RRX, 1u};
    }
}

// ThumbExpandImm_C [II, §0.3]
struct ExpandResult { uint32_t imm32; bool carry; };
inline ExpandResult thumb_expand_imm_c(uint32_t imm12, bool carry_in) {
    ExpandResult e{0, carry_in};
    if (((imm12 >> 10) & 0x3u) == 0) {
        const uint32_t b = imm12 & 0xFFu;
        switch ((imm12 >> 8) & 0x3u) {
            case 0: e.imm32 = b; break;
            case 1: e.imm32 = (b << 16) | b; break;
            case 2: e.imm32 = (b << 24) | (b << 8); break;
            default: e.imm32 = (b << 24) | (b << 16) | (b << 8) | b; break;
        }
    } else {
        const uint32_t unrot = 0x80u | (imm12 & 0x7Fu);
        e.imm32 = ror32(unrot, (imm12 >> 7) & 0x1Fu);
        e.carry = (e.imm32 >> 31) & 1u;
    }
    return e;
}

// Saturación [II, §0.3]
struct SatResult { int64_t result; bool sat; };
inline SatResult signed_sat_q(int64_t i, unsigned n) {
    const int64_t hi = (int64_t(1) << (n - 1)) - 1, lo = -(int64_t(1) << (n - 1));
    if (i > hi) return {hi, true};
    if (i < lo) return {lo, true};
    return {i, false};
}
inline SatResult unsigned_sat_q(int64_t i, unsigned n) {
    const int64_t hi = (n >= 63) ? int64_t(0x7FFFFFFFFFFFFFFFll) : ((int64_t(1) << n) - 1);
    if (i > hi) return {hi, true};
    if (i < 0)  return {0, true};
    return {i, false};
}

inline uint32_t sign_extend(uint32_t v, unsigned bits) {
    const uint32_t m = 1u << (bits - 1);
    return (v ^ m) - m;
}

inline unsigned count_leading_zeros(uint32_t v) {
    if (v == 0) return 32;
    unsigned n = 0;
    while (!(v & 0x80000000u)) { v <<= 1; ++n; }
    return n;
}

// REV / REV16 / REVSH / RBIT [II, §1.7, §4.2]
inline uint32_t rev32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}
inline uint32_t rev16_pair(uint32_t v) {
    return ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
}
inline uint32_t revsh(uint32_t v) {
    return sign_extend((((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu)), 16);
}
inline uint32_t rbit32(uint32_t v) {
    v = ((v & 0x55555555u) << 1) | ((v >> 1) & 0x55555555u);
    v = ((v & 0x33333333u) << 2) | ((v >> 2) & 0x33333333u);
    v = ((v & 0x0F0F0F0Fu) << 4) | ((v >> 4) & 0x0F0F0F0Fu);
    return rev32(v);
}

inline unsigned bit_count(uint32_t v) {
    unsigned n = 0;
    while (v) { n += v & 1u; v >>= 1; }
    return n;
}

// Evaluación de condición ARM [II, §1.8]
inline bool condition_passed(unsigned cond, const RegFile& reg) {
    bool r = false;
    switch (cond >> 1) {
        case 0: r = reg.z(); break;                                  // EQ
        case 1: r = reg.c(); break;                                  // CS
        case 2: r = reg.n(); break;                                  // MI
        case 3: r = reg.v(); break;                                  // VS
        case 4: r = reg.c() && !reg.z(); break;                      // HI
        case 5: r = reg.n() == reg.v(); break;                       // GE
        case 6: r = (reg.n() == reg.v()) && !reg.z(); break;         // GT
        case 7: r = true; break;                                     // AL
    }
    return (cond & 1u) && (cond != 0xF) ? !r : r;
}

} // namespace stm32
#endif // STM32_CORE_CPU_STATE_H
