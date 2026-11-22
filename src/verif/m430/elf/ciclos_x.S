    .text
    .global prueba
prueba:
    pushm.a #7, r10
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
; X1 ADDX.W Rn,Rm
    mov #0, r6
    mov &0x0104, r4
    addx.w r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+162
; X1 ADDX.A Rn,Rm
    mov #0, r6
    mov &0x0104, r4
    addx.a r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+164
; X1 ADDX.W Rn,PC
    mov #0, r6
    mov &0x0104, r4
    addx.w r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+166
; X1 ADDX.A Rn,PC
    mov #0, r6
    mov &0x0104, r4
    addx.a r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+168
; X1 ADDX.W Rn,x(Rm)
    mov #0, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+170
; X1 MOVX.W Rn,x(Rm)
    mov #0, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+172
; X1 ADDX.A Rn,x(Rm)
    mov #0, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+174
; X1 MOVX.A Rn,x(Rm)
    mov #0, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+176
; X1 ADDX.W Rn,EDE
    mov #0, r6
    mov &0x0104, r4
    addx.w r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+178
; X1 MOVX.W Rn,EDE
    mov #0, r6
    mov &0x0104, r4
    movx.w r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+180
; X1 ADDX.A Rn,EDE
    mov #0, r6
    mov &0x0104, r4
    addx.a r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+182
; X1 MOVX.A Rn,EDE
    mov #0, r6
    mov &0x0104, r4
    movx.a r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+184
; X1 ADDX.W Rn,&EDE
    mov #0, r6
    mov &0x0104, r4
    addx.w r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+186
; X1 MOVX.W Rn,&EDE
    mov #0, r6
    mov &0x0104, r4
    movx.w r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+188
; X1 ADDX.A Rn,&EDE
    mov #0, r6
    mov &0x0104, r4
    addx.a r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+190
; X1 MOVX.A Rn,&EDE
    mov #0, r6
    mov &0x0104, r4
    movx.a r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+192
; X1 ADDX.W @Rn,Rm
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+194
; X1 ADDX.A @Rn,Rm
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+196
; X1 ADDX.W @Rn,PC
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+198
; X1 ADDX.A @Rn,PC
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+200
; X1 ADDX.W @Rn,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w @r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+202
; X1 MOVX.W @Rn,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w @r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+204
; X1 ADDX.A @Rn,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a @r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+206
; X1 MOVX.A @Rn,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a @r6, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+208
; X1 ADDX.W @Rn,EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+210
; X1 MOVX.W @Rn,EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.w @r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+212
; X1 ADDX.A @Rn,EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+214
; X1 MOVX.A @Rn,EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.a @r6, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+216
; X1 ADDX.W @Rn,&EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+218
; X1 MOVX.W @Rn,&EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.w @r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+220
; X1 ADDX.A @Rn,&EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+222
; X1 MOVX.A @Rn,&EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.a @r6, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+224
; X1 ADDX.W @Rn+,Rm
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6+, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+226
; X1 ADDX.A @Rn+,Rm
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6+, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+228
; X1 ADDX.W @Rn+,PC
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6+, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+230
; X1 ADDX.A @Rn+,PC
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6+, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+232
; X1 ADDX.W @Rn+,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w @r6+, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+234
; X1 MOVX.W @Rn+,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w @r6+, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+236
; X1 ADDX.A @Rn+,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a @r6+, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+238
; X1 MOVX.A @Rn+,x(Rm)
    mova #dato, r6
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a @r6+, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+240
; X1 ADDX.W @Rn+,EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6+, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+242
; X1 MOVX.W @Rn+,EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.w @r6+, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+244
; X1 ADDX.A @Rn+,EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6+, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+246
; X1 MOVX.A @Rn+,EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.a @r6+, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+248
; X1 ADDX.W @Rn+,&EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.w @r6+, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+250
; X1 MOVX.W @Rn+,&EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.w @r6+, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+252
; X1 ADDX.A @Rn+,&EDE
    mova #dato, r6
    mov &0x0104, r4
    addx.a @r6+, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+254
; X1 MOVX.A @Rn+,&EDE
    mova #dato, r6
    mov &0x0104, r4
    movx.a @r6+, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+256
; X1 ADDX.W #N,Rm
    mov &0x0104, r4
    addx.w #0x1000, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+258
; X1 ADDX.A #N,Rm
    mov &0x0104, r4
    addx.a #0x1000, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+260
; X1 ADDX.W #N,PC
    mov &0x0104, r4
    addx.w #6, pc
    nop
    nop
    nop
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+262
; X1 ADDX.A #N,PC
    mov &0x0104, r4
    addx.a #6, pc
    nop
    nop
    nop
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+264
; X1 ADDX.W #N,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w #0x1000, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+266
; X1 MOVX.W #N,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w #0x1000, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+268
; X1 ADDX.A #N,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a #0x1000, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+270
; X1 MOVX.A #N,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a #0x1000, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+272
; X1 ADDX.W #N,EDE
    mov &0x0104, r4
    addx.w #0x1000, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+274
; X1 MOVX.W #N,EDE
    mov &0x0104, r4
    movx.w #0x1000, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+276
; X1 ADDX.A #N,EDE
    mov &0x0104, r4
    addx.a #0x1000, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+278
; X1 MOVX.A #N,EDE
    mov &0x0104, r4
    movx.a #0x1000, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+280
; X1 ADDX.W #N,&EDE
    mov &0x0104, r4
    addx.w #0x1000, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+282
; X1 MOVX.W #N,&EDE
    mov &0x0104, r4
    movx.w #0x1000, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+284
; X1 ADDX.A #N,&EDE
    mov &0x0104, r4
    addx.a #0x1000, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+286
; X1 MOVX.A #N,&EDE
    mov &0x0104, r4
    movx.a #0x1000, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+288
; X1 ADDX.W X(Rn),Rm
    mova #dato-2, r7
    mov &0x0104, r4
    addx.w 2(r7), r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+290
; X1 ADDX.A X(Rn),Rm
    mova #dato-2, r7
    mov &0x0104, r4
    addx.a 2(r7), r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+292
; X1 ADDX.W X(Rn),PC
    mova #dato-2, r7
    mov &0x0104, r4
    addx.w 2(r7), pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+294
; X1 ADDX.A X(Rn),PC
    mova #dato-2, r7
    mov &0x0104, r4
    addx.a 2(r7), pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+296
; X1 ADDX.W X(Rn),x(Rm)
    mova #dato-2, r7
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w 2(r7), 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+298
; X1 MOVX.W X(Rn),x(Rm)
    mova #dato-2, r7
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w 2(r7), 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+300
; X1 ADDX.A X(Rn),x(Rm)
    mova #dato-2, r7
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a 2(r7), 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+302
; X1 MOVX.A X(Rn),x(Rm)
    mova #dato-2, r7
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a 2(r7), 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+304
; X1 ADDX.W X(Rn),EDE
    mova #dato-2, r7
    mov &0x0104, r4
    addx.w 2(r7), dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+306
; X1 MOVX.W X(Rn),EDE
    mova #dato-2, r7
    mov &0x0104, r4
    movx.w 2(r7), dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+308
; X1 ADDX.A X(Rn),EDE
    mova #dato-2, r7
    mov &0x0104, r4
    addx.a 2(r7), dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+310
; X1 MOVX.A X(Rn),EDE
    mova #dato-2, r7
    mov &0x0104, r4
    movx.a 2(r7), dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+312
; X1 ADDX.W X(Rn),&EDE
    mova #dato-2, r7
    mov &0x0104, r4
    addx.w 2(r7), &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+314
; X1 MOVX.W X(Rn),&EDE
    mova #dato-2, r7
    mov &0x0104, r4
    movx.w 2(r7), &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+316
; X1 ADDX.A X(Rn),&EDE
    mova #dato-2, r7
    mov &0x0104, r4
    addx.a 2(r7), &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+318
; X1 MOVX.A X(Rn),&EDE
    mova #dato-2, r7
    mov &0x0104, r4
    movx.a 2(r7), &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+320
; X1 ADDX.W EDE,Rm
    mov &0x0104, r4
    addx.w dato, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+322
; X1 ADDX.A EDE,Rm
    mov &0x0104, r4
    addx.a dato, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+324
; X1 ADDX.W EDE,PC
    mov &0x0104, r4
    addx.w dato, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+326
; X1 ADDX.A EDE,PC
    mov &0x0104, r4
    addx.a dato, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+328
; X1 ADDX.W EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+330
; X1 MOVX.W EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+332
; X1 ADDX.A EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+334
; X1 MOVX.A EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+336
; X1 ADDX.W EDE,EDE
    mov &0x0104, r4
    addx.w dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+338
; X1 MOVX.W EDE,EDE
    mov &0x0104, r4
    movx.w dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+340
; X1 ADDX.A EDE,EDE
    mov &0x0104, r4
    addx.a dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+342
; X1 MOVX.A EDE,EDE
    mov &0x0104, r4
    movx.a dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+344
; X1 ADDX.W EDE,&EDE
    mov &0x0104, r4
    addx.w dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+346
; X1 MOVX.W EDE,&EDE
    mov &0x0104, r4
    movx.w dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+348
; X1 ADDX.A EDE,&EDE
    mov &0x0104, r4
    addx.a dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+350
; X1 MOVX.A EDE,&EDE
    mov &0x0104, r4
    movx.a dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+352
; X1 ADDX.W &EDE,Rm
    mov &0x0104, r4
    addx.w &dato, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+354
; X1 ADDX.A &EDE,Rm
    mov &0x0104, r4
    addx.a &dato, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+356
; X1 ADDX.W &EDE,PC
    mov &0x0104, r4
    addx.w &dato, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+358
; X1 ADDX.A &EDE,PC
    mov &0x0104, r4
    addx.a &dato, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+360
; X1 ADDX.W &EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.w &dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+362
; X1 MOVX.W &EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.w &dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+364
; X1 ADDX.A &EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    addx.a &dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+366
; X1 MOVX.A &EDE,x(Rm)
    mova #dato2-2, r9
    mov &0x0104, r4
    movx.a &dato, 2(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+368
; X1 ADDX.W &EDE,EDE
    mov &0x0104, r4
    addx.w &dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+370
; X1 MOVX.W &EDE,EDE
    mov &0x0104, r4
    movx.w &dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+372
; X1 ADDX.A &EDE,EDE
    mov &0x0104, r4
    addx.a &dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+374
; X1 MOVX.A &EDE,EDE
    mov &0x0104, r4
    movx.a &dato, dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+376
; X1 ADDX.W &EDE,&EDE
    mov &0x0104, r4
    addx.w &dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+378
; X1 MOVX.W &EDE,&EDE
    mov &0x0104, r4
    movx.w &dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+380
; X1 ADDX.A &EDE,&EDE
    mov &0x0104, r4
    addx.a &dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+382
; X1 MOVX.A &EDE,&EDE
    mov &0x0104, r4
    movx.a &dato, &dato2
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+384
; X1 RPT #4 ADDX.W Rn,Rm
    mov #1, r6
    mov #0, r8
    mov &0x0104, r4
    rpt #4 { addx.w r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+386
; X1 RPT #16 RRCX.W Rn
    mov #0, r8
    mov &0x0104, r4
    rpt #16 { rrcx.w r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+388
; X2 RRCX Rn
    mova #0, r6
    clrc
    mov &0x0104, r4
    rrcx.w r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+390
; X2 RRCX @Rn
    mova #dato, r6
    clrc
    mov &0x0104, r4
    rrcx.w @r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+392
; X2 RRCX @Rn+
    mova #dato, r6
    clrc
    mov &0x0104, r4
    rrcx.w @r6+
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+394
; X2 RRCX X(Rn)
    mova #dato-2, r7
    clrc
    mov &0x0104, r4
    rrcx.w 2(r7)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+396
; X2 RRCX EDE
    clrc
    mov &0x0104, r4
    rrcx.w dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+398
; X2 RRCX &EDE
    clrc
    mov &0x0104, r4
    rrcx.w &dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+400
; X2 RRCX.A Rn
    mova #0, r6
    clrc
    mov &0x0104, r4
    rrcx.a r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+402
; X2 RRCX.A @Rn
    mova #dato, r6
    clrc
    mov &0x0104, r4
    rrcx.a @r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+404
; X2 RRCX.A @Rn+
    mova #dato, r6
    clrc
    mov &0x0104, r4
    rrcx.a @r6+
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+406
; X2 RRCX.A X(Rn)
    mova #dato-2, r7
    clrc
    mov &0x0104, r4
    rrcx.a 2(r7)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+408
; X2 RRCX.A EDE
    clrc
    mov &0x0104, r4
    rrcx.a dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+410
; X2 RRCX.A &EDE
    clrc
    mov &0x0104, r4
    rrcx.a &dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+412
; X2 PUSHX Rn
    mova #0, r6
    mov &0x0104, r4
    pushx.w r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+414
    incd r1
; X2 PUSHX @Rn
    mova #dato, r6
    mov &0x0104, r4
    pushx.w @r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+416
    incd r1
; X2 PUSHX @Rn+
    mova #dato, r6
    mov &0x0104, r4
    pushx.w @r6+
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+418
    incd r1
; X2 PUSHX #N
    mov &0x0104, r4
    pushx.w #0x1234
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+420
    incd r1
; X2 PUSHX X(Rn)
    mova #dato-2, r7
    mov &0x0104, r4
    pushx.w 2(r7)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+422
    incd r1
; X2 PUSHX EDE
    mov &0x0104, r4
    pushx.w dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+424
    incd r1
; X2 PUSHX &EDE
    mov &0x0104, r4
    pushx.w &dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+426
    incd r1
; X2 PUSHX.A Rn
    mova #0, r6
    mov &0x0104, r4
    pushx.a r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+428
    adda #4, r1
; X2 PUSHX.A @Rn
    mova #dato, r6
    mov &0x0104, r4
    pushx.a @r6
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+430
    adda #4, r1
; X2 PUSHX.A @Rn+
    mova #dato, r6
    mov &0x0104, r4
    pushx.a @r6+
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+432
    adda #4, r1
; X2 PUSHX.A #N
    mov &0x0104, r4
    pushx.a #0x12345
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+434
    adda #4, r1
; X2 PUSHX.A X(Rn)
    mova #dato-2, r7
    mov &0x0104, r4
    pushx.a 2(r7)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+436
    adda #4, r1
; X2 PUSHX.A EDE
    mov &0x0104, r4
    pushx.a dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+438
    adda #4, r1
; X2 PUSHX.A &EDE
    mov &0x0104, r4
    pushx.a &dato
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+440
    adda #4, r1
; X2 CALLA Rn
    mova #ruta, r10
    mov &0x0104, r4
    calla r10
    sub r4, r5
    sub #3, r5
    mov r5, &res+442
; X2 CALLA @Rn
    mova #tab_ruta, r10
    mov &0x0104, r4
    calla @r10
    sub r4, r5
    sub #3, r5
    mov r5, &res+444
; X2 CALLA @Rn+
    mova #tab_ruta, r10
    mov &0x0104, r4
    calla @r10+
    sub r4, r5
    sub #3, r5
    mov r5, &res+446
; X2 CALLA #N
    mov &0x0104, r4
    calla #ruta
    sub r4, r5
    sub #3, r5
    mov r5, &res+448
; X2 CALLA X(Rn)
    mova #tab_ruta-4, r10
    mov &0x0104, r4
    calla 4(r10)
    sub r4, r5
    sub #3, r5
    mov r5, &res+450
; X2 CALLA EDE
    mov &0x0104, r4
    calla tab_ruta-2
    sub r4, r5
    sub #3, r5
    mov r5, &res+452
; X2 CALLA &EDE
    mov &0x0104, r4
    calla &tab_ruta
    sub r4, r5
    sub #3, r5
    mov r5, &res+454
; X2 RRCM.A #1
    mova #0x12345, r8
    mov &0x0104, r4
    rrcm.a #1, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+456
; X2 RRAM.A #1
    mova #0x12345, r8
    mov &0x0104, r4
    rram.a #1, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+458
; X2 RLAM.A #1
    mova #0x12345, r8
    mov &0x0104, r4
    rlam.a #1, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+460
; X2 RRUM.A #1
    mova #0x12345, r8
    mov &0x0104, r4
    rrum.a #1, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+462
; X2 RRCM.A #4
    mova #0x12345, r8
    mov &0x0104, r4
    rrcm.a #4, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+464
; X2 RRAM.A #4
    mova #0x12345, r8
    mov &0x0104, r4
    rram.a #4, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+466
; X2 RLAM.A #4
    mova #0x12345, r8
    mov &0x0104, r4
    rlam.a #4, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+468
; X2 RRUM.A #4
    mova #0x12345, r8
    mov &0x0104, r4
    rrum.a #4, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+470
; X2 PUSHM.W #1
    mov &0x0104, r4
    pushm.w #1, r12
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+472
    popm.w #1, r12
; X2 PUSHM.A #1
    mov &0x0104, r4
    pushm.a #1, r12
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+474
    popm.a #1, r12
; X2 POPM.A #1
    pushm.a #1, r12
    mov &0x0104, r4
    popm.a #1, r12
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+476
; X2 PUSHM.W #3
    mov &0x0104, r4
    pushm.w #3, r12
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+478
    popm.w #3, r12
; X2 PUSHM.A #3
    mov &0x0104, r4
    pushm.a #3, r12
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+480
    popm.a #3, r12
; X2 POPM.A #3
    pushm.a #3, r12
    mov &0x0104, r4
    popm.a #3, r12
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+482
; MOVA Rn,Rm
    mova #0, r6
    mov &0x0104, r4
    mova r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+484
; MOVA Rn,PC
    mova #1f, r6
    mov &0x0104, r4
    mova r6, pc
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+486
; MOVA Rn,x(Rm)
    mova #0, r6
    mova #dato2_a-4, r9
    mov &0x0104, r4
    mova r6, 4(r9)
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+488
; MOVA Rn,&EDE
    mova #0, r6
    mov &0x0104, r4
    mova r6, &dato2_a
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+490
; MOVA @Rn,Rm
    mova #dato_a, r6
    mov &0x0104, r4
    mova @r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+492
; MOVA @Rn,PC
    mova #dato_a, r6
    mova #1f, r11
        mova r11, &dato_a
    mov &0x0104, r4
    mova @r6, pc
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+494
; MOVA @Rn+,Rm
    mova #dato_a, r6
    mov &0x0104, r4
    mova @r6+, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+496
; MOVA @Rn+,PC
    mova #dato_a, r6
    mova #1f, r11
        mova r11, &dato_a
    mov &0x0104, r4
    mova @r6+, pc
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+498
; MOVA #N,Rm
    mov &0x0104, r4
    mova #0x12345, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+500
; MOVA #N,PC
    mov &0x0104, r4
    mova #1f, pc
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+502
; MOVA X(Rn),Rm
    mova #dato_a-4, r7
    mov &0x0104, r4
    mova 4(r7), r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+504
; MOVA X(Rn),PC
    mova #dato_a-4, r7
    mova #1f, r11
        mova r11, &dato_a
    mov &0x0104, r4
    mova 4(r7), pc
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+506
; MOVA &EDE,Rm
    mov &0x0104, r4
    mova &dato_a, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+508
; MOVA &EDE,PC
    mova #1f, r11
        mova r11, &dato_a
    mov &0x0104, r4
    mova &dato_a, pc
    1:
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+510
; CMPA Rn,Rm
    mova #0, r6
    mov &0x0104, r4
    cmpa r6, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+512
; ADDA Rn,PC
    mova #0, r6
    mov &0x0104, r4
    adda r6, pc
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+514
; CMPA #N,Rm
    mova #0, r6
    mov &0x0104, r4
    cmpa #0x00000, r8
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+516
; ADDA #N,PC
    mova #0, r6
    mov &0x0104, r4
    adda #4, pc
    nop
    nop
    mov &0x0104, r5
    sub r4, r5
    sub #3, r5
    mov r5, &res+518
    popm.a #7, r10
    ret
rut:
    mov &0x0104, r5
    ret
ruta:
    mov &0x0104, r5
    reta
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
tab_ruta: .long ruta
    .long ruta
modo_irq: .word 0
    .global res
res: .space 520
    .global n_filas
n_filas: .word 260
    .section __interrupt_vector_20,"ax",@progbits
    .word isr
