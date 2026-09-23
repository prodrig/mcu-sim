# Fase F5 (parte SDIO) — Interfaz para tarjetas SD, SD I/O y MultiMediaCard

Informe de implementación de la **parte de SDIO** de la fase F5 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la fase
F4 y a las partes de SPI/I2S, I2C, ADC, DAC y RTC/watchdogs de esta misma fase
(`doc/stm32f4xx/stm32f407vg_fase5_spi.md`, `_i2c.md`, `_adc.md`, `_dac.md`,
`_rtc_wdog.md`).
Fuentes: `doc/refs/stm32f407xx/informe_revisado.md` [IR] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** el bloque SDIO del STM32F407VG [IR, §12.17],
con el requisito explícito de **analizar las similitudes y diferencias de los
distintos canales SDIO** y, si las hay, de que el tipo se seleccione por
parámetros de plantilla C++ o del constructor.

**Resultado:** el bloque está completo y verificado. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **1070 de 1070 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S + 75 de I2C +
95 de ADC + 65 de DAC + 94 de RTC y perros guardianes + **83 nuevas de SDIO**,
código de salida 0, en unos 5 s).

La verificación **no se queda en los registros**. El banco lleva una **tarjeta
SD modelada a nivel de pin** (`SdCard` en `verif/ext_parts.h`), que habla el
protocolo de verdad: recibe tramas de 48 bits por `CMD`, comprueba su **CRC7**,
contesta con respuestas de 48 o 136 bits y mueve bloques de 512 bytes por
`D0-D3` con **CRC16 por cada línea**. Sobre ella se ejecuta un **firmware real
compilado con el CMSIS oficial de ARM y de ST** que arranca la tarjeta desde
cero, la lleva a cuatro hilos, lee un bloque y escribe otro, sin que el banco de
pruebas toque ni un registro del SDIO.

---

## 1. Similitudes y diferencias de los «canales» SDIO

El encargo pide comparar los distintos canales SDIO. Conviene decirlo de
entrada, porque la respuesta condiciona todo el diseño:

> **El STM32F407VG lleva UN SOLO bloque SDIO** [IR, §12.17.1]. No hay un
> SDIO1 y un SDIO2 que comparar, como sí había tres I2C, tres ADC o cinco
> SPI/I2S.

Ahora bien, el bloque **sí tiene ejes de variación reales**, y son de **dos
clases completamente distintas**. Ambas se han modelado, y las dos justifican
la parametrización.

### 1.1 Variación HACIA AFUERA: tres protocolos de tarjeta en un mismo bloque

El periférico no se llama «SD»: se llama **SDIO**, y la razón es que el mismo
silicio habla con **tres familias de tarjeta que no son el mismo protocolo**
[IR, §12.17.1]:

| Familia | Qué añade al bloque |
| :--- | :--- |
| **MultiMediaCard v4.2** | Bus de **8 hilos** (`WIDBUS = 10`) y **modo de flujo continuo** (`DCTRL.DTMODE = 1`), que transfiere sin bloques |
| **SD Memory Card v2.0** | Bus de **1 o 4 hilos**, transferencia siempre por bloques |
| **SD I/O v2.0** | `DCTRL.SDIOEN`, las funciones de lectura-espera `RWSTART`/`RWSTOP`/`RWMOD`, `CMD.SDIOSuspend` y **la interrupción `STA.SDIOIT`**, que la manda la propia tarjeta |
| **CE-ATA** | `CMD.ENCMDcompl` y `CMD.nIEN`, y la bandera `STA.CEATAEND` |

Estas cuatro columnas **no son un adorno de documentación**: son bits que
existen o no existen. Un derivado de la familia STM32 que solo lleve el
controlador de tarjeta SD tiene el mismo mapa de registros con esos bits
**reservados**, y un bit reservado **lee cero pase lo que pase**.

### 1.2 Variación HACIA ADENTRO: dos máquinas de estados que corren a la vez

Este es el eje que de verdad cuesta modelar, y el que más se parece a lo que el
encargo llama «canales distintos». Dentro del bloque hay **dos rutas
independientes que comparten reloj y pines pero corren solapadas**
[IR, §12.17.2]:

| | **CPSM** (Command Path State Machine) | **DPSM** (Data Path State Machine) |
| :--- | :--- | :--- |
| Hilo físico | `SDIO_CMD`, uno solo, bidireccional | `SDIO_D0`-`D7`, de 1 a 8, bidireccionales |
| Unidad | trama de **48 bits** (comando) / 48 o **136** (respuesta) | **bloque** de 2^`DBLOCKSIZE` bytes, o flujo continuo |
| Protección | **CRC7** sobre los 40 primeros bits | **CRC16 CCITT por cada línea**, no sobre el byte |
| Arranque | escribir `SDIO_CMD` con `CPSMEN = 1` | escribir `SDIO_DCTRL` con `DTEN = 1` |
| Temporizador | fijo, **64 ciclos** de `SDIO_CK` | programable, `SDIO_DTIMER` |
| Fin | `CMDSENT` / `CMDREND` | `DATAEND` / `DBCKEND` |
| Errores | `CTIMEOUT`, `CCRCFAIL` | `DTIMEOUT`, `DCRCFAIL`, `STBITERR`, `RXOVERR`, `TXUNDERR` |
| Tránsito | los registros `RESP1..RESP4` | la **FIFO de 32 palabras** |

Y **corren a la vez**. Esa es la parte que no se puede simplificar: en una
lectura de bloque, el firmware **arranca la DPSM ANTES de mandar el comando**,
porque la tarjeta empieza a soltar datos justo después de responder, y si la
máquina de datos no está ya esperando **se pierde el bit de arranque**. En una
escritura el orden es el contrario: primero el comando, y la DPSM arranca con la
FIFO ya cebada. Las dos secuencias están en el firmware de demostración, y las
dos son necesarias.

### 1.3 Lo que NO varía

Frente a eso, hay una parte que es idéntica en cualquier variante concebible del
bloque y que por tanto **no se ha parametrizado**:

* el **mapa de registros** y sus desplazamientos;
* el **generador de reloj**: `SDIO_CK = SDIOCLK / (CLKDIV + 2)`;
* el formato de la trama de comando y el de las dos respuestas;
* los polinomios de CRC: `x⁷+x³+1` para comandos, `x¹⁶+x¹²+x⁵+1` para datos;
* el **origen del reloj**: `SDIOCLK` es la salida **Q del PLL principal**
  (`PLL48CK`), no `PCLK2` [IR, §4.4]. `PCLK2` alimenta solo el interfaz APB2.

---

## 2. El diseño flexible

Se ha aplicado **la misma receta de familia que en el resto del proyecto**
(USART/UART, los seis TIM, los cinco SPI/I2S, los tres I2C, los tres ADC y los
dos canales de DAC): una `struct` de rasgos `constexpr`, una clase base que la
recibe **por el constructor** (selección en tiempo de ejecución) y una plantilla
que la fija **en el tipo** (selección en tiempo de compilación).

### 2.1 Los rasgos

```cpp
struct SdioCaps {
    unsigned max_bus_width = 8;      // WIDBUS: 1, 4 u 8 hilos
    unsigned fifo_words    = 32;     // profundidad de la FIFO
    double   max_ck_hz     = 48.0e6; // frecuencia máxima de SDIO_CK
    bool     clk_bypass    = true;   // CLKCR.BYPASS (SDIO_CK = SDIOCLK)
    bool     hw_flow_ctrl  = true;   // CLKCR.HWFC_EN
    bool     sdio_card     = true;   // SD I/O: DCTRL.SDIOEN/RWxxx y STA.SDIOIT
    bool     ceata         = true;   // CE-ATA: CMD.ENCMDcompl/nIEN, STA.CEATAEND
    bool     stream_mode   = true;   // DCTRL.DTMODE (flujo continuo de la MMC)
    bool     dma           = true;   // DCTRL.DMAEN y la petición
    const char* kind       = "SDIO";
};
```

Cada campo corresponde a **un eje real** de la §1.1, no a una comodidad del
modelo.

### 2.2 Las tres variantes previstas

```cpp
inline constexpr SdioCaps CAPS_SDIO_F407  = caps_sdio_f407();
inline constexpr SdioCaps CAPS_SDIO_SD4   = caps_sdio_sd4();
inline constexpr SdioCaps CAPS_SDIO_BASIC = caps_sdio_basic();

using Sdio      = SdioT<CAPS_SDIO_F407>;   // 8 hilos, SD I/O, CE-ATA, MMC
using SdioSd4   = SdioT<CAPS_SDIO_SD4>;    // solo SD de memoria, 4 hilos
using SdioBasic = SdioT<CAPS_SDIO_BASIC>;  // 1 hilo, FIFO de 16, sin DMA
```

y, para elegir la variante **en ejecución**, el constructor:

```cpp
SdioCaps c{};
c.max_bus_width = 1; c.fifo_words = 16; c.dma = false;
SdioBase raro{"raro", c};
```

`CAPS_SDIO_BASIC` **no existe en ningún silicio**: se ha incluido a propósito
para comprobar en el banco que los ejes son **independientes entre sí** y que
apagar uno no arrastra a los demás.

### 2.3 Cómo actúan los rasgos: máscaras de escritura

Los rasgos **no se consultan con un `if` en la lógica**. Se aplican como
**máscara de escritura de cada registro**, que es exactamente lo que hace el
silicio con un bit reservado: se escribe, no se guarda, y se lee cero.

```cpp
uint32_t clkcr_mask() const {
    uint32_t m = 0xFFu | (1u<<8) | (1u<<9) | (1u<<13);   // CLKDIV, CLKEN, PWRSAV, NEGEDGE
    if (caps_.clk_bypass)   m |= 1u << 10;
    if (caps_.hw_flow_ctrl) m |= 1u << 14;
    if (caps_.max_bus_width >= 4) m |= 1u << 11;         // WIDBUS bajo
    if (caps_.max_bus_width >= 8) m |= 1u << 12;         // WIDBUS alto
    return m;
}
```

Con lo cual la firma de cada variante es directamente observable escribiendo
unos y volviendo a leer. Es lo que hace el grupo T75:

```
CLKCR = 0x7FFF | CMD = 0x7FFF | DCTRL = 0x0FFF        (F407 completo)
variante en ejecucion (a medida): CLKCR = 0x23FF, DCTRL = 0x00F3, CMD = 0x07FF
```

Hay cuatro máscaras —`clkcr_mask`, `cmd_mask`, `dctrl_mask`, `sta_mask`— más
`icr_mask`, que decide qué banderas son borrables. Un `WIDBUS` que la instancia
no admite no llega al registro, de modo que `bus_width()` cae a un hilo por
construcción y no por una comprobación aparte.

---

## 3. El modelo

`src/periph/sdio.h`, ≈810 líneas. Lo que sigue son las decisiones que no son
evidentes.

### 3.1 Todo el protocolo, a nivel de bit sobre los pines

El bloque **no tiene atajos**. El host genera `SDIO_CK`, arma la trama de 48
bits del comando —bit de arranque, bit de dirección, índice de 6 bits, argumento
de 32, CRC7 y bit de parada—, la **saca bit a bit** por `SDIO_CMD` en los flancos
de bajada, suelta el hilo, y **muestrea la respuesta** en los de subida. Los
datos van igual por `D0-D7`, con su bit de arranque simultáneo en todas las
líneas, el reparto de los bits del byte entre las líneas activas, el **CRC16 por
línea** y el bit de parada.

Consecuencia directa: cuando la tarjeta devuelve un CRC malo, el modelo levanta
`CCRCFAIL` o `DCRCFAIL` **porque el cálculo no cuadra**, no porque alguien haya
puesto una bandera. Las pruebas de error del grupo T78 inyectan el fallo **en la
tarjeta**, que es donde ocurriría en la placa.

El reparto de un byte entre las líneas es el detalle que más se equivoca al leer
el manual: con cuatro hilos, el bit más significativo del byte va por `D3`, no
por `D0`, y cada línea lleva **su propio** CRC16 sobre **sus propios** bits.

### 3.2 Un solo hilo mueve las dos máquinas

```cpp
void ck_proc() {
    for (;;) {
        ...
        o_ck_ = false;  cpsm_falling(); dpsm_falling(); publish();   // todos ponen
        wait(h);
        o_ck_ = true;   publish();
        wait(h / 4);    cpsm_rising();  dpsm_rising();               // todos muestrean
        wait(3 * h / 4);
    }
}
```

Las dos máquinas se mueven en el **mismo** hilo porque comparten el reloj y
porque su solapamiento es parte del comportamiento: separarlas en dos procesos
habría metido un orden de evaluación arbitrario entre ellas.

El cuarto de periodo de espera tras el flanco de subida no es cosmético: da
tiempo a que **el nivel eléctrico llegue** por el pad y por el nodo analógico
antes de muestrearlo. Sin él, el host muestrearía el valor anterior del hilo.

### 3.3 El cerrojo de arranque (`c_armed_` / `d_armed_`)

El firmware escribe `SDIO_CMD` o `SDIO_DCTRL` **en cualquier instante** del ciclo
de `SDIO_CK`, porque la CPU va cuarenta veces más deprisa. Si la escritura cae
entre el flanco de bajada y el de subida, la máquina contaría como transmitido
un bit que **todavía no ha puesto en el hilo**, y se perdería el bit de arranque
de la trama. Un par de banderas de armado hacen que la máquina no empiece a
contar hasta el **siguiente** flanco de bajada completo. En el silicio esto lo
resuelve el sincronizador de entrada del bloque.

### 3.4 `SDIO_STA` con el bloque apagado

Con `POWER.PWRCTRL != 11` el adaptador **no tiene reloj**: ni la FIFO ni las
máquinas de estados dan señal, y `SDIO_STA` se lee como cero, que es su valor de
reset documentado [IR, §12.17.2]. Sin esto, un modelo que calcule las banderas de
FIFO siempre daría `TXFIFOE`, `RXFIFOE` y `TXFIFOHE` puestas nada más resetear,
que no es lo que devuelve el silicio.

### 3.5 Bajar `DTEN` al terminar

Al terminar una transferencia el hardware **baja `DCTRL.DTEN`**. No es un
detalle: el modelo arranca la DPSM en el **flanco** de `DTEN`, así que si la
bandera se quedara puesta, el firmware que escribe `DCTRL` para la transferencia
siguiente **no produciría flanco** y la máquina de datos se quedaría parada. Fue
justo el síntoma que apareció al encadenar `CMD17` y `CMD24` en la misma prueba.

### 3.6 La FIFO se sirve palabra a palabra

La FIFO de 32 palabras **no se llena ni se vacía de golpe** con el bloque
entero: la máquina de datos empuja una palabra cada 32 bits recibidos y saca una
palabra cada 32 bits transmitidos (`ensure_loaded()`). Solo así tienen sentido
`FIFOCNT`, las ocho banderas de estado, `RXOVERR`, `TXUNDERR` y la petición de
DMA — y solo así el firmware de la §4.5 se comporta como en la placa: **por
encima de unos pocos megahercios, sondear la FIFO desde la CPU no da abasto**.

### 3.7 Un único escritor de puertos

Como en el resto del proyecto: el estado lo actualizan tanto `ck_proc` como el
`b_transport` del banco de registros, que corre en el proceso del maestro. Las
salidas hacia los pines se guardan en miembros ordinarios (`o_ck_`, `o_cmd_`,
`o_d_[8]`, con sus `oe`) y **un solo** `pub_proc` las escribe. SystemC no admite
dos escritores sobre un `sc_signal`.

### 3.8 Integración

* **Reloj:** `sdioclk` viene del `PLL48CK` del RCC, no del APB2 [IR, §4.4]. La
  puerta de reloj de bus es `RCC_APB2ENR.SDIOEN`; sin ella el bloque **responde
  con error de bus**, como el resto de los periféricos del modelo.
* **Pines:** AF12 sobre `PC12` (CK), `PD2` (CMD), `PC8-PC11` (D0-D3),
  `PB8`/`PB9` (D4/D5) y `PC6`/`PC7` (D6/D7), con las tres señales
  `out`/`oe`/`in` de cada hilo bidireccional.
* **Interrupción:** vector **49**, con `SDIO_MASK` bit a bit.
* **DMA:** `DMA2`, **stream 3 canal 4** (y stream 6 canal 4) [IR, §12.17].

---

## 4. Verificación

Cinco grupos, **83 comprobaciones**, todas autocomprobables.

### 4.1 T75 — Un solo bloque y sus ejes de variación

Comprueba que la parametrización es real: la firma de máscaras del F407
completo, la de la variante `CAPS_SDIO_BASIC` fijada en el tipo, y la de una
variante **a medida construida en ejecución**. Verifica que cada eje se puede
apagar por separado —sin `WIDBUS`, sin `BYPASS`, sin control de flujo, sin DMA,
sin SD I/O, sin flujo continuo, sin CE-ATA— y que apagar uno no arrastra a los
otros.

### 4.2 T76 — Registros y generador de `SDIO_CK`

Error de bus sin `SDIOEN`, valores de reset, la ley
`SDIO_CK = SDIOCLK/(CLKDIV+2)` medida **sobre el pin** en cuatro puntos, el
camino de `BYPASS`, `WIDBUS`, las ocho banderas de la FIFO con sus 32 palabras
en orden, y el `CTIMEOUT` de **64 ciclos** con el zócalo vacío.

### 4.3 T77 — Arranque de una tarjeta SD por los pines

La secuencia de identificación completa contra la tarjeta modelada:
`CMD0`, `CMD8`, `ACMD41` hasta que deja de estar ocupada, `CMD2` (CID),
`CMD3` (RCA), `CMD9` (CSD), `CMD7`, `CMD16` y `ACMD6`. Después, una lectura y
una escritura de bloque de 512 bytes **por las cuatro líneas**:

```
la tarjeta atendio 11 comandos; el ultimo fue el CMD6
CID = 02544D53 41303447 00112233 44012A00
leidos 512 bytes: 40 41 42 ... 7F (STA = 0x001C4540)
escritos: la tarjeta guarda 44 33 22 11 ... C3 33 22 11
```

Se comprueba, entre otras cosas, que `ACMD6` deja el bus a cuatro hilos **en los
dos extremos**, que una respuesta larga devuelve `RESPCMD = 0x3F` porque no
lleva índice, y que la escritura llega a la tarjeta **byte a byte, incluida la
última palabra del bloque**.

### 4.4 T78 — Errores, interrupción y DMA

Los fallos se inyectan **en la tarjeta**, no en el host:

* CRC7 de respuesta roto → `CCRCFAIL`, y la respuesta **no** se da por buena;
* tarjeta muda → `CTIMEOUT`, y el comando siguiente vuelve a ir bien;
* CRC16 de datos roto → `DCRCFAIL`;
* máquina de datos arrancada sin comando → `DTIMEOUT` al agotar `DTIMER`;
* `CMDREND` desenmascarado → **IRQ 49**, y borrarlo la retira;
* un bloque de 512 bytes recogido entero por **DMA2 stream 3 canal 4**, sin que
  la CPU toque la FIFO.

### 4.5 T79 — Firmware real con CMSIS

`verif/fw/sdio_demo/` es un driver de tarjeta SD escrito contra la cabecera de
dispositivo de ST, compilado con `arm-none-eabi-gcc` y **ejecutado por el
Cortex-M4 del modelo**. Sube el reloj a 168 MHz con `Q = 7` para tener
`PLL48CK`, configura los seis pines a AF12 con pull-up, arranca por debajo de
400 kHz como manda la norma, recorre las doce etapas de la identificación, pasa
a cuatro hilos, lee un bloque y escribe otro:

```
etapa = 12 | RCA = 0x0002 | CID0 = 02544D53 | SDIO_CK = 4000000 Hz
leidos 512 bytes (suma 78592, esperada 78592), escritos 512 | STA = 0x001C4540
la tarjeta atendio 45 comandos y movio 4+2 bloques
```

Dos cosas que el firmware puso de manifiesto y que **no eran fallos del modelo
sino comportamiento correcto**, y que están comentadas en el propio `main.c`:

1. **NCC.** Entre el final de un comando y el principio del siguiente la norma
   SD exige **8 ciclos de `SDIO_CK`**. La primera versión encadenaba comandos sin
   esperar, y el modelo eléctrico avisó de **sobrecorriente en `PD2`**: el host
   empezaba a gobernar `CMD` mientras la tarjeta todavía la soltaba. Es
   exactamente la pelea de etapas de salida que ocurriría en la placa, y la
   detectó el modelo de pines, no una comprobación puesta a mano.
2. **La FIFO sondeada tiene un techo.** A 12 MHz por cuatro hilos, la CPU no
   llega a servir la FIFO a tiempo y aparecen `RXOVERR` en la lectura y
   `TXUNDERR` en la escritura. Es el motivo real por el que los drivers de
   verdad usan DMA por encima de unos pocos megahercios. El firmware trabaja por
   eso a `CLKDIV = 10` (4 MHz) y lo dice en el comentario; el camino de DMA a
   pleno ritmo queda demostrado en T78.

---

## 5. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/sdio.h` | **reescrito** | El bloque SDIO completo y parametrizado, CPSM y DPSM a nivel de bit (≈810 líneas; antes era el esqueleto de F1) |
| `src/verif/ext_parts.h` | ampliado | `SdCard`: tarjeta SD a nivel de pin, con CRC7, CRC16 por línea, los comandos del arranque y la transferencia, e **inyección de fallos** |
| `src/top/stm32f407vg_bind2.h` | ampliado | `SDIOCLK` desde `PLL48CK` y la tabla AF12 de los nueve pines |
| `src/top/sc_main.cpp` | ampliado | Grupos T75-T79 (83 comprobaciones) |
| `src/verif/fw/sdio_demo/` | **nuevo** | Firmware de tarjeta SD con CMSIS (`main.c`, `Makefile`) |
| `src/README.md` | actualizado | Estado de la fase y recuento de comprobaciones |

---

## 6. Trabajo pendiente

Del propio SDIO quedan fuera, anotados en el código, los caminos que **ninguna
tarjeta SD de memoria usa** y que no tienen contraparte que los ejercite en el
banco: el **flujo continuo de la MMC** (`DTMODE = 1`), las funciones de
**lectura-espera de SD I/O** (`RWSTART`/`RWSTOP`/`RWMOD` y `CMD.SDIOSuspend`) y
las de **CE-ATA**. Sus bits existen, se guardan o no según los rasgos de la
variante y se leen correctamente; lo que no hay es una máquina que los ejecute.
Modelarlos exigiría antes una tarjeta SD I/O y una MMC en `ext_parts.h`.

De la fase F5 quedan por modelar, en el orden previsto por el plan: **bxCAN** y
**CRC/RNG**.
