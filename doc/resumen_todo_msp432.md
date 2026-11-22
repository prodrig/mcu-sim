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

## 1. El programa funciona en el simulador y falla en el chip, sin aviso

### Arranque, relojes y energía (P4)

1. **El código de arranque de TI no se ejecuta (l)**: sin buzón, sin invocar el
   BSL por pin, sin bloqueo de JTAG ni zonas IP. Un programa que dependa de lo
   que deja hecho funciona distinto.
2. **Los cristales de CS arrancan en 5 ms / 0,5 ms (g)**, cuando el de 32 kHz
   tarda cientos de ms en el real; sin contador de fallos, sin IRQ de CS, sin
   tiempos de conmutación. Un programa que no espere al cristal funciona aquí.
3. **PCM (h)**: sus reglas y tiempos salen en parte de ejemplos; no hay IRQ de
   PCM. Una transición de modo inválida puede pasar aquí.
4. **PxIES vale 0 tras el reset (z)**: un LED que en el chip se enciende al
   pulsar aquí puede hacerlo al soltar; y **escribir IES no levanta IFG (k)**.
5. **WDT_A no se reinicia con el reset blando (i)**; **LOCKUP no está conectado
   (j)**.
6. **SRAM_BANKEN sin efecto (m).**
7. **FLCTL**: tiempos de modelo, borrado de un sector protegido instantáneo (n);
   sin VER_PRE/AUTO_PRE, lecturas de margen, BMRK ni límite de pulsos (o).
8. **La ROM R1 con otras instrucciones (y)**: diferencias de µs en las funciones
   de driverlib en ROM; ROM_VERSION 1; 12 huecos de BKPT.
9. **El SysTick con CLKSOURCE = 0 da FCLK/8**, una integración de ST que no se
   ha contrastado en el P4.
10. **Los pulsos de GPIO más cortos que el *quantum* pueden desaparecer**: el
    `sync_escritura_perif` del F303 (w) no está aquí.
11. **Analógico ideal**: ADC14 sin ruido; PWRMD y REFBURST sin efecto; RDYIFG;
    CHxMAP.
12. **RTC_C**: RTCRDY siempre a 1; la calibración no tiene efecto.
13. **La TLV es sintética (f)**: calibraciones elegidas, checksum 0. Un programa
    que use las calibraciones de fábrica obtiene otros números.

### MSP432E401Y

14. **SysCtlDelay tarda 4 ciclos por vuelta en lugar de 3 (E4 m)**: un 33 % más
    lento, porque el núcleo es el compartido.
15. **Los modos de sueño no paran los relojes (E4 i)**; el SysTick con
    CLKSOURCE = 0 da SYSCLK/8 (en el real, PIOSC/4); SRGPIO; INTTYPE de WDT como
    NMI.
16. **Prefetch de modelo (E4 f)**; tiempos de flash y EEPROM de modelo, sin
    errores (E4 g); la ROM R1 con 85 huecos de BKPT (E4 b).
17. **ADC ideal**: sin comparadores digitales, promediado, DMA ni GSYNC.
18. **C_CAN**: BUSY invisible, bus-off instantáneo.
19. **El PHY negocia en 1 ms** (en el real, unos 2 s): un programa que no espere
    al enlace funciona aquí.
20. **CCM instantáneo.**

## 2. No se pueden usar las herramientas reales

21. **El XDS110 no llega por USB**: CCS usa CMSIS-DAP por TCP u OpenOCD; CMSIS-DAP
    por USB/IP (D5/G4, U1/U4) sin hacer; **sin EnergyTrace (X3, X4)** ni
    actualización del firmware del XDS110.
22. **El BSL de TI del P4 es demasiado lento** para el plazo de 1 s de
    BSL-Scripter (ff, B2); el BSL B1 sin SPI ni I2C, sin plazo en LPM4.5, sin
    FACTORY_RESET ni escritura de la flash de información (bb).
23. **Las sesiones de OpenOCD no son reproducibles (cc)**; OpenOCD frente al E4,
    sin probar (E4 n).
24. **E4**: sflash, LM Flash Programmer y UniFlash, sin probar (E4 l); el
    cargador solo por UART0, sin comprobar el CRC, con un límite de ~460 800
    baudios por sondeo; el cargador por Ethernet con BOOTP sin DHCP y TFTP sin
    opciones.
25. **El puente USB del E4 solo hace CDC** (sin HID ni MSC); no lee
    SERIAL_STATE; las líneas de módem son fijas.

## 3. Declarado o a medias (se nota al usarlo)

26. **CAPTIO y PSS**, declarados.
27. **eUSCI (q)**: sin autobaud, IrDA, filtro de glitches, I2C de 10 bits,
    UCCLTO ni UCSWACK.
28. **Timer_A (r)**: CCInB internas e INCLK leen 0; el CCIFG de CCR0 no se borra
    solo.
29. **uDMA (t)**: sin scatter-gather; Timer_A, AES y los pines no le piden.
30. **AES (u)**: sin modos DMA, CBC, OFB ni CFB.
31. **COMP_E (w)**: sin los canales internos 8-15 ni SHORT; su salida no llega a
    los temporizadores.
32. **E4 (i)**: UART sin IrDA, smartcard, 9 bits ni líneas de módem; SSI e I2C
    solo maestros; GPTM sin modo RTC, interrupciones de PWM, TnWOT, TnSNAPS ni
    TCACT; hibernación sin calendario, *tamper* ni retención de GPIO.
33. **E4 (t)**: EPI, PWM, QEI, comparadores y SYSEXC, declarados.
34. **E4 (o)**: CCM sin DMA ni IRQ; sin los modos CCM, CBC-MAC, XTS, f8 ni f9.
35. **E4 (q)**: EMAC sin PAUSE, VLAN, half duplex ni checksum; MDIO solo en la
    dirección 0.
36. **E4 (s)**: USB0 solo dispositivo FS (sin host, OTG, HS, isócrono, LPM,
    suspensión, VBUS ni ID).
37. El hueco de ROM_pvAESTable del E4 (j).

## 4. Menor

38. Las cifras del modelo de consumo (x).
39. Las placas del E4 (u): el RGB y el zumbador del EDUMKII, tras transistores,
    aproximados como cargas del pin; la TFT de 128x128 enseñada con 160 filas;
    sin el TPS2052B; J101 de adorno salvo el backchannel y los 5 V; sin el pad de
    VREF+.
40. Datos de una sola fuente (d, e, p).
41. `rom.h`/`rom_map.h` vienen de Energia con una nota de licencia antigua:
    cambiarlos por los del SDK 3.40.
42. El pull-up de 50 kΩ de RST/WAKE del E4 es de modelo (E4 h).
43. T8 sin hacer: P401M, P4011/P4111 con LCD_F, E411Y, TM4C129x, TM4C123.

## Por dónde empezar

Por 1-4: el arranque de TI, los cristales y PxIES son lo que más programas de
prácticas toca (todo programa que use LFXT o un pulsador), y por 14, que hace
que cualquier retardo del E4 salga un tercio más largo. En herramientas, 21: el
XDS110 por USB/IP es lo que deja usar CCS sin cambiar su configuración.
