    .text
    .global prueba
    .global fin
prueba:
    pushm.a #7, r10
    mov r1, &sp_guardado
    movx.a #res, &resptr
; bloque 0000
    mova #0x5E567, r4
    mova #0xB0F85, r13
    mov #0x0002, r6
    mov #0x1, r2
    rpt r6 { subx.a r4, r13
    call #guarda
; bloque 0001
    mov #0xAEA4, r15
    mova #0x9F119, r4
    mova #zona+128, r4
    mov #0x1, r2
    mova r15, -22(r4)
    call #guarda
; bloque 0002
    mov #0x9542, r15
    mov #0x6482, r9
    mov #0x6, r2
    rpt #9 { daddx.w r15, r9
    call #guarda
; bloque 0003
    mov #0x2, r2
    rrcx.w zona+72
    call #guarda
; bloque 0004
    mova #0x983A2, r9
    mova #0xDDF00, r10
    mova #0xDACC9, r11
    mov #0x0000, r12
    mova #0xB61E4, r13
    mov #0x5, r2
    pushm.w #5, r13
    popm.w #5, r14
    call #guarda
; bloque 0005
    mov #0x0000, r10
    mov #0x7, r2
    rram.a #4, r10
    call #guarda
; bloque 0006
    mova #0x672BF, r5
    mov #0x6, r2
    cmp.w zona+166, r5
    call #guarda
; bloque 0007
    mova #0x45695, r5
    mov #0x0, r2
    rrum.a #4, r5
    call #guarda
; bloque 0008
    mov #0x0008, r13
    mov #0x1, r2
    rrc.b r13
    call #guarda
; bloque 0009
    mov #0x0000, r4
    mov #0x0, r2
    addc.b #0xFF, r4
    call #guarda
; bloque 000a
    mova #0x6A596, r13
    mov #0x1, r2
    rrcx.w r13
    call #guarda
; bloque 000b
    mova #0xC3CE0, r7
    mova #0xD6B90, r4
    mov #0x7, r2
    subcx.b r4, r7
    call #guarda
; bloque 000c
    mov #zona+128, r4
    mov #zona+126, r7
    mov #0x6, r2
    add.w @r7, -114(r4)
    call #guarda
; bloque 000d
    mov #zona+110, r6
    mov #0x3, r2
    movx.w @r6+, zona+136
    call #guarda
; bloque 000e
    mova #0x59D8E, r10
    mova #0xB9612, r13
    mov #0x4, r2
    cmpa r10, r13
    call #guarda
; bloque 000f
    mov #0x6, r2
    addcx.b zona+34, zona+209
    call #guarda
; bloque 0010
    mova #0x2BDED, r12
    mov #0x0000, r13
    mova #0x80000, r14
    mov #0x0000, r15
    mov #0x4, r2
    pushm.a #4, r15
    popm.a #4, r10
    call #guarda
; bloque 0011
    mova #0x21585, r12
    mova #0x5B527, r5
    mov #0x7, r2
    bisx.w r5, r12
    call #guarda
; bloque 0012
    mova #0xA4866, r4
    mov #0x1BE0, r5
    mov #0x0002, r6
    mov #0x3, r2
    pushm.w #3, r6
    popm.w #3, r9
    call #guarda
; bloque 0013
    mova #0x51D37, r11
    mova #0xD466F, r9
    mov #0x2, r2
    rpt #5 { addcx.a r11, r9
    call #guarda
; bloque 0014
    mova #0x6F687, r11
    mov #0x1, r2
    pushm.w #1, r11
    popm.w #1, r8
    call #guarda
; bloque 0015
    mov #zona+28, r12
    mov #0x1, r2
    pushx.a @r12
    popx.a r15
    call #guarda
; bloque 0016
    mov #0x1, r2
    cmpx.a zona+136, zona+160
    call #guarda
; bloque 0017
    mov #0, r15
    mova #tabla_a, r7
    mov #0x7, r2
    calla @r7
    call #guarda
; bloque 0018
    mov #zona+202, r14
    mov #0x3, r2
    bit.w @r14+, &zona+120
    call #guarda
; bloque 0019
    mov #0x0, r2
    sub.w #8, zona+114
    call #guarda
; bloque 001a
    mov #zona+128, r14
    mov #0x0, r2
    pushx.a -72(r14)
    popx.a r15
    call #guarda
; bloque 001b
    mov #0, r15
    mov #0x6, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 001c
    mova #0x41935, r9
    mov #0x2, r2
    rrcm.a #3, r9
    call #guarda
; bloque 001d
    mov #0, r15
    mov #subw, r6
    mov #0x0, r2
    call r6
    call #guarda
; bloque 001e
    mov #0x3, r2
    rrcx.b &zona+66
    call #guarda
; bloque 001f
    mov #zona+128, r5
    mova #0xFFFFF, r11
    mov #0x0, r2
    bit.w r11, -34(r5)
    call #guarda
; bloque 0020
    mova #0xA4E12, r11
    mov #0x0020, r9
    mov #0x6, r2
    cmpa #0x19DAE, r9
    call #guarda
; bloque 0021
    mov #0x5, r2
    push.b #0x30
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0022
    mov #zona+128, r10
    mov #zona+130, r5
    mov #0x0, r2
    cmp.b @r5, 109(r10)
    call #guarda
; bloque 0023
    mova #0xF628A, r9
    mov #zona+128, r7
    mov #0x1, r2
    movx.w -30(r7), r9
    call #guarda
; bloque 0024
    mova #0x80000, r7
    mov #0x7, r2
    sub.w r7, zona+208
    call #guarda
; bloque 0025
    mov #zona+128, r13
    mov #0x7, r2
    subx.b zona+169, -50(r13)
    call #guarda
; bloque 0026
    mov #zona+128, r5
    mov #0x0, r2
    rrax.w -90(r5)
    call #guarda
; bloque 0027
    mov #0x7, r2
    mov.b &zona+130, &zona+226
    call #guarda
; bloque 0028
    mov #zona+128, r5
    mov #0x7, r2
    mov.w &zona+162, -72(r5)
    call #guarda
; bloque 0029
    mova #0xFFFFF, r9
    mov #zona+128, r5
    mov #0x0, r2
    xor.b -94(r5), r9
    call #guarda
; bloque 002a
    mova #0x9C16D, r15
    mova #0x7251F, r8
    mov #0x6, r2
    mova r15, &zona+50
    call #guarda
; bloque 002b
    mov #0x0008, r13
    mov #0x5, r2
    add.w r13, &zona+124
    call #guarda
; bloque 002c
    mova #0xED8B8, r14
    mova #0x45C9A, r13
    mov #0x7, r2
    suba #0x7A9AF, r13
    call #guarda
; bloque 002d
    mov #0x6, r2
    bicx.b &zona+59, &zona+93
    call #guarda
; bloque 002e
    mov #0xF57D, r5
    mova #0x7FFFE, r13
    mov #0x1, r2
    rpt #4 { addx.b r5, r13
    call #guarda
; bloque 002f
    mova #0x46EE6, r7
    mov #zona+128, r9
    mov #0x1, r2
    bit.b -104(r9), r7
    call #guarda
; bloque 0030
    mov #zona+128, r9
    mov #0x7, r2
    sub.w #0x4, 68(r9)
    call #guarda
; bloque 0031
    mova #0x785A4, r7
    mov #0x7, r2
    bic.w zona+180, r7
    call #guarda
; bloque 0032
    mova #0xE7C70, r12
    mov #0x7, r2
    rrum.w #4, r12
    call #guarda
; bloque 0033
    mov #zona+128, r13
    mov #0x6, r2
    sub.b &zona+217, -15(r13)
    call #guarda
; bloque 0034
    mova #0x9DFF8, r11
    mova #0x37AAC, r5
    mov #0x4, r2
    mova r11, &zona+92
    call #guarda
; bloque 0035
    mova #0x8F40D, r4
    mov #0x53B0, r11
    mov #0x7, r2
    suba r4, r11
    call #guarda
; bloque 0036
    mov #zona+128, r9
    mov #0x5, r2
    subcx.a &zona+22, -116(r9)
    call #guarda
; bloque 0037
    mov #0, r15
    mov #0x0, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0038
    mov #0, r15
    mov #0x2, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0039
    mov #zona+44, r12
    mov #0x4, r2
    add.w @r12+, &zona+132
    call #guarda
; bloque 003a
    mova #0xC3B12, r12
    mov #0x1, r2
    subx.w #1, r12
    call #guarda
; bloque 003b
    mova #0x42977, r14
    mova #0x7FFFE, r10
    mov #0x4, r2
    subcx.w r10, r14
    call #guarda
; bloque 003c
    mov #0x0000, r8
    mov #0x2, r2
    rrux.w r8
    call #guarda
; bloque 003d
    mov #0, r15
    mov #0x5, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 003e
    mova #0x8BEAB, r5
    mova #0x20D58, r6
    mov #0x6, r2
    rpt #5 { xorx.w r5, r6
    call #guarda
; bloque 003f
    mov #0x1, r2
    movx.b &zona+178, zona+187
    call #guarda
; bloque 0040
    mov #0, r15
    mov #0x2, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0041
    mov #0, r15
    mov #0x4, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0042
    mov #0x3, r2
    rrux.w zona+178
    call #guarda
; bloque 0043
    mov #0x0, r2
    bit.b #0x10, &zona+114
    call #guarda
; bloque 0044
    mova #0xDE929, r11
    mova #0x7FFFF, r5
    mov #0x2, r2
    rpt #1 { subx.b r11, r5
    call #guarda
; bloque 0045
    mova #0xCEE85, r9
    mova #0x6BE59, r11
    mov #0x6, r2
    rpt r11 { rrax.b r9
    call #guarda
; bloque 0046
    mov #0x0000, r9
    mov #0x4, r2
    rra.b r9
    call #guarda
; bloque 0047
    mov #0x2000, r10
    mov #0x6, r2
    xor.w &zona+88, r10
    call #guarda
; bloque 0048
    mov #zona+192, r12
    mov #0x0, r2
    bic.w @r12, &zona+64
    call #guarda
; bloque 0049
    mov #0, r15
    mov #0x5, r2
    calla &tabla_a
    call #guarda
; bloque 004a
    mov #0x4, r2
    addx.b zona+12, &zona+52
    call #guarda
; bloque 004b
    mov #0x0000, r9
    mov #0x2, r2
    rrcx.w r9
    call #guarda
; bloque 004c
    mova #0x4F700, r13
    mova #0x835F9, r10
    mov #0x1, r2
    mova &zona+198, r10
    call #guarda
; bloque 004d
    mova #0x5B76B, r7
    mova #0x4FDCF, r8
    mov #0x6, r2
    pushm.a #2, r8
    popm.a #2, r11
    call #guarda
; bloque 004e
    mova #0x80000, r11
    mov #0x0002, r6
    mov #0x5, r2
    bic.w r6, r11
    call #guarda
; bloque 004f
    mov #0x0000, r7
    mov #0x4, r2
    push.w r7
    pop r15
    call #guarda
; bloque 0050
    mov #0x0000, r6
    mova #0xB25EE, r7
    mov #0x1, r2
    pushm.a #2, r7
    popm.a #2, r9
    call #guarda
; bloque 0051
    mov #0x6, r2
    push.b zona+86
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0052
    mov #0x0000, r7
    mov #zona+130, r9
    mov #0x4, r2
    cmpx.a @r9, r7
    call #guarda
; bloque 0053
    mov #zona+128, r9
    mov #0x0008, r11
    mov #0x0, r2
    add.b r11, -13(r9)
    call #guarda
; bloque 0054
    mova #0x51037, r6
    mov #zona+134, r5
    mov #0x6, r2
    xor.b @r5, r6
    call #guarda
; bloque 0055
    mova #0x28B85, r6
    mov #0x0000, r13
    mova #zona+128, r6
    mov #0x1, r2
    mova -124(r6), r13
    call #guarda
; bloque 0056
    mov #0x0000, r5
    mov #0x0000, r12
    mov #0x3, r2
    mova r5, r12
    call #guarda
; bloque 0057
    mova #0xBFC2D, r11
    mov #0x5, r2
    bit.w zona+178, r11
    call #guarda
; bloque 0058
    mova #0x7FFFF, r11
    mov #0x5, r2
    xorx.a &zona+250, r11
    call #guarda
; bloque 0059
    mov #zona+189, r11
    mov #0x5, r2
    bitx.b @r11+, &zona+107
    call #guarda
; bloque 005a
    mova #0x1CE53, r10
    mov #0x7, r2
    swpbx r10
    call #guarda
; bloque 005b
    mova #0xFFFFF, r14
    mova #0xED08C, r4
    mov #0x0, r2
    adda r14, r4
    call #guarda
; bloque 005c
    mov #0x6, r2
    andx.w #0, zona+160
    call #guarda
; bloque 005d
    mova #0xDC093, r6
    mov #0x0000, r7
    mov #0x4, r2
    bicx.a r7, r6
    call #guarda
; bloque 005e
    mova #0xD8055, r7
    mov #0x0, r2
    rrcm.w #3, r7
    call #guarda
; bloque 005f
    mova #0xE73D9, r12
    mova #0x80000, r11
    mov #0x2, r2
    mova r12, r11
    call #guarda
; bloque 0060
    mov #zona+128, r12
    mov #0x6, r2
    mov.b 52(r12), zona+37
    call #guarda
; bloque 0061
    mov #zona+128, r7
    mov #zona+112, r6
    mov #0x1, r2
    cmpx.a @r6+, -6(r7)
    call #guarda
; bloque 0062
    mova #0x8FA85, r6
    mov #0x7, r2
    rram.w #1, r6
    call #guarda
; bloque 0063
    mov #0, r15
    mova #sub, r4
    mov #0x2, r2
    calla r4
    call #guarda
; bloque 0064
    mov #0x0000, r14
    mova #0xFFFFF, r7
    mova #zona+128, r14
    mov #0x3, r2
    mova 74(r14), r7
    call #guarda
; bloque 0065
    mova #0x89CDA, r4
    mov #0x8000, r5
    mova #0x3AEF9, r6
    mova #0x44A17, r7
    mov #0x7, r2
    pushm.w #4, r7
    popm.w #4, r15
    call #guarda
; bloque 0066
    mov #0x7, r2
    movx.a zona+108, &zona+102
    call #guarda
; bloque 0067
    mov #0x0028, r11
    mov #0x3, r2
    daddx.b #0x76, r11
    call #guarda
; bloque 0068
    mov #0x2, r2
    rrcx.b zona+96
    call #guarda
; bloque 0069
    mova #0xF2EC4, r14
    mov #0x1, r2
    swpb r14
    call #guarda
; bloque 006a
    mov #0, r15
    mov #0x0, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 006b
    mov #zona+182, r10
    mov #0x2, r2
    movx.b @r10+, zona+182
    call #guarda
; bloque 006c
    mov #0x0040, r15
    mov #0x7, r2
    daddx.b #0x5, r15
    call #guarda
; bloque 006d
    mova #0xAB81F, r10
    mov #0x5, r2
    subc.b #0x56, r10
    call #guarda
; bloque 006e
    mov #0x5, r2
    bisx.w #8, zona+0
    call #guarda
; bloque 006f
    mov #zona+128, r9
    mov #zona+63, r13
    mov #0x7, r2
    bisx.b @r13+, -103(r9)
    call #guarda
; bloque 0070
    mova #0x7FFFE, r8
    mov #zona+104, r14
    mov #0x0, r2
    addcx.a @r14+, r8
    call #guarda
; bloque 0071
    mov #0, r15
    mov #0x5, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0072
    mov #0x0000, r4
    mov #0x373A, r14
    mov #0x0, r2
    rpt #15 { bicx.a r4, r14
    call #guarda
; bloque 0073
    mova #0x101E4, r7
    mov #0x4, r2
    and.w #4, r7
    call #guarda
; bloque 0074
    mov #0x0000, r9
    mov #0x3, r2
    pushm.a #1, r9
    popm.a #1, r13
    call #guarda
; bloque 0075
    mov #0x0020, r12
    mov #0x0, r2
    rrc.w r12
    call #guarda
; bloque 0076
    mova #0x551A2, r14
    mov #0x0, r2
    swpb r14
    call #guarda
; bloque 0077
    mova #0x8341B, r11
    mov #0x3, r2
    bit.b r11, zona+127
    call #guarda
; bloque 0078
    mov #zona+134, r14
    mov #0x5, r2
    rrux.w @r14
    call #guarda
; bloque 0079
    mova #0xFFFFF, r15
    mov #0x7, r2
    add.w r15, zona+104
    call #guarda
; bloque 007a
    mova #0xE8122, r11
    mov #zona+128, r12
    mov #0x6, r2
    andx.a -36(r12), r11
    call #guarda
; bloque 007b
    mova #0x969DF, r12
    mov #0x7, r2
    rlam.a #4, r12
    call #guarda
; bloque 007c
    mova #0xDDBE2, r10
    mova #0xFFFFF, r7
    mov #0x1, r2
    mova &zona+104, r7
    call #guarda
; bloque 007d
    mov #0x5, r2
    bit.b zona+177, zona+73
    call #guarda
; bloque 007e
    mov #zona+196, r5
    mov #0x7, r2
    bitx.w @r5, zona+44
    call #guarda
; bloque 007f
    mov #0x2BFF, r9
    mov #0x6, r2
    sub.w r9, &zona+190
    call #guarda
; bloque 0080
    mova #0xFAE3D, r13
    mova #0x8B111, r11
    mova #zona+128, r13
    mov #0x4, r2
    mova 98(r13), r11
    call #guarda
; bloque 0081
    mov #0, r15
    mov #0x0, r2
    call #subw
    call #guarda
; bloque 0082
    mov #0, r15
    mov #tabla, r14
    mov #0x7, r2
    call @r14
    call #guarda
; bloque 0083
    mov #zona+90, r4
    mov #0x2, r2
    andx.b @r4+, zona+11
    call #guarda
; bloque 0084
    mova #0x7ADB8, r13
    mov #zona+148, r14
    mov #0x3, r2
    bitx.b @r14, r13
    call #guarda
; bloque 0085
    mov #zona+208, r11
    mov #0x3, r2
    sub.w @r11+, &zona+164
    call #guarda
; bloque 0086
    mova #0x80000, r12
    mov #0x6, r2
    rrax.b r12
    call #guarda
; bloque 0087
    mov #zona+17, r7
    mov #0x3, r2
    bit.b @r7, &zona+193
    call #guarda
; bloque 0088
    mova #0xC9DC8, r14
    mov #0x0, r2
    rra.w r14
    call #guarda
; bloque 0089
    mova #0x155FA, r12
    mov #0x4, r2
    rrcm.a #4, r12
    call #guarda
; bloque 008a
    mov #0x1, r2
    rrcx.b zona+151
    call #guarda
; bloque 008b
    mova #0x881D7, r13
    mov #0x5, r2
    addcx.b r13, &zona+94
    call #guarda
; bloque 008c
    mov #0x79ED, r8
    mov #0x6, r2
    andx.b &zona+51, r8
    call #guarda
; bloque 008d
    mov #zona+68, r13
    mov #0x3, r2
    rrux.w @r13
    call #guarda
; bloque 008e
    mov #0, r15
    mov #tabla, r13
    mov #0x1, r2
    call @r13
    call #guarda
; bloque 008f
    mov #zona+128, r11
    mov #zona+128, r5
    mov #0x1, r2
    bic.b 46(r5), -42(r11)
    call #guarda
; bloque 0090
    mova #0x42166, r14
    mova #0x28555, r13
    mova #0x4C62F, r7
    mov #0x7, r2
    rpt r7 { xorx.w r14, r13
    call #guarda
; bloque 0091
    mov #0x0000, r13
    mova #0xFFFFF, r12
    mova #zona+132, r13
    mov #0x5, r2
    mova @r13+, r12
    call #guarda
; bloque 0092
    mova #0x35FEF, r4
    mov #0x4, r2
    rpt #8 { rrux.a r4
    call #guarda
; bloque 0093
    mov #0x4908, r7
    mov #0x1, r2
    dadd.w #0x9708, r7
    call #guarda
; bloque 0094
    mova #0xF9802, r15
    mov #0x5, r2
    rrcm.a #1, r15
    call #guarda
; bloque 0095
    mov #0x4, r2
    rra.w zona+94
    call #guarda
; bloque 0096
    mov #0x4, r2
    rra.b zona+8
    call #guarda
; bloque 0097
    mova #0x5C776, r4
    mova #0xE726B, r8
    mova #zona+210, r4
    mov #0x7, r2
    mova @r4+, r8
    call #guarda
; bloque 0098
    mova #0x7533F, r14
    mov #0x5, r2
    rrum.w #1, r14
    call #guarda
; bloque 0099
    mova #0x800FA, r11
    mova #0xFFFFF, r8
    mova #0xFFFFF, r6
    mov #0x2, r2
    rpt r6 { bisx.b r11, r8
    call #guarda
; bloque 009a
    mov #zona+56, r9
    mov #0x1, r2
    movx.b @r9+, &zona+12
    call #guarda
; bloque 009b
    mov #zona+107, r11
    mov #0x6, r2
    xorx.b @r11, zona+36
    call #guarda
; bloque 009c
    mova #0xFFFFF, r15
    mov #zona+128, r10
    mov #0x3, r2
    addx.a -56(r10), r15
    call #guarda
; bloque 009d
    mov #0x0000, r14
    mov #0x0000, r4
    mov #0x0008, r7
    mov #0x1, r2
    rpt r7 { xorx.a r14, r4
    call #guarda
; bloque 009e
    mov #0, r15
    mov #0x3, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 009f
    mov #0x0096, r12
    mov #0x0046, r5
    mov #0x6, r2
    rpt #4 { daddx.b r12, r5
    call #guarda
; bloque 00a0
    mova #0x6859F, r14
    mova #0x1254D, r15
    mov #0x6, r2
    pushm.a #2, r15
    popm.a #2, r10
    call #guarda
; bloque 00a1
    mov #0, r15
    mov #0x2, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 00a2
    mova #0xFFFFF, r4
    mov #0x7, r2
    rlam.w #4, r4
    call #guarda
; bloque 00a3
    mov #0x3, r2
    and.b &zona+216, &zona+167
    call #guarda
; bloque 00a4
    mov #0, r15
    mova #sub, r14
    mov #0x0, r2
    calla r14
    call #guarda
; bloque 00a5
    mova #0xF6FCF, r15
    mov #0x7, r2
    addx.b &zona+218, r15
    call #guarda
; bloque 00a6
    mov #0x5, r2
    rrc.b &zona+197
    call #guarda
; bloque 00a7
    mov #zona+128, r5
    mov #0x3, r2
    bisx.a #0x73861, -42(r5)
    call #guarda
; bloque 00a8
    mov #zona+128, r8
    mova #0xEEDA3, r15
    mov #0x0, r2
    subcx.w r15, 96(r8)
    call #guarda
; bloque 00a9
    mov #zona+128, r7
    mov #zona+248, r4
    mov #0x7, r2
    andx.w @r4, 106(r7)
    call #guarda
; bloque 00aa
    mov #0x7DDA, r10
    mov #0x0000, r4
    mov #0x3, r2
    cmp.w r4, r10
    call #guarda
; bloque 00ab
    mov #zona+128, r6
    mov #zona+44, r8
    mov #0x4, r2
    bisx.a @r8+, 4(r6)
    call #guarda
; bloque 00ac
    mova #0x89F97, r7
    mov #0x2, r2
    swpb r7
    call #guarda
; bloque 00ad
    mova #0x80000, r11
    mov #0x2, r2
    bisx.w #0x56AA, r11
    call #guarda
; bloque 00ae
    mov #0x2, r2
    push.w &zona+182
    pop r15
    call #guarda
; bloque 00af
    mova #0x73041, r13
    mova #0xB4936, r14
    mova #0xF027B, r15
    mov #0x6, r2
    pushm.w #3, r15
    popm.w #3, r7
    call #guarda
; bloque 00b0
    mov #0x2, r2
    addcx.b #1, zona+99
    call #guarda
; bloque 00b1
    mov #0, r15
    mov #tabla, r4
    mov #0x1, r2
    call @r4
    call #guarda
; bloque 00b2
    mov #0x2, r2
    bis.b &zona+173, zona+127
    call #guarda
; bloque 00b3
    mov #zona+128, r15
    mov #zona+128, r4
    mov #0x0, r2
    subx.w 86(r4), 20(r15)
    call #guarda
; bloque 00b4
    mova #0x9195C, r10
    mov #zona+226, r15
    mov #0x2, r2
    bicx.w @r15, r10
    call #guarda
; bloque 00b5
    mova #0x5CE8E, r10
    mova #0xFDA4F, r11
    mov #0x7, r2
    cmpa r10, r11
    call #guarda
; bloque 00b6
    mov #0x4, r2
    mov.b #2, zona+28
    call #guarda
; bloque 00b7
    mova #0x82F9E, r7
    mova #0x52E04, r13
    mova #0x117F1, r8
    mov #0x4, r2
    rpt r8 { bicx.w r7, r13
    call #guarda
; bloque 00b8
    mova #0xC1E15, r4
    mova #0xAC953, r5
    mov #0x4, r2
    pushm.w #2, r5
    popm.w #2, r7
    call #guarda
; bloque 00b9
    mov #zona+156, r6
    mov #0x4, r2
    mov.w @r6, &zona+112
    call #guarda
; bloque 00ba
    mov #0x5, r2
    bitx.b zona+76, &zona+191
    call #guarda
; bloque 00bb
    mov #0x6, r2
    pushx.a zona+54
    popx.a r15
    call #guarda
; bloque 00bc
    mova #0x82AAD, r10
    mov #0x0, r2
    bisx.a r10, zona+236
    call #guarda
; bloque 00bd
    mov #0x0200, r4
    mov #zona+128, r12
    mov #0x2, r2
    and.b 6(r12), r4
    call #guarda
; bloque 00be
    mov #0x2, r2
    bic.b #4, zona+191
    call #guarda
; bloque 00bf
    mov #0x5, r2
    swpbx zona+142
    call #guarda
; bloque 00c0
    mov #0x0400, r13
    mov #zona+180, r8
    mov #0x5, r2
    bisx.w @r8, r13
    call #guarda
; bloque 00c1
    mov #0, r15
    mov #0x1, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c2
    mova #0xAC290, r10
    mova #0xA472B, r5
    mov #0x4, r2
    adda r10, r5
    call #guarda
; bloque 00c3
    mova #0x7FFFF, r4
    mova #0xB7430, r15
    mova #zona+128, r4
    mov #0x5, r2
    mova -60(r4), r15
    call #guarda
; bloque 00c4
    mova #0x7FFFE, r9
    mova #0xD40E1, r11
    mova #0x7FFFF, r14
    mov #0x6, r2
    rpt r14 { addx.a r9, r11
    call #guarda
; bloque 00c5
    mova #0xD4804, r11
    mova #0x67014, r10
    mov #0x5, r2
    rpt #16 { subcx.b r11, r10
    call #guarda
; bloque 00c6
    mova #0x118A5, r9
    mov #0x5, r2
    rrcm.w #3, r9
    call #guarda
; bloque 00c7
    mov #zona+123, r10
    mov #0x0, r2
    xor.b @r10+, &zona+100
    call #guarda
; bloque 00c8
    mova #0x6AA98, r7
    mova #0x109BC, r9
    mova #0xD1ECD, r5
    mov #0x0, r2
    rpt r5 { addx.b r7, r9
    call #guarda
; bloque 00c9
    mova #0x31C77, r8
    mova #0x8EA4D, r9
    mova #0x73E32, r10
    mova #0xF79A8, r11
    mov #0x0, r2
    pushm.w #4, r11
    popm.w #4, r15
    call #guarda
; bloque 00ca
    mova #0x5738F, r15
    mova #0x305FE, r14
    mova #zona+216, r15
    mov #0x2, r2
    mova @r15+, r14
    call #guarda
; bloque 00cb
    mova #0xFFFFF, r7
    mova #0xEC37E, r11
    mov #0x4, r2
    bitx.a r11, r7
    call #guarda
; bloque 00cc
    mov #0, r15
    mov #0x5, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00cd
    mov #zona+128, r9
    mov #0x6, r2
    rrc.b -33(r9)
    call #guarda
; bloque 00ce
    mova #0x76FAB, r5
    mov #0x4, r2
    rram.w #3, r5
    call #guarda
; bloque 00cf
    mov #0, r15
    mova #tabla_a, r8
    mov #0x6, r2
    calla @r8
    call #guarda
; bloque 00d0
    mov #0x6, r2
    rrc.b zona+167
    call #guarda
; bloque 00d1
    mov #zona+128, r7
    mov #0x7, r2
    rrc.b -106(r7)
    call #guarda
; bloque 00d2
    mov #0x0000, r11
    mov #0x4, r2
    and.w r11, &zona+120
    call #guarda
; bloque 00d3
    mova #0x38F44, r14
    mova #0xCA94E, r8
    mova #zona+128, r8
    mov #0x5, r2
    mova r14, -16(r8)
    call #guarda
; bloque 00d4
    mova #0xFFFFF, r7
    mova #0x214A2, r12
    mov #0x7, r2
    rpt r12 { rrcx.w r7
    call #guarda
; bloque 00d5
    mova #0xF9DCA, r10
    mov #0x7, r2
    swpbx r10
    call #guarda
; bloque 00d6
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d7
    mov #0, r15
    mov #0x1, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d8
    mov #zona+128, r13
    mov #0x1, r2
    addcx.b -101(r13), &zona+77
    call #guarda
; bloque 00d9
    mov #0x4, r2
    rrc.b &zona+153
    call #guarda
; bloque 00da
    mova #0xFFFFF, r7
    mov #0x2, r2
    addcx.w &zona+206, r7
    call #guarda
; bloque 00db
    mov #0, r15
    mov #0x5, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00dc
    mova #0xBC7BC, r8
    mov #0x5, r2
    rrcm.w #1, r8
    call #guarda
; bloque 00dd
    mova #0xB923D, r10
    mov #0x5, r2
    rlam.a #3, r10
    call #guarda
; bloque 00de
    mova #0x80000, r9
    mova #0xBA210, r5
    mov #0x7, r2
    mova #0xF14DF, r5
    call #guarda
; bloque 00df
    mov #zona+128, r9
    mova #0xDC884, r14
    mov #0x4, r2
    cmp.b r14, -12(r9)
    call #guarda
; bloque 00e0
    mova #0x2813D, r8
    mova #0x8AD6D, r6
    mov #0x2, r2
    mova #0x00080, r6
    call #guarda
; bloque 00e1
    mova #0x6E7D7, r13
    mov #zona+100, r8
    mov #0x2, r2
    subcx.a @r8+, r13
    call #guarda
; bloque 00e2
    mov #zona+128, r11
    mov #0x7, r2
    bisx.b &zona+51, -23(r11)
    call #guarda
; bloque 00e3
    mova #0x7FFFE, r9
    mov #0x4, r2
    bit.b r9, zona+19
    call #guarda
; bloque 00e4
    mov #zona+128, r5
    mov #0x3, r2
    swpb 92(r5)
    call #guarda
; bloque 00e5
    mov #0, r15
    mov #0x0, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e6
    mov #0x0000, r10
    mov #0x0000, r15
    mov #0x3, r2
    cmpa #0x891C3, r15
    call #guarda
; bloque 00e7
    mov #zona+128, r8
    mov #0x0, r2
    rra.w 16(r8)
    call #guarda
; bloque 00e8
    mov #zona+128, r10
    mova #0xFF4B4, r5
    mov #0x7, r2
    xor.b r5, -80(r10)
    call #guarda
; bloque 00e9
    mov #0x0, r2
    subcx.w zona+194, &zona+72
    call #guarda
; bloque 00ea
    mov #0x0, r2
    swpbx zona+28
    call #guarda
; bloque 00eb
    mova #0xBB82E, r12
    mova #0x4EE05, r5
    mova #zona+220, r12
    mov #0x3, r2
    mova @r12+, r5
    call #guarda
; bloque 00ec
    mova #0x80000, r5
    mov #0x0, r2
    rrum.w #1, r5
    call #guarda
; bloque 00ed
    mov #0x1, r2
    mov.w #4, zona+38
    call #guarda
; bloque 00ee
    mov #0x1, r2
    sub.b #0xFF, zona+144
    call #guarda
; bloque 00ef
    mov #zona+128, r15
    mov #0x0, r2
    bicx.w @r15, zona+62
    call #guarda
; bloque 00f0
    mov #0x0, r2
    rra.b &zona+14
    call #guarda
; bloque 00f1
    mov #0, r15
    mov #0x5, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f2
    mov #0x5039, r5
    mov #0x1, r2
    dadd.w #1, r5
    call #guarda
; bloque 00f3
    mov #zona+128, r8
    mov #0x7, r2
    sub.w zona+30, 48(r8)
    call #guarda
; bloque 00f4
    mov #zona+128, r7
    mov #0x5, r2
    rrcx.a 76(r7)
    call #guarda
; bloque 00f5
    mov #0x1577, r10
    mov #0x0, r2
    dadd.w #2, r10
    call #guarda
; bloque 00f6
    mova #0xA9D54, r11
    mova #0x37E29, r6
    mova #zona+128, r6
    mov #0x0, r2
    mova r11, -32(r6)
    call #guarda
; bloque 00f7
    mov #zona+208, r4
    mov #0x0, r2
    cmp.w @r4, &zona+174
    call #guarda
; bloque 00f8
    mov #0x0040, r10
    mov #0x5, r2
    rra.b r10
    call #guarda
; bloque 00f9
    mov #0x0000, r10
    mov #0x7, r2
    rrum.a #4, r10
    call #guarda
; bloque 00fa
    mov #0, r15
    mov #0x0, r2
    call #subw
    call #guarda
; bloque 00fb
    mov #0, r15
    mov #0x5, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00fc
    mova #0x80E5B, r6
    mov #0x0080, r15
    mova #0xFFFFF, r8
    mov #0x7, r2
    rpt r8 { movx.a r6, r15
    call #guarda
; bloque 00fd
    mov #zona+8, r8
    mov #0x5, r2
    subc.b @r8, zona+171
    call #guarda
; bloque 00fe
    mova #0x357C0, r15
    mov #0x0000, r12
    mova #0x2EA03, r8
    mov #0x3, r2
    rpt r8 { addcx.b r15, r12
    call #guarda
; bloque 00ff
    mova #0xFFFFF, r8
    mova #0x9706A, r7
    mova #zona+216, r8
    mov #0x2, r2
    mova @r8, r7
    call #guarda
; bloque 0100
    mov #0x0000, r6
    mov #0x1, r2
    swpb r6
    call #guarda
; bloque 0101
    mov #0x0000, r12
    mov #0x0000, r9
    mov #0x3, r2
    mova r12, &zona+214
    call #guarda
; bloque 0102
    mov #0, r15
    mova #tabla_a, r7
    mov #0x4, r2
    calla @r7
    call #guarda
; bloque 0103
    mov #0x6, r2
    push.w zona+198
    pop r15
    call #guarda
; bloque 0104
    mova #0xA8E95, r4
    mova #0x7FFFF, r14
    mov #0x5, r2
    suba #0x2D6D9, r14
    call #guarda
; bloque 0105
    mova #0xA665E, r15
    mova #0x32973, r4
    mova #zona+162, r15
    mov #0x3, r2
    mova @r15, r4
    call #guarda
; bloque 0106
    mova #0x7FFFE, r8
    mova #0xE0E85, r9
    mov #0x3, r2
    pushm.a #2, r9
    popm.a #2, r8
    call #guarda
; bloque 0107
    mova #0x4786C, r11
    mov #0x4, r2
    rlam.w #1, r11
    call #guarda
; bloque 0108
    mova #0x28582, r15
    mov #0x0400, r12
    mov #0x4, r2
    rpt r12 { rrux.w r15
    call #guarda
; bloque 0109
    mova #0x19776, r4
    mova #0x7FFFE, r7
    mov #0x1, r2
    mova &zona+232, r7
    call #guarda
; bloque 010a
    mov #0x0000, r9
    mov #0x4, r2
    bit.w #2, r9
    call #guarda
; bloque 010b
    mova #0xC4EBC, r14
    mov #0x2, r2
    rrcm.a #1, r14
    call #guarda
; bloque 010c
    mov #zona+128, r11
    mova #0xEBBA3, r15
    mov #0x5, r2
    bic.b r15, -39(r11)
    call #guarda
; bloque 010d
    mova #0xF5049, r10
    mov #0x3, r2
    bic.b #0xAB, r10
    call #guarda
; bloque 010e
    mov #zona+128, r9
    mov #0x1, r2
    swpb 108(r9)
    call #guarda
; bloque 010f
    mov #zona+128, r6
    mova #0xE163D, r9
    mov #0x1, r2
    subx.b r9, 42(r6)
    call #guarda
; bloque 0110
    mova #0xE9BA8, r9
    mova #0xAEA66, r10
    mov #0x0, r2
    pushm.w #2, r10
    popm.w #2, r11
    call #guarda
; bloque 0111
    mov #zona+44, r9
    mov #0x1, r2
    bitx.b @r9+, zona+87
    call #guarda
; bloque 0112
    mov #0x6, r2
    bis.w zona+68, &zona+8
    call #guarda
; bloque 0113
    mova #0x10000, r13
    mov #0x2, r2
    bis.b #2, r13
    call #guarda
; bloque 0114
    mov #0x5, r2
    addcx.w #1, &zona+106
    call #guarda
; bloque 0115
    mov #0x0087, r4
    mov #0x0049, r11
    mov #0x5, r2
    rpt #11 { daddx.b r4, r11
    call #guarda
; bloque 0116
    mov #0x5, r2
    rrc.b &zona+44
    call #guarda
; bloque 0117
    mova #0x38678, r4
    mov #0x1, r2
    andx.w zona+220, r4
    call #guarda
; bloque 0118
    mov #0x0BA0, r11
    mov #0x6, r2
    bit.w #0, r11
    call #guarda
; bloque 0119
    mova #0x2B627, r14
    mov #zona+34, r8
    mov #0x4, r2
    xorx.b @r8+, r14
    call #guarda
; bloque 011a
    mov #zona+128, r12
    mov #0x4, r2
    add.w #0x2000, 56(r12)
    call #guarda
; bloque 011b
    mov #0x1, r2
    pushx.w #0x1251
    pop r15
    call #guarda
; bloque 011c
    mova #0xE878E, r7
    mov #0x7, r2
    bit.b #0x28, r7
    call #guarda
; bloque 011d
    mov #zona+128, r4
    mov #zona+128, r7
    mov #0x5, r2
    bitx.w -42(r7), -72(r4)
    call #guarda
; bloque 011e
    mov #0x2, r2
    addx.b #1, &zona+164
    call #guarda
; bloque 011f
    mova #0xB2404, r9
    mov #0x0000, r12
    mova #zona+242, r9
    mov #0x7, r2
    mova @r9, r12
    call #guarda
; bloque 0120
    mova #0x80000, r15
    mov #0x2, r2
    xorx.a zona+220, r15
    call #guarda
; bloque 0121
    mov #0x3, r2
    subcx.a #1, zona+54
    call #guarda
; bloque 0122
    mov #0x7863, r15
    mov #0x7806, r6
    mov #0x7, r2
    dadd.w r6, r15
    call #guarda
; bloque 0123
    mova #0x3B532, r5
    mova #0x44334, r13
    mov #0x7, r2
    mova &zona+74, r13
    call #guarda
; bloque 0124
    mov #zona+222, r13
    mov #0x7, r2
    addx.w @r13, &zona+204
    call #guarda
; bloque 0125
    mov #0x4, r2
    add.b #-1, &zona+189
    call #guarda
; bloque 0126
    mov #zona+232, r4
    mov #0x5, r2
    rrc.b @r4
    call #guarda
; bloque 0127
    mova #0x55C2D, r8
    mova #0x21223, r9
    mov #0x1, r2
    adda r8, r9
    call #guarda
; bloque 0128
    mova #0xFFFFF, r15
    mov #0x0, r2
    rrum.w #2, r15
    call #guarda
; bloque 0129
    mov #0, r15
    mov #0x2, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 012a
    mova #0xAFBB2, r15
    mov #0x1, r2
    sub.b r15, r15
    call #guarda
; bloque 012b
    mova #0xE4DD5, r13
    mova #0x24031, r11
    mov #0x6, r2
    suba #0x00000, r11
    call #guarda
; bloque 012c
    mova #0xE9450, r7
    mova #0x8523C, r5
    mov #0x1, r2
    subcx.a r5, r7
    call #guarda
; bloque 012d
    mova #0xB8B63, r12
    mov #0x6, r2
    rrux.a r12
    call #guarda
; bloque 012e
    mova #0xFFFFF, r4
    mova #0xA8A86, r7
    mov #0x0, r2
    mova r4, &zona+188
    call #guarda
; bloque 012f
    mova #0xA17F1, r9
    mov #0x1, r2
    pushx.a r9
    popx.a r15
    call #guarda
; bloque 0130
    mova #0x80000, r11
    mova #0xF127B, r10
    mova #zona+200, r11
    mov #0x7, r2
    mova @r11, r10
    call #guarda
; bloque 0131
    mova #0xE566F, r12
    mova #0x92A8E, r5
    mova #0xDD169, r11
    mov #0x5, r2
    rpt r11 { addx.b r12, r5
    call #guarda
; bloque 0132
    mov #0x3, r2
    push.b &zona+242
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0133
    mova #0xA48CB, r8
    mova #0xFFFFF, r5
    mov #0x3, r2
    suba #0x3DE73, r5
    call #guarda
; bloque 0134
    mova #0xECEEF, r6
    mov #0x3, r2
    bisx.b #0xD, r6
    call #guarda
; bloque 0135
    mova #0xED204, r8
    mova #0xB474A, r15
    mov #0x0000, r12
    mov #0x5, r2
    rpt r12 { subcx.b r8, r15
    call #guarda
; bloque 0136
    mova #0xA4D46, r11
    mova #0x59A5C, r9
    mov #0x1, r2
    cmp.w r9, r11
    call #guarda
; bloque 0137
    mov #0x0800, r13
    mov #0x3, r2
    subx.a #4, r13
    call #guarda
; bloque 0138
    mov #0x4, r2
    sub.w &zona+228, zona+74
    call #guarda
; bloque 0139
    mov #zona+128, r12
    mov #0x3, r2
    rra.b -8(r12)
    call #guarda
; bloque 013a
    mova #0x80000, r7
    mova #0x66199, r15
    mov #0x6, r2
    mova r7, &zona+42
    call #guarda
; bloque 013b
    mov #zona+128, r12
    mova #0x1EBFA, r8
    mov #0x2, r2
    addx.w r8, -70(r12)
    call #guarda
; bloque 013c
    mov #zona+128, r4
    mov #zona+128, r6
    mov #0x4, r2
    cmpx.a 78(r6), 12(r4)
    call #guarda
; bloque 013d
    mov #0, r15
    mov #0x4, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 013e
    mov #0, r15
    mov #0x0, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 013f
    mov #0x6, r2
    rrax.w zona+188
    call #guarda
; bloque 0140
    mov #zona+128, r11
    mov #0x3, r2
    bicx.a #0, 116(r11)
    call #guarda
; bloque 0141
    mov #0x0200, r9
    mov #zona+30, r15
    mov #0x2, r2
    and.w @r15+, r9
    call #guarda
; bloque 0142
    mov #zona+128, r8
    mov #0x0, r2
    sub.b 51(r8), zona+192
    call #guarda
; bloque 0143
    mov #0, r15
    mov #tabla, r11
    mov #0x3, r2
    call @r11
    call #guarda
; bloque 0144
    mova #0x343A8, r15
    mova #0xDAC60, r11
    mov #0x7, r2
    cmpa #0x02000, r11
    call #guarda
; bloque 0145
    mov #zona+128, r14
    mov #0x4, r2
    rrax.w 12(r14)
    call #guarda
; bloque 0146
    mova #0xEE943, r6
    mov #0x3, r2
    swpb r6
    call #guarda
; bloque 0147
    mov #zona+126, r15
    mov #0x7, r2
    bicx.a @r15, zona+112
    call #guarda
; bloque 0148
    mov #zona+128, r5
    mov #0x7, r2
    bicx.a &zona+236, 36(r5)
    call #guarda
; bloque 0149
    mov #0x0, r2
    rrc.b &zona+111
    call #guarda
; bloque 014a
    mova #0x62F62, r9
    mova #0x5BD1D, r4
    mova #zona+128, r4
    mov #0x1, r2
    mova r9, 110(r4)
    call #guarda
; bloque 014b
    mova #0xBACAE, r9
    mova #0xFFFFF, r10
    mov #0x3, r2
    pushm.w #2, r10
    popm.w #2, r13
    call #guarda
; bloque 014c
    mov #0x7, r2
    add.b #0x1, &zona+149
    call #guarda
; bloque 014d
    mov #zona+128, r9
    mov #0x1, r2
    rrux.w 112(r9)
    call #guarda
; bloque 014e
    mov #0xE20F, r14
    mov #0x4, r2
    rrum.a #2, r14
    call #guarda
; bloque 014f
    mov #0x0, r2
    and.b zona+9, &zona+251
    call #guarda
; bloque 0150
    mov #0, r15
    mov #0x4, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0151
    mov #0, r15
    mova #tabla_a-4, r10
    mov #0x1, r2
    calla 4(r10)
    call #guarda
; bloque 0152
    mov #0x0008, r5
    mova #0x2EBA0, r7
    mov #0x4, r2
    rpt #16 { subx.a r5, r7
    call #guarda
; bloque 0153
    mova #0xAE973, r7
    mov #0x4, r2
    rrux.b r7
    call #guarda
; bloque 0154
    mov #zona+128, r14
    mov #0x2, r2
    addc.w &zona+96, 0(r14)
    call #guarda
; bloque 0155
    mov #0, r15
    mov #0x4, r2
    calla #sub
    call #guarda
; bloque 0156
    mov #0x4, r2
    pushx.a #0x0
    popx.a r15
    call #guarda
; bloque 0157
    mov #zona+128, r12
    mov #0x5, r2
    rrux.w 58(r12)
    call #guarda
; bloque 0158
    mova #0xC99BE, r12
    mova #0xAE117, r13
    mova #zona+240, r12
    mov #0x3, r2
    mova @r12, r13
    call #guarda
; bloque 0159
    mov #0x0000, r12
    mova #0xDD69F, r13
    mova #0xD15B8, r14
    mova #0x6236B, r15
    mov #0x3, r2
    pushm.a #4, r15
    popm.a #4, r9
    call #guarda
; bloque 015a
    mov #0x0000, r13
    mov #0x5, r2
    rlam.a #4, r13
    call #guarda
; bloque 015b
    mova #0xCA6EE, r12
    mov #0x3, r2
    addx.b zona+205, r12
    call #guarda
; bloque 015c
    mov #0x0089, r8
    mov #0x0053, r10
    mova #0xD6B4B, r6
    mov #0x1, r2
    rpt r6 { daddx.b r8, r10
    call #guarda
; bloque 015d
    mova #0x92FCA, r14
    mov #0x3, r2
    add.w r14, zona+54
    call #guarda
; bloque 015e
    mov #zona+128, r4
    mov #0x0, r2
    push.w -88(r4)
    pop r15
    call #guarda
; bloque 015f
    mova #0x5AA8A, r10
    mov #0x3, r2
    rrum.w #3, r10
    call #guarda
; bloque 0160
    mov #0x1, r2
    rrcx.w &zona+76
    call #guarda
; bloque 0161
    mova #0xFFFFF, r9
    mova #0xAD8DB, r10
    mov #0x5, r2
    andx.a r10, r9
    call #guarda
; bloque 0162
    mov #0x5, r2
    rra.b &zona+70
    call #guarda
; bloque 0163
    mova #0x48EAC, r15
    mova #0x7FFFE, r12
    mov #0x0, r2
    andx.a r12, r15
    call #guarda
; bloque 0164
    mov #zona+133, r6
    mov #0x4, r2
    addc.b @r6, zona+118
    call #guarda
; bloque 0165
    mova #0xE1769, r13
    mova #0xFFFFF, r8
    mov #0x5, r2
    rpt r8 { rrux.a r13
    call #guarda
; bloque 0166
    mov #zona+128, r4
    mov #0x7, r2
    bitx.a zona+160, 106(r4)
    call #guarda
; bloque 0167
    mov #0x5, r2
    subc.w zona+206, zona+250
    call #guarda
; bloque 0168
    mova #0x80000, r12
    mov #0x0080, r13
    mov #0x0, r2
    subx.b r13, r12
    call #guarda
; bloque 0169
    mov #0x0000, r12
    mov #0x0, r2
    subx.w &zona+56, r12
    call #guarda
; bloque 016a
    mov #zona+195, r11
    mov #0x1, r2
    subcx.b @r11, &zona+151
    call #guarda
; bloque 016b
    mova #0x90CFF, r12
    mova #0x6E6A7, r4
    mova #0x62B6C, r5
    mov #0x5, r2
    rpt r5 { addx.w r12, r4
    call #guarda
; bloque 016c
    mova #0xFFFFF, r12
    mova #0x6A7E6, r8
    mov #0x3, r2
    adda #0x5FF34, r8
    call #guarda
; bloque 016d
    mov #0x3, r2
    rrux.w zona+12
    call #guarda
; bloque 016e
    mova #0x80000, r15
    mov #0x7, r2
    rrax.w r15
    call #guarda
; bloque 016f
    mova #0x2E11C, r11
    mova #0xFFFFF, r5
    mov #0x7, r2
    adda r11, r5
    call #guarda
; bloque 0170
    mov #zona+128, r14
    mov #0x4, r2
    bit.b zona+193, -53(r14)
    call #guarda
; bloque 0171
    mova #0xCBC04, r5
    mova #0x59525, r11
    mova #zona+128, r11
    mov #0x4, r2
    mova r5, 58(r11)
    call #guarda
; bloque 0172
    mova #0x9B139, r7
    mov #zona+128, r5
    mov #0x2, r2
    and.w -38(r5), r7
    call #guarda
; bloque 0173
    mova #0x8BB1C, r8
    mova #0x7A811, r9
    mov #0x3, r2
    rpt #9 { addx.w r8, r9
    call #guarda
; bloque 0174
    mova #0x98C6A, r15
    mov #0x5, r2
    pushm.a #1, r15
    popm.a #1, r13
    call #guarda
; bloque 0175
    mova #0xB0601, r4
    mova #0x29674, r9
    mov #0x2, r2
    cmpa #0x00000, r9
    call #guarda
; bloque 0176
    mov #0x4, r2
    push.b &zona+26
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0177
    mova #0xFFFFF, r6
    mov #0x2, r2
    sub.w &zona+74, r6
    call #guarda
; bloque 0178
    mov #0x3, r2
    rra.w &zona+142
    call #guarda
; bloque 0179
    mova #0x55058, r4
    mov #0x6, r2
    pushx.a r4
    popx.a r15
    call #guarda
; bloque 017a
    mov #zona+40, r7
    mov #0x2, r2
    subcx.b @r7+, zona+18
    call #guarda
; bloque 017b
    mov #0x4, r2
    bic.w #0x12AC, &zona+72
    call #guarda
; bloque 017c
    mova #0x40000, r7
    mova #0xF729D, r15
    mova #zona+128, r7
    mov #0x1, r2
    mova -24(r7), r15
    call #guarda
; bloque 017d
    mova #0x7FFFF, r7
    mova #0xAD79D, r8
    mova #0xB7373, r9
    mov #0x2, r2
    pushm.a #3, r9
    popm.a #3, r8
    call #guarda
; bloque 017e
    mova #0x2075E, r9
    mov #0x8DF3, r7
    mov #0x0, r2
    xor.w r7, r9
    call #guarda
; bloque 017f
    mov #0x5, r2
    rra.b zona+247
    call #guarda
; bloque 0180
    mov #zona+66, r10
    mov #0x1, r2
    rrax.a @r10
    call #guarda
; bloque 0181
    mova #0x9DA2B, r11
    mov #0x0000, r13
    mov #0x5, r2
    rpt #16 { bisx.b r11, r13
    call #guarda
; bloque 0182
    mova #0xD1583, r8
    mova #0x80000, r9
    mova #0xFFFFF, r10
    mov #0x0004, r11
    mova #0x5C8A9, r12
    mov #0x7, r2
    pushm.a #5, r12
    popm.a #5, r13
    call #guarda
; bloque 0183
    mov #zona+128, r6
    mov #zona+58, r15
    mov #0x3, r2
    subx.w @r15+, -84(r6)
    call #guarda
; bloque 0184
    mov #0x2657, r14
    mov #0x3, r2
    dadd.w #0x5704, r14
    call #guarda
; bloque 0185
    mova #0xC72F5, r8
    mova #0x8D33C, r14
    mov #0x6, r2
    mova &zona+30, r14
    call #guarda
; bloque 0186
    mov #0x4, r2
    sub.w #0x41D5, zona+66
    call #guarda
; bloque 0187
    mov #zona+50, r4
    mov #0x7, r2
    rra.w @r4
    call #guarda
; bloque 0188
    mov #0x0080, r5
    mova #0x3BA07, r9
    mov #0x0000, r7
    mov #0x2, r2
    rpt r7 { subcx.a r5, r9
    call #guarda
; bloque 0189
    mova #0x2BA35, r11
    mov #0x1, r2
    subcx.a zona+182, r11
    call #guarda
; bloque 018a
    mov #0x4, r2
    rrux.a zona+124
    call #guarda
; bloque 018b
    mov #0x4197, r13
    mov #0x9973, r9
    mova #0x2AA51, r11
    mov #0x5, r2
    rpt r11 { daddx.w r13, r9
    call #guarda
; bloque 018c
    mov #0, r15
    mova #tabla_a-4, r9
    mov #0x7, r2
    calla 4(r9)
    call #guarda
; bloque 018d
    mova #0x265E2, r10
    mova #0x7D2B5, r11
    mov #0x1, r2
    pushm.w #2, r11
    popm.w #2, r12
    call #guarda
; bloque 018e
    mova #0xFA5CE, r4
    mov #0x3, r2
    bitx.w r4, &zona+22
    call #guarda
; bloque 018f
    mova #0x7FFFF, r11
    mov #0x0000, r4
    mov #0x1, r2
    cmpa #0x00004, r4
    call #guarda
; bloque 0190
    mov #0x5490, r12
    mov #0x8132, r7
    mov #0x0, r2
    rpt #16 { daddx.w r12, r7
    call #guarda
; bloque 0191
    mova #0xE875E, r11
    mova #0x36828, r13
    mov #0x4, r2
    bitx.w r13, r11
    call #guarda
; bloque 0192
    mov #0x0040, r11
    mova #0x8A8F7, r7
    mov #0x1, r2
    rpt r7 { rrux.a r11
    call #guarda
; bloque 0193
    mova #0x650B1, r7
    mov #0x0000, r8
    mova #0x38CE5, r9
    mova #0x98D9E, r10
    mov #0x4, r2
    pushm.a #4, r10
    popm.a #4, r14
    call #guarda
; bloque 0194
    mova #0xFFFFF, r12
    mov #zona+174, r11
    mov #0x0, r2
    addcx.a @r11+, r12
    call #guarda
; bloque 0195
    mova #0xE50B5, r7
    mova #0x7FFFE, r8
    mova #0x7FFFE, r9
    mov #0x2, r2
    pushm.a #3, r9
    popm.a #3, r8
    call #guarda
; bloque 0196
    mova #0x3C1E1, r6
    mov #0x1, r2
    bicx.b r6, zona+65
    call #guarda
; bloque 0197
    mov #0x0000, r13
    mov #0x4, r2
    rram.w #1, r13
    call #guarda
; bloque 0198
    mova #0xC9520, r11
    mov #0x3, r2
    rrux.a r11
    call #guarda
; bloque 0199
    mov #0x0000, r12
    mov #zona+103, r14
    mov #0x2, r2
    xorx.b @r14, r12
    call #guarda
; bloque 019a
    mov #zona+128, r14
    mov #0x1, r2
    cmp.w -98(r14), &zona+14
    call #guarda
; bloque 019b
    mov #zona+128, r10
    mov #0x3, r2
    rrc.b 114(r10)
    call #guarda
; bloque 019c
    mova #0xFFFFF, r6
    mov #0x6, r2
    cmpx.a r6, &zona+82
    call #guarda
; bloque 019d
    mov #0x4, r2
    swpb &zona+26
    call #guarda
; bloque 019e
    mov #zona+129, r5
    mov #0x2, r2
    mov.b @r5, zona+124
    call #guarda
; bloque 019f
    mov #0x0080, r13
    mov #0x6, r2
    rlam.a #1, r13
    call #guarda
; bloque 01a0
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a1
    mov #0x0020, r8
    mov #0x3, r2
    rrc.b r8
    call #guarda
; bloque 01a2
    mova #0x44507, r5
    mova #0x1F807, r10
    mov #0x6, r2
    suba r5, r10
    call #guarda
; bloque 01a3
    mova #0x2145D, r8
    mova #0xC2257, r10
    mov #0x4, r2
    mova &zona+94, r10
    call #guarda
; bloque 01a4
    mova #0xFFFFF, r15
    mov #0x4, r2
    xorx.w r15, &zona+166
    call #guarda
; bloque 01a5
    mova #0x271EA, r9
    mova #0xFFFFF, r14
    mova #zona+128, r9
    mov #0x1, r2
    mova -20(r9), r14
    call #guarda
; bloque 01a6
    mov #0x1, r2
    swpb zona+142
    call #guarda
; bloque 01a7
    mov #0xF81D, r11
    mov #0x0, r2
    bicx.w zona+66, r11
    call #guarda
; bloque 01a8
    mov #zona+128, r6
    mova #0xFB305, r5
    mov #0x5, r2
    cmpx.w r5, 44(r6)
    call #guarda
; bloque 01a9
    mov #0x7, r2
    pushx.w #4
    pop r15
    call #guarda
; bloque 01aa
    mov #zona+44, r4
    mov #0x5, r2
    pushx.w @r4
    pop r15
    call #guarda
; bloque 01ab
    mova #0x3AEAB, r8
    mova #0x1FF31, r12
    mov #0x5, r2
    cmp.b r12, r8
    call #guarda
; bloque 01ac
    mov #0, r15
    mov #0x1, r2
    calla &tabla_a
    call #guarda
; bloque 01ad
    mov #0x3, r2
    rrc.w &zona+196
    call #guarda
; bloque 01ae
    mov #0x416E, r5
    mova #0xC6DA1, r8
    mova #zona+120, r5
    mov #0x7, r2
    mova @r5, r8
    call #guarda
; bloque 01af
    mov #0x0000, r8
    mova #0xAFF28, r14
    mov #0x7, r2
    rpt #5 { bicx.a r8, r14
    call #guarda
; bloque 01b0
    mova #0x91E1B, r4
    mova #0xFFFFF, r10
    mov #0xA74E, r7
    mov #0x5, r2
    rpt r7 { subcx.a r4, r10
    call #guarda
; bloque 01b1
    mov #0x0E1A, r14
    mov #0x1, r2
    swpb r14
    call #guarda
; bloque 01b2
    mov #zona+128, r15
    mov #0x4, r2
    swpbx -128(r15)
    call #guarda
; bloque 01b3
    mov #0x1, r2
    rra.w &zona+150
    call #guarda
; bloque 01b4
    mova #0x7D1B5, r13
    mov #0x4, r2
    pushm.a #1, r13
    popm.a #1, r14
    call #guarda
; bloque 01b5
    mov #0x2, r2
    swpb zona+124
    call #guarda
; bloque 01b6
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 01b7
    mova #0xC159F, r11
    mov #0x3, r2
    rrum.a #1, r11
    call #guarda
; bloque 01b8
    mova #0x51EF2, r8
    mov #0x3, r2
    subcx.a #0x0, r8
    call #guarda
; bloque 01b9
    mova #0x139C6, r4
    mov #0x4, r2
    swpbx r4
    call #guarda
; bloque 01ba
    mov #zona+128, r4
    mov #0x3, r2
    mov.w -124(r4), &zona+206
    call #guarda
; bloque 01bb
    mova #0xFFFFF, r9
    mov #zona+128, r11
    mov #0x0, r2
    sub.w 82(r11), r9
    call #guarda
; bloque 01bc
    mova #0x40000, r6
    mova #0x2A6A9, r9
    mov #0x4, r2
    rpt #14 { xorx.w r6, r9
    call #guarda
; bloque 01bd
    mov #0x0000, r4
    mova #0xFFFFF, r10
    mov #0x6, r2
    rpt #3 { movx.a r4, r10
    call #guarda
; bloque 01be
    mov #zona+128, r11
    mov #0x5, r2
    swpbx @r11
    call #guarda
; bloque 01bf
    mov #zona+128, r10
    mov #0x3, r2
    rrux.a -64(r10)
    call #guarda
; bloque 01c0
    mova #0x80000, r8
    mov #0x0000, r9
    mov #0x12E9, r10
    mov #0x5, r2
    pushm.w #3, r10
    popm.w #3, r9
    call #guarda
; bloque 01c1
    mov #zona+128, r8
    mov #0x3, r2
    subcx.b -111(r8), zona+99
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
