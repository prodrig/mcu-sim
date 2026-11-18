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
// De las funciones de los pines, las que usan la LaunchPad y sus programas
// [DS tablas 6-64 a 6-78]: la UCA1 del backchannel (P3.4 TXD, P3.5 RXD), la
// UCA0 (P2.0, P2.1), la UCB0 (P1.4 CLK, P1.6 SIMO, P1.7 SOMI), TA0.1 y TA0.2
// en P1.6/P1.7; las entradas analógicas A0-A5 en P1.0-P1.5 y A12-A15 en
// P8.4-P8.7; el LFXT; JTAG en PJ.0-PJ.3. Lo demás, sin declarar.
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
inline constexpr Fr2Fn FN[] = {
    {fr2_pin(3, 4), 1, 2, UA1, 0}, {fr2_pin(3, 5), 1, 2, UA1, 1}, {fr2_pin(3, 6), 1, 2, UA1, 2}, {fr2_pin(3, 7), 1, 2, UA1, 3},
    {fr2_pin(2, 0), 1, 2, UA0, 0}, {fr2_pin(2, 1), 1, 2, UA0, 1},
    {fr2_pin(1, 4), 1, 2, UB0, 2}, {fr2_pin(1, 6), 1, 2, UB0, 0}, {fr2_pin(1, 7), 1, 2, UB0, 1},
    {fr2_pin(1, 6), 2, 1, TA0, 1}, {fr2_pin(1, 7), 2, 1, TA0, 2},
    {fr2_pin(1, 0), 3, 3, 0, 0}, {fr2_pin(1, 1), 3, 3, 0, 0}, {fr2_pin(1, 2), 3, 3, 0, 0}, {fr2_pin(1, 3), 3, 3, 0, 0},
    {fr2_pin(1, 4), 3, 3, 0, 0}, {fr2_pin(1, 5), 3, 3, 0, 0},
    {fr2_pin(8, 4), 3, 3, 0, 0}, {fr2_pin(8, 5), 3, 3, 0, 0}, {fr2_pin(8, 6), 3, 3, 0, 0}, {fr2_pin(8, 7), 3, 3, 0, 0},
    {fr2_pin(PJ, 0), 1, 4, 0, 1}, {fr2_pin(PJ, 1), 1, 4, 0, 0}, {fr2_pin(PJ, 2), 1, 4, 0, 2},
    {fr2_pin(PJ, 4), 1, 3, 0, 0}, {fr2_pin(PJ, 5), 1, 3, 0, 0}, {fr2_pin(PJ, 6), 1, 3, 0, 0}, {fr2_pin(PJ, 7), 1, 3, 0, 0},
};
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
    for (unsigned i = 0; i < 16; ++i) c.ce_pin[i] = -1;
    const int8_t ad[16] = {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5), -1, -1,
                           -1, -1, -1, -1, fr2_pin(8, 7), fr2_pin(8, 6), fr2_pin(8, 5), fr2_pin(8, 4)};
    for (unsigned i = 0; i < 16; ++i) c.adc5_pin[i] = ad[i];
    c.ce_ccib = fr6989::CE_CCIB; c.nce_ccib = 5;
    c.dma_tsel_ccr0 = fr6989::DMA_CCR0; c.dma_tsel_usci = fr6989::DMA_USCI;
    c.disparos = nullptr; c.ndisparos = 0;
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
