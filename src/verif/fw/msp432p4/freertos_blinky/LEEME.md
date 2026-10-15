# freertos_blinky — La demo de FreeRTOS para la MSP432 LaunchPad (T4)

La demo `CORTEX_M4F_MSP432_LaunchPad_IAR_CCS_Keil` del repositorio de FreeRTOS,
en su versión «simply blinky» (`configCREATE_SIMPLE_TICKLESS_DEMO = 1`), **sin
modificar**: dos tareas y una cola; la que envía se despierta cada 1000 ms con
`vTaskDelayUntil`, la que recibe enciende P1.0 50 ms. Con *tickless idle* a
100 Hz, así que el núcleo duerme entre medias, y S1 lo despierta por PORT1.
Usa DriverLib con `MAP_`: es el ejemplo con RTOS del cierre de T4 (P67).

| Carpeta | Qué es | Procedencia |
| :--- | :--- | :--- |
| `demo/` | `main.c`, `FreeRTOSConfig.h` y `SimplyBlinkyDemo/main_blinky.c` | `github.com/FreeRTOS/FreeRTOS`, revisión `f4fcc3b` (2026-08-26), `FreeRTOS/Demo/CORTEX_M4F_MSP432_LaunchPad_IAR_CCS_Keil` |
| `kernel/` | El núcleo: `tasks.c`, `queue.c`, `list.c`, `timers.c`, el port `GCC/ARM_CM4F` y `heap_4.c` | `github.com/FreeRTOS/FreeRTOS-Kernel` V11.3.1, revisión `3a22924`, la que fija como submódulo esa revisión del repositorio de la demo |

Licencia: **MIT**, en `kernel/LICENSE.md` y en la cabecera de cada fichero.

La demo trae proyectos de IAR, CCS y Keil, pero no de GCC: se compila con el
arranque, el `system_msp432p401r.c` y el guion de enlazado de GCC de los
ejemplos del SDK (`../sdk_ejemplos/comun`), y con su DriverLib.

    make            # freertos_blinky.out y .bin; las huellas, en verif/fw/huellas.txt
