# Fuentes: qué se ha leído, de dónde y qué revisión

Este proyecto tiene una **regla de dos fuentes**: los datos de registro, pin,
DMA y vector no se copian a mano de un PDF, sino que se extraen de un fichero
**legible por máquina del propio fabricante** y se contrastan contra el manual.
De ahí que aquí haya dos clases de documento y no una: los PDF de ST, que
explican, y los XML y las cabeceras de ST, que se pueden comparar por programa.

Los ficheros **no se versionan**: son 72 MB, cambian de revisión sin avisar y
`.gitignore` excluye `doc/refs/`. Lo que sí se versiona es **este índice**: qué
documento se usó, en qué revisión, dónde está la copia local y —para lo que
falta— de dónde se baja. Un informe que cita una sección sin decir de qué
revisión envejece mintiendo.

**La carpeta se llamaba `doc/pdf/`** y se renombró a `doc/refs/` cuando dejó de
tener solo PDF: ahora guarda también los informes internos del proyecto, por lo
que dice la sección 0.

---

## 0. Los informes internos del proyecto, en `doc/refs/stm32f407xx/`

Tres ficheros que **este proyecto escribió y que sin embargo no están en el
repositorio**:

| Etiqueta | Fichero | Qué es |
| :--- | :--- | :--- |
| `[IR]` | `informe_revisado.md` | El informe técnico de bajo nivel. La fuente del modelo: se cita 1 083 veces en `src/` |
| `[II]` | `informe_instrucciones.md` | La codificación de todas las instrucciones del Cortex-M4F |
| — | `informe.md` | La primera versión del informe, anterior a la revisión |

**Por qué no se versionan, que no es lo mismo que por qué no se publican.** Se
escribieron con una regla explícita, la que está en
`doc/stm32f4xx/informe_reglas.md`:

> *Prohibido remitir a otro documento: nunca escribas «véase el manual de
> referencia»… Reproduce el dato en el propio informe.* … *No resumas ni
> selecciones «lo más importante».*

Esa regla es exactamente la que los convierte en **obra derivada**: se pidieron
para **sustituir** al manual, no para comentarlo. El equipo destinatario «NO
tendrá acceso a los manuales originales», decía el encargo. Los **hechos** —una
dirección base, una máscara, un canal de DMA— no son de nadie; la **expresión**
sí, y un documento que sigue la estructura del manual y reproduce sus tablas
puede ser derivado de él. **Solo se puede licenciar lo que es propio**, y el
repositorio es público. Véase **I-50** en `doc/todo.md`.

**Dónde está la raya.** Se va lo que se escribió para *reemplazar* el manual; se
queda lo que se escribió para *razonar sobre* él. Por eso siguen versionados los
planes de fase de `doc/stm32f4xx/`, las dos comparativas entre piezas y los
cuatro ficheros de prompts —`informe_general.md`, `informe_reglas.md`,
`informe_capitulos.md`, `informe_genera_capitulo.txt`—, que son el **método** y
no el contenido.

**Qué implica para quien clone el repositorio.** Las 1 083 citas `[IR, §x]` y
`[II, §y]` de los comentarios del modelo **apuntan a un documento que no viene
con él**. No dejan el código sin justificar —cada una dice qué sección lo
respalda— pero para leer esa sección hace falta el manual de ST, que es de
dónde salió. La tabla de la sección 1 dice cuál y en qué revisión.

**Tampoco están en el historial.** Estuvieron versionados hasta el 23 de
septiembre de 2026; sacarlos del pasado exigió reescribir la historia pública
con `git filter-repo`, así que los commits son los mismos pero **sus hashes no**.
Lo cuenta el `README`, y el porqué entero está en **I-50** de `doc/todo.md`.

---

## 1. Manuales y hojas técnicas de ST, en `doc/refs/`

| Etiqueta | Documento | Revisión | Fichero |
| :--- | :--- | :--- | :--- |
| `[RM0090]` | Reference manual F405/415/407/417/427/437/429/439 | **Rev 22**, mayo de 2026 | `stm32f407xx/rm0090-...-stmicroelectronics.pdf` |
| `[DS8626]` | Datasheet STM32F405xx/F407xx | **Rev 12** | `stm32f407xx/stm32f405xx stm32f407xx datasheet.pdf` |
| `[DS8597]` | Datasheet STM32F415xx/F417xx | **Rev 9**, septiembre de 2020 | `stm32f407xx/stm32f415rxx stm32f417xx datasheet.pdf` |
| `[RM0390]` | Reference manual F446xx | **Rev 9**, febrero de 2026 | `stm32f446xx/rm0390-...-stmicroelectronics.pdf` |
| `[DS10693]` | Datasheet STM32F446xC/E | **Rev 11** | `stm32f446xx/DS_stm32f446mc.pdf` |
| `[DS9118]` | Datasheet STM32F303xB/C (P-18) | la de `st.com` en octubre de 2026 | `stm32f303/ds9118-DS_stm32f303vc.pdf` |
| `[UM1570]` | Discovery kit with STM32F303VC MCU | **Rev 7**, septiembre de 2023 | `stm32f303/um1570-discovery-kit-with-stm32f303vc-mcu-stmicroelectronics.pdf` |
| `[MB1035]` | Esquemático de la STM32F3DISCOVERY | **rev. D.1** (PCB.SCH), 8 de junio de 2016 | `stm32f303/mb1035-f303c-d01_schematic.pdf` |

Cada PDF tiene al lado su extracción a texto con `pdftotext -layout`, que es
como se leen las tablas largas sin abrir el visor: `rm0090.txt`,
`ds_f405_f407.txt`, `ds_f415_f417.txt`, `rm0390-...txt` y `ds_f446.txt`.

Del F303 falta el manual de referencia, **RM0316**: lo que el código cita de
él sigue marcado **[1F]** (P-18 de `doc/todo.md`). El análisis de octubre se
escribió con **UM1570 Rev 3**; la placa y su dibujo están ya contrastados con
la Rev 7 y el esquemático (§«Estado» de `doc/analisis_stm32f303.md`).

**Dos avisos de revisión que importan.** El primero: el informe del F415/F417 es
**el primero del proyecto escrito contra el RM0090 Rev 22**; los anteriores
leyeron la **Rev 18**, y el resto del proyecto sigue anclado a ella. El segundo:
los informes más antiguos —`doc/refs/stm32f407xx/informe.md`— citan los documentos por
su identificador interno de ST, `en.DM00031020.pdf` y `en.DM00037051.pdf`, que
son **el RM0090 y el DS8626**, los mismos dos de la tabla.

## 2. Fuentes legibles por máquina, en `doc/refs/`

| Carpeta | Qué hay | Para qué se usó |
| :--- | :--- | :--- |
| `cmsis_v1.28.3/` | Las seis cabeceras de dispositivo de **STM32Cube_FW_F4 V1.28.3**: `stm32f405xx.h`, `stm32f407xx.h`, `stm32f415xx.h`, `stm32f417xx.h`, `stm32f446xx.h` y `stm32f4xx.h` | Las veinte máscaras de `RCC_xxxENR`/`RSTR`/`LPENR` (I-44), las posiciones de vector, y la comparación símbolo a símbolo entre familias del plan del F415/F417 |
| `cubemx/mcu/` | Los **22 descriptores** de CubeMX de las referencias del F405/F407, F415/F417 y F446 | Las ocho comparaciones de pines y las dos de tabla AF del plan del F415/F417 (§3.2), y el reparto de pines del F446 (`STM32F446R(C-E)Tx.xml`) |
| `cubemx/IP/` | `DMA-STM32F417_dma_v2_0_Modes.xml`, `DMA-STM32F446_dma_v2_0_Modes.xml`, `GPIO-STM32F417_gpio_v1_0_Modes.xml`, `GPIO-STM32F446_gpio_v1_0_Modes.xml` | Las 128 celdas del mapa de canales de DMA (I-47) y las cinco de los I2SxEXT (I-48); la tabla de funciones alternativas |

Las tres salen de la instalación local de CubeMX (`db/mcu` y `db/mcu/IP`) y del
repositorio de Cube: `STM32CubeRepository/STM32Cube_FW_F4_V1.28.3`. El
repositorio público `STM32_open_pin_data` —el `[PINDATA]` de los informes— es el
subconjunto de pines de esa misma base de datos.

### 2.1 Las cabeceras vendidas en `src/verif/fw/cmsis/` NO son las mismas copias

Y conviene saberlo, porque es la trampa que destapó la §12.1 del informe del
F415/F417: **comparar cabeceras de versiones distintas mezcla diferencias de
chip con diferencias de edición**. Comparados los ficheros:

| Cabecera vendida | contra `cmsis_v1.28.3/` |
| :--- | :--- |
| `stm32f417xx.h` | **idéntica** |
| `stm32f4xx.h` | **idéntica** |
| `stm32f407xx.h` | distinta |
| `stm32f446xx.h` | distinta |

**Pero la diferencia no toca un solo dato**: puestas las dos listas de `#define`
una al lado de otra —nombres y valores—, **no difiere ninguna línea** en
ninguna de las dos. Lo que cambia son comentarios y espaciado. O sea: el aviso
de método sigue en pie y hay que seguir comparando dentro de una misma versión,
pero esta vez la mezcla **no causó daño**, y eso ahora está **medido** en vez de
supuesto.

## 2 bis. Las del MSP432 (P-16), y la etiqueta con la que las cita el código

El código del MSP432P4 (`soc/msp432p4/`, `periph/msp432p4/`) cita así; lo que
solo tiene una fuente lleva **[1F]** y su subpunto de P-16 en `doc/todo.md`.

| Etiqueta | Qué | Dónde |
| :--- | :--- | :--- |
| `[H]` | `msp432p401r.h`, la cabecera CMSIS de TI (2017-12-06, versión 3230 de la cabecera) | **Versionada**, `src/verif/fw/msp432p4/ti/`. Tomada del port de mbed de Andreas Terstegge (`github.com/Terstegge/mbed-os-MSP432`), que la incluye sin tocar |
| `[SA]` | SLAA668A, «Getting Started with MSP432P4xx» (TI, marzo 2017): VCORE y estados de espera, tabla 2 | `ti.com/lit/an/slaa668a` |
| `[SYS]` | `system_msp432p401r.c` (del mismo port; derivado del de TI): VLO, REFO, MODOSC, SYSOSC y la fórmula del DCO | No versionado |
| `[OO]` | OpenOCD, `src/flash/nor/msp432.c` (GPL-2.0, solo como referencia: no se copia código) | `openocd.org/doc/doxygen/html/msp432_8c_source.html` |
| — | **OpenOCD como herramienta de prueba (T5)**: hace falta uno de git posterior al 2024-04-07 (`79b51fe`, las órdenes de espera Z/z), porque el SWD de `remote_bitbang` (`0f70c6c`, 2023-12-03) no está en ninguna versión publicada: la 0.12.0 es de enero de 2023. Probado con `2075427` (master, 2026-10-13), la que fija `ci/construye_openocd.sh`. No se copia nada de él | `github.com/openocd-org/openocd` |
| — | DriverLib del SDK SimpleLink MSP432P4 3.40: `rom.h` (`ROM_APITABLE`), `sysctl.h` (etiquetas del TLV), `reset.h` | `software-dl.ti.com/simplelink/esd/simplelink_msp432_sdk/3.40.00.05/docs/` |
| `[ROM]` | La imagen de la ROM de DriverLib del P401R rev. C, con símbolos, que instala CCS para depurar dentro de ella (`ccs_base/emulation/gel/images/msp432p401r/revC/msp432_driverlib_rom_image.out`). Segunda opinión de las tablas de la R1 y la R2 de `--rom` (T4) | **No versionada**: es de TI. Se usó la copia que lleva el TICloudAgent del port de mbed de Terstegge (`1d1bf66`) |

El BSL del P4 (T5, D4: `soc/msp432p4/bsl_p4.h`, `verif/fw/msp432p4/bsl_b1`)
tiene dos fuentes, en `doc/refs/` y **sin versionar**: el paquete
**MSP432BSL_1_01_00_00** de TI (el código del BSL a medida, «TI Text File
License», y su imagen; de ahí el sector de 0x0020_2000, la palabra de entrada,
la versión de 0x0020_2010, los parámetros de r0 y la etiqueta 15 del TLV) y
**BSL Scripter 3.4** (código BSD y ejecutables: el lado del PC del protocolo,
y la herramienta con la que se prueba). Del primero no se copia código: el B1
está escrito desde la descripción del protocolo.

El del MSP432E4 (`soc/msp432e4/`, `periph/msp432e4/`, T6) cita además así; lo
que solo tiene una fuente lleva **[1F]** y su subpunto de **P-17**:

| Etiqueta | Qué | Dónde |
| :--- | :--- | :--- |
| `[H]` | `msp432e401y.h`, la cabecera CMSIS del E4, y los `hw_*.h` de DriverLib | **Versionadas**, `src/verif/fw/msp432e4/sdk/`: el SDK SimpleLink MSP432E4 que lleva `msp432e-core` de Energia (`be5c1c2`), BSD-3 |
| `[DL]` | DriverLib del E4: `sysctl.c` (PIOSC, la tabla de `MEMTIM0`, `SysCtlClockFreqSet`), `flash.c`, `eeprom.c`, `gpio.c`, `pin_map.h` (las funciones de cada pin), `rom.h` y `rom_map.h` (las 34 tablas de la ROM en `0x0100_0010`) | La misma copia, versionada |
| `[EN]` | Energia, `variants/MSP_EXP432E401Y`: los LED, los pulsadores y la UART0 de la LaunchPad | `github.com/energia/msp432e-core` |
| `[OO]` | OpenOCD: `DID0`/`DID1` del E401Y y el algoritmo de Flash del E4 (`contrib/loaders/flash/msp432`), como segunda opinión del controlador. No se copia código | `github.com/openocd-org/openocd` |
| `[SPMU301]` | «TivaWare Boot Loader User's Guide»: el protocolo serie del cargador (paquetes, ACK/NAK, órdenes y estados), que el cargador de la ROM R1 implementa **sin** copiar el `boot_loader` de TivaWare | `ti.com/lit/ug/spmu301` (no versionado) |
| `[DS]` | Datasheet del MSP432E401Y (SLASEN5): 120 MHz, 90 E/S en el TQFP de 128, la ROM | `ti.com/lit/ds/slasen5` (no versionado; de ahí, de memoria, buena parte de los **[1F]**) |

Y en la T7 (CCM, C_CAN, EMAC con su PHY, el cargador por Ethernet y el USB):

| Etiqueta | Qué | Dónde |
| :--- | :--- | :--- |
| `[H]`, `[DL]` | `hw_ccm.h`, `hw_shamd5.h`, `hw_aes.h`, `hw_des.h`, `hw_can.h`, `hw_emac.h`, `hw_usb.h` y sus `.c` de DriverLib (`crc.c`, `shamd5.c`, `aes.c`, `des.c`, `can.c`, `emac.c`, `usb.c`): mapas, bits y secuencias. Cada bloque se contrasta además con una de las de abajo | La misma copia, versionada |
| `[TRM]` | SLAU723A, el TRM del MSP432E4: el PHY interno (`EPHY_*`, `EMACPC`), la ventana del USB y del EMAC | Leído en el visor de documentos de `ti.com` (no versionado, no bajado entero) |
| — | Bosch, «C_CAN User's Manual» rev. 1.2: la prioridad entre objetos, las FIFOs, bus-off e INIT | Segunda fuente de `can.c`; de memoria, sin versionar |
| — | El controlador `omap-sham` de Linux (`drivers/crypto/omap-sham.c`, la misma IP que el SHA/MD5 del CCM): el orden de los bytes del resumen | `git.kernel.org` (GPL-2.0, solo como referencia: no se copia código) |
| — | QEMU, `-netdev stream` (y gvproxy, passt): cada trama con su longitud delante en 32 bits big-endian | `qemu.org/docs`, y el código de gvproxy |
| — | lwIP 2.2.1, versionado en `e4_eth/lwip/` (BSD-3, TERCEROS.md) | `github.com/lwip-tcpip/lwip` |
| — | USB 2.0 (cap. 7 y 9: la conexión, el reset y las peticiones estándar) y CDC PSTN 1.2 (`SET_LINE_CODING`, `SET_CONTROL_LINE_STATE`, `SEND_BREAK`) | `usb.org`, de memoria; sin versionar |
| — | RFC 951 (BOOTP) y RFC 1350 (TFTP), para el cargador por Ethernet | `rfc-editor.org`, de memoria |

Las placas (`placas/msp_exp432p401r.xml`, `msp_exp432e401y.xml`,
`boostxl_edumkii.xml` y sus dibujos) salen de sus guías, **en `doc/refs/` sin
versionar**: SLAU597F (MSP-EXP432P401R, rev. de marzo de 2018, con los
esquemáticos de la placa rev. 2.1), SLAU748B (MSP-EXP432E401Y, septiembre de
2018, esquemáticos rev. 1.1; el fichero se llama «MSP432P401Y» por error) y
SLAU599B (BOOSTXL-EDUMKII, julio de 2021, esquemáticos rev. 1.1). Lo que el
dibujo no saca de ellas -las medidas de la P401R y de la EDUMKII- es
aproximado (P-17 u).

**Pendientes de bajar a `doc/refs/`, sin versionar:** SLAU356 (TRM del
MSP432P4), SLAS826 (datasheet del P401R), SLAU622 (BSL), SLAU723 (TRM del
E4). La red de este entorno pide permiso para `ti.com` y no se pudo leer el
datasheet completo: de ahí buena parte de los **[1F]**.

## 3. Lo que se ha citado y NO está aquí

| Etiqueta | Documento | Por qué falta | De dónde se baja |
| :--- | :--- | :--- | :--- |
| `[PM0214]` | Cortex-M4 programming manual, **rev. 10** | La red de este entorno rechaza `www.st.com` | [st.com](https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) |
| `[AN4658]` | Migración F429/439 → F446 | Igual | [st.com](https://www.st.com/resource/en/application_note/an4658-migration-of-applications-from-stm32f429439-lines-to-stm32f446-line-stmicroelectronics.pdf) |
| FIPS 197 | AES | La red rechaza `nvlpubs.nist.gov` | `https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197-upd1.pdf` |
| NIST SP 800-38A | Modos de operación (ECB, CBC, CTR) | Igual | `https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf` |
| RFC 1321 | MD5 | La red rechaza `www.rfc-editor.org` | `https://www.rfc-editor.org/rfc/rfc1321.txt` |
| RFC 3174 | SHA-1 | Igual | `https://www.rfc-editor.org/rfc/rfc3174.txt` |
| RFC 2202 | Vectores de HMAC-MD5 y HMAC-SHA-1 | Igual | `https://www.rfc-editor.org/rfc/rfc2202.txt` |
| ARMv7-M ARM | DDI 0403, citado una vez (§B3.2.10, la máscara de `SHPR3`) | Exige registro en `developer.arm.com` | `developer.arm.com` |

Las cinco especificaciones de cripto son **el origen de los 39 vectores** de
`make hash` y `make cryp`, y de los 54 de `make vectores`. Que el documento no
esté en disco no deja el dato sin contrastar: cada vector se recalcula con **dos
motores independientes** —OpenSSL y pycryptodome, más `hashlib`— y
`src/verif/vectores/comprueba_vectores.py` falla si algún caso se apoya en uno
solo. El PDF serviría para leer el porqué, no para saber si el número es bueno.

## 4. Cómo volver a llenar `doc/refs/` en otra máquina

1. Los cinco PDF de ST se bajan de `st.com` buscando su etiqueta (`RM0090`,
   `DS8626`, `DS8597`, `RM0390`, `DS10693`) y se extraen con
   `pdftotext -layout <fichero>.pdf <fichero>.txt`.
2. Las seis cabeceras salen de `STM32Cube_FW_F4_V1.28.3`, en
   `Drivers/CMSIS/Device/ST/STM32F4xx/Include`.
3. Los 22 descriptores y los cuatro XML de modos salen de la instalación de
   **STM32CubeMX**, en `db/mcu` y `db/mcu/IP`. Sin CubeMX instalado, el
   repositorio público `STM32_open_pin_data` cubre la parte de pines.

La regla de colocación, para no volver a desparramarlo: **las carpetas de
familia —`stm32f407xx/` y `stm32f446xx/`— llevan solo PDF y su extracción a
texto**, y las fuentes legibles por máquina van cada una a la suya
(`cmsis_v1.28.3/`, `cubemx/`). Las cuatro cabeceras que estuvieron sueltas en
`stm32f407xx/` eran copias byte a byte de las de `cmsis_v1.28.3/` y se han
quitado.

`doc/refs/src_snapshot.tgz` no es documentación de ST: es una instantánea del
árbol `src/` que se dejó ahí como copia de seguridad de trabajo.
