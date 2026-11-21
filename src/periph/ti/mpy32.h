// =============================================================================
// mpy32.h — El multiplicador de 32 bits de los MSP430 (MPY32) [UG5 SLAU208Q
// cap. 5] (M4 de doc/analisis_msp430.md)
//
// Sin tiempo propio: el resultado está a la instrucción siguiente (el chip da
// 16x16 en 1 ciclo y 32x32 en 4 con MPYDLYWRTEN = 0; un programa que lee
// antes de tiempo lee lo viejo, y eso no se modela). Lo que importa es que
// las rutinas de multiplicar de libgcc (-mhwmult=f5series: __mspabi_mpyi_f5hw,
// __mspabi_mpyl_f5hw...) y las de DriverLib den lo que dan en el chip.
//
//   +00 MPY   +02 MPYS  +04 MAC   +06 MACS   el primer operando, 16 bits; la
//                                          palabra elige la operación
//   +08 OP2   el segundo, 16 bits: escribirlo la arranca
//   +0A RESLO +0C RESHI +0E SUMEXT
//   +10/+12 MPY32L/H  +14/+16 MPYS32L/H  +18/+1A MAC32L/H  +1C/+1E MACS32L/H
//   +20 OP2L  +22 OP2H (escribir OP2H la arranca con el segundo de 32 bits)
//   +24 RES0  +26 RES1  +28 RES2  +2A RES3   (RES0 y RES1 son RESLO y RESHI)
//   +2C MPY32CTL0: MPYC, MPYFRAC, MPYSAT, MPYM (la operación), OP1_32, OP2_32
//
// Una escritura de BYTE a un operando lo extiende: con signo en MPYS y MACS
// (y en OP2 si el primero es con signo), sin signo en los demás [§5.2.1].
// SUMEXT: 0 en MPY; el signo del resultado en MPYS y MACS; el acarreo de la
// suma en MAC. MPYFRAC desplaza el producto un bit a la izquierda; MPYSAT
// satura el resultado de MPYS/MACS al leerlo [§5.2.4-5.2.5].
// =============================================================================
#ifndef STM32_PERIPH_TI_MPY32_H
#define STM32_PERIPH_TI_MPY32_H

#include <cstdint>

namespace stm32 {

class Mpy32 {
public:
    enum Modo { MPY = 0, MPYS = 1, MAC = 2, MACS = 3 };

    void reset() { op1_ = 0; op2_ = 0; res_ = 0; sumext_ = 0; ctl_ = 0; modo_ = MPY; op2l_ = 0; desb_ = false; }

    uint16_t lee16(uint32_t off) const {
        switch (off & 0x3E) {
        case 0x00: case 0x02: case 0x04: case 0x06: return uint16_t(op1_);
        case 0x08: return uint16_t(op2_);
        case 0x0A: case 0x24: return uint16_t(resultado());
        case 0x0C: case 0x26: return uint16_t(resultado() >> 16);
        case 0x28: return uint16_t(resultado() >> 32);
        case 0x2A: return uint16_t(resultado() >> 48);
        case 0x0E: return sumext_;
        case 0x10: case 0x14: case 0x18: case 0x1C: return uint16_t(op1_);
        case 0x12: case 0x16: case 0x1A: case 0x1E: return uint16_t(op1_ >> 16);
        case 0x20: return uint16_t(op2_);
        case 0x22: return uint16_t(op2_ >> 16);
        case 0x2C: return uint16_t(ctl_ | (modo_ << 4));
        default: return 0;
        }
    }
    // `byte`: escritura de 8 bits (a la dirección par: el byte bajo)
    void escribe(uint32_t off, uint16_t v, bool byte) {
        const unsigned o = off & 0x3E;
        auto ext16 = [&](bool con_signo) -> uint32_t {
            if (!byte) return v;
            return con_signo ? uint32_t(int32_t(int8_t(v)) & 0xFFFF) : uint32_t(v & 0xFF);
        };
        switch (o) {
        case 0x00: case 0x02: case 0x04: case 0x06: {
            modo_ = Modo(o / 2);
            const bool s = modo_ == MPYS || modo_ == MACS;
            uint32_t x = ext16(s);
            // El operando de 16 bits, extendido a 32 si es con signo
            op1_ = s ? uint32_t(int32_t(int16_t(x))) : x;
            ctl_ &= uint16_t(~OP1_32);
            return;
        }
        case 0x08: {
            const bool s = modo_ == MPYS || modo_ == MACS;
            const uint32_t x = ext16(s);
            op2_ = s ? uint32_t(int32_t(int16_t(x))) : x;
            ctl_ &= uint16_t(~OP2_32);
            opera();
            return;
        }
        case 0x10: case 0x14: case 0x18: case 0x1C:
            modo_ = Modo((o - 0x10) / 4);
            op1_ = (op1_ & 0xFFFF0000u) | v;
            {   // si solo se escribe la parte baja, es un operando de 16 bits [§5.2.2]
                const bool s = modo_ == MPYS || modo_ == MACS;
                op1_ = s ? uint32_t(int32_t(int16_t(v))) : uint32_t(v);
            }
            ctl_ &= uint16_t(~OP1_32);
            return;
        case 0x12: case 0x16: case 0x1A: case 0x1E:
            modo_ = Modo((o - 0x12) / 4);
            op1_ = (op1_ & 0x0000FFFFu) | (uint32_t(v) << 16);
            ctl_ |= OP1_32;
            return;
        case 0x20: op2l_ = v; op2_ = v; ctl_ &= uint16_t(~OP2_32); return;
        case 0x22: op2_ = uint32_t(op2l_) | (uint32_t(v) << 16); ctl_ |= OP2_32; opera(); return;
        case 0x0A: case 0x24: res_ = (res_ & ~0xFFFFull) | v; return;
        case 0x0C: case 0x26: res_ = (res_ & ~(0xFFFFull << 16)) | (uint64_t(v) << 16); return;
        case 0x28: res_ = (res_ & ~(0xFFFFull << 32)) | (uint64_t(v) << 32); return;
        case 0x2A: res_ = (res_ & ~(0xFFFFull << 48)) | (uint64_t(v) << 48); return;
        case 0x0E: return;                                        // SUMEXT: solo lectura
        case 0x2C: ctl_ = uint16_t((ctl_ & (OP1_32 | OP2_32)) | (v & 0x030D & ~0x0030)); return;
        default: return;
        }
    }
    uint16_t ctl() const { return uint16_t(ctl_ | (modo_ << 4)); }

private:
    enum : uint16_t { MPYC = 0x01, MPYFRAC = 0x04, MPYSAT = 0x08, OP1_32 = 0x40, OP2_32 = 0x80 };
    uint32_t op1_ = 0, op2_ = 0;
    uint16_t op2l_ = 0;
    uint64_t res_ = 0;
    uint16_t sumext_ = 0, ctl_ = 0;
    bool desb_ = false;                       // la última operación con signo desbordó (lo que satura MPYSAT)
    Modo modo_ = MPY;

    bool ancho64() const { return (ctl_ & (OP1_32 | OP2_32)) != 0; }
    void opera() {
        const bool s = modo_ == MPYS || modo_ == MACS;
        const bool a1 = (ctl_ & OP1_32) != 0, a2 = (ctl_ & OP2_32) != 0;
        // Los operandos a su ancho, con signo si toca
        const int64_t x1 = s ? (a1 ? int64_t(int32_t(op1_)) : int64_t(int16_t(op1_))) : int64_t(a1 ? op1_ : (op1_ & 0xFFFF));
        const int64_t x2 = s ? (a2 ? int64_t(int32_t(op2_)) : int64_t(int16_t(op2_))) : int64_t(a2 ? op2_ : (op2_ & 0xFFFF));
        uint64_t p;
        if (s) p = uint64_t(x1 * x2);
        else p = uint64_t(x1) * uint64_t(x2);
        if (ctl_ & MPYFRAC) p <<= 1;
        const bool w = ancho64();
        const uint64_t mask = w ? ~0ull : 0xFFFFFFFFull;
        if (modo_ == MAC || modo_ == MACS) {
            const uint64_t ant = res_ & mask;
            const uint64_t suma = (ant + (p & mask));
            if (modo_ == MAC) {
                const bool c = w ? (suma < ant) : (suma > 0xFFFFFFFFull);
                res_ = suma & mask;
                sumext_ = c ? 1 : 0;
                ctl_ = uint16_t((ctl_ & ~MPYC) | (c ? MPYC : 0));
            } else {
                res_ = suma & mask;
                const bool neg = w ? (int64_t(res_) < 0) : ((res_ >> 31) & 1u);
                // el acarreo de la suma con signo (desbordamiento) va a MPYC
                const bool sa = w ? (int64_t(ant) < 0) : ((ant >> 31) & 1u);
                const bool sp = w ? (int64_t(p) < 0) : ((p >> 31) & 1u);
                const bool ovf = (sa == sp) && (sa != neg);
                sumext_ = neg ? 0xFFFF : 0;
                ctl_ = uint16_t((ctl_ & ~MPYC) | (ovf ? MPYC : 0));
                desb_ = ovf;
            }
        } else {
            res_ = p & mask;
            const bool neg = s && (w ? (int64_t(res_) < 0) : ((res_ >> 31) & 1u));
            sumext_ = neg ? 0xFFFF : 0;
            ctl_ = uint16_t((ctl_ & ~MPYC) | (neg ? MPYC : 0));
            // MPYS solo desborda en fraccional con -1 · -1 (el producto, al revés de signo)
            desb_ = s && (ctl_ & MPYFRAC) && neg && ((x1 < 0) == (x2 < 0));
        }
    }
    // MPYSAT: el resultado con signo saturado (lo que se lee) [§5.2.4]
    uint64_t resultado() const {
        if (!(ctl_ & MPYSAT) || !(modo_ == MPYS || modo_ == MACS) || !desb_) return res_;
        const bool w = ancho64();
        const bool neg_res = w ? (int64_t(res_) < 0) : ((res_ >> 31) & 1u);
        // desbordado: el signo del resultado está al revés
        if (w) return neg_res ? 0x7FFFFFFFFFFFFFFFull : 0x8000000000000000ull;
        return neg_res ? 0x7FFFFFFFull : 0x80000000ull;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_MPY32_H
