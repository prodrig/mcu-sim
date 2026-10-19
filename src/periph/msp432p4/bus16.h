// =============================================================================
// bus16.h — Un esclavo de bus con registros de 16 bits (los del MSP430)
//
// Los periféricos que el MSP432P4 hereda del MSP430 -eUSCI, Timer_A, REF_A,
// COMP_E, RTC_C, AES256, CRC32- tienen registros de 16 bits en direcciones
// pares, y algunos con efectos al leerlos: leer UCAxRXBUF baja RXIFG, leer un
// IV baja el flag que nombra. `BusSlave` trabaja por palabras de 32 bits y no
// dice qué mitad se lee, así que un `ldrh` de TXBUF (0x0E) dispararía el
// efecto de RXBUF (0x0C). Esta base parte el acceso en medias palabras: una
// llamada por registro de 16 bits tocado, con la máscara de los bytes.
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_BUS16_H
#define STM32_PERIPH_MSP432P4_BUS16_H

#include "../../common/periph_base.h"

namespace stm32 {

class Bus16P4 : public BusSlave {
public:
    Bus16P4(sc_core::sc_module_name nm, uint32_t base, uint32_t size)
        : BusSlave(nm, base, size) {}

protected:
    // `dbg`: un acceso del depurador, que no debe tener efectos al leer.
    virtual uint16_t lee16(uint32_t off, bool dbg) = 0;
    // `m`: los bits que se escriben (0x00FF, 0xFF00 o 0xFFFF)
    virtual void escribe16(uint32_t off, uint16_t v, uint16_t m) = 0;

    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        if (!check_common(gp)) return;
        acceso(gp, false);
        t += cycles(access_cycles(gp.is_write()));
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) override {
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        if (uint64_t(off) + gp.get_data_length() > size_) return 0;
        acceso(gp, true);
        return gp.get_data_length();
    }
    // Para quien herede y quiera seguir usando reg_read/reg_write (banco)
    uint32_t reg_read(uint32_t off) override {
        return uint32_t(lee16(off, true)) | (uint32_t(lee16(off + 2, true)) << 16);
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint16_t m0 = uint16_t(((be & 1u) ? 0x00FFu : 0u) | ((be & 2u) ? 0xFF00u : 0u));
        uint16_t m1 = uint16_t(((be & 4u) ? 0x00FFu : 0u) | ((be & 8u) ? 0xFF00u : 0u));
        if (m0) escribe16(off, uint16_t(v), m0);
        if (m1) escribe16(off + 2, uint16_t(v >> 16), m1);
    }

private:
    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        unsigned i = 0;
        while (i < len) {
            const uint32_t h = (off + i) & ~1u;
            if (gp.is_read()) {
                const uint16_t v = lee16(h, dbg);
                while (i < len && ((off + i) & ~1u) == h) {
                    d[i] = uint8_t(v >> (8u * ((off + i) & 1u)));
                    ++i;
                }
            } else {
                uint16_t v = 0, m = 0;
                while (i < len && ((off + i) & ~1u) == h) {
                    const unsigned b = (off + i) & 1u;
                    if (dbg || byte_enabled(gp, i)) {
                        v = uint16_t(v | (uint16_t(d[i]) << (8u * b)));
                        m = uint16_t(m | (0xFFu << (8u * b)));
                    }
                    ++i;
                }
                if (m) escribe16(h, v, m);
            }
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_BUS16_H
