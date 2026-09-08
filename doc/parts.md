# Catálogo de componentes externos

Referencia de las **21 piezas** que la factoría de `src/parts/` sabe construir:
qué terminales tiene cada una, qué parámetros admite, qué hace cada parámetro y
qué queda fuera del fichero.

Una placa se describe en XML y se monta con:

```
./build/sim placa.xml [firmware.bin] [ms]
./build/sim placa.xml --valida        # comprueba la placa sin simular
./build/sim --help                    # lista los tipos que conoce la factoría
```

El formato y el porqué están en `doc/stm32f407vg_parts_paso3.md`; esto es el
catálogo.

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

### 2.1 Los nodos

```xml
<placa nombre="mi-placa">
  <nodo id="PA5"/>                          <!-- un pin: ya existe -->
  <nodo id="n_led1" externo="si"/>           <!-- hay que crearlo -->
  <nodo id="PB6" bus="si"/>                  <!-- varios conductores, a propósito -->
  ...
</placa>
```

| Atributo | Omisión | Efecto |
| :--- | :--- | :--- |
| `id` | *(obligatorio)* | El nombre del nodo |
| `externo` | `no` | `si` = no es un pin del MCU; el netlist lo crea |
| `bus` | `no` | `si` = varias piezas pueden conducir a la vez sin que sea un error |

**Los nodos de pin no hace falta declararlos.** Los 144 pads del chip y los diez
nodos de alimentación y arranque existen desde el principio, con su nombre de
esquemático: `PA0`…`PI15`, `VDD`, `VSS`, `VDDA`, `VSSA`, `VREF+`, `VBAT`,
`VCAP1`, `VCAP2`, `NRST`, `BOOT0`. Declararlos con `<nodo id="PA5"/>` es
opcional y solo sirve para documentar. **Los externos sí**: sin
`externo="si"` el nodo no existe y la validación lo rechaza.

**`bus="si"` no cambia nada eléctrico**; solo calla el aviso de conducción
simultánea. Úsese cuando varias piezas conducen ese nodo por diseño —un bus de
colector abierto, un cable en Y— o cuando un pin se comparte a propósito entre
dos montajes. Si no se declara y hay dos conductores, `--valida` lo dice.

**Los pads que este encapsulado no saca son un error.** El LQFP100 solo tiene
los puertos A–E más `PH0` y `PH1`. Conectar algo a `PF3` se rechaza antes de
simular, aunque el nodo exista en el modelo.

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
| `r` | `Led`, `Resistor` | La resistencia en serie del LED, que fija su corriente; el valor de la resistencia |
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
que darlo alguien: el pull-up interno del MCU o un `Resistor` externo. Sin
ninguno de los dos, el pin queda indeterminado — que es lo que pasa en una placa
real y lo que el modelo reproduce.

| Terminal | | |
| :--- | :--- | :--- |
| `pin` | obligatorio | El pin del pulsador |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `r_cerrado` | `10` | Resistencia del contacto cerrado, en ohmios. Pulsado, la pieza gobierna el nodo con `{0 V, r_cerrado}` |

Se acciona desde C++ con `press()` y `release()`. **Un pulsador
`conectada="no"` no cierra aunque se le pulse.**

#### `Resistor`

Una resistencia entre el nodo y una tensión fija. Es el pull-up o el pull-down
de la placa, y también la carga con la que se mide cuánta corriente entrega un
pad.

| Terminal | | |
| :--- | :--- | :--- |
| `a` | obligatorio | El nodo al que se conecta |

| Parámetro | Omisión | Efecto |
| :--- | :--- | :--- |
| `v` | `3.3` | La tensión del otro extremo. `3.3` = pull-up; `0` = pull-down; cualquier otra cosa = un divisor contra lo que haya en el nodo |
| `r` | `10000` | El valor, en ohmios |

Conduce siempre desde que se construye. Es el único componente cuyo efecto no
depende de ningún proceso.

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
conectando los dos pines al mismo nodo.

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
acciones, no descripción, y viven en el programa que conduce la simulación.

**El MCU no se describe.** Variante, encapsulado y rasgos de los periféricos
siguen fijados en C++. El fichero describe lo que está fuera del chip.

**Un parámetro mal escrito no se detecta** (§3.4): se guarda como parámetro,
nadie lo lee y la pieza usa su valor por omisión.

---

## 6. Un ejemplo completo

`src/placas/discovery_min.xml` — una placa mínima al estilo de las Discovery de
ST:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<placa nombre="discovery-min">
  <nodo id="PH0"/>
  <nodo id="PD12"/>
  <nodo id="PA0"/>

  <componente tipo="Crystal" id="X1" vdd="3.3">
    <pin nombre="osc_in" nodo="PH0"/>
  </componente>

  <componente tipo="Led" id="LD4" a_vss="si" vf="2.0" r="330">
    <pin nombre="anodo" nodo="PD12"/>
  </componente>

  <componente tipo="Button" id="B1" r_cerrado="10">
    <pin nombre="pin" nodo="PA0"/>
  </componente>
</placa>
```

`src/placas/led_azul_5v.xml` — el montaje invertido a 5 V de §4.1, listo para
correr con el blinky:

```
$ ./build/sim placas/led_azul_5v.xml verif/fw/blinky/blinky.bin 200
  LED LD_AZUL en PD12: encendido  (0.40 V, 7.27 mA)
$ ./build/sim placas/led_azul_5v.xml verif/fw/blinky/blinky.bin 205
  LED LD_AZUL en PD12: apagado  (3.30 V, 0.00 mA)
```

Y la placa entera del banco de pruebas —43 componentes de 20 de los 21 tipos,
todos menos `Resistor`— se saca
del propio modelo, que es la mejor referencia de formato que hay:

```
./build/stm32f407vg --netlist > placas/banco.xml
./build/sim placas/banco.xml --valida
```
