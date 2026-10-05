# Catálogo de componentes externos

Referencia de las **25 piezas** que la factoría de `src/parts/` sabe construir:
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
| `depuracion` | `pines` | `pines`: expone SWCLK/SWDIO y el stub se cuelga por fuera, como un ST-LINK. `dap`: reserva los cinco pines de depuración y el stub habla con el núcleo por llamada de función |
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

**Todo lo de la placa `A` se llama `A/...`**: `A/LD2`, `A/u0`, `A/vcc`, el
pin de conector `A/CN9.2`. Dentro del fichero de la placa se escribe como
siempre, y **`PA5` es el pin del chip de ESA placa** (`A/u0.PA5`) aunque el
sistema lleve varios. La barra no puede aparecer en un nombre de la placa.

Lo demás funciona igual: con un solo MCU en todo el sistema, el firmware de la
línea de órdenes, `--gdb` y `--mcu` valen y mandan; con varios, cada chip lleva
lo suyo, aquí en `<mcu ref>`. `placas/nucleo_y_shield.xml` y
`placas/pila_pc104.xml` son los ejemplos, y el
porqué de cada decisión está en `doc/analisis_placas_conectadas.md`.

---

## 3. Parámetros comunes

### 3.1 Los tres de todos los componentes

| Parámetro | Obligatorio | Qué hace |
| :--- | :--- | :--- |
| `tipo` | sí | El nombre de clase que la factoría busca. Distingue mayúsculas: `Led`, no `LED`. Un tipo que no conoce se rechaza con la lista de los que sí |
| `id` | sí | Identificador único de instancia. Es el nombre por el que otros componentes lo referencian, y el que sale en los avisos de validación |
| `conectada` | no (`si`) | `no` = se construye desoldada (§2.4) |

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
`externo` y `bus` el lector es estricto: solo `si` o `no`, y otra cosa es un
error con su línea.

**Un parámetro que la pieza no mira se ignora en silencio.** Todo atributo de
`<componente>` que no sea `tipo`, `id` o `conectada` se guarda como parámetro,
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
| `anodo` *o* `catodo` | uno de los dos | La patilla que va soldada al pin. Son **el mismo terminal con dos nombres**: el que se escriba no cambia la física —eso lo decide `a_vss`—, pero permite que el fichero diga la verdad. Con `a_vss="si"` lo que toca el pin es el ánodo; con `a_vss="no"`, el cátodo. Declarar los dos es un error |

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
antes de que hubiera rebotes. Y el botón de RESET de la Discovery tampoco: en la
tarjeta, el condensador de NRST se come los rebotes, y como ese condensador no
se modela, B2 lleva `rebote="no"`.

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

**El límite se comporta como el de una fuente de laboratorio.** Por debajo,
tensión constante. Si la carga pide más, la pieza pasa a **corriente
constante**: entrega exactamente el límite y deja caer la tensión —o, en una
`Gnd`, deja subir el nodo—. Al entrar en limitación pone su observable
`sobrecorriente` a 1 y lo avisa una vez por el informe de SystemC (un `T_AVISO`
en la ventana); al salir, vuelve sola a tensión constante. El cálculo no itera a
ciegas: lee el resto del nodo como otro Thevenin `{Vx, Rx}` y se pone la
resistencia que deja la corriente en el límite, `|v − Vx| / I_lim − Rx`.

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

#### `Conector`

Una tira de **filas × columnas** pines, numerados del 1 al N. **No conduce ni
escucha**: dice que esos nodos salen de la placa. Su pin *k* se llama `ID.k`
(`CN9.2`), se suelda a un nodo de la placa con `<pin nombre="k">`, y uno sin
soldar es un nodo propio, al aire, del que se puede colgar otra pieza. Dos
conectores se enchufan en un `<sistema>` (§2.5).

| Terminal | | |
| :--- | :--- | :--- |
| `1` … `N` | los que se usen | Cada pin, por su número, al nodo al que va soldado |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `columnas` | *(obligatorio)* | Cuántos pines tiene cada fila |
| `filas` | `1` | Cuántas filas: 2 en un IDC, en un morpho o en la cabecera de una Raspberry Pi |
| `numeracion` | `zigzag` | `zigzag`: el 1 y el 2 enfrentados, impares en una fila y pares en la otra. `filas`: la primera fila entera, del 1 a `columnas`, y luego la siguiente |

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

**Un límite.** Un id de conector no puede ser algo como `P1`, porque `P1.1`
sería el pad `PB1`.

**Alimentación, masa, reset y arranque también pasan.** Un pin se puede
soldar a `VDD`, `VSS` (la masa del chip), `NRST`, `BOOT0` o cualquiera de los
diez pads de alimentación y arranque, y llevarlos así a otra placa o unirlos
con los de otro chip: con el NRST compartido, un reset de una placa resetea a
las dos.

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

Hace falta al menos `rx` o `tx`. Los nombres son los del **adaptador**, como
vienen serigrafiados en uno de verdad: su `rx` va al TX del MCU.

| Atributo | Por omisión | |
| :--- | :--- | :--- |
| `host` | `rfc2217:3355` | `memoria`, `tcp:PUERTO` (en crudo) o `rfc2217:PUERTO` (Telnet con la opción 44: el terminal puede fijar la línea, mover DTR y RTS y mandar breaks), escuchando siempre en `localhost`. O **conectándose** a un servidor que ya escucha: `tcp-cliente:HOST:PUERTO` o `rfc2217-cliente:HOST:PUERTO` (este configura el puerto remoto con la línea del puente), reintentando cada segundo. `--serie ID=DESTINO` lo cambia sin tocar el XML. Con cualquier destino de red, la simulación no termina sola (como con `--gdb`) y conviene `--tiempo-real` |
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
`placas/vcp_rfc2217.xml` y `placas/nucleo_f446re_vcp.xml` (la Nucleo-F446RE con
el VCP del ST-LINK). **La receta para ver el `printf` en tu ordenador, por
sistema, está en `doc/puente_serie.md`**; con `--espera-terminal`, `mcu-sim` no
arranca el MCU hasta que el terminal está conectado, y así no se pierde lo que
el firmware imprime al arrancar.

### 4.10 Lo que `mcu-sim-gui` puede ver y tocar

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
| `Button` | `pulsado` (0/1): el **dedo**, no el contacto, que en un NC es lo contrario | `pulsar` (botón, 0 suelta, 1 pulsa) |
| `Crystal` | `presente` (0/1): si está soldado | — |
| `Fuente`, `Gnd` | `corriente` (mA, la que entrega o recibe; la escala es ± el límite, o ±100 sin él) y `sobrecorriente` (0/1, **una alarma**: el catálogo la marca con `alarma="si"` y la ventana la pinta en rojo) | — |

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
piezas que el enunciado de la GUI necesita y no existen —`PwmMeter`, `Servo`,
`Encoder`, `StepperDriver`, `DcMotor`— están en `doc/analisis_gui.md` §8.

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
cada pieza declara (§4.10)—.

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

Y la placa entera del banco de pruebas —43 componentes de 20 de los 25 tipos,
todos menos `Rpull`, `Fuente`, `Gnd`, `Conector` y `PuenteSerie`, que tiene su propio banco (`testserie`)— se saca
del propio modelo, que es la mejor referencia de formato que hay:

```
./build/test407 --netlist > placas/banco.xml
./build/mcu-sim placas/banco.xml --mcu STM32F407VG --valida
```

El `--mcu` hace falta porque `banco.xml` no declara `<mcu>`: el chip del banco lo
construye el código C++, no el XML, y `mcu-sim` ya no supone ninguno.
