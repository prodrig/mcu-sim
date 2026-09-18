# ¿Cuánto cuesta simular lo que no hace nada?

Medida del coste de simulación del USB OTG y del Ethernet cuando el firmware no
los toca. La pregunta se planteó como una elección entre dos escenarios:

* **A** — el MCU incluye el modelado completo de OTG y ETH, pero el programa no
  los usa.
* **B** — el MCU no los modela: solo bloques *dummy* con los registros y sin
  funcionalidad.

La respuesta corta es que **la elección es falsa**: casi todo el coste de A no
viene de modelar el periférico, sino de **dos bucles de sondeo** que se
despiertan aunque nadie haya encendido nada. Corregidos —escenario **C**—, el
modelo completo cuesta **exactamente lo mismo que el dummy**, y sigue estando
entero.

Y midiendo eso apareció un sondeo mucho mayor en el propio núcleo, que la
sección 6 cuenta: **un MCU dormido pasó de 0,575 s de anfitrión por cada dos
segundos simulados a 0,000 s**, y el parpadeo de referencia —que retrasa con
`WFI`— es ahora **catorce veces y media más rápido haciendo exactamente el mismo
trabajo**.

---

## 1. Cómo se ha medido

`src/top/bench_main.cpp` (`make -f Makefile.mcu-sim bench`) levanta el MCU, carga
un firmware, lo deja correr una ventana de tiempo **simulado** fija y mide el
tiempo de CPU del **anfitrión**, el número de deltas de SystemC y las
transacciones que han pasado por la matriz:

```
./build/bench [imagen.bin] [ms_simulados] [reset]
```

Tres variantes del árbol, compiladas con las mismas opciones (`-O2`), y tres
cargas:

| | Qué es |
| :--- | :--- |
| **A** | el modelo tal como estaba: OTG y ETH completos |
| **B** | `periph/otg.h` y `periph/eth_mac.h` sustituidos por *dummies*: mismos puertos, mismo mapa, banco de registros, **ningún proceso de SystemC** |
| **C** | el modelo completo de A, con los dos bucles de sondeo de los periféricos convertidos en espera por evento |
| **D** | C más el bucle de sueño del núcleo, también por evento — **es el estado actual del árbol** |

| Carga | Qué hace el núcleo |
| :--- | :--- |
| `parked` | aparcado en `wfe; b .-2` — el MCU encendido sin hacer nada |
| `blinky` | el parpadeo de referencia con CMSIS |
| `coremark` | CoreMark: carga acotada por CPU y por bus |

La onda cuadrada de los relojes internos se apaga (`set_internal_waveforms(false)`),
que es lo que hace el modo de ejecución normal del modelo; con ella encendida,
los flancos de HCLK ahogarían cualquier otra medida.

> **Los tiempos absolutos solo valen dentro de una misma tanda.** La máquina es
> compartida y su carga varía: el mismo binario con la misma carga sale entre un
> 10 % y un 20 % distinto según el momento. Todas las comparaciones de este
> documento se han medido **en la misma tanda**, alternando variantes, y con la
> mediana de 15 repeticiones. Entre tablas de secciones distintas, compárense
> las proporciones, no los segundos. La métrica que **no** depende de la carga
> de la máquina es el número de **deltas**, y por eso aparece siempre al lado.

A la variante C se le añadió después una cuarta, **D**, con el bucle de sueño
del núcleo convertido también en espera por evento (sección 6).

---

## 2. El coste puro, aislado

Con **NRST mantenido abajo** el resto del modelo no hace nada, así que lo único
que queda en la simulación es la actividad de fondo de los periféricos. Dos
segundos de tiempo simulado:

| Variante | Deltas | Tiempo de anfitrión |
| :--- | ---: | ---: |
| **A** — modelo completo | **112 033** | **0,0154 s** |
| **B** — dummies | 29 | ~0,000 s |
| **C** — modelo sin sondeo | **29** | **~0,000 s** |

**C reproduce exactamente el número de B: 29 deltas.** No es que se le parezca:
es el mismo.

### De dónde salen esos 112 000 despertares

Son tres bucles, y uno de ellos se lleva casi todo:

| Proceso | Periodo | Despertares por segundo simulado | Peso |
| :--- | ---: | ---: | ---: |
| `EthBase::tx_proc` | 20 µs | **50 000** | **89 %** |
| `OtgBase::motor_proc` (HS) | 125 µs | 8 000 | 14 % |
| `OtgBase::motor_proc` (FS) | 1 ms | 1 000 | 2 % |

El del Ethernet miraba cada 20 µs si el DMA de transmisión tenía algo que
enviar. Con `DMAOMR.ST` a cero —es decir, siempre que nadie haya arrancado el
MAC— la respuesta era que no, cincuenta mil veces por segundo simulado.

El del USB despertaba cada trama para contar tramas que nadie pide. Y en el
OTG_HS el periodo es de microtrama, 125 µs, así que cuesta ocho veces más que
el FS **por estar apagado**.

Cada despertar sale por unos **0,14 µs** de anfitrión en aislamiento, y por
unos **0,35 µs** cuando el simulador está ocupado con otras cosas (la cola de
procesos es mayor y la caché está más fría).

---

## 3. El coste con el MCU funcionando

Ventana de 200 ms simulados, **15 repeticiones**, mediana y desviación:

| Carga | A | B | C | Ahorro A→B | Ahorro A→C |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `parked` | 0,0689 s (σ 0,0069) | 0,0638 s (σ 0,0014) | **0,0609 s** (σ 0,0013) | 7,4 % | **11,6 %** |
| `blinky` | 0,0731 s (σ 0,0046) | 0,0688 s (σ 0,0053) | **0,0653 s** (σ 0,0007) | 5,9 % | **10,7 %** |
| `coremark` | 0,1614 s (σ 0,0079) | 0,1575 s (σ 0,0040) | **0,1537 s** (σ 0,0055) | 2,4 % | **4,8 %** |

Y la suite de verificación completa —que **sí** usa OTG y ETH, en 8 de sus 120
grupos, sobre 2,33 s de tiempo simulado—:

| | Tiempo |
| :--- | ---: |
| **A** | 20,81 s / 20,83 s |
| **C** | 20,65 s / 20,74 s |

Diferencia: **0,5 %**. Y **1811/1811 comprobaciones siguen pasando** con C.

---

## 4. Lo que estos números significan

**El sobrecoste es una tasa fija por segundo SIMULADO, no un porcentaje.**
Unos 56 000 despertares por segundo simulado, que son entre **8 y 25 ms de
anfitrión por cada segundo simulado** según lo cargado que esté el simulador.
No depende de lo que haga el firmware: depende de cuánto tiempo simulado pase.

De ahí que el porcentaje varíe tanto, y de ahí que citar un porcentaje suelto
sea engañoso. Lo que cambia es el denominador:

| Carga | Coste de la carga | Sobrecoste | Fracción |
| :--- | ---: | ---: | ---: |
| MCU aparcado | 0,33 s anfitrión / s simulado | ~25 ms/s | 7 % |
| `blinky` | 0,36 s/s | ~21 ms/s | 6 % |
| CoreMark | 0,79 s/s | ~20 ms/s | 2,4 % |
| Suite completa | 8,9 s/s | ~45 ms/s | 0,5 % |

**Cuanto más trabajo real haga la simulación, menos se nota.** El escenario que
más sufre es justamente el que menos importa: un MCU encendido sin hacer nada.

### Lo que NO cuesta

Conviene decirlo porque es la intuición que se suele tener al revés:

* **La elaboración es indistinguible.** Arrancar el proceso entero —construir
  los 62 módulos, los 144 pads, el núcleo— cuesta **0,0337 s en las tres
  variantes**. Los dos periféricos no se notan al construirse.
* **La memoria tampoco.** 16,9 MiB (A) frente a 17,0 MiB (C): ruido. Un dummy
  con un banco de registros generoso llega a gastar *más*.
* **El binario crece 0,6 MiB** de código con el modelo completo. Irrelevante.
* **El decodificador de bus no cambia**, porque el dummy sigue ocupando su
  entrada en la tabla de esclavos: el coste por transacción es el mismo.

Es decir: **un periférico modelado pero quieto no cuesta prácticamente nada por
existir. Solo cuesta por despertarse.**

---

## 5. La conclusión, y lo que se ha hecho con ella

Elegir B —quitar el modelo— compra entre un 2 % y un 7 %, y a cambio se pierde
todo: no se puede probar el firmware de USB, ni el de Ethernet, ni el
comportamiento de los pines compartidos, ni las erratas de configuración que el
modelo destapa. Es un mal negocio.

**La opción C compra el doble y no cuesta nada**, porque el problema nunca fue
el modelado: era el sondeo. El cambio son dos condiciones:

```cpp
// eth_mac.h — antes de sondear, comprobar si hay algo que sondear
if (!mac_activo() || !(dmaomr_ & OMR_ST)) { sc_core::wait(tx_ev_); continue; }
sc_core::wait(sc_core::sc_time(20, sc_core::SC_US), tx_ev_);

// otg.h — con el transceptor apagado no hay tramas que contar
if (!rst_n.read() || !clock_enabled() || !phy_encendido()) {
    sc_core::wait(motor_ev_);
    continue;
}
```

Más las notificaciones que despiertan al hilo cuando el bloque vuelve a la vida:
salida de reset, llegada del reloj, escritura de `DMAOMR` o de `GCCFG`. Sin
ellas el hilo se dormiría para siempre, que es el error clásico al hacer este
cambio.

**Ya está aplicado y verificado: 1811/1811, 0 fallos.**

### La regla general que deja esto

En un modelo de eventos discretos, **el coste de un bloque es proporcional a
cuántas veces despierta, no a lo complicado que sea**. Un periférico de mil
líneas que solo reacciona a eventos es gratis cuando está apagado; uno de
cincuenta líneas que mira el reloj cada 20 µs no lo es nunca.

Aplicada esa misma regla al resto del modelo, el sospechoso mayor no era ningún
periférico: era el propio núcleo. La sección 6 lo cuenta.

---

## 6. El núcleo dormido: el mismo error, veinte veces mayor

El bucle de sueño de `core/cpu.h` **sondeaba cada 1 µs** mientras la CPU estaba
dormida en un `WFI` o un `WFE`: **un millón de despertares por segundo
simulado**, veinte veces más que OTG y ETH juntos. Cada uno para preguntar si
había algo pendiente, encontrar que no, y volver a dormirse.

Y es el caso que más importa, porque **dormir es lo que hace un MCU la mayor
parte del tiempo**. El blinky de referencia del propio proyecto retrasa así:

```c
while ((g_ms - t0) < ms) { __WFI(); }     // verif/fw/blinky/blinky.c:93
```

### El arreglo

Esperar a los sucesos que de verdad despiertan al núcleo, que son estos y solo
estos [ARMv7-M B1.5.18]:

| Suceso | De dónde viene |
| :--- | :--- |
| una excepción queda pendiente | `sys->pending_ev()`, nuevo en `core_sys_if` |
| llega un evento del EXTI (WFE) | `event_in` |
| el núcleo entra en reset | `rst_n` |
| se para el árbol de reloj (Stop) | `fclk_hz` |
| el depurador quiere parar | `dbg_halt_req` y `dbg->dbg_wake()` |

El evento nuevo, `pending_ev()`, se dispara en tres sitios del SCS: al muestrear
las líneas de interrupción (`sample_proc`), en `set_pending()`, y **en cualquier
escritura al SCS**. Este último es el que evita el error sutil: `ISPR`, `STIR`,
`ICSR` con `PENDSVSET` o `SHCSR` pueden dejar una excepción pendiente, y esas
escrituras **pueden venir del depurador con el núcleo dormido**. En vez de
enumerar los casos uno a uno —y olvidarse de alguno—, se avisa siempre: escribir
en el SCS es un suceso raro comparado con sondear.

### La carrera que había que cerrar

`wait()` solo ve las notificaciones **posteriores** a la propia espera. Un
suceso que ocurriera entre la instrucción `WFI`/`WFE` y la espera se perdería, y
el núcleo se dormiría para siempre. El sondeo de antes tapaba esa carrera a base
de fuerza bruta.

Por eso la condición **se re-comprueba justo antes de bloquear**, y solo se
bloquea si en ese instante no hay nada. Entre la comprobación y el `wait` no
cede el control ningún otro proceso, así que la ventana se cierra del todo.

### Lo que cuesta ahora un MCU dormido

Dos segundos de tiempo simulado con el MCU encendido y parado en `wfe`:

| | Deltas | Tiempo de anfitrión |
| :--- | ---: | ---: |
| Original | 2 112 245 | 0,575 s |
| Solo periféricos (§5) | 2 000 229 | 0,492 s |
| **Núcleo y periféricos** | **50** | **0,000 s** |

**Cuarenta y dos mil veces menos eventos.** El tiempo simulado con el MCU
dormido ha dejado de costar nada, que es exactamente lo que debe costar en un
modelo de eventos discretos.

### Y con firmware de verdad

200 ms simulados, 15 repeticiones, mediana:

| Carga | Original | Solo periféricos | **Núcleo y periféricos** | Mejora |
| :--- | ---: | ---: | ---: | ---: |
| MCU aparcado en `wfe` | 0,0580 s | 0,0491 s | **0,0000 s** | — |
| `blinky` (SysTick + WFI) | 0,0608 s | 0,0530 s | **0,0047 s** | **12,9×** |
| CoreMark | 0,1362 s | 0,1247 s | **0,1052 s** | **1,29×** |
| Suite completa (2,33 s simulados) | 18,20 s | — | **17,20 s** | 5,5 % |

**La prueba de que no se ha cambiado nada más:** en `blinky`, las transacciones
que cruzan la matriz son **13 713 en las dos versiones**, exactamente las
mismas. Mismas instrucciones, mismo tráfico de bus; lo único que ha desaparecido
es la espera desperdiciada. Los deltas bajan de 215 872 a 7 197 y el tiempo de
anfitrión de 0,0726 s a 0,0050 s: **catorce veces y media más rápido haciendo el
mismo trabajo**.

La suite mejora poco —un 5,5 %— y tiene sentido: está dominada por trabajo
activo (el maestro de pruebas machacando el bus, CoreMark, los quince
firmwares), no por esperas. Es justo el perfil en el que menos se nota. El
tiempo simulado total cambia en 20 ns sobre 2,33 s, que es el redondeo de la
latencia de despertar al pasar de 1 µs de granularidad a inmediata.

### Lo que queda

`ClockGen`, cuando las ondas cuadradas de los relojes están encendidas. Ahí el
sondeo **sí es el modelo** —un reloj es una onda—, así que no es un error sino
un coste legítimo; lo que hay es el interruptor `set_internal_waveforms(false)`
para apagarlo cuando nadie observa los flancos, y eso ya está.

---

## 7. Cómo reproducirlo

```sh
make -f Makefile.mcu-sim bench

./build/bench "" 2000 reset          # coste puro: MCU en reset, 2 s simulados
./build/bench "" 200                 # MCU aparcado
./build/bench verif/fw/blinky/blinky.bin     200
./build/bench verif/fw/coremark/coremark.bin 200
```

La salida trae el tiempo de anfitrión, los deltas y las transacciones que han
cruzado la matriz, que es el denominador honesto de cualquier discusión sobre
el coste del bus.
