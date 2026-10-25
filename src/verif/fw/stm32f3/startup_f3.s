/* ===========================================================================
 * startup_f3.s — Arranque de los firmwares de verificacion del STM32F303VC
 *
 * GENERADO por gen_startup.py a partir de IRQn_Type de stm32f303xc.h: 16
 * excepciones de sistema + 82 IRQ. Cada IRQ tiene un manejador debil con el
 * nombre de CMSIS; los huecos de la numeracion van a Default_Handler.
 *
 * El Reset_Handler enciende la FPU, copia .data y .ccmtext (el codigo que se
 * ejecuta desde la CCM), borra .bss y llama a ENTRY_SYMBOL (main, o cm_start
 * en CoreMark). Sin SystemInit: el chip sale de reset con el HSI a 8 MHz.
 * ===========================================================================*/
    .syntax unified
    .cpu cortex-m4
    .fpu fpv4-sp-d16
    .thumb

    .section .isr_vector, "a", %progbits
    .global  g_vectors
g_vectors:
    .word  _estack
    .word  Reset_Handler
    .word  NMI_Handler
    .word  HardFault_Handler
    .word  MemManage_Handler
    .word  BusFault_Handler
    .word  UsageFault_Handler
    .word  0
    .word  0
    .word  0
    .word  0
    .word  SVC_Handler
    .word  DebugMon_Handler
    .word  0
    .word  PendSV_Handler
    .word  SysTick_Handler
    .word  WWDG_IRQHandler              /*  0 */
    .word  PVD_IRQHandler               /*  1 */
    .word  TAMP_STAMP_IRQHandler        /*  2 */
    .word  RTC_WKUP_IRQHandler          /*  3 */
    .word  FLASH_IRQHandler             /*  4 */
    .word  RCC_IRQHandler               /*  5 */
    .word  EXTI0_IRQHandler             /*  6 */
    .word  EXTI1_IRQHandler             /*  7 */
    .word  EXTI2_TSC_IRQHandler         /*  8 */
    .word  EXTI3_IRQHandler             /*  9 */
    .word  EXTI4_IRQHandler             /* 10 */
    .word  DMA1_Channel1_IRQHandler     /* 11 */
    .word  DMA1_Channel2_IRQHandler     /* 12 */
    .word  DMA1_Channel3_IRQHandler     /* 13 */
    .word  DMA1_Channel4_IRQHandler     /* 14 */
    .word  DMA1_Channel5_IRQHandler     /* 15 */
    .word  DMA1_Channel6_IRQHandler     /* 16 */
    .word  DMA1_Channel7_IRQHandler     /* 17 */
    .word  ADC1_2_IRQHandler            /* 18 */
    .word  USB_HP_CAN_TX_IRQHandler     /* 19 */
    .word  USB_LP_CAN_RX0_IRQHandler    /* 20 */
    .word  CAN_RX1_IRQHandler           /* 21 */
    .word  CAN_SCE_IRQHandler           /* 22 */
    .word  EXTI9_5_IRQHandler           /* 23 */
    .word  TIM1_BRK_TIM15_IRQHandler    /* 24 */
    .word  TIM1_UP_TIM16_IRQHandler     /* 25 */
    .word  TIM1_TRG_COM_TIM17_IRQHandler /* 26 */
    .word  TIM1_CC_IRQHandler           /* 27 */
    .word  TIM2_IRQHandler              /* 28 */
    .word  TIM3_IRQHandler              /* 29 */
    .word  TIM4_IRQHandler              /* 30 */
    .word  I2C1_EV_IRQHandler           /* 31 */
    .word  I2C1_ER_IRQHandler           /* 32 */
    .word  I2C2_EV_IRQHandler           /* 33 */
    .word  I2C2_ER_IRQHandler           /* 34 */
    .word  SPI1_IRQHandler              /* 35 */
    .word  SPI2_IRQHandler              /* 36 */
    .word  USART1_IRQHandler            /* 37 */
    .word  USART2_IRQHandler            /* 38 */
    .word  USART3_IRQHandler            /* 39 */
    .word  EXTI15_10_IRQHandler         /* 40 */
    .word  RTC_Alarm_IRQHandler         /* 41 */
    .word  USBWakeUp_IRQHandler         /* 42 */
    .word  TIM8_BRK_IRQHandler          /* 43 */
    .word  TIM8_UP_IRQHandler           /* 44 */
    .word  TIM8_TRG_COM_IRQHandler      /* 45 */
    .word  TIM8_CC_IRQHandler           /* 46 */
    .word  ADC3_IRQHandler              /* 47 */
    .word  Default_Handler              /* 48 */
    .word  Default_Handler              /* 49 */
    .word  Default_Handler              /* 50 */
    .word  SPI3_IRQHandler              /* 51 */
    .word  UART4_IRQHandler             /* 52 */
    .word  UART5_IRQHandler             /* 53 */
    .word  TIM6_DAC_IRQHandler          /* 54 */
    .word  TIM7_IRQHandler              /* 55 */
    .word  DMA2_Channel1_IRQHandler     /* 56 */
    .word  DMA2_Channel2_IRQHandler     /* 57 */
    .word  DMA2_Channel3_IRQHandler     /* 58 */
    .word  DMA2_Channel4_IRQHandler     /* 59 */
    .word  DMA2_Channel5_IRQHandler     /* 60 */
    .word  ADC4_IRQHandler              /* 61 */
    .word  Default_Handler              /* 62 */
    .word  Default_Handler              /* 63 */
    .word  COMP1_2_3_IRQHandler         /* 64 */
    .word  COMP4_5_6_IRQHandler         /* 65 */
    .word  COMP7_IRQHandler             /* 66 */
    .word  Default_Handler              /* 67 */
    .word  Default_Handler              /* 68 */
    .word  Default_Handler              /* 69 */
    .word  Default_Handler              /* 70 */
    .word  Default_Handler              /* 71 */
    .word  Default_Handler              /* 72 */
    .word  Default_Handler              /* 73 */
    .word  USB_HP_IRQHandler            /* 74 */
    .word  USB_LP_IRQHandler            /* 75 */
    .word  USBWakeUp_RMP_IRQHandler     /* 76 */
    .word  Default_Handler              /* 77 */
    .word  Default_Handler              /* 78 */
    .word  Default_Handler              /* 79 */
    .word  Default_Handler              /* 80 */
    .word  FPU_IRQHandler               /* 81 */

    .section .text.Reset_Handler, "ax", %progbits
    .global  Reset_Handler
    .type    Reset_Handler, %function
    .thumb_func
Reset_Handler:
    ldr   r0, =0xE000ED88          /* CPACR: CP10 y CP11 */
    ldr   r1, [r0]
    orr   r1, r1, #(0xF << 20)
    str   r1, [r0]
    dsb
    isb
    ldr   r0, =_sdata              /* .data de la Flash a la SRAM */
    ldr   r1, =_edata
    ldr   r2, =_sidata
1:  cmp   r0, r1
    bcs   2f
    ldr   r4, [r2], #4
    str   r4, [r0], #4
    b     1b
2:  ldr   r0, =_sccm               /* .ccmtext de la Flash a la CCM */
    ldr   r1, =_eccm
    ldr   r2, =_siccm
3:  cmp   r0, r1
    bcs   4f
    ldr   r4, [r2], #4
    str   r4, [r0], #4
    b     3b
4:  ldr   r0, =_sbss               /* .bss a cero */
    ldr   r1, =_ebss
    movs  r2, #0
5:  cmp   r0, r1
    bcs   6f
    str   r2, [r0], #4
    b     5b
6:
#ifndef ENTRY_SYMBOL
#define ENTRY_SYMBOL main
#endif
    bl    ENTRY_SYMBOL
7:  b     7b
    .size Reset_Handler, .-Reset_Handler

    .section .text.Default_Handler, "ax", %progbits
    .global  Default_Handler
    .type    Default_Handler, %function
    .thumb_func
Default_Handler:
    b     Default_Handler
    .size Default_Handler, .-Default_Handler

    .weak  NMI_Handler
    .thumb_set NMI_Handler, Default_Handler
    .weak  HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler
    .weak  MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler
    .weak  BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler
    .weak  UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler
    .weak  SVC_Handler
    .thumb_set SVC_Handler, Default_Handler
    .weak  DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler
    .weak  PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler
    .weak  SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler
    .weak  WWDG_IRQHandler
    .thumb_set WWDG_IRQHandler, Default_Handler
    .weak  PVD_IRQHandler
    .thumb_set PVD_IRQHandler, Default_Handler
    .weak  TAMP_STAMP_IRQHandler
    .thumb_set TAMP_STAMP_IRQHandler, Default_Handler
    .weak  RTC_WKUP_IRQHandler
    .thumb_set RTC_WKUP_IRQHandler, Default_Handler
    .weak  FLASH_IRQHandler
    .thumb_set FLASH_IRQHandler, Default_Handler
    .weak  RCC_IRQHandler
    .thumb_set RCC_IRQHandler, Default_Handler
    .weak  EXTI0_IRQHandler
    .thumb_set EXTI0_IRQHandler, Default_Handler
    .weak  EXTI1_IRQHandler
    .thumb_set EXTI1_IRQHandler, Default_Handler
    .weak  EXTI2_TSC_IRQHandler
    .thumb_set EXTI2_TSC_IRQHandler, Default_Handler
    .weak  EXTI3_IRQHandler
    .thumb_set EXTI3_IRQHandler, Default_Handler
    .weak  EXTI4_IRQHandler
    .thumb_set EXTI4_IRQHandler, Default_Handler
    .weak  DMA1_Channel1_IRQHandler
    .thumb_set DMA1_Channel1_IRQHandler, Default_Handler
    .weak  DMA1_Channel2_IRQHandler
    .thumb_set DMA1_Channel2_IRQHandler, Default_Handler
    .weak  DMA1_Channel3_IRQHandler
    .thumb_set DMA1_Channel3_IRQHandler, Default_Handler
    .weak  DMA1_Channel4_IRQHandler
    .thumb_set DMA1_Channel4_IRQHandler, Default_Handler
    .weak  DMA1_Channel5_IRQHandler
    .thumb_set DMA1_Channel5_IRQHandler, Default_Handler
    .weak  DMA1_Channel6_IRQHandler
    .thumb_set DMA1_Channel6_IRQHandler, Default_Handler
    .weak  DMA1_Channel7_IRQHandler
    .thumb_set DMA1_Channel7_IRQHandler, Default_Handler
    .weak  ADC1_2_IRQHandler
    .thumb_set ADC1_2_IRQHandler, Default_Handler
    .weak  USB_HP_CAN_TX_IRQHandler
    .thumb_set USB_HP_CAN_TX_IRQHandler, Default_Handler
    .weak  USB_LP_CAN_RX0_IRQHandler
    .thumb_set USB_LP_CAN_RX0_IRQHandler, Default_Handler
    .weak  CAN_RX1_IRQHandler
    .thumb_set CAN_RX1_IRQHandler, Default_Handler
    .weak  CAN_SCE_IRQHandler
    .thumb_set CAN_SCE_IRQHandler, Default_Handler
    .weak  EXTI9_5_IRQHandler
    .thumb_set EXTI9_5_IRQHandler, Default_Handler
    .weak  TIM1_BRK_TIM15_IRQHandler
    .thumb_set TIM1_BRK_TIM15_IRQHandler, Default_Handler
    .weak  TIM1_UP_TIM16_IRQHandler
    .thumb_set TIM1_UP_TIM16_IRQHandler, Default_Handler
    .weak  TIM1_TRG_COM_TIM17_IRQHandler
    .thumb_set TIM1_TRG_COM_TIM17_IRQHandler, Default_Handler
    .weak  TIM1_CC_IRQHandler
    .thumb_set TIM1_CC_IRQHandler, Default_Handler
    .weak  TIM2_IRQHandler
    .thumb_set TIM2_IRQHandler, Default_Handler
    .weak  TIM3_IRQHandler
    .thumb_set TIM3_IRQHandler, Default_Handler
    .weak  TIM4_IRQHandler
    .thumb_set TIM4_IRQHandler, Default_Handler
    .weak  I2C1_EV_IRQHandler
    .thumb_set I2C1_EV_IRQHandler, Default_Handler
    .weak  I2C1_ER_IRQHandler
    .thumb_set I2C1_ER_IRQHandler, Default_Handler
    .weak  I2C2_EV_IRQHandler
    .thumb_set I2C2_EV_IRQHandler, Default_Handler
    .weak  I2C2_ER_IRQHandler
    .thumb_set I2C2_ER_IRQHandler, Default_Handler
    .weak  SPI1_IRQHandler
    .thumb_set SPI1_IRQHandler, Default_Handler
    .weak  SPI2_IRQHandler
    .thumb_set SPI2_IRQHandler, Default_Handler
    .weak  USART1_IRQHandler
    .thumb_set USART1_IRQHandler, Default_Handler
    .weak  USART2_IRQHandler
    .thumb_set USART2_IRQHandler, Default_Handler
    .weak  USART3_IRQHandler
    .thumb_set USART3_IRQHandler, Default_Handler
    .weak  EXTI15_10_IRQHandler
    .thumb_set EXTI15_10_IRQHandler, Default_Handler
    .weak  RTC_Alarm_IRQHandler
    .thumb_set RTC_Alarm_IRQHandler, Default_Handler
    .weak  USBWakeUp_IRQHandler
    .thumb_set USBWakeUp_IRQHandler, Default_Handler
    .weak  TIM8_BRK_IRQHandler
    .thumb_set TIM8_BRK_IRQHandler, Default_Handler
    .weak  TIM8_UP_IRQHandler
    .thumb_set TIM8_UP_IRQHandler, Default_Handler
    .weak  TIM8_TRG_COM_IRQHandler
    .thumb_set TIM8_TRG_COM_IRQHandler, Default_Handler
    .weak  TIM8_CC_IRQHandler
    .thumb_set TIM8_CC_IRQHandler, Default_Handler
    .weak  ADC3_IRQHandler
    .thumb_set ADC3_IRQHandler, Default_Handler
    .weak  SPI3_IRQHandler
    .thumb_set SPI3_IRQHandler, Default_Handler
    .weak  UART4_IRQHandler
    .thumb_set UART4_IRQHandler, Default_Handler
    .weak  UART5_IRQHandler
    .thumb_set UART5_IRQHandler, Default_Handler
    .weak  TIM6_DAC_IRQHandler
    .thumb_set TIM6_DAC_IRQHandler, Default_Handler
    .weak  TIM7_IRQHandler
    .thumb_set TIM7_IRQHandler, Default_Handler
    .weak  DMA2_Channel1_IRQHandler
    .thumb_set DMA2_Channel1_IRQHandler, Default_Handler
    .weak  DMA2_Channel2_IRQHandler
    .thumb_set DMA2_Channel2_IRQHandler, Default_Handler
    .weak  DMA2_Channel3_IRQHandler
    .thumb_set DMA2_Channel3_IRQHandler, Default_Handler
    .weak  DMA2_Channel4_IRQHandler
    .thumb_set DMA2_Channel4_IRQHandler, Default_Handler
    .weak  DMA2_Channel5_IRQHandler
    .thumb_set DMA2_Channel5_IRQHandler, Default_Handler
    .weak  ADC4_IRQHandler
    .thumb_set ADC4_IRQHandler, Default_Handler
    .weak  COMP1_2_3_IRQHandler
    .thumb_set COMP1_2_3_IRQHandler, Default_Handler
    .weak  COMP4_5_6_IRQHandler
    .thumb_set COMP4_5_6_IRQHandler, Default_Handler
    .weak  COMP7_IRQHandler
    .thumb_set COMP7_IRQHandler, Default_Handler
    .weak  USB_HP_IRQHandler
    .thumb_set USB_HP_IRQHandler, Default_Handler
    .weak  USB_LP_IRQHandler
    .thumb_set USB_LP_IRQHandler, Default_Handler
    .weak  USBWakeUp_RMP_IRQHandler
    .thumb_set USBWakeUp_RMP_IRQHandler, Default_Handler
    .weak  FPU_IRQHandler
    .thumb_set FPU_IRQHandler, Default_Handler
