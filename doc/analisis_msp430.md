# MSP430 en `mcu-sim`

**Análisis y plan por fases** · rama `msp430` · 09-11-2026

El objetivo es que `mcu-sim` simule **diez MCUs MSP430 de Texas Instruments, de
cuatro familias**, igual que hoy simula los STM32, los MSP432 y el ESP32-C3. Eso
quiere decir:

* ejecutar sin recompilar el binario que sale de Code Composer Studio o de
  `msp430-elf-gcc`;
* tener pines eléctricos y placas en XML con su dibujo SVG;
* tener consola serie por el *backchannel* de la LaunchPad;
* y descargar y depurar con las herramientas de siempre.

La lista de MCUs y de LaunchPads es la de `doc/refs/msp430/`:

| Familia (guía de usuario) | MCU | LaunchPad |
| :--- | :--- | :--- |
| F2xx/G2xx (SLAU144K) | MSP430G2553 | MSP-EXP430G2ET |
| F5xx/F6xx (SLAU208Q) | MSP430F5529 | MSP-EXP430F5529LP |
| FR2xx/FR4xx (SLAU445I) | MSP430FR2311 | MSP-EXP430FR2311 |
| | MSP430FR2355 | MSP-EXP430FR2355 |
| | MSP430FR2433 | MSP-EXP430FR2433 |
| | MSP430FR4133 | MSP-EXP430FR4133 |
| | MSP430FR2476 | LP-MSP430FR2476 |
| FR5xx/FR6xx (SLAU367P) | MSP430FR5969 | MSP-EXP430FR5969 |
| | MSP430FR5994 | MSP-EXP430FR5994 |
| | MSP430FR6989 | MSP-EXP430FR6989 |

El plan tiene tres condiciones que lo ordenan todo:

1. **Tienen que poder entrar MSP430 nuevos sin tocar el núcleo.** Pueden ser de
   estas familias (un FR2153, un FR5739, un F5438A, un G2231) o de otras (un
   F1xx, un F4xx con LCD, un i20xx, un FR6047 con USS, un CC430). Por eso la §3
   separa el núcleo de las familias, y la §5 hace que un MCU nuevo sea, sobre
   todo, **datos**.
2. **Depuración por los pines**, con el JTAG de 4 hilos y el Spy-Bi-Wire de 2
   hilos tal y como los describe SLAU320AJ, y **depuración por stub**, como
   `--gdb-dap`.
3. **El cargador de arranque (BSL)** de las tres generaciones:
   * la ROM de 1xx/2xx/4xx y la Flash de 5xx/6xx, según SLAU319AF;
   * y la ROM de las FRAM, según SLAU550G.

Referencias de este documento, todas en `doc/refs/msp430/` y sin versionar:

| Clave | Documento |
| :--- | :--- |
| [UG2] | MSP430F2xx, MSP430G2xx Family User's Guide, SLAU144K (2022) |
| [UG5] | MSP430F5xx and MSP430F6xx Family User's Guide, SLAU208Q |
| [UGFR2] | MSP430FR4xx and MSP430FR2xx Family User's Guide, SLAU445I (2019) |
| [UGFR5] | MSP430FR58xx, FR59xx, FR68xx and FR69xx Family User's Guide, SLAU367P (2020) |
| [CPUX] | MSP430x5xx/6xx CPUX (extracto de SLAU208), SLAU391F (2018) |
| [JTAG] | MSP430 Programming With the JTAG Interface, SLAU320AJ (2021) |
| [BSL] | MSP430 Flash Devices Bootloader (BSL), SLAU319AF (2022) |
| [BSLFR] | MSP430FR57xx, FR58xx, FR59xx, FR68xx and FR69xx Bootloader (BSL), SLAU550G |
| [DBGUG] | MSP Debuggers User's Guide, SLAU647O (2020) |
| [GCC] | MSP430 GCC Toolchain User's Guide, SLAU646F |
| [DS-G2553] | MSP430G2x53, G2x13 Mixed Signal MCU, SLAS735J |
| [DS-F5529] | MSP430F552x, F551x, SLAS590P |
| [DS-FR2311] | MSP430FR231x, SLASE58E |
| [DS-FR2355] | MSP430FR235x, FR215x, SLASEC4D |
| [DS-FR2433] | MSP430FR243x, SLASE59F |
| [DS-FR4133] | MSP430FR413x, SLAS865F |
| [DS-FR2476] | MSP430FR247x, SLASEO7C |
| [DS-FR5969] | MSP430FR59xx, SLAS704G |
| [DS-FR5994] | MSP430FR599x, FR596x, SLASE54D |
| [DS-FR6989] | MSP430FR698x, FR598x, SLAS789D |
| [ER-…] | Fe de erratas de cada MCU (§1.1) |
| [LP-…] | Guía de usuario de cada LaunchPad, con sus esquemáticos (`launchpads/<placa>/`) |
| [CCS] | Carpeta `msp430` de CCS 11.2: `include/` (cabeceras y `.cmd`), `include_gcc/` (cabeceras, `.ld` y **`devices.csv`**), `lib/` y `templates/` |

---

## 0. Resumen

* **La documentación está completa para el núcleo, los diez MCUs, sus erratas,
  el JTAG y el BSL de 1xx/2xx/4xx, 5xx/6xx y FR5xx/FR6xx.** Hay **un hueco**: el
  BSL de las **FR2xx/FR4xx**. SLAU550G es de 2017 y solo trata las FR57xx, FR58xx,
  FR59xx, FR68xx y FR69xx. Las hojas de datos de las cinco FR2xx/FR4xx remiten a
  la «MSP430 FRAM Devices Bootloader (BSL) User's Guide», que es **SLAU550 en su
  revisión AB** (septiembre de 2022). Sus erratas citan además SLAU610. Hasta que
  esté, la fase del BSL de las FR2xx/FR4xx queda bloqueada, y lo demás no (§1.2).
* **Los tres núcleos son uno con rasgos.** La CPU de 16 bits del G2553, la CPUX
  (ningún MCU de la lista la usa, pero sí los F2xx grandes y los F4xx) y la
  CPUXV2 de los otros nueve comparten registros, modos de direccionamiento y
  codificación. Lo que cambia es esto:
  * si existen las instrucciones de 20 bits y la palabra de extensión;
  * **la tabla de ciclos**, que CPUX y CPUXV2 no comparten;
  * el comportamiento de algunos casos límite, que está en las erratas de CPU.

  El núcleo va en `src/m430/` con un `M430Caps` (§3). Se valida contra **dos
  simuladores independientes que ya trae el *toolchain* o se instalan con
  `apt`**:
  * el simulador de GDB (`msp430-elf-run`), con CPUX;
  * y el de `mspdebug` (`sim`), con la CPU de 16 bits.
* **La mitad de los periféricos ya existe.** El MSP432P4 tiene los módulos de
  MSP430 detrás de un puente de 16 bits (`periph/msp432p4/bus16.h`): eUSCI,
  Timer_A, WDT_A, RTC_C, Comp_E, REF_A, AES256, CRC32 y los puertos. Esos
  módulos pasan a `src/periph/ti/` y se comparten (M0). El MSP432 no puede mover
  su invariante al hacerlo.
* **Un MCU nuevo es, sobre todo, datos.** `include_gcc/devices.csv` de CCS da, para
  **693 dispositivos**:
  * el tipo de CPU y las erratas de CPU que tiene;
  * el multiplicador, el SBW, la versión del EEM y sus disparadores;
  * y el mapa de memoria completo (RAM, RAM de USB y de LEA, BSL, información,
    FRAM o Flash, firma y vectores).

  Las cabeceras dan la dirección base de cada módulo (`__MSP430_BASEADDRESS_*`)
  y la tabla de vectores. Un generador (`gen_msp430.py`) saca de ahí el
  descriptor de cada chip. A mano quedan los pines y las funciones de cada pin,
  que salen de la hoja de datos, y los módulos que aún no estén modelados (§5).
* **Depuración por los pines (§7).** Se modelan:
  * el TAP de MSP430 con su IR de 8 bits;
  * las cuatro generaciones de JTAG ID (0x89, 0x91, 0x98 y 0x99);
  * el registro de señales de control;
  * el **Spy-Bi-Wire** sobre TEST/SBWTCK y RST/SBWTDIO, y la secuencia de entrada;
  * el **buzón JTAG (JMB)**;
  * el fusible del G2553 y el bloqueo por firma de las 5xx y las FR;
  * y el **EEM** con sus tamaños (XS, S y L), que es donde viven los puntos de
    ruptura.

  Para alcanzarlo con herramientas reales hay tres sondas. Por orden de
  dificultad:
  1. **una sonda de bancada propia** que ejecuta las secuencias de SLAU320 (el
     camino de `--gdb` en los ARM);
  2. **`mspdebug` compilado de git** con un controlador de *bit-bang*;
  3. **la eZ-FET de la LaunchPad emulada**, para que la usen la MSP Debug Stack
     de TI (`libmsp430`), el agente de GDB de TI, CCS, UniFlash y
     `mspdebug tilib`.
* **Depuración por stub (§8).** `--gdb-dap` en un MSP430 es el stub de GDB
  pegado directamente al control de la CPU y al EEM, sin pasar por los pines. Se
  comporta como la herramienta real, según `herramienta_if`: el EEM para los
  relojes que `libmsp430` para por omisión, y por eso el WDT no muerde con la CPU
  parada. Funciona con `msp430-elf-gdb` y con `--stopped-core-stops-sim`.
* **BSL (§9).** Las tres variantes son tres protocolos:
  * la de 1xx/2xx/4xx (SYNC 0x80, 9600 baudios, 8E1);
  * la de 5xx/6xx y FR (paquete 0x80 con CRC-CCITT, por UART, I2C 0x48 o HID
    USB con VID 0x2047 y PID 0x0200);
  * y la entrada por TEST/RST.

  Se modelan como en el MSP432P4: un **BSL sustituto propio** que corre en la CPU
  simulada, el de TI desde fichero si alguien lo tiene, o ninguno. Se prueban con
  `python-msp430-tools` y con el BSL-Scripter de TI.
* **Dependencia.** Este plan se apoya en la infraestructura de la rama
  `esp32rv`, que todavía no está en `main`. La rama `msp430` sale de
  `main` (`69f2582`), así que **la fase M0 empieza integrando `esp32rv`**:
  * `GdbRspBase` con sus *backends*;
  * `herramienta_if`;
  * `congela.h` y `--stopped-core-stops-sim`;
  * `mcu_if::nodo_depuracion`;
  * y las sondas con JTAG.
* **El plan tiene trece fases (M0-M12, §11):**
  * M0 prepara el terreno;
  * M1 y M2 son el núcleo y su depuración en un laboratorio sin chip;
  * de M3 a M8, los chips por familias, cada uno con su LaunchPad;
  * de M9 a M11, el BSL y la eZ-FET;
  * M12, la GUI.

---

## 1. La documentación: qué hay y qué falta

### 1.1 Erratas: las diez están

| MCU | Fe de erratas | Fecha de la revisión | Otros documentos que cita |
| :--- | :--- | :--- | :--- |
| MSP430G2553 | SLAZ440K | mayo de 2021 | — |
| MSP430F5529 | SLAZ314AD | mayo de 2021 | — |
| MSP430FR4133 | SLAZ550AA | mayo de 2021 | SLAU610 (BSL) |
| MSP430FR2433 | SLAZ664S | mayo de 2021 | SLAU610 |
| MSP430FR2311 | SLAZ679R | agosto de 2021 | SLAU610 |
| MSP430FR2355 | SLAZ695J | septiembre de 2021 | SLAU610 |
| MSP430FR2476 | SLAZ726B | agosto de 2021 | SLAU610 |
| MSP430FR5969 | SLAZ473AG | agosto de 2021 | — |
| MSP430FR6989 | SLAZ517AG | agosto de 2021 | — |
| MSP430FR5994 | SLAZ681O | agosto de 2021 | — |

Las erratas de CPU que afectan al modelo vienen, además, en una segunda fuente
legible por programa: la columna `CPU_Bugs` de `devices.csv`. El G2553 no tiene
ninguna. El F5529 tiene CPU21, CPU22, CPU23, CPU27, CPU29, CPU30 y CPU40. Todas
las FR tienen CPU21, CPU22 y CPU40. Así se contrasta la hoja de erratas con el
fichero (§10.2).

### 1.2 El hueco: el BSL de las FR2xx/FR4xx

* **SLAU550G** (la que hay) es la revisión G, de enero de 2017. Su título y su
  contenido son de las FR57xx, FR58xx, FR59xx, FR68xx y FR69xx. Basta para el
  FR5969, el FR5994 y el FR6989.
* Las FR2xx/FR4xx tienen otro BSL. En las FR2311 y FR2476 está partido en
  **BSL1**, en ROM (0x1000-0x17FF), y **BSL2**, en la parte alta (0xFFC00). En
  las FR2433, FR2355 y FR4133 está en ROM, en 0x1000. La entrada también cambia:
  se entra en el BSL con el vector de reset en blanco, y las interfaces son
  otras. Todo eso está en la **revisión AB de SLAU550** (septiembre de 2022),
  que añadió las FR2xx/FR4xx al mismo documento. Hay que **sustituir SLAU550G
  por SLAU550AB**, o añadirla al lado.
* **SLAU610**, «MSP430FR4xx and MSP430FR2xx Bootloader (BSL) User's Guide», es la
  guía que citan las erratas de las cinco FR2xx/FR4xx. TI la ha ido absorbiendo
  en SLAU550, pero las erratas la nombran. Conviene tenerla si todavía se puede
  descargar; si no, basta SLAU550AB.

Sin esos documentos, la fase **M10 (BSL de las FR2xx/FR4xx) queda bloqueada**.
Lo demás no depende de ellos.

### 1.3 Lo que conviene añadir, sin que bloquee nada

| Documento | Para qué | Fase |
| :--- | :--- | :--- |
| **MSP Debug Stack** (*MSPDS open source*, BSD), con el *firmware* de la MSP-FET y la eZ-FET | Segunda fuente del EEM a nivel de registro, de las secuencias de JTAG y de las órdenes HAL de la sonda. Es imprescindible para emular la eZ-FET (M11) | M2, M11 |
| **Replicator430**, el código de ejemplo que acompaña a SLAU320 | Segunda fuente de las secuencias de JTAG y SBW: es el programa de referencia de TI | M2 |
| `ccs_base/common/targetdb/` de CCS (`devices/msp430*.xml` y `modules/*.xml`) | Descripción de registros y campos por módulo: segunda fuente de los valores de reset y de los bits | M3-M8 |
| **MSPBSL** (*BSL Scripter* y los fuentes del BSL a medida de 5xx/6xx y FRxx) | Segunda fuente del protocolo y base del BSL sustituto | M9-M10 |
| SLAA450 «Creating a Custom Flash-Based BSL» y SLAA721 «Customizing the MSP430 FRAM Bootloader» | Cómo está hecho el BSL por dentro: memoria, Z-Area y entradas públicas | M9-M10 |
| Esquemáticos de la eZ-FET (vienen en las guías de las LaunchPad) | Ya están. Se citan en la placa | M3-M8 |

### 1.4 Herramientas

* **`msp430-elf-gcc` 9.3.1.11** (`msp430-gcc-opensource/`). Trae `gcc`, `gdb` y
  `msp430-elf-run`, el simulador de GDB con CPUX y multiplicador, que sirve de
  oráculo para M1. El instalador completo (9.3.1.2) añade las cabeceras y los
  guiones de enlazado de cada chip y, según [GCC], el **agente de GDB de TI**
  (`gdb_agent_console` con `libmsp430`). Hay que comprobarlo al desempaquetarlo
  en M11.
* **`mspdebug`**. La versión de `apt` es la 0.22, y trae `sim`, `gdbc`, `tilib`,
  `uif`, `uif-bsl`, `flash-bsl`, `goodfet`, `olimex*`, `rf2500` y `pif`. Le
  falta `rom-bsl` y los controladores de *bit-bang* modernos. En M2 se compila
  de git, como se hizo con OpenOCD para el SWD.
* **`python-msp430-tools`** (pip). Trae `msp430.bsl.target` (BSL de 1xx/2xx/4xx),
  `msp430.bsl5.uart` (5xx/6xx/FR por UART) y `msp430.bsl5.hid` (F5529 por USB).
  Es la herramienta real del BSL para la integración continua.
* **BSL-Scripter** de TI (instaladores en `doc/refs/`). Es la herramienta real
  del BSL en las prácticas de laboratorio.

---

## 2. Los diez chips, según sus hojas de datos

### 2.1 Vista de conjunto

Los datos de CPU, multiplicador, EEM y disparadores son de `devices.csv`,
contrastados con la hoja de datos de cada chip. Los módulos salen de las
cabeceras de [CCS] (`__MSP430_HAS_*`).

| MCU | CPU | MPY | Reloj | Memoria | EEM (disparadores) | JTAG ID | BSL |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| G2553 | CPU (16 bits) | — | BCS+ (BC2), 16 MHz | Flash 16 KB, RAM 512 B | XS (2) | 0x89 | ROM 1xx/2xx (UART por P1.1/P1.5) |
| F5529 | CPUXV2 | MPY32 | UCS + PMM, 25 MHz | Flash 128 KB, RAM 8 KB + 2 KB de USB | L (8) | 0x91 | Flash, USB HID |
| FR2311 | CPUXV2 | — | CS con FLL, 16 MHz | FRAM 3,75 KB, RAM 1 KB | S (3) | 0x98 | ROM BSL1 + BSL2 |
| FR2355 | CPUXV2 | MPY32 | CS con FLL, 24 MHz | FRAM 32 KB, RAM 4 KB, ROM de bibliotecas 20 KB | S (3) | 0x98 | ROM |
| FR2433 | CPUXV2 | MPY32 | CS con FLL, 16 MHz | FRAM 15,5 KB, RAM 4 KB | S (3) | 0x98 | ROM |
| FR4133 | CPUXV2 | — | CS con FLL, 16 MHz | FRAM 15,5 KB, RAM 2 KB | S (3) | 0x98 | ROM |
| FR2476 | CPUXV2 | MPY32 | CS con FLL, 16 MHz | FRAM 64 KB, RAM 8 KB | S (3) | 0x98 | ROM BSL1 + BSL2 |
| FR5969 | CPUXV2 | MPY32 | CS, 16 MHz | FRAM 64 KB, RAM 2 KB | S (3) | 0x99 | ROM (UART, I2C) |
| FR5994 | CPUXV2 | MPY32 | CS, 16 MHz | FRAM 256 KB, RAM 4 KB + 4 KB compartida con LEA | S (3) | 0x99 | ROM |
| FR6989 | CPUXV2 | MPY32 | CS, 16 MHz | FRAM 128 KB, RAM 2 KB | S (3) | 0x99 | ROM |

El JTAG ID de cada MCU sale de las tablas de dispositivos de [JTAG], cap. 3 y
anexos: 0x89 para 1xx/2xx/4xx, 0x91 para 5xx/6xx, 0x98 para FR2xx/FR4xx y 0x99
para FR5xx/FR6xx. Se confirma al cerrar cada fase contra la tabla del anexo.

### 2.2 Mapas de memoria

| MCU | Memoria principal | RAM | Información | BSL | Otras regiones |
| :--- | :--- | :--- | :--- | :--- | :--- |
| G2553 | Flash C000-FFFF (vectores FFC0-FFFF) | 0200-03FF | 1000-10FF (A-D de 64 B; A, calibración) | ROM 0C00-0FFF | Periféricos de 16 bits 0100-01FF, de 8 bits 0010-00FF, SFR 0000-000F |
| F5529 | Flash 004400-0243FF, bancos A-D (vectores FF80-FFFF) | 2400-43FF | 1800-19FF (A-D) | Flash 1000-17FF | RAM de USB 1C00-23FF; TLV 1A00-1AFF |
| FR2311 | FRAM F100-FFFF | 2000-23FF | — | BSL1 1000-17FF, BSL2 FFC00-FFFFF | TLV 1A00 |
| FR2355 | FRAM 8000-FFFF | 2000-2FFF | 1800-19FF | 1000-17FF | ROM FAC00-FFBFF (DriverLib y FFT); RAM diminuta 0006-001F |
| FR2433 | FRAM C400-FFFF | 2000-2FFF | 1800-19FF | 1000-17FF | — |
| FR4133 | FRAM C400-FFFF | 2000-27FF | 1800-19FF | 1000-13FF | Memoria de LCD |
| FR2476 | FRAM 8000-FF7F + 10000-17FFF | 2000-3FFF | 1800-19FF | BSL1 1000-17FF, BSL2 FFC00 | RAM diminuta 0006-001F |
| FR5969 | FRAM 4400-FF7F + 10000-13FFF | 1C00-23FF | 1800-19FF (A-D) | 1000-17FF | TLV 1A00-1AFF |
| FR5994 | FRAM 4000-FF7F + 10000-43FFF | 1C00-2BFF + 2C00-3BFF (LEA) | 1800-19FF | 1000-17FF | RAM diminuta 000A-001F |
| FR6989 | FRAM 4400-FF7F + 10000-23FFF | 1C00-23FF | 1800-19FF | 1000-17FF | ROM de arranque 1B00-1BFF; RAM diminuta 0006-001F; RAM del ESI |

Los vectores de todas las 5xx y FR están en FF80-FFFF (FF88 o FFA2 en
algunas FR2xx/FR4xx, según `devices.csv`). La firma de JTAG y BSL está en
FF80-FF8F.

Cada fila sale de dos fuentes, la tabla «Memory Organization» de la hoja de datos
y las columnas de `devices.csv`, y coinciden. **Por encima de 0xFFFF** solo hay
memoria en el F5529, el FR2476, el FR5969, el FR5994 y el FR6989: son los que
obligan a probar el modelo de código y datos grande (`-mlarge`) del compilador.

### 2.3 Periféricos

| Módulo | G2553 | F5529 | FR2311 | FR2355 | FR2433 | FR4133 | FR2476 | FR5969 | FR5994 | FR6989 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Puertos (con PxREN) | P1-P3 | P1-P8, PJ | P1-P2 | P1-P6 | P1-P3 | P1-P8 | P1-P6 | P1-P4, PJ | P1-P8, PJ | P1-P10, PJ |
| Reasignación de pines | — | PMAP (P4) | SYSCFG | SYSCFG | SYSCFG | — | SYSCFG | — | — | — |
| WDT | WDT+ | WDT_A | WDT_A | WDT_A | WDT_A | WDT_A | WDT_A | WDT_A | WDT_A | WDT_A |
| Timer_A | 2 × TA3 | TA5, 2 × TA3 | — | — | 2 × TA3, 2 × TA2 | 2 × TA3 | 4 × TA3 | 2 × TA3, 2 × TA2 | 5 × TA | TA3, TA3, TA2, TA5 |
| Timer_B | — | TB7 | 2 × TB3 | 3 × TB3 + TB7 | — | — | TB7 | TB7 | TB7 | TB7 |
| Comunicaciones | USCI_A0/B0 | 2 × USCI_A, 2 × USCI_B | eUSCI_A0/B0 | 2 × eUSCI_A, 2 × eUSCI_B | 2 × eUSCI_A, eUSCI_B | eUSCI_A0/B0 | 2 × eUSCI_A, 2 × eUSCI_B | 2 × eUSCI_A, eUSCI_B | 4 × eUSCI_A, 4 × eUSCI_B | 2 × eUSCI_A, 2 × eUSCI_B |
| ADC | ADC10 | ADC12_A | ADC 10 bits | ADC 12 bits | ADC 10 bits | ADC 10 bits | ADC 12 bits | ADC12_B | ADC12_B | ADC12_B |
| Comparador | Comp_A+ | Comp_B | eCOMP | 2 × eCOMP | — | — | eCOMP | Comp_E | Comp_E | Comp_E |
| Analógico propio | — | REF | SAC-L1, TIA | 4 × SAC-L3 | — | — | — | REF_A | REF_A | REF_A |
| RTC | — | RTC_A | RTC contador | RTC contador | RTC contador | RTC contador | RTC contador | RTC_B | RTC_C | RTC_C |
| CRC | — | CRC16 | CRC16 | CRC16 | CRC16 | CRC16 | CRC16 | CRC16 | CRC16 + CRC32 | CRC16 + CRC32 |
| DMA | — | 3 canales | — | — | — | — | — | 3 canales | 6 canales | 3 canales |
| Otros | — | **USB**, PMM con LDO | CapTIvate IO | ICC, MFM | Backup RAM | **LCD_E**, CapTIvate IO | — | AES256, MPU | AES256, MPU, **LEA** | AES256, MPU, **LCD_C**, **ESI** |

El G2553 tiene además el BCS+ (BC2), el controlador de Flash de 2xx (FLASH2) y
el CAPLUS. Las FR2xx/FR4xx tienen la protección de escritura de la FRAM en
SYSCFG0 (PFWP/DFWP). Las FR5xx/FR6xx tienen el controlador de FRAM con estados
de espera y caché, la MPU y la IPE. Todas las 5xx y FR tienen PMM, SFR, SYS y
TLV.

**Los nombres de las cabeceras cambian de generación en generación** para un
mismo módulo: `T0A3` en el FR5969 y `TA0` en el FR5994, `FRAM` frente a `FRCTL`
y `FRCTL_A`, `PMM_FRAM` frente a `PMM`, `CAP_TOUCH_IO_0` frente a `CAPTIO0`. El
generador de §5 normaliza esos nombres con una tabla.

### 2.4 Pines

* **Los puertos son de 8 bits, agrupados en pares de 16 bits** (PA = P1/P2,
  PB = P3/P4…), con un mismo bloque de registros. Los bits de un puerto son:
  * IN, OUT, DIR y REN;
  * DS, solo en 5xx;
  * SEL, en 2xx, o SEL0/SEL1, en FR;
  * IES, IE e IFG, en P1 y P2 en 2xx y 5xx, y en más puertos en las FR;
  * e IV, en 5xx y FR.

  Cada pin tiene hasta cuatro funciones, elegidas por SEL0/SEL1 (o por SEL y
  SEL2 en el G2553), más la analógica, que suele ir por SYSCFG2 o por el propio
  módulo. El `dio.h` del MSP432P4 ya modela exactamente esto.
* **PMAP** (F5529, puerto 4): las funciones digitales se asignan por registro y
  hay una clave de 0x02D52.
* **El JTAG comparte pines.** En las FR van en P1.4-P1.7 o en PJ.0-PJ.3, según
  el chip; en el G2553, en P1.4-P1.7 con TEST. TEST/SBWTCK y RST/NMI/SBWTDIO
  son pines propios. **La entrada en JTAG** (la secuencia de TEST) es lo que
  convierte esos pines GPIO en pines de JTAG, y eso es parte del modelo de pines
  (§7.3).
* Los pines analógicos (A0-A15, entradas de comparador, salidas del SAC y el
  TIA, segmentos de LCD) son `AnalogNet` como en el resto de `mcu-sim`. Los
  segmentos del LCD necesitan una pieza nueva de vidrio LCD (§10.3).

---

## 3. El núcleo reutilizable

### 3.1 Qué es del núcleo y qué es del chip

| Del núcleo (`src/m430/`) | Del chip |
| :--- | :--- |
| Registros R0-R15, SR y generador de constantes | Si es CPU, CPUX o CPUXV2: un `M430Caps` |
| Las 27 instrucciones de formato I, II y saltos, y las de 20 bits de CPUX (palabra de extensión, MOVA, CALLA, PUSHM/POPM, RxxM, xxxX) | Las erratas de CPU que tiene (`CPU_Bugs`) |
| Los siete modos de direccionamiento en 16 y 20 bits | — |
| Interrupciones: prioridad por la posición del vector, apilado de PC y SR, RETI, el NMI y el reset | Cuántos vectores, dónde está la tabla y quién pide cada uno |
| Bajo consumo: CPUOFF, OSCOFF, SCG0 y SCG1 en SR, y la salida por interrupción | **Qué** relojes apaga cada bit: eso es del sistema de reloj, no de la CPU |
| Ciclos por instrucción: **tres tablas** | Los estados de espera de la FRAM o la Flash: los pone el controlador de memoria |
| Control de la CPU por la depuración (parada, paso, TCLK) y la interfaz con el EEM | El tamaño del EEM (XS, S, M o L) |
| *Backend* MSP430 de GDB | El programador de su memoria no volátil |
| TAP JTAG y SBW (§7) | El JTAG ID y los pines |

Como en `NucleoRv`, el núcleo es un `SC_MODULE` fino (`NucleoM430`). Tiene un solo
*socket* TLM, porque MSP430 es von Neumann y el bus es uno; una entrada de
interrupciones (vector y nivel) con su reconocimiento; el reloj MCLK; el reset
(POR, BOR o PUC, que el SoC distingue); y la interfaz de depuración.

### 3.2 Las tres CPU

* **CPU (16 bits)** [UG2, cap. 3]:
  * 16 registros de 16 bits y 64 KB de espacio de direcciones;
  * 27 instrucciones básicas y 24 emuladas por el generador de constantes;
  * ciclos según el modo de origen y de destino [UG2, tablas 3-14 a 3-16].
* **CPUX** [CPUX; UG5 cap. 6 en su versión de 2xx grandes]:
  * registros de 20 bits y 1 MB;
  * palabra de extensión (con repetición y ZC);
  * instrucciones de direcciones (MOVA, ADDA, SUBA, CMPA, CALLA, RETA, PUSHM y
    POPM);
  * las 27 básicas en 20 bits (xxxX).

  En el modo de compatibilidad, una aplicación de CPU (16 bits) corre igual.
* **CPUXV2** [UG5 cap. 6; UGFR2 cap. 4; UGFR5 cap. 4]: la misma ISA que la CPUX.
  Cambian dos cosas:
  * **las tablas de ciclos**, que en general bajan un ciclo en los modos indexados
    y absolutos;
  * el apilado en interrupción: SR y PC[19:16] en una palabra y PC[15:0] en otra,
    igual que la CPUX, pero con otros tiempos.

  Es la CPU de las 5xx, 6xx y FR.

```cpp
struct M430Caps {
    enum class Arq { CPU, CPUX, CPUXV2 } arq;
    const TablaCiclos *ciclos;        // tres tablas constexpr, con su fuente
    uint32_t errata;                  // bits CPU21, CPU22, CPU40… (devices.csv)
    bool tclk_por_jtag;               // siempre; aquí por si un día no
};
```

### 3.3 Las erratas de CPU

Las erratas de CPU que trae `devices.csv` son pocas y cambian lo que ve el
programa. Por ejemplo:

* **CPU40**: PC corrompido en ciertos accesos con el modo indexado al final de la
  memoria;
* **CPU21 y CPU22**: el uso de `POPM` y `RRxX` con algunos registros.

Se modelan **encendidas por omisión**, como en el C3, se pueden apagar desde el
descriptor y avisan una vez. Las que solo afectan al consumo o al analógico
quedan documentadas y no se modelan.

### 3.4 Interrupciones y bajo consumo

Las interrupciones son vectorizadas con prioridad fija, que es la posición en la
tabla. El SoC da al núcleo, en cada ciclo, el vector pendiente de más prioridad
que no esté enmascarado. El enmascaramiento es de dos clases: GIE para las
enmascarables, y los NMI de sistema y de usuario (SYSSNIV y SYSUNIV en 5xx y FR,
y el NMI de 2xx). El núcleo entra a la interrupción con su tabla de ciclos y
**borra los bits de bajo consumo del SR** que apila, de modo que el `RETI`
vuelve al modo de bajo consumo salvo que la rutina haya cambiado el SR apilado
(`__bic_SR_register_on_exit`).

Los modos LPM0 a LPM4 son combinaciones de los bits del SR. **Qué** apaga cada
bit lo decide el sistema de reloj de cada familia (BCS+, UCS o CS), no la CPU.
Los modos **LPMx.5** (5xx y FR) apagan el regulador del núcleo, y al despertar
pasan por un BOR con LOCKLPM5. Son del PMM (§4.2).

### 3.5 Memoria, bus y tiempos

* Un solo espacio de 20 bits. Los accesos son de byte o de palabra, y una palabra
  en dirección impar pierde el bit 0. Los periféricos de 8 bits (0x10-0xFF en la
  2xx) solo admiten byte, y los de 16 bits, solo palabra. Se modela como en el
  `bus16.h` del P4.
* **Estados de espera**:
  * Flash de 2xx y 5xx: cero estados de espera, pero la CPU queda **parada
    mientras se borra o se escribe**, salvo si el código corre en RAM;
  * FRAM de FR5xx/FR6xx: NWAITS en FRCTL0, con **caché de dos vías** (4 líneas
    de 64 bits) que esconde los estados de espera en los aciertos [UGFR5 §7];
  * FRAM de FR2xx/FR4xx: NWAITS en FRCTL0 por encima de 8 MHz.

  Todo esto es del controlador de memoria, que es del chip y retrasa la
  transacción TLM.
* **MPY32** es un periférico (0x4C0) y no una instrucción. El compilador lo usa
  según `-mhwmult`. Es un bloque compartido por todas las FR que lo tienen.

### 3.6 El laboratorio y la verificación

Como `testrv`, el núcleo se valida sin chip, en un **`SocLabM430`** con:

* RAM y «Flash» planas de 1 MB;
* una UART de pega;
* un registro de salida, al estilo HTIF, para cerrar la prueba;
* y un controlador de interrupciones de laboratorio.

La batería se llama `test430` (y `asan430`). Tiene tres partes:

1. **Diferencial contra `msp430-elf-run`** (simulador de GDB, CPUX con
   MPY32). Hay programas generados que recorren todos los modos de direccionamiento
   y todas las instrucciones en 8, 16 y 20 bits. Se compara el estado de los
   registros y la memoria tras cada programa. El de GDB no cuenta ciclos con
   fidelidad, así que **solo** es oráculo de la semántica.
2. **Diferencial contra `mspdebug sim`**. Solo cubre la CPU de 16 bits, pero
   es un simulador independiente del anterior: son las dos fuentes de la ISA.
3. **Ciclos**: una tabla de pruebas, una por fila de las tablas de ciclos de
   [UG2], [UG5] y [UGFR5], con lo que mide un temporizador del laboratorio. Esto
   no tiene segunda fuente simulada. La segunda es la propia guía de cada familia,
   porque cada una publica su tabla, y las de [UG5] y [UGFR5] tienen que
   coincidir donde las dos dicen CPUXV2.

---

## 4. La capa de familia

### 4.1 `src/periph/ti/`: lo que comparten MSP430 y MSP432P4

Los módulos de TI del MSP432P4 ya son, a nivel de registro, los de MSP430: el
MSP432P4 los cuelga de un puente de 16 bits. En M0 se mueven a
`src/periph/ti/`, sin cambiar lo que hacen, y se parametrizan con su `Caps`. Por
ejemplo, `TimerACaps` lleva el número de CCR, y `EusciCaps`, si es A o B.

| Ya existe (P4) | Uso en MSP430 |
| :--- | :--- |
| `eusci.h` | Todas las FR |
| `timer_a.h` | Todos; Timer_B (§4.3) es un Timer_A con TBxCL0, longitud y grupos |
| `wdt_a.h` | 5xx y FR |
| `rtc_c.h` | FR5994, FR6989; RTC_B (FR5969) y RTC_A (F5529) son subconjuntos |
| `comp_e.h` | FR5xx/FR6xx; Comp_B (F5529) es casi igual |
| `ref_a.h` | FR5xx/FR6xx; REF (F5529) |
| `aes256.h`, `crc32.h` | FR5969, FR5994, FR6989 |
| `dio.h` | Puertos de 5xx y FR, con IV |
| `adc14.h` | Base para ADC12_A y ADC12_B (mismo diseño de memorias de conversión) |

**Cierre de este paso:** `testp4` y todas las demás suites con el mismo
invariante al picosegundo.

### 4.2 Lo propio de MSP430, por familia

| Bloque | 2xx | 5xx/6xx | FR2xx/FR4xx | FR5xx/FR6xx |
| :--- | :--- | :--- | :--- | :--- |
| Reloj | BCS+ (DCO con RSEL/DCO/MOD, VLO, LFXT1; calibración en INFO A) | UCS (FLL, DCO, REFO, XT1, XT2) | CS con FLL (DCO a 16 o 24 MHz, REFO, VLO, XT1) | CS (DCO por pasos, VLO, LFXT, HFXT, MODOSC) |
| Alimentación | — | PMM con VCORE (niveles 0-3), SVS/SVM | PMM con LPM3.5/LPM4.5, BOR | PMM con LPMx.5, LOCKLPM5 |
| Memoria no volátil | FLASH2: generador de tiempos, segmentos, LOCKA | FLASH 5xx: bancos, borrado de banco y masivo | FRAM con SYSCFG0 PFWP/DFWP y FRWPPW | FRAM con NWAITS, caché, **MPU** y **IPE** |
| Sistema | SFR (IE1/IFG1), calibración | SYS (SYSRSTIV, SYSSNIV, SYSUNIV, SYSBSLC, JMB), SFR, TLV | SYSCFG0-3, SYS, TLV | SYS, SFR, TLV, MPU, RAMCTL |
| Perro guardián | WDT+ | WDT_A | WDT_A | WDT_A |

* **El TLV** (5xx y FR): la tabla de calibración por chip (CRC, DCO, ADC, REF,
  temperatura y el ID). Se genera con su CRC y valores de la hoja de datos; si
  falta un dato, se marca `[de modelo]`. Ya existe la opción `--tlv`.
* **La calibración del G2553**: el DCO calibrado a 1, 8, 12 y 16 MHz y la del
  ADC10, en INFO A, con su *checksum*. El firmware de la LaunchPad la usa
  (`BCSCTL1 = CALBC1_1MHZ`). Si INFO A llega en blanco, el modelo avisa, como
  el chip real cuando se ha borrado.

### 4.3 Módulos nuevos que pide esta lista

Por número de chips que los usan:

* **Timer_B**, en ocho de los diez.
* **eCOMP**, SAC (L1 y L3), TIA, ICC y RTC contador, en las FR2xx/FR4xx.
* **ADC10** y Comp_A+ (G2553); ADC12_A (F5529); el ADC de las FR2xx.
* **USCI** A y B (G2553 y F5529). El A es UART, IrDA y SPI; el B, SPI e I2C.
  Es un módulo distinto del eUSCI, no un subconjunto.
* **DMA** de MSP430 (DMAX_3): canales, disparos por tabla del chip.
* **USB** del F5529: un `usb_dev_if` como el OTG del F303 o el MUSB del E4.
* **LCD_E** (FR4133) y **LCD_C** (FR6989), con su memoria de segmentos y el
  pin de cada segmento.
* **LEA** (FR5994), **ESI** (FR6989), **MFM** (FR2355) y **CapTIvate IO**: se
  **declaran** en la primera pasada (contestan, no guardan y avisan una vez) y
  se modelan después, si hay firmware de prácticas que los use.

---

## 5. Cómo entra un MSP430 nuevo

La regla es que **un chip nuevo de una familia ya modelada no escribe C++**.
Para eso hay tres capas, como en los RISC-V:

1. **`src/m430/`**: núcleo, EEM, TAP, SBW y *backend* de GDB.
2. **`src/soc/msp430/`**: lo común de los MSP430. Comprende:
   * el `Msp430Soc` genérico, que monta lo que dice su descriptor;
   * el sistema de reloj y el PMM de cada familia (`bcs_plus.h`, `ucs.h`,
     `cs_fll.h` y `cs_fr5.h`);
   * los controladores de memoria;
   * el BSL y su entrada;
   * y el programador para el `load` de GDB.
3. **Descriptor del chip** (`src/soc/msp430/desc/msp430fr5969.h`), **generado**
   por `herramientas/gen_msp430.py` a partir de dos ficheros de CCS. El
   descriptor es un `constexpr` con:
   * **de `devices.csv`**:
     * la CPU, el MPY, el SBW, el EEM y sus disparadores;
     * las erratas de CPU;
     * el mapa (RAM, RAM2, RAM diminuta, RAM de USB, RAM de LEA, BSL,
       información con A-D, memoria principal 1 y 2, firma y vectores);
   * **de la cabecera del chip**: cada módulo con su dirección base
     (`__MSP430_BASEADDRESS_*`), su instancia y su vector (`*_VECTOR`), con la
     tabla de normalización de nombres de §2.3;
   * **a mano, desde la hoja de datos**: el encapsulado, las funciones de cada
     pin (tabla «Signal Descriptions» y las de «Input/Output Diagrams»), los
     disparos del DMA y del ADC, y el contenido del TLV.

   El generador se comprueba contra la hoja de datos de los diez chips de este
   plan. Es la prueba de que acierta con los demás. Los pines se escriben con el
   mismo patrón que los `gen_pines.py` del F3 y del E4.

Un chip **de otra familia** (un F1xx, un F4xx o un i20xx) escribe además su
sistema de reloj y los módulos que no existan. La CPU, la depuración, GDB y el
BSL de 1xx/2xx/4xx le llegan hechos: la CPU 16 bits y la CPUX están desde M1, y
el EEM de los cuatro tamaños desde M2.

El registro en `mcu_if` es el de siempre: `REGISTRA_MCU("msp430fr5969", …)`. El
`<mcu tipo="msp430fr5969">` de la placa escoge el descriptor.

---

## 6. Descarga del firmware

| Vía | Qué | Herramienta real | Fase |
| :--- | :--- | :--- | :--- |
| D1 | `--firmware` (ELF o TI-TXT), escrito directamente en la Flash o la FRAM | — | M3 |
| D2 | `load` de GDB, por el stub (`--gdb-dap`) | `msp430-elf-gdb` | M2/M3 |
| D3 | JTAG o SBW por los pines, con la sonda de bancada o `mspdebug` | `mspdebug` (git) | M2/M3 |
| D4 | BSL por UART, I2C o USB | `python-msp430-tools`, BSL-Scripter | M9, M10 |
| D5 | eZ-FET emulada | `libmsp430`, UniFlash, CCS, `mspdebug tilib` | M11 |

El cargador ya lee ELF (`common/imagen_fw.h`). Para MSP430 se añaden dos cosas:

* `ELF_MSP430` (máquina 105), con los segmentos por encima de 0x10000 del modelo
  grande;
* el formato **TI-TXT** (`@dirección`, bytes en hexadecimal, `q`), que es lo que
  sacan CCS y BSL-Scripter, y lo que usa el `bsl_p4.h` del P4.

**Las particularidades de la memoria no volátil** las resuelve el programador
de cada familia:

* **Flash** (2xx y 5xx): el borrado es por segmento; INFO A está bloqueado por
  LOCKA, y la herramienta real tiene una opción para tocarlo.
* **FRAM** (FR): no hay borrado, pero sí protección. En FR2xx/FR4xx es por
  SYSCFG0; en FR5xx/FR6xx, por la MPU y la IPE, que el depurador real respeta o
  desactiva según la opción de borrado.

La persistencia entre simulaciones es la de `memoria_nv.h`.

---

## 7. Depuración por los pines (SLAU320AJ)

### 7.1 Lo que define [JTAG]

* **El TAP de MSP430**: la máquina de 16 estados de IEEE 1149.1, pero con una
  **IR de 8 bits** que se desplaza con el LSB primero en la IR y el MSB primero
  en los datos. Al desplazar la IR, por TDO sale **el JTAG ID** [JTAG §1.3]:
  0x89, 0x91, 0x98 o 0x99. Las instrucciones son:

  | Instrucción | Código | Qué hace |
  | :--- | :--- | :--- |
  | `IR_ADDR_16BIT` | 0x83 | Escribe en el MAB (20 bits en CPUX/CPUXV2) |
  | `IR_ADDR_CAPTURE` | 0x84 | Lee el MAB |
  | `IR_DATA_TO_ADDR` | 0x85 | Dato en la dirección del MAB |
  | `IR_DATA_16BIT` | 0x41 | Escribe o lee el MDB |
  | `IR_DATA_QUICK` | 0x43 | Accesos rápidos con autoincremento del PC |
  | `IR_DATA_PSA` | 0x44 | Firma (PSA) de un bloque de memoria |
  | `IR_SHIFT_OUT_PSA` | 0x46 | Lee la firma |
  | `IR_CNTRL_SIG_16BIT` | 0x13 | Registro de señales de control: R/W, HALT_JTAG, BYTE, TCE1, PUC, RELEASE_LBYTE… |
  | `IR_CNTRL_SIG_CAPTURE` | 0x14 | Lo lee |
  | `IR_CNTRL_SIG_RELEASE` | 0x15 | Devuelve la CPU a su reloj |
  | `IR_JMB_EXCHANGE` | 0x61 | El buzón JTAG (5xx y FR) |
  | `IR_BYPASS` | 0xFF | Bypass |

  Además están `IR_COREIP_ID`, `IR_DEVICE_ID` (5xx y FR), `IR_TEST_REG` /
  `IR_TEST_3V_REG` y `IR_PREPARE_BLOW` / `IR_EX_BLOW` (fusible).
* **La CPU se controla «a mano» desde el JTAG.** No es como un DAP de ARM o un DM
  de RISC-V, que piden la parada y esperan. Aquí la herramienta toma el reloj de
  la CPU: **TCLK es TDI** cuando la CPU está bajo control JTAG, y cada flanco
  avanza un ciclo. Para leer memoria, la herramienta pone la dirección en el MAB,
  el sentido en el registro de control y da flancos de TCLK [JTAG §2.3]. **Esto
  marca el modelo**: hay que modelar el núcleo ciclo a ciclo **cuando está bajo
  JTAG**, y por instrucciones cuando corre libre. El núcleo tiene los dos modos y
  el registro de control decide cuál.
* **Spy-Bi-Wire** [JTAG §1.5]: dos hilos, SBWTCK (en TEST) y SBWTDIO (en RST).
  Cada ciclo de TCK de JTAG son tres franjas de SBWTCK: **TMS**, **TDI** y
  **TDO**. En la franja de TDO, el chip gobierna SBWTDIO. La temporización tiene
  un tope: SBWTCK en bajo más de 7 µs saca al chip de SBW. El modelo tiene que
  tenerlo en cuenta, porque un SBWTCK quieto devuelve el pin RST a su función.
* **Entrada en JTAG** [JTAG §2.3.1]. La secuencia de TEST y RST elige entre SBW y
  JTAG de 4 hilos. En las 5xx y las FR hay además una **«magic pattern»**
  (`SyncJtag_AssertPor`, el patrón por el JMB) que **para la CPU antes de la
  primera instrucción del usuario**. Sin ella no se puede recuperar un chip cuyo
  programa desactiva el JTAG o duerme en LPMx.5.
* **Buzón JTAG (JMB)** (5xx y FR): dos registros de 16 bits en cada sentido, entre
  el JTAG y la CPU (`SYSJMBC`, `SYSJMBI0/1` y `SYSJMBO0/1`). Lo usan la «magic
  pattern», la contraseña de JTAG y los programas que hablan con el host.
* **Protección**:
  * en 1xx/2xx/4xx, el **fusible** (`IR_PREPARE_BLOW`/`IR_EX_BLOW`), que en el
    chip real necesita 6,5 V en TEST. En el modelo es un bit persistente en la
    memoria no volátil, que **nunca se fija por omisión** y que una opción
    restaura (como `--restore-rdp`);
  * en 5xx/6xx, la **firma de bloqueo** en 0x17FC-0x17FF (zona del BSL);
  * en FR, la **firma en 0xFF80**: 0x5555 bloquea; con contraseña, 0xAAAA y la
    longitud. Se desbloquea con el «User Code Erase» por JMB o con la contraseña
    [JTAG §2.3.4].
* **El EEM** [UG2 cap. 28; UG5; UGFR5; MSPDS]. Tiene:
  * disparadores sobre MAB y MDB, con comparación =, ≠, ≥ y ≤ y máscara;
  * disparadores de escritura en registro;
  * combinaciones;
  * secuenciador (M y L);
  * almacenamiento de estado (L);
  * y la **reacción «CPU Stop»**.

  Los tamaños son estos [UG2, tabla 28-1]:

  | | XS | S | M | L |
  | :--- | :---: | :---: | :---: | :---: |
  | Disparadores de bus | 2 (= y ≠) | 3 | 5 | 8 |
  | Disparadores de registro | 0 | 1 | 1 | 2 |
  | Combinaciones | 2 | 4 | 6 | 8 |
  | Secuenciador | — | — | sí | sí |
  | Almacenamiento de estado | — | — | — | sí |

  El EEM también tiene **control de relojes**: qué relojes de módulo se paran
  cuando la CPU está parada por el depurador. Es la pieza que hace que el WDT no
  muerda en una parada. **Los registros del EEM no están en ningún manual de
  usuario**: [JTAG] los usa por su dirección en las secuencias, y su definición
  completa está en los fuentes de la MSP Debug Stack (§1.3). Esa es la segunda
  fuente.

### 7.2 Dónde va cada cosa

* `src/m430/debug/tap_m430.h`: el TAP, la IR, los registros de datos y el
  registro de control, sobre el núcleo. Funciona en JTAG de 4 hilos.
* `src/m430/debug/sbw.h`: la máquina de franjas de SBW **delante** del TAP.
  Convierte SBWTCK y SBWTDIO en TCK, TMS, TDI y TDO del TAP. Lleva su tope de
  7 µs.
* `src/m430/debug/eem.h`: el EEM parametrizado por su tamaño, con la misma
  interfaz `debug_if` del núcleo que usan el FPB y los *triggers* de RISC-V.
* `src/m430/debug/jmb.h`: el buzón, como bloque de SYS y como registro de datos
  del TAP.
* `src/m430/debug/proteccion.h`: el fusible y las firmas.
* La entrada en JTAG y la selección de pines es del chip: el descriptor dice
  qué pines son TCK, TMS, TDI y TDO, y la placa dice si están conectados.

### 7.3 Cómo se llega desde fuera

| Camino | Qué | Coste | Fase |
| :--- | :--- | :--- | :--- |
| **Sonda de bancada propia** (`--gdb` en un MSP430) | Un stub de GDB que, en vez de hablar con el EEM, mueve los pines del chip con las funciones de [JTAG] (`GetDevice`, `ReadMem`, `WriteMem`, `SetPC`, `ReleaseDevice`, el borrado y la escritura de Flash, y el desbloqueo), en JTAG o en SBW | Cientos de flancos por acceso | M2 |
| **`mspdebug` de git** por *bit-bang* | El controlador de puerto paralelo o de GPIO (`jtdev`) y, si lo tiene, el de Bus Pirate. Se sirven por un pty, como la sonda `remote_bitbang`. Usa la `jtaglib` propia de `mspdebug`, que es una segunda implementación de [JTAG] | Igual | M2 (en principio, 0x89; las 5xx y FR según lo que soporte su `jtaglib`) |
| **eZ-FET emulada** | La sonda de la LaunchPad, del lado de la sonda (§10.1 y M11) | Órdenes HAL por USB CDC | M11 |

**La sonda de bancada se comprueba contra dos fuentes**: las funciones de [JTAG]
y el Replicator430 (§1.3). Así se cumple la regla de las dos fuentes sin
necesitar una herramienta de terceros. `mspdebug` se añade como tercera prueba
cuando el controlador de git cubra el chip.

### 7.4 Lo que no está

* **EnergyTrace** (la medida de corriente de la eZ-FET): igual que en el XDS110
  del P4, queda fuera.
* La programación del fusible con tensión real: solo como bit de modelo.
* El **almacenamiento de estado** del EEM L (F5529): se declara.

---

## 8. Depuración por stub (`--gdb-dap`)

### 8.1 El *backend* MSP430 de GDB

Va sobre `GdbRspBase` (rama `esp32rv`), como el de RISC-V, en
`src/m430/gdb/gdb_m430.h`.

* **Registros**: 16, de R0 (PC) a R15. Con `msp430-elf-gdb`, el tamaño en el
  paquete `g` depende de la arquitectura que anuncie el `qXfer:features`. El de
  MSP430X usa 4 bytes por registro, con 20 bits útiles. Se contrasta con dos
  fuentes: el `msp430-tdep.c` de GDB y lo que contesta el agente de GDB de TI
  (M11).
* **Memoria**: `m` y `M`, con los accesos de palabra y de byte en su sitio
  (periféricos de 8 y de 16 bits) y la Flash o la FRAM por el programador.
* **Puntos de ruptura**:
  * `Z0`: por software. MSP430 no tiene una instrucción de ruptura propia, así
    que la herramienta real usa un **disparador del EEM**, o escribe en la
    RAM o la FRAM una instrucción que el EEM reconoce al buscarla (0x4343,
    `CLR.B R3`, en la MSP Debug Stack; a confirmar con sus fuentes en M2);
  * `Z1`: por hardware, un disparador de bus;
  * `Z2`-`Z4`: puntos de vigilancia, disparadores de MDB y MAB.

  El número sale del tamaño del EEM.
* `vCont` con paso; Ctrl-C; `monitor reset`, `monitor reset halt` y
  `monitor erase`. Son las órdenes que entiende el agente de GDB de TI. Los
  nombres se comprueban en M11 contra el agente y contra `mspdebug gdb`.

### 8.2 Se comporta como la herramienta real

Con la parada del depurador, el modelo hace lo que hace la sonda real con
`libmsp430` en su configuración por omisión. Es la regla de `herramienta_if`
que se cumplió en todos los ARM y en el C3:

* **El control de relojes del EEM** se programa como lo programa la MSP Debug
  Stack. Con la CPU parada, se paran MCLK y SMCLK y, por tanto, los módulos que
  van con ellos, incluido el **WDT**. ACLK sigue o se para según el valor por
  omisión de la herramienta, que se toma de los fuentes de la MSP Debug Stack.
* **`monitor reset halt`**: con la «magic pattern» en 5xx y FR, la parada es antes
  de la primera instrucción; en 2xx, con el PUC por JTAG.
* **El `load`**: borra la memoria principal y deja la información salvo que se
  pida. Respeta la MPU y la IPE como la herramienta con sus opciones por omisión,
  y avisa si hay que desactivarlas.

Con `--stopped-core-stops-sim` el tiempo se congela entero, como en los demás
MCUs.

### 8.3 Opciones

| Opción | En un MSP430 |
| :--- | :--- |
| `--gdb-dap` | Stub interno, directamente sobre el control de la CPU y el EEM |
| `--gdb` | Stub con la sonda de bancada por los pines (JTAG o SBW, según la placa) |
| `--jtag=sbw` o `--jtag=4hilos` | Elige el protocolo de la sonda de bancada. Por omisión, el que conecte la placa |
| `--ezfet` | Expone la eZ-FET emulada (M11) |
| `--bsl FICHERO` o `--bsl ninguno` | Como en el P4 (§9) |

---

## 9. El cargador de arranque (BSL)

### 9.1 Tres protocolos

| | 1xx/2xx/4xx (G2553) | 5xx/6xx (F5529) | FR5xx/FR6xx | FR2xx/FR4xx |
| :--- | :--- | :--- | :--- | :--- |
| Fuente | [BSL] cap. 2 | [BSL] cap. 3 | [BSLFR] | **SLAU550AB** (falta) |
| Dónde | ROM 0C00-0FFF | Flash 1000-17FF (borrable) | ROM 1000-17FF | ROM (BSL1) y, en FR2311/FR2476, BSL2 alto |
| Interfaz | UART 9600 8E1 semidúplex, por pines fijos (TX P1.1, RX P1.5 en el G2553) | **USB HID** (VID 0x2047, PID 0x0200) en el F5529; UART en otros 5xx | UART 9600 8E1 (P2.0/P2.1 en las FR59xx/FR69xx) e I2C (0x48) | UART e I2C |
| Entrada | Secuencia de TEST y RST | PUR en alto en el reset, o vector de reset en blanco | Secuencia de TEST y RST; desde la aplicación | Secuencia; vector de reset en blanco; desde la aplicación |
| Trama | SYNC 0x80 → ACK 0x90; HDR, CMD, L1, L2, AL, AH, LL, LH, datos y *checksum* XOR | 0x80, longitud, núcleo (CMD + datos) y CRC-CCITT | La de 5xx | La de 5xx |
| Órdenes | RX data block, RX password, borrar segmento, borrado masivo, borrado de comprobación, cambiar baudios, cargar PC, TX data block, versión | RX data block (y *fast*), RX password, borrar segmento, bloquear o desbloquear INFO, borrado masivo, CRC, cargar PC, TX data block, versión, tamaño del búfer, cambiar baudios | Las de 5xx sin las de Flash | Las de 5xx |
| Seguridad | Contraseña: los 32 bytes de vectores; si falla, **borrado masivo**, salvo que la palabra de 0xFFDE (BSLSKEY) lo impida | Contraseña de vectores; si falla, borrado masivo | Igual; además, la firma en FF84 desactiva el BSL | Igual |

### 9.2 Cómo se modela: como el P4

`bsl_p4.h` ya resolvió el problema de licencia del BSL de TI con tres
contenidos, y aquí se repite:

* **B1, el BSL sustituto, por omisión**: un programa propio (AGPLv3) para cada
  protocolo, **que corre en la CPU simulada**. Está compilado con
  `msp430-elf-gcc`, se versiona con su huella (T-22) y se carga en la región del
  BSL. Así:
  * los tiempos son los de una CPU;
  * la entrada por pines es la del chip;
  * y la UART o el USB son los periféricos modelados.

  Para 5xx/6xx y FR, los fuentes de BSL a medida de TI (MSPBSL, §1.3) pueden ser
  la base si su licencia lo permite. Si no, se escribe desde [BSL] y [BSLFR].
* **B2, el de TI, desde fichero** (`--bsl FICHERO`): el volcado de la ROM o de la
  Flash de BSL de un chip real, si alguien lo tiene. No se versiona.
* **B3, sin BSL** (`--bsl ninguno`).

**La entrada.** El modelo del chip vigila TEST y RST. La secuencia de [BSL]
§1.3 y [BSLFR] fig. 2 es esta: RST en bajo, dos pulsos en TEST y RST en alto
con TEST en alto. Al ver la secuencia completa, el reset sale en el BSL. En el
F5529 se entra con PUR en alto durante el reset, y en las FR2xx/FR4xx, con el
vector de reset en blanco. Esa lógica es lo que en el chip hace el código de
arranque de la ROM. Va en `soc/msp430/arranque.h`, de alto nivel, como el
`arranque_fab` del P4.

**Desde el PC.** Los programas reales gobiernan TEST y RST con las líneas de
módem, RTS a TEST y DTR a RST, de un adaptador USB-serie. El puente serie de
`mcu-sim` ya lleva DTR y RTS por RFC 2217: es lo que usa el auto-reset de
esptool en E5. Hay una pieza nueva:

* **`bsl_adaptador.xml`**: un adaptador USB-serie con DTR y RTS cableados a RST
  y TEST, como la MSP-BSL «BSL Rocket» o un FTDI con dos hilos.

  En las LaunchPad, el BSL de UART de las FR va por los mismos pines que el
  *backchannel* de la eZ-FET, pero la eZ-FET no mueve TEST y RST para el BSL.
  La placa con BSL es, por eso, LaunchPad + adaptador, igual que en el
  laboratorio real.

**El USB BSL del F5529** necesita el USB del chip (M5) y una vía para que el
host vea un dispositivo HID. Se usa la que dé el puente USB
(`doc/analisis_puente_usb.md`). Si solo hay CDC, se añade el reenvío de HID por
TCP que entiende `python-msp430-tools` (`msp430.bsl5.hid`, con `hidapi`), o se
prueba solo con un cliente propio.

### 9.3 Cómo se prueba

* `python-msp430-tools`: `msp430.bsl.target` para el G2553, `msp430.bsl5.uart`
  para las FR y `msp430.bsl5.hid` para el F5529. Se programa, se verifica, se
  lee y se arranca blink.
* BSL-Scripter: los mismos guiones que en el laboratorio real, contra el puerto
  RFC 2217 del puente.
* Contraseña incorrecta → borrado masivo; firma de BSL desactivado → no
  contesta. Cada caso tiene su comprobación.

---

## 10. Placas, dos fuentes y erratas

### 10.1 Las diez LaunchPad

Todas son el mismo diseño de TI, con dos lados. A la izquierda está la sonda,
una eZ-FET o eZ-FET lite: un MSP430 propio con USB, que da el depurador, el
*backchannel* UART y la alimentación de 3,3 V. Entre la sonda y el objetivo hay
un **bloque de puentes**. A la derecha está el objetivo, con sus conectores de
BoosterPack. Así se modela, como la MSP-EXP432P401R con su J101. Los puentes
son elementos de la placa: quitar RXD o TXD corta el *backchannel*; quitar
SBWTDIO o SBWTCK corta la depuración; quitar 3V3 deja el chip sin alimentación.

| Placa | Conectores | LED | Pulsadores | *Backchannel* | Extra |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `msp_exp430g2et.xml` | 2 × 10 (20 pines) | LED1 P1.0, LED2 P1.6 | S1 RST, S2 P1.3 | P1.1/P1.2 (USCI_A0), con puentes SW/HW UART | Cristal de 32 kHz, opcional |
| `msp_exp430f5529lp.xml` | 40 pines | LED1 P1.0, LED2 P4.7 | S1 P2.1, S2 P1.1 | P4.4/P4.5 (USCI_A1) | **USB del objetivo**; XT1 y XT2 |
| `msp_exp430fr2311.xml` | 20 pines | LED1 P1.0, LED2 P2.0 | S1 P1.1 | P1.6/P1.7 (eUSCI_A0) | Fotodiodo en el TIA |
| `msp_exp430fr2355.xml` | 40 pines | LED1 P1.0, LED2 P6.6 | S1 P4.1, S2 P2.3 | P4.2/P4.3 (eUSCI_A1) | Sensor de luz en el SAC |
| `msp_exp430fr2433.xml` | 20 pines | LED1 P1.0, LED2 P1.1 | S1 P2.3, S2 P2.7 | P1.4/P1.5 (eUSCI_A0) | — |
| `msp_exp430fr4133.xml` | 20 pines | LED1 P4.0, LED2 P1.0 | S1 P1.2, S2 P2.6 | P1.0/P1.1 (eUSCI_A0, compartido con LED2) | **LCD de segmentos FH-1138P** |
| `lp_msp430fr2476.xml` | 40 pines | LED1 P1.0, LED2 RGB (P5.0 verde y otros dos) | S1, S2 | P1.4/P1.5 (eUSCI_A0) | — |
| `msp_exp430fr5969.xml` | 20 pines | LED1 P4.6, LED2 P1.0 | S1 P4.5, S2 P1.1 | P2.0/P2.1 (eUSCI_A0) | Supercondensador |
| `msp_exp430fr5994.xml` | 40 pines | LED1 P1.0, LED2 P1.1 | S1 P5.6, S2 P5.5 | P2.0/P2.1 (eUSCI_A0) | **microSD** por SPI; supercondensador |
| `msp_exp430fr6989.xml` | 40 pines | LED1 P1.0, LED2 P9.7 | S1 P1.1, S2 P1.2 | P3.4/P3.5 (eUSCI_A1) | **LCD de segmentos FH-1138P** |

**Estos pines son de una primera lectura de las guías y tienen que salir del
esquemático antes de cerrar cada placa**, como se hizo con la MB1035 y la
MSP-EXP432P401R. Hay que contrastar las resistencias de los LED, los
pulsadores, el condensador de RST y las orientaciones de los puentes de UART.
Lo que no salga del esquemático se marca `[de modelo]`.

Los dibujos se hacen con `placas/dibuja_ti.py`, que ya dibuja las LaunchPad de
MSP432, con la plantilla de 20 o 40 pines. Las BoosterPack ya existentes (la
`boostxl_edumkii`) valen en las LaunchPad de 40 pines: así se tienen
combinaciones como `msp_exp430fr5994_edumkii.xml`.

### 10.2 Dos fuentes de cada dato

| Papel | Fuente 1 | Fuente 2 |
| :--- | :--- | :--- |
| ISA y ciclos | [UG2] cap. 3, [CPUX], [UG5] cap. 6, [UGFR5] cap. 4 | `msp430-elf-run` y `mspdebug sim` (semántica); las tablas de cada familia entre sí (ciclos) |
| Mapa, CPU, MPY, EEM y erratas de CPU | Hojas de datos y erratas | `devices.csv` de CCS |
| Módulos, direcciones base y vectores | Hojas de datos | Cabeceras de CCS (`msp430*.h`) |
| Registros y valores de reset | Guía de cada familia | Cabeceras de CCS; `targetdb` de CCS (§1.3); DriverLib |
| Pines y funciones | Hoja de datos («Signal Descriptions» y diagramas de E/S) | Los `.h` de pines de DriverLib y las tablas de pines de Energia, cuando las haya; si no, dos tablas de la misma hoja de datos |
| JTAG, SBW y EEM | [JTAG], [UG2] cap. 28 | Replicator430; MSP Debug Stack |
| BSL | [BSL], [BSLFR], SLAU550AB | `python-msp430-tools`, BSL-Scripter |
| Placas | Esquemático de cada LaunchPad | Guía de usuario de la LaunchPad |

La regla de siempre: un registro tiene dos fuentes, o se declara (contesta, no
guarda y avisa una vez), y nunca se deja seguir al firmware sin decirlo.

### 10.3 Piezas nuevas

* **Vidrio LCD de segmentos** (FH-1138P de las LaunchPad FR4133 y FR6989). Tiene
  `COM0-COM3`, sus segmentos y un dibujo SVG que enciende cada segmento con su
  valor RMS. Sirve para LCD_E y para LCD_C.
* **Adaptador BSL** (§9.2).
* **microSD** por SPI (FR5994): si `parts/` no tiene ya una tarjeta SD por SPI,
  se declara la ranura y se modela después.

### 10.4 Erratas

Cada fase de chip recorre su hoja de erratas y clasifica cada entrada:

* **se modela**: cambia lo que ve el programa. Va encendida, se puede apagar y
  avisa;
* **se documenta**: consumo, analógico fino o encapsulado;
* **no aplica**: revisiones de silicio que no se simulan. Se fija una revisión
  de silicio por chip en el descriptor.

El resultado va en un apartado de `doc/todo.md` por chip, como se hizo con el
F303 y el C3.

---

## 11. Plan por fases

Es el método de siempre. Las fases son pequeñas, y cada una tiene sus entregas
y su criterio de cierre. Cada banco tiene su invariante y su `asan*`:

* `test430` para el laboratorio;
* `testg2` para el G2553;
* `testf5` para el F5529;
* `testfr2` para las cinco FR2xx/FR4xx;
* `testfr5` para las tres FR5xx/FR6xx.

**Ninguna fase mueve el invariante de las diez suites que ya hay.** Los
firmwares de prueba se versionan con su huella (T-22). Compilarlos pide
`msp430-elf-gcc`; ejecutar las suites, no.

### Vista de conjunto

| Fase | Qué | Banco | Depende de | Tamaño |
| :--- | :--- | :--- | :--- | :--- |
| **M0** | Integrar `esp32rv`; `periph/ti/`; generador de descriptores | Las diez | — | Medio |
| **M1** | Núcleo CPU/CPUX/CPUXV2 + laboratorio | `test430` | M0 | Grande |
| **M2** | TAP, SBW, JMB, EEM, protección, *backend* de GDB, `--gdb-dap` y `--gdb`, en el laboratorio | `test430` | M1 | Grande |
| **M3** | G2553 + MSP-EXP430G2ET | `testg2` | M2 | Medio |
| **M4** | F5529 sin USB + MSP-EXP430F5529LP | `testf5` | M2 | Grande |
| **M5** | F5529: USB | `testf5` | M4 | Medio |
| **M6** | FR2xx/FR4xx común: FR2433 + su LaunchPad | `testfr2` | M2 | Grande |
| **M7** | FR2311, FR2355, FR4133 y FR2476, con sus LaunchPad | `testfr2` | M6 | Grande, divisible por chip |
| **M8** | FR5xx/FR6xx: FR5969, después FR5994 y FR6989, con sus LaunchPad | `testfr5` | M2 | Grande, divisible |
| **M9** | BSL de 2xx (G2553), de 5xx (F5529, USB HID) y de FR5xx/FR6xx | `testg2`, `testf5`, `testfr5` + Python | M3, M5, M8 | Medio |
| **M10** | BSL de FR2xx/FR4xx | `testfr2` + Python | M7, **SLAU550AB** | Pequeño |
| **M11** | eZ-FET emulada: `libmsp430`, agente de GDB, UniFlash, CCS | Python | M2, M3 | Grande, **la de más riesgo** |
| **M12** | `mcu-sim-gui`: placas, dibujos y LCD | — | M3 | Pequeño |

```
M0 ──► M1 ──► M2 ──┬──► M3 ───────────────┬──► M9
                   ├──► M4 ──► M5 ────────┤
                   ├──► M6 ──► M7 ──► M10 │
                   ├──► M8 ───────────────┘
                   └──► M11 (con M3)
M12 en paralelo desde M3
```

### M0 · El terreno

**Objetivo:** que el resto del plan no toque nada de ARM ni de RISC-V.

**Entregas:**
1. **Integrar `esp32rv` en `main`** (o, si todavía no se ha fusionado, rebasar
   `msp430` sobre `esp32rv`). Trae `GdbRspBase`, `herramienta_if`,
   `congela.h`, `nodo_depuracion` y las sondas con JTAG.
2. **`src/periph/ti/`**: mover los módulos de TI del P4 sin cambiarlos. Primero
   se mueven y después se parametrizan, en dos commits.
3. **El bus de 16 bits como componente común** (`bus16.h`), con los periféricos
   de 8 bits y la regla de los accesos de palabra impares.
4. **`herramientas/gen_msp430.py`**: genera el descriptor de los diez chips a
   partir de `devices.csv` y las cabeceras; tiene la tabla de normalización de
   nombres. Su salida se versiona; sus entradas (CCS) no.
5. `ELF_MSP430` y TI-TXT en el cargador.

**Cierre:** las diez suites con el mismo invariante al picosegundo; el
generador saca los diez descriptores, y su mapa de memoria coincide con la
tabla de §2.2.

### M1 · El núcleo y el laboratorio

**Entregas:**
1. `src/m430/cpu/`:
   * `m430_caps.h`;
   * `m430_estado.h`;
   * `m430_decod.h`, con la palabra de extensión;
   * `m430_exec.h`, con CPU, CPUX y CPUXV2;
   * `m430_ciclos.h`, con las tres tablas y su fuente fila a fila;
   * `m430_irq.h`, con la interfaz de vector y nivel y los NMI;
   * `m430_errata.h`.
2. `NucleoM430` (`SC_MODULE`): quantum LT; bajo consumo con el reloj parado;
   **dos modos de avance**, por instrucciones o por ciclos de TCLK, este último
   preparado para M2.
3. `src/m430/lab/`: `SocLabM430` y `top/sc_main_430.cpp` → `make test430`,
   `asan430`.
4. `verif/m430/`:
   * el generador de programas diferenciales;
   * los guiones contra `msp430-elf-run` y `mspdebug sim`;
   * las pruebas de ciclos.

**Cierre:**
* la batería diferencial sin una diferencia en CPU, CPUX y CPUXV2;
* las tablas de ciclos fila a fila;
* el `printf` de newlib por la UART de pega, compilado con `-mlarge` y sin él.

**Tamaño:** grande. Es el corazón de todo lo demás.

### M2 · La depuración, en el laboratorio

**Entregas:**
1. El TAP con su IR de 8 bits, los cuatro JTAG ID y el registro de control, en
   `tap_m430.h`. Incluye el avance por TCLK.
2. SBW, con su tope de tiempo, y la entrada en JTAG de 4 hilos y de 2 hilos.
3. El JMB y la «magic pattern».
4. El EEM con los tamaños XS, S, M y L, la reacción de parada y el control de
   relojes.
5. El fusible y las dos clases de firma.
6. `gdb_m430.h` sobre `GdbRspBase`; `--gdb-dap`; `herramienta_if` para MSP430.
7. **La sonda de bancada** (`--gdb`) con las funciones de [JTAG], en JTAG y en
   SBW.
8. `mspdebug` compilado de git en el CI, como OpenOCD. Se prueba con el
   controlador de *bit-bang* que tenga.

**Cierre:**
* el guion de depuración de siempre por `--gdb-dap` y por `--gdb`, en JTAG y en
  SBW: `load`, ruptura en `main`, ruptura por hardware en una función,
  vigilancia, paso, Ctrl-C y `monitor reset halt`;
* el WDT no muerde con la CPU parada, y sí si se apaga el control de relojes;
* el chip con la firma de bloqueo no deja entrar, y se recupera con el borrado
  por JMB;
* `mspdebug` (git) lee el JTAG ID y la memoria del laboratorio en CPU de 16 bits.

**Tamaño:** grande. La parte delicada es el avance por TCLK: es lo que hace que
la sonda de bancada funcione de verdad.

### M3 · G2553 y MSP-EXP430G2ET

**Entregas:**
* el SoC con:
  * el BCS+ (DCO, VLO y LFXT1, con la calibración en INFO A);
  * FLASH2 con su generador de tiempos y LOCKA;
  * WDT+, 2 × Timer_A3, USCI_A0/B0, ADC10 y Comp_A+;
  * P1-P3 con SEL/SEL2;
* `msp_exp430g2et.xml` y su SVG, contrastados con el esquemático;
* los ejemplos de TI del G2553 (blink, UART por USCI y por el Timer_A, ADC10
  y temperatura) y la demo de fábrica de la placa;
* el `programador_nv_if` de la Flash de 2xx, para el `load`;
* erratas de SLAZ440K clasificadas.

**Cierre:** la demo de fábrica (temperatura por el *backchannel* y pulsador
P1.3); `--gdb-dap` y `--gdb` por SBW sobre la placa; las comprobaciones de
`testg2` y su invariante.

**Tamaño:** medio. Es el chip de prácticas más extendido y el primero que entra.

### M4 · F5529 sin USB

**Entregas:**
* el SoC con:
  * UCS, PMM con VCORE y SVS, y el controlador de Flash de 5xx con sus bancos;
  * SYS, SFR, TLV y PMAP;
  * WDT_A, TA5, 2 × TA3, TB7, 2 × USCI_A y 2 × USCI_B;
  * ADC12_A, Comp_B, REF, MPY32, DMA, RTC_A y CRC16;
* `msp_exp430f5529lp.xml` y su SVG;
* el `load` sobre la Flash de 5xx, el EEM L y la protección por firma;
* erratas de SLAZ314AD clasificadas.

**Cierre:** los ejemplos de DriverLib del F5529 (reloj a 25 MHz con VCORE 3,
UART, ADC, DMA); por GDB, una ruptura por encima de 0x10000 (modelo grande).

### M5 · F5529: USB

**Entregas:** el módulo USB (endpoints, búferes en la RAM de USB, PLL y
alimentación de USB) detrás de `usb_dev_if`; la pila USB de TI con CDC;
el puente USB enumera el F5529 como enumera el F303.

**Cierre:** el ejemplo CDC de la MSP430 USB Developers Package se ve como puerto
serie en el PC; el *backchannel* sigue funcionando a la vez.

### M6 · FR2xx/FR4xx: lo común, con el FR2433

**Entregas:**
* lo común de la familia:
  * CS con FLL;
  * PMM con LPM3.5 y LPM4.5;
  * FRAM con SYSCFG0 y SYSCFG1-3;
  * RTC contador, CRC16 y Backup RAM;
  * el ADC de 10 bits;
* eUSCI y Timer_A desde `periph/ti/`;
* FR2433 y `msp_exp430fr2433.xml`;
* erratas de SLAZ664S.

**Cierre:** la demo de fábrica del FR2433; LPM3.5 con el RTC y salida por
BOR con LOCKLPM5; la protección de escritura de la FRAM.

### M7 · FR2311, FR2355, FR4133 y FR2476

Un chip por subfase, en este orden, porque así cada uno añade poco:

1. **FR4133**: LCD_E y el vidrio FH-1138P (la demo de fábrica pinta en el LCD).
2. **FR2311**: Timer_B3, eCOMP con su DAC, SAC-L1 y TIA.
3. **FR2355**: SAC-L3 × 4, 2 × eCOMP, ICC, el ADC de 12 bits, MPY32, la ROM de
   DriverLib y FFT (se declara; si hay firmware que la llame, se carga el
   binario de TI desde fichero, como B2), y MFM declarado.
4. **FR2476**: FRAM por encima de 0x10000, 4 × TA3 + TB7 y ADC de 12 bits.

**Cierre:** la demo de fábrica de cada LaunchPad y las erratas de cada chip.

### M8 · FR5xx/FR6xx: FR5969, FR5994 y FR6989

1. **FR5969**: lo común de la familia (CS, PMM con LPMx.5, FRAM con NWAITS y
   caché, MPU, IPE, REF_A, ADC12_B, Comp_E, RTC_B, AES256, DMA y MPY32) y
   `msp_exp430fr5969.xml`.
2. **FR5994**: 4 × eUSCI_A/B, CRC32, RTC_C y RAMCTL; LEA declarado; la
   microSD; FRAM hasta 0x43FFF.
3. **FR6989**: LCD_C con el FH-1138P y ESI declarado.

**Cierre:** las demos de fábrica (la del FR6989 usa el LCD); la MPU con una
violación que da su NMI; la IPE que no deja leer por JTAG ni por el depurador;
un `load` en una FRAM con la MPU puesta.

### M9 · BSL de 2xx, 5xx y FR5xx/FR6xx

**Entregas:**
* el BSL sustituto de cada protocolo (B1), compilado y con su huella;
* B2 y B3;
* la entrada por TEST y RST, por PUR y por vector en blanco;
* `bsl_adaptador.xml`;
* el USB HID del F5529.

**Cierre:** `python-msp430-tools` programa, verifica y arranca blink en el
G2553 (`bsl.target`), el FR5969 (`bsl5.uart`) y el F5529 (`bsl5.hid`); con
contraseña incorrecta hay borrado masivo; BSL-Scripter con un guion de prácticas.

### M10 · BSL de FR2xx/FR4xx

**Bloqueada hasta que esté SLAU550AB** (§1.2). Entregas: BSL1 y BSL2 donde los
haya, la entrada por vector en blanco y la interfaz I2C. Cierre: el de M9 en el
FR2433 y el FR2476.

### M11 · La eZ-FET emulada

**Objetivo:** que las herramientas de TI vean la LaunchPad como la ven en el
laboratorio. Son:

* la MSP Debug Stack (`libmsp430`);
* el agente de GDB de TI;
* UniFlash y CCS;
* y `mspdebug tilib`.

**Entregas:**
1. Del lado del PC, un **puerto CDC** (el de la eZ-FET real) servido por el
   puente USB o por un pty, con el protocolo de la MSP-FET y eZ-FET. Son
   órdenes HAL: `SyncJtag`, `ReadMemWords`, `WriteMemWords`, `SetBreakpoint`,
   `EemDataExchange`…, con su número de versión.
2. Del lado del chip, la ejecución de esas órdenes por los pines **con las
   funciones de la sonda de bancada de M2**: la eZ-FET emulada es la sonda de
   bancada con otro protocolo de entrada.
3. El *backchannel* UART de la eZ-FET, que ya existe desde M3 por RFC 2217, en
   el mismo dispositivo USB compuesto, como en la sonda real.

**Cierre:** `mspdebug tilib` y el agente de GDB de TI con `msp430-elf-gdb`
hacen `load`, ruptura y paso sobre el G2553 y el FR5969.

**El riesgo** es el de la versión del *firmware*: `libmsp430` compara la
versión de la sonda con la suya y quiere actualizarla si no coinciden. La sonda
emulada tiene que contestar la versión exacta de la `libmsp430` con la que se
prueba, y la actualización se rechaza limpiamente. Se fija una versión de MSPDS
en el CI.

**Tamaño:** grande. Sin los fuentes de la MSP Debug Stack (§1.3) no se puede
hacer.

### M12 · `mcu-sim-gui`

Las diez placas con sus dibujos; el vidrio LCD como elemento animado; los
puentes de la eZ-FET como interruptores de la placa; y el estado de la CPU en
LPMx, que no es lo mismo que una CPU parada por el depurador.

---

## 12. Riesgos y preguntas abiertas

1. **El avance por TCLK (M2).** El JTAG de MSP430 no pide la parada a la CPU, le
   quita el reloj. Si el núcleo solo supiera avanzar por instrucciones, ninguna
   herramienta real podría leer la memoria con la CPU «en marcha a mano». Hay que
   diseñarlo así desde M1.
2. **El EEM no tiene manual de registros.** La segunda fuente son los fuentes
   de la MSP Debug Stack. Si no están, el EEM se modela por lo que hacen las
   secuencias de [JTAG], y la eZ-FET (M11) no se puede hacer.
3. **El hueco del BSL de las FR2xx/FR4xx** (§1.2): M10 queda bloqueada.
4. **Las tablas de ciclos.** Cada guía publica la suya, pero hay casos límite
   (las instrucciones de 20 bits con repetición, la entrada a una interrupción
   con la CPU en LPM o los estados de espera de la FRAM con caché) que no están
   claros. Lo que no tenga dos fuentes va a la sección D de `todo.md`.
5. **El código de arranque de la ROM.** En las FR, la ROM de arranque
   comprueba firmas, entra en el BSL o no, y aplica la protección. Se modela de
   alto nivel, como el `arranque_fab` del P4. Cualquier cosa que haga y no esté
   documentada no está.
6. **`mspdebug` de git.** Está por ver qué controlador de *bit-bang* se puede
   servir por un pty, y qué generaciones de JTAG soporta su `jtaglib`. Si no
   cubre las 5xx y las FR, se queda como prueba del G2553, y las FR se prueban
   solo con la sonda de bancada y con la eZ-FET.
7. **Coste de simulación.** Por los pines y en SBW, cada acceso son cientos de
   flancos. La sonda `remote_bitbang` ya lo resolvió con el ritmo real
   (`RitmoReal`) y la congelación; aquí se mide en M2.
8. **Lo que no está y hay que decirlo desde el primer día:**
   * EnergyTrace;
   * el analógico fino: el ruido del ADC, el *offset* de los SAC y la ganancia
     del TIA, más allá de lo que dé la hoja de datos;
   * LEA, ESI, MFM y CapTIvate: declarados;
   * el almacenamiento de estado del EEM L;
   * y el fusible con alta tensión real.

---

## 13. Fuentes

* Repositorio `mcu-sim`, ramas `msp430` (`e7a34c9`) y `esp32rv` (`9dba504`):
  * `soc/mcu_if.h`, `common/gdb_rsp.h`, `common/congela.h`,
    `soc/herramientas.h` y `common/imagen_fw.h`;
  * `periph/msp432p4/*` (en especial `bus16.h`, `eusci.h`, `timer_a.h` y
    `dio.h`), `soc/msp432p4/bsl_p4.h` y `placas/msp_exp432p401r.xml`;
  * `placas/dibuja_ti.py`, `src/verif/invariantes.txt`, `doc/analisis_msp432.md`,
    `doc/analisis_esp32_riscv.md` y `doc/analisis_xds110.md`.
* `doc/refs/msp430/` (tabla de referencias al principio).
* CCS 11.2, carpeta `ccs_base/msp430`: `include/msp430*.h`, `include_gcc/devices.csv`
  (v1.213, 15-10-2021) y los `.ld` y `.cmd` de cada chip.
* `mspdebug` 0.22 (Debian), `mspdebug --help`: lista de controladores.
  Consultado el 09-11-2026.
* Texas Instruments, página de SLAU550 (revisión AB, septiembre de 2022).
  Consultada el 09-11-2026.
* Pendientes de incorporar a `doc/refs/msp430/`:
  * **SLAU550AB** (y SLAU610, si se puede descargar);
  * los fuentes de la MSP Debug Stack;
  * Replicator430;
  * MSPBSL;
  * el `targetdb` de CCS;
  * SLAA450 y SLAA721.
