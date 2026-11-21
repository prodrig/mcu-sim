; crt_g2.S — el arranque del BSL B1 del G2553 (AGPLv3, mcu-sim): en 0x0C00,
; la pila arriba de la RAM (0x0400), sin datos que copiar.
        .section .entrada, "ax", @progbits
        .global bsl_entrada
bsl_entrada:
        dint
        nop
        mov     #0x0400, sp
        mov     #0x0200, r12               ; borra la RAM (0x0200-0x03FF)
1:      clr     @r12
        incd    r12
        cmp     #0x0400, r12
        jne     1b
        call    #bsl_principal
        jmp     .
; 0x0FF0-0x0FFF: lo que lee la orden de la versión y JTAG [DS G2553: el ID en 0x0FF0]
        .section .info_rom, "a", @progbits
        .byte   0x25, 0x53                 ; 0x0FF0: el ID del chip (0x2553)
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x02, 0x04                 ; 0x0FFA: la versión del BSL, 2.04 [de modelo]
        .byte   0xFF, 0xFF
        .word   bsl_entrada                ; 0x0FFE
