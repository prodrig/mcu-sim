// =============================================================================
// cpu.h — Núcleo de ejecución ARMv7E-M (fetch / decode / execute)
//
// El banco de registros es una struct C++ (plan P3), no submódulos: R0-R12,
// SP (MSP/PSP), LR, PC, xPSR, PRIMASK, FAULTMASK, BASEPRI, CONTROL [IR, §7.2].
// La ISA a implementar (codificación, pseudocódigo y flags) está íntegra en
// doc/informe_instrucciones.md; el decodificador se valida con los vectores
// de doc/valida_instrucciones.py [fase F2].
// =============================================================================
#ifndef STM32_CORE_CPU_H
#define STM32_CORE_CPU_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "../common/ahb_types.h"

namespace stm32 {

// Interfaz NVIC <-> CPU (llamada a método; la implementa Nvic en scs.h)
class nvic_cpu_if : virtual public sc_core::sc_interface {
public:
    // Excepción pendiente de mayor prioridad que la de ejecución actual, o -1.
    virtual int  pending_exception(int current_prio) const = 0;
    virtual void ack_exception(int excp_num)               = 0;  // -> active
    virtual void return_exception(int excp_num)            = 0;  // EXC_RETURN
    virtual int  exception_priority(int excp_num) const    = 0;
};

// Estado arquitectónico [IR, §7.2-7.4; II, §0]
struct RegFile {
    uint32_t r[13]   = {0};        // R0-R12
    uint32_t msp = 0, psp = 0;     // R13 banked
    uint32_t lr  = 0xFFFFFFFF;     // R14
    uint32_t pc  = 0;              // R15
    uint32_t xpsr = 0x01000000;    // T=1
    uint8_t  primask = 0, faultmask = 0, basepri = 0, control = 0;
    bool     handler_mode = false;
    // FPv4-SP
    uint32_t s[32] = {0};
    uint32_t fpscr = 0;
};

SC_MODULE(Cpu) {
    // Buses hacia el router del núcleo (cortex_m4f.h)
    tlm_utils::simple_initiator_socket<Cpu> ibus{"ibus"};   // fetch
    tlm_utils::simple_initiator_socket<Cpu> dbus{"dbus"};   // datos/literales
    tlm_utils::simple_initiator_socket<Cpu> sbus{"sbus"};   // >= 0x2000_0000

    sc_core::sc_in<bool>   fclk{"fclk"};       // reloj libre del núcleo
    sc_core::sc_in<double> fclk_hz{"fclk_hz"};
    sc_core::sc_in<bool>   rst_n{"rst_n"};
    sc_core::sc_port<nvic_cpu_if> nvic{"nvic"};

    sc_core::sc_out<bool> sleeping{"sleeping"};    // WFI/WFE -> RCC/PWR
    sc_core::sc_out<bool> sleepdeep_out{"sleepdeep_out"};
    sc_core::sc_in<bool>  sleepdeep_cfg{"sleepdeep_cfg"};  // SCR.SLEEPDEEP (SCB)
    sc_core::sc_in<bool>  event_in{"event_in"};    // SEV externo / EXTI evento
    sc_core::sc_out<bool> event_out{"event_out"};  // instrucción SEV
    sc_core::sc_in<bool>  dbg_halt_req{"dbg_halt_req"};    // DHCSR.C_HALT
    sc_core::sc_out<bool> dbg_halted{"dbg_halted"};

    RegFile reg;   // estado arquitectónico (accesible por el debug DCRSR)

    SC_CTOR(Cpu) {
        SC_THREAD(exec_proc);
        sensitive << fclk.pos();
    }

private:
    void exec_proc() {
        // TODO(F2): reset (carga MSP de [0x0], PC de [0x4] [IR, §7.2.2]),
        //           bucle fetch(ibus) -> decode [II] -> execute; entrada/salida
        //           de excepciones (stacking, tail-chaining, lazy FPU),
        //           monitores LDREX/STREX (AhbExt.exclusive), WFI/WFE, IT.
        for (;;) wait();
    }
};

} // namespace stm32
#endif // STM32_CORE_CPU_H
