#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# =============================================================================
# construye.sh — Los ejemplos de ESP-IDF que arranca testc6 (E9), compilados
#
#   ./construye.sh <IDF_PATH> <v5.5> [carpeta de trabajo]
#
# Con REUSA=1 no borra la carpeta de trabajo: lo que ya esté compilado allí
# no se vuelve a compilar.
#
# Con el entorno de ESP-IDF ya activado (export.sh, o el PATH con
# riscv32-esp-elf-gcc y el python del entorno), copia de <IDF_PATH>/examples
# hello_world y los ejemplos del núcleo LP de examples/system/ulp/lp_core
# (E10; SIN TOCARLOS, con el prefijo lp_), los de periféricos de E11
# (examples/peripherals: el PCNT, el MCPWM y el ETM; sin tocarlos, con el
# prefijo e11_) y de aquí perifericos y motor (las pruebas de E9.4 y E11,
# nuestras),
# los compila para el esp32c6 y empaqueta en
# idf-<versión>.tar.xz el flash_args y sus tres .bin (el bootloader, la tabla
# de particiones y la aplicación). Son los binarios de ESP-IDF (Apache-2.0):
# ver LICENSE-esp-idf.md. Como el de verif/fw/esp32c3/idf.
# =============================================================================
set -e
IDF="$1"; VER="$2"; TRAB="${3:-/tmp/idf-c6-$VER}"
[ -n "$IDF" ] && [ -n "$VER" ] || { echo "uso: $0 <IDF_PATH> <v5.5> [trabajo]"; exit 1; }
AQUI=$(cd "$(dirname "$0")" && pwd)
[ "$REUSA" = 1 ] || rm -rf "$TRAB"
rm -rf "$TRAB/$VER"; mkdir -p "$TRAB/$VER"
copia() { [ -d "$2" ] && [ "$REUSA" = 1 ] || { rm -rf "$2"; cp -r "$1" "$2"; rm -rf "$2/build" "$2/sdkconfig"; }; }
copia "$IDF/examples/get-started/hello_world" "$TRAB/hello_world"
rm -rf "$TRAB/perifericos" "$TRAB/motor"
cp -r "$AQUI/perifericos" "$TRAB/perifericos"
cp -r "$AQUI/motor" "$TRAB/motor"
# E10: los ejemplos del núcleo LP (examples/system/ulp/lp_core), SIN TOCARLOS
LP="interrupt lp_timer_interrupt inter_cpu_critical_section gpio gpio_intr_pulse_counter gpio_wakeup lp_uart/lp_uart_print lp_uart/lp_uart_echo lp_i2c debugging build_system"
EJ=""
for e in $LP; do
    n="lp_$(basename "$e")"
    copia "$IDF/examples/system/ulp/lp_core/$e" "$TRAB/$n"
    EJ="$EJ $n"
done
# E11: el PCNT, el MCPWM y el ETM (examples/peripherals), SIN TOCARLOS
E11="pcnt/rotary_encoder mcpwm/mcpwm_servo_control mcpwm/mcpwm_capture_hc_sr04 mcpwm/mcpwm_sync timer_group/gptimer_capture_hc_sr04"
for e in $E11; do
    n="e11_$(basename "$e")"
    copia "$IDF/examples/peripherals/$e" "$TRAB/$n"
    EJ="$EJ $n"
done
for a in hello_world perifericos motor $EJ; do
    [ -f "$TRAB/$a/build/flash_args" ] && [ "$REUSA" = 1 ] && [ "$a" != perifericos ] && [ "$a" != motor ] ||
    (cd "$TRAB/$a" && IDF_COMPONENT_MANAGER=0 python "$IDF/tools/idf.py" set-target esp32c6 build)
    mkdir -p "$TRAB/$VER/$a/bootloader" "$TRAB/$VER/$a/partition_table"
    cp "$TRAB/$a/build/flash_args" "$TRAB/$VER/$a/"
    cp "$TRAB/$a/build/bootloader/bootloader.bin" "$TRAB/$VER/$a/bootloader/"
    cp "$TRAB/$a/build/partition_table/partition-table.bin" "$TRAB/$VER/$a/partition_table/"
    # El .bin de la app se llama como el proyecto, no como la carpeta
    app=$(awk '/^0x/ {print $2}' "$TRAB/$a/build/flash_args" | grep -v "/" | head -1)
    cp "$TRAB/$a/build/$app" "$TRAB/$VER/$a/"
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
