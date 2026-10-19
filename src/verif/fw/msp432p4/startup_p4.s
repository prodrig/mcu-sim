/* ===========================================================================
 * startup_p4.s — Arranque minimo de los firmwares de verificacion del MSP432P4
 *
 * Tabla de vectores del MSP432P401R: 16 excepciones de sistema + 41 IRQ, de
 * PSS_IRQn (0) a PORT6_IRQn (40) [msp432p401r.h, IRQn_Type]. Los manejadores
 * que el C no define caen en un bucle.
 *
 * DOS COSAS QUE LO DISTINGUEN DEL DE LOS STM32 (verif/fw/startup.s):
 *
 *   * la tabla esta en 0x0, porque ahi esta la Flash: no hay alias de arranque;
 *   * el perro guardian ARRANCA ENCENDIDO (10,9 ms a 3 MHz). Con -DPARA_WDT
 *     el arranque lo para antes de nada, que es lo que hace el system de TI
 *     en sus ejemplos; sin el, lo para el main -o no, que es la prueba del
 *     perro-.
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
    /* --- IRQ 0..40 ------------------------------------------------------- */
    .word  Default_IRQ_Handler     /*  0 PSS     */
    .word  Default_IRQ_Handler     /*  1 CS      */
    .word  Default_IRQ_Handler     /*  2 PCM     */
    .word  WDT_A_IRQHandler        /*  3 WDT_A   */
    .rept  31
    .word  Default_IRQ_Handler     /*  4..34     */
    .endr
    .word  PORT1_IRQHandler        /* 35 PORT1   */
    .word  Default_IRQ_Handler     /* 36 PORT2   */
    .word  Default_IRQ_Handler     /* 37 PORT3   */
    .word  Default_IRQ_Handler     /* 38 PORT4   */
    .word  Default_IRQ_Handler     /* 39 PORT5   */
    .word  Default_IRQ_Handler     /* 40 PORT6   */

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
    .weak  HardFault_Handler
    .weak  MemManage_Handler
    .weak  BusFault_Handler
    .weak  UsageFault_Handler
    .weak  SVC_Handler
    .weak  DebugMon_Handler
    .weak  PendSV_Handler
    .weak  SysTick_Handler
    .weak  Default_IRQ_Handler
    .weak  WDT_A_IRQHandler
    .weak  PORT1_IRQHandler
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
WDT_A_IRQHandler:
    .thumb_func
PORT1_IRQHandler:
    b   .
