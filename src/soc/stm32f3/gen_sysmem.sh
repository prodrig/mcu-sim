#!/bin/sh
# gen_sysmem.sh — compila el cargador de sistema propio (cargador/, S7) y
# escribe sysmem.inc, que se versiona: el modelo no necesita arm-none-eabi-gcc.
set -e
cd "$(dirname "$0")"
make -s -C cargador clean all
B=cargador/cargador.bin
# Lo de fabrica empieza en 0x1FFF_F7AC (el UID): el cargador no puede llegar
[ "$(wc -c < $B)" -le 8064 ] || { echo "gen_sysmem: el cargador no cabe" >&2; exit 1; }
{
  echo "// GENERADO por gen_sysmem.sh a partir de cargador/: NO SE EDITA A MANO."
  echo "// sha256 de cargador.bin: $(sha256sum $B | cut -d' ' -f1)"
  od -An -v -tx1 "$B" | sed -E 's/ ([0-9a-f]{2})/0x\1, /g'
} > sysmem.inc
make -s -C cargador clean
