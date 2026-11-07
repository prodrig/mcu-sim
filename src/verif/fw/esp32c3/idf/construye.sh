#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# construye.sh — Los ejemplos de ESP-IDF que arranca testc3 (E4), compilados
#
#   ./construye.sh <IDF_PATH> <v5.5|v6.0> [carpeta de trabajo]
#
# Con el entorno de ESP-IDF ya activado (export.sh, o el PATH con
# riscv32-esp-elf-gcc y el python del entorno), copia de <IDF_PATH>/examples
# los tres ejemplos, los compila para el esp32c3 SIN TOCARLOS salvo esto:
#   * blink: el LED es un GPIO (GPIO8, cada 200 ms), no la tira WS2812 por
#     RMT (E8), y el #include "led_strip.h" queda dentro de su #ifdef: así no
#     hace falta el componente del registro de Espressif;
#   * uart_echo: tal cual (UART1, TX en GPIO4 y RX en GPIO5, 115200).
# y empaqueta en idf-<versión>.tar.xz, para cada ejemplo, el flash_args y sus
# tres .bin (el bootloader, la tabla de particiones y la aplicación). Son los
# binarios de ESP-IDF (Apache-2.0): ver LICENSE-esp-idf.md.
# =============================================================================
set -e
IDF="$1"; VER="$2"; TRAB="${3:-/tmp/idf-c3-$VER}"
[ -n "$IDF" ] && [ -n "$VER" ] || { echo "uso: $0 <IDF_PATH> <v5.5|v6.0> [trabajo]"; exit 1; }
AQUI=$(cd "$(dirname "$0")" && pwd)
rm -rf "$TRAB"; mkdir -p "$TRAB/$VER"
cp -r "$IDF/examples/get-started/hello_world" "$TRAB/hello_world"
cp -r "$IDF/examples/get-started/blink" "$TRAB/blink"
cp -r "$IDF/examples/peripherals/uart/uart_echo" "$TRAB/uart_echo"
rm -rf "$TRAB"/*/build "$TRAB/blink/main/idf_component.yml" "$TRAB"/blink/sdkconfig.defaults.* "$TRAB"/blink/sdkconfig.ci*
printf 'CONFIG_BLINK_GPIO=8\nCONFIG_BLINK_LED_GPIO=y\nCONFIG_BLINK_PERIOD=200\n' > "$TRAB/blink/sdkconfig.defaults"
sed -i 's|^#include "led_strip.h"|#ifdef CONFIG_BLINK_LED_STRIP\n#include "led_strip.h"\n#endif|' "$TRAB/blink/main/blink_example_main.c"
for a in hello_world blink uart_echo; do
    (cd "$TRAB/$a" && IDF_COMPONENT_MANAGER=0 python "$IDF/tools/idf.py" set-target esp32c3 build)
    mkdir -p "$TRAB/$VER/$a/bootloader" "$TRAB/$VER/$a/partition_table"
    cp "$TRAB/$a/build/flash_args" "$TRAB/$VER/$a/"
    cp "$TRAB/$a/build/bootloader/bootloader.bin" "$TRAB/$VER/$a/bootloader/"
    cp "$TRAB/$a/build/partition_table/partition-table.bin" "$TRAB/$VER/$a/partition_table/"
    cp "$TRAB/$a/build/$a.bin" "$TRAB/$VER/$a/"
done
tar -C "$TRAB" --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cJf "$AQUI/idf-$VER.tar.xz" "$VER"
echo "$AQUI/idf-$VER.tar.xz"
