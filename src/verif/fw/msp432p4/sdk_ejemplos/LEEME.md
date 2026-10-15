# sdk_ejemplos — Ejemplos de DriverLib del SDK de TI, sin modificar (T4)

Cuatro ejemplos del SimpleLink MSP432P4 SDK para la MSP-EXP432P401R
(`examples/nortos/MSP_EXP432P401R/driverlib/`), **copiados tal cual**:

| Carpeta | Qué hace | Lo comprueba |
| :--- | :--- | :--- |
| `systick_interrupt_gpio_blink` | SysTick cada 1 500 000 ciclos de MCLK cambia P1.0; duerme en LPM0 | P63 |
| `cs_dco_frequency_tune` | `CS_setDCOFrequency(8330000)`, que calcula DCOTUNE con el TLV | P64 |
| `timer32_periodic_mode_led_toggle` | MCLK = REFO a 128 kHz en AM_LF_VCORE0; S1 enciende P1.0 un segundo con Timer32 | P65 |
| `uart_pc_echo_12mhz_brclk` | Eco por la UCA0 a 9600 con SMCLK = 12 MHz | P65 |

`comun/` lleva lo que los cuatro comparten, que en el SDK es el mismo fichero
en cada ejemplo: `system_msp432p401r.c` y, en `gcc/`, el arranque
(`startup_msp432p401r_gcc.c`) y el guion de enlazado (`msp432p401r.lds`).

**Procedencia.** El SDK de TI no está en un repositorio público; los ficheros
salen de la copia del SDK que publica Adriano Ruseler en
`github.com/AdrianoRuseler/MSP-EXP432P401R`, revisión `0e4cf7f` (2018-06-01),
que los incluye sin tocar. Cada uno lleva su cabecera de licencia de TI,
**BSD de tres cláusulas**, y DriverLib, de `../sdk` (véase `../sdk/LICENSE-TI.md`).

**Cómo se compilan.** Con las opciones del makefile de gcc de los propios
ejemplos, más `-ffile-prefix-map` para que la huella no dependa de la carpeta
(T-22). Lo que es `MAP_` salta a la ROM por `ROM_APITABLE`; lo demás
(`CS_setDCOCenteredFrequency`, por ejemplo) sale de `obj/libdriverlib.a`,
DriverLib compilada aquí de sus fuentes, como la librería del SDK.

    make            # los cuatro .out y .bin; las huellas, en verif/fw/huellas.txt
