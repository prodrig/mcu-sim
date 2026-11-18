// =============================================================================
// soc_fr5969.h — EL MSP430FR5969 (M8 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de las FRAM (soc/msp430/fr2/soc_fr2.h, con `fr5`) [DS
// SLAS704G]
//
//   RAM 0x1C00-0x23FF (2 KB), FRAM 0x4400-0xFF7F y 0x10000-0x13FFF (64 KB),
//   información 0x1800-0x19FF; P1-P4 (con interrupciones y despertar de
//   LPMx.5) y PJ (RGZ-48); TA0, TA1 (3 CCR), TB0 (7 CCR), TA2, TA3 (2 CCR);
//   eUSCI_A0, A1 y B0; MPY32; CRC16; el ADC12_B con A0-A15; el Comp_E con
//   C0-C15; REF_A; RTC_B; AES256; DMA de 3 canales; la MPU con la IPE; LFXT
//   en PJ.4/PJ.5 y HFXT en PJ.6/PJ.7; JTAG de 4 hilos en PJ.0-PJ.3; el
//   backchannel de la LaunchPad, la UCA0 en P2.0 (TXD) y P2.1 (RXD).
//
// Las funciones de los pines [DS tablas 6-49 a 6-61], las conexiones de los
// Timer [tablas 6-13 a 6-17: la salida del Comp_E a las CCI1B], los
// disparos del ADC12_B [tabla 6-18] y los del DMA [tabla 6-12].
// =============================================================================
#ifndef STM32_SOC_MSP430_FR5969_SOC_FR5969_H
#define STM32_SOC_MSP430_FR5969_SOC_FR5969_H

#include "../fr2/soc_fr2.h"
#include "../desc/msp430fr5969.h"

namespace stm32 {

namespace fr5969 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_a3", 0x0340, 3, "TIMER0_A0", "TIMER0_A1"},
    {"timer1_a3", 0x0380, 3, "TIMER1_A0", "TIMER1_A1"},
    {"timer0_b7", 0x03C0, 7, "TIMER0_B0", "TIMER0_B1", true},
    {"timer2_a2", 0x0400, 2, "TIMER2_A0", "TIMER2_A1"},
    {"timer3_a2", 0x0440, 2, "TIMER3_A0", "TIMER3_A1"},
};
enum : uint8_t { TA0 = 0, TA1 = 1, TB0 = 2, TA2 = 3, TA3 = 4 };
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x05C0, false, "USCI_A0"},
    {"eusci_a1", 0x05E0, false, "USCI_A1"},
    {"eusci_b0", 0x0640, true, "USCI_B0"},
};
enum : uint8_t { UA0 = 0, UA1 = 1, UB0 = 2 };
// tipo: 1 Timer, 2 eUSCI, 3 analógico/cristal, 4 reloj, 10 COUT (Comp_E)
inline constexpr Fr2Fn FN[] = {
    // P1: 01 los Timer; 10 RTCCLK/COUT/UCB0STE/UCA0STE/UCA0CLK/UCB0SIMO/UCB0SOMI; 11 analógica y TA0.0/TA1.0
    {fr2_pin(1, 0), 1, 1, TA0, 1}, {fr2_pin(1, 1), 1, 1, TA0, 2}, {fr2_pin(1, 2), 1, 1, TA1, 1}, {fr2_pin(1, 3), 1, 1, TA1, 2},
    {fr2_pin(1, 4), 1, 1, TB0, 1}, {fr2_pin(1, 5), 1, 1, TB0, 2}, {fr2_pin(1, 6), 1, 1, TB0, 3}, {fr2_pin(1, 7), 1, 1, TB0, 4},
    {fr2_pin(1, 1), 2, 10, 0, 0}, {fr2_pin(1, 2), 2, 10, 0, 0}, {fr2_pin(1, 3), 2, 2, UB0, 3}, {fr2_pin(1, 4), 2, 2, UA0, 3},
    {fr2_pin(1, 5), 2, 2, UA0, 2}, {fr2_pin(1, 6), 2, 2, UB0, 0}, {fr2_pin(1, 7), 2, 2, UB0, 1},
    {fr2_pin(1, 0), 3, 3, 0, 0}, {fr2_pin(1, 1), 3, 3, 0, 0}, {fr2_pin(1, 2), 3, 3, 0, 0}, {fr2_pin(1, 3), 3, 3, 0, 0},
    {fr2_pin(1, 4), 3, 3, 0, 0}, {fr2_pin(1, 5), 3, 3, 0, 0}, {fr2_pin(1, 6), 3, 1, TA0, 0}, {fr2_pin(1, 7), 3, 1, TA1, 0},
    // P2
    {fr2_pin(2, 0), 1, 1, TB0, 6}, {fr2_pin(2, 1), 1, 1, TB0, 0}, {fr2_pin(2, 2), 1, 1, TB0, 2}, {fr2_pin(2, 3), 1, 1, TA0, 0},
    {fr2_pin(2, 4), 1, 1, TA1, 0}, {fr2_pin(2, 5), 1, 1, TB0, 0}, {fr2_pin(2, 6), 1, 1, TB0, 1},
    {fr2_pin(2, 0), 2, 2, UA0, 0}, {fr2_pin(2, 1), 2, 2, UA0, 1}, {fr2_pin(2, 2), 2, 2, UB0, 2}, {fr2_pin(2, 3), 2, 2, UA1, 3},
    {fr2_pin(2, 4), 2, 2, UA1, 2}, {fr2_pin(2, 5), 2, 2, UA1, 0}, {fr2_pin(2, 6), 2, 2, UA1, 1},
    {fr2_pin(2, 0), 3, 4, 0, 2}, {fr2_pin(2, 3), 3, 3, 0, 0}, {fr2_pin(2, 4), 3, 3, 0, 0},
    // P3: A12-A15; TB0.3-TB0.6; SMCLK, COUT
    {fr2_pin(3, 0), 3, 3, 0, 0}, {fr2_pin(3, 1), 3, 3, 0, 0}, {fr2_pin(3, 2), 3, 3, 0, 0}, {fr2_pin(3, 3), 3, 3, 0, 0},
    {fr2_pin(3, 4), 1, 1, TB0, 3}, {fr2_pin(3, 5), 1, 1, TB0, 4}, {fr2_pin(3, 6), 1, 1, TB0, 5}, {fr2_pin(3, 7), 1, 1, TB0, 6},
    {fr2_pin(3, 4), 2, 4, 0, 1}, {fr2_pin(3, 5), 2, 10, 0, 0},
    // P4: A8-A11; TB0.5
    {fr2_pin(4, 0), 3, 3, 0, 0}, {fr2_pin(4, 1), 3, 3, 0, 0}, {fr2_pin(4, 2), 3, 3, 0, 0}, {fr2_pin(4, 3), 3, 3, 0, 0},
    {fr2_pin(4, 4), 1, 1, TB0, 5},
    // PJ: SMCLK, MCLK, ACLK; C6-C9; LFXIN/LFXOUT, HFXIN/HFXOUT
    {fr2_pin(PJ, 0), 1, 4, 0, 1}, {fr2_pin(PJ, 1), 1, 4, 0, 0}, {fr2_pin(PJ, 2), 1, 4, 0, 2},
    {fr2_pin(PJ, 0), 3, 3, 0, 0}, {fr2_pin(PJ, 1), 3, 3, 0, 0}, {fr2_pin(PJ, 2), 3, 3, 0, 0}, {fr2_pin(PJ, 3), 3, 3, 0, 0},
    {fr2_pin(PJ, 4), 1, 3, 0, 0}, {fr2_pin(PJ, 5), 1, 3, 0, 0}, {fr2_pin(PJ, 6), 1, 3, 0, 0}, {fr2_pin(PJ, 7), 1, 3, 0, 0},
};
// ADC12SHS: 1 TA0.1, 2 TB0.0, 3 TB0.1, 4 TA1.1, 5 TA2.1, 6 TA3.1
inline constexpr Fr2Disparo DISPAROS[] = {{TA0, 1, 1}, {TB0, 0, 2}, {TB0, 1, 3}, {TA1, 1, 4}, {TA2, 1, 5}, {TA3, 1, 6}};
// COUT a las CCI1B de TA0, TA1, TB0, TA2 y TA3
inline constexpr uint8_t CE_CCIB[] = {TA0, 1, TA1, 1, TB0, 1, TA2, 1, TA3, 1};
// El DMA: el CCR0 de cada Timer (en el orden de TIMERS), el RX de cada eUSCI
inline constexpr uint8_t DMA_CCR0[] = {1, 3, 7, 5, 6};
inline constexpr uint8_t DMA_USCI[] = {14, 16, 18};
} // namespace fr5969

inline constexpr ChipFr2 CHIP_FR5969 = [] {
    ChipFr2 c{
        &m430::msp430fr5969::DESC,
        M430Caps{"msp430fr5969", M430Arq::CPUXV2, m430::msp430fr5969::DESC.errata, true},
        "fr5969",
        11, {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0xFF}, 4,
        fr5969::TIMERS, 5, fr5969::USCIS, 3,
        true, 0,
        fr5969::FN, sizeof fr5969::FN / sizeof fr5969::FN[0],
        nullptr, 0, fr5969::DISPAROS, 6,
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        -1,
        0.700, 0.0, 0.0025,                           // [DS tabla 5-21: VSENSOR (a 0 °C), TCSENSOR]
        FR5_PJ, 0x30,                                 // LFXIN/LFXOUT en PJ.4/PJ.5 (PJSEL0)
        {fr2_pin(PJ, 3), fr2_pin(PJ, 2), fr2_pin(PJ, 1), fr2_pin(PJ, 0)},
        fr5969::UA0, fr2_pin(2, 0), fr2_pin(2, 1),
        0x8169, 0xFF,
        0, nullptr, 0, nullptr, 0,
    };
    c.adc_por_sel = true;
    c.fr5 = true;
    c.ref_base = 0x01B0; c.adc12b_base = 0x0800; c.compe_base = 0x08C0; c.rtcb_base = 0x04A0;
    c.aes_base = 0x09C0; c.dma_base = 0x0500; c.mpu_base = 0x05A0;
    const int8_t ad[16] = {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5),
                           fr2_pin(2, 3), fr2_pin(2, 4), fr2_pin(4, 0), fr2_pin(4, 1), fr2_pin(4, 2), fr2_pin(4, 3),
                           fr2_pin(3, 0), fr2_pin(3, 1), fr2_pin(3, 2), fr2_pin(3, 3)};
    for (unsigned i = 0; i < 16; ++i) c.adc5_pin[i] = ad[i];
    const int8_t ce[16] = {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5),
                           int8_t(fr2_pin(PJ, 0)), int8_t(fr2_pin(PJ, 1)), int8_t(fr2_pin(PJ, 2)), int8_t(fr2_pin(PJ, 3)),
                           fr2_pin(2, 3), fr2_pin(2, 4), fr2_pin(3, 0), fr2_pin(3, 1), fr2_pin(3, 2), fr2_pin(3, 3)};
    for (unsigned i = 0; i < 16; ++i) c.ce_pin[i] = ce[i];
    c.veref_mas = fr2_pin(1, 1); c.veref_menos = fr2_pin(1, 0);
    c.ce_ccib = fr5969::CE_CCIB; c.nce_ccib = 5;
    c.dma_tsel_ccr0 = fr5969::DMA_CCR0; c.dma_tsel_usci = fr5969::DMA_USCI; c.dma_tsel_adc = 26; c.dma_canales = 3;
    c.hfxt_puerto = FR5_PJ; c.hfxt_bits = 0xC0;
    return c;
}();

struct SocFR5969 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr5969::DESC;
    SocFR5969(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR5969, cab) {
        cristal_xt1 = true;                   // la LaunchPad: Y4, 32 768 Hz en LFXIN/LFXOUT [SLAU535]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR5969_SOC_FR5969_H
