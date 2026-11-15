/* irq_spike.S — Interrupciones del CLINT con la traza IDÉNTICA a la de Spike
 * (E1, lo que faltaba)
 *
 * Spike avanza en tandas de 5000 instrucciones, sube mtime 50 al acabar cada
 * una (y cada trap o wfi la acaba), y solo mira las interrupciones al empezar
 * una tanda o tras una instrucción que serializa (escribir un CSR). Nuestro
 * hart las mira en cada instrucción. Para que las dos trazas coincidan sin
 * imitar eso, el programa sigue tres reglas:
 *   1. toda interrupción se deja PENDIENTE CON ELLA ENMASCARADA y luego se
 *      desenmascara escribiendo un CSR (csrs mie, csrsi mstatus): los dos la
 *      toman justo detrás de esa escritura;
 *   2. el temporizador solo con mtimecmp en el PASADO (0: MTIP sube al
 *      escribirlo, en Spike y aquí) o en el futuro lejano (~0);
 *   3. no se lee mtime, y wfi solo con la interrupción ya pendiente.
 * Prueba MSI y MTI en modo directo y vectorizado, su prioridad (MSI antes que
 * MTI), MPIE en mret, una interrupción dentro de otra, wfi con una pendiente
 * y MIE = 0, y mcause/mepc. */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, directo
    csrw  mtvec, t0
    li    s0, 0                 /* cuántas MSI */
    li    s1, 0                 /* cuántas MTI */
    li    s3, 0                 /* el orden: causa de la primera, de la segunda */
    li    s4, 0
    call  mtimecmp_lejos

    /* 1. MSI, modo directo: pendiente con MSIE = 0; csrs mie la deja entrar */
    csrsi mstatus, 8
    li    t1, CLINT_MSIP
    li    t2, 1
    sw    t2, 0(t1)
    li    t0, 8
    csrs  mie, t0
    nop
    CHEQUEA(1, s0, 1)
    csrw  mie, zero
    csrci mstatus, 8

    /* 2. MTI: mtimecmp = 0 (en el pasado) con MIE = 0; csrsi la deja entrar */
    li    t0, 0x80
    csrw  mie, t0
    li    t1, CLINT_MTIMECMP
    sw    zero, 4(t1)
    sw    zero, 0(t1)
    csrsi mstatus, 8
    nop
    CHEQUEA(2, s1, 1)
    csrci mstatus, 8

    /* 3. Las dos pendientes a la vez: MSI (3) antes que MTI (7) */
    li    s0, 0
    li    s1, 0
    li    t1, CLINT_MSIP
    li    t2, 1
    sw    t2, 0(t1)
    li    t1, CLINT_MTIMECMP
    sw    zero, 0(t1)
    sw    zero, 4(t1)
    li    t0, 0x88
    csrw  mie, t0
    csrsi mstatus, 8
    nop
    nop
    csrci mstatus, 8
    CHEQUEA(3, s3, 3)
    CHEQUEA(4, s4, 7)
    CHEQUEA(5, s0, 1)
    CHEQUEA(6, s1, 1)

    /* 4. Modo vectorizado: la MTI va a base + 4·7 */
    la    t0, vtabla
    ori   t0, t0, 1
    csrw  mtvec, t0
    li    s1, 0
    li    t1, CLINT_MTIMECMP
    sw    zero, 0(t1)
    sw    zero, 4(t1)
    li    t0, 0x80
    csrw  mie, t0
    csrsi mstatus, 8
    nop
    csrci mstatus, 8
    CHEQUEA(7, s1, 1)

    /* 5. Una dentro de otra: el manejador de la MTI pone MSIP y abre MIE */
    li    s0, 0
    li    s1, 0
    li    s5, 1                 /* el manejador de MTI anida */
    li    t1, CLINT_MTIMECMP
    sw    zero, 0(t1)
    sw    zero, 4(t1)
    li    t0, 0x88
    csrw  mie, t0
    csrsi mstatus, 8
    nop
    csrci mstatus, 8
    li    s5, 0
    CHEQUEA(8, s0, 1)
    CHEQUEA(9, s1, 1)
    CHEQUEA(10, s6, 3)          /* la anidada vio mcause = MSI */

    /* 6. wfi con la MSI ya pendiente y MIE = 0: no duerme, no entra */
    la    t0, directo
    csrw  mtvec, t0
    li    s0, 0
    li    t0, 8
    csrw  mie, t0
    li    t1, CLINT_MSIP
    li    t2, 1
    sw    t2, 0(t1)
    wfi
    CHEQUEA(11, s0, 0)
    csrsi mstatus, 8            /* y ahora sí */
    nop
    csrci mstatus, 8
    CHEQUEA(12, s0, 1)

    /* 7. mret devuelve MPIE a MIE: tras la interrupción, MIE vuelve a 1 */
    csrr  t0, mstatus
    andi  t0, t0, 8
    CHEQUEA(13, t0, 0)
    PASA

mtimecmp_lejos:
    li    t0, -1
    li    t1, CLINT_MTIMECMP
    sw    t0, 0(t1)
    sw    t0, 4(t1)
    ret

/* Modo directo: mcause dice cuál */
    .align 2
directo:
    csrr  t5, mcause
    li    t6, 0x80000003
    beq   t5, t6, d_msi
    li    t6, 0x80000007
    beq   t5, t6, d_mti
    li    a0, 99
    j     termina
d_msi:
    addi  s0, s0, 1
    bnez  s3, 1f
    li    s3, 3
    j     2f
1:  bnez  s4, 2f
    li    s4, 3
2:  li    t5, CLINT_MSIP
    sw    zero, 0(t5)
    mret
d_mti:
    addi  s1, s1, 1
    bnez  s3, 1f
    li    s3, 7
    j     2f
1:  bnez  s4, 2f
    li    s4, 7
2:  li    t5, -1
    li    t6, CLINT_MTIMECMP
    sw    t5, 0(t6)
    sw    t5, 4(t6)
    mret

    .option push
    .option norvc
    .align 8
vtabla:
    j     malo
    j     malo
    j     malo
    j     v_msi
    j     malo
    j     malo
    j     malo
    j     v_mti
    .option pop
malo:
    li    a0, 98
    j     termina
v_msi:
    addi  s0, s0, 1
    csrr  s6, mcause
    andi  s6, s6, 0xF
    li    t5, CLINT_MSIP
    sw    zero, 0(t5)
    mret
v_mti:
    addi  s1, s1, 1
    li    t5, -1
    li    t6, CLINT_MTIMECMP
    sw    t5, 0(t6)
    sw    t5, 4(t6)
    beqz  s5, 1f
    /* Anidar: guardar mepc, poner MSIP, abrir MIE (entra la MSI), cerrar */
    csrr  s7, mepc
    li    t5, CLINT_MSIP
    li    t6, 1
    sw    t6, 0(t5)
    csrsi mstatus, 8
    nop
    csrci mstatus, 8
    csrw  mepc, s7
    li    t5, 0x1800
    csrs  mstatus, t5           /* MPP = M otra vez (la anidada lo bajó) */
1:  mret
