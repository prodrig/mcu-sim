#!/usr/bin/env python3
# =============================================================================
# congela.py — El tiempo simulado frente al reloj de pared con un depurador
#
# common/congela.h. Con el SYSTIMER del ESP32-C3 como reloj (16 MHz, se lee
# por el bus del sistema aunque el núcleo esté parado o en marcha):
#
#   K1  ritmo real: sin --stopped-core-stops-sim, con el núcleo parado por
#       OpenOCD y la sonda callada 2 s de pared, el simulado avanza como mucho
#       eso (antes, unas 120 veces más: el fallo de O5 de openocd_c3.py)
#   K2  --stopped-core-stops-sim con OpenOCD (remote_bitbang): parado, el
#       SYSTIMER no se mueve en 2 s de pared; en marcha, sí
#   K3  lo mismo con GDB por el stub interno (G1): thb app_main, continue,
#       2 s de pared parado, bt; ningún perro salta y el tiempo de la
#       parada y el de la vuelta son el mismo
#   K4  varios núcleos (placas/esp32c3_y_f407.xml): GDB para el STM32F407 por
#       su stub interno y el SYSTIMER del ESP32-C3, que sigue EN MARCHA, no
#       avanza mientras; al soltar el F407 (monitor resume), avanza
#   K5  --stopped-core-stops-sim con CMSIS-DAP por SWD en un STM32F407:
#       halt, lecturas de memoria por el DAP congelado, resume, y un reset
#       halt y un reset run con el núcleo parado (el reset sí avanza)
#   K6  los otros Cortex-M (STM32F303, MSP432P401R, MSP432E401Y) por su stub
#       interno de GDB: parar, leer y seguir
#
#   python3 verif/congela.py [--sim build/mcu-sim] [--openocd RUTA] [--gdb RUTA]
#                            [--gdb-arm gdb-multiarch] [--elf hello_world.elf]
# =============================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "esp"))
import openocd_c3 as oc  # noqa: E402

IDF = os.path.join("build", "idf", "v5.5")
HZ_SYSTIMER = 16e6
fallos = 0


def check(cond, texto):
    global fallos
    print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
    if not cond:
        fallos += 1
    return cond


def puerto():
    return oc.puerto_libre()


class Sim:
    def __init__(self, sim, args, puertos):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
        self.p = subprocess.Popen([sim] + args + ["--ms=100000000"], stdout=self.log, stderr=subprocess.STDOUT)
        t0 = time.time()
        self.listo = False
        # Sin conectarse a probar: el stub de GDB para el nucleo en cuanto
        # alguien se conecta. Se espera a que el simulador diga que escucha
        while time.time() - t0 < 30 and self.p.poll() is None:
            t = self.texto()
            if all(("localhost:%d" % pu) in t for pu in puertos):
                self.listo = True
                break
            time.sleep(0.2)

    def texto(self):
        with open(self.log.name, errors="replace") as f:
            return f.read()

    def fin(self):
        self.p.terminate()
        try:
            self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
        t = self.texto()
        os.unlink(self.log.name)
        return t


def ocd(openocd, scripts, cfg, cmds, plazo=300):
    a = [openocd] + (["-s", scripts] if scripts else []) + ["-f", cfg, "-c", "gdb port disabled",
                                                           "-c", "telnet port disabled", "-c", "tcl port disabled"]
    for c in cmds:
        a += ["-c", c]
    p = subprocess.Popen(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        o, _ = p.communicate(timeout=plazo)
    except subprocess.TimeoutExpired:
        p.kill()
        o, _ = p.communicate()
    return o


# Leer el SYSTIMER (UNIT0) desde OpenOCD: pedir la foto y leer las dos mitades
def lee_systimer():
    return ["mww 0x60023004 0x40000000", "echo \"ST [read_memory 0x60023040 32 1] [read_memory 0x60023044 32 1]\""]


def valores_st(o):
    return [(int(a, 0) << 32) | int(b, 0) for a, b in re.findall(r"ST \{?(0x[0-9a-f]+)\}? \{?(0x[0-9a-f]+)\}?", o)]


def pares_congela(texto):
    para = [float(x) for x in re.findall(r"\[congela\] .* parado: el tiempo simulado se detiene en t = ([0-9.]+) ms", texto)]
    sigue = [float(x) for x in re.findall(r"\[congela\] en marcha otra vez en t = ([0-9.]+) ms", texto)]
    return list(zip(para, sigue))


def main():
    ap = argparse.ArgumentParser(description="El tiempo simulado con un depurador: ritmo real y congelar")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim"))
    ap.add_argument("--openocd", default=None)
    ap.add_argument("--gdb", default=None)
    ap.add_argument("--gdb-arm", default=None)
    ap.add_argument("--elf", default=None)
    a = ap.parse_args()
    openocd, scripts = oc.busca_openocd(a)
    if not openocd:
        print("  [OMITE] no hay openocd-esp32 (--openocd RUTA o $OPENOCD_ESP32)")
        sys.exit(0)
    flash = os.path.join(IDF, "hello_world", "flash_args")
    base = ["placas/esp32c3_devkitm1.xml", flash, "--serie", "UART=memoria", "--jtag=pines"]

    # K1 y K2: OpenOCD por remote_bitbang, con y sin congelar
    for congela in (False, True):
        k = "K2" if congela else "K1"
        pu = puerto()
        s = Sim(a.sim, base + ["--openocd", "--port=%d" % pu] + (["--stopped-core-stops-sim"] if congela else []), [pu])
        try:
            if not check(s.listo, "%s: el simulador sirve remote_bitbang" % k):
                continue
            cfg = oc.cfg_rb(pu)
            o = ocd(openocd, scripts, cfg, ["init", "halt", "sleep 300"] + lee_systimer() + ["sleep 2000"] +
                    lee_systimer() + ["resume", "sleep 1000"] + lee_systimer() + ["shutdown"])
            os.unlink(cfg)
            v = valores_st(o)
            if not check(len(v) == 3, "%s: tres lecturas del SYSTIMER (%d)" % (k, len(v))):
                print(o[-1500:])
                continue
            parado = (v[1] - v[0]) / HZ_SYSTIMER
            marcha = (v[2] - v[1]) / HZ_SYSTIMER
            if not congela:
                check(0.2 < parado <= 2.6,
                      "K1: 2 s de pared con el nucleo parado: %.3f s simulados (no mas que el reloj de pared)" % parado)
            else:
                check(v[1] == v[0], "K2: 2 s de pared con el nucleo parado: el SYSTIMER no se mueve (%d tics)"
                      % (v[1] - v[0]))
                check(marcha > 0, "K2: en marcha otra vez, si: %.3f ms simulados" % (marcha * 1e3))
                pc = pares_congela(s.texto())
                check(len(pc) >= 1 and all(x == y for x, y in pc),
                      "K2: [congela]: cada parada vuelve en el mismo instante (%s)" %
                      ", ".join("%.6f" % x for x, _ in pc[:3]))
        finally:
            s.fin()

    # K3: GDB por el stub interno (G1), congelado
    gdb = a.gdb or shutil.which("riscv32-esp-elf-gdb")
    if not gdb or not a.elf:
        print("  [OMITE] K3: hace falta riscv32-esp-elf-gdb y --elf hello_world.elf")
    else:
        pg = puerto()
        s = Sim(a.sim, base + ["--gdb-dap", "--port=%d" % pg, "--stopped-core-stops-sim"], [pg])
        try:
            if check(s.listo, "K3: el simulador sirve GDB (G1)"):
                r = subprocess.run([gdb, "-q", "-batch", "-ex", "set remotetimeout 60",
                                    "-ex", "target extended-remote localhost:%d" % pg,
                                    "-ex", "monitor reset halt", "-ex", "maintenance flush register-cache",
                                    "-ex", "thbreak app_main", "-ex", "continue", "-ex", "shell sleep 2",
                                    "-ex", "bt 3", "-ex", "detach", a.elf],
                                   capture_output=True, text=True, timeout=300)
                o = r.stdout + r.stderr
                check("Temporary breakpoint 1, app_main ()" in o and "main_task" in o,
                      "K3: thb app_main, continue, 2 s de pared parado y bt")
                time.sleep(1)
                t = s.texto()
                check("rst:0x8" not in t and "rst:0x7" not in t, "K3: ningun perro salta")
                pc = pares_congela(t)
                check(len(pc) >= 1 and all(x == y for x, y in pc),
                      "K3: %d paradas, cada una vuelve en el mismo instante" % len(pc))
        finally:
            s.fin()

    # K4: dos MCUs; GDB para el F407 y el C3, en marcha, se congela con el
    gdb_arm = a.gdb_arm or shutil.which("gdb-multiarch") or shutil.which("arm-none-eabi-gdb")
    if not gdb_arm:
        print("  [OMITE] K4: hace falta gdb-multiarch o arm-none-eabi-gdb")
    else:
        pu, pg = puerto(), puerto()
        xml = tempfile.NamedTemporaryFile("w", suffix=".xml", delete=False, dir="placas")
        with open("placas/esp32c3_y_f407.xml") as f:
            t = f.read()
        t = t.replace('<mcu tipo="ESP32-C3" id="c3" firmware="verif/fw/esp32c3/uart.elf"/>',
                      '<mcu tipo="ESP32-C3" id="c3" firmware="verif/fw/esp32c3/uart.elf" jtag="pines" '
                      'depuracion="openocd" puerto_gdb="%d"/>' % pu)
        t = t.replace('<mcu tipo="STM32F407VG" id="f4" firmware="verif/fw/vcp_demo/vcp_demo.bin"/>',
                      '<mcu tipo="STM32F407VG" id="f4" firmware="verif/fw/vcp_demo/vcp_demo.bin" '
                      'depuracion="dap" puerto_gdb="%d"/>' % pg)
        xml.write(t)
        xml.close()
        s = Sim(a.sim, [xml.name, "--serie", "C3=memoria", "--serie", "F4=memoria", "--stopped-core-stops-sim"],
                [pu, pg])
        try:
            if check(s.listo, "K4: ESP32-C3 (OpenOCD) y STM32F407 (GDB) en la misma placa"):
                cfg = oc.cfg_rb(pu)
                # OpenOCD lee el SYSTIMER del C3 -que sigue en marcha- cada segundo,
                # con la hora de pared; GDB para el F407 un rato en medio
                bucle = ("for {set i 0} {$i < 16} {incr i} { mww 0x60023004 0x40000000; "
                         "echo \"ST [clock milliseconds] [esp32c3 curstate] [read_memory 0x60023040 32 1] "
                         "[read_memory 0x60023044 32 1]\"; sleep 1000 }")
                po = subprocess.Popen([openocd, "-s", scripts, "-f", cfg, "-c", "gdb port disabled",
                                       "-c", "telnet port disabled", "-c", "tcl port disabled", "-c", "init",
                                       "-c", bucle, "-c", "shutdown"],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                time.sleep(5)
                t_para = time.time() * 1e3
                g = subprocess.run([gdb_arm, "-q", "-batch", "-ex", "set architecture arm",
                                    "-ex", "target remote localhost:%d" % pg, "-ex", "info registers pc",
                                    "-ex", "shell sleep 5", "-ex", "monitor resume", "-ex", "detach"],
                                   capture_output=True, text=True, timeout=120)
                t_sigue = time.time() * 1e3
                o, _ = po.communicate(timeout=120)
                os.unlink(cfg)
                lect = [(int(t), e, (int(a, 0) << 32) | int(b, 0)) for t, e, a, b in
                        re.findall(r"ST (\d+) (\w+) \{?(0x[0-9a-f]+)\}? \{?(0x[0-9a-f]+)\}?", o)]
                antes = [v for t, e, v in lect if t < t_para - 200]
                durante = [v for t, e, v in lect if t_para + 1500 < t < t_sigue - 1500]
                despues = [v for t, e, v in lect if t > t_sigue + 500]
                if check(len(antes) >= 2 and len(durante) >= 2 and len(despues) >= 2,
                         "K4: lecturas del SYSTIMER del C3 antes, durante y despues (%d, %d, %d)"
                         % (len(antes), len(durante), len(despues))):
                    check(all(e == "running" for _, e, _ in lect), "K4: el C3 esta en marcha todo el rato")
                    check(antes[-1] > antes[0], "K4: antes de parar el F407, el SYSTIMER del C3 avanza")
                    check(len(set(durante)) == 1,
                          "K4: con el F407 parado por GDB, el del C3 no se mueve (%d lecturas iguales)" % len(durante))
                    check(despues[-1] > despues[0] and despues[0] >= durante[-1],
                          "K4: soltado el F407 (monitor resume), avanza otra vez")
                else:
                    print(o[-1500:])
                pc = pares_congela(s.texto())
                check("pc " in g.stdout and len(pc) >= 1 and all(x == y for x, y in pc) and
                      "f4.core parado" in s.texto(),
                      "K4: [congela] dice que es el nucleo del F407 y vuelve en el mismo instante")
        finally:
            s.fin()
            os.unlink(xml.name)

    # K5: CMSIS-DAP por SWD en un STM32F407, congelado
    pu = puerto()
    s = Sim(a.sim, ["placas/discovery_min.xml", "--cmsis-dap", "--port=%d" % pu, "--stopped-core-stops-sim"], [pu])
    try:
        if check(s.listo, "K5: el simulador sirve CMSIS-DAP (SWD) para un STM32F407"):
            cfg = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
            cfg.write("adapter driver cmsis-dap\ncmsis-dap backend tcp\ncmsis-dap tcp host localhost\n"
                      "cmsis-dap tcp port %d\ntransport select swd\nadapter speed 2000\n"
                      "source [find target/stm32f4x.cfg]\nreset_config none\n" % pu)
            cfg.close()
            o = ocd(openocd, scripts, cfg.name, ["init", "halt", "sleep 1000", "mdw 0x08000000 4",
                                                 "mdw 0x20000000 4", "reg pc", "resume", "sleep 500", "halt",
                                                 "reset halt", "echo \"PC [reg pc]\"", "reset run", "sleep 300",
                                                 "halt", "resume", "shutdown"], 300)
            os.unlink(cfg.name)
            check("0x08000000:" in o and "0x20000000:" in o and "pc (/32)" in o,
                  "K5: halt, mdw y reg pc por el DAP con el tiempo congelado, y resume")
            check("PC pc (/32): 0x08000100" in o,
                  "K5: reset halt con el nucleo parado: el reset avanza y para en el vector (0x08000100)")
            pc = pares_congela(s.texto())
            check(len(pc) >= 2 and all(x == y for x, y in pc),
                  "K5: %d paradas, cada una vuelve en el mismo instante" % len(pc))
            if fallos:
                print(o[-1500:])
    finally:
        s.fin()

    # K6: los otros Cortex-M (F303, MSP432P4, MSP432E4) por su stub interno
    if gdb_arm:
        for xml, nom in (("placas/stm32f3discovery.xml", "STM32F303"), ("placas/msp_exp432p401r.xml", "MSP432P401R"),
                         ("placas/msp_exp432e401y.xml", "MSP432E401Y")):
            pg = puerto()
            s = Sim(a.sim, [xml, "--gdb-dap", "--port=%d" % pg, "--stopped-core-stops-sim"], [pg])
            try:
                if check(s.listo, "K6: %s con --gdb-dap" % nom):
                    g = subprocess.run([gdb_arm, "-q", "-batch", "-ex", "set architecture arm",
                                        "-ex", "target remote localhost:%d" % pg, "-ex", "info registers pc",
                                        "-ex", "shell sleep 1", "-ex", "x/2wx 0x20000000", "-ex", "monitor resume",
                                        "-ex", "detach"], capture_output=True, text=True, timeout=120)
                    time.sleep(1)
                    pc = pares_congela(s.texto())
                    check("pc " in g.stdout and "0x20000000" in g.stdout and len(pc) >= 1 and
                          all(x == y for x, y in pc),
                          "K6: %s: GDB lo para, el tiempo se detiene, lee y sigue en el mismo instante" % nom)
            finally:
                s.fin()

    print("TOTAL congela: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    sys.exit(1 if fallos else 0)


if __name__ == "__main__":
    main()
