# Pasar a SystemC 3.0 — análisis

*Qué habría que cambiar en el modelo para compilar contra la línea 3.0 de
Accellera, y si el cambio compensa. No es un plan aprobado: es el análisis
previo, con lo que se ha podido comprobar separado de lo que no.*

---

## 0. Cómo leer esto: qué está verificado y qué no

Este informe tiene dos mitades muy desiguales en solidez, y conviene no
mezclarlas.

**Lo verificado, y es la mitad que decide.** El inventario exacto de qué API de
SystemC usa este modelo. Está sacado del código con `grep` sobre los 45 279
líneas de `src/`, no de la memoria, y es lo que determina el coste de cualquier
migración. Aquí no hay opinión.

**Lo no verificado.** **No he podido examinar SystemC 3.0.** No está instalado en
este contenedor —donde hay 2.3.4, la de Ubuntu 24.04— y las fuentes de Accellera
no son alcanzables desde aquí: el acceso a GitHub está cerrado para esta sesión.
Así que todo lo que digo sobre qué trae la 3.0 viene de lo que sé, **no de haber
leído sus cabeceras**, y lo marco como tal. En particular **no sé qué distingue
la 3.0.2 de la 3.0.0 ni cuándo salió**; hablo de «la línea 3.0».

Por eso el informe está construido para que **la parte débil no cambie la
conclusión**: la pregunta «¿qué habría que cambiar?» se responde desde nuestro
lado, y la respuesta resulta ser tan corta que casi cualquier cosa que traiga la
3.0 la deja igual. Y §7 dice cómo comprobar lo demás en una tarde.

---

## 1. Resumen

| | |
| :--- | :--- |
| **Cambios necesarios en el código** | **Ninguno conocido.** El modelo usa exclusivamente la API estándar de IEEE 1666 más dos comodidades de `tlm_utils`. Tenía **un solo** punto que tocaba las tripas de SystemC, y se ha arreglado al escribir este informe (§3) |
| **El estándar de C++** | No es problema: ya se compila en C++17, y las cabeceras del modelo y de SystemC 2.3.4 pasan también por **C++20 y C++23** sin un error (§4.1) |
| **La ventaja real** | Una sola, y todavía **hipotética**: que la 3.0 haga más fácil construir SystemC en **Windows y macOS**, que es el hueco abierto (I-23) del que depende que el producto se pueda repartir |
| **La ventaja que NO hay** | **Rendimiento.** El coste de este modelo lo domina cuántas veces despiertan sus procesos, no el planificador; ya va entre 11 y 200 veces más rápido que el tiempo real |
| **El coste oculto** | Ubuntu 24.04 —y las distribuciones en general— traen **2.3.4** empaquetada. Pasar a la 3.0 significa **compilar SystemC** en las tres plataformas, y eso es lo contrario de lo que un proyecto que se reparte a alumnos quiere |
| **Recomendación** | **No migrar todavía**, y hacer la migración *barata* por si acaso — que en gran parte ya está hecha. La decisión debe tomarla el problema de Windows/macOS, no la lista de novedades |

---

## 2. Lo que este modelo usa de SystemC

*Verificado sobre `src/`, sin contar `verif/fw/` (que es firmware para
arm-none-eabi y no toca SystemC).*

### 2.1 El núcleo, y es todo lo que hay

| Construcción | Usos | Dónde |
| :--- | ---: | :--- |
| `sc_module`, `SC_MODULE`, `SC_CTOR`, `sc_module_name` | 143 | Todo el modelo |
| `SC_METHOD`, `SC_THREAD`, `SC_HAS_PROCESS`, `wait()` | 262 | Todo el modelo |
| `sc_time`, `SC_PS`/`SC_NS`/`SC_US`/`SC_MS`/`SC_SEC`, `SC_ZERO_TIME`, `sc_time_stamp()` | ~1 400 | Todo el modelo |
| `sc_signal<T>`, `sc_in`, `sc_out`, `sc_vector` | 434 | Interconexión interna |
| `sc_event`, `sc_event_or_list` | 70 | Esperas de proceso |
| `sc_prim_channel`, `sc_interface`, `sc_port`, `sc_export` | 7 | `AnalogNet`, `Pad::net`, `Scs::cpu_if` |
| `sc_spawn`, `sc_spawn_options` | 4 | `pins/pin_mux.h`: un proceso por pad |
| `sc_object`, `name()`, `basename()`, `dynamic_cast` | — | `common/nombres_nodo.h` |
| `sc_report_handler::set_actions`, `SC_REPORT_WARNING/ERROR` | 41 | Avisos por periférico |
| `sc_start`, `sc_stop`, `sc_main`, `sc_delta_count`, `sc_gen_unique_name` | 24 | Los tres ejecutables |
| `sc_trace`, `sc_trace_file` | 3 | `pins/pad.h`, para volcar `PadDrive` a VCD |

### 2.2 TLM

| Construcción | Nota |
| :--- | :--- |
| `tlm::tlm_generic_payload`, `tlm_response_status` y sus constantes, `tlm_extension`, `tlm_dmi` | Estándar |
| `tlm_utils::simple_initiator_socket` / `simple_target_socket` (+ variantes `_tagged`) | Utilidades oficiales, no parte del estándar |
| `tlm_utils::simple_target_socket_optional` | **El único que conviene mirar** (§5.3): existe desde 2.3.3 y no está en la norma |

### 2.3 Lo que llama la atención de esa lista

**No hay nada raro.** Ni `sc_fifo`, ni tipos de datos de SystemC (`sc_int`,
`sc_bv`, `sc_logic`: el modelo usa `uint32_t` y `float`), ni `sc_clock`, ni
`sc_mutex`/`sc_semaphore`, ni control de procesos (`suspend`/`kill`/`reset`), ni
`sc_attr`, ni herencia de `sc_channel`, ni nada del subconjunto de síntesis.

Es una superficie **pequeña, antigua y central**: justo la parte de IEEE 1666 que
lleva estable desde 2005 y que ninguna revisión del estándar puede permitirse
romper. Eso es lo que hace que la respuesta a «qué habría que cambiar» sea corta,
y no es casualidad: es la consecuencia de que el modelo esté escrito con
`sc_module`, procesos y señales y nada más.

---

## 3. El único sitio que tocaba las tripas — arreglado

Buscando cabeceras de SystemC en todo `src/`:

```
27  #include <systemc>
 8  #include <tlm>
 8  #include <tlm_utils/simple_initiator_socket.h>
 4  #include <tlm_utils/simple_target_socket.h>
 1  #include <sysc/kernel/sc_spawn.h>        <-- este
```

Los cuatro primeros son la API pública. El último, en `pins/pin_mux.h`, **no**:
es la distribución interna de cabeceras de la implementación de Accellera, y es
exactamente la clase de cosa que cambia entre versiones mayores sin que nadie lo
considere una ruptura, porque nunca fue API.

Estaba ahí por un motivo real: `sc_spawn()` no forma parte de lo que `<systemc>`
expone por omisión. La vía oficial es pedirla con una macro **antes** de incluir
`<systemc>`, y como el orden de inclusión no se controla desde una cabecera, esa
macro tiene que venir de la línea de órdenes. Incluir el fichero interno era el
atajo.

**Cambiado ya**, porque es la única modificación que este análisis exige y no
tiene sentido dejarla escrita en vez de hecha:

```make
# Makefile.stm32
DEFS := -DSC_INCLUDE_DYNAMIC_PROCESSES
```

```cpp
// pins/pin_mux.h
#ifndef SC_INCLUDE_DYNAMIC_PROCESSES
#  error "compila con -DSC_INCLUDE_DYNAMIC_PROCESSES: pin_mux.h usa sc_spawn()"
#endif
#include <systemc>
```

Falla con un mensaje que dice qué hacer en vez de con un error de enlazado sobre
`sc_spawn`. **Suite: 1899/1899, tiempo simulado idéntico al picosegundo.**

Con eso, **`src/` no incluye ni una sola cabecera interna de SystemC**, que es la
precondición de cualquier migración de versión y también de que la biblioteca se
pueda cambiar por otra implementación.

---

## 4. Lo que NO hay que cambiar, y por qué se sabe

### 4.1 El estándar de C++

La línea 3.0 sube el mínimo a **C++17**. El proyecto ya está en C++17 desde el
principio, así que no hay nada que hacer.

Y para saber si el código está atascado en C++17 o solo lo usa, lo he medido:

| Estándar | Resultado (`g++ -fsyntax-only -Wall -Wextra`) |
| :--- | :--- |
| `-std=c++17` | **0 errores** |
| `-std=c++20` | **0 errores** |
| `-std=c++23` | **0 errores** |

El modelo **y las cabeceras de SystemC 2.3.4** pasan por los tres. Es una
comprobación de sintaxis, no de enlazado —para enlazar hace falta una biblioteca
compilada con el mismo estándar—, pero descarta la sorpresa más habitual al
subir de versión.

Un aviso que ya está en el Makefile y que aquí vale doble: **el estándar tiene
que coincidir con el que se usó al compilar la biblioteca**. Si no, el enlazado
falla con un error sobre `sc_api_version_...`, que es una comprobación que
SystemC hace a propósito precisamente para esto.

### 4.2 La versión del estándar que implementa cada una

SystemC 2.3.4 declara en sus cabeceras:

```c
#define IEEE_1666_SYSTEMC     201101L      // /usr/include/sysc/kernel/sc_ver.h:66
```

es decir, **IEEE 1666-2011**. La línea 3.0 implementa **IEEE 1666-2023**. Esa
macro es la forma limpia de escribir código que valga para las dos si algún día
hiciera falta:

```cpp
#if defined(IEEE_1666_SYSTEMC) && IEEE_1666_SYSTEMC >= 202301L
    // lo nuevo
#else
    // lo de siempre
#endif
```

Hoy no hace falta ni una vez.

---

## 5. Lo que creo que cambia en la 3.0, y cómo comprobarlo

*Esta sección es la débil. No he leído las cabeceras de la 3.0.*

### 5.1 Lo que espero que sea indiferente

`sc_module`, `SC_CTOR`, `SC_METHOD`, `SC_THREAD`, `wait`, `sc_time`,
`sc_signal`, `sc_in`/`sc_out`, `sc_vector`, `sc_event`, `sc_prim_channel`,
`sc_port`, `sc_export`, `sc_spawn`, `sc_report_handler`, `sc_start`/`sc_stop` y
todo TLM-2.0 son el corazón de la norma. Una revisión que los rompiera dejaría
sin compilar a la industria entera. **Espero cero cambios**, y es la parte de la
predicción en la que más confío.

`SC_HAS_PROCESS` (45 usos) es el candidato más plausible a volverse innecesario
—tengo la impresión de que 1666-2023 permite declarar procesos sin él— pero
«innecesario» no es «prohibido»: seguiría compilando.

### 5.2 Lo que hay que mirar sí o sí

**Las funciones marcadas como obsoletas en 2.3 y retiradas en 3.0.** Una versión
mayor es el momento en que eso se hace, y no puedo enumerar la lista. Lo bueno es
que el inventario de §2 dice exactamente qué hay que cruzar contra esa lista, y
son unas treinta entradas, todas centrales.

**El sistema de construcción.** La 2.3.4 trae autotools y CMake; tengo entendido
que la 3.0 se apoya solo en CMake. Para nosotros es indiferente —consumimos
`include/` y una biblioteca— salvo por lo de §6.2, que es justo lo que importa.

### 5.3 El único punto concreto de riesgo

```cpp
tlm_utils::simple_target_socket_optional        // bus/*.h
```

`tlm_utils` **no es parte de la norma**: son utilidades que Accellera distribuye
con la implementación, y `..._optional` se añadió en 2.3.3. Al no estar en el
estándar, nada obliga a mantenerlo. Si desapareciera, el arreglo es conocido y
acotado: un socket normal con un `b_transport` que responda
`TLM_ADDRESS_ERROR_RESPONSE`, que es lo que el opcional hace cuando no está
enlazado.

Es una hora de trabajo en el peor caso, y es **el único riesgo identificado** de
toda la migración.

---

## 6. ¿Compensa? Las ventajas, contra los problemas reales de este proyecto

La pregunta no es «¿qué trae la 3.0?» sino «¿arregla algo que nos duela?». Los
problemas conocidos están medidos en los informes de este mismo directorio, así
que se puede contestar una por una.

### 6.1 Rendimiento — no

`doc/stm32f407vg_coste_simulacion.md` estableció la regla que gobierna el coste
de este modelo: **lo que cuesta es cuántas veces despierta un proceso, no lo
complicado que sea**. Y `doc/analisis_gui.md` §3 midió dónde estamos: entre
**11 y 200 veces más rápido que el tiempo real** según el firmware.

Un planificador más rápido en la biblioteca movería el término que **no** domina.
Y aunque lo moviera, el problema que tenemos con la velocidad es el contrario:
para la GUI hay que **frenar** la simulación, no acelerarla.

No es un motivo.

### 6.2 Construir en Windows y macOS — **puede que sí, y es el único**

Es el hueco abierto: **I-23**, «sin verificación en Windows ni en macOS», y el
paso 0b del orden de trabajo del análisis de la GUI. El modelo ya cruza a MinGW
—compila y enlaza un PE32+ sin un aviso— pero **falta la biblioteca de SystemC en
esas dos plataformas**, y eso es lo que bloquea repartir el programa a alumnos.

Si la línea 3.0, apoyada solo en CMake y más reciente, resulta más fácil de
construir con MinGW-w64 y en macOS que la 2.3.4, **esa sola razón justificaría el
cambio**, porque desbloquea el producto. Y si no lo es, no queda ninguna otra.

**No lo sé, y es exactamente lo que hay que averiguar** (§7). Es la única
pregunta de este informe cuya respuesta cambia la decisión.

### 6.3 Los sanitizers y las corrutinas — habría que mirarlo

`src/common/asan_opciones.h` documenta que ASan y las corrutinas de SystemC son
incompatibles si `detect_stack_use_after_return` está encendido: el registro de
marcos falsos es por hilo del sistema operativo y todas las corrutinas comparten
uno. Está resuelto desactivándolo desde el propio binario.

Si la 3.0 hubiera cambiado la implementación de corrutinas, esto podría dejar de
hacer falta. **No tengo ni idea de si lo ha hecho**, y tampoco es un problema:
está apañado en cuatro líneas y `make asan` sale limpio.

### 6.4 Mantenimiento a largo plazo — sí, pero a plazo

Un programa que se reparte a alumnos y se usa durante cursos vive años. Estar en
la línea mantenida es preferible a estar en la anterior, y llegará el momento en
que las distribuciones empaqueten la 3.0 y sea la 2.3.4 la que dé trabajo.

Es un argumento **real pero no urgente**, y sobre todo: si la migración es tan
barata como parece por §2 y §3, se puede tomar el día que convenga. No hay que
adelantarse a nada.

### 6.5 Y el coste que juega en contra

**Hoy, en Linux, SystemC se instala con `apt install libsystemc-dev`.** Pasar a la
3.0 significa compilarla, y por tanto:

* que quien clone el repositorio tenga que construir una dependencia antes de
  poder compilar nada;
* una versión distinta de la que usa la máquina de al lado;
* y un paso más en unas instrucciones que, para un proyecto docente, deberían
  caber en tres líneas.

Para un modelo que se distribuye, **la disponibilidad empaquetada es una
característica**, y hoy la 2.3.4 la tiene y la 3.0 no.

---

## 7. Cómo contestar lo que falta, en una tarde

El experimento que decide, y el orden importa:

1. **Construir SystemC 3.0 con MinGW-w64 en Windows.** Si sale bien, ya hay un
   motivo para migrar y se sigue; si sale mal o cuesta lo mismo que la 2.3.4, la
   pregunta se cierra y este informe termina en «no».
2. Lo mismo en macOS.
3. Con la biblioteca hecha, compilar el modelo tal cual:
   `make SYSTEMC_HOME=/ruta/a/systemc-3.0`. Por §2 y §3, espero que compile sin
   tocar una línea; **lo que no compile es la respuesta a la pregunta del
   enunciado**, y sale en la primera pasada del compilador.
4. `make test`. Y aquí está la red de seguridad que hace que esto sea una tarde y
   no una semana: la suite son **1899 comprobaciones** y el tiempo simulado sale
   **idéntico al picosegundo** entre ejecuciones, compiladores y plataformas
   (`2328209149213 ps` con g++ y con clang). Si un cambio de biblioteca alterase
   el orden de los eventos o la resolución del tiempo, **esa cifra se movería**, y
   se sabría en veintitrés segundos. Un invariante así es justo lo que se necesita
   para cambiar de versión sin miedo.
5. `make asan` y `make red`, que cubren memoria y sockets.
6. Y `./build/bench`, para ver si el planificador ha cambiado de coste.

Si los seis pasos salen bien, la migración consiste en cambiar `SYSTEMC_HOME` y
escribir en el README que hace falta compilar SystemC. Si el paso 1 sale mal, no
hay migración que discutir.

---

## 8. Recomendación

**No migrar ahora.** No porque la 3.0 sea peor, sino porque **ninguna de sus
ventajas conocidas ataca un problema que tengamos**, y la que podría hacerlo
—Windows y macOS— es una hipótesis que cuesta una tarde comprobar y que hay que
comprobar de todos modos, con 2.3.4 o con 3.0.

**Sí hacer barata la migración**, que es lo que se ha hecho al escribir esto:
`src/` ya no incluye ninguna cabecera interna de SystemC, el modelo compila en
C++17, C++20 y C++23, la superficie de API está inventariada y el estándar que
implementa la biblioteca se puede consultar en tiempo de compilación con
`IEEE_1666_SYSTEMC`. Con eso, el día que haya un motivo, la migración será
mirar si compila.

**Y el orden correcto**, que es lo único de este informe que cambiaría el plan de
trabajo: la pregunta «¿2.3.4 o 3.0?» **es una rama de la pregunta “¿cómo se
construye esto en Windows?”**, no una decisión aparte. Quien vaya a atacar I-23
que pruebe las dos versiones a la vez; construir una y luego la otra sería hacer
el trabajo dos veces.

---

## 9. Lo que no haría

**No migrar “para estar al día”.** Un modelo de 45 000 líneas con 1899
comprobaciones no cambia de biblioteca sin un motivo, y «es más nueva» no lo es.
La versión de SystemC no la ve ningún alumno.

**No escribir código condicionado por versión.** Hoy no hace falta ni un
`#if IEEE_1666_SYSTEMC >= ...`, y meterlo «por si acaso» sería pagar por
adelantado una compatibilidad que nadie ha pedido. Si algún día hay que soportar
las dos a la vez, ese será el momento de decidir si compensa.

**No dar por buena la migración porque compile.** El criterio es el tiempo
simulado: `2328209149213 ps`. Si al cambiar de biblioteca esa cifra se mueve, algo
del planificador es distinto, y hay que entender qué antes de seguir — aunque las
1899 comprobaciones pasen.
