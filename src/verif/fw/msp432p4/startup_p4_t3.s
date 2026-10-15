/* ===========================================================================
 * startup_p4_t3.s — Arranque de los firmwares de la fase T3 del MSP432P401R
 *
 * El de startup_p4.s con la tabla de vectores ENTERA y con los nombres de TI:
 * los 41 manejadores de startup_msp432p401r_gcc.c (PSS_IRQHandler ...
 * PORT6_IRQHandler), cada uno debil y apuntando a un bucle. Un firmware
 * define el suyo con el mismo nombre que en un proyecto de CCS, y ya esta.
 *
 * Es un fichero aparte para no cambiar ni un byte de los firmwares de T1 y
 * T2, cuya huella esta versionada.
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
    .word  PSS_IRQHandler          /*  0 */
    .word  CS_IRQHandler           /*  1 */
    .word  PCM_IRQHandler          /*  2 */
    .word  WDT_A_IRQHandler        /*  3 */
    .word  FPU_IRQHandler          /*  4 */
    .word  FLCTL_IRQHandler        /*  5 */
    .word  COMP_E0_IRQHandler      /*  6 */
    .word  COMP_E1_IRQHandler      /*  7 */
    .word  TA0_0_IRQHandler        /*  8 */
    .word  TA0_N_IRQHandler        /*  9 */
    .word  TA1_0_IRQHandler        /* 10 */
    .word  TA1_N_IRQHandler        /* 11 */
    .word  TA2_0_IRQHandler        /* 12 */
    .word  TA2_N_IRQHandler        /* 13 */
    .word  TA3_0_IRQHandler        /* 14 */
    .word  TA3_N_IRQHandler        /* 15 */
    .word  EUSCIA0_IRQHandler      /* 16 */
    .word  EUSCIA1_IRQHandler      /* 17 */
    .word  EUSCIA2_IRQHandler      /* 18 */
    .word  EUSCIA3_IRQHandler      /* 19 */
    .word  EUSCIB0_IRQHandler      /* 20 */
    .word  EUSCIB1_IRQHandler      /* 21 */
    .word  EUSCIB2_IRQHandler      /* 22 */
    .word  EUSCIB3_IRQHandler      /* 23 */
    .word  ADC14_IRQHandler        /* 24 */
    .word  T32_INT1_IRQHandler     /* 25 */
    .word  T32_INT2_IRQHandler     /* 26 */
    .word  T32_INTC_IRQHandler     /* 27 */
    .word  AES256_IRQHandler       /* 28 */
    .word  RTC_C_IRQHandler        /* 29 */
    .word  DMA_ERR_IRQHandler      /* 30 */
    .word  DMA_INT3_IRQHandler     /* 31 */
    .word  DMA_INT2_IRQHandler     /* 32 */
    .word  DMA_INT1_IRQHandler     /* 33 */
    .word  DMA_INT0_IRQHandler     /* 34 */
    .word  PORT1_IRQHandler        /* 35 */
    .word  PORT2_IRQHandler        /* 36 */
    .word  PORT3_IRQHandler        /* 37 */
    .word  PORT4_IRQHandler        /* 38 */
    .word  PORT5_IRQHandler        /* 39 */
    .word  PORT6_IRQHandler        /* 40 */

    .section .text.Reset_Handler, "ax", %progbits
    .global  Reset_Handler
    .type    Reset_Handler, %function
Reset_Handler:
#ifdef PARA_WDT
    /* WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD (0x5A80), en 0x4000_480C */
    ldr   r0, =0x4000480C
    movw  r1, #0x5A80
    strh  r1, [r0]
#endif
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
    .weak  PSS_IRQHandler
    .thumb_set PSS_IRQHandler, Default_Handler
    .weak  CS_IRQHandler
    .thumb_set CS_IRQHandler, Default_Handler
    .weak  PCM_IRQHandler
    .thumb_set PCM_IRQHandler, Default_Handler
    .weak  WDT_A_IRQHandler
    .thumb_set WDT_A_IRQHandler, Default_Handler
    .weak  FPU_IRQHandler
    .thumb_set FPU_IRQHandler, Default_Handler
    .weak  FLCTL_IRQHandler
    .thumb_set FLCTL_IRQHandler, Default_Handler
    .weak  COMP_E0_IRQHandler
    .thumb_set COMP_E0_IRQHandler, Default_Handler
    .weak  COMP_E1_IRQHandler
    .thumb_set COMP_E1_IRQHandler, Default_Handler
    .weak  TA0_0_IRQHandler
    .thumb_set TA0_0_IRQHandler, Default_Handler
    .weak  TA0_N_IRQHandler
    .thumb_set TA0_N_IRQHandler, Default_Handler
    .weak  TA1_0_IRQHandler
    .thumb_set TA1_0_IRQHandler, Default_Handler
    .weak  TA1_N_IRQHandler
    .thumb_set TA1_N_IRQHandler, Default_Handler
    .weak  TA2_0_IRQHandler
    .thumb_set TA2_0_IRQHandler, Default_Handler
    .weak  TA2_N_IRQHandler
    .thumb_set TA2_N_IRQHandler, Default_Handler
    .weak  TA3_0_IRQHandler
    .thumb_set TA3_0_IRQHandler, Default_Handler
    .weak  TA3_N_IRQHandler
    .thumb_set TA3_N_IRQHandler, Default_Handler
    .weak  EUSCIA0_IRQHandler
    .thumb_set EUSCIA0_IRQHandler, Default_Handler
    .weak  EUSCIA1_IRQHandler
    .thumb_set EUSCIA1_IRQHandler, Default_Handler
    .weak  EUSCIA2_IRQHandler
    .thumb_set EUSCIA2_IRQHandler, Default_Handler
    .weak  EUSCIA3_IRQHandler
    .thumb_set EUSCIA3_IRQHandler, Default_Handler
    .weak  EUSCIB0_IRQHandler
    .thumb_set EUSCIB0_IRQHandler, Default_Handler
    .weak  EUSCIB1_IRQHandler
    .thumb_set EUSCIB1_IRQHandler, Default_Handler
    .weak  EUSCIB2_IRQHandler
    .thumb_set EUSCIB2_IRQHandler, Default_Handler
    .weak  EUSCIB3_IRQHandler
    .thumb_set EUSCIB3_IRQHandler, Default_Handler
    .weak  ADC14_IRQHandler
    .thumb_set ADC14_IRQHandler, Default_Handler
    .weak  T32_INT1_IRQHandler
    .thumb_set T32_INT1_IRQHandler, Default_Handler
    .weak  T32_INT2_IRQHandler
    .thumb_set T32_INT2_IRQHandler, Default_Handler
    .weak  T32_INTC_IRQHandler
    .thumb_set T32_INTC_IRQHandler, Default_Handler
    .weak  AES256_IRQHandler
    .thumb_set AES256_IRQHandler, Default_Handler
    .weak  RTC_C_IRQHandler
    .thumb_set RTC_C_IRQHandler, Default_Handler
    .weak  DMA_ERR_IRQHandler
    .thumb_set DMA_ERR_IRQHandler, Default_Handler
    .weak  DMA_INT3_IRQHandler
    .thumb_set DMA_INT3_IRQHandler, Default_Handler
    .weak  DMA_INT2_IRQHandler
    .thumb_set DMA_INT2_IRQHandler, Default_Handler
    .weak  DMA_INT1_IRQHandler
    .thumb_set DMA_INT1_IRQHandler, Default_Handler
    .weak  DMA_INT0_IRQHandler
    .thumb_set DMA_INT0_IRQHandler, Default_Handler
    .weak  PORT1_IRQHandler
    .thumb_set PORT1_IRQHandler, Default_Handler
    .weak  PORT2_IRQHandler
    .thumb_set PORT2_IRQHandler, Default_Handler
    .weak  PORT3_IRQHandler
    .thumb_set PORT3_IRQHandler, Default_Handler
    .weak  PORT4_IRQHandler
    .thumb_set PORT4_IRQHandler, Default_Handler
    .weak  PORT5_IRQHandler
    .thumb_set PORT5_IRQHandler, Default_Handler
    .weak  PORT6_IRQHandler
    .thumb_set PORT6_IRQHandler, Default_Handler
    .global Default_Handler
    .thumb_func
Default_Handler:
    b   .
