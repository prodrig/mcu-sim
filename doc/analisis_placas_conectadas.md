# Varias placas enchufadas entre sí: conectores y `<sistema>`

*Análisis del 2026-10-05, y lo que se decidió e hizo a partir de él (P-15 en
`doc/todo.md`). Ampliado el mismo día con las pilas —un conector que une
varias placas, como en PC/104— y con lo que `mcu-sim` cuenta a la ventana de
cada placa (§13 y §14).*

## 1. La pregunta

Se plantearon dos cosas, pensando en montajes como una **NUCLEO-F446RE con una
X-NUCLEO-67W61M1** enchufada encima:

1. **Una pieza nueva, el conector**, puramente pasiva, de `filas` × `columnas`
   pines numerados del 1 al N —un IDC40 como el Samtec TST-120-03-S-D son 2
   filas y 20 columnas, pines `J1.1` a `J1.40`, impares en una fila y pares en
   la otra—, que pueda decir a qué otro conector idéntico está enchufado.
2. **Un diseño jerárquico**: un XML con varias `<placa>`, cada una con su
   nombre e identificador, unidas por esos conectores:

   ```xml
   <placa id="MCU" nombre="nucleo-f446re">
     <componente tipo="conector" id="CN7" filas="2" columnas="20"/>
   </placa>
   <placa id="WiFi" nombre="X-NUCLEO-67W61M1">
     <componente tipo="conector" id="J1" filas="2" columnas="20" conecta_con="MCU-CN7"/>
   </placa>
   ```

Y una alternativa a estudiar: **un fichero base que cite otros XML**, cada uno
con una sola placa, y que diga él cómo se conectan.

## 2. Lo que ya había

Eléctricamente no hacía falta nada nuevo. El modelo ya sabía que varios nombres
pueden ser el mismo punto:

* **`<nodo une="PB9 PD3">`** junta pads en un solo `AnalogNet`. Se resuelve en
  `cableado_desde_netlist()` *antes* de construir el MCU, porque `Pad::net` es
  un `sc_port` que se ata al construir. Un acople de conectores es lo mismo
  entre nombres cualesquiera.
* **Los prefijos de MCU** (`u0.PD12`) y `placas/dos_mcu.xml`: dos chips en un
  mismo nodo ya funcionaban.
* **La validación eléctrica agrupa por nodo eléctrico**, no por cómo se
  escribió: el nombre de cada terminal sale del `AnalogNet` al que está atado
  (`nombre_nodo()`), así que dos alias de un mismo nodo cuentan como uno.

Lo que faltaba era **componer la declaración**: una pasada que junte en clases
los nombres que son el mismo punto y deje la placa escrita con un nombre por
clase, antes de construir nada. El modelo, las piezas y los invariantes no se
tocan.

## 3. Una corrección al ejemplo

* **La X-NUCLEO-67W61M1 no va por el CN7.** Va por los conectores **Arduino
  Uno V3** y lleva además una cabecera de 40 pines tipo Raspberry Pi
  ([data brief de ST](https://www.st.com/resource/en/data_brief/x-nucleo-67w61m1.pdf)).
* **En una Nucleo-64 los Arduino son cuatro conectores de una fila**: CN5 (10
  pines), CN6 (8), CN8 (6) y CN9 (8). Los morpho CN7 y CN10 son de **2×19**, no
  de 2×20 ([UM1724](https://www.farnell.com/datasheets/1948962.pdf)).
* Consecuencias para el diseño: hace falta `filas="1"`, y **dos placas se unen
  por varios pares de conectores**, no por uno.
* **El módulo Wi‑Fi (ST67W611M1) no está modelado.** Sin un modelo de su
  esclavo SPI, esa placa concreta sería solo pistas. Los conectores son la
  fontanería; el módulo es otro trabajo, bastante mayor.

## 4. Estrategia 1: el conector

Encaja bien como pieza, con matices:

* **Sus pines se sueldan como los de cualquier pieza**:
  `<pin nombre="17" nodo="PA13"/>`. Un pin sin soldar es un nodo propio,
  `J1.17`, del que se puede colgar otra pieza directamente.
* **No conduce ni crea procesos**: no mueve ningún invariante.
* **La numeración necesita un parámetro.** Un IDC, una cabecera de Raspberry
  Pi o un morpho numeran en *zigzag* (el 1 y el 2 enfrentados); otros, fila a
  fila. `numeracion="zigzag|filas"`, zigzag por omisión. Solo importa al
  acoplar en espejo.
* **No todo acople es 1↔1.** Dos placas cara a cara quedan en espejo (en un
  2×N, el 1 cae sobre el 2), y un cable puede cruzar líneas (TX↔RX). Hace falta
  `espejo` y unir pin a pin.
* **Validación**: mismo número de pines, cada conector acoplado una sola vez,
  no consigo mismo, que el otro extremo sea un conector.
* **Un límite real**: `une` solo sabe unir **pads de puerto**. `Cableado::une`
  no cubre VDD, VSS ni NRST, y el conector Arduino lleva NRST. Unir el NRST de
  una placa con un nodo de otra que no tiene chip funciona (es un alias); unir
  los NRST de dos chips, todavía no.
* **Las masas son en gran parte simbólicas**: cada pieza resuelve su masa por
  dentro (`a_vss`, `vdd`), así que unir GND entre placas es casi siempre
  documental. Solo pesa con `Fuente`/`Gnd` en los dos lados, y entonces salta
  «dos fuentes en un nodo», que es lo correcto.

## 5. Estrategia 2: varias `<placa>` en un fichero

Aquí está el grueso del impacto: todo lo que tiene que ser único en una placa
pasa a serlo en el sistema.

| Área | Qué cambia |
| :--- | :--- |
| **Identificadores** | `R1`, `LD1` o `CN7` se repiten entre placas: hacen falta nombres cualificados. Afecta a la fábrica, a SystemC, al catálogo de la ventana y a los mensajes |
| **Separador** | El `.` ya lo usan `u0.PD12` y `J1.40`. El `-` de `conecta_con="MCU-CN7"` es ambiguo, porque los nombres llevan guiones (`nucleo-f446re`) |
| **Nombres en SystemC** | El `.` es el separador de jerarquía (SystemC lo cambia por `_` con un aviso); la `/` es legal |
| **MCUs** | Ids únicos en el sistema, puertos de GDB, el `mcu=` de `T_HOLA`, el firmware de cada chip. `--mcu` vale solo con un MCU en todo el sistema |
| **Nodos** | Cada placa tiene su espacio de nombres: `PA5` en una no es el `PA5` de la otra |
| **Volcado** | El de la placa y su comprobación de ida y vuelta pasan a ser jerárquicos |
| **Ventana** | `T_PLACA` deja de tener `<placa>` como raíz: una ventana actual lo rechaza. Hay que subir la versión del protocolo |

## 6. ¿Mejor un fichero base que cite placas?

Sí, y la razón principal es **dónde vive el acoplamiento**:

| | Varias `<placa>` con `conecta_con` | Fichero base que cita placas |
| :--- | :--- | :--- |
| Dónde se dice qué se enchufa | Dentro de una de las placas | En el montaje |
| La placa sola | Lleva dentro un enchufe a otra concreta | **Se simula sola**, igual que siempre |
| La misma placa en otro montaje | Hay que editarla, o copiarla | Se cita tal cual |
| La misma placa dos veces | Copiar y renombrar | Dos `<placa>` con ids distintos y el mismo fichero |
| Compatibilidad | — | Un fichero con raíz `<placa>` es exactamente lo de antes |

**Una placa no sabe dónde la van a enchufar.** Un `conecta_con="MCU/CN7"` dentro
de la X-NUCLEO la ata a un montaje: con otra Nucleo o con una Discovery habría
que editarla. Por eso el fichero base, con los acoples en él.

## 7. Lo que se decidió

* **Un fichero `<sistema>`** que nombra placas —de su fichero o escritas
  dentro— y dice cómo se enchufan.
* **El separador es `/`**: todo lo de la placa `A` se llama `A/...`.
* **`espejo`** en los acoples, y **`<hilo>`** para unir pin a pin.
* **`T_PLACA` sube de versión**: el protocolo pasa a la 2.

## 8. El formato

```xml
<sistema nombre="nucleo-y-shield">
  <placa id="N" fichero="nucleo_f446re.xml"/>     <!-- de su fichero -->
  <placa id="S" nombre="shield"> ... </placa>     <!-- o escrita aquí -->
  <acopla a="N/CN5" b="S/J5"/>                     <!-- pin a pin -->
  <acopla a="N/CN6" b="S/J6" espejo="si"/>         <!-- cara a cara -->
  <hilo a="N/CN9.2" b="S/J9.1"/>                   <!-- un cable -->
  <mcu ref="N/u0" firmware="demo.bin"/>            <!-- lo que decide el montaje -->
</sistema>
```

**`<placa id>`**: letras, cifras, `_` o `-` (ni `/` ni `.`). Con `fichero=`, la
ruta es **relativa a la carpeta del sistema**; sin él, la placa va escrita
dentro, con su `nombre`. Un sistema dentro de otro, todavía no.

**Los nombres.** Dentro de una placa se escribe como siempre, y al leerla se
cualifica:

| Dentro de la placa `A` | Pasa a ser |
| :--- | :--- |
| `LD2`, `vcc`, `CN7.17` | `A/LD2`, `A/vcc`, `A/CN7.17` |
| `u0` (su chip) | `A/u0` |
| `PA5`, `NRST` con un solo MCU en *esa* placa | `A/u0.PA5`, `A/u0.NRST` |
| `PA5` con dos MCUs en la placa | error: «es ambiguo… escribe u0.PA5 o u1.PA5» |
| `PA5` en una placa sin MCU | error: «es una patilla de MCU, y la placa A no lleva ninguno» |
| cualquier cosa con `/` | error: los nombres de otra placa solo se usan en el sistema |

Así `PA5` en la placa `B` es el de **su** chip aunque el sistema lleve varios.
En una placa sin MCU, cada `<nodo>` es externo sin decirlo, igual que en una
placa suelta sin MCU.

**`<acopla a b [espejo]>`**: dos `Conector` del mismo número de pines; el pin
*k* de uno queda unido al *k* del otro. En espejo, la misma forma en los dos
(filas, columnas y numeración) y se da la vuelta a las filas —o, si solo hay
una, a las columnas: el 1 cae sobre el último—.

**`<acopla conectores="A/J1 B/J1 C/J1"/>`**: una **pila**, dos o más
conectores con el pin *k* de todos unido (§13). En espejo no: tres placas no
pueden estar cara a cara. Un conector va en un acople solo, de dos o de
muchos.

**`<hilo a b>`**: dos nodos cualesquiera, cada uno `placa/nombre` con las
reglas de la tabla: `A/CN9.2`, `B/PA2`, `A/vcc`. Tienen que existir.

**`<mcu ref="A/u0">`**: firmware, depuración y puerto de GDB de un chip de una
placa, sin tocar el fichero de la placa. El tipo lo dice la placa (o `--mcu`).
Las rutas de firmware, como siempre, respecto a la carpeta desde la que se
lanza `mcu-sim`.

## 9. Cómo se resuelve

1. **Leer aplana.** El lector del sistema lee cada placa con su contexto y
   deja un único `Netlist` con todo cualificado, más los acoples y los hilos
   apuntados. Del lector en adelante **nadie sabe que hay placas**: validación,
   construcción, catálogo y ventana ven un netlist plano con nombres únicos.
2. **`Netlist::resuelve_alias()`**, antes de construir los MCUs (lo mismo que
   `une`). Junta en clases, con unión-búsqueda, cada pin de conector con su
   nodo, cada pin con el de enfrente, cada hilo y los `une` de siempre. Por
   clase deja **un nombre**:

   | Pads distintos en la clase | El nombre que queda |
   | :--- | :--- |
   | ninguno | el `<nodo>` declarado, o el primer pin de conector; nodo externo |
   | uno | ese pad (`A/u0.PA5`): todo lo demás pasa a llamarse así |
   | dos o más | un nodo compartido, como un `<nodo une>`; solo de pads de puerto |

   Reescribe las patillas de todas las piezas, los externos, los de bus y los
   compartidos. **Sin conectores, hilos ni acoples no hace nada**: las placas
   de siempre salen idénticas.
3. **Construir y validar** como siempre. Un pin de conector se marca `paso`
   en la pieza, y la validación eléctrica no lo cuenta como nodo flotante: un
   pin al aire es lo normal.

## 10. La ventana: `T_PLACA` en la versión 2

Desde la versión 2 del protocolo, `T_PLACA` puede traer un `<sistema>`:

```xml
<sistema nombre="nucleo-y-shield">
  <placa id="N" nombre="nucleo-f446re" fichero="nucleo_f446re.xml"/>
  <placa id="S" nombre="shield-leds" fichero="shield_leds.xml"/>
  <mcu tipo="STM32F446RE" id="N/u0" firmware="..."/>
  <componente tipo="Led" id="S/LD_D13" ...><pin nombre="anodo" nodo="N/u0.PA5"/></componente>
  ...
  <acopla a="N/CN5" b="S/J5"/>
</sistema>
```

Es el sistema **aplanado y resuelto**: las placas delante, luego todo con su
nombre cualificado y los conectores ya convertidos en nodos, y los acoples e
hilos al final, para que la ventana sepa qué se enchufó con qué. No es un
fichero para releer.

**Con una ventana de la versión 1** —que solo sabe leer `<placa>`—, `mcu-sim`
manda el mismo contenido con la raíz `<placa>` y sin lo que solo tiene un
sistema. Una ventana vieja lo pinta, sin agrupar. Una placa suelta se manda
igual en las dos versiones.

En `mcu-sim-gui` (plan §19): las piezas se agrupan en un recuadro por placa,
con su id local (`LD2`, no `N/LD2`), y el resumen dice las placas y los
acoples. Una pieza con muchas patillas —un conector— las pliega.

## 11. Cómo se comprueba

* **`make gui-sistema`** (`verif/gui/sistema.py`, 56 comprobaciones) contra el
  `mcu-sim` de verdad: un conector en una placa; `placas/nucleo_y_shield.xml`
  con el blinky de la Nucleo encendiendo **a la vez** su LD2 y el LED del
  shield, que está en otra placa; el espejo en 2×3 y en 1×4, y un hilo
  cruzado; dos Nucleo con un MCU cada una y la UART cruzada; `T_PLACA` en las
  versiones 1 y 2; doce errores de sistema, cada uno con lo que hay que
  hacer; la pila `placas/pila_pc104.xml` y sus seis errores; y lo que
  `T_PLACA` cuenta de cada placa (§14).
* **`make gui-proto`**: la versión que no se ofreció es ahora la 3.
* **T-suites**: la comprobación de la versión del protocolo dice 2; ningún
  invariante se mueve.
* En `mcu-sim-gui`, `prueba_sesion`, `prueba_ventana` y `prueba_cruzada`.

## 12. Lo que queda

* **Unir VDD, VSS, NRST o BOOT0 de dos chips**: necesita que `Cableado` sepa
  atar los pads de alimentación, no solo los de puerto.
* **Alimentación real entre placas**: hoy un rail que cruza un conector es un
  alias; una placa que alimente a otra necesita `Fuente` en una y cargas en la
  otra, y las piezas siguen resolviendo su `vdd` por dentro.
* **Un sistema dentro de otro**, y **cambiar parámetros de las piezas desde
  el montaje** (puentes de soldadura, jumpers): el formato lo admite sin
  romper nada; no ha hecho falta todavía.
* **El módulo ST67W611M1**, para que la X-NUCLEO-67W61M1 haga algo más que
  pasar pines.
* **Dibujar el sistema** en la ventana. La información ya llega (§14) y la
  ventana ya la tiene en su modelo; falta el dibujo. Y para un dibujo fiel
  harán falta datos que hoy no hay: el tamaño de cada placa y dónde está cada
  conector en ella.

## 13. Pilas: un conector que une varias placas

En una pila **PC/104** cada placa lleva el conector de bus **pasante**: los
pines atraviesan la placa y la siguiente se enchufa encima con el mismo
conector, así que el pin *k* es el mismo hilo en todas. Lo mismo hacen las
cabeceras apilables de Arduino.

**Cómo se dice.** Un solo `<acopla>` con todos los conectores de la pila:

```xml
<sistema nombre="pila-pc104">
  <placa id="CPU" fichero="pc104_cpu.xml"/>
  <placa id="L1"  fichero="pc104_leds.xml"/>
  <placa id="L2"  fichero="pc104_leds.xml"/>
  <acopla conectores="CPU/J1 L1/J1 L2/J1"/>
</sistema>
```

**Por qué no una cadena de acoples de dos** (`CPU/J1-L1/J1` y `L1/J1-L2/J1`).
Eléctricamente sería lo mismo, pero obliga a dejar que un conector esté en
dos acoples, y entonces el error más frecuente —un conector enchufado dos
veces por una errata— deja de poder detectarse. Con la pila en un acople, la
regla «un conector, un acople» se mantiene y el error dice cómo escribir la
pila. Además, el acople de la pila conserva el **orden**, que es el de abajo
arriba y el que hace falta para dibujarla.

**Qué se comprueba**: al menos dos conectores, ninguno repetido, todos del
mismo número de pines, ninguno en otro acople, y nada de espejo con más de
dos. `placas/pila_pc104.xml` es el ejemplo: un módulo CPU con un F407 y dos
módulos de LEDs —el mismo fichero dos veces—; el blinky enciende a la vez el
LD1 de los dos módulos. Los módulos declaran `bus="si"` en los pines con LED:
con dos iguales en la pila, dos LEDs conducen el mismo pin a propósito.

**Lo que no es PC/104 de verdad**: la forma del J1 (2×32, en zigzag) sí; la
asignación de señales del bus ISA, no. En el ejemplo cada pin es lo que dice
su placa.

## 14. Lo que la ventana sabe de cada placa

Para que algún día `mcu-sim-gui` pueda **dibujar** el sistema —un rectángulo
por placa, sus conectores, líneas entre ellos—, `T_PLACA` describe cada placa
entera, sin que la ventana tenga que deducir nada de los prefijos:

```xml
<placa id="CPU" nombre="pc104-cpu" fichero="pc104_cpu.xml" piezas="2">
  <mcu ref="CPU/u0" tipo="STM32F407VG"/>
  <conector ref="CPU/J1" filas="2" columnas="32" numeracion="zigzag" acople="0"/>
</placa>
...
<acopla n="0" conectores="CPU/J1 L1/J1 L2/J1" placas="CPU L1 L2"/>
<hilo a="CPU/u0.PA2" b="L2/J9.1" placas="CPU L2"/>
```

* **cada placa**: cuántas piezas lleva, sus chips con su tipo, y sus
  conectores con su forma y el acople en que están (sin `acople`, al aire);
* **cada acople**: su número, sus conectores **en orden** y la placa de cada
  uno. Con dos lleva además `a=` y `b=`, como en la primera versión de los
  sistemas, para que una ventana de entonces lo siga leyendo;
* **cada hilo**: las dos placas que une.

Todo es **añadido** a la versión 2 del protocolo, que ya llevaba el
`<sistema>`: una ventana que no lo conoce lo ignora, y por eso no sube de
versión. En `mcu-sim-gui` (plan §20) esto llena su modelo —cada placa con lo
suyo, y un **grafo de placas**: una arista por cada par de placas vecinas en
un acople, en el orden de la pila, y una por cada hilo entre placas—; hoy lo
usa para que el recuadro de cada placa diga a cuáles está unida y por dónde,
y cada conector de una pila nombre a los demás.
