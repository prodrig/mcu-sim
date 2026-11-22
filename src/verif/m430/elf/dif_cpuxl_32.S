    .text
    .global prueba
    .global fin
prueba:
    pushm.a #7, r10
    movx.a r1, &sp_guardado
    mova #0xE0000, r1          ; la pila, por encima de 64 KB
    movx.a #res, &resptr
; bloque 0000
    mova #zona+184, r4
    mov #0x1, r2
    subcx.b @r4+, zona+6
    calla #guarda
; bloque 0001
    mova #zona+134, r4
    mov #0x5, r2
    pushx.a @r4
    popx.a r15
    calla #guarda
; bloque 0002
    mova #0xFFFFF, r12
    mova #0xDCDEF, r6
    mov #0x7, r2
    subcx.w r6, r12
    calla #guarda
; bloque 0003
    mova #zona+145, r13
    mov #0x0, r2
    subx.b @r13, &zona+76
    calla #guarda
; bloque 0004
    mova #zona+128, r15
    mova #zona+128, r11
    mov #0x7, r2
    cmpx.b -111(r11), -19(r15)
    calla #guarda
; bloque 0005
    mov #0x0000, r14
    mova #0x7FFFF, r8
    mov #0x1, r2
    rpt #2 { addx.a r14, r8
    calla #guarda
; bloque 0006
    mov #0x3, r2
    cmpx.a #8, &zona+146
    calla #guarda
; bloque 0007
    mova #zona+128, r13
    mov #0x6, r2
    bicx.b #1, -93(r13)
    calla #guarda
; bloque 0008
    mova #0xA309F, r11
    mova #zona+128, r6
    mov #0x2, r2
    addc.b -17(r6), r11
    calla #guarda
; bloque 0009
    mov #0x5, r2
    rrax.b zona+67
    calla #guarda
; bloque 000a
    mov #0x0000, r4
    mov #0x2, r2
    rrcm.a #3, r4
    calla #guarda
; bloque 000b
    mova #0xE6B67, r13
    mova #zona+174, r4
    mov #0x5, r2
    addx.a @r4, r13
    calla #guarda
; bloque 000c
    mov #0x0400, r12
    mov #0x1, r2
    rrax.b r12
    calla #guarda
; bloque 000d
    mova #0xBABCB, r7
    mov #0x2, r2
    rrcx.a r7
    calla #guarda
; bloque 000e
    mov #0, r15
    mov #0x5, r2
    jge 1f
    mov #1, r15
1:
    calla #guarda
; bloque 000f
    mova #0xC0FCC, r14
    mova #0x1B35F, r15
    mova #0xE5031, r12
    mov #0x7, r2
    rpt r12 { addcx.a r14, r15
    calla #guarda
; bloque 0010
    mov #0x2937, r8
    mov #0x2, r2
    dadd.w #2, r8
    calla #guarda
; bloque 0011
    mova #0x9974B, r6
    mov #0x0080, r8
    mov #0x5, r2
    mova #0x4E543, r8
    calla #guarda
; bloque 0012
    mova #0x7FFFF, r8
    mov #0x5, r2
    mov.w #0x7FFE, r8
    calla #guarda
; bloque 0013
    mov #0x1, r2
    rrax.a zona+228
    calla #guarda
; bloque 0014
    mov #0x7CDE, r13
    mova #0x8D0F2, r4
    mov #0x4, r2
    suba #0x00000, r4
    calla #guarda
; bloque 0015
    mov #0, r15
    mova #subw, r14
    mov #0x7, r2
    call r14
    calla #guarda
; bloque 0016
    mova #0x3E4B5, r14
    mov #0x2, r2
    rrcx.w r14
    calla #guarda
; bloque 0017
    mova #0x55D14, r5
    mova #0xFFFFF, r6
    mova #zona+128, r6
    mov #0x1, r2
    mova r5, -24(r6)
    calla #guarda
; bloque 0018
    mova #0x5B138, r5
    mov #0x2, r2
    xorx.b &zona+187, r5
    calla #guarda
; bloque 0019
    mova #0xDBAAA, r11
    mova #0xB7971, r12
    mova #0x346B2, r13
    mov #0x3, r2
    pushm.a #3, r13
    popm.a #3, r8
    calla #guarda
; bloque 001a
    mova #zona+118, r7
    mov #0x7, r2
    rrux.a @r7
    calla #guarda
; bloque 001b
    mov #0x7, r2
    swpbx &zona+20
    calla #guarda
; bloque 001c
    mov #0x0006, r11
    mov #0x5, r2
    dadd.b r11, r11
    calla #guarda
; bloque 001d
    mov #0x1, r2
    andx.a #4, zona+212
    calla #guarda
; bloque 001e
    mov #0x0002, r14
    mov #0x7, r2
    rlam.a #2, r14
    calla #guarda
; bloque 001f
    mov #0, r15
    mov #0x7, r2
    jn 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0020
    mov #0, r15
    mov #0x7, r2
    call #subw
    calla #guarda
; bloque 0021
    mov #0x3, r2
    addcx.a &zona+238, zona+94
    calla #guarda
; bloque 0022
    mova #0x80000, r10
    mov #0x7, r2
    rlam.a #4, r10
    calla #guarda
; bloque 0023
    mova #0xFFFFF, r6
    mov #0x0000, r13
    mov #0x2, r2
    and.b r13, r6
    calla #guarda
; bloque 0024
    mov #0x0020, r13
    mova #0xFFFFF, r5
    mov #0x3, r2
    bisx.b r5, r13
    calla #guarda
; bloque 0025
    mova #0x7FFFF, r4
    mova #zona+128, r11
    mov #0x1, r2
    bis.w 8(r11), r4
    calla #guarda
; bloque 0026
    mov #0x4, r2
    rrux.a zona+84
    calla #guarda
; bloque 0027
    mov #0x0000, r10
    mov #0xF650, r7
    mov #0x7, r2
    mova &zona+116, r7
    calla #guarda
; bloque 0028
    mov #0, r15
    mov #0x2, r2
    call #subw
    calla #guarda
; bloque 0029
    mov #0x3, r2
    push.b #1
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 002a
    mov #0x0001, r4
    mov #0x0, r2
    rlam.a #3, r4
    calla #guarda
; bloque 002b
    mova #0xFFFFF, r8
    mov #0x0020, r14
    mova #zona+128, r8
    mov #0x0, r2
    mova 68(r8), r14
    calla #guarda
; bloque 002c
    mov #0x0588, r15
    mov #0x8679, r8
    mov #0x3, r2
    dadd.w r8, r15
    calla #guarda
; bloque 002d
    mov #0x0, r2
    rrcx.b &zona+31
    calla #guarda
; bloque 002e
    mov #0x1, r2
    swpbx zona+14
    calla #guarda
; bloque 002f
    mova #0x77B11, r9
    mov #0x2, r2
    swpbx r9
    calla #guarda
; bloque 0030
    mova #0x4A08F, r14
    mova #0x81755, r15
    mova #0x7D26A, r7
    mov #0x7, r2
    rpt r7 { bicx.a r14, r15
    calla #guarda
; bloque 0031
    mova #0x742AF, r15
    mova #0xFFFFF, r9
    mov #0x4, r2
    suba #0x6FAA2, r9
    calla #guarda
; bloque 0032
    mova #0xF4044, r9
    mov #0x0, r2
    andx.w r9, &zona+110
    calla #guarda
; bloque 0033
    mova #0x964B0, r14
    mova #zona+100, r7
    mov #0x2, r2
    mov.w @r7+, r14
    calla #guarda
; bloque 0034
    mov #0x7, r2
    swpbx zona+24
    calla #guarda
; bloque 0035
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0036
    mov #0, r15
    mov #0x3, r2
    call #subw
    calla #guarda
; bloque 0037
    mova #zona+10, r8
    mov #0x2, r2
    movx.a @r8+, &zona+92
    calla #guarda
; bloque 0038
    mova #0x4FA71, r6
    mov #0x2, r2
    rram.a #1, r6
    calla #guarda
; bloque 0039
    mova #0x8E2B3, r7
    mov #0x0800, r9
    mova #zona+158, r7
    mov #0x1, r2
    mova @r7, r9
    calla #guarda
; bloque 003a
    mova #zona+128, r15
    mov #0x3, r2
    bicx.b zona+251, -2(r15)
    calla #guarda
; bloque 003b
    mova #0x80000, r10
    mova #0x7FFFF, r12
    mov #0x2, r2
    mova r10, r12
    calla #guarda
; bloque 003c
    mov #0, r15
    mov #0x6, r2
    jl 1f
    mov #1, r15
1:
    calla #guarda
; bloque 003d
    mova #zona+128, r5
    mov #0x1, r2
    andx.a &zona+104, -78(r5)
    calla #guarda
; bloque 003e
    mova #0x7FFFE, r15
    mov #0x5, r2
    mov.w #8, r15
    calla #guarda
; bloque 003f
    mov #0, r15
    mov #0x6, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0040
    mova #0xFFFFF, r5
    mov #0x2, r2
    addcx.b #0x2, r5
    calla #guarda
; bloque 0041
    mova #0x124E7, r14
    mov #0x4, r2
    bicx.w #-1, r14
    calla #guarda
; bloque 0042
    mova #0x65F77, r4
    mova #zona+128, r12
    mov #0x1, r2
    andx.a -68(r12), r4
    calla #guarda
; bloque 0043
    mova #0x79FA6, r5
    mova #zona+128, r10
    mov #0x2, r2
    add.w -94(r10), r5
    calla #guarda
; bloque 0044
    mova #0x80000, r5
    mova #0x842F4, r6
    mova #0x167BB, r7
    mov #0x2E73, r8
    mov #0x3, r2
    pushm.a #4, r8
    popm.a #4, r12
    calla #guarda
; bloque 0045
    mova #0x7C879, r12
    mova #0x5E7A2, r10
    mova #zona+128, r10
    mov #0x7, r2
    mova r12, -102(r10)
    calla #guarda
; bloque 0046
    mov #0x3, r2
    rrcx.w &zona+34
    calla #guarda
; bloque 0047
    mov #0x7, r2
    subcx.w #0xF816, &zona+198
    calla #guarda
; bloque 0048
    mova #0x40000, r10
    mov #0x1, r2
    addx.a #0x1B1A2, r10
    calla #guarda
; bloque 0049
    mov #0xCF5A, r6
    mov #0x0080, r12
    mova #0x6CF96, r10
    mov #0x0, r2
    rpt r10 { xorx.w r6, r12
    calla #guarda
; bloque 004a
    mov #0x0, r2
    rrux.a zona+202
    calla #guarda
; bloque 004b
    mov #0x0, r2
    cmpx.w &zona+126, zona+70
    calla #guarda
; bloque 004c
    mova #0x4A170, r11
    mov #0x1, r2
    addcx.b #8, r11
    calla #guarda
; bloque 004d
    mov #0x0000, r4
    mov #0x4, r2
    pushx.b r4
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 004e
    mova #0x34754, r4
    mova #0xFFFFF, r6
    mova #zona+128, r6
    mov #0x6, r2
    mova r4, -36(r6)
    calla #guarda
; bloque 004f
    mov #0x1, r2
    pushx.b #1
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 0050
    mov #0x0004, r12
    mov #0x4, r2
    movx.a r12, zona+0
    calla #guarda
; bloque 0051
    mov #0x2B0A, r7
    mova #0x7FFFF, r15
    mov #0x3, r2
    adda r7, r15
    calla #guarda
; bloque 0052
    mova #0xAA75F, r14
    mova #0x607BE, r9
    mova #0x86F3A, r11
    mov #0x1, r2
    rpt r11 { addx.w r14, r9
    calla #guarda
; bloque 0053
    mova #0xF24DA, r9
    mova #0xFFFFF, r10
    mova #0x2000C, r11
    mova #0x93A68, r12
    mova #0xDCBBB, r13
    mov #0x7, r2
    pushm.a #5, r13
    popm.a #5, r9
    calla #guarda
; bloque 0054
    mova #zona+128, r13
    mova #zona+186, r10
    mov #0x1, r2
    bis.b @r10, 109(r13)
    calla #guarda
; bloque 0055
    mov #0x6, r2
    rrax.b zona+132
    calla #guarda
; bloque 0056
    mova #0x2ECEC, r6
    mov #0x7, r2
    cmpx.b zona+236, r6
    calla #guarda
; bloque 0057
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0058
    mov #0x1151, r8
    mov #0x3, r2
    daddx.w #0x1610, r8
    calla #guarda
; bloque 0059
    mov #0x7, r2
    rrax.w zona+34
    calla #guarda
; bloque 005a
    mova #zona+128, r9
    mov #0x3, r2
    rrax.a -124(r9)
    calla #guarda
; bloque 005b
    mova #zona+128, r4
    mova #0x1A1CC, r6
    mov #0x5, r2
    subcx.w r6, -110(r4)
    calla #guarda
; bloque 005c
    mova #0x61546, r15
    mova #0xC00F8, r13
    mov #0x0, r2
    mov.w r13, r15
    calla #guarda
; bloque 005d
    mov #0x0800, r5
    mov #0x6, r2
    rrum.w #1, r5
    calla #guarda
; bloque 005e
    mova #0x7FFFE, r6
    mova #0x7FFFE, r13
    mov #0x6, r2
    mova r6, r13
    calla #guarda
; bloque 005f
    mova #zona+128, r15
    mova #zona+128, r5
    mov #0x2, r2
    andx.b -25(r5), 4(r15)
    calla #guarda
; bloque 0060
    mova #0xFFFFF, r4
    mova #0x40000, r5
    mova #0x9B8CF, r6
    mova #0xE1CE2, r7
    mova #0xFFFFF, r8
    mov #0x2, r2
    pushm.a #5, r8
    popm.a #5, r12
    calla #guarda
; bloque 0061
    mova #0x7FFFE, r12
    mov #0x6, r2
    bis.b #2, r12
    calla #guarda
; bloque 0062
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0063
    mova #0x471F0, r12
    mov #0x7, r2
    rrcm.w #2, r12
    calla #guarda
; bloque 0064
    mova #zona+176, r9
    mov #0x5, r2
    subx.w @r9, &zona+226
    calla #guarda
; bloque 0065
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0066
    mov #0x0040, r7
    mov #0x1, r2
    subx.a r7, zona+148
    calla #guarda
; bloque 0067
    mov #0, r15
    mov #0x3, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0068
    mova #zona+78, r8
    mov #0x0, r2
    rra.w @r8
    calla #guarda
; bloque 0069
    mova #0x234B7, r7
    mov #0x3, r2
    movx.a r7, &zona+62
    calla #guarda
; bloque 006a
    mov #0x6, r2
    bicx.w &zona+8, &zona+182
    calla #guarda
; bloque 006b
    mova #0x3FE20, r13
    mova #0xFFFFF, r12
    mov #0x0, r2
    rpt #11 { subx.a r13, r12
    calla #guarda
; bloque 006c
    mov #0x0000, r15
    mova #0x277CC, r13
    mov #0x0, r2
    cmpa r15, r13
    calla #guarda
; bloque 006d
    mova #0xAE2B8, r15
    mova #0x1EAA1, r12
    mov #0x7, r2
    mova r15, &zona+140
    calla #guarda
; bloque 006e
    mova #0x20000, r11
    mova #0x9C6F3, r8
    mov #0x2, r2
    cmpa r11, r8
    calla #guarda
; bloque 006f
    mova #zona+128, r8
    mov #0x6, r2
    rra.w 26(r8)
    calla #guarda
; bloque 0070
    mov #0x0000, r6
    mov #0x0000, r5
    mova #zona+168, r6
    mov #0x0, r2
    mova @r6+, r5
    calla #guarda
; bloque 0071
    mova #zona+194, r7
    mov #0x0, r2
    rrax.a @r7
    calla #guarda
; bloque 0072
    mov #0x0045, r11
    mov #0x7, r2
    dadd.b #1, r11
    calla #guarda
; bloque 0073
    mova #0x62071, r13
    mova #zona+122, r9
    mov #0x1, r2
    bit.b @r9, r13
    calla #guarda
; bloque 0074
    mov #0x0000, r15
    mov #0x3, r2
    pushm.a #1, r15
    popm.a #1, r6
    calla #guarda
; bloque 0075
    mova #0xFFFFF, r14
    mova #zona+226, r10
    mov #0x3, r2
    subcx.b @r10+, r14
    calla #guarda
; bloque 0076
    mova #0xC2D15, r6
    mova #0x5D083, r13
    mova #0x597E1, r7
    mov #0x7, r2
    rpt r7 { addx.a r6, r13
    calla #guarda
; bloque 0077
    mov #0x0000, r8
    mov #0x0, r2
    rram.w #3, r8
    calla #guarda
; bloque 0078
    mov #0x0029, r6
    mova #zona+128, r9
    mov #0x5, r2
    xor.w 42(r9), r6
    calla #guarda
; bloque 0079
    mova #zona+128, r4
    mova #0x3B56B, r15
    mov #0x4, r2
    subc.w r15, -108(r4)
    calla #guarda
; bloque 007a
    mova #0xFFFFF, r8
    mova #0xBC986, r9
    mova #0x7FFFF, r10
    mova #0xDB51A, r11
    mov #0x0000, r12
    mov #0x1, r2
    pushm.a #5, r12
    popm.a #5, r15
    calla #guarda
; bloque 007b
    mova #0x9A175, r12
    mov #0x0, r2
    rlam.a #3, r12
    calla #guarda
; bloque 007c
    mov #0x0000, r8
    mova #zona+128, r9
    mov #0x1, r2
    addcx.b 96(r9), r8
    calla #guarda
; bloque 007d
    mova #zona+248, r8
    mov #0x2, r2
    addx.w @r8, &zona+160
    calla #guarda
; bloque 007e
    mova #zona+128, r12
    mova #0xFFFFF, r11
    mov #0x6, r2
    subcx.b r11, 49(r12)
    calla #guarda
; bloque 007f
    mova #0x7FFFE, r9
    mov #0x6, r2
    rram.w #2, r9
    calla #guarda
; bloque 0080
    mova #zona+224, r15
    mov #0x5, r2
    subcx.a @r15, zona+64
    calla #guarda
; bloque 0081
    mov #0x1, r2
    rrux.b zona+172
    calla #guarda
; bloque 0082
    mov #0x7, r2
    pushx.w #0x0
    pop r15
    calla #guarda
; bloque 0083
    mova #zona+128, r11
    mov #0x4, r2
    rrux.a -90(r11)
    calla #guarda
; bloque 0084
    mova #0x57CDB, r9
    mov #0x3, r2
    movx.b r9, zona+248
    calla #guarda
; bloque 0085
    mov #0, r15
    mova #sub, r5
    mov #0x5, r2
    calla r5
    calla #guarda
; bloque 0086
    mova #0x70237, r14
    mov #0x0, r2
    movx.w #0xFFFF, r14
    calla #guarda
; bloque 0087
    mov #0x4, r2
    rrux.w &zona+214
    calla #guarda
; bloque 0088
    mova #0x97613, r9
    mov #0x2, r2
    rrax.b r9
    calla #guarda
; bloque 0089
    mov #0, r15
    mov #0x2, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 008a
    mova #0xB4D23, r7
    mov #0x6, r2
    bisx.w r7, zona+136
    calla #guarda
; bloque 008b
    mov #0x4, r2
    bicx.w #0x0, zona+162
    calla #guarda
; bloque 008c
    mov #0x5, r2
    subx.a zona+130, &zona+200
    calla #guarda
; bloque 008d
    mov #0x0000, r12
    mov #0x0, r2
    rpt #2 { rrux.a r12
    calla #guarda
; bloque 008e
    mov #0x0, r2
    pushx.b zona+12
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 008f
    mova #0x7FFFF, r8
    mov #0x5, r2
    rrcm.w #3, r8
    calla #guarda
; bloque 0090
    mova #0x4E425, r6
    mov #0x6, r2
    rlam.a #3, r6
    calla #guarda
; bloque 0091
    mov #0x0000, r7
    mov #0x4, r2
    subcx.w r7, zona+84
    calla #guarda
; bloque 0092
    mov #0x0000, r10
    mova #0xFFE32, r8
    mov #0x7A50, r7
    mov #0x4, r2
    rpt r7 { andx.b r10, r8
    calla #guarda
; bloque 0093
    mov #0x8B6E, r14
    mova #0xD4BFB, r15
    mov #0x7, r2
    pushm.a #2, r15
    popm.a #2, r14
    calla #guarda
; bloque 0094
    mov #0x2, r2
    addx.a &zona+88, &zona+46
    calla #guarda
; bloque 0095
    mova #0x549CF, r15
    mova #0xDE2E6, r10
    mova #0x7FFFE, r13
    mov #0x5, r2
    rpt r13 { andx.a r15, r10
    calla #guarda
; bloque 0096
    mova #zona+128, r8
    mov #0x2, r2
    movx.b #2, -114(r8)
    calla #guarda
; bloque 0097
    mova #0xFFFFF, r14
    mova #0x1070A, r7
    mov #0x7, r2
    suba #0x80000, r7
    calla #guarda
; bloque 0098
    mova #zona+128, r7
    mova #0xFF316, r12
    mov #0x0, r2
    addx.w r12, -80(r7)
    calla #guarda
; bloque 0099
    mova #zona+193, r13
    mov #0x2, r2
    bicx.b @r13, &zona+129
    calla #guarda
; bloque 009a
    mova #0x346E0, r10
    mova #0xCD102, r5
    mov #0x0, r2
    addcx.w r5, r10
    calla #guarda
; bloque 009b
    mova #0xFFFFF, r12
    mova #0xEB8DB, r11
    mova #zona+128, r12
    mov #0x3, r2
    mova 98(r12), r11
    calla #guarda
; bloque 009c
    mova #0xF9006, r13
    mov #0xF3E2, r10
    mova #0xFFFFF, r8
    mov #0x5, r2
    rpt r8 { bisx.b r13, r10
    calla #guarda
; bloque 009d
    mov #0, r15
    mov #0x2, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 009e
    mova #zona+128, r4
    mova #zona+179, r15
    mov #0x0, r2
    addc.b @r15, 64(r4)
    calla #guarda
; bloque 009f
    mova #0x16881, r12
    mov #0x6, r2
    movx.a r12, zona+170
    calla #guarda
; bloque 00a0
    mova #0x30427, r10
    mov #0x7, r2
    pushm.a #1, r10
    popm.a #1, r11
    calla #guarda
; bloque 00a1
    mova #0x38337, r15
    mov #0x1, r2
    addx.w r15, zona+220
    calla #guarda
; bloque 00a2
    mova #0xEB313, r5
    mova #0x71887, r14
    mov #0x1, r2
    rpt r14 { swpbx r5
    calla #guarda
; bloque 00a3
    mov #0x0001, r15
    mova #0xB9302, r5
    mov #0x3, r2
    mova #0x489EE, r5
    calla #guarda
; bloque 00a4
    mova #0xE7E3B, r4
    mova #0xFA38A, r12
    mov #0x1, r2
    rpt #5 { movx.b r4, r12
    calla #guarda
; bloque 00a5
    mov #0x83C6, r5
    mova #0x4775B, r8
    mova #0xFFFFF, r4
    mov #0x7, r2
    rpt r4 { addx.w r5, r8
    calla #guarda
; bloque 00a6
    mova #0x7B2C5, r8
    mova #zona+150, r13
    mov #0x2, r2
    addx.w @r13+, r8
    calla #guarda
; bloque 00a7
    mova #0xE66FD, r6
    mov #0x1, r2
    addcx.w #0, r6
    calla #guarda
; bloque 00a8
    mova #zona+128, r14
    mov #0x3, r2
    push.w 40(r14)
    pop r15
    calla #guarda
; bloque 00a9
    mov #0x0, r2
    rrax.w &zona+218
    calla #guarda
; bloque 00aa
    mov #0x0000, r14
    mova #0xF92F2, r10
    mova #zona+220, r14
    mov #0x5, r2
    mova @r14, r10
    calla #guarda
; bloque 00ab
    mova #0xC2603, r14
    mov #0x0, r2
    rra.w r14
    calla #guarda
; bloque 00ac
    mova #zona+139, r11
    mov #0x2, r2
    rrax.b @r11
    calla #guarda
; bloque 00ad
    mov #0x0, r2
    pushx.b #4
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 00ae
    mova #zona+96, r9
    mov #0x7, r2
    swpb @r9
    calla #guarda
; bloque 00af
    mov #0x0096, r8
    mov #0x0031, r15
    mova #0x86F91, r6
    mov #0x0, r2
    rpt r6 { daddx.b r8, r15
    calla #guarda
; bloque 00b0
    mov #0x0, r2
    subcx.a zona+6, zona+70
    calla #guarda
; bloque 00b1
    mova #0x80000, r11
    mova #0xE8B23, r12
    mov #0x5, r2
    rpt #6 { xorx.b r11, r12
    calla #guarda
; bloque 00b2
    mova #zona+150, r6
    mov #0x7, r2
    bisx.a @r6+, &zona+36
    calla #guarda
; bloque 00b3
    mov #0x0000, r15
    mova #0xFE987, r4
    mov #0x7, r2
    rpt r4 { rrux.b r15
    calla #guarda
; bloque 00b4
    mova #0x94C3B, r10
    mov #0x1, r2
    pushx.a r10
    popx.a r15
    calla #guarda
; bloque 00b5
    mov #0x6, r2
    movx.w &zona+68, zona+22
    calla #guarda
; bloque 00b6
    mova #0x55E94, r15
    mov #0x2, r2
    rram.w #1, r15
    calla #guarda
; bloque 00b7
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00b8
    mova #0xBBCFD, r6
    mova #zona+136, r9
    mov #0x1, r2
    bicx.b @r9+, r6
    calla #guarda
; bloque 00b9
    mov #0, r15
    mov #0x4, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00ba
    mov #0x0000, r8
    mova #0x6AA78, r7
    mov #0x6, r2
    adda #0x5CE86, r7
    calla #guarda
; bloque 00bb
    mova #0x7FFFE, r8
    mova #0xFFFFF, r14
    mov #0x1, r2
    add.b r14, r8
    calla #guarda
; bloque 00bc
    mov #0x1, r2
    rrux.a zona+18
    calla #guarda
; bloque 00bd
    mova #zona+128, r13
    mov #0x2, r2
    xor.w #8, 114(r13)
    calla #guarda
; bloque 00be
    mova #0x40EF2, r4
    mova #0xFFFFF, r5
    mova #0x1F43F, r6
    mova #0xBD47D, r7
    mov #0x6, r2
    pushm.a #4, r7
    popm.a #4, r8
    calla #guarda
; bloque 00bf
    mov #0x2, r2
    addcx.b &zona+70, zona+66
    calla #guarda
; bloque 00c0
    mov #0x7, r2
    rrux.a zona+202
    calla #guarda
; bloque 00c1
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00c2
    mova #0x4474D, r8
    mov #0x3, r2
    rrux.w r8
    calla #guarda
; bloque 00c3
    mova #zona+128, r5
    mov #0x7, r2
    addx.b #8, -35(r5)
    calla #guarda
; bloque 00c4
    mov #0x0008, r8
    mova #0x1678A, r10
    mov #0x1, r2
    rpt r10 { rrux.b r8
    calla #guarda
; bloque 00c5
    mov #0x0040, r11
    mov #0x0000, r12
    mova #0x9A5FC, r13
    mov #0x7, r2
    pushm.w #3, r13
    popm.w #3, r6
    calla #guarda
; bloque 00c6
    mova #0x36AC3, r11
    mov #0x4, r2
    rrum.a #2, r11
    calla #guarda
; bloque 00c7
    mova #0x57611, r8
    mova #0x7FFFE, r9
    mova #0x979CC, r10
    mova #0x7DFEF, r11
    mova #0x7FFFF, r12
    mov #0x1, r2
    pushm.w #5, r12
    popm.w #5, r15
    calla #guarda
; bloque 00c8
    mov #0x0496, r9
    mov #0x3695, r5
    mov #0x7, r2
    rpt #14 { daddx.w r9, r5
    calla #guarda
; bloque 00c9
    mova #zona+220, r4
    mov #0x4, r2
    pushx.w @r4
    pop r15
    calla #guarda
; bloque 00ca
    mov #0x7, r2
    rrax.b zona+55
    calla #guarda
; bloque 00cb
    mova #zona+128, r6
    mov #0x4, r2
    subcx.w #8, -104(r6)
    calla #guarda
; bloque 00cc
    mov #0x0018, r10
    mov #0x0017, r8
    mova #0x58666, r7
    mov #0x3, r2
    rpt r7 { daddx.b r10, r8
    calla #guarda
; bloque 00cd
    mova #zona+68, r9
    mov #0x1, r2
    addcx.a @r9, zona+114
    calla #guarda
; bloque 00ce
    mov #0x6, r2
    addx.w #0xE443, zona+204
    calla #guarda
; bloque 00cf
    mova #0x40638, r11
    mov #0x0020, r13
    mov #0x2, r2
    suba r11, r13
    calla #guarda
; bloque 00d0
    mov #0xFB3B, r9
    mova #0xB92E9, r8
    mova #zona+214, r9
    mov #0x0, r2
    mova @r9+, r8
    calla #guarda
; bloque 00d1
    mova #zona+128, r10
    mov #0x40AE, r11
    mov #0x7, r2
    add.b r11, 122(r10)
    calla #guarda
; bloque 00d2
    mova #0xF1E03, r14
    mova #0xA23FC, r4
    mov #0x5, r2
    rpt #14 { xorx.b r14, r4
    calla #guarda
; bloque 00d3
    mov #0x2000, r6
    mova #0x1B4CC, r15
    mov #0x3, r2
    bisx.w r15, r6
    calla #guarda
; bloque 00d4
    mov #0x5559, r6
    mov #0x9805, r10
    mov #0x4, r2
    dadd.w r10, r6
    calla #guarda
; bloque 00d5
    mova #zona+158, r6
    mov #0x5, r2
    rrc.w @r6
    calla #guarda
; bloque 00d6
    mov #0, r15
    mov #0x2, r2
    jl 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00d7
    mov #0x0200, r12
    mova #0x7FFFE, r15
    mov #0x6, r2
    rpt #10 { andx.w r12, r15
    calla #guarda
; bloque 00d8
    mov #0x6, r2
    rrax.w &zona+220
    calla #guarda
; bloque 00d9
    mova #zona+128, r8
    mova #zona+108, r15
    mov #0x2, r2
    add.w @r15, 46(r8)
    calla #guarda
; bloque 00da
    mov #0x0004, r15
    mov #0x2, r2
    swpbx r15
    calla #guarda
; bloque 00db
    mov #0x6147, r14
    mov #0x7, r2
    dadd.w #2, r14
    calla #guarda
; bloque 00dc
    mova #0x80000, r6
    mov #0x0001, r7
    mov #0x0080, r8
    mov #0x2, r2
    pushm.a #3, r8
    popm.a #3, r15
    calla #guarda
; bloque 00dd
    mova #zona+48, r12
    mov #0x3, r2
    andx.w @r12+, zona+160
    calla #guarda
; bloque 00de
    mov #0x4, r2
    swpbx zona+176
    calla #guarda
; bloque 00df
    mova #0x1C4A7, r12
    mova #0x5145C, r8
    mov #0x0000, r5
    mov #0x4, r2
    rpt r5 { movx.w r12, r8
    calla #guarda
; bloque 00e0
    mova #zona+138, r12
    mov #0x5, r2
    swpbx @r12
    calla #guarda
; bloque 00e1
    mova #0xD254B, r8
    mov #0x1, r2
    rrux.a r8
    calla #guarda
; bloque 00e2
    mova #0x22D3C, r15
    mov #0x1, r2
    cmpx.b zona+184, r15
    calla #guarda
; bloque 00e3
    mov #0x4, r2
    pushx.a zona+0
    popx.a r15
    calla #guarda
; bloque 00e4
    mov #0x0000, r7
    mov #0x0, r2
    bisx.w zona+140, r7
    calla #guarda
; bloque 00e5
    mov #0x0000, r12
    mov #0x6, r2
    rrcx.b r12
    calla #guarda
; bloque 00e6
    mova #zona+128, r6
    mov #0x3, r2
    bisx.w &zona+164, 114(r6)
    calla #guarda
; bloque 00e7
    mova #0x3780F, r15
    mov #0x1, r2
    xorx.b r15, &zona+7
    calla #guarda
; bloque 00e8
    mova #0x80000, r11
    mova #0x7429F, r12
    mova #0x22FFA, r13
    mova #0x39D58, r14
    mov #0x6, r2
    pushm.w #4, r14
    popm.w #4, r10
    calla #guarda
; bloque 00e9
    mov #0, r15
    mova #tabla_a-4, r7
    mov #0x5, r2
    calla 4(r7)
    calla #guarda
; bloque 00ea
    mov #0x1, r2
    movx.b #8, zona+11
    calla #guarda
; bloque 00eb
    mov #0, r15
    mov #0x7, r2
    jge 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00ec
    mova #0xC7369, r8
    mov #0x6, r2
    rrc.b r8
    calla #guarda
; bloque 00ed
    mova #0x7FFFE, r11
    mova #0x4B435, r5
    mov #0x1, r2
    rpt #14 { subx.b r11, r5
    calla #guarda
; bloque 00ee
    mova #zona+106, r15
    mov #0x3, r2
    addx.a @r15+, zona+152
    calla #guarda
; bloque 00ef
    mov #0, r15
    mova #sub, r5
    mov #0x4, r2
    calla r5
    calla #guarda
; bloque 00f0
    mova #0x80000, r8
    mov #0x5, r2
    swpbx r8
    calla #guarda
; bloque 00f1
    mova #0x7FFFF, r12
    mova #0xBC6CD, r8
    mova #zona+128, r8
    mov #0x2, r2
    mova r12, 74(r8)
    calla #guarda
; bloque 00f2
    mova #zona+128, r6
    mov #0x5, r2
    cmpx.b -113(r6), &zona+34
    calla #guarda
; bloque 00f3
    mova #0xAAFF1, r13
    mov #0x2, r2
    bis.w #0xFFFF, r13
    calla #guarda
; bloque 00f4
    mov #0xBF18, r12
    mov #0x1, r2
    subcx.a &zona+222, r12
    calla #guarda
; bloque 00f5
    mova #zona+128, r9
    mov #0x2, r2
    sub.w #1, -6(r9)
    calla #guarda
; bloque 00f6
    mov #0x4, r2
    pushx.b #0x77
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 00f7
    mova #zona+128, r9
    mov #0x3, r2
    bic.w #0x5426, -52(r9)
    calla #guarda
; bloque 00f8
    mov #0x6, r2
    swpbx zona+246
    calla #guarda
; bloque 00f9
    mova #0x4CA9D, r8
    mov #0x0000, r9
    mova #0xAE5D9, r10
    mov #0x5, r2
    pushm.a #3, r10
    popm.a #3, r7
    calla #guarda
; bloque 00fa
    mov #0, r15
    mova #tabla_a, r10
    mov #0x4, r2
    calla @r10
    calla #guarda
; bloque 00fb
    mov #0x0000, r12
    mov #0x5, r2
    rrcm.a #4, r12
    calla #guarda
; bloque 00fc
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00fd
    mova #zona+128, r10
    mov #0x2, r2
    addc.w #0xE837, -96(r10)
    calla #guarda
; bloque 00fe
    mova #0xFDF23, r13
    mova #0xD6F70, r14
    mov #0x0000, r15
    mov #0x3, r2
    pushm.w #3, r15
    popm.w #3, r8
    calla #guarda
; bloque 00ff
    mova #0x933D3, r7
    mova #zona+128, r8
    mov #0x5, r2
    sub.b -50(r8), r7
    calla #guarda
; bloque 0100
    mova #zona+181, r9
    mov #0x4, r2
    rrc.b @r9
    calla #guarda
; bloque 0101
    mova #0xFFFFF, r5
    mov #0x0000, r8
    mova #zona+166, r5
    mov #0x6, r2
    mova @r5+, r8
    calla #guarda
; bloque 0102
    mova #0x1BB6D, r7
    mov #0x7, r2
    addx.a r7, &zona+148
    calla #guarda
; bloque 0103
    mova #0xD2030, r4
    mov #0x3, r2
    rrc.w r4
    calla #guarda
; bloque 0104
    mova #0x7FFFE, r15
    mov #0x5, r2
    rlam.a #2, r15
    calla #guarda
; bloque 0105
    mova #0xE1425, r10
    mova #0xF4A46, r4
    mov #0x2, r2
    suba r10, r4
    calla #guarda
; bloque 0106
    mova #0xED455, r13
    mova #0x1535B, r6
    mov #0x3, r2
    rpt #8 { subx.a r13, r6
    calla #guarda
; bloque 0107
    mov #0x2379, r11
    mov #0x5, r2
    daddx.w #0x850, r11
    calla #guarda
; bloque 0108
    mova #zona+128, r8
    mova #0xB6A5D, r10
    mov #0x1, r2
    bic.w r10, 36(r8)
    calla #guarda
; bloque 0109
    mova #0x9DB33, r11
    mov #0x5, r2
    movx.a &zona+62, r11
    calla #guarda
; bloque 010a
    mova #0x20000, r4
    mova #0x7FFFF, r5
    mov #0x5, r2
    pushm.a #2, r5
    popm.a #2, r11
    calla #guarda
; bloque 010b
    mova #0xADCEF, r8
    mov #0x2, r2
    push.w r8
    pop r15
    calla #guarda
; bloque 010c
    mova #0x7FFFE, r15
    mova #0xEB5A7, r10
    mov #0x6, r2
    mova r15, r10
    calla #guarda
; bloque 010d
    mova #zona+128, r12
    mov #0x7, r2
    bisx.a -62(r12), zona+102
    calla #guarda
; bloque 010e
    mova #0x12EB8, r10
    mov #0x4, r2
    rrum.w #3, r10
    calla #guarda
; bloque 010f
    mova #zona+128, r12
    mova #zona+124, r11
    mov #0x5, r2
    addx.w @r11, 58(r12)
    calla #guarda
; bloque 0110
    mova #0x8A4AB, r5
    mov #0x7, r2
    rrum.w #1, r5
    calla #guarda
; bloque 0111
    mova #zona+118, r15
    mov #0x3, r2
    rrux.a @r15
    calla #guarda
; bloque 0112
    mov #0x4, r2
    bicx.w zona+38, &zona+140
    calla #guarda
; bloque 0113
    mova #0xC8081, r13
    mova #0xE100B, r9
    mova #zona+80, r13
    mov #0x2, r2
    mova @r13+, r9
    calla #guarda
; bloque 0114
    mov #0x5, r2
    xorx.a &zona+98, zona+28
    calla #guarda
; bloque 0115
    mov #0, r15
    mov #0x2, r2
    call #subw
    calla #guarda
; bloque 0116
    mov #0x1, r2
    swpbx &zona+84
    calla #guarda
; bloque 0117
    mov #0, r15
    mova #tabla_a-4, r13
    mov #0x7, r2
    calla 4(r13)
    calla #guarda
; bloque 0118
    mova #0x7B084, r5
    mov #0x5, r2
    pushm.a #1, r5
    popm.a #1, r9
    calla #guarda
; bloque 0119
    mova #0x8823F, r14
    mova #0x2191B, r11
    mov #0x4, r2
    suba #0x19FB2, r11
    calla #guarda
; bloque 011a
    mova #0xAA0C2, r9
    mova #0x65AA1, r13
    mov #0x1, r2
    rpt #3 { addcx.w r9, r13
    calla #guarda
; bloque 011b
    mova #0xBC0DC, r9
    mova #0xFFC93, r10
    mov #0x0000, r11
    mov #0x3, r2
    pushm.w #3, r11
    popm.w #3, r14
    calla #guarda
; bloque 011c
    mov #0x7, r2
    rrax.w &zona+180
    calla #guarda
; bloque 011d
    mov #0x0002, r5
    mova #0xE45D1, r7
    mov #0x4, r2
    mova #0x7561D, r7
    calla #guarda
; bloque 011e
    mova #0xCDEF1, r5
    mov #0x2, r2
    bicx.b #8, r5
    calla #guarda
; bloque 011f
    mova #zona+128, r15
    mov #0x1, r2
    addc.b #0xEF, 42(r15)
    calla #guarda
; bloque 0120
    mova #0x26F3E, r15
    mova #0x6EBCF, r13
    mov #0x1, r2
    cmpa r15, r13
    calla #guarda
; bloque 0121
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0122
    mov #0x0000, r11
    mova #0x5202D, r12
    mov #0x0000, r13
    mova #0xF58E5, r14
    mova #0x438DA, r15
    mov #0x3, r2
    pushm.w #5, r15
    popm.w #5, r10
    calla #guarda
; bloque 0123
    mova #0x8D1AC, r6
    mova #0xC480A, r7
    mova #0xED4B6, r8
    mov #0x0000, r9
    mova #0x8B840, r10
    mov #0x0, r2
    pushm.a #5, r10
    popm.a #5, r10
    calla #guarda
; bloque 0124
    mov #0x4, r2
    rrcx.w zona+236
    calla #guarda
; bloque 0125
    mov #0x0, r2
    rrax.b &zona+110
    calla #guarda
; bloque 0126
    mov #0x3, r2
    pushx.a zona+188
    popx.a r15
    calla #guarda
; bloque 0127
    mova #zona+128, r11
    mov #0x2, r2
    xorx.a -8(r11), &zona+64
    calla #guarda
; bloque 0128
    mov #0x5878, r12
    mov #0x4660, r13
    mov #0x3, r2
    daddx.w r13, r12
    calla #guarda
; bloque 0129
    mova #0xFFFFF, r8
    mov #0x0000, r9
    mov #0x1, r2
    adda #0x72D44, r9
    calla #guarda
; bloque 012a
    mova #0xFFFFF, r7
    mov #0xE769, r6
    mov #0x6, r2
    rpt r6 { swpbx r7
    calla #guarda
; bloque 012b
    mov #0x2, r2
    push.w #0xF0C5
    pop r15
    calla #guarda
; bloque 012c
    mova #0xD027A, r11
    mova #zona+128, r4
    mov #0x3, r2
    addcx.w 40(r4), r11
    calla #guarda
; bloque 012d
    mova #0x89218, r13
    mov #0x4, r2
    cmpx.a &zona+206, r13
    calla #guarda
; bloque 012e
    mova #zona+128, r14
    mova #zona+77, r4
    mov #0x2, r2
    bitx.b @r4, 22(r14)
    calla #guarda
; bloque 012f
    mov #0x2, r2
    addcx.b #0x41, zona+141
    calla #guarda
; bloque 0130
    mova #zona+128, r8
    mov #0x1, r2
    bisx.b zona+38, -37(r8)
    calla #guarda
; bloque 0131
    mova #zona+128, r7
    mova #zona+44, r11
    mov #0x4, r2
    andx.a @r11+, 82(r7)
    calla #guarda
; bloque 0132
    mov #0x0FE3, r13
    mov #0x4, r2
    bitx.w zona+14, r13
    calla #guarda
; bloque 0133
    mova #0x855A6, r11
    mova #0x547F1, r12
    mova #0x7FFFE, r13
    mova #0xCEAB0, r14
    mov #0x0, r2
    pushm.a #4, r14
    popm.a #4, r12
    calla #guarda
; bloque 0134
    mov #0x0031, r4
    mov #0x0041, r13
    mov #0x1, r2
    daddx.b r13, r4
    calla #guarda
; bloque 0135
    mova #0xF87A5, r13
    mov #0x0, r2
    addx.b &zona+148, r13
    calla #guarda
; bloque 0136
    mova #0xDBFAB, r6
    mov #0x6, r2
    rrum.w #4, r6
    calla #guarda
; bloque 0137
    mov #0x0000, r9
    mov #0x6, r2
    mov.w #4, r9
    calla #guarda
; bloque 0138
    mova #0x19E9A, r13
    mova #zona+130, r5
    mov #0x3, r2
    bitx.b @r5, r13
    calla #guarda
; bloque 0139
    mov #0x0000, r9
    mova #0xB0087, r4
    mov #0x7, r2
    suba r9, r4
    calla #guarda
; bloque 013a
    mov #0x0000, r9
    mova #0x7FFFE, r7
    mova #0xA2D43, r5
    mov #0x5, r2
    rpt r5 { bicx.a r9, r7
    calla #guarda
; bloque 013b
    mova #0x7E67C, r7
    mov #0x2, r2
    rra.b r7
    calla #guarda
; bloque 013c
    mova #zona+128, r15
    mov #0x2, r2
    rrcx.w -102(r15)
    calla #guarda
; bloque 013d
    mova #0x29954, r5
    mov #0x6157, r14
    mova #0xFFFFF, r15
    mov #0x1, r2
    rpt r15 { bicx.a r5, r14
    calla #guarda
; bloque 013e
    mov #0x4370, r11
    mov #0x0685, r12
    mova #0xEE225, r9
    mov #0x3, r2
    rpt r9 { daddx.w r11, r12
    calla #guarda
; bloque 013f
    mova #0xFD1D1, r9
    mov #0x1, r2
    rram.a #3, r9
    calla #guarda
; bloque 0140
    mova #0x49788, r11
    mova #0xDEB56, r15
    mov #0x4, r2
    mova r11, &zona+196
    calla #guarda
; bloque 0141
    mova #0xEF42B, r10
    mova #0x3A391, r12
    mov #0x7, r2
    bicx.w r12, r10
    calla #guarda
; bloque 0142
    mov #0x0057, r9
    mov #0x7, r2
    dadd.b #2, r9
    calla #guarda
; bloque 0143
    mova #0x45553, r13
    mova #0x2A3F8, r6
    mov #0x6, r2
    rpt #9 { andx.w r13, r6
    calla #guarda
; bloque 0144
    mov #0x1, r2
    addx.a #0x7FFFE, zona+214
    calla #guarda
; bloque 0145
    mova #0x7FFFE, r6
    mova #0x2FB5E, r8
    mova #zona+16, r6
    mov #0x6, r2
    mova @r6, r8
    calla #guarda
; bloque 0146
    mova #0xA5002, r4
    mova #0xACB2E, r10
    mova #zona+222, r4
    mov #0x5, r2
    mova @r4+, r10
    calla #guarda
; bloque 0147
    mova #0xBB547, r14
    mova #0x88BB1, r12
    mova #zona+72, r14
    mov #0x0, r2
    mova @r14+, r12
    calla #guarda
; bloque 0148
    mov #0, r15
    mova #sub, r13
    mov #0x0, r2
    calla r13
    calla #guarda
; bloque 0149
    mov #0x6870, r4
    mov #0x3, r2
    dadd.w #2, r4
    calla #guarda
; bloque 014a
    mova #0xF4F40, r6
    mova #0xFFFFF, r9
    mova #0xAFB00, r5
    mov #0x4, r2
    rpt r5 { bisx.a r6, r9
    calla #guarda
; bloque 014b
    mov #0x0027, r7
    mov #0x7, r2
    dadd.b #1, r7
    calla #guarda
; bloque 014c
    mov #0x0000, r9
    mova #0x6CB96, r15
    mov #0x4, r2
    rpt #1 { addx.b r9, r15
    calla #guarda
; bloque 014d
    mov #0x0000, r5
    mov #0x5, r2
    bisx.a &zona+26, r5
    calla #guarda
; bloque 014e
    mov #0x6, r2
    push.b #0
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 014f
    mova #0xFFFFF, r15
    mov #0x6, r2
    addx.a r15, zona+172
    calla #guarda
; bloque 0150
    mov #0x5, r2
    push.b #0x0
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 0151
    mova #0x87B50, r6
    mova #0xBCD75, r8
    mova #zona+84, r6
    mov #0x2, r2
    mova @r6+, r8
    calla #guarda
; bloque 0152
    mov #0x0000, r12
    mov #0x0, r2
    rlam.a #3, r12
    calla #guarda
; bloque 0153
    mov #0x0400, r6
    mova #0x7FFFF, r13
    mova #zona+128, r13
    mov #0x2, r2
    mova r6, -32(r13)
    calla #guarda
; bloque 0154
    mova #0xFA37C, r7
    mov #0x1, r2
    rrux.b r7
    calla #guarda
; bloque 0155
    mova #0x729F1, r12
    mov #0x0000, r5
    mov #0x5, r2
    mova &zona+250, r5
    calla #guarda
; bloque 0156
    mov #0x8261, r15
    mova #0x1044D, r7
    mov #0x4, r2
    mova r15, &zona+70
    calla #guarda
; bloque 0157
    mova #0xFFFFF, r7
    mov #0x0000, r9
    mov #0x3, r2
    rpt #6 { addx.w r7, r9
    calla #guarda
; bloque 0158
    mov #0x2, r2
    push.w #0xBF00
    pop r15
    calla #guarda
; bloque 0159
    mov #0x4, r2
    push.w #0x8E56
    pop r15
    calla #guarda
; bloque 015a
    mova #0x1D9D8, r4
    mova #0x41DF5, r10
    mov #0x7, r2
    subc.w r10, r4
    calla #guarda
; bloque 015b
    mova #0xBCA4B, r7
    mova #0x6D60D, r8
    mova #zona+128, r8
    mov #0x7, r2
    mova r7, 38(r8)
    calla #guarda
; bloque 015c
    mova #zona+128, r13
    mov #0x0, r2
    bisx.a #0x33590, 70(r13)
    calla #guarda
; bloque 015d
    mova #zona+212, r7
    mov #0x7, r2
    subx.a @r7, zona+206
    calla #guarda
; bloque 015e
    mov #0x0000, r10
    mov #0x0000, r13
    mov #0x6, r2
    addx.b r13, r10
    calla #guarda
; bloque 015f
    mova #zona+128, r8
    mova #zona+128, r9
    mov #0x7, r2
    movx.a 46(r9), 40(r8)
    calla #guarda
; bloque 0160
    mov #0x7, r2
    rrax.b &zona+195
    calla #guarda
; bloque 0161
    mov #0, r15
    mova #sub, r14
    mov #0x2, r2
    calla r14
    calla #guarda
; bloque 0162
    mov #0x3, r2
    pushx.a #2
    popx.a r15
    calla #guarda
; bloque 0163
    mova #zona+206, r11
    mov #0x4, r2
    swpb @r11
    calla #guarda
; bloque 0164
    mova #0x73B74, r14
    mova #0xE197A, r9
    mov #0x1, r2
    adda #0x1156D, r9
    calla #guarda
; bloque 0165
    mova #0x20000, r11
    mov #0x8C6D, r9
    mova #zona+180, r11
    mov #0x1, r2
    mova @r11+, r9
    calla #guarda
; bloque 0166
    mova #0xFFFFF, r10
    mov #0x8B58, r11
    mov #0x7, r2
    cmpa #0x80829, r11
    calla #guarda
; bloque 0167
    mov #0, r15
    mov #0x0, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0168
    mova #0x9392A, r14
    mova #0x52ED6, r13
    mova #zona+128, r13
    mov #0x4, r2
    mova r14, -118(r13)
    calla #guarda
; bloque 0169
    mov #0x6, r2
    pushx.a zona+60
    popx.a r15
    calla #guarda
; bloque 016a
    mova #0x4DD9A, r12
    mov #0x2, r2
    rrum.w #4, r12
    calla #guarda
; bloque 016b
    mova #zona+128, r5
    mova #zona+128, r11
    mov #0x5, r2
    sub.b -105(r11), -87(r5)
    calla #guarda
; bloque 016c
    mov #0, r15
    mov #0x4, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 016d
    mov #0x9749, r14
    mov #0x9254, r9
    mov #0x2, r2
    rpt #6 { daddx.w r14, r9
    calla #guarda
; bloque 016e
    mov #0x0, r2
    pushx.b &zona+61
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 016f
    mova #zona+128, r6
    mov #0x5, r2
    rra.w 90(r6)
    calla #guarda
; bloque 0170
    mova #0xFFFFF, r13
    mova #0x7FFFF, r15
    mov #0x6, r2
    mova r13, r15
    calla #guarda
; bloque 0171
    mov #0, r15
    mov #0x3, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0172
    mov #0x0000, r4
    mova #0xDB4CD, r11
    mov #0x2, r2
    adda #0xFFFFF, r11
    calla #guarda
; bloque 0173
    mova #0xFFFFF, r9
    mov #0x7, r2
    rra.b r9
    calla #guarda
; bloque 0174
    mova #0x3B485, r9
    mov #0x1, r2
    bisx.w r9, &zona+64
    calla #guarda
; bloque 0175
    mova #zona+128, r10
    mov #0x6, r2
    subcx.w #0x837D, 120(r10)
    calla #guarda
; bloque 0176
    mov #0x1, r2
    rrcx.a &zona+34
    calla #guarda
; bloque 0177
    mova #zona+128, r13
    mov #0x4, r2
    xorx.w zona+46, -94(r13)
    calla #guarda
; bloque 0178
    mova #0x7FFFE, r10
    mov #0x1, r2
    pushm.w #1, r10
    popm.w #1, r11
    calla #guarda
; bloque 0179
    mova #0xB0FA6, r8
    mov #0x5, r2
    xor.w r8, r8
    calla #guarda
; bloque 017a
    mova #0x20E0A, r9
    mova #0x12BEC, r8
    mov #0x0, r2
    mova &zona+148, r8
    calla #guarda
; bloque 017b
    mova #0x186B5, r4
    mov #0x0040, r13
    mova #zona+128, r4
    mov #0x2, r2
    mova 100(r4), r13
    calla #guarda
; bloque 017c
    mova #zona+182, r4
    mov #0x6, r2
    xorx.w @r4+, &zona+184
    calla #guarda
; bloque 017d
    mov #0x0000, r12
    mov #0xE075, r13
    mov #0x3, r2
    mova r12, &zona+24
    calla #guarda
; bloque 017e
    mova #0xADAF4, r12
    mov #0xF60B, r8
    mova #zona+64, r12
    mov #0x3, r2
    mova @r12+, r8
    calla #guarda
; bloque 017f
    mova #0x7FFFE, r12
    mova #0x80000, r7
    mova #zona+222, r12
    mov #0x0, r2
    mova @r12, r7
    calla #guarda
; bloque 0180
    mov #0x6, r2
    bicx.b #2, &zona+8
    calla #guarda
; bloque 0181
    mova #zona+22, r10
    mov #0x0, r2
    rra.w @r10
    calla #guarda
; bloque 0182
    mov #0, r15
    mova #tabla_a, r13
    mov #0x4, r2
    calla @r13
    calla #guarda
; bloque 0183
    mova #0x7FFFE, r8
    mov #0x1000, r13
    mova #zona+88, r8
    mov #0x2, r2
    mova @r8, r13
    calla #guarda
; bloque 0184
    mov #0, r15
    mov #0x3, r2
    jn 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0185
    mova #0x7FFFF, r11
    mov #0x6, r2
    rrum.a #4, r11
    calla #guarda
; bloque 0186
    mova #0xFFFFF, r7
    mova #0x80000, r6
    mov #0x7, r2
    cmpa #0xFFFFF, r6
    calla #guarda
; bloque 0187
    mova #0xEC1CF, r15
    mov #0x2, r2
    addcx.a #0, r15
    calla #guarda
; bloque 0188
    mov #0x1, r2
    rrcx.b &zona+198
    calla #guarda
; bloque 0189
    mov #0, r15
    mov #0x4, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 018a
    mova #0xB75F9, r12
    mova #zona+158, r11
    mov #0x5, r2
    subcx.a @r11, r12
    calla #guarda
; bloque 018b
    mov #0x0000, r5
    mova #0xD47F3, r7
    mova #zona+36, r5
    mov #0x4, r2
    mova @r5+, r7
    calla #guarda
; bloque 018c
    mova #0x7FFFF, r14
    mov #0x1, r2
    rrcm.w #2, r14
    calla #guarda
; bloque 018d
    mova #0x5CFB4, r13
    mova #0xFFFFF, r14
    mov #0x7, r2
    pushm.w #2, r14
    popm.w #2, r15
    calla #guarda
; bloque 018e
    mov #0x0000, r11
    mova #0x80000, r14
    mov #0x0, r2
    mova r11, r14
    calla #guarda
; bloque 018f
    mov #0x5563, r9
    mov #0x5, r2
    daddx.w #0x9039, r9
    calla #guarda
; bloque 0190
    mova #0x53004, r9
    mov #0x2, r2
    andx.a &zona+128, r9
    calla #guarda
; bloque 0191
    mova #0x545BD, r13
    mova #zona+128, r9
    mov #0x2, r2
    bisx.w -92(r9), r13
    calla #guarda
; bloque 0192
    mova #zona+160, r4
    mov #0x3, r2
    subcx.w @r4, zona+18
    calla #guarda
; bloque 0193
    mova #0x73643, r8
    mov #0x3, r2
    subx.b r8, &zona+216
    calla #guarda
; bloque 0194
    mova #0x6E3D6, r13
    mov #0x0000, r14
    mov #0x0000, r5
    mov #0x7, r2
    rpt r5 { subcx.b r13, r14
    calla #guarda
; bloque 0195
    mova #zona+116, r5
    mov #0x6, r2
    rra.b @r5
    calla #guarda
; bloque 0196
    mov #0x1, r2
    addx.b &zona+84, &zona+92
    calla #guarda
; bloque 0197
    mova #zona+227, r15
    mov #0x7, r2
    rrax.b @r15
    calla #guarda
; bloque 0198
    mov #0x1, r2
    xorx.b &zona+186, zona+107
    calla #guarda
; bloque 0199
    mova #0xCCC31, r4
    mov #0x6, r2
    rra.w r4
    calla #guarda
; bloque 019a
    mova #zona+128, r9
    mova #0x8CEFC, r13
    mov #0x7, r2
    bic.b r13, -54(r9)
    calla #guarda
; bloque 019b
    mova #0xD2D98, r10
    mov #0x5, r2
    bisx.b zona+36, r10
    calla #guarda
; bloque 019c
    mova #zona+128, r12
    mova #zona+184, r5
    mov #0x5, r2
    subx.b @r5, 8(r12)
    calla #guarda
; bloque 019d
    mov #0x8000, r8
    mova #0xC7125, r9
    mov #0x2, r2
    pushm.w #2, r9
    popm.w #2, r12
    calla #guarda
; bloque 019e
    mova #0x21424, r8
    mov #0x0000, r9
    mov #0x4, r2
    pushm.a #2, r9
    popm.a #2, r11
    calla #guarda
; bloque 019f
    mov #0x2, r2
    rrcx.b &zona+212
    calla #guarda
; bloque 01a0
    mov #0x0000, r8
    mova #zona+128, r11
    mov #0x2, r2
    cmp.b 96(r11), r8
    calla #guarda
; bloque 01a1
    mova #zona+128, r5
    mova #zona+128, r10
    mov #0x6, r2
    bicx.b -43(r10), -53(r5)
    calla #guarda
; bloque 01a2
    mova #0xA2C75, r15
    mova #0xFFFFF, r8
    mov #0x4, r2
    rpt r8 { rrux.w r15
    calla #guarda
; bloque 01a3
    mova #zona+36, r4
    mov #0x2, r2
    swpbx @r4
    calla #guarda
; bloque 01a4
    mova #zona+128, r10
    mov #0x3, r2
    xor.w #0xBC04, 78(r10)
    calla #guarda
; bloque 01a5
    mova #0xDC3D5, r9
    mova #0x93806, r13
    mov #0x3, r2
    rpt #15 { xorx.a r9, r13
    calla #guarda
; bloque 01a6
    mova #zona+128, r13
    mov #0x1, r2
    xorx.a #8, -84(r13)
    calla #guarda
; bloque 01a7
    mova #0xFFFFF, r7
    mova #0xAD129, r8
    mova #0x2967D, r9
    mov #0x0000, r10
    mov #0x4, r2
    pushm.a #4, r10
    popm.a #4, r14
    calla #guarda
; bloque 01a8
    mov #0x0027, r9
    mov #0x0, r2
    daddx.b #2, r9
    calla #guarda
; bloque 01a9
    mov #0x0000, r6
    mova #0xFFFFF, r11
    mov #0x1, r2
    rpt #11 { bisx.b r6, r11
    calla #guarda
; bloque 01aa
    mova #zona+196, r5
    mov #0x0, r2
    swpb @r5
    calla #guarda
; bloque 01ab
    mova #0x80BDE, r9
    mov #0x4, r2
    rra.b r9
    calla #guarda
; bloque 01ac
    mov #0x9215, r6
    mov #0x7, r2
    daddx.w #8, r6
    calla #guarda
; bloque 01ad
    mova #0x64CDB, r8
    mova #0x7FFFE, r5
    mov #0x5, r2
    cmp.b r5, r8
    calla #guarda
; bloque 01ae
    mova #0x7FFFF, r11
    mov #0x3, r2
    rpt #2 { rrux.b r11
    calla #guarda
; bloque 01af
    mov #0x53E4, r5
    mov #0x0, r2
    rra.b r5
    calla #guarda
; bloque 01b0
    mov #0x1, r2
    rrux.a zona+14
    calla #guarda
; bloque 01b1
    mov #0x1, r2
    bitx.a &zona+136, zona+96
    calla #guarda
; bloque 01b2
    mova #0x9877B, r12
    mov #0x6, r2
    rrcm.w #1, r12
    calla #guarda
; bloque 01b3
    mov #0x0000, r10
    mova #0x7FFFF, r5
    mov #0x0000, r12
    mov #0x1, r2
    rpt r12 { bicx.w r10, r5
    calla #guarda
; bloque 01b4
    mova #0xD3E72, r7
    mov #0x2, r2
    andx.a r7, zona+48
    calla #guarda
; bloque 01b5
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 01b6
    mova #0x7EDBE, r13
    mov #0x0200, r14
    mov #0x0800, r15
    mov #0x2, r2
    pushm.a #3, r15
    popm.a #3, r12
    calla #guarda
; bloque 01b7
    mov #0x2664, r13
    mov #0x3, r2
    daddx.w #0x749, r13
    calla #guarda
; bloque 01b8
    mova #0xC719A, r4
    mova #0x853BD, r11
    mov #0x0040, r8
    mov #0x3, r2
    rpt r8 { addcx.w r4, r11
    calla #guarda
; bloque 01b9
    mova #0xEA527, r9
    mov #0x5, r2
    subx.a zona+182, r9
    calla #guarda
; bloque 01ba
    mova #zona+145, r4
    mov #0x6, r2
    cmpx.b @r4+, &zona+8
    calla #guarda
; bloque 01bb
    mov #0x0800, r4
    mov #0x0001, r9
    mov #0x0, r2
    mova r4, r9
    calla #guarda
; bloque 01bc
    mova #0x80000, r6
    mova #0xD08D6, r13
    mov #0x2, r2
    mova #0x00000, r13
    calla #guarda
; bloque 01bd
    mova #0x1E717, r6
    mova #0xAAA6B, r7
    mov #0x0000, r8
    mov #0x1, r2
    pushm.a #3, r8
    popm.a #3, r10
    calla #guarda
; bloque 01be
    mov #0, r15
    mov #0x2, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 01bf
    mov #0x0000, r5
    mov #0x3, r2
    movx.w zona+214, r5
    calla #guarda
; bloque 01c0
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 01c1
    mova #0x2F2B5, r8
    mov #0x1, r2
    pushx.w r8
    pop r15
    calla #guarda
    movx.a &sp_guardado, r1
    popm.a #7, r10
fin:
    reta
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
    reta
sub:
    mov #0x1234, r15
    reta
subw:
    mov #0x1234, r15
    ret
    .section .upper.data,"aw"
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
