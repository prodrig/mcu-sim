// =============================================================================
// soc_fr5994.h — EL MSP430FR5994 (M8 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de las FRAM (soc/msp430/fr2/soc_fr2.h, con `fr5`) [DS
// SLASE54D]
//
//   RAM 0x1C00-0x2BFF (4 KB) y la del LEA, 0x2C00-0x3BFF (otros 4 KB, que la
//   CPU usa como RAM); FRAM 0x4000-0xFF7F y 0x10000-0x43FFF (256 KB);
//   información 0x1800-0x19FF; P1-P8 (con interrupciones) y PJ (PN-80); TA0,
//   TA1 y TA4 (3 CCR), TB0 (7), TA2 y TA3 (2); eUSCI_A0-A3 y B0-B3; MPY32;
//   CRC16 y CRC32; el ADC12_B; el Comp_E; REF_A; RTC_C; AES256; DMA de 6
//   canales; la MPU con la IPE; LFXT en PJ.4/PJ.5 y HFXT en PJ.6/PJ.7.
//
// El LEA (0xA80) está declarado: sus registros no hacen nada [de modelo]. Las
// funciones de P1-P4 y PJ son las del FR5969 [DS tablas 6-60 a 6-65: las
// mismas en esos pines]; P5-P7 añaden las eUSCI A2, A3, B1, B2 y B3 (con
// PxSEL = 01).
// =============================================================================
#ifndef STM32_SOC_MSP430_FR5994_SOC_FR5994_H
#define STM32_SOC_MSP430_FR5994_SOC_FR5994_H

#include "../fr2/soc_fr2.h"
#include "../fr5969/soc_fr5969.h"
#include "../desc/msp430fr5994.h"

namespace stm32 {

namespace fr5994 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_a3", 0x0340, 3, "TIMER0_A0", "TIMER0_A1"},
    {"timer1_a3", 0x0380, 3, "TIMER1_A0", "TIMER1_A1"},
    {"timer0_b7", 0x03C0, 7, "TIMER0_B0", "TIMER0_B1", true},
    {"timer2_a2", 0x0400, 2, "TIMER2_A0", "TIMER2_A1"},
    {"timer3_a2", 0x0440, 2, "TIMER3_A0", "TIMER3_A1"},
    {"timer4_a3", 0x07C0, 3, "TIMER4_A0", "TIMER4_A1"},
};
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x05C0, false, "EUSCI_A0"}, {"eusci_a1", 0x05E0, false, "EUSCI_A1"},
    {"eusci_a2", 0x0600, false, "EUSCI_A2"}, {"eusci_a3", 0x0620, false, "EUSCI_A3"},
    {"eusci_b0", 0x0640, true, "EUSCI_B0"},  {"eusci_b1", 0x0680, true, "EUSCI_B1"},
    {"eusci_b2", 0x06C0, true, "EUSCI_B2"},  {"eusci_b3", 0x0700, true, "EUSCI_B3"},
};
enum : uint8_t { UA0 = 0, UA1 = 1, UA2 = 2, UA3 = 3, UB0 = 4, UB1 = 5, UB2 = 6, UB3 = 7 };
// Las del FR5969 (P1-P4 y PJ), con las eUSCI renumeradas, y P5-P7
inline constexpr auto FN = [] {
    constexpr size_t N5969 = sizeof fr5969::FN / sizeof fr5969::FN[0];
    std::array<Fr2Fn, N5969 + 22> t{};
    size_t n = 0;
    for (size_t i = 0; i < N5969; ++i) {
        Fr2Fn f = fr5969::FN[i];
        if (f.tipo == 2) f.mod = f.mod == fr5969::UB0 ? UB0 : f.mod;   // A0 y A1 siguen en 0 y 1
        t[n++] = f;
    }
    const Fr2Fn mas[] = {
        {fr2_pin(5, 0), 1, 2, UB1, 0}, {fr2_pin(5, 1), 1, 2, UB1, 1}, {fr2_pin(5, 2), 1, 2, UB1, 2}, {fr2_pin(5, 3), 1, 2, UB1, 3},
        {fr2_pin(5, 4), 1, 2, UA2, 0}, {fr2_pin(5, 5), 1, 2, UA2, 1}, {fr2_pin(5, 6), 1, 2, UA2, 2}, {fr2_pin(5, 7), 1, 2, UA2, 3},
        {fr2_pin(6, 0), 1, 2, UA3, 0}, {fr2_pin(6, 1), 1, 2, UA3, 1}, {fr2_pin(6, 2), 1, 2, UA3, 2}, {fr2_pin(6, 3), 1, 2, UA3, 3},
        {fr2_pin(6, 4), 1, 2, UB3, 0}, {fr2_pin(6, 5), 1, 2, UB3, 1}, {fr2_pin(6, 6), 1, 2, UB3, 2}, {fr2_pin(6, 7), 1, 2, UB3, 3},
        {fr2_pin(7, 0), 1, 2, UB2, 0}, {fr2_pin(7, 1), 1, 2, UB2, 1}, {fr2_pin(7, 2), 1, 2, UB2, 2}, {fr2_pin(7, 3), 1, 2, UB2, 3},
        {fr2_pin(5, 6), 2, 4, 0, 1}, {fr2_pin(5, 7), 2, 4, 0, 0},
    };
    for (const Fr2Fn& f : mas) t[n++] = f;
    return t;
}();
inline constexpr uint8_t CE_CCIB[] = {fr5969::TA0, 1, fr5969::TA1, 1, fr5969::TB0, 1, fr5969::TA2, 1, fr5969::TA3, 1};
inline constexpr uint8_t DMA_CCR0[] = {1, 3, 7, 5, 6, 8};
inline constexpr uint8_t DMA_USCI[] = {14, 16, 20, 22, 18, 24, 0, 0};
} // namespace fr5994

inline constexpr ChipFr2 CHIP_FR5994 = [] {
    ChipFr2 c = CHIP_FR5969;
    c.desc = &m430::msp430fr5994::DESC;
    c.caps = M430Caps{"msp430fr5994", M430Arq::CPUXV2, m430::msp430fr5994::DESC.errata, true};
    c.etiqueta = "fr5994";
    c.npuertos = 11;
    const uint8_t b[11] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0xFF};
    for (unsigned i = 0; i < 11; ++i) c.bits[i] = b[i];
    c.npuertos_irq = 8;
    c.timers = fr5994::TIMERS; c.ntimers = 6;
    c.uscis = fr5994::USCIS; c.nuscis = 8;
    c.fn = fr5994::FN.data(); c.nfn = fr5994::FN.size();
    c.bcl_usci = fr5994::UA0;
    c.tlv_id = 0x82A1;
    c.rtcb_base = 0; c.rtcc_base = 0x04A0;
    c.crc32_base = 0x0980;
    c.dma_canales = 6;
    c.dma_tsel_ccr0 = fr5994::DMA_CCR0; c.dma_tsel_usci = fr5994::DMA_USCI;
    c.v_rtc5 = "RTC_C"; c.v_adc5 = "ADC12_B";
    return c;
}();

struct SocFR5994 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr5994::DESC;
    SocFR5994(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR5994, cab) {
        cristal_xt1 = true;                   // la LaunchPad: Y4, 32 768 Hz [SLAU678C]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR5994_SOC_FR5994_H
