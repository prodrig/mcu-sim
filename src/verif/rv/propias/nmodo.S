/* nmodo.S — El núcleo HP del ESP32-C6 en el laboratorio (E9): la delegación
 * de interrupciones a U (la extensión N: ustatus, uie, utvec, ucause, uepc,
 * uip y uret), mtvec y utvec solo vectorizados con base alineada a 256, mie
 * y mideleg de 32 bits con mideleg 0x111 de reset, y los identificadores.
 *
 * [TRM-C6 §1.5-§1.7; RISC-V Privileged 1.10 / 1.11, la extensión N]. Corre
 * con RV_ESP32C6_HP en el SoC del laboratorio: la interrupción es el MSIP del
 * CLINT (ID 3), que aquí se delega a U o no. */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, vec_m
    csrw  mtvec, t0
    /* PMP: todo abierto para U (entrada 15, NAPOT, RWX) */
    li    t0, -1
    csrw  pmpaddr15, t0
    li    t0, 0x1F000000
    csrw  pmpcfg3, t0

    /* 1. Identificadores y misa: A C I M U */
    csrr  t1, mvendorid
    CHEQUEA(1, t1, 0x612)
    csrr  t1, marchid
    CHEQUEA(2, t1, 0x80000002)
    csrr  t1, mimpid
    CHEQUEA(3, t1, 2)
    csrr  t1, misa
    CHEQUEA(4, t1, 0x40101105)

    /* 2. mideleg de reset: USI, UTI y la externa 8 */
    csrr  t1, mideleg
    CHEQUEA(5, t1, 0x111)

    /* 3. mtvec: solo vectorizado y la base alineada a 256 */
    li    t1, 0x12345
    csrw  mtvec, t1
    csrr  t1, mtvec
    CHEQUEA(6, t1, 0x12301)
    la    t0, vec_m
    csrw  mtvec, t0
    csrr  t1, mtvec
    ori   t0, t0, 1
    bne   t1, t0, f7
    /* utvec, igual */
    li    t1, 0x2345
    csrw  utvec, t1
    csrr  t1, utvec
    CHEQUEA(8, t1, 0x2301)

    /* 4. mie de 32 bits; uie ve solo lo delegado */
    li    t1, -1
    csrw  mie, t1
    csrr  t1, mie
    CHEQUEA(9, t1, -1)
    csrr  t1, uie
    CHEQUEA(10, t1, 0x111)
    csrw  mie, zero

    /* 5. MSI sin delegar desde U: va a M, a mtvec + 4·3, mcause 0x8000_0003 */
    li    s2, 0
    li    t1, 8
    csrs  mie, t1              /* MSIE */
    li    t1, 8
    csrs  mstatus, t1          /* MIE (en U las de M entran siempre) */
    la    s1, 1f
    call  a_u
    li    t2, CLINT_MSIP
    li    t3, 1
    sw    t3, 0(t2)            /* desde U */
    nop
    nop
    nop
    nop
    nop
    nop
1:  CHEQUEA(11, s2, 0x80000003)
    li    t1, 8
    csrc  mstatus, t1

    /* 6. MSI delegada a U: con ustatus.UIE y uie.MSIE, a utvec + 4·3,
     *    ucause 0x8000_0003, y uret vuelve a U */
    la    t0, vec_u
    csrw  utvec, t0
    li    t1, 8
    csrs  mideleg, t1
    csrs  uie, t1              /* el bit 3 de mie, ahora delegado */
    li    t1, 1
    csrs  ustatus, t1          /* UIE */
    li    s6, 0
    la    s1, 1f
    call  a_u
    li    t2, CLINT_MSIP
    li    t3, 1
    sw    t3, 0(t2)
    nop
    nop
    nop
    nop
    nop
    nop
    ecall                      /* de vuelta en U tras uret: a M */
1:  CHEQUEA(12, s6, 0x80000003)
    CHEQUEA(13, s2, 8)          /* la ecall de U llegó a M: uret volvió a U */
    csrr  t1, ustatus
    andi  t1, t1, 0x11
    CHEQUEA(14, t1, 0x11)       /* UIE otra vez a 1 (lo puso uret) y UPIE a 1 */
    csrr  t1, uepc
    li    a0, 15
    beqz  t1, termina

    /* 7. uip refleja lo pendiente delegado */
    li    t2, CLINT_MSIP
    li    t3, 1
    sw    t3, 0(t2)
    csrr  t1, uip
    andi  t1, t1, 8
    CHEQUEA(16, t1, 8)
    sw    zero, 0(t2)
    csrr  t1, uip
    andi  t1, t1, 8
    CHEQUEA(17, t1, 0)
    PASA
f7: FALLA(7)

/* Salta a U en la instrucción que sigue a la llamada */
a_u:
    li    t1, 0x1800
    csrc  mstatus, t1          /* MPP = U */
    csrw  mepc, ra
    mret

/* Las de M: la excepción en la base, la MSI en +12 (sin comprimidas, para
 * que cada entrada mida 4 bytes) */
    .option push
    .option norvc
    .align 8
vec_m:
    j     exc_m
    .word 0, 0
    j     msi_m
exc_m:
    csrr  s2, mcause
    li    t1, 0x1800
    csrs  mstatus, t1          /* volver a M */
    csrw  mepc, s1
    mret
msi_m:
    csrr  s2, mcause
    li    t2, CLINT_MSIP
    sw    zero, 0(t2)
    li    t1, 0x1800
    csrs  mstatus, t1
    csrw  mepc, s1
    mret

/* Las de U: la MSI delegada en +12 */
    .align 8
vec_u:
    j     vec_u
    .word 0, 0
    j     msi_u
msi_u:
    csrr  s6, ucause
    li    t2, CLINT_MSIP
    sw    zero, 0(t2)
    uret
    .option pop
