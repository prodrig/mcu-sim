# Lo que falta en los ESP32, de más a menos importante

El mismo baremo que en `resumen_todo_msp430.md`: cuánto puede ocultar la
simulación que un programa falla en el chip real. Primero lo que funciona aquí y
falla allí sin avisar, luego lo que impide usar las herramientas de verdad,
después lo declarado o a medias y al final lo menor.

Abarca el ESP32-C3 y el ESP32-C6 (RISC-V), fases E0-E13 de
`doc/analisis_esp32_riscv.md`. Lo marcado [modelo] en ese documento es una
elección del modelo sin fuente de Espressif.

Fuente: `doc/analisis_esp32_riscv.md` (§10 y §11), `doc/analisis_wifi_bl.md` y
`doc/todo.md`, en el estado del commit e64916a.

## Cómo leer el coste

La misma escala y las mismas marcas que en `resumen_todo_msp430.md`: **Bajo**
(< ~200 líneas, horas a dos días), **Medio** (~200-1 000 líneas, días a dos
semanas), **Alto** (> ~1 000 líneas o rediseño, semanas), **Muy alto** (proyecto
aparte o información sin publicar); **[placa]** medir en una placa real,
**[herramienta]** probar a mano una herramienta de terceros, **[doc]**
documentación que falta, **[invariantes]** mueve los invariantes de las suites,
**[decisión]** hay que decidir antes qué se quiere.

En los ESP32 pesa más que en los demás la marca **[doc]**: Espressif no publica
la microarquitectura, el formato de varias interfaces ni la radio, y muchas
cifras del modelo son elecciones. Casi todo lo que se puede cerrar se cierra
midiendo en una DevKit C3 y una C6.

## 1. El programa no puede funcionar, o funciona aquí y falla en el chip

1. **No hay radio.** FE, NRX y BB están declarados; E12 (opcional) depende de
   R1-R3 de `doc/analisis_wifi_bl.md`, que está solo analizado. Cualquier
   programa con WiFi o BLE, que en estos chips es casi todo, no puede ejecutarse.
   Es con diferencia lo más importante.
   *Coste:* **Muy alto**. La radio real no se puede modelar: **[doc]** el PHY
   y la banda base de Espressif son binarios cerrados. El camino del análisis es
   paravirtual: R1 (`MedioRadio`, `PuntoAcceso`), R2-R3 y luego E12
   (`VirtWifi`, «Grande» en la tabla de fases), que exige una opción de
   ESP-IDF que sustituya el controlador de WiFi. **[decisión]** si se acepta
   que el firmware del alumno se compile con esa opción. **[herramienta]** el
   `station_example` de ESP-IDF a mano contra un servidor del ordenador. BLE,
   aparte (R7, HCI hacia RootCanal o Bumble), también **Alto**.
2. **La microarquitectura no está publicada**: los ciclos y el BTB están
   calibrados por el modelo (CoreMark); el tiempo de ejecución es aproximado.
   *Coste:* **Medio**, casi todo medida. **[doc]** no hay manual de la
   microarquitectura; **[placa]** una batería de microbucles con `mcycle` en una
   DevKit C3 y una C6 (saltos, cargas, multiplicación, fallos de caché).
   **[invariantes]**.
3. **La CPU tras el reset va a XTAL/2 = 20 MHz**, pero la ROM cuenta 40 ticks
   por µs: los retardos de la ROM salen el doble. Falta comprobarlo en una placa.
   *Coste:* **Bajo** una vez medido. **[placa]** `esp_rom_delay_us()` con un pin
   y un analizador lógico, antes de que el cargador de segunda etapa cambie el
   reloj. **[invariantes]** si se corrige.
4. **MSPI y caché**: el tiempo de una transacción y de un fallo de caché es de
   modelo; las operaciones de EXTMEM son instantáneas.
   *Coste:* **Medio**. **[placa]** medir fallos de caché con un bucle que
   recorra la flash a pasos de 32 bytes; **[invariantes]**.
5. **La calibración del reloj lento es nominal**, sin deriva: un programa que
   duerma con RTC slow clock despierta aquí a la hora exacta.
   *Coste:* **Bajo-medio**. Las tolerancias (±5 % del RC de 150 kHz) están en la
   hoja de datos; **[decisión]** deriva opcional y con semilla.
6. **SYSCON_RND_DATA es un xorshift con semilla fija**: los números aleatorios
   son siempre los mismos (claves, *nonces*, retrasos aleatorios).
   *Coste:* **Bajo**. **[decisión]** semilla variable por omisión y fija con
   una opción (las suites la fijan para no mover los invariantes).
7. **Los valores analógicos de REGI2C, declarados**: no hay PLL que enganchar.
   *Coste:* **Medio**. **[doc]** los registros de REGI2C no están documentados
   (se conocen por el código de ESP-IDF): modelar solo los que lee el firmware.
8. **RTC_CNTL**: el *super watchdog*, el *brownout*, CLK_CONF y ANA_CONF,
   declarados.
   *Coste:* **Medio** en total; el *super watchdog* y el *brownout*, **Bajo**
   cada uno y documentados en el TRM.
9. **UART con autobaud sin filtro de glitches.**
   *Coste:* **Bajo**.
10. **Latencia de interrupción de modelo**; ciclos del SHA de modelo.
    *Coste:* **Bajo** en código. **[placa]** medir los dos (un pin en la
    rutina; `esp_sha` cronometrado con `mcycle`).
11. **La HP SRAM se conserva en deep sleep [modelo]**; la LP SRAM vista desde
    HP tarda 2 ciclos de APB [modelo]: un programa que confíe en la SRAM tras
    deep sleep funciona aquí.
    *Coste:* **Bajo**. **[placa]** comprobar en una C6 qué queda en la HP SRAM
    tras deep sleep; **[doc]** el TRM dice que se apaga, así que probablemente
    basta con borrarla (o llenarla de basura con semilla).
12. **ETM instantáneo.**
    *Coste:* **Bajo**; **[placa]** medir el retardo de una tarea ETM de GPIO a
    GPIO.
13. **El SAR ADC**, presumiblemente ideal.
    *Coste:* **Medio**, con el modelo de ruido común a los demás MCU. **[placa]**
    los ADC de estos chips tienen mucho error sin calibrar: medir la curva real
    y usar la calibración de los eFuses.

## 2. No se pueden usar las herramientas reales

14. **USB-Serial-JTAG**: el formato de las órdenes es de modelo.
    *Coste:* **Medio**. **[doc]** Espressif no lo publica; se deduce del
    controlador de OpenOCD-esp32. **[herramienta]** OpenOCD-esp32 a mano, y luego
    en el CI.
15. **La disposición de la región DM** es de modelo.
    *Coste:* **Bajo-medio**; mismo origen que el 14 (el *debug module* sigue la
    norma RISC-V Debug 0.13, la disposición concreta no).
16. **La secuencia de reset por RFC 2217** la reconoce el puente; no hay
    esptool contra un USB real.
    *Coste:* **Bajo** lo que hay; esptool contra USB real necesita el punto 17.
17. **Sin USB/IP**: el USB-Serial-JTAG no llega al anfitrión como dispositivo.
    *Coste:* **Alto** (U4 de `doc/analisis_puente_usb.md`, compartido con los
    demás MCU) más el USB-Serial-JTAG como dispositivo. **[herramienta]** esptool,
    `idf.py monitor` y OpenOCD-esp32 a mano en Linux y Windows.
18. **D10-D11 del C6 (GDB G1/G2') fallan con ASan** (V-12), sin explicar.
    *Coste:* **Bajo-medio**, depuración. Puede ser un fallo real escondido.

## 3. Declarado o a medias (se nota al usarlo)

19. **Flash**: auto-suspensión y PMS de las ventanas, declarados.
    *Coste:* **Medio**; la auto-suspensión necesita los tiempos de la flash
    concreta de la DevKit (**[doc]** su hoja de datos).
20. **eFuse**: programar no programa (la lectura es instantánea).
    *Coste:* **Bajo-medio**. **[decisión]** dónde se guarda lo programado (un
    fichero junto a la placa), porque en el chip es irreversible.
21. **Arranque seguro y eFuses de seguridad**, declarados (el C3 puede arrancar
    de fábrica una flash cifrada por XTS_AES).
    *Coste:* **Medio-alto**. **[herramienta]** `espsecure.py` e `idf.py` con
    secure boot v2, a mano. Depende del 20.
22. **Deep sleep**: los dominios de potencia, solo lo que necesita LP.
    *Coste:* **Medio**.
23. **C6**: PARL_IO, ECC, SDIO esclavo y el codificador de traza, declarados; el
    monitor de memoria solo con registros; el chopper de MCPWM sin modelar; APM y
    TEE declarados en parte.
    *Coste:* **Alto** en total. PARL_IO y ECC, **Medio** cada uno; el SDIO
    esclavo necesita un maestro SDIO (**Alto**); el chopper, **Bajo**; APM/TEE,
    **Medio**.
24. **C6, pruebas ACT**: 18 difieren por diseño.
    *Coste:* **Bajo**: documentar cada diferencia junto a la prueba, si no lo
    está.

## 4. Menor

25. Consumo.
    *Coste:* **Medio**; **[placa]** con un medidor de corriente en la DevKit.
    Sin radio, el consumo que más importa (el de la WiFi) no existe todavía.
26. La imagen de ROM está reconstruida: 8 bytes distintos de la de QEMU, que
    nada referencia.
    *Coste:* **Bajo**: aclarar los 8 bytes contra `esp-rom-elfs`.
27. El resto de E12 (radio), cuando se haga 1.

## Balance del coste

* **Barato y con efecto**: 3, 6, 9, 11 y 12, cada uno de un día, y 18 por si
  esconde un fallo. Una semana con la placa.
* **Medio**: 2, 4 y 13 (calibrar el tiempo y el ADC midiendo), 14 y 15 (dejan
  OpenOCD-esp32 trabajar como contra el chip).
* **Muy caro**: 1, la radio, que es lo que más pesa en el baremo y lo que más
  cuesta: el camino paravirtual del análisis es de meses y pide que el firmware
  del alumno se compile con una opción distinta.
* **Trabajo a mano**: una sesión con una DevKit C3 y una C6 (analizador lógico y
  `mcycle`) cierra 3, 10, 11 y 12 y da los datos de 2 y 4. Es la más rentable de
  los cuatro documentos, porque aquí casi todo lo que falta es una cifra que
  Espressif no publica.

## Por dónde empezar

Por 1: sin radio, el simulador solo sirve para la parte del programa que no usa
WiFi ni BLE. Después, 3 (comprobarlo en una placa es barato y cambia todos los
retardos de la ROM) y 6, que es sencillo de arreglar con una semilla variable.
