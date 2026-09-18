# Fase F1 — Infraestructura del modelo SystemC del STM32F407VG

Informe de implementación de la fase **F1** del plan `doc/stm32f4xx/smt32f407vg_diseño.md`
(§7). Continúa a `doc/stm32f4xx/smt32f407vg_fase0.md` (esqueleto y contrato de
integración). Fuentes: `doc/stm32f4xx/informe_revisado.md` [IR] y
`doc/stm32f4xx/informe_instrucciones.md` [II].

**Alcance según el plan:** *"Infraestructura: ClockGen, reset, matriz LT,
flash/sram, cargador de binario"*.
**Criterio de salida:** *"lectura/escritura de memoria desde un maestro de
prueba"*.

**Resultado:** criterio cumplido y superado. El modelo compila sin avisos con
`-Wall -Wextra` y la suite de verificación pasa **124 de 124 comprobaciones**
(código de salida 0). Verificado con SystemC 2.3.4 / g++ 13.3 / C++17.

---

## 1. Resumen ejecutivo

| Bloque | Estado tras F1 |
| :--- | :--- |
| Matriz AHB 8×7 | Funcional: decodificación global, máscara de conectividad, arbitraje por puerto de esclavo, anotación de ciclos HCLK, errores de bus |
| Decodificadores AHB1/AHB2/APB1/APB2 | Funcionales, con error en huecos reservados y en bloques ausentes del F407 |
| Puentes AHB→APB1/APB2 | Funcionales, con penalización de 2 ciclos PCLK |
| Flash + ART + registros FLASH_* | Completo: array 1 MB, System Memory, OTP, option bytes, llaves, PG/SER/MER, errores, wait states, caché I/D y prefetch |
| SRAM1 / SRAM2 / BKPSRAM / CCM | Completas, con DMI y acceso de depuración |
| Bit-banding (SRAM y periféricos) | Completo, con RMW atómica |
| RCC: registros, árbol de reloj, gating | Completo salvo lo eléctrico (ver §6) |
| RCC: controlador de reset | Completo: POR/NRST/WWDG/IWDG/SW, pulso de 20 µs, flags de RCC_CSR |
| SYSCFG (MEMRMP/PMC/EXTICR/CMPCR) | Funcional |
| Router del núcleo | Alias de 0x0, CCM exclusiva del D-bus, PPB, bit-banding, etiquetado AhbExt |
| Muestreo de BOOT[1:0] | Completo (4º flanco de SYSCLK tras reset) |
| Maestro de prueba y cargador | Completos (.bin, Intel HEX, tabla de vectores) |

Código nuevo o reescrito en esta fase: **≈3 900 líneas** repartidas en 17
ficheros (`common/`, `bus/`, `mem/`, `rcc/`, `core/`, `periph/syscfg.h`,
`verif/`, `top/`).

---

## 2. Ficheros de la fase

### 2.1 Nuevos

| Fichero | Contenido |
| :--- | :--- |
| `src/bus/bitband.h` | Decodificación y acceso atómico de los alias de bit-banding [IR, §5.4] |
| `src/verif/bus_test_master.h` | Maestro TLM de verificación que impersona cualquiera de los 8 maestros de la matriz |
| `src/verif/image_loader.h` | Cargador de imágenes (.bin / Intel HEX) y generador de la tabla de vectores mínima |

### 2.2 Reescritos o ampliados

| Fichero | Cambio principal |
| :--- | :--- |
| `src/common/ahb_types.h` | Mapa de memoria completo, regiones ARMv7-M, tabla de sectores de Flash, tabla de estados de espera, alias de arranque, utilidades de payload TLM, tipos HTRANS/HBURST |
| `src/common/clock_gen.h` | Fase estable ante reprogramación; `period()`, `stopped()`, `clk_divide()` |
| `src/common/periph_base.h` | Transporte completo: accesos de 8/16/32 bits, byte enables, `transport_dbg`, respuestas AHB, anotación en ciclos |
| `src/bus/ahb_matrix.h` | Decodificación global, máscara §6.2, arbitraje, estadísticas, puerto de verificación |
| `src/bus/ahb_decoder.h` | `transport_dbg`, estadísticas, puente APB con comprobación de reloj del dominio |
| `src/mem/flash_if.h` | Implementación completa (ver §4) |
| `src/mem/sram.h` | Accesos byte-exactos, DMI, `transport_dbg`, `BkpSram` |
| `src/rcc/osc_pll.h` | Control por método, FSM de arranque, tiempo de lock del PLL, validación de rangos del VCO |
| `src/rcc/rcc.h` | Banco de registros completo, árbol de reloj, gating/reset por periférico, controlador de reset (ver §5) |
| `src/core/cortex_m4f.h` | Router con alias de 0x0, CCM directa, PPB, bit-banding y etiquetado `AhbExt` |
| `src/core/debug.h` | Transactor del AHB-AP (`ap_read32` / `ap_write32` / `ap_access`) |
| `src/periph/syscfg.h` | MEMRMP con valor de reset desde BOOT, PMC, EXTICR1-4, CMPCR |
| `src/top/stm32f407vg.h` | `hclk_hz` a la matriz, `BkpSram`, muestreo de BOOT en el 4º flanco de SYSCLK |
| `src/top/sc_main.cpp` | Suite de verificación F1 (14 grupos, 124 comprobaciones) |
| `src/Makefile.mcu-sim` | Objetivos `test` y `run IMG=...`; incluye `verif/` en las dependencias |

---

## 3. Buses

### 3.1 Matriz AHB (`AhbMatrix`)

Decodificación global [IR, §5.1, §6.5]:

| Rango | Destino |
| :--- | :--- |
| 0x0000 0000 – 0x0FFF FFFF | Flash (espejo de arranque + Flash principal) |
| 0x1000 0000 – 0x1000 FFFF | **CCM: error de bus** (no cuelga de la matriz) [IR, §5.3, §6.1.2] |
| 0x1001 0000 – 0x1FFF FFFF | Flash (System Memory, OTP, option bytes) |
| 0x2000 0000 – 0x2001 BFFF | SRAM1 |
| 0x2001 C000 – 0x2001 FFFF | SRAM2 |
| 0x2002 0000 – 0x3FFF FFFF | reservado → error |
| 0x4000 0000 – 0x4FFF FFFF | segmento AHB1 (incluye los puentes APB1/APB2) |
| 0x5000 0000 – 0x5FFF FFFF | segmento AHB2 |
| 0x6000 0000 – 0x9FFF FFFF | FSMC (bancos externos) |
| 0xA000 0000 – 0xA000 0FFF | FSMC (registros) |
| resto | reservado → error |

Dentro de la región Code, el bus **ICode** solo lo usa el maestro `CORE_IBUS`
con `AhbExt.instr = 1`; cualquier otro acceso a esa región se encamina al bus
**DCode** [IR, §5.1, §7.6].

La tabla de conectividad de [IR, §6.2] se codifica literalmente y se verifica
en el test T05 recorriendo los 48 pares maestro-esclavo alcanzables. Un camino
inexistente devuelve `TLM_ADDRESS_ERROR_RESPONSE`, que es el equivalente TLM de
`HRESP = ERROR` y de la BusFault correspondiente.

**Arbitraje (LT).** Cada puerto de esclavo lleva su instante de "ocupado hasta".
Una transacción que llega mientras el esclavo sigue ocupado recibe la
penalización en su anotación de tiempo (`t`), y el número de ciclos de espera se
devuelve al iniciador en `AhbExt.wait_cycles`. Los pares maestro-esclavo
disjuntos no se penalizan entre sí, que es la propiedad esencial de una matriz
multicapa [IR, §6-Implicaciones]. Además se anota **1 ciclo HCLK de fase de
dirección** por transacción [IR, §6.3]; la fase de datos la anota cada esclavo.

Contadores expuestos para verificación y trazas: `n_xfer[maestro][esclavo]`,
`n_err_conn`, `n_err_decode`, `n_err_ccm`, `n_contention`.

**Puerto de verificación `from_tb`.** Socket target opcional que no corresponde
a hardware: permite que un banco de pruebas inyecte transacciones declarando en
`AhbExt.master` qué maestro impersona. Sin él no sería posible comprobar las
filas de la tabla §6.2 correspondientes a maestros (DMA, ETH, OTG_HS) cuyo
comportamiento aún no está implementado.

### 3.2 Decodificadores y puentes APB

`AhbDecoder` responde con error en cualquier hueco no cubierto por un esclavo
registrado, lo que incluye los bloques que RM0090 documenta para las líneas
F42x/43x y F415/417 pero **no existen en el F407**: LTDC (0x4001 6800), SAI1
(0x4001 5800), DMA2D (0x4002 B000), SPI4/5/6, UART7-10 y CRYP/HASH
(0x5006 0000/0400) [IR, §6.5-nota]. El test T06 lo comprueba.

`AhbApbBridge` añade **2 ciclos del PCLK correspondiente** por acceso [IR, §6.4]
y devuelve error si el dominio APB está sin reloj. Medición del test T11 con
HCLK = 168 MHz, PCLK1 = 42 MHz y PCLK2 = 84 MHz:

| Destino | Coste medido |
| :--- | :--- |
| AHB1 directo (GPIOA) | 5 952 ps (1 ciclo HCLK) |
| APB2 (SYSCFG) | 29 762 ps (1 HCLK + 2 PCLK2) |
| APB1 (PWR) | 53 571 ps (1 HCLK + 2 PCLK1) |

### 3.3 Bit-banding

Implementado en `bus/bitband.h` y aplicado en el **router del núcleo**, no en la
matriz. Justificación: el bit-banding es una función del interfaz de bus del
Cortex-M4 sobre las regiones del mapa ARMv7-M, no del interconector de ST. La
consecuencia observable es la correcta: los maestros DMA que apuntan a
0x2200 0000 / 0x4200 0000 caen en rango no decodificado de la matriz y obtienen
error (test T09), mientras que la CPU y el AHB-AP sí ven los alias.

Fórmula implementada [IR, §5.4]:
`bit_word_addr = alias_base + (offset_de_byte × 32) + (nº_de_bit × 4)`

La escritura por el alias es una secuencia lectura-modificación-escritura de un
byte, atómica porque `b_transport` no cede el control entre ambas fases.

---

## 4. Memorias

### 4.1 Interfaz Flash (`FlashIf`)

Arrays modelados [IR, §5.2.1, §15.1]: Flash principal 1 MB (12 sectores),
System Memory 30 KB (0x1FFF 0000), OTP 528 B (0x1FFF 7800) y option bytes 16 B
(0x1FFF C000). Se mantiene además el espejo 0x0000 0000-0x000F FFFF sobre la
Flash principal, que es el modo de arranque por defecto con BOOT0 = 0.

**Registros** [IR, §5.5-§5.8], con sus valores de reset y efectos laterales:

| Offset | Registro | Reset | Comportamiento modelado |
| :--- | :--- | :--- | :--- |
| 0x00 | FLASH_ACR | 0x0000 0000 | LATENCY, PRFTEN, ICEN, DCEN; ICRST/DCRST solo con la caché deshabilitada; aviso si LATENCY < mínimo para la HCLK actual |
| 0x04 | FLASH_KEYR | 0x0000 0000 | Secuencia KEY1 = 0x45670123, KEY2 = 0xCDEF89AB; una secuencia rota bloquea FLASH_CR **hasta el siguiente reset** |
| 0x08 | FLASH_OPTKEYR | 0x0000 0000 | Secuencia 0x08192A3B / 0x4C5D6E7F |
| 0x0C | FLASH_SR | 0x0000 0000 | BSY (r); EOP/OPERR/WRPERR/PGAERR/PGPERR/PGSERR como rc_w1 |
| 0x10 | FLASH_CR | 0x8000 0000 | LOCK 'rs'; escrituras ignoradas con LOCK = 1; PG/SER/MER/SNB/PSIZE; STRT dispara el borrado y se limpia al bajar BSY; ERRIE/EOPIE gobiernan la IRQ 4 |
| 0x14 | FLASH_OPTCR | 0x0FFF AAED | OPTLOCK 'rs'; nWRP por sector; RDP; nRST_STOP/STDBY; WDG_SW; BOR_LEV; OPTSTRT |

**Programación y borrado** [IR, §5.9]: se reproduce el pseudocódigo del informe,
incluida la regla de que la Flash solo puede llevar bits de 1 a 0
(`memoria &= dato`), y se generan los errores documentados:

* `PGPERR` si la anchura del acceso no coincide con PSIZE;
* `PGAERR` si la dirección no está alineada a PSIZE;
* `WRPERR` si el sector tiene su bit nWRP a 0;
* `PGSERR` si la posición no está previamente borrada (≠ 0xFF);
* `OPERR` acompañando a cualquiera de los anteriores, y `EOP` en caso de éxito.

Los tiempos de programación y borrado **⚠ NO DISPONIBLES EN LAS FUENTES** se
exponen como parámetros públicos del módulo (`t_prog`, `t_erase`,
`t_mass_erase`) para que puedan ajustarse sin tocar el código.

**Estados de espera y ART** [IR, §5.2.2, §5.2.3]. La tabla de latencias para
VDD = 2.7-3.6 V está en `flash_min_latency()`:

| LATENCY | HCLK |
| :--- | :--- |
| 0 WS | ≤ 30 MHz |
| 1 WS | 30 < f ≤ 60 MHz |
| 2 WS | 60 < f ≤ 90 MHz |
| 3 WS | 90 < f ≤ 120 MHz |
| 4 WS | 120 < f ≤ 150 MHz |
| 5 WS | 150 < f ≤ 168 MHz |

El ART se modela con las geometrías del informe: caché de instrucciones de 64
líneas de 128 bits, caché de datos de 8 líneas de 128 bits y cola de prefetch.
Un acierto cuesta 1 ciclo; un fallo cuesta 1 + LATENCY ciclos. Con PRFTEN
activo, un fallo trae también la línea siguiente, de modo que el acceso
secuencial posterior es acierto (verificado en T08). El módulo publica
`art_hits()` y `art_misses()`.

**Cargador de imágenes**: `load_binary`, `load_binary_file` y `load_ihex_file`
(registros Intel HEX 00, 01, 02 y 04). Toda carga invalida las cachés del ART.

### 4.2 SRAM, CCM y BKPSRAM

`Sram` acepta accesos de cualquier longitud con byte enables, ofrece `DMI`
(necesario para acelerar el fetch de la CPU en F2), `transport_dbg` y acceso
directo `peek/poke` para verificación. Cero estados de espera.

`Ccm` es la misma clase con base 0x1000 0000: su único camino es el D-bus del
núcleo. `BkpSram` es la de 0x4002 4000, con su reset ligado al dominio de
backup y un método `erase_on_rdp_change()` para el borrado por bajada del nivel
de protección de lectura [IR, §4.1.3].

---

## 5. RCC: relojes y resets

### 5.1 Banco de registros

Implementados los 23 registros del mapa consolidado [IR, §4.12] con sus
máscaras de escritura, bits de solo lectura y valores de reset: `CR`,
`PLLCFGR`, `CFGR`, `CIR`, `AHB1/2/3RSTR`, `APB1/2RSTR`, `AHB1/2/3ENR`,
`APB1/2ENR`, `AHB1/2/3LPENR`, `APB1/2LPENR`, `BDCR`, `CSR`, `SSCGR`,
`PLLI2SCFGR`.

Comportamientos con efecto lateral reproducidos:

* los bits `xxxRDY` reflejan el estado real de la FSM del oscilador o del PLL,
  no un valor almacenado;
* `SWS` refleja la fuente **efectiva**: si la fuente pedida por `SW` no está
  lista, el árbol mantiene la anterior [IR, §4.5.3];
* no se puede apagar la fuente que alimenta SYSCLK: `HSION`, `HSEON` o `PLLON`
  se fuerzan a 1 según `SWS` [IR, §4.5.1] (verificado en T10);
* `PLLCFGR` y `PLLI2SCFGR` solo son modificables con su PLL apagado;
* `RCC_CIR`: los bits `xxxRDYF` se activan en el flanco de subida del RDY
  correspondiente y se limpian escribiendo 1 en el bit `xxxRDYC`; la IRQ 5 sale
  de `flags & IE` [IR, §4.6];
* `RCC_CSR.RMVF` borra los flags de fuente de reset; esos flags **sobreviven al
  reset de sistema** [IR, §4.1.1];
* `RCC_BDCR.BDRST` es un nivel: mientras está a 1 mantiene en reset el dominio
  de backup y el propio BDCR.

⚠ NO DISPONIBLE EN LAS FUENTES: el valor de calibración de fábrica `HSICAL`
(el informe lo indica como `XX` dentro de `0x0000 XX83`). El modelo usa 0x10 y
lo documenta como constante `HSICAL_FACTORY`.

### 5.2 Árbol de reloj

`update_clocks()` recalcula, en el mismo delta en que se escribe el registro:

```
fVCO_IN  = fPLL_IN / PLLM                (rango legal 1-2 MHz)
fVCO_OUT = fVCO_IN * PLLN                (rango legal 100-432 MHz)
PLLCLK   = fVCO_OUT / PLLP               (P = 2,4,6,8)
PLL48CK  = fVCO_OUT / PLLQ               (Q = 2..15)
SYSCLK   = mux SW {HSI, HSE, PLLCLK}
HCLK     = SYSCLK / HPRE                 (máx 168 MHz)
PCLK1    = HCLK / PPRE1                  (máx  42 MHz)
PCLK2    = HCLK / PPRE2                  (máx  84 MHz)
TIMxCLK  = PCLKx si PPREx == 1, si no 2 * PCLKx
RTCCLK   = mux RTCSEL {LSE, LSI, HSE/RTCPRE}, con RTCEN
SysTick  = HCLK / 8
MCO1/2   = mux + prescaler /1../5
```

Cada salida es una instancia de `ClockGen` reprogramada en caliente, de modo que
el resto del modelo ve una onda cuadrada real y, en paralelo, su frecuencia en
`sc_signal<double>`. Se emite un aviso si HCLK, PCLK1 o PCLK2 superan sus
límites o si el VCO queda fuera de rango.

Los tiempos de arranque son los del informe [IR, §4.2]: HSI 4 µs, HSE ~2 ms,
LSI 40 µs, LSE ~2 s. ⚠ NO DISPONIBLE EN LAS FUENTES: el tiempo de enganche del
PLL, expuesto como parámetro `Pll::t_lock_s` (200 µs por defecto).

Verificación de la secuencia canónica de 168 MHz (test T10): HSE de 8 MHz,
PLLM = 8, PLLN = 336, PLLP = 2, PLLQ = 7 → VCO de entrada 1 MHz, VCO de salida
336 MHz, SYSCLK = 168 MHz, PCLK1 = 42 MHz, PCLK2 = 84 MHz, PLL48CK = 48 MHz,
TIMCLK1 = 84 MHz. La frecuencia se comprueba además **midiendo 168 flancos
reales** de la señal HCLK.

### 5.3 Gating y reset por periférico

La tabla `RCC_BITMAP` relaciona cada par (registro, bit) de los ENR/RSTR con su
`PeriphId` [IR, §4.7, §4.8]. En cada escritura se refrescan los vectores
`periph_clk_en[]` y `periph_rst_n[]` del top. Un periférico sin reloj responde
`TLM_GENERIC_ERROR_RESPONSE` a cualquier acceso (test T02), que es el
comportamiento observable del silicio: la transacción no obtiene respuesta y el
núcleo levanta una BusFault.

### 5.4 Controlador de reset

Fuentes implementadas [IR, §4.1.1]: pin NRST, WWDG, IWDG, `SYSRESETREQ` del
SCB y POR/PDR-BOR. Secuencia:

1. se latchea el flag correspondiente en `RCC_CSR`;
2. se llevan a valores de reset todos los registros **excepto** `RCC_CSR` y
   `RCC_BDCR`;
3. `sys_rst_n` y `bkp_rst_n` bajan, y NRST se fuerza a nivel bajo por su driver
   open-drain durante al menos **20 µs**;
4. se libera NRST y se espera a que POR y el pin estén en nivel alto;
5. se sale de reset con HSION = 1 y SW = HSI [IR, §4.5.1].

El nivel bajo del pin NRST provocado por el propio reset interno no se
contabiliza como `PINRSTF`, ya que NRST es bidireccional open-drain.

La temporización total "VDD estable → primera instrucción" que el informe sitúa
entre 0.5 y 3.0 ms (típico 1.5 ms) se expone como parámetro `t_rst_release`,
con valor por defecto 20 µs para no penalizar el tiempo de simulación; basta
asignarle 1.5 ms para reproducir el comportamiento del silicio.

---

## 6. Núcleo, SYSCFG y arranque

`CortexM4F` implementa el router completo:

| Camino | Destino |
| :--- | :--- |
| I-bus, dirección < 0x2000 0000 | ICode (matriz) tras aplicar el alias de 0x0 |
| I-bus, dirección ≥ 0x2000 0000 | bus System [IR, §5.1, §7.6] |
| D-bus, 0x1000 0000-0x1000 FFFF | CCM directa, fuera de la matriz [IR, §5.3] |
| D-bus/S-bus, 0xE000 0000-0xE00F FFFF | SCS (SCB/NVIC/SysTick/MPU) o componentes de depuración |
| D-bus/S-bus, alias de bit-banding | RMW atómica sobre la región base |
| D-bus, resto < 0x2000 0000 | DCode (matriz) |
| resto | bus System |

Cada transacción se etiqueta con una extensión `AhbExt` (maestro y HPROT) que la
matriz usa para el arbitraje y la máscara de conectividad. La extensión se
"presta" al payload y se retira al salir, sin reservar memoria por transacción.

`Syscfg` implementa `MEMRMP` (cuyo valor de reset lo imponen los pines BOOT),
`PMC`, `EXTICR1-4` y `CMPCR`. El top muestrea **BOOT0 (pin) y BOOT1 (PB2) en el
4º flanco ascendente de SYSCLK tras la salida de reset** [IR, §2.3] y los
mantiene hasta el siguiente reset. La tabla resultante:

| BOOT1 | BOOT0 | MEM_MODE | 0x0000 0000 refleja |
| :---: | :---: | :--- | :--- |
| x | 0 | 00 | Flash principal |
| 0 | 1 | 01 | System Memory (bootloader) |
| 1 | 1 | 11 | SRAM1 |

`DebugSys` incorpora el transactor del AHB-AP (`ap_read32`/`ap_write32`), que es
la vía por la que una sonda accede a la memoria del sistema con el mismo mapa
que la CPU. En F6 lo gobernará la FSM SWD/JTAG; en F1 sirve además para
verificar la CCM y el bit-banding.

---

## 7. Verificación

`make test407` compila `top/sc_main.cpp` y ejecuta una suite autocomprobable
(código de salida 0 si todo pasa). Resultado actual: **124 OK, 0 fallos**.

| Grupo | Comprobaciones | Referencia |
| :--- | :--- | :--- |
| T01 Power-up, reset y estado inicial del RCC | 13 | [IR, §4.1, §4.5, §4.12] |
| T02 Gating y reset por RCC_xxxENR/RSTR | 7 | [IR, §4.7, §4.8] |
| T03 Lectura/escritura de memoria (8/16/32/bloque) | 13 | criterio de salida de F1 |
| T04 CCM: solo por el D-bus del núcleo | 6 | [IR, §5.3, §6.1.2] |
| T05 Máscara de conectividad (48 pares) | 2 | [IR, §6.2] |
| T06 Rangos reservados y bloques ausentes | 7 | [IR, §6.5] |
| T07 Flash: llaves, borrado, programación, errores | 20 | [IR, §5.5-§5.9] |
| T08 Estados de espera y ART | 7 | [IR, §5.2.2, §5.2.3] |
| T09 Bit-banding (SRAM y periféricos) | 8 | [IR, §5.4] |
| T10 Árbol de reloj HSE+PLL → 168 MHz | 16 | [IR, §4.3, §4.4, §4.5] |
| T11 Penalización del puente AHB→APB | 2 | [IR, §6.4] |
| T12 Contención en un puerto de esclavo | 2 | [IR, §6.7] |
| T13 Reset por NRST y flags de RCC_CSR | 7 | [IR, §4.1, §4.10] |
| T14 Cargador de imagen y alias de arranque | 14 | [IR, §2.3, §5.1, §12.21.2] |

Comprobaciones adicionales realizadas sobre el modelo:

* compilación con `-Wall -Wextra`: **sin avisos**;
* ejecución bajo `valgrind`: **0 bytes perdidos definitivamente y ningún error
  de montículo**. Los avisos que aparecen se refieren todos a las pilas de las
  corrutinas QuickThreads de SystemC ("Address is on thread 1's stack"), un
  falso positivo conocido de la biblioteca compilada sin corrutinas pthread;
* SystemC detecta y rechaza dos escritores sobre un mismo `sc_signal`. En los
  módulos cuyo estado actualizan tanto sus procesos internos como el
  `b_transport` del banco de registros (RCC, SYSCFG, Flash), un único proceso
  `publish_proc` escribe los puertos de salida a partir de variables C++
  ordinarias. El resto del modelo debe seguir esta convención.

---

## 8. Decisiones de diseño de esta fase

| # | Decisión | Motivo |
| :--- | :--- | :--- |
| **F1-1** | El bit-banding se aplica en el router del núcleo, no en la matriz | Es una función del mapa ARMv7-M, no del interconector de ST. Efecto observable correcto: el DMA no ve los alias |
| **F1-2** | Osciladores y PLL se controlan por método (`enable`, `configure`), no por puerto de entrada | El RCC escribe estos controles desde el `b_transport` de un registro y necesita el efecto dentro del mismo delta; con `sc_signal` el árbol quedaría un ciclo por detrás del banco de registros |
| **F1-3** | Cada módulo con estado compartido publica sus puertos desde un único proceso (`publish_proc`) | SystemC prohíbe dos escritores sobre un `sc_signal`, y el `b_transport` corre en el proceso del maestro |
| **F1-4** | La matriz expone un puerto de verificación `from_tb` | Permite comprobar las filas de la tabla §6.2 correspondientes a maestros aún no implementados (DMA, ETH, OTG_HS) sin tocar la topología |
| **F1-5** | El alias de 0x0 lo resuelve el router del núcleo; la Flash mantiene además su espejo por defecto | Es el punto donde se conoce MEM_MODE. Se anota como pendiente de F3 el espejo a SRAM/FSMC visto por maestros distintos del núcleo |
| **F1-6** | Los tiempos no documentados (programación/borrado de Flash, lock del PLL, temporización total de reset) son parámetros públicos, no constantes ocultas | Regla de exhaustividad: lo que no está en las fuentes se marca y se deja configurable |
| **F1-7** | `t_rst_release` por defecto 20 µs en lugar de los 1.5 ms típicos | Evita penalizar cada arranque de simulación; el valor real es una asignación |

---

## 9. Trabajo pendiente heredado a fases posteriores

**F2 (CPU):**

* bucle fetch/decode/execute con la ISA completa [II] y las excepciones;
* uso de DMI sobre SRAM y Flash para acelerar el fetch (la infraestructura ya
  está disponible en `Sram::dmi`);
* monitores exclusivos LDREX/STREX sobre `AhbExt.exclusive`;
* comprobación XN y filtrado de la MPU antes de emitir cada transacción.

**F3 (pines y RCC eléctrico):**

* detección eléctrica de cristal/reloj externo en HSE y LSE a través del
  `AnalogNet` del pad (`Oscillator::xtal_in`);
* Clock Security System: detección de fallo del HSE, conmutación automática a
  HSI y línea NMI (`nmi_css` ya está cableada) [IR, §4.2];
* modulación de espectro ensanchado (`RCC_SSCGR`) [IR, §4.11.1];
* encaminamiento de MCO1 (PA8) y MCO2 (PC9) a sus pads; los generadores ya
  producen la frecuencia correcta en `mco1_sig`/`mco2_sig`;
* umbrales reales de POR/PDR/BOR en `PowerPads` (hoy 1.7 V aproximado) y
  niveles `BOR_LEV` de los option bytes;
* frecuencia de dominio en cada `BusSlave` (`set_domain_hz`) para que la fase de
  datos de los periféricos se anote en ciclos reales.

**F4 y posteriores:**

* elevación de la matriz a AT (nb_transport con HTRANS/HBURST y arbitraje ciclo
  a ciclo); los tipos `HTrans`/`HBurst` y la topología ya están preparados;
* temporización real del FSMC (BTR/BWTR) y reparto por bancos NE1-4;
* revisión de la máscara de conectividad para el acceso mem-to-mem de DMA2
  desde Flash-D.
