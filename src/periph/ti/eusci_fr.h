// =============================================================================
// eusci_fr.h — La eUSCI de los MSP430 FRAM (FR2xx/FR4xx, FR5xx/FR6xx)
// (M6 de doc/analisis_msp430.md)
//
// Es la del MSP432P4 tal cual (eusci.h, a nivel de bit sobre los pines): los
// mismos registros en los mismos desplazamientos [UG FR2 SLAU445I cap.
// 22-24; H: EUSCI_A_Type y EUSCI_B_Type]. Lo único que hace falta es la cara
// que el SoC de MSP430 espera: leer y escribir por desplazamiento (el SoC
// reparte su bus de 16 bits), la petición de interrupción (IFG & IE, la que
// también pinta el IV), el aviso de cambio para arbitrar, y la petición de
// reloj en bajo consumo (como la USCI de 5xx, usci.h).
// =============================================================================
#ifndef STM32_PERIPH_TI_EUSCI_FR_H
#define STM32_PERIPH_TI_EUSCI_FR_H

#include <tlm_utils/simple_initiator_socket.h>
#include "eusci.h"

namespace stm32 {

class EusciFr : public EusciP4 {
public:
    sc_core::sc_signal<bool> irq_interna{"irq_interna"};

    // `ventana`: 0x20 la A, 0x30 la B
    EusciFr(sc_core::sc_module_name nm, bool es_b)
        : EusciP4(nm, 0, es_b, es_b ? 0x30 : 0x20), yo_("yo") {
        irq(irq_interna);
        yo_.bind(tsk);
        al_cambiar_ifg = [this] { ev_cambio_.notify(sc_core::SC_ZERO_TIME); };
    }
    uint16_t lee(uint32_t off, bool dbg) { return EusciP4::lee16(off & ~1u, dbg); }
    void escribe(uint32_t off, uint16_t v, uint16_t m) {
        EusciP4::escribe16(off & ~1u, v, m);
        ev_cambio_.notify(sc_core::SC_ZERO_TIME);
    }
    bool pide() const { return (ifg_ & ie_) != 0; }
    bool en_i2c() const { return i2c(); }
    const sc_core::sc_event& cambio() const { return ev_cambio_; }
    // La petición condicional de reloj (SMCLKREQEN/ACLKREQEN) [UG FR2 §3.2.11]
    bool pide_reloj() const { return !en_reset() && (txlleno_ || tx_ocupado_ || rx_ocupado_); }
    unsigned fuente_brclk() const { return (ctlw0_ >> 6) & 3u; }

private:
    tlm_utils::simple_initiator_socket<EusciFr> yo_;
    sc_core::sc_event ev_cambio_;
};

} // namespace stm32
#endif // STM32_PERIPH_TI_EUSCI_FR_H
