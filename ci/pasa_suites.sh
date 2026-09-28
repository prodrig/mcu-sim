#!/bin/sh
# ===========================================================================
# ci/pasa_suites.sh [opciones extra de make...]
#
# Ejecuta las tres suites. Existe por un fallo del 2026-09-28 que costo la
# unica cosa que no se puede reconstruir: LA EVIDENCIA.
#
# Aquel dia macOS Intel se paso de los 30 minutos del trabajo, GitHub lo mato,
# y con el se llevo por delante los pasos que faltaban -incluido el
# `upload-artifact` que tiene `if: always()` justamente para esto-. Resultado:
# `gh run view --log` contesto «log not found» y nos quedamos sin saber si
# aquello fue lentitud uniforme o un cuelgue en un sitio. Al relanzarlo sin
# tocar nada paso en minutos, asi que la pregunta sigue abierta y no hay con
# que contestarla.
#
# LAS DOS COSAS QUE HACE, Y POR QUE
#
# 1. UN PRESUPUESTO PROPIO, mas corto que el limite del trabajo. Si se agota,
#    falla ESTA ETAPA en vez de morir EL TRABAJO, y entonces el paso de subir
#    artefactos si se ejecuta y nos quedamos con los tres .log y con el
#    desglose por grupos, que es lo que distingue «uniformemente lento» de
#    «colgado en T25».
#
#    Esto NO es aflojar el limite. El limite del trabajo sigue donde estaba;
#    lo que se gana es que, cuando se agote, quede el cuerpo.
#
# 2. SIN BUFER DE BLOQUE. Las comprobaciones salen por stdout y los avisos de
#    SC_REPORT por stderr; stdout con bufer de bloque y stderr sin el ordenan
#    el log al reves de como ocurrio. Eso ya costo un diagnostico falso: se
#    dijo «se cuelga en T24» -y se escribio en un commit y en I-24- cuando el
#    cuelgue estaba en T25 y T24 era solo lo ultimo que se VEIA. Con
#    `stdbuf -oL` aparecio donde estaba.
#
# EN macOS NO EXISTEN `timeout` NI `stdbuf`: son de GNU coreutils y BSD no los
# trae. Con `brew install coreutils` llegan como `gtimeout` y `gstdbuf`, y por
# eso aqui se buscan los dos nombres. Si no esta ninguno se dice EN VOZ ALTA y
# se sigue sin presupuesto, que es peor pero honesto: una proteccion que se
# desactiva en silencio es peor que no tenerla.
#
# Variables:
#   SUITES_PRESUPUESTO   segundos para CADA suite (por omision 600)
# ===========================================================================
set -eu

cd "$(dirname "$0")/../src"

presupuesto="${SUITES_PRESUPUESTO:-600}"

if   command -v timeout  >/dev/null 2>&1; then LIMITA="timeout $presupuesto"
elif command -v gtimeout >/dev/null 2>&1; then LIMITA="gtimeout $presupuesto"
else
    LIMITA=""
    echo "  [suites] AVISO: no hay timeout ni gtimeout; SIN presupuesto" >&2
    echo "  [suites]        en macOS se arregla con: brew install coreutils" >&2
fi

if   command -v stdbuf  >/dev/null 2>&1; then BUFER="stdbuf -oL -eL"
elif command -v gstdbuf >/dev/null 2>&1; then BUFER="gstdbuf -oL -eL"
else
    BUFER=""
    echo "  [suites] AVISO: no hay stdbuf ni gstdbuf; el log puede salir" >&2
    echo "  [suites]        desordenado si algo falla" >&2
fi

mkdir -p build
fallos=0

for s in test407 test446 test417; do
    echo "  [suites] $s (presupuesto ${presupuesto}s)"

    # El `echo $?` dentro del subshell es como se recoge el estado a traves de
    # la tuberia: `pipefail` no es POSIX y esto tiene que correr igual en el
    # `sh` de Ubuntu, en el de macOS -que es viejo- y en el de MSYS2.
    #
    # El `set +e` de dentro NO sobra: sin el, el `set -e` heredado aborta el
    # subshell en cuanto `make` falla y el `echo $?` no llega a escribirse.
    # Lo aprendi probandolo con un presupuesto de 5 segundos: el `timeout`
    # mataba a make y el script se quedaba sin codigo que leer.
    rm -f build/.rc
    ( set +e; $LIMITA $BUFER make -f Makefile.mcu-sim "$s" "$@" 2>&1; echo $? > build/.rc ) \
        | tee "build/$s.log"
    rc=$(cat build/.rc 2>/dev/null || echo 125)

    if [ "$rc" = "124" ]; then
        echo "  [FALLO] $s agoto su presupuesto de ${presupuesto}s" >&2
        echo "          build/$s.log tiene hasta donde llego: el desglose por" >&2
        echo "          grupos dice si iba lento o si se quedo parado en uno." >&2
        fallos=1
        # Y se para aqui. Si una suite agota su presupuesto, seguir con las
        # otras dos puede comerse el limite del TRABAJO, y entonces GitHub lo
        # mata y perdemos los .log -que es exactamente el fallo del 2026-09-28
        # que este fichero existe para no repetir-.
        echo "  [suites] no sigo con las demas: hay que salir vivo para que" >&2
        echo "           el paso de subir artefactos llegue a ejecutarse." >&2
        break
    elif [ "$rc" != "0" ]; then
        echo "  [FALLO] $s termino con codigo $rc" >&2
        fallos=1
    fi
done

rm -f build/.rc
exit $fallos
