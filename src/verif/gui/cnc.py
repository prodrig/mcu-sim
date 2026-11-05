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
#   C2  (F1a) LOS PASO A PASO Y SUS DRIVERS. El NEMA 17 de la 3018 tras el
#       módulo Pololu del A4988, movido por un generador de pasos: sigue a los
#       pasos, con su corriente; y cada error de cableado y de temporización,
#       un aviso con nombre -RESET al aire, 3,3 V con VDD a 5 V, un pulso
#       corto, DIR tarde, una bobina al aire o entre dos fases, VMOT bajo-, la
#       protección térmica y la bobina al revés. El ST5918 tras un DM542: su
#       CURVA DE PAR MÁXIMO a 24 y a 48 V, con su esquina; los pasos perdidos
#       con una rampa demasiado corta; el modelo ideal; el DRV8825 y el
#       TMC2209; un paso a paso tras un L298; y lo que cuesta simularlo.
#   C3  (F1b) LA CONTINUA DE POTENCIA. El cabezal, un motor 775 a 24 V tras un
#       módulo MOSFET: a unas 10 000 rpm; con PWM, casi a tope en vacío y a
#       la mitad con carga; sin el diodo, el aviso; un IRF520 que no se abre
#       con 5 V. El IBT-2 con sus dos BTS7960: adelante, atrás y freno, su IS,
#       y el 74HC244 con 3,3 V. Y un PID de posición en el firmware de una
#       Nucleo-F446RE, con el motorreductor de 37 mm y su encoder Hall: una
#       vuelta y media atrás, con el error en 4 cuentas.
#   C4  (F1c) EL BLDC CON SU CONTROLADOR. El DB59L024035-A tras un controlador
#       tipo C5-E, en reloj y dirección: la secuencia del freno; 600 rpm con
#       cada paso contado; 3500 rpm con 0,47 N·m a 9,4 A; con más carga de la
#       que puede, no pierde pasos: se para y lo dice. Las fases y los Hall
#       cruzados, los Hall sin tensión, y el encoder contra los Hall a 60 rpm.
#   C5  (F2) LA TRANSMISIÓN. El rendimiento y el autobloqueo de la tabla del
#       §6.3, calculados; la inercia reflejada; el Z con bolas sin freno se
#       cae, con Tr8x2 o con freno, no; los pasos, la holgura de la tuerca, el
#       tope y la velocidad crítica; un acople que no encaja.
#   C6  (F3) LA MÁQUINA, con verif/fw/cnc. La shield, la fresadora y el
#       sistema; un cuadrado y un círculo contra lo pedido; $H, G38.2, los
#       límites duros, la seta que corta los drivers y el cabezal. Y el fallo
#       del núcleo que destapó el firmware (CONTROL.FPCA).
#   C7  (F3) grblHAL SIN MODIFICAR, en la 3020 con su cableado. El fallo del
#       núcleo que destapó (GE de UADD8, el strlen de newlib); lo que hay que
#       ajustar ($5, $14, $3) y lo que no puede (la sonda). Si no está
#       verif/fw/grblhal/grblhal.bin -GPL-3, no se versiona: `make -C
#       verif/fw/grblhal`-, se saltan sus pruebas.
#   C8  (F4) LA PIEZA. Un bolsillo de 20 x 10 x 2 mm sale con su fondo plano,
#       su hondo y sus paredes; una V de 60° da el ancho que toca; la imagen
#       de la pieza sale a trozos (T_IMAGEN_RECT) y llega igual que entera;
#       en aluminio el corte frena el cabezal; sin M3, la fresa parada hace
#       perder pasos; y una fresa que baja de más entra en la mesa.
#   C9  (F5) LA POTENCIA DE 48 V. El Z con BLDC, freno y husillo de bolas: al
#       frenar, la energía vuelve al bus; la resistencia de frenado la quema;
#       sin ella, el bus sube y salta la protección -y el freno sujeta el Z-;
#       con una fuente que absorbe, no pasa nada. Y un final de 24 V por un
#       optoacoplador.
#   C10 (F6) EL BUS DE CAMPO. Un eje X con su controlador por CANopen y una
#       Nucleo con un transceptor y un maestro (verif/fw/canopen): el
#       arranque -boot-up, SDO, NMT, la SYNC y la maquina de estados de CiA
#       402-, el homing por el final, el perfil de posicion, un final que para
#       el eje, el fallo con su EMCY y su reinicio, el perfil de velocidad, la
#       parada rapida, y un bus sin nadie que asienta. Las tramas, en el hilo:
#       las apunta un analizador.
#   C11 (F7) EL BLDC CONMUTADO POR EL MCU. Una Nucleo con una X-NUCLEO-IHM07M1
#       (el L6230) y un Nanotec DB42S01, con verif/fw/bldc6: seis pasos con
#       los Hall en el TIM2 -la corriente por fase, dos de tres-, el lazo de
#       velocidad en los dos sentidos, la sobrecorriente que baja DIAG-EN y
#       frena el TIM1, los Hall cruzados, la conmutacion senoidal y lo que
#       cuesta cada una.
#   C12 LA FOC. El mismo banco, con los tres shunts de la IHM07M1 y sus
#       amplificadores en el ADC, disparados por el TIM1 en el pico del PWM:
#       el offset, la corriente medida contra la del motor, iq e id, el lazo
#       de velocidad con una carga de golpe, el cambio de sentido sin parar,
#       las tres fases a la vez y lo que cuesta.
#   C13 UNO O TRES SHUNTS: J5 y J6 de la IHM07M1. Con uno -el de fabrica-, las
#       tres fuentes de abajo a R43 y la corriente del bus en PA0; con tres,
#       cada fase la suya. La del bus, la misma con los dos; y la FOC, que no
#       arranca con uno.
#   C14 (F8) LA VISTA 3D. El <vista3d> de la fresadora -sólidos y la
#       superficie de la pieza, movidos por los mismos observables que el
#       dibujo-, validado: cada error, con su línea; vuelve en T_PLACA tal
#       cual. Y la imagen de alturas de la pieza, contra el mapa de alturas:
#       en centésimas de mm, la fila de arriba la de más Y, y la huella de la
#       fresa en el azul.
#
# Cada grupo monta su banco en una carpeta temporal, con placas escritas aquí.
#
# LO QUE CUESTA, en segundos de reloj, depende de la máquina, y de lo cargada
# que esté. Por eso cada tope de tiempo de reloj está escrito para la máquina
# de REFERENCIA -un contenedor Linux de dos núcleos, donde el patrón de abajo
# tarda PATRON_REF- y se multiplica por lo que la de ahora sea más lenta: el
# patrón -servo_demo en la Nucleo-F446RE, 300 ms simulados, el mejor de tres-
# se mide al empezar. Una máquina más rápida no aprieta los topes. Cada tope
# deja además un 40 % sobre lo peor que se ha medido en la de referencia: lo
# que tienen que cazar es que algo cueste el doble, no un mal momento.
#
#   make -f Makefile.mcu-sim gui-cnc
#   python3 verif/gui/cnc.py [--sim build/mcu-sim]
#
# Hay que ejecutarlo desde src/. Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

import ventana
from ventana import (Ventana, arranque, check, grupo, arranca, termina, saludo_hasta_listo,
                     suscribe, instantanea, aviso, ordenes, imagen, imagen_rect, aplica_rect,
                     T_INSTANTANEA, T_AVISO, T_FIN, T_IMAGEN, T_IMAGEN_RECT)


# ---------------------------------------------------------------------------
# Lo común
# ---------------------------------------------------------------------------
PATRON_REF = 0.5          # s de reloj del patron en la maquina de referencia
LENTITUD = [1.0]          # lo que esta maquina es mas lenta que la de referencia


def mide_patron(sim):
    """El patron de velocidad: servo_demo en la Nucleo-F446RE, 300 ms simulados -el
    nucleo a 180 MHz sin parar, que es lo que pesa en los bancos de abajo-. El mejor
    de tres, en segundos de reloj, con el arranque de mcu-sim."""
    mejor = None
    for _ in range(3):
        t0 = time.time()
        rc, _out = corre(sim, "placas/nucleo_f446re.xml", 300,
                         ["verif/fw/servo_demo/servo_demo.bin"])
        dt = time.time() - t0
        if rc == 0 and (mejor is None or dt < mejor):
            mejor = dt
    return mejor


def cuesta(dt, tope, texto):
    """Un tope de tiempo de reloj -el de la maquina de referencia-, por lo lenta
    que sea esta."""
    t = tope * LENTITUD[0]
    check(dt < t, texto + " (tope %.1f s%s)" %
          (t, "" if LENTITUD[0] == 1.0 else ", esta maquina a x%.2f de la de referencia"
           % LENTITUD[0]))


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


ULTIMA = [""]          # la consola de la ultima simulacion con ventana
PLACA = [b""]          # el T_PLACA de la ultima simulacion con ventana
# Las imagenes de la ultima simulacion con ventana, por (pieza, imagen): las
# enteras y los trozos que llegaron, los bytes y la imagen de al final, rehecha
IMAGENES = [{}]


def muestrea(sim, ruta, ms, pedidos, periodo_ns=1000000, lista=(), args=(), imagenes=(),
             parcial=False):
    """Corre `ruta` `ms` ms con la ventana, suscrita cada `periodo_ns` a los
    observables `pedidos` -[(pieza, observable)]-, mandando las `lista` de
    órdenes [(t_ms, pieza, mando, valor)]. Devuelve (series, avisos, catalogo, rc):
    series[(pieza, obs)] = [(t_s, valor)]. Con `imagenes` -[(pieza, imagen)]-
    se suscribe tambien a ellas, a trozos si `parcial`, y deja en IMAGENES lo
    que llego."""
    v = Ventana()
    p = arranca(sim, [ruta, "--ms=%d" % ms] + list(args), v.puerto)
    try:
        if not v.acepta():
            return None, None, None, None
        _, PLACA[0], cat, listo = saludo_hasta_listo(v, version=2, parcial=parcial)
        pz = {x.get("id"): x for x in ET.fromstring(cat).iter("pieza")}
        ids = []
        for pid, nombre in pedidos:
            ob = [o for o in pz[pid].iter("observable") if o.get("nombre") == nombre]
            ids.append(int(ob[0].get("id_obs")))
        img_de = {}
        for pid, nombre in imagenes:
            im = [o for o in pz[pid].iter("imagen") if o.get("nombre") == nombre][0]
            img_de[int(im.get("id_obs"))] = (pid, nombre)
            ids.append(int(im.get("id_obs")))
        IMAGENES[0] = {k: {"enteras": 0, "trozos": 0, "bytes": 0, "final": None, "ancho": 0,
                           "alto": 0, "vacios": 0} for k in imagenes}
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
            elif tt == T_IMAGEN and len(c) >= 24:
                ts, i, fmt, an, al, br, pix = imagen(c)
                e = IMAGENES[0][img_de[i]]
                e.update(enteras=e["enteras"] + 1, bytes=e["bytes"] + len(c), ancho=an, alto=al,
                         final=bytearray(pix))
            elif tt == T_IMAGEN_RECT and len(c) >= 32:
                ts, i, fmt, an, al, x, y, w, h, br, pix = imagen_rect(c)
                e = IMAGENES[0][img_de[i]]
                e.update(trozos=e["trozos"] + 1, bytes=e["bytes"] + len(c))
                if w * h == 0:
                    e["vacios"] += 1
                elif e["final"] is not None and an == e["ancho"] and al == e["alto"]:
                    aplica_rect(e["final"], an, x, y, w, h, pix)
            elif tt == T_FIN:
                break
        rc, out, _ = termina(p)
        ULTIMA[0] = out
        return series, avs, cat, rc
    finally:
        if p.poll() is None:
            p.kill()
        v.cierra()


def piezas_sin_dibujo(nombre):
    """Las piezas visibles y los pines de conector del XML que el SVG no tiene
    (como en sistema.py: con expresiones regulares, que los comentarios de las
    placas llevan `--valida`)"""
    with open(os.path.join("placas", nombre + ".svg"), encoding="utf-8") as f:
        ids = set(re.findall(r'id="([^"]+)"', f.read()))
    with open(os.path.join("placas", nombre + ".xml"), encoding="utf-8") as f:
        x = re.sub(r"<!--.*?-->", "", f.read(), flags=re.S)
    falta = []
    for tipo, id_, attrs in re.findall(r'<componente\s+tipo="(\w+)"\s+id="([^"]+)"([^>]*)>', x):
        if 'visible="no"' in attrs:
            continue
        if id_ not in ids:
            falta.append(id_)
        if tipo in ("Conector", "Jumper"):
            m = re.search(r'nombres="([^"]+)"', attrs)
            falta += ["%s.%s" % (id_, q) for q in (m.group(1).split() if m else [])
                      if "%s.%s" % (id_, q) not in ids]
    return falta


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


# ---------------------------------------------------------------------------
# C2 (F1a): los paso a paso y sus drivers
# ---------------------------------------------------------------------------
# El banco: las alimentaciones, EN y las MS con un mando cada una, y el
# generador de pasos
BANCO_PAP = """<placa nombre="banco-pap">
  <componente tipo="Conector" id="J" filas="1" columnas="9"
              nombres="VMOT GND VLOG EN MS1 MS2 MS3 STEP DIR"/>
  <nodo id="J.GND" bus="si"/>
  <componente tipo="Fuente" id="VMOT" v="%(vmot)s" limite_ma="5000" media_ms="20">
    <pin nombre="pin" nodo="J.VMOT"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="J.GND"/></componente>
  <componente tipo="Fuente" id="VLOG" v="%(vlog)s"><pin nombre="pin" nodo="J.VLOG"/></componente>
  <componente tipo="FuenteAjustable" id="EN" v="0" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.EN"/></componente>
  <componente tipo="FuenteAjustable" id="MS1" v="%(ms)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.MS1"/></componente>
  <componente tipo="FuenteAjustable" id="MS2" v="%(ms)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.MS2"/></componente>
  <componente tipo="FuenteAjustable" id="MS3" v="%(ms)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.MS3"/></componente>
  <componente tipo="GeneradorPasos" id="G" hz="%(hz)s" rampa_ms="%(rampa)s" ancho_us="%(ancho)s"
              dir_us="%(dir_us)s" vdd="%(vpaso)s">
    <pin nombre="step" nodo="J.STEP"/><pin nombre="dir" nodo="J.DIR"/></componente>
</placa>
"""
SISTEMA_PAP = """<sistema nombre="prueba-pap">
  <placa id="D" fichero="%(placas)s/pololu_a4988.xml"/>
  <placa id="M" fichero="%(placas)s/%(motor)s.xml"/>
  <placa id="T" fichero="banco_pap.xml"/>
  <hilo a="T/J.VMOT" b="D/J2.VMOT"/>
  <hilo a="T/J.GND"  b="D/J2.GND1"/>
  <hilo a="T/J.VLOG" b="D/J2.VLOG"/>
  <hilo a="T/J.EN"   b="D/J1.EN"/>
  <hilo a="T/J.MS1"  b="D/J1.MS1"/>
  <hilo a="T/J.MS2"  b="D/J1.MS2"/>
  <hilo a="T/J.MS3"  b="D/J1.MS3"/>
  <hilo a="T/J.STEP" b="D/J1.STEP"/>
  <hilo a="T/J.DIR"  b="D/J1.DIR"/>
%(rst)s%(bobinas)s</sistema>
"""
RST_SLP = '  <hilo a="D/J1.RST" b="D/J1.SLP"/>\n'
BOBINAS = """  <hilo a="M/M.A1" b="D/J2.1A"/>
  <hilo a="M/M.A2" b="D/J2.1B"/>
  <hilo a="M/M.B1" b="D/J2.2A"/>
  <hilo a="M/M.B2" b="D/J2.2B"/>
"""


def textos(avs):
    """Los avisos de una ventana, en un texto: origen y texto de cada uno"""
    return " | ".join("%s: %s" % (a[2], a[3]) for a in (avs or []))


def linea(out, cabeza):
    k = out.find(cabeza)
    return out[k:out.find("\n", k)].strip() if k >= 0 else ""


def numero_tras(lin, antes):
    k = lin.find(antes)
    if k < 0:
        return None
    t = lin[k + len(antes):].split()[0].rstrip(",;")
    try:
        return float(t)
    except ValueError:
        return None


def c2_pap(sim, cp):
    placas = os.path.abspath("placas")
    grupo("C2 (F1a) El NEMA 17 de la 3018 tras el modulo Pololu del A4988")
    for placa, cab in (("nema17_3018", "placa 'nema17-3018': SIN MCU, 2 componentes, 4 nodos, 0 avisos"),
                       ("st5918l3008", "placa 'st5918l3008': SIN MCU, 2 componentes, 4 nodos, 0 avisos"),
                       ("pololu_a4988", "placa 'pololu-a4988': SIN MCU, 4 componentes, 15 nodos, "
                        "0 avisos")):
        rc, out = valida(sim, "placas/%s.xml" % placa)
        f = piezas_sin_dibujo(placa)
        check(rc == 0 and cab in out and ("dibujo: %s.svg (" % placa) in out and not f,
              "placas/%s.xml se valida sin avisos, con su dibujo: cada pieza visible y cada pin "
              "de conector, con su id%s" % (placa, " (faltan: %s)" % f if f else ""))

    def banco(vmot=12, vlog=5, ms=5, hz=3200, rampa=100, ancho=2, dir_us=5, vpaso=5,
              motor="nema17_3018", rst=RST_SLP, bobinas=BOBINAS, nombre="pap"):
        cp("banco_pap.xml", BANCO_PAP % {"vmot": vmot, "vlog": vlog, "ms": ms, "hz": hz,
                                         "rampa": rampa, "ancho": ancho, "dir_us": dir_us,
                                         "vpaso": vpaso})
        return cp(nombre + ".xml", SISTEMA_PAP % {"placas": placas, "motor": motor, "rst": rst,
                                                   "bobinas": bobinas})

    # 1/16 a 3200 Hz: una vuelta por segundo, 60 rpm
    ruta = banco()
    series, avs, cat, rc = muestrea(sim, ruta, 1500, [("M/MOTOR", "rpm"), ("M/MOTOR", "corriente_a"),
                                                      ("M/MOTOR", "corriente_b"),
                                                      ("M/MOTOR", "pasos_perdidos"),
                                                      ("D/U1", "micropasos"), ("D/U1", "pasos"),
                                                      ("D/U1", "corriente_vmot"),
                                                      ("T/VMOT", "corriente")])
    ok = series is not None and rc == 0
    check(ok, "el banco corre 1500 ms: la Pololu, el NEMA 17 y el generador")
    if not ok:
        return
    pz = {x.get("id"): x for x in ET.fromstring(cat).iter("pieza")}
    mo = [o.get("nombre") for o in pz["M/MOTOR"].iter("observable")]
    dr = [o.get("nombre") for o in pz["D/U1"].iter("observable")]
    md = [m.get("nombre") for m in pz["D/U1"].iter("mando")]
    check(mo == ["angulo", "rpm", "corriente_a", "corriente_b", "par", "retraso", "pasos_perdidos",
                 "pierde_pasos"] and
          dr == ["corriente_1", "corriente_2", "micropasos", "pasos", "corriente_vmot",
                 "temperatura", "sobretemperatura", "sin_tension"] and md == ["vref"],
          "en el catalogo, el motor deja ver su angulo, rpm, sus dos corrientes, el par, el "
          "retraso y los pasos perdidos; el driver, sus consignas, el micropaso, los pasos, lo "
          "que gasta y su temperatura; su mando es el potenciometro de Vref")
    rpm = [v for t, v in series[("M/MOTOR", "rpm")] if t > 0.6]
    media = sum(rpm) / len(rpm)
    check(abs(media - 60.0) < 1.0,
          "con las MS a 5 V va a 1/16, y a 3200 Hz da una vuelta por segundo: %.2f rpm de media"
          % media)
    ia = max(abs(v) for t, v in series[("M/MOTOR", "corriente_a")] if t > 0.6)
    ib = max(abs(v) for t, v in series[("M/MOTOR", "corriente_b")] if t > 0.6)
    check(abs(ia - 1103) < 30 and abs(ib - 1103) < 30,
          "la corriente de cada fase sube y baja con la consigna: de pico, %.0f y %.0f mA, los "
          "0,6 V / (8 x 0,068 ohm) del potenciometro" % (ia, ib))
    perd = series[("M/MOTOR", "pasos_perdidos")][-1][1]
    mp = series[("D/U1", "micropasos")][-1][1]
    check(perd == 0 and mp == 16, "sin perder un paso, a 1/16 (%g)" % mp)
    rc, out = corre(sim, ruta, 1500)
    lm = linea(out, "PASO A PASO M/MOTOR:")
    ld = linea(out, "DRIVER D/U1 (A4988):")
    lg = linea(out, "GENERADOR T/G:")
    pasos_m = numero_tras(lm, "rpm, ")
    pasos_g = numero_tras(lg, "Hz, ")
    check(pasos_m is not None and pasos_g is not None and abs(pasos_m - pasos_g / 16.0) < 0.6,
          "y al final, el rotor esta donde lo han llevado: %s pasos enteros por %s micropasos del "
          "generador (%s)" % (pasos_m, pasos_g, lm))
    check("activo, 1/16" in ld and "corriente" in lm,
          "la consola dice como queda cada uno: %s" % ld)
    ivm = [v for t, v in series[("D/U1", "corriente_vmot")] if t > 0.6]
    fuente = [v for t, v in series[("T/VMOT", "corriente")] if t > 0.6]
    check(ivm and 40 < sum(ivm) / len(ivm) < 400 and fuente and
          abs(sum(fuente) / len(fuente) - sum(ivm) / len(ivm)) < 0.25 * sum(ivm) / len(ivm),
          "lo que pide a VMOT es lo que gastan las bobinas y sus transistores, de media: %.0f mA, "
          "y la fuente lo ve (%.0f mA): el chopper no se simula, pero se paga"
          % (sum(ivm) / len(ivm), sum(fuente) / len(fuente)))
    check("fuera de especificacion" not in textos(avs),
          "con el generador a 5 V y VDD a 5 V, ningun nivel dudoso")

    grupo("C2 (F1a) Los errores de cableado y de temporizacion, cada uno con su aviso")

    def avisos_de(ruta, ms=300):
        rc, out = corre(sim, ruta, ms)
        return rc, out

    casos = [
        ("3,3 V con VDD a 5 V", dict(vpaso=3.3),
         "en step, y el A4988 pide 3.50 V para un alto y 1.50 para un bajo con su VDD: fuera de "
         "especificacion (lo lee alto)"),
        ("RESET al aire", dict(rst=""), "RESET esta al aire. En el modulo de Pololu no lleva "
         "resistencia: se une a SLEEP, o a VDD"),
        ("un pulso de 0,5 us", dict(ancho=0.5), "STEP sube 500 ns y el minimo es 1000 ns"),
        ("VMOT a 6 V", dict(vmot=6), "6.0 V en VMOT, y el A4988 pide de 8.00 a 35 V"),
        ("la bobina B al aire", dict(bobinas=BOBINAS.replace('  <hilo a="M/M.B1" b="D/J2.2A"/>\n', '')
                                     .replace('  <hilo a="M/M.B2" b="D/J2.2B"/>\n', '')),
         "la bobina B esta al aire: con una fase sola el motor no gira"),
        ("la bobina A entre las dos fases", dict(bobinas=BOBINAS.replace('b="D/J2.1B"', 'b="D/J2.2B"')
                                                 .replace('<hilo a="M/M.B2" b="D/J2.2B"/>',
                                                          '<hilo a="M/M.B2" b="D/J2.1B"/>')),
         "la bobina A va entre la fase 1 de D/U1 y la fase 2 de D/U1"),
    ]
    for caso, kw, dice in casos:
        rc, out = avisos_de(banco(**kw))
        check(dice in out, "%s: «...%s»" % (caso, dice[:60]))
    # RESET al aire: no se mueve
    rc, out = corre(sim, banco(rst=""), 600)
    lm = linea(out, "PASO A PASO M/MOTOR:")
    check(abs(numero_tras(lm, "rpm, ") or 0.0) < 1.0,
          "con RESET al aire el A4988 se queda en reset y el motor no se mueve: %s" % lm)
    # EN alto: el traductor cuenta, las salidas no
    cp("banco_pap.xml", (BANCO_PAP % {"vmot": 12, "vlog": 5, "ms": 5, "hz": 3200, "rampa": 100,
                                      "ancho": 2, "dir_us": 5, "vpaso": 5})
       .replace('id="EN" v="0"', 'id="EN" v="5"'))
    rc, out = corre(sim, cp("en_alto.xml", SISTEMA_PAP % {"placas": placas, "motor": "nema17_3018",
                                                          "rst": RST_SLP, "bobinas": BOBINAS}), 600)
    lm, ld = linea(out, "PASO A PASO M/MOTOR:"), linea(out, "DRIVER D/U1 (A4988):")
    check("salidas apagadas" in ld and abs(numero_tras(lm, "rpm, ") or 0.0) < 0.6 and
          (numero_tras(ld, "1/16, ") or 0) > 1000,
          "EN alto, que es apagado: el traductor cuenta los pasos, pero el motor no se mueve "
          "(%s; %s)" % (ld, lm))
    # La bobina A al reves: gira al reves
    rc, out = corre(sim, banco(bobinas=BOBINAS.replace('a="M/M.A1" b="D/J2.1A"', 'a="M/M.A1" b="D/J2.1B"')
                               .replace('a="M/M.A2" b="D/J2.1B"', 'a="M/M.A2" b="D/J2.1A"')), 600)
    lm = linea(out, "PASO A PASO M/MOTOR:")
    check((numero_tras(lm, "rpm, ") or 0) < -30,
          "la bobina A al reves: el motor gira AL REVES (%s)" % lm)
    # DIR tarde: un cambio de sentido con 50 ns de margen
    cp("banco_pap.xml", (BANCO_PAP % {"vmot": 12, "vlog": 5, "ms": 5, "hz": 3200, "rampa": 20,
                                      "ancho": 2, "dir_us": 0.05, "vpaso": 5}))
    ruta = cp("dir.xml", SISTEMA_PAP % {"placas": placas, "motor": "nema17_3018", "rst": RST_SLP,
                                        "bobinas": BOBINAS})
    series, avs, cat, rc = muestrea(sim, ruta, 400, [("M/MOTOR", "rpm")],
                                    lista=[(150, "T/G", "sentido", 0.0)])
    textos_ = textos(avs)
    check("DIR cambia 50 ns antes del flanco de STEP, y necesita 200 ns" in textos_,
          "DIR cambia 50 ns antes del paso, y el A4988 pide 200: se avisa")
    # La proteccion termica: Vref alto, sin disipador, parado
    cp("banco_pap.xml", (BANCO_PAP % {"vmot": 12, "vlog": 5, "ms": 0, "hz": 0, "rampa": 0,
                                      "ancho": 2, "dir_us": 5, "vpaso": 5}))
    ruta = cp("termica.xml", SISTEMA_PAP % {"placas": placas, "motor": "nema17_3018",
                                            "rst": RST_SLP, "bobinas": BOBINAS})
    series, avs, cat, rc = muestrea(sim, ruta, 14000, [("D/U1", "temperatura"),
                                                       ("D/U1", "sobretemperatura"),
                                                       ("M/MOTOR", "corriente_a")],
                                    periodo_ns=50000000, lista=[(1, "D/U1", "vref", 1.2)])
    textos_ = textos(avs)
    tsd = [t for t, v in (series or {}).get(("D/U1", "sobretemperatura"), []) if v == 1.0]
    tj = max(v for t, v in series[("D/U1", "temperatura")]) if series else 0
    vuelve = [t for t, v in series[("D/U1", "sobretemperatura")] if tsd and t > tsd[0] and v == 0.0] \
        if series else []
    fin_tsd = vuelve[0] if vuelve else 1e9
    ia_tsd = [abs(v) for t, v in series[("M/MOTOR", "corriente_a")] if tsd and t > tsd[0] + 0.02
              and t < fin_tsd - 0.02] if series else []
    check(tsd and 160 <= tj < 172 and "la proteccion termica apaga las salidas" in textos_ and
          ia_tsd and max(ia_tsd) < 1.0,
          "con Vref a 1,2 V (2,2 A, que el A4988 deja en 2) y sin disipador, parado: a los "
          "%.1f s pasa de 165 °C, la "
          "proteccion termica apaga las salidas y la corriente se va" % (tsd[0] if tsd else -1))
    check(bool(vuelve), "y al bajar 15 °C vuelve a encenderlas, a los %.1f s"
          % (vuelve[0] if vuelve else -1))

    grupo("C2 (F1a) La curva de par maximo del ST5918 tras un DM542, a 24 y a 48 V")
    curva = {}
    for vbus in (24, 48):
        for rpm in (120, 600, 900, 1200, 1800):
            curva[(vbus, rpm)] = pull_out(sim, cp, vbus, rpm)
    tabla = "; ".join("%d V: %s" % (vb, ", ".join("%d rpm %.2f" % (r, curva[(vb, r)])
                                                     for r in (120, 600, 900, 1200, 1800)))
                      for vb in (24, 48))
    bajo = curva[(24, 120)]
    check(0.55 * 1.8668 < bajo < 0.72 * 1.8668 and abs(curva[(48, 120)] - bajo) < 0.05,
          "a 120 rpm, %.2f N·m con 24 y con 48 V: Km·I = 0,31 x 4,2 = 1,31, menos sus "
          "rozamientos y el par sin corriente, el 62 %% del de retencion" % bajo)
    mono = all(curva[(vb, b)] <= curva[(vb, a)] + 0.04 for vb in (24, 48)
               for a, b in ((120, 600), (600, 900), (900, 1200), (1200, 1800)))
    check(mono, "el par cae con la velocidad: %s" % tabla)
    check(all(curva[(48, r)] >= curva[(24, r)] - 0.03 for r in (600, 900, 1200, 1800)) and
          curva[(48, 1200)] > 3 * curva[(24, 1200)],
          "y a 48 V aguanta mucho mas: a 1200 rpm, %.2f N·m contra %.2f"
          % (curva[(48, 1200)], curva[(24, 1200)]))

    def esquina(vb):
        ref = 0.7 * curva[(vb, 120)]
        pts = [(r, curva[(vb, r)]) for r in (120, 600, 900, 1200, 1800)]
        for (r0, t0), (r1, t1) in zip(pts, pts[1:]):
            if t1 < ref <= t0:
                return r0 + (r1 - r0) * (t0 - ref) / (t0 - t1)
        return None
    e24, e48 = esquina(24), esquina(48)
    check(e24 and e48 and 450 < e24 < 950 and 1000 < e48 < 1800 and 1.5 < e48 / e24 < 2.6,
          "la esquina -el 70 %% del par de abajo-: a %s rpm con 24 V y a %s con 48, casi el "
          "doble (el analisis estimaba unas 800 y 1600)"
          % ("%.0f" % e24 if e24 else "?", "%.0f" % e48 if e48 else "?"))

    grupo("C2 (F1a) Los pasos perdidos, el modelo ideal, los otros drivers y lo que cuesta")
    # Una rampa demasiado corta: a 1000 rpm en 5 ms
    for rampa, pierde in ((5, True), (400, False)):
        rc, out = corre(sim, dm542(cp, 24, 6667, rampa), 900)
        lm = linea(out, "PASO A PASO M/MOTOR:")
        n = int(lm.split("pasos, ")[1].split()[0]) if "pasos, " in lm else None
        if pierde:
            check(n and n != 0 and "ha perdido pasos" in out,
                  "a 1000 rpm con una rampa de 5 ms el rotor se descuelga: %d pasos perdidos, y "
                  "se avisa" % (n or 0))
        else:
            check(n == 0 and abs(numero_tras(lm, "PASO A PASO M/MOTOR: ") or 0) > 900,
                  "con 400 ms de rampa llega, y no pierde ninguno (%s)" % lm)
    # El modelo ideal: exactamente los pasos
    texto = open(os.path.join(placas, "nema17_3018.xml"), encoding="utf-8").read()
    cp("nema17_3018.xml", texto.replace('id="MOTOR" ', 'id="MOTOR" modelo="ideal" '))
    for svg in ("nema17_3018", "pololu_a4988"):
        cp(svg + ".svg", open(os.path.join(placas, svg + ".svg"), encoding="utf-8").read())
    cp("pololu_a4988.xml", open(os.path.join(placas, "pololu_a4988.xml"), encoding="utf-8").read())
    cp("banco_pap.xml", BANCO_PAP % {"vmot": 12, "vlog": 5, "ms": 5, "hz": 32000, "rampa": 0,
                                     "ancho": 2, "dir_us": 5, "vpaso": 5})
    sis = SISTEMA_PAP % {"placas": os.path.dirname(cp("x", "")), "motor": "nema17_3018",
                         "rst": RST_SLP, "bobinas": BOBINAS}
    rc, out = corre(sim, cp("ideal.xml", sis), 500)
    lm = linea(out, "PASO A PASO M/MOTOR:")
    ld = linea(out, "DRIVER D/U1 (A4988):")
    pm, pd = numero_tras(lm, "rpm, "), numero_tras(ld, "1/16, ")
    check("ideal" in lm and pm is not None and pd is not None and abs(pm - pd / 16.0) < 0.0626 and
          abs(pm * 16 - round(pm * 16)) < 0.08 and "0 perdidos" in lm,
          "con modelo=\"ideal\", el rotor da exactamente un micropaso por paso, aunque arranque de "
          "golpe a 600 rpm: %s pasos enteros por %s micropasos del driver (el primero llega en "
          "t = 0, antes de que el motor vea la posicion de casa)" % (pm, pd))
    # El DRV8825 y el TMC2209
    for modelo, pines, dice in (
            ("DRV8825", '<pin nombre="vdd" nodo="X"/>', "el DRV8825 no tiene vdd"),
            ("TMC2209", '<pin nombre="ms3" nodo="X"/>', "el TMC2209 no tiene ms3"),
            ("DM542", '', "el DM542 no tiene ms1: el micropaso y la "
             "corriente van con sus interruptores")):
        rc, out = valida(sim, cp("mal_%s.xml" % modelo, otro_driver(modelo, extra=pines)))
        check(rc != 0 and dice in out, "%s con una patilla que no tiene: error (%s)" % (modelo, dice))
    for modelo, ms, micro in (("DRV8825", (3.3, 0, 3.3), 32), ("DRV8825", (0, 3.3, 0), 4),
                              ("TMC2209", (3.3, 0, 0), 32), ("TMC2209", (0, 0, 0), 8),
                              ("TMC2209", (3.3, 3.3, 0), 16)):
        rc, out = corre(sim, cp("ms.xml", otro_driver(modelo, ms=ms)), 50)
        ld = linea(out, "DRIVER U1 (%s):" % modelo)
        check(("1/%d," % micro) in ld and "fuera de especificacion" not in out,
              "%s con MS = %s: 1/%d, y sus entradas aceptan 3,3 V sin aviso (%s)"
              % (modelo, "".join("1" if x else "0" for x in ms[:(3 if modelo == "DRV8825" else 2)]),
                 micro, ld))
    # Tras un L298: las bobinas, con Thevenin
    rc, out = corre(sim, cp("l298.xml", PAP_L298 % {"in1": 3.3, "in2": 0, "in3": 0, "in4": 0}), 300)
    lm = linea(out, "PASO A PASO MOT:")
    a0 = numero_tras(lm, "rpm, ")
    rc, out = corre(sim, cp("l298.xml", PAP_L298 % {"in1": 0, "in2": 0, "in3": 3.3, "in4": 0}), 300)
    lm2 = linea(out, "PASO A PASO MOT:")
    a1 = numero_tras(lm2, "rpm, ")
    check(a0 is not None and a1 is not None and abs(a0) < 0.05 and abs(a1 - 1.0) < 0.05 and
          "A tension" in lm and "B tension" in lm2,
          "tras un L298, cada bobina es una rama de Thevenin, como un motor de continua: con "
          "la A, el rotor en 0; con la B, un paso a la derecha (%s; %s)" % (lm, lm2))
    # Lo que cuesta: un eje moviendose, 1/8 a 4 kHz -20 mm/s con un husillo de 8 mm-
    t0 = time.time()
    rc, out = corre(sim, dm542(cp, 24, 3200, 50, micro=8), 3000)
    dt = time.time() - t0
    cuesta(dt if rc == 0 else float("inf"), 1.5,
           "y lo que cuesta: un eje con su DM542, a 1/8 y 3200 Hz, 3 s simulados en %.2f s de "
           "reloj (%.2f s por segundo simulado, con el arranque de mcu-sim)" % (dt, dt / 3.0))


# El ST5918 tras un DM542: el driver en la placa del banco
BANCO_DM542 = """<placa nombre="banco-dm542">
  <componente tipo="Conector" id="M" filas="1" columnas="4" nombres="A1 A2 B1 B2"/>
  <nodo id="VMOT" externo="si"/>
  <nodo id="GND" externo="si" bus="si"/>
  <nodo id="STEP" externo="si"/>
  <nodo id="DIR" externo="si"/>
  <nodo id="M.A1" bus="si"/><nodo id="M.A2" bus="si"/><nodo id="M.B1" bus="si"/><nodo id="M.B2" bus="si"/>
  <componente tipo="Fuente" id="PSU" v="%(vbus)s" limite_ma="10000" media_ms="20">
    <pin nombre="pin" nodo="VMOT"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="GND"/></componente>
  <componente tipo="DriverPaP" id="U1" modelo="DM542" micropasos="%(micro)s" corriente_a="4.2">
    <pin nombre="vmot" nodo="VMOT"/><pin nombre="gnd" nodo="GND"/>
    <pin nombre="step" nodo="STEP"/><pin nombre="dir" nodo="DIR"/>
    <pin nombre="out1a" nodo="M.A1"/><pin nombre="out1b" nodo="M.A2"/>
    <pin nombre="out2a" nodo="M.B1"/><pin nombre="out2b" nodo="M.B2"/>
  </componente>
  <componente tipo="GeneradorPasos" id="G" hz="%(hz)s" rampa_ms="%(rampa)s" ancho_us="5" vdd="5">
    <pin nombre="step" nodo="STEP"/><pin nombre="dir" nodo="DIR"/></componente>
</placa>
"""
SISTEMA_DM542 = """<sistema nombre="prueba-dm542">
  <placa id="B" fichero="banco_dm542.xml"/>
  <placa id="M" fichero="%(placas)s/st5918l3008.xml"/>
  <hilo a="M/M.A1" b="B/M.A1"/>
  <hilo a="M/M.A2" b="B/M.A2"/>
  <hilo a="M/M.B1" b="B/M.B1"/>
  <hilo a="M/M.B2" b="B/M.B2"/>
</sistema>
"""


def dm542(cp, vbus, hz, rampa, micro=2):
    cp("banco_dm542.xml", BANCO_DM542 % {"vbus": vbus, "hz": hz, "rampa": rampa, "micro": micro})
    return cp("dm542.xml", SISTEMA_DM542 % {"placas": os.path.abspath("placas")})


def pull_out(sim, cp, vbus, rpm):
    """El par maximo a `rpm`: llega con su rampa y, desde ahi, la carga sube un
    1 % del de retencion cada 30 ms hasta que pierde pasos. A medio paso."""
    hz = rpm * 200 * 2 / 60.0
    rampa = max(50.0, rpm * 0.3)                     # ms
    ruta = dm542(cp, vbus, hz, rampa)
    t0 = rampa + 200
    lista = [(t0 + 30 * k, "M/MOTOR", "carga", float(k)) for k in range(1, 100)]
    series, avs, cat, rc = muestrea(sim, ruta, int(t0 + 30 * 100), [("M/MOTOR", "pasos_perdidos")],
                                    periodo_ns=1000000, lista=lista)
    if not series:
        return 0.0
    p0 = None
    for t, v in series[("M/MOTOR", "pasos_perdidos")]:
        if t * 1e3 < t0:
            p0 = v
            continue
        if p0 is not None and v != p0:
            k = int((t * 1e3 - t0) // 30)            # la carga que tenia: k %
            return max(0, k - 1) * 0.01 * 1.8668
    return 1.8668


def otro_driver(modelo, extra="", ms=(0, 0, 0)):
    """Una placa con un DriverPaP de otro modelo, sus patillas y el NEMA 17"""
    vdd = '<pin nombre="vdd" nodo="VLOG"/>' if modelo == "TMC2209" else ""
    ms3 = '<pin nombre="ms3" nodo="MS3"/><pin nombre="rst" nodo="VLOG"/><pin nombre="slp" nodo="VLOG"/>' \
        if modelo == "DRV8825" else ""
    return """<placa nombre="otro">
  <nodo id="VMOT" externo="si"/><nodo id="GND" externo="si" bus="si"/><nodo id="VLOG" externo="si" bus="si"/>
  <nodo id="EN" externo="si"/><nodo id="MS1" externo="si"/><nodo id="MS2" externo="si"/>
  <nodo id="MS3" externo="si"/><nodo id="STEP" externo="si"/><nodo id="DIR" externo="si"/>
  <nodo id="X" externo="si"/>
  <nodo id="BA1" externo="si" bus="si"/><nodo id="BA2" externo="si" bus="si"/>
  <nodo id="BB1" externo="si" bus="si"/><nodo id="BB2" externo="si" bus="si"/>
  <componente tipo="Fuente" id="PV" v="12"><pin nombre="pin" nodo="VMOT"/></componente>
  <componente tipo="Fuente" id="PL" v="3.3"><pin nombre="pin" nodo="VLOG"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="GND"/></componente>
  <componente tipo="Gnd" id="GEN"><pin nombre="pin" nodo="EN"/></componente>
  <componente tipo="Fuente" id="F1" v="%s"><pin nombre="pin" nodo="MS1"/></componente>
  <componente tipo="Fuente" id="F2" v="%s"><pin nombre="pin" nodo="MS2"/></componente>
  <componente tipo="Fuente" id="F3" v="%s"><pin nombre="pin" nodo="MS3"/></componente>
  <componente tipo="DriverPaP" id="U1" modelo="%s">
    <pin nombre="vmot" nodo="VMOT"/><pin nombre="gnd" nodo="GND"/>%s%s
    <pin nombre="en" nodo="EN"/><pin nombre="ms1" nodo="MS1"/><pin nombre="ms2" nodo="MS2"/>
    <pin nombre="step" nodo="STEP"/><pin nombre="dir" nodo="DIR"/>%s
    <pin nombre="out1a" nodo="BA1"/><pin nombre="out1b" nodo="BA2"/>
    <pin nombre="out2a" nodo="BB1"/><pin nombre="out2b" nodo="BB2"/>
  </componente>
  <componente tipo="GeneradorPasos" id="G" hz="1000" vdd="3.3">
    <pin nombre="step" nodo="STEP"/><pin nombre="dir" nodo="DIR"/></componente>
  <componente tipo="MotorPaP" id="MOT">
    <pin nombre="a1" nodo="BA1"/><pin nombre="a2" nodo="BA2"/>
    <pin nombre="b1" nodo="BB1"/><pin nombre="b2" nodo="BB2"/>
  </componente>
</placa>
""" % (ms[0], ms[1], ms[2], modelo, vdd, ms3, extra)


# Un paso a paso tras un L298: cuatro tensiones en IN1..IN4, los dos puentes
# habilitados, y una bobina en cada uno
PAP_L298 = """<placa nombre="pap-l298">
  <nodo id="VS" externo="si"/><nodo id="V5" externo="si"/><nodo id="GND" externo="si" bus="si"/>
  <nodo id="IN1" externo="si"/><nodo id="IN2" externo="si"/><nodo id="IN3" externo="si"/>
  <nodo id="IN4" externo="si"/>
  <nodo id="O1" externo="si" bus="si"/><nodo id="O2" externo="si" bus="si"/>
  <nodo id="O3" externo="si" bus="si"/><nodo id="O4" externo="si" bus="si"/>
  <componente tipo="Fuente" id="PS" v="12"><pin nombre="pin" nodo="VS"/></componente>
  <componente tipo="Fuente" id="P5" v="5"><pin nombre="pin" nodo="V5"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="GND"/></componente>
  <componente tipo="Fuente" id="F1" v="%(in1)s"><pin nombre="pin" nodo="IN1"/></componente>
  <componente tipo="Fuente" id="F2" v="%(in2)s"><pin nombre="pin" nodo="IN2"/></componente>
  <componente tipo="Fuente" id="F3" v="%(in3)s"><pin nombre="pin" nodo="IN3"/></componente>
  <componente tipo="Fuente" id="F4" v="%(in4)s"><pin nombre="pin" nodo="IN4"/></componente>
  <componente tipo="L298" id="U1" diodos="si">
    <pin nombre="vs" nodo="VS"/><pin nombre="vss" nodo="V5"/><pin nombre="gnd" nodo="GND"/>
    <pin nombre="ena" nodo="V5"/><pin nombre="enb" nodo="V5"/>
    <pin nombre="in1" nodo="IN1"/><pin nombre="in2" nodo="IN2"/>
    <pin nombre="in3" nodo="IN3"/><pin nombre="in4" nodo="IN4"/>
    <pin nombre="out1" nodo="O1"/><pin nombre="out2" nodo="O2"/>
    <pin nombre="out3" nodo="O3"/><pin nombre="out4" nodo="O4"/>
  </componente>
  <componente tipo="MotorPaP" id="MOT" r_ohm="30" l_mh="40" corriente_a="0.4" par_retencion_ncm="30">
    <pin nombre="a1" nodo="O1"/><pin nombre="a2" nodo="O2"/>
    <pin nombre="b1" nodo="O3"/><pin nombre="b2" nodo="O4"/>
  </componente>
</placa>
"""


# ---------------------------------------------------------------------------
# C3 (F1b): la continua de potencia
# ---------------------------------------------------------------------------
BANCO_CABEZAL = """<placa nombre="banco-cabezal">
  <componente tipo="Conector" id="J" filas="1" columnas="3" nombres="V24 GND SIG"/>
  <nodo id="J.GND" bus="si"/>
  <componente tipo="Fuente" id="PSU" v="24" limite_ma="10000" media_ms="50">
    <pin nombre="pin" nodo="J.V24"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="J.GND"/></componente>
  %(senal)s
</placa>
"""
SENAL_FIJA = """<componente tipo="FuenteAjustable" id="SIG" v="%s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.SIG"/></componente>"""
SENAL_PWM = """<componente tipo="ExtClock" id="SIG" hz="1000" vdd="5"><pin nombre="out" nodo="J.SIG"/></componente>"""
SISTEMA_CABEZAL = """<sistema nombre="prueba-cabezal">
  <placa id="Q" fichero="%(dir)s/modulo_mosfet.xml"/>
  <placa id="M" fichero="%(placas)s/motor_775.xml"/>
  <placa id="T" fichero="banco_cabezal.xml"/>
  <hilo a="T/J.V24" b="Q/X1.VIN"/>
  <hilo a="T/J.GND" b="Q/X1.GNDP"/>
  <hilo a="T/J.SIG" b="Q/P1.SIG"/>
  <hilo a="M/M.ROJO"  b="Q/X2.MAS"/>
  <hilo a="M/M.NEGRO" b="Q/X2.MENOS"/>
</sistema>
"""
BANCO_IBT2 = """<placa nombre="banco-ibt2">
  <componente tipo="Conector" id="J" filas="1" columnas="8"
              nombres="RPWM LPWM R_EN L_EN R_IS V5 GND V12"/>
  <nodo id="J.GND" bus="si"/>
  <componente tipo="Fuente" id="PSU" v="12" limite_ma="10000" media_ms="20">
    <pin nombre="pin" nodo="J.V12"/></componente>
  <componente tipo="Fuente" id="V5" v="5"><pin nombre="pin" nodo="J.V5"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="J.GND"/></componente>
  <componente tipo="FuenteAjustable" id="RPWM" v="%(rpwm)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.RPWM"/></componente>
  <componente tipo="FuenteAjustable" id="LPWM" v="%(lpwm)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.LPWM"/></componente>
  <componente tipo="FuenteAjustable" id="EN" v="%(en)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.R_EN"/></componente>
  <componente tipo="Resistencia" id="RIS" r="1000">
    <pin nombre="a" nodo="J.R_IS"/><pin nombre="b" nodo="J.GND"/></componente>
</placa>
"""
SISTEMA_IBT2 = """<sistema nombre="prueba-ibt2">
  <placa id="H" fichero="%(placas)s/ibt2.xml"/>
  <placa id="M" fichero="%(placas)s/motor_jgb37_encoder.xml"/>
  <placa id="T" fichero="banco_ibt2.xml"/>
  <hilo a="T/J.RPWM" b="H/P1.RPWM"/>
  <hilo a="T/J.LPWM" b="H/P1.LPWM"/>
  <hilo a="T/J.R_EN" b="H/P1.R_EN"/>
  <hilo a="T/J.R_EN" b="H/P1.L_EN"/>
  <hilo a="T/J.R_IS" b="H/P1.R_IS"/>
  <hilo a="T/J.V5"   b="H/P1.VCC"/>
  <hilo a="T/J.GND"  b="H/P1.GND"/>
  <hilo a="T/J.V12"  b="H/X1.BMAS"/>
  <hilo a="M/M.ROJO"   b="H/X2.MMAS"/>
  <hilo a="M/M.BLANCO" b="H/X2.MMENOS"/>
</sistema>
"""


def c3_continua(sim, cp):
    placas = os.path.abspath("placas")
    grupo("C3 (F1b) El cabezal: un motor 775 a 24 V tras un modulo MOSFET")
    for placa, cab in (("motor_775", "placa 'motor-775': SIN MCU, 2 componentes, 2 nodos, 0 avisos"),
                       ("modulo_mosfet", "placa 'modulo-mosfet': SIN MCU, 5 componentes, 4 nodos, 0 avisos"),
                       ("ibt2", "placa 'ibt2': SIN MCU, 5 componentes, 11 nodos, 0 avisos"),
                       ("motor_jgb37_encoder", "placa 'motor-jgb37-encoder': SIN MCU, 3 componentes, "
                        "6 nodos, 0 avisos")):
        rc, out = valida(sim, "placas/%s.xml" % placa)
        f = piezas_sin_dibujo(placa)
        check(rc == 0 and cab in out and ("dibujo: %s.svg (" % placa) in out and not f,
              "placas/%s.xml se valida sin avisos, con su dibujo: cada pieza visible y cada pin "
              "de conector, con su id%s" % (placa, " (faltan: %s)" % f if f else ""))
    mosfet = open(os.path.join(placas, "modulo_mosfet.xml"), encoding="utf-8").read()

    def cabezal(senal, modulo=mosfet, ms=800):
        cp("banco_cabezal.xml", BANCO_CABEZAL % {"senal": senal})
        cp("modulo_mosfet.xml", modulo)
        cp("modulo_mosfet.svg", open(os.path.join(placas, "modulo_mosfet.svg"), encoding="utf-8").read())
        ruta = cp("cabezal.xml", SISTEMA_CABEZAL % {"placas": placas,
                                                     "dir": os.path.dirname(cp("x", ""))})
        return corre(sim, ruta, ms)

    rc, out = cabezal(SENAL_FIJA % 5)
    lm, lq = linea(out, "MOTOR M/MOTOR:"), linea(out, "MOSFET Q/Q1 (IRLZ44N):")
    rpm5 = numero_tras(lm, "MOTOR M/MOTOR: ")
    check(rc == 0 and rpm5 and 9600 < rpm5 < 10000 and "conduce" in lq,
          "con 5 V en SIG, el IRLZ44N se abre y el 775 va a %.0f rpm: las 10 000 de su anuncio, "
          "menos lo que cae en la fuente y en los 25 mohm del MOSFET (%s)" % (rpm5 or 0, lq))
    rc, out = cabezal(SENAL_FIJA % 3.3)
    rpm33 = numero_tras(linea(out, "MOTOR M/MOTOR:"), "MOTOR M/MOTOR: ")
    check(rpm33 and abs(rpm33 - rpm5) < 30 and "no se abre" not in out,
          "con 3,3 V, como lo da un STM32, casi igual (%.0f rpm): el IRLZ44N es de nivel logico"
          % (rpm33 or 0))
    rc, out = cabezal(SENAL_PWM)
    lm = linea(out, "MOTOR M/MOTOR:")
    rpm_pwm = numero_tras(lm, "MOTOR M/MOTOR: ")
    check(rc == 0 and rpm_pwm and 0.9 * rpm5 < rpm_pwm < rpm5 and "sin diodo" not in out,
          "con un PWM de 1 kHz al 50 %% y en vacio, CASI A TODA VELOCIDAD (%.0f rpm), como en el "
          "taller: con L/R = 0,25 ms la corriente se acaba en cada hueco, y mientras no hay "
          "corriente el motor no frena; el PWM regula el par, no la velocidad" % (rpm_pwm or 0))

    def cabezal_cargado(senal):
        cp("banco_cabezal.xml", BANCO_CABEZAL % {"senal": senal})
        cp("modulo_mosfet.xml", mosfet)
        ruta = cp("cabezal.xml", SISTEMA_CABEZAL % {"placas": placas,
                                                     "dir": os.path.dirname(cp("x", ""))})
        s, avs, cat, rc = muestrea(sim, ruta, 800, [("M/MOTOR", "rpm"), ("M/MOTOR", "corriente")],
                                   periodo_ns=50000000, lista=[(1, "M/MOTOR", "carga", 10)])
        if not s or not s[("M/MOTOR", "rpm")]:
            return None, None
        return s[("M/MOTOR", "rpm")][-1][1], s[("M/MOTOR", "corriente")][-1][1]

    r_lleno, i_lleno = cabezal_cargado(SENAL_FIJA % 5)
    r_medio, i_medio = cabezal_cargado(SENAL_PWM.replace('hz="1000"', 'hz="20000"'))
    check(r_lleno and r_medio and 0.35 * r_lleno < r_medio < 0.5 * r_lleno and
          abs(i_medio - i_lleno) < 0.05 * i_lleno,
          "con el 10 %% del par de bloqueo, la corriente ya no se acaba: un PWM de 20 kHz al 50 %% "
          "lo deja a %.0f rpm de las %.0f a tope -la mitad de la tension, menos la caida en el "
          "bobinado y en el diodo-, con la misma corriente, %.0f mA" % (r_medio or 0, r_lleno or 0,
                                                                     i_medio or 0))
    sin = mosfet.replace('diodo="si"', 'diodo="no"').replace('    <pin nombre="libre"    nodo="VMAS"/>\n', '')
    rc, out = cabezal(SENAL_PWM, modulo=sin, ms=200)
    check("se abre con" in out and "sin diodo de libre circulacion" in out and
          "en una placa de verdad lo rompe" in out,
          "SIN EL DIODO, en el primer hueco del PWM: «...sin diodo de libre circulacion: la bobina "
          "del motor lo sube hasta la avalancha del MOSFET, y en una placa de verdad lo rompe»")
    irf = mosfet.replace('modelo="IRLZ44N"', 'modelo="IRF520"')
    rc, out = cabezal(SENAL_FIJA % 5, modulo=irf)
    rpm_irf = numero_tras(linea(out, "MOTOR M/MOTOR:"), "MOTOR M/MOTOR: ")
    check("no se abre" not in out and "pide 10.0 para abrirse del todo" in out and rpm_irf and
          rpm_irf < rpm5 - 100,
          "con el IRF520 de los modulos azules y 5 V en la puerta: se avisa de que no se abre del "
          "todo, y el motor va mas despacio (%.0f rpm)" % (rpm_irf or 0))

    grupo("C3 (F1b) El IBT-2: dos BTS7960 con el motorreductor de 37 mm")

    def ibt2(rpwm, lpwm, en=5.0, carga=None, ms=600):
        cp("banco_ibt2.xml", BANCO_IBT2 % {"rpwm": rpwm, "lpwm": lpwm, "en": en})
        ruta = cp("ibt2.xml", SISTEMA_IBT2 % {"placas": placas})
        if carga is None:
            return corre(sim, ruta, ms)
        s, avs, cat, rc = muestrea(sim, ruta, ms, [("M/MOTOR", "rpm"), ("M/MOTOR", "corriente")],
                                   periodo_ns=10000000, lista=[(1, "M/MOTOR", "carga", carga)])
        return s, avs

    rc, out = ibt2(5, 0)
    lm, l1, l2 = linea(out, "MOTOR M/MOTOR:"), linea(out, "BTS7960 H/U1:"), linea(out, "BTS7960 H/U2:")
    r_adel = numero_tras(lm, "MOTOR M/MOTOR: ")
    check(r_adel and 320 < r_adel < 333 and "alto" in l1 and "bajo" in l2,
          "RPWM alto y LPWM bajo, con los dos EN: M+ a B+ y M- a masa, hacia delante, a %.0f rpm "
          "-las 333 del anuncio a 12 V, menos los miliohmios del puente- (%s; %s)" % (r_adel or 0, l1, l2))
    rc, out = ibt2(0, 5)
    r_atras = numero_tras(linea(out, "MOTOR M/MOTOR:"), "MOTOR M/MOTOR: ")
    check(r_atras and abs(r_atras + r_adel) < 2, "al reves, al reves: %.0f rpm" % (r_atras or 0))
    rc, out = ibt2(5, 0, en=0)
    r_dormido = numero_tras(linea(out, "MOTOR M/MOTOR:"), "MOTOR M/MOTOR: ")
    check("dormido" in linea(out, "BTS7960 H/U1:") and r_dormido is not None and
          abs(r_dormido) < 0.5,
          "con los EN bajos, los dos duermen y el motor no se mueve")
    s, avs = ibt2(5, 0, carga=50.0)
    i = [v for t, v in s[("M/MOTOR", "corriente")] if t > 0.4]
    i_m = sum(i) / len(i)
    check(i_m > 500, "con una carga del 50 %% del par de bloqueo pide %.0f mA" % i_m)
    lis = linea(ULTIMA[0], "BTS7960 H/U1:")
    i_is = numero_tras(lis, "IS ")
    i_out = numero_tras(lis, "alto, ")
    check(i_is is not None and i_out and
          abs(i_is - i_out * 1e3 / 8500.0) < 0.05 * abs(i_out * 1e3 / 8500.0) + 0.2,
          "R_IS es una fuente de corriente: %.1f uA por %.0f mA que salen, entre 8500, por la "
          "resistencia de 1 kohm del banco (%s)" % (i_is or 0, i_out or 0, lis))
    rc, out = ibt2(3.3, 0)
    check("3.30 V en IN, y el 74HC244 del modulo, con su VCC, pide 3.50 V para un alto" in out,
          "con 3,3 V en RPWM y el VCC del modulo a 5 V, el 74HC244 queda fuera de especificacion "
          "y se avisa (y lo lee alto)")

    grupo("C3 (F1b) Un PID de posicion en el firmware, con el encoder Hall en el TIM2")
    t0 = time.time()
    s, avs, cat, rc = muestrea(sim, "placas/nucleo_f446re_ibt2_jgb37.xml", 2000,
                               [("M/MOTOR", "cuenta"), ("N/LD2", "encendido")], periodo_ns=5000000)
    dt = time.time() - t0
    ok = s is not None and rc == 0
    check(ok, "placas/nucleo_f446re_ibt2_jgb37.xml corre 2 s con verif/fw/cnc_pid")
    if not ok:
        return
    c = s[("M/MOTOR", "cuenta")]
    led = s[("N/LD2", "encendido")]
    ida = [v for t, v in c if 0.1 < t <= 1.1]
    vuelta = [v for t, v in c if t > 1.1]
    check(max(ida) < 1320 * 1.02 and abs(ida[-1] - 1320) <= 4,
          "una vuelta del eje de salida, 1320 cuentas: llega sin pasarse mas de un 2 %% (como "
          "mucho %d) y se queda a %d" % (max(ida), ida[-1]))
    check(min(vuelta) > -660 - 0.02 * 1980 and abs(vuelta[-1] + 660) <= 4,
          "y media vuelta atras, a -660: como mucho %d, y se queda a %d" % (min(vuelta), vuelta[-1]))
    dentro = [t for t, v in c if 0.1 < t <= 1.1 and all(abs(x - 1320) <= 4 for tt, x in c
                                                         if t <= tt <= 1.1)]
    check(dentro and dentro[0] < 0.7,
          "dentro de 4 cuentas desde los %.0f ms: en menos de 600 ms desde la orden"
          % (dentro[0] * 1e3 if dentro else -1))
    check(led and led[-1][1] == 1.0 and any(v == 0.0 for t, v in led if 0.1 < t < 0.4),
          "y el LED LD2, que el firmware enciende con el error en 4 cuentas, luce al final")
    check("74HC244" in textos(avs), "el 74HC244 del IBT-2, con 3,3 V del STM32: se avisa")
    cuesta(dt, 4.0, "2 s simulados, con el MCU y el PWM de 1 kHz, en %.2f s de reloj" % dt)

# =============================================================================
# C4 (F1c): el BLDC con su controlador
# =============================================================================
BANCO_BLDC = """<placa nombre="banco-bldc">
  <componente tipo="Conector" id="J" filas="1" columnas="7" nombres="V24 GND STEP DIR EN BRK ERR"/>
  <nodo id="J.GND" bus="si"/>
  <nodo id="V5"/>
  <componente tipo="Fuente" id="PSU" v="24" media_ms="20"><pin nombre="pin" nodo="J.V24"/></componente>
  <componente tipo="Fuente" id="L5" v="5"><pin nombre="pin" nodo="V5"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="J.GND"/></componente>
  <componente tipo="FuenteAjustable" id="EN" v="%(en)s" vmin="0" vmax="5" r="10">
    <pin nombre="pin" nodo="J.EN"/></componente>
  <componente tipo="GeneradorPasos" id="G" hz="%(hz)s" aceleracion="%(acel)s" vdd="5">
    <pin nombre="step" nodo="J.STEP"/><pin nombre="dir" nodo="J.DIR"/></componente>
  <componente tipo="Resistencia" id="RF" r="1000"><pin nombre="a" nodo="J.BRK"/><pin nombre="b" nodo="J.GND"/></componente>
  <componente tipo="Resistencia" id="RE" r="10000"><pin nombre="a" nodo="J.ERR"/><pin nombre="b" nodo="V5"/></componente>
</placa>
"""
SISTEMA_BLDC = """<sistema nombre="prueba-bldc">
  <placa id="C" fichero="controlador.xml"/>
  <placa id="M" fichero="motor.xml"/>
  <placa id="T" fichero="banco_bldc.xml"/>
  <hilo a="T/J.V24"  b="C/X1.VCC"/>
  <hilo a="T/J.GND"  b="C/X1.GND"/>
  <hilo a="T/J.STEP" b="C/X5.STEP"/>
  <hilo a="T/J.DIR"  b="C/X5.DIR"/>
  <hilo a="T/J.EN"   b="C/X5.EN"/>
  <hilo a="T/J.BRK"  b="C/X5.BRK"/>
  <hilo a="T/J.ERR"  b="C/X5.ERR"/>
  <hilo a="M/M.%(u)s" b="C/X2.U"/>
  <hilo a="M/M.%(v)s" b="C/X2.V"/>
  <hilo a="M/M.W"    b="C/X2.W"/>
%(hall)s</sistema>
"""
HALL = """  <hilo a="M/P1.VCC" b="C/X3.5V"/>
  <hilo a="M/P1.GND" b="C/X3.GND"/>
  <hilo a="M/P1.%(h1)s" b="C/X3.H1"/>
  <hilo a="M/P1.%(h2)s" b="C/X3.H2"/>
  <hilo a="M/P1.H3" b="C/X3.H3"/>
"""
ENCODER = """  <hilo a="M/P1.A" b="C/X4.A"/>
  <hilo a="M/P1.B" b="C/X4.B"/>
"""
OBS_BLDC = [("M/MOTOR", "rpm"), ("M/MOTOR", "corriente"), ("M/MOTOR", "tension"),
            ("C/U1", "estado"), ("C/U1", "error_seguimiento"), ("C/U1", "freno_suelto"),
            ("C/U1", "corriente_vcc"), ("C/U1", "error")]


def bldc(sim, cp, hz=0.0, acel=0.0, en=5, lista=(), ms=1000, periodo_ns=1000000, cruza_fases=False,
         cruza_hall=False, sin_hall_vcc=False, encoder=False, ctrl=""):
    """El controlador tipo C5-E y el DB59L024035-A en un banco: 24 V, un generador
    de pasos, EN, la salida del freno a 1 kohm y la de error con su pull-up"""
    placas = os.path.abspath("placas")
    m = open(os.path.join(placas, "db59l024035_a.xml"), encoding="utf-8").read()
    c = open(os.path.join(placas, "controlador_tipo_c5e.xml"), encoding="utf-8").read()
    if encoder:
        # El -B, con un encoder de 250 lineas en su eje de atras
        m = m.replace('filas="5" columnas="1" nombres="VCC GND H1 H2 H3"',
                      'filas="7" columnas="1" nombres="VCC GND H1 H2 H3 A B"')
        m = m.replace('pares_polos="3">', 'pares_polos="3" ppr="250">')
        m = m.replace('<pin nombre="h3"       nodo="P1.H3"/>',
                      '<pin nombre="h3"       nodo="P1.H3"/>\n    <pin nombre="enc_a" nodo="P1.A"/>'
                      '<pin nombre="enc_b" nodo="P1.B"/>')
        c = c.replace('freno_ms="100">', 'freno_ms="100" encoder="si" ppr="250">')
    if ctrl:
        c = c.replace('freno_ms="100"', 'freno_ms="100" ' + ctrl)
    cp("motor.xml", m)
    cp("controlador.xml", c)
    cp("banco_bldc.xml", BANCO_BLDC % {"en": en, "hz": hz, "acel": acel})
    hall = HALL % {"h1": "H2" if cruza_hall else "H1", "h2": "H1" if cruza_hall else "H2"}
    if sin_hall_vcc:
        hall = hall.replace('  <hilo a="M/P1.VCC" b="C/X3.5V"/>\n', "")
    if encoder:
        hall += ENCODER
    ruta = cp("bldc.xml", SISTEMA_BLDC % {"u": "V" if cruza_fases else "U",
                                          "v": "U" if cruza_fases else "V", "hall": hall})
    t0 = time.time()
    s, avs, cat, rc = muestrea(sim, ruta, ms, OBS_BLDC, periodo_ns=periodo_ns, lista=lista)
    return s, avs, ULTIMA[0], time.time() - t0


def entre(serie, t0, t1):
    return [v for t, v in serie if t0 <= t <= t1]


def c4_bldc(sim, cp):
    grupo("C4 (F1c) El BLDC DB59L024035-A y su controlador tipo C5-E")
    for placa, cab in (("db59l024035_a", "placa 'db59l024035-a': SIN MCU"),
                       ("controlador_tipo_c5e", "placa 'controlador-tipo-c5e': SIN MCU")):
        rc, out = valida(sim, "placas/%s.xml" % placa)
        f = piezas_sin_dibujo(placa)
        check(rc == 0 and cab in out and " 0 avisos" in out and ("dibujo: %s.svg (" % placa) in out
              and not f, "placas/%s.xml se valida sin avisos, con su dibujo: cada pieza visible y "
              "cada pin de conector, con su id%s" % (placa, " (faltan: %s)" % f if f else ""))

    # El freno: con EN, corriente y despues se suelta; sin EN, se echa y despues
    # se quita la corriente
    s, avs, out, dt = bldc(sim, cp, ms=500, lista=[(300, "T/EN", "tension", 0)],
                           periodo_ns=500000)
    est, fr = s[("C/U1", "estado")], s[("C/U1", "freno_suelto")]
    suelta = [t for t, v in fr if v == 1.0]
    marcha = [t for t, v in est if v == 3.0]
    check(suelta and suelta[0] <= 0.0021 and marcha and 0.099 <= marcha[0] <= 0.102 and
          all(v == 2.0 for t, v in est if t < 0.099),
          "con EN, el controlador pone corriente y en la vuelta siguiente suelta el freno (a los "
          "%.1f ms); pasados sus 100 ms, en marcha (a los %.1f)" % ((suelta or [0])[0] * 1e3,
                                                                    (marcha or [0])[0] * 1e3))
    echado = [t for t, v in fr if t > 0.3 and v == 0.0]
    deshab = [t for t, v in est if t > 0.3 and v == 1.0]
    check(echado and echado[0] <= 0.3011 and deshab and 0.399 <= deshab[0] <= 0.402 and
          all(v == 4.0 for t, v in est if 0.3006 < t < 0.399),
          "sin EN, al reves: se echa el freno al momento (%.1f ms) y la corriente sigue 100 ms, "
          "hasta los %.1f: el eje no cae en ningun momento" % ((echado or [0])[0] * 1e3,
                                                               (deshab or [0])[0] * 1e3))
    s, avs, out, dt = bldc(sim, cp, hz=2000, ms=200)
    check("antes de que se suelte el freno" in textos(avs),
          "los pasos que llegan antes de que se suelte el freno se cuentan, y se avisa")

    # 600 rpm, y la posicion al paso
    s, avs, out, dt = bldc(sim, cp, acel=40000, ms=1500, lista=[(150, "T/G", "hz", 20000)])
    r = entre(s[("M/MOTOR", "rpm")], 1.0, 1.5)
    e = [abs(v) for t, v in s[("C/U1", "error_seguimiento")]]
    lb, lg = linea(out, "BLDC M/MOTOR:"), linea(out, "GENERADOR T/G:")
    vueltas, pasos = numero_tras(lb, "rpm, "), numero_tras(lg, "Hz, ")
    check(r and abs(sum(r) / len(r) - 600) < 3 and max(r) - min(r) < 20 and max(e) < 90,
          "con 20 kHz de STEP a 2000 pasos por vuelta, a %.1f rpm -600-, sin rizado que se vea "
          "(de %.1f a %.1f) y con el error de seguimiento en %.0f° como mucho, en la rampa"
          % (sum(r) / len(r) if r else 0, min(r or [0]), max(r or [0]), max(e or [0])))
    check(vueltas is not None and pasos and abs(vueltas - pasos / 2000.0) < 0.05,
          "cada paso cuenta: %+.0f pasos son %+.3f vueltas, y el motor ha dado %+.3f -empezo "
          "en el medio del primer tramo de los Hall, a 10°-" % (pasos or 0, (pasos or 0) / 2000.0,
                                                               vueltas or 0))

    # El punto nominal: 3500 rpm con 0,47 N·m
    rampa = [(150, "T/G", "hz", 116667)] + [(1000 + 10 * k, "M/MOTOR", "carga", 2.0 * k)
                                            for k in range(1, 51)]
    s0, avs0, out0, dt0 = bldc(sim, cp, acel=200000, ms=2200, lista=[(150, "T/G", "hz", 116667)])
    s1, avs1, out1, dt1 = bldc(sim, cp, acel=200000, ms=2200, lista=rampa)
    i0 = entre(s0[("M/MOTOR", "corriente")], 1.7, 2.2)
    i1 = entre(s1[("M/MOTOR", "corriente")], 1.7, 2.2)
    r1 = entre(s1[("M/MOTOR", "rpm")], 1.7, 2.2)
    v1 = entre(s1[("M/MOTOR", "tension")], 1.7, 2.2)
    ivcc = entre(s1[("C/U1", "corriente_vcc")], 1.7, 2.2)
    e1 = [abs(v) for t, v in s1[("C/U1", "error_seguimiento")]]
    di = (sum(i1) / len(i1) - sum(i0) / len(i0)) if i0 and i1 else 0
    check(r1 and abs(sum(r1) / len(r1) - 3500) < 5 and abs(di - 9.4) < 0.15 and
          s1[("C/U1", "estado")][-1][1] == 3.0,
          "a 3500 rpm, con la carga subiendo hasta el par nominal, 0,47 N·m: la corriente sube "
          "%.2f A, las 9,4 de la ficha (Kt·I = 0,05 x 9,4 = 0,47), y se queda en %.2f con el "
          "rozamiento" % (di, sum(i1) / len(i1) if i1 else 0))
    check(v1 and max(v1) < 23.0 and max(e1) < 180,
          "y la tension cabe: R·I + Kt·w = %.1f V de los 24 -el tope no llega-; el error de "
          "seguimiento, %.0f° como mucho" % (sum(v1) / len(v1) if v1 else 0, max(e1 or [0])))
    p_eje = 0.47 * 3500 * 2 * math.pi / 60
    check(ivcc and 7000 < sum(ivcc) / len(ivcc) < 9500,
          "de los 24 V tira %.0f mA de media: %.0f W en el eje, mas lo del bobinado, el puente y "
          "el reposo" % (sum(ivcc) / len(ivcc) if ivcc else 0, p_eje))

    # Mas de lo que puede: no pierde pasos, se para y lo dice
    rampa = [(150, "T/G", "hz", 116667)] + [(1000 + 10 * k, "M/MOTOR", "carga", 2.4 * k)
                                            for k in range(1, 51)]
    s, avs, out, dt = bldc(sim, cp, acel=200000, ms=2200, lista=rampa)
    lc = linea(out, "CONTROLADOR C/U1")
    sin_seguir = numero_tras(lc, "pasos (")
    check(s[("C/U1", "estado")][-1][1] == 5.0 and s[("C/U1", "error")][-1][1] == 1.0 and
          "error de seguimiento" in textos(avs) and "freno echado" in lc and sin_seguir,
          "con el 120 %% -0,56 N·m, 11,6 A de los 10 del controlador- el motor no puede: NO "
          "PIERDE PASOS SIN ENTERARSE, se para, echa el freno, lo dice y deja de seguir pasos "
          "(%s)" % lc)
    check("por detras de la consigna (el limite es 360° durante 10 ms)" in textos(avs),
          "«...error de seguimiento: el motor va N° por detras de la consigna (el limite es 360° "
          "durante 10 ms). Se para, echa el freno y lo dice por su salida de error»")
    s, avs, out, dt = bldc(sim, cp, acel=200000, ms=1500,
                           lista=[(150, "T/G", "hz", 116667), (1000, "M/MOTOR", "carga", 100)])
    check(s[("C/U1", "estado")][-1][1] == 5.0,
          "el 100 % DE GOLPE tambien lo para: frena al rotor de 119 g·cm² a 39 500 rad/s², y "
          "los lazos, con una medida cada 20°, no llegan a tiempo; con la misma carga en medio "
          "segundo, si")
    s, avs, out, dt = bldc(sim, cp, acel=200000, ms=1500,
                           lista=[(150, "T/G", "hz", 116667), (1000, "M/MOTOR", "carga", 50)])
    check(s[("C/U1", "estado")][-1][1] == 3.0,
          "y el 50 %% de golpe lo aguanta, con %.0f° de error como mucho"
          % max(abs(v) for t, v in s[("C/U1", "error_seguimiento")]))

    grupo("C4 (F1c) Los errores de cableado, y el encoder")
    s, avs, out, dt = bldc(sim, cp, acel=40000, ms=600, lista=[(150, "T/G", "hz", 20000)],
                           cruza_fases=True)
    check("las fases van cruzadas -U a la V de C/U1, V a la U, W a la W-" in textos(avs) and
          s[("C/U1", "estado")][-1][1] == 5.0,
          "U y V cruzadas: el motor lo dice, no hay corriente, y el controlador se para por el "
          "error de seguimiento")
    s, avs, out, dt = bldc(sim, cp, acel=40000, ms=600, lista=[(150, "T/G", "hz", 20000)],
                           cruza_hall=True)
    rmax = max(abs(v) for t, v in s[("M/MOTOR", "rpm")])
    check(s[("C/U1", "estado")][-1][1] == 5.0 and "error de seguimiento" in textos(avs),
          "H1 y H2 cruzados: el controlador cree que va al reves, empuja mas -el motor se "
          "embala hasta %.0f rpm- y el error de seguimiento lo para" % rmax)
    s, avs, out, dt = bldc(sim, cp, ms=300, sin_hall_vcc=True)
    check("los Hall dicen 111, que no existe" in textos(avs) and s[("C/U1", "estado")][-1][1] == 5.0,
          "sin los 5 V de los Hall: «los Hall dicen 111, que no existe: un hilo suelto o sin "
          "alimentacion. Se para»")
    lenta = [(150, "T/G", "hz", 2000)]
    sh, avs, out, dth = bldc(sim, cp, acel=20000, ms=1200, lista=lenta)
    se, avs, out, dte = bldc(sim, cp, acel=20000, ms=1200, lista=lenta, encoder=True)
    rh, re_ = entre(sh[("M/MOTOR", "rpm")], 0.8, 1.2), entre(se[("M/MOTOR", "rpm")], 0.8, 1.2)
    check(rh and re_ and max(re_) - min(re_) < 5 and max(rh) - min(rh) > 50 and
          abs(sum(re_) / len(re_) - 60) < 1 and abs(sum(rh) / len(rh) - 60) < 10,
          "a 60 rpm, con los Hall -18 medidas por vuelta- va a tirones (de %.0f a %.0f rpm); "
          "con un encoder de 250 lineas, suave (de %.1f a %.1f)" % (min(rh), max(rh), min(re_),
                                                                     max(re_)))
    cuesta(dt1, 4.0, "lo que cuesta: 2,2 s a 3500 rpm con la carga, en %.2f s de reloj (con los "
          "Hall; el encoder añade un suceso por flanco)" % dt1)

# =============================================================================
# C5 (F2): la transmisión
# =============================================================================
BANCO_EJE = """<placa nombre="banco-eje">
  <componente tipo="Conector" id="J" filas="1" columnas="7" nombres="VMOT GND VLOG STEP DIR V24 BRK"/>
  <nodo id="J.GND" bus="si"/>
  <componente tipo="Fuente" id="VMOT" v="12" limite_ma="5000" media_ms="20">
    <pin nombre="pin" nodo="J.VMOT"/></componente>
  <componente tipo="Gnd" id="MASA"><pin nombre="pin" nodo="J.GND"/></componente>
  <componente tipo="Fuente" id="VLOG" v="5"><pin nombre="pin" nodo="J.VLOG"/></componente>
  <componente tipo="FuenteAjustable" id="BRK" v="%(freno)s" vmin="0" vmax="24" r="0.5">
    <pin nombre="pin" nodo="J.BRK"/></componente>
  <componente tipo="GeneradorPasos" id="G" hz="0" ancho_us="5" vdd="5">
    <pin nombre="step" nodo="J.STEP"/><pin nombre="dir" nodo="J.DIR"/></componente>
</placa>
"""
SISTEMA_EJE = """<sistema nombre="prueba-eje">
  <placa id="E" fichero="eje.xml"/>
  <placa id="T" fichero="banco_eje.xml"/>
%(driver)s</sistema>
"""
DRIVER_EJE = """  <placa id="D" fichero="%(placas)s/pololu_a4988.xml"/>
  <hilo a="T/J.VMOT" b="D/J2.VMOT"/>
  <hilo a="T/J.GND"  b="D/J2.GND1"/>
  <hilo a="T/J.VLOG" b="D/J2.VLOG"/>
  <hilo a="T/J.GND"  b="D/J1.EN"/>
  <hilo a="T/J.GND"  b="D/J1.MS1"/>
  <hilo a="T/J.GND"  b="D/J1.MS2"/>
  <hilo a="T/J.GND"  b="D/J1.MS3"/>
  <hilo a="T/J.STEP" b="D/J1.STEP"/>
  <hilo a="T/J.DIR"  b="D/J1.DIR"/>
  <hilo a="D/J1.RST" b="D/J1.SLP"/>
  <hilo a="E/M.A1" b="D/J2.1A"/>
  <hilo a="E/M.A2" b="D/J2.1B"/>
  <hilo a="E/M.B1" b="D/J2.2A"/>
  <hilo a="E/M.B2" b="D/J2.2B"/>
"""
FRENO_EJE = """  <hilo a="T/J.BRK" b="E/F.A"/>
  <hilo a="T/J.GND" b="E/F.B"/>
"""
BOLAS = ('<componente tipo="Husillo" id="H" rosca="bolas" diametro_mm="16" avance_mm="5" '
         'longitud_mm="200" extremo_mm="8" apoyos="fijo-apoyado"/>')


def eje(sim, cp, husillo=None, masa=None, driver=True, freno=None, ms=500, lista=(), extra="",
        cambia=(), pedidos=(("E/C", "posicion"),)):
    """El eje Z de placas/eje_z_tr8.xml en un banco: con su A4988 en paso
    entero (100 pasos por mm con el Tr8x2) y un generador de pasos, o sin
    driver. Con otro husillo, otra masa, y un freno: el BCD de 24 V entre el
    motor y el acople"""
    x = open(os.path.join("placas", "eje_z_tr8.xml"), encoding="utf-8").read()
    if husillo:
        x = re.sub(r'<componente tipo="Husillo".*?/>', husillo, x, flags=re.S)
    if masa:
        x = x.replace('masa_kg="1.2"', 'masa_kg="%s"' % masa)
    if freno is not None:
        x = x.replace('<transmision id="Z" cadena="MOTOR A H C"/>',
                      '<componente tipo="Conector" id="F" filas="2" columnas="1" nombres="A B"/>\n'
                      '  <componente tipo="Freno" id="FR" v_nominal="24" potencia_w="8" '
                      'par_ncm="50" eje_mm="5">\n'
                      '    <pin nombre="a" nodo="F.A"/><pin nombre="b" nodo="F.B"/>\n'
                      '  </componente>\n'
                      '  <transmision id="Z" cadena="MOTOR FR A H C"/>')
    for a, b in cambia:
        x = x.replace(a, b)
    x = x.replace("</placa>", extra + "</placa>")
    cp("eje.xml", x)
    cp("banco_eje.xml", BANCO_EJE % {"freno": freno or 0})
    drv = DRIVER_EJE % {"placas": os.path.abspath("placas")} if driver else ""
    if freno is not None:
        drv += FRENO_EJE
    ruta = cp("ejez.xml", SISTEMA_EJE % {"driver": drv})
    s, avs, cat, rc = muestrea(sim, ruta, ms, list(pedidos), periodo_ns=5000000, lista=lista)
    return s, avs, ULTIMA[0], rc


def c5_transmision(sim, cp):
    grupo("C5 (F2) El husillo: el rendimiento y el autobloqueo salen de la helice")
    husillos = (("Tr8, avance 2, laton", 'rosca="Tr" diametro_mm="8" avance_mm="2" paso_mm="2" mu="0.15"',
                 5.2, 0.36, None, True),
                ("Tr8, avance 8 (4 entradas)", 'rosca="Tr" diametro_mm="8" avance_mm="8" paso_mm="2" mu="0.15"',
                 20.0, 0.66, 0.54, False),
                ("bolas 1605", 'rosca="bolas" diametro_mm="16" avance_mm="5"', 5.7, 0.95, 0.95, False))
    for nombre, attrs, lam, eta, etai, sujeta in husillos:
        s, avs, out, rc = eje(sim, cp, husillo='<componente tipo="Husillo" id="H" %s longitud_mm="200" '
                              'extremo_mm="8"/>' % attrs, driver=False, ms=10)
        lh = linea(out, "HUSILLO E/H:")
        m_ = re.search(r"helice ([0-9.]+)", lh)
        l_ = float(m_.group(1)) if m_ else None
        e_ = numero_tras(lh, "rendimiento ")
        ei = numero_tras(lh, "directo y ")
        ok = (l_ is not None and abs(l_ - lam) < 0.15 and e_ is not None and abs(e_ - eta) < 0.015
              and (("SE SUJETA SOLO" in lh) == sujeta) and (etai is None or abs(ei - etai) < 0.015))
        check(ok, "%s: la tabla del §6.3 -helice %.1f°, rendimiento %.2f%s- sale del calculo: %s"
              % (nombre, lam, eta, "; se sujeta solo" if sujeta else ", inverso %.2f" % etai, lh))

    grupo("C5 (F2) El eje Z de la 3018: la inercia reflejada y lo que se sujeta sin corriente")
    rc, out = valida(sim, "placas/eje_z_tr8.xml")
    f = piezas_sin_dibujo("eje_z_tr8")
    check(rc == 0 and " 0 avisos" in out and "dibujo: eje_z_tr8.svg (" in out and not f,
          "placas/eje_z_tr8.xml se valida sin avisos, con su dibujo: cada pieza visible y cada pin "
          "de conector, con su id%s" % (" (faltan: %s)" % f if f else ""))
    s, avs, out, rc = eje(sim, cp, driver=False, ms=300)
    lt = linea(out, "TRANSMISION E/Z:")
    j = numero_tras(lt, "elemento(s), ")
    j_h = 7850 * math.pi * 0.008 ** 4 * 0.2 / 32 * 1e7
    j_c = 1.2 * (0.002 / (2 * math.pi)) ** 2 * 1e7
    cuenta = 34 + 3 + j_h + j_c
    check(j and abs(j - cuenta) < 0.2,
          "la inercia reflejada en el motor es J_motor + J_acople + J_husillo + m·(p/2·pi)²: "
          "34 + 3 + %.1f + %.2f = %.1f g·cm², y la cadena dice %.1f" % (j_h, j_c, cuenta, j or 0))
    pos = s[("E/C", "posicion")] if s else []
    check(pos and abs(pos[-1][1] - 60.0) < 0.01,
          "sin corriente en el motor, el Tr8x2 sujeta el cabezal: sigue a %.3f mm" % pos[-1][1])
    s, avs, out, rc = eje(sim, cp, driver=False, masa=5, ms=300)
    check(abs(s[("E/C", "posicion")][-1][1] - 60.0) < 0.01,
          "y con 5 kg -un cabezal de 800 W-, tambien: un Tr8x2 se sujeta solo con cualquier peso")
    s, avs, out, rc = eje(sim, cp, husillo=BOLAS, driver=False, masa=5, ms=600)
    p = s[("E/C", "posicion")]
    t0 = [t for t, v in p if v < 1e-3]
    check(t0 and "choca con el tope de abajo" in textos(avs),
          "EL Z CON BOLAS SIN FRENO SE CAE: con un 1605 y 5 kg, el cabezal llega abajo a los %.0f "
          "ms y choca con el tope" % ((t0 or [0])[0] * 1e3))
    s, avs, out, rc = eje(sim, cp, husillo=BOLAS, driver=False, masa=1.2, ms=300)
    check(abs(s[("E/C", "posicion")][-1][1] - 60.0) < 0.01,
          "con 1,2 kg no se cae: el par de retencion sin corriente del NEMA 17 (1,5 N·cm) puede "
          "con los 0,9 que le llegan")
    s, avs, out, rc = eje(sim, cp, husillo=BOLAS, driver=False, masa=5, freno=0, ms=300)
    lf = linea(out, "FRENO E/FR:")
    check(abs(s[("E/C", "posicion")][-1][1] - 60.0) < 0.01 and "echado" in lf,
          "con un freno de muelle sin corriente entre el motor y el acople, no se cae (%s)" % lf)
    s, avs, out, rc = eje(sim, cp, husillo=BOLAS, driver=False, masa=5, freno=24, ms=800,
                          pedidos=(("E/C", "posicion"), ("E/FR", "suelto")))
    lf = linea(out, "FRENO E/FR:")
    i_fr = numero_tras(lf, "suelto, ")
    suelto = [t for t, v in s[("E/FR", "suelto")] if v == 1.0]
    check(s[("E/C", "posicion")][-1][1] < 1e-3 and i_fr and abs(i_fr - 331) < 3 and suelto and
          0.025 <= suelto[0] <= 0.040,
          "con sus 24 V se suelta a los %.0f ms -su retardo, 30-, gasta %.0f mA -una bobina de "
          "24²/8 W = 72 ohm, tras los 0,5 de la fuente- y el cabezal se cae"
          % ((suelto or [0])[0] * 1e3, i_fr or 0))
    reductora = ('<componente tipo="Reductora" id="R" relacion="10" rendimiento="0.5" '
                 'rendimiento_inverso="-0.1" inercia_gcm2="5"/>\n  ')
    s, avs, out, rc = eje(sim, cp, husillo=BOLAS, driver=False, masa=5, ms=300,
                          cambia=(('<componente tipo="Acople"', reductora + '<componente tipo="Acople"'),
                                  ('cadena="MOTOR A H C"', 'cadena="MOTOR R A H C"')))
    check(abs(s[("E/C", "posicion")][-1][1] - 60.0) < 0.01,
          "con una reductora de sinfin de 1:10 que se sujeta sola, el 1605 con 5 kg tampoco se cae")

    grupo("C5 (F2) El eje en marcha: los pasos, la holgura, los topes y los avisos")
    lista = [(100, "T/G", "hz", 1000), (600, "T/G", "hz", 0), (700, "T/G", "sentido", 0),
             (750, "T/G", "hz", 1000), (1250, "T/G", "hz", 0)]
    # Con el modelo ideal del paso a paso, cada paso es un paso exacto: lo que
    # falta es la holgura
    s, avs, out, rc = eje(sim, cp, ms=1400, lista=lista, pedidos=(("E/C", "posicion"),
                                                                ("T/G", "pasos")),
                          cambia=(('inercia_gcm2="34" eje_mm="5"',
                                   'inercia_gcm2="34" eje_mm="5" modelo="ideal"'),))
    p, g = s[("E/C", "posicion")], s[("T/G", "pasos")]
    antes = [v for t, v in p if t < 0.095][-1]
    arriba = [v for t, v in p if 0.62 < t < 0.69][-1]
    n_up = [v for t, v in g if 0.62 < t < 0.69][-1]
    n_down = n_up - g[-1][1]
    fin = p[-1][1]
    sube, baja = arriba - antes, arriba - fin
    check(n_up / 100.0 - 0.016 <= sube <= n_up / 100.0 + 0.001,
          "con el A4988 en paso entero, %d pasos hacia arriba son %.2f mm -100 pasos por mm-, y el "
          "carro sube %.4f: la holgura de la tuerca se come un poco al arrancar" % (n_up, n_up / 100.0,
                                                                                 sube))
    check(abs(baja - (n_down / 100.0 - 0.03)) < 0.0005,
          "y al bajar %d pasos baja %.4f, no %.2f: al cambiar de sentido el motor recorre los 30 um "
          "de la holgura antes de mover el carro" % (n_down, baja, n_down / 100.0))
    lista = [(100, "T/G", "hz", 1000), (1600, "T/G", "hz", 0)]
    s, avs, out, rc = eje(sim, cp, ms=1700, lista=lista)
    lp = linea(out, "PASO A PASO E/MOTOR:")
    perd = re.search(r"([0-9]+) perdidos", lp)
    check("choca con el tope de arriba" in textos(avs) and
          s[("E/C", "posicion")][-1][1] > 71.9 and perd and int(perd.group(1)) > 100,
          "1500 pasos hacia arriba desde los 60 mm: a los 12 el carro choca con el tope de arriba, "
          "se queda ahi y el paso a paso pierde los que faltan, sin que el driver se entere (%s)" % lp)
    s, avs, out, rc = eje(sim, cp, ms=600, lista=[(100, "T/G", "hz", 1000)],
                          cambia=(('longitud_mm="200" apoyos="fijo-apoyado"',
                                   'longitud_mm="1000" apoyos="fijo-libre"'),
                                  ('recorrido_mm="72"', 'recorrido_mm="900"')))
    check("por encima de su velocidad critica" in textos(avs),
          "un Tr8 de un metro con un extremo libre tiene la velocidad critica en 260 rpm, y a 300 "
          "se avisa: «...por encima de su velocidad critica, 259 (Tr de 1000 mm, apoyos fijo-libre): "
          "vibra como una cuerda»")
    x = open(os.path.join("placas", "eje_z_tr8.xml"), encoding="utf-8").read()
    x = x.replace('d1_mm="5"', 'd1_mm="6.35"')
    rc, out = valida(sim, cp("eje_mal.xml", x))
    check(rc != 0 and "MOTOR da un eje de 5 mm y A lo recibe de 6.35: no encajan" in out,
          "un acople de 6,35 mm en el eje de 5 de un NEMA 17 no se monta: «MOTOR da un eje de 5 mm "
          "y A lo recibe de 6.35: no encajan»")
    s, avs, out, rc = eje(sim, cp, ms=300, lista=[(50, "T/G", "hz", 1000)],
                          pedidos=(("E/Z", "posicion"),))
    z = s[("E/Z", "posicion")] if s else []
    check(z and z[-1][1] > 62.0,
          "la transmision deja ver la cota del carro, para el display del dibujo: %.2f mm"
          % (z[-1][1] if z else 0))

# =============================================================================
# C6 (F3): la máquina
# =============================================================================
def maquina(sim, cp, guion, ms, pos=("50", "50", "50"), pedidos=(("M/CX", "posicion"),),
            lista=(), periodo_ns=5000000, extra_maquina=(), sistema="cnc_3020.xml",
            firmware="verif/fw/cnc/cnc.bin", guion_ms=20, imagenes=(), parcial=False):
    """La fresadora 3020 entera (placas/cnc_3020.xml, u otro `sistema`) con su
    firmware, el G-code de `guion` tecleado por el VCP -en memoria- a los
    `guion_ms` y los carros donde diga `pos`. Devuelve (series, avisos, consola,
    segundos de reloj)"""
    placas = os.path.abspath("placas")
    n = open(os.path.join(placas, "nucleo_f446re.xml"), encoding="utf-8").read()
    n = n.replace('host="rfc2217:3355"\n              baudios="host"',
                  'host="memoria" guion_ms="%d" guion="%s"\n              baudios="115200"'
                  % (guion_ms, guion))
    m = open(os.path.join(placas, "fresadora_3020.xml"), encoding="utf-8").read()
    m = m.replace('posicion_mm="150"', 'posicion_mm="%s"' % pos[0])
    m = m.replace('posicion_mm="100"', 'posicion_mm="%s"' % pos[1])
    m = m.replace('posicion_mm="50" vertical', 'posicion_mm="%s" vertical' % pos[2])
    for a, b in extra_maquina:
        m = m.replace(a, b)
    d = os.path.dirname(cp("nucleo.xml", n))
    cp("fresadora.xml", m)
    x = open(os.path.join(placas, sistema), encoding="utf-8").read()
    x = x.replace('fichero="nucleo_f446re.xml"', 'fichero="%s/nucleo.xml"' % d)
    x = x.replace('fichero="fresadora_3020.xml"', 'fichero="%s/fresadora.xml"' % d)
    for f in ("cnc_shield_v3", "pololu_a4988", "modulo_mosfet", "motor_775"):
        x = x.replace('fichero="%s.xml"' % f, 'fichero="%s/%s.xml"' % (placas, f))
    fw = os.path.abspath(firmware)
    x = re.sub(r'(<mcu ref="N/u0" firmware=)"[^"]*"', lambda m: '%s"%s"' % (m.group(1), fw), x)
    ruta = cp("cnc.xml", x)
    t0 = time.time()
    s, avs, cat, rc = muestrea(sim, ruta, ms, list(pedidos), periodo_ns=periodo_ns, lista=lista,
                               imagenes=imagenes, parcial=parcial)
    return s, avs, ULTIMA[0], time.time() - t0


def vcp(out):
    """Lo que ha dicho el firmware por el VCP, una línea por elemento"""
    return [l.split("[N/VCP] ", 1)[1] for l in out.splitlines() if "[N/VCP] " in l]


def d_segmento(px, py, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    u = max(0.0, min(1.0, ((px - a[0]) * dx + (py - a[1]) * dy) / l2)) if l2 else 0.0
    return math.hypot(px - a[0] - u * dx, py - a[1] - u * dy)


def quieto_en(serie_x, serie_y, t0, t1):
    """La posición media de los carros entre t0 y t1 (s)"""
    xs = [v for t, v in serie_x if t0 <= t <= t1]
    ys = [v for t, v in serie_y if t0 <= t <= t1]
    return sum(xs) / len(xs), sum(ys) / len(ys)


def cuadrado_y_circulo(s, t_quieto):
    """El cuadrado de 20 mm y el circulo de 10 de radio de C6 y C7, medidos
    desde donde estan quietos los carros en `t_quieto` (s): el peor
    apartamiento de los lados, la distancia a las esquinas, los radios y el
    mayor hueco del circulo, en grados"""
    sx, sy = s[("M/CX", "posicion")], s[("M/CY", "posicion")]
    x0, y0 = quieto_en(sx, sy, t_quieto, t_quieto + 0.1)
    pts = [(t, a - x0, b - y0) for (t, a), (_, b) in zip(sx, sy) if t > t_quieto + 0.15]
    cuad = [(0, 0), (20, 0), (20, 20), (0, 20), (0, 0)]
    # El cuadrado acaba al volver a (0, 0) despues de pasar por (0, 20)
    t20 = [t for t, x, y in pts if math.hypot(x, y - 20) < 0.05]
    t_fin = [t for t, x, y in pts if t20 and t > t20[0] and math.hypot(x, y) < 0.06]
    t_cuad = t_fin[0] if t_fin else 1e9
    lado = [(x, y) for t, x, y in pts if t <= t_cuad]
    peor_c = max((min(d_segmento(x, y, cuad[i], cuad[i + 1]) for i in range(4)) for x, y in lado),
                 default=9)
    esquinas = [min(math.hypot(x - cx, y - cy) for t, x, y in pts) for cx, cy in cuad[1:4]]
    # El circulo: lo de despues, menos el rapido de (0, 0) a (10, 0)
    arco = [(x, y) for t, x, y in pts if t > t_cuad and d_segmento(x, y, (0, 0), (10, 0)) > 0.06]
    radios = [math.hypot(x - 10, y - 10) for x, y in arco]
    angulos = sorted(math.degrees(math.atan2(y - 10, x - 10)) for x, y in arco)
    hueco = max([b - a for a, b in zip(angulos, angulos[1:])] + [angulos[0] + 360 - angulos[-1]]) \
        if angulos else 360
    return peor_c, esquinas, len(lado), radios, hueco


def c6_maquina(sim, cp):
    grupo("C6 (F3) La maquina: la CNC Shield, la fresadora y el cableado")
    for placa, cab in (("cnc_shield_v3", "placa 'cnc-shield-v3': SIN MCU"),
                       ("fresadora_3020", "placa 'fresadora-3020': SIN MCU")):
        rc, out = valida(sim, "placas/%s.xml" % placa)
        f = piezas_sin_dibujo(placa)
        check(rc == 0 and cab in out and " 0 avisos" in out and ("dibujo: %s.svg (" % placa) in out
              and not f, "placas/%s.xml se valida sin avisos, con su dibujo: cada pieza visible y "
              "cada pin de conector, con su id%s" % (placa, " (faltan: %s)" % f if f else ""))
    rc, out = valida(sim, "placas/cnc_3020.xml")
    check(rc == 0 and "8 placas" in out and "0 avisos" in out and "mcu N/u0: verif/fw/cnc/cnc.bin" in out,
          "placas/cnc_3020.xml, el sistema entero -Nucleo, shield, tres A4988, la fresadora y el "
          "cabezal-, se monta sin avisos")
    r = subprocess.run([sim, "placas/nucleo_f446re.xml", "verif/fw/fp_anidado/fp_anidado.bin", "300",
                        "--serie", "VCP=memoria"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=120)
    o = r.stdout.decode("utf-8", "replace")
    check("FP anidado: 100 de 100 bien (100 fuera, 100 dentro)" in o,
          "EL FALLO DEL NUCLEO QUE DESTAPO EL FIRMWARE: una interrupcion anidada en un manejador "
          "sin FP ya no borra S0-S15 del hilo principal -CONTROL.FPCA a cero al entrar en una "
          "excepcion- (verif/fw/fp_anidado: %s)" % (linea(o, "FP anidado") or "nada"))

    grupo("C6 (F3) El G-code: un cuadrado y un circulo, contra lo pedido")
    s, avs, out, dt = maquina(sim, cp, "$I\\n?\\n", 200)
    v = vcp(out)
    check(any(l.startswith("Grbl 1.1f") for l in v) and "[VER:1.1f.mcu-sim:]" in v and
          any(l.startswith("<Idle|MPos:0.000,0.000,0.000") for l in v),
          "el firmware saluda como GRBL 1.1 -para que Candle o UGS lo reconozcan-, dice que no lo "
          "es, y contesta a $I y a ? (%s)" % " / ".join(v[:4]))
    check("3.30 V en step, y el A4988 pide 3.50 V para un alto" in textos(avs),
          "los A4988 del shield leen los 3,3 V de la Nucleo con su logica a 5 V: por debajo de su "
          "0,7·VDD, y se avisa (§8.1)")
    # Un cuadrado de 20 mm y un circulo de 10 de radio, a 1200 mm/min. Antes, un
    # paso adelante en X y en Y que recoge la holgura, y medio segundo quieto
    g = ("G21 G91\\nG1 X1 Y1 F600\\nG4 P0.5\\nG90 G92 X0 Y0\\n"
         "G1 X20 F1200\\nG1 Y20\\nG1 X0\\nG1 Y0\\nG4 P0.3\\nG0 X10\\n"
         "G2 X10 Y0 I0 J10 F1200\\n")
    s, avs, out, dt = maquina(sim, cp, g, 9500, pedidos=(("M/CX", "posicion"), ("M/CY", "posicion")),
                              periodo_ns=2000000)
    peor_c, esquinas, n_lado, radios, hueco = cuadrado_y_circulo(s, 0.35)
    check(peor_c < 0.05 and max(esquinas) < 0.03,
          "el CUADRADO de 20 mm: el carro X y la mesa no se separan de su lado mas de %.3f mm -la "
          "holgura de las tuercas es de 0,02-, y pasan por las esquinas a %.3f como mucho (%d "
          "puntos)" % (peor_c, max(esquinas), n_lado))
    check(radios and max(abs(r - 10) for r in radios) < 0.05 and hueco < 10,
          "el CIRCULO de 10 mm de radio, en G2: entre %.3f y %.3f, y da la vuelta entera"
          % (min(radios or [0]), max(radios or [0])))
    cuesta(dt, 9.5 * 2.5, "9,5 s de maquina en %.1f s de reloj: %.2f s por segundo, con los tres "
          "ejes y el MCU" % (dt, dt / 9.5))

    grupo("C6 (F3) El homing, la sonda, los limites, la seta y el cabezal")
    s, avs, out, dt = maquina(sim, cp, "$H\\nG0 X10 Y5\\n", 7000, pos=("8", "6", "66"),
                              pedidos=(("M/CX", "posicion"), ("M/CY", "posicion"), ("M/CZ", "posicion"),
                                       ("M/FX", "pisado"), ("M/FY", "pisado"), ("M/FZ", "pisado")),
                              periodo_ns=2000000)
    v = vcp(out)
    primero = {}
    for k in ("FX", "FY", "FZ"):
        t = [t for t, x in s[("M/%s" % k, "pisado")] if x == 1.0]
        primero[k] = t[0] if t else 99
    veces = {k: sum(1 for (t0, a), (t1, b) in zip(s[("M/%s" % k, "pisado")], s[("M/%s" % k, "pisado")][1:])
                    if a == 0.0 and b == 1.0) for k in ("FX", "FY", "FZ")}
    cx, cy, cz = (s[("M/C%s" % e, "posicion")][-1][1] for e in "XYZ")
    check(v.count("ok") == 2 and primero["FZ"] < primero["FX"] < primero["FY"] and
          all(n == 2 for n in veces.values()),
          "$H: primero el Z ARRIBA -para no arrastrar la fresa-, despues X y despues Y; cada "
          "final se pisa dos veces, rapido y despacio (Z a %.2f s, X a %.2f, Y a %.2f)"
          % (primero["FZ"], primero["FX"], primero["FY"]))
    check(abs(cx - 11.48) < 0.03 and abs(cy - 6.48) < 0.03 and abs(cz - 70.52) < 0.03,
          "la casa queda a 1 mm de cada final (X e Y en 1,48, Z en 70,52), y G0 X10 Y5 va de ahi: "
          "el carro X a %.3f mm y la mesa a %.3f; el Z, arriba, a %.3f" % (cx, cy, cz))
    s, avs, out, dt = maquina(sim, cp, "G38.2 Z-10 F300\\n", 2500, pos=("50", "50", "26"),
                              pedidos=(("M/CZ", "posicion"), ("M/PRB", "toca")))
    v = vcp(out)
    prb = [l for l in v if l.startswith("[PRB:")]
    cz = s[("M/CZ", "posicion")][-1][1]
    zp = float(prb[0].split(",")[2].split(":")[0]) if prb else 0
    check(prb and prb[0].endswith(":1]") and abs(zp + 6.0) < 0.03 and abs(cz - 20.0) < 0.01,
          "G38.2 Z-10: el Z baja hasta que la fresa toca la placa de la sonda, a 20 mm, y se para "
          "ahi (%.3f); el control dice %s -sus pasos: lo que de mas es la holgura de la tuerca-"
          % (cz, prb[0] if prb else "nada"))
    s, avs, out, dt = maquina(sim, cp, "G38.2 Z-3 F300\\n", 2000, pos=("50", "50", "26"),
                              lista=[(5, "M/PRB", "puesta", 0)])
    v = vcp(out)
    check(any(l.endswith(":0]") and l.startswith("[PRB:") for l in v) and "ALARM:5" in v,
          "sin la placa, baja los 3 mm sin tocar nada: [PRB:...:0] y ALARM:5, la de GRBL")
    s, avs, out, dt = maquina(sim, cp, "G0 X-10\\n", 2000, pos=("5", "50", "50"))
    v = vcp(out)
    cx = s[("M/CX", "posicion")][-1][1]
    check("ALARM:1" in v and "[MSG:Limite duro: un final pisado]" in v and 0.3 < cx < 0.5,
          "G0 X-10 desde 5 mm: el final de X se pisa a 0,5 mm y el control se para en seco -un "
          "limite duro, ALARM:1-, sin llegar al tope (el carro, a %.2f mm)" % cx)
    s, avs, out, dt = maquina(sim, cp, "G1 X40 F600\\n", 2500,
                              pedidos=(("M/CX", "posicion"), ("M/CZ", "posicion")),
                              lista=[(1000, "M/ESTOP", "pulsar", 1)])
    v = vcp(out)
    px = s[("M/CX", "posicion")]
    en_seta = [x for t, x in px if 1.0 <= t <= 1.02]
    al_final = px[-1][1]
    ld = [linea(out, "DRIVER %s/U1" % d) for d in ("DX", "DY", "DZ")]
    check("ALARM:3" in v and "[MSG:Abortado: la seta o A0]" in v and en_seta and
          abs(al_final - en_seta[0]) < 0.2 and all("salidas apagadas" in l for l in ld) and
          abs(s[("M/CZ", "posicion")][-1][1] - 50.0) < 0.01,
          "LA SETA a mitad de un G1: su contacto NC corta los 24 V de los drivers -se apagan sus "
          "salidas- y el NO avisa al control por A0, que se para y lo dice (ALARM:3). El carro X "
          "se queda en %.2f mm, y el Z, sin corriente, no se cae: su Tr8x2 se sujeta solo"
          % al_final)
    s, avs, out, dt = maquina(sim, cp, "M3 S1000\\nG4 P0.6\\nM5\\n", 3000,
                              pedidos=(("C/MOTOR", "rpm"),), periodo_ns=10000000)
    r = s[("C/MOTOR", "rpm")]
    check(max(v for t, v in r) > 9500 and r[-1][1] < 0.15 * max(v for t, v in r),
          "M3 S1000: el PWM de D11 abre el MOSFET y el 775 se pone a %.0f rpm; con M5 se queda "
          "suelto y se para por su rozamiento, en unos dos segundos (%.0f rpm al final)"
          % (max(v for t, v in r), r[-1][1]))


# La 3020 en los ajustes de grblHAL: los de verif/fw/cnc -200, 200 y 800 pasos
# por mm; 20, 20 y 8 mm/s; 300, 300 y 150 mm/s2; 300, 200 y 72 mm de
# recorrido-, los motores siempre con corriente ($1=255) y DIR al reves que
# verif/fw/cnc ($3=7: grblHAL pone DIR a uno para ir hacia negativo)
AJUSTES_3020 = ("$100=200\\n$101=200\\n$102=800\\n$110=1200\\n$111=1200\\n$112=480\\n"
                "$120=300\\n$121=300\\n$122=150\\n$130=300\\n$131=200\\n$132=72\\n"
                "$1=255\\n$3=7\\n")


def guion_grblhal(gcode, ajustes=""):
    """Lo que se teclea a grblHAL en la 3020: $14 y $5 -sus entradas son de
    contacto NC y las de esta maquina, NO-, una vuelta en blanco mientras los
    escribe en la Flash, Ctrl-X para que salga de la seta (ALARM:10), $X, los
    AJUSTES_3020, los `ajustes` y el `gcode`. El guion se teclea de una vez:
    las lineas en blanco son la espera (400 son 35 ms a 115200)"""
    espera = "\\n" * 400
    return ("\\n$14=70\\n$5=7\\n" + espera + "\\x18" + espera + "$X\\n" + AJUSTES_3020 + ajustes +
            gcode)


GRBLHAL = "verif/fw/grblhal/grblhal.bin"


def c7_grblhal(sim, cp):
    grupo("C7 (F3) El fallo del nucleo que destapo grblHAL: GE en las sumas paralelas")
    r = subprocess.run([sim, "placas/nucleo_f446re.xml", "verif/fw/simd_ge/simd_ge.bin", "300",
                        "--serie", "VCP=memoria"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=120)
    o = r.stdout.decode("utf-8", "replace")
    check("SIMD GE: 72 de 72 bien" in o and "strlen: 164 de 164 bien" in o,
          "UADD8, UASX y USAX ponen GE con el ACARREO, no con un >= 0 que una suma sin signo "
          "cumple siempre; y el strlen de newlib, que busca el cero con uadd8 y sel, lo encuentra "
          "(verif/fw/simd_ge: %s; %s)" % (linea(o, "SIMD GE") or "nada", linea(o, "strlen") or "nada"))
    rc, out = valida(sim, "placas/cnc_3020_grblhal.xml")
    check(rc == 0 and "8 placas" in out and "0 avisos" in out,
          "placas/cnc_3020_grblhal.xml, la 3020 con el cableado de grblHAL -el final de Z en D11 y "
          "el PWM del cabezal en D13-, se monta sin avisos")
    if not os.path.exists(GRBLHAL):
        print("  [--  ] sin %s: `make -C verif/fw/grblhal` baja grblHAL y lo construye; se "
              "saltan sus pruebas" % GRBLHAL)
        return
    kw = dict(sistema="cnc_3020_grblhal.xml", firmware=GRBLHAL, guion_ms=600)

    grupo("C7 (F3) grblHAL sin modificar en la 3020")
    s, avs, out, dt = maquina(sim, cp, "$I\\n?\\n", 1000, **kw)
    v = vcp(out)
    est = [l for l in v if l.startswith("<")]
    pn = est[0].split("Pn:")[1].split("|")[0] if est and "Pn:" in est[0] else ""
    check(any(l.startswith("GrblHAL 1.1f") for l in v) and "[BOARD:Protoneer v3]" in v and
          "[DRIVER:STM32F446@180MHz]" in v and any("NOPROBE" in l for l in v),
          "grblHAL arranca en la Nucleo a 180 MHz con el HSI, como \"Protoneer v3\" -la CNC "
          "Shield-, y dice que no tiene sonda (NOPROBE): su mapa la quita")
    check("ALARM:10" in v and all(c in pn for c in "XYE") and "SDIO_CK" not in textos(avs),
          "SUS ENTRADAS SON NC: con los finales y la seta de esta maquina, que son NO, arranca "
          "en ALARM:10 -la seta- con los finales de X e Y pisados (Pn:%s). Se arregla con $5 y "
          "$14, sin tocar el firmware" % pn)
    s, avs, out, dt = maquina(sim, cp, guion_grblhal("$H\\nM3 S1000\\n", "$21=1\\n$22=1\\n"), 3000,
                              pos=("8", "6", "66"), pedidos=(("M/CZ", "posicion"), ("C/MOTOR", "rpm")),
                              firmware=GRBLHAL, guion_ms=600)
    v = vcp(out)
    check("ALARM:1" in v and s[("M/CZ", "posicion")][-1][1] == 66.0 and
          max(x for t, x in s[("C/MOTOR", "rpm")]) == 0.0,
          "EN EL CABLEADO DE GRBL 1.1 (placas/cnc_3020.xml) NO VA: grblHAL lee el final de Z en "
          "D11, que ahi es el PWM del cabezal, y los 10 kohm del modulo MOSFET lo tienen a cero: "
          "pisado. Con los limites duros, ALARM:1 sin moverse, y ni $H ni M3")
    s, avs, out, dt = maquina(sim, cp, guion_grblhal(
        "$H\\nG91 G0 X10 Y5\\n", "$21=1\\n$22=1\\n$23=3\\n$24=60\\n$25=600\\n$27=1\\n"),
        9000, pos=("8", "6", "66"),
        pedidos=(("M/CX", "posicion"), ("M/CY", "posicion"), ("M/CZ", "posicion"),
                 ("M/FX", "pisado"), ("M/FY", "pisado"), ("M/FZ", "pisado")), periodo_ns=2000000, **kw)
    primero, veces = {}, {}
    for k in ("FX", "FY", "FZ"):
        f = s[("M/%s" % k, "pisado")]
        t = [t for t, x in f if x == 1.0]
        primero[k] = t[0] if t else 99
        veces[k] = sum(1 for (t0, a), (t1, b) in zip(f, f[1:]) if a == 0.0 and b == 1.0)
    cx, cy, cz = (s[("M/C%s" % e, "posicion")][-1][1] for e in "XYZ")
    check(primero["FZ"] < min(primero["FX"], primero["FY"]) and all(n == 2 for n in veces.values()),
          "$H: el Z arriba primero y despues X e Y A LA VEZ ($44=4, $45=3); cada final, dos "
          "veces (Z a %.2f s, X a %.2f, Y a %.2f)" % (primero["FZ"], primero["FX"], primero["FY"]))
    check(abs(cx - 11.48) < 0.03 and abs(cy - 6.48) < 0.03 and abs(cz - 70.52) < 0.03,
          "la casa, la misma que con verif/fw/cnc -a 1 mm de cada final-: G91 G0 X10 Y5 deja el "
          "carro X a %.3f mm, la mesa a %.3f y el Z a %.3f. (Sus coordenadas de maquina, no: como "
          "GRBL, grblHAL llama a esa casa X-299 Y-199 Z-1, y un G0 X10 absoluto se va al otro "
          "extremo)" % (cx, cy, cz))
    g = ("G21 G91\\nG1 X1 Y1 F600\\nG4 P0.5\\nG90 G92 X0 Y0\\n"
         "G1 X20 F1200\\nG1 Y20\\nG1 X0\\nG1 Y0\\nG4 P0.3\\nG0 X10\\n"
         "G2 X10 Y0 I0 J10 F1200\\n")
    s, avs, out, dt = maquina(sim, cp, guion_grblhal(g), 10500,
                              pedidos=(("M/CX", "posicion"), ("M/CY", "posicion")), periodo_ns=2000000,
                              **kw)
    # Quieto tras el paso de 1 mm que recoge la holgura: grblHAL tarda en
    # escribir sus ajustes, y el paso empieza hacia 1 s
    t1 = [t for t, x in s[("M/CX", "posicion")] if x > 50.5]
    peor_c, esquinas, n_lado, radios, hueco = cuadrado_y_circulo(s, (t1[0] if t1 else 0) + 0.2)
    check(peor_c < 0.05 and max(esquinas) < 0.03 and radios and
          max(abs(r - 10) for r in radios) < 0.05 and hueco < 10,
          "el mismo cuadrado y el mismo circulo que C6: los lados a %.3f mm como mucho, las "
          "esquinas a %.3f, el radio entre %.3f y %.3f" % (peor_c, max(esquinas),
                                                          min(radios or [0]), max(radios or [0])))
    cuesta(dt, 10.5 * 10, "10,5 s de maquina en %.1f s de reloj: %.2f s por segundo -a 180 MHz, "
          "con el tick de pasos de grblHAL-" % (dt, dt / 10.5))
    s, avs, out, dt = maquina(sim, cp, guion_grblhal(
        "M3 S1000\\nG4 P0.6\\nM5\\nG38.2 Z-10 F300\\nG1 X80 F600\\n\\nG1 X80 F600\\n"), 5000,
        pedidos=(("M/CX", "posicion"), ("M/CZ", "posicion"), ("C/MOTOR", "rpm")),
        lista=[(3000, "M/ESTOP", "pulsar", 1)], periodo_ns=10000000, **kw)
    v = vcp(out)
    r = s[("C/MOTOR", "rpm")]
    px = s[("M/CX", "posicion")]
    en_seta = [x for t, x in px if 3.0 <= t <= 3.02]
    ld = [linea(out, "DRIVER %s/U1" % d) for d in ("DX", "DY", "DZ")]
    check(max(x for t, x in r) > 9500 and r[-1][1] < 0.15 * max(x for t, x in r),
          "M3 S1000: el PWM de D13 -y LD2, que esta en el mismo pin- abre el MOSFET y el 775 se "
          "pone a %.0f rpm; con M5 se para solo" % max(x for t, x in r))
    check(v.count("error:20") == 2 and en_seta and en_seta[0] > 52,
          "G38.2, sin sonda: error:20. Y EL ERROR SE QUEDA: el G1 de despues da error:20 sin "
          "moverse -grblHAL no ejecuta mas G-code tras un error hasta una linea en blanco o una "
          "orden $-; tras la linea en blanco, el G1 si va (X a %.2f mm al pulsar la seta)"
          % (en_seta[0] if en_seta else 0))
    check("ALARM:10" in v and all("salidas apagadas" in l for l in ld) and
          abs(px[-1][1] - en_seta[0]) < 0.2 and abs(s[("M/CZ", "posicion")][-1][1] - 50) < 0.01,
          "LA SETA a mitad del G1: los drivers se quedan sin 24 V y grblHAL, por A0, se para "
          "(ALARM:10); X en %.2f mm y el Z, sujeto por su husillo" % px[-1][1])


def corta(sim, cp, g, ms, pos, material="mdf", fresa="plana", angulo=60, imagenes=(),
          parcial=False, pedidos=()):
    """Un mecanizado con verif/fw/cnc en la 3020: el G-code `g` -una lista de
    lineas- desde los carros en `pos`, que el programa declara con G92 para
    hablar en coordenadas de maquina. Devuelve (series, avisos, consola,
    segundos de reloj, mapa): el mapa de alturas de la pieza al acabar, por
    filas de Y"""
    mapa = os.path.join(os.path.dirname(cp("vacio.txt", "")), "mapa_%s_%s.csv" % (material, fresa))
    g = ["G92 X%s Y%s Z%s" % pos] + g
    ped = (("M/PZA", "q"), ("M/PZA", "par"), ("M/PZA", "fuerza"), ("C/MOTOR", "rpm"),
           ("M/CX", "posicion"), ("M/CY", "posicion"), ("M/CZ", "posicion")) + tuple(pedidos)
    s, avs, out, dt = maquina(
        sim, cp, "\\n".join(g) + "\\n", ms, pos=pos, pedidos=ped, periodo_ns=5000000,
        extra_maquina=[('material="mdf"', 'material="%s"' % material),
                       ('fresa="plana" fresa_mm="3.175">',
                        'fresa="%s" fresa_mm="3.175" angulo_v="%g" mapa="%s">'
                        % (fresa, angulo, mapa.replace("\\", "/")))],
        imagenes=imagenes, parcial=parcial)
    filas = [[float(x) for x in l.split(",")] for l in open(mapa) if not l.startswith("#")] \
        if os.path.exists(mapa) else []
    return s, avs, out, dt, filas


def durante_corte(s, clave):
    """Los valores de `clave` mientras la pieza pierde material"""
    q = s[("M/PZA", "q")]
    return [v for (t, a), (_, v) in zip(q, s[clave]) if a > 1.0]


def c8_pieza(sim, cp):
    CELDA, E = 0.25, 12.0
    grupo("C8 (F4) La pieza: un bolsillo de profundidad conocida")
    # Un bolsillo de 20 x 10 mm y 2 de hondo en X 120-140 e Y 85-95 -en la
    # pieza, 20-40 y 15-25-, con la fresa plana de 3,175 mm: en zigzag con
    # pasadas de 1,5 mm y una vuelta al borde
    r = 3.175 / 2
    x0, x1, y0, y1 = 120 + r, 140 - r, 85 + r, 95 - r
    g = ["M3 S1000", "G4 P1", "G1 Z13 F120"]
    y, d = y0, 1
    while True:
        g.append("G1 X%.4f F600" % (x1 if d > 0 else x0))
        d = -d
        if y >= y1 - 1e-9:
            break
        y = min(y + 1.5, y1)
        g.append("G1 Y%.4f" % y)
    g += ["G1 X%.4f Y%.4f" % (x0, y0), "G1 X%.4f" % x1, "G1 Y%.4f" % y1, "G1 X%.4f" % x0,
          "G1 Y%.4f" % y0, "G0 Z20", "M5"]
    s, avs, out, dt, m = corta(sim, cp, g, 23000, ("%.4f" % x0, "%.4f" % y0, "20"))
    zs = s[("M/CZ", "posicion")]
    z_fondo = min(z for t, z in zs)
    def h(u, v):
        return m[int(v / CELDA)][int(u / CELDA)]
    dentro = [h(u / 4, v / 4) for u in range(84, 157) for v in range(64, 97)]
    cortadas = [(i, j) for j, f in enumerate(m) for i, x in enumerate(f) if x < E - 0.5]
    us = [(i + 0.5) * CELDA for i, j in cortadas]
    vs = [(j + 0.5) * CELDA for i, j in cortadas]
    hondo = E - max(dentro)
    check(m and max(dentro) - min(dentro) < 1e-3 and abs(hondo - (15 - z_fondo)) < 1e-3 and
          abs(hondo - 2.0) < 0.025,
          "EL FONDO, PLANO Y A SU HONDO: %.3f mm en todo el bolsillo -lo que de verdad baja el "
          "carro Z, a %.3f: la holgura de su tuerca y un paso, contra los 13 pedidos-"
          % (hondo, z_fondo))
    check(cortadas and abs(min(us) - 20.125) < 1e-6 and abs(max(us) - 39.875) < 1e-6 and
          abs(min(vs) - 15.125) < 1e-6 and abs(max(vs) - 24.875) < 1e-6,
          "LAS PAREDES, donde tocan: el bolsillo va de X %.3f a %.3f y de Y %.3f a %.3f en la "
          "pieza -los centros de sus celdas de 0,25 mm: de 20 a 40 y de 15 a 25-"
          % (min(us or [0]), max(us or [0]), min(vs or [0]), max(vs or [0])))
    quitado = float(re.search(r"PIEZA M/PZA: .*?; ([0-9.]+) mm3", out).group(1)) \
        if "PIEZA M/PZA" in out else 0
    geo = 20 * 10 * 2.0 - (4 - math.pi) * r * r * 2.0
    rpm = durante_corte(s, ("C/MOTOR", "rpm"))
    check(abs(quitado - geo * hondo / 2.0) / geo < 0.02 and rpm and min(rpm) > 9500,
          "%.1f mm3 quitados, los del bolsillo con sus esquinas redondeadas (%.1f); en MDF el "
          "cabezal ni se entera: %.0f rpm como poco" % (quitado, geo * hondo / 2.0,
                                                        min(rpm or [0])))

    grupo("C8 (F4) Un grabado en V da el ancho que toca, y la imagen sale a trozos")
    # Una raya de 40 mm con una fresa en V de 60 grados, 1 mm de honda: de
    # ancho, 2 x 1 x tan(30) = 1,155 mm. En Y = 115,125, el centro de una
    # celda: la punta de la V es mas fina que la celda
    g = ["M3 S1000", "G4 P1", "G1 Z14 F120", "G1 X170 F600", "G0 Z20", "M5"]
    res = {}
    for parc in (True, False):
        res[parc] = corta(sim, cp, g, 11000, ("130", "115.125", "20"), fresa="v",
                          imagenes=(("M/PZA", "pieza"),), parcial=parc) + (dict(IMAGENES[0]),)
    s, avs, out, dt, m, im = res[True]
    hondo = 15 - min(z for t, z in s[("M/CZ", "posicion")])
    anchos = []
    for i in range(int(35 / CELDA), int(36 / CELDA)):
        area = sum(E - f[i] for f in m) * CELDA
        anchos.append(2 * area / hondo)
    w = sum(anchos) / len(anchos) if anchos else 0
    teo = 2 * hondo * math.tan(math.radians(30))
    check(anchos and abs(w - teo) / teo < 0.05,
          "LA V DE 60 GRADOS, %.3f mm de honda: %.3f mm de ancho -de su seccion, 2 x area / "
          "hondo-, contra %.3f de 2 x hondo x tan 30" % (hondo, w, teo))
    p, t = im[("M/PZA", "pieza")], res[False][5][("M/PZA", "pieza")]
    check(p["enteras"] == 1 and p["trozos"] > 20 and t["trozos"] == 0 and
          p["final"] is not None and p["final"] == t["final"] and p["bytes"] * 5 < t["bytes"],
          "LA IMAGEN, A TROZOS (T_IMAGEN_RECT) si la ventana lo pide: una entera y %d trozos, "
          "%d kB, y la misma imagen al final que %d enteras sin pedirlo, %d kB"
          % (p["trozos"], p["bytes"] // 1024, t["enteras"], t["bytes"] // 1024))

    grupo("C8 (F4) Las fuerzas del corte")
    g = ["M3 S1000", "G4 P1", "G1 Z14 F120", "G1 X170 F600", "G0 Z20", "M5"]
    s, avs, out, dt, m = corta(sim, cp, g, 11000, ("130", "115.125", "20"), material="aluminio")
    rpm, par = durante_corte(s, ("C/MOTOR", "rpm")), durante_corte(s, ("M/PZA", "par"))
    vx = durante_corte(s, ("M/CX", "posicion"))
    check(rpm and sum(rpm) / len(rpm) < 0.9 * 9878 and max(par) > 20,
          "UNA RANURA EN ALUMINIO, de 1 mm a 600 mm/min: el corte pide hasta %.0f N·cm y el "
          "775 baja a %.0f rpm de media (de 9878 en vacio): k_c1.1 de 700 N/mm2, frente a "
          "40 del MDF" % (max(par or [0]), sum(rpm) / max(1, len(rpm))))
    g = ["G1 Z14 F120", "G1 X170 F600", "G0 Z20"]
    s, avs, out, dt, m = corta(sim, cp, g, 9000, ("130", "115.125", "20"))
    tx = textos(avs)
    check("la fresa esta parada y la maquina la mete en el material" in tx and
          "ha perdido pasos" in tx and s[("M/CX", "posicion")][-1][1] < 169,
          "SIN M3, la fresa no gira y la maquina la mete en el MDF: la pieza lo dice, los paso "
          "a paso pierden pasos y el carro X se queda en %.2f mm de los 170 pedidos"
          % s[("M/CX", "posicion")][-1][1])
    g = ["M3 S1000", "G4 P0.5", "G1 Z2.5 F480", "G0 Z20", "M5"]
    s, avs, out, dt, m = corta(sim, cp, g, 6000, ("150", "100", "20"),
                               pedidos=(("M/PZA", "choque"), ("M/PZA", "hondo")))
    check("la fresa entra" in textos(avs) and
          s[("M/PZA", "choque")][-1][1] == 1.0 and s[("M/PZA", "hondo")][-1][1] == 12.0,
          "Y SI BAJA DEMASIADO, atraviesa los 12 mm y entra en la mesa: la alarma choque y el "
          "aviso: %s" % next((a[3] for a in avs if "la fresa entra" in a[3]), "nada"))


OBS_Z48 = [("CZ", "posicion"), ("CBUS", "tension"), ("RF", "conduce"), ("RF", "energia"),
           ("U1", "estado"), ("U1", "corriente_vcc"), ("U1", "error"), ("PSU", "corriente"),
           ("MOTOR", "rpm"), ("U2", "led"), ("U2", "salida"), ("U2", "saturado")]


def eje_z48(sim, cp, lista, ms, sin_chopper=False, absorbe=False, periodo_ns=1000000):
    """El Z de 48 V (placas/eje_z_48v.xml) con las ordenes de `lista` a su
    generador de pasos; sin la resistencia de frenado -desoldada- o con una
    fuente que absorbe, si se pide. Devuelve (series, avisos, consola, reloj)"""
    x = open("placas/eje_z_48v.xml", encoding="utf-8").read()
    if sin_chopper:
        x = x.replace('<componente tipo="ChopperFrenado" id="RF"',
                      '<componente tipo="ChopperFrenado" id="RF" conectada="no"')
    if absorbe:
        x = x.replace('absorbe="no"', 'absorbe="si"')
    d = os.path.dirname(cp("vacio.txt", ""))
    ruta = cp("eje_z_48v.xml", x)
    shutil.copy("placas/eje_z_48v.svg", os.path.join(d, "eje_z_48v.svg"))
    t0 = time.time()
    s, avs, cat, rc = muestrea(sim, ruta, ms, OBS_Z48, periodo_ns=periodo_ns, lista=lista)
    return s, avs, ULTIMA[0], time.time() - t0


# Bajar el Z 45 mm a 2000 rpm del motor -16 667 pasos/s, 83 mm/s a la salida
# de la reductora- y pararlo en seco, con una rampa de 100 ms
BAJA_Y_PARA = [(1, "G", "sentido", 0), (300, "G", "hz", 16667), (1100, "G", "hz", 0)]


def c9_potencia(sim, cp):
    grupo("C9 (F5) La potencia de 48 V: el bus, su condensador y la energia que vuelve")
    rc, out = valida(sim, "placas/eje_z_48v.xml")
    f = piezas_sin_dibujo("eje_z_48v")
    check(rc == 0 and "placa 'eje-z-48v': SIN MCU" in out and " 0 avisos" in out and
          "dibujo: eje_z_48v.svg (" in out and not f,
          "placas/eje_z_48v.xml -BLDC, controlador, freno, reductora, husillo de bolas, "
          "fuente conmutada, condensador, resistencia de frenado y el final por un "
          "optoacoplador- se valida sin avisos, con su dibujo%s" % (" (faltan: %s)" % f if f else ""))
    s, avs, out, dt = eje_z48(sim, cp, BAJA_Y_PARA, 1500)
    bus = s[("CBUS", "tension")]
    pico = max(v for t, v in bus)
    vcc = s[("U1", "corriente_vcc")]
    dev = min(v for t, v in vcc)
    z = s[("CZ", "posicion")][-1][1]
    e_rf = s[("RF", "energia")][-1][1]
    est = s[("U1", "estado")][-1][1]
    check(dev < -300 and 49.0 < pico < 51.0 and e_rf > 0.02 and est == 3.0 and abs(z - 75.55) < 0.2,
          "AL FRENAR EL Z QUE BAJA, el motor genera y el controlador lo DEVUELVE al bus (%.0f mA "
          "hacia la fuente); la fuente conmutada no lo absorbe y el condensador sube hasta %.2f V; "
          "la resistencia de frenado entra a 50,5 y quema %.0f mJ; el controlador sigue en marcha "
          "y el Z llega: %.2f mm" % (dev, pico, e_rf * 1e3, z))
    cuesta(dt, 1.5 * 20, "1,5 s del eje en %.1f s de reloj" % dt)
    s, avs, out, dt = eje_z48(sim, cp, BAJA_Y_PARA, 1500, sin_chopper=True)
    bus = s[("CBUS", "tension")]
    pico = max(v for t, v in bus)
    t_pico = [t for t, v in bus if v == pico][0]
    est = s[("U1", "estado")][-1][1]
    err = s[("U1", "error")][-1][1]
    z = s[("CZ", "posicion")][-1][1]
    tx = textos(avs)
    check("SOBRETENSION" in tx and 52.8 <= pico < 53.5 and t_pico > 1.05 and est == 5.0 and
          err == 1.0,
          "SIN LA RESISTENCIA DE FRENADO, el bus sube al parar hasta %.2f V a los %.3f s y SALTA "
          "LA PROTECCION del controlador a 52,8: corta el puente y da error (estado %d)"
          % (pico, t_pico, est))
    check(abs(z - 75.55) < 0.5 and "freno esta echado y el eje gira" not in tx,
          "y el Z, sin corriente, no se cae: lo sujeta el FRENO, que se echa en 20 ms (%.2f mm, "
          "contra 75,55 con la resistencia)" % z)
    s, avs, out, dt = eje_z48(sim, cp, BAJA_Y_PARA, 1500, sin_chopper=True, absorbe=True)
    bus = s[("CBUS", "tension")]
    psu = s[("PSU", "corriente")]
    check(max(v for t, v in bus) < 48.5 and min(v for t, v in psu) < -20 and
          s[("U1", "estado")][-1][1] == 3.0,
          "con una fuente de LABORATORIO, que lo absorbe (absorbe=\"si\"), el bus no se mueve "
          "(%.2f V como mucho): la fuente se traga lo que vuelve -%.0f mA hacia atras, de media "
          "en 20 ms, como lo ve su display-"
          % (max(v for t, v in bus), min(v for t, v in psu)))

    grupo("C9 (F5) El final de Z, un inductivo de 24 V, por un optoacoplador")
    s, avs, out, dt = eje_z48(sim, cp, [(1, "G", "sentido", 1), (300, "G", "hz", 8000)], 1500,
                              periodo_ns=5000000)
    led, sal, sat = s[("U2", "led")], s[("U2", "salida")], s[("U2", "saturado")]
    antes = [v for t, v in sat if t < 0.3]
    pis = [k for k, (t, v) in enumerate(sat) if v == 1.0]
    check(antes and max(antes) == 0.0 and pis and 9.5 < led[pis[0]][1] < 11.0 and
          0.25 < sal[pis[0]][1] < 0.35,
          "el Z sube hasta su final, a 148 mm: el sensor cierra a masa, el LED del PC817 se "
          "lleva %.1f mA por sus 2,2 kohm a 24 V y la salida se SATURA: %.2f mA por los 10 kohm "
          "a 3,3 V, el pin del MCU a nivel bajo, y los 24 V lejos de el"
          % (led[pis[0]][1] if pis else 0, sal[pis[0]][1] if pis else 0))


OBS_CAN = [("E/CX", "posicion"), ("E/U1", "statusword"), ("E/U1", "posicion"), ("E/A1", "tramas"),
           ("E/A1", "carga")]


def eje_can(sim, cp, guion, ms, extra=(), periodo_ns=5000000):
    """El eje X por CANopen (placas/eje_canopen.xml) con el maestro de
    verif/fw/canopen, las órdenes de `guion` tecleadas por el VCP y el
    analizador apuntando en una traza. Devuelve (series, avisos, consola,
    marcos, segundos de reloj): marcos, [(ms, id o None, bytes, texto)]"""
    placas = os.path.abspath("placas")
    n = open(os.path.join(placas, "nucleo_f446re.xml"), encoding="utf-8").read()
    n = n.replace('host="rfc2217:3355"\n              baudios="host"',
                  'host="memoria" guion_ms="20" guion="%s"\n              baudios="115200"' % guion)
    d = os.path.dirname(cp("nucleo.xml", n))
    traza = os.path.join(d, "traza_can.txt")
    if os.path.exists(traza):
        os.remove(traza)
    e = open(os.path.join(placas, "eje_x_canopen.xml"), encoding="utf-8").read()
    e = e.replace('bitrate="500e3">', 'bitrate="500e3" traza="%s">' % traza)
    for a, b in extra:
        e = e.replace(a, b)
    cp("eje.xml", e)
    x = open(os.path.join(placas, "eje_canopen.xml"), encoding="utf-8").read()
    x = x.replace('fichero="nucleo_f446re.xml"', 'fichero="%s/nucleo.xml"' % d)
    x = x.replace('fichero="eje_x_canopen.xml"', 'fichero="%s/eje.xml"' % d)
    x = x.replace('fichero="sn65hvd230.xml"', 'fichero="%s/sn65hvd230.xml"' % placas)
    x = x.replace('firmware="verif/fw/canopen/canopen.bin"',
                  'firmware="%s"' % os.path.abspath("verif/fw/canopen/canopen.bin"))
    ruta = cp("canopen.xml", x)
    t0 = time.time()
    s, avs, cat, rc = muestrea(sim, ruta, ms, OBS_CAN, periodo_ns=periodo_ns)
    dt = time.time() - t0
    marcos = []
    if os.path.exists(traza):
        for l in open(traza, encoding="utf-8"):
            if l.startswith("#") or not l.strip():
                continue
            p = l.split()
            if p[1] == "---":
                marcos.append((float(p[0]), None, [], l.split("---", 1)[1].strip()))
                continue
            k = int(p[2])
            marcos.append((float(p[0]), int(p[1], 16), [int(b, 16) for b in p[3:3 + k]],
                           " ".join(p[3 + k:])))
    return s, avs, ULTIMA[0], marcos, dt


def tras(lineas, texto, desde=0):
    """El índice de la primera línea desde `desde` que lleva `texto`, o -1"""
    for i in range(desde, len(lineas)):
        if texto in lineas[i]:
            return i
    return -1


def c10_canopen(sim, cp):
    grupo("C10 (F6) El bus de campo: un eje por CANopen")
    rc, out = valida(sim, "placas/eje_canopen.xml")
    f = piezas_sin_dibujo("sn65hvd230") + piezas_sin_dibujo("eje_x_canopen")
    check(rc == 0 and "3 placas" in out and " 0 avisos" in out and
          "dibujo T: sn65hvd230.svg (" in out and "dibujo E: eje_x_canopen.svg (" in out and not f,
          "placas/eje_canopen.xml -la Nucleo con el maestro, un transceptor SN65HVD230 en su "
          "CAN1 y el eje X con su controlador por CANopen y un analizador del bus- se valida sin "
          "avisos, con sus dibujos%s" % (" (faltan: %s)" % f if f else ""))

    # --- El arranque ---------------------------------------------------------
    s, avs, out, mc, dt = eje_can(sim, cp, "?\\n", 700)
    v = vcp(out)
    orden = ["boot-up del nodo 1", "tipo de aparato 0x00020192", "Switch on disabled (0x0250)",
             "Ready to switch on (0x0231)", "Switched on (0x0233)", "Operation enabled (0x0237)",
             "listo"]
    pos, i = [], 0
    for o in orden:
        i = tras(v, o, i)
        pos.append(i)
    check(all(k >= 0 for k in pos),
          "EL ARRANQUE: el boot-up del nodo, su tipo por SDO -0x00020192, CiA 402-, y la maquina "
          "de estados del accionamiento por la palabra de control, en su orden: Switch on "
          "disabled, Ready to switch on, Switched on, Operation enabled%s"
          % ("" if all(k >= 0 for k in pos) else " (falta: %s)" % orden[pos.index(-1)]))
    pide = [m for m in mc if m[1] == 0x601]
    resp = [m for m in mc if m[1] == 0x581]
    nmt = [m for m in mc if m[1] == 0x000]
    syncs = [m for m in mc if m[1] == 0x080]
    check(mc and mc[0][1] == 0x701 and mc[0][2] == [0] and abs(mc[0][0] - 100.0) < 0.01 and
          len(pide) == len(resp) >= 14 and not any("ABORTA" in m[3] for m in resp) and
          nmt and nmt[0][2] == [1, 1] and syncs and nmt[0][0] < syncs[0][0],
          "EN EL HILO, lo primero es el boot-up (701, 00) a los %.1f ms -los 100 de su arranque-; "
          "%d SDO, todos contestados y ninguno abortado; y el NMT de arranque (000, 01 01) antes de "
          "la primera SYNC" % (mc[0][0] if mc else 0, len(pide)))
    huecos = [b[0] - a[0] for a, b in zip(syncs, syncs[1:])]
    tpdo = [m for m in mc if m[1] == 0x181]
    tras_sync = all(any(0 < t[0] - y[0] < 0.25 for t in tpdo) for y in syncs)
    check(huecos and max(abs(h - 5.0) for h in huecos) < 0.002 and tras_sync,
          "la SYNC sale cada 5 ms -%d, de %.4f a %.4f ms entre una y otra- y tras cada una, en "
          "menos de un cuarto de ms, el TPDO1 del nodo: su palabra de estado y su posicion"
          % (len(syncs), min(huecos) if huecos else 0, max(huecos) if huecos else 0))

    # --- El homing, el perfil de posicion y un final -------------------------
    s, avs, out, mc, dt = eje_can(sim, cp, "H\\nG 100\\nG 50.5\\n?\\nG -10\\nG 20\\n", 9000)
    v = vcp(out)
    cx = s[("E/CX", "posicion")]
    hm = next((l for l in v if l.startswith("homing hecho")), "")
    fin = cx[-1][1]
    cero = fin - 20.0
    check("0x1637" in hm and abs(cero - 5.02) < 0.01,
          "EL HOMING por el final de abajo (metodo 17): hacia el deprisa, de vuelta despacio, y el "
          "cero donde se suelta: a %.3f mm del tope, el final de 5 mm con su histeresis. La "
          "palabra de estado lo dice: %s" % (cero, hm.split(": ", 1)[-1]))
    g = next((l for l in v if l.startswith("en 100.")), "")
    ms_g = numero_tras(g, "a los ") if g else 0
    # La velocidad, de 20 en 20 ms: la posición llega cada 5, con el paso del encoder
    vel = [(b[1] - a[1]) / (b[0] - a[0]) for a, b in zip(cx, cx[4:]) if b[0] - a[0] > 0]
    vmax = max(vel) if vel else 0
    crucero = sorted(x for x in vel if x > 45.0)
    vc = crucero[len(crucero) // 2] if crucero else 0
    llega = [x for t, x in cx if abs(x - (cero + 100.0)) < 0.05]
    check(g and abs(ms_g - 2100) <= 20 and abs(vc - 50.0) < 0.1 and vmax < 55.0 and llega,
          "EL PERFIL DE POSICION: a 100 mm por el RPDO1 -la consigna con el bit 4 y el saludo del "
          "12- a 50 mm/s con 500 mm/s2: llega en %d ms -2100 en la cuenta-, a %.2f mm/s de "
          "crucero, y el carro se queda a 100 mm del cero. Al acabar de acelerar el servo se pasa "
          "hasta %.1f mm/s: recupera el error de seguimiento" % (ms_g, vc, vmax))
    lim = next((l for l in v if l.startswith("error: no llega")), "")
    vuelta = next((l for l in v if l.startswith("en 20.")), "")
    xmin = min(x for t, x in cx[len(cx) // 2:])
    check("0x0A37" in lim and xmin > 3.0 and "choca" not in textos(avs) and vuelta,
          "UN FINAL QUE PARA EL EJE: a -10 mm, el final de abajo lo para con la parada rapida "
          "-el bit 11 de la palabra de estado, %s-, a %.2f mm del tope sin llegar a el; y de "
          "vuelta a 20 mm, sale del final y llega" % (lim.split(", ")[-1], xmin))
    hb = [m for m in mc if m[1] == 0x701 and m[2] == [5]]
    hb_h = [b[0] - a[0] for a, b in zip(hb, hb[1:])]
    rpdo = [m for m in mc if m[1] == 0x201]
    errs = [m for m in mc if m[1] is None]
    noack = [m for m in mc if "NADIE" in m[3]]
    carga = 100.0 * sum(1 for m in mc if m[1] is not None) / max(1, len(mc))
    check(hb_h and max(abs(h - 100.0) for h in hb_h) < 1.0 and len(rpdo) == 12 and not errs and
          not noack and s[("E/A1", "carga")][-1][1] < 15.0,
          "en %.0f s, %d marcos en el hilo: el heartbeat del nodo cada 100 ms (%d, operacional), "
          "12 RPDO -tres por movimiento-, ni un error ni un marco sin asentir, y el bus al %.1f %%"
          % (mc[-1][0] / 1e3 if mc else 0, len(mc), len(hb), s[("E/A1", "carga")][-1][1]))
    cuesta(dt, 9.0 * 1.5, "9 s del eje en %.1f s de reloj" % dt)

    # --- El fallo, su reinicio, el perfil de velocidad y la parada rapida ----
    g = "H\\nG 320\\nR\\nG 200\\nV -20\\nP 500\\nV 0\\nP 300\\n?\\nV 30\\nP 300\\nQ\\n?\\n"
    s, avs, out, mc, dt = eje_can(sim, cp, g, 14000)
    v = vcp(out)
    tx = textos(avs)
    em = [m for m in mc if m[1] == 0x081]
    i_f = tras(v, "Fault (0x0218)")
    check("choca con el tope de arriba" in tx and em and em[0][2][:3] == [0x11, 0x86, 0x21] and
          tras(v, "EMCY del nodo 1: 0x8611") >= 0 and tras(v, "Fault reaction active (0x021F)") >= 0
          and i_f >= 0,
          "EL FALLO: a 320 mm, el carro choca con el tope de arriba, el error de seguimiento pasa "
          "de una vuelta y el controlador se para y lo dice por el bus: un EMCY 8611 con el "
          "registro 21, y Fault reaction active -echando el freno- y Fault en su palabra de estado")
    i_r = tras(v, "EMCY del nodo 1: 0x0000", i_f)
    i_oe = tras(v, "Operation enabled", i_r)
    g200 = next((l for l in v if l.startswith("en 199.") or l.startswith("en 200.")), "")
    check(i_r > 0 and i_oe > 0 and g200 and abs(numero_tras(g200, "en ") - 200.0) < 0.02,
          "y SU REINICIO: el bit 7 de la palabra de control en Fault quita el fallo -un EMCY "
          "0000-, se vuelve a habilitar y el eje vuelve a ir: %s" % g200)
    p190 = next((l for l in v if l.startswith("pos 19")), "")
    check(p190 and abs(numero_tras(p190, "pos ") - 190.0) < 0.05,
          "EL PERFIL DE VELOCIDAD, por el RPDO2: a -20 mm/s medio segundo y parado, el eje "
          "acaba a 10 mm de donde estaba -%s-" % p190.split(" (")[0])
    i_q = tras(v, "Quick stop active (0x0217)")
    i_s = tras(v, "Switch on disabled (0x0250)", max(i_q, 0))
    cx = s[("E/CX", "posicion")]
    sw = s[("E/U1", "statusword")]
    check(i_q > 0 and i_s > i_q and abs(cx[-1][1] - cx[-2][1]) < 1e-3 and sw[-1][1] == 0x0250,
          "LA PARADA RAPIDA, yendo a 30 mm/s: Quick stop active mientras frena con su rampa y, "
          "parado, Switch on disabled: sin corriente")

    # --- Un bus sin nadie que asienta ----------------------------------------
    s, avs, out, mc, dt = eje_can(sim, cp, "?\\n", 1200, extra=(
        ('<componente tipo="ControladorMotor" id="U1"',
         '<componente tipo="ControladorMotor" id="U1" conectada="no"'),))
    v = vcp(out)
    nc = next((l for l in v if l.startswith("nadie contesta")), "")
    marcos = [m for m in mc if m[1] is not None]
    banderas = [m for m in mc if m[1] is None and "marco de error" in m[3]]
    # Los reintentos: cada 75 bits mientras es error activo; con la bandera
    # pasiva y la suspension, cada 83
    h = [round((b[0] - a[0]) * 1e3) for a, b in zip(marcos, marcos[1:])]
    check(nc and "TEC=128" in nc and "error pasivo" in nc and "bus off" not in nc and
          len(marcos) > 1000 and all("NADIE LO ASIENTE" in m[3] for m in marcos) and
          len(banderas) == 16 and h[:15] == [150] * 15 and h[20:30] == [166] * 10 and
          marcos[-1][0] < 1100.0,
          "SIN EL CONTROLADOR, nadie asiente: el maestro reintenta su NMT sin parar -el "
          "analizador, que no asiente, lo ve %d veces-; los 16 primeros con su marco de error, "
          "hasta el error pasivo, TEC 128, y desde ahi con la bandera pasiva -que no se ve- y la "
          "suspension: un reintento cada 166 us en vez de cada 150. Y ahi se queda, sin "
          "desconectarse: %s" % (len(marcos), nc))


OBS_BLDC6 = [("M/MOTOR", "rpm"), ("M/MOTOR", "hall"), ("M/MOTOR", "corriente_u"),
             ("M/MOTOR", "corriente_v"), ("M/MOTOR", "corriente_w"), ("X/U1", "sobrecorrientes"),
             ("X/U1", "fallo"), ("F/PSU", "corriente")]


def bldc6(sim, cp, guion, ms, periodo_ns=1000000, lista=(), extra_sistema=(), extra_motor=()):
    """La Nucleo con la X-NUCLEO-IHM07M1 y el DB42S01 (placas/nucleo_ihm07m1_db42.xml) con
    verif/fw/bldc6 y las órdenes de `guion` por el VCP. Devuelve (series, avisos, consola,
    segundos de reloj)"""
    placas = os.path.abspath("placas")
    n = open(os.path.join(placas, "nucleo_f446re.xml"), encoding="utf-8").read()
    n = n.replace('host="rfc2217:3355"\n              baudios="host"',
                  'host="memoria" guion_ms="20" guion="%s"\n              baudios="115200"' % guion)
    d = os.path.dirname(cp("nucleo.xml", n))
    m = open(os.path.join(placas, "db42s01.xml"), encoding="utf-8").read()
    for a, b in extra_motor:
        m = m.replace(a, b)
    cp("motor.xml", m)
    x = open(os.path.join(placas, "nucleo_ihm07m1_db42.xml"), encoding="utf-8").read()
    x = x.replace('fichero="nucleo_f446re.xml"', 'fichero="%s/nucleo.xml"' % d)
    x = x.replace('fichero="db42s01.xml"', 'fichero="%s/motor.xml"' % d)
    for f in ("x_nucleo_ihm07m1", "fuente_24v"):
        x = x.replace('fichero="%s.xml"' % f, 'fichero="%s/%s.xml"' % (placas, f))
    x = x.replace('firmware="verif/fw/bldc6/bldc6.bin"',
                  'firmware="%s"' % os.path.abspath("verif/fw/bldc6/bldc6.bin"))
    for a, b in extra_sistema:
        x = x.replace(a, b)
    ruta = cp("bldc6.xml", x)
    t0 = time.time()
    s, avs, cat, rc = muestrea(sim, ruta, ms, OBS_BLDC6, periodo_ns=periodo_ns, lista=lista)
    return s, avs, ULTIMA[0], time.time() - t0


def rpm_vcp(lineas, k=-1):
    """Las rpm de la k-ésima respuesta a ? del firmware"""
    r = [l for l in lineas if l.startswith("rpm ")]
    return numero_tras(r[k], "rpm ") if r else None


def c11_bldc_mcu(sim, cp):
    grupo("C11 (F7) El BLDC conmutado por el MCU: seis pasos con los Hall")
    rc, out = valida(sim, "placas/nucleo_ihm07m1_db42.xml")
    f = (piezas_sin_dibujo("x_nucleo_ihm07m1") + piezas_sin_dibujo("db42s01") +
         piezas_sin_dibujo("fuente_24v"))
    check(rc == 0 and "4 placas" in out and " 0 avisos" in out and
          "dibujo X: x_nucleo_ihm07m1.svg (" in out and "dibujo M: db42s01.svg (" in out and
          "dibujo F: fuente_24v.svg (" in out and not f,
          "placas/nucleo_ihm07m1_db42.xml -la Nucleo, la X-NUCLEO-IHM07M1 en sus morpho, el "
          "DB42S01 por fases y una fuente de 24 V- se valida sin avisos, con sus dibujos%s"
          % (" (faltan: %s)" % f if f else ""))

    # --- Sin lazo: un ciclo fijo ----------------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "D 30\\nP 600\\n?\\n", 700)
    v = vcp(out)
    r_fw = rpm_vcp(v)
    r_m = s[("M/MOTOR", "rpm")][-1][1]
    # Lo que da la cuenta: Kt·w = d·V - (2·Rf + 2·Ron)·I, con I la del rozamiento
    kt, rf, ron = 0.057, 1.75, 0.73
    i0 = 0.003 / kt
    w = (0.3 * 24.0 - 2 * (rf + ron) * i0) / kt
    r_teo = w * 60 / (2 * math.pi)
    check(r_fw and abs(r_fw - r_m) / r_m < 0.01 and abs(r_m - r_teo) / r_teo < 0.08,
          "SEIS PASOS SIN LAZO, al 30 %%: el motor va a %.0f rpm -%.0f en la cuenta, con 7,2 V "
          "medios contra su f.e.m., sus bobinas y los transistores-, y el firmware lo mide con "
          "el TIM2 en su interfaz de Hall: %.0f rpm" % (r_m, r_teo, r_fw))
    hs = [int(round(x)) for t, x in s[("M/MOTOR", "hall")] if t > 0.4]
    sec = []
    for h in hs:
        if not sec or sec[-1] != h:
            sec.append(h)
    orden = [5, 4, 6, 2, 3, 1]
    bien = all(orden[(orden.index(a) + 1) % 6] == b for a, b in zip(sec, sec[1:]) if a in orden)
    check(len(sec) > 12 and bien and 0 not in hs and 7 not in hs,
          "los Hall pasan por los seis sectores en su orden, 101 100 110 010 011 001, sin "
          "saltarse ninguno")

    # La corriente por fase: dos de tres, en bloques de 120°. Mientras acelera,
    # que es cuando lleva corriente: en vacio no pasa de 0,1 A, y entonces pesa
    # lo que tarda en apagarse la de la fase que se abre, por los diodos
    s, avs, out, dt = bldc6(sim, cp, "D 30\\n", 80, periodo_ns=20000)
    iu, iv, iw = (s[("M/MOTOR", "corriente_%s" % k)] for k in "uvw")
    tr = [(a[1], b[1], c[1]) for a, b, c in zip(iu, iv, iw) if 0.025 < a[0] <= 0.05]
    pico = max(max(abs(x) for x in t) for t in tr) if tr else 0
    dos = sum(1 for t in tr if sorted(abs(x) for x in t)[0] < 0.1 * pico)
    check(tr and pico > 0.5 and dos / len(tr) > 0.95,
          "LA CORRIENTE POR FASE, cada 20 us mientras acelera: en el %.0f %% de las muestras "
          "una de las tres esta a menos del 10 %% del pico -%.2f A-: la abierta del sector. Las "
          "otras dos llevan la misma, una entrando y otra saliendo" % (100 * dos / max(1, len(tr)), pico))

    # --- Con lazo, en los dos sentidos ---------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "R 2000\\nP 1000\\n?\\nR -1500\\nP 1500\\n?\\nS\\nP 300\\n?\\n",
                            3500)
    v = vcp(out)
    r1, r2, r3 = rpm_vcp(v, 0), rpm_vcp(v, 1), rpm_vcp(v, 2)
    check(r1 is not None and abs(r1 - 2000) < 20 and r2 is not None and abs(r2 + 1500) < 15 and
          r3 == 0,
          "CON EL LAZO DE VELOCIDAD, cada ms: a 2000 rpm -%.0f-, hacia atras a 1500 -%.0f-, "
          "pasando por cero, y parado" % (r1 or 0, r2 or 0))
    cuesta(dt, 3.5 * 3.5,
           "seis pasos con PWM de 20 kHz: 3,5 s del motor en %.1f s de reloj" % dt)

    # --- La sobrecorriente -----------------------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "D 70\\nP 200\\n?\\nR 1000\\nP 800\\n?\\n", 1200,
                            lista=[(1, "M/MOTOR", "bloquear", 1), (150, "M/MOTOR", "bloquear", 0)])
    v = vcp(out)
    fallo = next((l for l in v if l.startswith("FALLO")), "")
    oc = s[("X/U1", "sobrecorrientes")][-1][1]
    st = [l for l in v if l.startswith("rpm ")]
    check(fallo and oc >= 1 and "SOBRECORRIENTE" in textos(avs) and st and "fallos 1" in st[0] and
          "parado" in st[0],
          "CON EL ROTOR SUJETO, al 70 %%: 2,9 A en la cuenta. El L6230 salta a 2,8, baja DIAG-EN, "
          "y la entrada de freno del TIM1 apaga el PWM en el acto, sin esperar al firmware, "
          "que despues lo dice: %s" % fallo)
    check(len(st) > 1 and abs(rpm_vcp(v, 1) - 1000) < 15 and "fallos 1" in st[1],
          "y suelto el rotor, vuelve a arrancar -rearmando MOE- y va a 1000 rpm: %s" % st[-1])

    # --- Los Hall cruzados ----------------------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "D 30\\nP 600\\n?\\n", 700, extra_sistema=(
        ('<hilo a="M/P1.H1" b="X/J3.H1"/>', '<hilo a="M/P1.H1" b="X/J3.H2"/>'),
        ('<hilo a="M/P1.H2" b="X/J3.H2"/>', '<hilo a="M/P1.H2" b="X/J3.H1"/>')))
    rm = s[("M/MOTOR", "rpm")]
    vmax = max(abs(x) for t, x in rm if t > 0.2)
    check(vmax < 200,
          "CON H1 Y H2 CRUZADOS, la tabla de seis pasos no casa con el rotor: el motor da "
          "sacudidas sin arrancar (%.0f rpm como mucho)" % vmax)

    # --- En senoidal -----------------------------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "M 1\\nR 2000\\nP 1300\\n?\\n", 1500, periodo_ns=1000000)
    v = vcp(out)
    r = rpm_vcp(v)
    s2, avs2, out2, dt2 = bldc6(sim, cp, "M 1\\nD 50\\n", 420, periodo_ns=20000)
    iu, iv, iw = (s2[("M/MOTOR", "corriente_%s" % k)] for k in "uvw")
    tr = [(a[1], b[1], c[1]) for a, b, c in zip(iu, iv, iw) if a[0] > 0.4]
    pico = max(max(abs(x) for x in t) for t in tr)
    tres = sum(1 for t in tr if sorted(abs(x) for x in t)[0] > 0.1 * pico) if tr else 0
    check(r is not None and abs(r - 2000) < 20 and "senoidal" in v[-2] and tr and
          tres / len(tr) > 0.6,
          "EN SENOIDAL -las tres fases con PWM y la tension girando con el angulo que se "
          "interpola entre dos Hall, cada periodo del PWM-: a 2000 rpm (%.0f), con las tres "
          "fases llevando corriente a la vez el %.0f %% del tiempo" % (r or 0, 100 * tres / max(1, len(tr))))
    cuesta(dt, 1.5 * 16,
           "y cuesta: 1,5 s del motor en %.1f s de reloj, %.1f por segundo; seis pasos, poco mas "
           "de uno" % (dt, dt / 1.5))


def c12_foc(sim, cp):
    grupo("C12 La FOC: la corriente de los shunts, en los ejes del rotor")
    # --- El offset, al arrancar -----------------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "M 2\\nI 1\\nP 150\\n?\\nC\\n", 260, periodo_ns=1000000,
                            lista=[(1, "M/MOTOR", "bloquear", 1)])
    v = vcp(out)
    saludo = next((l for l in v if "offset de la corriente" in l), "")
    try:
        off = [float(x) for x in saludo.split("offset de la corriente")[1].split("cuentas")[0].split()]
    except (IndexError, ValueError):
        off = []
    teo = 1.56 / 3.3 * 4095
    check(len(off) == 3 and all(abs(x - teo) < 8 for x in off),
          "EL OFFSET, medido al arrancar con el puente apagado: %s cuentas por fase -los 1,56 V "
          "de los amplificadores, %.0f en la cuenta-" % (" ".join("%.0f" % x for x in off), teo))
    # --- Con el rotor sujeto: corriente continua, la medida contra la del motor
    lc = next((l for l in v if l.startswith("ia ")), "")
    med = [numero_tras(lc, k) for k in ("ia ", "ib ", "ic ")]
    mot = [s[("M/MOTOR", "corriente_%s" % k)][-1][1] for k in "uvw"]
    st = next((l for l in v if l.startswith("rpm ")), "")
    iq, idd = numero_tras(st, "iq "), numero_tras(st, "id ")
    check(None not in med and all(abs(a - b) < 0.02 for a, b in zip(med, mot)) and
          iq is not None and abs(iq - 1.0) < 0.03 and abs(idd) < 0.03,
          "CON EL ROTOR SUJETO e iq a 1 A: el firmware mide %s A por los shunts, el ADC y Clarke, "
          "y el motor lleva %s; en los ejes del rotor, iq %.2f e id %.2f" % (
              " ".join("%+.3f" % x for x in med if x is not None),
              " ".join("%+.3f" % x for x in mot), iq or 0, idd or 0))

    # --- El lazo de velocidad, con una carga de golpe --------------------------
    s, avs, out, dt = bldc6(sim, cp, "M 2\\nR 2000\\nP 500\\n?\\nP 600\\n?\\n", 1250,
                            lista=[(600, "M/MOTOR", "carga", 100)])
    v = vcp(out)
    st = [l for l in v if l.startswith("rpm ")]
    r1, r2 = (rpm_vcp(v, 0), rpm_vcp(v, 1)) if len(st) > 1 else (None, None)
    iq1 = numero_tras(st[0], "iq ") if st else None
    iq2 = numero_tras(st[1], "iq ") if len(st) > 1 else None
    rm = s[("M/MOTOR", "rpm")]
    caida = min((x for t, x in rm if 0.6 < t < 0.8), default=0)
    # La carga, el 100 % del par nominal, mas el rozamiento; el par por amperio
    # de iq, el de la fundamental de la f.e.m. trapezoidal: 1,5·(4/pi)·sen(30°)
    # /(pi/6)·Kt/2 = 0,912·Kt
    kt = 0.057
    t_carga = kt * 0.88 + 0.003 + 0.005e-3 * 2000 * 2 * math.pi / 60
    iq_teo = t_carga / (0.912 * kt)
    tarda = numero_tras(st[-1], "tarda ") if st else None
    check(r1 is not None and abs(r1 - 2000) < 25 and iq1 is not None and iq1 < 0.2 and
          r2 is not None and abs(r2 - 2000) < 25 and iq2 is not None and
          abs(iq2 - iq_teo) / iq_teo < 0.1,
          "CON EL LAZO DE VELOCIDAD, que da iq: a 2000 rpm (%.0f) con %.2f A, lo del rozamiento. "
          "Con el par nominal de golpe cae a %.0f rpm y vuelve a %.0f, con iq %.2f A: %.2f en la "
          "cuenta, con el par por amperio de la fundamental de su f.e.m. trapezoidal" % (
              r1 or 0, iq1 or 0, caida, r2 or 0, iq2 or 0, iq_teo))
    check(tarda is not None and tarda < 50,
          "la interrupcion de la FOC, en el pico de cada periodo del PWM, tarda %.1f us de los 50 "
          "como mucho -en este modelo, que cuenta unas tres veces los ciclos del silicio (P-02 de "
          "doc/todo.md)-" % (tarda or 0))
    cuesta(dt, 1.25 * 20,
           "y cuesta: 1,25 s del motor en %.1f s de reloj, %.1f por segundo -en senoidal, unos 8-"
           % (dt, dt / 1.25))

    # --- Hacia atras, sin parar ------------------------------------------------
    s, avs, out, dt = bldc6(sim, cp, "M 2\\nR 2000\\nP 400\\nR -1500\\n", 800)
    rm = s[("M/MOTOR", "rpm")]
    t_r = next((t for t, x in rm if x < 1900 and t > 0.3), None)
    t_c = next((t for t, x in rm if t_r and t > t_r and x < 0), None)
    t_l = next((t for t, x in rm if t_r and t > t_r and x < -1450), None)
    fin = [x for t, x in rm if t > 0.7]
    check(t_r and t_c and t_l and t_l - t_r < 0.1 and fin and all(abs(x + 1500) < 20 for x in fin),
          "DE 2000 rpm A -1500 SIN PARAR: el lazo pide iq negativa, el motor frena, pasa por cero "
          "a los %.0f ms y llega a los %.0f; luego, a %.0f" % (
              1000 * ((t_c or 0) - (t_r or 0)), 1000 * ((t_l or 0) - (t_r or 0)),
              sum(fin) / max(1, len(fin))))

    # --- Las tres fases a la vez, senoidales -----------------------------------
    s, avs, out, dt = bldc6(sim, cp, "M 2\\nR 2000\\n", 260, periodo_ns=20000,
                            lista=[(1, "M/MOTOR", "carga", 100)])
    iu, iv, iw = (s[("M/MOTOR", "corriente_%s" % k)] for k in "uvw")
    tr = [(a[1], b[1], c[1]) for a, b, c in zip(iu, iv, iw) if a[0] > 0.2]
    pico = max(max(abs(x) for x in t) for t in tr) if tr else 0
    tres = sum(1 for t in tr if sorted(abs(x) for x in t)[0] > 0.1 * pico) if tr else 0
    check(tr and 0.9 < pico < 1.5 and tres / len(tr) > 0.7,
          "LA CORRIENTE POR FASE, cada 20 us con la carga: las tres llevan corriente a la vez el "
          "%.0f %% del tiempo -en una senoide, el 81 %%; en seis pasos, nunca-, con %.2f A de pico"
          % (100 * tres / max(1, len(tr)), pico))


UN_SHUNT = (('<jumper ref="X/J5" puente="1 2"/>', ""), ('<jumper ref="X/J6" puente="1 2"/>', ""))


def c13_shunts(sim, cp):
    grupo("C13 Uno o tres shunts: J5 y J6")
    rc, out = valida(sim, "placas/x_nucleo_ihm07m1.xml")
    rc2, out2 = valida(sim, "placas/nucleo_ihm07m1_db42.xml")
    check(rc == 0 and "J5 2-3, J6 2-3" in out and " 0 avisos" in out and
          rc2 == 0 and "J5 1-2, J6 1-2" in out2,
          "LA IHM07M1 SALE CON UN SHUNT -J5 y J6 en 2-3, como de fabrica-, y el sistema de la "
          "FOC los pone en 1-2, tres, con <jumper ref>; el dibujo enseña cada puente donde esta")

    # Con el rotor sujeto en 101 -U arriba con el PWM, V abajo-, al 20 %
    res = {}
    for nombre, extra in (("uno", UN_SHUNT), ("tres", ())):
        s, avs, out, dt = bldc6(sim, cp, "D 20\\nP 100\\nC\\n?\\nS\\nM 2\\n?\\n", 260,
                                lista=[(1, "M/MOTOR", "bloquear", 1)], extra_sistema=extra)
        v = vcp(out)
        lc = next((l for l in v if l.startswith("ia ")), "")
        st = [l for l in v if l.startswith("rpm ")]
        res[nombre] = ([numero_tras(lc, k) for k in ("ia ", "ib ", "ic ")],
                       numero_tras(st[0], "ibus ") if st else None,
                       s[("M/MOTOR", "corriente_u")][-1][1], v, st)
    i_mot = 0.847
    (a1, b1, c1), ibus1, _, v1, st1 = res["uno"]
    (a3, b3, c3), ibus3, _, v3, st3 = res["tres"]
    check(None not in (a1, b1, c1) and abs(a1 + i_mot) < 0.02 and abs(b1) < 0.01 and abs(c1) < 0.01,
          "CON UN SHUNT, en el valle del PWM -U arriba, V abajo-: PA0 ve %+.3f A, toda la "
          "corriente, y PC1 y PC0 se quedan en el offset (%+.3f, %+.3f)" % (a1 or 0, b1 or 0, c1 or 0))
    check(None not in (a3, b3, c3) and abs(b3 + i_mot) < 0.02 and abs(a3) < 0.01 and abs(c3) < 0.01,
          "CON TRES, la de V en su shunt, PC1: %+.3f A; U, arriba, y W, abierta, nada (%+.3f, %+.3f)"
          % (b3 or 0, a3 or 0, c3 or 0))
    check(ibus1 is not None and ibus3 is not None and abs(ibus1 - i_mot) < 0.02 and
          abs(ibus3 - i_mot) < 0.02,
          "LA CORRIENTE DEL BUS, la suma de lo que baja por los shunts: %.2f A con uno y %.2f con "
          "tres -el motor lleva %.2f-" % (ibus1 or 0, ibus3 or 0, i_mot))
    err = next((l for l in v1 if l.startswith("error: un solo shunt")), "")
    check(err and len(st1) > 1 and "seis pasos" in st1[1] and len(st3) > 1 and "foc" in st3[1],
          "Y LA FOC, que necesita los tres: el firmware los cuenta con un pulso -en el pico del "
          "PWM, con un shunt para las tres, lo que baja por una sube por las otras y no ve nada- "
          "y con uno no la arranca: %s" % err)


def c14_vista3d(sim, cp):
    grupo("C14 (F8) La vista 3D: el <vista3d> de la fresadora")
    placas = os.path.abspath("placas")
    fres = open(os.path.join(placas, "fresadora_3020.xml"), encoding="utf-8").read()
    rc, out = valida(sim, "placas/fresadora_3020.xml")
    # (con una expresion regular: los comentarios de las placas llevan `--valida`)
    b3 = re.search(r"^  <vista3d .*?</vista3d>", fres, re.S | re.M)
    v3 = ET.fromstring(b3.group(0)) if b3 else None
    sol = v3.findall("solido") if v3 is not None else []
    sup = v3.findall("superficie") if v3 is not None else []
    check(rc == 0 and " 0 avisos" in out and len(sol) > 15 and len(sup) == 1 and
          sup[0].get("pieza") == "PZA" and sup[0].get("imagen") == "alturas",
          "LA FRESADORA DECLARA SU VISTA 3D: %d solidos -bancada, guias, husillos, carros, "
          "motores y cabezal- y la superficie de la pieza, la imagen de alturas de PZA; valida, "
          "sin avisos" % len(sol))

    # Cada error, en una copia de la fresadora con su <vista3d> estropeado
    malos = [
        ('<vista3d camara="560 -560 430"', '<vista3d camara="560 -560" ',
         'camara="560 -560" no vale'),
        ('<solido forma="caja" tam="440 360 25"', '<caja forma="caja" tam="440 360 25"',
         'van <solido> y <superficie>, no <caja>'),
        ('<solido forma="caja" tam="440 360 25"', '<solido forma="caja" tam="440 360 25" brillo="1"',
         'atributo desconocido: brillo'),
        ('<solido pieza="CY"', '<solido pieza="CQ"', "ninguna pieza 'CQ'"),
        ('<solido forma="caja" tam="440 360 25"', '<solido forma="caja" tam="440 360"',
         'largo, ancho y alto'),
        ('<solido forma="caja" tam="440 360 25"', '<solido forma="esfera" tam="440 360 25"',
         'caja o cilindro'),
        ('<solido forma="caja" tam="440 360 25"', '<solido forma="caja" eje="x" tam="440 360 25"',
         'eje= es de un cilindro'),
        ('<solido forma="caja" tam="440 360 25"', '<solido forma="caja" vector="1 0 0" tam="440 360 25"',
         'falta pieza='),
        ('observable="posicion" vector="0 -1 0"', 'observable="posicion" vector="0 -1"',
         'tres numeros, x y z'),
        ('gira="angulo"', 'gira="angulo rpm"', 'gira= es UN observable'),
        ('color="#c8a27a"', 'color="marron"', 'color="marron" no vale'),
        ('imagen="alturas" ', '', 'pieza= e imagen='),
    ]
    bien = []
    for a, b, esperado in malos:
        m = fres.replace(a, b, 1)
        r, o = valida(sim, cp("fresadora_mala.xml", m))
        bien.append(m != fres and r != 0 and esperado in o and "linea " in o)
        if not bien[-1]:
            print("    (%s: rc %d) %s" % (esperado, r, o.strip().splitlines()[-1] if o.strip() else ""))
    m = fres.replace("</vista3d>", "</vista3d>\n  <vista3d/>", 1)
    r, o = valida(sim, cp("fresadora_mala.xml", m))
    bien.append(r != 0 and "vista3d" in o)
    check(all(bien), "CADA ERROR, CON SU LINEA: %d de %d -una camara sin z, un elemento que no es "
          "ni solido ni superficie, un atributo, una pieza, un tam, una forma, un eje o un "
          "vector que no van, un gira de dos observables, un color, una superficie sin imagen y "
          "un segundo <vista3d>-" % (sum(bien), len(bien)))

    # La imagen de alturas: la raya en V de C8, 1 mm de honda en Y = 115,125,
    # y la vuelta por encima, en vacio, de X 170 a 150
    CELDA, E = 0.25, 12.0
    g = ["M3 S1000", "G4 P1", "G1 Z14 F120", "G1 X170 F600", "G0 Z20", "G0 X150", "M5"]
    s, avs, out, dt, mapa = corta(sim, cp, g, 11000, ("130", "115.125", "20"), fresa="v",
                                  imagenes=(("M/PZA", "alturas"), ("M/PZA", "pieza")))
    im = IMAGENES[0][("M/PZA", "alturas")]
    raiz = ET.fromstring(PLACA[0]) if PLACA[0] else None
    pl = [p for p in raiz.iter("placa") if p.get("id") == "M"] if raiz is not None else []
    v3e = pl[0].find("vista3d") if pl else None
    check(v3e is not None and v3e.get("camara") == v3.get("camara") and
          len(v3e) == len(v3) and
          all(a.tag == b.tag and a.attrib == b.attrib for a, b in zip(v3e, v3)),
          "EN T_PLACA, el <vista3d> en la <placa id=\"M\"> del sistema, tal cual: %d elementos, "
          "con sus atributos; la ventana lo lee de ahi" % (len(v3e) if v3e is not None else 0))
    an, al, px = im["ancho"], im["alto"], im["final"]
    dif, filas_ok, paso = 1e9, False, []
    if px is not None and mapa and an == len(mapa[0]) and al == len(mapa):
        filas_ok = True
        dif = 0.0
        for fila in range(al):
            j = al - 1 - fila                              # arriba, la de mas Y
            for i in range(an):
                q = 3 * (fila * an + i)
                h = ((px[q] << 8) | px[q + 1]) / 100.0
                dif = max(dif, abs(h - mapa[j][i]))
                if px[q + 2] == 255:
                    paso.append((i, j))
    fila_raya = int((115.125 - 70) / CELDA)
    vuelta = [i for i, j in paso if j == fila_raya and int(50 / CELDA) <= i <= int(70 / CELDA)]
    check(im["enteras"] >= 1 and filas_ok and dif <= 0.005 + 1e-6,
          "LA IMAGEN DE ALTURAS, %dx%d, contra el mapa de alturas de la pieza: la misma en "
          "todas sus celdas, con %.4f mm de diferencia como mucho -centesimas de mm en el rojo "
          "y el verde, la fila de arriba la de mas Y-" % (an, al, dif))
    check(paso and all(j == fila_raya for i, j in paso) and len(vuelta) >= 79 and
          len(paso) - len(vuelta) <= 3,
          "Y EN EL AZUL, la huella de la punta en vacio: las %d celdas de la vuelta por encima, "
          "de X 170 a 150 -de 81, las de los extremos segun donde paran los pasos-, y %d donde "
          "empezo a bajar; todas en la fila de la raya"
          % (len(vuelta), len(paso) - len(vuelta)))


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
        p = mide_patron(sim)
        if p is not None:
            LENTITUD[0] = max(1.0, p / PATRON_REF)
        print("  patron de velocidad: %s s de reloj -%.2f en la maquina de referencia-; los "
              "topes de reloj, x%.2f" % ("%.2f" % p if p is not None else "?", PATRON_REF,
                                         LENTITUD[0]))
        c1_cadena(sim, cp)
        c2_pap(sim, cp)
        c3_continua(sim, cp)
        c4_bldc(sim, cp)
        c5_transmision(sim, cp)
        c6_maquina(sim, cp)
        c7_grblhal(sim, cp)
        c8_pieza(sim, cp)
        c9_potencia(sim, cp)
        c10_canopen(sim, cp)
        c11_bldc_mcu(sim, cp)
        c12_foc(sim, cp)
        c13_shunts(sim, cp)
        c14_vista3d(sim, cp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print()
    print("=====================================================")
    print("TOTAL CNC : %d comprobaciones OK, %d fallos" % (ventana.ok, ventana.fallos))
    print("=====================================================")
    return 0 if ventana.fallos == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
