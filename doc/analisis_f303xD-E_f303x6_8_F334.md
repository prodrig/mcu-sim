# El resto de la familia F3: F303xD/E, F303x6/8 y F334

**Qué hace falta para los dos dies que S9 dejó fuera, y qué hay que bajar a
`doc/refs/` antes de empezar** · 21-10-2026

`doc/analisis_stm32f303.md` (S9) metió en el catálogo todo el **die 0x422**:
F303xB/C, F302xB/C y F358xC, quince referencias que son el mismo silicio con
otra Flash, otra SRAM, otro encapsulado y algún bloque de menos. Este documento
es el plan para los otros dos dies de la línea F30x, que **no** son el mismo
silicio y por eso no caben en un descriptor:

| Die | DEV_ID | Referencias | Lo que los distingue |
| :--- | :--- | :--- | :--- |
| **0x446** | `0x446` | **F303xD/E** (384/512 KB), F302xD/E, F398xE | 64 KB de SRAM, FMC, TIM20, I²C3, SPI4, puertos G y H, LQFP144 |
| **0x438** | `0x438` | **F303x6/8** (32/64 KB), **F334x4/6/8**, F328x8 | 12 KB de SRAM, sin DMA2 ni puerto E, DAC2; el **F334 con HRTIM1** |

Lo que se dice abajo de cada die sale de dos fuentes que ya están en la máquina
de trabajo y se pueden comparar por programa —la **base de pines de ST**
(`STM32_open_pin_data`, BSD-3) y las **cabeceras CMSIS** de
`cmsis_device_f3` v2.3.9 (Apache-2.0)—, como pide la regla de dos fuentes de
`doc/fuentes.md`. Lo que solo dice un PDF que aún no está en `doc/refs/` va
marcado **[1F]**: es lo primero que hay que contrastar cuando lleguen.

---

## 0. Resumen

* **El F303xD/E es el F303xC con más cosas**, y casi todo lo que el modelo ya
  tiene vale: mismo núcleo, mismo RCC, mismo FPEC (páginas de 2 KB), mismos
  periféricos de segunda generación. Lo nuevo es el **FMC** (el controlador de
  memoria externa: el FSMC del F407 con otro nombre y otra base), el
  **TIM20** (un avanzado más, como TIM1/TIM8), **I²C3** y **SPI4** (otra
  instancia de lo que hay), los puertos **G y H**, tres interrupciones más
  (82 → 85) y el **LQFP144**. Es el más barato de los dos: un `F3Caps`
  ampliado, una tabla de AF nueva (`STM32F303E_gpio_v1_0`) y cuatro bloques.
  La **Nucleo-F303RE** y la **Nucleo-F303ZE** son las placas de pruebas.
* **El F303x6/8 es el F303xC con menos cosas** —sin DMA2, sin puerto E, sin
  TIM4/TIM8, sin SPI2/3, I²C2, UART4/5, USB ni ADC3/4— y una cosa más: **DAC2**.
  Es la misma mecánica de S9 (bloques ausentes), pero con **otro mapa de
  AF** (`STM32F333_gpio_v1_0`), otra tabla de peticiones de DMA, otra red de
  ITR de los TIM y otros remapeos de `SYSCFG_CFGR1/3`: no es un descriptor,
  es un SoC hermano. La **Nucleo-F303K8** (Nucleo-32) es la placa.
* **El F334 es el F303x8 con HRTIM1**: el mismo die (`DIE438` en la base de
  pines, la misma tabla de AF) y un temporizador de alta resolución (217 ps,
  cinco temporizadores de dos salidas, eventos externos, ráfagas, fallos)
  que es **el bloque más complejo de toda la familia F3**. Tiene su propio
  manual, **RM0364**. La placa: **Nucleo-F334R8** y la **32F3348DISCOVERY**
  (la de la conversión de potencia, con su buck-boost y su LED de potencia).
* **El orden**: 0x446 primero (fases E0-E3), luego 0x438 sin HRTIM (K0-K3) y
  el HRTIM al final (H0-H3), que puede quedarse en «declarado» si no hay un
  caso de uso: en un curso, el F334 se usa casi solo por el HRTIM.

---

## 1. El die 0x446: F303xD/E (y F302xD/E, F398xE)

### 1.1 Lo que cambia, contado por programa

`stm32f303xe.h` frente a `stm32f303xc.h` (cmsis_device_f3 v2.3.9), contando
las macros `*_BASE` y los `IRQn`:

| | F303xC | F303xE |
| :--- | :--- | :--- |
| Bloques nuevos | — | `FMC` (con sus bancos 1, 1E, 2/3 y 4), `GPIOG`, `GPIOH`, `I2C3`, `SPI4`, `TIM20` |
| Bloques que faltan | — | ninguno |
| Última IRQ | 81 (`FPU_IRQn`) | 84 (85 posiciones) |

Y de la base de pines (`STM32F303R(D-E)Tx`, `V(D-E)Tx`, `Z(D-E)Tx`): 384/512
KB de Flash, **64 KB de SRAM** (frente a 40), `DIE446`, la tabla de AF
`STM32F303E_gpio_v1_0`, y encapsulados LQFP64 (51 E/S), LQFP100 (86) y
**LQFP144 (115)**. La CCM: 16 KB [1F: DS10362]. El F302xD/E no tiene CCM, TIM7,
TIM8, ADC3/4 ni COMP3/5/7/OPAMP3/4 (`stm32f302xe.h`); el F398xE es el F303xE a
1,65-1,95 V sin USB [PINDATA; `stm32f398xx.h`] —el mismo patrón que el F302xC
y el F358 de S9—.

### 1.2 Lo que hay que escribir

| Qué | Dónde | Desde qué | Coste |
| :--- | :--- | :--- | :--- |
| `F3Caps` con el die: N_IRQ, el número de puertos, la SRAM, los bloques | `soc/stm32f3/f3_desc.h` | CMSIS + PINDATA | Pequeño |
| La tabla de AF de `STM32F303E_gpio_v1_0` y los tres encapsulados | `gen_pines.py` (ya lee varios encapsulados desde S9) | PINDATA | Pequeño |
| Los puertos G y H en el `PinMux` del F3 (hoy A-F) | `stm32f303.h` | CMSIS | Pequeño |
| `TIM20`, `I2C3`, `SPI4`: instancias de lo que hay, con su reloj (`RCC_CFGR3`), su DMA y su ITR | `stm32f303.h`, `rcc/rcc_f3.h` | **RM0316 §13.4.7 (DMA) y tabla de ITR** | Medio |
| El **FMC** | `periph/fsmc.h` (el del F407) con rasgos: otra base (`0xA000_0000` registros, bancos en `0x6000_0000`), sin PC Card en el 4 [1F: RM0316 §16] | **RM0316 §16** | Medio |
| La memoria de paquetes del USB: 1 KB y otro esquema de acceso que el del F303xC [1F: RM0316, el capítulo del USB] | con S6 | RM0316 | — |
| Los bytes de opción y la protección de escritura con 512 KB | `mem/flash_f1.h` | RM0316 §4 | Pequeño |
| El cargador de S7 con el PID `0x446` y la SRAM de 64 KB | `soc/stm32f3/cargador/` | AN2606 | Pequeño |
| La placa: **Nucleo-F303RE** (MB1136) y su dibujo | `placas/nucleo_f303re.xml` | **UM1724** y el esquemático | Medio |
| `testf3` o una suite hermana con los ejemplos de STM32CubeF3 para la Nucleo-F303RE | `top/` | STM32CubeF3 | Medio |

### 1.3 Fases

* **E0 · Fuentes.** Lo de §4 en `doc/refs/stm32f303xe/`; la base de pines del
  die 0x446 en `soc/stm32f3/pindata/`; `stm32f303xe.h` en el `sdk/`.
* **E1 · El chip.** El descriptor, los encapsulados, los puertos G/H, TIM20,
  I²C3, SPI4 y el FMC declarado. **Cierre**: `GPIO_IOToggle` de STM32CubeF3
  para la Nucleo-F303RE, sin modificar, parpadea LD2.
* **E2 · El FMC.** Del FSMC del F407, con rasgos. **Cierre**: el ejemplo
  `FMC_SRAM` de ST (si STM32CubeF3 lo trae para la F303ZE) contra una SRAM
  del banco de pruebas.
* **E3 · La placa y la ventana.** `nucleo_f303re.xml` y su SVG, el VCP del
  ST-LINK/V2-1 (USART2, PA2/PA3), G9 en `mcu-sim-gui`.

---

## 2. El die 0x438: F303x6/8 (y F328x8)

### 2.1 Lo que cambia, contado por programa

`stm32f303x8.h` frente a `stm32f303xc.h`:

| | Está | Falta |
| :--- | :--- | :--- |
| Bloques | **`DAC2`** | `ADC3`, `ADC4`, `COMP1/3/5/7`, `OPAMP1/3/4`, **`DMA2`**, **`GPIOE`**, `I2C2`, `I2S2ext`/`I2S3ext`, **`SPI2`, `SPI3`**, `TIM4`, `TIM8`, `UART4`, `UART5`, `USB` |
| Última IRQ | 81 | 81 (con otros nombres en algunas posiciones [1F]) |

De la base de pines (`STM32F303K(6-8)Tx`, `C(6-8)Tx`, `R(6-8)Tx`): 32/64 KB de
Flash, **12 KB de SRAM**, `DIE438`, la tabla de AF **`STM32F333_gpio_v1_0`**
(otra que la del F303xC), y LQFP32 (25 E/S), LQFP48 (37) y LQFP64 (51). La
CCM: 4 KB [1F: el datasheet del F303x6/8]. El F328x8 es el F303x8 a
1,65-1,95 V [PINDATA; `stm32f328xx.h`].

### 2.2 Por qué no es S9 otra vez

En S9 bastó con quitar bloques porque lo que queda es **idéntico**: misma
tabla de AF, mismas peticiones de DMA, misma red de ITR. Aquí no:

* **La tabla de AF es otra** (`STM32F333_gpio_v1_0`): `gen_pines.py` con el
  fichero de modos del die 0x438.
* **Sin DMA2**, las peticiones se reparten distinto entre los siete canales del
  DMA1, con otros remapeos en `SYSCFG_CFGR1` y `SYSCFG_CFGR3` [1F: RM0316
  §13.4.7, tabla del F303x6/8].
* **La red de ITR** de los TIM cambia sin TIM4 ni TIM8 [1F: RM0316].
* **DAC2** (un canal, con su propio disparo) y los COMP/OPAMP que quedan
  cambian de entradas [1F].

Así que la forma es un **`SocF3x8` hermano** que reutiliza todas las piezas
(`UsartV2`, `DmaCanal`, `SpiBase` v3, `FmpI2c`, los TIM v3) con otras tablas, o
un `SocF3` parametrizado por die con las tablas como dato. Lo segundo es lo
que pide `doc/reutilizacion.md`; se decide en K0, a la vista de cuántas tablas
cambian de verdad.

### 2.3 Fases

* **K0 · Fuentes y decisión.** Lo de §4 en `doc/refs/stm32f303x8/`; contar las
  tablas que cambian (DMA, ITR, CFGR1/3, IRQ) contra RM0316 y decidir entre
  SoC hermano y SoC parametrizado.
* **K1 · El chip.** Descriptor, encapsulados (LQFP32/48/64), las tablas.
  **Cierre**: `GPIO_IOToggle` de ST para la Nucleo-F303K8, sin modificar.
* **K2 · La placa.** `placas/nucleo_f303k8.xml` (Nucleo-32, MB1180) con su
  VCP (USART2 en PA2/PA15) y su dibujo.
* **K3 · Los ejemplos.** Los de STM32CubeF3 para la Nucleo-F303K8 que ya
  pasan en el F303xC (UART, SPI1, I2C1, TIM), sin modificar.

---

## 3. El F334: el die 0x438 con HRTIM1

`stm32f334x8.h` frente a `stm32f303x8.h`: **los mismos bloques más `HRTIM1`**
(`HRTIM1_COMMON` y los temporizadores maestro y A-E); la base de pines le da
`DIE438` y la misma tabla de AF que al F303x8, con 16/32/64 KB de Flash y 12
KB de SRAM en LQFP32/48/64. Así que **el F334 sale de K1 casi gratis** —un
descriptor más— salvo el HRTIM.

**El HRTIM1** es otra cosa: un DLL que da 217 ps de resolución a 144 MHz, un
maestro y cinco temporizadores de dos salidas cada uno, diez eventos
externos con filtros, cinco entradas de fallo, ráfagas, modos *push-pull* y de
tiempo muerto, disparos para el ADC y el DAC. RM0364 le dedica más de cien
páginas [1F]. En el modelo:

* **H0** · Declararlo (ventana, reloj, interrupciones) y medir su tamaño.
* **H1** · Los temporizadores A-E con sus comparadores, sus salidas y el
  maestro: el PWM de alta resolución, con la resolución como dato y el tiempo
  simulado a 217 ps (`sc_time` lo admite; hay que mirar lo que cuesta).
* **H2** · Eventos externos, fallos y ráfagas.
* **H3** · Los ejemplos `HRTIM_*` de STM32CubeF3 para la Nucleo-F334R8 y la
  32F3348DISCOVERY, sin modificar; y la placa: o la Nucleo-F334R8 (Nucleo-64,
  la misma MB1136 que la F303RE) o la Discovery, que necesitaría un modelo del
  convertidor buck-boost y del LED de potencia para que tenga sentido.

**Si no hay un curso que lo pida, el F334 se queda en H0**: el chip arranca y
corre todo lo del F303x8, y el HRTIM avisa, como avisan hoy ADC, DAC y USB.

---

## 4. Lo que hay que bajar a `doc/refs/`

Los PDF de ST no se pueden bajar desde el entorno de trabajo, así que esta es
la lista para bajarlos a mano. **Todos desde la página de producto de st.com**
(la pestaña *Documentation*); la columna *Código* dice cómo se llama el
fichero que hay que buscar. Donde pone **por confirmar**, el código no se ha
podido comprobar desde aquí: el título manda, y al bajarlo se corrige la tabla.

Las carpetas siguen la de S0 (`doc/refs/stm32f303/`, que ya tiene RM0316 Rev
10, DS9118, UM1570 Rev 7 y el esquemático MB1035).

### 4.1 Imprescindibles (sin ellos no se empieza)

| Carpeta | Código | Título | Página de producto | Para |
| :--- | :--- | :--- | :--- | :--- |
| `stm32f303/` (ya está) | RM0316 | *STM32F303xB/C/D/E, STM32F303x6/8, STM32F328x8, STM32F358xC, STM32F398xE advanced Arm-based MCUs* | stm32f303re | Los dos dies: FMC, TIM20, I²C3, SPI4, las tablas de DMA y de ITR del F303x6/8. **Basta con comprobar que la Rev 10 los trae** |
| `stm32f303xe/` | **DS10362** | Datasheet STM32F303xD/E | [stm32f303re](https://www.st.com/en/microcontrollers-microprocessors/stm32f303re.html) | Memoria, CCM de 16 KB, patillas, tiempos de la Flash, corrientes |
| `stm32f303x8/` | DS9866 (**por confirmar**) | Datasheet STM32F303x6/8 | [stm32f303k8](https://www.st.com/en/microcontrollers-microprocessors/stm32f303k8.html) | CCM de 4 KB, DAC2, patillas |
| `stm32f334/` | **RM0364** | *STM32F334xx advanced Arm-based 32-bit MCUs* (manual de referencia) | [stm32f334r8](https://www.st.com/en/microcontrollers-microprocessors/stm32f334r8.html) | El HRTIM; el resto debería coincidir con RM0316 para el F303x8 |
| `stm32f334/` | DS9994 (**por confirmar**) | Datasheet STM32F334x4/6/8 | stm32f334r8 | El HRTIM eléctrico (DLL, 217 ps), patillas |
| `stm32f3/` | **AN2606** | *STM32 microcontroller system memory boot mode* | cualquiera | Los cargadores de los tres dies (PID, SRAM reservada, pines). **Hace falta también para S7**, que hoy lo cita de memoria |
| `stm32f3/` | **AN3155** | *USART protocol used in the STM32 bootloader* | cualquiera | El protocolo de S7, que tampoco está |

### 4.2 Las placas

| Carpeta | Código | Título | Para |
| :--- | :--- | :--- | :--- |
| `nucleo64/` | **UM1724** | *STM32 Nucleo-64 boards (MB1136)* | Nucleo-F303RE y Nucleo-F334R8: LED, botón, VCP, puentes. Y el **esquemático MB1136** (en la página de la placa, *CAD Resources*) |
| `nucleo32/` | **UM1956** | *STM32 Nucleo-32 boards (MB1180)* | Nucleo-F303K8, con el esquemático MB1180 |
| `nucleo144/` | **UM1974** | *STM32 Nucleo-144 boards (MB1137)* | Nucleo-F303ZE (el LQFP144, el FMC por los conectores) |
| `stm32f334/` | UM1735 (**por confirmar**) | *Discovery kit with STM32F334C8 MCU* (32F3348DISCOVERY) | Solo si se hace H3 con la Discovery |

### 4.3 Erratas (para no modelar un defecto como si fuera lo correcto)

| Carpeta | Código | Título |
| :--- | :--- | :--- |
| `stm32f303/` | **ES0204** | *STM32F303xB/C device errata* (también vale para lo que ya hay: S0-S9) |
| `stm32f303x8/` | **ES0246** | *STM32F303x6/x8 rev Z device limitations* |
| `stm32f303xe/` | (**por confirmar**) | *STM32F303xD/E device errata* |
| `stm32f334/` | (**por confirmar**) | *STM32F334xx device errata* |
| `stm32f303xe/` | **ES0297** | *STM32F398VE rev Y device limitations* (solo si se hace el F398) |
| `stm32f303x8/` | **ES0260** | *STM32F328C8 rev Z device limitations* (solo si se hace el F328) |

### 4.4 Útiles

| Carpeta | Código | Título | Para |
| :--- | :--- | :--- | :--- |
| `stm32f334/` | AN4539 (**por confirmar**) | *HRTIM cookbook* | H1-H3: los casos de uso del HRTIM |
| `stm32f3/` | **PM0214** | *STM32 Cortex-M4 MCUs and MPUs programming manual* | Si se toca el núcleo |
| `stm32f3/` | — | Los SVD de la familia F3 (paquete *STM32F3 System View Description* de st.com) | Contrastar por programa los mapas de registros de los bloques nuevos (FMC, TIM20, HRTIM) |

### 4.5 Lo que NO hace falta bajar a mano

Lo legible por máquina viene de GitHub y lo baja el propio trabajo, como en
S0 y S9:

* **La base de pines** (`STMicroelectronics/STM32_open_pin_data`, BSD-3):
  `STM32F303R(D-E)Tx`, `V(D-E)Tx`, `Z(D-E)Tx`, `STM32F303K/C/R(6-8)Tx`,
  `STM32F334K/C(4-6-8)Tx`, `STM32F334R(6-8)Tx`, y los dos ficheros de modos
  `GPIO-STM32F303E_gpio_v1_0_Modes.xml` y `GPIO-STM32F333_gpio_v1_0_Modes.xml`.
  Ya se han leído para este documento (las cifras de §1-§3 salen de ahí).
* **CMSIS** (`STMicroelectronics/cmsis_device_f3` v2.3.9, Apache-2.0):
  `stm32f303xe.h`, `stm32f303x8.h`, `stm32f334x8.h` y sus `startup_*.s`. Ya
  leídos (las tablas de §1.1 y §2.1 son su diferencia).
* **La HAL y los ejemplos** (`STM32CubeF3`): los de las tres Nucleo.

---

## 5. Riesgos

1. **Los códigos por confirmar.** Cinco de los documentos de §4 no se han
   podido comprobar desde aquí. El título es lo que vale.
2. **El HRTIM** puede costar tanto como todo lo demás junto. Por eso va al
   final y con salida en H0.
3. **Las tablas del F303x6/8** (DMA, ITR, remapeos) son lo que más se presta a
   equivocarse: RM0316 las da en tablas separadas por línea, y hay que
   contrastarlas con el `stm32f303x8.h` (los bits de `SYSCFG_CFGR1/3` que
   existen) antes de escribir una sola.
4. **La CCM** del F303xD/E (16 KB) y del F303x6/8 (4 KB) es de una sola fuente
   [1F] hasta que estén los datasheets: CMSIS solo da la base.
