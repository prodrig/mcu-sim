# =============================================================================
# cnc.py — LA FRESADORA CNC, por fases (doc/analisis_cnc.md §11), contra el
#          mcu-sim de verdad y con la ventana mínima de `ventana.py`
#
#   C1  (F0) LA CADENA MECÁNICA. Un motor de continua con un VOLANTE de
#       inercia conocida colgado en una <transmision>: la constante de tiempo
#       es la de la cuenta, (J_motor + J_volante)·R / (Ke·Kt), y sin el volante
#       la del motor suelto. Lo que la transmisión deja ver, la inercia
#       reflejada, y los errores de una cadena mal declarada. Y el efecto
#       `desplaza` de la tabla de enlaces, con su vector=.
#
# Cada grupo monta su banco en una carpeta temporal, con placas escritas aquí.
#
#   make -f Makefile.mcu-sim gui-cnc
#   python3 verif/gui/cnc.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, check, grupo, arranca, termina, saludo_hasta_listo,
                     suscribe, instantanea, aviso, ordenes, T_INSTANTANEA, T_AVISO, T_FIN)


# ---------------------------------------------------------------------------
# Lo común
# ---------------------------------------------------------------------------
def valida(sim, ruta):
    """`--valida` de una placa: (codigo, salida)."""
    r = subprocess.run([sim, ruta, "--valida"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=60)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def corre(sim, ruta, ms, args=()):
    """Una simulación sin ventana: (codigo, salida)."""
    r = subprocess.run([sim, ruta, "--ms=%d" % ms] + list(args), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=600)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def muestrea(sim, ruta, ms, pedidos, periodo_ns=1000000, lista=(), args=()):
    """Corre `ruta` `ms` ms con la ventana, suscrita cada `periodo_ns` a los
    observables `pedidos` -[(pieza, observable)]-, mandando las `lista` de
    órdenes [(t_ms, pieza, mando, valor)]. Devuelve (series, avisos, catalogo, rc):
    series[(pieza, obs)] = [(t_s, valor)]."""
    v = Ventana()
    p = arranca(sim, [ruta, "--ms=%d" % ms] + list(args), v.puerto)
    try:
        if not v.acepta():
            return None, None, None, None
        _, _, cat, listo = saludo_hasta_listo(v, version=2)
        pz = {x.get("id"): x for x in ET.fromstring(cat).iter("pieza")}
        ids = []
        for pid, nombre in pedidos:
            ob = [o for o in pz[pid].iter("observable") if o.get("nombre") == nombre]
            ids.append(int(ob[0].get("id_obs")))
        suscribe(v, periodo_ns, ids)
        ords, t = [], 0
        for t_ms, pid, mando, valor in lista:
            m = [x for x in pz[pid].iter("mando") if x.get("nombre") == mando][0]
            ords.append((int(t_ms * 1000000) - t, int(pz[pid].get("idx")), int(m.get("idx")),
                         float(valor)))
            t = int(t_ms * 1000000)
        if ords:
            ordenes(v, ords)
        arranque(v)
        series = {k: [] for k in pedidos}
        avs = []
        while True:
            tt, c = v.recibe(seg=300)
            if tt is None:
                break
            if tt == T_INSTANTANEA:
                ts, _, m = instantanea(c)
                for k, (i, val) in zip(pedidos, m):
                    series[k].append((ts * 1e-9, val))
            elif tt == T_AVISO:
                avs.append(aviso(c))
            elif tt == T_FIN:
                break
        rc, _, _ = termina(p)
        return series, avs, cat, rc
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def cruce(serie, nivel):
    """El instante en que la serie pasa por `nivel`, interpolado."""
    for (t0, a), (t1, b) in zip(serie, serie[1:]):
        if (a - nivel) * (b - nivel) <= 0 and a != b:
            return t0 + (t1 - t0) * (nivel - a) / (b - a)
    return None


# Las cifras del MotorCC (parts/motor_cc.h), para comprobar las cuentas
def constantes_motor(v_nominal=6.0, rpm_vacio=250.0, i_vacio_ma=160.0, i_bloqueo_ma=1500.0,
                     tau_ms=50.0):
    r = v_nominal / (i_bloqueo_ma * 1e-3)
    w = rpm_vacio * 2 * math.pi / 60
    ke = (v_nominal - i_vacio_ma * 1e-3 * r) / w
    j = tau_ms * 1e-3 * ke * ke / r
    return r, ke, j


# ---------------------------------------------------------------------------
# C1 (F0): la cadena mecánica y el efecto desplaza
# ---------------------------------------------------------------------------
# Un motor TT casi sin inductancia, directo a una fuente de 6 V: así su
# arranque es una exponencial limpia. La fuente y la masa ponen 0,1 ohm cada una
BANCO_VOLANTE = """<placa nombre="banco-volante">
  <nodo id="MA" externo="si"/>
  <nodo id="MB" externo="si"/>
  <componente tipo="Fuente" id="V6" v="6"><pin nombre="pin" nodo="MA"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="MB"/></componente>
  <componente tipo="MotorCC" id="M" l_mh="0.01">
    <pin nombre="a" nodo="MA"/><pin nombre="b" nodo="MB"/>
  </componente>
%(volante)s
</placa>
"""


def c1_cadena(sim, cp):
    grupo("C1 (F0) La cadena mecanica: un volante de inercia conocida tras un motor")
    r, ke, jm = constantes_motor()
    rt = r + 0.2
    jv = jm                                  # un volante como el motor: el doble de tau
    jv_gcm2 = jv * 1e7
    taus = {}
    for caso, vol in (("suelto", ""),
                      ("volante", '  <componente tipo="Volante" id="V" inercia_gcm2="%.6f"/>\n'
                                  '  <transmision id="EJE" cadena="M V"/>' % jv_gcm2)):
        ruta = cp("volante_%s.xml" % caso, BANCO_VOLANTE % {"volante": vol})
        series, avs, cat, rc = muestrea(sim, ruta, 900, [("M", "rpm")], periodo_ns=250000)
        s = series[("M", "rpm")] if series else []
        if not s:
            taus[caso] = None
            continue
        # La velocidad final, de la cuenta: con la corriente de vacio
        w_ss = (6.0 - 0.160 * rt) / ke * 60 / (2 * math.pi)
        t63 = cruce([(0.0, 0.0)] + s, w_ss * (1 - math.exp(-1)))
        taus[caso] = (t63, s[-1][1], avs, cat, w_ss)
    ok = all(taus.get(k) for k in ("suelto", "volante"))
    check(ok, "el banco corre suelto y con el volante")
    if not ok:
        return
    tau_m = jm * rt / (ke * ke)
    tau_v = (jm + jv) * rt / (ke * ke)
    t_s, f_s, _, _, w_ss = taus["suelto"]
    t_v, f_v, avs, cat, _ = taus["volante"]
    check(abs(t_s - tau_m) < 0.03 * tau_m,
          "suelto, su constante de tiempo es la de la cuenta: %.1f ms (J·R/(Ke·Kt) = %.1f ms)"
          % (t_s * 1e3, tau_m * 1e3))
    check(abs(t_v - tau_v) < 0.03 * tau_v,
          "con un volante de %.0f g·cm2 -la inercia del motor-, la del motor y el volante "
          "juntos: %.1f ms (%.1f de la cuenta, el doble)" % (jv_gcm2, t_v * 1e3, tau_v * 1e3))
    check(abs(f_s - w_ss) < 0.3 and abs(f_v - w_ss) < 0.3,
          "y los dos llegan a la misma velocidad, la de la cuenta: la inercia no frena en "
          "regimen (%.1f y %.1f rpm; %.1f)" % (f_s, f_v, w_ss))
    pz = {x.get("id"): x for x in ET.fromstring(cat).iter("pieza")}
    tr = [o.get("nombre") for o in pz["EJE"].iter("observable")] if "EJE" in pz else []
    vo = [o.get("nombre") for o in pz["V"].iter("observable")] if "V" in pz else []
    check(tr == ["angulo", "rpm", "inercia"] and vo == ["angulo", "rpm"],
          "en el catalogo, la transmision deja ver angulo, rpm e inercia; el volante, su "
          "angulo y sus rpm")
    ruta = cp("volante_volante.xml", BANCO_VOLANTE % {
        "volante": '  <componente tipo="Volante" id="V" inercia_gcm2="%.6f"/>\n'
                   '  <transmision id="EJE" cadena="M V"/>' % jv_gcm2})
    rc, out = corre(sim, ruta, 300)
    k = out.find("TRANSMISION EJE: ")
    lin = out[k:out.find("\n", k)] if k >= 0 else ""
    check(rc == 0 and "1 motor(es) y 1 elemento(s)" in lin and
          ("%.1f g·cm2" % ((jm + jv) * 1e7)) in lin,
          "la consola dice como va el eje y la inercia reflejada: %s" % lin.strip())

    # Un volante con relacion: un disco de acero, tras un motor y con la
    # transmision por delante del motor en el fichero
    rc, out = valida(sim, cp("mal_orden.xml", BANCO_VOLANTE % {
        "volante": '  <transmision id="EJE" cadena="M V"/>\n'
                   '  <componente tipo="Volante" id="V" inercia_gcm2="10"/>'}))
    check(rc != 0 and "EJE.e1: V se declara despues" in out,
          "una transmision que nombra un volante declarado despues: error con nombre")
    rc, out = valida(sim, cp("mal_motor.xml", BANCO_VOLANTE % {
        "volante": '  <componente tipo="Volante" id="V" inercia_gcm2="10"/>\n'
                   '  <transmision id="EJE" cadena="V M"/>'}))
    check(rc != 0 and "M es un motor y va detras de un elemento" in out,
          "el motor detras del volante: primero los motores")
    rc, out = valida(sim, cp("mal_dos.xml", BANCO_VOLANTE % {
        "volante": '  <componente tipo="Volante" id="V" inercia_gcm2="10"/>\n'
                   '  <transmision id="EJE" cadena="M V"/>\n'
                   '  <transmision id="OTRA" cadena="M"/>'}))
    check(rc != 0 and "M ya va en otra transmision" in out,
          "un motor en dos transmisiones: error")
    rc, out = valida(sim, cp("mal_pieza.xml", BANCO_VOLANTE % {
        "volante": '  <transmision id="EJE" cadena="M MASA"/>'}))
    check(rc != 0 and "MASA es un Gnd, que no tiene eje" in out,
          "una masa en una cadena: no tiene eje")
    rc, out = valida(sim, cp("mal_volante.xml", BANCO_VOLANTE % {
        "volante": '  <componente tipo="Volante" id="V"/>'}))
    check(rc != 0 and "inercia_gcm2, o diametro_mm y espesor_mm" in out,
          "un volante sin inercia: error")
    rc, out = valida(sim, cp("disco.xml", BANCO_VOLANTE % {
        "volante": '  <componente tipo="Volante" id="V" diametro_mm="60" espesor_mm="10"/>\n'
                   '  <transmision id="EJE" cadena="M V"/>'}))
    check(rc == 0 and "0 avisos" in out,
          "un disco de acero de 60 x 10 mm, en su transmision: se valida sin avisos")

    grupo("C1 (F0) El efecto desplaza: el elemento se traslada con varios observables")
    placa = """<placa nombre="desplaza">
  <nodo id="MA" externo="si"/>
  <nodo id="MB" externo="si"/>
  <componente tipo="Fuente" id="V6" v="6"><pin nombre="pin" nodo="MA"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="MB"/></componente>
  <componente tipo="MotorCC" id="M"><pin nombre="a" nodo="MA"/><pin nombre="b" nodo="MB"/></componente>
  <ilustracion>
    <enlace pieza="M" elemento="carro" efecto="desplaza" observable="%s" vector="%s"/>
  </ilustracion>
</placa>
"""
    rc, out = valida(sim, cp("desplaza.xml", placa % ("vueltas_no rpm", "0.1 0  0 1")))
    check(rc == 0, "observable=\"a b\" con vector=\"0.1 0  0 1\": dos por observable, vale "
          "(la ventana mira luego que la pieza los tenga)")
    rc, out = valida(sim, cp("desplaza_mal.xml", placa % ("rpm angulo", "1 0 1")))
    check(rc != 0 and "son 4 numeros, x e y por cada observable" in out,
          "un vector que no casa con los observables: error con lo que hace falta")
    rc, out = valida(sim, cp("desplaza_sin.xml",
                             placa.replace(' vector="%s"', '') % ("rpm",)))
    check(rc != 0 and "necesita observable= -uno o varios- y vector=" in out,
          "desplaza sin vector=: error")
    rc, out = valida(sim, cp("vector_angulo.xml", placa.replace('efecto="desplaza"',
                                                                'efecto="angulo"') %
                             ("angulo", "1 0")))
    check(rc != 0 and "vector= solo vale con efecto=\"desplaza\"" in out,
          "vector= con otro efecto: error")


def main():
    a = argparse.ArgumentParser(description="La fresadora CNC, por fases, contra el mcu-sim de verdad")
    exe = "build/mcu-sim.exe" if os.name == "nt" else "build/mcu-sim"
    a.add_argument("--sim", default=exe)
    o = a.parse_args()
    sim = os.path.normpath(o.sim)
    if not os.path.exists(sim):
        print("  [FALLO] no esta %s: make -f Makefile.mcu-sim mcu-sim" % sim)
        return 2
    tmp = tempfile.mkdtemp(prefix="cnc_")
    try:
        def cp(nombre, texto):
            ruta = os.path.join(tmp, nombre)
            with open(ruta, "w", encoding="utf-8") as f:
                f.write(texto)
            return ruta
        c1_cadena(sim, cp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print()
    print("=====================================================")
    print("TOTAL CNC : %d comprobaciones OK, %d fallos" % (ventana.ok, ventana.fallos))
    print("=====================================================")
    return 0 if ventana.fallos == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
