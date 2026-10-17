// =============================================================================
// e4_desc.h — El DESCRIPTOR de la familia MSP432E4, su mapa y sus nombres (T6)
//
// Es al MSP432E4 lo que `soc/msp432p4/p4_desc.h` al P4. Lo que cambia de un
// MSP432 al otro es casi todo menos el núcleo: el E4 es un Tiva (TM4C129) con
// otro nombre, y sus periféricos son de esa escuela -PrimeCell, `RCGC`/`PR`
// por periférico, el dato del GPIO enmascarado por la dirección-.
// [doc/analisis_msp432.md, §2 y §8 T6]
//
// LAS FUENTES de cada dato (regla del proyecto, doc/fuentes.md):
//
//   [H]   msp432e401y.h, la cabecera CMSIS de TI (BSD-3), y las hw_*.h de
//         DriverLib del E4: bases, registros, campos, IRQ, `__NVIC_PRIO_BITS`.
//   [DL]  DriverLib del E4 (BSD-3, la copia del núcleo `msp432e-core` de
//         Energia): sysctl.c (PLL, MEMTIM0), gpio.c, flash.c, pin_map.h.
//   [OO]  OpenOCD, src/flash/nor/msp432.c (GPL-2.0, solo como referencia, sin
//         copiar código): DID0 = 0x180C0002 y DID1 = 0x102DC06E en el
//         E401Y, 1 MB de Flash en sectores de 16 KB; y su algoritmo de Flash
//         para el E4 (contrib/loaders/flash/msp432, BSD-3): FMA/FMD/FMC/FMC2.
//   [EN]  Energia, variants/MSP_EXP432E401Y (BSD): los pines que saca la
//         LaunchPad, sus LED y sus pulsadores.
//   [DS]  El datasheet del MSP432E401Y (SLASEN5), por la copia de Mouser:
//         90 E/S en quince puertos, TQFP de 128, 120 MHz, 1 MB/256 KB/6 KB.
//
// Lo que solo tiene UNA fuente está marcado [1F] y anotado en doc/todo.md
// (P-17, subpuntos).
// =============================================================================
#ifndef STM32_SOC_MSP432E4_E4_DESC_H
#define STM32_SOC_MSP432E4_E4_DESC_H

#include <cstdint>
#include <cstdio>
#include <string>
#include "../../core/core_caps.h"
#include "../../pins/encapsulado.h"
#include "../../pins/nomenclatura.h"
#include "../mcu_desc.h"

namespace stm32 {
namespace e4 {

// --- El mapa de memoria [H; OO] ----------------------------------------------
constexpr uint32_t FLASH_B     = 0x00000000u;
constexpr uint32_t SECTOR      = 0x00004000u;   // 16 KB [OO, E4_SECTOR_LENGTH]
constexpr uint32_t ROM_B       = 0x01000000u;   // cargador y DriverLib [DS]
constexpr uint32_t ROM_SIZE    = 0x00040000u;   // 256 KB [1F: P-17 b]
constexpr uint32_t ROM_APITABLE = 0x01000010u;  // [DL, rom.h]
constexpr uint32_t SRAM_B      = 0x20000000u;
constexpr uint32_t PERIPH_B    = 0x40000000u;
constexpr uint32_t PERIPH_FIN  = 0x40100000u;
constexpr uint32_t CCM_B       = 0x44030000u;   // CRC, SHA/MD5, AES, DES
constexpr uint32_t CCM_FIN     = 0x4403A000u;

// Los periféricos [H, *_BASE]
constexpr uint32_t WDT0_B = 0x40000000u, WDT1_B = 0x40001000u;
constexpr uint32_t SSI0_B = 0x40008000u;          // SSIn = SSI0_B + n·0x1000
constexpr uint32_t UART0_B = 0x4000C000u;         // UARTn = UART0_B + n·0x1000
constexpr uint32_t I2C0_B = 0x40020000u;          // I2C0..3 seguidos
constexpr uint32_t PWM0_B = 0x40028000u, QEI0_B = 0x4002C000u;
constexpr uint32_t TIMER0_B = 0x40030000u;        // TIMER0..5 seguidos
constexpr uint32_t ADC0_B = 0x40038000u, ADC1_B = 0x40039000u;
constexpr uint32_t COMP_B = 0x4003C000u;
constexpr uint32_t CAN0_B = 0x40040000u, CAN1_B = 0x40041000u;
constexpr uint32_t USB0_B = 0x40050000u;
constexpr uint32_t GPIO_B = 0x40058000u;          // A..Q, 4 KB cada uno, en el AHB
constexpr uint32_t EEPROM_B = 0x400AF000u;
constexpr uint32_t I2C8_B = 0x400B8000u, I2C9_B = 0x400B9000u;
constexpr uint32_t I2C4_B = 0x400C0000u;          // I2C4..7 seguidos
constexpr uint32_t EPI0_B = 0x400D0000u;
constexpr uint32_t TIMER6_B = 0x400E0000u, TIMER7_B = 0x400E1000u;
constexpr uint32_t EMAC0_B = 0x400EC000u;
constexpr uint32_t SYSEXC_B = 0x400F9000u;
constexpr uint32_t HIB_B = 0x400FC000u;
constexpr uint32_t FLASH_CTRL_B = 0x400FD000u;
constexpr uint32_t SYSCTL_B = 0x400FE000u;
constexpr uint32_t UDMA_B = 0x400FF000u;

// --- Las interrupciones [H, IRQn_Type] ---------------------------------------
enum Irq : unsigned {
    IRQ_GPIOA = 0, IRQ_GPIOB = 1, IRQ_GPIOC = 2, IRQ_GPIOD = 3, IRQ_GPIOE = 4,
    IRQ_UART0 = 5, IRQ_UART1 = 6, IRQ_SSI0 = 7, IRQ_I2C0 = 8,
    IRQ_PWM0_FAULT = 9, IRQ_PWM0_0 = 10, IRQ_PWM0_1 = 11, IRQ_PWM0_2 = 12,
    IRQ_QEI0 = 13, IRQ_ADC0SS0 = 14, IRQ_ADC0SS1 = 15, IRQ_ADC0SS2 = 16,
    IRQ_ADC0SS3 = 17, IRQ_WATCHDOG = 18, IRQ_TIMER0A = 19, IRQ_TIMER0B = 20,
    IRQ_TIMER1A = 21, IRQ_TIMER1B = 22, IRQ_TIMER2A = 23, IRQ_TIMER2B = 24,
    IRQ_COMP0 = 25, IRQ_COMP1 = 26, IRQ_COMP2 = 27, IRQ_SYSCTL = 28,
    IRQ_FLASH = 29, IRQ_GPIOF = 30, IRQ_GPIOG = 31, IRQ_GPIOH = 32,
    IRQ_UART2 = 33, IRQ_SSI1 = 34, IRQ_TIMER3A = 35, IRQ_TIMER3B = 36,
    IRQ_I2C1 = 37, IRQ_CAN0 = 38, IRQ_CAN1 = 39, IRQ_EMAC0 = 40, IRQ_HIB = 41,
    IRQ_USB0 = 42, IRQ_PWM0_3 = 43, IRQ_UDMA = 44, IRQ_UDMAERR = 45,
    IRQ_ADC1SS0 = 46, IRQ_ADC1SS1 = 47, IRQ_ADC1SS2 = 48, IRQ_ADC1SS3 = 49,
    IRQ_EPI0 = 50, IRQ_GPIOJ = 51, IRQ_GPIOK = 52, IRQ_GPIOL = 53,
    IRQ_SSI2 = 54, IRQ_SSI3 = 55, IRQ_UART3 = 56, IRQ_UART4 = 57,
    IRQ_UART5 = 58, IRQ_UART6 = 59, IRQ_UART7 = 60, IRQ_I2C2 = 61,
    IRQ_I2C3 = 62, IRQ_TIMER4A = 63, IRQ_TIMER4B = 64, IRQ_TIMER5A = 65,
    IRQ_TIMER5B = 66, IRQ_SYSEXC = 67, IRQ_I2C4 = 70, IRQ_I2C5 = 71,
    IRQ_GPIOM = 72, IRQ_GPION = 73, IRQ_GPIOP0 = 76, IRQ_GPIOQ0 = 84,
    IRQ_TIMER6A = 98, IRQ_TIMER6B = 99, IRQ_TIMER7A = 100, IRQ_TIMER7B = 101,
    IRQ_I2C6 = 102, IRQ_I2C7 = 103, IRQ_I2C8 = 109, IRQ_I2C9 = 110,
    N_IRQ = 111
};

// --- Los puertos ----------------------------------------------------------------
// Quince en el E401Y: A..H, J..N, P y Q (sin I ni O). El índice interno es el
// de la tabla de bases de DriverLib, g_pui32GPIOBaseAddrs [DL, gpio.c]: A = 0,
// ..., H = 7, J = 8, ..., N = 12, P = 13, Q = 14. R, S y T (15..17) son del
// E411Y.
constexpr unsigned N_PUERTOS = 15;
constexpr unsigned N_PINES   = 8;
constexpr char LETRAS[] = "ABCDEFGHJKLMNPQRST";
enum Puerto : unsigned { PA = 0, PB, PC, PD, PE, PF, PG, PH, PJ, PK, PL, PM, PN, PP, PQ };
inline uint32_t base_gpio(unsigned p) { return GPIO_B + 0x1000u * p; }
// La línea de interrupción de cada puerto [H]. P y Q tienen una por pin; aquí,
// la del pin 0 (la de resumen, con GPIOSI.SUM).
inline unsigned irq_gpio(unsigned p) {
    static const uint8_t t[N_PUERTOS] = { 0, 1, 2, 3, 4, 30, 31, 32, 51, 52, 53, 72, 73, 76, 84 };
    return p < N_PUERTOS ? t[p] : 0;
}

// --- Los osciladores [DL, sysctl.c; 1F donde se dice] -------------------------
constexpr double HZ_PIOSC  = 16.0e6;    // [DL: SysCtlClockFreqSet, «ui32Osc = 16000000»]
constexpr double HZ_LFIOSC = 33.0e3;    // [1F: datasheet de memoria, 33 kHz típicos;
                                        //  DriverLib usa 30 kHz nominales: P-17 c]
constexpr double HZ_RTC    = 32768.0;   // el cristal del módulo de hibernación
constexpr double HZ_MAX    = 120.0e6;   // [DS]

// --- Los tiempos de la Flash: MEMTIM0 [DL: g_sXTALtoMEMTIM] ----------------------
// Hasta 16 MHz sin estado de espera (y con FBCE); 40/60/80/100/120 MHz con 1..5.
inline unsigned fws_minimo(double hz) {
    if (hz <= 16.0e6 * (1 + 1e-9))  return 0;
    if (hz <= 40.0e6 * (1 + 1e-9))  return 1;
    if (hz <= 60.0e6 * (1 + 1e-9))  return 2;
    if (hz <= 80.0e6 * (1 + 1e-9))  return 3;
    if (hz <= 100.0e6 * (1 + 1e-9)) return 4;
    return 5;
}

// --- La identidad [OO] -----------------------------------------------------------
constexpr uint32_t DID0_E401Y = 0x180C0002u;
constexpr uint32_t DID1_E401Y = 0x102DC06Eu;

} // namespace e4

// =============================================================================
// EL ENCAPSULADO: el TQFP de 128 (PDT), el de la MSP-EXP432E401Y
//
// 90 E/S [DS]. El reparto por puerto es el del TM4C1294NCPDT, que es el mismo
// silicio; los pines que usa la LaunchPad [EN] caen todos dentro, y la suma da
// 90. PA6 y PQ4 no salen a los conectores de la LaunchPad, pero sí del chip
// [1F para esos dos: P-17 a].
// =============================================================================
inline constexpr Encapsulado ENC_E4_TQFP128 {
    "TQFP128", 128, 90,
    { 0xFF, 0x3F, 0xFF, 0xFF, 0x3F, 0x1F, 0x03, 0x0F,   // A..H: 8 6 8 8 6 5 2 4
      0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0x3F, 0x1F },       // J..Q: 2 8 8 8 6 6 5
    true
};

// =============================================================================
// LA NOMENCLATURA: `PA0`…`PQ7`, sin I ni O, y `PN.0` como alias
//
// Es la del datasheet y la de la serigrafía de la LaunchPad. Se parece a la de
// ST a propósito, pero no es la misma: en un STM32, `PJ0` no existe y `PI0` sí.
// =============================================================================
namespace e4 {
inline int indice_letra(char c) {
    for (unsigned k = 0; k < N_PUERTOS; ++k) if (LETRAS[k] == c) return int(k);
    return -1;
}
inline bool pad_e4(const std::string& t, unsigned& puerto, unsigned& pin) {
    std::string s = t;
    if (s.size() == 4 && (s[2] == '.' || s[2] == '_')) s.erase(2, 1);   // PN.0, PN_0
    if (s.size() != 3 || s[0] != 'P') return false;
    const int p = indice_letra(s[1]);
    if (p < 0 || s[2] < '0' || s[2] > '7') return false;
    puerto = unsigned(p);
    pin    = unsigned(s[2] - '0');
    return true;
}
inline std::string canonico_e4(unsigned puerto, unsigned pin) {
    char b[8];
    std::snprintf(b, sizeof b, "P%c%u", puerto < 18 ? LETRAS[puerto] : '?', pin);
    return b;
}
// Las patillas que no son de puerto. RST es la del datasheet (activa a nivel
// bajo); OSC0/OSC1 el cristal principal (25 MHz en la LaunchPad); XOSC0/XOSC1
// el de 32 768 Hz de la hibernación; HIB y WAKE las suyas.
inline const char* const ALIM_E4[] = {
    "VDD", "GND", "VDDA", "GNDA", "VDDC", "VBAT", "RST", "OSC0", "OSC1",
    "XOSC0", "XOSC1", "HIB", "WAKE", nullptr
};
} // namespace e4

inline const Nomenclatura NOM_MSP432E4{
    "MSP432E4", &e4::pad_e4, &e4::canonico_e4, e4::ALIM_E4
};
REGISTRA_NOMENCLATURA(MSP432E4, &NOM_MSP432E4);

// =============================================================================
// EL DESCRIPTOR de un miembro de la familia
// =============================================================================
struct McuCapsE4 {
    const char*  nombre;
    CoreCaps     nucleo;
    uint32_t     flash_size;
    uint32_t     sram_size;
    uint32_t     eeprom_size;
    uint32_t     did0, did1;
    Encapsulado  enc;
};

// El núcleo: Cortex-M4F r0p1... [H: __CM4_REV = 0x0102, __NVIC_PRIO_BITS = 3,
// __MPU_PRESENT, __FPU_PRESENT]. 111 líneas, de GPIOA (0) a I2C9 (110).
inline constexpr CoreCaps CORE_MSP432E4{
    e4::N_IRQ, 3, 8, FpuKind::SimplePrec, CPUID_CORTEX_M4
};

inline constexpr McuCapsE4 MCU_MSP432E401Y{
    "MSP432E401Y", CORE_MSP432E4,
    0x00100000u,                 // 1 MB [DS, OO]
    0x00040000u,                 // 256 KB [DS]
    0x00001800u,                 // 6 KB de EEPROM [DS]
    e4::DID0_E401Y, e4::DID1_E401Y,
    ENC_E4_TQFP128
};

struct RegistroCatalogoE4 {
    RegistroCatalogoE4() {
        CatalogoMcu::registra(McuDesc{ MCU_MSP432E401Y.nombre, "MSP432E4",
                                       &MCU_MSP432E401Y.enc, &MCU_MSP432E401Y });
    }
};
inline const RegistroCatalogoE4 g_registro_catalogo_e4{};

} // namespace stm32
#endif // STM32_SOC_MSP432E4_E4_DESC_H
