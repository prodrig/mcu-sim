#!/bin/sh
# ===========================================================================
# ci/comprueba_invariante.sh <suite> <fichero de salida>
#
# Contrasta la salida de una suite contra src/verif/invariantes.txt y devuelve
# 0 solo si coinciden LAS DOS cosas: el numero de comprobaciones y el tiempo
# simulado al picosegundo.
#
# La segunda es la que importa y la que no se comprueba sola. Una suite puede
# pasar entera y haber cambiado el comportamiento del modelo; lo que lo delata
# es que el reloj simulado se mueva. Por eso esto es un FALLO de la
# integracion continua y no un aviso.
#
# POSIX sh a proposito: tiene que correr igual en el `bash` de Ubuntu, en el
# de macOS -que es viejo- y en el de MSYS2.
# ===========================================================================
set -eu

if [ $# -ne 2 ]; then
    echo "uso: $0 <suite> <fichero de salida>" >&2
    exit 2
fi

suite="$1"
salida="$2"
aqui=$(dirname "$0")
manifiesto="$aqui/../src/verif/invariantes.txt"

[ -f "$manifiesto" ] || { echo "no encuentro $manifiesto" >&2; exit 2; }
[ -f "$salida" ]     || { echo "no encuentro $salida" >&2; exit 2; }

linea=$(grep "^$suite[[:space:]]" "$manifiesto" || true)
if [ -z "$linea" ]; then
    echo "  [FALLO] $suite no esta en verif/invariantes.txt" >&2
    exit 1
fi
esp_comp=$(echo "$linea" | awk '{print $2}')
esp_ps=$(echo "$linea"   | awk '{print $3}')
# Cuarta columna: QUE cifra se compara. `total` es el tiempo simulado entero;
# `resto` es ese total MENOS lo que consumen los grupos del servidor de GDB.
# Si falta, `total`, que es lo de siempre.
cual=$(echo "$linea" | awk '{print ($4 == "") ? "total" : $4}')

# «TOTAL     : 2118 comprobaciones OK, 0 fallos» y tambien «TOTAL F446 : 204...»
resumen=$(grep -E '^TOTAL' "$salida" | tail -1)
got_comp=$(echo "$resumen" | sed -n 's/.*: *\([0-9][0-9]*\) comprobaciones.*/\1/p')
got_fallos=$(echo "$resumen" | sed -n 's/.*OK, *\([0-9][0-9]*\) fallos.*/\1/p')

# «Tiempo simulado: 2336217899213 ps»
got_total=$(grep -E '^Tiempo simulado:' "$salida" | tail -1 |
            sed -n 's/^Tiempo simulado: *\([0-9][0-9]*\) *ps.*/\1/p')
# «  el resto                             : 2239552024213 ps»
got_resto=$(grep -E '^ +el resto ' "$salida" | tail -1 |
            sed -n 's/.*: *\([0-9][0-9]*\) *ps.*/\1/p')

if [ "$cual" = "resto" ]; then got_ps="$got_resto"; else got_ps="$got_total"; fi

mal=0
echo "--- $suite ---"

if [ -z "$got_comp" ] || [ -z "$got_fallos" ]; then
    echo "  [FALLO] no encuentro la linea TOTAL en $salida"
    mal=1
elif [ "$got_fallos" != "0" ]; then
    echo "  [FALLO] $got_fallos comprobaciones fallidas"
    mal=1
elif [ "$got_comp" != "$esp_comp" ]; then
    echo "  [FALLO] $got_comp comprobaciones, se esperaban $esp_comp"
    echo "          Si el cambio es querido, actualiza verif/invariantes.txt"
    mal=1
else
    echo "  [OK  ] $got_comp comprobaciones, 0 fallos"
fi

if [ -z "$got_ps" ]; then
    echo "  [FALLO] no encuentro el tiempo simulado en $salida"
    mal=1
elif [ "$got_ps" != "$esp_ps" ]; then
    dif=$((got_ps - esp_ps))
    echo "  [FALLO] tiempo simulado ($cual) $got_ps ps, se esperaba $esp_ps ps"
    echo "          diferencia: $dif ps"
    echo "          Esto NO es ruido de la maquina: el tiempo lo lleva el"
    echo "          planificador de SystemC, no el reloj de pared. Ha cambiado"
    echo "          el comportamiento de algo, o el firmware no es el mismo"
    echo "          binario (verif/huellas.txt, T-22 en doc/todo.md)."
    mal=1
else
    echo "  [OK  ] tiempo simulado ($cual) $got_ps ps, al picosegundo"
fi

# El total se imprime siempre aunque no sea el criterio: es la cifra que la
# documentacion lleva anotando desde la fase 7, y la que hay que mirar cuando
# el resto coincide y aun asi algo huele raro.
if [ "$cual" = "resto" ] && [ -n "$got_total" ]; then
    echo "         (total, con el socket de GDB dentro: $got_total ps)"
fi

exit $mal
