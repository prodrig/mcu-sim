// =============================================================================
// soc_fr6989.h — EL MSP430FR6989 (M8 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de las FRAM (soc/msp430/fr2/soc_fr2.h, con `fr5`) [DS
// SLAS789D]
//
//   RAM 0x1C00-0x23FF (2 KB), FRAM 0x4400-0xFF7F y 0x10000-0x23FFF (128 KB);
//   P1-P10 (P1-P4 con interrupciones) y PJ (PZ-100); TA0, TA1 (3 CCR), TB0
//   (7), TA2, TA3 (2); eUSCI_A0, A1, B0 y B1; MPY32; CRC16; el ADC12_B; el
//   Comp_E; REF_A; RTC_C; AES256; DMA de 3 canales; la MPU con la IPE; LFXT
//   en PJ.4/PJ.5; el LCD_C (S0-S43, COM0-COM3 en P6.3-P6.6); el ESI (0x0D00),
//   declarado [de modelo: sus registros no hacen nada].
//
// Las funciones de todos los pines [DS tablas 6-20 a 6-42]; el ESI (0x0D00)
// y sus pines (ESICHx, ESICIx, ESITESTx, ESIC1OUT, ESIC2OUT).
// Los segmentos del LCD_C [tabla 4 de SLAU627A y la hoja de datos]: el pin
// de cada Sx.
// =============================================================================
#ifndef STM32_SOC_MSP430_FR6989_SOC_FR6989_H
#define STM32_SOC_MSP430_FR6989_SOC_FR6989_H

#include "../fr2/soc_fr2.h"
#include "../fr5969/soc_fr5969.h"
#include "../desc/msp430fr6989.h"

namespace stm32 {

namespace fr6989 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_a3", 0x0340, 3, "TIMER0_A0", "TIMER0_A1"},
    {"timer1_a3", 0x0380, 3, "TIMER1_A0", "TIMER1_A1"},
    {"timer0_b7", 0x03C0, 7, "TIMER0_B0", "TIMER0_B1", true},
    {"timer2_a2", 0x0400, 2, "TIMER2_A0", "TIMER2_A1"},
    {"timer3_a2", 0x0440, 2, "TIMER3_A0", "TIMER3_A1"},
};
enum : uint8_t { TA0 = 0, TA1 = 1, TB0 = 2, TA2 = 3, TA3 = 4 };
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x05C0, false, "USCI_A0"}, {"eusci_a1", 0x05E0, false, "USCI_A1"},
    {"eusci_b0", 0x0640, true, "USCI_B0"},  {"eusci_b1", 0x0680, true, "USCI_B1"},
};
enum : uint8_t { UA0 = 0, UA1 = 1, UB0 = 2, UB1 = 3 };
// Todas las funciones [DS SLAS789D tablas 6-20 a 6-42], por PxSEL y PxDIR
// (los tipos, los del FR5969). Los segmentos (LCDSx) mandan sobre PxSEL y los
// lleva el LCD_C; COM0-COM3 (P6.3-P6.6), también, con el LCD en marcha [de
// modelo: la demo de fábrica de TI no pone P6SEL y el vidrio se ve]. P6.0-P6.2
// (R23, R13/LCDREF, R03) y los ESICHx/ESICIx, analógicos con 11.
inline constexpr Fr2Fn FN[] = {
    // P1
    {fr2_pin(1, 0), 1, 1, TA0, 1}, fn_ent(fr2_pin(1, 0), 2, 12, 0, 0), fn_sal(fr2_pin(1, 0), 2, 13, 0, 0), {fr2_pin(1, 0), 3, 3, 0, 0},
    {fr2_pin(1, 1), 1, 1, TA0, 2}, fn_ent(fr2_pin(1, 1), 2, 5, TA1, 0), fn_sal(fr2_pin(1, 1), 2, 10, 0, 0), {fr2_pin(1, 1), 3, 3, 0, 0},
    {fr2_pin(1, 2), 1, 1, TA1, 1}, fn_ent(fr2_pin(1, 2), 2, 5, TA0, 0), fn_sal(fr2_pin(1, 2), 2, 10, 0, 0), {fr2_pin(1, 2), 3, 3, 0, 0},
    {fr2_pin(1, 3), 1, 1, TA1, 2}, fn_sal(fr2_pin(1, 3), 2, 16, 0, 4), {fr2_pin(1, 3), 3, 3, 0, 0},
    {fr2_pin(1, 4), 1, 2, UB0, 2}, {fr2_pin(1, 4), 2, 2, UA0, 3}, {fr2_pin(1, 4), 3, 1, TA1, 0},
    {fr2_pin(1, 5), 1, 2, UB0, 3}, {fr2_pin(1, 5), 2, 2, UA0, 2}, {fr2_pin(1, 5), 3, 1, TA0, 0},
    {fr2_pin(1, 6), 1, 2, UB0, 0}, {fr2_pin(1, 6), 3, 1, TA0, 1},
    {fr2_pin(1, 7), 1, 2, UB0, 1}, {fr2_pin(1, 7), 3, 1, TA0, 2},
    // P2
    {fr2_pin(2, 0), 1, 2, UA0, 0}, {fr2_pin(2, 0), 2, 11, TB0, 6}, fn_ent(fr2_pin(2, 0), 3, 5, TB0, 0),
    {fr2_pin(2, 1), 1, 2, UA0, 1}, {fr2_pin(2, 1), 2, 11, TB0, 5}, fn_ent(fr2_pin(2, 1), 3, 12, 0, 0),
    {fr2_pin(2, 2), 1, 2, UA0, 2}, {fr2_pin(2, 2), 2, 11, TB0, 4}, fn_sal(fr2_pin(2, 2), 3, 13, 0, 0),
    {fr2_pin(2, 3), 1, 2, UA0, 3}, fn_ent(fr2_pin(2, 3), 2, 9, TB0, 0),
    {fr2_pin(2, 4), 1, 1, TB0, 3}, {fr2_pin(2, 5), 1, 1, TB0, 4},
    {fr2_pin(2, 6), 1, 1, TB0, 5}, fn_sal(fr2_pin(2, 6), 2, 16, 0, 5),
    {fr2_pin(2, 7), 1, 1, TB0, 6}, fn_sal(fr2_pin(2, 7), 2, 16, 0, 6),
    // P3
    {fr2_pin(3, 0), 1, 2, UB1, 2}, {fr2_pin(3, 1), 1, 2, UB1, 0}, {fr2_pin(3, 2), 1, 2, UB1, 1},
    {fr2_pin(3, 3), 2, 1, TA1, 1}, fn_ent(fr2_pin(3, 3), 3, 5, TB0, 0),
    {fr2_pin(3, 4), 1, 2, UA1, 0}, {fr2_pin(3, 4), 2, 1, TB0, 0},
    {fr2_pin(3, 5), 1, 2, UA1, 1}, {fr2_pin(3, 5), 2, 1, TB0, 1},
    {fr2_pin(3, 6), 1, 2, UA1, 2}, {fr2_pin(3, 6), 2, 1, TB0, 2},
    {fr2_pin(3, 7), 1, 2, UA1, 3}, {fr2_pin(3, 7), 2, 11, TB0, 3},
    // P4
    {fr2_pin(4, 0), 2, 2, UB1, 0}, fn_sal(fr2_pin(4, 0), 3, 4, 0, 0),
    {fr2_pin(4, 1), 2, 2, UB1, 1}, fn_sal(fr2_pin(4, 1), 3, 4, 0, 2),
    {fr2_pin(4, 2), 1, 2, UA0, 0}, {fr2_pin(4, 2), 2, 2, UB1, 2},
    {fr2_pin(4, 3), 1, 2, UA0, 1}, {fr2_pin(4, 3), 2, 2, UB1, 3},
    {fr2_pin(4, 4), 2, 2, UB1, 3}, fn_ent(fr2_pin(4, 4), 3, 5, TA1, 0),
    {fr2_pin(4, 5), 2, 2, UB1, 2}, {fr2_pin(4, 5), 3, 1, TA1, 0},
    {fr2_pin(4, 6), 2, 2, UB1, 0}, {fr2_pin(4, 6), 3, 1, TA1, 1},
    {fr2_pin(4, 7), 2, 2, UB1, 1}, {fr2_pin(4, 7), 3, 1, TA1, 2},
    // P5
    {fr2_pin(5, 0), 1, 1, TA1, 1}, fn_sal(fr2_pin(5, 0), 3, 4, 0, 0),
    {fr2_pin(5, 1), 1, 1, TA1, 2},
    {fr2_pin(5, 2), 1, 11, TA1, 0}, fn_ent(fr2_pin(5, 2), 2, 5, TA1, 0), fn_sal(fr2_pin(5, 2), 3, 4, 0, 2),
    {fr2_pin(5, 3), 2, 2, UB1, 3},
    {fr2_pin(5, 4), 1, 2, UA1, 0}, {fr2_pin(5, 5), 1, 2, UA1, 1}, {fr2_pin(5, 6), 1, 2, UA1, 2},
    {fr2_pin(5, 7), 1, 2, UA1, 3}, fn_ent(fr2_pin(5, 7), 3, 5, TB0, 0),
    // P6: R23, R13/LCDREF, R03; COUT; TB0.0-TB0.2; TA0CLK
    {fr2_pin(6, 0), 3, 3, 0, 0}, {fr2_pin(6, 1), 3, 3, 0, 0},
    fn_sal(fr2_pin(6, 2), 1, 10, 0, 0), {fr2_pin(6, 2), 3, 3, 0, 0},
    {fr2_pin(6, 4), 1, 11, TB0, 0}, {fr2_pin(6, 5), 1, 1, TB0, 1}, {fr2_pin(6, 6), 1, 1, TB0, 2},
    fn_ent(fr2_pin(6, 7), 1, 5, TA0, 0),
    // P7
    fn_ent(fr2_pin(7, 0), 1, 5, TA0, 0),
    {fr2_pin(7, 1), 1, 11, TA0, 0}, fn_sal(fr2_pin(7, 1), 3, 4, 0, 2),
    {fr2_pin(7, 2), 1, 1, TA0, 1}, {fr2_pin(7, 3), 1, 1, TA0, 2},
    fn_sal(fr2_pin(7, 4), 3, 4, 0, 1),
    {fr2_pin(7, 5), 1, 1, TA0, 2}, {fr2_pin(7, 6), 1, 1, TA0, 1},
    {fr2_pin(7, 7), 2, 1, TA1, 2}, fn_ent(fr2_pin(7, 7), 3, 9, TB0, 0),
    // P8: RTCCLK, DMAE0, MCLK; A7-A4/C7-C4
    fn_sal(fr2_pin(8, 0), 3, 13, 0, 0), fn_ent(fr2_pin(8, 1), 3, 12, 0, 0), fn_sal(fr2_pin(8, 3), 3, 4, 0, 0),
    {fr2_pin(8, 4), 3, 3, 0, 0}, {fr2_pin(8, 5), 3, 3, 0, 0}, {fr2_pin(8, 6), 3, 3, 0, 0}, {fr2_pin(8, 7), 3, 3, 0, 0},
    // P9: ESITEST0-3 con 10; ESICH0-3/A8-A11/C8-C11 y ESICI0-3/A12-A15/C12-C15 con 11
    fn_sal(fr2_pin(9, 0), 2, 16, 0, 0), fn_sal(fr2_pin(9, 1), 2, 16, 0, 1), fn_sal(fr2_pin(9, 2), 2, 16, 0, 2), fn_sal(fr2_pin(9, 3), 2, 16, 0, 3),
    {fr2_pin(9, 0), 3, 3, 0, 0}, {fr2_pin(9, 1), 3, 3, 0, 0}, {fr2_pin(9, 2), 3, 3, 0, 0}, {fr2_pin(9, 3), 3, 3, 0, 0},
    {fr2_pin(9, 4), 3, 3, 0, 0}, {fr2_pin(9, 5), 3, 3, 0, 0}, {fr2_pin(9, 6), 3, 3, 0, 0}, {fr2_pin(9, 7), 3, 3, 0, 0},
    // P10
    fn_sal(fr2_pin(10, 0), 3, 4, 0, 1),
    {fr2_pin(10, 1), 1, 11, TA0, 0},
    {fr2_pin(10, 2), 1, 11, TA1, 0}, fn_sal(fr2_pin(10, 2), 3, 4, 0, 1),
    // PJ.0-PJ.3: TB0OUTH/SMCLK, MCLK, ACLK, COUT; con 10, los bits del SR
    fn_ent(fr2_pin(PJ, 0), 1, 9, TB0, 0), fn_sal(fr2_pin(PJ, 0), 1, 4, 0, 1), fn_sal(fr2_pin(PJ, 0), 2, 14, 0, 0x80),
    fn_sal(fr2_pin(PJ, 1), 1, 4, 0, 0), fn_sal(fr2_pin(PJ, 1), 2, 14, 0, 0x40),
    fn_sal(fr2_pin(PJ, 2), 1, 4, 0, 2), fn_sal(fr2_pin(PJ, 2), 2, 14, 0, 0x20),
    fn_sal(fr2_pin(PJ, 3), 1, 10, 0, 0), fn_sal(fr2_pin(PJ, 3), 2, 14, 0, 0x10),
};
inline constexpr Fr2Outh OUTH[] = {{TB0, -1, 0xFF}};
// ADC12SHS: los del FR5969 [DS tabla 6-17]
inline constexpr Fr2Disparo DISPAROS[] = {{TA0, 1, 1}, {TB0, 0, 2}, {TB0, 1, 3}, {TA1, 1, 4}, {TA2, 1, 5}, {TA3, 1, 6}};
// El pin de cada Sx (L0-L43) y de COM0-COM3 (L60-L63); 0xFF, sin pin
inline constexpr auto LCD_L = [] {
    std::array<uint8_t, 64> t{};
    for (auto& x : t) x = 0xFF;
    const uint8_t s[44] = {
        fr2_pin(1, 5), fr2_pin(1, 4), fr2_pin(4, 1), fr2_pin(4, 0), fr2_pin(10, 0), fr2_pin(4, 7), fr2_pin(4, 6), fr2_pin(4, 5),
        fr2_pin(4, 4), fr2_pin(5, 7), fr2_pin(5, 6), fr2_pin(5, 5), fr2_pin(5, 4), fr2_pin(7, 4), fr2_pin(7, 3), fr2_pin(7, 2),
        fr2_pin(7, 1), fr2_pin(7, 0), fr2_pin(8, 3), fr2_pin(8, 2), fr2_pin(8, 1), fr2_pin(8, 0), fr2_pin(3, 7), fr2_pin(3, 6),
        fr2_pin(3, 5), fr2_pin(3, 4), fr2_pin(3, 3), fr2_pin(7, 7), fr2_pin(10, 1), fr2_pin(7, 6), fr2_pin(7, 5), fr2_pin(6, 7),
        fr2_pin(3, 2), fr2_pin(3, 1), fr2_pin(3, 0), fr2_pin(5, 3), fr2_pin(5, 2), fr2_pin(5, 1), fr2_pin(5, 0), fr2_pin(10, 2),
        fr2_pin(2, 7), fr2_pin(2, 6), fr2_pin(2, 5), fr2_pin(2, 4)};
    for (unsigned i = 0; i < 44; ++i) t[i] = s[i];
    for (unsigned c = 0; c < 4; ++c) t[60 + c] = fr2_pin(6, 3 + c);
    return t;
}();
inline constexpr uint8_t CE_CCIB[] = {TA0, 1, TA1, 1, TB0, 1, TA2, 1, TA3, 1};
inline constexpr uint8_t DMA_CCR0[] = {1, 3, 7, 5, 6};
inline constexpr uint8_t DMA_USCI[] = {14, 16, 18, 20};
} // namespace fr6989

inline constexpr ChipFr2 CHIP_FR6989 = [] {
    ChipFr2 c = CHIP_FR5969;
    c.desc = &m430::msp430fr6989::DESC;
    c.caps = M430Caps{"msp430fr6989", M430Arq::CPUXV2, m430::msp430fr6989::DESC.errata, true};
    c.etiqueta = "fr6989";
    c.npuertos = 11;
    const uint8_t b[11] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0xFF};
    for (unsigned i = 0; i < 11; ++i) c.bits[i] = b[i];
    c.npuertos_irq = 4;
    c.timers = fr6989::TIMERS; c.ntimers = 5;
    c.uscis = fr6989::USCIS; c.nuscis = 4;
    c.fn = fr6989::FN; c.nfn = sizeof fr6989::FN / sizeof fr6989::FN[0];
    c.bcl_usci = fr6989::UA1; c.bcl_tx = fr2_pin(3, 4); c.bcl_rx = fr2_pin(3, 5);
    c.tlv_id = 0x81A8;
    c.rtcb_base = 0; c.rtcc_base = 0x04A0;
    c.lcd_base = 0x0A00; c.lcd_l = fr6989::LCD_L.data(); c.nlcd = 64; c.lcd_c = true;
    // A0-A3/C0-C3 en P1.0-P1.3, A4-A7/C4-C7 en P8.7-P8.4, A8-A11/C8-C11 en
    // P9.0-P9.3 (ESICH0-3), A12-A15/C12-C15 en P9.4-P9.7 (ESICI0-3) [DS tablas
    // 6-20, 6-36 a 6-38]
    {
        const int8_t ad[16] = {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(8, 7), fr2_pin(8, 6),
                               fr2_pin(8, 5), fr2_pin(8, 4), fr2_pin(9, 0), fr2_pin(9, 1), fr2_pin(9, 2), fr2_pin(9, 3),
                               fr2_pin(9, 4), fr2_pin(9, 5), fr2_pin(9, 6), fr2_pin(9, 7)};
        for (unsigned i = 0; i < 16; ++i) { c.adc5_pin[i] = ad[i]; c.ce_pin[i] = ad[i]; }
    }
    c.ce_ccib = fr6989::CE_CCIB; c.nce_ccib = 5;
    c.dma_tsel_ccr0 = fr6989::DMA_CCR0; c.dma_tsel_usci = fr6989::DMA_USCI;
    c.disparos = fr6989::DISPAROS; c.ndisparos = 6;
    c.outh = fr6989::OUTH; c.nouth = 1;
    // Un sector de RAM [DS SLAS789D §6.8]
    c.nram_sectores = 1;
    c.ram_sector[0][0] = 0x1C00; c.ram_sector[0][1] = 0x23FF;
    // Las erratas que se modelan [SLAZ517AG] (TB25, con la de las FR2)
    c.errata5 = E5_ADC42 | E5_ADC43 | E5_ADC66 | E5_ADC69 | E5_ADC71 | E5_AES1 | E5_COMP10 | E5_REF9 | E5_RTC12 | E5_TA22 | E5_WDG5;
    c.errata_tb25 = true;
    return c;
}();

struct SocFR6989 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr6989::DESC;
    SocFR6989(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR6989, cab) {
        cristal_xt1 = true;                   // la LaunchPad: Y4, 32 768 Hz [SLAU627A]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR6989_SOC_FR6989_H
