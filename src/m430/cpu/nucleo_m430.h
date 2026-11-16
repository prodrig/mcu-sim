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
#include "../debug/tap_m430.h"
#include "../debug/eem.h"

namespace stm32 {

struct M430Ext : tlm::tlm_extension<M430Ext> {
    M430Acceso acceso = M430Acceso::Lectura;
    bool depurador = false;
    tlm::tlm_extension_base* clone() const override { return new M430Ext(*this); }
    void copy_from(const tlm::tlm_extension_base& e) override { *this = static_cast<const M430Ext&>(e); }
};

SC_MODULE(NucleoM430), public m430_mem_if, public m430_jtag_cpu_if {
    tlm_utils::simple_initiator_socket<NucleoM430> bus{"bus"};
    sc_core::sc_in<double> clk_hz{"clk_hz"};       // MCLK
    sc_core::sc_in<bool>   rst_n{"rst_n"};

    M430Cpu cpu;
    sc_core::sc_time quantum{1, sc_core::SC_US};
    // La depuración por los pines (M2): el TAP (para el MDB de JTAG) y el EEM
    TapM430* tap = nullptr;
    EemM430* eem = nullptr;
    std::function<void(bool)> al_por;          // el POR de JTAG (lo hace el SoC)
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
    // La depuración (M2). Seguir tras una parada salta el disparador del EEM
    // de la instrucción en la que se paró (como la herramienta)
    void pide_parada(bool v) {
        if (!v && (parada_ || (eem && eem->parado()))) { salta_bp_ = true; if (eem) eem->suelta(); }
        parada_ = v; ev_dbg_.notify(sc_core::SC_ZERO_TIME);
    }
    bool parado() const { return parado_; }
    // La CPU, en manos de la depuración: parada (EEM o directa) o bajo JTAG
    // (solo avanza con TCLK). Es lo que mira el control de relojes del EEM
    // (GENCLKCTRL: qué relojes se paran con ella) [UG2 §28.2.4]
    bool parada_dbg() const { return parado_ || tclk_; }
    std::function<void(bool)> al_parada_dbg;
    bool parada_pedida() const { return parada_; }
    // La CPU espera a un periférico (la Flash borrando o escribiendo, con el
    // programa en ella): no ejecuta, pero NO está parada para la depuración
    // (ni la suelta al acabar, si un depurador la había parado)
    void espera(bool v) { espera_ = v; ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    // Una instrucción estando parado (el «single step» de la depuración)
    void pide_paso() { pasos_++; salta_bp_ = true; if (eem) eem->suelta(); ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    // El avance por TCLK
    void modo_tclk(bool v) { tclk_m1_ = v; credito_ = 0; ev_dbg_.notify(sc_core::SC_ZERO_TIME); }
    bool en_modo_tclk() const { return tclk_m1_; }
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
    // [UG2 §2.1.2] (2xx): el vector de reset a 0xFFFF apaga el chip
    bool vector_vacio_apaga = true;

    // --- m430_jtag_cpu_if: lo que pide el TAP ------------------------------
    void jtag_control(bool tce1) override { tce1_ = tce1; if (!tce1) { tclk_ = false; halt_jtag_ = false; mira_dbg(); }
                                            ev_dbg_.notify(sc_core::SC_ZERO_TIME); ev_tclk_.notify(sc_core::SC_ZERO_TIME); }
    void jtag_halt(bool v) override { halt_jtag_ = v; ev_tclk_.notify(sc_core::SC_ZERO_TIME); }
    void jtag_tclk(bool nivel) override {
        // Bajo JTAG, MCLK es TCLK: cada bajada es un ciclo de MCLK para quien
        // lo cuente (el generador de tiempos de la Flash con FSSEL = MCLK)
        if (!nivel && tclk_) { ++mclk_jtag_; ev_mclk_jtag_.notify(sc_core::SC_ZERO_TIME); }
        if (!nivel && tclk_ && !halt_jtag_) { ++bajadas_; ev_tclk_.notify(sc_core::SC_ZERO_TIME); }
    }
    // Los ciclos de MCLK que ha dado TCLK (y su evento)
    uint64_t ciclos_mclk_jtag() const { return mclk_jtag_; }
    const sc_core::sc_event& ev_mclk_jtag() const { return ev_mclk_jtag_; }
    bool jtag_sincronizado() const override { return tclk_; }
    bool jtag_instr_load() const override { return tclk_ && espera_instr_; }
    void jtag_por(bool v) override { if (al_por) al_por(v); }
    uint32_t jtag_mab() const override { return tclk_ && espera_instr_ ? cpu.pc() : mab_; }
    uint32_t jtag_ultima_busqueda() const override { return busqueda_; }
    // Un acceso de JTAG es un acceso de bus como los de la CPU: la Flash pasa
    // por su controlador (por eso se programa con FCTL y TCLK [JTAG §2.4.7])
    uint16_t jtag_lee(uint32_t a, bool byte) override {
        uint32_t v = 0;
        acceso_bus(false, a & (byte ? ~0u : ~1u), byte ? 1 : 2, v, M430Acceso::Lectura, true, false);
        return uint16_t(v);
    }
    void jtag_escribe(uint32_t a, uint16_t v, bool byte) override {
        uint32_t x = byte ? (v & 0xFFu) : v;
        acceso_bus(true, a & (byte ? ~0u : ~1u), byte ? 1 : 2, x, M430Acceso::Escritura, true, false);
    }
    // Para la depuración directa (--gdb-dap) y los bancos: la memoria sin
    // efectos ni tiempo; la Flash se escribe sin el controlador
    uint16_t lee_dbg(uint32_t a, bool byte) {
        uint32_t v = 0;
        acceso_bus(false, a & (byte ? ~0u : ~1u), byte ? 1 : 2, v, M430Acceso::Lectura, true, true);
        return uint16_t(v);
    }
    void escribe_dbg(uint32_t a, uint16_t v, bool byte) {
        uint32_t x = byte ? (v & 0xFFu) : v;
        acceso_bus(true, a & (byte ? ~0u : ~1u), byte ? 1 : 2, x, M430Acceso::Escritura, true, true);
    }
    // Una escritura de la depuración directa que SÍ pasa por el controlador
    // (el programador de la Flash por --gdb-dap)
    void escribe_bus(uint32_t a, uint16_t v, bool byte) { jtag_escribe(a, v, byte); }
    bool en_tclk() const { return tclk_; }

private:
    bool tce1_ = false, tclk_ = false, halt_jtag_ = false, espera_instr_ = false, salta_bp_ = false,
         para_tras_ = false;
    uint64_t bajadas_ = 0, vistas_ = 0, mclk_jtag_ = 0;
    sc_core::sc_event ev_mclk_jtag_;
    unsigned accesos_paso_ = 0, deuda_ = 0;
    uint32_t mab_ = 0, busqueda_ = 0;
    // Una bajada de TCLK para la CPU (antes, las que deba la instrucción anterior)
    void espera_bajada(bool instr) {
        for (;;) {
            while (tclk_ && (halt_jtag_ || bajadas_ == vistas_)) {
                espera_instr_ = instr && deuda_ == 0;
                wait(ev_tclk_ | rst_n.value_changed_event());
                if (!rst_n.read()) { espera_instr_ = false; return; }
            }
            espera_instr_ = false;
            if (!tclk_) return;
            ++vistas_;
            if (deuda_) { --deuda_; continue; }
            return;
        }
    }
    sc_core::sc_time local_{sc_core::SC_ZERO_TIME};
    tlm::tlm_generic_payload gp_;
    sc_core::sc_event ev_irq_, ev_dbg_, ev_parado_, ev_tclk_;
    bool dbg_ant_ = false;
    void mira_dbg() {
        const bool p = parado_ || tclk_;
        if (p != dbg_ant_) { dbg_ant_ = p; if (al_parada_dbg) al_parada_dbg(p); }
    }
    bool espera_ = false;
    bool en_reset_ = true, rst_bajo_ = false, parada_ = false, parado_ = false, tclk_m1_ = false;
    unsigned pasos_ = 0;
    int64_t credito_ = 0;
    void baja_rst() { rst_bajo_ = true; }

    bool acceso(bool escr, uint32_t a, unsigned n, uint32_t& v, M430Acceso t) {
        mab_ = a;
        if (tclk_) {
            // Bajo JTAG: cada acceso, en una bajada de TCLK; lo que la CPU lee
            // lo da el MDB de JTAG con IR_DATA_16BIT [JTAG §2.2.4.2.2]
            ++accesos_paso_;
            espera_bajada(t == M430Acceso::Instruccion);
            if (t == M430Acceso::Instruccion) busqueda_ = a;   // la búsqueda, hecha en su bajada
            if (!escr && tap && tap->da_mdb()) {
                v = n == 1 ? tap->mdb_jtag() & 0xFFu : tap->mdb_jtag();
                return true;
            }
        }
        else if (t == M430Acceso::Instruccion) busqueda_ = a;
        if (eem && eem->activo() && !tclk_ && t != M430Acceso::Instruccion && t != M430Acceso::Busqueda) {
            // los datos (los disparadores de búsqueda se miran antes de ejecutar)
            if (escr && eem->acceso(M430Bus::Escritura, a, v)) para_tras_ = true;
        }
        const bool r = acceso_bus(escr, a, n, v, t, false, false);
        if (tap) tap->pon_mdb(uint16_t(v));
        if (eem && eem->activo() && !tclk_ && !escr && t != M430Acceso::Instruccion && t != M430Acceso::Busqueda
            && eem->acceso(M430Bus::Lectura, a, v)) para_tras_ = true;
        return r;
    }
    // `sin_tiempo`: no suma latencia (JTAG y depuración); `dbg`: lo marca como
    // del depurador (la Flash, sin el controlador)
    bool acceso_bus(bool escr, uint32_t a, unsigned n, uint32_t& v, M430Acceso t, bool sin_tiempo, bool dbg) {
        unsigned char buf[2];
        if (escr) for (unsigned i = 0; i < n; ++i) buf[i] = uint8_t(v >> (8 * i));
        tlm::tlm_generic_payload& gp = gp_;
        M430Ext ext;
        ext.acceso = t;
        ext.depurador = dbg;
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
        if (!sin_tiempo && !tclk_) local_ += d;
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
            tclk_ = false;
            while (!rst_n.read()) wait(rst_n.value_changed_event());
            rst_bajo_ = false;
            if (tce1_) { tclk_ = true; deuda_ = 0; vistas_ = bajadas_; }
            mira_dbg();
            // Con el vector borrado, el chip se apaga (LPM4) si arranca solo;
            // bajo JTAG, la CPU queda en la búsqueda de instrucción y manda
            // la herramienta [de modelo: JTAG §2.3.2.2, ExecutePOR sigue con
            // la CPU sincronizada sea cual sea el contenido de la Flash]
            cpu.vector_vacio_apaga = vector_vacio_apaga && !tce1_;
            cpu.reset();
            en_reset_ = false;
            for (;;) {
                if (!rst_n.read() || rst_bajo_) { sincroniza(); break; }
                // JTAG toma la CPU entre dos instrucciones; la suelta igual
                if (tce1_ && !tclk_) { sincroniza(); tclk_ = true; deuda_ = 0; vistas_ = bajadas_; mira_dbg(); }
                if (!tce1_ && tclk_) { tclk_ = false; mira_dbg(); }
                // Parada por la depuración directa o por el EEM: quieta, salvo un paso
                const bool parar = !tclk_ && (parada_ || (eem && eem->parado()));
                if (parar && !pasos_) {
                    sincroniza();
                    if (!parado_) { parado_ = true; ev_parado_.notify(sc_core::SC_ZERO_TIME); mira_dbg(); }
                    wait(ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                if (parado_) { parado_ = false; mira_dbg(); }
                if (espera_ && !tclk_) {
                    sincroniza();
                    wait(ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                // El avance por TCLK de M1 (sin TAP)
                if (tclk_m1_ && credito_ <= 0) {
                    sincroniza();
                    wait(ev_tclk_ | ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                // Los disparadores de búsqueda del EEM, antes de ejecutar
                if (!tclk_ && eem && eem->activo() && !salta_bp_ && !cpu.durmiendo()) {
                    const uint32_t pc = cpu.pc();
                    if (eem->acceso(M430Bus::Busqueda, pc, lee_dbg(pc, false))) continue;
                }
                salta_bp_ = false;
                accesos_paso_ = 0;
                const M430Cpu::Paso p = cpu.paso();
                if (pasos_ && !p.dormido) --pasos_;
                if (para_tras_) { para_tras_ = false; if (eem) { /* ya está parado por el EEM */ } }
                if (p.dormido) {
                    if (pasos_) pasos_ = 0;
                    sincroniza();
                    if (tclk_) { espera_bajada(false); continue; }
                    if (tclk_m1_) { credito_ = 0; wait(ev_tclk_ | ev_irq_ | ev_dbg_ | rst_n.value_changed_event()); }
                    else wait(quantum, ev_irq_ | ev_dbg_ | rst_n.value_changed_event());
                    continue;
                }
                if (traza_limite && cpu.instrucciones() <= traza_limite)
                    std::printf("[%s] pc=0x%05x\n", name(), unsigned(cpu.pc()));
                if (tclk_) { if (p.ciclos > accesos_paso_) deuda_ += p.ciclos - accesos_paso_; }
                else if (tclk_m1_) credito_ -= int64_t(p.ciclos);
                else consume(ciclos_tiempo(p.ciclos));
            }
        }
    }
};

} // namespace stm32
#endif // STM32_M430_CPU_NUCLEO_M430_H
