; crt_fr2.S — la Z-Area y el arranque del BSL B1 de las FR2xx/FR4xx (AGPLv3, mcu-sim)
;   0x1000  JMP bsl_inicio       la entrada (desde el arranque o la aplicación)
;   0x1002  JMP bsl_accion       la «BSL Action»: R13 = 0xDEAD y R14 = 0xBEEF vuelven al BSL
;   0x1004-0x100E  JMP $
;   Las variables y la pila del BSL, en 0x2200-0x23FF (la RAM que tienen
;   todas); las variables se borran aquí; la RAM de la tabla de BSLFRAM §7
;   (desde 0x2000), en bsl_principal, si la configuración no dice que no.
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
#ifdef BSL_FR4
        ret
#else
        reta
#endif
        .global bsl_inicio
bsl_inicio:
        dint
        nop
        mov     #0x5A80, &0x01CC              ; el perro, parado YA: corre desde el reset, y
                                            ; con TEST arriba el arranque puede haber esperado
        mov     #0x2400, sp
        mov     #__bss_start, r12
2:      cmp     #__bss_end, r12
        jhs     3f
        clr.b   @r12
        inc     r12
        jmp     2b
#ifdef BSL_FR4
3:      call    #bsl_principal
#else
3:      calla   #bsl_principal
#endif
        jmp     .
