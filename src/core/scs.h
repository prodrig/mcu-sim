// =============================================================================
// scs.h — System Control Space: SCB + NVIC + SysTick + MPU (+ regs FPU-SCS)
//
// Bloque de 4 KB en 0xE000E000 accesible solo desde el núcleo (PPB) [IR, §10.1].
// EXTI NO está aquí: es un periférico APB2 (plan P2). El NVIC implementa la
// interfaz nvic_cpu_if para el arbitraje de excepciones con la CPU.
// =============================================================================
#ifndef STM32_CORE_SCS_H
#define STM32_CORE_SCS_H

#include "../common/periph_base.h"
#include "cpu.h"

namespace stm32 {

// --------------------------------------------------------------------------
class Nvic : public sc_core::sc_module, public nvic_cpu_if {
public:
    sc_core::sc_vector<sc_core::sc_in<bool>> irq_in;   // [N_IRQ=82] [IR, §9.1.2]
    sc_core::sc_in<bool> nmi_in{"nmi_in"};             // CSS y otros
    sc_core::sc_export<nvic_cpu_if> cpu_if{"cpu_if"};

    explicit Nvic(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm), irq_in("irq_in", N_IRQ) {
        cpu_if(*this);
        SC_HAS_PROCESS(Nvic);
        SC_METHOD(sample_proc);
        for (unsigned i = 0; i < N_IRQ; ++i) sensitive << irq_in[i];
        sensitive << nmi_in;
        dont_initialize();
    }

    // nvic_cpu_if — TODO(F2): prioridades ISER/IPR/PRIGROUP, estados
    // inactive/pending/active, tail-chaining/late-arrival [IR, §9]
    int  pending_exception(int) const override { return -1; }
    void ack_exception(int) override {}
    void return_exception(int) override {}
    int  exception_priority(int) const override { return 256; }

    // Acceso a registros (lo enruta Scs): ISER/ICER/ISPR/ICPR/IABR/IPR/STIR
    uint32_t reg_read(uint32_t) { return 0; }                       // TODO(F2)
    void     reg_write(uint32_t, uint32_t) {}                       // TODO(F2)

private:
    void sample_proc() { /* TODO(F2): latch de pendientes por flanco/nivel */ }
};

// --------------------------------------------------------------------------
SC_MODULE(SysTick) {
    sc_core::sc_in<bool> proc_clk{"proc_clk"};      // FCLK (CLKSOURCE=1)
    sc_core::sc_in<bool> ext_clk{"ext_clk"};        // HCLK/8 (CLKSOURCE=0)
    sc_core::sc_in<bool> rst_n{"rst_n"};
    sc_core::sc_out<bool> tick_irq{"tick_irq"};     // excepción 15 (interna SCS)
    SC_CTOR(SysTick) {
        SC_METHOD(count_proc);
        sensitive << proc_clk.pos() << ext_clk.pos();
        dont_initialize();
    }
    uint32_t reg_read(uint32_t) { return 0; }       // CSR/RVR/CVR/CALIB TODO(F2)
    void     reg_write(uint32_t, uint32_t) {}
private:
    void count_proc() { /* TODO(F2): downcounter 24 bits, COUNTFLAG [IR, §10.3] */ }
};

// --------------------------------------------------------------------------
SC_MODULE(Mpu) {
    SC_CTOR(Mpu) {}
    // TODO(F5): 8 regiones, chequeo por transacción (llamada desde el router
    //           del núcleo antes de emitir al bus) [IR, §10.4]
    bool check(uint64_t /*addr*/, bool /*write*/, bool /*instr*/,
               bool /*privileged*/) { return true; }
    uint32_t reg_read(uint32_t) { return 0; }
    void     reg_write(uint32_t, uint32_t) {}
};

// --------------------------------------------------------------------------
SC_MODULE(Scb) {
    sc_core::sc_out<bool> sysresetreq{"sysresetreq"};   // AIRCR -> RCC [IR, §4.1]
    sc_core::sc_out<bool> sleepdeep{"sleepdeep"};       // SCR -> CPU/PWR
    SC_CTOR(Scb) {}
    // CPUID/ICSR/VTOR/AIRCR/SCR/CCR/SHPR/SHCSR/CFSR/HFSR/DFSR/MMFAR/BFAR/
    // CPACR + FPCCR/FPCAR/FPDSCR [IR, §10.2/10.5/8.12]  TODO(F2)
    uint32_t reg_read(uint32_t) { return 0; }
    void     reg_write(uint32_t, uint32_t) {}
    uint32_t vtor = 0;                                  // usado por CPU en excepciones
};

// --------------------------------------------------------------------------
// Contenedor SCS: decodifica 0xE000E000-0xE000EFFF entre sus bloques.
// --------------------------------------------------------------------------
SC_MODULE(Scs) {
    tlm_utils::simple_target_socket<Scs> ppb{"ppb"};    // desde el router del núcleo

    Nvic    nvic{"nvic"};
    SysTick systick{"systick"};
    Mpu     mpu{"mpu"};
    Scb     scb{"scb"};

    SC_CTOR(Scs) {
        ppb.register_b_transport(this, &Scs::bt);
    }

private:
    void bt(tlm::tlm_generic_payload& gp, sc_core::sc_time&) {
        const uint32_t a = uint32_t(gp.get_address());
        (void)a;
        // TODO(F2): decode E000E010 SysTick / E000E100+EF00 NVIC+STIR /
        //           E000ED00 SCB / E000ED90 MPU / E000EF30 FPU [IR, §10.1]
        gp.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

} // namespace stm32
#endif // STM32_CORE_SCS_H
