#!/usr/bin/env python3
# =============================================================================
# programa.py — OpenOCD DE VERDAD contra el modelo, por los pines (T5 del MSP432)
#
# El cierre de T5 [doc/analisis_msp432.md, §8]: lanzar `mcu-sim` con la sonda
# remote_bitbang (`--openocd`), ejecutar
#
#     openocd ... -f target/ti/msp432.cfg -c "program X verify reset exit"
#
# con el controlador `msp432` SIN MODIFICAR, y comprobar que el firmware
# nuevo arranca; y lo mismo contra un F407 con target/stm32f4x.cfg.
#
#   O1  MSP432P401R: con `p4_demo` en la Flash, OpenOCD programa el ejemplo
#       del SDK systick_interrupt_gpio_blink (el ELF), lo verifica y resetea.
#       Por debajo, el algoritmo de Flash de TI que trae OpenOCD se copia a la
#       SRAM y se EJECUTA en el núcleo simulado, llamando a la ROM de DriverLib
#       (la R1 de T4) para borrar, programar y verificar.
#       El firmware nuevo arranca: SYST_RVR vale 1 499 999, el periodo que
#       pone su MAP_SysTick_setPeriod(1500000), y el PC está en su código.
#   O2  STM32F407VG: con `blinky` en la Flash, OpenOCD programa `uart_demo`
#       (el binario, en 0x0800_0000) con el controlador stm32f2x y su
#       algoritmo de escritura, lo verifica y resetea. El firmware nuevo
#       arranca: su buzón dice done = 1 y PCLK1 = 42 MHz.
#   O3  G3: GDB por el servidor de GDB de OpenOCD, con su soporte de RTOS:
#       la demo de FreeRTOS en el P4, y `info threads` enseña sus tareas.
#   O4  Y la sonda, desde fuera: los errores que dice `sim`.
#
# Hace falta un OpenOCD con SWD en remote_bitbang, que NO está en la 0.12.0
# de las distribuciones: se integró el 2023-12-03 y sus órdenes de espera
# son las de ahora desde el 2024-04-07 (79b51fe). Se usa `--openocd`, la
# variable OPENOCD, o `openocd` del PATH. Si el que hay no sirve, la prueba
# se OMITE diciéndolo, salvo con OPENOCD_OBLIGATORIO=1 (el CI), que falla.
#
#   make -f Makefile.mcu-sim openocd       # desde src/
#   python3 verif/openocd/programa.py [--sim build/mcu-sim] [--openocd RUTA]
#
# Código de salida 0 si todo va bien.
# =============================================================================
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

OK = 0
MAL = 0


def check(cond, que):
    global OK, MAL
    if cond:
        OK += 1
        print("  [OK  ] " + que)
    else:
        MAL += 1
        print("  [FALLO] " + que)
    sys.stdout.flush()
    return cond


def grupo(t):
    print("\n--- %s ---" % t)
    sys.stdout.flush()


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def ocd_base(ocd, puerto, cfg, gdb=False, gdb_puerto=0):
    a = [ocd,
         "-c", "adapter driver remote_bitbang",
         "-c", "remote_bitbang host localhost",
         "-c", "remote_bitbang port %d" % puerto,
         "-c", "transport select swd",
         "-c", "telnet port disabled",
         "-c", "tcl port disabled"]
    a += ["-c", ("gdb port %d" % gdb_puerto) if gdb else "gdb port disabled"]
    return a + ["-f", cfg]


def corre(cmd, plazo):
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=plazo)
        return r.returncode, r.stdout + r.stderr, time.time() - t0
    except subprocess.TimeoutExpired as e:
        sal = (e.stdout or b"").decode(errors="replace") + (e.stderr or b"").decode(errors="replace")
        return -1, sal + "\n(plazo agotado)", time.time() - t0


class Sim:
    """mcu-sim con la sonda; se espera a que diga que escucha."""

    def __init__(self, sim, placa, fw, puerto, extra=()):
        self.log = tempfile.TemporaryFile(mode="w+")
        self.p = subprocess.Popen([sim, placa, fw, "--openocd", "--port=%d" % puerto] + list(extra),
                                  stdout=self.log, stderr=subprocess.STDOUT, text=True)
        self.listo = False
        t0 = time.time()
        while time.time() - t0 < 20 and self.p.poll() is None:
            if "remote_bitbang escuchando" in self.texto():
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


def palabras(salida, dir_):
    """Las palabras de `mdw dir N` en la salida de OpenOCD."""
    m = re.search(r"0x%08x:((?:\s+[0-9a-f]{8})+)" % dir_, salida)
    return [int(x, 16) for x in m.group(1).split()] if m else []


def openocd_valido(ocd):
    """¿Tiene este OpenOCD SWD en remote_bitbang? (no en la 0.12.0)"""
    if not shutil.which(ocd) and not os.path.exists(ocd):
        return False, "no hay OpenOCD (%s)" % ocd
    rc, sal, _ = corre([ocd, "-c", "adapter driver remote_bitbang",
                        "-c", "transport select swd", "-c", "shutdown"], 20)
    if "only one transport option" in sal or "is not a valid transport" in sal or \
       "Can't change session's transport" in sal:
        v = re.search(r"Open On-Chip Debugger (\S+)", sal)
        return False, ("OpenOCD %s no tiene SWD en remote_bitbang: hace falta uno de "
                       "git posterior al 2024-04-07" % (v.group(1) if v else "?"))
    v = re.search(r"Open On-Chip Debugger (\S+)", sal)
    return True, v.group(1) if v else "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default="build/mcu-sim")
    ap.add_argument("--openocd", default=os.environ.get("OPENOCD", "openocd"))
    ap.add_argument("--gdb", default=os.environ.get("GDB", "gdb-multiarch"))
    a = ap.parse_args()

    valido, que = openocd_valido(a.openocd)
    if not valido:
        print("  [--  ] OMITIDA: " + que)
        if os.environ.get("OPENOCD_OBLIGATORIO") == "1":
            check(False, "OPENOCD_OBLIGATORIO=1 y no hay un OpenOCD que sirva")
            return 1
        return 0
    print("  OpenOCD %s" % que)

    # ------------------------------------------------------------------------
    grupo("O1 MSP432P401R: program ... verify reset con el controlador msp432")
    p = puerto_libre()
    fw_nuevo = "verif/fw/msp432p4/sdk_ejemplos/systick_interrupt_gpio_blink/" \
               "systick_interrupt_gpio_blink.out"
    s = Sim(a.sim, "placas/msp_exp432p401r.xml", "verif/fw/msp432p4/p4_demo/p4_demo.out", p)
    check(s.listo, "mcu-sim escucha remote_bitbang en el puerto %d" % p)
    rc, sal, t = corre(ocd_base(a.openocd, p, "target/ti/msp432.cfg") +
                       ["-c", "program %s verify reset exit" % fw_nuevo], 240)
    print("    %.1f s; %s" % (t, " | ".join(l for l in sal.splitlines() if l.startswith("**"))))
    check(rc == 0 and "** Programming Finished **" in sal and "** Verified OK **" in sal,
          "OpenOCD programa y verifica el ELF: el algoritmo de TI corre en el núcleo "
          "simulado, contra la ROM R1")
    check("flash 'msp432'" in sal or "msp432" in sal, "con target/ti/msp432.cfg sin tocar")
    rc2, sal2, _ = corre(ocd_base(a.openocd, p, "target/ti/msp432.cfg") +
                         ["-c", "init", "-c", "sleep 300", "-c", "halt", "-c", "reg pc",
                          "-c", "mdw 0xE000E014", "-c", "resume", "-c", "shutdown"], 60)
    rvr = palabras(sal2, 0xE000E014)
    pc = re.search(r"pc \(/32\): 0x([0-9a-f]+)", sal2)
    print("    tras el reset: SYST_RVR = %s, pc = %s" %
          (rvr[0] if rvr else "?", pc.group(1) if pc else "?"))
    check(rc2 == 0 and rvr and rvr[0] == 1499999,
          "el firmware nuevo arranca: SYST_RVR = 1 499 999 (MAP_SysTick_setPeriod(1500000))")
    texto = s.fin()
    check("pad: corriente" not in texto, "y sin peleas en SWDIO (el modelo de pads no avisa)")

    # ------------------------------------------------------------------------
    grupo("O2 STM32F407VG: lo mismo con target/stm32f4x.cfg")
    p = puerto_libre()
    s = Sim(a.sim, "placas/discovery_min.xml", "verif/fw/blinky/blinky.bin", p)
    check(s.listo, "mcu-sim escucha remote_bitbang en el puerto %d" % p)
    rc, sal, t = corre(ocd_base(a.openocd, p, "target/stm32f4x.cfg") +
                       ["-c", "program verif/fw/uart_demo/uart_demo.bin 0x08000000 verify reset exit"],
                       240)
    print("    %.1f s; %s" % (t, " | ".join(l for l in sal.splitlines() if l.startswith("**"))))
    check(rc == 0 and "** Programming Finished **" in sal and "** Verified OK **" in sal,
          "OpenOCD programa y verifica uart_demo.bin con el algoritmo de stm32f2x")
    check("flash size = 1024 KiB" in sal and "device id = 0x10016413" in sal,
          "identifica el F407: DBGMCU_IDCODE 0x10016413 y F_SIZE = 1024 KiB")
    buzon = []
    for _ in range(10):
        rc2, sal2, _ = corre(ocd_base(a.openocd, p, "target/stm32f4x.cfg") +
                             ["-c", "init", "-c", "sleep 200", "-c", "mdw 0x20000000 6",
                              "-c", "shutdown"], 60)
        buzon = palabras(sal2, 0x20000000)
        if buzon and buzon[0] == 1:
            break
    print("    buzon de uart_demo: %s" % " ".join("%08x" % v for v in buzon))
    check(len(buzon) == 6 and buzon[0] == 1 and buzon[5] == 42000000,
          "el firmware nuevo arranca y acaba: done = 1, PCLK1 = 42 MHz")
    s.fin()

    # ------------------------------------------------------------------------
    grupo("O3 G3: GDB por OpenOCD, con FreeRTOS")
    gdb = shutil.which(a.gdb)
    if not gdb:
        print("  [--  ] OMITIDO: no hay %s" % a.gdb)
    else:
        p = puerto_libre()
        pg = puerto_libre()
        rtos = "verif/fw/msp432p4/freertos_blinky/freertos_blinky.out"
        s = Sim(a.sim, "placas/msp_exp432p401r.xml", "verif/fw/msp432p4/p4_demo/p4_demo.out", p)
        ocd = subprocess.Popen(ocd_base(a.openocd, p, "target/ti/msp432.cfg", gdb=True, gdb_puerto=pg) +
                               ["-c", "msp432.cpu configure -rtos FreeRTOS",
                                "-c", "init", "-c", "program %s verify reset" % rtos],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        # Se espera a que OpenOCD haya programado y escuche GDB
        listo = False
        lineas = []
        t0 = time.time()
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
                            "-ex", "monitor halt",
                            "-ex", "info threads",
                            "-ex", "monitor resume",
                            "-ex", "detach"], 120)
        hilos = re.findall(r"^\*?\s*\d+\s+Thread\s+\S+\s+\"([^\"]+)\"", sal, re.M)
        print("    hilos: %s" % ", ".join(hilos))
        check(rc == 0 and len(hilos) >= 3 and "IDLE" in hilos,
              "info threads enseña las tareas de FreeRTOS (Rx, TX, IDLE y el temporizador)")
        ocd.terminate()
        try:
            ocd.wait(10)
        except subprocess.TimeoutExpired:
            ocd.kill()
        s.fin()

    # ------------------------------------------------------------------------
    grupo("O4 La sonda dice lo que no admite")
    p = puerto_libre()
    s = Sim(a.sim, "placas/msp_exp432p401r.xml", "verif/fw/msp432p4/p4_demo/p4_demo.out", p)
    rc, sal, _ = corre([a.openocd, "-c", "adapter driver remote_bitbang",
                        "-c", "remote_bitbang host localhost", "-c", "remote_bitbang port %d" % p,
                        "-c", "transport select jtag", "-c", "telnet port disabled",
                        "-c", "tcl port disabled", "-c", "gdb port disabled",
                        "-c", "jtag newtap x cpu -irlen 4", "-c", "init", "-c", "shutdown"], 60)
    time.sleep(0.5)
    texto = s.fin()
    check("transport select swd" in texto,
          "con JTAG, mcu-sim avisa de que la sonda es SWD (y OpenOCD no encuentra TAP)")

    print("\nTOTAL OpenOCD: %d comprobaciones OK, %d fallos" % (OK, MAL))
    return 0 if MAL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
