# Fase F4 (parte EXTI/SYSCFG) — Interrupciones externas y configuración del sistema

Informe de implementación de la **parte de EXTI y SYSCFG** de la fase F4 del
plan `doc/stm32f4xx/smt32f407vg_diseño.md` (§7), y **cierre de la fase F4**. Continúa a
`doc/stm32f4xx/stm32f407vg_fase4_dma.md` (controladores DMA),
`doc/stm32f4xx/stm32f407vg_fase4_uart.md` (interfaces serie) y
`doc/stm32f4xx/stm32f407vg_fase4_tim.md` (temporizadores). Fuentes:
`doc/refs/stm32f407xx/informe_revisado.md` [IR] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** el controlador de interrupciones externas
**EXTI** con sus 23 líneas [IR, §9.4] y el bloque de configuración del sistema
**SYSCFG** [IR, §12.21.2], que es quien decide qué pin llega a cada línea
[IR, §9.4.3]. Con él queda **completa la fase F4**.

**Resultado:** ambos bloques están completos y verificados. El modelo compila
sin avisos con `-Wall -Wextra -O2` y la suite pasa **559 de 559 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 71 del DMA + 70 de UART/USART + 118 de los
temporizadores + **84 nuevas de EXTI/SYSCFG**, código de salida 0, ~2,7 s de CPU
del anfitrión). La verificación empieza en el **pulsador real de la placa**:
PA0 con su pull-up interno, el pad eléctrico, el multiplexor de SYSCFG, el
detector de flanco y el vector del NVIC; y termina en un firmware compilado con
el **CMSIS oficial de ARM y de ST** que duerme en `WFI` entre pulsación y
pulsación.

---

## 1. Resumen ejecutivo

| Aspecto | Estado |
| :--- | :--- |
| EXTI — líneas | 23: 0-15 desde los pines (multiplexadas por SYSCFG), 16-22 internas [IR, §9.4.1] |
| EXTI — registros | IMR, EMR, RTSR, FTSR, SWIER, PR [IR, §9.4.2], con PR de tipo `rc_w1` |
| EXTI — detección | flanco de subida, de bajada o ambos, por línea, sobre la fuente seleccionada en cada instante |
| EXTI — caminos | interrupción (`PR & IMR` → NVIC) y evento (`EMR` → pulso al núcleo), **independientes** |
| EXTI — vectores | 6, 7, 8, 9, 10 (líneas 0-4), 23 (5-9), 40 (10-15) y los siete dedicados de las líneas internas [IR, §9.1.2] |
| EXTI — sistema | software (`SWIER`), salida de despertar hacia el PWR para el modo Stop [IR, §14] |
| SYSCFG | MEMRMP (espejo de 0x0), PMC (MII/RMII), EXTICR1-4 (multiplexor), CMPCR (celda de compensación) [IR, §12.21.2] |
| Núcleo | enganche del registro de evento con el pulso de la entrada de evento (`WFE`) |
| Verificación | 84 comprobaciones nuevas (T44-T48), 559 en total, 0 fallos, ~2,7 s |

---

## 2. Ficheros de la fase

### 2.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `verif/fw/exti_demo/main.c` | firmware CMSIS que usa los dos caminos del EXTI: evento (`WFE`) e interrupción (pulsador → LED) |
| `verif/fw/exti_demo/Makefile` | compilación con el startup y el `system_stm32f4xx.c` de ST |
| `doc/stm32f4xx/stm32f407vg_fase4_exti_syscfg.md` | este informe |

### 2.2 Reescritos o ampliados

| Fichero | Cambio |
| :--- | :--- |
| `periph/exti.h` | reescrito por completo: de un esqueleto de 60 líneas con `TODO(F4)` al modelo de 299 líneas |
| `core/cpu.h` | enganche del registro de evento con el flanco de `event_in` (véase §5) |
| `top/sc_main.cpp` | grupos T44-T48 y sus utilidades |
| `README.md` | fase F4 completada, cuenta de la suite y una convención nueva |

`periph/syscfg.h` **no ha necesitado cambios**: se implementó completo en F1
porque el espejo de 0x0000 0000 hacía falta para arrancar el núcleo. Este
entregable lo verifica de punta a punta por primera vez (T44), incluido su papel
como multiplexor del EXTI, que hasta ahora no tenía consumidor.

---

## 3. El modelo del EXTI

### 3.1 Dos caminos independientes

Lo que distingue al EXTI de un simple registro de banderas es que un mismo
flanco alimenta **dos caminos que no se tocan**:

```
                                       ┌── & IMR ──► PR ──► NVIC (interrupción)
pin/línea ──► detector de flanco ──┬───┤
                (RTSR / FTSR)      │   └── & EMR ──► generador de pulso ──► núcleo (evento)
                                   │
                                SWIER
```

Una línea desenmascarada **solo** en `EMR` despierta a la CPU de un `WFE` sin
entrar en ningún manejador y sin consumir un vector del NVIC — es el mecanismo
que usa un sistema de bajo consumo para reanudar un bucle sin el coste de una
excepción. El modelo los mantiene separados:

```cpp
void fire(uint32_t trig) {
    pr_ |= trig;                       // camino de interrupción
    if (trig & emr_) pulse_event();    // camino de evento
    update_out();
}
```

La prueba T47 lo comprueba en las dos direcciones: una línea con `EMR` y sin
`IMR` genera el pulso de evento **y deja la IRQ 6 en reposo**.

### 3.2 El detector es asíncrono

El EXTI del silicio no muestrea sus entradas con un reloj: detecta pulsos más
cortos que un ciclo de APB y sigue funcionando con los relojes parados, que es
precisamente lo que hace falta para salir del modo Stop. En el modelo esto sale
gratis y es fiel: el detector es un `SC_METHOD` sensible a las señales de pin,
no a un flanco de reloj.

Por el mismo motivo, el bloque **no tiene bit de habilitación en el RCC**. El
`SYSCFGEN` que suele aparecer en los drivers hace falta para escribir los
`EXTICR`, que están en SYSCFG, no en el EXTI; el top le ata `clk_en` a uno y la
prueba T45 comprueba que el EXTI responde sin ningún bit de `RCC_APB2ENR`.

Una consecuencia práctica: el `SC_METHOD` se declara **sin** `dont_initialize()`.
Su primera activación toma la foto de partida de los 23 niveles, y como `RTSR` y
`FTSR` valen cero tras el reset no puede generar ningún flanco espurio.

### 3.3 El multiplexor de SYSCFG, resuelto en cada evaluación

Las líneas 0-15 no están cableadas a un puerto: `SYSCFG_EXTICRx` elige cuál de
los nueve puertos llega a cada una [IR, §9.4.3]. El modelo lo resuelve en el
momento de leer la línea, no al configurarla:

```cpp
bool line_level(unsigned l) const {
    if (l < 16) {
        const unsigned p = unsigned(exticr_sel[l].read());
        if (p >= N_GPIO_PORTS) return false;     // 0x9..0xF: reservado
        return gpio_line[p * N_PORT_PINS + l].read();
    }
    ...
}
```

Un selector fuera de rango —los valores `1001`..`1111`, reservados en
[IR, §9.4.3]— no conecta nada y la línea se queda a cero, que es lo que hace un
multiplexor sin entrada seleccionada. T44 lo comprueba escribiendo `0xF` en el
campo de la línea 12.

**Decisión de diseño.** Cambiar `EXTICR` cambia la *fuente* de la línea, no su
valor. Si los dos puertos están a niveles distintos, un modelo ingenuo generaría
un flanco al reconfigurar y dejaría una petición pendiente que el firmware no ha
pedido. El modelo **resincroniza el nivel sin generar flanco**:

```cpp
bool resync = !sel_init_;
if (l < 16) {
    const uint8_t s = exticr_sel[l].read();
    if (s != sel_[l]) { sel_[l] = s; resync = true; }
}
const bool lv = line_level(l);
if (lv != lvl_[l]) {
    if (!resync) { /* aquí sí se evalúan RTSR y FTSR */ }
    lvl_[l] = lv;
}
```

Es una decisión deliberada y no está dictada por [IR], que no dice nada del
caso. El silicio real puede producir ese pulso, y por eso los drivers de ST
configuran `EXTICR` **antes** de habilitar `IMR`; el modelo elige el
comportamiento que no sorprende al firmware correcto y lo deja documentado aquí
y en el propio fichero. La prueba T46 comprueba explícitamente que reconfigurar
el multiplexor no deja peticiones espurias.

### 3.4 `PR` y `SWIER`: dos fuentes con reglas distintas

Aquí [IR] deja un margen que conviene explicitar, porque afecta a lo que ve el
firmware:

* **Flanco de hardware.** [IR, §9.4.2] describe `PR` como el registro que se
  activa "cuando llega el flanco seleccionado", sin mencionar máscaras. El
  modelo levanta `PR` **aunque la línea esté enmascarada en `IMR`**; lo que la
  máscara decide es si la petición llega al NVIC. Es el comportamiento que
  explica los bits pendientes «heredados» con los que se encuentra un driver que
  habilita `RTSR` antes que `IMR`, y la prueba T46 lo fija explícitamente.
* **Software.** La descripción del bit `SWIER` en el manual de referencia sí
  condiciona el efecto: escribir un uno levanta `PR` **si la línea está
  desenmascarada**. El modelo lo sigue, extendiéndolo al camino de evento
  (`IMR` o `EMR`), y T45 comprueba las dos ramas.
* **Borrado.** `PR` es `rc_w1`: escribir un uno borra, escribir un cero no hace
  nada. Y borrar `PR` borra también el bit de `SWIER` que produjo la petición,
  que es lo que permite volver a dispararla.

### 3.5 Agrupación de vectores

Las líneas 0 a 4 tienen vector propio (IRQ 6-10); las 5-9 comparten el 23 y las
10-15 el 40 [IR, §9.1.2]. La consecuencia importante para el firmware es que una
IRQ agrupada solo se retira cuando se han borrado **todas** las líneas activas
de su grupo:

```cpp
o_irq_[5] = (act & 0x000003E0u) != 0;            // líneas 5-9   -> IRQ 23
o_irq_[6] = (act & 0x0000FC00u) != 0;            // líneas 10-15 -> IRQ 40
```

T46 lo verifica levantando a la vez la línea 7 y la 12 y comprobando que cada
vector se libera por separado.

### 3.6 Las siete líneas internas

Las líneas 16-22 no vienen de pines sino de otros bloques —PVD, RTC (alarma,
tamper/timestamp y wakeup), OTG FS, OTG HS y Ethernet— y cada una tiene su
propio vector [IR, §9.4.1]:

| Línea | Fuente | Vector | Estado de la fuente |
| :---: | :--- | :---: | :--- |
| 16 | PVD (detector de tensión del PWR) | IRQ 1 | PWR es F7 |
| 17 | RTC Alarm | IRQ 41 | RTC es F5 |
| 18 | USB OTG FS Wakeup | IRQ 42 | OTG es F7 |
| 19 | Ethernet Wakeup | IRQ 62 | ETH es F7 |
| 20 | USB OTG HS Wakeup | IRQ 76 | OTG es F7 |
| 21 | RTC Tamper / Timestamp | IRQ 2 | RTC es F5 |
| 22 | RTC Wakeup | IRQ 3 | RTC es F5 |

El EXTI está completo para las siete: entradas, enmascaramiento, detección y
encaminamiento al vector. Lo que todavía no existe son los **bloques que las
generan**, que pertenecen a fases posteriores; el netlist ya los tiene cableados
(`s_pvd_line`, `s_rtc_l17`, `s_rtc_l21`, `s_rtc_l22`, `s_fswk_l18`,
`s_hswk_l20`, `s_ethwk_l19`). T47 verifica el camino completo de las siete
usando `SWIER`, que es exactamente para lo que existe, y comprueba que cada una
llega a su vector dedicado y a ningún otro.

---

## 4. El modelo de SYSCFG

SYSCFG ya estaba implementado desde F1 —el espejo de 0x0000 0000 es condición
para que el núcleo arranque—, y este entregable lo verifica entero:

* **MEMRMP.** Solo `MEM_MODE[1:0]`. Su valor de reset **no es una constante**:
  refleja los pines BOOT muestreados tras el reset [IR, §2.3], y se publica al
  router del núcleo, que resuelve el espejo. El aliasing en sí se verifica en
  T14 desde F1; T44 comprueba el registro, su máscara y que `SYSCFGRST` lo
  devuelve al valor de los pines BOOT.
* **PMC.** Solo `MII_RMII_SEL` (bit 23) es escribible; alimenta directamente la
  entrada de selección de interfaz del ETH_MAC.
* **EXTICR1-4.** Cuatro campos de cuatro bits por registro, dieciséis
  selectores publicados como puertos hacia el EXTI.
* **CMPCR.** `CMP_PD` enciende la celda de compensación de E/S y el hardware
  levanta `READY`, que es de solo lectura; apagarla lo retira.

La única corrección de esta fase ha sido de **verificación**, no de modelo: el
multiplexor de EXTICR llevaba desde F1 sin ningún consumidor que lo mirase.

---

## 5. Un fallo del modelo que ha destapado esta fase

El camino de evento del EXTI es un **pulso de un ciclo de bus**, no un nivel. El
núcleo lo consume por dos vías: la instrucción `WFE`, que mira el registro de
evento y el nivel de la entrada, y el bucle de sueño, que comprueba las
condiciones de despertar **cada microsegundo**. Con esas dos, un pulso de doce
nanosegundos se pierde siempre:

* si la CPU está despierta cuando llega, nadie lo recuerda y el `WFE` siguiente
  duerme para siempre;
* si está dormida, el sondeo de un microsegundo casi nunca coincide con él.

Faltaba lo que el arquitectura llama el *event register* (ARMv7-M B1.5.18): el
biestable que **engancha** el evento hasta que un `WFE` lo consume. El modelo lo
tenía declarado (`event_reg_`) pero solo lo escribía la instrucción `SEV`. La
corrección son tres líneas en `core/cpu.h`:

```cpp
SC_METHOD(event_latch_proc); sensitive << event_in.pos(); dont_initialize();
...
void event_latch_proc() { event_reg_ = true; }
```

Esto no es un detalle cosmético: sin él, el uso normal del EXTI en bajo consumo
—desenmascarar una línea solo en `EMR` y esperar en `WFE`— cuelga el firmware.
La prueba T48 lo cubre de la forma más honesta posible: el firmware **hace ese
`WFE` de verdad**, y si el pulso se pierde no vuelve nunca. Desactivando a
propósito el enganche, la prueba pasa de 8 comprobaciones correctas a 6 fallos y
el firmware se queda colgado antes incluso de configurar el pulsador — es decir,
el test tiene poder de detección real, no solo cobertura.

---

## 6. Verificación

Cinco grupos nuevos, 84 comprobaciones.

### 6.1 T44 — SYSCFG (19 comprobaciones)

Gating (`SYSCFGEN`), MEMRMP y su máscara, PMC y la señal de selección MII/RMII
que llega al ETH_MAC, CMPCR con su `READY` de solo lectura, los cuatro EXTICR
con sus dieciséis campos —incluido un selector reservado— y el efecto de
`SYSCFGRST`, que borra los EXTICR y devuelve MEMRMP al valor de los pines BOOT.

### 6.2 T45 — Registros del EXTI (19 comprobaciones)

Que el bloque responde sin bit de RCC propio; los valores de reset; que los
cuatro registros de máscara y flanco implementan exactamente 23 bits y los
31:23 son reservados; el comportamiento de `SWIER` con la línea enmascarada y
desenmascarada; y `PR` como `rc_w1`, incluido que borrarlo arrastra el bit de
`SWIER` y retira la interrupción.

### 6.3 T46 — Del pin al NVIC (24 comprobaciones)

El recorrido completo, empezando por el **pulsador real** de la placa
(`verif/ext_parts.h`) sobre PA0 con el pull-up interno del pad:

- pulsar genera el flanco de bajada, levanta `PR` y activa la **IRQ 6**;
- con solo `FTSR`, la subida no genera petición; con solo `RTSR`, la bajada
  tampoco; con las dos, ambas;
- el **multiplexor de SYSCFG manda**: apuntando la línea 0 al puerto B, PA0 deja
  de alcanzarla y PB0 —gobernado como salida GPIO— sí;
- reconfigurar el multiplexor no deja peticiones espurias (§3.3);
- la **agrupación de vectores**: PB7 activa el 23 y PD12 el 40, cada uno se
  libera por separado, y el LED verde de la placa se enciende con el mismo pin
  que dispara la línea 12;
- una línea enmascarada en `IMR` sigue registrando la petición en `PR` (§3.4).

### 6.4 T47 — Eventos y líneas internas (14 comprobaciones)

El camino de evento con `EMR` y sin `IMR`: se cuenta el pulso, se comprueba que
la IRQ **no** se activa y que la salida de despertar hacia el PWR sí; `SWIER`
sobre una línea de solo evento genera el pulso sin levantar `PR`; y las siete
líneas internas, cada una a su vector dedicado:

```
  [OK  ] linea interna 16 PVD           -> IRQ 1
  [OK  ] linea interna 17 RTC Alarm     -> IRQ 41
  [OK  ] linea interna 18 OTG FS Wakeup -> IRQ 42
  [OK  ] linea interna 19 ETH Wakeup    -> IRQ 62
  [OK  ] linea interna 20 OTG HS Wakeup -> IRQ 76
  [OK  ] linea interna 21 RTC Tamper    -> IRQ 2
  [OK  ] linea interna 22 RTC Wakeup    -> IRQ 3
```

### 6.5 T48 — Firmware con CMSIS (8 comprobaciones)

`verif/fw/exti_demo/main.c` se compila con la cabecera de dispositivo de ST y el
CMSIS-Core oficiales, sin ninguna adaptación al modelo, y usa los **dos
caminos**:

```c
/* 1. Camino de EVENTO: linea 1 solo en EMR (sin IMR) */
EXTI->IMR  &= ~(1u << 1);
EXTI->EMR  |=  (1u << 1);
EXTI->SWIER =  (1u << 1);
__WFE();                       /* vuelve por el evento, sin manejador */

/* 2. Camino de INTERRUPCION: PA0 -> linea 0 -> vector EXTI0 */
SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~0xFu) | 0x0u;   /* puerto A */
EXTI->FTSR |= (1u << 0);
EXTI->IMR  |= (1u << 0);
NVIC_EnableIRQ(EXTI0_IRQn);
```

El manejador borra `PR`, cuenta la pulsación y conmuta el LED de PD12; entre
pulsaciones la CPU espera en `WFI`. El banco de pruebas pulsa cuatro veces el
botón físico y comprueba el resultado:

```
pulsaciones = 4 | EXTICR1 = 0x0000 | PR en el manejador = 0x1 |
salidas de WFE = 1 | eventos = 1
el LED se ha encendido en 2 de las 4 pulsaciones
```

(dos de cuatro porque el manejador **conmuta** el LED en cada pulsación).

### 6.6 Resultado de la suite

```
Resumen F1: 124 comprobaciones OK, 0 fallos
Resumen F2: 12 comprobaciones OK, 0 fallos
Resumen F3: 80 comprobaciones OK, 0 fallos
Resumen F4 (DMA): 71 comprobaciones OK, 0 fallos
Resumen F4 (USART): 70 comprobaciones OK, 0 fallos
Resumen F4 (TIM)  : 118 comprobaciones OK, 0 fallos
Resumen F4 (EXTI) : 84 comprobaciones OK, 0 fallos
TOTAL     : 559 comprobaciones OK, 0 fallos
```

Sin regresiones en las fases anteriores. ~2,7 s de CPU del anfitrión.

---

## 7. Decisiones de diseño

1. **Caminos de interrupción y evento separados** (§3.1). La alternativa —tratar
   `EMR` como una segunda máscara del mismo camino— haría imposible el uso de
   bajo consumo, que es el motivo por el que existe.
2. **Detector asíncrono, sin reloj** (§3.2). Es lo fiel y además lo barato: el
   coste de simulación es proporcional a los cambios de pin.
3. **Reconfigurar `EXTICR` no genera flanco** (§3.3). Decisión deliberada, no
   dictada por [IR]; se documenta porque se aparta de lo que puede hacer el
   silicio.
4. **`PR` se levanta aunque la línea esté enmascarada; `SWIER` no** (§3.4). Cada
   regla sigue su propia fuente documental, y las dos están fijadas por pruebas.
5. **Las líneas internas se verifican con `SWIER`** (§3.6). Es lo que permite
   cerrar el EXTI ahora sin esperar a PWR, RTC, OTG y ETH; el camino desde esas
   fuentes ya está cableado en el netlist y se comprobará al implementarlas.
6. **El registro de evento se engancha en el núcleo, no se alarga el pulso en el
   EXTI** (§5). Alargar el pulso habría escondido el fallo detrás de un número
   mágico; el biestable es lo que dice la arquitectura.

---

## 8. Trabajo pendiente

De este bloque:

* **Filtro/antirrebote**: el EXTI real no filtra, y el modelo tampoco. Si el
  banco de pruebas llega a modelar rebotes mecánicos del pulsador, cada rebote
  producirá su interrupción, como en el sistema real.
* **Líneas internas con fuente real**: PVD (con PWR, F7), RTC (F5), OTG y ETH
  (F7). El EXTI no necesita cambios; solo que esos bloques escriban su señal.
* **Modo Stop de verdad**: la salida `wakeup` del EXTI llega al PWR, pero la
  máquina de estados Run/Sleep/Stop/Standby del PWR es F7. Hoy el efecto
  observable del despertar es el `WFE`/`WFI` del núcleo.
* **`SYSCFG_CMPCR`**: la celda de compensación se modela como lista de
  inmediato; su efecto eléctrico sobre los tiempos de conmutación del pad no
  está modelado.

Con este entregable **la fase F4 queda cerrada**. Lo siguiente en el plan es la
fase **F5**: el resto de periféricos (SPI/I2S, I2C, ADC, DAC, RTC, bxCAN, SDIO,
CRC/RNG y watchdogs).
