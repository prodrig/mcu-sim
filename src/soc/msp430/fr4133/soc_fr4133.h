// =============================================================================
// soc_fr4133.h — EL MSP430FR4133 (M7 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de la familia (soc/msp430/fr2/soc_fr2.h) [DS SLAS865F]
//
//   RAM 0x2000-0x27FF (2 KB), FRAM 0xC400-0xFFFF (15 KB), información
//   0x1800-0x19FF; P1-P8 (LQFP-64: P8.0-P8.3), P1 y P2 con interrupciones y
//   despertar de LPMx.5; un solo PxSEL0 (una función por pin); TA0 y TA1 (3
//   CCR) en 0x300 y 0x340; eUSCI_A0 (P1.0-P1.3) y B0 (P5.0-P5.3); sin MPY; el
//   ADC de 10 bits con A0-A7 en P1 y A8/A9 en P8.0/P8.1 [tabla 9-12]; el RTC
//   en 0x3C0; XT1 en P4.1/P4.2; JTAG de 4 hilos en P1.4-P1.7; y el LCD_E en
//   0x600 con 40 pines: L0-L7 en P7, L8-L15 en P3, L16-L23 en P6, L24-L31 en
//   P2 y L32-L39 en P5, y LCDCAP0/1, R33, R23 y R13 en P4.3-P4.7 (con LCDPCTL
//   de SYSCFG2) [tablas 9-14 a 9-25].
//
// Las conexiones de los Timer [tablas 9-10 y 9-11]: las salidas de TA0 a las
// capturas B de TA1; TA1.1B y TA1.2B disparan el ADC (ADCSHS = 2 y 3, tabla
// 9-13). [de modelo, no está] El RTC a CCI1B de TA0, la modulación IR de
// SYSCFG1 (TA0/TA1 sobre UCA0TXD) y el CapTIvate IO (CAPTIO0): declarados.
//
// El sensor: VSENSOR 1,013 V a 0 °C y 3,35 mV/°C [DS §8.12.7: «Temperature
// Sensor»]. La TLV, como la de la FR2433 con el ID 0x81F0 y la CRC de 0x1A04 a
// 0x1A77 [tablas 9-26 y 9-27].
//
// En la LaunchPad (MSP-EXP430FR4133) hay un cristal de 32 768 Hz (Y1) en
// P4.1/P4.2 y el vidrio FH-1138P (parts/lcd_fh1138p.h) en L0-L39.
// =============================================================================
#ifndef STM32_SOC_MSP430_FR4133_SOC_FR4133_H
#define STM32_SOC_MSP430_FR4133_SOC_FR4133_H

#include "../fr2/soc_fr2.h"
#include "../desc/msp430fr4133.h"

namespace stm32 {

namespace fr4133 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_a3", 0x0300, 3, "TIMER0_A0", "TIMER0_A1"},
    {"timer1_a3", 0x0340, 3, "TIMER1_A0", "TIMER1_A1"},
};
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x0500, false, "USCI_A0"},
    {"eusci_b0", 0x0540, true, "USCI_B0"},
};
enum : uint8_t { UA0 = 0, UB0 = 1 };
inline constexpr Fr2Fn FN[] = {
    // P1: UCA0TXD/SIMO, RXD/SOMI, CLK, STE; MCLK; TA0CLK; TA0.2; TA0.1
    {fr2_pin(1, 0), 1, 2, UA0, 0}, {fr2_pin(1, 1), 1, 2, UA0, 1}, {fr2_pin(1, 2), 1, 2, UA0, 2}, {fr2_pin(1, 3), 1, 2, UA0, 3},
    {fr2_pin(1, 4), 1, 4, 0, 0}, {fr2_pin(1, 5), 1, 5, 0, 0}, {fr2_pin(1, 6), 1, 1, 0, 2}, {fr2_pin(1, 7), 1, 1, 0, 1},
    // P4: TA1.1; XIN, XOUT
    {fr2_pin(4, 0), 1, 1, 1, 1}, {fr2_pin(4, 1), 1, 3, 0, 0}, {fr2_pin(4, 2), 1, 3, 0, 0},
    // P5: UCB0STE, CLK, SIMO/SDA, SOMI/SCL
    {fr2_pin(5, 0), 1, 2, UB0, 3}, {fr2_pin(5, 1), 1, 2, UB0, 2}, {fr2_pin(5, 2), 1, 2, UB0, 0}, {fr2_pin(5, 3), 1, 2, UB0, 1},
    // P8: SMCLK, ACLK, TA1CLK, TA1.2
    {fr2_pin(8, 0), 1, 4, 0, 1}, {fr2_pin(8, 1), 1, 4, 0, 2}, {fr2_pin(8, 2), 1, 5, 1, 0}, {fr2_pin(8, 3), 1, 1, 1, 2},
};
inline constexpr Fr2Enlace ENLACES[] = {{0, 1, 3}};
inline constexpr Fr2Disparo DISPAROS[] = {{1, 1, 2}, {1, 2, 3}};
// L0..L39 [DS tabla 7-1]
inline constexpr uint8_t LCD_L[40] = {
    fr2_pin(7, 0), fr2_pin(7, 1), fr2_pin(7, 2), fr2_pin(7, 3), fr2_pin(7, 4), fr2_pin(7, 5), fr2_pin(7, 6), fr2_pin(7, 7),
    fr2_pin(3, 0), fr2_pin(3, 1), fr2_pin(3, 2), fr2_pin(3, 3), fr2_pin(3, 4), fr2_pin(3, 5), fr2_pin(3, 6), fr2_pin(3, 7),
    fr2_pin(6, 0), fr2_pin(6, 1), fr2_pin(6, 2), fr2_pin(6, 3), fr2_pin(6, 4), fr2_pin(6, 5), fr2_pin(6, 6), fr2_pin(6, 7),
    fr2_pin(2, 0), fr2_pin(2, 1), fr2_pin(2, 2), fr2_pin(2, 3), fr2_pin(2, 4), fr2_pin(2, 5), fr2_pin(2, 6), fr2_pin(2, 7),
    fr2_pin(5, 0), fr2_pin(5, 1), fr2_pin(5, 2), fr2_pin(5, 3), fr2_pin(5, 4), fr2_pin(5, 5), fr2_pin(5, 6), fr2_pin(5, 7),
};
// LCDCAP0, LCDCAP1, R33, R23, R13
inline constexpr uint8_t LCD_POT[5] = {fr2_pin(4, 3), fr2_pin(4, 4), fr2_pin(4, 5), fr2_pin(4, 6), fr2_pin(4, 7)};
} // namespace fr4133

inline constexpr ChipFr2 CHIP_FR4133{
    &m430::msp430fr4133::DESC,
    M430Caps{"msp430fr4133", M430Arq::CPUXV2, m430::msp430fr4133::DESC.errata, true},
    "fr4133",
    8, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F}, 2,
    fr4133::TIMERS, 2, fr4133::USCIS, 2,
    false, 0x03C0,
    fr4133::FN, sizeof fr4133::FN / sizeof fr4133::FN[0],
    fr4133::ENLACES, 1, fr4133::DISPAROS, 2,
    {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7),
     fr2_pin(8, 0), fr2_pin(8, 1), -1, -1, -1, -1, -1, -1},
    fr2_pin(1, 1),                                    // VeREF+ en P1.1
    1.013, 0.0, 0.00335,                              // [DS: VSENSOR a 0 °C, TCSENSOR]
    3, 0x06,                                          // XIN/XOUT en P4.1/P4.2
    {fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7)},
    fr4133::UA0, fr2_pin(1, 0), fr2_pin(1, 1),
    0x81F0, 0x77,
    0x0600, fr4133::LCD_L, 40, fr4133::LCD_POT, 5,
    true,                                             // SYSCFG0 sin contraseña [UG FR2 figura 1-35]
    true,                                             // SYS23 [SLAZ550AA]
};

struct SocFR4133 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr4133::DESC;
    SocFR4133(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR4133, cab) {
        cristal_xt1 = true;                   // la LaunchPad: Y1, 32 768 Hz [SLAU595B, esquema]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR4133_SOC_FR4133_H
