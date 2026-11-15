#!/usr/bin/env python3
# =============================================================================
# gen_dif.py — Los programas diferenciales del núcleo MSP430 (M1 de
# doc/analisis_msp430.md, §3.6)
#
# Genera programas que recorren todos los modos de direccionamiento y todas
# las instrucciones, en 8, 16 y 20 bits, con operandos al azar (semilla fija).
# Cada bloque pone sus registros, ejecuta UNA instrucción y guarda R4-R15 y el
# SR en un vector; al final, main() los imprime con la zona de memoria que
# han tocado. Para cada programa:
#
#   1. lo compila con msp430-elf-gcc (-msim: el mapa y la E/S del simulador
#      de GDB);
#   2. lo ejecuta en msp430-elf-run (el simulador de GDB: CPUX con MPY32) y
#      guarda lo que imprime: es lo que tiene que imprimir el laboratorio;
#   3. los de CPU (16 bits), además, en `mspdebug sim` (otro simulador, de
#      otra gente): se para en `fin`, lee el vector con `md` y lo compara con
#      lo del de GDB. Una diferencia entre los dos oráculos se imprime: la
#      resuelve una persona (OVERRIDES, con la razón y la fuente).
#
# Salida (versionada): verif/m430/elf/dif_*.elf y dif_*.txt.
# Uso: gen_dif.py [--solo NOMBRE] [--sin-mspdebug]
# Pide msp430-elf-gcc y msp430-elf-run en el PATH (o en MSP430_GCC/bin).
# =============================================================================
import argparse, os, random, re, shutil, subprocess, sys, tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
SAL = os.path.join(AQUI, 'elf')

def herramienta(n):
    d = os.environ.get('MSP430_GCC')
    if d:
        p = os.path.join(d, 'bin', n)
        if os.path.exists(p):
            return p
    p = shutil.which(n)
    if not p:
        for c in ('/home/claude/ext/msp430/msp430-gcc-9.3.1.11_linux64/bin', '/opt/msp430-gcc/bin'):
            if os.path.exists(os.path.join(c, n)):
                return os.path.join(c, n)
        sys.exit(f'gen_dif: falta {n}')
    return p

F1 = ['mov', 'add', 'addc', 'subc', 'sub', 'cmp', 'dadd', 'bit', 'bic', 'bis', 'xor', 'and']
REGS = [f'r{i}' for i in range(4, 16)]
CG = ['#0', '#1', '#2', '#4', '#8', '#-1']
SALTOS = ['jne', 'jeq', 'jnc', 'jc', 'jn', 'jge', 'jl', 'jmp']
ZONA = 256

class Gen:
    def __init__(self, semilla, cpux, grande, n):
        self.r = random.Random(semilla)
        self.cpux, self.grande, self.n = cpux, grande, n
        self.asm, self.k = [], 0
        self.vals = {}

    # --- utilidades
    def val(self, ancho=None):
        r = self.r
        if ancho is None:
            ancho = 20 if self.cpux else 16
        m = (1 << ancho) - 1
        c = r.random()
        if c < 0.1:
            return 0
        if c < 0.2:
            return m
        if c < 0.3:
            return 1 << r.randrange(ancho)
        if c < 0.4:
            return (1 << (ancho - 1)) - r.randrange(3)       # cerca de los límites con signo
        return r.randrange(m + 1)

    def bcd(self, digitos):
        return int(''.join(str(self.r.randrange(10)) for _ in range(digitos)), 16)

    def pon(self, reg, v):
        if self.cpux and v > 0xFFFF:
            self.asm.append(f'    mova #0x{v:05X}, {reg}')
        else:
            self.asm.append(f'    mov #0x{v & 0xFFFF:04X}, {reg}')

    def flags(self, v=True):
        # V de entrada solo donde hay dos oráculos (CPU): el simulador de GDB no
        # baja V en muchas instrucciones ni lo mira en JL/JGE (ver CABECERA)
        f = 0
        for b in ((1, 2, 4, 0x100) if (v and not self.cpux) else (1, 2, 4)):
            if self.r.random() < 0.5:
                f |= b
        self.asm.append(f'    mov #0x{f:X}, r2')

    def guarda(self):
        self.asm.append('    call' + ('a' if self.grande else '') + ' #guarda')
        self.k += 1

    def regs_libres(self, *excluir):
        return [x for x in REGS if x not in excluir]

    # --- operandos
    def operando_mem(self, tam, como, ocupados):
        """Un operando en memoria dentro de zona: (texto, registro puntero usado o None)."""
        r = self.r
        al = 1 if tam == 1 else 2
        lim = ZONA - 4
        off = r.randrange(0, lim // al) * al
        if como == 'ind' or como == 'inc':
            p = r.choice(self.regs_libres(*ocupados))
            self.asm.append(f'    mov #zona+{off}, {p}' if not self.grande else f'    mova #zona+{off}, {p}')
            return (f'@{p}' + ('+' if como == 'inc' else ''), p)
        if como == 'idx':
            p = r.choice(self.regs_libres(*ocupados))
            base = 128
            x = off - base
            self.asm.append(f'    mov #zona+{base}, {p}' if not self.grande else f'    mova #zona+{base}, {p}')
            return (f'{x}({p})', p)
        if como == 'sym':
            return (f'zona+{off}', None)
        return (f'&zona+{off}', None)

    # --- los bloques
    def f1(self):
        r = self.r
        op = r.choice(F1)
        ext = self.cpux and r.random() < 0.5
        if ext:
            suf = r.choice(['.a', '.w', '.b'])
        else:
            suf = r.choice(['.w', '.b'])
        tam = {'.a': 4, '.w': 2, '.b': 1}[suf]
        nm = op + ('x' if ext else '') + suf
        smodo = r.choice(['rn', 'rn', 'ind', 'inc', 'imm', 'cg', 'idx', 'sym', 'abs'])
        dmodo = r.choice(['rn', 'rn', 'idx', 'sym', 'abs'])
        if op == 'dadd':
            smodo = r.choice(['rn', 'imm', 'cg'])
            dmodo = 'rn'
            if suf == '.a':                 # DADDX.A: el de GDB suma 16 bits (test430 lo mira)
                suf, tam = '.w', 2
                nm = op + ('x' if ext else '') + suf
        cg = CG if op != 'dadd' else ['#0', '#1', '#2', '#4', '#8']
        if self.grande and not ext and (smodo in ('sym', 'abs') or dmodo in ('sym', 'abs')):
            ext = True
            suf = r.choice(['.a', '.w', '.b'])
            tam = {'.a': 4, '.w': 2, '.b': 1}[suf]
            nm = op + 'x' + suf
        ocupados = []
        self.flags_luego = True
        # destino primero (los punteros no se pisan)
        if dmodo == 'rn':
            d = r.choice(REGS)
            ocupados.append(d)
            dv = self.bcd({1: 2, 2: 4, 4: 5}[tam]) if op == 'dadd' else self.val()
            self.pon(d, dv)
            dtxt = d
        else:
            dtxt, p = self.operando_mem(tam, dmodo, ocupados)
            if p:
                ocupados.append(p)
        if smodo == 'rn':
            s = r.choice(REGS)
            if s not in ocupados:
                sv = self.bcd({1: 2, 2: 4, 4: 5}[tam]) if op == 'dadd' else self.val()
                self.pon(s, sv)
            stxt = s
        elif smodo == 'imm':
            ancho = {1: 8, 2: 16, 4: 20}[tam]
            v = self.bcd({1: 2, 2: 4, 4: 5}[tam]) if op == 'dadd' else self.val(ancho)
            stxt = f'#0x{v:X}'
        elif smodo == 'cg':
            stxt = r.choice(cg)
        else:
            stxt, p = self.operando_mem(tam, smodo, ocupados)
            if p:
                ocupados.append(p)
        self.flags(op != 'dadd')       # DADD: V indefinido [UG2]
        self.asm.append(f'    {nm} {stxt}, {dtxt}')
        self.guarda()

    def f2(self):
        r = self.r
        # SXT en la CPUX: el de GDB no extiende a 19:16 en registro ni pone bien las
        # banderas: va en las pruebas propias de test430 (y en las de CPU, con mspdebug)
        # (y SWPBX.A: el de GDB borra 19:16, que la guía deja)
        op = r.choice(['rrc', 'rra', 'swpb', 'push'] + (['rru'] if self.cpux else ['sxt']))
        ext = self.cpux and (op == 'rru' or r.random() < 0.5)
        if op in ('swpb', 'sxt'):
            sufs = ['']
        else:
            sufs = ['.a', '.w', '.b'] if ext else ['.w', '.b']
        suf = r.choice(sufs)
        tam = {'.a': 4, '.w': 2, '.b': 1, '': 2}[suf]
        nm = op + ('x' if ext else '') + suf
        modos = ['rn', 'rn', 'ind', 'inc', 'idx', 'sym', 'abs']
        if self.cpux:
            modos.remove('inc')        # el de GDB sube Rn dos veces en el formato II con @Rn+
        if op == 'push':
            modos += ['imm', 'cg']
        m = r.choice(modos)
        if self.grande and not ext and m in ('sym', 'abs'):
            ext = True
            suf = r.choice(['.a', ''] if op in ('swpb', 'sxt') else ['.a', '.w', '.b'])
            tam = {'.a': 4, '.w': 2, '.b': 1, '': 2}[suf]
            nm = op + 'x' + suf
            if op == 'swpb' and suf == '.a':
                suf, tam, nm = '', 2, 'swpbx'
        if m == 'rn':
            d = r.choice(REGS)
            self.pon(d, self.val())
            txt = d
        elif m == 'imm':
            txt = f'#0x{self.val({1: 8, 2: 16, 4: 20}[tam]):X}'
        elif m == 'cg':
            txt = r.choice(CG)
        else:
            txt, _ = self.operando_mem(tam, m, [])
        rep = ''
        if ext and m == 'rn' and op != 'push' and r.random() < 0.4:
            if r.random() < 0.5:
                rep = f'rpt #{r.randrange(1, 17)} {{ '
            else:
                rc = r.choice([x for x in REGS if x != txt])
                self.pon(rc, self.val())
                rep = f'rpt {rc} {{ '
        self.flags()
        self.asm.append(f'    {rep}{nm} {txt}')
        if op == 'push':
            # Lo apilado, a r15 (y la pila como estaba)
            if tam == 4:
                self.asm.append('    popx.a r15')
            elif tam == 1:
                # MOV.B @SP+ sube el SP en 2 [UG2 POP.B]; el simulador de GDB, en 1:
                # no se usa aquí (la prueba propia de test430 lo mira)
                self.asm.append('    mov.b @r1, r15')
                self.asm.append('    adda #2, r1' if self.cpux else '    add #2, r1')
            else:
                self.asm.append('    pop r15')
        self.guarda()

    def salto(self):
        j = self.r.choice(SALTOS)
        self.asm.append('    mov #0, r15')
        self.flags()
        self.asm.append(f'    {j} 1f')
        self.asm.append('    mov #1, r15')
        self.asm.append('1:')
        self.guarda()

    def rep_ext_f1(self):
        r = self.r
        op = r.choice(['add', 'addc', 'sub', 'subc', 'xor', 'and', 'bis', 'bic', 'mov', 'dadd'])
        suf = r.choice(['.w', '.b'] if op == 'dadd' else ['.a', '.w', '.b'])
        tam = {'.a': 4, '.w': 2, '.b': 1}[suf]
        s, d = r.sample(REGS, 2)
        for x in (s, d):
            self.pon(x, self.bcd({1: 2, 2: 4, 4: 5}[tam]) if op == 'dadd' else self.val())
        if r.random() < 0.5:
            rep = f'rpt #{r.randrange(1, 17)} {{ '
        else:
            rc = r.choice([x for x in REGS if x not in (s, d)])
            self.pon(rc, self.val())
            rep = f'rpt {rc} {{ '
        self.flags(op != 'dadd')
        self.asm.append(f'    {rep}{op}x{suf} {s}, {d}')
        self.guarda()

    def direccion(self):
        r = self.r
        k = r.choice(['mova_r', 'mova_ind', 'mova_inc', 'mova_abs', 'mova_idx', 'mova_r_abs', 'mova_r_idx',
                      'mova_imm', 'cmpa_imm', 'adda_imm', 'suba_imm', 'cmpa_r', 'adda_r', 'suba_r'])
        s, d = r.sample(REGS, 2)
        self.pon(s, self.val())
        self.pon(d, self.val())
        off = r.randrange(0, (ZONA - 4) // 2) * 2
        if k == 'mova_r':
            t = f'mova {s}, {d}'
        elif k in ('mova_ind', 'mova_inc'):
            self.asm.append(f'    mova #zona+{off}, {s}')
            t = f'mova @{s}{"+" if k == "mova_inc" else ""}, {d}'
        elif k == 'mova_abs':
            t = f'mova &zona+{off}, {d}'
        elif k == 'mova_idx':
            self.asm.append(f'    mova #zona+128, {s}')
            t = f'mova {off - 128}({s}), {d}'
        elif k == 'mova_r_abs':
            t = f'mova {s}, &zona+{off}'
        elif k == 'mova_r_idx':
            self.asm.append(f'    mova #zona+128, {d}')
            t = f'mova {s}, {off - 128}({d})'
        elif k.endswith('_imm'):
            t = f'{k[:4]} #0x{self.val(20):05X}, {d}'
        else:
            t = f'{k[:4]} {s}, {d}'
        self.flags()
        self.asm.append('    ' + t)
        self.guarda()

    def rotm(self):
        r = self.r
        op = r.choice(['rrcm', 'rram', 'rlam', 'rrum'])
        suf = r.choice(['.a', '.w'])
        d = r.choice(REGS)
        self.pon(d, self.val())
        self.flags()
        self.asm.append(f'    {op}{suf} #{r.randrange(1, 5)}, {d}')
        self.guarda()

    def pushpop_m(self):
        r = self.r
        suf = r.choice(['.a', '.w'])
        n = r.randrange(1, 6)
        hi = r.randrange(3 + n, 16)               # r(hi-n+1)..r(hi), todos >= 4
        hi2 = r.randrange(3 + n, 16)
        for i in range(hi - n + 1, hi + 1):
            self.pon(f'r{i}', self.val())
        self.flags()
        self.asm.append(f'    pushm{suf} #{n}, r{hi}')
        self.asm.append(f'    popm{suf} #{n}, r{hi2}')
        self.guarda()

    def llamada(self):
        r = self.r
        if self.cpux:
            k = r.choice(['call_imm', 'call_r', 'call_ind', 'calla_imm', 'calla_r', 'calla_ind', 'calla_inc',
                          'calla_abs', 'calla_idx'])
        else:
            k = r.choice(['call_imm', 'call_r', 'call_ind', 'call_inc', 'call_abs', 'call_idx', 'call_sym'])
        p = r.choice(REGS[:-1])
        self.asm.append('    mov #0, r15')
        tab = 'tabla_a' if k.startswith('calla') else 'tabla'
        mv = 'mova' if self.grande or k.startswith('calla') else 'mov'
        ins = 'calla' if k.startswith('calla') else 'call'
        modo = k.split('_')[1]
        dest = 'sub' if (ins == 'calla' or not self.cpux) else 'subw'
        if modo == 'imm':
            t = f'{ins} #{dest}'
        elif modo == 'r':
            self.asm.append(f'    {mv} #{dest}, {p}')
            t = f'{ins} {p}'
        elif modo in ('ind', 'inc'):
            self.asm.append(f'    {mv} #{tab}, {p}')
            t = f'{ins} @{p}' + ('+' if modo == 'inc' else '')
        elif modo == 'abs':
            t = f'{ins} &{tab}'
        elif modo == 'idx':
            # índice positivo: el de GDB no extiende el signo del de CALLA X(Rn)
            self.asm.append(f'    {mv} #{tab}-4, {p}')
            t = f'{ins} 4({p})'
        else:
            t = f'{ins} {tab}'
        self.flags()
        self.asm.append('    ' + t)
        self.guarda()

    def programa(self):
        kinds = [(self.f1, 6), (self.f2, 3), (self.salto, 1), (self.llamada, 0.5)]
        if self.cpux:
            kinds += [(self.direccion, 2), (self.rotm, 1), (self.pushpop_m, 1), (self.rep_ext_f1, 1)]
        tot = sum(w for _, w in kinds)
        for _ in range(self.n):
            self.asm.append(f'; bloque {self.k:04x}')
            x = self.r.random() * tot
            for f, w in kinds:
                x -= w
                if x < 0:
                    f()
                    break
        return self.k

    def fuente(self):
        ancho = 4 if self.cpux else 2
        nreg = 13
        cab = []
        cab.append('    .text')
        cab.append('    .global prueba')
        cab.append('    .global fin')
        cab.append('prueba:')
        if self.cpux:
            cab.append('    pushm.a #7, r10')
        else:
            for i in range(4, 11):
                cab.append(f'    push r{i}')
        cab.append(('    movx.a' if self.grande else '    mov') + ' r1, &sp_guardado')
        if self.grande:
            cab.append('    mova #0xE0000, r1          ; la pila, por encima de 64 KB')
        cab.append(('    movx.a' if self.cpux else '    mov') + ' #res, &resptr')
        # La zona, con un patrón conocido (los dos simuladores parten de lo mismo)
        pie = []
        pie.append(('    movx.a' if self.grande else '    mov') + ' &sp_guardado, r1')
        if self.cpux:
            pie.append('    popm.a #7, r10')
        else:
            for i in range(10, 3, -1):
                pie.append(f'    pop r{i}')
        pie.append('fin:')
        pie.append('    ret' + ('a' if self.grande else ''))
        # guarda: R4-R15 y el SR en *resptr
        g = ['guarda:']
        if self.cpux:
            g.append('    pushx.a r15')
            g.append('    mova &resptr, r15')
            g.append('    mov r2, 48(r15)')
            for i in range(4, 15):
                g.append(f'    movx.a r{i}, {(i - 4) * 4}(r15)')
            g.append('    popx.a 44(r15)')
            g.append('    adda #52, r15')
            g.append('    mova r15, &resptr')
            g.append('    reta' if self.grande else '    ret')
        else:
            g.append('    push r15')
            g.append('    mov &resptr, r15')
            g.append('    mov r2, 24(r15)')
            for i in range(4, 15):
                g.append(f'    mov r{i}, {(i - 4) * 2}(r15)')
            g.append('    mov @r1+, 22(r15)')
            g.append('    add #26, &resptr')
            g.append('    ret')
        sub = ['sub:', '    mov #0x1234, r15', '    ret' + ('a' if (self.cpux) else '')]
        # call (no calla) vuelve con ret; calla, con reta: dos subrutinas
        if self.cpux:
            sub = ['sub:', '    mov #0x1234, r15', '    reta', 'subw:', '    mov #0x1234, r15', '    ret']
        datos = (['    .section .upper.data,"aw"'] if self.grande else ['    .data']) + ['    .balign 4', '    .global zona', 'zona:']
        rz = random.Random(7)
        for i in range(0, ZONA, 16):
            datos.append('    .byte ' + ', '.join(f'0x{rz.randrange(256):02X}' for _ in range(16)))
        datos += ['    .data', '    .balign 4', '    .global zona_ptr', 'zona_ptr:',
                  '    .long zona' if self.cpux else '    .word zona', 'tabla:', '    .word ' + ('subw' if self.cpux else 'sub'), '    .word 0',
                  'tabla_a:', '    .long sub', '    .long 0']
        datos += ['    .balign 4', '    .global res', 'res:', f'    .space {self.k * nreg * ancho}',
                  '    .global resptr', 'resptr:', '    .long 0', 'sp_guardado:', '    .long 0',
                  '    .global n_bloques', 'n_bloques:', f'    .word {self.k}']
        cuerpo = self.asm
        return '\n'.join(cab + cuerpo + pie + g + sub + datos) + '\n'

MAIN_C = r'''
#include <stdint.h>
#include <unistd.h>
extern void prueba(void);
extern uint8_t * const zona_ptr;     /* puede estar por encima de 64 KB (-mlarge) */
extern uint16_t n_bloques;
extern uint8_t res[];
static char buf[160];
static unsigned n;
static void hex(unsigned long v, unsigned d) {
    while (d--) { unsigned x = (unsigned)(v >> (4 * d)) & 15u; buf[n++] = (char)(x < 10 ? '0' + x : 'a' + x - 10); }
}
static void sale(void) { write(1, buf, n); n = 0; }
int main(void) {
    prueba();
    unsigned i, j;
    for (i = 0; i < n_bloques; ++i) {
        hex(i, 4);
        for (j = 0; j < 13; ++j) {
            buf[n++] = ' ';
#if ANCHO == 4
            const uint8_t* p = &res[(i * 13 + j) * 4];
            hex(p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16), 5);
#else
            const uint8_t* p = &res[(i * 13 + j) * 2];
            hex(p[0] | (p[1] << 8), 4);
#endif
        }
        buf[n++] = '\n';
        sale();
    }
    for (i = 0; i < 256; ++i) { hex(zona_ptr[i], 2); if ((i % 32) == 31) { buf[n++] = '\n'; sale(); } }
    return 0;
}
'''

CONFIGS = {
    # nombre: (cpux, grande, flags de gcc, bloques, semillas)
    'cpu':   (False, False, ['-mcpu=msp430'], 450, [11, 12, 13, 14]),
    'cpux':  (True, False, ['-mcpu=msp430x'], 450, [21, 22, 23, 24]),
    'cpuxl': (True, True, ['-mcpu=msp430x', '-mlarge'], 450, [31, 32]),
}

def compila(tmp, nombre, src, cfg):
    cpux, grande, flags, _, _ = cfg
    gcc = herramienta('msp430-elf-gcc')
    s = os.path.join(tmp, nombre + '.S')
    c = os.path.join(tmp, nombre + '_main.c')
    open(s, 'w').write(src)
    open(c, 'w').write(MAIN_C)
    elf = os.path.join(tmp, nombre + '.elf')
    cmd = [gcc] + flags + ['-msim', '-O1', '-Wa,-mY', f'-DANCHO={4 if cpux else 2}', s, c, '-o', elf]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        sys.exit(f'gen_dif: no compila {nombre}:\n{p.stderr[:3000]}')
    return elf

def corre_gdb(elf):
    p = subprocess.run([herramienta('msp430-elf-run'), elf], capture_output=True, text=True, timeout=600)
    return p.stdout, p.returncode

def simbolo(elf, n):
    p = subprocess.run([herramienta('msp430-elf-nm'), elf], capture_output=True, text=True)
    for l in p.stdout.splitlines():
        x = l.split()
        if len(x) == 3 and x[2] == n:
            return int(x[0], 16)
    return None

def corre_mspdebug(elf, nb):
    """La salida que daría el programa según mspdebug sim (solo CPU de 16
    bits): los bloques y la zona, en el mismo formato que imprime main()."""
    if not shutil.which('mspdebug'):
        return None
    fin, res, zona = simbolo(elf, 'fin'), simbolo(elf, 'res'), simbolo(elf, 'zona')
    n = nb * 26
    cmds = ['prog ' + elf, f'setbreak 0x{fin:x}', 'run', f'md 0x{res:x} {n}', f'md 0x{zona:x} 256']
    p = subprocess.run(['mspdebug', '-q', '-n', 'sim'] + cmds, capture_output=True, text=True, timeout=600)
    mem = {}
    for l in p.stdout.splitlines():
        m = re.match(r'\s*([0-9a-f]{4,5}):((?:\s[0-9a-f]{2})+)', l)
        if m:
            base = int(m.group(1), 16)
            for i, b in enumerate(m.group(2).split()[:16]):
                mem[base + i] = int(b, 16)
    if any((res + i) not in mem for i in range(n)) or any((zona + i) not in mem for i in range(256)):
        return None
    lineas = []
    for i in range(nb):
        ws = [mem[res + (i * 13 + j) * 2] | (mem[res + (i * 13 + j) * 2 + 1] << 8) for j in range(13)]
        lineas.append(f'{i:04x} ' + ' '.join(f'{w:04x}' for w in ws))
    for f in range(8):
        lineas.append(''.join(f'{mem[zona + f * 32 + i]:02x}' for i in range(32)))
    return lineas

def arregla_gdb(src, salida):
    """La salida del simulador de GDB, con sus dos erratas de banderas
    corregidas o tapadas (programas de CPUX, donde es el único oráculo):

      * pone V en el bit 7 del SR (no en el 8) y no borra el 8: V pasa al 8;
      * V de DADD (indefinido [UG2]), de RLAM (indefinido [SLAU391F]), de
        ADDA/SUBA/CMPA y SUB/SUBC/CMP (el de GDB lo calcula mal a veces) y de
        RRC/RRCX/RRUX (el de GDB lo pone; la guía dice que se borra): se tapa con una máscara
        (` m=XXXX`) que test430 respeta. Lo comprueban las pruebas propias.
    """
    sal = []
    for l in salida.splitlines():
        x = l.split()
        if len(x) != 14:
            sal.append(l)
            continue
        sr = int(x[13], 16)
        sr = (sr & 0x7F) | ((sr & 0x80) << 1)
        ins = instruccion(src, int(x[0], 16)).lower()
        m = 0x1FF
        if re.search(r'\b(dadd|daddx|rlam|adda|suba|cmpa|rrc|rrcx|rrux|sub|subx|subc|subcx|cmp|cmpx)(\.[abw])?\b', ins):
            m &= ~0x100
            sr &= m
        x[13] = f'{sr:05x}'
        sal.append(' '.join(x) + (f' m={m:x}' if m != 0x1FF else ''))
    return '\n'.join(sal) + '\n'

def instruccion(src, k):
    """El código del bloque k (para el informe)."""
    i = src.find(f'; bloque {k:04x}')
    if i < 0:
        return '?'
    t = []
    for x in src[i:].split('\n')[1:]:
        if x.startswith('; bloque') or 'guarda' in x:
            break
        if not x.startswith('    mov #0x'):
            t.append(x.strip())
    return ' | '.join(t)[:160]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--solo')
    ap.add_argument('--sin-mspdebug', action='store_true')
    ap.add_argument('--ver', type=int, default=0)
    a = ap.parse_args()
    os.makedirs(SAL, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='gen_dif_')
    total_dif = 0
    for nombre_cfg, cfg in CONFIGS.items():
        cpux, grande, flags, nb, semillas = cfg
        for sem in semillas:
            nombre = f'dif_{nombre_cfg}_{sem}'
            if a.solo and a.solo != nombre:
                continue
            g = Gen(sem, cpux, grande, nb)
            k = g.programa()
            src = g.fuente()
            elf = compila(tmp, nombre, src, cfg)
            out, rc = corre_gdb(elf)
            if rc != 0 or not out:
                sys.exit(f'gen_dif: {nombre} en msp430-elf-run: rc={rc}')
            lineas = out.splitlines()
            print(f'{nombre}: {k} bloques, {len(lineas)} líneas de msp430-elf-run')
            esperado = arregla_gdb(src, out) if cpux else out
            if not cpux and not a.sin_mspdebug:
                md = corre_mspdebug(elf, k)
                if md is None:
                    sys.exit('gen_dif: mspdebug sim no da datos; --sin-mspdebug para seguir sin él')
                # La referencia de la CPU de 16 bits es mspdebug: el de GDB tiene
                # erratas conocidas (ver la cabecera); sus diferencias, por clase
                dif = [(x, y) for x, y in zip(lineas, md) if x != y]
                total_dif += len(dif)
                print(f'  mspdebug sim: {len(md) - len(dif)} líneas iguales, {len(dif)} distintas del de GDB')
                for x, y in dif[:a.ver]:
                    k_ = int(x.split()[0], 16) if len(x.split()) > 1 else -1
                    print(f'    {instruccion(src, k_)}\n      gdb      {x}\n      mspdebug {y}')
                esperado = '\n'.join(md) + '\n'
            shutil.copy(elf, os.path.join(SAL, nombre + '.elf'))
            open(os.path.join(SAL, nombre + '.S'), 'w').write(src)
            open(os.path.join(SAL, nombre + '.txt'), 'w').write(esperado)
    print(f'gen_dif: {total_dif} líneas en las que msp430-elf-run y mspdebug sim no coinciden (CPU)')

if __name__ == '__main__':
    main()
