// =============================================================================
// p4_desc.h — El DESCRIPTOR de la familia MSP432P4, su mapa y sus nombres
//
// Es a un MSP432P4 lo que `top/mcu_caps.h` es a un STM32F4, con una diferencia
// que es la razón de T0: NO es un `McuCaps`. Un MSP432 no tiene árbol de reloj
// de ST, ni matriz AHB de ST, ni DBGMCU, y describirlo con ese struct sería
// ponerle una etiqueta falsa. Lo que tiene es esto. [doc/analisis_msp432.md, §2]
//
// LAS DOS FUENTES de cada dato (regla del proyecto, doc/fuentes.md):
//
//   [H]   msp432p401r.h, la cabecera CMSIS de TI (BSD-3, 2017-12-06): bases,
//         registros, campos, IRQ, `__NVIC_PRIO_BITS`.
//   [SA]  SLAA668A, «Getting Started with MSP432P4xx» (TI, 2017): VCORE y
//         estados de espera de la Flash, tabla 2.
//   [SYS] system_msp432p401r.c y startup_msp432p401r.c de TI (BSD-3): las
//         frecuencias de VLO, REFO, MODOSC y SYSOSC, y la fórmula del DCO.
//   [OO]  OpenOCD, src/flash/nor/msp432.c (GPL-2.0, solo como referencia, sin
//         copiar código): el TLV en 0x0020_100C, el tamaño de la Flash en
//         0xE004_3020, y que los sectores 2 y 3 de la información son el BSL.
//
// Lo que solo tiene UNA fuente está marcado [1F] y anotado en doc/todo.md
// (P-16, subpuntos), igual que con los STM32.
// =============================================================================
#ifndef STM32_SOC_MSP432P4_P4_DESC_H
#define STM32_SOC_MSP432P4_P4_DESC_H

#include <cstdint>
#include <cstdio>
#include <string>
#include "../../core/core_caps.h"
#include "../../pins/encapsulado.h"
#include "../../pins/nomenclatura.h"
#include "../mcu_desc.h"

namespace stm32 {
namespace p4 {

// --- El mapa de memoria [H] -------------------------------------------------
constexpr uint32_t FLASH_B      = 0x00000000u;   // Flash principal
constexpr uint32_t INFO_B       = 0x00200000u;   // Flash de información
constexpr uint32_t INFO_SIZE    = 0x00004000u;   // 16 KB: dos bancos de 8
constexpr uint32_t TLV_B        = 0x00201000u;   // el Device Descriptor
constexpr uint32_t SRAM_CODE_B  = 0x01000000u;   // la SRAM vista desde el I-bus
constexpr uint32_t ROM_B        = 0x02000000u;   // DriverLib en ROM
constexpr uint32_t ROM_SIZE     = 0x00008000u;   // 32 KB
constexpr uint32_t SRAM_B       = 0x20000000u;
constexpr uint32_t PERIPH_B     = 0x40000000u;
constexpr uint32_t PERIPH_SIZE  = 0x00020000u;

// Los periféricos [H, *_BASE]
constexpr uint32_t TIMER_A0_B = 0x40000000u, TIMER_A1_B = 0x40000400u,
                   TIMER_A2_B = 0x40000800u, TIMER_A3_B = 0x40000C00u;
constexpr uint32_t EUSCI_A0_B = 0x40001000u, EUSCI_A1_B = 0x40001400u,
                   EUSCI_A2_B = 0x40001800u, EUSCI_A3_B = 0x40001C00u;
constexpr uint32_t EUSCI_B0_B = 0x40002000u, EUSCI_B1_B = 0x40002400u,
                   EUSCI_B2_B = 0x40002800u, EUSCI_B3_B = 0x40002C00u;
constexpr uint32_t REF_A_B    = 0x40003000u, COMP_E0_B = 0x40003400u,
                   COMP_E1_B  = 0x40003800u, AES256_B  = 0x40003C00u,
                   CRC32_B    = 0x40004000u, RTC_C_B   = 0x40004400u,
                   WDT_A_B    = 0x40004800u, DIO_B     = 0x40004C00u,
                   PMAP_B     = 0x40005000u, CAPTIO0_B = 0x40005400u,
                   CAPTIO1_B  = 0x40005800u, TIMER32_B = 0x4000C000u,
                   DMA_B      = 0x4000E000u, PCM_B     = 0x40010000u,
                   CS_B       = 0x40010400u, PSS_B     = 0x40010800u,
                   FLCTL_B    = 0x40011000u, ADC14_B   = 0x40012000u;
// LOS TRES QUE VIVEN EN EL PPB, en la ventana que ARMv7-M deja al fabricante
// [H: PERIPH_BASE2 = 0xE000_0000]. RSTCTL está justo donde ST pone su DBGMCU,
// y es la razón de `IntegracionNucleo` (T0).
constexpr uint32_t RSTCTL_B      = 0xE0042000u;
constexpr uint32_t SYSCTL_B      = 0xE0043000u;
constexpr uint32_t SYSCTL_BOOT_B = 0xE0044000u;   // [H: SYSCTL_BASE + 0x1000]
constexpr uint32_t PPB_FAB_FIN   = 0xE0045000u;

// --- Las interrupciones [H, IRQn_Type] ---------------------------------------
enum Irq : unsigned {
    IRQ_PSS = 0, IRQ_CS = 1, IRQ_PCM = 2, IRQ_WDT_A = 3, IRQ_FPU = 4,
    IRQ_FLCTL = 5, IRQ_COMP_E0 = 6, IRQ_COMP_E1 = 7,
    IRQ_TA0_0 = 8, IRQ_TA0_N = 9, IRQ_TA1_0 = 10, IRQ_TA1_N = 11,
    IRQ_TA2_0 = 12, IRQ_TA2_N = 13, IRQ_TA3_0 = 14, IRQ_TA3_N = 15,
    IRQ_EUSCIA0 = 16, IRQ_EUSCIA1 = 17, IRQ_EUSCIA2 = 18, IRQ_EUSCIA3 = 19,
    IRQ_EUSCIB0 = 20, IRQ_EUSCIB1 = 21, IRQ_EUSCIB2 = 22, IRQ_EUSCIB3 = 23,
    IRQ_ADC14 = 24, IRQ_T32_INT1 = 25, IRQ_T32_INT2 = 26, IRQ_T32_INTC = 27,
    IRQ_AES256 = 28, IRQ_RTC_C = 29, IRQ_DMA_ERR = 30, IRQ_DMA_INT3 = 31,
    IRQ_DMA_INT2 = 32, IRQ_DMA_INT1 = 33, IRQ_DMA_INT0 = 34,
    IRQ_PORT1 = 35, IRQ_PORT2 = 36, IRQ_PORT3 = 37, IRQ_PORT4 = 38,
    IRQ_PORT5 = 39, IRQ_PORT6 = 40,
    N_IRQ = 41
};

// --- Los puertos -------------------------------------------------------------
// Once puertos de 8 bits: P1..P10 y PJ. El índice interno es 0..9 para P1..P10
// y 10 para PJ; es el que usan el encapsulado, el DIO y la nomenclatura.
constexpr unsigned N_PUERTOS = 11;
constexpr unsigned PUERTO_J  = 10;
constexpr unsigned N_PINES   = 8;

// --- Las frecuencias de los osciladores internos [SYS] ------------------------
constexpr double HZ_VLO     = 9400.0;
constexpr double HZ_REFO_L  = 32768.0;
constexpr double HZ_REFO_H  = 128000.0;
constexpr double HZ_MODOSC  = 25.0e6;
constexpr double HZ_SYSOSC  = 5.0e6;
// El centro del DCO: 1,5 MHz << DCORSEL [SYS: SystemCoreClockUpdate].
constexpr double HZ_DCO_BASE = 1.5e6;

// --- Los topes [SA, tabla 2] ---------------------------------------------------
//   VCORE0: 16 MHz sin estado de espera, 24 MHz con uno, y 24 MHz es el máximo
//   VCORE1: 24 MHz sin estado de espera, 48 MHz con uno, y 48 MHz es el máximo
// (La versión de system_msp432p401r.c del mbed dice en un comentario «BANK0
// VCORE1 max is 16 MHz» por estado de espera; es de una revisión anterior del
// silicio y contradice a SLAA668A, que es posterior. Se sigue a SLAA668A y la
// discrepancia queda anotada: P-16 c.)
inline double tope_mclk(unsigned vcore) { return vcore ? 48e6 : 24e6; }
inline double tope_flash(unsigned vcore, unsigned wait) {
    const double paso = vcore ? 24e6 : 16e6;
    if (wait == 0) return paso;
    return vcore ? 48e6 : 24e6;   // con 1 o más, el tope es el del VCORE
}

// --- La identidad, en el TLV [OO + H] ----------------------------------------
// 0xA000 es el primero de los identificadores que OpenOCD asigna al P401x.
// [1F] para la revisión: no hay segunda fuente a mano (P-16 d).
constexpr uint32_t DEVICE_ID_P401R = 0x0000A000u;
constexpr uint32_t HWREV_P401R     = 0x00000043u;   // 'C'

} // namespace p4

// =============================================================================
// EL ENCAPSULADO: el LQFP100 (PZ), que es el de la MSP-EXP432P401R
//
// 84 E/S: P1..P9 completos (72), P10.0..P10.5 (6) y PJ.0..PJ.5 (6). Las
// máscaras son de 8 bits por puerto, en los índices de la nomenclatura del P4.
// =============================================================================
inline constexpr Encapsulado ENC_P4_LQFP100 {
    "LQFP100", 100, 84,
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // P1..P9
      0x3F,                                                   // P10.0-5
      0x3F },                                                 // PJ.0-5
    true
};

// =============================================================================
// LA NOMENCLATURA: `P1.0`…`P10.7` y `PJ.0`…`PJ.5`, y su alias `P1_0`
//
// Es la del datasheet y la de las serigrafías de la LaunchPad. La forma con
// barra baja es la que se usa en código (`GPIO_PORT_P1, GPIO_PIN0` no, pero sí
// los nombres de los ejemplos: `P1_0`). La canónica, la que sale en volcados,
// es la del datasheet.
// =============================================================================
namespace p4 {
inline bool lee_numero(const std::string& s, unsigned& v) {
    if (s.empty() || s.size() > 2) return false;
    if (s.size() > 1 && s[0] == '0') return false;           // "P01.0" no
    v = 0;
    for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + unsigned(c - '0'); }
    return true;
}
inline bool pad_p4(const std::string& t, unsigned& puerto, unsigned& pin) {
    if (t.size() < 4 || t[0] != 'P') return false;
    size_t sep = t.find('.');
    if (sep == std::string::npos) sep = t.find('_');
    if (sep == std::string::npos || sep < 2 || sep + 2 != t.size()) return false;
    const std::string pu = t.substr(1, sep - 1);
    const char c = t[sep + 1];
    if (c < '0' || c > '7') return false;
    const unsigned pi = unsigned(c - '0');
    unsigned idx;
    if (pu == "J") {
        idx = PUERTO_J;
        if (pi > 5) return false;                 // PJ.0..PJ.5
    } else {
        unsigned n = 0;
        if (!lee_numero(pu, n) || n < 1 || n > 10) return false;
        idx = n - 1;
    }
    puerto = idx;
    pin    = pi;
    return true;
}
inline std::string canonico_p4(unsigned puerto, unsigned pin) {
    char b[8];
    if (puerto == PUERTO_J) std::snprintf(b, sizeof b, "PJ.%u", pin);
    else                    std::snprintf(b, sizeof b, "P%u.%u", puerto + 1, pin);
    return b;
}
// Las patillas que el chip registra además de las de puerto. RSTN es la
// RSTn/NMI del datasheet: la barra no se puede usar en un nombre de nodo,
// porque separa la placa del nombre en un <sistema>.
inline const char* const ALIM_P4[] = {
    "DVCC", "DVSS", "AVCC", "AVSS", "VCORE", "RSTN", "SWCLKTCK", "SWDIOTMS",
    nullptr
};
} // namespace p4

inline const Nomenclatura NOM_MSP432P4{
    "MSP432P4", &p4::pad_p4, &p4::canonico_p4, p4::ALIM_P4
};
REGISTRA_NOMENCLATURA(MSP432P4, &NOM_MSP432P4);

// =============================================================================
// EL DESCRIPTOR de un miembro de la familia
// =============================================================================
struct McuCapsP4 {
    const char*  nombre;
    CoreCaps     nucleo;
    uint32_t     flash_size;     // principal
    uint32_t     sram_size;
    uint32_t     device_id;      // el del TLV
    uint32_t     hwrev;
    Encapsulado  enc;
};

// El núcleo: Cortex-M4F r0p1 con 3 bits de prioridad, MPU y FPU [H:
// __CM4_REV = 0x0001, __NVIC_PRIO_BITS = 3, __MPU_PRESENT, __FPU_PRESENT].
// Las 41 líneas de PSS_IRQn (0) a PORT6_IRQn (40).
inline constexpr CoreCaps CORE_MSP432P4{
    p4::N_IRQ, 3, 8, FpuKind::SimplePrec, CPUID_CORTEX_M4
};

inline constexpr McuCapsP4 MCU_MSP432P401R{
    "MSP432P401R", CORE_MSP432P4,
    0x00040000u,                 // 256 KB [H: SYSCTL_FLASH_SIZE lo lee el chip]
    0x00010000u,                 // 64 KB
    p4::DEVICE_ID_P401R, p4::HWREV_P401R,
    ENC_P4_LQFP100
};

// Al catálogo común [soc/mcu_desc.h], con la familia por la que despacha la
// factoría.
struct RegistroCatalogoP4 {
    RegistroCatalogoP4() {
        CatalogoMcu::registra(McuDesc{ MCU_MSP432P401R.nombre, "MSP432P4",
                                       &MCU_MSP432P401R.enc, &MCU_MSP432P401R });
    }
};
inline const RegistroCatalogoP4 g_registro_catalogo_p4{};

} // namespace stm32
#endif // STM32_SOC_MSP432P4_P4_DESC_H
