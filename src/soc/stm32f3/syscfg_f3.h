// =============================================================================
// syscfg_f3.h — El SYSCFG del STM32F303, y el PWR básico (fase S1)
//
// SYSCFG, en 0x4001_0000 [CMSIS: SYSCFG_TypeDef y SYSCFG_*_Pos]:
//
//   0x00 CFGR1   MEM_MODE (1:0): lo que hay en 0x0 —00 la Flash, 01 la memoria
//                de sistema, 11 la SRAM—, que sale de reset con lo que dijeron
//                BOOT0 y nBOOT1 y luego cambia el firmware; los remapeos (USB_IT,
//                TIM1_ITR3, DAC1_TRIG1, DMA), los Fm+ de los I²C y FPU_IE (31:26)
//   0x04 RCR     protección de escritura de la CCM, por páginas de 1 KB
//   0x08..0x14   EXTICR1-4: qué puerto (0 = A .. 5 = F) llega a cada línea
//   0x18 CFGR2   los cerrojos (LOCKUP, paridad de la SRAM, PVD) y SRAM_PE
//
// y en la misma ventana, aunque son otros bloques, los registros de los siete
// comparadores (0x1C-0x34) y los cuatro amplificadores (0x38-0x44) [CMSIS:
// COMPx_BASE, OPAMPx_BASE]. En esta fase guardan lo que se escribe y avisan
// una vez: son de S5.
//
// PWR, en 0x4000_7000 [CMSIS: PWR_TypeDef]: CR (LPDS, PDDS, CWUF, CSBF, PVDE,
// PLS, DBP) y CSR (WUF, SBF, PVDO, VREFINTRDYF, EWUP1-3). De lo que hace, aquí
// solo cuenta DBP —que abre el dominio de respaldo al RCC y al RTC— y VREFINTRDYF;
// los modos de bajo consumo son de S3.
// =============================================================================
#ifndef STM32_SOC_STM32F3_SYSCFG_F3_H
#define STM32_SOC_STM32F3_SYSCFG_F3_H

#include <cstdio>
#include "../../common/periph_base.h"
#include "f3_desc.h"

namespace stm32 {

class SyscfgF3 : public BusSlave {
public:
    enum : uint32_t { R_CFGR1 = 0x00, R_RCR = 0x04, R_EXTICR1 = 0x08, R_CFGR2 = 0x18,
                      R_COMP1 = 0x1C, R_OPAMP4 = 0x44 };
    // MEM_MODE [CMSIS: SYSCFG_CFGR1_MEM_MODE]
    enum MemMode : uint8_t { MEM_FLASH = 0, MEM_SISTEMA = 1, MEM_SRAM = 3 };

    sc_core::sc_vector<sc_core::sc_out<uint8_t>> exticr_sel;   // [16] -> EXTI
    sc_core::sc_in<uint8_t> boot{"boot"};                      // lo muestreado al salir de reset

    explicit SyscfgF3(sc_core::sc_module_name nm)
        : BusSlave(nm, f3::SYSCFG_B, 0x400), exticr_sel("exticr_sel", 16) {
        SC_HAS_PROCESS(SyscfgF3);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n << boot;
    }

    uint8_t mem_mode() const { return uint8_t(cfgr1_ & 3u); }
    // ¿Se puede escribir esta dirección de la CCM? (RCR, un bit por KB)
    bool ccm_protegida(uint32_t off) const { return (rcr_ >> (off / 0x400u)) & 1u; }
    uint32_t cfgr1() const { return cfgr1_; }
    unsigned avisos_comp() const { return n_comp_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        if (off >= R_EXTICR1 && off < R_CFGR2) return exticr_[(off - R_EXTICR1) / 4];
        if (off >= R_COMP1 && off <= R_OPAMP4) return compopamp_[(off - R_COMP1) / 4];
        switch (off) {
            case R_CFGR1: return cfgr1_;
            case R_RCR:   return rcr_;
            case R_CFGR2: return cfgr2_;
            default:      return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (reg_read(off) & ~m) | (v & m);
        }
        if (off >= R_EXTICR1 && off < R_CFGR2) {
            exticr_[(off - R_EXTICR1) / 4] = v & 0xFFFFu;
            pub_ev_.notify(sc_core::SC_ZERO_TIME);
            return;
        }
        if (off >= R_COMP1 && off <= R_OPAMP4) {
            // Los bits de bloqueo (31) de COMPx_CSR y OPAMPx_CSR: una vez a uno,
            // el registro no cambia hasta el reset [CMSIS: COMP_CSR_COMPxLOCK]
            uint32_t& r = compopamp_[(off - R_COMP1) / 4];
            if (!(r & 0x80000000u)) r = v;
            if (!avisado_comp_) {
                avisado_comp_ = true;
                ++n_comp_;
                SC_REPORT_WARNING("syscfg_f3", "COMPx_CSR / OPAMPx_CSR: los comparadores y "
                                  "los amplificadores estan DECLARADOS (fase S5): guardan lo "
                                  "escrito y no hacen nada");
            }
            return;
        }
        switch (off) {
            case R_CFGR1:
                // MEM_MODE, los remapeos, los Fm+, ENCODER_MODE y FPU_IE
                cfgr1_ = v & 0xFCFF39E3u;
                break;
            case R_RCR: rcr_ = (rcr_ | v) & 0xFFu; break;     // solo se ponen, no se quitan
            case R_CFGR2:
                // Los tres cerrojos se ponen y no se quitan; SRAM_PE es rc_w1
                cfgr2_ = (cfgr2_ & 0x7u) | (v & 0x17u);
                if (v & (1u << 8)) cfgr2_ &= ~(1u << 8);
                break;
            default: return;
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }

private:
    uint32_t cfgr1_ = 0x7C000000u, rcr_ = 0, cfgr2_ = 0;
    uint32_t exticr_[4] = {};
    uint32_t compopamp_[11] = {};
    bool avisado_comp_ = false;
    unsigned n_comp_ = 0;
    sc_core::sc_event pub_ev_;

    void pub_proc() {
        for (unsigned l = 0; l < 16; ++l)
            exticr_sel[l].write(uint8_t((exticr_[l / 4] >> (4 * (l % 4))) & 0xFu));
    }
    // En reset, MEM_MODE sigue a los pines de arranque, de modo que al soltarlo
    // ya vale lo que dijeron; FPU_IE vale 011111 [1F: RM0316 §12.1.1]. Fuera
    // de reset, lo que cambie el firmware.
    void reset_proc() {
        if (rst_n.read()) return;
        cfgr1_ = 0x7C000000u | (boot.read() & 3u);
        rcr_ = 0; cfgr2_ = 0;
        for (uint32_t& e : exticr_) e = 0;
        for (uint32_t& c : compopamp_) c = 0;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

class PwrF3 : public BusSlave {
public:
    enum : uint32_t { R_CR = 0x00, R_CSR = 0x04 };
    sc_core::sc_out<bool> dbp{"dbp"};

    explicit PwrF3(sc_core::sc_module_name nm) : BusSlave(nm, f3::PWR_B, 0x400) {
        SC_HAS_PROCESS(PwrF3);
        SC_METHOD(pub_proc); sensitive << pub_ev_;
        SC_METHOD(reset_proc); sensitive << rst_n;
    }
    uint32_t cr() const { return cr_; }

protected:
    uint32_t reg_read(uint32_t off) override {
        switch (off) {
            case R_CR:  return cr_;
            // VREFINTRDYF: la referencia está lista en cuanto hay tensión
            case R_CSR: return csr_ | (1u << 3);
            default:    return 0;
        }
    }
    void reg_write(uint32_t off, uint32_t v, uint32_t be) override {
        if (be != 0xFu) {
            uint32_t m = 0;
            for (unsigned b = 0; b < 4; ++b) if (be & (1u << b)) m |= 0xFFu << (8 * b);
            v = (reg_read(off) & ~m) | (v & m);
        }
        switch (off) {
            case R_CR:
                if (v & (1u << 2)) csr_ &= ~1u;            // CWUF
                if (v & (1u << 3)) csr_ &= ~2u;            // CSBF
                cr_ = v & 0x1F3u;                          // LPDS, PDDS, PVDE, PLS, DBP
                break;
            case R_CSR: csr_ = (csr_ & 0x7u) | (v & 0x700u); break;   // EWUP1-3
            default: return;
        }
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
private:
    uint32_t cr_ = 0, csr_ = 0;
    sc_core::sc_event pub_ev_;
    void pub_proc() { dbp.write((cr_ >> 8) & 1u); }
    void reset_proc() {
        if (rst_n.read()) return;
        cr_ = 0; csr_ = 0;
        pub_ev_.notify(sc_core::SC_ZERO_TIME);
    }
};

} // namespace stm32
#endif // STM32_SOC_STM32F3_SYSCFG_F3_H
