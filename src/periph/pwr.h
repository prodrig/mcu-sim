// =============================================================================
// pwr.h — Controlador de energía (APB1) [IR, §12.21/14]
// Máquina de estados Run/Sleep/Stop/Standby coordinada con CPU (WFI/WFE +
// SLEEPDEEP) y RCC; PVD/BOR desde el nivel real de VDD (PowerPads); DBP
// protege el dominio backup; pin WKUP (PA0) y flags WUF/SBF.
// =============================================================================
#ifndef STM32_PERIPH_PWR_H
#define STM32_PERIPH_PWR_H

#include "../common/periph_base.h"

namespace stm32 {

class Pwr : public BusSlave {
public:
    sc_core::sc_in<double> vdd_lvl{"vdd_lvl"};        // PowerPads
    sc_core::sc_in<bool>   sleeping{"sleeping"};      // CPU (WFI/WFE)
    sc_core::sc_in<bool>   sleepdeep{"sleepdeep"};    // SCB.SCR via CPU
    sc_core::sc_in<bool>   wkup_pin{"wkup_pin"};      // PA0 (EWUP)
    sc_core::sc_in<bool>   exti_wakeup{"exti_wakeup"};// EXTI (salida de Stop)
    sc_core::sc_out<bool>  irq_pvd{"irq_pvd"};        // via EXTI16
    sc_core::sc_out<bool>  dbp{"dbp"};                // acceso dominio backup
    sc_core::sc_out<bool>  standby_req{"standby_req"};// -> RCC (apagar 1.2V)
    sc_core::sc_out<bool>  stop_req{"stop_req"};      // -> RCC (parar relojes)
    sc_core::sc_out<bool>  vos_rdy{"vos_rdy"};

    enum : uint32_t { R_CR = 0x00, R_CSR = 0x04 };
    static constexpr uint32_t CR_DBP = 1u << 8;      // acceso al dominio backup

    Pwr(sc_core::sc_module_name nm) : BusSlave(nm, addr::PWR_B, 0x400) {
        SC_HAS_PROCESS(Pwr);
        SC_METHOD(power_fsm);
        sensitive << sleeping << sleepdeep << exti_wakeup << wkup_pin << vdd_lvl;
        dont_initialize();
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(rst_proc); sensitive << rst_n;
    }

protected:
    // Fase F5: el banco de registros, que es lo que hace falta para que el RTC
    // sea utilizable —DBP es la llave que abre el dominio de backup entero
    // [IR, §12.9-integración]—. La máquina de Stop/Standby sigue siendo trabajo
    // de la fase F7.
    uint32_t cr_ = 0x0000C000u, csr_ = 0x00004000u;   // VOS = escala 1, VOSRDY
    bool     o_dbp_ = false, o_vos_ = true;
    sc_core::sc_event pub_ev_;

    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return cr_;
            case R_CSR: return csr_;
            default:    return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t cur = reg_read(off), m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (cur & ~m) | (v & m);
        }
        switch (off) {
            case R_CR:
                cr_ = v & 0x0000C3FFu;
                // CWUF y CSBF son órdenes de borrado, no bits guardados
                if (v & (1u << 2)) csr_ &= ~1u;               // CWUF
                if (v & (1u << 3)) csr_ &= ~2u;               // CSBF
                cr_ &= ~((1u << 2) | (1u << 3));
                publish();
                return;
            case R_CSR:
                // Solo EWUP y BRE son de escritura; el resto son banderas
                csr_ = (csr_ & ~0x0300u) | (v & 0x0300u);
                return;
            default: return;
        }
    }
    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void pub_proc() {
        dbp.write((cr_ & CR_DBP) != 0);
        vos_rdy.write((csr_ & (1u << 14)) != 0);
    }
    void rst_proc() {
        if (rst_n.read()) return;
        cr_ = 0x0000C000u; csr_ = 0x00004000u;
        publish();
    }
    void power_fsm() {
        // TODO(F7): entrada y salida de Stop y Standby (Standby => reset tipo
        //           POR con SBF = 1), PVD sobre el nivel real de VDD y el pin
        //           WKUP [IR, §14.6].
    }
};

} // namespace stm32
#endif // STM32_PERIPH_PWR_H
