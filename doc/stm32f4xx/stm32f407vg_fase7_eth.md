# Fase F7 (parte de ETH) — El Ethernet, y las tres cosas que se llaman "canal"

Modelo del controlador Ethernet MAC 10/100 con DMA propio del STM32F407VG en
SystemC 2.3, sobre `doc/refs/stm32f407xx/informe_revisado.md` [IR, §12.16] y la tabla de pines
del capítulo 2. El código está en `src/periph/eth_mac.h`; el PHY que se le
suelda, en `src/verif/ext_parts.h`; las pruebas, en los grupos T117–T120 de
`src/top/sc_main.cpp`.

El Ethernet es el periférico con más piezas del chip, y las piezas no se
parecen entre sí: cuatro bloques de registros que no comparten nada (MAC, MMC,
PTP y DMA), dos caminos de datos que tampoco, dos interfaces físicas distintas
y una máquina de descriptores que vive en la SRAM del usuario y a la que el
periférico llega **por su cuenta**, como maestro del bus.

```
                 ┌──────── AHB1 (esclavo): MAC / MMC / PTP / DMA
   CPU ──────────┤
                 └──────── el firmware solo escribe registros y descriptores

 descriptores  ┌── DMA propio (MAESTRO de la matriz) ──┐
 y buffers  ───┤                                        ├── FIFO ── MAC ── PHY
 en la SRAM    └────────────────────────────────────────┘        MII / RMII
```

---

## 1. El análisis pedido: ¿en qué se diferencian los "canales" del ETH?

Hay **un solo** MAC en el F407, así que —como pasaba con el DCMI— la pregunta
no se responde comparando instancias sino mirando dentro. Y dentro, la palabra
"canal" significa tres cosas distintas.

### 1.1 (A) Las dos interfaces físicas: MII y RMII **no** son dos modos

Son **dos caminos**:

| | MII | RMII |
|---|---|---|
| Hilos de datos | 4 por sentido | **2** por sentido |
| Relojes | **DOS**: TX_CLK y RX_CLK, uno por sentido | **UNO**: REF_CLK común |
| Frecuencia a 100 Mbit/s | 25 MHz | **50 MHz** |
| Señales de medio | CRS, COL y RX_ER aparte | CRS_DV combinada, sin COL |
| Pines totales | **18** | **9** |

Se elige con el bit 23 de `SYSCFG_PMC`, y —esto es lo importante— **solo se
puede cambiar con el MAC en reset**: no es un modo de funcionamiento, es un
cableado. Un firmware que lo toque con el MAC en marcha no cambia nada.

### 1.2 (B) Los dos anillos del DMA **no** son copias

Transmisión y recepción comparten la palabra "descriptor" y poco más. Los
cuatro campos tienen los mismos nombres y significados **completamente
distintos**:

| | Anillo de transmisión | Anillo de recepción |
|---|---|---|
| Quién escribe el descriptor | el **firmware** (órdenes) | el **DMA** (resultado) |
| `xDES0` | FS, LS, IC, CIC, TTSE, cuenta de colisiones | **longitud recibida**, error de CRC, trama larga, filtro que la aceptó |
| `xDES1` | dos tamaños de buffer (TBS1/TBS2) | dos capacidades (RBS1/RBS2) + RCH/RER + DIC |
| Bits de encadenado | TDES0.TCH / TDES0.TER | **RDES1**.RCH / **RDES1**.RER (¡otra palabra!) |
| Arranque | `DMAOMR.ST` | `DMAOMR.SR` |
| Umbral de FIFO | `DMAOMR.TTC` (3 bits) | `DMAOMR.RTC` (2 bits) |
| Estado de la máquina | `DMASR.TPS[22:20]` | `DMASR.RPS[19:17]` |
| Puntero de lista | `DMATDLAR` | `DMARDLAR` |
| Sin buffer libre | `DMASR.TBUS` | `DMASR.RBUS` |

Nótese el detalle más traicionero: **los bits de encadenado y de fin de anillo
están en `TDES0` para transmisión y en `RDES1` para recepción**. Un modelo (o un
driver) que los busque en la misma palabra en los dos casos falla en silencio,
y el síntoma aparece cuando el anillo da la vuelta.

### 1.3 (C) Los cuatro filtros de dirección: **el filtro 0 es distinto**

Igual que el endpoint 0 del USB en la subfase anterior:

| | `MACA0` | `MACA1..3` |
|---|---|---|
| `AE` (habilitación) | **no existe: siempre activo** | sí, y arrancan apagados |
| `SA` (origen o destino) | **no: solo destino** | sí |
| `MBC[5:0]` (máscara de bytes) | **no: compara los seis** | sí |

Con `MBC` se puede aceptar un grupo entero de aparatos con un solo filtro
—enmascarando los bytes bajos—, y con `SA` se filtra por dirección de **origen**,
que es lo que hace un puente. El filtro 0 no sabe hacer ninguna de las dos cosas.

### 1.4 Y en este encapsulado no falta ningún pin: **están todos cogidos**

La tabla del capítulo 2 confirma que los dieciocho pines de MII existen en el
LQFP100 (puertos A, B y C). El problema es otro, y es peor:

- **`ETH_MII_CRS` es `PA0-WKUP`.** Cablear MII cuesta **el pin de despertar
  desde Standby**. No hay otro.
- **`ETH_MII_RXD0/RXD1` son `PC4`/`PC5` = `ADC12_IN14/IN15`.** Cuesta también
  dos entradas del convertidor A/D.
- **`ETH_MDIO` es `PA2` y `ETH_MII_COL` es `PA3`**, que son el USART2 — y `PA3`
  es además `OTG_HS_ULPI_D0`.
- **`ETH_RMII_TX_EN/TXD0/TXD1` son `PB11/PB12/PB13`**, que son `ULPI_D4/D5/D6`,
  `SPI2`, `I2S2`, `I2C2` y `CAN2`. Con MII el solape con el ULPI sube a ocho de
  los doce hilos.

> **Ethernet y USB de alta velocidad por PHY externo no caben juntos en este
> chip.** Es la misma conclusión a la que se llegó desde el otro lado en
> `doc/stm32f4xx/stm32f407vg_fase7_otg.md`, y ahora está comprobada desde los dos.

---

## 2. Cómo se elige el tipo de Ethernet

Misma receta del proyecto (`UsartCaps`, `TimCaps`, `SpiCaps`, `I2cCaps`,
`AdcCaps`, `DacCaps`, `SdioCaps`, `CanCaps`, `DcmiCaps`, `FsmcCaps`, `OtgCaps`):

```cpp
struct EthCaps {
    bool mii, rmii;              // qué interfaz física existe EN LA PLACA
    bool ptp, mmc, pmt;          // qué BLOQUES de registros existen
    bool hash, vlan, checksum, flow;
    unsigned filtros;            // MACA0..MACA3
    unsigned fifo_tx, fifo_rx;
    const char* kind;
};
```

```cpp
// compilación: coste cero, los rasgos son constantes
template <const EthCaps& C> class EthT : public EthBase { ... };
using Eth       = EthT<CAPS_ETH_F407>;    // MII+RMII, PTP, MMC, 4 filtros
using EthRmii   = EthT<CAPS_ETH_RMII>;    // placa cableada en RMII
using EthBasico = EthT<CAPS_ETH_BASIC>;   // sin PTP, sin MMC, un filtro

// ejecución: el mismo modelo con los rasgos por el constructor
EthBase e{"e", EthCaps{...}};
```

**Los rasgos son máscara de escritura y, además, deciden qué grupos de
registros existen:** sin PTP, todo el bloque `0x700` se lee cero; sin MMC, el
`0x100`; sin hash, `MACHTHR`/`MACHTLR`; sin control de flujo, `MACFCR`; con un
solo filtro, `MACA1` no está; sin descarga de suma de comprobación, `IPCO` no se
guarda. Todo verificado **sobre el bus**, no leyendo una tabla.

Y un caso que no es máscara sino **valor fijo**: el bit 15 de `MACCR` (véase
§4.1).

---

## 3. Qué se ha modelado

**El camino de datos por los pines, nibble a nibble.** Una trama sale con su
preámbulo de siete `0x55`, su delimitador `0xD5`, los bytes del buffer y un
**CRC-32 de verdad** calculado sobre la trama; y entra igual. En MII van cuatro
bits por ciclo, en RMII dos. **El reloj no lo pone el MAC: lo pone el PHY**, y
si no llega, el MAC no transmite y lo dice por `DMASR.TUS`.

**MDIO, bit a bit por dos pines.** La trama de 32 bits completa —preámbulo,
arranque, orden, dirección de PHY, dirección de registro, giro y dato— con su
divisor de `MDC` a partir de HCLK. El firmware solo pone `MACMIIAR.MB` y espera
a que el hardware lo borre, que es lo único que ve.

**La máquina de descriptores**, en anillo (con `TER`/`RER` y el hueco que diga
`DMABMR.DSL`) y encadenada (con el puntero en la cuarta palabra), con los dos
buffers por descriptor, el bit `OWN` en los dos sentidos y la trama repartida
entre varios descriptores si hace falta.

**El filtrado de direcciones completo**: los cuatro filtros exactos con su
máscara de bytes, difusión, multicast, promiscuo, `RA`, y el **filtro hash de
64 bits**, cuyo índice son los seis bits altos del mismo CRC-32 que cierra la
trama. El descriptor dice después si la aceptó el hash (`RDES0.AFM`) o un filtro
exacto.

**Los contadores MMC**, con su lectura destructiva opcional (`MMCCR.ROR`), que
es como se hacen estadísticas sin perder cuentas entre dos lecturas.

**El reloj PTP del 1588**, con carga (`TSSTI`) y ajuste con signo (`TSSTU`),
autoborrables los dos, y el sello de tiempo que el descriptor de transmisión se
queda cuando se le pide con `TDES0.TTSE`.

**El despertar por Magic Packet**: seis `0xFF` seguidos de dieciséis copias de
la propia dirección MAC despiertan al MAC dormido, levantan `MACPMTCSR.MPR` y
salen por la **línea 19 del EXTI**, que es lo que saca al MCU de Stop.

**Las interrupciones con sus dos resúmenes.** `DMASR` tiene un resumen *normal*
(`NIS`) y otro *anormal* (`AIS`), y el manejador tiene que borrar **el bit de
causa y el de resumen**: es la fuente número uno de interrupciones que no se
van. `TBUS` es normal; `TUS` es anormal. Modelado y comprobado.

### 3.1 Dónde está la frontera del modelo

La trama cruza los pines de verdad. Lo que **no** se modela es la capa eléctrica
del par trenzado —eso es cosa del PHY, que está al otro lado de esos pines— ni
la autonegociación, que aquí se resuelve por MDIO exactamente como en el
silicio: escribiendo y leyendo los registros del PHY.

### 3.2 Lo que se le suelda al MAC

`EthPhy`, en `verif/ext_parts.h`, es el integrado que hay entre el MAC y el
RJ45: **pone los relojes** (25 MHz en MII, 50 en RMII), **habla MDIO** con sus
registros de la norma (BMCR, BMSR, identificación, ANAR) y con su pull-up de
placa en la línea, **recoge** lo que el MAC transmite y **inyecta** tramas hacia
él, con la posibilidad de romperles el CRC a propósito.

Todos conducen en el flanco de bajada y muestrean en el de subida: medio ciclo
de margen, que es lo que dan los tiempos de establecimiento y mantenimiento de
la norma. Sin ese convenio, el modelo dependería del orden en que SystemC
despierte a dos procesos en el mismo delta, que es no depender de nada.

---

## 4. Seis cosas que costaron trabajo

**1. El informe se contradice consigo mismo en `MACCR`.** Dice que el valor de
reset es `0x0000 8000` y, en la misma tabla, que el bit 14 es `RE` con reset 1.
Pero `0x8000` **es el bit 15**, no el 14: si `RE` valiera uno al reset, el valor
sería `0x4000`. Lo que hay en el bit 15 es un bit reservado que vale uno. Se ha
seguido **el valor de reset**, que es lo comprobable, y ese uno se modela como
fijo: escribir cero en `MACCR` no lo borra. Queda anotado aquí porque el resto
de la tabla de bits de esa sección tampoco cuadra con la disposición estándar
del bloque.

**2. `DMABMR.SR` no es `DMAOMR.SR`.** Dos registros contiguos, dos bits con el
mismo nombre, y uno de ellos —el de `DMABMR`— es el **reset del bloque entero**,
no el "arranca la recepción". Están modelados como lo que son.

**3. Los bits de encadenado están en palabras distintas** según el sentido
(`TDES0` frente a `RDES1`). Escribirlo mal no da error: el anillo funciona hasta
que da la vuelta, y entonces el DMA se va a leer descriptores a una dirección
inventada.

**4. Sin reloj del PHY no hay transmisión, y hay que decirlo bien.** El primer
intento generaba la temporización dentro del MAC a partir de `MACCR.FES`, lo
cual funcionaba... incluso con el PHY desconectado. Ahora el MAC **sigue** los
flancos que le llegan y, si no llegan, aborta y levanta `DMASR.TUS` (vaciado de
la FIFO por debajo), que es exactamente el síntoma real de un PHY que no ha
arrancado.

**5. Los pines del banco de pruebas eran un cortocircuito real.** Las pistas de
las pruebas de puerto serie unen `PA2` con `PB11` y `PC12` con `PA1` — es decir,
**MDIO con TX_EN** y **el reloj de referencia con una salida**. Con el MAC
gobernando esos pads, el modelo denunciaba corriente de pin por encima del
máximo: no era un artefacto, era un corto. La prueba suelta ahora esas cuatro
pistas antes de enchufar el PHY, **y devuelve los pines a entrada antes de
volver a soldarlas**, que era la mitad que faltaba.

**6. MDIO necesita su resistencia de pull-up.** Preguntarle a una dirección de
PHY donde no hay nadie tiene que devolver **todo unos**, y eso no sale de
ninguna lógica: sale de la resistencia de 10 kΩ que lleva la placa. Sin ella, el
modelo devolvía la última tensión que se hubiera quedado en el hilo. Está ahora
en el `EthPhy`, que es donde vive el resto de la placa.

---

## 5. Verificación: 82 comprobaciones

### 5.1 T117 — Los tres sentidos de "canal" (28)

Valores de reset (`MACCR = 0x0000 8000`, `DMABMR = 0x0002 0101`) y el bit 15
que no se puede borrar. **(A)** Las dos interfaces, y `SYSCFG_PMC` eligiendo
entre ellas. **(B)** `ST` arranca la transmisión sin tocar la recepción, cada
máquina tiene su campo de estado en `DMASR` y cada anillo su puntero de lista.
**(C)** El filtro 0 arranca habilitado, no se puede apagar y no tiene ni `MBC`
ni `SA`; el filtro 1 sí tiene las dos cosas y se puede apagar. Y la variante
`CAPS_ETH_BASIC` **instanciada en tiempo de ejecución** sobre su propio maestro
de bus: sin PTP, sin MMC, sin hash, sin control de flujo, sin `MACA1` y sin
`IPCO`.

### 5.2 T118 — MDIO por dos pines (11)

La identificación del PHY leída **por los pines**, no de una tabla; `MACMIIAR.MB`
borrado por el hardware; `BMSR` diciendo que el enlace está arriba y, al caerse
el cable, que ya no —el MAC no se entera solo—; una escritura que llega y un
`BMCR.RESET` que se autoborra en el PHY; y una lectura a la dirección
equivocada que devuelve todo unos, porque en el bus MDIO no hay nadie ahí.

### 5.3 T119 — Una trama entera (22)

Un descriptor de transmisión en la SRAM, `ST`, y la trama aparece en el PHY con
sus cuatro bytes de FCS y **los bytes exactos del buffer**; el CRC-32 comprobado
por el otro extremo. El descriptor devuelto con `OWN` a cero, `DMASR.TS`, el
resumen `NIS`, la IRQ 61 en el NVIC y la matriz confirmando que el acceso a la
SRAM pasó como **`ETH_DMA`**, no como CPU. Sin descriptor libre, `TBUS`. En
recepción: una trama inyectada por el PHY llega a la SRAM, con `FS`, `LS`, la
longitud de la trama entera y sin error. Y con el PHY desconectado, `TUS` en el
resumen **anormal**.

### 5.4 T120 — Filtrado, estadísticas y lo que cuesta en pines (21)

Una trama para otro se tira **antes** del DMA; la dirigida a nosotros entra; la
difusión entra sin programar nada salvo que se vete con `BFD`; un multicast
cuyo bit está en la tabla hash entra y el descriptor dice que lo aceptó el hash;
el filtro 1 con `MBC` acepta un grupo entero; el modo promiscuo entra todo. Una
trama con el CRC roto se detecta, no se entrega y la cuenta el contador MMC. El
reloj PTP se carga y se ajusta con `TSSTI`/`TSSTU`, ambos autoborrables. Un
Magic Packet despierta al MAC y sale por EXTI19. Y el recuento de pines: los
dieciocho existen, pero `MII_CRS` es `PA0-WKUP`.

### 5.5 La suite entera

```
Resumen F7 (bajo consumo): 116 comprobaciones OK, 0 fallos
Resumen F7 (DCMI)        :  61 comprobaciones OK, 0 fallos
Resumen F7 (FSMC)        :  54 comprobaciones OK, 0 fallos
Resumen F7 (OTG)         : 113 comprobaciones OK, 0 fallos
Resumen F7 (ETH)         :  82 comprobaciones OK, 0 fallos
TOTAL                    : 1811 comprobaciones OK, 0 fallos   (~23 s)
```

---

## 6. Limitaciones, dichas claramente

- **No hay control de flujo activo.** `MACFCR` guarda y enmascara, pero el MAC
  no emite ni interpreta tramas PAUSE.
- **La descarga de suma de comprobación no calcula nada.** `IPCO`, `CSTF` y
  `TDES0.CIC` se guardan; el modelo no rellena ni verifica sumas IP/TCP/UDP.
- **Semidúplex sin colisiones.** `MACCR.DM`, `BL`, `DC` y `RD` se guardan, y
  `COL` es una entrada, pero no hay algoritmo de retroceso exponencial ni
  reintentos: el modelo trabaja en dúplex completo.
- **Descriptores de cuatro palabras.** El formato mejorado de ocho —con los
  sellos PTP dentro del propio descriptor— no se implementa; el sello va en las
  palabras 2 y 3 del de transmisión.
- **PTP simplificado**: el reloj se lee del tiempo de simulación en lugar de
  acumular `PTPSSIR` en cada tick, y no hay disparo de `PPS` ni alarma por
  `PTPTTHR/TTLR`.
- **El MMC lleva cinco contadores**, no los treinta y tantos del bloque
  completo.
- **VLAN**: `MACVLANTR` se guarda; no hay filtrado por etiqueta.

---

## 7. Estado

Subfase F7-ETH **completada**. El MAC está parametrizado por rasgos con tres
variantes seleccionables en compilación o en ejecución; la trama sale y entra
por los pines con su preámbulo y su CRC-32 de verdad, siguiendo el reloj que
pone el PHY; MDIO es una trama de 32 bits que viaja por dos hilos; el filtrado
de direcciones está completo, hash incluido; y el DMA propio va a la SRAM por su
cuenta como octavo maestro de la matriz.

**Con esto queda cerrada la fase F7 entera** (bajo consumo, DCMI, FSMC, OTG y
ETH). Del plan original solo queda el afinado del bus de LT a AT.
