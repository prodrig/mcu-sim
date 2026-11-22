; crt_fr5.S — la Z-Area y el arranque del BSL B1 de las FR5xx/FR6xx (AGPLv3, mcu-sim)
;   0x1000  JMP bsl_inicio       la entrada (desde el arranque o la aplicación)
;   0x1002  JMP bsl_accion       la «BSL Action»: R13 = 0xDEAD y R14 = 0xBEEF vuelven al BSL
;   0x1004-0x100E  JMP $
        .section .zarea, "ax", @progbits
        .global bsl_z
bsl_z:  jmp     bsl_inicio
        jmp     bsl_accion
        jmp     .
        jmp     .
        jmp     .
        jmp     .
        jmp     .
        jmp     .
        .text
bsl_accion:
        cmp     #0xDEAD, r13
        jne     1f
        cmp     #0xBEEF, r14
        jne     1f
        jmp     bsl_inicio
1:      clr     r12
        clr     r13
        reta
        .global bsl_inicio
bsl_inicio:
        dint
        nop
        mov     #0x5A80, &0x015C              ; el perro, parado YA: corre desde el reset, y
                                            ; con TEST arriba el arranque puede haber esperado
        mov     #0x2000, sp                 ; la pila, en lo alto de la RAM del BSL
        mov     #0x1C00, r12                ; borra 0x1C00-0x1FFF
2:      clr     @r12
        incd    r12
        cmp     #0x2000, r12
        jne     2b
        calla   #bsl_principal
        jmp     .
