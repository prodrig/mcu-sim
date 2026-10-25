// =============================================================================
// dma_canal.h — El DMA por canales (fase S3 del F303)
//
// El DMA de los F0, F1, F3, L0 y L1 —y, con un multiplexor delante, el de la
// L4 y la G4— [RM0316 §13]. NO es el del F4 (`dma.h`), que tiene flujos,
// canales por flujo, FIFO, ráfagas y doble buffer: este es mucho más sencillo,
// y por eso es otro modelo:
//
//   * hasta siete CANALES, cada uno con cuatro registros: CCR, CNDTR, CPAR y
//     CMAR, en 0x08 + 0x14 · (x − 1);
//   * las peticiones de los periféricos llegan a cada canal ya sumadas (un OR
//     fijo: la tabla 76 del RM0316 para el DMA1 del F303xC); el firmware no
//     elige nada, salvo los remapeos de SYSCFG_CFGR1 —eso lo cablea el SoC—;
//   * un árbitro por prioridad de software (PL) y, a igualdad, el canal de
//     número más bajo [§13.4.3]; una transferencia por concesión, «leer y
//     después escribir», y vuelta a arbitrar;
//   * anchuras de 8, 16 y 32 bits a cada lado, con la tabla 79 de las
//     alineaciones: si el destino es más ancho se rellena con ceros, si es más
//     estrecho se queda con la parte baja;
//   * circular, memoria a memoria, y las banderas GIF/TCIF/HTIF/TEIF con su
//     borrado por IFCR (CGIFx borra las cuatro);
//   * el error de transferencia: un acceso que el bus rechaza apaga el canal
//     (EN a cero) y pone TEIF; EN no vuelve a ponerse mientras TEIF esté
//     puesto [§13.4.6, §13.6.3].
//
// LO QUE SE PROTEGE: con EN = 1 no se escriben MEM2MEM, PL, MSIZE, PSIZE,
// MINC, PINC, DIR ni CNDTR [§13.6.3, §13.6.4]. Y el firmware que intenta
// «suspender y reanudar» (EN a cero y otra vez a uno sin reconfigurar) no está
// soportado en el silicio [§13.4.4]; aquí sigue por donde iba, que es lo
// razonable que podría pasar, y no se avisa.
//
// LAS PETICIONES: un periférico pide con un NIVEL (la USART, el SPI y el I²C
// mientras TXE o RXNE) o con un PULSO (los temporizadores, que en el silicio
// lo retira el reconocimiento). Para no perder un pulso mientras el DMA sirve
// otro canal, cada flanco de subida se queda apuntado hasta que se sirve.
//
// LA MATRIZ NO SE MODELA: cada acceso factura un ciclo de HCLK por fase, sin
// contención con la CPU. Es lo que el análisis llama el riesgo 6, y queda
// dicho en `limitaciones()` del SoC.
// =============================================================================
#ifndef STM32_PERIPH_DMA_CANAL_H
#define STM32_PERIPH_DMA_CANAL_H

#include <cstdio>
#include <functional>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/periph_base.h"

namespace stm32 {

class DmaCanal : public BusSlave {
public:
    static constexpr unsigned MAX_CANALES = 7;

    tlm_utils::simple_initiator_socket<DmaCanal> m{"m"};      // el maestro AHB
    sc_core::sc_vector<sc_core::sc_in<bool>>  req;           // [n] ya sumadas
    sc_core::sc_vector<sc_core::sc_out<bool>> irq;           // [n] una por canal

    enum : uint32_t { R_ISR = 0x00, R_IFCR = 0x04, R_C1 = 0x08, PASO = 0x14 };
    enum : uint32_t { CCR = 0x00, CNDTR = 0x04, CPAR = 0x08, CMAR = 0x0C };
    static constexpr uint32_t CCR_EN = 1u << 0, CCR_TCIE = 1u << 1, CCR_HTIE = 1u << 2,
        CCR_TEIE = 1u << 3, CCR_DIR = 1u << 4, CCR_CIRC = 1u << 5, CCR_PINC = 1u << 6,
        CCR_MINC = 1u << 7, CCR_MEM2MEM = 1u << 14;
    enum : unsigned { F_GIF = 0, F_TCIF = 1, F_HTIF = 2, F_TEIF = 3 };

    DmaCanal(sc_core::sc_module_name nm, uint32_t base, unsigned n_canales)
        : BusSlave(nm, base, 0x400), req("req", n_canales), irq("irq", n_canales),
          n_(n_canales) {
        SC_HAS_PROCESS(DmaCanal);
        SC_THREAD(motor);
        SC_METHOD(req_proc);
        for (unsigned i = 0; i < n_; ++i) sensitive << req[i];
        dont_initialize();
        SC_METHOD(pub_proc);   sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }

    unsigned canales() const { return n_; }
    // La última transferencia de un bloque (CNDTR llega a cero, circular o no):
    // la señal DMA_EOT que el ADC de los F3 usa para dejar de pedir en el modo
    // de una vez (DMACFG = 0) [RM0316 §15.3.26]. Se llama con el canal (0..n-1)
    std::function<void(unsigned)> al_terminar;
    // Para el banco
    uint64_t transferencias() const { return n_trf_; }
    uint32_t cndtr(unsigned c) const { return c < n_ ? ch_[c].ndt : 0; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off == R_ISR) return isr_;
        if (off == R_IFCR) return 0;
        unsigned c; uint32_t r;
        if (!canal_de(off, c, r)) return 0;
        const Canal& x = ch_[c];
        switch (r) {
            case CCR:   return x.ccr;
            case CNDTR: return x.ndt;
            case CPAR:  return x.par;
            case CMAR:  return x.mar;
            default:    return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = (off == R_IFCR) ? 0u : reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        if (off == R_ISR) return;
        if (off == R_IFCR) {
            for (unsigned c = 0; c < n_; ++c) {
                const uint32_t f = (v >> (4 * c)) & 0xFu;
                if (f & 1u) isr_ &= ~(0xFu << (4 * c));    // CGIF: las cuatro
                else isr_ &= ~(f << (4 * c));
                recalcula_gif(c);
            }
            actualiza();
            return;
        }
        unsigned c; uint32_t r;
        if (!canal_de(off, c, r)) return;
        Canal& x = ch_[c];
        const bool en = x.ccr & CCR_EN;
        switch (r) {
            case CCR: {
                // Con EN = 1 solo cambian EN, CIRC y las tres habilitaciones de
                // interrupción [RM0316 §13.6.3]
                const uint32_t w = en ? (CCR_EN | CCR_TCIE | CCR_HTIE | CCR_TEIE | CCR_CIRC) : 0x7FFFu;
                uint32_t nv = (x.ccr & ~w) | (v & w);
                // EN no vuelve a ponerse mientras TEIF siga puesto [§13.4.6]
                if (!en && (nv & CCR_EN) && bandera(c, F_TEIF)) nv &= ~CCR_EN;
                x.ccr = nv;
                if (!en && (nv & CCR_EN)) arranca(c);
                if (en && !(nv & CCR_EN)) x.activo = false;
                break;
            }
            case CNDTR: if (!en) { x.ndt = v & 0xFFFFu; x.ndt_prog = x.ndt; } break;
            case CPAR:  x.par = v; if (!en) x.pa = v; break;
            case CMAR:  x.mar = v; if (!en) x.ma = v; break;
            default: break;
        }
        actualiza();
        trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    struct Canal {
        uint32_t ccr = 0, ndt = 0, ndt_prog = 0, par = 0, mar = 0;
        uint32_t pa = 0, ma = 0;           // los punteros internos
        bool activo = false, mitad = false, pulso = false;
    };
    Canal ch_[MAX_CANALES];
    uint32_t isr_ = 0;
    const unsigned n_;
    bool o_irq_[MAX_CANALES] = {};
    bool req_ant_[MAX_CANALES] = {};
    uint64_t n_trf_ = 0;
    sc_core::sc_event pub_ev_, trabajo_ev_;
    AhbExt ext_;

    bool canal_de(uint32_t off, unsigned& c, uint32_t& r) const {
        if (off < R_C1) return false;
        c = (off - R_C1) / PASO;
        r = (off - R_C1) % PASO;
        return c < n_ && r <= CMAR;
    }
    bool bandera(unsigned c, unsigned f) const { return (isr_ >> (4 * c + f)) & 1u; }
    void pon(unsigned c, unsigned f) { isr_ |= 1u << (4 * c + f); recalcula_gif(c); }
    void recalcula_gif(unsigned c) {
        if (isr_ & (0xEu << (4 * c))) isr_ |= 1u << (4 * c);
        else isr_ &= ~(1u << (4 * c));
    }
    void actualiza() {
        for (unsigned c = 0; c < n_; ++c) {
            const uint32_t cc = ch_[c].ccr;
            o_irq_[c] = (bandera(c, F_TCIF) && (cc & CCR_TCIE)) ||
                        (bandera(c, F_HTIF) && (cc & CCR_HTIE)) ||
                        (bandera(c, F_TEIF) && (cc & CCR_TEIE));
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
    void pub_proc() { for (unsigned c = 0; c < n_; ++c) irq[c].write(o_irq_[c]); }
    void reset_proc() {
        if (rst_n.read()) return;
        for (unsigned c = 0; c < n_; ++c) ch_[c] = Canal();
        isr_ = 0;
        actualiza();
    }
    // Los flancos de subida se apuntan: un pulso no se pierde aunque el DMA
    // esté sirviendo otro canal
    void req_proc() {
        for (unsigned c = 0; c < n_; ++c) {
            const bool r = req[c].read();
            if (r && !req_ant_[c]) ch_[c].pulso = true;
            req_ant_[c] = r;
        }
        trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    void arranca(unsigned c) {
        Canal& x = ch_[c];
        x.pa = x.par; x.ma = x.mar;
        x.ndt_prog = x.ndt;
        x.mitad = false;
        x.activo = x.ndt != 0;
        // Un pulso que llegó antes de armar el canal no cuenta
        x.pulso = false;
        trabajo_ev_.notify(sc_core::SC_ZERO_TIME);
    }

    bool listo(unsigned c) const {
        const Canal& x = ch_[c];
        if (!x.activo || !(x.ccr & CCR_EN) || x.ndt == 0) return false;
        if (x.ccr & CCR_MEM2MEM) return true;
        return req[c].read() || x.pulso;
    }
    int arbitra() const {
        int mejor = -1; unsigned pl = 0;
        for (unsigned c = 0; c < n_; ++c) {
            if (!listo(c)) continue;
            const unsigned p = (ch_[c].ccr >> 12) & 3u;
            if (mejor < 0 || p > pl) { mejor = int(c); pl = p; }
        }
        return mejor;
    }

    sc_core::sc_time hclk() const {
        const double hz = domain_hz();
        return hz > 0 ? sc_core::sc_time(1.0e12 / hz, sc_core::SC_PS) : sc_core::sc_time(14, sc_core::SC_NS);
    }
    static unsigned tam(unsigned code) { return code >= 2 ? 4u : (1u << code); }

    bool acceso(bool escribe, uint32_t a, uint32_t& dato, unsigned n, sc_core::sc_time& t) {
        tlm::tlm_generic_payload gp;
        unsigned char b[4];
        for (unsigned i = 0; i < 4; ++i) b[i] = uint8_t(dato >> (8 * i));
        gp.set_command(escribe ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        gp.set_address(a);
        gp.set_data_ptr(b);
        gp.set_data_length(n);
        gp.set_streaming_width(n);
        gp.set_byte_enable_ptr(nullptr);
        gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        ext_ = AhbExt();
        ext_.master = BusMaster::DMA1_MEM;
        gp.set_extension(&ext_);
        sc_core::sc_time l = sc_core::SC_ZERO_TIME;
        m->b_transport(gp, l);
        gp.clear_extension(&ext_);
        t += l;
        if (!escribe) { dato = 0; for (unsigned i = 0; i < n; ++i) dato |= uint32_t(b[i]) << (8 * i); }
        return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
    }

    void motor() {
        for (;;) {
            if (!rst_n.read() || !clock_enabled()) {
                wait(trabajo_ev_ | rst_n.value_changed_event());
                continue;
            }
            const int k = arbitra();
            if (k < 0) { wait(trabajo_ev_ | rst_n.value_changed_event()); continue; }
            sirve(unsigned(k));
        }
    }

    // Una transferencia: leer del origen y escribir en el destino [§13.4.2]
    void sirve(unsigned c) {
        Canal& x = ch_[c];
        x.pulso = false;
        const bool dir = x.ccr & CCR_DIR;                 // 1: de memoria
        const unsigned ps = tam((x.ccr >> 8) & 3u), ms = tam((x.ccr >> 10) & 3u);
        const unsigned s_tam = dir ? ms : ps, d_tam = dir ? ps : ms;
        // La dirección se alinea al tamaño [§13.6.5, §13.6.6]
        const uint32_t s_a = (dir ? x.ma : x.pa) & ~(s_tam - 1u);
        const uint32_t d_a = (dir ? x.pa : x.ma) & ~(d_tam - 1u);
        sc_core::sc_time t = hclk();
        uint32_t dato = 0;
        bool ok = acceso(false, s_a, dato, s_tam, t);
        if (ok) {
            // Tabla 79: más ancho, con ceros; más estrecho, la parte baja
            if (d_tam < 4) dato &= (1u << (8 * d_tam)) - 1u;
            ok = acceso(true, d_a, dato, d_tam, t);
        }
        wait(t);
        if (!ok) {
            // Error de transferencia: el canal se apaga [§13.4.6]
            x.ccr &= ~CCR_EN; x.activo = false;
            pon(c, F_TEIF);
            actualiza();
            aviso_error(c, s_a, d_a);
            return;
        }
        ++n_trf_;
        if (x.ccr & CCR_PINC) x.pa += ps;
        if (x.ccr & CCR_MINC) x.ma += ms;
        --x.ndt;
        if (!x.mitad && x.ndt <= x.ndt_prog / 2) { x.mitad = true; pon(c, F_HTIF); }
        if (x.ndt == 0) {
            pon(c, F_TCIF);
            if (al_terminar) al_terminar(c);
            if ((x.ccr & CCR_CIRC) && !(x.ccr & CCR_MEM2MEM)) {
                x.ndt = x.ndt_prog; x.pa = x.par; x.ma = x.mar; x.mitad = false;
            } else {
                x.activo = false;                     // CNDTR se queda a cero
            }
        }
        actualiza();
    }

    bool avisado_ = false;
    void aviso_error(unsigned c, uint32_t s, uint32_t d) {
        if (avisado_) return;
        avisado_ = true;
        char m[200];
        std::snprintf(m, sizeof m, "%s: error de transferencia en el canal %u (de 0x%08X a "
                      "0x%08X): TEIF, y el canal se apaga [RM0316 13.4.6]", name(), c + 1,
                      unsigned(s), unsigned(d));
        SC_REPORT_WARNING("dma_canal", m);
    }
};

} // namespace stm32
#endif // STM32_PERIPH_DMA_CANAL_H
