# Catálogo de componentes externos

Referencia de las **44 piezas** que la factoría de `src/parts/` sabe construir:
qué terminales tiene cada una, qué parámetros admite, qué hace cada parámetro y
qué queda fuera del fichero.

Una placa se describe en XML y se monta con:

```
./build/mcu-sim placa.xml [firmware.bin] [ms]
./build/mcu-sim placa.xml --valida        # comprueba la placa sin simular
./build/mcu-sim --help                    # lista los tipos que conoce la factoría
./build/mcu-sim --help Button             # qué hace ese componente y qué admite
```

**Este documento y `sim --help` dicen lo mismo, y no por casualidad.** La ficha
que imprime `--help` no está escrita en el programa que la imprime: viaja con el
registro de la pieza en la factoría, en la misma llamada que da de alta su
creador, de modo que no hay dos sitios que puedan discrepar. La macro de
registro **exige** esa ficha, así que una pieza nueva no se puede dar de alta
sin explicarse, y la suite comprueba que ninguna se ha registrado con una ayuda
de adorno (T126). El porqué está en `src/parts/part_help.h`.

De los dos, `--help` es el que está siempre al día y el que se tiene a mano sin
salir de la consola; esto es el catálogo, con tablas, comparaciones y los
ejemplos largos que no caben en una ficha.

El formato y el porqué están en `doc/stm32f4xx/stm32f407vg_parts_paso3.md`.

---

## 1. La idea en una página

Todo lo que se suelda fuera del encapsulado se conecta a **nodos**, y un nodo es
un punto eléctrico: un `AnalogNet` donde cada pieza se registra como fuente
Thevenin `{V, Rout}`. La tensión del nodo la resuelve la superposición de todas
ellas, en `float`. No hay lógica de 0 y 1 en esa frontera: un cero gana a un
pull-up porque 30 Ω es mil veces menos que 4700 Ω, no porque el modelo lo
decida.

De ahí salen tres cosas que conviene tener presentes al escribir una placa:

* **La alta impedancia es un estado de verdad.** `R_HIZ` vale 10¹² Ω. Una pieza
  desoldada, un pulsador sin pulsar o un driver en reposo no desaparecen: siguen
  ahí presentando una resistencia enorme.
* **Un nodo que nadie gobierna conserva su última tensión.** No vale cero. Si
  ninguna pieza tira de un nodo externo, lo que se lea de él será lo que dejó
  otro. `--valida` avisa de eso.
* **Las resistencias importan.** El `r` de un LED decide su corriente, y el
  `r_pull` de un bus I2C decide cuánto tarda la línea en subir frente a un pin
  que la suelta.

---

## 2. Cómo se establecen las conexiones

### 2.0 Los MCUs: `<mcu>`

Una placa declara los chips que lleva:

```xml
<mcu tipo="STM32F407VG" id="u0" firmware="maestro.bin" depuracion="dap"   puerto_gdb="3333"/>
<mcu tipo="STM32F407VG" id="u1" firmware="esclavo.bin" depuracion="pines" puerto_gdb="3334"/>
```

| Atributo | Omisión | Efecto |
| :--- | :--- | :--- |
| `tipo` | *(obligatorio)* | El modelo. Se saben construir **los veintiún miembros de la familia F405/F407/F415/F417**: `STM32F405RG`, `STM32F405OG`, `STM32F405VG`, `STM32F405ZG`, `STM32F405OE`, `STM32F407VE`, `STM32F407VG`, `STM32F407ZE`, `STM32F407ZG`, `STM32F407IE`, `STM32F407IG`, `STM32F415RG`, `STM32F415OG`, `STM32F415VG`, `STM32F415ZG`, `STM32F417VE`, `STM32F417VG`, `STM32F417ZE`, `STM32F417ZG`, `STM32F417IE` y `STM32F417IG` — los diez últimos son los que llevan el acelerador criptográfico. `sim --help` los lista, y `--mcu TIPO` manda sobre él. Lo que los distingue está en `doc/reutilizacion.md` §9 |
| `id` | *(obligatorio)* | El prefijo de sus nodos (`u0.PD12`) y su nombre en la jerarquía de SystemC |
| `firmware` | ninguno | La imagen que se le carga en la Flash. **Una por chip** |
| `depuracion` | `pines` | `pines`: expone SWCLK/SWDIO y el stub se cuelga por fuera, como un ST-LINK. `dap`: reserva los cinco pines de depuración y el stub habla con el núcleo por llamada de función. `openocd`: como `pines`, pero lo que se cuelga es una sonda `remote_bitbang` para que OpenOCD genere el SWD; `puerto_gdb` es entonces el de esa sonda (T5 del MSP432) |
| `puerto_gdb` | `0` | Puerto TCP de su stub. **0 = no se abre ninguno** |

**El MCU no se supone.** Si la placa no declara ninguno, lo pone `--mcu TIPO`
en la línea de órdenes —un chip sin id, con los nodos de nombre desnudo—; si
declara uno y además se pasa `--mcu`, **gana `--mcu`**: sustituye el tipo del
`<mcu>` y lo dice. Y si no hay ni lo uno ni lo otro, **la placa va sin MCU**
(§2.1 dice qué cambia entonces en los nodos).

**Un `<mcu>` no lleva hijos.** Sus 144 pads existen sin declararlos: un
`<pin>` dentro de un `<mcu>` se rechaza.

**Cómo se nombran los pines**, que es la parte que hay que tener clara:

| MCUs declarados | Nombres de nodo válidos |
| :--- | :--- |
| ninguno, con `--mcu` | Solo el desnudo: `PD12` |
| ninguno, sin `--mcu` | Ninguno: **la placa no lleva MCU** y `PD12` es un error |
| uno | Los dos: `PD12` y `u0.PD12`, y designan el mismo pad |
| dos o más | Solo el cualificado. `PD12` a secas es un error que dice cuáles son los candidatos |

Con dos o más chips, **cada uno lleva lo suyo en el XML**: un firmware o un
puerto sueltos en la línea de órdenes ya no dicen a cuál, y se rechazan. Con uno
solo —de `<mcu>` o de `--mcu`— los argumentos de siempre valen y mandan sobre el
fichero. Los detalles están en `doc/multi_mcu.md`, §5.

**El firmware, chip a chip, desde la línea de órdenes.** Con cualquier número de
chips, `--firmware ID=FICHERO` le da el suyo a uno —`--firmware N/u0=x.bin`, con
el id del sistema— y manda sobre su `firmware=`; `--sin-firmware=ID` lo deja sin
ninguno aunque el XML lo diga, y `--sin-firmware` a secas, a todos. Sin
firmware, el núcleo se aparca en `wfe`. Un id que no está, el mismo chip dos
veces o todo esto junto al firmware posicional son errores que dicen por qué. Y
`mcu-sim placa.xml --mcus` lista los chips —id, tipo y firmware del XML— en XML:
es lo que usa el diálogo de lanzamiento de `mcu-sim-gui` para poner una fila de
firmware por chip, con su casilla «sin firmware» (su plan §42).

**La ROM y el TLV de un MSP432P4** (T4 del MSP432). Su `<mcu>` admite
`rom="..."` y `tlv="..."`, y la línea de órdenes `--rom [ID=]VALOR` y `--tlv
[ID=]FICHERO`, que mandan sobre el XML. La ROM de DriverLib es `r1` por
omisión —la sustituta, que va dentro del simulador—, `ninguna`, o la de TI de
un fichero: un volcado de los 32 KB de 0x0200_0000 o la imagen con símbolos
que instala CCS (`msp432_driverlib_rom_image.out`). El TLV es el sintético
por omisión, o los 4 KB de 0x0020_1000 volcados de una placa; con él, el DCO
del modelo sigue las calibraciones de esa pieza. Sin ID se aplica a todos los
chips que los tienen; un chip que no los tiene, con ID o en su XML, es un
error.

**El Ethernet y el USB de un MSP432E4** (T7 del MSP432). No son piezas de la
placa sino puentes del chip: su `<mcu>` admite `eth="..."` y `usb="..."`, y la
línea de órdenes `--eth [ID=]DESTINO` y `--usb [ID=]DESTINO`, que mandan sobre
el XML. `eth` lleva las tramas de su PHY interno a `memoria`, `qemu:PUERTO` o
`qemu-cliente:HOST:PUERTO` (el protocolo de `-netdev stream` de QEMU, el de
gvproxy y passt), con `,pcap=FICHERO` detrás si se quieren guardar
(`parts/puente_ethernet.h`). `usb` pone un PC en D+/D- (PL6/PL7) que enumera
el CDC-ACM del firmware y lo saca a un puerto del puente serie: los mismos
destinos que `--serie` (`rfc2217:PUERTO`, `tcp:PUERTO`, sus `-cliente:` y
`memoria`), con los baudios, el formato y DTR/RTS del terminal convertidos en
`SET_LINE_CODING` y `SET_CONTROL_LINE_STATE` (`parts/puente_usb.h`). Por TCP
conviene `--tiempo-real`. Desde S6 del F303, `usb` vale también para un **STM32F303**
(xB/C y F302; el F358 no tiene USB): el PC va a D+/D- en PA12/PA11, y la placa
pone la resistencia de subida de D+ (R16 en `stm32f3discovery.xml`), que el
chip no tiene; ese PC deja 125 µs entre dos transferencias de control, como uno
de verdad (`ControladorUsb::pausa_control_us`).

### 2.1 Los nodos

```xml
<placa nombre="mi-placa">
  <nodo id="PA5"/>                          <!-- un pin: ya existe -->
  <nodo id="n_led1" externo="si"/>           <!-- hay que crearlo -->
  <nodo id="PB6" bus="si"/>                  <!-- varios conductores, a propósito -->
  <nodo id="n_puente" une="PB9 PD3"/>        <!-- dos pines soldados entre sí -->
  ...
</placa>
```

| Atributo | Omisión | Efecto |
| :--- | :--- | :--- |
| `id` | *(obligatorio)* | El nombre del nodo |
| `externo` | `no` | `si` = no es un pin del MCU; el netlist lo crea |
| `bus` | `no` | `si` = varias piezas pueden conducir a la vez sin que sea un error |
| `une` | *(ninguno)* | Los pads que **son** este nodo, separados por espacios. Al menos dos |

**Los nodos de pin no hace falta declararlos.** Los 144 pads del chip y los diez
nodos de alimentación y arranque existen desde el principio, con su nombre de
esquemático: `PA0`…`PI15`, `VDD`, `VSS`, `VDDA`, `VSSA`, `VREF+`, `VBAT`,
`VCAP1`, `VCAP2`, `NRST`, `BOOT0`. Declararlos con `<nodo id="PA5"/>` es
opcional y solo sirve para documentar. **Los externos sí**: sin
`externo="si"` el nodo no existe y la validación lo rechaza.

**En una placa sin MCU** no hay pads: todo nodo es de la placa, así que cada
`<nodo>` es externo sin tener que escribir `externo="si"`. Un nodo con nombre de
pin —`PD12`, `VDD`, `u0.PA0`— se rechaza con la pista de declarar el chip,
porque casi siempre es un `<mcu>` que falta. `placas/fuente_y_masa.xml` es una.

**`bus="si"` no cambia nada eléctrico**; solo calla el aviso de conducción
simultánea. Úsese cuando varias piezas conducen ese nodo por diseño —un bus de
colector abierto, un cable en Y— o cuando un pin se comparte a propósito entre
dos montajes. Si no se declara y hay dos conductores, `--valida` lo dice.
Los conductores se cuentan **por hilo, no por nombre**: en un sistema, el
`NRST` de una placa y el de otra se llaman igual y son dos hilos distintos —el
RESET de cada Nucleo es solo suyo—, y no se mezclan; si un acople o un `une`
los junta, entonces sí son uno y cuentan juntos.

**Los pads que este encapsulado no saca son un error.** Y *este* encapsulado
depende del chip: el LQFP100 del `F407VG` tiene los puertos A–E más `PH0`/`PH1`;
el LQFP64 de un `F405RG` tiene A–C, **solo `PD2`** y `PH0`/`PH1`; el LQFP176 de
un `F407IG` llega hasta `PI11`. Conectar algo a un pad que no sale se rechaza
antes de simular —con el nombre del encapsulado en el mensaje— aunque el nodo
exista en el modelo, porque el silicio sí lo tiene y el plástico no.

**`une` sí cambia algo eléctrico, y es el único atributo que lo hace.** Un nodo
con `une` no se cuelga de los pads: **los sustituye**. Los pads que nombra dejan
de tener su propio punto eléctrico y pasan a compartir este, de modo que hay un
solo `AnalogNet` y la superposición los resuelve juntos. Es un puente de placa de
verdad: bidireccional, sin retardo, con la corriente repartida entre los dos, y
si los dos pines conducen a la vez en sentidos opuestos el conflicto **sale**
—media tensión en el nodo y sobrecorriente en los dos pads—.

```xml
<nodo id="n_puente" externo="si" une="PB9 PD3"/>
<componente tipo="Rpull" id="R1" v="3.3" r="10000">
  <pin nombre="a" nodo="n_puente"/>          <!-- o nodo="PB9": es el mismo punto -->
</componente>
```

Cuatro cosas que conviene saber antes de usarlo:

* **el nodo hay que declararlo**, al revés que los de pin. Es la única forma de
  decírselo al MCU **antes** de construirlo, y hay que hacerlo antes porque el
  pad ata su nodo a un `sc_port` en el constructor y un `sc_port` no se reata;
* `une` implica `externo="si"`: el nodo lo crea la placa, no un pad;
* el nombre del puente y el de cualquiera de sus pads designan el **mismo**
  punto, así que una pieza puede conectarse por cualquiera de los dos;
* **no se puede desoldar en marcha.** Un puente es permanente; `conectada="no"`
  no se aplica a un nodo. Para un enlace que haya que soldar y despegar entre
  pruebas —o que sea unidireccional, como una salida PWM hacia una entrada de
  captura— la pieza correcta es [`SignalLink`](#signallink), que es un buffer y
  no un cable. La comparación entre las dos está en
  `doc/multi_mcu.md`, §4.5.

Se rechazan antes de simular, cada uno con su mensaje: un nombre que no es un
pad, un pad que el encapsulado no saca, el mismo pad en dos puentes, y un `une`
con un solo pad.

#### Cómo se escribe el nombre de un pin

El modelo acepta **las cuatro formas** con las que los fabricantes nombran un
pin, y todas designan el mismo punto:

| | |
| :--- | :--- |
| `PD12` | letra de puerto, sin punto — la de ST, y la **canónica** aquí |
| `PD.12` | letra de puerto, con punto — muchos esquemáticos |
| `P3.12` | número de puerto, con punto — LPC, MSP430, 8051… |
| `P312` | la misma, escrita deprisa |

Con el prefijo del MCU delante funcionan igual: `u0.PD12`, `u0.P3.12`.

**El número de puerto es el índice, contado como las letras:** `P0` = `PA`,
`P1` = `PB`, … `P8` = `PI`. Es la única correspondencia que hace que las dos
formas nombren lo mismo.

**El punto está sobrecargado**, y de ahí la única regla que hay que saber: en
`u0.PD12` separa el chip del pad y en `P3.12` separa el puerto del pin. Se
distinguen mirando lo que hay detrás del último punto — si son solo dígitos, es
el del pin.

**La ambigüedad de la forma sin punto**: `P111` puede leerse `P1.11` o `P11.1`, y
**se elige siempre el puerto más pequeño**, `P1.11`. No es un desempate
caprichoso: los puertos bajos son los que existen en todos los chips, y la regla
es estable — si un modelo futuro tuviera más puertos, lo que hoy se lee `P1.11`
se seguirá leyendo igual.

Lo que **no** vale es escribir el mismo número de dos maneras: `PA05`, `P1.05` y
`P016` son errores, no formas alternativas de `PA5`, `PB5` y `PB6`.

**Se canoniza a la entrada**, así que da igual mezclar formas dentro de un
fichero: quien declara `<nodo id="P3.12"/>` y quien conecta `nodo="PD.12"` acaban
en el mismo nodo, y los volcados hablan siempre con una sola voz — `PD12`.

### 2.2 Los terminales: `<pin>`

```xml
<componente tipo="Led" id="LD4">
  <pin nombre="anodo" nodo="PD12"/>
</componente>
```

Un terminal se nombra, **nunca se cuenta**. `nombre` es el nombre nominal que la
pieza le da a esa patilla —`anodo`, `sda`, `mdio`, `d0`— y `nodo` es dónde va
soldada. El orden de los `<pin>` dentro de un componente da igual.

Tres reglas:

* **Los terminales que la pieza necesita y no están se resuelven a un nodo
  vacío**, y el montaje falla con un error de nodo desconocido. La tabla de cada
  componente dice cuáles son obligatorios.
* **Los terminales opcionales se omiten sin más.** Una tarjeta SD sin `dat1`,
  `dat2` y `dat3` funciona en modo de un hilo; una NAND sin `rb` no señala
  ocupado.
* **Los buses van indexados desde cero y sin huecos**: `d0`, `d1`, `d2`… La
  pieza cuenta hasta que falta uno, así que declarar `d0`, `d1` y `d3` da un bus
  de **dos** hilos, no de cuatro. Es el error fácil de este formato.

Un mismo nodo puede recibir varios terminales, de la misma pieza o de distintas.
Es como se modela un hilo compartido: dos pines al mismo nodo **están
cortocircuitados de verdad**, con la física resolviendo quién gana.

### 2.3 Las referencias: `<ref>`

No todo lo que une dos componentes es un nodo. Un transceptor CAN necesita el
**objeto** del hilo —para leer si el bus está dominante y a qué tensión—, no
solo el punto eléctrico:

```xml
<componente tipo="CanWire" id="bus1" r_term="120">
  <pin nombre="bus" nodo="n_can"/>
</componente>
<componente tipo="CanTransceiver" id="U2">
  <pin nombre="txd" nodo="PD1"/>
  <pin nombre="rxd" nodo="PD0"/>
  <pin nombre="bus" nodo="n_can"/>
  <ref nombre="hilo" componente="bus1"/>
</componente>
```

**El orden de declaración es semántico.** Los componentes se construyen en el
orden en que aparecen en el fichero, así que **lo referido va antes que quien lo
refiere**. Una referencia hacia delante se rechaza al validar, con ese mensaje.

### 2.4 Desoldar: `conectada`

```xml
<componente tipo="EthPhy" id="phy" conectada="no"> ... </componente>
```

`conectada="no"` construye la pieza y la deja **desoldada**: todos sus drivers
en alta impedancia, eléctricamente invisible. Es la forma de tener en el fichero
un montaje que hoy no está puesto —dos memorias que comparten el mismo chip
select, dos periféricos que se disputan un pin— sin borrarlo del netlist.

No es una comodidad, es una necesidad: **la elaboración de SystemC es estática**
y no se puede crear una pieza con la simulación en marcha, así que lo que no
está montado se construye igual y nace desconectado.

### 2.5 Varias placas enchufadas: `<sistema>`

Una Nucleo con un shield encima, o dos placas unidas por un cable, son un
**sistema**: un fichero que nombra las placas y dice cómo se enchufan. Las
placas no cambian —el fichero de una Nucleo es el mismo que se simula sola—,
y lo que las une se dice en el sistema, que es donde se sabe.

```xml
<sistema nombre="nucleo-y-shield">
  <placa id="N" fichero="nucleo_f446re.xml"/>
  <placa id="S" fichero="shield_leds.xml"/>
  <acopla a="N/CN5" b="S/J5"/>
  <acopla a="N/CN9" b="S/J9"/>
  <mcu ref="N/u0" firmware="verif/fw/blinky446/blinky446.bin"/>
</sistema>
```

| Elemento | Qué dice |
| :--- | :--- |
| `<placa id="A" fichero="x.xml"/>` | Una placa, de su fichero (relativo a la carpeta del sistema) |
| `<placa id="A" nombre="..."> ... </placa>` | O escrita dentro, con lo mismo que una `<placa>` suelta |
| `<acopla a="A/CN9" b="B/J9" [espejo="si"]/>` | Dos `Conector` (§4.1) enchufados: el pin *k* de uno con el *k* del otro; en espejo, con el que le cae enfrente |
| `<acopla conectores="A/J1 B/J1 C/J1"/>` | Una **pila** (PC/104, cabeceras apilables): el pin *k* es el mismo en todos. Un conector va en un acople solo |
| `<hilo a="A/CN9.2" b="B/PA3"/>` | Dos nodos cualesquiera, unidos: un cable, un cruce TX↔RX |
| `<mcu ref="A/u0" firmware= depuracion= puerto_gdb=/>` | Lo que el montaje decide de un chip de una placa, sin tocar su fichero |
| `<jumper ref="A/JP1" puente="..."/>` | Dónde va el puente de un `Jumper` de una placa en este montaje, sin tocar su fichero (`Jumper`, §4) |

**Todo lo de la placa `A` se llama `A/...`**: `A/LD2`, `A/u0`, `A/vcc`, el
pin de conector `A/CN9.2`. Dentro del fichero de la placa se escribe como
siempre, y **`PA5` es el pin del chip de ESA placa** (`A/u0.PA5`) aunque el
sistema lleve varios. La barra no puede aparecer en un nombre de la placa.

Lo demás funciona igual: con un solo MCU en todo el sistema, el firmware de la
línea de órdenes, `--gdb` y `--mcu` valen y mandan; con varios, cada chip lleva
lo suyo, aquí en `<mcu ref>`. `placas/nucleo_y_shield.xml` y
`placas/pila_pc104.xml` son los ejemplos, y el
porqué de cada decisión está en `doc/analisis_placas_conectadas.md`.

### 2.6 El dibujo de la placa: `ilustracion`

`mcu-sim-gui` enseña cada placa con un **dibujo SVG** —una aproximación
dibujada a mano, no una foto—, con los LEDs que se encienden y los botones que
se pulsan sobre él. El dibujo **viaja con la placa**: `mcu-sim` lo busca, y se
lo manda a la ventana en el saludo (`T_ILUSTRACION`), que puede estar en otra
máquina y no ver estos ficheros.

```xml
<placa nombre="nucleo-f446re" ilustracion="nucleo_f446re.svg">
  <ilustracion>
    <enlace pieza="LD2" elemento="led-verde" efecto="brillo"/>
  </ilustracion>
  ...
</placa>
```

| Qué | Cómo |
| :--- | :--- |
| `ilustracion="x.svg"` en la `<placa>` | El dibujo, relativo al fichero de la placa |
| Sin `ilustracion=` | El SVG que **se llame como la placa**, a su lado: `nucleo_f446re.xml` → `nucleo_f446re.svg`. Si no está, no pasa nada |
| `<placa id="N" fichero="..." ilustracion="y.svg"/>` en un `<sistema>` | El montaje cambia el dibujo de esa placa (relativo al sistema); la tabla de la placa, que era de SU dibujo, no se usa |
| `giro="90"` en la `<placa>`, o en su `<placa id>` de un `<sistema>` | El dibujo, **girado** 90, 180 o 270 grados en el sentido de las agujas del reloj: como está montada la placa. El del sistema manda sobre el de la placa. Ver abajo |
| `<ilustracion><enlace pieza= elemento= [efecto=] [observable=] [vector=]/></ilustracion>` | La **tabla de enlaces**: qué elemento del SVG es cada pieza, para dibujos que no se quieren tocar. Sin ella, cada pieza es el elemento con su mismo id (`id="LD2"`). `efecto` es `brillo`, `hundido`, `giro`, `pantalla`, `angulo`, `display`, `desplaza` o `ninguno`; con `giro` o `angulo`, `observable` dice cuál de los de la pieza lo mueve; con `display`, cuáles enseña, uno por fila; con `desplaza`, cuáles lo trasladan, y `vector` cuánto cada uno (abajo) |

**Lo que se comprueba aquí** —el SVG lo lee la ventana—: un dibujo
**declarado** que no está, uno que no empieza por `<svg` ni por `<?xml`, o uno
de más de **2 MiB**, son un **aviso** y no se mandan: la placa funciona igual.
Una `pieza` de la tabla que la placa no tiene, un `efecto` que no existe o una
pieza dos veces son **errores** de la placa, como cualquier referencia rota. Y
desde aquí la raíz `<placa>` **rechaza los atributos que no conoce** (antes los
ignoraba, y un `ilustarcion=` habría pasado sin que nadie lo viera).

La consola dice el de cada placa: `dibujo: nucleo_f446re.svg (49 kB)`, o en un
sistema `dibujo N: …` y `dibujo S: ninguno`. Cómo se dibuja un SVG para que la
ventana lo entienda —SVG 1.2 Tiny, tamaño en milímetros, un id por pieza viva—
está en `mcu-sim-gui`, `doc/analisis-uso-ilustraciones.md` §11; y
`placas/nucleo_f446re.svg` y `placas/discovery_min.svg` son los ejemplos.

**Lo que se toca en el dibujo es una pieza del XML.** Un elemento del SVG solo
es un mando si hay una pieza con su id: el RESET negro de la Nucleo no se
podía pulsar mientras la placa no tenía su `B2` —un `Button` en `NRST`, a
masa—, por mucho que estuviera dibujado. Con él, pulsarlo en la ventana tiene
el MCU en reset mientras se mantiene —el LED, apagado— y al soltarlo arranca
desde la flash, como en la tarjeta. La Discovery ya tenía su `B2`; le faltaba
un dibujo donde tocarlo.

**Lo que gira: el efecto `giro`.** Solo lo pone la tabla de enlaces. El
elemento gira sobre el centro de su caja con el primer numérico que la pieza
sugiere, tomado como posiciones enteras de `min` a `max` —una vuelta son
`max - min + 1`—. Es el anillo del mando de `placas/ky040.svg`, que sigue a la
`posicion` del `Encoder`: 12° por clic. Para que gire sobre el eje, el
elemento es un círculo centrado en él con todo lo demás dentro. Encima, la tapa
del pulsador, `SW1`, con `hundido`: el clic en la tapa la aprieta, y la rueda
del ratón **atraviesa** la tapa —un botón no tiene nada que girar— y gira el
encoder.

**Lo que gira un ángulo: el efecto `angulo`.** Es el de omisión de una pieza
que sugiere un numérico **en grados** —unidad `°`—, y lo puede pedir también
la tabla. El elemento gira sobre el centro de su caja tantos grados como diga
ese numérico, a la derecha los positivos, sin vueltas ni posiciones: es el
aspa del `Servo` (§4.12), que en `placas/servo_sg90.svg` está dibujada en el
0 con la pala de la marca hacia arriba. Ese numérico no lleva etiqueta: ya lo
dice el giro. Como con `giro`, para que gire sobre el eje el elemento lleva
dentro un círculo centrado en él —sin pintar, `fill="none"`— que alcanza más
que todo lo demás.

**Cuál lo mueve: `observable=`.** Con `giro` y con `angulo`, la tabla puede
nombrar el observable que mueve el elemento cuando no es el que se elegiría
solo —el primero que la pieza sugiere, o el primero en grados—. Es la aguja de
rumbo de `placas/stm32f3discovery.svg`: la `Postura` tiene tres ángulos, el
rumbo es el tercero y se mide en «grados», y su enlace es
`<enlace pieza="MOV" elemento="MOV" efecto="angulo" observable="rumbo"/>`. Con
otro efecto, el fichero no valida; un nombre que la pieza no tiene, la ventana
lo dice y deja el elemento quieto.

**Lo que se ve en una pantalla: el efecto `pantalla`.** Es el de omisión de
una pieza que enseña una IMAGEN —el `Tft128x160`—: la ventana pinta la imagen
encima de su elemento, llenando su caja, con su luz. Si el elemento es
apaisado y la imagen no —o al revés—, la pone girada un cuarto de vuelta a la
izquierda: la fila de arriba de la imagen queda a la izquierda, y su columna
izquierda, abajo. `placas/tft_128x160.svg` lleva el vidrio apaisado, con el
conector a la derecha; un firmware que quiera verlo derecho en esa postura
pone MADCTL = 0x60.

**Lo que se lee en un display: el efecto `display`.** Solo lo pone la tabla
de enlaces. La ventana escribe DENTRO del elemento el valor de los numéricos
que diga `observable=` —varios, separados por blancos, uno por fila, de arriba
abajo— o, sin él, los que la pieza sugiere, con sus cifras de colores sobre el
fondo que traiga el dibujo, como el display de un aparato: cuatro cifras, y
los mA que pasan de 1000, en A —`0.367 A`, `7.474 V`—. Esos ya no llevan
etiqueta debajo. Es el display de `placas/fuente_laboratorio.svg`:
`<enlace pieza="PSU" elemento="PSU" efecto="display" observable="tension
corriente"/>`. Para que las filas caigan en sus ventanitas, el elemento las
lleva todas, del mismo alto y repartidas por igual.

**Lo que se mueve en línea recta: el efecto `desplaza`.** Es el `angulo`
lineal, y solo lo pone la tabla: la ventana TRASLADA el elemento. Lo mueven
uno o varios numéricos de la pieza —`observable=`, separados por blancos—,
cada uno con su vector —`vector=`, dos números por observable, x e y—, en
unidades del dibujo (las de su `viewBox`) por unidad del observable, y se
suman:

    <enlace pieza="MAQ" elemento="cabezal" efecto="desplaza"
            observable="x z" vector="2.5 0  0 -2.5"/>

lleva el cabezal 2,5 a la derecha por cada mm de `x` y 2,5 hacia arriba por
cada mm de `z`. Así un elemento se mueve con dos ejes sin que la ventana tenga
que componer grupos anidados, que es lo que haría falta para que el carro Z
de una fresadora se moviera con el X (`doc/analisis_cnc.md` §9). `mcu-sim`
comprueba que los dos atributos estén y que el vector tenga dos números por
observable; que la pieza tenga esos observables lo mira la ventana, que se
queda sin efecto si no, y lo dice. Con el elemento van su halo y su contorno
de alarma; los numéricos que lo mueven no llevan etiqueta.

**El giro: la misma placa, montada de otra manera.** Una placa no siempre va
derecha: la pantalla TFT puede ir de pie, con el conector abajo, o del revés,
con el conector a la izquierda. En vez de un dibujo por postura, `giro=` gira
el que hay: `mcu-sim` cambia el viewBox y el tamaño de la raíz y mete todo lo
de dentro en un `<g transform="...">` (`gira_svg` en
`common/svg_variantes.h`). Los ids no cambian, la ventana encuentra cada pieza
donde ha quedado, y lo que va encima de una pieza —el brillo de un LED, la
imagen de una pantalla— gira con ella. La serigrafía también gira, como en la
placa de verdad. La consola lo dice: `dibujo T: tft_128x160.svg (6 kB, girado
90)`. Hace falta que la raíz del SVG tenga viewBox; si no, se avisa y se manda
como está. Un giro que no sea 0, 90, 180 o 270 es un error de la placa.

```xml
<sistema nombre="nucleo-y-tft-de-pie">
  <placa id="N" fichero="nucleo_f446re.xml"/>
  <placa id="T" fichero="tft_128x160.xml" giro="90"/>   <!-- el conector abajo -->
  ...
</sistema>
```

**Las variantes: lo que depende de cómo está montada la placa.** El puente de
un `Jumper` no cambia con la simulación, pero sí de una placa a otra, y el
dibujo tiene que enseñarlo donde está. El SVG lleva **todas** las posiciones,
cada una con un id `PIEZA@valor` —`JP1@5V-VCC`, `JP1@VCC-3.3V`—, y `mcu-sim`,
antes de mandarlo, deja solo la de la placa y quita las demás con todo lo que
llevan dentro (`common/svg_variantes.h`). La ventana recibe un SVG normal y no
sabe nada de esto. La consola lo dice: `dibujo: ftdi_ft232rl.svg (10 kB, JP1
5V-VCC)`. Dos placas con el mismo dibujo y el jumper distinto reciben dos
dibujos. Un detalle de autor: la ventana pinta cada pieza con lo que lleva
**dentro**, así que las variantes de `JP1` van dentro del elemento `JP1`; fuera,
quedarían debajo de él.

Una pieza puede tener **varias** variantes, cada una con un nombre detrás del
id: las del `Servo` son `SERVO.cuerpo@azul` y `SERVO.cuerpo@negro` —su
`color`— y `SERVO.aspa@una` … `SERVO.aspa@disco` —su `aspa`—. Las del aspa van
dentro del elemento `SERVO`, que es lo que gira; las del cuerpo, fuera, debajo.
La consola las dice todas: `dibujo S: servo_sg90.svg (16 kB, SERVO.aspa
cuatro, SERVO.cuerpo azul)`.

**Los rótulos: lo que solo se sabe al lanzar.** Un texto del dibujo con id
`PIEZA#campo` y nada más dentro se queda con el texto que le toca antes de
mandarlo, y lo que lleva escrito es lo que se ve si nadie lo cambia. Hoy los
tiene un `PuenteSerie`: `VCP#destino` es su destino contado a una persona
—`RFC 2217 en localhost:3355`— y `VCP#host`, como se escribe —`rfc2217:3355`—,
los dos ya con lo que diga `--serie` y con el puerto al que se haya corrido.
`placas/nucleo_f446re.svg` lo pone junto al ST-LINK: es el VCP, la USART2 por
el mismo USB que la depuración. Dos placas con el mismo dibujo y rótulos
distintos reciben dos dibujos.

**Lo que no interesa ver: `visible="no"`.** La ventana enseña en la
ilustración todas las piezas que dejan ver o tocar algo. Las que el dibujo no
trae no se pierden: van a una **bandeja**, «sin dibujar», al lado de la placa,
con sus medidores de siempre. Pero a veces son piezas que no interesan —la
`Fuente` que hace de regulador interno del FT232RL, la `Gnd` de un módulo— y
la bandeja solo ocupa sitio. `visible="no"` en su `<componente>` lo dice:

```xml
<componente tipo="Fuente" id="REG_3V3" v="3.3" limite_ma="50" visible="no">
  <pin nombre="pin" nodo="JP1.3.3V"/>
</componente>
```

La pieza no va a la bandeja ni al dibujo que la ventana genera para una placa
sin SVG, y el informe de la placa la cuenta aparte, como «oculta». Lo que el
SVG dibuje se queda: el dibujo es de quien lo hace. El **panel** las sigue
enseñando todas, también sus alarmas. Para la simulación no cambia nada: va
en `T_PLACA` con la pieza —también en un sistema— y nadie más lo mira. Sin el
atributo, todo es como siempre, bandeja incluida. Solo `si` o `no`; otra cosa
es un error.

**La disposición en la ventana: dónde va cada placa.** En la ilustración de
`mcu-sim-gui`, las placas de un sistema se ponen en fila si nadie dice otra
cosa, y en el modo *Edición* se mueven, se giran, se escalan, se cambia el
lienzo y las líneas se llevan en tramos rectos. Eso se puede guardar en el
propio XML —a mano, o con «Guardar en el XML» de la ventana—, y entonces viaja
con el sistema: `mcu-sim` lo lee, lo comprueba y lo manda en `T_PLACA`. Para la
simulación no significa nada: no cambia ni un nodo.

```xml
<sistema nombre="nucleo-f446re-servo" lienzo="-10 -10 330 200">
  <placa id="N" fichero="nucleo_f446re.xml" x="0" y="0"/>
  <placa id="S" fichero="servo_sg90.xml" x="140" y="0" escala="1.5"/>
  <placa id="T" fichero="tft_128x160.xml" giro="90" x="200" y="2"/>
  <hilo a="S/P1.PWM" b="N/CN9.6"/>
  ...
  <ruta linea="hilo S/P1.PWM N/CN9.6" eje="v" codos="40 12.5"/>
</sistema>
```

| Qué | Dónde | Cómo |
| :--- | :--- | :--- |
| `x="..." y="..."` | En la `<placa id>` de un `<sistema>` —de fichero o escrita dentro— | La esquina de arriba a la izquierda del dibujo de la placa, ya girado, en mm. Van juntos. Una placa sin ellos la coloca la ventana |
| `escala="..."` | En la `<placa id>`, o en la raíz de una placa suelta | El tamaño del dibujo, de **0.25 a 4**; 1 es el de siempre |
| `lienzo="x y ancho alto"` | En el `<sistema>`, o en la raíz de una placa suelta | Lo que se ve de la ilustración, en mm. Sin él, el que haga falta para que quepa todo |
| `<ruta linea="..." eje="h\|v" codos="..."/>` | Dentro del `<sistema>` | Una **línea en tramos rectos**. `linea` es `hilo A/P1.X B/CN9.6` —sus dos extremos, como en el `<hilo>`— o `acople A/J1 B/J1`; `eje`, el del primer tramo (`h` si no se dice); `codos`, en mm, dónde gira: una x y una y alternadas, empezando por la del eje. Los dos últimos tramos los pone la ventana |

El `giro=` es el de siempre: el dibujo ya llega girado. La ventana manda sobre
el XML: lo que se haya colocado a mano y guardado en su configuración se ve
antes que esto, y su «Restablecer» vuelve a lo que dice el XML.

**Lo que no vale es un error del fichero**, que dice por qué: `x=` e `y=` en
la raíz de una placa —«son la posición de la placa en la ventana, y solo se
dicen en su `<placa id>` de un sistema»—; uno sin el otro; algo que no es un
número; una `escala` fuera de 0.25 a 4; un `lienzo` sin cuatro números o sin
tamaño; y una `<ruta>` de una línea que no hay —el error dice las que hay—,
repetida, con un `eje` que no es `h` ni `v` o con unos `codos` que no son
números.

La consola lo cuenta después de los dibujos, si hay algo que contar:
`ventana: lienzo 330 x 200 mm; N en (0, 0) mm; S en (140, 0) mm al 150 %; T en
(200, 2) mm; 1 linea(s) en tramos rectos`. Y `T_PLACA` lo lleva como lo dice el
fichero —`x`, `y` y `escala` en cada `<placa id>`, `lienzo` en la raíz y las
`<ruta>` detrás de los acoples y los hilos—, añadido a la versión 2 del
protocolo sin subirla: una ventana que no lo conoce no lo lee.
`placas/nucleo_f446re_servo.xml` lo usa.

---

## 3. Parámetros comunes

### 3.1 Los tres de todos los componentes

| Parámetro | Obligatorio | Qué hace |
| :--- | :--- | :--- |
| `tipo` | sí | El nombre de clase que la factoría busca. Distingue mayúsculas: `Led`, no `LED`. Un tipo que no conoce se rechaza con la lista de los que sí |
| `id` | sí | Identificador único de instancia. Es el nombre por el que otros componentes lo referencian, y el que sale en los avisos de validación |
| `conectada` | no (`si`) | `no` = se construye desoldada (§2.4) |
| `visible` | no (`si`) | `no` = la ventana no la enseña en la ilustración (§2.6). La simulación no lo mira |

### 3.2 `vdd` — la tensión de la lógica de la pieza

La admiten desde el XML **`Led`**, **`Crystal`**, **`Driver`**, **`CanWire`** y
**`CanTransceiver`**, y en las cinco vale **3.3** por omisión.

No es la alimentación del MCU: es la tensión del **otro extremo** de la pieza,
el que no toca el pin — con la que conduce su nivel alto y contra la que compara
los niveles que recibe. **No tiene por qué ser la del microcontrolador.** Un LED
azul de 3,0 V no luce con 3,3 V y en una placa real se cuelga de los 5 V; un
componente de 1,8 V soldado al mismo pin produce la caída y el nivel
indeterminado que corresponde. Eso es lo que `vdd` modela.

> Nota: `ExtClock`, `SignalLink`, `I2cWire`, `I2cEeprom`, `I2cExtMaster`,
> `SwoReceiver`, `SdCard` y los aparejos de USB **tienen `vdd` en su constructor
> de C++ pero la factoría no lo expone**: desde el XML se quedan en 3.3 V.

### 3.3 Las resistencias

Todas en ohmios y todas con el mismo significado: la `Rout` del equivalente
Thevenin con el que la pieza gobierna el nodo. Cuanto menor, más «fuerte».

| Parámetro | Dónde | Qué es |
| :--- | :--- | :--- |
| `r` | `Led`, `Rpull` | La resistencia en serie del LED, que fija su corriente; el valor de la rama resistiva |
| `r_out` | `Driver` | Impedancia de salida del driver |
| `r_cerrado` | `Button` | Resistencia del contacto cerrado |
| `r_pull` | `I2cWire` | Pull-up del bus I2C |
| `r_term` | `CanWire` | Terminador del bus CAN |

De referencia: una salida push-pull ronda los 25–50 Ω, un contacto cerrado unos
10 Ω, un pull-up 4,7 kΩ y la alta impedancia 10¹² Ω.

### 3.4 Cómo se escriben los valores

**Los números no admiten sufijos de unidad.** Se leen con `atof`, así que valen
`330`, `4700`, `0.5`, `500e3`, `1e6` y también el hexadecimal `0x50`. **No**
valen `8M`, `4k7` ni `4.7k`: se leen hasta el primer carácter que no encaja, de
modo que `8M` es **8** y `4.7k` es **4.7**. Es un fallo silencioso, y de los
malos: la placa se monta sin quejarse y el cristal oscila a 8 Hz. Escríbase
`8e6`.

**Los booleanos** se escriben `si` o `no`. En los parámetros de pieza también se
aceptan `1` y `true`, y cualquier otra cosa cuenta como falso. En `conectada`,
`visible`, `externo` y `bus` el lector es estricto: solo `si` o `no`, y otra cosa es un
error con su línea.

**Un parámetro que la pieza no mira se ignora en silencio.** Todo atributo de
`<componente>` que no sea `tipo`, `id`, `conectada` o `visible` se guarda como
parámetro,
sin lista blanca, y cada pieza consulta los suyos. La consecuencia es que
`vff="2.0"` no da error: se guarda, no lo lee nadie y el LED usa 2.0 V porque es
su valor por omisión. Al escribir una placa conviene comparar con la tabla del
componente.

---

## 4. El catálogo

> `./build/mcu-sim --help TIPO` imprime la ficha de cualquiera de estas piezas sin
> salir de la consola: qué hace, sus terminales, sus atributos con el valor por
> omisión de cada uno y un `<componente>` de ejemplo que copiar. Para
> **preguntar** da igual cómo se escriba el nombre —`--help led` vale—; para
> **describir una placa** no, que ahí `tipo="led"` sigue siendo un error.

### 4.1 Piezas pasivas y de estímulo

#### `Led`

Diodo con su resistencia en serie. **No es lineal**: mientras la tensión
aplicada no supera `vf` no conduce, y se modela con dos estados —conduciendo,
con equivalente `{vf, r}`, o en corte, en alta impedancia— reevaluados cada vez
que cambia la tensión del pin.

| Terminal | | |
| :--- | :--- | :--- |
| `anodo` *o* `catodo` | uno de los dos, **o los dos** | Con uno, la patilla que va soldada al pin, y el otro extremo va por dentro a masa o a `vdd`. Son **el mismo terminal con dos nombres**: el que se escriba no cambia la física —eso lo decide `a_vss`—, pero permite que el fichero diga la verdad. Con `a_vss="si"` lo que toca el pin es el ánodo; con `a_vss="no"`, el cátodo. Con **los dos**, el LED tiene las dos patillas a la vista (abajo) |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `a_vss` | `si` | El montaje. `si`: ánodo al pin y cátodo a masa, **luce con el pin alto**, y conduce cuando V > `vf`. `no`: ánodo a `vdd` y cátodo al pin, **luce con el pin bajo** (el montaje de las placas Discovery y Nucleo), y conduce cuando V < `vdd` − `vf` |
| `vdd` | `3.3` | Tensión del extremo que **no** toca el pin. **Solo interviene con `a_vss="no"`**; con el ánodo al pin ese extremo es masa y `vdd` no se usa. Es lo que permite colgar el LED de una alimentación distinta de la del MCU |
| `vf` | `2.0` | Tensión directa del diodo, en voltios. Un LED rojo ronda 1,8; uno azul o blanco, 3,0 |
| `r` | `330` | Resistencia en serie, en ohmios. Es quien fija la corriente: con `vf`=2,0 y `r`=330 salen unos 3,4 mA |

**El caso del LED azul.** Con `vf`=3,0 sobre 3,3 V no queda margen para la
resistencia: el LED apenas luce, y es un problema real de placa, no del modelo.
La solución de siempre es colgarlo de los 5 V con el cátodo al pin, de modo que
**el MCU lo enciende poniendo el pin a cero**:

```xml
<componente tipo="Led" id="LD_AZUL" a_vss="no" vdd="5.0" vf="3.0" r="220">
  <pin nombre="catodo" nodo="PD12"/>
</componente>
```

Conduce cuando el pin baja de 5,0 − 3,0 = 2,0 V, y entonces gobierna el nodo con
`{2,0 V, 220 Ω}`. Con el pin a cero salen **7,3 mA a 0,40 V**; con el pin alto,
apagado. Compárese con el mismo montaje en verde a 3,3 V —`{1,3 V, 330 Ω}`—, que
da 3,4 mA: la alimentación más alta no solo enciende el LED, también le pasa más
corriente, y por eso la resistencia baja de 330 a 220 Ω.

> **Cuidado con lo que este modelo NO dice.** Un LED en corte queda en alta
> impedancia, así que con `vdd="5.0"` la pieza **nunca presenta 5 V en el pin**.
> En una placa real, con ese pin configurado como entrada y sin nada que lo
> sujete, el nodo se iría cerca de los 5 V — por encima del máximo absoluto de un
> pad que no sea tolerante a 5 V. El modelo no avisará de eso; hay que saberlo.

**Las dos patillas a la vista.** Con `<pin nombre="anodo">` **y**
`<pin nombre="catodo">`, ningún extremo va por dentro a masa ni a VDD: el
ánodo está en un nodo, el cátodo en otro, y lo que haya en cada uno lo dice la
placa. Luce cuando la tensión del ánodo menos la del cátodo supera `vf`, con
`r` en serie. Es el LED de una **barra de ánodo o cátodo común**, cuyo común
sale por un pin del conector y puede acabar en VDD, en masa o en el pin de otra
placa:

```xml
<nodo id="P1.COM" bus="si"/>                    <!-- el comun: 8 LEDs a la vez -->
<componente tipo="Led" id="D1" vf="1.8" r="2000">
  <pin nombre="anodo"  nodo="P1.COM"/>          <!-- anodo comun -->
  <pin nombre="catodo" nodo="P1.D1"/>
</componente>
```

`a_vss` y `vdd` no tienen sentido ahí, y escribirlos es un error que lo dice.
Cómo se resuelve una rama entre dos nodos con un canal que resuelve cada nodo
por separado: lo que no es el LED es, en cada nodo, un Thevenin `{V0, R0}`
—`voltage_excluding()` y la conductancia sin la propia, como hace `Fuente`—, y
la rama es un circuito de una malla, `I = (Va0 − Vk0 − vf) / (Ra0 + r + Rk0)`.
A cada nodo se le pone el Thevenin de la rama visto desde él —`{Vk + vf, r}` en
el ánodo, `{Va − vf, r}` en el cátodo—, y cada uno, resuelto por su cuenta, da
exactamente `Va` y `Vk`.

Dos cosas que conviene saber:

* **un extremo que nadie más sujeta deja el LED apagado**: con el común al
  aire no hay por dónde cerrar el circuito, como en la placa. `sim` lo dice
  al terminar: `LED D4 entre P1.COM y P1.D4: apagado (un extremo al aire)`;
* **un nodo al que solo llegan LEDs** —el común de una barra unido al de otra
  y a nada más— se queda a oscuras aunque en la placa real luciera: cada LED
  ve el común flotando sin los demás, y ninguno empieza.

El informe final dice entre qué nodos está cada uno y la tensión entre sus
patillas: `LED B/D2 entre N/u0.PB6 y N/u0.PA9: encendido (3.22 V, 0.71 mA)`.
`placas/barra8_*.xml` son las cuatro barras de ocho LEDs —ánodo o cátodo
común, rojas o azules—; `placas/barra8_en_nucleo.xml`, una cableada a la
Nucleo con el común en un pin; y `placas/nucleo_f446re_barra8ac_azul.xml`,
la azul de ánodo común por los morpho, con el común a VDD.

El estado se consulta desde C++ con `on()` y `current()`, y `sim` lo imprime al
terminar:

```
  LED LD4 en PD12: encendido  (3.11 V, 3.38 mA)
```

Y el montaje clásico, para comparar:

```xml
<componente tipo="Led" id="LD4" a_vss="si" vf="2.0" r="330">
  <pin nombre="anodo" nodo="PD12"/>
</componente>
```

#### `RGBLed`

**Tres LED —rojo, verde y azul— en un solo encapsulado transparente**, cada uno
con su resistencia en serie: el LED RGB de la LED2 de la MSP-EXP432P401R o el
D1 de la BOOSTXL-EDUMKII. Por dentro son tres `Led` (arriba), con todo lo que
eso supone —no lineales, `{vf, r}` conduciendo y alta impedancia en corte—,
que comparten una patilla común. Lo que la pieza añade es **verse como una
sola**: en la ventana los colores encendidos se mezclan en un único halo —rojo
y verde dan amarillo; los tres, **blanco**— y el informe del final dice el
color que se ve.

| Terminal | | |
| :--- | :--- | :--- |
| `r`, `g`, `b` | al menos uno | La patilla de cada color, al pin o al nodo que la gobierne. Un color sin patilla no se monta |
| `comun` | opcional | La patilla común —el cátodo común o el ánodo común— a la vista, en un nodo. Sin ella va por dentro a masa (`comun="catodo"`) o a `vdd` (`comun="anodo"`) |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `comun` | `catodo` | `catodo`: cada color luce con su patilla **alta**. `anodo`: luce con su patilla **baja**, como un `Led` con `a_vss="no"` |
| `vdd` | `3.3` | La tensión del ánodo común, solo con `comun="anodo"` y la patilla común por dentro. Con la patilla `comun` a la vista es un error: la tensión la pone ese nodo |
| `vf_r`, `vf_g`, `vf_b` | `2.0`, `3.0`, `3.0` | La tensión directa de cada color |
| `r_r`, `r_g`, `r_b` | `330` | La resistencia en serie de cada color |

```xml
<!-- La LED2 de la MSP-EXP432P401R, tras sus puentes JP9 a JP11 -->
<componente tipo="RGBLed" id="LED2" comun="catodo" vf_r="2.0" vf_g="2.9" vf_b="2.9"
            r_r="110" r_g="16" r_b="24">
  <pin nombre="r" nodo="JP9.2"/>
  <pin nombre="g" nodo="JP10.2"/>
  <pin nombre="b" nodo="JP11.2"/>
</componente>
```

**Observables**, por este orden: `encendido` (0/1, alguno de los tres; va el
primero para que una ventana que no sepa de canales lo pinte como un `Led`);
`rojo`, `verde` y `azul` (0/1, los que sugiere pintar); y `corriente_r`,
`corriente_g` y `corriente_b` (mA, de 0 a 25). Los seis últimos llevan en el
catálogo **`color="#rrggbb"`**: un 0/1 y una corriente del mismo color forman
un **canal**, y la ventana suma los canales encendidos, cada uno con su brillo,
y lleva la suma al tope —por eso rojo + verde + azul es blanco y no gris—. La
pieza no dice cómo mezclar: la ventana no sabe qué es un `RGBLed`, solo que
hay canales.

**Dónde va.** Una placa que lleva un LED RGB de verdad —un único encapsulado—
lo declara con un `RGBLed`, no con tres `Led`. Tres LED de colores sueltos
—las LD3 a LD6 de la Discovery, los D1 a D4 de la MSP-EXP432E401Y— siguen
siendo `Led`: no se mezclan, que están separados.

#### `Button`

Pulsador a masa. **Sin pulsar deja el pin abierto**, así que el nivel alto tiene
que darlo alguien: el pull-up interno del MCU o un `Rpull` externo. Sin
ninguno de los dos, el pin queda indeterminado — que es lo que pasa en una placa
real y lo que el modelo reproduce.

| Terminal | | |
| :--- | :--- | :--- |
| `pin` | obligatorio | El pin del pulsador |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `r_cerrado` | `10` | Resistencia del contacto cerrado, en ohmios. Pulsado, la pieza gobierna el nodo con `{v_cerrado, r_cerrado}` |
| `v_cerrado` | `0` | **La tensión a la que lleva el pin al cerrarse.** Cero es el pulsador a masa de siempre; `3.3` es el pulsador a VDD |
| `normalmente` | `abierto` | El **reposo del contacto**: `abierto` (suelto no conduce, pulsado conduce) o `cerrado` (suelto CONDUCE, y pulsarlo lo ABRE). Cualquier otra palabra es un error, no un `abierto` silencioso |
| `rebote` | `2` | **Los rebotes del contacto**: lo que tarda como mucho, en ms, en quedarse quieto al cerrarse; al abrirse, la mitad. `0` o `no` es un contacto ideal, que cambia de una vez |
| `rebotes` | `5` | **Cuántas veces** se separa y vuelve a tocar en cada rebote: exactamente esas, en instantes al azar. Con N rebotes, una EXTI por flanco de subida ve N+1 flancos al pulsar. De 1 a 1000; para no rebotar, `rebote="no"` |
| `semilla` | `0` | La del patrón pseudoaleatorio de los rebotes. `0` la saca del id: dos pulsadores de la misma placa no rebotan igual, y el mismo rebota igual en todas las ejecuciones |

**`normalmente="cerrado"` no es una rareza: es lo que hay en seguridad.** Un
final de carrera, una seta de emergencia o un detector de puerta se cablean NC a
propósito, para que **un cable cortado se vea igual que una pulsación** y la
máquina pare. Descrito como NA, el montaje parece funcionar hasta el día en que
se corta el cable — que es justo el día que importa.

Lo que conduce, entonces, no es «pulsado» sino **«pulsado XOR normalmente
cerrado»**:

| `normalmente` | suelto | pulsado | desoldado |
| :--- | :---: | :---: | :---: |
| `abierto` (omisión) | abierto | **cierra** | abierto |
| `cerrado` | **cierra** | abre | abierto |

La última columna es la misma para los dos, y es lo razonable: **una pieza que
no está no cierra ningún contacto.** Al volver a soldarla recupera su reposo, que
en un NC es conduciendo.

Desde C++, `pressed()` es lo que hace el **dedo** y `cerrado()` lo que hace el
**contacto**. En un NC son opuestos, y confundirlos es el error fácil.

**No todos los pulsadores van a masa, y la diferencia se nota en el firmware.**
En la STM32F4-Discovery el botón de usuario lleva PA0 **a VDD** y es una
resistencia externa la que lo sujeta abajo; por eso el código que STM32CubeIDE
genera para esa placa configura PA0 como EXTI por flanco de **subida** y sin
pull interno. Descrito con un pulsador a masa, ese flanco no llegaría nunca y el
firmware parecería roto sin estarlo:

```xml
<nodo id="PA0" bus="si"/>              <!-- los dos conducen al pulsar -->
<componente tipo="Button" id="B1" v_cerrado="3.3" r_cerrado="10">
  <pin nombre="pin" nodo="PA0"/>
</componente>
<componente tipo="Rpull" id="R35" v="0" r="100000">
  <pin nombre="a" nodo="PA0"/>
</componente>
```

Se acciona desde C++ con `press()` y `release()`. **Un pulsador
`conectada="no"` no cierra aunque se le pulse.**

**Rebota, como uno de verdad.** Un contacto mecánico golpea y rebota antes de
quedarse cerrado, y un firmware que cuente flancos de EXTI sin filtrarlos cuenta
varias pulsaciones donde hubo una — igual que en la placa. Por eso una placa en
XML rebota **por omisión**, con 2 ms y 5 rebotes, que es el orden de lo
que se mide en pulsadores reales (Ganssle, *A Guide to Debouncing*):

- el contacto se mueve **en el acto**, con el primer golpe, y luego se separa y
  vuelve a tocar **exactamente `rebotes` veces**, en instantes al azar dentro
  de `rebote` ms, hasta quedarse donde el dedo quiere. Exactas y no «como
  mucho» —que es como fue al principio— porque para enseñar y para depurar lo
  que sirve es poder decir «con 3 rebotes, la EXTI ve 4 flancos»;
- al soltar, igual, en la mitad de tiempo;
- un movimiento del dedo a media rebote corta el que había: manda el último;
- en un NC rebota igual, porque lo que rebota es la lámina, no la lógica;
- el **observable `pulsado` es el dedo**, que no rebota: la ventana pinta el
  botón hundido, no el contacto, que además cambia mucho más deprisa de lo que
  la ventana muestrea.

Y se puede cambiar **con la simulación en marcha**: el pulsador tiene tres
mandos, `pulsar` (botón), **`rebote_ms`** (continuo, de 0 a 20 ms, o hasta lo
que diga la placa si es más) y **`rebotes`** (discreto —un desplegable en la
ventana—, de 1 a 9, o hasta lo que diga la placa), que valen desde el siguiente
movimiento del dedo. Desde C++, `pon_rebote_ms()` y `pon_rebotes()`.

El patrón es pseudoaleatorio pero **reproducible al picosegundo**: un generador
propio (xorshift64\*) con semilla sacada del id —o de `semilla`— e instantes en
ns enteros, sin `<random>` ni `double`, cuyas distribuciones cambian de una
biblioteca a otra. Un pulsador sin rebote no crea ningún proceso: se simula
exactamente como antes.

**Los bancos de pruebas no rebotan**: `pulsador()`, la función con la que se
montan desde C++, pone `rebote="no"`, porque cuentan flancos exactos desde mucho
antes de que hubiera rebotes. Y el botón de RESET de la Discovery y el de la
Nucleo tampoco: en las dos tarjetas, el condensador de NRST se come los
rebotes, y como ese condensador no se modela, su B2 lleva `rebote="no"`.

#### `Fuente` y `Gnd`

Una **tensión fija en un nodo**, con un límite de corriente opcional. `Fuente`
pone `v` voltios (3,3 si no se dice) y `Gnd` pone 0 V: son la misma pieza —un
equivalente Thevenin `{v, r}` con `r` mínima— y solo cambian la tensión y el
sentido en que cuentan la corriente: la de una `Fuente` es la que **entrega** al
nodo; la de una `Gnd`, la que **recibe** de él. Son lo que hace falta para montar
una placa sin MCU —un raíl, una masa— y lo que deja ver en la ventana cuánto
tira lo que cuelga de ellas.

| Terminal | | |
| :--- | :--- | :--- |
| `pin` | obligatorio | El nodo que sostiene |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `v` | `3.3` | Solo `Fuente`: la tensión, en voltios |
| `limite_ma` | `no` | **El límite de corriente**, en mA, o `no` para ninguno |
| `r` | `0.1` | La resistencia interna, en ohmios. 0,1 Ω es lo menos que admite un nodo: una fuente casi ideal |
| `media_ms` | `0` | Solo en `Fuente`. Con más de 0, la `corriente` que deja ver es su **media** en esos ms —un filtro de primer orden—, como el display de una fuente de laboratorio, que no salta con el PWM de la carga; y deja ver también la `tension`, de media. Con 0, la de cada instante |

**El límite se comporta como el de una fuente de laboratorio.** Por debajo,
tensión constante. Si la carga pide más, la pieza pasa a **corriente
constante**: entrega exactamente el límite y deja caer la tensión —o, en una
`Gnd`, deja subir el nodo—. Al entrar en limitación pone su observable
`sobrecorriente` a 1 y lo avisa una vez por el informe de SystemC (un `T_AVISO`
en la ventana); al salir, vuelve sola a tensión constante. El cálculo no itera a
ciegas: lee el resto del nodo como otro Thevenin `{Vx, Rx}` y se pone la
resistencia que deja la corriente en el límite, `|v − Vx| / I_lim − Rx`.
Con una **carga inductiva** —un motor que arranca y tira de 40 A de golpe— la
cuenta no basta: la bobina sujeta su corriente, y lo de fuera ya no se comporta
como un Thevenin. Una `Fuente` no deja su nodo por debajo de 0 V: si lo que
haría falta para el límite lo hundiría, se pone la resistencia que deja pasar
la corriente que la carga sujeta con el nodo a 0 V, `v / I`, y las dos partes
están de acuerdo a la primera, sin oscilar.

**Un nodo con una fuente y varias cargas es un raíl, no un cortocircuito**: la
validación eléctrica no cuenta la fuente como otra pieza conduciendo, así que no
hace falta `bus="si"`. Dos fuentes —o una fuente y una masa— en el mismo nodo, en
cambio, se pelean por él, y se avisa.

```xml
<placa nombre="rail-limitado">
  <nodo id="vcc"/>
  <componente tipo="Fuente" id="F1" v="3.3" limite_ma="20">
    <pin nombre="pin" nodo="vcc"/>
  </componente>
  <componente tipo="Led" id="LD1" a_vss="si" vf="2.0" r="330">
    <pin nombre="anodo" nodo="vcc"/>
  </componente>
  <componente tipo="Button" id="CORTO" v_cerrado="0" r_cerrado="1" rebote="no">
    <pin nombre="pin" nodo="vcc"/>
  </componente>
</placa>
```

Suelto, F1 entrega los 3,9 mA del LED. Con `CORTO` pulsado la carga pediría
3,3 A: F1 da sus **20 mA justos**, el nodo cae a unos 20 mV, el LED se apaga y
`sobrecorriente` se pone a 1. Al soltarlo, todo vuelve. Al final de la
simulación, `mcu-sim` dice por consola cuánto entregó cada una y cuántas veces
tuvo que limitar.

Desde C++, `corriente()` en amperios, `sobrecorriente()` y `episodios()`, las
veces que ha entrado en limitación.

**El +5V de la Nucleo es una.** `placas/nucleo_f446re.xml` lleva la `Fuente`
`USB`: los 5 V del cable del ST-LINK, que pasan por su interruptor de potencia
—corta a unos 500 mA, `limite_ma="500"`— y salen como `+5V` (CN6.5 y CN7.18)
y `U5V` (CN10.8). Es de donde toma su tensión un módulo de 5 V, como el servo
de `placas/nucleo_f446re_servo.xml`. En el dibujo es el conector USB de
arriba: deja ver lo que entrega, y se rodea de rojo al limitar —un servo
bloqueado pide 650 mA—. VIN y E5V, que son entradas, siguen al aire.

#### `Conector`

Una tira de **filas × columnas** pines, numerados del 1 al N. **No conduce ni
escucha**: dice que esos nodos salen de la placa. Su pin *k* se llama `ID.k`
(`CN9.2`) —o por su nombre, si lo tiene: `P1.COM`—, se suelda a un nodo de la
placa con `<pin nombre="k">`, y uno sin soldar es un nodo propio, al aire, del
que se puede colgar otra pieza. Dos conectores se enchufan en un `<sistema>`
(§2.5).

| Terminal | | |
| :--- | :--- | :--- |
| `1` … `N` | los que se usen | Cada pin, por su número —o por su nombre, si lo tiene—, al nodo al que va soldado |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `columnas` | *(obligatorio)* | Cuántos pines tiene cada fila |
| `filas` | `1` | Cuántas filas: 2 en un IDC, en un morpho o en la cabecera de una Raspberry Pi |
| `acodado` | `no` | `si`: los pines salen **de lado**, por el borde de la placa —una tira acodada, como la del KY-040, la pantalla TFT, el servo o el FT232RL—; `no`: hacia arriba, como los de la Nucleo. Solo cambia el dibujo de `mcu-sim-gui`: un hilo llega al **borde** de un pin acodado y al **centro** de uno derecho, con un círculo. Va en su `<conector>` de `T_PLACA`. Otro valor es un error |
| `numeracion` | `zigzag` | `zigzag`: el 1 y el 2 enfrentados, impares en una fila y pares en la otra. `filas`: la primera fila entera, del 1 a `columnas`, y luego la siguiente |
| `nombres` | *(ninguno)* | **Los nombres de los pines**, uno por pin y en orden, separados por espacios; `-` para uno que se sigue llamando por su número (abajo) |

```xml
<!-- El CN9 de una Nucleo-64: D0..D7 -->
<componente tipo="Conector" id="CN9" filas="1" columnas="8">
  <pin nombre="1" nodo="PA3"/>    <!-- D0 -->
  <pin nombre="2" nodo="PA2"/>    <!-- D1 -->
</componente>
<!-- Un LED colgado directamente del pin 8 -->
<componente tipo="Led" id="LD7" a_vss="si"><pin nombre="anodo" nodo="CN9.8"/></componente>
```

**Un pin soldado a un pad ES ese pad**: el LED de un shield en `J5.6`, con
`J5` enchufado al `CN5` de la Nucleo y `CN5.6` soldado a `PA5`, cuelga de
`PA5`, y así lo dicen los mensajes y el informe. **Un pin al aire no es un
nodo flotante que avisar**: casi ningún montaje usa todos los pines de un
conector.

**La numeración solo importa en espejo.** Dos placas cara a cara se dan la
vuelta: en un 2×N el 1 cae sobre el 2; con una sola fila, el 1 cae sobre el
último. Para eso hace falta saber dónde está cada pin.

**Los pines con nombre.** Con `nombres="COM D1 D2 D3 D4 D5 D6 D7 D8"`, el
pin 1 se llama `P1.COM`, el 2 `P1.D1`, y así: es el nombre que se ve en la
serigrafía, el que se escribe en un `<hilo>` (`B/P1.D3`) y el que sale en los
mensajes. **Un pin con nombre se llama solo por su nombre**: `P1.1` no es el
pin 1 de `P1`, sino el pad `PB1`, como en cualquier otra parte. El número sigue
mandando en la forma —el 1 es el primero, y al acoplar el 1 va con el 1, tenga
nombre o no— y `T_PLACA` lleva los nombres en su `<conector nombres="...">`.
Un nombre empieza por letra, sigue con letras, cifras o `_`, no se repite, y
no puede hacer de `ID.nombre` un pad (`X.PA0`) ni una patilla de alimentación
(`X.VDD`); con `-` el pin se queda con su número.

```xml
<!-- El P1 de una barra de 8 LEDs: el comun a la izquierda, y D1..D8 -->
<componente tipo="Conector" id="P1" filas="1" columnas="9"
            nombres="COM D1 D2 D3 D4 D5 D6 D7 D8"/>
```

**Un límite.** Un id de conector con pines **por número** no puede ser algo
como `P1`, porque `P1.1` sería el pad `PB1`. Con todos sus pines con nombre,
sí: `P1.COM` no es ningún pad.

**Alimentación, masa, reset y arranque también pasan.** Un pin se puede
soldar a `VDD`, `VSS` (la masa del chip), `NRST`, `BOOT0` o cualquiera de los
diez pads de alimentación y arranque, y llevarlos así a otra placa o unirlos
con los de otro chip: con el NRST compartido, un reset de una placa resetea a
las dos.

#### `Jumper`

Un **jumper**: una tira de pines como un `Conector` —con su forma, sus números
y sus nombres—, y un **puente** puesto, la pieza de plástico que une dos pines
vecinos. Los dos son el mismo nodo, como si un `<hilo>` los uniera dentro de la
placa, y eso se resuelve antes de construir: el puente no se mueve con la
simulación en marcha.

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `puente` | *(obligatorio)* | **Los dos pines que une**, por nombre o por número: `"5V VCC"`. Tienen que ser vecinos —uno al lado del otro en la misma fila o columna—. Un bloque que lleva **varios puentes** los dice separados por comas, sin que un pin esté en dos: `"1 2, 3 4"`. `"no"`: sin puente |
| `columnas`, `filas`, `numeracion`, `nombres`, `acodado` | como en `Conector` | La forma de la tira, los nombres de sus pines y hacia dónde salen |

```xml
<!-- El jumper de un adaptador FT232RL: 5V arriba, VCC en medio, 3.3V abajo -->
<nodo id="VCC"/>
<componente tipo="Jumper" id="JP1" filas="3" columnas="1"
            nombres="5V VCC 3.3V" puente="5V VCC">
  <pin nombre="VCC" nodo="VCC"/>
</componente>
```

Son errores un `puente=` que falta, que no son dos pines, un pin que el jumper
no tiene, un pin consigo mismo, dos pines que no están uno al lado del otro y
un pin en dos puentes. En el dibujo, el puente es una **variante** (§2.6):
`JP1@5V-VCC` —los dos pines por orden de pin, con un guion— y `mcu-sim` manda
la que dice `puente=`; con varios, unidos por `+` en el orden en que se dicen:
`JP4@1-2+3-4`. `placas/ftdi_ft232rl.xml` es el ejemplo de uno, y los JP4 y JP5
de `placas/msp_exp432e401y.xml`, de 2x2 con dos puentes, el de varios.

**Un sistema puede mover el puente** de una de sus placas sin tocar su
fichero: `<jumper ref="E/J5" puente="PWM LCD"/>` (§2.5). Es como se monta la
placa en ese sistema: la BOOSTXL-EDUMKII sale de fábrica con su J5 en el rojo
del RGB, y `placas/msp_exp432e401y_edumkii.xml` lo pasa a la luz de la
pantalla.

#### `Rpull`

Una rama resistiva entre el nodo y una tensión fija. Es el equivalente Thevenin
`{v, r}` y nada más, así que sirve para el pull-up y el pull-down de la placa,
para polarizar una entrada y para cargar un pad y medir cuánta corriente
entrega.

| Terminal | | |
| :--- | :--- | :--- |
| `a` | obligatorio | El nodo al que se conecta |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `v` | `3.3` | La tensión del otro extremo. **Es una tensión cualquiera, no una elección entre VDD y masa**, y no tiene por qué existir en el MCU |
| `r` | `10000` | El valor de la resistencia, en ohmios |

Lo que `v` permite:

| | Qué monta |
| :--- | :--- |
| `v="3.3" r="4700"` | Pull-up al mismo raíl que el chip |
| `v="5" r="4700"` | **Pull-up a un raíl de 5 V**, como el de un bus I2C mixto |
| `v="0" r="4700"` | Pull-down |
| `v="1.8" r="10000"` | Polarización a media escala para una entrada de ADC |
| `v="0" r="1000"` | Una carga de 1 kΩ con la que medir la corriente de un pad |

Los dos últimos enseñan por qué la pieza no se llama `PullUp`: lo que hace es
una rama resistiva a un potencial, y de ahí salen tanto los pulls como las
cargas de prueba.

**Conduce siempre desde que se construye.** Es el único componente cuyo efecto
no depende de ningún proceso, y por eso es la forma más directa de sujetar un
nodo que si no quedaría flotante.

Un pull-up de 5 V sobre un pin donde también hay un LED, para ver que la tensión
es real y no una etiqueta:

```xml
<placa nombre="pull-5v">
  <nodo id="PE2" bus="si"/>          <!-- dos piezas conducen, a propósito -->
  <componente tipo="Rpull" id="R1" v="5" r="4700">
    <pin nombre="a" nodo="PE2"/>
  </componente>
  <componente tipo="Led" id="LD1" a_vss="si" vf="2.0" r="330">
    <pin nombre="anodo" nodo="PE2"/>
  </componente>
</placa>
```

Con el pin en entrada, el nodo lo resuelve la superposición de los dos: **2,20 V
y 0,60 mA** por el LED, que luce débil porque un pull-up de 4,7 kΩ no da para
más. Con `v="3.3"` bajan a 2,09 V y 0,26 mA; con `v="1.8"`, por debajo de la Vf,
el LED no conduce y el nodo se queda en 1,80 V.

#### `Resistencia`

Una resistencia **entre dos nodos**. A diferencia de `Rpull`, el otro extremo
no es una tensión fija sino un nodo del circuito: el pull-up que cuelga de la
VCC de un conector, la que ponga quien alimenta el módulo —5 V, 3,3 V o nada—.

| Terminal | | |
| :--- | :--- | :--- |
| `a` | obligatorio | Un extremo |
| `b` | obligatorio | El otro |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `r` | `10000` | El valor de la resistencia, en ohmios |

Se resuelve como el `Led` con las dos patillas a la vista: en cada nodo, lo que
no es la resistencia es un Thevenin, y la rama entre los dos se calcula exacta
(`RamaDosNodos` en `parts/ext_parts.h`). **Con un extremo al aire** no lleva
corriente, y ese extremo se queda a la tensión del otro: el pin que solo tiene
su pull-up lee la VCC. Con los dos al aire, no hace nada. `corriente()` es la
que va de `a` a `b`.

```xml
<componente tipo="Resistencia" id="R2" r="10000">
  <pin nombre="a" nodo="P1.VCC"/>
  <pin nombre="b" nodo="P1.CLK"/>
</componente>
```

#### `Encoder`

Un **encoder rotativo mecánico**, como el del módulo KY-040: un eje con
`pasos` posiciones por vuelta —los clics que se notan al girarlo— y **dos
contactos**, uno entre `a` y `c` y otro entre `b` y `c`. Según su hoja de
datos:

* en cada posición los dos están igual: **abiertos en las pares, cerrados en
  las impares**;
* cada clic los cambia los dos, pero **no a la vez**: girando a la derecha
  —en el sentido de las agujas del reloj— cambia primero `a`; a la izquierda,
  primero `b`. Es lo que dice hacia dónde se gira.

Con `c` a masa y `a` y `b` con su pull-up, `a` y `b` dan un **código Gray** de
dos bits, un cambio por cada medio clic:

| Girando | `a` `b` |
| :--- | :--- |
| a la derecha | 11 → 01 → 00 → 10 → 11 … |
| a la izquierda | 11 → 10 → 00 → 01 → 11 … |

Los pull-ups no son del encoder: los pone la placa.

Eso es **medio ciclo por clic**: en una vuelta, cada contacto da `pasos/2`
pulsos —el de 30 clics, 15—. Otros encoders dan **un ciclo entero por clic**:
los de 20 o 24 clics y otros tantos pulsos por vuelta (el PEC11R-4220F-S0024
de Bourns, 24 PPR y 24 clics). En el reposo los dos contactos están abiertos
—con pull-ups, `a` y `b` altos—, y cada clic los cierra y los vuelve a abrir,
los dos, el `a` delante a la derecha: los cuatro estados del código Gray en
cada clic. Es `pulsos` igual a `pasos`:

| Girando, un clic | `a` `b` |
| :--- | :--- |
| a la derecha | 11 → 01 → 00 → 10 → 11 |
| a la izquierda | 11 → 10 → 00 → 01 → 11 |

Hay **un** flanco de bajada de `a` por clic, y el sentido es `b` en ese
flanco: alto, a la derecha. Un firmware escrito para el de medio ciclo, que
cuenta los dos flancos de `a`, aquí cuenta dos por clic.

| Terminal | | |
| :--- | :--- | :--- |
| `a` | obligatorio | El contacto A, el que va delante girando a la derecha |
| `b` | obligatorio | El contacto B, el que va delante girando a la izquierda |
| `c` | obligatorio | El común de los dos |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `pasos` | `30` | Las posiciones —los clics— de una vuelta, de 1 a 1000 |
| `pulsos` | `pasos/2` | Los pulsos de una vuelta en cada contacto (PPR): `pasos/2`, medio ciclo por clic, o `pasos`, uno entero. Otro valor es un error que dice los dos que valen; con `pasos` impares, solo el entero |
| `r_cerrado` | `1` | La resistencia de un contacto cerrado, en ohmios |
| `desfase_ms` | `1` | Lo que tarda el segundo contacto en seguir al primero dentro de un clic. Tiene que ser menor que `clic_ms`; con un ciclo entero por clic, que son tres desfases, menor que un tercio |
| `clic_ms` | `5` | Lo que se espera, cuando se pide girar varios de una vez, entre el final de un clic y el principio del siguiente |

**El mando es `girar`**: la **cuenta** de clics, sin vueltas —sube hacia la
derecha y baja hacia la izquierda—, de −30000 a 30000. Pedir otra cuenta gira
el eje hasta ella, un clic detrás de otro. Deja ver `posicion` —la cuenta
dentro de la vuelta, de 0 a `pasos − 1`: −1 es la 29—, `cuenta`, `contacto_a`
y `contacto_b`. Desde C++, `gira(clics)` y `gira_a(cuenta)`.

Cada contacto es una rama de dos nodos, como una `Resistencia` de `r_cerrado`
que se abre y se cierra; el común `c` lleva las dos.

`placas/ky040.xml` es el módulo entero: el encoder con `c` en `P1.GND`, los
pull-ups de 10 kΩ de `CLK` y `DT` a `P1.VCC` (dos `Resistencia`) y el pulsador
del eje, un `Button` normalmente abierto a 0 V en `P1.SW`, **sin pull-up**,
como en la mayoría de los módulos. Su dibujo, `placas/ky040.svg`, tiene en el
centro el mando que gira (§2.6). `placas/ky040_24ppr.xml` es el mismo módulo
con el otro encoder que se le monta, `pasos="24" pulsos="24"`, y con el mismo
dibujo, que gira 15° por clic; `placas/nucleo_f446re_encoder_tft_servo.xml` lo
usa.

#### `Driver`

Salida digital externa genérica: otro chip de la placa gobernando ese pin.
**Nace en alta impedancia** y solo conduce cuando se le manda desde C++ con
`set(nivel)` o `set_volts(v, r)`.

Sirve para dos cosas: dar un nivel a una entrada del MCU, y provocar a propósito
el conflicto con una salida push-pull para ver la sobrecorriente en el pad.

| Terminal | | |
| :--- | :--- | :--- |
| `pin` | obligatorio | |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `vdd` | `3.3` | La tensión de su nivel alto |
| `r_out` | `25` | Su impedancia de salida. Bajarla lo hace «más fuerte» en un conflicto |

#### `SignalLink`

Una pista de placa entre dos pines: lo que hay entre el TX de un puerto serie y
el RX del otro. Lee la tensión del origen, la digitaliza **con el mismo umbral
de histéresis que un pad** (sube a 0,55·VDD, baja a 0,45·VDD) y la reproduce en
el destino con 50 Ω.

Es **unidireccional por construcción**, que es lo que hace falta en un enlace
full-duplex donde cada hilo tiene un único emisor. Un hilo compartido de verdad
—medio dúplex, colector abierto— **no se modela con esta pieza**, sino
conectando los dos pines al mismo nodo con [`une`](#21-los-nodos).

Sirve igual entre dos pines del **mismo** MCU: el banco lleva una pista de PD12
(la salida PWM de TIM4) a PB4 (la entrada de captura de TIM3) para medir con un
temporizador lo que genera otro, y la suelda solo para esas pruebas. Frente a un
nodo compartido, la pista tiene dos ventajas y una carencia: **se puede desoldar
en marcha** y no carga el origen, pero **el conflicto entre los dos extremos es
invisible** —si los dos conducen, uno pisa al otro sin que nada avise—. La
comparación completa está en `doc/multi_mcu.md`, §4.5.

| Terminal | | |
| :--- | :--- | :--- |
| `origen` | obligatorio | Solo se lee: no lleva driver, y en el netlist sale marcado como pasivo |
| `destino` | obligatorio | El que la pista gobierna |

Sin parámetros: 3,3 V y 50 Ω fijos.

#### `ExtClock`

Oscilador de encapsulado o generador de señal: una onda cuadrada de 0 a 3,3 V
con 50 Ω de salida.

| Terminal | | |
| :--- | :--- | :--- |
| `out` | obligatorio | |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `hz` | `0` | Frecuencia. **Con `0` la pieza no conduce**: queda en alta impedancia y no gasta tiempo de simulación. Es lo razonable al arrancar, y se enciende desde C++ con `set_freq()` |
| `onda` | `si` | Con `no` la pieza **no da flancos**: deja el nodo a 0 V y le DICE su frecuencia, que es lo que mira el HSE del chip para arrancar (en modo cristal y en bypass) y contar. Ocho millones de flancos por segundo cuestan más que el resto de una placa entera; sin onda, el reloj llega igual y el pin se lee a cero. Es lo que hace el MCO del ST-LINK de `placas/stm32f3discovery.xml` (S1 del F303) |

Recuérdese §3.4: `hz="8M"` son **8 Hz**. Escríbase `8e6`.

#### `Crystal`

Cristal de cuarzo con sus condensadores de carga. Lo que el pin `OSC_IN` ve del
lazo oscilador es una red de polarización: una impedancia de 1 MΩ hacia VDD/2. Su
presencia es **la condición para que el HSE o el LSE arranquen**, y quitarlo es
lo que debe detectar el CSS.

| Terminal | | |
| :--- | :--- | :--- |
| `osc_in` | obligatorio | `PH0` para el HSE, `PC14` para el LSE |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `vdd` | `3.3` | La pieza polariza el pin a `vdd`/2 |

No lleva la frecuencia: la del HSE es un dato del árbol de reloj y se configura
en el RCC. El cristal solo dice que **hay** un cristal. `conectada="no"` modela
la placa sin él.

#### `SwoReceiver`

Analizador de traza colgado del pin SWO. **Solo escucha, nunca conduce.** En
modo NRZ el pin es una línea serie asíncrona corriente, así que esto es un
receptor de UART con el desempaquetado del protocolo ITM encima.

| Terminal | | |
| :--- | :--- | :--- |
| `swo` | obligatorio | Normalmente `PB3` |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `bitrate` | `1000000` | Velocidad en bits por segundo. **Tiene que coincidir con la que programe el TPIU** o la trama se desincroniza |

Lo recibido se consulta desde C++: `bytes()`, `mensajes()`, `texto(puerto)` —el
`printf` del ITM—.

---

### 4.2 El bus I2C

Es de **colector abierto**: nadie fuerza un uno, solo se tira de la línea a cero
(30 Ω) o se suelta, y el nivel alto lo dan las resistencias de pull-up. Todo eso
ocurre de verdad en el nodo.

#### `I2cWire`

El hilo: **cortocircuita entre sí N pines** y les pone un pull-up común. Es lo
que permite que el I2C1 del MCU y el I2C3 hablen por el mismo bus, o que la
EEPROM esté colgada de los mismos hilos que el maestro.

| Terminal | | |
| :--- | :--- | :--- |
| `l0`, `l1`, … | al menos uno | Los pines que quedan unidos. Bus indexado: sin huecos (§2.2) |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `r_pull` | `4700` | El pull-up, en ohmios. Subirlo hace la línea más lenta al soltarla, que es el efecto real de un pull-up flojo |

Hace falta **una instancia por línea**: una para SCL y otra para SDA. Y el nodo
de cada línea querrá `bus="si"`, porque por definición tendrá varios
conductores.

#### `I2cEeprom`

Una 24Cxx en los pines: 64 bytes, direccionamiento de un byte de puntero,
escritura y lectura secuenciales, y su bit de reconocimiento.

| Terminal | | |
| :--- | :--- | :--- |
| `scl` | obligatorio | |
| `sda` | obligatorio | |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `dir` | `0x50` (80) | Dirección de esclavo de 7 bits. Se puede escribir en hexadecimal (`0x50`) o decimal (`80`) |

Desde C++: `peek`/`poke` para ver y poner memoria, `set_ack(false)` para
provocar un NACK y `set_stretch_us()` para que retenga el reloj.

#### `I2cExtMaster`

Otro microcontrolador en la misma placa. Sirve para probar el MCU **como
esclavo** y para provocar la pérdida de arbitraje.

| Terminal | | |
| :--- | :--- | :--- |
| `scl` | obligatorio | |
| `sda` | obligatorio | |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `f_scl` | `100000` | Frecuencia de reloj del bus, en Hz. Respeta el estiramiento del esclavo |

Las transacciones se piden desde C++ (`request_write`, `request_read`).

---

### 4.3 El bus CAN

Un bus CAN **no es una señal: es un cable en Y**. El estado dominante gana al
recesivo porque un cero de 20 Ω gana a un terminador de 1 kΩ, y de ahí —no de un
`&&`— salen el arbitraje y el asentimiento.

Se monta con tres piezas y **el orden importa**: el hilo primero.

#### `CanWire`

El hilo con su terminador. Recesivo = alto.

| Terminal | | |
| :--- | :--- | :--- |
| `bus` | obligatorio | El nodo del hilo. Casi siempre un nodo `externo="si"` y `bus="si"` |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `vdd` | `3.3` | Nivel recesivo, y umbral: dominante es por debajo de `vdd`/2 |
| `r_term` | `1000` | El terminador. Un bus real lleva 120 Ω; `conectada="no"` es desenchufar el cable y **deja el hilo flotando** |

#### `CanTransceiver`

El chip que va al lado del microcontrolador: convierte los dos pines digitales
en el estado del hilo y al revés.

| Terminal | | |
| :--- | :--- | :--- |
| `txd` | obligatorio | El pin CAN_TX del MCU. **Lleva pull-up de 10 kΩ**, como los chips reales: sin él, un TXD flotante —el MCU aún sin configurar— atascaría el bus entero en dominante |
| `rxd` | obligatorio | El pin CAN_RX. El transceptor lo gobierna push-pull a 50 Ω |
| `bus` | recomendado | El nodo del hilo. La construcción no lo usa —para eso está la referencia—, pero **sin él el netlist queda incompleto** y la comprobación de ida y vuelta lo detecta |

| Parámetro / referencia | Omisión | Efecto |
| :--- | :--- | :--- |
| `<ref nombre="hilo">` | *(obligatorio)* | El `id` del `CanWire`. Tiene que estar declarado **antes** |
| `vdd` | `3.3` | Tensión de la lógica del transceptor |

Tiene **dos interruptores distintos**, y conviene no confundirlos:
`conectada="no"` es no soldarlo a la placa; `set_standby(false)` desde C++ es el
modo de bajo consumo, en el que deja de gobernar el hilo pero sigue escuchando.

#### `CanNode`

Otro controlador colgado del mismo hilo, que habla el protocolo de verdad
—relleno de bits, CRC15, asentimiento— con las mismas funciones que el
periférico del MCU. **No toca ningún pin del MCU.**

Sin él no se puede probar casi nada: sin nadie que asienta, un bus CAN no
entrega nada.

| Terminal | | |
| :--- | :--- | :--- |
| `bus` | obligatorio | El nodo del hilo |

| Parámetro / referencia | Omisión | Efecto |
| :--- | :--- | :--- |
| `<ref nombre="hilo">` | *(obligatorio)* | El `id` del `CanWire`, declarado antes |
| `bitrate` | `500000` | Velocidad en bits por segundo. **Tiene que coincidir con la del bxCAN** o no habrá más que errores de bit |

Desde C++: `send()`, `received()`, `last()`, y `set_ack(false)` para dejar de
asentir y provocar el error de reconocimiento.

---

### 4.4 Memorias del bus externo (FSMC)

Las dos comparten los hilos de datos y varias señales de control, y en este
encapsulado **comparten también el chip select** (`PD7` es NE1 y NCE2 a la vez):
no pueden estar puestas las dos. Una de las dos irá con `conectada="no"`.

#### `ExtSram`

SRAM asíncrona de 64 KB. No tiene reloj y no negocia nada: obedece.

| Terminal | | |
| :--- | :--- | :--- |
| `d0`…`d15` | obligatorios | Los hilos de datos. Bidireccionales: la memoria conduce al leer y escucha al escribir |
| `a16`…`a23` | opcionales | Las direcciones altas que el encapsulado saca. **Empiezan en 16 a propósito**: sin A0–A15, este chip solo puede usar el bus multiplexado, y la parte baja de la dirección la engancha de los propios hilos de datos con NL |
| `ne` | obligatorio | Chip select, activo a cero. Pasivo |
| `noe` | obligatorio | Output enable. Pasivo |
| `nwe` | obligatorio | Write enable; **guarda en su flanco de subida** |
| `nl` | obligatorio | Address latch; engancha la dirección baja en su flanco de subida |
| `nbl0`, `nbl1` | opcionales | Byte lanes: cuál de los dos bytes se escribe |
| `nwait` | opcional | Si se conecta, la memoria puede pedir tiempo. Es lo único del bus externo que no decide el controlador |

Sin parámetros en el XML. El ancho (8 o 16 bits) y el multiplexado se ajustan
desde C++ con `set_ancho()` y `set_mux()`; por omisión, **16 bits y
multiplexado**. El contenido se ve con `lee`/`escribe`.

#### `ExtNand`

NAND flash de 16 páginas de 512 bytes. Una NAND **no tiene bus de direcciones**:
tiene ocho hilos por los que van mandatos, direcciones y datos, y dos señales
—CLE y ALE— que dicen cuál de las tres cosas viaja en cada ciclo. El FSMC saca
CLE y ALE por A16 y A17, de modo que *escribir en una dirección u otra del banco*
es lo que elige el tipo de ciclo.

Entiende leer identificación (0x90), leer página (0x00…0x30), programar
(0x80…0x10) y leer estado (0x70).

| Terminal | | |
| :--- | :--- | :--- |
| `d0`…`d7` | obligatorios | Los ocho hilos, compartidos con la SRAM |
| `cle` | obligatorio | Command latch enable (A16). Pasivo |
| `ale` | obligatorio | Address latch enable (A17). Pasivo |
| `nce` | obligatorio | Chip enable. Pasivo |
| `noe`, `nwe` | obligatorios | Pasivos |
| `rb` | opcional | Ready/Busy. Si se conecta, la NAND lo gobierna |

Sin parámetros en el XML.

---

### 4.5 El sensor de imagen (DCMI)

#### `CameraSensor`

Un sensor CMOS como el de una placa con OV7670 o MT9V034. Genera PIXCLK,
conduce los datos **en el flanco contrario al que muestrea el DCMI** —así se
cumple el tiempo de establecimiento— y marca los bordes con VSYNC y HSYNC, o sin
ellos, metiendo los códigos de sincronismo en el propio flujo (BT.656).

Su rasgo esencial: **no se puede parar**. Si el DCMI no vacía su FIFO a tiempo,
los datos se pierden. Un modelo de sensor que esperase sería un modelo inútil.

| Terminal | | |
| :--- | :--- | :--- |
| `pixclk` | obligatorio | El reloj de píxel, que pone el sensor |
| `hsync`, `vsync` | obligatorios | Los sincronismos |
| `d0`…`dN` | al menos uno | Los hilos de datos. **Los que el encapsulado no saca sencillamente no se declaran**, y el DCMI leerá lo que haya en unas entradas que nadie gobierna — que es justo lo que pasa en la placa |

Sin parámetros en el XML. El formato (`set_formato`), la frecuencia
(`set_pixclk`), las polaridades, el blanking, el sincronismo embebido y el
patrón se ajustan desde C++. Por omisión: 16×8 píxeles de 8 bits a 6 MHz. Los
cuadros se piden con `emitir(n)`.

---

### 4.6 Los dos extremos del cable USB

Todo lo eléctrico va por los nodos con tensiones de verdad: **la conexión, la
velocidad y el reset salen del divisor resistivo**, no de una variable booleana.
Los paquetes, en cambio, cruzan como paquetes.

#### `UsbHostRig`

Un PC al otro lado: da los 5 V de VBUS, pone los dos 15 kΩ a masa —que es lo que
convierte a este extremo en anfitrión— y hace el reset de bus con un SE0 largo.

| Terminal | | |
| :--- | :--- | :--- |
| `dm`, `dp` | obligatorios | El par diferencial |
| `vbus` | obligatorio | Los 5 V |
| `id` | obligatorio | A masa = cable A = el que lo tiene enchufado es el anfitrión |

Sin parámetros. `conectada="no"` es el cable desenchufado, y es lo razonable al
arrancar. Desde C++: `set_vbus`, `reset_bus`, `resume`, `setup`/`in`/`out`,
`sofs` —el latido de 1 ms, sin el cual todo dispositivo se suspende— y
`conectar_dispositivo` para engancharlo al OTG del MCU.

#### `UsbDeviceRig`

Un pendrive: pone su 1,5 kΩ en D+ cuando lo enchufan y **se entera del reset
porque lo ve en el cable**, no porque nadie se lo diga.

| Terminal | | |
| :--- | :--- | :--- |
| `dm`, `dp` | obligatorios | El par diferencial |
| `vbus` | obligatorio | El interruptor de 5 V de la placa. No lo da el MCU —el pin de VBUS es una entrada de sensado—, lo da un conmutador externo |

Sin parámetros. El 1,5 kΩ es **la declaración de existencia**, y en cuál de los
dos hilos se pone es lo que dice la velocidad: D+ para Full Speed, D− para Low
Speed (`set_baja_velocidad`). Desde C++: `enchufar`, `alimentacion_placa`,
`direccion`, `resets`.

---

### 4.7 El PHY de Ethernet

#### `EthPhy`

El integrado entre el MAC y el conector RJ45. Hace tres cosas que el MAC no
puede hacer solo: **pone los relojes** del camino de datos (el MAC no genera
nada, los sigue), habla **MDIO** bit a bit con los registros de la norma, y hace
de buzón de tramas con su CRC-32.

Dieciocho terminales para MII; de ellos, nueve son los de RMII.

| Terminal | | |
| :--- | :--- | :--- |
| `mdc` | obligatorio | Reloj del MDIO, que pone el MAC. Pasivo |
| `mdio` | obligatorio | Datos del MDIO, bidireccional. **Lleva el pull-up de 10 kΩ de la placa**, sin el cual preguntar a una dirección donde no hay nadie devolvería basura en vez de todo unos |
| `tx_clk` | obligatorio | 25 MHz en MII a 100 Mbit/s |
| `rx_clk` | obligatorio | En RMII es el REF_CLK de 50 MHz que sirve para los dos sentidos |
| `tx_en` | obligatorio | Lo gobierna el MAC. Pasivo |
| `txd0`…`txd3` | según el modo | Lo gobierna el MAC. Pasivos. Dos en RMII, cuatro en MII |
| `rxd0`…`rxd3` | según el modo | Los gobierna el PHY |
| `rx_dv` | obligatorio | RX_DV en MII, CRS_DV en RMII |
| `rx_er`, `crs`, `col` | obligatorios | Solo MII, pero se declaran igual |

Sin parámetros en el XML. El modo (`set_rmii`), la velocidad (`set_cien`), el
estado del enlace (`set_enlace`) y la dirección de MDIO (`set_dir`, 0 por
omisión) se ajustan desde C++, igual que la inyección de tramas (`enviar`) y lo
capturado (`recibidas`).

Cuidado con los pines: en el LQFP100 están los dieciocho, pero **todos están
cogidos**. MII_CRS es `PA0`-WKUP, así que con MII cableado el MCU pierde el pin
de despertar desde Standby; y MII_RXD0/1 son `PC4`/`PC5` = ADC12_IN14/15,
mientras que MII_COL es `PA3` = OTG_HS_ULPI_D0.

---

### 4.8 La tarjeta SD

#### `SdCard`

Una tarjeta SD v2.0 a nivel de pin: habla el protocolo de verdad, bit a bit, con
su CRC7 en los comandos y su CRC16 por cada línea de datos. No entiende de
registros del MCU; solo ve CK, CMD y D0–D3.

Implementa el arranque completo: CMD0, CMD8, CMD55, ACMD41, CMD2, CMD3, CMD9,
CMD7, CMD16, ACMD6 —que es lo que pone el bus a cuatro hilos—, CMD17 y CMD24.

| Terminal | | |
| :--- | :--- | :--- |
| `ck` | obligatorio | El reloj, que pone el host. Pasivo |
| `cmd` | obligatorio | Bidireccional. **Lleva el pull-up de 47 kΩ del zócalo** |
| `dat0` | recomendado | Con solo `dat0` la tarjeta funciona en modo de un hilo |
| `dat1`, `dat2`, `dat3` | opcionales | Hacen falta para el bus de cuatro hilos. Cada uno con su pull-up de 47 kΩ |

Sin parámetros en el XML. `conectada="no"` es el zócalo vacío: **se van con ella
los pull-ups**, que es precisamente lo que distingue un zócalo vacío de uno
ocupado.

Desde C++: `peek`/`poke`, `commands()`, `bus_width()`, y tres averías a
propósito —`break_resp_crc`, `break_data_crc` y `set_mute`— para comprobar que
el host levanta CCRCFAIL, DCRCFAIL y CTIMEOUT de verdad.

### 4.9 El puente UART hacia el ordenador

#### `PuenteSerie`

Lo que hace el ST-LINK/V2-1 de una Nucleo con su puerto COM virtual: un UART de
placa colgado de los pines de una USART, que lleva los bytes a otro sitio
(**P-14**; el plan está en `doc/analisis_puente_serie.md` §10). Lee el TX del MCU
con el mismo receptor que `SwoReceiver` y gobierna su RX con 50 Ω, así que **si
los baudios o el formato no coinciden con los del firmware, la USART del modelo
ve basura y levanta FE/NF**, como en la placa.

| Terminal | | |
| :--- | :--- | :--- |
| `rx` | opcional | Lee el TX del MCU. Pasivo |
| `tx` | opcional | Gobierna el RX del MCU |
| `cts` | opcional | Lee el RTS del MCU. Pasivo. Imprescindible con `flujo="rtscts"` |
| `rts` | opcional | Gobierna el CTS del MCU: bajo = puede mandar |
| `dtr` | opcional | Alta en reposo, baja si el anfitrión activa DTR (por RFC 2217) |
| `vccio` | opcional | **La alimentación de sus patillas**, como el VCCIO de un FT232R: si está, el nivel alto de `tx`, `rts` y `dtr` y el umbral de `rx` y `cts` son los de ese nodo, y lo siguen si cambia; y sus entradas, `rx` y `cts`, llevan el pull-up de 200 kΩ a VCCIO del chip. Sin ella, 3,3 V y entradas sin pull-up |

Hace falta al menos `rx` o `tx`. Los nombres son los del **adaptador**, como
vienen serigrafiados en uno de verdad: su `rx` va al TX del MCU.

| Atributo | Por omisión | |
| :--- | :--- | :--- |
| `host` | `rfc2217:3355` | `memoria`, `tcp:PUERTO` (en crudo) o `rfc2217:PUERTO` (Telnet con la opción 44: el terminal puede fijar la línea, mover DTR y RTS y mandar breaks), escuchando siempre en `localhost`. O **conectándose** a un servidor que ya escucha: `tcp-cliente:HOST:PUERTO` o `rfc2217-cliente:HOST:PUERTO` (este configura el puerto remoto con la línea del puente), reintentando cada segundo. `--serie ID=DESTINO` lo cambia sin tocar el XML —en un sistema, con la placa delante: `--serie N/VCP=memoria`—. Con cualquier destino de red, la simulación no termina sola (como con `--gdb`) y conviene `--tiempo-real`; **salvo con los ms dichos** (`--ms=` o el tercer argumento), que acaba a su hora con el puente escuchando mientras tanto. Dos puentes que piden **el mismo puerto en sus ficheros** —dos Nucleo en un sistema— no chocan: el segundo se corre al siguiente libre, y se dice; con `--serie`, en cambio, es un error |
| `baudios` | `115200` | Un entero entre 50 y 10 500 000, o `host`: los fija el terminal por RFC 2217, **y con ellos el formato y el control de flujo**. Con un número, el XML manda: lo que pida el terminal se le contesta con lo que hay, y se avisa una vez |
| `formato` | `8N1` | Bits de datos **sin contar la paridad** (5..9), paridad `N`/`E`/`O`/`M`/`S` y parada `1`, `1.5` o `2` |
| `flujo` | `no` | `rtscts`: no manda mientras el RTS del MCU esté alto |
| `muestra` | `si` | Imprime línea a línea lo que manda el MCU, con el id delante; lo que quede a medias sale al acabar |
| `guion` | nada | Lo que se «teclea» al arrancar, con `\r \n \t \0 \\ \xNN`. Solo con `host="memoria"`: con un terminal al otro lado, teclea el terminal |
| `guion_ms` | `10` | Cuándo se teclea, en ms simulados |

A diferencia del resto del catálogo, **los atributos de esta pieza se validan
antes de montar**: un formato, unos baudios o un destino mal escritos no se
quedan en su valor por omisión, se rechazan con el motivo. Una trama con error se
entrega igual —es la basura que el alumno tiene que ver— y un break se cuenta y
no se entrega. Con `rfc2217`, los dos llegan además al terminal como
`NOTIFY-LINESTATE` si los pide con su máscara, y el RTS del MCU (terminal `cts`)
es el CTS del terminal, en `NOTIFY-MODEMSTATE`; DSR y DCD, siempre activas.

Desde C++: `envia(texto)`, `recibido()`, `set_baudios()`, `set_formato()`,
`envia_break()`, `set_break()`, `set_rts()`, `set_dtr()`, `rfc2217()` y los
contadores. Ejemplos completos en `placas/vcp_memoria.xml`, `placas/vcp_tcp.xml`,
`placas/vcp_rfc2217.xml`, `placas/nucleo_f446re_vcp.xml` (la Nucleo-F446RE con
el VCP del ST-LINK) y `placas/nucleo_f446re.xml`, que lo lleva también. **La receta para ver el `printf` en tu ordenador, por
sistema, está en `doc/puente_serie.md`**; con `--espera-terminal`, `mcu-sim` no
arranca el MCU hasta que el terminal está conectado, y así no se pierde lo que
el firmware imprime al arrancar.

### 4.10 La sonda XDS110 de las LaunchPad MSP432

#### `Xds110`

**El lado de la sonda de J101** en la MSP-EXP432P401R (el XDS110-ET) y en la
MSP-EXP432E401Y (el XDS110): el LDO que alimenta el lado del objetivo, la
sonda SWD para OpenOCD y la traza SWO (X1 de `doc/analisis_xds110.md`).
Como los puentes de J101 están entre esta pieza y el chip, **hacen lo que en
la placa**: sin el de 3V3 el chip no arranca; sin el de TMS o el de TCK,
OpenOCD no encuentra el DAP; sin el de RST no hay SRST.

| Terminal | | |
| :--- | :--- | :--- |
| `3v3` | opcional | La salida del LDO: el pin 3V3 del lado de la sonda de J101. Con él, el adaptador del chip **deja de alimentar** su DVCC (P4) o su VDD (E4): lo pone la placa |
| `rst` | opcional | El SRST: a masa (10 Ω) mientras OpenOCD resetea |
| `swdio`, `swclk` | opcional | TMS_SWDIO y TCK_SWCLK. Hacen falta para la sonda |
| `swo` | opcional | TDO_SWO: la traza. Hace falta para `swo=` |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `et` | `no` | `si`: es el XDS110-ET. Hoy solo se dice; la medida de EnergyTrace es de la fase X3 |
| `v`, `limite_ma` | `3.3`, `500` | La tensión del LDO y su límite: una `Fuente` por dentro |
| `sonda` | `no` | `openocd` (`remote_bitbang`) o `cmsis-dap` (CMSIS-DAP por TCP). Lo normal es no ponerlo: **`--openocd` y `--cmsis-dap` (o `depuracion=` en el `<mcu>`) cuelgan la sonda de esta pieza** si la placa del chip la lleva |
| `puerto` | `3335` / `4441` | El de la sonda |
| `swo` | `no` | A dónde va la traza: `memoria`, `tcp:PUERTO`, `rfc2217:PUERTO` o sus clientes, como el `host=` de un `PuenteSerie`. `--swo DESTINO` lo dice sin tocar el XML |
| `swo_bitrate` | `1000000` | Los bit/s de la traza en NRZ —el único formato del XDS110—. **Tienen que ser los del TPIU** (HCLK/(ACPR+1)), o llega basura. `--swo-bitrate=N` |
| `swo_formato` | `itm` | `itm`: solo lo que el firmware escribe en el puerto de estímulo `swo_puerto` del ITM —el `printf` de `ITM_SendChar`—; `crudo`: los bytes de la línea, para un decodificador externo |
| `swo_puerto` | `0` | El puerto de estímulo, con `itm` |

```xml
<componente tipo="Xds110" id="XDS" et="si">
  <pin nombre="3v3"   nodo="J101.6"/>
  <pin nombre="rst"   nodo="J101.12"/>
  <pin nombre="swdio" nodo="J101.14"/>
  <pin nombre="swclk" nodo="J101.16"/>
  <pin nombre="swo"   nodo="J101.18"/>
</componente>
```

**Observables:** `corriente` (mA, la que entrega el LDO, medida sin el umbral
de resolución del nodo: se ven los microamperios), `sobrecorriente` (alarma)
y `depurando` (hay un OpenOCD conectado). Al acabar, `sim` dice la corriente
del LDO, lo recibido por la traza y los SRST.

**Lo que no hace:** el *backchannel* es un `PuenteSerie` aparte; las
corrientes de los pines del chip no pasan por el LDO (las dan los pads del
modelo), y el E4 no tiene modelo de consumo: su LDO entrega 0 mA.

**La traza en el P401R** sale por PJ.5 con su función primaria (`PJSEL0.5 =
1`); en el E401Y, por PC3, que ya es TDO/SWO al salir del reset. Los
firmwares `verif/fw/msp432p4/p4_swo` y `verif/fw/msp432e4/e4_swo` son el
ejemplo:

```
./build/mcu-sim placas/msp_exp432p401r.xml verif/fw/msp432p4/p4_swo/p4_swo.out --swo memoria --ms=20
```

### 4.11 La pantalla TFT

#### `Tft128x160`

Una pantalla TFT de 1,8 pulgadas y **128x160** píxeles con su controlador
**ST7735S**, por **SPI de cuatro hilos**: el módulo rojo de ocho pines, con su
regulador de 3,3 V y su retroiluminación [ST7735S, Sitronix, v1.1]. Está en
`parts/tft_st7735.h`.

| Terminal | | |
| :--- | :--- | :--- |
| `vcc` | obligatorio | La alimentación del módulo, de 3,3 a 5 V: va al regulador, que da los 3,3 V del chip. Con menos de 2,65 V el chip no arranca. Consume unos 3 mA |
| `gnd` | obligatorio | La masa |
| `cs` | obligatorio | CSX, la selección: con ella alta la interfaz no atiende y se reinicia |
| `reset` | obligatorio | RESX: un pulso bajo de más de 5 µs reinicia el chip, que no atiende órdenes hasta 5 ms después |
| `ad` | obligatorio | D/CX —A0, DC o RS en otros módulos—: bajo, el byte es una orden; alto, un dato |
| `sda` | obligatorio | SDA, el dato. Lo toma en el flanco de subida de SCK, y lo pone el chip al contestar una lectura |
| `sck` | obligatorio | SCL, el reloj |
| `led` | obligatorio | La retroiluminación: el ánodo de los LEDs blancos, con su resistencia en la placa. A 3,3 V, unos 26 mA y la luz entera |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `memoria` | `128x160` | Cómo direcciona la memoria el ST7735S: `128x160` (GM = 11: lo visible empieza en la columna 0 y la fila 0) o `132x162` (GM = 00, el de algunos módulos: lo visible empieza en la columna 2 y la fila 1) |
| `panel` | `rgb` | El orden de los filtros de color del panel, `rgb` o `bgr`. Con uno `bgr` el rojo y el azul salen cambiados si el firmware no pone el bit RGB de MADCTL |
| `vf_luz` | `2.9` | La tensión de los LEDs de la retroiluminación, en V |
| `r_luz` | `15` | Su resistencia en la placa, en ohmios |

**Lo que se ve es lo que enseñaría la pantalla de verdad**, y se lo manda a
la ventana como una IMAGEN de 128x160 (§4.15):

* el chip sin tensión, en reset, dormido —SLPIN, el estado tras arrancar— o
  con la pantalla apagada —DISPOFF—: **blanca**. El cristal es de los
  normalmente blancos: sin tensión deja pasar la luz. Es la pantalla blanca
  que enseña un módulo que nadie ha iniciado;
* despierto y encendido, **su memoria**, con la geometría de MADCTL (MX, MY,
  MV), el desplazamiento vertical (SCRLAR, VSCSAD), el modo parcial (PTLAR,
  PTLON: fuera del área, blanco), los ocho colores (IDMON: el bit alto de cada
  color), la inversión (INVON) y el orden RGB/BGR del panel contra el bit RGB
  de MADCTL;
* la memoria, al dar tensión, tiene lo que tenga: **ruido**. Un firmware que
  la enciende sin borrarla enseña colores al azar —siempre los mismos—;
* y todo con la **luz** del pin LED: sin ella, negra. La luz va aparte de los
  píxeles y es la media entre dos imágenes: con PWM en LED se ve la luz media.

**La interfaz** es la de la hoja [§9.4]: con CSX bajo, cada subida de SCL
toma un bit de SDA, el más alto primero, y en la octava también D/CX. Un CSX
que sube a medio byte lo tira [§9.5]; uno que sube entre bytes es una pausa
[§9.6]. Los formatos de COLMOD —12, 16 y 18 bits por píxel— pasan por la tabla
de color (RGBSET); mientras nadie la escriba, por la expansión natural de cada
formato —la hoja dice que al arrancar es aleatoria, pero los módulos de verdad
pintan bien en 16 bits sin tocarla—. **Las órdenes del sistema están todas**,
también las de lectura —RDDID, RDDST, RDDPM, RDDMADCTL, RDDCOLMOD, RDDIM,
RDDSM, RDDSDR, RDID1-3 y RAMRD—, que contestan por SDA poniendo cada bit en la
bajada de SCL; las de 24 y 32 bits y RAMRD, con un ciclo de reloj vacío
delante [figura 20]. El ID es 7C 89 F0. Las del panel (B1h-FCh: marco,
potencia, VCOM, gamma) se aceptan con sus parámetros y no cambian nada de lo
que se ve.

**Lo que se dice en vez de callar**, un aviso por cosa:

| Qué | Lo que hace el modelo |
| :--- | :--- |
| Una orden antes de 5 ms tras soltar RESET [§9.17, nota 7] | La pierde, como el chip, que está cargando sus registros |
| Una orden antes de los 120 ms de SLPOUT, SLPIN o SWRESET; SLPOUT antes de 120 ms tras el reset | La aplica, pero la hoja no lo garantiza |
| Un reloj más rápido que la hoja: 66 ns de ciclo al escribir, 150 al leer [§8.4] | Lo atiende igual |
| Más de VDDI + 0,3 V en una entrada: 5 V en un chip de 3,3 V [§7.1] | Lo atiende igual; este módulo no lleva adaptadores de nivel |
| Una orden que el ST7735S no tiene, o un COLMOD que no existe | La ignora |
| TFA + VSA + BFA distinto de las líneas de la memoria [10.1.26] | Desplaza igual; la hoja dice que la imagen no está definida |

**Lo que no se modela**: la gamma y los ajustes de potencia —se aceptan—, la
salida TE —este módulo no la saca—, el tiempo de refresco del cristal —lo
escrito se ve en cuanto está escrito— y el autodiagnóstico de SLPOUT (RDDSDR
lee 0).

Deja ver `encendida` (si enseña su memoria), `luz` (mA) y la imagen
`pantalla`. Al terminar, `sim` dice cómo ha quedado:
`TFT T/TFT: ensena su memoria; 16 bits por pixel, MADCTL 0x60; 180 ordenes,
30816 pixeles; luz 26.2 mA`, y si se perdió alguna orden. Desde C++,
`mostrando()`, `color_en(x, y)` —el color que se ve— y `memoria(col, fila)`.

**La postura y MADCTL.** La imagen gira con la placa, así que lo que
enseña la pantalla derecho depende de cómo esté montada y de MADCTL, como en
la de verdad:

| `giro=` | Cómo queda | MADCTL para verlo derecho |
| :--- | :--- | :--- |
| 0 | apaisada, conector a la derecha | `0x60` (MV, MX) |
| 90 | de pie, conector abajo | `0x00` |
| 180 | apaisada, conector a la izquierda | `0xA0` (MV, MY) |
| 270 | de pie, conector arriba | `0xC0` (MX, MY) |

`placas/tft_128x160.xml` es el módulo, con el conector de 8 pines `P1` a la
derecha: VCC, GND, CS, RESET, AD —serigrafiado `A/D`; la barra separa en un
sistema la placa del nombre—, SDA, SCK y LED. Y
`placas/nucleo_f446re_tft.xml`, el módulo cableado a los conectores Arduino de
la Nucleo, con `verif/fw/tft_demo`: lee el ID por SDA, la despierta y dibuja
—el título, ocho barras de color y un cuadrado que va y viene—.

### 4.12 El servo

#### `Servo`

Un **servo de modelismo**, como el SG90: un motor con su reductora y, dentro,
un potenciómetro y un circuito que lleva el eje al ángulo que pide la
**anchura del pulso** de la señal, que llega cada ~20 ms [SG90, TowerPro;
Handson Technology, «SG90 Micro Servo»]. Está en `parts/servo.h`.

| Terminal | | |
| :--- | :--- | :--- |
| `vcc` | obligatorio | La alimentación del motor y del circuito, de `v_min` a `v_max` |
| `gnd` | obligatorio | La masa |
| `pwm` | obligatorio | La señal. Entrada de alta impedancia con umbrales de 1,6 V al subir y 0,8 V al bajar: vale con los 3,3 V de un STM32 |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `periodo_ms` | `20` | El periodo que espera de la señal, de 2 a 100 ms. Uno medido de menos de la mitad o más del doble se avisa |
| `angulo_min`, `angulo_max` | `-90`, `90` | Los ángulos de los dos extremos, en grados. Positivo es en el sentido de las agujas del reloj, mirando el eje desde arriba |
| `pulso_min_ms`, `pulso_max_ms` | `1`, `2` | Las anchuras de pulso de `angulo_min` y de `angulo_max`, entre 0,3 y 3 ms. En medio, en línea recta |
| `continuo` | `no` | `si`: de **giro continuo**. El pulso ya no es una posición sino una velocidad: el del centro del rango lo para, `pulso_max_ms` lo lleva a toda velocidad a la derecha y `pulso_min_ms` a la izquierda. El ángulo da vueltas, de 0 a 360 |
| `velocidad` | `0.12@4.8` | Lo que tarda en girar 60 grados a cada tensión: pares `s@V` separados por blancos —`0.12@4.8 0.10@6`—. Entre dos, en línea recta; fuera, el más cercano; con uno solo, la velocidad es proporcional a la tensión |
| `v_min`, `v_max` | `4`, `7.2` | Por debajo de `v_min` el motor no puede con la reductora y no se mueve; por encima de `v_max`, se avisa |
| `banda_muerta_us` | `10` | Un cambio de pulso menor no lo mueve, de 0 a 200 µs |
| `sin_senal` | `suelta` | Qué hace si deja de llegar señal —tres periodos sin pulso—: `suelta`, el analógico, deja el motor y se queda donde esté; `mantiene`, el digital, sigue hacia el último ángulo |
| `i_reposo_ma`, `i_marcha_ma`, `i_bloqueo_ma` | `6`, `150`, `650` | Lo que gasta quieto, moviéndose y con el eje sujeto, en mA a 5 V |
| `posicion_inicial` | `aleatoria` | Dónde está el eje al arrancar: `aleatoria`, en cualquier punto del rango —redondeado a la décima de grado—, o un ángulo |
| `semilla` | `0` | Fija la posición aleatoria: con la misma semilla, el mismo ángulo, también en otro sistema operativo. `0`, distinta cada vez |
| `aspa` | `dos` | El aspa del eje, para el dibujo: `una`, `dos` —a 180°—, `cuatro` —dos largas y dos cortas, en cruz—, `seis` o `disco`. Una de las palas lleva una marca roja para seguirla con la vista |
| `color` | `azul` | El color del cuerpo en el dibujo: `azul` o `negro` |

**Lo que hace, como el de verdad:**

* mide la anchura de cada pulso y la lleva a un ángulo. Un pulso fuera del
  rango —más allá de la banda muerta— lleva al **extremo**, donde están los
  topes de la reductora, y se avisa; uno fuera de lo que el circuito reconoce
  como pulso, 0,3 a 3 ms, se **ignora**, y se avisa;
* va hacia el ángulo pedido a su **velocidad**, que depende de la **tensión**
  que tiene en VCC en cada momento: una fuente floja que se hunde lo hace ir
  más despacio, y por debajo de `v_min` lo para;
* es una **carga de verdad** sobre VCC —una rama de dos nodos con la
  resistencia que da su corriente a 5 V—: poco quieto, más moviéndose y mucho
  bloqueado. Por eso un servo se alimenta de un rail de 5 V que lo aguante, no
  de un pin;
* sin señal, el analógico suelta y el digital mantiene;
* y al arrancar el eje está **donde se quedó**: el primer pulso lo lleva a su
  sitio, y se le ve llegar.

El eje no se simula paso a paso: su ángulo es una recta en el tiempo entre
dos cambios —un pulso nuevo, la llegada, la tensión, la señal que se pierde—,
y el proceso del motor solo despierta en ellos.

Deja ver `angulo` (grados, el que sugiere pintar: la ventana gira el aspa con
él, el efecto `angulo`), `pulso` (µs), `corriente` (mA) y `rpm`. El **mando**
es `bloquear`, un interruptor: la mano que sujeta el eje. Avisa, una vez cada
cosa, de los pulsos fuera de rango o que no lo son, de un periodo que no es el
suyo, de la sobretensión y de cuando se queda sin señal. Al terminar, `sim`
dice cómo ha quedado: `SERVO S/SERVO: +0.0 grados (objetivo +0.0), pulso 1500
us cada 20.0 ms; quieto; 5.00 V, 6 mA; empezo en -17.4 grados`. Desde C++,
`angulo()`, `objetivo()`, `inicial()`, `pulso_us()`, `moviendose()`,
`con_senal()`, `tension()`, `corriente_ma()`, `rpm()` y `bloquea(si)`.

`placas/servo_sg90.xml` es una placa con el SG90 a 5 V —de −90 a +90 grados
con pulsos de 1 a 2 ms cada 20 ms, `velocidad="0.12@5"`, aspa de cuatro palas
y cuerpo azul— y su conector de tres pines acodado debajo: `VCC`, `GND` y
`PWM`. En el dibujo, junto a cada pin, el color de su hilo en el cable del
servo. Y `placas/nucleo_f446re_servo.xml` la cablea a una Nucleo —el `PWM` a
D5, PB4, el TIM3; el `VCC` a su `+5V`— con un KY-040 que lo mueve 5 grados por
clic y la pantalla TFT de pie, con `verif/fw/servo_demo`.

### 4.13 Las piezas de la BOOSTXL-EDUMKII

Las cuatro que trajo la placa educativa de TI (fase T3 del MSP432,
`parts/boosterpack.h`). La placa entera es `placas/boostxl_edumkii.xml`, con
su dibujo, y enchufada a las LaunchPad, `placas/msp_exp432p401r_edumkii.xml` y
`placas/msp_exp432e401y_edumkii.xml`.

#### `FuenteAjustable`

Una tensión que se mueve con un mando: el cursor de un potenciómetro como su
equivalente de Thevenin. Los ejes de un joystick o de un acelerómetro
analógico, un micrófono en reposo.

| Terminal | | |
| :--- | :--- | :--- |
| `pin` | obligatorio | El nodo al que da la tensión |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `v` | `1.65` | La tensión de partida, en V |
| `vmin`, `vmax` | `0`, `3.3` | El rango del mando |
| `r` | `100` | Su resistencia de salida, en Ω |

Mando `tension` (continuo); desde C++, `pon_tension(v)`.

#### `Zumbador`

Un zumbador magnético tras su transistor: para el pin es una carga a masa, y
escucha el PWM que le llega. Observables `sonando` y `frecuencia` (el periodo
entre dos flancos de subida; deja de sonar si pasan tres periodos sin flanco).

| Terminal | | |
| :--- | :--- | :--- |
| `pin` | obligatorio | La señal que lo mueve |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `r_carga` | `1000` | La carga que ve el pin, en Ω |

#### `Opt3001`

El sensor de luz ambiental de TI por I2C, bit a bit sobre los nodos: RESULT
con exponente y mantisa (0,01·2^E·M lx), CONFIG con la escala automática,
conversiones de 100 u 800 ms, una vez o continuas, CRF, los límites con FH/FL
y la patilla INT de drenador abierto; los identificadores 0x5449 y 0x3001.

| Terminal | | |
| :--- | :--- | :--- |
| `scl`, `sda` | obligatorios | El bus |
| `int` | opcional | La interrupción |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `dir` | `0x44` | Dirección de 7 bits |
| `lux` | `300` | La luz de partida; tiene mando |

#### `Tmp006`

El termómetro de infrarrojos de TI por I2C: VOBJECT (la termopila, 156,25 nV
por cuenta), TAMBIENT (1/32 °C), CONFIG con el modo, el ritmo de conversión y
DRDY, y su patilla; los identificadores 0x5449 y 0x0067. La tensión de la
termopila es S0·(Tobj⁴ − Tchip⁴), con S0 = 6,4·10⁻¹⁴ y sin los términos de
segundo orden.

| Terminal | | |
| :--- | :--- | :--- |
| `scl`, `sda` | obligatorios | El bus |
| `drdy` | opcional | Dato listo, activa baja |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `dir` | `0x40` | Dirección de 7 bits |
| `t_chip`, `t_objeto` | `25`, `25` | Las temperaturas, en °C; tienen mando |

### 4.14 Los sensores de la STM32F3DISCOVERY

Los de la placa del F303 (fase S4, `parts/mems_st.h`): una `Postura` que
dice cómo está la placa y cuatro MEMS de ST que la leen, dos por revisión
de la PCB. La placa entera es `placas/stm32f3discovery.xml`.

**Los ejes de la placa**: x hacia el **este** (LD7), y hacia el **norte**
(LD3, el lado del ST-LINK), z saliendo por la cara de los componentes; la
serigrafía de la rosa dice N, E, S y W. Cada sensor dice los suyos respecto a
esos con `ejes="..."`: tres ejes de la placa con su signo (`"y,-x,z"`: su x es
la y de la placa y su y, la −x). Tiene que ser una rotación —tres distintos y la
regla de la mano derecha— o el fichero no valida.

**Las revisiones**: un `<componente>` con `revision="A-D"`, `"E-"` (de la E
en adelante) o `"E"` solo se monta si la placa es de esas. La placa dice la
suya en la raíz (`<placa ... revision="E">`), y un sistema puede cambiarla
para una de sus placas (`<placa id="D" fichero="..." revision="D">`; véase
`placas/stm32f3discovery_d.xml`). Dos componentes de revisiones distintas
pueden llamarse igual —`U5` es el L3GD20 en una y el I3G4250D en la otra—,
`--valida` dice la revisión de cada placa, y el dibujo rotula el chip que se
monta como variante (`U5@L3GD20`).

#### `Postura`

La orientación de la placa: **alabeo** (el borde este hacia abajo, positivo),
**cabeceo** (el borde norte hacia arriba, positivo, de −90 a 90) y **rumbo**
(hacia dónde mira el norte de la placa, en grados desde el norte magnético).
Un mando continuo cada uno: al moverlo, la placa **gira** hasta ahí a
`velocidad` grados por segundo, en pasos de 1 ms, y de ese giro sale la
velocidad angular que ven los giróscopos; parada, cero. De la orientación sale
lo que ven los acelerómetros —1 g hacia arriba en reposo, la fuerza
específica— y las brújulas —el campo de la Tierra, `campo` gauss con
`inclinacion` grados hacia abajo—. No tiene patillas: los sensores la nombran
con `<ref nombre="postura" componente="MOV"/>`. En la ventana, además de sus
tres deslizadores, lleva un mando de **inclinación** —un círculo con un punto
que se arrastra hacia el lado que baja—, que sale en el panel de cualquier
pieza con un `alabeo` y un `cabeceo` continuos y mueve esos dos deslizadores;
y en el dibujo de la Discovery es una **aguja de rumbo** (`observable=`, §2.6).

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `alabeo`, `cabeceo`, `rumbo` | `0` | La de partida, en grados |
| `velocidad` | `90` | A cuántos grados por segundo gira hacia lo que piden los mandos |
| `campo` | `0.45` | El campo de la Tierra, en gauss |
| `inclinacion` | `55` | Su inclinación, en grados (la de Madrid) |
| `temperatura` | `25` | La de los sensores, en °C |

Observables `alabeo`, `cabeceo`, `rumbo` (los que sugiere pintar) y `giro`
(dps, el módulo). Desde C++, `pon(alabeo, cabeceo, rumbo, ya)` —con `ya`, de
golpe— y `moviendose()`.

#### `L3gd20`, `I3g4250d`

Los giróscopos de las revisiones A-D (`WHO_AM_I` 0xD4; con `quien="0xD5"`, el
L3GD20_TR que el BSP también acepta) y E (0xD3): el mismo mapa, de 0x0F a
0x38, por **SPI de cuatro hilos en modo 0 o 3** (R/W en el bit 7 del primer
byte, el autoincremento en el 6). Las cuatro tasas (95 a 760 Hz), los tres
fondos de escala (8,75 / 17,5 / 70 mdps por cuenta), `PD` y los ejes, STATUS
con sus sobrescrituras, BDU y BLE, la temperatura (−1 cuenta por grado, 0 a
25 °C [de modelo]), el paso alto de primer orden con `HPCF` cuando `Out_Sel` lo
pide, INT1 con sus umbrales (OR o AND, con LIR; sin la duración) y DRDY en
INT2, en contrafase o drenador abierto y activas altas o bajas. MISO, en alta
impedancia salvo cuando saca un dato. La FIFO, el SPI de tres hilos y el modo
I2C no están: los dos primeros avisan.

| Terminal | | |
| :--- | :--- | :--- |
| `spc`, `sdi`, `sdo`, `cs` | obligatorios | El SPI |
| `int1`, `drdy` | opcionales | INT1 y DRDY/INT2 |

Parámetro `ejes` (`x,y,z`); referencia `postura`. Observables `giro_x`,
`giro_y` y `giro_z` (dps, la última muestra).

#### `Lsm303dlhc`, `Lsm303agr`

Las brújulas con acelerómetro de las revisiones A-D y E, por **I2C**: dos
esclavos en un encapsulado, el acelerómetro en 0x19 y el magnetómetro en 0x1E
(el bit 7 de la subdirección pide el autoincremento en el primero; el segundo
avanza siempre). El acelerómetro, el mismo en los dos (`WHO_AM_I_A` 0x33, que
la hoja de datos del DLHC no cuenta): las tasas de 1 a 5376 Hz, HR (12 bits),
normal (10) y LPen (8), justificados a la izquierda, los cuatro fondos de
escala con las sensibilidades de cada hoja de datos, el paso alto con FDS,
BDU, BLE, INT1 e INT2 con sus umbrales y el DRDY en INT1. El magnetómetro del
DLHC: CRA/CRB/MR, X-Z-Y con el byte alto primero, 12 bits y −4096 si se sale,
`IRA/IRB/IRC` = 'H43' y la temperatura; el del AGR, estilo LIS2MDL:
`WHO_AM_I_M` 0x40, `CFG_REG_A/B/C_M`, el byte bajo primero y 1,5 mG por
cuenta, una vez o continuo. La FIFO, el click y el 6D avisan; los pull-up los
pone la placa.

| Terminal | | |
| :--- | :--- | :--- |
| `scl`, `sda` | obligatorios | El bus |
| `int1`, `int2` | opcionales | Las interrupciones del acelerómetro |
| `drdy` | opcional | El dato listo del magnetómetro |

Parámetros `ejes_a` y `ejes_m` (`x,y,z`); referencia `postura`. Observables
`acel_x/y/z` (mg) y `campo_x/y/z` (mG).

### 4.15 Lo que `mcu-sim-gui` puede ver y tocar

Desde la fase 1 del plan de `mcu-sim-gui` (P-12), una pieza puede **declarar**
qué deja ver —sus *observables*— y qué se le puede hacer —sus *mandos*—. La
pantalla no ve nada más: ni la tensión de un pin cualquiera ni un registro del
MCU. Lo declara la propia pieza, en `parts/ext_parts.h`, y el catálogo que la
GUI recibirá se construye recorriendo el inventario, así que una pieza que
empiece a declarar algo aparece sola.

Lo declaran estas:

| Pieza | Observables | Mandos |
| :--- | :--- | :--- |
| `Led` | `encendido` (0/1, el que sugiere pintar) y `corriente` (mA, de 0 a 25) | — |
| `RGBLed` | `encendido` (0/1, alguno), `rojo`, `verde` y `azul` (0/1, los que sugiere pintar) y `corriente_r`, `corriente_g` y `corriente_b` (mA, de 0 a 25); los seis con **`color=`**, sus canales | — |
| `Button` | `pulsado` (0/1): el **dedo**, no el contacto, que en un NC es lo contrario | `pulsar` (botón, 0 suelta, 1 pulsa) |
| `Crystal` | `presente` (0/1): si está soldado | — |
| `Fuente`, `Gnd` | `corriente` (mA, la que entrega o recibe; la escala es ± el límite, o ±100 sin él; con `media_ms`, de media) y `sobrecorriente` (0/1, **una alarma**: el catálogo la marca con `alarma="si"` y la ventana la pinta en rojo); con `media_ms`, también `tension` (V, de media) | — |
| `Xds110` | `corriente` (mA, la que entrega el LDO), `sobrecorriente` (alarma) y `depurando` (0/1, hay un OpenOCD conectado) | — |
| `Encoder` | `posicion` (de 0 a `pasos − 1`, la que sugiere pintar), `cuenta`, `contacto_a` y `contacto_b` (0/1) | `girar` (discreto, la cuenta de clics, de −30000 a 30000) |
| `Tft128x160` | `encendida` (0/1, la que sugiere pintar) y `luz` (mA, de 0 a 40); y la IMAGEN `pantalla`, de 128x160 | — |
| `Servo` | `angulo` (°, de `angulo_min` a `angulo_max` o de 0 a 360, el que sugiere pintar), `pulso` (µs), `corriente` (mA) y `rpm` | `bloquear` (interruptor: sujeta el eje) |
| `Postura` | `alabeo`, `cabeceo`, `rumbo` (°, los que sugiere pintar) y `giro` (dps) | `alabeo`, `cabeceo` y `rumbo` (continuos: la placa gira hasta ahí) |
| `L3gd20`, `I3g4250d` | `giro_x`, `giro_y`, `giro_z` (dps) | — |
| `Lsm303dlhc`, `Lsm303agr` | `acel_x/y/z` (mg) y `campo_x/y/z` (mG) | — |
| `MotorCC` | `angulo` (°, de 0 a 360, el que sugiere pintar), `rpm`, `corriente` (mA) y `tension` (V), las dos de media; con encoder, `cuenta`, `enc_a` y `enc_b` | `bloquear` (interruptor: la mano en la rueda) y `carga` (continuo, de 0 a 100 % del par de bloqueo) |
| `L298` | `corriente_a`, `corriente_b` y `corriente_vs` (mA, de media), `disipacion` (W) y `logica` (0/1) | — |
| `Regulador` | `tension` (V), `corriente` (mA) y `sin_margen` (**alarma**) | — |

**Una pieza puede enseñar además una IMAGEN entera** —la pantalla de un
TFT—, que no cabe en un número (`Imagen` en `parts/part_base.h`). El catálogo
la declara como `<imagen>`, con un `id_obs` del mismo espacio que los
observables pero detrás de todos ellos —así los de siempre no se mueven—, y
la ventana la pide en la misma suscripción. No va en las instantáneas: en
cada instante de la rejilla, si ha cambiado —su contenido o su luz— desde la
última que salió, sale en su propio mensaje, `T_IMAGEN`, con los píxeles en
RGB888 y la luz aparte (`doc/protocolo.md` §4.1 en `mcu-sim-gui`). Con dos
esperando a una ventana que no lee, la siguiente no se toma: la muestra de
después mandará la que haya.

`Crystal` no publica la frecuencia por lo mismo que no la lleva como atributo:
la del HSE es un dato del árbol de reloj y vive en el RCC.

Desde la fase 5 los mandos se **accionan** desde la ventana: una orden dice
pieza, mando y valor, y llega a `acciona()` en su instante simulado. El rango
de cada mando es el que la pieza declara; una orden que se sale se recorta a él
y se aplica, y el modelo lo avisa (`doc/protocolo.md` §5 en `mcu-sim-gui`).
Para la pieza no hay diferencia entre eso y que el programa de pruebas llame al
método de siempre: `acciona(pulsar, 1)` hace lo mismo que `press()`.

Las demás no declaran nada todavía, y no les hace falta para
compilar: los seis métodos de `ExtPartBase` tienen valores por omisión. Las
piezas que el enunciado de la GUI necesita y no existen —`PwmMeter` y
`StepperDriver`— están en `doc/analisis_gui.md` §8; el `Encoder` (§4.1), el
`Servo` (§4.12) y el motor de continua con su puente en H, `MotorCC` y `L298`
(§4.16), ya están.


### 4.16 El motor de continua y su puente en H

Las tres piezas del robot de prácticas (`parts/motor_cc.h` y
`parts/puente_h.h`): el motorreductor, el L298 que lo mueve y el regulador
que alimenta la lógica del módulo L298N. Montadas, son `placas/l298n.xml`,
`placas/motor_tt_encoder.xml` y `placas/fuente_laboratorio.xml`, y con la Nucleo,
`placas/nucleo_f446re_encoder_lcd_servo_2xmcc.xml`.

#### `MotorCC`

Un **motor de corriente continua con reductora** y, si se le dan sus cuatro
patillas, un **encoder incremental** en el eje de salida. Las cifras de
omisión son las del motorreductor TT amarillo de 1:48 y 3 a 6 V [Adafruit
3777: 250 rpm y 160 mA en vacío y 1,5 A bloqueado a 6 V].

**Lo eléctrico.** Entre `a` y `b`, una f.e.m. `E = Ke·ω` en serie con la
resistencia `R` y la **inductancia** `L` del bobinado:

    Va − Vb = E + R·i + L·di/dt

Con `a` más alto gira a la **derecha** —en el sentido de las agujas del reloj,
mirando el eje de salida desde su extremo—, que es el ángulo positivo. Un
motor que gira **genera**, y con un borne al aire enseña su f.e.m. en él.

**La bobina no deja que la corriente cambie de golpe.** Con PWM, sube y baja
dentro de cada periodo con la constante `L / (R + lo de fuera)`, unos 0,3 ms
tras un L298N. Cuando el puente suelta los bornes, la corriente sigue por
donde pueda: por los **diodos de libre circulación** del módulo L298N (`L298`
con `diodos="si"`), contra toda la tensión de la alimentación, y se acaba en
unas decenas de µs. Si no hay por dónde, se corta. Para la red, el motor es
una rama entre dos nodos que deja pasar la corriente de su bobina: una f.e.m.
tal que en los bornes quede `Vth − Rth·i`.

**Lo mecánico**, en el eje de salida y con la reductora dentro:

    J·dω/dt = Kt·i − Tf·sgn(ω) − Tc·sgn(ω)

con `Kt = Ke`, un rozamiento seco `Tf` —el que se lleva la corriente en
vacío—, uno de **arranque** mayor que hay que vencer para que se mueva parado
—el de `i_arranque_ma`— y la carga `Tc` del mando `carga`. La inercia `J` sale
de la constante de tiempo. Las cifras se dan como en una hoja de datos y de ahí
sale el resto: `R = v_nominal / i_bloqueo`, `Ke = (v_nominal − i_vacio·R) /
ω_vacio`.

Esa ecuación es de su **cadena mecánica** (§4.17): el motor da el par `Kt·i` y
pone su `J`, su `Tf`, su rozamiento de arranque y su `Tc`, y la cadena les suma
lo que tenga detrás. Un motor suelto lleva la suya, de él solo, y es lo de
siempre al decimal: M9 y M10 dan las mismas rpm que antes de la cadena.

**Cómo se integra.** Las dos ecuaciones, la de la bobina y la del eje, juntas,
con Runge-Kutta de cuarto orden a pasos de 5 µs como mucho —y nunca más de un
quinto de `L/(R + Rth)`—. Pero SystemC no despierta en cada paso: es pura
cuenta. El proceso despierta en cada cambio de los bornes; en cada flanco del
encoder y cuando la corriente cambia de sentido —que es cuando se apagan los
diodos—, ensayados de antemano con los mismos pasos, así que caen en su
instante; cuando se para; y cada 0,1 ms mientras la corriente se asienta o
cada milisegundo mientras acelera, para que la corriente y la f.e.m. que ven
los demás sigan al motor.

**Lo que se ve con el PWM.** Con el L298N a 1 kHz y sus diodos, el 50 % da
unas 145 rpm de las 243 del 100 %. Casi sin inductancia —`l_mh="0.01"`— daría
unas 200: la corriente se cortaría con el PWM y el motor seguiría, en cada
hueco, con lo que tenía. Sin los diodos, la corriente de la bobina se corta al
soltar el puente, y va más despacio aún (`make gui-marcha`, M10). La
inductancia del TT no la da su hoja de datos: 1,5 mH es una estimación —los
motores de su tamaño andan entre 0,5 y 3 mH—, y se cambia con `l_mh`.

| Terminal | | |
| :--- | :--- | :--- |
| `a`, `b` | obligatorios | Los bornes: con `a` más alto, gira a la derecha |
| `enc_vcc`, `enc_gnd` | con `ppr` | La alimentación del encoder. Con menos de 2,5 V entre los dos, sus salidas se quedan al aire |
| `enc_a`, `enc_b` | con `ppr` | Las salidas, en cuadratura: girando a la derecha A va delante —00, 10, 11, 01—; a la izquierda, B. De colector abierto con un pull-up de 10 kΩ a `enc_vcc`: altas con 10 kΩ, bajas con 100 Ω |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `v_nominal` | `6` | La tensión a la que se dan las demás cifras, en V |
| `rpm_vacio` | `250` | Las rpm del eje de salida en vacío, a `v_nominal` |
| `i_vacio_ma` | `160` | La corriente en vacío, a `v_nominal`: el rozamiento |
| `i_bloqueo_ma` | `1500` | La corriente con el eje sujeto, a `v_nominal`: la resistencia |
| `i_arranque_ma` | `250` | La que hace falta para que arranque parado, de `i_vacio_ma` a `i_bloqueo_ma` |
| `tau_ms` | `50` | La constante de tiempo mecánica en vacío: lo que tarda en llegar al 63 % de su velocidad. De ella sale la inercia |
| `l_mh` | `1.5` | La inductancia del bobinado, de 0,001 a 1000 mH |
| `reduccion` | `48` | La de la reductora: solo para decir las rpm del motor |
| `v_max` | `7.2` | Lo que aguanta: por encima, se avisa |
| `ppr` | `0` | Las ventanas del disco del encoder, los pulsos de cada canal por vuelta del eje de salida. `0`, sin encoder y sin sus patillas; con él, las cuatro |
| `enc_i_ma` | `20` | Lo que gasta el sensor del encoder a 3,3 V |
| `posicion_inicial` | `0` | El ángulo del eje al arrancar, en grados |

Deja ver `angulo` (de 0 a 360, el que sugiere pintar: la ventana gira el
elemento de la pieza con él, el efecto `angulo`, como el disco de
`placas/motor_tt_encoder.svg`), `rpm`, y la `corriente` y la `tension` **de
media** —un filtro de primer orden de 5 ms: lo que marcaría un polímetro con
PWM—; con encoder, `cuenta` —los cuartos de ciclo, con signo: 4·`ppr` por
vuelta— y `enc_a` y `enc_b`. Los **mandos**: `bloquear`, la mano que sujeta la
rueda, y `carga`, un par que se opone al giro, en % del de bloqueo. Avisa de la
sobretensión. Al terminar, `sim` dice cómo ha quedado: `MOTOR MA/MOTOR: +237.8
rpm (+11413 en el motor), +5.74 V y +160 mA de media; +3.72 vueltas; encoder
+357 cuartos (24 PPR)`. Desde C++, `rpm()`, `rpm_motor()`, `angulo()`,
`vueltas()`, `corriente_ma()` y `tension()` —las de ahora—,
`corriente_media_ma()` y `tension_media()`, `fem()`, `cuenta()`, `enc_a()`,
`enc_b()`, `bloquea(si)` y `pon_carga(0..1)`.

`placas/motor_tt_encoder.xml` es el motorreductor con un disco de 24 ventanas
de través en el eje de salida y la plaquita de sus dos sensores ópticos: 24
PPR, 96 cuartos por vuelta del eje. Sus terminales son `M.ROJO` (+) y
`M.NEGRO`; la tira del encoder, `P1.VCC`, `P1.GND`, `P1.A` y `P1.B`.

#### `L298`

El **doble puente en H** de ST [DS0218]: dos puentes, A y B, para dos motores
de continua (o un paso a paso bipolar).

| ENA | IN1 | IN2 | OUT1 | OUT2 | El motor |
| :---: | :---: | :---: | :---: | :---: | :--- |
| alto | alto | bajo | VS | masa | hacia delante |
| alto | bajo | alto | masa | VS | hacia atrás |
| alto | igual | igual | VS o masa | VS o masa | en corto: **frena** |
| bajo | — | — | al aire | al aire | **libre** |

Igual ENB, IN3, IN4, OUT3 y OUT4. Las entradas son **TTL** —bajo hasta 1,5 V,
alto desde 2,3 V—: valen los 3,3 V de un STM32. Entre los dos, la entrada se
queda como estaba y se avisa.

**Las salidas no llegan a VS ni a masa**: son Darlington, y caen lo que dice
la hoja de datos —arriba 1,35 V con 1 A y 2 V con 2 A; abajo 1,2 y 1,7—. En
línea recta, eso es una fuente de `VS − 0,7 V` con 0,65 Ω arriba y de 0,7 V
con 0,5 Ω abajo: con un motorreductor en vacío, unos 1,6 V menos de los que da
la fuente, y casi 3,2 con 1 A. Es lo que más sorprende del L298 la primera
vez.

Lo que sale por las salidas altas lo pide a `vs`, más 13 mA con algún puente
habilitado (4 sin ninguno); la lógica, a `vss`, 24 mA (6 sin habilitar). Con
menos de 4,5 V en `vss` las salidas se quedan al aire, y se avisa. Pasar de
2 A por una salida se avisa: el L298 no se protege. Los SENSE A y B van a masa
por dentro.

**Los diodos de libre circulación** no son del chip: un motor los necesita por
fuera, y el módulo L298N los lleva. Con `diodos="si"`, una salida que se
suelta llevando la corriente de la bobina de un motor la sigue llevando por su
diodo —la que salía, desde masa, a −0,9 V; la que entraba, hacia VS, a VS +
0,9 V, con 0,2 Ω— hasta que esa corriente cambia de sentido, y entonces se
suelta de verdad. La que vuelve a VS no se descuenta de lo que se le pide.

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `diodos` | `no` | `si`: los diodos de libre circulación, por fuera del chip |

| Terminal | | |
| :--- | :--- | :--- |
| `vs` | obligatorio | La alimentación de los motores |
| `vss` | obligatorio | La de la lógica, de 4,5 a 7 V |
| `gnd` | obligatorio | La masa |
| `ena`, `in1`, `in2` | obligatorios | El puente A |
| `enb`, `in3`, `in4` | obligatorios | El puente B |
| `out1` … `out4` | obligatorios | Las salidas: `out1` y `out2` del A, `out3` y `out4` del B |

Deja ver `corriente_a` y `corriente_b` —la de cada motor, la que sale por OUT1
y OUT3—, `corriente_vs` —lo que pide a VS—, `disipacion` —lo que se queda en
el chip, en W—, las cuatro de media, y `logica`. Al terminar: `L298 L/U1: A
adelante (+160 mA), B adelante (+160 mA); VS 333 mA, VSS 24 mA; disipa 0.72 W
(como mucho 6.58), de media`. Desde C++, `logica()`, `habilitado(p)`,
`estado(p)` —`adelante`, `atras`, `freno` o `libre`—, `corriente_ma(p)`,
`corriente_salida(k)` —la de ahora—, `corriente_vs_ma()`, `disipacion()` y
`disipacion_max()`.

#### `Regulador`

Un **regulador lineal** de tres patillas, como el 78M05 de los módulos L298N:
da `v` voltios mientras la entrada le deje `caida` de margen; con menos, la
entrada menos la caída, y lo deja ver como una alarma, `sin_margen`, cuando se
queda 50 mV corto. Lo que entrega lo pide a la entrada, más su corriente de
reposo.

| Terminal | | |
| :--- | :--- | :--- |
| `entrada`, `salida`, `gnd` | obligatorios | |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `v` | `5` | La tensión de salida |
| `caida` | `2` | El margen que necesita, en V |
| `limite_ma` | `500` | Lo que da: pasar de ahí se avisa |
| `iq_ma` | `5` | Su corriente de reposo |
| `r` | `0.1` | La resistencia de su salida |

`placas/l298n.xml` es el módulo rojo entero: el L298 (`U1`), el 78M05 (`U2`,
con `caida="1.7"`, la suya a los 30 mA de la lógica), el LED de los 5 V
(`D1`), las bornas de los motores (`MA.OUT1`, `MA.OUT2`, `MB.OUT3`,
`MB.OUT4`), la de la alimentación (`X1.12V`, `X1.GND`, `X1.5V`), la tira de
abajo con los **puentes de ENA y ENB** puestos de fábrica (`JA` y `JB`, que se
quitan con `<jumper ref="L/JA" puente="no"/>` para darles PWM) y el **5V-EN**
(`JP5`), con `diodos="si"` en el L298. `placas/fuente_laboratorio.xml` es la
fuente de mesa que alimenta los motores: una `Fuente` de 7,5 V con el límite
en 3 A y `media_ms="100"`, que deja ver la corriente y la tensión de media, y
la ventana las escribe DENTRO de su display —el efecto `display`, §2.6—:
`7.474 V` arriba y `0.340 A` abajo. En
`placas/nucleo_f446re_encoder_lcd_servo_2xmcc.xml` el 5V-EN se quita y la
lógica del L298N toma el +5V de la Nucleo: la fuente enseña lo que gastan los
motores, más el reposo del L298 y del 78M05 (de 9 a 18 mA).

### 4.17 La cadena mecánica: `<transmision>` y `Volante`

`parts/cadena.h`, la fase F0 de `doc/analisis_cnc.md` (§4, opción C). Hasta
aquí cada motor llevaba su carga dentro. Una máquina necesita que el par pase
por una reductora, un acople, un husillo y una tuerca hasta un carro, y que
todo eso vuelva al motor como inercia y como par resistente.

**Una sola masa.** Con acoples rígidos —las resonancias torsionales de varios
kHz no interesan aquí—, toda la cadena es un solo grado de libertad: el ángulo
y la velocidad de su **eje de referencia**, que es el de los motores. Cada cosa
se refleja en él:

    J = Σ J_motor + Σ J_k / n_k²
    F = Σ F_motor + Σ F_k / (n_k · η_k)

con `n_k` las vueltas del eje de referencia por vuelta de la entrada del
elemento `k` —el producto de las relaciones de lo que hay antes de él— y `η_k`
el rendimiento de eso mismo. La cadena integra **una sola ecuación mecánica**
con las corrientes de todos sus motores:

    J·dω/dt = Σ T_motor(i, ω, θ) − F·sgn(ω) − B·ω

Parada, no se mueve mientras el par no pase de la suma de los rozamientos de
**arranque** de cada pieza; en marcha, se opone al giro con los de marcha.

**Las piezas son de dos clases.** Un **actuador** es un motor: tiene estados
eléctricos —la corriente de su bobina— y da un par; sus bornes y sus mandos
despiertan a la cadena. Un **elemento** es pasivo: una inercia, unos
rozamientos y una relación de transmisión, con sus cifras en su entrada, el eje
que da al motor. Hoy hay un elemento, el `Volante`; la reductora, el acople, el
husillo y el carro son de la fase F2.

**Cómo se integra.** Como lo hacía el `MotorCC`, generalizado: Runge-Kutta de
cuarto orden en todo, a pasos de 5 µs como mucho —o menos, si lo pide la parte
eléctrica de algún motor—. El ángulo, con Runge-Kutta si algún motor da un par
que depende de él —el paso a paso—; si no, con la regla del trapecio sobre la
velocidad, que es lo que hacía el `MotorCC`. SystemC despierta al proceso de
la cadena en los cambios de los bornes de sus motores, en sus mandos, en lo que
cada motor diga que hay que hacer ver —un flanco de su encoder, un cambio de
sentido de su corriente—, ensayado de antemano con los mismos pasos, cuando se
para y cada 0,1 o 1 ms mientras se mueve.

**El motor suelto.** Un motor que no va en ninguna `<transmision>` lleva su
cadena **implícita**, de él solo, con su carga de siempre, y la mueve su propio
proceso. Si una transmisión lo engancha, la mueve el proceso de ella. Con su
cadena implícita el `MotorCC` es el de antes **bit a bit**: un banco del motor
tras el L298 —a tensión fija, con PWM de 1 kHz, casi sin inductancia y sin los
diodos, con carga, bloqueado y suelto— da los mismos números con los 17
decimales, y M9 y M10 las mismas rpm.

#### `<transmision>`

    <componente tipo="MotorCC" id="M" .../>
    <componente tipo="Volante" id="V" inercia_gcm2="500"/>
    <transmision id="EJE" cadena="M V"/>

`cadena=` nombra las piezas **en orden**: primero los motores —uno, o hasta
cuatro en el mismo eje, como los dos de un pórtico—, después los elementos, del
motor hacia la carga. Va detrás de todas las que nombra, como cualquier
referencia, y es una pieza `Transmision` cuyas `<ref>` son `e0`, `e1`... (así
se vuelca). Son errores, con su nombre: una pieza que no se ha declarado antes,
una que no tiene eje (`MASA es un Gnd, que no tiene eje`), un motor detrás de
un elemento, una pieza en dos transmisiones y una cadena sin motores. No se
dibuja (`visible="no"` de oficio).

Deja ver `angulo` y `rpm` del eje de referencia e `inercia`, toda la reflejada
en él, en g·cm². Los mandos de cada motor —`bloquear`, `carga`— siguen siendo
suyos y actúan sobre la cadena entera. Al terminar: `TRANSMISION EJE: +234.2
rpm, +0.83 vueltas; 1 motor(es) y 1 elemento(s), 10479.3 g·cm2 reflejados`.
Desde C++, `cadena()`: `rpm()`, `th()`, `w()`, `inercia()`, `relacion_total()`,
`actuadores()` y `elementos()`.

#### `Volante`

Una inercia en un eje, con su rozamiento seco y viscoso, y nada más. Sin
patillas: lo une al motor la transmisión que los nombra. Con uno de inercia
conocida, la constante de tiempo del motor es la de la cuenta, `τ = (J_motor +
J_volante)·R / (Ke·Kt)`: con uno igual que el TT, el doble (`make gui-cnc`,
C1). Suelto, no se mueve.

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `inercia_gcm2` | — | Su inercia, en g·cm² (1 g·cm² = 10⁻⁷ kg·m²). O esta, o las dos de abajo |
| `diametro_mm`, `espesor_mm` | — | Un disco macizo de acero, 7850 kg/m³: `J = m·r²/2` |
| `rozamiento_ncm` | `0` | Su rozamiento seco, en N·cm: el de sus rodamientos |
| `viscoso_mnms` | `0` | Su rozamiento viscoso, en mN·m por rad/s |

Deja ver `angulo` —el que sugiere pintar: la ventana lo gira— y `rpm`.

### 4.18 El paso a paso, sus drivers y un generador de pasos

`parts/motor_pap.h` y `parts/driver_pap.h`, la fase F1a de
`doc/analisis_cnc.md` (§5.1 y §5.2). Montadas, son `placas/nema17_3018.xml`,
`placas/st5918l3008.xml` y `placas/pololu_a4988.xml`.

#### La corriente impuesta con tope de tensión

Un driver de paso a paso de verdad es un **chopper**: conmuta su puente a 20 o
40 kHz para que la corriente de cada fase siga la consigna del micropaso.
Simularlo conmutando costaría cientos de miles de sucesos por segundo con tres
ejes, y por eso cada fase del driver no es un Thevenin sino una **rama de
corriente impuesta** (`RamaCorriente`, `parts/ext_parts.h`): «quiero `i*`, y
puedo poner entre mis dos salidas de −VMOT a +VMOT». Lo **declara** en los
nodos de sus dos salidas —un polo en cada uno— por un canal aparte del nodo
(`FaseCorriente` y `declara_salida_corriente` en `common/analog_net.h`, como
`declara_reloj` declara una frecuencia). La bobina que encuentra esas dos
salidas en sus bornes integra su corriente sabiéndolo:

* **si la tensión que haría falta, `R·i* + f.e.m.`, cabe en el tope, la
  corriente es la consigna**: lo que consigue un chopper bueno;
* si no, la tensión se queda en el tope —`±VMOT`— y la corriente va por
  detrás: es la **caída de par con la velocidad**;
* con el driver apagado, la corriente vuelve al bus por sus diodos, contra
  `VMOT + 2·Vd`, hasta acabarse.

El driver despierta en los flancos de STEP, en los cambios de sus entradas y de
sus alimentaciones, y cada milisegundo mientras conduce. No hay sucesos de
chopper, y su rizado se pierde. Lo que gasta lo deja la bobina en la rama, ya
**integrado** —la energía que ha entrado en ella, por su balance: lo que se
queda en R, lo que va a la inductancia y lo que se convierte en par, y la
integral de `i²`—, porque con un chopper la potencia de cada instante salta de
signo dentro de cada micropaso. El driver pide a VMOT esa potencia de media,
más `I²·RDS(on)` de sus transistores y su reposo: el chopper no se simula,
pero se paga. Lo que vuelve de las bobinas al frenar no se lleva a ninguna
parte: una fuente de laboratorio no lo absorbe, y es de la fase F5.

Dentro de cada paso de integración el régimen de cada bobina no cambia —sigue
a la consigna, sube o baja contra el tope, o vuelve por los diodos—: un
cambio de régimen a mitad de un paso de Runge-Kutta no se integra. Cuando la
corriente cruza su consigna, se queda en ella.

#### `MotorPaP`

Un **híbrido bipolar de dos fases**, con `Nr = pasos/4` dientes de rotor:

    va = R·ia + L·dia/dt − Km·ω·sin(Nr·θ)
    vb = R·ib + L·dib/dt + Km·ω·cos(Nr·θ)
    T  = −Km·ia·sin(Nr·θ) + Km·ib·cos(Nr·θ) − Td·sin(4·Nr·θ)

Con la corriente de `a1` a `a2` el rotor va a 0; con la de `b1` a `b2`, un paso
a la derecha. Cada bobina va **como esté cableada**, y se mira en cada vuelta:
entre las dos salidas de una fase de un `DriverPaP`, con corriente impuesta;
entre nodos de Thevenin —un `L298`—, como un motor de continua (`RamaFem`); al
aire, sin corriente, y con la otra movida se avisa («con una fase sola el
motor no gira»); entre salidas de dos fases o de dos drivers, sin corriente, y
se avisa. Las dos salidas al revés: gira al revés.

Lo mecánico es de su cadena (§4.17): el par, la inercia del rotor, un
rozamiento seco y uno viscoso, y la carga del mando. El par depende del ángulo,
así que la cadena integra el ángulo con Runge-Kutta, a pasos de **10 µs**
como mucho —el doble de largos que los del `MotorCC`: la corriente sigue a su
consigna sin integrar nada, y lo que queda son la mecánica y las subidas de la
corriente contra el tope, de un kHz y pico; con 5 µs la curva de par máximo
cambia menos de un 2 %—. No ensaya nada para despertar en su instante: despierta
en cada flanco de STEP de todas formas.

**Los pasos perdidos.** El motor sabe dónde tendría que estar: donde lo llevan
las corrientes que le piden, `Nr·θ* = atan2(ib*, ia*)`, seguido vuelta a vuelta.
La primera vez que tiene corriente se queda con el equilibrio más cercano; desde
ahí, el **retraso** es lo que el rotor va por detrás, en pasos enteros, y un
rotor que se descuelga cae en el equilibrio de al lado: **cuatro pasos** —una
vuelta eléctrica— cada vez. Eso son los `pasos_perdidos`, con su alarma.

**El modelo ideal** (`modelo="ideal"`): cada micropaso es exactamente un
micropaso, sin corrientes ni pérdidas; el rotor va a donde dicen las consignas
(`Cadena::impone`). Para probar firmware deprisa.

**Un motor de ocho hilos.** Nanotec da las cifras de los suyos como se miden en
unipolar, por media bobina. `conexion="serie"` las convierte a bipolar en serie
—`2R`, `4L`, `I/√2`— y `conexion="paralelo"` a bipolar en paralelo —`R/2`, `L`,
`I·√2`—; el par de retención es el mismo. El ST5918L3008 da 3 A, 1 Ω y 1,9 mH;
en paralelo son 4,2 A, 0,5 Ω y 1,9 mH. `Km` sale del par de retención, medido
con las dos fases: `Km = T/(I·√2)`.

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `par_retencion_ncm` | `25` | El par de retención con las dos fases a la corriente nominal, en N·cm |
| `corriente_a` | `1.3` | La corriente nominal por fase |
| `r_ohm`, `l_mh` | `2.4`, `2.8` | La resistencia y la inductancia de cada fase |
| `conexion` | `bipolar` | `serie` o `paralelo`: un motor de ocho hilos, con las cifras de media bobina |
| `retencion_una_fase` | `no` | El par de retención, medido con una fase |
| `km` | `0` | La constante de par, en N·m/A; `0`, del par de retención |
| `detent_ncm` | `1.5` | El par sin corriente |
| `inercia_gcm2` | `34` | La del rotor |
| `rozamiento_ncm`, `viscoso_mnms` | `0.5`, `0.1` | Sus rozamientos: seco, en N·cm, y viscoso, en mN·m por rad/s (estimados) |
| `pasos` | `200` | Por vuelta: 200 o 400 |
| `modelo` | `dinamico` | `ideal`: un micropaso por micropaso |

Deja ver `angulo` (el que sugiere pintar: la ventana gira el eje), `rpm`,
`corriente_a` y `corriente_b` —las de ahora—, `par`, `retraso`,
`pasos_perdidos` y la alarma `pierde_pasos`; sus mandos son `bloquear` y
`carga`, en % del par de retención. Al terminar dice dónde ha quedado, con las
rpm y las corrientes de ese instante —a 1/16 y 60 rpm de media, el rotor
vibra con cada micropaso—: `PASO A PASO M/MOTOR: +107.6 rpm, +291.70 pasos
(+1.46 vueltas); A corriente +924 mA, B corriente -604 mA; retraso -0.01 pasos,
0 perdidos`.

#### `DriverPaP`

El chip de un módulo de los de la CNC Shield, o un driver externo, como cuatro
juegos de parámetros (`modelo=`):

| | VMOT | Lógica | Pulso de STEP; DIR antes | Corriente | Micropaso | Protección |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **A4988** (módulo Pololu) | 8–35 V | VDD de 3 a 5,5 V; alto desde 0,7·VDD | 1 µs; 200 ns | `Vref/(8·0,068)`, hasta 2 A | MS1..3: 1, 1/2, 1/4, 1/8, 1/16 | 165 °C |
| **DRV8825** | 8,2–45 V | la suya; alto desde 2,2 V | 1,9 µs; 650 ns | `2·Vref`, hasta 2,5 A | MODE0..2: hasta 1/32 | 150 °C |
| **TMC2209** | 4,75–29 V | VIO de 3 a 5 V; alto desde 0,7·VIO | 100 ns; 20 ns | `Vref·0,325/(2,5·0,13)`, hasta 2,8 A | MS1, MS2: 1/8, 1/32, 1/64, 1/16 | 143 °C |
| **DM542** (externo) | 20–50 V | **optoacoplada**: un LED con 270 Ω, de 7 a 16 mA | 2,5 µs; 5 µs | la de sus interruptores, hasta 4,2 A | la de sus interruptores, hasta 1/128 | — |

Cada flanco de subida de STEP avanza la posición un micropaso hacia donde diga
DIR —alto, hacia delante—. La posición va en 1/256 de paso; la fase eléctrica
es `φ = posición·90°/256` y las consignas, `I·cos φ` y `I·sin φ`. Empieza en 45°,
como el A4988 y el DRV8825: en paso entero, las dos fases al 70,71 %. El
traductor cuenta aunque EN apague las salidas, como el de verdad.

**Lo que comprueba, porque es lo que se equivoca en la vida real**, y cada fallo
es un aviso con nombre, una vez: el ancho del pulso de STEP, alto y bajo; DIR
antes y después del flanco; el nivel de cada entrada —con VDD a 5 V, el A4988
pide 3,5 V para un alto, y los 3,3 V de un STM32 se quedan cortos: funciona,
fuera de especificación—; una entrada al aire; EN, activo a nivel bajo; RESET
al aire —el módulo de Pololu no lleva resistencia, y se queda en reset—; los
pasos en el milisegundo que tarda en despertar de SLEEP; una Vref que pide más
de lo que da el chip; VMOT fuera de su margen —por debajo de su mínimo se
apagan las salidas—; la lógica sin tensión; y la **temperatura**: lo que se
queda en sus transistores, `I²·RDS(on)` por fase, con una resistencia térmica
—la del módulo, sin o con disipador, estimada— y una constante de tiempo de
3 s. Pasar del umbral apaga las salidas hasta que se enfría; con Vref a 1,2 V y
sin disipador, un A4988 parado salta a los 7,6 s. Las entradas del A4988 llevan
por dentro sus resistencias a masa en MS1..3; las del DRV8825 y el TMC2209, en
todas. Las del DM542 son el LED de un optoacoplador: piden su corriente a quien
las mueve.

**Lo que no hace**: el rizado del chopper y sus modos de decaimiento; la
interpolación a 1/256, el UART, el StallGuard, el StealthChop y la reducción
de corriente parado del TMC2209; la protección de cortocircuito de las
salidas; y la energía que vuelve al bus al frenar.

| Terminal | | |
| :--- | :--- | :--- |
| `vmot`, `gnd` | obligatorios | La potencia y la masa |
| `vdd` | A4988 y TMC2209 | La lógica (VIO en el TMC2209) |
| `en` | obligatorio; opcional en el DM542 | Activo a nivel bajo: alto, salidas apagadas |
| `ms1`, `ms2`, `ms3` | `ms3` no en el TMC2209; ninguna en el DM542 | El micropaso |
| `rst`, `slp` | A4988 y DRV8825 | RESET y SLEEP, activos a nivel bajo |
| `step`, `dir` | obligatorios | Paso y dirección |
| `out1a`, `out1b`, `out2a`, `out2b` | obligatorios | La fase 1, de OUT1A a OUT1B, y la 2 |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `modelo` | `A4988` | `A4988`, `DRV8825`, `TMC2209` o `DM542` |
| `vref` | `0.6`, `0.5`, `1.4` | La del potenciómetro, en V (el mando `vref`) |
| `disipador` | `no` | Con disipador, la mitad de resistencia térmica |
| `t_ambiente` | `25` | La temperatura del aire |
| `micropasos`, `corriente_a` | `8`, `2` | Solo en el DM542: sus interruptores |

Deja ver `corriente_1` y `corriente_2` —las consignas—, `micropasos`, `pasos`,
`corriente_vmot` —de media—, `temperatura` y las alarmas `sobretemperatura` y
`sin_tension`. Al terminar: `DRIVER D/U1 (A4988): activo, 1/16, +4659 pasos;
fases +973 y -520 mA de 1.10 A; VMOT 332 mA de media; 44 °C`.

#### `GeneradorPasos`

Un instrumento de banco: STEP y DIR a `hz` pasos por segundo, con una rampa de
aceleración constante —`aceleracion` en pasos/s², o `rampa_ms` de parado a
`hz`—; un cambio de `sentido` en marcha frena, cambia DIR, espera `dir_us` y
vuelve a arrancar. El pulso dura `ancho_us`, y las salidas son de `vdd` voltios
con 50 Ω. Sus mandos son `hz` y `sentido`; deja ver `hz` y `pasos`.

#### Lo que sale

Con el ST5918 en paralelo tras un DM542 de 4,2 A, a medio paso, el **par
máximo** —la carga con la que empieza a perder pasos— a 120 rpm es 1,23 N·m con
24 y con 48 V: `Km·I = 0,31 × 4,2 = 1,31`, menos sus rozamientos. Cae con la
velocidad, y mucho antes a 24 V:

| rpm | 120 | 600 | 900 | 1200 | 1800 |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 24 V | 1,23 | 0,93 | 0,41 | 0,22 | 0,11 |
| 48 V | 1,23 | 1,16 | 1,08 | 0,90 | 0,43 |

La **esquina** —el 70 % del par de abajo— está en unas 640 rpm a 24 V y en
unas 1240 a 48 V, el doble: por eso los Nanotec van a 48 V. Con una rampa de
5 ms hasta 1000 rpm el rotor se descuelga y pierde miles de pasos; con 400 ms,
ninguno. Un eje así, a 1/8 y 3200 Hz, cuesta unos 0,09 s de CPU por segundo
simulado, con el arranque de `mcu-sim` (`make gui-cnc`, C2).

### 4.19 La continua de potencia: `Mosfet` y `MedioPuente`

`parts/potencia.h`, la fase F1b de `doc/analisis_cnc.md` (§5.3): lo que hace
falta para mover el cabezal de una fresadora y un eje servo de continua.
Montadas, son `placas/motor_775.xml`, `placas/modulo_mosfet.xml`,
`placas/ibt2.xml`, `placas/motor_jgb37_encoder.xml` y el sistema
`placas/nucleo_f446re_ibt2_jgb37.xml`. Los motores son `MotorCC` (§4.16) con
otras cifras.

#### `Mosfet`

Un **N-MOSFET de lado bajo**, como el IRLZ44N de los módulos que dan el PWM del
cabezal. Con `Vgs` por encima del umbral, el drenador es un Thevenin hacia la
fuente con una `RDS` que baja con la puerta: a `vgs_ref` es `rds_on`, y cerca
del umbral, mucho más —un MOSFET que no es de nivel lógico apenas se abre con
3,3 V, y con menos de `vgs_plena` y más de 1 A se avisa—. Cerrado, con
`diodo="si"` y la patilla `libre` en el + de la carga, el **diodo de libre
circulación** del módulo: la corriente de la bobina sigue por él, contra
`V(libre) + Vd`. **Sin el diodo**, la bobina sube el drenador hasta la
avalancha y en una placa de verdad lo rompe: aquí la corriente se corta, y se
avisa en el primer hueco.

| Terminal | | |
| :--- | :--- | :--- |
| `drenador` | obligatorio | El − de la carga |
| `fuente` | obligatorio | La masa |
| `puerta` | obligatorio | La señal |
| `libre` | con `diodo="si"` | El cátodo del diodo: el + de la carga |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `modelo` | `IRLZ44N` | `IRLZ44N` (25 mΩ a 5 V, de nivel lógico) o `IRF520` (0,27 Ω a 10 V: el de los módulos azules) |
| `vth`, `rds_on`, `vgs_ref`, `vgs_plena`, `i_max` | según el modelo | Las cifras del modelo, cambiadas |
| `diodo` | `si` | El diodo de libre circulación |

Deja ver `conduce`, `corriente` (mA, de media), `disipacion` (W) y `vgs`. Al
terminar: `MOSFET Q/Q1 (IRLZ44N): conduce, Vgs 5.00 V, 500 mA de media,
25 mohm; disipa 0.01 W`.

#### `MedioPuente`

Un **medio puente integrado**, el BTS7960 de Infineon. Con `inh` alto, `in`
alto pone la salida en VS por el transistor de arriba (7 mΩ) e `in` bajo en
masa por el de abajo (9 mΩ); con `inh` bajo duerme, y la salida se queda al
aire: la corriente de una bobina vuelve por los diodos de sus transistores
hasta acabarse. **`is` es una fuente de corriente**, la que sale por arriba
entre 8500, que una resistencia de fuera convierte en tensión. Con `vdd`, las
entradas pasan por el 74HC244 del módulo IBT-2: cortan en VCC/2 y piden
0,7·VCC para un alto, así que los 3,3 V de un STM32 con el VCC a 5 V quedan
fuera de especificación —funciona, y se avisa—. Dos son un puente en H: el
IBT-2, de 43 A.

| Terminal | | |
| :--- | :--- | :--- |
| `vs`, `gnd` | obligatorios | La alimentación, de 5,5 a 27,5 V |
| `in`, `inh` | obligatorios | La entrada y el despertar |
| `out` | obligatorio | La salida |
| `is` | opcional | La medida de la corriente |
| `vdd` | opcional | El VCC del 74HC244 del módulo |

Deja ver `corriente` (mA, de media), `disipacion` y `despierto`. Al terminar:
`BTS7960 H/U1: alto, +1021 mA de media, IS 120.1 uA; disipa 0.01 W`.

#### Lo que sale

* **El cabezal**: el 775 a 24 V tras el módulo con el IRLZ44N, a 9958 rpm con
  5 V en la puerta y casi igual con 3,3. Con el IRF520 y 5 V, se avisa de que
  no se abre del todo, y va más despacio.
* **El PWM regula el par, no la velocidad.** Con 1 kHz al 50 % y en vacío, el
  775 va casi a tope, a 9612 rpm: con `L/R` = 0,25 ms la corriente se acaba en
  cada hueco —la conducción discontinua—, y sin corriente el motor no frena.
  Con el 10 % del par de bloqueo y 20 kHz, la corriente ya no se acaba, y va a
  3471 rpm de las 8608 a tope: la mitad de la tensión, menos la caída en el
  bobinado y en el diodo. Así se comporta el de verdad.
* **El eje servo**: el motorreductor de 37 mm tras el IBT-2, a 332 rpm a 12 V
  en los dos sentidos; dormido, quieto; con la mitad del par de bloqueo, 1 A,
  y su IS. Con `verif/fw/cnc_pid` en la Nucleo-F446RE, un **PID de posición**
  —el encoder en el TIM2 en modo encoder, el PWM del TIM4 a 1 kHz— lleva el
  eje de salida una vuelta, 1320 cuentas, y media atrás, sin pasarse más de un
  2 % y a 2 cuentas de la consigna en menos de 600 ms (`make gui-cnc`, C3).

---

## 5. Lo que el fichero todavía no puede hacer

Conviene decirlo, porque es el límite real de esta forma de trabajar.

**La configuración de las piezas complejas no está en el XML.** El sensor de
imagen, la SRAM, el PHY, los aparejos de USB y la tarjeta SD se montan desde el
fichero, pero su formato, su ancho de bus, su modo MII o su avería provocada se
ajustan desde C++. Una placa descrita solo en XML los tiene en su estado por
omisión. Los parámetros que faltan son fáciles de añadir —el mecanismo ya
existe—; simplemente no ha hecho falta todavía.

**Nada de lo que ocurre durante la simulación está en el XML.** Pulsar un botón,
encender un oscilador, enviar una trama CAN o inyectar una trama Ethernet son
acciones, no descripción, y viven en el programa que conduce la simulación
—o, con `--gui`, en la ventana, que las manda como órdenes a los mandos que
cada pieza declara (§4.15)—.

**El MCU no se describe.** Variante, encapsulado y rasgos de los periféricos
siguen fijados en C++. El fichero describe lo que está fuera del chip.

**Un parámetro mal escrito no se detecta** (§3.4): se guarda como parámetro,
nadie lo lee y la pieza usa su valor por omisión. Lo único que se puede hacer
hoy es comparar con la ficha: `sim --help Led` enumera los cuatro que un LED
mira, y cualquier otro que aparezca en el `<componente>` sobra.

**Un tipo mal escrito sí** (§3.1), y desde que existen las fichas el mensaje
distingue el caso frecuente: si lo único que falla son las mayúsculas, lo dice
—`tipo desconocido 'led'. Se escribe 'Led': el tipo distingue mayúsculas`— en
lugar de limitarse a enumerarlos todos.

---

## 6. Un ejemplo completo

`src/placas/discovery_min.xml` — la **STM32F4DISCOVERY (MB997)**: los dos
osciladores, los cuatro LEDs, los dos pulsadores, el arranque y los pines de
depuración. El fichero lleva en la cabecera las tres cosas que conviene saber
antes de usarla —el pulsador azul va a VDD y no a masa, el negro va a NRST y no
a PB2, y el cristal del LSE está **declarado pero desoldado**, porque en la
tarjeta real el zócalo está vacío—. Aquí, el esqueleto:

```xml
<placa nombre="discovery">
  <componente tipo="Crystal" id="X2" vdd="3.3">          <!-- HSE 8 MHz  -->
    <pin nombre="osc_in" nodo="PH0"/>
  </componente>
  <componente tipo="Crystal" id="X3" vdd="3.3" conectada="no">
    <pin nombre="osc_in" nodo="PC14"/>    <!-- LSE: zocalo vacio, como alli -->
  </componente>

  <!-- verde PD12, naranja PD13, rojo PD14, azul PD15; 680 ohmios -->
  <componente tipo="Led" id="LD4" a_vss="si" vf="2.0" r="680">
    <pin nombre="anodo" nodo="PD12"/>
  </componente>
  <!-- ... LD3, LD5 y LD6 igual ... -->

  <nodo id="PA0" bus="si"/>                              <!-- boton azul -->
  <componente tipo="Button" id="B1" v_cerrado="3.3" r_cerrado="10">
    <pin nombre="pin" nodo="PA0"/>
  </componente>
  <componente tipo="Rpull" id="R35" v="0" r="100000">
    <pin nombre="a" nodo="PA0"/>
  </componente>

  <nodo id="NRST" bus="si"/>                             <!-- boton negro -->
  <componente tipo="Button" id="B2" r_cerrado="10">
    <pin nombre="pin" nodo="NRST"/>
  </componente>
</placa>
```

Con los cuatro LEDs encendidos, el informe final enseña de un vistazo por qué
llevan Vf distintas:

```
  LED LD4 en PD12: encendido  (3.20 V, 1.77 mA)      verde,   Vf 2,0
  LED LD3 en PD13: encendido  (3.20 V, 1.77 mA)      naranja, Vf 2,0
  LED LD5 en PD14: encendido  (3.19 V, 2.04 mA)      rojo,    Vf 1,8
  LED LD6 en PD15: encendido  (3.28 V, 0.41 mA)      azul,    Vf 3,0
```

El azul da **cuatro veces menos corriente** que los otros: con 3,0 V de caída
sobre 3,3 V de alimentación no queda casi nada para la resistencia. No es una
simplificación del modelo, es lo que pasa en la tarjeta.

`src/placas/led_azul_5v.xml` — el montaje invertido a 5 V de §4.1, listo para
correr con el blinky:

```
$ ./build/mcu-sim placas/led_azul_5v.xml verif/fw/blinky/blinky.bin 200
  LED LD_AZUL en PD12: encendido  (0.40 V, 7.27 mA)
$ ./build/mcu-sim placas/led_azul_5v.xml verif/fw/blinky/blinky.bin 205
  LED LD_AZUL en PD12: apagado  (3.30 V, 0.00 mA)
```

Y la placa entera del banco de pruebas —43 componentes de 20 de los 30 tipos,
todos menos `Rpull`, `Resistencia`, `Encoder`, `Tft128x160`, `Servo`, `Fuente`, `Gnd`, `Conector`, `Jumper` y `PuenteSerie`, que tiene su propio banco (`testserie`)— se saca
del propio modelo, que es la mejor referencia de formato que hay:

```
./build/test407 --netlist > placas/banco.xml
./build/mcu-sim placas/banco.xml --mcu STM32F407VG --valida
```

El `--mcu` hace falta porque `banco.xml` no declara `<mcu>`: el chip del banco lo
construye el código C++, no el XML, y `mcu-sim` ya no supone ninguno.
