// =============================================================================
// emac.h — El Ethernet del MSP432E4: el EMAC y su PHY interno (T7)
//
// EL MAC ES EL DEL F407. El EMAC del E4 es la misma IP de Synopsys (DWC
// Ethernet MAC 10/100) que el ETH de los STM32F4: el mismo mapa del MAC
// (CFG = MACCR, FRAMEFLTR = MACFFR, MIIADDR, ADDR0-3...), el mismo MMC y el
// mismo PTP en 0x700, y el mismo DMA con los mismos registros y los mismos
// descriptores [H: hw_emac.h frente a stm32f407xx.h; 2F: los dos manuales lo
// describen igual, bit a bit]. Lo que cambia, y es todo lo que hay aquí:
//
//   * el DMA está en 0xC00 y no en 0x1000 (`traduce`), y el bloque mide 4 KB;
//   * cuatro registros propios al final: PP (las propiedades: MAC y PHY de
//     clase MSP432E4), PC (la configuración del PHY: interno o externo, y su
//     autonegociación), CC y las interrupciones del PHY (EPHYRIS/IM/MISC);
//   * MIIADDR.CR tiene cuatro bits, no tres;
//   * EMACInit pone SIEMPRE descriptores de 8 palabras (ATDS = EDFE), que el
//     modelo del F407 ya entiende;
//   * y, sobre todo, EL PHY ESTÁ DENTRO: no hay pines MII ni MDIO. El MAC le
//     habla por la frontera de tramas (`eth_phy_if`, en eth_mac.h) y por un
//     MDIO interno, sin pines (dirección de PHY 0).
//
// EL PHY INTERNO (`EphyE4`). Los registros de la norma (BMCR, BMSR, ID1/2,
// ANA, ANLPA, ANER) y los de TI que usa DriverLib (STS, SCR, MISR1/2, CTL,
// RCR, y REGCTL/ADDAR para los extendidos), con el enlace de verdad: hay
// enlace si hay CABLE (un puente con el ordenador, otro chip, o el banco de
// pruebas) y el PHY tiene reloj (RCGCEPHY) y no está en reset (SREPHY ni
// BMCR.RESET). La autonegociación acaba 1 ms después [modelo: en el chip son
// unos 2 s; P-17 q] a 100 Mbit/s en dúplex, o a lo que digan BMCR o
// EMACPC.ANMODE. ID1/ID2 valen 0x2000/0xA221 [1F, de memoria del datasheet
// de la TM4C129: P-17 q].
//
// EL CABLE. Lo que hay al otro lado del PHY es un `eth_cable_if`: le llegan
// las tramas que salen del MAC (sin FCS, con su relleno), y quien sea las
// mete en el chip con `EphyE4::de_cable()`, que las encola: un proceso del
// EMAC las entrega al DMA a la velocidad del cable.
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_EMAC_H
#define STM32_PERIPH_MSP432E4_EMAC_H

#include <deque>
#include <functional>
#include <vector>
#include "../eth_mac.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

// Lo que hay al otro lado del cable de un PHY de tramas
class eth_cable_if {
public:
    virtual ~eth_cable_if() = default;
    virtual void trama_del_cable(const std::vector<uint8_t>& t) = 0;
};

// Los rasgos del EMAC del E4: el del F407 sin MII ni RMII en pines
inline constexpr EthCaps caps_emac_e4() {
    EthCaps c{};
    c.mii = false; c.rmii = false;
    c.kind = "EMAC del MSP432E4 (DWC 10/100, PHY interno)";
    return c;
}
inline constexpr EthCaps CAPS_EMAC_E4 = caps_emac_e4();

class EphyE4 : public eth_phy_if {
public:
    enum : unsigned {
        BMCR = 0x00, BMSR = 0x01, ID1 = 0x02, ID2 = 0x03, ANA = 0x04, ANLPA = 0x05, ANER = 0x06,
        CFG1 = 0x09, CFG2 = 0x0A, CFG3 = 0x0B, REGCTL = 0x0D, ADDAR = 0x0E, STS = 0x10, SCR = 0x11,
        MISR1 = 0x12, MISR2 = 0x13, CTL = 0x19, RCR = 0x1F
    };
    std::function<void(const std::vector<uint8_t>&)> al_mac;   // lo pone el EMAC
    std::function<bool()> con_reloj = [] { return true; };
    std::function<void()> cambia = [] {};                       // el enlace se ha movido

    void conecta_cable(eth_cable_if* c) { cable_ = c; cambia(); }
    eth_cable_if* cable() const { return cable_; }
    bool hay_cable() const { return cable_ != nullptr; }

    // ---- eth_phy_if: lo que ve el MAC -----------------------------------
    void trama_del_mac(const std::vector<uint8_t>& t) override {
        if (!enlace()) return;
        ++n_tx_;
        if (cable_) cable_->trama_del_cable(t);
    }
    bool enlace() const override { return activo() && cable_ && enlazado_; }
    double bps() const override { return cien_ ? 100e6 : 10e6; }

    // ---- Lo que llega por el cable ---------------------------------------
    void de_cable(const std::vector<uint8_t>& t) {
        if (!enlace()) { ++n_perdidas_; return; }
        ++n_rx_;
        if (al_mac) al_mac(t);
    }

    // ---- El estado ------------------------------------------------------
    bool activo() const { return !en_reset_ && con_reloj() && !(r_[BMCR] & 0x0800u); }  // PWRDOWN
    void reset(uint32_t pc) {
        pc_ = pc;
        r_.fill(0);
        // AN según EMACPC: ANEN (bit 3) y ANMODE (2:1) [H: hw_emac.h, EMAC_PC_*]
        const bool an = pc & 0x8u;
        const unsigned modo = (pc >> 1) & 3u;           // 0: 10H, 1: 10F, 2: 100H, 3: 100F
        r_[BMCR] = uint16_t((an ? 0x1000u : 0u) | ((modo & 2u) ? 0x2000u : 0u) | ((modo & 1u) ? 0x0100u : 0u));
        r_[ANA] = 0x01E1u;
        r_[ID1] = 0x2000u;
        r_[ID2] = 0xA221u;
        r_[CFG1] = 0x0000u;
        r_[CTL] = 0x0000u;
        enlazado_ = false;
        an_hecha_ = false;
    }
    void pon_reset(bool r) { en_reset_ = r; if (r) enlazado_ = false; }
    // La autonegociación ha acabado: el enlace sube (si hay cable)
    void negocia() {
        if (!activo() || !cable_) { enlazado_ = false; return; }
        enlazado_ = true;
        an_hecha_ = (r_[BMCR] & 0x1000u) != 0;
        if (an_hecha_) {
            // Lo mejor que anuncian los dos: el otro extremo lo anuncia todo
            const uint16_t ana = r_[ANA];
            cien_ = (ana & 0x0180u) != 0;
            duplex_ = cien_ ? (ana & 0x0100u) != 0 : (ana & 0x0040u) != 0;
            r_[ANLPA] = 0x45E1u;                         // 10/100 H/F, ACK
            r_[ANER] = 0x0001u;
        } else {
            cien_ = (r_[BMCR] & 0x2000u) != 0;
            duplex_ = (r_[BMCR] & 0x0100u) != 0;
        }
        r_[MISR1] |= 0x2000u;                            // LINKSTAT cambia
    }
    void corta() { if (enlazado_) r_[MISR1] |= 0x2000u; enlazado_ = false; }
    bool cien() const { return cien_; }
    bool duplex() const { return duplex_; }

    // ---- MDIO ------------------------------------------------------------
    uint16_t lee(unsigned reg) {
        if (!activo() && !(r_[BMCR] & 0x0800u)) return 0xFFFFu;
        switch (reg) {
            case BMSR: {
                // 100BASE-TX F/H, 10BASE-T F/H, preámbulo suprimible, AN posible
                uint16_t v = 0x7849u;
                if (enlace()) v |= 0x0004u;
                if (enlace() && an_hecha_) v |= 0x0020u;
                return v;
            }
            case STS: {
                uint16_t v = 0;
                if (enlace()) v |= 0x0001u;
                if (!cien_) v |= 0x0002u;
                if (duplex_) v |= 0x0004u;
                if (enlace() && an_hecha_) v |= 0x0010u;
                return v;
            }
            case MISR1: case MISR2: {                     // se borran al leer
                const uint16_t v = r_[reg];
                r_[reg] = uint16_t(v & 0x00FFu);          // los habilitados se quedan
                return v;
            }
            case ADDAR: return ext_[ext_dir()];
            default: return reg < 32 ? r_[reg] : 0xFFFFu;
        }
    }
    void escribe(unsigned reg, uint16_t v) {
        if (reg >= 32) return;
        switch (reg) {
            case BMCR:
                if (v & 0x8000u) { reset(pc_); cambia(); return; }       // RESET, autoborrable
                r_[BMCR] = uint16_t(v & 0x7F80u);
                if (v & 0x0200u) { enlazado_ = false; cambia(); }        // reiniciar la AN
                return;
            case BMSR: case ID1: case ID2: case ANLPA: case ANER: case STS: return;
            case ADDAR: ext_[ext_dir()] = v; return;
            case RCR:
                if (v & 0x8000u) { reset(pc_); cambia(); }               // SWRESET
                return;
            default: r_[reg] = v; return;
        }
    }
    unsigned enviadas() const { return n_tx_; }
    unsigned recibidas() const { return n_rx_; }
    unsigned perdidas() const { return n_perdidas_; }

private:
    std::array<uint16_t, 32> r_{};
    std::array<uint16_t, 0x1000> ext_{};        // los registros extendidos (REGCTL/ADDAR)
    eth_cable_if* cable_ = nullptr;
    uint32_t pc_ = 0x0Eu;                       // el EMACPC del ultimo reset
    bool en_reset_ = false, enlazado_ = false, an_hecha_ = false, cien_ = true, duplex_ = true;
    unsigned n_tx_ = 0, n_rx_ = 0, n_perdidas_ = 0;
    unsigned ext_dir() const { return r_[REGCTL] & 0x0FFFu; }   // modo dirección: sin autoincremento
};

class EmacE4 : public EthBase {
public:
    sc_core::sc_in<bool> rst_phy_n{"rst_phy_n"};       // SREPHY
    EphyE4 phy;

    EmacE4(sc_core::sc_module_name nm)
        : EthBase(nm, CAPS_EMAC_E4, e4::EMAC0_B, 0x1000) {
        SC_HAS_PROCESS(EmacE4);
        SC_THREAD(cable_proc);
        SC_THREAD(enlace_proc);
        SC_METHOD(rst_phy_proc); sensitive << rst_phy_n << rst_n; dont_initialize();
        conectar_phy(&phy);
        phy.al_mac = [this](const std::vector<uint8_t>& t) {
            // Lo que no cabe en la FIFO de recepcion (2 KB) se pierde, como en
            // el chip cuando el firmware no devuelve descriptores [modelo:
            // 16 tramas de cola]
            if (entrada_.size() >= 16) { ++n_cola_llena_; return; }
            entrada_.push_back(t);
            cable_ev_.notify(sc_core::SC_ZERO_TIME);
        };
        phy.cambia = [this] { enlace_ev_.notify(sc_core::SC_ZERO_TIME); };
        phy.reset(pc_);
    }
    unsigned tramas_perdidas_rx() const { return n_cola_llena_; }
    void reevalua_enlace() { enlace_ev_.notify(sc_core::SC_ZERO_TIME); }

protected:
    uint32_t pc_ = 0x0080040Eu, cc_ = 0, ephyris_ = 0, ephyim_ = 0, wdogto_ = 0;
    std::deque<std::vector<uint8_t>> entrada_;
    sc_core::sc_event cable_ev_, enlace_ev_;
    unsigned n_cola_llena_ = 0;

    uint32_t traduce(uint32_t off) const override {
        if (off >= 0xC00 && off < 0xD00) return 0x1000u + (off - 0xC00u);
        if (off >= 0x1000) return ~0u;
        return off;
    }
    uint32_t mask_miiar() const override { return 0xFFFFu; }
    bool mdio_por_dentro() const override { return !(pc_ & 0x80000000u); }   // PHYEXT = 0
    uint16_t mdio_op(bool escr, unsigned pa, unsigned reg, uint16_t dato) override {
        if (pa != 0) return 0xFFFFu;                    // el interno contesta en la 0
        if (escr) { phy.escribe(reg, dato); return 0; }
        return phy.lee(reg);
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case 0x0DC: return wdogto_;
            case 0xFC0: return 0x00000103u;             // PP: MAC y PHY de clase MSP432E4 [H]
            case 0xFC4: return pc_;
            case 0xFC8: return cc_;
            case 0xFD0: return ephyris_;
            case 0xFD4: return ephyim_;
            case 0xFD8: return ephyris_ & ephyim_;
            // DMABUSMOD.SWR (el reset del bloque) se borra solo en cuanto el
            // reset acaba, que aqui es en el acto [modelo]: el modelo del F407
            // lo deja a 1, que es su valor de reset documentado
            case 0xC00: return EthBase::reg_read(off) & ~1u;
            default: return EthBase::reg_read(off);
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        switch (off) {
            case 0x0DC: wdogto_ = v & 0x00013FFFu; return;
            case 0xFC4: pc_ = v; return;                // se aplica al resetear el PHY
            case 0xFC8: cc_ = v & 0x00070000u; return;
            case 0xFD0: ephyris_ &= ~v; return;
            case 0xFD4: ephyim_ = v & 1u; return;
            default: EthBase::reg_write(off, v, be); return;
        }
    }

    // El reset del PHY: el del sistema o SREPHY
    void rst_phy_proc() {
        const bool r = !rst_phy_n.read() || !rst_n.read();
        if (r) phy.reset(pc_);
        phy.pon_reset(r);
        enlace_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    // El enlace: la AN acaba 1 ms después de que haya PHY y cable
    void enlace_proc() {
        for (;;) {
            sc_core::wait(enlace_ev_);
            if (!phy.activo() || !phy.hay_cable()) { phy.corta(); continue; }
            if (phy.enlace()) continue;
            sc_core::wait(sc_core::sc_time(1, sc_core::SC_MS), enlace_ev_);
            if (phy.activo() && phy.hay_cable()) phy.negocia();
            ephyris_ |= 1u;
        }
    }
    // Lo que llega por el cable, al DMA, al ritmo del cable
    void cable_proc() {
        for (;;) {
            if (entrada_.empty()) sc_core::wait(cable_ev_);
            if (entrada_.empty()) continue;
            std::vector<uint8_t> t = std::move(entrada_.front());
            entrada_.pop_front();
            const double bits = 8.0 * double(8 + t.size() + 4 + 12);
            sc_core::wait(sc_core::sc_time(bits / phy.bps(), sc_core::SC_SEC));
            trama_al_mac(t);
        }
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_EMAC_H
