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

## 1. El programa no puede funcionar, o funciona aquí y falla en el chip

1. **No hay radio.** FE, NRX y BB están declarados; E12 (opcional) depende de
   R1-R3 de `doc/analisis_wifi_bl.md`, que está solo analizado. Cualquier
   programa con WiFi o BLE, que en estos chips es casi todo, no puede ejecutarse.
   Es con diferencia lo más importante.
2. **La microarquitectura no está publicada**: los ciclos y el BTB están
   calibrados por el modelo (CoreMark); el tiempo de ejecución es aproximado.
3. **La CPU tras el reset va a XTAL/2 = 20 MHz**, pero la ROM cuenta 40 ticks
   por µs: los retardos de la ROM salen el doble. Falta comprobarlo en una placa.
4. **MSPI y caché**: el tiempo de una transacción y de un fallo de caché es de
   modelo; las operaciones de EXTMEM son instantáneas.
5. **La calibración del reloj lento es nominal**, sin deriva: un programa que
   duerma con RTC slow clock despierta aquí a la hora exacta.
6. **SYSCON_RND_DATA es un xorshift con semilla fija**: los números aleatorios
   son siempre los mismos (claves, *nonces*, retrasos aleatorios).
7. **Los valores analógicos de REGI2C, declarados**: no hay PLL que enganchar.
8. **RTC_CNTL**: el *super watchdog*, el *brownout*, CLK_CONF y ANA_CONF,
   declarados.
9. **UART con autobaud sin filtro de glitches.**
10. **Latencia de interrupción de modelo**; ciclos del SHA de modelo.
11. **La HP SRAM se conserva en deep sleep [modelo]**; la LP SRAM vista desde
    HP tarda 2 ciclos de APB [modelo]: un programa que confíe en la SRAM tras
    deep sleep funciona aquí.
12. **ETM instantáneo.**
13. **El SAR ADC**, presumiblemente ideal.

## 2. No se pueden usar las herramientas reales

14. **USB-Serial-JTAG**: el formato de las órdenes es de modelo.
15. **La disposición de la región DM** es de modelo.
16. **La secuencia de reset por RFC 2217** la reconoce el puente; no hay
    esptool contra un USB real.
17. **Sin USB/IP**: el USB-Serial-JTAG no llega al anfitrión como dispositivo.
18. **D10-D11 del C6 (GDB G1/G2') fallan con ASan** (V-12), sin explicar.

## 3. Declarado o a medias (se nota al usarlo)

19. **Flash**: auto-suspensión y PMS de las ventanas, declarados.
20. **eFuse**: programar no programa (la lectura es instantánea).
21. **Arranque seguro y eFuses de seguridad**, declarados (el C3 puede arrancar
    de fábrica una flash cifrada por XTS_AES).
22. **Deep sleep**: los dominios de potencia, solo lo que necesita LP.
23. **C6**: PARL_IO, ECC, SDIO esclavo y el codificador de traza, declarados; el
    monitor de memoria solo con registros; el chopper de MCPWM sin modelar; APM y
    TEE declarados en parte.
24. **C6, pruebas ACT**: 18 difieren por diseño.

## 4. Menor

25. Consumo.
26. La imagen de ROM está reconstruida: 8 bytes distintos de la de QEMU, que
    nada referencia.
27. El resto de E12 (radio), cuando se haga 1.

## Por dónde empezar

Por 1: sin radio, el simulador solo sirve para la parte del programa que no usa
WiFi ni BLE. Después, 3 (comprobarlo en una placa es barato y cambia todos los
retardos de la ROM) y 6, que es sencillo de arreglar con una semilla variable.
