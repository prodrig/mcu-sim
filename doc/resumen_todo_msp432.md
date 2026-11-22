# Lo que falta en los MSP432, de más a menos importante

El mismo baremo que en `resumen_todo_msp430.md`: cuánto puede ocultar la
simulación que un programa falla en el chip real. Primero lo que funciona aquí y
falla allí sin avisar, luego lo que impide usar las herramientas de verdad,
después lo declarado o a medias y al final lo menor.

Abarca el MSP432P401R (P4) y el MSP432E401Y (E4), con sus LaunchPad y el XDS110
(X1 y X2 hechos). Las letras son las de las listas de P-16 a P-18 de
`doc/todo.md`; «E4 x» es la letra x de la lista del E401Y.

Fuente: `doc/analisis_msp432.md`, `doc/analisis_xds110.md`,
`doc/analisis_puente_usb.md` y `doc/todo.md`, en el estado del commit e64916a.

## Cómo leer el coste

La misma escala y las mismas marcas que en `resumen_todo_msp430.md`: **Bajo**
(< ~200 líneas, horas a dos días), **Medio** (~200-1 000 líneas, días a dos
semanas), **Alto** (> ~1 000 líneas o rediseño, semanas), **Muy alto** (proyecto
aparte o información sin publicar); **[placa]** medir en una placa real,
**[herramienta]** probar a mano una herramienta de terceros, **[doc]**
documentación que falta, **[invariantes]** mueve los invariantes de las suites,
**[decisión]** hay que decidir antes qué se quiere.

## 1. El programa funciona en el simulador y falla en el chip, sin aviso

### Arranque, relojes y energía (P4)

1. **El código de arranque de TI no se ejecuta (l)**: sin buzón, sin invocar el
   BSL por pin, sin bloqueo de JTAG ni zonas IP. Un programa que dependa de lo
   que deja hecho funciona distinto.
   *Coste:* **Medio-alto**. **[doc]** el código de arranque está en la ROM de
   TI y no es abierto: o se ejecuta la imagen de un chip (volcada de una placa,
   **[placa]**, con la duda de si se puede distribuir) o se reescribe su
   comportamiento desde la guía (SLAU356 §«Device Security» y el buzón), que lo
   describe por encima. **[invariantes]** cambia el tiempo de arranque.
2. **Los cristales de CS arrancan en 5 ms / 0,5 ms (g)**, cuando el de 32 kHz
   tarda cientos de ms en el real; sin contador de fallos, sin IRQ de CS, sin
   tiempos de conmutación. Un programa que no espere al cristal funciona aquí.
   *Coste:* **Bajo-medio** (~200 líneas). **[placa]** medir el arranque del LFXT
   y del HFXT de la LaunchPad (sus cristales y condensadores concretos).
   **[invariantes]**.
3. **PCM (h)**: sus reglas y tiempos salen en parte de ejemplos; no hay IRQ de
   PCM. Una transición de modo inválida puede pasar aquí.
   *Coste:* **Medio**. **[doc]** la tabla de transiciones de la guía está
   incompleta; **[placa]** probar en una LaunchPad las transiciones dudosas (qué
   pone PCMCTL1 y si se dispara la IRQ).
4. **PxIES vale 0 tras el reset (z)**: un LED que en el chip se enciende al
   pulsar aquí puede hacerlo al soltar; y **escribir IES no levanta IFG (k)**.
   *Coste:* **Bajo** los dos. **[placa]** leer PxIES tras el reset en una
   LaunchPad (la guía dice «indefinido») para elegir el valor.
5. **WDT_A no se reinicia con el reset blando (i)**; **LOCKUP no está conectado
   (j)**.
   *Coste:* **Bajo**.
6. **SRAM_BANKEN sin efecto (m).**
   *Coste:* **Bajo** (los bancos apagados leen como en el chip; **[placa]** para
   saber qué leen: ¿cero, error de bus?).
7. **FLCTL**: tiempos de modelo, borrado de un sector protegido instantáneo (n);
   sin VER_PRE/AUTO_PRE, lecturas de margen, BMRK ni límite de pulsos (o).
   *Coste:* **Medio**. **[placa]** medir tiempos de programación y borrado con
   driverlib en una LaunchPad.
8. **La ROM R1 con otras instrucciones (y)**: diferencias de µs en las funciones
   de driverlib en ROM; ROM_VERSION 1; 12 huecos de BKPT.
   *Coste:* el remedio es la ROM real (**[placa]** volcarla; **[decisión]** si
   se puede distribuir, que es lo mismo que en el punto 1): **Bajo** en código,
   bloqueado por la licencia.
9. **El SysTick con CLKSOURCE = 0 da FCLK/8**, una integración de ST que no se
   ha contrastado en el P4.
   *Coste:* **Bajo**. **[placa]** comprobar en una LaunchPad qué reloj usa.
10. **Los pulsos de GPIO más cortos que el *quantum* pueden desaparecer**: el
    `sync_escritura_perif` del F303 (w) no está aquí.
    *Coste:* **Bajo-medio**, compartido con el F4. **[invariantes]**.
11. **Analógico ideal**: ADC14 sin ruido; PWRMD y REFBURST sin efecto; RDYIFG;
    CHxMAP.
    *Coste:* **Medio** (con el modelo de ruido común a los MSP430 y STM32).
    RDYIFG y CHxMAP, **Bajo**.
12. **RTC_C**: RTCRDY siempre a 1; la calibración no tiene efecto.
    *Coste:* **Bajo-medio**.
13. **La TLV es sintética (f)**: calibraciones elegidas, checksum 0. Un programa
    que use las calibraciones de fábrica obtiene otros números.
    *Coste:* **Bajo** con un volcado. **[placa]** leer la TLV de una o dos
    LaunchPad (y calcular el checksum como lo hace driverlib).

### MSP432E401Y

14. **SysCtlDelay tarda 4 ciclos por vuelta en lugar de 3 (E4 m)**: un 33 % más
    lento, porque el núcleo es el compartido.
    *Coste:* **Alto** si se arregla en el núcleo (es el P-02 de los STM32);
    **Bajo** si se corrige solo el caso del salto hacia atrás que usa
    SysCtlDelay. **[invariantes]**. **[placa]** medirlo en una LaunchPad E401Y.
15. **Los modos de sueño no paran los relojes (E4 i)**; el SysTick con
    CLKSOURCE = 0 da SYSCLK/8 (en el real, PIOSC/4); SRGPIO; INTTYPE de WDT como
    NMI.
    *Coste:* **Medio** los cuatro; el SysTick es **Bajo** y está documentado en
    la hoja de datos.
16. **Prefetch de modelo (E4 f)**; tiempos de flash y EEPROM de modelo, sin
    errores (E4 g); la ROM R1 con 85 huecos de BKPT (E4 b).
    *Coste:* **Medio**. **[placa]** calibrar el prefetch con bucles medidos a
    120 MHz.
17. **ADC ideal**: sin comparadores digitales, promediado, DMA ni GSYNC.
    *Coste:* **Medio** (~500 líneas los cuatro); bien documentado.
18. **C_CAN**: BUSY invisible, bus-off instantáneo.
    *Coste:* **Bajo**.
19. **El PHY negocia en 1 ms** (en el real, unos 2 s): un programa que no espere
    al enlace funciona aquí.
    *Coste:* **Bajo** (un parámetro). **[placa]** medirlo con un conmutador
    real; **[decisión]** si las suites lo aceleran.
20. **CCM instantáneo.**
    *Coste:* **Bajo**; **[doc]** la hoja de datos da pocos ciclos, hay que
    medirlos (**[placa]**).

## 2. No se pueden usar las herramientas reales

21. **El XDS110 no llega por USB**: CCS usa CMSIS-DAP por TCP u OpenOCD; CMSIS-DAP
    por USB/IP (D5/G4, U1/U4) sin hacer; **sin EnergyTrace (X3, X4)** ni
    actualización del firmware del XDS110.
    *Coste:* según `doc/analisis_xds110.md`: X3 ~500 líneas (Bajo-medio), X4
    ~700 (Medio), X5 CMSIS-DAP por USB/IP ~1 500 (Medio-alto), X6 el XDS110 de
    OpenOCD ~600 sobre X5. **[herramienta]** CCS y UniFlash en Windows con
    usbip-win2; EnergyTrace de CCS solo con X5. **[placa]** calibrar el consumo
    de X3 con un EnergyTrace real.
22. **El BSL de TI del P4 es demasiado lento** para el plazo de 1 s de
    BSL-Scripter (ff, B2); el BSL B1 sin SPI ni I2C, sin plazo en LPM4.5, sin
    FACTORY_RESET ni escritura de la flash de información (bb).
    *Coste:* la lentitud, **Medio** (perfilar qué hace lento al BSL de TI en el
    modelo). Lo que falta del B1, **Medio** en total. **[herramienta]**
    BSL-Scripter a mano.
23. **Las sesiones de OpenOCD no son reproducibles (cc)**; OpenOCD frente al E4,
    sin probar (E4 n).
    *Coste:* **Medio**, casi todo depuración. **[herramienta]** OpenOCD del
    sistema, a mano y luego en el CI.
24. **E4**: sflash, LM Flash Programmer y UniFlash, sin probar (E4 l); el
    cargador solo por UART0, sin comprobar el CRC, con un límite de ~460 800
    baudios por sondeo; el cargador por Ethernet con BOOTP sin DHCP y TFTP sin
    opciones.
    *Coste:* **Medio**. **[herramienta]** LM Flash Programmer y UniFlash son de
    Windows: a mano. **[doc]** SPMU373 (cargador de arranque de TivaWare).
25. **El puente USB del E4 solo hace CDC** (sin HID ni MSC); no lee
    SERIAL_STATE; las líneas de módem son fijas.
    *Coste:* HID y MSC, **Medio** cada uno (U2 de `analisis_puente_usb.md`);
    SERIAL_STATE y las líneas, **Bajo**.

## 3. Declarado o a medias (se nota al usarlo)

26. **CAPTIO y PSS**, declarados.
    *Coste:* CAPTIO **Medio** (necesita una pieza electrodo); PSS **Bajo-medio**.
27. **eUSCI (q)**: sin autobaud, IrDA, filtro de glitches, I2C de 10 bits,
    UCCLTO ni UCSWACK.
    *Coste:* **Medio** en total; se comparte casi entero con el eUSCI de los
    MSP430.
28. **Timer_A (r)**: CCInB internas e INCLK leen 0; el CCIFG de CCR0 no se borra
    solo.
    *Coste:* **Bajo**. El CCIFG es un fallo del modelo y conviene hacerlo ya.
29. **uDMA (t)**: sin scatter-gather; Timer_A, AES y los pines no le piden.
    *Coste:* **Medio**.
30. **AES (u)**: sin modos DMA, CBC, OFB ni CFB.
    *Coste:* **Bajo-medio**; los vectores de prueba de NIST bastan.
31. **COMP_E (w)**: sin los canales internos 8-15 ni SHORT; su salida no llega a
    los temporizadores.
    *Coste:* **Bajo**.
32. **E4 (i)**: UART sin IrDA, smartcard, 9 bits ni líneas de módem; SSI e I2C
    solo maestros; GPTM sin modo RTC, interrupciones de PWM, TnWOT, TnSNAPS ni
    TCACT; hibernación sin calendario, *tamper* ni retención de GPIO.
    *Coste:* **Alto** en total, **Bajo-medio** cada uno. Los esclavos de SSI e
    I2C y el modo RTC del GPTM son los que más se usan en prácticas.
33. **E4 (t)**: EPI, PWM, QEI, comparadores y SYSEXC, declarados.
    *Coste:* PWM y QEI **Medio** cada uno (son los útiles: motores y
    codificadores); EPI **Alto**; comparadores y SYSEXC **Bajo**.
34. **E4 (o)**: CCM sin DMA ni IRQ; sin los modos CCM, CBC-MAC, XTS, f8 ni f9.
    *Coste:* **Medio**.
35. **E4 (q)**: EMAC sin PAUSE, VLAN, half duplex ni checksum; MDIO solo en la
    dirección 0.
    *Coste:* **Medio**; se comparte con el Ethernet del F407 (F-42..F-48).
36. **E4 (s)**: USB0 solo dispositivo FS (sin host, OTG, HS, isócrono, LPM,
    suspensión, VBUS ni ID).
    *Coste:* **Alto** (el host, sobre todo); suspensión, VBUS e ID, **Bajo-medio**.
37. El hueco de ROM_pvAESTable del E4 (j).
    *Coste:* **Bajo**.

## 4. Menor

38. Las cifras del modelo de consumo (x).
    *Coste:* **Bajo** en código; **[placa]** medirlas con EnergyTrace.
39. Las placas del E4 (u): el RGB y el zumbador del EDUMKII, tras transistores,
    aproximados como cargas del pin; la TFT de 128x128 enseñada con 160 filas;
    sin el TPS2052B; J101 de adorno salvo el backchannel y los 5 V; sin el pad de
    VREF+.
    *Coste:* **Bajo-medio** en total, entre el XML, las piezas y `mcu-sim-gui`.
40. Datos de una sola fuente (d, e, p).
    *Coste:* **[doc]** o **[placa]** para contrastarlos; poco código.
41. `rom.h`/`rom_map.h` vienen de Energia con una nota de licencia antigua:
    cambiarlos por los del SDK 3.40.
    *Coste:* **Bajo**. **[doc]** el SDK 3.40 de TI (descarga con licencia).
42. El pull-up de 50 kΩ de RST/WAKE del E4 es de modelo (E4 h).
    *Coste:* **Bajo**; **[placa]** medirlo.
43. T8 sin hacer: P401M, P4011/P4111 con LCD_F, E411Y, TM4C129x, TM4C123.
    *Coste:* **Alto** en total (el P4111 con LCD_F y el TM4C123 son los más
    pedidos). **[doc]** sus hojas de datos y guías, en `doc/refs` o a descargar.

## Balance del coste

* **Barato y con efecto**: 4, 5, 9, 13 (con un volcado), 18, 19, 28, 31 y el
  SysTick de 15. Una semana en total, y casi todos dependen de una tarde de
  medidas con las dos LaunchPad.
* **Medio y que se nota en prácticas**: 2 (cristales), 3 (PCM), 7 (FLCTL), 10
  (pulsos cortos, compartido con el F4) y 22 (BSL).
* **Caro o bloqueado**: 1 y 8 dependen de poder usar la ROM de TI (una
  **[decisión]** de licencia antes que de código); 14 en serio es el P-02 de los
  STM32; 21 es la serie X3-X6 del XDS110.
* **Trabajo a mano**: una sesión con la LaunchPad P401R y la E401Y (osciloscopio,
  un volcado de la TLV y de la ROM, PxIES tras el reset, el arranque del LFXT,
  SysCtlDelay, el enlace del PHY) cierra o calibra una docena de puntos. Las
  herramientas de Windows (UniFlash, LM Flash Programmer, CCS) solo tras X5.

## Por dónde empezar

Por 1-4: el arranque de TI, los cristales y PxIES son lo que más programas de
prácticas toca (todo programa que use LFXT o un pulsador), y por 14, que hace
que cualquier retardo del E4 salga un tercio más largo. En herramientas, 21: el
XDS110 por USB/IP es lo que deja usar CCS sin cambiar su configuración.
