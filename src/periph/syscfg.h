// =============================================================================
// syscfg.h — SYSCFG (APB2; plan P2) [IR, §12.21.2]
//
// MEMRMP (aliasing de 0x0 -> router del núcleo), PMC (MII/RMII), EXTICR1-4
// (mux GPIO->EXTI), CMPCR (celda de compensación).
//
// Fase F1: banco de registros funcional. MEM_MODE arranca con el valor que
// imponen los pines BOOT muestreados tras el reset [IR, §2.3] y se publica al
// router del núcleo, que resuelve el espejo de 0x0000 0000.
// =============================================================================
#ifndef STM32_PERIPH_SYSCFG_H
#define STM32_PERIPH_SYSCFG_H

#include "../common/periph_base.h"

namespace stm32 {

class Syscfg : public BusSlave {
public:
    enum : uint32_t {
        MEMRMP = 0x00, PMC = 0x04, EXTICR1 = 0x08, EXTICR2 = 0x0C,
        EXTICR3 = 0x10, EXTICR4 = 0x14, CMPCR = 0x20
    };

    sc_core::sc_in<uint8_t>  boot_pins{"boot_pins"};   // BOOT[1:0] muestreados
    sc_core::sc_out<uint8_t> mem_mode{"mem_mode"};     // -> router del núcleo
    sc_core::sc_out<bool>    mii_rmii_sel{"mii_rmii_sel"};  // -> ETH_MAC
    sc_core::sc_vector<sc_core::sc_out<uint8_t>> exticr_sel; // [16] -> EXTI

    explicit Syscfg(sc_core::sc_module_name nm)
        : BusSlave(nm, addr::SYSCFG_B, 0x400), exticr_sel("exticr_sel", 16) {
        SC_HAS_PROCESS(Syscfg);
        // Un único proceso escribe los puertos: el estado lo actualizan tanto
        // los procesos internos como el b_transport del banco de registros.
        SC_METHOD(publish_proc); sensitive << pub_ev_;              dont_initialize();
        SC_METHOD(boot_proc);   sensitive << boot_pins;             dont_initialize();
        SC_METHOD(reset_proc);  sensitive << rst_n;                 dont_initialize();
    }

    uint8_t mode() const { return uint8_t(memrmp_ & 3u); }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case MEMRMP:  return memrmp_;
            case PMC:     return pmc_;
            case EXTICR1: case EXTICR2: case EXTICR3: case EXTICR4:
                          return exticr_[(off - EXTICR1) / 4];
            case CMPCR:   return cmpcr_;
            default:      return 0;
        }
    }

    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {                              // fusión byte a byte
            const uint32_t cur = reg_read(off);
            uint32_t mask = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) mask |= 0xFFu << (8 * b);
            v = (cur & ~mask) | (v & mask);
        }
        switch (off) {
            case MEMRMP:
                memrmp_ = v & 3u;
                publish();
                break;
            case PMC:
                pmc_ = v & 0x00800000u;
                publish();
                break;
            case EXTICR1: case EXTICR2: case EXTICR3: case EXTICR4: {
                const unsigned k = (off - EXTICR1) / 4;
                exticr_[k] = v & 0x0000FFFFu;
                publish();
                break;
            }
            case CMPCR:
                // CMP_PD es rw; READY (bit 8) lo genera el hardware: se modela
                // como listo inmediatamente cuando se enciende la celda.
                cmpcr_ = (v & 1u) ? 0x00000101u : 0u;
                break;
            default: break;
        }
    }

private:
    uint32_t memrmp_ = MEM_MODE_FLASH, pmc_ = 0, cmpcr_ = 0;
    uint32_t exticr_[4] = {0, 0, 0, 0};

    // El valor de reset de MEM_MODE refleja los pines BOOT [IR, §2.3, §12.21.2]:
    //   BOOT0=0        -> Flash principal
    //   BOOT1=0,BOOT0=1-> System memory (bootloader)
    //   BOOT1=1,BOOT0=1-> SRAM1
    void boot_proc() {
        const uint8_t b = boot_pins.read();            // bit0 = BOOT0, bit1 = BOOT1
        uint8_t m = MEM_MODE_FLASH;
        if (b & 1u) m = (b & 2u) ? MEM_MODE_SRAM1 : MEM_MODE_SYSTEM;
        memrmp_ = m;
        publish();
    }

    void reset_proc() {
        if (!rst_n.read()) {
            pmc_ = 0; cmpcr_ = 0;
            for (unsigned k = 0; k < 4; ++k) exticr_[k] = 0;
            boot_proc();                               // MEMRMP vuelve a BOOT
        }
    }

    void publish() { pub_ev_.notify(sc_core::SC_ZERO_TIME); }
    void publish_proc() {
        mem_mode.write(uint8_t(memrmp_ & 3u));
        mii_rmii_sel.write(((pmc_ >> 23) & 1u) != 0);
        for (unsigned k = 0; k < 4; ++k)
            for (unsigned i = 0; i < 4; ++i)
                exticr_sel[k * 4 + i].write(uint8_t((exticr_[k] >> (4 * i)) & 0xFu));
    }

    sc_core::sc_event pub_ev_;
};

} // namespace stm32
#endif // STM32_PERIPH_SYSCFG_H
