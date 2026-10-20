#!/usr/bin/env python3
# gen_startup.py — escribe startup_f3.s, el arranque de los firmwares PROPIOS
# del F303 (los de ST llevan el suyo), a partir de IRQn_Type de stm32f303xc.h.
#   python3 gen_startup.py sdk/cmsis_device_f3/Include/stm32f303xc.h > startup_f3.s
import re
import sys

s = open(sys.argv[1]).read()
irq = {}
for m in re.finditer(r'^\s*(\w+)_IRQn\s*=\s*(\d+),', s, re.M):
    irq[int(m.group(2))] = m.group(1)
n = max(irq) + 1
o = ["""/* ===========================================================================
 * startup_f3.s — Arranque de los firmwares de verificacion del STM32F303VC
 *
 * GENERADO por gen_startup.py a partir de IRQn_Type de stm32f303xc.h: 16
 * excepciones de sistema + %d IRQ. Cada IRQ tiene un manejador debil con el
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

    .section .isr_vector, "a", %%progbits
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
    .word  SysTick_Handler""" % n]
nombres = []
for k in range(n):
    if k in irq:
        nm = irq[k] + "_IRQHandler"
        nombres.append(nm)
        o.append("    .word  %-28s /* %2d */" % (nm, k))
    else:
        o.append("    .word  Default_Handler              /* %2d */" % k)
o.append("""
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
""")
for nm in ["NMI_Handler", "HardFault_Handler", "MemManage_Handler", "BusFault_Handler",
           "UsageFault_Handler", "SVC_Handler", "DebugMon_Handler", "PendSV_Handler",
           "SysTick_Handler"] + nombres:
    o.append("    .weak  %s\n    .thumb_set %s, Default_Handler" % (nm, nm))
print("\n".join(o))
