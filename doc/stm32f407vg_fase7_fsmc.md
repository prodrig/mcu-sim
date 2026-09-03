# Fase F7 (parte de FSMC) — El bus externo, y los dieciséis hilos que no salen del encapsulado

Modelo del *Flexible Static Memory Controller* del STM32F407VG en SystemC 2.3,
sobre el informe técnico `doc/informe_revisado.md` [IR, §12.18] y la tabla de
pines del capítulo 2. El código está en `src/periph/fsmc.h`; la circuitería
externa que se le suelda, en `src/verif/ext_parts.h`; las pruebas, en los
grupos T109–T112 de `src/top/sc_main.cpp`.

El FSMC es el único periférico del chip cuyo trabajo **no** es un protocolo. No
compone tramas, no negocia arbitrajes y no tiene máquina de estados de enlace.
Lo que tiene son TIEMPOS: convierte un acceso del bus AHB en un ciclo de bus
externo —baja un chip select, pone una dirección, espera los ciclos que le
hayan programado, muestrea dieciséis hilos y lo sube todo otra vez— y el
firmware paga en ciclos de HCLK lo que la memoria de fuera tarde en contestar.

```
      ADDSET        DATAST                BUSTURN
    ├────────┤├──────────────────┤├──────────────┤
NEx  ‾‾\____________________________________/‾‾‾‾‾
A    ──<        direccion valida           >─────
NOE  ‾‾‾‾‾‾‾‾‾‾\____________________/‾‾‾‾‾‾‾‾‾‾‾‾‾
D    ────────────────────< dato >────────────────    (lectura)
```

---

## 1. El análisis pedido: ¿en qué se diferencian los "canales" del FSMC?

Aquí la respuesta es la contraria a la del DCMI. En el DCMI había un solo
canal y las diferencias estaban en los pines. En el FSMC hay **cuatro bancos, y
son distintos de verdad**: no comparten ni el mapa de registros.

### 1.1 Los cuatro bancos

| | Base | Memoria | Chip selects | Registros propios | Rasgos exclusivos |
|---|---|---|---|---|---|
| **Banco 1** | `0x6000_0000` | SRAM / PSRAM / NOR | **4** (NE1–NE4) | BCRx, BTRx, **BWTRx** | multiplexado, ráfaga síncrona, modo extendido, NBL0/1, FACCEN |
| **Banco 2** | `0x7000_0000` | NAND | 1 (NCE2) | PCR2, SR2, PMEM2, PATT2, **ECCR2** | **ECC por hardware**, 2 espacios (común / atributos) |
| **Banco 3** | `0x8000_0000` | NAND | 1 (NCE3) | PCR3, SR3, PMEM3, PATT3, **ECCR3** | idem banco 2 |
| **Banco 4** | `0x9000_0000` | PC Card / CF | 1 (NCE4) | PCR4, SR4, PMEM4, PATT4, **PIO4** | **tercer espacio de E/S**; **SIN ECC** |

Las diferencias no son de matiz:

- El banco 1 tiene `BTR`/`BWTR` (ADDSET, ADDHLD, DATAST, BUSTURN, CLKDIV,
  DATLAT, ACCMOD) y **no tiene** `PMEM`/`PATT`.
- Los bancos 2 y 3 tienen `ECCR` y **no tienen** `BTR`: sus tiempos se expresan
  en SETUP/WAIT/HOLD/HIZ, un byte cada uno, y por partida doble (espacio común
  y espacio de atributos).
- El banco 4 tiene `PIO4` —un tercer juego de tiempos para el espacio de E/S,
  que es lo que distingue a una PC Card de una NAND— y **no tiene** ECC.
- Solo el banco 1 sabe multiplexar, hacer ráfagas síncronas y usar tiempos de
  escritura independientes de los de lectura.
- Solo el banco 1 tiene carriles de byte (NBL0/NBL1); una NAND es siempre de
  ocho bits (o dieciséis, pero sin carriles).

Y hay un eje **transversal** que no es de banco sino de bus: el ancho (8 o 16
bits), que en el banco 1 sale de `BCR.MWID` y en los NAND de `PCR.PWID`.

### 1.2 Y en este encapsulado faltan dieciséis hilos de dirección

La tabla de pines del informe [IR, cap. 2] es tajante. De todo el bus externo,
al LQFP100 salen:

- **D0–D15**, los dieciséis hilos de datos (PD14, PD15, PD0, PD1, PE7–PE15,
  PD8, PD9, PD10);
- **A16–A23**, ocho hilos de dirección (PD11, PD12, PD13, PE3, PE4, PE5, PE6,
  PE2);
- NOE (PD4), NWE (PD5), NWAIT (PD6), NBL0 (PE0), NBL1 (PE1), NL/NADV (PB7);
- y **un solo chip select**: PD7, que es a la vez FSMC_NE1 y FSMC_NCE2.

No aparece **ni un solo A0–A15** —viven en PF0–PF15— ni NE2/NE3/NE4 —PG9,
PG10, PG12— ni nada del banco 4. Los puertos F y G no existen en este chip.

La consecuencia es fuerte, y es el hallazgo de esta subfase:

> **En el STM32F407VG el bus externo solo se puede usar MULTIPLEXADO.**
> Sin A0–A15 no hay forma de decirle a la memoria qué palabra se quiere dentro
> de un bloque de 64 K, salvo sacando la dirección baja por los propios hilos
> de datos y enganchándola con NL. Un firmware que copie un ejemplo de una
> placa con encapsulado grande y ponga `MUXEN = 0` verá cómo `0x6000_0100` y
> `0x6000_0200` son **la misma celda**.

No es casualidad que el valor de reset de `BCR1` traiga `MUXEN = 1`: es la
única configuración que este encapsulado admite. Y que PD7 sirva a la vez de
NE1 y de NCE2 significa que en una placa con F407VG **una SRAM y una NAND no
pueden convivir en el bus**: comparten el pad, y el que gana es el que esté
soldado.

### 1.3 La misma diferencia entre miembros de la familia

Los derivados sin NAND traen el bloque con el banco 1 solo; los encapsulados
de 144 y 176 patillas sacan A0–A25 y los cuatro NE. El modelo tiene que poder
representar las tres cosas sin duplicar el fichero.

---

## 2. Cómo se elige el tipo de FSMC

Misma receta del proyecto (`UsartCaps`, `TimCaps`, `SpiCaps`, `I2cCaps`,
`AdcCaps`, `DacCaps`, `SdioCaps`, `CanCaps`, `DcmiCaps`), con un nivel más de
anidamiento porque aquí los rasgos son **por banco**:

```cpp
struct FsmcBankCaps {           // lo que sabe hacer UN banco
    FsmcKind kind;              // Ninguno / NorPsram / Nand / PcCard
    unsigned chip_sel;          // cuántos NE gobierna (4 en el banco 1)
    bool mux, sync, extmod, nor_flash, ecc, io_space, wait;
    unsigned max_width;         // 8 o 16
    const char* nombre;
};
struct FsmcCaps {               // el periférico y sus PINES
    FsmcBankCaps banco[4];
    unsigned lineas_addr;       // cuántas A hay soldadas
    unsigned addr_base;         // la primera de ellas (16 en el LQFP100)
    unsigned lineas_dato;
    unsigned chip_sel_pin;      // chip selects con pin de verdad
    const char* kind;
};
```

Y las dos formas de elegir, como en el resto del proyecto:

```cpp
// compilación: coste cero, los rasgos son constantes
template <const FsmcCaps& C> class FsmcT : public FsmcBase { ... };
using Fsmc    = FsmcT<CAPS_FSMC_F407>;   // 1 NOR/PSRAM + 2 NAND + PC Card
using FsmcNor = FsmcT<CAPS_FSMC_NOR>;    // solo estáticas, 26 A, 4 NE
using FsmcMin = FsmcT<CAPS_FSMC_MIN>;    // una SRAM de 8 bits y nada más

// ejecución: el mismo modelo con rasgos por el constructor
FsmcBase f{"fsmc", FsmcCaps{...}};
```

**Los rasgos se aplican como máscara de escritura.** Un bit que el banco no
implementa lee cero exactamente igual que un bit reservado del silicio, y un
registro que no existe se lee cero entero. Así, `MUXEN` no se guarda en la
variante mínima, `BWTR1` se lee cero sin modo extendido, `PCR2` se lee cero sin
banco NAND y `MWID` se queda en 8 bits si el banco no tiene dieciséis hilos.

Un detalle que hubo que modelar aparte: el bit 7 de `BCR` es **reservado pero
vale uno** (el reset documentado es `0x30DB` para el banco 1 y `0x30D2` para
los otros). Ponerlo en la máscara sería mentir —se podría borrar—, así que va
en un `bcr_fijo()` que se re-inyecta en cada escritura.

---

## 3. Qué se ha modelado

**El bus externo, en los pines.** El controlador tiene 16 hilos de datos
bidireccionales (`d_out`/`d_oe`/`d_in`), 8 de dirección, 4 chip selects, NOE,
NWE, NL, NBL0/1 y NWAIT de entrada, todos por AF12 del pinmux, con la
electrónica de pad del proyecto (tensión y corriente en `float`, alta
impedancia real cuando el controlador suelta el bus).

**El ciclo asíncrono completo**, fase por fase: chip select y dirección alta →
fase de dirección multiplexada con NL → fase de datos con NOE o NWE →
mantenimiento del dato → vuelta del bus (BUSTURN). Cada fase dura los ciclos de
HCLK que digan `BTR`/`BWTR`, y ese tiempo **se consume de verdad** en la
simulación: el maestro AHB se queda esperando.

**El ancho y los carriles.** Un acceso de 32 bits sobre un bus de 16 son dos
ciclos externos; sobre uno de 8, cuatro. Y escribir un byte suelto en una
memoria de 16 bits baja solo NBL0 o NBL1, de forma que el vecino no se toca.

**La dirección que sale por los hilos no es la de byte.** Con bus de 16 bits el
FSMC saca `HADDR[25:1]` por `A[24:0]`: A0 no existe, porque cada dirección
externa vale dos bytes y son NBL0/NBL1 los que eligen cuál. Con bus de 8 bits
sale la dirección de byte tal cual.

**NWAIT**, que es lo único del ciclo que no decide el controlador: con
`WAITEN = 1` la memoria puede pedir más tiempo, y el modelo lo cuenta.

**Los bancos NAND**, que no tienen bus de direcciones: tienen un puerto de ocho
bits por el que van mandatos, direcciones y datos, y dos señales —CLE y ALE—
que dicen cuál de las tres cosas es. El FSMC las saca por A16 y A17, de modo
que **escribir en una dirección u otra del banco es lo que elige el tipo de
ciclo**: `0x7001_0000` es un mandato, `0x7002_0000` una dirección y
`0x7000_0000` un dato.

**El ECC por hardware**, que es la única aritmética que hace este periférico y
la hace sin coste para el firmware, mientras los datos pasan: paridad de
columna (qué bit de los ocho) y de línea (en qué byte), acumuladas sobre la
marcha y legibles en `ECCR`. Encender `ECCEN` reinicia el acumulador, porque
arrastrar la página anterior no serviría de nada.

**La interrupción 48**, con los tres flags de `SR` (rising edge, level, falling
edge) y sus tres habilitaciones.

### 3.1 Lo que se le suelda al bus

En `verif/ext_parts.h` se han añadido dos componentes que se comportan como el
integrado real, no como un modelo de conveniencia:

- **`ExtSram`** — SRAM asíncrona de 64 K. No tiene reloj y no negocia nada:
  si NE y NOE están a cero conduce el dato de la dirección que le hayan puesto;
  si NE está a cero y NWE **sube**, guarda lo que hubiera en los hilos, byte a
  byte según NBL0/NBL1; y en modo multiplexado coge la parte baja de la
  dirección de los propios hilos de datos en el flanco de subida de NL. Puede
  pedir tiempo por NWAIT.
- **`ExtNand`** — NAND de 16 páginas de 512 bytes que entiende los mandatos
  `0x90` (leer ID), `0x00…0x30` (leer página), `0x80…0x10` (programar) y `0x70`
  (estado), con la semántica correcta de programación: **solo baja bits**.

### 3.2 Una decisión de placa que hubo que tomar

El bus externo comparte pines con medio chip: PD0/PD1 son CAN1, PB7 es I2C1,
PE0–PE15 y PD8–PD15 se los reparten temporizadores y puertos serie. Como en la
subfase del DCMI, las pruebas del FSMC **sueltan explícitamente** lo que
estorba (`can_links(false)`, `i2c_bus(false)`, `cam->soltar()`) antes de soldar
la memoria, y lo devuelven al terminar. Es una decisión de placa, y se toma a
la vista, no escondida dentro del modelo.

Y hay una segunda: **la SRAM y la NAND no pueden estar conectadas a la vez**,
porque comparten PD7. `fsmc_placa()` lo impone.

---

## 4. Cinco cosas que costaron trabajo

**1. Un solo escritor por señal.** Un ciclo de bus lo puede disparar cualquier
maestro de la matriz —el núcleo, el DMA, el depurador— y SystemC no admite que
dos procesos conduzcan la misma `sc_signal`. La solución es la que hay en el
silicio: el controlador es uno solo. El `b_transport` **no toca los pines**;
deja la petición, despierta a un `SC_THREAD` propio (`bus_proc`) y se bloquea
en un evento. El tiempo del acceso se consume dentro de ese hilo, no se anota
en el `sc_time` del transporte.

**2. `BWTR1` no está donde parece.** Los `BWTR` viven en `0x104`, `0x10C`,
`0x114` y `0x11C`, de ocho en ocho a partir de `0x104`. El decodificador
comprobaba `(off & 4) == 0`, que es falso para `0x104`, así que las escrituras
a `BWTR1` se perdían en silencio y el modo extendido usaba el valor de reset:
una escritura de 578 ciclos de HCLK donde debía costar 18. Se ve enseguida
midiendo; no se ve nunca leyendo el registro, porque el `leer_reg` tenía el
mismo error y devolvía coherentemente lo mismo que no había escrito.

**3. El dato tiene que sobrevivir al flanco.** Una SRAM asíncrona guarda lo que
hay en los hilos **después** de que NWE suba. El modelo soltaba los datos en el
mismo instante en que subía NWE; entre eso y el retardo de propagación del pad,
la memoria veía el bus ya liberado y no guardaba nada. Ahora el controlador
mantiene el dato un ciclo más —el t_DH que exige cualquier hoja de
características— y ese ciclo se cuenta en el coste del acceso.

**4. El chip select del banco NAND no tiene pad propio.** PD7 es a la vez
FSMC_NE1 y FSMC_NCE2: es el MISMO pad, y el controlador saca por él uno u otro
según a qué banco vaya el acceso. Modelarlo como dos señales distintas dejaba a
la NAND sin seleccionar nunca. Lo hace ahora `ne_nand()`, que además deja
NCE3 y NCE4 dentro del chip, porque PG9 y PG12 no existen aquí.

**5. Un ECC de cero no es un ECC roto.** La primera versión de la prueba exigía
`ECCR != 0` sobre una página de treinta y dos bytes `0xC0..0xDF`. Ese contenido
tiene tantos unos como ceros en cada columna y en cada mitad, de modo que
**todas** las paridades se cancelan y el ECC correcto es exactamente cero. Un
Hamming no promete un valor distinto de cero: promete ser una función de los
datos. La prueba comprueba ahora eso —el mismo contenido da el mismo ECC, y un
bit distinto lo cambia—, que es la propiedad que de verdad importa.

---

## 5. Verificación: 54 comprobaciones

### 5.1 T109 — Los cuatro bancos y sus rasgos (18)

Valores de reset (`BCR1 = 0x30DB` con el bit 7 incluido; los otros tres
subbancos deshabilitados, porque cuatro chip selects a la vez en el mismo bus
serían un cortocircuito); existencia y ausencia de registro por banco (`PCR2`
sí, `ECCR4` no, `PIO4` solo en el 4); un banco NAND no contesta al bus con
`PBKEN = 0` y sí en cuanto se enciende. Y la variante `CAPS_FSMC_MIN`
**instanciada en tiempo de ejecución** sobre su propio maestro de bus: sin
`MUXEN`, sin ráfaga, sin modo extendido, `MWID` clavado en 8 bits, `BWTR1`
leyendo cero entero y `PCR2` inexistente. Los ejes mux / ráfaga / extendido /
ECC / E-S resultan ser independientes entre sí.

### 5.2 T110 — El ciclo de bus sobre una SRAM soldada (16)

Una escritura de 32 bits llega a la SRAM real como **dos** ciclos de 16, cada
mitad en su celda; la lectura devuelve exactamente lo que hay en la memoria; la
dirección la engancha la SRAM del bus de datos con NL. Una escritura de un byte
llega y **no** se lleva por delante al vecino. Alargar `DATAST` de 3 a 15
alarga el acceso en 12 ciclos de HCLK por cada uno de los dos accesos —24 en
total, medidos— y el bus AHB se queda esperando ese tiempo de verdad. Con
`EXTMOD` la escritura usa `BWTR` y sale más rápida que la lectura (18 ciclos
frente a 42) sin dejar de escribir bien. Con `WREN = 0` el banco es de solo
lectura y a la SRAM no le llega nada. Con bus de 8 bits, una palabra de 32 son
cuatro ciclos externos.

### 5.3 T111 — Lo que este encapsulado NO tiene (9)

Ocho hilos de dirección, empezando en A16, y un solo chip select. Multiplexado,
dos direcciones distintas dan dos datos distintos. **Sin multiplexar, escribir
en `0x100` y leer en `0x200` devuelve lo mismo**: los dieciséis hilos de abajo
no existen y todo el bloque de 64 K es un alias. Y un acceso al subbanco 2, con
`BCR2` habilitado: el controlador hace su ciclo y contesta OK —porque el
controlador siempre contesta—, pero la SRAM no se entera, porque NE2 no tiene
pin.

### 5.4 T112 — La NAND y el ECC (11)

Mandato de identificación por CLE, dirección por ALE y cuatro datos de vuelta:
fabricante `0x20`, dispositivo `0x33`. Programación de treinta y dos bytes y
lectura de vuelta sin un error. Encender `ECCEN` pone el acumulador a cero;
leer la misma página da el mismo ECC; cambiar un solo bit lo cambia, y el
síndrome dice **qué bit** de los ocho (paridad de columna, `0x08`) y **en qué
byte** (paridad de línea, `7`): con las dos, el bit se corrige.

### 5.5 La suite entera

```
Resumen F7 (bajo consumo): 116 comprobaciones OK, 0 fallos
Resumen F7 (DCMI)        :  61 comprobaciones OK, 0 fallos
Resumen F7 (FSMC)        :  54 comprobaciones OK, 0 fallos
TOTAL                    : 1616 comprobaciones OK, 0 fallos
```

---

## 6. Limitaciones, dichas claramente

- **La ráfaga síncrona no mueve los pines.** `BURSTEN`, `CLKDIV` y `DATLAT` se
  guardan y se usan para calcular el coste del acceso, pero el modelo no saca
  el reloj FSMC_CLK por PD3 ni encadena los datos de la ráfaga en los hilos.
  Los accesos síncronos son, en los pines, accesos asíncronos con el tiempo de
  la ráfaga. Con el LQFP100 esto es poco menos que académico: sin A0–A15 no hay
  NOR síncrona que valga.
- **El banco 4 (PC Card) tiene registros pero no pines.** `PCR4`, `SR4`,
  `PMEM4`, `PATT4` y `PIO4` se comportan como debe; los ciclos de bus del
  espacio de E/S no se sacan, porque en este encapsulado no hay por dónde.
- **La FIFO de escritura se modela vacía siempre** (`SR.FEMPT = 1`): las
  escrituras se completan dentro del acceso, así que no hay nada que vaciar.
  Un firmware que espere a `FEMPT` no se bloqueará, pero tampoco medirá la
  latencia real de una FIFO.
- **El ECC es el de 256 bytes.** `ECCPS` se guarda, pero el acumulador no
  cambia de tamaño de bloque.
- La **PSRAM síncrona con NWAIT durante la ráfaga** y el modo `WRAPMOD` no se
  modelan más allá de sus bits.

---

## 7. Estado

Subfase F7-FSMC **completada**. El controlador mueve pines de verdad contra
memoria de verdad, los tiempos del registro se pagan en HCLK, los cuatro bancos
tienen los registros que les tocan y ni uno más, y el modelo dice a las claras
lo que este encapsulado concreto puede y no puede hacer con su bus externo.

Queda pendiente de la fase F7: OTG y ETH.
