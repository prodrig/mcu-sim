#!/usr/bin/env python3
# =============================================================================
# comprueba_piezas.py — La prueba de B5 (doc/analisis_build.md)
#
#   python3 comprueba_piezas.py CARPETA [REGENERADA]
#
# Por cada TIPO.forma.xml de CARPETA (biblioteca/piezas):
#   * los terminales de sus <ancla> son EXACTAMENTE los de la ficha
#     (TIPO.pieza.xml), ni uno más ni uno menos;
#   * cada <ancla elemento> existe en TIPO.svg, y su x, y caen dentro;
#   * el SVG cumple el perfil de autor de mcu-sim-gui (§11 de
#     doc/analisis-uso-ilustraciones.md): SVG 1.2 Tiny, viewBox y tamaño en
#     mm; nada de filtros, máscaras, scripts, estilos, animaciones ni
#     imágenes; la pieza es <g id="$"> y todo id empieza por "$";
#   * una paramétrica (generador=) no tiene SVG.
# Con REGENERADA (otra carpeta donde dibuja_piezas.py acaba de escribir),
# además, que lo versionado es lo que se regenera, byte a byte.
# =============================================================================
import filecmp
import os
import re
import sys
import xml.dom.minidom as minidom

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dibuja_piezas import GENERADORES, terminales  # noqa: E402

PROHIBIDOS = {"filter", "mask", "script", "style", "image", "foreignObject", "animate",
              "animateTransform", "animateMotion", "set", "clipPath", "pattern"}
LADOS = {"arriba", "abajo", "izquierda", "derecha"}


def ids(nodo, out):
    if nodo.nodeType == nodo.ELEMENT_NODE:
        if nodo.tagName in PROHIBIDOS:
            out["prohibidos"].append(nodo.tagName)
        if nodo.hasAttribute("style"):
            out["prohibidos"].append("style= en <%s>" % nodo.tagName)
        if nodo.hasAttribute("id"):
            out["ids"].append(nodo.getAttribute("id"))
    for h in nodo.childNodes:
        ids(h, out)


def comprueba(carpeta):
    errores = []
    formas = sorted(fn for fn in os.listdir(carpeta) if fn.endswith(".forma.xml"))
    piezas = sorted(fn for fn in os.listdir(carpeta) if fn.endswith(".pieza.xml"))
    if [x[:-10] for x in formas] != [x[:-10] for x in piezas]:
        errores.append("no hay una forma por ficha: %s" % sorted(
            set(x[:-10] for x in formas) ^ set(x[:-10] for x in piezas)))
    for fn in formas:
        tipo = fn[:-10]
        fo = minidom.parse(os.path.join(carpeta, fn)).documentElement
        svg_ruta = os.path.join(carpeta, tipo + ".svg")
        if fo.getAttribute("generador"):
            if os.path.exists(svg_ruta):
                errores.append("%s: es paramétrica y tiene SVG" % tipo)
            if GENERADORES.get(tipo) != fo.getAttribute("generador"):
                errores.append("%s: generador desconocido" % tipo)
            continue
        if not os.path.exists(svg_ruta):
            errores.append("%s: no tiene %s.svg" % (tipo, tipo))
            continue
        s = minidom.parse(svg_ruta).documentElement
        if s.getAttribute("version") != "1.2" or s.getAttribute("baseProfile") != "tiny":
            errores.append("%s.svg: no es SVG 1.2 Tiny" % tipo)
        vb = s.getAttribute("viewBox").split()
        if len(vb) != 4 or not re.match(r"^[\d.]+mm$", s.getAttribute("width")) or \
                not re.match(r"^[\d.]+mm$", s.getAttribute("height")):
            errores.append("%s.svg: sin viewBox o sin tamaño en mm" % tipo)
            continue
        w, h = float(vb[2]), float(vb[3])
        if abs(float(s.getAttribute("width")[:-2]) * 10 - w) > 0.1:
            errores.append("%s.svg: el viewBox no está en unidades de 0,1 mm" % tipo)
        r = {"ids": [], "prohibidos": []}
        ids(s, r)
        if r["prohibidos"]:
            errores.append("%s.svg: lleva %s" % (tipo, ", ".join(sorted(set(r["prohibidos"])))))
        if not r["ids"] or r["ids"][0] != "$":
            errores.append("%s.svg: la pieza no es <g id=\"$\">" % tipo)
        malos = [i for i in r["ids"] if i != "$" and not i.startswith("$.")]
        if malos:
            errores.append("%s.svg: ids que no son de la pieza: %s" % (tipo, malos))
        if len(set(r["ids"])) != len(r["ids"]):
            errores.append("%s.svg: ids repetidos" % tipo)
        ts, _, sin = terminales(os.path.join(carpeta, tipo + ".pieza.xml"))
        anclas = fo.getElementsByTagName("ancla")
        if sin != (fo.getAttribute("sin_terminales") == "si"):
            errores.append("%s: sin_terminales no dice lo mismo que la ficha" % tipo)
        nombres = [a.getAttribute("terminal") for a in anclas]
        if sorted(nombres) != sorted(ts):
            errores.append("%s: las anclas no son los terminales de la ficha. De más: %s. Faltan: %s"
                           % (tipo, sorted(set(nombres) - set(ts)), sorted(set(ts) - set(nombres))))
        for a in anclas:
            el, x, y = a.getAttribute("elemento"), float(a.getAttribute("x")), float(a.getAttribute("y"))
            if el not in r["ids"]:
                errores.append("%s: el ancla %s no está en el SVG" % (tipo, el))
            if not (0 <= x <= w and 0 <= y <= h):
                errores.append("%s: el ancla %s cae fuera del dibujo" % (tipo, el))
            if a.getAttribute("lado") not in LADOS:
                errores.append("%s: el ancla %s tiene un lado desconocido" % (tipo, el))
    return errores, len(formas)


def al_dia(carpeta, nueva):
    distintos = []
    for fn in sorted(os.listdir(nueva)):
        if fn.endswith((".svg", ".forma.xml")):
            v = os.path.join(carpeta, fn)
            if not os.path.exists(v) or not filecmp.cmp(v, os.path.join(nueva, fn), shallow=False):
                distintos.append(fn)
    for fn in sorted(os.listdir(carpeta)):
        if fn.endswith((".svg", ".forma.xml")) and not os.path.exists(os.path.join(nueva, fn)):
            distintos.append(fn + " (sobra)")
    return distintos


def main():
    carpeta = sys.argv[1]
    errores, n = comprueba(carpeta)
    if len(sys.argv) > 2:
        d = al_dia(carpeta, sys.argv[2])
        if d:
            errores.append("no están al día (make biblioteca-piezas): %s" % ", ".join(d))
    for e in errores:
        print("ERROR " + e)
    if errores:
        sys.exit(1)
    print("%d formas: anclas, dibujos y perfil de autor en regla" % n)


if __name__ == "__main__":
    main()
