/* ===========================================================================
 * startup_e4.s — Arranque minimo de los firmwares de verificacion del MSP432E4
 *
 * Tabla de vectores del MSP432E401Y: 16 excepciones de sistema + 111 IRQ, de
 * GPIOA (0) a I2C9 (110) [msp432e401y.h, IRQn_Type]; los huecos de la
 * numeracion (68, 69, 74, 75, 92-97, 104-108) van a Default_IRQ_Handler.
 * Cada IRQ tiene un manejador debil con el nombre de la cabecera (como el
 * startup de TI): el C define los que usa y los demas caen en un bucle.
 *
 * Lo distinto del P4: los perros del E4 arrancan APAGADOS, asi que no hay que
 * pararlos. GENERADO por un guion a partir de la cabecera; se puede editar.
 * ===========================================================================*/
    .syntax unified
    .cpu cortex-m4
    .fpu fpv4-sp-d16
    .thumb

    .section .isr_vector, "a", %progbits
    .global  g_vectors
g_vectors:
    .word  _estack                 /* 0x00: MSP inicial                      */
    .word  Reset_Handler           /* 0x04: Reset                            */
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
    .word  GPIOA_IRQHandler         /*   0 */
    .word  GPIOB_IRQHandler         /*   1 */
    .word  GPIOC_IRQHandler         /*   2 */
    .word  GPIOD_IRQHandler         /*   3 */
    .word  GPIOE_IRQHandler         /*   4 */
    .word  UART0_IRQHandler         /*   5 */
    .word  UART1_IRQHandler         /*   6 */
    .word  SSI0_IRQHandler          /*   7 */
    .word  I2C0_IRQHandler          /*   8 */
    .word  PWM0_FAULT_IRQHandler    /*   9 */
    .word  PWM0_0_IRQHandler        /*  10 */
    .word  PWM0_1_IRQHandler        /*  11 */
    .word  PWM0_2_IRQHandler        /*  12 */
    .word  QEI0_IRQHandler          /*  13 */
    .word  ADC0SS0_IRQHandler       /*  14 */
    .word  ADC0SS1_IRQHandler       /*  15 */
    .word  ADC0SS2_IRQHandler       /*  16 */
    .word  ADC0SS3_IRQHandler       /*  17 */
    .word  WATCHDOG0_IRQHandler     /*  18 */
    .word  TIMER0A_IRQHandler       /*  19 */
    .word  TIMER0B_IRQHandler       /*  20 */
    .word  TIMER1A_IRQHandler       /*  21 */
    .word  TIMER1B_IRQHandler       /*  22 */
    .word  TIMER2A_IRQHandler       /*  23 */
    .word  TIMER2B_IRQHandler       /*  24 */
    .word  COMP0_IRQHandler         /*  25 */
    .word  COMP1_IRQHandler         /*  26 */
    .word  COMP2_IRQHandler         /*  27 */
    .word  SYSCTL_IRQHandler        /*  28 */
    .word  FLASH_CTRL_IRQHandler    /*  29 */
    .word  GPIOF_IRQHandler         /*  30 */
    .word  GPIOG_IRQHandler         /*  31 */
    .word  GPIOH_IRQHandler         /*  32 */
    .word  UART2_IRQHandler         /*  33 */
    .word  SSI1_IRQHandler          /*  34 */
    .word  TIMER3A_IRQHandler       /*  35 */
    .word  TIMER3B_IRQHandler       /*  36 */
    .word  I2C1_IRQHandler          /*  37 */
    .word  CAN0_IRQHandler          /*  38 */
    .word  CAN1_IRQHandler          /*  39 */
    .word  EMAC0_IRQHandler         /*  40 */
    .word  HIB_IRQHandler           /*  41 */
    .word  USB0_IRQHandler          /*  42 */
    .word  PWM0_3_IRQHandler        /*  43 */
    .word  UDMA_IRQHandler          /*  44 */
    .word  UDMAERR_IRQHandler       /*  45 */
    .word  ADC1SS0_IRQHandler       /*  46 */
    .word  ADC1SS1_IRQHandler       /*  47 */
    .word  ADC1SS2_IRQHandler       /*  48 */
    .word  ADC1SS3_IRQHandler       /*  49 */
    .word  EPI0_IRQHandler          /*  50 */
    .word  GPIOJ_IRQHandler         /*  51 */
    .word  GPIOK_IRQHandler         /*  52 */
    .word  GPIOL_IRQHandler         /*  53 */
    .word  SSI2_IRQHandler          /*  54 */
    .word  SSI3_IRQHandler          /*  55 */
    .word  UART3_IRQHandler         /*  56 */
    .word  UART4_IRQHandler         /*  57 */
    .word  UART5_IRQHandler         /*  58 */
    .word  UART6_IRQHandler         /*  59 */
    .word  UART7_IRQHandler         /*  60 */
    .word  I2C2_IRQHandler          /*  61 */
    .word  I2C3_IRQHandler          /*  62 */
    .word  TIMER4A_IRQHandler       /*  63 */
    .word  TIMER4B_IRQHandler       /*  64 */
    .word  TIMER5A_IRQHandler       /*  65 */
    .word  TIMER5B_IRQHandler       /*  66 */
    .word  SYSEXC_IRQHandler        /*  67 */
    .word  Default_IRQ_Handler      /*  68 */
    .word  Default_IRQ_Handler      /*  69 */
    .word  I2C4_IRQHandler          /*  70 */
    .word  I2C5_IRQHandler          /*  71 */
    .word  GPIOM_IRQHandler         /*  72 */
    .word  GPION_IRQHandler         /*  73 */
    .word  Default_IRQ_Handler      /*  74 */
    .word  Default_IRQ_Handler      /*  75 */
    .word  GPIOP0_IRQHandler        /*  76 */
    .word  GPIOP1_IRQHandler        /*  77 */
    .word  GPIOP2_IRQHandler        /*  78 */
    .word  GPIOP3_IRQHandler        /*  79 */
    .word  GPIOP4_IRQHandler        /*  80 */
    .word  GPIOP5_IRQHandler        /*  81 */
    .word  GPIOP6_IRQHandler        /*  82 */
    .word  GPIOP7_IRQHandler        /*  83 */
    .word  GPIOQ0_IRQHandler        /*  84 */
    .word  GPIOQ1_IRQHandler        /*  85 */
    .word  GPIOQ2_IRQHandler        /*  86 */
    .word  GPIOQ3_IRQHandler        /*  87 */
    .word  GPIOQ4_IRQHandler        /*  88 */
    .word  GPIOQ5_IRQHandler        /*  89 */
    .word  GPIOQ6_IRQHandler        /*  90 */
    .word  GPIOQ7_IRQHandler        /*  91 */
    .word  Default_IRQ_Handler      /*  92 */
    .word  Default_IRQ_Handler      /*  93 */
    .word  Default_IRQ_Handler      /*  94 */
    .word  Default_IRQ_Handler      /*  95 */
    .word  Default_IRQ_Handler      /*  96 */
    .word  Default_IRQ_Handler      /*  97 */
    .word  TIMER6A_IRQHandler       /*  98 */
    .word  TIMER6B_IRQHandler       /*  99 */
    .word  TIMER7A_IRQHandler       /* 100 */
    .word  TIMER7B_IRQHandler       /* 101 */
    .word  I2C6_IRQHandler          /* 102 */
    .word  I2C7_IRQHandler          /* 103 */
    .word  Default_IRQ_Handler      /* 104 */
    .word  Default_IRQ_Handler      /* 105 */
    .word  Default_IRQ_Handler      /* 106 */
    .word  Default_IRQ_Handler      /* 107 */
    .word  Default_IRQ_Handler      /* 108 */
    .word  I2C8_IRQHandler          /* 109 */
    .word  I2C9_IRQHandler          /* 110 */

    .section .text.Reset_Handler, "ax", %progbits
    .global  Reset_Handler
    .type    Reset_Handler, %function
Reset_Handler:
    /* FPU: CPACR.CP10/CP11 */
    ldr   r0, =0xE000ED88
    ldr   r1, [r0]
    orr   r1, r1, #(0xF << 20)
    str   r1, [r0]
    dsb
    isb
    /* .data de la Flash a la SRAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
1:  cmp   r0, r1
    bcs   2f
    ldr   r4, [r2]
    str   r4, [r0]
    adds  r0, r0, #4
    adds  r2, r2, #4
    b     1b
2:
    /* .bss a cero */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
    movs  r2, #0
3:  cmp   r0, r1
    bcs   4f
    str   r2, [r0]
    adds  r0, r0, #4
    b     3b
4:
#ifndef ENTRY_SYMBOL
#define ENTRY_SYMBOL main
#endif
    bl    ENTRY_SYMBOL
5:  b     5b
    .size Reset_Handler, .-Reset_Handler

    .section .text.handlers, "ax", %progbits
    .weak  NMI_Handler
    .weak  HardFault_Handler
    .weak  MemManage_Handler
    .weak  BusFault_Handler
    .weak  UsageFault_Handler
    .weak  SVC_Handler
    .weak  DebugMon_Handler
    .weak  PendSV_Handler
    .weak  SysTick_Handler
    .weak  Default_IRQ_Handler
    .weak  GPIOA_IRQHandler
    .weak  GPIOB_IRQHandler
    .weak  GPIOC_IRQHandler
    .weak  GPIOD_IRQHandler
    .weak  GPIOE_IRQHandler
    .weak  UART0_IRQHandler
    .weak  UART1_IRQHandler
    .weak  SSI0_IRQHandler
    .weak  I2C0_IRQHandler
    .weak  PWM0_FAULT_IRQHandler
    .weak  PWM0_0_IRQHandler
    .weak  PWM0_1_IRQHandler
    .weak  PWM0_2_IRQHandler
    .weak  QEI0_IRQHandler
    .weak  ADC0SS0_IRQHandler
    .weak  ADC0SS1_IRQHandler
    .weak  ADC0SS2_IRQHandler
    .weak  ADC0SS3_IRQHandler
    .weak  WATCHDOG0_IRQHandler
    .weak  TIMER0A_IRQHandler
    .weak  TIMER0B_IRQHandler
    .weak  TIMER1A_IRQHandler
    .weak  TIMER1B_IRQHandler
    .weak  TIMER2A_IRQHandler
    .weak  TIMER2B_IRQHandler
    .weak  COMP0_IRQHandler
    .weak  COMP1_IRQHandler
    .weak  COMP2_IRQHandler
    .weak  SYSCTL_IRQHandler
    .weak  FLASH_CTRL_IRQHandler
    .weak  GPIOF_IRQHandler
    .weak  GPIOG_IRQHandler
    .weak  GPIOH_IRQHandler
    .weak  UART2_IRQHandler
    .weak  SSI1_IRQHandler
    .weak  TIMER3A_IRQHandler
    .weak  TIMER3B_IRQHandler
    .weak  I2C1_IRQHandler
    .weak  CAN0_IRQHandler
    .weak  CAN1_IRQHandler
    .weak  EMAC0_IRQHandler
    .weak  HIB_IRQHandler
    .weak  USB0_IRQHandler
    .weak  PWM0_3_IRQHandler
    .weak  UDMA_IRQHandler
    .weak  UDMAERR_IRQHandler
    .weak  ADC1SS0_IRQHandler
    .weak  ADC1SS1_IRQHandler
    .weak  ADC1SS2_IRQHandler
    .weak  ADC1SS3_IRQHandler
    .weak  EPI0_IRQHandler
    .weak  GPIOJ_IRQHandler
    .weak  GPIOK_IRQHandler
    .weak  GPIOL_IRQHandler
    .weak  SSI2_IRQHandler
    .weak  SSI3_IRQHandler
    .weak  UART3_IRQHandler
    .weak  UART4_IRQHandler
    .weak  UART5_IRQHandler
    .weak  UART6_IRQHandler
    .weak  UART7_IRQHandler
    .weak  I2C2_IRQHandler
    .weak  I2C3_IRQHandler
    .weak  TIMER4A_IRQHandler
    .weak  TIMER4B_IRQHandler
    .weak  TIMER5A_IRQHandler
    .weak  TIMER5B_IRQHandler
    .weak  SYSEXC_IRQHandler
    .weak  I2C4_IRQHandler
    .weak  I2C5_IRQHandler
    .weak  GPIOM_IRQHandler
    .weak  GPION_IRQHandler
    .weak  GPIOP0_IRQHandler
    .weak  GPIOP1_IRQHandler
    .weak  GPIOP2_IRQHandler
    .weak  GPIOP3_IRQHandler
    .weak  GPIOP4_IRQHandler
    .weak  GPIOP5_IRQHandler
    .weak  GPIOP6_IRQHandler
    .weak  GPIOP7_IRQHandler
    .weak  GPIOQ0_IRQHandler
    .weak  GPIOQ1_IRQHandler
    .weak  GPIOQ2_IRQHandler
    .weak  GPIOQ3_IRQHandler
    .weak  GPIOQ4_IRQHandler
    .weak  GPIOQ5_IRQHandler
    .weak  GPIOQ6_IRQHandler
    .weak  GPIOQ7_IRQHandler
    .weak  TIMER6A_IRQHandler
    .weak  TIMER6B_IRQHandler
    .weak  TIMER7A_IRQHandler
    .weak  TIMER7B_IRQHandler
    .weak  I2C6_IRQHandler
    .weak  I2C7_IRQHandler
    .weak  I2C8_IRQHandler
    .weak  I2C9_IRQHandler
    .thumb_func
NMI_Handler:
    .thumb_func
HardFault_Handler:
    .thumb_func
MemManage_Handler:
    .thumb_func
BusFault_Handler:
    .thumb_func
UsageFault_Handler:
    .thumb_func
SVC_Handler:
    .thumb_func
DebugMon_Handler:
    .thumb_func
PendSV_Handler:
    .thumb_func
SysTick_Handler:
    .thumb_func
Default_IRQ_Handler:
    .thumb_func
GPIOA_IRQHandler:
    .thumb_func
GPIOB_IRQHandler:
    .thumb_func
GPIOC_IRQHandler:
    .thumb_func
GPIOD_IRQHandler:
    .thumb_func
GPIOE_IRQHandler:
    .thumb_func
UART0_IRQHandler:
    .thumb_func
UART1_IRQHandler:
    .thumb_func
SSI0_IRQHandler:
    .thumb_func
I2C0_IRQHandler:
    .thumb_func
PWM0_FAULT_IRQHandler:
    .thumb_func
PWM0_0_IRQHandler:
    .thumb_func
PWM0_1_IRQHandler:
    .thumb_func
PWM0_2_IRQHandler:
    .thumb_func
QEI0_IRQHandler:
    .thumb_func
ADC0SS0_IRQHandler:
    .thumb_func
ADC0SS1_IRQHandler:
    .thumb_func
ADC0SS2_IRQHandler:
    .thumb_func
ADC0SS3_IRQHandler:
    .thumb_func
WATCHDOG0_IRQHandler:
    .thumb_func
TIMER0A_IRQHandler:
    .thumb_func
TIMER0B_IRQHandler:
    .thumb_func
TIMER1A_IRQHandler:
    .thumb_func
TIMER1B_IRQHandler:
    .thumb_func
TIMER2A_IRQHandler:
    .thumb_func
TIMER2B_IRQHandler:
    .thumb_func
COMP0_IRQHandler:
    .thumb_func
COMP1_IRQHandler:
    .thumb_func
COMP2_IRQHandler:
    .thumb_func
SYSCTL_IRQHandler:
    .thumb_func
FLASH_CTRL_IRQHandler:
    .thumb_func
GPIOF_IRQHandler:
    .thumb_func
GPIOG_IRQHandler:
    .thumb_func
GPIOH_IRQHandler:
    .thumb_func
UART2_IRQHandler:
    .thumb_func
SSI1_IRQHandler:
    .thumb_func
TIMER3A_IRQHandler:
    .thumb_func
TIMER3B_IRQHandler:
    .thumb_func
I2C1_IRQHandler:
    .thumb_func
CAN0_IRQHandler:
    .thumb_func
CAN1_IRQHandler:
    .thumb_func
EMAC0_IRQHandler:
    .thumb_func
HIB_IRQHandler:
    .thumb_func
USB0_IRQHandler:
    .thumb_func
PWM0_3_IRQHandler:
    .thumb_func
UDMA_IRQHandler:
    .thumb_func
UDMAERR_IRQHandler:
    .thumb_func
ADC1SS0_IRQHandler:
    .thumb_func
ADC1SS1_IRQHandler:
    .thumb_func
ADC1SS2_IRQHandler:
    .thumb_func
ADC1SS3_IRQHandler:
    .thumb_func
EPI0_IRQHandler:
    .thumb_func
GPIOJ_IRQHandler:
    .thumb_func
GPIOK_IRQHandler:
    .thumb_func
GPIOL_IRQHandler:
    .thumb_func
SSI2_IRQHandler:
    .thumb_func
SSI3_IRQHandler:
    .thumb_func
UART3_IRQHandler:
    .thumb_func
UART4_IRQHandler:
    .thumb_func
UART5_IRQHandler:
    .thumb_func
UART6_IRQHandler:
    .thumb_func
UART7_IRQHandler:
    .thumb_func
I2C2_IRQHandler:
    .thumb_func
I2C3_IRQHandler:
    .thumb_func
TIMER4A_IRQHandler:
    .thumb_func
TIMER4B_IRQHandler:
    .thumb_func
TIMER5A_IRQHandler:
    .thumb_func
TIMER5B_IRQHandler:
    .thumb_func
SYSEXC_IRQHandler:
    .thumb_func
I2C4_IRQHandler:
    .thumb_func
I2C5_IRQHandler:
    .thumb_func
GPIOM_IRQHandler:
    .thumb_func
GPION_IRQHandler:
    .thumb_func
GPIOP0_IRQHandler:
    .thumb_func
GPIOP1_IRQHandler:
    .thumb_func
GPIOP2_IRQHandler:
    .thumb_func
GPIOP3_IRQHandler:
    .thumb_func
GPIOP4_IRQHandler:
    .thumb_func
GPIOP5_IRQHandler:
    .thumb_func
GPIOP6_IRQHandler:
    .thumb_func
GPIOP7_IRQHandler:
    .thumb_func
GPIOQ0_IRQHandler:
    .thumb_func
GPIOQ1_IRQHandler:
    .thumb_func
GPIOQ2_IRQHandler:
    .thumb_func
GPIOQ3_IRQHandler:
    .thumb_func
GPIOQ4_IRQHandler:
    .thumb_func
GPIOQ5_IRQHandler:
    .thumb_func
GPIOQ6_IRQHandler:
    .thumb_func
GPIOQ7_IRQHandler:
    .thumb_func
TIMER6A_IRQHandler:
    .thumb_func
TIMER6B_IRQHandler:
    .thumb_func
TIMER7A_IRQHandler:
    .thumb_func
TIMER7B_IRQHandler:
    .thumb_func
I2C6_IRQHandler:
    .thumb_func
I2C7_IRQHandler:
    .thumb_func
I2C8_IRQHandler:
    .thumb_func
I2C9_IRQHandler:
    b   .
