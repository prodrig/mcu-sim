// =============================================================================
// crc32.h — El módulo CRC32 del MSP432P4 (fase T3)
//
// Dos firmas en el mismo bloque, las del módulo CRC32 del MSP430FR [H:
// CRC32_Type; TRM SLAU356 cap. 8; segunda fuente: DriverLib crc32.c]:
//
//   CRC16   polinomio 0x1021 (CRC-CCITT)        DI16, DIRB16, INIRES16, RESR16
//   CRC32   polinomio 0x04C11DB7 (ISO 3309)     DI32, DIRB32, INIRES32, RESR32
//
// EL ORDEN DE LOS BITS es lo que distingue DI de DIRB. Un registro de
// desplazamiento con el polinomio, desplazando hacia el bit alto, que recibe
// los datos por bytes -el bajo primero en una escritura de 16 bits- y cada
// byte desde su bit 0 (DI) o desde su bit 7 (DIRB, «reversed»). INIRES es el
// registro; RESR, el mismo con los bits al revés. Así:
//
//   CRC-CCITT de la norma (MSB primero): semilla 0xFFFF en INIRES16, los datos
//   a DIRB16, y el resultado en INIRES16 («123456789» -> 0x29B1)
//   CRC32 de Ethernet/zlib (reflejado): semilla 0xFFFFFFFF en INIRES32, los
//   datos a DI32, y el resultado es ~RESR32 («123456789» -> 0xCBF43926)
//
// [1F: TRM, «CRC Implementation»; los dos valores de comprobación son los
// normales de esos dos CRC, y el banco los mira]. Sin tiempo: el resultado
// está en el ciclo siguiente.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_CRC32_H
#define STM32_PERIPH_MSP432P4_CRC32_H

#include "bus16.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class Crc32P4 : public Bus16P4 {
public:
    explicit Crc32P4(sc_core::sc_module_name nm) : Bus16P4(nm, p4::CRC32_B, 0x400) {
        SC_HAS_PROCESS(Crc32P4);
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    uint16_t crc16() const { return c16_; }
    uint32_t crc32() const { return c32_; }

protected:
    uint16_t c16_ = 0xFFFF;
    uint32_t c32_ = 0xFFFFFFFFu;

    void reset_proc() { if (!rst_n.read()) { c16_ = 0xFFFF; c32_ = 0xFFFFFFFFu; } }

    static uint8_t al_reves8(uint8_t b) {
        b = uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
        b = uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
        return uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
    }
    static uint16_t al_reves16(uint16_t v) {
        return uint16_t((al_reves8(uint8_t(v)) << 8) | al_reves8(uint8_t(v >> 8)));
    }
    static uint32_t al_reves32(uint32_t v) {
        return (uint32_t(al_reves16(uint16_t(v))) << 16) | al_reves16(uint16_t(v >> 16));
    }
    // Un byte, desde su bit 0
    void byte16(uint8_t b) {
        for (unsigned i = 0; i < 8; ++i) {
            const bool fb = ((c16_ >> 15) & 1u) ^ ((b >> i) & 1u);
            c16_ = uint16_t(c16_ << 1);
            if (fb) c16_ ^= 0x1021;
        }
    }
    void byte32(uint8_t b) {
        for (unsigned i = 0; i < 8; ++i) {
            const bool fb = ((c32_ >> 31) & 1u) ^ ((b >> i) & 1u);
            c32_ <<= 1;
            if (fb) c32_ ^= 0x04C11DB7u;
        }
    }
    // Los bytes escritos, el bajo primero; `rb`: cada uno desde su bit 7
    void datos(bool es32, bool rb, uint16_t v, uint16_t m) {
        for (unsigned k = 0; k < 2; ++k) {
            if (!(m & (0xFFu << (8 * k)))) continue;
            uint8_t b = uint8_t(v >> (8 * k));
            if (rb) b = al_reves8(b);
            if (es32) byte32(b); else byte16(b);
        }
    }

    uint16_t lee16(uint32_t off, bool) override {
        switch (off) {
            case 0x08: return uint16_t(c32_);                       // INIRES32_LO
            case 0x0A: return uint16_t(c32_ >> 16);                 // INIRES32_HI
            case 0x0C: return uint16_t(al_reves32(c32_));           // RESR32_LO
            case 0x0E: return uint16_t(al_reves32(c32_) >> 16);     // RESR32_HI
            case 0x18: return c16_;                                 // INIRES16
            case 0x1E: return al_reves16(c16_);                     // RESR16
            default:   return 0;
        }
    }
    void escribe16(uint32_t off, uint16_t v, uint16_t m) override {
        auto mz = [&](uint16_t cur) { return uint16_t((cur & ~m) | (v & m)); };
        switch (off) {
            case 0x00: datos(true,  false, v, m); return;           // DI32
            case 0x04: datos(true,  true,  v, m); return;           // DIRB32
            case 0x08: c32_ = (c32_ & 0xFFFF0000u) | mz(uint16_t(c32_)); return;
            case 0x0A: c32_ = (c32_ & 0x0000FFFFu) | (uint32_t(mz(uint16_t(c32_ >> 16))) << 16); return;
            case 0x10: datos(false, false, v, m); return;           // DI16
            case 0x14: datos(false, true,  v, m); return;           // DIRB16
            case 0x18: c16_ = mz(c16_); return;
            default:   return;
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_CRC32_H
