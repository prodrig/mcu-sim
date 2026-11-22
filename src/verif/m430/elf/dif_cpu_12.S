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
    mov #zona+95, r11
    mov #0x1, r2
    xor.b @r11, zona+2
    call #guarda
; bloque 0001
    mov #zona+128, r9
    mov #0x5, r2
    xor.w zona+230, -88(r9)
    call #guarda
; bloque 0002
    mov #0x724D, r4
    mov #0x101, r2
    push.w r4
    pop r15
    call #guarda
; bloque 0003
    mov #0x57A9, r15
    mov #0x6, r2
    addc.b &zona+129, r15
    call #guarda
; bloque 0004
    mov #0x5564, r8
    mov #0x7, r2
    dadd.w #0, r8
    call #guarda
; bloque 0005
    mov #0xDC3D, r10
    mov #0x103, r2
    sxt r10
    call #guarda
; bloque 0006
    mov #0x0000, r6
    mov #0x7, r2
    bis.w &zona+70, r6
    call #guarda
; bloque 0007
    mov #zona+128, r11
    mov #zona+121, r12
    mov #0x5, r2
    bit.b @r12+, -56(r11)
    call #guarda
; bloque 0008
    mov #zona+128, r9
    mov #0x0001, r6
    mov #0x106, r2
    and.w r6, 106(r9)
    call #guarda
; bloque 0009
    mov #zona+128, r5
    mov #0x104, r2
    addc.w #1, 34(r5)
    call #guarda
; bloque 000a
    mov #0x1, r2
    push.w #0x2
    pop r15
    call #guarda
; bloque 000b
    mov #zona+128, r4
    mov #0x4, r2
    rra.b -63(r4)
    call #guarda
; bloque 000c
    mov #zona+128, r12
    mov #0x100, r2
    subc.w -114(r12), zona+250
    call #guarda
; bloque 000d
    mov #0x5569, r12
    mov #0x1, r2
    dadd.w #1, r12
    call #guarda
; bloque 000e
    mov #zona+128, r10
    mov #0x104, r2
    rrc.b -77(r10)
    call #guarda
; bloque 000f
    mov #zona+128, r5
    mov #0x1, r2
    sxt 28(r5)
    call #guarda
; bloque 0010
    mov #zona+128, r14
    mov #0x9F06, r5
    mov #0x0, r2
    cmp.b r5, -92(r14)
    call #guarda
; bloque 0011
    mov #0x105, r2
    mov.w zona+124, &zona+112
    call #guarda
; bloque 0012
    mov #zona+128, r7
    mov #0x3, r2
    subc.w #-1, -58(r7)
    call #guarda
; bloque 0013
    mov #zona+155, r12
    mov #0x6, r2
    rra.b @r12+
    call #guarda
; bloque 0014
    mov #zona+128, r10
    mov #0x106, r2
    rra.b 50(r10)
    call #guarda
; bloque 0015
    mov #zona+128, r11
    mov #0x5738, r9
    mov #0x103, r2
    sub.b r9, 70(r11)
    call #guarda
; bloque 0016
    mov #zona+128, r13
    mov #0x3, r2
    cmp.b -29(r13), &zona+72
    call #guarda
; bloque 0017
    mov #0x102, r2
    push.b &zona+158
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0018
    mov #0, r15
    mov #0x7, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0019
    mov #0x6C23, r8
    mov #0x102, r2
    mov.b #0xC, r8
    call #guarda
; bloque 001a
    mov #zona+128, r4
    mov #zona+154, r6
    mov #0x107, r2
    sub.w @r6, 10(r4)
    call #guarda
; bloque 001b
    mov #0x105, r2
    addc.b &zona+75, zona+162
    call #guarda
; bloque 001c
    mov #0xBB9D, r11
    mov #0xFFFF, r15
    mov #0x0, r2
    and.b r15, r11
    call #guarda
; bloque 001d
    mov #0xFFFF, r7
    mov #0x100, r2
    add.w #0, r7
    call #guarda
; bloque 001e
    mov #zona+128, r13
    mov #0x6, r2
    sxt -70(r13)
    call #guarda
; bloque 001f
    mov #zona+167, r13
    mov #0x3, r2
    bic.b @r13+, zona+22
    call #guarda
; bloque 0020
    mov #0, r15
    mov #sub, r8
    mov #0x107, r2
    call r8
    call #guarda
; bloque 0021
    mov #zona+128, r10
    mov #zona+128, r9
    mov #0x3, r2
    add.b 65(r9), 83(r10)
    call #guarda
; bloque 0022
    mov #0x7FFE, r9
    mov #0xFFFF, r13
    mov #0x100, r2
    subc.b r13, r9
    call #guarda
; bloque 0023
    mov #0x8D6E, r9
    mov #0x3, r2
    add.w zona+96, r9
    call #guarda
; bloque 0024
    mov #0, r15
    mov #0x1, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0025
    mov #0x3, r2
    sxt &zona+240
    call #guarda
; bloque 0026
    mov #0xFFFF, r8
    mov #0x104, r2
    cmp.w &zona+124, r8
    call #guarda
; bloque 0027
    mov #0x6C9E, r8
    mov #zona+150, r6
    mov #0x5, r2
    and.w @r6+, r8
    call #guarda
; bloque 0028
    mov #zona+128, r7
    mov #0x4, r2
    bit.b #1, 55(r7)
    call #guarda
; bloque 0029
    mov #zona+78, r6
    mov #0x101, r2
    swpb @r6+
    call #guarda
; bloque 002a
    mov #0, r15
    mov #tabla, r6
    mov #0x100, r2
    call @r6+
    call #guarda
; bloque 002b
    mov #0x8000, r12
    mov #0x1, r2
    mov.w r12, zona+28
    call #guarda
; bloque 002c
    mov #0x8346, r10
    mov #zona+238, r13
    mov #0x104, r2
    add.w @r13, r10
    call #guarda
; bloque 002d
    mov #zona+8, r12
    mov #0x6, r2
    rrc.w @r12
    call #guarda
; bloque 002e
    mov #0, r15
    mov #0x103, r2
    call &tabla
    call #guarda
; bloque 002f
    mov #zona+128, r12
    mov #0x1, r2
    rrc.w 106(r12)
    call #guarda
; bloque 0030
    mov #0x8000, r4
    mov #zona+128, r15
    mov #0x104, r2
    mov.b 7(r15), r4
    call #guarda
; bloque 0031
    mov #0xBA00, r8
    mov #0x2, r2
    push.w r8
    pop r15
    call #guarda
; bloque 0032
    mov #0x5AA6, r12
    mov #zona+140, r9
    mov #0x6, r2
    addc.w @r9, r12
    call #guarda
; bloque 0033
    mov #0x9221, r8
    mov #0x106, r2
    bis.b #4, r8
    call #guarda
; bloque 0034
    mov #0xD46C, r13
    mov #0x7, r2
    swpb r13
    call #guarda
; bloque 0035
    mov #zona+158, r12
    mov #0x105, r2
    sxt @r12
    call #guarda
; bloque 0036
    mov #0, r15
    mov #0x2, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0037
    mov #0x105, r2
    swpb &zona+10
    call #guarda
; bloque 0038
    mov #0, r15
    mov #tabla, r14
    mov #0x0, r2
    call @r14
    call #guarda
; bloque 0039
    mov #zona+131, r7
    mov #0x2, r2
    push.b @r7
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 003a
    mov #0, r15
    mov #0x1, r2
    call tabla
    call #guarda
; bloque 003b
    mov #0, r15
    mov #0x106, r2
    call #sub
    call #guarda
; bloque 003c
    mov #0x6699, r10
    mov #0x0, r2
    xor.b &zona+43, r10
    call #guarda
; bloque 003d
    mov #0x3, r2
    bic.w #1, &zona+168
    call #guarda
; bloque 003e
    mov #0x0, r2
    add.b &zona+38, &zona+58
    call #guarda
; bloque 003f
    mov #0, r15
    mov #0x104, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0040
    mov #zona+128, r12
    mov #0x7, r2
    bic.w -14(r12), &zona+10
    call #guarda
; bloque 0041
    mov #0xCE64, r15
    mov #0x2D8B, r8
    mov #0x1, r2
    mov.w r8, r15
    call #guarda
; bloque 0042
    mov #0x0000, r10
    mov #0x0576, r9
    mov #0x7, r2
    addc.b r9, r10
    call #guarda
; bloque 0043
    mov #0x317A, r11
    mov #0x102, r2
    and.b r11, &zona+43
    call #guarda
; bloque 0044
    mov #0, r15
    mov #0x6, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0045
    mov #0, r15
    mov #0x106, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0046
    mov #0x4, r2
    addc.w zona+136, zona+172
    call #guarda
; bloque 0047
    mov #0xE5E9, r13
    mov #0x103, r2
    rrc.w r13
    call #guarda
; bloque 0048
    mov #0, r15
    mov #0x1, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0049
    mov #0, r15
    mov #0x107, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 004a
    mov #zona+128, r9
    mov #0xFFFF, r4
    mov #0x107, r2
    sub.b r4, -42(r9)
    call #guarda
; bloque 004b
    mov #0, r15
    mov #0x6, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 004c
    mov #0x6, r2
    rra.w &zona+94
    call #guarda
; bloque 004d
    mov #0x107, r2
    rrc.w &zona+142
    call #guarda
; bloque 004e
    mov #0x1007, r11
    mov #0x0, r2
    dadd.w #0x83, r11
    call #guarda
; bloque 004f
    mov #0x0071, r4
    mov #0x5, r2
    dadd.b r4, r4
    call #guarda
; bloque 0050
    mov #zona+218, r10
    mov #0x4, r2
    swpb @r10+
    call #guarda
; bloque 0051
    mov #zona+184, r7
    mov #0x2, r2
    push.b @r7
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0052
    mov #zona+206, r14
    mov #0x103, r2
    addc.w @r14, zona+142
    call #guarda
; bloque 0053
    mov #0x7, r2
    addc.b &zona+174, zona+81
    call #guarda
; bloque 0054
    mov #zona+128, r9
    mov #0x2, r2
    subc.b -13(r9), &zona+72
    call #guarda
; bloque 0055
    mov #0x103, r2
    xor.w &zona+8, &zona+138
    call #guarda
; bloque 0056
    mov #0, r15
    mov #0x3, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0057
    mov #zona+88, r4
    mov #0x7, r2
    push.w @r4
    pop r15
    call #guarda
; bloque 0058
    mov #0x1237, r7
    mov #0x4, r2
    dadd.w #0x9732, r7
    call #guarda
; bloque 0059
    mov #0x0, r2
    push.b zona+31
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 005a
    mov #zona+128, r11
    mov #zona+114, r5
    mov #0x104, r2
    xor.w @r5, 78(r11)
    call #guarda
; bloque 005b
    mov #0x0E13, r4
    mov #0x104, r2
    push.w r4
    pop r15
    call #guarda
; bloque 005c
    mov #0x7FFE, r15
    mov #0x3, r2
    and.b zona+193, r15
    call #guarda
; bloque 005d
    mov #0, r15
    mov #tabla, r6
    mov #0x3, r2
    call @r6+
    call #guarda
; bloque 005e
    mov #zona+128, r10
    mov #0x4, r2
    add.b #1, 68(r10)
    call #guarda
; bloque 005f
    mov #zona+120, r6
    mov #0x0, r2
    rrc.w @r6
    call #guarda
; bloque 0060
    mov #0, r15
    mov #0x105, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0061
    mov #zona+128, r5
    mov #0x316B, r15
    mov #0x3, r2
    bis.b r15, 90(r5)
    call #guarda
; bloque 0062
    mov #0x2893, r14
    mov #0x5, r2
    dadd.w #2, r14
    call #guarda
; bloque 0063
    mov #0x650D, r12
    mov #0x2, r2
    sub.w &zona+154, r12
    call #guarda
; bloque 0064
    mov #0x1, r2
    xor.w zona+44, zona+200
    call #guarda
; bloque 0065
    mov #0x5, r2
    swpb &zona+128
    call #guarda
; bloque 0066
    mov #0x101, r2
    bit.b #8, &zona+224
    call #guarda
; bloque 0067
    mov #zona+128, r9
    mov #0x2, r2
    bic.w 114(r9), &zona+200
    call #guarda
; bloque 0068
    mov #0x8DB2, r6
    mov #0x2, r2
    sxt r6
    call #guarda
; bloque 0069
    mov #0, r15
    mov #0x102, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 006a
    mov #0x3, r2
    push.b #0x7E
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 006b
    mov #0x0000, r12
    mov #0x2, r2
    addc.b r12, zona+122
    call #guarda
; bloque 006c
    mov #0x100, r2
    subc.w &zona+186, zona+76
    call #guarda
; bloque 006d
    mov #0x106, r2
    sxt &zona+192
    call #guarda
; bloque 006e
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 006f
    mov #0x8EBF, r13
    mov #0x4, r2
    bic.w r13, zona+222
    call #guarda
; bloque 0070
    mov #0x0085, r9
    mov #0x0005, r4
    mov #0x6, r2
    dadd.b r4, r9
    call #guarda
; bloque 0071
    mov #0x0, r2
    push.b &zona+38
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0072
    mov #zona+232, r9
    mov #0x0, r2
    push.b @r9
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0073
    mov #0x107, r2
    bis.w #0, &zona+54
    call #guarda
; bloque 0074
    mov #zona+128, r4
    mov #zona+18, r11
    mov #0x101, r2
    mov.w @r11, -32(r4)
    call #guarda
; bloque 0075
    mov #0, r15
    mov #0x1, r2
    call tabla
    call #guarda
; bloque 0076
    mov #0x6, r2
    add.b #1, &zona+144
    call #guarda
; bloque 0077
    mov #zona+128, r8
    mov #0x2, r2
    rrc.b 71(r8)
    call #guarda
; bloque 0078
    mov #0x9861, r12
    mov #0x106, r2
    sub.w #0xCD60, r12
    call #guarda
; bloque 0079
    mov #0x1336, r9
    mov #0x0175, r5
    mov #0x102, r2
    bit.w r5, r9
    call #guarda
; bloque 007a
    mov #zona+87, r11
    mov #0x106, r2
    addc.b @r11, zona+218
    call #guarda
; bloque 007b
    mov #0x8000, r9
    mov #zona+22, r8
    mov #0x103, r2
    bis.w @r8+, r9
    call #guarda
; bloque 007c
    mov #0x0000, r8
    mov #0x8247, r7
    mov #0x100, r2
    sub.b r7, r8
    call #guarda
; bloque 007d
    mov #zona+128, r10
    mov #zona+182, r13
    mov #0x102, r2
    cmp.w @r13+, -116(r10)
    call #guarda
; bloque 007e
    mov #0x100, r2
    bic.b #0x82, &zona+82
    call #guarda
; bloque 007f
    mov #zona+128, r12
    mov #0x107, r2
    rrc.w -82(r12)
    call #guarda
; bloque 0080
    mov #zona+114, r8
    mov #0x2, r2
    push.b @r8+
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0081
    mov #zona+128, r5
    mov #0x100, r2
    xor.w #0, 36(r5)
    call #guarda
; bloque 0082
    mov #0xAB2B, r6
    mov #0x104, r2
    add.b r6, &zona+4
    call #guarda
; bloque 0083
    mov #0xDEF1, r6
    mov #0x103, r2
    sxt r6
    call #guarda
; bloque 0084
    mov #zona+128, r11
    mov #0x107, r2
    rra.w -30(r11)
    call #guarda
; bloque 0085
    mov #0xCDAC, r8
    mov #0x106, r2
    addc.w #-1, r8
    call #guarda
; bloque 0086
    mov #zona+128, r5
    mov #0x3A96, r8
    mov #0x106, r2
    sub.w r8, -88(r5)
    call #guarda
; bloque 0087
    mov #0, r15
    mov #0x107, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0088
    mov #0xFFFF, r9
    mov #0x6, r2
    rra.b r9
    call #guarda
; bloque 0089
    mov #zona+128, r5
    mov #0x6, r2
    sxt 58(r5)
    call #guarda
; bloque 008a
    mov #0xFFFF, r11
    mov #0x107, r2
    rrc.w r11
    call #guarda
; bloque 008b
    mov #zona+84, r7
    mov #0x103, r2
    rra.w @r7
    call #guarda
; bloque 008c
    mov #zona+196, r6
    mov #0x104, r2
    push.b @r6
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 008d
    mov #zona+158, r14
    mov #0x1, r2
    swpb @r14
    call #guarda
; bloque 008e
    mov #0x6, r2
    mov.b &zona+137, zona+17
    call #guarda
; bloque 008f
    mov #0x9CD8, r5
    mov #0x107, r2
    bit.w #0x4DF1, r5
    call #guarda
; bloque 0090
    mov #0x3464, r7
    mov #0x6498, r12
    mov #0x7, r2
    dadd.w r12, r7
    call #guarda
; bloque 0091
    mov #0x2234, r12
    mov #0x3, r2
    bis.b r12, r12
    call #guarda
; bloque 0092
    mov #zona+28, r8
    mov #0x7, r2
    swpb @r8+
    call #guarda
; bloque 0093
    mov #zona+45, r5
    mov #0x2, r2
    cmp.b @r5, zona+13
    call #guarda
; bloque 0094
    mov #0x0, r2
    rrc.w zona+8
    call #guarda
; bloque 0095
    mov #0, r15
    mov #0x107, r2
    call #sub
    call #guarda
; bloque 0096
    mov #zona+128, r6
    mov #zona+183, r4
    mov #0x5, r2
    add.b @r4, -77(r6)
    call #guarda
; bloque 0097
    mov #0x105, r2
    swpb &zona+92
    call #guarda
; bloque 0098
    mov #0x5, r2
    subc.b #1, zona+233
    call #guarda
; bloque 0099
    mov #0x7, r2
    swpb zona+110
    call #guarda
; bloque 009a
    mov #zona+128, r14
    mov #zona+142, r7
    mov #0x106, r2
    bit.b @r7+, -27(r14)
    call #guarda
; bloque 009b
    mov #zona+130, r9
    mov #0x101, r2
    subc.b @r9+, zona+165
    call #guarda
; bloque 009c
    mov #0x0000, r7
    mov #0x0004, r8
    mov #0x5, r2
    xor.b r8, r7
    call #guarda
; bloque 009d
    mov #zona+128, r14
    mov #0x104, r2
    cmp.b &zona+211, -60(r14)
    call #guarda
; bloque 009e
    mov #zona+128, r14
    mov #0x103, r2
    add.b #1, -72(r14)
    call #guarda
; bloque 009f
    mov #0x104, r2
    subc.b zona+96, &zona+216
    call #guarda
; bloque 00a0
    mov #zona+128, r13
    mov #0x104, r2
    addc.w 114(r13), &zona+156
    call #guarda
; bloque 00a1
    mov #0xFFFF, r15
    mov #0x107, r2
    xor.w &zona+124, r15
    call #guarda
; bloque 00a2
    mov #0x6334, r15
    mov #0x3, r2
    bic.b r15, &zona+239
    call #guarda
; bloque 00a3
    mov #0, r15
    mov #tabla, r4
    mov #0x102, r2
    call @r4
    call #guarda
; bloque 00a4
    mov #zona+200, r8
    mov #0x100, r2
    sxt @r8+
    call #guarda
; bloque 00a5
    mov #zona+110, r5
    mov #0x1, r2
    rra.b @r5
    call #guarda
; bloque 00a6
    mov #0x772F, r8
    mov #0x6, r2
    push.b r8
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00a7
    mov #0x7A32, r12
    mov #0x7FFF, r14
    mov #0x2, r2
    mov.w r14, r12
    call #guarda
; bloque 00a8
    mov #0x7A19, r4
    mov #0x5, r2
    push.w r4
    pop r15
    call #guarda
; bloque 00a9
    mov #0x4830, r11
    mov #zona+111, r5
    mov #0x106, r2
    addc.b @r5+, r11
    call #guarda
; bloque 00aa
    mov #zona+245, r11
    mov #0x3, r2
    sub.b @r11+, &zona+36
    call #guarda
; bloque 00ab
    mov #0x0400, r14
    mov #0x81F7, r5
    mov #0x105, r2
    addc.w r5, r14
    call #guarda
; bloque 00ac
    mov #zona+102, r9
    mov #0x100, r2
    add.b @r9, &zona+46
    call #guarda
; bloque 00ad
    mov #0xEF6E, r9
    mov #0x101, r2
    add.b #0, r9
    call #guarda
; bloque 00ae
    mov #0x685A, r8
    mov #0x104, r2
    sxt r8
    call #guarda
; bloque 00af
    mov #0, r15
    mov #tabla, r5
    mov #0x104, r2
    call @r5+
    call #guarda
; bloque 00b0
    mov #0, r15
    mov #0x103, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00b1
    mov #zona+128, r14
    mov #0x4, r2
    subc.w #0, 84(r14)
    call #guarda
; bloque 00b2
    mov #0x106, r2
    add.w #0x80, &zona+236
    call #guarda
; bloque 00b3
    mov #zona+11, r8
    mov #0x3, r2
    bit.b @r8+, &zona+146
    call #guarda
; bloque 00b4
    mov #0x6, r2
    mov.b #0xD8, &zona+226
    call #guarda
; bloque 00b5
    mov #zona+128, r13
    mov #0x7045, r6
    mov #0x101, r2
    mov.w r6, 96(r13)
    call #guarda
; bloque 00b6
    mov #0, r15
    mov #0x5, r2
    call #sub
    call #guarda
; bloque 00b7
    mov #0x104, r2
    sub.b #1, zona+88
    call #guarda
; bloque 00b8
    mov #0x0000, r12
    mov #zona+0, r11
    mov #0x6, r2
    sub.w @r11, r12
    call #guarda
; bloque 00b9
    mov #zona+128, r4
    mov #0x0004, r6
    mov #0x2, r2
    cmp.w r6, 116(r4)
    call #guarda
; bloque 00ba
    mov #0x62B8, r4
    mov #0x105, r2
    add.b #1, r4
    call #guarda
; bloque 00bb
    mov #zona+8, r7
    mov #0x107, r2
    rra.b @r7
    call #guarda
; bloque 00bc
    mov #0xE752, r7
    mov #0x104, r2
    sxt r7
    call #guarda
; bloque 00bd
    mov #0x105, r2
    add.w #0x17DC, &zona+42
    call #guarda
; bloque 00be
    mov #0x4DCC, r13
    mov #0x100, r2
    bic.b &zona+185, r13
    call #guarda
; bloque 00bf
    mov #0x102, r2
    rra.w &zona+146
    call #guarda
; bloque 00c0
    mov #0x0010, r14
    mov #0x4, r2
    dadd.b #0x85, r14
    call #guarda
; bloque 00c1
    mov #0x0008, r8
    mov #0x103, r2
    sub.b r8, zona+131
    call #guarda
; bloque 00c2
    mov #zona+98, r6
    mov #0x5, r2
    rra.w @r6+
    call #guarda
; bloque 00c3
    mov #0x0042, r8
    mov #0x4, r2
    dadd.b #0x18, r8
    call #guarda
; bloque 00c4
    mov #0x0100, r15
    mov #0x3, r2
    rra.w r15
    call #guarda
; bloque 00c5
    mov #zona+86, r12
    mov #0x1, r2
    and.w @r12+, &zona+76
    call #guarda
; bloque 00c6
    mov #0, r15
    mov #0x106, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c7
    mov #0, r15
    mov #tabla-4, r8
    mov #0x102, r2
    call 4(r8)
    call #guarda
; bloque 00c8
    mov #0x0000, r9
    mov #0x2, r2
    cmp.b #0, r9
    call #guarda
; bloque 00c9
    mov #zona+128, r8
    mov #0x5, r2
    mov.b #8, -123(r8)
    call #guarda
; bloque 00ca
    mov #0x5046, r8
    mov #0x102, r2
    xor.b zona+148, r8
    call #guarda
; bloque 00cb
    mov #0, r15
    mov #0x2, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00cc
    mov #zona+128, r12
    mov #zona+61, r7
    mov #0x3, r2
    subc.b @r7, -64(r12)
    call #guarda
; bloque 00cd
    mov #0x2, r2
    rra.w zona+62
    call #guarda
; bloque 00ce
    mov #0x0000, r9
    mov #zona+44, r7
    mov #0x107, r2
    sub.w @r7, r9
    call #guarda
; bloque 00cf
    mov #0x103, r2
    sxt &zona+48
    call #guarda
; bloque 00d0
    mov #0x747A, r8
    mov #0x1, r2
    sub.w &zona+118, r8
    call #guarda
; bloque 00d1
    mov #zona+128, r13
    mov #0x0, r2
    subc.w #-1, 100(r13)
    call #guarda
; bloque 00d2
    mov #0x1BD8, r13
    mov #0x106, r2
    xor.b #0x37, r13
    call #guarda
; bloque 00d3
    mov #0x107, r2
    sxt &zona+54
    call #guarda
; bloque 00d4
    mov #0, r15
    mov #0x4, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d5
    mov #0x100, r2
    bis.b #0xC0, zona+14
    call #guarda
; bloque 00d6
    mov #0x9403, r7
    mov #zona+114, r11
    mov #0x0, r2
    bis.w @r11+, r7
    call #guarda
; bloque 00d7
    mov #zona+36, r14
    mov #0x1, r2
    push.w @r14
    pop r15
    call #guarda
; bloque 00d8
    mov #0, r15
    mov #0x7, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d9
    mov #0x105, r2
    rra.w &zona+50
    call #guarda
; bloque 00da
    mov #zona+82, r11
    mov #0x100, r2
    swpb @r11+
    call #guarda
; bloque 00db
    mov #zona+180, r11
    mov #0x1, r2
    push.w @r11+
    pop r15
    call #guarda
; bloque 00dc
    mov #0x0000, r8
    mov #0x105, r2
    subc.b r8, r8
    call #guarda
; bloque 00dd
    mov #0x7FFF, r7
    mov #0x105, r2
    xor.b &zona+161, r7
    call #guarda
; bloque 00de
    mov #0x102, r2
    rra.b &zona+214
    call #guarda
; bloque 00df
    mov #0x3, r2
    mov.b #8, &zona+212
    call #guarda
; bloque 00e0
    mov #0x1C87, r7
    mov #0x892C, r6
    mov #0x102, r2
    add.w r6, r7
    call #guarda
; bloque 00e1
    mov #0, r15
    mov #0x5, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e2
    mov #0, r15
    mov #0x107, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e3
    mov #zona+20, r5
    mov #0x6, r2
    rrc.w @r5+
    call #guarda
; bloque 00e4
    mov #0x52C9, r11
    mov #zona+165, r12
    mov #0x5, r2
    subc.b @r12+, r11
    call #guarda
; bloque 00e5
    mov #0x7874, r13
    mov #0x3123, r6
    mov #0x4, r2
    dadd.w r6, r13
    call #guarda
; bloque 00e6
    mov #0x0035, r11
    mov #0x0, r2
    dadd.b #0x96, r11
    call #guarda
; bloque 00e7
    mov #0x0048, r10
    mov #0x0, r2
    dadd.b #4, r10
    call #guarda
; bloque 00e8
    mov #0, r15
    mov #0x3, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e9
    mov #0x4, r2
    addc.w zona+102, &zona+228
    call #guarda
; bloque 00ea
    mov #zona+128, r10
    mov #0x1, r2
    swpb 64(r10)
    call #guarda
; bloque 00eb
    mov #0, r15
    mov #0x107, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ec
    mov #0x0000, r11
    mov #0x1, r2
    subc.w r11, zona+82
    call #guarda
; bloque 00ed
    mov #zona+72, r10
    mov #0x7, r2
    swpb @r10+
    call #guarda
; bloque 00ee
    mov #zona+28, r12
    mov #0x4, r2
    swpb @r12+
    call #guarda
; bloque 00ef
    mov #zona+128, r9
    mov #zona+110, r6
    mov #0x4, r2
    addc.w @r6, -56(r9)
    call #guarda
; bloque 00f0
    mov #0, r15
    mov #0x2, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f1
    mov #0x102, r2
    rra.w zona+230
    call #guarda
; bloque 00f2
    mov #0x0064, r15
    mov #0x1, r2
    dadd.b #0, r15
    call #guarda
; bloque 00f3
    mov #0, r15
    mov #0x104, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00f4
    mov #0, r15
    mov #tabla, r13
    mov #0x5, r2
    call @r13
    call #guarda
; bloque 00f5
    mov #0x2, r2
    subc.w zona+238, &zona+130
    call #guarda
; bloque 00f6
    mov #0x9896, r5
    mov #0x104, r2
    xor.b #0xB, r5
    call #guarda
; bloque 00f7
    mov #0x0097, r8
    mov #0x6, r2
    dadd.b #0x15, r8
    call #guarda
; bloque 00f8
    mov #zona+128, r4
    mov #0x6, r2
    and.b -44(r4), zona+85
    call #guarda
; bloque 00f9
    mov #0x102, r2
    sxt &zona+242
    call #guarda
; bloque 00fa
    mov #0xBFFC, r11
    mov #zona+128, r14
    mov #0x6, r2
    xor.w 80(r14), r11
    call #guarda
; bloque 00fb
    mov #zona+128, r12
    mov #0x4, r2
    xor.b &zona+161, -6(r12)
    call #guarda
; bloque 00fc
    mov #0, r15
    mov #0x7, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00fd
    mov #0, r15
    mov #tabla, r6
    mov #0x102, r2
    call @r6+
    call #guarda
; bloque 00fe
    mov #0x101, r2
    sub.b #1, zona+91
    call #guarda
; bloque 00ff
    mov #0, r15
    mov #0x100, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0100
    mov #zona+128, r9
    mov #zona+81, r5
    mov #0x105, r2
    cmp.b @r5+, 91(r9)
    call #guarda
; bloque 0101
    mov #zona+128, r4
    mov #0x33D5, r13
    mov #0x103, r2
    bit.w r13, 24(r4)
    call #guarda
; bloque 0102
    mov #0x5B50, r6
    mov #0x6, r2
    add.w zona+82, r6
    call #guarda
; bloque 0103
    mov #0x7, r2
    and.w #0xDEC0, &zona+162
    call #guarda
; bloque 0104
    mov #0x0425, r11
    mov #0x104, r2
    cmp.w #0x2AD7, r11
    call #guarda
; bloque 0105
    mov #0x1, r2
    sub.b #0xFF, &zona+172
    call #guarda
; bloque 0106
    mov #zona+128, r15
    mov #zona+164, r13
    mov #0x5, r2
    sub.w @r13+, 48(r15)
    call #guarda
; bloque 0107
    mov #zona+128, r4
    mov #0x101, r2
    rrc.b 102(r4)
    call #guarda
; bloque 0108
    mov #0x3, r2
    sxt &zona+244
    call #guarda
; bloque 0109
    mov #0, r15
    mov #tabla-4, r11
    mov #0x4, r2
    call 4(r11)
    call #guarda
; bloque 010a
    mov #0, r15
    mov #0x6, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 010b
    mov #0, r15
    mov #tabla, r9
    mov #0x5, r2
    call @r9
    call #guarda
; bloque 010c
    mov #zona+128, r4
    mov #0x5, r2
    rrc.w 70(r4)
    call #guarda
; bloque 010d
    mov #zona+134, r4
    mov #0x3, r2
    sub.w @r4+, &zona+132
    call #guarda
; bloque 010e
    mov #zona+128, r10
    mov #0x101, r2
    and.w #0x50FB, 104(r10)
    call #guarda
; bloque 010f
    mov #0x9A9A, r4
    mov #0x0800, r12
    mov #0x105, r2
    sub.b r12, r4
    call #guarda
; bloque 0110
    mov #0xD79A, r12
    mov #0x2, r2
    bis.b &zona+174, r12
    call #guarda
; bloque 0111
    mov #0, r15
    mov #0x101, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0112
    mov #0x0008, r14
    mov #0x104, r2
    sub.b zona+2, r14
    call #guarda
; bloque 0113
    mov #0x106, r2
    and.w #1, zona+96
    call #guarda
; bloque 0114
    mov #zona+128, r11
    mov #0x7, r2
    sxt 84(r11)
    call #guarda
; bloque 0115
    mov #zona+128, r10
    mov #0xAE85, r4
    mov #0x104, r2
    bit.b r4, -61(r10)
    call #guarda
; bloque 0116
    mov #zona+157, r7
    mov #0x103, r2
    rra.b @r7
    call #guarda
; bloque 0117
    mov #0x74E2, r9
    mov #0x49AA, r10
    mov #0x1, r2
    bit.b r10, r9
    call #guarda
; bloque 0118
    mov #0x6, r2
    sxt &zona+22
    call #guarda
; bloque 0119
    mov #0x0008, r4
    mov #0x5, r2
    dadd.b #0x43, r4
    call #guarda
; bloque 011a
    mov #zona+128, r6
    mov #0x106, r2
    cmp.b #0xFF, -112(r6)
    call #guarda
; bloque 011b
    mov #0x42CB, r12
    mov #0x102, r2
    rra.w r12
    call #guarda
; bloque 011c
    mov #0xFFFF, r9
    mov #0x2, r2
    rra.w r9
    call #guarda
; bloque 011d
    mov #0xFFFF, r15
    mov #0x1, r2
    bic.w r15, &zona+242
    call #guarda
; bloque 011e
    mov #zona+128, r4
    mov #0xD9D6, r13
    mov #0x106, r2
    addc.b r13, -60(r4)
    call #guarda
; bloque 011f
    mov #0xF79B, r12
    mov #0x2, r2
    bit.w #0x20, r12
    call #guarda
; bloque 0120
    mov #0x103, r2
    subc.w #0x0, &zona+26
    call #guarda
; bloque 0121
    mov #zona+128, r15
    mov #zona+206, r8
    mov #0x4, r2
    bit.w @r8+, 62(r15)
    call #guarda
; bloque 0122
    mov #0x8858, r11
    mov #0x3, r2
    dadd.w #0x3731, r11
    call #guarda
; bloque 0123
    mov #zona+94, r8
    mov #0x0, r2
    addc.w @r8+, &zona+42
    call #guarda
; bloque 0124
    mov #0x8000, r12
    mov #0x0, r2
    sxt r12
    call #guarda
; bloque 0125
    mov #0x106, r2
    swpb zona+118
    call #guarda
; bloque 0126
    mov #0x6, r2
    bic.w #2, &zona+146
    call #guarda
; bloque 0127
    mov #0x106, r2
    rrc.w &zona+248
    call #guarda
; bloque 0128
    mov #0, r15
    mov #0x7, r2
    call #sub
    call #guarda
; bloque 0129
    mov #zona+222, r7
    mov #0x5, r2
    push.w @r7+
    pop r15
    call #guarda
; bloque 012a
    mov #0, r15
    mov #0x107, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 012b
    mov #0, r15
    mov #0x100, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 012c
    mov #0x0019, r8
    mov #0x0, r2
    dadd.b #2, r8
    call #guarda
; bloque 012d
    mov #0, r15
    mov #0x107, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 012e
    mov #zona+128, r4
    mov #0x3, r2
    sxt -48(r4)
    call #guarda
; bloque 012f
    mov #0x0043, r6
    mov #0x6, r2
    dadd.b #1, r6
    call #guarda
; bloque 0130
    mov #0xFFFF, r6
    mov #0x3, r2
    sxt r6
    call #guarda
; bloque 0131
    mov #0x0025, r7
    mov #0x6, r2
    dadd.b #8, r7
    call #guarda
; bloque 0132
    mov #zona+128, r13
    mov #0x7, r2
    xor.w 54(r13), zona+84
    call #guarda
; bloque 0133
    mov #zona+128, r10
    mov #0x80EA, r6
    mov #0x105, r2
    xor.w r6, -44(r10)
    call #guarda
; bloque 0134
    mov #zona+128, r7
    mov #0x8000, r10
    mov #0x5, r2
    addc.w r10, 58(r7)
    call #guarda
; bloque 0135
    mov #0x0797, r13
    mov #0x4, r2
    bic.b #2, r13
    call #guarda
; bloque 0136
    mov #0, r15
    mov #0x105, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0137
    mov #0xCDD2, r4
    mov #0x104, r2
    swpb r4
    call #guarda
; bloque 0138
    mov #0x105, r2
    push.b &zona+211
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0139
    mov #zona+128, r15
    mov #zona+67, r10
    mov #0x7, r2
    bit.b @r10+, -119(r15)
    call #guarda
; bloque 013a
    mov #0, r15
    mov #0x105, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 013b
    mov #0x0000, r7
    mov #0x3, r2
    addc.w #0x86D8, r7
    call #guarda
; bloque 013c
    mov #0x0040, r9
    mov #zona+128, r14
    mov #0x5, r2
    bit.b -40(r14), r9
    call #guarda
; bloque 013d
    mov #0x8000, r5
    mov #0x103, r2
    rrc.b r5
    call #guarda
; bloque 013e
    mov #0x0002, r12
    mov #0x4, r2
    bis.w r12, &zona+62
    call #guarda
; bloque 013f
    mov #0xFFFF, r10
    mov #0x6, r2
    add.b #0x76, r10
    call #guarda
; bloque 0140
    mov #0x0000, r7
    mov #zona+33, r15
    mov #0x103, r2
    subc.b @r15, r7
    call #guarda
; bloque 0141
    mov #0, r15
    mov #0x5, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0142
    mov #0, r15
    mov #0x100, r2
    call tabla
    call #guarda
; bloque 0143
    mov #0x4F67, r5
    mov #0x100, r2
    rrc.w r5
    call #guarda
; bloque 0144
    mov #0x0085, r7
    mov #0x4, r2
    dadd.b #0, r7
    call #guarda
; bloque 0145
    mov #0x0067, r11
    mov #0x0, r2
    dadd.b #4, r11
    call #guarda
; bloque 0146
    mov #zona+128, r11
    mov #0x102, r2
    push.b 19(r11)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0147
    mov #0, r15
    mov #sub, r10
    mov #0x7, r2
    call r10
    call #guarda
; bloque 0148
    mov #0xCC73, r13
    mov #0x7, r2
    swpb r13
    call #guarda
; bloque 0149
    mov #0x103, r2
    addc.b #0x52, &zona+134
    call #guarda
; bloque 014a
    mov #0x102, r2
    rra.w zona+6
    call #guarda
; bloque 014b
    mov #0x8787, r14
    mov #0x1913, r15
    mov #0x4, r2
    dadd.w r15, r14
    call #guarda
; bloque 014c
    mov #0x3701, r15
    mov #0x1, r2
    dadd.w #8, r15
    call #guarda
; bloque 014d
    mov #zona+128, r15
    mov #0x4, r2
    bic.b 108(r15), zona+64
    call #guarda
; bloque 014e
    mov #0x0110, r6
    mov #0x4, r2
    and.b r6, zona+122
    call #guarda
; bloque 014f
    mov #0xBBFA, r12
    mov #0x102, r2
    cmp.b r12, &zona+40
    call #guarda
; bloque 0150
    mov #zona+128, r5
    mov #0x107, r2
    and.b -22(r5), &zona+15
    call #guarda
; bloque 0151
    mov #0, r15
    mov #0x103, r2
    call &tabla
    call #guarda
; bloque 0152
    mov #0x5, r2
    rrc.b zona+66
    call #guarda
; bloque 0153
    mov #0x105, r2
    bit.w zona+232, &zona+198
    call #guarda
; bloque 0154
    mov #0x100, r2
    bis.b zona+250, &zona+225
    call #guarda
; bloque 0155
    mov #0xA21E, r15
    mov #0x103, r2
    addc.b r15, zona+240
    call #guarda
; bloque 0156
    mov #0x2307, r4
    mov #0x4, r2
    sxt r4
    call #guarda
; bloque 0157
    mov #0x9584, r15
    mov #0x0, r2
    dadd.w #0x9045, r15
    call #guarda
; bloque 0158
    mov #zona+128, r15
    mov #0x103, r2
    rra.b -30(r15)
    call #guarda
; bloque 0159
    mov #zona+128, r14
    mov #0xDE33, r8
    mov #0x2, r2
    cmp.w r8, 42(r14)
    call #guarda
; bloque 015a
    mov #zona+128, r6
    mov #0x6, r2
    rrc.b 0(r6)
    call #guarda
; bloque 015b
    mov #zona+4, r14
    mov #0x103, r2
    rrc.w @r14
    call #guarda
; bloque 015c
    mov #0x0020, r4
    mov #0x2, r2
    dadd.b #0x32, r4
    call #guarda
; bloque 015d
    mov #0x0002, r9
    mov #0x104, r2
    mov.w zona+42, r9
    call #guarda
; bloque 015e
    mov #zona+224, r14
    mov #0x102, r2
    push.w @r14
    pop r15
    call #guarda
; bloque 015f
    mov #0x3, r2
    xor.b #1, zona+22
    call #guarda
; bloque 0160
    mov #0x1, r2
    and.w &zona+112, zona+100
    call #guarda
; bloque 0161
    mov #zona+128, r5
    mov #0xFF78, r14
    mov #0x1, r2
    add.w r14, -126(r5)
    call #guarda
; bloque 0162
    mov #0, r15
    mov #0x7, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0163
    mov #0x2E46, r4
    mov #0x104, r2
    cmp.w #1, r4
    call #guarda
; bloque 0164
    mov #0x100, r2
    and.b zona+210, &zona+222
    call #guarda
; bloque 0165
    mov #0x3, r2
    add.w &zona+86, &zona+160
    call #guarda
; bloque 0166
    mov #0xDF85, r7
    mov #0x2, r2
    rrc.b r7
    call #guarda
; bloque 0167
    mov #zona+128, r14
    mov #0x106, r2
    add.w #0x611B, -54(r14)
    call #guarda
; bloque 0168
    mov #0x3F16, r6
    mov #zona+128, r12
    mov #0x5, r2
    bit.b 64(r12), r6
    call #guarda
; bloque 0169
    mov #zona+110, r15
    mov #0x100, r2
    addc.w @r15, &zona+58
    call #guarda
; bloque 016a
    mov #0x78B9, r15
    mov #0x2, r2
    subc.b zona+84, r15
    call #guarda
; bloque 016b
    mov #0x7FFE, r11
    mov #zona+123, r13
    mov #0x107, r2
    sub.b @r13+, r11
    call #guarda
; bloque 016c
    mov #0x3, r2
    and.b zona+4, zona+113
    call #guarda
; bloque 016d
    mov #zona+128, r4
    mov #0x106, r2
    add.b -53(r4), zona+71
    call #guarda
; bloque 016e
    mov #0, r15
    mov #0x0, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 016f
    mov #0, r15
    mov #0x6, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0170
    mov #0x3, r2
    xor.b #0x25, zona+181
    call #guarda
; bloque 0171
    mov #zona+128, r12
    mov #0x104, r2
    xor.b #0xCA, -62(r12)
    call #guarda
; bloque 0172
    mov #zona+230, r5
    mov #0x102, r2
    rra.w @r5+
    call #guarda
; bloque 0173
    mov #0x5, r2
    bis.w zona+118, &zona+196
    call #guarda
; bloque 0174
    mov #0x1503, r6
    mov #0x5, r2
    dadd.w #1, r6
    call #guarda
; bloque 0175
    mov #0x855C, r12
    mov #0x7, r2
    sub.b #0, r12
    call #guarda
; bloque 0176
    mov #zona+128, r13
    mov #0x08E3, r11
    mov #0x106, r2
    bis.b r11, -1(r13)
    call #guarda
; bloque 0177
    mov #0x5, r2
    bic.w zona+50, &zona+236
    call #guarda
; bloque 0178
    mov #zona+128, r6
    mov #0x104, r2
    and.b #0xFF, -116(r6)
    call #guarda
; bloque 0179
    mov #0x106, r2
    sxt zona+80
    call #guarda
; bloque 017a
    mov #0x0000, r13
    mov #0x3, r2
    mov.b #0, r13
    call #guarda
; bloque 017b
    mov #zona+106, r10
    mov #0x0, r2
    push.b @r10+
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 017c
    mov #zona+150, r5
    mov #0x105, r2
    xor.w @r5, &zona+132
    call #guarda
; bloque 017d
    mov #0x1, r2
    subc.w &zona+120, &zona+156
    call #guarda
; bloque 017e
    mov #0x104, r2
    sub.b #0xF3, zona+115
    call #guarda
; bloque 017f
    mov #zona+158, r14
    mov #0x0, r2
    bic.w @r14+, zona+192
    call #guarda
; bloque 0180
    mov #0x0406, r6
    mov #0x104, r2
    push.w r6
    pop r15
    call #guarda
; bloque 0181
    mov #0, r15
    mov #0x106, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0182
    mov #0, r15
    mov #tabla, r6
    mov #0x101, r2
    call @r6+
    call #guarda
; bloque 0183
    mov #0x0000, r9
    mov #0x6, r2
    dadd.b #0x75, r9
    call #guarda
; bloque 0184
    mov #0x106, r2
    addc.w &zona+108, zona+132
    call #guarda
; bloque 0185
    mov #zona+128, r10
    mov #0x102, r2
    sub.b #0x0, -90(r10)
    call #guarda
; bloque 0186
    mov #0x101, r2
    and.b #0x0, &zona+234
    call #guarda
; bloque 0187
    mov #0, r15
    mov #0x102, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0188
    mov #0x1, r2
    add.w &zona+200, zona+44
    call #guarda
; bloque 0189
    mov #0x0000, r5
    mov #zona+191, r12
    mov #0x0, r2
    bic.b @r12+, r5
    call #guarda
; bloque 018a
    mov #0, r15
    mov #0x7, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 018b
    mov #0x3814, r5
    mov #0x0000, r12
    mov #0x103, r2
    cmp.w r12, r5
    call #guarda
; bloque 018c
    mov #0x3, r2
    sub.w &zona+214, &zona+50
    call #guarda
; bloque 018d
    mov #0x5, r2
    bic.b #0x56, &zona+226
    call #guarda
; bloque 018e
    mov #zona+128, r11
    mov #0x106, r2
    push.w -48(r11)
    pop r15
    call #guarda
; bloque 018f
    mov #zona+128, r14
    mov #0x106, r2
    subc.b 6(r14), &zona+179
    call #guarda
; bloque 0190
    mov #zona+238, r10
    mov #0x101, r2
    mov.w @r10+, zona+118
    call #guarda
; bloque 0191
    mov #0xDB48, r15
    mov #zona+5, r7
    mov #0x6, r2
    xor.b @r7, r15
    call #guarda
; bloque 0192
    mov #0, r15
    mov #tabla, r8
    mov #0x103, r2
    call @r8
    call #guarda
; bloque 0193
    mov #0x5, r2
    cmp.w &zona+86, zona+56
    call #guarda
; bloque 0194
    mov #0x100, r2
    swpb &zona+102
    call #guarda
; bloque 0195
    mov #0, r15
    mov #0x6, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0196
    mov #0xFFFF, r5
    mov #0x1, r2
    sub.w &zona+130, r5
    call #guarda
; bloque 0197
    mov #0xA057, r7
    mov #0x65A4, r4
    mov #0x5, r2
    bis.b r4, r7
    call #guarda
; bloque 0198
    mov #0x103, r2
    mov.w #2, &zona+222
    call #guarda
; bloque 0199
    mov #0x0, r2
    sxt zona+86
    call #guarda
; bloque 019a
    mov #zona+48, r10
    mov #0x105, r2
    swpb @r10+
    call #guarda
; bloque 019b
    mov #zona+78, r10
    mov #0x103, r2
    bis.b @r10+, zona+175
    call #guarda
; bloque 019c
    mov #zona+128, r5
    mov #0x0, r2
    addc.b &zona+109, 92(r5)
    call #guarda
; bloque 019d
    mov #0x550B, r9
    mov #0x1, r2
    mov.w #0x15CB, r9
    call #guarda
; bloque 019e
    mov #zona+246, r14
    mov #0x107, r2
    bic.b @r14+, &zona+27
    call #guarda
; bloque 019f
    mov #zona+51, r4
    mov #0x105, r2
    push.b @r4
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 01a0
    mov #zona+36, r7
    mov #0x102, r2
    rra.w @r7
    call #guarda
; bloque 01a1
    mov #0, r15
    mov #0x103, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a2
    mov #0x750F, r11
    mov #0x2, r2
    push.b r11
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 01a3
    mov #zona+128, r15
    mov #zona+54, r13
    mov #0x106, r2
    cmp.w @r13, -70(r15)
    call #guarda
; bloque 01a4
    mov #0x246A, r14
    mov #zona+120, r11
    mov #0x100, r2
    bic.w @r11, r14
    call #guarda
; bloque 01a5
    mov #zona+128, r13
    mov #0x1, r2
    rra.b -2(r13)
    call #guarda
; bloque 01a6
    mov #0, r15
    mov #0x107, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a7
    mov #0x0000, r6
    mov #0x7, r2
    rrc.b r6
    call #guarda
; bloque 01a8
    mov #0, r15
    mov #tabla, r14
    mov #0x5, r2
    call @r14
    call #guarda
; bloque 01a9
    mov #0x8000, r15
    mov #0x103, r2
    xor.w r15, zona+200
    call #guarda
; bloque 01aa
    mov #zona+128, r4
    mov #0x1, r2
    sxt 68(r4)
    call #guarda
; bloque 01ab
    mov #zona+128, r13
    mov #0x4, r2
    rrc.b -27(r13)
    call #guarda
; bloque 01ac
    mov #zona+128, r8
    mov #0x4, r2
    bis.b 113(r8), zona+53
    call #guarda
; bloque 01ad
    mov #zona+128, r7
    mov #0x3, r2
    cmp.b &zona+195, -112(r7)
    call #guarda
; bloque 01ae
    mov #0x2, r2
    rra.w &zona+214
    call #guarda
; bloque 01af
    mov #zona+70, r6
    mov #0x104, r2
    bic.w @r6, &zona+222
    call #guarda
; bloque 01b0
    mov #0x0, r2
    swpb zona+188
    call #guarda
; bloque 01b1
    mov #0x6897, r8
    mov #zona+140, r12
    mov #0x105, r2
    xor.w @r12+, r8
    call #guarda
; bloque 01b2
    mov #zona+128, r15
    mov #0x102, r2
    add.b #0xFE, 76(r15)
    call #guarda
; bloque 01b3
    mov #0x105, r2
    rrc.b &zona+73
    call #guarda
; bloque 01b4
    mov #0x4055, r11
    mov #zona+128, r9
    mov #0x3, r2
    and.b -93(r9), r11
    call #guarda
; bloque 01b5
    mov #0x0000, r11
    mov #0x4, r2
    bit.w #1, r11
    call #guarda
; bloque 01b6
    mov #0xF51C, r14
    mov #zona+128, r15
    mov #0x5, r2
    subc.b 24(r15), r14
    call #guarda
; bloque 01b7
    mov #zona+128, r11
    mov #0x102, r2
    rrc.w 120(r11)
    call #guarda
; bloque 01b8
    mov #0x102, r2
    bic.b #0x7A, &zona+164
    call #guarda
; bloque 01b9
    mov #0x0000, r9
    mov #0x1, r2
    mov.b zona+39, r9
    call #guarda
; bloque 01ba
    mov #0x9ADC, r15
    mov #0x7FFF, r5
    mov #0x7, r2
    addc.b r5, r15
    call #guarda
; bloque 01bb
    mov #zona+128, r9
    mov #zona+88, r5
    mov #0x7, r2
    add.w @r5+, 98(r9)
    call #guarda
; bloque 01bc
    mov #0x0080, r5
    mov #zona+68, r12
    mov #0x4, r2
    subc.w @r12+, r5
    call #guarda
; bloque 01bd
    mov #zona+202, r10
    mov #0x0, r2
    rra.w @r10
    call #guarda
; bloque 01be
    mov #zona+128, r5
    mov #0x104, r2
    rrc.b 31(r5)
    call #guarda
; bloque 01bf
    mov #0xFF76, r6
    mov #zona+128, r12
    mov #0x101, r2
    xor.w 88(r12), r6
    call #guarda
; bloque 01c0
    mov #0x4, r2
    subc.b #1, &zona+246
    call #guarda
; bloque 01c1
    mov #0x2, r2
    subc.b #8, zona+74
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
