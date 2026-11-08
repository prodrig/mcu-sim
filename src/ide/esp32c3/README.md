# Depurar el ESP32-C3 del simulador desde un IDE

Fase E6 de `doc/analisis_esp32_riscv.md`. Tres caminos, como con una placa:

| Camino | Simulador | Cliente |
| :--- | :--- | :--- |
| **G1**: el stub de GDB pegado al Debug Module (el más rápido) | `--gdb-dap` (puerto 3333) | `riscv32-esp-elf-gdb`, `idf.py gdb`, la extensión de ESP-IDF, Espressif-IDE |
| **G2'**: el mismo stub por los pines del JTAG | `--gdb --jtag=pines` | igual |
| **G2**: openocd-esp32 con `target/esp32c3.cfg` sin tocar | `--openocd --jtag=pines` (remote_bitbang, 3335) o `--cmsis-dap --jtag=pines` (4441) | `openocd -f ide/esp32c3/openocd/mcu-sim_esp32c3.cfg` y GDB a su 3333 |

El C3 de fábrica tiene el JTAG en el USB-Serial-JTAG, no en los pines
MTMS-MTDO: `--jtag=pines` (o `jtag="pines"` en el `<mcu>` de la placa) es como
quemar `EFUSE_DIS_USB_JTAG`. G1 no lo necesita.

```bash
# El simulador, con la aplicacion en su Flash
./build/mcu-sim placas/esp32c3_devkitm1.xml PROYECTO/build/flash_args --gdb-dap --tiempo-real
# GDB, como hace idf.py gdb
riscv32-esp-elf-gdb -ex "target extended-remote :3333" -ex "monitor reset halt" \
    -ex "maintenance flush register-cache" -ex "thb app_main" -ex c PROYECTO/build/PROYECTO.elf
```

* `monitor reset halt` es un reset de la CPU (como `esp_restart()`): la ROM
  vuelve a arrancar el bootloader y la aplicación, parada en su primera
  instrucción.
* Puntos de ruptura: en la Flash (0x4200_0000) por *trigger* (8, `hbreak` o
  `thbreak`); en la IRAM, también por `ebreak` (el stub escribe la palabra
  entera: la IRAM no admite medias palabras por el bus de datos).
* `load` escribe lo que va a la Flash por las ventanas de la MMU (las páginas
  que el bootloader dejó mapeadas): sirve para recargar la misma aplicación
  recompilada con ella parada. Para programar de verdad, esptool por la UART
  (E5) o `program_esp` de OpenOCD.
* Con OpenOCD, `--flash-rapida`: el flasher de openocd-esp32 espera con plazos
  de reloj de pared y la simulación no va a tiempo real.

Ficheros:

* `openocd/mcu-sim_esp32c3.cfg` y `openocd/mcu-sim-cmsis-dap_esp32c3.cfg`;
* `vscode/launch.json`: la extensión de ESP-IDF (adaptador `gdbtarget`), con
  G1 y con OpenOCD, y `cppdbg` sin extensiones;
* `espressif-ide/simulador_esp32c3.launch`: «GDB Hardware Debugging» de CDT
  contra G1.

Lo comprobado (`make openocd-c3`): IDCODE, examine, halt y `program_esp` +
verify + reset por remote_bitbang, lo mismo por CMSIS-DAP, y GDB por G1 hasta
`app_main` con su `bt`. **Pendiente**: GDB a través de openocd-esp32 después
de su `reset halt` (el punto de ruptura no llega a GDB) y por tanto
`info threads` con las tareas de FreeRTOS por OpenOCD.
