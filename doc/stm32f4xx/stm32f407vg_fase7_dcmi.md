# Fase F7 (parte de DCMI) — La interfaz de cámara, y los dos hilos que no existen

Segunda parte de la fase F7. Cubre **únicamente el DCMI** [IR, §12.22]: el
muestreo sobre PIXCLK con sus tres polaridades, el sincronismo por hardware y el
embebido, la ventana de recorte, los cuatro anchos de bus, el control de
cadencia, la instantánea, la FIFO con su desbordamiento y el camino completo
hasta la SRAM por DMA2. Lo que queda de F7 (OTG, ETH, FSMC y el afinado de la
matriz) no se toca aquí.

**Resultado:** la suite pasa **1562 de 1562 comprobaciones** (61 nuevas, grupos
T105 a T108), compilación limpia con `-Wall -Wextra -O2`, 13 s de ejecución. Y,
de propina, un hallazgo que no estaba en el encargo y que sale de la propia
tabla de pines del informe: **el DCMI del STM32F407VG no puede capturar 14 bits,
por mucho que su registro lo admita**.

---

## 1. El análisis pedido: ¿en qué se diferencian los "canales" del DCMI?

El encargo pide analizar las similitudes y diferencias de los distintos canales
y, si las hay, parametrizar. Aquí la respuesta empieza por una corrección:

> **El STM32F407 tiene UN SOLO DCMI.** No dos, como los bxCAN; ni tres, como los
> I2C; ni cinco, como el bloque SPI/I2S. Uno.

Así que la pregunta, tomada al pie de la letra, no tiene objeto: no hay dos
instancias que comparar. Pero tomada como lo que quiere decir —*¿qué varía en
este bloque, y cómo se parametriza?*— tiene tres respuestas, y las tres son
reales.

### 1.1 Los catorce canales de datos no son iguales entre sí

El bus del DCMI son catorce hilos, D0 a D13, y **no se usan todos nunca**: el
ancho se programa con `EDM[1:0]` y vale 8, 10, 12 o 14 bits. Los hilos por
encima del ancho elegido no se leen. Ya ahí hay una diferencia entre canales:
D0-D7 los usa cualquier configuración; D8-D13 solo algunas.

### 1.2 Y hay dos que, en este chip, no existen

Esta es la diferencia que importa, y la encontró la propia tabla de pines del
informe [IR, cap. 2]. Recorriendo las 100 filas del LQFP100 y buscando `DCMI_`
aparecen:

| Señal | Pines en el LQFP100 |
| :--- | :--- |
| D0 | PA9, PC6 |
| D1 | PA10, PC7 |
| D2 | PC8, PE0 |
| D3 | PC9, PE1 |
| D4 | PC11, PE4 |
| D5 | **PB6** (único) |
| D6 | PB8, PE5 |
| D7 | PB9, PE6 |
| D8 | **PC10** (único) |
| D9 | **PC12** (único) |
| D10 | **PB5** (único) |
| D11 | **PD2** (único) |
| HSYNC | **PA4** (único) |
| VSYNC | **PB7** (único) |
| PIXCLK | **PA6** (único) |
| **D12** | **ninguno** |
| **D13** | **ninguno** |

D12 y D13 solo salen por PF11/PG6 y PG7/PI0, y los puertos F, G e I **no
existen en este encapsulado**. La consecuencia es concreta y desagradable:

- `EDM = 11` se puede escribir, se lee de vuelta y el DCMI captura sin
  protestar;
- pero los dos bits más significativos de cada píxel valen siempre cero, porque
  no hay pin que los conduzca;
- de modo que un sensor de 14 bits conectado a un F407VG entrega imágenes a las
  que les falta el rango alto **sin que nada lo indique**. No hay bandera de
  error, no hay aviso, no hay síntoma salvo una imagen que no cuadra.

Por eso los rasgos del modelo separan dos cosas que se confunden con facilidad:

```cpp
unsigned max_edm    = 3;    // lo que deja programar el REGISTRO
unsigned lineas_pin = 12;   // lo que tiene soldado el ENCAPSULADO
```

y el modelo avisa por `SC_REPORT_WARNING` cuando se le piden más bits de los que
hay hilos. Es más de lo que hace el silicio.

### 1.3 Y el mismo bloque cambia entre miembros de la familia

Los ejes reales, que son los que se han parametrizado:

| Eje | Qué cambia |
| :--- | :--- |
| `max_edm` | 8, 10, 12 o 14 bits programables |
| `lineas_pin` | cuántos hilos hay de verdad (encapsulado) |
| `embedded_sync` | `ESS` + `DCMI_ESCR`/`ESUR` (BT.656) |
| `jpeg` | captura de un flujo comprimido |
| `crop` | `CROP` + `DCMI_CWSTRT`/`CWSIZE` |
| `frame_rate_ctrl` | `FCRC`: uno de cada 1, 2 o 4 cuadros |
| `snapshot` | `CM`: un cuadro y para |
| `byte_select` / `line_select` | `BSM`/`OEBS`/`LSM`/`OELS`: submuestreo en el propio periférico. **No están en el F407** [IR, §12.22.2 llega hasta `ENABLE`]; sí en los F4x9 y los F7 |
| `fifo_words` | profundidad de la FIFO (4 en el F407) |
| `dma` | petición hacia DMA2 |

## 2. Cómo se elige el tipo de DCMI

La misma receta que `UsartCaps`, `TimCaps`, `SpiCaps`, `I2cCaps`, `AdcCaps`,
`DacCaps`, `SdioCaps` y `CanCaps`: un `struct` `constexpr` de rasgos, unas
instancias con nombre, una clase base **no plantilla** que los toma **por
valor** y un alias de plantilla para fijarlos en compilación.

```cpp
// En tiempo de COMPILACIÓN:
using Dcmi     = DcmiT<CAPS_DCMI_F407>;   // 14 bits en el registro, 12 cableados
using DcmiFull = DcmiT<CAPS_DCMI_144>;    // el mismo silicio, encapsulado grande
using Dcmi8    = DcmiT<CAPS_DCMI_8BIT>;   // puerto simple de 8 bits
using DcmiBsm  = DcmiT<CAPS_DCMI_BSM>;    // con selección de byte y de línea

// En tiempo de EJECUCIÓN:
DcmiCaps c{};
c.max_edm = 0; c.lineas_pin = 8; c.crop = false; c.jpeg = false;
DcmiBase d{"dcmi_rt", c};
```

Los rasgos se aplican como **máscara de escritura** de cada registro, de modo
que un bit que la variante no implementa lee cero exactamente igual que un bit
reservado del silicio, y sus registros asociados se leen cero enteros. T105 no
comprueba eso leyendo una tabla: lo comprueba **escribiendo por el bus** en una
segunda instancia del mismo modelo, construida con otros rasgos y con su propio
maestro de pruebas.

## 3. Qué se ha modelado

```
   PIXCLK ──►┐ muestreo en el flanco que dice PCKPOL
   D[13:0] ──┤
   HSYNC  ──►│ ¿línea o borrado horizontal?   (HSPOL)
   VSYNC  ──►┘ ¿cuadro o borrado vertical?    (VSPOL)
                    │
                    ▼
              recorte (CROP) ──► empaquetado a 32 bits según EDM
                    │
                    ▼
            FIFO de 4 palabras ──► DCMI_DR ──► DMA2 (S1C1 o S7C1) ──► SRAM
```

| Fichero | Contenido |
| :--- | :--- |
| `src/periph/dcmi.h` | **Reescrito.** De 36 líneas de esqueleto a el bloque entero: rasgos, máquina de captura por flanco de PIXCLK, polaridades, recorte, cuatro anchos de bus, cadencia, instantánea, sincronismo embebido, FIFO con desbordamiento, interrupciones y petición de DMA |
| `src/verif/ext_parts.h` | **Nuevo:** `CameraSensor`, un sensor CMOS en los pines |
| `src/top/stm32f407vg.h` | Los diecisiete pines de AF13, uno por uno, según la tabla del informe |
| `src/top/sc_main.cpp` | Grupos T105-T108 (61 comprobaciones) y el sensor de la placa |

### 3.1 El sensor, en los pines

El DCMI no se puede probar sin la otra mitad. Y la otra mitad no es un
estímulo: es un **sensor de imagen soldado a diecisiete pines**, con su reloj,
sus sincronismos y su patrón reproducible, que hace lo que hace un OV7670:

- genera PIXCLK a la frecuencia que se le pida y **cambia los datos en el flanco
  contrario** al que muestrea el DCMI, que es como se cumple el tiempo de
  establecimiento;
- marca los bordes con VSYNC y HSYNC con las polaridades que se le configuren,
  o —si se le pide— **sin ellos**, metiendo los códigos BT.656 en el propio
  flujo de datos;
- emite un patrón que depende de la posición, de modo que el banco comprueba
  **píxel a píxel** lo que llegó a la memoria;
- y **no se puede parar**. Esa es su característica esencial: si el DCMI no
  vacía la FIFO a tiempo, los datos se pierden. Un sensor que esperase sería un
  modelo inútil, porque la mitad de los problemas de un interfaz de cámara son
  exactamente ese.

### 3.2 Una decisión de placa que hubo que tomar

En el LQFP100, `DCMI_VSYNC` sale **solo** por PB7 y `DCMI_D5` **solo** por PB6.
Esos dos pines son también `I2C1_SDA` e `I2C1_SCL`, y el banco tiene ahí un bus
I2C con su EEPROM desde F5. Un hilo de colector abierto y una salida push-pull
no se pueden compartir: el que tira a cero gana, y el sensor se queda mudo.

El modelo no lo esconde ni lo arregla por dentro: la prueba **levanta el bus
I2C** antes de soldar la cámara (`i2c_bus(false)`), que es exactamente la
decisión que se toma en una placa de verdad, con el soldador en la mano. Lo
mismo con los enlaces del SPI en PA4 y PA6.

## 4. Cinco cosas que costaron trabajo

1. **El detector de flancos hay que referenciarlo al encender.** Lo que el DCMI
   llama "borrado" no es un nivel: es *el nivel que digan `VSPOL` y `HSPOL`*.
   Al escribir `DCMI_CR` puede cambiar el **significado** del nivel que ya hay
   en el pin sin que el pin se mueva. La primera versión guardaba el nivel
   anterior y esperaba un flanco que nunca llegaba: **no capturaba nada, y no
   había ninguna bandera que lo dijera**. Ahora el registro de flanco se relee
   en cada escritura del `CR`, que es también lo que hace falta cuando se
   enciende el periférico con el sensor ya emitiendo.

2. **Un cuadro saltado no puede apagar la captura para siempre.** El control de
   cadencia (`FCRC`) decidía si capturar mirando "¿estaba capturando?". Con
   `FCRC = 01` capturaba el primer cuadro, saltaba el segundo… y ya no volvía a
   capturar nunca, porque tras el salto no estaba capturando. La condición
   correcta es el **bit `CAPTURE`**, que es lo único que el firmware controla.
   Se veía en la cuenta: cuatro cuadros emitidos, uno capturado en vez de dos.

3. **`CAPTURE` no captura: arma.** La captura empieza con el cuadro
   *siguiente*, nunca a mitad de uno [IR, §12.22.1]. Es lo que impide que la
   primera imagen salga cortada, y hay que modelarlo explícitamente porque la
   tentación es empezar a guardar en cuanto se escribe el bit.

4. **En modo instantánea es el HARDWARE quien limpia `CAPTURE`.** No el
   firmware. Y que lo haga el hardware es justo lo que permite al firmware saber
   que la foto ya está sin mirar el reloj.

5. **Los pines se comparten, y eso es parte del modelo.** Véase §3.2. Costó dos
   pruebas enteras, y el síntoma —"el sensor emite y el DCMI no ve nada"—
   apuntaba al periférico cuando el culpable era un pull-down de 20 Ω a tres
   módulos de distancia.

## 5. Verificación: 61 comprobaciones

### 5.1 T105 — Los rasgos (23)

Máscaras de escritura del `CR` en la variante del F407 (EDM llega a 11; `CROP`,
`JPEG`, `ESS` y `FCRC` existen; `BSM`/`LSM` no); los registros de ventana y de
códigos que existen porque la variante los tiene; y **la misma prueba sobre una
segunda instancia con rasgos de ejecución**, donde `EDM` no se guarda, `CROP`,
`JPEG` y `ESS` leen cero y `DCMI_CWSTRT`/`ESCR` se leen cero enteros. Más los
dos hilos que faltan: se programan 14 bits y `ancho_bits()` dice 14 mientras
`bits_utiles()` dice 12.

### 5.2 T106 — Captura por sincronismo de hardware (18)

Un cuadro de 16×8 a 8 bits, con el sensor emitiendo de verdad:

- llegan **32 palabras**, y cada uno de los 128 píxeles está donde y como lo
  puso el sensor, cuatro por palabra;
- `FRAME`, `LINE` y `VSYNC` se levantan donde deben, `DCMI_ICR` los limpia y,
  con `IER` puesto, la interrupción llega al NVIC por la IRQ 78;
- instantánea: el sensor manda **tres** cuadros y se captura **uno**, y es el
  hardware quien limpia `CAPTURE`;
- cadencia: el sensor manda **cuatro** y se capturan **dos** con `FCRC = 01`;
- polaridades: con las tres invertidas —sensor y DCMI de acuerdo— se captura
  igual y los píxeles son los mismos; con `VSPOL` al revés que el sensor **no se
  captura nada**, que es el fallo más común al estrenar un sensor.

### 5.3 T107 — Recorte, anchos y el cuadro entero por DMA (14)

- **Recorte:** de un cuadro de 16×8 se piden 8×4 desde (4, 2) y llegan 8
  palabras que son exactamente los píxeles de dentro de la ventana.
- **Anchos:** a 12 bits cada píxel ocupa **media palabra**, dos por palabra, con
  los cuatro bits altos a cero.
- **Los dos hilos que no existen, en funcionamiento:** se conecta un sensor de
  **14 bits** y se programa `EDM = 11`. La captura funciona, los contadores
  cuadran… y **los 32 píxeles llegan mutilados**, todos, con los bits 13:12
  siempre a cero. Ninguna bandera de error se levanta.
- **El cuadro entero por DMA2:** 32×16 píxeles, 128 palabras, stream 1 canal 1,
  destino SRAM1. `NDTR` llega a cero y en memoria están **los 512 bytes
  correctos, píxel a píxel**, sin que el núcleo haya intervenido.
- **El desbordamiento:** se captura sin que nadie vacíe la FIFO y los datos **se
  pierden**, con `OVR` levantado. Es la característica que define a este
  periférico.

### 5.4 T108 — Sincronismo embebido (6)

Sin mover HSYNC ni VSYNC: los cuatro códigos (`FSC`, `LSC`, `LEC`, `FEC`) viajan
en el flujo, se comparan con `DCMI_ESCR` a través de la máscara de `DCMI_ESUR`,
**no se guardan** entre los datos, y el código de fin de cuadro levanta `FRAME`
igual que lo haría VSYNC. Con un código fuera de secuencia —dos inicios seguidos
sin su fin— se levanta `ERR`, que es exactamente para lo que ese bit existe.

## 6. Limitaciones, dichas claramente

- **El modo JPEG está en los rasgos y en el registro, pero no ejercitado.** El
  bit `JPEG` se guarda y desactiva el recorte y el conteo de líneas; no hay
  ninguna prueba que meta un flujo JPEG real, porque el DCMI no interpreta el
  contenido y la prueba solo repetiría la del flujo continuo.
- **`BSM`/`LSM` se modelan como eje, no como función.** La variante
  `CAPS_DCMI_BSM` acepta esos bits; el submuestreo que implican no está
  implementado, porque el F407 no los tiene y el informe no los describe.
- **No hay firmware CMSIS de demostración**, a diferencia de las fases
  anteriores. El camino sensor → DCMI → DMA → SRAM se verifica desde el banco.
  Un `HAL_DCMI_Start_DMA` sobre el modelo sería el siguiente paso natural.
- **El sensor del banco no modela el tiempo de establecimiento** más allá de
  cambiar los datos en el flanco contrario: no hay `t_su`/`t_h` con márgenes,
  ni jitter en PIXCLK.
- **La FIFO se modela por palabras, no por bytes.** El silicio tiene cuatro
  palabras de 32 bits y el modelo también, pero un desbordamiento real puede
  perder parte de una palabra; aquí se pierde la palabra entera.

## 7. Estado

Fase F7, parte de DCMI: **cerrada**. **1562/1562 comprobaciones**, 0 fallos,
`-Wall -Wextra -O2` limpio, 13 s de ejecución.

Queda pendiente de F7: OTG, ETH, FSMC y el afinado de los tiempos de arbitraje
de la matriz.
