# Lo que falta en los STM32, de más a menos importante

El mismo baremo que en `resumen_todo_msp430.md`: cuánto puede ocultar la
simulación que un programa falla en el chip real. Primero lo que funciona aquí y
falla allí sin avisar, luego lo que impide usar las herramientas de verdad,
después lo declarado o a medias y al final lo menor.

Abarca el F407, el F446, el F415/F417 y el F303 (dado 0x422). El F303xD/E, el
F303x6/8 y el F334 están analizados (`doc/analisis_f303xD-E_f303x6_8_F334.md`)
pero sin empezar.

Fuente: `doc/todo.md` (P = plan, F = bits sin máquina, T = temporización,
D = datos sin fuente, X = discrepancias, V = verificación, I = deuda) y las
letras de su lista del F303, en el estado del commit e64916a.

## Cómo leer el coste

La misma escala y las mismas marcas que en `resumen_todo_msp430.md`: **Bajo**
(< ~200 líneas, horas a dos días), **Medio** (~200-1 000 líneas, días a dos
semanas), **Alto** (> ~1 000 líneas o rediseño, semanas), **Muy alto** (proyecto
aparte o información sin publicar); **[placa]** medir en una placa real,
**[herramienta]** probar a mano una herramienta de terceros, **[doc]**
documentación que falta, **[invariantes]** mueve los invariantes de las suites,
**[decisión]** hay que decidir antes qué se quiere. Donde `doc/todo.md` §10 ya
pone el punto en un bloque de su propuesta de cierre, se cita.

## 1. El programa funciona en el simulador y falla en el chip, sin aviso

### Tiempo

1. **El núcleo, unas 3 veces más lento en ciclos (P-02)**: ~4,7 CPI frente a
   ~1,5 del Cortex-M4 real (CoreMark 0,71/MHz frente a ~3,4). Todo lo que mide o
   depende del tiempo de ejecución sale distinto: bucles de espera, perfiles,
   plazos de tiempo real.
   *Coste:* **Alto** (solape de búsqueda y ejecución, cola de prebúsqueda de tres
   palabras, coste de los saltos). Según `todo.md` §10, tiene sentido después de
   P-01. **[placa]** calibrar con DWT_CYCCNT en una Discovery F407 y una Nucleo
   F446/F303 (CoreMark y una batería de microbucles). **[invariantes]** los
   mueve todos.
2. **El bus es LT, no AT (P-01)**: sin solape de transacciones, arbitraje a
   posteriori, sin HREADY. Las contiendas entre CPU y DMA no se notan.
   *Coste:* **Muy alto**: el plan declara +50-100 % de esfuerzo y una simulación
   ~10× más lenta (la suite pasaría de segundos a minutos). Se puede migrar por
   partes (matriz primero, solo Flash/ART, SRAM y FSMC en AT). **[decisión]**
   si se paga la lentitud, o si se deja como modo opcional. **[invariantes]**.
3. **La entrada a excepción no cuesta sus 12 ciclos (P-03).**
   *Coste:* **Bajo** en código, pero **[invariantes]**; según `todo.md` §10 va
   con P-01/P-02.
4. **El acuse del DMA no se consume (P-07)**: una petición de TIM de 1 ps se
   puede perder; y **la FIFO del DMA no puede desbordarse ni vaciarse en
   dinámico (P-04)**, así que un mal ajuste de ráfagas no da error aquí.
   *Coste:* P-07 **Bajo** (bloque 1 de `todo.md` §10; permite retirar I-01).
   P-04 **Medio**, y depende de P-01 para tener contienda real.
5. **Esperas del APB (P-10)** sin modelar.
   *Coste:* **Bajo-medio**; arrastrado por P-01. **[invariantes]**.
6. **VOS deja 168 MHz en escala 3 (T-20)**: más permisivo que el chip.
   *Coste:* **Bajo**. **[decisión]** qué hace el modelo: avisar o fallar como
   el chip (¿cuelgue, lecturas erróneas?). **[placa]** para saber cómo falla.
7. **Pulsos de GPIO más cortos que el *quantum* pueden desaparecer** en el F4
   (y en el MSP432): `sync_escritura_perif` solo está en el F303 (w).
   *Coste:* **Bajo-medio**: llevar al F4 lo que ya existe en el F303.
   **[invariantes]** probablemente.
8. Prefetch de una sola palabra (T-01), retraso de pad fijo (T-06),
   `t_rst_release` de 20 µs frente a 0,5-3 ms reales (D-04).
   *Coste:* T-01 **Medio** (**[doc]** RM0090 describe el ART por encima).
   T-06 y D-04 **Bajo**, con **[placa]**: medir el retraso de pad por velocidad
   de OSPEEDR y la salida del reset con un osciloscopio.

### Comportamiento

9. **LOCKUP para la simulación** en lugar de dejar PC = 0xFFFFFFFE (T-19); no
   llega al *break* de los TIM, como tampoco el fallo del HSE ni el PVD (r).
   *Coste:* **Bajo-medio** (un estado del núcleo que el depurador pueda ver, y
   tres líneas al *break*).
10. **Un evento EXTI despierta a medias de WFI (T-21).**
    *Coste:* **Bajo**.
11. **EXTICR sin flanco espurio al cambiarlo (X-09).**
    *Coste:* **Bajo** en código. **[placa]** el manual no lo aclara: hay que ver
    en una placa si el flanco aparece.
12. **El filtro de entrada de los TIM no se aplica (F-10)**: un pulsador con
    rebotes funciona aquí y falla allí.
    *Coste:* **Medio** (~200 líneas: muestreo a f_DTS con N muestras por ICF).
    Bien documentado en el RM; la pieza Pulsador ya rebota.
13. **Bus-off de CAN con recuperación instantánea (F-29).**
    *Coste:* **Bajo** (128 × 11 bits recesivos).
14. **IWDG**: se para en Standby (el silicio sigue) (r); el arranque por
    hardware desde los option bytes está atado a 0 (P-08); el IWDG del F4 con
    reset de sistema, sin comprobar (p).
    *Coste:* **Bajo** los tres (P-08 está en el bloque 1 de `todo.md` §10).
15. **El CSS da una sola NMI** en el F303 (o).
    *Coste:* **Bajo**.
16. **La FPU no tiene su IRQ 81 conectada (P-09).**
    *Coste:* **Bajo** (bloque 1 de `todo.md` §10).
17. **RDP declarado a medias**: el nivel 1 no bloquea el depurador y el nivel 2
    no lo desactiva.
    *Coste:* **Medio**. **[herramienta]** STM32CubeProgrammer a mano (subir y
    bajar el nivel, el borrado masivo al bajar). **[decisión]** si el nivel 2,
    irreversible en el chip, se deja deshacer en el simulador.
18. **Un reset durante un borrado de flash lo completa (f).**
    *Coste:* **Bajo**. **[decisión]** qué contenido queda (indeterminado en el
    chip): todo a 0xFF, nada, o aleatorio con semilla.
19. **El alias de 0x0 para maestros que no son el núcleo**, sin decidir (P-06).
    *Coste:* **Bajo** una vez decidido; **[decisión]** primero (bloque 2 de
    `todo.md` §10).
20. **Analógico ideal (S5)**: ADC sin ruido, VREF+ = VDDA, comparadores en un
    delta, OPAMP instantáneo, disparos sin latencia.
    *Coste:* **Medio** (~400 líneas); las cifras están en las hojas de datos.
    **[decisión]** opcional y con semilla.
21. **RTC**: la calibración, el *shift* y los subsegundos no tienen efecto
    (F-22..F-24).
    *Coste:* **Medio** (~300 líneas); bien documentado en el RM y en AN4759.

## 2. No se pueden usar las herramientas reales

22. **El ST-LINK de STM32CubeIDE no sirve (I-27)**: su protocolo es cerrado; hay
    que lanzar el depurador como GDB genérico.
    *Coste:* **Muy alto**: USB/IP (punto 30) más un ST-LINK/V2 emulado según lo
    que OpenOCD y stlink-org han deducido del protocolo. **[doc]** no hay
    especificación oficial; **[herramienta]** el servidor GDB de ST comprueba la
    versión de firmware de la sonda y puede rechazarla. Alternativa **Baja**:
    una plantilla de lanzamiento de CubeIDE ya preparada y documentada.
23. **Sin TAP JTAG completo (F-01)**: el IDE forzado a JTAG no conecta; SWD va
    bien.
    *Coste:* **Medio** (~500 líneas: TAP, IR/DR, BYPASS, IDCODE y el JTAG-DP).
    **[herramienta]** OpenOCD con `transport select jtag`.
24. **El stub GDB no anuncia el mapa de memoria (I-29)**: GDB usa paquetes X.
    *Coste:* **Bajo** (`qXfer:memory-map:read` desde el mapa que ya existe).
25. **El ITM no se reenvía a GDB (F-05).**
    *Coste:* **Bajo-medio**: sacar el SWO por un puerto TCP como hace OpenOCD.
    **[herramienta]** la vista SWV de CubeIDE, a mano.
26. **vFlashWrite solo en 32 bits (F-06)**, es decir, con VDD ≥ 2,7 V.
    *Coste:* **Bajo**.
27. **ETM y TPIU Manchester (F-02, F-03)**; un solo AP y un solo hilo (F-04).
    *Coste:* **Alto**, y casi ninguna herramienta de prácticas lo usa. No
    compensa.
28. **El stub GDB consume tiempo simulado (T-16).**
    *Coste:* **Medio** (desacoplar el stub del tiempo simulado sin romper el
    determinismo). **[invariantes]** en las suites con GDB.
29. **El bootloader de sistema del F303 es propio (g)**: sin I2C, SPI, CAN ni
    DFU; las órdenes de protección contestan NACK; 2 % de error de baudios a
    115 200.
    *Coste:* **Medio** por protocolo (~300 líneas cada uno); DFU depende de
    USB/IP. **[doc]** AN2606, AN3155, AN4221, AN4286 y AN3154 en `doc/refs`.
    **[herramienta]** STM32CubeProgrammer a mano por cada interfaz.
30. **Sin USB/IP**: ningún periférico USB llega al anfitrión como dispositivo
    real.
    *Coste:* **Alto** (U4 de `doc/analisis_puente_usb.md`, compartido con los
    MSP430 y el MSP432). **[herramienta]** usbip-win2 en Windows 10 y 11 a mano.

## 3. Declarado o a medias (se nota al usarlo)

Casi todo este bloque está en el «bloque 4» de `todo.md` §10: hacerlo solo
cuando haya un firmware de prácticas que lo use. Ninguno necesita placa salvo
donde se dice; todos están descritos en el RM.

31. **UART**: IrDA, smartcard y mute (F-07..F-09); en el F303 además autobaud y
    UESM (r). *Coste:* **Medio** en total; smartcard necesita una pieza tarjeta.
32. **I2C**: WUPEN y PEC del F303 (r); ARP de SMBus (F-18). *Coste:* **Bajo**.
33. **TIM**: precarga CCPC/COM para BLDC con Hall (F-11), AOE (F-12), ETPS/MSM
    (F-13). *Coste:* **Bajo-medio**; F-11 necesita una pieza motor BLDC para
    probarse de verdad.
34. **SPI/I2S**: trama TI (F-15), I2S PCM e I2S_CKIN (F-16, F-17), I2S
    full-duplex del F303 (r). *Coste:* **Medio**.
35. **ADC/DAC**: disparos por CC de TIM (F-19) y por EXTI (F-20), modos
    alternado e inyectado simultáneo (F-21). *Coste:* F-19 y F-20 **Bajo**
    (bloque 1 de `todo.md` §10: desbloquean muchos huecos de una vez); F-21
    **Medio**.
36. **SDIO**: MMC, SDIO y CE-ATA (F-25..F-27), sin piezas que los usen (I-06).
    *Coste:* **Medio-alto**, empezando por las piezas (I-06, bloque 2).
37. **CAN**: TTCM (F-28) y AWUM (F-30). *Coste:* **Bajo**.
38. **DCMI y FSMC (F-31..F-36).** *Coste:* **Alto**; DCMI necesita una pieza
    cámara.
39. **USB OTG**: ULPI, chirp de HS, SRP/HNP, planificador periódico, hubs
    (F-37..F-41); el FSDEV del F303 sin doble búfer, isócrono, ERR ni RESUME
    (dd). *Coste:* **Alto**; HS y ULPI exigen otro PHY. El doble búfer y el
    isócrono del FSDEV, **Medio**, son los únicos que usan ejemplos corrientes
    (audio USB).
40. **Ethernet**: PAUSE, descarga de checksum, half duplex, descriptores
    mejorados, PPS, VLAN, contadores MMC (F-42..F-48). *Coste:* **Medio** cada
    uno; la descarga de checksum y los descriptores mejorados son los que usa
    LwIP de CubeF4, los demás casi nadie.
41. **TSC declarado**; el F358 a 1,65-1,95 V sin modelar; al F302 le falta el
    analógico. *Coste:* TSC **Alto** (**[placa]** para calibrar); el F358
    **Bajo**; el analógico del F302 **Medio**.
42. **Los MEMS de las placas**: sin FIFO, *click*, 6D ni ruido; cada eje con una
    sola fuente. *Coste:* **Medio**; las hojas de datos de ST bastan.

## 4. Menor

43. Consumo lineal y tiempos de despertar como parámetros (T-13, T-14, D-09).
    *Coste:* **Medio**; **[placa]** con un medidor de corriente (el IDD de la
    Nucleo) para calibrar.
44. USB a nivel de paquete (T-17); *spread spectrum* (T-07); CMPCR (F-14).
    *Coste:* **Bajo-medio** cada uno; poco efecto en los programas.
45. Interior de la criptografía (F-49..F-51). *Coste:* **Medio**; el resultado
    ya es correcto, falta el tiempo.
46. Datos sin fuente (D-01..D-14); FP_REMAP abierto (X-04).
    *Coste:* **Bajo** cada uno, pero **[placa]** o **[doc]** para cerrarlos:
    son cifras que hay que medir o encontrar; X-04, contra RM0090 (bloque 2).
47. Verificación: sin comparación instrucción a instrucción con una referencia
    (V-01); VBAT (V-03); T53 de I2S inestable (V-11); un falso positivo de la
    validación (I-14); sin comprobación automática de varios MCU (I-21).
    *Coste:* V-01 **Alto** (una referencia como QEMU o un núcleo RTL, con traza
    comparada); los demás **Bajo-medio**.
48. La comprobación de 240 mA no se llama (P-11); el bit de reloj de FSMC
    (I-49). *Coste:* **Bajo** (P-11 en el bloque 1 de `todo.md` §10).
49. Los otros dados del F303, sin empezar.
    *Coste:* **Alto** (está el análisis); **[doc]** sus RM y hojas de datos
    están en `doc/refs`.

## Balance del coste

* **Barato y con efecto** (el bloque 1 de `todo.md` §10 y lo que se le parece):
  P-07, P-08, P-09, P-11, F-19, F-20, 7, 10, 13, 14, 15, 16, 24 y 26. Unas dos
  semanas en total, y quitan la mitad de los puntos de «sin aviso».
* **Medio y que se nota en prácticas**: 12 (filtro de los TIM), 21 (RTC),
  23 (JTAG) y 25 (ITM).
* **Caro**: 1 y 2 (P-01/P-02), que son lo que más pesa en el baremo pero cuestan
  semanas, hacen la simulación más lenta y mueven todos los invariantes; 22
  (ST-LINK) no compensa frente a documentar el lanzamiento genérico.
* **Trabajo a mano**: una sesión con la Discovery F407 y una Nucleo F303 con
  osciloscopio cierra 6, 8, 11 y buena parte de D-xx, y da la batería de
  microbucles que necesita el punto 1. STM32CubeProgrammer y CubeIDE, a mano,
  para 17, 23, 25 y 29.

## Por dónde empezar

Por 1 y 2: el CPI del núcleo y el bus LT hacen que todo lo que dependa del
tiempo salga distinto, y son lo que más programas reales afecta. Después, 7
(pulsos cortos de GPIO en el F4), que es un fallo latente del modelo, y 22-23,
que son lo que impide al alumno depurar desde CubeIDE como en el laboratorio.
Por coste, sin embargo, conviene hacer antes lo barato del balance: deja 1 y 2
como los únicos huecos grandes de «sin aviso».
