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
    mov #0x5, r2
    bic.b zona+150, &zona+218
    call #guarda
; bloque 0001
    mov #0x1571, r12
    mov #zona+152, r10
    mov #0x100, r2
    bit.b @r10, r12
    call #guarda
; bloque 0002
    mov #0x0F66, r7
    mov #0x7FFE, r11
    mov #0x100, r2
    bic.w r11, r7
    call #guarda
; bloque 0003
    mov #0x100, r2
    push.b &zona+141
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0004
    mov #0x94F1, r5
    mov #0x101, r2
    sub.w r5, &zona+196
    call #guarda
; bloque 0005
    mov #zona+128, r13
    mov #0x104, r2
    mov.b -110(r13), zona+107
    call #guarda
; bloque 0006
    mov #0x0077, r5
    mov #0x2, r2
    dadd.w #1, r5
    call #guarda
; bloque 0007
    mov #0, r15
    mov #0x7, r2
    call tabla
    call #guarda
; bloque 0008
    mov #0x7FFE, r6
    mov #0x158F, r14
    mov #0x6, r2
    bis.b r14, r6
    call #guarda
; bloque 0009
    mov #0x0080, r5
    mov #0xBE4F, r4
    mov #0x2, r2
    sub.b r4, r5
    call #guarda
; bloque 000a
    mov #0x3328, r13
    mov #0x6791, r7
    mov #0x7, r2
    dadd.w r7, r13
    call #guarda
; bloque 000b
    mov #0, r15
    mov #sub, r10
    mov #0x101, r2
    call r10
    call #guarda
; bloque 000c
    mov #0x25F5, r11
    mov #zona+70, r7
    mov #0x106, r2
    and.w @r7, r11
    call #guarda
; bloque 000d
    mov #zona+128, r12
    mov #zona+146, r6
    mov #0x107, r2
    mov.w @r6, -34(r12)
    call #guarda
; bloque 000e
    mov #zona+128, r4
    mov #0x107, r2
    sxt -120(r4)
    call #guarda
; bloque 000f
    mov #0x8000, r15
    mov #0x2, r2
    rrc.b r15
    call #guarda
; bloque 0010
    mov #0, r15
    mov #0x4, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0011
    mov #0, r15
    mov #0x106, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0012
    mov #0x0000, r5
    mov #0x107, r2
    push.w r5
    pop r15
    call #guarda
; bloque 0013
    mov #0, r15
    mov #0x5, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0014
    mov #0x39CC, r7
    mov #0x4, r2
    bis.w zona+126, r7
    call #guarda
; bloque 0015
    mov #0xE244, r9
    mov #0x102, r2
    addc.w &zona+126, r9
    call #guarda
; bloque 0016
    mov #0x6B77, r13
    mov #zona+128, r6
    mov #0x1, r2
    subc.w -94(r6), r13
    call #guarda
; bloque 0017
    mov #0, r15
    mov #0x6, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0018
    mov #0x104, r2
    push.b zona+183
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0019
    mov #0x107, r2
    bis.w #0x7FFE, &zona+90
    call #guarda
; bloque 001a
    mov #0x6, r2
    rra.w &zona+12
    call #guarda
; bloque 001b
    mov #0x107, r2
    swpb zona+50
    call #guarda
; bloque 001c
    mov #0x5181, r8
    mov #zona+100, r7
    mov #0x101, r2
    and.w @r7, r8
    call #guarda
; bloque 001d
    mov #zona+128, r12
    mov #0x4, r2
    xor.w 38(r12), &zona+144
    call #guarda
; bloque 001e
    mov #0x9EA5, r10
    mov #zona+136, r9
    mov #0x5, r2
    and.w @r9+, r10
    call #guarda
; bloque 001f
    mov #0x102, r2
    cmp.b #0x0, zona+235
    call #guarda
; bloque 0020
    mov #zona+185, r12
    mov #0x100, r2
    addc.b @r12, &zona+230
    call #guarda
; bloque 0021
    mov #0, r15
    mov #0x2, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0022
    mov #0x103, r2
    addc.b #0x6A, zona+244
    call #guarda
; bloque 0023
    mov #0x0808, r10
    mov #0x4, r2
    dadd.w #4, r10
    call #guarda
; bloque 0024
    mov #0x104, r2
    addc.w &zona+234, zona+104
    call #guarda
; bloque 0025
    mov #0x0069, r7
    mov #0x0042, r4
    mov #0x4, r2
    dadd.b r4, r7
    call #guarda
; bloque 0026
    mov #zona+128, r13
    mov #0x5, r2
    mov.b #4, -66(r13)
    call #guarda
; bloque 0027
    mov #0x51C7, r10
    mov #0x100, r2
    subc.b r10, zona+162
    call #guarda
; bloque 0028
    mov #zona+128, r6
    mov #zona+80, r14
    mov #0x5, r2
    bic.w @r14+, 102(r6)
    call #guarda
; bloque 0029
    mov #0, r15
    mov #0x106, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 002a
    mov #0x0139, r14
    mov #0x4, r2
    dadd.w #2, r14
    call #guarda
; bloque 002b
    mov #0x0000, r4
    mov #0x100, r2
    cmp.w r4, zona+26
    call #guarda
; bloque 002c
    mov #zona+128, r14
    mov #zona+24, r13
    mov #0x5, r2
    subc.w @r13, 108(r14)
    call #guarda
; bloque 002d
    mov #zona+128, r7
    mov #0x105, r2
    bit.b &zona+153, -18(r7)
    call #guarda
; bloque 002e
    mov #0xFFFF, r13
    mov #zona+128, r4
    mov #0x3, r2
    cmp.b 3(r4), r13
    call #guarda
; bloque 002f
    mov #0, r15
    mov #0x102, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0030
    mov #0x4, r2
    swpb zona+190
    call #guarda
; bloque 0031
    mov #0, r15
    mov #0x4, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0032
    mov #0x1961, r4
    mov #0x105, r2
    cmp.b zona+59, r4
    call #guarda
; bloque 0033
    mov #zona+128, r7
    mov #0x100, r2
    addc.w 36(r7), &zona+126
    call #guarda
; bloque 0034
    mov #zona+102, r12
    mov #0x107, r2
    rra.b @r12+
    call #guarda
; bloque 0035
    mov #0x4, r2
    cmp.b &zona+25, &zona+216
    call #guarda
; bloque 0036
    mov #0x5697, r10
    mov #0x100, r2
    rrc.b r10
    call #guarda
; bloque 0037
    mov #zona+128, r8
    mov #0x102, r2
    sub.b #0x8A, 7(r8)
    call #guarda
; bloque 0038
    mov #zona+174, r10
    mov #0x7, r2
    subc.w @r10, zona+192
    call #guarda
; bloque 0039
    mov #0x5, r2
    swpb &zona+230
    call #guarda
; bloque 003a
    mov #0x19CD, r13
    mov #zona+159, r10
    mov #0x7, r2
    add.b @r10, r13
    call #guarda
; bloque 003b
    mov #zona+128, r14
    mov #0x2, r2
    bit.w -62(r14), zona+152
    call #guarda
; bloque 003c
    mov #0, r15
    mov #tabla, r6
    mov #0x3, r2
    call @r6
    call #guarda
; bloque 003d
    mov #zona+156, r14
    mov #0x104, r2
    swpb @r14
    call #guarda
; bloque 003e
    mov #0x2000, r4
    mov #0x102, r2
    subc.w r4, r4
    call #guarda
; bloque 003f
    mov #0xF7D0, r14
    mov #0x107, r2
    xor.w &zona+122, r14
    call #guarda
; bloque 0040
    mov #0, r15
    mov #0x1, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0041
    mov #0xFFFF, r9
    mov #zona+128, r14
    mov #0x1, r2
    and.b 86(r14), r9
    call #guarda
; bloque 0042
    mov #zona+128, r12
    mov #0x101, r2
    cmp.w zona+20, -108(r12)
    call #guarda
; bloque 0043
    mov #zona+54, r14
    mov #0x4, r2
    rrc.w @r14
    call #guarda
; bloque 0044
    mov #zona+128, r5
    mov #0x0, r2
    addc.w #2, -36(r5)
    call #guarda
; bloque 0045
    mov #0, r15
    mov #0x104, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0046
    mov #0, r15
    mov #tabla-4, r12
    mov #0x107, r2
    call 4(r12)
    call #guarda
; bloque 0047
    mov #0x9922, r6
    mov #0x4, r2
    dadd.w #0, r6
    call #guarda
; bloque 0048
    mov #0x0001, r13
    mov #0x102, r2
    bic.w &zona+166, r13
    call #guarda
; bloque 0049
    mov #0x0000, r9
    mov #0x5, r2
    cmp.w #0x49AA, r9
    call #guarda
; bloque 004a
    mov #0x106, r2
    bic.b zona+86, zona+89
    call #guarda
; bloque 004b
    mov #zona+192, r14
    mov #0x107, r2
    rrc.w @r14+
    call #guarda
; bloque 004c
    mov #zona+128, r4
    mov #0x101, r2
    bic.w &zona+204, 52(r4)
    call #guarda
; bloque 004d
    mov #zona+128, r10
    mov #zona+108, r13
    mov #0x105, r2
    xor.b @r13, -76(r10)
    call #guarda
; bloque 004e
    mov #0x0000, r11
    mov #zona+122, r4
    mov #0x7, r2
    xor.w @r4, r11
    call #guarda
; bloque 004f
    mov #0x103, r2
    push.w #0xFFFF
    pop r15
    call #guarda
; bloque 0050
    mov #0x4, r2
    bic.b #0xAF, zona+227
    call #guarda
; bloque 0051
    mov #zona+128, r8
    mov #0x2, r2
    subc.w 4(r8), &zona+202
    call #guarda
; bloque 0052
    mov #0, r15
    mov #0x7, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 0053
    mov #0x102, r2
    cmp.w #0x8873, &zona+32
    call #guarda
; bloque 0054
    mov #zona+128, r13
    mov #0xF5CF, r15
    mov #0x2, r2
    addc.w r15, -60(r13)
    call #guarda
; bloque 0055
    mov #0x0000, r14
    mov #zona+128, r11
    mov #0x101, r2
    mov.w 82(r11), r14
    call #guarda
; bloque 0056
    mov #zona+102, r5
    mov #0x100, r2
    swpb @r5
    call #guarda
; bloque 0057
    mov #zona+128, r5
    mov #0x6, r2
    addc.w 118(r5), zona+190
    call #guarda
; bloque 0058
    mov #0x0010, r15
    mov #0x0, r2
    bis.w zona+74, r15
    call #guarda
; bloque 0059
    mov #zona+48, r7
    mov #0x107, r2
    sxt @r7
    call #guarda
; bloque 005a
    mov #0x3, r2
    xor.b zona+26, zona+23
    call #guarda
; bloque 005b
    mov #0x1302, r5
    mov #0x5, r2
    dadd.w #0, r5
    call #guarda
; bloque 005c
    mov #0x104, r2
    sxt zona+52
    call #guarda
; bloque 005d
    mov #0x097E, r14
    mov #0x103, r2
    mov.b #2, r14
    call #guarda
; bloque 005e
    mov #0x81FC, r13
    mov #0x102, r2
    bic.b #0x98, r13
    call #guarda
; bloque 005f
    mov #zona+128, r4
    mov #0x106, r2
    sxt 112(r4)
    call #guarda
; bloque 0060
    mov #0x0008, r9
    mov #0x5, r2
    swpb r9
    call #guarda
; bloque 0061
    mov #zona+128, r6
    mov #zona+31, r4
    mov #0x5, r2
    add.b @r4, 25(r6)
    call #guarda
; bloque 0062
    mov #zona+8, r10
    mov #0x1, r2
    rra.w @r10+
    call #guarda
; bloque 0063
    mov #0x10ED, r7
    mov #0x3C8E, r6
    mov #0x102, r2
    xor.b r6, r7
    call #guarda
; bloque 0064
    mov #0x347D, r10
    mov #0x7FFE, r11
    mov #0x104, r2
    cmp.w r11, r10
    call #guarda
; bloque 0065
    mov #0, r15
    mov #0x104, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0066
    mov #0x6, r2
    bic.b &zona+107, &zona+110
    call #guarda
; bloque 0067
    mov #0, r15
    mov #0x4, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0068
    mov #zona+101, r4
    mov #0x3, r2
    add.b @r4+, &zona+223
    call #guarda
; bloque 0069
    mov #0x2, r2
    rra.w zona+246
    call #guarda
; bloque 006a
    mov #0x4289, r5
    mov #0x104, r2
    xor.b r5, r5
    call #guarda
; bloque 006b
    mov #0xE66B, r5
    mov #0x6, r2
    rrc.b r5
    call #guarda
; bloque 006c
    mov #zona+128, r11
    mov #0x104, r2
    mov.b #1, -45(r11)
    call #guarda
; bloque 006d
    mov #zona+128, r9
    mov #zona+230, r12
    mov #0x1, r2
    and.w @r12+, 86(r9)
    call #guarda
; bloque 006e
    mov #0xA03F, r8
    mov #0x4, r2
    subc.w r8, r8
    call #guarda
; bloque 006f
    mov #0, r15
    mov #0x103, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 0070
    mov #0x6, r2
    swpb zona+60
    call #guarda
; bloque 0071
    mov #0x0010, r14
    mov #0x103, r2
    add.w #0x2E12, r14
    call #guarda
; bloque 0072
    mov #0, r15
    mov #0x6, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0073
    mov #0x0EC2, r8
    mov #zona+46, r6
    mov #0x6, r2
    sub.w @r6+, r8
    call #guarda
; bloque 0074
    mov #0xB820, r12
    mov #zona+82, r15
    mov #0x7, r2
    bit.w @r15, r12
    call #guarda
; bloque 0075
    mov #zona+128, r6
    mov #0x3, r2
    sxt 70(r6)
    call #guarda
; bloque 0076
    mov #0x103, r2
    and.w #0x8000, zona+138
    call #guarda
; bloque 0077
    mov #zona+155, r11
    mov #0x0, r2
    addc.b @r11, &zona+4
    call #guarda
; bloque 0078
    mov #0x107, r2
    add.w &zona+184, &zona+202
    call #guarda
; bloque 0079
    mov #0x57DB, r14
    mov #0x3, r2
    bis.b zona+96, r14
    call #guarda
; bloque 007a
    mov #zona+119, r9
    mov #0x100, r2
    rrc.b @r9+
    call #guarda
; bloque 007b
    mov #0x1415, r15
    mov #0x103, r2
    sxt r15
    call #guarda
; bloque 007c
    mov #0x75B2, r14
    mov #0x0080, r8
    mov #0x107, r2
    bit.b r8, r14
    call #guarda
; bloque 007d
    mov #0, r15
    mov #0x103, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 007e
    mov #0x107, r2
    and.w zona+214, zona+238
    call #guarda
; bloque 007f
    mov #0x4000, r10
    mov #0x3, r2
    bic.w zona+194, r10
    call #guarda
; bloque 0080
    mov #0, r15
    mov #0x106, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0081
    mov #0, r15
    mov #0x0, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0082
    mov #0x0000, r9
    mov #0x104, r2
    rra.b r9
    call #guarda
; bloque 0083
    mov #zona+214, r10
    mov #0x105, r2
    rrc.w @r10
    call #guarda
; bloque 0084
    mov #0, r15
    mov #0x105, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0085
    mov #zona+128, r10
    mov #zona+122, r13
    mov #0x100, r2
    addc.w @r13+, -24(r10)
    call #guarda
; bloque 0086
    mov #0x7626, r15
    mov #0x6, r2
    dadd.w #8, r15
    call #guarda
; bloque 0087
    mov #0x05D1, r15
    mov #0x101, r2
    subc.w r15, r15
    call #guarda
; bloque 0088
    mov #0x2000, r14
    mov #0x7, r2
    rrc.b r14
    call #guarda
; bloque 0089
    mov #0x0020, r10
    mov #zona+89, r15
    mov #0x0, r2
    and.b @r15+, r10
    call #guarda
; bloque 008a
    mov #0x106, r2
    push.b #2
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 008b
    mov #0x0047, r13
    mov #0x7, r2
    dadd.b #0x54, r13
    call #guarda
; bloque 008c
    mov #zona+157, r9
    mov #0x2, r2
    sub.b @r9, zona+98
    call #guarda
; bloque 008d
    mov #zona+128, r5
    mov #0x101, r2
    rrc.b -22(r5)
    call #guarda
; bloque 008e
    mov #0, r15
    mov #0x104, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 008f
    mov #zona+128, r8
    mov #zona+174, r7
    mov #0x103, r2
    mov.w @r7+, -70(r8)
    call #guarda
; bloque 0090
    mov #zona+128, r9
    mov #0x2, r2
    bit.w &zona+26, -62(r9)
    call #guarda
; bloque 0091
    mov #0xFB37, r9
    mov #0x106, r2
    rra.w r9
    call #guarda
; bloque 0092
    mov #zona+92, r9
    mov #0x0, r2
    push.w @r9+
    pop r15
    call #guarda
; bloque 0093
    mov #0, r15
    mov #0x2, r2
    call tabla
    call #guarda
; bloque 0094
    mov #zona+128, r10
    mov #0x6, r2
    rrc.w 92(r10)
    call #guarda
; bloque 0095
    mov #zona+128, r15
    mov #0x102, r2
    push.b -34(r15)
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0096
    mov #zona+212, r8
    mov #0x6, r2
    addc.b @r8, &zona+189
    call #guarda
; bloque 0097
    mov #0x104, r2
    addc.w zona+240, &zona+176
    call #guarda
; bloque 0098
    mov #0xFFFF, r4
    mov #zona+128, r5
    mov #0x3, r2
    xor.b -98(r5), r4
    call #guarda
; bloque 0099
    mov #zona+128, r8
    mov #zona+112, r15
    mov #0x3, r2
    mov.b @r15, -111(r8)
    call #guarda
; bloque 009a
    mov #0, r15
    mov #tabla, r14
    mov #0x100, r2
    call @r14+
    call #guarda
; bloque 009b
    mov #0x103, r2
    add.b zona+38, zona+30
    call #guarda
; bloque 009c
    mov #0xBB4F, r5
    mov #0x104, r2
    swpb r5
    call #guarda
; bloque 009d
    mov #0x6787, r9
    mov #0x106, r2
    rrc.w r9
    call #guarda
; bloque 009e
    mov #zona+168, r13
    mov #0x107, r2
    bic.w @r13+, &zona+156
    call #guarda
; bloque 009f
    mov #0x3CD0, r4
    mov #0x6, r2
    sxt r4
    call #guarda
; bloque 00a0
    mov #0x0000, r9
    mov #zona+134, r8
    mov #0x100, r2
    bit.w @r8+, r9
    call #guarda
; bloque 00a1
    mov #zona+215, r15
    mov #0x101, r2
    rrc.b @r15+
    call #guarda
; bloque 00a2
    mov #0x102, r2
    cmp.b #2, &zona+168
    call #guarda
; bloque 00a3
    mov #zona+128, r7
    mov #0xFFFF, r15
    mov #0x107, r2
    xor.b r15, -43(r7)
    call #guarda
; bloque 00a4
    mov #zona+128, r10
    mov #0x105, r2
    addc.w #0, -82(r10)
    call #guarda
; bloque 00a5
    mov #zona+128, r13
    mov #zona+26, r14
    mov #0x102, r2
    subc.w @r14, -112(r13)
    call #guarda
; bloque 00a6
    mov #0x7FFF, r14
    mov #0x2, r2
    and.w #0xB6E1, r14
    call #guarda
; bloque 00a7
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00a8
    mov #0xE80B, r15
    mov #0x3, r2
    sxt r15
    call #guarda
; bloque 00a9
    mov #0xE4C4, r12
    mov #0x100, r2
    bic.w &zona+72, r12
    call #guarda
; bloque 00aa
    mov #zona+102, r4
    mov #0x102, r2
    sub.b @r4+, zona+141
    call #guarda
; bloque 00ab
    mov #0x0100, r12
    mov #0xFFFF, r5
    mov #0x103, r2
    xor.w r5, r12
    call #guarda
; bloque 00ac
    mov #zona+246, r14
    mov #0x7, r2
    mov.b @r14+, zona+71
    call #guarda
; bloque 00ad
    mov #0, r15
    mov #0x0, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ae
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00af
    mov #0, r15
    mov #sub, r8
    mov #0x7, r2
    call r8
    call #guarda
; bloque 00b0
    mov #0x0400, r14
    mov #0x4, r2
    push.w r14
    pop r15
    call #guarda
; bloque 00b1
    mov #zona+128, r13
    mov #0x6, r2
    mov.b #1, 40(r13)
    call #guarda
; bloque 00b2
    mov #zona+128, r9
    mov #0x9D4E, r13
    mov #0x1, r2
    addc.b r13, 112(r9)
    call #guarda
; bloque 00b3
    mov #0xD975, r5
    mov #zona+95, r14
    mov #0x3, r2
    sub.b @r14+, r5
    call #guarda
; bloque 00b4
    mov #zona+30, r8
    mov #0x101, r2
    and.b @r8+, zona+148
    call #guarda
; bloque 00b5
    mov #zona+50, r15
    mov #0x103, r2
    rra.b @r15+
    call #guarda
; bloque 00b6
    mov #zona+128, r15
    mov #0x6, r2
    bis.b #0x0, 41(r15)
    call #guarda
; bloque 00b7
    mov #0, r15
    mov #0x3, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 00b8
    mov #zona+228, r7
    mov #0x7, r2
    sxt @r7+
    call #guarda
; bloque 00b9
    mov #0x7, r2
    swpb zona+162
    call #guarda
; bloque 00ba
    mov #0x75BC, r10
    mov #zona+166, r13
    mov #0x104, r2
    xor.w @r13+, r10
    call #guarda
; bloque 00bb
    mov #0x7, r2
    xor.w &zona+126, &zona+52
    call #guarda
; bloque 00bc
    mov #0x0816, r10
    mov #0x100, r2
    addc.b zona+94, r10
    call #guarda
; bloque 00bd
    mov #zona+128, r13
    mov #0x0000, r5
    mov #0x101, r2
    bic.w r5, 0(r13)
    call #guarda
; bloque 00be
    mov #zona+128, r13
    mov #0x5, r2
    subc.w -74(r13), zona+12
    call #guarda
; bloque 00bf
    mov #zona+44, r10
    mov #0x100, r2
    bic.w @r10, &zona+134
    call #guarda
; bloque 00c0
    mov #0x105, r2
    rra.w &zona+208
    call #guarda
; bloque 00c1
    mov #0x104, r2
    push.w #0x7FFE
    pop r15
    call #guarda
; bloque 00c2
    mov #zona+128, r11
    mov #zona+128, r15
    mov #0x4, r2
    sub.w -76(r15), 100(r11)
    call #guarda
; bloque 00c3
    mov #0, r15
    mov #0x103, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00c4
    mov #0x0057, r10
    mov #0x5, r2
    dadd.b #0, r10
    call #guarda
; bloque 00c5
    mov #0x0000, r14
    mov #zona+128, r4
    mov #0x7, r2
    bic.b -33(r4), r14
    call #guarda
; bloque 00c6
    mov #zona+128, r4
    mov #0x6, r2
    mov.b -102(r4), &zona+247
    call #guarda
; bloque 00c7
    mov #zona+128, r13
    mov #0x7, r2
    swpb 66(r13)
    call #guarda
; bloque 00c8
    mov #0x6, r2
    add.w #0xD9DD, &zona+68
    call #guarda
; bloque 00c9
    mov #0x2244, r9
    mov #0x7069, r4
    mov #0x7, r2
    dadd.w r4, r9
    call #guarda
; bloque 00ca
    mov #zona+128, r4
    mov #0x6, r2
    sxt 10(r4)
    call #guarda
; bloque 00cb
    mov #0, r15
    mov #0x103, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 00cc
    mov #0x100, r2
    push.w #0x7FFE
    pop r15
    call #guarda
; bloque 00cd
    mov #zona+78, r15
    mov #0x100, r2
    rrc.w @r15+
    call #guarda
; bloque 00ce
    mov #0x1010, r8
    mov #0x668C, r10
    mov #0x4, r2
    bic.w r10, r8
    call #guarda
; bloque 00cf
    mov #0x8000, r13
    mov #0x4, r2
    bit.w &zona+88, r13
    call #guarda
; bloque 00d0
    mov #0x102, r2
    push.b zona+197
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00d1
    mov #zona+186, r10
    mov #0x6, r2
    rra.b @r10
    call #guarda
; bloque 00d2
    mov #zona+128, r6
    mov #0x104, r2
    and.w 14(r6), &zona+16
    call #guarda
; bloque 00d3
    mov #0x106, r2
    bis.w #0xFFFF, &zona+216
    call #guarda
; bloque 00d4
    mov #0x6FDD, r7
    mov #0x1, r2
    bit.b #0xB0, r7
    call #guarda
; bloque 00d5
    mov #0, r15
    mov #0x101, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d6
    mov #0, r15
    mov #0x7, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00d7
    mov #zona+128, r8
    mov #0x107, r2
    swpb -96(r8)
    call #guarda
; bloque 00d8
    mov #0x8289, r5
    mov #0x3, r2
    dadd.w #2, r5
    call #guarda
; bloque 00d9
    mov #zona+114, r14
    mov #0x1, r2
    sxt @r14+
    call #guarda
; bloque 00da
    mov #0x24C1, r13
    mov #0x3, r2
    cmp.w r13, &zona+28
    call #guarda
; bloque 00db
    mov #0x22C0, r4
    mov #0x1, r2
    cmp.b #1, r4
    call #guarda
; bloque 00dc
    mov #zona+134, r14
    mov #0x105, r2
    sxt @r14+
    call #guarda
; bloque 00dd
    mov #0xFFFF, r11
    mov #zona+128, r12
    mov #0x101, r2
    and.w 48(r12), r11
    call #guarda
; bloque 00de
    mov #0, r15
    mov #0x1, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 00df
    mov #0, r15
    mov #0x104, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00e0
    mov #0x5073, r5
    mov #0x6, r2
    sxt r5
    call #guarda
; bloque 00e1
    mov #0, r15
    mov #0x1, r2
    call #sub
    call #guarda
; bloque 00e2
    mov #zona+16, r7
    mov #0x104, r2
    rrc.w @r7
    call #guarda
; bloque 00e3
    mov #0x7FFE, r13
    mov #0x101, r2
    mov.w #0x7FFE, r13
    call #guarda
; bloque 00e4
    mov #0x2, r2
    subc.b zona+200, zona+56
    call #guarda
; bloque 00e5
    mov #zona+128, r4
    mov #0x107, r2
    sxt 96(r4)
    call #guarda
; bloque 00e6
    mov #zona+128, r5
    mov #0x104, r2
    rrc.w -106(r5)
    call #guarda
; bloque 00e7
    mov #0xC63B, r5
    mov #0x5, r2
    addc.b #1, r5
    call #guarda
; bloque 00e8
    mov #0xC2C3, r4
    mov #0x105, r2
    sub.w zona+244, r4
    call #guarda
; bloque 00e9
    mov #0, r15
    mov #0x102, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 00ea
    mov #0x7955, r6
    mov #0x100, r2
    rrc.w r6
    call #guarda
; bloque 00eb
    mov #0x1937, r6
    mov #zona+31, r9
    mov #0x5, r2
    add.b @r9, r6
    call #guarda
; bloque 00ec
    mov #0x0874, r5
    mov #0x3, r2
    push.b r5
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 00ed
    mov #zona+128, r13
    mov #0x7FFF, r11
    mov #0x3, r2
    addc.b r11, -5(r13)
    call #guarda
; bloque 00ee
    mov #0xFFFF, r8
    mov #0x101, r2
    bis.b &zona+44, r8
    call #guarda
; bloque 00ef
    mov #0x8000, r6
    mov #0x2, r2
    bit.w zona+76, r6
    call #guarda
; bloque 00f0
    mov #0xFFFF, r9
    mov #0x4, r2
    swpb r9
    call #guarda
; bloque 00f1
    mov #zona+142, r8
    mov #0x4, r2
    bit.w @r8, zona+130
    call #guarda
; bloque 00f2
    mov #0xB1B7, r14
    mov #0x101, r2
    and.b #0x39, r14
    call #guarda
; bloque 00f3
    mov #0x103, r2
    bit.b #0xEC, zona+146
    call #guarda
; bloque 00f4
    mov #0x103, r2
    mov.b &zona+21, zona+12
    call #guarda
; bloque 00f5
    mov #zona+128, r4
    mov #zona+128, r6
    mov #0x7, r2
    subc.w 114(r6), 14(r4)
    call #guarda
; bloque 00f6
    mov #zona+76, r14
    mov #0x0, r2
    cmp.w @r14+, zona+204
    call #guarda
; bloque 00f7
    mov #0x103, r2
    rra.w &zona+120
    call #guarda
; bloque 00f8
    mov #0xFFFF, r14
    mov #zona+128, r8
    mov #0x6, r2
    cmp.b -62(r8), r14
    call #guarda
; bloque 00f9
    mov #0x106, r2
    sxt zona+186
    call #guarda
; bloque 00fa
    mov #zona+163, r11
    mov #0x5, r2
    rrc.b @r11+
    call #guarda
; bloque 00fb
    mov #0xEEFC, r12
    mov #0x0DC8, r8
    mov #0x100, r2
    subc.w r8, r12
    call #guarda
; bloque 00fc
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 00fd
    mov #zona+54, r12
    mov #0x6, r2
    swpb @r12
    call #guarda
; bloque 00fe
    mov #0x7FFF, r13
    mov #0x5, r2
    and.w #0x8000, r13
    call #guarda
; bloque 00ff
    mov #0xF2A4, r10
    mov #0x106, r2
    sxt r10
    call #guarda
; bloque 0100
    mov #0x3, r2
    xor.b &zona+135, zona+3
    call #guarda
; bloque 0101
    mov #0x0000, r15
    mov #0x104, r2
    sub.b #0x0, r15
    call #guarda
; bloque 0102
    mov #0, r15
    mov #0x3, r2
    jeq 1f
    mov #1, r15
1:
    call #guarda
; bloque 0103
    mov #0, r15
    mov #tabla, r13
    mov #0x102, r2
    call @r13+
    call #guarda
; bloque 0104
    mov #0, r15
    mov #0x105, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0105
    mov #0x6A79, r11
    mov #0x4, r2
    mov.b r11, zona+186
    call #guarda
; bloque 0106
    mov #zona+128, r7
    mov #0x6, r2
    xor.w #-1, -124(r7)
    call #guarda
; bloque 0107
    mov #0x68CF, r9
    mov #0x7, r2
    add.b zona+115, r9
    call #guarda
; bloque 0108
    mov #0x0, r2
    rrc.b zona+121
    call #guarda
; bloque 0109
    mov #0x9976, r15
    mov #0x4391, r7
    mov #0x4, r2
    dadd.w r7, r15
    call #guarda
; bloque 010a
    mov #zona+196, r10
    mov #0x105, r2
    rrc.b @r10
    call #guarda
; bloque 010b
    mov #0, r15
    mov #sub, r10
    mov #0x1, r2
    call r10
    call #guarda
; bloque 010c
    mov #0x105, r2
    bis.b #2, &zona+122
    call #guarda
; bloque 010d
    mov #0xAFB9, r8
    mov #0x2, r2
    bis.b &zona+135, r8
    call #guarda
; bloque 010e
    mov #0xEBB4, r5
    mov #zona+128, r13
    mov #0x106, r2
    bit.b -10(r13), r5
    call #guarda
; bloque 010f
    mov #zona+128, r15
    mov #0x6E99, r6
    mov #0x7, r2
    mov.w r6, 10(r15)
    call #guarda
; bloque 0110
    mov #0x7FDC, r8
    mov #zona+128, r6
    mov #0x101, r2
    cmp.b -76(r6), r8
    call #guarda
; bloque 0111
    mov #0xA94A, r14
    mov #0x101, r2
    mov.b r14, &zona+7
    call #guarda
; bloque 0112
    mov #0, r15
    mov #0x4, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0113
    mov #0x0400, r12
    mov #0x105, r2
    swpb r12
    call #guarda
; bloque 0114
    mov #zona+128, r15
    mov #0x101, r2
    addc.w zona+88, 26(r15)
    call #guarda
; bloque 0115
    mov #0x0C80, r14
    mov #0x7FFE, r5
    mov #0x1, r2
    subc.b r5, r14
    call #guarda
; bloque 0116
    mov #0x0010, r12
    mov #0x106, r2
    add.b r12, &zona+45
    call #guarda
; bloque 0117
    mov #0x9975, r7
    mov #0x107, r2
    cmp.w #2, r7
    call #guarda
; bloque 0118
    mov #0x0, r2
    addc.w zona+78, &zona+48
    call #guarda
; bloque 0119
    mov #zona+128, r8
    mov #0x1, r2
    and.w 0(r8), zona+92
    call #guarda
; bloque 011a
    mov #0, r15
    mov #0x7, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 011b
    mov #0xECF7, r9
    mov #0x5, r2
    bit.b &zona+177, r9
    call #guarda
; bloque 011c
    mov #0xABF7, r7
    mov #0x9EA2, r12
    mov #0x7, r2
    bis.w r12, r7
    call #guarda
; bloque 011d
    mov #zona+162, r5
    mov #0x1, r2
    add.w @r5, zona+182
    call #guarda
; bloque 011e
    mov #0x6EFF, r7
    mov #0x0, r2
    mov.w r7, zona+190
    call #guarda
; bloque 011f
    mov #0, r15
    mov #0x1, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0120
    mov #zona+25, r14
    mov #0x103, r2
    sub.b @r14, &zona+133
    call #guarda
; bloque 0121
    mov #0x0000, r14
    mov #zona+40, r11
    mov #0x0, r2
    cmp.w @r11, r14
    call #guarda
; bloque 0122
    mov #zona+128, r14
    mov #0x5, r2
    swpb -128(r14)
    call #guarda
; bloque 0123
    mov #zona+128, r4
    mov #0x3, r2
    and.w 118(r4), zona+166
    call #guarda
; bloque 0124
    mov #zona+128, r14
    mov #zona+128, r5
    mov #0x100, r2
    addc.w 50(r5), 110(r14)
    call #guarda
; bloque 0125
    mov #0xC4A3, r10
    mov #0x107, r2
    push.w r10
    pop r15
    call #guarda
; bloque 0126
    mov #zona+36, r13
    mov #0x104, r2
    sxt @r13+
    call #guarda
; bloque 0127
    mov #zona+48, r11
    mov #0x102, r2
    sxt @r11+
    call #guarda
; bloque 0128
    mov #zona+128, r6
    mov #0x4, r2
    bit.w 62(r6), &zona+96
    call #guarda
; bloque 0129
    mov #0x8000, r14
    mov #0x0000, r15
    mov #0x100, r2
    addc.w r15, r14
    call #guarda
; bloque 012a
    mov #zona+110, r7
    mov #0x5, r2
    sub.w @r7+, zona+28
    call #guarda
; bloque 012b
    mov #0x106, r2
    push.w &zona+144
    pop r15
    call #guarda
; bloque 012c
    mov #0, r15
    mov #0x4, r2
    call #sub
    call #guarda
; bloque 012d
    mov #0x1, r2
    swpb zona+32
    call #guarda
; bloque 012e
    mov #zona+128, r8
    mov #0x104, r2
    xor.w #0x8, -52(r8)
    call #guarda
; bloque 012f
    mov #zona+128, r5
    mov #0x4, r2
    swpb 122(r5)
    call #guarda
; bloque 0130
    mov #0, r15
    mov #0x0, r2
    call &tabla
    call #guarda
; bloque 0131
    mov #0x0069, r4
    mov #0x5, r2
    dadd.b #2, r4
    call #guarda
; bloque 0132
    mov #0x6128, r9
    mov #0x105, r2
    xor.w #0x868E, r9
    call #guarda
; bloque 0133
    mov #0x101, r2
    mov.b #0x21, zona+129
    call #guarda
; bloque 0134
    mov #zona+128, r10
    mov #0x0, r2
    bit.b #0x80, 15(r10)
    call #guarda
; bloque 0135
    mov #0, r15
    mov #sub, r13
    mov #0x4, r2
    call r13
    call #guarda
; bloque 0136
    mov #zona+128, r4
    mov #0x8327, r6
    mov #0x100, r2
    subc.b r6, -103(r4)
    call #guarda
; bloque 0137
    mov #0x0000, r5
    mov #0x106, r2
    xor.w zona+40, r5
    call #guarda
; bloque 0138
    mov #zona+170, r14
    mov #0x0, r2
    swpb @r14
    call #guarda
; bloque 0139
    mov #0, r15
    mov #0x101, r2
    jge 1f
    mov #1, r15
1:
    call #guarda
; bloque 013a
    mov #0x103, r2
    bit.b zona+130, &zona+232
    call #guarda
; bloque 013b
    mov #0x8F18, r12
    mov #0x105, r2
    add.b r12, zona+155
    call #guarda
; bloque 013c
    mov #0x8000, r15
    mov #0xE35C, r14
    mov #0x3, r2
    cmp.w r14, r15
    call #guarda
; bloque 013d
    mov #0x106, r2
    sub.b #1, zona+227
    call #guarda
; bloque 013e
    mov #0x1A10, r9
    mov #zona+72, r4
    mov #0x103, r2
    add.w @r4, r9
    call #guarda
; bloque 013f
    mov #0x6, r2
    xor.b zona+235, zona+230
    call #guarda
; bloque 0140
    mov #0xD6A3, r14
    mov #zona+128, r6
    mov #0x4, r2
    sub.b 108(r6), r14
    call #guarda
; bloque 0141
    mov #0, r15
    mov #0x2, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0142
    mov #zona+80, r6
    mov #0x6, r2
    sub.w @r6, &zona+132
    call #guarda
; bloque 0143
    mov #0, r15
    mov #0x107, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0144
    mov #0x3, r2
    push.b zona+50
    mov.b @r1, r15
    add #2, r1
    call #guarda
; bloque 0145
    mov #0xD237, r14
    mov #zona+128, r9
    mov #0x7, r2
    and.b 72(r9), r14
    call #guarda
; bloque 0146
    mov #0x101, r2
    and.w #0x80A2, zona+174
    call #guarda
; bloque 0147
    mov #0, r15
    mov #0x0, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 0148
    mov #0x7, r2
    sxt &zona+194
    call #guarda
; bloque 0149
    mov #0, r15
    mov #0x104, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 014a
    mov #0xFFFF, r4
    mov #0x101, r2
    bis.b zona+19, r4
    call #guarda
; bloque 014b
    mov #0xB509, r12
    mov #0x2, r2
    bit.b #2, r12
    call #guarda
; bloque 014c
    mov #0x105, r2
    rra.b &zona+161
    call #guarda
; bloque 014d
    mov #0x100, r2
    addc.b #2, &zona+75
    call #guarda
; bloque 014e
    mov #zona+128, r10
    mov #0x0, r2
    rrc.w 6(r10)
    call #guarda
; bloque 014f
    mov #zona+190, r4
    mov #0x100, r2
    push.w @r4
    pop r15
    call #guarda
; bloque 0150
    mov #0x0095, r9
    mov #0x102, r2
    addc.b #4, r9
    call #guarda
; bloque 0151
    mov #zona+128, r14
    mov #zona+128, r10
    mov #0x103, r2
    xor.w -76(r10), 4(r14)
    call #guarda
; bloque 0152
    mov #0x8000, r14
    mov #0xE158, r11
    mov #0x1, r2
    subc.b r11, r14
    call #guarda
; bloque 0153
    mov #zona+233, r5
    mov #0x106, r2
    add.b @r5, zona+59
    call #guarda
; bloque 0154
    mov #zona+128, r9
    mov #0x0, r2
    xor.b -9(r9), zona+6
    call #guarda
; bloque 0155
    mov #0, r15
    mov #tabla-4, r6
    mov #0x1, r2
    call 4(r6)
    call #guarda
; bloque 0156
    mov #0x3514, r9
    mov #0x0996, r6
    mov #0x7, r2
    dadd.w r6, r9
    call #guarda
; bloque 0157
    mov #0x7FFF, r7
    mov #0x9B10, r5
    mov #0x107, r2
    subc.w r5, r7
    call #guarda
; bloque 0158
    mov #0, r15
    mov #0x0, r2
    call tabla
    call #guarda
; bloque 0159
    mov #0x7FFF, r11
    mov #zona+18, r14
    mov #0x2, r2
    cmp.w @r14+, r11
    call #guarda
; bloque 015a
    mov #zona+128, r10
    mov #0x104, r2
    mov.b #0x80, 78(r10)
    call #guarda
; bloque 015b
    mov #0x0000, r11
    mov #0x103, r2
    swpb r11
    call #guarda
; bloque 015c
    mov #0x7FFF, r4
    mov #0x102, r2
    sub.b r4, zona+172
    call #guarda
; bloque 015d
    mov #0x0A02, r4
    mov #0x6, r2
    rra.w r4
    call #guarda
; bloque 015e
    mov #0x1564, r11
    mov #0x1, r2
    dadd.w #8, r11
    call #guarda
; bloque 015f
    mov #zona+245, r12
    mov #0x0, r2
    rra.b @r12+
    call #guarda
; bloque 0160
    mov #0, r15
    mov #0x6, r2
    jne 1f
    mov #1, r15
1:
    call #guarda
; bloque 0161
    mov #0, r15
    mov #0x5, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 0162
    mov #0x4167, r10
    mov #0x4, r2
    dadd.w #0, r10
    call #guarda
; bloque 0163
    mov #0xECCF, r13
    mov #zona+31, r11
    mov #0x101, r2
    subc.b @r11, r13
    call #guarda
; bloque 0164
    mov #0x3, r2
    bic.b &zona+152, zona+34
    call #guarda
; bloque 0165
    mov #zona+164, r11
    mov #0x102, r2
    and.w @r11+, zona+232
    call #guarda
; bloque 0166
    mov #0x0004, r5
    mov #0xE393, r8
    mov #0x100, r2
    add.w r8, r5
    call #guarda
; bloque 0167
    mov #zona+128, r14
    mov #0x104, r2
    sxt 26(r14)
    call #guarda
; bloque 0168
    mov #0, r15
    mov #0x1, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0169
    mov #0x104, r2
    swpb zona+238
    call #guarda
; bloque 016a
    mov #0x4, r2
    push.w #0x6612
    pop r15
    call #guarda
; bloque 016b
    mov #0x0078, r11
    mov #0x0012, r15
    mov #0x7, r2
    dadd.b r15, r11
    call #guarda
; bloque 016c
    mov #0x58DA, r14
    mov #zona+187, r8
    mov #0x106, r2
    subc.b @r8, r14
    call #guarda
; bloque 016d
    mov #0x0, r2
    rrc.w zona+118
    call #guarda
; bloque 016e
    mov #0, r15
    mov #tabla, r4
    mov #0x4, r2
    call @r4+
    call #guarda
; bloque 016f
    mov #0x0, r2
    mov.w #4, &zona+124
    call #guarda
; bloque 0170
    mov #0x0000, r9
    mov #0x6, r2
    xor.b r9, &zona+83
    call #guarda
; bloque 0171
    mov #0xB6BB, r13
    mov #0x102, r2
    push.w r13
    pop r15
    call #guarda
; bloque 0172
    mov #0x9544, r10
    mov #0x1, r2
    subc.b r10, &zona+77
    call #guarda
; bloque 0173
    mov #0x0060, r8
    mov #0x0023, r9
    mov #0x0, r2
    dadd.b r9, r8
    call #guarda
; bloque 0174
    mov #zona+128, r13
    mov #0x7, r2
    rra.w -16(r13)
    call #guarda
; bloque 0175
    mov #0x0000, r8
    mov #0x7FFE, r6
    mov #0x1, r2
    bit.b r6, r8
    call #guarda
; bloque 0176
    mov #0x26B0, r13
    mov #0x5, r2
    and.b #4, r13
    call #guarda
; bloque 0177
    mov #0xE5BD, r11
    mov #0x2, r2
    mov.w r11, zona+218
    call #guarda
; bloque 0178
    mov #0x2, r2
    rrc.b zona+45
    call #guarda
; bloque 0179
    mov #zona+128, r5
    mov #0x105, r2
    bic.b #0xBB, -39(r5)
    call #guarda
; bloque 017a
    mov #0, r15
    mov #0x3, r2
    call tabla
    call #guarda
; bloque 017b
    mov #0x0002, r7
    mov #0x0027, r4
    mov #0x2, r2
    dadd.b r4, r7
    call #guarda
; bloque 017c
    mov #0, r15
    mov #0x104, r2
    jn 1f
    mov #1, r15
1:
    call #guarda
; bloque 017d
    mov #0xFFFF, r15
    mov #0x101, r2
    sxt r15
    call #guarda
; bloque 017e
    mov #0xBCD2, r12
    mov #0x0000, r7
    mov #0x103, r2
    bic.b r7, r12
    call #guarda
; bloque 017f
    mov #zona+156, r14
    mov #0x107, r2
    cmp.w @r14+, zona+60
    call #guarda
; bloque 0180
    mov #0x87D0, r7
    mov #0x106, r2
    and.b zona+0, r7
    call #guarda
; bloque 0181
    mov #0x7268, r11
    mov #zona+140, r5
    mov #0x2, r2
    bit.w @r5+, r11
    call #guarda
; bloque 0182
    mov #0x0000, r6
    mov #zona+128, r13
    mov #0x107, r2
    xor.b -13(r13), r6
    call #guarda
; bloque 0183
    mov #zona+28, r14
    mov #0x107, r2
    sxt @r14+
    call #guarda
; bloque 0184
    mov #0, r15
    mov #0x6, r2
    jnc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0185
    mov #0x6E97, r11
    mov #0x107, r2
    rra.b r11
    call #guarda
; bloque 0186
    mov #0x100, r2
    addc.b #0x95, zona+196
    call #guarda
; bloque 0187
    mov #zona+128, r12
    mov #0x107, r2
    rrc.w 16(r12)
    call #guarda
; bloque 0188
    mov #zona+129, r11
    mov #0x4, r2
    rrc.b @r11+
    call #guarda
; bloque 0189
    mov #zona+30, r12
    mov #0x7, r2
    push.w @r12
    pop r15
    call #guarda
; bloque 018a
    mov #zona+128, r13
    mov #0x100, r2
    push.w 106(r13)
    pop r15
    call #guarda
; bloque 018b
    mov #0x2038, r8
    mov #0x6837, r9
    mov #0x7, r2
    dadd.w r9, r8
    call #guarda
; bloque 018c
    mov #0x7E31, r9
    mov #0x3, r2
    push.w r9
    pop r15
    call #guarda
; bloque 018d
    mov #zona+194, r7
    mov #0x1, r2
    sxt @r7
    call #guarda
; bloque 018e
    mov #0x1450, r8
    mov #0x103, r2
    bic.w #0x7ED8, r8
    call #guarda
; bloque 018f
    mov #zona+226, r4
    mov #0x0, r2
    swpb @r4
    call #guarda
; bloque 0190
    mov #0, r15
    mov #0x6, r2
    jc 1f
    mov #1, r15
1:
    call #guarda
; bloque 0191
    mov #0x0000, r14
    mov #zona+93, r5
    mov #0x1, r2
    xor.b @r5, r14
    call #guarda
; bloque 0192
    mov #0x104, r2
    sxt zona+42
    call #guarda
; bloque 0193
    mov #0xF140, r12
    mov #0x105, r2
    add.b r12, &zona+101
    call #guarda
; bloque 0194
    mov #0x580C, r6
    mov #zona+148, r15
    mov #0x100, r2
    subc.w @r15+, r6
    call #guarda
; bloque 0195
    mov #0x0, r2
    bic.b &zona+70, &zona+55
    call #guarda
; bloque 0196
    mov #zona+128, r10
    mov #0x3, r2
    subc.b #0x5E, 67(r10)
    call #guarda
; bloque 0197
    mov #0x0000, r5
    mov #0x4, r2
    addc.b zona+151, r5
    call #guarda
; bloque 0198
    mov #0, r15
    mov #tabla, r13
    mov #0x4, r2
    call @r13+
    call #guarda
; bloque 0199
    mov #zona+128, r11
    mov #0x102, r2
    xor.w 58(r11), zona+248
    call #guarda
; bloque 019a
    mov #0x9593, r13
    mov #zona+22, r15
    mov #0x2, r2
    addc.w @r15, r13
    call #guarda
; bloque 019b
    mov #zona+128, r11
    mov #zona+73, r5
    mov #0x100, r2
    xor.b @r5, -34(r11)
    call #guarda
; bloque 019c
    mov #0x0058, r8
    mov #0x2, r2
    dadd.b #8, r8
    call #guarda
; bloque 019d
    mov #zona+53, r11
    mov #0x103, r2
    sub.b @r11, zona+59
    call #guarda
; bloque 019e
    mov #zona+128, r14
    mov #0x6, r2
    rra.w 122(r14)
    call #guarda
; bloque 019f
    mov #0x1, r2
    xor.b #0xC9, zona+222
    call #guarda
; bloque 01a0
    mov #zona+128, r4
    mov #0x0000, r12
    mov #0x106, r2
    addc.w r12, -98(r4)
    call #guarda
; bloque 01a1
    mov #zona+128, r14
    mov #zona+12, r10
    mov #0x3, r2
    bit.w @r10, -126(r14)
    call #guarda
; bloque 01a2
    mov #0xF366, r14
    mov #0x7, r2
    sxt r14
    call #guarda
; bloque 01a3
    mov #0x106, r2
    subc.w zona+58, zona+68
    call #guarda
; bloque 01a4
    mov #0x86B3, r15
    mov #zona+192, r4
    mov #0x5, r2
    and.w @r4+, r15
    call #guarda
; bloque 01a5
    mov #0x0000, r5
    mov #0xFFFF, r4
    mov #0x105, r2
    add.w r4, r5
    call #guarda
; bloque 01a6
    mov #zona+182, r12
    mov #0x5, r2
    sxt @r12+
    call #guarda
; bloque 01a7
    mov #0x8186, r6
    mov #0x5, r2
    add.w r6, &zona+56
    call #guarda
; bloque 01a8
    mov #0, r15
    mov #0x103, r2
    jmp 1f
    mov #1, r15
1:
    call #guarda
; bloque 01a9
    mov #0xC857, r12
    mov #0x6, r2
    cmp.b #1, r12
    call #guarda
; bloque 01aa
    mov #0x7FFE, r7
    mov #zona+128, r15
    mov #0x4, r2
    and.b -18(r15), r7
    call #guarda
; bloque 01ab
    mov #0x102, r2
    bis.b #0x28, zona+242
    call #guarda
; bloque 01ac
    mov #zona+76, r9
    mov #0x104, r2
    addc.w @r9, &zona+246
    call #guarda
; bloque 01ad
    mov #0x77DD, r4
    mov #0x0, r2
    bit.w zona+118, r4
    call #guarda
; bloque 01ae
    mov #0x101, r2
    add.w #0xEBAA, &zona+200
    call #guarda
; bloque 01af
    mov #0x7EC5, r15
    mov #zona+149, r5
    mov #0x103, r2
    bic.b @r5+, r15
    call #guarda
; bloque 01b0
    mov #0x0377, r8
    mov #zona+226, r6
    mov #0x107, r2
    cmp.b @r6+, r8
    call #guarda
; bloque 01b1
    mov #zona+173, r11
    mov #0x106, r2
    rrc.b @r11+
    call #guarda
; bloque 01b2
    mov #zona+128, r8
    mov #0x5, r2
    bic.b #-1, -126(r8)
    call #guarda
; bloque 01b3
    mov #0, r15
    mov #0x103, r2
    call tabla
    call #guarda
; bloque 01b4
    mov #0x0400, r10
    mov #0x100, r2
    xor.w &zona+208, r10
    call #guarda
; bloque 01b5
    mov #zona+238, r10
    mov #0x100, r2
    sxt @r10+
    call #guarda
; bloque 01b6
    mov #0x3, r2
    bic.w #2, zona+10
    call #guarda
; bloque 01b7
    mov #zona+148, r11
    mov #0x103, r2
    sxt @r11
    call #guarda
; bloque 01b8
    mov #zona+6, r14
    mov #0x4, r2
    rra.w @r14+
    call #guarda
; bloque 01b9
    mov #0x100, r2
    swpb zona+230
    call #guarda
; bloque 01ba
    mov #0x6563, r8
    mov #0x4532, r9
    mov #0x5, r2
    dadd.w r9, r8
    call #guarda
; bloque 01bb
    mov #zona+236, r7
    mov #0x0, r2
    swpb @r7+
    call #guarda
; bloque 01bc
    mov #0, r15
    mov #0x7, r2
    jl 1f
    mov #1, r15
1:
    call #guarda
; bloque 01bd
    mov #0x0000, r10
    mov #0x4, r2
    cmp.w zona+184, r10
    call #guarda
; bloque 01be
    mov #zona+128, r11
    mov #0x101, r2
    bis.w 62(r11), &zona+244
    call #guarda
; bloque 01bf
    mov #0x0, r2
    mov.w #-1, zona+210
    call #guarda
; bloque 01c0
    mov #zona+128, r5
    mov #zona+128, r4
    mov #0x0, r2
    subc.w -34(r4), 50(r5)
    call #guarda
; bloque 01c1
    mov #0, r15
    mov #0x107, r2
    jmp 1f
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
