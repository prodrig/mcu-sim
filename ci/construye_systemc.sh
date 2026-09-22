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

cmake -S systemc-fuente -B systemc-obj \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_INSTALL_PREFIX="$prefijo" \
      "$@"

cmake --build systemc-obj --parallel 4
cmake --install systemc-obj

rm -rf systemc-fuente systemc-obj
echo "  [systemc] instalada"
