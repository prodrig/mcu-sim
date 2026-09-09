# Una GUI en Qt sobre la simulación SystemC — análisis

*Qué haría falta para poner una representación gráfica de la placa delante del
modelo, qué se puede tocar desde ella, y qué le cuesta a la simulación. No es un
plan aprobado: es el análisis previo, con las decisiones que hay que tomar, las
medidas que las sostienen y el precio de cada camino.*

Caso de referencia, el del enunciado: una placa con un STM32F407, reloj externo,
sistema de reset, varios LEDs y botones, salidas PWM para servos y variadores,
etapas de potencia para motores paso a paso y de continua, y entradas de
encoders incrementales. En pantalla se quiere ver **el color de los LEDs, el
ángulo de los servos, el periodo y el ciclo de trabajo de las PWM y el giro de
los motores**; y se quiere poder **pulsar botones y finales de carrera** y que
el modelo se entere. No se quiere ver ni una tensión de pin ni un registro del
MCU.

---

## 1. Resumen

La conclusión importante no es cuál de los tres escenarios gana, sino que **la
decisión que de verdad importa no es el reparto en hilos: es dónde se pone la
frontera y qué la cruza.** Si la frontera está bien puesta, los tres escenarios
son tres formas de dar cuerda a la misma máquina y se puede pasar de uno a otro
en una tarde. Si está mal puesta —si el widget del LED llama a `led->on()`—, el
escenario 1 es el único que funciona y no hay camino desde ahí.

| | |
| :--- | :--- |
| **Lo que no es problema** | El rendimiento. El modelo va **entre 11 y 200 veces más rápido que el tiempo real** (§3.1), y trocear `sc_start()` no cuesta absolutamente nada: los mismos deltas y el mismo tiempo (§3.2) |
| **Lo que sí lo es** | Que va **demasiado** rápido. Sin freno, un servo cruzaría su recorrido en una centésima de segundo. Hay que decidir una política de ritmo (§3.4) |
| **La restricción dura** | El núcleo de SystemC **no es reentrante ni seguro entre hilos**. Un solo hilo del sistema operativo puede estar dentro, y desde fuera solo se puede llamar a `async_request_update()` (§4) |
| **Lo que de verdad cuesta** | No la GUI: **las piezas que no existen**. No hay servo, ni medidor de PWM, ni motor paso a paso, ni motor de continua, ni encoder en el catálogo de 21 componentes (§8) |
| **El riesgo que nadie ve venir** | La interactividad **destruye la reproducibilidad**, que es el activo principal de este proyecto. Tiene arreglo barato y además da un tipo de prueba nuevo (§9) |

**Recomendación:** escenario **2** (un proceso Qt, la simulación en su propio
hilo), con la frontera diseñada **como si fuera el 3**. El escenario 1 vale como
primer hito porque comparte todo el código de la frontera y solo cambia quién da
cuerda. Detalles y motivos en §7.

---

## 2. Lo primero es QUÉ se enseña, no cómo

El enunciado ya lleva dentro la decisión de arquitectura más importante, y
conviene sacarla a la luz antes de hablar de hilos.

> «no sería necesario disponer en la visualización gráfica de los detalles de
> todos los voltajes y corrientes de todos los pines»

Eso no es una simplificación de la vista: es **dónde va la frontera**. Y tiene
una consecuencia que no es evidente: si por la frontera solo pasan magnitudes
de alto nivel, entonces **quien las calcula está dentro del modelo**, no en la
GUI.

### 2.1 Por qué la medida tiene que ocurrir dentro

Tomemos el caso más claro, el del ciclo de trabajo de una PWM. La GUI refresca a
60 Hz. La PWM de un servo va a 50 Hz con pulsos de 1 a 2 ms. **Muestrear la
tensión del pin 60 veces por segundo no permite recuperar ni el periodo ni el
ciclo de trabajo**: se estaría submuestreando por tres órdenes de magnitud. Lo
mismo con un motor paso a paso, cuyos pasos pueden ir a kilohercios.

La medida tiene que hacerla una pieza del modelo, que es quien está despierta en
cada flanco:

* un `Servo` que decodifica la anchura del pulso y publica **un ángulo**;
* un `PwmMeter` que cronometra flancos y publica **periodo y ciclo**;
* un `StepperDriver` que cuenta pasos y publica **una posición de eje**.

Y la GUI lee escalares. Es la misma idea que ya gobierna el modelo eléctrico:
*«una vez la información entra en la parte digital de un periférico puede
simplificarse a 0/1»*. Aquí es lo mismo un escalón más arriba: **una vez la
información sale del encapsulado hacia la pantalla, puede simplificarse a un
número con nombre y unidad.**

### 2.2 El contrato: observables y mandos

La forma natural, y encaja con lo que `parts/part_base.h` ya hace —terminales
con nombre, inventario global, un único `set_enabled`—, es que cada pieza declare
qué se puede **ver** de ella y qué se le puede **hacer**:

```cpp
// Lo que una pieza deja VER. Un escalar con nombre y unidad; nada más.
struct Observable {
    const char* nombre;      // "encendido", "angulo", "ciclo", "rpm"
    const char* unidad;      // "", "grados", "%", "rpm"
    double      valor;
};
// Lo que a una pieza se le puede HACER desde fuera.
struct Mando {
    const char* nombre;      // "pulsar", "posicion", "final_de_carrera"
    enum Tipo { Boton, Interruptor, Continuo } tipo;
    double      min, max;    // solo para Continuo
};

class ExtPartBase {
    // ...
    virtual unsigned   n_observables() const { return 0; }
    virtual Observable observable(unsigned i) const;
    virtual unsigned   n_mandos() const { return 0; }
    virtual Mando      mando(unsigned i) const;
    virtual void       acciona(unsigned i, double valor) {}
};
```

Con eso, las piezas de hoy dirían:

| Pieza | Observables | Mandos |
| :--- | :--- | :--- |
| `Led` | `encendido` (0/1), `corriente` (mA) | — |
| `Button` | `pulsado` (0/1) | `pulsar` (botón) |
| `Crystal` | `frecuencia` (Hz) | — |
| `Servo` *(no existe)* | `angulo` (grados), `pulso` (µs) | `carga` (continuo) |
| `PwmMeter` *(no existe)* | `periodo` (µs), `ciclo` (%) | — |
| `Stepper` *(no existe)* | `pasos`, `angulo`, `rpm` | `final_de_carrera` |
| `Encoder` *(no existe)* | `cuenta` | `angulo` (continuo) |

**Y aquí está el beneficio que justifica el diseño:** la GUI puede **enumerar la
placa sin conocer ni un tipo de C++**. El netlist ya publica tipo, identificador
y parámetros de cada instancia, y ya se vuelca en XML (`--netlist`). Añadiendo
los observables y los mandos, la pantalla se construye sola a partir de una
descripción, exactamente como las piezas se construyen solas a partir de una
cadena en la factoría del paso 3. Es el mismo truco, aplicado dos capas más
arriba.

Eso también quiere decir que **la GUI no se recompila al añadir una pieza**, que
es la diferencia entre una herramienta y una demo.

### 2.3 Lo que NO debe cruzar la frontera

* Tensiones y corrientes de pin, salvo como observable declarado de una pieza
  que tenga sentido enseñar (la corriente de un LED, sí; la de PA7, no).
* Punteros a objetos del modelo. Ni uno. Ni en el escenario 1, donde
  «funcionaría».
* Registros del MCU. Para eso están los dos stubs de GDB, que ya existen y
  hablan un protocolo que los IDE entienden. Duplicar eso en la GUI sería
  reinventar mal lo que ya está hecho.

---

## 3. Las cifras que deciden esto

Medidas en este contenedor, SystemC 2.3.4, g++ 13, `-O2`, con una sonda que
trocea `sc_start()` y cronometra cada rodaja
(`doc/analisis_gui.md` §12 dice cómo reproducirlas).

### 3.1 La simulación va MUCHO más rápido que el tiempo real

| Firmware | Segundos de anfitrión por segundo simulado | Veces más rápido que el tiempo real |
| :--- | ---: | ---: |
| Sin firmware (núcleo aparcado en `wfe`) | 0,005 | **200×** |
| `blinky` (núcleo en `WFI` casi todo el rato) | 0,018 | **55×** |
| CoreMark (núcleo al 100 %) | 0,089 | **11×** |

Es lo contrario de lo que uno teme al poner una GUI delante de un simulador. **El
problema no va a ser que la simulación no dé abasto: va a ser frenarla.** Con el
núcleo al 100 %, un fotograma de 60 Hz —16,7 ms de tiempo simulado— cuesta
**1,5 ms de reloj de pared**, el 9 % del presupuesto del fotograma. Queda el 91 %
para pintar.

### 3.2 Trocear `sc_start()` no cuesta nada

`sc_start(t)` se puede llamar muchas veces seguidas: la simulación continúa donde
la dejó. La pregunta es qué se paga por ello. Nada:

| Firmware | Una sola llamada | Troceado a 1 ms | Deltas |
| :--- | ---: | ---: | :--- |
| `blinky`, 1 s simulado | 0,0180 s | 0,0192 s (1000 rodajas) | **29 837 en los dos casos** |
| CoreMark, 1 s simulado | 0,0891 s | 0,0896 s (1000 rodajas) | **81 968 en los dos casos** |

**El número de deltas es idéntico**, que es la comprobación de que trocear no
cambia la simulación en absoluto, y la diferencia de tiempo está dentro del ruido
de medida. El coste por llamada a `sc_start` es despreciable frente a lo que se
simula dentro.

Y las rodajas, en régimen permanente:

| Firmware | 1ª rodaja | mediana | p95 | p99 | máx |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Sin firmware | 4,1 ms | 0,000 | 0,000 | 0,000 | 0,000 ms |
| `blinky` | 5,5 ms | 0,011 | 0,018 | 0,035 | 0,150 ms |
| CoreMark | 5,9 ms | 0,000 | 0,840 | 0,897 | **1,019 ms** |

La rodaja **peor** en régimen permanente es **1 ms**, muy por debajo de los
16,7 ms de un fotograma. La primera cuesta 4–6 ms porque lleva el reset y la
carga del firmware; se paga una vez y conviene hacerla antes de mostrar la
ventana.

### 3.3 Lo que la GUI le cuesta a la simulación: nada medible

Un proceso de SystemC que despierta a 60 Hz de tiempo simulado y lee lo que la
pantalla necesitaría, sobre `blinky`, 5 segundos simulados:

| | Deltas | Tiempo de anfitrión |
| :--- | ---: | ---: |
| Sin muestreador | 141 837 | 0,0625 s |
| Con muestreador a 60 Hz | 142 136 | 0,0558 s |

**+299 deltas**, que son exactamente los 60 despertares por segundo simulado
durante 5 segundos. El 0,2 % de los deltas de un modelo casi ocioso, y una
diferencia de tiempo que es ruido —sale incluso más rápido—.

Puesto en la escala del proyecto: `doc/stm32f407vg_coste_simulacion.md` midió que
OTG y ETH juntos costaban **56 000 despertares por segundo simulado**. La vista
cuesta **60**. Tres órdenes de magnitud por debajo de algo que ya se consideró
aceptable. **La visualización no es un problema de rendimiento y no hay que
diseñarla como si lo fuera.**

### 3.4 La excepción, y es grande: las ondas de reloj

El modelo tiene un interruptor, `set_internal_waveforms(bool)`, que hace que los
relojes internos generen su onda cuadrada de verdad en vez de existir solo como
frecuencia. Está apagado por omisión y `--ondas` lo enciende. Con él encendido:

| Firmware | s de anfitrión / s simulado | Rodaja de 1 ms, mediana |
| :--- | ---: | ---: |
| `blinky` **con ondas** | **222** | 227 ms |
| CoreMark **con ondas** | **20,5** | 21 ms |

De 55 veces más rápido que el tiempo real a **222 veces más lento**: un factor de
doce mil. Una rodaja de 1 ms simulado tarda **un cuarto de segundo** de reloj de
pared.

Esto no invalida nada, pero sí decide un punto de diseño: **con las ondas
encendidas, ninguna arquitectura de un solo hilo sobrevive.** La GUI se
congelaría en trozos de 200 ms. Es el argumento más fuerte a favor del escenario
2 o el 3, y también el argumento para que **la pantalla enseñe siempre la
relación entre tiempo simulado y tiempo de pared**: sin ese número, un usuario
que enciende `--ondas` cree que el programa se ha colgado.

---

## 4. La restricción dura: SystemC no es seguro entre hilos

Todo lo que sigue depende de un hecho que no se puede negociar: **el núcleo de
SystemC no es reentrante ni seguro entre hilos.** Los `SC_THREAD` son
*corrutinas* —una pila por proceso y un cambio de puntero de pila hecho a mano,
todo dentro de un único hilo del sistema operativo—, y el planificador, la cola
de eventos y el registro de canales primitivos son estructuras sin cerrojo.

En la práctica:

* **Un solo hilo del sistema operativo puede estar dentro del modelo.** El que
  llama a `sc_start()` y todo lo que corre debajo.
* Llamar a `Button::press()` desde el hilo de la GUI **no es un fallo que salte**:
  toca un `AnalogNet`, que es un `sc_prim_channel`, y le pide una actualización
  metiéndolo en el registro del núcleo. La mitad de las veces funciona. La otra
  mitad corrompe la cola.
* La única puerta oficial es `sc_prim_channel::async_request_update()`, que
  **existe en la 2.3.4** (verificado en
  `/usr/include/sysc/communication/sc_prim_channel.h:63`) y está pensada
  exactamente para esto: la puede llamar cualquier hilo, y el núcleo recoge la
  petición en su siguiente ciclo de evaluación.
* Y su compañera, `async_attach_suspending()` (misma cabecera, línea 92), que
  evita el otro problema: que `sc_start()` vuelva porque no hay nada que hacer
  justo cuando un hilo externo iba a inyectar algo.

Ese par —`async_request_update` para meter, `async_attach_suspending` para que la
simulación no se dé por terminada— es todo el andamiaje que SystemC ofrece, y es
suficiente.

### 4.1 El precedente que ya está en el árbol

Conviene mirar cómo resolvió esto el proyecto la primera vez que tuvo que hablar
con el mundo exterior, porque ya lo hizo: **los dos servidores de GDB**.

Y la respuesta es que **no usan hilos del sistema operativo en absoluto**. El
servidor es un `SC_THREAD` que abre el socket en modo **no bloqueante**
(`common/gdb_rsp.h:354`) y lo atiende cada cierto **tiempo simulado**:

```cpp
void set_poll(sc_core::sc_time t) { poll_ = t; }   // gdb_rsp.h:67
sc_core::sc_time poll_{100, sc_core::SC_US};       // gdb_rsp.h:757
// ...
for (;;) {
    sc_core::wait(poll_, ev_);                     // gdb_rsp.h:136
    if (!activo_) continue;
    aceptar();  rx_poll();  procesar_rx();
}
```

Es la solución más simple que funciona, y tiene una propiedad que conviene
entender antes de copiarla: **la capacidad de respuesta frente al exterior está
atada a la velocidad a la que avanza el tiempo simulado.** Con el modelo yendo
55 veces más rápido que el tiempo real, sondear cada 100 µs simulados son unos
2 µs de reloj de pared: instantáneo. Con `--ondas` son 22 ms: perceptible pero
usable. Con el modelo detenido, infinito.

*(De paso: un comentario de `top/sim_main.cpp` dice que cada stub «vive en su
propio hilo de sistema operativo». Es falso, y lo escribí yo en el turno
anterior. Corregido junto con este análisis.)*

Para la GUI, el mismo patrón es válido y probablemente suficiente para los
**mandos** —son eventos raros, de a uno— pero no para el ritmo de pintura, que
tiene que seguir el reloj de pared y no el simulado.

---

## 5. La forma de la frontera, que es común a los tres escenarios

Antes de comparar, esto es lo que hay que construir en los tres casos. Es la
parte que se reaprovecha entera y por eso vale la pena hacerla primero.

### 5.1 Del modelo a la pantalla: una INSTANTÁNEA, no un puntero

La simulación publica, cada cierto tiempo simulado, un bloque plano de valores:

```cpp
struct Muestra { uint16_t id; float valor; };
struct Instantanea {
    uint64_t t_sim_ns;         // el instante simulado al que corresponde
    uint32_t n;
    Muestra  v[N_MAX];         // POD: copiable, serializable, sin punteros
};
```

Plano y POD a propósito: eso es lo que hace que el escenario 3 sea una
posibilidad y no una reescritura. Una instantánea se puede pasar por una señal de
Qt, copiar a un búfer doble o escribir en un socket sin cambiar ni una línea de
quien la produce.

Quién la produce: un `SC_THREAD` que despierta a la frecuencia de refresco —60 Hz
de tiempo simulado, medidos en §3.3 como gratis— recorre el inventario de piezas,
lee sus observables y publica.

### 5.2 De la pantalla al modelo: una COLA DE ÓRDENES

```cpp
struct Orden {
    uint64_t t_sim_ns;         // cuándo se aplicó (lo rellena el modelo)
    uint16_t pieza, mando;
    float    valor;
};
```

La GUI empuja órdenes; un proceso del modelo las vacía y llama a
`ExtPartBase::acciona()`. Los mandos son **raros y discretos** —un clic, un final
de carrera— así que la cola es corta y el proceso que la vacía puede despertar
a 60 Hz simulados igual que el muestreador, o dormir hasta que
`async_request_update()` lo despierte.

**La asimetría es el punto:** del modelo a la pantalla van muchos datos a ritmo
fijo; de la pantalla al modelo van pocos datos a ritmo irregular. Son dos
mecanismos distintos porque son dos problemas distintos, y mezclarlos —un
«acceso compartido a los objetos»— es lo que hace que un diseño así no se pueda
partir después.

### 5.3 El ritmo

Tres políticas, y la GUI debería dejar elegir:

| Política | Qué hace | Para qué |
| :--- | :--- | :--- |
| **Tiempo real** | La GUI avanza 16,7 ms simulados por fotograma y espera si sobra tiempo | Ver servos y motores moverse a velocidad creíble. Es la de por defecto |
| **Libre** | Se avanza todo lo que se pueda | Llegar rápido a un punto lejano. A 11–200× el tiempo real, lo que se ve es un borrón |
| **A demanda** | N milisegundos por clic, o hasta el siguiente evento | Depuración. Encaja con parar por GDB |

Y en los tres casos, **enseñar la relación entre tiempo simulado y tiempo de
pared**. Es un `QLabel` y evita la pregunta «¿se ha colgado?» en el único caso
donde la respuesta es no (§3.4).

---

## 6. Los tres escenarios

### 6.1 Escenario 1 — un programa Qt, un solo hilo

`sc_start()` troceado desde un `QTimer` en el hilo de la GUI.

```cpp
void MainWindow::tick() {                 // QTimer a 60 Hz
    sc_start(16.7, SC_MS);                // avanza el modelo
    muestrear_y_repintar();               // lee observables y pinta
}
```

**A favor.** Es de verdad simple: no hay cerrojos, no hay condiciones de carrera,
no hay reglas de afinidad de `QObject` que recordar, y depurar es depurar un
programa normal. Y las medidas dicen que **funciona**: la rodaja peor en régimen
permanente es 1 ms (§3.2) contra un presupuesto de 16,7 ms.

**En contra**, y no son teóricos:

* **Cualquier cosa que bloquee, congela las dos mitades.** Un diálogo modal para
  abrir un fichero detiene la simulación. Un `QMessageBox` la detiene. Eso puede
  ser aceptable o puede ser un fallo, según lo que esté simulando.
* **La rodaja peor no es la que se mide, es la que traiga el firmware de
  mañana.** 1 ms con CoreMark; con `--ondas`, 227 ms (§3.4). El escenario 1
  apuesta a que ningún caso futuro se pase, y esa apuesta no se puede cerrar.
* **El modelo no puede correr mientras la GUI está ocupada.** Al arrastrar una
  ventana, en algunos sistemas el bucle de eventos entra en un modo propio y los
  temporizadores dejan de disparar: la simulación se para al mover la ventana.
* **No se puede aprovechar más de un núcleo**, y hoy los hay de sobra.

**Veredicto.** Viable, y buen **primer hito**: si la frontera de §5 está hecha,
esto son unas cien líneas y sirve para validar el modelo de vista con piezas
reales antes de meter un hilo. Como destino final, solo si el uso previsto es
siempre `--ondas` apagado y firmware ligero.

### 6.2 Escenario 2 — un programa Qt, la simulación en su propio hilo

Un `QThread` cuyo único trabajo es llamar a `sc_start()`; el hilo de la GUI no
entra jamás en el modelo.

```cpp
class HiloSim : public QThread {
    void run() override {                     // el ÚNICO hilo que toca SystemC
        for (;;) {
            vacia_ordenes();                  // lo que la GUI ha pedido
            sc_start(rodaja_, SC_MS);
            emit nueva_instantanea(snap_);    // Qt::QueuedConnection: copia
            gobernar_ritmo();
        }
    }
};
```

**A favor.**

* **La GUI nunca se congela**, pase lo que pase dentro del modelo. Con `--ondas`
  la pantalla sigue viva y simplemente se refresca con menos tiempo simulado por
  fotograma, que es exactamente lo que hay que enseñar.
* La simulación aprovecha su propio núcleo.
* El coste sobre el escenario 1 es **acotado y pequeño**: un hilo, una cola y una
  instantánea. Si la frontera de §5 ya está hecha, el trabajo adicional es quién
  llama a `sc_start`.
* Qt ya trae lo necesario: una `connect` con `Qt::QueuedConnection` marshala la
  instantánea al hilo de la GUI sin que haya que escribir un cerrojo.

**En contra.**

* **La trampa clásica, y hay que decirla con todas las letras: tocar el modelo
  desde el hilo de la GUI parece que funciona.** `led->on()` desde un `paintEvent`
  devuelve un valor correcto casi siempre. El fallo aparece semanas después como
  una corrupción imposible de reproducir. La disciplina —solo instantáneas, solo
  órdenes— hay que sostenerla, y una revisión de código no la ve fácilmente.
* El objeto que envuelve la simulación **no puede ser un `QObject` que viva en el
  hilo de la GUI**, y las reglas de afinidad de Qt son una fuente conocida de
  errores sutiles.
* Un fallo de segmentación en el modelo se lleva la ventana por delante.
* Bajo sanitizers hay una complicación conocida: ASan y las corrutinas de SystemC
  ya obligan a `detect_stack_use_after_return=0` (véase
  `src/common/asan_opciones.h`); meter un hilo más no lo empeora, pero conviene
  saber que ese terreno ya está pisado.

**Veredicto.** **Es la recomendación.** El coste sobre el 1 es pequeño y la clase
de problemas que elimina —«se congela y no sé por qué»— es justo la que hace
inutilizable una herramienta interactiva.

### 6.3 Escenario 3 — dos procesos

La GUI por un lado, `sim` por otro, hablando por un socket.

**A favor.**

* **Aislamiento de verdad.** Un fallo del modelo no se lleva la ventana y al
  revés. Se puede correr `sim` bajo ASan o valgrind con la GUI encima, sin que
  los sanitizers vean el código de Qt.
* **El modelo sigue siendo lo que es.** `sim` no aprende nada de Qt: ni una
  cabecera, ni una dependencia, ni un `moc`. Eso importa más de lo que parece en
  un proyecto cuyo ejecutable de verificación tiene que compilar en cualquier
  sitio.
* **La GUI puede engancharse a una simulación ya en marcha**, y desengancharse,
  y volver. También a una que corre en otra máquina.
* **El protocolo se convierte en un activo**, igual que pasó con el netlist en
  XML: cualquiera puede escribir otro cliente —un guion, una prueba automática,
  un panel web— sin tocar el simulador.
* **Ya hay precedente y ya hay puerto.** El proyecto sirve dos servidores de GDB
  por TCP; este sería un tercer socket. Y el saludo inicial ya está escrito:
  `--netlist` vuelca la placa entera en XML, así que **la GUI puede pedir la
  descripción de la placa al conectarse y construir sus widgets a partir de
  ella**, sin conocer ni un tipo.

**En contra.**

* Hay que **definir y versionar un protocolo**, y eso incluye qué pasa cuando el
  cliente es más nuevo que el servidor. Es trabajo real.
* Dos ejecutables, dos ciclos de compilación, dos sitios donde mirar cuando algo
  no cuadra.
* **Latencia** de ida y vuelta para los mandos. Con todo en local es de decenas
  de microsegundos y da igual, pero deja de dar igual si algún día hay una red
  por medio.
* La tentación de meter lógica en el cliente, que es como estos diseños se
  pudren.

Sobre el volumen de datos, para quitarle hierro: la placa del enunciado tendría
del orden de **cuarenta observables**. A 60 Hz, con 6 bytes por muestra, son
**14 kB/s**. No es un problema de ancho de banda por ningún lado.

**Veredicto.** Es el **destino correcto** si la GUI llega a ser una entrega
aparte, o si se quiere depurar el modelo con herramientas que no soporten Qt. Y
lo importante: **si la frontera de §5 está bien hecha, pasar del 2 al 3 es
sustituir una cola en memoria por un socket**, no una reescritura.

---

## 7. Comparación y recomendación

| | 1 — un hilo | 2 — hilo de simulación | 3 — dos procesos |
| :--- | :--- | :--- | :--- |
| Líneas de la frontera (§5) | las mismas | las mismas | las mismas + protocolo |
| Trabajo adicional | ~100 líneas | ~250 líneas | ~600 líneas |
| La GUI se congela si el modelo se atasca | **sí** | no | no |
| Sobrevive a `--ondas` | no | sí | sí |
| Un fallo del modelo mata la ventana | sí | sí | **no** |
| Aprovecha más de un núcleo | no | sí | sí |
| Riesgo de corrupción por concurrencia | **ninguno** | real, y silencioso | ninguno |
| Se puede enganchar a una simulación en marcha | no | no | **sí** |
| `sim` depende de Qt | sí | sí | **no** |
| Coste sobre la simulación (§3.3) | despreciable | despreciable | despreciable |

**Recomendación: el 2, con la frontera diseñada como si fuera el 3.**

El argumento no es de elegancia. Es que **las tres columnas comparten la parte
cara** —los observables, los mandos, las piezas nuevas de §8— y se diferencian en
la parte barata. Elegir mal el reparto en hilos cuesta un día; elegir mal la
frontera cuesta el proyecto, porque un widget que llama a `led->on()` no se
migra: se tira.

Y un aviso concreto: **incluso si se empieza por el escenario 1, hay que pasar
por la instantánea y por la cola.** En un solo hilo es tentador leer la pieza
directamente, funciona, y deja el código en un sitio del que no se sale.

---

## 8. Lo que de verdad cuesta: las piezas que no existen

Esta es la parte del análisis que cambia las prioridades. El catálogo tiene hoy
**21 tipos** (`parts/netlist_parts.h`):

```
Led Button Crystal Rpull Driver SignalLink ExtClock I2cWire I2cEeprom
I2cExtMaster SwoReceiver SdCard CameraSensor ExtSram ExtNand UsbHostRig
UsbDeviceRig EthPhy CanWire CanTransceiver CanNode
```

De lo que el enunciado quiere ver en pantalla, **solo los LEDs y los botones
existen**. No hay servo, ni medidor de PWM, ni etapa de potencia, ni motor paso a
paso, ni motor de continua, ni encoder incremental.

| Pieza | Qué tendría que hacer | Tamaño |
| :--- | :--- | :--- |
| `PwmMeter` | Cronometrar flancos de un pin y publicar periodo y ciclo. Sin estado eléctrico propio: es un observador pasivo, como el `origen` de un `SignalLink` | pequeño |
| `Servo` | Decodificar la anchura del pulso a un ángulo, con velocidad máxima de giro para que el movimiento sea creíble | pequeño |
| `Encoder` | Generar dos canales en cuadratura a partir de una posición que se mueve desde fuera. Es la primera pieza cuyo **mando** manda de verdad | medio |
| `StepperDriver` | Cuatro o dos pines de paso y dirección, contar pasos, publicar ángulo. Con micropaso, si hace falta | medio |
| `DcMotor` + puente en H | Tensión media a partir del ciclo de trabajo, y de ahí una velocidad. Aquí empieza a haber física de verdad, y hay que decidir cuánta se quiere | **el grande** |

El `DcMotor` es el que merece cuidado y una decisión explícita: un motor de
continua con su inercia, su constante de tiempo y su carga es un modelo
**continuo** metido en un simulador de eventos discretos. Se puede hacer —se
integra a pasos fijos, digamos cada milisegundo, que son 1 000 despertares por
segundo simulado, dos órdenes de magnitud por debajo del OTG— pero hay que
elegir el paso y decir en qué se cree y en qué no.

**Consecuencia para el plan:** la GUI no es lo caro. Y hay una manera cómoda de
ordenarlo: **las piezas nuevas se pueden escribir y verificar sin GUI ninguna**,
con el banco de pruebas de siempre, exactamente como se escribieron las 21 que
hay. Un `PwmMeter` con su grupo de comprobaciones es útil aunque nunca se pinte.

---

## 9. La consecuencia que no se ve venir: la reproducibilidad

Este proyecto tiene una propiedad que vale más que casi cualquier funcionalidad:
**la simulación es determinista.** La suite lleva 1 899 comprobaciones y el
tiempo simulado sale idéntico al picosegundo de una ejecución a otra. Ese es el
motivo por el que un fallo se puede perseguir.

**La interactividad rompe eso.** Un clic ocurre cuando ocurre, y el instante
simulado en que aterriza depende del reloj de pared, de la carga de la máquina y
de si el usuario estornudó. Dos sesiones «iguales» dejan de serlo. Y el fallo que
se acaba de ver haciendo clic en el botón **no se puede volver a ver**, ni
contar, ni meter en la suite.

Y esto pasa en **los tres escenarios**: no es un problema de concurrencia, es un
problema de tener una persona dentro del lazo.

**El arreglo es barato y da algo a cambio.** La cola de órdenes de §5.2 ya lleva
el instante simulado en que cada orden se aplicó. Guardarla es un fichero:

```xml
<sesion placa="banco.xml" firmware="motores.bin">
  <orden t_ns="1250000000" pieza="btn_marcha"  mando="pulsar" valor="1"/>
  <orden t_ns="1310000000" pieza="btn_marcha"  mando="pulsar" valor="0"/>
  <orden t_ns="4700000000" pieza="fin_carrera" mando="pulsar" valor="1"/>
</sesion>
```

Reproducirlo **no necesita la GUI**: es `sim` leyendo un fichero y aplicando
órdenes en instantes simulados exactos. Con eso se gana:

* un fallo encontrado a mano se convierte en **un caso reproducible**;
* y de ahí, en **una prueba de la suite**, que es la moneda de este proyecto;
* la GUI deja de ser imprescindible para reproducir lo que la GUI encontró, que
  es justo lo que hace falta cuando alguien informa de un problema.

Diría que **esto no es opcional**. Una GUI interactiva sin grabación convierte un
simulador determinista en uno que a veces falla, y ese cambio es difícil de
deshacer una vez la gente se acostumbra.

---

## 10. Detalles que muerden

Cosas concretas que conviene tener escritas antes de empezar, no después.

**`sc_stop()` es definitivo.** Detiene la simulación para siempre; no hay
`sc_resume`. La pausa de la GUI se implementa **dejando de llamar a `sc_start`**,
no parando SystemC.

**La elaboración es estática.** No se puede añadir una pieza con la simulación en
marcha. Una GUI que ofrezca «añadir un LED» tiene que reconstruir el modelo
entero, que es un `sc_main` nuevo —o, en el escenario 3, un proceso nuevo, que es
la única forma limpia de hacerlo—. En la práctica: las piezas que la placa pueda
llevar se construyen **desoldadas** (`conectada="no"`), que es la misma solución
que ya se tomó para las piezas que el netlist declara y el montaje no lleva.

**El tiempo simulado no avanza solo.** Si el modelo se queda sin eventos
pendientes, `sc_start(t)` vuelve enseguida y el reloj salta hasta `t`. Con la GUI
eso está bien; con `async_attach_suspending()` se evita que la simulación se dé
por acabada cuando lo único que falta por llegar es un clic.

**Los dos stubs de GDB conviven con esto sin tocarlos.** Son procesos de SystemC
que sondean su socket en tiempo simulado (§4.1). Con la GUI dando cuerda en
rodajas, siguen funcionando igual; con la simulación en pausa, GDB se queda
esperando, que es exactamente lo que un depurador espera de un objetivo parado.

**Los identificadores tienen que ser estables.** La GUI dibuja `LD4` en un sitio
de la pantalla; si el índice de las piezas cambia al reordenar el XML, el LED
salta. El identificador de instancia del netlist ya es estable y único —lo valida
`Netlist::valida()`— así que la frontera debe indexar por **ese** nombre, no por
posición. Es el mismo criterio que hizo que los terminales sean nominales y no
posicionales.

**El SVG del paso 4 y esta GUI son el mismo problema a medias.** La ruta de
adopción de QtSysC tiene pendiente un generador de SVG desde el netlist (I-13).
Una GUI que dibuja la placa necesita exactamente lo mismo: geometría por pieza.
Sería un error hacer dos veces la decisión de dónde se dibuja cada cosa; si el
SVG llega primero, la GUI debería consumirlo.

---

## 11. Lo que no haría

**No pintar el modelo eléctrico.** Un panel con las tensiones de los 144 pines es
fácil de hacer, impresiona en una demo y no se usa nunca. Lo que hace falta
mirar cuando algo va mal no es la tensión de PA7: es qué pieza está conduciendo
sobre qué nodo, y **eso ya lo dice `--valida` antes de simular**, con nombres y
sin mirar una pantalla.

**No meter Qt en `src/`.** El modelo compila hoy con `g++` y `-lsystemc` y nada
más. Esa propiedad vale mucho: es lo que hace que la suite corra en cualquier
sitio y bajo cualquier sanitizer. La GUI debe depender del modelo, nunca al
revés, y en el escenario 3 eso es automático.

**No hacer que la GUI calcule nada del modelo.** Ni el ciclo de trabajo, ni el
ángulo, ni las revoluciones. Si un número hace falta en pantalla, lo publica una
pieza (§2.1). Un número calculado en el cliente es un número que no se puede
verificar con el banco de pruebas, y este proyecto verifica todo.

**No empezar por la GUI.** Las piezas de §8 son la mayor parte del trabajo, se
escriben y se verifican sin ventana ninguna, y son útiles por sí solas. Empezar
por la ventana lleva a una ventana bonita enseñando dos LEDs.

---

## 12. Orden de trabajo, y cómo reproducir las medidas

| | Trabajo | Tamaño | Qué desbloquea |
| :--- | :--- | :--- | :--- |
| 1 | `Observable` / `Mando` en `ExtPartBase`, y declararlos en `Led` y `Button` | pequeño | La frontera existe y se puede probar con lo que ya hay |
| 2 | Instantánea + cola de órdenes, con el muestreador como proceso de SystemC | pequeño | El escenario 1 es posible; el 2 y el 3 comparten esto entero |
| 3 | `PwmMeter` y `Servo`, con su grupo en la suite | medio | Lo primero que el enunciado pide y que no existe |
| 4 | GUI mínima en Qt, escenario 1: LEDs, botones y un servo | medio | Se ve si el modelo de vista es el correcto **antes** de meter hilos |
| 5 | Grabación y reproducción de sesiones (§9) | pequeño | La reproducibilidad vuelve, y con ella las pruebas |
| 6 | Pasar al escenario 2 | pequeño | La ventana deja de congelarse |
| 7 | `Encoder`, `StepperDriver`, `DcMotor` | grande | El resto del enunciado |
| 8 | El protocolo y el escenario 3, si hace falta | medio | GUI aparte, enganche a simulación en marcha, `sim` sin Qt |

El paso 5 va **antes** que las piezas grandes a propósito: en cuanto haya un
motor que se pueda romper haciendo clic, querremos poder contar cómo se rompió.

### Reproducir las medidas de §3

Las cifras salen de una sonda que trocea `sc_start()` y cronometra cada rodaja,
sobre el mismo modelo que la suite. No está en el repositorio porque es una
herramienta de un rato; el patrón es:

```cpp
Probe p("probe");                       // MCU + cristal, arranque y firmware
for (unsigned i = 0; i < n; ++i) {      // en vez de un solo sc_start()
    const auto a = std::chrono::steady_clock::now();
    sc_start(ms_rodaja, SC_MS);
    rodajas.push_back(ahora() - a);
}
```

Con `set_internal_waveforms(false)` para §3.1–3.3 y `true` para §3.4, y
descartando las 20 primeras rodajas, que llevan el reset, la carga del firmware y
los fallos de página del anfitrión. Los números de referencia del modelo sin GUI
están medidos con `./build/bench` y explicados en
`doc/stm32f407vg_coste_simulacion.md`.
