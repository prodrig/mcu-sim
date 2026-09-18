# Fase F5 (parte I2C) — Bus de dos hilos, SMBus y PMBus

Informe de implementación de la **parte de I2C** de la fase F5 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la fase
F4 (`doc/stm32f4xx/stm32f407vg_fase4_dma.md`, `_uart.md`, `_tim.md` y `_exti_syscfg.md`) y
a la parte de SPI/I2S de esta misma fase (`doc/stm32f4xx/stm32f407vg_fase5_spi.md`).
Fuentes: `doc/stm32f4xx/informe_revisado.md` [IR] y `doc/stm32f4xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** los **tres** canales I2C del STM32F407VG —I2C1,
I2C2 e I2C3— [IR, §12.6], con el requisito explícito de **analizar sus
similitudes y diferencias** y de que, si las hubiera, el tipo de bloque se pueda
seleccionar por parámetros de plantilla o del constructor.

**Resultado:** el bloque está completo y verificado. El modelo compila sin avisos
con `-Wall -Wextra -O2` y la suite pasa **733 de 733 comprobaciones** (124 de F1
+ 12 de F2 + 80 de F3 + 343 de F4 + 99 de SPI/I2S + **75 nuevas de I2C**, código
de salida 0). La verificación no se queda en los registros: el MCU habla con una
**EEPROM 24Cxx conectada a los pines**, por un bus de colector abierto con sus
pull-up, en el que el cero gana por resistencia y no por decreto del modelo; dos
I2C del propio MCU se hablan entre sí por una pista de placa; y un firmware
compilado con el **CMSIS oficial de ARM y de ST** escribe y relee la EEPROM con
START repetido.

---

## 1. Similitudes y diferencias de los canales I2C

Esta es la parte que el encargo pide analizar, y el análisis tiene una respuesta
tajante que conviene decir antes que nada.

### 1.1 Las tres instancias son funcionalmente idénticas

A diferencia de lo que ocurre con los temporizadores (donde TIM1 y TIM8 tienen
generador de tiempo muerto y TIM6/TIM7 no tienen ni canales), con las USART
(donde USART1/6 llegan al doble de velocidad y las UART4/5 no tienen modo
síncrono ni SPI) o con el propio SPI (donde SPI1 va a 42 MHz y no tiene I2S,
mientras SPI2/3 van a 21 MHz y sí lo tienen), **los tres I2C del F407 son el
mismo bloque sin ninguna reducción**. Comparten, bit a bit:

* el **banco de registros** completo —`CR1`, `CR2`, `OAR1`, `OAR2`, `DR`, `SR1`,
  `SR2`, `CCR`, `TRISE`, `FLTR`— en los mismos offsets [IR, §12.6.3];
* las **velocidades**: modo estándar (Sm) hasta 100 kHz y modo rápido (Fm) hasta
  400 kHz, con las dos relaciones de ciclo de trabajo (2:1 y 16:9);
* el **direccionamiento** de 7 y de 10 bits, la **dirección dual** (`OAR2`,
  `ENDUAL`) y la **llamada general** (`ENGC`);
* **SMBus/PMBus** completo: `SMBUS`, `SMBTYPE`, `ENARP`, `ALERT`, el pin `SMBA`
  y el **PEC** (CRC-8 de polinomio 0x07) con `ENPEC`, `PEC` y `PECERR`;
* el **filtro analógico y el digital** (`FLTR` con `ANOFF` y `DNF[3:0]`), que es
  propio de la serie F4 y no existe en la F1;
* el **estiramiento del reloj** y su inhibición (`NOSTRETCH`);
* las **interrupciones** de evento y de error separadas (`ITEVTEN`, `ITBUFEN`,
  `ITERREN`) y las **peticiones de DMA** (`DMAEN`, `LAST`).

Los tres cuelgan además **del mismo bus (APB1) y del mismo reloj (PCLK1)**, de
modo que ni siquiera se diferencian en la frecuencia máxima de trabajo, como sí
pasaba entre SPI1 y SPI2/3.

### 1.2 En qué se diferencian: solo en la integración

| | I2C1 | I2C2 | I2C3 |
| :--- | :---: | :---: | :---: |
| Bus y reloj | APB1 / PCLK1 | APB1 / PCLK1 | APB1 / PCLK1 |
| Base [IR, §12.6.2] | 0x4000 5400 | 0x4000 5800 | 0x4000 5C00 |
| Habilitación | `APB1ENR.21` | `APB1ENR.22` | `APB1ENR.23` |
| IRQ de evento / error | 31 / 32 | 33 / 34 | **72 / 73** |
| Celdas de DMA1 (RX) | S0C1, S5C1 | S2C7, S3C7 | S2C3 |
| Celdas de DMA1 (TX) | S6C1, S7C1 | S7C7 | S4C3 |
| SCL en el LQFP100 | PB6, **PB8** | PB10 | PA8 |
| SDA en el LQFP100 | PB7, **PB9** | PB11 | PC9 |
| SMBA | PB5 | PB12 | PA9 |
| Función alternativa | AF4 | AF4 | AF4 |

Todo lo de esa tabla **vive en el netlist del top**, no en el modelo del
periférico: las bases y el decodificador en `top/stm32f407vg.h`, los vectores y
las celdas de DMA en `top/stm32f407vg_bind2.h`, y la tabla de funciones
alternativas en ese mismo fichero. El bloque en sí se instancia tres veces sin
un solo parámetro distinto:

```cpp
I2c i2c1{"i2c1", addr::I2C1_B}, i2c2{"i2c2", addr::I2C2_B}, i2c3{"i2c3", addr::I2C3_B};
```

Merece la pena señalar dos detalles del encapsulado, porque son diferencias
reales aunque no sean del bloque: I2C1 es el único con **pines alternativos**
(PB8/PB9 además de PB6/PB7), y I2C2 pierde en el LQFP100 los pines PF0/PF1 que
sí tiene en encapsulados mayores, de modo que en este dispositivo solo puede ir
por PB10/PB11.

### 1.3 Entonces, ¿por qué parametrizar el modelo?

Porque el encargo pide un diseño flexible *en caso de existir diferencias*, y
porque hay dos razones de peso aunque en este dispositivo no las haya:

1. **El bloque sí varía entre familias de STM32.** Hay derivados sin SMBus, sin
   dirección dual, sin filtro digital (`FLTR` no existe en la serie F1) o
   limitados a modo estándar. Un modelo con los ejes explícitos se reutiliza;
   uno que dé por hecho «I2C = I2C del F407» hay que reescribirlo.
2. **Convierte una suposición en una comprobación.** Decir «las tres son
   iguales» es una afirmación sobre el silicio. Con los rasgos explícitos, el
   banco de pruebas **lo comprueba por el bus**: T56 escribe unos a todos los
   registros de las tres instancias y compara los bits que quedan puestos
   (§4.1). Si alguna se apartara del molde, la comprobación fallaría en vez de
   pasar desapercibida.

Es la misma receta que se usó con `UsartCaps`, `TimCaps` y `SpiCaps`, de modo
que el proyecto tiene un único patrón de familia de periféricos.

---

## 2. Selección del tipo de I2C

`src/periph/i2c.h` define un `struct` de rasgos, dos instancias constantes y las
dos vías de selección.

```cpp
struct I2cCaps {
    bool smbus          = true;   // CR1.SMBUS/SMBTYPE/ENARP/ALERT y el pin SMBA
    bool pec            = true;   // CR1.ENPEC/PEC, SR2.PEC[15:8] y PECERR
    bool dual_addr      = true;   // OAR2 y ENDUAL
    bool general_call   = true;   // CR1.ENGC y SR2.GENCALL
    bool ten_bit        = true;   // OAR1.ADDMODE y el encabezado de 10 bits
    bool digital_filter = true;   // registro FLTR (ANOFF, DNF)
    bool dma            = true;   // CR2.DMAEN/LAST
    bool fast_mode      = true;   // CCR.F/S y DUTY (400 kHz)
    double max_scl_hz   = 400.0e3;
    const char* kind    = "I2C";
};
```

### 2.1 En tiempo de compilación (parámetro de plantilla)

```cpp
template <const I2cCaps& Caps> class I2cT : public I2cBase { ... };

using I2c      = I2cT<CAPS_I2C_FULL>;    // I2C1, I2C2 e I2C3 del F407
using I2cBasic = I2cT<CAPS_I2C_BASIC>;   // variante reducida de otra familia
```

Cada combinación de rasgos es un **tipo distinto**, así que una confusión entre
variantes es un error de compilación y no un fallo en simulación.

### 2.2 En tiempo de ejecución (parámetro del constructor)

```cpp
I2cBase b{"b", base, I2cCaps{ /* ... */ }};        // rasgos completos
I2cBase c{"c", base, /*smbus=*/false, /*f_max=*/100e3};   // atajo de dos ejes
```

Útil para barridos y para el banco de pruebas: T56 construye una instancia «a
medida» en tiempo de ejecución y comprueba que sus registros pierden exactamente
los bits que le faltan.

### 2.3 Cómo actúan los rasgos: máscaras de escritura

El mecanismo es el mismo que en el resto del proyecto y es lo que le da valor:
los rasgos **no** se consultan en el camino de datos, sino que definen la
**máscara de escritura de cada registro**. Un bit que la instancia no implementa
no se guarda, y por tanto **lee cero exactamente igual que un bit reservado del
silicio**:

```cpp
uint32_t cr1_mask() const {
    uint32_t m = (1u<<0)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<10)|(1u<<11);  // PE..POS
    m |= 1u << 15;                                                    // SWRST
    if (caps_.smbus)        m |= (1u<<1)|(1u<<3)|(1u<<4)|(1u<<13);
    if (caps_.pec)          m |= (1u<<5)|(1u<<12);
    if (caps_.general_call) m |= 1u<<6;
    return m;
}
```

Lo mismo con `CR2` (`DMAEN`/`LAST`), `OAR1` (`ADDMODE`), `OAR2` (existe o no),
`CCR` (`F/S` y `DUTY`), `FLTR` (registro entero) y `SR1` (qué banderas de error
son `rc_w0`). Así, la firma de bits de una instancia se puede leer desde el bus
y compararse, que es justo lo que hace T56.

---

## 3. El modelo

`src/periph/i2c.h` (≈930 líneas). El bloque tiene dos mitades que comparten
registros: un **maestro** que marca el ritmo con un hilo, y un **esclavo**
dirigido por los flancos del bus.

### 3.1 El bus es de colector abierto, y aquí lo es de verdad

El periférico **nunca fuerza un uno**. Sus señales de función alternativa siguen
el convenio `out = 0` tira de la línea, `out = 1` la suelta; el pad, con
`OTYPER = open-drain`, la deja en alta impedancia. El nivel alto lo dan las
resistencias de pull-up **de la placa** (`I2cWire`, 4,7 kΩ a 3,3 V), no el MCU.

La consecuencia es que el arbitraje y el estiramiento del reloj **no están
programados**: emergen de la física. Cuando dos participantes tiran a la vez,
gana el cero porque su resistencia de salida (30 Ω) es dos órdenes de magnitud
menor que la del pull-up, exactamente como en la placa. Si el firmware olvida
configurar open-drain, en el modelo aparece el mismo conflicto que en el
silicio, con su corriente medible en el pin.

Para unir varios pines en un mismo nodo eléctrico hizo falta una operación nueva
en la interfaz analógica:

```cpp
virtual float voltage_excluding(int id, bool& floating_out) const = 0;
```

Devuelve la tensión que tendría el nodo **sin la aportación de un driver
concreto**. `I2cWire` la usa para decidir si alguien está tirando de verdad de
su línea antes de propagar el cero a los demás pines; sin ella, la propagación
se realimenta y el hilo se queda enganchado a cero para siempre. Es el
equivalente en el modelo a no confundir «la línea está baja» con «yo la estoy
bajando».

### 3.2 El maestro

Un `SC_THREAD` que baja al nivel de bit: `m_start`, `m_stop`, `m_bit_out`,
`m_bit_in`, `m_send_byte`, `m_recv_byte`. Genera START, START repetido y STOP,
direccionamiento de 7 y 10 bits, y la secuencia de banderas EV5–EV8 del manual.
El reloj sale de `CCR` y `TRISE` con las fórmulas de [IR, §12.6.3-D]:

* modo estándar: `f_SCL = f_PCLK1 / (2 · CCR)`
* modo rápido 2:1: `f_SCL = f_PCLK1 / (3 · CCR)`
* modo rápido 16:9: `f_SCL = f_PCLK1 / (25 · CCR)`

Cada vez que suelta SCL, el maestro **espera a que la línea suba de verdad**
(`release_scl_and_wait`): cualquier esclavo puede estar reteniéndola. Es el
punto exacto donde el estiramiento del reloj deja de ser una opción del modelo y
pasa a ser una consecuencia del bus.

### 3.3 El esclavo

Un `SC_METHOD` sensible a `scl_in` y `sda_in`. Detecta START y STOP (cambio de
SDA con SCL alto), compara la dirección recibida con `OAR1`, con `OAR2` si
`ENDUAL`, y con la llamada general si `ENGC`; transmite y recibe; y estira el
reloj mientras el firmware no atienda `ADDR` o no vacíe `DR`.

### 3.4 SMBus y PEC

El PEC es el CRC-8 de polinomio `x⁸+x²+x+1` acumulado sobre todos los bytes de
la transferencia, dirección incluida. `ENPEC` lo habilita, `PEC` (CR1.12) hace
que el siguiente byte transmitido sea el propio CRC, y `PECERR` marca el
desacuerdo en recepción. El pin `SMBA` levanta `SMBALERT` por nivel bajo.

### 3.5 Cinco errores de temporización que el modelo destapó

Los cinco son fallos clásicos de un I2C escrito a mano, y los cinco aparecieron
porque la verificación se hace **por los pines** contra una EEPROM y un maestro
externo, no comparando registros con lo que uno espera. Merecen quedar escritos
porque son el contenido técnico real de esta fase.

1. **El maestro no soltaba SDA tras su bit de reconocimiento.** El esclavo pone
   su siguiente bit en el mismo flanco de bajada; si el maestro sigue tirando de
   la línea, lo enmascara. Corregido con `m_release_sda()` al final de
   `m_recv_byte`.

2. **El bit de reconocimiento ocupa un flanco completo, no medio.** Tanto la
   EEPROM del banco como el esclavo del MCU pasaban del último bit de datos al
   estado de reconocimiento **en el flanco de bajada**, de modo que el flanco de
   subida en el que el maestro muestrea ese último bit se interpretaba como el
   ACK. El síntoma era precioso: se leía `DE 5B FF FF` donde había `DE AD BE EF`
   —`0x5B` es `0xAD` desplazado un bit con un uno metido por la derecha—. Se
   corrigió introduciendo un estado intermedio explícito (`SL_ACK_HOLD` en el
   modelo, `ACK_HOLD` en la EEPROM): se **pone** el ACK en la bajada, el maestro
   lo **muestrea** en la subida y solo entonces se **suelta** la línea.

3. **El estiramiento empezaba un pulso antes de tiempo.** El esclavo tiraba de
   SCL en cuanto reconocía su dirección, es decir, **en mitad del octavo bit**,
   truncando el pulso que el maestro todavía estaba usando. El manual es claro:
   `ADDR` se levanta *después* del noveno pulso y SCL se retiene a partir de
   ahí. Corregido dejando el estiramiento para el flanco de bajada posterior al
   ACK.

4. **El estiramiento sobrevivía a su causa.** La retención se pedía como una
   bandera pendiente; si el firmware borraba `ADDR` o leía `DR` antes de que
   llegara ese flanco, el esclavo retenía SCL por una condición ya atendida y
   **colgaba el bus para siempre**. Ahora la retención es una **condición viva**
   que se evalúa en el flanco: se estira si `ADDR` sigue puesto o si `DR` sigue
   lleno, y no por una petición guardada.

5. **La petición de STOP se perdía tras un fallo de reconocimiento.** Cuando
   nadie contestaba a la dirección, el firmware pedía STOP —que es lo que hace
   cualquier driver—, pero la fase de dirección del maestro no miraba ese bit:
   se quedaba esperando a que se escribiera `DR`. El STOP quedaba pendiente en
   `CR1` y **cerraba la transferencia siguiente** en cuanto empezaba. Es el peor
   tipo de fallo: no rompe la transacción que falla, sino la de después.

A esos cinco se suma una funcionalidad que faltaba y que el firmware con CMSIS
sacó a la luz: el **START repetido en mitad de la transferencia**. El modelo
solo lo atendía antes de enviar la primera dirección, pero el patrón obligado
para leer una EEPROM es escribir el puntero y girar a lectura **sin soltar el
bus**, con la dirección ya enviada. Ahora se atiende también ahí.

---

## 4. Verificación

75 comprobaciones nuevas, en cinco grupos, todas dentro de la suite acumulativa
de `src/top/sc_main.cpp`.

### 4.1 T56 — Las tres instancias y la variante

Escribe unos a todos los registros de I2C1, I2C2 e I2C3 y **compara la firma de
bits implementados**:

```
         CR1    CR2   OAR1   OAR2   CCR   TRISE  FLTR
  I2C1  0x3EFB 0x1F3F 0xC3FF 0x00FF 0xCFFF 0x003F 0x001F
  I2C2  0x3EFB 0x1F3F 0xC3FF 0x00FF 0xCFFF 0x003F 0x001F
  I2C3  0x3EFB 0x1F3F 0xC3FF 0x00FF 0xCFFF 0x003F 0x001F
```

La afirmación «las tres son idénticas» queda así comprobada por el bus y no
supuesta. A continuación construye una instancia reducida en tiempo de ejecución
y comprueba que pierde `SMBUS`, `ENPEC`, el modo rápido y `FLTR`, con lo que se
demuestra que los ejes son independientes y que la igualdad de las tres del F407
es un resultado, no una limitación del modelo.

### 4.2 T57 — Registros y generador de reloj

Valores de reset (incluido `TRISE = 0x0002`), error de bus sin `I2C1EN`, las tres
fórmulas de `f_SCL` medidas, la congelación del generador con `PE = 1`, el bit 14
de `OAR1` y `SWRST`.

### 4.3 T58 — Maestro contra una EEPROM, por los pines

El caso realista, y el que destapó los errores de §3.5. Sobre el bus físico con
pull-up: reposo en alto, escritura de puntero + cuatro bytes, relectura con START
repetido y comparación byte a byte, dirección sin dispositivo (AF), **una EEPROM
que estira el reloj 40 µs** en cada reconocimiento, y el mismo enlace repetido a
400 kHz.

### 4.4 T59 — El MCU como esclavo, y dos I2C del MCU en el mismo bus

Un maestro externo escribe tres bytes al MCU y se comprueban `ADDR`, `RXNE` y el
contenido; dirección dual con `DUALF`; llamada general con `GENCALL`. Después,
**I2C1 y I2C3 se hablan entre sí** por el hilo de placa, con I2C1 de maestro e
I2C3 de esclavo: es la comprobación de que el modelo es uno solo y funciona en
los dos extremos.

### 4.5 T60 — Arbitraje, errores y SMBus

`AF` y su borrado `rc_w0`, reacción al bus ocupado por otro maestro, `SMBUS` y
`ENPEC`, acumulación real del PEC sobre una transferencia (`0x62`), y `SMBALERT`
por el pin `SMBA`.

### 4.6 T61 — Interrupciones, DMA y firmware con CMSIS

IRQ 31 de evento; transmisión **por DMA** (DMA1, stream 6, canal 1 = I2C1_TX) de
cinco bytes que acaban en la EEPROM sin que la CPU toque `DR`; y un firmware
real en `verif/fw/i2c_demo/`, compilado con el CMSIS oficial:

```
PCLK1 = 42000000 Hz | CCR = 210 | bytes = 4 | escritura = 1 | lectura = 1
```

El firmware sube el reloj a 168 MHz con el PLL sobre el HSE, calcula él mismo
`CCR = PCLK1/(2·100 kHz) = 210` y `TRISE = 43`, configura PB6/PB7 en AF4 y
**open-drain**, escribe cuatro bytes en la EEPROM y los relee con START repetido.
Es código que se grabaría tal cual en una placa.

---

## 5. Ficheros

| Fichero | Estado | Contenido |
| :--- | :--- | :--- |
| `src/periph/i2c.h` | **nuevo** | El bloque I2C/SMBus parametrizado (≈930 líneas) |
| `src/common/analog_net.h` | ampliado | `voltage_excluding()` para nodos unidos |
| `src/verif/ext_parts.h` | ampliado | `I2cWire`, `I2cEeprom`, `I2cExtMaster`, `I2cPart` |
| `src/top/stm32f407vg.h` | ampliado | Las tres instancias y el decodificador APB1 |
| `src/top/stm32f407vg_bind2.h` | ampliado | AF4, vectores 31/32/33/34/72/73 y celdas de DMA |
| `src/top/sc_main.cpp` | ampliado | Grupos T56–T61 (75 comprobaciones) |
| `verif/fw/i2c_demo/` | **nuevo** | Firmware con CMSIS (`main.c`, `Makefile`) |

---

## 6. Trabajo pendiente de la fase F5

Quedan por modelar, en el orden previsto por el plan: **ADC**, **DAC**, **RTC**,
**bxCAN**, **SDIO**, **CRC/RNG** y los **perros guardianes** (IWDG y WWDG).

Del propio I2C queda una única cosa fuera: el **modo ARP de SMBus** (resolución
dinámica de direcciones) está reconocido en `CR1.ENARP` pero no se ejecuta su
protocolo, porque ningún dispositivo del banco de pruebas lo usa. La estructura
—dirección por defecto, `SMBDEFAULT`, `SMBHOST`— ya está.
