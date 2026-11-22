#!/usr/bin/env python3
# =============================================================================
# gen_msp430.py — Los descriptores de los MSP430 (§5 de doc/analisis_msp430.md)
#
# Cruza cuatro fuentes de TI, que NO se versionan (son de la instalación de
# CCS y de la MSP Debug Stack; ver doc/refs/msp430/):
#
#   1. include_gcc/devices.csv: CPU, MPY, SBW, EEM, disparadores, erratas de
#      CPU y el mapa de memoria;
#   2. las cabeceras de CCS (include/msp430*.h) y de GCC (include_gcc/): los
#      módulos (__MSP430_HAS_*, __MSP430_BASEADDRESS_*), los vectores y, en el
#      _symbols.ld de GCC, la dirección de cada registro;
#   3. el targetdb de CCS (devices/MSP430*.xml y Modules/msp430/*.xml): las
#      mismas propiedades que devices.csv, la lista de módulos y sus registros
#      con dirección absoluta;
#   4. devicedb.h de la MSP Debug Stack (un ZIP de XML dentro de un array de
#      C): el identificador que lee la herramienta, la arquitectura, el EEM, el
#      control de relojes, los eemTimers/eemClocks y otro mapa de memoria.
#
# Cada dato que dan dos fuentes se compara. Una diferencia que no esté en
# RESUELTAS (con su razón) es una discrepancia: se imprime y el descriptor NO
# se genera. Además, el mapa de cada chip se compara con la tabla de §2.2.
#
# Uso:  herramientas/gen_msp430.py [--refs doc/refs/msp430] [--sal src/soc/msp430/desc]
#                                  [--comprueba]   (no escribe; falla si cambiaría algo)
# =============================================================================
import argparse, csv, io, os, re, sys, zipfile
import xml.etree.ElementTree as ET

CHIPS = ['msp430g2553', 'msp430f5529', 'msp430fr2311', 'msp430fr2355', 'msp430fr2433',
         'msp430fr4133', 'msp430fr2476', 'msp430fr5969', 'msp430fr5994', 'msp430fr6989']

# El nombre del dispositivo en devicedb (el G2553 es de la familia G2xx3)
DEVICEDB_NOMBRE = {'msp430g2553': 'MSP430G2xx3'}

# El JTAG ID por familia [SLAU320AJ cap. 3 y anexos; §2.1]
def jtag_id(chip):
    if chip.startswith('msp430g') or chip.startswith('msp430f2') or chip.startswith('msp430f1') \
            or chip.startswith('msp430f4'):
        return 0x89
    if chip.startswith('msp430fr2') or chip.startswith('msp430fr4'):
        return 0x98
    if chip.startswith('msp430fr5') or chip.startswith('msp430fr6'):
        return 0x99
    return 0x91

# --- La tabla de §2.2 (de la hoja de datos de cada chip) ---------------------------
# (memoria principal 1, principal 2, RAM, RAM2/LEA, información, BSL, BSL2, ROM)
TABLA_22 = {
    'msp430g2553':  dict(principal=(0xC000, 0xFFFF), ram=(0x0200, 0x03FF), info=(0x1000, 0x10FF),
                         bsl=(0x0C00, 0x0FFF), vectores=(0xFFC0, 0xFFFF)),
    'msp430f5529':  dict(principal=(0x4400, 0xFFFF), principal2=(0x10000, 0x243FF), ram=(0x2400, 0x43FF),
                         ram_usb=(0x1C00, 0x23FF), info=(0x1800, 0x19FF), bsl=(0x1000, 0x17FF)),
    'msp430fr2311': dict(principal=(0xF100, 0xFFFF), ram=(0x2000, 0x23FF), bsl=(0x1000, 0x17FF),
                         bsl2=(0xFFC00, 0xFFFFF)),
    'msp430fr2355': dict(principal=(0x8000, 0xFFFF), ram=(0x2000, 0x2FFF), info=(0x1800, 0x19FF),
                         bsl=(0x1000, 0x17FF), bsl2=(0xFFC00, 0xFFFFF), rom=(0xFAC00, 0xFFBFF),
                         ram_diminuta=(0x0006, 0x001F)),
    'msp430fr2433': dict(principal=(0xC400, 0xFFFF), ram=(0x2000, 0x2FFF), info=(0x1800, 0x19FF),
                         bsl=(0x1000, 0x17FF), bsl2=(0xFFC00, 0xFFFFF)),
    'msp430fr4133': dict(principal=(0xC400, 0xFFFF), ram=(0x2000, 0x27FF), info=(0x1800, 0x19FF),
                         bsl=(0x1000, 0x13FF)),
    'msp430fr2476': dict(principal=(0x8000, 0xFFFF), principal2=(0x10000, 0x17FFF), ram=(0x2000, 0x3FFF),
                         info=(0x1800, 0x19FF), bsl=(0x1000, 0x17FF), bsl2=(0xFFC00, 0xFFFFF),
                         ram_diminuta=(0x0006, 0x001F)),
    'msp430fr5969': dict(principal=(0x4400, 0xFFFF), principal2=(0x10000, 0x13FFF), ram=(0x1C00, 0x23FF),
                         info=(0x1800, 0x19FF), bsl=(0x1000, 0x17FF)),
    'msp430fr5994': dict(principal=(0x4000, 0xFFFF), principal2=(0x10000, 0x43FFF), ram=(0x1C00, 0x2BFF),
                         ram_lea=(0x2C00, 0x3BFF), info=(0x1800, 0x19FF), bsl=(0x1000, 0x17FF),
                         ram_diminuta=(0x000A, 0x001F)),
    'msp430fr6989': dict(principal=(0x4400, 0xFFFF), principal2=(0x10000, 0x23FFF), ram=(0x1C00, 0x23FF),
                         info=(0x1800, 0x19FF), bsl=(0x1000, 0x17FF), ram_diminuta=(0x0006, 0x001F)),
}

# Lo que no está en devices.csv y sí en la hoja de datos (se añade al descriptor)
A_MANO = {
    'msp430g2553':  dict(bsl=((0x0C00, 0x0FFF), 'slas735j «Memory Organization»: BSL en ROM; devices.csv lo deja a 0'),
                         vectores=((0xFFC0, 0xFFFF), 'slas735j: el área de vectores es FFC0-FFFF; devices.csv da '
                                   'FFE0-FFFF, los 16 que usa el G2553')),
    'msp430fr2311': dict(bsl2=((0xFFC00, 0xFFFFF), 'slas845: BSL2 en ROM')),
    'msp430fr2355': dict(bsl2=((0xFFC00, 0xFFFFF), 'slasec4: BSL2 en ROM'),
                         rom=((0xFAC00, 0xFFBFF), 'slasec4: ROM de DriverLib y FFT')),
    'msp430fr2433': dict(bsl2=((0xFFC00, 0xFFFFF), 'slase59: BSL2 en ROM')),
    'msp430fr2476': dict(bsl2=((0xFFC00, 0xFFFFF), 'slasep5: BSL2 en ROM')),
}

# Diferencias entre fuentes que una persona ha mirado y resuelto: (chip o '*', clave) -> razón
RESUELTAS = {
    ('*', 'mapa-devicedb:Main'): 'devicedb cuenta la memoria principal hasta 0xFFFF (vectores y firma '
                                 'incluidos) y, en las de 20 bits, la une a la de arriba; devices.csv la parte',
    ('msp430g2553', 'bsl-csv'): 'el BSL de ROM del 2xx no está en devices.csv; sí en devicedb (0x0C00, '
                                '0x400) y en la hoja de datos',
    ('msp430g2553', 'modulo:SFR'): 'las cabeceras de 2xx no declaran __MSP430_HAS_SFR__; IE1, IE2, IFG1 e '
                                   'IFG2 están en 0x0000-0x0003 [slas735j, «Special Function Registers»]',
    ('msp430g2553', 'vectores-22'): 'ver A_MANO: el área es FFC0-FFFF; devices.csv da los 16 usados',
}

# --- Utilidades ------------------------------------------------------------------
class Informe:
    def __init__(self):
        self.lineas, self.pendientes = [], 0
    def ok(self, chip, txt):
        self.lineas.append(f'  {chip:14s} {txt}')
    def dif(self, chip, clave, txt):
        r = RESUELTAS.get((chip, clave)) or RESUELTAS.get(('*', clave))
        if r:
            self.lineas.append(f'  {chip:14s} RESUELTA {clave}: {txt} -- {r}')
        else:
            self.lineas.append(f'  {chip:14s} DISCREPANCIA {clave}: {txt}')
            self.pendientes += 1

def hx(s):
    s = (s or '').strip()
    if not s:
        return 0
    return int(s, 16)

# --- 1. devices.csv --------------------------------------------------------------
def lee_csv(ruta):
    filas, cab, version = {}, None, '?'
    with open(ruta, newline='') as f:
        for r in csv.reader(f):
            if not r:
                continue
            if r[0].startswith('Version:'):
                version = r[1]
            if r[0].startswith('# Device Name'):
                cab = [c.strip() for c in r]
                cab[0] = 'Device Name'
                continue
            if cab and r[0] and not r[0].startswith('#'):
                filas[r[0].strip().lower()] = dict(zip(cab, [c.strip() for c in r]))
    return filas, version

ERRATAS = ['CPU4', 'CPU7', 'CPU8', 'CPU11', 'CPU12', 'CPU13', 'CPU18', 'CPU19', 'CPU20', 'CPU21', 'CPU22',
           'CPU23', 'CPU24', 'CPU25', 'CPU26', 'CPU27', 'CPU28', 'CPU29', 'CPU30', 'CPU31', 'CPU32', 'CPU33',
           'CPU34', 'CPU35', 'CPU39', 'CPU40', 'CPU41', 'CPU42', 'CPU43', 'CPU44', 'CPU45', 'CPU46']

def region_csv(f, a, b):
    i, e = hx(f[a]), hx(f[b])
    return (i, e) if (i or e) else (0, 0)

def mapa_csv(f):
    m = dict(ram=region_csv(f, 'RAMStart', 'RAMEnd'), ram2=region_csv(f, 'RAMStart2', 'RAMEnd2'),
             ram_diminuta=region_csv(f, 'TINYRAMStart', 'TINYRAMEnd'),
             ram_usb=region_csv(f, 'USBRAMStart', 'USBRAMEnd'), ram_lea=region_csv(f, 'LEARAMStart', 'LEARAMEnd'),
             bsl=region_csv(f, 'BSLStart', 'BSLEnd'), info=region_csv(f, 'INFOStart', 'INFOEnd'),
             principal=region_csv(f, 'FStart', 'FEnd'), principal2=region_csv(f, 'FStart2', 'FEnd2'),
             vectores=region_csv(f, 'INTStart', 'INTEnd'))
    # Los segmentos A-D: de su inicio al siguiente (o al final de la información)
    tam_seg = 0
    ini = [hx(f[k]) for k in ('INFOA', 'INFOB', 'INFOC', 'INFOD')]
    if m['info'][1] and all(ini):
        tam_seg = ini[0] - ini[1]
    for k, i in zip('abcd', ini):
        m['info_' + k] = (i, i + tam_seg - 1) if (i and tam_seg) else (0, 0)
    fs, ft = hx(f['Signature_Start']), hx(f['Signature_Size'] or '0')
    m['firma'] = (fs, fs + ft - 1) if fs and ft else (0, 0)
    return m

# --- 2. Cabeceras ----------------------------------------------------------------
RE_HAS = re.compile(r'#define\s+__MSP430_HAS_(\w+?)__(?:\s+\d+)?\s+/\*\s*Definition to show', re.I)
RE_BASE = re.compile(r'#define\s+__MSP430_BASEADDRESS_(\w+?)__\s+(0x[0-9A-Fa-f]+)')
RE_VEC_CCS = re.compile(r'#define\s+(\w+)_VECTOR\s+\((\d+)\s*\*\s*1u\)\s*/\*\s*0x([0-9A-Fa-f]{4})')
RE_VEC_GCC = re.compile(r'#define\s+(\w+)_VECTOR\s+\(\s*(\d+|"reset")\s*\)\s*/\*\s*0x([0-9A-Fa-f]{4})')
RE_SYM = re.compile(r'PROVIDE\(\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*\)')

def lee_cabeceras(ti, chip):
    ccs = open(os.path.join(ti, 'msp430', 'include', chip + '.h'), errors='replace').read()
    gcc = open(os.path.join(ti, 'msp430', 'include_gcc', chip + '.h'), errors='replace').read()
    sym = open(os.path.join(ti, 'msp430', 'include_gcc', chip + '_symbols.ld'), errors='replace').read()
    has = [m for m in RE_HAS.findall(ccs)]
    base = {n: int(v, 16) for n, v in RE_BASE.findall(ccs)}
    vccs = {n: (int(i), int(a, 16)) for n, i, a in RE_VEC_CCS.findall(ccs)}
    vgcc = {n: (0 if i.startswith('"') else int(i), int(a, 16)) for n, i, a in RE_VEC_GCC.findall(gcc)}
    simbolos = {n: int(v, 16) for n, v in RE_SYM.findall(sym)}
    return has, base, vccs, vgcc, simbolos

# --- 3. targetdb -----------------------------------------------------------------
def lee_targetdb(ti, chip):
    ruta = os.path.join(ti, 'targetdb', 'devices', chip.upper() + '.xml')
    raiz = ET.parse(ruta).getroot()
    props = {p.get('id'): p.get('Value') for p in raiz.iter('property')}
    mods, regs = [], []
    for inst in raiz.iter('instance'):
        href = inst.get('href') or ''
        if 'Modules' not in href:
            continue
        ident = inst.get('id') or ''
        base = int(inst.get('baseaddr') or '0', 16)
        xml = os.path.join(ti, 'targetdb', 'Modules', 'msp430', os.path.basename(href))
        m = ET.parse(xml).getroot()
        rs = []
        for r in m.iter('register'):
            nombre = r.get('acronym') or r.get('id')
            off = int((r.get('offset') or '0').strip(), 16)
            rs.append((nombre, base + off, int(r.get('width') or '16')))
        mods.append((ident, rs))
        regs += rs
    return props, mods, regs

def xml_tolerante(s):
    """Algún XML de devicedb repite un atributo en una etiqueta: se queda el primero."""
    try:
        return ET.fromstring(s)
    except ET.ParseError:
        def limpia(m):
            vistos, partes = set(), []
            for a in re.finditer(r'\s+([\w:-]+)\s*=\s*("[^"]*"|\'[^\']*\')', m.group(2)):
                if a.group(1) not in vistos:
                    vistos.add(a.group(1))
                    partes.append(a.group(0))
            return '<' + m.group(1) + ''.join(partes) + m.group(3) + '>'
        return ET.fromstring(re.sub(r'<([\w:-]+)((?:\s+[\w:-]+\s*=\s*(?:"[^"]*"|\'[^\']*\'))*)\s*(/?)>', limpia, s))

# --- 4. devicedb.h ---------------------------------------------------------------
class DeviceDb:
    def __init__(self, ruta_h):
        s = open(ruta_h, errors='replace').read()
        i = s.index('g_database[] = {')
        j = s.index('};', i)
        b = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', s[i:j]))
        z = zipfile.ZipFile(io.BytesIO(b))
        self.raices = [xml_tolerante(z.read(n).decode('utf-8', 'replace'))
                       for n in z.namelist() if n.endswith('.xml')]
        self.por_id = {}
        for r in self.raices:
            for e in r.iter():
                if e.get('id'):
                    self.por_id.setdefault((e.tag, e.get('id')), e)

    def resuelve(self, e):
        """Un elemento con todas sus herencias (ref) aplicadas: dict anidado."""
        base = None
        if e.get('ref') is not None and (e.tag, e.get('ref')) in self.por_id:
            base = self.resuelve(self.por_id[(e.tag, e.get('ref'))])
        propio = {'_txt': (e.text or '').strip(), '_hijos': {}}
        for h in e:
            if not isinstance(h.tag, str):
                continue
            clave = (h.tag, h.get('name') or h.get('index') or '')
            propio['_hijos'][clave] = self.resuelve(h)
        return self.mezcla(base, propio) if base else propio

    def mezcla(self, a, b):
        r = {'_txt': b['_txt'] or a['_txt'], '_hijos': dict(a['_hijos'])}
        for k, v in b['_hijos'].items():
            r['_hijos'][k] = self.mezcla(r['_hijos'][k], v) if k in r['_hijos'] else v
        return r

    def dispositivo(self, descripcion):
        for r in self.raices:
            for d in r.iter('device'):
                ds = d.find('description')
                if ds is not None and (ds.text or '').strip() == descripcion:
                    return self.resuelve(d)
        return None

def hijo(d, *ruta):
    for t in ruta:
        if d is None:
            return None
        cand = [v for (tag, _), v in d['_hijos'].items() if tag == t]
        d = cand[0] if cand else None
    return d

def txt(d, *ruta):
    h = hijo(d, *ruta)
    return h['_txt'] if h else ''

def hijos(d, tag):
    return [(k[1], v) for k, v in d['_hijos'].items() if k[0] == tag] if d else []

# --- Normalización de nombres (§2.3) ---------------------------------------------
IGNORA_CAB = re.compile(r'(SEL\d?|_PRESENT|_CPU|DIO|TLV|_CONFIG|^DATA_FRWP|^FRWPPW|^GC|^LCP_POWER_PIN|^ADC_PCTLSEL)$')
NORM_CAB = {
    'FRAM': 'FRCTL', 'FRCTL_A': 'FRCTL', 'PMM_FRAM': 'PMM', 'PMM_FR5xx': 'PMM', 'COMPB': 'COMP_B',
    'ADC12_PLUS': 'ADC12_A', 'BC2': 'BCS', 'CAPLUS': 'COMP_A', 'RC': 'RAMCTL', 'PORT_MAPPING': 'PMAP',
    'PORT4_MAPPING': 'PMAP4', 'WDT': 'WDT', 'FLASH': 'FLASH', 'FLASH2': 'FLASH2', 'TA3': 'TA0',
    'CAPTIO': 'CAPTIO0', 'CRC32': 'CRC32', 'LCD_E': 'LCD_E', 'LCD_C': 'LCD_C', 'BACKUP_RAM': 'BKMEM',
    'ADC': 'ADC', 'RC_FRAM': 'RAMCTL', 'USCI': 'USCI_A0+USCI_B0', 'USB': 'USB',
}

def norm_cab(n):
    if IGNORA_CAB.search(n) and n not in ('DIO',):
        return []
    if n == 'DIO' or n.endswith('_R') and re.match(r'PORT[A-F]_R$', n):
        return []                                    # los pares PA/PB...: ya están P1/P2...
    if n in NORM_CAB:
        return NORM_CAB[n].split('+')
    m = re.match(r'PORT(\d+|J)_R$', n)
    if m:
        return ['P' + m.group(1)]
    m = re.match(r'T(\d)([AB])(\d)$', n)
    if m:
        return [f'T{m.group(2)}{m.group(1)}']
    m = re.match(r'T([AB])(\d)$', n)
    if m:
        return [f'T{m.group(1)}{m.group(2)}']
    m = re.match(r'CAP_TOUCH_IO_(\d)$', n)
    if m:
        return ['CAPTIO' + m.group(1)]
    m = re.match(r'CAPTIO(\d)$', n)
    if m:
        return ['CAPTIO' + m.group(1)]
    m = re.match(r'(DMAX)_\d+$', n)
    if m:
        return ['DMA']
    return [n]

def norm_tdb(n, cab):
    """Un id del targetdb a nombres normalizados; `cab` es el conjunto de la cabecera."""
    n = re.sub(r'__.*$', '', n)                       # USCI_A0__UART_Mode, CS__Clock_System
    if n in ('MSP430_NotVisible', 'Calibration_Data', 'TLV_Calibration_Data', 'TLV', ''):
        return []
    m = re.match(r'Timer(\d)_([AB])(\d)$', n)
    if m:
        return [f'T{m.group(2)}{m.group(1)}']
    if re.match(r'Port_[\dJ_]+$', n) or re.match(r'P(\d+|J)$', n):
        return ['@PUERTOS']                          # de sus registros (PnIN)
    if re.match(r'(Port_|P)[A-F]$', n):
        return []
    m = re.match(r'(e?)USCI_([AB]\d)$', n)
    if m:
        e = 'EUSCI_' + m.group(2)
        return [e] if e in cab else ['USCI_' + m.group(2)]
    m = re.match(r'Capacitive_Touch_IO_(\d)$', n)
    if m:
        return ['CAPTIO' + m.group(1)]
    tabla = {
        'ADC12': [x for x in ('ADC12_A', 'ADC12_B') if x in cab] or ['ADC12'],
        'Comparator_E': ['COMP_E'], 'Comparator_B': ['COMP_B'], 'Comparator_A': ['COMP_A'],
        'CRC16': ['CRC'], 'CRC': ['CRC'], 'AES_Accelerator': ['AES256'], 'CS': ['CS'], 'DMA': ['DMA'],
        'FRAM': ['FRCTL'], 'FRCTL': ['FRCTL'], 'MPU': ['MPU'], 'MPY_16': ['MPY32'], 'MPY_32': ['MPY32'],
        'PMM': ['PMM'], 'Shared_Reference': [x for x in ('REF_A', 'REF') if x in cab] or ['REF'],
        'RTC_B': ['RTC_B'], 'RTC_C': ['RTC_C'], 'RTC': ['RTC'], 'SFR': ['SFR'], 'SYS': ['SYS'],
        'Watchdog_Timer': [x for x in ('WDT_A', 'WDT') if x in cab] or ['WDT'],
        'Special_Function': ['SFR'], 'System_Clock': ['BCS'],
        'Flash': [x for x in ('FLASH2', 'FLASH') if x in cab] or ['FLASH'],
        'UCS': ['UCS'], 'RC': ['RAMCTL'], 'USB_Control': ['USB'], 'USB_Operation': ['USB'],
        'Port_Mapping_Control': ['PMAP'], 'Port_Mapping_Port_4': ['PMAP4'],
        'ADC10': ['ADC10'], 'LCD_E': ['LCD_E'], 'LCD_C': ['LCD_C'], 'ESI': ['ESI'], 'LEA': ['LEA'],
        'RAMCTL': ['RAMCTL'], 'Backup_Memory': ['BKMEM'], 'ExtScanIF': ['ESI'], 'ExtScanIF_RAM': ['ESI_RAM'], 'Real-Time_Clock': ['RTC'],
    }
    if n in tabla:
        return tabla[n]
    return [x.upper() for x in norm_cab(n)]

# --- Generación ------------------------------------------------------------------
def c_reg(r):
    return '{0x%X, 0x%X}' % r if r != (0, 0) else '{}'

def genera_chip(chip, refs, filas, ddb, inf, versiones):
    ti = os.path.join(refs, 'ti')
    f = filas[chip]
    mapa = mapa_csv(f)
    has, base_cab, vccs, vgcc, simbolos = lee_cabeceras(ti, chip)
    props, mods_tdb, regs_tdb = lee_targetdb(ti, chip)
    dd = ddb.dispositivo(DEVICEDB_NOMBRE.get(chip, chip.upper()))
    if dd is None:
        inf.dif(chip, 'devicedb', 'el dispositivo no está en devicedb')
        return None

    # --- CPU, EEM, SBW, relojes: csv / targetdb / devicedb
    arq = int(f['CPU_TYPE'])
    arq_dd = {'Cpu': 0, 'CpuX': 1, 'CpuXv2': 2}.get(txt(dd, 'architecture'), -1)
    (inf.ok if arq == arq_dd else lambda c, t: inf.dif(c, 'arq', t))(chip, f'CPU_TYPE {arq} = devicedb {txt(dd, "architecture")}')
    eem = f['EEM']
    for k, v in (('eem', props.get('eem')), ('devicedb', txt(dd, 'eem'))):
        (inf.ok if v == eem else lambda c, t: inf.dif(c, 'eem-' + k, t))(chip, f'EEM {eem} = {k} {v}')
    bp = int(f['BREAKPOINTS'])
    (inf.ok if str(bp) == props.get('breakpoints') else lambda c, t: inf.dif(c, 'breakpoints', t))(
        chip, f'BREAKPOINTS {bp} = targetdb {props.get("breakpoints")}')
    cc = f['CLOCKCONTROL']
    cc_dd = txt(dd, 'clockInfo', 'clockControl')
    cc_ok = cc == props.get('clockcontrol') and cc_dd.startswith('GCC_' + cc)
    (inf.ok if cc_ok else lambda c, t: inf.dif(c, 'clockcontrol', t))(
        chip, f'CLOCKCONTROL {cc} = targetdb {props.get("clockcontrol")} ~ devicedb {cc_dd}')
    sbw = int(f['SBW']) != 0
    (inf.ok if sbw == (int(props.get('spybiwire') or '0') != 0) else lambda c, t: inf.dif(c, 'sbw', t))(
        chip, f'SBW {f["SBW"]} ~ targetdb {props.get("spybiwire")}')
    ivb = int(props.get('iv_base', '0'), 16)
    (inf.ok if ivb == mapa['vectores'][0] else lambda c, t: inf.dif(c, 'iv_base', t))(
        chip, f'INTStart 0x{mapa["vectores"][0]:X} = targetdb iv_base 0x{ivb:X}')

    # --- El mapa: devicedb
    for nombre, clave in (('Main', 'principal'), ('Ram', 'ram'), ('Info', 'info'), ('Bsl', 'bsl')):
        md = [v for (t, n), v in dd['_hijos'].get(('memoryLayout', ''), {'_hijos': {}})['_hijos'].items()
              if t == 'memory' and n == nombre]
        if not md:
            if mapa[clave] != (0, 0):
                inf.dif(chip, 'mapa-devicedb:' + nombre, f'devicedb no tiene {nombre}')
            continue
        ini, tam = int(txt(md[0], 'start') or '0', 0), int(txt(md[0], 'size') or '0', 0)
        r_dd = (ini, ini + tam - 1)
        r_csv = mapa[clave]
        if clave == 'bsl' and r_csv == (0, 0):
            inf.dif(chip, 'bsl-csv', f'devices.csv sin BSL; devicedb 0x{r_dd[0]:X}-0x{r_dd[1]:X}')
        elif r_dd == r_csv:
            inf.ok(chip, f'{nombre} 0x{r_csv[0]:X}-0x{r_csv[1]:X} = devicedb')
        else:
            inf.dif(chip, 'mapa-devicedb:' + nombre,
                    f'csv 0x{r_csv[0]:X}-0x{r_csv[1]:X} / devicedb 0x{r_dd[0]:X}-0x{r_dd[1]:X}')

    # --- A mano y la tabla de §2.2
    mapa.setdefault('bsl2', (0, 0))
    mapa.setdefault('rom', (0, 0))
    for k, (r, razon) in A_MANO.get(chip, {}).items():
        mapa[k] = r
        inf.ok(chip, f'{k} 0x{r[0]:X}-0x{r[1]:X} a mano: {razon}')
    for k, esperado in TABLA_22[chip].items():
        if k == 'principal':
            # La memoria principal de §2.2 llega hasta 0xFFFF: la de devices.csv más firma y vectores
            obtenido = (mapa['principal'][0], max(mapa['principal'][1], mapa['vectores'][1]))
        else:
            obtenido = mapa[k]
        if obtenido == esperado:
            inf.ok(chip, f'§2.2 {k} 0x{esperado[0]:X}-0x{esperado[1]:X}')
        else:
            inf.dif(chip, k + '-22', f'§2.2 0x{esperado[0]:X}-0x{esperado[1]:X} / obtenido '
                    f'0x{obtenido[0]:X}-0x{obtenido[1]:X}')

    # --- Los módulos: cabecera frente a targetdb
    cab = {}
    for h in has:
        for n in norm_cab(h):
            cab.setdefault(n, h)
    tdb = {}
    for ident, rs in mods_tdb:
        for n in norm_tdb(ident, set(cab)):
            if n == '@PUERTOS':
                for nombre, dirr, bits in rs:
                    m = re.match(r'P(\d+|J)IN$', nombre)
                    if m:
                        tdb.setdefault('P' + m.group(1), []).extend(
                            r for r in rs if re.match(r'P' + m.group(1) + r'[A-Z]', r[0]))
                continue
            tdb.setdefault(n, []).extend(rs)
    for n in sorted(set(cab) | set(tdb)):
        if n in cab and n in tdb:
            continue
        if n in cab:
            inf.dif(chip, 'modulo:' + n, f'{n} ({cab[n]}) en la cabecera y no en el targetdb')
        else:
            inf.dif(chip, 'modulo:' + n, f'{n} en el targetdb y no en la cabecera')
    modulos = []
    for n in sorted(set(cab) | set(tdb)):
        h = cab.get(n, '')
        if h in base_cab:
            b = base_cab[h]
        elif n in tdb and tdb[n]:
            # Sin __MSP430_BASEADDRESS_ (2xx): el registro más bajo, sin el IV (el
            # TA0IV del G2553 está en 0x012E y su TA0CTL en 0x0160)
            sin_iv = [r[1] for r in tdb[n] if not r[0].endswith('IV')] or [r[1] for r in tdb[n]]
            b = min(sin_iv)
        else:
            b = 0
        modulos.append((n, h, b))
    inf.ok(chip, f'{len(modulos)} módulos')

    # --- Los registros: targetdb frente a _symbols.ld de GCC
    malos, vistos, regs = 0, set(), []
    for nombre, dirr, bits in sorted(regs_tdb, key=lambda r: (r[1], r[0])):
        if (nombre, dirr) in vistos:
            continue
        vistos.add((nombre, dirr))
        regs.append((nombre, dirr, bits))
        if nombre in simbolos and simbolos[nombre] != dirr:
            malos += 1
            inf.dif(chip, 'registro:' + nombre, f'targetdb 0x{dirr:X} / GCC 0x{simbolos[nombre]:X}')
    comunes = sum(1 for n, _, _ in regs if n in simbolos)
    inf.ok(chip, f'{len(regs)} registros del targetdb; {comunes} contrastados con GCC, {malos} distintos')

    # --- Los vectores: CCS frente a GCC, y dentro del área
    vecs = []
    # La dirección de cada vector sale de su número (RESET, el último, en
    # 0xFFFE); la del comentario de la cabecera se contrasta con ella: en las
    # de TI del FR2433 (y otras FR2xx) TIMER0_A0 dice 0xFFE8, la del RTC, y
    # es 0xFFF8 [DS SLASE59F, tabla «Interrupt Sources»]
    if 'RESET' in vccs:
        ir = vccs['RESET'][0]
        for n, (i, a) in list(vccs.items()):
            de_num = 0xFFFE - 2 * (ir - i)
            if de_num != a:
                inf.ok(chip, f'RESUELTA vector-comentario:{n}: la cabecera dice 0x{a:X} y su número ({i}) da '
                             f'0x{de_num:X} -- vale el número')
                vccs[n] = (i, de_num)
                if n in vgcc:
                    vgcc[n] = (vgcc[n][0], de_num)
    for n, (i, a) in sorted(vccs.items(), key=lambda x: x[1][1]):
        if n not in vgcc:
            inf.dif(chip, 'vector:' + n, f'{n} en CCS y no en GCC')
        elif vgcc[n][1] != a:
            inf.dif(chip, 'vector:' + n, f'{n} CCS 0x{a:X} / GCC 0x{vgcc[n][1]:X}')
        if not (mapa['vectores'][0] <= a <= mapa['vectores'][1]):
            inf.dif(chip, 'vector-fuera:' + n, f'{n} 0x{a:X} fuera de los vectores')
        vecs.append((n, a, i))
    inf.ok(chip, f'{len(vecs)} vectores, iguales en CCS y GCC')

    # --- devicedb: id, eemTimers, eemClocks
    idv = int(txt(dd, 'idCode', 'version') or '0', 16)
    timers = []
    for idx, v in hijos(hijo(dd, 'clockInfo', 'eemTimers'), 'eemTimer'):
        timers.append((int(idx), txt(v, 'name'), int(txt(v, 'value') or '0', 16), txt(v, 'defaultStop') == 'true'))
    timers.sort()
    clocks = sorted((int(idx), v['_txt']) for idx, v in hijos(hijo(dd, 'clockInfo', 'eemClocks'), 'eemClock'))
    inf.ok(chip, f'devicedb: id 0x{idv:04X}, {len(timers)} eemTimers, {len(clocks)} eemClocks')

    errata = [e.strip() for e in f['CPU_Bugs'].split(';') if e.strip() and e.strip() != '0']
    for e in errata:
        if e not in ERRATAS:
            inf.dif(chip, 'errata:' + e, f'errata {e} sin bit en desc430.h')
    tam_eem = {'EMEX_LOW': 'XS', 'EMEX_MEDIUM': 'M', 'EMEX_HIGH': 'L', 'EMEX_EXTRA_SMALL_5XX': 'XS',
               'EMEX_SMALL_5XX': 'S', 'EMEX_MEDIUM_5XX': 'M', 'EMEX_LARGE_5XX': 'L'}.get(eem, 'Ninguno')
    mpy = {0: 'Ninguno', 1: 'Mpy16', 2: 'Mpy16Mac', 8: 'Mpy32'}[int(f['MPY_TYPE'])]

    # --- El fichero
    o = []
    o.append('// ' + '=' * 77)
    o.append(f'// {chip}.h — Descriptor de {chip.upper()}. GENERADO por herramientas/gen_msp430.py:')
    o.append('// no se edita a mano. Fuentes:')
    for v in versiones:
        o.append(f'//   {v}')
    o.append('// ' + '=' * 77)
    g = 'STM32_SOC_MSP430_DESC_' + chip.upper() + '_H'
    o += [f'#ifndef {g}', f'#define {g}', '', '#include "desc430.h"', '',
          'namespace stm32 { namespace m430 { namespace ' + chip + ' {', '']
    o.append('inline constexpr Modulo MODULOS[] = {')
    for n, h, b in modulos:
        o.append(f'    {{"{n}", "{h}", 0x{b:04X}}},')
    o.append('};')
    o.append('inline constexpr Vector VECTORES[] = {')
    for n, a, i in vecs:
        o.append(f'    {{"{n}", 0x{a:04X}, {i}}},')
    o.append('};')
    o.append('inline constexpr Registro REGISTROS[] = {')
    for n, a, b in regs:
        o.append(f'    {{"{n}", 0x{a:05X}, {b}}},')
    o.append('};')
    o.append('inline constexpr EemTimer EEM_TIMERS[] = {')
    for i, n, v, p in timers:
        o.append(f'    {{{i}, "{n}", 0x{v:02X}, {"true" if p else "false"}}},')
    if not timers:
        o.append('    {0, "", 0, false},')
    o.append('};')
    o.append('inline constexpr EemClock EEM_CLOCKS[] = {')
    for i, n in clocks:
        o.append(f'    {{{i}, "{n}"}},')
    if not clocks:
        o.append('    {0, ""},')
    o.append('};')
    o.append('')
    o.append('inline constexpr Desc430 DESC = {')
    o.append(f'    "{chip}", Arq::{["CPU", "CPUX", "CPUXV2"][arq]},')
    o.append('    ' + (' | '.join('ERR_' + e for e in errata) if errata else '0') + ',')
    o.append(f'    Mpy::{mpy}, {"true" if sbw else "false"}, TamEem::{tam_eem}, "{eem}", {bp},')
    o.append(f'    {"true" if cc == "EXTENDED" else "false"}, {int(f["CYCLECOUNTER"])}, {int(f["STACKSIZE"])},')
    o.append(f'    0x{jtag_id(chip):02X}, 0x{idv:04X}, 0x{ivb:04X},')
    o.append('    ' + ', '.join(c_reg(mapa[k]) for k in ('ram', 'ram2', 'ram_diminuta', 'ram_usb', 'ram_lea')) + ',')
    o.append('    ' + ', '.join(c_reg(mapa[k]) for k in ('bsl', 'info', 'info_a', 'info_b', 'info_c', 'info_d')) + ',')
    o.append('    ' + ', '.join(c_reg(mapa[k]) for k in ('principal', 'principal2', 'firma', 'vectores')) + ',')
    o.append('    ' + ', '.join(c_reg(mapa[k]) for k in ('bsl2', 'rom')) + ',')
    o.append(f'    MODULOS, {len(modulos)}, VECTORES, {len(vecs)}, REGISTROS, {len(regs)},')
    o.append(f'    EEM_TIMERS, {len(timers)}, EEM_CLOCKS, {len(clocks)},')
    o.append('};')
    o.append('')
    o.append('}}} // namespace stm32::m430::' + chip)
    o.append(f'#endif // {g}')
    return '\n'.join(o) + '\n'

def main():
    ap = argparse.ArgumentParser()
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument('--refs', default=os.path.join(raiz, 'doc', 'refs', 'msp430'))
    ap.add_argument('--sal', default=os.path.join(raiz, 'src', 'soc', 'msp430', 'desc'))
    ap.add_argument('--comprueba', action='store_true')
    a = ap.parse_args()
    ti = os.path.join(a.refs, 'ti')
    filas, ver_csv = lee_csv(os.path.join(ti, 'msp430', 'include_gcc', 'devices.csv'))
    ruta_ddb = None
    for d, _, fs in os.walk(a.refs):
        if 'devicedb.h' in fs:
            ruta_ddb = os.path.join(d, 'devicedb.h')
            break
    if not ruta_ddb:
        sys.exit('gen_msp430: falta devicedb.h (MSPDebugStack_OS_Package) en ' + a.refs)
    ddb = DeviceDb(ruta_ddb)
    mspds = re.search(r'MSPDebugStack_OS_Package_([\d_]+)', ruta_ddb)
    versiones = [f'devices.csv {ver_csv}', 'cabeceras de CCS y GCC 1.213 (__MSP430_HEADER_VERSION__ 1213)',
                 'targetdb de CCS 11.2 (ccs1120)',
                 f'devicedb.h de la MSP Debug Stack {mspds.group(1).replace("_", ".") if mspds else "?"}']
    inf = Informe()
    salidas = {}
    for chip in CHIPS:
        s = genera_chip(chip, a.refs, filas, ddb, inf, versiones)
        if s:
            salidas[chip + '.h'] = s
    print('gen_msp430: informe de las cuatro fuentes')
    print('\n'.join(inf.lineas))
    if inf.pendientes:
        print(f'gen_msp430: {inf.pendientes} discrepancias sin resolver; no se genera nada')
        return 1
    informe = ('# Informe de gen_msp430.py (generado; no se edita)\n#   ' + '\n#   '.join(versiones) + '\n'
               + '\n'.join(inf.lineas) + '\n')
    salidas['INFORME.txt'] = informe
    cambios = 0
    for n, s in salidas.items():
        p = os.path.join(a.sal, n)
        viejo = open(p).read() if os.path.exists(p) else None
        if viejo != s:
            cambios += 1
            if not a.comprueba:
                os.makedirs(a.sal, exist_ok=True)
                open(p, 'w').write(s)
    print(f'gen_msp430: {len(salidas) - 1} descriptores, {cambios} ficheros '
          + ('distintos' if a.comprueba else 'escritos'))
    return 1 if (a.comprueba and cambios) else 0

if __name__ == '__main__':
    sys.exit(main())
