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

## 4. Lo que está en el repositorio pero **no** en el ejecutable

Esto no viaja en los paquetes; está en el árbol de fuentes, con su licencia
propia al lado, y se compila dentro de los **firmwares** de las suites, no del
simulador.

| Qué | Dónde | Licencia |
| :--- | :--- | :--- |
| Cabeceras CMSIS-Core y de dispositivo de ST (`stm32f4xx.h` y familia) | `src/verif/fw/cmsis/` | `LICENSE-CMSIS-Core.md` y `LICENSE-cmsis_device_f4.md`, en la misma carpeta |
| CoreMark (EEMBC) | `src/verif/fw/coremark/` | `LICENSE.md`, en la misma carpeta |

De CoreMark hay que saber una cosa más: **sus reglas prohíben publicar una
puntuación como «CoreMark» sin seguir su procedimiento de comunicación**. Aquí
no se publica ninguna: `coremark.bin` se usa como carga de trabajo para que el
tiempo simulado sea comparable entre máquinas (T-22 en `doc/todo.md`), y la
cifra que el proyecto vigila es el reloj del simulador, no una marca de
rendimiento del silicio.

## 5. Los manuales de ST y de ARM

No están en el repositorio, y no por tamaño. Están en `doc/refs/`, excluida por
`.gitignore`, junto con los informes técnicos internos que se escribieron
reproduciéndolos —lo que los hace obra derivada de ellos—. El porqué entero está
en la **§0 de `doc/fuentes.md`** y en **I-50** de `doc/todo.md`.

---

*Este fichero no es un dictamen legal; es el inventario de lo que hay y de dónde
está cada licencia, hecho leyendo los ficheros que acompañan a cada pieza.*
