# Una fresadora CNC de tres ejes con husillos roscados

*Análisis del 2026-10-28. Se pidió estudiar qué cuesta modelar y ver en la
ventana una CNC de tres ejes, empezando por motores más potentes que el
motorreductor TT —de continua y paso a paso—, sus drivers, las piezas que
unen el eje del motor a la guía y la visualización del movimiento, con un plan
por fases. Las referencias que se dieron son la fresadora VEVOR 3020 y los
catálogos de Nanotec: motores BLDC y paso a paso, controladores, frenos,
reductoras, husillos y tuercas.*

**Dos palabras.** En este documento:

* **Husillo** es el **husillo roscado**: la varilla con rosca que, con su
  tuerca, convierte el giro en avance (en inglés, *lead screw* o *ball
  screw*).
* **Cabezal** es el motor que hace girar la fresa (en inglés, *spindle*).

En el habla de taller los dos se llaman a veces «husillo».

## 0. Resumen

| | |
| :--- | :--- |
| **¿Se puede?** | Sí. Ningún elemento es de una dificultad nueva para `mcu-sim`. El modelo eléctrico, el `MotorCC` con su inductancia, los temporizadores con encoder, PWM e interfaz de sensores Hall, el `<sistema>` de placas, el puente serie y la ventana con sus efectos ya hacen lo difícil |
| **Lo que de verdad es nuevo** | **La cadena mecánica.** Hoy cada motor lleva su carga dentro (el mando `carga`). Una CNC necesita que el par del motor pase por una reductora, un acople, un husillo y una tuerca hasta un carro con masa, rozamiento, recorrido y gravedad, y que todo eso vuelva al motor como inercia y par resistente. Es la decisión de arquitectura de este análisis (§4) |
| **La segunda novedad** | **El driver de paso a paso no puede simularse conmutando.** Un *chopper* de 20 a 40 kHz por fase, en tres ejes, son cientos de miles de cambios de nodo por segundo. La propuesta es una rama nueva para la red, de **corriente impuesta con tope de tensión** (§5.2): el driver dice qué corriente quiere y el motor la sigue mientras la tensión del bus le llegue. Así sale igual la caída de par con la velocidad, que es lo que importa |
| **El coste de simular** | Asumible. Medido hoy: un `MotorCC` con PWM de 1 kHz e inductancia cuesta **0,11 s de CPU por segundo simulado**; parado o a tensión fija, 0,014. Tres paso a paso, el firmware y el cabezal deberían quedar **por debajo de 0,5 s/s**, todavía más rápido que el tiempo real (§10). Es una estimación que la fase 1 tiene que medir |
| **El firmware** | Hay dos caminos, y conviene recorrer los dos. Primero, uno **propio y pequeño** en `verif/fw`, para las pruebas. Después, **grblHAL sin tocar**: tiene soporte oficial para la Nucleo-F446RE, y que un firmware de producción ajeno funcione es la mejor prueba de fidelidad que se le puede pedir a `mcu-sim` (§8) |
| **La visualización** | Se reutiliza casi todo: <br>• Un efecto nuevo y pequeño, **`desplaza`**, el «`angulo`» lineal. <br>• El **`display`** de ayer, para las cotas X, Y y Z. <br>• El mecanismo de imágenes de la TFT, para **la pieza vista desde arriba con lo que se ha fresado**. <br>Las tres vistas, planta, alzado e isométrica, salen de un dibujo SVG sin motor 3D. El 3D de verdad (Qt Quick 3D) queda como fase opcional (§9) |
| **El plan** | Ocho fases (§11). Las cuatro primeras dan una 3020 simulada que se mueve con G-code, mecaniza una pieza y se ve: <br>• **F0**, los cimientos. <br>• **F1**, los motores y sus drivers. <br>• **F2**, la transmisión. <br>• **F3**, la máquina con su firmware. <br>Las siguientes amplían: <br>• **F4**, la pieza. <br>• **F5**, la potencia de 48 V con freno y energía regenerada. <br>• **F6**, el bus de campo. <br>• **F7**, el BLDC gobernado desde el MCU. <br>• **F8**, el 3D |

## 1. La pregunta

Una fresadora CNC de sobremesa tiene:

* tres ejes lineales movidos por motores a través de husillos roscados;
* un cabezal que hace girar la fresa;
* finales de carrera, una sonda y una seta de emergencia;
* un controlador que interpreta G-code.

Se pide analizar cuánto cuesta llevar eso a `mcu-sim` y a su ventana, **empezando por los motores y sus drivers**, con motores más potentes que el TT de 6 V del sistema `nucleo_f446re_encoder_lcd_servo_2xmcc.xml`, tanto de corriente continua (incluidos los BLDC de Nanotec) como paso a paso. Después vienen el acoplamiento mecánico y la visualización del movimiento.

## 2. La máquina de referencia: la VEVOR 3020

La página que se dio no se deja leer con su dirección original, que es demasiado larga para la herramienta. El mismo producto (id `010249298884`) está publicado en vevor.nl, y de ahí salen estas cifras. Como la ficha calla mucho, se completan con las de su hermana pequeña, la 3018 de la misma marca:

| | VEVOR 3020 (la pedida) | VEVOR 3018 (su hermana) |
| :--- | :--- | :--- |
| Área de trabajo | 300 × 200 × 72 mm | 300 × 180 × 45 mm |
| Cabezal | 120 W de máximo, 75 W nominales | Motor 775 de continua, 12–24 V, 10 000 rpm |
| Velocidad de los ejes | 1200 mm/min (20 mm/s) | — |
| Precisión | Repetibilidad ≤ 0,05 mm | Posicionado 0,08 mm |
| Motores de los ejes | No lo dice | Tres paso a paso de 12 V, 1,3 A y 0,25 N·m |
| Husillos | No lo dice | Acero inoxidable, con **doble tuerca de latón** contra la holgura. La URL dice T8 |
| Control | GRBL 1.1f, USB y mando sin ordenador | GRBL, USB y mando sin ordenador |
| Alimentación | 110/220 V de red | 24 V, 5 A |
| Bastidor | Aluminio | Aluminio y baquelita |

**Lo que no publican y hay que suponer:**

* **Los motores son NEMA 17.** El tamaño 42 mm es el de 0,25 N·m.
* **Los drivers son de la familia A4988 o DRV8825** soldados en la placa. La
  placa GRBL de SainSmart para la 3018, la más documentada, lleva DRV8825.
* **El husillo es un Tr8.** Su avance (2, 4 u 8 mm por vuelta) no lo dice
  nadie. **Lo único seguro es que es un parámetro**, y que cambia la máquina
  entera. A 20 mm/s, un avance de 2 mm pide 600 rpm y 16 kHz de pasos con
  1/8 de micropaso. Uno de 8 mm pide 150 rpm y 4 kHz.

Esa 3020 es el **punto de partida**. La versión potente que se pide (§5) es la misma máquina con motores NEMA 23 o BLDC de Nanotec, a 48 V, con husillos de bolas y freno en el eje Z.

## 3. Lo que ya hay en `mcu-sim` y lo que falta

**Lo que se reutiliza tal cual o casi:**

| Pieza o mecanismo | Para qué en la CNC |
| :--- | :--- |
| `AnalogNet`, `RamaDosNodos`, `RamaFem` | Toda la parte eléctrica: alimentación, consumo y masas |
| **`MotorCC`** con `l_mh` y su encoder (§4.16 de `parts.md`) | El **cabezal 775** sale cambiando las cifras. Y su integrador (RK4 a ≤ 5 µs, despertar en flancos y cambios de signo «ensayados de antemano») es **el patrón** de todos los motores nuevos |
| `L298` con `diodos` | La plantilla de un puente en H. Un BTS7960 es otro juego de parámetros y una tabla de verdad distinta (§5.3) |
| `Fuente` con `media_ms` y el **efecto `display`** | La fuente de 24 o 48 V que enseña lo que consumen los motores. El mismo `display` sirve para las **cotas X, Y y Z** |
| `Button` (con rebotes) | Los **finales de carrera**, la sonda y la seta: un pulsador que no aprieta la mano sino la posición del carro |
| `Conector` y `<sistema>` (P-15) | La **CNC Shield V3** enchufada en los conectores Arduino de la Nucleo, y la máquina como una placa más |
| Temporizadores del F446: PWM, modo encoder, **OPM**, **interfaz de sensores Hall** (`TI1S`) y **BDTR** (tiempos muertos) | Los pulsos STEP, el PWM del cabezal y, en F7, la conmutación de un BLDC desde el MCU |
| `PuenteSerie` (P-14): memoria, TCP y RFC 2217 | Que un **emisor de G-code de verdad** (Candle, UGS, CNCjs, bCNC) hable con el firmware simulado |
| bxCAN, `CanTransceiver`, `CanWire` | El **CANopen** de los controladores Nanotec, en F6 |
| La escritura en flash del F4 (`mem/flash_if.h`) | Los ajustes `$` de grblHAL, que guarda en flash |
| El mecanismo de **imágenes** (`T_IMAGEN`, el de la TFT) | **La pieza vista desde arriba**, con lo fresado sombreado por profundidad (§9) |

**Lo que no hay:**

* **Ninguna conexión mecánica entre piezas.** El eje de un motor no puede
  unirse a nada; su carga va dentro (§4).
* **Motores paso a paso, BLDC, sus drivers, frenos, reductoras, husillos,
  carros y la pieza.**
* **Dos piezas eléctricas sueltas**: un **MOSFET** de potencia con su diodo,
  para el cabezal, y un **condensador**, para el bus de 48 V cuando un motor
  devuelve energía (§6.6).
* **En la ventana**: un efecto que **traslade** un elemento del dibujo según
  un observable, y que admita varios observables con su vector cada uno (§9).

## 4. El problema de fondo: la cadena mecánica

**Hoy el motor lo sabe todo de su carga.** `MotorCC` integra `J·dω/dt = Kt·i − Tf − Tc`. La inercia `J` sale de su `tau_ms` y la carga `Tc` de un mando. En una CNC eso tiene que salir de lo que hay detrás:

```
motor ── reductora ── acople ── husillo ══ tuerca ── carro (+ guía, + pieza)
 J_m      i, η, holg.   J_a     p, η, J_h   holgura   m, rozamiento, recorrido,
                                                     gravedad, fuerza de corte
```

**Una sola masa, para empezar.** Si los acoples son rígidos, que es razonable salvo por las resonancias torsionales de varios kHz que no interesan aquí, toda la cadena es **un solo grado de libertad**. Todo se refleja al eje del motor:

* La inercia equivalente es `J = J_motor + J_acople + J_husillo + m·(p/2π)²/i²`.
* El par resistente es la fuerza en la tuerca pasada por el rendimiento: `F·p/(2π·η)`.

Con eso, el motor integra una ecuación más rica, pero sigue siendo una.

**Tres maneras de escribirlo:**

| | Cómo es | A favor | En contra |
| :--- | :--- | :--- | :--- |
| **A. Pieza compuesta** | Una pieza `EjeLineal` lleva dentro motor, husillo y carro, con todos sus parámetros | Es lo más rápido de hacer | Una combinación por pieza: paso a paso con husillo, BLDC con reductora y husillo… Y no deja ver ni cambiar una pieza suelta |
| **B. Red mecánica general** | Como `AnalogNet`, pero de par y velocidad, con nodos mecánicos y elementos elásticos entre ellos | La más general: resonancias, dos motores contra un pórtico con flexión | Un resolvedor nuevo, rígido y caro. Es demasiado para lo que se va a ver |
| **C. Cadena rígida declarada** | Las piezas mecánicas son piezas como las demás, pero pasivas. Una **cadena** (`<transmision>`) las ordena desde el motor hasta el carro. Al elaborar, cada cadena calcula su inercia y sus pares reflejados, y **un solo integrador por cadena** lleva el eje y los estados eléctricos de sus motores | Una pieza por cosa física, intercambiables. Un grado de libertad, el coste de hoy. **Admite varios motores en una cadena**: el pórtico de dos motores en Y | Hay que sacar el integrador del `MotorCC` a la cadena. Un motor suelto tiene una cadena implícita con su carga de siempre, así que **M9 y M10 no deben moverse ni una rpm** |

**La recomendación es la C.** En el XML sería algo así:

```xml
<componente tipo="MotorPaP"  id="MX" ...> <pin .../> </componente>
<componente tipo="Acople"    id="AX" inercia_gcm2="3"/>
<componente tipo="Husillo"   id="HX" rosca="Tr" diametro_mm="8" avance_mm="8"
            mu="0.15" holgura_mm="0.04" longitud_mm="350"/>
<componente tipo="Carro"     id="CX" masa_kg="1.5" recorrido_mm="300"
            rozamiento_n="4" eje="x"/>
<transmision id="X" cadena="MX AX HX CX"/>
```

**La holgura sin otro grado de libertad.** Se trata como una **zona muerta cinemática**. Mientras el motor recorre el hueco, el carro se queda donde está, sujeto por su rozamiento, y la cadena integra solo la inercia del lado del motor. Al cerrarse el hueco, vuelve a ser una masa. Es lo que hace la doble tuerca de la 3018, y lo que el firmware compensa con la compensación de holgura de grblHAL.

**Los topes duros** son una restricción: el carro llega al final del recorrido, la fuerza empuja contra el tope y la cadena se bloquea, que es el `bloquear` del `MotorCC` de hoy. Lo que pasa entonces sale solo:

* **Un paso a paso pierde pasos.** El rotor deja de seguir al campo y el
  driver no se entera.
* **Uno de continua pide la corriente de bloqueo**, y la fuente lo enseña.

## 5. Los motores

### 5.1 Paso a paso

**El modelo es el clásico de un híbrido bipolar de dos fases**, con `Nr = 50` dientes de rotor (1,8° por paso):

```
va = R·ia + L·dia/dt − Km·ω·sin(Nr·θ)
vb = R·ib + L·dib/dt + Km·ω·cos(Nr·θ)
T  = −Km·ia·sin(Nr·θ) + Km·ib·cos(Nr·θ) − Td·sin(4·Nr·θ)
J·dω/dt = T − (lo que devuelve la cadena)
```

`Td` es el par de retención sin corriente (*detent*).

**Todo lo que importa sale de esas cuatro líneas, sin programarlo aparte**:

* la curva par–velocidad, que cae cuando la f.e.m. y `L·di/dt` se comen la
  tensión del bus;
* la **pérdida de pasos** por aceleración excesiva o por carga;
* la **resonancia** a media velocidad;
* el arrastre del rotor por el campo, con su retraso, que crece con la carga.

**Las cifras de la versión potente** salen de la ficha del Nanotec **ST5918L3008-A**:

| | Valor |
| :--- | :--- |
| Tamaño | NEMA 23, 56 mm |
| Par de retención | 1,87 N·m |
| Corriente | 3 A por bobina |
| Resistencia | 1 Ω por bobina |
| Inductancia | 1,9 mH por bobina |
| Inercia del rotor | 480 g·cm² |

Con 3 A, la constante es `Km ≈ 0,44 N·m/A` si el par de retención se mide con las dos fases, y 0,62 N·m/A si se mide con una. Las fichas no siempre lo dicen, así que `km` debe poder darse aparte.

**La velocidad de esquina** es un buen número para saber si el modelo es sensato: la velocidad a la que el driver ya no consigue meter la corriente en la bobina. Con `ω_e ≈ V/(L·I)`:

| Bus | Frecuencia eléctrica | Velocidad de esquina |
| :--- | :--- | :--- |
| 24 V | ~670 Hz | ~800 rpm |
| 48 V | ~1340 Hz | ~1600 rpm |

**Por eso los Nanotec van a 48 V**, y el modelo tiene que enseñarlo.

**El de la 3018** (NEMA 17, 0,25 N·m, 1,3 A) es el mismo modelo con otras cifras. Nanotec lo tiene en su serie **ST4118** (NEMA 17: 9–80 N·cm, 0,22–3 A).

**Un modelo cinemático aparte, como opción.** Además del dinámico, hay sitio para un `modelo="ideal"`: cada paso es exactamente un paso, sin corrientes ni pérdidas. Cuesta casi nada y vale para probar firmware deprisa. El dinámico es el de omisión.

### 5.2 Drivers de paso a paso

**Los que hay que tener**, por ser los de las máquinas reales y los de la CNC Shield:

| Driver | Tensión | Corriente | Micropasos | Lo que lo distingue |
| :--- | :--- | :--- | :--- | :--- |
| **A4988** (Allegro, módulo Pololu) | 8–35 V | ~2 A con disipador | 1/16 | El más común. Decaimiento mixto. Pulso STEP mínimo de 1 µs. Nivel alto de 0,7·VDD (§8.1) |
| **DRV8825** (TI) | 8,2–45 V | ~2,2 A | 1/32 | El de la placa GRBL de la 3018 de SainSmart. Nivel alto fijo de 2,2 V |
| **TMC2209** (Trinamic/ADI) | 4,75–29 V | 2 A eficaces | 1/256 interpolados | StealthChop (silencioso) y SpreadCycle. **UART** para configurarlo, **StallGuard** para detectar el bloqueo sin final de carrera |
| **Driver externo tipo DM542** | 20–50 V | hasta 4,2 A | DIP | El de los NEMA 23. **Entradas optoacopladas de 5 V**: con un MCU de 3,3 V hace falta pensarlo, y eso enseña algo |
| **Nanotec C5** (sin lazo) | 12–48 V | 6 A | — | Reloj y dirección o entrada analógica (§5.5) |

**El modelo eléctrico, que es la decisión importante.** Un driver real es un *chopper*: conmuta el puente a 20–40 kHz para que la corriente de cada fase siga la consigna del micropaso. Hay tres formas de llevarlo a `mcu-sim`:

1. **Conmutando de verdad.** Cada conmutación es un cambio de nodo y
   despierta al motor. Con tres ejes de dos fases a 30 kHz son unos 360 000
   eventos por segundo, que se comen el presupuesto de tiempo real. **No.**
2. **El driver integra el motor.** El driver lleva dentro las ecuaciones
   eléctricas del motor y los cables son solo una declaración. Es barato y
   exacto, pero rompe la regla de la casa de que **lo que no está cableado no
   funciona**: no se podría cruzar una bobina, ni dejarla al aire, ni medir su
   corriente. **No.**
3. **Corriente impuesta con tope de tensión. Es la que se propone.** Cada
   salida del driver no es un Thévenin sino una rama nueva de la red,
   `RamaCorriente`: «quiero `i*` y puedo poner entre `−Vbus` y `+Vbus`». El
   motor, que integra su fase con RK4, mira en cada paso si la tensión que
   haría falta (`R·i* + L·di*/dt + f.e.m.`) cabe en el tope:
   * **Si cabe, la corriente es la consigna.** Es lo que consigue un chopper
     bueno.
   * **Si no cabe, la tensión se queda en el tope y la corriente va por
     detrás.** Es la caída de par con la velocidad.

   El driver solo despierta en los flancos de STEP, en los cambios de DIR y
   EN, y para refrescar lo que gasta de su fuente. Lo que gasta sale de la
   potencia en las fases: `Σ v·i / Vbus` más el consumo en reposo. **No hay
   eventos de chopper.** El rizado del chopper se pierde, pero no lo ve nadie
   (§10).

   Es la generalización de lo que ya hace `RamaFem`. Allí el motor pregunta a
   la red su Thévenin exterior (`fuera(vth, rth)`); aquí la red puede
   contestar «soy una fuente de corriente con estos límites».

**Lo que el driver tiene que comprobar, porque es lo que se equivoca en la vida real:**

* el ancho mínimo del pulso STEP;
* el tiempo de preparación de DIR antes de STEP (unos 200 ns en el A4988);
* el ENABLE activo a nivel bajo;
* el ajuste de corriente, que en el módulo Pololu es un potenciómetro y una
  `Vref`;
* que una fase al aire no da par;
* la protección térmica y de sobrecorriente.

Cada fallo es un aviso con nombre, como los del `Servo`.

### 5.3 Continua con escobillas, de más potencia

Hay dos usos, y ninguno necesita un modelo nuevo de motor: es `MotorCC` con otras cifras.

* **El cabezal: el motor 775 de 24 V.**
  * Ronda las 10 000 rpm en vacío. La corriente en vacío (~0,5 A) y la de
    bloqueo (decenas de amperios) son **estimaciones**, porque los
    vendedores no publican la ficha.
  * Lo gobierna el PWM del cabezal de GRBL a través de un **módulo MOSFET**,
    que es la pieza nueva: un N-MOSFET de nivel lógico (IRLZ44N o similar)
    con su `R_on`, su umbral y su diodo de libre circulación.
  * Sin diodo, la inductancia del 775 se lleva por delante el MOSFET. Con
    `diodos="no"` el modelo debe avisar, como ya hace el L298.
* **Un eje servo de continua**, como alternativa didáctica al paso a paso: un
  motorreductor de 37 mm con encoder de efecto Hall (tipo JGB37-520) y un
  puente **BTS7960** (el módulo IBT-2, 43 A).
  * El lazo de posición lo cierra el **firmware**: un PID con el encoder en
    el TIM en modo encoder, que ya está.
  * El BTS7960 es un `L298` con otros parámetros:
    * dos medios puentes con `IN` e `INH` en lugar de `ENA`, `IN1` e `IN2`;
    * `R_on` de miliohmios en lugar de caídas de 1,2 V;
    * salidas `IS` de medida de corriente, que son una fuente de corriente
      proporcional a la de la carga.

### 5.4 BLDC

**El motor de referencia es el Nanotec DB59L024035-A** (NEMA 23):

| | Valor |
| :--- | :--- |
| Tensión | 24 V |
| Potencia | 172 W |
| Velocidad | 3500 rpm |
| Corriente nominal | 9,4 A |
| Corriente de pico | 28 A |
| Constante de par | 5 N·cm/A |
| Resistencia entre fases | 0,22 Ω |
| Inductancia entre fases | 0,29 mH |
| Inercia del rotor | 119 g·cm² |
| Sensores | Hall |

**Las cuentas cuadran**: `Kt·I = 0,05 × 9,4 = 0,47 N·m`, y `172 W / 366 rad/s = 0,47 N·m`. El pico es de 1,4 N·m.

**Dos niveles de modelo, según quién lo conmute:**

* **Con un driver inteligente** (§5.5), que es lo de la máquina:
  * El driver hace la conmutación y la FOC por dentro, y desde fuera nadie
    las ve.
  * El motor se modela **en el eje q, como un motor de continua
    equivalente**: `L·di/dt = v − R·i − Ke·ω` y `T = Kt·i`.
  * Los Hall y el encoder se generan de la posición del rotor, para que se
    puedan cablear a donde haga falta.
  * Los tres hilos U, V y W **se cablean y se comprueban** (que estén, que
    no se crucen con otra cosa), pero la corriente que circula es la del
    eje q. Es la misma rama de corriente impuesta del §5.2.
* **Conmutado por el propio MCU** (F7):
  * Tres fases de verdad: tres `RamaFem` con f.e.m. trapezoidal o
    sinusoidal y los Hall.
  * Un puente trifásico, por ejemplo la **X-NUCLEO-IHM07M1** (L6230), que se
    enchufa en la Nucleo.
  * El TIM1 con salidas complementarias y tiempos muertos, y el TIM2 o el
    TIM3 en modo sensor Hall. Todo eso ya está modelado.
  * Aquí sí hay PWM real: 20 kHz por tres fases, el coste de un motor con
    PWM multiplicado por 20 (§10). Por eso va al final.

### 5.5 Los controladores inteligentes de Nanotec

Del catálogo de controladores:

| Familia | Tensión | Nominal / pico | Motores | Mandos y buses |
| :--- | :--- | :--- | :--- | :--- |
| **C5** | 12–48 V | 6 / 6 A | Paso a paso NEMA 17–34, sin lazo | Reloj y dirección, entrada analógica |
| **C5-E** | 12–48 V | 6 / 6 A o 10 / 30 A | BLDC y paso a paso, NEMA 17–34 | Reloj y dirección, entradas analógicas y digitales, **lazo cerrado con encoder**, **salida de freno**. Buses: Modbus RTU/TCP, CANopen, EtherNet/IP, EtherCAT. Se programa para funcionar solo |
| **CL3-E** | 12–24 V | 3 / 3–6 A | BLDC y paso a paso hasta NEMA 23 | CANopen, Modbus RTU |
| **CL4-E** | 12–58 V | 3–6 / 6–18 A | BLDC y paso a paso | CANopen, Modbus RTU |
| **CLC** | 12–57,6 V | 3–15 / 3–45 A | BLDC y paso a paso (el CLC15, solo BLDC) | FOC con modos CiA 402 (perfil de posición, velocidad y par, *homing*, síncronos) y reloj y dirección. Hall, encoder incremental y SSI. Salida de freno por PWM. 6 entradas digitales, 2 analógicas y 2 salidas. NanoJ |
| **N5 / N6** | 12–72 / 12–57,6 V | 10–18 / 10–40 A y 6 / 6–18 A | BLDC y paso a paso | EtherCAT, CANopen, EtherNet/IP, Modbus, PROFINET (N6) |

**Lo que se modela es un controlador genérico** (`ControladorMotor`), con parámetros que lo acercan a un C5-E o a un CL3-E, y **por capas**:

1. **Reloj y dirección, en lazo cerrado. Es lo de F5.**
   * Desde fuera es un driver de pasos: el firmware de la CNC no cambia.
   * Por dentro es una cascada de lazos de posición, velocidad y corriente
     sobre el encoder del motor, con límite de corriente y de tensión.
   * Tiene una **salida de freno**: suelta el freno al habilitar y lo echa
     al deshabilitar o al fallar, con su retardo.
   * Tiene **entradas de final de carrera** y una **salida de error**: si el
     error de seguimiento pasa de un límite, se para y lo dice. Es la
     diferencia práctica con un paso a paso sin lazo, que pierde pasos sin
     enterarse.
2. **CANopen** con un subconjunto de CiA 402 (F6):
   * la máquina de estados del drive (*Switch on disabled* → *Ready* →
     *Operation enabled*);
   * el perfil de posición, el *homing* y unos PDO;
   * por el bxCAN y el `CanTransceiver`, que ya están.

   El Modbus RTU es la alternativa: necesita un transceptor RS-485 nuevo,
   pequeño.
3. **NanoJ**, los programas que corren dentro del controlador: **fuera de
   alcance**.

Nanotec vende también motores con el controlador montado (los PD4 y PD6, «motores con controlador»). En el modelo son la misma pareja metida en una sola placa.

## 6. La transmisión

### 6.1 Reductora

Del catálogo de Nanotec:

| Familia | Relaciones | Rendimiento | Holgura | Par nominal |
| :--- | :--- | :--- | :--- | :--- |
| Planetaria de precisión (GPLE/GPLEP) | 3–512 | 48–98 % | 7–55′ | 1,5–130 N·m |
| Planetaria de alto par | 3,29–256 | 70–92 % | 21–60′ | 6,5–48 N·m |
| Planetaria silenciosa | 3,24–35 | 81–93 % | 18–48′ | 0,8–11,8 N·m |
| Planetaria económica (GPLL) | 4,66–89,7 | 60–80 % | **150′** | 0,2–0,4 N·m |
| Sinfín (GSGE) | 5–50 | 45–86 % | hasta 120′ | 35–64 N·m |

**El modelo** es una relación, un rendimiento, una holgura en minutos de arco (la misma zona muerta del §4), una inercia en la entrada y un par máximo, que se avisa si se pasa.

**El sinfín necesita además el autobloqueo**: con rendimiento inverso nulo, la carga no puede mover el motor. Es lo que sujeta un eje Z sin freno.

**Dónde hace falta**: entre un BLDC de 3500 rpm y un husillo. A 3500 rpm directo, un Tr8 pasa de su velocidad crítica (§6.3) y su tuerca de plástico no lo aguanta. Una 1:3 o 1:5 deja el BLDC en su zona buena.

### 6.2 Acople

**No está en los enlaces que se dieron, y es imprescindible.** Une el eje del motor (5 mm en NEMA 17, 6,35 u 8 mm en NEMA 23) con el del husillo (8 mm en un Tr8, 10 o 12 mm mecanizado en un 1605). Los habituales son el **elástico de espiral** (el de la 3018), el **de mordaza con estrella de elastómero** y el **de fuelle metálico**.

**En el modelo** es una inercia y, si acaso, una holgura (la estrella de un acople de mordaza tiene alguna). Su rigidez torsional no se modela (§4). Lo que sí se comprueba es que **los diámetros encajan**, como se comprueba que una placa encaja en un conector. Así no sale una máquina que monta un acople de 5 en un eje de 6,35.

### 6.3 Husillo y tuerca

Nanotec los hace de rosca **trapezoidal** (30°) y **ACME** (29°), de acero inoxidable, con holgura axial estándar de ±0,03 a ±0,07 mm. Las tuercas pueden ser normales o **antiholgura** de POM pretensado.

Los **husillos de bolas** (SFU1605 y similares) no están en ese catálogo, pero son los de cualquier CNC seria y se añaden.

**El modelo no pide el rendimiento: lo calcula.** Lo saca del ángulo de la hélice, `λ = atan(p/(π·d_m))`, y del rozamiento de la tuerca. Así salen **el rendimiento directo, el inverso y el autobloqueo**, que es lo que de verdad cambia la máquina:

| Husillo | λ | Rendimiento directo | Inverso | ¿Se sujeta solo? |
| :--- | ---: | ---: | ---: | :--- |
| Tr8, avance 2 mm, tuerca de latón (μ ≈ 0,15) | 5,2° | 0,36 | 0 | **Sí**: el Z no se cae sin corriente |
| Tr8, avance 8 mm (4 entradas), μ ≈ 0,15 | 20° | 0,66 | 0,54 | **No** |
| Bolas 1605 (d = 16, avance 5, μ ≈ 0,005) | 5,7° | 0,95 | 0,95 | **No**: hace falta freno en Z |

Estas cifras son cálculos con valores típicos de μ. **El parámetro es `mu`**, y el rendimiento sale de él.

**El husillo también aporta:**

* **Su inercia.** Un Tr8 de 350 mm tiene ~11 g·cm². Un 1605 de 400 mm tiene
  **~200 g·cm²**, del orden del rotor de un NEMA 23. No se puede despreciar.
* **La conversión de par en fuerza**, `F = 2π·η·T/p`. Con 1 N·m, un Tr8×2
  empuja ~1400 N y un Tr8×8 ~590 N. Con los 0,25 N·m de la 3018 y un Tr8×8,
  147 N.
* **Dos avisos de límite**, que no paran la simulación:
  * La **velocidad crítica**: el husillo vibra como una cuerda. Con ambos
    extremos apoyados es `n_c ≈ 1,2·10⁵·d_r/L²` rpm, con `d_r` y `L` en
    metros. Un factor la corrige según cómo esté sujeto. Para un Tr8 de
    350 mm salen unas 6000 rpm; con un extremo libre, menos de la mitad.
  * El **DN** de los husillos de bolas, `d·n ≲ 70 000`.

**La tuerca** lleva la holgura: simple (unas centésimas), doble (la de la 3018) o antiholgura (casi cero). Es una zona muerta, como en el §4.

### 6.4 Guías y carro

**Tampoco están en la lista.** Las guías llevan el carro mientras el husillo lo empuja, y sin ellas el husillo trabajaría a flexión. En la 3018 son **ejes calibrados de 10 mm con rodamientos lineales** (LM10UU). En una máquina seria son **guías de perfil** (MGN12, HGR15).

**En el modelo**, guías, carro y mesa son una sola pieza, `Carro`:

* **una masa**, la del carro más lo que lleva encima: el carro X lleva el Z;
  el Z lleva el cabezal;
* **un rozamiento** seco y otro viscoso;
* **un recorrido** con topes duros;
* **la gravedad**, si el eje es vertical;
* **una entrada de fuerza externa**, para el corte (F4).

Su **posición en mm** es el observable que mueve el dibujo (§9).

### 6.5 Freno

Nanotec tiene dos, ambos **de muelle**: frenan sin corriente y se sueltan con 24 V.

| | Potencia | Montaje |
| :--- | :--- | :--- |
| **BWA** | 5,9–11 W | Ultracompacto, directo sobre el motor, ejes de 5 o 6,35 mm |
| **BCD** | 6,9–13,5 W | Frontal, con su eje y brida, IP65. Para NEMA 17, 23, 24 y 34 o de 80 mm, también con reductora |

El catálogo da un par de retención de 35 a 500 N·cm para el conjunto. Hay también un **módulo EB-Brake** que, por PWM, baja la potencia de mantenimiento.

**El modelo** es pequeño:

* una bobina de `24²/P` ohmios, que gasta de la fuente;
* un retardo al soltar y otro al frenar. Las fichas no los dan; **decenas de
  ms son una estimación** que habrá que sacar de un PDF;
* un par de retención que entra en la cadena como **rozamiento estático**.

**Lo que enseña**: con un husillo de bolas en el Z y la fuente apagada, el cabezal se cae si no hay freno. Con un Tr8×2, no.

### 6.6 La energía que vuelve

**Al frenar una masa, o al bajar el Z, el motor genera.** El driver la devuelve al bus, y una fuente de laboratorio **no la absorbe**. La tensión del bus sube y, en una máquina real, se lleva por delante el condensador del driver o dispara su protección de sobretensión.

**Hace falta modelarlo**, y no cuesta:

* un **condensador** de bus, la pieza nueva;
* que la `Fuente` diga si absorbe o no (`absorbe="no"`, que es lo de una
  fuente conmutada);
* una **resistencia de frenado** con su comparador, *chopper* de frenado, en
  el controlador o aparte.

Es lo único de la versión de 48 V que no se ve en la de 24 V. Va en F5.

## 7. Lo que falta en la lista

Además de las siete referencias que se dieron, una CNC necesita:

| Elemento | Por qué | Pieza en `mcu-sim` | Fase |
| :--- | :--- | :--- | :--- |
| **Acople** motor–husillo (espiral, mordaza, fuelle) | Sin él no se transmite nada | `Acople` (§6.2) | F2 |
| **Soportes del husillo**: rodamiento fijo y libre (KFL08, BK12/BF12) | Fijan el husillo y se llevan el empuje axial. Cambian el factor de la velocidad crítica | Un parámetro del `Husillo`: `apoyos="fijo-libre"` | F2 |
| **Guías lineales** y carro | Llevan la carga (§6.4) | `Carro` | F2 |
| **Finales de carrera** (micro NC o inductivo NPN de 6–36 V) y referencia | El *homing* (`$H`) y los límites duros. **El inductivo de 24 V no se puede conectar directo a un pin de 3,3 V**: optoacoplador o divisor | `FinalCarrera`, un `Button` accionado por la posición de un carro | F3 |
| **Sonda de herramienta** (placa de contacto) | El cero de Z (`G38.2`) | `Sonda`: contacto entre la fresa y una placa a una altura | F3 |
| **Seta de emergencia** | Tiene que **cortar la potencia de los drivers**, no solo avisar al MCU. Un análisis didáctico de una CNC no puede pasarla por alto | `Button` NC y un relé, o el `ENABLE` de los drivers | F3 |
| **Cabezal** (775 a 24 V, o BLDC de 300–500 W con pinza ER11 y su variador) | Gira la fresa. Su velocidad la pide el PWM de GRBL (`M3 S…`) | `MotorCC`, y un **módulo MOSFET** nuevo; o un BLDC con su driver | F3 |
| **Fuente** de 24 o 48 V | Potencia para los motores | `Fuente`, con `absorbe` y un **condensador** de bus | F3 / F5 |
| **CNC Shield V3** (Protoneer) | Lleva tres o cuatro zócalos Pololu y las entradas de GRBL, y se enchufa en los conectores Arduino de la Nucleo | Una placa con `Conector`, como `nucleo_y_shield.xml` | F3 |
| **Desplazadores de nivel** y optoacopladores | Para las entradas de 5 V de un DM542 o los sensores de 24 V | `Resistencia`, `Led` y un `Optoacoplador` nuevo y pequeño | F5 |
| **Transceptor RS-485** | Modbus RTU con los controladores Nanotec, si se elige en lugar de CANopen | Pieza nueva y pequeña | F6 |
| **La pieza y la fresa** (madera, MDF, aluminio; plana, en V, de bola) | Para ver qué sale (§9) | `Pieza` con su mapa de alturas | F4 |
| **Emisor de G-code** (Candle, UGS, CNCjs, bCNC) | El software «GRBL Control» que da VEVOR es Candle | No es una pieza. Se conecta por el `PuenteSerie` (§8) | F3 |
| *Cadena portacables, aspiración, refrigeración* | No cambian nada eléctrico ni mecánico de interés | Solo el dibujo. `M8` es una salida, un LED | — |

## 8. El control: firmware y anfitrión

### 8.1 La CNC Shield sobre la Nucleo

Es el montaje más común de GRBL, y la Nucleo-64 tiene los conectores Arduino. El mapa clásico de GRBL 1.1 en un Uno, traducido a los pines del F446:

| Arduino | F446 | Señal en la shield |
| :--- | :--- | :--- |
| D2 | PA10 | STEP X |
| D3 | PB3 | STEP Y |
| D4 | PB5 | STEP Z |
| D5 | PB4 | DIR X |
| D6 | PB10 | DIR Y |
| D7 | PA8 | DIR Z |
| D8 | PA9 | ENABLE (activo bajo, común) |
| D9 | PC7 | Límite X |
| D10 | PB6 | Límite Y |
| D11 | PA7 | **PWM del cabezal** (GRBL 1.1 lo cambió con el límite Z) |
| D12 | PA6 | Límite Z |
| D13 | PA5 | Dirección del cabezal. También es el **LD2** de la Nucleo: se encenderá con el cabezal |
| A0 | PA0 | Reset/abortar |
| A1 | PA1 | Retención (*feed hold*) |
| A2 | PA4 | Arranque de ciclo |
| A3 | PB0 | Refrigeración |
| A5 | PC0 | Sonda |

**Los 3,3 V de la Nucleo con los drivers.** En la shield, la lógica de los drivers va a los 5 V del conector Arduino, y eso importa:

* **El DRV8825** tiene un umbral fijo de 2,2 V, así que los 3,3 V de la
  Nucleo valen.
* **El A4988** pide un alto de **0,7·VDD, 3,5 V con 5 V**. Los 3,3 V quedan
  por debajo: funciona casi siempre, pero fuera de especificación.
* **El TMC2209** depende de cómo se alimente su VIO.
* **El DM542 optoacoplado** pide corriente para su LED (§7).

El `DriverPaP` debe comprobar estos umbrales y avisar, como hace el `Servo` con su pulso. Que salga solo en la simulación es una de las cosas que esta máquina enseña.

**grblHAL puede no usar este mapa**: el suyo para la Nucleo-64 está en sus `boards/*_map.h`, y hay que comprobarlo antes de dibujar la shield.

### 8.2 El firmware

**1. Uno propio y pequeño** (`verif/fw/cnc_*`), para F1–F3:

* genera pasos con un TIM: interrupción de actualización y Bresenham entre
  ejes, o un canal en OPM por eje;
* hace perfiles trapezoidales;
* tiene *homing*;
* entiende un subconjunto de G-code (G0, G1, G28, G38.2, M3/M5) por la USART2,
  la del VCP.

Sirve para las pruebas, y cada cosa que falle se ve en su código.

**2. grblHAL sin modificar**, en F3.

* **Lo soporta de forma oficial**: su driver STM32F4xx soporta el F401, F407,
  F411 y **F446**, con configuraciones para la Nucleo-64, y en su repositorio
  hay un entorno de PlatformIO para la Nucleo-F411RE con la shield de
  Protoneer.
* **Lo que pide al MCU:**
  * temporizadores de pasos y de pulso;
  * EXTI para los límites;
  * PWM del cabezal;
  * USART o USB CDC;
  * flash para los ajustes;
  * SysTick.

  Todo está modelado.
* **Si funciona sin tocarlo, es la mejor prueba de fidelidad que puede tener
  `mcu-sim`.** Si no, cada fallo será una divergencia del modelo con nombre y
  apellido, que es lo que `doc/todo.md` recoge.
* **Las velocidades de paso** son el dato a vigilar. A 20 mm/s:

  | Avance del husillo | Pasos con 1/8 | Pasos con 1/16 |
  | :--- | ---: | ---: |
  | 2 mm | 16 kHz | 32 kHz |
  | 8 mm | 4 kHz | 8 kHz |

  El GRBL original en un AVR se queda en unos 30 kHz; grblHAL en un F4 va
  bastante más allá. El coste para `mcu-sim` está en el §10.

### 8.3 El anfitrión

**El G-code entra por el `PuenteSerie`.** Con `--serie U=rfc2217:PUERTO` o `tcp:PUERTO`, Candle, UGS o CNCjs se conectan como a una máquina real. En Windows hace falta un puerto virtual (com0com) o un emisor que hable TCP; CNCjs y bCNC lo hacen.

**Para las pruebas**, `memoria` con un fichero de G-code.

Así se puede usar **el mismo software que trae la VEVOR** contra la máquina simulada.

## 9. La visualización

**Lo que la ventana ya sabe hacer:**

* girar o poner a un ángulo un elemento del SVG (`giro`, `angulo`);
* encender o hundir elementos (`brillo`, `hundido`);
* pintar una imagen que manda la pieza (`pantalla`);
* desde ayer, escribir valores (`display`).

Todo ello lo pide una tabla de enlaces, y el refresco son 60 instantáneas por segundo de tiempo simulado, en tiempo real.

**V1: planta y alzado, en F2 y F3.**

* **El efecto `desplaza`**, nuevo y pequeño, es el `angulo` lineal: traslada
  un elemento del dibujo.
  * Como el `display`, admite **varios observables, cada uno con su vector
    en píxeles por unidad**:

    ```xml
    <enlace pieza="MAQ" elemento="cabezal" efecto="desplaza"
            observable="x z" vector="2.5 0  0 -2.5"/>
    ```

  * Así un elemento se mueve con dos ejes **sin que la ventana tenga que
    componer grupos anidados**, que es lo que haría falta para que el carro
    Z se moviera con el X.
* **El dibujo de la máquina tiene dos vistas**: la planta, donde se ven X e Y
  y la pieza, y un alzado frontal, donde se ven X y Z. Es lo que se ve en
  cualquier plano de taller, y no necesita perspectiva.
* **Las cotas** son un `display` con X, Y y Z en mm. Necesita **signo y una
  cifra más** que el de la fuente, y es un cambio pequeño.
* **Las alarmas** son observables con `alarma="si"`, que la ventana ya hace
  notar:
  * final de carrera pisado;
  * **pasos perdidos**: la diferencia entre los pasos pedidos y la posición
    real del rotor pasa de medio paso;
  * error de seguimiento de un controlador;
  * choque: la fresa por debajo de la mesa o un tope duro;
  * freno echado con el motor empujando;
  * sobretensión del bus.

**La pieza, en F4.** `Pieza` lleva un **mapa de alturas**: una rejilla de 0,5 o 1 mm sobre la mesa.

* **Al mecanizar.** Cada vez que la cadena avanza la fresa, la pieza
  «estampa» el barrido del cilindro, el cono o la esfera de la fresa entre
  la posición anterior y la de ahora, y se queda con el mínimo. Son unas
  decenas de celdas por paso: nada.
* **Se ve** con el mecanismo de la TFT, una imagen que la pieza manda
  **solo cuando cambia**. La profundidad va en color y la trayectoria en
  vacío, la fresa por encima de la superficie, en una línea tenue.
* **Lo que cuesta mandarla.** Una imagen de 300 × 200 son 180 kB en RGB888.
  A 60 Hz serían 10,8 MB/s por un socket local: se puede, pero conviene que
  la pieza la mande a 10 Hz o que `T_IMAGEN` admita **rectángulos sucios**.
  Esa ampliación del protocolo es pequeña y sirve también a la TFT.

**V2: isométrica, que es opcional.** El mismo `desplaza` con vectores isométricos (X a 30°, Y a 150°, Z vertical) mueve los carros en una vista en perspectiva dibujada en SVG. La pieza pide entonces una **transformación afín** de la imagen, que `QTransform` hace sin más.

**V3: 3D, en F8.** Qt Quick 3D, con mallas de los carros y un mapa de alturas como malla. Es otro orden de trabajo en la ventana, y **nada en `mcu-sim` cambia**: lee los mismos observables.

**Una gráfica temporal**, un osciloscopio de observables, sería muy útil para ver `ia`, `ib`, la velocidad y el error de seguimiento. No existe en la ventana y no depende de la CNC: va aparte.

## 10. El coste de simular

**Lo medido hoy**, en el banco de M10: el motor TT tras el L298N con la fuente, sin MCU, 10 s simulados.

| Caso | CPU por segundo simulado |
| :--- | ---: |
| Motor a tensión fija, en régimen | 0,014 s |
| Motor con PWM de 1 kHz e inductancia | **0,11 s** |

**Lo que cuesta es la integración continua**: RK4 a ≤ 5 µs mientras la corriente se mueve, unos 200 000 pasos por segundo, a algo menos de medio microsegundo cada uno.

**Lo estimado para una CNC moviéndose**, con todo a la vez. Son estimaciones que F1 tiene que medir:

| Elemento | Por qué cuesta | Estimación |
| :--- | :--- | ---: |
| 3 paso a paso con corriente impuesta | RK4 continuo mientras se mueven, como el TT con PWM | ~0,3 s/s |
| Flancos STEP a 16–32 kHz en tres ejes | Cada flanco cambia un nodo y despierta al driver | ~0,05–0,1 s/s |
| Firmware (interrupción de pasos a la frecuencia del eje dominante) | Del 10 al 30 % de CPU del F446. CoreMark al 100 % cuesta 0,089 s/s | ~0,03 s/s |
| Cabezal (`MotorCC` con PWM) | Como el TT con PWM | ~0,1 s/s |
| Pieza y mapa de alturas | Una estampación por cada avance de la cadena | despreciable |
| **Total** | | **~0,5 s/s** |

**Va más rápido que el tiempo real, con margen pero sin mucho.**

**Dos riesgos:**

* **Los motores despiertan muy a menudo.** En las plataformas donde los
  `SC_THREAD` son caros, el CI de Windows con pthreads fue cien veces más
  lento (I-23), conviene que los drivers sean `SC_METHOD`.
* **El chopper de verdad no cabe**: multiplicaría el total por diez (§5.2).
  Lo mismo pasa con el BLDC conmutado desde el MCU (F7). Ahí hay que medir
  antes de prometer el tiempo real.

**Los invariantes no se mueven.** Ninguna pieza nueva entra en las ocho suites. Las pruebas nuevas van en las de Python de la ventana (`gui-marcha` y una `gui-cnc`), como M9 y M10.

## 11. Plan por fases

**Los tamaños** se dan comparando con lo ya hecho:

* `motor_cc.h` tiene 520 líneas y `puente_h.h` 440.
* El sistema de los dos motores (M, una pieza grande y dos placas) fue una
  tarea grande.
* El efecto `display` (S) fue pequeño.

| Fase | Qué | Piezas y cambios | Cómo se comprueba | Tamaño |
| :--- | :--- | :--- | :--- | :--- |
| **F0** Cimientos | La cadena mecánica (§4, opción C): `<transmision>`, el integrador por cadena, y `MotorCC` pasado a ella con su carga de siempre como cadena implícita. El efecto **`desplaza`** con varios observables | `parts/cadena.h`, `motor_cc.h` reorganizado, `netlist_xml.h`, `ilustracion.cpp` | **M9 y M10 dan las mismas rpm, al decimal.** I27 para `desplaza`. Un volante de inercia conocida da la τ calculada | M |
| **F1a** Paso a paso | `MotorPaP` (dinámico e ideal). `DriverPaP` (A4988, DRV8825 y TMC2209 como juegos de parámetros) con la **rama de corriente impuesta**. Placas: NEMA 17 de la 3018, **ST5918L3008**, módulo Pololu | `parts/motor_pap.h`, `parts/driver_pap.h`, `RamaCorriente` en `ext_parts.h`, `placas/*.xml` y SVG | **La curva par–velocidad** del ST5918 a 24 y 48 V contra la de la ficha. La esquina en ~800 y ~1600 rpm. **Pasos perdidos** con una rampa demasiado corta. Un aviso por cada error de cableado y temporización | L |
| **F1b** Continua de potencia | El 775 (cabezal) con un **módulo MOSFET** y diodo. El **BTS7960** como `PuenteH` con parámetros. Un motorreductor de 37 mm con encoder | `MotorCC` con otras cifras, `Mosfet`, `L298` generalizado | El 775 a 24 V en ~10 000 rpm. Sin diodo, aviso. Un PID de posición en el firmware con el encoder | M |
| **F1c** BLDC con controlador | `MotorBLDC` (eje q, Hall y encoder) y `ControladorMotor` en reloj y dirección con lazo cerrado y salida de freno. Placas: **DB59L024035-A** y un controlador «tipo C5-E» | `parts/motor_bldc.h`, `parts/controlador_motor.h` | 0,47 N·m a 9,4 A. El error de seguimiento con carga. **No pierde pasos: avisa y se para** | L |
| **F2** Transmisión | `Reductora`, `Acople`, `Husillo` (Tr, ACME y bolas), `Carro`, `Freno`. Un eje completo en banco, con su dibujo y sus cotas | `parts/transmision.h`, placas y SVG de un eje | El rendimiento y el autobloqueo de la tabla del §6.3. La inercia reflejada. **El Z con bolas sin freno se cae; con Tr8×2, no.** La velocidad crítica avisa | M |
| **F3** La máquina | `placas/cnc_3020.xml`: tres ejes, finales de carrera, sonda, seta, cabezal y fuente de 24 V. La **CNC Shield V3** sobre la Nucleo-F446RE. El firmware propio y después **grblHAL**. El `PuenteSerie` para Candle y UGS. Dibujo de planta y alzado | Placas, SVG, `verif/fw/cnc`, `gui-cnc` | Un cuadrado y un círculo en G-code: la trayectoria contra la pedida. `$H` con los finales. `G38.2` con la sonda. La seta corta los drivers | L |
| **F4** La pieza | `Pieza` con mapa de alturas, la fresa y la imagen. **Opcional: fuerzas de corte** (Kienzle, `F = k_c·b·h`) que cargan los ejes y frenan el cabezal | `parts/pieza.h`, `T_IMAGEN` con rectángulos sucios | Un bolsillo de profundidad conocida sale con esa profundidad. Un grabado en V da el ancho que toca | M |
| **F5** Potencia | La versión de 48 V: NEMA 23 o BLDC con controlador, husillos de bolas 1605, **freno en Z**, reductora planetaria en el BLDC, **condensador de bus y resistencia de frenado**, optoacopladores | `Condensador`, `Fuente absorbe=`, `Optoacoplador` | Sin resistencia de frenado, el bus sube al frenar el Z y salta la protección | M |
| **F6** Bus de campo | El controlador por **CANopen**, un subconjunto de CiA 402 (estados, perfil de posición, *homing*, PDO), por el bxCAN que ya está. O Modbus RTU con un RS-485 | `ControladorMotor` con su diccionario de objetos | Un firmware que lleva un eje por CANopen. Las tramas en el `CanWire` | L |
| **F7** BLDC desde el MCU | Tres fases de verdad. **X-NUCLEO-IHM07M1** (L6230). Seis pasos con Hall por el TIM1 y el TIM2/3; después FOC | `MotorBLDC` por fases, `PuenteTrifasico` | Arranca y gira con seis pasos. La corriente por fase. **Medir el coste antes** | L |
| **F8** 3D | Qt Quick 3D en la ventana | Solo `mcu-sim-gui` | La misma máquina, vista en 3D | L |

**El orden es el que se pidió**: motores y drivers primero (F1), la transmisión después (F2) y la máquina y su visualización (F3).

**F0 va delante de todo porque es barata y evita rehacer F1**: si los motores nuevos se escriben con la carga dentro, como el TT, habrá que abrirlos otra vez en F2.

**F1a, F1b y F1c son independientes entre sí.** **F4 a F8 son ampliaciones**: con F0–F3 ya hay una 3020 simulada que se mueve con G-code y se ve.

## 12. Riesgos y preguntas abiertas

**Decisiones que tomar antes de empezar:**

1. **La arquitectura mecánica.** ¿Cadena rígida declarada (opción C del §4)?
   Es la recomendación. La red mecánica general (B) solo se justificaría si
   interesaran las resonancias.
2. **El firmware de F3.** ¿grblHAL como objetivo, o basta el propio? grblHAL
   es la mejor prueba, pero sus fallos serán del modelo y pueden llevar
   tiempo.
3. **La visualización.** ¿Planta y alzado (V1) bastan, o la isométrica (V2)
   es un requisito? El 3D (V3) es otro proyecto.
4. **Los drivers genéricos o solo Nanotec.** Los de la 3018 (A4988 y DRV8825)
   y el TMC2209 son los que un alumno tendrá en la mano. Los Nanotec, los de
   una máquina industrial. El plan lleva los dos.
5. **24 V o 48 V para la versión potente.** 48 V es lo que pide un NEMA 23 y
   lo que usa Nanotec, y trae el problema de la energía que vuelve (§6.6).

**Riesgos:**

* **Datos que no se publican.** De la VEVOR: el avance del husillo, los
  motores y los drivers. Del 775: la corriente de bloqueo. De los frenos:
  los tiempos. De los paso a paso de Nanotec: si el par de retención es con
  una fase o con dos. Cada uno será un parámetro con una estimación
  documentada, como la `l_mh` del TT.
* **La rama de corriente impuesta** es nueva en la red. Si no encaja con el
  resolvedor de `AnalogNet`, la alternativa es la 2 del §5.2 (el driver
  integra el motor), con la pérdida de honestidad eléctrica que eso supone.
* **El coste real** puede quedar por encima de la estimación. Se mide en F1a,
  con un banco de un eje, antes de construir la máquina entera.
* **grblHAL** puede tocar periféricos o modos que nadie ha usado todavía en
  `mcu-sim`: DMA en los temporizadores, *bit-banding*, ciertos modos de los
  TIM. Cada uno es un fallo que arreglar en el modelo, y bienvenido.
* **Las fuerzas de corte** (F4) dependen del material y de la fresa con una
  dispersión enorme. Valen para ver tendencias (avanzar más deprisa frena el
  cabezal y pierde pasos), no para predecir un mecanizado.

## 13. Lo decidido, y lo hecho

**Decidido el 2026-10-28**, al empezar la rama `cnc` de `mcu-sim` y de
`mcu-sim-gui`:

1. **La arquitectura mecánica**: la cadena rígida declarada, la opción C del §4.
2. **El modelo eléctrico de los drivers**: la **corriente impuesta con tope de
   tensión**, la tercera forma del §5.2.
3. **El orden**: F0, F1a, F1b y F1c, un parche por fase.

### F0, hecha: la cadena mecánica y el efecto `desplaza`

* **`parts/cadena.h`**. La `Cadena`: un grado de libertad, los motores
  (`Actuador`) y los elementos pasivos (`ElementoMecanico`) reflejados en el eje
  de los motores, y **un solo integrador** —Runge-Kutta de cuarto orden, a pasos
  de 5 µs como mucho— con las corrientes de todos los motores y la ecuación
  mecánica. El ángulo va con Runge-Kutta si algún motor da un par que depende de
  él —el paso a paso de F1a—; si no, con el trapecio de siempre.
* **`<transmision id cadena="M V"/>`** en el XML de una placa: una pieza
  `Transmision` cuyas referencias son las piezas de la cadena, en orden, motores
  primero. Sus errores tienen nombre: la pieza declarada después, la que no
  tiene eje, el motor detrás de un elemento, la pieza en dos transmisiones.
* **El `MotorCC`, actuador de su cadena.** Suelto, lleva la suya implícita, de
  él solo, con su carga de siempre. **M9 y M10 dan las mismas rpm al decimal**,
  y más que eso: un banco del motor tras el L298 —a tensión fija, con PWM de
  1 kHz, casi sin inductancia y sin diodos, con carga, bloqueado y suelto— da
  los mismos números **con sus 17 cifras** antes y después de la cadena.
* **El `Volante`**, el primer elemento: una inercia con sus rozamientos. Con uno
  igual que el TT, la constante de tiempo pasa de 53 ms a 106 —la cuenta da 52,5
  y 105— y llega a la misma velocidad (`make gui-cnc`, C1).
* **El efecto `desplaza`**: `observable="x z" vector="2.5 0  0 -2.5"` en la
  tabla de enlaces. `mcu-sim` lo valida y lo manda en `T_PLACA`; la ventana
  traslada el elemento con la suma de cada observable por su vector (I27 de
  `mcu-sim-gui`). Va con su halo y su contorno.
* **Pruebas**: `make gui-cnc` (C1, 16 comprobaciones), y en `mcu-sim-gui` la I27.
  Ninguna suite se mueve; las dos de `gui-sistema` que comprobaban el texto de la
  lista de efectos lo comprueban con `desplaza`.

### F1a, hecha: los paso a paso y sus drivers, con corriente impuesta

* **La rama de corriente impuesta con tope de tensión** (§5.2, la tercera
  forma): un canal aparte del nodo, `FaseCorriente` en `common/analog_net.h`,
  que el driver declara en los nodos de sus dos salidas, y `RamaCorriente` en
  `parts/ext_parts.h`, su lado. Encaja con el resolvedor de `AnalogNet` sin
  tocarlo: el riesgo del §12 no se da. La bobina deja en la rama la energía
  que ha entrado en ella, integrada —con un chopper, la potencia de cada
  instante salta de signo dentro de cada micropaso—, y el driver la pide de
  media a VMOT.
* **`MotorPaP`** (`parts/motor_pap.h`), dinámico e ideal, con sus pasos
  perdidos. Cada bobina va como esté cableada: con corriente impuesta, con
  Thevenin tras un L298, al aire o mal, y lo dice.
* **`DriverPaP`** (`parts/driver_pap.h`): **A4988, DRV8825 y TMC2209**, y
  además el **DM542**, el driver externo de 48 V con entradas optoacopladas:
  ninguno de los otros tres aguanta los 48 V de la curva del ST5918. Y un
  **`GeneradorPasos`** de banco, para mover un driver sin firmware.
* **Placas**: `nema17_3018.xml`, `st5918l3008.xml` y `pololu_a4988.xml`, con su
  dibujo (`placas/dibuja_cnc.py`).
* **Lo que dice la ficha de Nanotec y el §5.1 no**: el ST5918L3008 es de
  OCHO hilos, y sus 3 A, 1 Ω y 1,9 mH son de media bobina, en unipolar. En
  bipolar va a 2,1 A en serie (2 Ω, 7,6 mH) o a 4,2 A en paralelo (0,5 Ω,
  1,9 mH), con el mismo par de retención; la curva de su ficha se da así, a
  24 y a 48 V. `MotorPaP` lo admite con `conexion=`, y la placa va en
  paralelo. Con eso, `Km` es 0,31 N·m/A, y no 0,44.
* **La curva par–velocidad** del ST5918 en paralelo tras el DM542, con la
  carga subiendo hasta que pierde pasos: 1,23 N·m abajo con 24 y 48 V, y la
  **esquina** —el 70 %— en unas **640 rpm a 24 V y 1240 a 48 V**. El §5.1
  estimaba unas 800 y 1600 con `V/(L·I)`, que deja fuera la f.e.m.: el orden y
  la proporción son los de la cuenta. La curva de la ficha no se puede leer de
  su página, así que no se contrasta punto a punto.
* **Pasos perdidos** con una rampa de 5 ms hasta 1000 rpm, y ninguno con
  400 ms. **Un aviso por cada error** de cableado y de temporización: RESET al
  aire, 3,3 V con VDD a 5 V, un pulso corto, DIR tarde, una bobina al aire o
  entre dos fases, VMOT bajo, la bobina al revés —gira al revés—, EN alto —el
  traductor cuenta y el motor no se mueve— y la protección térmica.
* **El coste, medido** (§10, el riesgo): el primer intento costaba 0,6 s de
  CPU por segundo simulado por eje, porque la cadena ensayaba 1 ms por delante
  en cada flanco de STEP. Sin ensayo para el paso a paso —que despierta en cada
  flanco de todas formas— y con pasos de 10 µs, **0,09 s/s por eje** a 1/8 y
  3200 Hz, arranque incluido. Los tres ejes caben de sobra en la estimación
  de 0,3 s/s del §10.
* **Pruebas**: `make gui-cnc` C2, 41 comprobaciones (57 en total). M9 y M10 no
  se mueven, y el banco del `MotorCC` sigue dando los mismos números con sus
  17 cifras.

### F1b, hecha: la continua de potencia

* **`Mosfet`** (`parts/potencia.h`): un N-MOSFET de lado bajo con una `RDS`
  que baja con la puerta —el IRLZ44N de nivel lógico y el IRF520 de los
  módulos azules, que con 5 V apenas se abre, y se avisa— y el diodo de libre
  circulación del módulo. **Sin el diodo, aviso** en el primer hueco del PWM.
* **`MedioPuente`**: el BTS7960, en vez de un `PuenteH` con parámetros: un
  IBT-2 son DOS medios puentes, cada uno con su IN, su INH y su IS, y el
  modelo es el del chip. IS es una fuente de corriente, la que sale entre
  8500, y el 74HC244 del módulo avisa de los 3,3 V de un STM32 con el VCC a
  5 V.
* **Placas**: `motor_775.xml`, `modulo_mosfet.xml`, `ibt2.xml`,
  `motor_jgb37_encoder.xml` y el sistema `nucleo_f446re_ibt2_jgb37.xml`, con su
  dibujo. Las cifras de los dos motores que no da ningún vendedor —la
  corriente de bloqueo, la constante de tiempo, la inductancia— son
  estimaciones, y lo dicen.
* **El 775 a 24 V, a 9958 rpm** con 5 V o 3,3 V en la puerta del IRLZ44N.
* **Lo que el §5.3 no decía: con PWM y en vacío, el 775 no va a la mitad.**
  Con `L/R` = 0,25 ms la corriente se acaba en cada hueco de un PWM de 1 kHz,
  y al 50 % va a 9612 rpm. Con carga y 20 kHz, la corriente es continua y va
  al 40 % de la velocidad a tope: el PWM de un MOSFET de lado bajo regula el
  par, y GRBL lo usa en lazo abierto. Es lo que pasa en el taller.
* **El PID de posición** en el firmware (`verif/fw/cnc_pid`): una vuelta del
  eje de salida y media atrás, sin pasarse más de un 2 % y en 2 cuentas de la
  consigna. El motorreductor no arranca con menos del 16 % de PWM —su
  rozamiento de arranque—: el PID lleva una zona muerta y un mando mínimo.
* **La fuente con límite y un motor que arranca**: el 775 parado pide 40 A, y
  el límite de la `Fuente` de 10 A oscilaba sin fin en el mismo instante —la
  bobina sujeta su corriente y lo de fuera no es un Thevenin—. Ahora una
  `Fuente` no deja su nodo por debajo de 0 V, con la resistencia que deja
  pasar la corriente de la bobina: converge a la primera. El banco del
  `MotorCC` sigue dando sus 17 cifras.
* **Pruebas**: `make gui-cnc` C3, 23 comprobaciones (80 en total).

## Fuentes

* **VEVOR 3020.** La página que se dio no se pudo abrir por su longitud. El
  mismo producto (id 010249298884):
  [vevor.nl, versión en inglés](https://vevor.nl/hout-graveermachine-c_11142/vevor-cnc-freesmachine-graveermachine-120w-3-assige-grbl-besturing-houtgravure-p_010249298884?lang=en).
* **VEVOR 3018**, su hermana:
  [3-axis CNC router 3018 with offline controller, T8 screw](https://www.vevor.com/wood-engraving-machine-c_11142/3-axis-cnc-router-3018-with-offline-controller-engraving-tools-usb-port-t8-screw-p_010663412383).
* **Placa GRBL de SainSmart** para la 3018 (DRV8825, 12–24 V):
  [Controller board, Genmitsu 3018-PRO](https://www.sainsmart.com/products/controller-board-genmitsu-3018-pro).
* **Nanotec, motores BLDC**:
  [la familia](https://www.nanotec.com/eu/en/products/156-brushless-dc-motors-from-manufacturer),
  [los de rotor interior (DB22–DB87)](https://www.nanotec.com/eu/en/products/2265-brushless-dc-motors-rpm-w-watt) y
  [la ficha del DB59L024035-A](https://www.nanotec.com/eu/en/products/2870-db59l024035-a).
* **Nanotec, motores paso a paso**:
  [la familia](https://www.nanotec.com/eu/en/products/153-stepper-motors-from-manufacturer),
  [los NEMA de 14 a 110 mm (ST4118, ST5918, ST6018, ST8918)](https://www.nanotec.com/eu/en/products/2263-nema-stepper-motors-14-110-mm-flange) y
  [la ficha del ST5918L3008-A](https://www.nanotec.com/eu/en/products/533-st5918l3008-a).
* **Nanotec, controladores**:
  [la familia](https://www.nanotec.com/eu/en/products/10741-programmable-stepper-motor-controller-and-bldc),
  [el C5-E](https://www.nanotec.com/eu/en/products/1764-c5-e-controller-for-stepper-motors-bldc) y
  [los servodrives CLC](https://www.nanotec.com/eu/en/products/13784-clc-servo-drives).
* **Nanotec, frenos**:
  [la familia](https://www.nanotec.com/eu/en/products/161-stepper-motor-brake-brushless-dc-motor-brake) y
  [el BWA](https://www.nanotec.com/eu/en/products/1936-spring-loaded-brake).
* **Nanotec, reductoras**:
  [la familia](https://www.nanotec.com/eu/en/products/160-brushless-dc-motor-gearbox-stepper-motor-gearbox) y
  [las planetarias](https://www.nanotec.com/eu/en/products/2704-planetary-gearboxes).
* **Nanotec, husillos y tuercas**:
  [la familia](https://www.nanotec.com/eu/en/products/2273-screws-and-nuts-for-linear-actuators),
  [los husillos](https://www.nanotec.com/eu/en/products/290-leads-screws-for-stepper-motor) y
  [los actuadores lineales NEMA 17](https://www.nanotec.com/eu/en/products/2973-nema17-linear-actuator-42-mm).
* **grblHAL para STM32F4xx** (F401, F407, F411, F446; Nucleo-64):
  [github.com/grblHAL/STM32F4xx](https://www.github.com/grblHAL/STM32F4xx).
* **TMC2209**:
  [la hoja de datos, rev. 1.08](https://wcm-cce.cldnet.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.08.pdf).
* **Código y documentación de `mcu-sim`:**
  * `src/parts/motor_cc.h` y `src/parts/puente_h.h` (§4.16 de
    `doc/parts.md`);
  * `src/parts/part_base.h` (observables e imágenes);
  * `src/periph/timers.h` (OPM, Hall, BDTR);
  * `doc/analisis_gui.md` §3.1 (el coste);
  * `doc/protocolo.md` §4.1 de `mcu-sim-gui` (`T_IMAGEN`);
  * el banco de M10 en `src/verif/gui/marcha.py` (la medida del §10).
* **Lo calculado aquí y no publicado:**
  * los rendimientos de los husillos con μ típicos;
  * las inercias de los husillos y las reflejadas;
  * la velocidad crítica;
  * la velocidad de esquina del ST5918;
  * las frecuencias de paso.

  Las cuentas son las del §5.1 y el §6.3.
