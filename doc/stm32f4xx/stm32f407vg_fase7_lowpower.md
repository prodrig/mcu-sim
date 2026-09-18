# Fase F7 (parte de bajo consumo) — Sleep, Stop, Standby y el consumo como magnitud

Primera parte de la fase F7. Cubre **únicamente los modos de bajo consumo**
[IR, cap. 14]: la máquina de energía completa, lo que se apaga en cada modo, las
fuentes de despertar, el PVD, el pin WKUP, el dominio de backup, la depuración
de firmware que duerme y —lo que ata todo lo anterior— **un modelo de consumo
que presenta la corriente del MCU como carga real sobre el pin VDD**. Lo que
queda de F7 (OTG, ETH, FSMC, DCMI y el afinado de la matriz) no se toca aquí.

**Resultado:** la suite pasa **1501 de 1501 comprobaciones** (116 nuevas, grupos
T98 a T104), compilación limpia con `-Wall -Wextra -O2`, 13 s de ejecución.
Entre las nuevas hay un firmware de verdad, compilado con CMSIS, que recorre los
tres modos él solito mientras el banco se limita a apretarle el pulsador.

---

## 1. Qué se apaga en cada modo

Un modo de bajo consumo no es un bit: es una lista de cosas que dejan de
funcionar. Esa lista es todo el trabajo de esta fase.

| | **Sleep** | **Stop** | **Standby** |
| :--- | :--- | :--- | :--- |
| Entrada | WFI/WFE con SLEEPDEEP=0 | SLEEPDEEP=1, PDDS=0 | SLEEPDEEP=1, PDDS=1, WUF limpio |
| Núcleo | parado | parado | **sin alimentación** |
| Relojes del dominio 1,2 V | **siguen** | parados | parados |
| HSI / HSE / PLL | encendidos | **apagados** | apagados |
| LSI / LSE (IWDG, RTC) | siguen | **siguen** | **siguen** |
| Gating de periféricos | `xxxLPENR` | ninguno | ninguno |
| SRAM1/2, CCM, registros | intactos | **intactos** | **se pierden** |
| BKPSRAM | intacta | intacta | intacta **si BRE** |
| Pines | normales | normales | **alta impedancia** |
| Despiertan | cualquier IRQ o evento | cualquier línea EXTI | WKUP, RTC, NRST |
| Salida | sigue la instrucción | sigue, **con HSI** | **reset**, con SBF=1 |
| IDD medido (168 MHz) | 15,1 mA | 230 µA | 3,0 µA |

Las tres filas en negrita del centro son las que separan los tres modos, y cada
una costó su trabajo:

- **En Stop se apagan HSI, HSE y los PLL, pero no el LSI ni el LSE.** No es un
  detalle: es lo que permite que el IWDG siga vigilando y que el RTC siga
  contando la hora mientras el MCU duerme. En el modelo eso sale de una sola
  línea en `apply_osc_controls()`, pero es la línea que hace que un despertador
  por RTC tenga sentido.
- **Al salir de Stop, el reloj de sistema es el HSI.** El hardware apaga
  `HSEON` y `PLLON` al entrar y deja `HSION` puesto y `SW=00` para la vuelta.
  Quien no reprograme el PLL se pasa el resto del programa corriendo a 16 MHz
  sin enterarse. El firmware de demostración lo mide y lo deja escrito
  (§6.3): **168 MHz al dormirse, 16 MHz al despertar**.
- **Standby no es "dormir mucho": es apagarse.** La SRAM se pierde, los
  registros se pierden, los pines quedan en alta impedancia y la salida no es
  una vuelta sino un **arranque por el vector de reset**. Lo único que distingue
  ese arranque de un encendido es `SBF`, y para poder leerlo hay que reactivar
  antes el reloj del propio PWR, porque el RCC también volvió a su reset.

## 2. Ficheros

| Fichero | Contenido |
| :--- | :--- |
| `src/periph/pwr.h` | **Reescrito.** De un banco de registros de 95 líneas a el árbitro de la energía: CR/CSR completos, PVD con histéresis sobre el VDD real, pin WKUP, regulador de backup, la máquina de los cuatro modos con sus tiempos de despertar y el modelo de consumo |
| `src/rcc/rcc.h` | Parada de HSI/HSE/PLL en Stop y Standby (el LSI y el LSE, no), gating por `xxxLPENR` mientras se duerme, el Standby como reset que se mantiene, y la cuenta de relojes de periférico abiertos que necesita el modelo de consumo |
| `src/pins/power_pads.h` | La corriente del MCU, presentada como **carga real** sobre los nodos VDD y VBAT, con su punto de trabajo resuelto por iteración |
| `src/pins/pin_mux.h` | Alta impedancia de todos los pines de puerto en Standby, salvo WKUP y —si DBGMCU lo pide— los de depuración |
| `src/mem/sram.h` | `pierde_contenido()`: apagar el dominio de 1,2 V no deja nada detrás |
| `src/core/cpu.h`, `cpu_exec16/32.h` | El núcleo distingue **cómo** se durmió (WFI o WFE), lee `SCR.SLEEPDEEP` del SCB en vez de por una señal, y deja de anunciarse dormido mientras está en reset |
| `src/core/scs.h` | `core_sys_if::scr_sleepdeep()` |
| `src/core/debug.h` | `DBGMCU_CR[2:0]` publicado hacia el PWR y el RCC |
| `src/common/ahb_types.h` | `enum LpMode`, que mira medio modelo |
| `src/top/*.h` | El netlist de la energía y la pérdida de estado del Standby |
| `src/verif/fw/lowpower_demo/` | **Nuevo.** Firmware CMSIS que recorre los tres modos |
| `src/top/sc_main.cpp` | Grupos T98-T104 (116 comprobaciones) |

## 3. El consumo, como magnitud eléctrica

Aquí es donde esta fase se separa de un modelo funcional. En el encargo de este
proyecto los pines se modelan con **tensiones y corrientes en float**, y el bajo
consumo es, literalmente, el asunto de la corriente. Así que el consumo no se
guarda en una variable: se presenta como una **carga sobre el nodo VDD**.

```
   fuente del banco            nodo VDD (AnalogNet)            MCU
   [3,3 V, Rout] ────────────────────●────────────── [ R = V / I(modo) ]
                                     │
                              V = 3,3 · R/(R+Rout)
```

El PWR calcula la corriente que pide el chip; los pines de alimentación la
convierten en la resistencia que a la tensión actual del nodo pide justo esa
corriente, y como la tensión depende a su vez de la carga, el punto de trabajo
se resuelve iterando (con una fuente de baja impedancia converge en una vuelta).
Es el mismo truco que se usa para meter una carga no lineal en un simulador de
circuitos.

Lo que se gana con eso no es cosmético:

```
    con fuente de 5 ohm: VDD = 2.957 V en Run, 3.189 V en Sleep
```

**La alimentación se hunde cuando el MCU corre y se recupera cuando duerme**, y
se hunde 343 mV porque la fuente tiene 5 Ω y el chip pide 68 mA. Eso permite
plantear en el modelo preguntas que antes no se podían plantear: si el BOR está
a 2,7 V y la pila tiene 5 Ω de resistencia interna, ¿arranca el MCU o se resetea
solo al primer pico de consumo? Con el consumo escrito en una variable, esa
pregunta no existe.

### 3.1 La fórmula

```
  Run     = k(VOS)·(I0 + k_f·f_HCLK[MHz]) + n_periféricos·I_periférico
  Sleep   = k(VOS)·(I0' + k_f'·f_HCLK)    + n_periféricos·I_periférico'
  Stop    = I_stop(regulador normal o LPDS) − ahorro de FPDS
  Standby = I_stby + (RTC vivo ? I_rtc : 0) + (BRE ? I_regulador_backup : 0)
```

El término que más pesa en Run y en Sleep es **cuántos relojes de periférico
hay abiertos**, que es precisamente la palanca que tiene el firmware. Por eso el
RCC publica esa cuenta: no es una estadística, es una entrada del modelo.

⚠ **NO DISPONIBLE EN LAS FUENTES.** El informe [IR, cap. 14] no da ni una sola
corriente ni un solo tiempo de despertar. Todos los coeficientes son los valores
típicos del datasheet a 3,3 V y 25 °C y están **expuestos como parámetros
públicos** de `Pwr`, con la misma convención que se usó para los umbrales de
POR/PDR/BOR en F3. Quien necesite los de su lote los cambia sin tocar el modelo.

### 3.2 Lo que sale, y por qué es creíble

| Situación | Modelo | Datasheet (típico) |
| :--- | ---: | ---: |
| Run, 168 MHz, sin periféricos | 61,5 mA | ~60 mA |
| Run, 168 MHz, con periféricos | 68,5 mA | hasta ~87 mA (todos) |
| Sleep, 168 MHz, LPEN limpios | 15,1 mA | ~15 mA |
| Sleep, 168 MHz, LPEN de reset | 22,3 mA | hasta ~39 mA (todos) |
| Stop, regulador normal | 420 µA | ~420 µA |
| Stop, LPDS + FPDS | 230 µA | ~290 µA |
| Standby con RTC | 3,0 µA | ~3 µA |

Y la lección de los LPENR, que es de las que se cuentan en los cursos y aquí se
**mide**: limpiar antes de dormir los `LPEN` de lo que no se usa baja el consumo
en Sleep de 22,3 mA a 15,1 mA. Un tercio, gratis.

## 4. Decisiones que hubo que tomar

### 4.1 Qué registro gobierna el gating durante el Sleep

El manual admite dos lecturas y hay que elegir una. La descripción de cada bit
`LPEN` es **incondicional**: *«0: reloj del periférico desactivado durante el
modo Sleep»*. Leída al pie de la letra, en Sleep mandaría **solo** el `LPENR`.
Como el valor de reset de los `LPENR` es «todo a uno», eso significa que al
dormirse el MCU se le **encienden** los relojes a periféricos que el firmware
nunca habilitó.

Se implementó primero así, literalmente. La consecuencia apareció en el acto:
once comprobaciones de fases anteriores —del tipo «este periférico sin su bit de
`ENR` da error de bus»— empezaron a fallar, porque el banco aparca el núcleo con
un `WFE` y **la suite entera corre con el MCU en Sleep**. Es decir: la lectura
literal es observable, y lo es de una forma que nadie esperaría.

**Decisión: el gating en Sleep es `ENR` AND `LPENR`.** Es la lectura física —el
gate de Run está aguas arriba en el árbol de reloj y el de Sleep lo estrecha
todavía más— y los dos criterios solo se diferencian en el caso `EN=0` y
`LPEN=1`, que en silicio únicamente se puede observar metiendo un maestro ajeno
a la CPU (el DMA, o el AHB-AP de una sonda) en un periférico que nadie habilitó.
Lo que NO cambia es para lo que sirven los `LPENR`, que sigue siendo medible
(§3.2). Queda escrito en `rcc.h`, junto al código, con las dos lecturas y el
motivo.

### 4.2 El gating no lo decide el modo, lo decide si hay reloj

Con `DBG_STOP` puesto, el MCU **está** en Stop y sin embargo el árbol de reloj
sigue girando. Si el gating mirase el modo arquitectónico, los periféricos se
quedarían sin reloj en un MCU que lo tiene, y ni siquiera se podría leer
`PWR_CSR` con la sonda enganchada. El criterio correcto es el físico: si los
relojes están parados, no hay nada que repartir; si están girando y el MCU está
dormido, se reparte con `ENR & LPENR`.

### 4.3 El valor de reset de `PWR_CR`

El informe se contradice consigo mismo: dice *«Reset: 0x0000 0000»* y a la vez
que `VOS[1:0]` vale `00` en reset y que `00: Reservado`. Un valor de reset que
la propia tabla marca como reservado no puede ser. El modelo mantiene
`0x0000 C000` (VOS = escala 1, el máximo rendimiento), que es lo que el
dispositivo hace al encenderse y lo que ya tenía el modelo desde F5. Queda
anotado aquí porque es una contradicción de la fuente, no una decisión libre.

### 4.4 Los umbrales del PVD

[IR, §14.6.1] remite al datasheet para la tabla de `PLS[2:0]` y no la reproduce.
⚠ **NO DISPONIBLE EN LAS FUENTES**: se usan los valores típicos del F405/407
(2,0 a 2,9 V) como parámetros públicos, con 100 mV de histéresis, igual que se
hizo con los umbrales de BOR en F3.

## 5. Cinco cosas que el modelo hacía mal y que solo se vieron al dormirlo

Esta es la parte útil del informe. Ninguno de estos cinco fallos era visible
antes de F7, y los cinco eran fallos de verdad.

1. **El núcleo no distinguía WFI de WFE.** El bucle de sueño despertaba con el
   registro de evento estuviera como estuviera. Como ese registro **no se limpia
   solo**, el primer evento del sistema dejaba al núcleo incapaz de volver a
   dormirse: despertaba, ejecutaba dos instrucciones, se dormía y volvía a
   despertar, para siempre. Arreglado guardando **cómo** se durmió: al WFI lo
   despierta una excepción; al WFE, además, un evento, y al despertar lo
   consume.

2. **`SCR.SLEEPDEEP` llegaba tarde.** El núcleo lo leía por una señal, y una
   señal no se propaga hasta el siguiente delta. Un firmware que escribe
   `SCB->SCR` y ejecuta el `WFE` unas instrucciones después —o sea, todos— caía
   en Sleep en lugar de en Stop, porque entre las dos cosas no había avanzado el
   tiempo simulado. Ahora el núcleo lee el SCB directamente, que es lo que hace
   el silicio: el SCB es suyo. **Este fallo solo lo encontró el firmware real**;
   las pruebas escritas a mano, que sí dejaban pasar tiempo entre una cosa y
   otra, no lo veían.

3. **Un núcleo en reset se anunciaba dormido.** `sleeping` y `sleepdeep` se
   quedaban colgados a uno durante todo el reset. En un reset normal daba igual;
   en el de salida de Standby, que dura cientos de microsegundos, el PWR creía
   que el núcleo acababa de ejecutar otro WFI y **volvía a dormir el MCU antes
   de que ejecutara su primera instrucción**.

4. **Se volvía a entrar en Stop en el mismo instante de salir.** Al despertar,
   el sistema arranca los relojes pero el núcleo sigue en su WFE hasta que le
   llega el reloj y ve su despertador. Durante esa ventana `sleeping` y
   `sleepdeep` siguen a uno, y la máquina de energía volvía a meterlo en Stop
   inmediatamente. Ahora, después de salir, se espera a que el núcleo despierte
   de verdad; y si no despierta —un WFI al que solo llegó un evento— el sistema
   se queda en Run, que es exactamente lo que hace el silicio.

5. **Escribir un `LPENR` no repartía relojes.** El registro se guardaba y no
   pasaba nada más, porque hasta F7 no gobernaba nada. Sin esto, la palanca de
   §3.2 no habría movido ni un microamperio.

Y uno más, del banco y no del modelo, que merece mención porque es la clase de
error que se paga caro: **soltar un pin no lo pone a cero**. Un `AnalogNet` sin
ningún driver conserva su última tensión —no se inventa un cero, que es la
decisión correcta desde F1—, así que `set(false)` seguido de `release()` en el
mismo delta dejaba el pin **alto**, y el flanco siguiente no existía. Dos
pruebas enteras estuvieron fallando por eso, y el síntoma —«el EXTI no despierta
al MCU»— apuntaba a cualquier sitio menos al culpable.

## 6. Verificación: 116 comprobaciones

### 6.1 T98 — El PWR (18)

Valores de reset y máscaras de escritura; `CWUF`/`CSBF` como órdenes que nunca
se leen puestas; qué bits de `PWR_CSR` son de escritura y cuáles banderas; `DBP`
como llave del dominio de backup. Y el **PVD contra el nivel real de VDD**: se
baja la fuente del banco a 2,5 V y el flag se pone y la línea EXTI16 se levanta;
se sube a 2,75 V y **no** se suelta (histéresis); se sube a 3,3 V y se suelta;
se cambia el umbral y los mismos 2,5 V ya no disparan nada. Más el regulador de
backup, que no está listo por pedirlo: `BRE` no es `BRR`.

### 6.2 T99-T101 — Los tres modos (49)

**Sleep**: el MCU de la suite está dormido *ahora mismo* (el aparcamiento hace
`WFE`), los relojes siguen, y quitarle el `LPEN` a un periférico habilitado le
quita el reloj —y sus registros dejan de responder— mientras se duerme. Un
evento del EXTI lo despierta; con la línea enmascarada, el mismo flanco ya no.

**Stop**: entra, y entonces `HCLK` y `SYSCLK` valen **cero**, `HSEON` y `PLLON`
están apagados, `HSION` puesto, ningún periférico tiene reloj y la SRAM
conserva lo suyo. Sale con una línea EXTI, y **vuelve a 16 MHz**. Con
`LPDS`+`FPDS` entra igual y tarda más en volver, porque el regulador y la Flash
estaban dormidos.

**Standby**: con `WUF` puesto **no se entra** —por eso la secuencia canónica
escribe `CWUF` justo antes—; con `WUF` limpio se apaga el dominio, los pines
quedan en alta impedancia, la SRAM se borra, la BKPSRAM no, el consumo cae a
microamperios y el sistema queda en reset. Un flanco en WKUP lo resucita, la
salida es un reset, `SBF` y `WUF` sobreviven y lo cuentan, y el RCC ha vuelto a
sus valores de reset.

### 6.3 T102 — El consumo (15) y T103 — Firmware real (21)

T102 mide lo de §3. T103 es la prueba que más vale: un binario compilado con
CMSIS —el mismo que se grabaría en la placa— hace la secuencia entera él solo
mientras el banco se limita a apretar PA0 cuando toca:

```
    en Sleep quedan 3 relojes de periferico abiertos
    el firmware midio 16000000 Hz al salir de Stop
```

El firmware limpia sus `LPENR` antes de dormir, se duerme con `WFE`, cae en Stop
con el regulador en bajo consumo, **mide su propio reloj al volver** y descubre
los 16 MHz, reprograma el PLL, y se apaga en Standby dejando su contador en la
BKPSRAM. Al volver, lo primero que hace es mirar `SBF` —y para poder mirarlo,
reactivar `PWREN`— y comprobar que su marca en la SRAM ya no está. La cuenta de
arranques va por 2; la marca, a cero.

### 6.4 T104 — Depurar lo que duerme (13)

Un modo de bajo consumo y una sonda enganchada son enemigos naturales: en cuanto
el firmware ejecuta su primer `WFI` con `SLEEPDEEP`, el DAP deja de contestar y
el IDE dice que ha perdido el objetivo. Con `DBG_STOP` el MCU **entra igual** en
Stop —el firmware no nota la diferencia— pero los relojes siguen y el DAP
contesta. Se comprueban las dos cosas y también el precio:

```
    Stop: 420 uA sin sonda, 5.96 mA con DBG_STOP
```

Catorce veces más. Con `DBG_STANDBY`, el dominio de 1,2 V no se apaga, no hay
reset y la SRAM conserva su contenido, que es lo que hace depurable un modo que
por definición se lleva la memoria por delante.

## 7. Limitaciones, dichas claramente

- **Las corrientes son un modelo, no una medida.** Lineales con la frecuencia y
  proporcionales al número de relojes abiertos, sin temperatura, sin dependencia
  de VDD y sin distinguir un periférico de otro (un ADC convirtiendo gasta más
  que un GPIO parado). Sirve para comparar modos y para ver caídas de tensión;
  no sirve para certificar una autonomía.
- **Los tiempos de despertar son parámetros, no física.** 13 µs, 40 µs y 375 µs
  salen del datasheet, no de modelar el arranque del regulador. El del HSI, en
  cambio, sí sale del modelo del oscilador.
- **En el modelo, un evento del EXTI despierta también de un `WFI`.** En ARM,
  estrictamente, el registro de evento no despierta de un `WFI`; el sistema sí
  sale de Stop, pero el núcleo debería seguir dormido. La distinción está hecha
  a medias: el núcleo ya no despierta con el registro de evento, pero sí con el
  nivel de la entrada. Es una simplificación heredada, y está anotada.
- **El `VBAT` se modela pero no se ejercita.** La corriente por VBAT se calcula
  cuando falta VDD, pero la suite no tiene todavía una prueba de funcionamiento
  con solo la pila puesta.
- **La escala de tensión (`VOS`) afecta al consumo pero no a la frecuencia
  máxima.** El modelo no impide correr a 168 MHz con la escala 3, que el
  silicio no permite.

## 8. Estado

Fase F7, parte de bajo consumo: **cerrada**. **1501/1501 comprobaciones**, 0
fallos, `-Wall -Wextra -O2` limpio, 13 s de ejecución.

Queda pendiente de F7 lo que no es consumo: OTG, ETH, FSMC, DCMI y el afinado
de los tiempos de arbitraje de la matriz.
