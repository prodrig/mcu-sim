# Paso 1 — Consolidación de la librería de componentes externos

*Primer paso de la ruta de adopción del esquema XML+SVG de QtSysC. El análisis
completo y los cuatro pasos están en `doc/chat.md`, turno «Estrategia
XML + SVG de QtSysC».*

---

## 1. Qué se pedía y qué se ha hecho

El paso 1 era el único de los cuatro que **no añade funcionalidad nueva**: pone
la librería en condiciones de ser descrita desde fuera. Tres cosas concretas:

| Pedido | Hecho |
| :--- | :--- |
| `ExtPart` gana `set_enabled(bool)` | `ExtPartBase::set_enabled` virtual, implementado o redefinido en las 20 clases |
| Descripción de terminales por nombre | tabla `Terminal{nombre, nodo, drivers, pasivo}` por pieza |
| Constructor canónico o adaptador | tipo e instancia explícitos; adaptador, no cambio de firma |
| Sacar `ext_parts.h` de `verif/` | ahora en `parts/`, junto a `part_base.h` |

Y un cuarto que no estaba pedido pero que sale gratis y demuestra que los tres
anteriores son de verdad: **el volcado del netlist**, `./build/mcu-sim
--netlist`.

**Suite: 1811/1811, 0 fallos.** El tiempo simulado total es idéntico al
picosegundo (2 327 837 024 213 ps), que es la prueba más dura de que no se ha
movido nada: cualquier cambio en el orden o el instante de una conducción lo
habría desplazado.

---

## 2. La base: `parts/part_base.h`

### 2.1 El terminal

```cpp
struct Terminal {
    std::string      nombre;   // "anodo", "sda", "d0"...
    analog_net_if*   net;
    std::string      nodo;     // "PA5", "vdd", "can_bus"
    std::vector<int> ids;      // drivers de ESTA pieza sobre ese nodo
    bool             pasivo;   // true: solo escucha
};
```

Tres decisiones que conviene justificar:

**El nombre es nominal, no posicional.** Es el punto entero del paso. Un XML
tiene que poder decir «el pin `sda` de `eeprom` va al nodo `PB7`» sin saber en
qué orden recibe el constructor de `I2cEeprom` sus referencias. Hoy ese orden es
un detalle de implementación, y con esto deja de filtrarse.

**Un terminal puede tener VARIOS drivers.** No es una rareza: es lo normal en
esta librería. El `cmd` de una tarjeta SD lleva el driver de la tarjeta *y* el
pull-up de 47 kΩ del zócalo; el `mdio` de un PHY lleva el driver del PHY *y* el
pull-up de 10 kΩ de la placa; cada línea de un `I2cWire` lleva el pull-up y el
hilo que propaga el cero. Modelarlos como un terminal con dos drivers y no como
dos terminales es lo correcto: eléctricamente son el mismo hilo.

**Un terminal puede no tener NINGÚN driver** (`pasivo`). El `NOE` de una SRAM,
el `MDC` de un PHY, el `TX_EN` y los `TXD` que el MAC gobierna, el origen de una
pista unidireccional: son patillas que solo escuchan. Para la física no aportan
conductancia; para el netlist son conexiones de pleno derecho, porque **hay un
hilo ahí** y un validador tiene que verlo. Distinguirlas importa: un nodo donde
todo el mundo es pasivo está flotante por construcción, y eso es un error de
placa que el paso 3 debe detectar antes de simular.

### 2.2 El interruptor

El proyecto ya tenía la idea de «desoldar» una pieza, pero repartida en **cinco
nombres distintos** que habían ido apareciendo según hacía falta:

| Nombre antiguo | Pieza | Ahora |
| :--- | :--- | :--- |
| `set_enabled` | `SignalLink`, `I2cWire`, `CanNode` | igual, ahora `override` |
| `set_attached` | `CanTransceiver` | alias de `set_enabled` |
| `set_conectada` | `ExtSram`, `ExtNand` | alias de `set_enabled` |
| `conectar` | `UsbHostRig`, `EthPhy` | alias de `set_enabled` |
| `soldar` / `soltar` | `CameraSensor` | alias de `set_enabled` |
| `enchufar` | `UsbDeviceRig` | alias de `set_enabled` |
| `attach` / `detach` | `Crystal` | alias de `set_enabled` |
| *(no existía)* | `Led`, `Button`, `Driver`, `Resistor`, `ExtClock`, `SdCard`, `I2cEeprom`, `I2cExtMaster`, `SwoReceiver`, `CanWire` | implementado |

**Los nombres antiguos se conservan todos.** Un `set_conectada(false)` sigue
compilando y haciendo exactamente lo mismo; es `set_enabled` con otro nombre.
Esto no es cortesía: es lo que permite que las 12 000 líneas de `sc_main.cpp` no
se toquen y que la suite sea una comprobación honesta del refactor.

Hay una excepción deliberada. `CanTransceiver` tenía **dos** interruptores con
significados físicos distintos: `set_attached` (soldarlo o no) y `set_enabled`
(el modo reposo STB, en el que deja de gobernar el hilo pero sigue escuchando).
El interruptor de la librería es el primero, así que el segundo pasa a llamarse
`set_standby`. Es el único renombre con cambio de significado del paso, y era
necesario: dos conceptos distintos no pueden compartir nombre en la interfaz
común.

### 2.3 Por qué desconectar, y no dejar de construir

Es la restricción que más forma le da a todo esto, y viene de dos sitios:

1. **La elaboración de SystemC es estática.** No se puede construir un
   `sc_module` una vez arrancado `sc_start()`. Cualquier lector de XML tendrá
   que crear las piezas antes de simular.
2. **`AnalogNet` no sabe desregistrar drivers**, solo ponerlos en alta
   impedancia (punto I-03 del TODO). Un driver, una vez registrado, ocupa su
   sitio en la superposición para siempre.

De ahí sale la lectura correcta del requisito de QtSysC de que *no todos los
componentes del XML tengan que estar en el SVG*: la pieza ausente **no se deja
de construir, se construye desconectada**. Con `R_HIZ = 1e12` frente a los 30 Ω
típicos de un driver, una pieza desconectada aporta una conductancia
36 000 millones de veces menor: es eléctricamente invisible. El modelo ya
funcionaba así —`usb_placa()`, `eth_placa()`, `fsmc_placa()` sueltan y vuelven a
soldar piezas entre grupos de prueba—; lo que cambia es que ahora es **una sola
operación con un solo nombre** en todas las piezas, no un idioma repetido.

### 2.4 Identidad: tipo e instancia

Al montar el volcado apareció un hueco que no se veía desde dentro del C++: las
piezas de una sola patilla **no tenían identificador de instancia**. Un
`Led`, un `Button`, un `Resistor` no son `sc_module` —no tienen procesos, y
hacerlos módulos solo añadiría objetos a la jerarquía—, así que no tenían
nombre. El volcado salía con nueve componentes llamados `extdrv`, que no es un
netlist.

Solución: la base separa **tipo** (el nombre de clase que escribirá el XML:
`Led`, `Crystal`, `Driver`) del **identificador de instancia**. Las piezas que
son módulos lo tienen gratis —su nombre de módulo—; las que no, se numeran por
tipo (`Crystal_1`, `Crystal_2`). Y como efecto lateral se separa un tercer
nombre que antes iba mezclado con los otros dos: la **etiqueta de driver**, la
que aparece en los avisos de sobrecorriente del pad (`led`, `xtal`, `extdrv`).
Los tres son cosas distintas y ahora se escriben distintas.

---

## 3. El volcado del netlist

```
./build/mcu-sim --netlist
```

Elabora el modelo, recorre el inventario y escribe el grafo. **No simula**: se
detiene justo en el punto en el que un lector de XML habría terminado de
construir las piezas, que es el único punto donde puede estar.

La placa de la suite da 43 componentes de 20 tipos:

| | | | |
| :--- | ---: | :--- | ---: |
| `SignalLink` | 13 | `ExtSram` | 1 |
| `Driver` | 9 | `ExtNand` | 1 |
| `I2cWire` | 2 | `SdCard` | 1 |
| `Crystal` | 2 | `CameraSensor` | 1 |
| `CanTransceiver` | 2 | `EthPhy` | 1 |
| `CanWire`, `CanNode` | 1+1 | `UsbHostRig`, `UsbDeviceRig` | 1+1 |
| `I2cEeprom`, `I2cExtMaster` | 1+1 | `Led`, `Button`, `ExtClock`, `SwoReceiver` | 1 c/u |

Un ejemplo real de la salida:

```xml
<componente tipo="EthPhy" id="phy" conectada="no">
  <pin nombre="mdc"    nodo="PC1" pasivo="si"/>
  <pin nombre="mdio"   nodo="PA2"/>
  <pin nombre="tx_clk" nodo="PC3"/>
  ...
  <pin nombre="tx_en"  nodo="PB11" pasivo="si"/>
  <pin nombre="txd0"   nodo="PB12" pasivo="si"/>
</componente>
```

### Y aquí está el argumento entero, en dos líneas

En el mismo volcado, unas líneas más arriba:

```xml
<componente tipo="SignalLink" id="lnk_u2_u3" conectada="si">
  <pin nombre="destino" nodo="PB11"/>
  <pin nombre="origen"  nodo="PA2" pasivo="si"/>
</componente>
```

**Ese es exactamente el cortocircuito que costó horas de depuración en la fase
F7-ETH**: la pista de USART PA2→PB11 y el PHY, que usa PA2 para MDIO y PB11 para
TX_EN. Se manifestó como un aviso de sobrecorriente en el pad, en mitad de una
simulación, y hubo que rastrearlo hacia atrás hasta el enlace olvidado.

Con el netlist delante son dos líneas de un `grep`. Y esto no requiere ni XML ni
SVG: **el volcado ya vale por sí solo**. Es la razón por la que el paso 1
justifica su coste aunque los pasos 3 y 4 no se hagan nunca.

---

## 4. Lo que NO se ha hecho, y por qué

Conviene ser explícito, porque es fácil leer el paso 1 como si fuera el sistema
entero.

**No hay factoría ni lector de XML.** C++ no tiene reflexión: convertir la
cadena `"Led"` en un `new Led(...)` exige un registro estático con auto-registro
por tipo. Es el paso 3, y hacerlo ahora habría obligado a homogeneizar los 20
constructores de golpe —o a escribir 20 adaptadores— sin tener todavía el
formato definido. El orden correcto es al revés: primero el netlist en memoria
(paso 2), que define el formato con lo que el modelo realmente tiene; el lector
después.

**No hay SVG.** El generador va desde el netlist, y el netlist en memoria es el
paso 2.

**El interruptor no es total en las piezas puramente pasivas.** `SwoReceiver`
solo escucha: desconectarlo no cambia nada eléctricamente, y su hilo sigue
decodificando la traza que ve. Es correcto —un analizador desenchufado no carga
la línea— pero conviene saberlo si algún día se le quiere apagar de verdad.

**Dos `CanWire` crearían dos nodos con el mismo nombre.** El `AnalogNet` interno
se llama `can_bus`, y ahora el constructor admite un nombre propio, pero
`sc_main.cpp` no lo usa porque solo hay un bus. En cuanto haya dos, el netlist
tendrá dos nodos homónimos. Está preparado; no está ejercitado.

**El volcado no valida nada.** Escribe el grafo, no lo comprueba. La validación
—dos drivers de baja impedancia con tensiones incompatibles sobre el mismo nodo,
un pin con dos funciones alternativas, un nodo sin camino a masa, un pad no
bonded en LQFP100 con algo colgado— es el paso 3, y es donde está el retorno
grande.

---

## 5. Coste en tiempo de simulación

Ninguno medible, y por construcción:

* El inventario son 43 `push_back` durante la elaboración.
* La comprobación de `conectada_` se ha metido en dos caminos que despiertan a
  menudo —`SdCard::edge_proc` y el hilo del `Led`— y es una prueba booleana
  sobre un miembro ya en caché.
* El hilo del `Led` espera ahora sobre un evento más
  (`net_->value_changed_event() | evento_conexion()`). Ese evento no se dispara
  jamás si nadie desconecta la pieza, así que no añade despertares.

Nada de esto toca la regla que gobierna el coste en este modelo —**un bloque
cuesta en proporción a cuántas veces despierta, no a lo complicado que sea**—,
que es lo que se atacó en el trabajo de rendimiento de la fase anterior
(`doc/coste_simulacion.md`). El banco de medida sigue dando **50
deltas** para un MCU aparcado en `wfe` durante 5 ms simulados.

---

## 6. Siguiente

El paso 2 es la estructura `Netlist` en memoria y la reescritura de **un** grupo
de prueba de `sc_main.cpp` sobre ella. El objetivo de reescribir solo uno es
deliberado: que el formato lo defina lo que el modelo realmente necesita, y no
la especulación sobre lo que un XML debería tener.
