// =============================================================================
// c6_desc.h — El ESP32-C6: sus núcleos, su mapa, sus encapsulados y sus nombres
//
// E9 de doc/analisis_esp32_riscv.md. Lo que es del C6 y no del núcleo
// genérico ni de la capa de Espressif.
//
// EL NÚCLEO HP [TRM-C6 §1]: RV32IMAC con M y U y la delegación de
// interrupciones a U (los CSR de la extensión N: ustatus, uie, utvec,
// uscratch, uepc, ucause, uip y uret; misa no dice N), mie/mip de 32 bits
// (los ID 0-31: 0 USI, 3 MSI, 4 UTI y 7 MTI del CLINT, y 28 externas),
// mideleg 0x111 de reset (USI, UTI y la externa 8), mtvec y utvec solo
// vectorizados con base alineada a 256, 16 regiones PMP y 16 de PMA (CSR
// propios, soc/esp/pma_esp.h), sin contadores estándar (mpcer/mpcmr/mpccr),
// desalineados en hardware con la errata CPU-863 (también en el v0.2, con
// el PMP y la PMA) [ER-C6 §3.2], mvendorid 0x612, marchid 0x8000_0002,
// mimpid 2. Depuración: DM 0.13 con
// program buffer de 16, 4 triggers y tcontrol. El BTB con predicción
// estática: los tiempos son de modelo, calibrados con el CoreMark/MHz del
// datasheet (3,10).
// =============================================================================
#ifndef STM32_SOC_ESP32C6_C6_DESC_H
#define STM32_SOC_ESP32C6_C6_DESC_H

#include <cstdio>
#include <string>
#include "../mcu_desc.h"
#include "../../pins/encapsulado.h"
#include "../../pins/nomenclatura.h"
#include "../../rv/hart/rv_caps.h"
#include "../../periph/esp/gpio_esp.h"

namespace stm32 {

namespace c6 {
// --- Memoria [TRM-C6 tabla 5.3-1]: un solo espacio para los buses I y D ----------
constexpr uint32_t ROM_B = 0x40000000u, ROM_FIN = 0x40050000u;      // 320 KB
constexpr uint32_t HP_SRAM = 0x40800000u, HP_SRAM_TAM = 512u * 1024u;
constexpr uint32_t LP_SRAM = 0x50000000u, LP_SRAM_TAM = 16u * 1024u;
constexpr uint32_t FLASH_B = 0x42000000u, FLASH_TAM = 16u << 20;    // por la caché
constexpr uint32_t PERIF = 0x60000000u, PERIF_FIN = 0x600D0000u;
// La región de la CPU: el DM, el PLIC (MX y UX) y el CLINT [TRM-C6 §1.4, §1.7; reg_base.h]
constexpr uint32_t CPU_B = 0x20000000u, PLIC_B = 0x20001000u, CLINT_B = 0x20001800u, CLINT_FIN = 0x20002000u;
// --- Periféricos [TRM-C6 tabla 5.3-2; reg_base.h de ESP-IDF] -----------------------
constexpr uint32_t UART0_B = 0x60000000u, UART1_B = 0x60001000u, SPI0_B = 0x60002000u,
                   SPI1_B = 0x60003000u, I2C_B = 0x60004000u, UHCI_B = 0x60005000u,
                   RMT_B = 0x60006000u, LEDC_B = 0x60007000u, TIMG0_B = 0x60008000u,
                   TIMG1_B = 0x60009000u, SYSTIMER_B = 0x6000A000u, TWAI0_B = 0x6000B000u,
                   I2S_B = 0x6000C000u, TWAI1_B = 0x6000D000u, ADC_B = 0x6000E000u,
                   USB_SJ_B = 0x6000F000u, INTMTX_B = 0x60010000u, ATOMIC_B = 0x60011000u,
                   PCNT_B = 0x60012000u, ETM_B = 0x60013000u, MCPWM_B = 0x60014000u,
                   PARL_IO_B = 0x60015000u, HINF_B = 0x60016000u, SLC_B = 0x60017000u,
                   SLCHOST_B = 0x60018000u, PVT_B = 0x60019000u,
                   GDMA_B = 0x60080000u, SPI2_B = 0x60081000u, AES_B = 0x60088000u,
                   SHA_B = 0x60089000u, RSA_B = 0x6008A000u, ECC_B = 0x6008B000u,
                   DS_B = 0x6008C000u, HMAC_B = 0x6008D000u, IO_MUX_B = 0x60090000u,
                   GPIO_B = 0x60091000u, MEM_MON_B = 0x60092000u, PAU_B = 0x60093000u,
                   HP_SYS_B = 0x60095000u, PCR_B = 0x60096000u, TEE_B = 0x60098000u,
                   HP_APM_B = 0x60099000u, LP_APM0_B = 0x60099800u, MISC_B = 0x6009F000u,
                   MODEM_SYSCON_B = 0x600A9800u, MODEM_LPCON_B = 0x600AF000u, I2C_ANA_MST_B = 0x600AF800u,
                   PMU_B = 0x600B0000u, LP_CLKRST_B = 0x600B0400u, EFUSE_B = 0x600B0800u,
                   LP_TIMER_B = 0x600B0C00u, LP_AON_B = 0x600B1000u, LP_UART_B = 0x600B1400u,
                   LP_I2C_B = 0x600B1800u, LP_WDT_B = 0x600B1C00u, LP_IO_B = 0x600B2000u,
                   LP_I2C_ANA_B = 0x600B2400u, LPPERI_B = 0x600B2800u, LP_ANA_B = 0x600B2C00u,
                   LP_TEE_B = 0x600B3400u, LP_APM_B = 0x600B3800u, OTP_DBG_B = 0x600B3C00u,
                   TRACE_B = 0x600C0000u, ASSIST_B = 0x600C2000u, INTPRI_B = 0x600C5000u,
                   EXTMEM_B = 0x600C8000u;
// --- Fuentes de interrupción [TRM-C6 tabla 10.3-1] -----------------------------------
constexpr unsigned N_FUENTES = 77;
constexpr unsigned F_PMU = 13, F_EFUSE = 14, F_LP_RTC_TIMER = 15, F_LP_UART = 16, F_LP_I2C = 17,
                   F_LP_WDT = 18, F_FROM_CPU0 = 22, F_ASSIST = 26, F_GPIO = 30,
                   F_MSPI = 40, F_I2S = 41, F_UHCI0 = 42, F_UART0 = 43, F_UART1 = 44, F_LEDC = 45,
                   F_TWAI0 = 46, F_TWAI1 = 47, F_USB_SJ = 48, F_RMT = 49, F_I2C = 50,
                   F_TG0_T0 = 51, F_TG0_WDT = 53, F_TG1_T0 = 54, F_TG1_WDT = 56,
                   F_SYSTIMER0 = 57, F_ADC = 60, F_MCPWM = 61, F_PCNT = 62, F_PARL_IO = 63,
                   F_DMA_IN0 = 66, F_DMA_OUT0 = 69, F_SPI2 = 72, F_AES = 73, F_SHA = 74,
                   F_RSA = 75, F_ECC = 76;
// --- Señales de la matriz GPIO [gpio_sig_map.h de ESP-IDF; TRM-C6 §7.11] -------------
constexpr unsigned SIG_U0RXD = 6, SIG_U0TXD = 6, SIG_U1RXD = 9, SIG_U1TXD = 9,
                   SIG_LEDC0 = 0, SIG_I2C_SCL = 45, SIG_I2C_SDA = 46, SIG_FSPICLK = 63,
                   SIG_RMT0 = 71, SIG_TWAI0 = 73, SIG_TWAI1 = 77, SIG_FSPIQ = 64, SIG_FSPID = 65,
                   SIG_FSPICS0 = 68, SIG_I2SO_SD1 = 18;
constexpr unsigned N_GPIO = 31;
} // namespace c6

// --- El núcleo HP ------------------------------------------------------------------
// [de modelo] salvo lat_irq (4 ciclos, TRM-C6 §1.6.3.1): como el C3, con un
// BTB de 32 entradas
inline constexpr RvTiempos RV_TIEMPOS_C6_HP{1, 1, 1, 1, 3, 2, 3, 1, 17, 1, 1, 3, 3, 4, 32};
inline constexpr RvDebugCaps RV_DBG_C6_HP{4, true, 16, 2, false, true, 0x4, 0x20000000u, 0x00001000u};
inline constexpr RvCaps RV_ESP32C6_HP{
    "ESP32-C6 HP", 32,
    RV_EXT_I | RV_EXT_M | RV_EXT_A | RV_EXT_C | RV_EXT_U,
    RV_Z_ICSR | RV_Z_IFENCEI,
    true, 0xFFFFFFFFu, true, 0x111u, RV_MTVEC_VECTORIZADO, 256,
    0x40000000u, 0x00000001u, 16, false, RvDesalineado::HardwareCpu863,
    0x612u, 0x80000002u, 2u, false, RV_DBG_C6_HP, &RV_TIEMPOS_C6_HP
};

// --- Los pines: función 0 y configuración tras el reset [TRM-C6 tabla 7.12-1] ------
// Reset: 0 IE = 0; 1 IE; 3 IE y WPU; 4 OE y WPU (U0TXD). MTCK con pull-up
// mientras DIS_PAD_JTAG = 0. GPIO12/13 son el USB (D-/D+).
inline const std::vector<PinEsp>& pines_c6() {
    using T = PinEsp;
    static const std::vector<PinEsp> v{
        {"GPIO0", T::Gpio, -1, false, false, false, 2},
        {"GPIO1", T::Gpio, -1, false, false, false, 2},
        {"GPIO2", T::Gpio, -1, true, false, false, 2},
        {"GPIO3", T::Gpio, -1, true, false, false, 2},
        {"MTMS", T::NoModelada, -1, true, false, false, 2},
        {"MTDI", T::NoModelada, -1, true, false, false, 2},
        {"MTCK", T::NoModelada, -1, true, true, false, 2},
        {"MTDO", T::NoModelada, -1, true, false, false, 2},
        {"GPIO8", T::Gpio, -1, true, false, false, 2},
        {"GPIO9", T::Gpio, -1, true, true, false, 2},
        {"GPIO10", T::Gpio, -1, true, false, false, 2},
        {"GPIO11", T::Gpio, -1, true, false, false, 2},
        {"GPIO12", T::Gpio, -1, true, false, false, 3},
        {"GPIO13", T::Gpio, -1, true, true, false, 3},
        {"GPIO14", T::Gpio, -1, true, false, false, 2},
        {"GPIO15", T::Gpio, -1, true, false, false, 2},
        {"U0TXD", T::SalidaDirecta, int(c6::SIG_U0TXD), false, true, false, 2},
        {"U0RXD", T::EntradaDirecta, int(c6::SIG_U0RXD), true, true, false, 2},
        {"SDIO_CMD", T::NoModelada, -1, true, true, false, 2},
        {"SDIO_CLK", T::NoModelada, -1, true, true, false, 2},
        {"SDIO_DATA0", T::NoModelada, -1, true, true, false, 2},
        {"SDIO_DATA1", T::NoModelada, -1, true, true, false, 2},
        {"SDIO_DATA2", T::NoModelada, -1, true, true, false, 2},
        {"SDIO_DATA3", T::NoModelada, -1, true, true, false, 2},
        {"SPICS0", T::NoModelada, -1, true, true, false, 2},
        {"SPIQ", T::NoModelada, -1, true, true, false, 2},
        {"SPIWP", T::NoModelada, -1, true, true, false, 2},
        {"GPIO27", T::Gpio, -1, false, false, false, 2},
        {"SPIHD", T::NoModelada, -1, true, true, false, 2},
        {"SPICLK", T::NoModelada, -1, true, true, false, 2},
        {"SPID", T::NoModelada, -1, true, true, false, 2},
    };
    return v;
}

// --- Encapsulados [DS-C6 tablas 2-1 y 2-2] -----------------------------------------
// QFN40: 30 GPIO (sin GPIO14). QFN32 (C6FH4/FH8, la Flash dentro): 22, sin
// GPIO10, GPIO11 ni los de la Flash (24-30).
inline constexpr Encapsulado ENC_C6_QFN40{"QFN40", 41, 30, {0x7FFFBFFFu}, true};
inline constexpr Encapsulado ENC_C6_QFN32{"QFN32", 33, 22, {0x00FFF3FFu}, true};

// --- El descriptor de un miembro de la familia --------------------------------------
struct McuCapsC6 {
    const char* nombre;
    const RvCaps* nucleo;
    uint32_t flash_interna;      // 0: la Flash va en la placa
    unsigned revision;           // 0 = v0.0, 1 = v0.1, 2 = v0.2
    Encapsulado enc;
};
inline constexpr McuCapsC6 MCU_ESP32C6{"ESP32-C6", &RV_ESP32C6_HP, 0, 2, ENC_C6_QFN40};
inline constexpr McuCapsC6 MCU_ESP32C6FH4{"ESP32-C6FH4", &RV_ESP32C6_HP, 4u << 20, 2, ENC_C6_QFN32};
inline constexpr McuCapsC6 MCU_ESP32C6FH8{"ESP32-C6FH8", &RV_ESP32C6_HP, 8u << 20, 2, ENC_C6_QFN32};

} // namespace stm32
#endif // STM32_SOC_ESP32C6_C6_DESC_H
