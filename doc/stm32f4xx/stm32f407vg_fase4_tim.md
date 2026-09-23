# Fase F4 (parte TIM) — Los catorce temporizadores

Informe de implementación de la **parte de temporizadores** de la fase F4 del
plan `doc/stm32f4xx/smt32f407vg_diseño.md` (§7). Continúa a `doc/stm32f4xx/stm32f407vg_fase3.md`
(pines, GPIO y RCC eléctrico), `doc/stm32f4xx/stm32f407vg_fase4_dma.md` (controladores
DMA) y `doc/stm32f4xx/stm32f407vg_fase4_uart.md` (interfaces serie). Fuentes:
`doc/refs/stm32f407xx/informe_revisado.md` [IR] y `doc/refs/stm32f407xx/informe_instrucciones.md` [II].

**Alcance de este entregable:** los **catorce** temporizadores del STM32F407VG
—TIM1 y TIM8 (control avanzado, [IR, §12.1]), TIM2 a TIM5 (propósito general,
[IR, §12.2]), TIM6 y TIM7 (básicos, [IR, §12.3]) y TIM9 a TIM14 (propósito
general reducidos, [IR, §12.8])—, con el requisito explícito de que **el tipo de
temporizador, el número de bits, el número de canales, etc. se seleccionen por
parámetros**, de plantilla o de constructor. EXTI/SYSCFG queda para el último
entregable de esta fase.

**Resultado:** el bloque está completo y verificado. El modelo compila sin
avisos con `-Wall -Wextra -O2` y la suite pasa **475 de 475 comprobaciones**
(124 de F1 + 12 de F2 + 80 de F3 + 71 del DMA + 70 de UART/USART + **118 nuevas
de los temporizadores**, código de salida 0, ~2,5 s de CPU del anfitrión). La
verificación no se queda en los registros: el PWM se **mide en el pin**, con su
pad eléctrico, iluminando el LED de la placa; una pista de placa lleva esa misma
señal a la entrada de captura de otro temporizador, que mide su periodo; y un
firmware compilado con el **CMSIS oficial de ARM y de ST** gobierna cuatro
familias distintas con el mismo driver mínimo.

---

## 1. Cómo se selecciona el funcionamiento de cada tipo de temporizador

Esta es la parte que el encargo pide documentar con detalle.

### 1.1 La idea

Los catorce temporizadores son el **mismo bloque de diseño**. Lo que distingue a
un TIM1 de un TIM14 no es un modo de operación, sino **qué recursos están
presentes en el silicio**: la anchura del contador, cuántos canales de
captura/comparación tiene, si esos canales llevan salida complementaria con
tiempo muerto, si hay contador de repeticiones, controlador de esclavo, entrada
ETR, peticiones de DMA, modo ráfaga o una salida TRGO. El modelo lo representa
con una estructura de rasgos `constexpr`, `TimCaps`:

```cpp
struct TimCaps {
    unsigned width_bits    = 16;  // anchura de CNT/ARR/CCRx: 16 o 32
    unsigned channels      = 4;   // canales de captura/comparación: 0..4
    unsigned comp_channels = 0;   // de ellos, cuántos tienen salida OCxN
    bool bdtr           = false;  // BDTR: MOE/AOE/BKE/BKP/OSSR/OSSI/DTG
    bool repetition     = false;  // RCR (contador de repeticiones)
    bool slave_mode     = true;   // SMCR.SMS/TS/MSM y entradas ITRx
    bool ext_trigger    = true;   // SMCR.ETF/ETPS/ECE/ETP y entrada ETR
    bool encoder        = true;   // modos de codificador incremental
    bool hall           = true;   // CR2.TI1S (XOR de las tres entradas)
    bool dma            = true;   // DIER.UDE/CCxDE/COMDE/TDE y peticiones
    bool dma_burst      = true;   // DCR/DMAR (modo ráfaga)
    bool trgo           = true;   // CR2.MMS y salida TRGO
    bool center_aligned = true;   // CR1.CMS (modos alineados al centro)
    bool down_count     = true;   // CR1.DIR (conteo descendente)
    bool one_pulse      = true;   // CR1.OPM
    bool clock_division = true;   // CR1.CKD
    bool split_irq      = false;  // 4 vectores propios o uno solo
    const char* kind    = "TIM";  // etiqueta para trazas y avisos
};
```

Los diecisiete campos son **ejes independientes**, no un selector de seis
posiciones. Esa independencia es lo que hace útil la parametrización: TIM9 tiene
controlador de esclavo pero no entrada ETR; TIM6 no tiene canales pero sí
peticiones de DMA y TRGO (es el disparo del DAC); TIM2 tiene 32 bits sin ser
avanzado. Ninguna de esas combinaciones cabe en una jerarquía de herencia
lineal, y todas caben en una estructura de rasgos.

### 1.2 Las seis variantes del F407

Como C++17 no admite inicializadores designados, cada variante se construye con
una función `constexpr` que dice **explícitamente en qué se aparta** del bloque
completo. Es la forma legible de describir una familia de periféricos: se lee
como la lista de diferencias del manual.

```cpp
constexpr TimCaps caps_advanced() {           // TIM1, TIM8 [IR, §12.1]
    TimCaps c{};
    c.width_bits = 16; c.channels = 4; c.comp_channels = 3;
    c.bdtr = true; c.repetition = true; c.split_irq = true;
    c.kind = "avanzado";
    return c;
}
constexpr TimCaps caps_gp2ch() {              // TIM9, TIM12 [IR, §12.8]
    TimCaps c{};
    c.width_bits = 16; c.channels = 2;
    c.ext_trigger = false;                    // no tienen entrada ETR
    c.dma = false; c.dma_burst = false;       // sin DMA en el F407 [IR, §11.4]
    c.center_aligned = false; c.down_count = false;   // solo ascendente
    c.hall = false;
    c.kind = "GP 2 canales";
    return c;
}
/* ... caps_gp32(), caps_gp16(), caps_gp1ch(), caps_basic() ... */

inline constexpr TimCaps CAPS_TIM_ADV   = caps_advanced();
inline constexpr TimCaps CAPS_TIM_GP32  = caps_gp32();
inline constexpr TimCaps CAPS_TIM_GP16  = caps_gp16();
inline constexpr TimCaps CAPS_TIM_GP2CH = caps_gp2ch();
inline constexpr TimCaps CAPS_TIM_GP1CH = caps_gp1ch();
inline constexpr TimCaps CAPS_TIM_BASIC = caps_basic();
```

| Variante | Instancias | Bits | Canales | Complementarias | BDTR | RCR | Esclavo | ETR | DMA | Ráfaga | TRGO | Centro/DIR | Vectores |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| `CAPS_TIM_ADV` | TIM1, TIM8 | 16 | 4 | 3 | sí | sí | sí | sí | sí | sí | sí | sí | 4 |
| `CAPS_TIM_GP32` | TIM2, TIM5 | **32** | 4 | – | no | no | sí | sí | sí | sí | sí | sí | 1 |
| `CAPS_TIM_GP16` | TIM3, TIM4 | 16 | 4 | – | no | no | sí | sí | sí | sí | sí | sí | 1 |
| `CAPS_TIM_GP2CH` | TIM9, TIM12 | 16 | 2 | – | no | no | sí | **no** | **no** | no | sí | no | 1 |
| `CAPS_TIM_GP1CH` | TIM10, TIM11, TIM13, TIM14 | 16 | **1** | – | no | no | **no** | no | no | no | **no** | no | 1 |
| `CAPS_TIM_BASIC` | TIM6, TIM7 | 16 | **0** | – | no | no | no | no | **sí** | no | **sí** | no | 1 |

### 1.3 Selección en tiempo de compilación (parámetro de plantilla)

La plantilla toma una **referencia a los rasgos** como parámetro no-tipo, de
modo que la variante queda grabada en el tipo y disponible como constante de
clase:

```cpp
template <const TimCaps& Caps>
class TimT : public TimerBase {
public:
    TimT(sc_core::sc_module_name nm, uint32_t base) : TimerBase(nm, base, Caps) {}
    static constexpr const TimCaps& variant() { return Caps; }
    static constexpr unsigned width()    { return Caps.width_bits; }
    static constexpr unsigned channels() { return Caps.channels; }
    static constexpr bool has_bdtr()     { return Caps.bdtr; }
    static constexpr bool is_32bit()     { return Caps.width_bits >= 32; }
};

using TimAdvanced = TimT<CAPS_TIM_ADV>;    // TIM1, TIM8
using TimGp32     = TimT<CAPS_TIM_GP32>;   // TIM2, TIM5
using TimGp16     = TimT<CAPS_TIM_GP16>;   // TIM3, TIM4
using TimGp2Ch    = TimT<CAPS_TIM_GP2CH>;  // TIM9, TIM12
using TimGp1Ch    = TimT<CAPS_TIM_GP1CH>;  // TIM10, TIM11, TIM13, TIM14
using TimBasic    = TimT<CAPS_TIM_BASIC>;  // TIM6, TIM7
```

Con esto, el netlist del top declara el tipo de cada temporizador y no puede
equivocarse: `TimGp32` y `TimBasic` son **tipos distintos**, y confundirlos es un
error de compilación, no un fallo de simulación.

```cpp
TimAdvanced tim1{"tim1", addr::TIM1_B},   tim8{"tim8", addr::TIM8_B};
TimGp32     tim2{"tim2", addr::TIM2_B},   tim5{"tim5", addr::TIM5_B};
TimGp16     tim3{"tim3", addr::TIM3_B},   tim4{"tim4", addr::TIM4_B};
TimGp2Ch    tim9{"tim9", addr::TIM9_B},   tim12{"tim12", addr::TIM12_B};
TimGp1Ch    tim10{"tim10", addr::TIM10_B}, tim11{"tim11", addr::TIM11_B};
TimGp1Ch    tim13{"tim13", addr::TIM13_B}, tim14{"tim14", addr::TIM14_B};
TimBasic    tim6{"tim6", addr::TIM6_B},   tim7{"tim7", addr::TIM7_B};
```

Los rasgos son visibles en tiempo de compilación, así que las incoherencias se
detectan al compilar:

```cpp
static_assert(TimAdvanced::has_bdtr(),   "los avanzados tienen freno y tiempo muerto");
static_assert(!TimGp32::has_bdtr(),      "TIM2/TIM5 no tienen BDTR");
static_assert(TimGp32::is_32bit(),       "TIM2/TIM5 son de 32 bits");
static_assert(!TimGp16::is_32bit(),      "TIM3/TIM4 son de 16 bits");
static_assert(TimGp2Ch::channels() == 2, "TIM9/TIM12 tienen dos canales");
static_assert(TimGp1Ch::channels() == 1, "TIM10/11/13/14 tienen un canal");
static_assert(TimBasic::channels() == 0, "TIM6/TIM7 no tienen canales");
```

### 1.4 Selección en tiempo de ejecución (parámetro del constructor)

Toda la implementación vive en `TimerBase`, que recibe los rasgos por el
constructor. Es el punto de selección en tiempo de ejecución, útil para barrer
variantes desde un banco de pruebas o para modelar un derivado de la familia con
otra combinación de recursos:

```cpp
// 1. Rasgos completos, campo a campo
TimCaps mios{};
mios.width_bits = 32; mios.channels = 3; mios.bdtr = true;
TimerBase t{"t", 0x40001800u, mios};

// 2. Atajo con los dos ejes que más distinguen a un temporizador de otro
TimerBase u{"u", 0x40001800u, /*bits=*/32, /*canales=*/2};
```

El atajo se apoya en `TimerBase::runtime_caps(bits, canales)`, que deriva del
número de canales los recursos que en esta familia van con él (sin cuatro
canales no hay modo ráfaga ni alineado al centro; sin dos, no hay controlador de
esclavo ni codificador). La prueba **T38** construye con este constructor un
temporizador que **no existe en el F407** —contador de 32 bits con solo dos
canales— y comprueba que se comporta como se le ha pedido:

```
variante en ejecucion (a medida, 32 bits, 2 canales): ARR = 0xFFFFFFFF, CCER = 0x00BB, CR1 = 0x039E
```

### 1.5 Qué cambia realmente al cambiar los rasgos

Los rasgos **no son cosméticos ni documentación**: gobiernan las **máscaras de
escritura de todos los registros** y la anchura de `CNT`, `ARR` y `CCRx`. Un
campo que la variante no tiene es un bit reservado: no se escribe y lee cero,
exactamente igual que en el silicio.

```cpp
uint32_t cr1_mask() const {
    uint32_t m = 0x0087u;                              // CEN, UDIS, URS, ARPE
    if (caps_.one_pulse)      m |= 1u << 3;            // OPM
    if (caps_.down_count)     m |= 1u << 4;            // DIR
    if (caps_.center_aligned) m |= 3u << 5;            // CMS
    if (caps_.clock_division) m |= 3u << 8;            // CKD
    return m;
}
uint32_t ccer_mask() const {
    uint32_t m = 0;
    for (unsigned c = 0; c < caps_.channels; ++c) {
        m |= (1u << (4 * c)) | (1u << (4 * c + 1));    // CCxE, CCxP
        m |= 1u << (4 * c + 3);                        // CCxNP
        if (c < caps_.comp_channels) m |= 1u << (4 * c + 2);   // CCxNE
    }
    return m;
}
uint32_t cnt_mask() const { return caps_.width_bits >= 32 ? 0xFFFFFFFFu : 0xFFFFu; }
```

La prueba T38 escribe `0xFFFF` en todos los registros de seis temporizadores de
familias distintas y **imprime lo que se puede leer**; ésta es la salida real de
la suite, y es la comprobación más directa de que la parametrización es
observable desde el bus:

```
           CR1    CR2    SMCR   DIER   CCER   BDTR RCR  DCR
    TIM1  0x03FF 0x7FFD 0xFFF7 0x7FFF 0xBFFF 0xFFFF 0xFF 0x1F1F
    TIM2  0x03FF 0x00F8 0xFFF7 0x5F5F 0xBBBB 0x0000 0x00 0x1F1F
    TIM3  0x03FF 0x00F8 0xFFF7 0x5F5F 0xBBBB 0x0000 0x00 0x1F1F
    TIM9  0x038F 0x0070 0x00F7 0x0047 0x00BB 0x0000 0x00 0x0000
    TIM10 0x0387 0x0000 0x0000 0x0003 0x000B 0x0000 0x00 0x0000
    TIM6  0x008F 0x0078 0x0000 0x0101 0x0000 0x0000 0x00 0x0000
```

Léase por columnas:

* **CR1** — TIM1/2/3 tienen `CKD`, `CMS`, `DIR` y `OPM` (0x03FF); TIM9 pierde
  `CMS` y `DIR` porque solo cuenta hacia arriba (0x038F); TIM10 pierde además
  `OPM` (0x0387); TIM6 es la «versión reducida» que describe [IR, §12.3.2]:
  solo `ARPE`, `OPM`, `URS`, `UDIS` y `CEN` (0x008F).
* **CR2** — solo TIM1 tiene los estados de reposo `OISx`/`OISxN` y los bits de
  precarga `CCPC`/`CCUS` de las complementarias. TIM10 no tiene CR2 útil.
  TIM6 **sí** conserva `MMS`: su TRGO es el disparo del DAC [IR, §12.3.1].
* **SMCR** — TIM3 lleva el controlador de esclavo completo con ETR (0xFFF7);
  TIM9 conserva `SMS`/`TS`/`MSM` pero **no la entrada ETR** (0x00F7); TIM10 no
  tiene controlador de esclavo.
* **DIER** — TIM1 añade `COMIE`/`BIE`/`COMDE`; TIM9 y TIM10 no tienen ninguna
  habilitación de DMA porque el F407 no les asigna peticiones [IR, §11.4].
* **CCER** — el número de nibbles activos **es** el número de canales, y el bit
  `CCxNE` de cada nibble solo existe en los tres primeros canales de TIM1/TIM8.
* **BDTR / RCR / DCR** — presentes únicamente donde el manual los sitúa.

Y la anchura del contador se ve igual de claro:

```
ARR de reset de TIM2 = 0xFFFFFFFF        ARR de reset de TIM3 = 0x0000FFFF
TIM2_CNT <- 0x12345678 se lee 0x12345678 TIM3_CNT <- 0x12345678 se lee 0x00005678
```

### 1.6 Resumen de la receta

| Quiero… | Escribo… |
| :--- | :--- |
| un TIM del F407, con su tipo grabado en el netlist | `TimGp32 tim2{"tim2", addr::TIM2_B};` |
| el mismo, sin alias | `TimT<CAPS_TIM_GP32> tim2{"tim2", addr::TIM2_B};` |
| una variante a medida, en ejecución | `TimerBase t{"t", base, TimCaps{...}};` |
| lo mismo, por los dos ejes principales | `TimerBase t{"t", base, 32, 2};` |
| consultar la variante de un tipo | `TimGp32::is_32bit()`, `TimGp32::channels()` |
| consultar la variante de una instancia | `tim2.caps().width_bits` |
| impedir una combinación inválida al compilar | `static_assert(TimAdvanced::has_bdtr(), ...)` |

Es la misma receta que en `periph/usart.h` (véase §1 de
`doc/stm32f4xx/stm32f407vg_fase4_uart.md`), y ya puede considerarse la **convención del
proyecto** para toda familia de periférico con instancias de recursos desiguales:
SPI/I2S y los dos bxCAN de F5 la seguirán.

---

## 2. Resumen ejecutivo

| Aspecto | Estado |
| :--- | :--- |
| Instancias | 14, de 6 variantes, de un solo modelo (`periph/timers.h`, 1102 líneas) |
| Banco de registros | CR1, CR2, SMCR, DIER, SR, EGR, CCMR1/2, CCER, CNT, PSC, ARR, RCR, CCR1-4, BDTR, DCR, DMAR [IR, §12.1.4-D] |
| Base de tiempos | prescaler, auto-recarga con precarga (ARPE), ascendente, descendente y alineado al centro, contador de repeticiones, un solo pulso |
| Comparación | 8 modos `OCxM` (congelado, activo, inactivo, conmutar, forzados, PWM 1 y 2), precarga `OCxPE`, polaridad `CCxP` |
| Complementarias | `MOE`/`AOE`/`OSSI`/`OSSR`/`OISx`, tiempo muerto `DTG` con sus cuatro tramos de codificación, entrada de freno `BKIN` con polaridad |
| Captura | `CCxS` (directa, cruzada), polaridad y ambos flancos, prescaler `ICxPSC`, banderas `CCxIF`/`CCxOF` con borrado por lectura de `CCRx` |
| Esclavo | reset, gated, trigger, reloj externo 1 y 2 (ETR), codificador incremental en sus tres modos; `TS` = ITR0-3, TI1F_ED, TI1FP1, TI2FP2, ETRF |
| Maestro | `MMS`: reset, enable, update, pulso de comparación y `OC1REF`..`OC4REF` |
| Interrupciones | 4 vectores en los avanzados (24/25/26/27 y 43/44/45/46) y uno global en el resto, con las puertas OR de los vectores compartidos |
| DMA | `UDE`/`CCxDE`/`COMDE`/`TDE` y modo ráfaga `DCR`/`DMAR` |
| Sistema | congelación por el depurador (`DBGMCU_APBx_FZ`), reloj TIMCLK1/TIMCLK2 con la regla del doble [IR, §4.4] |
| Pines | 69 entradas de la tabla AF registradas (CHx, CHxN, ETR, BKIN de los 14) |
| Verificación | 118 comprobaciones nuevas (T38-T43), 475 en total, 0 fallos, ~2,5 s |

---

## 3. Ficheros de la fase

### 3.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `verif/fw/tim_demo/main.c` | firmware CMSIS que gobierna cuatro familias de temporizador con el mismo driver mínimo |
| `verif/fw/tim_demo/Makefile` | compilación con el startup y el `system_stm32f4xx.c` de ST |
| `doc/stm32f4xx/stm32f407vg_fase4_tim.md` | este informe |

### 3.2 Reescritos o ampliados

| Fichero | Cambio |
| :--- | :--- |
| `periph/timers.h` | reescrito por completo: de un esqueleto de 80 líneas con `TODO(F4)` al modelo parametrizado de 1102 líneas |
| `top/stm32f407vg.h` | instancias con los nuevos tipos; `s_nc` ampliado a 256 señales |
| `top/stm32f407vg_bind2.h` | `bind_tim` con TIMCLK, su frecuencia y la matriz ITRx; IRQs de las seis variantes; peticiones de DMA de los 14; tabla AF de los canales |
| `verif/ext_parts.h` | `SignalLink::set_enabled()`: una pista de placa que se puede soldar y quitar |
| `top/sc_main.cpp` | grupos T38-T43, un maestro de bus más para la variante de ejecución y la circuitería de las pruebas |
| `README.md` | estado de la fase, cuenta de la suite y dos convenciones nuevas |

---

## 4. El modelo

### 4.1 El contador salta al siguiente suceso

Un temporizador a 84 MHz con `ARR` = 65535 desborda cada 780 µs. Evaluarlo
flanco a flanco de TIMCLK costaría **65 536 activaciones de proceso por
periodo**, y en un MCU con catorce temporizadores eso hunde la simulación. Peor
aún: el banco de pruebas **suprime las formas de onda** de los relojes internos
para ejecutar firmware largo (`Rcc::set_internal_waveforms(false)`, convención
introducida en F2), de modo que un contador sensible a `timclk.pos()`
sencillamente no contaría durante las pruebas de firmware.

El modelo cuenta **por tiempo, no por flancos**. El proceso de cuenta calcula
cuántos pasos faltan hasta el siguiente suceso —una coincidencia de comparación
o el desbordamiento— y espera exactamente ese tiempo:

```cpp
void tick_proc() {
    for (;;) {
        if (!tick_enabled() || tim_hz() <= 0.0) { /* dormir hasta que cambie algo */ }
        if (ticking_) cnt_ = cnt_now();
        t_anchor_ = sc_core::sc_time_stamp();
        t_tick_   = tick_time();            // (PSC+1) / f_TIMCLK
        steps_    = steps_to_event();
        const sc_core::sc_time dt = t_tick_ * double(steps_);
        wait(dt, wake_ev_ | rst_n.value_changed_event() |
                 freeze.value_changed_event() | timclk_hz.value_changed_event());
        if (sc_core::sc_time_stamp() < t_anchor_ + dt) continue;   // interrumpido
        arrive(steps_);
    }
}
```

Lo que hace que esto sea **indistinguible** de un contador ciclo a ciclo es que
`CNT` se **interpola** en cualquier instante intermedio, de modo que una lectura
del registro devuelve lo mismo que devolvería el silicio:

```cpp
uint32_t cnt_now() const {
    if (!ticking_ || t_tick_ == sc_core::SC_ZERO_TIME) return cnt_;
    const double d = (sc_core::sc_time_stamp() - t_anchor_) / t_tick_;
    uint64_t n = (d > 0.0) ? uint64_t(d + 1e-9) : 0u;
    if (n > steps_) n = steps_;                        // nunca pasa del suceso
    if (!dir_down_) { const uint64_t v = uint64_t(cnt_) + n;
                      return uint32_t(v > arr_act_ ? arr_act_ : v); }
    return uint32_t(n >= cnt_ ? 0u : cnt_ - n);
}
```

La prueba T39 comprueba precisamente eso: con `PSC` = 15 sobre TIMCLK de 16 MHz,
`CNT` vale 100 a los 100 µs y 200 a los 200 µs, sin que haya ocurrido ningún
suceso entre medias. El coste de simulación pasa a depender del **número de
sucesos** y no de la frecuencia del reloj: la suite completa, con catorce
temporizadores instanciados y hasta cinco corriendo a la vez, añade menos de un
segundo de CPU.

Cualquier escritura de registro que pueda mover el próximo suceso (`CR1`, `PSC`,
`ARR`, `CCRx`, `EGR`, `SMCR`…) notifica `wake_ev_`; el proceso se re-ancla con
`cnt_ = cnt_now()` y recalcula. Los modos en los que el contador **no** avanza
con el tiempo —reloj externo 1 y 2, codificador, gated con la puerta cerrada— lo
duermen y lo hacen avanzar paso a paso desde los procesos de entrada.

### 4.2 Comparación, PWM y tiempo muerto

`OCxREF` se evalúa en cada suceso a partir del valor exacto del contador. Los
modos «al igualar» (activo, inactivo, conmutar) solo conmutan **en el instante
del suceso**, no cuando una escritura de registro coincide por casualidad con
`CCRx`; de ahí el parámetro `at_event`:

```cpp
bool oc_ref(unsigned c, uint32_t cv, bool at_event) const {
    const uint32_t r = ccr_act_[c];
    const bool eq = at_event && (cv == r);
    switch (ocm(c)) {
        case 0: return ref_[c];                              // congelado
        case 1: return eq ? true  : ref_[c];                 // activo al igualar
        case 2: return eq ? false : ref_[c];                 // inactivo al igualar
        case 3: return eq ? !ref_[c] : ref_[c];              // conmutar
        case 4: return false;                                // forzado inactivo
        case 5: return true;                                 // forzado activo
        case 6: return dir_down_ ? (cv <= r) : (cv < r);     // PWM modo 1
        case 7: return !(dir_down_ ? (cv <= r) : (cv < r));  // PWM modo 2
        default: return ref_[c];
    }
}
```

El nivel del pin sale de `OCxREF` combinado con la polaridad `CCxP`, la
habilitación `CCxE` y, en los avanzados, `MOE`/`OSSI`/`OISx`. Con `MOE` = 0 y
`OSSI` = 0 el pad queda en **alta impedancia de verdad** (la prueba T40 lo
comprueba con `is_floating()` sobre el `AnalogNet`), que es lo que espera un
puente de potencia cuando el freno actúa.

El tiempo muerto se inserta con un proceso propio: al cambiar `OCxREF`, **las
dos salidas del par se apagan**, se espera `t_DTG` y solo entonces se enciende
la que corresponde. `DTG[7:0]` se decodifica con los cuatro tramos del manual, y
`CKD` divide `t_DTS` como manda [IR, §12.1.4-A].

### 4.3 La cadena de triggers, cableada en el netlist

El modelo del temporizador es **genérico**: para él, `ITR0..ITR3` son cuatro
entradas cualesquiera y `TRGO` una salida. Qué TRGO llega a cada ITR de cada
temporizador es una decisión de **integración**, no de diseño del bloque, y por
eso vive en el netlist:

```cpp
static const int ITR_T3[4] = { 0, 1, 4, 3};   // TIM3: TIM1, TIM2, TIM5, TIM4
...
bind_tim(tim3, s_pclk1, s_timclk1, s_timclk1_hz, P_TIM3, FZ_TIM3, s_trgo[2], ITR_T3);
```

**Nota de fuentes:** [IR] no recoge la matriz ITRx. Se ha cableado la tabla del
F407 (RM0090, tablas de «TIMx internal trigger connection») y se ha dejado
constancia en el propio fichero, junto a la tabla. Al vivir en el netlist, y no
en el modelo, corregirla es cambiar ocho líneas de datos; ninguna prueba de
`timers.h` depende de ella.

### 4.4 La petición de DMA es un pulso, no un nivel

En el silicio, la petición de DMA de un temporizador es un **pulso** que retira
el reconocimiento del controlador. El modelo del DMA de esta misma fase muestrea
el **nivel** de sus 64 líneas de petición (`DmaCtrl::stream_ready`), lo cual es
correcto para un USART —cuya bandera `TXE` la borra la propia transferencia—
pero no para un temporizador, cuya bandera `UIF` sigue levantada hasta que la
borra el software: un nivel produciría transferencias sin fin.

El temporizador emite por tanto un pulso de anchura mínima:

```cpp
void dma_proc() {
    for (;;) {
        wait(dma_ev_);
        while (dma_pend_) {
            o_dma_ = dma_pend_; dma_pend_ = 0; publish();
            wait(sc_core::sc_time(1, sc_core::SC_PS));
            o_dma_ = 0; publish();
        }
    }
}
```

El controlador arbitra en el mismo instante en que la línea sube, y su
transferencia ya consume tiempo de simulación (≥ 1 ciclo de HCLK), de modo que
en el siguiente arbitraje la línea está de nuevo baja: **exactamente una
transferencia por suceso del temporizador**. La prueba T42 lo verifica de la
forma más directa posible —`NDTR` llega a cero justo tras cuatro eventos de
update— y comprueba el resultado en el destino (`TIM4_CCR1` = 400).

*Limitación conocida:* si el controlador estuviera ocupado sirviendo otro stream
en el instante del pulso, la petición se perdería, mientras que el silicio la
mantiene hasta el reconocimiento. El arreglo definitivo es enrutar `ack_out` de
vuelta a los periféricos, que ya estaba anotado como pendiente en el entregable
del DMA; el efecto que quedaría sin él es análogo a un *overrun*, no un error
silencioso de datos.

### 4.5 Regla de un solo escritor

Como en el USART, el estado del temporizador lo modifican tanto sus procesos
internos (cuenta, captura, trigger, tiempo muerto) como el `b_transport` del
banco de registros, que corre en el proceso del maestro. Un único `pub_proc`
escribe **todas** las salidas —las cinco líneas de interrupción, `TRGO`, las
siete de DMA, los cuatro canales y las tres complementarias— desde miembros C++
ordinarios.

---

## 5. Verificación

Seis grupos nuevos, 118 comprobaciones, sobre el modelo completo del MCU.

### 5.1 T38 — Selección del tipo de temporizador (35 comprobaciones)

`static_assert` y accesores `constexpr` para la selección en compilación; la
tabla de máscaras de §1.5 leída **por el bus** para las seis familias; la
anchura de `CNT`/`ARR`/`CCRx` de 16 y 32 bits; y una variante construida en
ejecución que no existe en el F407 (32 bits, 2 canales), a la que se accede por
su propio maestro de bus.

### 5.2 T39 — Base de tiempos (22 comprobaciones)

Prescaler e interpolación de `CNT`; diez desbordamientos en un milisegundo con
`ARR` = 99; `UIF` como `rc_w0`; `UDIS` inhibiendo el evento de update y `URS`
distinguiendo el `UG` por software del desbordamiento; **precarga de `ARR`**: con
`ARPE` el valor nuevo se lee ya en el registro pero el contador sigue usando el
antiguo hasta el siguiente evento (y sin `ARPE` el cambio es inmediato); conteo
descendente; alineado al centro con su cambio de sentido en `ARR`; un solo pulso
en el TIM6 básico; y el contador de repeticiones de TIM1, que con `RCR` = 3
convierte diez desbordamientos en dos eventos de update.

### 5.3 T40 — PWM medido en el pin (23 comprobaciones)

TIM4_CH1 sale por **PD12**, que en la placa Discovery es el LED verde. El banco
de pruebas mide el pin con su propio «osciloscopio» (`measure_pwm`), a través del
pad eléctrico:

```
PD12: periodo = 1000.0 us, alto = 250.0 us (25.0 %)
```

Se comprueban el periodo, el ciclo de trabajo, su cambio al escribir `CCR1`, la
inversión por `CC1P` y que el LED sigue al PWM sin que la CPU toque el puerto.
Después, sobre TIM1: `MOE` = 0 deja las salidas en alta impedancia; `MOE` = 1 las
habilita; `CH1` (PA8) y `CH1N` (PA7) son complementarias; con `DTG` = 64 —4 µs a
16 MHz— **las dos quedan apagadas durante el tiempo muerto** y la complementaria
enciende después; y un nivel bajo en `BKIN` (PA6, gobernado por un driver
externo del banco) levanta `BIF`, pone `MOE` a cero, suelta los pines y activa la
IRQ 24.

### 5.4 T41 — Captura, cadena ITRx y codificador (13 comprobaciones)

Una **pista de placa** (`SignalLink`, ampliado en esta fase para poder soldarse
y quitarse) lleva el PWM de PD12 a PB4, que es TIM3_CH1. TIM3 captura los flancos
y mide el periodo:

```
capturas en TIM3_CH1: 997 y 1997 -> periodo = 1000 us
```

Se comprueba que leer `CCR1` borra `CC1IF` como en el silicio, y que dos capturas
sin leer levantan `CC1OF`. Después, la **cadena maestro-esclavo**: TIM2 con
`MMS` = update alimenta ITR1 de TIM3, que en modo de reloj externo 1 cuenta 10
disparos en 1 ms; en **modo gated** con `MMS` = OC1REF y un ciclo del 50 %, TIM3
solo avanza 499 µs de cada 1000; y en **modo trigger**, TIM3 arranca solo cuando
llega el flanco (el hardware pone `CEN` a uno). Por último el **codificador
incremental** en modo 3, con dos drivers externos generando la cuadratura sobre
PB4 y PB5: ocho flancos hacia delante llevan `CNT` de 1000 a 1008 y ocho hacia
atrás lo devuelven a 1000.

### 5.5 T42 — Interrupciones, TRGO y DMA (17 comprobaciones)

Vector único de TIM3 (IRQ 29) y su retirada al borrar `UIF`; los **cuatro
vectores separados** de TIM1, comprobando que el update va al 25 y la
comparación al 27; que TIM10 llega **al mismo vector 25** por la puerta OR
[IR, §9.1.2]; el pulso de TRGO de TIM6 que dispara el DAC; la transferencia por
DMA descrita en §4.4; y el **modo ráfaga**, con `DBA` apuntando a `CCR1` y
`DBL` = 3: cuatro accesos a `DMAR` escriben `CCR1`..`CCR4` y el quinto vuelve al
principio.

### 5.6 T43 — Firmware con CMSIS (8 comprobaciones)

`verif/fw/tim_demo/main.c` se compila con la cabecera de dispositivo de ST y el
CMSIS-Core oficiales, sin ninguna adaptación al modelo, y gobierna **cuatro
familias a la vez** con el mismo driver mínimo de cinco líneas
(`PSC`/`ARR`/`EGR`/`CEN`):

```c
static void tim_base(TIM_TypeDef *t, uint32_t psc, uint32_t arr)
{
    t->CR1  = 0u;
    t->PSC  = psc;
    t->ARR  = arr;
    t->EGR  = TIM_EGR_UG;            /* carga PSC y ARR, reinicia el contador */
    t->SR   = 0u;
    t->CR1  = TIM_CR1_CEN;
}
```

TIM4 genera el PWM de 1 kHz al 25 % sobre el LED; TIM3 lo captura por la pista de
placa y mide su periodo; TIM2 corre libre a 84 MHz demostrando que su contador
pasa de `0xFFFF`; y TIM7 interrumpe cada milisegundo. Que el mismo código sirva
para el básico, para los de 16 bits y para el de 32 es justamente lo que
comprueba la parametrización:

```
TIMCLK1 = 84000000 Hz | interrupciones de update = 10 | periodo capturado = 1000 us |
CNT de 32 bits = 0x000CD56D | 159456 instrucciones
```

El firmware calcula `TIMCLK1` = 2 × `PCLK1` = 84 MHz porque el prescaler del APB1
no es 1, y el modelo se lo confirma [IR, §4.4].

### 5.7 Resultado de la suite

```
Resumen F1: 124 comprobaciones OK, 0 fallos
Resumen F2: 12 comprobaciones OK, 0 fallos
Resumen F3: 80 comprobaciones OK, 0 fallos
Resumen F4 (DMA): 71 comprobaciones OK, 0 fallos
Resumen F4 (USART): 70 comprobaciones OK, 0 fallos
Resumen F4 (TIM)  : 118 comprobaciones OK, 0 fallos
TOTAL     : 475 comprobaciones OK, 0 fallos
```

Sin regresiones en las fases anteriores. Tiempo simulado 822 ms; ~2,5 s de CPU.

---

## 6. Decisiones de diseño

1. **Un modelo, seis tipos.** Alternativa descartada: seis clases con herencia.
   Los rasgos del F407 no forman una jerarquía (TIM9 tiene esclavo sin ETR;
   TIM6 tiene DMA y TRGO sin canales; TIM2 tiene 32 bits sin ser avanzado), y
   una jerarquía obligaría a duplicar código o a herencia múltiple.
2. **Los rasgos gobiernan las máscaras de escritura.** Que un TIM10 lea cero en
   `SMCR` no es un detalle estético: es lo que hace que un driver que sondea
   registros —o un test de firmware— se comporte como en el silicio.
3. **Contar por tiempo y no por flancos.** Justificado en §4.1; es lo que
   permite tener catorce temporizadores instanciados sin coste y lo que los hace
   compatibles con la supresión de formas de onda de F2.
4. **Cinco salidas de interrupción en todas las instancias.** El rasgo
   `split_irq` decide cuáles se activan; el netlist deja sin conectar las que
   cada familia no usa. La alternativa —puertos condicionales— no existe en
   SystemC, donde todo puerto debe quedar enlazado.
5. **La matriz ITRx en el netlist, no en el modelo.** §4.3. Además de ser lo
   correcto arquitectónicamente, aísla al modelo de un dato que [IR] no aporta.
6. **La petición de DMA como pulso.** §4.4, con su limitación documentada.
7. **`SignalLink` con interruptor.** Una pista de placa permanente entre PD12 y
   PB4 estorbaría a las pruebas de GPIO de F3 que usan esos pines. Soldarla y
   quitarla es lo que haría un ingeniero con una placa de evaluación, y mantiene
   la convención de que todo lo que hay fuera del encapsulado vive en `verif/`.

---

## 7. Trabajo pendiente

De este bloque, para fases posteriores:

* **Filtro digital de entrada** (`ICxF`, `ETF`): el modelo aplica polaridad y
  prescaler `ICxPSC`, pero no filtra por número de muestras. Afecta solo a
  entradas con rebotes, que el banco de pruebas no genera todavía.
* **Precarga de la configuración de canal** (`CCPC`/`CCUS` y el evento COM):
  `COMIF`/`COMG` funcionan, pero la transferencia diferida de `CCxE`/`CCxNE`/
  `OCxM` al llegar el evento COM no está modelada. Es lo que usa el control de
  motores sin escobillas con sensores Hall, junto con `TI1S`, ya implementado.
* **Rearme automático tras el freno** (`AOE`): el bit es escribible y el freno
  actúa, pero `MOE` no se restaura solo en el siguiente evento de update.
* **`ETPS` (prescaler de ETR)** y el escalón de `SMCR.MSM`.
* **Enrutado del `ack` del DMA** de vuelta a los periféricos (§4.4), común con
  el pendiente del entregable del DMA.
* **Registros `DBGMCU_APBx_FZ`**: el puerto `freeze` está cableado y el contador
  lo respeta; falta que el bloque de depuración de F6 los exponga al software.
* **Salidas complementarias de TIM8 en pines de los puertos E/H**, no presentes
  en el encapsulado LQFP100 modelado.

Del resto de la fase F4 queda **EXTI/SYSCFG**, último entregable.
