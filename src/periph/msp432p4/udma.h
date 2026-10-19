// =============================================================================
// udma.h — La µDMA del MSP432P4: un PL230 de ARM con el multiplexor de TI
//
// Dos mitades en el mismo bloque de 8 KB [H: DMA_Channel_Type y
// DMA_Control_Type; ARM DDI 0417 (PL230 TRM); TRM SLAU356 cap. 11; segunda
// fuente: DriverLib dma.c/dma.h y TI-Drivers UDMAMSP432.c]:
//
//   0x0000  la parte de TI    DEVICE_CFG, SW_CHTRIG, CH_SRCCFG[n] (de cuál de
//                             sus ocho fuentes toma cada canal su petición),
//                             INT1..3_SRCCFG (qué canal va a DMA_INT1..3) e
//                             INT0_SRCFLG/CLRFLG (los demás, a DMA_INT0)
//   0x1000  el PL230          STAT, CFG, CTLBASE, ALTBASE, WAITSTAT, SWREQ,
//                             USEBURST, REQMASK, ENA, ALT, PRIO, ERRCLR
//
// Ocho canales. LA TABLA DE CONTROL ESTÁ EN LA SRAM, donde la pone el
// firmware: 16 bytes por canal -fin de origen, fin de destino, palabra de
// control y uno sin usar-, la primaria en CTLBASE y la alternativa 0x80 más
// allá. La palabra de control es la del PL230: cycle_ctrl, next_useburst,
// n_minus_1, R_power, los prot, y tamaño e incremento de origen y destino.
// El controlador LA LEE DEL BUS al empezar cada tanda y ESCRIBE de vuelta
// n_minus_1 al acabarla, y cycle_ctrl = 0 al acabar el ciclo: el firmware ve
// la cuenta bajar en la memoria, como en el chip.
//
//   básico (1)      cada petición, 2^R transferencias; al acabar, el canal se
//                   apaga (ENA a 0) y avisa
//   automático (2)  una petición basta para el ciclo entero, en tandas de 2^R
//   ping-pong (3)   al acabar una estructura sigue con la otra, y avisa; si la
//                   otra está parada (cycle_ctrl = 0), el canal se apaga
//
// LAS PETICIONES son niveles: la de la eUSCI es su TXIFG o RXIFG, la del
// ADC14 que haya un IFG puesto. Mientras la petición está alta y el canal
// habilitado, la DMA hace una tanda, vuelve a arbitrar, y así: escribir TXBUF
// baja TXIFG, leer RXBUF o MEMn baja la suya. El canal 0 gana, salvo los de
// prioridad alta (PRIOSET). Las fuentes por canal son las de DMA_CHn_* de
// DriverLib: eUSCI A/B, ADC14 en el 7, y Timer_A, AES y el pin externo, que
// NO están conectados: avisan una vez.
//
// LAS INTERRUPCIONES: al acabar un ciclo, si el canal está en INTn_SRCCFG con
// EN, pulso en DMA_INTn; si no, su bit de INT0_SRCFLG y DMA_INT0 alto hasta
// que se borre. Un error de bus apaga el canal, pone ERRCLR y DMA_ERR.
//
// LO QUE NO ESTÁ: scatter-gather (cycle_ctrl 4-7: error y aviso), las
// peticiones simples (dma_sreq) frente a las de ráfaga -todas son de ráfaga-,
// USEBURST, y los prot. [P-16 t]
// =============================================================================
#ifndef STM32_PERIPH_MSP432P4_UDMA_H
#define STM32_PERIPH_MSP432P4_UDMA_H

#include <array>
#include <functional>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "../../common/periph_base.h"
#include "../../soc/msp432p4/p4_desc.h"

namespace stm32 {

class UdmaP4 : public BusSlave {
public:
    static constexpr unsigned NCH = 8, NSRC = 8;
    tlm_utils::simple_initiator_socket<UdmaP4> mst{"mst"};   // al bus, como maestro
    sc_core::sc_out<bool> irq_int[4];                        // DMA_INT0..3
    sc_core::sc_out<bool> irq_err{"irq_err"};

    explicit UdmaP4(sc_core::sc_module_name nm) : BusSlave(nm, p4::DMA_B, 0x2000) {
        SC_HAS_PROCESS(UdmaP4);
        SC_THREAD(motor);
        SC_METHOD(irq_proc); sensitive << irq_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
        valores_de_reset();
    }

    // La señal de petición de la fuente `src` del canal `ch` (elaboración)
    void conecta_fuente(unsigned ch, unsigned src, const sc_core::sc_signal<bool>* s) {
        if (ch < NCH && src < NSRC) fuente_[ch][src] = s;
    }
    void end_of_elaboration() override {
        BusSlave::end_of_elaboration();
        for (auto& f : fuente_) for (const auto* s : f) if (s) espera_ |= s->value_changed_event();
        espera_ |= ev_;
    }

    // --- Lo que mira el banco ------------------------------------------------
    uint64_t transferencias() const { return n_trans_; }
    unsigned avisos() const { return n_avisos_; }

protected:
    std::array<std::array<const sc_core::sc_signal<bool>*, NSRC>, NCH> fuente_{};
    sc_core::sc_event_or_list espera_;
    sc_core::sc_event ev_, irq_ev_;
    uint32_t cfg_ = 0, ctlbase_ = 0, useburst_ = 0, reqmask_ = 0, ena_ = 0, alt_ = 0,
             prio_ = 0, err_ = 0, srccfg_[NCH] = {}, int_srccfg_[4] = {}, int0_flg_ = 0,
             sw_ = 0, auto_ = 0;
    bool pulso_[4] = {};
    uint64_t n_trans_ = 0;
    unsigned n_avisos_ = 0;
    bool av_sg_ = false, av_fuente_ = false;

    void valores_de_reset() {
        cfg_ = ctlbase_ = useburst_ = reqmask_ = ena_ = alt_ = prio_ = err_ = 0;
        for (uint32_t& s : srccfg_) s = 0;
        for (uint32_t& s : int_srccfg_) s = 0;
        int0_flg_ = sw_ = auto_ = 0;
    }
    void reset_proc() {
        if (rst_n.read()) return;
        valores_de_reset();
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
        ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void irq_proc() {
        irq_int[0].write(int0_flg_ != 0);
        for (unsigned k = 1; k < 4; ++k) {
            irq_int[k].write(pulso_[k]);
            if (pulso_[k]) { pulso_[k] = false; irq_ev_.notify(cycles(1)); }
        }
        irq_err.write(err_ != 0);
    }
    void aviso(bool& ya, const char* m) {
        if (ya) return;
        ya = true; ++n_avisos_;
        SC_REPORT_WARNING("msp432p4/udma", m);
    }

    // --- El bus, como maestro --------------------------------------------------
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

    // --- La máquina --------------------------------------------------------------
    bool pide(unsigned c) const {
        if (!(ena_ & (1u << c))) return false;
        if ((sw_ | auto_) & (1u << c)) return true;
        if (reqmask_ & (1u << c)) return false;
        const auto* s = fuente_[c][srccfg_[c] & 7u];
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
        for (unsigned k = 1; k < 4; ++k)
            if ((int_srccfg_[k] & 0x20u) && (int_srccfg_[k] & 0x1Fu) == c) {
                pulso_[k] = true;
                irq_ev_.notify(sc_core::SC_ZERO_TIME);
                return;
            }
        int0_flg_ |= 1u << c;
        irq_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void motor() {
        for (;;) {
            const int ci = elige();
            if (ci < 0) { wait(espera_); continue; }
            const unsigned c = unsigned(ci);
            sc_core::sc_time t = sc_core::SC_ZERO_TIME;
            const bool alt = (alt_ >> c) & 1u;
            const uint32_t est = (ctlbase_ & ~0xFFu) + (alt ? 0x80u : 0u) + 16u * c;
            uint32_t src_fin = 0, dst_fin = 0, ctl = 0;
            if (!lee32(est, src_fin, t) || !lee32(est + 4, dst_fin, t) || !lee32(est + 8, ctl, t)) {
                error_bus(c); wait(t + cycles(1)); continue;
            }
            const unsigned ciclo = ctl & 7u;
            if (ciclo == 0) {                            // estructura parada
                ena_ &= ~(1u << c); auto_ &= ~(1u << c); sw_ &= ~(1u << c);
                wait(t + cycles(1));
                continue;
            }
            if (ciclo >= 4) {
                aviso(av_sg_, "uDMA: scatter-gather (cycle_ctrl 4-7) no se modela: el canal "
                      "se para con error");
                error_bus(c); wait(t + cycles(1)); continue;
            }
            if (ciclo == 2) auto_ |= 1u << c;            // automático: sigue solo
            sw_ &= ~(1u << c);
            const unsigned resto = ((ctl >> 4) & 0x3FFu) + 1u;
            const unsigned r = 1u << ((ctl >> 14) & 0xFu);
            const unsigned tanda = resto < r ? resto : r;
            const unsigned ssz = (ctl >> 24) & 3u, sinc = (ctl >> 26) & 3u;
            const unsigned dsz = (ctl >> 28) & 3u, dinc = (ctl >> 30) & 3u;
            const unsigned nb = 1u << (ssz > 2 ? 2 : ssz);
            bool ok = true;
            for (unsigned j = 0; j < tanda && ok; ++j) {
                const unsigned queda = resto - j - 1u;          // los que faltan tras este
                // El fin puede ser el ÚLTIMO BYTE del búfer, no el principio
                // del último dato: es lo que escribe DriverLib (inicio +
                // bytes - 1), igual que TivaWare. En el bus de AHB los bits
                // bajos de una transferencia de 16 o 32 bits no cuentan.
                const unsigned dnb = 1u << (dsz > 2 ? 2 : dsz);
                const uint32_t sa = (sinc == 3 ? src_fin : src_fin - (queda << sinc)) & ~(nb - 1u);
                const uint32_t da = (dinc == 3 ? dst_fin : dst_fin - (queda << dinc)) & ~(dnb - 1u);
                uint8_t b[4] = {};
                ok = acceso(false, sa, b, nb, t);
                if (ok) ok = acceso(true, da, b, dnb, t);
                if (ok) ++n_trans_;
            }
            if (!ok) { error_bus(c); wait(t + cycles(1)); continue; }
            const unsigned quedan = resto - tanda;
            if (quedan) {
                ctl = (ctl & ~(0x3FFu << 4)) | ((quedan - 1u) << 4);
            } else {
                ctl &= ~((0x3FFu << 4) | 7u);            // cycle_ctrl = 0, n_minus_1 = 0
            }
            if (!escribe32(est + 8, ctl, t)) { error_bus(c); wait(t + cycles(1)); continue; }
            // Lo que cuesta arbitrar, además de los accesos [modelo]
            wait(t + cycles(2));
            if (quedan) continue;
            // Fin de ciclo
            auto_ &= ~(1u << c);
            if (ciclo == 3) {
                alt_ ^= 1u << c;                         // ping-pong: a la otra estructura
            } else {
                ena_ &= ~(1u << c);
            }
            avisa_fin(c);
        }
    }

    // --- Los registros ------------------------------------------------------------
    uint32_t reg_read(uint32_t off) override {
        if (off < 0x1000) {
            if (off == 0x000) return (NSRC << 8) | NCH;            // DEVICE_CFG
            if (off >= 0x010 && off < 0x010 + 4 * 32) {
                const unsigned c = (off - 0x010) / 4u;
                return c < NCH ? srccfg_[c] : 0u;
            }
            if (off >= 0x100 && off <= 0x108) return int_srccfg_[1 + (off - 0x100) / 4u];
            if (off == 0x110) return int0_flg_;
            return 0;
        }
        switch (off - 0x1000) {
            case 0x00: return ((NCH - 1u) << 16) | (cfg_ & 1u);     // STAT
            case 0x08: return ctlbase_;
            case 0x0C: return (ctlbase_ & ~0xFFu) + 0x80u;           // ALTBASE
            case 0x10: return 0;                                     // WAITSTAT
            case 0x18: return useburst_;
            case 0x20: return reqmask_;
            case 0x28: return ena_;
            case 0x30: return alt_;
            case 0x38: return prio_;
            case 0x4C: return err_;
            default:   return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        uint32_t m = 0;
        for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
        v &= m;
        const uint32_t todos = (1u << NCH) - 1u;
        if (off < 0x1000) {
            if (off == 0x004) { sw_ |= v & todos; despierta(); return; }   // SW_CHTRIG
            if (off >= 0x010 && off < 0x010 + 4 * NCH) {
                const unsigned c = (off - 0x010) / 4u;
                srccfg_[c] = v & 0xFFu;
                const unsigned s = srccfg_[c] & 7u;
                if (s != 0 && !fuente_[c][s])
                    aviso(av_fuente_, "uDMA: esa fuente de peticion (Timer_A, AES o el pin "
                          "externo) no esta conectada en el modelo: el canal solo se "
                          "movera por software");
                despierta();
                return;
            }
            if (off >= 0x100 && off <= 0x108) { int_srccfg_[1 + (off - 0x100) / 4u] = v & 0x3Fu; return; }
            if (off == 0x114) { int0_flg_ &= ~v; irq_ev_.notify(sc_core::SC_ZERO_TIME); return; }
            return;
        }
        switch (off - 0x1000) {
            case 0x04: cfg_ = v & 0xE1u; break;
            case 0x08: ctlbase_ = v & ~0x7Fu; break;
            case 0x14: sw_ |= v & todos; break;                   // SWREQ
            case 0x18: useburst_ |= v & todos; break;
            case 0x1C: useburst_ &= ~v; break;
            case 0x20: reqmask_ |= v & todos; break;
            case 0x24: reqmask_ &= ~v; break;
            case 0x28: ena_ |= v & todos; break;
            case 0x2C: ena_ &= ~v; auto_ &= ~v; break;
            case 0x30: alt_ |= v & todos; break;
            case 0x34: alt_ &= ~v; break;
            case 0x38: prio_ |= v & todos; break;
            case 0x3C: prio_ &= ~v; break;
            case 0x4C: if (v & 1u) err_ = 0; irq_ev_.notify(sc_core::SC_ZERO_TIME); break;
            default: return;
        }
        despierta();
    }
    void despierta() { ev_.notify(sc_core::SC_ZERO_TIME); }
};

} // namespace stm32
#endif // STM32_PERIPH_MSP432P4_UDMA_H
