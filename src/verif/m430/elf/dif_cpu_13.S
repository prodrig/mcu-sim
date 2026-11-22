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
    mov #0x5FEF, r7
    mov #zona+32, r5
    mov #0x106, r2
    xor.w @r5+, r7
    call #guarda
; bloque 0001
    mov #zona+212, r6
    mov #0x101, r2
    sxt @r6
    call #guarda
; bloque 0002
    mov #0x8965, r11
    mov #0x5159, r10
    mov #0x0, r2
    dadd.w r10, r11
    call #guarda
; bloque 0003
    mov #0x107, r2
    and.w zona+44, zona+136
    call #guarda
; bloque 0004
    mov #zona+140, r7
    mov #0x100, r2
    sub.b @r7, &zona+125
    call #guarda
; bloque 0005
    mov #0x161F, r15
    mov #zona+128, r6
    mov #0x105, r2
    xor.b -71(r6), r15
    call #guarda
; bloque 0006
    mov #0xBFE0, r15
    mov #0x0, r2
    swpb r15
    call #guarda
; bloque 0007
    mov #0x0000, r13
    mov #0x104, r2
    bit.w r13, &zona+178
    call #guarda
; bloque 0008
    mov #0x0000, r6
    mov #0x100, r2
    sxt r6
    call #guarda
; bloque 0009
    mov #0x3E37, r6
    mov #0x103, r2
    cmp.w &zona+104, r6
    call #guarda
; bloque 000a
    mov #zona+128, r6
    mov #0xF2CD, r11
    mov #0x103, r2
    cmp.w r11, -44(r6)
    call #guarda
; bloque 000b
    mov #zona+108, r14
    mov #0x106, r2
    rra.w @r14
    call #guarda
; bloque 000c
    mov #zona+52, r14
    mov #0x107, r2
    sxt @r14+
    call #guarda
; bloque 000d
    mov #0x103, r2
    sxt zona+198
    call #guarda
; bloque 000e
    mov #0xFFFF, r4
    mov #zona+210, r7
    mov #0x107, r2
    bic.b @r7, r4
    call #guarda
; bloque 000f
    mov #0x102, r2
    cmp.b zona+149, &zona+224
    call #guarda
; bloque 0010
    mov #zona+128, r6
    mov #0x0, r2
    swpb 60(r6)
    call #guarda
; bloque 0011
    mov #zona+128, r7
    mov #0x0, r2
    cmp.w #0, 76(r7)
    call #guarda
; bloque 0012
    mov #0, r15
    mov #0x7, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0013
    mov #zona+128, r10
    mov #0x105, r2
    bis.w &zona+76, 8(r10)
    call #guarda
; bloque 0014
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0015
    mov #0, r15
    mov #0x1, r2
    call tabla
    call #guarda
; bloque 0016
    mov #zona+128, r10
    mov #0x8000, r11
    mov #0x106, r2
    mov.w r11, -26(r10)
    call #guarda
; bloque 0017
    mov #zona+128, r6
    mov #zona+214, r12
    mov #0x3, r2
    cmp.w @r12+, -22(r6)
    call #guarda
; bloque 0018
    mov #0x0078, r4
    mov #0x7, r2
    dadd.b #1, r4
    call #guarda
; bloque 0019
    mov #zona+244, r12
    mov #0x5, r2
    bit.w @r12+, zona+6
    call #guarda
; bloque 001a
    mov #0x0000, r11
    mov #0x1, r2
    mov.b #0x0, r11
    call #guarda
; bloque 001b
    mov #0, r15
    mov #0x100, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 001c
    mov #0x4442, r11
    mov #0x107, r2
    mov.w #-1, r11
    call #guarda
; bloque 001d
    mov #zona+128, r4
    mov #0x102, r2
    rra.w -102(r4)
    call #guarda
; bloque 001e
    mov #0x101, r2
    cmp.b zona+105, &zona+131
    call #guarda
; bloque 001f
    mov #0x4, r2
    add.w #0, zona+148
    call #guarda
; bloque 0020
    mov #zona+128, r15
    mov #0x7, r2
    and.b 16(r15), zona+123
    call #guarda
; bloque 0021
    mov #0, r15
    mov #0x105, r2
    call tabla
    call #guarda
; bloque 0022
    mov #0, r15
    mov #0x0, r2
    call #sub
    call #guarda
; bloque 0023
    mov #0x0020, r4
    mov #zona+168, r11
    mov #0x100, r2
    mov.w @r11, r4
    call #guarda
; bloque 0024
    mov #0x102, r2
    bis.w &zona+200, &zona+198
    call #guarda
; bloque 0025
    mov #0x5, r2
    sub.b &zona+72, zona+232
    call #guarda
; bloque 0026
    mov #0x8000, r13
    mov #zona+126, r11
    mov #0x2, r2
    add.b @r11, r13
    call #guarda
; bloque 0027
    mov #zona+128, r9
    mov #0x105, r2
    bit.w zona+30, 86(r9)
    call #guarda
; bloque 0028
    mov #0x009F, r10
    mov #0x106, r2
    swpb r10
    call #guarda
; bloque 0029
    mov #zona+128, r7
    mov #0x107, r2
    cmp.b #0, 63(r7)
    call #guarda
; bloque 002a
    mov #0x0010, r12
    mov #0x6, r2
    bis.b r12, zona+105
    call #guarda
; bloque 002b
    mov #0x0062, r7
    mov #0x6, r2
    dadd.b #8, r7
    call #guarda
; bloque 002c
    mov #zona+142, r15
    mov #0x107, r2
    bic.b @r15, &zona+11
    call #guarda
; bloque 002d
    mov #0, r15
    mov #0x102, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 002e
    mov #0x6, r2
    push.w &zona+192
    pop r15
    call #guarda
; bloque 002f
    mov #0x0000, r4
    mov #0x101, r2
    sxt r4
    call #guarda
; bloque 0030
    mov #0, r15
    mov #0x106, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0031
    mov #0x5AB6, r15
    mov #0x7, r2
    rrc.w r15
    call #guarda
; bloque 0032
    mov #0, r15
    mov #0x107, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0033
    mov #0x08D5, r6
    mov #0x107, r2
    sxt r6
    call #guarda
; bloque 0034
    mov #0x2111, r11
    mov #zona+184, r14
    mov #0x5, r2
    bit.w @r14+, r11
    call #guarda
; bloque 0035
    mov #zona+205, r9
    mov #0x101, r2
    mov.b @r9+, zona+173
    call #guarda
; bloque 0036
    mov #0x105, r2
    subc.w &zona+206, zona+130
    call #guarda
; bloque 0037
    mov #0x4, r2
    mov.w zona+182, zona+60
    call #guarda
; bloque 0038
    mov #0x104, r2
    subc.b &zona+0, &zona+229
    call #guarda
; bloque 0039
    mov #0, r15
    mov #0x103, r2
    call #sub
    call #guarda
; bloque 003a
    mov #0x5A17, r13
    mov #0x4, r2
    add.w #-1, r13
    call #guarda
; bloque 003b
    mov #0x7, r2
    and.w #2, &zona+188
    call #guarda
; bloque 003c
    mov #0xFFFF, r13
    mov #zona+99, r14
    mov #0x3, r2
    subc.b @r14, r13
    call #guarda
; bloque 003d
    mov #0x9A99, r13
    mov #0x103, r2
    push.w r13
    pop r15
    call #guarda
; bloque 003e
    mov #0x100, r2
    push.w #0x6B95
    pop r15
    call #guarda
; bloque 003f
    mov #zona+8, r8
    mov #0x4, r2
    bit.w @r8, &zona+84
    call #guarda
; bloque 0040
    mov #0x6348, r9
    mov #0x3015, r11
    mov #0x6, r2
    dadd.w r11, r9
    call #guarda
; bloque 0041
    mov #zona+128, r4
    mov #0x4, r2
    bis.b &zona+188, -126(r4)
    call #guarda
; bloque 0042
    mov #0xE286, r10
    mov #zona+208, r8
    mov #0x103, r2
    and.w @r8, r10
    call #guarda
; bloque 0043
    mov #0x3153, r5
    mov #0x1563, r6
    mov #0x5, r2
    dadd.w r6, r5
    call #guarda
; bloque 0044
    mov #0x107, r2
    push.b &zona+99
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0045
    mov #0x5, r2
    and.b zona+136, &zona+17
    call #guarda
; bloque 0046
    mov #zona+148, r10
    mov #0x6, r2
    swpb @r10+
    call #guarda
; bloque 0047
    mov #0, r15
    mov #tabla-4, r10
    mov #0x4, r2
    call 4(r10)
    call #guarda
; bloque 0048
    mov #zona+128, r7
    mov #0x7, r2
    xor.b 5(r7), zona+172
    call #guarda
; bloque 0049
    mov #zona+128, r6
    mov #0x0, r2
    subc.b zona+95, 119(r6)
    call #guarda
; bloque 004a
    mov #0x8483, r6
    mov #0x104, r2
    rrc.b r6
    call #guarda
; bloque 004b
    mov #0x0004, r5
    mov #0x4, r2
    dadd.b #0x23, r5
    call #guarda
; bloque 004c
    mov #zona+188, r6
    mov #0x102, r2
    subc.w @r6+, zona+194
    call #guarda
; bloque 004d
    mov #0x0094, r5
    mov #0x2, r2
    dadd.b #0x43, r5
    call #guarda
; bloque 004e
    mov #zona+128, r7
    mov #zona+154, r9
    mov #0x104, r2
    sub.b @r9, -1(r7)
    call #guarda
; bloque 004f
    mov #0xD924, r5
    mov #0x100, r2
    addc.b &zona+209, r5
    call #guarda
; bloque 0050
    mov #0x051A, r12
    mov #0x2, r2
    subc.b r12, zona+104
    call #guarda
; bloque 0051
    mov #0x3318, r15
    mov #0x7, r2
    dadd.w #0x7027, r15
    call #guarda
; bloque 0052
    mov #0, r15
    mov #sub, r4
    mov #0x6, r2
    call r4
    call #guarda
; bloque 0053
    mov #0x8000, r8
    mov #0x2, r2
    push.b r8
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0054
    mov #0x0DEC, r14
    mov #0x0020, r4
    mov #0x101, r2
    addc.w r4, r14
    call #guarda
; bloque 0055
    mov #zona+128, r10
    mov #0x2, r2
    bic.w #1, -110(r10)
    call #guarda
; bloque 0056
    mov #zona+128, r5
    mov #0x105, r2
    push.b -117(r5)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0057
    mov #0x105, r2
    bit.b #0x41, zona+238
    call #guarda
; bloque 0058
    mov #0xFFFF, r5
    mov #zona+119, r8
    mov #0x4, r2
    bit.b @r8, r5
    call #guarda
; bloque 0059
    mov #0xE640, r15
    mov #0x6, r2
    cmp.w #2, r15
    call #guarda
; bloque 005a
    mov #zona+38, r8
    mov #0x106, r2
    sxt @r8+
    call #guarda
; bloque 005b
    mov #0xF3C4, r13
    mov #0xFFFF, r15
    mov #0x5, r2
    addc.w r15, r13
    call #guarda
; bloque 005c
    mov #0x4000, r10
    mov #0x100, r2
    rra.w r10
    call #guarda
; bloque 005d
    mov #0x8B83, r14
    mov #0x0, r2
    subc.b r14, zona+247
    call #guarda
; bloque 005e
    mov #0x0040, r14
    mov #0x7, r2
    cmp.w #2, r14
    call #guarda
; bloque 005f
    mov #0x100, r2
    bit.b #0, zona+144
    call #guarda
; bloque 0060
    mov #0, r15
    mov #0x102, r2
    call #sub
    call #guarda
; bloque 0061
    mov #0x0001, r12
    mov #0x4, r2
    xor.b r12, zona+109
    call #guarda
; bloque 0062
    mov #0x0010, r13
    mov #0x104, r2
    bic.b #8, r13
    call #guarda
; bloque 0063
    mov #zona+128, r12
    mov #0x6, r2
    rra.b 21(r12)
    call #guarda
; bloque 0064
    mov #zona+128, r4
    mov #zona+172, r14
    mov #0x5, r2
    cmp.w @r14+, -66(r4)
    call #guarda
; bloque 0065
    mov #0xFFFF, r7
    mov #0xFFFF, r11
    mov #0x0, r2
    mov.b r11, r7
    call #guarda
; bloque 0066
    mov #zona+217, r12
    mov #0x107, r2
    push.b @r12+
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0067
    mov #0, r15
    mov #0x101, r2
    call tabla
    call #guarda
; bloque 0068
    mov #0, r15
    mov #0x6, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0069
    mov #0x0, r2
    sub.w #1, &zona+204
    call #guarda
; bloque 006a
    mov #0xEECF, r9
    mov #0x102, r2
    xor.w zona+246, r9
    call #guarda
; bloque 006b
    mov #zona+128, r8
    mov #0x6, r2
    bit.b zona+130, -69(r8)
    call #guarda
; bloque 006c
    mov #zona+128, r7
    mov #0x3AD4, r6
    mov #0x101, r2
    bis.w r6, 2(r7)
    call #guarda
; bloque 006d
    mov #0x6, r2
    rrc.b &zona+70
    call #guarda
; bloque 006e
    mov #0x0000, r8
    mov #0x104, r2
    addc.w #8, r8
    call #guarda
; bloque 006f
    mov #0x1706, r6
    mov #0x2, r2
    dadd.w #2, r6
    call #guarda
; bloque 0070
    mov #0x7FFE, r6
    mov #0xE809, r9
    mov #0x100, r2
    bic.w r9, r6
    call #guarda
; bloque 0071
    mov #zona+128, r12
    mov #0x7, r2
    addc.w #0x22A0, 16(r12)
    call #guarda
; bloque 0072
    mov #0, r15
    mov #tabla-4, r8
    mov #0x104, r2
    call 4(r8)
    call #guarda
; bloque 0073
    mov #0xD2A5, r11
    mov #0x105, r2
    sxt r11
    call #guarda
; bloque 0074
    mov #zona+128, r14
    mov #0x100, r2
    and.w zona+154, 38(r14)
    call #guarda
; bloque 0075
    mov #0x0000, r13
    mov #0x104, r2
    rrc.w r13
    call #guarda
; bloque 0076
    mov #0xE72C, r10
    mov #0x102, r2
    rrc.w r10
    call #guarda
; bloque 0077
    mov #0x2157, r5
    mov #0x4, r2
    dadd.w #4, r5
    call #guarda
; bloque 0078
    mov #zona+128, r4
    mov #0x1077, r7
    mov #0x105, r2
    cmp.w r7, 58(r4)
    call #guarda
; bloque 0079
    mov #0x103, r2
    xor.w &zona+122, zona+222
    call #guarda
; bloque 007a
    mov #0x0000, r14
    mov #0x2, r2
    rrc.w r14
    call #guarda
; bloque 007b
    mov #0x104, r2
    add.w #8, zona+108
    call #guarda
; bloque 007c
    mov #0x3, r2
    push.b zona+133
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 007d
    mov #0, r15
    mov #0x2, r2
    call tabla
    call #guarda
; bloque 007e
    mov #zona+122, r4
    mov #0x7, r2
    rra.w @r4
    call #guarda
; bloque 007f
    mov #0xE1CE, r14
    mov #0x6, r2
    bic.w #0x1F0A, r14
    call #guarda
; bloque 0080
    mov #zona+128, r5
    mov #0x3, r2
    bis.b zona+72, 83(r5)
    call #guarda
; bloque 0081
    mov #zona+186, r12
    mov #0x107, r2
    sxt @r12+
    call #guarda
; bloque 0082
    mov #0, r15
    mov #sub, r9
    mov #0x100, r2
    call r9
    call #guarda
; bloque 0083
    mov #zona+128, r7
    mov #0x5, r2
    add.b 54(r7), zona+103
    call #guarda
; bloque 0084
    mov #0x0F37, r8
    mov #0x2, r2
    xor.b r8, &zona+54
    call #guarda
; bloque 0085
    mov #zona+34, r7
    mov #0x106, r2
    xor.w @r7, &zona+140
    call #guarda
; bloque 0086
    mov #0x103, r2
    swpb &zona+104
    call #guarda
; bloque 0087
    mov #zona+128, r6
    mov #0x6, r2
    xor.b #-1, -94(r6)
    call #guarda
; bloque 0088
    mov #0x6, r2
    rrc.w &zona+138
    call #guarda
; bloque 0089
    mov #zona+128, r5
    mov #0xFFFF, r7
    mov #0x107, r2
    bic.w r7, -102(r5)
    call #guarda
; bloque 008a
    mov #zona+236, r9
    mov #0x105, r2
    swpb @r9+
    call #guarda
; bloque 008b
    mov #0x0000, r4
    mov #0x106, r2
    swpb r4
    call #guarda
; bloque 008c
    mov #0x0000, r8
    mov #0x102, r2
    push.w r8
    pop r15
    call #guarda
; bloque 008d
    mov #0x0000, r7
    mov #0x0090, r11
    mov #0x3, r2
    dadd.b r11, r7
    call #guarda
; bloque 008e
    mov #0xAE97, r6
    mov #zona+44, r13
    mov #0x106, r2
    bic.w @r13, r6
    call #guarda
; bloque 008f
    mov #0xFFFF, r11
    mov #zona+128, r15
    mov #0x101, r2
    bis.b 104(r15), r11
    call #guarda
; bloque 0090
    mov #0x101, r2
    sxt &zona+140
    call #guarda
; bloque 0091
    mov #zona+128, r13
    mov #0x102, r2
    sub.b #0xC8, -100(r13)
    call #guarda
; bloque 0092
    mov #zona+128, r10
    mov #0x107, r2
    rrc.b 8(r10)
    call #guarda
; bloque 0093
    mov #0xFA09, r7
    mov #zona+172, r6
    mov #0x101, r2
    subc.b @r6, r7
    call #guarda
; bloque 0094
    mov #0x100, r2
    and.b &zona+73, &zona+178
    call #guarda
; bloque 0095
    mov #0x2, r2
    rrc.b &zona+0
    call #guarda
; bloque 0096
    mov #zona+128, r8
    mov #0x1, r2
    swpb 72(r8)
    call #guarda
; bloque 0097
    mov #0x0082, r5
    mov #0x7, r2
    dadd.b #0x30, r5
    call #guarda
; bloque 0098
    mov #0x0010, r7
    mov #0x102, r2
    rra.w r7
    call #guarda
; bloque 0099
    mov #0x93F2, r11
    mov #0xD5E8, r9
    mov #0x3, r2
    cmp.w r9, r11
    call #guarda
; bloque 009a
    mov #0xFFFF, r9
    mov #0x356B, r12
    mov #0x106, r2
    cmp.w r12, r9
    call #guarda
; bloque 009b
    mov #0xFFFF, r9
    mov #0x5, r2
    push.w r9
    pop r15
    call #guarda
; bloque 009c
    mov #zona+128, r8
    mov #0x6, r2
    bit.b #1, -45(r8)
    call #guarda
; bloque 009d
    mov #0x0000, r11
    mov #0x102, r2
    rra.w r11
    call #guarda
; bloque 009e
    mov #0xFB12, r13
    mov #0x102, r2
    rra.b r13
    call #guarda
; bloque 009f
    mov #zona+128, r9
    mov #0x102, r2
    add.b &zona+191, 89(r9)
    call #guarda
; bloque 00a0
    mov #0xFFFF, r12
    mov #zona+128, r11
    mov #0x101, r2
    mov.w 34(r11), r12
    call #guarda
; bloque 00a1
    mov #0x102, r2
    rrc.w zona+40
    call #guarda
; bloque 00a2
    mov #0, r15
    mov #tabla, r8
    mov #0x105, r2
    call @r8
    call #guarda
; bloque 00a3
    mov #zona+182, r5
    mov #0x101, r2
    rrc.w @r5
    call #guarda
; bloque 00a4
    mov #0xFFFF, r10
    mov #0x103, r2
    swpb r10
    call #guarda
; bloque 00a5
    mov #zona+128, r12
    mov #0x1, r2
    bit.b &zona+230, -122(r12)
    call #guarda
; bloque 00a6
    mov #0, r15
    mov #0x105, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00a7
    mov #0x100, r2
    mov.b &zona+8, &zona+216
    call #guarda
; bloque 00a8
    mov #zona+221, r14
    mov #0x1, r2
    rra.b @r14+
    call #guarda
; bloque 00a9
    mov #zona+128, r15
    mov #0x4, r2
    sxt 116(r15)
    call #guarda
; bloque 00aa
    mov #0x107, r2
    bit.w zona+44, &zona+196
    call #guarda
; bloque 00ab
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ac
    mov #zona+128, r15
    mov #zona+128, r13
    mov #0x5, r2
    bis.w -28(r13), 116(r15)
    call #guarda
; bloque 00ad
    mov #0x106, r2
    rra.w zona+182
    call #guarda
; bloque 00ae
    mov #0x1, r2
    bit.b zona+229, zona+231
    call #guarda
; bloque 00af
    mov #0x0400, r14
    mov #0x101, r2
    bic.w r14, zona+150
    call #guarda
; bloque 00b0
    mov #zona+128, r6
    mov #0x2, r2
    push.w -38(r6)
    pop r15
    call #guarda
; bloque 00b1
    mov #zona+40, r7
    mov #0x2, r2
    rrc.b @r7+
    call #guarda
; bloque 00b2
    mov #0, r15
    mov #0x1, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00b3
    mov #zona+128, r5
    mov #0x2, r2
    subc.w -108(r5), &zona+210
    call #guarda
; bloque 00b4
    mov #zona+128, r8
    mov #0x103, r2
    rra.b -47(r8)
    call #guarda
; bloque 00b5
    mov #0x410F, r6
    mov #0xAA54, r15
    mov #0x107, r2
    cmp.w r15, r6
    call #guarda
; bloque 00b6
    mov #0, r15
    mov #tabla, r6
    mov #0x4, r2
    call @r6+
    call #guarda
; bloque 00b7
    mov #zona+89, r10
    mov #0x104, r2
    rrc.b @r10
    call #guarda
; bloque 00b8
    mov #0x3C33, r4
    mov #zona+150, r5
    mov #0x102, r2
    bit.b @r5+, r4
    call #guarda
; bloque 00b9
    mov #0x0078, r12
    mov #0x6, r2
    dadd.b #1, r12
    call #guarda
; bloque 00ba
    mov #0x1C06, r15
    mov #0x103, r2
    rrc.w r15
    call #guarda
; bloque 00bb
    mov #zona+128, r12
    mov #0x1, r2
    and.w &zona+16, 60(r12)
    call #guarda
; bloque 00bc
    mov #0x2, r2
    push.b &zona+109
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00bd
    mov #0x0400, r9
    mov #0x100, r2
    addc.b &zona+59, r9
    call #guarda
; bloque 00be
    mov #0x4, r2
    push.w &zona+52
    pop r15
    call #guarda
; bloque 00bf
    mov #zona+128, r7
    mov #0x0, r2
    mov.w zona+180, 66(r7)
    call #guarda
; bloque 00c0
    mov #zona+128, r9
    mov #0x3, r2
    bis.b -17(r9), zona+42
    call #guarda
; bloque 00c1
    mov #0, r15
    mov #0x100, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c2
    mov #zona+128, r10
    mov #0x1BA6, r7
    mov #0x6, r2
    bit.b r7, -46(r10)
    call #guarda
; bloque 00c3
    mov #zona+217, r7
    mov #0x104, r2
    rrc.b @r7+
    call #guarda
; bloque 00c4
    mov #0x100, r2
    swpb zona+166
    call #guarda
; bloque 00c5
    mov #0x6, r2
    bic.b &zona+183, zona+98
    call #guarda
; bloque 00c6
    mov #0, r15
    mov #0x100, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c7
    mov #0x0000, r13
    mov #0x102, r2
    rrc.w r13
    call #guarda
; bloque 00c8
    mov #zona+128, r8
    mov #0x107, r2
    addc.w -30(r8), zona+120
    call #guarda
; bloque 00c9
    mov #0xE3E4, r15
    mov #zona+59, r12
    mov #0x7, r2
    bic.b @r12+, r15
    call #guarda
; bloque 00ca
    mov #0x104, r2
    sxt zona+40
    call #guarda
; bloque 00cb
    mov #0x4296, r9
    mov #0x101, r2
    bis.b r9, &zona+13
    call #guarda
; bloque 00cc
    mov #zona+242, r8
    mov #0x3, r2
    cmp.w @r8, zona+156
    call #guarda
; bloque 00cd
    mov #zona+128, r6
    mov #zona+128, r11
    mov #0x7, r2
    add.w 26(r11), 86(r6)
    call #guarda
; bloque 00ce
    mov #0xD0AE, r13
    mov #0x105, r2
    addc.b r13, &zona+164
    call #guarda
; bloque 00cf
    mov #zona+128, r5
    mov #0x1, r2
    cmp.b #0x7F, -83(r5)
    call #guarda
; bloque 00d0
    mov #0, r15
    mov #0x2, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d1
    mov #0x3F99, r7
    mov #0x3, r2
    mov.b &zona+35, r7
    call #guarda
; bloque 00d2
    mov #0, r15
    mov #0x2, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d3
    mov #0x107, r2
    bit.b &zona+236, &zona+230
    call #guarda
; bloque 00d4
    mov #0, r15
    mov #0x107, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d5
    mov #0xD8BC, r4
    mov #zona+128, r12
    mov #0x105, r2
    bit.w 116(r12), r4
    call #guarda
; bloque 00d6
    mov #zona+128, r8
    mov #0x104, r2
    rrc.b -73(r8)
    call #guarda
; bloque 00d7
    mov #0xD5BA, r4
    mov #zona+128, r6
    mov #0x100, r2
    sub.w -4(r6), r4
    call #guarda
; bloque 00d8
    mov #0x107, r2
    sxt zona+156
    call #guarda
; bloque 00d9
    mov #zona+128, r9
    mov #zona+250, r15
    mov #0x3, r2
    bic.w @r15, 106(r9)
    call #guarda
; bloque 00da
    mov #0x103, r2
    subc.w &zona+92, zona+242
    call #guarda
; bloque 00db
    mov #0x676A, r6
    mov #0x1, r2
    mov.b zona+12, r6
    call #guarda
; bloque 00dc
    mov #0, r15
    mov #0x6, r2
    call tabla
    call #guarda
; bloque 00dd
    mov #0x2, r2
    rra.w zona+120
    call #guarda
; bloque 00de
    mov #0x1, r2
    swpb &zona+236
    call #guarda
; bloque 00df
    mov #0x9CD5, r7
    mov #0x106, r2
    rra.w r7
    call #guarda
; bloque 00e0
    mov #0xCA2E, r9
    mov #0x102, r2
    bis.w zona+250, r9
    call #guarda
; bloque 00e1
    mov #0x043C, r6
    mov #0x4, r2
    sub.b r6, zona+189
    call #guarda
; bloque 00e2
    mov #0x0000, r10
    mov #0x2, r2
    sub.b r10, zona+22
    call #guarda
; bloque 00e3
    mov #0x7FFE, r11
    mov #0x102, r2
    xor.b &zona+135, r11
    call #guarda
; bloque 00e4
    mov #0x1000, r5
    mov #0x100, r2
    bic.w r5, &zona+200
    call #guarda
; bloque 00e5
    mov #0x43B1, r14
    mov #0x0, r2
    bit.w #0, r14
    call #guarda
; bloque 00e6
    mov #zona+128, r13
    mov #0x0000, r14
    mov #0x7, r2
    bis.b r14, -116(r13)
    call #guarda
; bloque 00e7
    mov #0x2, r2
    push.w #0xE55
    pop r15
    call #guarda
; bloque 00e8
    mov #0x104, r2
    sxt &zona+52
    call #guarda
; bloque 00e9
    mov #0xC5F0, r13
    mov #0x0000, r10
    mov #0x104, r2
    bic.w r10, r13
    call #guarda
; bloque 00ea
    mov #zona+128, r6
    mov #0x107, r2
    and.w 60(r6), zona+222
    call #guarda
; bloque 00eb
    mov #0x2, r2
    subc.w #0, zona+114
    call #guarda
; bloque 00ec
    mov #zona+128, r12
    mov #0x0BD5, r11
    mov #0x100, r2
    bic.w r11, 96(r12)
    call #guarda
; bloque 00ed
    mov #0x0062, r10
    mov #0x3, r2
    dadd.b #1, r10
    call #guarda
; bloque 00ee
    mov #0x103, r2
    xor.w &zona+40, &zona+2
    call #guarda
; bloque 00ef
    mov #0x3447, r13
    mov #0x0, r2
    subc.b r13, zona+131
    call #guarda
; bloque 00f0
    mov #0xC6B3, r4
    mov #0x101, r2
    cmp.b #8, r4
    call #guarda
; bloque 00f1
    mov #zona+128, r9
    mov #0x100, r2
    bis.w &zona+228, -12(r9)
    call #guarda
; bloque 00f2
    mov #0x0000, r14
    mov #zona+184, r11
    mov #0x6, r2
    addc.w @r11, r14
    call #guarda
; bloque 00f3
    mov #0x103, r2
    rrc.b zona+110
    call #guarda
; bloque 00f4
    mov #0x0EF6, r11
    mov #zona+128, r9
    mov #0x104, r2
    subc.w -96(r9), r11
    call #guarda
; bloque 00f5
    mov #0, r15
    mov #0x103, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f6
    mov #0x6095, r6
    mov #0x102, r2
    addc.b &zona+6, r6
    call #guarda
; bloque 00f7
    mov #0x0000, r13
    mov #0x102, r2
    add.w zona+218, r13
    call #guarda
; bloque 00f8
    mov #0x0016, r4
    mov #0x5, r2
    dadd.b #8, r4
    call #guarda
; bloque 00f9
    mov #0x0000, r5
    mov #0x104, r2
    addc.b zona+108, r5
    call #guarda
; bloque 00fa
    mov #0, r15
    mov #tabla, r12
    mov #0x5, r2
    call @r12+
    call #guarda
; bloque 00fb
    mov #0x0025, r5
    mov #0x0050, r13
    mov #0x6, r2
    dadd.b r13, r5
    call #guarda
; bloque 00fc
    mov #0x104, r2
    sxt &zona+174
    call #guarda
; bloque 00fd
    mov #zona+128, r11
    mov #0x0000, r9
    mov #0x101, r2
    mov.b r9, -123(r11)
    call #guarda
; bloque 00fe
    mov #zona+10, r8
    mov #0x5, r2
    addc.w @r8+, zona+22
    call #guarda
; bloque 00ff
    mov #0x7FFE, r13
    mov #0x6, r2
    swpb r13
    call #guarda
; bloque 0100
    mov #0, r15
    mov #0x106, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0101
    mov #0xD7C2, r12
    mov #zona+128, r7
    mov #0x3, r2
    bic.b 24(r7), r12
    call #guarda
; bloque 0102
    mov #0xA72F, r4
    mov #0x8000, r8
    mov #0x103, r2
    bic.b r8, r4
    call #guarda
; bloque 0103
    mov #zona+100, r12
    mov #0x5, r2
    rrc.w @r12
    call #guarda
; bloque 0104
    mov #0, r15
    mov #tabla, r9
    mov #0x101, r2
    call @r9+
    call #guarda
; bloque 0105
    mov #0x0024, r11
    mov #0x0080, r9
    mov #0x2, r2
    dadd.b r9, r11
    call #guarda
; bloque 0106
    mov #0x101, r2
    mov.w zona+20, zona+98
    call #guarda
; bloque 0107
    mov #0x0000, r7
    mov #zona+42, r9
    mov #0x103, r2
    subc.w @r9, r7
    call #guarda
; bloque 0108
    mov #0, r15
    mov #0x3, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0109
    mov #0x7, r2
    xor.w #8, zona+134
    call #guarda
; bloque 010a
    mov #zona+128, r12
    mov #0x107, r2
    rrc.b -43(r12)
    call #guarda
; bloque 010b
    mov #0x7FFF, r13
    mov #0x1DE1, r9
    mov #0x102, r2
    cmp.b r9, r13
    call #guarda
; bloque 010c
    mov #0x100, r2
    and.b #0xFF, zona+95
    call #guarda
; bloque 010d
    mov #zona+128, r14
    mov #0x0, r2
    rrc.b 85(r14)
    call #guarda
; bloque 010e
    mov #0x0000, r15
    mov #0x105, r2
    addc.w zona+46, r15
    call #guarda
; bloque 010f
    mov #zona+105, r7
    mov #0x0, r2
    addc.b @r7, zona+118
    call #guarda
; bloque 0110
    mov #0x1A63, r6
    mov #zona+139, r11
    mov #0x4, r2
    addc.b @r11, r6
    call #guarda
; bloque 0111
    mov #0x0000, r6
    mov #0x100, r2
    sxt r6
    call #guarda
; bloque 0112
    mov #zona+128, r8
    mov #0x107, r2
    bit.w #-1, 120(r8)
    call #guarda
; bloque 0113
    mov #zona+200, r7
    mov #0x4, r2
    and.w @r7, zona+70
    call #guarda
; bloque 0114
    mov #zona+32, r12
    mov #0x101, r2
    rrc.w @r12+
    call #guarda
; bloque 0115
    mov #zona+164, r7
    mov #0x1, r2
    rrc.w @r7
    call #guarda
; bloque 0116
    mov #0x2, r2
    cmp.w #0xFCB1, zona+6
    call #guarda
; bloque 0117
    mov #0x609C, r13
    mov #0x6, r2
    subc.b r13, zona+148
    call #guarda
; bloque 0118
    mov #zona+128, r12
    mov #0x7, r2
    xor.w #0xABC9, 28(r12)
    call #guarda
; bloque 0119
    mov #0xFFFF, r6
    mov #0x2, r2
    add.b #-1, r6
    call #guarda
; bloque 011a
    mov #zona+128, r7
    mov #0x104, r2
    swpb 48(r7)
    call #guarda
; bloque 011b
    mov #0x105, r2
    mov.w #0x0, &zona+228
    call #guarda
; bloque 011c
    mov #0x106, r2
    add.w &zona+80, &zona+94
    call #guarda
; bloque 011d
    mov #0xFFFF, r4
    mov #0x107, r2
    sub.b #0x34, r4
    call #guarda
; bloque 011e
    mov #0x7FFF, r15
    mov #0x0, r2
    bic.w &zona+44, r15
    call #guarda
; bloque 011f
    mov #zona+128, r6
    mov #0x385A, r4
    mov #0x3, r2
    cmp.w r4, -116(r6)
    call #guarda
; bloque 0120
    mov #zona+128, r4
    mov #0x5, r2
    subc.b 121(r4), zona+239
    call #guarda
; bloque 0121
    mov #zona+28, r11
    mov #0x106, r2
    addc.w @r11+, &zona+26
    call #guarda
; bloque 0122
    mov #0x104, r2
    xor.w zona+242, &zona+80
    call #guarda
; bloque 0123
    mov #0, r15
    mov #0x2, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0124
    mov #0x7, r2
    sxt zona+44
    call #guarda
; bloque 0125
    mov #0x0231, r12
    mov #0x101, r2
    sxt r12
    call #guarda
; bloque 0126
    mov #zona+128, r15
    mov #0x106, r2
    rrc.w 82(r15)
    call #guarda
; bloque 0127
    mov #zona+128, r15
    mov #0x105, r2
    rra.w -2(r15)
    call #guarda
; bloque 0128
    mov #0x0010, r13
    mov #0x104, r2
    rrc.b r13
    call #guarda
; bloque 0129
    mov #0, r15
    mov #0x106, r2
    call &tabla
    call #guarda
; bloque 012a
    mov #zona+128, r7
    mov #0x7, r2
    sxt -68(r7)
    call #guarda
; bloque 012b
    mov #zona+247, r12
    mov #0x7, r2
    rra.b @r12+
    call #guarda
; bloque 012c
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 012d
    mov #0x104, r2
    bic.b &zona+235, &zona+51
    call #guarda
; bloque 012e
    mov #0x102, r2
    sxt &zona+140
    call #guarda
; bloque 012f
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0130
    mov #0x7FFE, r15
    mov #0x6, r2
    sxt r15
    call #guarda
; bloque 0131
    mov #zona+128, r10
    mov #0x106, r2
    bis.w zona+56, -30(r10)
    call #guarda
; bloque 0132
    mov #0x42EC, r4
    mov #0x5, r2
    subc.w #8, r4
    call #guarda
; bloque 0133
    mov #0, r15
    mov #0x106, r2
    call tabla
    call #guarda
; bloque 0134
    mov #0x4372, r9
    mov #zona+128, r6
    mov #0x4, r2
    subc.b -50(r6), r9
    call #guarda
; bloque 0135
    mov #zona+128, r11
    mov #0x104, r2
    swpb 74(r11)
    call #guarda
; bloque 0136
    mov #0x103, r2
    push.w zona+224
    pop r15
    call #guarda
; bloque 0137
    mov #0x9BBD, r4
    mov #zona+124, r9
    mov #0x7, r2
    add.w @r9+, r4
    call #guarda
; bloque 0138
    mov #0, r15
    mov #0x103, r2
    call &tabla
    call #guarda
; bloque 0139
    mov #0x0080, r12
    mov #0x3, r2
    rrc.b r12
    call #guarda
; bloque 013a
    mov #0, r15
    mov #tabla, r13
    mov #0x4, r2
    call @r13
    call #guarda
; bloque 013b
    mov #0x0006, r6
    mov #0x0018, r13
    mov #0x7, r2
    dadd.b r13, r6
    call #guarda
; bloque 013c
    mov #0x0001, r9
    mov #zona+32, r4
    mov #0x1, r2
    and.w @r4+, r9
    call #guarda
; bloque 013d
    mov #0, r15
    mov #sub, r8
    mov #0x105, r2
    call r8
    call #guarda
; bloque 013e
    mov #0x3456, r14
    mov #0x3, r2
    dadd.w #0x1192, r14
    call #guarda
; bloque 013f
    mov #0x1, r2
    rra.w &zona+76
    call #guarda
; bloque 0140
    mov #0x0000, r8
    mov #0x102, r2
    push.b r8
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0141
    mov #0xBD9D, r4
    mov #0x103, r2
    mov.w r4, &zona+194
    call #guarda
; bloque 0142
    mov #0x102, r2
    and.w zona+120, &zona+14
    call #guarda
; bloque 0143
    mov #0xFFFF, r15
    mov #0x1, r2
    sxt r15
    call #guarda
; bloque 0144
    mov #0x1, r2
    cmp.b zona+36, zona+117
    call #guarda
; bloque 0145
    mov #0x102, r2
    addc.w #0xB2B6, zona+162
    call #guarda
; bloque 0146
    mov #0x18FF, r14
    mov #0x7FFE, r11
    mov #0x2, r2
    sub.w r11, r14
    call #guarda
; bloque 0147
    mov #0x100, r2
    rra.b &zona+142
    call #guarda
; bloque 0148
    mov #0x301D, r4
    mov #0x1, r2
    rrc.w r4
    call #guarda
; bloque 0149
    mov #0x0000, r11
    mov #0x1, r2
    subc.w &zona+224, r11
    call #guarda
; bloque 014a
    mov #zona+128, r14
    mov #0x105, r2
    add.b 21(r14), &zona+137
    call #guarda
; bloque 014b
    mov #zona+128, r6
    mov #0x3, r2
    sxt 0(r6)
    call #guarda
; bloque 014c
    mov #0x3112, r10
    mov #0x6, r2
    dadd.w #1, r10
    call #guarda
; bloque 014d
    mov #0, r15
    mov #sub, r5
    mov #0x2, r2
    call r5
    call #guarda
; bloque 014e
    mov #0xFFFF, r8
    mov #0x102, r2
    bit.w zona+212, r8
    call #guarda
; bloque 014f
    mov #0xE770, r15
    mov #0x101, r2
    swpb r15
    call #guarda
; bloque 0150
    mov #0x0000, r10
    mov #0x6, r2
    add.b #2, r10
    call #guarda
; bloque 0151
    mov #0xE724, r13
    mov #0x5, r2
    addc.w #0x5EC9, r13
    call #guarda
; bloque 0152
    mov #zona+128, r11
    mov #0x101, r2
    sub.w zona+68, -26(r11)
    call #guarda
; bloque 0153
    mov #0x97D7, r7
    mov #0x106, r2
    add.b zona+3, r7
    call #guarda
; bloque 0154
    mov #0xFFFF, r12
    mov #0x104, r2
    bic.b &zona+150, r12
    call #guarda
; bloque 0155
    mov #0x7, r2
    sxt zona+20
    call #guarda
; bloque 0156
    mov #zona+128, r10
    mov #zona+128, r7
    mov #0x105, r2
    sub.b -54(r7), 70(r10)
    call #guarda
; bloque 0157
    mov #0x5, r2
    mov.w #8, &zona+200
    call #guarda
; bloque 0158
    mov #0, r15
    mov #0x6, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0159
    mov #0x22FD, r9
    mov #0x4, r2
    sub.b r9, &zona+25
    call #guarda
; bloque 015a
    mov #0x104, r2
    addc.w &zona+116, &zona+94
    call #guarda
; bloque 015b
    mov #zona+88, r12
    mov #0x107, r2
    sxt @r12
    call #guarda
; bloque 015c
    mov #0x0026, r10
    mov #0x0046, r14
    mov #0x1, r2
    dadd.b r14, r10
    call #guarda
; bloque 015d
    mov #zona+128, r10
    mov #0x0008, r6
    mov #0x2, r2
    mov.b r6, 123(r10)
    call #guarda
; bloque 015e
    mov #0x8000, r10
    mov #0xEE45, r15
    mov #0x105, r2
    bis.w r15, r10
    call #guarda
; bloque 015f
    mov #0, r15
    mov #sub, r8
    mov #0x107, r2
    call r8
    call #guarda
; bloque 0160
    mov #0, r15
    mov #0x107, r2
    call #sub
    call #guarda
; bloque 0161
    mov #0, r15
    mov #tabla, r13
    mov #0x106, r2
    call @r13
    call #guarda
; bloque 0162
    mov #0x2, r2
    sub.w &zona+248, &zona+154
    call #guarda
; bloque 0163
    mov #0, r15
    mov #tabla-4, r10
    mov #0x103, r2
    call 4(r10)
    call #guarda
; bloque 0164
    mov #0xA399, r15
    mov #zona+128, r8
    mov #0x7, r2
    sub.w 76(r8), r15
    call #guarda
; bloque 0165
    mov #0, r15
    mov #0x7, r2
    call tabla
    call #guarda
; bloque 0166
    mov #0x3681, r15
    mov #0x105, r2
    bit.w r15, zona+122
    call #guarda
; bloque 0167
    mov #0x7FFE, r6
    mov #0x0000, r12
    mov #0x102, r2
    cmp.w r12, r6
    call #guarda
; bloque 0168
    mov #0x0100, r4
    mov #0x107, r2
    bit.b zona+239, r4
    call #guarda
; bloque 0169
    mov #0x2, r2
    bic.w &zona+54, zona+84
    call #guarda
; bloque 016a
    mov #zona+26, r11
    mov #0x4, r2
    swpb @r11+
    call #guarda
; bloque 016b
    mov #zona+228, r8
    mov #0x2, r2
    sxt @r8
    call #guarda
; bloque 016c
    mov #0x105, r2
    rrc.w &zona+92
    call #guarda
; bloque 016d
    mov #0, r15
    mov #0x106, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 016e
    mov #0xFFFF, r14
    mov #0x4, r2
    swpb r14
    call #guarda
; bloque 016f
    mov #0xDEB3, r7
    mov #0x6, r2
    push.w r7
    pop r15
    call #guarda
; bloque 0170
    mov #0, r15
    mov #tabla-4, r12
    mov #0x105, r2
    call 4(r12)
    call #guarda
; bloque 0171
    mov #zona+128, r10
    mov #0x104, r2
    swpb 52(r10)
    call #guarda
; bloque 0172
    mov #0x100, r2
    rra.w zona+18
    call #guarda
; bloque 0173
    mov #0, r15
    mov #0x6, r2
    call &tabla
    call #guarda
; bloque 0174
    mov #0x103, r2
    bis.b #0, &zona+242
    call #guarda
; bloque 0175
    mov #0, r15
    mov #0x7, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0176
    mov #0x83D5, r9
    mov #0x1, r2
    mov.w #0x9ADE, r9
    call #guarda
; bloque 0177
    mov #0x7EE7, r6
    mov #0xECB4, r9
    mov #0x1, r2
    add.w r9, r6
    call #guarda
; bloque 0178
    mov #0xB751, r10
    mov #zona+128, r11
    mov #0x103, r2
    sub.w -14(r11), r10
    call #guarda
; bloque 0179
    mov #zona+128, r4
    mov #zona+128, r14
    mov #0x4, r2
    cmp.b 119(r14), 46(r4)
    call #guarda
; bloque 017a
    mov #0xE719, r7
    mov #0x3, r2
    bit.b zona+140, r7
    call #guarda
; bloque 017b
    mov #zona+28, r12
    mov #0x105, r2
    rrc.b @r12
    call #guarda
; bloque 017c
    mov #0, r15
    mov #0x105, r2
    call #sub
    call #guarda
; bloque 017d
    mov #0, r15
    mov #0x105, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 017e
    mov #zona+128, r8
    mov #0x83FF, r4
    mov #0x6, r2
    and.w r4, -78(r8)
    call #guarda
; bloque 017f
    mov #0, r15
    mov #0x103, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0180
    mov #0x658C, r4
    mov #zona+80, r8
    mov #0x100, r2
    bic.b @r8, r4
    call #guarda
; bloque 0181
    mov #zona+8, r5
    mov #0x2, r2
    swpb @r5
    call #guarda
; bloque 0182
    mov #0x5B78, r15
    mov #0x106, r2
    push.b r15
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0183
    mov #zona+128, r8
    mov #zona+204, r14
    mov #0x102, r2
    sub.w @r14, 100(r8)
    call #guarda
; bloque 0184
    mov #0x104, r2
    rra.b zona+113
    call #guarda
; bloque 0185
    mov #0x0040, r4
    mov #0x101, r2
    mov.b r4, zona+160
    call #guarda
; bloque 0186
    mov #0x0063, r15
    mov #0x0093, r12
    mov #0x2, r2
    dadd.b r12, r15
    call #guarda
; bloque 0187
    mov #zona+128, r4
    mov #zona+130, r7
    mov #0x4, r2
    xor.w @r7, -38(r4)
    call #guarda
; bloque 0188
    mov #zona+128, r11
    mov #0x2, r2
    addc.b #0, 107(r11)
    call #guarda
; bloque 0189
    mov #0x0002, r6
    mov #0x4, r2
    dadd.b #8, r6
    call #guarda
; bloque 018a
    mov #zona+244, r13
    mov #0x102, r2
    push.b @r13
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 018b
    mov #0x7, r2
    rra.b &zona+19
    call #guarda
; bloque 018c
    mov #0x2A08, r8
    mov #0x102, r2
    cmp.w #2, r8
    call #guarda
; bloque 018d
    mov #0x2, r2
    cmp.w &zona+242, &zona+50
    call #guarda
; bloque 018e
    mov #0xFFFF, r13
    mov #0x103, r2
    swpb r13
    call #guarda
; bloque 018f
    mov #zona+128, r13
    mov #0x105, r2
    bis.b -6(r13), &zona+59
    call #guarda
; bloque 0190
    mov #zona+48, r6
    mov #0x4, r2
    rrc.b @r6
    call #guarda
; bloque 0191
    mov #zona+128, r15
    mov #0x101, r2
    bic.w #-1, -114(r15)
    call #guarda
; bloque 0192
    mov #zona+128, r5
    mov #0x104, r2
    xor.w &zona+230, -110(r5)
    call #guarda
; bloque 0193
    mov #zona+128, r8
    mov #zona+150, r10
    mov #0x1, r2
    subc.b @r10+, 59(r8)
    call #guarda
; bloque 0194
    mov #zona+128, r4
    mov #zona+128, r5
    mov #0x5, r2
    mov.w 22(r5), 108(r4)
    call #guarda
; bloque 0195
    mov #zona+128, r12
    mov #zona+192, r11
    mov #0x103, r2
    subc.w @r11+, -58(r12)
    call #guarda
; bloque 0196
    mov #0xFFFF, r14
    mov #0x103, r2
    swpb r14
    call #guarda
; bloque 0197
    mov #0x0200, r11
    mov #0x106, r2
    sxt r11
    call #guarda
; bloque 0198
    mov #0, r15
    mov #0x2, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0199
    mov #0x107, r2
    sxt zona+114
    call #guarda
; bloque 019a
    mov #zona+128, r13
    mov #0xF186, r10
    mov #0x107, r2
    bit.w r10, -38(r13)
    call #guarda
; bloque 019b
    mov #0x0E15, r5
    mov #0xF064, r8
    mov #0x7, r2
    and.b r8, r5
    call #guarda
; bloque 019c
    mov #zona+58, r13
    mov #0x1, r2
    sxt @r13+
    call #guarda
; bloque 019d
    mov #0, r15
    mov #0x107, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 019e
    mov #zona+76, r14
    mov #0x102, r2
    swpb @r14
    call #guarda
; bloque 019f
    mov #0x100, r2
    addc.b #-1, &zona+91
    call #guarda
; bloque 01a0
    mov #0, r15
    mov #0x6, r2
    call #sub
    call #guarda
; bloque 01a1
    mov #zona+128, r11
    mov #0x1, r2
    xor.w 16(r11), &zona+204
    call #guarda
; bloque 01a2
    mov #zona+108, r13
    mov #0x1, r2
    addc.b @r13, &zona+58
    call #guarda
; bloque 01a3
    mov #0xFDED, r10
    mov #0x6, r2
    xor.b r10, zona+1
    call #guarda
; bloque 01a4
    mov #zona+128, r11
    mov #0xA625, r12
    mov #0x6, r2
    mov.w r12, 52(r11)
    call #guarda
; bloque 01a5
    mov #0x2000, r4
    mov #zona+128, r14
    mov #0x103, r2
    cmp.b 32(r14), r4
    call #guarda
; bloque 01a6
    mov #0, r15
    mov #tabla, r5
    mov #0x102, r2
    call @r5+
    call #guarda
; bloque 01a7
    mov #zona+10, r9
    mov #0x2, r2
    rrc.w @r9+
    call #guarda
; bloque 01a8
    mov #0x1, r2
    addc.b #0, zona+45
    call #guarda
; bloque 01a9
    mov #zona+128, r12
    mov #zona+58, r15
    mov #0x104, r2
    cmp.w @r15+, -98(r12)
    call #guarda
; bloque 01aa
    mov #0xFFFF, r9
    mov #0x0, r2
    bic.b #0x2, r9
    call #guarda
; bloque 01ab
    mov #0, r15
    mov #0x105, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 01ac
    mov #0x0ACD, r11
    mov #0x101, r2
    bit.w #2, r11
    call #guarda
; bloque 01ad
    mov #zona+236, r7
    mov #0x5, r2
    sxt @r7
    call #guarda
; bloque 01ae
    mov #0, r15
    mov #0x1, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 01af
    mov #0xFFFF, r10
    mov #0x7, r2
    mov.b r10, &zona+207
    call #guarda
; bloque 01b0
    mov #0x7D84, r4
    mov #0x5, r2
    push.b r4
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 01b1
    mov #0x0000, r13
    mov #0x100, r2
    swpb r13
    call #guarda
; bloque 01b2
    mov #0x0005, r5
    mov #0x0, r2
    dadd.b #1, r5
    call #guarda
; bloque 01b3
    mov #0xA5FD, r13
    mov #0x106, r2
    rrc.w r13
    call #guarda
; bloque 01b4
    mov #0x0000, r4
    mov #0x100, r2
    sub.b r4, zona+160
    call #guarda
; bloque 01b5
    mov #0x2000, r8
    mov #0x107, r2
    sxt r8
    call #guarda
; bloque 01b6
    mov #0x0008, r10
    mov #zona+128, r13
    mov #0x2, r2
    sub.w 4(r13), r10
    call #guarda
; bloque 01b7
    mov #zona+36, r13
    mov #0x102, r2
    rra.w @r13+
    call #guarda
; bloque 01b8
    mov #0x8000, r15
    mov #0x100, r2
    sxt r15
    call #guarda
; bloque 01b9
    mov #0x6417, r8
    mov #zona+57, r7
    mov #0x6, r2
    bit.b @r7, r8
    call #guarda
; bloque 01ba
    mov #0x0D0B, r12
    mov #0x5492, r4
    mov #0x5, r2
    and.w r4, r12
    call #guarda
; bloque 01bb
    mov #0x2, r2
    rra.w zona+2
    call #guarda
; bloque 01bc
    mov #0xFFFF, r5
    mov #0x102, r2
    addc.b r5, zona+38
    call #guarda
; bloque 01bd
    mov #zona+128, r4
    mov #zona+128, r11
    mov #0x100, r2
    cmp.w -104(r11), 86(r4)
    call #guarda
; bloque 01be
    mov #0xFFFF, r13
    mov #zona+160, r8
    mov #0x101, r2
    subc.b @r8, r13
    call #guarda
; bloque 01bf
    mov #zona+160, r11
    mov #0x104, r2
    rra.w @r11+
    call #guarda
; bloque 01c0
    mov #0x7FFF, r15
    mov #zona+8, r5
    mov #0x2, r2
    xor.w @r5+, r15
    call #guarda
; bloque 01c1
    mov #0x0037, r8
    mov #0x3, r2
    dadd.b #2, r8
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
