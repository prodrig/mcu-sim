// =============================================================================
// soc_lab_m430.h — El SoC de laboratorio: un MSP430 sin chip
//
// M1 de doc/analisis_msp430.md, §3.6. Lo mínimo para ejecutar un programa
// MSP430 y saber cómo ha acabado, con el mismo mapa que el simulador de GDB
// (msp430-elf-run) para que un ELF enlazado con `-msim` corra igual en los
// dos y su salida se pueda comparar:
//
//   0x00000-0xFFFFF  RAM plana de 1 MB (la «Flash» también: es RAM)
//   0x00100-0x0011F  los registros del laboratorio (palabras):
//       +0x00 UART      escribir un byte lo saca
//       +0x02 FIN       escribir termina el programa con ese código
//       +0x04 CICLOS_L  el contador de ciclos de MCLK (32 bits; leer la
//       +0x06 CICLOS_H  baja congela la alta; escribir la baja lo pone a 0)
//       +0x08 PIDE      escribir la dirección de un vector lo deja pendiente
//       +0x0A QUITA     escribir la dirección de un vector lo quita
//       +0x0C NMI       escribir la dirección de un vector: pendiente y NMI
//                       (entra aunque GIE esté a 0)
//       +0x0E AUTO      bit 0: aceptar un vector lo quita (1, por omisión)
//       +0x10 RETARDO   escribir N: dentro de N ciclos de MCLK, pide el
//       +0x12 VECRET    vector de VECRET (para despertar de LPM)
//       +0x14 ACEPTADO  el último vector aceptado
//   las llamadas al sistema de libgloss: un CALL a 0x180+n (n = 1, exit con
//       R12; las demás devuelven -1 en R12), como hace el simulador de GDB;
//   C$$IO$$ y __CIOBUF__: la E/S de TI que usa el write() de libgloss
//       (orden 0xF3: escribir), si el ELF tiene esos símbolos.
//
// Los vectores de 0xFF80 a 0xFFFE: cuanto más alta la dirección, más
// prioridad. El de reset (0xFFFE) lo pone el ELF.
// =============================================================================
#ifndef STM32_M430_LAB_SOC_LAB_M430_H
#define STM32_M430_LAB_SOC_LAB_M430_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../cpu/nucleo_m430.h"
#include "../debug/pines_m430.h"
#include "../../common/analog_net.h"
#include "../../common/imagen_fw.h"

namespace stm32 {

SC_MODULE(SocLabM430), public m430_irq_if {
    tlm_utils::simple_target_socket<SocLabM430> bus{"bus"};
    sc_core::sc_signal<double> hz{"hz"};
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_n{"rst_n"};

    static constexpr uint32_t LAB = 0x0100, MEM = 1u << 20;

    NucleoM430 nucleo;
    bool eco_uart = false;
    // La depuración (M2): el EEM, el TAP y sus pines, de 4 hilos y de SBW
    EemM430 eem;
    TapM430 tap;
    AnalogNet jtag_tck{"jtag_tck"}, jtag_tms{"jtag_tms"}, jtag_tdi{"jtag_tdi"}, jtag_tdo{"jtag_tdo"},
              sbw_test{"sbw_test"}, sbw_rst{"sbw_rst"};
    PinesM430 pines;

    // `disparadores` del EEM: 2 (XS, como el G2553) por omisión
    SocLabM430(sc_core::sc_module_name nm, const M430Caps& caps, double hz_nucleo = 8e6, unsigned disparadores = 2)
        : sc_core::sc_module(nm), nucleo("nucleo", caps),
          eem(disparadores, disparadores <= 2 ? 1 : disparadores == 3 ? 2 : disparadores <= 5 ? 3 : 4),
          tap(caps.arq == M430Arq::CPU ? 0x89 : caps.arq == M430Arq::CPUX ? 0x89 : 0x91, caps.tiene_20bits(), &eem),
          pines("pines", tap, PinesM430::Pines{&jtag_tck, &jtag_tms, &jtag_tdi, &jtag_tdo, &sbw_test, &sbw_rst}),
          ram_(MEM, 0) {
        nucleo.tap = &tap;
        nucleo.eem = &eem;
        tap.pon_cpu(&nucleo);
        nucleo.al_por = [this](bool v) { rst_n.write(!v); };
        bus.register_b_transport(this, &SocLabM430::b_transport);
        bus.register_transport_dbg(this, &SocLabM430::transport_dbg);
        nucleo.bus.bind(bus);
        nucleo.clk_hz(hz);
        nucleo.rst_n(rst_n);
        hz.write(hz_nucleo);
        rst_n.write(false);
        nucleo.cpu.pon_irq(this);
        nucleo.cpu.llamada_lab = [this](uint32_t d) { return llamada(d); };
        SC_HAS_PROCESS(SocLabM430);
        SC_THREAD(retardo_proc);
        SC_METHOD(por_proc); sensitive << ev_por_; dont_initialize();
    }

    M430Cpu& cpu() { return nucleo.cpu; }

    // "" si se ha cargado; deja el chip en reset (lo suelta reset(false))
    std::string carga_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        std::string e = lee_elf(ruta, t, ELF_MSP430);
        if (!e.empty()) {
            if (formato_fw(ruta) == FormatoFw::TiTxt) e = lee_titxt(ruta, t);
            if (!e.empty()) return e;
        }
        std::fill(ram_.begin(), ram_.end(), 0);
        for (const TrozoFw& tr : t) {
            if (uint64_t(tr.dir) + tr.datos.size() > MEM) return ruta + ": un segmento se sale de 1 MB";
            std::copy(tr.datos.begin(), tr.datos.end(), ram_.begin() + tr.dir);
        }
        uint32_t s = 0;
        nucleo.cpu.pc_gancho = 0xFFFFFFFFu;
        nucleo.cpu.gancho = nullptr;
        cio_ = 0;
        if (simbolo_elf(ruta, "__CIOBUF__", s)) cio_ = s;
        if (cio_ && simbolo_elf(ruta, "C$$IO$$", s)) {
            nucleo.cpu.pc_gancho = s;
            nucleo.cpu.gancho = [this] { cio(); };
        }
        return std::string();
    }
    void reset(bool activo) {
        if (activo) {
            fin_ = false; codigo_ = 0; salida_.clear(); pend_ = nmi_ = 0; auto_ = true;
            ciclos0_ = 0; ultimo_ = 0;
        }
        nucleo.pide_parada(false);
        rst_n.write(!activo);
    }
    // Un POR que no borra lo del laboratorio (para la depuración)
    void por() { rst_n.write(false); ev_por_.notify(sc_core::sc_time(1, sc_core::SC_US)); }
    bool terminado() const { return fin_; }
    uint32_t codigo() const { return codigo_; }
    const sc_core::sc_event& ev_fin() const { return ev_fin_; }
    const std::string& salida() const { return salida_; }
    uint8_t ram8(uint32_t a) const { return a < MEM ? ram_[a] : 0; }
    uint16_t ram16(uint32_t a) const { return uint16_t(ram8(a) | (ram8(a + 1) << 8)); }
    void pon_ram16(uint32_t a, uint16_t v) { if (a + 1 < MEM) { ram_[a] = uint8_t(v); ram_[a + 1] = uint8_t(v >> 8); } }
    uint64_t accesos() const { return n_acc_; }

    // --- m430_irq_if -----------------------------------------------------------
    uint16_t pendiente(bool gie) override {
        const uint64_t p = gie ? pend_ : (pend_ & nmi_);
        if (!p) return 0;
        unsigned i = 63;
        while (!((p >> i) & 1u)) --i;
        return uint16_t(0xFF80u + 2 * i);
    }
    void acepta(uint16_t v) override {
        ultimo_ = v;
        if (auto_ && v >= 0xFF80u) { pend_ &= ~(uint64_t(1) << ((v - 0xFF80u) / 2)); nmi_ &= pend_; }
    }

private:
    std::vector<uint8_t> ram_;
    uint32_t cio_ = 0, codigo_ = 0;
    bool fin_ = false, auto_ = true;
    std::string salida_;
    sc_core::sc_event ev_fin_, ev_ret_;
    uint64_t n_acc_ = 0, pend_ = 0, nmi_ = 0, ciclos0_ = 0;
    uint32_t ciclos_alta_ = 0;
    uint16_t vecret_ = 0, ultimo_ = 0;
    unsigned retardo_ = 0;

    static unsigned bit_vec(uint32_t v) { return ((v & 0xFFFEu) - 0xFF80u) / 2; }
    bool es_vec(uint32_t v) const { return (v & 0xFFFFu) >= 0xFF80u; }
    void pide(uint32_t v, bool nmi) {
        if (!es_vec(v)) return;
        pend_ |= uint64_t(1) << bit_vec(v);
        if (nmi) nmi_ |= uint64_t(1) << bit_vec(v);
        nucleo.nota_irq();
    }
    void termina(uint32_t c) {
        if (fin_) return;
        codigo_ = c; fin_ = true; ev_fin_.notify(sc_core::SC_ZERO_TIME);
        nucleo.pide_parada(true);           // el núcleo no sigue: el final es determinista
    }

    // Las llamadas al sistema de libgloss (el simulador de GDB hace lo mismo)
    bool llamada(uint32_t d) {
        if ((d & ~0x3Fu) != 0x180u) return false;
        const unsigned n = d & 0x3Fu;
        if (n == 1) { termina(nucleo.cpu.reg(12) & 0xFFFFu); return true; }
        nucleo.cpu.pon_reg(12, 0xFFFFu);
        return true;
    }
    // La E/S de TI: solo «escribir» (0xF3) [libgloss msp430 cio.h]
    void cio() {
        const uint32_t b = cio_;
        const uint8_t orden = ram8(b + 2);
        if (orden == 0xF3) {
            const unsigned fd = ram16(b + 3), n = ram16(b + 5);
            std::string s;
            for (unsigned i = 0; i < n; ++i) s.push_back(char(ram8(b + 11 + i)));
            if (fd == 1 || fd == 2) {
                salida_ += s;
                if (eco_uart) { std::fwrite(s.data(), 1, s.size(), stdout); std::fflush(stdout); }
            }
            pon_ram16(b + 3, uint16_t(n));
        }
    }

    uint64_t ciclos_ahora() const { return nucleo.cpu.ciclos() - ciclos0_; }

    uint16_t lee_lab(uint32_t o) {
        switch (o) {
        case 0x04: { const uint64_t c = ciclos_ahora(); ciclos_alta_ = uint32_t(c >> 16); return uint16_t(c); }
        case 0x06: return uint16_t(ciclos_alta_);
        case 0x0E: return auto_ ? 1 : 0;
        case 0x12: return vecret_;
        case 0x14: return ultimo_;
        default: return 0;
        }
    }
    void escribe_lab(uint32_t o, uint16_t v) {
        switch (o) {
        case 0x00:
            salida_.push_back(char(v & 0xFF));
            if (eco_uart) { std::putchar(v & 0xFF); std::fflush(stdout); }
            break;
        case 0x02: termina(v); break;
        case 0x04: ciclos0_ = nucleo.cpu.ciclos(); break;
        case 0x08: pide(v, false); break;
        case 0x0A: if (es_vec(v)) { pend_ &= ~(uint64_t(1) << bit_vec(v)); nmi_ &= pend_; } break;
        case 0x0C: pide(v, true); break;
        case 0x0E: auto_ = v & 1u; break;
        case 0x10: retardo_ = v; ev_ret_.notify(nucleo.adelanto()); break;
        case 0x12: vecret_ = v; break;
        default: break;
        }
    }
    sc_core::sc_event ev_por_;
    void por_proc() { rst_n.write(true); }
    void retardo_proc() {
        for (;;) {
            wait(ev_ret_);
            if (!retardo_) continue;
            wait(nucleo.ciclos_tiempo(retardo_));
            retardo_ = 0;
            pide(vecret_, false);
        }
    }

    void acceso(tlm::tlm_generic_payload& gp, bool dbg) {
        const uint64_t a = gp.get_address();
        const unsigned n = gp.get_data_length();
        unsigned char* p = gp.get_data_ptr();
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        if (a + n > MEM) return;
        if (!dbg && a >= LAB && a < LAB + 0x20) {
            const uint32_t o = uint32_t(a - LAB) & ~1u;
            if (gp.is_write()) {
                uint16_t v = p[0];
                if (n == 2) v = uint16_t(v | (p[1] << 8));
                escribe_lab(o, v);
            } else {
                const uint16_t v = lee_lab(o);
                p[0] = uint8_t(v >> (8 * (a & 1)));
                if (n == 2) p[1] = uint8_t(v >> 8);
            }
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (gp.is_write()) std::copy(p, p + n, ram_.begin() + a);
        else std::copy(ram_.begin() + a, ram_.begin() + a + n, p);
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
    void b_transport(tlm::tlm_generic_payload& gp, sc_core::sc_time&) { ++n_acc_; acceso(gp, false); }
    unsigned transport_dbg(tlm::tlm_generic_payload& gp) { acceso(gp, true); return gp.get_data_length(); }
};

} // namespace stm32
#endif // STM32_M430_LAB_SOC_LAB_M430_H
