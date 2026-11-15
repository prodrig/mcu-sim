#!/usr/bin/env python3
# =============================================================================
# gen_ciclos.py — Las pruebas de ciclos del núcleo MSP430 (M1 de
# doc/analisis_msp430.md, §3.6, parte 3)
#
# Una prueba por fila de las tablas de ciclos de la guía de cada familia:
#   CPU    SLAU144K tablas 3-14, 3-15 y 3-16
#   CPUX   SLAU144K tablas 4-8, 4-9, 4-10, 4-17, 4-18 y 4-19
#   CPUXV2 SLAU391F (extracto de SLAU208) tablas 1-8, 1-9, 1-10, 1-17, 1-18 y 1-19
#
# Esta es la SEGUNDA transcripción de las tablas (la primera es
# m430/cpu/m430_ciclos.h): si las dos no dicen lo mismo, la prueba falla.
# No hay segunda fuente simulada: los simuladores de GDB y mspdebug no cuentan
# ciclos con fidelidad.
#
# Cada fila es un trozo de código entre dos lecturas del contador de ciclos
# del laboratorio (CICLOS_L, 0x0104): lo medido es los ciclos de la primera
# lectura (MOV &EDE,Rn: 3 en las tres tablas) más los de la instrucción. Las
# llamadas y las interrupciones leen el contador dentro de la rutina.
#
# Salida (versionada): verif/m430/elf/ciclos_cpu.elf, ciclos_x.elf y
# verif/m430/ciclos.inc (fila, código, esperado en CPU, CPUX y CPUXV2).
# =============================================================================
import os, subprocess, sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from gen_dif import herramienta

CNT = '&0x0104'
# Modos de origen y de destino, como texto de ensamblador. Los punteros: r6
# apunta a `dato` (que vale 0, para que un ADD al PC no salte) y r7 a dato-2
# para los índices; `dato` es simbólico y absoluto.
SRC = {
    'Rn':   ('r6', 'mov #0, r6'),
    '@Rn':  ('@r6', 'mov #dato, r6'),
    '@Rn+': ('@r6+', 'mov #dato, r6'),
    '#N':   ('#0x1000', ''),
    'X(Rn)': ('2(r7)', 'mov #dato-2, r7'),
    'EDE':  ('dato', ''),
    '&EDE': ('&dato', ''),
}
DST = {
    'Rm':   ('r8', ''),
    'PC':   ('pc', ''),
    'x(Rm)': ('2(r9)', 'mov #dato2-2, r9'),
    'EDE':  ('dato2', ''),
    '&EDE': ('&dato2', ''),
}
MS = ['Rn', '@Rn', '@Rn+', '#N', 'X(Rn)', 'EDE', '&EDE']
MD = ['Rm', 'PC', 'x(Rm)', 'EDE', '&EDE']

# --- La transcripción (fila por fila, en el orden de la guía) ----------------
# Formato I, MSP430: [CPU, CPUX, CPUXV2] por (origen, destino)
F1 = {
    ('Rn', 'Rm'): (1, 1, 1), ('Rn', 'PC'): (2, 2, 3), ('Rn', 'x(Rm)'): (4, 4, 4), ('Rn', 'EDE'): (4, 4, 4),
    ('Rn', '&EDE'): (4, 4, 4),
    ('@Rn', 'Rm'): (2, 2, 2), ('@Rn', 'PC'): (2, 3, 4), ('@Rn', 'x(Rm)'): (5, 5, 5), ('@Rn', 'EDE'): (5, 5, 5),
    ('@Rn', '&EDE'): (5, 5, 5),
    ('@Rn+', 'Rm'): (2, 2, 2), ('@Rn+', 'PC'): (3, 3, 4), ('@Rn+', 'x(Rm)'): (5, 5, 5),
    ('@Rn+', 'EDE'): (5, 5, 5), ('@Rn+', '&EDE'): (5, 5, 5),
    ('#N', 'Rm'): (2, 2, 2), ('#N', 'PC'): (3, 3, 3), ('#N', 'x(Rm)'): (5, 5, 5), ('#N', 'EDE'): (5, 5, 5),
    ('#N', '&EDE'): (5, 5, 5),
    ('X(Rn)', 'Rm'): (3, 3, 3), ('X(Rn)', 'PC'): (3, 3, 5), ('X(Rn)', 'x(Rm)'): (6, 6, 6),
    ('X(Rn)', 'EDE'): (6, 6, 6), ('X(Rn)', '&EDE'): (6, 6, 6),
    ('EDE', 'Rm'): (3, 3, 3), ('EDE', 'PC'): (3, 3, 5), ('EDE', 'x(Rm)'): (6, 6, 6), ('EDE', 'EDE'): (6, 6, 6),
    ('EDE', '&EDE'): (6, 6, 6),
    ('&EDE', 'Rm'): (3, 3, 3), ('&EDE', 'PC'): (3, 3, 5), ('&EDE', 'x(Rm)'): (6, 6, 6),
    ('&EDE', 'EDE'): (6, 6, 6), ('&EDE', '&EDE'): (6, 6, 6),
}
# Formato II, MSP430: (RRA..SXT, PUSH, CALL) × modo, [CPU, CPUX, CPUXV2]
F2 = {
    'Rn':    ((1, 1, 1), (3, 3, 3), (4, 3, 4)),
    '@Rn':   ((3, 3, 3), (4, 3, 3), (4, 4, 4)),
    '@Rn+':  ((3, 3, 3), (5, 3, 3), (5, 4, 4)),
    '#N':    (None, (4, 3, 3), (5, 4, 4)),
    'X(Rn)': ((4, 4, 4), (5, 4, 4), (5, 4, 5)),
    'EDE':   ((4, 4, 4), (5, 4, 4), (5, 4, 5)),
    '&EDE':  ((4, 4, 4), (5, 4, 4), (5, 4, 6)),
}
IRQ = (6, 5, 6)
RETI = (5, 3, 5)
SALTO = 2
# MSP430X formato I (.B/.W, .A) × (origen, destino), [CPUX, CPUXV2]
X1 = {
    ('Rn', 'Rm'): ((2, 2), (2, 2)), ('Rn', 'PC'): ((3, 3), (4, 4)), ('Rn', 'x(Rm)'): ((5, 7), (5, 7)),
    ('Rn', 'EDE'): ((5, 7), (5, 7)), ('Rn', '&EDE'): ((5, 7), (5, 7)),
    ('@Rn', 'Rm'): ((3, 4), (3, 4)), ('@Rn', 'PC'): ((3, 4), (5, 6)), ('@Rn', 'x(Rm)'): ((6, 9), (6, 9)),
    ('@Rn', 'EDE'): ((6, 9), (6, 9)), ('@Rn', '&EDE'): ((6, 9), (6, 9)),
    ('@Rn+', 'Rm'): ((3, 4), (3, 4)), ('@Rn+', 'PC'): ((4, 5), (5, 6)), ('@Rn+', 'x(Rm)'): ((6, 9), (6, 9)),
    ('@Rn+', 'EDE'): ((6, 9), (6, 9)), ('@Rn+', '&EDE'): ((6, 9), (6, 9)),
    ('#N', 'Rm'): ((3, 3), (3, 3)), ('#N', 'PC'): ((4, 4), (4, 4)), ('#N', 'x(Rm)'): ((6, 8), (6, 8)),
    ('#N', 'EDE'): ((6, 8), (6, 8)), ('#N', '&EDE'): ((6, 8), (6, 8)),
    ('X(Rn)', 'Rm'): ((4, 5), (4, 5)), ('X(Rn)', 'PC'): ((5, 6), (6, 7)), ('X(Rn)', 'x(Rm)'): ((7, 10), (7, 10)),
    ('X(Rn)', 'EDE'): ((7, 10), (7, 10)), ('X(Rn)', '&EDE'): ((7, 10), (7, 10)),
    ('EDE', 'Rm'): ((4, 5), (4, 5)), ('EDE', 'PC'): ((5, 6), (6, 7)), ('EDE', 'x(Rm)'): ((7, 10), (7, 10)),
    ('EDE', 'EDE'): ((7, 10), (7, 10)), ('EDE', '&EDE'): ((7, 10), (7, 10)),
    ('&EDE', 'Rm'): ((4, 5), (4, 5)), ('&EDE', 'PC'): ((5, 6), (6, 7)), ('&EDE', 'x(Rm)'): ((7, 10), (7, 10)),
    ('&EDE', 'EDE'): ((7, 10), (7, 10)), ('&EDE', '&EDE'): ((7, 10), (7, 10)),
}
# MSP430X formato II: [CPUX, CPUXV2] × modo
X2 = {
    'RRCX':    {'Rn': (2, 2), '@Rn': (4, 4), '@Rn+': (4, 4), 'X(Rn)': (5, 5), 'EDE': (5, 5), '&EDE': (5, 5)},
    'RRCX.A':  {'Rn': (2, 2), '@Rn': (6, 6), '@Rn+': (6, 6), 'X(Rn)': (7, 7), 'EDE': (7, 7), '&EDE': (7, 7)},
    'PUSHX':   {'Rn': (4, 4), '@Rn': (4, 4), '@Rn+': (4, 4), '#N': (4, 4), 'X(Rn)': (5, 5), 'EDE': (5, 5),
                '&EDE': (5, 5)},
    'PUSHX.A': {'Rn': (5, 5), '@Rn': (6, 6), '@Rn+': (6, 6), '#N': (6, 5), 'X(Rn)': (7, 7), 'EDE': (7, 7),
                '&EDE': (7, 7)},
    'CALLA':   {'Rn': (4, 5), '@Rn': (5, 6), '@Rn+': (5, 6), '#N': (4, 5), 'X(Rn)': (6, 5), 'EDE': (6, 7),
                '&EDE': (6, 7)},
}
# MOVA [CPUX, CPUXV2] y CMPA/ADDA/SUBA
MOVA = {
    ('Rn', 'Rm'): (1, 1), ('Rn', 'PC'): (2, 3), ('Rn', 'x(Rm)'): (4, 4), ('Rn', '&EDE'): (4, 4),
    ('@Rn', 'Rm'): (3, 3), ('@Rn', 'PC'): (3, 5), ('@Rn+', 'Rm'): (3, 3), ('@Rn+', 'PC'): (3, 5),
    ('#N', 'Rm'): (2, 2), ('#N', 'PC'): (3, 3), ('X(Rn)', 'Rm'): (4, 4), ('X(Rn)', 'PC'): (4, 6),
    ('&EDE', 'Rm'): (4, 4), ('&EDE', 'PC'): (4, 6),
}
CMPA = {('Rn', 'Rm'): (1, 1), ('Rn', 'PC'): (2, 3), ('#N', 'Rm'): (3, 3), ('#N', 'PC'): (3, 3)}


class Prog:
    def __init__(self, cpux):
        self.cpux = cpux
        self.l = []
        self.filas = []          # (nombre, (cpu, cpux, cpuxv2)); None = no aplica
        self.k = 0

    def medida(self, nombre, esperado, prep, codigo, tras=(), dentro=False):
        """Un trozo: prep; lee; código; lee; guarda la diferencia en res[k]."""
        k = self.k
        self.l.append(f'; {nombre}')
        for p in prep:
            if p:
                self.l.append('    ' + p)
        self.l.append(f'    mov {CNT}, r4')
        for c in codigo:
            self.l.append('    ' + c)
        if not dentro:
            self.l.append(f'    mov {CNT}, r5')
        self.l.append('    sub r4, r5')
        self.l.append('    sub #3, r5')
        self.l.append(f'    mov r5, &res+{2 * k}')
        for t in tras:
            self.l.append('    ' + t)
        self.filas.append((nombre, esperado))
        self.k += 1

    def f1(self):
        for s in MS:
            for d in MD:
                cpu, cx, v2 = F1[(s, d)]
                so, sp = SRC[s]
                do, dp = DST[d]
                if d == 'PC':
                    # ADD de 0 al PC: no salta (dato = 0; #N: el número salta 4 nops)
                    if s == '#N':
                        cod = ['add #6, pc', 'nop', 'nop', 'nop']    # #6: no es del generador de constantes
                    elif s == 'Rn':
                        cod = ['add r6, pc']
                    else:
                        cod = [f'add {so}, pc']
                else:
                    cod = [f'add {so}, {do}']
                self.medida(f'F1 ADD {s},{d}', (cpu, cx, v2), [sp, dp], cod)
                if d in ('x(Rm)', 'EDE', '&EDE'):
                    # MOV, BIT y CMP: un ciclo menos en CPUX y CPUXV2 (nota 1)
                    self.medida(f'F1 MOV {s},{d}', (cpu, cx - 1, v2 - 1), [sp, dp],
                                [f'mov {so}, {do}'])

    def f2(self):
        for m in MS:
            rot, push, call = F2[m]
            so, sp = SRC[m]
            if rot and m != '#N':
                self.medida(f'F2 RRA {m}', rot, [sp, 'clrc'], [f'rra {so}'])
            if m == '#N':
                so = '#0x1234'
            self.medida(f'F2 PUSH {m}', push, [sp], [f'push {so}'], ['incd r1'])
            # CALL: la segunda lectura, en la rutina
            if m == 'Rn':
                cs, cp = 'r10', 'mov #rut, r10'
            elif m == '#N':
                cs, cp = '#rut', ''
            elif m in ('@Rn', '@Rn+'):
                cs, cp = ('@r10' if m == '@Rn' else '@r10+'), 'mov #tab_rut, r10'
            elif m == 'X(Rn)':
                cs, cp = '2(r10)', 'mov #tab_rut-2, r10'
            elif m == 'EDE':
                cs, cp = 'tab_rut', ''
            else:
                cs, cp = '&tab_rut', ''
            self.medida(f'F2 CALL {m}', call, [cp], [f'call {cs}'], dentro=True)
        # CALL X(SP): CPUX, un ciclo más (nota 2 de la tabla 4-9)
        # (2(SP): 0(SP) lo ensambla binutils como @SP)
        self.medida('F2 CALL X(SP)', (5, 5, 5), ['push #rut', 'push #0'], ['call 2(r1)'], ['add #4, r1'],
                    dentro=True)

    def saltos(self):
        self.medida('JMP', (SALTO,) * 3, [], ['jmp 1f', '1:'])
        self.medida('JNE (no salta)', (SALTO,) * 3, ['clrz'], ['jeq 1f', '1:'])

    def irq(self):
        # La interrupción (vector 0xFFE8): la segunda lectura es la primera
        # instrucción de la rutina
        cpu, cx, v2 = IRQ
        mvi = (5, 4, 4)       # MOV #N,&EDE: 5, y 4 en CPUX/CPUXV2 (MOV, un ciclo menos)
        self.medida('IRQ (aceptar)', (cpu + mvi[0], cx + mvi[1], v2 + mvi[2]), ['eint', 'nop'],
                    ['mov #0xFFE8, &0x0108', 'nop'], ['dint', 'nop'], dentro=True)
        # RETI: la primera lectura, en la rutina justo antes de RETI (modo_irq
        # = 1); la segunda, la primera instrucción tras volver
        k = self.k
        self.l += ['; RETI', '    eint', '    nop', '    mov #1, &modo_irq', '    mov #0xFFE8, &0x0108',
                   f'    mov {CNT}, r5', '    sub r4, r5', '    sub #3, r5', f'    mov r5, &res+{2 * k}',
                   '    dint', '    nop', '    mov #0, &modo_irq']
        self.filas.append(('RETI', RETI))
        self.k += 1

    def x1(self):
        for s in MS:
            for d in MD:
                (bw, a), (bw2, a2) = X1[(s, d)]
                so, sp = SRC[s]
                do, dp = DST[d]
                for suf, c1, c2 in (('.w', bw, bw2), ('.a', a, a2)):
                    if d == 'PC':
                        if s == '#N':
                            cod = [f'addx{suf} #6, pc', 'nop', 'nop', 'nop']
                        elif s == 'Rn':
                            cod = [f'addx{suf} r6, pc']
                        else:
                            cod = [f'addx{suf} {so}, pc']
                    else:
                        cod = [f'addx{suf} {so}, {do}']
                    if d == 'PC' and s in ('#N', 'X(Rn)', 'EDE', '&EDE'):
                        c1, c2 = c1 - 1, c2 - 1          # nota 4: MOV, ADD y SUB, uno menos
                    esp = (None, c1, c2)
                    self.medida(f'X1 ADDX{suf.upper()} {s},{d}', esp, [sp.replace('mov ', 'mova ') if s in ('@Rn', '@Rn+', 'X(Rn)') else sp,
                                dp.replace('mov ', 'mova ') if dp else dp], cod)
                    if d in ('x(Rm)', 'EDE', '&EDE'):
                        r = 1 if suf == '.w' else 2      # notas 2 y 3
                        self.medida(f'X1 MOVX{suf.upper()} {s},{d}', (None, c1 - r, c2 - r),
                                    [sp.replace('mov ', 'mova ') if s in ('@Rn', '@Rn+', 'X(Rn)') else sp,
                                     dp.replace('mov ', 'mova ') if dp else dp], [f'movx{suf} {so}, {do}'])
        # La repetición: n + 1 (nota 1 de la tabla 4-18/1-18)
        self.medida('X1 RPT #4 ADDX.W Rn,Rm', (None, 5, 5), ['mov #1, r6', 'mov #0, r8'], ['rpt #4 { addx.w r6, r8'])
        self.medida('X1 RPT #16 RRCX.W Rn', (None, 17, 17), ['mov #0, r8'], ['rpt #16 { rrcx.w r8'])

    def x2(self):
        for ins, modos in X2.items():
            for m, (c1, c2) in modos.items():
                so, sp = SRC[m]
                sp = sp.replace('mov ', 'mova ')
                if ins.startswith('RRCX'):
                    suf = '.a' if ins.endswith('.A') else '.w'
                    self.medida(f'X2 {ins} {m}', (None, c1, c2), [sp, 'clrc'], [f'rrcx{suf} {so}'])
                elif ins.startswith('PUSHX'):
                    suf = '.a' if ins.endswith('.A') else '.w'
                    if m == '#N':
                        so = '#0x12345' if suf == '.a' else '#0x1234'
                    self.medida(f'X2 {ins} {m}', (None, c1, c2), [sp], [f'pushx{suf} {so}'],
                                ['adda #4, r1' if suf == '.a' else 'incd r1'])
                else:
                    if m == 'Rn':
                        cs, cp = 'r10', 'mova #ruta, r10'
                    elif m == '#N':
                        cs, cp = '#ruta', ''
                    elif m in ('@Rn', '@Rn+'):
                        cs, cp = ('@r10' if m == '@Rn' else '@r10+'), 'mova #tab_ruta, r10'
                    elif m == 'X(Rn)':
                        cs, cp = '4(r10)', 'mova #tab_ruta-4, r10'
                    elif m == 'EDE':
                        # binutils 2.34 resuelve CALLA EDE desde el código; la CPU,
                        # desde la palabra de índice (como el simulador de GDB y el
                        # desensamblador): -2
                        cs, cp = 'tab_ruta-2', ''
                    else:
                        cs, cp = '&tab_ruta', ''
                    self.medida(f'X2 CALLA {m}', (None, c1, c2), [cp], [f'calla {cs}'], dentro=True)
        for n in (1, 4):
            for op in ('rrcm', 'rram', 'rlam', 'rrum'):
                self.medida(f'X2 {op.upper()}.A #{n}', (None, n, n), ['mova #0x12345, r8'], [f'{op}.a #{n}, r8'])
        for n in (1, 3):
            self.medida(f'X2 PUSHM.W #{n}', (None, 2 + n, 2 + n), [], [f'pushm.w #{n}, r12'],
                        [f'popm.w #{n}, r12'])
            self.medida(f'X2 PUSHM.A #{n}', (None, 2 + 2 * n, 2 + 2 * n), [], [f'pushm.a #{n}, r12'],
                        [f'popm.a #{n}, r12'])
            self.medida(f'X2 POPM.A #{n}', (None, 2 + 2 * n, 2 + 2 * n), [f'pushm.a #{n}, r12'],
                        [f'popm.a #{n}, r12'], [])

    def mova(self):
        for (s, d), (c1, c2) in MOVA.items():
            if s == 'Rn':
                so, sp = 'r6', 'mova #ceroa, r6' if d == 'PC' else 'mova #0, r6'
            elif s in ('@Rn', '@Rn+'):
                so, sp = ('@r6' if s == '@Rn' else '@r6+'), 'mova #dato_a, r6'
            elif s == '#N':
                so, sp = '#0x12345', ''
            elif s == 'X(Rn)':
                so, sp = '4(r7)', 'mova #dato_a-4, r7'
            else:
                so, sp = '&dato_a', ''
            if d == 'Rm':
                cod = [f'mova {so}, r8']
            elif d == 'PC':
                # MOVA a PC: salta a la etiqueta siguiente (su dirección, en el dato)
                if s == '#N':
                    cod = ['mova #1f, pc', '1:']
                elif s == 'Rn':
                    sp = 'mova #1f, r6'
                    cod = ['mova r6, pc', '1:']
                else:
                    sp = (sp + '\n' if sp else '') + 'mova #1f, r11\n    mova r11, &dato_a'
                    cod = [f'mova {so}, pc', '1:']
            elif d == 'x(Rm)':
                cod = ['mova r6, 4(r9)']
                sp = (sp + '\n' if sp else '') + 'mova #dato2_a-4, r9'
            else:
                cod = ['mova r6, &dato2_a']
            self.medida(f'MOVA {s},{d}', (None, c1, c2), sp.split('\n') if sp else [], cod)
        for (s, d), (c1, c2) in CMPA.items():
            so = 'r6' if s == 'Rn' else '#0x00000'
            if d == 'PC':
                cod = [f'adda {"r6" if s == "Rn" else "#0"}, pc'] if s == 'Rn' else ['adda #4, pc', 'nop', 'nop']
                prep = ['mova #0, r6']
            else:
                cod = [f'cmpa {so}, r8']
                prep = ['mova #0, r6']
            self.medida(f'{"CMPA" if d == "Rm" else "ADDA"} {s},{d}', (None, c1, c2), prep, cod)

    def fuente(self):
        rut = ['rut:', f'    mov {CNT}, r5', '    ret']
        ruta = ['ruta:', f'    mov {CNT}, r5', '    reta']
        # La rutina de la interrupción (vector 0xFFE8): en `modo_irq` 0, lee y
        # vuelve; en 1, lee justo antes de RETI y la medida de RETI va fuera
        isr = ['isr:', f'    mov {CNT}, r5', '    tst &modo_irq', '    jz 1f', f'    mov {CNT}, r4',
               '1:  reti']
        cab = ['    .text', '    .global prueba', 'prueba:']
        if self.cpux:
            cab.append('    pushm.a #7, r10')
        else:
            cab += [f'    push r{i}' for i in range(4, 11)]
        pie = []
        if self.cpux:
            pie.append('    popm.a #7, r10')
        else:
            pie += [f'    pop r{i}' for i in range(10, 3, -1)]
        pie.append('    ret')
        # RETI: la segunda lectura, tras volver
        cuerpo = []
        for x in self.l:
            cuerpo.append(x)
        datos = ['    .data', '    .balign 4', 'dato: .word 0', '    .word 0', 'dato2: .word 0', '    .word 0',
                 'dato_a: .long 0', 'dato2_a: .long 0', 'ceroa: .long 0',
                 'tab_rut: .word rut', '    .word rut'] + (['tab_ruta: .long ruta', '    .long ruta'] if self.cpux else []) + [
                 'modo_irq: .word 0',
                 '    .global res', f'res: .space {2 * self.k}', '    .global n_filas', f'n_filas: .word {self.k}',
                 '    .section __interrupt_vector_20,"ax",@progbits', '    .word isr']   # 0xFFE8
        return '\n'.join(cab + cuerpo + pie + rut + (ruta if self.cpux else []) + isr + datos) + '\n'


def construye(cpux):
    p = Prog(cpux)
    p.f1(); p.f2(); p.saltos(); p.irq()
    if cpux:
        p.x1(); p.x2(); p.mova()
    return p


def arregla_reti(src):
    # RETI: la primera lectura es la de la rutina (r4); la segunda, tras volver
    return src


MAIN = r'''
#include <stdint.h>
extern void prueba(void);
int main(void) { prueba(); return 0; }
'''

def main():
    sal = os.path.join(AQUI, 'elf')
    inc = []
    for cpux, nombre in ((False, 'ciclos_cpu'), (True, 'ciclos_x')):
        p = construye(cpux)
        src = p.fuente()
        s = os.path.join(sal, nombre + '.S')
        open(s, 'w').write(src)
        c = os.path.join('/tmp', nombre + '_main.c')
        open(c, 'w').write(MAIN)
        flags = ['-mcpu=msp430x'] if cpux else ['-mcpu=msp430']
        r = subprocess.run([herramienta('msp430-elf-gcc')] + flags + ['-msim', '-O1', '-Wa,-mY', s, c, '-o',
                            os.path.join(sal, nombre + '.elf')], capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'gen_ciclos: no compila {nombre}:\n{r.stderr[:4000]}')
        for i, (n, e) in enumerate(p.filas):
            cpu, cx, v2 = (x if x is not None else -1 for x in e)
            inc.append(f'    {{"{nombre}", {i}, "{n}", {cpu}, {cx}, {v2}}},')
        print(f'{nombre}: {p.k} filas')
    open(os.path.join(AQUI, 'ciclos.inc'), 'w').write(
        '// GENERADO por verif/m430/gen_ciclos.py: {elf, fila, qué, CPU, CPUX, CPUXV2} (-1: no aplica)\n'
        + '\n'.join(inc) + '\n')

if __name__ == '__main__':
    main()
