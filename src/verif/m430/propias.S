; =============================================================================
; propias.S — Las pruebas propias del núcleo MSP430 (M1 de doc/analisis_msp430.md)
;
; Lo que los programas diferenciales no pueden mirar, porque el simulador de
; GDB lo hace distinto de la guía (gen_dif.py lo aparta), y lo que no tiene
; oráculo simulado: interrupciones, bajo consumo y los registros del
; laboratorio. El resultado esperado de cada una está en sc_main_430.cpp, con
; la fuente al lado. Cada prueba deja palabras en `res` (la k-ésima, en
; res + 2k).
;
; Se ensambla dos veces: -mcpu=msp430 (CPU) y -mcpu=msp430x (CPUX; corre en
; CPUX y en CPUXV2). Con -mlarge (CPUX_GRANDE), además, la interrupción desde
; una rutina por encima de 64 KB.
; =============================================================================
#if defined(__MSP430X__) || defined(__MSP430X_LARGE__)
#define CPUX 1
#endif

// El SP es de 20 bits en la CPUX: con -mlarge está por encima de 64 KB, y una
// instrucción .W sobre él le borraría 19:16
#ifdef CPUX
#define SP_A_R12    mova r1, r12
#define SUB_SP_R12  suba r1, r12
#define INCD_SP     adda #2, r1
#else
#define SP_A_R12    mov r1, r12
#define SUB_SP_R12  sub r1, r12
#define INCD_SP     incd r1
#endif

#define LAB_PIDE    0x0108
#define LAB_NMI     0x010C
#define LAB_RET     0x0110
#define LAB_VECRET  0x0112
#define LAB_CIC     0x0104

        .text
        .global prueba
prueba:
#ifdef CPUX
        pushm.a #7, r10
#else
        push r4
        push r5
        push r6
        push r7
        push r8
        push r9
        push r10
#endif
        mov #res, r10                  ; el puntero a los resultados (lo mantiene cada prueba)

; --- P1: MOV.B @SP+ sube el SP en 2 [UG2 POP.B: «el SP sube siempre en 2»] ---
        SP_A_R12
        push #0x1234
        mov.b @r1+, r13
        SUB_SP_R12                     ; 0 si el SP ha vuelto a donde estaba
        mov r12, 0(r10)
        mov r13, 2(r10)                ; 0x0034
        add #4, r10

; --- P2: PUSH.B: SP - 2, el byte en el bajo [UG2 PUSH] ---
        SP_A_R12
        mov #0xFFFF, r13
        push r13
        INCD_SP
        push.b #0x5A
        mov @r1, r14                   ; 0xFF5A: el alto no se toca
        INCD_SP
        SUB_SP_R12
        mov r12, 0(r10)                ; 0
        mov r14, 2(r10)                ; 0xFF5A
        add #4, r10

; --- P3: la interrupción y RETI; EINT retrasa una instrucción [UG2 EINT, §2.2.3] ---
        mov #0, &cuenta
        mov #0, r9
        mov #0xFFE8, &LAB_PIDE         ; pendiente con GIE a 0: no entra
        nop
        mov &cuenta, 0(r10)            ; 0
        eint
        mov #1, r9                     ; se ejecuta antes de la rutina
        nop
        dint
        nop
        mov &cuenta, 2(r10)            ; 1
        mov &r9_en_isr, 4(r10)         ; 1
        mov &sr_en_isr, 6(r10)         ; el SR dentro: 0 (CPU, CPUX); SCG0 se queda en CPUXV2
        add #8, r10

; --- P4: el SR al entrar: se borra (menos SCG0 en la CPUXV2) [UG2 §2.2.3.1; SLAU208 §1.3.4.1] ---
        bis #0x0040, r2                ; SCG0
        mov #0xFFE8, &LAB_PIDE
        eint
        nop
        dint
        nop
        bic #0x0040, r2
        mov &sr_en_isr, 0(r10)         ; CPU/CPUX: 0; CPUXV2: 0x0040
        mov &sr_apilado, 2(r10)        ; el SR apilado: SCG0 | GIE = 0x0048
        add #4, r10

; --- P5: prioridad: el vector más alto, primero [UG2 §2.2.3] ---
        mov #0, &orden
        mov #0xFFE8, &LAB_PIDE
        mov #0xFFF0, &LAB_PIDE
        eint
        nop
        nop
        dint
        nop
        mov &orden, 0(r10)             ; 0xF0E8: primero 0xFFF0 y luego 0xFFE8
        add #2, r10

; --- P6: NMI con GIE a 0 (el controlador del laboratorio) ---
        mov #0, &cuenta
        mov #0xFFE8, &LAB_NMI
        nop
        nop
        mov &cuenta, 0(r10)            ; 1
        add #2, r10

; --- P7: LPM0 y despertar; el reloj de la CPU no cuenta dormida ---
        mov #0, &cuenta
        mov #0, &dormir
        mov #0xFFE8, &LAB_VECRET
        mov &LAB_CIC, r12
        mov #2000, &LAB_RET            ; dentro de 2000 ciclos
        bis #0x0018, r2                ; LPM0 + GIE
        nop
        dint
        nop
        mov &LAB_CIC, r13
        sub r12, r13
        mov &cuenta, 0(r10)            ; 1
        cmp #1000, r13                 ; los ciclos de MCLK dormido no cuentan
        mov r2, r12
        and #1, r12                    ; C = 1 si r13 >= 1000: tiene que ser 0
        mov r12, 2(r10)
        add #4, r10

; --- P8: RETI vuelve a dormir si la rutina no toca el SR apilado [§3.4] ---
        mov #0, &cuenta
        mov #1, &dormir                ; la rutina: la 1.ª vez no despierta
        mov #0xFFE8, &LAB_VECRET
        mov #300, &LAB_RET
        bis #0x0018, r2
        nop
        dint
        nop
        mov &cuenta, 0(r10)            ; 2
        add #2, r10

#ifdef CPUX
; --- X1: SXT en registro: el bit 7 a 19:8 [SLAU391F SXT] ---
        mova #0x12380, r5
        sxt r5
        mova r5, &tmpa
        mov &tmpa, 0(r10)              ; 0xFF80
        mov &tmpa+2, 2(r10)            ; 0x000F
; --- X2: SXTX.W en registro: también 19:8 [SLAU391F SXTX] ---
        mova #0x12345, r5
        sxtx.w r5
        mova r5, &tmpa
        mov &tmpa, 4(r10)              ; 0x0045
        mov &tmpa+2, 6(r10)            ; 0x0000
; --- X3: SXT en memoria, y sus banderas [SLAU391F SXT] ---
        mov #0x1280, &tmp
        mov #0x0100, r2                ; V a 1: tiene que bajar
        sxt &tmp
        mov r2, r12
        mov &tmp, 8(r10)               ; 0xFF80
        mov r12, 10(r10)               ; N|C = 0x0005
        add #12, r10
; --- X4: SWPBX.A y SWPBX.W en registro [SLAU391F SWPBX, sus dos ejemplos] ---
        mova #0x23456, r5
        swpbx.a r5
        mova r5, &tmpa
        mov &tmpa, 0(r10)              ; 0x5634
        mov &tmpa+2, 2(r10)            ; 0x0002
        mova #0x23456, r5
        swpbx.w r5
        mova r5, &tmpa
        mov &tmpa, 4(r10)              ; 0x5634
        mov &tmpa+2, 6(r10)            ; 0x0000
; --- X5: SWPBX.A en memoria: 19:16 se quedan, 31:20 a 0 [SLAU391F SWPBX] ---
        mov #0x3456, &tmpa
        mov #0xFFF2, &tmpa+2
        swpbx.a &tmpa
        mov &tmpa, 8(r10)              ; 0x5634
        mov &tmpa+2, 10(r10)           ; 0x0002
        add #12, r10
; --- X6: DADDX.A: cinco cifras [SLAU391F DADDX] ---
        mova #0x19999, r5
        clrc
        daddx.a #0x00001, r5
        mov r2, r12
        mova r5, &tmpa
        mov &tmpa, 0(r10)              ; 0x0000
        mov &tmpa+2, 2(r10)            ; 0x0002 (20000)
        and #0x0003, r12
        mov r12, 4(r10)                ; 0: ni C ni Z
        mova #0x99999, r5
        clrc
        daddx.a #0x00001, r5
        mov r2, r12
        and #0x0003, r12
        mova r5, &tmpa
        mov &tmpa, 6(r10)              ; 0x0000
        mov r12, 8(r10)                ; C|Z = 3
        add #10, r10
; --- X7: V de SUBA, ADDA y CMPA (20 bits) [SLAU391F] ---
        mova #0x7FFFF, r5
        mova #0xFFFFF, r6
        suba r6, r5                    ; 7FFFF - (-1) = 80000: desborda
        mov r2, 0(r10)                 ; N|V = 0x0104
        mova #0x7FFFF, r5
        adda #1, r5
        mov r2, 2(r10)                 ; N|V = 0x0104
        mova #0x80000, r5
        cmpa #1, r5                    ; 80000 - 1: desborda; sin préstamo
        mov r2, 4(r10)                 ; V|C = 0x0101
        add #6, r10
; --- X8: RRC y RRCX borran V [UG2 RRC; SLAU391F tabla 1-14] ---
        mov #0x0101, r2
        mov #2, r5
        rrc r5
        mov r2, 0(r10)                 ; N (de C) = 0x0004
        mov #0x0100, r2
        mova #0x00002, r5
        rrcx.a r5
        mov r2, 2(r10)                 ; 0
        add #4, r10
; --- X9: CALLA EDE (la palabra de índice es la base) y RETA ---
        mov #0, r12
        calla tab_f-2                  ; binutils 2.34 lo resuelve desde el código: -2
        mov r12, 0(r10)                ; 0x00F1
        add #2, r10
; --- X10: MOVX.A a memoria: la palabra alta, con 19:16 y 31:20 a 0 ---
        mov #0xFFFF, &tmpa+2
        movx.a #0xABCDE, &tmpa
        mov &tmpa, 0(r10)              ; 0xBCDE
        mov &tmpa+2, 2(r10)            ; 0x000A
; --- X11: PUSHX.A / POPX.A: el SP en 4 ---
        mova r1, r12
        pushx.a #0x12345
        popx.a &tmpa
        suba r1, r12
        mov r12, 4(r10)                ; 0
        mov &tmpa+2, 6(r10)            ; 0x0001
; --- X12: PUSHM.A y POPM.A en orden [SLAU391F PUSHM, POPM] ---
        mova #0x11111, r11
        mova #0x22222, r12
        mova #0x33333, r13
        pushm.a #3, r13
        popm.a #3, r6                  ; r4 = 11111, r5 = 22222, r6 = 33333
        mova r4, &tmpa
        mov &tmpa+2, 8(r10)            ; 1
        mova r6, &tmpa
        mov &tmpa+2, 10(r10)           ; 3
        add #12, r10
; --- X13: un MOV.W a un registro borra 19:16 [SLAU391F §1.4.1] ---
        mova #0xFFFFF, r5
        mov #0x1234, r5
        mova r5, &tmpa
        mov &tmpa+2, 0(r10)            ; 0
        mova #0xFFFFF, r5
        mov.b #0x12, r5
        mova r5, &tmpa
        mov &tmpa, 2(r10)              ; 0x0012
        add #4, r10
#endif
#ifdef __MSP430X_LARGE__
; --- L1: una interrupción desde por encima de 64 KB: PC.19:16 en el SR apilado [SLAU391F §1.2] ---
        calla #arriba
        mov &sr_apilado, 0(r10)        ; 0x1008 (PC.19:16 = 1 en 15:12; GIE)
        mov &pc_apilado, 2(r10)        ; la palabra baja del PC de vuelta
        mov &pc_arriba, 4(r10)
        add #6, r10
#endif
        mov r10, &fin_res
#ifdef CPUX
        popm.a #7, r10
#else
        pop r10
        pop r9
        pop r8
        pop r7
        pop r6
        pop r5
        pop r4
#endif
#ifdef __MSP430X_LARGE__
        reta
#else
        ret
#endif

#ifdef CPUX
f_lab:  mov #0x00F1, r12
        reta
#endif

; La rutina de interrupción del vector 0xFFE8
isr:
        mov r9, &r9_en_isr
        mov r2, &sr_en_isr
        mov @r1, &sr_apilado
        mov 2(r1), &pc_apilado
        inc &cuenta
        swpb &orden                    ; el orden de entrada (byte bajo del vector)
        bic #0x00FF, &orden
        bis #0x00E8, &orden
        tst &dormir
        jz 1f
        dec &dormir                    ; la 1.ª vez: no despierta; vuelve a pedir
        mov #300, &LAB_RET
        reti
1:      bic #0x0010, 0(r1)             ; despierta (CPUOFF fuera del SR apilado)
        reti

isr_f0:
        swpb &orden
        bic #0x00FF, &orden
        bis #0x00F0, &orden
        reti

#ifdef __MSP430X_LARGE__
        .section .upper.text,"ax",@progbits
arriba:
        mov #0xFFE8, &LAB_PIDE
        eint
        nop
        dint
        nop
        mova #arriba, r12
        mov r12, &pc_arriba
        reta
#endif

        .data
        .balign 4
cuenta:     .word 0
dormir:     .word 0
orden:      .word 0
r9_en_isr:  .word 0
sr_en_isr:  .word 0
sr_apilado: .word 0
pc_apilado: .word 0
pc_arriba:  .word 0
tmp:        .word 0
tmpa:       .long 0
#ifdef CPUX
tab_f:      .long f_lab
#endif
        .global res
res:        .space 128
        .global fin_res
fin_res:    .word 0
        .section __interrupt_vector_20,"ax",@progbits
        .word isr
        .section __interrupt_vector_24,"ax",@progbits
        .word isr_f0
