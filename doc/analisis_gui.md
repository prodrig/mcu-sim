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

> **Nota de lectura.** Las partes 1 a 12 se escribieron sabiendo *qué* había que
> enseñar pero no *para qué*. Después se aclaró el objetivo real: **un programa
> didáctico para alumnos que no tienen placa, que desarrollan y depuran con
> STM32CubeIDE y prueban contra el simulador como lo harían contra el hardware.**
> Eso no invalida nada de lo anterior —las medidas son las mismas y la
> recomendación no cambia— pero **reordena las prioridades y añade requisitos que
> no estaban**. La **parte II**, a partir de §13, dice qué cambia y por qué. Si
> solo se va a leer una cosa, que sea §13 y §16.

> **DECISIÓN TOMADA, y NO es la que este documento recomienda.** Este análisis
> recomienda el **escenario 2** —un solo ejecutable Qt con la simulación en su
> propio hilo (§7, §18.2)—. Lo que se va a construir es el **escenario 3: dos
> procesos**, con la GUI en un repositorio aparte, **`mcu-sim-gui`**.
>
> El motivo por el que §18.2 descartaba los dos procesos —*la distribución*: «un
> alumno tiene que instalar una cosa y pulsar un icono»— se convierte en diseño
> en vez de en excusa: **la GUI escucha primero y lanza `mcu-sim` como proceso
> hijo**, así que no hay carrera de arranque, ni puerto ocupado, ni cortafuegos
> por medio. `mcu-sim` gana un solo argumento, `--gui host:puerto`, y **sin él
> se comporta exactamente como hoy**.
>
> Todo lo demás de este documento sigue valiendo, empezando por lo que más
> cuesta: la frontera de §5 —observables, mandos, instantánea y cola de
> órdenes— es **la misma en los tres escenarios**, y las piezas que no existen
> (§8) siguen siendo la parte grande de verdad.
>
> El plan por fases y la especificación del protocolo están en el otro
> repositorio: `mcu-sim-gui/doc/plan_dos_procesos.md` y
> `mcu-sim-gui/doc/protocolo.md`. En este está el punto **P-12** de `todo.md`,
> con lo que le toca crecer a `mcu-sim`.

---

## PARTE I — La GUI como problema técnico

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

Puesto en la escala del proyecto: `doc/coste_simulacion.md` midió que
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

> **Este orden quedó superado por §20**, que lo rehace bajo el objetivo
> didáctico: lo primero pasa a ser demostrar que CubeIDE habla con `sim`. Se
> conserva porque el razonamiento de por qué la frontera va antes que la ventana
> sigue valiendo.

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
`doc/coste_simulacion.md`.

---
---

# PARTE II — El objetivo real: un simulador didáctico

*Añadido después de aclararse para qué es todo esto: **un programa didáctico para
alumnos universitarios que empiezan con sistemas embebidos y no tienen material
hardware**. El alumno desarrolla y depura con STM32CubeIDE —o con otro entorno—
y prueba contra este programa **igual que lo haría contra una placa real**. Las
piezas externas que no existen (variadores, servos, etapas de potencia, finales
de carrera, motores) se añaden después.*

---

## 13. Qué cambia, y qué no

Lo primero, para no perderlo: **la parte I sigue siendo válida.** Las medidas de
§3 no dependen del uso, la frontera de §5 es la misma, y la recomendación de
escenario tampoco cambia. Lo que cambia es **el orden de las prioridades y el
rasero con el que se juzga cada decisión**.

| Conclusión de la parte I | Bajo el objetivo didáctico |
| :--- | :--- |
| La GUI es la interfaz con el usuario | **Se invierte.** La interfaz principal es el **servidor de GDB**: el alumno vive en su IDE, y la ventana es el «cacharro» que mira de reojo (§14) |
| El ritmo es una comodidad; hay tres políticas y se elige | **Se endurece.** El tiempo real deja de ser una opción y pasa a ser **el modo por omisión y un requisito de corrección**: un `blinky` que parpadea 55 veces más rápido no enseña nada (§15) |
| La fidelidad del modelo se da por buena | **Aparece un requisito nuevo, y es el más serio del informe.** «Exactitud funcional» hay que acotarla: hay una discrepancia de ~3× en el tiempo de cómputo que un alumno **va a ver** (§16) |
| El escenario 1 es viable, con reservas | **Deja de serlo.** Un argumento nuevo y concreto: si la ventana se bloquea, el IDE del alumno da el objetivo por muerto (§18) |
| Las piezas que faltan son lo caro | **Se mantiene**, pero dejan de ser lo urgente: el enunciado dice que se añaden después (§20) |
| La reproducibilidad se rompe con la interactividad (§9) | **Se refuerza**, y por un motivo nuevo: un alumno que informa de un fallo no sabe describirlo. Una sesión grabada es la única forma de que un profesor vea lo que él vio (§17) |
| Las piezas se verifican con la suite | **Se mantiene tal cual**, y ahora protege a terceros: un fallo del modelo lo paga un alumno que creerá que su código está mal |

Y aparece una restricción que en la parte I no existía: **el programa se
distribuye**. Deja de ser una herramienta para quien lo escribió y pasa a
instalarse en el portátil de cien personas que no van a compilar SystemC (§19.3).

---

## 14. La interfaz principal no es la GUI: es el servidor de GDB

> «que el usuario pueda desarrollar y depurar la aplicación embebida con
> STM32CubeIDE u otro entorno de desarrollo para MCUs […] de la misma manera que
> lo haría con el hardware real»

Con hardware real, ese «de la misma manera» quiere decir una cosa muy concreta:
CubeIDE compila, lanza un **servidor de GDB** —el de ST-LINK, u OpenOCD—, y su
depurador habla RSP por TCP contra él. **Sustituir la placa es sustituir ese
servidor.** Y eso ya está hecho: `sim` abre un puerto TCP y habla RSP
(`--gdb-dap`, `puerto_gdb=` por MCU).

Dicho de otra manera: **el producto ya existe a medias, y la mitad que existe no
es la que este informe empezó analizando.**

### 14.1 Lo que el motor RSP ya cubre

Verificado en `common/gdb_rsp.h`:

| Necesidad del IDE | Estado |
| :--- | :--- |
| Tramado `$cuerpo#suma`, `QStartNoAckMode` | **sí** (`qSupported`, línea 649) |
| Registros: `g`/`G`/`p`/`P` y `target.xml` por `qXfer:features:read` | **sí** (23 registros descritos) |
| Memoria: `m`/`M`/`X` | **sí**, y por el AHB-AP, así que **funcionan con el núcleo corriendo** — que es lo que necesitan las *Live Expressions* y la vista de registros de periféricos |
| Cargar el programa: `vFlashErase` / `vFlashWrite` / `vFlashDone` | **sí**, y además una escritura suelta a `0x0800_0000` se encamina al controlador de Flash, así que las dos rutas de `load` funcionan |
| Puntos de ruptura y watchpoints: `Z`/`z`, `swbreak+`, `hwbreak+` | **sí**, sobre el FPB y el DWT de verdad |
| Ejecución: `c`, `s`, `vCont;c;C;s;S` | **sí** |
| Reset: `R`, `r` y `monitor reset` | **sí**, con captura del vector de reset, que es lo que hace `monitor reset halt` |
| `monitor halt` / `monitor resume` | **sí** |
| `printf` por SWV/ITM | **sí**: 32 puertos de estímulo con empaquetado CoreSight, TPIU, y una pieza `SwoReceiver` que lo decodifica |

Esa última fila merece detenerse. **El `printf` por SWV funciona**, y para un
alumno sin placa eso es enorme: es cómo se saca texto de un Cortex-M sin gastar
un UART, y es una de las primeras cosas que se enseñan.

Y una consecuencia que ahorra trabajo: **el ELF no hay que leerlo.** GDB carga
los símbolos en el lado del IDE y por el socket solo manda escrituras de memoria.
La inspección de variables, los tipos, los `struct` de CMSIS: todo eso lo resuelve
el IDE con el ELF que él mismo generó. El simulador no se entera.

### 14.2 Lo que falta comprobar, y cómo

Aquí hay que ser honesto sobre el límite de este análisis: **no he podido probar
CubeIDE contra `sim`.** Lo que sigue no es una lista de defectos sino el guion de
la prueba que decide si el proyecto es viable, y **va antes que cualquier otra
cosa de este informe**.

1. Compilar un proyecto vacío de CubeIDE para la F407 y lanzar `sim` a mano con
   `--gdb-dap --port=3333`.
2. Configurar en CubeIDE una depuración *GDB Hardware Debugging* (o equivalente)
   apuntando a `localhost:3333`, y comprobar, en este orden:
   **conecta → `load` → para en `main` → punto de ruptura → paso a paso →
   inspección de variables → SFR de un periférico → reiniciar → SWV**.
3. Anotar cada paquete que el stub responda con `""` (no soportado). Ahí está la
   lista de trabajo real.

Candidatos conocidos a salir de esa prueba:

* **`qXfer:memory-map:read`**, que hoy no se anuncia. Sin él GDB no sabe dónde
  está la Flash y usa escrituras sueltas —que funcionan— en vez de `vFlash*`.
  Anunciarlo es más correcto y probablemente lo que el IDE espera.
* **Los `monitor` que el IDE mande por su cuenta.** Hoy se aceptan `reset`,
  `halt` y `resume`; cualquier otro devuelve vacío. Los servidores de ST y de
  OpenOCD aceptan un vocabulario más amplio y un IDE puede mandar alguno al
  arrancar.
* **`vRun` / `qAttached` / `!` (modo extendido)**, que algunos flujos usan.
* **El tiempo de espera del IDE al conectar.** Si el modelo va despacio —con
  `--ondas`, o mientras carga— el sondeo de 100 µs simulados puede tardar en
  reloj de pared más de lo que el IDE tolera. Es la misma raíz que §18.

Ninguno es difícil. Lo que importa es que **esta prueba se haga la primera**,
porque es la única que puede invalidar el producto entero, y hacerla cuesta una
tarde.

### 14.3 Consecuencia sobre el reparto de trabajo

La ventana en Qt deja de ser el objetivo y pasa a ser **la mitad barata**: da los
LEDs, los botones y los motores. La otra mitad —el depurador— ya está escrita,
verificada con 151 comprobaciones (fase F6) y solo hay que hacerla hablar con un
IDE de verdad. Empezar por la ventana sería empezar por la mitad que no decide
nada.

---

## 15. El ritmo deja de ser una opción

En la parte I, las tres políticas de ritmo (§5.3) eran una comodidad. Con alumnos
delante, **dos de las tres dejan de ser aceptables por omisión**:

* un `blinky` de 1 Hz que parpadea a 55 Hz no es un LED parpadeando: es un LED
  encendido;
* un servo que recorre 180° en dos centésimas no enseña qué es un servo;
* y un alumno que mide «cuánto tarda mi bucle» con un cronómetro obtiene un
  número sin relación con nada.

**El tiempo real tiene que ser el modo por omisión**, y las otras dos políticas
—libre y a demanda— quedan como herramientas explícitas, no como estados en los
que uno se pueda encontrar sin querer.

Las medidas de §3.1 dicen que eso es holgado: con el núcleo al 100 %, un
fotograma de 16,7 ms simulados cuesta 1,5 ms de reloj de pared, el 9 % del
presupuesto. **Hay margen de sobra para frenar; no lo hay para acelerar.**

### 15.1 Y hay un caso que hay que decidir a propósito

Cuando el alumno para en un punto de ruptura, ¿el tiempo simulado sigue
corriendo?

En hardware real, **sí**: el núcleo se para, pero el cristal sigue oscilando, los
temporizadores siguen contando salvo que DBGMCU los congele, y el motor sigue
girando por inercia. El modelo ya reproduce eso, incluida la congelación
selectiva por DBGMCU.

Para enseñar hay un argumento a favor de lo contrario —congelarlo todo, para que
el alumno inspeccione un estado coherente— y es un argumento razonable. Pero
**sería una divergencia deliberada con el hardware**, y este proyecto tiene por
norma no divergir en silencio. Mi recomendación: **que el tiempo siga corriendo,
como en la placa**, y que la GUI lo enseñe («parado en un punto de ruptura; el
tiempo simulado sigue»). Que un motor se descontrole mientras el alumno mira una
variable **es exactamente la lección** que el hardware da y que un simulador
demasiado amable le ahorraría.

Si se decide lo contrario, que sea un interruptor con nombre y documentado, no el
comportamiento por omisión.

---

## 16. «Exactitud funcional»: dónde acaba

Este es el apartado más importante de la parte II, y el que puede obligar a
reordenar el trabajo de todo el proyecto.

Un simulador didáctico tiene un modo de fallo peor que no funcionar: **enseñar
algo que no es verdad**. Un alumno que ve un comportamiento raro no tiene criterio
para saber si el raro es él o el simulador — no tiene placa con la que comparar,
que es justo el motivo por el que usa esto.

Así que hay que decir, y decírselo a él, **en qué se puede confiar**.

### 16.1 Lo que es fiel, y es casi todo

Las siete fases del proyecto modelan los periféricos **a nivel de registro**, con
1 899 comprobaciones que los ejercitan. Un alumno que escriba en `GPIOD->ODR`,
configure un temporizador, arranque un ADC, mande una trama por SPI o pida una
interrupción externa **ve lo que vería en la placa**. Y la frontera del
encapsulado está modelada en `float` —alta impedancia, pull internos,
open-drain, sobrecorriente—, así que hasta los errores de conexión se
manifiestan como en el hardware.

Eso, para lo que se enseña en un primer curso de embebidos, es prácticamente
todo.

### 16.2 Lo que NO es fiel, y el alumno lo va a ver

**El tiempo de cómputo del núcleo.** El modelo mide **≈ 4,7 ciclos por
instrucción frente a ≈ 1,5 del Cortex-M4 real**; en CoreMark, 0,71 CoreMark/MHz
contra los ≈ 3,4 del silicio (`doc/stm32f4xx/stm32f407vg_fase2.md`, §405-407). No hay
solapamiento entre búsqueda y ejecución, cada acceso al bus se factura entero y
no se modela la cola de prebúsqueda.

Traducido a lo que un alumno hace:

| Lo que el alumno escribe | Qué pasa en el simulador |
| :--- | :--- |
| `for (volatile int i = 0; i < 100000; i++);` como retardo | Tarda **unas 3 veces más** que en la placa. Si lo calibra aquí, en la placa irá 3 veces rápido |
| Un retardo con SysTick, `HAL_Delay()`, o un temporizador | **Exacto.** Lo gobierna el árbol de reloj, no el contador de instrucciones |
| Una interrupción cada N µs por TIM | **Exacto** en el disparo; la latencia de entrada es pesimista |
| Bit-banging de un protocolo con bucles | Sale más lento de lo que saldría; puede parecer que no cumple un requisito temporal que sí cumpliría |
| Medir «cuántas cuentas por segundo hace mi lazo» | **Pesimista en ~3×** |

**Esto no es un defecto que se pueda esconder: es una decisión de alcance del
proyecto** (punto **P-02** del TODO, «modo aproximado por ciclos del núcleo»,
declarado desde la fase F2). Lo que cambia con el objetivo didáctico es su
prioridad: en el informe de F2 se anotó que «nada de lo verificado depende de
ello», y era cierto. **Con alumnos delante, es la primera cosa que van a notar.**

Tres formas de tratarlo, y no son excluyentes:

1. **Decirlo, y convertirlo en lección.** «Los retardos por bucle no son
   portables ni medibles; usa un temporizador» es un buen consejo *aunque no
   hubiera simulador*. El programa puede decirlo donde se note —en la ventana, al
   arrancar— en vez de esconderlo.
2. **Calibrar el modelo**, aplicando un factor a la anotación de ciclos para que
   CoreMark/MHz se parezca al del silicio. Es barato y **es mentir mejor**: sería
   más parecido de media y seguiría siendo falso en cada instrucción concreta. Si
   se hace, que quede escrito que es una calibración y no un modelo.
3. **Arreglarlo de verdad** (P-02: solapamiento, cola de prebúsqueda, coste real
   de los saltos). Es trabajo serio y no lo pide el objetivo didáctico salvo que
   se quieran enseñar cosas de rendimiento.

Mi recomendación es la **1**, y anotar la 2 como posibilidad si algún ejercicio
del curso lo exige. Lo que no vale es callarlo.

### 16.3 El resto de la letra pequeña

`doc/todo.md` tiene 135 puntos, y casi todos son caminos que
ningún firmware corriente usa. Pero **la lista está escrita desde el punto de
vista de quien hace el modelo, no del alumno**. Hace falta una lectura nueva, y
corta, con otra pregunta: *¿un alumno de primer curso puede tropezar con esto?*

De un primer repaso, lo que sí puede aparecer:

* **el bus es *loosely-timed*** (P-01): no hay contención real entre maestros, así
  que un ejercicio de DMA compitiendo con el núcleo no enseñará la contención;
* **los errores dinámicos de FIFO del DMA no existen** (P-04): un desbordamiento
  por falta de ancho de banda no se puede provocar;
* **el `ack` del DMA no se reenvía a los periféricos** (P-07), con una
  consecuencia anotada en F4-TIM: una petición puede perderse donde el silicio la
  mantendría;
* y las **48 funciones «bits sin máquina»**, que se comportan como registros que
  guardan y no hacen nada. Ahí está el peor caso: el alumno configura algo, lee
  de vuelta lo que escribió, y no pasa nada. En la placa tampoco pasaría nada si
  se equivoca, así que **no distingue un modelo incompleto de un error suyo**.

De ahí una propuesta concreta que no cuesta casi nada: **que el modelo avise
cuando el firmware toca algo que no está modelado.** El proyecto ya usa
`SC_REPORT_WARNING` con etiquetas por periférico; un aviso «has habilitado X y
este modelo no lo implementa» convertido en una línea en la ventana del
simulador es, para un alumno, la diferencia entre una tarde perdida y aprender
algo. **Es más valioso que cualquier función que se pueda añadir en ese tiempo.**

---

## 17. Lo que un alumno hace y el hardware perdona

Un alumno de primer curso escribe firmware que se cuelga, desreferencia punteros
nulos, se pasa de los límites de un vector, deshabilita interrupciones y no las
vuelve a habilitar, y configura el reloj mal. Con una placa eso es normal: se
pulsa reset y a otra cosa.

El simulador tiene que **portarse igual de bien**, y eso son requisitos:

| Lo que hará el alumno | Qué tiene que pasar | Estado |
| :--- | :--- | :--- |
| Desreferenciar un puntero nulo | HardFault, y el depurador parando ahí | **Modelado** (escalado a HardFault, [IR §9]) |
| Bucle infinito con interrupciones cerradas | El simulador sigue vivo y GDB puede pararlo | Funciona; en el escenario 1 **no** (§18) |
| Escribir en una dirección reservada | Fallo de bus, como en la placa | Modelado |
| Pulsar «reset» en la ventana | NRST, arranque limpio | El pad existe; falta el mando |
| Cargar un firmware corrupto | Un mensaje claro, no un volcado de SystemC | **Falta**: hoy los errores están escritos para nosotros |
| Cerrar la ventana con GDB conectado | Salida limpia | Falta |

Esa penúltima fila es una categoría entera de trabajo: **los mensajes del
programa tienen que estar escritos para alguien que empieza.** Los del netlist ya
lo están —«el pad PF3 no sale al encapsulado LQFP100» se entiende— pero un
`SC_REPORT_ERROR` sin capturar saca un volcado que a un alumno no le dice nada y
le hace pensar que ha roto el simulador.

Y la contrapartida, que es la parte bonita: **un simulador puede explicar cosas
que el hardware no puede.** «Estás conduciendo PB6 desde dos sitios a la vez»,
«ese pin no está en este encapsulado», «has puesto ADCCLK por encima de su
máximo» son avisos que el modelo **ya sabe dar** —la validación eléctrica y los
avisos de periférico existen— y que en una placa se manifiestan como un
comportamiento raro y nada más. Es una ventaja didáctica real que ya está pagada.

---

## 18. Los tres escenarios, revisados

La recomendación no cambia —**escenario 2**— pero los motivos sí, y hay uno nuevo
que es decisivo.

### 18.1 El argumento que mata el escenario 1

En el escenario 1, quien da cuerda al modelo es el temporizador de la GUI. Y el
servidor de GDB **es un proceso de SystemC que sondea su socket en tiempo
simulado** (§4.1). Encadenando las dos cosas:

> **si la ventana se bloquea, el tiempo simulado deja de avanzar; si el tiempo
> simulado no avanza, el socket de GDB no se atiende; y si el socket no se
> atiende, el IDE del alumno da el objetivo por muerto.**

Y se bloquea por cosas normales: un diálogo para abrir un fichero, arrastrar la
ventana en algunos sistemas, un repintado lento en un portátil modesto. El alumno
verá «target not responding» y **culpará a su código**, que es el peor resultado
posible en una herramienta de enseñanza.

En la parte I el escenario 1 era «viable con reservas». Con el IDE al otro lado
del socket, **no es viable como producto**. Sigue valiendo como primer hito
interno (§20), donde no hay alumnos.

### 18.2 El escenario 3 gana peso, pero no gana

A favor, y son argumentos nuevos:

* el IDE del alumno **ya es un tercer proceso** hablando por TCP, así que la
  arquitectura de procesos separados no es una rareza en este producto: es lo
  normal;
* **la robustez importa más cuando el código de entrada es de cien alumnos**. Un
  fallo del modelo provocado por un firmware raro no se llevaría la ventana;
* y `sim` seguiría sin depender de Qt, lo que mantiene la suite compilable en
  cualquier sitio.

En contra, y pesa mucho en este contexto: **la distribución.** Un alumno tiene
que instalar *una* cosa y pulsar *un* icono. Dos ejecutables que se buscan por un
puerto es una fuente de incidencias de soporte —cortafuegos, puertos ocupados,
uno que arranca y el otro no— que consume el tiempo del profesor, que es el
recurso escaso.

**Veredicto: escenario 2**, un solo ejecutable que trae la ventana y el servidor
de GDB. Y con el matiz de siempre: **si la frontera de §5 está bien puesta, pasar
al 3 sigue siendo cambiar una cola por un socket**, así que la decisión es
reversible y no hay que agonizar con ella.

---

## 19. Lo que el producto tiene que ser, y no estaba en la parte I

### 19.1 La placa es del profesor, no del alumno

El alumno **no debe escribir XML**. Elige una placa de una lista y ya está.

El XML se convierte en la herramienta del **profesor**: describe la placa de la
práctica, la reparte con el enunciado, y el simulador la carga. Eso encaja con lo
que ya hay —`placas/` con cuatro ficheros, `--valida` para comprobarlos antes de
repartirlos— y le da un uso que justifica el trabajo del paso 3.

Sugerencia concreta: una placa **`discovery.xml`** que reproduzca una placa real
de las que se usan en clase, para que el alumno que sí tenga hardware vea lo
mismo en los dos sitios. Eso es lo que hace que el simulador sea un sustituto y
no otra cosa.

### 19.2 El alumno tiene que poder equivocarse de placa

Si la práctica dice PD12 y el alumno escribe PD13, en la placa no pasa nada y en
el simulador tampoco. Bien: es fiel. Pero el simulador **puede** decir «has
configurado PD13 como salida y en esta placa no hay nada conectado ahí», porque
conoce la placa entera. Otra ventaja didáctica que sale gratis del netlist.

### 19.3 Se distribuye, y eso es un requisito nuevo  *(el paso 0b, hecho a medias)*

Los alumnos usan Windows y macOS; el proyecto se compilaba solo en Linux. Este
era **el riesgo no cuantificado más grande del informe**, y una parte ya está
resuelta: *(añadido después de hacerlo)*

* la dependencia del sistema operativo estaba **acotada a dos ficheros** —
  `common/gdb_rsp.h`, el servidor que sí va en el producto, y
  `verif/gdb_client.h`, el cliente con el que la suite se prueba a sí misma —, y
  ahora está acotada a **uno solo**: `common/red.h`, que traduce entre Berkeley
  y Winsock y que es lo único de `src/` que incluye una cabecera del sistema;
* el **Makefile es único para las tres plataformas**, con detección automática y
  una variable (`PLATAFORMA`) para forzarla;
* **verificado**: Linux con g++ y con clang, 1899/1899 y el mismo tiempo simulado
  al picosegundo; `make red` —trece comprobaciones de la capa de sockets, sin
  SystemC de por medio— pasa en Linux y **cruza a Windows con MinGW-w64 sin un
  aviso**, produciendo un PE32+;
* **verificado después, y era la mitad que faltaba**: SystemC 2.3.4 **sí** se
  construye para MinGW, y `mcu-sim.exe` —22,5 MB, PE32+— **arranca y responde**
  desde la terminal MINGW64. Queda pasar las tres suites allí y comprobar que el
  tiempo simulado sale idéntico al picosegundo;
* **y una trampa que solo aparece al ejecutarlo FUERA de MSYS2**: el GCC de
  MSYS2 usa el modelo de hilos POSIX, así que `std::chrono` y SystemC arrastran
  una importación de `libwinpthread-1.dll`. Desde `cmd` gana la primera copia
  que haya en el `PATH` y, si es antigua, el programa no arranca: «no se
  encuentra el punto de entrada `clock_gettime64`». Arreglado con `-static` en
  el Makefile; contado en `doc/compilacion.md` §5.6. Importa aquí porque **es
  exactamente el fallo que tendrá el alumno que reciba el ejecutable**;
* **no verificado, y hay que decirlo**: macOS, entero.

Las trampas concretas que aparecieron al hacerlo, por si sirven de aviso para el
resto de la portabilidad:

* en Windows el descriptor de socket es un entero **sin signo**, así que el
  `if (s < 0)` de todo el código POSIX **nunca es cierto** y los errores se
  tragaban en silencio. No es un error de compilación: es un fallo mudo;
* MinGW usa por omisión el `printf` de msvcrt, que **no entiende `%llu`**, y el
  modelo lo usa veintiocho veces. Se arregla con `-D__USE_MINGW_ANSI_STDIO=1`;
* y una que no es de Windows sino de **macOS**: allí no existe `MSG_NOSIGNAL`, y
  escribir en un socket que el otro extremo cerró manda un `SIGPIPE` que **mata
  el proceso**. Un simulador que se muere porque el alumno cerró el IDE de golpe
  no es aceptable, así que la capa pone `SO_NOSIGPIPE` al crear cada socket.

### 19.4 Y hay una asimetría cómoda

El alumno **no necesita compilar nada del simulador**, y el simulador **no
necesita compilar nada del alumno**: el IDE le da el ELF a GDB y GDB manda bytes.
No hay cadena de herramientas cruzada, ni versiones de compilador que casar. Es
el reparto de trabajo más limpio que podía tocar, y es gratis por haber elegido
RSP.

---

## 20. Orden de trabajo, revisado

El de §12 seguía la lógica «primero la frontera, luego la GUI». Con el objetivo
didáctico el orden cambia, y bastante: **lo primero es demostrar que el producto
puede existir.**

| | Trabajo | Tamaño | Por qué va aquí |
| :--- | :--- | :--- | :--- |
| **0** | **La ida y vuelta con CubeIDE** (§14.2): conectar, cargar, parar en `main`, punto de ruptura, inspeccionar, SWV | pequeño | **Es lo único que puede invalidar el proyecto.** Cuesta una tarde y decide todo lo demás |
| **0b** | **Compilar en Windows** (§19.3) | medio | El otro que puede invalidarlo. **Hecho a medias**: la capa de red cruza a MinGW sin avisos y el Makefile ya es multiplataforma; falta construir SystemC allí y ejecutarlo |
| 1 | Ritmo en tiempo real, y la relación tiempo simulado / tiempo de pared visible | pequeño | Sin esto, nada de lo que se vea tiene sentido (§15) |
| 2 | `Observable` / `Mando`, instantánea y cola de órdenes (§5) | pequeño | La frontera. Igual que antes |
| 3 | Mensajes de error para alguien que empieza, y avisos de «esto no está modelado» (§16.3, §17) | medio | Es lo que más rendimiento didáctico da por línea escrita |
| 4 | GUI mínima, **escenario 2 directamente** | medio | El 1 ya no vale como producto (§18.1); el hito interno con un hilo cuesta lo mismo |
| 5 | Grabación y reproducción de sesiones (§9) | pequeño | Es cómo un alumno cuenta un fallo |
| 6 | Una placa de curso (`discovery.xml`) y un guion de práctica que la use | pequeño | Es el producto puesto delante de alguien |
| 7 | `PwmMeter`, `Servo`, `Encoder`, `StepperDriver`, `DcMotor` | grande | El enunciado dice explícitamente que van después |
| 8 | Decidir qué hacer con P-02 (§16.2) | — | Depende de qué se quiera enseñar |

Los pasos **0 y 0b van antes que todo**, y no por prudencia: es que si alguno
sale mal, el trabajo que hay debajo cambia de forma. Todo lo demás del informe
supone que esos dos salen bien.

---

## 21. Los riesgos didácticos, que son distintos de los técnicos

Para cerrar, y porque en una herramienta de enseñanza los fallos que importan no
son los que rompen el programa:

**Que el alumno aprenda algo falso.** Es el peor, y el ejemplo concreto está en
§16.2: quien calibre un retardo por bucle en el simulador tendrá código que en la
placa va tres veces rápido. Mitigación: decirlo, y empujar hacia los
temporizadores, que es lo correcto de todos modos.

**Que el alumno crea que su código está mal cuando lo que falta es el modelo.**
Mitigación: los avisos de «esto no está modelado» (§16.3). Es la mitigación más
rentable de la lista.

**Que el simulador sea más amable que el hardware.** Un simulador que no deja
colgarse, que congela el mundo en los puntos de ruptura y que no deja meter la
pata enseña a programar simuladores, no sistemas embebidos. La norma debería ser:
**divergir del hardware solo a propósito, y decirlo.** Es la misma norma que ha
seguido el modelo hasta ahora con [IR].

**Que el alumno se acostumbre a no medir.** Con hardware hay un osciloscopio y
hay dudas. Aquí el simulador puede dar la respuesta exacta siempre, y eso quita
una parte del oficio. Mitigación posible: que la ventana enseñe magnitudes como
las enseñaría un instrumento —un valor con su unidad, no la variable interna— que
es exactamente lo que §2.2 propone por razones técnicas y resulta que también es
lo pedagógicamente correcto.

**Y el que no es un riesgo sino una oportunidad:** el simulador ve cosas que
ninguna placa deja ver. Dos piezas conduciendo el mismo nodo, un pad que el
encapsulado no saca, un reloj por encima de su máximo, un nodo flotante. Todo eso
**ya está implementado y verificado**, y hoy solo lo mira quien ejecuta
`--valida`. Ponerlo delante de un alumno no cuesta código nuevo: cuesta decidir
dónde se enseña.
