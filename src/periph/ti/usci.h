// =============================================================================
// usci.h — La USCI de los MSP430 (2xx, 4xx y 5xx/6xx): UART, SPI e I2C
// (P-20 / M3 y M4 de doc/analisis_msp430.md)
//
// La USCI es la madre de la eUSCI, y lo que hace en el pin es lo mismo: los
// bits de CTL0 y CTL1 son los de la mitad alta y baja de UCxCTLW0, STAT es la
// mitad baja de UCxSTATW, el I2C lleva las mismas banderas con los mismos
// nombres. Lo que cambia es dónde están los registros y cómo se cuentan los
// baudios. Así que aquí no hay otra máquina: `UsciTi` ES una `EusciP4`
// (eusci.h, a nivel de bit sobre los pines) con la cara de la USCI delante.
//
// LAS DOS CARAS
//
//   2xx/4xx (`lee8_2xx`/`escribe8_2xx`, registros de byte sueltos; el SoC los
//   reparte) [UG2 SLAU144K cap. 15-17]:
//     A: ABCTL IRTCTL IRRCTL CTL0 CTL1 BR0 BR1 MCTL STAT RXBUF TXBUF
//     B: CTL0 CTL1 BR0 BR1 I2CIE STAT RXBUF TXBUF, I2COA e I2CSA (palabras)
//     RXIFG y TXIFG viven en IFG2 (y sus IE en IE2): las tiene el SoC, que las
//     lee y las escribe aquí (`ifg_rx`, `ifg_tx`, `pon_ifg`). En I2C, AL, STT,
//     STP y NACK son bits de UCB0STAT, con su habilitación en UCB0I2CIE.
//
//   5xx/6xx (`lee16_5xx`/`escribe16_5xx`, una ventana de 32 bytes) [UG5
//   SLAU208Q cap. 34-36]:
//     +00 CTLW0 (CTL1 abajo, CTL0 arriba)  +06 BRW  +08 MCTL (A)
//     +0A STAT  +0C RXBUF  +0E TXBUF  +10 ABCTL (A) / I2COA (B)
//     +12 IRCTL (A) / I2CSA (B)  +1C IE  +1D IFG  +1E IV
//     IFG/IE: RXIFG 01, TXIFG 02 y, en I2C, STTIFG 04, STPIFG 08, ALIFG 10,
//     NACKIFG 20: los mismos bits que la eUSCI. El IV de la A: 2 RX, 4 TX; el
//     de la B en I2C: 2 AL, 4 NACK, 6 STT, 8 STP, 0A RX, 0C TX.
//
// LOS BAUDIOS. UCxMCTL de la USCI: UCBRFx (7-4), UCBRSx (3-1), UCOS16 (0).
// UCBRSx (0-7) es un número de unos, no un patrón: la tabla 15-2 de UG2 dice
// en qué bits del carácter caen, y eso es exactamente el patrón de 8 bits de
// la eUSCI (bit k del patrón, bit k del carácter desde el de arranque).
//
// LO QUE NO ESTÁ: la detección automática de baudios y la IrDA se guardan y
// avisan (lo mismo que en la eUSCI). UCOA de 10 bits, sí; el registro de
// máscara de direcciones y los OA1-OA3 de la eUSCI no existen en la USCI.
// =============================================================================
#ifndef STM32_PERIPH_TI_USCI_H
#define STM32_PERIPH_TI_USCI_H

#include <tlm_utils/simple_initiator_socket.h>
#include "eusci.h"

namespace stm32 {

class UsciTi : public EusciP4 {
public:
    // La patilla de interrupción de la eUSCI no se usa: la USCI de 2xx reparte
    // sus banderas entre IFG2 y STAT, y el SoC decide; la de 5xx tiene su IV
    sc_core::sc_signal<bool> irq_interna{"irq_interna"};

    UsciTi(sc_core::sc_module_name nm, bool es_b)
        : EusciP4(nm, 0, es_b, 0x40), yo_("yo") {
        irq(irq_interna);
        yo_.bind(tsk);                          // nadie más llega por el bus
        al_cambiar_ifg = [this] { ev_cambio_.notify(sc_core::SC_ZERO_TIME); };
    }

    // ---------------------------------------------------------------- 2xx ---
    enum R2 { CTL0, CTL1, BR0, BR1, MCTL, STAT, RXBUF, TXBUF, ABCTL, IRTCTL, IRRCTL, I2CIE };
    uint8_t lee8_2xx(R2 r, bool dbg = false) {
        switch (r) {
        case CTL0:  return uint8_t(ctlw0_ >> 8);
        case CTL1:  return uint8_t(ctlw0_);
        case BR0:   return uint8_t(brw_);
        case BR1:   return uint8_t(brw_ >> 8);
        case MCTL:  return mctl_;
        case STAT: {
            uint8_t s = uint8_t(statw_lee());
            if (i2c()) s = uint8_t((s & 0x70u) | ((ifg_ & 0x10) ? 0x01 : 0) | ((ifg_ & 0x04) ? 0x02 : 0) |
                                   ((ifg_ & 0x08) ? 0x04 : 0) | ((ifg_ & 0x20) ? 0x08 : 0));
            return s;
        }
        case RXBUF: return uint8_t(lee16(0x0C, dbg));
        case TXBUF: return uint8_t(txbuf_);
        case ABCTL: return uint8_t(abctl_);
        case IRTCTL: return uint8_t(irctl_);
        case IRRCTL: return uint8_t(irctl_ >> 8);
        case I2CIE: return i2cie_;
        }
        return 0;
    }
    void escribe8_2xx(R2 r, uint8_t v) {
        switch (r) {
        case CTL0:  EusciP4::escribe16(0x00, uint16_t(v << 8), 0xFF00); break;
        case CTL1:  EusciP4::escribe16(0x00, v, 0x00FF); break;
        case BR0:   EusciP4::escribe16(0x06, v, 0x00FF); break;
        case BR1:   EusciP4::escribe16(0x06, uint16_t(v << 8), 0xFF00); break;
        case MCTL:  pon_mctl(v); break;
        case STAT:
            if (i2c()) {
                // En 2xx, AL, STT, STP y NACK se borran escribiendo STAT
                uint16_t f = ifg_;
                if (!(v & 0x01)) f &= uint16_t(~0x10);
                if (!(v & 0x02)) f &= uint16_t(~0x04);
                if (!(v & 0x04)) f &= uint16_t(~0x08);
                if (!(v & 0x08)) f &= uint16_t(~0x20);
                EusciP4::escribe16(0x2C, f, 0xFFFF);
            } else EusciP4::escribe16(es_b_ ? 0x08 : 0x0A, v, 0x0080);   // UCLISTEN (STATW)
            break;
        case RXBUF: break;
        case TXBUF: EusciP4::escribe16(0x0E, v, 0x00FF); break;
        case ABCTL: EusciP4::escribe16(0x10, v, 0x00FF); break;
        case IRTCTL: EusciP4::escribe16(0x12, v, 0x00FF); break;
        case IRRCTL: EusciP4::escribe16(0x12, uint16_t(v << 8), 0xFF00); break;
        case I2CIE: i2cie_ = uint8_t(v & 0x0F); break;
        }
    }
    uint16_t i2coa() const { return uint16_t((oa_[0] & 0x8000u) | (oa_[0] & 0x03FFu)); }
    void pon_i2coa(uint16_t v) {
        // La USCI no tiene UCOAEN: su dirección propia vale siempre
        EusciP4::escribe16(0x14, uint16_t((v & 0x83FFu) | 0x0400u), 0xFFFF);
    }
    uint16_t i2csa() const { return i2csa_; }
    void pon_i2csa(uint16_t v) { EusciP4::escribe16(0x20, uint16_t(v & 0x03FFu), 0xFFFF); }
    // IFG2: RXIFG y TXIFG de este módulo
    bool ifg_rx() const { return (ifg_ & RXIFG) != 0; }
    bool ifg_tx() const { return (ifg_ & TXIFG) != 0; }
    void pon_ifg(bool rx, bool tx) {
        const uint16_t f = uint16_t((ifg_ & ~(RXIFG | TXIFG)) | (rx ? RXIFG : 0) | (tx ? TXIFG : 0));
        EusciP4::escribe16(es_b_ ? 0x2C : 0x1C, f, 0xFFFF);
    }
    // Las de estado del I2C que piden interrupción (2xx: al vector RX)
    bool i2c_estado_pide() const {
        if (!i2c()) return false;
        const uint8_t s = uint8_t(((ifg_ & 0x10) ? 0x01 : 0) | ((ifg_ & 0x04) ? 0x02 : 0) |
                                  ((ifg_ & 0x08) ? 0x04 : 0) | ((ifg_ & 0x20) ? 0x08 : 0));
        return (s & i2cie_) != 0;
    }
    bool en_i2c() const { return i2c(); }
    // Cambió alguna bandera: el SoC vuelve a arbitrar sus interrupciones
    const sc_core::sc_event& cambio() const { return ev_cambio_; }

    // ---------------------------------------------------------------- 5xx ---
    uint16_t lee16_5xx(uint32_t off, bool dbg = false) {
        switch (off & ~1u) {
        case 0x00: return ctlw0_;
        case 0x06: return brw_;
        case 0x08: return es_b_ ? 0 : mctl_;
        case 0x0A: return uint16_t(statw_lee() & 0xFFu);
        case 0x0C: return uint16_t(lee16(0x0C, dbg) & 0xFFu);
        case 0x0E: return uint16_t(txbuf_ & 0xFFu);
        case 0x10: return es_b_ ? i2coa() : abctl_;
        case 0x12: return es_b_ ? i2csa_ : irctl_;
        case 0x1C: return uint16_t((ie5_ & 0xFFu) | ((ifg_ & mascara_ifg()) << 8));
        case 0x1E: return iv5(dbg);
        default: return 0;
        }
    }
    // m: máscara de los bytes escritos (0x00FF, 0xFF00 o 0xFFFF)
    void escribe16_5xx(uint32_t off, uint16_t v, uint16_t m) {
        switch (off & ~1u) {
        case 0x00: EusciP4::escribe16(0x00, v, m); break;
        case 0x06: EusciP4::escribe16(0x06, v, m); break;
        case 0x08: if (!es_b_ && (m & 0xFF)) pon_mctl(uint8_t(v)); break;
        case 0x0A: if (m & 0xFF) EusciP4::escribe16(es_b_ ? 0x08 : 0x0A, v, 0x0080); break;
        case 0x0E: if (m & 0xFF) EusciP4::escribe16(0x0E, v, 0x00FF); break;
        case 0x10: if (es_b_) pon_i2coa(uint16_t((i2coa() & ~m) | (v & m))); else EusciP4::escribe16(0x10, v, m); break;
        case 0x12: if (es_b_) pon_i2csa(uint16_t((i2csa_ & ~m) | (v & m))); else EusciP4::escribe16(0x12, v, m); break;
        case 0x1C:
            if (m & 0x00FF) ie5_ = uint8_t(v & mascara_ifg());
            if (m & 0xFF00) EusciP4::escribe16(es_b_ ? 0x2C : 0x1C,
                                               uint16_t((ifg_ & ~mascara_ifg()) | ((v >> 8) & mascara_ifg())), 0xFFFF);
            break;
        default: break;
        }
        ev_cambio_.notify(sc_core::SC_ZERO_TIME);
    }
    // La petición de interrupción de 5xx: IFG & IE
    bool irq5() const { return (ifg_ & ie5_ & mascara_ifg()) != 0; }
    // Para el DMA de 5xx: UCRXIFG y UCTXIFG
    bool dma_rx_pide() const { return (ifg_ & RXIFG) != 0 && !en_reset(); }
    bool dma_tx_pide() const { return (ifg_ & TXIFG) != 0 && !en_reset(); }

private:
    tlm_utils::simple_initiator_socket<UsciTi> yo_;
    uint8_t mctl_ = 0, i2cie_ = 0, ie5_ = 0;
    sc_core::sc_event ev_irq5_, ev_cambio_;

    uint16_t mascara_ifg() const { return i2c() ? 0x003Fu : 0x0003u; }
    // UCBRSx de la USCI (cuántos unos) al patrón de la eUSCI [UG2 tabla 15-2]
    void pon_mctl(uint8_t v) {
        static const uint8_t PATRON[8] = {0x00, 0x02, 0x22, 0x2A, 0xAA, 0xAE, 0xEE, 0xFE};
        mctl_ = v;
        const uint16_t w = uint16_t((PATRON[(v >> 1) & 7u] << 8) | (v & 0xF0u) | (v & 0x01u));
        EusciP4::escribe16(0x08, w, 0xFFFF);
    }
    uint16_t iv5(bool dbg) {
        const uint16_t p = uint16_t(ifg_ & ie5_ & mascara_ifg());
        static const uint16_t ORDEN_A[2] = {RXIFG, TXIFG};
        static const uint16_t ORDEN_I2C[6] = {0x10, 0x20, 0x04, 0x08, 0x01, 0x02};
        const uint16_t* o = i2c() ? ORDEN_I2C : ORDEN_A;
        const unsigned n = i2c() ? 6u : 2u;
        for (unsigned k = 0; k < n; ++k)
            if (p & o[k]) {
                // Leer el IV borra la de más prioridad [UG5 §34.4.15]
                if (!dbg)
                    EusciP4::escribe16(es_b_ ? 0x2C : 0x1C, uint16_t(ifg_ & ~o[k]), 0xFFFF);
                return uint16_t(2u * (k + 1u));
            }
        return 0;
    }
};

} // namespace stm32
#endif // STM32_PERIPH_TI_USCI_H
