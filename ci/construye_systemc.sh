#!/bin/sh
# ===========================================================================
# ci/construye_systemc.sh <prefijo> [opciones extra de cmake...]
#
# Construye e instala SystemC donde se le diga. Una sola receta para macOS y
# para MSYS2, porque un paso de instalacion distinto por plataforma es otro
# sitio donde esconder una diferencia.
#
# Si el prefijo ya tiene las cabeceras, no hace nada: eso es lo que convierte
# la cache de la integracion continua en un acierto y no en una recompilacion
# de ocho minutos por cada commit.
#
# EL ESTANDAR DE C++ NO ES NEGOCIABLE. SystemC mete la version, el estandar y
# algunas macros dentro del nombre de un simbolo (`sc_api_version_...`) para
# que la aplicacion y la biblioteca no se compilen con opciones distintas. Si
# aqui se pone C++14 y el modelo usa C++17, el enlazado falla con un
# `undefined reference` que no parece lo que es. Esta contado en
# doc/compilacion.md 5.1.
#
# Se instala la biblioteca ESTATICA (BUILD_SHARED_LIBS=OFF): asi el ejecutable
# no arrastra un rpath a un directorio de la cache, que es un sitio que puede
# no existir cuando alguien se baja el artefacto.
#
# ---------------------------------------------------------------------------
# POR QUE ESTA AQUI `CMAKE_POLICY_VERSION_MINIMUM=3.5`
#
# SystemC 2.3.4 es de 2022 y su CMakeLists.txt declara
# `cmake_minimum_required` por debajo de 3.5. **CMake 4 ha retirado esa
# compatibilidad**, asi que en una maquina moderna la configuracion aborta
# con «Compatibility with CMake < 3.5 has been removed from CMake».
#
# Esta opcion es la salida que el propio CMake propone en el mensaje de
# error, y no oculta nada: no toca como se compila SystemC, solo permite
# seguir leyendo un CMakeLists.txt viejo. Lo que SI hace es documentar un
# hecho que la integracion continua ha sacado a la luz el primer dia:
# **2.3.4 se esta quedando fuera de las cadenas de herramientas actuales.**
# Eso es un argumento concreto, y hasta ahora no habia ninguno, a favor de
# subir a la linea 3.0 -que es el punto I-24 de doc/todo.md, donde consta
# que la decision estaba tomada al reves por falta de motivos-.
# ===========================================================================
set -eu

if [ $# -lt 1 ]; then
    echo "uso: $0 <prefijo> [opciones de cmake...]" >&2
    exit 2
fi

prefijo="$1"; shift
version="${SYSTEMC_VER:-2.3.4}"

if [ -d "$prefijo/include/sysc" ]; then
    echo "  [systemc] ya esta en $prefijo, no reconstruyo"
    exit 0
fi

echo "  [systemc] $version -> $prefijo"
rm -rf systemc-fuente systemc-obj

# Clon completo y luego `checkout` de la etiqueta, en vez de `--depth 1
# --branch`. Con la version superficial, git escupe «refs/tags/2.3.4 ... is
# not a commit!» y deja en el aire si lo que se ha construido es la etiqueta o
# la rama por omision, que hoy es la linea 3.0. Son unos segundos mas, solo
# cuando la cache falla, a cambio de no tener que preguntarselo.
git clone --quiet https://github.com/accellera-official/systemc.git systemc-fuente
git -C systemc-fuente checkout --quiet "$version"

# Y se dice en voz alta QUE se ha construido, que es la mitad del valor de
# todo esto.
echo "  [systemc] fuente: $(git -C systemc-fuente describe --tags --always)"

# En MSYS2, el compilador DEL ENTORNO EN QUE SE ESTA (UCRT64 o MINGW64), sin
# dejar que CMake lo busque: con los dos entornos instalados y el otro en el
# PATH de Windows, CMake cogio el g++ de /mingw64 desde el shell UCRT64 (en la
# maquina del autor, el 2026-11-23), y una SystemC construida contra msvcrt no
# enlaza con un modelo construido contra la UCRT. Con CC/CXX puestos, mandan.
compiladores=""
if [ -n "${MINGW_PREFIX:-}" ]; then
    cc_="${CC:-$MINGW_PREFIX/bin/gcc}"; cxx_="${CXX:-$MINGW_PREFIX/bin/g++}"
    if [ ! -x "$cxx_" ] && [ ! -x "$cxx_.exe" ]; then
        echo "  [systemc] FALLO: no hay $cxx_ (pacman -S ${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}-gcc)" >&2
        exit 1
    fi
    # CMake es un programa de Windows: la ruta, en su forma (C:/msys64/...)
    if command -v cygpath >/dev/null 2>&1; then cc_=$(cygpath -m "$cc_"); cxx_=$(cygpath -m "$cxx_"); fi
    compiladores="-DCMAKE_C_COMPILER=$cc_ -DCMAKE_CXX_COMPILER=$cxx_"
    echo "  [systemc] entorno ${MSYSTEM:-?}: $cxx_, con $(command -v cmake)"
fi
# -Wno-deprecated: el CMakeLists.txt de SystemC 2.3.4 pide una version de
# CMake vieja y CMake 4 lo avisa; es de Accellera, no de aqui, y no afecta
cmake -S systemc-fuente -B systemc-obj -Wno-deprecated $compiladores \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_INSTALL_PREFIX="$prefijo" \
      "$@"

cmake --build systemc-obj --parallel 4
cmake --install systemc-obj

# LA LICENCIA VIAJA CON LA BIBLIOTECA, y no es papeleo.
#
# SystemC es Apache-2.0 y trae un fichero NOTICE con las atribuciones de
# Accellera, Arm, Cadence, Doulos, Intel, NXP, ST, Synopsys y unos cuantos
# mas. La seccion 4 de esa licencia se aplica a distribuir «in Source OR
# OBJECT form», asi que cuando el ejecutable se enlaza ESTATICAMENTE -que es
# lo que hace esto- la biblioteca va dentro del binario y sus dos ficheros
# tienen que ir al lado.
#
# `cmake --install` no los copia, y la fuente se borra tres lineas mas abajo,
# asi que se copian aqui o no se copian nunca.
cp systemc-fuente/LICENSE systemc-fuente/NOTICE "$prefijo/" 2>/dev/null || \
    echo "  [systemc] AVISO: no encuentro LICENSE/NOTICE en la fuente"

rm -rf systemc-fuente systemc-obj
echo "  [systemc] instalada"
