/* startup.S — Arranque del firmware de prueba del ESP32-C3 sin ROM (E3)
 *
 * gp, sp, .bss a cero, mtvec a la tabla de vectores (solo modo vectorizado,
 * base alineada a 256 [TRM-C3 §1.5]) y main(). La tabla: la entrada 0 es
 * para las excepciones (`excepcion`, débil: un bucle) y la n, para la línea
 * n del INTC (`isr_n`, débiles: `isr_nula`). Cada isr_n es una función C
 * corriente: el envoltorio guarda los registros que pisa la convención de
 * llamada y vuelve con mret. */
    .section .text.init, "ax", @progbits
    .globl _start
_start:
    .option push
    .option norelax
    la    gp, __global_pointer$
    .option pop
    la    sp, _stack_top
    la    t0, _sbss
    la    t1, _ebss
1:  bgeu  t0, t1, 2f
    sw    zero, 0(t0)
    addi  t0, t0, 4
    j     1b
2:  la    t0, vectores
    ori   t0, t0, 1
    csrw  mtvec, t0
    call  main
3:  wfi
    j     3b

    .section .vectores, "ax", @progbits
    .option push
    .option norvc
    .globl vectores
vectores:
    j     ent_excepcion
    .irp n, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    j     ent_\n
    .endr
    .option pop

    .text
/* Envoltorio: guarda ra, t0-t6, a0-a7 (los que puede pisar una función C) */
.macro ENVUELVE nombre, destino
\nombre:
    addi  sp, sp, -64
    sw    ra, 0(sp)
    sw    t0, 4(sp)
    sw    t1, 8(sp)
    sw    t2, 12(sp)
    sw    a0, 16(sp)
    sw    a1, 20(sp)
    sw    a2, 24(sp)
    sw    a3, 28(sp)
    sw    a4, 32(sp)
    sw    a5, 36(sp)
    sw    a6, 40(sp)
    sw    a7, 44(sp)
    sw    t3, 48(sp)
    sw    t4, 52(sp)
    sw    t5, 56(sp)
    sw    t6, 60(sp)
    call  \destino
    lw    ra, 0(sp)
    lw    t0, 4(sp)
    lw    t1, 8(sp)
    lw    t2, 12(sp)
    lw    a0, 16(sp)
    lw    a1, 20(sp)
    lw    a2, 24(sp)
    lw    a3, 28(sp)
    lw    a4, 32(sp)
    lw    a5, 36(sp)
    lw    a6, 40(sp)
    lw    a7, 44(sp)
    lw    t3, 48(sp)
    lw    t4, 52(sp)
    lw    t5, 56(sp)
    lw    t6, 60(sp)
    addi  sp, sp, 64
    mret
.endm
    ENVUELVE ent_excepcion, excepcion
    .irp n, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ENVUELVE ent_\n, isr_\n
    .weak isr_\n
    .set isr_\n, isr_nula
    .endr

    .weak excepcion
excepcion:
    j     excepcion
isr_nula:
    ret
