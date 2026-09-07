# Paso 2 — El netlist en memoria

*Segundo paso de la ruta de adopción del esquema XML+SVG de QtSysC. El análisis
completo está en `doc/stm32f407vg_chat.md`; el paso anterior, en
`doc/stm32f407vg_parts_paso1.md`.*

---

## 1. Qué cambia respecto al paso 1

El paso 1 hizo que cada pieza supiera decir por dónde está soldada: terminales
con nombre y un inventario volcable. Eso permite **mirar el modelo ya construido
y describirlo** — es lo que hace `--netlist`.

Este paso hace lo contrario, que es lo que de verdad hacía falta: **describir
primero y construir después**.

| | Paso 1 | Paso 2 |
| :--- | :--- | :--- |
| Dirección | modelo → descripción | descripción → modelo |
| Quién crea las piezas | `sc_main.cpp`, a mano | el `Netlist` |
| Quién posee los nodos | el componente | el netlist |
| Qué sabe el volcado | terminales y nodos | + parámetros, referencias y qué nodos hay que crear |
| Validación | ninguna | la declaración, sin simular |

Deliberadamente **no** hay factoría por cadena. El XML dirá `tipo="Led"` y
alguien tendrá que convertir esa cadena en un `new Led(...)`; eso exige un
registro estático con auto-registro por tipo —C++ no tiene reflexión— y es el
paso 3. Aquí cada instancia lleva su propio creador **tipado**, puesto por quien
la declara. El resultado es que la declaración ya es datos, que es lo que hacía
falta para que **el formato lo defina el modelo y no la especulación**.

**Suite: 1839/1839, 0 fallos** (1811 anteriores + 28 nuevas).

---

## 2. Los ficheros

**`parts/netlist.h`** — la máquina. No conoce ni una sola clase de pieza.

* `NodeMap` — los nodos. `registra_mcu()` da de alta los 144 pads con su nombre
  de esquemático (`PA0`…`PI15`) y los diez de alimentación y arranque, cada uno
  marcado con si el LQFP100 lo saca. `externo()` crea los que no son pines.
* `Instancia` — tipo, identificador, `pines` (terminal→nodo), `params`, `refs`
  (a otras instancias) y un `crea`.
* `Netlist` — declara, valida, construye, consulta y vuelca.

**`parts/netlist_parts.h`** — los constructores tipados: `led`, `pulsador`,
`cristal`, `resistencia`, `driver`, `pista`, `hilo_can`, `transceptor_can`,
`nodo_can`. Cada uno declara los terminales **y** pone el creador que los usa.
Están juntos a propósito: si un día alguien añade un terminal y se olvida de
conectarlo, la comprobación de ida y vuelta lo caza. En el paso 3 se reescribe
**este** fichero; `netlist.h` no se toca.

---

## 3. El grupo elegido, y por qué

Se reescribió **un solo** grupo, como estaba previsto: el del **bus CAN**.

La elección no fue por comodidad — el grupo de pistas de SPI/I2S son ocho
`SignalLink` idénticos y habría sido mucho más fácil. Se eligió el CAN porque es
el que **más exige del formato**, y el objetivo del paso 2 es descubrir qué
necesita el formato, no confirmar lo que ya se sabía:

| Lo que exige | Por qué ningún grupo de pistas lo exigiría |
| :--- | :--- |
| Tres tipos distintos | ocho `SignalLink` no prueban que el esquema sea general |
| Un nodo que **no es un pin** | el hilo del bus no está en el encapsulado |
| **Referencias entre instancias** | un transceptor necesita el *objeto* del hilo, no solo el punto eléctrico |
| **Orden de construcción** | el hilo tiene que existir antes que quien se cuelga de él |
| Un componente que el MCU **ni ve** | `CanNode` no toca un solo pin |

Antes:

```cpp
can_bus = new CanWire();
xcvr1 = new CanTransceiver("xcvr1", dut->pinmux.analog(3, 1),
                                    dut->pinmux.analog(3, 0), *can_bus);
xcvr2 = new CanTransceiver("xcvr2", dut->pinmux.analog(1, 13),
                                    dut->pinmux.analog(1, 12), *can_bus);
nodo_ext = new CanNode("nodo_ext", *can_bus, 500e3);
```

Después:

```cpp
nodos.registra_mcu(dut->pinmux, dut->pwr_pads);
hilo_can(placa_can, "can_bus", "n_can");
transceptor_can(placa_can, "xcvr1", "PD1",  "PD0",  "can_bus").desconectada();
transceptor_can(placa_can, "xcvr2", "PB13", "PB12", "can_bus").desconectada();
nodo_can(placa_can, "nodo_ext", "n_can", "can_bus", 500e3);
for (const std::string& e : placa_can.valida(nodos)) SC_REPORT_ERROR("netlist", e.c_str());
placa_can.construye(nodos);
can_bus = placa_can.como<CanWire>("can_bus");   // ...y los otros tres
```

Las setenta y pico líneas de prueba que usan `can_bus`, `xcvr1`, `xcvr2` y
`nodo_ext` **no se han tocado**: los punteros siguen ahí y apuntan a lo mismo.
Que las 97 comprobaciones del bxCAN sigan pasando es la red de seguridad real.

---

## 4. Tres cosas que descubrió el ejercicio

Esto es lo que se buscaba: hacerlo obliga a encontrar lo que no se ve pensando.

### 4.1 Los nodos son del circuito, no de los componentes

`CanWire` creaba su propio `AnalogNet`. Parecía razonable —un bus es un hilo—
hasta que el netlist quiso referirse a ese hilo por nombre y descubrió que había
**dos nodos distintos llamados igual**: el que creaba el `CanWire` y el que
creaba el `NodeMap`.

El arreglo es pequeño y es una mejora de diseño por sí sola: `CanWire` gana un
constructor que recibe el nodo desde fuera. El constructor histórico sigue ahí
para el uso directo. La regla que queda escrita es la que faltaba: **un nodo
pertenece al circuito, no al componente que se cuelga de él.**

### 4.2 No todo lo que une dos componentes es un nodo

Un `CanTransceiver` necesita el `CanWire` —el objeto, con su `dominant()` y su
`vdd()`—, no solo el punto eléctrico. Al principio eso viajaba como un
parámetro de texto, lo cual funcionaba y era mentira: un parámetro es un valor,
y esto es una arista del grafo.

De ahí salió `Instancia::refs`, con dos consecuencias que un parámetro no
habría tenido:

* el volcado lo dice — `<ref nombre="hilo" componente="can_bus"/>`;
* y la validación lo comprueba, incluido el **orden**: referirse a algo que se
  declara después es un error, porque `construye()` va en orden de declaración.

### 4.3 La ida y vuelta necesita las dos direcciones

La comprobación obvia es que cada conexión declarada exista en la pieza
construida. Es insuficiente: **declarar de menos pasaría igual**. Y declarar de
menos era exactamente lo que estaba pasando — el terminal `bus` del transceptor
estaba en la pieza y no en el netlist.

Por eso T121 comprueba también la vuelta: ningún terminal de las piezas se queda
fuera del netlist. Con las dos direcciones, la descripción y el modelo no pueden
divergir en silencio.

---

## 5. Validación: lo que ya se detecta sin simular

`Netlist::valida()` se ejecuta **antes** de construir y no simula nada. Hoy
detecta:

| Error | Qué es en la placa |
| :--- | :--- |
| Nodo desconocido | un cable a ninguna parte |
| Pad no bonded (`PF3`) | conectar algo a un pin que este encapsulado no saca |
| Identificador repetido | dos componentes con la misma referencia |
| Terminal conectado dos veces | una patilla a dos sitios |
| Referencia a un componente inexistente | — |
| Referencia hacia delante | un orden de montaje imposible |
| Instancia sin tipo o sin creador | — |

Los ocho casos están ejercitados en T121 con netlists rotos a propósito. Falta
—y es el paso 3— la validación **eléctrica**: dos drivers de baja impedancia con
tensiones incompatibles sobre el mismo nodo, un nodo sin ningún terminal activo,
un pin con dos funciones alternativas asignadas.

---

## 6. Los dos volcados, y por qué son dos

```
./build/stm32f407vg --netlist          # inventario: lo que HAY construido
Netlist::volcar_xml(os)                # declaración: lo que se PIDIÓ construir
```

No son redundantes:

* el **inventario** recorre el modelo ya montado. Es la verdad sobre el terreno,
  cubre las 43 piezas de la placa —también las que todavía se montan a mano— y
  no puede conocer ni los parámetros ni cuáles de los nodos hubo que crear;
* la **declaración** es lo que un día leerá el paso 3. Lleva parámetros,
  referencias y la marca `externo="si"` de los nodos que no son pines.

Que coincidan en lo que ambos pueden ver es precisamente lo que comprueba T121.
La declaración del grupo del CAN, tal como sale hoy:

```xml
<placa nombre="can-de-pruebas">
  <nodo id="PB12"/>
  <nodo id="PB13"/>
  <nodo id="PD0"/>
  <nodo id="PD1"/>
  <nodo id="n_can" externo="si"/>
  <componente tipo="CanWire" id="can_bus" r_term="1000" vdd="3.3">
    <pin nombre="bus" nodo="n_can"/>
  </componente>
  <componente tipo="CanTransceiver" id="xcvr1" vdd="3.3" conectada="no">
    <pin nombre="txd" nodo="PD1"/>
    <pin nombre="rxd" nodo="PD0"/>
    <pin nombre="bus" nodo="n_can"/>
    <ref nombre="hilo" componente="can_bus"/>
  </componente>
  ...
</placa>
```

El `conectada="no"` de los transceptores no es un detalle: es el caso «está en
el XML pero no en el SVG». PD0/PD1 y PB12/PB13 los usan otros grupos de prueba,
y `can_links(true)` es la decisión de placa que los suelda cuando toca. La pieza
se construye —la elaboración de SystemC es estática y no hay otra— y nace
desoldada.

---

## 7. Lo que NO se ha hecho

**No hay lector de XML.** Escribir el formato es fácil; leerlo exige la factoría,
que es el paso 3. Escribirlo primero ha servido para lo que tenía que servir:
descubrir que hacían falta referencias entre instancias, que los nodos externos
hay que declararlos y que el orden de declaración es semántico. Ninguna de las
tres se habría visto especulando.

**Solo un grupo está migrado.** Los otros 39 componentes se siguen montando a
mano y aparecen en el inventario pero no en ninguna declaración. Es lo previsto:
migrarlos todos antes de tener lector no aporta nada y arriesga mucho.

**La validación es de la declaración, no eléctrica.** Ver §5.

**El `NodeMap` conoce un solo encapsulado.** `registra_mcu` marca lo que sale en
LQFP100 porque es el chip del proyecto. Otro encapsulado sería otro predicado, y
la estructura ya lo admite.

---

## 8. Siguiente

El paso 3: factoría con auto-registro por tipo, lector de XML (TinyXML-2 o
expat, sin dependencias pesadas), validación eléctrica y un ejecutable que
acepte `./sim placa.xml firmware.bin`. Es donde está el retorno grande: convertir
en un mensaje de arranque la clase de fallo que hoy aparece como un aviso de
sobrecorriente en mitad de una simulación.
