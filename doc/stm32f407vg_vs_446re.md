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

Las revisiones de los RM consultadas **no son las últimas** (RM0390 va por la 9 y
RM0090 por la 22; aquí se han leído la 4 y la 18). Para los datos estructurales
que usa este documento —mapa de memoria, tabla de vectores, geometría de Flash—
eso no debería importar, y además todos ellos están corroborados por las
cabeceras CMSIS de ST, que son código del propio fabricante. Aun así, **conviene
recontrastar contra la revisión vigente antes de escribir código**, y donde hay
discrepancia entre documentos de ST se dice cuál y no se elige por nuestra
cuenta.

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

> ⚠ **SIN VERIFICAR**: el etiquetado literal «Reserved» de esas once posiciones
> en `[RM0390]` Tabla 38 se ha inferido de su ausencia en el `IRQn_Type` de ST
> más la lista de bajas de `[AN4658]`, no leyendo la tabla. Lo cerraría abrir
> `[RM0390]` §10.1.3 directamente.

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

> ⚠ **SIN VERIFICAR**: el «Seven masters» viene de una reproducción secundaria
> de `[RM0390]` §2.1, no de st.com directamente, y **el número de esclavos del
> F446 no se ha podido leer para ninguna de las dos piezas**. Lo cerraría abrir
> `[RM0090]` Figura 1 y `[RM0390]` Figura 1. La coincidencia 8−1=7 es una
> corroboración fuerte, pero es aritmética, no una lectura.

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

Direcciones de `[CMSIS]` `stm32f446xx.h`, corroboradas por el SVD de ST y, para
cuatro de ellas, por `[AN4658]`.

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

> **Nota de alcance sobre el F446RE en concreto:** en LQFP64, `[DS10693]` Tabla 2
> dice que el **FMC no está disponible** y que **SAI2 no sale**; `[PINDATA]`
> confirma que el XML del F446R no tiene ni pines de FMC ni de SAI2 ni de SPI4.
> Es decir: en la pieza que se pide modelar, tres de las altas de arriba
> **existen en el die pero no tienen pines**. Con la máquina de encapsulados que
> el modelo ya tiene, eso se describe solo — y es un buen argumento para modelar
> **el die del F446 y el encapsulado por separado**, como se hizo con la familia
> F405/407.

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

> ⚠ **SIN VERIFICAR**: el valor del `DEV_ID` del F446. El del F407 está en el
> modelo; el del F446 lo daría `[RM0390]` §33.6.1 (registro `DBGMCU_IDCODE`).
> **Hay que leerlo antes de escribir la línea**, porque poner el del F407
> significaría que STM32CubeIDE identifique mal el chip — el error exacto que
> costó media sesión depurar.

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

### Fase 0 — Cerrar lo que está sin verificar *(antes de escribir código)*

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

### Fase 1 — Separar «familia F4» de «STM32F407» *(sin añadir un solo chip)*

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

### Fase 2 — El esqueleto del F446, sin periféricos nuevos

Montar un `Stm32F446` que sea **el F407 menos lo que no tiene**: sin Ethernet,
sin RNG, sin CCM, con 512 KB de Flash, con el LQFP64 del F446 y con las 97
posiciones de vector.

Al acabar la fase 2, **un blinky compilado para F446RE debe arrancar y parpadear**
—usa GPIO, RCC básico y SysTick, y nada de eso ha cambiado—. Es un hito
comprobable y temprano, y vale la pena perseguirlo antes que ningún periférico
nuevo.

### Fase 3 — El RCC del F446

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
| **H1** | La fase 1 acaba con la suite intacta y el invariante en su sitio |
| **H2** | `sim placa.xml --mcu STM32F446RE` monta y valida |
| **H3** | Un blinky de CubeIDE para F446RE parpadea |
| **H4** | `SystemClock_Config()` llega a 180 MHz por over-drive |
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

## 14. Lo que este documento NO ha podido verificar

Recogido en un sitio para que no se pierda entre las secciones:

1. **Esclavos de la matriz AHB**, en ninguna de las dos piezas. → `[RM0090]`
   Figura 1 y `[RM0390]` Figura 1.
2. **Los 8 maestros del F407** están en el modelo `[IR, §6.1.2]`, pero no se ha
   podido contrastar contra `[RM0090]` en esta pasada; y **los 7 del F446**
   vienen de una reproducción secundaria.
3. **El etiquetado «Reserved» de las once posiciones** de vector del F446, que se
   ha inferido de la cabecera de ST. → `[RM0390]` §10.1.3.
4. **`DBGMCU_IDCODE` del F446.** → `[RM0390]` §33.6.1. **Es bloqueante para la
   depuración desde STM32CubeIDE.**
5. **La discrepancia 96 / 91** líneas de IRQ entre `[RM0390]` §10.1.1 y
   `[DS10693]` §3.11, que son los dos de ST.
6. **`[RM0390]` Tabla 1** (fronteras de registros) no se ha leído: todas las
   direcciones del F446 de este documento vienen de la cabecera CMSIS de ST y del
   SVD, que coinciden entre sí y con `[AN4658]` en lo que este último lista.
7. Las revisiones leídas **no son las vigentes** (RM0390 rev. 4 frente a la 9;
   RM0090 rev. 18 frente a la 22).

Ninguno de los siete invalida el plan. Los puntos 3, 4 y 5 hay que cerrarlos
**antes** de escribir el código correspondiente; el resto afecta a comentarios y
a comprobaciones, no a la arquitectura.
