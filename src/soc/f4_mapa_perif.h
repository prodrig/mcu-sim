// =============================================================================
// f4_mapa_perif.h — DÓNDE ESTÁ CADA PERIFÉRICO en la familia STM32F405/407
//
// La otra mitad de lo que define una familia, junto con el mapa de funciones
// alternativas (`f4_mapa_af.h`) y el árbol de reloj. Aquí están las direcciones
// base de los periféricos, la geometría de la Flash, cuántas líneas de
// interrupción hay y los topes de frecuencia por dominio.
//
// POR QUÉ SE SEPARÓ DE `common/ahb_types.h` (fase 1 del plan del F446). Ese
// fichero mezclaba dos capas que envejecen a ritmos muy distintos:
//
//   * lo que es ARQUITECTURA ARMv7-M o del mapa de memoria de cualquier STM32
//     —las regiones, el PPB, el bit-banding, `0x0800_0000` para la Flash y
//     `0x2000_0000` para la SRAM—, que **no cambia nunca**; y
//   * lo que es DE ESTA FAMILIA —que el TIM2 esté en `0x4000_0000` y el RNG en
//     `0x5006_0800`—, que cambia con el chip. En un F446 no hay RNG y en
//     `0x4000_4000` hay un SPDIF-RX donde aquí está el I2S3ext
//     [doc/stm32f4xx/stm32f407vg_vs_446re.md, §5.3].
//
// Lo primero se queda en `ahb_types.h`; lo segundo es esto.
//
// CÓMO SE INCLUYE, Y POR QUÉ ASÍ. Este fichero no se incluye solo: lo incluye
// `ahb_types.h` al final, reabriendo el `namespace addr`. Eso hace que el
// cambio sea invisible para los cuarenta ficheros que ya usaban `addr::TIM2_B`
// —ninguno ha tenido que tocar un include— y deja el punto exacto donde el día
// de mañana se elige OTRO mapa. Es el mismo andamio de compatibilidad que se
// dejó al partir `crc_rng.h`.
// =============================================================================
#ifndef STM32_SOC_F4_MAPA_PERIF_H
#define STM32_SOC_F4_MAPA_PERIF_H

// Se incluye DESDE ahb_types.h, con `namespace stm32 { namespace addr {`
// abierto. No lleva includes propios a proposito: no es una cabecera
// autonoma, es la mitad de otra.

// APB1 [IR, §15.1]
constexpr uint32_t TIM2_B = 0x40000000, TIM3_B = 0x40000400, TIM4_B = 0x40000800;
constexpr uint32_t TIM5_B = 0x40000C00, TIM6_B = 0x40001000, TIM7_B = 0x40001400;
constexpr uint32_t TIM12_B = 0x40001800, TIM13_B = 0x40001C00, TIM14_B = 0x40002000;
constexpr uint32_t RTC_B = 0x40002800, WWDG_B = 0x40002C00, IWDG_B = 0x40003000;
constexpr uint32_t SPI2_B = 0x40003800, SPI3_B = 0x40003C00;
// Bloques de extension del I2S full-duplex [IR, mapa de perifericos APB1]
constexpr uint32_t I2S2EXT_B = 0x40003400, I2S3EXT_B = 0x40004000;
constexpr uint32_t USART2_B = 0x40004400, USART3_B = 0x40004800;
constexpr uint32_t UART4_B = 0x40004C00, UART5_B = 0x40005000;
constexpr uint32_t I2C1_B = 0x40005400, I2C2_B = 0x40005800, I2C3_B = 0x40005C00;
constexpr uint32_t CAN1_B = 0x40006400, CAN2_B = 0x40006800;
constexpr uint32_t PWR_B = 0x40007000, DAC_B = 0x40007400;
// APB2
constexpr uint32_t TIM1_B = 0x40010000, TIM8_B = 0x40010400;
constexpr uint32_t USART1_B = 0x40011000, USART6_B = 0x40011400;
constexpr uint32_t ADC_B = 0x40012000;      // ADC1/2/3 + común (0x300)
constexpr uint32_t SDIO_B = 0x40012C00, SPI1_B = 0x40013000;
constexpr uint32_t SYSCFG_B = 0x40013800, EXTI_B = 0x40013C00;
constexpr uint32_t TIM9_B = 0x40014000, TIM10_B = 0x40014400, TIM11_B = 0x40014800;
// AHB1
constexpr uint32_t GPIOA_B = 0x40020000;    // GPIOx = GPIOA_B + 0x400*x
constexpr uint32_t CRC_B = 0x40023000, RCC_B = 0x40023800, FLASHIF_B = 0x40023C00;
constexpr uint32_t DMA1_B = 0x40026000, DMA2_B = 0x40026400;
constexpr uint32_t ETH_B = 0x40028000, OTG_HS_B = 0x40040000;
// AHB2
constexpr uint32_t OTG_FS_B = 0x50000000, DCMI_B = 0x50050000, RNG_B = 0x50060800;
// El acelerador criptográfico del F415/F417, en el mismo kilobyte que el RNG y
// justo delante de él. En un F405/F407 estas dos ventanas están RESERVADAS: no
// las decodifica nadie y tocarlas da error de bus, que es lo que hace el
// silicio. [RM0090 Rev 22, tabla 1]
constexpr uint32_t CRYP_B = 0x50060000, HASH_B = 0x50060400;

} // namespace addr

// ---------------------------------------------------------------------------
// Sectores de la Flash principal [IR, §5.2.1]
// ---------------------------------------------------------------------------
struct FlashSector { uint32_t base; uint32_t size; };
constexpr unsigned N_FLASH_SECTORS = 12;
constexpr FlashSector FLASH_SECTORS[N_FLASH_SECTORS] = {
    {0x08000000, 0x04000}, {0x08004000, 0x04000},   // 0,1  : 16 KB
    {0x08008000, 0x04000}, {0x0800C000, 0x04000},   // 2,3  : 16 KB
    {0x08010000, 0x10000},                          // 4    : 64 KB
    {0x08020000, 0x20000}, {0x08040000, 0x20000},   // 5,6  : 128 KB
    {0x08060000, 0x20000}, {0x08080000, 0x20000},   // 7,8
    {0x080A0000, 0x20000}, {0x080C0000, 0x20000},   // 9,10
    {0x080E0000, 0x20000}                           // 11
};
// Devuelve el nº de sector que contiene la dirección, o -1.
inline int flash_sector_of(uint32_t a) {
    for (unsigned s = 0; s < N_FLASH_SECTORS; ++s)
        if (a >= FLASH_SECTORS[s].base && a < FLASH_SECTORS[s].base + FLASH_SECTORS[s].size)
            return int(s);
    return -1;
}

// Estados de espera mínimos de la Flash para VDD = 2.7..3.6 V [IR, §5.2.2].
// Devuelve la LATENCY mínima legal para una frecuencia HCLK dada.
//
// Esto es la curva DEL F407, y por eso sigue aquí. La que usa el modelo es la
// del chip que se esté montando (`MapaFlash::latencia_minima`, en
// mem/mem_caps.h), que es la misma regla —un estado de espera más cada 30 MHz—
// con los dos números como dato: el escalón y el techo. Cambian con el chip y
// con el rango de VDD, y por eso no podían quedarse escritos en una función.
inline unsigned flash_min_latency(double hclk_hz) {
    const double f = hclk_hz;
    if (f <=  30e6) return 0;
    if (f <=  60e6) return 1;
    if (f <=  90e6) return 2;
    if (f <= 120e6) return 3;
    if (f <= 150e6) return 4;
    return 5;                    // 150 < HCLK <= 168 MHz
}

// Nº de líneas de interrupción externas del NVIC en el F407 [IR, §9.1.2]
constexpr unsigned N_IRQ = 82;
// Líneas EXTI [IR, §9.4.1]
constexpr unsigned N_EXTI = 23;

// Frecuencias máximas por dominio [IR, §4.4]
constexpr double F_SYSCLK_MAX = 168e6;
constexpr double F_HCLK_MAX   = 168e6;
constexpr double F_PCLK1_MAX  =  42e6;
constexpr double F_PCLK2_MAX  =  84e6;

#endif // STM32_SOC_F4_MAPA_PERIF_H
