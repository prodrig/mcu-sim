// =============================================================================
// gpio_port.h — Puerto GPIO (AHB1; plan P1) — instanciado 9 veces (A..I)
//
// Registros MODER/OTYPER/OSPEEDR/PUPDR/IDR/ODR/BSRR/LCKR/AFRL/AFRH [IR, §3].
// Gobierna los 16 pads del puerto (bundle PadDrive) y lee las entradas
// digitalizadas (IDR muestreado con HCLK). Publica la selección AF por pin al
// pin_mux y las 16 líneas hacia EXTI (una por pin; el mux de puerto lo hace
// SYSCFG_EXTICR dentro del EXTI/SYSCFG [IR, §9.4.3]).
// =============================================================================
#ifndef STM32_PERIPH_GPIO_PORT_H
#define STM32_PERIPH_GPIO_PORT_H

#include "../common/periph_base.h"
#include "../pins/pad.h"

namespace stm32 {

class GpioPort : public BusSlave {
public:
    sc_core::sc_vector<sc_core::sc_out<PadDrive>> pad_drive;  // [16] -> pads
    sc_core::sc_vector<sc_core::sc_in<bool>>      pad_din;    // [16] <- pads
    sc_core::sc_vector<sc_core::sc_out<bool>>     exti_line;  // [16] -> EXTI/SYSCFG

    // idx: 0=A .. 8=I. Valores de reset especiales de PA/PB (debug) [IR, §3.4.1]
    GpioPort(sc_core::sc_module_name nm, unsigned idx)
        : BusSlave(nm, addr::GPIOA_B + 0x400u * idx, 0x400),
          pad_drive("pad_drive", 16), pad_din("pad_din", 16),
          exti_line("exti_line", 16), idx_(idx) {
        SC_HAS_PROCESS(GpioPort);
        SC_METHOD(update_pads);
        sensitive << ev_regs_;
        SC_METHOD(sample_idr);
        sensitive << clk.pos();
        dont_initialize();
    }

protected:
    uint32_t reg_read(uint32_t off) override {
        (void)off; return 0;
        // TODO(F3): MODER(0x00)/OTYPER/OSPEEDR/PUPDR/IDR/ODR/BSRR/LCKR/AFRL/AFRH
        //           con resets especiales GPIOA=0xA8000000, GPIOB=0x00000280...
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        (void)off; (void)v; (void)be;
        ev_regs_.notify(sc_core::SC_ZERO_TIME);
        // TODO(F3): BSRR atómico (BS gana a BR [IR, §3.4.7]); FSM LCKR 1-0-1;
        //           publicar af_sel al pin_mux cuando cambien MODER/AFRx.
    }

private:
    unsigned idx_;
    sc_core::sc_event ev_regs_;

    void update_pads() {
        // TODO(F3): traducir MODER/OTYPER/OSPEEDR/PUPDR/ODR (o AF out) a PadDrive
    }
    void sample_idr() {
        // TODO(F3): IDR <= pad_din (muestreo por HCLK [IR, §3.3.1]);
        //           exti_line[i] <= pad_din[i]
    }
};

} // namespace stm32
#endif // STM32_PERIPH_GPIO_PORT_H
