// =============================================================================
// f3_desc.h — EL STM32F303VC como dato (fase S1 del F303)
//
// Lo que `sim` y el SoC necesitan saber del chip, y nada más: el mapa de
// memoria, los números de interrupción, el DEV_ID, el encapsulado con sus
// funciones alternativas (generados de la base de pines de ST), y el
// descriptor `F3Caps` con el que se da de alta en el catálogo.
//
// LA FAMILIA ES "STM32F3", no "STM32F4": el F303 no cabe en un `McuCaps` del
// F4 sin rellenarlo de ceros —otro RCC, otra Flash, otros buses— y lleva el
// suyo, como los MSP432. [doc/analisis_stm32f303.md, §1.1 y §3.4]
//
// FUENTES, por orden de peso:
//   [CMSIS]   stm32f303xc.h de cmsis_device_f3 (Apache-2.0), vendida en
//             verif/fw/stm32f3/sdk/: direcciones, IRQn, campos de registro
//   [PINDATA] la base de pines de ST (BSD-3), en soc/stm32f3/pindata/
//   [HAL]     stm32f3xx_hal_driver (BSD-3): valores de las llaves, plazos
//   [1F]      una sola fuente o un valor típico, dicho en el sitio
// =============================================================================
#ifndef STM32_SOC_STM32F3_F3_DESC_H
#define STM32_SOC_STM32F3_F3_DESC_H

#include <cstdint>
#include "../../core/core_caps.h"
#include "../../pins/encapsulado.h"
#include "../mcu_desc.h"

namespace stm32 {
namespace f3 {

// --- El mapa de memoria [CMSIS] ----------------------------------------------
constexpr uint32_t FLASH_B    = 0x08000000u;
constexpr uint32_t PAGINA     = 0x00000800u;    // 2 KB [HAL: FLASH_PAGE_SIZE]
constexpr uint32_t CCM_B      = 0x10000000u;
constexpr uint32_t SYSMEM_B   = 0x1FFFD800u;    // 8 KB: el cargador de ST [stm32flash]
constexpr uint32_t SYSMEM_FIN = 0x1FFFF800u;
constexpr uint32_t UID_B      = 0x1FFFF7ACu;    // 96 bits [CMSIS: UID_BASE]
constexpr uint32_t TS_CAL1_A  = 0x1FFFF7B8u;    // 30 °C, VDDA = 3,3 V [HAL: TEMPSENSOR_CAL1_ADDR]
constexpr uint32_t VREFINT_A  = 0x1FFFF7BAu;    // [HAL: VREFINT_CAL_ADDR]
constexpr uint32_t TS_CAL2_A  = 0x1FFFF7C2u;    // 110 °C [HAL: TEMPSENSOR_CAL2_ADDR]
constexpr uint32_t F_SIZE_A   = 0x1FFFF7CCu;    // KB de Flash [CMSIS: FLASHSIZE_BASE]
constexpr uint32_t OB_B       = 0x1FFFF800u;    // bytes de opción [CMSIS: OB_BASE]
constexpr uint32_t OB_SIZE    = 0x10u;
constexpr uint32_t SRAM_B     = 0x20000000u;
// Los buses de periféricos
constexpr uint32_t APB1_B = 0x40000000u, APB1_FIN = 0x4000A000u;
constexpr uint32_t APB2_B = 0x40010000u, APB2_FIN = 0x40018000u;
constexpr uint32_t AHB1_B = 0x40020000u, AHB1_FIN = 0x40024400u;
constexpr uint32_t AHB2_B = 0x48000000u, AHB2_FIN = 0x48001800u;   // los GPIO
constexpr uint32_t AHB3_B = 0x50000000u, AHB3_FIN = 0x50000800u;   // los ADC
// Cada bloque [CMSIS]
constexpr uint32_t TIM2_B = 0x40000000u, TIM3_B = 0x40000400u, TIM4_B = 0x40000800u;
constexpr uint32_t TIM6_B = 0x40001000u, TIM7_B = 0x40001400u;
constexpr uint32_t RTC_B = 0x40002800u, WWDG_B = 0x40002C00u, IWDG_B = 0x40003000u;
constexpr uint32_t I2S2EXT_B = 0x40003400u, SPI2_B = 0x40003800u, SPI3_B = 0x40003C00u;
constexpr uint32_t I2S3EXT_B = 0x40004000u;
constexpr uint32_t USART2_B = 0x40004400u, USART3_B = 0x40004800u;
constexpr uint32_t UART4_B = 0x40004C00u, UART5_B = 0x40005000u;
constexpr uint32_t I2C1_B = 0x40005400u, I2C2_B = 0x40005800u;
constexpr uint32_t USB_B = 0x40005C00u, USB_PMA_B = 0x40006000u, CAN_B = 0x40006400u;
constexpr uint32_t PWR_B = 0x40007000u, DAC1_B = 0x40007400u;
constexpr uint32_t SYSCFG_B = 0x40010000u, EXTI_B = 0x40010400u;
constexpr uint32_t TIM1_B = 0x40012C00u, SPI1_B = 0x40013000u, TIM8_B = 0x40013400u;
constexpr uint32_t USART1_B = 0x40013800u;
constexpr uint32_t TIM15_B = 0x40014000u, TIM16_B = 0x40014400u, TIM17_B = 0x40014800u;
constexpr uint32_t DMA1_B = 0x40020000u, DMA2_B = 0x40020400u, RCC_B = 0x40021000u;
constexpr uint32_t FLASH_R = 0x40022000u, CRC_B = 0x40023000u, TSC_B = 0x40024000u;
constexpr uint32_t GPIOA_B = 0x48000000u;
constexpr uint32_t ADC12_B = 0x50000000u, ADC34_B = 0x50000400u;

// --- Las interrupciones [CMSIS: IRQn_Type] ------------------------------------
enum Irq : unsigned {
    IRQ_WWDG = 0, IRQ_PVD = 1, IRQ_TAMP_STAMP = 2, IRQ_RTC_WKUP = 3, IRQ_FLASH = 4,
    IRQ_RCC = 5, IRQ_EXTI0 = 6, IRQ_EXTI1 = 7, IRQ_EXTI2_TSC = 8, IRQ_EXTI3 = 9,
    IRQ_EXTI4 = 10, IRQ_DMA1_CH1 = 11, IRQ_ADC1_2 = 18, IRQ_USB_HP_CAN_TX = 19,
    IRQ_USB_LP_CAN_RX0 = 20, IRQ_CAN_RX1 = 21, IRQ_CAN_SCE = 22, IRQ_EXTI9_5 = 23,
    IRQ_TIM1_BRK_TIM15 = 24, IRQ_TIM1_UP_TIM16 = 25, IRQ_TIM1_TRG_COM_TIM17 = 26,
    IRQ_TIM1_CC = 27, IRQ_TIM2 = 28, IRQ_TIM3 = 29, IRQ_TIM4 = 30,
    IRQ_I2C1_EV = 31, IRQ_I2C1_ER = 32, IRQ_I2C2_EV = 33, IRQ_I2C2_ER = 34,
    IRQ_SPI1 = 35, IRQ_SPI2 = 36, IRQ_USART1 = 37, IRQ_USART2 = 38, IRQ_USART3 = 39,
    IRQ_EXTI15_10 = 40, IRQ_RTC_ALARM = 41, IRQ_USB_WAKEUP = 42,
    IRQ_TIM8_BRK = 43, IRQ_TIM8_UP = 44, IRQ_TIM8_TRG_COM = 45, IRQ_TIM8_CC = 46,
    IRQ_ADC3 = 47, IRQ_SPI3 = 51, IRQ_UART4 = 52, IRQ_UART5 = 53, IRQ_TIM6_DAC = 54,
    IRQ_TIM7 = 55, IRQ_DMA2_CH1 = 56, IRQ_ADC4 = 61, IRQ_COMP1_2_3 = 64,
    IRQ_COMP4_5_6 = 65, IRQ_COMP7 = 66, IRQ_USB_HP = 74, IRQ_USB_LP = 75,
    IRQ_USB_WAKEUP_RMP = 76, IRQ_FPU = 81,
    N_IRQ = 82
};

// --- Identificación ------------------------------------------------------------
// DEV_ID 0x422: el mismo die que el F302xB/C y el F358 [stm32flash: dev_table.c;
// OpenOCD: stm32f1x.c]. REV_ID 0x1003 es la revisión «Y», la que se vende hoy
// [RM0316 §33.6.1: 0x1001 la Z, 0x1003 la Y; UM1570: la Discovery lleva la Y].
constexpr uint16_t DEV_ID = 0x422;
constexpr uint16_t REV_ID = 0x1003;

// --- Relojes ------------------------------------------------------------------
constexpr double HZ_HSI = 8.0e6;      // [HAL: HSI_VALUE]
constexpr double HZ_LSI = 40.0e3;     // [HAL: LSI_VALUE]
constexpr double HZ_MAX = 72.0e6;     // SYSCLK, HCLK y PCLK2 [PINDATA: Frequency]
constexpr double HZ_APB1_MAX = 36.0e6;    // [HAL: RCC_ClkInitTypeDef] [DS9118, tabla 24]
// Los estados de espera de la Flash: 0 hasta 24 MHz, 1 hasta 48, 2 hasta 72
// [HAL: comentarios de FLASH_LATENCY_x] [DS9118, §3.3 y §6.3.5]
inline unsigned latencia_minima(double hz) {
    if (hz <= 24.0e6 * (1 + 1e-9)) return 0;
    if (hz <= 48.0e6 * (1 + 1e-9)) return 1;
    return 2;
}

// --- La calibración de fábrica -----------------------------------------------
// Valores TÍPICOS, no de un chip concreto, y así se dice en las limitaciones:
// V25 = 1,43 V y 4,3 mV/°C de pendiente media [DS9118, tabla 78], y VREFINT =
// 1,23 V [DS9118, tabla 28], todo a VDDA = 3,3 V y 12 bits. Las direcciones,
// las de las tablas 29 y 79 de DS9118.
constexpr uint16_t TS_CAL1 = 1748;    // 30 °C:  (1,43 - 5 * 0,0043) / 3,3 * 4095
constexpr uint16_t TS_CAL2 = 1321;    // 110 °C: (1,43 - 85 * 0,0043) / 3,3 * 4095
constexpr uint16_t VREFINT_CAL = 1526;    // 1,23 / 3,3 * 4095
// El identificador único: un valor del modelo, fijo para que las suites sean
// reproducibles. En el silicio son las coordenadas en la oblea, el número de
// oblea y el lote [RM0316 §34.1]: X e Y en BCD, el número de oblea y el lote en ASCII
constexpr uint32_t UID[3] = { 0x00330024u, 0x3333470Fu, 0x31373939u };

} // namespace f3

// --- El encapsulado, generado [soc/stm32f3/f3_patillas.inc, PINDATA] ----------
// Las cien patillas, por posición, y la máscara de lo que sale: 87 E/S, A..E
// completos y de F solo PF0, PF1, PF2, PF4, PF6, PF9 y PF10. OSC_IN y OSC_OUT
// son PF0 y PF1, no PH0 y PH1 como en el F4.
struct PatillaF3 { unsigned pos; const char* nombre; const char* tipo; };
inline constexpr PatillaF3 PATILLAS_F303[] = {
#define F3_PATILLA(p, n, t) { p, n, t },
#include "f3_patillas.inc"
};
inline constexpr unsigned N_PATILLAS_F303 = sizeof(PATILLAS_F303) / sizeof(PATILLAS_F303[0]);

inline constexpr Encapsulado ENC_LQFP100_F303 {
    "LQFP100", 100, F3_N_GPIO_LQFP100,
    { F3_MASCARA_LQFP100 },
    true
};

// =============================================================================
// EL DESCRIPTOR de un miembro de la familia
// =============================================================================
struct F3Caps {
    const char* nombre;
    CoreCaps    nucleo;
    uint32_t    flash_size;
    uint32_t    sram_size;
    uint32_t    ccm_size;
    uint16_t    dev_id, rev_id;
    Encapsulado enc;
    uint32_t idcode() const { return (uint32_t(rev_id) << 16) | dev_id; }
};

// El núcleo: Cortex-M4F r0p1, 82 posiciones, 4 bits de prioridad, MPU y FPU
// [CMSIS: __CM4_REV 0x0001, __NVIC_PRIO_BITS 4, __MPU_PRESENT, __FPU_PRESENT]
inline constexpr CoreCaps CORE_STM32F303 = core_m4f_st(f3::N_IRQ);

inline constexpr F3Caps MCU_STM32F303VC{
    "STM32F303VC", CORE_STM32F303,
    0x00040000u,                 // 256 KB [PINDATA]
    0x0000A000u,                 // 40 KB [PINDATA]
    0x00002000u,                 // 8 KB de CCM [CMSIS: CCMDATARAM_BASE; RM0316 §3.3]
    f3::DEV_ID, f3::REV_ID,
    ENC_LQFP100_F303
};

struct RegistroCatalogoF3 {
    RegistroCatalogoF3() {
        CatalogoMcu::registra(McuDesc{ MCU_STM32F303VC.nombre, "STM32F3",
                                       &MCU_STM32F303VC.enc, &MCU_STM32F303VC });
    }
};
inline const RegistroCatalogoF3 g_registro_catalogo_f3{};

} // namespace stm32
#endif // STM32_SOC_STM32F3_F3_DESC_H
