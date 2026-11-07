/* irq.S — Interrupciones del CLINT: software y temporizador, directo y
 * vectorizado, wfi y prioridades (E1)
 *
 * [Privileged 20211203, §3.1.9, §3.3.3]. No se compara con Spike: cuándo
 * llega una interrupción de temporizador depende del tiempo, y Spike no lo
 * mide igual. Se comprueba sola. */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, directo
    csrw  mtvec, t0
    li    s0, 0                 /* cuántas MSI */
    li    s1, 0                 /* cuántas MTI */
    li    s3, 0                 /* orden: causa de la primera */
    li    t0, -1
    li    t1, CLINT_MTIMECMP
    sw    t0, 0(t1)
    sw    t0, 4(t1)

    /* 1. Software, modo directo: entra en cuanto MIE = 1 */
    li    t0, 8
    csrw  mie, t0               /* MSIE */
    li    t1, CLINT_MSIP
    li    t2, 1
    sw    t2, 0(t1)
    csrsi mstatus, 8
    nop
    nop
    csrci mstatus, 8
    CHEQUEA(1, s0, 1)

    /* 2. mip.MSIP sigue al CLINT */
    li    t1, CLINT_MSIP
    li    t2, 1
    sw    t2, 0(t1)
    csrr  t3, mip
    andi  t3, t3, 8
    CHEQUEA(2, t3, 8)
    sw    zero, 0(t1)
    csrr  t3, mip
    andi  t3, t3, 8
    CHEQUEA(3, t3, 0)

    /* 3. Temporizador, modo vectorizado, despertando de wfi */
    la    t0, vtabla
    ori   t0, t0, 1
    csrw  mtvec, t0
    li    t0, 0x80
    csrw  mie, t0               /* MTIE */
    li    t1, CLINT_MTIME
    lw    t2, 0(t1)
    addi  t2, t2, 20
    li    t1, CLINT_MTIMECMP
    sw    zero, 4(t1)
    sw    t2, 0(t1)
    csrsi mstatus, 8
1:  wfi
    beqz  s1, 1b
    csrci mstatus, 8
    CHEQUEA(4, s1, 1)

    /* 4. wfi con MIE = 0: despierta, pero no entra */
    li    t1, CLINT_MTIME
    lw    t2, 0(t1)
    addi  t2, t2, 20
    li    t1, CLINT_MTIMECMP
    sw    t2, 0(t1)
    sw    zero, 4(t1)           /* el manejador de antes lo dejó en ~0 */
    wfi
    csrr  t3, mip
    andi  t3, t3, 0x80
    CHEQUEA(5, t3, 0x80)
    CHEQUEA(6, s1, 1)
    li    t0, -1
    li    t1, CLINT_MTIMECMP
    sw    t0, 0(t1)

    /* 5. Las dos a la vez: entra antes MSI (3) que MTI (7) */
    li    s0, 0
    li    s1, 0
    li    s3, 0
    li    t0, 0x88
    csrw  mie, t0
    li    t1, CLINT_MTIMECMP
    sw    zero, 0(t1)           /* MTIP ya */
    li    t1, CLINT_MSIP
    li    t2, 1
    sw    t2, 0(t1)
    csrsi mstatus, 8
    nop
    nop
    nop
    csrci mstatus, 8
    CHEQUEA(7, s3, 3)
    CHEQUEA(8, s0, 1)
    CHEQUEA(9, s1, 1)
    PASA

    .align 2
directo:
    csrr  t5, mcause
    li    t6, 0x80000003
    bne   t5, t6, malo
    addi  s0, s0, 1
    li    t5, CLINT_MSIP
    sw    zero, 0(t5)
    mret
malo:
    li    a0, 99
    j     termina

    .option push
    .option norvc
    .align 8
vtabla:
    j     malo                  /* 0: excepciones */
    j     malo
    j     malo
    j     v_msi                 /* 3: MSI */
    j     malo
    j     malo
    j     malo
    j     v_mti                 /* 7: MTI */
    .fill 24, 4, 0x00000013
    .option pop
v_msi:
    bnez  s3, 1f
    li    s3, 3
1:  addi  s0, s0, 1
    li    t5, CLINT_MSIP
    sw    zero, 0(t5)
    mret
v_mti:
    bnez  s3, 1f
    li    s3, 7
1:  addi  s1, s1, 1
    li    t5, -1
    li    t6, CLINT_MTIMECMP
    sw    t5, 0(t6)
    sw    t5, 4(t6)
    mret
