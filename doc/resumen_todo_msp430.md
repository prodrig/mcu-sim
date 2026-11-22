# Lo que falta en los MSP430, de más a menos importante

El baremo es cuánto puede ocultar la simulación que un programa falla en el chip
real: primero lo que funciona aquí y falla allí sin avisar, luego lo que impide
usar las herramientas de verdad, después lo declarado o a medias (que se nota al
usarlo) y al final lo menor.

Fuente: el «Hecho»/«Queda» de cada fase (M0-M12) de `doc/analisis_msp430.md` y
P-20 de `doc/todo.md`, en el estado del commit e64916a (M12).

## 1. El programa funciona en el simulador y falla en el chip, sin aviso

1. **El tiempo de la FRAM** (FR2xx, FR4xx, FR5xx, FR6xx). NWAITS solo avisa y
   no hay caché de FR5/FR6: olvidarse de poner NWAITS por encima de 8 MHz
   funciona aquí y falla en el chip, y el código en FRAM corre más deprisa que en
   el real (`__delay_cycles`, medidas de tiempo, software que cuenta ciclos).
2. **El JTAG frente a la HAL de TI no es del todo fiel.** Con mspdebug `tilib` y
   GDB por la eZ-FET emulada, `continue` hasta una ruptura y el paso a paso no
   vuelven; el PC tras la sincronización queda en 0xC004 en el G2553. Con la HAL
   como referencia ya salieron cinco errores (EEM, parada, PC al reiniciar), y es
   probable que queden más.
3. **El USB del F5529 tras un reset de bus.** El BSL de RAM no vuelve a
   enumerarse tras el Load PC a 0x2504 (no se queda en la dirección 0). Afecta a
   cualquier firmware USB que se reenumere. Es un fallo del modelo.
4. **Las carreras de las erratas documentadas, sin modelar**: ADC38/39/70,
   DMA7/11, PMM24-33, CS3/7/12, RTC10, USCI40-50, TA12/16/21, CPU46/47… Un
   programa que tropieza con una de ellas funciona aquí.
5. **Relojes ideales**: el DCO es exacto, sin deriva, y cambia al instante
   (BCL12); HFXTOFFG no se pone tras el POR; no hay *jitter*.
6. **Analógico ideal**: ADC, SAC, TIA, eCOMP y Comp_E sin ruido, *offset*, ancho
   de banda ni retraso de filtro.
7. **CPUXv2 sin cola de búsqueda** (PC = MAB): diferencias de ciclos en código
   que dependa de ello.

## 2. No se pueden usar las herramientas reales

8. **GDB por la eZ-FET**: el agente GDB de TI no está en la MSP Debug Stack; se
   depura con `--gdb-dap` o `--gdb` del simulador.
9. **Sin USB/IP (U4)**: CCS y UniFlash no ven la eZ-FET; no hay HID ni el F5529
   como dispositivo USB real; nada en Windows.
10. **El backchannel** va por el RFC 2217 de la placa, no por el segundo CDC de
    la eZ-FET.
11. **BSL**: `msp430.bsl5.hid` necesita `/dev/hidraw` o USB/IP; BSL-Scripter no
    está en `doc/refs`; faltan el BSL I2C de las FR5 y el UART del F5529.
12. **La actualización del firmware de la eZ-FET**, sin probar.

## 3. Declarado o a medias (se nota al usarlo)

13. **CapTIvate IO** (FR2433, FR4133, FR2311).
14. **LEA** sin el tiempo de sus órdenes.
15. **ESI** sin la sincronización de 37-5 ni el sensor LC.
16. **Conexiones internas declaradas**: RTC a las capturas de TA0/TB0/TA2, VLO a
    CCI0B; en el FR2355, TB3 CCR0B→TB1, la entrada MFM del eCOMP1 y la salida IR
    invertida del eUSCI_A1.
17. **ICC** con la pila llena y el RETI de una NMI.
18. **Reanudación remota USB** contestada al momento.
19. **USCI** sin autobaud ni I2C multimaestro.

## 4. Menor

20. Consumo y EnergyTrace.
21. FRAM sin errores de bit ni ECC (GCCTL1 = 0).
22. El fusible JTAG es un solo bit; sin almacenamiento de estado del EEM L; el
    JTAG de 4 hilos de las FR2 sin prueba propia.
23. LCD: 8-mux con bias 1/3; los símbolos se pintan como marcas.
24. El puente J101 quitado sigue dibujado como puesto.
25. A revisar: `msp_exp430fr5969.xml` sin firmware da «sobrecorriente» con la
    fuente USB; los ejemplos del G2553 están escritos aquí (faltan los SLAC485).
26. La CI se salta las pruebas de la eZ-FET si no tiene las imágenes; el pty no
    existe en Windows.

## Por dónde empezar

Por 1-3: el tiempo de la FRAM es lo que más programas reales rompe, y 2 y 3 son
fallos del modelo, no simplificaciones.
