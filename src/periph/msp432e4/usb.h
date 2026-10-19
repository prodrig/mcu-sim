// =============================================================================
// usb.h — El USB del MSP432E4: el controlador OTG de Mentor (MUSB), como
//         DISPOSITIVO de velocidad completa (T7)
//
// USB0 en 0x4005_0000, RCGCUSB, la línea 42 [H: msp432e401y.h, hw_usb.h;
// segunda fuente de cada secuencia: usb.c de DriverLib]. Es el MUSBMHDRC de
// Mentor Graphics, muy distinto del OTG de Synopsys de los STM32 (otg.h): no
// hay canales ni FIFOs en palabras, hay ocho endpoints con sus registros de
// estado de 8 bits (CSRL0, TXCSRL/RXCSRL...) y una FIFO por endpoint a la que
// se escribe y de la que se lee DE BYTE EN BYTE por FIFOn. Por eso este bloque
// atiende cada acceso con su tamaño (b_transport propio): leer un byte de IS
// no puede leer -y borrar- la palabra entera.
//
// LA FRONTERA es la de otg.h: lo eléctrico por los pines y los paquetes como
// paquetes (`usb_dev_if`), sin NRZI, relleno de bits, CRC ni conmutador
// DATA0/DATA1 [T-17]. Lo eléctrico:
//   * SOFTCONN (POWER) pone los 1,5 kΩ en D+ (PL6): el anfitrión ve llegar
//     un dispositivo de velocidad completa.
//   * Un SE0 de más de 2,5 µs en D+/D- es un reset de bus: FADDR a 0, el EP0
//     a su estado inicial, e IS.RESET.
//   * Cada SOF del anfitrión pone FRAME e IS.SOF.
//
// EL EP0 tiene su máquina de control, la que espera DriverLib y usblib:
//   SETUP  los 8 bytes en la FIFO, RXRDY, y la interrupción de EP0
//   OUT    datos (con RXRDY libre) o la etapa de estado (cero bytes)
//   IN     lo que se dejó con TXRDY, y DATAEND marca el último; sin TXRDY y
//          con DATAEND, es la etapa de estado de una petición sin datos
//   STALL  contesta STALL al siguiente testigo y deja STALLED
// Los endpoints 1-7: OUT con RXRDY libre (si no, NAK) e IN con TXRDY.
//
// LO QUE NO ESTÁ [P-17 s]: el modo anfitrión y OTG (SRP, HNP), la alta
// velocidad y ULPI, la uDMA del USB, los isócronos, el LPM, la suspensión por
// falta de SOF (IS.SUSPEND), y la detección de VBUS e ID: el bloque es
// dispositivo en cuanto se le pone SOFTCONN.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_USB_H
#define STM32_PERIPH_MSP432E4_USB_H

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <deque>
#include <vector>
#include "../otg.h"
#include "../../common/analog_net.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class UsbE4 : public BusSlave, public usb_dev_if {
public:
    sc_core::sc_out<bool> irq{"irq"};
    static constexpr unsigned NEP = 8;

    enum : uint8_t {
        IS_SUSPEND = 0x01, IS_RESUME = 0x02, IS_RESET = 0x04, IS_SOF = 0x08,
        POWER_SOFTCONN = 0x40,
        C0_RXRDY = 0x01, C0_TXRDY = 0x02, C0_STALLED = 0x04, C0_DATAEND = 0x08, C0_SETEND = 0x10,
        C0_STALL = 0x20, C0_RXRDYC = 0x40, C0_SETENDC = 0x80,
        TX_TXRDY = 0x01, TX_FIFONE = 0x02, TX_UNDRN = 0x04, TX_FLUSH = 0x08, TX_STALL = 0x10,
        TX_STALLED = 0x20, TX_CLRDT = 0x40,
        RX_RXRDY = 0x01, RX_FULL = 0x02, RX_OVER = 0x04, RX_FLUSH = 0x10, RX_STALL = 0x20,
        RX_STALLED = 0x40, RX_CLRDT = 0x80
    };

    explicit UsbE4(sc_core::sc_module_name nm) : BusSlave(nm, e4::USB0_B, 0x1000) {
        SC_HAS_PROCESS(UsbE4);
        SC_METHOD(pub_proc); sensitive << pub_ev_; dont_initialize();
        SC_METHOD(rst_proc); sensitive << rst_n;
        SC_THREAD(linea_proc);
        reinicia();
    }
    // Los pines: D+ (PL6) y D- (PL7). Los pone la SoC
    void conecta_pines(analog_net_if& dp, analog_net_if& dm) {
        dp_ = &dp; dm_ = &dm;
        d_pu_ = dp.register_driver((std::string(basename()) + "_pu_dp").c_str());
        dp.set_hiz(d_pu_);
    }

    // ---- usb_dev_if: lo que hace el anfitrión ----------------------------
    uint8_t transaccion(uint8_t pid, uint8_t addr, uint8_t ep, const std::vector<uint8_t>& salida,
                        std::vector<uint8_t>& entrada) override {
        if (!conectado() || addr != faddr_ || ep >= NEP) return PID_NADIE;
        ++n_trans_;
        const uint8_t r = ep == 0 ? ep0(pid, salida, entrada) : epn(pid, ep, salida, entrada);
        actualiza_irq();
        return r;
    }
    void sof(uint16_t trama) override {
        if (!conectado()) return;
        frame_ = uint16_t(trama & 0x7FFu);
        is_ |= IS_SOF;
        actualiza_irq();
    }

    // ---- Para el banco ----------------------------------------------------
    bool conectado() const {
        return (!con_reloj || con_reloj()) && rst_n.read() && (power_ & POWER_SOFTCONN);
    }
    // RCGCUSB: sin reloj el bloque se desconecta (lo llama la SoC al cambiar)
    std::function<bool()> con_reloj;
    void reevalua() { pon_pullup(); sale_ev_.notify(sc_core::SC_ZERO_TIME); }
    uint8_t direccion() const { return faddr_; }
    unsigned transacciones() const { return n_trans_; }
    unsigned resets() const { return n_resets_; }

protected:
    struct Ep {
        uint16_t txmaxp = 0, rxmaxp = 0;
        uint8_t txcsrl = 0, txcsrh = 0, rxcsrl = 0, rxcsrh = 0, txtype = 0, rxtype = 0,
                txint = 0, rxint = 0, txfifosz = 0, rxfifosz = 0;
        uint16_t txfifoadd = 0, rxfifoadd = 0;
        std::deque<uint8_t> tx;          // lo que el firmware escribe para un IN
        std::vector<uint8_t> rx;         // lo que llegó en el último OUT
        size_t rx_i = 0;
    };
    std::array<Ep, NEP> ep_;
    uint8_t faddr_ = 0, power_ = 0x20, is_ = 0, ie_ = 0x06, epidx_ = 0, test_ = 0, devctl_ = 0x80;
    uint16_t txis_ = 0, rxis_ = 0, txie_ = 0xFFFF, rxie_ = 0xFFFE, frame_ = 0;
    uint8_t csrl0_ = 0, csrh0_ = 0;
    bool estado_in_ = false;             // la etapa de estado IN de una peticion sin datos
    bool estado_out_ = false;            // la etapa de estado OUT tras un IN con DATAEND
    uint32_t gpcs_ = 0, pc_ = 0, cc_ = 0, epc_ = 0;
    bool o_irq_ = false;
    sc_core::sc_event pub_ev_, sale_ev_;
    analog_net_if *dp_ = nullptr, *dm_ = nullptr;
    int d_pu_ = -1;
    unsigned n_trans_ = 0, n_resets_ = 0;
    bool pu_puesto_ = false;

    void reinicia() {
        for (Ep& e : ep_) e = Ep{};
        faddr_ = 0; power_ = 0x20; is_ = 0; ie_ = 0x06; epidx_ = 0; test_ = 0;
        txis_ = rxis_ = 0; txie_ = 0xFFFF; rxie_ = 0xFFFE; frame_ = 0;
        csrl0_ = csrh0_ = 0; estado_in_ = estado_out_ = false;
        gpcs_ = pc_ = cc_ = epc_ = 0;
    }
    void rst_proc() {
        if (rst_n.read()) { pon_pullup(); return; }
        reinicia();
        o_irq_ = false;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
        pon_pullup();
    }
    void pub_proc() { irq.write(o_irq_); }
    void actualiza_irq() {
        const bool n = (txis_ & txie_) || (rxis_ & rxie_) || (is_ & ie_);
        if (n != o_irq_) { o_irq_ = n; pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    }
    // El 1,5 kOhm de D+: SOFTCONN con reloj
    void pon_pullup() {
        const bool on = conectado();
        if (!dp_ || on == pu_puesto_) return;
        pu_puesto_ = on;
        if (on) dp_->set_drive(d_pu_, 3.3f, 1500.0f);
        else dp_->set_hiz(d_pu_);
    }
    // El reset de bus: un SE0 de mas de 2,5 us con el dispositivo conectado
    void linea_proc() {
        for (;;) {
            if (!dp_ || !dm_) { sc_core::wait(sale_ev_); continue; }
            sc_core::wait(dp_->value_changed_event() | dm_->value_changed_event() | sale_ev_);
            if (!se0() || !conectado()) continue;
            sc_core::wait(sc_core::sc_time(2.5, sc_core::SC_US),
                          dp_->value_changed_event() | dm_->value_changed_event());
            if (!se0() || !conectado()) continue;
            reset_de_bus();
            while (se0()) sc_core::wait(dp_->value_changed_event() | dm_->value_changed_event());
        }
    }
    bool se0() const { return dp_->voltage() < 0.8f && dm_->voltage() < 0.8f; }
    void reset_de_bus() {
        ++n_resets_;
        faddr_ = 0;
        csrl0_ = 0; estado_in_ = estado_out_ = false;
        ep_[0].tx.clear(); ep_[0].rx.clear(); ep_[0].rx_i = 0;
        for (unsigned i = 1; i < NEP; ++i) {
            ep_[i].tx.clear(); ep_[i].rx.clear(); ep_[i].rx_i = 0;
            ep_[i].txcsrl = 0; ep_[i].rxcsrl = 0;
        }
        txis_ = rxis_ = 0;
        is_ |= IS_RESET;
        actualiza_irq();
    }

    // ---- El EP0 -------------------------------------------------------------
    uint8_t ep0(uint8_t pid, const std::vector<uint8_t>& s, std::vector<uint8_t>& e) {
        Ep& q = ep_[0];
        if (pid == PID_SETUP) {
            // Una SETUP siempre se acepta; si pilla una transferencia a medias,
            // SETEND lo dice
            if (csrl0_ & (C0_TXRDY | C0_DATAEND) || estado_in_ || estado_out_) csrl0_ |= C0_SETEND;
            q.tx.clear();
            q.rx = s; q.rx_i = 0;
            csrl0_ = uint8_t((csrl0_ & (C0_SETEND | C0_STALLED)) | C0_RXRDY);
            csrl0_ &= uint8_t(~C0_STALL);
            estado_in_ = estado_out_ = false;
            txis_ |= 1u;
            return PID_ACK;
        }
        if (csrl0_ & C0_STALL) {
            csrl0_ = uint8_t((csrl0_ & ~C0_STALL) | C0_STALLED);
            txis_ |= 1u;
            return PID_STALL;
        }
        if (pid == PID_OUT) {
            if (s.empty() && estado_out_) {            // la etapa de estado
                estado_out_ = false;
                txis_ |= 1u;
                return PID_ACK;
            }
            if (csrl0_ & C0_RXRDY) return PID_NAK;
            q.rx = s; q.rx_i = 0;
            csrl0_ |= C0_RXRDY;
            txis_ |= 1u;
            return PID_ACK;
        }
        if (pid == PID_IN) {
            if (csrl0_ & C0_TXRDY) {
                e.assign(q.tx.begin(), q.tx.end());
                q.tx.clear();
                csrl0_ &= uint8_t(~C0_TXRDY);
                if (csrl0_ & C0_DATAEND) { csrl0_ &= uint8_t(~C0_DATAEND); estado_out_ = true; }
                txis_ |= 1u;
                return PID_ACK;
            }
            if (estado_in_) {                          // estado de una sin datos
                e.clear();
                estado_in_ = false;
                txis_ |= 1u;
                return PID_ACK;
            }
            return PID_NAK;
        }
        return PID_NADIE;
    }
    void escribe_csrl0(uint8_t v) {
        if (v & C0_SETENDC) csrl0_ &= uint8_t(~C0_SETEND);
        if (v & C0_RXRDYC) {
            csrl0_ &= uint8_t(~C0_RXRDY);
            if (v & C0_DATAEND) estado_in_ = true;     // sin mas datos: el estado es IN
        }
        if (v & C0_TXRDY) {
            csrl0_ |= C0_TXRDY;
            if (v & C0_DATAEND) csrl0_ |= C0_DATAEND;
        }
        if (v & C0_STALL) csrl0_ |= C0_STALL;
        // STALLED se borra escribiendolo a 0
        if (!(v & C0_STALLED)) csrl0_ &= uint8_t(~C0_STALLED);
    }

    // ---- Los endpoints 1-7 --------------------------------------------------
    uint8_t epn(uint8_t pid, unsigned n, const std::vector<uint8_t>& s, std::vector<uint8_t>& e) {
        Ep& q = ep_[n];
        if (pid == PID_OUT) {
            if (q.rxcsrl & RX_STALL) { q.rxcsrl |= RX_STALLED; rxis_ |= uint16_t(1u << n); return PID_STALL; }
            if (q.rxcsrl & RX_RXRDY) return PID_NAK;
            q.rx = s; q.rx_i = 0;
            q.rxcsrl |= RX_RXRDY;
            rxis_ |= uint16_t(1u << n);
            return PID_ACK;
        }
        if (pid == PID_IN) {
            if (q.txcsrl & TX_STALL) { q.txcsrl |= TX_STALLED; txis_ |= uint16_t(1u << n); return PID_STALL; }
            if (!(q.txcsrl & TX_TXRDY)) return PID_NAK;
            e.assign(q.tx.begin(), q.tx.end());
            q.tx.clear();
            q.txcsrl &= uint8_t(~(TX_TXRDY | TX_FIFONE));
            txis_ |= uint16_t(1u << n);
            return PID_ACK;
        }
        return PID_NADIE;
    }

    // ---- Los registros, con su tamaño --------------------------------------
    uint8_t lee_fifo(unsigned n) {
        Ep& q = ep_[n];
        return q.rx_i < q.rx.size() ? q.rx[q.rx_i++] : 0;
    }
    uint32_t lee(uint32_t off, unsigned len) {
        if (off >= 0x20 && off < 0x40) {
            uint32_t v = 0;
            for (unsigned i = 0; i < len; ++i) v |= uint32_t(lee_fifo((off - 0x20) / 4)) << (8 * i);
            return v;
        }
        if (off >= 0x100 && off < 0x180) {
            const unsigned n = (off - 0x100) / 0x10, r = (off - 0x100) % 0x10;
            Ep& q = ep_[n];
            const auto qrx = uint16_t(q.rx.size() - std::min(q.rx.size(), q.rx_i));
            if (n == 0) {
                switch (r) {
                    case 0x2: return csrl0_ | (len > 1 ? uint32_t(csrh0_) << 8 : 0u);
                    case 0x3: return csrh0_;
                    case 0x8: return qrx;
                    default: return 0;
                }
            }
            switch (r) {
                case 0x0: return q.txmaxp;
                case 0x2: return q.txcsrl | (len > 1 ? uint32_t(q.txcsrh) << 8 : 0u);
                case 0x3: return q.txcsrh;
                case 0x4: return q.rxmaxp;
                case 0x6: return q.rxcsrl | (len > 1 ? uint32_t(q.rxcsrh) << 8 : 0u);
                case 0x7: return q.rxcsrh;
                case 0x8: return qrx;
                case 0xA: return q.txtype;
                case 0xB: return q.txint;
                case 0xC: return q.rxtype;
                case 0xD: return q.rxint;
                default: return 0;
            }
        }
        switch (off) {
            case 0x00: return faddr_ | (len > 1 ? uint32_t(power_) << 8 : 0u);
            case 0x01: return power_;
            case 0x02: { const uint16_t v = txis_; txis_ = 0; actualiza_irq(); return v; }
            case 0x04: { const uint16_t v = rxis_; rxis_ = 0; actualiza_irq(); return v; }
            case 0x06: return txie_;
            case 0x08: return rxie_;
            case 0x0A: { const uint8_t v = is_; is_ = 0; actualiza_irq(); return v; }
            case 0x0B: return ie_;
            case 0x0C: return frame_;
            case 0x0E: return epidx_;
            case 0x0F: return test_;
            case 0x60: return devctl_;
            case 0x62: return ep_[epidx_ & 7].txfifosz;
            case 0x63: return ep_[epidx_ & 7].rxfifosz;
            case 0x64: return ep_[epidx_ & 7].txfifoadd;
            case 0x66: return ep_[epidx_ & 7].rxfifoadd;
            case 0x78: return 0x77;                    // EPINFO: 7 TX y 7 RX
            case 0x79: return 0x0C;                    // RAMINFO: 4 KB [modelo]
            case 0x400: return epc_;
            case 0x41C: return gpcs_;
            case 0xFC0: return 0x000008C0u;            // PP: 8 endpoints, OTG [modelo]
            case 0xFC4: return pc_;
            case 0xFC8: return cc_;
            default: return 0;
        }
    }
    void escribe(uint32_t off, unsigned len, uint32_t v) {
        if (off >= 0x20 && off < 0x40) {
            Ep& q = ep_[(off - 0x20) / 4];
            for (unsigned i = 0; i < len; ++i) q.tx.push_back(uint8_t(v >> (8 * i)));
            if ((off - 0x20) / 4) q.txcsrl |= TX_FIFONE;
            return;
        }
        if (off >= 0x100 && off < 0x180) {
            const unsigned n = (off - 0x100) / 0x10, r = (off - 0x100) % 0x10;
            Ep& q = ep_[n];
            if (n == 0) {
                if (r == 0x2) { escribe_csrl0(uint8_t(v)); if (len > 1) csrh0_ = uint8_t(v >> 8); }
                else if (r == 0x3) {
                    csrh0_ = uint8_t(v & ~1u);
                    if (v & 1u) q.tx.clear();          // FLUSH
                }
                actualiza_irq();
                return;
            }
            switch (r) {
                case 0x0: q.txmaxp = uint16_t(v & 0x7FF); break;
                case 0x2: escribe_txcsrl(q, uint8_t(v)); if (len > 1) q.txcsrh = uint8_t(v >> 8); break;
                case 0x3: q.txcsrh = uint8_t(v); break;
                case 0x4: q.rxmaxp = uint16_t(v & 0x7FF); break;
                case 0x6: escribe_rxcsrl(q, uint8_t(v)); if (len > 1) q.rxcsrh = uint8_t(v >> 8); break;
                case 0x7: q.rxcsrh = uint8_t(v); break;
                case 0xA: q.txtype = uint8_t(v); break;
                case 0xB: q.txint = uint8_t(v); break;
                case 0xC: q.rxtype = uint8_t(v); break;
                case 0xD: q.rxint = uint8_t(v); break;
                default: break;
            }
            actualiza_irq();
            return;
        }
        switch (off) {
            case 0x00:
                faddr_ = uint8_t(v & 0x7F);
                if (len > 1) escribe_power(uint8_t(v >> 8));
                break;
            case 0x01: escribe_power(uint8_t(v)); break;
            case 0x06: txie_ = uint16_t(v); if (len > 2) rxie_ = uint16_t(v >> 16); actualiza_irq(); break;
            case 0x08: rxie_ = uint16_t(v & 0xFFFE); actualiza_irq(); break;
            case 0x0B: ie_ = uint8_t(v); actualiza_irq(); break;
            case 0x0E: epidx_ = uint8_t(v & 0xF); break;
            case 0x0F: test_ = uint8_t(v); break;
            case 0x62: ep_[epidx_ & 7].txfifosz = uint8_t(v); break;
            case 0x63: ep_[epidx_ & 7].rxfifosz = uint8_t(v); break;
            case 0x64: ep_[epidx_ & 7].txfifoadd = uint16_t(v); break;
            case 0x66: ep_[epidx_ & 7].rxfifoadd = uint16_t(v); break;
            case 0x400: epc_ = v; break;
            case 0x41C: gpcs_ = v & 7u; break;
            case 0xFC4: pc_ = v; break;
            case 0xFC8: cc_ = v; break;
            default: break;
        }
    }
    void escribe_power(uint8_t v) {
        power_ = uint8_t(v & 0x7F);
        pon_pullup();
        sale_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void escribe_txcsrl(Ep& q, uint8_t v) {
        if (v & TX_FLUSH) { q.tx.clear(); q.txcsrl &= uint8_t(~TX_FIFONE); }
        if (v & TX_TXRDY) q.txcsrl |= TX_TXRDY;
        if (v & TX_STALL) q.txcsrl |= TX_STALL; else q.txcsrl &= uint8_t(~TX_STALL);
        if (!(v & TX_STALLED)) q.txcsrl &= uint8_t(~TX_STALLED);
        if (!(v & TX_UNDRN)) q.txcsrl &= uint8_t(~TX_UNDRN);
    }
    void escribe_rxcsrl(Ep& q, uint8_t v) {
        if (v & RX_FLUSH) { q.rx.clear(); q.rx_i = 0; q.rxcsrl &= uint8_t(~RX_RXRDY); }
        if (!(v & RX_RXRDY)) { q.rxcsrl &= uint8_t(~RX_RXRDY); }
        if (v & RX_STALL) q.rxcsrl |= RX_STALL; else q.rxcsrl &= uint8_t(~RX_STALL);
        if (!(v & RX_STALLED)) q.rxcsrl &= uint8_t(~RX_STALLED);
        if (!(v & RX_OVER)) q.rxcsrl &= uint8_t(~RX_OVER);
    }

    // Cada acceso con su direccion y su tamaño: la FIFO y los registros que
    // se borran al leer no admiten que se lea la palabra entera
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) override {
        if (!check_common(gp)) return;
        const uint32_t off = uint32_t(gp.get_address()) - base_;
        const unsigned len = gp.get_data_length();
        unsigned char* d = gp.get_data_ptr();
        if (gp.is_read()) {
            const uint32_t v = lee(off, len);
            for (unsigned i = 0; i < len; ++i) d[i] = uint8_t(v >> (8 * i));
        } else {
            uint32_t v = 0;
            for (unsigned i = 0; i < len && i < 4; ++i) v |= uint32_t(d[i]) << (8 * i);
            escribe(off, len, v);
        }
        t += cycles(1);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) override {
        // La depuracion no toca la FIFO ni borra banderas: lee cero
        if (gp.is_read()) std::memset(gp.get_data_ptr(), 0, gp.get_data_length());
        return gp.get_data_length();
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_USB_H
