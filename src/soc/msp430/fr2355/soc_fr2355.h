// =============================================================================
// soc_fr2355.h — EL MSP430FR2355 (M7 de doc/analisis_msp430.md): la tabla del
// chip para el SoC de la familia (soc/msp430/fr2/soc_fr2.h) [DS SLASEC4D]
//
//   RAM 0x2000-0x2FFF (4 KB), FRAM 0x8000-0xFFFF (32 KB), información
//   0x1800-0x19FF; P1-P6 (LQFP-48: P5.0-P5.4 y P6.0-P6.6), con
//   interrupciones P1-P4; Timer0_B3, Timer1_B3, Timer2_B3 (0x380, 0x3C0,
//   0x400) y Timer3_B7 (0x440); eUSCI_A0, A1, B0 y B1; MPY32; el ADC de 12
//   bits (A0-A7 en P1, A8-A11 en P5.0-P5.3, analógicos con PxSEL = 11); la
//   referencia compartida mejorada (REFVSEL: 1,5, 2,0 o 2,5 V); el ICC en
//   0x6C0 (ILSR0 es el vector 0xFFCE, el P4, hasta ILSR21, el 0xFFF8 de
//   TB0 CCR0 [tabla 6-13]); el RTC en 0x300; XT1 en P2.7/P2.6 con PxSEL =
//   10; JTAG de 4 hilos en P1.4-P1.7; y lo analógico [tablas 6-21 a 6-32]:
//
//     eCOMP0 (0x8E0)  CPPSEL 000 P1.0, 001 P1.1, 010 la de 1,2 V, 101 la
//                     salida del SAC0 (OA0O); CPNSEL igual salvo 101, la del
//                     SAC2 (OA2O). COUT en P2.0 (PxSEL = 10), a TB0.CCI1B y
//                     al ADC (ADCSHS = 3)
//     eCOMP1 (0x900)  000 P2.5, 001 P2.4, 010 la de 1,2 V; 101 la salida
//                     del SAC1 (+) o del SAC3 (-). COUT en P2.1, a TB2.CCI1B
//     SAC0-SAC3       SAC-L3 (OA, PGA y DAC de 12 bits) en 0xC80, 0xC90,
//                     0xCA0, 0xCB0, por parejas SAC0-SAC2 y SAC1-SAC3 (PSEL
//                     o NSEL = 10, la salida de la pareja). Pines: OA0 P1.3+,
//                     P1.2-, P1.1 O; OA1 P1.7, P1.6, P1.5; OA2 P3.3, P3.2,
//                     P3.1; OA3 P3.7, P3.6, P3.5. Las salidas del SAC0 y del
//                     SAC1 llegan al ADC (A1 y A5) por dentro. El DAC:
//                     DACSREF 0 DVCC, 1 la compartida; DACLSEL 10 TB2.1, 11
//                     TB2.2. Sus vectores, SAC0_SAC2 y SAC1_SAC3
//
// Las conexiones de los Timer [tablas 6-16 a 6-19]: las salidas de TB0 a las
// capturas B de TB1; TB1.1B dispara el ADC (ADCSHS = 2, tabla 6-22). [de
// modelo, no está] TB3 CCR0B a TB1 CCI0A, el RTC a TB0 CCI0A, TBxOUTH
// (TBxTRG), la eUSCI_A1 invertida (PxSEL = 10), el IR, el MFM (P5.0/P5.1)
// y la ROM de 20 KB (0xFAC00-0xFFBFF: DriverLib y FFT, que la demo de
// fábrica no llama): declarados.
//
// SYSCFG0.FRWPOA: el primer FRWPOA x 1 KB de la FRAM principal queda sin
// proteger por PFWP; el arranque de CCS lo usa para las variables
// PERSISTENT de la demo de fábrica [UG FR2 §1.9.2].
//
// El sensor: VSENSOR 788 mV a 30 °C y 2,32 mV/°C [DS tabla 5-9]. La TLV, el
// ID 0x830C y la CRC de 0x1A04 a 0x1AF7 [tablas 6-69 y 6-70].
//
// La LaunchPad (MSP-EXP430FR2355) lleva cristal de 32 768 Hz (Y4) y el
// fotodiodo D1 al SAC2 en transimpedancia (R3 = 2,37 Mohm) [SLAU680].
// =============================================================================
#ifndef STM32_SOC_MSP430_FR2355_SOC_FR2355_H
#define STM32_SOC_MSP430_FR2355_SOC_FR2355_H

#include "../fr2/soc_fr2.h"
#include "../desc/msp430fr2355.h"

namespace stm32 {

namespace fr2355 {
inline constexpr Fr2Timer TIMERS[] = {
    {"timer0_b3", 0x0380, 3, "TIMER0_B0", "TIMER0_B1", true},
    {"timer1_b3", 0x03C0, 3, "TIMER1_B0", "TIMER1_B1", true},
    {"timer2_b3", 0x0400, 3, "TIMER2_B0", "TIMER2_B1", true},
    {"timer3_b7", 0x0440, 7, "TIMER3_B0", "TIMER3_B1", true},
};
inline constexpr Fr2Usci USCIS[] = {
    {"eusci_a0", 0x0500, false, "EUSCI_A0"},
    {"eusci_a1", 0x0580, false, "EUSCI_A1"},
    {"eusci_b0", 0x0540, true, "EUSCI_B0"},
    {"eusci_b1", 0x05C0, true, "EUSCI_B1"},
};
enum : uint8_t { UA0 = 0, UA1 = 1, UB0 = 2, UB1 = 3 };
// [DS tablas 6-63 a 6-68]
inline constexpr Fr2Fn FN[] = {
    // P1, 01: la UCB0 (STE, CLK, SIMO/SDA, SOMI/SCL) y la UCA0 (STE, CLK, RXD, TXD)
    {fr2_pin(1, 0), 1, 2, UB0, 3}, {fr2_pin(1, 1), 1, 2, UB0, 2}, {fr2_pin(1, 2), 1, 2, UB0, 0}, {fr2_pin(1, 3), 1, 2, UB0, 1},
    {fr2_pin(1, 4), 1, 2, UA0, 3}, {fr2_pin(1, 5), 1, 2, UA0, 2}, {fr2_pin(1, 6), 1, 2, UA0, 1}, {fr2_pin(1, 7), 1, 2, UA0, 0},
    // P1, 10: SMCLK, ACLK, TB0.1, TB0.2 (TB0TRG en P1.2: declarado)
    {fr2_pin(1, 0), 2, 4, 0, 1}, {fr2_pin(1, 1), 2, 4, 0, 2}, {fr2_pin(1, 6), 2, 1, 0, 1}, {fr2_pin(1, 7), 2, 1, 0, 2},
    // P1, 11: A0-A7, COMP0.0/1, OA0 y OA1
    {fr2_pin(1, 0), 3, 3, 0, 0}, {fr2_pin(1, 1), 3, 3, 0, 0}, {fr2_pin(1, 2), 3, 3, 0, 0}, {fr2_pin(1, 3), 3, 3, 0, 0},
    {fr2_pin(1, 4), 3, 3, 0, 0}, {fr2_pin(1, 5), 3, 3, 0, 0}, {fr2_pin(1, 6), 3, 3, 0, 0}, {fr2_pin(1, 7), 3, 3, 0, 0},
    // P2: TB1.1 / COMP0.O; TB1.2 / COMP1.O; TB1CLK; (TB1TRG: declarado); COMP1.1, COMP1.0;
    // MCLK / XOUT; TB0CLK / XIN
    {fr2_pin(2, 0), 1, 1, 1, 1}, {fr2_pin(2, 0), 2, 7, 0, 0}, {fr2_pin(2, 1), 1, 1, 1, 2}, {fr2_pin(2, 1), 2, 7, 1, 0},
    {fr2_pin(2, 2), 1, 5, 1, 0},
    {fr2_pin(2, 4), 3, 3, 0, 0}, {fr2_pin(2, 5), 3, 3, 0, 0},
    {fr2_pin(2, 6), 1, 4, 0, 0}, {fr2_pin(2, 6), 2, 3, 0, 0}, {fr2_pin(2, 7), 1, 5, 0, 0}, {fr2_pin(2, 7), 2, 3, 0, 0},
    // P3: MCLK, OA2O, OA2-, OA2+, SMCLK, OA3O, OA3-, OA3+
    {fr2_pin(3, 0), 1, 4, 0, 0}, {fr2_pin(3, 1), 3, 3, 0, 0}, {fr2_pin(3, 2), 3, 3, 0, 0}, {fr2_pin(3, 3), 3, 3, 0, 0},
    {fr2_pin(3, 4), 1, 4, 0, 1}, {fr2_pin(3, 5), 3, 3, 0, 0}, {fr2_pin(3, 6), 3, 3, 0, 0}, {fr2_pin(3, 7), 3, 3, 0, 0},
    // P4, 01: la UCA1 (STE, CLK, RXD, TXD) y la UCB1 (STE, CLK, SIMO/SDA, SOMI/SCL)
    {fr2_pin(4, 0), 1, 2, UA1, 3}, {fr2_pin(4, 1), 1, 2, UA1, 2}, {fr2_pin(4, 2), 1, 2, UA1, 1}, {fr2_pin(4, 3), 1, 2, UA1, 0},
    {fr2_pin(4, 4), 1, 2, UB1, 3}, {fr2_pin(4, 5), 1, 2, UB1, 2}, {fr2_pin(4, 6), 1, 2, UB1, 0}, {fr2_pin(4, 7), 1, 2, UB1, 1},
    // P5: TB2.1, TB2.2, TB2CLK (TB2TRG y el MFM: declarados); A8-A11
    {fr2_pin(5, 0), 1, 1, 2, 1}, {fr2_pin(5, 1), 1, 1, 2, 2}, {fr2_pin(5, 2), 1, 5, 2, 0},
    {fr2_pin(5, 0), 3, 3, 0, 0}, {fr2_pin(5, 1), 3, 3, 0, 0}, {fr2_pin(5, 2), 3, 3, 0, 0}, {fr2_pin(5, 3), 3, 3, 0, 0},
    // P6: TB3.1-TB3.6 y TB3CLK
    {fr2_pin(6, 0), 1, 1, 3, 1}, {fr2_pin(6, 1), 1, 1, 3, 2}, {fr2_pin(6, 2), 1, 1, 3, 3}, {fr2_pin(6, 3), 1, 1, 3, 4},
    {fr2_pin(6, 4), 1, 1, 3, 5}, {fr2_pin(6, 5), 1, 1, 3, 6}, {fr2_pin(6, 6), 1, 5, 3, 0},
    // TB0TRG (P1.2, 10), TB1TRG (P2.3, 01) y TB2TRG (P5.3, 01): los disparos de TBxOUTH
    {fr2_pin(1, 2), 2, 9, 0, 0}, {fr2_pin(2, 3), 1, 9, 1, 0}, {fr2_pin(5, 3), 1, 9, 2, 0},
    // El MFM: MFM.RX en P5.0 y MFM.TX en P5.1 (PxSEL = 10) [DS §6.10.14]
    {fr2_pin(5, 0), 2, 8, 0, 0}, {fr2_pin(5, 1), 2, 8, 0, 1},
};
// TBxOUTH [DS tabla 6-20]: TB0/TB1 con el eCOMP0, TB2/TB3 con el eCOMP1, o
// su pin TBxTRG (TB3: ninguno) con TBxTRGSEL (SYSCFG2 bits 15-12)
inline constexpr Fr2Outh OUTH[] = {{0, 0, 15}, {1, 0, 14}, {2, 1, 13}, {3, 1, 12}};
// El MFM: la eUSCI_B1 de esclavo; arranca con la salida de TB2 CCR0 y avisa
// del fin en TB2 CCI0B [DS §6.10.14 y tabla 6-18]
inline constexpr Fr2Mfm MFM{3, 2, 0, 0};
inline constexpr Fr2Enlace ENLACES[] = {{0, 1, 3}};
inline constexpr Fr2Disparo DISPAROS[] = {{1, 1, 2}};
inline constexpr Fr2Disparo DAC_DISPAROS[] = {{2, 1, 2}, {2, 2, 3}};     // TB2.1: DACLSEL = 10; TB2.2: 11
inline constexpr Fr2Src NADA{0, 0};
inline constexpr Fr2Src V12{6, 0};
inline constexpr Fr2Ecomp ECOMPS[] = {
    {"ecomp0", 0x08E0, "ECOMP0_ECOMP1",
     {{1, fr2_pin(1, 0)}, {1, fr2_pin(1, 1)}, V12, NADA, NADA, {2, 0}, NADA, NADA},
     {{1, fr2_pin(1, 0)}, {1, fr2_pin(1, 1)}, V12, NADA, NADA, {2, 2}, NADA, NADA},
     0, 1, 3},
    {"ecomp1", 0x0900, "ECOMP0_ECOMP1",
     {{1, fr2_pin(2, 5)}, {1, fr2_pin(2, 4)}, V12, NADA, NADA, {2, 1}, NADA, NADA},
     {{1, fr2_pin(2, 5)}, {1, fr2_pin(2, 4)}, V12, NADA, NADA, {2, 3}, NADA, NADA},
     2, 1, 0},
};
inline constexpr Fr2Sac SACS[] = {
    {"sac0", 0x0C80, true, fr2_pin(1, 3), fr2_pin(1, 2), fr2_pin(1, 1), 2, {2, 2}, {2, 2}, "SAC0_SAC2", 1},
    {"sac1", 0x0C90, true, fr2_pin(1, 7), fr2_pin(1, 6), fr2_pin(1, 5), 3, {2, 3}, {2, 3}, "SAC1_SAC3", 5},
    {"sac2", 0x0CA0, true, fr2_pin(3, 3), fr2_pin(3, 2), fr2_pin(3, 1), 0, {2, 0}, {2, 0}, "SAC0_SAC2", -1},
    {"sac3", 0x0CB0, true, fr2_pin(3, 7), fr2_pin(3, 6), fr2_pin(3, 5), 1, {2, 1}, {2, 1}, "SAC1_SAC3", -1},
};
} // namespace fr2355

inline constexpr ChipFr2 CHIP_FR2355 = [] {
    ChipFr2 c{
        &m430::msp430fr2355::DESC,
        M430Caps{"msp430fr2355", M430Arq::CPUXV2, m430::msp430fr2355::DESC.errata, true},
        "fr2355",
        6, {0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0x7F}, 4,
        fr2355::TIMERS, 4, fr2355::USCIS, 4,
        true, 0x0300,
        fr2355::FN, sizeof fr2355::FN / sizeof fr2355::FN[0],
        fr2355::ENLACES, 1, fr2355::DISPAROS, 1,
        {fr2_pin(1, 0), fr2_pin(1, 1), fr2_pin(1, 2), fr2_pin(1, 3), fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7),
         fr2_pin(5, 0), fr2_pin(5, 1), fr2_pin(5, 2), fr2_pin(5, 3), -1, -1, -1, -1},
        fr2_pin(1, 0),                                // VeREF+ en P1.0
        0.788, 30.0, 0.00232,                         // [DS tabla 5-9]
        1, 0xC0,                                      // XIN/XOUT en P2.7/P2.6
        {fr2_pin(1, 4), fr2_pin(1, 5), fr2_pin(1, 6), fr2_pin(1, 7)},
        fr2355::UA1, fr2_pin(4, 3), fr2_pin(4, 2),    // el backchannel: la UCA1 [SLAU680 §2.2.4]
        0x830C, 0xF7,
        0, nullptr, 0, nullptr, 0,
    };
    c.adc_por_sel = true;
    c.xt1_selv = 2;                                   // P2SEL = 10
    c.ecomps = fr2355::ECOMPS; c.necomps = 2;
    c.sacs = fr2355::SACS; c.nsacs = 4;
    c.adc12 = true;
    c.ref_mejorada = true;
    c.frwpoa = true;
    c.errata_tb25 = true;                             // [SLAZ695J]
    c.outh = fr2355::OUTH; c.nouth = 4;
    c.mfm = &fr2355::MFM;
    c.rom_imagen = "verif/fw/fr2355/rom_driverlib_fr235x.txt";
    c.icc_base = 0x06C0; c.icc_vec0 = 0xFFCE;
    c.dac_disparos = fr2355::DAC_DISPAROS; c.ndac_disparos = 2;
    return c;
}();

struct SocFR2355 : SocFR2 {
    static constexpr const m430::Desc430& DESC = m430::msp430fr2355::DESC;
    SocFR2355(sc_core::sc_module_name nm, const Cableado* cab = nullptr) : SocFR2(nm, CHIP_FR2355, cab) {
        cristal_xt1 = true;                   // la LaunchPad: Y4, 32 768 Hz [SLAU680, esquema]
    }
};

} // namespace stm32
#endif // STM32_SOC_MSP430_FR2355_SOC_FR2355_H
