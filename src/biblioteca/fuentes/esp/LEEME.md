# `fuentes/esp/`: el patillaje de los ESP32-C3 y ESP32-C6

Fase B4b de `doc/analisis_build.md`. Dos fuentes:

* `TIPO.pines`: la **transcripción** de la tabla «Pin Overview» del apéndice
  de cada datasheet de Espressif. Trae cada patilla, su nombre y, por GPIO,
  las funciones del IO MUX con su número (F0, F2; F1 es siempre el GPIO),
  las analógicas y las del LP IO MUX (C6). La escribe
  `herramientas/extrae_esp.py` (`make biblioteca-fuentes REFS=...`) y no se
  edita a mano.

  | Ficheros | Datasheet |
  | :--- | :--- |
  | `ESP32-C3*.pines` | *ESP32-C3 Series Datasheet* v2.4 |
  | `ESP32-C6*.pines` | *ESP32-C6 Series Datasheet* v1.1 |

  La C3FH4X no conecta las patillas 19 a 24 (nota 9 de la tabla 2-1): van
  como `NC`.

* `esp32c3/gpio_sig_map.h` y `esp32c6/gpio_sig_map.h`: el índice de cada
  señal de la **matriz GPIO**, copiado de ESP-IDF v5.3.1
  (`components/soc/<chip>/include/soc/`, commit
  `c8fc5f643b7a7b0d3b182d3df610844e3dc9bd74`). Licencia Apache-2.0, que va en
  la cabecera de cada fichero. Es la versión legible por máquina de la tabla
  del TRM, y la misma de la que salen `c3::SENALES` y `c6::SENALES` del
  modelo. No se edita: para pasar a otra versión de ESP-IDF se copian los
  mismos ficheros y se cambia `IDF` en `fuente_esp.py`.
