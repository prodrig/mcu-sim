    .text
    .global prueba
prueba:
    push r4
    push r5
    push r6
    push r7
    push r8
    push r9
    push r10
; F1 ADD Rn,Rm
    mov #0, r6
    mov &0x0104, r4
    add r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+0
; F1 ADD Rn,PC
    mov #0, r6
    mov &0x0104, r4
    add r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+2
; F1 ADD Rn,x(Rm)
    mov #0, r6
    mov #dato2-2, r9
    mov &0x0104, r4
    add r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+4
; F1 MOV Rn,x(Rm)
    mov #0, r6
    mov #dato2-2, r9
    mov &0x0104, r4
    mov r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+6
; F1 ADD Rn,EDE
    mov #0, r6
    mov &0x0104, r4
    add r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+8
; F1 MOV Rn,EDE
    mov #0, r6
    mov &0x0104, r4
    mov r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+10
; F1 ADD Rn,&EDE
    mov #0, r6
    mov &0x0104, r4
    add r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+12
; F1 MOV Rn,&EDE
    mov #0, r6
    mov &0x0104, r4
    mov r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+14
; F1 ADD @Rn,Rm
    mov #dato, r6
    mov &0x0104, r4
    add @r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+16
; F1 ADD @Rn,PC
    mov #dato, r6
    mov &0x0104, r4
    add @r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+18
; F1 ADD @Rn,x(Rm)
    mov #dato, r6
    mov #dato2-2, r9
    mov &0x0104, r4
    add @r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+20
; F1 MOV @Rn,x(Rm)
    mov #dato, r6
    mov #dato2-2, r9
    mov &0x0104, r4
    mov @r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+22
; F1 ADD @Rn,EDE
    mov #dato, r6
    mov &0x0104, r4
    add @r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+24
; F1 MOV @Rn,EDE
    mov #dato, r6
    mov &0x0104, r4
    mov @r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+26
; F1 ADD @Rn,&EDE
    mov #dato, r6
    mov &0x0104, r4
    add @r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+28
; F1 MOV @Rn,&EDE
    mov #dato, r6
    mov &0x0104, r4
    mov @r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+30
; F1 ADD @Rn+,Rm
    mov #dato, r6
    mov &0x0104, r4
    add @r6+, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+32
; F1 ADD @Rn+,PC
    mov #dato, r6
    mov &0x0104, r4
    add @r6+, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+34
; F1 ADD @Rn+,x(Rm)
    mov #dato, r6
    mov #dato2-2, r9
    mov &0x0104, r4
    add @r6+, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+36
; F1 MOV @Rn+,x(Rm)
    mov #dato, r6
    mov #dato2-2, r9
    mov &0x0104, r4
    mov @r6+, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+38
; F1 ADD @Rn+,EDE
    mov #dato, r6
    mov &0x0104, r4
    add @r6+, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+40
; F1 MOV @Rn+,EDE
    mov #dato, r6
    mov &0x0104, r4
    mov @r6+, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+42
; F1 ADD @Rn+,&EDE
    mov #dato, r6
    mov &0x0104, r4
    add @r6+, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+44
; F1 MOV @Rn+,&EDE
    mov #dato, r6
    mov &0x0104, r4
    mov @r6+, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+46
; F1 ADD #N,Rm
    mov &0x0104, r4
    add #0x1000, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+48
; F1 ADD #N,PC
    mov &0x0104, r4
    add #6, pc
    nop
    nop
    nop
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+50
; F1 ADD #N,x(Rm)
    mov #dato2-2, r9
    mov &0x0104, r4
    add #0x1000, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+52
; F1 MOV #N,x(Rm)
    mov #dato2-2, r9
    mov &0x0104, r4
    mov #0x1000, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+54
; F1 ADD #N,EDE
    mov &0x0104, r4
    add #0x1000, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+56
; F1 MOV #N,EDE
    mov &0x0104, r4
    mov #0x1000, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+58
; F1 ADD #N,&EDE
    mov &0x0104, r4
    add #0x1000, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+60
; F1 MOV #N,&EDE
    mov &0x0104, r4
    mov #0x1000, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+62
; F1 ADD X(Rn),Rm
    mov #dato-2, r7
    mov &0x0104, r4
    add 2(r7), r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+64
; F1 ADD X(Rn),PC
    mov #dato-2, r7
    mov &0x0104, r4
    add 2(r7), pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+66
; F1 ADD X(Rn),x(Rm)
    mov #dato-2, r7
    mov #dato2-2, r9
    mov &0x0104, r4
    add 2(r7), 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+68
; F1 MOV X(Rn),x(Rm)
    mov #dato-2, r7
    mov #dato2-2, r9
    mov &0x0104, r4
    mov 2(r7), 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+70
; F1 ADD X(Rn),EDE
    mov #dato-2, r7
    mov &0x0104, r4
    add 2(r7), dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+72
; F1 MOV X(Rn),EDE
    mov #dato-2, r7
    mov &0x0104, r4
    mov 2(r7), dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+74
; F1 ADD X(Rn),&EDE
    mov #dato-2, r7
    mov &0x0104, r4
    add 2(r7), &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+76
; F1 MOV X(Rn),&EDE
    mov #dato-2, r7
    mov &0x0104, r4
    mov 2(r7), &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+78
; F1 ADD EDE,Rm
    mov &0x0104, r4
    add dato, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+80
; F1 ADD EDE,PC
    mov &0x0104, r4
    add dato, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+82
; F1 ADD EDE,x(Rm)
    mov #dato2-2, r9
    mov &0x0104, r4
    add dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+84
; F1 MOV EDE,x(Rm)
    mov #dato2-2, r9
    mov &0x0104, r4
    mov dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+86
; F1 ADD EDE,EDE
    mov &0x0104, r4
    add dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+88
; F1 MOV EDE,EDE
    mov &0x0104, r4
    mov dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+90
; F1 ADD EDE,&EDE
    mov &0x0104, r4
    add dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+92
; F1 MOV EDE,&EDE
    mov &0x0104, r4
    mov dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+94
; F1 ADD &EDE,Rm
    mov &0x0104, r4
    add &dato, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+96
; F1 ADD &EDE,PC
    mov &0x0104, r4
    add &dato, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+98
; F1 ADD &EDE,x(Rm)
    mov #dato2-2, r9
    mov &0x0104, r4
    add &dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+100
; F1 MOV &EDE,x(Rm)
    mov #dato2-2, r9
    mov &0x0104, r4
    mov &dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+102
; F1 ADD &EDE,EDE
    mov &0x0104, r4
    add &dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+104
; F1 MOV &EDE,EDE
    mov &0x0104, r4
    mov &dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+106
; F1 ADD &EDE,&EDE
    mov &0x0104, r4
    add &dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+108
; F1 MOV &EDE,&EDE
    mov &0x0104, r4
    mov &dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+110
; F2 RRA Rn
    mov #0, r6
    clrc
    mov &0x0104, r4
    rra r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+112
; F2 PUSH Rn
    mov #0, r6
    mov &0x0104, r4
    push r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+114
    incd r1
; F2 CALL Rn
    mov #rut, r10
    mov &0x0104, r4
    call r10
    sub r4, r5
    sub #3, r5
    mov r5, &res+116
; F2 RRA @Rn
    mov #dato, r6
    clrc
    mov &0x0104, r4
    rra @r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+118
; F2 PUSH @Rn
    mov #dato, r6
    mov &0x0104, r4
    push @r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+120
    incd r1
; F2 CALL @Rn
    mov #tab_rut, r10
    mov &0x0104, r4
    call @r10
    sub r4, r5
    sub #3, r5
    mov r5, &res+122
; F2 RRA @Rn+
    mov #dato, r6
    clrc
    mov &0x0104, r4
    rra @r6+
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+124
; F2 PUSH @Rn+
    mov #dato, r6
    mov &0x0104, r4
    push @r6+
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+126
    incd r1
; F2 CALL @Rn+
    mov #tab_rut, r10
    mov &0x0104, r4
    call @r10+
    sub r4, r5
    sub #3, r5
    mov r5, &res+128
; F2 PUSH #N
    mov &0x0104, r4
    push #0x1234
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+130
    incd r1
; F2 CALL #N
    mov &0x0104, r4
    call #rut
    sub r4, r5
    sub #3, r5
    mov r5, &res+132
; F2 RRA X(Rn)
    mov #dato-2, r7
    clrc
    mov &0x0104, r4
    rra 2(r7)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+134
; F2 PUSH X(Rn)
    mov #dato-2, r7
    mov &0x0104, r4
    push 2(r7)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+136
    incd r1
; F2 CALL X(Rn)
    mov #tab_rut-2, r10
    mov &0x0104, r4
    call 2(r10)
    sub r4, r5
    sub #3, r5
    mov r5, &res+138
; F2 RRA EDE
    clrc
    mov &0x0104, r4
    rra dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+140
; F2 PUSH EDE
    mov &0x0104, r4
    push dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+142
    incd r1
; F2 CALL EDE
    mov &0x0104, r4
    call tab_rut
    sub r4, r5
    sub #3, r5
    mov r5, &res+144
; F2 RRA &EDE
    clrc
    mov &0x0104, r4
    rra &dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+146
; F2 PUSH &EDE
    mov &0x0104, r4
    push &dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+148
    incd r1
; F2 CALL &EDE
    mov &0x0104, r4
    call &tab_rut
    sub r4, r5
    sub #3, r5
    mov r5, &res+150
; F2 CALL X(SP)
    push #rut
    push #0
    mov &0x0104, r4
    call 2(r1)
    sub r4, r5
    sub #3, r5
    mov r5, &res+152
    add #4, r1
; JMP
    mov &0x0104, r4
    jmp 1f
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+154
; JNE (no salta)
    clrz
    mov &0x0104, r4
    jeq 1f
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+156
; IRQ (aceptar)
    eint
    nop
    mov &0x0104, r4
    mov #0xFFE8, &0x0108
    nop
    sub r4, r5
    sub #3, r5
    mov r5, &res+158
    dint
    nop
; RETI
    eint
    nop
    mov #1, &modo_irq
    mov #0xFFE8, &0x0108
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+160
    dint
    nop
    mov #0, &modo_irq
    pop r10
    pop r9
    pop r8
    pop r7
    pop r6
    pop r5
    pop r4
    ret
rut:
    mov &0x0104, r5
    ret
isr:
    mov &0x0104, r5
    tst &modo_irq
    jz 1f
    mov &0x0104, r4
1:  reti
    .data
    .balign 4
dato: .word 0
    .word 0
dato2: .word 0
    .word 0
dato_a: .long 0
dato2_a: .long 0
ceroa: .long 0
tab_rut: .word rut
    .word rut
modo_irq: .word 0
    .global res
res: .space 162
    .global n_filas
n_filas: .word 81
    .section __interrupt_vector_20,"ax",@progbits
    .word isr
