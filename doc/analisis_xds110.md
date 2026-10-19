# El XDS110 y el XDS110-ET de las LaunchPad MSP432

**Análisis de coste para `mcu-sim`** · 19-10-2026

> **Estado (2026-10-19): hechas X1 y X2.** La pieza **`Xds110`**
> (`parts/xds110.h`) está en las dos LaunchPad, del lado de la sonda de J101:
> su LDO alimenta el chip por el puente 3V3 (sin él, el chip no arranca),
> `--openocd` y `--cmsis-dap` van por sus pines TMS/TCK/RST (sin el puente de
> TMS, OpenOCD no encuentra el DAP), y la traza SWO -de PJ.5 en el P401R, de
> PC3 en el E401Y- sale con `swo=` o `--swo` a memoria, TCP o RFC 2217, como
> texto del ITM o en crudo. La sonda **CMSIS-DAP por TCP**
> (`verif/sonda_cmsis_dap.h`, `--cmsis-dap`, puerto 4441) atiende al
> `cmsis-dap tcp` de OpenOCD en cualquier chip: programa el P401R en 1,0 s
> frente a los 4-6 s de `remote_bitbang`, y el F407 en 1,6 s frente a 16 s.
> Lo prueban `make xds110` (15) y `make cmsis-dap` (25), en el CI de Linux.
> §5 dice lo que se hizo y lo que quedó distinto del plan; §8, cómo se usa
> desde CCS 11 y CCS 20.

La pregunta: cuánto cuesta que `mcu-sim` ofrezca lo que da la sonda que va
soldada en las LaunchPad de la familia MSP432 —el **XDS110-ET** de la
MSP-EXP432P401R (y de la P401Y) y el **XDS110** de la MSP-EXP432E401Y—, qué
partes merecen la pena y en qué orden hacerlas.

Lo que el alumno recibe de esa sonda, con una LaunchPad de verdad, son cinco
cosas distintas que conviene no mezclar:

1. **Programar y depurar** desde CCS, UniFlash/DSLite u OpenOCD, por USB.
2. **El *backchannel***: un puerto COM («XDS110 Class Application/User UART»)
   unido a la eUSCI_A0 del chip por J101.
3. **La alimentación**: el LDO del XDS110 da los 3,3 V del lado del objetivo a
   través del puente 3V3 de J101 (hasta 500 mA; 75 mA con EnergyTrace).
4. **EnergyTrace+**, solo en el -ET: la corriente que entra en el lado del
   objetivo, en el tiempo, con los estados de la CPU y la energía por función
   en CCS.
5. **Otras configuraciones de la sonda**: CMSIS-DAP 2.0, el puerto auxiliar
   (SWO) y la actualización de su firmware.

---

## 0. Resumen

* **Lo más visible ya está hecho.** El *backchannel* es un `PuenteSerie`
  (`BCL`, RFC 2217 en el 3356) colgado de J101, y OpenOCD programa y depura el
  P401R por los pines con `remote_bitbang` (T5 de
  `doc/analisis_msp432.md`). Lo que falta es que **la sonda sea una pieza de
  la placa**: hoy J101 une pines del chip con nada, y el chip se alimenta
  solo. Barato, y es lo que hace que quitar un puente de J101 tenga el efecto
  que tiene en la placa (§3.1, fase X1).
* **Hay una vía nueva y barata para una sonda «de verdad»: CMSIS-DAP por
  TCP.** El OpenOCD de git que ya usa el CI (2075427) trae, desde septiembre
  de 2025, el *backend* `cmsis-dap tcp` (`src/jtag/drivers/cmsis_dap_tcp.c`),
  y el que construye `ci/construye_openocd.sh` lo lleva compilado (probado:
  intenta conectar a `127.0.0.1:4441`):
  los mismos paquetes CMSIS-DAP que irían por USB, con una cabecera de 8
  bytes, por un socket. **Sin USB/IP, sin drivers, en las tres
  plataformas.** Cambia la cuenta de la vía D5 del análisis del MSP432, que
  la daba por cara porque dependía del puente USB. Y es mucho más rápida que
  `remote_bitbang`: una transferencia del DAP por mensaje, no un bit por
  carácter (§3.2, fase X2).
* **EnergyTrace es barato en el modelo y caro en CCS.** El P4 ya calcula su
  consumo (`idd_modelo()`) y lo pide por DVCC como una carga más del nodo, y
  los LED y la BoosterPack consumen por la red analógica. Medir la corriente
  del raíl de 3,3 V, integrarla y sacarla como fichero y en la ventana es poco
  código (§4, fases X3 y X4). **Llevarla al EnergyTrace de CCS exige el
  protocolo propietario de la sonda**, que no está publicado (§3.4).
* **El protocolo nativo del XDS110 tiene dos niveles de coste muy
  distintos.** El subconjunto que usa OpenOCD está escrito por TI en el
  propio controlador `xds110` de OpenOCD (GPL-2.0): unas veinte órdenes con
  un marco trivial. Lo que usan **CCS, UniFlash y DSLite** es mucho más
  —versiones, actualización de firmware, EnergyTrace, órdenes de la pila de
  emulación de TI— y **no está documentado**: habría que deducirlo de
  capturas USB con una sonda real. Y las dos cosas exigen **USB/IP**, que en
  macOS no existe en la práctica (§3.3, §3.4).
* **Recomendación (§6):** X1 (la sonda en la placa), X2 (CMSIS-DAP por TCP),
  X3 (EnergyTrace en el modelo) y X4 (su ventana). Con eso el alumno tiene la
  experiencia del XDS110-ET —J101, *backchannel*, una sonda rápida para
  OpenOCD, VS Code y el GDB de CCS, y la curva de energía— en Linux, Windows y
  macOS sin instalar nada. **X5 y X6 (USB/IP), solo si un laboratorio pide
  pyOCD, Keil, IAR o el controlador `xds110` de OpenOCD. X7 (CCS nativo y su
  EnergyTrace), no.**
* **Mientras tanto, CCS puede depurar con OpenOCD** (§8): *GDB Hardware
  Debugging* en CCS 11 y la extensión Cortex-Debug en CCS 20, contra el
  `mcu-sim` de hoy o contra una LaunchPad por su XDS110. Los `.cfg` y las
  configuraciones están en `src/ide/msp432/`, y la sesión de GDB con OpenOCD
  está probada contra el P401R y el E401Y simulados.

| Fase | Qué da | Código nuevo (aprox.) | Coste | Plataformas | Riesgo |
| :--- | :--- | ---: | :--- | :--- | :--- |
| X0 | Capturas y descriptores de una sonda real | 0 | Bajo, pero necesita la placa | — | Bajo |
| **X1** | La sonda en la placa: J101 con sentido, LDO, SWD y SRST — **hecha** | ~400 | **Bajo** | Las tres | Bajo |
| **X2** | CMSIS-DAP por TCP para OpenOCD — **hecha** | ~900 | **Medio** | Las tres | Bajo |
| **X3** | EnergyTrace en el modelo: fichero, observables, energía por función | ~500 | **Bajo-medio** | Las tres | Medio: la fidelidad del consumo |
| **X4** | La ventana de EnergyTrace en `mcu-sim-gui` | ~700 | **Medio** | Las tres | Bajo |
| X5 | CMSIS-DAP 2.0 por USB/IP (`1cbe:02a5`) | ~1 500 | Medio-alto | Linux, Windows | Medio |
| X6 | El XDS110 «de OpenOCD» por USB/IP (`0451:bef3`) | ~600 sobre X5 | Medio | Linux, Windows | Medio |
| X7 | CCS, UniFlash y DSLite nativos; EnergyTrace en CCS | Abierto | **Alto** | Linux, Windows | **Alto**: protocolo cerrado |

Las líneas son una estimación por comparación con piezas parecidas que ya
existen: `verif/sonda_openocd.h` (305), `verif/swd_port.h` (325),
`parts/puente_usb.h` (443), `parts/puente_serie.h` (514).

---

## 1. Qué es el XDS110

* **Un microcontrolador TM4C1294NCPDT con el firmware de TI**, que hace de
  sonda JTAG/SWD (y cJTAG) con un *backchannel* UART, cuatro GPIO y, en las
  variantes -ET, la medida de EnergyTrace. Es el mismo en la sonda suelta
  (TMDSEMU110-U), en las LaunchPad SimpleLink (MSP432, CC13xx/26xx, CC32xx)
  y en algunas Hercules. El *hardware* de la -ET de la LaunchPad está en sus
  esquemas («*an open-source onboard debug probe*», SLAU597F §1); **el
  firmware, no**: es un binario del *emupack* de TI.
* **En la MSP-EXP432P401R es el XDS110-ET** (SLAU597F §2.3): la sonda, el
  *backchannel* y EnergyTrace+, aislados del objetivo por **J101**, un bloque
  2x10 de puentes: GND, 5V, 3V3, RXD, TXD, RST, TMS, TCK, TDO y TDI. Nada más
  cruza de un dominio al otro. **La P401Y** lleva la misma sonda. **La
  MSP-EXP432E401Y lleva un XDS110 sin EnergyTrace**: lo que hay de la sonda en
  esa placa es el *backchannel* por JP4/JP5 y J101, ya modelado.
* **Configuraciones USB** (commit `d35c44c` de OpenOCD, de TI):

  | VID:PID | Configuración | Interfaz de depuración |
  | :--- | :--- | :--- |
  | `0451:bef3` | Estándar (la de siempre) | Interfaz 2, *bulk* EP2 OUT / EP3 IN |
  | `0451:bef4` | *Drag-n-Drop* | Interfaz 2, igual |
  | `1cbe:02a5` | **CMSIS-DAP 2.0 por *bulk*** | Interfaz 0, EP1 |
  | `1cbe:00ff` | DFU (actualización del firmware) | — |

  Además, en la estándar, dos puertos CDC: la **Application/User UART**
  (el *backchannel*) y la **Auxiliary Data Port**. Qué viaja exactamente por
  la segunda (SWO en formato UART, según la documentación de la sonda; ¿y
  EnergyTrace?) hay que confirmarlo con una captura (X0).
* **EnergyTrace** (página de TI y wiki del MSP432): mide **contando los
  pulsos de un convertidor DC-DC** controlado por software, no con una
  resistencia y un ADC, así que no pierde picos cortos. Hasta 75 mA:
  ±2 % ±500 nA por debajo de 25 mA y ±5 % ±500 nA hasta 75 mA. La energía y
  la potencia se calculan a 3,3 V fijos. Los datos salen por un ***endpoint
  bulk*** de la sonda. En la LaunchPad mide **todo lo que entra en el lado del
  objetivo**: el chip, los LED y la BoosterPack (SLAU597F §2.3.5). Con el MSP432
  hay **EnergyTrace+** (los estados de la CPU) pero **no EnergyTrace++** (el
  estado de los periféricos). La frecuencia de muestreo no está publicada.

---

## 2. Qué hay ya en `mcu-sim`

| Pieza | Dónde | Qué cubre del XDS110 |
| :--- | :--- | :--- |
| `PuenteSerie` `BCL` | `placas/msp_exp432p401r.xml`, J101.8/J101.10 | **El *backchannel***, con RFC 2217, baudios del terminal, DTR/RTS (`doc/puente_serie.md`) |
| J101 como `Jumper` 2x10 | Las dos LaunchPad | Los puentes, **sin nada al otro lado**: los pines pares no van a ninguna parte |
| `SondaOpenOcd` | `verif/sonda_openocd.h`, `depuracion="openocd"` | Una sonda SWD bit a bit para OpenOCD (`remote_bitbang`), colgada **de los nodos SWCLK/SWDIO del chip**, no de J101 |
| `SwdPort` | `verif/swd_port.h` | **Un maestro SWD** con lo que exige ADIv5: encendido del DP, WAIT, FAULT/ABORT, lecturas aplazadas. Lo usa el stub de pines |
| Stub de GDB por pines y por DAP | `depuracion="pines"` / `"dap"` | La depuración sin sonda externa |
| ITM, TPIU, SWO, `SwoReceiver` | `core/debug.h`, `parts/netlist_parts.h` | La traza SWO en NRZ y su receptor con el desempaquetado ITM |
| Consumo del P4 | `Msp432P4::idd_modelo()` / `idd_medida()` | Cifras típicas por modo (activo por MHz y por modo de núcleo, LPM0, LPM3, LPM4, LPMx.5) cargadas **en el nodo DVCC** como una carga más |
| `Fuente` | `parts/ext_parts.h` | Una fuente con límite de corriente y observable `corriente` (mA) |
| Puente USB, U2 | `parts/puente_usb.h` | El MCU como dispositivo USB enumerado por el puente. **No hay servidor USB/IP (U1)** |

Lo que **no** hay: un dominio de la sonda en la placa. El adaptador del chip
registra sus propios *drivers* de alimentación (`top/sim_main.cpp`), así que
el P401R arranca aunque J101 esté abierto, y la sonda de OpenOCD se cuelga de
SWCLK/SWDIO por dentro, sin pasar por TMS/TCK de J101.

---

## 3. Coste, capacidad por capacidad

### 3.1 La sonda como pieza de la placa (X1)

Una pieza **`Xds110`** que es el lado de la sonda de J101:

* **El LDO de 3,3 V**: una `Fuente` de 3,3 V con 500 mA de límite —75 mA con
  EnergyTrace activo, como dice SLAU597F §2.4.1— en el pin 3V3 de J101.
  Cuando hay una fuente así en el nodo de alimentación del chip, el adaptador
  no pone la suya. **Quitar el puente 3V3 apaga el objetivo**, como en la
  placa.
* **El *backchannel***: el `PuenteSerie` de hoy, dentro de la pieza (o al
  lado, como ahora; lo importante es que va a RXD/TXD de la sonda).
* **SWD y reset**: la sonda gobierna TMS(SWDIO), TCK(SWCLK) y RST **del lado
  de la sonda de J101**. Quitar los puentes TMS/TCK deja a OpenOCD sin
  objetivo; quitar RST deja sin SRST.
* `et="si|no"`: el -ET de la P401R/P401Y o el XDS110 de la E401Y.
* `depuracion=` del `<mcu>` sigue existiendo para quien no quiera sonda, y
  `depuracion="openocd"` puede colgarse de la pieza en vez de los nodos del
  chip.

**Coste bajo**, unas 400 líneas: casi todo es recolocar lo que ya hay. Lo más
delicado es el orden de la alimentación (que la `Fuente` de la sonda sustituya
a la del adaptador sin mover ningún invariante, que dependen de placas que no
la llevan). **Pruebas**: con J101 entero, OpenOCD programa como hoy; sin el
puente TMS, no encuentra el DAP; sin 3V3, el chip no arranca y DVCC está a
0 V; el *backchannel* sigue pasando `make p4-edumkii`.

### 3.2 CMSIS-DAP por TCP (X2)

El *backend* `tcp` de OpenOCD (`cmsis_dap_tcp.c`, en master desde el
1-9-2025):

* Cada orden y cada respuesta CMSIS-DAP llevan una **cabecera de 8 bytes**:
  la firma `"DAP\0"` (`0x00504144`), la longitud (16 bits), el tipo
  (1 petición, 2 respuesta) y un byte reservado; todo en *little endian*.
  Puerto por omisión **4441**. Paquetes de hasta 1 024 bytes.
* **Sin UART ni SWO** por este *backend* (lo dice su cabecera): el
  *backchannel* sigue yendo por el `PuenteSerie`.
* Se usa con `adapter driver cmsis-dap`, `cmsis-dap backend tcp`,
  `cmsis-dap tcp host localhost` y `cmsis-dap tcp port 4441`. **La 0.12.0 no
  lo tiene**: es la misma condición que ya pone `remote_bitbang` con SWD, y el
  mismo OpenOCD fijado de `ci/construye_openocd.sh` sirve.

Lo que hay que escribir es **el procesador de órdenes CMSIS-DAP** sobre el
maestro SWD de `SwdPort`, conectado a los pines de la sonda (X1):

| Órdenes | Qué hacen | Nota |
| :--- | :--- | :--- |
| `DAP_Info`, `DAP_HostStatus`, `DAP_Connect`, `DAP_Disconnect`, `DAP_Delay`, `DAP_ResetTarget` | Identidad, LED, conexión | `DAP_Info` dice el tamaño de paquete y las capacidades (SWD sí, JTAG no, SWO no) |
| `DAP_SWJ_Pins`, `DAP_SWJ_Clock`, `DAP_SWJ_Sequence`, `DAP_SWD_Configure`, `DAP_SWD_Sequence` | Los hilos: *line reset*, JTAG→SWD, SRST | Bit a bit sobre `SwdPort::ciclo()` |
| `DAP_TransferConfigure`, **`DAP_Transfer`**, **`DAP_TransferBlock`**, `DAP_TransferAbort`, `DAP_WriteABORT` | Los accesos al DP/AP | **El grueso**: reintentos de WAIT con el límite configurado, lecturas aplazadas, comparación de valor (`match`), códigos de respuesta como los de la especificación |
| `DAP_ExecuteCommands`, `DAP_QueueCommands` | Varias órdenes en un paquete | Las usa OpenOCD para ir deprisa |

**El tiempo** mejora mucho respecto a `remote_bitbang`: allí cada bit leído
era un viaje por el socket y hubo que hacer avanzar el tiempo simulado con el
reloj de pared para que los algoritmos de Flash acabaran
(`verif/sonda_openocd.h`). Aquí un paquete trae decenas de transferencias, y
la sonda las ejecuta en tiempo simulado a la velocidad de SWCLK que pida
`DAP_SWJ_Clock`. El avance en los huecos sigue haciendo falta (OpenOCD sondea
DHCSR en tiempo de pared), pero los huecos son muchos menos.

**Coste medio**, unas 900 líneas, y **riesgo bajo**: la especificación de
CMSIS-DAP es pública (ARM, Apache-2.0) y el cliente es código de otros.
Sirve igual para los STM32: es una sonda genérica. **Pruebas**: un
`verif/openocd/cmsis_dap.py` con los mismos casos que `programa.py` (programar
y verificar el P401R con `target/ti/msp432.cfg`, el F407 con
`target/stm32f4x.cfg`, los hilos de FreeRTOS en GDB) y una prueba del
procesador de órdenes con paquetes escritos a mano (WAIT, FAULT, `match`,
bloques que cruzan 1 KiB). `make cmsis-dap` en el CI de Linux.

Con esto, la vía D5 del análisis del MSP432 queda **para OpenOCD sin
USB/IP**. Lo que no cubre: **pyOCD, Keil, IAR y probe-rs no tienen un
*backend* TCP** de CMSIS-DAP; para ellos sigue haciendo falta X5.

### 3.3 La sonda por USB: USB/IP (X5 y X6)

Para que una herramienta que solo sabe hablar USB vea una sonda, el sistema
tiene que tener un dispositivo USB: **USB/IP**, con `mcu-sim` como
**servidor**. Es la U1 de `doc/analisis_puente_usb.md` con una diferencia que
la abarata: **el dispositivo no es el OTG simulado, es la sonda**, escrita en
C++ por el propio puente. No hay que hacer de controlador de anfitrión ni
sincronizar con tramas simuladas: cada `USBIP_CMD_SUBMIT` se contesta con lo
que diga la sonda.

* **X5, CMSIS-DAP 2.0 (`1cbe:02a5`)**: los descriptores de esa configuración
  y el procesador de X2 detrás del *endpoint bulk*. Lo usan **pyOCD, OpenOCD
  (`cmsis-dap` por USB), Keil MDK, IAR y probe-rs**. Unas 1 500 líneas: el
  servidor USB/IP (lista de dispositivos, `IMPORT`, `SUBMIT`, `UNLINK`), los
  descriptores, el control estándar (`GET_DESCRIPTOR`, `SET_CONFIGURATION`,
  las cadenas, el descriptor BOS/MS OS 2.0 que Windows necesita para cargar
  WinUSB sin INF) y el *bulk*.
* **X6, el XDS110 de OpenOCD (`0451:bef3`)**: los descriptores de la
  configuración estándar (interfaz 2, EP2/EP3) y **el subconjunto del
  controlador `xds110`**. Su marco es trivial —`'*'`, la longitud en 16 bits y
  la carga; la respuesta empieza con un código de error de 4 bytes— y las
  órdenes son las que define el controlador: `XDS_CONNECT`, `XDS_VERSION`,
  `XDS_SET_TCK`, `XDS_SET_SRST`, `SWD_CONNECT`, `CMAPI_*` (lectura y escritura
  de registros del DAP), `OCD_DAP_REQUEST` (un bloque de peticiones al DAP) y,
  para JTAG, `XDS_JTAG_SCAN`, `OCD_SCAN_REQUEST` y `OCD_PATHMOVE`. La versión
  que se anuncie tiene que ser ≥ 3.0.0.3 para que OpenOCD use los bloques y
  todas las frecuencias. Unas 600 líneas sobre X5.
* **Plataformas**: Linux (`vhci-hcd`, en el núcleo; `usbip attach` como
  `root`) y Windows (usbip-win2, con drivers firmados). **macOS, no**, por las
  razones de `doc/analisis_puente_usb.md` §5.1.
* **El CI**: necesita cargar `vhci-hcd` en el *runner*. En los de Ubuntu de
  GitHub hay `sudo`, pero el módulo puede no venir en el núcleo de Azure: hay
  que comprobarlo antes de prometer la prueba.

**Coste medio-alto** las dos juntas. Lo que dan sobre X2 es **otras
herramientas**, no otro comportamiento del modelo. Solo si un laboratorio las
usa.

### 3.4 CCS, UniFlash, DSLite y EnergyTrace en CCS (X7)

Es lo que más se parecería a la placa para quien no usa GDB, y **lo más caro
con diferencia**:

* **El protocolo no está publicado.** La pila de emulación de TI (la que
  usan CCS, UniFlash, DSLite, `dbgjtag`, `xds110reset` y `stune`) usa muchas
  más órdenes que las del controlador de OpenOCD: versiones de *hardware* y
  firmware, el control de la alimentación, la configuración de la sonda,
  EnergyTrace por su *endpoint bulk*… Habría que **deducirlas de capturas
  USB** (usbmon o USBPcap con Wireshark) de sesiones con una sonda real: cada
  herramienta, cada versión. Y mantenerlas: una versión nueva de CCS puede
  pedir algo distinto.
* **El firmware se actualiza solo.** CCS, UniFlash y SmartRF comparan la
  versión del firmware con la que traen y, si es más vieja, ponen la sonda en
  DFU (`1cbe:00ff`) y la reprograman. La sonda simulada tiene que anunciar la
  versión justa —ni más vieja, ni tan nueva que algún programa no la
  reconozca— y nunca entrar en DFU.
* **EnergyTrace en CCS** necesita además el formato de los datos de ese
  *endpoint*, también sin publicar.
* **La licencia.** El firmware y la pila de TI van con su licencia del
  *emupack*. Deducir un protocolo para la interoperabilidad tiene su amparo en
  la UE (Directiva 2009/24/CE, art. 6), pero conviene leer esa licencia antes
  de empezar, y no reutilizar nada que venga de los binarios de TI. **No es
  asesoramiento jurídico.**
* **Plataformas**: las de USB/IP, sin macOS.

**Coste alto y abierto**, con un riesgo de mantenimiento que no tiene nada
de lo anterior. El camino de CCS que ya funciona —*GDB Hardware Debugging*
contra el stub o contra OpenOCD— cubre la depuración. **No se recomienda**
salvo que un laboratorio lo necesite y preste la sonda para las capturas.

### 3.5 La traza SWO

> **Hecho con X1**: `swo=` en la pieza o `--swo` (§5, X1).

El modelo ya genera SWO (TPIU en NRZ) y lo recibe (`SwoReceiver`). Lo que
hace el XDS110 —llevarlo al ordenador por su puerto auxiliar— se puede ofrecer
**sacando el `SwoReceiver` por un canal del puente serie** (TCP en crudo o
desempaquetado ITM por puerto de estímulo). No pasa por CMSIS-DAP por TCP,
que no lleva SWO. Es poco código (≈150 líneas) y se puede hacer en X1 o
cuando se pida.

---

## 4. EnergyTrace en el modelo (X3 y X4)

### 4.1 Qué se mide

**Lo mismo que en la placa: la corriente que entrega el LDO de la sonda al
lado del objetivo**, es decir, la corriente de la `Fuente` de 3,3 V de la
pieza `Xds110` (X1). Eso incluye, sin hacer nada más:

* el chip: `idd_modelo()` cargado en DVCC;
* los LED, los pulsadores con su resistencia de pull, la BoosterPack —la
  EDUMKII entera— y lo que el alumno cuelgue, porque todo consume por la red
  analógica.

Con **el límite de 75 mA** mientras EnergyTrace está activo: la `Fuente` ya
sabe limitar y avisar de la sobrecorriente.

### 4.2 Lo que se ofrece

| Pieza | Qué | Coste |
| :--- | :--- | :--- |
| **Muestreo** | Corriente media en cada periodo (por omisión 1 ms; configurable), energía acumulada a 3,3 V como hace TI | Integrar en tiempo simulado: exacto, sin el ruido ni el ±2 % del DC-DC |
| **Fichero** | `--energytrace FICHERO[,periodo]`: CSV con tiempo, corriente, tensión y energía —las columnas de `energytrace-util`— y el estado de la CPU | Bajo |
| **Observables** | `corriente`, `potencia`, `energia` y `estado` en el catálogo, para la ventana y para cualquier cliente del protocolo | Bajo |
| **EnergyTrace+ (estados)** | Activo, LPM0, LPM3, LPM4, LPMx.5 y reset, de `modo_lpm()` | Trivial |
| **Energía por función** | Al acabar, una tabla: cada función del ELF con su tiempo y su energía. CCS lo hace muestreando el PC (y no puede en *Free Run*); **el modelo lo puede hacer exacto**, instrucción a instrucción, con los símbolos del ELF que ya se cargan | Medio: atribuir la energía de cada intervalo al PC que corría |
| **La ventana (X4)** | En `mcu-sim-gui`: la curva de potencia, la energía acumulada, la franja de estados y la tabla por función; guardar y cargar una medida para comparar, como hace CCS | Medio: la ventana no tiene hoy ninguna gráfica en el tiempo |

### 4.3 El límite de lo que vale

**Medir es exacto; lo medido es tan bueno como el modelo de consumo**, y el
del P4 es de cifras típicas por modo: ni la corriente de cada periférico, ni
las transiciones, ni la temperatura (lo dice el propio `limitaciones()` del
chip). Para lo que se enseña con EnergyTrace —que LPM3 gasta mil veces menos
que el modo activo, cuánto se lleva un LED, qué ahorra bajar el reloj o
pasar al DC-DC— sobra. Para comparar con una placa al microamperio, no: hay
que decirlo en la documentación y, si se quiere afinar, **medir una placa con
EnergyTrace de verdad** (X0) y ajustar las cifras de `idd_modelo()`, que
entonces sí dirían de dónde vienen.

Lo mismo valdría para un STM32 (el F407 tiene su modelo IDD de la fase F7),
pero allí no hay XDS110: sería «un EnergyTrace» de `mcu-sim`, no el de la
placa.

---

## 5. Plan por fases

### X0 · Las referencias, con una placa de verdad

**Qué:** con una MSP-EXP432P401R y una MSP-EXP432E401Y:

* `lsusb -v` de las dos sondas (descriptores completos de `0451:bef3` y, con
  `xdsdfu`, de `1cbe:02a5`), y la versión del firmware (`xds110 info` de
  OpenOCD);
* capturas USB (usbmon o USBPcap) de: una sesión de OpenOCD con el
  controlador `xds110`; una de OpenOCD con `cmsis-dap` en la configuración
  CMSIS-DAP; y, solo si se piensa en X7, CCS conectando, cargando y
  ejecutando, UniFlash programando y una sesión de EnergyTrace;
* **la frecuencia de muestreo real de EnergyTrace** y una medida de
  referencia (activo a 3, 12 y 48 MHz, LPM0, LPM3 y LPM4.5) para contrastar
  `idd_modelo()`.

**Coste:** sin código; una tarde con la placa. Las capturas son datos
propios: se pueden versionar en `doc/refs/` si no contienen firmware de TI.
**Hecho cuando:** los descriptores y las cifras están en un anexo de este
documento.

### X1 · La sonda en la placa

**Qué:** la pieza `Xds110` (§3.1) con `et="si|no"`; las dos LaunchPad la
declaran en el lado de la sonda de J101; el adaptador no alimenta el chip
cuando hay una fuente en su nodo; `depuracion="openocd"` (y X2) se cuelgan de
la sonda; el *backchannel* pasa a ser parte de ella. Opcional: el SWO por un
canal del puente serie (§3.5).

**Pruebas:** J101 entero, todo como hoy (`make openocd`, `p4-edumkii`,
`e4-edumkii`, `bsl-p4`); sin TMS/TCK, OpenOCD no encuentra el DAP; sin 3V3, el
chip no arranca; sin RXD/TXD, el *backchannel* calla. El dibujo de las dos
placas con la sonda como pieza visible. **Ningún invariante se mueve.**

**Hecha (2026-10-19).** Lo que hay, y lo que quedó distinto del plan:

* **`parts/xds110.h`**: la pieza, con las patillas `3v3`, `rst`, `swdio`,
  `swclk` y `swo` y los atributos `et`, `v`, `limite_ma` (500), `sonda`,
  `puerto`, `swo`, `swo_bitrate` (1 Mbit/s), `swo_formato` (`itm` o `crudo`)
  y `swo_puerto`. El LDO es una `Fuente` por dentro. Observables:
  `corriente` (mA), `sobrecorriente` (alarma) y `depurando` (hay un OpenOCD
  conectado). `doc/parts.md` la describe.
* **El *backchannel* sigue fuera**, como `PuenteSerie`: ya funcionaba y no
  gana nada por estar dentro.
* **La alimentación**: `mcu_if::alimentacion_externa()`. Con un XDS110 con LDO
  en su placa, el adaptador del P4 deja DVCC (y el del E4, VDD) a la placa y
  solo pone las masas y la analógica, que las placas no unen al 3V3. Sin el
  puente 3V3 de J101 el chip se queda sin alimentar.
* **La corriente del LDO se mide sin el umbral del nodo**
  (`AnalogNet::corriente_exacta`): con 0,1 ohm, el 0,1 mV con el que el nodo
  decide si ha cambiado son 1 mA, y el P401R a 3 MHz pide 0,34. Hace falta
  para X3. **El E4 no tiene modelo de consumo**: su LDO entrega 0 mA.
* **Las corrientes de los pines no pasan por el LDO**: los LED los alimentan
  los pads del modelo, no DVCC. Para X3 habrá que sumarlas (lo que en los
  STM32 hace `PowerPads`).
* **El límite de 75 mA con EnergyTrace no se aplica**: `et="si"` solo se dice.
  Es de X3.
* **TDO/SWO en PJ.5**, su función primaria, que el P4 no tenía. El modelo deja
  PJ.5 como E/S al salir del reset: el firmware que quiera la traza pone
  `PJSEL0.5` (en la placa no hace daño; si sale del reset como TDO, falta
  confirmarlo en X0). En el E4, PC3 ya era TDO/SWO.
* **La traza**: un receptor UART 8N1 y el desempaquetado del ITM; a memoria,
  TCP, RFC 2217 o sus clientes, como un puente serie. `--swo` y
  `--swo-bitrate` la piden sin tocar el XML, y `--espera-terminal` también
  espera a su cliente. `p4_swo` y `e4_swo` son los firmwares de prueba.
* **`--openocd` y `--cmsis-dap` van por la sonda** si la placa del chip lleva
  un `Xds110` (en un sistema, la de su misma placa); si no, por los pines del
  chip, como antes.
* **La validación eléctrica** reconoce un pad por su nodo y no solo por su
  nombre: en un sistema, la patilla `swdio` de la sonda ve el nodo como
  `SWDIOTMS`, y el pad se llama `L/u0.SWDIOTMS`.
* **Pruebas**: `verif/openocd/xds110.py` y `make xds110` (15 comprobaciones,
  en el CI de Linux); `gui-sistema` cuenta las piezas nuevas, y
  `prueba_cruzada` de `mcu-sim-gui` comprueba la pieza y sus observables. No
  se probó el SRST quitando el puente de RST.

### X2 · CMSIS-DAP por TCP

**Qué:** el procesador CMSIS-DAP (§3.2) sobre `SwdPort`, en la sonda de X1 o
suelto en los nodos de un STM32; `depuracion="cmsis-dap"` y
`--cmsis-dap[=PUERTO]` (4441). Recetas en `doc/ejecutables.md` para OpenOCD,
VS Code (Cortex-Debug con OpenOCD) y CCS (*GDB Hardware Debugging* contra
OpenOCD).

**Pruebas:** `verif/openocd/cmsis_dap.py` y `make cmsis-dap` (P401R, E401Y y
F407; FreeRTOS en GDB), la prueba del procesador con paquetes a mano, y la
comparación de tiempos con `remote_bitbang` en el informe.

**Hecha (2026-10-19).** `verif/sonda_cmsis_dap.h` (`SondaCmsisDap`), con
`verif/sonda_red.h`, la interfaz que comparte con la de `remote_bitbang`:

* **No usa `SwdProbe`** (`swd_port.h`), que comparten los stubs de GDB y no
  tiene la capa de bits de CMSIS-DAP (`DAP_SWJ_Sequence`, `DAP_SWD_Sequence`,
  el *turnaround* configurable): la sonda lleva la suya, con los mismos
  tiempos.
* **Las lecturas del AP** se recogen en RDBUFF por el anfitrión: en
  `DAP_Transfer`, una lectura y RDBUFF detrás; en `DAP_TransferBlock`,
  aplazadas, una recoge la anterior y la última va a RDBUFF.
* **Los WAIT se reintentan con espera**, cada vez el doble hasta 1 ms.
  OpenOCD pide 64 reintentos y no reintenta él; 64 paquetes seguidos son menos
  que el milisegundo que el borrado de un sector del F407 tiene el bus
  ocupado, y daba el borrado por fallido. Con la espera cubren unos 60 ms.
* **`DAP_SWJ_Pins` con nRESET** es el SRST, y se lee del nodo de RST.
* **Lo que tarda**, en esta máquina: el P401R se programa en 1,0 s (4-6 s con
  `remote_bitbang`), el E401Y en 0,7 s y el F407 en 1,6 s (16 s); el `load` de
  GDB va a 3-4 KB/s (0,5 KB/s).
* **Pruebas**: `verif/openocd/cmsis_dap.py` y `make cmsis-dap`, 25
  comprobaciones, en el CI de Linux con el mismo OpenOCD que `make openocd`.

### X3 · EnergyTrace en el modelo

**Qué:** el medidor de §4 en la pieza `Xds110` con `et="si"`: el muestreo,
`--energytrace`, los observables, los estados y la energía por función con
los símbolos del ELF; el límite de 75 mA. Documentado en `doc/parts.md` con
lo que mide y lo que no (§4.3).

**Pruebas:** un firmware de `verif/fw/msp432p4/` que alterna activo, LPM0 y
LPM3 con LED1 encendido y apagado: la energía de cada tramo contra la
calculada a mano con las cifras del modelo; la suma por función igual a la
total; sobrecorriente por encima de 75 mA con `et`.

### X4 · La ventana de EnergyTrace

**Qué:** en `mcu-sim-gui`, un panel con la curva de potencia y la energía
acumulada en el tiempo, la franja de estados y la tabla por función; guardar
y cargar una medida. Necesita una gráfica en el tiempo, que la ventana no
tiene: Qt Charts (en Qt 6, GPL/comercial; comprobar que encaja con la licencia
del proyecto) o un dibujo propio con `QPainter`, que es poco y no añade
dependencias. Lo más probable es lo segundo.

**Pruebas:** con el modelo falso de las pruebas de la ventana (las muestras
llegan, la curva y la energía cuadran) y en `prueba_cruzada` contra el
`mcu-sim` de verdad con la P401R.

### X5 · CMSIS-DAP 2.0 por USB/IP (opcional)

**Qué:** el servidor USB/IP (la parte de servidor de U1) con la sonda
`1cbe:02a5` detrás, reutilizando el procesador de X2. Recetas para Linux y
Windows. **Solo si se pide** pyOCD, Keil, IAR o probe-rs.

**Pruebas:** en Linux, `usbip attach` y pyOCD programando el P401R, si el
*runner* deja cargar `vhci-hcd`; si no, una prueba del protocolo USB/IP con
un cliente escrito en Python.

### X6 · El XDS110 de OpenOCD por USB/IP (opcional)

**Qué:** la configuración `0451:bef3` con el subconjunto de órdenes del
controlador `xds110` (§3.3). Lo que da es que `interface/xds110.cfg` funcione
igual que con la placa.

### X7 · CCS, UniFlash y EnergyTrace nativos (no recomendada)

**Qué:** deducir de las capturas de X0 lo que la pila de TI necesita, y la
salida de EnergyTrace por su *endpoint*. Solo con una necesidad concreta, la
sonda prestada y la licencia leída (§3.4).

### Orden y dependencias

```
X0 ─┬──────────────► (anexo: cifras y descriptores)
    │
X1 ─┼─► X2 ─────────► X5 ─► X6 ─► (X7)
    │
    └─► X3 ─► X4
```

X1 va primero porque las demás se cuelgan de la sonda. X2 y X3 son
independientes entre sí. X0 no bloquea nada salvo X7, pero sus cifras hacen
que X3 diga algo comprobado.

---

## 6. Recomendación

1. **X1**: la sonda en la placa. Barata, y hace verdad lo que el alumno ve
   en J101.
2. **X2**: CMSIS-DAP por TCP. Una sonda rápida y estándar para OpenOCD en las
   tres plataformas sin drivers, que sirve también a los STM32.
3. **X3 y X4**: EnergyTrace en el modelo y en la ventana. Lo que hace
   distinta a la LaunchPad del P401R, a un coste bajo, y con una ventaja sobre
   la placa: la energía por función exacta.
4. **X0** en paralelo, cuando haya una placa a mano.
5. **X5 y X6** solo si lo pide un laboratorio. **X7, no.**

---

## 7. Riesgos y preguntas abiertas

1. **¿Qué hay en la Auxiliary Data Port?** SWO en formato UART, según la
   documentación de la sonda; si también EnergyTrace, solo lo dirá una captura.
   No afecta a X1–X4.
2. **La frecuencia de muestreo de EnergyTrace.** No está publicada. Se elige
   1 ms por omisión y se ajusta con X0.
3. **CCS 20 (Theia) y OpenOCD.** Es la misma pregunta abierta que en el
   análisis del MSP432 (§9.2): si CCS 20 no admite un GDB remoto genérico, el
   camino desde CCS sigue siendo la 12.x o VS Code.
4. **`cmsis-dap tcp` es reciente** (septiembre de 2025) y no está en ninguna
   versión publicada. Su protocolo dice que
   cambiará la firma si cambia; hay que fijar la revisión de OpenOCD, como ya se
   hace, y vigilar el cambio.
5. **Que la fuente de la sonda sustituya a la del adaptador** sin mover
   invariantes ni romper placas que no la llevan (todas las de ST). Se decide
   en X1 con una regla simple: el adaptador alimenta solo si nadie más lo hace.
6. **macOS y USB.** Cualquier cosa por USB/IP (X5–X7) lo deja fuera.

---

## 8. La alternativa de hoy: OpenOCD desde CCS 11 y CCS 20

Mientras no haya un XDS110 emulado —y el nativo de CCS no se recomienda
(§3.4)—, **CCS puede depurar un MSP432 a través de OpenOCD**, que hace de
servidor de GDB. CCS deja su *debug server* y su `.ccxml` y habla con GDB.
Sirve igual para dos objetivos, y solo cambia el `.cfg` de OpenOCD:

* **El MSP432 de `mcu-sim`**, por la sonda de su XDS110 y J101 (X1): la
  **CMSIS-DAP por TCP** (`--cmsis-dap`, X2), que es la rápida, o la
  `remote_bitbang` (`--openocd`, T5) con un OpenOCD sin el backend `tcp` de
  cmsis-dap. Para CCS son lo mismo: cambia el `.cfg` y nada más.
* **Una LaunchPad de verdad**, por su XDS110, con el controlador `xds110` de
  OpenOCD.

Lo que se conserva: puntos de ruptura, paso a paso, variables, pila, memoria,
desensamblado y registros del núcleo; con Cortex-Debug (CCS 20), además, los
registros de los periféricos (con el SVD) y los hilos de un RTOS. Lo que se
pierde frente al depurador nativo: EnergyTrace, las vistas de registros con los
datos de TI y los ajustes de Flash de CCS.

### 8.1 Lo que hace falta, en las dos versiones

| Pieza | Para `mcu-sim` | Para una LaunchPad |
| :--- | :--- | :--- |
| **OpenOCD** | **Con el backend `tcp` de cmsis-dap** para `--cmsis-dap`: uno de git posterior al 2025-09-01, o **xPack OpenOCD 0.12.0-7** (octubre de 2025). Con uno anterior pero **con SWD en `remote_bitbang`** (git posterior al 2024-04-07, xPack 0.12.0-4), `--openocd`. En Linux, `sh ci/construye_openocd.sh <prefijo>` da uno con los dos. **La 0.12.0 publicada no vale para ninguno**: dice «only one transport option; autoselect 'jtag'» | Cualquiera desde la 0.10; el de xPack sirve. El XDS110 con firmware 2.3.0.11 o posterior (CCS lo actualiza solo al conectar, o `xdsdfu`) |
| **GDB para ARM** | `arm-none-eabi-gdb` de la toolchain GNU de CCS: `<ccs>/tools/compiler/gcc-arm-none-eabi_<versión>/bin/`. Si no se instaló, se añade desde el gestor de compiladores de CCS, o se usa el de Arm. **El `gdb` del PC no sirve**: no lleva ARM | Igual |
| **El `.cfg`** | En `src/ide/msp432/openocd/`: `mcu-sim-cmsis-dap_msp432p401r.cfg` o `…_msp432e401y.cfg` con `--cmsis-dap`; `mcu-sim_msp432p401r.cfg` o `…_msp432e401y.cfg` con `--openocd` | P401R: `board/ti/msp432-launchpad.cfg` (en la 0.12.0, `board/ti_msp432_launchpad.cfg`), que trae OpenOCD. E401Y: `src/ide/msp432/openocd/launchpad_msp432e401y.cfg` |
| **El objetivo** | `mcu-sim placas/msp_exp432p401r.xml --cmsis-dap --tiempo-real` (o `--openocd`), con o sin firmware: con la Flash en blanco el chip entra en el BSL, y OpenOCD la programa igual | La placa por USB |

Los `.cfg` de `mcu-sim` hacen dos cosas: eligen la sonda -`cmsis-dap` por TCP
en `localhost:4441`, o `remote_bitbang` en `localhost:3335`- con SWD, y cargan
el objetivo que trae OpenOCD sin tocarlo (`target/ti/msp432.cfg` para el P4;
para el E4, `target/ti/stellaris.cfg`, porque OpenOCD lo trata como un
TM4C129). Los del E4 cambian además el evento `reset-start` de
`stellaris.cfg`: ese evento pone `adapter speed 500`, que `remote_bitbang` no
admite, y el reset se queda a medias. El E4 se resetea con SYSRESETREQ, que
es lo único que necesita, y el de CMSIS-DAP lo dice igual.

**Antes de abrir el IDE, la comprobación por consola**, en tres terminales
(desde `src/` de `mcu-sim`):

```
./build/mcu-sim placas/msp_exp432p401r.xml --cmsis-dap --tiempo-real
openocd -f ide/msp432/openocd/mcu-sim-cmsis-dap_msp432p401r.cfg
arm-none-eabi-gdb -q -ex "target extended-remote localhost:3333" \
    -ex "set remotetimeout 60" -ex "monitor reset halt" -ex load \
    -ex "monitor reset halt" -ex "break main" -ex continue Debug/proyecto.out
```

Lo que tiene que salir, en este orden (es la salida real del 19-10-2026, con
`p4_demo.out`):

```
  mcu u0: sin firmware, sonda CMSIS-DAP por TCP en el puerto 4441 por XDS (J101)
[cmsis-dap] escuchando en localhost:4441                            ← mcu-sim
Info : CMSIS-DAP: SWD supported                                      ← OpenOCD
Info : CMSIS-DAP: Interface ready
Info : SWD DPIDR 0x2ba01477
Info : [msp432.cpu] Cortex-M4 r0p1 processor detected
Info : Listening on port 3333 for gdb connections
Loading section .isr_vector, size 0xe4 lma 0x0                       ← GDB
Loading section .text, size 0x688 lma 0xe4
Transfer rate: 4 KB/sec, 950 bytes/write.
Breakpoint 1, main () at main.c:208
```

Con `--openocd` y los `.cfg` `mcu-sim_<chip>.cfg` sale `[openocd] remote_bitbang
escuchando en localhost:3335`, y la carga va a medio kilobyte por segundo.

Si esto funciona, lo que falle después es del IDE.

### 8.2 CCS 11 (y 12): *GDB Hardware Debugging*

CCS 11 es un Eclipse 2021-03 sin modificar, con CDT. CDT trae un tipo de
lanzamiento, ***GDB Hardware Debugging***, que conecta GDB a un servidor
remoto: es el que usa también STM32CubeIDE. En `src/ide/msp432/ccs/` hay uno
hecho, `openocd_msp432.launch`.

**Paso 1 · Que aparezca el tipo de lanzamiento.** En *Run → Debug
Configurations…* tiene que estar ***GDB Hardware Debugging*** en la lista de la
izquierda. Si no está:

1. *Window → Preferences → General → Capabilities*: marcar lo que sea de
   CDT/GDB (CCS esconde partes de CDT detrás de «capacidades»); *Apply and
   Close*, y volver a mirar.
2. Si sigue sin estar, instalarlo: *Help → Install New Software…*, *Work
   with:* `https://download.eclipse.org/tools/cdt/releases/10.2/` (el CDT de
   Eclipse 2021-03), desmarcar *Group items by category*, buscar **C/C++ GDB
   Hardware Debugging**, *Next*, aceptar la licencia y reiniciar CCS.

**Paso 2 · Arrancar `mcu-sim` y OpenOCD desde CCS** (o desde dos terminales,
como en §8.1; con una LaunchPad, solo OpenOCD). *Run → External Tools →
External Tools Configurations…*, *Program*, botón *New* (arriba a la
izquierda):

| Campo | `mcu-sim` | OpenOCD |
| :--- | :--- | :--- |
| *Name* | `mcu-sim P401R` | `OpenOCD mcu-sim P401R` |
| *Main → Location* | `C:\…\mcu-sim\src\build\mcu-sim.exe` | `C:\…\xpack-openocd-0.12.0-7\bin\openocd.exe` |
| *Main → Working Directory* | `C:\…\mcu-sim\src` | `C:\…\mcu-sim\src` |
| *Main → Arguments* | `placas/msp_exp432p401r.xml --cmsis-dap --tiempo-real` | `-f ide/msp432/openocd/mcu-sim-cmsis-dap_msp432p401r.cfg` |
| *Build* | Desmarcar *Build before launch* | Igual |
| *Common* | Marcar *External Tools* en *Display in favorites menu* | Igual |

*Apply*, y *Run* primero el de `mcu-sim` y después el de OpenOCD. Cada uno abre
su consola en la vista *Console*: la de `mcu-sim` tiene que decir
`[cmsis-dap] escuchando en localhost:4441`, y la de OpenOCD,
`Listening on port 3333 for gdb connections`. Se paran con el cuadrado rojo de
su consola.

**Paso 3 · La configuración de depuración.** O se importa la hecha (*File →
Import… → Run/Debug → Launch Configurations*, la carpeta
`src/ide/msp432/ccs`, marcar `openocd_msp432`), o se crea: *Run → Debug
Configurations…*, *GDB Hardware Debugging*, *New*. Pestaña por pestaña:

* ***Main***: *Project*, el del programa; *C/C++ Application*,
  `Debug/<proyecto>.out` (con *Search Project…* se encuentra).
* ***Debugger***:
  * *GDB Command*: la **ruta completa** de `arm-none-eabi-gdb.exe` (§8.1);
  * marcar ***Use remote target***;
  * *JTAG Device*: ***Generic TCP/IP***;
  * *Host name or IP address*: `localhost`; *Port number*: `3333`.
* ***Startup***:
  * desmarcar *Reset and Delay* y *Halt*: los hace la orden de abajo;
  * *Initialization Commands*:
    ```
    set remotetimeout 60
    monitor reset halt
    maintenance flush register-cache
    ```
  * marcar ***Load image*** con *Use project binary*, y ***Load symbols*** con
    *Use project binary*;
  * *Run Commands*:
    ```
    monitor reset halt
    maintenance flush register-cache
    ```
  * marcar ***Set breakpoint at:*** `main` y ***Resume***.
* ***Common***: si se quiere tener a mano, *Display in favorites menu → Debug*.

*Apply* y ***Debug***. CCS ofrece pasar a la perspectiva *Debug* (la de CDT,
no la *CCS Debug*): sí. GDB conecta, OpenOCD programa la Flash con su algoritmo
—en `mcu-sim`, a 3-4 KB/s por CMSIS-DAP (medio kilobyte por segundo por
`remote_bitbang`)— y el programa para en `main`.

**Por qué esas órdenes.** `monitor` pasa la orden a OpenOCD: `reset halt`
deja el núcleo parado en el vector de reset. GDB no vuelve a leer los
registros después de un `monitor`; `maintenance flush register-cache` se lo
fuerza, o la vista de registros enseñaría los de antes del reset. `set
remotetimeout 60` porque OpenOCD no contesta a GDB mientras ejecuta su
algoritmo de Flash, y el plazo por omisión (2 s) se queda corto.

**Paso 4 (opcional) · Todo con un botón.** *Run → Debug Configurations… →
Launch Group → New*, y con *Add…* las tres configuraciones en este orden:
`mcu-sim P401R` (modo *run*, *Post launch action*: *Wait for console output
(regexp)* con `cmsis-dap\] escuchando`), `OpenOCD mcu-sim P401R` (*run*,
*Wait for console output* con `Listening on port 3333`) y `openocd_msp432`
(*debug*).

**El E401Y y la LaunchPad**: lo mismo, cambiando `msp_exp432p401r.xml` por
`msp_exp432e401y.xml` y el `.cfg` (§8.1). Con la placa de verdad no hay
`mcu-sim` y el `.cfg` de OpenOCD es el de la placa.

### 8.3 CCS 20 (Theia): Cortex-Debug

CCS 20 ya no es Eclipse: es Theia, y no trae *GDB Hardware Debugging*. Su
`launch.json` solo admite su tipo `ccs-debug`, que va a las sondas de TI. Pero
**acepta extensiones de VS Code de Open VSX**, y la de depuración de Cortex-M
de referencia, **Cortex-Debug** (`marus25.cortex-debug`), está allí. Esa
extensión arranca OpenOCD, le conecta GDB y carga el programa. En
`src/ide/msp432/ccs20/` están `launch.json` y `tasks.json` hechos.

**Paso 1 · Instalar Cortex-Debug.** El icono de *Extensions* en la barra de
actividad de la izquierda, buscar `cortex-debug`, el de **marus25**, *Install*.
Instala también sus dependencias (`mcu-debug`: *debug-tracker-vscode*,
*memory-view*, *rtos-views*, *peripheral-viewer*). Sin acceso a Internet: bajar
los `.vsix` de open-vsx.org y, en la vista *Extensions*, el menú `…` →
*Install from VSIX…*. **TI no la soporta**: su documentación dice que
`ccs-debug` es el único tipo «soportado»; funciona porque Theia es Theia, pero
cada versión nueva de CCS hay que comprobarla.

**Paso 2 · Las tareas y los lanzamientos.**

* `tasks.json`: arranca `mcu-sim` con `--cmsis-dap` en segundo plano y espera
  a que diga `[cmsis-dap] escuchando` antes de dejar seguir (con un OpenOCD
  sin `cmsis-dap tcp`, `--openocd` y los `.cfg` de `remote_bitbang`). Se copia a la carpeta de
  configuración del proyecto (`.theia/tasks.json`, o `.vscode/tasks.json`,
  que Theia también lee) y se cambian las dos `RUTA`.
* `launch.json`: en la vista *Run and Debug*, el engranaje (*Open
  launch.json*) abre el que CCS usa —el de la carpeta `.theia` del espacio de
  trabajo—, y se añaden las entradas de `src/ide/msp432/ccs20/launch.json`,
  cambiando las `RUTA`:
  * `serverpath`: `openocd.exe`;
  * `configFiles`: el `.cfg` de §8.1;
  * `gdbPath`: `arm-none-eabi-gdb.exe`;
  * `executable`: el `.out` del proyecto.

  Lo que hace cada campo que no es una ruta:

  | Campo | Valor | Por qué |
  | :--- | :--- | :--- |
  | `type` / `servertype` | `cortex-debug` / `openocd` | Cortex-Debug lanza OpenOCD con su propio puerto de GDB y lo para al acabar |
  | `preLaunchTask` | La tarea de `mcu-sim` | Que la sonda esté escuchando antes de que OpenOCD la busque. Con la LaunchPad no hace falta |
  | `preLaunchCommands` | `set remotetimeout 60` | Lo mismo que en CCS 11 |
  | `runToEntryPoint` | `main` | Para en `main` después de cargar |
  | `device` | `MSP432P401R` / `MSP432E401Y` | Informativo; con `svdFile` apuntando al SVD del chip (del paquete CMSIS de TI, `MSP432P4xx_DFP`), la vista de periféricos |

  Las órdenes de reset y carga no hay que darlas: con `servertype: openocd`,
  Cortex-Debug hace `monitor reset halt`, `load` y otra vez `monitor reset
  halt`.

**Paso 3 · Depurar.** En *Run and Debug*, elegir «MSP432P401R · mcu-sim +
OpenOCD» en el desplegable y ▶ (F5). En el panel de abajo aparecen la terminal
de la tarea (`mcu-sim`), la consola *gdb-server* (OpenOCD) y la *Debug
Console* (GDB). Al parar en `main`, la barra de depuración, las variables, la
pila, los *breakpoints* y, con el SVD, *Cortex Peripherals*.

### 8.4 Problemas conocidos

| Síntoma | Causa | Arreglo |
| :--- | :--- | :--- |
| OpenOCD: «only one transport option; autoselect 'jtag'» | OpenOCD 0.12.0, sin SWD en `remote_bitbang` | Uno de git o el de xPack (§8.1) |
| OpenOCD: «Error connecting DP» al arrancar | `mcu-sim` no está escuchando, u otro proceso tiene el 3335 | Arrancar antes `mcu-sim`; `--port` cambia el puerto (y el `.cfg`, igual) |
| GDB: «Truncated register 18» o «Remote 'g' packet reply is too long» | El `gdb` del PC, no el de ARM | La ruta completa de `arm-none-eabi-gdb` |
| GDB: «Remote communication error» o tiempo agotado en `load` | El plazo de 2 s de GDB mientras OpenOCD programa | `set remotetimeout 60` |
| OpenOCD avisa «keep_alive() was not invoked…» | Lo mismo, del lado de OpenOCD | Inofensivo |
| E401Y: «Unknown did0 class» | El controlador `stellaris` no conoce la clase del MSP432E4 | Inofensivo: programa con el tamaño de DID1 |
| E401Y: «Execution of event reset-start failed» | `stellaris.cfg` sin el cambio de nuestro `.cfg` | Usar `mcu-sim_msp432e401y.cfg` |
| GDB: «Invalid state, unable to determine sp alias» | GDB 15 con la descripción de registros de OpenOCD | Inofensivo |
| La carga es lenta | `remote_bitbang`: un carácter por flanco | `--cmsis-dap` y el `.cfg` de CMSIS-DAP: en CCS no cambia nada más |
| OpenOCD: «CMSIS-DAP: unable to connect to device localhost:4441» | `mcu-sim` no está escuchando por CMSIS-DAP: falta `--cmsis-dap`, o ha arrancado con `--openocd` | Arrancar `mcu-sim` con `--cmsis-dap` antes que OpenOCD |
| OpenOCD: «invalid backend argument» o no conoce `cmsis-dap tcp` | Un OpenOCD anterior al backend `tcp` (septiembre de 2025) | xPack 0.12.0-7 o uno de git; o `--openocd` con su `.cfg` |
| OpenOCD no encuentra el DAP («Error connecting DP») con J101 cambiado | Falta el puente de TMS o el de TCK: la sonda va por J101, como en la placa | Ponerlos; sin el de 3V3 el chip ni siquiera arranca |

### 8.5 Qué está probado

**Probado aquí, el 19-10-2026**, en Linux, con `mcu-sim` (rama `msp432`),
OpenOCD 2075427 y `gdb-multiarch` 15.1, con los cuatro `.cfg` de `mcu-sim`.
Las órdenes son las mismas que mandan los dos IDE: `monitor reset halt`,
`load`, `compare-sections` (todo «matched»), `break main`, `continue` y
`next`.

* **P401R**: con la Flash en blanco y con otro firmware cargado; el ejemplo
  `systick_interrupt_gpio_blink` del SDK y `p4_demo`: a 4 KB/s por CMSIS-DAP
  y a unos 500-630 B/s por `remote_bitbang`.
* **E401Y**: `e4_demo`, a 3 KB/s por CMSIS-DAP y a 526 B/s por
  `remote_bitbang`.
* Las dos sondas, por el XDS110 de la placa (J101).

**No probado**:

* dentro de CCS 11 ni de CCS 20: los pasos siguen la documentación de CDT, de
  Cortex-Debug y de CCS, y la configuración de CubeIDE, que sí se probó en su
  IDE (`cubeide/README.md`);
* el xPack en Windows;
* los `.cfg` de las LaunchPad de verdad.

**Capturas de pantalla: no hay.** Las descargas de TI no se alcanzan desde el
entorno donde se escribió esto, así que CCS no se pudo instalar allí; y en el
PC del usuario, un IDE se puede mirar y pulsar pero no escribir en él. Las que
conviene añadir, en `doc/img/`, al seguir los pasos a mano:

1. `ccs11_external_tools.png`: la configuración de `mcu-sim` en *External Tools*;
2. `ccs11_debugger.png` y `ccs11_startup.png`: las pestañas *Debugger* y
   *Startup* de *GDB Hardware Debugging*;
3. `ccs11_sesion.png`: la sesión parada en `main`;
4. `ccs20_extension.png`: Cortex-Debug en la vista *Extensions*;
5. `ccs20_sesion.png`: la sesión, con las tres consolas abajo.

---

## Fuentes

Consultadas el 19-10-2026.

* Repositorio `mcu-sim` (rama `msp432`, commit `95f1119`):
  `doc/analisis_msp432.md` (§4.4–4.6, §5, T5 y T8),
  `doc/analisis_puente_usb.md` (U1, §5.1), `verif/sonda_openocd.h`,
  `verif/swd_port.h`, `core/debug.h`, `soc/msp432p4/msp432p401r.h`
  (`idd_modelo`, `idd_medida`), `placas/msp_exp432p401r.xml` (J101, `BCL`),
  `top/sim_main.cpp`.
* TI, *MSP432P401R SimpleLink™ Microcontroller LaunchPad™ Development Kit*,
  SLAU597F (`doc/refs`): §2.3 (XDS110-ET y J101), §2.3.5 (EnergyTrace+),
  §2.4.1 (500 mA y 75 mA con EnergyTrace), §2.5.
* [OpenOCD, `src/jtag/drivers/xds110.c`](https://github.com/openocd-org/openocd/blob/master/src/jtag/drivers/xds110.c),
  revisión 2075427 (GPL-2.0, de TI): VID/PID, interfaces y *endpoints*, marco
  `'*'` + longitud, órdenes `XDS_*`, `CMAPI_*`, `SWD_*`, `OCD_*` y versiones
  mínimas del firmware; y el commit
  [`d35c44c`](https://github.com/openocd-org/openocd/commit/d35c44c743d6f471697ed1d204fe5447fd47cefd)
  con las tres configuraciones.
* [OpenOCD, `cmsis_dap_tcp.c`](https://www.openocd.org/doc/doxygen/html/cmsis__dap__tcp_8c_source.html):
  la cabecera de 8 bytes, el puerto 4441, sin UART ni SWO; y la sección
  `cmsis-dap tcp` del manual (`doc/openocd.texi`).
* [TI, XDS110 Debug Probe](https://software-dl.ti.com/ccs/esd/documents/xdsdebugprobes/emu_xds110.html):
  TM4C1294NCPDT, compatibilidad con CMSIS-DAP, SWO en formato UART,
  *backchannel*, `xdsdfu` y la actualización automática del firmware desde CCS.
* [TI, EnergyTrace](https://software-dl.ti.com/ccs/esd/documents/xdsdebugprobes/emu_energytrace.html):
  medida por pulsos del DC-DC, rangos y precisión, datos por un *endpoint
  bulk*, `stune`.
* [TI wiki, EnergyTrace for MSP432](https://processors.wiki.ti.com/index.php/EnergyTrace_for_MSP432):
  100 mA de máximo, potencia a 3,3 V fijos, EnergyTrace+ sí y ++ no, sin
  muestreo del PC en *Free Run*.
* [carrotIndustries/energytrace-util](https://github.com/carrotIndustries/energytrace-util)
  (GPL-3.0): el formato de columnas tiempo, corriente, tensión y energía (para
  las sondas MSP430, con `libmsp430`).
* [TI, CCS 20.1, *User Interface Introduction*](https://software-dl.ti.com/ccs/esd/documents/users_guide_ccs_20.1.0/ccs_ide.html):
  extensiones de Open VSX desde la vista *Extensions*; y
  [*Debug Overview*](https://software-dl.ti.com/ccs/esd/documents/users_guide_ccs_20.1.0/ccs_debug-main.html):
  `launch.json` en la carpeta `.theia`, `ccs-debug` como único tipo soportado.
* [TI, notas de CCS 11.2](https://software-dl.ti.com/ccs/esd/CCSv11/CCS_11_2_0/exports/CCS_11.2.0_ReleaseNotes.htm):
  Eclipse y CDT 2021-03 sin modificar.
* [Cortex-Debug en Open VSX](https://open-vsx.org/extension/marus25/cortex-debug):
  la extensión y sus dependencias `mcu-debug`.
* [xPack OpenOCD, versiones](https://github.com/xpack-dev-tools/openocd-xpack/releases):
  de 0.12.0-4 (agosto de 2024) a 0.12.0-7 (octubre de 2025), construidas
  desde el master de OpenOCD.
* OpenOCD 2075427: `board/ti/msp432-launchpad.cfg`, `target/ti/msp432.cfg` y
  `target/ti/stellaris.cfg` (el evento `reset-start` con `adapter speed 500`).
* [Directiva 2009/24/CE](https://eur-lex.europa.eu/eli/dir/2009/24/oj),
  art. 6 (descompilación para la interoperabilidad).
