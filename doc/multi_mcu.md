# Varios MCUs en una placa — análisis

*Qué habría que cambiar en el XML y en el C++ para que una placa pueda llevar
un MCU distinto, o más de uno. Empezó siendo el análisis previo, con las
decisiones que hay que tomar y el precio de cada una; una parte ya está hecha y
se marca como tal.*

Caso de referencia: una placa con **dos STM32F407**, `u0` y `u1`, donde los
nodos se escriben `PD12` si solo hay un MCU y `u0.PD12` si hay varios.

**Estado.** Ya se puede poner **dos STM32F407 en una placa, hablarse por un bus
y depurarlos a la vez, cada uno en su puerto de GDB**: `placas/dos_mcu.xml`.
Están hechos los nodos compartidos (§4.3), el elemento `<mcu>` con sus stubs
(§5.1 y §5.2), la regla de nombres (§3) y las cuatro cosas que estaban mal (§7).
Y desde la fase 1 del plan del F446, lo que hacía falta para un MCU **distinto**
también está: la interfaz `mcu_if` con su factoría (§6.1) y el encapsulado como
dato de instancia (§6.3) están implementados, así que `tipo=` **despacha** en vez
de comprobarse. Lo que falta ya no es andamiaje, es el chip: un `Stm32F446` con
su árbol de reloj y sus periféricos propios. Cada sección dice en qué estado
está.

---

## 1. Resumen

| | | Estado |
| :--- | :--- | :--- |
| **Lo fácil** | El esquema de nombres `u0.PD12`. El `NodeMap` es un mapa de cadenas: admite prefijos sin tocar nada de su lógica | **hecho** (`registra_mcu(prefijo, …)`) |
| **Lo medio** | Un elemento `<mcu>` en el XML y una interfaz `mcu_if` con su factoría. La superficie que `sim_main.cpp` usa del MCU son **cuatro cosas** | `<mcu>` **hecho**; `mcu_if` **hecho** (§6.1) |
| **Lo difícil** | **Unir un pin de `u0` con un pin de `u1`.** Cada pad poseía su propio `AnalogNet` y lo ataba a un `sc_port` en el constructor. Es el único cambio que toca el interior del MCU | **hecho** (`une=`, `Cableado`) |
| **Lo que ya no estorba** | No hay estado global mutable que impida dos instancias. SystemC ya separa las jerarquías por el nombre del módulo | — |

El trabajo se puede hacer por partes y cada una sirve por sí sola. Lo importante
es no empezar por el final: **el esquema de nombres primero, la unión de pines
al final**, porque esa última decisión es la que condiciona todo lo demás.

Y resultó que la unión de pines vale por sí sola, sin necesidad de un segundo
MCU: unir dos pines del **mismo** chip es el mismo mecanismo y es lo que se ha
implementado primero (§4.5). Fue una manera de pagar el paso caro con un caso
pequeño que además se puede probar.

---

## 2. Lo que ya no estorba

Antes de las modificaciones conviene decir lo que *no* hace falta cambiar,
porque acota el problema.

**No hay estado global mutable en el modelo.** Busqué explícitamente por si
había contadores, cachés o singletons que dos instancias se pisarían: no los
hay. Todo el estado vive en miembros de módulo. Las únicas dos excepciones son
cosméticas y están en §7.

**SystemC ya separa las jerarquías.** Los hijos de un módulo llevan su nombre
por delante, así que `u0.pinmux.net_A5` y `u1.pinmux.net_A5` son objetos
distintos con nombres distintos. No hay colisión que resolver, solo un nombre
que **leer bien** (§7.1).

**La variante ya es un parámetro de instancia.** `Stm32F407VG` se construye con
`(nombre, DebugCaps)`, y `DebugCaps` decide si el depurador va por los pines o
por dentro. El patrón de «rasgos por instancia» que atraviesa todo el proyecto
—`UsartCaps`, `OtgCaps`, `EthCaps`…— ya está aplicado al propio MCU. Añadir
encapsulado o variante es más de lo mismo.

**La superficie del MCU es pequeña.** Todo lo que `sim_main.cpp` le pide son
cuatro cosas:

```cpp
nodos.registra_mcu(dut->pinmux, dut->pwr_pads);   // los pines
dut->rcc.set_internal_waveforms(g_ondas);          // las ondas de reloj
dut->pwr_pads.vdd.set_drive(...);                  // la alimentación
ImageLoader ld(*dut);                              // cargar el firmware
```

Eso es lo que tendría que declarar una interfaz `mcu_if`. No es poco, pero es
acotado y conocido.

---

## 3. Los nodos: `u0.PD12`

Es la parte fácil, y la propuesta del enunciado es la correcta.

`NodeMap` es un `std::map<std::string, Nodo>`. Los nombres los pone
`registra_mcu()`, que hoy escribe `PA0`…`PI15` y los diez de alimentación. Con
un prefijo:

```cpp
void registra_mcu(const std::string& prefijo, PinMux& pm, PowerPads& pp);
//   prefijo vacío  ->  "PD12"
//   prefijo "u0"   ->  "u0.PD12"
```

Ni una línea más de lógica: el mapa no sabe ni le importa que el nombre lleve un
punto.

**La regla de compatibilidad** que hace que esto no rompa nada:

| MCUs en el fichero | Cómo se nombran los pines |
| :--- | :--- |
| Ninguno declarado | Un STM32F407VG implícito, nombres desnudos: `PD12`. **Es el comportamiento de hoy** |
| Uno, `<mcu id="u0"/>` | Valen los dos: `PD12` y `u0.PD12` |
| Dos o más | **Solo con prefijo.** `PD12` a secas es un error que nombra la ambigüedad |

Con esa regla, las tres placas de `placas/` siguen funcionando sin tocarlas, la
comprobación de ida y vuelta por XML sigue pasando y `banco.xml` no cambia. La
compatibilidad no es cortesía: es lo que permite hacer el cambio sin una
migración.

El error del tercer caso merece ser bueno, porque será el más frecuente:

```
[decl] LD4.anodo: 'PD12' es ambiguo, hay 2 MCUs. Escribe u0.PD12 o u1.PD12
```

### 3.1 Con un MCU llamado `u0`, ¿hay que declarar el nodo de PD12?

No. Un LED puede seguir escribiendo `nodo="PD12"` y no hay que declarar nada.
Hay **dos razones independientes**, y conviene no mezclarlas porque responden a
preguntas distintas.

**Por el nombre.** La regla de la tabla de arriba no depende de que el MCU sea
implícito, sino de que **haya uno solo**. Con exactamente un `<mcu id="u0">`,
`registra_mcu` inscribe cada pad con el nombre cualificado `u0.PD12` y además el
alias desnudo `PD12`; los dos resuelven al mismo nodo. `<nodo>` solo es
obligatorio para los nodos **externos** (`externo="si"`), que son los que no
existen si nadie los declara. Los pads existen desde el instante en que se
construye el `PinMux`: declararlos nunca ha hecho falta ni la hará.

**Por la mecánica de la opción B.** Esto es lo que podría haber roto la
compatibilidad, y no la rompe. Un pad deja de crear y poseer su `AnalogNet`
**solo si algún `<nodo … une="…">` lo nombra explícitamente**. El constructor
recibe un `Cableado` (pad → nodo compartido ya creado) y, para cada pad, hace lo
de siempre salvo que aparezca en ese mapa:

* pad citado en un `une` → `pad[p][i]->net(*cableado[p][i])`, con la red creada
  antes por el lector;
* pad no citado → `net[p][i] = new AnalogNet(nm); pad[p][i]->net(*net[p][i]);`,
  exactamente el código de siempre.

En una placa de un solo MCU sin puentes nadie une pads con pads, luego el
`Cableado` llega vacío y el bucle del constructor se comporta byte a byte como
antes. **El coste para una placa sin nodos compartidos es literalmente cero**, y
las tres placas de `placas/` siguieron siendo válidas sin tocar una línea.

**La asimetría que conviene tener presente.** `une` no nombra nodos externos:
nombra **pads**, y crea un nodo que *sustituye* al que esos pads habrían creado.
Por eso:

* un nodo que fusiona pads —de dos MCUs, o de dos pines del mismo— **hay que
  declararlo**: es la única forma de decírselo al constructor antes de que
  construya;
* un pad al que solo se cuelgan piezas externas (el LED) **no se declara
  nunca**: la pieza se engancha al `AnalogNet` que el pad ya posee.

Declarar `<nodo id="u0.PD12"/>` sin `une` seguiría siendo legal y puramente
documental: el `NodeMap` ya tiene esa entrada y la declaración no haría más que
ratificarla. Un `une` con un solo pad también sería legal en apariencia, pero no
uniría nada, y por eso se rechaza con un error propio.

**El detalle que había que cuidar**, y así se resolvió: con un solo MCU, los dos
nombres se dan de alta apuntando al **mismo `AnalogNet`**, no a dos. `sim` llama
a `registra_mcu("u0", …)` y después a `registra_mcu("", …)`, y `NodeMap::registra`
es idempotente cuando el nodo es el mismo (§7.2). Así, un `une="u0.PD12"` y un
`nodo="PD12"` acaban en el mismo sitio, que era el fallo silencioso que se temía.

El precio es que el mapa tiene más **nombres** que **nodos**, y por eso
`NodeMap::n_nodos()` cuenta `AnalogNet` distintos: decir «308 nodos» de una placa
que tiene 154 es mentir con una cifra exacta.

---

## 4. El problema difícil: unir un pin de `u0` con uno de `u1`

Aquí está el verdadero contenido del análisis. Todo lo demás es fontanería.

### 4.1 Por qué no es inmediato

Dos MCUs en una placa se comunican, y comunicarse quiere decir **compartir un
nodo eléctrico**: el SCL de `u0` y el de `u1` son el mismo hilo, no dos hilos
parecidos. Pero hoy:

```cpp
// pins/pin_mux.h, constructor
net[p][i] = new AnalogNet(nm);      // cada pad CREA su nodo
pad[p][i]->net(*net[p][i]);         // y lo ata a un sc_port
```

Cada pad **posee** su `AnalogNet`, y `Pad::net` es un `sc_port<analog_net_if>`
que se ata en el constructor. Un `sc_port` no se puede reatar después. De modo
que la elección de qué nodo usa un pad **tiene que tomarse cuando se construye
el MCU**, no después.

Es exactamente la lección del paso 2 con `CanWire`, que creaba su propio
`AnalogNet` y hubo que darle la opción de recibirlo de fuera: **un nodo
pertenece al circuito, no al componente que se cuelga de él.** Lo que cambia es
que ahora el componente es el MCU.

### 4.2 Opción A — un acoplador entre nodos

No tocar el MCU: añadir una pieza que observe N nodos y los empuje a parecerse.
Es exactamente lo que ya hace `I2cWire`, que cortocircuita N pines del mismo
chip.

**A favor:** barato, ya existe el precedente y funciona, no toca el interior del
MCU y se puede tener esta semana.

**En contra, y es serio:** no es un cortocircuito, es una *simulación* de
cortocircuito. Mirando lo que `I2cWire` hace de verdad:

* solo propaga el **cero**. Lee cada nodo con `voltage_excluding` y, si alguno
  está bajo, tira de los demás a 0 con 20 Ω. Un uno push-pull no se propaga:
  para eso está el pull-up. Sirve para colector abierto —que es su caso— y **no
  sirve para un bus push-pull** como SPI o un GPIO cualquiera entre dos MCUs;
* cuesta un delta. El acoplador reacciona *después* de que el nodo cambie, así
  que los dos lados no valen lo mismo en el mismo instante;
* y las tensiones son aproximadas. Con dos drivers fuertes enfrentados a los dos
  lados, el resultado no es la superposición que daría un nodo único, sino dos
  nodos con dos resoluciones distintas acopladas por un tercero.

Para un bus I2C entre dos MCUs, la opción A es correcta y suficiente. Para
cualquier otra cosa, es una aproximación que habrá que explicar cada vez.

### 4.3 Opción B — nodos de verdad compartidos  *(implementado)*

Que el pad reciba su `AnalogNet` desde fuera cuando la placa diga que ese pin va
a un nodo compartido:

```xml
<mcu tipo="STM32F407VG" id="u0"/>
<mcu tipo="STM32F407VG" id="u1"/>

<nodo id="n_scl" externo="si" bus="si" une="u0.PB6 u1.PB6"/>
<nodo id="n_sda" externo="si" bus="si" une="u0.PB7 u1.PB7"/>
```

`une` es la lista de pads que **son** ese nodo. No hay acoplamiento ni retardo:
hay un `AnalogNet` y dos pads registrados en él como drivers, y la superposición
los resuelve juntos, exactamente igual que resuelve hoy un pad contra un LED.

**Lo que cuesta.** Hay que invertir el orden de construcción, que hoy es:

```
construir el MCU  ->  leer el XML  ->  construir las piezas
```

y pasaría a ser:

```
leer el XML  ->  crear los nodos compartidos  ->  construir los MCUs
             ->  registrar los nodos  ->  construir las piezas
```

Es viable **porque leer no construye**: `netlist_desde_fichero` devuelve datos.
Ese fue un acierto del paso 3 que aquí se cobra solo.

Y hay que dar a `PinMux` la forma de recibir nodos de fuera. Por el `sc_port`,
tiene que ser en el constructor.

Así ha quedado, que es casi exactamente lo que decía este análisis:

```cpp
// pins/pin_mux.h
class Cableado {                          // qué pads NO crean su nodo
public:
    void           une(unsigned port, unsigned pin, analog_net_if& n);
    analog_net_if* busca(unsigned port, unsigned pin) const;
};
bool pad_desde_nombre(const std::string& s, unsigned& port, unsigned& pin);

PinMux(sc_module_name nm, const Cableado& cab = Cableado());
Stm32F407VG(sc_module_name nm, DebugCaps dbg = DBG_PINES,
            const Cableado& cab = Cableado());

// parts/netlist.h
std::string cableado_desde_netlist(const Netlist& nl, NodeMap& nodos, Cableado& cab);
```

`PinMux` guarda ahora dos matrices: `net[p][i]`, los nodos que **posee** y
destruirá —`nullptr` en los pines con puente—, y `nodo[p][i]`, el que el pad usa
de verdad, que es el que devuelve `analog()`. La diferencia entre las dos es
justo la respuesta a «¿de quién es este punto eléctrico?», y ahora está escrita.

Los pads que nadie menciona siguen creando el suyo, así que **el coste para una
placa de un solo MCU es exactamente cero**: el mapa está vacío y el bucle del
constructor hace lo de siempre.

Lo único que el análisis no había previsto es que el nombre del nodo compartido
y el del pad designan el **mismo** `AnalogNet`, y que la validación agrupa por
nombre. Un nodo con `une` se registra como externo, no como pin, así que había
que enseñarle dos cosas: que un puente no puede «quedar flotante por culpa de la
placa» —al otro lado hay pads— y que declararlo `bus` por el nombre del puente o
por el de cualquiera de sus pads tiene que querer decir lo mismo
(`es_bus_efectivo`).

### 4.4 Recomendación

**Opción B**, y la A solo si hace falta algo funcionando antes.

El argumento no es de elegancia. Este proyecto modela la frontera del
encapsulado en `float` —tensiones, corrientes, alta impedancia, sobrecorriente
en el pad— precisamente para que los conflictos eléctricos salgan solos. Un
acoplador que aproxima tensiones y añade un delta desactiva justo eso en el
punto donde dos chips se pelean por un hilo, que es donde más falta hace. Sería
gastar la precisión del modelo en el sitio equivocado.

Además, la opción B deja el `--valida` funcionando sin cambios: con un nodo real
compartido, dos MCUs conduciendo a la vez sobre él es el mismo aviso de
conducción simultánea que ya existe, y `bus="si"` sigue siendo la forma de decir
«es a propósito».

### 4.5 Unir dos pines del MISMO MCU

Es la misma pregunta con un chip menos: una salida PWM de un temporizador y la
entrada de captura de otro, un pin que se lee a sí mismo, un puente de placa
entre dos pines. Y tiene dos respuestas, porque hay dos maneras de juntar dos
pines y **no son la misma cosa**.

#### Lo que ya se podía hacer: una PISTA

El banco lleva ese montaje desde F4:

```xml
<componente tipo="SignalLink" id="lnk_pwm" conectada="no">
  <pin nombre="origen"  nodo="PD12"/>   <!-- TIM4_CH1, la salida PWM -->
  <pin nombre="destino" nodo="PB4"/>    <!-- TIM3_CH1, la entrada de captura -->
</componente>
```

T41 y T43 lo sueldan con `set_enabled(true)`, miden el periodo del PWM que el
propio MCU genera, y lo despegan al terminar.

`SignalLink` **no es un cable: es un buffer unidireccional.** Lee el origen con
`add_ref` —o sea, se declara oyente y no le pone driver, luego no lo carga—, le
aplica el umbral con histéresis de un pad de entrada (`0,45·vdd` / `0,55·vdd`) y
conduce el destino a 0 o a `vdd` con 50 Ω. Su propia cabecera lo dice: *«un hilo
compartido de verdad se modela conectando los dos pines al MISMO AnalogNet, no
con esta pieza»*.

Para medir un PWM eso es fiel, y por tres razones:

* **direccionalidad**: PWM → captura tiene un único emisor, así que perder la
  bidireccionalidad no pierde nada;
* **retardo cero en tiempo simulado**: el hilo despierta en el
  `value_changed_event` del origen, luego la propagación cuesta un *delta cycle*,
  que no avanza `sc_time`. TIM3 sella el mismo instante que el flanco de TIM4;
  por eso T43 comprueba 1000 µs con 3 % de tolerancia y sale clavado;
* **se puede despegar**: `conectada="no"` por defecto y `set_enabled(false)` al
  terminar dejan PB4 libre para el codificador, que es otra prueba de la misma
  placa.

Lo que a cambio no es fiel: el origen no ve carga —la corriente que sale de PD12
es la de un pin al aire— y el destino recibe un 0/3,3 regenerado aunque el origen
esté en una tensión intermedia.

#### Lo que hacía falta la opción B: un NODO

Cuando lo que se quiere es un punto eléctrico de verdad —medio dúplex, colector
abierto, un pull-up externo compartido, o simplemente que el modelo detecte que
has configurado los dos pines como salidas en sentidos opuestos, que con
`SignalLink` no es un conflicto sino un pisotón silencioso— hace falta un nodo
compartido. Y es **el mismo mecanismo** que entre dos MCUs, sin una línea de más:
el `Cableado` es un mapa pad → nodo, y no le importa a qué chip pertenece cada
pad.

```xml
<nodo id="n_puente" externo="si" une="PB9 PD3"/>
```

En C++, con la placa declarada a mano:

```cpp
placa.nodo_une("n_puente", {"PB9", "PD3"});
Cableado cab;
cableado_desde_netlist(placa, nodos, cab);     // crea el nodo, rellena el mapa
dut = new Stm32F407VG("dut", dbg_caps, cab);   // y solo entonces el MCU
```

El banco lo lleva montado y **T122** lo comprueba: que `nodos["PB9"]` y
`nodos["PD3"]` son el mismo objeto; que el nivel pasa en los dos sentidos, cosa
que una pista no puede hacer; y —lo que de verdad justifica el trabajo— que con
los dos pines conduciendo a la vez el nodo se queda a **1,65 V** y los **dos**
pads avisan de sobrecorriente. Dos buffers de 55 Ω enfrentados hacen pasar 30 mA
por cada uno, por encima de los 25 mA del máximo [IR, §2.4]. Con un acoplador
entre dos nodos ese conflicto no existiría: uno de los dos simplemente pisaría al
otro.

#### El precio, que es real, y por eso las dos cosas conviven

**Un nodo compartido es soldadura permanente.** Se decide en la elaboración, el
`sc_port` del pad no se reata, y por tanto `conectada="no"` no tiene sentido para
esa unión. El banco usa precisamente el poder despegar la pista para que PB4
sirva después al codificador. De ahí la regla práctica:

| | Pista (`SignalLink`) | Nodo compartido (`une`) |
| :--- | :--- | :--- |
| Sentido | uno | los dos |
| Retardo en tiempo simulado | ninguno (un delta) | ninguno (es el mismo nodo) |
| Tensiones | regeneradas a 0/vdd | superposición real |
| Carga sobre el origen | ninguna | la que toque |
| Conflicto entre los dos extremos | invisible | sale: media tensión y sobrecorriente |
| Se puede desoldar en marcha | **sí** | no |

Pista para lo unidireccional y para lo que haya que soldar y desoldar entre
pruebas; nodo compartido para lo que eléctricamente **es** un solo punto.

**Una salvedad honesta sobre el validador.** Hoy avisa de conducción simultánea,
no de tensiones incompatibles (punto I-14 del TODO). Los pads del MCU no cuentan
como conductores en ese análisis —quien conduce lo decide el firmware en tiempo
de ejecución, y la validación es estática—, así que un puente entre dos pines sin
piezas colgadas no produce ningún aviso, que es lo correcto. En cuanto haya
piezas externas en los dos extremos, valdrá `bus="si"` como para cualquier otro
nodo compartido.

---

## 5. El XML

### 5.1 El elemento `<mcu>`  *(implementado)*

Una cuarta entidad, junto a `<nodo>`, `<componente>` y `<ref>`. La placa
`placas/dos_mcu.xml` es exactamente esto y funciona:

```xml
<placa nombre="dos-efe-cuatro">
  <mcu tipo="STM32F407VG" id="u0" depuracion="dap"   puerto_gdb="3333"/>
  <mcu tipo="STM32F407VG" id="u1" depuracion="pines" puerto_gdb="3334"/>

  <nodo id="n_scl" externo="si" bus="si" une="u0.PB6 u1.PB6"/>
  <nodo id="n_sda" externo="si" bus="si" une="u0.PB7 u1.PB7"/>

  <componente tipo="Rpull" id="R1" v="3.3" r="4700"><pin nombre="a" nodo="n_scl"/></componente>
  <componente tipo="Rpull" id="R2" v="3.3" r="4700"><pin nombre="a" nodo="n_sda"/></componente>

  <componente tipo="Crystal" id="X0" hz="8e6"><pin nombre="osc_in" nodo="u0.PH0"/></componente>
  <componente tipo="Crystal" id="X1" hz="8e6"><pin nombre="osc_in" nodo="u1.PH0"/></componente>
  <componente tipo="Led" id="LD0"><pin nombre="anodo" nodo="u0.PD12"/></componente>
</placa>
```

| Atributo | Omisión | Qué hace |
| :--- | :--- | :--- |
| `tipo` | obligatorio | El modelo de MCU. Hoy solo se sabe construir `STM32F407VG`, y cualquier otro se rechaza diciéndolo (§6.1) |
| `id` | obligatorio | El prefijo de sus nodos, y su nombre de módulo en SystemC |
| `firmware` | ninguno | La imagen que se le carga. **Una por MCU**, que es lo que hace útil tener dos |
| `depuracion` | `pines` | `pines` o `dap`, el `DebugCaps` que ya existía. Por instancia, así que un chip puede llevar el stub interno y el de al lado el de pines |
| `puerto_gdb` | 0 | Puerto TCP de su stub. **0 = no se abre ninguno**, que es lo que hace que ningún XML existente empiece a escuchar por sorpresa |

`encapsulado` no está: mientras todos los MCUs sean LQFP100 no hace falta, y
cuando haga falta va con §6.3, que sigue pendiente.

**Un MCU no lleva hijos.** Un `<pin>` dentro de un `<mcu>` se rechaza: sus 144
pads existen sin declararlos, y decirlo así evita que alguien intente describir
un chip como si fuera un componente (§9).

### 5.2 Los stubs de GDB, uno por MCU  *(implementado)*

Es lo que de verdad se gana con `<mcu>`, porque es lo primero que se necesita en
cuanto hay dos chips: poder pararlos por separado.

```
$ ./build/sim placas/dos_mcu.xml
placa 'dos-efe-cuatro': 2 MCU(s), 6 componentes, 306 nodos, 0 avisos
  mcu u0: sin firmware, gdb por dap en el puerto 3333
  mcu u1: sin firmware, gdb por pines en el puerto 3334
[gdb-dap] escuchando en localhost:3333
[gdb] escuchando en localhost:3334
esperando a GDB; la simulacion no se detiene sola (Ctrl-C para salir)
```

Dos sesiones a la vez, cada una contra su chip y **por transportes distintos**:
`u0` por llamada de función contra el DAP, `u1` bit a bit por SWCLK/SWDIO como
un ST-LINK. Los dos responden `qSupported` y `?` de forma independiente.

Con algún stub escuchando, `sim` **no se detiene solo**: un depurador necesita
que el modelo siga ahí. El argumento de milisegundos deja de aplicarse y se
dice por pantalla.

Dos MCUs con el mismo `puerto_gdb` se rechazan antes de montar nada. No es una
delicadeza: el segundo `bind()` fallaría en silencio y el síntoma sería un GDB
conectado al chip equivocado.

#### La línea de órdenes

| MCUs declarados | Qué pasa con `firmware.bin`, `--gdb`, `--gdb-dap`, `--port=` |
| :--- | :--- |
| ninguno, o uno | **Siguen sirviendo y mandan sobre el XML.** El fichero es la configuración y el argumento es la intención inmediata, así que se puede depurar la placa de otro sin editarla |
| dos o más | **Se rechazan**, nombrando los MCUs: «la placa lleva 2 MCUs (u0, u1), así que un firmware o un puerto sueltos no dicen a cuál». Elegir uno por nuestra cuenta cargaría el firmware en el chip equivocado, que es de los fallos más caros de diagnosticar |

El tiempo simulado es la excepción, porque **sí** es global: hay un solo reloj de
simulación por muchos chips que haya. Como argumento posicional va detrás del
firmware, que con varios MCUs ya no se pone ahí; de ahí `--ms=2`, que es la
forma utilizable entonces.

### 5.3 El orden y su validación

Los `<mcu>` se leen **antes** que todo lo demás, igual que hoy los `<nodo>` se
leen antes que los `<componente>`. La validación gana tres casos:

* un `<pin>` que nombra `u2.PA0` sin que exista el MCU `u2`;
* un `une` que nombra un pad de un MCU que no existe, o dos veces el mismo pad;
* un nombre desnudo con dos MCUs declarados (§3).

### 5.4 El atributo `une`  *(implementado)*

De todo §5, esto es lo único que ya existe, porque es lo que la opción B
necesitaba y no depende de que haya `<mcu>`:

```xml
<nodo id="n_puente" externo="si" une="PB9 PD3"/>
```

| Atributo | Omisión | Qué hace |
| :--- | :--- | :--- |
| `id` | obligatorio | El nombre del nodo. Vale igual que el de un pad para conectarse a él |
| `une` | ninguno | Los pads que **son** este nodo, separados por espacios. Al menos dos |
| `externo` | implícito con `une` | Un nodo con `une` lo crea la placa, nunca un pad |
| `bus` | `no` | Como en cualquier otro nodo: «que conduzcan varios es a propósito» |

Se valida sin construir nada: un nombre que no es un pad, un pad que el
encapsulado no saca, el mismo pad en dos puentes y un `une` con un solo pad son
cuatro errores distintos, cada uno con su mensaje. Y sobrevive a la ida y vuelta
por fichero: `volcar_xml` lo escribe **siempre**, aunque el puente no lleve
ninguna pieza colgada, porque un puente es placa aunque no tenga nada encima.

Cuando llegue el `<mcu>`, `une` pasará a admitir `u0.PB6 u1.PB6` con la misma
regla de nombres de §3. Hoy solo admite nombres desnudos, que es lo único que
puede significar algo con un MCU.

### 5.5 Lo que NO cambia

`<componente>`, `<pin>` y `<ref>` no se tocan. Un componente externo sigue sin
saber a qué MCU va conectado, y no tiene por qué saberlo: se conecta a un nodo y
punto. Esa indiferencia es la que hace que el catálogo entero de 21 piezas valga
para dos MCUs sin una línea nueva.

---

## 6. El C++

### 6.1 `mcu_if`, con su factoría  *(implementado)*

Hoy `tipo=` se comprueba, no se despacha: `sim_main.cpp` acepta
`STM32F407VG` y rechaza cualquier otro nombre diciendo cuál conoce. Es
suficiente mientras solo haya un modelo de MCU —una factoría con un solo tipo
registrado es andamio sin obra— y deja de serlo el día que haya dos.

Para que `tipo=` signifique algo de verdad hay que hacer con los MCUs lo que el
paso 3 hizo con las piezas: una interfaz y un registro por cadena.

```cpp
class mcu_if {
public:
    virtual ~mcu_if() = default;
    virtual void registra_nodos(const std::string& pref, NodeMap&) = 0;
    virtual bool carga_firmware(const std::string& ruta) = 0;
    virtual void arranque_electrico(bool on) = 0;      // VDD, NRST, BOOT0
    virtual void set_ondas_reloj(bool) = 0;
};
```

Cuatro métodos, que son las cuatro cosas de §2. `Stm32F407VG` los implementa
delegando en lo que ya tiene; ningún periférico se entera.

Una `FabricaMcu` paralela a la de piezas, con la misma macro de auto-registro.
Se puede escribir en media hora porque el patrón ya está resuelto.

> **HECHO en la fase 1 del plan del F446** (`doc/stm32f4xx/stm32f407vg_vs_446re.md` §16):
> está en `soc/mcu_if.h`, y el adaptador de la familia F405/407 en
> `soc/stm32f4_mcu.h`. Lo que cambió respecto a este boceto, y por qué:
>
> * **son nueve métodos, no cuatro.** Aparecieron `caps()`, `nodo_analogico()`
>   —por donde se engancha la sonda SWD a PA13/PA14—, `reset_pin()` separado de
>   la alimentación, y los dos interruptores del stub interno de GDB. Ninguno es
>   un periférico: siguen siendo las cuatro cosas de §2 más lo que `sim` ya le
>   pedía al puntero concreto;
> * **la factoría se indexa por FAMILIA, no por nombre de pieza.** No estaba
>   previsto y es lo que la hace útil: los once miembros del F405/407 son la
>   misma clase con descriptores distintos, así que comparten un creador, y el
>   creador recibe además el `McuCaps`. Un tipo nuevo de la misma familia sigue
>   siendo una línea en el catálogo;
> * **`Stm32F407VG` no hereda de `mcu_if`**: lo envuelve un adaptador. Meterle
>   una herencia virtual más a un `sc_module` con ciento y pico submódulos sería
>   pagar en el sitio equivocado, y el adaptador resultó ser además el sitio
>   natural para los cuatro índices de driver de la alimentación y el orden de
>   arranque, que eran campos sueltos en una `struct` de `sim_main`;
> * y una familia sin modelo enlazado devuelve **`nullptr`**. `sim` lo convierte
>   en un error con nombre; nunca monta otro chip en su lugar.

### 6.2 `ImageLoader`

Hoy es `ImageLoader(Stm32F407VG&)`. Pasa a construirse sobre `mcu_if`, o
—más simple— desaparece del `sim` y se convierte en el `carga_firmware()` de la
interfaz, que es donde el conocimiento de dónde está la Flash pertenece de todos
modos.

### 6.2b Los rasgos del chip, como dato  *(implementado)*

Lo que `mcu_if` necesitaría **devolver** ya existe. `CoreCaps` (líneas de IRQ,
bits de prioridad, regiones de MPU, FPU, CPUID), `MapaFlash`/`MapaRam` (tamaños,
tabla de sectores, curva de estados de espera) y `McuCaps`, que los junta con los
topes de reloj, son ahora **datos de instancia**, y los reciben `Scs`, `Mpu`,
`CortexM4F`, `FlashIf`, `AhbMatrix`, `Rcc` y el propio top, todos con el F407 por
omisión. Un F405 —el mismo silicio sin Ethernet ni cámara— se describe cambiando
números; un F446 no, y el campo `McuCaps::familia` es donde está escrita esa
frontera. Los detalles y lo que sigue soldado, en
`doc/reutilizacion.md`.

### 6.3 Cambiar de MCU: el encapsulado

Para *cambiar* el MCU, y no solo repetirlo, hay un obstáculo concreto:

```cpp
static bool is_bonded_lqfp100(unsigned port, unsigned pin);   // pins/pin_mux.h
```

El encapsulado está **cableado como función estática**. Mientras haya un solo
chip y un solo encapsulado da igual; con dos MCUs de encapsulados distintos, no.
Tiene que pasar a ser un dato de instancia —un predicado o una máscara por
puerto— que el constructor recibe. Es un cambio pequeño y mecánico, pero hay que
hacerlo antes de que exista un segundo encapsulado, no después.

`NodeMap` ya guarda `bonded` por nodo, así que la validación de «ese pad no sale
al encapsulado» sigue funcionando sin cambios en cuanto el dato deje de ser
estático.

---

## 7. Cuatro cosas concretas que estaban mal

No eran especulaciones: son sitios del código que se rompen o mienten en cuanto
haya un segundo MCU. **Las dos primeras están arregladas**; las dos últimas
siguen ahí, y se quedan documentadas a propósito porque son cosméticas y de
configuración, no de arquitectura.

### 7.1 El volcado del inventario daba nombres ambiguos  *(arreglado)*

```cpp
// parts/part_base.h, nombre_nodo() — la versión que había
std::string b = o->basename();                 // "net_A5"
if (b.rfind("net_", 0) == 0) return "P" + b.substr(4);   // -> "PA5"
```

`basename()` devuelve el nombre **sin la jerarquía**, así que el pad `PA5` de
`u0` y el de `u1` se volcaban los dos como `PA5`. No es un error que salte: es un
volcado que miente, que es peor.

La traducción se ha mudado a `common/nombres_nodo.h`, que parte del nombre
**jerárquico** (`u0.pinmux.net_A5`) y cualifica el pad con el nombre del módulo
que lo contiene **cuando hay más de un MCU**:

```cpp
inline unsigned& n_mcus();                       // lo cuenta el ctor de PinMux
inline std::string nombre_nodo(const analog_net_if&);
//   dut.pinmux.net_A5  ->  PA5        (un solo MCU: nada cambia)
//   u1.pinmux.net_A5   ->  u1.PA5     (dos o más)
//   tb.n_can           ->  n_can      (un nodo que no es un pad)
```

Vive en `common/` y no en `parts/` porque lo necesitan los dos lados: las piezas
para nombrar sus terminales, y los pines para contarse. Contar los MCUs en el
constructor de `PinMux` no es un truco: hay exactamente uno por MCU, y es el
único sitio que se entera sin que nadie tenga que acordarse de avisar.

Con un solo MCU el volcado sale idéntico al de antes, que era la condición para
poder hacerlo sin migrar nada.

### 7.2 El segundo MCU sobrescribía los nodos del primero  *(arreglado)*

`registra_mcu()` escribía nombres desnudos en un mapa plano. Llamarlo dos veces
sin prefijo hacía que los 154 nodos del segundo **pisaran** los del primero, en
silencio y sin error.

Dos cambios, y hacen falta los dos:

```cpp
void registra_mcu(const std::string& prefijo, PinMux&, PowerPads&);
void registra_mcu(PinMux&, PowerPads&);          // prefijo vacío: lo de siempre
```

y, en `NodeMap::registra()`, dar de alta **dos veces el mismo nombre con dos
`AnalogNet` distintos** pasa a ser un `SC_REPORT_ERROR` que dice qué hacer:

```
nodo duplicado: 'PD12' ya esta dado de alta con otro AnalogNet. Si la placa
lleva mas de un MCU, registralos con prefijos distintos: registra_mcu("u0", ...)
```

Registrar el mismo nombre con el **mismo** nodo sigue valiendo, y hace falta que
valga: ocurre de verdad cuando un pad forma parte de un nodo compartido que ya
estaba dado de alta por su nombre de placa.

El prefijo arregla el problema; el error hace que, si alguien se lo salta, se
entere. Un fallo mudo que se vuelve ruidoso vale más que un fallo mudo que se
evita por convenio.

### 7.3 Dos avisos de una sola vez  *(arreglado)*

```cpp
static bool warned = false;      // periph/adc.h:861 y periph/sdio.h:777
```

Eran banderas de «avisar una vez» compartidas por todas las instancias del
proceso. Con dos MCUs, **el aviso del segundo se lo tragaba el primero**: el chip
que falla es justo el que no avisa. Ahora son miembros —`aviso_adcclk_` y
`aviso_ck_`, `mutable` porque quien los mira es un método `const`— y cada chip
avisa de lo suyo.

Se dejó pendiente mientras no había un segundo MCU y se arregló al haberlo, que
es cuando el síntoma pasó de imposible a inevitable.

### 7.4 Un solo puerto de GDB  *(arreglado)*

El puerto salía de una variable global del banco. Ahora es un atributo por chip,
`puerto_gdb`, y `DebugCaps` ya lo llevaba por instancia, así que el modelo no
hubo que tocarlo: solo quien lo construye. Dos MCUs con el mismo puerto se
rechazan antes de montar nada (§5.2), porque el segundo `bind()` fallaría en
silencio y el síntoma sería un GDB conectado al chip equivocado.

---

## 8. Orden de trabajo y coste

Cada paso deja el árbol funcionando y verificable con la suite.

| | Trabajo | Tamaño | Qué desbloquea | Estado |
| :--- | :--- | :--- | :--- | :--- |
| 1a | §7.1 nombre jerárquico + §7.2 prefijo y error ruidoso | pequeño | Fallos mudos: mejor antes | **hecho** |
| 1b | §7.3 los dos `static bool warned` + §7.4 el puerto de GDB | pequeño | Que cada chip avise de lo suyo y depure en su puerto | **hecho** |
| 2 | `<mcu>` en el XML + la regla de compatibilidad de §3 | pequeño | `u0.PD12` funciona; firmware, depuración y puerto por chip | **hecho** |
| 3 | `mcu_if` + factoría de MCUs + `ImageLoader` sobre la interfaz | medio | `tipo=` despacha en vez de comprobar | **hecho** (§6.1) |
| 4 | Encapsulado como dato de instancia | pequeño | Dos MCUs **distintos** | pendiente (§6.3) |
| 5 | **Nodos compartidos** (`une`, `Cableado`, `PinMux`) | el grande | Que dos pines cualesquiera sean el mismo punto | **hecho** |
| 6 | Placa de ejemplo con dos F407 por I2C, y su comprobación en la suite | medio | Que esto no se rompa sin que nos enteremos | **a medias** |

El orden real fue 1a, 5, y luego 1b y 2 juntos. No es el de la tabla, y la razón
es la de §4.5: el paso 5 valía por sí solo para unir dos pines del **mismo** MCU,
que es un caso que cabe en el banco y se prueba hoy (**T122**). Se pagó el paso
caro con el caso pequeño, y cuando llegó el segundo chip lo difícil ya estaba
hecho y probado. El 1b se hizo con el 2 porque hasta entonces su síntoma era
imposible y desde entonces es inevitable.

Los pasos 3 y 4 son los que hacen falta para un MCU **distinto**, no para otro
igual, y no tienen riesgo apreciable: son un registro por cadena y un parámetro.

**El 6 está a medias, y conviene decirlo.** Hay una placa de dos F407 por I2C
—`placas/dos_mcu.xml`— y **T123** comprueba toda la capa de declaración: leer
`<mcu>`, resolver `u0.PD12`, rechazar el nombre ambiguo, el chip que no existe,
el puerto repetido y los cinco `<mcu>` mal escritos. Lo que **no** está
automatizado es el montaje: que dos chips se construyan, arranquen y se hablen
solo se comprueba a mano, corriendo `sim`. La suite monta un único `dut` del que
cuelgan la mitad de sus 1935 comprobaciones, y meter un segundo dentro sería
duplicar la elaboración de todo el banco para probar otra cosa. El sitio natural
es un banco aparte que corra `sim` sobre las placas de `placas/`, y eso todavía
no existe.

---

## 9. Lo que no haría

**No convertir el MCU en un componente más del netlist.** Es tentador —tiene
patillas, tiene un tipo, tiene un id— pero se rompe en cuanto se mira: un
componente **se conecta** a nodos y el MCU **los aporta**, y declarar sus 144
pads como `<pin>` sería absurdo. El elemento aparte es lo correcto.

**No modelar la alimentación como un nodo compartido todavía.** Dos MCUs en una
placa comparten VDD de verdad, y con la opción B saldría casi gratis (`une` con
`u0.VDD` y `u1.VDD`). Pero el modelo de consumo calcula la corriente que el chip
pide por sus pines, y dos chips sobre el mismo raíl con una fuente de impedancia
finita es un experimento que hay que hacer *después* de que lo demás funcione,
no a la vez.

**No inventar sintaxis para el reloj compartido.** Un cristal de un MCU
alimentando al otro por su `OSC_IN` es un montaje real, pero se describe ya con
lo que hay: un `Crystal` en `u0.PH0` y un nodo compartido con `u1.PH0`. No hace
falta nada nuevo.

**No abrir el resto hasta cerrar el paso 4 de la adopción.** El generador de SVG
tiene que dibujar el netlist, y si el netlist va a ganar una entidad —el MCU—,
mejor que la gane antes de que haya un dibujante que mantener. Es la única
dependencia de orden entre las dos líneas de trabajo, y apunta a hacer esto
**primero**.

Los nodos compartidos ya se cobraron su parte de esa deuda: el netlist ha ganado
un atributo (`une`) y el dibujante tendrá que representarlo —un nodo con dos
pads dentro no se dibuja como un nodo con un pad—. Que llegara antes que el SVG
y no después es exactamente lo que este párrafo pedía.
