#!/usr/bin/env python3
# =============================================================================
# extrae_ti.py — Transcribe el patillaje de los MSP430 y los MSP432 de sus
# datasheets a biblioteca/fuentes/ti/TIPO.pines (fase B4b de
# doc/analisis_build.md)
#
#   python3 extrae_ti.py REFS [--cache DIR] [TIPO...]
#
# REFS es la carpeta doc/refs de mcu-sim (sin versionar: los datasheets son
# de TI). Por cada chip de CHIPS se leen dos tablas del datasheet:
#
#   * la de PATILLAS -«Pin Attributes», o «Terminal Functions» / «Signal
#     Descriptions» en los más viejos-: la posición de cada patilla en el
#     encapsulado que usa mcu-sim, su nombre y sus señales;
#   * las de FUNCIONES -«Port Px Pin Functions»-: por cada pad, cada función
#     y los bits que la eligen (PxSEL, PxSEL2, PxSEL1/PxSEL0, PxMAP). De ahí
#     sale el MODO de cada función, el mismo número que usa el modelo:
#       MSP430G2              PxSEL + 2·PxSEL2
#       MSP430F5              PxSEL
#       MSP430FR2/FR4/FR5/FR6 2·PxSEL1 + PxSEL0  (la columna «PxSELx» de dos
#       MSP432P4              bits de las FR2 es lo mismo)
#     Y en el MSP432E4, el número de PCTL de la tabla «GPIO Pins and Alternate
#     Functions»;
#   * y la del PMAP -«Port Mapping Mnemonics and Functions»-, en el F5529 y el
#     P401R: el valor de cada mnemónico PM_*.
#
# El resultado es TEXTO, una línea por dato, para que un cambio de datasheet
# se vea en el diff. Se versiona (los PDF no), y es lo que lee fuente_ti.py.
# Las correcciones a mano, donde la tabla del PDF no se deja leer, están en
# CORRECCIONES, con la tabla y la página de donde salen: nunca en el .pines.
#
# Las tablas se leen con pdfplumber (pip install pdfplumber). Leer un
# datasheet entero lleva minutos: --cache DIR guarda las tablas de cada PDF en
# DIR/NOMBRE.json y las reutiliza.
# =============================================================================
import json
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(AQUI, "..", "fuentes", "ti")

# tipo: (pdf bajo REFS, documento, encapsulado del modelo, columna del
#        encapsulado en la tabla de patillas, regla del modo)
CHIPS = {
    "MSP430G2553": ("msp430/msp430f2xx/slas735j.pdf", "SLAS735J", "PDIP20", "N20", "g2"),
    "MSP430G2553IPW28": ("msp430/msp430f2xx/slas735j.pdf", "SLAS735J", "TSSOP28", "PW28", "g2"),
    "MSP430F5529": ("msp430/msp430f5xx_msp430f6xx/slas590p.pdf", "SLAS590P", "LQFP80", "PN", "f5"),
    "MSP430FR2311": ("msp430/msp430fr2xx_msp430fr4xx/slase58e.pdf", "SLASE58E", "TSSOP20", "PW20", "fr"),
    "MSP430FR2355": ("msp430/msp430fr2xx_msp430fr4xx/slasec4d.pdf", "SLASEC4D", "LQFP48", "PT", "fr"),
    "MSP430FR2433": ("msp430/msp430fr2xx_msp430fr4xx/slase59f.pdf", "SLASE59F", "VQFN24", "RGE", "fr"),
    "MSP430FR2476": ("msp430/msp430fr2xx_msp430fr4xx/msp430fr2476.pdf", "SLASEO7C", "LQFP48", "PT", "fr"),
    "MSP430FR4133": ("msp430/msp430fr2xx_msp430fr4xx/slas865f.pdf", "SLAS865F", "LQFP64", "PM", "fr"),
    "MSP430FR5969": ("msp430/msp430fr5xx_msp430fr6xx/msp430fr5969.pdf", "SLAS704G", "VQFN48", "RGZ", "fr"),
    "MSP430FR5994": ("msp430/msp430fr5xx_msp430fr6xx/slase54d.pdf", "SLASE54D", "LQFP80", "PN", "fr"),
    "MSP430FR6989": ("msp430/msp430fr5xx_msp430fr6xx/slas789d.pdf", "SLAS789D", "LQFP100", "PZ", "fr"),
    "MSP432P401R": ("msp432/slas826e.pdf", "SLAS826E", "LQFP100", "PZ", "fr"),
    "MSP432E401Y": ("msp432/slasen5.pdf", "SLASEN5", "TQFP128", "PINNUMBER", "e4"),
}

PAD = re.compile(r"^(P\d+|PJ)\.\d+$")
PAD_E4 = re.compile(r"^P[A-T]\d$")
# Lo que es analógico (o un cristal) cuando la tabla no dice el tipo de buffer
ANALOGICA = re.compile(
    r"^(A\d+|C\d+|CA\d+|CB\d+|CD\d+|AIN\d+|VREF[+-]?|VeREF[+-]|Veref[+-]|VEREF[+-]|"
    r"OA\d+[+\-O]|TRI\d[+\-O]|X(IN|OUT)|LFXIN|LFXOUT|HFXIN|HFXOUT|XT2IN|XT2OUT|"
    r"LFXT(IN|OUT)?|HFXT(IN|OUT)?|DCOR|R\d\d|LCDCAP\d?|L\d+|COM\d+|S\d+|ESI(CH|CI)\d|"
    r"ESICOM|ESITEST\d|AUXVCC\d|LCDREF|REFOUT|VREF\+/VeREF\+|CTPL|Capacitivesensing|PinOsc|"
    r"OSC\d?|HIB|WAKE|XOSC\d|USB0(DM|DP|ID|VBUS)|EN0(RXI|TXO)[NP]|DCOR|COUT_N)$")


# --- Las tablas del PDF ------------------------------------------------------
def paginas(pdf, cache):
    """[{pagina, texto, tablas}] de todo el PDF; de la caché si está."""
    if cache:
        os.makedirs(cache, exist_ok=True)
        f = os.path.join(cache, os.path.splitext(os.path.basename(pdf))[0] + ".json")
        if os.path.exists(f):
            with open(f) as fh:
                return json.load(fh)
    import pdfplumber
    out = []
    with pdfplumber.open(pdf) as d:
        for i, p in enumerate(d.pages):
            out.append({"pagina": i + 1, "texto": p.extract_text() or "",
                        "tablas": p.extract_tables()})
    if cache:
        with open(f, "w") as fh:
            json.dump(out, fh)
    return out


def limpia(c):
    return None if c is None else c.replace("\n", "").replace(" ", "")


def sin_notas(v):
    """«P1.1(RD)», «A1(2)», «1(3)», «A3(onlyMSP430F552x)» -> sin los paréntesis."""
    if not v:
        return v
    v = re.sub(r"\([^()]*\)", "", v)
    return v.replace("–", "-").replace("'", "").replace("’", "").strip()


def nombres(celda):
    """«C0,A0/Veref+» -> [C0, A0, Veref+]; «JTAGTDI/TCLK» -> [TDI, TCLK].

    Una notación se unifica: las tablas de funciones del G2 escriben
    «Timer1_A3.TA0» y «Timer1_A3.CCI0A» lo que la de patillas llama «TA1.0» y
    «TA1.CCI0A»."""
    out = []
    for p in re.split(r"[/,;\n]", sin_notas(celda)):
        p = p.strip()
        p = re.sub(r"^Timer(\d)_([AB])\d\.T[AB]CLK$", r"T\2\1CLK", p)
        p = re.sub(r"^T([AB])(\d)\.T[AB]CLK$", r"T\1\2CLK", p)
        p = re.sub(r"^Timer(\d)_[AB]\d\.TA(\d)$", r"TA\1.\2", p)
        p = re.sub(r"^Timer(\d)_[AB]\d\.CCI", r"TA\1.CCI", p)
        if p.startswith("JTAG") and len(p) > 4:
            p = p[4:]
        p = re.sub(r"(crystalmode|bypassmode)$", "", p)       # «XIN crystal mode»
        if p.startswith("Mappedsecondarydigitalfunction"):
            p = "PMAP"          # la función que diga PxMAPy (tabla del PMAP)
        if p:
            out.append(p)
    return out


# --- Las patillas ------------------------------------------------------------
def patillas(pags, columna):
    """[(pos, [señales], {señal: analógica})] de la tabla de patillas.

    Vale para las dos formas de TI: «Pin Attributes» (la primera fila de una
    patilla lleva su número y las siguientes, sin número, sus otras señales)
    y «Terminal Functions» (una fila por patilla, con el nombre compuesto
    P1.0/TA0CLK/ACLK/A0/CA0)."""
    res, vistas = [], set()
    for pg in pags:
        t0 = pg["texto"].replace(" ", "")
        if not re.search(r"(PinAttributes|TerminalFunctions|SignalDescriptions)", t0):
            continue
        if re.search(r"Table\d+-\d+\.SignalDescriptions", t0) and "PinAttributes" not in t0 and \
                any("PinAttributes" in p["texto"].replace(" ", "") for p in pags):
            continue      # si hay Pin Attributes, las Signal Descriptions sobran
        for t in pg["tablas"]:
            filas = [[limpia(c) for c in f] for f in t]
            crudas = [[limpia_lineas(c) for c in f] for f in t]
            # La columna del encapsulado y la del nombre
            cpos = cnom = cbuf = cseg = None
            ini = None
            for i, f in enumerate(filas[:4]):
                for j, c in enumerate(f):
                    if c and (c == columna or re.split(r",", c)[-1] == columna or
                              (columna == "N20" and c in ("PW20,N20", "N20"))):
                        cpos, ini = j, i + 1
                    # «PZ» partido en dos celdas, «P» y «Z» (SLAS789D)
                    if c and len(columna) == 2 and c == columna[0] and j + 1 < len(f) and \
                            f[j + 1] == columna[1]:
                        cpos, ini = j, i + 1
                    if c == "Seg." and cpos is not None and j == cpos + 1:
                        cseg, ini = j, i + 1
                    if c and ("SIGNALNAME" in c or c == "NAME"):
                        cnom = j
                    if c and c.startswith("BUFFER"):
                        cbuf = j
            if cpos is None or cnom is None:
                continue
            actual = None
            for f, fc in zip(filas[ini:], crudas[ini:]):
                if len(f) <= max(cpos, cnom):
                    continue
                pos, nom = f[cpos], fc[cnom]
                if pos and pos not in ("N/A", "–", "-", "—"):
                    pos = sin_notas(pos)
                    if not re.match(r"^[A-Z]?\d+$", pos):
                        actual = None
                        continue
                    if pos in vistas:          # la cabecera repetida de otra página
                        actual = None
                        continue
                    vistas.add(pos)
                    actual = (pos, [], {})
                    res.append(actual)
                elif pos:                       # N/A: no está en este encapsulado
                    actual = None
                    continue
                if actual and nom:
                    # «Sx»: el segmento LCD de este encapsulado, en su columna
                    if cseg is not None and cseg < len(f) and f[cseg]:
                        nom = re.sub(r"(^|[/\n])Sx(?=[/\n]|$)", r"\g<1>" + sin_notas(f[cseg]), nom)
                    for n in nombres(nom):
                        if re.match(r"^S[xyz]$", n):
                            continue        # segmento LCD que este encapsulado no tiene
                        actual[1].append(n)
                        if cbuf is not None and cbuf < len(f) and f[cbuf]:
                            actual[2][n] = "Analog" in f[cbuf]
    return res


# --- Las funciones -----------------------------------------------------------
NO_FUNCION = re.compile(r"^((P\d+|PJ)\.(\d+|x)(\(I/?O\))?|DVSS|DVCC|VSS|VCC|Hi-Z|N/A|NA|Px\.x.*|"
                        r"Disabled|Internallytied.*|Output.*|Input.*)$")
RUIDO_CABECERA = re.compile(r"^(C|CO|CON|CONTR|CONTRO|CONTROL.*|ONTROL.*|NTROL.*|TROL.*|LBIT.*|"
                            r"BIT.*|SAND.*|SIGNAL.*|S\(1\)|ORSI.*|ORS.*|OR|GNALS.*|IGNALS.*|NALS.*|"
                            r"LS.*|\(1\)|\)|D|S/SIGNALS.*|IGNALS|BITS)$")


def limpia_lineas(c):
    """Como limpia(), pero deja los saltos de línea: en las tablas de funciones
    una celda puede llevar varias filas."""
    return None if c is None else "\n".join(x.replace(" ", "") for x in c.split("\n"))


def tablas_funciones(pags):
    puerto_ant = None
    for pg in pags:
        for t in pg["tablas"]:
            filas = [[limpia_lineas(c) for c in f] for f in t]
            planas = [[limpia(c) for c in f] for f in t]
            i0 = next((i for i, f in enumerate(planas) if "FUNCTION" in f and
                       any(c and c.startswith("PINNAME") for c in f)), None)
            if i0 is None:
                continue
            f0 = planas[i0]
            cfun = f0.index("FUNCTION")
            cnom = next(i for i, c in enumerate(f0) if c and c.startswith("PINNAME"))
            m = re.search(r"\((P\d+|PJ)\.x\)", f0[cnom])
            puerto = m.group(1) if m else puerto_ant
            puerto_ant = puerto
            cx = next((i for i, c in enumerate(f0) if c == "x"), None)
            j = i0 + 1
            while j < len(filas) and cfun < len(filas[j]) and not filas[j][cfun]:
                j += 1
            yield pg["pagina"], puerto, f0, filas[i0 + 1:j], filas[j:], cfun, cnom, cx


def _expande(datos, cfun, cnom, cx):
    """Las filas de verdad. pdfplumber junta a veces varias filas de la tabla
    en una celda, una por línea:
      * la celda FUNCTION con N líneas y N-1 filas debajo sin FUNCTION: cada
        línea es la función de una fila;
      * todas las celdas con N líneas: N filas;
      * si no, la celda es un nombre partido en dos líneas («UCB0SIMO/» y
        «UCB0SDA»): se juntan."""
    out = []
    i = 0
    while i < len(datos):
        r = list(datos[i])
        fc = r[cfun] if cfun < len(r) else None
        lin = fc.split("\n") if fc else []
        # «Capacitive» / «sensing»: una línea en minúscula sigue a la de arriba
        for n in range(len(lin) - 1, 0, -1):
            if lin[n][:1].islower():
                lin[n - 1] += lin.pop(n)
        if len(lin) > 1:
            k = 0
            while i + 1 + k < len(datos) and not (datos[i + 1 + k][cfun] if cfun < len(datos[i + 1 + k]) else None) \
                    and not (datos[i + 1 + k][cnom] if cnom < len(datos[i + 1 + k]) else None):
                k += 1
            otras = [c for j, c in enumerate(r) if j not in (cfun, cnom, cx) and c]
            if k == len(lin) - 1:
                r[cfun] = lin[0]
                out.append(r)
                for n in range(k):
                    rr = list(datos[i + 1 + n])
                    rr[cfun] = lin[n + 1]
                    out.append(rr)
                i += 1 + k
                continue
            if otras and all(len(c.split("\n")) == len(lin) for c in otras):
                for n in range(len(lin)):
                    rr = [None] * len(r)
                    for j, c in enumerate(r):
                        if c is None:
                            continue
                        if j in (cnom, cx):
                            rr[j] = c if n == 0 else None
                        else:
                            rr[j] = c.split("\n")[n]
                    out.append(rr)
                i += 1
                continue
            r[cfun] = "".join(lin)
        out.append(r)
        i += 1
    return out


def funciones(pags, avisos):
    """[(pad, celda de función, {cabecera: valor}, página)], con las celdas
    combinadas (en vertical: None) heredadas de la fila de arriba."""
    out = []
    for (pag, pu, f0, cab, datos, cfun, cnom, cx) in tablas_funciones(pags):
        datos = _expande(datos, cfun, cnom, cx)
        ncol = max(len(r) for r in [f0] + cab + datos)

        def celda(r, i):
            return r[i] if i < len(r) else None
        dcols = [i for i in range(cfun + 1, ncol) if any(celda(r, i) is not None for r in datos)]
        nom = {}
        for i in range(cfun + 1, ncol):
            partes = [celda(r, i) for r in [f0] + cab if celda(r, i)]
            partes = [p for p in partes if not RUIDO_CABECERA.match(p.replace("\n", ""))]
            if partes:
                nom[i] = " ".join(partes)
        hcols = sorted(nom)
        if hcols != dcols:
            # pdfplumber pone a veces la cabecera una columna más allá que
            # los datos (una celda combinada): se emparejan por orden
            if len(hcols) == len(dcols):
                nom = {d: nom[h] for h, d in zip(hcols, dcols)}
            else:
                avisos.append("p%d %s: cabeceras %s, datos en las columnas %s"
                              % (pag, pu, [(h, nom[h]) for h in hcols], dcols))
        pad, ant = None, {}
        for r in datos:
            c0 = celda(r, cnom)
            x = celda(r, cx) if cx is not None else None
            if x and re.match(r"^\d+$", sin_notas(x)):
                pad, ant = "%s.%s" % (pu, sin_notas(x)), {}
            elif c0:
                m = [p for p in re.split(r"[/,\n]", c0) if PAD.match(sin_notas(p) or "")]
                if m:
                    pad, ant = sin_notas(m[0]), {}
            fn = celda(r, cfun)
            if not fn or pad is None:
                continue
            fn = sin_notas(fn.replace("\n", ""))
            vals = {}
            for i, n in nom.items():
                v = celda(r, i)
                if v is None:
                    v = ant.get(n)
                elif "\n" in n and "\n" in v:
                    # «P2SEL.6 / P2SEL.7» en una cabecera: el valor del bit del pin
                    bits = n.split("\n")
                    suyo = next((k for k, b in enumerate(bits) if b.endswith("." + pad.split(".")[1])), 0)
                    v = v.split("\n")[suyo] if suyo < len(v.split("\n")) else v
                vals[n] = sin_notas(v.replace("\n", "")) if v else v
            ant = vals
            if NO_FUNCION.match(fn):
                continue
            # La fila GPIO se reconoce por su PxDIR («I: 0; O: 1»), aunque el
            # PDF traiga otro texto en su nombre (SLASEO7C, P3.0: «filter9»)
            if any(v and re.match(r"^(I:0;O:1|0=Input)", v) for v in vals.values()):
                continue
            out.append((pad, fn, vals, pag))
    return out


def bits(v):
    """Los valores que admite un bit: «0», «1», «X» (los dos) o nada."""
    if v is None:
        return None
    v = v.strip()
    return {"0": [0], "1": [1], "X": [0, 1]}.get(v)


def modo_de(vals, regla):
    """Los números que eligen la función ([] si no se elige con PxSEL).

    Una X en un bit de PxSEL es «cualquiera de los dos»: la función se elige
    con los dos números (el TB0.0 de P2.1 del FR5969, con PxSEL1 = X y
    PxSEL0 = 1, con 01 y con 11). Si todos los bits son X, no se elige con
    PxSEL (lo analógico del G2, el JTAG)."""
    def busca(pat):
        return next((v for n, v in vals.items() if re.search(pat, n)), "ausente")
    s1, s0 = busca(r"SEL1"), busca(r"SEL0")
    s2 = busca(r"SEL2")
    s = busca(r"SEL(\.x|x|\.\d)?$|SEL\.\d")
    if s1 != "ausente" and s0 != "ausente":
        a, b = bits(s1), bits(s0)
        if a is None or b is None or (len(a) == 2 and len(b) == 2):
            return []
        return sorted({2 * x + y for x in a for y in b})
    if s0 != "ausente" and regla == "fr":           # FR4133: solo PxSEL0
        b = bits(s0)
        return [] if b is None or len(b) == 2 else b
    if regla == "g2":
        a, b = bits(s), bits(s2)
        if a is None or b is None or (len(a) == 2 and len(b) == 2):
            return []
        return sorted({x + 2 * y for x in a for y in b})
    if s == "ausente" or s is None:
        return []
    s = s.strip()
    if re.match(r"^[01X]{2}$", s):                  # FR2: «PxSELx» de dos bits
        a, b = bits(s[0]), bits(s[1])
        if len(a) == 2 and len(b) == 2:
            return []
        return sorted({2 * x + y for x in a for y in b})
    b = bits(s)
    return [] if b is None or len(b) == 2 else b


# --- El PMAP ------------------------------------------------------------------
def pmap(pags):
    """{mnemónico: valor} de «Port Mapping Mnemonics and Functions». Un valor
    puede tener dos mnemónicos (2: PM_UCA0RXD y PM_UCA0SOMI), en la misma
    celda o en la fila de debajo, sin valor."""
    res = {}
    for pg in pags:
        for t in pg["tablas"]:
            filas = [[limpia_lineas(c) for c in f] for f in t]
            if not filas or not filas[0] or (filas[0][0] or "").replace("\n", "") != "VALUE" or \
                    not any(c and "MNEMONIC" in c.replace("\n", "") for c in filas[0]):
                continue
            val = None
            for f in filas[1:]:
                if len(f) < 2 or not f[1]:
                    continue
                vs = [int(v) for v in re.findall(r"\d+", sin_notas(f[0] or ""))]
                mns = re.findall(r"PM_[A-Za-z0-9_.]+", f[1].replace("\n", ""))
                if vs:
                    val = vs[0]
                if val is None:
                    continue
                if len(vs) == len(mns) and len(vs) > 1:
                    for v, mn in zip(vs, mns):
                        res[mn] = v
                else:
                    for mn in mns:
                        res[mn] = val
    return res


# --- El MSP432E4: «GPIO Pins and Alternate Functions» -----------------------
def funciones_e4(pags):
    """[(pad, nombre, modo)] de la tabla de PCTL: una columna por número."""
    out = []
    for pg in pags:
        if "GPIOPinsandAlternateFunctions" not in pg["texto"].replace(" ", ""):
            continue
        for t in pg["tablas"]:
            filas = [[limpia_lineas(c) for c in f] for f in t]
            # La fila con los números de PCTL: 1 2 3 ... 15
            i0 = next((i for i, f in enumerate(filas) if sum(1 for c in f if c and re.match(r"^\d+$", c)) >= 8), None)
            if i0 is None:
                continue
            cab = filas[i0]
            cols = {j: int(c) for j, c in enumerate(cab) if c and re.match(r"^\d+$", c)}
            for f in filas[i0 + 1:]:
                if not f or not f[0] or not PAD_E4.match(sin_notas(f[0].replace("\n", ""))):
                    continue
                pad = sin_notas(f[0].replace("\n", ""))
                crudas = t[filas.index(f)]
                for j, m in cols.items():
                    if j < len(f) and f[j] and f[j] not in ("-", "–"):
                        # «TDO SWO»: dos señales en una celda, separadas por un
                        # espacio o un salto de línea
                        for n in nombres(re.sub(r"\s+", "/", (crudas[j] or "").strip())):
                            out.append((pad, n, m))
                # Lo analógico o especial: la columna que va antes de los números
                for j in range(2, min(cols)):
                    if j < len(f) and f[j] and f[j] not in ("-", "–"):
                        for n in nombres(f[j]):
                            out.append((pad, n, None))
    return out


# --- Correcciones a mano -----------------------------------------------------
# Donde pdfplumber no lee bien una tabla (un esquema dibujado en la misma
# página, una celda partida), lo que dice el PDF, con la tabla y la página.
# (tipo, pad) -> [(nombre, modo)] que SUSTITUYEN a lo extraído de ese pad
CORRECCIONES = {}
# Las patillas que pdfplumber pierde -la última fila de una página, sin raya
# debajo-: (tipo) -> {posición: nombre}, copiado del PDF
PATILLAS_PERDIDAS = {
    # SLAS590P, tabla 7-1 «Terminal Functions», última fila de las páginas 19 y 20
    "MSP430F5529": {"60": "P7.7/TB0CLK/MCLK", "80": "P6.3/CB3/A3"},
}


# Lo que una tabla de funciones DESCRIBE en vez de nombrar, y la señal de la
# tabla de patillas que es: (documento, pad o None, descripción) -> señal
DESCRIPCIONES = {
    ("SLAS735J", None, "Comparatoroutput"): "CAOUT",            # tabla 23, P3.7
    # SLAS735J tablas 16-23: la fila «Capacitive sensing» es la función que la
    # columna de nombres de la misma tabla llama «Pin Osc»
    ("SLAS735J", None, "Capacitivesensing"): "PinOsc",
    # SLASEC4D tabla 6-66: P4.0 con P4SEL = 10 es la UART de un hilo (ISO
    # 7816): la entrada va a UCA1RXD y a TB3.CCI2B, y la salida es UCA1TXD
    # AND TB3.2B. La tabla de patillas (4-1) las llama ISORXD e ISOTXD
    ("SLASEC4D", "P4.0", "UCA1RXD"): "ISORXD",
    ("SLASEC4D", "P4.0", "UCA1TXDlogic-ANDTB3.2B"): "ISOTXD",
    # SLAS789D: la tabla de funciones escribe DMA0E y la de patillas DMAE0
    ("SLAS789D", None, "DMA0E"): "DMAE0",
}
# Erratas de la tabla de patillas: (documento, escrito) -> lo que es
ERRATAS = {
    # SLASEC4D tabla 4-1: COMP0_0 y COMP0_1, donde el resto del datasheet
    # (y COMP1.0, COMP1.1 en la misma tabla) usa el punto
    ("SLASEC4D", "COMP0_0"): "COMP0.0",
    ("SLASEC4D", "COMP0_1"): "COMP0.1",
}


def canonico(doc, pad, n, señales):
    """El nombre de la señal `n` como lo escribe la tabla de patillas de su
    pad: la de funciones añade a veces una descripción («CBOUT comparator
    B»)."""
    n = DESCRIPCIONES.get((doc, pad, n), DESCRIPCIONES.get((doc, None, n), n))
    # «CPU Status Register Bit SCG1» (FR5/FR6) es la señal SRSCG1
    n = re.sub(r"^CPUStatusRegisterBit(\w+)$", r"SR\1", n)
    # «Sz»/«Sx»: el segmento LCD del pad EN ESTE ENCAPSULADO, de la tabla de
    # patillas (S37); si no tiene, ese pad no es segmento aquí
    if re.match(r"^S[xyz]$", n):
        seg = [x for x in (señales or []) if re.match(r"^S\d+$", x)]
        return seg[:1]
    if not señales or n.upper() in {x.upper() for x in señales}:
        return [n]
    pref = [x for x in señales if len(x) >= 3 and n.upper().startswith(x.upper()) and
            n[len(x):len(x) + 1].islower()]
    if pref:
        return [max(pref, key=len)]
    # «TCKSWCLK»: dos señales del pad escritas juntas, sin separador (SLASEN5)
    for x in señales:
        for y in señales:
            if x != y and (x + y).upper() == n.upper():
                return [x, y]
    return [n]


# --- La transcripción --------------------------------------------------------
def transcribe(tipo, refs, cache):
    pdf, doc, enc, col, regla = CHIPS[tipo]
    pags = paginas(os.path.join(refs, pdf), cache)
    avisos = []
    pts = patillas(pags, col)
    pts = [(pos, [ERRATAS.get((doc, x), x) for x in sen], {ERRATAS.get((doc, k), k): v for k, v in an.items()})
           for pos, sen, an in pts]
    for pos, nom in PATILLAS_PERDIDAS.get(tipo, {}).items():
        if pos not in {p[0] for p in pts}:
            pts.append((pos, nom.split("/"), {}))
    # Que estén todas: 1..N sin huecos (en las rejillas de bolas no se mira)
    nums = sorted(int(p[0]) for p in pts if p[0].isdigit())
    if nums and nums != list(range(1, len(nums) + 1)):
        avisos.append("faltan patillas: %s" % sorted(set(range(1, nums[-1] + 1)) - set(nums)))
    analog = {}
    for _, señales, an in pts:
        analog.update(an)
    lineas = []
    lineas.append("# %s, %s: el patillaje del datasheet de TI %s." % (tipo, enc, doc))
    lineas.append("# Lo escribe biblioteca/herramientas/extrae_ti.py; no se edita a mano.")
    lineas.append("# patilla POSICION NOMBRE         (el nombre, como en la tabla de patillas)")
    lineas.append("# funcion PAD MODO NOMBRE [analogica]   (MODO: el número que la elige;")
    lineas.append("#                                       - si no se elige con PxSEL/PCTL)")
    lineas.append("# pmap MNEMONICO VALOR               (el PMAP: el valor de PxMAPy)")
    lineas.append("documento %s" % doc)
    lineas.append("encapsulado %s %s" % (enc, col))
    for pos, señales, _ in pts:
        lineas.append("patilla %s %s" % (pos, "/".join(señales)))
    vistas = set()
    if regla == "e4":
        fs = [(pad, n, m) for (pad, n, m) in funciones_e4(pags)]
    else:
        fs = []
        for pad, celda, vals, pag in funciones(pags, avisos):
            ms = modo_de(vals, regla) or [None]
            for n in nombres(celda):
                for m in ms:
                    fs.append((pad, n, m))
    for (tpad, pad_), reemplazo in CORRECCIONES.items():
        if tpad == tipo:
            fs = [x for x in fs if x[0] != pad_] + [(pad_, n, m) for n, m in reemplazo]
    señales_de = {}
    for _, señales, _ in pts:
        pad = next((x for x in señales if PAD.match(x) or (regla == "e4" and PAD_E4.match(x))), None)
        if pad:
            señales_de[pad] = señales
    fs = [(pad, c, m) for pad, n, m in fs for c in canonico(doc, pad, n, señales_de.get(pad))]
    pads_patillas = set()
    for _, señales, _ in pts:
        for s in señales:
            if PAD.match(s) or (regla == "e4" and PAD_E4.match(s)):
                pads_patillas.add(s)
                break
    # El cruce de las dos tablas: cada señal de una patilla de E/S tiene que
    # salir en la tabla de funciones de su pad (o es lo que pdfplumber perdió)
    por_pad = {}
    for pad, n, m in fs:
        por_pad.setdefault(pad, set()).add(n.upper())
    for pos, señales, _ in pts:
        pad = next((x for x in señales if PAD.match(x) or (regla == "e4" and PAD_E4.match(x))), None)
        if not pad:
            continue
        for x in señales:
            if x != pad and x.upper() not in por_pad.get(pad, set()) and not x.startswith("PM_"):
                # Lo que no se elige con PxSEL (el BSL, VeREF de un ADC que la
                # elige con su propio registro): sin número. Se dice, por si es
                # una fila que pdfplumber perdió
                fs.append((pad, x, None))
                por_pad.setdefault(pad, set()).add(x.upper())
                if not re.match(r"^(TDO|TDI|TMS|TCK|TCLK|SWCLK|SWDIO|SWO|SBW)", x):
                    avisos.append("%s: «%s» solo está en la tabla de patillas (sin número)" % (pad, x))
    for pad, n, m in fs:
        if pad not in pads_patillas:
            continue
        clave = (pad, n, m)
        if clave in vistas:
            continue
        vistas.add(clave)
        an = analog.get(n, bool(ANALOGICA.match(n)))
        lineas.append("funcion %s %s %s%s" % (pad, "-" if m is None else m, n,
                                              " analogica" if an else ""))
    if regla != "e4":
        for mn, v in sorted(pmap(pags).items(), key=lambda x: (x[1], x[0])):
            lineas.append("pmap %s %d" % (mn, v))
    return "\n".join(lineas) + "\n", avisos, len(pts)


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit("uso: extrae_ti.py REFS [--cache DIR] [TIPO...]")
    refs = args.pop(0)
    cache = None
    if args[:1] == ["--cache"]:
        cache = args[1]
        args = args[2:]
    os.makedirs(SALIDA, exist_ok=True)
    for tipo in args or sorted(CHIPS):
        texto, avisos, n = transcribe(tipo, refs, cache)
        with open(os.path.join(SALIDA, tipo + ".pines"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(texto)
        print("%-18s %3d patillas %4d funciones%s" % (
            tipo, n, texto.count("\nfuncion "), "".join("\n    " + a for a in avisos)))


if __name__ == "__main__":
    main()
