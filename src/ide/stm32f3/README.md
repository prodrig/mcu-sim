# Depurar el STM32F303VC del simulador desde un IDE (fase S2 del F303)

El simulador hace de sonda: abre un servidor de GDB en el puerto 3333 y
cualquier IDE que sepa hablar con un `gdbserver` remoto lo usa como si fuera
la STM32F3DISCOVERY. No hace falta el ST-LINK; hace falta el
**`arm-none-eabi-gdb`** (o `gdb-multiarch`), no el `gdb` del PC, que no lleva
ARM (la explicación larga está en `cubeide/README.md`, «Truncated register 18»).

| Fichero | Para |
| :--- | :--- |
| `cubeide/simulador_f303.launch` | STM32CubeIDE, *GDB Hardware Debugging* contra el stub (la misma que `cubeide/simulador.launch`) |
| `vscode/launch.json` | VS Code: Cortex-Debug contra el stub, Cortex-Debug con OpenOCD por CMSIS-DAP, y el depurador C/C++ de Microsoft |
| `openocd/mcu-sim-cmsis-dap_stm32f303.cfg` | OpenOCD contra el simulador por CMSIS-DAP sobre TCP (`--cmsis-dap`, el 4441): la rápida; hace falta un OpenOCD con `cmsis-dap tcp` (xPack 0.12.0-7 o de git) |
| `openocd/mcu-sim_stm32f303.cfg` | OpenOCD contra el simulador por `remote_bitbang` (`--openocd`, el 3335), para un OpenOCD sin `cmsis-dap tcp` |

Contra una STM32F3DISCOVERY de verdad no hace falta nada de aquí: OpenOCD trae
`board/stm32f3discovery.cfg` (el ST-LINK de la placa y el mismo
`target/stm32f3x.cfg`).

## 1. El simulador, escuchando

```
./build/mcu-sim placas/stm32f3discovery.xml --gdb-dap --tiempo-real
```

Tiene que decir `[gdb-dap] escuchando en localhost:3333` antes de darle a
depurar. Las dos formas de conectar el depurador dan lo mismo (lo comprueba el
grupo F19 de `testf3`):

| Opción | Por dónde | Cuándo |
| :--- | :--- | :--- |
| `--gdb-dap` | el stub habla directamente con el DAP del núcleo | lo normal: más rápido |
| `--gdb` | una sonda SWD de verdad, bit a bit por PA14 (SWCLK) y PA13 (SWDIO) | para ver el SWD en los pines |

Con `--cmsis-dap` o `--openocd` el simulador es la sonda y el servidor de GDB
es el de OpenOCD (los `.cfg` de `openocd/`). `--port=N` cambia el puerto, y
`--tiempo-real` ata el tiempo simulado al reloj: sin él, LD3 parpadea cientos de
veces por segundo.

## 2. STM32CubeIDE

*File → Import… → Run/Debug → Launch Configurations*, la carpeta `cubeide`. En
la configuración importada, `TU_PROYECTO` → el nombre del proyecto. El resto,
como en `cubeide/README.md`: el *debug probe* es *GDB Hardware Debugging*, no
el ST-LINK, y el `arm-none-eabi-gdb` es el que trae CubeIDE.

Un proyecto nuevo de CubeIDE para la STM32F3DISCOVERY (o un ejemplo de
STM32CubeF3) se depura tal cual: el reloj de 72 MHz por el HSE llega de los
8 MHz del MCO del ST-LINK, que la placa del modelo da en PF0 (con
`RCC_HSE_ON` o con `RCC_HSE_BYPASS`, como en la placa).

## 3. VS Code

Copiar `vscode/launch.json` a `.vscode/launch.json` del proyecto y cambiar la
ruta del `.elf`. Para la vista de periféricos de Cortex-Debug, `"svdFile"` con
el `STM32F303.svd` del paquete de ST.

## Lo que hace la sesión, y por qué

```
monitor reset halt                  el stub resetea con VC_CORERESET: parado en Reset_Handler
maintenance flush register-cache    GDB no relee los registros tras un `monitor`
load                                el stub programa la Flash por el FPEC
monitor reset halt                  otra vez al principio, con el programa nuevo
maintenance flush register-cache
(break en main, continue)
```

`load` funciona como con la placa: el stub hace lo que hace el controlador
`stm32f1x` de OpenOCD cuando escribe por el DAP —las dos llaves en
`FLASH_KEYR`, el borrado de cada página de 2 KB (`PER`, `FLASH_AR`, `STRT`),
`PG` y medias palabras (el FPEC del F3 da error de bus con una palabra, así que
la sonda pone `CSW.Size` a 16 bits), `BSY`, `PGERR` y `WRPRTERR`— y al final
lee de vuelta y vuelve a echar `LOCK`.

## Lo que está comprobado y lo que no

Comprobado:

* `testf3` F19: GDB por `--gdb` y por `--gdb-dap`, `load` de `f3_demo` sobre
  CoreMark, lectura de vuelta, `monitor reset halt`, watchpoint, punto de
  ruptura, paso y continuar;
* con `gdb-multiarch` contra `placas/stm32f3discovery.xml`, por las dos vías,
  la secuencia de arriba con `f3_demo.elf`: `compare-sections` da todo igual y
  `tbreak main` para en `main`;
* `make openocd` (O5) y `make cmsis-dap` (C6): OpenOCD con
  `target/stm32f3x.cfg` sin tocar programa y verifica (`program ... verify
  reset`), identifica el chip (`device id = 0x10030422`, `flash size = 256
  KiB`), congela los perros en el DBGMCU, y GDB por su servidor hace `load` y
  `compare-sections` (G3).

**No** está probado dentro de CubeIDE ni de VS Code: los ficheros siguen la
documentación de cada uno y la configuración de CubeIDE del F407, que sí se
probó en el IDE (`cubeide/README.md`). Si algo falla, `--traza-gdb` en el
simulador enseña los paquetes.

Lo que no está: la protección de lectura (RDP) se carga pero no bloquea al
depurador (`limitaciones()` del chip lo dice al arrancar), y el cargador de
sistema de ST (`stm32flash` por la USART1) es la fase S7, opcional.
