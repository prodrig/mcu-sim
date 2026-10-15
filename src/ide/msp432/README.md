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
