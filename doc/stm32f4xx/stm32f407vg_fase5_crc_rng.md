# Fase F5 (parte CRC y RNG) — Unidad de cálculo CRC y generador de números aleatorios

Informe de implementación de la **parte de CRC y RNG** de la fase F5 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la fase
F4 y a las partes de SPI/I2S, I2C, ADC, DAC, RTC/watchdogs y SDIO de esta misma
fase (`doc/stm32f4xx/stm32f407vg_fase5_spi.md`, `_i2c.md`, `_adc.md`, `_dac.md`,
`_rtc_wdog.md`, `_sdio.md`).
Fuentes: `doc/stm32f4xx/informe_revisado.md` [IR] y `doc/stm32f4xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** la unidad de cálculo CRC [IR, §12.20] y el
generador de números aleatorios [IR, §12.19] del STM32F407VG.

**Resultado:** los dos bloques están completos y verificados. El modelo compila
sin avisos con `-Wall -Wextra -O2` y la suite pasa **1137 de 1137
comprobaciones** (124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S +
75 de I2C + 95 de ADC + 65 de DAC + 94 de RTC y perros guardianes + 83 de SDIO +
**67 nuevas de CRC y RNG**, código de salida 0, en unos 5 s).

Son los dos periféricos más pequeños del dispositivo y, precisamente por eso,
los dos en los que es más tentador hacer trampa. Aquí no se ha hecho:

* la unidad CRC **calcula el polinomio bit a bit**, no con una tabla ni con una
  llamada a una biblioteca. El resultado sale de la división polinómica, igual
  que del silicio;
* el RNG **no devuelve `rand()`**. Se modela lo que hay dentro —una fuente de
  ruido, un registro de desplazamiento realimentado y un contador de cosecha— y
  de ahí salen solas la cadencia, la bandera `DRDY` y, sobre todo, **las dos
  condiciones de error del bloque**, que son propiedades de la fuente y del
  reloj, no banderas puestas a mano.

---

## 1. Sobre la parametrización por rasgos

El resto del proyecto usa una receta de familia bien establecida —una `struct`
de rasgos `constexpr`, selección en ejecución por el constructor y en
compilación por plantilla— para USART/UART, los seis TIM, los cinco SPI/I2S, los
tres I2C, los tres ADC, los dos canales de DAC y el SDIO. **Aquí no se ha
aplicado**, y conviene decir por qué antes que nada.

De cada uno de estos dos bloques el F407 lleva **una sola instancia**, y ninguno
de los dos tiene ejes de configuración en este silicio:

* el polinomio del CRC está **cableado** (0x04C11DB7), el valor inicial también
  (0xFFFF FFFF), no hay inversión de bits ni de entrada ni de salida, y no hay
  longitud seleccionable;
* el RNG no tiene más ajuste que encenderlo: `RNG_CR` solo lleva `RNGEN` e `IE`.

Montar una `struct` de rasgos para describir un espacio con **un solo punto**
sería maquinaria sin contrapartida, y añadir variantes exigiría inventar mapas
de registros que no están en [IR] —la unidad CRC programable de familias
posteriores tiene registros `CRC_INIT` y `CRC_POL` que este dispositivo no
tiene—. Lo que sí se ha hecho es dejar **el polinomio y el valor inicial como
constantes con nombre en un solo sitio**, que es exactamente donde entraría el
eje si algún día hiciera falta:

```cpp
constexpr uint32_t CRC32_POLY = 0x04C11DB7u;
constexpr uint32_t CRC32_INIT = 0xFFFFFFFFu;
```

---

## 2. La unidad de cálculo CRC

### 2.1 Qué CRC es este, exactamente

Es la pregunta que más problemas causa en la práctica, así que va primero. El
bloque implementa el polinomio de Ethernet [IR, §12.20.1]:

> x³² + x²⁶ + x²³ + x²² + x¹⁶ + x¹² + x¹¹ + x¹⁰ + x⁸ + x⁷ + x⁵ + x⁴ + x² + x + 1

con valor inicial `0xFFFFFFFF`, **sin inversión de bits** de entrada ni de
salida y **sin XOR final**. Eso es el **CRC-32/MPEG-2**.

**No es el CRC-32 de zip ni el FCS de Ethernet**, aunque compartan polinomio:
esos dos invierten la entrada, invierten la salida y aplican un XOR final. Un
firmware que compare el resultado del periférico con el de `zlib` obtendrá
números distintos y concluirá, erróneamente, que el bloque está mal. La prueba
del banco lo dice literalmente:

```
[OK  ] el resultado es el CRC-32/MPEG-2, no el CRC-32 de zip
```

El modelo hace la división bit a bit, con el más significativo por delante:

```cpp
inline uint32_t crc32_word(uint32_t crc, uint32_t data) {
    crc ^= data;
    for (unsigned i = 0; i < 32; ++i)
        crc = (crc & 0x80000000u) ? uint32_t((crc << 1) ^ CRC32_POLY)
                                  : uint32_t(crc << 1);
    return crc;
}
```

### 2.2 Los tres registros, y lo que cada uno tiene de particular

| Registro | Particularidad |
| :--- | :--- |
| **`CRC_DR`** | Escribir **acumula** una palabra; leer entrega el resultado **parcial** y **no reinicia nada**. Su valor de reset es `0xFFFFFFFF`, no cero. |
| **`CRC_IDR`** | **Ocho bits** y ajeno al cálculo. Lo que sobra del acceso se pierde: escribir `0xDEADBEEF` deja `0xEF`. |
| **`CRC_CR`** | El bit `RESET` es de **solo escritura y se autoborra**: el registro entero se lee como cero. |

El detalle que más se pasa por alto —y que la prueba comprueba explícitamente—
es que **`CRC_CR.RESET` no toca `CRC_IDR`**. Para eso está ese registro: para
guardar un dato mientras se reinicia el cálculo. El reset **del sistema**, en
cambio, sí se lleva los dos por delante. Son dos resets distintos, y el banco
distingue uno de otro.

El camino de datos del bloque es de 32 bits: cada escritura consume una palabra
completa. Un acceso más estrecho no gobierna los bytes que no selecciona, y esos
entran como ceros; queda anotado en el código que el firmware debe escribir
palabras alineadas.

### 2.3 El coste, que es la razón de que el bloque exista

Un ciclo de HCLK por palabra [IR, §12.20.2]. Eso es lo que justifica gastar
silicio en algo que la CPU sabe hacer, y el modelo lo respeta con
`access_cycles()`. El firmware de demostración lo mide con el SysTick y lo
compara con la misma división escrita en C:

```
coste: 13926 ciclos el bloque, 618392 ciclos la rutina en C -> x44.4
```

Los 13 926 ciclos para 256 palabras no son del periférico —él consume uno por
palabra— sino del bucle de la CPU que se las va dando. Aun así, el factor
cuarenta y cuatro es el argumento.

---

## 3. El generador de números aleatorios

### 3.1 Lo que hay dentro, y por qué importa modelarlo

Un modelo que devolviera `rand()` en `RNG_DR` pasaría cualquier prueba de
registros y **no serviría para nada**: las dos condiciones de error del bloque
son propiedades físicas de sus piezas, y sin las piezas hay que inventarse las
banderas. Aquí se modelan las tres:

1. una **fuente de ruido** que entrega un bit por ciclo de RNGCLK;
2. un **registro de desplazamiento realimentado** que lo digiere;
3. un **contador de cosecha** que entrega una palabra cada 40 ciclos
   [IR, §12.19.1].

Con eso, los dos errores salen solos:

| Error | Qué lo provoca | Qué hace el bloque |
| :--- | :--- | :--- |
| **Semilla** (`SECS` / `SEIS`) | La fuente se atasca. El detector de salud la declara muerta al ver **64 bits consecutivos iguales**. | **Para la generación.** Hay que borrar `SEIS` y **apagar y volver a encender `RNGEN`** para rearmarlo. |
| **Reloj** (`CECS` / `CEIS`) | RNGCLK por debajo de **HCLK/16**. | **No para nada.** Avisa de que lo que salga no es de fiar, pero sigue entregando palabras. |

El banco **no pone esas banderas a mano**: atasca la fuente de ruido y deja que
el detector de salud la descubra, igual que rompe el CRC de la tarjeta SD para
provocar un `DCRCFAIL`.

```cpp
void force_noise(int nivel);   // -1 normal, 0 pegada a cero, 1 pegada a uno
```

### 3.2 `CECS` y `CEIS` no son lo mismo

`CECS` es un **estado**: sigue a la condición y se retira solo en cuanto el
reloj se recupera. `CEIS` es la **bandera con memoria**: se queda hasta que el
firmware la borra. Lo mismo con `SECS` y `SEIS`. Es una distinción que el modelo
respeta y que la prueba comprueba en los dos sentidos.

Y las dos banderas con memoria son **`rc_w0`**: se borran escribiendo **cero**,
no uno. Es al revés que casi todo el dispositivo —el `SR` del DAC, el `ICR` del
SDIO— y confundirlos deja la interrupción colgada para siempre:

```
[OK  ] SEIS NO se borra escribiendo unos...
[OK  ] ...sino CEROS: es rc_w0, al reves que el SR del DAC o el ICR del SDIO
```

### 3.3 De dónde cuelga el reloj

`RNGCLK` es el **PLL48CK**, la salida Q del PLL principal, **no HCLK**
[IR, §12.19]. La consecuencia práctica está comprobada: con el PLL parado, el
bloque encendido no genera nada. Y hay más —esto lo destapó la propia prueba—:
la **ausencia** de reloj es, por definición, un reloj por debajo de HCLK/16, así
que encender el RNG antes que su PLL **ya es un error de reloj**, y el bloque lo
denuncia. No es un fallo del modelo; es lo que haría el silicio, y la prueba lo
recoge como comportamiento esperado.

Por eso el bloque necesita **las dos frecuencias**: la vigilancia es una
comparación entre `PLL48CK` y `HCLK`, no una cuenta de flancos.

```cpp
rng.pll48ck(s_pll48); rng.pll48ck_hz(s_pll48_hz); rng.hclk_hz(s_hclk_hz);
```

### 3.4 Dirigido por sucesos, y reproducible

El generador **no se mueve flanco a flanco** de RNGCLK: calcula cuándo toca la
palabra siguiente y se cita una sola vez, que es la convención del proyecto para
los contadores. Así el bloque sigue funcionando cuando el banco apaga las ondas
de reloj (`set_internal_waveforms(false)`) para ejecutar firmware largo.

La fuente de ruido es **determinista y con semilla ajustable**. Una simulación
tiene que dar lo mismo dos veces o no vale como regresión. Lo que se le pide no
es que sea impredecible sino que tenga las **propiedades estadísticas** de la
fuente real, y eso el banco lo comprueba sobre mil palabras:

```
1000 palabras cosechadas: 49.7% de unos, 0 repeticiones seguidas
con la semilla 0xC0FFEE01: 2852B60F 64FF693B ... / 2852B60F 64FF693B ...
```

### 3.5 Integración

* **Reloj de bus:** bit 6 de `RCC_AHB2ENR`; sin él, error de bus.
* **Reloj de la fuente:** `PLL48CK`.
* **Interrupción:** vector **80** (`HASH_RNG`), compartida con el bloque HASH
  que este dispositivo no tiene. La levanta `DRDY`, `CEIS` **o** `SEIS` cuando
  `IE` está puesto: no solo el dato listo.

---

## 4. Verificación

Tres grupos, **67 comprobaciones**, todas autocomprobables.

### 4.1 T80 — La unidad CRC

Error de bus sin `CRCEN`, valores de reset, el valor canónico del MPEG-2 y una
tabla de vectores contra resultados calculados **fuera del modelo**. La
comprobación clave es doble: el banco lleva **su propia** división polinómica,
escrita aparte y recorriendo **bytes** en lugar de palabras, y comprueba que
coincide con el periférico. Si el modelo y la referencia coinciden **y además**
cuadran con el valor canónico publicado, no hay margen para un error común.

```
CRC de "12345678": por palabras 0x49E3C2FB, byte a byte 0x49E3C2FB
1024 palabras: CRC = 0x2E7030D0, referencia 0x2E7030D0, 128.00 us
```

También: que leer `CRC_DR` entrega el resultado parcial sin reiniciar nada; que
`CRC_IDR` guarda ocho bits y **sobrevive a `CRC_CR.RESET`** pero **no** al reset
del sistema; y que el bloque consume exactamente una palabra por escritura.

### 4.2 T81 — El RNG

La cadencia medida **promediando cien palabras** —el generador corre libre, así
que el hueco entre dos datos sueltos da la fase que quede, no el periodo—:

```
PLL48CK = 48 MHz -> 100 palabras a 835 ns cada una (esperado 833 ns)
```

Después: `DRDY` y su lectura destructiva; las propiedades estadísticas del
flujo; la reproducibilidad con la misma semilla y la divergencia con otra; la
IRQ 80; el error de semilla provocado **atascando la fuente**, con su parada y
su rearme por `RNGEN`; y el error de reloj provocado **bajando de verdad la
frecuencia del PLL**, con la distinción entre el estado que se retira solo y la
bandera que hay que borrar.

### 4.3 T82 — Firmware real con CMSIS

`verif/fw/crc_rng_demo/` está escrito contra la cabecera de dispositivo de ST,
compilado con `arm-none-eabi-gcc` y **ejecutado por el Cortex-M4 del modelo**.
Calcula el CRC de 256 palabras por los dos caminos y los compara, cronometra
ambos con el SysTick, usa `CRC_IDR` para lo que está, y cosecha 64 palabras del
RNG sondeando `DRDY` y mirando **los dos errores antes que el dato**, como haría
un driver de verdad:

```
CRC por hardware 0x573C35EC, por software 0x573C35EC ("123456789" = 0x0376E6E7)
coste: 13926 ciclos el bloque, 618392 ciclos la rutina en C -> x44.4
RNG: 64 palabras, 1003 bits a uno de 2048, 64 distintas
```

---

## 5. Un hallazgo colateral: `RCC_PLLCFGR` con el PLL en marcha

La prueba del error de reloj necesita **bajar** la frecuencia del PLL48CK y
luego **subirla** otra vez. Al hacerlo salió a la luz que el ayudante del banco
`pll48_on()` reprogramaba `RCC_PLLCFGR` sin parar antes el PLL, y la escritura
no surtía efecto —igual que en el silicio, donde ese registro solo se deja
escribir con el PLL parado [IR, §4.4]—. El modelo del RCC estaba bien; el
ayudante del banco, no. Corregido, y anotado en el propio ayudante.

---

## 6. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/crc_rng.h` | **reescrito** | La unidad CRC y el RNG completos (≈340 líneas; antes eran los esqueletos de F1) |
| `src/top/stm32f407vg_bind2.h` | ampliado | El RNG recibe también la **frecuencia** de PLL48CK, que es lo que su vigilancia de reloj compara con la de HCLK |
| `src/top/sc_main.cpp` | ampliado | Grupos T80-T82 (67 comprobaciones) y la corrección de `pll48_on()` |
| `src/verif/fw/crc_rng_demo/` | **nuevo** | Firmware con CMSIS (`main.c`, `Makefile`) |
| `src/README.md` | actualizado | Estado de la fase y recuento de comprobaciones |

---

## 7. Trabajo pendiente

De estos dos bloques **no queda nada fuera**: no tienen DMA propio, no tienen
más registros y no tienen más modos.

De la fase F5 queda por modelar un solo periférico: **bxCAN**.
