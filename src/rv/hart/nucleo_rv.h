// =============================================================================
// nucleo_rv.h — Un hart RISC-V en una simulación SystemC: el tiempo y el bus
//
// E1 de doc/analisis_esp32_riscv.md, §3.1. El hart (rv_hart.h) es C++ puro;
// este módulo es lo poco que hace falta para meterlo en un SoC, igual que
// `Cpu` hace con el Cortex-M4F:
//
//   * dos sockets TLM iniciadores, `ibus` (búsquedas) y `dbus` (datos), que
//     el integrador conecta a su decodificador -el C3 los separa en IRAM/
//     DRAM/AHB, el laboratorio los junta-;
//   * una extensión del payload (`RvExt`) con el hart, el modo efectivo y el
//     tipo de acceso, por si el bus filtra por ello (el PMS de un ESP32-C3);
//   * el tiempo LT con quantum y anotación de ciclos, como el Cortex: la
//     latencia que anota el bus se suma a los ciclos de la instrucción;
//   * wfi con el reloj parado, y la depuración: un hart parado no consume
//     tiempo y espera a que el Debug Module lo suelte.
//
// Las interrupciones las da un `rv_irq_if` que pone el integrador. Para que
// un hart dormido en wfi se entere de que una línea ha cambiado, el
// controlador llama a `nota_irq()`; si no lo hace, el hart mira cada quantum
// (como el Cortex en su bucle de sueño), así que no se queda dormido para
// siempre, solo tarda un poco más.
// =============================================================================
#ifndef STM32_RV_HART_NUCLEO_RV_H
#define STM32_RV_HART_NUCLEO_RV_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstdio>
#include "rv_hart.h"

namespace stm32 {

// La extensión que acompaña a cada transacción de un núcleo RISC-V
struct RvExt : tlm::tlm_extension<RvExt> {
    unsigned hart = 0;
    RvModo   modo = RvModo::M;
    RvAcceso acceso = RvAcceso::Load;
    bool     depurador = false;      // la hace el Debug Module (SBA), no el hart
    tlm::tlm_extension_base* clone() const override { return new RvExt(*this); }
    void copy_from(const tlm::tlm_extension_base& e) override {
        *this = static_cast<const RvExt&>(e);
    }
};

// Lo que el integrador pone alrededor del núcleo. Hoy, nada: el laboratorio y
// los chips de Espressif no tienen ventanas propias que el núcleo tenga que
// tratar aparte (el CLINT del C6 está en su mapa, como un periférico más).
// Existe para que un chip futuro que las tenga no tenga que tocar el núcleo,
// como `IntegracionNucleo` en el Cortex.
struct IntegracionRv {
    // Escrituras a periféricos que se sincronizan con el planificador antes de
    // salir (como `sync_escritura_perif` del Cortex). [perif_lo, perif_hi)
    uint32_t perif_lo = 0, perif_hi = 0;
};

SC_MODULE(NucleoRv), public rv_mem_if {
    tlm_utils::simple_initiator_socket<NucleoRv> ibus{"ibus"};
    tlm_utils::simple_initiator_socket<NucleoRv> dbus{"dbus"};
    sc_core::sc_in<double> clk_hz{"clk_hz"};
    sc_core::sc_in<bool>   rst_n{"rst_n"};

    RvHart hart;
    sc_core::sc_time quantum{1, sc_core::SC_US};
    IntegracionRv integracion;
    // Traza de ejecución (depuración del modelo)
    uint64_t traza_limite = 0;

    NucleoRv(sc_core::sc_module_name nm, const RvCaps& caps, unsigned hartid = 0)
        : sc_core::sc_module(nm), hart(caps, hartid) {
        hart.pon_memoria(this);
        SC_HAS_PROCESS(NucleoRv);
        SC_THREAD(run);
    }

    // --- Lo que llaman los de fuera ------------------------------------------
    // El controlador de interrupciones: una línea ha cambiado
    void nota_irq() { ev_irq_.notify(sc_core::SC_ZERO_TIME); }
    // El Debug Module (E2): parar, seguir, resetear
    void pide_parada(bool v)  { hart.pide_parada(v); ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    void pide_seguir()        { hart.pide_seguir(); ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    void pide_reset_hart()    { reset_pedido_ = true; ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    bool parado() const       { return hart.parado(); }
    bool durmiendo() const    { return hart.en_wfi(); }
    bool en_reset() const     { return en_reset_; }
    const sc_core::sc_event& ev_parado() const { return ev_parado_; }

    uint64_t instrucciones() const { return hart.instret(); }
    sc_core::sc_time ciclos_tiempo(unsigned n) const {
        const double f = clk_hz.read();
        return f > 0.0 ? sc_core::sc_time(double(n) * 1.0e12 / f, sc_core::SC_PS)
                       : sc_core::sc_time(double(n) * 10.0, sc_core::SC_NS);
    }

    // --- rv_mem_if: lo que pide el hart ---------------------------------------
    bool lee(uint32_t a, unsigned n, uint32_t& v, RvAcceso t, RvModo m) override {
        return bus(t == RvAcceso::Fetch ? ibus : dbus, false, a, n, v, t, m);
    }
    bool escribe(uint32_t a, unsigned n, uint32_t v, RvAcceso t, RvModo m) override {
        if (integracion.perif_hi > integracion.perif_lo &&
            a >= integracion.perif_lo && a < integracion.perif_hi) sincroniza();
        return bus(dbus, true, a, n, v, t, m);
    }

private:
    sc_core::sc_time local_{sc_core::SC_ZERO_TIME};
    sc_core::sc_event ev_irq_, ev_dbg_, ev_parado_;
    bool reset_pedido_ = false, en_reset_ = true;

    bool bus(tlm_utils::simple_initiator_socket<NucleoRv>& sk, bool escr, uint32_t a,
             unsigned n, uint32_t& v, RvAcceso t, RvModo m) {
        unsigned char buf[4];
        if (escr) for (unsigned i = 0; i < n; ++i) buf[i] = uint8_t(v >> (8 * i));
        tlm::tlm_generic_payload gp;
        RvExt ext;
        ext.hart = hart.hartid(); ext.modo = m; ext.acceso = t;
        gp.set_command(escr ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        gp.set_address(a);
        gp.set_data_ptr(buf);
        gp.set_data_length(n);
        gp.set_streaming_width(n);
        gp.set_byte_enable_ptr(nullptr);
        gp.set_dmi_allowed(false);
        gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        gp.set_extension(&ext);
        sc_core::sc_time d = sc_core::SC_ZERO_TIME;
        sk->b_transport(gp, d);
        gp.clear_extension(&ext);
        local_ += d;
        if (gp.get_response_status() != tlm::TLM_OK_RESPONSE) return false;
        if (!escr) {
            v = 0;
            for (unsigned i = 0; i < n; ++i) v |= uint32_t(buf[i]) << (8 * i);
        }
        return true;
    }

    void sincroniza() {
        if (local_ > sc_core::SC_ZERO_TIME) { wait(local_); local_ = sc_core::SC_ZERO_TIME; }
    }
    void consume(const sc_core::sc_time& t) {
        local_ += t;
        if (local_ >= quantum) sincroniza();
    }

    void run() {
        for (;;) {
            // En reset: quieto hasta que se suelte
            en_reset_ = true;
            while (!rst_n.read()) wait(rst_n.value_changed_event());
            hart.reset();
            reset_pedido_ = false;
            en_reset_ = false;
            if (hart.parado()) ev_parado_.notify(sc_core::SC_ZERO_TIME);
            for (;;) {
                if (!rst_n.read()) { sincroniza(); break; }
                if (reset_pedido_) { sincroniza(); hart.reset(); reset_pedido_ = false;
                                     if (hart.parado()) ev_parado_.notify(sc_core::SC_ZERO_TIME); }
                const bool estaba = hart.parado();
                const RvHart::Paso p = hart.paso();
                if (p.parado) {
                    sincroniza();
                    if (!estaba) ev_parado_.notify(sc_core::SC_ZERO_TIME);
                    wait(ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                if (p.dormido) {
                    sincroniza();
                    wait(quantum, ev_irq_ | ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                if (traza_limite && hart.instret() <= traza_limite)
                    std::printf("[%s] pc=0x%08x\n", name(), hart.pc());
                consume(ciclos_tiempo(p.ciclos));
            }
        }
    }
};

} // namespace stm32
#endif // STM32_RV_HART_NUCLEO_RV_H
