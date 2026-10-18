# Software de terceros en mcu-sim

*Qué software ajeno lleva dentro este proyecto, bajo qué licencia, y qué de
todo eso viaja con el ejecutable que se reparte. Está aquí porque algunas de
esas licencias obligan a que sus avisos acompañen al programa, también cuando
lo que se entrega es un binario.*

> **mcu-sim en sí es AGPLv3** (`LICENSE`). Este documento cubre **lo que no es
> nuestro**, que conserva su propia licencia. Todas las de abajo son
> permisivas y compatibles con la AGPLv3 en esta dirección —código Apache-2.0
> puede formar parte de una obra AGPLv3, no al revés—. Con la GPLv2 no lo
> habrían sido, y por eso esa versión quedó descartada desde el principio.

---

## 1. SystemC — Apache License 2.0

El simulador **es** un modelo SystemC: sin esta biblioteca no hay programa.

| | |
| :--- | :--- |
| Autor | Accellera Systems Initiative Inc. |
| Licencia | Apache License, Version 2.0 |
| Versiones usadas | 2.3.4 (Linux, Windows) y 3.0.2 (macOS, vía Homebrew) |
| Dónde | https://github.com/accellera-official/systemc |

El fichero `NOTICE` de SystemC atribuye código, además de a Accellera, a **Arm,
Cadence, Circuitsutra, COSEDA, Doulos, Ericsson, Fraunhofer, GreenSocs, Intel,
Mentor Graphics, NXP, OFFIS, STMicroelectronics y Synopsys**. No se reproduce
aquí a mano: el fichero original va en los paquetes que lo necesitan, con su
texto íntegro.

**Y aquí está la distinción que importa**, porque cambia según la plataforma:

| Paquete | ¿SystemC va dentro? | Qué se incluye |
| :--- | :--- | :--- |
| **Windows** | **Sí**, enlazada estáticamente | `LICENSE-SystemC.txt` y `NOTICE-SystemC.txt` |
| **Linux** | **Sí**, enlazada estáticamente | `LICENSE-SystemC.txt` y `NOTICE-SystemC.txt` |
| **macOS** | **No**: el alumno la instala con `brew install systemc` | Nada: no se redistribuye |

La sección 4 de la Apache-2.0 se aplica a distribuir «in Source **or Object**
form», así que un ejecutable con la biblioteca dentro tiene que llevar copia de
la licencia y trasladar las atribuciones del `NOTICE`. En macOS no hay nada que
trasladar: la biblioteca la obtiene el usuario de Accellera, por Homebrew, con
su licencia al lado.

## 2. Biblioteca de ejecución de GCC — GPLv3 **con la GCC Runtime Library Exception**

Los paquetes de Windows y de Linux enlazan `libstdc++` y `libgcc` de forma
estática. Eso **no** convierte el ejecutable en GPL, y no por una
interpretación: la GCC Runtime Library Exception existe exactamente para este
caso, y se aplica porque el modelo se compila con GCC de la manera normal —lo
que la excepción llama un *Eligible Compilation Process*—.

Conviene que esté escrito, porque es la duda que aparece siempre al ver un
`-static-libstdc++`.

## 3. Runtime de MinGW-w64 (solo Windows)

`-static` mete también `libwinpthread` y el runtime de MinGW-w64, con licencias
permisivas —**Zope Public License 2.1** para winpthreads y las de MinGW-w64 para
el resto—. Su aviso es el que acompaña a la distribución de MSYS2 con la que se
construye.

## 4. Lo que va en los firmwares, no en el ejecutable

Esto está en el árbol de fuentes, con su licencia propia al lado, y se compila
dentro de los **firmwares** de ejemplo y de las suites, no del simulador. Los
firmwares (`.bin`) **sí viajan en los paquetes** desde el 2026-10-06, en
`verif/fw/` como en el repositorio, y por eso van con ellos estas licencias, en
las mismas carpetas: es lo que piden al redistribuir en forma objeto.

| Qué | Dónde | Licencia |
| :--- | :--- | :--- |
| Cabeceras CMSIS-Core y de dispositivo de ST (`stm32f4xx.h` y familia) | `src/verif/fw/cmsis/` | `LICENSE-CMSIS-Core.md` y `LICENSE-cmsis_device_f4.md`, en la misma carpeta |
| CoreMark (EEMBC) | `src/verif/fw/coremark/` | `LICENSE.md`, en la misma carpeta |
| Cabeceras de dispositivo de Texas Instruments (`msp432p401r.h`, `msp_compatibility.h`) | `src/verif/fw/msp432p4/ti/` | BSD de tres cláusulas: `LICENSE-TI.md`, en la misma carpeta. Compatible con la AGPLv3 en esta dirección, como el Apache-2.0 de ST |
| DriverLib de Texas Instruments (entera, desde T4) y las cabeceras de su SDK | `src/verif/fw/msp432p4/sdk/` | BSD de tres cláusulas: `LICENSE-TI.md`, en esa carpeta, **salvo `rom.h` y `rom_map.h`**, que en esta copia llevan la nota antigua «TI Information - Selective Disclosure» (vease `LICENSE-TI.md`). Va compilada dentro de `driverlib_t3`, de los ejemplos del SDK y de la **ROM sustituta**, `rom_r1.bin`, que el simulador lleva dentro (`src/soc/msp432p4/rom_r1.inc`): con la del E4, las únicas piezas de terceros que van en el ejecutable y no solo en un firmware |
| DriverLib del MSP432E4 (entera) y las cabeceras, el arranque y el guion de enlazado de su SDK (desde T6) | `src/verif/fw/msp432e4/sdk/` | BSD de tres cláusulas, en todos los ficheros, también `rom.h` y `rom_map.h`: `LICENSE-TI.md`, en esa carpeta. Va compilada dentro de `e4_nv`, `e4_t3`, `e4_rom` y, desde T7, `e4_t7`, `e4_eth`, `e4_bootp` y `e4_usb`, y de la **ROM sustituta del E4**, `rom_r1.bin`, que el simulador también lleva dentro (`src/soc/msp432e4/rom_r1.inc`). El cargador serie de esa ROM (`src/verif/fw/msp432e4/rom_r1/cargador.c`) es del proyecto, AGPLv3: **no** lleva el `boot_loader` de TivaWare, cuya licencia no lo permitiría |
| lwIP 2.2.1 (un subconjunto: `src/core` sin IPv6, `src/include` y `src/netif/ethernet.c`, sin tocar), desde T7 | `src/verif/fw/msp432e4/e4_eth/lwip/` | BSD de tres cláusulas: `lwip/COPYING` y la cabecera de cada fichero; procedencia (etiqueta `STABLE-2_2_1_RELEASE`, commit `77dcd25a`) en `e4_eth/LEEME.md`. Va compilada solo dentro del firmware `e4_eth`, no en el simulador |
| El BSL sustituto del MSP432P4 (B1) | `src/verif/fw/msp432p4/bsl_b1/`, y dentro del simulador (`src/soc/msp432p4/bsl_b1.inc`) | Del proyecto, AGPLv3. **No** lleva el BSL de TI (paquete MSP432BSL): su «TI Text File License» solo permite usarlo con dispositivos de TI y prohíbe desensamblarlo, así que ni se versiona ni va dentro; quien lo tenga lo carga con `--bsl`. BSL Scripter (BSD) tampoco se versiona: se usa como herramienta de prueba |
| Ejemplos de DriverLib del SDK SimpleLink MSP432P4, con su arranque, su `system_msp432p401r.c` y su guion de enlazado para GCC | `src/verif/fw/msp432p4/sdk_ejemplos/` | BSD de tres cláusulas, en la cabecera de cada fichero; procedencia en `LEEME.md`, en esa carpeta |
| FreeRTOS: el núcleo V11.3.1 y la demo para la MSP432 LaunchPad | `src/verif/fw/msp432p4/freertos_blinky/` | MIT: `kernel/LICENSE.md` y la cabecera de cada fichero; procedencia en `LEEME.md`, en esa carpeta |

De CoreMark hay que saber una cosa más: **sus reglas prohíben publicar una
puntuación como «CoreMark» sin seguir su procedimiento de comunicación**. Aquí
no se publica ninguna: `coremark.bin` se usa como carga de trabajo para que el
tiempo simulado sea comparable entre máquinas (T-22 en `doc/todo.md`), y la
cifra que el proyecto vigila es el reloj del simulador, no una marca de
rendimiento del silicio.

## 5. Los manuales de ST, de TI y de ARM

No están en el repositorio, y no por tamaño. Están en `doc/refs/`, excluida por
`.gitignore`, junto con los informes técnicos internos que se escribieron
reproduciéndolos —lo que los hace obra derivada de ellos—. El porqué entero está
en la **§0 de `doc/fuentes.md`** y en **I-50** de `doc/todo.md`.

---

*Este fichero no es un dictamen legal; es el inventario de lo que hay y de dónde
está cada licencia, hecho leyendo los ficheros que acompañan a cada pieza.*
