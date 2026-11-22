// =============================================================================
// soc_fr2433.h — EL MSP430FR2433 (M6 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de la familia (soc/msp430/fr2/soc_fr2.h) [DS SLASE59F]
//
//   RAM 0x2000-0x2FFF (4 KB), FRAM 0xC400-0xFFFF (15 KB), información
//   0x1800-0x19FF; P1 y P2 (con interrupciones y despertar de LPMx.5) y
//   P3.0-P3.2 (VQFN-24); TA0, TA1 (3 CCR), TA2, TA3 (2 CCR); eUSCI_A0, A1 y
//   B0; MPY32; el ADC de 10 bits con A0-A7 en P1; el RTC en 0x300; XT1 en
//   P2.0/P2.1; JTAG de 4 hilos en P1.4-P1.7; el backchannel de la LaunchPad,
//   la UCA0 en P1.4 (TXD) y P1.5 (RXD).
//
// Las funciones de los pines [DS tablas 6-1 y 6-38 a 6-44], las conexiones
// de los Timer [tablas 6-11 a 6-14: las salidas de TA0 a las capturas B de
// TA1, y las de TA2 a las de TA3] y el disparo del ADC [tabla 6-16: TA1.1B
// con ADCSHS = 2].
// =============================================================================
#ifndef STM32_SOC_MSP430_FR2433_SOC_FR2433_H
#define STM32_SOC_MSP430_FR2433_SOC_FR2433_H

#include "../fr2/soc_fr2.h"
#include "../desc/msp430fr2433.h"

namespace stm32 {

namespace fr2433 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_a3", 0x0380, 3, "TIMER0_A0", "TIMER0_A1"},
    {"timer1_a3", 0x03C0, 3, "TIMER1_A0", "TIMER1_A1"},
    {"timer2_a2", 0x0400, 2, "TIMER2_A0", "TIMER2_A1"},
    {"timer3_a2", 0x0440, 2, "TIMER3_A0", "TIMER3_A1"},
};
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x0500, false, "USCI_A0"},
    {"eusci_a1", 0x0520, false, "USCI_A1"},
    {"eusci_b0", 0x0540, true, "USCI_B0"},
};
enum : uint8_t { UA0 = 0, UA1 = 1, UB0 = 2 };
inline constexpr Fr2Fn FN[] = {
    // P1, PxSEL = 01: la UCB0 en P1.0-P1.3 (STE, CLK, SIMO/SDA, SOMI/SCL) y la UCA0 en P1.4-P1.7
    {fr2_pin(1, 0), 1, 2, UB0, 3}, {fr2_pin(1, 1), 1, 2, UB0, 2}, {fr2_pin(1, 2), 1, 2, UB0, 0}, {fr2_pin(1, 3), 1, 2, UB0, 1},
    {fr2_pin(1, 4), 1, 2, UA0, 0}, {fr2_pin(1, 5), 1, 2, UA0, 1}, {fr2_pin(1, 6), 1, 2, UA0, 2}, {fr2_pin(1, 7), 1, 2, UA0, 3},
    // P1, PxSEL = 10: TA0CLK, TA0.1, TA0.2, MCLK, TA1.2, TA1.1, TA1CLK, SMCLK
    {fr2_pin(1, 0), 2, 5, 0, 0}, {fr2_pin(1, 1), 2, 1, 0, 1}, {fr2_pin(1, 2), 2, 1, 0, 2}, {fr2_pin(1, 3), 2, 4, 0, 0},
    {fr2_pin(1, 4), 2, 1, 1, 2}, {fr2_pin(1, 5), 2, 1, 1, 1}, {fr2_pin(1, 6), 2, 5, 1, 0}, {fr2_pin(1, 7), 2, 4, 0, 1},
    // P2: XOUT, XIN; ACLK; la UCA1 (CLK, RXD, TXD) en P2.4-P2.6 y su STE en P3.1
    {fr2_pin(2, 0), 1, 3, 0, 0}, {fr2_pin(2, 1), 1, 3, 0, 0}, {fr2_pin(2, 2), 2, 4, 0, 2},
    {fr2_pin(2, 4), 1, 2, UA1, 2}, {fr2_pin(2, 5), 1, 2, UA1, 1}, {fr2_pin(2, 6), 1, 2, UA1, 0},
    {fr2_pin(3, 1), 1, 2, UA1, 3},
};
inline constexpr Fr2Enlace ENLACES[] = {{0, 1, 3}, {2, 3, 2}};
inline constexpr Fr2Disparo DISPAROS[] = {{1, 1, 2}};
} // namespace fr2433

inline constexpr ChipFr2 CHIP_FR2433{
    &m430::msp430fr2433::DESC,
    M430Caps{"msp430fr2433", M430Arq::CPUXV2, m430::msp430fr2433::DESC.errata, true},
    "fr2433",
    3, {0xFF, 0xFF, 0x07}, 2,
    fr2433::TIMERS, 4, fr2433::USCIS, 3,
    true, 0x0300,
    fr2433::FN, sizeof fr2433::FN / sizeof fr2433::FN[0],
    fr2433::ENLACES, 2, fr2433::DISPAROS, 1,
    {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1},
    fr2_pin(1, 0),                                    // VeREF+ en P1.0
    0.913, 30.0, 0.00335,                             // [DS: VSENSOR a 30 °C, TCSENSOR]
    1, 0x03,                                          // XIN/XOUT en P2.1/P2.0
    {fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7)},
    fr2433::UA0, fr2_pin(1, 4), fr2_pin(1, 5),
    0x8240, 0xF5,                                     // [DS tabla 6-22: la CRC de 0x1A04 a 0x1AF5]
    0, nullptr, 0, nullptr, 0,
};

struct SocFR2433 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr2433::DESC;
    SocFR2433(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR2433, cab) {
        cristal_xt1 = false;                  // la LaunchPad: Q1 no está conectado (R4/R5) [SLAU739 §2.5]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR2433_SOC_FR2433_H
