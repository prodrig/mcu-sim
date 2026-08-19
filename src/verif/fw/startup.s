/* ===========================================================================
 * startup.s — Arranque mínimo del firmware de verificación (fase F2)
 *
 * Tabla de vectores completa del STM32F407VG: 16 excepciones de sistema +
 * 82 IRQ [IR, §9.1.2]. Todos los manejadores no usados apuntan a un bucle de
 * error que deja constancia en el buzón de resultados.
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
    .word  NMI_Handler             /* 0x08: NMI                              */
    .word  HardFault_Handler       /* 0x0C: HardFault                        */
    .word  MemManage_Handler       /* 0x10: MemManage                        */
    .word  BusFault_Handler        /* 0x14: BusFault                         */
    .word  UsageFault_Handler      /* 0x18: UsageFault                       */
    .word  0
    .word  0
    .word  0
    .word  0
    .word  SVC_Handler             /* 0x2C: SVCall                           */
    .word  DebugMon_Handler        /* 0x30: DebugMonitor                     */
    .word  0
    .word  PendSV_Handler          /* 0x38: PendSV                           */
    .word  SysTick_Handler         /* 0x3C: SysTick                          */
    /* --- IRQ 0..81 [IR, §9.1.2] -------------------------------------------*/
    .word  WWDG_IRQHandler          /* IRQ 0: usada por las pruebas           */
    .rept  81
    .word  Default_IRQ_Handler
    .endr

    .section .text.Reset_Handler, "ax", %progbits
    .global  Reset_Handler
    .type    Reset_Handler, %function
Reset_Handler:
    /* Habilitación del coprocesador de coma flotante (CPACR.CP10/CP11), lo
       mismo que hace SystemInit() en el arranque estándar de ST. Sin esto la
       primera instrucción VFP —incluida la VMOV Dm,Rt,Rt2 que el ABI hard-float
       usa para devolver un double— genera UsageFault NOCP [IR, §7.4; II, §5.1]. */
    ldr   r0, =0xE000ED88
    ldr   r1, [r0]
    orr   r1, r1, #(0xF << 20)
    str   r1, [r0]
    dsb
    isb

    /* Copia de .data desde Flash (LMA) a SRAM (VMA) */
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
    /* Borrado de .bss */
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

/* --- Manejadores por defecto (los fuertes los define el C) -----------------*/
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
    .weak  WWDG_IRQHandler
    /* .thumb_func afecta solo al símbolo que le sigue: hace falta uno por
       etiqueta para que el bit 0 de cada vector quede a 1 (estado Thumb).
       Sin él, el núcleo entra en el manejador con EPSR.T = 0 y genera
       UsageFault INVSTATE [IR, §7.1.3]. */
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
WWDG_IRQHandler:
    b   .
