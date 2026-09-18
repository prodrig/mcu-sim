# Fase F5 (parte SPI/I2S) — Interfaces serie síncronas

Informe de implementación de la **parte de SPI e I2S** de la fase F5 del plan
`doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a los cuatro entregables de la fase
F4 (`doc/stm32f4xx/stm32f407vg_fase4_dma.md`, `_uart.md`, `_tim.md` y `_exti_syscfg.md`).
Fuentes: `doc/stm32f4xx/informe_revisado.md` [IR] y `doc/stm32f4xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** **todos** los canales SPI/I2S del STM32F407VG
—SPI1, SPI2/I2S2, SPI3/I2S3 y los bloques de extensión I2S2ext e I2S3ext—
[IR, §12.5, §12.7], con el requisito explícito de **analizar sus similitudes y
diferencias** y de que el tipo se seleccione por parámetros. Se aprovecha además
para cerrar el **modo síncrono del USART**, que quedó anotado como pendiente en
`doc/stm32f4xx/stm32f407vg_fase4_uart.md` §8 precisamente para hacerlo junto con el SPI,
con el que comparte la temporización de la trama.

**Resultado:** los bloques están completos y verificados. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **658 de 658 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 343 de F4 + **99 nuevas de SPI/I2S y del modo
síncrono**, código de salida 0, ~2,5 s de CPU del anfitrión). La verificación no
se queda en los registros: dos SPI se hablan **por cuatro pistas de la placa**,
con sus pads eléctricos; un flujo de audio de 47,6 kHz viaja de un bloque a otros
dos por CK, WS y SD; el reloj del SPI y el del USART síncrono se **miden en el
pin**; y un firmware compilado con el **CMSIS oficial de ARM y de ST** gobierna
el maestro y el esclavo con el mismo mini driver.

---

## 1. Similitudes y diferencias de los canales SPI/I2S

Esta es la parte que el encargo pide analizar.

### 1.1 Lo que comparten (que es casi todo)

Las cinco instancias son **el mismo bloque de diseño**. Comparten, bit a bit:

* el **banco de registros**: `CR1`, `CR2`, `SR`, `DR`, `CRCPR`, `RXCRCR`,
  `TXCRCR`, `I2SCFGR`, `I2SPR`, en los mismos offsets [IR, §12.5.3];
* el **desplazador full-duplex** y su temporización: cuatro combinaciones de
  `CPOL`/`CPHA`, tramas de 8 o 16 bits (`DFF`), MSB o LSB primero (`LSBFIRST`);
* las **banderas** `TXE`, `RXNE`, `BSY`, `OVR`, `MODF`, `CRCERR`, `UDR`,
  `CHSIDE` y su semántica de borrado por secuencia de lecturas;
* las **interrupciones** (`TXEIE`, `RXNEIE`, `ERRIE`) y las **peticiones de
  DMA** (`TXDMAEN`, `RXDMAEN`);
* la **gestión de NSS** por hardware o por software y el **prescalador**
  `f_SCK = f_PCLK / 2^(BR+1)`.

Un driver escrito para uno funciona en los demás mientras se limite a lo común;
el firmware de T55 lo demuestra usando literalmente la misma función para el
maestro y para el esclavo.

### 1.2 En qué se diferencian

| | SPI1 | SPI2, SPI3 | I2S2ext, I2S3ext |
| :--- | :---: | :---: | :---: |
| Bus | **APB2** | APB1 | APB1 |
| `f_SCK` máxima [IR, §12.5.1] | **42 MHz** | 21 MHz | — |
| Base | 0x4001 3000 | 0x4000 3800 / 3C00 | 0x4000 3400 / 4000 |
| Modo SPI (`CR1`) | sí | sí | **no** |
| Modo I2S (`I2SCFGR`/`I2SPR`) | **no** | sí | sí |
| Maestro de audio (divisor, `MCKOE`) | — | sí | **no, solo esclavo** |
| CRC de hardware | sí | sí | **no** |
| Formato TI (`CR2.FRF`), `SSOE` | sí | sí | **no** |
| Pin de datos en modo I2S | — | MOSI | **MISO** |
| Vector de interrupción | 35 | 36 / 51 | comparte el de su padre |
| Reloj de audio | — | PLLI2S R | PLLI2S R (del padre) |

Tres diferencias merecen comentario porque no son cosméticas:

1. **El bus del que cuelgan.** SPI1 está en APB2 y los otros en APB1. Con el
   árbol de reloj a 168 MHz eso son 84 MHz contra 42 MHz de reloj de periférico
   y, por tanto, **el doble de velocidad de línea** [IR, §12.5.1]. No es un
   rasgo del bloque sino de su integración, pero se comporta como tal: el mismo
   `BR` da frecuencias distintas, y el modelo avisa si se supera el límite.
2. **En el F407 el audio vive en SPI2 y SPI3.** El SPI1 no implementa el modo
   I2S; sus registros `I2SCFGR` e `I2SPR` son reservados y leen cero.
3. **Los bloques de extensión son la mitad que falta.** Un I2S normal es
   half-duplex: o transmite o recibe. Para tener audio full-duplex, ST añade
   junto a I2S2 e I2S3 un bloque **I2SxEXT** que solo hace I2S, solo como
   esclavo, y que **cuelga del CK y del WS del bloque principal** aportando
   únicamente su pin de datos —que es el pin MISO del SPI padre
   ([IR, §2.1]: `I2S2ext_SD` en PB14/PC2, `I2S3ext_SD` en PB4/PC11)—. No tiene
   CR1 útil, ni CRC, ni divisor de audio propio.

### 1.3 Cómo se selecciona la variante

Los rasgos se describen con una estructura `constexpr`, igual que en el USART
(§1 de `doc/stm32f4xx/stm32f407vg_fase4_uart.md`) y en los temporizadores (§1 de
`doc/stm32f4xx/stm32f407vg_fase4_tim.md`); ya es la convención del proyecto:

```cpp
struct SpiCaps {
    bool spi_mode   = true;   // el bloque puede funcionar como SPI (CR1 útil)
    bool i2s_mode   = false;  // ... y como I2S (I2SCFGR / I2SPR)
    bool i2s_master = true;   // puede GENERAR el reloj de audio (divisor, MCK)
    bool crc        = true;   // generador de CRC de hardware
    bool ti_mode    = true;   // CR2.FRF: formato de trama TI
    bool nss_pin    = true;   // gestión de NSS por hardware (SSOE, MODF)
    bool sd_on_miso = false;  // el dato de I2S sale/entra por MISO, no por MOSI
    double max_sck_hz = 21.0e6;   // límite de la hoja de características
    const char* kind  = "SPI";
};
```

**En tiempo de compilación**, con el parámetro de plantilla —una referencia a
los rasgos, que quedan disponibles como constantes de la clase—:

```cpp
template <const SpiCaps& Caps>
class SpiT : public SpiBase {
public:
    SpiT(sc_core::sc_module_name nm, uint32_t base) : SpiBase(nm, base, Caps) {}
    static constexpr bool has_i2s()   { return Caps.i2s_mode; }
    static constexpr bool has_spi()   { return Caps.spi_mode; }
    static constexpr bool has_crc()   { return Caps.crc; }
    static constexpr double max_sck() { return Caps.max_sck_hz; }
};

using Spi    = SpiT<CAPS_SPI_APB2>;   // SPI1
using SpiI2s = SpiT<CAPS_SPI_I2S>;    // SPI2, SPI3
using I2sExt = SpiT<CAPS_I2S_EXT>;    // I2S2ext, I2S3ext

static_assert(!Spi::has_i2s(),    "en el F407 el SPI1 no tiene modo I2S");
static_assert(!I2sExt::has_spi(), "los bloques de extension solo hacen audio");
static_assert(Spi::max_sck() > SpiI2s::max_sck(),
              "SPI1 esta en APB2 y admite el doble de SCK [IR, 12.5.1]");
```

y el netlist declara el tipo de cada instancia, de modo que confundirlas es un
error de compilación:

```cpp
Spi    spi1{"spi1", addr::SPI1_B};                                  // SPI puro, APB2
SpiI2s spi2{"spi2", addr::SPI2_B}, spi3{"spi3", addr::SPI3_B};      // SPI + I2S
I2sExt i2s2ext{"i2s2ext", addr::I2S2EXT_B};                         // solo audio
I2sExt i2s3ext{"i2s3ext", addr::I2S3EXT_B};
```

**En tiempo de ejecución**, con el parámetro del constructor:

```cpp
SpiBase s{"s", base, SpiCaps{...}};                    // rasgos campo a campo
SpiBase t{"t", base, /*i2s=*/true, /*f_max=*/42e6};    // atajo por los dos ejes
```

La prueba T49 construye con el atajo un SPI **con modo I2S y el límite de
frecuencia del APB2**, que no existe en el F407, para dejar claro que los dos
ejes son independientes y no un interruptor de tres posiciones.

### 1.4 Qué cambia realmente al cambiar los rasgos

Como en las fases anteriores, los rasgos gobiernan las **máscaras de escritura**
de los registros. T49 escribe unos en todos los registros de tres instancias y
lee lo que queda; ésta es la salida real de la suite:

```
             CR1    CR2   CRCPR I2SCFGR I2SPR
    SPI1    0xFFBF 0x00F7 0x1021 0x0000  0x0000
    SPI2    0xFFBF 0x00F7 0x1021 0x0BBF  0x03FF
    I2S2ext 0x0000 0x00E3 0x0000 0x09BF  0x0000
```

* **SPI1** tiene el `CR1` completo pero `I2SCFGR` e `I2SPR` reservados.
* **SPI2** los tiene todos.
* **I2S2ext** tiene `CR1` entero reservado (no es un SPI), no tiene `CRCPR`, le
  faltan `FRF` y `SSOE` en `CR2` (0x00E3 frente a 0x00F7), su `I2SPR` es
  reservado —no genera reloj— y en `I2SCFGR` le falta el bit 9, de modo que
  `I2SCFG` solo puede tomar los dos valores de esclavo (0x09BF frente a 0x0BBF).

---

## 2. Resumen ejecutivo

| Aspecto | Estado |
| :--- | :--- |
| Instancias | 5, de 3 variantes, de un solo modelo (`periph/spi.h`, 860 líneas) |
| Registros | CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR, I2SCFGR, I2SPR [IR, §12.5.3] |
| Trama SPI | 8/16 bits, MSB/LSB primero, las cuatro combinaciones CPOL/CPHA |
| Maestro / esclavo | maestro con prescalador `BR`; esclavo dirigido por los flancos del pin SCK |
| NSS | por software (`SSM`/`SSI`), por hardware (entrada) y salida (`SSOE`), con fallo de modo `MODF` |
| Conectividad | full-duplex, solo recepción (`RXONLY`) y bidireccional de un hilo (`BIDIMODE`/`BIDIOE`) |
| CRC | polinomio programable, acumulación de 8/16 bits, `CRCNEXT` y `CRCERR` |
| I2S | maestro y esclavo, transmisor y receptor; Philips, MSB y LSB justificados; `DATLEN`/`CHLEN`; divisor `I2SDIV`/`ODD`; salida `MCK` |
| Sistema | interrupciones, DMA, vectores compartidos de los bloques de extensión |
| Pines | 29 entradas de la tabla AF (SPI1/2/3, I2S2/3, MCK y los dos I2SxEXT) |
| USART síncrono | reloj de datos en el pin CK con `CPOL`/`CPHA`/`LBCL` (cierra `_uart.md` §8) |
| Verificación | 99 comprobaciones nuevas (T49-T55), 658 en total, 0 fallos, ~2,5 s |

---

## 3. Ficheros de la fase

### 3.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `verif/fw/spi_demo/main.c` | firmware CMSIS: SPI1 maestro contra SPI2 esclavo con el mismo driver, y el modo I2S que solo tiene el SPI2 |
| `verif/fw/spi_demo/Makefile` | compilación con el startup y el `system_stm32f4xx.c` de ST |
| `doc/stm32f4xx/stm32f407vg_fase5_spi.md` | este informe |

### 3.2 Reescritos o ampliados

| Fichero | Cambio |
| :--- | :--- |
| `periph/spi.h` | reescrito por completo: de un esqueleto de 37 líneas con `TODO(F5)` al modelo parametrizado de 860 líneas |
| `periph/usart.h` | modo síncrono terminado: generación del reloj de datos CK |
| `common/ahb_types.h` | direcciones base de I2S2ext e I2S3ext [IR, mapa APB1] |
| `top/stm32f407vg.h` | instancias con los nuevos tipos, los dos bloques de extensión y sus entradas del decodificador APB1 |
| `top/stm32f407vg_bind2.h` | relojes de audio (onda y frecuencia), CK/WS compartidos de los bloques de extensión, IRQ con puertas OR, y la tabla AF de SPI/I2S |
| `top/sc_main.cpp` | grupos T49-T55, ocho pistas de placa nuevas y un maestro de bus más |
| `README.md` | estado de la fase y cuenta de la suite |

---

## 4. El modelo

### 4.1 El maestro marca el tiempo; el esclavo, los flancos

El SPI tiene dos papeles muy distintos y el modelo los implementa con dos
mecanismos distintos, cada uno el natural para su papel:

* el **maestro** es un `SC_THREAD`: él pone el reloj, así que sabe cuánto dura
  cada bit y avanza el tiempo de simulación;
* el **esclavo** es un `SC_METHOD` sensible al pin `SCK`: no tiene reloj propio
  y se limita a reaccionar a los flancos que le llegan, exactamente como el
  silicio. Por eso funciona con un maestro a cualquier frecuencia y sin que el
  modelo tenga que suponer nada sobre él.

```cpp
void slave_sck_proc() {
    ...
    const bool leading = (sck_in.read() != cpol());   // reposo -> activo
    if (!cpha()) {
        if (leading) { /* captura */ } else { /* prepara el siguiente bit */ }
    } else {
        if (leading) { /* prepara */ } else { /* captura */ }
    }
}
```

La sutileza que costó una iteración: la referencia del detector de flancos tiene
que ser el **nivel de reposo que hay en el pin al habilitarse**. Con `CPOL = 1`
la línea está alta en reposo, y un detector inicializado a cero se come el
primer flanco de captura y desplaza el marco entero. `start_up()` toma esa foto:

```cpp
sck_prev_ = sck_in.read();
ws_prev_  = ws_level();
```

### 4.2 El dato del esclavo tiene que estar en el pin ANTES del primer flanco

Con `CPHA = 0` el maestro captura en el **primer** flanco del bit, de modo que
el esclavo debe tener ya su primer bit en MISO cuando ese flanco llega. El
modelo carga el desplazador y saca el bit en tres momentos: al habilitarse, al
seleccionarse por NSS y —esto es lo que faltaba en la primera versión— **al
escribir `DR` estando entre marcos**:

```cpp
void write_dr(uint16_t v) {
    ...
    // Un esclavo entre marcos ya está enganchado a la línea de reloj: el dato
    // recién escrito tiene que entrar en el desplazador AHORA y, con CPHA = 0,
    // salir ya el primer bit al pin, porque el maestro puede empezar a dar
    // flancos en cualquier momento.
    if (!i2s_active() && caps_.spi_mode && spe() && !mstr() && bit_i_ == 0)
        start_slave_frame();
```

Sin esto el esclavo transmitía siempre ceros: cargaba el desplazador al
habilitarse, cuando el firmware todavía no le había dado ningún dato.

### 4.3 Fallo de modo: solo cuando NSS es entrada

`MODF` avisa de que otro maestro ha tomado el bus tirando de NSS. El modelo lo
evalúa **por nivel** y no solo por flanco —al habilitar el SPI la línea ya puede
estar baja—, pero únicamente cuando NSS es realmente una entrada:

```cpp
if (mstr() && !ssm() && !ssoe() && !nss_in.read()) { /* MODF */ }
```

Con `SSOE` el pin es una salida que el propio maestro pone a cero: sin la
condición `!ssoe()`, habilitar un maestro con `SSOE` se auto-provocaba el fallo
de modo. Es exactamente el tipo de error que solo aparece al probar el bloque
contra un esclavo real, y por eso la prueba T51 monta el enlace completo.

### 4.4 Apagar el SPI a media trama la aborta

El manual exige esperar a que `BSY` caiga antes de tocar la configuración. Si no
se hace, el marco queda a medias; el modelo lo **descarta** en vez de entregar
medio dato, y vacía el desplazador del esclavo para que la próxima habilitación
empiece un marco nuevo:

```cpp
if (!enabled()) { aborted = true; break; }
...
if (!aborted) frame_done(tx, rx);
```

Sin eso, un marco abortado dejaba `RXNE` con basura y descolocaba al esclavo
medio byte durante el resto de la simulación —un fallo que se manifestaba tres
pruebas más tarde y costó encontrarlo—.

### 4.5 El I2S: WS manda, CK cuenta

El modo I2S reutiliza los mismos pines (SCK→CK, NSS→WS, MOSI→SD) y el mismo
desplazador, con tres diferencias de fondo: la trama la marca **WS** (un hueco
por canal, de `CHLEN` bits), el dato ocupa `DATLEN` bits dentro del hueco, y el
reloj sale de un divisor propio alimentado por el PLLI2S:

```
con MCKOE = 0:   f_CK = I2SCLK / (2*I2SDIV + ODD)     y   F_S = f_CK / (2*CHLEN)
con MCKOE = 1:   F_S  = I2SCLK / (256 * (2*I2SDIV + ODD))   y   MCK = 256*F_S
```

El estándar **Philips** adelanta el flanco de WS un ciclo de CK respecto al
dato. El modelo lo reproduce en los dos lados: el maestro conmuta WS durante el
último bit del hueco, y el esclavo interpreta ese flanco como «queda un bit para
cerrar el hueco actual», que es además lo que le permite **sincronizarse** si
arranca a media trama:

```cpp
if (ws != ws_prev_) {
    ws_prev_ = ws;
    if (philips) align_ = 1u;      // el hueco acaba en un ciclo
    else         begin_slot(ws);   // en los justificados, el flanco ES el hueco
}
...
if (align_)              close = (--align_ == 0);
else if (bit_i_ >= slot) close = true;
```

### 4.6 Los bloques de extensión no tienen pines de reloj

Un `I2SxEXT` cuelga del CK y del WS **del bloque principal**: en el silicio son
los mismos hilos, no una función alternativa propia. Modelarlo con entradas del
multiplexor de pines era incorrecto —PB13 solo puede estar en una AF a la vez, y
el bloque de extensión se quedaba sin reloj—. El modelo tiene dos entradas
explícitas para eso y el netlist las ata a la entrada del pad de los pines del
padre, que es exactamente lo que ve el silicio:

```cpp
sc_core::sc_in<bool> ext_ck{"ext_ck"}, ext_ws{"ext_ws"};
...
i2s2ext.ext_ck(pinmux.pad_din[1 * N_PORT_PINS + 13]);   // PB13 I2S2_CK
i2s2ext.ext_ws(pinmux.pad_din[1 * N_PORT_PINS + 12]);   // PB12 I2S2_WS
```

### 4.7 El modo síncrono del USART, terminado

`doc/stm32f4xx/stm32f407vg_fase4_uart.md` §8 dejó anotado que el USART aceptaba
`CLKEN`/`CPOL`/`CPHA`/`LBCL` y habilitaba el pin CK, pero no emitía el reloj de
datos, y que se completaría «junto con el SPI en F5, que comparte la
temporización de la trama síncrona». Hecho:

```cpp
void send_bit(bool level, const sc_core::sc_time& tb, bool with_clock) {
    drive_tx(level);
    if (!with_clock) { wait(tb); return; }
    ++n_ck_;
    if (!cpha_ck()) {
        wait(tb / 2.0); o_ck_ = !cpol_ck(); publish();   // flanco de captura
        wait(tb / 2.0); o_ck_ =  cpol_ck(); publish();
    } else {
        o_ck_ = !cpol_ck(); publish();                    // flanco de preparación
        wait(tb / 2.0); o_ck_ = cpol_ck(); publish();     // flanco de captura
        wait(tb / 2.0);
    }
}
```

Las tres reglas del bloque quedan cubiertas: el reloj lo genera **siempre** el
USART (es el maestro), acompaña a los bits de **datos y paridad** pero **no** al
de arranque ni a los de parada, y el pulso del último bit solo sale con
`LBCL = 1` [IR, §12.4.3-E]. Es, salvo el nombre de los bits, la misma
temporización que la del SPI de §4.1, que es justo la razón por la que tenía
sentido dejarlo para esta fase.

---

## 5. Verificación

Siete grupos nuevos, 99 comprobaciones.

### 5.1 T49 — Selección de la variante (20 comprobaciones)

`static_assert` y accesores `constexpr`; los rasgos declarados por cada
instancia; la tabla de máscaras de §1.4 leída **por el bus** para las tres
variantes; que el mismo `BR` da el doble de frecuencia en el SPI1 que en el
SPI2 porque cuelgan de buses distintos; y una variante construida en ejecución
que no existe en el F407.

### 5.2 T50 — Registros y prescalador (14 comprobaciones)

Gating, valores de reset (`SR` = 0x0002, `CRCPR` = 0x0007, `I2SPR` = 0x0002),
el prescalador `f_PCLK / 2^(BR+1)` barrido para varios `BR`, y que el polinomio
de CRC queda congelado con `SPE = 1`.

### 5.3 T51 — Enlace maestro-esclavo por los pines (19 comprobaciones)

SPI1 (APB2) contra SPI2 (APB1) por cuatro pistas de placa —PA5→PB13 (SCK),
PA7→PB15 (MOSI), PB14→PA6 (MISO) y PA4→PB12 (NSS)—, con sus pads eléctricos:

- intercambio full-duplex de 8 bits en las dos direcciones y cuatro bytes
  seguidos;
- trama de 16 bits (`DFF`) y orden `LSBFIRST`;
- **las cuatro combinaciones de `CPOL`/`CPHA`**, una por una;
- el reloj **medido en el pin** con el «osciloscopio» del banco (500 kHz
  programados, 500 kHz medidos);
- `NSS` por hardware: `SSOE` saca la señal en el maestro y el esclavo la ve en
  su pin.

### 5.4 T52 — CRC, errores y conectividad (11 comprobaciones)

El generador de CRC acumulando sobre tres bytes y **coincidiendo en los dos
extremos**; que sin `CRCEN` los registros no se mueven; `OVR` con la semántica
de borrado por lectura de `SR` y `DR`; el fallo de modo `MODF` provocado tirando
de NSS con el pull interno del pad (y comprobando que el hardware borra `SPE` y
`MSTR`); y el modo bidireccional de un hilo.

### 5.5 T53 — El enlace de audio (13 comprobaciones)

PLLI2S a 96 MHz; I2S2 como **maestro transmisor** Philips de 16 bits con
`I2SDIV = 31`, `ODD = 1` → `f_CK` = 1,524 MHz y `F_S` = 47,6 kHz (el redondeo
real al pedir 48 kHz con ese PLL); I2S3 como **esclavo receptor** por PC10/PA15/
PC12 y, a la vez, **I2S2ext** recibiendo el mismo flujo por su pin MISO, que es
la otra mitad del full-duplex:

```
f_CK = 1523810 Hz, F_S = 47619 Hz
enviadas 23 muestras; I2S3 recibio 19 (17 correctas), I2S2ext 19 (17 correctas)
```

(las dos primeras muestras de cada receptor son los huecos de sincronización).
Se comprueba además que `CHSIDE` alterna entre canal izquierdo y derecho y que
`MCKOE` saca el reloj maestro del códec por PC6 con la frecuencia de la fórmula
de 256·F_S.

### 5.6 T54 — El modo síncrono del USART (9 comprobaciones)

Sobre el pin PA4 (USART2_CK, AF7):

```
pulsos de CK en un marco 8N1 con LBCL = 0: 7
periodo de CK medido en PA4: 1.000 us (bit = 1.000 us)
```

Siete pulsos para ocho bits con `LBCL = 0`, ocho con `LBCL = 1`, nueve con
`M = 1` y paridad; el nivel de reposo siguiendo a `CPOL`; el periodo medido en
el pin igual al tiempo de bit; y el marco de datos intacto —el mismo byte llega
al otro puerto— mientras el reloj sale por CK.

### 5.7 T55 — DMA y firmware con CMSIS (13 comprobaciones)

Interrupción de recepción por `RXNEIE`; una transferencia de ocho bytes
**memoria → SPI1 → cuatro cables → SPI2 → memoria** servida por dos streams de
DMA distintos (DMA2 S3C3 para transmitir y DMA1 S3C0 para recibir); y el
firmware `verif/fw/spi_demo/main.c`, compilado con el CMSIS oficial, que usa el
**mismo mini driver** para el maestro y el esclavo y comprueba desde el
software que el modo I2S existe en el SPI2 y no en el SPI1:

```
PCLK2 = 84000000 Hz | bytes intercambiados = 8 | CR1 = 0x036C | audio configurado = 1
```

### 5.8 Resultado de la suite

```
Resumen F1: 124 comprobaciones OK, 0 fallos
Resumen F2: 12 comprobaciones OK, 0 fallos
Resumen F3: 80 comprobaciones OK, 0 fallos
Resumen F4 (DMA): 71 comprobaciones OK, 0 fallos
Resumen F4 (USART): 70 comprobaciones OK, 0 fallos
Resumen F4 (TIM)  : 118 comprobaciones OK, 0 fallos
Resumen F4 (EXTI) : 84 comprobaciones OK, 0 fallos
Resumen F5 (SPI)  : 99 comprobaciones OK, 0 fallos
TOTAL     : 658 comprobaciones OK, 0 fallos
```

Sin regresiones en las fases anteriores. ~2,5 s de CPU del anfitrión.

---

## 6. Decisiones de diseño

1. **Un modelo, tres variantes** (§1.3), con la misma receta que USART y TIM.
   Ya es la convención del proyecto para familias de instancias desiguales.
2. **La frecuencia máxima es un rasgo**, aunque venga de la integración y no del
   bloque. Es lo que permite que el modelo avise cuando un `BR` deja el SPI2 por
   encima de sus 21 MHz.
3. **Maestro con hilo, esclavo con método** (§4.1): cada papel con el mecanismo
   de SystemC que le corresponde.
4. **Los bloques de extensión toman CK y WS por puertos propios** (§4.6) en vez
   de por el multiplexor de pines, porque en el silicio no son pines.
5. **Philips se modela de verdad**, con el adelanto de un ciclo de WS y la
   resincronización del esclavo (§4.5), en lugar de alinear el dato al hueco y
   documentar la diferencia.
6. **Un marco abortado se descarta** (§4.4) en vez de entregarse a medias.

---

## 7. Trabajo pendiente

De este bloque:

* **Formato de trama TI** (`CR2.FRF`): el bit es escribible en las variantes que
  lo tienen y la bandera `FRE` existe, pero la trama TI —con NSS como pulso de
  sincronismo de un ciclo— no está modelada; el modelo usa siempre el formato
  Motorola.
* **`CRCNEXT` de extremo a extremo**: el modelo calcula y compara el CRC y
  levanta `CRCERR`, pero la prueba comprueba la coincidencia de los registros
  más que la secuencia completa de envío del CRC al final del bloque.
* **PCM (`I2SSTD = 11`) y `PCMSYNC`**: los bits se aceptan y se enmascaran, pero
  la trama PCM de sincronismo corto/largo no está implementada.
* **`DATLEN` de 24 y 32 bits con `CHLEN = 32`**: la ruta está escrita y el
  registro se respeta, pero solo se verifica la combinación de 16/16.
* **Celdas de DMA de los I2SxEXT**: [IR] no recoge su asignación en las tablas
  de [IR, §11.4], así que sus líneas de petición quedan sin conectar; el resto
  del bloque de extensión sí funciona.
* **`I2SSRC` (reloj de audio externo por I2S_CKIN)**: el modelo usa la salida R
  del PLLI2S; el pin alternativo no está enrutado.

Del resto de la fase F5 quedan **I2C, ADC, DAC, RTC, bxCAN, SDIO, CRC/RNG y los
watchdogs**.
