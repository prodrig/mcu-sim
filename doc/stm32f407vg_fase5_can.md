# Fase F5 (parte bxCAN) — Controller Area Network

Informe de implementación de la **parte de bxCAN** de la fase F5 del plan
`doc/smt32f407vg_diseño.md` (§7). Cierra la fase F5, tras SPI/I2S, I2C, ADC,
DAC, RTC/watchdogs, SDIO y CRC/RNG (`doc/stm32f407vg_fase5_spi.md`, `_i2c.md`,
`_adc.md`, `_dac.md`, `_rtc_wdog.md`, `_sdio.md`, `_crc_rng.md`).
Fuentes: `doc/informe_revisado.md` [IR] y `doc/informe_instrucciones.md` [II].

**Alcance de este entregable:** los dos bloques bxCAN del STM32F407VG
[IR, §12.12], con el requisito explícito de **analizar las similitudes y
diferencias de los distintos canales CAN** y, si las hay, de que el tipo se
seleccione por parámetros de plantilla C++ o del constructor.

**Resultado:** los dos bloques están completos y verificados. El modelo compila
sin avisos con `-Wall -Wextra -O2` y la suite pasa **1234 de 1234
comprobaciones** (124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S +
75 de I2C + 95 de ADC + 65 de DAC + 94 de RTC y perros guardianes + 83 de SDIO +
67 de CRC/RNG + **97 nuevas de bxCAN**, código de salida 0, en unos 6 s).
**Con esto queda cerrada la fase F5.**

El protocolo está modelado **a nivel de bit sobre los pines**, y el bus es un
**cable en Y** de verdad: los transceptores se conectan a un nodo analógico con
su terminador, y el estado dominante gana al recesivo **porque un cero de baja
impedancia gana a una resistencia de subida**. El arbitraje no se decide con un
`&&` en C++; se decide por superposición de conductancias.

---

## 1. Similitudes y diferencias de los dos canales CAN

El F407 lleva **dos** bxCAN, y ésta es la primera vez en toda la fase F5 que la
comparación pedida tiene dos instancias de verdad que comparar. La respuesta
corta: a primera vista son el mismo bloque dos veces, pero **no son
intercambiables**, y la diferencia no es de matiz.

### 1.1 En qué son idénticos

Todo lo que es protocolo y todo lo que es estructura interna:

| | CAN1 | CAN2 |
| :--- | :---: | :---: |
| Mapa de registros y desplazamientos | idéntico | idéntico |
| Buzones de transmisión | 3 | 3 |
| FIFOs de recepción | 2 | 2 |
| Marcos por FIFO | 3 | 3 |
| Protocolo | CAN 2.0A y 2.0B Active | igual |
| Temporizador de bit (`BTR`) | `BRP`/`TS1`/`TS2`/`SJW` | igual |
| Modos: bucle cerrado, silencioso, `NART`, `TXFP`, `RFLM`, `ABOM`, `TTCM` | todos | todos |
| Reloj | `PCLK1` (APB1) | `PCLK1` |

Si el bloque se juzgara solo por su protocolo, sobraría una sola clase sin
parámetros. Pero hay un recurso que **no se duplica**.

### 1.2 En qué se diferencian: los filtros son de CAN1

> **Los 28 bancos de filtros son un recurso ÚNICO del dispositivo, y viven
> físicamente en el espacio de CAN1** (0x4000 6600). En el de CAN2 esa ventana
> está **RESERVADA**: escribir allí no configura nada [IR, §12.12].

De ahí salen cuatro consecuencias, todas verificadas en el banco:

1. **CAN2 no tiene registros de filtro propios.** Ni `CAN_FMR`, ni `FM1R`, ni
   `FS1R`, ni `FFA1R`, ni `FA1R`, ni los 56 registros de banco. Se leen cero.
2. **El reparto lo decide `CAN2SB`**, un campo de `CAN_FMR` **de CAN1**: los
   bancos `0..CAN2SB-1` son de CAN1 y `CAN2SB..27` de CAN2. Al reset vale 14,
   mitad y mitad.
3. **Para usar CAN2 hay que encender el reloj de CAN1.** Ésta es la
   consecuencia práctica que más quebraderos de cabeza da en una placa real: sin
   `CAN1EN` no se pueden tocar los filtros de CAN2, y el bloque no recibe nada
   aunque el firmware de CAN2 sea impecable. El firmware de demostración
   enciende los dos relojes y lo dice en un comentario.
4. **Los vectores son distintos:** CAN1 usa 19 (TX), 20 (RX0), 21 (RX1) y 22
   (SCE); CAN2 usa 63, 64, 65 y 66. Y los pines: CAN1 en PA11/PA12, PB8/PB9 o
   PD0/PD1; CAN2 en PB5/PB6 o PB12/PB13, todos en AF9.

Esa asimetría es exactamente el tipo de diferencia que el proyecto modela con su
receta de familia.

---

## 2. El diseño flexible

Misma receta que en el resto del proyecto (USART/UART, los seis TIM, los cinco
SPI/I2S, los tres I2C, los tres ADC, los dos canales de DAC, el SDIO): una
`struct` de rasgos `constexpr`, una clase base que la recibe **por el
constructor** y una plantilla que la fija **en el tipo**.

### 2.1 Los rasgos

```cpp
struct CanCaps {
    bool     filter_master  = true;  // ¿tiene la ventana de filtros?
    unsigned filter_banks   = 28;    // bancos que administra
    bool     shared_filters = true;  // ¿los reparte con otro bxCAN (CAN2SB)?
    unsigned tx_mailboxes   = 3;     // buzones de transmisión
    unsigned rx_fifos       = 2;     // FIFOs de recepción
    unsigned fifo_depth     = 3;     // marcos por FIFO
    bool     ext_id         = true;  // CAN 2.0B activo
    bool     ttcm           = true;  // comunicación disparada por tiempo
    const char* kind        = "bxCAN";
};
```

`filter_master` **no es una comodidad del modelo**: es la diferencia física
entre los dos bloques del F407.

### 2.2 Las variantes

```cpp
inline constexpr CanCaps CAPS_CAN1_F407  = caps_can1_f407();
inline constexpr CanCaps CAPS_CAN2_F407  = caps_can2_f407();
inline constexpr CanCaps CAPS_CAN_SINGLE = caps_can_single();

using Can1      = BxCanT<CAPS_CAN1_F407>;   // maestro de 28 bancos
using Can2      = BxCanT<CAPS_CAN2_F407>;   // esclavo, sin ventana de filtros
using CanSingle = BxCanT<CAPS_CAN_SINGLE>;  // bxCAN único, 14 bancos
```

y, para elegir la variante **en ejecución**, el constructor:

```cpp
CanCaps c{};
c.tx_mailboxes = 1; c.rx_fifos = 1; c.fifo_depth = 2;
c.ext_id = false; c.ttcm = false;
c.filter_banks = 8; c.shared_filters = false;
BxCanBase raro{"raro", addr::CAN1_B, c};
```

`CAPS_CAN_SINGLE` describe un dispositivo con **un solo** bxCAN, dueño de todo
el banco y sin nadie con quien repartirlo (y por tanto sin `CAN2SB`); la
variante «a medida» del banco de pruebas no existe en ningún silicio y sirve
para comprobar que los ejes son **independientes entre sí**.

### 2.3 Cómo actúan los rasgos: máscaras de escritura

Como en el resto del proyecto, los rasgos **no se consultan con un `if` en la
lógica**: se aplican como **máscara de escritura de cada registro**, y la
ventana entera de filtros desaparece en el esclavo. Es lo que hace el silicio
con un bit —o un bloque de registros— reservado:

```
banco 0 visto desde CAN1 = 0xDEADBEEF, desde CAN2 = 0x00000000
CAN_FMR de reset = 0x2A1C0E01 -> CAN2SB = 14
variante a medida: TSR = 0x04000000, MCR = 0x00010002, FMR = 0x2A1C0001
```

En esa última línea se lee de un vistazo la variante entera: `TSR` sin `TME1`
ni `TME2` (un solo buzón), `MCR` sin `TTCM`, `FMR` sin `CAN2SB`.

---

## 3. El modelo

`src/periph/can.h`, ≈1150 líneas. Lo que sigue son las decisiones que no son
evidentes.

### 3.1 El bus es un cable en Y, no una señal

Un bus CAN **no es una señal**: es un nodo cableado en Y. La topología del
banco es la de una placa de verdad:

```
pin CAN_TX (push-pull) --> [transceptor] --> nodo del bus (cable en Y)
pin CAN_RX (entrada)   <-- [transceptor] <-- nodo del bus
```

El nodo del bus es **un `AnalogNet` más**, con su terminador haciendo de
resistencia de subida. Cada transceptor tira de él a cero (20 Ω) cuando su
entrada `TXD` está a cero, y lo suelta cuando está a uno. La resolución la hace
la superposición de conductancias del propio canal. De ahí salen, **sin una sola
línea de lógica de bus**:

* el estado recesivo en reposo (`3,30 V` medidos en la prueba);
* que **el dominante gane al recesivo**, que es la base del arbitraje;
* que el asentimiento de un receptor destruya el recesivo del emisor.

Un detalle que el modelo eléctrico obligó a poner y que es fiel al chip real:
**la entrada `TXD` del transceptor lleva pull-up**. No es un adorno. Sin él, un
`TXD` flotante —el MCU todavía sin configurar, o el pin en reset— deja el bus
atascado en dominante y **ningún nodo puede transmitir nunca**. Es exactamente
lo que pasó la primera vez que se ejecutó la prueba.

### 3.2 El protocolo, bit a bit

El motor de bit genera su propio tiempo de bit a partir de `BTR`:

> `t_q = (BRP+1)/PCLK1` ; `t_bit = (1 + TS1 + TS2) · t_q`

y en cada bit **pone su nivel en el flanco y muestrea en el punto de muestreo**.
Sobre eso se construye todo:

* **Relleno de bits**: tras cinco bits iguales seguidos se inserta uno del
  contrario, desde el bit de arranque hasta el último del CRC.
* **CRC15** (`x¹⁵+x¹⁴+x¹⁰+x⁸+x⁷+x⁴+x³+1`, o sea 0x4599) sobre los bits **sin
  relleno**.
* **Arbitraje**: quien manda recesivo y lee dominante en el campo de
  identificador **ha perdido la puja**. No es un error.
* **Asentimiento**: el emisor manda recesivo en la ranura de ACK y espera que
  alguien la ponga a dominante; un receptor con el CRC correcto lo hace.
* **Errores**: de bit, de relleno, de forma, de CRC y de asentimiento, cada uno
  con su código en `LEC`.
* **Recuento `TEC`/`REC`**, con `EWGF` (96), `EPVF` (128) y **bus-off** (255).

### 3.3 Un nodo CAN siempre se escucha a sí mismo

Ésta es la decisión estructural del modelo, y la que hizo que todo encajara:
**cada bit muestreado pasa por dos caminos a la vez** —el del transmisor, que
comprueba que el hilo lleva lo que él puso, y el del receptor, que va armando el
marco—. No es una comodidad: es cómo funciona un nodo CAN, y es de ahí de donde
salen el arbitraje y el asentimiento.

La recompensa se ve al perder una puja: el nodo que cede **no ha perdido el
principio del marco**, porque su receptor venía siguiendo el hilo bit a bit y
hasta ese momento lo que había en el hilo era idéntico a lo suyo. La prueba lo
comprueba explícitamente: tras perder el arbitraje, el MCU entrega el marco del
ganador **con sus datos intactos**, pese a haber empezado creyendo que
transmitía.

### 3.4 Los filtros, y su numeración

28 bancos, cada uno con dos registros de 32 bits y cuatro ejes de
configuración: activo o no (`FA1R`), escala de 32 o 16 bits (`FS1R`), modo de
lista de identificadores o de identificador y máscara (`FM1R`), y FIFO de
destino (`FFA1R`). Un banco puede contener 1, 2 o 4 filtros según la
combinación.

El detalle que se olvida: **`FMI`**, el índice del filtro que casó, que viaja en
`RDTxR` y es lo que permite al firmware saber **por qué** le ha llegado un
marco. La numeración es **por FIFO** y sigue el orden de los bancos; cada banco
aporta tantos números como filtros contenga. La prueba lo verifica con dos
filtros del mismo banco (`FMI = 0` y `FMI = 1`).

Y el reparto `CAN2SB` se comprueba **en funcionamiento**, no solo leyendo el
registro: moviendo la frontera a 1, el banco 1 deja de ser de CAN1 y sus marcos
dejan de llegar, mientras el banco 0 sigue filtrando.

### 3.5 Las protecciones del silicio

Dos, y las dos están modeladas porque las dos muerden en una placa real:

* **`BTR` solo se deja escribir en modo de inicialización.** Cambiar el tiempo
  de bit con el bloque en marcha desincronizaría a todo el bus.
* **Los bancos de filtro solo se dejan tocar con `FINIT`** (o con el banco
  desactivado). Si no, un marco a medio filtrar vería los registros cambiando
  bajo sus pies.

### 3.6 Integración

* **Reloj:** `PCLK1`, con `RCC_APB1ENR.CAN1EN`/`CAN2EN`; sin él, error de bus.
* **Pines:** AF9, con `CAN_TX` como salida push-pull normal y `CAN_RX` como
  entrada. La naturaleza de colector abierto del bus está **en el transceptor**,
  fuera del MCU.
* **Interrupciones:** cuatro vectores por bloque, cada uno con su parte de
  `CAN_IER`.
* **Congelación:** `DBG_CANx_STOP` para el depurador.

---

## 4. Verificación

Seis grupos, **97 comprobaciones**, todas autocomprobables.

### 4.1 T83 — En qué se diferencian CAN1 y CAN2

La parametrización, comprobada **desde el bus**: el banco 0 escrito por CAN1 se
lee en CAN1 y **cero** en CAN2; `CAN2SB` vale 14 al reset y se puede mover; y la
variante a medida enseña su firma en `TSR`, `MCR` y `FMR`. Cada eje se apaga por
separado sin arrastrar a los demás.

### 4.2 T84 — Registros, modos y tiempo de bit

Error de bus sin `CAN1EN`; valores de reset (el bloque **arranca dormido**, con
`SLEEP = 1`); la secuencia `INRQ`/`INAK`; la protección de `BTR`; la ley
`t_bit = (1 + TS1 + TS2)·(BRP+1)/PCLK1` medida sobre el modelo; y la mecánica de
los buzones, incluido que **escribir `RQCP` limpia el buzón entero**.

### 4.3 T85 — Un marco por el hilo

Contra el nodo CAN externo, que habla el protocolo de verdad:

```
hilo en reposo: 3.30 V (recesivo)
el nodo externo recibe id=0x123 dlc=8 datos DE AD ... BE
extendido: id=0x12345678 ide=1 dlc=3
RI0R = 0x64200000, DLC = 4, datos = 0x44332211
sin asentimiento: TSR = 0x1C000009, TEC = 0 -> 8, LEC = 3
```

Marco estándar de 8 bytes, marco extendido de 29 bits, el camino de vuelta con
su `FMP0`/`RFOM0`, y **el fallo más común al montar el primer nodo de un bus**:
sin nadie que asienta, `TERR0`, `LEC = 3` y el contador de errores subiendo de
ocho en ocho.

### 4.4 T86 — Los filtros

Máscara de 32 bits hacia la FIFO 0, lista de dos identificadores hacia la
FIFO 1, `FMI` correcto en cada caso, lo que no casa descartado **en el
hardware** (la CPU ni se entera), la FIFO de tres marcos con su `FULL0` y su
`FOVR0`, el orden de salida, y el reparto `CAN2SB` en funcionamiento.

### 4.5 T87 — Arbitraje, modo silencioso y bus-off

El arbitraje **de verdad**: los dos nodos ponen su bit de arranque en el mismo
instante y pujan. `0x700` contra `0x100`:

```
en plena puja: TSR = 0x1C000007, ALST0 = 1
```

El MCU pierde y marca `ALST0`, recibe el marco del ganador entero, y **reintenta
solo**: perder la puja no es un error y el contador `TEC` no sube. Después: el
modo silencioso (recibe pero **no asiente**, que es lo que permite pinchar un
analizador en un bus vivo), el bucle cerrado (el bloque se asiente a sí mismo,
sin nadie en el bus), y la escalada `EWGF` → `EPVF` → **bus-off** quitando el
nodo externo del hilo.

### 4.6 T88 — Firmware real con CMSIS

`verif/fw/can_demo/` está escrito contra la cabecera de dispositivo de ST y
**ejecutado por el Cortex-M4 del modelo**:

```
etapa = 5 | enviados = 1 | recibidos = 3 | id = 0x321 | dato = 0x00005AA0
ESR = 0x00000000 | el firmware calcula 500000 bit/s | el nodo externo vio 1 marcos
```

Sube el reloj a 168 MHz, calcula él mismo su velocidad de bit, **enciende los
dos relojes de CAN** (con el comentario que explica por qué), recorre la
secuencia `INRQ`/`BTR`/`FINIT`/marcha, transmite un marco y comprueba `TXOK0`, y
recibe por su filtro liberando cada marco con `RFOM0`.

---

## 5. Tres hallazgos que costaron encontrar

Los tres son fieles al hardware, y los tres los destapó el propio banco:

1. **El pull-up de `TXD` del transceptor.** Sin él, un pin de MCU sin
   configurar deja el bus atascado en dominante para todo el mundo. Los
   transceptores reales lo llevan exactamente por eso.
2. **El bit de relleno DESPUÉS del último bit del CRC.** Si los cinco últimos
   bits del CRC son iguales, el emisor inserta un bit de relleno más, todavía
   dentro de la zona con relleno. Sin tragárselo, el receptor muestrea el
   delimitador de CRC **un bit antes de tiempo** y todo el final del marco se
   desplaza. Es el clásico error de un decodificador de CAN escrito de memoria.
3. **Los once bits recesivos entre marcos.** Tras la ranura de asentimiento
   quedan el delimitador de ACK, los siete de fin de trama y los tres de
   intermisión. Empezar un bit antes es pisarle al emisor su último bit, y el
   emisor lo ve —correctamente— como un **error de bit**.

---

## 6. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/can.h` | **reescrito** | Los dos bxCAN, parametrizados, con el protocolo a nivel de bit (≈1150 líneas; antes era el esqueleto de F1) |
| `src/verif/ext_parts.h` | ampliado | `CanWire` (el hilo cableado en Y con su terminador), `CanTransceiver` y `CanNode` (un controlador CAN externo que asiente, transmite y compite en el arbitraje) |
| `src/top/stm32f407vg.h` | ampliado | `can1`/`can2` pasan a ser los tipos `Can1` y `Can2` |
| `src/top/stm32f407vg_bind2.h` | ampliado | La tabla AF9 de los seis pares de pines |
| `src/top/sc_main.cpp` | ampliado | Grupos T83-T88 (97 comprobaciones) |
| `src/verif/fw/can_demo/` | **nuevo** | Firmware con CMSIS (`main.c`, `Makefile`) |
| `src/README.md` | actualizado | **Fase F5 marcada como completada** |

---

## 7. Trabajo pendiente

Del propio bxCAN quedan fuera, anotados en el código, los caminos que ninguna
red CAN corriente usa y que no tienen contraparte que los ejercite: la
**comunicación disparada por tiempo** (`TTCM` con sus marcas de tiempo en
`TDTxR`), la **recuperación automática de bus-off** con su secuencia de 128×11
bits recesivos (el modelo la resuelve al instante cuando `ABOM` está puesto), y
el **despertar por actividad del bus** (`AWUM`). Sus bits existen, se guardan
según los rasgos de la variante y se leen correctamente.

**Con el bxCAN queda cerrada la fase F5.** Las siguientes del plan son la
**F6** (depuración: DAP/FPB/DWT/ITM, DBGMCU y trazas) y la **F7** (bajo consumo,
OTG/ETH/FSMC/DCMI y afinado a nivel de ciclo).
