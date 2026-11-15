// =============================================================================
// nucleo_m430.h — Un núcleo MSP430 en una simulación SystemC
//
// M1 de doc/analisis_msp430.md, §3.1. Como NucleoRv: el núcleo (m430_exec.h)
// es C++ puro, y este módulo es lo poco que hace falta para meterlo en un SoC:
//
//   * UN socket TLM iniciador (`bus`): el MSP430 es von Neumann y tiene un
//     solo bus. La extensión `M430Ext` dice si el acceso es una búsqueda, un
//     dato, la pila o un vector, y si lo hace el depurador;
//   * el tiempo LT con quantum: los ciclos de la tabla de su CPU más la
//     latencia que anote el bus (los estados de espera de la FRAM o la
//     Flash, que son del controlador de memoria);
//   * bajo consumo con el reloj parado: con CPUOFF el núcleo no consume
//     tiempo y espera a una interrupción (`nota_irq()`) o al quantum;
//   * DOS MODOS DE AVANCE: por instrucciones (el normal, con MCLK) o por
//     ciclos de TCLK, que da la depuración por JTAG (M2): cada `tclk()` es un
//     ciclo; una instrucción se ejecuta en el primero y consume los
//     siguientes hasta completar sus ciclos.
//   * la parada de depuración: un núcleo parado no ejecuta ni consume tiempo.
// =============================================================================
#ifndef STM32_M430_CPU_NUCLEO_M430_H
#define STM32_M430_CPU_NUCLEO_M430_H

#include <cstdio>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "m430_exec.h"

namespace stm32 {

struct M430Ext : tlm::tlm_extension<M430Ext> {
    M430Acceso acceso = M430Acceso::Lectura;
    bool depurador = false;
    tlm::tlm_extension_base* clone() const override { return new M430Ext(*this); }
    void copy_from(const tlm::tlm_extension_base& e) override { *this = static_cast<const M430Ext&>(e); }
};

SC_MODULE(NucleoM430), public m430_mem_if {
    tlm_utils::simple_initiator_socket<NucleoM430> bus{"bus"};
    sc_core::sc_in<double> clk_hz{"clk_hz"};       // MCLK
    sc_core::sc_in<bool>   rst_n{"rst_n"};

    M430Cpu cpu;
    sc_core::sc_time quantum{1, sc_core::SC_US};
    uint64_t traza_limite = 0;

    NucleoM430(sc_core::sc_module_name nm, const M430Caps& caps) : sc_core::sc_module(nm), cpu(caps) {
        cpu.pon_memoria(this);
        SC_HAS_PROCESS(NucleoM430);
        SC_THREAD(run);
        SC_METHOD(baja_rst);
        sensitive << rst_n.neg();
        dont_initialize();
    }

    // --- Lo que llaman los de fuera ------------------------------------------
    void nota_irq() { ev_irq_.notify(sc_core::SC_ZERO_TIME); }
    // La depuración (M2)
    void pide_parada(bool v) { parada_ = v; ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    bool parado() const { return parado_; }
    // Una instrucción estando parado (el «single step» de la depuración)
    void pide_paso() { pasos_++; ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    // El avance por TCLK
    void modo_tclk(bool v) { tclk_ = v; credito_ = 0; ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    bool en_modo_tclk() const { return tclk_; }
    void tclk(unsigned n = 1) { credito_ += int64_t(n); ev_tclk_.notify(sc_core::SC_ZERO_TIME); }
    bool en_reset() const { return en_reset_; }
    bool durmiendo() const { return cpu.durmiendo(); }
    const sc_core::sc_event& ev_parado() const { return ev_parado_; }

    sc_core::sc_time ciclos_tiempo(unsigned n) const {
        const double f = clk_hz.read();
        return f > 0.0 ? sc_core::sc_time(double(n) * 1.0e12 / f, sc_core::SC_PS)
                       : sc_core::sc_time(double(n) * 1000.0, sc_core::SC_NS);
    }
    // El tiempo que el núcleo lleva adelantado (LT): para quien lea un contador
    const sc_core::sc_time& adelanto() const { return local_; }

    // --- m430_mem_if -----------------------------------------------------------
    bool lee(uint32_t a, unsigned n, uint32_t& v, M430Acceso t) override {
        return acceso(false, a, n, v, t);
    }
    bool escribe(uint32_t a, unsigned n, uint32_t v, M430Acceso t) override {
        // Una escritura a un periférico se ve en su sitio en el tiempo
        if (a < perif_hi) sincroniza();
        return acceso(true, a, n, v, t);
    }
    // Por debajo de esta dirección, las escrituras se sincronizan (los periféricos)
    uint32_t perif_hi = 0x1000;

private:
    sc_core::sc_time local_{sc_core::SC_ZERO_TIME};
    tlm::tlm_generic_payload gp_;
    sc_core::sc_event ev_irq_, ev_dbg_, ev_parado_, ev_tclk_;
    bool en_reset_ = true, rst_bajo_ = false, parada_ = false, parado_ = false, tclk_ = false;
    unsigned pasos_ = 0;
    int64_t credito_ = 0;
    void baja_rst() { rst_bajo_ = true; }

    bool acceso(bool escr, uint32_t a, unsigned n, uint32_t& v, M430Acceso t) {
        unsigned char buf[2];
        if (escr) for (unsigned i = 0; i < n; ++i) buf[i] = uint8_t(v >> (8 * i));
        tlm::tlm_generic_payload& gp = gp_;
        M430Ext ext;
        ext.acceso = t;
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
        bus->b_transport(gp, d);
        gp.clear_extension(&ext);
        local_ += d;
        if (gp.get_response_status() != tlm::TLM_OK_RESPONSE) { if (!escr) v = 0; return false; }
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
            en_reset_ = true;
            while (!rst_n.read()) wait(rst_n.value_changed_event());
            rst_bajo_ = false;
            cpu.reset();
            en_reset_ = false;
            for (;;) {
                if (!rst_n.read() || rst_bajo_) { sincroniza(); break; }
                // Parado por la depuración: quieto, salvo un paso pedido
                if (parada_ && !pasos_) {
                    sincroniza();
                    if (!parado_) { parado_ = true; ev_parado_.notify(sc_core::SC_ZERO_TIME); }
                    wait(ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                parado_ = false;
                // Por TCLK: hace falta un ciclo para empezar
                if (tclk_ && credito_ <= 0) {
                    sincroniza();
                    wait(ev_tclk_ | ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                const M430Cpu::Paso p = cpu.paso();
                if (pasos_ && !p.dormido) --pasos_;
                if (p.dormido) {
                    if (pasos_) pasos_ = 0;
                    sincroniza();
                    if (tclk_) { credito_ = 0; wait(ev_tclk_ | ev_irq_ | ev_dbg_ | rst_n.value_changed_event()); }
                    else wait(quantum, ev_irq_ | ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                if (traza_limite && cpu.instrucciones() <= traza_limite)
                    std::printf("[%s] pc=0x%05x\n", name(), unsigned(cpu.pc()));
                if (tclk_) credito_ -= int64_t(p.ciclos);
                else consume(ciclos_tiempo(p.ciclos));
            }
        }
    }
};

} // namespace stm32
#endif // STM32_M430_CPU_NUCLEO_M430_H
