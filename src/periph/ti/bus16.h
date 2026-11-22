// =============================================================================
// bus16.h — Esclavos de bus con registros de 16 y de 8 bits (los del MSP430)
//
// Los periféricos que el MSP432P4 hereda del MSP430 -eUSCI, Timer_A, REF_A,
// COMP_E, RTC_C, AES256, CRC32- tienen registros de 16 bits en direcciones
// pares, y algunos con efectos al leerlos: leer UCAxRXBUF baja RXIFG, leer un
// IV baja el flag que nombra. `BusSlave` trabaja por palabras de 32 bits y no
// dice qué mitad se lee, así que un `ldrh` de TXBUF (0x0E) dispararía el
// efecto de RXBUF (0x0C). Esta base parte el acceso en medias palabras: una
// llamada por registro de 16 bits tocado, con la máscara de los bytes.
//
// Desde M0 de doc/analisis_msp430.md es el componente común de los dos
// (`Bus16`; `Bus16P4` es el nombre de siempre). Para el MSP430 añade:
//
//   * MODO DE BYTES (`ModoByte`). En 5xx, 6xx y FR, cada byte de un registro
//     de 16 bits se lee y se escribe por separado (UCAxBR0/BR1, UCAxCTL1): es
//     `Libre`, el del P4. En 1xx/2xx/4xx, los módulos de 16 bits
//     (0x0100-0x01FF) «se acceden por palabras; con instrucciones de byte
//     solo valen las direcciones pares, y el byte alto del resultado es
//     siempre 0» [UG2 §1.4.3]: es `Palabra2xx`. Una escritura de byte en
//     dirección par escribe la palabra con el byte alto a 0, y en dirección
//     impar no hace nada [de modelo: la guía no lo dice];
//   * `Bus8`, para los módulos de 8 bits (0x0010-0x00FF en 2xx, y las SFR):
//     «leer con palabras da un byte alto impredecible; escribir una palabra
//     escribe solo el byte bajo» [UG2 §1.4.3]. Aquí el byte alto se lee a 0
//     [de modelo].
//
// La regla de las palabras en dirección impar (pierden el bit 0) es de la
// CPU, que nunca las pide: un acceso de palabra impar que llegue aquí (de un
// depurador, por ejemplo) se parte en bytes como cualquier otro.
// =============================================================================
#ifndef STM32_PERIPH_TI_BUS16_H
#define STM32_PERIPH_TI_BUS16_H

#include "../../common/periph_base.h"

namespace stm32 {

class Bus16 : public BusSlave {
public:
    enum class ModoByte { Libre, Palabra2xx };

    Bus16(sc_core::sc_module_name nm, uint32_t base, uint32_t size)
        : BusSlave(nm, base, size) {}
    void modo_byte(ModoByte m) { modo_ = m; }
    ModoByte modo_byte() const { return modo_; }

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
    ModoByte modo_ = ModoByte::Libre;

    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        // 2xx: un byte suelto en un módulo de 16 bits
        if (modo_ == ModoByte::Palabra2xx && len == 1 && !dbg) {
            if (off & 1u) { if (gp.is_read()) d[0] = 0; return; }
            if (gp.is_read()) d[0] = uint8_t(lee16(off, false));
            else if (byte_enabled(gp, 0)) escribe16(off, d[0], 0xFFFFu);
            return;
        }
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
using Bus16P4 = Bus16;

// Un módulo de registros de 8 bits (los de 0x0010-0x00FF del 2xx, las SFR)
class Bus8 : public BusSlave {
public:
    Bus8(sc_core::sc_module_name nm, uint32_t base, uint32_t size) : BusSlave(nm, base, size) {}

protected:
    virtual uint8_t lee8(uint32_t off, bool dbg) = 0;
    virtual void escribe8(uint32_t off, uint8_t v) = 0;

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
    uint32_t reg_read(uint32_t off) override {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) v |= uint32_t(lee8(off + i, true)) << (8 * i);
        return v;
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        for (unsigned i = 0; i < 4; ++i) if ((be >> i) & 1u) escribe8(off + i, uint8_t(v >> (8 * i)));
    }

private:
    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        // La CPU: una palabra es el byte bajo (el alto, a 0 al leer; ignorado al escribir)
        if (!dbg && len == 2 && !(off & 1u)) {
            if (gp.is_read()) { d[0] = lee8(off, false); d[1] = 0; }
            else if (byte_enabled(gp, 0)) escribe8(off, d[0]);
            return;
        }
        for (unsigned i = 0; i < len; ++i) {
            if (gp.is_read()) d[i] = lee8(off + i, dbg);
            else if (dbg || byte_enabled(gp, i)) escribe8(off + i, d[i]);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_BUS16_H
