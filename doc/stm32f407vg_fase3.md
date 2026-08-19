# Fase F3 — Pines, GPIO y RCC eléctrico

Informe de implementación de la fase **F3** del plan `doc/smt32f407vg_diseño.md`
(§7). Continúa a `doc/smt32f407vg_fase0.md` (esqueleto), a
`doc/stm32f407vg_fase1.md` (infraestructura: relojes, reset, matriz AHB,
memorias) y a `doc/stm32f407vg_fase2.md` (núcleo Cortex-M4F). Fuentes:
`doc/informe_revisado.md` [IR] y `doc/informe_instrucciones.md` [II].

**Alcance según el plan:** *"Pads/pin_mux/GPIO/RCC completos (modelo
eléctrico)"*.
**Criterio de salida:** *"blinky real compilado con CMSIS"*.

**Resultado:** criterio cumplido. El modelo compila sin avisos con
`-Wall -Wextra -O2` y la suite de verificación pasa **216 de 216
comprobaciones** (124 de F1 + 12 de F2 + 80 nuevas de F3, código de salida 0).
El criterio de salida se cumple con el firmware que se grabaría en una placa
STM32F4-Discovery: **CMSIS-Core de ARM y `cmsis_device_f4` de ST sin
modificar**, con el `startup_stm32f407xx.s` y el `system_stm32f4xx.c` oficiales,
que arranca el HSE con un cristal de 8 MHz, engancha el PLL a 168 MHz y
parpadea el LED de PD12 con el SysTick — **y el parpadeo se observa como
tensión y corriente en el pin**, a través de un modelo de LED con resistencia
en serie conectado al nodo analógico. Verificado con SystemC 2.3.4 / g++ 13.3 /
C++17 y arm-none-eabi-gcc 13.2.1.

---

## 1. Resumen ejecutivo

| Bloque | Estado tras F3 |
| :--- | :--- |
| Nodo analógico de pin (`AnalogNet`) | Completo: resolución Thevenin, corriente por driver, conductancia total y **detección de nodo flotante** |
| Pad de E/S (`Pad`) | Completo: proceso sensible al nodo, trigger Schmitt con histéresis, banda no garantizada VIL-VIH, push-pull / open-drain, pull-up/pull-down de 40 kΩ, impedancia y retardo por OSPEEDR, rango absoluto con tolerancia a 5 V, vigilancia de corriente por pin |
| Multiplexor de AF (`PinMux`) | Funcional: un proceso de salida por pad y uno por señal de entrada de periférico, tabla AF registrable, AF0 (SWD/JTAG, MCO1/2) y AF15 (EVENTOUT) cableadas, ruta analógica |
| Puerto GPIO (`GpioPort`) | Completo: MODER/OTYPER/OSPEEDR/PUPDR/IDR/ODR/BSRR/LCKR/AFRL/AFRH, valores de reset especiales, BSRR atómico, FSM del LCKR, IDR muestreado con HCLK |
| Osciladores externos | Arranque condicionado a la presencia eléctrica del cristal; caída al desaparecer; medida de la frecuencia en modo bypass |
| Clock Security System | Completo: fallo del HSE → HSEON/CSSON a 0, SYSCLK a HSI, CSSF y NMI |
| MCO1 / MCO2 | Encaminados a PA8 y PC9 como AF0, con sus preescaladores |
| RCC_SSCGR | Se calculan f_Mod y la profundidad y se aplica el desplazamiento medio de frecuencia |
| Supervisión de alimentación | POR/PDR con histéresis, BOR con el nivel de los option bytes, distinción PORRSTF/BORRSTF, validación de rangos de VDD/VDDA/VBAT |
| Option bytes | Ahora **no volátiles**: OPTCR se recarga del bloque programado en cada reset |
| Frecuencia por dominio | Cada `BusSlave` anota sus accesos en ciclos de su propio bus (`clk_hz`) |
| Gating de reloj | Combinacional: la instrucción siguiente a `RCC->AHB1ENR |= ...` ya accede al periférico |
| Circuitería externa de prueba | Nueva biblioteca `verif/ext_parts.h`: cristal, reloj externo, resistencia, LED, pulsador y driver digital |
| Firmware de referencia | Blinky con CMSIS oficial de ARM y ST (Apache-2.0), compilado en el propio repositorio |

Código nuevo o reescrito en esta fase: **≈2 000 líneas** de modelo y
verificación, más el CMSIS vendorizado.

---

## 2. Ficheros de la fase

### 2.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `src/pins/af_types.h` | `AfEndpoint`, `AF_NONE` e interfaz `af_sel_if`, que el puerto GPIO usa para publicar su selección de AF sin depender del módulo que contiene los pads |
| `src/verif/ext_parts.h` | Componentes externos de placa para el banco de pruebas: `Crystal`, `ExtClock`, `Resistor`, `Led`, `Button`, `Driver` |
| `src/verif/fw/cmsis/` | CMSIS-Core de ARM y `cmsis_device_f4` de ST (Apache-2.0, licencias incluidas) más `startup_stm32f407xx.s` y `system_stm32f4xx.c` oficiales |
| `src/verif/fw/blinky/` | Blinky de referencia (`main.c`, `Makefile`) compilado contra el CMSIS anterior |

### 2.2 Reescritos o ampliados

| Fichero | Cambios |
| :--- | :--- |
| `src/common/analog_net.h` | `conductance()`, `floating()` y conservación de la última tensión resuelta cuando el nodo queda en alta impedancia |
| `src/pins/pad.h` | Modelo eléctrico completo (§3) |
| `src/pins/pin_mux.h` | Multiplexor real de funciones alternativas (§4) |
| `src/pins/power_pads.h` | Supervisión POR/PDR/BOR y validación de rangos (§6) |
| `src/periph/gpio_port.h` | Banco de registros completo del puerto (§5) |
| `src/rcc/osc_pll.h` | Presencia eléctrica de la fuente, fallo, medida en bypass, escritor único del flag RDY |
| `src/rcc/rcc.h` | CSS, SSCGR, gating combinacional, extensión de `set_internal_waveforms` |
| `src/mem/flash_if.h` | Option bytes no volátiles y salida `bor_lev` hacia el supervisor de alimentación |
| `src/common/periph_base.h` | Puerto `clk_hz` (frecuencia del dominio) y gating combinacional `clk_en_live` |
| `src/core/cpu.h` | El núcleo no ejecuta sin reloj |
| `src/top/stm32f407vg.h`, `stm32f407vg_bind2.h` | Enlazado del mux de AF, de los nodos de OSC_IN, de BOR_LEV y de la frecuencia de dominio de cada esclavo |
| `src/top/sc_main.cpp` | Grupos T18-T25 y circuitería externa de la placa |

---

## 3. El pad como frontera eléctrica

El contrato del proyecto es que fuera del encapsulado todo es analógico y en
`float`, y que la digitalización ocurre en el pad. F3 completa ese contrato.

### 3.1 Resolución del nodo

`AnalogNet` resuelve, en cada actualización, la superposición de los
equivalentes Thevenin de todos los drivers conectados:

```
V_pin = Σ(V_i/R_i) / Σ(1/R_i)          I_i = (V_i − V_pin) / R_i
```

La novedad de F3 es la **detección de nodo flotante**: si la conductancia total
cae por debajo de 1 nS (equivalente a que todos los drivers presenten más de
1 GΩ) el nodo se marca como flotante y conserva la última tensión resuelta en
lugar de inventar 0 V. Es lo que permite distinguir «pin a 0 V» de «pin al
aire», que son situaciones eléctricamente muy distintas y que el firmware
percibe de forma distinta.

### 3.2 Trigger Schmitt

El pad digitaliza con dos umbrales:

* **conmutación**: VT+ = 0,5·VDD + VHYS/2 y VT− = 0,5·VDD − VHYS/2, con
  VHYS = 10 % de VDD (mínimo 200 mV) [IR, §2.4]. Dentro de la banda el
  comparador **conserva el nivel anterior**, que es el comportamiento físico
  de un Schmitt;
* **validez**: la hoja de características solo garantiza el nivel por debajo de
  VIL = 0,3·VDD y por encima de VIH = 0,7·VDD [IR, §3.5]. Entre ambos, el pad
  entrega su nivel pero marca `din_valid = 0`, y lo mismo hace con un nodo
  flotante. Esa señal no existe en el silicio: es instrumentación del modelo
  para que el banco de pruebas detecte configuraciones eléctricamente dudosas.

### 3.3 Buffer de salida

* **Push-pull**: driver a VDD o a VSS con la impedancia del nivel de OSPEEDR.
* **Open-drain**: el '1' es alta impedancia y el '0' conduce a VSS; el nivel
  alto lo tiene que aportar el circuito externo. La prueba T18 lo comprueba con
  una carga a VSS (el pin se queda abajo) y con un pull-up externo de 4,7 kΩ
  (el pin sube a VDD).
* **Pull-up / pull-down** de 40 kΩ, desconectados en modo analógico.
* **Modo analógico**: buffers y Schmitt desconectados, pulls forzados a off y
  entrada digital a 0 [IR, §3.3.4].

⚠ **No disponible en las fuentes:** la hoja de características da la
*frecuencia máxima de conmutación* por nivel de OSPEEDR (2 / 25 / 50 / 100 MHz
[IR, §3.2, §3.4.3]) pero no la impedancia del driver ni el tiempo de
transición. Se modelan como parámetros públicos por instancia:
`r_on_speed[4] = {55, 40, 30, 25} Ω` y `t_pd_speed[4] = {50, 4, 2, 1} ns`. El
retardo de propagación solo se aplica cuando el buffer está activo; como
`sc_event::notify` conserva la notificación más temprana, un pin lento que
recibe cambios más rápidos que su retardo colapsa las transiciones, que es el
efecto observable del límite de frecuencia.

### 3.4 Rango absoluto y corriente

* Los pines FT admiten hasta 5,5 V, **salvo en modo analógico u oscilador**,
  donde el límite vuelve a VDD+0,3 V [IR, §2.1, nota 4]. El pad publica
  `out_of_range` y emite un aviso una sola vez.
* Se vigila la corriente por pin contra el máximo de 25 mA, con la excepción de
  PC13/PC14/PC15, limitados a 3 mA por pasar por el conmutador de potencia del
  dominio de backup [IR, §2.1, nota 2].
* `PinMux::total_pin_current()` acumula la corriente de todos los pines
  soldados, para contrastarla con el límite de 240 mA de VDD/VSS [IR, §2.4].

---

## 4. Multiplexor de funciones alternativas

El multiplexor es el punto donde se decide quién gobierna cada pad. La
restricción de SystemC de un único escritor por `sc_signal` marca la estructura:

* **una salida por pad**: un proceso por pin, creado en `end_of_elaboration`
  (las AF se registran durante el enlazado del top, después del constructor),
  sensible al bundle del GPIO, a los `out`/`oe` de todas las AF registradas en
  ese pin y al evento de cambio de selección. Si `MODER` indica AF y `AFRL/AFRH`
  selecciona una función registrada, el periférico gobierna `out` y `oe`; el
  resto de la configuración (tipo de salida, pulls, velocidad) sigue viniendo
  del puerto GPIO, como en el silicio. Si la AF seleccionada **no está
  modelada**, el pin queda en alta impedancia en lugar de quedarse con el valor
  del ODR: es la forma honesta de representar «este periférico aún no existe».
* **una entrada por señal de periférico**: varios pines pueden encaminar la
  misma entrada (por ejemplo USART1_RX en PA10 y en PB7), así que el proceso se
  agrupa por señal destino y entrega el nivel del primer pin que tenga esa AF
  seleccionada, o el valor de reposo del periférico si ninguno la tiene.

Registradas en esta fase: **AF0** con SWDIO (PA13, bidireccional con su `oe`),
SWCLK (PA14), JTDI (PA15), JTDO/SWO (PB3), NJTRST (PB4), MCO1 (PA8) y MCO2
(PC9); y **AF15** (EVENTOUT) en los 144 pines de puerto. El resto de la tabla se
irá registrando al implementar cada periférico en F4 y F5; la infraestructura
ya está y `bind_analog()` contiene los ejemplos de USART, I2C y SPI.

Un efecto observable que se comprueba en T20: los pines de depuración arrancan
en AF0 por los valores de reset de GPIOA/GPIOB, y **reconfigurar PA13 como GPIO
desconecta el puerto SWD**, igual que ocurre en el silicio.

---

## 5. Puerto GPIO

Banco de registros completo [IR, §3.4] con los valores de reset especiales de
los pines de depuración (GPIOA_MODER = 0xA800 0000, GPIOA_OSPEEDR =
0x0C00 0000, GPIOA_PUPDR = 0x6400 0000, GPIOB_MODER = 0x0000 0280,
GPIOB_PUPDR = 0x0000 0100).

* **BSRR** es atómico y **BS tiene prioridad sobre BR** en el mismo ciclo
  [IR, §3.4.7]; se lee como 0.
* **LCKR** implementa la máquina de estados de la secuencia 1-0-1: una vez
  activo LCKK, los bits de MODER, OTYPER, OSPEEDR, PUPDR y AFRL/AFRH de los
  pines bloqueados dejan de ser modificables hasta el siguiente reset; una
  secuencia incorrecta (por ejemplo con LCK distinto entre escrituras) no
  bloquea nada.
* **IDR** se muestrea un ciclo de HCLK después de que cambie el pin
  [IR, §3.3.1]. El muestreo **no espera un flanco de la señal de reloj** sino
  que calcula el instante con la frecuencia del dominio: así el modelo no
  obliga a generar la onda cuadrada de HCLK, que a 168 MHz es el mayor coste de
  simulación del sistema (véase §8). En modo analógico el IDR lee 0.
* Las 16 líneas hacia EXTI salen **sin** la latencia del IDR, porque el
  detector de flancos del EXTI es asíncrono.

---

## 6. RCC eléctrico

### 6.1 Presencia del cristal

HSE y LSE solo alcanzan su flag RDY si hay algo conectado eléctricamente al
nodo de OSC_IN (PH0 para el HSE, PC14 para el LSE): un cristal con su red de
polarización, o un reloj externo en modo bypass. Un `HSEON = 1` sin cristal deja
`HSERDY = 0` indefinidamente, que es exactamente lo que ve el firmware real
cuando falta el componente. Si la fuente desaparece con el oscilador en marcha,
el oscilador cae; si reaparece con `xxxON` todavía a 1, reintenta el arranque.

En **modo bypass** el modelo mide la frecuencia real del reloj inyectado
contando el periodo entre dos flancos de subida digitalizados por el pad, y
reprograma el generador en caliente. T21 inyecta 12 MHz y comprueba que el
modelo los mide (12,000 MHz medidos) en lugar de usar el valor nominal.

### 6.2 Clock Security System

Con `CSSON = 1`, el fallo del HSE dispara la secuencia completa [IR, §4.2]:
se apagan HSEON y CSSON, si SYSCLK venía del HSE (o del PLL alimentado por el
HSE) se conmuta a HSI y se apaga el PLL, se activa `CSSF` en RCC_CIR y se genera
una **NMI**, que no es enmascarable. El flag se limpia escribiendo `CSSC` y el
CSS solo se rearma con un reset del sistema. T22 rompe el cristal en marcha y
comprueba los cinco efectos, incluido que el núcleo toma la excepción y que
HCLK vuelve a 16 MHz.

### 6.3 MCO1 y MCO2

Los generadores de MCO ya existían desde F1; en F3 se encaminan a PA8 y PC9 como
AF0. T23 configura MCO1 = HSI con preescalador /4 y **cuenta los flancos en el
pin**, obteniendo 4,05 MHz sobre los 4 MHz teóricos (la diferencia es el
truncamiento de la ventana de medida).

### 6.4 Espectro ensanchado (RCC_SSCGR)

A partir de MODPER e INCSTEP se recuperan las magnitudes físicas invirtiendo las
fórmulas de programación del manual de referencia:

```
f_Mod = f_PLL_IN / (4 · MODPER)
md[%] = INCSTEP · 100 · 5 · MODPER / ((2^15 − 1) · PLLN)
```

El modelo aplica el **desplazamiento medio** de frecuencia que produce la
modulación: 0 en *center spread* y −md/2 en *down spread*. No se modela el
jitter instantáneo: este modelo no representa fase ciclo a ciclo y hacerlo
multiplicaría el número de eventos sin que ningún consumidor pudiera
observarlo. Se avisa si se escribe RCC_SSCGR con el PLL en marcha, que es una
programación no soportada por el silicio.

---

## 7. Supervisión de alimentación y option bytes

`PowerPads` compara VDD contra un umbral con histéresis:

* con el **BOR desactivado** (BOR_LEV = 11, valor de fábrica) manda el par
  POR/PDR: el dispositivo arranca por encima de VPOR y se resetea por debajo de
  VPDR;
* con el **BOR activo** manda el nivel programado: 10 → ~2,1 V, 01 → ~2,4 V,
  00 → ~2,7 V [IR, §5.7.1], con una histéresis de 100 mV.

La causa se publica por separado (`bor_trip`), lo que permite al RCC distinguir
**PORRSTF** de **BORRSTF** en RCC_CSR [IR, §4.10]. Se validan además los rangos
de VDD, VDDA y VBAT y la diferencia máxima de 300 mV entre VDD y VDDA
[IR, §2.2].

⚠ **No disponible en las fuentes con valor numérico:** las tensiones exactas de
POR/PDR (se usan 1,72 V de subida y 1,68 V de bajada) y la histéresis del BOR.
Son parámetros públicos de `PowerPads`.

Para que esto funcione hubo que corregir un fallo real del modelo heredado de
F1: **los option bytes son memoria no volátil**, y OPTCR debe recargarse del
bloque programado en cada reset en lugar de volver a una constante. Sin eso, un
BOR_LEV programado desaparecía justo con el reset que él mismo provocaba.

---

## 8. Verificación

Ocho grupos nuevos, **80 comprobaciones**, todos ejecutados sobre el MCU
completo con circuitería externa conectada a los pines.

| Grupo | Contenido |
| :--- | :--- |
| **T18** | Pad: alta impedancia, pull-up/pull-down internos, divisor 40k/40k con resistencia externa (zona no garantizada e histéresis), push-pull contra carga con su corriente, open-drain con y sin pull-up externo, sobrecorriente, tolerancia a 5 V y rechazo de 6 V, modo analógico |
| **T19** | GPIO: valores de reset especiales, ODR, BSRR (incluida la prioridad de BS sobre BR), secuencia LCKR correcta e incorrecta, IDR con estímulo externo y en modo analógico |
| **T20** | Mux de AF: PA13 en AF0 tras el reset y su desconexión al pasar a GPIO, AF no modelada → alta impedancia, EVENTOUT en AF15 |
| **T21** | HSE: sin cristal no hay HSERDY; reloj externo de 12 MHz en bypass, medido; caída al retirarlo |
| **T22** | CSS completo: CSSF, apagado de HSEON, conmutación a HSI, HCLK de vuelta a 16 MHz, NMI en el núcleo y borrado con CSSC |
| **T23** | MCO1 = HSI/4 medido contando flancos en PA8 |
| **T24** | BOR programado por option bytes, reset por caída de VDD, BORRSTF frente a PORRSTF, y BOR desactivado |
| **T25** | Blinky con CMSIS (criterio de salida) |

### 8.1 Circuitería externa de prueba

`verif/ext_parts.h` es una pequeña biblioteca de componentes de placa. Todos
derivan de `ExtPart`, que registra un driver en el nodo del pin y, al
destruirse, lo deja en alta impedancia (se «desuelda»):

* `Crystal` — red de polarización del lazo oscilador, con `attach()`/`detach()`
  para simular que el cristal se rompe;
* `ExtClock` — reloj externo de onda cuadrada con impedancia de salida;
* `Resistor` — pull externo a VDD o a VSS;
* `Led` — LED con resistencia en serie; el diodo **no es lineal**, así que se
  modela con dos estados (conduciendo, con equivalente {Vf, R}, o en corte, en
  alta impedancia) reevaluados cada vez que cambia la tensión del pin;
* `Button` — pulsador a VSS;
* `Driver` — driver digital externo genérico.

El banco de pruebas monta la placa: cristal de 8 MHz en PH0, cristal de
32,768 kHz en PC14, LED en PD12 y pulsador en PA0.

### 8.2 T25 — Blinky con CMSIS

El firmware usa **CMSIS-Core de ARM y `cmsis_device_f4` de ST sin ninguna
modificación** (Apache-2.0; las dos licencias están en el repositorio), con el
`startup_stm32f407xx.s` y el `system_stm32f4xx.c` oficiales. `main.c` es el
ejemplo canónico de la Discovery: 5 estados de espera y ART, HSE con el cristal
de 8 MHz, PLL M=8 N=336 P=2 Q=7 → 168/42/84 MHz, PD12 como salida push-pull,
PA0 como entrada con pull-down, SysTick a 1 kHz y `__WFI()` entre parpadeos.

Resultado:

```
SystemCoreClock = 168000000 Hz | conmutaciones = 6 | ticks = 600
14062 instrucciones | encendidos del LED observados = 3
```

Lo que se comprueba no es solo que el firmware termine, sino que:

* `SystemCoreClockUpdate()` —código de ST, leyendo los registros del modelo—
  calcula 168 MHz, y el árbol de reloj del modelo publica 168/42/84 MHz;
* el SysTick de CMSIS entrega 600 interrupciones en 600 ms simulados;
* **el LED se enciende y se apaga en el pin**: el banco de pruebas observa el
  componente externo, no el registro ODR;
* PA0 con pull-down interno se lee a 0 y sube a 1 cuando un driver externo lo
  fuerza.

### 8.3 Resultado de la suite

```
Resumen F1: 124 comprobaciones OK, 0 fallos
Resumen F2:  12 comprobaciones OK, 0 fallos
Resumen F3:  80 comprobaciones OK, 0 fallos
TOTAL     : 216 comprobaciones OK, 0 fallos
```

La suite completa (incluidos CoreMark y el blinky) tarda **unos 2 s** de CPU del
anfitrión.

---

## 9. Fallos del modelo que ha destapado esta fase

Los tres primeros los descubrió el firmware de CMSIS, que es exactamente para
lo que sirve un criterio de salida basado en software real.

| # | Síntoma | Causa y corrección |
| :--- | :--- | :--- |
| 1 | El blinky entra en HardFault en el primer acceso a GPIOD, justo después de `RCC->AHB1ENR \|= GPIODEN` | El gating llegaba al periférico por un `sc_signal`, es decir un delta más tarde, y el `b_transport` del RCC corre en el proceso del maestro, que no cede el control entre instrucciones. En el silicio la puerta de reloj es combinacional. Se añade `BusSlave::clk_en_live`, un puntero al estado interno del RCC |
| 2 | El BOR programado desaparecía con el reset que él mismo provocaba | Los option bytes son no volátiles: OPTCR debe recargarse del bloque programado, no de una constante |
| 3 | La simulación se quedaba girando sin avanzar el tiempo al caer la alimentación | Con el árbol de reloj parado, la anotación de ciclos del núcleo era nula y el intérprete giraba indefinidamente. Ahora el núcleo no ejecuta sin reloj, que además es lo que hace el silicio |
| 4 | E115: dos escritores sobre el flag RDY del oscilador | El arranque y la vigilancia de la fuente lo actualizan; se aplica la convención del modelo (un único proceso publica el puerto) |
| 5 | Un componente externo temporal del banco de pruebas seguía cargando el pin tras destruirse | `AnalogNet` no tiene «desregistro»; el destructor de `ExtPart` deja su driver en alta impedancia |

---

## 10. Decisiones de diseño de esta fase

| # | Decisión | Motivo |
| :--- | :--- | :--- |
| **F3-1** | El nodo flotante se detecta por conductancia total y conserva la última tensión resuelta | Distinguir «0 V» de «al aire» es el punto del modelado eléctrico de pines; devolver 0 V para un nodo sin drivers daría por bueno un nivel lógico que no existe |
| **F3-2** | El Schmitt conmuta con histéresis y `din_valid` marca la banda VIL-VIH | Son dos cosas distintas: el comportamiento físico (mantener el nivel dentro de la banda) y la garantía de la hoja de características (solo fuera de ella). El modelo entrega ambas |
| **F3-3** | Un pin en AF sin periférico modelado queda en alta impedancia | Es la representación honesta de «esto todavía no existe»; dejar el ODR gobernando daría un comportamiento falso que un firmware real podría dar por bueno |
| **F3-4** | El mux tiene un proceso por pad (salida) y uno por señal de periférico (entrada) | SystemC solo admite un escritor por señal, y varios pines pueden encaminar la misma entrada de periférico |
| **F3-5** | El IDR se muestrea calculando el instante con `clk_hz` en lugar de esperar un flanco de HCLK | Es observacionalmente equivalente y permite ejecutar firmware largo con la onda cuadrada de los relojes apagada |
| **F3-6** | HSE y LSE exigen presencia eléctrica en OSC_IN | Sin ello, «HSEON sin cristal» arrancaría en el modelo y no en la placa, que es justo el fallo que un modelo debe reproducir |
| **F3-7** | El gating de reloj se consulta de forma combinacional (`clk_en_live`) y no por el puerto | El puerto sigue existiendo para los procesos internos de cada periférico; el camino de bus necesita el valor del mismo delta |
| **F3-8** | Las salidas de MCO y los generadores de los osciladores y del PLL entran en `set_internal_waveforms` | MCO2 selecciona SYSCLK por defecto: a 168 MHz su onda cuadrada costaba más que todo el resto de la simulación junta |
| **F3-9** | El SSCGR aplica el desplazamiento medio y no el jitter instantáneo | El modelo no representa fase ciclo a ciclo; simular la modulación multiplicaría los eventos sin efecto observable |
| **F3-10** | Los componentes externos viven en `verif/`, no en el modelo | El MCU termina en el nodo analógico del pin; la placa es del banco de pruebas |

---

## 11. Trabajo pendiente heredado a fases posteriores

**Resuelto en F3 de lo que dejó F1:** detección eléctrica de cristal en HSE/LSE,
CSS con NMI, modulación de espectro ensanchado, encaminamiento de MCO1/MCO2,
umbrales reales de POR/PDR/BOR con `BOR_LEV`, y frecuencia de dominio en cada
`BusSlave`.

**F4 (DMA, USART, TIM, EXTI/SYSCFG):**

* registro de la tabla AF de cada periférico conforme se implemente
  (TIM CHx, USART, SPI, I2C, CAN, SDIO...); la infraestructura del mux ya no
  cambia;
* EXTI completo sobre las líneas que el GPIO ya entrega (detector de flanco,
  máscaras, software trigger) y el mux de puerto de SYSCFG_EXTICR;
* consumo de `IDR`/`ODR` desde el DMA.

**F5 (analógico):**

* ADC y DAC sobre la ruta analógica del pad, que ya está disponible
  (`PinMux::analog`): el DAC como driver Thevenin del nodo y el ADC como carga
  de alta impedancia;
* nodo VREF+ independiente de VDDA.

**F7 (bajo consumo y afinado):**

* PVD en el PWR a partir de `vdd_lvl`, con los niveles de `PLS[2:0]`;
* modos Stop y Standby: qué pads conservan su estado y cuáles pasan a alta
  impedancia;
* elevación de la matriz a AT y temporización de pads dependiente de la carga
  capacitiva (hoy el retardo por OSPEEDR es un parámetro fijo, no una función
  de la carga);
* límite acumulado de 240 mA en VDD/VSS como comprobación automática, usando
  `PinMux::total_pin_current()`, que ya existe.
