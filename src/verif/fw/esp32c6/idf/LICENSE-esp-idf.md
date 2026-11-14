# ESP-IDF — licencia de los binarios de `idf-v*.tar.xz`

`idf-v5.5.tar.xz` e `idf-v6.0.tar.xz` llevan tres ejemplos de
[ESP-IDF](https://github.com/espressif/esp-idf) compilados para el ESP32-C3
con `construye.sh` (ESP-IDF v5.5.6 y v6.0.3, `riscv32-esp-elf-gcc` 14.2.0 y
15.2.0 de Espressif): de cada uno, `flash_args`, el bootloader de segunda
etapa, la tabla de particiones y la aplicación.

* Los ejemplos (`examples/get-started/hello_world`, `blink` y
  `examples/peripherals/uart/uart_echo`) son de dominio público o CC0, como
  dice su cabecera.
* ESP-IDF, que va enlazado en los binarios, es Apache-2.0
  (<https://www.apache.org/licenses/LICENSE-2.0>), salvo los componentes de
  terceros que lleva con su propia licencia: FreeRTOS (MIT), newlib (BSD y
  similares), mbedTLS (Apache-2.0) y los que lista su
  `tools/ci/check_copyright_config.yaml`.

Lo único que se cambia respecto a los ejemplos tal cual: en `blink`, el LED es
un GPIO (GPIO8, 200 ms) y el `#include "led_strip.h"` va dentro de su
`#ifdef`, para no necesitar el componente `led_strip` del registro de
Espressif.
