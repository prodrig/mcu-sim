# Fase F7 (parte de OTG) — USB, y las tres cosas que se llaman "canal"

Modelo de los dos controladores USB On-The-Go del STM32F407VG en SystemC 2.3,
sobre `doc/refs/stm32f407xx/informe_revisado.md` [IR, §12.15 (OTG_FS), §12.23 (OTG_HS)] y la
tabla de pines del capítulo 2. El código está en `src/periph/otg.h`; los dos
extremos del cable, en `src/verif/ext_parts.h`; las pruebas, en los grupos
T113–T116 de `src/top/sc_main.cpp`.

El OTG es el periférico más grande del chip —unos noventa registros— y el único
que trabaja **a tres niveles a la vez**:

1. **Electricidad.** Dos hilos, D+ y D−, y una red de resistencias que decide
   todo lo demás. El anfitrión pone 15 kΩ a masa en los dos; el dispositivo, al
   querer que lo vean, pone 1,5 kΩ a 3,3 V en **uno** de ellos: D+ si es Full
   Speed, D− si es Low Speed. **No hay ningún registro que diga "hay algo
   enchufado"**: lo dice el divisor resistivo.
2. **Protocolo.** Testigos, datos y acuses (SETUP/IN/OUT, DATA0/DATA1,
   ACK/NAK/STALL) con su bit de conmutación.
3. **Memoria.** Una RAM de FIFOs que el firmware **parte a mano**, y que nadie
   comprueba.

El modelo hace los tres.

---

## 1. El análisis pedido: ¿en qué se diferencian los "canales" del OTG?

La palabra "canal" significa aquí tres cosas distintas, y conviene no
mezclarlas. Las tres se han modelado; sólo una de ellas resulta ser un conjunto
de copias.

### 1.1 (A) Las dos instancias: OTG_FS y OTG_HS **no** son dos copias

| | OTG_FS | OTG_HS |
|---|---|---|
| Bus de configuración | **AHB2**, `0x5000_0000` | **AHB1**, `0x4004_0000` |
| Maestro de la matriz | **no** | **sí**: DMA propio, el **octavo maestro** |
| Transceptor | integrado (PA11/PA12) | externo por **ULPI**, o el integrado (PB14/PB15) |
| Velocidad | 12 Mbit/s | 480 Mbit/s con el PHY externo |
| RAM de FIFOs | **1,25 KB** (320 palabras) | **4 KB** (1024 palabras) |
| Endpoints (EP0 incluido) | **4** | **6** |
| Canales de anfitrión | **8** | **12** |
| Líneas de interrupción | 1 global + WKUP | 1 global + WKUP + **EP1_IN** + **EP1_OUT** |
| Reloj | 48 MHz de PLL48CK | 60 MHz del PHY por ULPI (o PLL48CK en FS) |
| `GAHBCFG` | sin `DMAEN` ni `HBSTLEN` | con los dos |
| `GUSBCFG.PHYSEL` | **de sólo lectura, a uno** | escribible |
| Transacciones split | no | sí (`HCSPLT`) |

> ⚠ **Discrepancia detectada en el informe.** El §12.15.1 dice "hasta 8 canales
> de host o **4 endpoints de dispositivo (además del endpoint 0)**", pero el
> mapa de registros de la misma sección enumera "por endpoint **(0-3)**" y sólo
> `DIEPTXF1-3`. Son cuatro endpoints **en total**, EP0 incluido. El modelo sigue
> al mapa de registros, que es el que se puede comprobar.

### 1.2 (B) Los canales de anfitrión **sí** son copias

Los ocho (o doce) canales del modo anfitrión tienen el mismo juego de registros
—`HCCHARx`, `HCSPLTx`, `HCINTx`, `HCINTMSKx`, `HCTSIZx` (+ `HCDMAx` en el HS)—
y la misma máquina de estados. Lo único que los distingue es a qué FIFO van
(periódica o no, según `EPTYP`). Verificado: el canal 3 y el canal 9 del HS se
comportan exactamente igual; el 9 del FS no existe y se lee cero.

### 1.3 (C) Los endpoints de dispositivo **no** son copias: **el 0 es distinto**

Es la trampa más fina de todo el periférico. `DIEPCTL0` y `DIEPTSIZ0` **no
tienen la misma forma** que los de los demás endpoints:

| | EP0 | EP1..n |
|---|---|---|
| `MPSIZ` | **2 bits codificados** (0=64, 1=32, 2=16, 3=8) | **11 bits** de bytes |
| `XFRSIZ` | **7 bits** | **19 bits** |
| `PKTCNT` | **2 bits** | **10 bits** |
| `EPTYP` | siempre control, no escribible | 4 tipos |
| `EPDIS` | **no se puede deshabilitar** | sí |
| SETUP | **es el único que los recibe** | no |
| `USBAEP` | siempre activo | programable |

Y una diferencia más, que no está en el endpoint sino en el mapa: la FIFO de
transmisión del EP0 se programa en `0x028`, que **en modo anfitrión es otro
registro** (`HNPTXFSIZ`). El mismo offset con dos significados según el rol.

Un modelo que trate los endpoints como un vector homogéneo se equivoca en los
siete puntos de la tabla. Aquí van por una máscara distinta (`mask_epctl(ep,
in)` y `mask_eptsiz(ep, in)` tratan `ep == 0` aparte), y se comprueba.

### 1.4 Y en este encapsulado, por una vez, **no falta nada**

Al revés que con el DCMI (faltaban D12/D13) y que con el FSMC (faltaban A0–A15
y tres chip selects), aquí el LQFP100 lo tiene **todo**: los doce pines del
ULPI viven en los puertos A, B y C —PA3, PA5, PB0, PB1, PB5, PB10–PB13, PC0,
PC2, PC3— y los cuatro del OTG_FS en PA9–PA12.

Lo que hay es un problema distinto, y peor: **esos pines están cogidos**.

- **ULPI y Ethernet no caben a la vez.** PB11, PB12 y PB13 son `ULPI_D4/D5/D6`
  y a la vez `ETH_RMII_TX_EN/TXD0/TXD1`; con MII el solape sube a ocho de los
  doce. Una placa con Ethernet deja al OTG_HS con su transceptor Full Speed.
- **Con ULPI, el OTG_HS se queda sin pin ID y sin sensado de VBUS.** PB12 es
  `ULPI_D5` **y** `OTG_HS_ID`; PB13 es `ULPI_D6` **y** `OTG_HS_VBUS`. Si se
  cablea el PHY externo, la detección de rol y de sesión ya no puede hacerse por
  los pines del MCU: la hace el PHY y se lee por el propio ULPI.
- **ULPI_CK es DAC_OUT2.** PA5. Poner el PHY de alta velocidad cuesta el segundo
  canal del convertidor D/A.
- Y en el FS, PA11/PA12 son también CAN1_RX/CAN1_TX.

---

## 2. Cómo se elige el tipo de OTG

Misma receta del proyecto (`UsartCaps`, `TimCaps`, `SpiCaps`, `I2cCaps`,
`AdcCaps`, `DacCaps`, `SdioCaps`, `CanCaps`, `DcmiCaps`, `FsmcCaps`):

```cpp
struct OtgCaps {
    bool hs, ulpi, phy_fs, dma_interno;   // qué transceptor y quién mueve datos
    bool host, device, otg;               // qué GRUPOS de registros existen
    bool ep1_irq, split;
    unsigned endpoints;                   // EP0 incluido
    unsigned canales;
    unsigned fifo_palabras;               // la RAM compartida, de verdad
    uint32_t cid;
    const char* kind;
};
```

```cpp
// compilación: coste cero, los rasgos son constantes
template <const OtgCaps& C> class OtgT : public OtgBase { ... };
class OtgFs : public OtgT<CAPS_OTG_FS> { ... };   // PHY integrado, 4 EP, 320 pal.
class OtgHs : public OtgT<CAPS_OTG_HS> { ... };   // ULPI + DMA, 6 EP, 1024 pal.

// ejecución: el mismo modelo con los rasgos por el constructor
OtgBase u{"u", base, tam, CAPS_OTG_DEV};          // sólo dispositivo
```

Cuatro variantes, y las cuatro dicen algo:

| Variante | Para qué |
|---|---|
| `CAPS_OTG_FS` | el OTG_FS del F407VG |
| `CAPS_OTG_HS` | el OTG_HS con su PHY ULPI |
| `CAPS_OTG_HS_FS` | **el mismo núcleo HS en una placa sin PHY externo**: sigue teniendo DMA, seis endpoints y doce canales, pero los bits de ULPI ya no se guardan y `PHYSEL` se queda a uno. Es el caso real de casi todas las placas pequeñas con F407 |
| `CAPS_OTG_DEV` | el mínimo que sigue siendo USB: sólo dispositivo, sin anfitrión y sin OTG. Todo el bloque `0x400` y `GOTGCTL` se leen cero |

**Los rasgos son máscara de escritura y, además, deciden qué grupos de
registros existen.** Sin rol de anfitrión, `HCFG` y `HPRT` leen cero entero; sin
OTG, `GOTGCTL` también; el `DIEPTXF5` existe en el HS y no en el FS; el canal 9
existe en el HS y no en el FS. Verificado sobre el bus, no leyendo una tabla.

Un caso que no es máscara sino **valor forzado**: `DCFG.DSPD` son dos bits en
todos los núcleos, pero sin PHY de alta velocidad los códigos `00` y `01` no
existen y el bit 1 se queda a uno pase lo que pase.

---

## 3. Qué se ha modelado

**El transceptor, en los pines, con tensiones en `float`.** El 1,5 kΩ del
dispositivo, los dos 15 kΩ del anfitrión, la impedancia de 45 Ω del emisor y los
estados de línea (J, K, SE0, SE1). De ahí salen, medidos y no declarados:

- la **conexión** (el hilo que sube contra los 15 kΩ),
- la **velocidad** (D+ = Full Speed, D− = Low Speed),
- el **reset de bus** (SE0 durante más de 2,5 µs),
- la **reanudación** (una K larga),
- el **VBUS** de 5 V y el **pin ID** que decide el rol.

**Los dos roles.** En modo dispositivo el núcleo *implementa* la interfaz
`usb_dev_if`; en modo anfitrión la *llama*. La simetría no es decorativa: el OTG
es de doble rol y el mismo bloque hace las dos cosas según `GOTGCTL.CIDSTS` o
según `GUSBCFG.FHMOD/FDMOD`.

**Los endpoints de dispositivo**, con su `EPENA`/`NAKSTS`/`STALL`, su bit de
conmutación, sus contadores `XFRSIZ`/`PKTCNT` y el fin de transferencia por
cuenta agotada **o por paquete corto**.

**Los canales de anfitrión**, con `CHENA`, su `HCTSIZ` y sus interrupciones
`XFRC`/`NAK`/`STALL`/`ACK`, resumidas en `HAINT` y en `GINTSTS.HCINT`.

**La RAM de FIFOs, con direcciones de verdad.** `GRXFSIZ` y `DIEPTXFn` no son
números que se guardan: son la partición real de un `std::vector<uint32_t>` del
tamaño exacto del silicio, y si dos particiones se solapan, **se corrompen**.

**El DMA interno del HS**, como octavo maestro de la matriz: con `GAHBCFG.DMAEN`
el núcleo no deja los datos en una FIFO para que alguien los saque, sino que los
escribe él mismo en la SRAM a partir de `DIEPDMAx`/`DOEPDMAx`/`HCDMAx`.

**Las interrupciones**, con sus tres niveles encadenados: `DIEPINT`/`DOEPINT` →
`DAINT`/`DAINTMSK` → `GINTSTS`/`GINTMSK` → `GAHBCFG.GINTMSK` → NVIC. Más las
dos líneas dedicadas del EP1 del HS y la salida de despertar hacia EXTI18
(FS) y EXTI20 (HS).

**El latido**: SOF cada 1 ms (125 µs en alta velocidad), la suspensión a los
3 ms sin actividad y el despertar por K.

### 3.1 Dónde está la frontera del modelo

Los pines llevan tensiones de verdad y de ellas salen todos los eventos
eléctricos. Lo que **no** se modela es la codificación NRZI, el relleno de bits
ni el CRC: un paquete cruza el cable como paquete, no como una tira de bits a
480 Mbit/s. Es una frontera deliberada —simular el bitstream no aporta nada a un
modelo de MCU— y está escrita en la cabecera del fichero para que nadie la
descubra por sorpresa.

### 3.2 Lo que se le engancha al cable

En `verif/ext_parts.h`, dos aparejos simétricos:

- **`UsbHostRig`** — un PC: da los 5 V de VBUS, pone los dos 15 kΩ a masa, hace
  el reset con un SE0 de 10 ms, manda testigos y emite el SOF de cada
  milisegundo.
- **`UsbDeviceRig`** — un pendrive: pone su 1,5 kΩ en D+ (o en D− si se le pide
  baja velocidad) cuando lo enchufan y hay VBUS, contesta a los testigos con un
  descriptor de dieciocho bytes, y **se entera del reset porque ve el SE0**, no
  porque nadie se lo diga.

---

## 4. Siete cosas que costaron trabajo

**1. Los valores de reset de la partición de FIFOs no son una partición
válida.** `GRXFSIZ` arranca pidiendo 512 palabras de una RAM de 320, y las
cuatro `DIEPTXF` arrancan solapadas entre sí. No es un descuido del modelo: son
los valores que documenta el manual. Programarlas **todas** antes de usar el
periférico no es una recomendación, es un requisito, y el silicio no avisa de
nada. El modelo sí: `fifos_solapadas()` lo dice, y arranca diciendo que la
partición es inválida.

**2. `HPRT.PENA` se BORRA escribiendo uno.** Cuatro bits de `HPRT` —`PCDET`,
`PENA`, `PENCHNG`, `POCCHNG`— son `rc_w1`, de modo que el
lee-modifica-escribe de toda la vida **apaga el puerto que se acaba de
habilitar**. Es el error clásico del OTG en modo anfitrión y está reproducido
tal cual, con su comprobación.

**3. Sin 48 MHz no hay USB, y ningún registro lo dice.** El transceptor Full
Speed integrado saca su reloj de bit de PLL48CK. Sin él no aparece siquiera el
pull-up de D+: para el PC no hay nada enchufado, y el firmware no tiene dónde
enterarse. Es la avería número uno de una placa nueva con USB, y en el modelo se
ve **en el pin**: D+ se queda a 0 V.

**4. Un anfitrión no puede "ver" un cable que nadie sujeta.** El primer intento
detectaba conexión en cuanto D+ estaba alto — y un nodo analógico flotante
conserva su última tensión, que en el banco de pruebas venía de un enlace del
I2S dejado puesto tres grupos antes. La detección se hace ahora sólo con el par
sujeto por los 15 kΩ, y los pines se actualizan **en el acto** al escribir
`HPRT`, no un delta después. Y, como en las subfases del DCMI y del FSMC, las
pruebas **sueltan explícitamente** lo que estorba (`can_links`, `spi_links`,
`i2s_links`) antes de enchufar el cable: es una decisión de placa y se toma a la
vista.

**5. `GRXSTSR` mira y `GRXSTSP` saca.** Es la única pareja de registros del chip
que se diferencia sólo en el efecto lateral de leerla. Y la cola de estado y la
FIFO de datos son **dos colas distintas**: un SETUP de ocho bytes deja *dos*
palabras de estado (datos y fin) y *dos* de datos. `RXFLVL` se retira solo
cuando la cola de estado se vacía; no es `rc_w1`.

**6. El reset del núcleo tiene que autoborrarse.** `GRSTCTL.CSRST` lo escribe el
firmware y espera a que se apague. Un modelo que lo dejara puesto colgaría a
cualquier pila USB en el arranque, y el síntoma sería un cuelgue mudo.

**7. Dos peculiaridades de SystemC.** La primera: una variante **sin** DMA
propio deja su socket iniciador sin enlazar, y SystemC no admite puertos
sueltos; en vez de fingir que el OTG_FS es maestro de la matriz se le construye
—sólo cuando hace falta— un tapón que contesta `ERROR`, de modo que un uso
indebido se vería en el acto. La segunda: los nodos analógicos se enganchan
*después* de construir el módulo, cuando ya no se puede ampliar la lista
estática de sensibilidad; la lista de los pines se arma por eso en tiempo de
ejecución, dentro de un único proceso (`pines_proc`) que además garantiza un
solo escritor.

---

## 5. Verificación: 113 comprobaciones

### 5.1 T113 — Las dos instancias y los tres sentidos de "canal" (33)

Endpoints, canales, tamaño de RAM, DMA, ULPI y CID de cada instancia, leídos
por el bus. `DMAEN`/`HBSTLEN` sólo se guardan donde hay DMA; `PHYSEL` se queda a
uno en el FS aunque se escriba cero; los bits de ULPI se leen cero en el FS.
Los canales 3 y 9 del HS se comportan igual (**son copias**) y el 9 del FS no
existe. El EP0 **no** es uno más: `MPSIZ` de dos bits codificados frente a once
de bytes, `XFRSIZ` de siete bits frente a diecinueve. `DSPD = 00` no cuela en un
núcleo FS y sí en el HS. Y la variante `CAPS_OTG_DEV` **instanciada en tiempo de
ejecución** sobre su propio maestro de bus: sin `HCFG`, sin `HPRT`, sin
`GOTGCTL` y con sólo tres endpoints.

### 5.2 T114 — El PHY en los pines (25)

VBUS medido en el pin; `GOTGCTL.BSVLD` con `VBUSBSEN`. El 1,5 kΩ de D+ sube el
hilo a 3,00 V contra los 15 kΩ del PC —y D− se queda abajo: **eso** es Full
Speed—. `DCTL.SDIS` lo quita y D+ cae: para el PC el aparato se ha ido, sin
tocar el cable. Sin los 48 MHz, D+ ni se mueve. Con `PWRDWN` a cero, tampoco.
El pin ID a masa convierte el núcleo en anfitrión y avisa por `CIDSCHG`. Diez
milisegundos de SE0 son un reset de bus que el núcleo **ve**, con `USBRST` y
`ENUMDNE` detrás. Tres milisegundos sin SOF y se suspende; una K larga lo
despierta con `WKUINT` **y por la línea 18 del EXTI**, que es lo que permite
salir de Stop.

### 5.3 T115 — Endpoint 0, FIFOs y la partición que nadie comprueba (30)

La partición inválida de reset; una válida (128 + 64 + 32 + 32 + 32 = 288 de
320); la avería clásica de empezar la FIFO 0 dentro de la de recepción; y
pasarse del final de la RAM. Después, una enumeración de verdad: SETUP →
`RXFLVL` → IRQ 67 con sus tres máscaras → `GRXSTSR` que mira y `GRXSTSP` que
saca → los ocho bytes por la ventana de FIFO → `DOEPINT0.STUP`. La respuesta:
dieciocho bytes empujados por la ventana de FIFO que el PC recibe **exactamente
iguales**, con `XFRC` detrás. `NAK` sin `EPENA`, `STALL` con `STALL`, y un SETUP
que se acepta **incluso con el endpoint en STALL** —la única forma de rescatar
un dispositivo atascado—. `SET_ADDRESS`: con la dirección 5 puesta, a la cero ya
no contesta nadie. Y `CSRST`, que se borra solo.

### 5.4 T116 — El OTG_HS: anfitrión, DMA propio y los pines (25)

`FHMOD` fuerza el rol sin mirar el pin ID. Con el puerto alimentado y nada
enchufado, `PCSTS = 0`; al enchufar, el 1,5 kΩ del aparato levanta D+ y el
anfitrión **lo ve**, con `PCDET` y con `PSPD` sacado del hilo que subió. `PRST`
pone los dos hilos a cero y el aparato ve un reset **de verdad**; al soltarlo el
puerto queda habilitado —y devolver `HPRT` entero lo apaga—. El SOF de cada
milisegundo. Un canal completo: SETUP por la FIFO no periódica y tres paquetes
de vuelta, con `GRXSTSP` dando el número de **canal**. Y el DMA: con `DMAEN`, el
SETUP lo **lee** el núcleo de la memoria y el descriptor lo **escribe** él en la
SRAM, sin que la CPU toque una FIFO — verificado además en el contador de la
matriz, que lo ha visto pasar como **`OTG_HS_DMA`**, no como CPU.

### 5.5 La suite entera

```
Resumen F7 (bajo consumo): 116 comprobaciones OK, 0 fallos
Resumen F7 (DCMI)        :  61 comprobaciones OK, 0 fallos
Resumen F7 (FSMC)        :  54 comprobaciones OK, 0 fallos
Resumen F7 (OTG)         : 113 comprobaciones OK, 0 fallos
TOTAL                    : 1729 comprobaciones OK, 0 fallos   (~18 s)
```

---

## 6. Limitaciones, dichas claramente

- **La capa física es de paquetes, no de bits** (véase §3.1). No hay NRZI, ni
  relleno de bits, ni CRC5/CRC16, ni reintentos por error de CRC.
- **La interfaz ULPI está cableada pero no habla.** Los doce pines existen, se
  registran en AF10 y los bits de `GUSBCFG` que la configuran se guardan; lo que
  no se modela es el protocolo de registros del PHY externo por el bus de ocho
  bits con `DIR`/`NXT`/`STP`. Un núcleo HS con ULPI, en este modelo, mueve datos
  como si el transceptor fuera transparente.
- **Alta velocidad de verdad, no.** `DSTS.ENUMSPD` puede quedar en `00` y las
  tramas bajan a 125 µs, pero no hay negociación de *chirp* K/J ni transacciones
  split reales (`HCSPLT` se guarda y no se usa).
- **SRP y HNP están en los bits, no en la máquina.** `GOTGCTL` y `GOTGINT`
  guardan y notifican; la negociación de sesión y el traspaso de rol no se
  simulan paso a paso.
- **Las transferencias isócronas y de interrupción no tienen planificador.**
  `EPTYP` se guarda; el reparto por microtrama entre FIFO periódica y no
  periódica no se modela, y todos los canales se atienden en cada trama.
- **Un solo dispositivo por puerto**: no hay concentradores (hubs) ni, por
  tanto, direccionamiento de más de un aparato a la vez.
- La **ruta analógica de D+/D− no está condicionada a que el pin esté en AF10**,
  igual que la del ADC y la del DAC en este proyecto: el transceptor se gobierna
  con sus propios bits (`GCCFG.PWRDWN`, `DCTL.SDIS`) y con el reloj.

---

## 7. Estado

Subfase F7-OTG **completada**. Los dos controladores están parametrizados por
rasgos con cuatro variantes seleccionables en compilación o en ejecución, el
transceptor mueve tensiones de verdad en los pines —y de ellas salen la
conexión, la velocidad, el reset y el despertar—, la RAM de FIFOs se parte con
direcciones reales y detecta los solapes que el silicio calla, y el OTG_HS
trabaja como octavo maestro de la matriz llevando datos a la SRAM por su cuenta.

Queda pendiente de la fase F7: **ETH** (el MAC con su DMA dedicado) y el afinado
a AT.
