/* inicio.S — Arranque de los programas del laboratorio RISC-V
 * Pila al final de la RAM, .bss a cero, main() y su valor de vuelta a
 * `tohost` (HTIF): (código << 1) | 1, que es como terminan riscv-tests y como
 * los para Spike. 0 = bien.
 * Con varios harts (E2), solo el 0 hace todo eso; los demás van a
 * `otro_hart`, que por omisión duerme para siempre y que un programa puede
 * definir (con su pila: 4 KB por hart por debajo de la del 0). */
    .section .text.init, "ax", @progbits
    .globl _start
_start:
    csrr  t0, mhartid
    bnez  t0, 4f
    la    sp, __stack_top
    la    t0, __bss_start
    la    t1, __bss_end
1:  bgeu  t0, t1, 2f
    sw    zero, 0(t0)
    addi  t0, t0, 4
    j     1b
    /* Lo que deja la ROM de arranque de Spike (a0 = mhartid, a1 = el DTB, y
     * cinco instrucciones en mcycle y minstret) no tiene que verse: los
     * contadores y los argumentos de main empiezan a cero en los dos, y las
     * trazas se pueden comparar también donde se lee el reloj (CoreMark) */
2:  csrw  mcycle, zero
    csrw  mcycleh, zero
    csrw  minstret, zero
    csrw  minstreth, zero
    li    a0, 0
    li    a1, 0
    call  main
    .globl termina
termina:
    slli  a0, a0, 1
    ori   a0, a0, 1
    la    t0, tohost
    sw    a0, 0(t0)
    sw    zero, 4(t0)
3:  j     3b

4:  la    sp, __stack_top
    slli  t1, t0, 12
    sub   sp, sp, t1
    j     otro_hart

    .weak otro_hart
otro_hart:
    wfi
    j     otro_hart

    .section .tohost, "aw", @progbits
    .align 6
    .globl tohost
tohost:   .dword 0
    .align 6
    .globl fromhost
fromhost: .dword 0
