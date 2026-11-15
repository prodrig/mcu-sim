    .text
    .global prueba
    .global fin
prueba:
    push r4
    push r5
    push r6
    push r7
    push r8
    push r9
    push r10
    mov r1, &sp_guardado
    mov #res, &resptr
; bloque 0000
    mov #zona+128, r15
    mov #0x103, r2
    and.w #0x0, -54(r15)
    call #guarda
; bloque 0001
    mov #0, r15
    mov #0x105, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0002
    mov #0x8CF8, r12
    mov #0x105, r2
    sxt r12
    call #guarda
; bloque 0003
    mov #zona+128, r13
    mov #0x1, r2
    subc.b -16(r13), &zona+239
    call #guarda
; bloque 0004
    mov #0x102, r2
    swpb &zona+24
    call #guarda
; bloque 0005
    mov #0x105, r2
    push.b #2
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0006
    mov #0x5634, r5
    mov #0x106, r2
    and.b r5, &zona+150
    call #guarda
; bloque 0007
    mov #zona+100, r7
    mov #0x5, r2
    rrc.b @r7+
    call #guarda
; bloque 0008
    mov #0x0000, r10
    mov #0x106, r2
    sub.b r10, zona+241
    call #guarda
; bloque 0009
    mov #0x0000, r10
    mov #0x3, r2
    mov.b #0xFF, r10
    call #guarda
; bloque 000a
    mov #0x00E9, r6
    mov #0x105, r2
    swpb r6
    call #guarda
; bloque 000b
    mov #0x2, r2
    swpb zona+194
    call #guarda
; bloque 000c
    mov #zona+124, r10
    mov #0x3, r2
    rra.w @r10
    call #guarda
; bloque 000d
    mov #zona+198, r9
    mov #0x105, r2
    bis.w @r9+, zona+120
    call #guarda
; bloque 000e
    mov #0, r15
    mov #0x103, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 000f
    mov #zona+128, r10
    mov #0x7, r2
    swpb -20(r10)
    call #guarda
; bloque 0010
    mov #0x4, r2
    xor.w #8, &zona+168
    call #guarda
; bloque 0011
    mov #0x231F, r5
    mov #0x1, r2
    add.b r5, &zona+210
    call #guarda
; bloque 0012
    mov #0, r15
    mov #0x5, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0013
    mov #0, r15
    mov #0x103, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0014
    mov #0x004B, r8
    mov #0x106, r2
    sub.w #0x788E, r8
    call #guarda
; bloque 0015
    mov #0x0004, r9
    mov #0x1, r2
    dadd.b #1, r9
    call #guarda
; bloque 0016
    mov #0x7FFF, r11
    mov #0xD672, r13
    mov #0x4, r2
    subc.b r13, r11
    call #guarda
; bloque 0017
    mov #0x0004, r7
    mov #zona+62, r5
    mov #0x105, r2
    bic.w @r5+, r7
    call #guarda
; bloque 0018
    mov #0x105, r2
    sxt &zona+108
    call #guarda
; bloque 0019
    mov #0x5, r2
    swpb zona+50
    call #guarda
; bloque 001a
    mov #0x4428, r13
    mov #0x9C1B, r8
    mov #0x2, r2
    bit.b r8, r13
    call #guarda
; bloque 001b
    mov #0x0C26, r6
    mov #0x6, r2
    xor.b #1, r6
    call #guarda
; bloque 001c
    mov #0, r15
    mov #0x0, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 001d
    mov #0x103, r2
    subc.b #4, &zona+250
    call #guarda
; bloque 001e
    mov #0x4000, r10
    mov #0x3, r2
    subc.w &zona+28, r10
    call #guarda
; bloque 001f
    mov #0xC0F2, r13
    mov #0x7, r2
    sxt r13
    call #guarda
; bloque 0020
    mov #0x0008, r7
    mov #0x5, r2
    add.w r7, &zona+148
    call #guarda
; bloque 0021
    mov #zona+128, r8
    mov #0x103, r2
    swpb -42(r8)
    call #guarda
; bloque 0022
    mov #0x67DB, r4
    mov #0x2, r2
    bic.b #1, r4
    call #guarda
; bloque 0023
    mov #0xAF9E, r5
    mov #0xDF04, r11
    mov #0x1, r2
    sub.b r11, r5
    call #guarda
; bloque 0024
    mov #0x3D1E, r15
    mov #zona+65, r7
    mov #0x106, r2
    subc.b @r7+, r15
    call #guarda
; bloque 0025
    mov #zona+128, r8
    mov #zona+142, r7
    mov #0x5, r2
    subc.w @r7+, -120(r8)
    call #guarda
; bloque 0026
    mov #0x3, r2
    rrc.b zona+128
    call #guarda
; bloque 0027
    mov #zona+128, r7
    mov #zona+129, r14
    mov #0x6, r2
    addc.b @r14+, 122(r7)
    call #guarda
; bloque 0028
    mov #zona+128, r12
    mov #zona+128, r13
    mov #0x101, r2
    bic.b -46(r13), -54(r12)
    call #guarda
; bloque 0029
    mov #0x2, r2
    subc.b #0xC6, zona+126
    call #guarda
; bloque 002a
    mov #0x4, r2
    add.w &zona+242, zona+242
    call #guarda
; bloque 002b
    mov #0x0020, r9
    mov #0x1, r2
    dadd.b #0x24, r9
    call #guarda
; bloque 002c
    mov #zona+120, r4
    mov #0x3, r2
    push.w @r4+
    pop r15
    call #guarda
; bloque 002d
    mov #0x0002, r7
    mov #0x3, r2
    rra.b r7
    call #guarda
; bloque 002e
    mov #0x4C11, r7
    mov #0x103, r2
    and.w &zona+224, r7
    call #guarda
; bloque 002f
    mov #0x106, r2
    bis.w &zona+230, zona+4
    call #guarda
; bloque 0030
    mov #0x8000, r8
    mov #0x6, r2
    rrc.w r8
    call #guarda
; bloque 0031
    mov #0x105, r2
    sub.b zona+225, zona+49
    call #guarda
; bloque 0032
    mov #zona+41, r4
    mov #0x107, r2
    rra.b @r4+
    call #guarda
; bloque 0033
    mov #zona+128, r12
    mov #0x7, r2
    push.b -20(r12)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0034
    mov #zona+128, r4
    mov #0x101, r2
    rra.w -124(r4)
    call #guarda
; bloque 0035
    mov #zona+128, r10
    mov #0x3, r2
    bit.b -44(r10), &zona+33
    call #guarda
; bloque 0036
    mov #zona+128, r5
    mov #0x7FFE, r13
    mov #0x0, r2
    subc.b r13, 32(r5)
    call #guarda
; bloque 0037
    mov #0x7FFF, r5
    mov #0x7, r2
    and.b r5, zona+227
    call #guarda
; bloque 0038
    mov #0x2, r2
    subc.w #4, zona+180
    call #guarda
; bloque 0039
    mov #0, r15
    mov #0x101, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 003a
    mov #zona+120, r8
    mov #0x101, r2
    rrc.w @r8+
    call #guarda
; bloque 003b
    mov #0x55C1, r8
    mov #zona+238, r7
    mov #0x0, r2
    xor.w @r7, r8
    call #guarda
; bloque 003c
    mov #0x3, r2
    push.b &zona+105
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 003d
    mov #zona+7, r13
    mov #0x0, r2
    subc.b @r13, zona+114
    call #guarda
; bloque 003e
    mov #zona+110, r15
    mov #0x6, r2
    swpb @r15+
    call #guarda
; bloque 003f
    mov #0x104, r2
    sub.w zona+96, zona+186
    call #guarda
; bloque 0040
    mov #0, r15
    mov #0x101, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0041
    mov #zona+128, r6
    mov #0x104, r2
    rra.w 108(r6)
    call #guarda
; bloque 0042
    mov #zona+128, r4
    mov #0x102, r2
    subc.w zona+144, -96(r4)
    call #guarda
; bloque 0043
    mov #0x105, r2
    addc.w #2, zona+192
    call #guarda
; bloque 0044
    mov #0x7FFE, r8
    mov #0x103, r2
    subc.b #0x26, r8
    call #guarda
; bloque 0045
    mov #0x4, r2
    sxt zona+210
    call #guarda
; bloque 0046
    mov #0x0658, r9
    mov #0x5, r2
    rra.w r9
    call #guarda
; bloque 0047
    mov #0x0000, r10
    mov #0x107, r2
    rrc.w r10
    call #guarda
; bloque 0048
    mov #0, r15
    mov #tabla, r6
    mov #0x107, r2
    call @r6+
    call #guarda
; bloque 0049
    mov #0xCA19, r13
    mov #0x6, r2
    swpb r13
    call #guarda
; bloque 004a
    mov #0x0000, r15
    mov #0x102, r2
    rrc.b r15
    call #guarda
; bloque 004b
    mov #0xFFFF, r13
    mov #0x2, r2
    and.b r13, &zona+39
    call #guarda
; bloque 004c
    mov #0x100, r2
    xor.w #1, &zona+60
    call #guarda
; bloque 004d
    mov #zona+72, r13
    mov #0x106, r2
    sxt @r13
    call #guarda
; bloque 004e
    mov #0x6, r2
    rrc.b &zona+234
    call #guarda
; bloque 004f
    mov #0x1346, r12
    mov #0x102, r2
    cmp.w #-1, r12
    call #guarda
; bloque 0050
    mov #0x0331, r4
    mov #0x8335, r5
    mov #0x5, r2
    dadd.w r5, r4
    call #guarda
; bloque 0051
    mov #0x3BB5, r11
    mov #0x105, r2
    rra.b r11
    call #guarda
; bloque 0052
    mov #0xFFFF, r13
    mov #0x0, r2
    and.w r13, zona+96
    call #guarda
; bloque 0053
    mov #zona+128, r6
    mov #0x103, r2
    push.w 40(r6)
    pop r15
    call #guarda
; bloque 0054
    mov #0x102, r2
    sxt &zona+120
    call #guarda
; bloque 0055
    mov #0, r15
    mov #0x105, r2
    call &tabla
    call #guarda
; bloque 0056
    mov #0, r15
    mov #0x104, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0057
    mov #0x7FFE, r13
    mov #0x2, r2
    and.w &zona+140, r13
    call #guarda
; bloque 0058
    mov #0xA31B, r7
    mov #0xFA92, r6
    mov #0x2, r2
    and.b r6, r7
    call #guarda
; bloque 0059
    mov #zona+128, r11
    mov #zona+192, r6
    mov #0x5, r2
    and.w @r6, 24(r11)
    call #guarda
; bloque 005a
    mov #zona+128, r11
    mov #0x106, r2
    push.b -80(r11)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 005b
    mov #0xC733, r7
    mov #zona+192, r14
    mov #0x102, r2
    xor.w @r14, r7
    call #guarda
; bloque 005c
    mov #0, r15
    mov #0x4, r2
    call #sub
    call #guarda
; bloque 005d
    mov #0x6F32, r15
    mov #0x0, r2
    and.b #0xFF, r15
    call #guarda
; bloque 005e
    mov #zona+128, r11
    mov #zona+128, r8
    mov #0x106, r2
    xor.w -86(r8), 100(r11)
    call #guarda
; bloque 005f
    mov #0x7411, r12
    mov #0x7, r2
    dadd.w #0x428, r12
    call #guarda
; bloque 0060
    mov #0, r15
    mov #0x5, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0061
    mov #0, r15
    mov #sub, r13
    mov #0x7, r2
    call r13
    call #guarda
; bloque 0062
    mov #zona+128, r15
    mov #0x2, r2
    mov.w &zona+54, -10(r15)
    call #guarda
; bloque 0063
    mov #0, r15
    mov #0x3, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0064
    mov #0x107, r2
    sxt zona+204
    call #guarda
; bloque 0065
    mov #zona+128, r8
    mov #0x0, r2
    bic.b r8, -52(r8)
    call #guarda
; bloque 0066
    mov #zona+192, r15
    mov #0x107, r2
    bis.b @r15, zona+199
    call #guarda
; bloque 0067
    mov #0, r15
    mov #tabla, r10
    mov #0x1, r2
    call @r10
    call #guarda
; bloque 0068
    mov #0x4, r2
    bic.w #2, zona+248
    call #guarda
; bloque 0069
    mov #0x7FFE, r6
    mov #0x102, r2
    swpb r6
    call #guarda
; bloque 006a
    mov #zona+196, r9
    mov #0x107, r2
    sxt @r9
    call #guarda
; bloque 006b
    mov #zona+174, r11
    mov #0x101, r2
    rrc.w @r11
    call #guarda
; bloque 006c
    mov #zona+226, r8
    mov #0x3, r2
    push.w @r8+
    pop r15
    call #guarda
; bloque 006d
    mov #zona+128, r15
    mov #0x101, r2
    push.b -7(r15)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 006e
    mov #0, r15
    mov #0x2, r2
    call tabla
    call #guarda
; bloque 006f
    mov #0x729F, r4
    mov #zona+128, r9
    mov #0x7, r2
    bic.b -34(r9), r4
    call #guarda
; bloque 0070
    mov #0x9EC2, r4
    mov #zona+85, r8
    mov #0x100, r2
    bis.b @r8+, r4
    call #guarda
; bloque 0071
    mov #0xFFFF, r12
    mov #0x105, r2
    add.w r12, zona+52
    call #guarda
; bloque 0072
    mov #zona+196, r4
    mov #0x3, r2
    mov.w @r4, &zona+242
    call #guarda
; bloque 0073
    mov #0x1013, r15
    mov #0x0, r2
    dadd.w #1, r15
    call #guarda
; bloque 0074
    mov #0x7FFF, r15
    mov #0x3, r2
    sub.w #0xDE8, r15
    call #guarda
; bloque 0075
    mov #0xD9F3, r10
    mov #0x102, r2
    and.b zona+135, r10
    call #guarda
; bloque 0076
    mov #0x0, r2
    rrc.b &zona+103
    call #guarda
; bloque 0077
    mov #0, r15
    mov #0x4, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0078
    mov #zona+128, r6
    mov #0x106, r2
    bit.w &zona+202, -6(r6)
    call #guarda
; bloque 0079
    mov #zona+128, r7
    mov #0x101, r2
    sub.b &zona+89, 62(r7)
    call #guarda
; bloque 007a
    mov #zona+227, r8
    mov #0x104, r2
    rrc.b @r8
    call #guarda
; bloque 007b
    mov #zona+128, r6
    mov #0x103, r2
    xor.b #0x2D, 10(r6)
    call #guarda
; bloque 007c
    mov #0x7, r2
    push.w #0x8
    pop r15
    call #guarda
; bloque 007d
    mov #0x7A7F, r14
    mov #0x107, r2
    push.w r14
    pop r15
    call #guarda
; bloque 007e
    mov #zona+128, r5
    mov #0x106, r2
    rrc.w 72(r5)
    call #guarda
; bloque 007f
    mov #0x1, r2
    bit.b #-1, zona+130
    call #guarda
; bloque 0080
    mov #zona+128, r14
    mov #zona+128, r11
    mov #0x1, r2
    addc.b 47(r11), -99(r14)
    call #guarda
; bloque 0081
    mov #0x106, r2
    swpb &zona+58
    call #guarda
; bloque 0082
    mov #0xD869, r9
    mov #0x107, r2
    rra.b r9
    call #guarda
; bloque 0083
    mov #0x3BF4, r6
    mov #0x5, r2
    swpb r6
    call #guarda
; bloque 0084
    mov #0xFB5D, r11
    mov #0xE9C8, r5
    mov #0x2, r2
    and.w r5, r11
    call #guarda
; bloque 0085
    mov #0x8000, r4
    mov #zona+128, r5
    mov #0x107, r2
    add.b -99(r5), r4
    call #guarda
; bloque 0086
    mov #0xE99D, r11
    mov #0x106, r2
    sxt r11
    call #guarda
; bloque 0087
    mov #0x0000, r7
    mov #0x106, r2
    mov.w #0xDB97, r7
    call #guarda
; bloque 0088
    mov #zona+24, r12
    mov #0x105, r2
    swpb @r12+
    call #guarda
; bloque 0089
    mov #0x104, r2
    sxt &zona+34
    call #guarda
; bloque 008a
    mov #0, r15
    mov #0x7, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 008b
    mov #0x5B4C, r14
    mov #0x1, r2
    subc.b #1, r14
    call #guarda
; bloque 008c
    mov #zona+64, r15
    mov #0x5, r2
    push.w @r15
    pop r15
    call #guarda
; bloque 008d
    mov #0x7FFF, r6
    mov #0x7FFF, r5
    mov #0x107, r2
    and.w r5, r6
    call #guarda
; bloque 008e
    mov #0x7FFE, r14
    mov #0x104, r2
    sxt r14
    call #guarda
; bloque 008f
    mov #0x7910, r8
    mov #0x106, r2
    add.w #0x90B0, r8
    call #guarda
; bloque 0090
    mov #0xFFFF, r12
    mov #0x100, r2
    addc.b &zona+228, r12
    call #guarda
; bloque 0091
    mov #0x105, r2
    rra.w &zona+228
    call #guarda
; bloque 0092
    mov #0x7621, r4
    mov #0x6, r2
    add.b #0x80, r4
    call #guarda
; bloque 0093
    mov #zona+226, r9
    mov #0x3, r2
    rrc.b @r9
    call #guarda
; bloque 0094
    mov #0x105, r2
    rra.w zona+180
    call #guarda
; bloque 0095
    mov #0x7852, r11
    mov #zona+128, r14
    mov #0x5, r2
    sub.w -94(r14), r11
    call #guarda
; bloque 0096
    mov #0x0093, r7
    mov #0x0023, r14
    mov #0x0, r2
    dadd.b r14, r7
    call #guarda
; bloque 0097
    mov #0, r15
    mov #0x104, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0098
    mov #0x8D9E, r13
    mov #0x6, r2
    bit.w #0xFFFF, r13
    call #guarda
; bloque 0099
    mov #zona+54, r7
    mov #0x7, r2
    rrc.w @r7+
    call #guarda
; bloque 009a
    mov #zona+47, r8
    mov #0x102, r2
    add.b @r8, &zona+49
    call #guarda
; bloque 009b
    mov #0x5, r2
    subc.b #2, &zona+181
    call #guarda
; bloque 009c
    mov #0, r15
    mov #0x104, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 009d
    mov #0x8BE7, r12
    mov #0x5E7D, r8
    mov #0x0, r2
    and.b r8, r12
    call #guarda
; bloque 009e
    mov #zona+128, r8
    mov #0xFFFF, r10
    mov #0x105, r2
    mov.b r10, -69(r8)
    call #guarda
; bloque 009f
    mov #zona+128, r6
    mov #0x4, r2
    addc.b zona+219, 72(r6)
    call #guarda
; bloque 00a0
    mov #0x4000, r15
    mov #0x5, r2
    cmp.b zona+237, r15
    call #guarda
; bloque 00a1
    mov #0x106, r2
    swpb &zona+12
    call #guarda
; bloque 00a2
    mov #0x8C25, r15
    mov #0x2, r2
    swpb r15
    call #guarda
; bloque 00a3
    mov #zona+34, r5
    mov #0x105, r2
    cmp.b @r5+, zona+10
    call #guarda
; bloque 00a4
    mov #zona+128, r13
    mov #0x106, r2
    swpb 20(r13)
    call #guarda
; bloque 00a5
    mov #zona+128, r7
    mov #0x6, r2
    bic.w 112(r7), &zona+76
    call #guarda
; bloque 00a6
    mov #zona+128, r12
    mov #0x4, r2
    rra.w 90(r12)
    call #guarda
; bloque 00a7
    mov #0x2, r2
    swpb &zona+92
    call #guarda
; bloque 00a8
    mov #0x335C, r4
    mov #0x4, r2
    bit.w #1, r4
    call #guarda
; bloque 00a9
    mov #0x3E3A, r6
    mov #0x1, r2
    and.b r6, zona+249
    call #guarda
; bloque 00aa
    mov #0, r15
    mov #sub, r14
    mov #0x2, r2
    call r14
    call #guarda
; bloque 00ab
    mov #0x7, r2
    bit.b #2, &zona+78
    call #guarda
; bloque 00ac
    mov #0xFFFF, r9
    mov #0x0, r2
    rra.w r9
    call #guarda
; bloque 00ad
    mov #zona+128, r6
    mov #0x7, r2
    sxt 96(r6)
    call #guarda
; bloque 00ae
    mov #0x101, r2
    swpb zona+170
    call #guarda
; bloque 00af
    mov #0x0000, r15
    mov #0x2, r2
    xor.w #8, r15
    call #guarda
; bloque 00b0
    mov #0x0055, r11
    mov #0x4, r2
    dadd.b #0x94, r11
    call #guarda
; bloque 00b1
    mov #zona+128, r14
    mov #0x106, r2
    rra.w -8(r14)
    call #guarda
; bloque 00b2
    mov #zona+128, r5
    mov #0x0, r2
    sxt -70(r5)
    call #guarda
; bloque 00b3
    mov #zona+202, r8
    mov #0x5, r2
    bis.w @r8, &zona+146
    call #guarda
; bloque 00b4
    mov #0, r15
    mov #0x106, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00b5
    mov #0, r15
    mov #tabla-4, r12
    mov #0x1, r2
    call 4(r12)
    call #guarda
; bloque 00b6
    mov #0xFFFF, r4
    mov #0x3, r2
    rrc.b r4
    call #guarda
; bloque 00b7
    mov #0x0000, r4
    mov #0x101, r2
    bit.w r4, zona+168
    call #guarda
; bloque 00b8
    mov #zona+50, r8
    mov #0x3, r2
    sxt @r8
    call #guarda
; bloque 00b9
    mov #zona+128, r9
    mov #0x106, r2
    xor.w #1, -14(r9)
    call #guarda
; bloque 00ba
    mov #0x0000, r14
    mov #0x6, r2
    rra.w r14
    call #guarda
; bloque 00bb
    mov #0, r15
    mov #0x103, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00bc
    mov #zona+128, r8
    mov #zona+128, r9
    mov #0x104, r2
    bis.w 4(r9), -114(r8)
    call #guarda
; bloque 00bd
    mov #0x4, r2
    bic.w #0x100, &zona+40
    call #guarda
; bloque 00be
    mov #0, r15
    mov #0x106, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00bf
    mov #0x0036, r12
    mov #0x3, r2
    dadd.b #0x67, r12
    call #guarda
; bloque 00c0
    mov #0x100, r2
    swpb &zona+180
    call #guarda
; bloque 00c1
    mov #0, r15
    mov #tabla, r5
    mov #0x103, r2
    call @r5+
    call #guarda
; bloque 00c2
    mov #0, r15
    mov #0x101, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c3
    mov #zona+128, r6
    mov #0x100, r2
    sub.b zona+210, -84(r6)
    call #guarda
; bloque 00c4
    mov #zona+128, r8
    mov #0x3, r2
    add.b #4, -23(r8)
    call #guarda
; bloque 00c5
    mov #0, r15
    mov #sub, r10
    mov #0x0, r2
    call r10
    call #guarda
; bloque 00c6
    mov #0x72AD, r6
    mov #0x106, r2
    subc.b #0xAB, r6
    call #guarda
; bloque 00c7
    mov #0, r15
    mov #tabla, r6
    mov #0x6, r2
    call @r6+
    call #guarda
; bloque 00c8
    mov #0x6A2A, r14
    mov #0x39A6, r10
    mov #0x4, r2
    bis.b r10, r14
    call #guarda
; bloque 00c9
    mov #0x2F7B, r12
    mov #zona+128, r11
    mov #0x7, r2
    mov.b -95(r11), r12
    call #guarda
; bloque 00ca
    mov #zona+13, r14
    mov #0x102, r2
    subc.b @r14, &zona+90
    call #guarda
; bloque 00cb
    mov #0x104, r2
    xor.b &zona+236, zona+38
    call #guarda
; bloque 00cc
    mov #zona+128, r9
    mov #zona+128, r8
    mov #0x5, r2
    subc.w -94(r8), -28(r9)
    call #guarda
; bloque 00cd
    mov #0x1, r2
    xor.b &zona+2, zona+207
    call #guarda
; bloque 00ce
    mov #0, r15
    mov #0x3, r2
    call tabla
    call #guarda
; bloque 00cf
    mov #zona+26, r13
    mov #0x106, r2
    push.w @r13
    pop r15
    call #guarda
; bloque 00d0
    mov #zona+11, r11
    mov #0x101, r2
    bis.b @r11+, zona+151
    call #guarda
; bloque 00d1
    mov #zona+128, r14
    mov #zona+151, r5
    mov #0x4, r2
    subc.b @r5, -118(r14)
    call #guarda
; bloque 00d2
    mov #0x3, r2
    rrc.w zona+54
    call #guarda
; bloque 00d3
    mov #zona+240, r9
    mov #0x101, r2
    sxt @r9+
    call #guarda
; bloque 00d4
    mov #0, r15
    mov #0x107, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d5
    mov #0xBCD2, r10
    mov #0x107, r2
    push.w r10
    pop r15
    call #guarda
; bloque 00d6
    mov #zona+128, r15
    mov #zona+134, r14
    mov #0x105, r2
    bis.w @r14+, -118(r15)
    call #guarda
; bloque 00d7
    mov #0x0434, r11
    mov #0x2, r2
    dadd.w #0x72, r11
    call #guarda
; bloque 00d8
    mov #0x0000, r10
    mov #0x6, r2
    subc.w r10, zona+130
    call #guarda
; bloque 00d9
    mov #0x6045, r6
    mov #0x0, r2
    xor.b r6, &zona+9
    call #guarda
; bloque 00da
    mov #0, r15
    mov #0x2, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00db
    mov #zona+128, r4
    mov #0x7FFE, r8
    mov #0x3, r2
    mov.w r8, -64(r4)
    call #guarda
; bloque 00dc
    mov #0, r15
    mov #0x103, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00dd
    mov #0x8000, r14
    mov #zona+247, r11
    mov #0x100, r2
    bis.b @r11, r14
    call #guarda
; bloque 00de
    mov #zona+128, r14
    mov #0x4, r2
    add.w -62(r14), zona+230
    call #guarda
; bloque 00df
    mov #0x4BEA, r7
    mov #0x100, r2
    cmp.b r7, zona+90
    call #guarda
; bloque 00e0
    mov #zona+128, r5
    mov #0x106, r2
    bic.b &zona+78, -43(r5)
    call #guarda
; bloque 00e1
    mov #0x105, r2
    cmp.b zona+128, &zona+65
    call #guarda
; bloque 00e2
    mov #0x0080, r14
    mov #0x0082, r5
    mov #0x1, r2
    dadd.b r5, r14
    call #guarda
; bloque 00e3
    mov #0xFFFF, r9
    mov #0x4, r2
    swpb r9
    call #guarda
; bloque 00e4
    mov #0xFFFF, r11
    mov #0x101, r2
    cmp.w #4, r11
    call #guarda
; bloque 00e5
    mov #0xFFFF, r5
    mov #0x7, r2
    bit.b #0x80, r5
    call #guarda
; bloque 00e6
    mov #zona+128, r7
    mov #0x102, r2
    sxt 72(r7)
    call #guarda
; bloque 00e7
    mov #zona+128, r8
    mov #zona+4, r9
    mov #0x106, r2
    subc.b @r9+, 47(r8)
    call #guarda
; bloque 00e8
    mov #zona+20, r14
    mov #0x101, r2
    add.w @r14, zona+4
    call #guarda
; bloque 00e9
    mov #0xC8EC, r12
    mov #0x3, r2
    addc.b r12, zona+245
    call #guarda
; bloque 00ea
    mov #0x6496, r13
    mov #0x1, r2
    dadd.w #0x9844, r13
    call #guarda
; bloque 00eb
    mov #0xFFFF, r6
    mov #0x0, r2
    bic.b &zona+11, r6
    call #guarda
; bloque 00ec
    mov #0, r15
    mov #0x0, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ed
    mov #0, r15
    mov #0x5, r2
    call tabla
    call #guarda
; bloque 00ee
    mov #0x5F37, r10
    mov #0x3, r2
    and.b #1, r10
    call #guarda
; bloque 00ef
    mov #0x6B5C, r9
    mov #0x101, r2
    rrc.w r9
    call #guarda
; bloque 00f0
    mov #0xE1AA, r9
    mov #zona+128, r12
    mov #0x2, r2
    cmp.b 29(r12), r9
    call #guarda
; bloque 00f1
    mov #0x0A66, r14
    mov #zona+147, r4
    mov #0x102, r2
    and.b @r4, r14
    call #guarda
; bloque 00f2
    mov #0x5CAF, r15
    mov #0x106, r2
    swpb r15
    call #guarda
; bloque 00f3
    mov #zona+128, r12
    mov #0x6, r2
    add.b #0x64, -26(r12)
    call #guarda
; bloque 00f4
    mov #0x2, r2
    mov.w &zona+180, zona+88
    call #guarda
; bloque 00f5
    mov #0xFFFF, r5
    mov #0x0, r2
    xor.b #0xDC, r5
    call #guarda
; bloque 00f6
    mov #0x105, r2
    and.b #0xEE, &zona+119
    call #guarda
; bloque 00f7
    mov #zona+32, r13
    mov #0x101, r2
    bis.b @r13, &zona+13
    call #guarda
; bloque 00f8
    mov #0x8364, r9
    mov #0x100, r2
    rra.w r9
    call #guarda
; bloque 00f9
    mov #zona+128, r10
    mov #zona+149, r11
    mov #0x4, r2
    mov.b @r11, -38(r10)
    call #guarda
; bloque 00fa
    mov #0, r15
    mov #0x107, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00fb
    mov #zona+128, r9
    mov #zona+189, r4
    mov #0x100, r2
    subc.b @r4, -110(r9)
    call #guarda
; bloque 00fc
    mov #0, r15
    mov #tabla, r8
    mov #0x101, r2
    call @r8
    call #guarda
; bloque 00fd
    mov #zona+122, r12
    mov #0x6, r2
    sxt @r12+
    call #guarda
; bloque 00fe
    mov #0x1, r2
    cmp.w #0xB426, &zona+26
    call #guarda
; bloque 00ff
    mov #0x7FFE, r9
    mov #zona+128, r7
    mov #0x106, r2
    bic.b 17(r7), r9
    call #guarda
; bloque 0100
    mov #0xE8B7, r14
    mov #0x5, r2
    and.b r14, &zona+250
    call #guarda
; bloque 0101
    mov #0x105, r2
    addc.w #0x4, zona+228
    call #guarda
; bloque 0102
    mov #0, r15
    mov #0x1, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0103
    mov #0x0000, r9
    mov #0x2, r2
    and.b r9, &zona+57
    call #guarda
; bloque 0104
    mov #zona+128, r14
    mov #0x630B, r6
    mov #0x7, r2
    subc.b r6, -72(r14)
    call #guarda
; bloque 0105
    mov #0x1899, r14
    mov #0x3, r2
    dadd.w #0x7963, r14
    call #guarda
; bloque 0106
    mov #0, r15
    mov #0x4, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0107
    mov #0x100, r2
    bit.w #0x0, zona+70
    call #guarda
; bloque 0108
    mov #0x2, r2
    and.b zona+50, zona+148
    call #guarda
; bloque 0109
    mov #0x2, r2
    swpb zona+32
    call #guarda
; bloque 010a
    mov #0xF226, r5
    mov #0x0, r2
    rra.w r5
    call #guarda
; bloque 010b
    mov #zona+28, r15
    mov #0x3, r2
    rrc.w @r15
    call #guarda
; bloque 010c
    mov #zona+160, r9
    mov #0x104, r2
    push.w @r9
    pop r15
    call #guarda
; bloque 010d
    mov #zona+198, r14
    mov #0x100, r2
    rrc.w @r14+
    call #guarda
; bloque 010e
    mov #zona+128, r6
    mov #0x3, r2
    and.w 36(r6), &zona+98
    call #guarda
; bloque 010f
    mov #0x958A, r10
    mov #0x73C2, r6
    mov #0x1, r2
    bis.b r6, r10
    call #guarda
; bloque 0110
    mov #zona+122, r8
    mov #0x5, r2
    swpb @r8+
    call #guarda
; bloque 0111
    mov #0, r15
    mov #tabla, r12
    mov #0x5, r2
    call @r12+
    call #guarda
; bloque 0112
    mov #zona+170, r10
    mov #0x0, r2
    sxt @r10+
    call #guarda
; bloque 0113
    mov #0x0070, r15
    mov #0x3, r2
    dadd.b #0, r15
    call #guarda
; bloque 0114
    mov #0, r15
    mov #sub, r8
    mov #0x102, r2
    call r8
    call #guarda
; bloque 0115
    mov #zona+128, r4
    mov #0x102, r2
    sub.w #0x8000, -18(r4)
    call #guarda
; bloque 0116
    mov #0x0, r2
    bis.w #0x68B0, zona+44
    call #guarda
; bloque 0117
    mov #0x5F27, r10
    mov #0x106, r2
    sub.w zona+18, r10
    call #guarda
; bloque 0118
    mov #0, r15
    mov #0x6, r2
    call &tabla
    call #guarda
; bloque 0119
    mov #0, r15
    mov #0x0, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 011a
    mov #0x1, r2
    rra.w &zona+224
    call #guarda
; bloque 011b
    mov #zona+128, r5
    mov #zona+82, r4
    mov #0x6, r2
    cmp.b @r4+, 57(r5)
    call #guarda
; bloque 011c
    mov #zona+128, r8
    mov #zona+120, r10
    mov #0x102, r2
    bit.w @r10+, -6(r8)
    call #guarda
; bloque 011d
    mov #zona+128, r9
    mov #0x0800, r4
    mov #0x101, r2
    xor.w r4, -34(r9)
    call #guarda
; bloque 011e
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 011f
    mov #0x7FFF, r14
    mov #0x0000, r8
    mov #0x2, r2
    sub.w r8, r14
    call #guarda
; bloque 0120
    mov #0x1, r2
    bis.b #-1, &zona+7
    call #guarda
; bloque 0121
    mov #zona+128, r4
    mov #0x7F49, r15
    mov #0x101, r2
    bit.w r15, 36(r4)
    call #guarda
; bloque 0122
    mov #zona+214, r11
    mov #0x106, r2
    addc.w @r11+, zona+236
    call #guarda
; bloque 0123
    mov #0x104, r2
    cmp.b zona+78, &zona+38
    call #guarda
; bloque 0124
    mov #0x106, r2
    sxt zona+226
    call #guarda
; bloque 0125
    mov #0x0DED, r5
    mov #0x6, r2
    sub.b r5, zona+22
    call #guarda
; bloque 0126
    mov #zona+128, r12
    mov #0x101, r2
    and.w &zona+194, 46(r12)
    call #guarda
; bloque 0127
    mov #0x7FFF, r12
    mov #zona+128, r11
    mov #0x100, r2
    subc.w -96(r11), r12
    call #guarda
; bloque 0128
    mov #0x102, r2
    rrc.w zona+4
    call #guarda
; bloque 0129
    mov #0x0, r2
    xor.b &zona+209, zona+247
    call #guarda
; bloque 012a
    mov #0x0000, r14
    mov #0x104, r2
    rra.w r14
    call #guarda
; bloque 012b
    mov #0x101, r2
    addc.b &zona+177, zona+197
    call #guarda
; bloque 012c
    mov #0xFFFF, r10
    mov #0x2, r2
    and.b r10, &zona+72
    call #guarda
; bloque 012d
    mov #0x7, r2
    and.b #0x58, zona+194
    call #guarda
; bloque 012e
    mov #0x0000, r10
    mov #0x1, r2
    cmp.b r10, zona+81
    call #guarda
; bloque 012f
    mov #0x3, r2
    cmp.w &zona+22, &zona+202
    call #guarda
; bloque 0130
    mov #zona+148, r15
    mov #0x4, r2
    rrc.b @r15
    call #guarda
; bloque 0131
    mov #zona+211, r14
    mov #0x102, r2
    bic.b @r14, zona+124
    call #guarda
; bloque 0132
    mov #zona+24, r10
    mov #0x6, r2
    sxt @r10+
    call #guarda
; bloque 0133
    mov #zona+144, r14
    mov #0x7, r2
    sxt @r14+
    call #guarda
; bloque 0134
    mov #0x0000, r7
    mov #0x6, r2
    bit.b zona+207, r7
    call #guarda
; bloque 0135
    mov #zona+128, r11
    mov #0x6, r2
    bit.b -118(r11), &zona+146
    call #guarda
; bloque 0136
    mov #0xFFFF, r10
    mov #0x103, r2
    mov.w zona+142, r10
    call #guarda
; bloque 0137
    mov #0x7FFF, r7
    mov #zona+46, r14
    mov #0x101, r2
    cmp.b @r14+, r7
    call #guarda
; bloque 0138
    mov #0, r15
    mov #0x0, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0139
    mov #0x102, r2
    sxt &zona+198
    call #guarda
; bloque 013a
    mov #0xB6E6, r11
    mov #0x105, r2
    swpb r11
    call #guarda
; bloque 013b
    mov #zona+128, r12
    mov #0x100, r2
    addc.b #1, -1(r12)
    call #guarda
; bloque 013c
    mov #zona+246, r8
    mov #0x1, r2
    sxt @r8+
    call #guarda
; bloque 013d
    mov #0x5, r2
    swpb zona+234
    call #guarda
; bloque 013e
    mov #0, r15
    mov #0x5, r2
    call &tabla
    call #guarda
; bloque 013f
    mov #zona+128, r11
    mov #0x106, r2
    bit.b 33(r11), zona+18
    call #guarda
; bloque 0140
    mov #zona+128, r10
    mov #0x107, r2
    sub.b 12(r10), zona+137
    call #guarda
; bloque 0141
    mov #0, r15
    mov #tabla-4, r6
    mov #0x2, r2
    call 4(r6)
    call #guarda
; bloque 0142
    mov #0x7245, r10
    mov #0x887A, r12
    mov #0x6, r2
    bic.b r12, r10
    call #guarda
; bloque 0143
    mov #zona+128, r4
    mov #0x1, r2
    bic.w #1, 34(r4)
    call #guarda
; bloque 0144
    mov #0x0001, r13
    mov #0x4, r2
    push.w r13
    pop r15
    call #guarda
; bloque 0145
    mov #0x5813, r4
    mov #0x0156, r7
    mov #0x1, r2
    dadd.w r7, r4
    call #guarda
; bloque 0146
    mov #0x5, r2
    swpb &zona+144
    call #guarda
; bloque 0147
    mov #0x103, r2
    swpb zona+64
    call #guarda
; bloque 0148
    mov #0x48F7, r5
    mov #0x4, r2
    swpb r5
    call #guarda
; bloque 0149
    mov #0xEAEF, r15
    mov #0x102, r2
    mov.w zona+148, r15
    call #guarda
; bloque 014a
    mov #0, r15
    mov #0x100, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 014b
    mov #0x8829, r7
    mov #0x105, r2
    bit.w #0x1D1A, r7
    call #guarda
; bloque 014c
    mov #0xFFFF, r5
    mov #0x1, r2
    subc.w #4, r5
    call #guarda
; bloque 014d
    mov #zona+24, r10
    mov #0x107, r2
    push.w @r10
    pop r15
    call #guarda
; bloque 014e
    mov #0x100, r2
    add.b zona+94, &zona+199
    call #guarda
; bloque 014f
    mov #0x104, r2
    bis.w #2, zona+100
    call #guarda
; bloque 0150
    mov #zona+128, r14
    mov #0x3078, r12
    mov #0x105, r2
    bis.b r12, -15(r14)
    call #guarda
; bloque 0151
    mov #0xD907, r13
    mov #0x6BF3, r12
    mov #0x2, r2
    addc.w r12, r13
    call #guarda
; bloque 0152
    mov #0x6187, r9
    mov #zona+182, r10
    mov #0x7, r2
    bis.b @r10, r9
    call #guarda
; bloque 0153
    mov #0x102, r2
    rrc.b zona+77
    call #guarda
; bloque 0154
    mov #0, r15
    mov #0x106, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0155
    mov #0, r15
    mov #0x101, r2
    call &tabla
    call #guarda
; bloque 0156
    mov #0x61DC, r11
    mov #zona+171, r6
    mov #0x4, r2
    subc.b @r6, r11
    call #guarda
; bloque 0157
    mov #0, r15
    mov #0x103, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0158
    mov #0x0000, r11
    mov #0x7, r2
    bit.w #0x651A, r11
    call #guarda
; bloque 0159
    mov #0x7596, r5
    mov #0x0, r2
    xor.w &zona+102, r5
    call #guarda
; bloque 015a
    mov #0x1, r2
    rrc.w &zona+240
    call #guarda
; bloque 015b
    mov #0x105, r2
    push.w #0x8
    pop r15
    call #guarda
; bloque 015c
    mov #0x1816, r7
    mov #0x5, r2
    bit.b #1, r7
    call #guarda
; bloque 015d
    mov #0x7286, r4
    mov #0x9603, r10
    mov #0x5, r2
    dadd.w r10, r4
    call #guarda
; bloque 015e
    mov #0x5706, r7
    mov #0x2, r2
    dadd.w #1, r7
    call #guarda
; bloque 015f
    mov #0x5168, r12
    mov #0x104, r2
    add.b zona+28, r12
    call #guarda
; bloque 0160
    mov #0x3D79, r9
    mov #0x2, r2
    bit.b r9, zona+30
    call #guarda
; bloque 0161
    mov #zona+128, r11
    mov #0x4, r2
    and.b -11(r11), &zona+73
    call #guarda
; bloque 0162
    mov #zona+166, r4
    mov #0x4, r2
    swpb @r4+
    call #guarda
; bloque 0163
    mov #0xF9D4, r5
    mov #zona+128, r13
    mov #0x2, r2
    add.w 6(r13), r5
    call #guarda
; bloque 0164
    mov #zona+128, r8
    mov #0x1, r2
    and.w zona+222, -108(r8)
    call #guarda
; bloque 0165
    mov #0x107, r2
    and.w #0, &zona+240
    call #guarda
; bloque 0166
    mov #zona+104, r7
    mov #0x105, r2
    rrc.b @r7+
    call #guarda
; bloque 0167
    mov #0x100, r2
    add.w &zona+250, &zona+10
    call #guarda
; bloque 0168
    mov #0x0, r2
    bic.b #4, &zona+124
    call #guarda
; bloque 0169
    mov #0x7FFF, r9
    mov #zona+64, r6
    mov #0x3, r2
    bic.w @r6, r9
    call #guarda
; bloque 016a
    mov #0x100, r2
    add.b zona+79, &zona+120
    call #guarda
; bloque 016b
    mov #0x103, r2
    rrc.w zona+8
    call #guarda
; bloque 016c
    mov #zona+92, r13
    mov #0x101, r2
    rrc.w @r13+
    call #guarda
; bloque 016d
    mov #0x0080, r9
    mov #zona+128, r4
    mov #0x101, r2
    xor.w 52(r4), r9
    call #guarda
; bloque 016e
    mov #0, r15
    mov #0x5, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 016f
    mov #0x7, r2
    rra.w zona+126
    call #guarda
; bloque 0170
    mov #0x4A47, r8
    mov #0x101, r2
    bic.w r8, &zona+68
    call #guarda
; bloque 0171
    mov #0x1538, r11
    mov #0x0, r2
    dadd.w #0x7209, r11
    call #guarda
; bloque 0172
    mov #0xF51D, r10
    mov #0x7, r2
    subc.b &zona+54, r10
    call #guarda
; bloque 0173
    mov #0, r15
    mov #0x106, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0174
    mov #0x05F4, r8
    mov #0x6, r2
    bis.w &zona+72, r8
    call #guarda
; bloque 0175
    mov #0, r15
    mov #0x1, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0176
    mov #0x644A, r14
    mov #zona+63, r11
    mov #0x101, r2
    addc.b @r11+, r14
    call #guarda
; bloque 0177
    mov #0x7, r2
    xor.b zona+206, zona+155
    call #guarda
; bloque 0178
    mov #0xFEC1, r7
    mov #0x7FFF, r5
    mov #0x1, r2
    cmp.b r5, r7
    call #guarda
; bloque 0179
    mov #0x7BFA, r6
    mov #0xB21F, r13
    mov #0x3, r2
    addc.b r13, r6
    call #guarda
; bloque 017a
    mov #0x649D, r14
    mov #0x100, r2
    sub.w &zona+22, r14
    call #guarda
; bloque 017b
    mov #0x9452, r11
    mov #0x3, r2
    dadd.w #2, r11
    call #guarda
; bloque 017c
    mov #0x354D, r14
    mov #0xD83C, r4
    mov #0x103, r2
    and.b r4, r14
    call #guarda
; bloque 017d
    mov #0x2000, r12
    mov #0x0, r2
    and.w &zona+22, r12
    call #guarda
; bloque 017e
    mov #0x105, r2
    push.b &zona+81
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 017f
    mov #0x822A, r12
    mov #zona+96, r11
    mov #0x102, r2
    sub.w @r11+, r12
    call #guarda
; bloque 0180
    mov #zona+128, r14
    mov #0x895F, r13
    mov #0x0, r2
    subc.b r13, -74(r14)
    call #guarda
; bloque 0181
    mov #0, r15
    mov #0x4, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0182
    mov #0x5E01, r15
    mov #0x7FFE, r7
    mov #0x6, r2
    bic.w r7, r15
    call #guarda
; bloque 0183
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0184
    mov #0x0800, r5
    mov #zona+35, r14
    mov #0x0, r2
    addc.b @r14+, r5
    call #guarda
; bloque 0185
    mov #zona+128, r6
    mov #0x3656, r14
    mov #0x101, r2
    sub.b r14, -76(r6)
    call #guarda
; bloque 0186
    mov #zona+128, r9
    mov #0x103, r2
    bic.w r9, 98(r9)
    call #guarda
; bloque 0187
    mov #0x97B9, r6
    mov #zona+246, r7
    mov #0x4, r2
    cmp.w @r7+, r6
    call #guarda
; bloque 0188
    mov #0, r15
    mov #tabla, r9
    mov #0x3, r2
    call @r9
    call #guarda
; bloque 0189
    mov #0xDA7C, r9
    mov #0x5, r2
    bic.w #8, r9
    call #guarda
; bloque 018a
    mov #zona+128, r14
    mov #0x104, r2
    push.w 78(r14)
    pop r15
    call #guarda
; bloque 018b
    mov #0, r15
    mov #tabla, r11
    mov #0x107, r2
    call @r11
    call #guarda
; bloque 018c
    mov #0, r15
    mov #0x4, r2
    call tabla
    call #guarda
; bloque 018d
    mov #0x0415, r7
    mov #0x6, r2
    push.b r7
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 018e
    mov #0x3288, r4
    mov #0x5, r2
    dadd.w #0, r4
    call #guarda
; bloque 018f
    mov #0xFFFF, r10
    mov #zona+42, r15
    mov #0x100, r2
    cmp.b @r15, r10
    call #guarda
; bloque 0190
    mov #0x7FFF, r10
    mov #0x0200, r12
    mov #0x101, r2
    mov.w r12, r10
    call #guarda
; bloque 0191
    mov #0x89CD, r13
    mov #0x537C, r5
    mov #0x4, r2
    addc.b r5, r13
    call #guarda
; bloque 0192
    mov #0x8263, r7
    mov #0x4, r2
    and.b r7, zona+10
    call #guarda
; bloque 0193
    mov #0, r15
    mov #0x3, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0194
    mov #0x0060, r15
    mov #0x0, r2
    dadd.b #1, r15
    call #guarda
; bloque 0195
    mov #zona+100, r9
    mov #0x105, r2
    swpb @r9+
    call #guarda
; bloque 0196
    mov #0, r15
    mov #0x7, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0197
    mov #0x0075, r8
    mov #0x3, r2
    dadd.b #8, r8
    call #guarda
; bloque 0198
    mov #0x5, r2
    bis.w #0x0, &zona+198
    call #guarda
; bloque 0199
    mov #zona+128, r7
    mov #0x5, r2
    bic.b #0x0, 29(r7)
    call #guarda
; bloque 019a
    mov #0xC2A2, r9
    mov #0x3, r2
    bit.b r9, r9
    call #guarda
; bloque 019b
    mov #0x0089, r15
    mov #0x0, r2
    dadd.b #0x34, r15
    call #guarda
; bloque 019c
    mov #0x4000, r4
    mov #0x103, r2
    bit.w &zona+194, r4
    call #guarda
; bloque 019d
    mov #0x105, r2
    sub.w #0x8, zona+236
    call #guarda
; bloque 019e
    mov #0, r15
    mov #0x4, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 019f
    mov #0x1, r2
    rra.w zona+76
    call #guarda
; bloque 01a0
    mov #0x0000, r9
    mov #0x4, r2
    push.w r9
    pop r15
    call #guarda
; bloque 01a1
    mov #zona+222, r7
    mov #0x106, r2
    sxt @r7
    call #guarda
; bloque 01a2
    mov #0, r15
    mov #0x107, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a3
    mov #0x0010, r7
    mov #zona+189, r9
    mov #0x103, r2
    bit.b @r9, r7
    call #guarda
; bloque 01a4
    mov #0, r15
    mov #0x5, r2
    call tabla
    call #guarda
; bloque 01a5
    mov #zona+128, r5
    mov #0x1, r2
    rrc.w 2(r5)
    call #guarda
; bloque 01a6
    mov #0x100, r2
    swpb zona+100
    call #guarda
; bloque 01a7
    mov #0x102, r2
    push.b zona+89
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 01a8
    mov #0, r15
    mov #0x101, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a9
    mov #zona+128, r15
    mov #zona+242, r7
    mov #0x104, r2
    bis.w @r7+, 70(r15)
    call #guarda
; bloque 01aa
    mov #0x0, r2
    sxt zona+136
    call #guarda
; bloque 01ab
    mov #0xFFFF, r8
    mov #0x105, r2
    bic.b #0x98, r8
    call #guarda
; bloque 01ac
    mov #0x4000, r14
    mov #0x103, r2
    mov.w zona+210, r14
    call #guarda
; bloque 01ad
    mov #0x104, r2
    push.w &zona+192
    pop r15
    call #guarda
; bloque 01ae
    mov #zona+128, r12
    mov #0x102, r2
    xor.w -52(r12), &zona+176
    call #guarda
; bloque 01af
    mov #0x72E0, r15
    mov #0x5, r2
    sxt r15
    call #guarda
; bloque 01b0
    mov #0xCD81, r13
    mov #zona+166, r4
    mov #0x2, r2
    sub.w @r4, r13
    call #guarda
; bloque 01b1
    mov #0, r15
    mov #0x107, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 01b2
    mov #zona+223, r14
    mov #0x106, r2
    addc.b @r14+, zona+137
    call #guarda
; bloque 01b3
    mov #0xA442, r13
    mov #0x8F8C, r12
    mov #0x3, r2
    add.w r12, r13
    call #guarda
; bloque 01b4
    mov #0xA701, r4
    mov #0x2, r2
    bic.b zona+95, r4
    call #guarda
; bloque 01b5
    mov #0x6, r2
    rra.w zona+230
    call #guarda
; bloque 01b6
    mov #zona+128, r5
    mov #0x1, r2
    swpb -110(r5)
    call #guarda
; bloque 01b7
    mov #0xD957, r14
    mov #0x106, r2
    add.b zona+153, r14
    call #guarda
; bloque 01b8
    mov #0x7FFE, r14
    mov #0x4F5B, r4
    mov #0x0, r2
    xor.w r4, r14
    call #guarda
; bloque 01b9
    mov #0xBD64, r8
    mov #0x6, r2
    sxt r8
    call #guarda
; bloque 01ba
    mov #0x101, r2
    swpb zona+166
    call #guarda
; bloque 01bb
    mov #0x103A, r7
    mov #zona+46, r6
    mov #0x5, r2
    bis.w @r6+, r7
    call #guarda
; bloque 01bc
    mov #0x0021, r12
    mov #0x3, r2
    dadd.b #8, r12
    call #guarda
; bloque 01bd
    mov #0x102, r2
    bic.w #1, &zona+166
    call #guarda
; bloque 01be
    mov #0, r15
    mov #0x4, r2
    call &tabla
    call #guarda
; bloque 01bf
    mov #0x0004, r15
    mov #0x105, r2
    subc.b &zona+189, r15
    call #guarda
; bloque 01c0
    mov #0x3, r2
    bic.b &zona+249, zona+94
    call #guarda
; bloque 01c1
    mov #0, r15
    mov #0x6, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
    mov &sp_guardado, r1
    pop r10
    pop r9
    pop r8
    pop r7
    pop r6
    pop r5
    pop r4
fin:
    ret
guarda:
    push r15
    mov &resptr, r15
    mov r2, 24(r15)
    mov r4, 0(r15)
    mov r5, 2(r15)
    mov r6, 4(r15)
    mov r7, 6(r15)
    mov r8, 8(r15)
    mov r9, 10(r15)
    mov r10, 12(r15)
    mov r11, 14(r15)
    mov r12, 16(r15)
    mov r13, 18(r15)
    mov r14, 20(r15)
    mov @r1+, 22(r15)
    add #26, &resptr
    ret
sub:
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
    .word zona
tabla:
    .word sub
    .word 0
tabla_a:
    .long sub
    .long 0
    .balign 4
    .global res
res:
    .space 11700
    .global resptr
resptr:
    .long 0
sp_guardado:
    .long 0
    .global n_bloques
n_bloques:
    .word 450
