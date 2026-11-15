#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# construye.sh — Los ejemplos de ESP-IDF que arranca testc6 (E9), compilados
#
#   ./construye.sh <IDF_PATH> <v5.5> [carpeta de trabajo]
#
# Con el entorno de ESP-IDF ya activado (export.sh, o el PATH con
# riscv32-esp-elf-gcc y el python del entorno), copia de <IDF_PATH>/examples
# hello_world (SIN TOCARLO) y de aquí perifericos (la prueba de E9.4, nuestra),
# los compila para el esp32c6 y empaqueta en
# idf-<versión>.tar.xz el flash_args y sus tres .bin (el bootloader, la tabla
# de particiones y la aplicación). Son los binarios de ESP-IDF (Apache-2.0):
# ver LICENSE-esp-idf.md. Como el de verif/fw/esp32c3/idf.
# =============================================================================
set -e
IDF="$1"; VER="$2"; TRAB="${3:-/tmp/idf-c6-$VER}"
[ -n "$IDF" ] && [ -n "$VER" ] || { echo "uso: $0 <IDF_PATH> <v5.5> [trabajo]"; exit 1; }
AQUI=$(cd "$(dirname "$0")" && pwd)
rm -rf "$TRAB"; mkdir -p "$TRAB/$VER"
cp -r "$IDF/examples/get-started/hello_world" "$TRAB/hello_world"
cp -r "$AQUI/perifericos" "$TRAB/perifericos"
rm -rf "$TRAB"/*/build "$TRAB"/*/sdkconfig
for a in hello_world perifericos; do
    (cd "$TRAB/$a" && IDF_COMPONENT_MANAGER=0 python "$IDF/tools/idf.py" set-target esp32c6 build)
    mkdir -p "$TRAB/$VER/$a/bootloader" "$TRAB/$VER/$a/partition_table"
    cp "$TRAB/$a/build/flash_args" "$TRAB/$VER/$a/"
    cp "$TRAB/$a/build/bootloader/bootloader.bin" "$TRAB/$VER/$a/bootloader/"
    cp "$TRAB/$a/build/partition_table/partition-table.bin" "$TRAB/$VER/$a/partition_table/"
    cp "$TRAB/$a/build/$a.bin" "$TRAB/$VER/$a/"
done
# Los simbolos que usa testc6 (D10, la depuracion de E9.6), en un fichero aparte
NM=$(command -v riscv32-esp-elf-nm || true)
if [ -n "$NM" ]; then
    { echo "# hello_world de ESP-IDF $VER (el de idf-$VER.tar.xz): los simbolos que usa testc6 (D10, E9.6)"
      "$NM" "$TRAB/hello_world/build/hello_world.elf" | awk '$3=="app_main"||$3=="xTaskIncrementTick"||$3=="xTickCount"||$3=="pxCurrentTCBs"||$3=="vPortTaskWrapper"||$3=="esp_restart" {print "hello_world", $3, "0x"$1}' | sort -k2
    } > "$AQUI/simbolos-$VER.txt"
fi
tar -C "$TRAB" --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cJf "$AQUI/idf-$VER.tar.xz" "$VER"
echo "$AQUI/idf-$VER.tar.xz"
