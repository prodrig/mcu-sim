# Fase F4 (parte UART/USART) — Interfaces serie

Informe de implementación de la **parte de UART y USART** de la fase F4 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a `doc/stm32f4xx/stm32f407vg_fase3.md` (pines,
GPIO y RCC eléctrico) y a `doc/stm32f4xx/stm32f407vg_fase4_dma.md` (controladores DMA).
Fuentes: `doc/refs/stm32f407xx/informe_revisado.md` [IR] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** las seis interfaces serie del STM32F407VG
—USART1, USART2, USART3, USART6, UART4 y UART5— [IR, §12.4], con el requisito
explícito de que **la variante UART o USART se seleccione por parámetros**, de
plantilla o de constructor. Los temporizadores avanzados y EXTI/SYSCFG quedan
para entregables posteriores de la misma fase.

**Resultado:** el bloque está completo y verificado. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **357 de 357 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 71 del DMA + **70 nuevas de UART/USART**,
código de salida 0, ~1,6 s de CPU del anfitrión). La verificación no se queda en
los registros: los dos puertos se comunican **por los pines**, a través de los
pads eléctricos y de una pista de placa modelada, y un firmware compilado con el
**CMSIS oficial de ARM y de ST** usa el mismo mini driver sobre una USART y
sobre una UART.

---

## 1. Cómo se selecciona el funcionamiento UART / USART

Esta es la parte que el encargo pide documentar con detalle.

### 1.1 La idea

Las seis interfaces son el **mismo bloque de diseño**. Lo que distingue a una
UART de una USART en el STM32F407VG no es un modo de operación, sino qué
funciones están presentes en el silicio [IR, §12.4.1]. El modelo lo representa
con una estructura de rasgos:

```cpp
struct UsartCaps {
    bool synchronous;   // modo síncrono maestro: pin CK, CR2.CLKEN/CPOL/CPHA/LBCL
    bool flow_control;  // CTS/RTS por hardware: CR3.CTSE/RTSE/CTSIE
    bool smartcard;     // CR3.SCEN/NACK y el tiempo de guarda de USART_GTPR
    bool irda;          // CR3.IREN/IRLP
    bool lin;           // CR2.LINEN/LBDL/LBDIE
    bool half_duplex;   // CR3.HDSEL
    const char* kind;   // etiqueta para avisos y trazas
};

inline constexpr UsartCaps CAPS_USART{true,  true,  true,  true, true, true, "USART"};
inline constexpr UsartCaps CAPS_UART {false, false, false, true, true, true, "UART"};
```

Obsérvese que **IrDA, LIN y medio dúplex están en las dos variantes**: las UART
del F407 sí los tienen. Lo que les falta es el modo síncrono, el control de
flujo por hardware y el modo Smartcard. Los rasgos son ejes independientes, no
un interruptor de dos posiciones; esa independencia es precisamente lo que hace
útil la parametrización, y la prueba T32 la comprueba construyendo una variante
mixta que no existe en este MCU.

### 1.2 Selección en tiempo de compilación (parámetro de plantilla)

```cpp
template <const UsartCaps& Caps>
class UsartT : public UsartBase {
public:
    UsartT(sc_core::sc_module_name nm, uint32_t base) : UsartBase(nm, base, Caps) {}
    static constexpr const UsartCaps& variant()      { return Caps; }
    static constexpr bool             is_synchronous() { return Caps.synchronous; }
};

using Usart = UsartT<CAPS_USART>;   // USART1, USART2, USART3, USART6
using Uart  = UsartT<CAPS_UART>;    // UART4, UART5
```

El parámetro de plantilla es una **referencia a un objeto `constexpr` con
enlace externo** (C++17 lo permite gracias a `inline constexpr`). Con eso:

* `Usart` y `Uart` son **tipos distintos**, así que el netlist dice la verdad
  sobre lo que instancia y una confusión entre variantes es un error de
  compilación, no un fallo en simulación;
* los rasgos están disponibles como **constante de compilación**, de modo que
  se pueden comprobar con `static_assert` — el propio `usart.h` lleva dos:

```cpp
static_assert(Usart::is_synchronous(), "USART debe tener modo sincrono");
static_assert(!Uart::is_synchronous(), "UART no tiene modo sincrono");
```

Así queda el netlist del MCU (`top/stm32f407vg.h`):

```cpp
Usart usart1{"usart1", addr::USART1_B}, usart2{"usart2", addr::USART2_B};
Usart usart3{"usart3", addr::USART3_B};
// UART4/5 son la variante reducida: el tipo lo dice
Uart  uart4{"uart4", addr::UART4_B}, uart5{"uart5", addr::UART5_B};
Usart usart6{"usart6", addr::USART6_B};
```

Para una variante nueva basta declarar sus rasgos y usar el alias:

```cpp
inline constexpr UsartCaps CAPS_LPUART{false, true, false, false, false, true, "LPUART"};
using LpUart = UsartT<CAPS_LPUART>;
```

### 1.3 Selección en tiempo de ejecución (parámetro del constructor)

La implementación vive en `UsartBase`, que recibe los rasgos como argumento.
Es el mismo mecanismo, sin plantilla:

```cpp
UsartBase u{"u", base, UsartCaps{/*synchronous*/false, /*flow_control*/true,
                                 /*smartcard*/false, /*irda*/true,
                                 /*lin*/true, /*half_duplex*/true, "UART+CTS"}};
```

y hay un atajo de dos parámetros para el caso habitual:

```cpp
UsartBase v{"v", base, /*synchronous=*/false, /*flow_control=*/true};
```

Esto sirve para barrer variantes desde un banco de pruebas, para modelar un
derivado de la familia con otra combinación de funciones, o para construir la
instancia a partir de un fichero de configuración. El banco de pruebas de este
entregable crea precisamente una instancia así (T32).

### 1.4 Qué cambia realmente al cambiar la variante

La selección no es cosmética. Los rasgos determinan las **máscaras de escritura
de CR2 y CR3** y la existencia de GTPR, de modo que en una UART los campos que
el silicio marca como reservados no se escriben y leen cero:

| Campo | Registro | USART1/2/3/6 | UART4/5 |
| :--- | :--- | :---: | :---: |
| CLKEN, CPOL, CPHA, LBCL | CR2[11:8] | escribibles | reservados, leen 0 |
| STOP[1:0], ADD[3:0] | CR2 | escribibles | escribibles |
| LINEN, LBDIE, LBDL | CR2 | escribibles | escribibles |
| CTSIE, CTSE, RTSE | CR3[10:8] | escribibles | reservados, leen 0 |
| SCEN, NACK | CR3[5:4] | escribibles | reservados, leen 0 |
| IREN, IRLP, HDSEL, DMAT, DMAR, ONEBIT, EIE | CR3 | escribibles | escribibles |
| Guard time / prescaler | GTPR | existe | reservado, lee 0 |

Y determinan el comportamiento: el pin CK no se gobierna nunca en una UART, y
`CTSE`/`RTSE` no pueden bloquear ni activar nada porque no se pueden poner a 1.
El firmware, por tanto, distingue las dos variantes **exactamente igual que en
el silicio**: escribiendo y releyendo.

Resultado real de la prueba T32:

```
USART2_CR2 = 0x7F6F | UART4_CR2 = 0x706F
USART2_CR3 = 0x0FFF | UART4_CR3 = 0x08CF
variante en ejecucion (UART+CTS): CR2 = 0x706F, CR3 = 0x0FCF
```

---

## 2. Resumen ejecutivo

| Bloque | Estado |
| :--- | :--- |
| Selección de variante | Por plantilla y por constructor (§1), con efecto observable en CR2/CR3/GTPR |
| Banco de registros | SR, DR, BRR, CR1, CR2, CR3, GTPR con máscaras y valores de reset [IR, §12.4.3] |
| Generador de baudios | Divisor fraccionario con sobremuestreo x16 y x8; sigue a PCLK sin intervención del firmware |
| Transmisor | Nivel de bit sobre el pin: arranque, 8/9 datos, paridad, 0.5/1/1.5/2 parada; TXE, TC, break (SBK), tiempo de guarda de Smartcard |
| Receptor | Detección del arranque, muestreo triple en el centro del bit, RXNE, IDLE, y los errores PE, FE, NF y ORE |
| Control de flujo | CTS bloquea el transmisor y activa la bandera; RTS refleja si el receptor puede aceptar dato |
| Medio dúplex | HDSEL: el pin se libera cuando no se transmite |
| Interrupciones | TXEIE, TCIE, RXNEIE, IDLEIE, PEIE, LBDIE, CTSIE y EIE, en una única línea por periférico |
| DMA | CR3.DMAT y CR3.DMAR, con petición de nivel al controlador de DMA |
| Funciones alternativas | 22 registros en el mux de pines: TX/RX de los seis puertos con sus opciones de pin, y CK/CTS/RTS de las USART |
| Pendiente | Modulación IrDA en el pin, reintento por NACK de Smartcard, modo mute con despertar por dirección |

Código nuevo: **544 líneas** de modelo (`periph/usart.h`), **≈330 líneas** de
verificación y un firmware de demostración de 155 líneas.

---

## 3. Ficheros de la fase

### 3.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `src/verif/fw/uart_demo/main.c`, `Makefile` | Firmware con CMSIS que usa el mismo driver sobre una USART y sobre una UART |

### 3.2 Reescritos o ampliados

| Fichero | Cambios |
| :--- | :--- |
| `src/periph/usart.h` | Modelo completo y parametrizado (era un esqueleto de 40 líneas) |
| `src/verif/ext_parts.h` | Nueva pieza `SignalLink`: la pista de placa entre dos pines |
| `src/top/stm32f407vg.h` | UART4/5 pasan a ser del tipo `Uart` |
| `src/top/stm32f407vg_bind2.h` | Tabla de funciones alternativas de los seis puertos |
| `src/top/sc_main.cpp` | Grupos de verificación T32-T37 |

---

## 4. El modelo

### 4.1 Generador de baudios

La frecuencia de bit sale de la fórmula del manual [IR, §12.4.3-C]:

```
USARTDIV = mantisa + fracción / (8 × (2 − OVER8))
baudios  = f_PCLK / (8 × (2 − OVER8) × USARTDIV)
```

con la mantisa en `BRR[15:4]` y la fracción en `BRR[3:0]` (tres bits útiles con
sobremuestreo x8). El modelo lee la frecuencia del dominio **del puerto**
`clk_hz` que F3 añadió a `BusSlave`, no de una constante: cuando el firmware
cambia el preescalador de APB1, el baudrate cambia con él sin que nadie toque el
USART. T33 lo comprueba dividiendo PCLK1 por dos y viendo el baudrate caer a la
mitad.

Un detalle de implementación que merece nota: `recompute_baud()` lee el puerto
`clk_hz` directamente en lugar de `domain_hz()`, porque los dos procesos
sensibles a esa señal —el de `BusSlave` y el del USART— no tienen orden
garantizado entre sí.

### 4.2 Transmisor y receptor, bit a bit

No hay atajos: el transmisor **gobierna el pin** durante cada bit y el receptor
**muestrea el pin**. Un carácter viaja por el pad, por el nodo analógico, por la
pista de la placa y por el pad del otro extremo. Eso permite comprobar cosas
que un modelo de nivel de transacción no puede: el orden LSB primero, la
duración del bit medida en el pin, o que un pin reconfigurado como GPIO corta la
comunicación.

El receptor imita el muestreo real: tras el flanco de bajada del arranque se
sitúa en el centro del bit y toma **tres muestras** separadas 1/16 de bit, con
decisión por mayoría. Si las tres no coinciden levanta `NF`, que es exactamente
lo que hace el silicio y lo que `CR3.ONEBIT` desactiva.

Las banderas siguen la semántica de borrado real: `RXNE` se borra al leer `DR`,
y `PE`/`FE`/`NF`/`ORE`/`IDLE` solo se borran con la secuencia **leer SR y
después leer DR**, que es la fuente clásica de errores en los drivers y por
tanto algo que el modelo debe reproducir.

### 4.3 Congelar la configuración del marco

Un fallo del modelo que la verificación destapó (§6) llevó a una regla que
conviene enunciar: **el receptor muestrea la configuración del marco cuando
detecta el bit de arranque, no antes**. La longitud de palabra, la paridad y el
tiempo de bit se congelan en ese instante y no se vuelven a leer hasta el
siguiente marco. El transmisor hace lo mismo al empezar el suyo. Es lo que hace
el hardware —los registros de desplazamiento se cargan con la configuración
vigente al arrancar— y evita que una reprogramación hecha mientras el hilo está
bloqueado se aplique retroactivamente.

### 4.4 Control de flujo, medio dúplex y break

* **CTS**: con `CTSE`, el transmisor no empieza un marco mientras nCTS esté
  inactivo, y cualquier cambio de nivel levanta la bandera `CTS`.
* **RTS**: con `RTSE`, la salida indica si el receptor puede aceptar un dato.
* **HDSEL**: en medio dúplex el pin queda liberado (alta impedancia) cuando el
  transmisor está inactivo, para que el otro extremo pueda usar el mismo hilo.
* **SBK**: el carácter de break baja la línea durante todo el marco **incluido
  el bit de parada** (10 bits con M=0, 11 con M=1) y después envía el
  delimitador. Esa longitud es lo que hace que el receptor levante error de
  trama; con `LINEN`, además, `LBD`.

---

## 5. Verificación

Seis grupos nuevos, **70 comprobaciones**.

| Grupo | Contenido |
| :--- | :--- |
| **T32** | Selección de la variante: rasgos en tiempo de compilación, efecto sobre CR2/CR3/GTPR en USART2 frente a UART4, y una variante mixta creada en tiempo de ejecución |
| **T33** | Registros y baudios: valores de reset, BRR solo escribible con UE=0, sobremuestreo x16 y x8, divisor fraccionario para 115200, y seguimiento de PCLK1 |
| **T34** | El marco visto en el pin: un «analizador lógico» del banco de pruebas decodifica los 10 bits de un 0x55, mide la duración del bit y comprueba TXE y TC |
| **T35** | Enlace real entre puertos: 8N1, 9 bits con paridad, error de paridad, 2 bits de parada, break con FE y LBD, desbordamiento, línea en reposo, y el mismo enlace entre las dos UART |
| **T36** | Interrupción de recepción hacia el NVIC y transferencia **por DMA**: una cadena viaja de memoria a memoria pasando por USART2, un cable y USART3, servida por dos streams de DMA1 |
| **T37** | Firmware real con CMSIS (abajo) |

### 5.1 La pista de placa

Para que dos puertos se comuniquen hace falta el cable. `verif/ext_parts.h` gana
una pieza, `SignalLink`, que observa la tensión de un pin, la digitaliza con el
mismo criterio que un pad y la reproduce en otro con una impedancia de salida
pequeña. Es **unidireccional por construcción**, que es lo que corresponde a un
enlace serie full-duplex, donde cada hilo tiene un único emisor; un hilo
compartido de verdad (medio dúplex, bus open-drain) se modela conectando los dos
pines al mismo `AnalogNet`, no con esta pieza.

El banco de pruebas monta cuatro: PA2→PB11 y PB10→PA3 (USART2 ↔ USART3), y
PA0→PD2 y PC12→PA1 (UART4 ↔ UART5).

### 5.2 T36 — USART servido por el DMA

Esta prueba junta los dos entregables de la fase. USART2 transmite ocho bytes
que le entrega el **stream 6 canal 4 de DMA1**, viajan por el cable, y el
**stream 1 canal 4** recoge lo que recibe USART3 y lo deja en memoria. Ni la CPU
ni el banco de pruebas tocan `DR`: el camino completo —petición de nivel del
USART, mux de canal del DMA, puerto de periféricos contra APB1, puerto de
memoria contra la SRAM— funciona tal como está cableado desde F0.

### 5.3 T37 — Firmware con CMSIS

`verif/fw/uart_demo/main.c` se compila con el CMSIS de ARM y de ST vendorizado
en F3. Escribe un mini driver que **solo usa lo que las dos variantes
comparten** y lo aplica dos veces: a USART2→USART3 con recepción por
interrupción, y a UART4→UART5 por sondeo. Calcula el BRR con la misma fórmula
que el driver de ST a partir de `SystemCoreClock/4`.

```
PCLK1 = 42000000 Hz | BRR = 0x016C | IRQ de recepcion = 14 | 32230 instrucciones
```

El modelo genera 115385 baudios con ese BRR, un 0,16 % por encima de los 115200
nominales: exactamente el error de redondeo que tiene la placa real con esa
combinación de reloj y baudrate.

### 5.4 Resultado de la suite

```
Resumen F1:          124 comprobaciones OK, 0 fallos
Resumen F2:           12 comprobaciones OK, 0 fallos
Resumen F3:           80 comprobaciones OK, 0 fallos
Resumen F4 (DMA):     71 comprobaciones OK, 0 fallos
Resumen F4 (USART):   70 comprobaciones OK, 0 fallos
TOTAL            :   357 comprobaciones OK, 0 fallos
```

---

## 6. Fallos del modelo que ha destapado esta fase

| # | Síntoma | Causa y corrección |
| :--- | :--- | :--- |
| 1 | Tras reconfigurar el receptor de 9 bits con paridad a 8 bits sin paridad, el primer carácter llegaba con un bit de más y un break no producía error de trama | El receptor leía la longitud de palabra y la paridad **antes** de esperar el bit de arranque, así que un marco que llegaba poco después de una reconfiguración se decodificaba con los parámetros anteriores. Ahora la configuración se congela al detectar el arranque (§4.3) |
| 2 | El carácter de break no provocaba error de trama | El break duraba un bit menos de la cuenta: la línea volvía a subir justo a tiempo para que el receptor muestreara un bit de parada válido. Un break baja la línea **incluido** el bit de parada |
| 3 | Un «analizador lógico» del banco de pruebas leía 0xD5 en lugar de 0x55 | No es del modelo sino de la prueba, y merece constar: la escritura de `DR` por el bus consume tiempo simulado, y el bit de arranque salía antes de que el proceso de estímulo llegara a esperar el flanco. El decodificador se arma ahora **antes** de la escritura, con un proceso dinámico |

---

## 7. Decisiones de diseño

| # | Decisión | Motivo |
| :--- | :--- | :--- |
| **F4U-1** | Un único modelo con rasgos, en vez de dos clases o de un `if (is_uart)` repartido por el código | Las seis instancias son el mismo bloque; duplicar el código duplicaría los errores, y una bandera suelta no impide instanciar una UART y usarla como USART |
| **F4U-2** | Los rasgos son campos independientes y no un enumerado `UART`/`USART` | El eje «tiene CTS/RTS» y el eje «tiene modo síncrono» son distintos en la familia STM32; un enumerado obligaría a añadir un valor por cada combinación |
| **F4U-3** | La plantilla toma una **referencia** a los rasgos, no una copia | Deja los rasgos disponibles como constante de compilación (`static_assert`, `if constexpr`) y mantiene una sola definición por variante |
| **F4U-4** | La implementación está en la clase base no plantilla | Todo el código se compila una vez; la plantilla solo fija los rasgos y el tipo. Además permite manejar cualquier puerto serie como `UsartBase&` |
| **F4U-5** | La variante se manifiesta como máscaras de escritura de registro | Es la forma en que el silicio la manifiesta, así que el firmware la detecta igual en el modelo y en la placa |
| **F4U-6** | Transmisión y recepción a nivel de bit sobre el pin | Es coherente con el contrato eléctrico del proyecto y permite verificar el enlace de extremo a extremo, incluida la desconexión al reconfigurar el pin |
| **F4U-7** | La configuración del marco se congela al empezarlo | Evita que una reprogramación afecte retroactivamente a un marco en curso (§4.3) |
| **F4U-8** | `SignalLink` es unidireccional | Un enlace serie full-duplex tiene un emisor por hilo; un hilo compartido se modela con un nodo compartido, que ya existe |

---

## 8. Trabajo pendiente

**Del propio bloque serie:**

* **IrDA**: `IREN`/`IRLP` se aceptan y el marco se transmite, pero **no se
  modela la conformación de pulsos SIR** (un cero como pulso de 3/16 de bit).
  Hace falta cuando se quiera conectar un transceptor infrarrojo al pin.
* **Smartcard**: el tiempo de guarda de `GTPR` sí retrasa `TC`, pero falta el
  reintento automático ante `NACK` y el preescalador del reloj de tarjeta.
* **Modo mute y despertar por dirección** (`CR1.WAKE`/`RWU`, `CR2.ADD`): los
  bits existen, la máquina de silenciamiento no.
* **Modo síncrono**: `CLKEN`/`CPOL`/`CPHA`/`LBCL` se aceptan y el pin CK se
  habilita, pero el modelo aún no emite el reloj de datos; se completará junto
  con el SPI en F5, que comparte la temporización de la trama síncrona.

**Sobre el informe:** la tabla de `USART_CR1` de [IR, §12.4.3-D] lista un
subconjunto de los bits (omite TCIE, IDLEIE, PEIE, WAKE, RWU y SBK). El modelo
implementa el registro **completo** con la disposición estándar del bloque, que
es la que usan CMSIS y el firmware real; sin ella, el manejador de interrupción
más común no funcionaría.

**Del resto de la fase F4:** temporizadores avanzados y EXTI/SYSCFG, con el
registro de sus funciones alternativas en el multiplexor de pines.
