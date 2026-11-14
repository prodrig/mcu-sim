#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# construye.sh — Los ejemplos de ESP-IDF que arranca testc6 (E9), compilados
#
#   ./construye.sh <IDF_PATH> <v5.5> [carpeta de trabajo]
#
# Con el entorno de ESP-IDF ya activado (export.sh, o el PATH con
# riscv32-esp-elf-gcc y el python del entorno), copia de <IDF_PATH>/examples
# hello_world, lo compila para el esp32c6 SIN TOCARLO y empaqueta en
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
rm -rf "$TRAB"/*/build
for a in hello_world; do
    (cd "$TRAB/$a" && IDF_COMPONENT_MANAGER=0 python "$IDF/tools/idf.py" set-target esp32c6 build)
    mkdir -p "$TRAB/$VER/$a/bootloader" "$TRAB/$VER/$a/partition_table"
    cp "$TRAB/$a/build/flash_args" "$TRAB/$VER/$a/"
    cp "$TRAB/$a/build/bootloader/bootloader.bin" "$TRAB/$VER/$a/bootloader/"
    cp "$TRAB/$a/build/partition_table/partition-table.bin" "$TRAB/$VER/$a/partition_table/"
    cp "$TRAB/$a/build/$a.bin" "$TRAB/$VER/$a/"
done
tar -C "$TRAB" --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cJf "$AQUI/idf-$VER.tar.xz" "$VER"
echo "$AQUI/idf-$VER.tar.xz"
