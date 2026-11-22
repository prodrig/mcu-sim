    .text
    .global prueba
    .global fin
prueba:
    pushm.a #7, r10
    mov r1, &sp_guardado
    movx.a #res, &resptr
; bloque 0000
    mova #0x7FFFF, r4
    mov #0x6, r2
    pushm.w #1, r4
    popm.w #1, r13
    call #guarda
; bloque 0001
    mova #0xFF196, r5
    mov #0x1, r2
    rra.w r5
    call #guarda
; bloque 0002
    mov #0x0800, r13
    mov #0x2, r2
    movx.a #8, r13
    call #guarda
; bloque 0003
    mov #0, r15
    mova #tabla_a, r13
    mov #0x3, r2
    calla @r13
    call #guarda
; bloque 0004
    mov #0, r15
    mov #0x2, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0005
    mov #0x3, r2
    rrux.a zona+28
    call #guarda
; bloque 0006
    mova #0x7FFFF, r9
    mova #0xE8D22, r5
    mova #0xFFFFF, r10
    mov #0x2, r2
    rpt r10 { xorx.b r9, r5
    call #guarda
; bloque 0007
    mov #zona+128, r11
    mov #0x6, r2
    swpb -86(r11)
    call #guarda
; bloque 0008
    mova #0xB1399, r11
    mov #0x5, r2
    subc.w r11, &zona+240
    call #guarda
; bloque 0009
    mov #zona+128, r5
    mov #0x0, r2
    subx.a zona+26, -74(r5)
    call #guarda
; bloque 000a
    mova #0xAF805, r4
    mov #0x3, r2
    rrax.w r4
    call #guarda
; bloque 000b
    mov #0x2000, r12
    mov #0x1, r2
    rrcm.a #1, r12
    call #guarda
; bloque 000c
    mova #0xFFFFF, r14
    mov #0x4, r2
    rrcm.a #2, r14
    call #guarda
; bloque 000d
    mova #0xFFFFF, r12
    mova #0xF1032, r15
    mov #0x0000, r8
    mov #0x5, r2
    rpt r8 { addcx.w r12, r15
    call #guarda
; bloque 000e
    mova #0xD8588, r6
    mov #0x6, r2
    rram.a #2, r6
    call #guarda
; bloque 000f
    mova #0xFEE07, r4
    mov #0x6, r2
    rpt #15 { swpbx r4
    call #guarda
; bloque 0010
    mov #zona+106, r14
    mov #0x5, r2
    subx.w @r14, zona+10
    call #guarda
; bloque 0011
    mov #zona+28, r13
    mov #0x3, r2
    addx.w @r13, &zona+94
    call #guarda
; bloque 0012
    mova #0x15DA1, r13
    mov #0x3, r2
    rrcx.a r13
    call #guarda
; bloque 0013
    mov #0, r15
    mov #0x1, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0014
    mova #0xFFFFF, r15
    mova #0xFFFFF, r4
    mov #0x4, r2
    add.w r4, r15
    call #guarda
; bloque 0015
    mova #0x346F2, r4
    mova #0x80000, r5
    mova #0x80000, r6
    mova #0x69849, r7
    mova #0xA22D1, r8
    mov #0x2, r2
    pushm.w #5, r8
    popm.w #5, r8
    call #guarda
; bloque 0016
    mova #0x9FA84, r14
    mova #0x4225E, r7
    mov #0x6, r2
    rpt #10 { andx.b r14, r7
    call #guarda
; bloque 0017
    mova #0xFFFFF, r14
    mova #0x7FFFF, r12
    mov #0x5, r2
    adda r14, r12
    call #guarda
; bloque 0018
    mov #0x6597, r6
    mov #0x6, r2
    daddx.w #0x896, r6
    call #guarda
; bloque 0019
    mov #0x0040, r10
    mov #0x3, r2
    dadd.b #0, r10
    call #guarda
; bloque 001a
    mov #0, r15
    mova #sub, r6
    mov #0x1, r2
    calla r6
    call #guarda
; bloque 001b
    mova #0x4A88C, r5
    mov #0x4, r2
    rrum.a #1, r5
    call #guarda
; bloque 001c
    mova #0xB1C2C, r7
    mova #0x9FD61, r12
    mov #0x1, r2
    adda #0xDC513, r12
    call #guarda
; bloque 001d
    mova #0x47823, r5
    mov #0x4, r2
    andx.b &zona+170, r5
    call #guarda
; bloque 001e
    mov #0x0000, r14
    mov #0x5, r2
    bisx.a r14, zona+244
    call #guarda
; bloque 001f
    mov #0, r15
    mov #0x2, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0020
    mova #0xFFFFF, r14
    mov #0x0002, r6
    mova #zona+188, r14
    mov #0x2, r2
    mova @r14, r6
    call #guarda
; bloque 0021
    mov #0x0005, r6
    mov #0x0098, r14
    mov #0x4, r2
    dadd.b r14, r6
    call #guarda
; bloque 0022
    mov #0x0008, r6
    mov #0x3, r2
    pushx.b r6
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0023
    mov #0x0028, r12
    mov #0x0089, r7
    mova #0x884C0, r8
    mov #0x2, r2
    rpt r8 { daddx.b r12, r7
    call #guarda
; bloque 0024
    mova #0x20DDF, r12
    mov #0x1, r2
    rram.a #1, r12
    call #guarda
; bloque 0025
    mova #0xEB433, r9
    mova #0x20000, r14
    mova #zona+140, r9
    mov #0x2, r2
    mova @r9+, r14
    call #guarda
; bloque 0026
    mov #0x0000, r15
    mova #0x80000, r6
    mova #0x5378F, r12
    mov #0x2, r2
    rpt r12 { addcx.a r15, r6
    call #guarda
; bloque 0027
    mov #zona+128, r5
    mov #0x5, r2
    swpbx 74(r5)
    call #guarda
; bloque 0028
    mov #0xC23B, r13
    mov #0x0000, r14
    mova #zona+236, r13
    mov #0x4, r2
    mova @r13+, r14
    call #guarda
; bloque 0029
    mova #0xABCC6, r14
    mova #0x499E9, r15
    mov #0x1, r2
    adda r14, r15
    call #guarda
; bloque 002a
    mov #0x0094, r14
    mov #0x0065, r6
    mova #0x99B4D, r4
    mov #0x4, r2
    rpt r4 { daddx.b r14, r6
    call #guarda
; bloque 002b
    mova #0x2865F, r10
    mov #0x0008, r14
    mov #0x2, r2
    mova r10, r14
    call #guarda
; bloque 002c
    mov #zona+241, r7
    mov #0x0, r2
    rra.b @r7
    call #guarda
; bloque 002d
    mova #0xD1071, r15
    mova #0x7FFFE, r9
    mov #0x6, r2
    rpt #14 { subx.w r15, r9
    call #guarda
; bloque 002e
    mova #0xFFFFF, r6
    mov #zona+110, r9
    mov #0x6, r2
    andx.a @r9, r6
    call #guarda
; bloque 002f
    mov #0, r15
    mova #tabla_a, r8
    mov #0x4, r2
    calla @r8
    call #guarda
; bloque 0030
    mova #0x907F0, r7
    mova #0x12E80, r13
    mov #0x0, r2
    adda #0x5366F, r13
    call #guarda
; bloque 0031
    mov #0x4, r2
    rrcx.a &zona+92
    call #guarda
; bloque 0032
    mov #zona+128, r13
    mova #0xFFFFF, r7
    mov #0x0, r2
    addc.w r7, 96(r13)
    call #guarda
; bloque 0033
    mova #0xDE824, r11
    mova #0x66BF3, r12
    mova #zona+128, r11
    mov #0x0, r2
    mova -100(r11), r12
    call #guarda
; bloque 0034
    mov #0x0080, r6
    mov #0x3, r2
    rrcm.w #2, r6
    call #guarda
; bloque 0035
    mova #0x24742, r12
    mov #0x3, r2
    rrax.a r12
    call #guarda
; bloque 0036
    mov #0x0020, r4
    mov #0x7, r2
    dadd.b #0x10, r4
    call #guarda
; bloque 0037
    mov #zona+128, r11
    mov #0x0, r2
    add.b &zona+130, -1(r11)
    call #guarda
; bloque 0038
    mov #0x6, r2
    rrax.b zona+121
    call #guarda
; bloque 0039
    mov #0x0, r2
    bis.b &zona+170, &zona+216
    call #guarda
; bloque 003a
    mov #0x5, r2
    cmpx.w #2, &zona+190
    call #guarda
; bloque 003b
    mov #0, r15
    mov #0x3, r2
    call #subw
    call #guarda
; bloque 003c
    mov #0x3618, r7
    mov #0x7907, r5
    mov #0x7, r2
    dadd.w r5, r7
    call #guarda
; bloque 003d
    mova #0x5A0CF, r4
    mova #0xC4A3D, r9
    mov #0x0, r2
    mova &zona+14, r9
    call #guarda
; bloque 003e
    mova #0x503D6, r6
    mova #0xBEBFE, r15
    mova #0xFFFFF, r12
    mov #0x6, r2
    rpt r12 { subcx.w r6, r15
    call #guarda
; bloque 003f
    mov #0xF4B4, r11
    mov #0x0, r2
    rra.b r11
    call #guarda
; bloque 0040
    mov #0x1, r2
    bic.b &zona+25, zona+19
    call #guarda
; bloque 0041
    mova #0x98B3D, r7
    mov #0x5, r2
    pushm.w #1, r7
    popm.w #1, r9
    call #guarda
; bloque 0042
    mov #zona+128, r5
    mov #0x3, r2
    pushx.b -77(r5)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0043
    mov #0x0000, r5
    mov #0x0000, r7
    mov #0x6, r2
    mova r5, &zona+96
    call #guarda
; bloque 0044
    mova #0xFFFFF, r9
    mov #0x4000, r15
    mov #0x4, r2
    mova &zona+38, r15
    call #guarda
; bloque 0045
    mova #0xDD2DC, r7
    mova #0x8902C, r6
    mov #0x1, r2
    suba r7, r6
    call #guarda
; bloque 0046
    mova #0x9DA4F, r10
    mova #0x5BABD, r13
    mov #0x5, r2
    adda #0x00000, r13
    call #guarda
; bloque 0047
    mova #0x5E430, r4
    mov #0x0000, r8
    mova #zona+110, r4
    mov #0x7, r2
    mova @r4, r8
    call #guarda
; bloque 0048
    mov #zona+128, r9
    mov #zona+128, r7
    mov #0x6, r2
    xor.b 66(r7), 51(r9)
    call #guarda
; bloque 0049
    mova #0x80000, r15
    mov #zona+128, r10
    mov #0x2, r2
    subcx.b -30(r10), r15
    call #guarda
; bloque 004a
    mov #0x0080, r4
    mova #0x4C049, r11
    mova #0x80000, r15
    mov #0x6, r2
    rpt r15 { subx.b r4, r11
    call #guarda
; bloque 004b
    mov #0x4, r2
    subc.b zona+1, zona+188
    call #guarda
; bloque 004c
    mova #0xB511F, r10
    mova #0x16C75, r6
    mov #0x0001, r5
    mov #0x0, r2
    rpt r5 { addcx.a r10, r6
    call #guarda
; bloque 004d
    mova #0xFFFFF, r13
    mova #0xA6BAD, r9
    mov #0x2, r2
    mova &zona+250, r9
    call #guarda
; bloque 004e
    mova #0x80000, r7
    mov #0x1, r2
    rlam.a #2, r7
    call #guarda
; bloque 004f
    mova #0xDAA95, r12
    mov #0x6, r2
    add.b &zona+75, r12
    call #guarda
; bloque 0050
    mov #0x0010, r11
    mov #0x7, r2
    mov.b r11, zona+130
    call #guarda
; bloque 0051
    mov #0x8000, r11
    mova #0xBF1D5, r14
    mov #0x0, r2
    bit.w r14, r11
    call #guarda
; bloque 0052
    mova #0x7FFFE, r12
    mov #0x1, r2
    rrcm.a #4, r12
    call #guarda
; bloque 0053
    mova #0x4AE02, r13
    mov #0x7, r2
    add.b #-1, r13
    call #guarda
; bloque 0054
    mova #0x92A13, r15
    mov #0x5, r2
    movx.w #8, r15
    call #guarda
; bloque 0055
    mov #zona+66, r10
    mov #0x7, r2
    add.w @r10+, &zona+218
    call #guarda
; bloque 0056
    mov #zona+128, r5
    mov #0x1, r2
    add.b #0x7F, -67(r5)
    call #guarda
; bloque 0057
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0058
    mov #zona+230, r8
    mov #0x4, r2
    addcx.w @r8+, zona+128
    call #guarda
; bloque 0059
    mov #0x8372, r4
    mov #0x5, r2
    daddx.w #0x897, r4
    call #guarda
; bloque 005a
    mov #zona+128, r14
    mov #0x5, r2
    sub.b zona+147, 1(r14)
    call #guarda
; bloque 005b
    mova #0x80000, r12
    mov #0x7, r2
    rlam.w #1, r12
    call #guarda
; bloque 005c
    mova #0x8B976, r12
    mova #0x7FFFE, r11
    mova #zona+240, r12
    mov #0x5, r2
    mova @r12+, r11
    call #guarda
; bloque 005d
    mov #0x9598, r9
    mov #0x6638, r12
    mov #0x0, r2
    daddx.w r12, r9
    call #guarda
; bloque 005e
    mov #zona+128, r11
    mova #0xFFFFF, r7
    mov #0x7, r2
    addc.w r7, 118(r11)
    call #guarda
; bloque 005f
    mov #zona+128, r11
    mov #0x73AC, r4
    mov #0x0, r2
    bicx.a r4, -102(r11)
    call #guarda
; bloque 0060
    mov #0x0000, r13
    mov #0x2000, r8
    mova #zona+128, r13
    mov #0x0, r2
    mova 34(r13), r8
    call #guarda
; bloque 0061
    mov #zona+130, r9
    mov #0x2, r2
    addcx.w @r9+, &zona+40
    call #guarda
; bloque 0062
    mov #0x3, r2
    cmpx.b #0x80, &zona+34
    call #guarda
; bloque 0063
    mova #0xC210B, r15
    mov #0x0, r2
    rlam.w #3, r15
    call #guarda
; bloque 0064
    mova #0x10000, r15
    mov #0x5, r2
    rlam.w #4, r15
    call #guarda
; bloque 0065
    mov #zona+164, r13
    mov #0x3, r2
    subcx.w @r13+, &zona+220
    call #guarda
; bloque 0066
    mova #0x2C24B, r9
    mova #0x53F30, r8
    mova #zona+0, r9
    mov #0x3, r2
    mova @r9+, r8
    call #guarda
; bloque 0067
    mova #0x5ADC5, r4
    mov #0x2, r2
    rram.w #2, r4
    call #guarda
; bloque 0068
    mov #0x6, r2
    push.w #0x4000
    pop r15
    call #guarda
; bloque 0069
    mova #0x3B08A, r5
    mova #0xFFFFF, r6
    mova #0x54BDE, r7
    mov #0x0, r2
    pushm.w #3, r7
    popm.w #3, r15
    call #guarda
; bloque 006a
    mov #0, r15
    mov #0x0, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 006b
    mov #0xD4D1, r5
    mov #0x0, r2
    movx.b &zona+32, r5
    call #guarda
; bloque 006c
    mov #0, r15
    mov #0x5, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 006d
    mova #0xFFFFF, r4
    mov #0x4, r2
    rrc.w r4
    call #guarda
; bloque 006e
    mov #zona+217, r14
    mov #0x0, r2
    cmp.b @r14, &zona+165
    call #guarda
; bloque 006f
    mov #0x6, r2
    movx.b #4, &zona+70
    call #guarda
; bloque 0070
    mov #0x7, r2
    bis.b &zona+204, &zona+125
    call #guarda
; bloque 0071
    mov #0, r15
    mova #tabla_a-4, r6
    mov #0x1, r2
    calla 4(r6)
    call #guarda
; bloque 0072
    mova #0xD7A1E, r10
    mov #0x7, r2
    rrum.w #1, r10
    call #guarda
; bloque 0073
    mova #0xF0274, r13
    mov #0x6, r2
    rrcm.a #3, r13
    call #guarda
; bloque 0074
    mov #0x4, r2
    swpbx zona+138
    call #guarda
; bloque 0075
    mov #0x5, r2
    movx.a #0xF36FF, zona+4
    call #guarda
; bloque 0076
    mova #0xFFFFF, r4
    mov #0x0, r2
    rrc.w r4
    call #guarda
; bloque 0077
    mov #0x0098, r8
    mov #0x0096, r11
    mov #0x6, r2
    rpt #12 { daddx.b r8, r11
    call #guarda
; bloque 0078
    mov #zona+32, r15
    mov #0x7, r2
    push.w @r15
    pop r15
    call #guarda
; bloque 0079
    mov #0x3, r2
    bicx.w zona+174, &zona+110
    call #guarda
; bloque 007a
    mov #0x0400, r15
    mov #0x2, r2
    rpt #15 { rrcx.w r15
    call #guarda
; bloque 007b
    mova #0xB2CBB, r13
    mov #0x3, r2
    rrux.b r13
    call #guarda
; bloque 007c
    mova #0xECD1B, r11
    mova #0xC346C, r9
    mov #0x3, r2
    adda #0x80000, r9
    call #guarda
; bloque 007d
    mova #0x4601C, r13
    mov #0x3, r2
    and.b r13, zona+166
    call #guarda
; bloque 007e
    mov #zona+84, r11
    mov #0x4, r2
    xor.w @r11, &zona+158
    call #guarda
; bloque 007f
    mov #0x0200, r4
    mova #0x8A9B3, r10
    mov #0x1, r2
    mova r4, r10
    call #guarda
; bloque 0080
    mova #0x21055, r6
    mov #0x6, r2
    subcx.w &zona+14, r6
    call #guarda
; bloque 0081
    mov #zona+128, r12
    mov #0x4, r2
    rra.w -40(r12)
    call #guarda
; bloque 0082
    mova #0xCAE17, r15
    mova #0x788F1, r11
    mov #0x1, r2
    adda r15, r11
    call #guarda
; bloque 0083
    mov #0x0000, r8
    mov #0x7, r2
    rlam.a #1, r8
    call #guarda
; bloque 0084
    mova #0x85E7A, r13
    mov #0x1, r2
    bis.b r13, &zona+24
    call #guarda
; bloque 0085
    mov #0x0200, r13
    mov #0x0000, r8
    mov #0x5, r2
    mova r13, r8
    call #guarda
; bloque 0086
    mov #0x2, r2
    subx.w #0, zona+130
    call #guarda
; bloque 0087
    mova #0xA9EAD, r13
    mov #zona+24, r6
    mov #0x3, r2
    cmpx.w @r6+, r13
    call #guarda
; bloque 0088
    mova #0x10000, r4
    mova #0xEB8A6, r12
    mov #0x2, r2
    rpt #4 { addcx.b r4, r12
    call #guarda
; bloque 0089
    mov #0x3, r2
    bis.w zona+226, zona+44
    call #guarda
; bloque 008a
    mov #0x0000, r13
    mov #0x3, r2
    pushm.w #1, r13
    popm.w #1, r7
    call #guarda
; bloque 008b
    mova #0x118EF, r11
    mov #0x1, r2
    cmp.w &zona+52, r11
    call #guarda
; bloque 008c
    mov #0x0020, r5
    mova #0x75845, r13
    mov #0x4, r2
    adda #0x7FFFF, r13
    call #guarda
; bloque 008d
    mova #0x1A985, r11
    mova #0xE8EC0, r12
    mov #0x1, r2
    adda r11, r12
    call #guarda
; bloque 008e
    mov #0x5551, r12
    mov #0x9561, r10
    mov #0x6, r2
    daddx.w r10, r12
    call #guarda
; bloque 008f
    mova #0x2F5EF, r15
    mova #0xFFFFF, r12
    mov #0x6, r2
    mova &zona+208, r12
    call #guarda
; bloque 0090
    mov #0x0000, r14
    mova #0xB3CD5, r9
    mov #0x1, r2
    bic.w r9, r14
    call #guarda
; bloque 0091
    mova #0x1A9B0, r6
    mov #zona+136, r14
    mov #0x5, r2
    bisx.a @r14, r6
    call #guarda
; bloque 0092
    mov #0, r15
    mov #0x6, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0093
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0094
    mov #0x3, r2
    swpbx zona+234
    call #guarda
; bloque 0095
    mov #0x0800, r8
    mov #0x413E, r10
    mov #0x4, r2
    rpt #1 { xorx.w r8, r10
    call #guarda
; bloque 0096
    mov #0x5D7F, r9
    mov #0x6, r2
    rrux.w r9
    call #guarda
; bloque 0097
    mov #0x0022, r10
    mov #0x7, r2
    dadd.b #0x90, r10
    call #guarda
; bloque 0098
    mov #0, r15
    mov #0x2, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0099
    mov #0, r15
    mov #0x4, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 009a
    mova #0x6831A, r10
    mov #0x0, r2
    rrax.a r10
    call #guarda
; bloque 009b
    mova #0x1D378, r7
    mova #0x246E6, r5
    mov #0x0, r2
    subc.w r5, r7
    call #guarda
; bloque 009c
    mov #0x6, r2
    sub.w #0x3796, zona+222
    call #guarda
; bloque 009d
    mov #0, r15
    mov #0x2, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 009e
    mova #0xB451D, r4
    mova #0xBE72C, r14
    mov #0x7, r2
    rpt #15 { addx.b r4, r14
    call #guarda
; bloque 009f
    mov #0x0200, r6
    mov #0x7, r2
    pushm.w #1, r6
    popm.w #1, r5
    call #guarda
; bloque 00a0
    mova #0xEB8D4, r5
    mova #0x74A60, r13
    mov #0x7, r2
    cmpa r5, r13
    call #guarda
; bloque 00a1
    mov #0x02A8, r14
    mov #0x2, r2
    pushm.w #1, r14
    popm.w #1, r14
    call #guarda
; bloque 00a2
    mov #0x0000, r11
    mov #0x0020, r14
    mova #0xDA4A8, r9
    mov #0x5, r2
    rpt r9 { subcx.a r11, r14
    call #guarda
; bloque 00a3
    mova #0x8ECE8, r5
    mova #0x40000, r4
    mov #0x3, r2
    adda r5, r4
    call #guarda
; bloque 00a4
    mov #0x0070, r13
    mov #0x0094, r10
    mov #0x7, r2
    dadd.b r10, r13
    call #guarda
; bloque 00a5
    mov #zona+128, r12
    mov #zona+172, r6
    mov #0x6, r2
    addcx.a @r6+, 12(r12)
    call #guarda
; bloque 00a6
    mov #zona+128, r9
    mov #zona+128, r11
    mov #0x1, r2
    cmp.b -128(r11), 23(r9)
    call #guarda
; bloque 00a7
    mov #zona+148, r8
    mov #0x4, r2
    swpbx @r8
    call #guarda
; bloque 00a8
    mov #zona+128, r15
    mov #0x4, r2
    pushx.b 120(r15)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00a9
    mova #0x42B96, r15
    mov #zona+62, r14
    mov #0x5, r2
    mov.w @r14, r15
    call #guarda
; bloque 00aa
    mov #zona+84, r8
    mov #0x5, r2
    rrux.b @r8
    call #guarda
; bloque 00ab
    mov #zona+128, r14
    mov #0x3, r2
    cmpx.w -82(r14), &zona+40
    call #guarda
; bloque 00ac
    mov #0xC187, r8
    mov #0x0800, r9
    mov #0x7, r2
    cmpa r8, r9
    call #guarda
; bloque 00ad
    mova #0xFFFFF, r4
    mov #0x4, r2
    rrum.w #4, r4
    call #guarda
; bloque 00ae
    mova #0x7FFFE, r4
    mov #0x0010, r6
    mova #zona+128, r4
    mov #0x4, r2
    mova -124(r4), r6
    call #guarda
; bloque 00af
    mova #0xFFFFF, r12
    mova #0xFFFFF, r14
    mova #zona+128, r12
    mov #0x7, r2
    mova 82(r12), r14
    call #guarda
; bloque 00b0
    mova #0xE301C, r14
    mov #zona+128, r12
    mov #0x7, r2
    addcx.b 74(r12), r14
    call #guarda
; bloque 00b1
    mova #0xE4D64, r14
    mova #0x80000, r8
    mov #0x0, r2
    bitx.a r8, r14
    call #guarda
; bloque 00b2
    mova #0xC3183, r10
    mova #0xDAC15, r11
    mova #0xA1E25, r12
    mov #0x820D, r13
    mov #0x4, r2
    pushm.a #4, r13
    popm.a #4, r15
    call #guarda
; bloque 00b3
    mov #zona+128, r15
    mov #0x7, r2
    bitx.b &zona+43, -55(r15)
    call #guarda
; bloque 00b4
    mova #0x2A843, r10
    mov #0x2, r2
    bisx.w #4, r10
    call #guarda
; bloque 00b5
    mov #0, r15
    mov #0x0, r2
    call #subw
    call #guarda
; bloque 00b6
    mov #0x0400, r6
    mova #0xD1DAF, r14
    mova #zona+234, r6
    mov #0x5, r2
    mova @r6, r14
    call #guarda
; bloque 00b7
    mova #0xFFFFF, r4
    mova #0xFFFFF, r12
    mov #0x2, r2
    bit.w r12, r4
    call #guarda
; bloque 00b8
    mova #0x10000, r5
    mova #0x7FFFE, r8
    mova #0xA5A12, r13
    mov #0x2, r2
    rpt r13 { addcx.w r5, r8
    call #guarda
; bloque 00b9
    mova #0x64412, r15
    mova #0xCEF05, r14
    mova #0x1BAAE, r12
    mov #0x4, r2
    rpt r12 { addcx.w r15, r14
    call #guarda
; bloque 00ba
    mov #0x3, r2
    bitx.a #0x37004, &zona+112
    call #guarda
; bloque 00bb
    mova #0x7FFFE, r11
    mov #0x3, r2
    addx.a r11, &zona+54
    call #guarda
; bloque 00bc
    mov #0x0000, r7
    mova #0xECC99, r6
    mov #0x3, r2
    mova r7, &zona+8
    call #guarda
; bloque 00bd
    mova #0x3E233, r14
    mov #0x2, r2
    bicx.b #0xFE, r14
    call #guarda
; bloque 00be
    mova #0xB9F29, r4
    mov #0x3, r2
    swpb r4
    call #guarda
; bloque 00bf
    mov #0x2, r2
    swpb &zona+6
    call #guarda
; bloque 00c0
    mov #0, r15
    mov #0x4, r2
    call #subw
    call #guarda
; bloque 00c1
    mova #0xD566C, r5
    mova #0xFFFFF, r13
    mov #0x0, r2
    mova r5, &zona+158
    call #guarda
; bloque 00c2
    mov #0x5, r2
    rra.b &zona+161
    call #guarda
; bloque 00c3
    mov #zona+128, r13
    mov #zona+106, r14
    mov #0x5, r2
    subc.w @r14+, -116(r13)
    call #guarda
; bloque 00c4
    mova #0x95A34, r10
    mov #0x0008, r8
    mov #0x6, r2
    suba #0xFFFFF, r8
    call #guarda
; bloque 00c5
    mov #0x0000, r11
    mova #0x80000, r4
    mova #zona+126, r11
    mov #0x2, r2
    mova @r11+, r4
    call #guarda
; bloque 00c6
    mov #zona+86, r10
    mov #0x6, r2
    rrc.w @r10
    call #guarda
; bloque 00c7
    mova #0xFFFFF, r13
    mova #0x3DCB3, r7
    mov #0x4, r2
    rpt #4 { addx.w r13, r7
    call #guarda
; bloque 00c8
    mova #0xB4B66, r12
    mov #0x5, r2
    rram.a #1, r12
    call #guarda
; bloque 00c9
    mov #0x0000, r10
    mova #0x8A3BA, r5
    mova #zona+224, r10
    mov #0x4, r2
    mova @r10, r5
    call #guarda
; bloque 00ca
    mov #0x0008, r14
    mov #zona+128, r9
    mov #0x3, r2
    movx.a -52(r9), r14
    call #guarda
; bloque 00cb
    mova #0x80000, r15
    mov #zona+250, r7
    mov #0x1, r2
    mov.w @r7, r15
    call #guarda
; bloque 00cc
    mova #0x572A7, r13
    mov #0x5, r2
    bis.b &zona+155, r13
    call #guarda
; bloque 00cd
    mov #0x0000, r8
    mov #0x1, r2
    push.w r8
    pop r15
    call #guarda
; bloque 00ce
    mova #0x38094, r10
    mov #0x1000, r11
    mov #0x1, r2
    adda #0xFD1FC, r11
    call #guarda
; bloque 00cf
    mov #0x1016, r4
    mov #0x3, r2
    daddx.w #1, r4
    call #guarda
; bloque 00d0
    mov #zona+144, r12
    mov #0x2, r2
    subcx.w @r12, zona+142
    call #guarda
; bloque 00d1
    mova #0x80000, r13
    mov #zona+244, r15
    mov #0x6, r2
    sub.b @r15+, r13
    call #guarda
; bloque 00d2
    mova #0x627D1, r6
    mov #0x4, r2
    swpb r6
    call #guarda
; bloque 00d3
    mova #0x2CC87, r11
    mov #0x5, r2
    pushx.a r11
    popx.a r15
    call #guarda
; bloque 00d4
    mov #0, r15
    mov #subw, r4
    mov #0x0, r2
    call r4
    call #guarda
; bloque 00d5
    mova #0x80000, r8
    mova #0x7D4FA, r9
    mov #0x7, r2
    mova r8, &zona+208
    call #guarda
; bloque 00d6
    mov #zona+180, r8
    mov #0x6, r2
    cmpx.a @r8, zona+54
    call #guarda
; bloque 00d7
    mov #0x2, r2
    mov.b #8, zona+247
    call #guarda
; bloque 00d8
    mov #0, r15
    mov #0x5, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d9
    mov #0x3, r2
    subc.b zona+121, &zona+162
    call #guarda
; bloque 00da
    mov #0, r15
    mova #tabla_a, r6
    mov #0x7, r2
    calla @r6
    call #guarda
; bloque 00db
    mova #0xB50F5, r12
    mova #0x10000, r13
    mov #0x0, r2
    suba r12, r13
    call #guarda
; bloque 00dc
    mova #0x68FFE, r8
    mov #0x0000, r9
    mova #0xA28FE, r10
    mov #0x0, r2
    pushm.w #3, r10
    popm.w #3, r10
    call #guarda
; bloque 00dd
    mov #0x2, r2
    bitx.a zona+210, &zona+210
    call #guarda
; bloque 00de
    mov #zona+55, r8
    mov #0x7, r2
    addcx.b @r8+, &zona+11
    call #guarda
; bloque 00df
    mov #zona+128, r4
    mov #0x4, r2
    cmpx.b -78(r4), zona+118
    call #guarda
; bloque 00e0
    mov #0x0000, r12
    mova #0xF90E1, r11
    mov #0x2, r2
    mova &zona+104, r11
    call #guarda
; bloque 00e1
    mova #0x955F9, r12
    mov #0x1, r2
    rrcm.w #4, r12
    call #guarda
; bloque 00e2
    mova #0x7FFFE, r6
    mova #0xFFFFF, r9
    mova #zona+128, r9
    mov #0x4, r2
    mova r6, -6(r9)
    call #guarda
; bloque 00e3
    mova #0x2B8AE, r8
    mov #0x2, r2
    rra.w r8
    call #guarda
; bloque 00e4
    mov #0x948E, r8
    mova #0xFFFFF, r12
    mova #zona+128, r8
    mov #0x6, r2
    mova 120(r8), r12
    call #guarda
; bloque 00e5
    mova #0xA1F34, r12
    mov #0x6, r2
    rram.w #2, r12
    call #guarda
; bloque 00e6
    mov #0x0000, r12
    mov #0x6, r2
    bitx.w #0x856B, r12
    call #guarda
; bloque 00e7
    mova #0xFFFFF, r14
    mov #0x7, r2
    rrux.w r14
    call #guarda
; bloque 00e8
    mov #0x4E46, r15
    mov #0x0800, r4
    mov #0x5, r2
    rpt #7 { subcx.a r15, r4
    call #guarda
; bloque 00e9
    mov #0, r15
    mov #0x4, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ea
    mova #0x65340, r11
    mov #0x7, r2
    bitx.w zona+214, r11
    call #guarda
; bloque 00eb
    mova #0x6BAC8, r7
    mov #0x1, r2
    movx.b r7, r7
    call #guarda
; bloque 00ec
    mova #0x61E95, r8
    mov #0x0000, r12
    mov #0x0, r2
    adda r8, r12
    call #guarda
; bloque 00ed
    mov #0x0179, r11
    mov #0x1, r2
    dadd.w #8, r11
    call #guarda
; bloque 00ee
    mov #0x5055, r7
    mov #0x4, r2
    dadd.w #4, r7
    call #guarda
; bloque 00ef
    mov #zona+128, r12
    mova #0x40A13, r11
    mov #0x0, r2
    bis.b r11, -65(r12)
    call #guarda
; bloque 00f0
    mova #0x23777, r14
    mova #0x6A453, r15
    mov #0x5, r2
    pushm.w #2, r15
    popm.w #2, r10
    call #guarda
; bloque 00f1
    mova #0xAC5D7, r7
    mova #0xDC659, r12
    mov #0x7, r2
    mova &zona+38, r12
    call #guarda
; bloque 00f2
    mov #0x0400, r15
    mov #0x6, r2
    rlam.a #1, r15
    call #guarda
; bloque 00f3
    mov #zona+236, r6
    mov #0x4, r2
    andx.a @r6+, zona+148
    call #guarda
; bloque 00f4
    mov #0x2, r2
    swpb &zona+148
    call #guarda
; bloque 00f5
    mova #0x10000, r5
    mov #0x0, r2
    rrcx.w r5
    call #guarda
; bloque 00f6
    mov #zona+128, r9
    mov #0x2, r2
    movx.a #-1, -10(r9)
    call #guarda
; bloque 00f7
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f8
    mova #0xBFF46, r7
    mova #0x7FFFE, r4
    mov #0x3, r2
    adda #0xFE323, r4
    call #guarda
; bloque 00f9
    mov #0, r15
    mov #0x1, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00fa
    mova #0x7FFFF, r13
    mov #0x3, r2
    rpt #2 { rrcx.w r13
    call #guarda
; bloque 00fb
    mov #0x1, r2
    rrcx.w &zona+48
    call #guarda
; bloque 00fc
    mov #0x0008, r10
    mov #0x4, r2
    bic.w #0, r10
    call #guarda
; bloque 00fd
    mova #0xCFFBC, r12
    mov #zona+128, r13
    mov #0x4, r2
    subcx.w -110(r13), r12
    call #guarda
; bloque 00fe
    mov #0x0061, r15
    mov #0x6, r2
    daddx.b #0, r15
    call #guarda
; bloque 00ff
    mov #zona+128, r5
    mova #0x2881C, r12
    mov #0x4, r2
    subcx.b r12, -87(r5)
    call #guarda
; bloque 0100
    mov #zona+128, r4
    mov #0x4, r2
    subcx.a &zona+56, 10(r4)
    call #guarda
; bloque 0101
    mov #0x0200, r10
    mova #0xE8BD3, r11
    mov #0x3, r2
    mova r10, &zona+14
    call #guarda
; bloque 0102
    mova #0x18938, r10
    mov #0x7, r2
    swpbx r10
    call #guarda
; bloque 0103
    mova #0xFFFFF, r4
    mov #0x0, r2
    rrux.w r4
    call #guarda
; bloque 0104
    mova #0xA5296, r14
    mova #0x27FE8, r7
    mov #0x6, r2
    mova #0x22C73, r7
    call #guarda
; bloque 0105
    mov #0x0000, r13
    mova #0x45AD6, r14
    mov #0x5, r2
    rpt #2 { movx.b r13, r14
    call #guarda
; bloque 0106
    mova #0xB77D9, r10
    mova #0xFFFFF, r4
    mova #zona+128, r10
    mov #0x5, r2
    mova -42(r10), r4
    call #guarda
; bloque 0107
    mov #0x7, r2
    rrux.w &zona+212
    call #guarda
; bloque 0108
    mova #0x67EAF, r14
    mov #0x5, r2
    rlam.w #3, r14
    call #guarda
; bloque 0109
    mov #0x1, r2
    xor.w zona+156, zona+220
    call #guarda
; bloque 010a
    mov #0x2, r2
    pushx.w &zona+24
    pop r15
    call #guarda
; bloque 010b
    mov #zona+128, r10
    mova #0x7FFFE, r4
    mov #0x5, r2
    cmp.b r4, -34(r10)
    call #guarda
; bloque 010c
    mov #zona+128, r5
    mov #0x6, r2
    xorx.b zona+215, 88(r5)
    call #guarda
; bloque 010d
    mov #zona+128, r12
    mov #0x7, r2
    push.b 96(r12)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 010e
    mova #0xFFFFF, r15
    mov #0x4, r2
    rrux.w r15
    call #guarda
; bloque 010f
    mova #0xFFFFF, r8
    mov #0x1, r2
    subx.a &zona+224, r8
    call #guarda
; bloque 0110
    mova #0x10000, r14
    mov #0x0000, r5
    mov #0x2, r2
    rpt #5 { andx.a r14, r5
    call #guarda
; bloque 0111
    mov #0x9927, r9
    mov #0x6876, r7
    mov #0x2, r2
    dadd.w r7, r9
    call #guarda
; bloque 0112
    mova #0x7FFFF, r11
    mov #0x0080, r12
    mova #0xDD56D, r13
    mova #0x851C4, r14
    mov #0x1, r2
    pushm.a #4, r14
    popm.a #4, r10
    call #guarda
; bloque 0113
    mova #0x80000, r5
    mova #0xC1277, r6
    mova #0xFFFFF, r7
    mov #0x2, r2
    pushm.w #3, r7
    popm.w #3, r14
    call #guarda
; bloque 0114
    mov #0x7, r2
    rrux.w &zona+158
    call #guarda
; bloque 0115
    mov #0, r15
    mov #0x7, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0116
    mov #0x0000, r13
    mov #0x2, r2
    rram.w #1, r13
    call #guarda
; bloque 0117
    mov #0x0, r2
    add.w #1, &zona+146
    call #guarda
; bloque 0118
    mova #0x244C2, r8
    mova #0xA15AA, r9
    mova #0xE52F8, r10
    mov #0x5, r2
    pushm.w #3, r10
    popm.w #3, r7
    call #guarda
; bloque 0119
    mova #0xFFFFF, r14
    mov #0x2, r2
    bisx.w r14, zona+48
    call #guarda
; bloque 011a
    mov #0x3184, r6
    mov #0x8341, r8
    mov #0x1, r2
    dadd.w r8, r6
    call #guarda
; bloque 011b
    mova #0x507AD, r7
    mov #0x2, r2
    subc.b #0x40, r7
    call #guarda
; bloque 011c
    mova #0xD2DF4, r15
    mov #zona+128, r9
    mov #0x7, r2
    xorx.a -118(r9), r15
    call #guarda
; bloque 011d
    mova #0x22038, r7
    mov #0x0000, r4
    mov #0x5, r2
    mov.b r4, r7
    call #guarda
; bloque 011e
    mova #0x115F9, r4
    mova #0x2E64C, r11
    mova #zona+128, r4
    mov #0x2, r2
    mova 46(r4), r11
    call #guarda
; bloque 011f
    mova #0x6D856, r6
    mova #0x568DF, r12
    mov #0x7, r2
    adda #0xFFFFF, r12
    call #guarda
; bloque 0120
    mov #zona+128, r6
    mov #0x6, r2
    bit.w &zona+196, 52(r6)
    call #guarda
; bloque 0121
    mova #0x80000, r6
    mov #0xDB09, r7
    mova #0xB84F7, r8
    mova #0x80000, r9
    mov #0x5, r2
    pushm.a #4, r9
    popm.a #4, r7
    call #guarda
; bloque 0122
    mov #0, r15
    mov #0x2, r2
    calla #sub
    call #guarda
; bloque 0123
    mov #0x3, r2
    mov.w &zona+124, zona+64
    call #guarda
; bloque 0124
    mov #zona+164, r11
    mov #0x4, r2
    addc.b @r11, &zona+16
    call #guarda
; bloque 0125
    mov #0x7308, r12
    mov #0x8490, r4
    mov #0x6, r2
    dadd.w r4, r12
    call #guarda
; bloque 0126
    mov #0x5, r2
    rrux.w &zona+12
    call #guarda
; bloque 0127
    mov #0x0400, r8
    mov #0x1, r2
    and.w #1, r8
    call #guarda
; bloque 0128
    mova #0x7F13B, r12
    mov #0x7, r2
    addc.b #4, r12
    call #guarda
; bloque 0129
    mov #zona+128, r5
    mov #0x0, r2
    bis.w -98(r5), zona+62
    call #guarda
; bloque 012a
    mov #zona+128, r8
    mov #0x6, r2
    xor.w r8, 60(r8)
    call #guarda
; bloque 012b
    mova #0xC3743, r6
    mov #0x2, r2
    swpbx r6
    call #guarda
; bloque 012c
    mova #0xC7858, r11
    mov #0x6, r2
    pushm.w #1, r11
    popm.w #1, r6
    call #guarda
; bloque 012d
    mov #zona+128, r10
    mov #zona+128, r8
    mov #0x4, r2
    add.b -119(r8), 102(r10)
    call #guarda
; bloque 012e
    mov #zona+128, r5
    mov #0x4, r2
    sub.b &zona+5, -112(r5)
    call #guarda
; bloque 012f
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0130
    mov #0x2, r2
    pushx.b zona+171
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0131
    mova #0x5895B, r5
    mova #0x32982, r9
    mov #0x0, r2
    addc.b r9, r5
    call #guarda
; bloque 0132
    mov #zona+102, r15
    mov #0x4, r2
    addx.w @r15+, &zona+10
    call #guarda
; bloque 0133
    mova #0x5BAD2, r7
    mov #0x3, r2
    rrum.w #2, r7
    call #guarda
; bloque 0134
    mova #0xC7784, r13
    mov #0x2, r2
    cmpx.b zona+122, r13
    call #guarda
; bloque 0135
    mov #0x2, r2
    push.b &zona+153
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0136
    mova #0xB3DB0, r4
    mov #0x0000, r12
    mov #0x5, r2
    bit.w r12, r4
    call #guarda
; bloque 0137
    mov #0, r15
    mov #0x6, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0138
    mov #zona+165, r14
    mov #0x2, r2
    rrux.b @r14
    call #guarda
; bloque 0139
    mov #0x9137, r12
    mov #0x3716, r15
    mov #0x4, r2
    daddx.w r15, r12
    call #guarda
; bloque 013a
    mov #zona+128, r6
    mov #0x1, r2
    addx.w 20(r6), &zona+232
    call #guarda
; bloque 013b
    mova #0xC185B, r5
    mova #0x774CB, r8
    mova #zona+190, r5
    mov #0x5, r2
    mova @r5, r8
    call #guarda
; bloque 013c
    mov #0, r15
    mov #0x1, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 013d
    mova #0x4FB3C, r6
    mov #0x4, r2
    rrcm.a #2, r6
    call #guarda
; bloque 013e
    mova #0xD1615, r10
    mova #0xF7841, r14
    mova #0x2865D, r9
    mov #0x3, r2
    rpt r9 { andx.b r10, r14
    call #guarda
; bloque 013f
    mova #0x7FFFF, r7
    mova #0x5B689, r13
    mov #0x7, r2
    mova r7, &zona+250
    call #guarda
; bloque 0140
    mova #0x708CC, r7
    mova #0xFFFFF, r8
    mova #0x9BE94, r9
    mova #0x1694D, r10
    mova #0x843A1, r11
    mov #0x5, r2
    pushm.w #5, r11
    popm.w #5, r12
    call #guarda
; bloque 0141
    mov #0x0100, r5
    mov #0x4, r2
    rrcx.w r5
    call #guarda
; bloque 0142
    mov #0x5, r2
    subx.a #0, zona+94
    call #guarda
; bloque 0143
    mova #0xEB7C4, r6
    mova #0xCACC8, r11
    mov #0x2, r2
    adda r6, r11
    call #guarda
; bloque 0144
    mova #0xFFFFF, r10
    mova #0x32DB1, r9
    mov #0x0000, r15
    mov #0x1, r2
    rpt r15 { addx.w r10, r9
    call #guarda
; bloque 0145
    mov #0, r15
    mov #0x7, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0146
    mova #0x14610, r5
    mova #0x24566, r4
    mova #0x76FDF, r7
    mov #0x4, r2
    rpt r7 { addcx.w r5, r4
    call #guarda
; bloque 0147
    mova #0x80000, r8
    mov #0x2, r2
    rlam.w #4, r8
    call #guarda
; bloque 0148
    mov #zona+132, r5
    mov #0x5, r2
    pushx.w @r5
    pop r15
    call #guarda
; bloque 0149
    mova #0x2430B, r9
    mov #0x0000, r8
    mov #0x0, r2
    rpt #9 { bicx.a r9, r8
    call #guarda
; bloque 014a
    mov #0x1387, r14
    mov #0x4, r2
    dadd.w r14, r14
    call #guarda
; bloque 014b
    mov #0x0200, r10
    mova #0xCC9BE, r5
    mov #0x2, r2
    mova #0x9A94D, r5
    call #guarda
; bloque 014c
    mova #0x20000, r6
    mova #0xFFFA1, r7
    mova #0xAE8FE, r8
    mova #0x43110, r9
    mova #0x73185, r10
    mov #0x4, r2
    pushm.w #5, r10
    popm.w #5, r12
    call #guarda
; bloque 014d
    mov #zona+128, r5
    mov #0x2, r2
    rrux.w 108(r5)
    call #guarda
; bloque 014e
    mov #0, r15
    mov #0x7, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 014f
    mov #0x6, r2
    rrc.w &zona+220
    call #guarda
; bloque 0150
    mov #zona+110, r13
    mov #0x6, r2
    push.b @r13
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0151
    mova #0x2F0DE, r6
    mov #0x7, r2
    pushm.a #1, r6
    popm.a #1, r7
    call #guarda
; bloque 0152
    mov #zona+128, r9
    mov #0x5, r2
    sub.b #-1, 38(r9)
    call #guarda
; bloque 0153
    mov #zona+164, r12
    mov #0x0, r2
    mov.b @r12+, &zona+227
    call #guarda
; bloque 0154
    mov #0x0000, r10
    mov #zona+122, r15
    mov #0x0, r2
    bisx.w @r15, r10
    call #guarda
; bloque 0155
    mov #0, r15
    mov #0x3, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0156
    mova #0x32549, r5
    mov #zona+128, r4
    mov #0x2, r2
    cmp.b -80(r4), r5
    call #guarda
; bloque 0157
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0158
    mova #0x65E22, r7
    mova #0x7FFFF, r14
    mova #0xFFFFF, r6
    mov #0x5, r2
    rpt r6 { andx.w r7, r14
    call #guarda
; bloque 0159
    mov #zona+70, r10
    mov #0x3, r2
    subc.w @r10+, zona+92
    call #guarda
; bloque 015a
    mova #0xB8BB9, r4
    mova #0xB811C, r15
    mov #0x2, r2
    suba #0xFFFFF, r15
    call #guarda
; bloque 015b
    mova #0x45818, r13
    mova #0x519F6, r6
    mov #0x1, r2
    suba #0x00080, r6
    call #guarda
; bloque 015c
    mova #0x7B728, r14
    mov #zona+248, r6
    mov #0x1, r2
    movx.a @r6, r14
    call #guarda
; bloque 015d
    mov #zona+128, r6
    mov #0x3, r2
    subc.b &zona+57, 26(r6)
    call #guarda
; bloque 015e
    mov #zona+128, r15
    mov #0x0, r2
    bisx.b #0x0, -29(r15)
    call #guarda
; bloque 015f
    mova #0xD0B6B, r8
    mov #0x6, r2
    cmp.w #0x8265, r8
    call #guarda
; bloque 0160
    mov #zona+128, r7
    mov #0x2, r2
    bit.w zona+180, -108(r7)
    call #guarda
; bloque 0161
    mov #0x4, r2
    push.w #0x10
    pop r15
    call #guarda
; bloque 0162
    mova #0xFFFFF, r13
    mov #zona+35, r9
    mov #0x2, r2
    bic.b @r9+, r13
    call #guarda
; bloque 0163
    mov #0x0000, r5
    mova #0x94E26, r6
    mova #0x7FFFF, r7
    mova #0x2FD2B, r8
    mov #0x1, r2
    pushm.w #4, r8
    popm.w #4, r13
    call #guarda
; bloque 0164
    mov #0x6, r2
    push.b #0x0
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0165
    mov #0x0800, r8
    mova #0x55D82, r13
    mov #0x4, r2
    bit.b r13, r8
    call #guarda
; bloque 0166
    mov #0x01AD, r10
    mova #0x5D370, r14
    mov #0x2, r2
    suba #0xFFFFF, r14
    call #guarda
; bloque 0167
    mova #0xBA957, r5
    mov #0x7, r2
    cmp.b r5, &zona+201
    call #guarda
; bloque 0168
    mova #0x78E05, r12
    mova #0xBF636, r11
    mova #zona+236, r12
    mov #0x7, r2
    mova @r12+, r11
    call #guarda
; bloque 0169
    mov #0x8000, r4
    mov #0x0000, r5
    mov #0x1, r2
    rpt #8 { andx.a r4, r5
    call #guarda
; bloque 016a
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 016b
    mova #0xFFFFF, r7
    mov #zona+22, r10
    mov #0x4, r2
    xorx.w @r10, r7
    call #guarda
; bloque 016c
    mova #0x854B3, r10
    mova #0x781A8, r14
    mov #0x1, r2
    rpt #11 { movx.w r10, r14
    call #guarda
; bloque 016d
    mova #0xDFCAC, r12
    mova #0x80000, r10
    mov #0x0, r2
    mova r12, r10
    call #guarda
; bloque 016e
    mova #0x80000, r10
    mov #0x5, r2
    bitx.w r10, &zona+88
    call #guarda
; bloque 016f
    mov #0x0000, r8
    mov #zona+128, r5
    mov #0x2, r2
    movx.b 84(r5), r8
    call #guarda
; bloque 0170
    mova #0x941E0, r10
    mova #0xFFFFF, r13
    mov #0x5, r2
    suba #0xAD052, r13
    call #guarda
; bloque 0171
    mov #0x6, r2
    push.b #0xF1
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0172
    mova #0x7FFFF, r12
    mov #zona+128, r10
    mov #0x3, r2
    andx.b -40(r10), r12
    call #guarda
; bloque 0173
    mov #0x0, r2
    addx.b #2, zona+77
    call #guarda
; bloque 0174
    mov #zona+168, r6
    mov #0x6, r2
    swpb @r6
    call #guarda
; bloque 0175
    mova #0x77506, r14
    mova #0x7FFFF, r10
    mova #0x7FFFF, r6
    mov #0x6, r2
    rpt r6 { movx.a r14, r10
    call #guarda
; bloque 0176
    mov #zona+128, r14
    mov #0x1, r2
    bis.w #8, 106(r14)
    call #guarda
; bloque 0177
    mov #0x0040, r9
    mov #0x5, r2
    xorx.b #0, r9
    call #guarda
; bloque 0178
    mov #zona+128, r12
    mov #0x2, r2
    rrc.w 96(r12)
    call #guarda
; bloque 0179
    mova #0x765DF, r8
    mov #0x0, r2
    rram.a #3, r8
    call #guarda
; bloque 017a
    mov #0x5, r2
    rra.b &zona+122
    call #guarda
; bloque 017b
    mov #0x5, r2
    swpbx &zona+8
    call #guarda
; bloque 017c
    mova #0xFFFFF, r6
    mov #0x5, r2
    push.w r6
    pop r15
    call #guarda
; bloque 017d
    mov #zona+214, r6
    mov #0x6, r2
    andx.a @r6+, &zona+62
    call #guarda
; bloque 017e
    mova #0xA8467, r5
    mov #0x3, r2
    cmp.b zona+97, r5
    call #guarda
; bloque 017f
    mov #zona+128, r15
    mov #0x1, r2
    addx.b #1, -31(r15)
    call #guarda
; bloque 0180
    mov #zona+168, r7
    mov #0x4, r2
    rrax.w @r7
    call #guarda
; bloque 0181
    mov #0x0000, r5
    mov #0x6, r2
    xor.b r5, &zona+71
    call #guarda
; bloque 0182
    mova #0x6A536, r12
    mov #0x4, r2
    rrum.w #1, r12
    call #guarda
; bloque 0183
    mova #0x80000, r10
    mova #0xFFFFF, r9
    mov #0x2, r2
    rpt r9 { rrux.b r10
    call #guarda
; bloque 0184
    mov #0x0000, r12
    mov #0x3, r2
    subcx.w r12, zona+192
    call #guarda
; bloque 0185
    mova #0x6EBC5, r9
    mov #0x6, r2
    addc.w #0x0, r9
    call #guarda
; bloque 0186
    mova #0xFD602, r13
    mova #0x9AF59, r9
    mov #0x3, r2
    cmpa r13, r9
    call #guarda
; bloque 0187
    mova #0x49967, r9
    mova #0x80000, r10
    mov #0x4, r2
    rpt r10 { swpbx r9
    call #guarda
; bloque 0188
    mov #0x2, r2
    rra.b &zona+62
    call #guarda
; bloque 0189
    mov #0x0033, r13
    mov #0x1, r2
    daddx.b #8, r13
    call #guarda
; bloque 018a
    mov #zona+128, r15
    mova #0x80000, r12
    mov #0x1, r2
    cmpx.b r12, 33(r15)
    call #guarda
; bloque 018b
    mova #0xDF6EA, r14
    mova #0xFFFFF, r10
    mov #0x1, r2
    cmpa r14, r10
    call #guarda
; bloque 018c
    mov #0x6458, r8
    mov #0x8883, r14
    mov #0x0, r2
    daddx.w r14, r8
    call #guarda
; bloque 018d
    mova #0xCD6F7, r15
    mov #0x6, r2
    rrum.w #4, r15
    call #guarda
; bloque 018e
    mova #0x685E7, r10
    mova #0x7CCB7, r4
    mov #0x7, r2
    rpt r4 { rrax.a r10
    call #guarda
; bloque 018f
    mova #0xF4C53, r15
    mov #0x2, r2
    addcx.a #0x6F8D0, r15
    call #guarda
; bloque 0190
    mov #zona+158, r10
    mov #0x2, r2
    subc.w @r10, zona+170
    call #guarda
; bloque 0191
    mova #0xFFFFF, r8
    mov #0x0, r2
    xor.b r8, zona+162
    call #guarda
; bloque 0192
    mov #zona+250, r9
    mov #0x1, r2
    swpbx @r9
    call #guarda
; bloque 0193
    mova #0x19369, r7
    mova #0xFED12, r12
    mov #0x4, r2
    addx.a r12, r7
    call #guarda
; bloque 0194
    mov #0, r15
    mova #sub, r11
    mov #0x5, r2
    calla r11
    call #guarda
; bloque 0195
    mov #0x0002, r11
    mov #0x2, r2
    swpbx r11
    call #guarda
; bloque 0196
    mov #zona+128, r12
    mov #0x6, r2
    bit.b #0x2, 31(r12)
    call #guarda
; bloque 0197
    mov #zona+128, r10
    mov #0x1, r2
    sub.b #0, -62(r10)
    call #guarda
; bloque 0198
    mov #zona+24, r15
    mov #0x7, r2
    rrux.a @r15
    call #guarda
; bloque 0199
    mova #0x54175, r5
    mov #zona+128, r6
    mov #0x4, r2
    addc.w 4(r6), r5
    call #guarda
; bloque 019a
    mov #0x7, r2
    push.w #0x0
    pop r15
    call #guarda
; bloque 019b
    mov #0x0000, r12
    mova #0xFFFFF, r4
    mov #0x7, r2
    adda r12, r4
    call #guarda
; bloque 019c
    mova #0xD0AED, r9
    mova #0x60BC9, r11
    mov #0x1, r2
    mova r9, r11
    call #guarda
; bloque 019d
    mova #0xD9C64, r6
    mov #0x5, r2
    pushm.w #1, r6
    popm.w #1, r7
    call #guarda
; bloque 019e
    mova #0x98B5D, r8
    mov #0x0000, r12
    mov #0x4, r2
    cmpa r8, r12
    call #guarda
; bloque 019f
    mova #0xB0330, r10
    mova #0x7BDCF, r13
    mov #0x6, r2
    cmpa r10, r13
    call #guarda
; bloque 01a0
    mov #0x7975, r4
    mov #0x2224, r9
    mov #0x6, r2
    daddx.w r9, r4
    call #guarda
; bloque 01a1
    mov #0x0000, r4
    mova #0x80000, r5
    mov #0x1, r2
    pushm.w #2, r5
    popm.w #2, r12
    call #guarda
; bloque 01a2
    mov #zona+24, r8
    mov #0x5, r2
    rrux.a @r8
    call #guarda
; bloque 01a3
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a4
    mova #0x7FFFF, r7
    mov #0x0000, r11
    mov #0x7, r2
    mova #0x8771B, r11
    call #guarda
; bloque 01a5
    mov #0x6, r2
    rrax.b &zona+158
    call #guarda
; bloque 01a6
    mova #0x4846D, r15
    mov #0x3, r2
    rrum.w #2, r15
    call #guarda
; bloque 01a7
    mov #zona+128, r5
    mov #0x5, r2
    bicx.w #0xA7D8, -34(r5)
    call #guarda
; bloque 01a8
    mova #0x7FFFE, r10
    mova #0x11CF9, r11
    mov #0x1000, r12
    mova #0x8FDD1, r13
    mov #0x0004, r14
    mov #0x0, r2
    pushm.a #5, r14
    popm.a #5, r10
    call #guarda
; bloque 01a9
    mov #0, r15
    mov #tabla, r9
    mov #0x3, r2
    call @r9
    call #guarda
; bloque 01aa
    mov #zona+38, r9
    mov #0x6, r2
    swpbx @r9
    call #guarda
; bloque 01ab
    mov #0x2, r2
    swpbx &zona+80
    call #guarda
; bloque 01ac
    mov #zona+128, r14
    mov #0x4, r2
    sub.b #0x6E, 32(r14)
    call #guarda
; bloque 01ad
    mova #0xFFFFF, r6
    mova #0xD9C13, r7
    mova #0xB7855, r8
    mova #0xC429B, r9
    mov #0x5, r2
    pushm.w #4, r9
    popm.w #4, r13
    call #guarda
; bloque 01ae
    mov #zona+128, r11
    mova #0x7FFFF, r4
    mov #0x0, r2
    sub.w r4, 28(r11)
    call #guarda
; bloque 01af
    mov #0x2, r2
    push.w &zona+186
    pop r15
    call #guarda
; bloque 01b0
    mova #0x6FA18, r12
    mov #0x1, r2
    bisx.w r12, &zona+18
    call #guarda
; bloque 01b1
    mov #0x7, r2
    rrc.b zona+179
    call #guarda
; bloque 01b2
    mova #0x92598, r15
    mov #0x4, r2
    bic.w #1, r15
    call #guarda
; bloque 01b3
    mov #0, r15
    mov #0x4, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 01b4
    mov #zona+234, r12
    mov #0x0, r2
    andx.a @r12+, &zona+202
    call #guarda
; bloque 01b5
    mov #0x0024, r13
    mov #0x3, r2
    daddx.b #8, r13
    call #guarda
; bloque 01b6
    mova #0xDE0C8, r15
    mov #0x0800, r11
    mova #0xC0B23, r6
    mov #0x2, r2
    rpt r6 { andx.b r15, r11
    call #guarda
; bloque 01b7
    mov #0x0000, r12
    mova #0xFFFFF, r5
    mova #0x10000, r8
    mov #0x0, r2
    rpt r8 { subx.b r12, r5
    call #guarda
; bloque 01b8
    mov #zona+246, r4
    mov #0x6, r2
    subcx.a @r4+, zona+14
    call #guarda
; bloque 01b9
    mov #zona+237, r12
    mov #0x4, r2
    sub.b @r12+, &zona+121
    call #guarda
; bloque 01ba
    mova #0xAF8AB, r13
    mova #0xC5207, r14
    mova #0xD7504, r15
    mov #0x0, r2
    pushm.a #3, r15
    popm.a #3, r12
    call #guarda
; bloque 01bb
    mov #zona+238, r14
    mov #0x6, r2
    push.w @r14
    pop r15
    call #guarda
; bloque 01bc
    mov #0, r15
    mov #0x6, r2
    call #subw
    call #guarda
; bloque 01bd
    mov #0, r15
    mov #0x5, r2
    calla #sub
    call #guarda
; bloque 01be
    mov #0x0000, r11
    mova #0xFFFFF, r12
    mov #0x1, r2
    rpt r12 { swpbx r11
    call #guarda
; bloque 01bf
    mova #0x7FFFF, r13
    mova #0x343EF, r14
    mova #0xFFFFF, r15
    mov #0x6, r2
    pushm.w #3, r15
    popm.w #3, r13
    call #guarda
; bloque 01c0
    mov #0x0000, r8
    mova #0xFA005, r12
    mov #0x6, r2
    bit.w r12, r8
    call #guarda
; bloque 01c1
    mova #0x3C7C0, r10
    mov #0x2, r2
    rrcm.a #4, r10
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
