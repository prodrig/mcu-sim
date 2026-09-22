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

# `git clone` de la etiqueta y no un tarball: si la etiqueta no existe, falla
# diciendolo, en vez de bajarse un 404 de 9 bytes y morir cuatro pasos mas
# tarde dentro de cmake.
git clone --quiet --depth 1 --branch "$version" \
    https://github.com/accellera-official/systemc.git systemc-fuente

cmake -S systemc-fuente -B systemc-obj \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_INSTALL_PREFIX="$prefijo" \
      "$@"

cmake --build systemc-obj --parallel 4
cmake --install systemc-obj

rm -rf systemc-fuente systemc-obj
echo "  [systemc] instalada"
