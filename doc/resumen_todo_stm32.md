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

## 1. El programa funciona en el simulador y falla en el chip, sin aviso

### Tiempo

1. **El núcleo, unas 3 veces más lento en ciclos (P-02)**: ~4,7 CPI frente a
   ~1,5 del Cortex-M4 real (CoreMark 0,71/MHz frente a ~3,4). Todo lo que mide o
   depende del tiempo de ejecución sale distinto: bucles de espera, perfiles,
   plazos de tiempo real.
2. **El bus es LT, no AT (P-01)**: sin solape de transacciones, arbitraje a
   posteriori, sin HREADY. Las contiendas entre CPU y DMA no se notan.
3. **La entrada a excepción no cuesta sus 12 ciclos (P-03).**
4. **El acuse del DMA no se consume (P-07)**: una petición de TIM de 1 ps se
   puede perder; y **la FIFO del DMA no puede desbordarse ni vaciarse en
   dinámico (P-04)**, así que un mal ajuste de ráfagas no da error aquí.
5. **Esperas del APB (P-10)** sin modelar.
6. **VOS deja 168 MHz en escala 3 (T-20)**: más permisivo que el chip.
7. **Pulsos de GPIO más cortos que el *quantum* pueden desaparecer** en el F4
   (y en el MSP432): `sync_escritura_perif` solo está en el F303 (w).
8. Prefetch de una sola palabra (T-01), retraso de pad fijo (T-06),
   `t_rst_release` de 20 µs frente a 0,5-3 ms reales (D-04).

### Comportamiento

9. **LOCKUP para la simulación** en lugar de dejar PC = 0xFFFFFFFE (T-19); no
   llega al *break* de los TIM, como tampoco el fallo del HSE ni el PVD (r).
10. **Un evento EXTI despierta a medias de WFI (T-21).**
11. **EXTICR sin flanco espurio al cambiarlo (X-09).**
12. **El filtro de entrada de los TIM no se aplica (F-10)**: un pulsador con
    rebotes funciona aquí y falla allí.
13. **Bus-off de CAN con recuperación instantánea (F-29).**
14. **IWDG**: se para en Standby (el silicio sigue) (r); el arranque por
    hardware desde los option bytes está atado a 0 (P-08); el IWDG del F4 con
    reset de sistema, sin comprobar (p).
15. **El CSS da una sola NMI** en el F303 (o).
16. **La FPU no tiene su IRQ 81 conectada (P-09).**
17. **RDP declarado a medias**: el nivel 1 no bloquea el depurador y el nivel 2
    no lo desactiva.
18. **Un reset durante un borrado de flash lo completa (f).**
19. **El alias de 0x0 para maestros que no son el núcleo**, sin decidir (P-06).
20. **Analógico ideal (S5)**: ADC sin ruido, VREF+ = VDDA, comparadores en un
    delta, OPAMP instantáneo, disparos sin latencia.
21. **RTC**: la calibración, el *shift* y los subsegundos no tienen efecto
    (F-22..F-24).

## 2. No se pueden usar las herramientas reales

22. **El ST-LINK de STM32CubeIDE no sirve (I-27)**: su protocolo es cerrado; hay
    que lanzar el depurador como GDB genérico.
23. **Sin TAP JTAG completo (F-01)**: el IDE forzado a JTAG no conecta; SWD va
    bien.
24. **El stub GDB no anuncia el mapa de memoria (I-29)**: GDB usa paquetes X.
25. **El ITM no se reenvía a GDB (F-05).**
26. **vFlashWrite solo en 32 bits (F-06)**, es decir, con VDD ≥ 2,7 V.
27. **ETM y TPIU Manchester (F-02, F-03)**; un solo AP y un solo hilo (F-04).
28. **El stub GDB consume tiempo simulado (T-16).**
29. **El bootloader de sistema del F303 es propio (g)**: sin I2C, SPI, CAN ni
    DFU; las órdenes de protección contestan NACK; 2 % de error de baudios a
    115 200.
30. **Sin USB/IP**: ningún periférico USB llega al anfitrión como dispositivo
    real.

## 3. Declarado o a medias (se nota al usarlo)

31. **UART**: IrDA, smartcard y mute (F-07..F-09); en el F303 además autobaud y
    UESM (r).
32. **I2C**: WUPEN y PEC del F303 (r); ARP de SMBus (F-18).
33. **TIM**: precarga CCPC/COM para BLDC con Hall (F-11), AOE (F-12), ETPS/MSM
    (F-13).
34. **SPI/I2S**: trama TI (F-15), I2S PCM e I2S_CKIN (F-16, F-17), I2S
    full-duplex del F303 (r).
35. **ADC/DAC**: disparos por CC de TIM (F-19) y por EXTI (F-20), modos
    alternado e inyectado simultáneo (F-21).
36. **SDIO**: MMC, SDIO y CE-ATA (F-25..F-27), sin piezas que los usen (I-06).
37. **CAN**: TTCM (F-28) y AWUM (F-30).
38. **DCMI y FSMC (F-31..F-36).**
39. **USB OTG**: ULPI, chirp de HS, SRP/HNP, planificador periódico, hubs
    (F-37..F-41); el FSDEV del F303 sin doble búfer, isócrono, ERR ni RESUME
    (dd).
40. **Ethernet**: PAUSE, descarga de checksum, half duplex, descriptores
    mejorados, PPS, VLAN, contadores MMC (F-42..F-48).
41. **TSC declarado**; el F358 a 1,65-1,95 V sin modelar; al F302 le falta el
    analógico.
42. **Los MEMS de las placas**: sin FIFO, *click*, 6D ni ruido; cada eje con una
    sola fuente.

## 4. Menor

43. Consumo lineal y tiempos de despertar como parámetros (T-13, T-14, D-09).
44. USB a nivel de paquete (T-17); *spread spectrum* (T-07); CMPCR (F-14).
45. Interior de la criptografía (F-49..F-51).
46. Datos sin fuente (D-01..D-14); FP_REMAP abierto (X-04).
47. Verificación: sin comparación instrucción a instrucción con una referencia
    (V-01); VBAT (V-03); T53 de I2S inestable (V-11); un falso positivo de la
    validación (I-14); sin comprobación automática de varios MCU (I-21).
48. La comprobación de 240 mA no se llama (P-11); el bit de reloj de FSMC
    (I-49).
49. Los otros dados del F303, sin empezar.

## Por dónde empezar

Por 1 y 2: el CPI del núcleo y el bus LT hacen que todo lo que dependa del
tiempo salga distinto, y son lo que más programas reales afecta. Después, 7
(pulsos cortos de GPIO en el F4), que es un fallo latente del modelo, y 22-23,
que son lo que impide al alumno depurar desde CubeIDE como en el laboratorio.
