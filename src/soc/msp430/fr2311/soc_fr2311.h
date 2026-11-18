// =============================================================================
// soc_fr2311.h — EL MSP430FR2311 (M7 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de la familia (soc/msp430/fr2/soc_fr2.h) [DS SLASE58E]
//
//   RAM 0x2000-0x23FF (1 KB), FRAM 0xF100-0xFFFF (3,75 KB), SIN memoria de
//   información; P1 y P2 (TSSOP-20), los dos con interrupciones; Timer0_B3 y
//   Timer1_B3 en 0x380/0x3C0 (Timer_B); eUSCI_A0 (P1.4-P1.7) y B0 (P1.0-P1.3,
//   o P2.2-P2.5 con USCIBRMP); sin MPY; el ADC de 10 bits con A0-A7 en P1,
//   analógicos con PxSEL = 11 (el SYSCFG2 de los «Smart Analog Combo» no
//   tiene ADCPCTL); el RTC en 0x300; XT1 en P2.7/P2.6 con P2SEL = 10; JTAG
//   de 4 hilos en P1.4-P1.7; y lo analógico [tablas 6-18 a 6-22]:
//
//     eCOMP0 (0x8E0)  CPPSEL/CPNSEL 000 C0 (P1.0), 001 C1 (P1.1), 100 C4: la
//                     salida del SAC0 en el positivo y la del TIA0 en el
//                     negativo; 110 su DAC. COUT en P2.0 (P2SEL = 10), a
//                     TB0.CCI1B y al ADC (ADCSHS = 3)
//     SAC0 (0xC80)    SAC-L1 (solo el OA): OA0+ P1.4, OA0- P1.2, OA0O P1.3;
//                     PSEL = 10, la salida del TIA0
//     TIA0 (0xF00)    TRI0+ P1.7, TRI0- P1.6 (doble en el TSSOP-20), TRI0O
//                     P1.5
//
// Las conexiones de los Timer [tablas 6-13 y 6-14]: las salidas de TB0 a las
// capturas B de TB1; TB1.1B dispara el ADC (ADCSHS = 2, tabla 6-17). [de
// modelo, no está] El RTC a CCI0A de TB0, TBxOUTH (TB0TRG, TB1TRG y el
// eCOMP), la modulación IR y el CapTIvate IO (CAPTIO0): declarados.
//
// El sensor: VSENSOR 913 mV a 0 °C y 3,35 mV/°C [DS tabla 5-22]. La TLV, el
// ID 0x82F0 y la CRC de 0x1A04 a 0x1A77 [tablas 6-45 y 6-46].
//
// En la LaunchPad (MSP-EXP430FR2311) no hay cristal (Y1 sin montar) y el
// fotodiodo D1 va al SAC0 en transimpedancia (R3 = 2,37 Mohm) [SLAU664B].
// =============================================================================
#ifndef STM32_SOC_MSP430_FR2311_SOC_FR2311_H
#define STM32_SOC_MSP430_FR2311_SOC_FR2311_H

#include "../fr2/soc_fr2.h"
#include "../desc/msp430fr2311.h"

namespace stm32 {

namespace fr2311 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_b3", 0x0380, 3, "TIMER0_B0", "TIMER0_B1", true},
    {"timer1_b3", 0x03C0, 3, "TIMER1_B0", "TIMER1_B1", true},
};
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x0500, false, "EUSCI_A0"},
    {"eusci_b0", 0x0540, true, "EUSCI_B0"},
};
enum : uint8_t { UA0 = 0, UB0 = 1 };
inline constexpr Fr2Fn FN[] = {
    // P1, 01: la UCB0 (STE, CLK, SIMO/SDA, SOMI/SCL; sin USCIBRMP) y la UCA0 (STE, CLK, RXD, TXD)
    {fr2_pin(1, 0), 1 | REMAP_NO, 2, UB0, 3}, {fr2_pin(1, 1), 1 | REMAP_NO, 2, UB0, 2},
    {fr2_pin(1, 2), 1 | REMAP_NO, 2, UB0, 0}, {fr2_pin(1, 3), 1 | REMAP_NO, 2, UB0, 1},
    {fr2_pin(1, 4), 1, 2, UA0, 3}, {fr2_pin(1, 5), 1, 2, UA0, 2}, {fr2_pin(1, 6), 1, 2, UA0, 1}, {fr2_pin(1, 7), 1, 2, UA0, 0},
    // P1, 10: SMCLK, ACLK, TB0.1, TB0.2 (TB0TRG en P1.2: declarado)
    {fr2_pin(1, 0), 2, 4, 0, 1}, {fr2_pin(1, 1), 2, 4, 0, 2}, {fr2_pin(1, 6), 2, 1, 0, 1}, {fr2_pin(1, 7), 2, 1, 0, 2},
    // P1, 11: lo analógico (C0, C1, OA0-, OA0O, OA0+, TRI0O, TRI0-, TRI0+, A0-A7)
    {fr2_pin(1, 0), 3, 3, 0, 0}, {fr2_pin(1, 1), 3, 3, 0, 0}, {fr2_pin(1, 2), 3, 3, 0, 0}, {fr2_pin(1, 3), 3, 3, 0, 0},
    {fr2_pin(1, 4), 3, 3, 0, 0}, {fr2_pin(1, 5), 3, 3, 0, 0}, {fr2_pin(1, 6), 3, 3, 0, 0}, {fr2_pin(1, 7), 3, 3, 0, 0},
    // P2: TB1.1 / COUT; TB1.2; la UCB0 con USCIBRMP; TB1CLK; MCLK / XOUT; TB0CLK / XIN
    {fr2_pin(2, 0), 1, 1, 1, 1}, {fr2_pin(2, 0), 2, 7, 0, 0}, {fr2_pin(2, 1), 1, 1, 1, 2},
    {fr2_pin(2, 2), 1 | REMAP_SI, 2, UB0, 3}, {fr2_pin(2, 3), 1 | REMAP_SI, 2, UB0, 2},
    // TB0TRG en P1.2 y TB1TRG en P2.3 (PxSEL = 10): los disparos de TBxOUTH
    {fr2_pin(1, 2), 2, 9, 0, 0}, {fr2_pin(2, 3), 2, 9, 1, 0},
    {fr2_pin(2, 4), 1 | REMAP_SI, 2, UB0, 0}, {fr2_pin(2, 5), 1 | REMAP_SI, 2, UB0, 1},
    {fr2_pin(2, 2), 2, 5, 1, 0},
    {fr2_pin(2, 6), 1, 4, 0, 0}, {fr2_pin(2, 6), 2, 3, 0, 0}, {fr2_pin(2, 7), 1, 5, 0, 0}, {fr2_pin(2, 7), 2, 3, 0, 0},
};
inline constexpr Fr2Enlace ENLACES[] = {{0, 1, 3}};
inline constexpr Fr2Disparo DISPAROS[] = {{1, 1, 2}};
inline constexpr Fr2Src NADA{0, 0};
inline constexpr Fr2Ecomp ECOMPS[] = {
    {"ecomp0", 0x08E0, "ECOMP0",
     {{1, fr2_pin(1, 0)}, {1, fr2_pin(1, 1)}, NADA, NADA, {2, 0}, NADA, NADA, NADA},
     {{1, fr2_pin(1, 0)}, {1, fr2_pin(1, 1)}, NADA, NADA, {3, 0}, NADA, NADA, NADA},
     0, 1, 3},
};
// TBxOUTH [DS tabla 6-15]: el eCOMP0 o el pin, con TB0TRGSEL (bit 15) y TB1TRGSEL (14)
inline constexpr Fr2Outh OUTH[] = {{0, 0, 15}, {1, 0, 14}};
inline constexpr Fr2Sac SACS[] = {
    {"sac0", 0x0C80, false, fr2_pin(1, 4), fr2_pin(1, 2), fr2_pin(1, 3), -1, {3, 0}, NADA, nullptr},
};
inline constexpr Fr2Tia TIA{0x0F00, fr2_pin(1, 7), fr2_pin(1, 6), fr2_pin(1, 5), NADA, NADA};
} // namespace fr2311

inline constexpr ChipFr2 CHIP_FR2311 = [] {
    ChipFr2 c{
        &m430::msp430fr2311::DESC,
        M430Caps{"msp430fr2311", M430Arq::CPUXV2, m430::msp430fr2311::DESC.errata, true},
        "fr2311",
        2, {0xFF, 0xFF}, 2,
        fr2311::TIMERS, 2, fr2311::USCIS, 2,
        false, 0x0300,
        fr2311::FN, sizeof fr2311::FN / sizeof fr2311::FN[0],
        fr2311::ENLACES, 1, fr2311::DISPAROS, 1,
        {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7),
         -1, -1, -1, -1, -1, -1, -1, -1},
        fr2_pin(1, 0),                                // VeREF+ en P1.0
        0.913, 0.0, 0.00335,                          // [DS tabla 5-22]
        1, 0xC0,                                      // XIN/XOUT en P2.7/P2.6
        {fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7)},
        fr2311::UA0, fr2_pin(1, 7), fr2_pin(1, 6),
        0x82F0, 0x77,
        0, nullptr, 0, nullptr, 0,
    };
    c.adc_por_sel = true;
    c.xt1_selv = 2;                                   // P2SEL = 10
    c.ecomps = fr2311::ECOMPS; c.necomps = 1;
    c.sacs = fr2311::SACS; c.nsacs = 1;
    c.tia = &fr2311::TIA;
    c.remap_bit = 11;                                 // USCIBRMP [UG FR2 tabla 1-26]
    c.errata_tb25 = true;                             // [SLAZ679R]
    c.outh = fr2311::OUTH; c.nouth = 2;
    return c;
}();

struct SocFR2311 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr2311::DESC;
    SocFR2311(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR2311, cab) {
        cristal_xt1 = false;                  // la LaunchPad: Y1 sin montar [SLAU664B §2.5]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR2311_SOC_FR2311_H
