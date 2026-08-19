// =============================================================================
// spi.h — SPI1/2/3 con modo I2S (SPI2/3) [IR, §12.5/12.7]
// =============================================================================
#ifndef STM32_PERIPH_SPI_H
#define STM32_PERIPH_SPI_H

#include "../common/periph_base.h"

namespace stm32 {

class Spi : public BusSlave {
public:
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_out<bool> dma_req_rx{"dma_req_rx"}, dma_req_tx{"dma_req_tx"};
    // AF: SCK/MISO/MOSI/NSS (+WS/CK/SD/MCK en modo I2S sobre los mismos pines)
    sc_core::sc_signal<bool> sck_out{"sck_out"}, sck_oe{"sck_oe"}, sck_in{"sck_in"};
    sc_core::sc_signal<bool> miso_out{"miso_out"}, miso_oe{"miso_oe"}, miso_in{"miso_in"};
    sc_core::sc_signal<bool> mosi_out{"mosi_out"}, mosi_oe{"mosi_oe"}, mosi_in{"mosi_in"};
    sc_core::sc_signal<bool> nss_out{"nss_out"}, nss_oe{"nss_oe"}, nss_in{"nss_in"};
    sc_core::sc_in<bool> i2s_ext_clk{"i2s_ext_clk"};   // PLLI2S R (RCC) o I2S_CKIN

    Spi(sc_core::sc_module_name nm, uint32_t base, bool has_i2s)
        : BusSlave(nm, base, 0x400), has_i2s_(has_i2s) {
        SC_HAS_PROCESS(Spi);
        SC_THREAD(shift_proc);
    }

protected:
    bool has_i2s_;
    void shift_proc() {
        // TODO(F5): shift register full-duplex sensible a CPOL/CPHA; maestro
        //           (BR) y esclavo (sck_in); CRC; modo I2S (WS, DATLEN)
        //           [IR, §12.5.3/12.7]; MODF con nss_in.
        for (;;) wait(sc_core::sc_time(1, sc_core::SC_MS));
    }
};

} // namespace stm32
#endif // STM32_PERIPH_SPI_H
