// =============================================================================
// udma.h — La µDMA del MSP432E4: el PL230 de ARM con 32 canales (T6, entrega 3)
//
// La misma IP que la µDMA del P4 [periph/msp432p4/udma.h], pero con otra
// envoltura de TI [H; hw_udma.h; segunda fuente: udma.c/udma.h de DriverLib]:
//
//   0x000-0x04C  el PL230, sin desplazar: STAT, CFG, CTLBASE, ALTBASE,
//                WAITSTAT, SWREQ, USEBURST, REQMASK, ENA, ALT, PRIO, ERRCLR
//   0x500        CHASGN y CHIS, de los TM4C123: en el E4 la cabecera de TI no
//                los tiene (ni DriverLib uDMAIntStatus); el modelo deja CHIS
//                legible, y la línea UDMA es un PULSO [1F: P-17 i]
//   0x510-0x51C  CHMAP0..3: de cuál de sus fuentes (cuatro bits por canal)
//                toma cada canal la petición [DL: UDMA_CHn_* = enc << 16 | n]
//
// TREINTA Y DOS CANALES, con la tabla de control en la SRAM alineada a 1 KB: 16
// bytes por canal, la primaria en CTLBASE y la alternativa 0x200 más allá. La
// palabra de control es la del PL230, y el motor es el del P4: lee la
// estructura al empezar cada tanda, escribe de vuelta la cuenta y deja
// cycle_ctrl = 0 al acabar.
//
// AL ACABAR UN CICLO: si el canal lo pidió un periférico, la interrupción es
// la DEL PERIFÉRICO (DMATXRIS/DMARXRIS en la UART, por ejemplo); si se pidió
// por software (SWREQ), un pulso en la línea UDMA (y su bit de CHIS). Un
// error de bus apaga el canal y da UDMAERR.
//
// LO QUE NO ESTÁ: scatter-gather (cycle_ctrl 4-7), la distinción entre
// peticiones simples y de ráfaga (todas son de ráfaga) y los prot [P-17 i].
// =============================================================================
#ifndef STM32_PERIPH_MSP432E4_UDMA_H
#define STM32_PERIPH_MSP432E4_UDMA_H

#include <array>
#include <functional>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "../../common/periph_base.h"
#include "../../soc/msp432e4/e4_desc.h"

namespace stm32 {

class UdmaE4 : public BusSlave {
public:
    static constexpr unsigned NCH = 32, NENC = 16;
    tlm_utils::simple_initiator_socket<UdmaE4> mst{"mst"};
    sc_core::sc_out<bool> irq{"irq"}, irq_err{"irq_err"};

    explicit UdmaE4(sc_core::sc_module_name nm) : BusSlave(nm, e4::UDMA_B, 0x1000) {
        SC_HAS_PROCESS(UdmaE4);
        SC_THREAD(motor);
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }
    // La fuente `enc` del canal `ch`: su petición y a quién avisar al acabar
    void conecta_fuente(unsigned ch, unsigned enc, const sc_core::sc_signal<bool>* s,
                        std::function<void()> hecho = nullptr) {
        if (ch < NCH && enc < NENC) { fuente_[ch][enc] = s; hecho_[ch][enc] = std::move(hecho); }
    }
    void end_of_elaboration() override {
        BusSlave::end_of_elaboration();
        for (auto& f : fuente_) for (const auto* s : f) if (s) espera_ |= s->value_changed_event();
        espera_ |= ev_;
    }
    uint64_t transferencias() const { return n_trans_; }

protected:
    std::array<std::array<const sc_core::sc_signal<bool>*, NENC>, NCH> fuente_{};
    std::array<std::array<std::function<void()>, NENC>, NCH> hecho_{};
    sc_core::sc_event_or_list espera_;
    sc_core::sc_event ev_, irq_ev_;
    uint32_t cfg_ = 0, ctlbase_ = 0, useburst_ = 0, reqmask_ = 0, ena_ = 0, alt_ = 0,
             prio_ = 0, err_ = 0, sw_ = 0, auto_ = 0, chis_ = 0, chasgn_ = 0, porsw_ = 0;
    uint32_t chmap_[4] = {};
    uint64_t n_trans_ = 0;
    bool av_sg_ = false;

    unsigned enc(unsigned c) const { return (chmap_[c / 8u] >> (4 * (c % 8u))) & 0xFu; }
    void valores_de_reset() {
        cfg_ = ctlbase_ = useburst_ = reqmask_ = ena_ = alt_ = prio_ = err_ = 0;
        sw_ = auto_ = chis_ = chasgn_ = porsw_ = 0;
        for (uint32_t& m : chmap_) m = 0;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    bool pulso_ = false;
    void irq_proc() {
        irq.write(pulso_);
        if (pulso_) { pulso_ = false; irq_ev_.notify(cycles(1)); }
        irq_err.write(err_ != 0);
    }

    bool acceso(bool esc, uint32_t a, uint8_t* d, unsigned n, sc_core::sc_time& t) {
        tlm::tlm_generic_payload gp;
        gp.set_command(esc ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        gp.set_address(a);
        gp.set_data_ptr(d);
        gp.set_data_length(n);
        gp.set_streaming_width(n);
        gp.set_byte_enable_ptr(nullptr);
        gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        mst->b_transport(gp, t);
        return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
    }
    bool lee32(uint32_t a, uint32_t& v, sc_core::sc_time& t) {
        uint8_t b[4] = {};
        const bool ok = acceso(false, a, b, 4, t);
        v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        return ok;
    }
    bool escribe32(uint32_t a, uint32_t v, sc_core::sc_time& t) {
        uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        return acceso(true, a, b, 4, t);
    }
    bool pide(unsigned c) const {
        if (!(ena_ & (1u << c))) return false;
        if ((sw_ | auto_) & (1u << c)) return true;
        if (reqmask_ & (1u << c)) return false;
        const auto* s = fuente_[c][enc(c)];
        return s && s->read();
    }
    int elige() const {
        if (!(cfg_ & 1u)) return -1;
        for (int pase = 0; pase < 2; ++pase)
            for (unsigned c = 0; c < NCH; ++c) {
                const bool alta = (prio_ >> c) & 1u;
                if ((pase == 0) != alta) continue;
                if (pide(c)) return int(c);
            }
        return -1;
    }
    void error_bus(unsigned c) {
        err_ = 1;
        ena_ &= ~(1u << c); auto_ &= ~(1u << c); sw_ &= ~(1u << c);
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void avisa_fin(unsigned c) {
        const bool por_sw = (porsw_ >> c) & 1u;
        porsw_ &= ~(1u << c);
        const auto& h = hecho_[c][enc(c)];
        if (!por_sw && h) { h(); return; }
        chis_ |= 1u << c;
        pulso_ = true;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void motor() {
        for (;;) {
            const int ci = elige();
            if (ci < 0) { wait(espera_); continue; }
            const unsigned c = unsigned(ci);
            sc_core::sc_time t = sc_core::SC_ZERO_TIME;
            const bool alt = (alt_ >> c) & 1u;
            const uint32_t est = (ctlbase_ & ~0x3FFu) + (alt ? 0x200u : 0u) + 16u * c;
            uint32_t src_fin = 0, dst_fin = 0, ctl = 0;
            if (!lee32(est, src_fin, t) || !lee32(est + 4, dst_fin, t) || !lee32(est + 8, ctl, t)) {
                error_bus(c); wait(t + cycles(1)); continue;
            }
            const unsigned ciclo = ctl & 7u;
            if (ciclo == 0) {
                ena_ &= ~(1u << c); auto_ &= ~(1u << c); sw_ &= ~(1u << c);
                wait(t + cycles(1));
                continue;
            }
            if (ciclo >= 4) {
                if (!av_sg_) {
                    av_sg_ = true;
                    SC_REPORT_WARNING("msp432e4/udma", "uDMA: scatter-gather (cycle_ctrl 4-7) no "
                                      "se modela: el canal se para con error");
                }
                error_bus(c); wait(t + cycles(1)); continue;
            }
            if (ciclo == 2) auto_ |= 1u << c;
            if (sw_ & (1u << c)) porsw_ |= 1u << c;
            sw_ &= ~(1u << c);
            const unsigned resto = ((ctl >> 4) & 0x3FFu) + 1u;
            const unsigned r = 1u << ((ctl >> 14) & 0xFu);
            const unsigned tanda = resto < r ? resto : r;
            const unsigned ssz = (ctl >> 24) & 3u, sinc = (ctl >> 26) & 3u;
            const unsigned dsz = (ctl >> 28) & 3u, dinc = (ctl >> 30) & 3u;
            const unsigned nb = 1u << (ssz > 2 ? 2 : ssz);
            const unsigned dnb = 1u << (dsz > 2 ? 2 : dsz);
            bool ok = true;
            for (unsigned j = 0; j < tanda && ok; ++j) {
                const unsigned queda = resto - j - 1u;
                const uint32_t sa = (sinc == 3 ? src_fin : src_fin - (queda << sinc)) & ~(nb - 1u);
                const uint32_t da = (dinc == 3 ? dst_fin : dst_fin - (queda << dinc)) & ~(dnb - 1u);
                uint8_t b[4] = {};
                ok = acceso(false, sa, b, nb, t);
                if (ok) ok = acceso(true, da, b, dnb, t);
                if (ok) ++n_trans_;
            }
            if (!ok) { error_bus(c); wait(t + cycles(1)); continue; }
            const unsigned quedan = resto - tanda;
            if (quedan) ctl = (ctl & ~(0x3FFu << 4)) | ((quedan - 1u) << 4);
            else        ctl &= ~((0x3FFu << 4) | 7u);
            if (!escribe32(est + 8, ctl, t)) { error_bus(c); wait(t + cycles(1)); continue; }
            wait(t + cycles(2));
            if (quedan) continue;
            auto_ &= ~(1u << c);
            if (ciclo == 3) alt_ ^= 1u << c;
            else ena_ &= ~(1u << c);
            avisa_fin(c);
        }
    }

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case 0x000: return ((NCH - 1u) << 16) | (cfg_ & 1u);     // STAT
            case 0x008: return ctlbase_;
            case 0x00C: return (ctlbase_ & ~0x3FFu) + 0x200u;        // ALTBASE
            case 0x010: return 0;                                    // WAITSTAT
            case 0x018: return useburst_;
            case 0x020: return reqmask_;
            case 0x028: return ena_;
            case 0x030: return alt_;
            case 0x038: return prio_;
            case 0x04C: return err_;
            case 0x500: return chasgn_;
            case 0x504: return chis_;
            case 0x510: case 0x514: case 0x518: case 0x51C: return chmap_[(off - 0x510) / 4];
            // PrimeCell del PL230 [H]
            case 0xFE0: return 0x30; case 0xFE4: return 0xB2; case 0xFE8: return 0x0B;
            case 0xFF0: return 0x0D; case 0xFF4: return 0xF0; case 0xFF8: return 0x05;
            case 0xFFC: return 0xB1;
            default: return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        v &= m;
        switch (off) {
            case 0x004: cfg_ = v & 0xE1u; break;
            case 0x008: ctlbase_ = v & ~0x3FFu; break;
            case 0x014: sw_ |= v; break;
            case 0x018: useburst_ |= v; break;
            case 0x01C: useburst_ &= ~v; break;
            case 0x020: reqmask_ |= v; break;
            case 0x024: reqmask_ &= ~v; break;
            case 0x028: ena_ |= v; break;
            case 0x02C: ena_ &= ~v; auto_ &= ~v; break;
            case 0x030: alt_ |= v; break;
            case 0x034: alt_ &= ~v; break;
            case 0x038: prio_ |= v; break;
            case 0x03C: prio_ &= ~v; break;
            case 0x04C: if (v & 1u) err_ = 0; irq_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case 0x500: chasgn_ = v; break;
            case 0x504: chis_ &= ~v; irq_ev_.notify(sc_core::SC_ZERO_TIME); break;
            case 0x510: case 0x514: case 0x518: case 0x51C: chmap_[(off - 0x510) / 4] = v; break;
            default: return;
        }
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432E4_UDMA_H
