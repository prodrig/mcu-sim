// =============================================================================
// soc_lab_rv.h — El SoC de laboratorio: un RISC-V sin fabricante
//
// E1 de doc/analisis_esp32_riscv.md, §3.8. Lo mínimo para ejecutar un
// programa RISC-V y saber cómo ha acabado, con el MISMO mapa que la
// plataforma por omisión de Spike, para que un ELF corra igual en los dos y
// las trazas se puedan comparar:
//
//   0x0200_0000  CLINT (msip, mtimecmp, mtime)                [rv/irq/clint.h]
//   0x1000_0000  una UART de pega: escribir en +0 saca un carácter, y +5 (LSR)
//                dice siempre «transmisor vacío» (0x60), como un 16550
//   0x8000_0000  RAM, sin estados de espera
//   tohost       la interfaz HTIF de riscv-tests: escribir (código << 1) | 1
//                termina el programa con ese código (0 = bien). La dirección
//                es la del símbolo `tohost` del ELF.
//
// Sirve para tres cosas: pasar las pruebas de conformidad (riscv-tests) y las
// propias del núcleo; comparar trazas con Spike; y probar la depuración
// (E2) sin chip. Es lo primero que monta un MCU RISC-V futuro antes de
// escribir su SoC: si algo falla aquí, es del núcleo.
//
// Puede tener varios harts (para el Debug Module con `hartsel`, E2). Todos
// comparten la RAM, el CLINT y la UART.
//
// LA DEPURACIÓN (E2). Un Debug Module 0.13 (`dm`) con todos los harts y su
// SBA en el bus, un DTM JTAG (`tap`, IDCODE 0x10AB1001) y el TAP en cinco
// nodos sueltos (`jtag_tck`... `jtag_ntrst`), donde se suelda la sonda que
// sea: la de bancada (verif/sonda_jtag_rv.h), la de OpenOCD por
// remote_bitbang o la CMSIS-DAP. ndmreset resetea los harts y el CLINT.
// =============================================================================
#ifndef STM32_RV_LAB_SOC_LAB_RV_H
#define STM32_RV_LAB_SOC_LAB_RV_H

#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "../hart/nucleo_rv.h"
#include "../irq/clint.h"
#include "../debug/rv_dm.h"
#include "../debug/rv_dtm_jtag.h"
#include "../debug/jtag_pines.h"
#include "../../common/analog_net.h"
#include "../../common/imagen_fw.h"

namespace stm32 {

SC_MODULE(SocLabRv) {
    // Por aquí entran los buses de los harts y, en E2, el del Debug Module
    tlm_utils::multi_passthrough_target_socket<SocLabRv> bus{"bus"};
    sc_core::sc_signal<double> hz{"hz"};
    // El reset lo escriben el banco y el ndmreset del DM (desde el hilo del depurador)
    sc_core::sc_signal<bool, sc_core::SC_MANY_WRITERS> rst_n{"rst_n"};

    static constexpr uint32_t RAM_BASE = 0x80000000u, CLINT_BASE = 0x02000000u,
                              UART_BASE = 0x10000000u;

    static constexpr uint32_t IDCODE = 0x10AB1001u;

    std::vector<std::unique_ptr<NucleoRv>> harts;
    std::vector<std::unique_ptr<IrqEstandar>> irqs;
    Clint clint;
    // La depuración
    AnalogNet jtag_tck{"jtag_tck"}, jtag_tms{"jtag_tms"}, jtag_tdi{"jtag_tdi"},
              jtag_tdo{"jtag_tdo"}, jtag_ntrst{"jtag_ntrst"};
    std::unique_ptr<RvDm> dm;
    std::unique_ptr<RvDtmJtag> tap;
    std::unique_ptr<JtagPines> jtag;

    SocLabRv(sc_core::sc_module_name nm, const std::vector<const RvCaps*>& caps,
             uint32_t ram_bytes = 4u << 20, double hz_nucleo = 100e6)
        : sc_core::sc_module(nm), clint("clint", unsigned(caps.size())), ram_(ram_bytes, 0) {
        bus.register_b_transport(this, &SocLabRv::b_transport);
        hz.write(hz_nucleo);
        rst_n.write(false);
        for (unsigned h = 0; h < caps.size(); ++h) {
            harts.emplace_back(new NucleoRv(sc_core::sc_gen_unique_name("hart"), *caps[h], h));
            NucleoRv& n = *harts.back();
            n.ibus.bind(bus);
            n.dbus.bind(bus);
            n.clk_hz(hz);
            n.rst_n(rst_n);
            irqs.emplace_back(new IrqEstandar(clint, h));
            n.hart.pon_irq(irqs.back().get());
            clint.engancha(h, &n);
        }
        std::vector<NucleoRv*> hs;
        for (auto& h : harts) hs.push_back(h.get());
        dm.reset(new RvDm("dm", hs));
        dm->sba.bind(bus);
        dm->ndmreset = [this](bool v) {
            rst_n.write(!v);
            if (v) clint.reset();
        };
        tap.reset(new RvDtmJtag(*dm, IDCODE, 7));
        jtag.reset(new JtagPines("jtag", *tap,
                                 JtagPines::Pines{&jtag_tck, &jtag_tms, &jtag_tdi, &jtag_tdo,
                                                  &jtag_ntrst}));
    }

    NucleoRv& hart(unsigned i = 0) { return *harts.at(i); }

    // --- Cargar y saber cómo ha acabado --------------------------------------
    // "" si se ha cargado. Deja el chip en reset: lo suelta `arranca()`.
    std::string carga_elf(const std::string& ruta) {
        std::vector<TrozoFw> t;
        const std::string e = lee_elf(ruta, t, ELF_RISCV);
        if (!e.empty()) return e;
        std::fill(ram_.begin(), ram_.end(), 0);
        for (const TrozoFw& tr : t) {
            if (tr.dir < RAM_BASE || uint64_t(tr.dir) + tr.datos.size() > RAM_BASE + ram_.size())
                return ruta + ": un segmento cae fuera de la RAM del laboratorio";
            std::copy(tr.datos.begin(), tr.datos.end(), ram_.begin() + (tr.dir - RAM_BASE));
        }
        tohost_ = 0;
        simbolo_elf(ruta, "tohost", tohost_);
        return std::string();
    }
    void reset(bool activo) {
        if (activo) { fin_ = false; codigo_ = 0; salida_.clear(); clint.reset(); }
        rst_n.write(!activo);
    }
    bool terminado() const { return fin_; }
    uint32_t codigo() const { return codigo_; }
    const sc_core::sc_event& ev_fin() const { return ev_fin_; }
    const std::string& salida() const { return salida_; }
    uint32_t tohost() const { return tohost_; }
    // Acceso directo a la RAM, para las pruebas
    uint32_t ram32(uint32_t a) const {
        if (a < RAM_BASE || a + 4 > RAM_BASE + ram_.size()) return 0;
        const uint8_t* p = &ram_[a - RAM_BASE];
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    void pon_ram32(uint32_t a, uint32_t v) {
        if (a < RAM_BASE || a + 4 > RAM_BASE + ram_.size()) return;
        for (unsigned i = 0; i < 4; ++i) ram_[a - RAM_BASE + i] = uint8_t(v >> (8 * i));
    }
    // Lo que se hace con cada carácter de la UART (por omisión, guardarlo)
    bool eco_uart = false;
    uint64_t accesos() const { return n_acc_; }

private:
    std::vector<uint8_t> ram_;
    uint32_t tohost_ = 0, codigo_ = 0;
    bool fin_ = false;
    std::string salida_;
    sc_core::sc_event ev_fin_;
    uint64_t n_acc_ = 0;

    void b_transport(int, tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        ++n_acc_;
        const uint64_t a = gp.get_address();
        const unsigned n = gp.get_data_length();
        unsigned char* p = gp.get_data_ptr();
        gp.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        if (a >= RAM_BASE && a + n <= RAM_BASE + ram_.size()) {
            uint8_t* m = &ram_[size_t(a - RAM_BASE)];
            if (gp.is_write()) {
                std::copy(p, p + n, m);
                if (tohost_ && a == tohost_) htif();
            } else {
                std::copy(m, m + n, p);
            }
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        if (a >= CLINT_BASE && a < CLINT_BASE + 0x10000u) {
            clint.acceso(gp, uint32_t(a - CLINT_BASE));
            return;
        }
        if (a >= UART_BASE && a < UART_BASE + 0x100u) {
            const uint32_t o = uint32_t(a - UART_BASE);
            if (gp.is_write()) {
                if (o == 0) {
                    salida_.push_back(char(p[0]));
                    if (eco_uart) { std::putchar(p[0]); std::fflush(stdout); }
                }
            } else {
                for (unsigned i = 0; i < n; ++i) p[i] = 0;
                if (o == 5) p[0] = 0x60;
            }
            gp.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
    }

    void htif() {
        const uint32_t v = ram32(tohost_);
        if (!(v & 1u)) return;                    // una llamada al sistema: no hay
        codigo_ = v >> 1;
        fin_ = true;
        ev_fin_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_RV_LAB_SOC_LAB_RV_H
