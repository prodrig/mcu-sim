// =============================================================================
// soc_fr2476.h — EL MSP430FR2476 (M7 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de la familia (soc/msp430/fr2/soc_fr2.h) [DS SLASEO7C]
//
//   RAM 0x2000-0x3FFF (8 KB); FRAM 0x8000-0x17FFF (64 KB: la de arriba de
//   0x10000 es la `principal2` del descriptor, que el SoC ya lleva),
//   información 0x1800-0x19FF; P1-P6 (P6.0-P6.2), TODOS con interrupción
//   (PORT1-PORT6); Timer0_A3 a Timer3_A3 (0x380, 0x3C0, 0x400, 0x440) y
//   Timer0_B7 (0x480); eUSCI_A0 (0x500), A1 (0x520), B0 (0x540) y B1
//   (0x580); MPY32; el ADC de 12 bits (A0-A7 en P1, A8/A9 en P4.3/P4.4,
//   A10/A11 en P5.3/P5.4, analógicos con PxSEL = 11: no hay ADCPCTL); la
//   referencia compartida mejorada (REFVSEL); el eCOMP0 (0x8E0) con la de
//   1,2 V; el RTC en 0x300; XT1 en P2.1/P2.0 con PxSEL = 01; JTAG de 4 hilos
//   en P1.4-P1.7. Sin ICC, sin SAC y sin FRWPOA [UG FR2 tabla 1-31, nota 3].
//
// Las reasignaciones, cinco [DS tablas 9-11 y 9-16]: USCIB0RMP (SYSCFG2 bit
// 11), USCIA0RMP, TA2RMP, TA3RMP y USCIB1RMP (SYSCFG3 bits 0, 2, 3 y 4). Cada
// función reasignable lleva su bit (`Fr2Fn::rmp`).
//
// Las conexiones de los Timer [tablas 9-12 a 9-15]: las salidas de TA0 a las
// capturas B de TA1, las de TA2 a las de TA3 y las de TA3 a las de TB0
// (CCR0-CCR2); TA1.1B dispara el ADC (ADCSHS = 2) y el eCOMP0 también
// (ADCSHS = 3) [tabla 9-20]. [de modelo, no está] el RTC a TA0 y TA2 CCI1B,
// el VLO a CCI0B, TB0OUTH (tabla 9-17), el IR de SYSCFG1 y la BAKMEM.
//
// eCOMP0 [tabla 9-21]: CPPSEL/CPNSEL 000 P1.1, 001 P2.2, 010 la de 1,2 V, 011
// P5.7, 100 P6.0, 110 su DAC. COUT en P3.4 (PxSEL = 10) y al ADC; no a
// TB0.CCI1B (la errata COMP12 [SLAZ726B]: la captura B de TB0 CCR1 es la
// salida de TA3 CCR1).
//
// El sensor: VSENSOR 788 mV a 30 °C y 2,5 mV/°C [DS §8.12.5.1]. La TLV,
// el ID 0x832A y la CRC de 0x1A04 a 0x1AF7 [tablas 9-29 y
// 9-30].
//
// La LaunchPad (LP-MSP430FR2476) no lleva el cristal conectado (R2 y R3 sin
// montar) [SLAU802, esquema, hoja 3].
// =============================================================================
#ifndef STM32_SOC_MSP430_FR2476_SOC_FR2476_H
#define STM32_SOC_MSP430_FR2476_SOC_FR2476_H

#include "../fr2/soc_fr2.h"
#include "../desc/msp430fr2476.h"

namespace stm32 {

namespace fr2476 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_a3", 0x0380, 3, "TIMER0_A0", "TIMER0_A1"},
    {"timer1_a3", 0x03C0, 3, "TIMER1_A0", "TIMER1_A1"},
    {"timer2_a3", 0x0400, 3, "TIMER2_A0", "TIMER2_A1"},
    {"timer3_a3", 0x0440, 3, "TIMER3_A0", "TIMER3_A1"},
    {"timer0_b7", 0x0480, 7, "TIMER0_B0", "TIMER0_B1", true},
};
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x0500, false, "EUSCI_A0"},
    {"eusci_a1", 0x0520, false, "EUSCI_A1"},
    {"eusci_b0", 0x0540, true, "EUSCI_B0"},
    {"eusci_b1", 0x0580, true, "EUSCI_B1"},
};
enum : uint8_t { UA0 = 0, UA1 = 1, UB0 = 2, UB1 = 3 };
enum : uint8_t { TA0 = 0, TA1 = 1, TA2 = 2, TA3 = 3, TB0 = 4 };
// Los bits de reasignación, en SYSCFG3 << 16 | SYSCFG2
enum : int8_t { RB0 = 11, RA0 = 16, RT2 = 18, RT3 = 19, RB1 = 20 };
constexpr uint8_t SI1 = 1 | REMAP_SI, NO1 = 1 | REMAP_NO, SI2 = 2 | REMAP_SI, NO2 = 2 | REMAP_NO;
// [DS tablas 9-23 a 9-28]
inline constexpr Fr2Fn FN[] = {
    // P1: la UCB0 y la UCA0 sin reasignar (01); TA0CLK, TA0.1, TA0.2, MCLK,
    // TA1.2, TA1.1, TA1CLK, SMCLK (10); A0-A7 (11)
    {fr2_pin(1, 0), NO1, 2, UB0, 3, RB0}, {fr2_pin(1, 1), NO1, 2, UB0, 2, RB0},
    {fr2_pin(1, 2), NO1, 2, UB0, 0, RB0}, {fr2_pin(1, 3), NO1, 2, UB0, 1, RB0},
    {fr2_pin(1, 4), NO1, 2, UA0, 0, RA0}, {fr2_pin(1, 5), NO1, 2, UA0, 1, RA0},
    {fr2_pin(1, 6), NO1, 2, UA0, 2, RA0}, {fr2_pin(1, 7), NO1, 2, UA0, 3, RA0},
    {fr2_pin(1, 0), 2, 5, TA0, 0}, {fr2_pin(1, 1), 2, 1, TA0, 1}, {fr2_pin(1, 2), 2, 1, TA0, 2}, {fr2_pin(1, 3), 2, 4, 0, 0},
    {fr2_pin(1, 4), 2, 1, TA1, 2}, {fr2_pin(1, 5), 2, 1, TA1, 1}, {fr2_pin(1, 6), 2, 5, TA1, 0}, {fr2_pin(1, 7), 2, 4, 0, 1},
    {fr2_pin(1, 0), 3, 3, 0, 0}, {fr2_pin(1, 1), 3, 3, 0, 0}, {fr2_pin(1, 2), 3, 3, 0, 0}, {fr2_pin(1, 3), 3, 3, 0, 0},
    {fr2_pin(1, 4), 3, 3, 0, 0}, {fr2_pin(1, 5), 3, 3, 0, 0}, {fr2_pin(1, 6), 3, 3, 0, 0}, {fr2_pin(1, 7), 3, 3, 0, 0},
    // P2: XOUT, XIN; ACLK / COMP0.1; TA2.0; la UCA1 (CLK, RXD, TXD); UCB1STE
    {fr2_pin(2, 0), 1, 3, 0, 0}, {fr2_pin(2, 1), 1, 3, 0, 0},
    {fr2_pin(2, 2), 2, 4, 0, 2}, {fr2_pin(2, 2), 3, 3, 0, 0},
    {fr2_pin(2, 3), NO1, 1, TA2, 0, RT2},
    {fr2_pin(2, 4), 1, 2, UA1, 2}, {fr2_pin(2, 5), 1, 2, UA1, 1}, {fr2_pin(2, 6), 1, 2, UA1, 0},
    {fr2_pin(2, 7), NO1, 2, UB1, 3, RB1},
    // P3: TA2.2; UCA1STE; UCB1SIMO; TA2.1; TA2CLK / COMP0OUT; UCB1CLK (TB0TRG: declarado); UCB1SOMI; TA3.2
    {fr2_pin(3, 0), NO1, 1, TA2, 2, RT2}, {fr2_pin(3, 1), 1, 2, UA1, 3}, {fr2_pin(3, 2), NO1, 2, UB1, 0, RB1},
    {fr2_pin(3, 3), NO1, 1, TA2, 1, RT2}, {fr2_pin(3, 4), NO1, 5, TA2, 0, RT2}, {fr2_pin(3, 4), 2, 7, 0, 0},
    {fr2_pin(3, 5), NO1, 2, UB1, 2, RB1}, {fr2_pin(3, 6), NO1, 2, UB1, 1, RB1}, {fr2_pin(3, 7), NO1, 1, TA3, 2, RT3},
    // P4: TA3.1, TA3.0, TA3CLK; UCB1SOMI/TB0.5/A8 y UCB1SIMO/TB0.6/A9 (la UCB1
    // reasignada); UCB0SOMI/TA3.2 y UCB0SIMO/TA3.1 (reasignadas); UCA0STE/TB0.1
    {fr2_pin(4, 0), NO1, 1, TA3, 1, RT3}, {fr2_pin(4, 1), NO1, 1, TA3, 0, RT3}, {fr2_pin(4, 2), NO1, 5, TA3, 0, RT3},
    {fr2_pin(4, 3), SI1, 2, UB1, 1, RB1}, {fr2_pin(4, 3), 2, 1, TB0, 5}, {fr2_pin(4, 3), 3, 3, 0, 0},
    {fr2_pin(4, 4), SI1, 2, UB1, 0, RB1}, {fr2_pin(4, 4), 2, 1, TB0, 6}, {fr2_pin(4, 4), 3, 3, 0, 0},
    {fr2_pin(4, 5), SI1, 2, UB0, 1, RB0}, {fr2_pin(4, 5), SI2, 1, TA3, 2, RT3},
    {fr2_pin(4, 6), SI1, 2, UB0, 0, RB0}, {fr2_pin(4, 6), SI2, 1, TA3, 1, RT3},
    {fr2_pin(4, 7), SI1, 2, UA0, 3, RA0}, {fr2_pin(4, 7), 2, 1, TB0, 1},
    // P5: la UCA0 reasignada (CLK, RXD, TXD) con TB0.2-TB0.4; UCB1CLK/TA3.0/A10,
    // UCB1STE/TA3CLK/A11; UCB0CLK/TA2CLK, UCB0STE/TA2.0; TA2.1 / COMP0.2
    {fr2_pin(5, 0), SI1, 2, UA0, 2, RA0}, {fr2_pin(5, 0), 2, 1, TB0, 2},
    {fr2_pin(5, 1), SI1, 2, UA0, 1, RA0}, {fr2_pin(5, 1), 2, 1, TB0, 3},
    {fr2_pin(5, 2), SI1, 2, UA0, 0, RA0}, {fr2_pin(5, 2), 2, 1, TB0, 4},
    {fr2_pin(5, 3), SI1, 2, UB1, 2, RB1}, {fr2_pin(5, 3), SI2, 1, TA3, 0, RT3}, {fr2_pin(5, 3), 3, 3, 0, 0},
    {fr2_pin(5, 4), SI1, 2, UB1, 3, RB1}, {fr2_pin(5, 4), SI2, 5, TA3, 0, RT3}, {fr2_pin(5, 4), 3, 3, 0, 0},
    {fr2_pin(5, 5), SI1, 2, UB0, 2, RB0}, {fr2_pin(5, 5), SI2, 5, TA2, 0, RT2},
    {fr2_pin(5, 6), SI1, 2, UB0, 3, RB0}, {fr2_pin(5, 6), SI2, 1, TA2, 0, RT2},
    {fr2_pin(5, 7), SI1, 1, TA2, 1, RT2}, {fr2_pin(5, 7), 3, 3, 0, 0},
    // P6: TA2.2 / COMP0.3; TB0CLK; TB0.0
    {fr2_pin(6, 0), SI1, 1, TA2, 2, RT2}, {fr2_pin(6, 0), 3, 3, 0, 0},
    {fr2_pin(6, 1), 1, 5, TB0, 0}, {fr2_pin(6, 2), 1, 1, TB0, 0},
    // TB0TRG en P3.5 (PxSEL = 10): el disparo de TB0OUTH
    {fr2_pin(3, 5), 2, 9, TB0, 0},
};
// TB0OUTH [DS tabla 9-17]: el eCOMP0, o P3.5 con TB0TRGSEL (SYSCFG2 bit 15)
inline constexpr Fr2Outh OUTH[] = {{TB0, 0, 15}};
inline constexpr Fr2Enlace ENLACES[] = {{TA0, TA1, 3}, {TA2, TA3, 3}, {TA3, TB0, 3}};
inline constexpr Fr2Disparo DISPAROS[] = {{TA1, 1, 2}};
inline constexpr Fr2Src NADA{0, 0};
inline constexpr Fr2Src V12{6, 0};
inline constexpr Fr2Ecomp ECOMPS[] = {
    {"ecomp0", 0x08E0, "ECOMP0",
     {{1, fr2_pin(1, 1)}, {1, fr2_pin(2, 2)}, V12, {1, fr2_pin(5, 7)}, {1, fr2_pin(6, 0)}, NADA, NADA, NADA},
     {{1, fr2_pin(1, 1)}, {1, fr2_pin(2, 2)}, V12, {1, fr2_pin(5, 7)}, {1, fr2_pin(6, 0)}, NADA, NADA, NADA},
     -1, 0, 3},
};
} // namespace fr2476

inline constexpr ChipFr2 CHIP_FR2476 = [] {
    ChipFr2 c{
        &m430::msp430fr2476::DESC,
        M430Caps{"msp430fr2476", M430Arq::CPUXV2, m430::msp430fr2476::DESC.errata, true},
        "fr2476",
        6, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07}, 6,
        fr2476::TIMERS, 5, fr2476::USCIS, 4,
        true, 0x0300,
        fr2476::FN, sizeof fr2476::FN / sizeof fr2476::FN[0],
        fr2476::ENLACES, 3, fr2476::DISPAROS, 1,
        {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7),
         fr2_pin(4, 3), fr2_pin(4, 4), fr2_pin(5, 3), fr2_pin(5, 4), -1, -1, -1, -1},
        fr2_pin(1, 0),                                // VeREF+ en P1.0
        0.788, 30.0, 0.0025,                          // [DS: VSENSOR y TCSENSOR]
        1, 0x03,                                      // XOUT/XIN en P2.0/P2.1
        {fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7)},
        fr2476::UA0, fr2_pin(1, 4), fr2_pin(1, 5),    // el backchannel: la UCA0 [SLAU802 §2.2.4]
        0x832A, 0xF7,
        0, nullptr, 0, nullptr, 0,
    };
    c.adc_por_sel = true;
    c.ecomps = fr2476::ECOMPS; c.necomps = 1;
    c.adc12 = true;
    c.ref_mejorada = true;
    c.errata_tb25 = true;                             // [SLAZ726B]
    c.outh = fr2476::OUTH; c.nouth = 1;
    return c;
}();

struct SocFR2476 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr2476::DESC;
    SocFR2476(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR2476, cab) {}
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR2476_SOC_FR2476_H
