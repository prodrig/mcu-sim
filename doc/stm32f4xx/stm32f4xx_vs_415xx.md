# STM32F405/407 frente a STM32F415/417

Análisis elemento a elemento de las cuatro piezas, y plan para incorporar las
**diez referencias** de las familias STM32F415xx y STM32F417xx reutilizando el
modelo que ya existe.

---

## 0. Cómo leer este documento, y qué crédito darle

**Cada afirmación lleva su fuente.** Las etiquetas son:

| | |
| :--- | :--- |
| `[RM0090]` | Reference manual del F405/415/407/417/427/437/429/439, **Rev 22** (mayo de 2026, la vigente) |
| `[DS8626]` | Datasheet del F405xx/407xx |
| `[DS8597]` | Datasheet del F415xx/417xx, **Rev 9** (septiembre de 2020) |
| `[CMSIS]` | Cabeceras de dispositivo de ST: `stm32f405xx.h`, `stm32f407xx.h`, `stm32f415xx.h` y `stm32f417xx.h`, **las cuatro de STM32Cube_FW_F4 V1.28.3** |
| `[CUBEMX]` | Base de datos de STM32CubeMX instalada en la máquina (`db/mcu`), de la que `STM32_open_pin_data` es el subconjunto público de pines |
| `[IR]` | El informe técnico interno de este proyecto |
| `[VS446]` | `doc/stm32f4xx/stm32f407vg_vs_446re.md`, el informe hermano de este |
| **⚠ SIN VERIFICAR** | No se ha podido contrastar; se dice qué documento lo cerraría |

**Dos notas sobre el método, porque cambian el crédito que merece lo que sigue.**

La primera: este documento es el primero del proyecto que se escribe contra el
**RM0090 Rev 22**. Los anteriores leyeron la Rev 18. Lo que aquí importa —los
capítulos 23 (CRYP) y 25 (HASH)— no existe en el modelo todavía, así que no hay
nada que re-verificar; pero conviene saber que la referencia ha subido cuatro
revisiones y que el resto del proyecto sigue anclado a la 18.

La segunda, y es la que más vale: **las comparaciones de pines y de símbolos de
este informe están hechas por máquina, no a ojo**, y sobre ficheros del propio
fabricante. Donde se dice «idéntico» quiere decir que un programa comparó los
dos conjuntos y la diferencia salió vacía. Los recuentos están en §3.2 y §4.

> **ESTADO: las fases 0 a 4 del plan están EJECUTADAS.** Los siete puntos se han
> cerrado; los cinco primeros ya lo estaban al escribir este documento, y los
> dos que quedaban —el alcance y los vectores— se cerraron con código y con
> datos, no con una frase. Está contado en la **§14**, al final, y **encontró
> dos erratas antes de que existiera una línea de modelo**. La **fase 1** está
> en la **§15**: `Periferia` ya tiene sus dos campos, las máscaras del RCC son
> **por referencia** y con ello se cierra un agujero que llevaba abierto desde
> la primera fase del proyecto. La **fase 2** está en la **§16**: el HASH existe,
> calcula MD5, SHA-1 y los dos HMAC, y tiene **banco propio** —el tercer
> ejecutable del proyecto—. La **fase 3** está en la **§17**: el CRYP cifra y
> descifra con AES de 128, 192 y 256 bits en ECB, CBC y CTR, y con DES y TDES
> en ECB y CBC. La **fase 4** está en la **§18**: los dos bloques **ya están
> enchufados** —ventana de AHB2, bit de reloj, posición de vector y celda de
> DMA— y el invariante del F407 **no se movió**. Falta declarar las diez
> referencias, que es la fase 5. Las secciones de más arriba llevan incorporado
> lo verificado.

---

## 1. Resumen ejecutivo: tres frases

**Un F415 es un F405 con el acelerador criptográfico, y un F417 es un F407 con
el acelerador criptográfico. No hay nada más.** Mismo silicio, mismo núcleo,
misma memoria, mismos relojes, mismos encapsulados, mismos pines, misma tabla de
funciones alternativas, mismo `DBGMCU_IDCODE`. La comparación por máquina de las
cabeceras de ST lo dice sin ambigüedad: entre `stm32f405xx.h` y `stm32f415xx.h`
**el único símbolo que cambia y no es del CRYP o del HASH es el nombre del
propio chip**.

**Por eso esto sí cabe en un `McuCaps`**, y es exactamente el caso para el que
se escribió ese descriptor: misma familia, otros rasgos. Lo que no cabe en un
descriptor es lo que no está modelado, y **el CRYP y el HASH no lo están**. Ese
es el trabajo entero: dos bloques nuevos, cinco enganches de integración y diez
descriptores.

**El trabajo de verdad no es el pegamento, son los algoritmos.** Los cinco
enganches —dos ventanas de bus, dos bits de reloj, una posición de vector, tres
celdas de DMA, dos campos de `Periferia`— son unas 120 líneas y se hacen en una
tarde. Lo que cuesta es que **AES, DES/TDES, MD5 y SHA-1 den el resultado
correcto**, porque un acelerador criptográfico que acepta escrituras y devuelve
basura es la peor pieza que puede tener un simulador didáctico: el alumno no ve
un error, ve un cifrado que su PC no sabe descifrar.

---

## 2. Las cuatro piezas, de un vistazo

| | STM32F405xx | STM32F415xx | STM32F407xx | STM32F417xx |
| :--- | :---: | :---: | :---: | :---: |
| Núcleo | Cortex-M4F r0p1 | igual | igual | igual |
| SYSCLK máx. | 168 MHz | igual | igual | igual |
| Flash | 512 KB / 1 MB | **solo 1 MB** | 512 KB / 1 MB | 512 KB / 1 MB |
| SRAM | 112+16+64 CCM, +4 backup | igual | igual | igual |
| Líneas de IRQ | 82 posiciones | igual | igual | igual |
| Ethernet | — | — | sí | sí |
| Cámara (DCMI) | — | — | sí | sí |
| **CRYP** | — | **sí** | — | **sí** |
| **HASH** | — | **sí** | — | **sí** |
| RNG | sí | sí | sí | sí |
| `DBGMCU_IDCODE` | `0x1001 6413` | igual | igual | igual |
| Encapsulados | LQFP64, WLCSP90, LQFP100, LQFP144 | los mismos | LQFP100/144/176, UFBGA176 | los mismos |

*Fuentes: `[DS8626]` tabla 2, `[DS8597]` tabla 2, `[RM0090]` §32.6.1, `[CMSIS]`.*

La fila de la Flash es la única sorpresa del cuadro, y es de catálogo y no de
silicio: **ST no vende ningún F415 de 512 KB**. El F405 sí tiene su `OE`
(WLCSP90, 512 KB); el F415 no tiene equivalente. Las diez referencias de
`[DS8597]`, tabla 1, son:

> **STM32F415xx**: F415RG, F415VG, F415ZG, F415OG
> **STM32F417xx**: F417VG, F417IG, F417ZG, F417VE, F417ZE, F417IE

---

## 3. Lo que NO cambia

Esta sección es la que justifica el tamaño del plan, así que conviene que sea
concreta y verificable en vez de tranquilizadora.

### 3.1 El núcleo, las memorias, los buses, los relojes y la alimentación

**Idénticos, y no por parecido sino por construcción**: es el mismo die. El
`[RM0090]` es un solo documento para las ocho referencias de la familia y no
tiene una sola nota «esto solo aplica al F415/417» fuera de los capítulos 23 y
25. Las cabeceras de ST lo confirman símbolo a símbolo:

```
$ diff <(símbolos de stm32f405xx.h) <(símbolos de stm32f415xx.h)
< #define __STM32F405
> #define __STM32F415
                       ... y 166 símbolos nuevos, todos CRYP_* o HASH_*
```

Es decir: **fuera del acelerador, el F415 y el F405 no difieren en un solo bit
de un solo registro**. Con el F407 y el F417 pasa lo mismo salvo dos símbolos
que ST se dejó, y de eso habla §12.1.

En concreto, y por si alguien busca el punto exacto donde podría haber trabajo
escondido:

* **Núcleo y SCS**: 82 posiciones de vector en las cuatro piezas. La posición 79
  no es nueva —ya existe y está reservada en el F405/F407—; lo nuevo es que en
  el F415/F417 **tiene dueño**. `CoreCaps` no cambia.
* **Mapa de memoria**: las dos ventanas del acelerador, `0x5006 0000` y
  `0x5006 0400`, están **reservadas** en el F405/F407 y ocupadas en el
  F415/F417 `[RM0090]` tabla 1. No hay que mover ninguna frontera de segmento:
  caen dentro del AHB2, que ya se decodifica.
* **RCC**: mismos osciladores, mismos dos PLL, mismos topes. Los dos bits nuevos
  son bits **ya existentes en el registro** y hoy enmascarados a propósito.
* **Alimentación**: `[DS8597]` §2.2.16 y `[DS8626]` §2.2.16 describen el mismo
  regulador. El acelerador no añade modo ni dominio.
* **Matriz de buses**: el CRYP y el HASH son **esclavos** de AHB2, no maestros.
  Los ocho maestros de la matriz siguen siendo ocho. `Conectividad` no cambia.

### 3.2 Los pines: ocho comparaciones, cero diferencias

Esta era la pregunta con más riesgo —el F446 enseñó que dos referencias del
mismo die pueden sacar pads distintos `[VS446]` §15.8 y §22— y la respuesta es
limpia. Se compararon, por máquina, los ficheros de `[CUBEMX]` de cada
referencia criptográfica contra los de su gemela:

| Fichero de ST | contra | Pines | Diferencia |
| :--- | :--- | ---: | :---: |
| `STM32F415RGTx` | `STM32F405RGTx` | 45 | **ninguna** |
| `STM32F415OGYx` | `STM32F405O(E-G)Yx` | 72 | **ninguna** |
| `STM32F415VGTx` | `STM32F405VGTx` | 76 | **ninguna** |
| `STM32F415ZGTx` | `STM32F405ZGTx` | 108 | **ninguna** |
| `STM32F417V(E-G)Tx` | `STM32F407V(E-G)Tx` | 76 | **ninguna** |
| `STM32F417Z(E-G)Tx` | `STM32F407Z(E-G)Tx` | 108 | **ninguna** |
| `STM32F417I(E-G)Tx` | `STM32F407I(E-G)Tx` | 133 | **ninguna** |
| `STM32F417I(E-G)Hx` | `STM32F407I(E-G)Hx` | 133 | **ninguna** |

*(El recuento de la tabla cuenta los pines cuyo nombre es `P<puerto><n>` exacto;
los que ST nombra `PC14-OSC32_IN` y similares quedan fuera de la cuenta pero
dentro de la comparación. El `<IONb>` de ST —51, 72, 82, 114, 140— coincide con
el de las referencias sin cripto, que es el que ya usa `Encapsulado`.)*

Y no solo los pines: **la tabla de funciones alternativas también es idéntica**.
Comparando los pares `(pin, señal)` completos:

* LQFP100: **405 pares** en el F417VG y los **mismos 405** en el F407VG.
* LQFP64: **264 pares** en el F415RG y los **mismos 264** en el F405RG.

Conclusión, y es de las que ahorran una fase entera: **la capa de pines no
necesita ni una línea**. Los seis `Encapsulado` de la familia que ya existen
—`ENC_LQFP64`, `ENC_WLCSP90`, `ENC_LQFP100`, `ENC_LQFP144`, `ENC_LQFP176` y
`ENC_UFBGA176`— sirven tal cual, con su `verificado = true` ya ganado (el del
WLCSP90 costó cerrar I-40, y esa factura ya está pagada). El acelerador
criptográfico **no tiene un solo pin**: vive entero dentro del chip, y esa es la
razón de fondo por la que la comparación sale vacía.

### 3.3 El resto de los periféricos

Ninguno cambia. Ni el número de temporizadores, ni los ADC, ni el bxCAN, ni el
SDIO, ni los dos OTG, ni el FSMC, ni el Ethernet, ni la cámara. La tabla 2 de
`[DS8597]` y la tabla 2 de `[DS8626]` tienen **las mismas filas con los mismos
valores** salvo la fila «Cryptography».

Una consecuencia que conviene no pasar por alto: **el FSMC sigue sin existir en
el LQFP64** (`[DS8597]` tabla 2, columna del F415RG: «No»), que es justo lo que
describe `PERIF_F405_R64`. El F415RG se describe con ese mismo juego de
periféricos más los dos booleanos nuevos.

---

## 4. Lo que sí cambia: dos bloques y cinco enganches

### 4.1 El CRYP

**Qué es** `[RM0090]` §23, que abre diciendo *«This section applies to
STM32F415/417xx and STM32F43xxx devices»*. Es un esclavo AHB2 de 32 bits con dos
FIFO de ocho palabras, que cifra y descifra con:

| Algoritmo | Modos en el **F415/F417** | Claves |
| :--- | :--- | :--- |
| AES | ECB, CBC, CTR | 128, 192, 256 bits |
| DES | ECB, CBC | una sola clave, `K1` |
| TDES | ECB, CBC | tres claves, `K1`, `K2` y `K3` |

*(`[RM0090]` §23.2 lo dice de los dos juntos: «claves de 64, 128 y 192 bits,
incluida la paridad». `K0` no se usa en DES ni en TDES; en AES se usan las
cuatro.)*

**Y lo que NO tiene, que importa igual**: GCM y CCM **no existen en el
F415/F417**, solo en el F42x/F43x `[RM0090]` §23.2. Tampoco existen los
registros de intercambio de contexto `CRYP_CSGCMCCM0..7R` y `CRYP_CSGCM0..7R`:
la tabla 114 —*«CRYP register map and reset values for STM32F415/417xx»*—
**termina en el desplazamiento `0x4C`**, mientras que la 115, la del F43x, sigue
hasta `0x8C`. Es una distinción que el modelo tiene que respetar aunque la
cabecera de ST no la haga: `CRYP_TypeDef` de `stm32f417xx.h` **sí declara** los
dieciséis registros de GCM/CCM, porque es una cabecera compartida por toda la
familia F4. Escribir el modelo desde la cabecera en vez de desde la tabla 114
metería en un F417 dieciséis registros que su silicio no tiene.

**Los registros del F415/F417**, veinte en total:

| Desp. | Registro | Qué hace |
| ---: | :--- | :--- |
| `0x00` | `CRYP_CR` | `CRYPEN`(15), `FFLUSH`(14), `KEYSIZE`(9:8), `DATATYPE`(7:6), `ALGOMODE`(5:3), `ALGODIR`(2) |
| `0x04` | `CRYP_SR` | `BUSY`(4), `OFFU`(3), `OFNE`(2), `IFNF`(1), `IFEM`(0) — reset `0x0000 0003` |
| `0x08` | `CRYP_DIN` | entrada a la FIFO de ocho palabras |
| `0x0C` | `CRYP_DOUT` | salida |
| `0x10` | `CRYP_DMACR` | `DOEN`(1), `DIEN`(0) |
| `0x14` | `CRYP_IMSCR` | máscaras de las dos interrupciones |
| `0x18` | `CRYP_RISR` | estado bruto |
| `0x1C` | `CRYP_MISR` | estado enmascarado |
| `0x20`–`0x3C` | `CRYP_K0LR`…`K3RR` | la clave, ocho medias palabras |
| `0x40`–`0x4C` | `CRYP_IV0LR`…`IV1RR` | los vectores de inicialización |

**Y el detalle que convierte esto en un modelo de hardware y no en una
biblioteca de cifrado**: `[RM0090]` tabla 111 da el **coste en ciclos de HCLK**
por bloque en el F415/F417, y lo da exacto:

| | AES-128 | AES-192 | AES-256 | DES | TDES |
| :--- | ---: | ---: | ---: | ---: | ---: |
| ciclos por bloque | 14 | 16 | 18 | 16 | 48 |

Es decir: el modelo puede ser **fiel en tiempo simulado**, no solo en resultado.
Un alumno que mida cuánto tarda en cifrar un kilobyte obtendrá un número que se
parece al de la tarjeta, y esa es exactamente la clase de cosa por la que este
proyecto existe.

### 4.2 El HASH

**Qué es** `[RM0090]` §25, con la misma cabecera de aplicabilidad. Esclavo AHB2
con una FIFO de entrada de 16 palabras que, junto con el propio `HASH_DIN`,
forma el «IN buffer» de **17 palabras** del que habla §25.3.

| | En el **F415/F417** | En el F43x |
| :--- | :--- | :--- |
| Algoritmos | **MD5 y SHA-1** | + SHA-224 y SHA-256 |
| Palabras de resumen | **5** (`HR0`…`HR4`) | 8 |
| HMAC | sí, con clave corta y larga | sí |
| `HASH_CSRx` | `0x0F8`–`0x1C0` (51 registros) | hasta `0x1CC` (54) |
| Ciclos por bloque | **66** (SHA-1), **50** (MD5) | + 50 en SHA-2 |

*(SHA-224/SHA-256 solo en F43x: `[RM0090]` §25.2. Las cinco palabras de resumen:
la misma sección. Los ciclos: §25.3.1, más «al menos 16 ciclos» para cargar el
bloque.)*

Registros: `HASH_CR` (`0x00`), `HASH_DIN` (`0x04`), `HASH_STR` (`0x08`),
`HASH_HR0..4` (`0x0C`–`0x1C`), `HASH_IMR` (`0x20`), `HASH_SR` (`0x24`, reset
`0x0000 0001`), los `HASH_CSRx` y un **alias de los cinco registros de resumen
en `0x310`–`0x320`** que también existe en el F41x —la tabla 117 lo lista—, de
modo que la ventana del bloque es de 1 KB y no de 256 bytes.

El padding automático es parte del periférico y no del firmware: `NBLW` dice
cuántos bits valen de la última palabra y `DCAL` lanza el relleno y el cálculo
final `[RM0090]` §25.4.4. Modelarlo mal no da error, da un resumen que no
coincide con el de `sha1sum`.

### 4.3 El RCC: dos bits, y un agujero que obligan a tapar

`RCC_AHB2ENR`, `RCC_AHB2RSTR` y `RCC_AHB2LPENR` ganan **el bit 4 (CRYP) y el
bit 5 (HASH)** `[CMSIS]` `RCC_AHB2ENR_CRYPEN_Pos = 4`, `..._HASHEN_Pos = 5`. Hoy
el modelo los enmascara con toda intención: `MASC_F407.ahb2_enr = 0x000000C1`
—bits 0 (DCMI), 6 (RNG) y 7 (OTG FS)— y la suite lo comprueba en **T02** con el
comentario *«el CRYP y el HASH son de un F417 y este chip no los lleva»*. Eso
salió de **I-44**, uno de los cuatro fallos del F407 que destapó el puerto del
F446.

Aquí está el punto más interesante de todo el trabajo, y conviene verlo antes de
escribir una línea. **La máscara del RCC hoy es por FAMILIA, no por
referencia**: `masc()` elige entre `MASC_F407` y `MASC_F446` con un booleano. Si
el F417 se resuelve añadiendo un tercer juego de máscaras, el modelo queda
dividiendo el mundo en «F407, F417 y F446», y a la siguiente referencia hay un
cuarto juego. Peor: **deja sin tapar el agujero que ya existe**, anotado en
`doc/reutilizacion.md` §9.5 y todavía abierto:

> *Los bits de reloj de los periféricos ausentes siguen existiendo. En un F405,
> `RCC_AHB1ENR.ETHMACEN` debería leer cero; aquí se escribe y se lee como en el
> F407.*

Es el mismo error de siempre —**el modelo más permisivo que el silicio**— y es
exactamente la clase que I-41, I-43, I-44 y I-45 vinieron a corregir. La
propuesta de este informe es que **la fase 1 no añada una máscara sino que
convierta las máscaras en una función de `Periferia`**: el RCC ya recibe un
`bool` derivado de ahí (`perif.alguno_f446()`), así que el cambio es pasarle el
`Periferia` entero y quitar de la máscara los bits de lo que el chip no lleva.
Con eso, el mismo trabajo que enciende el CRYP en un F417 **apaga el ETHMACEN en
un F405**, que lleva abierto desde la fase 1.

### 4.4 El NVIC: una posición nueva y una compartida

| Posición | En el F405/F407 | En el F415/F417 |
| ---: | :--- | :--- |
| 79 | reservada, sin fuente | **CRYP** |
| 80 | RNG | **HASH y RNG, compartida** |

`[CMSIS]`: `stm32f417xx.h` declara `CRYP_IRQn = 79` y `HASH_RNG_IRQn = 80`;
`stm32f407xx.h` declara `RNG_IRQn = 80` y define `HASH_RNG_IRQn` como alias suyo
—ST ya dejaba el hueco preparado—.

`n_irq` **no cambia**: siguen siendo 82 posiciones. El enganche del 79 es una
línea. El del 80 necesita una puerta OR, y el modelo ya tiene el molde: `Or2` y
el banco `s_or_in[20]`, del que hoy se usan catorce entradas —`or_irq24`,
`or_irq25`, `or_irq26`, `or_irq43`, `or_irq44`, `or_irq45` y `or_irq54`—. Caben.

### 4.5 El DMA: tres celdas, confirmadas por dos fuentes de ST

| Controlador | Stream | Canal | Petición |
| :--- | ---: | ---: | :--- |
| DMA2 | 5 | 2 | `CRYP_OUT` |
| DMA2 | 6 | 2 | `CRYP_IN` |
| DMA2 | 7 | 2 | `HASH_IN` |

Las dos fuentes coinciden: `[RM0090]` **tabla 44** («DMA2 request mapping») y
`[CUBEMX]` `DMA-STM32F417_dma_v2_0_Modes.xml`, que es el fichero que el
descriptor del propio F417 nombra en su `Version=`.

Y aquí hay una ironía que vale la pena dejar escrita: **ese fichero de CubeMX es
el que este proyecto ya usó para el F407** —el mapa de canales del F407 sale del
fichero del F417, porque es el mismo die—. De modo que las tres celdas del
acelerador ya estaban a la vista cuando se cerró **I-48**, y se dejaron **sin
fuente a propósito**, con su comentario en `soc_f4_bind2.h` y su comprobación en
**T26**. El trabajo de esta fase es literalmente **quitar ese `if` y cablearlas
cuando el chip lleva el acelerador**, y dejar T26 comprobando lo contrario para
el F407, que sigue siendo verdad.

### 4.6 El mapa de memoria: dos ventanas

| Ventana | Bloque | En el F405/F407 |
| :--- | :--- | :--- |
| `0x5006 0000`–`0x5006 03FF` | CRYP | reservada |
| `0x5006 0400`–`0x5006 07FF` | HASH | reservada |
| `0x5006 0800`–`0x5006 0BFF` | RNG | **ya modelado** |

El RNG ya está ahí, en el mismo kilobyte de más arriba, y su alta en el
decodificador es el patrón exacto a copiar:

```cpp
if (mcu.perif.rng) ahb2_dec.add_slave("to_rng", addr::RNG_B, 0x400)->bind(rng.tsk);
else               tapa(rng.tsk, "nc_rng");
```

Con `tapa()`, el bloque no modelado **no contesta a nadie** y tocar su ventana da
error de bus, que es lo que hace el silicio. Es la doctrina de `[VS446]` §8.2 y
de `reutilizacion.md` §9.3: *un periférico ausente no es un periférico apagado*.
La suite lo comprueba hoy en la prueba de direcciones —`0x50060000` lleva la
etiqueta literal *«CRYP: no existe en el F407»*— y esa comprobación tiene que
seguir pasando para el F407 y decir lo contrario para el F417.

---

## 5. La decisión que hay que tomar antes de escribir: implementar o declarar

El proyecto tiene dos formas honestas de meter un bloque que no está modelado, y
las dos están en uso:

1. **`BloqueDeclarado`** —lo que se hizo con el SPDIF-RX y el HDMI-CEC del F446—:
   el bloque ocupa su ventana, el mapa de memoria es fiel, y **cualquier acceso
   avisa de que ese periférico está declarado y no modelado**. No miente, pero
   tampoco sirve.
2. **Modelarlo de verdad**, con sus registros, su temporización y su resultado.

**La recomendación de este informe es modelarlos de verdad, y no es una decisión
de gusto.** Tres razones:

* **Los algoritmos son públicos, cerrados y cortos.** AES (FIPS 197), DES
  (FIPS 46-3), MD5 (RFC 1321) y SHA-1 (RFC 3174) son especificaciones acabadas:
  no hay que inferir nada del silicio. En C++ sin optimizar son ~250 líneas AES,
  ~120 DES, ~90 MD5, ~80 SHA-1.
* **Hay vectores de prueba oficiales**, y eso convierte la verificación en algo
  binario. FIPS 197 trae el cifrado de ejemplo de AES-128/192/256; NIST SP
  800-38A trae los de CBC y CTR; RFC 1321 y RFC 3174 traen los suyos con la
  cadena `"abc"`. No hay margen para un modelo «que parece que va».
* **Es lo que un alumno viene a hacer.** Quien usa un F417 en vez de un F407 lo
  usa *por* el acelerador. Un F417 con el CRYP declarado y no modelado es un
  F407 con otro nombre, y ofrecerlo como F417 sería la mentira silenciosa que
  este proyecto lleva seis fases evitando.

**Lo que sí es negociable es el alcance de la primera entrega.** Una versión
razonable del primer hito: AES-ECB y AES-CBC con clave de 128 bits, y SHA-1 sin
HMAC. Cubre el 90 % de los ejemplos de STM32CubeIDE y deja lo demás para una
segunda pasada — con la condición, otra vez, de que **lo que no esté modelado lo
diga**: `ALGOMODE = 000` (TDES-ECB) en un modelo que solo hace AES tiene que
avisar, no callarse.

---

## 6. Las diez referencias

| Referencia | Encapsulado | E/S | Flash | Sectores | ETH | Cámara | Bus ext. | Gemela ya modelada |
| :--- | :--- | ---: | ---: | ---: | :---: | :---: | :---: | :--- |
| STM32F415RG | LQFP64 | 51 | 1 MB | 12 | — | — | — | STM32F405RG |
| STM32F415OG | WLCSP90 | 72 | 1 MB | 12 | — | — | sí | STM32F405OG |
| STM32F415VG | LQFP100 | 82 | 1 MB | 12 | — | — | sí | STM32F405VG |
| STM32F415ZG | LQFP144 | 114 | 1 MB | 12 | — | — | sí | STM32F405ZG |
| STM32F417VE | LQFP100 | 82 | 512 KB | 8 | sí | sí | sí | STM32F407VE |
| STM32F417VG | LQFP100 | 82 | 1 MB | 12 | sí | sí | sí | STM32F407VG |
| STM32F417ZE | LQFP144 | 114 | 512 KB | 8 | sí | sí | sí | STM32F407ZE |
| STM32F417ZG | LQFP144 | 114 | 1 MB | 12 | sí | sí | sí | STM32F407ZG |
| STM32F417IE | LQFP176 | 140 | 512 KB | 8 | sí | sí | sí | STM32F407IE |
| STM32F417IG | LQFP176 | 140 | 1 MB | 12 | sí | sí | sí | STM32F407IG |

*Recuentos de E/S: `[DS8597]` tabla 2, corroborados por el `<IONb>` de
`[CUBEMX]`. Los diez llevan CRYP y HASH.*

**Diez descriptores, tres juegos de `Periferia` nuevos** —`PERIF_F415_R64`,
`PERIF_F415` y `PERIF_F417`, que son los tres de hoy con dos `true` más— y ni un
encapsulado nuevo. El catálogo pasa de **diecinueve referencias a veintinueve**.

Dos notas de alcance, para que el informe no prometa de más:

* **`STM32F417IG` en UFBGA176 no se distingue de `STM32F417IG` en LQFP176.** Es
  la misma limitación que ya tiene el F407IG, documentada en
  `reutilizacion.md` §9.5: los dos encapsulados comparten reparto y solo se
  distinguen por el nombre. Correcto, porque es el mismo die con otro plástico.
* **No hay F415 de 512 KB.** Añadir un `STM32F415OE` porque «encajaría» sería
  inventarse una referencia que ST no vende. El catálogo solo declara lo que
  existe.

---

## 7. Por qué la suite del F417 tiene que ser un ejecutable aparte

La lección más cara del puerto del F446, y aquí vuelve a aplicar tal cual
`[VS446]` §17.4: **construir un segundo chip dentro del banco del F407 mueve el
invariante del F407**, porque un chip no es inerte —sus procesos despiertan, y el
orden en que SystemC los despierta depende de cuántos módulos hay—.

El invariante es `2336217899213 ps` y lleva siete fases sin moverse un
picosegundo. No se negocia. De ahí, dos reglas para la fase de verificación:

* **Lo que se puede comprobar sin construir nada va en el banco del F407.** Las
  consultas al descriptor —máscaras del RCC, recuentos de pines, celdas con
  fuente, bits implementados— **no cuestan tiempo simulado**, y así es como
  entraron los once miembros del F405/F407 en T127/T128 y como entró I-44 en
  T02. Aquí caben las comprobaciones «el F407 NO lleva esto», que son la mitad
  honesta del trabajo.
* **Lo que exige un F417 vivo va en un banco nuevo**, `top/sc_main_f417.cpp`,
  con su `make test417` y su `make asan417`, siguiendo el patrón exacto de
  `test446`. Ahí van los vectores de FIPS y de los RFC, la transferencia por DMA
  y las interrupciones.

---

## 8. Cuánto se reutiliza, en números

Las cifras de la izquierda son las reales del repositorio, contadas por carpeta:

| Capa | Fichero(s) | Líneas hoy | Qué hay que hacer |
| :--- | :--- | ---: | :--- |
| Núcleo, SCS, FPU, depuración y GDB | `core/` | 5 543 | **nada** |
| Memorias (Flash, RAM, CCM, backup) | `mem/` | 864 | **nada** |
| Buses y matriz | `bus/` | 603 | **nada** |
| Pines y encapsulados | `pins/` | 1 225 | **nada** (§3.2) |
| Netlist de placa y XML | `parts/` | 5 364 | nada, salvo aceptar los tipos nuevos |
| Los veinte periféricos de F4–F7 | `periph/` | 15 549 | **nada** |
| Utilidades, red, TLM | `common/` | 1 905 | **nada** |
| SoC del F446 y los mapas (AF, periféricos) | `soc/` | 1 327 | **nada** |
| RCC | `rcc/` | 1 741 | ~40 líneas: máscaras por `Periferia` (§4.3) |
| Integración | `top/soc_f4*.h` | 1 384 | ~60 líneas: ventanas, IRQ, celdas |
| Descriptores | `top/mcu_caps.h` | 446 | ~70 líneas: tres `Periferia` y diez `McuCaps` |
| **CRYP** | `periph/cryp.h` | 0 | **~700 nuevas** |
| **HASH** | `periph/hash.h` | 0 | **~450 nuevas** |
| Banco del F417 | `top/sc_main_f417.cpp` | 0 | **~800 nuevas** |
| Firmware de verificación | `verif/fw/crypto_demo` | 0 | un `crypto_demo` en C, ~200 líneas |

El modelo son **36 682 líneas** sin contar los dos bancos de pruebas (que suman
otras 16 303). De esas 36 682, **hay que tocar unas 170** y añadir ~1 150: se
reutiliza en torno al **97 %**, y de las 1 150 nuevas la mitad son los cuatro
algoritmos públicos. Para comparar: el puerto del F446 escribió ~4 000 líneas de
periférico y reescribió el RCC entero.

---

## 9. El plan por fases

### Fase 0 — Cerrar lo que está sin verificar *(antes de escribir código)* — **HECHA, §14**

Casi todo cerrado ya al escribir este informe; queda decidir, no investigar:

1. ~~Registros del CRYP y del HASH **del F415/F417**, distinguidos de los del
   F43x~~ — **cerrado**: `[RM0090]` tablas 114 y 117.
2. ~~Ciclos por bloque~~ — **cerrado**: tablas 111 y §25.3.1.
3. ~~Celdas de DMA~~ — **cerrado**, dos fuentes de ST que coinciden.
4. ~~Posiciones de vector~~ — **cerrado**, `[CMSIS]`.
5. ~~¿Cambian los pines o la tabla AF?~~ — **cerrado**, ocho comparaciones por
   máquina, ninguna diferencia.
6. ~~**Decidir el alcance de la primera entrega de algoritmos**~~ — **decidido,
   §14.1: el juego COMPLETO del F415/F417, sin entrega parcial.**
7. ~~**Elegir y anotar la fuente de cada vector de prueba** y meterlos en el
   repositorio como datos~~ — **hecho, §14.2**: `src/verif/vectores/`, 39 casos
   con su procedencia y un comprobador que los recalcula con dos motores
   independientes. `make vectores`.

### Fase 1 — `Periferia` gana dos campos, y las máscaras del RCC dejan de ser por familia — **HECHA, §15**

*Sin añadir un solo chip, y con el invariante intacto.*

* `Periferia` gana `cryp` y `hash`, a `false` explícito en los **seis** juegos
  que hay hoy —`PERIF_F407`, `PERIF_F405`, `PERIF_F405_R64`, `PERIF_F446_M`,
  `PERIF_F446_R` y `PERIF_F446_VZ`—, escritos a mano como documentación, que es
  la costumbre de ese fichero.
* `Rcc` recibe `Periferia` en vez del `bool`, y `masc()` **calcula** la máscara
  quitando los bits de lo que el chip no lleva, en vez de elegir entre dos
  constantes.
* **Se cierra el punto abierto de `reutilizacion.md` §9.5**: en un F405,
  `RCC_AHB1ENR.ETHMACEN` pasa a leer cero; en un F405RG, el bit del FSMC
  también; en un F446, los que ya estaban.
* Comprobaciones nuevas en **T02** —a coste cero de tiempo simulado, con
  `bits_implementados()`— y en el grupo **E1** del banco del F446.
* **Criterio de salida**: F407 2055/2055 en `2336217899213 ps`, F446 200/200, y
  las comprobaciones nuevas en verde. Si el invariante se mueve, la fase está mal
  hecha.

### Fase 2 — El HASH — **HECHA, §16**

Primero el HASH y no el CRYP, porque es la mitad de trabajo y valida el camino
entero —ventana, reloj, interrupción compartida, DMA— antes de meterse con las
claves.

* `periph/hash.h`: registros de la tabla 117, FIFO de entrada, `NBLW`/`DCAL`,
  padding automático, los cuatro tipos de dato (`DATATYPE`), MD5 y SHA-1, HMAC
  con clave corta y larga, `HASH_CSRx`, el alias del resumen en `0x310`.
* Temporización: 66 ciclos de HCLK por bloque en SHA-1, 50 en MD5, más la carga.
* Interrupción por `DCIS`/`DINIS` hacia la posición 80.
* **Criterio de salida**: los vectores de RFC 1321 y RFC 3174 salen bit a bit.

### Fase 3 — El CRYP — **HECHA, §17**

* `periph/cryp.h`: los veinte registros de la tabla 114 **y ni uno más** —nada de
  GCM/CCM—, las dos FIFO de ocho palabras, el intercambio por `DATATYPE`, la
  preparación de clave para descifrar en AES (`ALGOMODE = 111`), `BUSY`, las dos
  interrupciones y las dos peticiones de DMA.
* Núcleos: AES-128/192/256 en ECB, CBC y CTR; DES y TDES en ECB y CBC.
* Temporización de la tabla 111.
* **Criterio de salida**: FIPS 197 y NIST SP 800-38A salen bit a bit, en cifrado
  y en descifrado.

### Fase 4 — La integración — **HECHA, §18**

Las ~60 líneas de pegamento, todas condicionadas por `Periferia`:

* dos altas en `ahb2_dec`, con `tapa()` en la rama contraria;
* `bind_bus_slave` de los dos bloques con su bit de reloj y su reset;
* `cryp.irq(s_irq[79])` y una `Or2` nueva para el 80;
* las tres celdas de DMA y su máscara `celdas_con_fuente`;
* **T26 se queda como está para el F407**, que es la comprobación de que el
  trabajo no ha hecho más permisivo al chip que no lleva el acelerador.

### Fase 5 — Las diez referencias

* Tres `Periferia` nuevas y diez `McuCaps`; `CATALOGO_MCU` pasa a veintinueve.
* `sim --help` y `--mcu` los listan solos: salen del catálogo.
* Una placa de ejemplo con un F417, para que `--valida` los ejercite.
* Comprobaciones de descriptor en el banco del F407, a coste cero.

### Fase 6 — Verificación

* `top/sc_main_f417.cpp`, `make test417`, `make asan417` (§7).
* `verif/fw/crypto_demo`: un firmware en C, compilado con `arm-none-eabi-gcc`,
  que cifra y resume los vectores oficiales y escribe el resultado por el USART,
  igual que haría en la tarjeta.
* Una prueba cruzada como la del F446 `[VS446]` §20.1: **este documento
  convertido en tabla de afirmaciones**, cada fila comprobada contra el modelo.
  Es la que encontró que §9.2 del informe anterior era falso.
* ASan + UBSan limpios en los tres bancos.

### Fase 7 — Documentación

* `reutilizacion.md` §9: la tabla pasa de once referencias a veintiuna, y el
  segundo punto de §9.5 **se tacha**.
* `parts.md`: la lista de tipos que `sim` sabe construir.
* `README.md` y `compilacion.md`: el banco nuevo y sus recuentos.
* `todo.md`: alta de los puntos que la fase 3 deje abiertos —lo que del CRYP no
  se modele— y baja del punto de las máscaras por referencia.
* `chat.md`: el registro, como siempre.

### Orden de los hitos

| Hito | Qué se ve desde fuera |
| :--- | :--- |
| **H1** | En un F405, `RCC_AHB1ENR.ETHMACEN` lee cero *(fase 1)* |
| **H2** | `sha1sum` y el modelo dan el mismo resumen de `"abc"` *(fase 2)* |
| **H3** | El cifrado de FIPS 197 sale bit a bit *(fase 3)* |
| **H4** | Un F417 cifra un bloque **por DMA** y levanta la IRQ 79 *(fase 4)* |
| **H5** | `./build/mcu-sim placa.xml --mcu STM32F417VG --valida` monta la placa *(fase 5)* |
| **H6** | `make test417` en verde, y el invariante del F407 sin moverse *(fase 6)* |

---

## 10. Los riesgos, dichos por adelantado

**1. La tentación de fingir el cifrado.** Un CRYP que acepta todo y devuelve
ceros pasaría la mitad de las pruebas superficiales —el `BUSY` baja, la FIFO se
vacía, la IRQ llega— y sería inservible y peor que no tenerlo. Mitigación: los
vectores oficiales, y la regla de que **cualquier `ALGOMODE` no implementado
avisa por su nombre** en vez de comportarse como el modo de al lado.

**2. El invariante.** Cualquier construcción nueva dentro del banco del F407 lo
mueve. Mitigación: §7, y la costumbre ya establecida de que las comprobaciones
de descriptor sean consultas a coste cero.

**3. Los errores de endianismo son silenciosos.** El `DATATYPE` del CRYP y el
del HASH hacen intercambio automático de bytes, medias palabras o bits, y una
confusión ahí da un resultado perfectamente formado y equivocado. Mitigación:
probar **los cuatro valores de `DATATYPE`**, no solo el `00`.

**4. La cabecera de ST es más grande que el chip.** `CRYP_TypeDef` declara los
registros de GCM/CCM que el F415/F417 no tiene, y `HASH_TypeDef` declara 54
`CSR` donde el F41x tiene 51. Escribir el modelo desde la cabecera en vez de
desde las tablas 114 y 117 metería registros que no existen. Es la misma trampa
que I-44 —máscaras copiadas de donde no tocaba—, y se evita igual: **la tabla
del manual manda sobre la cabecera cuando la cabecera es compartida por varias
referencias**.

---

## 11. Lo que queda sin verificar

Poco, y dicho con su remedio:

* ~~⚠ **El reset exacto de cada `HASH_CSRx`.**~~ — **cerrado en la fase 0**
  leyendo la tabla 117 entera: son **51 registros**, `CSR0` a `CSR50`, de
  `0x0F8` a `0x1C0`; el reset de `CSR0` es **`0x0000 0002`** y el de los otros
  cincuenta, cero. La misma lectura confirma el reset de `HASH_SR`
  (`0x0000 0001`, con `DINIS` a uno) y que el alias de los cinco registros de
  resumen en `0x310`–`0x320` **también existe en el F41x**, no solo en el F43x.
* ⚠ **El comportamiento del CRYP cuando el firmware cambia `ALGOMODE` con
  `BUSY = 1`.** `[RM0090]` §23.6.1 dice que «no tiene efecto»; lo que no dice es
  qué pasa con lo que ya estaba en la FIFO. Se modelará como «se ignora la
  escritura y la FIFO sigue como estaba» y quedará anotado.
* ⚠ **Si el HASH y el RNG compiten por la posición 80**, el modelo tiene que
  poder distinguir cuál la levantó a través de sus `SR`. Está claro en el
  silicio; lo que falta es una prueba que lo ejercite con los dos activos a la
  vez.

---

## 12. Dos discrepancias en la documentación de ST, anotadas

Ninguna afecta al plan, pero las dos harían perder una tarde a quien las
encuentre por su cuenta.

### 12.1 Dos símbolos que el F417 pierde y el F407 tiene

Comparando `stm32f407xx.h` y `stm32f417xx.h` **de la misma versión del paquete**
(V1.28.3), el del F417 **no declara** `DCMI_CR_CRE` ni `SYSCFG_PMC_MII_RMII`, que
el del F407 sí. Los dos chips llevan el mismo DCMI y el mismo SYSCFG, así que
**no es una diferencia de silicio sino una omisión de ST en su cabecera**. El
modelo no debe leer nada en ella; el `[RM0090]`, que es un solo documento para
los dos, no distingue.

*(Y una advertencia de método que esto destapa: el `stm32f407xx.h` vendido en
`verif/fw/cmsis/` es de otra versión del paquete. Comparar cabeceras de
versiones distintas mezcla diferencias de chip con diferencias de edición. Las
comparaciones de este informe están hechas con las cuatro cabeceras de la misma
versión.)*

### 12.2 La sección 23.6.2 está mal titulada en el RM0090 Rev 22

El manual tiene dos secciones seguidas tituladas **«CRYP control register
(CRYP_CR) for STM32F415/417xx»**, la 23.6.1 y la 23.6.2. La segunda **es la del
F43x**: su figura incluye `GCM_CCMPH[1:0]` en los bits 17:16 y `ALGOMODE[3]` en
el 19, que en el F415/F417 están reservados. El error está en el título, y el
índice lo repite.

**Cómo desambiguar sin dudar**: las tablas 114 y 115 sí están bien tituladas y
son distintas —la 114 termina en `0x4C` y la 115 en `0x8C`—. La regla para este
proyecto: **el mapa de registros manda sobre la descripción bit a bit** cuando
las dos se contradicen, porque es la que ST mantiene por referencia.

---

## 13. Resumen del trabajo, en una tabla

| | Cuánto |
| :--- | :--- |
| Referencias nuevas | **10** (4 del F415, 6 del F417) |
| Encapsulados nuevos | **0** |
| Pines nuevos | **0** |
| Entradas de la tabla AF nuevas | **0** |
| Periféricos nuevos | **2** (CRYP y HASH) |
| Bits de RCC nuevos | **2** por registro, en `AHB2ENR`/`RSTR`/`LPENR` |
| Posiciones de vector nuevas | **1** (la 79), más una compartida (la 80) |
| Celdas de DMA nuevas | **3** (DMA2, canal 2, streams 5, 6 y 7) |
| Ventanas de bus nuevas | **2**, ambas dentro del AHB2 ya decodificado |
| Cambios en el núcleo, memorias o buses | **ninguno** |
| Líneas de modelo nuevas | ~1 150 |
| Líneas de verificación nuevas | ~1 000 |
| Puntos abiertos que este trabajo cierra | `reutilizacion.md` §9.5, segundo punto |

---

*Documento escrito contra `[RM0090]` Rev 22, `[DS8597]` Rev 9, `[DS8626]`, las
cuatro cabeceras `[CMSIS]` de STM32Cube_FW_F4 V1.28.3 y la base de datos
`[CUBEMX]` instalada en la máquina. Las comparaciones de pines, señales y
símbolos son automáticas y reproducibles.*

---

## 14. Fase 0: cómo se cerró cada punto

La fase 0 de un plan es la que no produce modelo y evita reescribirlo. Aquí
tenía siete puntos; cinco ya estaban cerrados al escribir las secciones de
arriba, y los dos que quedaban —el alcance y los vectores— se han cerrado con
**una decisión argumentada y con datos en el repositorio**, no con una frase de
intenciones. De propina se cerró el primero de los tres ⚠ de la §11.

### 14.1 El alcance: el juego completo, sin entrega parcial *(punto 6)*

§5 dejaba abierta la posibilidad de una primera entrega reducida —AES-128 en
ECB y CBC, y SHA-1 sin HMAC— y la descarta. **Se implementa el juego completo
que el F415/F417 tiene**: AES de 128, 192 y 256 bits en ECB, CBC y CTR; DES y
TDES en ECB y CBC; MD5, SHA-1 y HMAC con clave corta y larga.

Tres razones, en orden de peso:

**La primera, que el conjunto está cerrado y es pequeño.** No es una decisión
entre «poco» y «mucho», sino entre «poco» y «todo lo que hay»: el F415/F417 no
tiene GCM, ni CCM, ni SHA-2. Lo que ST no puso en este chip es justamente la
mitad cara del problema, y eso ya lo decidió el silicio.

**La segunda, que la entrega parcial no ahorra lo que parece.** Un modelo que
solo hace AES-ECB tiene que **decir en voz alta** que no hace los demás modos:
detectar `ALGOMODE = 000`, avisar por su nombre, no comportarse como el modo de
al lado. Esa maquinaria hay que escribirla, probarla y luego quitarla. Entre
escribirla y escribir el modo de verdad hay muy poca diferencia — y una de las
dos cosas hay que tirarla después.

**Y la tercera, que el riesgo no está donde parece.** Lo que puede salir mal en
este trabajo no es el algoritmo —AES es una especificación acabada, con
vectores oficiales, que se comprueba en un `if`— sino **el alrededor**: el
intercambio de bytes del `DATATYPE`, el orden de las medias palabras de la
clave, la preparación de clave para descifrar, el relleno del HASH, la
interacción con la FIFO y con el DMA. Todo eso hay que hacerlo igual para un
modo que para siete, y es donde se van a ir las tardes.

Lo que **sí** se mantiene del reparto original es el orden de las fases: el
HASH antes que el CRYP (fase 2 y fase 3), porque es la mitad del trabajo y
valida el camino entero —ventana, reloj, interrupción compartida, DMA— antes de
meterse con las claves.

Y lo que no cambia en absoluto es la regla de honestidad: **lo que no esté
modelado lo dice el modelo**, por su nombre y en el momento en que el firmware
lo pide.

### 14.2 Los vectores: 39 casos, dos motores, un comprobador *(punto 7)*

El punto 7 pedía «elegir y anotar la fuente de cada vector, y meterlos en el
repositorio como datos, no como números sueltos dentro de una prueba». Está
hecho, en `src/verif/vectores/`:

```
cryp.vec                 16 casos: AES-ECB/CBC/CTR de 128, 192 y 256 bits,
                         DES y TDES en ECB y CBC, y TDES de dos claves
hash.vec                 23 casos: MD5, SHA-1, HMAC-MD5 y HMAC-SHA-1
comprueba_vectores.py    los recalcula TODOS con dos motores independientes
README.md                de dónde sale cada número y por qué existe la carpeta
```

```
$ make vectores
cryp.vec: 16 casos
hash.vec: 23 casos

RESULTADO  54 comprobaciones con dos motores, 0 con uno solo, 0 fallos, 1 saltadas
Motores    hashlib, openssl, pycrypto
```

*(La saltada es el millón de letras «a» del RFC 3174, que son 15 625 bloques y
está marcado `lento = si`; `make vectores V=--lentos` lo corre también, y
también sale.)*

**Las fuentes.** NIST SP 800-38A apéndice F para todo el AES; FIPS PUB 81 para
el vector clásico de DES; RFC 1321, RFC 3174 y RFC 2202 para MD5, SHA-1 y HMAC;
y **los ejemplos del propio ST para esta placa** —`STM324xG_EVAL/Examples/CRYP`
y `.../HASH` de CubeF4 V1.28.3—, que son los que el alumno reproducirá con el
HAL. Los motores que recalculan son OpenSSL (con su proveedor `legacy`, sin el
cual OpenSSL 3 ya no hace DES simple), pycryptodome y el `hashlib`/`hmac` de
Python.

**Un detalle que conviene saber y que no es una casualidad afortunada**: los
vectores de los ejemplos de ST **son los del SP 800-38A**. Misma clave, mismo
texto claro, mismo vector inicial. Eso es cómodo —lo que valide este modelo
valida también el ejemplo de ST— pero es una sola fuente contada dos veces, y
por eso los motores independientes no son decoración.

**Qué ejercita cada grupo**, porque los casos están elegidos y no cogidos al
azar: el mensaje vacío, cuya única razón de ser es que su relleno es un bloque
entero; mensajes de 1, 3, 14, 26, 62 y 80 bytes, ninguno múltiplo de 4, para
`NBLW`; los tres tamaños frontera del relleno (55, 56 y 64); los 261 bytes del
ejemplo de ST, que son cuatro bloques y un resto de cinco; y HMAC con clave de
4, 16, 20, 80 y 261 bytes, que es lo que distingue `LKEY = 0` de `LKEY = 1`.

### 14.3 Las dos erratas que la fase 0 cazó, sin una línea de modelo

Las dos aparecieron el primer día, y las dos son de la clase que no da error:

**Una del documento leído.** La extracción automática del apéndice F.5 del
SP 800-38A devolvió el contador inicial de CTR como
`00000000000000000000000000000000` cuando es `f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff`,
y con él seis de los doce bloques de CTR-AES192 y CTR-AES256. Saltó porque los
dos motores decían otra cosa — y porque los vectores de ST, que son los mismos,
coincidían con los motores y no con la lectura. Es exactamente el fallo que
`[VS446]` §19.3 documenta para las máscaras del RCC: **un número copiado de
donde no tocaba, que no rompe nada y lo estropea todo**.

**Y otra propia.** Al transcribir el digest del cuarto caso de SHA-1 del
RFC 3174 se coló una `d` de más y dos dígitos bailados
(`…cdddd90c7…4f60452` por `…cddd90c7…4f460452`). La cazó
`comprueba_vectores.py` **en su primera pasada**, que es para lo que está.

Ninguna de las dos habría dado un error al compilar ni al simular. La primera
habría dado por roto un modelo bueno; la segunda, por bueno un modelo roto. Las
dos habrían costado una tarde de buscar el fallo en el sitio equivocado.

### 14.4 De propina: cerrado el primero de los tres ⚠

Leyendo la tabla 117 entera para escribir el fichero de vectores se cerró de
paso el punto de la §11 sobre los `HASH_CSRx`: son **51 registros**, `CSR0` a
`CSR50`, de `0x0F8` a `0x1C0`; el reset de `CSR0` es **`0x0000 0002`** y el de
los otros cincuenta es cero. La misma lectura confirma el reset de `HASH_SR`
—`0x0000 0001`, con `DINIS` a uno— y que el alias de los cinco registros de
resumen en `0x310`–`0x320` **también existe en el F41x**.

Quedan los otros dos ⚠, y los dos son de comportamiento y no de dato: qué pasa
con la FIFO del CRYP cuando se toca `ALGOMODE` con `BUSY = 1`, y la prueba del
HASH y el RNG compitiendo por la posición 80. El primero se cierra con una
decisión anotada en la fase 3; el segundo, con una prueba en la fase 6.

### 14.5 Qué deja lista la fase 0, y qué NO ha tocado

**Lista**: la decisión de alcance, los 39 vectores con su procedencia, el
comprobador, el target `make vectores` y el formato de fichero que el banco de
la fase 6 va a leer sin tener que inventarse nada.

**No ha tocado**: ni una línea del modelo. `src/verif/vectores/` es una carpeta
de datos y un script de Python; no entra en ninguna compilación, no necesita
SystemC y no se enlaza con nada. El invariante del F407 sigue donde estaba,
**`2336217899213 ps`**, con sus 2055 comprobaciones, y el del F446 con sus 200 —
comprobado después de la fase, no supuesto.

---

## 15. Fase 1: las máscaras del RCC, por referencia

La fase 1 no añade un solo chip y no toca un solo periférico. Lo que hace es
**cambiar de sitio una decisión**: hasta aquí, qué bits existían en
`RCC_xxxENR`/`RSTR`/`LPENR` lo decidía la *familia*; a partir de aquí lo decide
la *referencia*. Y de paso cierra un agujero que llevaba abierto desde la
primera fase del proyecto.

### 15.1 El agujero que se cierra

`doc/reutilizacion.md` §9.5 lo tenía escrito, con todas sus letras:

> *Los bits de reloj de los periféricos ausentes siguen existiendo. En un F405,
> `RCC_AHB1ENR.ETHMACEN` debería leer cero; aquí se escribe y se lee como en el
> F407. El periférico no está —su ventana da error de bus—, pero el bit que lo
> enciende sí. Es una diferencia observable y está sin cerrar.*

Es la misma familia de fallo que I-41, I-43, I-44 y I-45: **el modelo más
permisivo que el silicio**. Y el efecto, para un alumno, es de los que no se
diagnostican solos — enciende el reloj del Ethernet en un F405, se lo lee de
vuelta puesto a uno, y desde ese momento está depurando la pregunta
equivocada.

**Se podía haber hecho lo fácil.** Un tercer juego de máscaras, `MASC_F417`, y
el F417 resuelto en cinco minutos. Habría funcionado, habría dejado el modelo
repartiendo el mundo en «F407, F417 y F446», y a la siguiente referencia habría
hecho falta un cuarto juego. Y sobre todo: no habría tocado el F405.

### 15.2 Qué se escribió

Cuatro cambios, y ninguno grande:

**`Periferia` gana `cryp` y `hash`** (`top/mcu_caps.h`). A `false` explícito en
los **seis** juegos que hay —los tres del F405/F407 y los tres del F446—,
escritos a mano como documentación, que es la costumbre del fichero. Hoy las
quince referencias del catálogo dicen «no lo llevo», y eso es verdad: las diez
del F415/F417 llegan en la fase 5, cuando los bloques existan. Declarar el chip
antes que el bloque sería ponerle una etiqueta falsa.

**Una estructura nueva, `BloquesRcc`** (`rcc/reloj_caps.h`), con lo poco que el
RCC necesita saber del descriptor: `eth`, `dcmi`, `cryp`, `hash`, `rng` y el
viejo `f446`. Vive en la capa del RCC, y no en `top/`, para que la dependencia
apunte en la dirección correcta — `top/` conoce `rcc/`, no al revés. El puente
es `Periferia::bloques_rcc()`, que es una línea.

**`Rcc::mascaras()`**, que convierte esa estructura en las quince máscaras. La
tabla de la familia es el punto de partida; lo que el chip no lleva se quita y
lo que lleva y la tabla no trae se añade. Es estática y pública **a propósito**:
así la suite puede preguntar por la máscara de una referencia **sin construir el
chip**, y eso es lo que hace que las comprobaciones nuevas no cuesten un
picosegundo.

**Y las seis constantes de bits**, que no están escritas a mano: son los
`RCC_xxx_yyy_Pos` que la cabecera del F407 define y la del F405 no, y los que la
del F417 define y la del F407 no, extraídos por máquina de las cuatro cabeceras
de STM32Cube_FW_F4 V1.28.3. Es la misma fuente y el mismo método que produjeron
las tablas por familia cuando se cerró **I-44**:

| Bloque | `AHB1ENR` / `AHB1LPENR` | `AHB1RSTR` | `AHB2*` |
| :--- | :--- | :--- | :--- |
| Ethernet | bits 25, 26, 27, 28 | **solo el 25** | — |
| Cámara | — | — | bit 0 |
| CRYP | — | — | bit 4 |
| HASH | — | — | bit 5 |
| RNG | — | — | bit 6 |

Que el reset del MAC tenga **un** bit y su alimentación **cuatro** no es una
errata de la extracción: el MAC se resetea entero y se alimenta por partes.

### 15.3 El ajuste en las dos direcciones, que no es simetría gratuita

La primera versión solo quitaba bits, y la comprobación del F417 falló en el
sitio exacto donde tenía que fallar. El motivo es que **la tabla de la familia
es la del F407, no la del die**: salió de `stm32f407xx.h`, que es una referencia
concreta. Por eso hay bits que sobran en un F405 —la cámara— y bits que
*faltan* en un F417 —el CRYP y el HASH—, y quedarse solo con el `&` habría
dejado al F417 sin poder encender lo único que lo distingue.

Lo cuento porque la prueba que lo cazó **es de un chip que todavía no existe**:
coge el descriptor del F407, le pone `cryp` y `hash` a `true`, y comprueba que
salen los dos bits. La maquinaria de la fase 5 está verificada tres fases antes
de usarse.

### 15.4 Lo que NO se ha tocado, y por qué

**El bit del FSMC se queda.** `Periferia` lo lleva, y la tentación de meterlo en
`BloquesRcc` era evidente: un F405RG no tiene bus externo, luego fuera el bit.
Pero eso sería inventarse una fuente. Que un F405RG no saque el bus es un hecho
del **encapsulado** —no hay pines donde sacarlo— y no de la referencia, y **no
existe una cabecera de ST por encapsulado** que diga si el bit de reloj
desaparece. Para el Ethernet, la cámara, el CRYP y el HASH sí la hay, y por eso
esos cuatro sí entran.

Queda, eso sí, una incoherencia que no es nueva pero que ahora se ve mejor: el
modelo dice que en un LQFP64 la **ventana** del FSMC está reservada —tocarla da
error de bus— y a la vez que su **bit de reloj** existe. Las dos cosas no pueden
ser verdad a la vez. Se anota en `doc/todo.md` y no se decide aquí, porque
decidirla bien necesita una fuente que hoy no tengo.

### 15.5 Cómo se comprueba

**Catorce comprobaciones nuevas en T02** y **tres en el grupo E1** del banco del
F446, todas a coste cero de tiempo simulado, porque preguntan a
`Rcc::mascaras()` en vez de escribir unos por el bus. Las del F446 son las más
aburridas y las que más tranquilizan: comprueban que **no se ha movido nada**,
que es lo que tenía que pasar en una familia que este trabajo no venía a tocar.

Y el hito **H1**, verificado además construyendo los chips de verdad:

```
STM32F407VG  AHB1ENR=0x7E7411FF  AHB2ENR=0x000000C1  ETHMACEN=SE PUEDE ENCENDER
STM32F405VG  AHB1ENR=0x607411FF  AHB2ENR=0x000000C0  ETHMACEN=lee cero
STM32F405RG  AHB1ENR=0x607411FF  AHB2ENR=0x000000C0  ETHMACEN=lee cero
```

### 15.6 El estado, después

| | Antes | Después |
| :--- | ---: | ---: |
| Suite del F407 | 2055 | **2069** |
| Invariante del F407 | `2336217899213 ps` | **`2336217899213 ps`** |
| Suite del F446 | 200 | **203** |
| Invariante del F446 | `1033367277932 ps` | **`1033367277932 ps`** |
| Capa de red | 13 | 13 |
| Vectores | 54 | 54 |
| ASan + UBSan | limpios | **limpios** |

Las cinco placas siguen validando, y la Discovery sigue sin caber en un
LQFP64 por donde tiene que no caber: le faltan los cuatro LED del puerto D.

### 15.7 Qué deja lista la fase 2

El HASH se construirá con `mcu.perif.hash` ya en el descriptor, su bit de reloj
ya abriéndose solo cuando el chip lo lleva, y su ventana esperando en
`0x5006 0400`. Lo que falta es el bloque.

---

## 16. Fase 2: el HASH

El primero de los dos bloques. Calcula **MD5 y SHA-1**, y los dos HMAC
correspondientes; no calcula SHA-224 ni SHA-256, que son del F42x/F43x. Va
primero —y no el CRYP— porque es la mitad de trabajo y valida el camino entero
antes de meterse con las claves.

### 16.1 Dos ficheros, y la frontera entre ellos es lo importante

**`periph/hash_algo.h`** es aritmética pura: las dos rondas, el acumulador de
bits y el relleno. No sabe nada de registros, de buses ni de SystemC.

**`periph/hash.h`** es el protocolo: los registros de la tabla 117, la palabra
pendiente en `HASH_DIN`, `NBW`, el intercambio de `DATATYPE`, el recorte con
`NBLW`, el disparo con `DCAL`, las cuatro fases del HMAC, los 66 o 50 ciclos de
`BUSY`, las dos interrupciones, la petición de DMA y los 51 `HASH_CSRx`.

**Por qué separarlos, que no es manía de organización.** Son dos preguntas que
fallan de formas distintas. «¿Sale el resumen correcto?» se contesta con
vectores y no necesita simulación. «¿Sale por donde el firmware lo pide?»
necesita el bus. Y la razón de fondo: **un `DATATYPE` mal interpretado da un
resumen perfectamente formado y equivocado**, exactamente igual que una ronda
mal escrita. Con las dos cosas en el mismo sitio no habría forma de saber cuál
de las dos falló.

**Y se calcula, no se llama.** Igual que la unidad CRC calcula su polinomio bit
a bit en vez de usar una tabla [`periph/crc.h`], aquí están MD5 y SHA-1
escritos. Un simulador didáctico que delegara el resumen en OpenSSL no estaría
modelando el periférico: estaría tapándolo. Lo que sí se hace es **comprobar**
contra dos implementaciones independientes, y eso ya estaba desde la fase 0.

### 16.2 La parte que da la lata: el mensaje no se mide en bytes

El F415/F417 acepta mensajes cuya longitud **no es múltiplo de ocho bits**.
`HASH_STR.NBLW` dice cuántos bits de la última palabra escrita valen, de 0 a 31,
y el relleno empieza justo ahí. Por eso el acumulador no trabaja con bytes sino
con bits: un vector de bytes no sabría representar un mensaje de 27 bits, y el
silicio sí.

Hay un detalle del contrato que el manual no dice con todas sus letras y que
**lo resuelve el propio HAL de ST**: `__HAL_HASH_SET_NBVALIDBITS` calcula
`NBLW = 8 · (tamaño mod 4)`, de modo que un mensaje de cuatro bytes justos lleva
`NBLW = 0`. Es decir, **`NBLW = 0` significa que la última palabra vale entera**,
no que no valga nada. Leyendo solo el §25.4.4 caben las dos lecturas; leyendo el
código de ST, una sola.

### 16.3 El banco: el tercer ejecutable

`top/sc_main_f417.cpp`, con `make test417`, y es el tercero por la misma razón
por la que el del F446 es el segundo: construir piezas nuevas dentro del banco
del F407 **mueve su invariante**.

El bloque se prueba **suelto**, sin SoC alrededor: en la fase 2 todavía no está
integrado —eso es la fase 4— así que el maestro de pruebas se ata directamente
a su puerto de esclavo. Se gana algo con ello: lo que falle aquí es del bloque y
no del cableado.

**63 comprobaciones**, en siete grupos:

| | Qué mira |
| :--- | :--- |
| **A1** | los valores de reset de la tabla 117, incluido `HASH_CSR0` = `0x0000 0002` |
| **A2** | 51 registros de contexto y el alias del resumen en `0x310` |
| **B1** | los trece casos de hash.vec de MD5 y SHA-1, **por el bus** |
| **B2** | el **mismo** mensaje con los **cuatro** valores de `DATATYPE` |
| **B3** | `NBLW` en las tres fronteras del relleno (55, 56 y 64 bytes) y el mensaje vacío |
| **C1** | los diez HMAC, con clave corta y con clave larga (`LKEY`) |
| **D1** | `BUSY` dura los 66 ciclos de HCLK que dice el §25.3.1 |
| **E1/E2** | las dos interrupciones y la petición de DMA |
| **F1** | el contexto: dos mensajes **intercalados** |
| **G1** | que escribir el `ALGO[1]` del F43x no haga nada, porque aquí SHA-2 no existe |

**B2 es la que más falta hacía** y la que justifica el grupo entero: el mismo
`"abc"` entra cuatro veces, organizado en palabras, medias palabras, bytes y
bits sueltos, y tiene que salir el mismo SHA-1 las cuatro.

**F1 es la que más me gusta.** Los 51 `HASH_CSRx` existen para que una tarea
prioritaria pueda llevarse el bloque a media faena. La prueba empieza un
mensaje, guarda el contexto, **resume otro mensaje entero por encima**, restaura
y termina el primero. Si el contexto no sirviera de verdad, el primero saldría
mal. El formato de esos 51 registros es **el de este modelo y no el de ST** —ST
no lo documenta— y eso está dicho en la cabecera del fichero: lo que el silicio
promete es que guardar y restaurar funciona, y eso es lo que aquí se reproduce.

### 16.4 Los dos fallos que encontró, y quién los encontró

**El primero lo encontró el propio banco**: `DCIS` se levantaba en el mismo
instante del `DCAL`. Es tentador y es falso — `DCAL` no termina el resumen, lo
**lanza**, y el bit se levanta 66 o 50 ciclos después. El síntoma no fue un
resultado malo, fue un reloj: la prueba de `BUSY` midió **38 103 ns donde
esperaba 393**, porque el modelo iba diciendo que había terminado y acumulando
una deuda de ciclos que se pagaba toda junta en el peor momento. Un firmware
real lo habría notado leyendo un resumen que todavía no existía.

**El segundo lo encontró ASan**, y estaba escrito de antemano. `make asan417`
dio un SIGSEGV en `trabajo_proc()`, en la primera línea del hilo, con toda la
pinta de un desbordamiento de pila. No lo era: el banco nuevo **se había
olvidado de incluir `common/asan_opciones.h`**, sin el cual
`detect_stack_use_after_return` convierte las corrutinas de SystemC en un
desastre. Ese fichero existe precisamente porque el proyecto ya pagó esa tarde
una vez, y su cabecera describe el síntoma con la misma frase que apareció. Dos
`set_stack_size` que había puesto persiguiendo la causa equivocada se quitaron:
un comentario que explica un fallo con una causa falsa es peor que no tenerlo.

### 16.5 El estado, después

| | Antes | Después |
| :--- | ---: | ---: |
| Suite del F407 | 2069 | 2069, en `2336217899213 ps` |
| Suite del F446 | 203 | 203, en `1033367277932 ps` |
| **Banco del F417** | — | **63** |
| Vectores (`make hash`) | — | **23/23** contra el núcleo |
| Vectores (`make vectores`) | 54 | 54 |
| Capa de red | 13 | 13 |
| ASan + UBSan | limpios en 2 bancos | **limpios en los 3** |

`make hash` es nuevo y no necesita SystemC: compila `verif/prueba_hash.cpp`
contra el núcleo aritmético y pasa los 23 casos de `hash.vec`, el del millón de
letras incluido si se le pide.

### 16.6 Qué deja lista la fase 3

El CRYP entra por el mismo sitio: `periph/cryp_algo.h` para AES y DES/TDES,
`periph/cryp.h` para los veinte registros de la tabla 114, y sus grupos en el
banco que ya existe. La frontera entre aritmética y protocolo, el formato de los
`.vec` y la costumbre de medir `BUSY` en ciclos ya están puestos.

---

## 17. Fase 3: el CRYP

El segundo bloque, y el que da nombre al acelerador. **AES de 128, 192 y 256
bits en ECB, CBC y CTR; DES y TDES en ECB y CBC**, cifrando y descifrando. Con
eso, el juego completo que el F415/F417 tiene — que es lo que la fase 0 decidió
en §14.1.

### 17.1 El mismo reparto que el HASH, y una decisión nueva

`periph/cryp_algo.h` son los **dos cifradores de bloque** y nada más;
`periph/cryp.h` es todo lo que el silicio pone alrededor: las dos FIFO de ocho
palabras, el intercambio de `DATATYPE`, el encadenado de CBC, el contador de
CTR, la preparación de clave, `BUSY` con los ciclos de la tabla 111, las dos
interrupciones de servicio de FIFO y las dos peticiones de DMA.

**El encadenado no está en el núcleo**, y eso no es casualidad de
implementación: en el silicio esos XOR los hace el bloque **alrededor** del
cifrador, y modelarlo igual es lo que hace que CBC y CTR se puedan probar por
separado del AES.

**La caja-S del AES no es una tabla copiada, se genera.** El proyecto ya tenía
la costumbre —la unidad CRC calcula su polinomio bit a bit en vez de usar una
tabla— y aquí se puede ir un paso más allá: la caja-S **se define** como el
inverso multiplicativo en GF(2⁸) más la transformación afín de la FIPS 197, así
que se construye al arrancar. Con eso no hay 256 números que puedan estar mal
copiados.

**Con DES no se puede, y conviene decirlo.** Sus ocho cajas-S y sus seis
permutaciones son tablas **arbitrarias** elegidas en 1976: no se derivan de
nada, y van escritas. Lo único que las protege es el vector clásico
`"Now is t"` → `3fa40e8a984d4815`, que está en `cryp.vec` desde la fase 0 con
dos implementaciones independientes detrás.

### 17.2 La preparación de clave, y por qué resultó ser fácil

Descifrar en AES-ECB o AES-CBC necesita **preparar la clave** antes
(`ALGOMODE = 111`): el manual dice que el bloque calcula la expansión y «copia
el resultado de vuelta en K0…K3».

Modelar esa copia parecía obligar a implementar la expansión **inversa** —
recuperar todas las subclaves a partir de la última—, que es la parte fea del
AES. No hace falta, y la razón está en la propia tabla del §23.6.10: **los ocho
registros de clave tienen todos sus bits marcados `w`**. Son de solo escritura.
Esa copia no es observable desde el firmware.

Así que el modelo hace lo que **sí** se ve: cobra los ciclos, mantiene `BUSY`
mientras tanto y **deja `CRYPEN` a cero al terminar**, que es lo que el manual
promete y lo que el HAL de ST espera para seguir. El banco lo recorre: descifrar
un caso de ECB o CBC pasa por la fase de preparación igual que lo haría el
firmware.

### 17.3 Lo que este chip no tiene, comprobado

GCM y CCM son del F42x/F43x [§23.2], y con ellos se van los dieciséis registros
de contexto. **La tabla 114 —la de este chip— termina en `0x4C`**; la 115, la
del F43x, sigue hasta `0x8C`. La cabecera `stm32f417xx.h` de ST **sí** declara
los dieciséis, porque es compartida por toda la familia F4: escribir el modelo
desde la cabecera en vez de desde la tabla habría metido en un F417 registros
que su silicio no tiene. Es exactamente la trampa de **I-44**, y por eso hay una
comprobación que lee `0x50` y espera cero.

### 17.4 El fallo que encontró el banco, a un dato de distancia

La condición del servicio de la FIFO de entrada. La primera versión decía «pide
cuando le **caben** cuatro palabras»; el §23.5 dice, literal, «se activa cuando
hay **menos de cuatro** palabras en la FIFO de entrada».

Con una FIFO de ocho, las dos frases coinciden en siete de los nueve tamaños
posibles y **difieren justo en uno**: con cuatro palabras dentro, la versión
buena se calla y la mala sigue pidiendo. Un DMA configurado contra ese bit
habría escrito una palabra de más en el peor momento.

Lo cazó el banco, y de rebote mejoró la prueba: ahora no mira un tamaño
cualquiera sino **los dos lados de la frontera** —con tres pide, con cuatro se
calla—, que es donde viven los errores de este tipo.

### 17.5 Cómo se comprueba

Dos capas, como en el HASH:

**`make cryp`** pasa los 16 casos de `cryp.vec` por el núcleo aritmético, **en
las dos direcciones**: cifrar la entrada tiene que dar la salida, y descifrar la
salida tiene que devolver la entrada. Son 32 comprobaciones y no necesitan
SystemC. Que CTR pase las dos es en sí una comprobación: ahí cifrar y descifrar
son la misma operación.

**El banco** (`make test417`) pasa esos mismos 16 casos **por el bus**, con la
secuencia completa de registros que usaría el firmware —clave por el trozo bajo
de los ocho registros, IV, preparación de clave cuando toca, `CRYPEN`, y luego
bloque a bloque mirando `OFNE`—, y añade los cuatro `DATATYPE`, las banderas de
las FIFO, `FFLUSH`, los ciclos de `BUSY` y las cuatro líneas de salida.

### 17.6 El estado, después

| | Antes | Después |
| :--- | ---: | ---: |
| Banco del F417 | 63 | **124** |
| `make cryp` | — | **32/32**, las dos direcciones |
| `make hash` | 23/23 | 23/23 |
| Suite del F407 | 2069, `2336217899213 ps` | **igual** |
| Suite del F446 | 203, `1033367277932 ps` | **igual** |
| `make vectores` | 54 | 54 |
| Capa de red | 13 | 13 |
| ASan + UBSan | limpios | **limpios** |

### 17.7 Qué queda para la fase 4

Los dos bloques existen y nadie los ha enchufado todavía. La fase 4 son las ~60
líneas de pegamento: dos ventanas en el decodificador de AHB2 con su `tapa()`
en la rama contraria, los dos bits de reloj —que desde la fase 1 se abren
solos—, `cryp.irq` a la posición 79 y una `Or2` para la 80 que el HASH comparte
con el RNG, y las tres celdas de DMA que desde **I-48** están esperando con su
comentario puesto.

---

## 18. Fase 4: la integración

Las sesenta líneas de pegamento. No hay aritmética nueva y no hay un chip nuevo:
lo que hay es enchufar al bus, al reloj, al vector de interrupciones y al DMA
dos bloques que hasta ahora vivían sueltos.

### 18.1 Lo que se conectó

| Enganche | Dónde | Cómo |
| :--- | :--- | :--- |
| Dos ventanas de AHB2 | `soc_f4.h` | `add_slave` si el chip lo lleva, `tapa()` si no |
| Dos bits de reloj | `rcc.h`, `soc_f4_bind2.h` | `P_CRYP` y `P_HASH` al final del enum, bits 4 y 5 del AHB2 |
| Posición 79 | `soc_f4_bind2.h` | `cryp.irq(s_irq[79])`, que en un F407 no tenía dueño |
| Posición 80 | `soc_f4_bind2.h` | una `Or2` nueva: **HASH y RNG la comparten** |
| Tres celdas de DMA | `soc_f4_bind2.h` | DMA2, canal 2, streams 5, 6 y 7 |

**Los dos bloques se construyen siempre**, en las diecinueve referencias del
catálogo, porque la elaboración de SystemC es estática y no hay otra. En un
F405/F407 se quedan **sin camino desde el bus** —su ventana no la decodifica
nadie y tocarla da error— exactamente igual que el Ethernet en un F405. No hace
falta un `if` para el reloj: de eso se encarga la máscara por referencia que
puso la fase 1, y los dos bits sencillamente no existen en un chip sin
acelerador.

**Las tres celdas de DMA llevaban dos fases esperando.** Se dejaron sin fuente a
propósito cuando se cerró **I-48**, con su comentario en el código y su
comprobación en T26, porque el mapa del F407 sale del fichero del F417 y ahí
estaban a la vista. Cablearlas ha sido quitar un comentario y poner un `if`.

### 18.2 El riesgo que había, y que no se materializó

Añadir dos módulos **con su propio `SC_THREAD`** a un SoC que construyen las
diecinueve referencias era el riesgo real de esta fase. La lección de **I-42**
es que el orden en que SystemC despierta los procesos depende de cuántos hay, y
el invariante del F407 lleva nueve fases sin moverse un picosegundo.

**No se movió.** 2071 comprobaciones en `2336217899213 ps`, con el CRYP y el
HASH construidos dentro. La diferencia con el caso del F446 —donde añadir un
chip sí lo movía— es que aquí los procesos nuevos **nunca despiertan**: sus
puertos están atados a señales que nadie mueve, que es el mismo motivo por el
que el Ethernet de un F405 no cuesta nada.

### 18.3 Las dos cosas que salieron mal

**La primera la cazó SystemC en la elaboración**, que es la mejor hora para
cazar algo:

```
Error: (E115) sc_signal<T> cannot have more than one driver:
 signal `tb.dut.signal_29'
 first driver `tb.dut.i2s2ext.irq'
 second driver `tb.dut.hash.irq'
```

El banco de entradas para las puertas OR tiene veinte posiciones y yo cogí la 14
y la 15, que ya eran de los bloques de extensión del I2S. Las libres eran las
dos últimas. Un error de recurso compartido, detectado antes de simular un solo
picosegundo y con los dos culpables impresos por su nombre.

**La segunda la cazó el invariante.** La prueba de ventanas reservadas del F407
es una lista de direcciones que se leen por el bus esperando error, y añadir la
del HASH a esa lista parecía lo natural. El resultado:

```
TOTAL     : 2070 comprobaciones OK, 0 fallos
Tiempo simulado: 2336217961713 ps        <- 62 500 ps de mas
```

**Una lectura por el bus cuesta tiempo simulado**, y el invariante no se mueve ni
por una comprobación buena. La versión que quedó pregunta al **decodificador**
—`ahb2_dec.decodes(...)`— y no cuesta nada, que es la misma costumbre que la
fase 1 usó para las máscaras del RCC. De paso comprueba también que el RNG, que
está en el mismo kilobyte, **sí** se decodifica: la asimetría era el error
plausible.

### 18.4 Cómo se comprueba una integración cuando el chip aún no existe

Las diez referencias no se declaran hasta la fase 5. Para probar el cableado
hace falta un chip que **sí** lleve el acelerador, y el banco se lo fabrica: un
**descriptor de laboratorio**, un F407VG con `cryp` y `hash` a `true`, hecho en
`sc_main_f417.cpp` y **no** en `mcu_caps.h`.

La distinción no es un tecnicismo. El catálogo dice lo que el proyecto afirma
modelar —«no hay aquí ningún chip que el proyecto afirme modelar y no
modele»— y un banco de pruebas puede recombinar piezas para mirarlas. Es lo
mismo que hizo la fase 1 al calcular la máscara de un F417 que no existía.

Con ese chip, el grupo **I1** comprueba lo que la fase añade:

* `RCC_AHB2ENR` abre los bits 4 y 5 — **`0xF1`**, que es `0xC1` más los dos;
* **sin encender el reloj, las dos ventanas dan error de bus**, y con él contestan;
* un **AES-ECB-128 entero por el bus del chip** da el vector F.1.1 del SP 800-38A;
* el CRYP levanta la **posición 79**, que en un F407 no tiene dueño;
* el HASH levanta la **80**, y al quitarle la máscara se cae — es la compartida;
* las tres celdas del DMA2 **tienen fuente**, preguntado a `celdas_con_fuente`.

Y en el lado del F407 la comprobación es la contraria y sigue pasando: las dos
ventanas sin decodificar, las tres celdas sin fuente (T26) y los bits 4 y 5 sin
existir (T02).

### 18.5 El estado, después

| | Antes | Después |
| :--- | ---: | ---: |
| Suite del F407 | 2069 | **2071**, en `2336217899213 ps` |
| Suite del F446 | 203 | 203, en `1033367277932 ps` |
| Banco del F417 | 124 | **137** |
| `make hash` / `make cryp` | 23 / 32 | 23 / 32 |
| `make vectores` | 54 | 54 |
| Capa de red | 13 | 13 |
| Placas | 5 validan | 5 validan |
| ASan + UBSan | limpios | **limpios en los tres** |

### 18.6 Qué queda para la fase 5

Nada de fontanería: tres juegos de `Periferia`, diez `McuCaps` y el catálogo de
diecinueve a veintinueve referencias. El descriptor de laboratorio del banco
desaparece entonces, porque habrá chips de verdad que hagan su trabajo.
