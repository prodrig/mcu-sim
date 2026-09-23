// =============================================================================
// cpu.h — Núcleo de ejecución ARMv7E-M (fetch / decode / execute)
//
// El banco de registros es una struct C++ (plan P3), no submódulos: R0-R12,
// SP (MSP/PSP), LR, PC, xPSR, PRIMASK, FAULTMASK, BASEPRI, CONTROL [IR, §7.2].
// La ISA implementada (codificación, pseudocódigo y flags) es la de
// doc/refs/stm32f407xx/informe_instrucciones.md [II]; el decodificador se valida con los
// vectores de doc/stm32f4xx/valida_instrucciones.py.
//
// Fase F2 — implementado:
//   * Thumb de 16 bits completo [II, §1];
//   * Thumb-2 de 32 bits: múltiples, exclusivos, dual, tablas de salto,
//     DP con registro desplazado e inmediato, inmediato plano, saltos,
//     MSR/MRS, hints y barreras, carga/almacenamiento simple, DP de registro,
//     DSP/SIMD, multiplicación y división [II, §2-§4];
//   * FPv4-SP: aritmética, transferencias, comparaciones, conversiones y
//     cargas/almacenamientos [II, §5];
//   * bloques IT, monitores exclusivos, accesos no alineados;
//   * excepciones completas: entrada con trama básica y extendida, lazy
//     stacking, EXC_RETURN, tail-chaining y escalado a HardFault [IR, §9];
//   * WFI/WFE/SEV con las salidas sleeping/sleepdeep hacia PWR/RCC.
//
// Precisión temporal: loosely-timed con anotación de ciclos [II, §7]. El tiempo
// se acumula localmente y se sincroniza con el planificador cuando supera el
// quantum, que es lo que permite arrancar firmware real a velocidad útil.
// =============================================================================
#ifndef STM32_CORE_CPU_H
#define STM32_CORE_CPU_H

#include <systemc>
#include <cstdio>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/ahb_types.h"
#include "cpu_state.h"
#include "debug_if.h"
#include "scs.h"
#include "fpu.h"

namespace stm32 {

SC_MODULE(Cpu) {
    // Buses hacia el router del núcleo (cortex_m4f.h)
    tlm_utils::simple_initiator_socket<Cpu> ibus{"ibus"};   // fetch
    tlm_utils::simple_initiator_socket<Cpu> dbus{"dbus"};   // datos/literales
    tlm_utils::simple_initiator_socket<Cpu> sbus{"sbus"};   // no usado (el router reparte)

    sc_core::sc_in<bool>   fclk{"fclk"};       // reloj libre del núcleo
    sc_core::sc_in<double> fclk_hz{"fclk_hz"};
    sc_core::sc_in<bool>   rst_n{"rst_n"};
    sc_core::sc_port<core_sys_if> sys{"sys"};  // SCB + NVIC + SysTick + MPU

    sc_core::sc_out<bool> sleeping{"sleeping"};
    sc_core::sc_out<bool> sleepdeep_out{"sleepdeep_out"};
    // Vigilancia de SCR.SLEEPDEEP para quien quiera trazarla desde fuera. El
    // núcleo NO la usa para decidir: para eso lee el SCB directamente (véase
    // core_sys_if::sleepdeep), porque una señal llegaría un delta tarde.
    sc_core::sc_in<bool>  sleepdeep_cfg{"sleepdeep_cfg"};
    sc_core::sc_in<bool>  event_in{"event_in"};
    sc_core::sc_out<bool> event_out{"event_out"};
    sc_core::sc_in<bool>  dbg_halt_req{"dbg_halt_req"};
    sc_core::sc_out<bool> dbg_halted{"dbg_halted"};

    RegFile  reg;          // estado arquitectónico (accesible por el debug DCRSR)
    // Subsistema de depuración. A nulo, el núcleo se comporta como si no
    // hubiera depurador conectado: es lo que pasa en un chip sin sonda.
    core_debug_if* dbg = nullptr;
    // Las dos listas de espera del bucle de ejecución, armadas al arrancar
    // exec_proc() y no en cada vuelta (véase el comentario de allí).
    sc_core::sc_event_or_list ev_sueno_, ev_parada_;
    FpuCore  fpu;          // unidad funcional FPv4-SP
    Fpu*     fpu_mod = nullptr;   // para la línea de IRQ 81 (lo fija CortexM4F)

    // Quantum de sincronización con el planificador (loosely-timed)
    sc_core::sc_time quantum{1, sc_core::SC_US};

    // Estadísticas y control para el banco de pruebas
    uint64_t inst_count = 0, exc_count = 0;
    bool     halted_on_lockup = false;
    // Traza de ejecución (depuración del modelo): imprime las primeras
    // trace_limit instrucciones con su dirección, codificación y registros.
    uint64_t trace_limit = 0, trace_from = 0;
    // Traza de excepciones: imprime las primeras fault_trace excepciones con su
    // causa. Es la herramienta de diagnóstico cuando un firmware se descarrila.
    unsigned fault_trace = 0;

    SC_CTOR(Cpu) {
        SC_THREAD(exec_proc);
        SC_METHOD(pub_proc); sensitive << pub_ev_; dont_initialize();
        // El registro de evento se ARMA con el flanco de la entrada de evento
        // (el pulso del EXTI, o un SEV externo). Sin este enganche un pulso de
        // un ciclo se perdería: el bucle de sueño solo mira el NIVEL cada
        // microsegundo, y un WFE posterior debe volver de inmediato aunque el
        // evento ocurriera antes de ejecutarlo [ARMv7-M B1.5.18].
        SC_METHOD(event_latch_proc); sensitive << event_in.pos(); dont_initialize();
    }
    void event_latch_proc() { event_reg_ = true; }

    // -----------------------------------------------------------------------
    // Modo de sonda del decodificador (verificación)
    //
    // Ejecuta una única instrucción situada en 'addr' sin tomar la excepción
    // que pudiera generar: devuelve el tamaño consumido (2 o 4 bytes) y los
    // bits de CFSR que la instrucción habría provocado. Es lo que permite
    // recorrer los vectores de doc/stm32f4xx/valida_instrucciones.py y comprobar que el
    // decodificador reconoce todas las codificaciones.
    // Debe invocarse desde un proceso y con la CPU detenida (dbg_halt_req).
    // -----------------------------------------------------------------------
    struct Probe { unsigned size = 0; uint32_t cfsr = 0; bool ok = false; };
    Probe probe(uint32_t addr) {
        Probe p;
        const bool save_trap = trap_faults_;
        trap_faults_ = true; trapped_cfsr_ = 0;
        flush_prefetch();          // la sonda reescribe la misma dirección
        cur_pc_ = addr;
        reg.set_thumb(true);
        step();
        p.size = last_isize_;
        p.cfsr = trapped_cfsr_;
        p.ok   = (trapped_cfsr_ & UF_UNDEFINSTR) == 0;
        trap_faults_ = save_trap;
        return p;
    }
    // Prepara un estado conocido para la sonda (bases válidas en SRAM)
    void probe_setup(uint32_t scratch) {
        for (unsigned i = 0; i < 13; ++i) reg.r[i] = scratch;
        reg.msp = reg.psp = scratch + 0x400u;
        reg.r[13] = reg.msp;
        reg.r[14] = 0xFFFFFFFFu;
        reg.xpsr = 0x01000000u;
        reg.handler_mode = false;
        reg.control = 0; reg.primask = 0; reg.basepri = 0; reg.faultmask = 0;
    }

    // Estado observable por el banco de pruebas y el subsistema de depuración
    uint32_t pc() const { return cur_pc_; }
    // El PC que ve y escribe el depurador por DCRSR/DCRDR: es la DIRECCION DE
    // RETORNO DE DEPURACION, o sea la instruccion que se ejecutaria al
    // reanudar [IR, §13.4.2].
    uint32_t dbg_pc() const { return cur_pc_; }
    void dbg_set_pc(uint32_t v) { cur_pc_ = v & ~1u; flush_prefetch(); }
    // Ejecuta UNA instruccion. Solo tiene sentido con el nucleo detenido.
    void dbg_step_one() { check_exceptions(); if (running_) step(); }
    // Ventana al estado de sueño, para el banco de pruebas de bajo consumo:
    // no basta con ver `sleeping` desde fuera, hay que saber CÓMO se durmió.
    bool durmiendo() const { return sleeping_state_; }
    bool durmio_con_wfe() const { return sleep_wfe_; }
    bool evento_armado() const { return event_reg_; }

    // Ciclos de reloj -> tiempo
    sc_core::sc_time cycles_time(unsigned n) const {
        const double f = fclk_hz.read();
        return f > 0.0 ? sc_core::sc_time(double(n) * 1.0e12 / f, sc_core::SC_PS)
                       : sc_core::sc_time(double(n) * 6.0, sc_core::SC_NS);
    }

private:
    // ---------------------------------------------------------------------
    // Estado de ejecución
    // ---------------------------------------------------------------------
    uint32_t cur_pc_ = 0;        // dirección de la instrucción en curso
    uint32_t next_pc_ = 0;       // dirección de la siguiente
    bool     branched_ = false;  // la instrucción escribió el PC
    bool     aborted_ = false;   // fault detectado durante la instrucción
    int      fault_exc_ = 0;     // excepción a tomar
    unsigned cycles_ = 1;        // ciclos de la instrucción en curso
    unsigned lsu_extra_ = 0;     // ciclos de mas por accesos a memoria (DWT_LSUCNT)
    bool     running_ = false;
    bool     sleeping_state_ = false;
    bool     event_reg_ = false; // registro de evento para WFE/SEV
    // CÓMO se durmió: con WFE o con WFI. No es un detalle, es lo que decide
    // quién puede despertarlo [IR, §14.3.2; ARMv7-M B1.5.18]:
    //   WFI -> solo una excepción (o el depurador, o el reset). El registro de
    //          evento NO lo despierta, aunque esté armado.
    //   WFE -> además, cualquier evento; y al despertar lo CONSUME.
    // Confundirlos tiene una consecuencia muy concreta: como el registro de
    // evento no se limpia solo, un núcleo dormido con WFI que despertara con él
    // no volvería a dormirse nunca.
    bool     sleep_wfe_ = false;
    // Monitor exclusivo local (LDREX/STREX) [II, §2.2]
    bool     excl_valid_ = false;
    uint32_t excl_addr_ = 0;
    unsigned excl_size_ = 0;
    sc_core::sc_time local_time_{sc_core::SC_ZERO_TIME};
    bool     trap_faults_ = false;   // modo sonda del decodificador
    uint32_t trapped_cfsr_ = 0;
    unsigned last_isize_ = 0;        // longitud de la última instrucción (2 o 4)
    bool     it_just_set_ = false;   // la instrucción ejecutada fue un IT
    // Dirección de retorno apilada: las excepciones síncronas generadas *por*
    // una instrucción que se considera completada (SVC) retornan a la
    // siguiente; los faults precisos retornan a la instrucción que falló
    // [IR, §9.3.1].
    bool     ret_next_instr_ = false;
    // Salidas publicadas por un único proceso
    bool o_sleeping_ = false, o_sleepdeep_ = false, o_event_ = false, o_halted_ = false;
    sc_core::sc_event pub_ev_;

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        sleeping.write(o_sleeping_);
        sleepdeep_out.write(o_sleepdeep_);
        event_out.write(o_event_);
        dbg_halted.write(o_halted_);
    }

    // Sincronización con el planificador (quantum del modelo LT)
    void consume(const sc_core::sc_time& t) {
        local_time_ += t;
        if (local_time_ >= quantum) { wait(local_time_); local_time_ = sc_core::SC_ZERO_TIME; }
    }
    void sync() {
        if (local_time_ > sc_core::SC_ZERO_TIME) { wait(local_time_); local_time_ = sc_core::SC_ZERO_TIME; }
    }

    // =====================================================================
    // Accesos a memoria
    // =====================================================================
    bool bus_access(tlm_utils::simple_initiator_socket<Cpu>& sk, bool write,
                    uint32_t addr, unsigned size, uint32_t& data,
                    BusMaster master, bool instr, bool exclusive) {
        unsigned char buf[8];
        if (write) for (unsigned i = 0; i < size; ++i) buf[i] = uint8_t(data >> (8 * i));
        tlm::tlm_generic_payload gp;
        AhbExt ext;
        ext.master     = master;
        ext.instr      = instr;
        ext.privileged = reg.privileged();
        ext.exclusive  = exclusive;
        gp_setup(gp, write, addr, buf, size);
        gp.set_extension(&ext);
        sc_core::sc_time t = sc_core::SC_ZERO_TIME;
        sk->b_transport(gp, t);
        gp.clear_extension(&ext);
        local_time_ += t;
        if (gp.get_response_status() != tlm::TLM_OK_RESPONSE) return false;
        if (!write) {
            data = 0;
            for (unsigned i = 0; i < size; ++i) data |= uint32_t(buf[i]) << (8 * i);
        }
        return true;
    }

    // Comprobación de la MPU antes de emitir [IR, §10.4]
    bool mpu_ok(uint32_t addr, bool write, bool instr) {
        if (sys->mpu_check(addr, write, instr, reg.privileged())) return true;
        take_fault(EXC_MEMMANAGE, instr ? MM_IACCVIOL : MM_DACCVIOL, !instr, addr);
        return false;
    }

    // MemU: admite no alineado; MemA: exige alineación [II, §4.1]
    bool mem_read(uint32_t addr, unsigned size, uint32_t& out,
                  bool aligned_req = false, bool exclusive = false) {
        if ((addr & (size - 1)) != 0) {
            if (aligned_req || sys->unalign_trp()) {
                take_fault(EXC_USAGEFAULT, UF_UNALIGNED, false, 0);
                return false;
            }
        }
        if (!mpu_ok(addr, false, false)) return false;
        if (!bus_access(dbus, false, addr, size, out, BusMaster::CORE_DBUS, false, exclusive)) {
            take_fault(EXC_BUSFAULT, BF_PRECISERR, true, addr);
            return false;
        }
        if (dbg) dbg->dbg_data(addr, size, false, out);
        return true;
    }
    bool mem_write(uint32_t addr, unsigned size, uint32_t val,
                   bool aligned_req = false, bool exclusive = false) {
        if ((addr & (size - 1)) != 0) {
            if (aligned_req || sys->unalign_trp()) {
                take_fault(EXC_USAGEFAULT, UF_UNALIGNED, false, 0);
                return false;
            }
        }
        if (!mpu_ok(addr, true, false)) return false;
        uint32_t d = val;
        if (!bus_access(dbus, true, addr, size, d, BusMaster::CORE_DBUS, false, exclusive)) {
            take_fault(EXC_BUSFAULT, BF_PRECISERR, true, addr);
            return false;
        }
        if (dbg) dbg->dbg_data(addr, size, true, val);
        return true;
    }
    // Buffer de prebúsqueda de una palabra: el núcleo real lee la Flash en
    // líneas de 128 bits a través del ART [IR, §5.2.3], de modo que una
    // secuencia lineal no genera una transacción por media palabra. Aquí se
    // modela con una palabra alineada de 32 bits; se invalida en el reset y
    // con ISB.
    uint32_t pf_addr_ = 1;      // dirección de la palabra en el buffer (impar = vacío)
    uint32_t pf_data_ = 0;
    void flush_prefetch() { pf_addr_ = 1; }

    bool fetch_hw(uint32_t addr, uint32_t& hw) {
        if (!sys->mpu_check(addr, false, true, reg.privileged())) {
            take_fault(EXC_MEMMANAGE, MM_IACCVIOL, false, 0);
            return false;
        }
        // EL FPB VA ANTES QUE LA MEMORIA. Es un filtro sobre los buses ICode y
        // DCode: si un comparador casa, la transaccion original hacia la Flash
        // NO llega a ocurrir [IR, §13-Implicaciones, §13.7].
        uint32_t sub = 0, alt = 0;
        if (dbg) {
            const int act = dbg->dbg_fetch(addr, sub, alt);
            if (act == FETCH_SUBST) { hw = sub; return true; }
            if (act == FETCH_REMAP) {
                uint32_t d = 0;
                if (!bus_access(dbus, false, alt & ~3u, 4, d, BusMaster::CORE_DBUS,
                                false, false)) {
                    take_fault(EXC_BUSFAULT, BF_IBUSERR, false, 0);
                    return false;
                }
                hw = (d >> (8u * (alt & 2u))) & 0xFFFFu;
                return true;
            }
        }
        const uint32_t wa = addr & ~3u;
        if (pf_addr_ != wa) {
            uint32_t d = 0;
            if (!bus_access(ibus, false, wa, 4, d, BusMaster::CORE_IBUS, true, false)) {
                take_fault(EXC_BUSFAULT, BF_IBUSERR, false, 0);
                return false;
            }
            pf_addr_ = wa;
            pf_data_ = d;
        }
        hw = (pf_data_ >> (8u * (addr & 2u))) & 0xFFFFu;
        return true;
    }

    // =====================================================================
    // Faults [IR, §9.5]
    // =====================================================================
    void take_fault(int excp, uint32_t cfsr_bits, bool addr_valid, uint32_t a) {
        if (aborted_) return;                   // ya hay uno en curso
        aborted_ = true;
        if (trap_faults_) {                     // modo sonda: solo se registra
            trapped_cfsr_ |= cfsr_bits;
            fault_exc_ = excp;
            return;
        }
        fault_exc_ = sys->raise_fault(excp, cfsr_bits, addr_valid, a, reg);
    }
    void undefined() { take_fault(EXC_USAGEFAULT, UF_UNDEFINSTR, false, 0); }

    // =====================================================================
    // Acceso a registros con semántica de PC
    // =====================================================================
    uint32_t rd_(unsigned n) const { return reg.r[n & 15u]; }
    void set_reg(unsigned n, uint32_t v) {
        n &= 15u;
        if (n == 15) { alu_write_pc(v); return; }
        if (n == 13) { reg.r[13] = v & ~3u; return; }   // SP siempre alineado a 4
        reg.r[n] = v;
    }
    void branch_to(uint32_t a) { next_pc_ = a & ~1u; branched_ = true; }
    void alu_write_pc(uint32_t v) { branch_to(v & ~1u); }
    // BXWritePC [II, §1.4]: el bit 0 selecciona el estado Thumb
    void bx_write_pc(uint32_t v) {
        if (reg.handler_mode && (v >> 28) == 0xFu) { exception_return(v); return; }
        if (!(v & 1u)) { take_fault(EXC_USAGEFAULT, UF_INVSTATE, false, 0); return; }
        reg.set_thumb(true);
        branch_to(v);
    }
    // LoadWritePC [II, §2.1]: igual que BX (admite EXC_RETURN)
    void load_write_pc(uint32_t v) { bx_write_pc(v); }

    // =====================================================================
    // Proceso principal
    // =====================================================================
    void exec_proc();
    void do_reset();
    void step();
    void check_exceptions();
    void exception_entry(int excp);
    void exception_return(uint32_t exc_return);
    void push_stack(int excp);
    bool lazy_fp_check();

    // --- Decodificación ----------------------------------------------------
    void exec_16(uint32_t hw);
    void exec_32(uint32_t hw1, uint32_t hw2);
    void exec_32_ldstm(uint32_t hw1, uint32_t hw2);      // §2.1, §2.2
    void exec_32_dp_reg_shift(uint32_t hw1, uint32_t hw2); // §2.3
    void exec_32_dp_imm(uint32_t hw1, uint32_t hw2);     // §3.1, §3.2
    void exec_32_branch_misc(uint32_t hw1, uint32_t hw2); // §3.3
    void exec_32_ldst(uint32_t hw1, uint32_t hw2);       // §4.1
    void exec_32_dp_reg(uint32_t hw1, uint32_t hw2);     // §4.2
    void exec_32_mul(uint32_t hw1, uint32_t hw2);        // §4.3, §4.4
    bool exec_32_fp(uint32_t hw1, uint32_t hw2);         // §5
    bool exec_32_fp_ldst(uint32_t hw1, uint32_t hw2);    // §5.5

    // Ayudas de la ALU
    void alu_flags_nzcv(uint32_t r, bool c, bool v, bool setflags) {
        if (!setflags) return;
        reg.set_nz(r); reg.set_c(c); reg.set_v(v);
    }
    void alu_flags_nzc(uint32_t r, bool c, bool setflags) {
        if (!setflags) return;
        reg.set_nz(r); reg.set_c(c);
    }
    // Operación común de las tablas §2.3 y §3.1
    void dp_op(unsigned op, unsigned rd_, unsigned rn, uint32_t operand,
               bool carry_from_shift, bool setflags);
};

// ===========================================================================
// Reset y bucle principal
// ===========================================================================
inline void Cpu::do_reset() {
    reg = RegFile();
    reg.handler_mode = false;
    reg.set_thumb(true);
    excl_valid_ = false;
    flush_prefetch();
    sleeping_state_ = false;
    event_reg_ = false;
    inst_count = 0; exc_count = 0;
    halted_on_lockup = false;
    local_time_ = sc_core::SC_ZERO_TIME;
    o_sleeping_ = o_sleepdeep_ = o_event_ = o_halted_ = false;
    lsu_extra_ = 0;
    publish();
    // El subsistema de depuracion se entera del reset: es lo que arma
    // DHCSR.S_RESET_ST y, si esta pedida, la captura de vector de reset.
    if (dbg) dbg->dbg_reset();

    // MSP inicial de [VTOR+0] y PC de [VTOR+4] [IR, §7.2.2]
    const uint32_t base = sys->vtor();
    uint32_t msp = 0, rst = 0;
    aborted_ = false;
    if (!mem_read(base + 0, 4, msp, true) || !mem_read(base + 4, 4, rst, true)) {
        aborted_ = false;
        sys->set_hfsr(1u << 1);            // VECTTBL
        halted_on_lockup = true;
        return;
    }
    reg.msp = msp & ~3u;
    reg.r[13] = reg.msp;
    reg.r[14] = 0xFFFFFFFFu;
    reg.set_thumb((rst & 1u) != 0);
    cur_pc_ = rst & ~1u;
    reg.set_ipsr(0);
    running_ = true;
}

inline void Cpu::exec_proc() {
    // Las dos listas de espera del núcleo —la del sueño (WFI/WFE) y la de la
    // parada del depurador— se arman UNA VEZ, aquí. Ninguno de sus eventos
    // cambia después de la elaboración, incluido `dbg`, que se engancha al
    // construir el MCU. Rearmarlas en cada vuelta reservaba memoria en dos
    // caminos que se recorren mucho —cada WFI del firmware— y, cuando la
    // simulación termina con el núcleo parado en un `wfi`, la última lista se
    // perdía: un SC_THREAD suspendido en `wait()` no desenrolla su pila.
    ev_sueno_ |= sys->pending_ev()
               | event_in.value_changed_event()
               | rst_n.value_changed_event()
               | fclk_hz.value_changed_event()
               | dbg_halt_req.value_changed_event();
    if (dbg) ev_sueno_ |= dbg->dbg_wake();
    ev_parada_ |= dbg_halt_req.value_changed_event() | rst_n.value_changed_event();
    if (dbg) ev_parada_ |= dbg->dbg_wake();
    for (;;) {
        // --- fuera de reset -------------------------------------------------
        // Un núcleo en reset NO está dormido. Hay que decirlo en voz alta,
        // porque `sleeping` y `sleepdeep` los mira el PWR para decidir el modo
        // de energía: si se quedaran colgados a uno durante todo el reset -que
        // en Standby dura cientos de microsegundos-, al soltar el reset el PWR
        // creería que el núcleo acaba de ejecutar otro WFI y volvería a
        // dormir el MCU antes incluso de que ejecutara su primera instrucción.
        while (!rst_n.read()) {
            running_ = false;
            sleeping_state_ = false;
            if (o_sleeping_ || o_sleepdeep_ || o_halted_) {
                o_sleeping_ = o_sleepdeep_ = o_halted_ = false;
                publish();
            }
            wait(rst_n.value_changed_event());
        }
        do_reset();
        while (rst_n.read() && running_) {
            // Sin reloj no hay ejecución. Además de ser lo que hace el
            // silicio, evita que el intérprete gire sin que avance el tiempo
            // simulado: la anotación de ciclos se calcula con fclk_hz y sería
            // nula (situación real cuando el CSS o un fallo de alimentación
            // dejan el árbol de reloj sin fuente) [IR, §4.4].
            if (fclk_hz.read() <= 0.0) {
                sync();
                wait(fclk_hz.value_changed_event() | rst_n.value_changed_event());
                continue;
            }
            // PARADA DEL DEPURADOR. El hilo de ejecucion se suspende y el
            // nucleo entra en espera reactiva: solo el DAP puede tocar el
            // sistema mientras tanto [IR, §13-Implicaciones].
            if (dbg_halt_req.read() || (dbg && dbg->dbg_halt_now())) {
                // Parar SACA AL NUCLEO DEL SUEÑO. Un nucleo detenido no esta
                // dormido: si al reanudar volviera al bucle de WFI, el
                // depurador no podria arrancar nada tras un halt sobre un WFI,
                // que es justo la situacion mas comun al enganchar una sonda.
                if (sleeping_state_) {
                    sleeping_state_ = false;
                    o_sleeping_ = false; o_sleepdeep_ = false;
                }
                if (!o_halted_) { o_halted_ = true; publish(); }
                sync();
                // El PASO A PASO se ejecuta AQUI, con el nucleo formalmente
                // detenido: una instruccion y vuelta a la espera [IR, §13.4.1].
                if (dbg && dbg->dbg_take_step()) {
                    check_exceptions();
                    if (running_) step();
                    sync();
                    continue;
                }
                wait(ev_parada_);
                if (dbg_halt_req.read() || (dbg && dbg->dbg_halt_now())) continue;
                if (o_halted_) { o_halted_ = false; publish(); }
                continue;
            }
            if (sleeping_state_) {
                sync();
                if (!o_sleeping_) { o_sleeping_ = true; publish(); }
                // Despierta con cualquier excepción pendiente o evento.
                //
                // Si el árbol de reloj está PARADO -modo Stop o Standby, donde
                // el PWR ha mandado apagar el dominio de 1,2 V- no tiene
                // sentido sondear cada microsegundo: el núcleo no puede
                // reanudar nada hasta que vuelva el reloj, y esperarlo es
                // además lo que modela el tiempo de despertar [IR, §14.4.3].
                if (fclk_hz.read() <= 0.0) {
                    wait(fclk_hz.value_changed_event() | rst_n.value_changed_event());
                    continue;
                }
                // DORMIR ES ESPERAR, NO SONDEAR.
                //
                // Antes se miraba cada microsegundo si habia algo pendiente: un
                // MILLON de despertares por cada segundo simulado, casi todos
                // para comprobar que no habia nada y volver a dormirse. Ahora se
                // espera a los sucesos que de verdad pueden despertar al nucleo,
                // que son estos y solo estos [ARMv7-M B1.5.18]:
                //
                //   * una excepcion queda pendiente        -> sys->pending_ev()
                //   * llega un evento del EXTI (WFE)       -> event_in
                //   * el nucleo entra en reset             -> rst_n
                //   * se para el arbol de reloj (Stop)     -> fclk_hz
                //   * el depurador quiere parar            -> dbg_halt_req / dbg_wake
                //
                // La condicion se RE-COMPRUEBA justo antes de bloquear. Sin eso
                // habria una carrera: un suceso que ocurriera entre la
                // instruccion WFI/WFE y la espera se perderia, porque wait()
                // solo ve las notificaciones POSTERIORES a la propia espera.
                // El sondeo de antes tapaba esa carrera a base de fuerza bruta.
                {
                    const bool ya = sys->any_pending() ||
                                    (sleep_wfe_ && (event_reg_ || event_in.read())) ||
                                    !rst_n.read() || dbg_halt_req.read() ||
                                    (dbg && dbg->dbg_halt_now());
                    if (!ya) wait(ev_sueno_);
                }
                const bool por_evento = sleep_wfe_ &&
                                        (event_reg_ || event_in.read());
                if (sys->any_pending() || por_evento || !rst_n.read()) {
                    if (por_evento) event_reg_ = false;   // WFE lo consume
                    sleeping_state_ = false;
                    o_sleeping_ = false; o_sleepdeep_ = false; publish();
                }
                check_exceptions();
                continue;
            }
            check_exceptions();
            if (!running_) break;
            step();
        }
        if (halted_on_lockup) {
            sync();
            wait(rst_n.value_changed_event());
        }
    }
}

inline void Cpu::step() {
    aborted_ = false; branched_ = false; cycles_ = 1; it_just_set_ = false;
    ret_next_instr_ = false;
    const uint32_t pc = cur_pc_;
    next_pc_ = pc + 2u;                     // por defecto (se corrige tras el fetch)
    last_isize_ = 2u;
    if (!reg.thumb()) {                     // T = 0 -> INVSTATE [IR, §7.1.3]
        take_fault(EXC_USAGEFAULT, UF_INVSTATE, false, 0);
        if (!trap_faults_) exception_entry(fault_exc_);
        return;
    }
    uint32_t hw1 = 0;
    if (!fetch_hw(pc, hw1)) { if (!trap_faults_) exception_entry(fault_exc_); return; }

    // Una media palabra con [15:11] en {11101,11110,11111} inicia una
    // instrucción de 32 bits [II, §1].
    const unsigned top5 = (hw1 >> 11) & 0x1Fu;
    const bool is32 = (top5 == 0x1D || top5 == 0x1E || top5 == 0x1F);
    last_isize_ = is32 ? 4u : 2u;
    next_pc_ = pc + last_isize_;
    reg.r[15] = pc + 4u;                    // valor arquitectónico del PC

    // Condición del bloque IT [II, §1.7]
    bool exec = true;
    const uint8_t it = reg.itstate();
    if (it & 0xF) exec = condition_passed((it >> 4) & 0xFu, reg);

    if (is32) {
        uint32_t hw2 = 0;
        if (!fetch_hw(pc + 2, hw2)) { if (!trap_faults_) exception_entry(fault_exc_); return; }
        if (exec) exec_32(hw1, hw2);
    } else {
        if (exec) exec_16(hw1);
    }

    ++inst_count;
    if (inst_count >= trace_from && inst_count <= trace_from + trace_limit)
        std::printf("[cpu] %6llu pc=%08X %04X%s r0=%08X r1=%08X r2=%08X r3=%08X "
                    "sp=%08X lr=%08X psr=%08X%s\n",
                    (unsigned long long)inst_count, pc, hw1,
                    is32 ? "...." : "    ",
                    reg.r[0], reg.r[1], reg.r[2], reg.r[3], reg.r[13], reg.r[14],
                    reg.xpsr, aborted_ ? "  <FAULT>" : "");
    if (aborted_) { if (!trap_faults_) exception_entry(fault_exc_); return; }

    // Avance de ITSTATE [II, §1.7]. La propia instrucción IT establece el
    // estado y NO lo avanza: el avance corresponde a las instrucciones del
    // bloque. Un salto tomado termina el bloque.
    if (it_just_set_)   { /* ITSTATE recién cargado: no avanzar */ }
    else if (branched_) reg.set_itstate(0);
    else                reg.it_advance();

    cur_pc_ = next_pc_;
    // Los contadores de perfil del DWT se alimentan de la propia contabilidad
    // de ciclos del modelo, no de una estimacion aparte [IR, §13.5.2].
    if (dbg) dbg->dbg_cycles(cycles_, lsu_extra_);
    lsu_extra_ = 0;
    consume(cycles_time(cycles_));
}

// ===========================================================================
// Excepciones [IR, §9.3]
// ===========================================================================
inline void Cpu::check_exceptions() {
    const int cur = sys->execution_priority(reg);
    const int e = sys->pending_exception(cur, reg);
    if (e <= 0) return;
    // DHCSR.C_MASKINTS [ARMv7-M, C1.6.2]. Mientras el depurador lo tenga
    // puesto, las excepciones CONFIGURABLES no se toman: siguen pendientes y
    // esperan. Solo pasan NMI y HardFault, que no se pueden enmascarar.
    //
    // Esto no es un adorno: es lo que hace que dar un paso sobre una linea de C
    // no acabe dentro de SysTick_Handler. Una sonda pone C_MASKINTS junto con
    // C_STEP justamente por eso, porque si no cada paso se come la interrupcion
    // que estuviera pendiente y el paso a paso se vuelve inutilizable.
    if (dbg && dbg->dbg_mask_ints() && e != EXC_NMI && e != EXC_HARDFAULT) return;
    exception_entry(e);
}

inline void Cpu::push_stack(int excp) {
    (void)excp;
    // Trama básica de 8 palabras (+17 si hay contexto FP) [IR, §9.3.1, §8.13.1]
    const bool fp_ctx = reg.fpca() && sys->fpccr_aspen();
    const bool lazy   = fp_ctx && sys->fpccr_lspen();
    const unsigned frame_words = fp_ctx ? 26u : 8u;

    reg.store_sp();
    uint32_t sp = reg.using_psp() ? reg.psp : reg.msp;
    // Alineación a 8 bytes [IR, §10.2.5 CCR.STKALIGN]
    bool align = false;
    if (sys->stkalign() && (sp & 4u)) { align = true; }
    sp -= 4u * frame_words + (align ? 4u : 0u);
    sp &= ~3u;

    const uint32_t ret_addr = cur_pc_ + (ret_next_instr_ ? last_isize_ : 0u);
    uint32_t xpsr_save = reg.xpsr;
    if (align) xpsr_save |= (1u << 9);       // bit de alineación de la trama
    else       xpsr_save &= ~(1u << 9);
    // El campo IPSR de la trama guarda la excepción interrumpida
    const uint32_t vals[8] = { reg.r[0], reg.r[1], reg.r[2], reg.r[3],
                               reg.r[12], reg.r[14], ret_addr, xpsr_save };
    bool err = false;
    for (unsigned i = 0; i < 8; ++i)
        if (!mem_write(sp + 4 * i, 4, vals[i], true)) { err = true; break; }
    if (fp_ctx && !lazy && !err) {
        for (unsigned i = 0; i < 16; ++i)
            if (!mem_write(sp + 0x20 + 4 * i, 4, reg.s[i], true)) { err = true; break; }
        if (!err) mem_write(sp + 0x60, 4, reg.fpscr, true);
    }
    if (err) {
        // Fallo de bus durante el apilado [IR, §9.6.1 BFSR.STKERR]
        aborted_ = false;
        sys->raise_fault(EXC_BUSFAULT, BF_STKERR, false, 0, reg);
    }
    if (fp_ctx && lazy) {
        sys->set_lspact(true, sp + 0x20, !reg.handler_mode, reg.npriv());
    }
    // EXC_RETURN [IR, §7.5]
    uint32_t exc_ret;
    if (reg.handler_mode)          exc_ret = fp_ctx ? EXC_RET_HANDLER_MSP_FP : EXC_RET_HANDLER_MSP;
    else if (reg.spsel())          exc_ret = fp_ctx ? EXC_RET_THREAD_PSP_FP  : EXC_RET_THREAD_PSP;
    else                           exc_ret = fp_ctx ? EXC_RET_THREAD_MSP_FP  : EXC_RET_THREAD_MSP;
    (reg.using_psp() ? reg.psp : reg.msp) = sp;
    reg.r[14] = exc_ret;
}

inline void Cpu::exception_entry(int excp) {
    if (excp <= 0) return;
    // CAPTURA DE VECTORES: con el bit correspondiente de DEMCR, el nucleo se
    // para ANTES de entrar en el manejador, de modo que el depurador ve el
    // estado con el que ocurrio el fallo [IR, §13.4.3].
    if (dbg && dbg->dbg_enabled() && dbg->dbg_vector_catch(excp)) {
        dbg->dbg_request_halt(DFSR_VCATCH);
        return;
    }
    ++exc_count;
    if (dbg) dbg->dbg_exception(excp, true);
    aborted_ = false;
    if (fault_trace) {
        --fault_trace;
        std::printf("[exc] #%d en pc=%08X sp=%08X lr=%08X psr=%08X "
                    "(inst=%llu, ipsr previo=%u)\n",
                    excp, cur_pc_, reg.r[13], reg.r[14], reg.xpsr,
                    (unsigned long long)inst_count, reg.ipsr());
        std::fflush(stdout);
    }

    push_stack(excp);
    ret_next_instr_ = false;

    // Cambio a modo Handler y pila principal [IR, §7.1]
    reg.handler_mode = true;
    reg.control = uint8_t(reg.control & ~2u);       // SPSEL = 0 en Handler
    reg.r[13] = reg.msp;
    reg.set_ipsr(unsigned(excp));
    reg.set_itstate(0);
    reg.set_thumb(true);
    sys->ack_exception(excp);
    sys->set_vectactive(unsigned(excp));

    // Lectura del vector [IR, §9.3.1]
    uint32_t handler = 0;
    if (!mem_read(sys->vtor() + 4u * uint32_t(excp), 4, handler, true)) {
        aborted_ = false;
        sys->set_hfsr(1u << 1);                     // HFSR.VECTTBL
        halted_on_lockup = true;
        running_ = false;
        return;
    }
    reg.set_thumb((handler & 1u) != 0);
    cur_pc_ = handler & ~1u;
    // 12 ciclos de entrada (sin FPU) [II, §7]
    consume(cycles_time(12));
}

inline void Cpu::exception_return(uint32_t exc_ret) {
    const unsigned n = reg.ipsr();
    if (n == 0) { take_fault(EXC_USAGEFAULT, UF_INVPC, false, 0); return; }

    const bool ret_handler = ((exc_ret & 0xFu) == 0x1u);
    const bool use_psp     = ((exc_ret & 0xFu) == 0xDu);
    const bool fp_frame    = ((exc_ret & 0x10u) == 0u);
    if ((exc_ret & 0xFu) != 0x1u && (exc_ret & 0xFu) != 0x9u && (exc_ret & 0xFu) != 0xDu) {
        take_fault(EXC_USAGEFAULT, UF_INVPC, false, 0);
        return;
    }

    sys->return_exception(int(n));

    uint32_t sp = use_psp ? reg.psp : reg.msp;
    uint32_t v[8];
    bool err = false;
    for (unsigned i = 0; i < 8; ++i)
        if (!mem_read(sp + 4 * i, 4, v[i], true)) { err = true; break; }
    if (err) {
        aborted_ = false;
        sys->raise_fault(EXC_BUSFAULT, BF_UNSTKERR, false, 0, reg);
        return;
    }
    unsigned frame_words = 8;
    if (fp_frame) {
        if (sys->fpccr_lspact()) {
            sys->set_lspact(false, 0, false, false);   // el volcado nunca ocurrió
        } else {
            for (unsigned i = 0; i < 16; ++i) mem_read(sp + 0x20 + 4 * i, 4, reg.s[i], true);
            uint32_t f = 0;
            if (mem_read(sp + 0x60, 4, f, true)) reg.fpscr = f;
        }
        frame_words = 26;
        aborted_ = false;
    }
    reg.r[0] = v[0]; reg.r[1] = v[1]; reg.r[2] = v[2]; reg.r[3] = v[3];
    reg.r[12] = v[4]; reg.r[14] = v[5];
    const uint32_t ret_addr = v[6];
    const uint32_t xpsr_ret = v[7];

    sp += 4u * frame_words;
    if (sys->stkalign() && ((xpsr_ret >> 9) & 1u)) sp += 4u;

    reg.handler_mode = ret_handler;
    if (!ret_handler) reg.control = uint8_t(use_psp ? (reg.control | 2u) : (reg.control & ~2u));
    reg.set_fpca(fp_frame);
    if (use_psp) reg.psp = sp; else reg.msp = sp;
    reg.r[13] = ret_handler ? reg.msp : (use_psp ? reg.psp : reg.msp);

    reg.xpsr = (xpsr_ret & 0xF900FDFFu) | (reg.xpsr & 0x00000000u);
    reg.set_thumb((xpsr_ret >> 24) & 1u);
    reg.set_ipsr(xpsr_ret & 0x1FFu);
    sys->set_vectactive(reg.ipsr());
    branch_to(ret_addr);

    // SLEEPONEXIT [IR, §10.2.4]
    if (!reg.handler_mode && sys->sleeponexit()) {
        sleeping_state_ = true;
        sleep_wfe_ = false;                  // SLEEPONEXIT duerme como un WFI
        o_sleepdeep_ = sys->scr_sleepdeep();
        publish();
    }
}

// Volcado diferido del contexto FP antes de la primera V* [IR, §8.13.2]
inline bool Cpu::lazy_fp_check() {
    if (!sys->fpccr_lspact()) return true;
    const uint32_t base = sys->fpcar();
    for (unsigned i = 0; i < 16; ++i)
        if (!mem_write(base + 4 * i, 4, reg.s[i], true)) {
            sys->raise_fault(EXC_BUSFAULT, BF_LSPERR, false, 0, reg);
            return false;
        }
    mem_write(base + 0x40, 4, reg.fpscr, true);
    sys->set_lspact(false, 0, false, false);
    return true;
}

} // namespace stm32

#include "cpu_exec16.h"
#include "cpu_exec32.h"

#endif // STM32_CORE_CPU_H
