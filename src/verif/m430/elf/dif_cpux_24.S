    .text
    .global prueba
    .global fin
prueba:
    pushm.a #7, r10
    mov r1, &sp_guardado
    movx.a #res, &resptr
; bloque 0000
    mov #0x0020, r13
    mova #0xFFFFF, r6
    mov #0x0, r2
    suba r13, r6
    call #guarda
; bloque 0001
    mova #0x3B2F1, r15
    mov #0x0000, r4
    mova #zona+128, r15
    mov #0x4, r2
    mova -86(r15), r4
    call #guarda
; bloque 0002
    mov #zona+128, r8
    mov #zona+19, r13
    mov #0x4, r2
    add.b @r13, 117(r8)
    call #guarda
; bloque 0003
    mova #0x4E4D1, r7
    mov #0x2, r2
    cmp.w #0xFAA5, r7
    call #guarda
; bloque 0004
    mova #0xA6052, r11
    mova #0x43402, r12
    mova #0x84CCC, r13
    mova #0x7392C, r14
    mova #0x7C36E, r15
    mov #0x7, r2
    pushm.a #5, r15
    popm.a #5, r10
    call #guarda
; bloque 0005
    mov #zona+128, r11
    mov #0x3, r2
    rrax.w -112(r11)
    call #guarda
; bloque 0006
    mova #0x7F74C, r10
    mova #0x1148B, r9
    mov #0x4, r2
    suba r10, r9
    call #guarda
; bloque 0007
    mova #0xD17E7, r4
    mova #0xFE9F1, r5
    mova #zona+146, r4
    mov #0x0, r2
    mova @r4, r5
    call #guarda
; bloque 0008
    mov #0x7DF4, r4
    mov #0x2, r2
    cmpx.w r4, &zona+164
    call #guarda
; bloque 0009
    mova #0x7FFFE, r10
    mov #0x0000, r11
    mova #0xF6776, r12
    mova #0x22DFB, r13
    mov #0x7, r2
    pushm.w #4, r13
    popm.w #4, r12
    call #guarda
; bloque 000a
    mov #0, r15
    mov #0x5, r2
    calla #sub
    call #guarda
; bloque 000b
    mova #0x35299, r9
    mova #0xFFFFF, r6
    mov #0x0, r2
    subc.b r6, r9
    call #guarda
; bloque 000c
    mov #zona+65, r14
    mov #0x2, r2
    rrux.b @r14
    call #guarda
; bloque 000d
    mov #0x7, r2
    rrax.a zona+38
    call #guarda
; bloque 000e
    mov #0x0200, r13
    mova #0x79920, r6
    mov #0x0000, r12
    mov #0x1, r2
    rpt r12 { subcx.b r13, r6
    call #guarda
; bloque 000f
    mov #0x7, r2
    swpb &zona+222
    call #guarda
; bloque 0010
    mov #0x8000, r7
    mov #0x4, r2
    rram.a #3, r7
    call #guarda
; bloque 0011
    mova #0x96487, r9
    mova #0x80000, r7
    mov #0x0, r2
    sub.b r7, r9
    call #guarda
; bloque 0012
    mov #zona+128, r11
    mov #0x7, r2
    rrcx.w -38(r11)
    call #guarda
; bloque 0013
    mova #0x64491, r13
    mov #0x4, r2
    mov.b &zona+233, r13
    call #guarda
; bloque 0014
    mov #zona+128, r14
    mov #0x3, r2
    add.w &zona+180, 28(r14)
    call #guarda
; bloque 0015
    mov #zona+4, r5
    mov #0x5, r2
    addcx.b @r5, zona+156
    call #guarda
; bloque 0016
    mova #0x75F3D, r11
    mova #0xB34C3, r6
    mov #0x5, r2
    adda r11, r6
    call #guarda
; bloque 0017
    mov #0, r15
    mov #0x4, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0018
    mova #0xC339C, r8
    mov #0x7, r2
    swpbx r8
    call #guarda
; bloque 0019
    mov #0x1BDE, r12
    mov #0x4, r2
    rram.a #1, r12
    call #guarda
; bloque 001a
    mov #0x0, r2
    rrux.b zona+62
    call #guarda
; bloque 001b
    mova #0xF367C, r4
    mova #0x41554, r5
    mov #0x6, r2
    pushm.a #2, r5
    popm.a #2, r10
    call #guarda
; bloque 001c
    mov #0x0040, r10
    mova #0x6FD33, r13
    mova #0xFFFFF, r11
    mov #0x4, r2
    rpt r11 { addcx.a r10, r13
    call #guarda
; bloque 001d
    mova #0x40AE4, r8
    mova #0x7FFFE, r5
    mova #zona+202, r8
    mov #0x5, r2
    mova @r8, r5
    call #guarda
; bloque 001e
    mov #0, r15
    mov #0x6, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 001f
    mov #zona+128, r13
    mov #zona+128, r5
    mov #0x6, r2
    addx.a -46(r5), -92(r13)
    call #guarda
; bloque 0020
    mova #0x8DF32, r5
    mova #0xF0CEE, r10
    mov #0x2, r2
    addc.b r10, r5
    call #guarda
; bloque 0021
    mov #0, r15
    mov #0x1, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0022
    mov #0x0000, r4
    mova #0xE2B2B, r6
    mova #0x24FEF, r5
    mov #0x0, r2
    rpt r5 { addcx.w r4, r6
    call #guarda
; bloque 0023
    mov #zona+128, r12
    mova #0xB4A67, r9
    mov #0x4, r2
    add.b r9, 63(r12)
    call #guarda
; bloque 0024
    mova #0xC4A5C, r6
    mova #0x35FEF, r7
    mov #0x6, r2
    rpt #13 { subcx.a r6, r7
    call #guarda
; bloque 0025
    mov #zona+216, r14
    mov #0x1, r2
    add.w @r14+, zona+216
    call #guarda
; bloque 0026
    mova #0x7631B, r12
    mov #zona+172, r7
    mov #0x3, r2
    addc.w @r7, r12
    call #guarda
; bloque 0027
    mov #0x0, r2
    push.b zona+173
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0028
    mova #0x4652F, r4
    mova #0xFFFFF, r14
    mov #0x4, r2
    mova r4, r14
    call #guarda
; bloque 0029
    mov #zona+128, r6
    mov #0x3, r2
    subx.w #0x7FFF, -108(r6)
    call #guarda
; bloque 002a
    mov #0x0000, r8
    mov #0x5, r2
    rram.a #1, r8
    call #guarda
; bloque 002b
    mova #0xFFFFF, r14
    mov #zona+16, r6
    mov #0x5, r2
    bisx.w @r6, r14
    call #guarda
; bloque 002c
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 002d
    mov #zona+94, r15
    mov #0x3, r2
    rra.b @r15
    call #guarda
; bloque 002e
    mov #0x0400, r9
    mov #zona+42, r8
    mov #0x1, r2
    addcx.b @r8, r9
    call #guarda
; bloque 002f
    mov #0, r15
    mov #0x6, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0030
    mova #0x5F0E7, r15
    mova #0x64CC1, r11
    mova #zona+166, r15
    mov #0x2, r2
    mova @r15, r11
    call #guarda
; bloque 0031
    mova #0xBE3BD, r13
    mov #0x6, r2
    addc.w zona+168, r13
    call #guarda
; bloque 0032
    mova #0x38DD0, r9
    mov #0x1, r2
    xor.w r9, zona+80
    call #guarda
; bloque 0033
    mov #zona+128, r9
    mov #0x7, r2
    subcx.a #0x4AEDD, 22(r9)
    call #guarda
; bloque 0034
    mova #0x1B0F4, r10
    mov #0x4, r2
    rram.w #4, r10
    call #guarda
; bloque 0035
    mov #0x2, r2
    bisx.b zona+89, zona+33
    call #guarda
; bloque 0036
    mov #0x1, r2
    swpbx &zona+152
    call #guarda
; bloque 0037
    mova #0x4A399, r14
    mova #0xC4E3A, r12
    mov #0x3, r2
    rpt #11 { andx.a r14, r12
    call #guarda
; bloque 0038
    mov #zona+140, r8
    mov #0x6, r2
    rrax.w @r8
    call #guarda
; bloque 0039
    mov #0, r15
    mov #0x5, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 003a
    mov #zona+128, r13
    mov #0x6, r2
    addx.w #-1, 16(r13)
    call #guarda
; bloque 003b
    mov #zona+128, r11
    mov #0x5, r2
    xor.b &zona+161, 56(r11)
    call #guarda
; bloque 003c
    mov #zona+102, r14
    mov #0x0, r2
    addc.b @r14, zona+176
    call #guarda
; bloque 003d
    mov #zona+128, r13
    mov #zona+140, r14
    mov #0x5, r2
    bit.w @r14+, -46(r13)
    call #guarda
; bloque 003e
    mov #0x0008, r5
    mov #zona+238, r11
    mov #0x6, r2
    andx.w @r11, r5
    call #guarda
; bloque 003f
    mov #0x0000, r12
    mova #0xD564C, r13
    mova #0x7FFFE, r14
    mov #0x3, r2
    pushm.a #3, r14
    popm.a #3, r8
    call #guarda
; bloque 0040
    mova #0x200C1, r5
    mov #0x6, r2
    rrum.w #2, r5
    call #guarda
; bloque 0041
    mov #0x0049, r9
    mov #0x5, r2
    dadd.b #0, r9
    call #guarda
; bloque 0042
    mov #0x2690, r6
    mov #0x0, r2
    dadd.w #0x779, r6
    call #guarda
; bloque 0043
    mov #0x7, r2
    addx.a #4, zona+194
    call #guarda
; bloque 0044
    mova #0x3FF25, r8
    mov #0x4, r2
    and.w r8, zona+86
    call #guarda
; bloque 0045
    mov #0x0097, r11
    mov #0x0092, r8
    mov #0x1, r2
    dadd.b r8, r11
    call #guarda
; bloque 0046
    mov #0, r15
    mova #sub, r8
    mov #0x4, r2
    calla r8
    call #guarda
; bloque 0047
    mova #0xD33D4, r6
    mov #0x7, r2
    rlam.w #1, r6
    call #guarda
; bloque 0048
    mov #zona+128, r12
    mov #0x6, r2
    bisx.b -63(r12), zona+172
    call #guarda
; bloque 0049
    mov #0x2, r2
    bicx.b zona+52, zona+94
    call #guarda
; bloque 004a
    mov #zona+128, r10
    mov #0x2, r2
    mov.b #0xA, -39(r10)
    call #guarda
; bloque 004b
    mov #0x0000, r4
    mova #0xB20B8, r5
    mova #0xE7A1A, r6
    mov #0x5, r2
    pushm.a #3, r6
    popm.a #3, r6
    call #guarda
; bloque 004c
    mova #0x24F69, r4
    mov #0x0800, r11
    mov #0x7, r2
    and.b r11, r4
    call #guarda
; bloque 004d
    mova #0x354F5, r11
    mova #0x63A51, r9
    mov #0x2, r2
    mova &zona+4, r9
    call #guarda
; bloque 004e
    mova #0xB6CB7, r11
    mova #0x53437, r12
    mov #0x0, r2
    pushm.w #2, r12
    popm.w #2, r5
    call #guarda
; bloque 004f
    mov #0x7, r2
    rrcx.w zona+88
    call #guarda
; bloque 0050
    mov #0x0, r2
    bit.w #0xCB2A, &zona+196
    call #guarda
; bloque 0051
    mova #0x8814A, r5
    mov #zona+128, r6
    mov #0x3, r2
    add.b -54(r6), r5
    call #guarda
; bloque 0052
    mov #0, r15
    mov #0x3, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0053
    mova #0x9DB2A, r10
    mova #0xFFFFF, r13
    mov #0x3, r2
    rpt #9 { movx.a r10, r13
    call #guarda
; bloque 0054
    mov #0x5FCC, r7
    mov #0x0, r2
    pushm.w #1, r7
    popm.w #1, r13
    call #guarda
; bloque 0055
    mov #0x1, r2
    cmpx.b #0xB3, &zona+243
    call #guarda
; bloque 0056
    mov #0x0008, r7
    mova #0x46245, r8
    mova #0xD2650, r9
    mov #0x0800, r10
    mova #0x82FED, r11
    mov #0x2, r2
    pushm.w #5, r11
    popm.w #5, r15
    call #guarda
; bloque 0057
    mov #zona+112, r9
    mov #0x3, r2
    andx.a @r9, zona+160
    call #guarda
; bloque 0058
    mova #0x773DB, r7
    mova #0x80000, r15
    mov #0x7, r2
    adda #0xC3230, r15
    call #guarda
; bloque 0059
    mova #0x87767, r8
    mova #0x97E52, r9
    mov #0x5, r2
    pushm.a #2, r9
    popm.a #2, r8
    call #guarda
; bloque 005a
    mova #0x5729B, r10
    mov #0x0000, r11
    mov #0x5, r2
    xorx.a r11, r10
    call #guarda
; bloque 005b
    mov #0x0100, r10
    mov #0x1, r2
    sub.b #8, r10
    call #guarda
; bloque 005c
    mova #0x7A569, r12
    mova #0x80000, r13
    mov #0xD2BF, r14
    mov #0x3, r2
    pushm.a #3, r14
    popm.a #3, r13
    call #guarda
; bloque 005d
    mova #0xDD0E6, r9
    mov #0x5, r2
    pushx.w r9
    pop r15
    call #guarda
; bloque 005e
    mova #0x6380D, r8
    mova #0xFFFFF, r13
    mova #0xB6BA2, r11
    mov #0x1, r2
    rpt r11 { subcx.w r8, r13
    call #guarda
; bloque 005f
    mov #0x7076, r6
    mov #0x6070, r15
    mova #0x8973D, r10
    mov #0x3, r2
    rpt r10 { daddx.w r6, r15
    call #guarda
; bloque 0060
    mov #zona+94, r14
    mov #0x6, r2
    subcx.a @r14, &zona+36
    call #guarda
; bloque 0061
    mov #0x0002, r7
    mova #0x80000, r13
    mova #zona+132, r7
    mov #0x0, r2
    mova @r7+, r13
    call #guarda
; bloque 0062
    mova #0x9F2BA, r12
    mov #0x1, r2
    rram.w #3, r12
    call #guarda
; bloque 0063
    mova #0xFFFFF, r6
    mov #0x7, r2
    bis.b r6, zona+239
    call #guarda
; bloque 0064
    mov #zona+37, r6
    mov #0x1, r2
    subcx.b @r6, &zona+230
    call #guarda
; bloque 0065
    mov #0x0027, r6
    mov #0x0, r2
    daddx.b #0x20, r6
    call #guarda
; bloque 0066
    mova #0x51102, r10
    mova #0x7FFFF, r15
    mov #0x0, r2
    bit.b r15, r10
    call #guarda
; bloque 0067
    mov #0, r15
    mov #0x1, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0068
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0069
    mova #0x7FFFF, r12
    mov #0x7, r2
    subx.a zona+184, r12
    call #guarda
; bloque 006a
    mov #0x6, r2
    rrcx.a zona+212
    call #guarda
; bloque 006b
    mov #0x0001, r14
    mova #0x2BFFD, r11
    mov #0x5, r2
    andx.b r11, r14
    call #guarda
; bloque 006c
    mova #0x7FE3A, r15
    mov #0x6, r2
    bis.w r15, zona+34
    call #guarda
; bloque 006d
    mov #0x3006, r6
    mov #0x0, r2
    daddx.w #0x1989, r6
    call #guarda
; bloque 006e
    mova #0x4C314, r4
    mov #zona+128, r15
    mov #0x2, r2
    bitx.a -112(r15), r4
    call #guarda
; bloque 006f
    mova #0x7FFFE, r13
    mova #0xC3804, r5
    mov #0x3, r2
    mova &zona+182, r5
    call #guarda
; bloque 0070
    mov #0x7BA4, r5
    mov #0x4, r2
    rrum.a #4, r5
    call #guarda
; bloque 0071
    mov #zona+128, r10
    mov #0x2, r2
    bis.b #0x2D, -61(r10)
    call #guarda
; bloque 0072
    mov #0x0, r2
    movx.w &zona+134, &zona+0
    call #guarda
; bloque 0073
    mova #0xAAC2F, r11
    mov #0xEABB, r7
    mov #0x0, r2
    rpt #16 { bicx.a r11, r7
    call #guarda
; bloque 0074
    mova #0x7FFFE, r4
    mova #0xB3112, r11
    mov #0x7, r2
    cmpa r4, r11
    call #guarda
; bloque 0075
    mov #zona+128, r7
    mov #0x0, r2
    add.b &zona+50, 28(r7)
    call #guarda
; bloque 0076
    mov #0x0000, r12
    mova #0x80000, r14
    mova #zona+96, r12
    mov #0x0, r2
    mova @r12+, r14
    call #guarda
; bloque 0077
    mova #0x7FFFF, r9
    mov #zona+128, r14
    mov #0x6, r2
    bit.w 64(r14), r9
    call #guarda
; bloque 0078
    mov #0x2000, r4
    mova #0x7FFFF, r9
    mov #0x4, r2
    rpt #7 { xorx.a r4, r9
    call #guarda
; bloque 0079
    mov #0x4, r2
    bic.w zona+138, zona+28
    call #guarda
; bloque 007a
    mova #0x69E98, r9
    mova #0x80000, r4
    mova #zona+122, r9
    mov #0x7, r2
    mova @r9, r4
    call #guarda
; bloque 007b
    mov #zona+128, r4
    mov #0x4, r2
    rrc.w 118(r4)
    call #guarda
; bloque 007c
    mova #0xFFFFF, r5
    mov #zona+4, r10
    mov #0x2, r2
    subcx.w @r10, r5
    call #guarda
; bloque 007d
    mov #0x1, r2
    bit.b #8, &zona+117
    call #guarda
; bloque 007e
    mova #0x9D07C, r5
    mov #0x7, r2
    rram.w #4, r5
    call #guarda
; bloque 007f
    mov #0, r15
    mov #tabla, r13
    mov #0x6, r2
    call @r13
    call #guarda
; bloque 0080
    mov #zona+128, r15
    mova #0xFFFFF, r4
    mov #0x7, r2
    bis.b r4, 26(r15)
    call #guarda
; bloque 0081
    mova #0xCCF52, r13
    mov #0x3, r2
    add.b #0xE3, r13
    call #guarda
; bloque 0082
    mov #0x0, r2
    push.w zona+106
    pop r15
    call #guarda
; bloque 0083
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0084
    mov #0x0047, r15
    mov #0x5, r2
    dadd.b #0x35, r15
    call #guarda
; bloque 0085
    mova #0x317A2, r12
    mova #0x3909E, r8
    mov #0x5, r2
    adda #0x53D71, r8
    call #guarda
; bloque 0086
    mova #0x7FFFE, r4
    mov #0x4, r2
    rlam.a #4, r4
    call #guarda
; bloque 0087
    mova #0xA4551, r11
    mov #zona+71, r5
    mov #0x1, r2
    bisx.b @r5, r11
    call #guarda
; bloque 0088
    mov #zona+104, r12
    mov #0x1, r2
    rra.b @r12
    call #guarda
; bloque 0089
    mov #0x0055, r10
    mov #0x4, r2
    dadd.b #0x18, r10
    call #guarda
; bloque 008a
    mov #0, r15
    mov #0x1, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 008b
    mov #zona+20, r10
    mov #0x3, r2
    xorx.a @r10+, zona+176
    call #guarda
; bloque 008c
    mov #0x0, r2
    rrux.b zona+223
    call #guarda
; bloque 008d
    mov #zona+128, r10
    mov #0x5, r2
    pushx.a 32(r10)
    popx.a r15
    call #guarda
; bloque 008e
    mov #0x4, r2
    bisx.a zona+184, &zona+60
    call #guarda
; bloque 008f
    mov #0x6FF1, r12
    mov #0x1, r2
    cmpx.b #1, r12
    call #guarda
; bloque 0090
    mov #0xF3F3, r15
    mova #0x30DDB, r10
    mov #0x1, r2
    mova r15, r10
    call #guarda
; bloque 0091
    mova #0x80000, r4
    mov #0x2, r2
    rrc.w r4
    call #guarda
; bloque 0092
    mov #0x4, r2
    rrux.w zona+120
    call #guarda
; bloque 0093
    mov #0x7849, r10
    mov #0x7, r2
    daddx.w #0x7890, r10
    call #guarda
; bloque 0094
    mova #0x23896, r12
    mov #zona+214, r11
    mov #0x6, r2
    xorx.a @r11, r12
    call #guarda
; bloque 0095
    mov #zona+128, r5
    mov #zona+136, r14
    mov #0x5, r2
    and.b @r14, 1(r5)
    call #guarda
; bloque 0096
    mov #0x2, r2
    addcx.w &zona+220, &zona+92
    call #guarda
; bloque 0097
    mova #0x28CE8, r5
    mova #0x82257, r12
    mova #zona+128, r12
    mov #0x5, r2
    mova r5, 82(r12)
    call #guarda
; bloque 0098
    mov #0, r15
    mov #0x7, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0099
    mov #zona+140, r10
    mov #0x5, r2
    cmp.w @r10+, &zona+228
    call #guarda
; bloque 009a
    mova #0x7FFFF, r6
    mova #0xAEE9C, r13
    mov #0x1, r2
    adda r6, r13
    call #guarda
; bloque 009b
    mov #0x5, r2
    xorx.b &zona+184, &zona+229
    call #guarda
; bloque 009c
    mova #0x20000, r14
    mov #0x3, r2
    bicx.w &zona+108, r14
    call #guarda
; bloque 009d
    mova #0x94D6C, r10
    mova #0xD9CE2, r5
    mov #0x0, r2
    suba r10, r5
    call #guarda
; bloque 009e
    mov #0x7, r2
    pushx.b &zona+34
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 009f
    mova #0xD1CDD, r13
    mov #0x8F97, r6
    mova #zona+138, r13
    mov #0x4, r2
    mova @r13, r6
    call #guarda
; bloque 00a0
    mov #0x8000, r10
    mova #0x80000, r9
    mov #0x0, r2
    adda #0x7FFFE, r9
    call #guarda
; bloque 00a1
    mova #0x264C8, r9
    mov #0x0000, r8
    mova #0x90FBB, r5
    mov #0x0, r2
    rpt r5 { bisx.b r9, r8
    call #guarda
; bloque 00a2
    mov #zona+128, r6
    mov #0x2, r2
    xorx.b #8, 38(r6)
    call #guarda
; bloque 00a3
    mov #0x4, r2
    bisx.b &zona+161, zona+32
    call #guarda
; bloque 00a4
    mova #0xED5A4, r15
    mov #0x3, r2
    rrum.a #2, r15
    call #guarda
; bloque 00a5
    mova #0x7109A, r11
    mov #0x1, r2
    rram.a #2, r11
    call #guarda
; bloque 00a6
    mova #0x80000, r13
    mov #0x1, r2
    pushx.a r13
    popx.a r15
    call #guarda
; bloque 00a7
    mova #0x80000, r12
    mova #0x7AE20, r13
    mov #0x0, r2
    mova r12, r13
    call #guarda
; bloque 00a8
    mov #zona+128, r6
    mov #0x3, r2
    swpbx 110(r6)
    call #guarda
; bloque 00a9
    mov #0x5, r2
    xor.b #0xFF, &zona+21
    call #guarda
; bloque 00aa
    mov #0, r15
    mov #0x1, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ab
    mov #zona+128, r15
    mov #0x2, r2
    rrc.w 116(r15)
    call #guarda
; bloque 00ac
    mov #0x4356, r4
    mova #0x7A9FF, r7
    mov #0x6, r2
    adda r4, r7
    call #guarda
; bloque 00ad
    mov #0, r15
    mov #0x3, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ae
    mov #0x1B7B, r15
    mova #0x7FFFF, r4
    mov #0x4, r2
    mova r15, r4
    call #guarda
; bloque 00af
    mov #0x0000, r10
    mova #0x7FFFF, r9
    mova #0x2014F, r4
    mov #0x6, r2
    rpt r4 { subcx.w r10, r9
    call #guarda
; bloque 00b0
    mova #0x496F3, r15
    mov #0x0400, r12
    mov #0x5, r2
    adda #0xD2A8F, r12
    call #guarda
; bloque 00b1
    mov #0x0, r2
    rrux.w &zona+106
    call #guarda
; bloque 00b2
    mova #0x88CB0, r6
    mova #0xE5913, r8
    mov #0x4, r2
    suba r6, r8
    call #guarda
; bloque 00b3
    mova #0x20000, r5
    mov #0x0000, r10
    mov #0x3, r2
    rpt #14 { andx.b r5, r10
    call #guarda
; bloque 00b4
    mov #0x2CCE, r4
    mov #0x5, r2
    rram.w #3, r4
    call #guarda
; bloque 00b5
    mov #0x7, r2
    add.b #0x65, &zona+25
    call #guarda
; bloque 00b6
    mova #0x7D5D3, r12
    mov #zona+90, r13
    mov #0x4, r2
    andx.w @r13, r12
    call #guarda
; bloque 00b7
    mova #0x64E69, r12
    mov #0x3, r2
    bic.w &zona+168, r12
    call #guarda
; bloque 00b8
    mov #zona+128, r11
    mov #zona+128, r10
    mov #0x2, r2
    addx.a -116(r10), -70(r11)
    call #guarda
; bloque 00b9
    mova #0x46C35, r12
    mova #0x6C970, r5
    mova #0xFFFFF, r14
    mov #0x2, r2
    rpt r14 { subcx.w r12, r5
    call #guarda
; bloque 00ba
    mov #zona+64, r4
    mov #0x1, r2
    addcx.w @r4+, &zona+82
    call #guarda
; bloque 00bb
    mov #zona+192, r12
    mov #0x7, r2
    bit.w @r12, zona+250
    call #guarda
; bloque 00bc
    mov #0, r15
    mov #0x6, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 00bd
    mov #0, r15
    mov #0x2, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00be
    mov #0x0000, r14
    mov #0x5, r2
    bic.w #0xFFFF, r14
    call #guarda
; bloque 00bf
    mova #0x84069, r13
    mov #0x5, r2
    pushx.b r13
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00c0
    mov #zona+138, r4
    mov #0x7, r2
    bitx.a @r4+, &zona+200
    call #guarda
; bloque 00c1
    mov #zona+206, r8
    mov #0x5, r2
    rrax.a @r8
    call #guarda
; bloque 00c2
    mov #0x0070, r14
    mov #0x0032, r12
    mov #0x2, r2
    dadd.b r12, r14
    call #guarda
; bloque 00c3
    mov #0x3, r2
    rrax.b zona+166
    call #guarda
; bloque 00c4
    mova #0x821C8, r10
    mov #0x4000, r15
    mov #0x0, r2
    adda #0x7FFFF, r15
    call #guarda
; bloque 00c5
    mova #0x65355, r5
    mov #0x1, r2
    and.w r5, &zona+192
    call #guarda
; bloque 00c6
    mov #0x0200, r12
    mov #0x0004, r13
    mov #0x0400, r14
    mov #0x3, r2
    pushm.w #3, r14
    popm.w #3, r12
    call #guarda
; bloque 00c7
    mov #0x3, r2
    rra.w &zona+26
    call #guarda
; bloque 00c8
    mova #0xD0BC7, r14
    mov #0x2, r2
    rrcm.a #2, r14
    call #guarda
; bloque 00c9
    mov #0, r15
    mov #0x4, r2
    calla &tabla_a
    call #guarda
; bloque 00ca
    mova #0xB5AC6, r6
    mova #0xF8796, r9
    mov #0x7, r2
    suba r6, r9
    call #guarda
; bloque 00cb
    mov #0x0000, r8
    mov #0x1, r2
    cmp.b #0x47, r8
    call #guarda
; bloque 00cc
    mova #0xE2D16, r5
    mova #0xF3589, r15
    mov #0x7, r2
    rpt r15 { rrax.b r5
    call #guarda
; bloque 00cd
    mov #0x0001, r8
    mov #0x5, r2
    bis.w #4, r8
    call #guarda
; bloque 00ce
    mova #0x8C667, r4
    mov #0x6, r2
    and.b &zona+204, r4
    call #guarda
; bloque 00cf
    mov #0x9337, r8
    mova #0x7FFFE, r14
    mov #0x6, r2
    mova &zona+234, r14
    call #guarda
; bloque 00d0
    mova #0x30F68, r6
    mov #0x0020, r7
    mova #0xB4A3E, r8
    mova #0x14CE9, r9
    mova #0x7FFFF, r10
    mov #0x7, r2
    pushm.w #5, r10
    popm.w #5, r13
    call #guarda
; bloque 00d1
    mova #0xAE073, r13
    mov #0x0, r2
    rlam.w #3, r13
    call #guarda
; bloque 00d2
    mova #0xBC6E2, r13
    mova #0x5B69F, r11
    mov #0x4, r2
    rpt #15 { addcx.w r13, r11
    call #guarda
; bloque 00d3
    mov #0x0000, r7
    mov #zona+94, r6
    mov #0x3, r2
    xorx.w @r6+, r7
    call #guarda
; bloque 00d4
    mova #0x933DD, r10
    mov #0x1, r2
    rram.a #3, r10
    call #guarda
; bloque 00d5
    mov #zona+128, r15
    mov #0x6, r2
    swpb -122(r15)
    call #guarda
; bloque 00d6
    mov #zona+50, r10
    mov #0x0, r2
    sub.w @r10+, &zona+44
    call #guarda
; bloque 00d7
    mov #0, r15
    mov #0x1, r2
    call #subw
    call #guarda
; bloque 00d8
    mova #0x4BE61, r4
    mov #0x5, r2
    rlam.w #4, r4
    call #guarda
; bloque 00d9
    mov #0x49E3, r13
    mov #0x2, r2
    rrum.w #3, r13
    call #guarda
; bloque 00da
    mov #zona+43, r5
    mov #0x3, r2
    xor.b @r5+, zona+27
    call #guarda
; bloque 00db
    mova #0x25CA8, r14
    mov #zona+60, r9
    mov #0x1, r2
    movx.a @r9+, r14
    call #guarda
; bloque 00dc
    mov #zona+28, r8
    mov #0x5, r2
    sub.b @r8, &zona+134
    call #guarda
; bloque 00dd
    mova #0x7FFFF, r5
    mova #0x3AA6D, r10
    mova #0x7FFFE, r12
    mov #0x1, r2
    rpt r12 { subx.w r5, r10
    call #guarda
; bloque 00de
    mova #0xAEBE6, r5
    mov #0x7, r2
    pushm.w #1, r5
    popm.w #1, r12
    call #guarda
; bloque 00df
    mova #0x4C737, r12
    mov #zona+31, r14
    mov #0x3, r2
    add.b @r14+, r12
    call #guarda
; bloque 00e0
    mova #0x905BA, r12
    mova #0x4B248, r10
    mov #0x0, r2
    subx.a r10, r12
    call #guarda
; bloque 00e1
    mov #0, r15
    mov #0x2, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e2
    mov #0x2, r2
    rrc.w zona+240
    call #guarda
; bloque 00e3
    mova #0x95EA9, r8
    mova #0xFB191, r15
    mov #0x6, r2
    rpt #2 { addcx.w r8, r15
    call #guarda
; bloque 00e4
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e5
    mova #0xA6440, r9
    mov #0x2, r2
    addx.b r9, &zona+231
    call #guarda
; bloque 00e6
    mov #0x2000, r12
    mova #0xBCFE6, r9
    mov #0x7, r2
    adda r12, r9
    call #guarda
; bloque 00e7
    mov #zona+68, r10
    mov #0x2, r2
    rrc.w @r10
    call #guarda
; bloque 00e8
    mov #0, r15
    mov #0x4, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e9
    mov #0x4, r2
    rrux.a &zona+158
    call #guarda
; bloque 00ea
    mov #0x0081, r15
    mov #0x6, r2
    dadd.b #0x16, r15
    call #guarda
; bloque 00eb
    mov #0, r15
    mov #0x4, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ec
    mov #0x4, r2
    rrux.w &zona+146
    call #guarda
; bloque 00ed
    mova #0xC0B05, r8
    mov #0x0000, r6
    mova #zona+128, r8
    mov #0x3, r2
    mova 20(r8), r6
    call #guarda
; bloque 00ee
    mov #0x4, r2
    rrax.b &zona+55
    call #guarda
; bloque 00ef
    mov #zona+160, r9
    mov #0x6, r2
    mov.b @r9, &zona+130
    call #guarda
; bloque 00f0
    mov #0x208C, r5
    mov #0x4A65, r6
    mova #0x5B71D, r7
    mov #0x1, r2
    pushm.a #3, r7
    popm.a #3, r13
    call #guarda
; bloque 00f1
    mov #zona+192, r7
    mov #0x2, r2
    swpb @r7
    call #guarda
; bloque 00f2
    mova #0x37C7D, r6
    mova #0xE6C2D, r9
    mov #0x3, r2
    cmpa r6, r9
    call #guarda
; bloque 00f3
    mov #0, r15
    mov #0x0, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f4
    mova #0x25D24, r5
    mova #0x854A6, r9
    mova #0xFFFFF, r13
    mov #0x6, r2
    rpt r13 { subcx.a r5, r9
    call #guarda
; bloque 00f5
    mov #0x5, r2
    push.b zona+132
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00f6
    mov #zona+128, r13
    mov #0x0200, r7
    mov #0x5, r2
    bicx.a r7, 26(r13)
    call #guarda
; bloque 00f7
    mova #0x7EFEB, r4
    mov #0x0, r2
    pushm.w #1, r4
    popm.w #1, r13
    call #guarda
; bloque 00f8
    mov #zona+128, r8
    mov #zona+190, r12
    mov #0x2, r2
    xorx.a @r12, 72(r8)
    call #guarda
; bloque 00f9
    mova #0x7FFFE, r8
    mov #0x0040, r7
    mov #0x7, r2
    bit.b r7, r8
    call #guarda
; bloque 00fa
    mova #0x86D8A, r12
    mov #0x5, r2
    bis.w r12, &zona+128
    call #guarda
; bloque 00fb
    mov #0, r15
    mova #tabla_a-4, r7
    mov #0x2, r2
    calla 4(r7)
    call #guarda
; bloque 00fc
    mova #0x38E36, r15
    mov #0x0200, r10
    mov #0x2, r2
    mova r15, &zona+104
    call #guarda
; bloque 00fd
    mov #0x2, r2
    rra.w zona+186
    call #guarda
; bloque 00fe
    mova #0xFFFFF, r6
    mov #0x1, r2
    rlam.w #1, r6
    call #guarda
; bloque 00ff
    mova #0x10000, r13
    mov #0x0, r2
    pushm.w #1, r13
    popm.w #1, r11
    call #guarda
; bloque 0100
    mov #zona+216, r7
    mov #0x7, r2
    push.w @r7
    pop r15
    call #guarda
; bloque 0101
    mov #0x0000, r8
    mov #0x7, r2
    rrcm.a #2, r8
    call #guarda
; bloque 0102
    mova #0x9C6DC, r4
    mov #0x5, r2
    rram.a #2, r4
    call #guarda
; bloque 0103
    mov #0x1, r2
    rrax.a zona+76
    call #guarda
; bloque 0104
    mov #0x7, r2
    xor.b #0x15, zona+72
    call #guarda
; bloque 0105
    mov #0xC123, r6
    mova #0x7A1B0, r13
    mov #0x1, r2
    mova r6, r13
    call #guarda
; bloque 0106
    mov #zona+128, r15
    mova #0x9154D, r9
    mov #0x1, r2
    cmp.b r9, -125(r15)
    call #guarda
; bloque 0107
    mova #0x6148E, r4
    mova #0xBF1EB, r15
    mov #0x5, r2
    cmpa r4, r15
    call #guarda
; bloque 0108
    mova #0x80000, r6
    mova #0xB691C, r7
    mova #0xFFFFF, r8
    mov #0x0000, r9
    mov #0x6, r2
    pushm.a #4, r9
    popm.a #4, r10
    call #guarda
; bloque 0109
    mov #zona+128, r5
    mov #zona+172, r11
    mov #0x6, r2
    subx.w @r11, 96(r5)
    call #guarda
; bloque 010a
    mov #0x1722, r8
    mova #0xA93F9, r14
    mova #0xFFFFF, r6
    mov #0x3, r2
    rpt r6 { movx.a r8, r14
    call #guarda
; bloque 010b
    mov #zona+128, r13
    mov #0x0, r2
    rra.w 44(r13)
    call #guarda
; bloque 010c
    mova #0x68797, r4
    mov #0x2, r2
    bisx.a zona+84, r4
    call #guarda
; bloque 010d
    mov #0, r15
    mov #0x4, r2
    call #subw
    call #guarda
; bloque 010e
    mova #0xE6D86, r15
    mova #0xFFFFF, r13
    mova #zona+128, r15
    mov #0x7, r2
    mova -38(r15), r13
    call #guarda
; bloque 010f
    mov #0, r15
    mov #0x1, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0110
    mov #zona+76, r8
    mov #0x6, r2
    swpb @r8
    call #guarda
; bloque 0111
    mova #0x51A2C, r15
    mov #0x5, r2
    rrux.b r15
    call #guarda
; bloque 0112
    mov #zona+126, r5
    mov #0x0, r2
    rrc.w @r5
    call #guarda
; bloque 0113
    mov #0x8E13, r4
    mov #0x5, r2
    rlam.w #4, r4
    call #guarda
; bloque 0114
    mova #0x80000, r13
    mov #0x0, r2
    push.w r13
    pop r15
    call #guarda
; bloque 0115
    mova #0xFFFFF, r4
    mova #0x96493, r13
    mov #0x4, r2
    mova r4, r13
    call #guarda
; bloque 0116
    mova #0x7FFFF, r15
    mova #0x368F0, r7
    mov #0x7, r2
    mova &zona+122, r7
    call #guarda
; bloque 0117
    mova #0x80000, r6
    mova #0xFFFFF, r10
    mova #zona+128, r10
    mov #0x6, r2
    mova r6, 14(r10)
    call #guarda
; bloque 0118
    mov #0x9968, r13
    mov #zona+180, r5
    mov #0x5, r2
    movx.a @r5, r13
    call #guarda
; bloque 0119
    mova #0x45C71, r10
    mov #0x3, r2
    pushm.w #1, r10
    popm.w #1, r7
    call #guarda
; bloque 011a
    mov #0x2, r2
    cmp.w #8, &zona+44
    call #guarda
; bloque 011b
    mova #0xFFFFF, r13
    mov #0x2, r2
    rram.w #4, r13
    call #guarda
; bloque 011c
    mov #0x0, r2
    rrc.b &zona+194
    call #guarda
; bloque 011d
    mova #0x13AB9, r14
    mov #0x2, r2
    cmpx.w #0xD4B2, r14
    call #guarda
; bloque 011e
    mov #zona+128, r10
    mov #zona+2, r4
    mov #0x4, r2
    addx.a @r4+, -96(r10)
    call #guarda
; bloque 011f
    mov #0xD4D2, r9
    mov #0x6, r2
    sub.w #2, r9
    call #guarda
; bloque 0120
    mova #0x4AAFF, r14
    mova #0xC93A6, r7
    mov #0x0, r2
    mova r14, r7
    call #guarda
; bloque 0121
    mov #0x3, r2
    sub.w &zona+150, zona+48
    call #guarda
; bloque 0122
    mov #0x0000, r4
    mova #0xAE5DB, r14
    mova #0xFFFFF, r11
    mov #0x4, r2
    rpt r11 { addcx.w r4, r14
    call #guarda
; bloque 0123
    mov #0, r15
    mov #0x0, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0124
    mov #0x0000, r6
    mov #0x1, r2
    rlam.a #3, r6
    call #guarda
; bloque 0125
    mova #0xE03C6, r6
    mov #zona+38, r13
    mov #0x0, r2
    bisx.a @r13, r6
    call #guarda
; bloque 0126
    mov #0x8574, r15
    mov #0x7963, r10
    mov #0x2, r2
    rpt #5 { daddx.w r15, r10
    call #guarda
; bloque 0127
    mova #0xFFFFF, r11
    mov #0x1, r2
    rrax.a r11
    call #guarda
; bloque 0128
    mov #0x4, r2
    bit.b zona+229, &zona+189
    call #guarda
; bloque 0129
    mov #0, r15
    mov #0x4, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 012a
    mov #0x0073, r6
    mov #0x1804, r13
    mov #0x0, r2
    daddx.w r13, r6
    call #guarda
; bloque 012b
    mov #zona+128, r8
    mov #0x5, r2
    addcx.w -76(r8), zona+200
    call #guarda
; bloque 012c
    mov #zona+128, r11
    mov #0x6, r2
    rrux.b 14(r11)
    call #guarda
; bloque 012d
    mov #zona+128, r6
    mov #0x3, r2
    bisx.a 20(r6), &zona+192
    call #guarda
; bloque 012e
    mova #0xFFFFF, r6
    mov #0x7, r2
    rrum.a #4, r6
    call #guarda
; bloque 012f
    mova #0xEA568, r7
    mov #0x2, r2
    add.b #8, r7
    call #guarda
; bloque 0130
    mov #0, r15
    mov #0x0, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0131
    mov #0, r15
    mov #0x5, r2
    calla &tabla_a
    call #guarda
; bloque 0132
    mov #0xDA0C, r12
    mova #0xDEC46, r8
    mova #0x80000, r15
    mov #0x2, r2
    rpt r15 { xorx.w r12, r8
    call #guarda
; bloque 0133
    mova #0x5CFFC, r15
    mov #zona+88, r7
    mov #0x0, r2
    bitx.b @r7+, r15
    call #guarda
; bloque 0134
    mov #0, r15
    mov #0x1, r2
    call #subw
    call #guarda
; bloque 0135
    mov #0x6, r2
    swpbx &zona+208
    call #guarda
; bloque 0136
    mov #0x0000, r13
    mov #0x4, r2
    rrax.b r13
    call #guarda
; bloque 0137
    mova #0xF9C63, r4
    mov #0x2, r2
    rrum.w #2, r4
    call #guarda
; bloque 0138
    mova #0x1B2B2, r11
    mov #0x2, r2
    addc.w &zona+24, r11
    call #guarda
; bloque 0139
    mova #0x454FF, r9
    mova #0x54487, r5
    mov #0x7, r2
    suba r9, r5
    call #guarda
; bloque 013a
    mov #zona+128, r14
    mova #0x39D3F, r13
    mov #0x1, r2
    cmp.b r13, -91(r14)
    call #guarda
; bloque 013b
    mova #0xBAAAF, r5
    mov #0x0, r2
    rrux.w r5
    call #guarda
; bloque 013c
    mov #zona+128, r13
    mova #0xAA0ED, r15
    mov #0x6, r2
    bit.b r15, -32(r13)
    call #guarda
; bloque 013d
    mov #0, r15
    mova #tabla_a, r5
    mov #0x1, r2
    calla @r5+
    call #guarda
; bloque 013e
    mova #0x6FDB5, r5
    mov #zona+128, r6
    mov #0x5, r2
    bic.w -100(r6), r5
    call #guarda
; bloque 013f
    mova #0x27548, r5
    mov #0x3, r2
    rram.a #3, r5
    call #guarda
; bloque 0140
    mov #0x5, r2
    pushx.a #-1
    popx.a r15
    call #guarda
; bloque 0141
    mov #0x3, r2
    rrax.b &zona+245
    call #guarda
; bloque 0142
    mova #0xFF1C7, r11
    mova #0x7FFFF, r8
    mova #0x24919, r5
    mov #0x0, r2
    rpt r5 { bisx.w r11, r8
    call #guarda
; bloque 0143
    mov #0, r15
    mov #0x6, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0144
    mov #zona+128, r9
    mov #0x0, r2
    bitx.b -28(r9), zona+233
    call #guarda
; bloque 0145
    mov #0x0000, r10
    mova #0xFFFFF, r11
    mova #0x11CDC, r12
    mov #0x2, r2
    pushm.w #3, r12
    popm.w #3, r9
    call #guarda
; bloque 0146
    mova #0xA52D6, r7
    mova #0x27A61, r5
    mov #0x1, r2
    adda r7, r5
    call #guarda
; bloque 0147
    mova #0x547FF, r5
    mova #0x1459D, r14
    mov #0x2, r2
    mova &zona+140, r14
    call #guarda
; bloque 0148
    mov #0x0000, r7
    mov #0x6, r2
    mov.w &zona+62, r7
    call #guarda
; bloque 0149
    mov #0x0000, r10
    mov #zona+156, r12
    mov #0x5, r2
    bisx.a @r12, r10
    call #guarda
; bloque 014a
    mov #zona+128, r15
    mov #0x4, r2
    rrux.w -40(r15)
    call #guarda
; bloque 014b
    mov #zona+128, r10
    mov #0x8000, r5
    mov #0x3, r2
    subcx.b r5, -61(r10)
    call #guarda
; bloque 014c
    mov #0x0004, r9
    mov #0x0400, r15
    mov #0x5, r2
    rpt #10 { subx.w r9, r15
    call #guarda
; bloque 014d
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 014e
    mov #0xF5D8, r8
    mova #0x2D46A, r9
    mova #0x4DFAD, r10
    mova #0x7FFFF, r11
    mova #0xFFFFF, r12
    mov #0x7, r2
    pushm.w #5, r12
    popm.w #5, r12
    call #guarda
; bloque 014f
    mov #0x0000, r11
    mov #0x8000, r9
    mov #0x0, r2
    addx.a r9, r11
    call #guarda
; bloque 0150
    mov #0, r15
    mov #0x1, r2
    calla &tabla_a
    call #guarda
; bloque 0151
    mova #0x5B508, r6
    mov #0x7, r2
    rrax.b r6
    call #guarda
; bloque 0152
    mov #zona+168, r10
    mov #0x5, r2
    pushx.b @r10
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0153
    mov #0x1, r2
    swpbx zona+86
    call #guarda
; bloque 0154
    mova #0xBF6EE, r14
    mov #zona+165, r5
    mov #0x5, r2
    movx.b @r5+, r14
    call #guarda
; bloque 0155
    mov #zona+6, r8
    mov #0x6, r2
    bic.b @r8+, zona+231
    call #guarda
; bloque 0156
    mov #zona+118, r6
    mov #0x0, r2
    subcx.a @r6+, zona+164
    call #guarda
; bloque 0157
    mova #0xFA500, r7
    mov #0x6, r2
    swpbx r7
    call #guarda
; bloque 0158
    mov #0, r15
    mov #0x3, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0159
    mov #zona+128, r8
    mov #0x7, r2
    rra.w 76(r8)
    call #guarda
; bloque 015a
    mov #0x7, r2
    add.w zona+152, zona+102
    call #guarda
; bloque 015b
    mov #zona+128, r10
    mov #0x3, r2
    xorx.w -110(r10), zona+150
    call #guarda
; bloque 015c
    mov #0x1000, r8
    mov #0x3, r2
    rpt #12 { swpbx r8
    call #guarda
; bloque 015d
    mova #0x661C1, r13
    mov #0x6, r2
    rrux.a r13
    call #guarda
; bloque 015e
    mov #0x6, r2
    bisx.w &zona+84, zona+124
    call #guarda
; bloque 015f
    mova #0x3537A, r14
    mova #0x80000, r13
    mov #0x0, r2
    cmpa #0xEE925, r13
    call #guarda
; bloque 0160
    mov #0, r15
    mov #0x5, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0161
    mova #0x20000, r10
    mova #0x46CA3, r13
    mov #0x5, r2
    suba r10, r13
    call #guarda
; bloque 0162
    mova #0x159CD, r11
    mova #0x4F326, r14
    mov #0x4, r2
    cmpa #0xEB206, r14
    call #guarda
; bloque 0163
    mov #zona+128, r4
    mov #0x2, r2
    swpb -64(r4)
    call #guarda
; bloque 0164
    mov #0x1, r2
    swpbx &zona+226
    call #guarda
; bloque 0165
    mov #zona+128, r4
    mov #0x1, r2
    subx.b #0x2, 113(r4)
    call #guarda
; bloque 0166
    mova #0xFFFFF, r7
    mov #0x4, r2
    bisx.w r7, zona+210
    call #guarda
; bloque 0167
    mov #0x3, r2
    rrax.b zona+116
    call #guarda
; bloque 0168
    mova #0x41E5A, r4
    mova #0xFFFFF, r14
    mov #0x2, r2
    subcx.a r14, r4
    call #guarda
; bloque 0169
    mov #0x3885, r5
    mov #0x8587, r6
    mov #0x4, r2
    dadd.w r6, r5
    call #guarda
; bloque 016a
    mov #0x1, r2
    rrux.a &zona+200
    call #guarda
; bloque 016b
    mova #0x9A712, r4
    mov #0x0000, r6
    mov #0x2, r2
    rpt #5 { movx.w r4, r6
    call #guarda
; bloque 016c
    mova #0xBA086, r11
    mova #0x7E4ED, r13
    mova #0x85189, r14
    mov #0x1, r2
    rpt r14 { bisx.w r11, r13
    call #guarda
; bloque 016d
    mov #0x1, r2
    rrux.b zona+225
    call #guarda
; bloque 016e
    mova #0xE64D9, r5
    mov #0x0, r2
    andx.b #0x4F, r5
    call #guarda
; bloque 016f
    mova #0x95BD1, r12
    mov #0x2, r2
    rrc.b r12
    call #guarda
; bloque 0170
    mova #0xC7A79, r13
    mov #0x1, r2
    rrux.b r13
    call #guarda
; bloque 0171
    mov #0, r15
    mov #0x3, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0172
    mova #0x3A6F0, r7
    mova #0x7FFFE, r6
    mov #0x4, r2
    sub.b r6, r7
    call #guarda
; bloque 0173
    mov #0x5, r2
    xorx.b &zona+68, zona+143
    call #guarda
; bloque 0174
    mov #0x0000, r8
    mova #0xBB33D, r9
    mov #0x6, r2
    subc.b r9, r8
    call #guarda
; bloque 0175
    mov #0x0000, r6
    mov #0x2, r2
    addx.b &zona+56, r6
    call #guarda
; bloque 0176
    mov #0x925B, r15
    mov #0x7, r2
    rrum.w #2, r15
    call #guarda
; bloque 0177
    mov #0x0071, r8
    mov #0x0096, r9
    mov #0x0000, r14
    mov #0x6, r2
    rpt r14 { daddx.b r8, r9
    call #guarda
; bloque 0178
    mov #0x0000, r5
    mova #0x7FFFE, r13
    mov #0x6, r2
    cmpa #0x40000, r13
    call #guarda
; bloque 0179
    mov #0x2000, r15
    mov #0x6, r2
    cmp.b zona+28, r15
    call #guarda
; bloque 017a
    mov #0x0200, r5
    mova #0x71B49, r13
    mov #0x0, r2
    rpt r13 { rrax.a r5
    call #guarda
; bloque 017b
    mova #0xABA0C, r11
    mov #0x5, r2
    pushm.w #1, r11
    popm.w #1, r11
    call #guarda
; bloque 017c
    mova #0x1D2E0, r13
    mov #0x0004, r6
    mova #zona+128, r13
    mov #0x2, r2
    mova -114(r13), r6
    call #guarda
; bloque 017d
    mova #0x7FFFE, r13
    mova #0x8156B, r9
    mov #0x4, r2
    suba #0x268F4, r9
    call #guarda
; bloque 017e
    mova #0x10000, r10
    mov #0x0, r2
    andx.a r10, r10
    call #guarda
; bloque 017f
    mov #0x8560, r15
    mov #0x3123, r10
    mov #0x0, r2
    dadd.w r10, r15
    call #guarda
; bloque 0180
    mov #zona+128, r8
    mov #0x3, r2
    xorx.w -92(r8), zona+100
    call #guarda
; bloque 0181
    mova #0x10000, r15
    mov #0x7, r2
    swpbx r15
    call #guarda
; bloque 0182
    mov #0, r15
    mov #0x0, r2
    calla #sub
    call #guarda
; bloque 0183
    mov #zona+36, r4
    mov #0x5, r2
    swpb @r4
    call #guarda
; bloque 0184
    mova #0xE56C4, r11
    mov #0x1, r2
    rrum.w #2, r11
    call #guarda
; bloque 0185
    mova #0x52ABA, r9
    mov #0x0, r2
    andx.b #0xFF, r9
    call #guarda
; bloque 0186
    mova #0xAE0E8, r11
    mov #0x7, r2
    addc.w r11, zona+48
    call #guarda
; bloque 0187
    mov #0x0, r2
    sub.w zona+142, &zona+250
    call #guarda
; bloque 0188
    mov #0x0000, r9
    mov #zona+40, r10
    mov #0x5, r2
    addc.b @r10+, r9
    call #guarda
; bloque 0189
    mova #0x41822, r13
    mov #0x0010, r7
    mov #0x6, r2
    bitx.a r7, r13
    call #guarda
; bloque 018a
    mova #0xFFFFF, r5
    mov #0x241E, r15
    mov #0x4, r2
    adda r5, r15
    call #guarda
; bloque 018b
    mova #0x34C3E, r9
    mov #0x0004, r5
    mov #0x5, r2
    adda r9, r5
    call #guarda
; bloque 018c
    mova #0x27436, r5
    mova #0x7FFFF, r6
    mova #0xFFFFF, r7
    mova #0x11707, r8
    mova #0x7FFFF, r9
    mov #0x1, r2
    pushm.w #5, r9
    popm.w #5, r11
    call #guarda
; bloque 018d
    mova #0xBFF93, r8
    mov #0x0000, r11
    mova #0x8405F, r7
    mov #0x7, r2
    rpt r7 { bisx.a r8, r11
    call #guarda
; bloque 018e
    mova #0xFCAF0, r7
    mov #0x0080, r14
    mov #0x3, r2
    suba #0xAAD6E, r14
    call #guarda
; bloque 018f
    mov #zona+128, r8
    mov #0x0000, r14
    mov #0x7, r2
    add.w r14, -68(r8)
    call #guarda
; bloque 0190
    mova #0x7EE5C, r5
    mov #0x7, r2
    rrux.w r5
    call #guarda
; bloque 0191
    mov #0x4, r2
    pushx.b &zona+196
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0192
    mov #0, r15
    mov #0x5, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0193
    mova #0x46212, r7
    mova #0x75725, r14
    mov #0x5, r2
    mov.b r14, r7
    call #guarda
; bloque 0194
    mov #0, r15
    mov #0x2, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0195
    mov #zona+128, r9
    mov #0x0, r2
    sub.b 91(r9), zona+202
    call #guarda
; bloque 0196
    mova #0xFFFFF, r15
    mov #0x6, r2
    xor.b #8, r15
    call #guarda
; bloque 0197
    mov #zona+128, r10
    mov #0x0, r2
    and.b &zona+145, -81(r10)
    call #guarda
; bloque 0198
    mov #zona+128, r15
    mov #zona+214, r8
    mov #0x0, r2
    cmp.w @r8, -102(r15)
    call #guarda
; bloque 0199
    mov #0xB90A, r8
    mova #0xFFFFF, r11
    mov #0x3, r2
    suba #0x7FFFE, r11
    call #guarda
; bloque 019a
    mova #0x86D62, r9
    mov #0x6, r2
    rrc.b r9
    call #guarda
; bloque 019b
    mov #0x1, r2
    swpb &zona+224
    call #guarda
; bloque 019c
    mov #zona+128, r11
    mov #zona+128, r7
    mov #0x7, r2
    add.b -124(r7), -112(r11)
    call #guarda
; bloque 019d
    mov #0x0008, r14
    mova #0x1BEE8, r12
    mova #zona+128, r14
    mov #0x0, r2
    mova 56(r14), r12
    call #guarda
; bloque 019e
    mov #0x1, r2
    rrux.w zona+142
    call #guarda
; bloque 019f
    mova #0xCFD66, r8
    mova #0x7FFFE, r14
    mov #0x6, r2
    cmpa #0x49523, r14
    call #guarda
; bloque 01a0
    mova #0x98ED6, r4
    mova #0xD53A3, r5
    mov #0x0, r2
    rpt #11 { bisx.w r4, r5
    call #guarda
; bloque 01a1
    mov #zona+202, r9
    mov #0x2, r2
    rrax.w @r9
    call #guarda
; bloque 01a2
    mov #0x0000, r12
    mov #zona+22, r13
    mov #0x0, r2
    subcx.a @r13, r12
    call #guarda
; bloque 01a3
    mov #0x0000, r12
    mov #0x0200, r14
    mov #0x3, r2
    cmpa #0x08AD9, r14
    call #guarda
; bloque 01a4
    mova #0x3EE0C, r5
    mov #zona+128, r4
    mov #0x5, r2
    mov.b -5(r4), r5
    call #guarda
; bloque 01a5
    mova #0xB6D9D, r8
    mov #zona+128, r14
    mov #0x6, r2
    mov.b 122(r14), r8
    call #guarda
; bloque 01a6
    mova #0xFFFFF, r13
    mov #0x6, r2
    movx.w #0xCFD7, r13
    call #guarda
; bloque 01a7
    mov #0x0000, r9
    mov #0x3, r2
    rrcm.w #2, r9
    call #guarda
; bloque 01a8
    mova #0xEA889, r15
    mov #0x6, r2
    rlam.w #1, r15
    call #guarda
; bloque 01a9
    mova #0xA6831, r4
    mov #0x7, r2
    rrum.w #3, r4
    call #guarda
; bloque 01aa
    mov #zona+128, r15
    mov #0x6, r2
    add.b &zona+164, 112(r15)
    call #guarda
; bloque 01ab
    mova #0x3BA04, r6
    mov #0x262E, r14
    mov #0x6, r2
    cmpa r6, r14
    call #guarda
; bloque 01ac
    mov #zona+128, r5
    mov #zona+222, r7
    mov #0x5, r2
    bit.w @r7+, -30(r5)
    call #guarda
; bloque 01ad
    mov #0x8706, r6
    mov #0x0, r2
    daddx.w #8, r6
    call #guarda
; bloque 01ae
    mov #zona+128, r15
    mov #0x6, r2
    rrcx.b -128(r15)
    call #guarda
; bloque 01af
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 01b0
    mov #0x1, r2
    rrux.a &zona+206
    call #guarda
; bloque 01b1
    mova #0x21C99, r8
    mova #0x77277, r13
    mov #0x1, r2
    cmpa r8, r13
    call #guarda
; bloque 01b2
    mov #0x7, r2
    swpbx &zona+116
    call #guarda
; bloque 01b3
    mov #0, r15
    mova #tabla_a-4, r14
    mov #0x7, r2
    calla 4(r14)
    call #guarda
; bloque 01b4
    mova #0xDED3D, r11
    mov #0x1000, r12
    mov #0x0004, r13
    mova #0xDAEF3, r14
    mova #0x80000, r15
    mov #0x7, r2
    pushm.a #5, r15
    popm.a #5, r14
    call #guarda
; bloque 01b5
    mova #0x7FFFE, r11
    mov #0x4, r2
    bitx.w r11, &zona+230
    call #guarda
; bloque 01b6
    mova #0xE20F6, r8
    mov #0x4, r2
    and.b zona+40, r8
    call #guarda
; bloque 01b7
    mov #0x0000, r11
    mov #0x6, r2
    swpbx r11
    call #guarda
; bloque 01b8
    mov #0x4, r2
    swpbx zona+112
    call #guarda
; bloque 01b9
    mova #0x348CD, r6
    mov #0x0040, r5
    mov #0x1, r2
    mova r6, r5
    call #guarda
; bloque 01ba
    mova #0x7FFFE, r13
    mov #0x0, r2
    rrc.w r13
    call #guarda
; bloque 01bb
    mova #0x397D0, r14
    mov #0x1, r2
    andx.w r14, zona+134
    call #guarda
; bloque 01bc
    mova #0x7FFFF, r10
    mova #0x8E940, r8
    mov #0x2, r2
    mov.w r8, r10
    call #guarda
; bloque 01bd
    mov #zona+128, r5
    mova #0xBC409, r7
    mov #0x6, r2
    cmpx.b r7, 30(r5)
    call #guarda
; bloque 01be
    mova #0x7FFFE, r7
    mov #0x1, r2
    subc.b r7, &zona+109
    call #guarda
; bloque 01bf
    mov #0x0000, r8
    mov #0x1, r2
    pushm.a #1, r8
    popm.a #1, r7
    call #guarda
; bloque 01c0
    mov #0x7, r2
    bicx.w #0x831F, &zona+60
    call #guarda
; bloque 01c1
    mov #0x0000, r12
    mov #zona+112, r13
    mov #0x6, r2
    mov.w @r13, r12
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
