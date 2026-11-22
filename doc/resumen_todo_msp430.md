# Lo que falta en los MSP430, de más a menos importante

El baremo es cuánto puede ocultar la simulación que un programa falla en el chip
real: primero lo que funciona aquí y falla allí sin avisar, luego lo que impide
usar las herramientas de verdad, después lo declarado o a medias (que se nota al
usarlo) y al final lo menor.

Fuente: el «Hecho»/«Queda» de cada fase (M0-M12) de `doc/analisis_msp430.md` y
P-20 de `doc/todo.md`, en el estado del commit e64916a (M12).

## Cómo leer el coste

Cada punto lleva una línea **Coste** con una estimación (no una medida) y lo que
hace falta además de escribir código:

| Coste | Qué quiere decir |
|---|---|
| **Bajo** | Menos de ~200 líneas; de unas horas a dos días |
| **Medio** | ~200-1 000 líneas; de unos días a dos semanas |
| **Alto** | Más de ~1 000 líneas o un rediseño; semanas |
| **Muy alto** | Un proyecto aparte, o depende de información que no se publica |

| Marca | Qué hace falta |
|---|---|
| **[placa]** | Medir en una placa real (osciloscopio, analizador lógico, `usbmon`) para calibrar o para decidir cómo se porta el silicio |
| **[herramienta]** | Probar a mano una herramienta de terceros (CCS, UniFlash, BSL-Scripter, Windows), que no cabe en el CI |
| **[doc]** | Documentación que no está en `doc/refs` o que TI no publica |
| **[invariantes]** | Mueve los invariantes de las suites: hay que reasignarlos de forma explícita en `src/verif/invariantes.txt` |
| **[decisión]** | Antes de programar hay que decidir qué comportamiento se quiere |

## 1. El programa funciona en el simulador y falla en el chip, sin aviso

1. **El tiempo de la FRAM** (FR2xx, FR4xx, FR5xx, FR6xx). NWAITS solo avisa y
   no hay caché de FR5/FR6: olvidarse de poner NWAITS por encima de 8 MHz
   funciona aquí y falla en el chip, y el código en FRAM corre más deprisa que en
   el real (`__delay_cycles`, medidas de tiempo, software que cuenta ciclos).
   *Coste:* **Medio** (~400 líneas: estados de espera por acceso y la caché de
   FR5/FR6 por delante del controlador). **[doc]** la guía describe la caché por
   encima (líneas, asociatividad), no su política exacta. **[placa]** calibrarla
   con un bucle que conmute un pin en una FR5969 y una FR2433 a 8, 16 y 24 MHz,
   con NWAITS correcto y sin él (qué hace el chip: ¿se cuelga, lee basura?).
   **[invariantes]** cambia el tiempo de todas las suites de FRAM.
2. **El JTAG frente a la HAL de TI no es del todo fiel.** Con mspdebug `tilib` y
   GDB por la eZ-FET emulada, `continue` hasta una ruptura y el paso a paso no
   vuelven; el PC tras la sincronización queda en 0xC004 en el G2553. Con la HAL
   como referencia ya salieron cinco errores (EEM, parada, PC al reiniciar), y es
   probable que queden más.
   *Coste:* **Medio**, casi todo depuración y poco código. Hace falta
   `make ezfet-tilib` (compilar libmsp430) y las imágenes de la MSP Debug Stack.
   **[placa]** útil, no imprescindible: grabar con un analizador lógico la misma
   secuencia SBW entre una eZ-FET real y un G2553 real para comparar trama a
   trama.
3. **El USB del F5529 tras un reset de bus.** El BSL de RAM no vuelve a
   enumerarse tras el Load PC a 0x2504 (no se queda en la dirección 0). Afecta a
   cualquier firmware USB que se reenumere. Es un fallo del modelo.
   *Coste:* **Bajo-medio** (depuración del reset de bus en el USB del F5529).
   **[placa]** una captura con `usbmon` o Wireshark de la reenumeración de una
   F5529 LaunchPad real deja la prueba de referencia.
4. **Las carreras de las erratas documentadas, sin modelar**: ADC38/39/70,
   DMA7/11, PMM24-33, CS3/7/12, RTC10, USCI40-50, TA12/16/21, CPU46/47… Un
   programa que tropieza con una de ellas funciona aquí.
   *Coste:* **Bajo cada una, Alto en total** (son unas treinta). **[doc]** las
   hojas de erratas dicen el síntoma y el remedio, casi nunca la ventana de tiempo
   exacta: hay que elegirla. **[decisión]** cuáles merece la pena: las que tocan
   programas de prácticas (USCI, TA, CS) antes que las de PMM. Cada una con su
   prueba que falle sin el remedio de TI.
5. **Relojes ideales**: el DCO es exacto, sin deriva, y cambia al instante
   (BCL12); HFXTOFFG no se pone tras el POR; no hay *jitter*.
   *Coste:* **Medio** (~300 líneas). Las tolerancias están en las hojas de datos;
   **[decisión]** la deriva debe ser opcional y con semilla, para no perder la
   reproducibilidad de las suites. HFXTOFFG tras el POR es **Bajo**.
   **[invariantes]** si la deriva queda activa por omisión.
6. **Analógico ideal**: ADC, SAC, TIA, eCOMP y Comp_E sin ruido, *offset*, ancho
   de banda ni retraso de filtro.
   *Coste:* **Medio** (~400 líneas en común para todos). Offset, INL/DNL y
   ancho de banda vienen en las hojas de datos; **[decisión]** opcional y con
   semilla, como el punto 5.
7. **CPUXv2 sin cola de búsqueda** (PC = MAB): diferencias de ciclos en código
   que dependa de ello.
   *Coste:* **Alto**. **[doc]** TI no publica la microarquitectura; **[placa]**
   habría que deducirla de medidas de ciclos en un F5529 y una FR5969.
   **[invariantes]**. Poca ganancia para el coste.

## 2. No se pueden usar las herramientas reales

8. **GDB por la eZ-FET**: el agente GDB de TI no está en la MSP Debug Stack; se
   depura con `--gdb-dap` o `--gdb` del simulador.
   *Coste:* **no depende del simulador**: el agente es de CCS y no es abierto.
   Basta con documentarlo en la guía del alumno (**Bajo**). Si se hace el
   punto 9, CCS lo traerá consigo.
9. **Sin USB/IP (U4)**: CCS y UniFlash no ven la eZ-FET; no hay HID ni el F5529
   como dispositivo USB real; nada en Windows.
   *Coste:* **Alto** (~1 500 líneas, como X5 del XDS110). **[herramienta]** CCS y
   UniFlash a mano en Windows 10 y 11 con usbip-win2, y en Linux con
   `vhci-hcd`. Desbloquea 10, 11 y 12.
10. **El backchannel** va por el RFC 2217 de la placa, no por el segundo CDC de
    la eZ-FET.
    *Coste:* **Medio**, y solo útil con el punto 9. **[herramienta]** un
    terminal de Windows sobre el COM del backchannel.
11. **BSL**: `msp430.bsl5.hid` necesita `/dev/hidraw` o USB/IP; BSL-Scripter no
    está en `doc/refs`; faltan el BSL I2C de las FR5 y el UART del F5529.
    *Coste:* **Medio** (~300 líneas cada BSL que falta). HID depende del punto 9.
    **[doc]** descargar BSL-Scripter (licencia de TI, no se versiona).
    **[herramienta]** BSL-Scripter a mano contra cada BSL.
12. **La actualización del firmware de la eZ-FET**, sin probar.
    *Coste:* **Bajo-medio**, depende del punto 9. **[herramienta]** UniFlash o
    CCS a mano, que es lo que la lanza.

## 3. Declarado o a medias (se nota al usarlo)

13. **CapTIvate IO** (FR2433, FR4133, FR2311).
    *Coste:* **Alto** (~1 500 líneas con un modelo del electrodo). **[doc]** TI
    describe CapTIvate sobre todo a través de su biblioteca en ROM; los registros
    no están completos. **[placa]** para calibrar las cuentas con un dedo. El
    menos rentable de este bloque.
14. **LEA** sin el tiempo de sus órdenes.
    *Coste:* **Bajo-medio** (una tabla de ciclos por orden). **[doc]** las cifras
    de las notas de aplicación de LEA cubren pocas órdenes; **[placa]** medir el
    resto en una FR5994.
15. **ESI** sin la sincronización de 37-5 ni el sensor LC.
    *Coste:* **Medio-alto**. **[placa]** el sensor LC de la FR6989 LaunchPad
    (o de su BoosterPack) para ver la forma de la señal.
16. **Conexiones internas declaradas**: RTC a las capturas de TA0/TB0/TA2, VLO a
    CCI0B; en el FR2355, TB3 CCR0B→TB1, la entrada MFM del eCOMP1 y la salida IR
    invertida del eUSCI_A1.
    *Coste:* **Bajo** (~50 líneas cada una, con su prueba). Ninguna necesita
    placa: las tablas de las hojas de datos bastan.
17. **ICC** con la pila llena y el RETI de una NMI.
    *Coste:* **Bajo-medio**. **[doc]** la guía es escueta en estos dos casos;
    **[placa]** confirmarlos en una FR2355.
18. **Reanudación remota USB** contestada al momento.
    *Coste:* **Bajo** (los tiempos de la norma USB 2.0, §7.1.7.7).
19. **USCI** sin autobaud ni I2C multimaestro.
    *Coste:* **Medio** (~400 líneas los dos). Autobaud está bien documentado; el
    arbitraje multimaestro necesita una pieza I2C maestra para probarlo.

## 4. Menor

20. Consumo y EnergyTrace.
    *Coste:* **Medio-alto** (~1 200 líneas con su ventana, como X3+X4 del
    XDS110). **[placa]** medir corrientes con EnergyTrace real para calibrar;
    **[herramienta]** EnergyTrace de CCS necesita el punto 9.
21. FRAM sin errores de bit ni ECC (GCCTL1 = 0).
    *Coste:* **Bajo** (una inyección de errores a petición).
22. El fusible JTAG es un solo bit; sin almacenamiento de estado del EEM L; el
    JTAG de 4 hilos de las FR2 sin prueba propia.
    *Coste:* **Bajo**. La prueba de 4 hilos necesita la sonda de bancada.
23. LCD: 8-mux con bias 1/3; los símbolos se pintan como marcas.
    *Coste:* **Bajo-medio**. Los símbolos reales de la pantalla de la FR4133 y la
    FR6989 necesitan su dibujo (**[doc]** la hoja del cristal LCD).
24. El puente J101 quitado sigue dibujado como puesto.
    *Coste:* **Bajo**, en `mcu-sim-gui`.
25. A revisar: `msp_exp430fr5969.xml` sin firmware da «sobrecorriente» con la
    fuente USB; los ejemplos del G2553 están escritos aquí (faltan los SLAC485).
    *Coste:* **Bajo**. **[doc]** descargar los SLAC485 de TI.
26. La CI se salta las pruebas de la eZ-FET si no tiene las imágenes; el pty no
    existe en Windows.
    *Coste:* la CI, **no tiene arreglo barato**: las imágenes no se pueden
    versionar por su licencia (como mucho, un secreto del CI con la MSP Debug
    Stack). El pty en Windows, **Medio** (ConPTY o tuberías con nombre),
    **[herramienta]** probarlo a mano en Windows.

## Balance del coste

* **Barato y con efecto**: 3 (fallo del modelo), 16, 18, 21, 24, 25 y HFXTOFFG
  del punto 5. Una o dos semanas en total.
* **Medio y lo que más programas rompe**: 1 (FRAM) y 2 (JTAG). Los dos piden una
  sesión con placas reales y el 1 mueve invariantes.
* **Caro**: 9 (USB/IP), que es la puerta de 10, 11, 12 y del EnergyTrace de 20,
  y 13 (CapTIvate). 7 no compensa.
* **Trabajo a mano**: una tarde de medidas con la FR5969, la FR2433, la F5529 y el
  G2553 (puntos 1, 2, 3 y 17) rinde más que cualquier otro paso; las pruebas con
  CCS, UniFlash y BSL-Scripter en Windows solo tienen sentido tras el punto 9.

## Por dónde empezar

Por 1-3: el tiempo de la FRAM es lo que más programas reales rompe, y 2 y 3 son
fallos del modelo, no simplificaciones.
