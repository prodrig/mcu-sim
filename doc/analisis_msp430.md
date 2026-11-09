# MSP430 en `mcu-sim`

**Análisis y plan por fases** · rama `msp430` · 09-11-2026 (segunda versión)

Esta segunda versión incorpora la documentación que faltaba en la primera y las
fuentes de TI que la primera pedía como «conveniente»:

* SLAU550AB, que cierra el único hueco: el BSL de las FR2xx/FR4xx;
* la **MSP Debug Stack** completa, en código abierto: firmware de la eZ-FET,
  biblioteca `libmsp430` y su base de datos de dispositivos;
* el **Replicator430** de SLAU320;
* los fuentes del **BSL a medida** (MSPBSL);
* SLAA754, programación por Spy-Bi-Wire desde un MSP432P401R;
* y el `targetdb` de CCS.

Con eso cambian tres cosas del plan:

* la sonda por los pines deja de escribirse desde cero;
* el BSL del F5529 puede ser el de TI;
* y la eZ-FET emulada pasa de «arriesgada» a «con dos caminos conocidos».

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
   * y la ROM de todas las FRAM, según SLAU550G y SLAU550AB.

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
| [BSLFR] | MSP430FR57xx, FR58xx, FR59xx, FR68xx and FR69xx Bootloader (BSL), SLAU550G (2017) |
| [BSLFRAM] | MSP430 FRAM Devices Bootloader (BSL), **SLAU550AB** (septiembre de 2022): todas las FRAM, FR2xx/FR4xx incluidas |
| — | SLAU610G (2017): solo una página que remite a SLAU550. No aporta nada más |
| [DBGUG] | MSP Debuggers User's Guide, SLAU647O (2020) |
| [MSPDS-DG] | MSPDebugStack Developer's Guide, SLAU656D (2021) |
| [MSPDS] | MSP Debug Stack Open Source Package 3.15.1.1 (`MSPDebugStack_OS_Package_3_15_1_1.zip`). Es **BSD-3** en todo el código fuente (§1.3) |
| [MSPDS-DEV] | MSP430 DLL Developer Package 3.15.1.1: `libmsp430_64.so`, `MSP430.dll`, cabeceras y guía de la API |
| [MSPBSL] | MSP430 BSL 1.02.00.01 (MSPBSL_CUSTOMBSL430): fuentes **BSD-3** del BSL de 5xx/6xx e imágenes publicadas |
| [REP] | Replicator430 de SLAU320 (`REP430F Replicator/TI-SLAU320/slau320aj.zip`), versión 1.22, **BSD-3** |
| [REP430F] | Elprotronic REP430F, manual de usuario (programador comercial; solo informativo) |
| [SBW-SL] | MSP430 Spy-Bi-Wire With SimpleLink MCUs, SLAA754 (2017) |
| [TDB] | `ccs_base/common/targetdb` de CCS 11.2: 603 descripciones de dispositivos MSP430 y 491 de módulos |
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

* **La documentación está completa.** Cubre el núcleo, los diez MCUs con sus
  erratas, el JTAG y el SBW, el BSL de las tres generaciones y las LaunchPad.
  * El hueco de la primera versión, el BSL de las FR2xx/FR4xx, lo cierra
    SLAU550AB. SLAU610G es solo una página que remite a ella.
  * Lo único que falta es **el código de SLAA754** (`slaa754.zip`). No bloquea
    nada: es una prueba más (§1.4).
* **Los tres núcleos son uno con rasgos.** La CPU de 16 bits del G2553, la CPUX
  (ningún MCU de la lista la usa, pero sí los F2xx grandes y los F4xx) y la
  CPUXV2 de los otros nueve comparten registros, modos de direccionamiento y
  codificación. Lo que cambia es esto:
  * si existen las instrucciones de 20 bits y la palabra de extensión;
  * **la tabla de ciclos**, que CPUX y CPUXV2 no comparten;
  * el comportamiento de algunos casos límite, que está en las erratas de CPU.

  El núcleo va en `src/m430/` con un `M430Caps` (§3). Se valida contra **dos
  simuladores independientes**:
  * el simulador de GDB (`msp430-elf-run`), con CPUX;
  * y el de `mspdebug` (`sim`), con la CPU de 16 bits.
* **La mitad de los periféricos ya existe.** El MSP432P4 tiene los módulos de
  MSP430 detrás de un puente de 16 bits (`periph/msp432p4/bus16.h`): eUSCI,
  Timer_A, WDT_A, RTC_C, Comp_E, REF_A, AES256, CRC32 y los puertos. Esos
  módulos pasan a `src/periph/ti/` y se comparten (M0). El MSP432 no puede mover
  su invariante al hacerlo.
* **Un MCU nuevo es, sobre todo, datos, y ahora hay cuatro fuentes legibles por
  programa** (§5):
  1. `devices.csv` de CCS (693 dispositivos): CPU, erratas de CPU, MPY, SBW,
     EEM y mapa de memoria;
  2. las cabeceras de CCS: dirección base de cada módulo y vectores;
  3. el `targetdb` de CCS: **cada registro con su dirección absoluta, sus campos
     y sus valores con nombre**, aunque no sus valores de reset;
  4. la base de datos de dispositivos de la MSP Debug Stack (`devicedb.h`, un
     ZIP de XML dentro del código BSD), que da:
     * el identificador que lee la herramienta (`0x5325` para el G2xx3);
     * el tipo de EEM;
     * el control de relojes;
     * y **qué módulos puede parar el depurador** (`eemTimers`).

  El generador `gen_msp430.py` cruza las cuatro. A mano solo quedan los pines.
* **Depuración por los pines (§7).** Se modelan:
  * el TAP de MSP430 con su IR de 8 bits y las cuatro generaciones de JTAG ID;
  * el Spy-Bi-Wire, la «magic pattern» y el buzón JTAG;
  * las protecciones;
  * y el **EEM**, cuyos registros ya tienen fuente: `EEM_defs.h` de la MSP
    Debug Stack (GENCTRL 0x82, EEMVER 0x86, GENCLKCTRL 0x88, MODCLKCTRL0 0x8A,
    los bloques de disparo de 8 bytes…).

  **La sonda de bancada ya no se escribe desde cero.** Es un *port* de las
  **94 macros HAL** de la MSP Debug Stack, que son el código que ejecuta la
  eZ-FET de verdad y es BSD-3. Se contrasta con el **Replicator430**, la otra
  implementación de TI, también BSD-3. Hay dos pruebas con firmware de verdad
  corriendo en otro MCU simulado:
  * **SLAA754**: una MSP-EXP432P401R, que `mcu-sim` ya simula, programa un
    G2553 y un FR2311 por SBW con P6.0 y P6.1;
  * **el Replicator430 compilado para un MSP430 anfitrión**.
* **Depuración por stub (§8).** `--gdb-dap` en un MSP430 es el stub de GDB
  pegado al control de la CPU y al EEM. Se comporta como `libmsp430`, y ahora
  con el valor exacto: GENCLKCTRL = 0x26, que para ACLK y SMCLK con la CPU
  parada, más el bit 0 en los chips de control «estándar». Los módulos que se
  paran son los `eemTimers` de cada chip. Los puntos de ruptura por software
  usan `0x4343` (`SoftwareBreakpoints430.cpp`).
* **BSL (§9).** Hay tres protocolos y cuatro variantes de memoria y entrada:
  * **El F5529 puede llevar el BSL USB de TI.** Sus fuentes están en MSPBSL
    (`IAR_v6_MSP430F552x_USB`, BSD-3). Las imágenes publicadas (00.07.88.38 y
    00.08.88.39) son de la licencia «TI Text File», así que se cargan desde
    fichero.
  * Para el G2553 y las FRAM, el BSL es ROM sin fuentes. Se escribe un sustituto
    con el **intérprete de órdenes y las interfaces de MSPBSL** como base: hay
    `eUSCI_UART_I2C`, `USCIA_UART`, `Timer_A_UART` y `USB`.
  * SLAU550AB da los datos de cada FR2xx/FR4xx:
    * dónde está el BSL (1 KB en 0x1000-0x13FF en el FR4133; 2 KB + 1 KB en
      0x1000-0x17FF y 0xFFC00-0xFFFFF en FR23xx-FR26xx);
    * la detección de chip en blanco;
    * el plazo de 10 s, tras el que pasa a LPM4;
    * la configuración por TLV y por el usuario en las FR2355 y FR2476;
    * la versión de BSL de cada chip.
* **La eZ-FET emulada (M11) tiene dos caminos, los dos con fuentes:**
  1. **Alto nivel**: un C++ que habla el protocolo V3 de la sonda y ejecuta las
     macros HAL con la sonda de bancada.
  2. **Bajo nivel**: un **MSP430F5528 simulado**, que es de la familia del F5529
     (misma hoja de datos), con las imágenes de firmware de la eZ-FET que trae
     el paquete. Es lo que se hizo con el XDS110 en el P4.

  En los dos hay que contestar la versión y el CRC que espera `libmsp430`, y
  esos datos están en las imágenes. En Linux, `libmsp430` busca la sonda en
  `/sys/class/tty/ttyACM*` por VID 0x2047 y PID 0x0013. Así que hace falta
  **el servidor USB/IP** del puente USB (U4 de `doc/analisis_puente_usb.md`) o
  una `libmsp430` compilada de los fuentes con la enumeración cambiada.
* **Dependencia.** Este plan se apoya en la infraestructura de la rama
  `esp32rv`, que todavía no está en `main`. La rama `msp430` sale de `main`
  (`69f2582`), así que **la fase M0 empieza integrando `esp32rv`**:
  * `GdbRspBase` con sus *backends*;
  * `herramienta_if`;
  * `congela.h` y `--stopped-core-stops-sim`;
  * `mcu_if::nodo_depuracion`;
  * y las sondas con JTAG.
* **El plan tiene trece fases (M0-M12, §11):**
  * M0 prepara el terreno;
  * M1 y M2 son el núcleo y su depuración en un laboratorio sin chip;
  * de M3 a M8, los chips por familias, cada uno con su LaunchPad;
  * M9 y M10, el BSL (ya sin bloqueo);
  * M11, la eZ-FET;
  * M12, la GUI.

---

## 1. La documentación

### 1.1 Erratas: las diez están

| MCU | Fe de erratas | Fecha de la revisión |
| :--- | :--- | :--- |
| MSP430G2553 | SLAZ440K | mayo de 2021 |
| MSP430F5529 | SLAZ314AD | mayo de 2021 |
| MSP430FR4133 | SLAZ550AA | mayo de 2021 |
| MSP430FR2433 | SLAZ664S | mayo de 2021 |
| MSP430FR2311 | SLAZ679R | agosto de 2021 |
| MSP430FR2355 | SLAZ695J | septiembre de 2021 |
| MSP430FR2476 | SLAZ726B | agosto de 2021 |
| MSP430FR5969 | SLAZ473AG | agosto de 2021 |
| MSP430FR6989 | SLAZ517AG | agosto de 2021 |
| MSP430FR5994 | SLAZ681O | agosto de 2021 |

Las erratas de CPU que afectan al modelo vienen, además, en dos fuentes
legibles por programa:

* la columna `CPU_Bugs` de `devices.csv`: el G2553 no tiene ninguna; el F5529,
  CPU21, CPU22, CPU23, CPU27, CPU29, CPU30 y CPU40; las FR, CPU21, CPU22 y CPU40;
* y la base de datos de la MSP Debug Stack, que dice qué trucos aplica la
  herramienta para cada una.

### 1.2 Lo que ha llegado y para qué sirve

| Documento | Qué tiene | Para qué fase |
| :--- | :--- | :--- |
| **SLAU550AB** | El BSL de todas las FRAM:<br>• memoria y Z-Area (0x1000-0x100F, con la entrada en 0x1000 y la «BSL Action» en 0x1002, R13 = 0xDEAD y R14 = 0xBEEF);<br>• entrada por TEST/RST, por software y por chip en blanco;<br>• plazo de 10 s;<br>• configuración por TLV (etiqueta 0xB5) y de usuario (0xFF88, firma 0x695A);<br>• protocolo y versiones (§9) | M9, M10 |
| **SLAU656D** | Guía de la API de `libmsp430`: inicialización, conexión a un chip en marcha, varias sondas, JTAG o SBW, EEM y actualización del firmware de la sonda | M11 |
| **MSP Debug Stack, código abierto** | Ver §1.3 | M0, M2, M8, M11 |
| **Paquete de desarrollo de la DLL** | `libmsp430_64.so` (Linux), `MSP430.dll`, `libmsp430.dylib`, las cabeceras de la API (`MSP430.h`, `MSP430_Debug.h`, `MSP430_EEM.h`, `MSP430_FET.h`) y la guía | M11 (la herramienta real) |
| **MSPBSL 1.02.00.01** | Fuentes BSD-3 del BSL de 5xx/6xx:<br>• intérprete de órdenes, API de Flash e interfaces UART por Timer_A, USCI y eUSCI, I2C y USB;<br>• proyectos para F552x por USB y por Timer_A, F543xA, F522x por I2C y CC430;<br>• un BSL para G2xx en la información (UART por software, 9600 8N1, con P1.3 de habilitación);<br>• **las imágenes publicadas** (licencia «TI Text File»): las del F552x son UART 00.07.05.04 y USB 00.07.88.38 / 00.08.88.39 | M9 |
| **Replicator430** (SLAU320, v1.22) | Cuatro programadores de referencia en C, BSD-3, para un MSP430 anfitrión: `Replicator430` (CPU), `430X`, `430Xv2` y `430FR` (FR4xx y FR57xx-FR6xx). Cada uno trae sus funciones de JTAG y SBW (`JSBW.c`, `LowLevelFunc*.c`) y sus *funclets* de Flash y FRAM | M2, M3 |
| **REP430F** (Elprotronic) | Manual del programador comercial basado en el Replicator. No aporta datos que no estén en SLAU320; queda como referencia | — |
| **SLAA754** | SBW desde un MSP432P401R, un CC3220SF o un CC2640R2 a un G2553 y un FR2311, con los pines (P6.0 a RST y P6.1 a TEST en la MSP-EXP432P401R) y el formato de las imágenes. **Falta su código (`slaa754.zip`)** | M3, M7 |
| **`targetdb`** | Cada dispositivo (`devices/MSP430*.xml`): EEM, número de disparadores, control de relojes, SBW, base de los vectores, rango de la contraseña de JTAG y segmentos de la MPU. Cada módulo (`Modules/msp430/*.xml`): registros con dirección absoluta, campos con acceso y valores con nombre. **Sin valores de reset** | M0, M3-M8 |

### 1.3 La MSP Debug Stack, por dentro

El paquete de código abierto es **BSD-3 en todos sus fuentes**: 99 ficheros de
`Bios/src/hal`, 34 de 38 de `Bios/src/hil`, 73 de 90 de `Bios/src/fw` y 265 de
279 de `DLL430_v3/src`. Ninguno lleva la licencia «TI Text File». El manifiesto
reserva esa licencia para las **imágenes** de firmware y para el módulo de
EnergyTrace. Para `mcu-sim` importan cinco piezas:

| Pieza | Dónde | Qué da |
| :--- | :--- | :--- |
| **Las macros HAL** | `Bios/src/hal/macros/` (94) | Lo que la eZ-FET ejecuta para cada operación: `GetJtagId`, `SyncJtag_AssertPor_SaveContextXv2`, `ReadMemWordsXv2`, `WriteFramQuickXv2`, `EemDataExchangeXv2`, `MagicPattern`, `UnlockDeviceXv2`, `BlowFuse*`, `SingleStepXv2`, `PollJStateReg`… En versiones CPU, X y Xv2 |
| **Las definiciones de JTAG y del EEM** | `Bios/src/hal/JTAG_defs.h`, `EEM_defs.h` | Las instrucciones del TAP, **escritas con los bits invertidos** porque el firmware las desplaza con el MSB primero. El comentario de cada una da el código de [JTAG]: `IR_CNTRL_SIG_16BIT` 0xC8, «13». Añaden las que [JTAG] no cuenta: `IR_EMEX_DATA_EXCHANGE` (09), `IR_EMEX_READ_TRIGGER` (0A), `IR_EMEX_READ_CONTROL` (0B), `IR_EMEX_WRITE_CONTROL` (0C), `IR_JSTATE_ID` (62) y `IR_TDO_EVENT` (64). Y todos los registros del EEM |
| **El HIL** | `Bios/src/hil/` | El nivel de pines: 4 hilos (`hil_4w.c`, `hil_4w_ezFET.c`) y SBW (`hil_2w.c`, `hil_2wDma.c`) con sus tiempos |
| **La biblioteca** | `DLL430_v3/` | El otro lado: cómo se programa el EEM (`DebugManagerMSP430.cpp`: GENCLKCTRL por omisión 0x26), los puntos de ruptura por software (`0x4343`), el borrado y la escritura de Flash y FRAM, la actualización del firmware (`UpdateManagerFet.cpp`) y la enumeración de sondas (`UsbCdcIoChannel.cpp`) |
| **La base de datos** | `DLL430_v3/src/TI/DLL430/DeviceDb/devicedb.h` | 36 XML comprimidos. Por familia (`F2xx.xml`, `F55xx.xml`, `FR2xx.xml`, `FR4xx.xml`, `FR69xx.xml`… y uno por chip para FR2355 y FR2476), da para cada dispositivo:<br>• el identificador;<br>• el mapa como lo ve la herramienta;<br>• `eem`, `clockInfo` y `eemTimers`;<br>• las *funclets* |

Además, `Bios/include/eZ_Fet*.h` y `MSP_Fet*.h` son las **imágenes** del
firmware: núcleo, HAL, HIL, canal de comunicación y DC-DC. El HIL de la eZ-FET
es la versión 0x000D. Son las que `libmsp430` compara con la sonda: la versión
está en 0x18F6 y el CRC en 0x18FA de la imagen HIL. Y
`ThirdParty/BSL430_Firmware` es el BSL USB de la propia sonda, del mismo
protocolo HID que el del F5529. Es una segunda fuente para M9.

### 1.4 Lo que todavía conviene añadir

| Documento | Para qué | ¿Bloquea? |
| :--- | :--- | :--- |
| **`slaa754.zip`** (el código de SLAA754) y el SimpleLink MSP432P4 SDK con el que se compiló | La prueba con un MSP432P401R que programa un G2553 y un FR2311 por SBW (M3, M7) | No: M3 y M7 cierran con la sonda de bancada y el Replicator |
| SLAA450 «Creating a Custom Flash-Based BSL» | Explica lo que MSPBSL trae en código | No |

### 1.5 Herramientas

* **`msp430-elf-gcc` 9.3.1.11** (`msp430-gcc-opensource/`). Trae `gcc`, `gdb` y
  `msp430-elf-run`, el simulador de GDB con CPUX y multiplicador, que sirve de
  oráculo para M1. El instalador completo (9.3.1.2) añade las cabeceras y los
  guiones de enlazado y, según [GCC], el **agente de GDB de TI**
  (`gdb_agent_console`, con `libmsp430`). Hay que comprobarlo al desempaquetarlo
  en M11.
* **`libmsp430_64.so` 3.15.1.1** ([MSPDS-DEV]), y la misma compilada de los
  fuentes ([MSPDS], `make STATIC=1` con boost y hidapi). Es **la herramienta
  real** de M11.
* **`mspdebug`**. La versión de `apt` es la 0.22, y trae `sim`, `gdbc`, `tilib`,
  `uif`, `uif-bsl`, `flash-bsl`, `goodfet`, `olimex*`, `rf2500` y `pif`. Le
  falta `rom-bsl` y los controladores de *bit-bang* modernos. `tilib` es
  `libmsp430` por debajo, así que llega con M11. El *bit-bang* pide compilarlo
  de git.
* **`python-msp430-tools`** (pip): `msp430.bsl.target` (1xx/2xx/4xx),
  `msp430.bsl5.uart` (5xx/6xx/FR) y `msp430.bsl5.hid` (F5529). Es la herramienta
  real del BSL para la integración continua.
* **BSL-Scripter** de TI (instaladores en `doc/refs/`), para las prácticas.

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
| FR2355 | CPUXV2 | MPY32 | CS con FLL, 24 MHz | FRAM 32 KB, RAM 4 KB, ROM de bibliotecas 20 KB | S (3) | 0x98 | ROM BSL1 + BSL2, configurable por TLV |
| FR2433 | CPUXV2 | MPY32 | CS con FLL, 16 MHz | FRAM 15,5 KB, RAM 4 KB | S (3) | 0x98 | ROM BSL1 + BSL2 |
| FR4133 | CPUXV2 | — | CS con FLL, 16 MHz | FRAM 15,5 KB, RAM 2 KB | S (3) | 0x98 | ROM de 1 KB, solo UART |
| FR2476 | CPUXV2 | MPY32 | CS con FLL, 16 MHz | FRAM 64 KB, RAM 8 KB | S (3) | 0x98 | ROM BSL1 + BSL2, configurable por TLV |
| FR5969 | CPUXV2 | MPY32 | CS, 16 MHz | FRAM 64 KB, RAM 2 KB | S (3) | 0x99 | ROM, UART (I2C solo en el FR59691) |
| FR5994 | CPUXV2 | MPY32 | CS, 16 MHz | FRAM 256 KB, RAM 4 KB + 4 KB compartida con LEA | S (3) | 0x99 | ROM, UART |
| FR6989 | CPUXV2 | MPY32 | CS, 16 MHz | FRAM 128 KB, RAM 2 KB | S (3) | 0x99 | ROM, UART |

El JTAG ID de cada MCU sale de las tablas de dispositivos de [JTAG], cap. 3 y
anexos: 0x89 para 1xx/2xx/4xx, 0x91 para 5xx/6xx, 0x98 para FR2xx/FR4xx y 0x99
para FR5xx/FR6xx. Se confirma al cerrar cada fase contra la tabla del anexo. El
identificador que lee después la herramienta (la palabra de `IR_DEVICE_ID` o
0x0FF0 en 2xx) sale de `devicedb.h`: 0x5325 para el G2xx3 y 0x8169 para el
FR5969, por ejemplo.

### 2.2 Mapas de memoria

| MCU | Memoria principal | RAM | Información | BSL | Otras regiones |
| :--- | :--- | :--- | :--- | :--- | :--- |
| G2553 | Flash C000-FFFF (vectores FFC0-FFFF) | 0200-03FF | 1000-10FF (A-D de 64 B; A, calibración) | ROM 0C00-0FFF | Periféricos de 16 bits 0100-01FF, de 8 bits 0010-00FF, SFR 0000-000F |
| F5529 | Flash 004400-0243FF, bancos A-D (vectores FF80-FFFF) | 2400-43FF | 1800-19FF (A-D) | Flash 1000-17FF | RAM de USB 1C00-23FF; TLV 1A00-1AFF |
| FR2311 | FRAM F100-FFFF | 2000-23FF | — | BSL1 1000-17FF, BSL2 FFC00-FFFFF | TLV 1A00 |
| FR2355 | FRAM 8000-FFFF | 2000-2FFF | 1800-19FF | BSL1 1000-17FF, BSL2 FFC00-FFFFF | ROM FAC00-FFBFF (DriverLib y FFT); RAM diminuta 0006-001F |
| FR2433 | FRAM C400-FFFF | 2000-2FFF | 1800-19FF | BSL1 1000-17FF, BSL2 FFC00-FFFFF | — |
| FR4133 | FRAM C400-FFFF | 2000-27FF | 1800-19FF | 1000-13FF | Memoria de LCD |
| FR2476 | FRAM 8000-FF7F + 10000-17FFF | 2000-3FFF | 1800-19FF | BSL1 1000-17FF, BSL2 FFC00-FFFFF | RAM diminuta 0006-001F |
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
   por `herramientas/gen_msp430.py`. Es un `constexpr` que sale de cuatro
   fuentes de TI, que se cruzan entre sí:

   | Fuente | Qué da al descriptor |
   | :--- | :--- |
   | `include_gcc/devices.csv` | CPU, MPY, SBW, EEM y número de disparadores, erratas de CPU, y el mapa (RAM, RAM2, RAM diminuta, RAM de USB y de LEA, BSL, información A-D, memoria principal 1 y 2, firma y vectores) |
   | `include/msp430*.h` | Cada módulo con su dirección base (`__MSP430_BASEADDRESS_*`), su instancia y su vector (`*_VECTOR`), con la tabla de normalización de nombres de §2.3 |
   | `targetdb/devices/MSP430*.xml` y `Modules/msp430/*.xml` | Las mismas propiedades que `devices.csv` (`eem`, `breakpoints`, `clockcontrol`, `spybiwire`, `iv_base`), la lista de módulos y, por módulo, **los registros con su dirección absoluta y sus campos**. Sirve para dos cosas: generar la tabla de registros de cada módulo que el modelo comprueba (un acceso a una dirección sin registro avisa) y contrastar los nombres de los campos con el modelo |
   | `devicedb.h` de la MSP Debug Stack | El identificador que lee la herramienta (por ejemplo, 0x5325 para el G2xx3 y 0x8169 para el FR5969), el `clockInfo` y los `eemTimers`: la lista de módulos cuyo reloj puede parar el depurador, con su valor por omisión (`defaultStop`: el WDT_A y los Timer_A sí, las USCI y los ADC no) |

   **A mano, desde la hoja de datos**, quedan el encapsulado, las funciones de
   cada pin (tabla «Signal Descriptions» y las de «Input/Output Diagrams»), los
   disparos del DMA y del ADC, y el contenido del TLV.

   El generador se comprueba contra la hoja de datos de los diez chips de este
   plan. Es la prueba de que acierta con los demás. Cada discrepancia entre las
   cuatro fuentes la imprime y la tiene que resolver una persona: el descriptor
   no se genera si quedan sin resolver. Los pines se escriben con el mismo patrón
   que los `gen_pines.py` del F3 y del E4.

**Ninguna de las cuatro fuentes se versiona.** Son de la instalación de CCS y del
paquete de TI. Se versiona lo que genera el generador, con la versión de cada
fuente anotada en la cabecera: `devices.csv` 1.213, MSPDS 3.15.1.1.

Un chip **de otra familia** (un F1xx, un F4xx o un i20xx) escribe además su
sistema de reloj y los módulos que no existan. La CPU, la depuración, GDB y el
BSL de 1xx/2xx/4xx le llegan hechos: la CPU 16 bits y la CPUX están desde M1, y
el EEM de los cuatro tamaños desde M2. El `targetdb` y `devicedb.h` ya traen sus
datos: 603 dispositivos en uno y 36 familias en el otro.

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

### 7.1 Lo que define [JTAG], y lo que añade la MSP Debug Stack

* **El TAP de MSP430**: la máquina de 16 estados de IEEE 1149.1, pero con una
  **IR de 8 bits**. Al desplazar la IR, por TDO sale **el JTAG ID** [JTAG §1.3]:
  0x89, 0x91, 0x98 o 0x99. Las instrucciones son las de la tabla.
  * La columna «Código» es la de [JTAG].
  * La MSP Debug Stack escribe las mismas instrucciones **con los bits
    invertidos** (`IR_CNTRL_SIG_16BIT` 0xC8 = 0x13 invertido), porque su
    firmware las desplaza con el MSB primero. El comentario de cada `#define`
    da el código de [JTAG].
  * Que las dos fuentes coincidan, bit a bit, es una comprobación de M2.

  | Instrucción | Código | Qué hace | Fuente |
  | :--- | :--- | :--- | :--- |
  | `IR_ADDR_16BIT` | 0x83 | Escribe en el MAB (20 bits en CPUX/CPUXV2) | [JTAG], [MSPDS] |
  | `IR_ADDR_CAPTURE` | 0x84 | Lee el MAB | ambas |
  | `IR_DATA_TO_ADDR` | 0x85 | Dato en la dirección del MAB | ambas |
  | `IR_CAPTURE_CPU_REG` | 0x86 | Lee un registro de la CPU | [MSPDS] |
  | `IR_DEVICE_ID` | 0x87 | Puntero al identificador (5xx y FR) | ambas |
  | `IR_DATA_16BIT` | 0x41 | Escribe o lee el MDB | ambas |
  | `IR_DATA_CAPTURE` | 0x42 | Lee el MDB sin escribir | ambas |
  | `IR_DATA_QUICK` | 0x43 | Accesos rápidos con autoincremento del PC | ambas |
  | `IR_DATA_PSA` | 0x44 | Firma (PSA) de un bloque de memoria | ambas |
  | `IR_SHIFT_OUT_PSA` | 0x46 | Lee la firma | ambas |
  | `IR_EMEX_DATA_EXCHANGE` | 0x09 | Lee o escribe un registro del EEM | [MSPDS] |
  | `IR_EMEX_READ_TRIGGER`, `IR_EMEX_READ_CONTROL`, `IR_EMEX_WRITE_CONTROL` | 0x0A-0x0C | Estado del EEM y su control | [MSPDS] |
  | `IR_CNTRL_SIG_16BIT` | 0x13 | Registro de señales de control: R/W, HALT_JTAG, BYTE, TCE1, PUC, RELEASE_LBYTE… | ambas |
  | `IR_CNTRL_SIG_CAPTURE` | 0x14 | Lo lee | ambas |
  | `IR_CNTRL_SIG_RELEASE` | 0x15 | Devuelve la CPU a su reloj | ambas |
  | `IR_COREIP_ID` | 0x17 | Identificador del núcleo (5xx y FR) | ambas |
  | `IR_PREPARE_BLOW`, `IR_EX_BLOW` | 0x22, 0x24 | Fusible (1xx/2xx/4xx) | ambas |
  | `IR_JMB_EXCHANGE` | 0x61 | El buzón JTAG (5xx y FR) | ambas |
  | `IR_JSTATE_ID` | 0x62 | El registro JSTATE de las FR: el estado de la CPU y del LPMx.5 sin pararla | [MSPDS] |
  | `IR_TDO_EVENT`, `IR_TDO_EVENT_CTL` | 0x64, 0x65 | Aviso de parada por TDO | [MSPDS] |
  | `IR_BYPASS` | 0xFF | Bypass | ambas |

* **La CPU se controla «a mano» desde el JTAG.** No es como un DAP de ARM o un DM
  de RISC-V, que piden la parada y esperan. Aquí la herramienta toma el reloj de
  la CPU: **TCLK es TDI** cuando la CPU está bajo control JTAG, y cada flanco
  avanza un ciclo. Para leer memoria, la herramienta pone la dirección en el MAB,
  el sentido en el registro de control y da flancos de TCLK [JTAG §2.3]. **Esto
  marca el modelo**: hay que modelar el núcleo ciclo a ciclo **cuando está bajo
  JTAG**, y por instrucciones cuando corre libre. El núcleo tiene los dos modos y
  el registro de control decide cuál.
* **Spy-Bi-Wire** [JTAG §1.5; `hil_2w.c`]: dos hilos, SBWTCK (en TEST) y
  SBWTDIO (en RST). Cada ciclo de TCK de JTAG son tres franjas de SBWTCK:
  **TMS**, **TDI** y **TDO**. En la franja de TDO, el chip gobierna SBWTDIO. La
  temporización tiene un tope: SBWTCK en bajo más de 7 µs saca al chip de SBW.
  El modelo tiene que tenerlo en cuenta, porque un SBWTCK quieto devuelve el pin
  RST a su función. [SBW-SL] da el mismo procedimiento visto desde un anfitrión
  que no es MSP430.
* **Entrada en JTAG** [JTAG §2.3.1; `StartJtag.c`]. La secuencia de TEST y RST
  elige entre SBW y JTAG de 4 hilos. En las 5xx y las FR hay además una
  **«magic pattern»** (`MagicPattern.c`, por el JMB) que **para la CPU antes de
  la primera instrucción del usuario**. Sin ella no se puede recuperar un chip
  cuyo programa desactiva el JTAG o duerme en LPMx.5.
* **JSTATE** (FR): un registro de 64 bits que se lee por `IR_JSTATE_ID` sin parar
  la CPU. Lo usa `libmsp430` para saber si la CPU está parada, en qué LPM está o
  si ha entrado en LPMx.5 (`PollJStateReg.c`, `PollJStateRegFR57xx.c`). Es lo
  que permite depurar con LPMx.5, y hay que modelarlo en las FR.
* **Buzón JTAG (JMB)** (5xx y FR): dos registros de 16 bits en cada sentido,
  entre el JTAG y la CPU (`SYSJMBC`, `SYSJMBI0/1` y `SYSJMBO0/1`). Lo usan la
  «magic pattern», la contraseña de JTAG (`DR_JMB_PASSWORD_EXCHANGE_REQUEST`
  0x1E1E) y los programas que hablan con el host.
* **Protección**:
  * en 1xx/2xx/4xx, el **fusible** (`IR_PREPARE_BLOW`/`IR_EX_BLOW`;
    `BlowFuse.c`), que en el chip real necesita 6,5 V en TEST. En el modelo es
    un bit persistente en la memoria no volátil, que **nunca se fija por
    omisión** y que una opción restaura (como `--restore-rdp`);
  * en 5xx/6xx, la **firma de bloqueo** en 0x17FC-0x17FF (zona del BSL);
  * en FR, la **firma en 0xFF80**: 0x5555 bloquea; con contraseña, 0xAAAA y la
    longitud. Se desbloquea con el «User Code Erase» por JMB o con la contraseña
    [JTAG §2.3.4; `UnlockDeviceXv2.c`]. El `targetdb` da el rango de la
    contraseña de cada chip (`Password` = «0-120» en el FR5969).
* **El EEM** [UG2 cap. 28; `EEM_defs.h`; `DebugManagerMSP430.cpp`]. Ya tiene
  mapa de registros. Se lee y se escribe por `IR_EMEX_DATA_EXCHANGE` (en Xv2,
  `EemDataExchangeXv2.c`), con estas direcciones:

  | Registro | Dirección | Qué hace |
  | :--- | :--- | :--- |
  | Bloques de disparo TB0-TB9 | 0x00 + 8·n | `MBTRIGxVAL`, `MBTRIGxCTL` (MAB o MDB, tipo de acceso TRIG_0-TRIG_F, comparación =/>/</≠), `MBTRIGxMSK` y `MBTRIGxCMB` (a qué combinación va) |
  | `BREAKREACT` | 0x80 | Qué combinaciones paran la CPU |
  | `GENCTRL` | 0x82 | EEM_EN, CLEAR_STOP, EMU_CLK_EN, EMU_FEAT_EN, DEB_TRIG_LATCH, EEM_RST y E_STOPPED |
  | `EEMVER` | 0x86 | Versión y tamaño del EEM |
  | `GENCLKCTRL` | 0x88 | Qué relojes se paran con la CPU parada: STOP_ACLK, STOP_SMCLK, STOP_MCLK, y la selección de cada uno |
  | `MODCLKCTRL0` | 0x8A | Qué **módulos** se paran (control de relojes «extendido», 5xx y FR) |
  | `TRIGFLAG` | 0x8E | Qué disparadores han saltado |
  | `EVENT_REACT`, `EVENT_CTRL` | 0x94, 0x96 | Reacciones por evento |
  | `STOR_CTL` | 0x9E | Almacenamiento de estado (EEM L) |

  Los tamaños son estos [UG2, tabla 28-1]:

  | | XS | S | M | L |
  | :--- | :---: | :---: | :---: | :---: |
  | Disparadores de bus | 2 (= y ≠) | 3 | 5 | 8 |
  | Disparadores de registro | 0 | 1 | 1 | 2 |
  | Combinaciones | 2 | 4 | 6 | 8 |
  | Secuenciador | — | — | sí | sí |
  | Almacenamiento de estado | — | — | — | sí |

  En `devices.csv`, el `targetdb` y `devicedb.h` se llaman `EMEX_LOW` (el
  G2553), `EMEX_SMALL_5XX` (las FR) y `EMEX_LARGE_5XX` (el F5529).

### 7.2 Dónde va cada cosa

* `src/m430/debug/tap_m430.h`: el TAP, la IR, los registros de datos y el
  registro de control, sobre el núcleo. Funciona en JTAG de 4 hilos.
* `src/m430/debug/sbw.h`: la máquina de franjas de SBW **delante** del TAP.
  Convierte SBWTCK y SBWTDIO en TCK, TMS, TDI y TDO del TAP. Lleva su tope de
  7 µs.
* `src/m430/debug/eem.h`: el EEM parametrizado por su tamaño, con el mapa de
  §7.1 y la misma interfaz `debug_if` del núcleo que usan el FPB y los
  *triggers* de RISC-V. Incluye el control de relojes, que el SoC consulta para
  parar los módulos.
* `src/m430/debug/jmb.h` y `jstate.h`: el buzón y el JSTATE.
* `src/m430/debug/proteccion.h`: el fusible y las firmas.
* `src/m430/sonda/`: la **sonda de bancada** (§7.3), compartida por `--gdb` y
  por la eZ-FET de alto nivel (M11).
* La entrada en JTAG y la selección de pines es del chip: el descriptor dice
  qué pines son TCK, TMS, TDI y TDO, y la placa dice si están conectados.

### 7.3 Cómo se llega desde fuera

**La sonda de bancada** es un *port* a C++ de las macros HAL de la MSP Debug
Stack (BSD-3; la atribución va en la cabecera de cada fichero):

* por arriba, las operaciones de alto nivel que pide GDB;
* por abajo, el HIL de 4 hilos o de SBW sobre los `AnalogNet` de los pines.

Al ser el código de la eZ-FET real, lo que haga la sonda de bancada es lo que
hará la sonda de verdad. Su segunda fuente es el **Replicator430**: otra
implementación de TI de las mismas secuencias, escrita por otra gente y con
otra estructura. Las dos tienen que dar las mismas transacciones de JTAG para
cada operación, y eso se comprueba con una traza.

| Camino | Qué | Coste | Fase |
| :--- | :--- | :--- | :--- |
| **Sonda de bancada** (`--gdb`) | Stub de GDB sobre las macros HAL portadas, por los pines, en JTAG o SBW | Cientos de flancos por acceso | M2 |
| **Replicator430 en un MSP430 simulado** | El código de SLAU320 compilado con `msp430-elf-gcc` para un anfitrión que `mcu-sim` ya simula (el F5529 de M4; el original es para un F5437), y cableado al objetivo en una placa de dos MCUs | Firmware real de TI programando por los pines | M4 (para el G2553 y el F5529) y M8 (430FR) |
| **SLAA754 en una MSP-EXP432P401R** | El ejemplo de TI: el MSP432 programa un G2553 o un FR2311 por SBW (P6.0 → RST, P6.1 → TEST) e informa por el *backchannel* | Firmware real de TI en un anfitrión ARM ya modelado | M3, M7 (si llega `slaa754.zip`) |
| **`mspdebug` de git** por *bit-bang* | Su `jtaglib` es una tercera implementación de [JTAG]; se sirve por un pty, como `remote_bitbang` | Igual | Opcional, tras M2 |
| **eZ-FET emulada** | La sonda de la LaunchPad (M11), para `libmsp430` | Órdenes HAL por USB | M11 |

### 7.4 Lo que no está

* **EnergyTrace** (la medida de corriente de la eZ-FET): el código está bajo la
  licencia «TI Text File» y, como en el XDS110 del P4, queda fuera.
* La programación del fusible con tensión real: solo como bit de modelo.
* El **almacenamiento de estado** del EEM L (F5529): se declara (`STOR_CTL`
  contesta y avisa).

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
    que la herramienta real escribe en la RAM o la FRAM `0x4343` (`CLR.B R3`)
    y un disparador del EEM detecta su búsqueda (§8.2); en la Flash usa un
    disparador de hardware;
  * `Z1`: por hardware, un disparador de bus;
  * `Z2`-`Z4`: puntos de vigilancia, disparadores de MDB y MAB.

  El número sale del tamaño del EEM.
* `vCont` con paso; Ctrl-C; `monitor reset`, `monitor reset halt` y
  `monitor erase`. Son las órdenes que entiende el agente de GDB de TI. Los
  nombres se comprueban en M11 contra el agente y contra `mspdebug gdb`.

### 8.2 Se comporta como la herramienta real

Con la parada del depurador, el modelo hace lo que hace la sonda real con
`libmsp430` en su configuración por omisión. Es la regla de `herramienta_if`
que se cumplió en todos los ARM y en el C3, y ahora tiene los valores exactos:

* **El control de relojes del EEM** [`DebugManagerMSP430.cpp`, `IDebugManager.h`
  y `devicedb.h`]:
  * **GENCLKCTRL = 0x26** por omisión (`DefaultClkCntrl`): STOP_ACLK (0x02),
    STOP_SMCLK (0x04) y el bit 0x20. En los chips de control «estándar»
    (`GCC_STANDARD`, el G2553), además el bit 0, para que SMCLK no salga de
    TCLK durante la depuración. MCLK es el de la CPU, que está parada;
  * **MODCLKCTRL0**, en los de control «extendido» (`GCC_EXTENDED`: el F5529
    y todas las FR), se forma con los `eemTimers` del chip que tienen
    `defaultStop`. El **WDT_A** y los Timer_A se paran; las USCI/eUSCI, el
    ADC12 y el resto siguen. Así una UART no pierde el carácter que estaba
    recibiendo cuando el depurador para la CPU, y el WDT no muerde.

  Los `eemTimers` de cada chip salen de `devicedb.h` al descriptor (§5).
* **Los puntos de ruptura por software** escriben `0x4343` (`CLR.B R3`, una
  instrucción sin efecto) en la RAM o la FRAM, y un disparador del EEM detecta
  su búsqueda [`SoftwareBreakpoints430.cpp`]. En la Flash no se puede, y van por
  hardware. GDB los pide con `Z0` y el stub los traduce igual.
* **`monitor reset halt`**: con la «magic pattern» en 5xx y FR, la parada es antes
  de la primera instrucción; en 2xx, con el PUC por JTAG
  (`SyncJtag_AssertPor_SaveContext*`).
* **El `load`**: borra la memoria principal y deja la información salvo que se
  pida. Guarda y restaura el TLV, como hace la 3.15.1 [`revisions.txt`: «backup
  of TLV data during erase»]. Respeta la MPU y la IPE como la herramienta con
  sus opciones por omisión, y avisa si hay que desactivarlas.
* **LPMx.5**: con la CPU en LPMx.5, la herramienta real lo ve por JSTATE y
  espera a que despierte (`D_LPMX5_MODE` y `D_LPMX5_WAKEUP`). El stub hace lo
  mismo: GDB ve la CPU «en marcha» y no se pierde la conexión.

Con `--stopped-core-stops-sim` el tiempo se congela entero, como en los demás
MCUs.

### 8.3 Opciones

| Opción | En un MSP430 |
| :--- | :--- |
| `--gdb-dap` | Stub interno, directamente sobre el control de la CPU y el EEM |
| `--gdb` | Stub con la sonda de bancada por los pines (JTAG o SBW, según la placa) |
| `--jtag=sbw` o `--jtag=4hilos` | Elige el protocolo de la sonda de bancada. Por omisión, el que conecte la placa |
| `--ezfet` y `--ezfet-imagenes DIR` | Exponen la eZ-FET emulada (M11); el segundo da la carpeta de la copia de [MSPDS] de la que salen sus versiones o su firmware |
| `--bsl FICHERO` o `--bsl ninguno` | Como en el P4 (§9) |

---

## 9. El cargador de arranque (BSL)

### 9.1 Tres protocolos, cuatro implementaciones

| | 1xx/2xx/4xx (G2553) | 5xx/6xx (F5529) | FR5xx/FR6xx (FR5969, FR5994, FR6989) | FR2xx/FR4xx (FR2311, FR2355, FR2433, FR2476, FR4133) |
| :--- | :--- | :--- | :--- | :--- |
| Fuente | [BSL] cap. 2 | [BSL] cap. 3; [MSPBSL] (fuentes) | [BSLFR], [BSLFRAM] | [BSLFRAM] |
| Dónde | ROM 0C00-0FFF | Flash 1000-17FF (borrable y sustituible) | ROM 1000-17FF (2 KB) | FR4133: ROM 1000-13FF (1 KB). FR23xx-FR26xx: 1000-17FF + **FFC00-FFFFF** (3 KB) |
| Interfaz | UART 9600 8E1 semidúplex, por pines fijos (TX P1.1, RX P1.5 en el G2553) | **USB HID** (VID 0x2047, PID 0x0200) | UART por eUSCI_A0 (9600 8E1, puede subir). I2C (0x48) por eUSCI_B0 **solo en las variantes «1»** (FR59691…): el FR5969, el FR5994 y el FR6989 de las LaunchPad son de UART | UART (9600 8E1) e I2C (0x48). En el FR4133, solo UART y solo a 9600 |
| Entrada | Secuencia de TEST y RST | PUR en alto en el reset, o vector de reset en blanco | Secuencia de TEST y RST; desde la aplicación (Z-Area) | Secuencia; desde la aplicación; **chip en blanco** (vector de reset = 0xFFFF) en FR23xx-FR26xx |
| Trama | SYNC 0x80 → ACK 0x90; HDR, CMD, L1, L2, AL, AH, LL, LH, datos y *checksum* XOR | 0x80, longitud, núcleo (CMD + datos) y CRC-CCITT | La de 5xx | La de 5xx |
| Órdenes | RX data block, RX password, borrar segmento, borrado masivo, borrado de comprobación, cambiar baudios, cargar PC, TX data block, versión | RX data block (y *fast*), RX password, borrar segmento, bloquear o desbloquear INFO, borrado masivo, CRC, cargar PC, TX data block, versión, tamaño del búfer, cambiar baudios | Las de 5xx sin las de Flash | Las de 5xx |
| Seguridad | Contraseña: los 32 bytes de vectores; si falla, **borrado masivo**, salvo que la palabra de 0xFFDE (BSLSKEY) lo impida | Contraseña de vectores; si falla, borrado masivo | Igual; la firma de 0xFF84 desactiva el BSL | Igual |
| Versión (de [BSLFRAM] §7) | — | 00.07.88.38 o 00.08.88.39 (USB) | 00.07.34.B2 (FR5969, FR6989); 00.08.35.B3 (FR5994) | 00.08.35.B3 (FR2311, FR2433); 00.09.36.B4 (FR2355); 00.09.36.B5 (FR2476); 00.87.45.74 (FR4133) |
| Particularidades | — | — | Máximo 8 MHz si se llama desde la aplicación; borra RAM 0x1C00-0x1FFF | Plazo de **10 s** sin comunicación → LPM4 (Timer_B en FR23xx, Timer_A en FR26xx). FR2355 y FR2476: configuración por el **TLV** (etiqueta 0xB5: temporizador, eUSCI, puertos y máscaras, dirección I2C, RAM diminuta) y **de usuario** en 0xFF88 (firma 0x695A: interfaz y borrado de RAM). Unos 300 ms de arranque. Borra RAM 0x2000-0x21FF (o 0x23FF) |

**La Z-Area de las FRAM** [BSLFRAM §3.2.2, §3.3.1]: 0x1000-0x100F es la única
parte del BSL que la aplicación puede leer y a la que puede saltar. En 0x1000
está la entrada. En 0x1002 está la «BSL Action» (R12 = función, R13 = 0xDEAD,
R14 = 0xBEEF), que en las FR5xx/FR6xx es siempre «volver al BSL». El modelo
protege el resto del BSL igual: una lectura desde fuera da lo que da el chip, y
un salto desde fuera da el reset que da el chip.

**La secuencia de entrada** [BSLFRAM §3.3.2]:

1. con RST/NMI en bajo, TEST da al menos dos flancos de subida;
2. el primer nivel alto de TEST dura al menos tSBW,En;
3. RST/NMI sube con TEST en alto;
4. y el BSL arranca cuando TEST baja.

No arranca si el JTAG tiene el control, si hay un POR o si RST está configurado
como NMI. El modelo vigila esos casos, porque son los errores de laboratorio
más comunes.

### 9.2 Cómo se modela

`bsl_p4.h` resolvió el problema de licencia del BSL de TI con tres contenidos.
Aquí se repiten, pero el B1 cambia según el chip:

| | B1, por omisión | B2, `--bsl FICHERO` | B3, `--bsl ninguno` |
| :--- | :--- | :--- | :--- |
| **F5529** | **El BSL USB de TI compilado de sus fuentes BSD-3** ([MSPBSL] `IAR_v6_MSP430F552x_USB`: intérprete de órdenes, API de Flash e interfaz USB con la pila USB de TI). Hay que portarlo de IAR a `msp430-elf-gcc`: el C es C, y el `BSL430_Low_Level_Init.s43` y el enlazado se reescriben. Se versiona con su huella (T-22) | La imagen publicada (00.07.88.38 o 00.08.88.39), de la licencia «TI Text File»: la trae quien la tenga | La Flash del BSL en blanco |
| **FR5xx/FR6xx y FR2xx/FR4xx** | Un BSL propio (AGPLv3) hecho con piezas de [MSPBSL]: su intérprete de órdenes, su interfaz `eUSCI_UART_I2C`, y una API de FRAM nueva (versión 0x3x según [BSLFRAM] §3.5). Contesta la versión del chip de la tabla. En FR2355 y FR2476 lee su configuración del TLV y de 0xFF88, como el de TI | El volcado de la ROM de un chip real | Sin BSL: la entrada no hace nada |
| **G2553** | Un BSL propio (AGPLv3) del protocolo de 1xx/2xx, en la ROM | El volcado de la ROM de un chip real | Sin BSL |

Todos corren **en la CPU simulada**, así que los tiempos son los de una CPU, la
entrada por pines es la del chip y la UART o el USB son los periféricos
modelados. El B1 de las FRAM y el del G2553 usan el protocolo y las órdenes de
TI, así que las herramientas reales no notan la diferencia. La versión que
contestan es la del chip, con el byte de fabricante a 0x00, como el de TI.

**La entrada.** Lo que en el chip hace el código de arranque de la ROM va en
`soc/msp430/arranque.h`, de alto nivel, como el `arranque_fab` del P4:

* la secuencia de TEST y RST (§9.1);
* PUR en el F5529;
* el chip en blanco en FR23xx-FR26xx;
* la firma de 0xFF84;
* y la protección de la Z-Area.

**Desde el PC.** Los programas reales gobiernan TEST y RST con las líneas de
módem de un adaptador USB-serie: RTS a TEST y DTR a RST. El puente serie de
`mcu-sim` ya lleva DTR y RTS por RFC 2217: es lo que usa el auto-reset de
esptool en E5. Pieza nueva:

* **`bsl_adaptador.xml`**: un adaptador USB-serie con DTR y RTS cableados a RST
  y TEST, como la MSP-BSL «BSL Rocket» o un FTDI con dos hilos. En las LaunchPad
  de FRAM, el BSL de UART va por los pines del *backchannel*, pero la eZ-FET no
  mueve TEST y RST para el BSL. Por eso la placa con BSL es LaunchPad +
  adaptador, como en el laboratorio real.

**El USB BSL del F5529** necesita el USB del chip (M5) y que el host vea un
dispositivo HID. Hay dos vías:

* la del servidor USB/IP (U4 de `doc/analisis_puente_usb.md`), que da un HID de
  verdad a `python-msp430-tools` y a BSL-Scripter;
* o, mientras no exista, un cliente propio en el banco que habla HID con el
  controlador de anfitrión del puente (U2).

### 9.3 El BSL «de usuario» del G2xx

[MSPBSL] trae un BSL pequeño para la G2xx (`MSP430G2xxxBSL_CCS.asm`, BSD-3).
Vive en la memoria de información y no en la ROM, usa una UART por software a
9600 8N1 en P1.1/P1.2 y se habilita con P1.3, que es S2 en la LaunchPad. **Es
justo lo que la MSP-EXP430G2ET permite sin adaptador**, porque va por el
*backchannel*. Es un firmware de usuario más: no pide nada al modelo. Entra en
M9 como prueba de laboratorio realista.

### 9.4 Cómo se prueba

* `python-msp430-tools`:
  * `msp430.bsl.target` para el G2553;
  * `msp430.bsl5.uart` para todas las FRAM, y por I2C con un anfitrión I2C
    simulado en las FR2xx;
  * `msp430.bsl5.hid` para el F5529.

  Se programa, se verifica, se lee y se arranca blink.
* BSL-Scripter: los mismos guiones que en el laboratorio real, contra el puerto
  RFC 2217 del puente.
* Los casos de error, cada uno con su comprobación:
  * contraseña incorrecta → borrado masivo;
  * firma de BSL desactivado → no contesta;
  * FR2xx sin comunicación → LPM4 a los 10 s;
  * chip en blanco → BSL sin secuencia;
  * llamada a 0x1000 desde la aplicación → BSL con la RAM borrada en el rango de
    la tabla.
* **El F5529, contra la imagen de TI**: con B2, la misma batería tiene que dar
  las mismas respuestas byte a byte que con B1. Es la comprobación de que el
  *port* de MSPBSL es fiel.

---

## 10. Placas, dos fuentes y erratas

### 10.1 Las diez LaunchPad

Todas son el mismo diseño de TI, con dos lados. A la izquierda está la sonda,
una eZ-FET o eZ-FET lite: un MSP430 propio con USB, que da el depurador, el
*backchannel* UART y la alimentación de 3,3 V. Ese MSP430 es un **MSP430F5528**
en los esquemáticos que lo nombran en texto: la F5529LP, la FR2433, la FR4133,
la FR5969 y la LP-FR2476. En las demás se comprueba en su esquemático. Es lo que
hace posible la eZ-FET de bajo nivel (M11b) con el modelo del F5529. Entre la sonda y el objetivo hay
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

| Papel | Fuente 1 | Fuente 2 (y más) |
| :--- | :--- | :--- |
| ISA y ciclos | [UG2] cap. 3, [CPUX], [UG5] cap. 6, [UGFR5] cap. 4 | `msp430-elf-run` y `mspdebug sim` (semántica); las tablas de cada familia entre sí (ciclos) |
| Mapa, CPU, MPY, EEM y erratas de CPU | Hojas de datos y erratas | `devices.csv`; `targetdb`; `devicedb.h` de [MSPDS] |
| Módulos, direcciones base y vectores | Hojas de datos | Cabeceras de CCS; `targetdb` |
| Registros y campos | Guía de cada familia | Cabeceras de CCS; `targetdb` (direcciones, campos y valores con nombre) |
| Valores de reset | Guía de cada familia | DriverLib y las cabeceras, donde den algo; si no, se marcan con una sola fuente en la sección D de `todo.md`. **El `targetdb` no los trae** |
| Pines y funciones | Hoja de datos («Signal Descriptions» y diagramas de E/S) | Los `.h` de pines de DriverLib y las tablas de pines de Energia, cuando las haya; si no, dos tablas de la misma hoja de datos |
| JTAG y SBW | [JTAG] | Macros HAL de [MSPDS]; [REP]; [SBW-SL] |
| EEM | [UG2] cap. 28, [JTAG] | `EEM_defs.h` y `DebugManagerMSP430.cpp` de [MSPDS] |
| Comportamiento de la herramienta con la CPU parada | [MSPDS-DG] | [MSPDS] (`DebugManagerMSP430.cpp`, `devicedb.h`) |
| BSL | [BSL], [BSLFR], [BSLFRAM] | [MSPBSL] (fuentes); `python-msp430-tools`, BSL-Scripter |
| Placas | Esquemático de cada LaunchPad | Guía de usuario de la LaunchPad |

La regla de siempre: un registro tiene dos fuentes, o se declara (contesta, no
guarda y avisa una vez), y nunca se deja seguir al firmware sin decirlo.

**Licencias.** Todo lo que se toma de [MSPDS], [MSPBSL] y [REP] es BSD-3: se
puede portar y versionar con su aviso de copyright. Lo que es «TI Text File»
**no se versiona ni se lleva dentro**. Eso incluye las imágenes de firmware de
la eZ-FET, las imágenes de BSL publicadas y el código de EnergyTrace. Lo carga
quien lo tenga, desde su copia del paquete de TI, como el BSL de TI en el P4.

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
| **M0** | Integrar `esp32rv`; `periph/ti/`; generador de descriptores con las cuatro fuentes | Las diez | — | Medio |
| **M1** | Núcleo CPU/CPUX/CPUXV2 + laboratorio | `test430` | M0 | Grande |
| **M2** | TAP, SBW, JMB, JSTATE, EEM, protección; sonda de bancada (las macros HAL); *backend* de GDB, `--gdb-dap` y `--gdb`, en el laboratorio | `test430` | M1 | Grande |
| **M3** | G2553 + MSP-EXP430G2ET (+ SLAA754 con la MSP-EXP432P401R, si llega su código) | `testg2` | M2 | Medio |
| **M4** | F5529 sin USB + MSP-EXP430F5529LP; el Replicator430 en un F5529 programa un G2553 | `testf5` | M2, M3 | Grande |
| **M5** | F5529: USB | `testf5` | M4 | Medio |
| **M6** | FR2xx/FR4xx común: FR2433 + su LaunchPad | `testfr2` | M2 | Grande |
| **M7** | FR2311, FR2355, FR4133 y FR2476, con sus LaunchPad | `testfr2` | M6 | Grande, divisible por chip |
| **M8** | FR5xx/FR6xx: FR5969, después FR5994 y FR6989, con sus LaunchPad; Replicator430FR | `testfr5` | M2, M4 | Grande, divisible |
| **M9** | BSL de 2xx, de 5xx (USB de TI desde MSPBSL) y de FR5xx/FR6xx | `testg2`, `testf5`, `testfr5` + Python | M3, M5, M8 | Medio |
| **M10** | BSL de FR2xx/FR4xx (SLAU550AB) | `testfr2` + Python | M7, M9 | Pequeño |
| **M11** | eZ-FET emulada: alto nivel (M11a) y bajo nivel (M11b) | Python | M2, M3; U4 del puente USB (o `libmsp430` recompilada); M5 para M11b | Grande |
| **M12** | `mcu-sim-gui`: placas, dibujos y LCD | — | M3 | Pequeño |

```
M0 ──► M1 ──► M2 ──┬──► M3 ──► M4 ──► M5 ──┬──► M9 ──► M10
                   │           │           │      ▲
                   ├──► M6 ──► M7 ─────────┼──────┘
                   ├──► M8 ◄──(Replicator)─┘
                   └──► M11a (con M3) ──► M11b (con M5)
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
4. **`herramientas/gen_msp430.py`** con las cuatro fuentes de §5:
   * `devices.csv`;
   * las cabeceras;
   * el `targetdb`;
   * y `devicedb.h`, del que se extraen los XML del ZIP que lleva dentro.

   Tiene la tabla de normalización de nombres y el informe de discrepancias. Su
   salida se versiona; sus entradas no.
5. `ELF_MSP430` y TI-TXT en el cargador.

**Cierre:**
* las diez suites con el mismo invariante al picosegundo;
* el generador saca los diez descriptores sin discrepancias por resolver, y su
  mapa de memoria coincide con la tabla de §2.2.

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
   `tap_m430.h`, con todas las instrucciones de la tabla de §7.1. Incluye el
   avance por TCLK.
2. SBW, con su tope de tiempo, y la entrada en JTAG de 4 hilos y de 2 hilos.
3. El JMB, la «magic pattern» y el JSTATE.
4. El EEM con el mapa de `EEM_defs.h`: los tamaños XS, S, M y L, la reacción de
   parada y el control de relojes general (GENCLKCTRL) y por módulo
   (MODCLKCTRL0).
5. El fusible y las dos clases de firma, con el desbloqueo por JMB y por
   contraseña.
6. **La sonda de bancada** (`src/m430/sonda/`): el *port* de las macros HAL de
   [MSPDS] que usa la eZ-FET con CPU, X y Xv2, sobre un HIL de 4 hilos y otro
   de SBW en los pines. Con la atribución BSD-3 en cada fichero.
7. **El Replicator430 como oráculo** (`verif/m430/replicator/`): las funciones de
   [REP] compiladas **para el anfitrión**, en C para el PC, con su capa
   `LowLevelFunc` sobre los mismos pines simulados. Para cada operación (leer
   el ID, parar, leer y escribir memoria, borrar, desbloquear) se compara la
   traza de JTAG con la de la sonda de bancada.
8. `gdb_m430.h` sobre `GdbRspBase`; `--gdb-dap`; `herramienta_if` para MSP430
   con los valores de §8.2.

**Cierre:**
* el guion de depuración de siempre por `--gdb-dap` y por `--gdb`, en JTAG y en
  SBW: `load`, ruptura en `main` (por software con 0x4343 en RAM y por
  hardware), vigilancia, paso, Ctrl-C y `monitor reset halt`;
* el WDT no muerde con la CPU parada, y una UART sigue recibiendo (los
  `eemTimers` por omisión); con MODCLKCTRL0 a cero, el WDT sí muerde;
* el chip con la firma de bloqueo no deja entrar, y se recupera con el borrado
  por JMB;
* las trazas de la sonda de bancada y del Replicator coinciden en todas las
  operaciones comunes;
* los códigos de IR de [JTAG] y los invertidos de `JTAG_defs.h` coinciden bit a
  bit (una prueba de la tabla, sin simulación).

**Tamaño:** grande. Lo delicado es el avance por TCLK. Lo que antes era
escribir la sonda desde [JTAG] ahora es portar código probado, y eso lo hace
más corto y menos arriesgado que en la primera versión.

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
* erratas de SLAZ440K clasificadas;
* **si está `slaa754.zip`**: `msp_exp432p401r_sbw_g2et.xml`, la LaunchPad del
  MSP432 cableada a la G2ET (P6.0 → RST, P6.1 → TEST y masa), con el ejemplo de
  SLAA754 compilado tal cual.

**Cierre:**
* la demo de fábrica (temperatura por el *backchannel* y pulsador P1.3);
* `--gdb-dap` y `--gdb` por SBW sobre la placa;
* con SLAA754: el MSP432 programa el G2553 simulado, el G2553 arranca el
  programa nuevo y el *backchannel* del MSP432 da el mensaje de éxito de TI;
* las comprobaciones de `testg2` y su invariante.

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
* erratas de SLAZ314AD clasificadas;
* **el Replicator430 en el F5529**: `Replicator430` y `Replicator430Xv2`
  compilados con `msp430-elf-gcc` para un F5529, en lugar del F5437 del
  original. Es un cambio de la configuración de pines y del reloj en
  `LowLevelFunc*.h`. Una placa `replicator_f5529_g2553.xml` cablea el F5529 a
  un G2553 por JTAG de 4 hilos y por SBW.

**Cierre:**
* los ejemplos de DriverLib del F5529 (reloj a 25 MHz con VCORE 3, UART, ADC,
  DMA) y, por GDB, una ruptura por encima de 0x10000 (modelo grande);
* **un MSP430 simulado programa a otro con el código de referencia de TI**: el
  Replicator del F5529 borra, programa y verifica el G2553 (con la PSA) y lo
  arranca.

### M5 · F5529: USB

**Entregas:** el módulo USB (endpoints, búferes en la RAM de USB, PLL y
alimentación de USB) detrás de `usb_dev_if`; la pila USB de TI con CDC;
el puente USB enumera el F5529 como enumera el F303.

**Cierre:** el ejemplo CDC de la MSP430 USB Developers Package se ve como puerto
serie en el PC; el *backchannel* sigue funcionando a la vez.

**Además**, M5 es la base de dos cosas:
* el BSL USB (M9);
* y la eZ-FET de bajo nivel (M11b), que es un MSP430F5528 con su USB.

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

**Cierre:** la demo de fábrica del FR2433; LPM3.5 con el RTC y salida por BOR
con LOCKLPM5, con la depuración siguiéndolo por JSTATE; la protección de
escritura de la FRAM.

### M7 · FR2311, FR2355, FR4133 y FR2476

Un chip por subfase, en este orden, porque así cada uno añade poco:

1. **FR4133**: LCD_E y el vidrio FH-1138P (la demo de fábrica pinta en el LCD).
2. **FR2311**: Timer_B3, eCOMP con su DAC, SAC-L1 y TIA. Si está
   `slaa754.zip`, la segunda mitad de SLAA754: el MSP432 programa el FR2311 por
   SBW.
3. **FR2355**: SAC-L3 × 4, 2 × eCOMP, ICC, el ADC de 12 bits, MPY32 y MFM
   declarado. La ROM de DriverLib y FFT se declara; si hay firmware que la llame,
   se carga el binario de TI desde fichero, como B2.
4. **FR2476**: FRAM por encima de 0x10000, 4 × TA3 + TB7 y ADC de 12 bits.

**Cierre:** la demo de fábrica de cada LaunchPad y las erratas de cada chip.

### M8 · FR5xx/FR6xx: FR5969, FR5994 y FR6989

1. **FR5969**: lo común de la familia (CS, PMM con LPMx.5, FRAM con NWAITS y
   caché, MPU, IPE, REF_A, ADC12_B, Comp_E, RTC_B, AES256, DMA y MPY32) y
   `msp_exp430fr5969.xml`.
2. **FR5994**: 4 × eUSCI_A/B, CRC32, RTC_C y RAMCTL; LEA declarado; la
   microSD; FRAM hasta 0x43FFF.
3. **FR6989**: LCD_C con el FH-1138P y ESI declarado.

Y el **Replicator430FR** en el F5529 de M4, contra el FR5969.

**Cierre:**
* las demos de fábrica (la del FR6989 usa el LCD);
* la MPU con una violación que da su NMI;
* la IPE que no deja leer por JTAG ni por el depurador;
* un `load` en una FRAM con la MPU puesta;
* el Replicator430FR programa y desbloquea el FR5969 (con la contraseña de
  JTAG de 0xFF80).

### M9 · BSL de 2xx, 5xx y FR5xx/FR6xx

**Entregas:**
* **F5529**: el BSL USB de [MSPBSL] portado a `msp430-elf-gcc` (B1) y la carga
  de la imagen de TI (B2);
* **FR5xx/FR6xx**: el BSL propio con el intérprete y la interfaz
  `eUSCI_UART_I2C` de [MSPBSL] y una API de FRAM; la Z-Area; la versión de cada
  chip;
* **G2553**: el BSL propio de 1xx/2xx;
* B2 y B3 en todos;
* la entrada por TEST y RST, y por PUR;
* `bsl_adaptador.xml`;
* el BSL de usuario de la G2xx ([MSPBSL], §9.3) como firmware de prueba.

**Cierre:**
* `python-msp430-tools` programa, verifica y arranca blink:
  * en el G2553, con `bsl.target`;
  * en el FR5969, con `bsl5.uart`;
  * en el F5529, con `bsl5.hid` (por USB/IP si existe U4; si no, con el
    cliente del banco);
* el BSL de TI del F5529 (B1) y su imagen publicada (B2) dan las mismas
  respuestas a la misma batería;
* contraseña incorrecta → borrado masivo;
* BSL-Scripter con un guion de prácticas;
* el BSL de usuario de la G2xx carga un programa por el *backchannel* de la G2ET
  con S2 pulsado.

### M10 · BSL de FR2xx/FR4xx

**Entregas** (todo de [BSLFRAM]):
* BSL1 en 0x1000 y BSL2 en 0xFFC00 en FR2311, FR2433, FR2355 y FR2476; el BSL
  de 1 KB y solo UART a 9600 en el FR4133;
* la entrada por chip en blanco en FR23xx-FR26xx;
* el plazo de 10 s a LPM4 con el temporizador de cada chip;
* la configuración por TLV (etiqueta 0xB5) y de usuario (0xFF88, firma 0x695A)
  en FR2355 y FR2476, con su arranque de unos 300 ms;
* la interfaz I2C (0x48) y la automática.

**Cierre:**
* el de M9 en el FR2433 (UART), el FR2476 (I2C, con la configuración de
  usuario que deja solo I2C) y el FR4133 (UART a 9600; el cambio de baudios se
  rechaza);
* un FR2433 en blanco entra en el BSL sin secuencia;
* el BSL se duerme a los 10 s.

**Tamaño:** pequeño. El intérprete, la interfaz y la API son los de M9.

### M11 · La eZ-FET emulada

**Objetivo:** que las herramientas de TI vean la LaunchPad como la ven en el
laboratorio. Son:

* `libmsp430` (y con ella `mspdebug tilib`);
* el agente de GDB de TI;
* UniFlash y CCS.

**Lo que ya se sabe**, de [MSPDS]:

* **El transporte.** La eZ-FET es un CDC-ACM con VID 0x2047 y PID 0x0013 (la
  MSP-FET es 0x0014). En Linux, `libmsp430` la busca recorriendo
  `/sys/class/tty/ttyACM*` y leyendo su `modalias` y `bInterfaceNumber`
  (`UsbCdcIoChannel.cpp`). Un pty no le vale: el sistema tiene que ver un
  dispositivo USB.
* **El protocolo.** El V3 del firmware (`Bios/src/fw/fet/v3_0p.c`): mensajes que
  llaman a las macros HAL por su número, con sus parámetros, y respuestas y
  sondeos (`HalExecCommand`, `HalResponse`, `PollingManager`).
* **La comprobación de versiones.** Al conectar, `libmsp430` compara la versión y
  el CRC del núcleo, la HAL, el HIL, el canal y el DC-DC de la sonda con los de
  las imágenes que lleva compiladas. Por ejemplo, el HIL lleva la versión en
  0x18F6 y el CRC en 0x18FA. Si no coinciden, actualiza el firmware
  (`UpdateManagerFet::isUpdateRequired`). Las imágenes de la misma versión están
  en `Bios/include/eZ_Fet*.h`: de ahí salen los valores que hay que contestar.

**Entregas, en dos subfases:**

* **M11a, alto nivel.**
  * Un `EzFetV3` en C++ que habla el protocolo V3 por un CDC del puente USB, con
    el identificador de herramienta de la eZ-FET.
  * Contesta las versiones y los CRC leídos **de las imágenes de la copia de
    [MSPDS] del usuario**, sin versionarlas, con `--ezfet-imagenes DIR`.
  * Ejecuta cada macro HAL con la **sonda de bancada de M2**, que ya es el
    *port* de esas mismas macros.
  * Tiene el *backchannel* UART de la eZ-FET como segundo CDC del mismo
    dispositivo compuesto.
  * Rechaza limpiamente una actualización de firmware.
* **M11b, bajo nivel (opcional y de más fidelidad).**
  * La eZ-FET de la LaunchPad **como un MSP430F5528 simulado** (de la familia
    F552x de M4 y M5) que ejecuta **las imágenes de firmware de la eZ-FET**
    (núcleo, HAL, HIL y canal de comunicación), cargadas de la copia de [MSPDS]
    del usuario.
  * Va cableado al objetivo por SBW con los pines de la LaunchPad.
  * Es el equivalente de lo que el XDS110 es para la MSP-EXP432P401R
    (`doc/analisis_xds110.md`).
  * Ventaja: lo que haga es lo que hace la sonda, actualización de firmware
    incluida. Coste: dos MCUs en la simulación y un USB.

**El acceso desde el PC**, en este orden:

1. **Servidor USB/IP** (U4 de `doc/analisis_puente_usb.md`). Con `vhci-hcd`, el
   sistema ve un `ttyACM` de verdad con el `modalias` que busca `libmsp430`.
   Vale para Linux y, con usbip-win2, para Windows y CCS.
2. **Mientras U4 no exista**: `libmsp430` compilada de [MSPDS] (`make
   STATIC=1`) con un cambio pequeño en `UsbCdcIoChannel::createCdcPortList`.
   Acepta un puerto dado por una variable de entorno, un pty que el puente
   conecta al CDC. Solo para el CI. El cambio se versiona como parche, no como
   copia de [MSPDS].

**Cierre:**
* `libmsp430` (la de [MSPDS-DEV] por USB/IP, o la recompilada) abre la eZ-FET
  emulada sin pedir actualización;
* identifica el G2553 y el FR5969, los programa y los verifica;
* `mspdebug tilib` hace lo mismo;
* el agente de GDB de TI con `msp430-elf-gdb` hace `load`, ruptura y paso;
* con M11b, lo mismo con el firmware de TI ejecutándose en el F5528 simulado.

**Tamaño:** grande, pero ya sin incógnitas de fondo.

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
2. **Los valores de reset.** El `targetdb` da registros y campos, pero no los
   valores de reset. Siguen saliendo de las guías y, como segunda fuente, de
   DriverLib y de las cabeceras. Los que queden con una sola fuente van a la
   sección D de `todo.md`.
3. **El acceso de `libmsp430` a la sonda (M11).** Necesita un dispositivo USB
   visible para el sistema: USB/IP o una `libmsp430` recompilada. En macOS no
   hay USB/IP maduro, así que la eZ-FET emulada no estará en macOS hasta que lo
   haya.
4. **Las imágenes de firmware y de BSL** son de la licencia «TI Text File»: se
   usan desde la copia del usuario y no se versionan. En el CI solo se pueden
   usar si el paquete de TI se puede descargar allí. Si no, las pruebas de M11
   y de B2 quedan para hacerlas a mano.
5. **El *port* de IAR a `msp430-elf-gcc`** del BSL USB del F5529 y del
   Replicator. Lo que no es C (`.s43`, enlazado e intrínsecos de IAR) se reescribe.
   El riesgo es pequeño y está acotado a pocos ficheros.
6. **Las tablas de ciclos.** Cada guía publica la suya, pero hay casos límite
   (las instrucciones de 20 bits con repetición, la entrada a una interrupción
   con la CPU en LPM o los estados de espera de la FRAM con caché) que no están
   claros. Lo que no tenga dos fuentes va a la sección D de `todo.md`.
7. **El código de arranque de la ROM.** En las FR, la ROM de arranque comprueba
   firmas, entra en el BSL o no, y aplica la protección. Se modela de alto nivel
   con lo que dicen [BSLFRAM] y [JTAG]. Lo que haga y no esté documentado no
   está.
8. **Coste de simulación.** Por los pines y en SBW, cada acceso son cientos de
   flancos. Con M11b, son además dos MCUs y un USB. La sonda `remote_bitbang` ya
   lo resolvió con el ritmo real (`RitmoReal`) y la congelación; aquí se mide en
   M2 y en M11b.
9. **Lo que no está y hay que decirlo desde el primer día:**
   * EnergyTrace;
   * el analógico fino: el ruido del ADC, el *offset* de los SAC y la ganancia
     del TIA, más allá de lo que dé la hoja de datos;
   * LEA, ESI, MFM y CapTIvate: declarados;
   * el almacenamiento de estado del EEM L;
   * y el fusible con alta tensión real.

---

## 13. Fuentes

* Repositorio `mcu-sim`, ramas `msp430` y `esp32rv` (`9dba504`):
  * `soc/mcu_if.h`, `common/gdb_rsp.h`, `common/congela.h`,
    `soc/herramientas.h` y `common/imagen_fw.h`;
  * `periph/msp432p4/*` (en especial `bus16.h`, `eusci.h`, `timer_a.h` y
    `dio.h`), `soc/msp432p4/bsl_p4.h` y `placas/msp_exp432p401r.xml`;
  * `placas/dibuja_ti.py`, `src/verif/invariantes.txt`;
  * `doc/analisis_msp432.md`, `doc/analisis_esp32_riscv.md`,
    `doc/analisis_xds110.md` y `doc/analisis_puente_usb.md`.
* `doc/refs/msp430/` (tabla de referencias al principio).
* CCS 11.2:
  * `ccs_base/msp430`: `include/msp430*.h`, `include_gcc/devices.csv` (v1.213,
    15-10-2021) y los `.ld` y `.cmd` de cada chip;
  * `ccs_base/common/targetdb`: `devices/MSP430*.xml` (versión 1.213) y
    `Modules/msp430/*.xml`.
* MSP Debug Stack Open Source Package 3.15.1.1:
  * `Bios/src/hal/JTAG_defs.h`, `EEM_defs.h` y `macros/*`;
  * `Bios/src/hil/*`, `Bios/src/fw/fet/v3_0p.*` y `FetVersion.h`;
  * `Bios/include/eZ_Fet*.h`;
  * `DLL430_v3/src/TI/DLL430/`: `DebugManagerMSP430.cpp`, `IDebugManager.h`,
    `EM/SoftwareBreakpoints/SoftwareBreakpoints430.cpp`,
    `UpdateManagerFet.cpp`, `UsbCdcIoChannel.cpp` y `DeviceDb/devicedb.h` (sus
    XML: `defaults.xml`, `F2xx.xml`, `FR69xx.xml` y otros);
  * el manifiesto y `revisions.txt`.
* MSP430 BSL 1.02.00.01: `readme.txt`, el manifiesto, `CCS_BSL_Source/*` e
  `IAR_BSL_Source/*`.
* Replicator430 (SLAU320, paquete 1.22): `Readme.txt` y los fuentes de
  `Replicator430`, `430X`, `430Xv2` y `430FR`.
* `mspdebug` 0.22 (Debian), `mspdebug --help`: lista de controladores.
  Consultado el 09-11-2026.
