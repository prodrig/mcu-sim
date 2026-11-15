#!/usr/bin/env python3
# =============================================================================
# herramienta_gdb.py — Los stubs internos de GDB hacen lo que OpenOCD haría
#                      con cada chip (soc/herramientas.h)
#
#   H1  STM32F407 (--gdb-dap): al conectar, DBGMCU_CR |= 7 y DBGMCU_APB1_FZ
#       |= IWDG_STOP | WWDG_STOP (target/stm32f4x.cfg). Con el IWDG en marcha,
#       2 s de pared parado no lo disparan; en marcha, sí; y sin el bit de
#       congelar, parado también
#   H2  lo mismo con el STM32F303 (target/stm32f3x.cfg)
#   H3  ESP32-C3 por el stub interno (G1, --gdb-dap): al conectar con la
#       protección de memoria de ESP-IDF echada, reset del sistema
#       (RTC_SW_SYS_RST, como esp32c3_soc_reset); thb app_main, 3 s de pared
#       parado: los perros, apagados (esp32c3_wdt_disable) y ningún reset
#   H4  lo mismo por el stub de pines (G2', --gdb con el JTAG en MTMS-MTDO)
#   H5  MSP432P401R y MSP432E401Y: OpenOCD (ti/msp432.cfg, ti/stellaris.cfg)
#       no hace nada por su cuenta, y el stub tampoco
#
#   python3 verif/herramienta_gdb.py [--sim build/mcu-sim] [--gdb-arm gdb-multiarch]
#                                    [--gdb riscv32-esp-elf-gdb] [--elf hello_world.elf]
# =============================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "esp"))
import openocd_c3 as oc  # noqa: E402

IDF = os.path.join("build", "idf", "v5.5")
fallos = 0


def check(cond, texto):
    global fallos
    print("  [%s] %s" % ("OK  " if cond else "FALLO", texto))
    if not cond:
        fallos += 1
    return cond


class Sim:
    def __init__(self, sim, args, puerto):
        self.log = tempfile.NamedTemporaryFile("w+", suffix=".log", delete=False)
        self.p = subprocess.Popen([sim] + args + ["--port=%d" % puerto, "--ms=100000000"],
                                  stdout=self.log, stderr=subprocess.STDOUT)
        t0 = time.time()
        self.listo = False
        # Sin conectarse a probar: el stub para el núcleo en cuanto alguien se conecta
        while time.time() - t0 < 30 and self.p.poll() is None:
            if ("localhost:%d" % puerto) in self.texto():
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


def gdb(prog, puerto, cmds, elf=None, arm=True, plazo=300):
    a = [prog, "-q", "-batch"] + (["-ex", "set architecture arm"] if arm else []) + \
        ["-ex", "set remotetimeout 60", "-ex", "target extended-remote localhost:%d" % puerto]
    for c in cmds:
        a += ["-ex", c]
    if elf:
        a.append(elf)
    r = subprocess.run(a, capture_output=True, text=True, timeout=plazo)
    return r.stdout + r.stderr


def palabras(o, dir_):
    return [int(x, 16) for x in re.findall(r"0x%08x:\s+0x([0-9a-f]+)" % dir_, o)]


def stm32(nombre, xml, rcc_csr, guion, gdb_arm, sim):
    pu = oc.puerto_libre()
    s = Sim(sim, [xml, "--gdb-dap"], pu)
    try:
        if not check(s.listo, "%s: --gdb-dap" % nombre):
            return
        check("(hace lo que OpenOCD %s)" % guion in s.texto(), "%s: sim dice que imita %s" % (nombre, guion))
        o = gdb(gdb_arm, pu, [
            "x/2wx 0xe0042004",
            "set {int}0x%08x=0x01000000" % rcc_csr, "set {int}0x40003000=0xCCCC",   # RMVF; IWDG en marcha
            "shell sleep 2", "x/wx 0x%08x" % rcc_csr,
            "monitor resume", "shell sleep 1.5", "monitor halt", "x/wx 0x%08x" % rcc_csr,
            "set {int}0x%08x=0x01000000" % rcc_csr, "set {int}0x40003000=0xCCCC",
            "set {int}0xe0042008=0", "shell sleep 2", "x/wx 0x%08x" % rcc_csr,
            "monitor resume", "detach"])
        dbg = re.findall(r"0xe0042004:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", o)
        csr = palabras(o, rcc_csr)
        check(dbg and int(dbg[0][0], 16) & 7 == 7 and int(dbg[0][1], 16) & 0x1800 == 0x1800,
              "%s: al conectar, DBGMCU_CR |= 7 y DBGMCU_APB1_FZ |= 0x1800 (%s)" % (nombre, dbg[0] if dbg else "?"))
        if check(len(csr) == 3, "%s: tres lecturas de RCC_CSR" % nombre):
            IWDGRSTF = 1 << 29
            check(not csr[0] & IWDGRSTF, "%s: IWDG en marcha y 2 s de pared parado: no salta (0x%08x)" % (nombre, csr[0]))
            check(csr[1] & IWDGRSTF, "%s: en marcha, sin refrescarlo, salta (0x%08x)" % (nombre, csr[1]))
            check(csr[2] & IWDGRSTF, "%s: sin DBG_IWDG_STOP, parado tambien salta (0x%08x)" % (nombre, csr[2]))
        else:
            print(o[-1200:])
    finally:
        s.fin()


def esp(nombre, modo, gdb_rv, elf, sim):
    pu = oc.puerto_libre()
    s = Sim(sim, ["placas/esp32c3_devkitm1.xml", os.path.join(IDF, "hello_world", "flash_args"),
                  "--serie", "UART=memoria", "--jtag=pines", modo], pu)
    try:
        if not check(s.listo, "%s: el stub escucha" % nombre):
            return
        # Que ESP-IDF haya echado ya la protección de memoria: con hello_world en marcha
        fin = time.time() + 120
        while time.time() < fin and "Hello world!" not in s.texto():
            time.sleep(0.5)
        o = gdb(gdb_rv, pu, ["thbreak app_main", "continue", "shell sleep 3", "bt 2",
                             "x/wx 0x60020048", "x/wx 0x6001f048", "x/wx 0x60008090", "monitor resume", "detach"],
                elf=elf, arm=False, plazo=600)
        time.sleep(1)
        t = s.texto()
        check("la proteccion de memoria esta echada; reset del chip" in t and "rst:0x3 (RTC_SW_SYS_RST)" in t,
              "%s: al conectar, con la PMS de ESP-IDF echada, reset del sistema (rst:0x3)" % nombre)
        check("Temporary breakpoint 1, app_main ()" in o and "main_task" in o, "%s: thb app_main, continue y bt" % nombre)
        w = [palabras(o, a) for a in (0x60020048, 0x6001f048, 0x60008090)]
        check(all(x == [0] for x in w), "%s: parado, MWDT1, MWDT0 y RWDT apagados (%s)" % (nombre, w))
        check("rst:0x8" not in t and "rst:0x7" not in t and "rst:0x10" not in t,
              "%s: 3 s de pared parado y ningun perro salta" % nombre)
    finally:
        s.fin()


def main():
    ap = argparse.ArgumentParser(description="Los stubs internos de GDB hacen lo que OpenOCD con cada chip")
    ap.add_argument("--sim", default=os.path.join("build", "mcu-sim"))
    ap.add_argument("--gdb-arm", default=None)
    ap.add_argument("--gdb", default=None)
    ap.add_argument("--elf", default=None)
    a = ap.parse_args()
    gdb_arm = a.gdb_arm or shutil.which("gdb-multiarch") or shutil.which("arm-none-eabi-gdb")
    if not gdb_arm:
        print("  [OMITE] H1, H2 y H5: hace falta gdb-multiarch o arm-none-eabi-gdb")
    else:
        stm32("H1 STM32F407", "placas/discovery_min.xml", 0x40023874, "target/stm32f4x.cfg", gdb_arm, a.sim)
        stm32("H2 STM32F303", "placas/stm32f3discovery.xml", 0x40021024, "target/stm32f3x.cfg", gdb_arm, a.sim)
    gdb_rv = a.gdb or shutil.which("riscv32-esp-elf-gdb")
    if not gdb_rv or not a.elf:
        print("  [OMITE] H3 y H4: hacen falta riscv32-esp-elf-gdb y --elf hello_world.elf")
    else:
        esp("H3 ESP32-C3 (G1)", "--gdb-dap", gdb_rv, a.elf, a.sim)
        esp("H4 ESP32-C3 (G2', pines)", "--gdb", gdb_rv, a.elf, a.sim)
    if gdb_arm:
        for xml, nom in (("placas/msp_exp432p401r.xml", "MSP432P401R"), ("placas/msp_exp432e401y.xml", "MSP432E401Y")):
            pu = oc.puerto_libre()
            s = Sim(a.sim, [xml, "--gdb-dap"], pu)
            try:
                if check(s.listo, "H5 %s: --gdb-dap" % nom):
                    o = gdb(gdb_arm, pu, ["info registers pc", "x/wx 0x20000000", "monitor resume", "detach"])
                    check("hace lo que" not in s.texto() and "pc " in o,
                          "H5 %s: OpenOCD no hace nada por su cuenta, y el stub tampoco" % nom)
            finally:
                s.fin()
    print("TOTAL herramienta: %s" % ("todo OK" if not fallos else "%d fallos" % fallos))
    sys.exit(1 if fallos else 0)


if __name__ == "__main__":
    main()
