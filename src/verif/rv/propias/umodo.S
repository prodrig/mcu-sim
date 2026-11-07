/* umodo.S — El modo U: entrar, salir, privilegio de los CSR, TW, mcounteren,
 * MPRV y PMP (E1)
 *
 * [Privileged 20211203, §3.1.6, §3.1.11, §3.3.2, §3.7]. Se compara con Spike
 * (`--priv=MU`) además de comprobarse a sí mismo. */
#include "prueba.h"
    .text
    .globl main
main:
    la    t0, manejador
    csrw  mtvec, t0
    /* PMP: la entrada 15 abre todo (RWX) en NAPOT. Sin ninguna entrada que
     * coincida, U no puede tocar nada en cuanto hay PMP. La 0 deja `solo_r`
     * (64 bytes) en solo lectura. */
    li    t0, -1
    csrw  pmpaddr15, t0
    la    t0, solo_r
    srli  t0, t0, 2
    ori   t0, t0, 0x7          /* NAPOT de 64 bytes: ...0111 */
    csrw  pmpaddr0, t0
    li    t0, 0x19             /* NAPOT | R */
    csrw  pmpcfg0, t0
    li    t0, 0x1F000000       /* entrada 15: NAPOT | X | W | R */
    csrw  pmpcfg3, t0

    /* 1. A U con mret (MPP = 0), y una ecall desde U: causa 8 */
    la    s1, 1f
    call  a_u
    ecall
1:  CHEQUEA(1, s2, 8)

    /* 2. En U, leer mstatus: ilegal */
    la    s1, 1f
    call  a_u
    csrr  t1, mstatus
1:  CHEQUEA(2, s2, 2)

    /* 3. En U, mret: ilegal */
    la    s1, 1f
    call  a_u
    mret
1:  CHEQUEA(3, s2, 2)

    /* 4. cycle desde U con mcounteren = 0: ilegal; con el bit 0: se lee */
    csrw  mcounteren, zero
    la    s1, 1f
    call  a_u
    csrr  zero, cycle
1:  CHEQUEA(4, s2, 2)
    li    t1, 5
    csrw  mcounteren, t1       /* CY | IR */
    li    s2, 0
    la    s1, 1f
    call  a_u
    csrr  zero, cycle
    csrr  zero, instret
    ecall
1:  CHEQUEA(5, s2, 8)

    /* 5. wfi en U con TW = 1: ilegal */
    li    t1, 0x200000
    csrs  mstatus, t1
    la    s1, 1f
    call  a_u
    wfi
1:  CHEQUEA(6, s2, 2)
    li    t1, 0x200000
    csrc  mstatus, t1

    /* 6. PMP en U: leer `solo_r` sí; escribir, no (causa 7) */
    la    s5, solo_r
    li    s2, 0
    la    s1, 1f
    call  a_u
    lw    t1, 0(s5)
    ecall
1:  CHEQUEA(7, s2, 8)
    la    s1, 1f
    call  a_u
    sw    t1, 0(s5)
1:  CHEQUEA(8, s2, 7)
    li    a0, 9
    bne   s3, s5, termina

    /* 7. PMP en U: ejecutar donde no hay X (causa 1) */
    la    s1, 1f
    call  a_u
    jalr  ra, 0(s5)
1:  CHEQUEA(10, s2, 1)

    /* 8. En M, la entrada sin L no se aplica: escribir `solo_r` va bien */
    sw    t1, 0(s5)

    /* 9. MPRV = 1 con MPP = U: las cargas y almacenamientos de M se
     *    comprueban como de U */
    li    t1, 0x1800
    csrc  mstatus, t1          /* MPP = U */
    li    t1, 0x20000
    csrs  mstatus, t1          /* MPRV */
    la    s1, 1f
    sw    t1, 0(s5)
1:  li    t1, 0x20000
    csrc  mstatus, t1
    CHEQUEA(11, s2, 7)

    /* 10. Con L, la entrada se aplica también a M */
    li    t0, 0x99             /* L | NAPOT | R */
    csrw  pmpcfg0, t0
    la    s1, 1f
    sw    t1, 0(s5)
1:  CHEQUEA(12, s2, 7)
    /* y ya no se puede cambiar */
    li    t0, 0x1F
    csrw  pmpcfg0, t0
    csrr  t1, pmpcfg0
    andi  t1, t1, 0xFF
    CHEQUEA(13, t1, 0x99)
    PASA

/* Salta a U en la instrucción que sigue a la llamada */
a_u:
    li    t1, 0x1800
    csrc  mstatus, t1          /* MPP = U */
    csrw  mepc, ra
    mret

    .align 2
manejador:
    csrr  s2, mcause
    csrr  s3, mtval
    csrr  s4, mepc
    li    t1, 0x1800
    csrs  mstatus, t1          /* volver a M */
    csrw  mepc, s1
    mret

    .data
    .align 6
solo_r: .word 0x12345678
    .fill 15, 4, 0
