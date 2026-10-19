#!/bin/sh
# ===========================================================================
# ci/construye_openocd.sh <prefijo> [revision]
#
# Construye e instala OpenOCD de git donde se le diga, con el controlador
# remote_bitbang. Lo usa `make openocd` (verif/openocd/programa.py: OpenOCD
# programa y depura el modelo por sus pines; T5 del MSP432).
#
# POR QUE DE GIT Y NO EL DE LA DISTRIBUCION. El SWD de remote_bitbang se
# integro en OpenOCD el 2023-12-03 (0f70c6c) y sus ordenes de espera son las
# de ahora (Z/z) desde el 2024-04-07 (79b51fe). La ultima version publicada,
# la 0.12.0, es de enero de 2023: solo trae JTAG en ese controlador, y es la
# que empaquetan Ubuntu 24.04 y Debian. Con ella, OpenOCD dice «only one
# transport option; autoselect 'jtag'» y no hay sesion.
#
# LA REVISION ESTA FIJADA, como la de SystemC: una de master posterior a
# esas dos, la que se uso para cerrar T5. Otra se puede pedir como segundo
# argumento; con otra, programa.py dice cual es.
#
# Si el prefijo ya tiene bin/openocd, no hace nada (la cache del CI).
#
# Hace falta: git, autoconf, automake, libtool, pkg-config, texinfo, un
# compilador de C y libjim-dev (el Jim Tcl del sistema: asi no hay que bajar
# el submodulo).
# ===========================================================================
set -eu

prefijo="${1:?uso: ci/construye_openocd.sh <prefijo> [revision]}"
rev="${2:-2075427d1585dc1bc17f75a51df91a1b20b4ad4b}"   # master, 2026-10-13

if [ -x "$prefijo/bin/openocd" ]; then
    echo "  OpenOCD ya esta en $prefijo: $("$prefijo/bin/openocd" --version 2>&1 | head -1)"
    exit 0
fi

dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
cd "$dir"
git init -q openocd
cd openocd
git remote add origin https://github.com/openocd-org/openocd.git
git fetch -q --depth 1 origin "$rev"
git checkout -q FETCH_HEAD
./bootstrap nosubmodule > bootstrap.log 2>&1 || { tail -20 bootstrap.log; exit 1; }
./configure --prefix="$prefijo" --disable-werror --enable-remote-bitbang \
            --disable-internal-jimtcl --disable-jlink --disable-doxygen-html \
            > configure.log 2>&1 || { tail -30 configure.log; exit 1; }
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" > make.log 2>&1 || { tail -30 make.log; exit 1; }
make install > /dev/null
echo "  OpenOCD instalado en $prefijo: $("$prefijo/bin/openocd" --version 2>&1 | head -1)"
