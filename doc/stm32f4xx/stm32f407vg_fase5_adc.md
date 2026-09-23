# Fase F5 (parte ADC) — Convertidores analógico-digitales

Informe de implementación de la **parte de ADC** de la fase F5 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la fase
F4 (`doc/stm32f4xx/stm32f407vg_fase4_dma.md`, `_uart.md`, `_tim.md` y `_exti_syscfg.md`) y
a las partes de SPI/I2S e I2C de esta misma fase
(`doc/stm32f4xx/stm32f407vg_fase5_spi.md`, `doc/stm32f4xx/stm32f407vg_fase5_i2c.md`).
Fuentes: `doc/refs/stm32f407xx/informe_revisado.md` [IR] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** los **tres** convertidores del STM32F407VG
—ADC1, ADC2 y ADC3— y su bloque de registros comunes [IR, §12.13], con el
requisito explícito de **analizar sus similitudes y diferencias** y de que el
tipo se seleccione por parámetros de plantilla o del constructor (número de
bits, número de canales, etc.).

**Resultado:** el bloque está completo y verificado. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **828 de 828 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S + 75 de I2C +
**95 nuevas de ADC**, código de salida 0). La verificación no se queda en los
registros: el convertidor **mide tensiones puestas de verdad en los pines**, con
sus nodos analógicos en float; se comprueba que olvidar el modo analógico
falsea la medida igual que en la placa; se cronometra la conversión y se
contrasta con SMPR y la resolución; y un firmware compilado con el **CMSIS
oficial de ARM y de ST** lee dos sensores y los pasa a milivoltios.

---

## 1. Similitudes y diferencias de los tres ADC

Esta es la parte que el encargo pide analizar, y aquí la respuesta es la
contraria a la del I2C: **los tres ADC del F407 NO son intercambiables**.

### 1.1 Lo que comparten

Los tres son **el mismo convertidor de aproximaciones sucesivas**. Comparten,
bit a bit:

* el **banco de registros** completo —`SR`, `CR1`, `CR2`, `SMPR1`, `SMPR2`,
  `JOFR1-4`, `HTR`, `LTR`, `SQR1-3`, `JSQR`, `JDR1-4`, `DR`— en los mismos
  offsets, con ADC1 en 0x4001 2000, ADC2 en +0x100 y ADC3 en +0x200
  [IR, §12.13-D];
* la **resolución** seleccionable de 12, 10, 8 y 6 bits (`CR1.RES`) y la
  **alineación** izquierda o derecha (`CR2.ALIGN`);
* el **tiempo de muestreo por canal** (`SMPR1`/`SMPR2`, de 3 a 480 ciclos de
  ADCCLK) y el mismo número de ciclos de aproximación;
* los **dos secuenciadores**: el regular de hasta 16 rangos (`SQR1-3`, con
  `SCAN`, `CONT`, `DISCEN`/`DISCNUM` y `EOCS`) y el inyectado de hasta 4
  (`JSQR`, con `JDISCEN`, `JAUTO` y los offsets `JOFRx`);
* el **perro guardián analógico** (`HTR`/`LTR`, `AWDEN`/`JAWDEN`/`AWDSGL`);
* las **banderas** `EOC`, `JEOC`, `STRT`, `JSTRT`, `AWD` y `OVR`, y las
  **peticiones de DMA**;
* el **reloj**: los tres cuelgan de APB2 y su ADCCLK sale del mismo prescalador
  común `ADCPRE`, con el mismo máximo de 36 MHz.

Un driver escrito para uno funciona en los demás **mientras se limite a lo
común**. El problema es que lo que queda fuera de «lo común» es justo lo que
más se usa.

### 1.2 En qué se diferencian (y no es solo integración)

| | ADC1 | ADC2 | ADC3 |
| :--- | :---: | :---: | :---: |
| Base [IR, §12.13-D] | 0x4001 2000 | 0x4001 2100 | 0x4001 2200 |
| Habilitación | `APB2ENR.8` (**el mismo bit para los tres**) | ← | ← |
| IRQ | **18, compartida por los tres** | ← | ← |
| Celdas de DMA2 | S0C0, S4C0 | S2C1, S3C1 | S0C2, S1C2 |
| Sensor de temperatura (IN16) | **sí** | no | no |
| VREFINT (IN17) | **sí** | no | no |
| VBAT/2 (IN18) | **sí** | no | no |
| Maestro de los modos dual/triple | **sí** | no (esclavo) | no (esclavo) |
| Canales externos en LQFP100 | 16 | 16 | **8** |

Las tres últimas filas son diferencias **del bloque**, no del netlist:

1. **Las entradas internas son exclusivas del ADC1.** El sensor de temperatura,
   la referencia interna VREFINT y el divisor VBAT/2 solo llegan al primer
   convertidor. Los bits que los conectan (`TSVREFE` y `VBATE`) viven en el
   registro **común**, lo que despista: parecen globales, pero lo que habilitan
   es un cableado que solo existe en el ADC1. En el modelo eso se ve desde el
   bus: `SMPR1` cubre los canales 10 a 18, y en el ADC1 tiene **27 bits
   implementados** mientras que en el ADC2 y el ADC3 se queda en **18**, porque
   sus canales 16, 17 y 18 no existen.

2. **El ADC1 es el maestro del modo múltiple.** En cuanto `CCR.MULTI` deja de
   ser cero, el ADC2 y el ADC3 **dejan de obedecer a su propio `SWSTART`** y
   los arranca el ADC1. Es una asimetría de papel, no de registros, y es la que
   más sorprende al depurar: el esclavo tiene todos sus bits y aun así no
   convierte cuando se le ordena.

3. **El ADC3 pierde la mitad de sus canales en este encapsulado.** Las entradas
   IN4-IN9 e IN14/IN15 del ADC3 van a pines del **puerto F**, que el LQFP100 no
   tiene [IR, §2.1]. En el ADC1 y el ADC2 esas mismas entradas están en
   PA4-PA7, PB0/PB1 y PC4/PC5. Así que la clasificación real de los canales es:

   * **ADC123**: IN0-IN3 (PA0-PA3) e IN10-IN13 (PC0-PC3) — llegan a los tres;
   * **ADC12**: IN4-IN7 (PA4-PA7), IN8/IN9 (PB0/PB1) e IN14/IN15 (PC4/PC5) —
     solo al ADC1 y al ADC2.

Y una particularidad de integración que merece señalarse porque afecta al
firmware: **los tres comparten el vector de interrupción 18 y el bit de reloj
`ADC1EN`**. El manejador tiene que leer el registro común `ADC_CSR` —que es una
copia de las banderas `SR` de los tres— para saber cuál de ellos ha pedido
atención. Es lo contrario de lo que pasa con las USART o los temporizadores, y
está comprobado en T65.

---

## 2. Selección del tipo de ADC

`src/periph/adc.h` define un `struct` de rasgos, tres instancias constantes y
las dos vías de selección que pide el encargo.

```cpp
struct AdcCaps {
    unsigned max_bits       = 12;    // resolución máxima del SAR
    bool     res_selectable = true;  // CR1.RES permite 12/10/8/6 bits
    unsigned n_ext_channels = 16;    // entradas externas IN0..IN(n-1)
    unsigned n_regular      = 16;    // rangos de la secuencia regular (SQR)
    unsigned n_injected     = 4;     // rangos de la secuencia inyectada (JSQR)
    bool     temp_sensor    = false; // IN16 (solo ADC1)
    bool     vrefint        = false; // IN17 (solo ADC1)
    bool     vbat           = false; // IN18 (solo ADC1)
    bool     injected       = true;  // grupo inyectado completo
    bool     watchdog       = true;  // perro guardián analógico
    bool     dma            = true;  // CR2.DMA/DDS y la petición
    bool     multi_master   = false; // gobierna CCR.MULTI (solo ADC1)
    double   max_adcclk_hz  = 36.0e6;
    const char* kind        = "ADC";
};
```

### 2.1 En tiempo de compilación (parámetros de plantilla)

El bloque entero es una plantilla con **un parámetro por convertidor**, de modo
que el tipo del ADC1 y el de sus dos esclavos se eligen por separado:

```cpp
template <const AdcCaps& C1, const AdcCaps& C2, const AdcCaps& C3>
class AdcBlockT : public AdcBlockBase { ... };

using AdcBlock = AdcBlockT<CAPS_ADC1, CAPS_ADC23, CAPS_ADC23>;   // el F407
```

Las combinaciones imposibles no compilan:

```cpp
static_assert(!C2.multi_master && !C3.multi_master,
              "en el modo multiple el maestro es el ADC1");
static_assert(C1.n_regular >= 1 && C1.n_regular <= 16, "SQR admite 1..16 rangos");
static_assert(C1.n_injected <= 4, "JSQR admite como mucho 4 rangos");
```

### 2.2 En tiempo de ejecución (parámetros del constructor)

```cpp
AdcBlockBase b{"b", CAPS_ADC1, CAPS_ADC23, CAPS_ADC_BASIC};
```

`CAPS_ADC_BASIC` es una variante reducida que **no existe en el F407** y que
sirve para demostrar que los ejes son independientes: 10 bits fijos (sin
`CR1.RES`), ocho canales, secuencia regular de ocho rangos, sin grupo
inyectado, sin perro guardián, sin DMA y con un ADCCLK máximo de 14 MHz. T62
la construye y comprueba desde el bus que ha perdido exactamente esos bits.

### 2.3 Cómo actúan los rasgos: máscaras de escritura

El mecanismo es el mismo que en el resto del proyecto: los rasgos **no** se
consultan en el camino de datos, sino que definen la **máscara de escritura de
cada registro**. Un bit que la instancia no implementa no se guarda y por tanto
**lee cero exactamente igual que un bit reservado del silicio**:

```cpp
static uint32_t cr1_mask(const Unit& x) {
    uint32_t m = (1u<<5)|(1u<<8)|(1u<<11)|(7u<<13);   // EOCIE, SCAN, DISCEN...
    if (x.caps.watchdog) m |= 0x1Fu | (1u<<6) | (1u<<9) | (1u<<23);
    if (x.caps.injected) { m |= (1u<<7)|(1u<<10)|(1u<<12);
                           if (x.caps.watchdog) m |= 1u<<22; }
    if (x.caps.res_selectable) m |= 3u << 24;
    if (x.caps.dma) m |= 1u << 26;
    return m;
}
```

Lo mismo con `CR2` (`DMA`/`DDS`, `JEXTSEL`/`JEXTEN`/`JSWSTART`), `SMPR1` (cuyo
ancho depende del número de canales, y es donde se ve la diferencia entre el
ADC1 y los otros dos), `SQR1` (el campo `L` se limita a `n_regular-1`), `JSQR`,
`HTR`/`LTR` y `SR` (qué banderas son `rc_w0`). El registro **común** se enmascara
con los rasgos del ADC1: `TSVREFE` y `VBATE` solo existen si el primer
convertidor tiene esas entradas, y `MULTI` solo si hay un maestro.

Un detalle que conviene subrayar: la **selección del número de bits** actúa en
tres sitios a la vez, no solo en el cuantificador —el fondo de escala
(`2^N - 1`), el número de ciclos de aproximación (12, 11, 9 o 7) y el
desplazamiento de la alineación a la izquierda (`16 - N`)—, y las tres cosas
están comprobadas en T63.

---

## 3. El modelo

`src/periph/adc.h` (≈870 líneas). Un solo `BusSlave` cubre el kilobyte que el
mapa de memoria asigna al conjunto (0x4001 2000-0x4001 23FF), decodifica el
convertidor por los bits altos del offset y hospeda tres máquinas de conversión
independientes, una por hilo, más el bloque común.

### 3.1 La entrada es analógica de verdad

Cada canal externo es el `AnalogNet` del pin, en float. El pad, en modo
analógico, se aparta del nodo: desconecta el buffer de entrada, el trigger de
Schmitt y las resistencias de pull [IR, §3.3.4]. El cuantificador es el de un
SAR:

```
    code = round( V_in / V_REF+ * (2^N - 1) ),   saturado a [0, 2^N - 1]
```

La consecuencia interesante es que **los errores de configuración de pin se ven
como se ven en la placa**. T64 lo comprueba: con PA1 puesto como entrada digital
con pull-up interno, una fuente de 100 kΩ que debería leerse como 1241 cuentas
se lee como 3280, porque el pull-up carga el nodo; volviendo el pin a modo
analógico, la misma fuente da las 1241 cuentas exactas. No hay ninguna regla en
el modelo que diga «si el pin no está en modo analógico, falla»: falla porque el
divisor resistivo del nodo lo resuelve así.

Un canal **no conectado** no es un canal a cero: es una entrada que no llega al
encapsulado. Se deja a `nullptr` y se distingue de un nodo flotante, que es lo
que le pasa al ADC3 con IN4-IN9 en el LQFP100.

Las entradas internas del ADC1 se modelan con sus valores del manual: el sensor
de temperatura como `V = 0,76 + (T - 25) · 2,5 mV/°C` (con la temperatura del
die expuesta al banco de pruebas por `set_die_temp`), VREFINT como 1,21 V y el
canal de batería como VBAT/2. Las tres exigen además su bit del registro común:
sin `TSVREFE` ni siquiera el ADC1 las ve.

### 3.2 El tiempo de conversión

`ADCCLK = PCLK2 / ADCPRE`, con `ADCPRE` de 2, 4, 6 u 8 y un máximo de 36 MHz
(por encima el modelo avisa y sigue convirtiendo, que es lo que hace el
silicio, con precisión degradada). Cada conversión cuesta:

```
    t_conv = ( ciclos_de_SMPR + ciclos_del_SAR ) / ADCCLK
```

con `ciclos_de_SMPR` en {3, 15, 28, 56, 84, 112, 144, 480} y `ciclos_del_SAR`
en {12, 11, 9, 7} según la resolución. La tensión se captura **al final de la
ventana de muestreo**, no al principio, que es lo que hace un condensador de
sample-and-hold real.

### 3.3 Los dos secuenciadores

El **regular** recorre `SQR3`/`SQR2`/`SQR1` hasta la longitud de `SQR1.L`, con
`SCAN` (secuencia completa o solo el primer rango), `CONT` (reinicio
automático), `DISCEN`/`DISCNUM` (un trozo por disparo) y `EOCS` (bandera por
conversión o por secuencia).

El **inyectado** tiene una trampa del manual que el modelo reproduce y el banco
comprueba: si `JL` es menor que cuatro, la secuencia **no empieza en `JSQ1`**
sino en `JSQ(4-JL)`, es decir, va alineada por la derecha. Los resultados, en
cambio, sí van a `JDR1`, `JDR2`... en orden, con el offset de `JOFRx` restado y
con signo. `JEOC` se levanta al terminar el **grupo**, no cada conversión.

### 3.4 Disparos, interrupción compartida y DMA

Los vectores de disparo del modelo están **indexados directamente por el valor
de `EXTSEL`/`JEXTSEL`**, de modo que el netlist del top conecta cada fuente en
su hueco de la tabla del manual y el modelo no necesita traducir nada. `EXTEN`
y `JEXTEN` eligen flanco de subida, de bajada o ambos.

La interrupción es **una sola para los tres** (vector 18). `update_irq()` hace
el OR de las cuatro condiciones habilitadas de los tres convertidores, y
`ADC_CSR` publica una copia de las tres `SR` para que el manejador sepa quién
ha sido.

La petición de DMA es de nivel, como en el resto del proyecto: se levanta al
terminar una conversión regular con `CR2.DMA` y se retira cuando alguien lee
`DR`. `DDS` decide si las peticiones siguen después del último dato de la
secuencia.

### 3.5 Modos dual y triple

Cuando `CCR.MULTI` no es cero, el hilo del ADC1 orquesta la conversión de todos
los participantes: **simultánea** (todos muestrean el mismo instante y se cobra
el tiempo del más lento) o **entrelazada** (cada uno arranca `DELAY` ciclos
después del anterior, que es de donde sale el aumento de cadencia). Cada esclavo
actualiza además su propio `DR` y sus banderas, y el resultado conjunto se
empaqueta en el registro común `CDR`: en modo dual los dos datos caben en una
palabra (ADC2 arriba, ADC1 abajo) y en triple hacen falta tres lecturas
sucesivas.

### 3.6 Un fallo del bus que destapó el ADC

El registro `CDR` es el primer registro del proyecto cuya **lectura tiene un
efecto lateral que no es idempotente**: cada lectura entrega un dato distinto.
Al probarlo en modo triple salieron valores imposibles —1900 donde debía haber
620—, y la causa no estaba en el ADC.

El `b_transport` común de `BusSlave` (`src/common/periph_base.h`) recorría la
lectura **byte a byte**, llamando a `reg_read()` una vez por cada byte del
acceso. Para un acceso de 32 bits eso son **cuatro llamadas**, y el dato final
se montaba con un byte de cada una: 0x076C resultó ser el byte 0 de la primera
lectura (620 = 0x26C) pegado al byte 1 de la segunda (1861 = 0x745). El camino
de **escritura** ya agrupaba por palabra —su comentario lo decía expresamente,
«una sola llamada `reg_write` por palabra tocada»— pero el de lectura nunca
recibió el mismo trato.

El fallo llevaba latente desde la fase F1 y no se había manifestado porque
hasta ahora **todos** los registros con efecto lateral de lectura eran
idempotentes: borrar `EOC` o `RXNE` cuatro veces da igual que borrarlo una. Se
ha corregido agrupando la lectura por palabras, igual que la escritura. Las 733
comprobaciones anteriores siguen pasando sin cambios.

---

## 4. Verificación

95 comprobaciones nuevas, en cinco grupos, dentro de la suite acumulativa de
`src/top/sc_main.cpp`.

### 4.1 T62 — Las tres instancias y la variante

Comprueba desde el bus lo que dice §1.2. La firma de bits implementados de
`SMPR1` es la prueba directa de que las entradas internas solo existen en el
ADC1:

```
         SMPR1    CR1        CR2        SQR1
  ADC1  0x7FFFFFF 0x07C0FFFF 0x3F3F0F03 0x00FFFFFF
  ADC2  0x003FFFF 0x07C0FFFF 0x3F3F0F03 0x00FFFFFF
  ADC3  0x003FFFF 0x07C0FFFF 0x3F3F0F03 0x00FFFFFF
```

`CR1` y `CR2` son idénticos en los tres —es el mismo bloque— pero `SMPR1` no.
Después construye la variante reducida en tiempo de ejecución y comprueba que
pierde `CR1.RES`, `JSQR`, `HTR`, `AWDCH` y los bits de DMA, y que su campo `L`
se queda en 8 rangos.

### 4.2 T63 — Registros, reloj de conversión y resolución

Error de bus sin `ADC1EN`, valores de reset (incluido `HTR = 0x0FFF`, para que
el perro guardián no ladre solo), las cuatro divisiones de `ADCPRE` medidas, y
el **cronometraje real de la conversión**: se mide con tres tiempos de muestreo
distintos y se contrastan las **diferencias** con la teoría, que es lo que
cancela el sobrecoste del sondeo desde el bus:

```
  SMP = 0 (  3 ciclos + 12): conversion medida en  2.59 us (teorico  1.88 us)
  SMP = 4 ( 84 ciclos + 12): conversion medida en 12.90 us (teorico 12.00 us)
  SMP = 7 (480 ciclos + 12): conversion medida en 62.40 us (teorico 61.50 us)
  de SMP=0 a SMP=7: +59.81 us medidos, +59.62 us teoricos
```

Y la resolución: a fondo de escala el código es `2^N - 1` para las cuatro, media
escala da la mitad, y `ALIGN` desplaza el dato `16-N` bits.

### 4.3 T64 — Convierte lo que hay en los pines

El grupo central. Una rampa de tensiones sobre PA1 con su código teórico al
lado:

```
   V(PA1)   codigo   esperado
   0.00 V       0        0
   0.40 V     496      496
   1.00 V    1241     1241
   1.65 V    2048     2048
   2.50 V    3102     3102
   3.30 V    4095     4095
```

Saturación por encima de VREF+; el efecto de olvidar el modo analógico (§3.1);
la **diferencia ADC123/ADC12 medida**, no supuesta —IN1 da lo mismo en el ADC1
y en el ADC3, mientras que IN4 lo miden el ADC1 y el ADC2 y en el ADC3 no llega
al encapsulado—; y las entradas internas del ADC1, incluida la pendiente del
sensor de temperatura obtenida de dos medidas a 25 °C y 85 °C (2,50 mV/°C).

### 4.4 T65 — Secuencias, inyectadas, watchdog y modo múltiple

Secuencia `SCAN` de tres canales en el orden correcto; `OVR` cuando nadie lee
`DR`; el grupo inyectado con la alineación por la derecha de `JSQR` y el offset
de `JOFR1`; el perro guardián dentro y fuera de la ventana, y con `AWDSGL`; la
**IRQ 18 compartida**, con `ADC_CSR` distinguiendo al ADC1 del ADC2; el disparo
por `TIM2_TRGO` (31 conversiones sin tocar `SWSTART`); y los modos dual y
triple, con el esclavo ignorando su propio `SWSTART` y `CDR` entregando los
datos empaquetados.

### 4.5 T66 — DMA y firmware con CMSIS

Barrido de cuatro canales volcado por **DMA2, stream 0, canal 0** sin que la CPU
toque `DR`, y un firmware real en `verif/fw/adc_demo/`:

```
PCLK2 = 84000000 Hz | IN1 = 2048 | IN2 = 410 | IN1 = 1650 mV | VREFINT = 1502
```

El firmware sube el reloj a 168 MHz con el PLL, programa `ADCPRE = /4`
(ADCCLK = 21 MHz), pone PA1 y PA2 en modo analógico, convierte los dos canales,
pasa el primero a milivoltios con la regla del manual y lee la referencia
interna por el canal 17. Es código que se grabaría tal cual en una placa.

---

## 5. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/adc.h` | **reescrito** | El bloque ADC completo y parametrizado (≈870 líneas; antes era el esqueleto de F1) |
| `src/common/periph_base.h` | corregido | Lectura de registros agrupada por palabra (§3.6) |
| `src/top/stm32f407vg_bind2.h` | ampliado | Canales ADC123/ADC12, VBAT, disparos indexados por EXTSEL/JEXTSEL |
| `src/top/sc_main.cpp` | ampliado | Grupos T62-T66 (95 comprobaciones) |
| `verif/fw/adc_demo/` | **nuevo** | Firmware con CMSIS (`main.c`, `Makefile`) |

---

## 6. Trabajo pendiente

Del propio ADC quedan fuera tres cosas, todas anotadas en el código:

* **Disparos por evento de captura/comparación.** La tabla `EXTSEL`/`JEXTSEL`
  incluye fuentes del tipo `TIMx_CHy` además de los `TRGO`. El modelo del
  temporizador no exporta el evento CC en crudo —solo su petición de DMA, que
  está condicionada por `DIER`—, así que esos huecos de la tabla quedan a cero.
  Los `TRGO` de TIM1 a TIM5 y TIM8 sí están conectados y verificados.
* **Disparo por EXTI11 (regular) y EXTI15 (inyectado).**
* **Modos múltiples de disparo alterno e inyectado simultáneo.** Están
  implementados el independiente y los regulares simultáneo y entrelazado, en
  sus versiones dual y triple.

De la fase F5 quedan por modelar, en el orden previsto por el plan: **DAC**,
**RTC**, **bxCAN**, **SDIO**, **CRC/RNG** y los **perros guardianes** (IWDG y
WWDG). El DAC es el siguiente natural, porque comparte con el ADC el lado
analógico de los pines y los disparos por temporizador.
