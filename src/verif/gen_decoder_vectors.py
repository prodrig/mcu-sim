#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_decoder_vectors.py — Genera verif/decoder_vectors.h a partir de
doc/stm32f4xx/valida_instrucciones.py.

Toma los casos de prueba del validador de codificaciones (que ya compara las
tablas de doc/refs/stm32f407xx/informe_instrucciones.md contra un ensamblador ARM real), los
ensambla con arm-none-eabi-as en modo Cortex-M4F Thumb y emite la cabecera de
vectores que consume el banco de pruebas de la fase F2.

Uso (desde la raíz del repositorio):
    python3 src/verif/gen_decoder_vectors.py

Requiere arm-none-eabi-as y arm-none-eabi-objcopy en el PATH.
"""

import os
import re
import subprocess
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FUENTE = os.path.join(RAIZ, "doc", "valida_instrucciones.py")
DESTINO = os.path.join(RAIZ, "src", "verif", "decoder_vectors.h")

PROLOGO = ".syntax unified\n.cpu cortex-m4\n.fpu fpv4-sp-d16\n.thumb\n.text\n"

CABECERA = """// =============================================================================
// decoder_vectors.h — Vectores de verificación del decodificador (fase F2)
//
// GENERADO AUTOMÁTICAMENTE a partir de doc/stm32f4xx/valida_instrucciones.py mediante
// verif/gen_decoder_vectors.py. No editar a mano.
//
// Cada entrada es una instrucción real ensamblada por arm-none-eabi-as en modo
// Cortex-M4F Thumb, cuya codificación coincide bit a bit con la documentada en
// doc/refs/stm32f407xx/informe_instrucciones.md (el script de validación da 254/254 correctos).
// El banco de pruebas coloca la codificación en memoria, la ejecuta con la
// sonda del decodificador y comprueba que:
//   * no se genera UsageFault UNDEFINSTR (la instrucción se reconoce), y
//   * se consume el número de bytes correcto (2 para Thumb, 4 para Thumb-2).
// =============================================================================
#ifndef STM32_VERIF_DECODER_VECTORS_H
#define STM32_VERIF_DECODER_VECTORS_H

#include <cstdint>

namespace stm32 {

struct DecoderVector {
    const char* section;    // sección de doc/refs/stm32f407xx/informe_instrucciones.md
    const char* asm_text;   // instrucción en ensamblador
    uint16_t    hw[2];      // codificación en orden de programa
    unsigned    n_hw;       // 1 = 16 bits, 2 = 32 bits
};

static const DecoderVector DECODER_VECTORS[] = {"""

PIE = """};
constexpr unsigned N_DECODER_VECTORS = sizeof(DECODER_VECTORS) / sizeof(DECODER_VECTORS[0]);

} // namespace stm32
#endif // STM32_VERIF_DECODER_VECTORS_H
"""


def ensambla(asm):
    with tempfile.TemporaryDirectory() as td:
        s, o, b = (os.path.join(td, x) for x in ("t.s", "t.o", "t.bin"))
        open(s, "w").write(PROLOGO + asm + "\n")
        r = subprocess.run(["arm-none-eabi-as", "-mthumb", "-o", o, s],
                           capture_output=True, text=True)
        if r.returncode:
            return None
        r = subprocess.run(["arm-none-eabi-objcopy", "-O", "binary",
                            "--only-section=.text", o, b],
                           capture_output=True, text=True)
        if r.returncode:
            return None
        return open(b, "rb").read()


def main():
    src = open(FUENTE, encoding="utf-8").read()
    pat = re.compile(r'add\(\s*"([^"]*)"\s*,\s*("(?:[^"\\]|\\.)*")\s*,'
                     r'\s*"([0-9A-Fa-f ]+)"\s*(?:,\s*(\d+)\s*)?\)')
    casos = [(m.group(1), eval(m.group(2)), m.group(3).strip(),
              int(m.group(4)) if m.group(4) else None)
             for m in pat.finditer(src)]
    if not casos:
        sys.exit("no se han encontrado casos en %s" % FUENTE)

    lineas = [CABECERA]
    for seccion, asm, esperado, nbytes in casos:
        datos = ensambla(asm)
        if datos is None:
            sys.exit("no ensambla: %s" % asm)
        n = nbytes if nbytes else len(esperado.split()) * 2
        ins = datos[:n]
        hw = [ins[i] | (ins[i + 1] << 8) for i in range(0, len(ins), 2)]
        etiqueta = asm.split("\n")[0].replace('"', "'")
        lineas.append('    {"%s", "%s", {0x%04X, 0x%04X}, %d},'
                      % (seccion, etiqueta, hw[0], hw[1] if len(hw) > 1 else 0, len(hw)))
    lineas.append(PIE)
    open(DESTINO, "w", encoding="utf-8").write("\n".join(lineas))
    print("escritos %d vectores en %s" % (len(casos), DESTINO))


if __name__ == "__main__":
    main()
