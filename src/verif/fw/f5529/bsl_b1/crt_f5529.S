; crt_f5529.S — lo que el BSL USB de TI (B1 del F5529) tiene en ensamblador:
; la Z-Area, la «BSL Action», BSL_Protect, el arranque y las firmas. Escrito
; para mcu-sim (AGPLv3) a partir del comportamiento que describen [UG5
; SLAU208Q §1.15] y [BSL SLAU319 §3.8]; las direcciones, las del BSL de TI
; (su .map: BSLSIG en 0x17F0-0x17FB).
; las constantes de msp430f5529.h (el .S no la incluye)
#define SYSBSLPE 0x8000
#define SYSBSLSIZE0 0x0001
#define SYSBSLSIZE1 0x0002
#define PUR_IN 0x0004
#define PSEIEN 0x0100
#define USBBGVBV 0x0008
        .section .zarea, "ax", @progbits          ; 0x1000-0x1007
        .global bsl_z
bsl_z:  jmp     __program_start                    ; la entrada
        jmp     bsl_accion0                        ; la «BSL Action»
        jmp     .
        jmp     .
        .section .text.accion0, "ax", @progbits
; R13 = 0xDEAD y R14 = 0xBEEF: vuelve al BSL (quita la dirección de vuelta)
bsl_accion0:
        cmp     #0xDEAD, r13
        jne     1f
        cmp     #0xBEEF, r14
        jne     1f
        pop.w   r12
        pop.w   r13
        reta
1:      clr     r12
        clr     r13
        reta
        .text
; BSL_Protect, que llama el arranque de la ROM tras el BOR: protege el BSL y
; pide el BSL (R12.1) con PUR alto, o con el vector de reset en blanco y VBUS
        .global bsl_protect
bsl_protect:
        clr.w   r12
        bis.w   #SYSBSLPE+SYSBSLSIZE0+SYSBSLSIZE1, &SYSBSLC
        mov.w   #0x9628, &USBKEYID
        bis.w   #PSEIEN, &USBPHYCTL
        bit.w   #PUR_IN, &USBCNF
        jnz     2f
        cmp.w   #0xFFFF, &0xFFFE
        jne     3f
        mov.w   #300, r11
4:      dec.w   r11
        jnz     4b
        bit.w   #USBBGVBV, &USBPWRCTL
        jz      3f
2:      bis.w   #2, r12
3:      bic.w   #PSEIEN, &USBPHYCTL
        clr.w   &USBKEYID
        reta
; El arranque del BSL: la pila arriba de su RAM, .bss a cero y main()
        .section .text.arranque, "ax", @progbits
        .global __program_start
__program_start:                                   ; (las interrupciones las quita PI_init)
        mov.w   #0x5A80, &0x015C                   ; el perro, parado (lo que hacía __low_level_init de IAR)
        mov.w   #0x3400, sp
        mov.w   #__bss_ini, r12
5:      cmp.w   #__bss_fin, r12
        jhs     6f
        clr.b   @r12
        inc.w   r12
        jmp     5b
6:      calla   #main                       ; no vuelve
; Las firmas [UG5 §1.15.3]: 0x17F2 BSL_Protect, 0x17F4/0x17F6 0x3CA5 y
; 0xC35A, 0x17FA la entrada; 0x17FC-0x17FF la llave de JTAG (sin poner)
        .section .bslsig, "a", @progbits
        .word   0xFFFF
        .word   bsl_protect
        .word   0x3CA5
        .word   0xC35A
        .word   0xFFFF
        .word   bsl_z
        .word   0xFFFF, 0xFFFF
