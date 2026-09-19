# Vectores de prueba del acelerador criptográfico

Esto es la **fase 0** del plan del F415/F417 (`doc/stm32f4xx/stm32f4xx_vs_415xx.md`,
§9): los datos contra los que se verificarán el CRYP y el HASH cuando se
escriban, traídos antes de escribir una sola línea de modelo.

```
cryp.vec                 16 casos: AES, DES y TDES
hash.vec                 23 casos: MD5, SHA-1 y HMAC
comprueba_vectores.py    los recalcula todos con dos motores independientes
```

```bash
make -C ../.. vectores          # o, desde aquí:
python3 comprueba_vectores.py            # los rápidos
python3 comprueba_vectores.py --lentos   # también el del millón de letras
python3 comprueba_vectores.py --detalle  # una línea por caso
```

Hoy: **55 comprobaciones, las 55 confirmadas por dos motores, 0 fallos.**

## Por qué existe esta carpeta

Un periférico mal modelado suele fallar de una forma visible: un bit que no se
pone, una interrupción que no llega, un registro que lee cero. **Un acelerador
criptográfico mal modelado no falla: cifra.** Devuelve un bloque perfectamente
formado, con el `BUSY` que baja y la FIFO que se vacía, y el alumno descubre el
problema mucho más tarde, cuando su PC no sabe descifrar lo que su placa cifró.

La única red de seguridad contra eso son los vectores de prueba oficiales. Y
esa red solo sirve si los vectores son correctos, así que aquí se aplica la
misma regla que el proyecto usa con los registros y los pines: **ningún número
entra con una sola fuente.** Cada caso dice de dónde sale, y
`comprueba_vectores.py` lo recalcula con dos implementaciones que no son ni
este proyecto ni la fuente original.

## De dónde salen los números

| Fuente | Qué aporta |
| :--- | :--- |
| **NIST SP 800-38A**, apéndice F | AES en ECB, CBC y CTR, con claves de 128, 192 y 256 bits |
| **FIPS PUB 81** | el vector clásico de DES: `"Now is t"` → `3fa40e8a984d4815` |
| **RFC 1321**, apéndice A.5 | la suite de siete mensajes de MD5 |
| **RFC 3174**, §7.3 | los cuatro casos de SHA-1 |
| **RFC 2202** | HMAC-MD5 y HMAC-SHA-1, con clave corta y con clave larga |
| **ST**, `Projects/STM324xG_EVAL/Examples` de CubeF4 V1.28.3 | los vectores de los ejemplos del propio fabricante **para esta placa**, que son los que el alumno reproducirá con el HAL |

Los motores que los recalculan son **OpenSSL** (incluido su proveedor `legacy`,
sin el cual OpenSSL 3 no hace DES simple), **pycryptodome** y el
**`hashlib`/`hmac`** de Python.

La coincidencia entre ST y el NIST no es casualidad y conviene decirla: los
ejemplos de ST usan **los mismos vectores del SP 800-38A**. Eso da una
propiedad muy cómoda —lo que valide este modelo valida también el ejemplo de
ST— y una trampa, porque son la misma fuente contada dos veces. Por eso los
motores independientes no son un adorno.

## Las dos erratas que esto ya ha cazado

Se cuentan porque son la justificación entera de la carpeta, y las dos
aparecieron **el primer día**, antes de existir una línea de CRYP:

1. **La lectura automática del PDF del SP 800-38A devolvió mal el apéndice F.5.**
   Dio el contador inicial de CTR como `00000000000000000000000000000000` cuando
   es `f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff`, y con él seis de los doce bloques de
   CTR-AES192 y CTR-AES256. Se vio porque OpenSSL y pycryptodome decían otra
   cosa —y porque los vectores de ST, que son los mismos, coincidían con ellos—.
2. **Una errata de transcripción propia**, al copiar el digest del cuarto caso
   de SHA-1 del RFC 3174: una `d` de más y dos dígitos bailados
   (`…cdddd90c7…4f60452` por `…cddd90c7…4f460452`). La cazó
   `comprueba_vectores.py` en su primera pasada, que es exactamente para lo que
   está.

Ninguna de las dos habría dado un error al compilar ni al simular. Las dos
habrían dado por bueno un modelo roto, o por roto un modelo bueno.

## El formato

Bloques `[caso]` separados por una línea en blanco, con campos `clave = valor`
y `#` para comentarios. Está pensado para que el banco del F417 (fase 6) lo lea
tal cual o lo convierta en una tabla de C++ sin criterio propio: todo lo que
hace falta para montar la prueba está en el caso, incluido lo que **no** debe
correr en la pasada normal (`lento = si`).

La cabecera de cada `.vec` explica qué ejercita cada grupo de casos —las
fronteras del relleno, los cuatro valores de `DATATYPE`, el bit `LKEY` de la
clave larga— y por qué están elegidos esos y no otros.
