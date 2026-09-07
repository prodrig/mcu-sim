# Paso 3 — La placa en un fichero

*Tercer paso de la ruta de adopción del esquema XML+SVG de QtSysC. Los
anteriores, en `doc/stm32f407vg_parts_paso1.md` y `..._paso2.md`.*

---

## 1. Qué hay ahora

```
./build/sim placa.xml [firmware.bin] [ms]
```

Un MCU, la placa que diga el fichero y el firmware que se le pase. Para cambiar
de placa no hace falta recompilar nada.

```
$ ./build/sim placas/discovery_min.xml verif/fw/blinky/blinky.bin 205
placa 'discovery-min': 3 componentes, 154 nodos, 0 avisos
firmware: 1032 bytes de verif/fw/blinky/blinky.bin
simulados 205.000 ms en 0.008 s de anfitrion (7341 deltas)
  LED LD4 en PD12: encendido  (3.11 V, 3.38 mA)
```

Esa última línea es el arco entero de los tres pasos en un renglón: firmware de
verdad, compilado con `arm-none-eabi-gcc`, gobernando un LED descrito en un
fichero de texto, con la corriente resuelta en float por el nodo analógico. A
200 ms está apagado y a 205 encendido, porque el blinky parpadea.

**Suite: 1851/1851, 0 fallos** (1811 antes de esta línea de trabajo + 40 del
grupo T121).

---

## 2. Las cuatro piezas

| Fichero | Qué hace |
| :--- | :--- |
| `parts/part_factory.h` | El registro `cadena → creador`, con macro de auto-registro |
| `parts/xml_min.h` | Lector de XML mínimo y estricto, sin dependencias |
| `parts/netlist_xml.h` | Del árbol XML al `Netlist` |
| `top/sim_main.cpp` | El ejecutable |

Y `parts/netlist_parts.h`, que se reescribió sobre la factoría **tal y como el
paso 2 predijo**: los creadores se fueron a registros de la factoría y los
ayudantes tipados se quedaron en pura declaración. `netlist.h` no se tocó,
salvo para que `add()` consulte la factoría.

---

## 3. Por qué un lector de XML propio

Es la decisión discutible del paso, así que conviene el argumento completo.

Este proyecto tiene **una** dependencia —SystemC— y se compila con un Makefile
de veinte líneas en cualquier máquina con `g++`. Añadir TinyXML-2 o expat por un
formato de tres elementos y ocho atributos cambia eso para todo el que se baje
el repositorio.

Escribir un analizador de XML a mano es, normalmente, un error: se acaba con
algo permisivo que acepta ficheros mal formados y hace lo que le parece. Aquí se
evita por la vía de ser **estricto**: lo que el lector no entiende es un error
con línea y columna, nunca una suposición. Acepta `<?xml?>`, comentarios,
elementos con atributos entrecomillados, elementos vacíos y las cinco entidades
de la norma. Y rechaza —diciendo qué esperaba y dónde— espacios de nombres, DTD,
CDATA, atributos sin comillas, atributos repetidos, etiquetas descasadas,
entidades inventadas y texto suelto.

Lo último merece una nota: en este formato **ningún elemento tiene contenido
textual**, así que texto entre etiquetas no es contenido que ignorar, es
alguien que se ha equivocado. Ignorarlo sería la clase de permisividad que
convierte un fallo en un misterio.

Nueve XML rotos a propósito están ejercitados en T121, uno por cada motivo.

---

## 4. La factoría

`Netlist::add()` consulta la factoría por su cuenta, así que una instancia leída
del fichero sale con su creador puesto sin que nadie se lo ponga. Es el único
sitio del proyecto donde un nombre escrito por una persona se convierte en un
objeto.

Un tipo desconocido deja de ser un fallo de enlazado y pasa a ser un error de
datos con un mensaje utilizable:

```
[decl] d1: tipo desconocido 'Lde'. La fabrica conoce: Button, CameraSensor,
CanNode, CanTransceiver, CanWire, Crystal, Driver, EthPhy, ExtClock, ExtNand,
ExtSram, I2cEeprom, I2cExtMaster, I2cWire, Led, Resistor, SdCard, SignalLink,
SwoReceiver, UsbDeviceRig, UsbHostRig
```

Decir qué **sí** se conoce es la otra mitad del aviso, y es barato: la factoría
ya tiene la lista.

Los ayudantes tipados de `netlist_parts.h` no han desaparecido y no deberían:
el compilador comprueba lo que un fichero no puede —que el terminal se llame
`anodo` y no `anode`—. Quien monta una placa desde C++ sigue queriéndolos.

---

## 5. La validación eléctrica

Es el retorno que justificaba los tres pasos. Corre **después** de construir,
porque necesita saber qué terminal de cada pieza conduce y cuál solo escucha, y
eso lo sabe la pieza y no la declaración. Sigue sin simular: es un recorrido del
grafo.

**Conflicto.** Dos o más piezas conectadas y conduciendo sobre el mismo nodo sin
que el nodo esté declarado como de varios conductores. Es el cortocircuito de
placa, y es la familia de fallos que más tiempo ha costado en este proyecto.

**Flotante.** Un nodo **externo** con piezas colgadas y ninguna que conduzca. Su
tensión no está definida, y como un `AnalogNet` conserva la última resuelta, lo
que se lea de él será lo que dejó otro. Es exactamente por lo que un host de USB
llegó a «ver» un dispositivo que no estaba enchufado.

Dos decisiones del criterio, y las dos importan:

*El pad del MCU no cuenta como conductor.* Si conduce o no lo decide el firmware
en ejecución, y esto es estático. Lo que se comprueba es lo que impone la
**placa**.

*Un pin nunca se marca como flotante.* Al otro lado está el pad. Que ninguna
pieza externa lo gobierne es lo normal en una entrada. La primera versión no
hacía esta distinción y sacaba **52 avisos falsos** sobre 59 — un validador que
grita por todo no se usa, así que ese ajuste era la diferencia entre una
herramienta y un ruido.

### Lo que encontró en la placa del banco

Con el criterio afinado quedaron **siete** avisos, y los siete eran ciertos:

| Nodo | Quiénes | Qué es |
| :--- | :--- | :--- |
| `PB6`, `PB7` | EEPROM y maestro I2C externo | un bus de colector abierto de verdad |
| `n_can` | terminador y nodo CAN externo | un cable en Y |
| `PA0` | pulsador y fuente del ADC123_IN0 | pin compartido entre grupos de prueba |
| `PA1` | pista UART5→UART4 y ADC123_IN1 | ídem |
| `PD2` | UART5_RX y el CMD de la tarjeta SD | ídem |
| `PH0` | cristal del HSE y oscilador externo | ídem, y en una placa real sería un error |

Ninguno es un fallo, pero **ninguno estaba escrito en ningún sitio**: vivían
repartidos por `i2c_bus()`, `adc_links()`, `can_links()` y compañía como efectos
laterales de funciones que encienden y apagan enlaces. Declararlos con
`nodo_bus()` —siete líneas con su comentario— convirtió siete decisiones de
placa implícitas en explícitas. Eso ya vale el ejercicio.

Después de declararlos: **0 avisos sobre 43 componentes y 155 nodos**, y la
validación corre en cada arranque del banco, no solo cuando se pide.

### El caso que lo empezó todo

En T121 está reconstruido el cortocircuito de la fase F7-ETH: una pista que
gobierna PB11 y el TX_EN del PHY sobre el mismo pin.

```
nodo PB11: conducen a la vez u2_tx.pin y phy_txen.pin.
Si es un bus, declaralo con nodo_bus()
```

Entonces se manifestó como un aviso de sobrecorriente del pad en mitad de una
prueba de USART, y hubo que rastrearlo hacia atrás hasta el enlace olvidado.
Ahora sale antes de simular, con los dos culpables por su nombre.

---

## 6. La ida y vuelta

La prueba que de verdad cierra el formato: volcar la placa entera a XML,
releerla con el lector de verdad y comprobar que sale el **mismo grafo** — tipo,
identificador, terminales con su nodo, referencias, parámetros y estado de
conexión, más las marcas `externo` y `bus` de los nodos. Si el escritor y el
lector no coincidieran en algo —un atributo que uno pone y el otro ignora—, aquí
se vería.

Y el mismo fichero, leído por **otro ejecutable**:

```
$ ./build/stm32f407vg --netlist > placas/banco.xml
$ ./build/sim placas/banco.xml --valida
placa 'banco-de-pruebas': 43 componentes, 155 nodos, 0 avisos
```

Las 43 piezas de la suite, descritas por ella misma, montadas por un programa
que no sabe nada de la suite.

---

## 7. Lo que NO se ha hecho

**No hay SVG.** Es el paso 4, y ahora tiene de dónde salir: el netlist lleva
parámetros, referencias y la marca de qué nodos hay que crear.

**El `NodeMap` conoce un solo encapsulado.** `registra_mcu` marca lo que sale en
LQFP100 porque es el chip del proyecto. Otro encapsulado sería otro predicado.

**La validación no mira tensiones incompatibles, solo conducción simultánea.**
Dos piezas que conducen el mismo nivel sobre un nodo no son un cortocircuito, y
hoy se avisa igual. Distinguirlo exigiría que cada terminal declarase qué
tensión y qué impedancia presenta, que es información que hoy solo existe dentro
del constructor de cada pieza. Es trabajo acotado y no urgente: el falso
positivo se calla declarando el nodo, y declararlo documenta.

**El fichero no describe el MCU.** Variante, encapsulado y rasgos de los
periféricos siguen en C++. Un `<mcu tipo="STM32F407VG" encapsulado="LQFP100"/>`
sería el paso natural si alguna vez hace falta más de un chip.

---

## 8. Siguiente

El paso 4: generar el SVG desde el netlist, con `id` estables por componente y
por nodo, y opcionalmente colorearlo desde una traza de simulación. El valor es
documental —hoy, entender qué hay colgado de un pin exige leer el XML— y la
pieza que faltaba, el grafo con todo dentro, ya está.
