#!/bin/sh
# =============================================================================
# compila.sh — libmsp430 de la MSP Debug Stack con el parche de mcu-sim (M11)
#
#   herramientas/libmsp430/compila.sh MSPDS SALIDA
#
# MSPDS es la copia del usuario de la MSP Debug Stack de TI (la fuente,
# MSPDebugStack_OS_Package_x_y_z; se probó con la 3.15.1.1). No se toca: se
# copia en SALIDA/mspds, se le aplica mcu-sim.patch y se compila. Deja
#
#   SALIDA/libmsp430.so     la biblioteca (para LD_LIBRARY_PATH)
#   SALIDA/prueba_ezfet     el cliente de prueba de M11 (prueba_ezfet.cpp)
#
# El parche (versionado, no la copia de TI) hace tres cosas, todas por
# variables de entorno y sin cambiar nada si no se dan:
#
#   MSP430_PUERTO_CDC=RUTA  añade RUTA (el pty de la eZ-FET emulada) a la
#                           lista de eZ-FET (UsbCdcIoChannel::createCdcPortList)
#   MSP430_ESCALA_TIEMPO=N  multiplica por N los plazos de las órdenes
#                           (HalExecBuffered): la sonda simulada va más
#                           despacio que el reloj de pared
#   MSP430_VERSION_LONG=1   MSP430_Initialize escribe la versión en 64 bits:
#                           mspdebug la declara long (drivers/tilib.c)
#
# y, en el Makefile, -std=c++14 (Boost de hoy) y la hidapi del sistema
# (-lhidapi-libusb). Hacen falta Boost (system, filesystem, date_time,
# chrono, thread), libhidapi-dev y libusb-1.0-0-dev.
# =============================================================================
set -e
[ $# -eq 2 ] || { echo "uso: $0 MSPDS SALIDA" >&2; exit 2; }
AQUI=$(cd "$(dirname "$0")" && pwd)
MSPDS=$(cd "$1" && pwd)
mkdir -p "$2"
SALIDA=$(cd "$2" && pwd)
[ -f "$MSPDS/DLL430_v3/src/TI/DLL430/UsbCdcIoChannel.cpp" ] || {
    echo "$MSPDS: no es la fuente de la MSP Debug Stack (falta DLL430_v3)" >&2; exit 1; }
if [ ! -f "$SALIDA/mspds/.parcheado" ]; then
    rm -rf "$SALIDA/mspds"
    cp -r "$MSPDS" "$SALIDA/mspds"
    find "$SALIDA/mspds" \( -name '*.o' -o -name '*.so' -o -name '*.a' \) -delete
    (cd "$SALIDA/mspds" && patch -p1 --binary < "$AQUI/mcu-sim.patch")
    H=$(find /usr/include -name hidapi.h | head -1)
    [ -n "$H" ] || { echo "falta hidapi.h (libhidapi-dev)" >&2; exit 1; }
    cp "$H" "$SALIDA/mspds/ThirdParty/include/"
    touch "$SALIDA/mspds/.parcheado"
fi
(cd "$SALIDA/mspds" && make -j"$(nproc)" > "$SALIDA/compila.log" 2>&1) || {
    tail -20 "$SALIDA/compila.log" >&2; exit 1; }
cp "$SALIDA/mspds/libmsp430.so" "$SALIDA/"
c++ -std=c++14 -O1 -I"$SALIDA/mspds/DLL430_v3/include" -o "$SALIDA/prueba_ezfet" \
    "$AQUI/prueba_ezfet.cpp" -L"$SALIDA" -lmsp430
echo "libmsp430: $SALIDA/libmsp430.so"
