    .text
    .global prueba
    .global fin
prueba:
    pushm.a #7, r10
    movx.a r1, &sp_guardado
    mova #0xE0000, r1          ; la pila, por encima de 64 KB
    movx.a #res, &resptr
; bloque 0000
    mova #0x47788, r5
    mova #0x10F48, r6
    mov #0x2, r2
    add.w r6, r5
    calla #guarda
; bloque 0001
    mov #0x0000, r5
    mov #0x6, r2
    pushx.b r5
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 0002
    mova #0x1EBF4, r10
    mova #zona+244, r4
    mov #0x6, r2
    cmp.w @r4+, r10
    calla #guarda
; bloque 0003
    mova #0xE09AB, r5
    mov #0x2, r2
    rrum.a #3, r5
    calla #guarda
; bloque 0004
    mova #zona+128, r8
    mova #0xF6B1A, r9
    mov #0x0, r2
    xor.b r9, -42(r8)
    calla #guarda
; bloque 0005
    mova #0x9714C, r6
    mov #0x0000, r12
    mov #0x6, r2
    rpt #12 { addcx.w r6, r12
    calla #guarda
; bloque 0006
    mova #0xE3ACA, r11
    mov #0x3, r2
    pushm.a #1, r11
    popm.a #1, r7
    calla #guarda
; bloque 0007
    mova #zona+128, r15
    mova #zona+218, r6
    mov #0x3, r2
    xorx.a @r6+, -88(r15)
    calla #guarda
; bloque 0008
    mova #0xFFFFF, r11
    mova #0xFECD3, r15
    mov #0x4, r2
    cmp.b r15, r11
    calla #guarda
; bloque 0009
    mov #0x0000, r9
    mova #0x7FFFF, r7
    mov #0x3, r2
    mova #0xFFFFF, r7
    calla #guarda
; bloque 000a
    mova #0xCD842, r12
    mov #0x2, r2
    bis.w #0x40, r12
    calla #guarda
; bloque 000b
    mova #zona+190, r7
    mov #0x0, r2
    rrax.b @r7
    calla #guarda
; bloque 000c
    mova #0x7FFFF, r7
    mova #zona+52, r11
    mov #0x2, r2
    xorx.w @r11+, r7
    calla #guarda
; bloque 000d
    mova #0x8C54F, r4
    mov #0x3, r2
    pushm.w #1, r4
    popm.w #1, r11
    calla #guarda
; bloque 000e
    mov #0x5545, r7
    mov #0x3139, r5
    mov #0x4, r2
    daddx.w r5, r7
    calla #guarda
; bloque 000f
    mov #0, r15
    mova #tabla, r10
    mov #0x7, r2
    call @r10
    calla #guarda
; bloque 0010
    mova #zona+128, r8
    mov #0x1, r2
    rrc.b -59(r8)
    calla #guarda
; bloque 0011
    mov #0x0, r2
    rrax.a &zona+138
    calla #guarda
; bloque 0012
    mova #0x9CB3B, r9
    mova #0x84F66, r12
    mov #0x6, r2
    mova r9, r12
    calla #guarda
; bloque 0013
    mova #0x7A90C, r13
    mov #0x1, r2
    pushm.w #1, r13
    popm.w #1, r7
    calla #guarda
; bloque 0014
    mova #0xDAEEB, r15
    mov #0x2, r2
    add.w #1, r15
    calla #guarda
; bloque 0015
    mov #0x0000, r11
    mova #0xF25A3, r12
    mova #0xF5C8E, r13
    mov #0x1BC0, r14
    mova #0xAD801, r15
    mov #0x5, r2
    pushm.a #5, r15
    popm.a #5, r10
    calla #guarda
; bloque 0016
    mov #0x0002, r10
    mova #zona+128, r12
    mov #0x5, r2
    bisx.w 46(r12), r10
    calla #guarda
; bloque 0017
    mova #zona+128, r12
    mov #0x7, r2
    rrux.w -68(r12)
    calla #guarda
; bloque 0018
    mova #0x80000, r6
    mov #0x0400, r7
    mov #0x6, r2
    pushm.w #2, r7
    popm.w #2, r15
    calla #guarda
; bloque 0019
    mova #0x85160, r6
    mov #0x1, r2
    bit.b #1, r6
    calla #guarda
; bloque 001a
    mov #0x0000, r12
    mova #zona+204, r13
    mov #0x6, r2
    xor.b @r13+, r12
    calla #guarda
; bloque 001b
    mova #zona+80, r8
    mov #0x4, r2
    rrax.b @r8
    calla #guarda
; bloque 001c
    mova #0x5B17C, r15
    mova #0x20000, r7
    mov #0x0, r2
    adda #0xE1FFF, r7
    calla #guarda
; bloque 001d
    mova #0x48DDF, r11
    mova #zona+108, r12
    mov #0x2, r2
    add.w @r12, r11
    calla #guarda
; bloque 001e
    mova #0x6632D, r9
    mova #0xAC0A6, r10
    mov #0x6, r2
    adda #0x602C1, r10
    calla #guarda
; bloque 001f
    mova #0x9424B, r5
    mov #0x7, r2
    add.b #1, r5
    calla #guarda
; bloque 0020
    mov #0x5664, r7
    mov #0x8046, r5
    mov #0x0000, r6
    mov #0x3, r2
    rpt r6 { daddx.w r7, r5
    calla #guarda
; bloque 0021
    mova #zona+128, r7
    mova #0xB7D24, r13
    mov #0x2, r2
    bicx.w r13, -66(r7)
    calla #guarda
; bloque 0022
    mov #0, r15
    mova #sub, r4
    mov #0x6, r2
    calla r4
    calla #guarda
; bloque 0023
    mov #0x4000, r11
    mov #0x7, r2
    and.w #0xFFFF, r11
    calla #guarda
; bloque 0024
    mov #0, r15
    mova #sub, r11
    mov #0x5, r2
    calla r11
    calla #guarda
; bloque 0025
    mova #0xAC3ED, r4
    mov #0x4, r2
    rrux.b r4
    calla #guarda
; bloque 0026
    mova #zona+128, r12
    mov #0x4, r2
    rrux.b -3(r12)
    calla #guarda
; bloque 0027
    mova #zona+128, r5
    mov #0x3, r2
    rrc.b -57(r5)
    calla #guarda
; bloque 0028
    mov #0x3, r2
    pushx.w zona+8
    pop r15
    calla #guarda
; bloque 0029
    mov #0x0000, r10
    mov #0x6, r2
    rrcm.w #4, r10
    calla #guarda
; bloque 002a
    mov #0, r15
    mov #0x4, r2
    jl 1f
    mov #1, r15
1:
    calla #guarda
; bloque 002b
    mova #0xFFFFF, r5
    mov #0x6, r2
    addc.b #0x22, r5
    calla #guarda
; bloque 002c
    mov #0x6, r2
    swpbx zona+96
    calla #guarda
; bloque 002d
    mov #0x0000, r5
    mova #0x7FFFF, r6
    mov #0x5, r2
    subx.b r6, r5
    calla #guarda
; bloque 002e
    mov #0, r15
    mov #0x0, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 002f
    mova #0x7404E, r14
    mov #0xC05A, r15
    mov #0x0, r2
    pushm.w #2, r15
    popm.w #2, r12
    calla #guarda
; bloque 0030
    mov #0x2072, r13
    mov #0x9991, r7
    mova #0xBEFA1, r11
    mov #0x7, r2
    rpt r11 { daddx.w r13, r7
    calla #guarda
; bloque 0031
    mova #0xB4172, r12
    mov #0x0000, r13
    mov #0x1, r2
    suba #0x00000, r13
    calla #guarda
; bloque 0032
    mov #0x2, r2
    bitx.a #1, &zona+204
    calla #guarda
; bloque 0033
    mova #zona+128, r9
    mov #0x5, r2
    subx.w &zona+50, -76(r9)
    calla #guarda
; bloque 0034
    mova #zona+16, r15
    mov #0x7, r2
    andx.w @r15+, &zona+182
    calla #guarda
; bloque 0035
    mova #0x10000, r13
    mov #0x44A8, r8
    mov #0x0, r2
    mova r13, &zona+160
    calla #guarda
; bloque 0036
    mov #0x0045, r5
    mov #0x0056, r12
    mov #0x0000, r11
    mov #0x0, r2
    rpt r11 { daddx.b r5, r12
    calla #guarda
; bloque 0037
    mova #0xF246C, r10
    mova #0xA47DF, r15
    mov #0x2, r2
    mova &zona+188, r15
    calla #guarda
; bloque 0038
    mov #0x0000, r13
    mov #0x3, r2
    rrcm.a #4, r13
    calla #guarda
; bloque 0039
    mova #0x7A050, r6
    mova #0xB7AC8, r14
    mov #0x6, r2
    cmpa #0x1F458, r14
    calla #guarda
; bloque 003a
    mov #0x1, r2
    cmpx.b #-1, zona+130
    calla #guarda
; bloque 003b
    mova #0x55A4B, r4
    mova #0x55397, r14
    mov #0x3, r2
    suba #0x92F45, r14
    calla #guarda
; bloque 003c
    mova #0xFFFFF, r9
    mova #0xD20E3, r8
    mov #0x3, r2
    cmpa #0xA5E92, r8
    calla #guarda
; bloque 003d
    mov #0x7, r2
    rrcx.a &zona+76
    calla #guarda
; bloque 003e
    mova #0x7C71E, r10
    mova #0xAF008, r11
    mov #0x6, r2
    rpt #5 { subcx.a r10, r11
    calla #guarda
; bloque 003f
    mova #zona+128, r15
    mov #0x1, r2
    push.w -40(r15)
    pop r15
    calla #guarda
; bloque 0040
    mova #0x811E2, r8
    mova #0x1AA8A, r12
    mov #0x7, r2
    cmpa r8, r12
    calla #guarda
; bloque 0041
    mova #0x6A28E, r5
    mov #0x4, r2
    bisx.a zona+2, r5
    calla #guarda
; bloque 0042
    mova #0x85D56, r6
    mova #0xFAB9B, r7
    mova #0x80000, r8
    mova #0x96F66, r9
    mov #0x6, r2
    pushm.w #4, r9
    popm.w #4, r14
    calla #guarda
; bloque 0043
    mov #0, r15
    mov #0x5, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0044
    mov #0x2, r2
    xorx.a #4, &zona+124
    calla #guarda
; bloque 0045
    mov #0x0080, r8
    mova #0xB3707, r11
    mov #0x4, r2
    adda #0x00000, r11
    calla #guarda
; bloque 0046
    mova #0xB67ED, r12
    mov #0x2, r2
    rrum.a #4, r12
    calla #guarda
; bloque 0047
    mova #0x769D0, r11
    mov #0x0000, r7
    mov #0x7, r2
    rpt #8 { bicx.a r11, r7
    calla #guarda
; bloque 0048
    mov #0x6, r2
    pushx.w zona+4
    pop r15
    calla #guarda
; bloque 0049
    mova #0xFDC71, r15
    mov #0x6, r2
    rrcm.w #2, r15
    calla #guarda
; bloque 004a
    mova #0x80000, r4
    mov #0x2, r2
    bicx.b &zona+185, r4
    calla #guarda
; bloque 004b
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 004c
    mov #0x3, r2
    bisx.a #4, &zona+236
    calla #guarda
; bloque 004d
    mov #0x2, r2
    push.w #8
    pop r15
    calla #guarda
; bloque 004e
    mova #zona+128, r8
    mov #0x2, r2
    movx.b #0, 96(r8)
    calla #guarda
; bloque 004f
    mov #0x7, r2
    movx.b #0x0, &zona+111
    calla #guarda
; bloque 0050
    mov #0x4000, r13
    mova #zona+128, r15
    mov #0x5, r2
    cmpx.a -6(r15), r13
    calla #guarda
; bloque 0051
    mov #0x1000, r12
    mov #0x2, r2
    rrcm.a #4, r12
    calla #guarda
; bloque 0052
    mova #zona+128, r10
    mov #0x5, r2
    cmp.b #0x4, -90(r10)
    calla #guarda
; bloque 0053
    mova #0x9DA06, r11
    mov #0x2, r2
    xorx.w r11, zona+156
    calla #guarda
; bloque 0054
    mov #0xDA38, r15
    mov #0x6, r2
    pushm.a #1, r15
    popm.a #1, r7
    calla #guarda
; bloque 0055
    mova #zona+128, r4
    mov #0x5, r2
    subcx.b 35(r4), zona+30
    calla #guarda
; bloque 0056
    mova #zona+128, r7
    mova #zona+84, r12
    mov #0x1, r2
    mov.w @r12, -82(r7)
    calla #guarda
; bloque 0057
    mova #0xCAD12, r13
    mov #0x6, r2
    push.b r13
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 0058
    mova #0x176C8, r11
    mova #0x80000, r8
    mov #0x3, r2
    mova #0x7FFFF, r8
    calla #guarda
; bloque 0059
    mov #0, r15
    mov #0x7, r2
    calla &tabla_a
    calla #guarda
; bloque 005a
    mova #0xFFFFF, r5
    mov #0x3, r2
    rrcx.w r5
    calla #guarda
; bloque 005b
    mova #0x80000, r7
    mova #0x7FFFF, r9
    mov #0x6, r2
    adda #0x7FFFE, r9
    calla #guarda
; bloque 005c
    mov #0x7C36, r6
    mov #0x0000, r7
    mov #0x4, r2
    cmpa r6, r7
    calla #guarda
; bloque 005d
    mova #0xF26A5, r15
    mova #0xFFFFF, r10
    mov #0x3, r2
    addcx.w r10, r15
    calla #guarda
; bloque 005e
    mova #0x44A46, r8
    mov #0x1, r2
    bitx.b zona+18, r8
    calla #guarda
; bloque 005f
    mova #0x46B15, r6
    mov #0x0, r2
    pushm.w #1, r6
    popm.w #1, r15
    calla #guarda
; bloque 0060
    mova #0x608CD, r14
    mova #0xA1EB6, r12
    mov #0x6, r2
    cmpa r14, r12
    calla #guarda
; bloque 0061
    mova #zona+169, r6
    mov #0x3, r2
    pushx.b @r6
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 0062
    mova #0x17536, r5
    mova #0x3BF75, r14
    mova #zona+128, r5
    mov #0x6, r2
    mova 62(r5), r14
    calla #guarda
; bloque 0063
    mova #0xAE1EB, r14
    mov #0x1, r2
    rrum.w #2, r14
    calla #guarda
; bloque 0064
    mova #0xFDC38, r15
    mov #0x6, r2
    bic.b #0xFF, r15
    calla #guarda
; bloque 0065
    mov #0x4C12, r5
    mova #0x1ABEE, r11
    mov #0x3, r2
    adda r5, r11
    calla #guarda
; bloque 0066
    mova #0x2A4EB, r15
    mova #0x23718, r12
    mov #0x3, r2
    mova #0xF7222, r12
    calla #guarda
; bloque 0067
    mov #0xCB6E, r11
    mov #0x7, r2
    movx.w &zona+30, r11
    calla #guarda
; bloque 0068
    mova #0xACA90, r5
    mov #0x5, r2
    xorx.b r5, zona+106
    calla #guarda
; bloque 0069
    mova #0xCE1F0, r5
    mov #0x7, r2
    rra.w r5
    calla #guarda
; bloque 006a
    mov #0, r15
    mova #tabla_a-4, r6
    mov #0x0, r2
    calla 4(r6)
    calla #guarda
; bloque 006b
    mov #0x1000, r9
    mov #0x4, r2
    rpt #12 { rrcx.b r9
    calla #guarda
; bloque 006c
    mova #0x7FFFE, r10
    mov #0x5, r2
    bitx.b r10, r10
    calla #guarda
; bloque 006d
    mova #zona+128, r5
    mova #zona+88, r11
    mov #0x0, r2
    and.w @r11+, -2(r5)
    calla #guarda
; bloque 006e
    mova #0x6260E, r14
    mov #0x0, r2
    addx.b #0x20, r14
    calla #guarda
; bloque 006f
    mov #0x0014, r9
    mov #0x2, r2
    dadd.b #4, r9
    calla #guarda
; bloque 0070
    mov #0x7, r2
    andx.b zona+53, zona+142
    calla #guarda
; bloque 0071
    mov #0x0081, r15
    mov #0x6, r2
    dadd.b #2, r15
    calla #guarda
; bloque 0072
    mova #zona+188, r15
    mov #0x5, r2
    subx.a @r15, zona+86
    calla #guarda
; bloque 0073
    mova #0x17259, r5
    mova #0x80000, r14
    mov #0x7, r2
    addcx.b r14, r5
    calla #guarda
; bloque 0074
    mov #0x0008, r14
    mova #0x732A3, r13
    mov #0x4, r2
    mova &zona+62, r13
    calla #guarda
; bloque 0075
    mov #0, r15
    mov #0x6, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0076
    mova #0x80000, r9
    mova #0x26792, r14
    mov #0x3, r2
    cmpa r9, r14
    calla #guarda
; bloque 0077
    mova #0xA3347, r12
    mov #0x1, r2
    rlam.w #4, r12
    calla #guarda
; bloque 0078
    mov #0x2, r2
    rrux.a zona+216
    calla #guarda
; bloque 0079
    mov #0x7, r2
    addx.b zona+130, zona+38
    calla #guarda
; bloque 007a
    mov #0x0014, r10
    mov #0x3, r2
    daddx.b #8, r10
    calla #guarda
; bloque 007b
    mova #0x2E29E, r6
    mova #0xEF444, r15
    mov #0x4, r2
    rpt #13 { addx.b r6, r15
    calla #guarda
; bloque 007c
    mov #0, r15
    mov #0x2, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 007d
    mova #zona+128, r4
    mov #0x5, r2
    mov.b #0x57, 33(r4)
    calla #guarda
; bloque 007e
    mova #zona+128, r7
    mova #zona+228, r13
    mov #0x6, r2
    bis.w @r13, 46(r7)
    calla #guarda
; bloque 007f
    mov #0x0, r2
    push.b #4
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 0080
    mov #0x0012, r14
    mov #0x5, r2
    dadd.b #0x38, r14
    calla #guarda
; bloque 0081
    mova #0x251A0, r11
    mova #0xFFFFF, r12
    mova #0x49EF7, r13
    mov #0x7, r2
    pushm.w #3, r13
    popm.w #3, r7
    calla #guarda
; bloque 0082
    mov #0x0000, r4
    mova #0x7FFFE, r15
    mov #0x2, r2
    mova &zona+140, r15
    calla #guarda
; bloque 0083
    mov #0x1, r2
    swpbx &zona+60
    calla #guarda
; bloque 0084
    mova #zona+128, r10
    mov #0x1, r2
    movx.w &zona+62, -2(r10)
    calla #guarda
; bloque 0085
    mov #0x0, r2
    xorx.w &zona+86, &zona+28
    calla #guarda
; bloque 0086
    mov #0x0000, r8
    mova #0x2A685, r6
    mov #0x4, r2
    rpt r6 { rrux.w r8
    calla #guarda
; bloque 0087
    mov #0x6, r2
    rrcx.w zona+68
    calla #guarda
; bloque 0088
    mova #zona+128, r6
    mov #0x4, r2
    subc.w r6, -58(r6)
    calla #guarda
; bloque 0089
    mov #0, r15
    mov #0x5, r2
    jge 1f
    mov #1, r15
1:
    calla #guarda
; bloque 008a
    mova #0x43897, r14
    mov #0x3, r2
    addcx.b r14, zona+244
    calla #guarda
; bloque 008b
    mova #0x80000, r7
    mova #0xF55F5, r12
    mov #0x6, r2
    adda r7, r12
    calla #guarda
; bloque 008c
    mov #0x7, r2
    cmpx.b &zona+16, zona+157
    calla #guarda
; bloque 008d
    mova #0x7FFFE, r12
    mova #zona+56, r10
    mov #0x4, r2
    bisx.w @r10+, r12
    calla #guarda
; bloque 008e
    mova #0x2ED44, r11
    mova #0xFFFFF, r13
    mov #0x3, r2
    rpt #16 { movx.b r11, r13
    calla #guarda
; bloque 008f
    mova #zona+128, r12
    mova #zona+128, r9
    mov #0x5, r2
    xorx.b 34(r9), -65(r12)
    calla #guarda
; bloque 0090
    mova #0xFFFFF, r4
    mova #0x7C328, r5
    mova #0xC1C61, r6
    mova #0x96D85, r7
    mov #0x5, r2
    pushm.w #4, r7
    popm.w #4, r14
    calla #guarda
; bloque 0091
    mova #zona+128, r15
    mova #0xC7598, r4
    mov #0x4, r2
    bic.b r4, -43(r15)
    calla #guarda
; bloque 0092
    mova #zona+128, r9
    mov #0x7, r2
    addcx.a 58(r9), &zona+84
    calla #guarda
; bloque 0093
    mov #0x4497, r10
    mov #0x6, r2
    daddx.w #0x8625, r10
    calla #guarda
; bloque 0094
    mova #0x2F9FA, r6
    mov #0x1, r2
    bitx.w zona+50, r6
    calla #guarda
; bloque 0095
    mov #0x46B4, r9
    mova #0xF691B, r7
    mov #0x6, r2
    sub.b r7, r9
    calla #guarda
; bloque 0096
    mova #0xCEAA2, r7
    mova #zona+128, r13
    mov #0x7, r2
    cmpx.w -18(r13), r7
    calla #guarda
; bloque 0097
    mova #0xB82DC, r12
    mova #0x41F7E, r15
    mova #0xFFFFF, r13
    mov #0x5, r2
    rpt r13 { addcx.b r12, r15
    calla #guarda
; bloque 0098
    mova #0xFFFFF, r12
    mov #0x1, r2
    bisx.b #0, r12
    calla #guarda
; bloque 0099
    mov #0x0078, r13
    mov #0x3, r2
    daddx.b #0x45, r13
    calla #guarda
; bloque 009a
    mov #0x6, r2
    pushx.b zona+32
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 009b
    mova #zona+42, r14
    mov #0x5, r2
    xorx.w @r14, &zona+242
    calla #guarda
; bloque 009c
    mova #0x8BC86, r13
    mova #zona+212, r9
    mov #0x6, r2
    sub.b @r9+, r13
    calla #guarda
; bloque 009d
    mova #0xFFFFF, r15
    mova #0xAC619, r12
    mov #0x2, r2
    mova #0xFFFFF, r12
    calla #guarda
; bloque 009e
    mov #0xB49F, r9
    mova #0x387E9, r15
    mov #0x6, r2
    cmpa #0xD094F, r15
    calla #guarda
; bloque 009f
    mova #0x92B70, r7
    mov #0x1, r2
    rrux.a r7
    calla #guarda
; bloque 00a0
    mov #0, r15
    mov #0x7, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00a1
    mova #0xFFFFF, r9
    mov #0x7, r2
    rrum.w #2, r9
    calla #guarda
; bloque 00a2
    mov #0, r15
    mov #0x5, r2
    jn 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00a3
    mov #0x0, r2
    cmpx.b #0x7E, zona+15
    calla #guarda
; bloque 00a4
    mova #0xFFFFF, r13
    mova #0x80000, r12
    mov #0x5, r2
    adda r13, r12
    calla #guarda
; bloque 00a5
    mov #0x0000, r7
    mova #0xF9879, r8
    mov #0x0000, r9
    mov #0x0, r2
    pushm.w #3, r9
    popm.w #3, r11
    calla #guarda
; bloque 00a6
    mova #0x7FFFF, r8
    mov #0x1, r2
    movx.w #0x10B, r8
    calla #guarda
; bloque 00a7
    mov #0x0071, r5
    mov #0x0034, r4
    mov #0x1, r2
    dadd.b r4, r5
    calla #guarda
; bloque 00a8
    mova #0xFFFFF, r4
    mov #0x1, r2
    rrc.w r4
    calla #guarda
; bloque 00a9
    mova #0x8E4E4, r15
    mov #0xD07C, r13
    mov #0x3, r2
    cmpa r15, r13
    calla #guarda
; bloque 00aa
    mova #0xFFFFF, r11
    mov #0x0, r2
    rlam.w #1, r11
    calla #guarda
; bloque 00ab
    mov #0x0000, r12
    mova #0xFE67A, r13
    mova #0x40E22, r10
    mov #0x6, r2
    rpt r10 { movx.w r12, r13
    calla #guarda
; bloque 00ac
    mova #zona+128, r13
    mov #0x7, r2
    andx.a zona+144, -4(r13)
    calla #guarda
; bloque 00ad
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00ae
    mova #0xCFF92, r4
    mova #0xB13B0, r6
    mov #0x7, r2
    adda #0xA4D89, r6
    calla #guarda
; bloque 00af
    mov #0x0004, r10
    mov #0x7, r2
    rpt #15 { rrux.w r10
    calla #guarda
; bloque 00b0
    mova #zona+222, r8
    mov #0x1, r2
    subcx.a @r8, zona+62
    calla #guarda
; bloque 00b1
    mov #0x3, r2
    xorx.b #0x0, &zona+107
    calla #guarda
; bloque 00b2
    mov #0x2, r2
    rrux.a zona+26
    calla #guarda
; bloque 00b3
    mov #0x3, r2
    addx.w #2, zona+132
    calla #guarda
; bloque 00b4
    mova #0x62371, r12
    mova #0xCDC8D, r7
    mova #zona+214, r12
    mov #0x4, r2
    mova @r12+, r7
    calla #guarda
; bloque 00b5
    mov #0, r15
    mov #0x0, r2
    jl 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00b6
    mova #0x80000, r13
    mov #0x0001, r6
    mov #0x0, r2
    mova #0x737D2, r6
    calla #guarda
; bloque 00b7
    mova #0x7FFFF, r5
    mov #0x0000, r6
    mova #0xAC01A, r7
    mova #0x40000, r8
    mov #0x1, r2
    pushm.a #4, r8
    popm.a #4, r11
    calla #guarda
; bloque 00b8
    mov #0x0010, r11
    mov #0x4, r2
    subx.a r11, &zona+230
    calla #guarda
; bloque 00b9
    mova #0x84416, r9
    mov #0x6, r2
    pushx.a r9
    popx.a r15
    calla #guarda
; bloque 00ba
    mova #zona+88, r13
    mov #0x1, r2
    bisx.w @r13, &zona+16
    calla #guarda
; bloque 00bb
    mov #0, r15
    mov #0x4, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00bc
    mova #0xE948A, r6
    mova #0x9D1DF, r11
    mova #zona+212, r6
    mov #0x6, r2
    mova @r6+, r11
    calla #guarda
; bloque 00bd
    mova #0xFFFFF, r8
    mov #0x0, r2
    rlam.a #3, r8
    calla #guarda
; bloque 00be
    mova #0x82AE8, r4
    mova #0x2E674, r8
    mov #0x2, r2
    suba r4, r8
    calla #guarda
; bloque 00bf
    mova #zona+128, r9
    mova #0x11482, r8
    mov #0x5, r2
    bitx.a r8, -8(r9)
    calla #guarda
; bloque 00c0
    mova #zona+214, r6
    mov #0x6, r2
    pushx.a @r6
    popx.a r15
    calla #guarda
; bloque 00c1
    mova #zona+197, r14
    mov #0x1, r2
    pushx.b @r14
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 00c2
    mova #0x11C5D, r11
    mova #0x49455, r12
    mova #0x80000, r13
    mova #0x53817, r14
    mova #0x91D31, r15
    mov #0x5, r2
    pushm.w #5, r15
    popm.w #5, r11
    calla #guarda
; bloque 00c3
    mova #0x902CF, r8
    mov #0x6, r2
    rpt #7 { rrcx.w r8
    calla #guarda
; bloque 00c4
    mova #zona+128, r10
    mov #0x6, r2
    addx.w -18(r10), zona+124
    calla #guarda
; bloque 00c5
    mov #0, r15
    mov #0x3, r2
    calla &tabla_a
    calla #guarda
; bloque 00c6
    mova #0xB2FEF, r11
    mova #0xA3EBD, r12
    mova #0xCF275, r13
    mov #0x0000, r14
    mov #0x6, r2
    pushm.a #4, r14
    popm.a #4, r10
    calla #guarda
; bloque 00c7
    mov #0x7, r2
    rrax.w &zona+10
    calla #guarda
; bloque 00c8
    mova #zona+128, r5
    mov #0x7, r2
    xorx.b 97(r5), zona+165
    calla #guarda
; bloque 00c9
    mov #0x5556, r7
    mova #0x7929A, r8
    mova #0x7FFFE, r9
    mov #0x2, r2
    pushm.w #3, r9
    popm.w #3, r9
    calla #guarda
; bloque 00ca
    mova #0x6EA39, r14
    mov #0x5, r2
    bisx.a #0x3D915, r14
    calla #guarda
; bloque 00cb
    mova #0xF0E20, r11
    mov #0x0, r2
    movx.a zona+36, r11
    calla #guarda
; bloque 00cc
    mova #0x6D951, r9
    mov #0x4, r2
    rrc.b r9
    calla #guarda
; bloque 00cd
    mova #0x10000, r9
    mova #0x3B568, r4
    mova #zona+128, r9
    mov #0x3, r2
    mova -42(r9), r4
    calla #guarda
; bloque 00ce
    mova #0x83B75, r7
    mov #0x3, r2
    subcx.a r7, &zona+246
    calla #guarda
; bloque 00cf
    mov #0x0020, r9
    mov #0x4, r2
    pushm.w #1, r9
    popm.w #1, r5
    calla #guarda
; bloque 00d0
    mova #0x7FFFF, r8
    mova #zona+128, r5
    mov #0x5, r2
    cmpx.b 16(r5), r8
    calla #guarda
; bloque 00d1
    mova #zona+128, r9
    mov #0x4, r2
    swpbx -36(r9)
    calla #guarda
; bloque 00d2
    mov #0, r15
    mov #0x6, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00d3
    mova #0x997F4, r13
    mov #0x6, r2
    rpt #16 { rrux.w r13
    calla #guarda
; bloque 00d4
    mov #0x5, r2
    rrcx.w &zona+170
    calla #guarda
; bloque 00d5
    mova #zona+128, r11
    mova #zona+169, r8
    mov #0x1, r2
    add.b @r8+, 2(r11)
    calla #guarda
; bloque 00d6
    mova #zona+128, r9
    mov #0x1, r2
    bisx.w zona+110, -82(r9)
    calla #guarda
; bloque 00d7
    mova #zona+128, r14
    mov #0x0, r2
    cmpx.b #0xD3, 41(r14)
    calla #guarda
; bloque 00d8
    mova #0x7FFFF, r13
    mov #0x0001, r14
    mov #0x0, r2
    pushm.w #2, r14
    popm.w #2, r15
    calla #guarda
; bloque 00d9
    mova #0xCFA16, r9
    mov #0x4, r2
    rrux.a r9
    calla #guarda
; bloque 00da
    mova #0xFFFFF, r7
    mova #zona+32, r4
    mov #0x7, r2
    addc.w @r4, r7
    calla #guarda
; bloque 00db
    mov #0, r15
    mova #sub, r8
    mov #0x6, r2
    calla r8
    calla #guarda
; bloque 00dc
    mov #0x0, r2
    push.b #2
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 00dd
    mov #0x6, r2
    rrux.a &zona+250
    calla #guarda
; bloque 00de
    mova #0x68A10, r11
    mova #0x3DD69, r13
    mov #0x0, r2
    adda r11, r13
    calla #guarda
; bloque 00df
    mov #0x1, r2
    swpbx &zona+10
    calla #guarda
; bloque 00e0
    mov #0x3, r2
    addx.w #0x100, zona+106
    calla #guarda
; bloque 00e1
    mov #0, r15
    mov #0x1, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00e2
    mova #0xFFFFF, r10
    mova #0xFFFFF, r13
    mov #0x5, r2
    rpt #7 { subcx.b r10, r13
    calla #guarda
; bloque 00e3
    mova #0x32E18, r8
    mov #0x0, r2
    rrcx.b r8
    calla #guarda
; bloque 00e4
    mov #0x0000, r10
    mov #0x0400, r8
    mov #0x4, r2
    mova r10, &zona+216
    calla #guarda
; bloque 00e5
    mov #0, r15
    mov #0x1, r2
    jl 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00e6
    mova #0x3CB0B, r6
    mova #0x85C87, r7
    mova #0x7FFFF, r8
    mov #0x0400, r9
    mova #0x18C2F, r10
    mov #0x1, r2
    pushm.w #5, r10
    popm.w #5, r12
    calla #guarda
; bloque 00e7
    mova #0xE3FEC, r15
    mova #0xF4B15, r10
    mov #0x5, r2
    rpt #1 { subcx.a r15, r10
    calla #guarda
; bloque 00e8
    mov #0x7, r2
    andx.a #0x36791, &zona+38
    calla #guarda
; bloque 00e9
    mova #zona+128, r11
    mov #0x0, r2
    swpbx -128(r11)
    calla #guarda
; bloque 00ea
    mova #0xFFFFF, r8
    mova #zona+128, r11
    mov #0x7, r2
    bis.b 28(r11), r8
    calla #guarda
; bloque 00eb
    mov #0, r15
    mov #0x7, r2
    call #subw
    calla #guarda
; bloque 00ec
    mova #zona+128, r4
    mov #0x4, r2
    bit.b #0xDB, -53(r4)
    calla #guarda
; bloque 00ed
    mova #zona+78, r15
    mov #0x5, r2
    swpb @r15
    calla #guarda
; bloque 00ee
    mova #zona+244, r10
    mov #0x0, r2
    xorx.b @r10+, &zona+60
    calla #guarda
; bloque 00ef
    mov #0, r15
    mov #0x0, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00f0
    mova #0x86461, r10
    mov #0x1, r2
    swpbx r10
    calla #guarda
; bloque 00f1
    mova #0x536AD, r10
    mov #0x2, r2
    rrum.a #4, r10
    calla #guarda
; bloque 00f2
    mov #0x2, r2
    bisx.w #0x7FFF, zona+18
    calla #guarda
; bloque 00f3
    mova #0xB08E7, r5
    mov #0x7, r2
    rpt #9 { rrax.b r5
    calla #guarda
; bloque 00f4
    mova #0x79155, r15
    mov #0x6CED, r9
    mov #0x5, r2
    cmpa #0x2B193, r9
    calla #guarda
; bloque 00f5
    mov #0x0050, r10
    mov #0x0, r2
    daddx.b #4, r10
    calla #guarda
; bloque 00f6
    mova #0x9D717, r4
    mova #0xD0C9C, r11
    mov #0x5, r2
    suba r4, r11
    calla #guarda
; bloque 00f7
    mova #zona+128, r4
    mov #0x5, r2
    subx.w &zona+20, -106(r4)
    calla #guarda
; bloque 00f8
    mov #0, r15
    mov #0x6, r2
    jn 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00f9
    mov #0, r15
    mov #0x6, r2
    jge 1f
    mov #1, r15
1:
    calla #guarda
; bloque 00fa
    mova #0x1D3AB, r10
    mova #0xA2B68, r5
    mov #0x0, r2
    mova &zona+84, r5
    calla #guarda
; bloque 00fb
    mova #0x95CD2, r4
    mov #0x0000, r11
    mov #0x0, r2
    rpt #3 { subcx.b r4, r11
    calla #guarda
; bloque 00fc
    mov #0x2, r2
    rrux.b &zona+104
    calla #guarda
; bloque 00fd
    mova #zona+211, r8
    mov #0x4, r2
    subcx.b @r8+, zona+52
    calla #guarda
; bloque 00fe
    mov #0x3, r2
    andx.b #0x7F, zona+186
    calla #guarda
; bloque 00ff
    mov #0x4000, r11
    mova #0x6D57E, r9
    mov #0x1, r2
    adda r11, r9
    calla #guarda
; bloque 0100
    mova #0x2E9CF, r5
    mov #0x0, r2
    bicx.a zona+122, r5
    calla #guarda
; bloque 0101
    mova #0x29069, r7
    mov #0x4, r2
    bitx.b zona+55, r7
    calla #guarda
; bloque 0102
    mova #zona+12, r14
    mov #0x3, r2
    andx.a @r14, &zona+134
    calla #guarda
; bloque 0103
    mova #0xE70D7, r8
    mova #0x4C351, r9
    mov #0x7, r2
    rpt r9 { swpbx r8
    calla #guarda
; bloque 0104
    mov #0, r15
    mova #subw, r5
    mov #0x2, r2
    call r5
    calla #guarda
; bloque 0105
    mov #0x0000, r11
    mov #0x2, r2
    rrc.w r11
    calla #guarda
; bloque 0106
    mova #0xFFFFF, r11
    mov #0x3, r2
    rram.w #3, r11
    calla #guarda
; bloque 0107
    mov #0, r15
    mov #0x0, r2
    calla &tabla_a
    calla #guarda
; bloque 0108
    mov #0x0000, r11
    mov #0x0, r2
    rram.w #3, r11
    calla #guarda
; bloque 0109
    mova #0xFFFFF, r8
    mov #0x3, r2
    rrum.w #4, r8
    calla #guarda
; bloque 010a
    mova #zona+187, r4
    mov #0x6, r2
    rrux.b @r4
    calla #guarda
; bloque 010b
    mov #0x0000, r11
    mov #0x1, r2
    rram.w #2, r11
    calla #guarda
; bloque 010c
    mova #0x58DB5, r15
    mov #0x4, r2
    rlam.w #4, r15
    calla #guarda
; bloque 010d
    mova #0x7FFFF, r12
    mova #0xB5F73, r11
    mova #0x2DB35, r14
    mov #0x3, r2
    rpt r14 { addx.a r12, r11
    calla #guarda
; bloque 010e
    mov #0, r15
    mov #0x5, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 010f
    mova #0xB88BD, r4
    mov #0x0000, r5
    mov #0x4, r2
    pushm.a #2, r5
    popm.a #2, r9
    calla #guarda
; bloque 0110
    mova #0x7A9B1, r8
    mova #0x63E53, r5
    mova #0x7FFFE, r7
    mov #0x1, r2
    rpt r7 { bisx.b r8, r5
    calla #guarda
; bloque 0111
    mova #0x7FFFE, r7
    mova #0xFFFFF, r4
    mov #0x3, r2
    suba r7, r4
    calla #guarda
; bloque 0112
    mova #0xFFFFF, r9
    mov #0x7, r2
    rram.a #4, r9
    calla #guarda
; bloque 0113
    mova #zona+128, r15
    mova #0x63366, r10
    mov #0x0, r2
    cmpx.w r10, 42(r15)
    calla #guarda
; bloque 0114
    mova #0xC9830, r6
    mova #0xFFFFF, r9
    mov #0x5, r2
    adda r6, r9
    calla #guarda
; bloque 0115
    mov #0x0080, r15
    mova #zona+128, r5
    mov #0x0, r2
    bis.w -32(r5), r15
    calla #guarda
; bloque 0116
    mova #0xB42DC, r8
    mov #0x6, r2
    rpt #14 { rrux.b r8
    calla #guarda
; bloque 0117
    mov #0x1136, r8
    mov #0x6, r2
    rrc.w r8
    calla #guarda
; bloque 0118
    mova #zona+128, r11
    mov #0x2, r2
    pushx.w -6(r11)
    pop r15
    calla #guarda
; bloque 0119
    mova #0x4FBB8, r5
    mova #0xEF804, r6
    mova #0xA5F6C, r12
    mov #0x1, r2
    rpt r12 { subx.w r5, r6
    calla #guarda
; bloque 011a
    mova #0x7C4CE, r6
    mov #0x5, r2
    bitx.b &zona+199, r6
    calla #guarda
; bloque 011b
    mova #0xDB028, r13
    mov #0x1000, r7
    mov #0x0, r2
    suba r13, r7
    calla #guarda
; bloque 011c
    mov #0, r15
    mova #sub, r10
    mov #0x1, r2
    calla r10
    calla #guarda
; bloque 011d
    mova #0x7FFFF, r12
    mov #0x4, r2
    andx.b &zona+80, r12
    calla #guarda
; bloque 011e
    mov #0xB708, r13
    mov #0x4, r2
    rlam.w #1, r13
    calla #guarda
; bloque 011f
    mova #0xE89B3, r9
    mova #0x16F78, r15
    mov #0x3, r2
    bitx.w r15, r9
    calla #guarda
; bloque 0120
    mov #0x0800, r10
    mova #0x135B2, r14
    mov #0x0, r2
    rpt #11 { andx.b r10, r14
    calla #guarda
; bloque 0121
    mova #0x36B64, r6
    mova #0xCCE38, r12
    mov #0x2, r2
    rpt #5 { addcx.b r6, r12
    calla #guarda
; bloque 0122
    mova #0xE3305, r8
    mova #0xC69CD, r6
    mov #0x1, r2
    rpt r6 { rrax.b r8
    calla #guarda
; bloque 0123
    mova #0xA156E, r15
    mov #0x7, r2
    pushm.w #1, r15
    popm.w #1, r13
    calla #guarda
; bloque 0124
    mova #0x5D193, r10
    mov #0x0, r2
    bicx.b &zona+203, r10
    calla #guarda
; bloque 0125
    mov #0x0, r2
    subcx.w zona+110, &zona+2
    calla #guarda
; bloque 0126
    mov #0x2673, r11
    mov #0x3, r2
    rpt #5 { swpbx r11
    calla #guarda
; bloque 0127
    mova #zona+250, r4
    mov #0x6, r2
    swpb @r4
    calla #guarda
; bloque 0128
    mov #0x0, r2
    cmpx.b #0x91, &zona+224
    calla #guarda
; bloque 0129
    mov #0x0, r2
    rrux.b zona+136
    calla #guarda
; bloque 012a
    mova #0xFA88B, r6
    mova #0xAA110, r12
    mova #zona+8, r6
    mov #0x7, r2
    mova @r6+, r12
    calla #guarda
; bloque 012b
    mova #0x80000, r6
    mova #zona+110, r13
    mov #0x4, r2
    addc.w @r13, r6
    calla #guarda
; bloque 012c
    mova #0x80000, r9
    mov #0x4, r2
    pushm.a #1, r9
    popm.a #1, r4
    calla #guarda
; bloque 012d
    mova #0x455A3, r10
    mova #0x80000, r15
    mov #0x6, r2
    mova r10, &zona+248
    calla #guarda
; bloque 012e
    mova #zona+106, r14
    mov #0x3, r2
    rrux.a @r14
    calla #guarda
; bloque 012f
    mov #0, r15
    mova #subw, r4
    mov #0x1, r2
    call r4
    calla #guarda
; bloque 0130
    mov #0x0200, r4
    mov #0x3, r2
    rpt #14 { rrux.b r4
    calla #guarda
; bloque 0131
    mova #0x84ADE, r15
    mov #0x0, r2
    subx.b r15, &zona+95
    calla #guarda
; bloque 0132
    mova #0xFFFFF, r8
    mova #zona+92, r15
    mov #0x7, r2
    cmp.w @r15+, r8
    calla #guarda
; bloque 0133
    mova #0xFFFFF, r13
    mova #0x7FFFF, r10
    mov #0x6, r2
    rpt #11 { xorx.b r13, r10
    calla #guarda
; bloque 0134
    mova #0xF5AFA, r5
    mov #0x0001, r8
    mova #zona+128, r5
    mov #0x0, r2
    mova 100(r5), r8
    calla #guarda
; bloque 0135
    mov #0x9052, r15
    mov #0x0670, r6
    mova #0x7FFFF, r8
    mov #0x4, r2
    rpt r8 { daddx.w r15, r6
    calla #guarda
; bloque 0136
    mova #0xFFFFF, r8
    mov #0x6FC9, r14
    mov #0x1, r2
    bicx.w r14, r8
    calla #guarda
; bloque 0137
    mov #0x0011, r4
    mov #0x0065, r13
    mova #0x23F35, r14
    mov #0x1, r2
    rpt r14 { daddx.b r4, r13
    calla #guarda
; bloque 0138
    mov #0x2, r2
    rrcx.a zona+150
    calla #guarda
; bloque 0139
    mova #0xE2F5B, r10
    mov #0x3, r2
    rrcm.a #2, r10
    calla #guarda
; bloque 013a
    mova #zona+128, r5
    mov #0x0, r2
    subx.b -51(r5), zona+200
    calla #guarda
; bloque 013b
    mov #0x3, r2
    andx.w zona+192, zona+38
    calla #guarda
; bloque 013c
    mov #0x0002, r5
    mov #0x4, r2
    rrc.b r5
    calla #guarda
; bloque 013d
    mova #zona+128, r9
    mova #zona+210, r12
    mov #0x4, r2
    xorx.w @r12, -64(r9)
    calla #guarda
; bloque 013e
    mov #0x5FB4, r8
    mova #0x239B6, r9
    mova #0xF2CD6, r10
    mova #0x1C31A, r11
    mov #0x5, r2
    pushm.w #4, r11
    popm.w #4, r11
    calla #guarda
; bloque 013f
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0140
    mov #0x6912, r9
    mov #0x6, r2
    daddx.w r9, r9
    calla #guarda
; bloque 0141
    mova #0x54CBA, r15
    mov #0x6, r2
    pushx.w r15
    pop r15
    calla #guarda
; bloque 0142
    mov #0, r15
    mov #0x5, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0143
    mova #0x6250E, r8
    mov #0x0, r2
    rlam.w #1, r8
    calla #guarda
; bloque 0144
    mov #0x8999, r11
    mov #0x0, r2
    daddx.w #4, r11
    calla #guarda
; bloque 0145
    mova #zona+144, r13
    mov #0x3, r2
    rrcx.a @r13
    calla #guarda
; bloque 0146
    mova #0x7FFFE, r9
    mov #0x7, r2
    bitx.w #0x7FFE, r9
    calla #guarda
; bloque 0147
    mova #0x36FEB, r12
    mova #0x60D9A, r11
    mov #0x4, r2
    suba r12, r11
    calla #guarda
; bloque 0148
    mova #0xFFFFF, r11
    mov #0x6, r2
    rram.a #1, r11
    calla #guarda
; bloque 0149
    mova #zona+64, r14
    mov #0x4, r2
    pushx.a @r14
    popx.a r15
    calla #guarda
; bloque 014a
    mov #0x5, r2
    subx.w #2, zona+162
    calla #guarda
; bloque 014b
    mov #0x0084, r7
    mov #0x4, r2
    daddx.b #0, r7
    calla #guarda
; bloque 014c
    mov #0, r15
    mov #0x2, r2
    jge 1f
    mov #1, r15
1:
    calla #guarda
; bloque 014d
    mov #0, r15
    mov #0x7, r2
    calla &tabla_a
    calla #guarda
; bloque 014e
    mova #zona+128, r6
    mov #0x1, r2
    bisx.w 114(r6), &zona+210
    calla #guarda
; bloque 014f
    mova #0x2C6EA, r8
    mova #0x7FFFE, r14
    mov #0x5, r2
    cmpa r8, r14
    calla #guarda
; bloque 0150
    mova #zona+128, r7
    mov #0x2, r2
    addx.a 22(r7), &zona+82
    calla #guarda
; bloque 0151
    mov #0, r15
    mov #0x6, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0152
    mova #zona+128, r15
    mov #0x6, r2
    subx.w zona+34, -72(r15)
    calla #guarda
; bloque 0153
    mov #0, r15
    mov #0x3, r2
    jne 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0154
    mova #0xFFFFF, r4
    mov #0x0000, r10
    mov #0x6, r2
    adda #0xE4DC9, r10
    calla #guarda
; bloque 0155
    mov #0x1, r2
    rrax.w &zona+44
    calla #guarda
; bloque 0156
    mov #0, r15
    mov #0x1, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0157
    mova #0xE54B1, r13
    mov #0x0000, r10
    mov #0x5, r2
    mova #0xBE148, r10
    calla #guarda
; bloque 0158
    mova #0x27FAE, r12
    mova #0xF3D20, r11
    mov #0x1, r2
    adda #0x7DCB0, r11
    calla #guarda
; bloque 0159
    mova #0x7D3C8, r12
    mov #0x4, r2
    push.b r12
    mov.b @r1, r15
    adda #2, r1
    calla #guarda
; bloque 015a
    mov #0x2, r2
    movx.w &zona+118, zona+182
    calla #guarda
; bloque 015b
    mov #0, r15
    mov #0x5, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 015c
    mova #0x3F983, r14
    mov #0x5, r2
    mov.b #4, r14
    calla #guarda
; bloque 015d
    mov #0x0080, r10
    mov #0x6, r2
    rlam.w #1, r10
    calla #guarda
; bloque 015e
    mova #zona+52, r12
    mov #0x6, r2
    swpb @r12
    calla #guarda
; bloque 015f
    mova #zona+128, r4
    mova #zona+36, r13
    mov #0x1, r2
    bic.w @r13+, -70(r4)
    calla #guarda
; bloque 0160
    mova #zona+128, r7
    mov #0x3, r2
    rrux.w 44(r7)
    calla #guarda
; bloque 0161
    mov #0x0020, r8
    mova #0xFFFFF, r9
    mov #0x2, r2
    pushm.a #2, r9
    popm.a #2, r7
    calla #guarda
; bloque 0162
    mov #0, r15
    mov #0x0, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0163
    mov #0, r15
    mov #0x5, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0164
    mova #0x7FFFE, r6
    mova #0xBF247, r7
    mov #0x1, r2
    adda #0xFFFFF, r7
    calla #guarda
; bloque 0165
    mov #0, r15
    mova #tabla_a-4, r4
    mov #0x7, r2
    calla 4(r4)
    calla #guarda
; bloque 0166
    mova #zona+128, r14
    mov #0x2, r2
    addx.w zona+106, -36(r14)
    calla #guarda
; bloque 0167
    mova #0x26572, r5
    mova #0xFBE8C, r9
    mov #0x6, r2
    cmpa r5, r9
    calla #guarda
; bloque 0168
    mov #0, r15
    mov #0x3, r2
    jeq 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0169
    mova #zona+128, r12
    mov #0x1, r2
    rrc.w -86(r12)
    calla #guarda
; bloque 016a
    mova #0xC3B0F, r11
    mov #0x4, r2
    bitx.w #0x7FFE, r11
    calla #guarda
; bloque 016b
    mov #0x3, r2
    rrax.w &zona+20
    calla #guarda
; bloque 016c
    mov #0x0400, r14
    mova #0xFFFFF, r12
    mov #0x4, r2
    cmp.b r12, r14
    calla #guarda
; bloque 016d
    mova #0x52514, r6
    mov #0x0010, r14
    mov #0x7, r2
    adda r6, r14
    calla #guarda
; bloque 016e
    mova #0xBDADD, r7
    mov #0x3, r2
    bitx.a &zona+10, r7
    calla #guarda
; bloque 016f
    mova #0xBA640, r4
    mova #0x4683E, r12
    mova #zona+148, r4
    mov #0x5, r2
    mova @r4+, r12
    calla #guarda
; bloque 0170
    mov #0x0000, r9
    mova #0xB8197, r15
    mov #0x5, r2
    rpt #15 { addcx.a r9, r15
    calla #guarda
; bloque 0171
    mova #0x6CBB6, r7
    mov #0x6, r2
    addcx.w &zona+90, r7
    calla #guarda
; bloque 0172
    mova #0xFFFFF, r8
    mova #0xFFFFF, r6
    mov #0x3, r2
    adda #0x04DDD, r6
    calla #guarda
; bloque 0173
    mov #0x0, r2
    swpbx zona+142
    calla #guarda
; bloque 0174
    mov #0, r15
    mov #0x1, r2
    jnc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0175
    mov #0x2, r2
    swpbx zona+12
    calla #guarda
; bloque 0176
    mova #zona+116, r8
    mov #0x5, r2
    addx.a @r8+, zona+172
    calla #guarda
; bloque 0177
    mova #0xFFFFF, r15
    mova #0x653A4, r8
    mov #0x3, r2
    addc.b r8, r15
    calla #guarda
; bloque 0178
    mov #0x5, r2
    andx.a #4, &zona+74
    calla #guarda
; bloque 0179
    mova #zona+128, r11
    mov #0x1, r2
    subcx.b &zona+244, 36(r11)
    calla #guarda
; bloque 017a
    mova #0xFFFFF, r11
    mov #0x7, r2
    rrux.w r11
    calla #guarda
; bloque 017b
    mov #0x0027, r7
    mov #0x1, r2
    dadd.b #0x93, r7
    calla #guarda
; bloque 017c
    mov #0x7, r2
    pushx.w &zona+124
    pop r15
    calla #guarda
; bloque 017d
    mov #0x0100, r11
    mov #0x4, r2
    rram.w #4, r11
    calla #guarda
; bloque 017e
    mov #0, r15
    mov #0x3, r2
    jmp 1f
    mov #1, r15
1:
    calla #guarda
; bloque 017f
    mova #zona+128, r11
    mov #0x7, r2
    swpb 72(r11)
    calla #guarda
; bloque 0180
    mova #0x9D561, r11
    mova #zona+16, r13
    mov #0x0, r2
    movx.b @r13+, r11
    calla #guarda
; bloque 0181
    mova #zona+128, r14
    mova #zona+70, r11
    mov #0x5, r2
    cmpx.w @r11, -118(r14)
    calla #guarda
; bloque 0182
    mov #0, r15
    mov #0x6, r2
    jge 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0183
    mov #0x0000, r11
    mov #0x0020, r4
    mov #0x0000, r9
    mov #0x4, r2
    rpt r9 { subx.a r11, r4
    calla #guarda
; bloque 0184
    mov #0x0000, r9
    mov #0x0080, r10
    mov #0x5, r2
    pushm.a #2, r10
    popm.a #2, r15
    calla #guarda
; bloque 0185
    mova #0x46556, r11
    mov #0x6, r2
    bitx.w r11, &zona+128
    calla #guarda
; bloque 0186
    mov #0, r15
    mov #0x6, r2
    jc 1f
    mov #1, r15
1:
    calla #guarda
; bloque 0187
    mova #0x35813, r9
    mov #0x0000, r10
    mova #0xB3DC8, r11
    mova #0x47FA1, r12
    mova #0x8E29F, r13
    mov #0x0, r2
    pushm.a #5, r13
    popm.a #5, r15
    calla #guarda
; bloque 0188
    mov #0x2, r2
    bitx.b &zona+32, zona+107
    calla #guarda
; bloque 0189
    mova #0x80000, r12
    mova #0xFFFFF, r7
    mova #zona+128, r7
    mov #0x6, r2
    mova r12, -126(r7)
    calla #guarda
; bloque 018a
    mov #0x1, r2
    movx.w #0, zona+34
    calla #guarda
; bloque 018b
    mov #0x3, r2
    bisx.b zona+52, &zona+189
    calla #guarda
; bloque 018c
    mova #0x7FFFE, r10
    mov #0x2, r2
    bicx.b r10, zona+73
    calla #guarda
; bloque 018d
    mov #0x0000, r10
    mov #0x5, r2
    rrcx.b r10
    calla #guarda
; bloque 018e
    mova #zona+114, r4
    mov #0x2, r2
    bicx.b @r4+, &zona+15
    calla #guarda
; bloque 018f
    mova #zona+128, r14
    mov #0x3, r2
    bitx.w #0xE22E, -14(r14)
    calla #guarda
; bloque 0190
    mov #0x0000, r5
    mov #0x3, r2
    rrc.w r5
    calla #guarda
; bloque 0191
    mova #0x37201, r5
    mov #0x1, r2
    rra.w r5
    calla #guarda
; bloque 0192
    mova #zona+128, r9
    mova #zona+128, r15
    mov #0x5, r2
    bic.b 64(r15), -38(r9)
    calla #guarda
; bloque 0193
    mova #zona+118, r12
    mov #0x2, r2
    subx.a @r12+, &zona+130
    calla #guarda
; bloque 0194
    mova #zona+128, r10
    mova #zona+128, r11
    mov #0x5, r2
    bic.b 104(r11), 115(r10)
    calla #guarda
; bloque 0195
    mova #0xF5B3D, r12
    mov #0x0, r2
    rrum.w #4, r12
    calla #guarda
; bloque 0196
    mov #0xD513, r11
    mov #0x6373, r7
    mov #0x7, r2
    mova #0x9C458, r7
    calla #guarda
; bloque 0197
    mova #0x1B769, r12
    mov #0x0, r2
    rrcm.w #4, r12
    calla #guarda
; bloque 0198
    mova #zona+128, r7
    mov #0x2, r2
    rrax.b 24(r7)
    calla #guarda
; bloque 0199
    mov #0, r15
    mov #0x0, r2
    calla #sub
    calla #guarda
; bloque 019a
    mova #0xB6413, r9
    mov #0x0, r2
    addx.w #0xFFFF, r9
    calla #guarda
; bloque 019b
    mov #0x0000, r7
    mov #0x0, r2
    rrcm.a #2, r7
    calla #guarda
; bloque 019c
    mova #0x79ADA, r13
    mov #0x7, r2
    addx.w #0xF97C, r13
    calla #guarda
; bloque 019d
    mova #0xFFFFF, r8
    mov #0x6, r2
    subc.w #2, r8
    calla #guarda
; bloque 019e
    mova #0xFFFFF, r5
    mov #0x6, r2
    rlam.w #1, r5
    calla #guarda
; bloque 019f
    mova #0x74C7A, r5
    mova #0x5ACE4, r4
    mov #0x4, r2
    xorx.b r4, r5
    calla #guarda
; bloque 01a0
    mova #zona+128, r9
    mov #0x1, r2
    bisx.b &zona+212, -36(r9)
    calla #guarda
; bloque 01a1
    mov #0x0002, r10
    mova #0xB0812, r6
    mov #0x5, r2
    rpt r6 { rrcx.b r10
    calla #guarda
; bloque 01a2
    mova #0x2FE8C, r8
    mov #0x6, r2
    rrum.a #1, r8
    calla #guarda
; bloque 01a3
    mova #zona+158, r6
    mov #0x4, r2
    subcx.w @r6+, &zona+68
    calla #guarda
; bloque 01a4
    mov #0x1, r2
    rrcx.a zona+106
    calla #guarda
; bloque 01a5
    mova #0x4AF34, r10
    mova #0x246B8, r4
    mov #0x1, r2
    andx.a r4, r10
    calla #guarda
; bloque 01a6
    mova #0xFFFFF, r15
    mov #0x3, r2
    subx.w r15, &zona+134
    calla #guarda
; bloque 01a7
    mova #0xFEDF3, r4
    mov #0x0002, r11
    mov #0x6, r2
    rpt #7 { addx.w r4, r11
    calla #guarda
; bloque 01a8
    mov #0x0075, r7
    mov #0x5, r2
    dadd.b #0, r7
    calla #guarda
; bloque 01a9
    mov #0x7867, r9
    mov #0x0999, r6
    mov #0x2, r2
    dadd.w r6, r9
    calla #guarda
; bloque 01aa
    mov #0x7, r2
    rrcx.b &zona+182
    calla #guarda
; bloque 01ab
    mova #zona+128, r10
    mov #0x4, r2
    xorx.a #0, -84(r10)
    calla #guarda
; bloque 01ac
    mov #0x0000, r6
    mov #0x7, r2
    rrux.w r6
    calla #guarda
; bloque 01ad
    mova #0x7FFFF, r10
    mova #0x98B60, r4
    mova #0x354D7, r9
    mov #0x1, r2
    rpt r9 { bicx.w r10, r4
    calla #guarda
; bloque 01ae
    mov #0x4281, r8
    mova #0xE6045, r4
    mova #zona+32, r8
    mov #0x4, r2
    mova @r8+, r4
    calla #guarda
; bloque 01af
    mova #0xC838F, r7
    mov #0x0000, r8
    mova #0x7FFFE, r9
    mov #0x0002, r10
    mov #0x7, r2
    pushm.w #4, r10
    popm.w #4, r14
    calla #guarda
; bloque 01b0
    mova #0x19032, r6
    mova #0x1809F, r13
    mov #0x1, r2
    addcx.b r13, r6
    calla #guarda
; bloque 01b1
    mova #zona+128, r13
    mov #0x0, r2
    rrux.w 116(r13)
    calla #guarda
; bloque 01b2
    mova #0x56277, r7
    mov #0x1, r2
    rrux.b r7
    calla #guarda
; bloque 01b3
    mov #0x4813, r12
    mov #0x6729, r7
    mov #0x3, r2
    dadd.w r7, r12
    calla #guarda
; bloque 01b4
    mova #0x7FFFE, r6
    mov #0x3, r2
    rrcm.w #1, r6
    calla #guarda
; bloque 01b5
    mova #zona+89, r9
    mov #0x7, r2
    subx.b @r9+, zona+36
    calla #guarda
; bloque 01b6
    mov #0x7, r2
    addx.a #0x3076E, zona+122
    calla #guarda
; bloque 01b7
    mov #0, r15
    mova #tabla_a, r6
    mov #0x3, r2
    calla @r6+
    calla #guarda
; bloque 01b8
    mova #0xDDE6B, r15
    mov #0x0020, r6
    mov #0x4, r2
    xor.b r6, r15
    calla #guarda
; bloque 01b9
    mova #zona+128, r5
    mov #0xC6A7, r13
    mov #0x4, r2
    sub.b r13, -17(r5)
    calla #guarda
; bloque 01ba
    mov #0x0, r2
    movx.b &zona+29, &zona+3
    calla #guarda
; bloque 01bb
    mova #0x7B0A7, r13
    mova #zona+128, r6
    mov #0x6, r2
    bis.w 42(r6), r13
    calla #guarda
; bloque 01bc
    mova #0xFFFFF, r11
    mov #0x4, r2
    pushm.a #1, r11
    popm.a #1, r11
    calla #guarda
; bloque 01bd
    mova #0xAB627, r14
    mov #0x4, r2
    movx.w zona+228, r14
    calla #guarda
; bloque 01be
    mova #0x6EA44, r14
    mova #0xD481F, r15
    mov #0x5, r2
    pushm.w #2, r15
    popm.w #2, r5
    calla #guarda
; bloque 01bf
    mov #0, r15
    mov #0x1, r2
    calla &tabla_a
    calla #guarda
; bloque 01c0
    mova #zona+42, r8
    mov #0x5, r2
    swpb @r8
    calla #guarda
; bloque 01c1
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
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
