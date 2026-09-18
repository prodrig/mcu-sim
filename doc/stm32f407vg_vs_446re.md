# STM32F407VG frente a STM32F446RE

Análisis elemento a elemento de las dos piezas, y plan para modelar el F446RE
reutilizando el máximo posible del modelo que ya existe.

---

## 0. Cómo leer este documento, y qué crédito darle

**Cada afirmación lleva su fuente.** Las etiquetas son:

| | |
| :--- | :--- |
| `[RM0090]` | Reference manual del F405/407/415/417/427/437/429/439, rev. 18 |
| `[RM0390]` | Reference manual del F446, rev. 4 |
| `[PM0214]` | Cortex-M4 programming manual, rev. 10 |
| `[DS8626]` | Datasheet del F405xx/407xx |
| `[DS10693]` | Datasheet del F446xC/E |
| `[AN4658]` | Nota de migración F429/439 → F446 |
| `[CMSIS]` | Cabeceras de dispositivo de ST, `stm32f407xx.h` y `stm32f446xx.h` |
| `[PINDATA]` | `STM32_open_pin_data` de ST (la base de datos de CubeMX) |
| `[IR]` | El informe técnico interno de este proyecto |
| **⚠ SIN VERIFICAR** | No se ha podido contrastar; se dice qué documento lo cerraría |

Las revisiones de los RM consultadas **no son las últimas** (RM0390 va por la 9,
de febrero de 2026, y aquí se ha leído la 4; RM0090 va por la 22 y se ha leído
la 18). Para los datos estructurales que usa este documento —mapa de memoria,
tabla de vectores, geometría de Flash— eso no debería importar, y además todos
ellos están corroborados por las cabeceras CMSIS de ST, que son código del
propio fabricante. Donde hay discrepancia entre documentos de ST se dice cuál y
no se elige por nuestra cuenta.

> **ESTADO: la fase 0 del plan está EJECUTADA.** Los siete puntos que este
> documento dejaba sin verificar se han cerrado leyendo los PDF completos —no
> resúmenes— y los resultados están en la **§15**, al final. Seis se confirmaron,
> **uno resultó estar mal** y cambió el código. Las secciones de más arriba ya
> llevan incorporado lo verificado.

---

## 1. Resumen ejecutivo: tres frases

**El F446 no es un F407 con otros números.** Comparten núcleo, mapa de buses,
tabla de funciones alternativas y la inmensa mayoría de los periféricos, pero el
F446 tiene **otro árbol de reloj** (tres PLL en vez de dos, con selectores de
fuente que en el F407 no existen), **seis bloques que el F407 no tiene**
(SAI×2, SPDIF-RX, QUADSPI, FMPI2C1, HDMI-CEC, y el FMC con SDRAM) y **pierde
tres que sí tiene** (Ethernet, RNG y la CCM RAM).

**Por eso el F446 no cabe en un `McuCaps`.** Ese descriptor
[`doc/stm32f407vg_reutilizacion.md`] distingue chips de la misma familia —un
F405 de un F407— cambiando números. Describir un F446 así lo convertiría en un
F407 con etiqueta falsa: el firmware programaría `RCC_DCKCFGR` y el modelo lo
ignoraría en silencio.

**Pero se reutiliza casi todo.** De las ~33.000 líneas de modelo (sin contar la
suite), **unas 27.000 valen tal cual o con retoques menores**: el núcleo entero,
las memorias, los buses, los pines, el netlist de placa, la depuración y quince
de los veinte periféricos. Lo que hay que escribir de nuevo son ~4.000 líneas de
periféricos nuevos y **una reescritura del RCC**, que es el trabajo de verdad.

---

## 2. Las dos piezas, de un vistazo

| | STM32F407VG | STM32F446RE | Fuente |
| :--- | :--- | :--- | :--- |
| Núcleo | Cortex-M4F r0p1 | Cortex-M4F r0p1 | `[CMSIS]` `__CM4_REV 0x0001` en ambos |
| SYSCLK máx. | **168 MHz** | **180 MHz** (con over-drive) | `[RM0090]` §7.2 / `[RM0390]` §6.2 |
| Flash | 1 MB, 12 sectores | 512 KB, 8 sectores | `[RM0090]` Tabla 5 / `[RM0390]` Tabla 4 |
| SRAM sistema | 192 KB (112+16+**64 CCM**) | **128 KB** (112+16), sin CCM | `[RM0090]` §2.3.1 / `[RM0390]` §2.2.3 |
| SRAM backup | 4 KB | 4 KB | `[CMSIS]` `BKPSRAM_BASE 0x40024000` en ambos |
| Líneas de IRQ | **82** (posiciones 0–81) | **97 posiciones (0–96), 86 implementadas** | `[RM0090]` §12.1.1 / `[RM0390]` §10.1.1 y Tabla 38 |
| Encapsulado analizado | LQFP100, 82 E/S | LQFP64, **50 E/S** | `[PINDATA]`, `[DS]` de cada uno |
| PLL | 2 (PLL, PLLI2S) | **3** (PLL, PLLI2S, PLLSAI) | `[RM0090]` §7.2.3 / `[RM0390]` §6.2.3 |
| Maestros de la matriz | 8 `[IR, §6.1.2]` | **7** (se va el del Ethernet) | modelo / `[RM0390]` §2.1 ⚠ |

---

## 3. El núcleo y el SCS

### 3.1 Idénticos, y eso es la mejor noticia de este documento

| Elemento | Veredicto | Fuente |
| :--- | :--- | :--- |
| Juego de instrucciones, modelo de excepciones | idéntico | mismo núcleo con licencia |
| FPU | FPv4-SP, simple precisión, en los dos | `[PM0214]`; `[DS]` de ambos |
| MPU | 8 regiones, unificado, en los dos | `[PM0214]` `MPU_TYPE.DREGION = 0x08` |
| Bits de prioridad | **4** (16 niveles) en los dos | `[RM0090]` §12.1.1, `[RM0390]` §10.1.1, `[CMSIS]` `__NVIC_PRIO_BITS 4U` |
| CPUID | `0x410FC241` en los dos | `[PM0214]` §4.4.2 |
| SysTick | idéntico, incluida la calibración fija a 18750 | `[RM0090]` §12.1.2 y `[RM0390]` §10.1.2, texto literalmente igual |
| SCB | sin diferencias documentadas | los dos RM remiten a `[PM0214]` |
| Bit-banding | idéntico, mismas bases `0x2200_0000` y `0x4200_0000` | `[RM0090]` §2.3.3, `[RM0390]` §2.2.5 |

**Consecuencia para el modelo: `core/` se reutiliza entero**, sus 5.459 líneas,
sin tocar una. `cpu.h`, `cpu_exec16.h`, `cpu_exec32.h`, `cpu_state.h`, `fpu.h`,
`scs.h`, `debug.h` y `cortex_m4f.h` describen el Cortex-M4F, no el F407. Eso ya
se sabía —es la tesis de `doc/stm32f407vg_reutilizacion.md`— pero ahora está
contrastado contra los dos reference manuals.

### 3.2 Lo único que cambia: cuántas líneas cuelgan del NVIC

Y ahí hay **una discrepancia entre dos documentos de ST** que conviene tener
delante:

* `[RM0390]` §10.1.1 dice **«96 maskable interrupt channels»**;
* `[DS10693]` §3.11 dice **«up to 91 maskable interrupt channels»**;
* la tabla de vectores `[RM0390]` Tabla 38 llega a la **posición 96** y tiene
  **once huecos reservados** (61, 62, 79, 80, 82, 83, 85, 86, 88, 89, 90), lo
  que deja **86 líneas implementadas**;
* el `IRQn_Type` de `[CMSIS]` `stm32f446xx.h` tiene **86 entradas**, que cuadra
  con la tabla.

**Lo que este modelo debe usar: 97 posiciones (0–96), de las cuales 86
implementadas.** La tabla por posición es el único documento autoconsistente, y
además coincide con la cabecera de ST. El número de portada de cada documento no
se usa para nada.

Y aquí el modelo ya está preparado: `CoreCaps::n_irq` es un dato desde el
refactor anterior, y el NVIC dimensiona sus vectores con él. **Los huecos
reservados no necesitan nada especial**: una línea que nadie gobierna nunca se
pone pendiente, que es exactamente lo que hace una posición reservada.

> ✅ **VERIFICADO (fase 0).** La tabla de `[RM0390]` §10.1.3 se ha leído entera,
> fila a fila: posiciones **0 a 96**, con las once reservadas **exactamente en
> 61, 62, 79, 80, 82, 83, 85, 86, 88, 89 y 90**, cada una con su fila «-  -
> Reserved» y su dirección de vector. **86 implementadas.** Coincide al cien por
> cien con el `IRQn_Type` de ST. Detalle en §15.3.

---

## 4. Las memorias

### 4.1 La Flash

Misma base, mismo controlador, misma ART, **otra geometría**:

| | F407VG (1 MB) | F446RE (512 KB) |
| :--- | :--- | :--- |
| Sectores | 12 | 8 |
| 0–3 | 16 KB | 16 KB |
| 4 | 64 KB | 64 KB |
| 5–7 | 128 KB | 128 KB |
| 8–11 | 128 KB | *(no existen)* |
| Fin | `0x080F_FFFF` | `0x0807_FFFF` |

`[RM0090]` Tabla 5 y `[RM0390]` Tabla 4; corroborado por `FLASH_END` en
`[CMSIS]` y por `[AN4658]` Tabla 5.

**Esto ya está resuelto.** Es exactamente `FLASH_512K`, la geometría que se
añadió para los `STM32F4x7xE`: `mem/mem_caps.h` la tiene escrita y T128 la
comprueba. El F446RE la reutiliza sin cambiar nada.

**La ART y los registros de Flash son iguales**: 64 líneas de caché en I-Code y
8 en D-Code, mismos bits `PRFTEN/ICEN/DCEN/ICRST/DCRST` en `FLASH_ACR`
(`[RM0090]` §3.5.2, `[RM0390]` §3.4.2, texto equivalente). `mem/flash_if.h`
—566 líneas— se reutiliza tal cual.

**La curva de estados de espera es la misma hasta 150 MHz y solo se separa
arriba:**

| LATENCY | F407 (2,7–3,6 V) | F446 (2,7–3,6 V) |
| ---: | :--- | :--- |
| 0–4 | 0–150 MHz, **idénticos tramo a tramo** | idénticos |
| 5 | 150 < HCLK ≤ **168** | 150 < HCLK ≤ **180** |

`[RM0090]` Tabla 10 / `[RM0390]` Tabla 5. Esto **también está resuelto**: la
regla genérica `MapaFlash::latencia_minima()` es «un estado de espera más cada
30 MHz, con un techo», y para el F446 son los mismos 30 MHz con el techo en 5.
El escalón no cambia; cambia el tope de frecuencia, que va en `LimitesReloj`.

### 4.2 La RAM, y la baja que sí duele

| Bloque | F407VG | F446RE |
| :--- | :--- | :--- |
| SRAM1 | 112 KB @ `0x2000_0000` | **igual** |
| SRAM2 | 16 KB @ `0x2001_C000` | **igual** |
| **CCM** | **64 KB @ `0x1000_0000`** | **NO EXISTE** |
| BKPSRAM | 4 KB @ `0x4002_4000` | **igual** |

Verificado por tres caminos independientes: `CCMDATARAM_BASE` está en
`stm32f407xx.h` y **no está** en `stm32f446xx.h` `[CMSIS]`; `[PINDATA]` tiene
`<CCMRam>64</CCMRam>` para el F407V y **ningún elemento CCMRam** para el F446R;
y `[RM0390]` §2.2.3 dice «up to two blocks: SRAM1 and SRAM2» sin mencionar CCM
en ninguna parte.

**Esto también está resuelto**: `MapaRam` ya admite `ccm_size = 0`, la matriz
deja de decodificar ese rango y el cargador de imágenes deja de aceptarlo. Es
literalmente el caso `RAM_LAB_64K` que T127 comprueba.

**Lo que hay que mirar con cuidado, y no es el tamaño:** la CCM del F407 cuelga
del **D-bus del núcleo y de nadie más** —ningún DMA llega a ella—, y eso está
modelado en el router de `core/cortex_m4f.h` (`is_ccm()` → puerto `ccm`) y en la
matriz (código de retorno `-2`). Con `ccm_size = 0` esos caminos se apagan solos,
pero **hay que comprobar que el `-2` no se confunde con el `-1`**: en un F446, la
ventana `0x1000_0000` no es «la CCM a la que este maestro no llega», es espacio
reservado. Es una prueba de una línea y un error de los que no se ven.

### 4.3 Memoria de sistema, OTP y bytes de opción: idénticos

`0x1FFF_0000` + 30 KB, `0x1FFF_7800` + 528 B, `0x1FFF_C000` + 16 B, en los dos
(`[RM0090]` Tabla 5, `[RM0390]` Tabla 4, `[CMSIS]`). Sin trabajo.

---

## 5. Los buses

### 5.1 La matriz

El modelo implementa **8 maestros × 7 esclavos** `[IR, §6.1.2]`:

```
CORE_IBUS, CORE_DBUS, CORE_SBUS, DMA1_MEM, DMA2_MEM, DMA2_PERIPH,
ETH_DMA, OTG_HS_DMA                                    -> 8 maestros
FLASH_ICODE, FLASH_DCODE, SRAM1, SRAM2, AHB1_SEG,
AHB2_SEG, FSMC_EXT                                     -> 7 esclavos
```

Para el F446, `[RM0390]` §2.1 dice **«Seven masters»**, y la resta cuadra
exactamente: **se va `ETH_DMA` y no se va nada más.** Los índices de las
subsecciones del RM lo corroboran desde el otro lado —`[RM0090]` §2.1 tiene una
subsección «Ethernet DMA bus» que `[RM0390]` §2.1 no tiene—, y la numeración
salta en consecuencia.

> ✅ **VERIFICADO (fase 0), y con un hallazgo.** `[RM0090]` §2.1 dice
> literalmente «Eight masters» y «Seven slaves» para el F405xx/07xx, y la lista
> coincide uno a uno con el `enum BusMaster` del modelo. `[RM0390]` §2.1 dice
> «Seven masters» y «Seven slaves» para el F446. Pero el séptimo esclavo **no es
> el mismo**: en el F407 es `FSMC`, y en el F446 es **«FMC / QUADSPI»** — los dos
> comparten un único puerto de esclavo de la matriz. Eso no estaba en este
> documento y cambia el plan: el QUADSPI **no añade un octavo esclavo**. Detalle
> en §15.1.

**Trabajo previsto: quitar un maestro de un `enum` y una fila de la máscara de
conectividad.** La máscara ya es un dato (`build_connectivity()`), así que lo
que hace falta es que sea **un dato por chip** y no una función fija — el mismo
movimiento que se le hizo al encapsulado.

### 5.2 Las fronteras de segmento: idénticas

APB1 `0x4000_0000`, APB2 `0x4001_0000`, AHB1 `0x4002_0000`, AHB2 `0x5000_0000`,
AHB3 `0xA000_0000`, y los alias de bit-banding en los mismos sitios `[CMSIS]`.
`bus/ahb_decoder.h` y `bus/bitband.h` se reutilizan sin tocar.

La única novedad estructural en AHB3 es que **el F446 mete la página de
registros del QUADSPI en `0xA000_1000`**, dentro del mismo segmento `[CMSIS]`.

### 5.3 La trampa de direcciones, que es UNA y hay que verla

De todos los periféricos que existen en ambas piezas, **ninguno cambia de
dirección** — se comprobó comparando las dos cabeceras de ST macro a macro. Pero
hay **una dirección que se reutiliza para otra cosa**:

| Dirección | F407VG | F446RE |
| :--- | :--- | :--- |
| `0x4000_4000` | **I2S3ext** | **SPDIF-RX** |
| `0x4000_3400` | **I2S2ext** | *reservado* |

`[CMSIS]`. Los bloques de extensión `I2S2ext`/`I2S3ext` —los que dan el I2S
full-duplex en el F407— **desaparecen en el F446** (`[AN4658]` los lista
explícitamente como bajas), y uno de sus dos huecos lo ocupa un periférico
completamente distinto.

**Es el error más caro que puede colarse en este puerto**, porque no falla: un
firmware que escriba en `0x4000_4000` esperando el I2S3ext encontraría el
SPDIF-RX y al revés. Tiene que ser una prueba explícita.

---

## 6. Los relojes: aquí está el trabajo de verdad

### 6.1 Los osciladores no cambian

| | F407 | F446 |
| :--- | :--- | :--- |
| HSE | 4–26 MHz, arranque típ. 2 ms | **igual** |
| HSI | **16 MHz**, arranque típ. 2,2 µs | **igual, sigue siendo 16 MHz** |
| LSI | 32 kHz típ., arranque típ. 15 µs | **igual** |
| LSE | 32,768 kHz, arranque típ. 2 s | **igual** |

`[DS8626]` tablas 32–35 y `[DS10693]` tablas 39–42. **`rcc/osc_pll.h` se
reutiliza en su parte de osciladores sin tocar nada.**

### 6.2 Los PLL: de dos a tres, y un divisor nuevo

| | F407 | F446 |
| :--- | :--- | :--- |
| PLL principal | M, N, **P, Q** | M, N, **P, Q, R** |
| PLLI2S | **solo N y R** (M compartido con el PLL) | **M, N, P, Q, R** propios |
| PLLSAI | **no existe** | **existe** (M, N, P, Q; sin R, porque no hay LTDC) |

`[RM0090]` §7.2.3 frente a `[RM0390]` §6.2.3, corroborado por los campos de
`RCC_PLLCFGR` en `[CMSIS]` (`PLLR` en bits 30:28 solo en el F446).

Uso de las salidas: en el F407, P → SYSCLK y Q → los 48 MHz del USB FS / RNG /
SDIO. En el F446, P → SYSCLK, Q → 48 MHz o SDIO, y **R → I2S1/I2S2, SPDIF-RX o
el reloj de sistema**.

### 6.3 Los selectores que en el F407 no existen

Esto es lo que hace que el F446 **no** quepa en un descriptor:

| Registro / campo | Qué hace | ¿En el F407? |
| :--- | :--- | :---: |
| `RCC_DCKCFGR2.CK48MSEL` | elige la fuente de los 48 MHz: `PLL_Q` o `PLLSAI_P` | **no existe** |
| `RCC_DCKCFGR2.SDIOSEL` | reloj del SDIO: los 48 MHz o el de sistema | **no existe** |
| `RCC_DCKCFGR.SAI1SRC` / `SAI2SRC` | cuatro fuentes por bloque SAI | **no existe** |
| `RCC_DCKCFGR.I2S1SRC` / `I2S2SRC` | fuente de I2S **por dominio APB** | el F407 tiene un único `I2SSRC` global |
| `RCC_DCKCFGR2.SPDIFRXSEL` | `PLL_R` o `PLLI2S_P` | **no existe** |
| `RCC_DCKCFGR2.CECSEL` | LSE o HSI/488 | **no existe** |
| `RCC_DCKCFGR2.FMPI2C1SEL` | APB, SYSCLK o HSI | **no existe** |
| `RCC_DCKCFGR.TIMPRE` | multiplicador del reloj de los temporizadores | **no existe**: en el F407 la regla es fija |

La cadena `CK48MSEL` **no aparece ni una vez** en `[RM0090]`, y
`stm32f407xx.h` **no tiene ningún registro `RCC_DCKCFGR`** `[CMSIS]`.

Un detalle que conviene no exagerar: **el reloj de I2S desde pin externo no es
nuevo.** El F407 ya lo tiene (`RCC_CFGR.I2SSRC`, con el pin `I2S_CKIN`,
`[RM0090]` §7.2). Lo nuevo en el F446 es **elegirlo por separado para cada
dominio** y que además haya opciones de `PLL_R` y de HSI/HSE.

### 6.4 Los topes por dominio

| | F407 | F446 sin over-drive | F446 con over-drive |
| :--- | ---: | ---: | ---: |
| HCLK | 168 MHz | 168 MHz | **180 MHz** |
| PCLK1 | 42 MHz | 42 MHz | **45 MHz** |
| PCLK2 | 84 MHz | 84 MHz | **90 MHz** |

`[DS8626]` Tabla 14 y `[DS10693]` Tabla 16. **Los topes del F446 dependen de un
bit del PWR**, cosa que en el F407 no pasa: es la primera vez que
`LimitesReloj` tendría que ser función del estado y no una constante.

### 6.5 El veredicto sobre el RCC

`rcc/rcc.h` son **930 líneas** y `rcc/osc_pll.h` otras 291. De ellas:

* **los osciladores (291 líneas) se reutilizan enteros**;
* **el gating y el reset por periférico se reutilizan**, ampliando el `enum`;
* **el árbol de reloj hay que rehacerlo**: tercer PLL, divisor R, cinco
  registros de selección que no existen y el acoplamiento con el over-drive.

Estimación: **~400 líneas nuevas y ~300 modificadas** sobre el RCC. Es el bloque
más caro del puerto y el que más pruebas va a necesitar, porque un árbol de
reloj mal modelado **no falla: da la frecuencia equivocada**, y eso se manifiesta
como un UART con baudios raros tres capas más arriba.

---

## 7. La alimentación: el over-drive

El F446 tiene un modo que el F407 sencillamente no tiene.

| | F407 | F446 |
| :--- | :--- | :--- |
| `PWR_CR.VOS` | **1 bit**: escala 1 o 2 | **2 bits**: escalas 1, 2 y 3 |
| `PWR_CR.ODEN` / `ODSWEN` | **no existen** | over-drive |
| `PWR_CSR.ODRDY` / `ODSWRDY` / `UDRDY` | **no existen** | banderas del over/under-drive |
| `PWR_CR.MRUDS` / `LPUDS` | **no existen** | under-drive en deep-sleep |
| Modos Stop | los de siempre | **seis variantes** `[RM0390]` Tabla 17 |

`[RM0390]` PWR_CR frente a `[RM0090]` PWR_CR, corroborado por `[CMSIS]`:
`PWR_CR_VOS` es una **máscara de un bit** en `stm32f407xx.h` y de dos en
`stm32f446xx.h`, y `PWR_CR_ODEN` no existe en el primero.

**Y hay una secuencia que el modelo tiene que reproducir**, porque un firmware
real la ejecuta y se cuelga si el modelo no la acompaña (`[RM0390]` §5.1.3):

1. seleccionar HSI o HSE como SYSCLK;
2. configurar y arrancar el PLL;
3. poner `ODEN` y **esperar `ODRDY`**;
4. poner `ODSWEN` y **esperar `ODSWRDY`** — *el reloj de sistema se detiene
   durante la conmutación*;
5. fijar la latencia de Flash y los divisores;
6. esperar el enganche del PLL y conmutar SYSCLK.

Si el modelo no levanta `ODRDY`, el firmware se queda en el paso 3 **para
siempre**, exactamente igual que se quedaba esperando `HSERDY` en el caso I-32.
Es el mismo tipo de fallo y merece el mismo cuidado.

`periph/pwr.h` son 427 líneas: **se reutiliza la estructura y hay que añadir
~120 líneas** de over-drive, under-drive y la tercera escala.

---

## 8. Los periféricos, uno a uno

### 8.1 Idénticos: se reutilizan sin tocar

Todos a la misma dirección y con el mismo IP `[CMSIS]`, `[PINDATA]`:

| Bloque | Líneas del modelo | Nota |
| :--- | ---: | :--- |
| Temporizadores (14: 2 avanzados, 2 de 32 bits, 8 de 16, 2 básicos) | 1.102 | mismos bloques, mismas direcciones |
| USART ×4 + UART ×2 | 582 | idénticos |
| I2C clásico ×3 | 928 | idénticos, IP `i2c1_v1_5` en los dos |
| ADC ×3 de 12 bits + común | 895 | 16 canales externos en los dos encapsulados |
| DAC, 2 canales | 534 | idéntico |
| bxCAN ×2 | 1.163 | mismas direcciones |
| RTC | 523 | mismo IP, `rtc2_v2_3` en los dos |
| Watchdogs (IWDG + WWDG) | 387 | idénticos |
| DMA1/DMA2, 8 flujos cada uno | 551 | bloque byte a byte igual en las dos cabeceras |
| EXTI | 299 | idéntico |
| GPIO | 215 | idéntico |
| SYSCFG | 122 | ojo con `MEMRMP`, que gana opciones de FMC |
| CRC | *(en `crc_rng.h`)* | idéntico |
| SDIO | 809 | mismo nombre y dirección; el F446 pierde CE-ATA `[AN4658]` |
| DCMI | 609 | **presente en los dos**, misma dirección |
| USB OTG FS + HS (con ULPI) | 1.578 | mismas direcciones; el F446 tiene más endpoints en FS |

**Subtotal reutilizable sin cambios: ~10.300 líneas.**

Dos matices que no cambian el veredicto pero conviene anotar:

* **El DCMI está en las dos piezas.** Es fácil suponer lo contrario porque el
  F405 no lo tiene, pero el F446 sí: misma dirección `0x5005_0000` `[CMSIS]`.
* **El SDIO se sigue llamando SDIO** en el F446, no SDMMC `[CMSIS]`.

### 8.2 Bajas: se quedan fuera

| Bloque | Líneas | Por qué |
| :--- | ---: | :--- |
| **Ethernet MAC** | 1.162 | no existe en el F446 `[CMSIS]`, `[AN4658]` |
| **RNG** | *(parte de `crc_rng.h`)* | **no existe**: ningún símbolo `RNG` en `stm32f446xx.h` |
| **I2S2ext / I2S3ext** | *(en `spi.h`)* | los bloques de extensión desaparecen `[AN4658]` |

El RNG obliga a **partir `periph/crc_rng.h`**: el CRC se queda y el RNG se va. Son
405 líneas juntas hoy, y tenerlas en el mismo fichero fue cómodo mientras los dos
iban siempre juntos.

### 8.3 Altas: hay que escribirlas

| Bloque | Dirección | Vector | Esfuerzo estimado |
| :--- | :--- | ---: | :--- |
| **SAI1** (bloques A y B) | `0x4001_5800` | 87 | ~700 líneas. Es el más grande |
| **SAI2** | `0x4001_5C00` | 91 | comparte modelo con SAI1 |
| **SPDIF-RX** | `0x4000_4000` | 94 | ~400 líneas |
| **QUADSPI** | `0xA000_1000` | 92 | ~500 líneas |
| **FMPI2C1** | `0x4000_6000` | 95 / 96 | ~600 líneas. **Es otro IP**, no el I2C de siempre |
| **HDMI-CEC** | `0x4000_6C00` | 93 | ~300 líneas |
| **SPI4** | `0x4001_3400` | 84 | ~0: es otra instancia de `spi.h` |
| **I2S1** | *(sobre SPI1)* | — | pequeño, sobre el modelo que ya hay |
| **FMC con SDRAM** | `0xA000_0000` | 48 | ~600 líneas sobre las 825 del FSMC |

Direcciones verificadas en la fase 0 contra **`[RM0390]` Tabla 1 «register
boundary addresses»**, leída directamente: SAI1 `0x4001_5800`, SAI2
`0x4001_5C00`, SPI4 `0x4001_3400`, HDMI-CEC `0x4000_6C00`, SPDIF-RX
`0x4000_4000`, FMC `0xA000_0000`, QUADSPI `0xA000_1000`.

**Con una laguna, y conviene saberla:** la Tabla 1 de la revisión 4 **no lista
el FMPI2C1**. Deja sin nombrar el rango `0x4000_6000`–`0x4000_63FF`, entre el
I2C3 y el CAN1, que es exactamente donde `[CMSIS]` `stm32f446xx.h` pone
`FMPI2C1_BASE`. El capítulo 23 del mismo RM describe el periférico entero con
sus registros por desplazamiento, así que el bloque existe: lo que falta es su
fila en la tabla de fronteras. La dirección se toma de la cabecera de ST —que es
código del fabricante— y encaja en el único hueco disponible; **la revisión 9
debería confirmarla**.

**El FMPI2C merece un párrafo.** No es «el I2C con una velocidad más»: es el IP
nuevo de ST, el mismo que llevan las familias F0/F3/L4, con otro juego de
registros y otro modelo de temporización (`TIMINGR` en vez de `CCR`/`TRISE`).
`[RM0390]` le dedica **un capítulo aparte** (Ch. 23) del capítulo del I2C clásico
(Ch. 24), y `[PINDATA]` le da otra versión de IP (`i2c2_v1_1` frente a
`i2c1_v1_5`). Reutilizar `periph/i2c.h` para él sería un error de diseño.

**Y el FSMC → FMC no es solo un cambio de nombre.** El bloque base es el mismo
(`0xA000_0000`) pero **los desplazamientos de los registros por banco cambian**:
el F407 tiene `Bank2_3` en `+0x60` y `Bank4` en `+0xA0`; el F446 tiene `Bank3`
(NAND) en `+0x80` y `Bank5_6` (SDRAM) en `+0x140` `[CMSIS]`. Un modelo que
reutilice el decodificador del FSMC tal cual **respondería en las direcciones
equivocadas**.

> **Nota de alcance sobre el F446RE en concreto** *(corregida en la fase 0 —
> la versión anterior de este párrafo atribuía al datasheet algo que no dice)*:
>
> * el **FMC sí está en la tabla**: `[DS10693]` Tabla 2, fila «FMC memory
>   controller», pone **`No`** en las columnas MC/ME/RC/**RE** y `Yes` en las de
>   LQFP100 y LQFP144. En un F446RE **no hay bus externo**, y es el datasheet
>   quien lo dice;
> * el **SAI no**: esa misma tabla pone **`2`** en la fila del SAI para **todas**
>   las columnas, sin nota al pie que distinga el LQFP64. Las cuatro notas de la
>   tabla son otras (FMC en LQFP100, SPI/I2S exclusivos, QuadSPI limitado en
>   LQFP64, y el mínimo de VDD). Lo que ocurre con SAI2 y SPI4 es que **el die
>   los tiene y el LQFP64 no les saca pines**, cosa que dice `[PINDATA]`, no el
>   datasheet;
> * el **QUADSPI sí está**, con nota 3 literal: *«For the LQFP64 package the
>   Quad SPI is available with limited features»*.
>
> La distinción importa para el modelo: **FMC ausente** es un bloque que no está
> y cuya ventana es espacio reservado; **SAI2 y SPI4 sin pines** son bloques que
> sí están y a los que el firmware puede escribir. Son dos cosas distintas y el
> modelo las trata distinto — y es un buen argumento para modelar **el die del
> F446 y el encapsulado por separado**, como se hizo con la familia F405/407.

### 8.4 La tabla de vectores: dónde divergen

**Las posiciones 0 a 60 son idénticas**, con un único cambio de nombre: la
**48 es FSMC en el F407 y FMC en el F446** — misma ranura, IP renombrado.

De 61 en adelante:

| Pos. | F407VG | F446RE |
| ---: | :--- | :--- |
| 61, 62 | **ETH, ETH_WKUP** | *reservadas* |
| 63–78 | CAN2, OTG_FS, DMA2, USART6, I2C3, OTG_HS, DCMI | **iguales** |
| 79 | *reservada* | *reservada* |
| 80 | **RNG** | *reservada* |
| 81 | FPU | FPU |
| 82, 83 | *(el F407 acaba en 81)* | *reservadas* |
| **84** | — | **SPI4** |
| 85, 86 | — | *reservadas* |
| **87** | — | **SAI1** |
| 88–90 | — | *reservadas* |
| **91** | — | **SAI2** |
| **92** | — | **QUADSPI** |
| **93** | — | **CEC** |
| **94** | — | **SPDIF-RX** |
| **95, 96** | — | **FMPI2C1_EV, FMPI2C1_ER** |

De los `IRQn_Type` de `[CMSIS]`, contrastados con los SVD de ST y con `[AN4658]`.

**Dato tranquilizador: no hay ni una sola posición que cambie de dueño.** Lo que
el F407 tiene en una posición, o el F446 lo tiene igual, o lo tiene reservado.
Las altas del F446 van todas por encima de 81, donde el F407 no llega. Eso
significa que **el `enum` de IRQ del modelo se extiende, no se reescribe.**

---

## 9. Los pines

### 9.1 El encapsulado del F446RE

LQFP64, **50 E/S** `[DS10693]` Tabla 2, `[PINDATA]` `<IONb>50</IONb>`:

| Puerto | Cuántos | Cuáles |
| :--- | ---: | :--- |
| A | 16 | PA0–PA15 completo |
| B | **15** | PB0–PB10 y PB12–PB15 — **PB11 NO sale** |
| C | 16 | PC0–PC15 completo |
| D | **1** | **solo PD2** |
| H | 2 | PH0, PH1 |

`16+15+16+1+2 = 50`. ✓

Compárese con el LQFP64 del F405RG, que este modelo ya tiene: `A, B, C`
completos + `PD2` + `PH0/PH1` = **51**. **No son el mismo mapa**: el del F446
pierde PB11. Es exactamente la clase de detalle que la comprobación de
`Encapsulado::coherente()` caza —51 bits frente a 50— y la razón de que esa
comprobación exista.

**El die del F446 tiene GPIOA–GPIOH**, sin GPIOI `[CMSIS]`. Los puertos E, F y G
**existen en el mapa de memoria y se pueden encender desde el RCC**, pero no
tienen pines en el LQFP64 — que es justo la distinción entre die y encapsulado
que el modelo ya sabe hacer.

### 9.2 Las funciones alternativas: la mejor noticia del puerto

Se compararon las dos tablas de ST `[PINDATA]` par a par, por máquina y no a
ojo: **260 pares (pin, señal) son comunes a las dos piezas, y el número de AF
difiere en CERO de ellos.**

| AF | F407 | F446 |
| ---: | :--- | :--- |
| 0–2 | SYS/RTC, TIM1-2, TIM3-5 | **iguales** |
| 3 | TIM8–11 | iguales **+ CEC** |
| 4 | I2C1–3 | iguales **+ FMPI2C1, + CEC** |
| 5 | SPI1, SPI2, I2S2 | iguales **+ SPI3/I2S3, SPI4, I2S1** |
| 6 | SPI3, I2S2/3 | iguales **+ SAI1, SPI4** |
| 7 | USART1–3, **I2S3ext** | USART1–3 **+ SPI2/3, UART5, SPDIFRX** |
| 8 | UART4/5, USART6 | iguales **+ SAI2, SPDIFRX** |
| 9 | CAN1/2, TIM12–14 | iguales **+ QUADSPI** |
| 10 | OTG FS/HS | iguales **+ QUADSPI, SAI2** |
| **11** | **ETH (MII/RMII)** | **vacío** |
| 12 | **FSMC**, SDIO, OTG HS FS | **FMC**, SDIO, OTG HS FS |
| 13 | DCMI | DCMI |
| 15 | EVENTOUT | EVENTOUT |

**Tres consecuencias prácticas:**

1. cualquier constante de AF heredada del F407 **es correcta en el F446**;
2. los periféricos nuevos se meten en ranuras de AF **que estaban libres en esos
   mismos pines**: la tabla es un superconjunto, no una remezcla;
3. el único número que se vacía es el **AF11**, el del Ethernet.

Y un cambio de pin que hay que anotar porque es de los que se pasan: **ULPI_D4
está en PB11 en el F407 y en PB2 en el F446RE** `[PINDATA]` — porque PB11 no sale
en el LQFP64.

---

## 10. La depuración

Sin diferencias documentadas entre las dos piezas: mismo SWJ-DP, mismo AHB-AP,
mismo Core Debug, mismos FPB/DWT/ITM/TPIU, misma ROM table. Los dos RM remiten a
`[PM0214]` y ninguno declara particularidades.

Lo único que cambia de verdad es el **DBGMCU_IDCODE**, que lleva el identificador
del dispositivo y que un depurador lee para saber con qué está hablando — y que
es justamente lo que hizo fallar la conexión con STM32CubeIDE en su momento.

> ✅ **VERIFICADO (fase 0). Era el punto bloqueante y ya está cerrado.**
> `[RM0390]` §33.6.1: `DBGMCU_IDCODE` en `0xE004 2000`, **`DEV_ID = 0x421`** y
> `REV_ID = 0x1000` (revisión A) → el registro entero vale **`0x1000 0421`**.
> El F407 es `0x413` (`[RM0090]` §32.6.1) y el modelo devuelve `0x1001 6413`.
> El JTAG ID del boundary-scan del F446 es `0x0641 3041`. Detalle en §15.4.

`core/debug.h` (1.117 líneas), `core/gdb_stub_dap.h`, `common/gdb_rsp.h`,
`verif/gdb_stub.h` y `verif/swd_port.h` **se reutilizan enteros**: ~3.000 líneas
que no se tocan salvo esa constante.

---

## 11. Cuánto se reutiliza, en números

| Directorio | Líneas | Se reutiliza | Qué hay que hacer |
| :--- | ---: | :---: | :--- |
| `core/` | 5.459 | **100 %** | nada: es el Cortex-M4F |
| `common/` | 1.956 | ~95 % | el mapa de direcciones pasa a ser por familia |
| `bus/` | 467 | ~95 % | quitar el maestro del Ethernet |
| `mem/` | 864 | **100 %** | solo un `MapaFlash`/`MapaRam` nuevos, ya escritos |
| `pins/` | 1.103 | ~90 % | encapsulado nuevo (dato) + tabla de AF nueva |
| `parts/` | 5.360 | **100 %** | la placa no sabe qué chip lleva dentro |
| `verif/` | 1.170 | **100 %** | salvo el `IDCODE` |
| `periph/` | 13.976 | ~74 % | −1.162 (ETH) −RNG, +~3.100 nuevas |
| `rcc/` | 1.221 | ~50 % | el árbol de reloj se rehace |
| `top/` | 2.257 | ~60 % | otro netlist de integración |
| **Total modelo** | **33.833** | **≈ 80 %** | |

*(La suite, `top/sc_main.cpp`, son otras 14.502 líneas aparte.)*

**Trabajo nuevo estimado: ~4.500 líneas**, de las cuales ~3.100 son periféricos
que no existen y ~700 el RCC. Frente a las 33.800 del modelo actual, es
**alrededor de un 13 %**.

---

## 12. El plan

### Fase 0 — Cerrar lo que está sin verificar *(antes de escribir código)* — **HECHA, §15**

No es burocracia: cada uno de estos puntos, mal, produce un modelo que **funciona
y miente**, que es el fallo que este proyecto más ha perseguido.

1. Abrir `[RM0390]` rev. 9 y confirmar: §10.1.3 (tabla de vectores completa, con
   el etiquetado literal de las once reservadas), Tabla 1 (fronteras de
   registros), Figura 1 (**maestros y esclavos de la matriz**) y §33.6.1
   (**`DBGMCU_IDCODE`**).
2. Abrir `[RM0090]` rev. 22, Figura 1, y confirmar los 8×7 que este modelo
   implementa.
3. Resolver la discrepancia 96/91 líneas de IRQ, o dejar constancia de que se usa
   la tabla por posición y por qué.
4. Copiar la tabla de pines del LQFP64 del F446RE y comprobar que da 50.

### Fase 1 — Separar «familia F4» de «STM32F407» *(sin añadir un solo chip)* — **HECHA, §16**

El movimiento clave, y el que ya tiene precedente: es lo que se le hizo al
encapsulado.

* **`common/ahb_types.h` se parte**: lo que es ARMv7-M se queda; lo que es el
  mapa de periféricos del F407 se va a `soc/f407/mapa.h`.
* **La máscara de conectividad de la matriz** pasa de función a dato, igual que
  `Encapsulado`.
* **La tabla de funciones alternativas** (`bind_gpio_pins()` en el top) pasa a
  ser una tabla de datos —(puerto, pin, AF) → señal— en vez de 300 llamadas
  escritas a mano. Es la refactorización más aburrida del plan y la que más
  paga: sin ella, el F446 obliga a copiar y editar esas 300 líneas.
* **`periph/crc_rng.h` se parte** en `crc.h` y `rng.h`.
* **`mcu_if` y `FabricaMcu`**, que multi-MCU §6.1 ya tiene diseñados y que ahora
  sí hacen falta: con dos familias, `tipo=` tiene que despachar de verdad.

**Al final de la fase 1 el modelo hace exactamente lo que hace hoy**, con la
misma suite y el mismo tiempo simulado al picosegundo. Ese es el criterio de
aceptación, y es el que hace que la fase sea segura.

### Fase 2 — El esqueleto del F446, sin periféricos nuevos — **HECHA, §17**

Montar un `Stm32F446` que sea **el F407 menos lo que no tiene**: sin Ethernet,
sin RNG, sin CCM, con 512 KB de Flash, con el LQFP64 del F446 y con las 97
posiciones de vector.

Al acabar la fase 2, **un blinky compilado para F446RE debe arrancar y parpadear**
—usa GPIO, RCC básico y SysTick, y nada de eso ha cambiado—. Es un hito
comprobable y temprano, y vale la pena perseguirlo antes que ningún periférico
nuevo.

### Fase 3 — El RCC del F446 — **HECHA, §18**

El tercer PLL, el divisor R, `RCC_DCKCFGR` y `RCC_DCKCFGR2`, y el acoplamiento
con el over-drive del PWR. Con sus pruebas: **cada selector tiene que cambiar una
frecuencia observable**, no solo guardar un bit.

Criterio de aceptación honesto: el `SystemClock_Config()` que STM32CubeIDE genera
para una Nucleo-F446RE debe llegar a 180 MHz **pasando por la secuencia de
over-drive**, y debe colgarse si el modelo no levanta `ODRDY` — hay que
comprobar las dos cosas, la que funciona y la que debe fallar.

### Fase 4 — Los periféricos nuevos, por orden de utilidad docente

1. **FMPI2C1** — es el I2C moderno, el que el alumno se va a encontrar en
   cualquier familia posterior. El de más valor didáctico.
2. **QUADSPI** — memoria externa serie, cada vez más común.
3. **SAI1/SAI2** — audio; el más grande de los cinco.
4. **SPDIF-RX** y **HDMI-CEC** — los de menos recorrido; se pueden dejar
   declarados y sin modelar, **diciéndolo**, como se hizo con otros bloques.
5. **FMC con SDRAM** — sobre el FSMC que ya existe. En el F446**RE** no tiene
   pines, así que puede esperar sin bloquear nada.

### Fase 5 — Verificación

* La suite del F407 **no se toca y no puede moverse**: `2336217899213 ps`.
* Una suite paralela para el F446, con la misma estructura.
* **Una prueba cruzada** que compruebe lo que este documento dice: que
  `0x4000_4000` es I2S3ext en uno y SPDIF-RX en el otro, que el vector 80 es RNG
  en uno y está reservado en el otro, que PB11 sale en un LQFP64 y no en el otro.
  Es la prueba que evita que el puerto se coma al original.

### Orden de los hitos

| Hito | Qué demuestra |
| :--- | :--- |
| **H1** | La fase 1 acaba con la suite intacta y el invariante en su sitio — ✅ |
| **H2** | `sim placa.xml --mcu STM32F446RE` monta y valida — ✅ fase 2 |
| **H3** | Un blinky de CubeIDE para F446RE parpadea — ✅ fase 2 |
| **H4** | `SystemClock_Config()` llega a 180 MHz por over-drive — ✅ fase 3 |
| **H5** | El F446 se depura desde STM32CubeIDE con el `IDCODE` correcto |
| **H6** | Los periféricos nuevos, uno a uno, con su prueba |

---

## 13. Los tres riesgos, dichos por adelantado

**El RCC.** Es el único bloque que se rehace, y un árbol de reloj mal modelado no
falla: da la frecuencia equivocada. Se manifiesta tres capas más arriba como un
UART con baudios raros o un temporizador que va a destiempo, y para entonces
nadie mira el RCC. Mitigación: probar **frecuencias observables**, no bits.

**La tabla de funciones alternativas.** Son ~300 llamadas escritas a mano en el
top. Copiarlas y editarlas para el F446 es rápido, y es la peor idea del plan:
dos tablas que hay que mantener a la vez acaban discrepando. La fase 1 existe
sobre todo por esto.

**La tentación de meter el F446 en un `McuCaps`.** Es lo que parece barato desde
fuera, después del refactor de la familia F405/407. No lo es, y el motivo está en
la §6.3: un descriptor no puede describir cinco registros que no existen. Si se
intenta, el resultado es un modelo que acepta `RCC_DCKCFGR`, no hace nada con él
y no lo dice — que es exactamente la clase de mentira que este proyecto lleva
toda su vida quitando.

---

## 14. Lo que quedó sin verificar, y qué pasó con ello

La primera versión de este documento dejaba siete puntos abiertos. **La fase 0
del plan era cerrarlos antes de escribir una línea de código**, y ya está hecha.
Esta sección los enumera; la siguiente cuenta cómo se cerró cada uno.

| # | Punto | Estado |
| ---: | :--- | :--- |
| 1 | Esclavos de la matriz AHB, en las dos piezas | ✅ cerrado, **con hallazgo** |
| 2 | Los 8 maestros del F407 y los 7 del F446 | ✅ cerrado |
| 3 | Etiquetado «Reserved» de las once posiciones de vector | ✅ cerrado |
| 4 | `DBGMCU_IDCODE` del F446 *(bloqueante)* | ✅ cerrado: `0x421` |
| 5 | Discrepancia 96 / 91 líneas de IRQ | ✅ resuelto, **y es peor de lo que parecía** |
| 6 | `[RM0390]` Tabla 1, fronteras de registros | ✅ cerrado, **con una laguna** |
| 7 | Las revisiones leídas no son las vigentes | ⚠️ **sigue abierto** |

Y el cuarto punto del propio plan —contar los pines del LQFP64 del F446RE— **se
cerró y de paso destapó un error en otro sitio**.

---

## 15. Fase 0: cómo se cerró cada punto

**Método.** Se descargaron los PDF completos —RM0090 (1749 páginas), RM0390
(1328), DS8626, DS10693— y se extrajeron con `pdftotext -layout`, que preserva
las columnas de las tablas. Nada de resúmenes: las tablas se leyeron fila a
fila. Para los pines se usó además `STM32_open_pin_data`, la base de datos de
ST que alimenta CubeMX.

### 15.1 La matriz de buses, y el hallazgo que cambia el plan

`[RM0090]` §2.1, literal:

> «•  Eight masters: Cortex-M4 with FPU core I-bus, D-bus and S-bus / DMA1
> memory bus / DMA2 memory bus / DMA2 peripheral bus / **Ethernet DMA bus** /
> USB OTG HS DMA bus
> •  Seven slaves: Internal Flash memory ICode bus / Internal Flash memory
> DCode bus / Main internal SRAM1 (112 KB) / Auxiliary internal SRAM2 (16 KB) /
> AHB1 peripherals including AHB to APB bridges and APB peripherals / AHB2
> peripherals / **FSMC**»

**8 × 7 confirmado**, y la lista coincide uno a uno con el `enum BusMaster` del
modelo. El mismo párrafo cierra además otra cosa de rebote: *«The 64-Kbyte CCM
data RAM is not part of the bus matrix and can be accessed only through the
CPU»* — que es exactamente lo que el modelo hace con su código de retorno `-2`.

`[RM0390]` §2.1, literal:

> «•  Seven masters: […] (los mismos, **sin** el del Ethernet)
> •  Seven slaves: […] AHB2 peripherals / **FMC / QUADSPI**»

**7 × 7.** La resta 8 − 1 = 7 que este documento daba por buena era correcta en
los maestros. **Pero el séptimo esclavo no es el mismo**, y eso no estaba:

> **EL QUADSPI NO AÑADE UN ESCLAVO. Comparte el puerto del FMC.**

Consecuencia directa sobre el plan: en la fase 1 hay que dejar que el séptimo
esclavo sea **un puerto con dos destinos detrás** —decodificados por dirección,
`0xA000_0000` y `0xA000_1000`— y no pensar en un `BusSlaveId::QUADSPI`. Si se
hubiera descubierto al escribir el código, habría costado rehacer el `enum` y
la máscara de conectividad.

### 15.2 Los maestros

Cerrado arriba. Nada que añadir salvo que el modelo ya era correcto.

### 15.3 La tabla de vectores del F446

`[RM0390]` §10.1.3, leída fila a fila. **97 posiciones (0–96)**, once de ellas
con la fila `-  -  Reserved` y su dirección de vector:

| | |
| :--- | :--- |
| Reservadas | 61, 62, 79, 80, 82, 83, 85, 86, 88, 89, 90 |
| Implementadas | **86** |
| Renombrada | posición **48: `FMC`** (era `FSMC`) |
| Altas | 84 `SPI4`, 87 `SAI1`, 91 `SAI2`, 92 `QuadSPI`, 93 `HDMI-CEC`, 94 `SPDIF-Rx`, 95 `FMPI2C1`, 96 `FMPI2C1 error` |

Coincide **al cien por cien** con lo que este documento había inferido del
`IRQn_Type` de ST. La inferencia era buena; ahora además está leída.

Y el contraste del F407, `[RM0090]`: posiciones **0–81 sin ningún hueco**, con
la **79 = `CRYP`** y la **80 = `HASH_RNG`**. Ojo al matiz: esa tabla cubre
F405/407/**415/417** a la vez, y el CRYP es de los `415/417`. En un F407VG la
posición 79 existe en la tabla y **no tiene dueño**, que es por lo que la
cabecera de ST tiene 81 entradas y no 82.

### 15.4 El `DBGMCU_IDCODE` del F446 — el punto bloqueante

`[RM0390]` §33.6.1, literal: *«The device ID is 0x421»*, con
`REV_ID = 0x1000 = Revision A`.

| | Registro completo | `DEV_ID` |
| :--- | :--- | :--- |
| F407 | `0x1001 6413` *(lo que devuelve el modelo hoy)* | `0x413` `[RM0090]` §32.6.1 |
| **F446** | **`0x1000 0421`** | **`0x421`** |

El JTAG ID del boundary-scan del F446 es `0x0641 3041`, y el del SW-DP del
Cortex-M4 no cambia (`0x2BA0 1477`, el de Arm).

Con esto, el hito **H5** del plan —depurar el F446 desde STM32CubeIDE— deja de
tener un agujero. Era el punto que más podía costar, porque un `IDCODE`
equivocado no da un error claro: da un «Could not verify ST device», que es
exactamente el mensaje que costó media sesión diagnosticar en su día.

### 15.5 La discrepancia 96 / 91, que es peor de lo que parecía

Los tres números, los tres leídos directamente:

| Fuente | Dice |
| :--- | ---: |
| `[RM0390]` §10.1.1 | «96 maskable interrupt channels» |
| `[DS10693]` §3.11 | «up to 91 maskable interrupt channels» |
| `[RM0390]` §10.1.3, contando la tabla | **86** implementadas en **97** ranuras |

**Ninguno de los dos números de portada coincide con la tabla, ni entre sí.** El
96 parece ser el número de la última posición y no un recuento; el 91 no se
corresponde con nada que se pueda derivar de la tabla.

Decisión, y queda escrita: **el modelo dimensiona por la tabla.** 97 posiciones,
que es lo que mide el vector de entradas del NVIC. El recuento de implementadas
no le hace falta a nadie: una línea que nadie gobierna no se pone pendiente
jamás, que es precisamente lo que hace una posición reservada.

### 15.6 Las fronteras de registros

Cerrado en §8.3, con la laguna del FMPI2C1 documentada allí.

### 15.7 Las revisiones — lo único que sigue abierto

`[RM0390]` vigente es la **Rev 9, de febrero de 2026**; lo leído aquí es la
Rev 4. No se ha podido leer la 9 más allá del índice: st.com devuelve 403 al
cliente HTTP de este entorno, y el lector de páginas trunca un PDF de 1328
páginas mucho antes del capítulo 33.

Lo que sostiene el trabajo mientras tanto: **todo lo verificado coincide con la
cabecera CMSIS actual de ST** (`stm32f446xx.h`), que es código del fabricante y
está al día. Un dato estructural que hubiera cambiado entre la rev. 4 y la 9
—una dirección base, un número de vector— tendría que aparecer también ahí, y no
aparece.

Queda como tarea de bajo riesgo: **releer en la rev. 9 los §2.1, §10.1.1,
§33.6.1 y la Tabla 1**, sobre todo esta última, que es donde hay una laguna
conocida.

### 15.8 Los pines del LQFP64, y el error que destaparon

`[DS10693]` Tabla 2 da **50 GPIOs** para el LQFP64, y `[PINDATA]`
`STM32F446R(C-E)Tx.xml` declara `<IONb>50</IONb>` con este reparto: PA0–PA15,
PB **sin PB11**, PC0–PC15, **solo PD2**, PH0/PH1. Confirmado.

Y al comprobar con la misma herramienta el LQFP64 del F405RG —`<IONb>51</IONb>`,
con el puerto B completo— quedó confirmado lo que este documento decía: **no son
el mismo LQFP64**, y la diferencia es PB11.

**Lo que no estaba previsto:** teniendo el DS8626 abierto, se aprovechó para
cerrar **I-40**, el mapa del WLCSP90 que el modelo llevaba marcado como no
verificado. El resultado fue que **la reconstrucción que había era falsa**:

| | Lo que el modelo suponía | Lo que dice ST |
| :--- | :--- | :--- |
| PA, PB | completos | completos ✓ |
| PC | completo | **faltan PC1, PC4 y PC5** |
| PD | completo | **faltan PD3 y PD13** |
| PE | PE0–PE5 | **PE7–PE15**, la mitad contraria |
| PH | PH0/PH1 | PH0/PH1 ✓ |
| PI | *(no se contemplaba)* | **PI0 y PI1** |
| **Total** | 72 | **72** |

**El recuento cuadraba por casualidad y la identidad estaba mal en casi todo.**
Verificado por dos fuentes de ST que coinciden puerto a puerto —la figura 17 del
DS8626, el diagrama de bolas, y la base de pines de CubeMX—: 16/16/13/14/9/2/2.

El detalle que más sorprende: **PI0 y PI1 salen en un encapsulado de 90 bolas y
no salen en el LQFP144 de 144 patillas**. No es que a más patillas, más E/S.

Corregido en `pins/encapsulado.h`, ya marcado como verificado, y con tres
comprobaciones nuevas en T128. El efecto se ve enseguida:

```
$ ./build/sim placas/discovery_min.xml --mcu STM32F405OE --valida
  [decl] LD3.anodo: el pad PD13 no sale al encapsulado WLCSP90
```

La placa Discovery **no cabe en un WLCSP90**, porque le falta el pin del LED
naranja. Con el mapa anterior eso habría pasado en silencio.

---

## 16. Fase 1: qué se movió, y qué se aprendió moviéndolo

La fase 1 no añade un chip. Su único cometido es que, cuando llegue el F446, no
haya que **copiar y editar** nada: que lo que es «de la arquitectura ARMv7-M» y
lo que es «de la familia F4» dejen de estar en el mismo fichero que lo que es
«del STM32F407VG». El criterio de aceptación era severo a propósito —el modelo
tiene que hacer **exactamente** lo que hacía, con el mismo tiempo simulado al
picosegundo— porque una refactorización que cambia el comportamiento sin querer
es indistinguible de una que lo cambia porque estaba mal.

**Se cumplió: 2 336 217 899 213 ps antes y después, en las cinco entregas.** La
suite pasó de 2033 a 2043 comprobaciones, y las diez nuevas son las de T129, que
prueba la factoría; ninguna comprobación existente cambió de resultado.

### 16.1 Las cinco entregas

| # | Qué | Resultado |
| :--- | :--- | :--- |
| 1 | `periph/crc_rng.h` se parte | `crc.h` (140 líneas) + `rng.h` (288) + un `crc_rng.h` de 18 que incluye los dos |
| 2 | La conectividad de la matriz, de función a dato | `Conectividad` + `CONN_STM32F407VG` en `bus/ahb_matrix.h` |
| 3 | La tabla de funciones alternativas, fuera del top | `soc/f4_mapa_af.h` (357 líneas, 169 llamadas) |
| 4 | `common/ahb_types.h` se parte | 203 líneas de arquitectura + `soc/f4_mapa_perif.h` (117) de familia |
| 5 | `mcu_if` y `FabricaMcu` | `soc/mcu_if.h` (154) + `soc/stm32f4_mcu.h` (112), y `sim_main.cpp` migrado |

Tres detalles de ejecución que merecen quedar escritos, porque son los que
hicieron que la fase fuese aburrida en vez de arriesgada:

**El truco para mover código sin leerlo.** Las 169 llamadas de la tabla de
funciones alternativas se movieron a otro fichero como **definición fuera de
clase** —`inline void Stm32F407VG::bind_mapa_af() { ... }`—, que ve los miembros
exactamente igual que si siguiera dentro. Los cuerpos viajaron carácter a
carácter, sin adaptar ni una línea. Lo mismo vale para `f4_mapa_perif.h`, que
**no es un cabecero autónomo**: se incluye desde `ahb_types.h` con el
`namespace addr` todavía abierto, de modo que **ningún otro fichero tuvo que
cambiar un solo `#include`**. Es deliberadamente poco elegante y es exactamente
lo que un criterio de aceptación al picosegundo pide.

**Una fila a cero es un maestro que no existe.** Al pasar la conectividad a
dato, la pregunta «¿y cómo se dice que el F446 tiene siete maestros y no ocho?»
se contesta sola: con la fila del maestro que le falta puesta a cero. No hace
falta una constante nueva ni un `if`; `hay_maestro()` lo lee de la propia tabla.

**El despacho es por familia, no por pieza.** `FabricaMcu` se indexa por
`McuCaps::familia`, no por `McuCaps::nombre`: los once miembros del F405/407
comparten un creador y se distinguen por su descriptor. Un tipo nuevo de la
misma familia sigue siendo **una línea en el catálogo**; el F446 será otro
`REGISTRA_MCU`. Y cuando la familia no tiene modelo enlazado, `crea()` devuelve
`nullptr` y `sim` lo dice con nombre —«el tipo X es de la familia Y, y no hay
ningún modelo registrado para ella; las que sé construir son: …»—. **Nunca monta
otro chip en su lugar**, que es la forma más silenciosa que tendría este
programa de mentirle a un alumno.

### 16.2 Dos correcciones al propio plan

El plan de §12 decía dos cosas que la ejecución desmintió, y conviene corregirlo
en vez de dejar que envejezca:

* decía **`soc/f407/mapa.h`**; el fichero es **`soc/f4_mapa_perif.h`**, sin
  subcarpeta por chip. Con un solo mapa no había nada que separar, y una carpeta
  por chip vacía habría sido estructura por adelantado;
* decía que la tabla de funciones alternativas eran **«300 llamadas escritas a
  mano»**. Son **169**. La cuenta de 300 salía de contar señales, no llamadas.

### 16.3 El hallazgo del quinto paso: el saneador encontró la fuga

Al pasar `sim_main.cpp` a la interfaz, el destructor de `Sim` seguía haciendo
`delete m.dut` —el chip— cuando el dueño del chip pasó a ser el **adaptador**.
El resultado no era un doble borrado, que se habría visto enseguida, sino lo
contrario: el chip se borraba una vez y **el adaptador se quedaba colgando**,
248 bytes por MCU. Lo cazó AddressSanitizer sobre `sim`, no la suite, porque la
suite no construye `sim_main.cpp`.

Vale la pena anotarlo como método: **la suite no cubre el ejecutable de
usuario**. Desde esta fase, la verificación de una entrega incluye compilar
`sim` con `-fsanitize=address,undefined` y correr con él al menos la placa
Discovery con el *blinky*, que es el camino que recorre un alumno.

### 16.4 Qué queda preparado para la fase 2

Con la fase 1 cerrada, un `Stm32F446` necesita: una clase top propia, su
`McuCaps` en el catálogo con `familia = "STM32F446"`, su `Conectividad` de siete
maestros, su `MapaFlash` de 512 KB, su `Encapsulado` LQFP64 —ya verificado en la
fase 0— y **un `REGISTRA_MCU`**. Nada de eso obliga a tocar `ahb_types.h`, ni la
matriz, ni el CRC, ni `sim_main.cpp`. Que era justo el objetivo.

Queda pendiente, de la fase 0, el único punto abierto: releer `[RM0390]`
**rev. 9** §2.1, §10.1.1, §33.6.1 y Tabla 1, que es material de la fase 2.

---

## 17. Fase 2: el esqueleto, y el fallo que destapó

La fase 2 pedía **un `Stm32F446` que sea el F407 menos lo que no tiene**, y un
blinky compilado para F446RE que arranque y parpadee. Las dos cosas están, y por
el camino apareció un fallo en el SysTick que llevaba dentro desde el principio
y que no era del F446.

**El invariante del F407 sigue intacto: `2336217899213 ps`, 2045/2045.**

### 17.1 Lo que se hizo, y dónde

| Pieza | Qué es |
| :--- | :--- |
| `SocF4` (antes `Stm32F407VG`) | el netlist pasa a llamarse por lo que es: **el die de la familia F4**. `using Stm32F407VG = SocF4;` deja valer todo lo escrito |
| `soc/stm32f446.h` | la clase `Stm32F446`, derivada, con su `REGISTRA_MCU(STM32F446, …)` |
| `MCU_STM32F446RE` | el descriptor: 512 KB, sin CCM, LQFP64 propio, 97 posiciones, IDCODE `0x1000 0421` |
| `ENC_LQFP64_F446` | 50 E/S. **No es el LQFP64 del F405RG**, que saca 51: falta PB11 |
| `CONN_STM32F446` | siete maestros. Una fila a cero: la del DMA del Ethernet |
| `bus/conectividad.h` | la tabla sale de `ahb_matrix.h` a su propio fichero, porque es un rasgo del CHIP y lo lleva el descriptor |
| `Periferia` + `rng` + `i2sext` | dos ausencias más, modeladas como espacio reservado |
| `top/sc_main_f446.cpp` | **la suite del F446**, que empieza aquí y no en la fase 5 |
| `verif/fw/blinky446/` | el firmware del hito H3, con `stm32f446xx.h` de ST |
| `placas/nucleo_f446re.xml` | la placa: LD2 en PA5, B1 en PC13 |

Tres decisiones de las que conviene dejar constancia:

**El die es un superconjunto y el descriptor decide qué se alcanza.** `SocF4`
construye el Ethernet, el RNG, la CCM y los bloques de extensión del I2S los
lleve el chip o no —la elaboración de SystemC es estática y un módulo no se
puede no construir—, pero **lo que no lleva no entra en ningún decodificador**.
Su ventana es espacio reservado y tocarla es un error de bus, igual que en el
silicio. Es el mecanismo que ya usaban el Ethernet del F405 y el bus externo del
LQFP64; la fase 2 solo lo ha extendido.

**La CCM se fue del núcleo por dato.** `is_ccm()` leía las constantes globales
del F407; ahora el `CortexM4F` recibe el `MapaRam` y con tamaño cero no hay
camino directo desde el D-bus. Con eso, el aviso de §4.2 —«hay que comprobar que
el `-2` no se confunde con el `-1`»— es una comprobación de la suite del F446 y
no una nota al pie.

**El `IDCODE` es un dato del descriptor.** Era una constante escrita dentro de
`core/debug.h`, y es lo único del subsistema de depuración que cambia entre las
dos piezas.

### 17.2 El hito H3: el blinky de la Nucleo

```
$ ./build/sim placas/nucleo_f446re.xml verif/fw/blinky446/blinky446.bin 700
placa 'nucleo-f446re': 1 MCU(s), 2 componentes, 154 nodos, 0 avisos
firmware de u0: verif/fw/blinky446/blinky446.bin
simulados 700.000 ms en 0.010 s de anfitrion (18522 deltas)
  LED LD2 en PA5: encendido  (3.17 V, 2.30 mA)
```

El firmware usa **`stm32f446xx.h` de ST sin tocar**, el `startup_stm32f446xx.s`
oficial y el `system_stm32f4xx.c` de siempre, con `-DSTM32F446xx`. Eso es lo que
lo convierte en una prueba y no en una demostración: quien decide dónde está
cada registro es la cabecera del fabricante. El propio firmware calcula su
SYSCLK con `SystemCoreClockUpdate()` y le salen los 84 MHz que el modelo tiene
en `HCLK`; si el árbol de reloj del modelo no fuera el del F446, no coincidirían.

**Por qué 84 MHz y no 180.** Llegar a 180 exige la secuencia de over-drive del
PWR, que es la fase 3. Un `SystemClock_Config()` de CubeIDE se quedaría hoy
esperando a `ODRDY` para siempre — y eso es exactamente el criterio de
aceptación de esa fase.

### 17.3 El fallo del SysTick, que no era del F446

Al montar el blinky apareció lo siguiente: **el SysTick no interrumpía nunca a
84 MHz**. El contador corría —`SysTick->VAL` se movía—, `CTRL` valía 7, `LOAD`
valía 83 999, y la interrupción no llegaba jamás. El mismo firmware en el modelo
del F407 hacía exactamente lo mismo, así que **el fallo no era del chip nuevo**.

La causa: `tick_proc()` esperaba **N veces el periodo de un tick redondeado a
picosegundos**. Un tick a 84 MHz dura 11 904,7619… ps; redondeado hacia arriba y
multiplicado por 84 000, el contador queda **pasado** del cero. La condición de
disparo era «¿vale el contador exactamente cero?», no se cumplía nunca, y el
bucle volvía a esperar otra vuelta entera para pasarse otra vez.

Por qué no se había visto: las frecuencias que usa la suite —168 MHz, cuyo tick
son 5 952,38 ps, y 16 MHz, que son 62 500 exactos— redondean **hacia abajo** o
caen justas, y por abajo el bucle converge en dos vueltas. 84 MHz es la mitad de
168 y es lo que sale de un PLL con P = 4: no es un caso raro, es una
configuración de manual.

La corrección calcula el **instante absoluto** del cruce desde la base del
contador, en vez de sumar periodos, y dispara cuando **han pasado** los ticks que
faltaban en vez de exigir el cero exacto. Con ella, el invariante del F407 no se
mueve ni un picosegundo: a 168 MHz el camino que recorre el modelo es el mismo.

Es, de paso, el mejor argumento a favor del puerto: **modelar una pieza nueva
encuentra fallos en la vieja**, y este llevaba dentro desde la fase 6.

### 17.4 Por qué la suite del F446 es un ejecutable aparte

El primer intento fue construir el F446 dentro del banco del F407 para hacerle
preguntas. **Movió el invariante**: de `2336217899213` a `2336186149213 ps`. Un
segundo chip no es inerte —sus relojes internos oscilan y su núcleo arranca—, la
simulación se volvió trece veces más lenta, y una prueba del I2S que depende de
lo rápido que vaya el anfitrión cambió de resultado.

Dos conclusiones, y las dos valen más que la prueba que se quería hacer:

* **la fase 5 del plan tenía razón**: la suite del F446 es un banco propio, y
  empieza ya. `make test446`;
* hay **una prueba del I2S cuyo resultado depende del reloj de pared**. No se ha
  tocado en esta fase —el invariante manda— pero queda anotada como deuda: una
  suite que cambia de resultado según la máquina no es una suite.

### 17.5 Qué queda para la fase 3

El `limitaciones()` de `Stm32F446` lo dice, y `sim` lo imprime cada vez que monta
una placa con uno:

```
[ojo] u0: el arbol de reloj es todavia el del F407: no hay tercer PLL (PLLSAI),
      ni divisor R, ni M/P/Q propios del PLLI2S, y RCC_DCKCFGR y RCC_DCKCFGR2 no
      existen en el modelo [fase 3]
[ojo] u0: los topes de 180/45/90 MHz no EXIGEN la secuencia de over-drive [fase 3]
[ojo] u0: los seis perifericos que el F446 tiene y el F407 no [...] [fase 4]
```

Que el modelo diga en voz alta lo que aún no hace es el precio de poder
entregarlo a medias sin mentir. La fase 3 se mide por que esas dos primeras
líneas desaparezcan.

---

## 18. Fase 3: el árbol de reloj, y un segundo fallo del F407

La fase 3 era «el bloque más caro del puerto y el que más pruebas va a
necesitar», y el motivo estaba escrito en §13: **un árbol de reloj mal modelado
no falla, da la frecuencia equivocada**. De ahí el criterio, que se ha cumplido
al pie de la letra: *cada selector tiene que cambiar una frecuencia observable,
no solo guardar un bit*.

**El invariante del F407 sigue intacto: `2336217899213 ps`, 2047/2047. La suite
del F446 pasa de 43 a 101 comprobaciones.**

### 18.1 El hito H4, que es lo que se ve desde fuera

```
--- D1 H4: el SystemClock_Config() de una Nucleo-F446RE ---
    buzon: done=1 sysclk=180000000 etapa=6 od=3 ticks=59 pclk1=45000000
  [OK] el firmware recorre las seis etapas de RM0390 5.1.3
  [OK] y llega con ODRDY y ODSWRDY puestas: paso por el over-drive de verdad
  [OK] SYSCLK = 180 MHz, calculado por el firmware con la cabecera de ST
  [OK] y el modelo esta de acuerdo: HCLK = 180 MHz

--- D2 H4, la otra mitad: si ODRDY no sube, el firmware NO avanza ---
    buzon: done=0 etapa=1 (1 = escribio ODEN y sigue esperando ODRDY)
  [OK] sesenta milisegundos despues, el firmware SIGUE en el paso 3
  [OK] y NO hay 180 MHz: sin over-drive el chip no llega
```

Las dos mitades, como pedía el plan. La segunda se consigue sin construir un
chip aparte: el tiempo que tarda el regulador es un **parámetro** del PWR, y
ponerlo en diez segundos es exactamente «esta bandera no va a subir». El
firmware es el mismo, sin recompilar.

### 18.2 Qué se escribió

| Pieza | Qué es |
| :--- | :--- |
| `rcc/reloj_caps.h` | los rasgos del árbol (`ArbolReloj`) y los topes (`LimitesReloj`), que salen de `mcu_caps.h` al subsistema que los usa |
| `LimitesReloj` con dos juegos | 168/42/84 **sin** over-drive y 180/45/90 **con** él: por primera vez un tope depende del ESTADO y no del chip |
| `Pll::out_r_hz()` | el divisor R, sin generador de onda: hoy solo se consulta su frecuencia |
| `pllsai` | el tercer PLL, construido siempre y encendible solo donde existe |
| `PLLI2S` con M/P/Q propios | en el F407 comparte la M del PLL principal —las dos VCO van atadas— y en el F446 no |
| `R_PLLSAICFGR`/`R_DCKCFGR`/`R_CKGATENR`/`R_DCKCFGR2` | 0x88, 0x8C, 0x90 y **0x94**: DCKCFGR2 no está en 0x90, porque en medio hay un registro más |
| nueve selectores | CK48MSEL, SDIOSEL, SPDIFRXSEL, CECSEL, FMPI2C1SEL, SAI1SRC, SAI2SRC, I2S1SRC, I2S2SRC — y TIMPRE |
| `Pwr` con over-drive | ODEN/ODRDY, ODSWEN/ODSWRDY y la señal hacia el RCC |

**Dónde se sacaron las codificaciones.** No de la memoria: de las constantes del
propio ST. `stm32f446xx.h` da los desplazamientos de registro y las posiciones
de bit; `stm32f4xx_hal_rcc_ex.h` da qué significa cada valor de cada selector
(`RCC_SAI1CLKSOURCE_PLLR`, `RCC_CLK48CLKSOURCE_PLLSAIP`…), y la documentación
del macro `__HAL_RCC_TIMCLKPRESCALER` da, con las palabras de ST, las dos reglas
de TIMPRE. Eso permitió cazar tres cosas que se habrían copiado mal:

* **DCKCFGR2 está en 0x94**, no en 0x90 — en medio está CKGATENR;
* **PLLSAIDIVQ y PLLI2SDIVQ valen N−1**: un 3 divide por cuatro;
* **SAI1SRC = 11 es el pin `I2S_CKIN` y SAI2SRC = 11 NO lo es**, sino la fuente
  del PLL. La asimetría es real: el SAI2 no tiene entrada de reloj externa.

### 18.3 El segundo fallo del F407, encontrado por comparar

Al colocar `PLLSAIRDYF` hubo que mirar dónde estaban sus vecinos, y ahí apareció
que **toda la mitad baja de `RCC_CIR` estaba desplazada un bit**. El modelo
ponía `LSIRDYF` en el 1, los `xxxRDYIE` en 9..14 y los bits de limpieza en
17..22, siguiendo la tabla de `[IR, §4.6]`. La cabecera de ST los pone en **0**,
**8** y **16**.

La tabla del informe interno está mal, y el modelo la copió fielmente. Los dos
extremos del registro —`CSSF` en el 7 y `CSSC` en el 23— sí estaban bien, y son
los únicos que la suite comprobaba: por eso sobrevivió. Un firmware que
habilitara `RCC_CIR_HSERDYIE` con la constante de CMSIS no habría recibido nunca
esa interrupción.

Corregido, con una comprobación nueva en T22 que no cuesta tiempo simulado —los
flags son pegajosos, así que basta mirar dónde cayó el uno— y el invariante
intacto. Es el segundo fallo del F407 que destapa el puerto: **el primero fue el
SysTick de la fase 2**.

### 18.4 Lo que la fase 3 deja dicho y no hecho

`limitaciones()` lo enumera, y `sim` lo imprime al montar la placa:

* los **escalones de tensión** (`PWR_CR.VOS`) se leen y se escriben, pero el
  modelo no limita la frecuencia por escala: el único techo que se mueve es el
  del over-drive;
* **`RCC_CKGATENR`** se guarda y se devuelve y no hace nada: su único efecto
  observable es el consumo;
* durante la **conmutación** del over-drive el silicio para el reloj de sistema
  unos ciclos; el modelo espera el tiempo pero no lo para.

Las tres son de la misma clase: cosas que el modelo podría fingir y no finge.

---

## 19. Fase 4: los siete bloques nuevos, y un tercer fallo del F407

La fase 4 era la lista de la compra: **FMPI2C1, QUADSPI, SAI1/SAI2, SPDIF-RX,
HDMI-CEC, FMC con SDRAM**, más SPI4 e I2S1 de propina. El plan las ordenaba por
utilidad docente y no por tamaño, y así se han hecho.

**El invariante del F407 sigue intacto: `2336217899213 ps`. La suite del F407
pasa de 2047 a 2050 comprobaciones y la del F446 de 101 a 133. ASan + UBSan
limpios en las dos.**

### 19.1 Qué se escribió, y cuánta honestidad lleva cada cosa

| Bloque | Qué hace el modelo | Qué NO hace, y lo dice |
| :--- | :--- | :--- |
| **FMPI2C1** `periph/fmpi2c.h` | maestro y esclavo **a nivel de bit** sobre pines de colector abierto; TIMINGR de verdad contra el reloj que elige `FMPI2C1SEL`; NBYTES/RELOAD/AUTOEND; ISR/ICR; dos vectores (95 y 96) | el PEC (`PECR` lee cero) y los temporizadores de SMBus (`TIMEOUTR` se guarda y no vence) |
| **QUADSPI** `periph/quadspi.h` | los cuatro `CCR.FMODE`: escritura y lectura indirectas, sondeo automático con PSMKR/PSMAR/PIR, y **la ventana de 256 MB mapeada en memoria**; fases de instrucción, dirección, byte alternativo, ciclos vacíos y datos, en 1, 2 o 4 líneas | en el LQFP64 **no hay pin para IO2**: un comando en cuatro líneas escribe al aire por esa línea, y es el silicio el que no puede |
| **SAI1 y SAI2** `periph/sai.h` | dos bloques por SAI, cada uno esclavo con su dirección; MCLK/SCK/FS calculados desde el reloj del selector; FIFO de ocho con `FLVL`; OVRUDR, FREQ y **WCKCFG**, que es lo que impide arrancar con una trama imposible | companding (µ-law/A-law), MUTE, AC'97, y la sincronización SYNCIN/SYNCOUT: `GCR` se guarda y no encamina nada |
| **SPDIF-RX** y **HDMI-CEC** `periph/bloque_declarado.h` | ocupan su ventana, leen cero, no guardan lo que se escribe y **avisan la primera vez que alguien los toca** | todo lo demás, y por eso el aviso |
| **SPI4** | una línea: otra instancia de `spi.h` | no tiene un solo pin en el LQFP64 |
| **I2S1** | **no es un bloque nuevo**: es el SPI1 con la mitad de audio conectada. Lo que cambia es el rasgo (`CAPS_SPI_APB2_I2S`), no la clase | — |
| **FMC** | nada, y a propósito: en un F446**RE** el datasheet dice que ese encapsulado no saca el bus externo, así que `0xA000_0000` se queda **sin decodificar** | tocarlo da error de bus, que es lo correcto |

### 19.2 Las tres cosas que costaron, y por qué

**El puerto compartido.** El séptimo esclavo de la matriz del F446 es
«FMC / QUADSPI», uno solo (§15.1). `SocF4::bind_bus()` no puede atarlo a ciegas
porque la elaboración de SystemC no deja reatar un socket, así que decide por el
descriptor: con `perif.quadspi`, tapa el FSMC y **deja el puerto libre para la
clase derivada**, que lo lleva a un decodificador propio con dos ventanas
(`0x9000_0000` y `0xA000_1000`).

**Los relojes de núcleo.** Los SAI y el FMPI2C1 no comen de PCLK. La fase 3 ya
calculaba sus frecuencias «para cuando llegara el periférico»; lo que faltaba
era el cable, y se ha hecho con puertos del RCC (`sai1_hz`, `sai2_hz`,
`fmpi2c1_hz`, `i2s1_hz`) en vez de con consultas, porque un `sc_in<double>`
despierta al periférico y una consulta obliga a sondear. El resultado es lo que
el plan pedía: **cambiar `FMPI2C1SEL` cambia la frecuencia de SCL medida en el
pin**, sin tocar un registro del periférico.

**Los pines, que son del encapsulado y no del die.** Salen de `[PINDATA]`, el
repositorio *STM32 open pin data* de ST: `STM32F446R(C-E)Tx.xml` dice qué señal
llega a cada pin **de este LQFP64**, y `GPIO-STM32F446_gpio_v1_0_Modes.xml` dice
con qué número de AF. De ahí las tres consecuencias que un alumno se encuentra
en la tarjeta: el **SPI4 y el SAI2 no tienen pines**, el **QUADSPI no tiene
IO2** —la nota 3 literal del datasheet: *«available with limited features»*— y
el **FMPI2C1 sí los tiene**, en PC6 y PC7 con AF4.

### 19.3 El tercer fallo del F407, encontrado por sacar las máscaras de ST

Las máscaras de `RCC_xxxENR`/`RSTR`/`LPENR` estaban escritas a mano desde la
fase 1. Al necesitar las del F446 se extrajeron **contando los `RCC_xxx_yyy_Pos`
de las cabeceras de ST**, y al poner las dos columnas una al lado de otra
saltaron tres cosas:

* **`RCC_AHB1ENR` usaba la máscara del `AHB1LPENR`** (`0x7E6791FF` en vez de
  `0x7E7411FF`). Dejaba encender bits que no existen y prohibía dos que sí: los
  del OTG HS;
* **`RCC_AHB2ENR`** admitía los bits 4 y 5 —CRYP y HASH—, que son de un F417;
* y la primera versión de esta misma fase 4 se dejó fuera **el bit 16 del
  `APB1ENR` del F446, que es el SPDIF-RX**: el bloque estaba en su sitio y su
  reloj no se podía encender.

Es el tercer fallo del F407 que destapa el puerto —el primero fue el SysTick de
la fase 2, el segundo el `RCC_CIR` de la fase 3—, y los tres son de la misma
familia: **el modelo era más permisivo que el silicio**. Queda anotado como
**I-44**, con una comprobación en T02 que no cuesta tiempo simulado.

Por el camino apareció una cuarta, esta sí de la fase 4 y de las que no fallan
sino que callan: **el segmento APB2 no mide lo mismo en las dos piezas**. En el
F407 termina en `0x4001_57FF`; en el F446 sigue 2 KB más, porque detrás del
último temporizador están los dos SAI. Con la ventana corta, el puente APB2 no
reclamaba esas direcciones y el SAI —construido, dado de alta en su
decodificador y con reloj— no recibía un solo acceso.

### 19.4 Lo que la fase 4 deja dicho y no hecho

`limitaciones()` lo enumera y `sim` lo imprime al montar una placa con un F446.
Además de lo de la tabla de §19.1, queda una cosa de sistema: **las peticiones
de DMA de los bloques nuevos salen del periférico y no llegan a ninguna celda**,
porque el mapa de canales del DMA sigue siendo el del F407. Es trabajo de la
fase 5.

---

## 20. Fase 5: la verificación, que encontró tres cosas más

La fase 5 pedía tres cosas y no un periférico: que **la suite del F407 no se
moviera**, que hubiera **una suite paralela para el F446 con la misma
estructura**, y —sobre todo— **una prueba cruzada que comprobara lo que este
documento dice**, «la prueba que evita que el puerto se coma al original».

**El invariante del F407 sigue intacto: `2336217899213 ps`. La suite del F407
pasa de 2050 a 2053 comprobaciones y la del F446 de 133 a 189. Los dos hitos
que faltaban, H5 y H6, están cerrados.**

### 20.1 La prueba cruzada, escrita como una tabla de afirmaciones

El grupo **F** del banco del F446 no es una lista de comprobaciones sueltas: es
una **tabla cuyas filas son frases de este documento**, con su sección, y al
lado la expresión que las hace verdad o mentira preguntando a los dos
descriptores a la vez. Leída de arriba abajo, la tabla *es* el documento; y si
alguien cambia un descriptor sin pensar en lo que implica, la fila que se rompe
dice qué párrafo ha dejado de ser cierto.

Están las tres que el plan nombraba una por una —`0x4000_4000`, el vector 80 y
PB11— y once más: los siete esclavos de matriz con el séptimo compartido, los
ocho maestros contra siete, la Flash y la RAM, la CCM, el IDCODE, el AF11 que se
vacía, las 82 posiciones de vector contra 97, los topes que dependen del estado,
el segmento APB2 que no mide lo mismo, y el espacio reservado.

Y el grupo **F2**, que es la otra mitad y la que de verdad importa: **que el
F407 siga siendo el F407**. No se construye —eso movería su invariante—, se le
pregunta a su descriptor, que es lo que el die lee para construirse.

### 20.2 Lo que la prueba cruzada encontró el primer día

Dos cosas, las dos de la misma familia que las tres fases anteriores: **el
modelo era más permisivo que el silicio**.

**El AF11 no estaba vacío.** El documento dice, en §9.2, que el AF11 —el del
Ethernet— es el único número de función alternativa que el F446 vacía entero. No
lo estaba: `bind_mapa_af()` registraba las dieciocho entradas del MAC **sin
preguntar si el chip lleva Ethernet**. El módulo se construye siempre —la
elaboración de SystemC es estática— y su ventana de bus no la decodifica nadie
en un F405 ni en un F446, pero poner `AFR = 11` en PA2 de un F446 conectaba el
pad a un periférico que ese chip no tiene. Ahora la tabla va dentro de un
`if (mcu.perif.eth)`, y esto **también arregla los dos F405**, que llevaban el
mismo problema desde la fase 1.

**La ventana de la CCM contestaba en un chip sin CCM.** `decodifica_mapa()`
mandaba `0x1000_0000` al esclavo de la Flash cuando `ccm_size` era cero, es
decir, decía en el netlist que ahí contesta una memoria. La prueba anterior
(A2) solo miraba que no devolviera el código `-2`, que es el de la CCM, y por
eso pasaba. Ahora un chip sin CCM deja esa ventana **sin decodificar**.

> Queda anotado, y no arreglado, que el **resto** de la región de código por
> encima de la CCM y por debajo de la memoria de sistema también está reservado
> en las dos piezas y el modelo lo sigue mandando al esclavo de la Flash, que lo
> rechaza. El efecto visto desde el firmware es el correcto —error de bus— y la
> etiqueta del netlist no lo es. Arreglarlo es recortar la región de código
> entera, y eso es otra tarea.

### 20.3 El DMA: cuando no hay fuente, decirlo

La fase 4 dejó apuntado que las peticiones de DMA de los bloques nuevos salen
del periférico y no llegan a ninguna celda, porque el mapa de canales sigue
siendo el del F407. La fase 5 tenía que decidir: modelar el mapa del F446 o
decirlo con precisión.

**Se dice.** No se ha encontrado una fuente de ST **legible por máquina** para
la tabla de peticiones del F446 —las cabeceras CMSIS no la llevan y el
repositorio de datos de pines tampoco—, y este modelo no se inventa tablas: es
la misma regla que hizo buscar `[PINDATA]` en la fase 4 en vez de tirar de
memoria.

Lo que sí se ha hecho es **que deje de ser silencioso**. `DmaCtrl` tiene ahora
una máscara de 64 bits, `celdas_con_fuente`, que el top rellena con las celdas
que de verdad ha cableado; armar un stream sobre una celda que no está en ella
saca un aviso que dice cuál es. Sin eso, los dos motivos por los que una línea
de petición está permanentemente a cero —celda **reservada en el silicio** y
celda **que este modelo no cablea**— se ven exactamente igual desde dentro: un
stream que se queda esperando para siempre sin una bandera, sin un aviso y sin
una transferencia.

### 20.4 I-42: el diagnóstico estaba equivocado

Desde la fase 2 había una anotación que decía que **una prueba del I2S depende
del reloj de pared**: al construir un segundo chip en el banco del F407 la
simulación se volvió trece veces más lenta y la prueba pasó de «I2S3 recibió 23
muestras (21 correctas)» a «20 (18 correctas)».

**No es el reloj de pared.** La misma suite, compilada con ASan y UBSan —varias
veces más lenta— da los **mismos números** y el **mismo picosegundo**. Lo que
cambió al añadir un chip no fue la velocidad: fue **el orden en que SystemC
despierta los procesos**, que depende de cuántos módulos hay en la simulación.

La distinción importa porque cambia qué hay que arreglar. Y de paso deja ver que
la consecuencia era menor de lo que la anotación daba a entender: las
comprobaciones del T53 son por rango (`got3 >= 8`, `ok3 >= got3 - 2`), de modo
que el **veredicto** nunca cambió; lo que se movía eran los números impresos.

Lo que sí faltaba era comprobar **lo que no puede moverse**, y ahora está: *un
enlace de audio que funciona no pierde muestras*. `got3 == sent - 1` y
`gotx == sent - 1`, sin gastar un picosegundo, porque son variables ya contadas.
Y las dos muestras «incorrectas» de las veintitrés también están explicadas: son
**ceros del arranque del enlace**, no datos corrompidos —el esclavo engancha el
reloj en cuanto el maestro mueve CK y WS, y durante las primeras tramas lo que
hay en la línea de datos todavía es silencio—. Un número raro convertido en un
dato.

### 20.5 H5 y H6, los dos hitos que faltaban

**H5 — «el F446 se depura desde STM32CubeIDE con el `IDCODE` correcto».** El
punto 4 de la fase 0 lo marcó como bloqueante por una razón concreta: con el
IDCODE equivocado, CubeIDE no da un error útil, da un «Could not verify ST
device». Lo que se comprueba no es que el modelo **sepa** su IDCODE —eso ya lo
miraba A5 preguntándoselo por dentro— sino que lo diga **por los dos hilos**:
una sonda SWD soldada a PA13/PA14 hace el reset de línea, engancha el SW-DP, lee
el AHB-AP, y lee `0xE004_2000` **por los pines**. Sale `0x1000_0421`. Entre lo
uno y lo otro hay una cadena entera: los pines en AF0 desde el reset, el DP, el
AP, la matriz y el DBGMCU. Y luego lee y escribe la SRAM, vuelca un bloque con
auto-incremento de TAR, para el núcleo, lee y escribe sus registros y lo suelta:
51 paquetes, ni un ACK perdido.

**H6 — «los periféricos nuevos, uno a uno, con su prueba»** — con una mitad que
el plan no pedía con esas palabras y que es la que de verdad podía estar rota:
**los periféricos VIEJOS sobre el die nuevo**. El puerto le ha cambiado el árbol
de reloj a un modelo que llevaba siete fases funcionando, y si algo se rompió,
se rompió ahí y no en el QUADSPI:

| Prueba | Qué demuestra |
| :--- | :--- |
| USART2 a 115 200 baudios | con 45 MHz de PCLK1 el divisor **ya no es el que valía en un F407**, y el 0,1 % de error que queda es el del silicio |
| TIM2 cuenta 1000 en 1 ms | los 90 MHz de TIMCLK1 llegan **hasta el contador**, no se quedan en un registro |
| SPI1 a 22,5 MHz | un valor que con los 84 MHz de APB2 del F407 no sale |
| DMA2 SRAM1 → SRAM2 | los dos controladores y las dos memorias siguen en su sitio (y memoria a memoria **exige** modo FIFO, igual que el silicio) |
| FMPI2C1, trama entera | START, AUTOEND, y un **NACK** cuando no hay nadie en la dirección: la línea sube por la resistencia de la placa porque nadie tira de ella |
| QUADSPI mapeado en memoria | leer `0x9000_0040` **lanza un comando por los pines**: no es una memoria interna disfrazada |
| SAI1, trama con datos | la FIFO se vacía sola al ritmo de la trama, que es lo único que demuestra que el bloque transmite y no solo está configurado |
