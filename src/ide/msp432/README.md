# Depurar el MSP432P401R del simulador desde un IDE (fase T2)

El simulador hace de sonda: abre un servidor de GDB en el puerto 3333 y
cualquier IDE que sepa hablar con un `gdbserver` remoto lo usa como si fuera
la placa. No hace falta el XDS110 ni el driver de TI; hace falta el
**`arm-none-eabi-gdb`** (o `gdb-multiarch`), no el `gdb` del PC, que no lleva
ARM (la explicación larga está en `cubeide/README.md`, «Truncated register 18»).

Aquí hay dos configuraciones:

| Fichero | Para |
| :--- | :--- |
| `ccs/simulador_msp432.launch` | Code Composer Studio de Eclipse (hasta la 12), tipo *GDB Hardware Debugging* |
| `vscode/launch.json` | VS Code, con Cortex-Debug o con el depurador C/C++ de Microsoft |

Y, para depurar **con OpenOCD** en vez de con el stub —contra el simulador
(`--openocd`) o contra una LaunchPad de verdad por su XDS110—, con el paso a
paso en `doc/analisis_xds110.md` §8:

| Fichero | Para |
| :--- | :--- |
| `openocd/mcu-sim_msp432p401r.cfg`, `openocd/mcu-sim_msp432e401y.cfg` | OpenOCD contra el simulador (`remote_bitbang` en el 3335) |
| `openocd/launchpad_msp432e401y.cfg` | OpenOCD contra una MSP-EXP432E401Y por su XDS110 (para la P401R, el `board/ti/msp432-launchpad.cfg` de OpenOCD) |
| `ccs/openocd_msp432.launch` | CCS 11 y 12, *GDB Hardware Debugging* contra OpenOCD |
| `ccs20/launch.json`, `ccs20/tasks.json` | CCS 20 (Theia) con la extensión Cortex-Debug: arranca `mcu-sim` y OpenOCD |

## 1. El simulador, escuchando

```
./build/mcu-sim placas/msp_exp432p401r.xml --gdb-dap --tiempo-real
```

Tiene que decir `[gdb-dap] escuchando en localhost:3333` antes de darle a
depurar. Las dos formas de conectar el depurador dan lo mismo (lo comprueba
el grupo P21 de `testp4`):

| Opción | Por dónde | Cuándo |
| :--- | :--- | :--- |
| `--gdb-dap` | el stub habla directamente con el DAP del núcleo | lo normal: más rápido |
| `--gdb` | una sonda SWD de verdad, bit a bit por SWCLKTCK/SWDIOTMS | para ver el SWD en los pines, o si la placa tiene otra cosa en ellos |

También se puede elegir en la placa, con `depuracion="dap"` o `"pines"` en el
`<mcu>`. `--port=N` cambia el puerto, y `--tiempo-real` ata el tiempo simulado al
reloj: sin él, un LED a 1 Hz parpadea cientos de veces por segundo.

## 2. Code Composer Studio

*File → Import… → Run/Debug → Launch Configurations*, esta carpeta (`ccs`).
En la configuración importada:

* `TU_PROYECTO` → el nombre del proyecto, y el programa → su `.out`
  (normalmente `Debug/TU_PROYECTO.out`);
* *Debugger → GDB Command* → la ruta **completa** del `arm-none-eabi-gdb` de
  la toolchain GNU de CCS, en `ccs/tools/compiler/gcc-arm-none-eabi_<versión>/bin/`.
  Si no se instaló, se añade desde el gestor de compiladores de CCS, o se usa
  el de Arm.

No se usa el *Target Configuration* (`.ccxml`) de TI: ese camino va al XDS110
por el *debug server* de TI, que no habla GDB. El proyecto puede estar compilado
con TI Arm Clang o con GCC: el `.out` es un ELF con DWARF en los dos casos.

Las versiones de CCS basadas en Theia (la 20 en adelante) no traen el tipo de
lanzamiento de CDT; ahí vale la configuración de VS Code, `cppdbg` o
Cortex-Debug, según las extensiones que tenga.

## 3. VS Code

Copiar `vscode/launch.json` a `.vscode/launch.json` del proyecto y cambiar la
ruta del `.out`. Trae dos entradas:

* **Cortex-Debug** (`servertype: external`): con la vista de periféricos si se
  le da el SVD del MSP432P401R (`"svdFile"`, del paquete CMSIS de TI).
* **cppdbg**, para quien no tenga extensiones de ARM.

## Lo que hace la sesión, y por qué

```
monitor reset halt                  el stub resetea con VC_CORERESET: parado en Reset_Handler
maintenance flush register-cache    GDB no relee los registros tras un `monitor`
load                                el stub programa la Flash por el FLCTL
monitor reset halt                  otra vez al principio, con el programa nuevo
maintenance flush register-cache
(break en main, continue)
```

`load` funciona como con la placa: el stub hace lo que hace el algoritmo de
Flash de TI para OpenOCD —quita la protección del sector (`BANKx_MAIN_WEPROT`),
lo borra, programa en ráfagas de cuatro líneas de 128 bits, mira `ADDR_ERR` y
`PRG_ERR`, lee de vuelta y vuelve a proteger—. Un programa que pise el TLV
(`0x0020_1000`) falla diciéndolo, porque ese sector es de TI y no se borra.

Con el núcleo parado el perro guardián no corre: `SYSCTL_PERIHALT_CTL` sale del
reset con `HALT_WDT` puesto, como en el chip. Si el programa lo quita, el perro
resetea aunque el depurador tenga el núcleo parado; también como en el chip.

## Lo que está comprobado y lo que no

Comprobado con `gdb-multiarch` contra `placas/msp_exp432p401r.xml`, por
`--gdb` y por `--gdb-dap`, con `verif/fw/msp432p4/p4_demo/p4_demo.out`: la
secuencia de arriba por consola y por **MI** —el protocolo que usan los dos
IDE— con `-target-download`, `-break-insert -t main`, `-exec-continue`,
`-exec-next` y `-exec-interrupt` (la pausa contesta SIGINT, en `espera()`);
`compare-sections` da todo igual tras el `load`.

**No** está probado dentro de CCS ni de VS Code: los dos ficheros siguen la
documentación de cada uno y la configuración de CubeIDE, que sí se probó en el
IDE (`cubeide/README.md`). Si algo falla, `--traza-gdb` en el simulador enseña
los paquetes, y es lo primero que hay que mirar.

Lo que falta, aparte: el mapa de memoria (`qXfer:memory-map:read`, I-29), así
que GDB carga con paquetes `M`/`X` en vez de `vFlashWrite` —funciona igual,
porque el stub reconoce el rango de la Flash—.

El puerto serie de vuelta del XDS110 (backchannel UART, la UCA0 en
P1.2/P1.3) sí está desde T3: en la LaunchPad es el `PuenteSerie` BCL, por RFC
2217 en `localhost:3356`, donde un terminal (o `--serie BCL=...`) lo recoge
mientras se depura.

**La ROM de DriverLib** está desde T4: los proyectos que llaman a `MAP_xxx()`
—todos los ejemplos del SDK— saltan por `ROM_APITABLE` a la ROM sustituta
(R1), sin tocar nada. Para entrar en ella paso a paso con sus fuentes, se le
dan a GDB los símbolos de la R1, que están en el repositorio:

```
(gdb) add-symbol-file src/verif/fw/msp432p4/rom_r1/rom_r1.out
```

Si se arranca el simulador con `--rom .../msp432_driverlib_rom_image.out` (la
ROM de TI que instala CCS), los símbolos son los de ese mismo fichero, como en
CCS con la placa (*Run > Load > Add Symbols*).

## OpenOCD por los pines (T5)

La tercera forma de colgarse del chip: el simulador hace de **adaptador
`remote_bitbang`** y es OpenOCD el que genera el SWD bit a bit, con sus
propios controladores. Sirve para programar como en una placa y para
depurar con el soporte de RTOS de OpenOCD (los hilos de FreeRTOS en GDB).

```
./build/mcu-sim placas/msp_exp432p401r.xml --openocd --tiempo-real
#   [openocd] remote_bitbang escuchando en localhost:3335

openocd -c "adapter driver remote_bitbang" -c "remote_bitbang host localhost" \
        -c "remote_bitbang port 3335" -c "transport select swd" \
        -f target/ti/msp432.cfg \
        -c "program Debug/blinky.out verify reset exit"
```

Para depurar, sin el `program`: OpenOCD abre su servidor de GDB en el
**3333**, y los IDE de arriba se conectan a él igual que al simulador (con
`-c "msp432.cpu configure -rtos FreeRTOS"` antes de `init`, GDB ve las
tareas). Con un STM32 es lo mismo con `-f target/stm32f4x.cfg`.

**Hace falta un OpenOCD de git**: la 0.12.0, la última publicada y la que
traen las distribuciones, no tiene SWD en `remote_bitbang` y contesta «only
one transport option; autoselect 'jtag'». `ci/construye_openocd.sh` construye
la revisión con la que se probó. El SWD por socket es lento —unos 1,5 KiB/s
programando—, porque cada bit leído es un viaje por TCP.

## El MSP432E401Y (T6)

Lo mismo vale para el E4, con su placa y su firmware:

```
./build/mcu-sim placas/msp_exp432e401y.xml --gdb-dap --tiempo-real
```

En las configuraciones, el programa es el `.out` del proyecto del E4 y el
dispositivo, `MSP432E401Y`; el resto no cambia. `--gdb` cuelga la sonda de
**PC0/PC1** (SWCLK/SWDIO), que salen del reset en su función JTAG/SWD:
quitarles `AFSEL` con `LOCK`/`CR` abiertos deja la placa sin depurador, como
la de verdad. El `load` de GDB borra y programa por el controlador de Flash
del E4 (sectores de 16 KB); el grupo E18 de `teste4` hace la misma sesión por
los dos caminos (G1 y G2).

Y sin depurador: con la placa **sin firmware** la Flash queda en blanco y el
chip arranca el cargador serie de su ROM, al que se le habla por el
*backchannel* (el 3357, RFC 2217):

```
./build/mcu-sim placas/msp_exp432e401y.xml --sin-firmware
python3 verif/serie/sflash_e4.py rfc2217://localhost:3357 mi_programa.bin -r 0
```

o, en Windows, con un redirector de COM a RFC 2217, `sflash -c N -p 0 -r 0
mi_programa.bin` de TivaWare. OpenOCD contra el E4 no está probado (P-17 n).
