#!/usr/bin/env python3
# =============================================================================
# verif/openocd/cmsis_dap.py — La sonda CMSIS-DAP por TCP (X2 de
# doc/analisis_xds110.md), por dentro y con OpenOCD de verdad
#
#   C1  El protocolo, a mano, sin OpenOCD: el marco de 8 bytes, DAP_Info, el
#       paso de JTAG a SWD, DAP_Transfer con el DPIDR, el encendido del DP y
#       el IDR del AHB-AP, DAP_TransferBlock leyendo la tabla de vectores y
#       escribiendo la SRAM, la comparacion de valor, DAP_SWJ_Pins con
#       nRESET, DAP_ExecuteCommands y lo que no hay (JTAG): DAP_Invalid.
#   C2  OpenOCD `cmsis-dap tcp` programa y verifica el MSP432P401R por su
#       XDS110 (J101) con target/ti/msp432.cfg, y el firmware nuevo arranca;
#       y lo que tarda, frente a remote_bitbang.
#   C3  El MSP432E401Y por su XDS110, con target/ti/stellaris.cfg.
#   C4  El STM32F407 de discovery_min, sin XDS110: la sonda en sus pines.
#   C5  GDB por OpenOCD con FreeRTOS en el P4, por CMSIS-DAP.
#   C6  El STM32F303VC de la STM32F3DISCOVERY con target/stm32f3x.cfg, y G3
#       con `load` por el servidor de GDB de OpenOCD (S2 del F303; la misma
#       prueba que O5 de programa.py, por la otra sonda).
#
# C1 no necesita OpenOCD. El resto, uno con el backend `tcp` de cmsis-dap (de
# git posterior al 2025-09-01): si no lo hay se OMITE diciendolo, salvo con
# OPENOCD_OBLIGATORIO=1 (el CI).
#
#   make -f Makefile.mcu-sim cmsis-dap       # desde src/
#   python3 verif/openocd/cmsis_dap.py [--sim build/mcu-sim] [--openocd RUTA]
# =============================================================================
import argparse
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import programa as pr                                    # noqa: E402
from programa import check, grupo, puerto_libre, corre, palabras   # noqa: E402

P401R = "placas/msp_exp432p401r.xml"
E401Y = "placas/msp_exp432e401y.xml"
P4_DEMO = "verif/fw/msp432p4/p4_demo/p4_demo.out"
P4_BLINK = "verif/fw/msp432p4/sdk_ejemplos/systick_interrupt_gpio_blink/" \
           "systick_interrupt_gpio_blink.out"


class Sim:
    """mcu-sim con una sonda; se espera a que diga que escucha."""

    def __init__(self, sim, placa, fw, puerto, modo="--cmsis-dap", extra=()):
        self.log = tempfile.TemporaryFile(mode="w+")
        self.p = subprocess.Popen([sim, placa, fw, modo, "--port=%d" % puerto] + list(extra),
                                  stdout=self.log, stderr=subprocess.STDOUT, text=True)
        self.listo = False
        marca = "cmsis-dap] escuchando" if modo == "--cmsis-dap" else "remote_bitbang escuchando"
        t0 = time.time()
        while time.time() - t0 < 20 and self.p.poll() is None:
            if marca in self.texto():
                self.listo = True
                break
            time.sleep(0.05)

    def texto(self):
        self.log.seek(0)
        return self.log.read()

    def fin(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()
        return self.texto()


# ---------------------------------------------------------------------------
# Un cliente CMSIS-DAP por TCP, como el de OpenOCD
# ---------------------------------------------------------------------------
class Dap:
    def __init__(self, puerto):
        self.s = socket.create_connection(("127.0.0.1", puerto), timeout=20)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.rx = b""

    def manda(self, carga, tipo=1, firma=0x00504144):
        self.s.sendall(struct.pack("<IHBB", firma, len(carga), tipo, 0) + bytes(carga))

    def recibe(self):
        while True:
            if len(self.rx) >= 8:
                firma, n, tipo, _ = struct.unpack("<IHBB", self.rx[:8])
                if len(self.rx) >= 8 + n:
                    c = self.rx[8:8 + n]
                    self.rx = self.rx[8 + n:]
                    return firma, tipo, c
            b = self.s.recv(4096)
            if not b:
                raise EOFError("la sonda ha cerrado")
            self.rx += b

    def orden(self, carga):
        self.manda(carga)
        firma, tipo, c = self.recibe()
        assert firma == 0x00504144 and tipo == 2, "marco de respuesta malo"
        return c

    def cierra(self):
        self.s.close()


def info(d, i):
    r = d.orden([0x00, i])
    return r[2:2 + r[1]]


def c1_a_mano(a):
    grupo("C1 El protocolo, a mano: el marco, DAP_Info, SWD y las transferencias")
    p = puerto_libre()
    s = Sim(a.sim, P401R, P4_DEMO, p)
    if not check(s.listo, "mcu-sim escucha CMSIS-DAP por TCP en el puerto %d" % p):
        s.fin()
        return
    d = Dap(p)
    cap = info(d, 0xF0)
    tam = info(d, 0xFF)
    cnt = info(d, 0xFE)
    check(info(d, 0x01).rstrip(b"\0") == b"mcu-sim" and info(d, 0x04).rstrip(b"\0") == b"2.1.0",
          "DAP_Info: fabricante mcu-sim y protocolo 2.1.0")
    check(cap == b"\x01" and struct.unpack("<H", tam)[0] == 1024 and cnt[0] >= 1,
          "capacidades: solo SWD; paquetes de 1024 bytes, %d en vuelo" % cnt[0])
    check(d.orden([0x02, 2]) == b"\x02\x00" and d.orden([0x02, 1]) == b"\x02\x01",
          "DAP_Connect: JTAG no (0), SWD si (1)")
    check(d.orden([0x11]) == b"\xff", "una orden cortada contesta DAP_Invalid")
    d.orden([0x11] + list(struct.pack("<I", 2000000)))
    d.orden([0x04, 0, 100, 0, 0, 0])                         # TransferConfigure
    d.orden([0x13, 0])                                        # SWD_Configure
    # JTAG -> SWD: reset de linea, 0xE79E, reset de linea y dos ciclos a cero
    d.orden([0x12, 56] + [0xFF] * 7)
    d.orden([0x12, 16, 0x9E, 0xE7])
    d.orden([0x12, 56] + [0xFF] * 7)
    d.orden([0x12, 8, 0x00])
    r = d.orden([0x05, 0, 1, 0x02])                            # DP 0x0 R: DPIDR
    dpidr = struct.unpack("<I", r[3:7])[0] if len(r) >= 7 else 0
    check(r[:3] == b"\x05\x01\x01" and dpidr == 0x2BA01477,
          "DAP_Transfer lee el DPIDR: 0x%08X, como dice OpenOCD con remote_bitbang" % dpidr)
    # Encendido del DP: CDBGPWRUPREQ | CSYSPWRUPREQ, y esperar los acuses con
    # una lectura que COMPARA (bit 4 de la peticion, mascara por el bit 5)
    r = d.orden([0x05, 0, 4,
                 0x00] + list(struct.pack("<I", 0x1E)) +            # ABORT
                [0x04] + list(struct.pack("<I", 0x50000000)) +      # CTRL/STAT
                [0x20] + list(struct.pack("<I", 0xA0000000)) +      # mascara
                [0x16] + list(struct.pack("<I", 0xA0000000)))       # leer y comparar
    check(r[:3] == b"\x05\x04\x01",
          "encendido del DP: ABORT, CTRL/STAT, la mascara y una lectura que COMPARA hasta "
          "ver CDBGPWRUPACK y CSYSPWRUPACK")
    # El IDR del AHB-AP: SELECT al banco 0xF y una lectura del AP (aplazada:
    # la sonda la recoge en RDBUFF)
    r = d.orden([0x05, 0, 2, 0x08] + list(struct.pack("<I", 0x000000F0)) + [0x0F])
    idr = struct.unpack("<I", r[3:7])[0] if len(r) >= 7 else 0
    check(r[:3] == b"\x05\x02\x01" and (idr & 0x0FFFFFFF) == 0x04770011,
          "el IDR del AHB-AP por una lectura del AP: 0x%08X (un AHB-AP de ARM)" % idr)
    # TransferBlock: la tabla de vectores de p4_demo, con TAR auto-incrementado
    d.orden([0x05, 0, 3, 0x08] + list(struct.pack("<I", 0)) +
            [0x01] + list(struct.pack("<I", 0x23000012)) +           # CSW: palabra, inc
            [0x05] + list(struct.pack("<I", 0x00000000)))            # TAR
    r = d.orden([0x06, 0, 4, 0, 0x0F])
    n = struct.unpack("<H", r[1:3])[0] if len(r) >= 4 else 0
    v = list(struct.unpack("<4I", r[4:20])) if len(r) >= 20 else []
    with open(P4_DEMO.replace(".out", ".bin"), "rb") as f:
        esperado = list(struct.unpack("<4I", f.read(16)))
    check(n == 4 and r[3] == 1 and v == esperado,
          "DAP_TransferBlock lee la tabla de vectores, cuatro palabras con las lecturas "
          "aplazadas: %s" % " ".join("%08x" % x for x in v))
    # Escribir un bloque en la SRAM y leerlo
    datos = [0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00]
    d.orden([0x05, 0, 1, 0x05] + list(struct.pack("<I", 0x20004000)))
    r1 = d.orden([0x06, 0, 4, 0, 0x0D] + list(struct.pack("<4I", *datos)))
    d.orden([0x05, 0, 1, 0x05] + list(struct.pack("<I", 0x20004000)))
    r2 = d.orden([0x06, 0, 4, 0, 0x0F])
    check(r1[:4] == b"\x06\x04\x00\x01" and list(struct.unpack("<4I", r2[4:20])) == datos,
          "y escribe cuatro palabras en la SRAM que se leen igual")
    # nRESET por DAP_SWJ_Pins: abajo, y el pin lo dice; arriba otra vez
    a_ = d.orden([0x10, 0x00, 0x80, 0, 0, 0, 0])
    b_ = d.orden([0x10, 0x80, 0x80, 0, 0, 0, 0])
    check(a_[0] == 0x10 and not (a_[1] & 0x80) and (b_[1] & 0x80),
          "DAP_SWJ_Pins con nRESET: a cero el pin de reset se lee bajo, y suelto, alto")
    # Varias ordenes en un paquete, y lo que no hay
    r = d.orden([0x7F, 2, 0x00, 0x04, 0x00, 0xF0])
    check(r[:2] == b"\x7f\x02" and b"2.1.0" in r, "DAP_ExecuteCommands: dos DAP_Info en un paquete")
    check(d.orden([0x14, 1, 0x00, 0x00]) == b"\xff", "JTAG (DAP_JTAG_Sequence): DAP_Invalid")
    d.manda([0x00, 0x01], firma=0x12345678)
    try:
        d.recibe()
        cerrada = False
    except (EOFError, ConnectionError, socket.timeout):
        cerrada = True
    check(cerrada, "un marco con otra firma: la sonda cierra la conexion")
    d.cierra()
    time.sleep(0.3)
    texto = s.fin()
    check("firma que no es" in texto, "y dice por que")


def ocd_dap(ocd, puerto, cfg, gdb_puerto=0, rtos=False):
    a = [ocd, "-c", "adapter driver cmsis-dap", "-c", "cmsis-dap backend tcp",
         "-c", "cmsis-dap tcp host localhost", "-c", "cmsis-dap tcp port %d" % puerto,
         "-c", "transport select swd", "-c", "adapter speed 4000",
         "-c", "telnet port disabled", "-c", "tcl port disabled",
         "-c", ("gdb port %d" % gdb_puerto) if gdb_puerto else "gdb port disabled"]
    return a + ["-f", cfg]


def tiene_tcp(ocd):
    """¿Tiene este OpenOCD el backend tcp de cmsis-dap? Se le pide que conecte a
    un puerto cerrado: si lo intenta por TCP, lo tiene."""
    p = puerto_libre()
    rc, sal, _ = corre([ocd, "-c", "adapter driver cmsis-dap", "-c", "cmsis-dap backend tcp",
                        "-c", "cmsis-dap tcp host 127.0.0.1", "-c", "cmsis-dap tcp port %d" % p,
                        "-c", "transport select swd", "-c", "init", "-c", "shutdown"], 20)
    return "using TCP backend" in sal


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default="build/mcu-sim")
    ap.add_argument("--openocd", default=os.environ.get("OPENOCD", "openocd"))
    ap.add_argument("--gdb", default=os.environ.get("GDB", "gdb-multiarch"))
    ap.add_argument("--solo", default="", help="solo estos grupos: p. ej. C6")
    a = ap.parse_args()

    if a.solo != "C6":
        c1_a_mano(a)

    valido, que = pr.openocd_valido(a.openocd)
    if valido and not tiene_tcp(a.openocd):
        valido, que = False, "OpenOCD %s no tiene el backend tcp de cmsis-dap (de git posterior " \
                             "al 2025-09-01)" % que
    if not valido:
        print("\n  [--  ] C2-C5 OMITIDAS: " + que)
        if os.environ.get("OPENOCD_OBLIGATORIO") == "1":
            check(False, "OPENOCD_OBLIGATORIO=1 y no hay un OpenOCD que sirva")
        print("\nTOTAL CMSIS-DAP: %d comprobaciones OK, %d fallos" % (pr.OK, pr.MAL))
        return 0 if pr.MAL == 0 else 1
    print("\n  OpenOCD %s" % que)

    def c6():
        pr.f303(a, "CMSIS-DAP", lambda p: Sim(a.sim, pr.F3_PLACA, pr.F3_VIEJO, p),
                lambda p, pg: ocd_dap(a.openocd, p, "target/stm32f3x.cfg", gdb_puerto=pg),
                "C6 STM32F303VC en la STM32F3DISCOVERY por CMSIS-DAP: target/stm32f3x.cfg, y G3")
    if a.solo == "C6":
        c6()
        print("\nTOTAL CMSIS-DAP: %d comprobaciones OK, %d fallos" % (pr.OK, pr.MAL))
        return 0 if pr.MAL == 0 else 1

    # ------------------------------------------------------------------------
    grupo("C2 MSP432P401R por su XDS110: program ... verify reset, y lo que tarda")
    p = puerto_libre()
    s = Sim(a.sim, P401R, P4_DEMO, p)
    check(s.listo and "por XDS (J101)" in s.texto(),
          "mcu-sim escucha CMSIS-DAP en el puerto %d, por la sonda XDS de la placa" % p)
    rc, sal, t_dap = corre(ocd_dap(a.openocd, p, "target/ti/msp432.cfg") +
                           ["-c", "program %s verify reset exit" % P4_BLINK], 240)
    print("    %.1f s; %s" % (t_dap, " | ".join(l for l in sal.splitlines() if l.startswith("**"))))
    check(rc == 0 and "** Verified OK **" in sal and "CMSIS-DAP: SWD supported" in sal,
          "OpenOCD (cmsis-dap tcp) programa y verifica el ELF con el algoritmo de TI")
    rc2, sal2, _ = corre(ocd_dap(a.openocd, p, "target/ti/msp432.cfg") +
                         ["-c", "init", "-c", "sleep 300", "-c", "halt", "-c", "mdw 0xE000E014",
                          "-c", "resume", "-c", "shutdown"], 60)
    rvr = palabras(sal2, 0xE000E014)
    check(rc2 == 0 and rvr and rvr[0] == 1499999,
          "el firmware nuevo arranca: SYST_RVR = 1 499 999")
    texto = s.fin()
    check("pad: corriente" not in texto, "y sin peleas en los pines")
    # Lo mismo por remote_bitbang, para comparar
    p = puerto_libre()
    s = Sim(a.sim, P401R, P4_DEMO, p, modo="--openocd")
    rc, sal, t_rb = corre(pr.ocd_base(a.openocd, p, "target/ti/msp432.cfg") +
                          ["-c", "program %s verify reset exit" % P4_BLINK], 240)
    s.fin()
    print("    remote_bitbang: %.1f s; CMSIS-DAP: %.1f s" % (t_rb, t_dap))
    check(rc == 0 and t_dap < t_rb,
          "CMSIS-DAP programa mas deprisa que remote_bitbang (%.1f s frente a %.1f s)"
          % (t_dap, t_rb))

    # ------------------------------------------------------------------------
    grupo("C3 MSP432E401Y por su XDS110 (target/ti/stellaris.cfg)")
    p = puerto_libre()
    fw = "verif/fw/msp432e4/e4_swo/e4_swo.out"
    s = Sim(a.sim, E401Y, "verif/fw/msp432e4/e4_demo/e4_demo.out", p)
    cfg = os.path.join(tempfile.gettempdir(), "mcusim_e4_%d.cfg" % os.getpid())
    with open(cfg, "w") as f:
        f.write("set CHIPNAME msp432e401y\nsource [find target/ti/stellaris.cfg]\n"
                "$_TARGETNAME configure -event reset-start { cortex_m reset_config sysresetreq }\n")
    rc, sal, t = corre(ocd_dap(a.openocd, p, cfg) + ["-c", "program %s verify reset exit" % fw], 240)
    os.remove(cfg)
    print("    %.1f s; %s" % (t, " | ".join(l for l in sal.splitlines() if l.startswith("**"))))
    check(rc == 0 and "** Verified OK **" in sal,
          "OpenOCD programa y verifica e4_swo con el controlador stellaris")
    s.fin()

    # ------------------------------------------------------------------------
    grupo("C4 STM32F407 sin XDS110: la sonda en sus propios pines")
    p = puerto_libre()
    s = Sim(a.sim, "placas/discovery_min.xml", "verif/fw/blinky/blinky.bin", p)
    rc, sal, t = corre(ocd_dap(a.openocd, p, "target/stm32f4x.cfg") +
                       ["-c", "program verif/fw/uart_demo/uart_demo.bin 0x08000000 verify reset exit"],
                       240)
    print("    %.1f s; %s" % (t, " | ".join(l for l in sal.splitlines() if l.startswith("**"))))
    check(rc == 0 and "** Verified OK **" in sal and "device id = 0x10016413" in sal,
          "OpenOCD programa y verifica uart_demo.bin, e identifica el F407")
    buzon, t0 = [], time.time()
    while time.time() < t0 + 90:
        rc2, sal2, _ = corre(ocd_dap(a.openocd, p, "target/stm32f4x.cfg") +
                             ["-c", "init", "-c", "sleep 200", "-c", "mdw 0x20000000 6",
                              "-c", "shutdown"], 60)
        buzon = palabras(sal2, 0x20000000)
        if buzon and buzon[0] == 1:
            break
    check(len(buzon) == 6 and buzon[0] == 1 and buzon[5] == 42000000,
          "el firmware nuevo arranca y acaba: done = 1, PCLK1 = 42 MHz")
    s.fin()

    # ------------------------------------------------------------------------
    grupo("C5 GDB por OpenOCD, con FreeRTOS, por CMSIS-DAP")
    gdb = shutil.which(a.gdb)
    if not gdb:
        print("  [--  ] OMITIDO: no hay %s" % a.gdb)
    else:
        p, pg = puerto_libre(), puerto_libre()
        rtos = "verif/fw/msp432p4/freertos_blinky/freertos_blinky.out"
        s = Sim(a.sim, P401R, P4_DEMO, p)
        ocd = subprocess.Popen(ocd_dap(a.openocd, p, "target/ti/msp432.cfg", gdb_puerto=pg) +
                               ["-c", "msp432.cpu configure -rtos FreeRTOS",
                                "-c", "init", "-c", "program %s verify reset" % rtos],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        listo, lineas, t0 = False, [], time.time()
        while time.time() - t0 < 240:
            l = ocd.stdout.readline()
            if not l:
                break
            lineas.append(l)
            if "Listening on port %d for gdb" % pg in l:
                listo = True
            if listo and "Verified OK" in "".join(lineas):
                break
        check(listo and "Verified OK" in "".join(lineas),
              "OpenOCD programa la demo de FreeRTOS y abre su servidor de GDB")
        time.sleep(1.0)
        rc, sal, _ = corre([gdb, "-batch", "-nx", rtos,
                            "-ex", "target extended-remote localhost:%d" % pg,
                            "-ex", "monitor halt", "-ex", "info threads",
                            "-ex", "monitor resume", "-ex", "detach"], 120)
        hilos = re.findall(r"^\*?\s*\d+\s+Thread\s+\S+\s+\"([^\"]+)\"", sal, re.M)
        print("    hilos: %s" % ", ".join(hilos))
        check(rc == 0 and len(hilos) >= 3 and "IDLE" in hilos,
              "info threads ensena las tareas de FreeRTOS")
        ocd.terminate()
        try:
            ocd.wait(10)
        except subprocess.TimeoutExpired:
            ocd.kill()
        s.fin()

    # ------------------------------------------------------------------------
    c6()

    print("\nTOTAL CMSIS-DAP: %d comprobaciones OK, %d fallos" % (pr.OK, pr.MAL))
    return 0 if pr.MAL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
