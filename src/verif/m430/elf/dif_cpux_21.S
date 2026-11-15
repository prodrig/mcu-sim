    .text
    .global prueba
    .global fin
prueba:
    pushm.a #7, r10
    mov r1, &sp_guardado
    movx.a #res, &resptr
; bloque 0000
    mova #0x5DFE5, r11
    mov #0x4, r2
    andx.w zona+128, r11
    call #guarda
; bloque 0001
    mova #0x15ACF, r6
    mov #0x1, r2
    pushm.w #1, r6
    popm.w #1, r7
    call #guarda
; bloque 0002
    mov #0x5, r2
    pushx.w #-1
    pop r15
    call #guarda
; bloque 0003
    mova #0xCD7CD, r12
    mov #0x5, r2
    addx.a zona+124, r12
    call #guarda
; bloque 0004
    mova #0x24508, r7
    mov #0x3, r2
    rrcm.a #2, r7
    call #guarda
; bloque 0005
    mov #zona+128, r11
    mov #0x6, r2
    addc.b #0xA2, 116(r11)
    call #guarda
; bloque 0006
    mova #0x71AE4, r12
    mov #0x2, r2
    and.b r12, zona+17
    call #guarda
; bloque 0007
    mov #zona+128, r13
    mov #0x3, r2
    xorx.w #2, 66(r13)
    call #guarda
; bloque 0008
    mov #0x0062, r7
    mov #0x3, r2
    daddx.b #8, r7
    call #guarda
; bloque 0009
    mov #0, r15
    mov #0x6, r2
    calla #sub
    call #guarda
; bloque 000a
    mova #0x946A7, r8
    mov #0x0000, r4
    mov #0x7, r2
    mova r8, r4
    call #guarda
; bloque 000b
    mova #0xFFFFF, r4
    mova #0xF5483, r5
    mov #0x0000, r6
    mov #0x0800, r7
    mov #0x0, r2
    pushm.a #4, r7
    popm.a #4, r11
    call #guarda
; bloque 000c
    mov #zona+34, r12
    mov #0x2, r2
    rrux.w @r12
    call #guarda
; bloque 000d
    mov #0x3, r2
    addc.w #0xD79A, &zona+82
    call #guarda
; bloque 000e
    mov #0x0000, r5
    mov #0x0000, r6
    mov #0x3, r2
    pushm.a #2, r6
    popm.a #2, r11
    call #guarda
; bloque 000f
    mov #0, r15
    mov #tabla, r8
    mov #0x6, r2
    call @r8
    call #guarda
; bloque 0010
    mov #zona+10, r13
    mov #0x0, r2
    addcx.w @r13+, &zona+192
    call #guarda
; bloque 0011
    mova #0xCBBE6, r10
    mova #0x59C08, r11
    mov #0x2, r2
    mova r10, r11
    call #guarda
; bloque 0012
    mov #zona+128, r12
    mov #0x2, r2
    movx.a #0xFAE90, 80(r12)
    call #guarda
; bloque 0013
    mov #zona+178, r13
    mov #0x4, r2
    swpb @r13
    call #guarda
; bloque 0014
    mova #0x7FFFF, r5
    mov #0x7, r2
    rram.w #4, r5
    call #guarda
; bloque 0015
    mov #zona+128, r12
    mov #0x7, r2
    rrc.b 4(r12)
    call #guarda
; bloque 0016
    mov #0x6657, r7
    mov #0x3, r2
    daddx.w #0x3107, r7
    call #guarda
; bloque 0017
    mov #zona+127, r10
    mov #0x3, r2
    bit.b @r10+, zona+234
    call #guarda
; bloque 0018
    mov #0x1000, r6
    mova #0x2E6FF, r9
    mov #0x1, r2
    mova r6, r9
    call #guarda
; bloque 0019
    mov #zona+172, r9
    mov #0x5, r2
    andx.a @r9, zona+52
    call #guarda
; bloque 001a
    mov #zona+60, r13
    mov #0x4, r2
    movx.w @r13, &zona+50
    call #guarda
; bloque 001b
    mov #0x7, r2
    pushx.b zona+171
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 001c
    mova #0x141B0, r5
    mov #0x4, r2
    subcx.b #4, r5
    call #guarda
; bloque 001d
    mov #zona+228, r4
    mov #0x3, r2
    bicx.w @r4+, zona+94
    call #guarda
; bloque 001e
    mova #0x7D17E, r7
    mov #0x1, r2
    rrcm.w #4, r7
    call #guarda
; bloque 001f
    mova #0x60680, r5
    mova #0xFFFFF, r6
    mova #0xFB97F, r7
    mova #0xAD0DB, r8
    mova #0xFFFFF, r9
    mov #0x3, r2
    pushm.a #5, r9
    popm.a #5, r9
    call #guarda
; bloque 0020
    mova #0xB86AF, r14
    mov #0x3, r2
    add.w &zona+68, r14
    call #guarda
; bloque 0021
    mova #0xD11C2, r9
    mova #0xD8D1D, r7
    mov #0x5, r2
    xorx.a r7, r9
    call #guarda
; bloque 0022
    mov #zona+128, r11
    mova #0xE20AA, r6
    mov #0x0, r2
    cmp.w r6, -2(r11)
    call #guarda
; bloque 0023
    mov #0x3, r2
    xor.w #0xBBE3, zona+52
    call #guarda
; bloque 0024
    mov #0, r15
    mov #0x2, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0025
    mov #0x1, r2
    pushx.b zona+198
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0026
    mova #0xB974D, r6
    mov #0xDD2C, r7
    mov #0xD768, r8
    mova #0x6BF70, r9
    mov #0x2, r2
    pushm.a #4, r9
    popm.a #4, r12
    call #guarda
; bloque 0027
    mov #zona+164, r11
    mov #0x0, r2
    bicx.a @r11+, zona+156
    call #guarda
; bloque 0028
    mov #0x8730, r4
    mov #0x0, r2
    dadd.w #0x5974, r4
    call #guarda
; bloque 0029
    mov #0x0073, r7
    mov #0x2, r2
    daddx.b #0x87, r7
    call #guarda
; bloque 002a
    mov #0x7, r2
    cmpx.w zona+176, &zona+182
    call #guarda
; bloque 002b
    mov #0, r15
    mov #0x2, r2
    calla &tabla_a
    call #guarda
; bloque 002c
    mova #0xFFFFF, r7
    mova #0x87993, r8
    mova #0xFFFFF, r9
    mov #0x0, r2
    pushm.a #3, r9
    popm.a #3, r8
    call #guarda
; bloque 002d
    mov #0x7, r2
    andx.a #0, &zona+94
    call #guarda
; bloque 002e
    mov #0xAD4F, r4
    mova #0x77075, r5
    mova #zona+128, r5
    mov #0x2, r2
    mova r4, 24(r5)
    call #guarda
; bloque 002f
    mov #zona+88, r10
    mov #0x4, r2
    push.b @r10
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0030
    mova #0x71593, r14
    mov #0x4, r2
    rrax.a r14
    call #guarda
; bloque 0031
    mov #zona+128, r4
    mov #0x3, r2
    mov.b #4, -73(r4)
    call #guarda
; bloque 0032
    mova #0x3B30C, r6
    mov #0x6, r2
    rram.a #2, r6
    call #guarda
; bloque 0033
    mov #0x3, r2
    rrc.w zona+144
    call #guarda
; bloque 0034
    mova #0xB70C7, r8
    mov #0x2, r2
    rrcm.w #1, r8
    call #guarda
; bloque 0035
    mov #0x0000, r4
    mova #0xF18A2, r5
    mov #0x4, r2
    pushm.w #2, r5
    popm.w #2, r15
    call #guarda
; bloque 0036
    mov #zona+128, r5
    mov #zona+128, r14
    mov #0x3, r2
    xorx.w -56(r14), -116(r5)
    call #guarda
; bloque 0037
    mova #0x7FFFF, r8
    mova #0xE29A2, r15
    mova #zona+232, r8
    mov #0x3, r2
    mova @r8+, r15
    call #guarda
; bloque 0038
    mova #0x65904, r12
    mov #0x1, r2
    rrc.b r12
    call #guarda
; bloque 0039
    mov #0x0020, r4
    mov #0x5, r2
    rrcx.a r4
    call #guarda
; bloque 003a
    mov #0, r15
    mov #0x4, r2
    calla &tabla_a
    call #guarda
; bloque 003b
    mov #0x1, r2
    bis.w #2, zona+36
    call #guarda
; bloque 003c
    mova #0xA7C0B, r15
    mova #0x80000, r12
    mov #0x2, r2
    and.w r12, r15
    call #guarda
; bloque 003d
    mova #0x6BA33, r7
    mova #0x61269, r8
    mova #0x6C5BB, r9
    mova #0xDC749, r10
    mova #0x7FFFF, r11
    mov #0x4, r2
    pushm.a #5, r11
    popm.a #5, r9
    call #guarda
; bloque 003e
    mova #0xF25E4, r5
    mov #0x0000, r6
    mov #0x3, r2
    adda r5, r6
    call #guarda
; bloque 003f
    mov #0, r15
    mova #sub, r10
    mov #0x0, r2
    calla r10
    call #guarda
; bloque 0040
    mov #zona+128, r15
    mov #0x6, r2
    mov.b zona+67, 97(r15)
    call #guarda
; bloque 0041
    mova #0x8EB0D, r14
    mova #0x52C0B, r6
    mov #0x1, r2
    rpt #11 { andx.a r14, r6
    call #guarda
; bloque 0042
    mov #zona+146, r14
    mov #0x0, r2
    rrc.w @r14
    call #guarda
; bloque 0043
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0044
    mov #0x1000, r8
    mova #0x7FFFF, r9
    mova #zona+128, r9
    mov #0x6, r2
    mova r8, 62(r9)
    call #guarda
; bloque 0045
    mov #0x2297, r11
    mov #0x0, r2
    dadd.w #0x671, r11
    call #guarda
; bloque 0046
    mova #0x487CA, r4
    mov #0x0F9D, r5
    mova #0x20150, r6
    mova #0xE1A17, r7
    mov #0x0000, r8
    mov #0x1, r2
    pushm.a #5, r8
    popm.a #5, r8
    call #guarda
; bloque 0047
    mova #0xFFFFF, r15
    mov #0x0080, r8
    mov #0x6, r2
    mova #0x00000, r8
    call #guarda
; bloque 0048
    mova #0x1A262, r9
    mov #0x7, r2
    sub.b &zona+233, r9
    call #guarda
; bloque 0049
    mov #0x5, r2
    sub.w &zona+196, zona+70
    call #guarda
; bloque 004a
    mov #0, r15
    mov #0x0, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 004b
    mova #0x54AF3, r14
    mov #0x0000, r15
    mov #0x2, r2
    pushm.w #2, r15
    popm.w #2, r7
    call #guarda
; bloque 004c
    mova #0x80000, r12
    mov #0x6, r2
    rrux.w r12
    call #guarda
; bloque 004d
    mova #0x795BA, r6
    mova #0xD6E28, r4
    mov #0x0020, r12
    mov #0x7, r2
    rpt r12 { subcx.b r6, r4
    call #guarda
; bloque 004e
    mov #zona+128, r15
    mov #0x6, r2
    subc.b 22(r15), zona+72
    call #guarda
; bloque 004f
    mova #0x34E31, r7
    mov #0x5, r2
    cmp.w #0x8000, r7
    call #guarda
; bloque 0050
    mova #0xF643F, r4
    mov #0x2, r2
    rrux.a r4
    call #guarda
; bloque 0051
    mova #0xC2950, r6
    mov #zona+164, r15
    mov #0x3, r2
    subx.a @r15, r6
    call #guarda
; bloque 0052
    mov #0x0000, r9
    mova #0xAC954, r10
    mov #0x4, r2
    xor.w r10, r9
    call #guarda
; bloque 0053
    mov #0x7, r2
    and.w &zona+36, &zona+64
    call #guarda
; bloque 0054
    mov #0x6309, r4
    mov #0x3, r2
    daddx.w #2, r4
    call #guarda
; bloque 0055
    mov #zona+128, r9
    mov #0x0, r2
    rra.b 100(r9)
    call #guarda
; bloque 0056
    mov #0x0000, r14
    mov #0x7, r2
    sub.b zona+70, r14
    call #guarda
; bloque 0057
    mov #0, r15
    mova #tabla_a, r10
    mov #0x7, r2
    calla @r10
    call #guarda
; bloque 0058
    mova #0x27828, r6
    mov #0x1000, r12
    mova #zona+134, r6
    mov #0x4, r2
    mova @r6+, r12
    call #guarda
; bloque 0059
    mova #0xFC873, r15
    mov #0x5, r2
    bic.w r15, &zona+102
    call #guarda
; bloque 005a
    mova #0x1A4A8, r14
    mov #0x3, r2
    rrcm.a #2, r14
    call #guarda
; bloque 005b
    mov #0x0, r2
    pushx.a zona+224
    popx.a r15
    call #guarda
; bloque 005c
    mova #0x1F8D4, r10
    mov #0x3, r2
    rram.w #1, r10
    call #guarda
; bloque 005d
    mov #0x2, r2
    addcx.w &zona+80, &zona+98
    call #guarda
; bloque 005e
    mov #zona+128, r6
    mov #0x0, r2
    sub.w #0x438C, -50(r6)
    call #guarda
; bloque 005f
    mov #0xCE46, r13
    mov #0x2, r2
    rram.w #1, r13
    call #guarda
; bloque 0060
    mova #0x806CD, r10
    mov #0x0000, r11
    mov #0x4, r2
    mova r10, &zona+248
    call #guarda
; bloque 0061
    mov #0, r15
    mov #0x3, r2
    calla #sub
    call #guarda
; bloque 0062
    mov #0, r15
    mov #0x3, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0063
    mova #0x4724D, r6
    mova #0x7281D, r8
    mov #0x7, r2
    rpt #7 { addcx.a r6, r8
    call #guarda
; bloque 0064
    mov #0x3, r2
    addcx.w #0x98F5, zona+170
    call #guarda
; bloque 0065
    mov #0, r15
    mov #0x2, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0066
    mov #zona+86, r6
    mov #0x4, r2
    sub.b @r6+, &zona+80
    call #guarda
; bloque 0067
    mov #zona+128, r6
    mov #0x4, r2
    bitx.b 6(r6), zona+216
    call #guarda
; bloque 0068
    mov #0, r15
    mov #tabla, r12
    mov #0x4, r2
    call @r12
    call #guarda
; bloque 0069
    mov #zona+128, r8
    mov #0x3, r2
    rrux.b -47(r8)
    call #guarda
; bloque 006a
    mova #0x7FFFE, r9
    mov #zona+162, r10
    mov #0x1, r2
    subx.b @r10, r9
    call #guarda
; bloque 006b
    mova #0xFB0AA, r13
    mov #0x6, r2
    bitx.b &zona+220, r13
    call #guarda
; bloque 006c
    mova #0x7FFFF, r8
    mov #0x0000, r9
    mova #0x7FFFE, r10
    mova #0x72CD6, r11
    mova #0xA5DA8, r12
    mov #0x1, r2
    pushm.w #5, r12
    popm.w #5, r15
    call #guarda
; bloque 006d
    mov #0, r15
    mov #0x5, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 006e
    mov #0x2000, r7
    mov #0x5, r2
    rra.w r7
    call #guarda
; bloque 006f
    mov #0, r15
    mov #0x1, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0070
    mova #0xFFFFF, r13
    mova #0x65548, r4
    mova #zona+128, r4
    mov #0x0, r2
    mova r13, -94(r4)
    call #guarda
; bloque 0071
    mov #zona+198, r13
    mov #0x0, r2
    swpbx @r13
    call #guarda
; bloque 0072
    mov #0x1, r2
    swpbx &zona+100
    call #guarda
; bloque 0073
    mov #0x0002, r10
    mov #0x1, r2
    pushx.b r10
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0074
    mov #0x1, r2
    rrcx.b zona+89
    call #guarda
; bloque 0075
    mov #0, r15
    mov #0x3, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0076
    mova #0xDD64E, r6
    mov #0x1, r2
    pushm.w #1, r6
    popm.w #1, r9
    call #guarda
; bloque 0077
    mova #0x80000, r6
    mov #zona+128, r7
    mov #0x1, r2
    subx.w -92(r7), r6
    call #guarda
; bloque 0078
    mov #0x5, r2
    rrcx.b zona+40
    call #guarda
; bloque 0079
    mova #0x7FFFE, r7
    mov #0x4, r2
    swpb r7
    call #guarda
; bloque 007a
    mov #zona+128, r14
    mov #0x4, r2
    subx.a -124(r14), &zona+106
    call #guarda
; bloque 007b
    mov #zona+128, r11
    mov #0x5, r2
    push.w -106(r11)
    pop r15
    call #guarda
; bloque 007c
    mova #0x32EBF, r7
    mov #0x3, r2
    swpb r7
    call #guarda
; bloque 007d
    mov #zona+128, r12
    mov #0x0, r2
    mov.w &zona+156, 6(r12)
    call #guarda
; bloque 007e
    mov #0x4, r2
    bis.w #4, &zona+160
    call #guarda
; bloque 007f
    mov #zona+34, r8
    mov #0x1, r2
    subc.b @r8+, zona+204
    call #guarda
; bloque 0080
    mov #zona+219, r12
    mov #0x3, r2
    subx.b @r12, &zona+99
    call #guarda
; bloque 0081
    mova #0xFFFFF, r11
    mov #0x3, r2
    rrum.a #4, r11
    call #guarda
; bloque 0082
    mova #0x4216A, r12
    mov #zona+128, r15
    mov #0x2, r2
    cmpx.a -126(r15), r12
    call #guarda
; bloque 0083
    mov #zona+80, r14
    mov #0x5, r2
    bis.w @r14+, &zona+70
    call #guarda
; bloque 0084
    mov #0, r15
    mov #0x3, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0085
    mov #0x7, r2
    cmpx.b zona+72, zona+180
    call #guarda
; bloque 0086
    mov #0x6, r2
    addx.b zona+78, &zona+242
    call #guarda
; bloque 0087
    mova #0xFFFFF, r13
    mov #0x1, r2
    rram.a #2, r13
    call #guarda
; bloque 0088
    mov #0x0000, r15
    mov #0x2, r2
    rpt #1 { rrax.a r15
    call #guarda
; bloque 0089
    mov #zona+128, r13
    mov #0x6, r2
    rrax.a -18(r13)
    call #guarda
; bloque 008a
    mov #0x0000, r11
    mova #0xE540F, r12
    mov #0x0000, r13
    mova #0x3B2EC, r14
    mov #0x1, r2
    pushm.a #4, r14
    popm.a #4, r8
    call #guarda
; bloque 008b
    mova #0xF0CFB, r8
    mov #0x0004, r15
    mov #0x6, r2
    suba r8, r15
    call #guarda
; bloque 008c
    mova #0x80000, r11
    mova #0xB81F7, r12
    mov #0x3, r2
    mova #0xAD702, r12
    call #guarda
; bloque 008d
    mov #zona+128, r9
    mov #0x4, r2
    push.w 22(r9)
    pop r15
    call #guarda
; bloque 008e
    mova #0xC725C, r5
    mov #0x7, r2
    rrux.b r5
    call #guarda
; bloque 008f
    mov #0, r15
    mov #0x7, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0090
    mova #0xF11EE, r6
    mova #0x7FFFF, r5
    mova #zona+128, r5
    mov #0x4, r2
    mova r6, 82(r5)
    call #guarda
; bloque 0091
    mova #0x7FFFF, r7
    mov #0x2, r2
    addx.w r7, zona+210
    call #guarda
; bloque 0092
    mova #0xB5D07, r7
    mov #0x7, r2
    rrcm.a #3, r7
    call #guarda
; bloque 0093
    mova #0xBFDDB, r8
    mov #0x6, r2
    bit.w &zona+14, r8
    call #guarda
; bloque 0094
    mov #0, r15
    mov #0x6, r2
    call #subw
    call #guarda
; bloque 0095
    mova #0x63701, r10
    mov #zona+82, r13
    mov #0x2, r2
    subcx.w @r13, r10
    call #guarda
; bloque 0096
    mova #0x5658D, r11
    mov #0x5, r2
    movx.a r11, zona+74
    call #guarda
; bloque 0097
    mova #0xEE49C, r9
    mova #0xFFFFF, r5
    mov #0x2, r2
    rpt r5 { rrux.w r9
    call #guarda
; bloque 0098
    mov #0x0004, r4
    mova #0xB2A6C, r5
    mova #zona+34, r4
    mov #0x0, r2
    mova @r4, r5
    call #guarda
; bloque 0099
    mova #0x29A5B, r10
    mova #0xFFFFF, r5
    mov #0x4, r2
    adda r10, r5
    call #guarda
; bloque 009a
    mova #0xBE20A, r14
    mova #0x4E409, r4
    mov #0x5, r2
    andx.b r4, r14
    call #guarda
; bloque 009b
    mov #0, r15
    mov #0x2, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 009c
    mov #zona+5, r7
    mov #0x3, r2
    rrux.b @r7
    call #guarda
; bloque 009d
    mova #0x60D89, r9
    mov #zona+112, r6
    mov #0x7, r2
    and.w @r6, r9
    call #guarda
; bloque 009e
    mov #zona+128, r5
    mov #zona+90, r10
    mov #0x1, r2
    cmpx.w @r10, 24(r5)
    call #guarda
; bloque 009f
    mova #0xFFFFF, r9
    mov #0x4, r2
    rrum.w #3, r9
    call #guarda
; bloque 00a0
    mova #0x25F09, r11
    mov #zona+150, r14
    mov #0x2, r2
    bit.w @r14+, r11
    call #guarda
; bloque 00a1
    mova #0xD7C81, r6
    mov #0x6, r2
    rrum.a #1, r6
    call #guarda
; bloque 00a2
    mova #0xD222F, r5
    mova #0x40000, r6
    mova #0x944E9, r7
    mova #0x4C73A, r8
    mova #0x3883D, r9
    mov #0x2, r2
    pushm.a #5, r9
    popm.a #5, r13
    call #guarda
; bloque 00a3
    mova #0xD17AD, r11
    mov #0x0, r2
    rlam.a #2, r11
    call #guarda
; bloque 00a4
    mov #zona+128, r13
    mov #0x1, r2
    swpb -12(r13)
    call #guarda
; bloque 00a5
    mov #0x0770, r8
    mov #0x0, r2
    dadd.w #0x9201, r8
    call #guarda
; bloque 00a6
    mov #0x0000, r13
    mova #0xFFFFF, r12
    mov #0x4, r2
    mova #0x00400, r12
    call #guarda
; bloque 00a7
    mova #0xCC27B, r12
    mova #0xAB76E, r11
    mov #0x7, r2
    rpt #6 { bicx.b r12, r11
    call #guarda
; bloque 00a8
    mov #zona+128, r15
    mov #0x3, r2
    subx.b 109(r15), &zona+36
    call #guarda
; bloque 00a9
    mov #0x6, r2
    rrux.w zona+180
    call #guarda
; bloque 00aa
    mova #0x7FFFE, r11
    mov #0x0, r2
    rlam.a #3, r11
    call #guarda
; bloque 00ab
    mov #zona+36, r8
    mov #0x6, r2
    rrux.w @r8
    call #guarda
; bloque 00ac
    mova #0xFFFFF, r8
    mov #0x5, r2
    rram.a #2, r8
    call #guarda
; bloque 00ad
    mova #0x707E5, r12
    mova #0x6F825, r13
    mov #0x0000, r14
    mova #0x5C542, r15
    mov #0x4, r2
    pushm.a #4, r15
    popm.a #4, r10
    call #guarda
; bloque 00ae
    mov #0x2000, r12
    mov #0x7, r2
    addc.w zona+146, r12
    call #guarda
; bloque 00af
    mov #zona+4, r8
    mov #0x7, r2
    add.w @r8, zona+68
    call #guarda
; bloque 00b0
    mova #0x89DBB, r6
    mov #zona+128, r8
    mov #0x7, r2
    bisx.w -36(r8), r6
    call #guarda
; bloque 00b1
    mov #0x2, r2
    rrcx.b &zona+187
    call #guarda
; bloque 00b2
    mov #zona+128, r13
    mov #0x0000, r12
    mov #0x5, r2
    bic.w r12, 20(r13)
    call #guarda
; bloque 00b3
    mova #0x35369, r9
    mova #0x8C40E, r4
    mov #0x7, r2
    mova r9, &zona+18
    call #guarda
; bloque 00b4
    mov #0x6, r2
    rrc.w zona+212
    call #guarda
; bloque 00b5
    mova #0xFFFFF, r8
    mova #0x725E7, r4
    mova #zona+128, r8
    mov #0x1, r2
    mova 40(r8), r4
    call #guarda
; bloque 00b6
    mov #zona+72, r14
    mov #0x2, r2
    bicx.w @r14, &zona+26
    call #guarda
; bloque 00b7
    mova #0xF0F25, r9
    mova #0xDE17D, r8
    mova #zona+236, r9
    mov #0x2, r2
    mova @r9, r8
    call #guarda
; bloque 00b8
    mova #0x36DCF, r4
    mov #0x6, r2
    bit.b #8, r4
    call #guarda
; bloque 00b9
    mov #0x6, r2
    rrcx.b &zona+247
    call #guarda
; bloque 00ba
    mov #0x8792, r8
    mov #0x7619, r13
    mov #0x7, r2
    daddx.w r13, r8
    call #guarda
; bloque 00bb
    mova #0xC36B1, r11
    mov #zona+128, r5
    mov #0x2, r2
    andx.a 116(r5), r11
    call #guarda
; bloque 00bc
    mov #0x7, r2
    sub.w zona+64, &zona+32
    call #guarda
; bloque 00bd
    mov #zona+8, r7
    mov #0x7, r2
    swpb @r7
    call #guarda
; bloque 00be
    mov #zona+128, r15
    mov #0x1, r2
    rrcx.b -55(r15)
    call #guarda
; bloque 00bf
    mov #zona+128, r12
    mov #0x3, r2
    bis.b 85(r12), &zona+39
    call #guarda
; bloque 00c0
    mov #zona+128, r8
    mova #0xF58FF, r7
    mov #0x4, r2
    bis.w r7, 16(r8)
    call #guarda
; bloque 00c1
    mov #0x0000, r10
    mov #0x3, r2
    rrux.a r10
    call #guarda
; bloque 00c2
    mova #0x15870, r15
    mov #0x0, r2
    bis.b r15, r15
    call #guarda
; bloque 00c3
    mov #zona+128, r11
    mov #0x2, r2
    rrux.b -95(r11)
    call #guarda
; bloque 00c4
    mova #0x80000, r10
    mov #0x3, r2
    rrc.b r10
    call #guarda
; bloque 00c5
    mov #zona+128, r15
    mova #0x789CA, r11
    mov #0x3, r2
    add.b r11, -29(r15)
    call #guarda
; bloque 00c6
    mov #zona+164, r7
    mov #0x3, r2
    subc.w @r7, zona+68
    call #guarda
; bloque 00c7
    mov #0x1, r2
    push.w &zona+2
    pop r15
    call #guarda
; bloque 00c8
    mov #0, r15
    mov #0x0, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c9
    mova #0x80000, r7
    mova #0x7FFFE, r11
    mov #0x0000, r13
    mov #0x7, r2
    rpt r13 { bicx.b r7, r11
    call #guarda
; bloque 00ca
    mov #zona+128, r6
    mov #0x7, r2
    subx.w -54(r6), &zona+138
    call #guarda
; bloque 00cb
    mova #0xFA537, r5
    mova #0xF9CA9, r8
    mov #0x0, r2
    adda r5, r8
    call #guarda
; bloque 00cc
    mov #zona+128, r9
    mov #0x7, r2
    cmpx.a zona+214, 6(r9)
    call #guarda
; bloque 00cd
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ce
    mova #0x7FFFE, r15
    mova #0xDD82D, r10
    mov #0x0, r2
    mova &zona+146, r10
    call #guarda
; bloque 00cf
    mova #0xFFFFF, r12
    mova #0x35671, r4
    mova #zona+178, r12
    mov #0x6, r2
    mova @r12, r4
    call #guarda
; bloque 00d0
    mov #0x2, r2
    rrux.b zona+14
    call #guarda
; bloque 00d1
    mova #0x13458, r9
    mov #0x0000, r4
    mov #0x0, r2
    suba #0x80000, r4
    call #guarda
; bloque 00d2
    mov #0, r15
    mov #0x7, r2
    call #subw
    call #guarda
; bloque 00d3
    mov #zona+28, r14
    mov #0x0, r2
    subx.b @r14+, &zona+94
    call #guarda
; bloque 00d4
    mov #0x0000, r7
    mov #0x7, r2
    rlam.a #2, r7
    call #guarda
; bloque 00d5
    mova #0x25A1D, r6
    mova #0xF06A8, r15
    mov #0x7, r2
    adda r6, r15
    call #guarda
; bloque 00d6
    mova #0x8EB97, r9
    mova #0x10000, r10
    mova #0x80000, r4
    mov #0x3, r2
    rpt r4 { addcx.w r9, r10
    call #guarda
; bloque 00d7
    mov #0, r15
    mov #0x6, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d8
    mova #0x40000, r4
    mova #0xFFFFF, r10
    mov #0x6, r2
    suba #0x5DE6E, r10
    call #guarda
; bloque 00d9
    mova #0x6F0EE, r11
    mov #0x4, r2
    bit.w #8, r11
    call #guarda
; bloque 00da
    mova #0x94ACA, r11
    mov #0x0, r2
    rpt #11 { rrux.a r11
    call #guarda
; bloque 00db
    mov #0x2, r2
    rrux.w zona+134
    call #guarda
; bloque 00dc
    mov #zona+128, r6
    mov #zona+78, r8
    mov #0x6, r2
    addx.w @r8, -26(r6)
    call #guarda
; bloque 00dd
    mov #zona+98, r4
    mov #0x4, r2
    rrux.b @r4
    call #guarda
; bloque 00de
    mova #0xE02B4, r5
    mov #0x0, r2
    pushm.a #1, r5
    popm.a #1, r11
    call #guarda
; bloque 00df
    mov #0x6, r2
    andx.a &zona+154, &zona+166
    call #guarda
; bloque 00e0
    mov #0x3D24, r15
    mova #0x103FD, r9
    mov #0x1, r2
    xor.b r9, r15
    call #guarda
; bloque 00e1
    mova #0x5A043, r4
    mov #0x0000, r10
    mov #0x3, r2
    mova #0x00080, r10
    call #guarda
; bloque 00e2
    mova #0x9A546, r11
    mova #0xDD31E, r12
    mov #0x0, r2
    pushm.w #2, r12
    popm.w #2, r15
    call #guarda
; bloque 00e3
    mov #zona+248, r6
    mov #0x0, r2
    and.b @r6, &zona+13
    call #guarda
; bloque 00e4
    mov #0x0020, r9
    mov #0x0000, r11
    mov #0x6, r2
    rpt #1 { xorx.a r9, r11
    call #guarda
; bloque 00e5
    mov #0, r15
    mov #0x5, r2
    calla &tabla_a
    call #guarda
; bloque 00e6
    mov #0x4, r2
    rrcx.b zona+165
    call #guarda
; bloque 00e7
    mova #0xDFBC3, r13
    mova #0xF212E, r10
    mov #0x2, r2
    rpt r10 { rrux.a r13
    call #guarda
; bloque 00e8
    mova #0xC6009, r10
    mov #0x2, r2
    rrum.a #3, r10
    call #guarda
; bloque 00e9
    mova #0x80000, r13
    mov #0x7, r2
    bitx.b r13, &zona+247
    call #guarda
; bloque 00ea
    mov #zona+50, r15
    mov #0x4, r2
    bitx.a @r15+, &zona+96
    call #guarda
; bloque 00eb
    mov #0x8933, r7
    mov #0x1210, r11
    mov #0x7, r2
    rpt #13 { daddx.w r7, r11
    call #guarda
; bloque 00ec
    mov #0xDA25, r4
    mov #0x7, r2
    subx.b &zona+127, r4
    call #guarda
; bloque 00ed
    mova #0x7EBB0, r11
    mov #0x7, r2
    and.w r11, &zona+168
    call #guarda
; bloque 00ee
    mova #0xA2424, r9
    mova #0x92695, r10
    mova #0x80B31, r11
    mov #0x0000, r12
    mov #0x0000, r13
    mov #0x2, r2
    pushm.w #5, r13
    popm.w #5, r11
    call #guarda
; bloque 00ef
    mov #0x6, r2
    cmpx.a zona+170, &zona+172
    call #guarda
; bloque 00f0
    mov #0x0073, r9
    mov #0x3, r2
    dadd.b #0, r9
    call #guarda
; bloque 00f1
    mova #0xC30E3, r9
    mova #0x7FFFE, r10
    mov #0x5, r2
    adda r9, r10
    call #guarda
; bloque 00f2
    mov #0, r15
    mov #0x1, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f3
    mov #zona+128, r4
    mov #zona+128, r5
    mov #0x5, r2
    cmpx.a -76(r5), -66(r4)
    call #guarda
; bloque 00f4
    mov #0x4, r2
    addc.b &zona+155, zona+199
    call #guarda
; bloque 00f5
    mova #0xC84AF, r8
    mov #0x0080, r11
    mov #0x5, r2
    mova #0x2D9DA, r11
    call #guarda
; bloque 00f6
    mova #0x150C8, r11
    mov #0x4, r2
    subc.w &zona+154, r11
    call #guarda
; bloque 00f7
    mov #zona+90, r14
    mov #0x7, r2
    xorx.a @r14, &zona+114
    call #guarda
; bloque 00f8
    mov #zona+108, r13
    mov #0x6, r2
    swpbx @r13
    call #guarda
; bloque 00f9
    mov #0x0020, r4
    mova #0x728EE, r15
    mova #0x8DF96, r11
    mov #0x5, r2
    rpt r11 { addx.b r4, r15
    call #guarda
; bloque 00fa
    mova #0x267F7, r7
    mov #0x4, r2
    rlam.w #3, r7
    call #guarda
; bloque 00fb
    mov #0x0000, r10
    mova #0xFFFFF, r11
    mova #0xF25DA, r12
    mov #0x0000, r13
    mov #0x4, r2
    pushm.a #4, r13
    popm.a #4, r9
    call #guarda
; bloque 00fc
    mov #0, r15
    mov #subw, r6
    mov #0x1, r2
    call r6
    call #guarda
; bloque 00fd
    mov #0, r15
    mov #0x2, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 00fe
    mova #0x10000, r7
    mov #0x6, r2
    addcx.b r7, zona+82
    call #guarda
; bloque 00ff
    mova #0xE38F6, r9
    mov #0x2, r2
    bic.b r9, &zona+170
    call #guarda
; bloque 0100
    mova #0xFFFFF, r8
    mov #0x0000, r7
    mov #0x6, r2
    rpt #12 { subcx.w r8, r7
    call #guarda
; bloque 0101
    mov #0, r15
    mov #0x3, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0102
    mov #0x1A84, r15
    mov #0x6, r2
    rrcx.a r15
    call #guarda
; bloque 0103
    mov #zona+2, r14
    mov #0x4, r2
    push.w @r14
    pop r15
    call #guarda
; bloque 0104
    mov #0x4, r2
    push.b zona+162
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0105
    mova #0x13097, r15
    mova #0xC0342, r12
    mov #0x5, r2
    cmp.b r12, r15
    call #guarda
; bloque 0106
    mova #0x978AA, r11
    mova #0x10000, r14
    mov #0x2, r2
    andx.b r14, r11
    call #guarda
; bloque 0107
    mov #0x7589, r12
    mova #0xA1C60, r15
    mov #0x7, r2
    subx.a r15, r12
    call #guarda
; bloque 0108
    mov #0x0, r2
    bisx.a #4, zona+152
    call #guarda
; bloque 0109
    mov #0x6, r2
    swpbx zona+70
    call #guarda
; bloque 010a
    mov #0xC92F, r8
    mov #zona+128, r14
    mov #0x2, r2
    xor.w @r14+, r8
    call #guarda
; bloque 010b
    mov #0x1, r2
    rra.b zona+195
    call #guarda
; bloque 010c
    mov #0x0019, r11
    mov #0x7, r2
    dadd.b #2, r11
    call #guarda
; bloque 010d
    mov #0x2, r2
    bisx.w &zona+68, &zona+78
    call #guarda
; bloque 010e
    mov #0, r15
    mov #0x1, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 010f
    mov #zona+128, r7
    mov #0x1, r2
    cmp.w zona+172, -50(r7)
    call #guarda
; bloque 0110
    mov #0, r15
    mov #0x7, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0111
    mov #0, r15
    mova #tabla_a, r4
    mov #0x5, r2
    calla @r4
    call #guarda
; bloque 0112
    mova #0x99310, r12
    mov #0x5, r2
    bis.b r12, &zona+205
    call #guarda
; bloque 0113
    mov #0x68CE, r8
    mov #0x1, r2
    addcx.a zona+236, r8
    call #guarda
; bloque 0114
    mov #0x4, r2
    bitx.a #0x0, &zona+164
    call #guarda
; bloque 0115
    mova #0xFFFFF, r4
    mova #0xF729B, r10
    mov #0x0, r2
    mova r4, r10
    call #guarda
; bloque 0116
    mova #0x7FFFE, r5
    mov #0x7, r2
    rpt #10 { rrcx.a r5
    call #guarda
; bloque 0117
    mova #0xA7515, r6
    mova #0x7FFFF, r12
    mov #0x3, r2
    cmpa r6, r12
    call #guarda
; bloque 0118
    mova #0xC7EAC, r8
    mov #0x5, r2
    rrcm.w #4, r8
    call #guarda
; bloque 0119
    mov #zona+128, r12
    mov #0x1, r2
    bisx.a -66(r12), zona+234
    call #guarda
; bloque 011a
    mov #zona+128, r14
    mov #0x3, r2
    rra.b -101(r14)
    call #guarda
; bloque 011b
    mova #0x80000, r7
    mov #zona+162, r15
    mov #0x3, r2
    bit.w @r15, r7
    call #guarda
; bloque 011c
    mov #0x6, r2
    rrux.a zona+234
    call #guarda
; bloque 011d
    mov #0x0, r2
    rra.b &zona+63
    call #guarda
; bloque 011e
    mova #0xFFFFF, r12
    mov #0x0000, r9
    mov #0x3, r2
    cmpa r12, r9
    call #guarda
; bloque 011f
    mov #0x5, r2
    subcx.w #0x82AE, &zona+102
    call #guarda
; bloque 0120
    mova #0x1CBD5, r6
    mov #0x0, r2
    rlam.w #4, r6
    call #guarda
; bloque 0121
    mov #0x0000, r14
    mova #0x7FFFF, r13
    mov #0x2, r2
    rpt #8 { bicx.w r14, r13
    call #guarda
; bloque 0122
    mova #0xFFFFF, r10
    mova #0x2F965, r15
    mov #0x5, r2
    sub.b r15, r10
    call #guarda
; bloque 0123
    mov #0, r15
    mov #0x0, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0124
    mov #0x2, r2
    rrcx.b &zona+68
    call #guarda
; bloque 0125
    mova #0xB5AC6, r14
    mov #0x4, r2
    pushm.a #1, r14
    popm.a #1, r10
    call #guarda
; bloque 0126
    mov #zona+128, r13
    mova #0x3BCA8, r15
    mov #0x4, r2
    and.w r15, -8(r13)
    call #guarda
; bloque 0127
    mov #0x0800, r6
    mov #0x4, r2
    rrcx.a r6
    call #guarda
; bloque 0128
    mov #0x0010, r15
    mov #0x3, r2
    rra.b r15
    call #guarda
; bloque 0129
    mova #0x8986A, r5
    mov #0x0, r2
    bic.w r5, r5
    call #guarda
; bloque 012a
    mova #0x30469, r8
    mov #zona+128, r11
    mov #0x5, r2
    subcx.a @r11, r8
    call #guarda
; bloque 012b
    mova #0x40000, r14
    mov #zona+128, r7
    mov #0x3, r2
    cmp.w -98(r7), r14
    call #guarda
; bloque 012c
    mova #0xF566A, r9
    mov #zona+106, r10
    mov #0x4, r2
    xor.w @r10+, r9
    call #guarda
; bloque 012d
    mov #0, r15
    mov #0x4, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 012e
    mova #0xADFCE, r7
    mov #0x4, r2
    cmpx.b r7, &zona+67
    call #guarda
; bloque 012f
    mova #0xA1831, r10
    mov #0x6, r2
    bitx.b &zona+6, r10
    call #guarda
; bloque 0130
    mova #0x448CB, r13
    mov #0x5, r2
    sub.b r13, &zona+211
    call #guarda
; bloque 0131
    mova #0x40000, r6
    mova #0x9FB9F, r4
    mov #0x4, r2
    mova #0x2C7A6, r4
    call #guarda
; bloque 0132
    mov #0x9838, r10
    mov #0x0434, r11
    mov #0x6, r2
    dadd.w r11, r10
    call #guarda
; bloque 0133
    mova #0xB4869, r10
    mov #0x4, r2
    andx.w #0xA4EF, r10
    call #guarda
; bloque 0134
    mova #0xD2786, r11
    mov #0x2, r2
    subc.b #2, r11
    call #guarda
; bloque 0135
    mov #zona+128, r14
    mov #0x5, r2
    subx.b &zona+219, 65(r14)
    call #guarda
; bloque 0136
    mova #0x7B561, r7
    mov #0x5, r2
    rram.a #2, r7
    call #guarda
; bloque 0137
    mova #0x31FD9, r9
    mova #0xC8148, r13
    mov #0x0, r2
    mova r9, r13
    call #guarda
; bloque 0138
    mov #0, r15
    mov #0x6, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0139
    mov #0x6E78, r6
    mov #0xCD71, r4
    mov #0x1, r2
    movx.b r4, r6
    call #guarda
; bloque 013a
    mov #0x6, r2
    bitx.w zona+34, &zona+138
    call #guarda
; bloque 013b
    mova #0xAABC9, r7
    mova #0xD6F5B, r6
    mova #0x7FFFE, r8
    mov #0x2, r2
    rpt r8 { addcx.a r7, r6
    call #guarda
; bloque 013c
    mov #zona+128, r12
    mov #0x6, r2
    rrc.w -22(r12)
    call #guarda
; bloque 013d
    mov #zona+192, r14
    mov #0x2, r2
    swpb @r14
    call #guarda
; bloque 013e
    mova #0x122DF, r11
    mova #0x6631A, r7
    mov #0x0000, r5
    mov #0x4, r2
    rpt r5 { movx.a r11, r7
    call #guarda
; bloque 013f
    mova #0xFFFFF, r10
    mova #0xB53FC, r11
    mov #0x4000, r12
    mova #0x37317, r13
    mov #0x0, r2
    pushm.w #4, r13
    popm.w #4, r9
    call #guarda
; bloque 0140
    mov #0x0, r2
    push.b #0x80
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0141
    mov #zona+128, r12
    mov #0x2, r2
    bic.b &zona+133, -16(r12)
    call #guarda
; bloque 0142
    mov #0x27BA, r8
    mova #0xFFFFF, r12
    mova #zona+128, r8
    mov #0x0, r2
    mova -94(r8), r12
    call #guarda
; bloque 0143
    mova #0xA80A0, r4
    mov #0x0, r2
    subcx.b r4, r4
    call #guarda
; bloque 0144
    mov #0x0000, r15
    mova #0xF3460, r5
    mov #0x7, r2
    mova r15, &zona+128
    call #guarda
; bloque 0145
    mova #0x7A17A, r9
    mov #0x3, r2
    sub.b zona+71, r9
    call #guarda
; bloque 0146
    mov #zona+128, r7
    mov #0x7, r2
    bicx.a #-1, -114(r7)
    call #guarda
; bloque 0147
    mov #0x0000, r15
    mov #0x1, r2
    addc.b #-1, r15
    call #guarda
; bloque 0148
    mova #0x29A86, r15
    mov #0x0000, r14
    mov #0x4, r2
    rpt #3 { xorx.b r15, r14
    call #guarda
; bloque 0149
    mov #0x6, r2
    and.w #8, &zona+198
    call #guarda
; bloque 014a
    mova #0x7FFFF, r5
    mova #0xF1C73, r4
    mova #zona+128, r5
    mov #0x7, r2
    mova -24(r5), r4
    call #guarda
; bloque 014b
    mov #0x1, r2
    xorx.w &zona+24, zona+30
    call #guarda
; bloque 014c
    mov #0x0000, r9
    mov #0x4, r2
    swpb r9
    call #guarda
; bloque 014d
    mov #0x0000, r10
    mova #0x7FFFF, r15
    mova #0x80000, r5
    mov #0x5, r2
    rpt r5 { bisx.w r10, r15
    call #guarda
; bloque 014e
    mova #0xFFFFF, r11
    mova #0xFFFFF, r12
    mov #0x4, r2
    rpt #14 { bicx.a r11, r12
    call #guarda
; bloque 014f
    mov #zona+102, r10
    mov #0x4, r2
    pushx.b @r10
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0150
    mov #zona+128, r6
    mov #0x0, r2
    xor.b #0x0, -67(r6)
    call #guarda
; bloque 0151
    mova #0xEE85F, r10
    mov #0x7, r2
    pushm.a #1, r10
    popm.a #1, r8
    call #guarda
; bloque 0152
    mov #0x4, r2
    rrax.w &zona+206
    call #guarda
; bloque 0153
    mova #0x7FFFF, r11
    mov #0x2, r2
    bic.b #2, r11
    call #guarda
; bloque 0154
    mova #0xFFFFF, r6
    mova #0x3C7ED, r4
    mov #0x1, r2
    suba r6, r4
    call #guarda
; bloque 0155
    mova #0x7BAA4, r15
    mova #0xB75F7, r6
    mov #0x5, r2
    xor.w r6, r15
    call #guarda
; bloque 0156
    mova #0x9980D, r4
    mov #zona+6, r11
    mov #0x7, r2
    xorx.b @r11, r4
    call #guarda
; bloque 0157
    mov #0x3, r2
    rrax.w &zona+222
    call #guarda
; bloque 0158
    mova #0x214B1, r15
    mov #zona+128, r11
    mov #0x7, r2
    sub.w 30(r11), r15
    call #guarda
; bloque 0159
    mov #0x4, r2
    rrux.a &zona+28
    call #guarda
; bloque 015a
    mov #0, r15
    mov #0x6, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 015b
    mova #0x93F1C, r14
    mov #0x7, r2
    push.b r14
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 015c
    mov #zona+128, r13
    mov #0x1, r2
    bisx.b 20(r13), zona+101
    call #guarda
; bloque 015d
    mova #0x7FFFE, r10
    mov #0x0040, r4
    mov #0x4, r2
    mova r10, &zona+130
    call #guarda
; bloque 015e
    mova #0x4A9D9, r7
    mov #0x0, r2
    rlam.a #1, r7
    call #guarda
; bloque 015f
    mova #0xED5FF, r12
    mov #0x0000, r14
    mova #zona+128, r12
    mov #0x7, r2
    mova 72(r12), r14
    call #guarda
; bloque 0160
    mov #zona+14, r14
    mov #0x0, r2
    rrux.w @r14
    call #guarda
; bloque 0161
    mov #0xE7A4, r13
    mov #0x7, r2
    rrcm.w #4, r13
    call #guarda
; bloque 0162
    mova #0xF5D00, r10
    mov #zona+128, r15
    mov #0x1, r2
    bit.b 90(r15), r10
    call #guarda
; bloque 0163
    mov #0, r15
    mov #0x4, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0164
    mova #0x7FFFF, r10
    mov #0x5, r2
    subcx.b #0x80, r10
    call #guarda
; bloque 0165
    mova #0x7FFFF, r14
    mov #0x5, r2
    rpt #8 { rrux.w r14
    call #guarda
; bloque 0166
    mov #0x1, r2
    movx.w #0x2AFC, &zona+248
    call #guarda
; bloque 0167
    mov #zona+128, r8
    mov #0x2, r2
    subcx.b &zona+221, 86(r8)
    call #guarda
; bloque 0168
    mov #0x0010, r13
    mova #0xA6A55, r10
    mov #0x3, r2
    adda r13, r10
    call #guarda
; bloque 0169
    mov #0x0031, r4
    mov #0x5, r2
    dadd.b #0x6, r4
    call #guarda
; bloque 016a
    mova #0xAF52C, r5
    mova #0x7FFFF, r6
    mov #0x3, r2
    suba #0x1465A, r6
    call #guarda
; bloque 016b
    mova #0xAA3FB, r10
    mova #0xCD446, r5
    mov #0x7, r2
    mova &zona+50, r5
    call #guarda
; bloque 016c
    mov #0xC97E, r7
    mov #zona+128, r14
    mov #0x2, r2
    cmp.w -124(r14), r7
    call #guarda
; bloque 016d
    mov #zona+128, r8
    mov #0x5, r2
    bicx.b 88(r8), &zona+62
    call #guarda
; bloque 016e
    mova #0x2029F, r7
    mov #0x3, r2
    xor.b zona+94, r7
    call #guarda
; bloque 016f
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0170
    mov #0x1, r2
    rrax.b &zona+107
    call #guarda
; bloque 0171
    mov #0x0000, r6
    mova #0xFFFFF, r7
    mov #0x0000, r8
    mov #0x2, r2
    pushm.w #3, r8
    popm.w #3, r13
    call #guarda
; bloque 0172
    mov #zona+124, r13
    mov #0x1, r2
    addcx.w @r13, zona+118
    call #guarda
; bloque 0173
    mov #0x0000, r8
    mov #0x6, r2
    xor.b &zona+22, r8
    call #guarda
; bloque 0174
    mov #0x0000, r13
    mov #0x6, r2
    rram.a #2, r13
    call #guarda
; bloque 0175
    mova #0xFFFFF, r4
    mova #0x68BC9, r7
    mov #0x4, r2
    mova r4, &zona+144
    call #guarda
; bloque 0176
    mova #0xBE30D, r13
    mov #0x8000, r14
    mova #zona+202, r13
    mov #0x1, r2
    mova @r13+, r14
    call #guarda
; bloque 0177
    mova #0x80000, r9
    mov #0x5, r2
    addcx.b #-1, r9
    call #guarda
; bloque 0178
    mov #0x0002, r9
    mov #0x4, r2
    bicx.w r9, &zona+90
    call #guarda
; bloque 0179
    mov #0x0000, r10
    mova #0x8E59F, r11
    mov #0x2, r2
    mova #0x36BB3, r11
    call #guarda
; bloque 017a
    mova #0x2E606, r11
    mov #0x6, r2
    sub.b r11, &zona+235
    call #guarda
; bloque 017b
    mov #0, r15
    mov #0x4, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 017c
    mova #0x20BB1, r13
    mova #0x39607, r15
    mov #0xB159, r14
    mov #0x0, r2
    rpt r14 { bisx.w r13, r15
    call #guarda
; bloque 017d
    mova #0x7E55C, r15
    mov #zona+227, r11
    mov #0x1, r2
    bicx.b @r11, r15
    call #guarda
; bloque 017e
    mova #0x85ED3, r5
    mov #0x4, r2
    swpb r5
    call #guarda
; bloque 017f
    mov #zona+226, r10
    mov #0x5, r2
    addx.a @r10+, zona+186
    call #guarda
; bloque 0180
    mova #0x1BE51, r5
    mova #0x9C95E, r8
    mova #0xAC61D, r7
    mov #0x3, r2
    rpt r7 { andx.b r5, r8
    call #guarda
; bloque 0181
    mova #0xFFFFF, r5
    mov #zona+128, r7
    mov #0x3, r2
    sub.w 88(r7), r5
    call #guarda
; bloque 0182
    mova #0x80000, r5
    mova #0xF36F0, r11
    mov #0x5, r2
    mova #0x00000, r11
    call #guarda
; bloque 0183
    mova #0x480A5, r6
    mova #0x3800C, r12
    mov #0x2, r2
    sub.w r12, r6
    call #guarda
; bloque 0184
    mova #0xFFFFF, r5
    mov #0x0, r2
    rrum.w #1, r5
    call #guarda
; bloque 0185
    mova #0xC0AEE, r13
    mov #zona+204, r15
    mov #0x1, r2
    subcx.b @r15+, r13
    call #guarda
; bloque 0186
    mova #0xBFDB4, r8
    mov #0x3, r2
    rram.w #1, r8
    call #guarda
; bloque 0187
    mov #0x8000, r14
    mov #0x0000, r4
    mova #zona+170, r14
    mov #0x7, r2
    mova @r14+, r4
    call #guarda
; bloque 0188
    mov #0x0055, r9
    mov #0x0017, r8
    mov #0x6, r2
    dadd.b r8, r9
    call #guarda
; bloque 0189
    mov #zona+202, r15
    mov #0x2, r2
    cmpx.a @r15, &zona+78
    call #guarda
; bloque 018a
    mova #0x687E8, r13
    mov #0x6, r2
    rpt #4 { rrux.a r13
    call #guarda
; bloque 018b
    mov #0x0000, r11
    mov #0x0000, r12
    mova #0x7D072, r13
    mov #0x5, r2
    pushm.a #3, r13
    popm.a #3, r7
    call #guarda
; bloque 018c
    mova #0xF6B48, r4
    mova #0x20B1F, r11
    mov #0x6, r2
    adda #0x18D58, r11
    call #guarda
; bloque 018d
    mova #0x4A0E7, r6
    mov #0x6, r2
    pushm.w #1, r6
    popm.w #1, r10
    call #guarda
; bloque 018e
    mov #zona+128, r12
    mov #0x5, r2
    push.w -124(r12)
    pop r15
    call #guarda
; bloque 018f
    mov #0x0000, r8
    mov #0x4, r2
    rrcm.w #4, r8
    call #guarda
; bloque 0190
    mov #zona+128, r6
    mov #0x5, r2
    rra.b -91(r6)
    call #guarda
; bloque 0191
    mov #zona+226, r10
    mov #0x0, r2
    rrcx.w @r10
    call #guarda
; bloque 0192
    mova #0x7C917, r15
    mov #0x6, r2
    rrc.w r15
    call #guarda
; bloque 0193
    mov #0x7, r2
    bis.b &zona+94, zona+93
    call #guarda
; bloque 0194
    mov #zona+128, r13
    mov #zona+190, r15
    mov #0x0, r2
    bisx.b @r15+, 104(r13)
    call #guarda
; bloque 0195
    mova #0x586C6, r9
    mov #0x5, r2
    rram.a #4, r9
    call #guarda
; bloque 0196
    mov #zona+128, r6
    mov #0x5, r2
    bicx.b #1, -103(r6)
    call #guarda
; bloque 0197
    mov #0x0000, r14
    mova #0x3E036, r7
    mov #0x4, r2
    rpt #1 { addx.w r14, r7
    call #guarda
; bloque 0198
    mov #0x6, r2
    xor.b zona+83, zona+119
    call #guarda
; bloque 0199
    mova #0xFFFFF, r11
    mov #0x3, r2
    addc.w r11, zona+60
    call #guarda
; bloque 019a
    mova #0x87E78, r15
    mova #0xFFFFF, r12
    mova #0x80000, r4
    mov #0x4, r2
    rpt r4 { bicx.a r15, r12
    call #guarda
; bloque 019b
    mov #zona+226, r12
    mov #0x2, r2
    subc.w @r12+, zona+210
    call #guarda
; bloque 019c
    mov #0x0400, r5
    mova #0x1BE7C, r4
    mov #0x6, r2
    mov.b r4, r5
    call #guarda
; bloque 019d
    mov #zona+4, r6
    mov #0x2, r2
    swpbx @r6
    call #guarda
; bloque 019e
    mova #0x40000, r13
    mov #zona+128, r15
    mov #0x3, r2
    and.w -106(r15), r13
    call #guarda
; bloque 019f
    mov #zona+128, r15
    mov #0x7, r2
    mov.b #2, -118(r15)
    call #guarda
; bloque 01a0
    mov #0, r15
    mov #0x0, r2
    call #subw
    call #guarda
; bloque 01a1
    mov #0x7, r2
    rrax.b zona+209
    call #guarda
; bloque 01a2
    mov #zona+128, r12
    mov #0x7, r2
    and.w &zona+178, -110(r12)
    call #guarda
; bloque 01a3
    mova #0x38D5E, r10
    mov #0x0000, r11
    mov #0x1, r2
    cmpx.b r11, r10
    call #guarda
; bloque 01a4
    mov #zona+128, r8
    mov #0x5, r2
    rrcx.a -8(r8)
    call #guarda
; bloque 01a5
    mova #0x388B7, r4
    mova #0x254DF, r5
    mov #0x0000, r13
    mov #0x2, r2
    rpt r13 { addx.b r4, r5
    call #guarda
; bloque 01a6
    mov #0x9083, r8
    mov #0x9044, r4
    mov #0x7, r2
    daddx.w r4, r8
    call #guarda
; bloque 01a7
    mov #0x3, r2
    swpbx &zona+196
    call #guarda
; bloque 01a8
    mova #0x7FFFF, r7
    mova #0xFFFFF, r14
    mov #0x0, r2
    adda r7, r14
    call #guarda
; bloque 01a9
    mov #zona+8, r5
    mov #0x6, r2
    xorx.b @r5+, &zona+178
    call #guarda
; bloque 01aa
    mova #0x719F1, r11
    mova #0x4F011, r5
    mov #0x1, r2
    mova r11, &zona+56
    call #guarda
; bloque 01ab
    mov #0x1, r2
    xor.b &zona+127, zona+53
    call #guarda
; bloque 01ac
    mov #0, r15
    mov #0x7, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 01ad
    mov #zona+128, r10
    mov #zona+115, r6
    mov #0x6, r2
    subc.b @r6+, -37(r10)
    call #guarda
; bloque 01ae
    mov #zona+128, r14
    mov #0x6, r2
    rrax.w -76(r14)
    call #guarda
; bloque 01af
    mova #0x20000, r4
    mova #0x13A6C, r13
    mova #0x6365A, r14
    mov #0x7, r2
    rpt r14 { bisx.b r4, r13
    call #guarda
; bloque 01b0
    mova #0x7FFFE, r9
    mova #0x306A2, r12
    mova #zona+128, r12
    mov #0x1, r2
    mova r9, 6(r12)
    call #guarda
; bloque 01b1
    mova #0x6567D, r14
    mov #0x0800, r8
    mov #0x1, r2
    addx.b r8, r14
    call #guarda
; bloque 01b2
    mova #0xC83A7, r4
    mova #0xC313D, r10
    mov #0x1, r2
    rpt #2 { bisx.w r4, r10
    call #guarda
; bloque 01b3
    mov #zona+120, r9
    mov #0x1, r2
    bit.w @r9, zona+0
    call #guarda
; bloque 01b4
    mova #0xA2A09, r6
    mov #zona+232, r12
    mov #0x6, r2
    addcx.b @r12+, r6
    call #guarda
; bloque 01b5
    mov #zona+241, r15
    mov #0x3, r2
    rrux.b @r15
    call #guarda
; bloque 01b6
    mov #0x1, r2
    pushx.b zona+26
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 01b7
    mov #0x5, r2
    subx.a &zona+224, zona+218
    call #guarda
; bloque 01b8
    mov #zona+128, r9
    mov #0x5, r2
    bit.b zona+78, -23(r9)
    call #guarda
; bloque 01b9
    mov #0x6, r2
    rrcx.w &zona+60
    call #guarda
; bloque 01ba
    mova #0xFFFFF, r7
    mova #0xFFFFF, r8
    mov #0x7, r2
    cmpa r7, r8
    call #guarda
; bloque 01bb
    mov #0x4000, r10
    mova #0x49B3E, r12
    mov #0x1, r2
    addcx.a r12, r10
    call #guarda
; bloque 01bc
    mov #0x6, r2
    bicx.a zona+132, &zona+244
    call #guarda
; bloque 01bd
    mov #0x0000, r12
    mova #0x43E23, r8
    mov #0x1, r2
    adda r12, r8
    call #guarda
; bloque 01be
    mov #0x0002, r9
    mov #0x6, r2
    rram.a #3, r9
    call #guarda
; bloque 01bf
    mov #zona+128, r9
    mov #0x1, r2
    movx.b #0, -25(r9)
    call #guarda
; bloque 01c0
    mova #0xA25B2, r9
    mov #zona+128, r8
    mov #0x4, r2
    andx.b 98(r8), r9
    call #guarda
; bloque 01c1
    mov #0x2, r2
    rrax.b &zona+7
    call #guarda
    mov &sp_guardado, r1
    popm.a #7, r10
fin:
    ret
guarda:
    pushx.a r15
    mova &resptr, r15
    mov r2, 48(r15)
    movx.a r4, 0(r15)
    movx.a r5, 4(r15)
    movx.a r6, 8(r15)
    movx.a r7, 12(r15)
    movx.a r8, 16(r15)
    movx.a r9, 20(r15)
    movx.a r10, 24(r15)
    movx.a r11, 28(r15)
    movx.a r12, 32(r15)
    movx.a r13, 36(r15)
    movx.a r14, 40(r15)
    popx.a 44(r15)
    adda #52, r15
    mova r15, &resptr
    ret
sub:
    mov #0x1234, r15
    reta
subw:
    mov #0x1234, r15
    ret
    .data
    .balign 4
    .global zona
zona:
    .byte 0xA5, 0x4D, 0xCA, 0x18, 0x25, 0x30, 0xBB, 0x1D, 0x6D, 0x13, 0x2C, 0xDE, 0xD6, 0x23, 0x7B, 0x2E
    .byte 0xD9, 0x1E, 0x3F, 0x72, 0x1F, 0xCB, 0x19, 0x71, 0x17, 0x44, 0x94, 0xD6, 0x49, 0x3C, 0x9D, 0x5C
    .byte 0x34, 0x60, 0xBE, 0x31, 0x20, 0x1E, 0x69, 0xFE, 0xDA, 0xA0, 0xEE, 0xE8, 0xB9, 0x99, 0x7F, 0x5C
    .byte 0x7C, 0x29, 0x99, 0xFD, 0xAF, 0xE5, 0x93, 0x25, 0x3C, 0xD6, 0x54, 0xAF, 0x4D, 0xFA, 0xD7, 0x14
    .byte 0x27, 0xA0, 0xAE, 0xB3, 0xFE, 0xE9, 0x23, 0x2F, 0x8A, 0xF2, 0x21, 0x1F, 0x9E, 0xE4, 0x91, 0xC5
    .byte 0xB1, 0x0B, 0xEC, 0xB5, 0x56, 0x3B, 0xFC, 0x1E, 0x6F, 0x93, 0x42, 0x7E, 0xCB, 0xC8, 0xFE, 0x29
    .byte 0x55, 0xE5, 0xCD, 0x8E, 0x46, 0xDC, 0x8E, 0xD4, 0xB7, 0xC2, 0x76, 0x4D, 0x2A, 0x5A, 0x4D, 0x76
    .byte 0x77, 0x06, 0xF8, 0x5D, 0x86, 0x90, 0x02, 0x4A, 0xD6, 0xBD, 0xA3, 0x40, 0x1B, 0xE9, 0xC8, 0xCB
    .byte 0xCC, 0xC9, 0x35, 0xF6, 0xCD, 0x1F, 0x61, 0x22, 0x6A, 0xE1, 0x53, 0x38, 0xAE, 0x1A, 0x34, 0x00
    .byte 0x4D, 0x33, 0xBA, 0x0D, 0x24, 0x6A, 0xC0, 0x4C, 0x81, 0xB1, 0xBA, 0xF2, 0x3E, 0x3B, 0xF9, 0xEE
    .byte 0xF5, 0xF7, 0x9F, 0x2B, 0x49, 0x34, 0xAF, 0x87, 0xF5, 0x52, 0x0B, 0x69, 0xB9, 0x4B, 0x0D, 0x98
    .byte 0x2E, 0x85, 0xBB, 0x55, 0xB6, 0x72, 0xA8, 0x72, 0x63, 0x7A, 0xCD, 0x74, 0x66, 0xFC, 0xB6, 0x0E
    .byte 0x0E, 0x8F, 0xF1, 0x84, 0x63, 0xB0, 0xE4, 0xB2, 0xBA, 0x29, 0x70, 0x34, 0x74, 0xF0, 0x64, 0xAC
    .byte 0x68, 0xF7, 0x00, 0xF5, 0xB0, 0x2B, 0x3D, 0xC6, 0x66, 0xF4, 0x5B, 0xDE, 0xAA, 0x2C, 0xCA, 0xED
    .byte 0xCD, 0x2B, 0x51, 0x57, 0x41, 0x0E, 0x4D, 0xEE, 0x4A, 0xF2, 0xB3, 0x4F, 0x43, 0x0A, 0x07, 0x34
    .byte 0x47, 0xDE, 0x63, 0x6C, 0x0E, 0x80, 0x6C, 0x95, 0x7B, 0xA6, 0x84, 0xD6, 0x43, 0x1F, 0xB5, 0xEA
    .data
    .balign 4
    .global zona_ptr
zona_ptr:
    .long zona
tabla:
    .word subw
    .word 0
tabla_a:
    .long sub
    .long 0
    .balign 4
    .global res
res:
    .space 23400
    .global resptr
resptr:
    .long 0
sp_guardado:
    .long 0
    .global n_bloques
n_bloques:
    .word 450
