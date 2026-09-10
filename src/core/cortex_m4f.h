// =============================================================================
// cortex_m4f.h — Macro del núcleo: CPU + FPU + SCS + Debug + router interno
//
// El router del núcleo (CoreRouter) implementa el mapa visto por la CPU:
//   I-bus  -> icode (matriz) para fetch < 0x2000_0000 (con aliasing de 0x0
//             según SYSCFG_MEMRMP/BOOT [IR, §5.1])
//   D-bus  -> CCM (0x1000_0000, directo, plan P3) | dcode (matriz) | PPB
//   S-bus  -> matriz (>= 0x2000_0000) ; PPB solo desde núcleo/AHB-AP
//   AHB-AP -> mismo mapa que la CPU (debug) [IR, §13.2]
// La MPU filtra cada transacción antes de emitirla [IR, §10.4].
// =============================================================================
#ifndef STM32_CORE_CORTEX_M4F_H
#define STM32_CORE_CORTEX_M4F_H

#include "cpu.h"
#include "fpu.h"
#include "scs.h"
#include "debug.h"
#include "gdb_stub_dap.h"
#include "../bus/bitband.h"

namespace stm32 {

// =============================================================================
// Rasgos de depuración del núcleo — cómo se llega al DAP
//
// Misma receta que en el resto del modelo (UsartCaps, TimCaps, SpiCaps...): un
// struct constexpr de rasgos, unas instancias con nombre y una clase base no
// plantilla que los toma POR VALOR, de modo que se puede elegir en tiempo de
// compilación (con el alias) o en tiempo de ejecución (con el constructor).
//
//   attach == DebugAttach::Pines   -> el núcleo EXPONE SWCLK/SWDIO/JTDI/SWO/
//        NJTRST y no crea ningún stub. Quien quiera depurar se conecta por
//        fuera (verif/gdb_stub.h), como un ST-LINK. Es el modo fiel.
//   attach == DebugAttach::Interno -> el núcleo RESERVA esos cinco pines (no
//        los puede usar nadie más, pero quedan mudos) y crea dentro de sí un
//        GdbStubDap que habla con el DAP por llamada de función. Es el modo
//        rápido.
//
// El criterio para elegir está en doc/stm32f407vg_fase6_gdb2.md.
// =============================================================================
struct DebugCaps {
    DebugAttach attach;      // pines expuestos o reservados
    unsigned    puerto;      // puerto TCP del stub interno (0 = no escuchar)
};
inline constexpr DebugCaps DBG_PINES  { DebugAttach::Pines,   0    };
inline constexpr DebugCaps DBG_INTERNO{ DebugAttach::Interno, 3333 };

SC_MODULE(CortexM4F) {
    // --- Puertos de bus hacia el sistema (los conecta el top) ---------------
    tlm_utils::simple_initiator_socket<CortexM4F> icode{"icode"};   // matriz M0
    tlm_utils::simple_initiator_socket<CortexM4F> dcode{"dcode"};   // matriz M1
    tlm_utils::simple_initiator_socket<CortexM4F> sbus{"sbus"};     // matriz M2
    tlm_utils::simple_initiator_socket<CortexM4F> ccm{"ccm"};       // CCM directa

    // --- Relojes / reset ----------------------------------------------------
    sc_core::sc_in<bool>   fclk{"fclk"};        // = HCLK (libre para el núcleo)
    sc_core::sc_in<double> fclk_hz{"fclk_hz"};
    sc_core::sc_in<bool>   systick_ext{"systick_ext"};  // HCLK/8
    sc_core::sc_in<bool>   rst_n{"rst_n"};

    // --- Interrupciones -----------------------------------------------------
    sc_core::sc_vector<sc_core::sc_in<bool>> irq_in;    // [82] -> NVIC
    sc_core::sc_in<bool> nmi_in{"nmi_in"};

    // --- Sistema ------------------------------------------------------------
    sc_core::sc_out<bool> sysresetreq{"sysresetreq"};   // SCB -> RCC
    sc_core::sc_out<bool> sleeping{"sleeping"};         // -> RCC/PWR
    sc_core::sc_out<bool> sleepdeep{"sleepdeep"};       // -> PWR (Stop/Standby)
    sc_core::sc_in<bool>  event_in{"event_in"};         // EXTI evento / SEV ext.
    sc_core::sc_out<bool> event_out{"event_out"};
    sc_core::sc_in<uint8_t> boot_mode{"boot_mode"};     // aliasing 0x0 [IR, §2.3]

    // --- Debug (pines via pin_mux AF0; freeze hacia periféricos) ------------
    DebugSys debug{"debug"};

    // --- Submódulos ---------------------------------------------------------
    Cpu cpu{"cpu"};
    Fpu fpu{"fpu"};
    Scs scs{"scs"};

    // El stub interno solo existe en modo Interno; en modo Pines es nullptr y
    // no cuesta ni un evento de simulación.
    GdbStubDap* gdb = nullptr;

    const DebugCaps caps;

    explicit CortexM4F(sc_core::sc_module_name nm, DebugCaps c = DBG_PINES)
        : sc_core::sc_module(nm), irq_in("irq_in", N_IRQ), caps(c) {
        // Sin SC_HAS_PROCESS: este módulo no declara ningún proceso propio -sus
        // hijos (cpu, fpu, scs) sí, cada uno el suyo-, y la macro solo define un
        // typedef que entonces no usa nadie. GCC y clang avisan de ello.
        // --- La elección de los dos stubs, en tres líneas ---------------------
        if (caps.attach == DebugAttach::Interno) {
            // Los pines siguen siendo del puerto de depuración -el top los
            // conecta igual, y el mux los sigue reservando en AF0- pero el
            // frente SWD deja de escucharlos y el SWO deja de emitir.
            debug.set_pines_debug(false);
            if (caps.puerto) gdb = new GdbStubDap("gdb", debug, caps.puerto);
        }

        // CPU <-> infra
        cpu.fclk(fclk); cpu.fclk_hz(fclk_hz); cpu.rst_n(rst_n);
        cpu.sys(scs.cpu_if);                 // SCB + NVIC + SysTick + MPU
        cpu.sleeping(sleeping);
        cpu.sleepdeep_out(sleepdeep);
        cpu.sleepdeep_cfg(sig_sleepdeep_);
        cpu.event_in(event_in);
        cpu.event_out(event_out);
        cpu.dbg_halt_req(sig_halt_req_);
        cpu.dbg_halted(sig_halted_);
        cpu.fpu_mod = &fpu;                  // línea de IRQ 81 de la FPU
        scs.sleepdeep(sig_sleepdeep_);
        scs.sysresetreq(sysresetreq);
        scs.rst_n(rst_n);
        debug.halt_req(sig_halt_req_);
        debug.halted(sig_halted_);
        debug.hclk_hz(fclk_hz);              // referencia del SWO y del CYCCNT
        debug.cpu_reg = &cpu.reg;            // acceso del DCRSR al banco
        debug.cpu     = &cpu;
        // Y el enganche que hace que esto sea depuracion de verdad: la CPU
        // llama al subsistema en cada busqueda, cada acceso y cada excepcion.
        cpu.dbg = &debug;

        // FPU: la unidad funcional vive dentro de la CPU (Cpu::fpu); este
        // módulo solo publica la línea de interrupción de excepciones FP.
        fpu.fclk(fclk); fpu.rst_n(rst_n); fpu.irq_fpu(sig_irq_fpu_);

        // NVIC: entradas de interrupción y NMI
        for (unsigned i = 0; i < N_IRQ; ++i) scs.irq_in[i](irq_in[i]);
        scs.nmi_in(nmi_in);

        // SysTick (su salida tick_irq se conecta dentro del propio SCS)
        scs.systick.proc_clk(fclk);
        scs.systick.ext_clk(systick_ext);
        scs.systick.clk_hz(fclk_hz);
        scs.systick.rst_n(rst_n);
        // El SysTick es parte del núcleo: se para con él. [ARMv7-M, B3.3.1]
        scs.systick.parado(sig_halted_);

        // Router: CPU I/D/S y AHB-AP -> destinos
        cpu.ibus.bind(rt_ibus_);  cpu.dbus.bind(rt_dbus_);  cpu.sbus.bind(rt_sbus_);
        debug.ahb_ap.bind(rt_dbg_);
        rt_ibus_.register_b_transport(this, &CortexM4F::bt_ibus);
        rt_dbus_.register_b_transport(this, &CortexM4F::bt_dbus);
        rt_sbus_.register_b_transport(this, &CortexM4F::bt_sbus);
        rt_dbg_.register_b_transport(this, &CortexM4F::bt_dbg);    // mismo mapa
        scs_isk_.bind(scs.ppb);
        dbg_isk_.bind(debug.ppb);
    }

    ~CortexM4F() { delete gdb; }

    sc_core::sc_signal<bool>& fpu_irq_sig() { return sig_irq_fpu_; }

    // ¿Están los pines de depuración expuestos o solo reservados?
    bool pines_debug_expuestos() const { return caps.attach == DebugAttach::Pines; }

private:
    // Sockets internos del router
    tlm_utils::simple_target_socket<CortexM4F>    rt_ibus_{"rt_ibus"};
    tlm_utils::simple_target_socket<CortexM4F>    rt_dbus_{"rt_dbus"};
    tlm_utils::simple_target_socket<CortexM4F>    rt_sbus_{"rt_sbus"};
    tlm_utils::simple_target_socket<CortexM4F>    rt_dbg_{"rt_dbg"};
    tlm_utils::simple_initiator_socket<CortexM4F> scs_isk_{"scs_isk"};
    tlm_utils::simple_initiator_socket<CortexM4F> dbg_isk_{"dbg_isk"};

    sc_core::sc_signal<bool> sig_sleepdeep_{"sig_sleepdeep"};
    sc_core::sc_signal<bool> sig_halt_req_{"sig_halt_req"};
    sc_core::sc_signal<bool> sig_halted_{"sig_halted"};
    sc_core::sc_signal<bool> sig_irq_fpu_{"sig_irq_fpu"};

    void inv_dmi(sc_dt::uint64, sc_dt::uint64) {}

    // Extensiones AHB propias de cada puerto del núcleo (no se reservan por
    // transacción: el router las presta al payload y las retira al salir).
    AhbExt ext_i_, ext_d_, ext_s_, ext_dbg_;

    static bool is_ppb(uint64_t a)  { return a >= 0xE0000000ull && a < 0xE0100000ull; }
    static bool is_scs(uint64_t a)  { return a >= 0xE000E000ull && a < 0xE000F000ull &&
                                             !(a >= 0xE000EDF0ull && a < 0xE000EEFFull); }
    static bool is_ccm(uint64_t a)  { return a >= addr::CCM_BASE &&
                                             a < addr::CCM_BASE + addr::CCM_SIZE; }

    // Presta una extensión al payload si el iniciador no trajo la suya.
    // Devuelve el puntero prestado (nullptr si el payload ya tenía una).
    static AhbExt* lend(tlm::tlm_generic_payload& gp, AhbExt& e,
                        BusMaster m, bool instr) {
        AhbExt* own = gp.get_extension<AhbExt>();
        if (own) { own->master = m; own->instr = instr; return nullptr; }
        e = AhbExt();
        e.master = m;
        e.instr  = instr;
        gp.set_extension(&e);
        return &e;
    }
    static void give_back(tlm::tlm_generic_payload& gp, AhbExt* e) {
        if (e) gp.clear_extension(e);
    }

    // -----------------------------------------------------------------------
    // Camino de datos del núcleo (D-bus, S-bus y AHB-AP comparten mapa)
    //   CCM  : conexión directa, fuera de la matriz [IR, §5.3]
    //   PPB  : SCS (SCB/NVIC/SysTick/MPU) o componentes de depuración
    //   Code : bus DCode (a través de la matriz)
    //   resto: bus System
    // -----------------------------------------------------------------------
    void route_data(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        const uint64_t a = gp.get_address();
        if (is_ccm(a))       { ccm->b_transport(gp, t); return; }
        if (is_ppb(a)) {
            if (is_scs(a))   scs_isk_->b_transport(gp, t);
            else             dbg_isk_->b_transport(gp, t);
            return;
        }
        if (a < 0x20000000ull) dcode->b_transport(gp, t);
        else                   sbus->b_transport(gp, t);
    }

    // Aplica el alias de 0x0000 0000 (BOOT/SYSCFG_MEMRMP) [IR, §2.3, §12.21.2].
    uint64_t remap(uint64_t a) const {
        return (a < 0x00100000ull) ? apply_boot_alias(uint32_t(a), boot_mode.read())
                                   : a;
    }

    void bt_ibus(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        AhbExt* lent = lend(gp, ext_i_, BusMaster::CORE_IBUS, true);
        const uint64_t a0 = gp.get_address();
        const uint64_t a  = remap(a0);
        gp.set_address(a);
        // El ICode solo cubre la región Code; una búsqueda de instrucción por
        // encima de 0x2000 0000 sale por el bus System [IR, §5.1, §7.6].
        if (a < 0x20000000ull) icode->b_transport(gp, t);
        else                   sbus->b_transport(gp, t);
        gp.set_address(a0);
        give_back(gp, lent);
    }

    void bt_dbus(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        AhbExt* lent = lend(gp, ext_d_, BusMaster::CORE_DBUS, false);
        const uint64_t a0 = gp.get_address();
        gp.set_address(remap(a0));
        if (bitband::is_alias(gp.get_address()))
            bitband::access(gp, t, [this](tlm::tlm_generic_payload& s,
                                          sc_core::sc_time& tt) { route_data(s, tt); });
        else
            route_data(gp, t);
        gp.set_address(a0);
        give_back(gp, lent);
    }

    void bt_sbus(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        AhbExt* lent = lend(gp, ext_s_, BusMaster::CORE_SBUS, false);
        if (bitband::is_alias(gp.get_address()))
            bitband::access(gp, t, [this](tlm::tlm_generic_payload& s,
                                          sc_core::sc_time& tt) { route_data(s, tt); });
        else
            route_data(gp, t);
        give_back(gp, lent);
    }

    // AHB-AP de depuración: mismo mapa que la CPU, pero se presenta en la
    // matriz como el bus System [IR, §13.2].
    void bt_dbg(tlm::tlm_generic_payload& gp, sc_core::sc_time& t) {
        AhbExt* lent = lend(gp, ext_dbg_, BusMaster::CORE_SBUS, false);
        const uint64_t a0 = gp.get_address();
        gp.set_address(remap(a0));
        if (bitband::is_alias(gp.get_address()))
            bitband::access(gp, t, [this](tlm::tlm_generic_payload& s,
                                          sc_core::sc_time& tt) { route_data(s, tt); });
        else
            route_data(gp, t);
        gp.set_address(a0);
        give_back(gp, lent);
    }
};

// -----------------------------------------------------------------------------
// La misma elección, pero en tiempo de compilación. `CortexM4F` sigue siendo la
// clase que hace el trabajo; esto solo fija sus rasgos.
// -----------------------------------------------------------------------------
template <const DebugCaps& C>
struct CoreT : CortexM4F {
    explicit CoreT(sc_core::sc_module_name nm) : CortexM4F(nm, C) {}
};
using CortexM4F_Pines   = CoreT<DBG_PINES>;     // depuración por los pines
using CortexM4F_GdbDap  = CoreT<DBG_INTERNO>;   // depuración por el DAP

} // namespace stm32
#endif // STM32_CORE_CORTEX_M4F_H
