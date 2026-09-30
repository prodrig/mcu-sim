# Puente entre una UART del MCU y un puerto serie del ordenador

**Análisis de opciones para `mcu-sim`** · 29-09-2026

La pregunta: qué opciones hay para construir un elemento **externo al MCU** que
conecte un canal UART del chip simulado con un puerto serie, virtual o físico,
del mismo ordenador donde corre `mcu-sim`, y qué cambia en cada una de las
plataformas que el proyecto soporta.

---

## 0. Resumen

* **La pieza tiene tres capas, y solo una depende del sistema operativo.** El
  lado del MCU (leer el TX y gobernar el RX en los pines) y el acoplamiento
  entre tiempo simulado y tiempo real son iguales en las cuatro plataformas. Lo
  que cambia es cómo se abre el «puerto» en el host.
* **En el lado del MCU, la pieza debe colgarse de los pines**, como el resto de
  `parts/`. `SwoReceiver` ya es un receptor UART a nivel de bit, y la mitad del
  trabajo está hecha.
* **En el lado del host no hay un mecanismo común a las cuatro plataformas.**
  Linux y macOS pueden crear un puerto virtual sin instalar nada (un
  pseudoterminal). Windows no: un COM virtual exige un driver en modo núcleo
  (com0com o uno comercial), con instalación como administrador y problemas
  con Secure Boot. Lo único que funciona igual en todas partes es **TCP**, que
  ya está resuelto en `common/red.h`.
* **Recomendación:** una pieza `PuenteSerie` con un motor UART compartido y
  varios *backends* intercambiables. Primero TCP (portátil, sin código de SO
  nuevo), después pseudoterminal en Linux y macOS, y por último abrir un puerto
  existente por su nombre (`COMn`, `/dev/ttyUSB0`, `/dev/cu.*`), que en Windows
  cubre a la vez el par virtual de com0com y un adaptador USB-serie real. El
  terminal integrado en `mcu-sim-gui` llegaría más tarde, por el protocolo que
  ya existe.
* **Arquitectura D con herramientas de terceros (§7).** SerialTool (Duolabs)
  no sirve: en su edición gratuita reenvía solo de serie a red, solo como
  cliente y con un límite de 5 paquetes por sesión. Las alternativas libres que
  sí sirven (socat y gensio en Linux y macOS; com0com + hub4com o HW VSP3
  Single en Windows) no evitan el driver en Windows. Por eso D complementa a A
  en vez de sustituirla: TCP en `mcu-sim` más recetas documentadas. Para ver lo
  que imprime el firmware, lo más sencillo es un terminal que hable TCP
  directamente, como CoolTerm en las tres plataformas.
* **Plan de implementación (§10).** Se adopta D con RFC 2217, en nueve fases
  (D0-D8). La pieza `PuenteSerie` va en los pines y comparte motor UART con
  `SwoReceiver`. Tiene tres backends sobre `red.h` (`memoria`, `tcp` crudo y
  `rfc2217`) y ninguna línea de código de puertos serie. Con `baudios="host"`,
  la velocidad la fija el terminal del alumno, como en el ST-LINK de una
  Nucleo. Las pruebas con simulación van en un banco propio, `make testserie`,
  para no mover el invariante del F407.

---

## 1. Qué hay en `mcu-sim` que condiciona el diseño

**Plataformas.** Los ejecutables publicados son cuatro: Linux x86-64, Windows
x86-64 (MinGW-w64, enlazado con `-static`), macOS arm64 y macOS x86-64. Para
este problema, los dos macOS son la misma plataforma: la API de terminales y de
puertos serie es la misma en los dos. **En la práctica hay tres casos: Linux,
Windows y macOS.**

**Reglas del repositorio que afectan a la solución:**

| Regla | Consecuencia para el puente |
| :--- | :--- |
| Los pines son eléctricos y lo externo vive en `parts/`, declarado en el XML | El puente es un `<componente>` más, con terminales con nombre, ficha de `--help` obligatoria (`REGISTRA_PARTE`) y validación eléctrica |
| Solo `common/red.h` sabe en qué sistema operativo corre | Un backend de puerto serie añade código de SO. Hay que decidir si va en un segundo fichero hermano o si se evita del todo (§6) |
| El tiempo simulado del F407 es un invariante al picosegundo | La pieza no puede aparecer en las placas de los bancos de pruebas, y sus pruebas deben usar un backend determinista |
| Los stubs de GDB sondean un socket no bloqueante **en tiempo simulado** (`gdb_rsp.h`, cada 100 µs) | Ya existe un patrón probado para mezclar E/S del host con SystemC sin hilos |
| `--tiempo-real` ata el avance simulado al reloj de pared | Es lo que hace que 115 200 baudios simulados sean 115 200 baudios para el terminal |
| «Lo que hace que no termine es que haya un puerto escuchando» | El puente debe contar como uno: con él, la simulación no se para sola |
| GUI en dos procesos, protocolo con longitud explícita | Se puede añadir un tipo de mensaje «bytes de UART» sin subir la versión |

**Piezas que ya existen y se aprovechan:**

* `SwoReceiver`: espera el reposo, detecta el flanco del bit de arranque,
  muestrea en el centro de cada bit y comprueba el de parada. Es el receptor del
  puente, generalizado a 7/8/9 bits, paridad y 1/2 bits de parada.
* `Driver` / `ExtPart::drive()`: gobernar un nodo con un Thevenin {V, R}. Es el
  transmisor.
* `SignalLink`: el modelo de «una pista de TX a RX», que es como se conecta el
  puente.

**El equivalente real.** En la NUCLEO-F446RE ya existe este elemento: el
ST-LINK/V2-1 lleva un puerto COM virtual unido a la USART2 (PA2/PA3). El puente
es ese chip, y la placa `nucleo_f446re.xml` es el primer sitio donde tendría
sentido montarlo por omisión.

---

## 2. Las tres capas

```
 MCU simulado              PuenteSerie                         Host
 ───────────   pines   ┌──────────────────────┐   canal SO   ────────────
 USART2 TX ──── PA2 ──▶│ receptor UART (bits) │──┐           terminal,
                       │                      │  ├─ backend ─▶ PuTTY, Python,
 USART2 RX ◀─── PA3 ───│ transmisor UART      │◀─┘           otro programa,
                       │ cola + ritmo en baud │              adaptador real
                       └──────────────────────┘
       (1) lado MCU         (2) acoplamiento de tiempos       (3) lado host
```

1. **Lado MCU**: convertir forma de onda en bytes y bytes en forma de onda.
2. **Acoplamiento de tiempos**: cuándo se consulta al host, cómo se reparten los
   bytes que llegan en tiempo de pared a lo largo del tiempo simulado y qué pasa
   si uno de los dos lados va más rápido.
3. **Lado host**: qué objeto del sistema operativo representa «el puerto serie».

Las capas 1 y 2 son C++17 y SystemC, y valen para todas las plataformas. Solo
la 3 cambia según el sistema.

---

## 3. Lado MCU: dónde se engancha la pieza

| | M1 · En los pines | M2 · En las señales internas de la USART | M3 · En el registro DR |
| :--- | :--- | :--- | :--- |
| Qué es | Pieza de `parts/` sobre dos `AnalogNet` | Conexión a `tx_out` / `rx_in` de `UsartBase` | Puerta trasera que lee y escribe DR |
| Respeta el mux de AF, el GPIO y la tensión del pad | **Sí** | No: se salta el pad | No |
| Baudios equivocados dan basura, como en la placa | **Sí** | Sí | No: siempre «funciona» |
| Sirve para cualquier USART/UART y para el F446 sin tocar periféricos | **Sí**, se elige en el XML | Requiere un enganche por instancia | Ídem |
| Sirve para un UART hecho con bit-banging, o para el SWO | **Sí** | No | No |
| Coste de simulación | Un suceso por flanco (el mismo que ya tienen las USART) | Algo menor | Mínimo |
| Encaja con las reglas del proyecto | **Sí** | Es una excepción a «lo externo va por los pines» | Contradice «nunca más permisivo que el silicio» |

**M1 es la única que encaja con el proyecto**, y el coste extra es despreciable:
a 115 200 baudios son unos 10 000 flancos por segundo simulado, y la USART del
modelo ya los genera.

**Diseño de la pieza en M1:**

* **Terminales**: `rx` (se conecta al TX del MCU; solo lee, *pasivo*, como el
  `origen` de `SignalLink`) y `tx` (gobierna el RX del MCU). Opcionales: `cts`
  (lee el RTS del MCU) y `rts` (gobierna su CTS), porque el modelo de USART ya
  implementa el control de flujo por hardware.
* **Atributos**: `baudios`, `formato` (`8N1`, `8E1`, `7O2`…), `vdd`, `r_out`,
  `flujo` (`no` | `rtscts`), y los del backend (`host`, `ruta`/`puerto`).
* **Transmisor**: reposo en alto con 50 Ω, como `SignalLink`. Con
  `conectada="no"`, alta impedancia.
* **Receptor**: el de `SwoReceiver`, con la espera de reposo al arrancar (ya
  resuelve el falso bit de arranque del nodo sin gobernar). El error de trama y
  el de paridad se cuentan y se avisan una vez, no se tiran en silencio.
* **Break**: detectar una línea baja durante más de una trama (para enviarlo al
  host si el backend lo admite) y poder generarlo hacia el MCU, porque el modelo
  tiene detección de break LIN (`LBD`).
* **Refactorización recomendada**: sacar el muestreo de bits de `SwoReceiver` a
  un `MotorUart` compartido. Así la lógica de «centro del bit, parada mala» no
  se duplica, y la prueba del SWO sigue validando el receptor del puente.

**Autobaudios**, como opción (`baudios="auto"`): medir el pulso más corto de la
primera trama. Es cómodo, pero quita el aprendizaje de «si PuTTY y el firmware
no coinciden, sale basura». Mejor que no sea la opción por omisión.

---

## 4. Acoplamiento de tiempos: la parte difícil y común

El tiempo simulado no es tiempo de pared. Hay tres regímenes:

| Régimen | Cuándo | Consecuencia sin tratar |
| :--- | :--- | :--- |
| Simulación **más rápida** que la realidad | Sin `--tiempo-real` y con un firmware ligero (2000 ms simulados salen en 0,033 s) | El MCU vuelca un `printf` de 1 kB en microsegundos de pared; los bytes que teclea el usuario llegan «tarde» en tiempo simulado |
| **Atada** (`--tiempo-real`) | El modo de uso interactivo | 115 200 baudios son 115 200 de verdad |
| Simulación **más lenta** | Firmware pesado (CoreMark), o `--tiempo-real=0.5` | El host puede mandar más rápido de lo que el MCU simulado consume |

### 4.1 Cómo se consulta al host

**T1 · Sondeo en tiempo simulado** (el patrón de `gdb_rsp.h`). Un `SC_THREAD`
hace `wait(periodo)`, lee sin bloquear del canal y encola lo que haya.

* A favor: un solo hilo, sin sincronización, el mismo patrón que ya pasa las
  cuatro plataformas y ASan, y **mantiene viva la simulación** igual que el stub
  de GDB.
* En contra: una llamada al sistema por vuelta. Con el periodo igual a un
  tiempo de carácter (unos 87 µs a 115 200) y la simulación a 60× el tiempo
  real, son unas 700 000 llamadas por segundo de pared. Se puede asumir, y con
  `--tiempo-real` baja a unas 11 500. Conviene que el periodo sea adaptativo:
  corto mientras hay tráfico y largo (1 ms) en reposo.

**T2 · Hilo del sistema operativo + `async_request_update()`**. Un `std::thread`
se bloquea en `read()`/`ReadFile()`, deja los bytes en una cola protegida y
despierta al núcleo de SystemC con `sc_prim_channel::async_request_update()`,
que es seguro entre hilos. Para que la simulación no termine por falta de
sucesos mientras espera al host, SystemC 2.3.2 o posterior (el proyecto usa
2.3.4) ofrece `async_attach_suspending()`.

* A favor: no hay coste en reposo y la latencia es mínima.
* En contra: hilos de verdad en un modelo que hoy no los tiene; depende de que
  **la SystemC de cada plataforma se haya compilado con las actualizaciones
  asíncronas** (Homebrew, apt y la que se construye a mano en MSYS2: hay que
  comprobar las tres); introduce no determinismo en el orden de llegada; y
  ASan/TSan pasan a tener algo que decir.

**Recomendación: empezar con T1** (está probado y es barato de escribir) y
pasar a T2 solo si se mide que el sondeo cuesta.

### 4.2 Colas y control de flujo

* **Host → MCU**: los bytes se encolan y el transmisor los emite **al ritmo de
  los baudios simulados**. La cola debe tener límite (por ejemplo 4 kB). Si el
  MCU activa RTS y la pieza tiene `flujo="rtscts"`, se detiene la emisión, como
  en un adaptador real. Si se desborda, se descarta y se avisa una vez.
* **MCU → host**: escritura no bloqueante. Si nadie escucha (un pseudoterminal
  sin abrir, un socket sin cliente), **se descarta**: el simulador nunca debe
  bloquearse porque el terminal del alumno esté cerrado. Es la misma filosofía
  que el `SO_NOSIGPIPE` de `red.h`.

### 4.3 Integración con la línea de órdenes

* Con un puente activo, la simulación **no termina sola** (como con `--gdb`).
* Si no se ha pedido `--tiempo-real`, conviene **avisar** («el terminal verá el
  ritmo simulado, no el real») o activarlo implícitamente para las placas con
  puente. Es una decisión de producto; avisar es lo menos sorprendente.
* Sobrescribir el destino sin editar el XML: `--serie VCP=COM7` o
  `--serie VCP=tcp:3355`. En el aula, el mismo XML se usa en máquinas con
  puertos distintos.

### 4.4 Determinismo

El invariante de `make test407` no se toca mientras ninguna placa de los bancos
monte la pieza. Sus propias pruebas usan un backend **`memoria`** (una cola en
proceso con un guion de entrada y la salida capturada), que es determinista y no
toca el sistema operativo. Así se puede probar la pieza entera, incluidos el
control de flujo y el error de trama, contra `uart_demo` en las cuatro
plataformas.

---

## 5. Lado host: los mecanismos, plataforma por plataforma

### 5.1 Matriz

| Mecanismo | Linux | Windows | macOS (arm64 y x86-64) |
| :--- | :--- | :--- | :--- |
| **H1 · Pseudoterminal creado por `mcu-sim`** | ✅ `posix_openpt` → `/dev/pts/N` | ❌ No existe equivalente (ConPTY es para consolas, no un COM) | ✅ `posix_openpt` → `/dev/ttysNNN` |
| **H2 · Abrir un puerto que ya existe, por su nombre** | ✅ termios: `/dev/ttyUSB0`, `/dev/ttyACM0` | ✅ Win32: `\\.\COM7` | ✅ termios: `/dev/cu.usbserial-*` |
| **H3 · Par virtual con un driver de terceros** | Innecesario (existe `tty0tty`, módulo del núcleo) | com0com u opciones comerciales; **es la única forma de tener un COM virtual** | Sin opción mantenida (las kext están en desuso); tampoco hace falta |
| **H4 · TCP (crudo o RFC 2217)** | ✅ `red.h` | ✅ `red.h` | ✅ `red.h` |
| **H5 · Consola del propio `mcu-sim`** | ✅ | ✅ | ✅ (no es un puerto serie) |

### 5.2 H1 · Pseudoterminal (Linux y macOS)

Es lo más parecido a un «puerto serie virtual» sin instalar nada: `mcu-sim` se
queda con el lado maestro e imprime el esclavo (`[VCP] puerto serie en
/dev/pts/5`). `picocom`, `minicom`, `screen`, `pyserial` y cualquier programa
que acepte una ruta lo abren como un puerto normal.

Detalles que hay que resolver bien:

* **Modo crudo en el esclavo** (`cfmakeraw`). Por omisión el esclavo tiene eco
  y modo canónico: el MCU recibiría sus propios bytes de vuelta y el host solo
  vería líneas completas. Es el fallo típico.
* **`EIO` cuando no hay nadie al otro lado.** En Linux, leer el maestro sin
  ningún descriptor del esclavo abierto da `EIO`/`POLLHUP`. El truco habitual es
  que el propio simulador mantenga abierto un descriptor del esclavo. Hay que
  gestionarlo explícitamente también en macOS.
* **Nombre estable.** `/dev/pts/N` cambia en cada ejecución. Se puede crear un
  enlace simbólico donde diga el XML (`enlace="/tmp/mcu-sim-vcp"`); en `/dev`
  no, sin ser root.
* **Baudios en un pseudoterminal**: el terminal los puede fijar y no tienen
  efecto. En Linux, con el modo paquete (`TIOCPKT`, junto con `EXTPROC`) el maestro se entera de que
  el esclavo ha cambiado su termios, y la pieza podría leer los baudios que ha
  elegido el alumno y **reproducir la basura del desajuste**. Sería un buen
  detalle didáctico, pero es solo de Linux. En macOS no hay que contar con ello.
* **macOS**: `/dev/ttysNNN` funciona con herramientas de línea de órdenes y
  `pyserial`, pero **muchas aplicaciones gráficas de terminal serie solo listan
  los `/dev/cu.*` que publica IOKit**, y un pseudoterminal no aparece en su
  desplegable. Hay que probar las que se recomiendan en clase.
* **WSL**: un `mcu-sim` de Linux dentro de WSL crea el pseudoterminal dentro de
  WSL, y los programas de Windows no lo ven. Hay que decirlo en la guía.
* Sin bibliotecas nuevas: `posix_openpt`/`grantpt`/`unlockpt`/`ptsname` están en
  la libc de los dos sistemas (`openpty` necesitaría `-lutil` en Linux).

### 5.3 H2 · Abrir un puerto que ya existe

Sirve para dos cosas distintas:

1. **El otro extremo de un par virtual** (com0com en Windows, `socat` en
   Linux/macOS). El alumno abre el otro extremo con su terminal.
2. **Un puerto físico**: un adaptador USB-serie real. El MCU simulado habla con
   un aparato de verdad (un módulo GPS, otra placa). Es *hardware-in-the-loop*,
   con la condición de `--tiempo-real` y la advertencia de que, si la
   simulación se queda atrás, el aparato no espera.

Por plataforma:

* **Windows**: `CreateFile("\\\\.\\COM12", …)` (el prefijo es obligatorio a
  partir de COM10), `SetCommState` con un `DCB` (baudios, formato,
  RTS/CTS), y `SetCommTimeouts` con `ReadIntervalTimeout = MAXDWORD` y los
  demás a 0, para que `ReadFile` vuelva enseguida con lo que haya: encaja con el
  sondeo T1 sin E/S solapada. Todo está en `kernel32`, así que el `-static`
  del Makefile no cambia y no hace falta ninguna DLL.
* **Linux**: `open(O_RDWR | O_NOCTTY | O_NONBLOCK)` y termios. El usuario tiene
  que estar en el grupo `dialout`, lo que merece una línea en la guía.
* **macOS**: igual que Linux, pero **con `/dev/cu.*` y no con `/dev/tty.*`**:
  el segundo se bloquea en `open` esperando a la señal DCD.

Con un puerto real, **los baudios del host sí importan**: la pieza debe
configurar el puerto con los mismos que su atributo y avisar si el driver no
los admite.

### 5.4 H3 · Par de puertos virtuales con driver (el problema de Windows)

En Windows, para que un programa como PuTTY, Tera Term o el monitor serie de
un IDE vea un `COMn` que no es físico, hace falta **un driver en modo núcleo**.
Las opciones:

* **com0com** (libre): crea pares `CNCA0`/`CNCB0` renombrables a `COMx`.
  Distribuye una versión firmada para x64 (2.2.2.0). Hay informes de fallos de
  instalación con **Secure Boot** activo (códigos 52 y 577) con versiones
  posteriores y en algunos equipos. Requiere administrador.
* **Comerciales** (Virtual Serial Port Driver de Electronic Team, HHD Virtual
  Serial Port Tools y otros): firmados y con instalador cuidado, pero de pago o
  con licencia restringida.

Para el público del proyecto, un alumno en su portátil y a menudo con un
antivirus agresivo (el `README` de la GUI ya cuenta lo de ESET), esto es
exactamente el tipo de **incidencia de soporte** que `analisis_gui.md` §18.2
señala como el recurso escaso. **Por eso no conviene que Windows dependa de H3
por omisión**: debe ser una opción para quien ya lo tiene.

### 5.5 H4 · TCP

Reutiliza `common/red.h` tal cual y funciona igual en las cuatro plataformas.
La pieza escucha en `localhost:puerto`, con la misma política de solo bucle
local que los stubs de GDB. Dos formas de hablar:

* **Crudo**: los bytes del socket son los de la línea. Clientes: PuTTY (modo
  *Raw*), `nc`, Python (`pyserial` acepta `socket://localhost:3355` como si
  fuera un puerto) y cualquier programa propio.
* **RFC 2217** (Telnet con opciones de puerto COM): además de los datos,
  transporta baudios, formato, líneas de módem y break. `pyserial` lo admite
  (`rfc2217://`). Es más código (negociación Telnet y escape del byte `0xFF`),
  pero es lo que permite que un puente convierta TCP en un COM de Windows sin
  perder la configuración.

Y desde TCP se llega a un puerto serie con herramientas externas: `socat
pty,link=/tmp/vcp,raw tcp:localhost:3355` en Linux y macOS, y **hub4com /
com2tcp** (del proyecto com0com) o los redirectores comerciales en Windows.

### 5.6 H5 · La consola de `mcu-sim`

Mandar el TX del MCU a la salida estándar y el teclado a su RX. Es trivial y
útil para un `printf` rápido, pero no es un puerto serie, choca con los avisos
del modelo que ya salen por esa consola y en Windows exige tratar el modo de
consola. Vale como backend de depuración, no como solución.

---

## 6. Dónde vive el lado host: cuatro arquitecturas

| | A · Todo dentro de `mcu-sim` | B · `mcu-sim` solo habla TCP | C · En `mcu-sim-gui` | D · Un ayudante aparte |
| :--- | :--- | :--- | :--- | :--- |
| Qué es | Backends TCP + pseudoterminal + puerto por nombre en un `common/serie.h` hermano de `red.h` | La pieza expone un socket; el puerto serie lo pone `socat`/com0com | La pieza manda los bytes por el protocolo de la GUI; la GUI tiene un terminal y abre puertos con **Qt Serial Port** | Un ejecutable `mcu-sim-serie` que habla TCP con el modelo y el SO con el puerto |
| Código de SO nuevo en el modelo | Un fichero más (termios + Win32) | **Ninguno** | **Ninguno** | Ninguno |
| Qué instala el alumno | Nada (Linux/macOS); en Windows nada con TCP, driver solo si quiere un COM | `socat` (Homebrew/apt) o com0com + hub4com | Nada más que la GUI | Otro ejecutable que distribuir |
| Funciona sin GUI (CubeIDE + terminal) | **Sí** | Sí | No | Sí |
| Portabilidad del acceso a puertos | Hay que escribirla y probarla en tres SO | La resuelven otros | **La resuelve Qt** (COM, `/dev/tty*`, `/dev/cu.*`) | Hay que escribirla |
| Depende del avance de la GUI | No | No | **Sí**: la GUI está en la fase 0 | No |
| Pseudoterminales | Sí | Con `socat` | Habría que escribirlos igual (Qt no crea ptys) | Sí |

**Lectura:**

* **B** es lo más barato y lo más fiel a la regla de «solo `red.h` sabe el
  SO», pero traslada el coste al alumno: `socat` es fácil en Linux y macOS, y
  com0com+hub4com en Windows es precisamente lo que se quiere evitar.
* **A** rompe la regla solo en la forma y no en el fondo: si el código de SO
  sigue concentrado en `common/` (`red.h` y `serie.h`), con la misma
  disciplina de «un solo sitio y probado sin SystemC» (`make serie`, como
  `make red`), la propiedad que importa, que el modelo compile en cualquier
  sitio con C++17 y SystemC, se conserva. Añade `termios` y la API de COM de
  Win32, que no añaden bibliotecas.
* **C** es el destino natural del **terminal** («ver lo que imprime el
  firmware»), que es lo que la mayoría de los alumnos necesita, y Qt Serial
  Port les resuelve H2 en las tres plataformas. Pero no sirve para el caso
  «CubeIDE + terminal serie, sin GUI» y depende de fases que aún no existen.
  El protocolo lo admite sin cambiar de versión.
* **D** solo tiene sentido si se quiere mantener `mcu-sim` sin ningún código
  de terminal, y cuesta un segundo ejecutable que firmar y distribuir, que es
  el problema que ya tiene la GUI. **Salvo que ese ejecutable no sea nuestro**:
  la variante de D con herramientas de terceros está en el §7.

---

## 7. La arquitectura D con herramientas de terceros

La variante de D que no obliga a escribir un ayudante propio: `mcu-sim` solo
expone TCP (lo mismo que B, y la fase S1 del plan), y el papel de ayudante lo
hace un programa que ya existe, multiplataforma y **gratuito para uso
docente**. La pregunta de partida es si una herramienta como SerialTool sirve
para eso. Aquí se contesta, y se revisan las alternativas.

### 7.1 Qué tiene que hacer el ayudante, y qué no puede evitar

El ayudante tiene que hacer dos cosas a la vez:

1. hablar TCP con `mcu-sim`, normalmente como **cliente**, porque la pieza
   escucha;
2. presentar al programa del alumno algo que se comporte como un puerto serie.

La segunda depende más de la plataforma que de la herramienta:

* **Linux y macOS**: la herramienta puede crear ella misma un pseudoterminal
  (socat, gensio). No hace falta ningún driver.
* **Windows**: un `COMn` que no es físico **siempre** necesita un driver. O la
  herramienta *es* ese driver (un «redirector de COM», que asocia un COM
  virtual a una conexión TCP), o se apoya en un par virtual (com0com) y une uno
  de sus extremos con TCP.

**Consecuencia: D no elimina el driver en Windows.** Lo saca de `mcu-sim` y lo
deja en manos de una herramienta ajena. Lo que sí consigue es que `mcu-sim`
siga sin código de puertos serie.

Las herramientas se agrupan en tres familias:

| Familia | Qué hace | Ejemplos |
| :--- | :--- | :--- |
| **D-a · Puente TCP ↔ puerto existente** | Une un socket con un puerto que ya existe (el extremo de un par com0com, un pty, un adaptador real) | socat, ser2net, hub4com, pyserial, ser2tcp, Node-RED, CoolTerm |
| **D-b · Puerto virtual ↔ TCP, en una sola pieza** | Crea el puerto y lo conecta a TCP | socat y gensio con pty (POSIX); com0com + com2tcp, HW VSP3, HHD Free Com Port Redirector (Windows); ttynvt (Linux) |
| **D-c · Sin puerto: el terminal habla TCP directamente** | No es un puente, pero cubre el caso «ver el `printf`» | CoolTerm, PuTTY, Tera Term, YAT, miniterm de pyserial, Serial Studio, ScriptCommunicator |

### 7.2 SerialTool: tres programas con el mismo nombre

Al buscar «SerialTool» aparecen tres proyectos distintos. Conviene no
confundirlos:

* **SerialTool de Duolabs** (serialtool.com), el más conocido. Windows 7 o
  posterior, macOS desde Mojave (Intel y Apple Silicon) y Linux en AppImage.
  Código cerrado y modelo *freemium*. Según su FAQ, la edición gratuita se
  puede usar sin restricción de uso y en varias máquinas. Su función de red,
  *Serial packet to network*, tiene tres problemas para este caso:
  * **solo va de serie a red**;
  * **solo hace de cliente** TCP o UDP (HTTP/HTTPS solo en PRO);
  * en la edición gratuita **está limitada a 5 paquetes por sesión**.

  Además, no crea puertos virtuales: la captura de COM que ofrece en Windows es
  un *sniffer* de la edición PRO, no un puerto. **Veredicto: no sirve como
  ayudante de D.** La edición gratuita no es bidireccional ni tiene un
  rendimiento utilizable. La PRO (desde 29 €/año) quita el límite de paquetes,
  pero no está documentado que reciba de la red, y además no cumple el
  criterio de gratuidad. Como terminal serie sobre un puerto que ya existe es
  correcta, pero en ese papel hay alternativas libres (§7.3).
* **SerialTool de heropml** (GitHub, GPL-3.0). Windows, macOS y Linux x86-64.
  Serie, RTT de J-Link, TCP y UDP como cliente y como servidor, un bucle
  virtual y una pasarela Modbus TCP↔RTU. Sirve como **terminal TCP directo
  (D-c)**. No documenta un puente genérico serie↔TCP.
* **SerialTool de HoGC** (GPL-3.0, multiplataforma). Asistente serie y de red
  con gráficas y XModem. También es un **terminal D-c**, no un puente.

### 7.3 Catálogo de alternativas gratuitas para uso docente

Criterio de inclusión: sin coste para una asignatura, ni para el profesor ni
para el alumno. Datos de licencia y plataformas consultados el 29-09-2026. Las
licencias cambian, así que conviene revisarlas antes de cada curso.

| Herramienta | Familia | Linux | Windows | macOS | Licencia | ¿Gratis en docencia? | Notas |
| :--- | :--- | :---: | :---: | :---: | :--- | :--- | :--- |
| **socat** | D-a, D-b | ✅ | ⚠ | ✅ Homebrew | GPL-2 | Sí | `pty,link=…,raw,echo=0` ↔ `tcp:localhost:3355` es la pieza H1 fuera del simulador. En Windows solo hay compilaciones no oficiales sobre Cygwin, que no crean un COM |
| **gensio** (`gensiot`) y **ser2net 4** | D-a, D-b | ✅ | ⚠ compilar con MSYS2 | ✅ Homebrew | Biblioteca LGPL-2.1, herramientas GPL-2 | Sí | La más completa en POSIX: pty con `link=`, TCP, Telnet con RFC 2217 y `serialdev` (en Windows, `//./COMn`). Sin binarios oficiales para Windows (el paquete `gensio-binary` de PyPI solo cubre Linux y macOS) |
| **com0com + hub4com** (`com2tcp`, `com2tcp-rfc2217`) | D-b | — | ✅ | — | GPL | Sí | Driver x64 firmado (2.2.2.0), con los problemas de Secure Boot del §5.4. `com2tcp-rfc2217 \\.\CNCB0 localhost 3355` y el alumno abre el otro extremo del par |
| **HW VSP3 Single** (HW group) | D-b | — | ⚠ | — | Freeware | Sí: la única condición es para empresas (citar a HW group en su web) | COM virtual como cliente o servidor TCP, RFC 2217 y emulación de baudios. Un solo puerto en la versión gratuita. La ficha del fabricante solo cita hasta Windows 8: **hay que probarlo en Windows 10/11 x64** |
| **HHD Free Com Port Redirector** | D-b | — | ✅ 7 a 11, x64 y ARM64 | — | Propietaria, gratuita con límites | **Dudoso** | Cliente/servidor TCP, crudo y RFC 2217, sin componentes en modo núcleo según el fabricante. La edición gratuita **prohíbe el uso comercial, gubernamental o militar**: hay que confirmar con HHD si una universidad pública entra en «gubernamental». Tampoco guarda los dispositivos ni permite nombres propios |
| **pyserial**: `tcp_serial_redirect.py`, `rfc2217_server.py` | D-a | ✅ | ✅ | ✅ | BSD-3 | Sí | `tcp_serial_redirect.py -c localhost:3355 COM20 115200` actúa como cliente. Necesita Python y un puerto que ya exista (el extremo de com0com, un pty). Se podría empaquetar con PyInstaller como «ayudante oficial» |
| **ser2tcp** | D-a | ✅ | ✅ | ✅ | MIT | Sí | Python. Solo hace de servidor: exigiría que `mcu-sim` fuera el cliente (§7.5) |
| **Node-RED** + `node-red-node-serialport` | D-a | ✅ | ✅ | ✅ | Apache-2.0 | Sí | Un flujo TCP ↔ serie. Funciona, pero instala Node.js y un servidor web: demasiado para esto |
| **CoolTerm** (Roger Meier) | D-a y D-c | ✅ | ✅ | ✅ | Freeware | Sí | Conexión serie y conexión TCP (cliente o servidor), y **reenvío de datos entre ventanas**: una ventana TCP contra `mcu-sim` y otra contra el COM, reenviándose entre sí, hacen de puente. Hay que comprobar que no reenvía los ecos. Sobre todo, **es el mejor terminal D-c multiplataforma** |
| **ScriptCommunicator** | D-a (con guion) y D-c | ✅ | ✅ | ✅ | LGPL | Sí | Serie, TCP cliente/servidor, UDP, SPI, I²C y CAN. Sus guiones en JavaScript pueden abrir interfaces propias y unirlas: un puente escrito en veinte líneas |
| **Serial Studio** | D-c | ✅ | ✅ | ✅ | GPL-3 (compilado desde el código) / Pro de pago | Sí, la edición GPL | Panel de telemetría con fuente de red TCP/UDP y consola bidireccional. La edición libre hay que compilarla. Más interesante como referencia para `mcu-sim-gui` que como puente |
| **YAT** | D-c | (Mono) | ✅ | — | LGPL-2 | Sí | Terminal serie y TCP/UDP como cliente o servidor. Solo Windows en la práctica |
| **PuTTY**, **Tera Term** | D-c | PuTTY ✅ | ✅ | PuTTY (Homebrew/MacPorts) | MIT / BSD | Sí | Modo *Raw* contra `localhost:3355` |
| **tio** | D-a parcial | ✅ | — | ✅ | GPL-2 | Sí | `--socket inet:PUERTO` comparte un tty por TCP, pero haciendo de servidor. Con el pty del backend H1 no aporta nada |
| **ttynvt** | D-b | ✅ | — | — | Libre | Sí | Crea un `/dev/tty…` que es cliente RFC 2217. Es el equivalente en Linux de HW VSP |

**Descartadas** por el criterio o por no estar verificadas:

* **VSPE** (Eterlogic): gratuita solo en Windows de 32 bits. En x64 es de pago.
* **Lantronix CPR** y **Tibbo VSP**: redirectores gratuitos pensados para el
  hardware de cada fabricante. No está documentado que funcionen con un
  servidor TCP cualquiera. Quedan fuera hasta probarlos.
* **Comerciales** (Virtual Serial Port Driver y Serial to Ethernet Connector de
  Electronic Team, FabulaTech, Serial/IP, SerialTool PRO, Docklight): de pago.

### 7.4 Montajes concretos por plataforma

En todos los casos `mcu-sim` se lanza igual. La pieza escucha en
`localhost:3355`:

```
./build/mcu-sim placa.xml fw.bin --tiempo-real
```

> **Actualizado en D7:** las recetas para el alumno, comprobadas y al día,
> están en `doc/puente_serie.md`. Lo que sigue es la versión del análisis. Dos
> cambios de fondo: socat lleva `wait-slave` y se conecta a `127.0.0.1`, y
> `mcu-sim` se lanza con `--espera-terminal` (D-15).

**Linux**

```
socat -d -d pty,link=$HOME/vcp,raw,echo=0 tcp:localhost:3355
picocom -b 115200 ~/vcp            # o minicom, screen, pyserial…
```

**macOS** (los dos): lo mismo, después de `brew install socat`. Se aplica la
advertencia del §5.2: el pty que crea socat tampoco aparece en las aplicaciones
gráficas que solo listan `/dev/cu.*`.

**Windows**, de menos a más instalación:

1. **Sin puerto (D-c)**: CoolTerm, PuTTY o Tera Term en modo TCP contra
   `localhost:3355`. No hay driver ni ayudante. Para una práctica de «imprimir
   por la UART y teclear órdenes», basta.
2. **COM virtual con HW VSP3 Single**: se crea `COM20` como cliente TCP de
   `127.0.0.1:3355`. Es un solo programa, pero su compatibilidad con Windows
   10/11 está sin verificar.
3. **com0com + hub4com**: se crea el par, se renombra un extremo a `COM20` y el
   otro se une a TCP con `com2tcp-rfc2217 \\.\CNCB0 localhost 3355` (o en
   crudo con `hub4com`). Es libre y el más probado, pero arrastra el driver y
   Secure Boot.
4. **HHD Free Com Port Redirector**: lo mismo que el 2, pero con la duda de
   licencia del §7.3.

### 7.5 Qué exige a `mcu-sim`

* **Servidor TCP que acepte reconexiones.** Los redirectores (HW VSP, HHD,
  com2tcp, socat) se conectan y reconectan cuando el alumno abre y cierra el
  puerto. La pieza no debe terminar ni bloquearse entre una conexión y otra.
  Ya está en el diseño del §4.2 (sin cliente, se descarta).
* **Modo cliente opcional** (`host="tcp-cliente:HOST:PUERTO"`), para las
  herramientas que solo hacen de servidor (ser2tcp, tio, `rfc2217_server.py`).
  Cuesta poco: `red::conecta_local()` ya existe.
* **RFC 2217 sube de prioridad.** HW VSP3, HHD, `com2tcp-rfc2217`, ttynvt y
  gensio **transmiten los baudios que el alumno eligió en su terminal**. Si la
  pieza entiende RFC 2217, puede reproducir la basura de un desajuste de
  baudios **en las tres plataformas**, y no solo con el modo paquete del pty de
  Linux (§5.2). Si se opta por D, conviene adelantarlo de S4 a justo después
  de S1.
* **Las recetas son parte del producto.** Con D, lo que en A es código pasa a
  ser documentación por plataforma, y la documentación caduca con cada versión
  de un tercero. Necesita su propio documento, como `doc/ejecutables.md`, y una
  fila en `doc/todo.md` para lo que no se pueda probar en CI. En los
  *runners* solo se puede automatizar socat en Linux y en macOS.

### 7.6 Valoración: D con terceros frente a A

| | A · Backends propios | D · Herramientas de terceros |
| :--- | :--- | :--- |
| Linux y macOS | pty propio, unas 150 líneas en `common/serie.h` | `socat` en una línea, pero hay que instalarlo y es otro proceso que arrancar |
| Windows | Sin driver no hay COM; con com0com, `puerto:COM20` | Sin driver no hay COM; con driver, un proceso más (com2tcp) o un redirector (HW VSP3, HHD) |
| Código de SO en `mcu-sim` | Un fichero más | Ninguno |
| Coste de soporte | En el proyecto (código probado) | En las recetas (versiones y licencias ajenas) |
| Baudios del terminal visibles para el modelo | pty en Linux; RFC 2217 si se implementa | Solo si se implementa RFC 2217 |

**Conclusión.** SerialTool, en su edición gratuita, no vale para la
arquitectura D. Las herramientas que sí valen son libres: socat o gensio en
Linux y macOS, y com0com + hub4com o HW VSP3 Single en Windows. Pero **ninguna
evita el driver en Windows**, que era el único problema que A no resolvía.
Por eso D no desplaza a A. Queda como una estrategia complementaria: `mcu-sim`
con TCP (S1) y, si se hace, RFC 2217, más un documento de recetas para quien
necesite un COM en Windows. Para la mayoría de los alumnos, el camino
recomendado sigue siendo **un terminal que hable TCP directamente**, con
CoolTerm como opción común a las tres plataformas.

---

## 8. Recomendación

**Arquitectura A, con TCP como backend común y C como destino del terminal.**
La variante D con herramientas de terceros (§7) no la sustituye: se añade como
un documento de recetas para quien necesite un COM en Windows. Si se adopta,
RFC 2217 pasa de S4 a justo después de S1.

> **Actualización del 29-09-2026: se adopta D con RFC 2217.** El plan de
> implementación está en el §10. Las fases S2 y S3 de la tabla siguiente
> (pty y puerto por nombre dentro de `mcu-sim`) quedan descartadas, y S4 se
> adelanta.

```xml
<!-- El COM virtual del ST-LINK de la NUCLEO-F446RE: USART2 en PA2/PA3 -->
<componente tipo="PuenteSerie" id="VCP" baudios="115200" formato="8N1"
            host="auto" enlace="/tmp/mcu-sim-vcp">
  <pin nombre="rx" nodo="PA2"/>   <!-- USART2_TX del MCU -->
  <pin nombre="tx" nodo="PA3"/>   <!-- USART2_RX del MCU -->
</componente>
```

`host` admite `auto` | `pty` | `tcp:PUERTO` | `rfc2217:PUERTO` | `puerto:NOMBRE`
| `consola` | `memoria`. Con `auto`:

| Plataforma | `host="auto"` es | Por qué |
| :--- | :--- | :--- |
| Linux | `pty` (+ enlace simbólico) | Puerto de verdad, sin instalar nada |
| macOS (los dos) | `pty` (+ enlace simbólico) | Ídem; se avisa de que algunas apps gráficas no lo listan |
| Windows | `tcp:3355` | Sin driver no hay COM virtual; PuTTY en modo *Raw* o `socket://` bastan. Quien tenga com0com o un adaptador usa `puerto:COM7` |

**Plan por fases**, cada una cerrada con pruebas y sin mover el invariante:

| Fase | Contenido | Prueba |
| :--- | :--- | :--- |
| **S0** | `MotorUart` extraído de `SwoReceiver`; pieza `PuenteSerie` con backend `memoria`; ficha de `--help` | Eco contra un firmware en bucle, errores de trama y paridad, RTS/CTS, break. Las del SWO siguen pasando igual |
| **S1** | Backend `tcp` (crudo) sobre `red.h`; sondeo T1; «no termina sola»; `--serie ID=…` | Cliente TCP del propio banco, como `verif/gdb_client.h`; las cuatro plataformas en CI |
| **S2** | Backend `pty` (Linux y macOS): modo crudo, `EIO`, enlace | Abrir el esclavo desde la prueba y hacer el recorrido completo; en CI de Linux y de los dos macOS |
| **S3** | Backend `puerto:` (termios y Win32) en `common/serie.h`, con su `make serie` sin SystemC | En CI: pty (POSIX) y com0com no son instalables en todos los *runners*, así que parte de la prueba en Windows será manual y **tiene que decirlo** |
| **S4** | Si hace falta: RFC 2217, lectura de baudios del pty (Linux) y T2 con `async_attach_suspending` | Medida de CPU antes y después, como la de `--tiempo-real` |
| **S5** | En `mcu-sim-gui`: mensaje «bytes de UART» en el protocolo, terminal integrado y, opcionalmente, puertos con Qt Serial Port | Con la fase correspondiente del plan de la GUI |

---

## 9. Riesgos y preguntas abiertas

1. **¿`--tiempo-real` implícito con un puente?** Sin él, lo que el alumno ve en
   el terminal no tiene el ritmo de la placa. Hay que decidir entre avisar o
   activarlo.
2. **Windows sin driver = sin COM.** Si el enunciado de la asignatura exige
   «abre el COM en Tera Term», no hay forma de evitar el driver. TCP obliga a
   cambiar las instrucciones del enunciado, no el simulador.
3. **Apps gráficas en macOS** que no listan los pseudoterminales: hay que
   probar las que se recomiendan en el curso antes de prometer nada.
4. **SystemC con actualizaciones asíncronas** en las tres instalaciones, solo
   si se llega a T2.
5. **Dos MCUs** (`dos_mcu.xml`): nada impide un puente por chip. Cada uno
   necesita su puerto o su enlace, y conviene que `auto` no los haga chocar
   (puerto TCP libre siguiente, enlace con el id del MCU).
6. **CI**: los *runners* no tienen puertos físicos ni com0com. S3 en Windows
   quedará parcialmente sin verificar automáticamente, y tiene que figurar en
   `doc/todo.md` con su identificador.
7. **Licencias de terceros** (§7.3). Pueden cambiar entre cursos. Dos casos
   están pendientes de confirmar: si la cláusula «gubernamental» de HHD alcanza
   a una universidad pública, y si HW VSP3 Single funciona en Windows 10/11
   x64. Hay que revisar el catálogo antes de recomendarlo en un enunciado.

---

## 10. Plan de implementación: arquitectura D con RFC 2217

> **Decisión del 29-09-2026.** Se implementa la arquitectura D del §7:
> `mcu-sim` expone el puente **solo por TCP**, en crudo y con **RFC 2217**, y
> el puerto serie del sistema lo ponen herramientas de terceros gratuitas
> (§7.3). Esta sección es el plan por fases. Sustituye a las fases S2 y S3 del
> §8 (pty y puerto por nombre dentro del simulador), que dejan de hacerse, y
> adelanta S4 (RFC 2217).

### 10.1 Alcance

**Entra:**

* la pieza `PuenteSerie`, en los pines (M1, §3), con el motor UART compartido
  con `SwoReceiver`;
* tres backends, todos sobre `common/red.h` y **sin código de SO nuevo**:
  `memoria` (pruebas), `tcp` (crudo) y `rfc2217` (Telnet con la opción 44);
* opcionalmente, el modo cliente (`tcp-cliente`, `rfc2217-cliente`) para las
  herramientas que solo hacen de servidor;
* la línea de órdenes (`--serie`), el «no termina sola» y el aviso de
  `--tiempo-real`;
* un banco de pruebas propio, interoperabilidad automática con pySerial y
  socat, y una matriz manual para Windows;
* las recetas por plataforma (`doc/puente_serie.md`) y sus entradas en
  `doc/todo.md`.

**No entra:**

* pseudoterminales, `termios` o la API de COM de Win32 dentro de `mcu-sim`. Esa
  es la razón de ser de D: `red.h` sigue siendo el único fichero que sabe en
  qué sistema operativo corre;
* el terminal de `mcu-sim-gui` (S5), que sigue su propio plan;
* el control de flujo XON/XOFF en la línea (se responde que no se admite, §10.4).

### 10.2 Decisiones de diseño

| # | Decisión | Elegido | Por qué |
| :--- | :--- | :--- | :--- |
| D-1 | ¿Quién escucha? | **`mcu-sim` es el servidor** (en RFC 2217, el *access server*) | Es el papel del puerto físico. Los redirectores (HW VSP3, HHD, `com2tcp-rfc2217`, ttynvt, `rfc2217://` de pySerial) son clientes. Es además la política de los stubs de GDB |
| D-2 | ¿Crudo y RFC 2217 en el mismo puerto? | **No: modo explícito** (`tcp:3355` o `rfc2217:3355`) | Detectar Telnet por el primer byte falla con un cliente crudo que empiece por `0xFF`. Y un servidor Telnet que negocia por su cuenta pinta basura en PuTTY en modo *Raw*. El servidor RFC 2217 es **pasivo**: no envía nada hasta que el cliente negocia |
| D-3 | ¿Quién manda en los baudios de la línea? | Atributo `baudios`: un número (fijo) o **`host`** (lo que pida el cliente por RFC 2217; si no pide nada, 115 200) | Con `host`, el puente hace lo que hace el ST-LINK de una Nucleo: el PC fija la velocidad del UART del adaptador. Si no coincide con la del firmware, **la USART del modelo ve basura y levanta FE/NF**, sin simular nada especial |
| D-4 | ¿Cuándo se aplica un cambio de formato? | **Entre tramas**, nunca a mitad de un carácter | Es lo que hace un UART real al reprogramar su divisor, y evita tramas híbridas que no existen en el silicio |
| D-5 | ¿Cuántos clientes a la vez? | **Uno**. Si llega otro, **sustituye al anterior** con un aviso | Los redirectores se reconectan solos tras un cierre brusco. Rechazar al nuevo dejaría al alumno sin puerto hasta que caduque la conexión muerta |
| D-6 | Sin cliente | La línea hacia el MCU queda en reposo y **lo que manda el MCU se descarta** | El simulador no se bloquea nunca por el terminal (§4.2) |
| D-7 | ¿Desde dónde se puede conectar? | **Solo `localhost`**, como los stubs de GDB; `escucha="red"` para abrirlo, con aviso | Un puerto serie de un MCU no es un depurador, pero tampoco está autenticado |
| D-8 | Modo BINARY de Telnet | El servidor **acepta BINARY en los dos sentidos**. Si el cliente no lo negocia, aplica la regla NVT de `CR NUL` | Sin BINARY, un `\r` suelto se transmite como `CR NUL`, y el MCU recibiría un byte 0 que el alumno no ha tecleado |
| D-9 | ¿Cómo se consulta al host? | **Sondeo en tiempo simulado (T1)**, adaptativo: cada tiempo de carácter con tráfico y cada 1 ms en reposo | Sin hilos, con el patrón de `gdb_rsp.h`. T2 queda como mejora medible (§4.1) |
| D-10 | ¿Dónde van las pruebas con simulación? | En un **ejecutable nuevo, `make testserie`, con su propio invariante** | Añadir pruebas que simulan a `test407` movería los `2336217899213 ps`. Es la misma razón por la que existen `test446` y `test417` |
| D-11 | Puerto por omisión | **3355**, con RFC 2217 (`host` ausente = `rfc2217:3355`) | Sigue la serie del proyecto (3333 GDB, 3344 GUI) y **esquiva el 5000**, que en macOS desde Monterey es del receptor de AirPlay. *(Decidido en D0; los ejemplos de este documento decían 5000 y se han cambiado.)* |
| D-13 | ¿Cuándo pide el servidor BINARY? | **Pasivo hasta que el cliente negocia algo; en ese momento pide BINARY en los dos sentidos, una vez** | Afina D-2 y D-8. La captura de pySerial enseña que su cliente no ofrece BINARY por su cuenta: lo acepta cuando se lo piden. Sin pedirlo, la sesión se quedaría en NVT y un NUL detrás de un CR se perdería. Pedirlo tras la primera negociación no rompe la pasividad: el cliente ya ha demostrado que habla Telnet. *(Decidido en D4.)* |
| D-12 | ¿Dónde van las pruebas puras (sin simulación)? | En **programas aparte sin SystemC** (`make serie`, `make rfc2217`), en el trabajo rápido del CI | No tocan las cifras de `verif/invariantes.txt`, ni siquiera el número de comprobaciones: T130 sí lo movió al vivir dentro de `test407`. *(Decidido en D0.)* |
| D-14 | ¿Qué gobierna `baudios="host"`? | **La configuración entera de la línea: baudios, formato y control de flujo.** Con unos baudios fijos, el XML manda en las tres y el terminal recibe los valores del XML (con un aviso, una vez). DTR, RTS y el break los mueve **siempre** el terminal | El cliente de pySerial manda `SET-CONTROL 1` (sin control de flujo) cada vez que abre el puerto: si el control de flujo fuera siempre del terminal, un `flujo="rtscts"` del XML duraría hasta que se conectase alguien. DTR, RTS y break no son configuración, son señales. *(Decidido en D5.)* |
| D-15 | ¿Cómo se ve lo que el firmware imprime al arrancar? | **`--espera-terminal`**: con el tiempo simulado en cero, `mcu-sim` no da corriente al MCU hasta que cada puente por red tiene su terminal **y ese terminal lleva 300 ms sin mandar nada** (con RFC 2217, además, con la negociación terminada si la empezó; como mucho 2 s) | Sin ello el saludo sale cuando no hay nadie y se descarta (D-6). No basta con «conectado»: pySerial, al abrir, configura el puerto y **purga lo recibido** al final, y lo que el MCU mandase en medio se perdería igual; medido. Es lo que en la placa se hace abriendo el terminal y pulsando RESET. Con socat hace falta `wait-slave`, o socat se conecta antes de que nadie abra el pty. *(Decidido en D7.)* |
| D-16 | ¿Llevan los paquetes con qué probar el puerto serie? | **Sí: `ejemplos/`** con `nucleo_f446re_vcp.xml`, `vcp_rfc2217.xml` y `vcp_demo.bin` (unos 4 KB) | Sin una placa con la pieza y un firmware que imprima, la receta no se puede seguir con el paquete solo, y el alumno no tiene cómo distinguir «mi firmware no imprime» de «mi montaje está mal». Es la única excepción a «los paquetes no traen firmwares de ejemplo» (`doc/ejecutables.md` §7). *(Decidido en D7; confirmada al cerrar P-14, 30-09-2026.)* |
| D-17 | En modo cliente, ¿quién configura la línea? | **El puente**: `rfc2217-cliente` hace el papel del terminal, ofrece COM-PORT y manda al servidor SET-BAUDRATE, DATASIZE, PARITY, STOPSIZE y CONTROL con lo que tiene la línea simulada (las consultas de `LineaSerie`). Con `baudios="host"` manda 115 200. DTR y RTS no los toca | Al otro lado de un servidor RFC 2217 hay un puerto serie —de verdad o un pty— que tiene que ir a la velocidad del MCU simulado, y no hay terminal que la elija. No mover DTR evita reiniciar la placa que haya al otro lado de un adaptador (un Arduino se resetea con DTR). *(Decidido en D8.)* |

### 10.3 Componentes

| Fichero | Qué es | Depende de | Probado con |
| :--- | :--- | :--- | :--- |
| `parts/motor_uart.h` | `MotorUart`: emisor y receptor a nivel de bit sobre un `AnalogNet`, con 5 a 9 bits, paridad N/O/E/M/S, 1, 1,5 o 2 bits de parada, detección de break y contadores de FE/PE | `ExtPart`, SystemC | `testserie` y, a través de `SwoReceiver`, `test407` |
| `parts/ext_parts.h` | `SwoReceiver` pasa a usar `MotorUart` | `motor_uart.h` | `test407`, **con el invariante intacto** |
| `common/telnet2217.h` | **Códec puro** de Telnet + RFC 2217: una máquina de estados que parte el flujo entrante en datos y órdenes, y un codificador que escapa `IAC` y compone subnegociaciones. Ni sockets ni SystemC | C++17 | `make rfc2217`, sin SystemC, como `make red` |
| `parts/canal_host.h` | Interfaz `CanalHost` (`sondear()`, `leer()`, `escribir()`, `conectado()`, `describir()`) y sus backends `CanalMemoria`, `CanalTcp` y `CanalRfc2217` (TCP + códec) | `red.h`, `telnet2217.h` | `testserie` |
| `parts/puente_serie.h` | La pieza: terminales, colas, ritmo en baudios, líneas de módem, y el enlace entre RFC 2217 y el `MotorUart` | todo lo anterior | `testserie` |
| `parts/netlist_parts.h` | `REGISTRA_PARTE(PuenteSerie, Ayuda(…), …)`, con ficha completa | — | T126 (la factoría no admite piezas sin documentar) |
| `top/sim_main.cpp` | `--serie ID=DESTINO`, «no termina sola», aviso de `--tiempo-real` | `parse_serie()` puro, como `gui_destino.h` | Prueba sin tiempo simulado, como T130 |
| `verif/cliente_2217.h` | Cliente RFC 2217 del propio banco, hermano de `verif/gdb_client.h` | `red.h`, `telnet2217.h` | `testserie` |
| `verif/fw/vcp_demo/` | Firmware de eco con informe: devuelve cada byte, cuenta FE/NF/PE y lo publica en RAM | CMSIS | `.bin` versionado y su huella en `huellas.txt` |
| `verif/serie/interop.py` | Pruebas de interoperabilidad con pySerial (`rfc2217://` y `socket://`) | Python 3, pySerial | CI de las cuatro plataformas |

La pieza en el XML:

```xml
<componente tipo="PuenteSerie" id="VCP" host="rfc2217:3355"
            baudios="host" formato="8N1" flujo="no">
  <pin nombre="rx"  nodo="PA2"/>    <!-- USART2_TX del MCU -->
  <pin nombre="tx"  nodo="PA3"/>    <!-- USART2_RX del MCU -->
  <!-- opcionales -->
  <pin nombre="cts" nodo="PA1"/>    <!-- lee el RTS del MCU -->
  <pin nombre="rts" nodo="PA0"/>    <!-- gobierna el CTS del MCU -->
  <pin nombre="dtr" nodo="…"/>      <!-- línea DTR del host, si se quiere cablear -->
</componente>
```

### 10.4 Qué hace el servidor con cada orden de RFC 2217

La RFC exige que cada orden del cliente se confirme con el valor que **de
verdad** ha quedado aplicado (código + 100). Esa respuesta es lo que permite
decir «no» sin romper el protocolo: se contesta con el valor vigente.

| Orden (cliente → servidor) | Efecto en el modelo | Respuesta |
| :--- | :--- | :--- |
| `SIGNATURE` (0) | Vacía, pide la del servidor; con texto, es la del cliente: se guarda y no se contesta | `100` con el texto `mcu-sim PuenteSerie <id de la pieza>` (el proyecto no tiene número de versión) |
| `SET-BAUDRATE` (1) | Con `baudios="host"`, cambia la velocidad de la línea entre tramas (50 a 10 500 000). Con un número fijo, se ignora y se avisa una vez | `101` con la velocidad vigente; 0 es consulta |
| `SET-DATASIZE` (2) | 5 a 8 bits, aplicados a la línea (D-14) | `102` con el valor vigente (8 si el XML dice 9) |
| `SET-PARITY` (3) | NONE, ODD, EVEN, MARK, SPACE | `103` |
| `SET-STOPSIZE` (4) | 1, 2 y 1,5 | `104` |
| `SET-CONTROL` 0-3, 17, 19 | Control de flujo **de salida** (del puente al MCU): `HARDWARE` (3) activa RTS/CTS si hay terminal `cts` y `baudios="host"` (D-14); `XON/XOFF` (2), DCD (17) y DSR (19) no se admiten | `105` con el modo vigente, 1 o 3 |
| `SET-CONTROL` 13-16, 18 | Control de flujo **de entrada**: el puente no para nunca al MCU por su cuenta; el RTS lo mueve el terminal | `105` con 14 (ninguno) |
| `SET-CONTROL` 4-6 (BREAK) | Con BREAK ON, el terminal `tx` se mantiene en bajo hasta el OFF. **Es lo que dispara la detección de break LIN (`LBD`) en la USART del modelo** | `105` con el estado |
| `SET-CONTROL` 7-9 (DTR) | Gobierna el terminal `dtr` si está cableado; si no, solo se recuerda | `105` |
| `SET-CONTROL` 10-12 (RTS) | Gobierna el terminal `rts` (el CTS del MCU) si está cableado | `105` |
| `NOTIFY-LINESTATE` / `NOTIFY-MODEMSTATE` (6/7) | Los manda el servidor (106/107) al cambiar algo, **solo con los bits que dejen pasar las máscaras**. Si los manda el cliente, es una pregunta: LINESTATE 0 (los errores son sucesos, no estado) y el MODEMSTATE actual | `106` / `107` |
| `SET-LINESTATE-MASK` (10) | Máscara inicial 0, como dice la RFC | `110` |
| `SET-MODEMSTATE-MASK` (11) | Máscara inicial 255 | `111` |
| `FLOWCONTROL-SUSPEND` / `RESUME` (8/9) | Deja de enviar al cliente (datos y órdenes): los datos del MCU se retienen, hasta 64 KiB, y las respuestas esperan en la cola de salida | **Ninguna**: la RFC no las confirma, y el servidor de pySerial tampoco *(corregido en D5; el plan decía 108/109)* |
| `PURGE-DATA` (12) | 1: el búfer de **recepción** del servidor, lo que ha llegado del MCU y no ha salido; 2: el de **transmisión**, lo que espera para ir al MCU; 3: los dos *(corregido en D5: el plan lo decía al revés)* | `112` |

Lo que el servidor **notifica** sin que se lo pidan:

* **LINESTATE**: errores de trama (bit 3) y de paridad (bit 2) que detecta el
  receptor del puente en el TX del MCU, y break (bit 4) cuando el MCU manda un
  break (`SBK`). ~~Y desbordamiento (bit 1) si se llena la cola hacia el host~~:
  **no se ha hecho** en D5 (lo que no cabe se cuenta en `descartados()`);
* **MODEMSTATE**: CTS (bit 4 y su delta, bit 0) sigue al RTS del MCU si el
  terminal `cts` está cableado. DSR y DCD se dan fijos en activo, como un
  adaptador USB-serie sin esas líneas. Las notificaciones se mandan **al
  cambiar**, no por sondeo periódico.

### 10.5 Fases

Cada fase se cierra como las de siempre: pruebas en verde, **`test407`,
`test446` y `test417` con el mismo invariante**, entradas en `doc/todo.md` y
una nota en `doc/chat.md`.

| Fase | Contenido | Criterio de cierre |
| :--- | :--- | :--- |
| **D0 · Frontera** ✅ | Identificador en `doc/todo.md` (**P-14**). Parseo puro de `host=` y `--serie` con sus formas válidas y rechazadas (`tcp:0`, `tcp:65536`, un id inexistente, dos piezas en el mismo puerto). ~~Ficha de `--help PuenteSerie`~~ **se pasa a D2** (§10.8) | **HECHO el 29-09-2026** en la rama `puente-uart`: `make serie` 72/72, invariantes intactos (§10.8) |
| **D1 · Motor UART** ✅ | `MotorUart` extraído de `SwoReceiver` y ampliado (5-9 bits, paridad, 1/1,5/2 bits de parada, break, contadores). `SwoReceiver` migra a él | **HECHO el 29-09-2026**: `test407` da las mismas 2118 y el mismo `resto` (2240553274213 ps; el `2336217899213` de esta tabla era la cifra antigua del total) (§10.8) |
| **D2 · Pieza con backend `memoria`** ✅ | `PuenteSerie` en los pines con cola y ritmo en baudios; firmware `vcp_demo`; ~~nuevo~~ `make testserie` (nació en D1) | **HECHO el 29-09-2026** (§10.8). Los formatos 7E1 y 8N2 se prueban en el motor (D1), no con el firmware, que va en 8N1 |
| **D3 · TCP crudo** ✅ | `CanalTcp` sobre `red.h`: servidor en `localhost`, sondeo adaptativo, reconexión con sustitución, «no termina sola», `--serie`, aviso de `--tiempo-real` | **HECHO el 29-09-2026** (§10.8). La medida de CPU se hizo contra `--gdb` en la misma máquina, no contra el 5,3 % de otra |
| **D4 · Códec RFC 2217** ✅ | `common/telnet2217.h`: negociación (`WILL`/`DO` 44, BINARY, SGA), subnegociaciones, escape de `IAC` en datos y dentro de `SB`, regla `CR NUL` sin BINARY. `make rfc2217` sin SystemC | **HECHO el 29-09-2026** (§10.8). La captura de `com2tcp-rfc2217` necesita Windows y com0com: pasa a la matriz manual de D6 |
| **D5 · RFC 2217 en la pieza** ✅ | `CanalRfc2217`; `baudios="host"`; la tabla del §10.4 completa; notificaciones LINESTATE/MODEMSTATE con máscaras | `verif/cliente_2217.h` cambia la velocidad a mitad de sesión y se comprueba el cambio **entre tramas** (D-4); desajuste de baudios provocado **desde el host**; BREAK ON/OFF → `LBD`; PURGE; SUSPEND/RESUME; la firma. **HECHO el 30-09-2026** (§10.8) |
| **D6 · Interoperabilidad** ✅ | `verif/serie/interop.py`: pySerial con `rfc2217://` y `socket://` en CI (Linux, Windows y los dos macOS). socat (`pty` ↔ `tcp`) en CI de Linux y macOS | CI verde en las cuatro plataformas. La matriz manual del §10.6 hecha una vez y anotada con versión y fecha. **Lo automático, escrito y en verde en Linux el 30-09-2026** (§10.8), a falta de verlo en el CI de las cuatro; la matriz manual, a medias: necesita Windows y un escritorio. **Aceptada así al cerrar P-14** (30-09-2026, §10.8) |
| **D7 · Recetas y placas** ✅ | `doc/puente_serie.md` con los montajes del §7.4 actualizados, un apartado en `doc/ejecutables.md`, y la placa `placas/nucleo_f446re_vcp.xml` (**aparte**, para no tocar `test446`) | Un alumno sin experiencia sigue la receta de su plataforma y ve el `printf` de `vcp_demo`. **HECHO el 30-09-2026** (§10.8), con `--espera-terminal` (D-15) y `ejemplos/` en los paquetes (D-16); comprobado en Linux, y automáticamente con pySerial y socat en el CI |
| **D8 · Modo cliente** (opcional) ✅ | `tcp-cliente:HOST:PUERTO` y `rfc2217-cliente:…`, con reintento, para ser2tcp, tio y `rfc2217_server.py` | Eco contra `rfc2217_server.py` de pySerial sobre un pty de socat, en CI de Linux. **HECHO el 30-09-2026** (§10.8) contra su `PortManager` —el script tal cual se cae sobre un pty—, en el CI de Linux y macOS |

Dependencias: D1 → D2 → D3 → D5, y D4 en paralelo con D2-D3. D6 necesita D5.
D7 y D8 solo necesitan D5.

### 10.6 Matriz de interoperabilidad

Lo automático corre en CI; lo manual necesita un driver o una interfaz gráfica
y se hace a mano antes de cada versión publicada, anotando en `doc/todo.md` la
versión de la herramienta y la fecha.

| Cliente | Modo | Plataforma | Cómo se prueba | Qué se comprueba | Estado |
| :--- | :--- | :--- | :--- | :--- | :--- |
| pySerial `rfc2217://` | RFC 2217 | las cuatro | **CI** (`make interop`, I1-I7) | Negociación, eco binario, baudios y paridad **con efecto en la USART**, líneas de módem, RTS/DTR, break, purge, control de flujo, reapertura y sustitución, línea fija en el XML | Linux 30-09-2026 (pySerial 3.5); resto, en el CI de D6 |
| pySerial `socket://` | crudo | las cuatro | **CI** (I8) | Eco, los 255 valores | ídem |
| socat `pty` ↔ `tcp` | crudo | Linux, macOS | **CI** (I9) | Eco y los 255 valores a través de un pty abierto como puerto serie | Linux 30-09-2026 (socat 1.8.0.0); macOS, en el CI |
| socat `pty` ↔ `tcp` + `picocom` | crudo | Linux, macOS | manual | Receta del §7.4 tal cual | **Linux 30-09-2026** (socat 1.8.0.0, picocom 3.1): eco de `Hola picocom`. macOS, pendiente (**M3**) |
| `com2tcp-rfc2217` + com0com + Tera Term | RFC 2217 | Windows | manual | Cambiar la velocidad en Tera Term → basura en el MCU; volver a la buena → texto limpio | pendiente (**M6**) |
| HW VSP3 Single + PuTTY | RFC 2217 y crudo | Windows 10 y 11 x64 | manual | Instalación sin modo de prueba; reconexión al reiniciar `mcu-sim` | pendiente (**M5**) |
| HHD Free Com Port Redirector | RFC 2217 | Windows 11 | manual (si se aclara la licencia) | Ídem | pendiente, licencia antes (**M7**) |
| ttynvt | RFC 2217 | Linux | manual | `/dev/ttyNVT0` con `stty` cambiando la velocidad | pendiente, necesita CUSE (**M4**) |
| CoolTerm (TCP) | crudo | las tres | manual | El camino recomendado para la mayoría (§7.6) | **Windows 10 Pro 22H2 64 bits, 30-09-2026: M1 completa, pasos 1 a 7 (el 7, opcional, en modo RFC 2217, incluido), todos bien** (CoolTerm Win Intel64 v2.4.0, 05/19/2025): eco sin duplicar, texto largo entero, reconexión, reinicio de `mcu-sim` con CoolTerm conectado. macOS y Linux, pendientes (**M1**) |
| PuTTY *Raw* | crudo | Windows (Linux opcional) | manual | Ídem | pendiente (**M2**) |

### 10.7 Riesgos específicos

1. **Clientes que no siguen la RFC al pie de la letra.** Unos no negocian
   BINARY, otros mandan órdenes antes del `DO`, otros esperan una respuesta a
   `SIGNATURE`. Los vectores de D4 deben salir de **capturas reales** de cada
   cliente de la matriz, no solo del texto de la RFC.
2. **Baudios del host sin `--tiempo-real`.** El cliente cree que el puerto va a
   9600 y la simulación va a 60×. Los datos no se pierden (hay cola), pero el
   ritmo no es el de la placa. Se mantiene el aviso del §4.3.
3. **La cola hacia el MCU con el cliente enviando más rápido de lo que la
   línea simulada consume.** Con RFC 2217 hay mecanismo: `FLOWCONTROL-SUSPEND`
   **hacia el cliente** cuando la cola pasa del 75 % y `RESUME` por debajo del
   25 %. Hay que comprobar en la matriz qué clientes lo respetan.
4. **Windows sigue necesitando driver para tener un COM** (§7.1). El plan no lo
   resuelve: lo documenta y lo prueba.
5. **Presupuesto de pruebas manuales.** Si la matriz del §10.6 no se repite en
   cada versión, las recetas envejecen en silencio. Por eso cada fila anota su
   fecha.


### 10.8 Registro de ejecución

#### D0 · Frontera — 29-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/common/serie_destino.h` | Parseo puro de un destino (`memoria`, `tcp:PUERTO`, `rfc2217:PUERTO`), de `--serie ID=DESTINO`, y `resuelve()`: aplica la línea de órdenes sobre la placa y rechaza los choques. Sin red, sin SystemC, sin sistema de ficheros |
| `src/verif/prueba_serie.cpp` | 72 comprobaciones en seis grupos: formas buenas, límites del puerto, dieciséis formas malas con el texto que debe llevar su error, ida y vuelta, `--serie` y `resuelve()` |
| `src/Makefile.mcu-sim` | Objetivo `make serie`, sin SystemC |
| `src/top/sim_main.cpp` | `--serie ID=DESTINO` (y `--serie=`), repetible. La sintaxis se comprueba al leer el argumento (salida 1, como `--gui`); el resto, en un paso 5 bis tras validar la declaración (salida 2, como cualquier error de placa). Los puertos de los GDB y el de la GUI local cuentan como ocupados. Una línea en `--help` |
| `.github/workflows/suites.yml` | **PENDIENTE, a mano**: añadir `serie` al trabajo rápido, junto a `red`, `hash` y `cryp`. Las herramientas remotas con las que se hizo D0 no pueden escribir en `.github/workflows/`, así que el cambio no va en este commit. Mientras no se aplique, `make serie` no lo ejecuta el CI |
| `doc/todo.md` | **P-14**, con la tabla de fases, y el recuento |

**Qué dice el programa en D0.** Como ninguna placa puede tener todavía un
`PuenteSerie` (la pieza llega en D2), cualquier `--serie` bien escrito se
rechaza con `la placa no tiene ningun PuenteSerie`, y un `tipo="PuenteSerie"`
en el XML se rechaza como tipo desconocido. Es lo que tiene que pasar: el
modelo no puede ser más permisivo que lo que existe. Lo que ya funciona de
verdad son los mensajes:

```
$ mcu-sim placa.xml --serie VCP=COM7
--serie: VCP: 'COM7' es un puerto serie del sistema, y mcu-sim no abre ninguno:
lo pone una herramienta externa conectada por TCP (doc/analisis_puente_serie.md, 7).
Aqui va tcp:PUERTO o rfc2217:PUERTO
```

**Cómo se ha comprobado:**

* `make serie`: **72/72**. Y para que no sea decoración, tres mutaciones del
  parseo (aceptar el puerto 0, no mirar los puertos ocupados, no reconocer `pty`
  como puerto del sistema) hacen fallar 1, 2 y 1 comprobaciones respectivamente;
* `test407`, `test446` y `test417`, antes y después: **2118 / 204 / 165**, con
  `resto` `2240553274213 ps`, `1033367277932 ps` y `718988288 ps`. Idénticos,
  como tenían que salir: ninguno de los tres incluye nada de lo tocado;
* las seis placas de `placas/` siguen validando con `--valida` sin un aviso;
* compilado con g++ 13 y SystemC 2.3.4 (Ubuntu 24.04) **sin un solo aviso** con
  `-Wall -Wextra`. **No compilado aún en Windows ni en macOS**: lo hará el CI al
  empujar la rama (`mcu-sim` sí; `make serie`, solo cuando se añada al trabajo
  rápido).

**Dos desviaciones del plan, con su motivo:**

1. **La ficha de `--help PuenteSerie` pasa a D2.** Registrar el tipo en la
   factoría sin que exista la pieza haría que `tipo="PuenteSerie"` pasara la
   validación de la declaración y fallara después, o que se construyera algo que
   no hace nada. Eso es exactamente «más permisivo que lo que hay». La ficha
   viaja con el registro (`REGISTRA_PARTE`), así que irá con la pieza.
2. **Las pruebas no están en `test407` sino en `make serie`** (decisión D-12).
   El plan decía «como T130», pero T130 movió el recuento del banco al vivir
   dentro. Así, D0 no toca ni una cifra de `verif/invariantes.txt`.

**Lo que no se ha hecho y era del plan:** la nota en `doc/chat.md`. Es el diario
de trabajo sin editar, y no es sitio para que yo escriba entradas en tu nombre.


#### D1 · Motor UART — 29-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/common/formato_uart.h` | La parte PURA: `FormatoUart` (bits sin contar la paridad, paridad N/E/O/M/S, parada 1, 1.5 o 2), lo que ocupa una trama en medios bits, el bit de paridad de un dato, y `parsea_formato("8N1")` con sus errores. Probado en `make serie`, que pasa de 72 a **100** comprobaciones |
| `src/parts/motor_uart.h` | `ReceptorUart` (lee un nodo, no conduce) y `EmisorUart` (lo conduce con un Thevenin, 50 Ω por omisión). No son módulos: son ayudantes que usa el hilo de una pieza. Contadores de tramas, errores de trama, de paridad y breaks |
| `src/parts/ext_parts.h` | `SwoReceiver` usa `ReceptorUart` en 8N1. Pierde veinte líneas y ningún comportamiento |
| `src/top/sc_main_serie.cpp` | **`make testserie`**, el cuarto banco, con su invariante: **43** comprobaciones y **`51783680816 ps`** |
| `src/Makefile.mcu-sim` | `testserie`, `testserie-build` y `asanserie` |
| `src/verif/invariantes.txt` | La línea de `testserie` |

**El criterio de la fase, cumplido.** El receptor hace en 8N1 exactamente las
mismas esperas que hacía `SwoReceiver`, con las mismas expresiones de tiempo
(la conversión de segundos a picosegundos redondea, y escribir la cuenta de
otra forma podría redondear distinto). Resultado:

* `test407`: **2118** comprobaciones y `resto` **`2240553274213 ps`**, total
  `2337219149213 ps`. **Igual al picosegundo.** Además, la salida entera de la
  suite, línea a línea, es la misma que antes del cambio; solo cambian las dos
  líneas que miden la velocidad del anfitrión;
* `test446` y `test417`: 204 / `1033367277932 ps` y 165 / `718988288 ps`, que
  tampoco podían moverse (no montan un `SwoReceiver`, pero se compilan con él).

**Qué prueba `testserie` en esta fase** (el motor suelto, sin chip):

* S0 — la línea sin gobierno al arrancar no da tramas fantasma, que es lo que
  ya resolvía `SwoReceiver`;
* S1 — en 8N1, **los tiempos exactos**: el emisor tarda diez bits y el receptor
  entrega la trama 9,5 bits después del flanco de arranque, en el centro de la
  parada. Si eso cambiara, cambiaría el SWO del F407;
* S2 — ida y vuelta en once formatos (7E1, 7O1, 8E1, 8O2, 8M1, 8S1, 9N1, 5N1,
  6E2, 8N1.5 y 8N1);
* S3 — la duración de 1, 1,5 y 2 bits de parada, y que un emisor 8N2 se entiende
  con un receptor 8N1;
* S4 — los errores provocados: paridad al revés (error de paridad y solo de
  paridad), **baudios distintos** (9600 contra 115 200: sale basura y errores de
  trama, que es lo que el puente tiene que reproducir), break (se ve, se cuenta
  y el receptor se recupera) y que un `0x00` no es un break.

**Cómo se ha comprobado que las pruebas pueden fallar:** cuatro mutaciones del
motor (muestrear a 1,4 bits en vez de 1,5; no marcar nunca el break; la parada
siempre de un bit; no mirar la paridad) hacen fallar 1, 2, 2 y 2
comprobaciones. `make asanserie`: limpio.

**Desviaciones del plan:**

1. **`testserie` nace en D1 y no en D2.** El motor ampliado tiene paridad,
   break y formatos que `SwoReceiver` no ejercita, y dejarlos sin probar hasta
   el commit siguiente habría sido cerrar la fase con código sin probar.
2. **El formato va en un fichero puro** (`common/formato_uart.h`), que no
   estaba en la tabla de componentes del §10.3. Así el parseo de `formato=` se
   prueba sin SystemC, como el de `host=`.
3. **El muestreo es simple, no triple.** Es el de `SwoReceiver` y no puede
   cambiar sin mover el invariante. Un terminal no informa de ruido, así que al
   puente no le falta.

**PENDIENTE A MANO, como en D0:** que el CI ejecute `testserie` y contraste su
invariante. Hace falta tocar `.github/workflows/suites.yml`, y las herramientas
remotas no pueden.


#### D2 · La pieza — 29-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/parts/puente_serie.h` | `PuenteSerie`: terminales `rx`, `tx`, `cts`, `rts` y `dtr` (con los nombres del adaptador), un hilo por sentido, control de flujo mirando el CTS antes de cada trama, break hacia el MCU, `muestra` línea a línea y `guion`. Las tramas con error se entregan (es la basura que hay que ver) y los breaks se cuentan |
| `src/parts/canal_host.h` | La interfaz `CanalHost` (nadie bloquea; aviso por evento o sondeo) y su primer backend, `CanalMemoria`, con cola de 4 KiB hacia el MCU. TCP y RFC 2217 serán otros dos backends de la misma interfaz |
| `src/parts/netlist_parts.h` | El registro en la factoría, **con la ficha de `--help` que se aplazó en D0**, y `valida_puente_serie()` |
| `src/parts/motor_uart.h` | El receptor y el emisor **fijan formato y baudios al empezar cada trama** (D-4). Son las mismas cuentas con los mismos valores: `test407` no se ha movido |
| `src/common/serie_destino.h` | `parsea_baudios()` y `desescapa()` (el `guion`), puros. `make serie` pasa a **114** |
| `src/top/sim_main.cpp` | `--serie` se escribe en la instancia antes de validar y construir; los atributos de la pieza se validan en el paso 5 bis; al acabar, lo que quede a medias en `muestra` y un resumen de contadores por puente |
| `src/top/sc_main_serie.cpp` | La mitad D2 del banco: un F407 con `vcp_demo` y la pieza en PA0-PA3. `testserie`: **79** y **`149861131044 ps`** |
| `src/verif/fw/vcp_demo/` | El firmware: USART2 a 115200 8N1 con RTS por hardware y `LINEN`, HSI a 16 MHz sin PLL (sin cristal, y diez veces menos instrucciones por ms simulado), eco de lo que llega sin error, y la orden `0x13` para dejar de leer 5 ms. 1096 B, `0x8BAC8D5B` |
| `src/verif/fw/huellas.txt` | Su línea, suite `serie`. **La cadena cruzada usada reproduce byte a byte `uart_demo.bin`** (1452 B, `0x63E40E5A`): es la misma que produjo las demás |
| `src/Makefile.mcu-sim` | `fwserie` y `testseriefw`, como los de las otras suites |
| `src/placas/vcp_memoria.xml` | Una placa de ejemplo: el F407 implícito y un puente en memoria con guion y RTS/CTS |
| `doc/parts.md` | §4.9, la pieza en el catálogo |

**Qué prueba `testserie` en esta fase** (P0-P10, además de lo de D1):

* P1 — el firmware arranca, saluda y el saludo llega entero; BRR = `0x8B`;
* P2 y P3 — el eco de un mensaje y de **los 255 valores de byte** (todos menos
  la orden de pausa), en orden y sin tocar;
* P4 y P5 — **RTS/CTS**: con el firmware sin leer durante 5 ms, sin control de
  flujo la USART se desborda (ORE) y se pierden 39 de 40 bytes; con él, el
  puente espera al RTS del MCU y llegan los 40;
* P6 — **baudios equivocados**: el puente a 9600 y el firmware a 115200 dan
  errores de trama o ruido en la USART y el eco no dice lo que se mandó; de
  vuelta a 115200 todo funciona;
* P7 — **un cambio a mitad de trama espera a la siguiente (D-4)**, en los dos
  sentidos: la Z que el puente ya estaba mandando llega entera aunque se le
  cambien formato y baudios, y la Y que el MCU ya estaba devolviendo se lee
  entera aunque se cambien los baudios del receptor;
* P8 — un break del puente es un break LIN: `LBD` sube una vez y la línea sigue
  funcionando;
* P9 — el `rts` del puente, en el pin;
* P10 — la cola hacia el MCU tiene límite, rechaza lo que no cabe y lo cuenta.

**Y desde el XML, con `mcu-sim`:** `placas/vcp_memoria.xml` con `vcp_demo`
imprime el saludo y el eco del guion. Con `baudios="9600"` imprime la basura,
avisa una vez de que los baudios no cuadran y da el recuento de errores. Los
destinos `tcp` y `rfc2217` (el de omisión incluido), un `formato` o unos
`baudios` mal escritos, un guion con un escape que no existe y `rtscts` sin
terminal `cts` se rechazan antes de montar, con el motivo.

**Cómo se ha comprobado que las pruebas pueden fallar:** cinco mutaciones. No
mirar el CTS (3 fallos), no emitir el break (1), y leer en vivo, sin fijarlos al
empezar la trama, el formato del emisor (1) y los baudios del receptor (1). **La
quinta, leer en vivo los baudios del emisor, no la caza ningún banco**, y con
razón: el emisor ya construía el tiempo de bit al empezar la trama, así que esa
mutación solo alarga la parada, y eso es inofensivo. La primera versión de P7
solo cambiaba los baudios del emisor y no cazaba ninguna de estas mutaciones;
por eso se amplió al formato y al receptor.

**Resultado de la verificación, compilado desde cero** con g++ 13 y SystemC
2.3.4, sin avisos: `test407` 2118 y `resto` 2240553274213 ps, `test446` 204 y
1033367277932 ps, `test417` 165 y 718988288 ps (**las tres intactas**);
`testserie` 79 y 149861131044 ps, determinista entre ejecuciones; `make serie`
114; `asanserie` limpio con el chip dentro; las siete placas validan sin un
aviso; `T126` acepta la ficha nueva.

**Desviaciones del plan y decisiones tomadas por el camino:**

1. **`CanalHost` ya existe en D2**, con un solo backend. La pieza no tendrá que
   cambiar cuando lleguen TCP y RFC 2217, solo ganar backends.
2. **Los atributos de la pieza se validan antes de montar**, y no como en el
   resto del catálogo, donde un atributo mal escrito se queda en su valor por
   omisión. En esta pieza callarlo sería peor: `formato="8N1,5"` haría un 8N1
   sin decirlo y el alumno buscaría el fallo en su firmware.
3. **`muestra` y `guion`**, que no estaban en el plan: sin ellos, el destino
   `memoria` solo servía para el banco. Con ellos, `mcu-sim` enseña lo que
   imprime el firmware sin herramientas externas, que es el uso más común.
4. **La placa de ejemplo es `placas/vcp_memoria.xml`**, un F407. La de la Nucleo
   (`nucleo_f446re_vcp.xml`) sigue en D7, cuando haya TCP.
5. **Los formatos 7E1 y 8N2 no se prueban contra el firmware**, que va en 8N1:
   se prueban en el motor (D1, grupo S2). Probarlos con la USART es probar la
   USART, que ya tiene su banco.

**Queda sin probar, y está en P-14:** `set_dtr()` y el terminal `dtr`, que
moverá RFC 2217 en D5.

**PENDIENTE A MANO, como en D0 y D1:** que el CI corra `testserie`
(`.github/workflows/`). **Y uno nuevo:** `src/verif/fw/vcp_demo/Makefile`, que
las herramientas remotas tampoco pueden escribir (tratan como protegido
cualquier fichero llamado `Makefile`). Es el de `uart_demo` con el nombre
cambiado, y se genera con una línea:

```
sed -e 's/uart_demo/vcp_demo/g' \
    -e 's/Firmware de demostracion de USART y UART con CMSIS (fase F4)/Firmware del puente UART: eco por la USART2 con RTS (P-14, fase D2)/' \
    src/verif/fw/uart_demo/Makefile > src/verif/fw/vcp_demo/Makefile
```

Sin él, las suites funcionan igual (el `.bin` está versionado); lo que falla
es `make fwserie`, que regenera el firmware.


#### D3 · TCP en crudo — 29-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/parts/canal_host.h` | `CanalTcp`: servidor en `localhost` sobre `common/red.h`, **sin una línea de código de SO nueva**. Un cliente; el nuevo sustituye al viejo y se avisa una vez (D-5). Cola de 4 KiB hacia el MCU: cuando se llena se deja de leer el socket y TCP frena al emisor, así que en ese sentido **no se tira nada**. Hacia el anfitrión, sin cliente o con 64 KiB sin leer, se descarta y se cuenta. `CanalHost` gana `sondear()` y `pendientes()` |
| `src/parts/puente_serie.h` | El canal según el destino, `canal_ok()`/`error_canal()` (puerto ocupado), `por_red()`, y `hilo_canal`: el **sondeo en tiempo simulado, adaptativo** (un tiempo de carácter con tráfico, 1 ms en reposo; D-9) |
| `src/parts/netlist_parts.h` | `tcp` deja de rechazarse; `guion` con un destino que no es `memoria`, sí. La ficha lo cuenta, y dice qué poner al otro lado |
| `src/top/sim_main.cpp` | Si un puente no puede escuchar, `sim` no arranca y dice por qué. Con un puente por TCP la simulación **no termina sola** (como con `--gdb`), y sin `--tiempo-real` se avisa |
| `src/top/sc_main_serie.cpp` | P11-P17 con un cliente TCP de verdad en el propio banco. `testserie`: **101** y **`236861131044 ps`** |
| `src/placas/vcp_tcp.xml` | La placa de ejemplo por TCP, con las recetas del §7.4 en su cabecera |

**El problema que había que resolver, y cómo.** Un socket de verdad entrega
los datos cuando quiere el sistema operativo. Si la pieza los descubre en un
sondeo en tiempo simulado, cuántos sondeos hacen falta depende de la máquina, y
el tiempo simulado del banco con ello: es exactamente lo que le pasa a
`test407` con el stub de GDB, y por lo que allí se contrasta el `resto`. Aquí se
ha evitado desde el diseño: el banco **provoca** cada efecto del anfitrión
(conectar, mandar, cerrar) y **espera en tiempo de pared**, llamando al
`sondear()` del canal, a que el canal lo tenga dentro; solo entonces deja
avanzar la simulación. Mientras espera no corre ningún proceso de SystemC,
porque comparten un hilo. Resultado: **`testserie` sigue contrastando el
`total`**, y sale igual al picosegundo en cinco ejecuciones seguidas, dos de
ellas con cuatro procesos compitiendo por la CPU.

**Qué prueba `testserie` en esta fase:**

* P11 — escucha en `localhost`, se describe, acepta a un cliente;
* P12 y P13 — el eco por TCP de un mensaje y de los 255 valores de byte;
* P14 — **sin cliente**, lo que manda el MCU se descarta y se cuenta (la «x» que
  ya estaba dentro llega igual al MCU);
* P15 — reconexión en caliente;
* P16 — un cliente nuevo sustituye al anterior: al viejo se le cierra, el eco va
  al nuevo;
* P17 — **puerto ocupado**: un canal no puede escuchar y una pieza sobre ese
  puerto lo dice con `canal_ok()`, que es lo que mira `sim` para no arrancar.

**Y con `mcu-sim`, a mano:** `placas/vcp_tcp.xml` con `vcp_demo` y
`--tiempo-real`, y un cliente en Python: el saludo sale por la consola, lo que
manda el cliente llega al MCU y el eco vuelve al cliente. **Consumo en reposo**,
seis segundos medidos en `/proc` en la misma máquina:

| | CPU |
| :--- | ---: |
| puente TCP, `--tiempo-real` | **9,2 %** |
| solo `--gdb`, `--tiempo-real` | 11,0 % |
| puente TCP, sin freno | 97 % (un núcleo: es lo que avisa `sim`) |

El 5,3 % que decía el plan era de otra máquina; lo que vale es que el puente
cueste lo mismo o menos que el stub de GDB en la misma, y cuesta menos.

**Cómo se ha comprobado que las pruebas pueden fallar:** tres mutaciones del
canal y la pieza (no contar lo descartado, no avisar al emisor cuando llegan
datos, rechazar al cliente nuevo en vez de sustituir al viejo): 1, 4 y 4 fallos.

**Verificación**, compilado desde cero con g++ 13 y SystemC 2.3.4 y **con el
propio `ci/pasa_suites.sh`**: `test407` 2118 y `resto` 2240553274213 ps,
`test446` 204 y 1033367277932 ps, `test417` 165 y 718988288 ps (**intactas**),
`testserie` 101 y 236861131044 ps; `make serie` 114; `asanserie` limpio; las
ocho placas validan sin un aviso; `testserie` con clang 18, igual al
picosegundo. `canal_host.h` mete `red.h` en todo lo que monta piezas, pero
**los cuatro bancos y `mcu-sim` ya la incluían antes** por el stub de GDB: en
Windows no entra ninguna cabecera que no estuviera.

**Lo que no se ha hecho, y era del plan o lo roza:**

1. **`escucha="red"` (D-7)**: abrir el puerto a otras máquinas exige una
   función nueva en `red.h`. No hace falta para nada de lo que viene y queda
   para cuando alguien lo pida.
2. **«0 bytes perdidos a 115 200 con `--tiempo-real`»** no está en un banco:
   con tiempo de pared de por medio no sería determinista. Lo cubren P13 (255
   bytes seguidos, sin freno) y la prueba a mano.
3. **Dos `testserie` a la vez en la misma máquina chocan en el puerto 47355**:
   el segundo falla en P11 diciéndolo. En el CI cada trabajo tiene su máquina.


#### D4 · El códec de Telnet y RFC 2217 — 29-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/common/telnet2217.h` | El códec, **puro**: `Decodificador` (datos por un lado, sucesos por otro; el estado vive entre llamadas, así que una orden puede llegar partida en cualquier byte), el codificador (`escapa`, `negociacion`, `subopcion`, `orden_2217`, los baudios en orden de red) y el `Negociador`, la política del servidor |
| `src/verif/prueba_rfc2217.cpp` | `make rfc2217`: **64 comprobaciones** sin SystemC. Y un modo `--servidor PUERTO`, un servidor RFC 2217 mínimo con este códec para probar clientes de verdad |
| `src/verif/vectores/captura_rfc2217.py` | Graba una sesión RFC 2217 byte a byte. Sin argumentos, entre el cliente y el servidor de pySerial; con `--contra PUERTO`, de proxy delante de otro servidor |
| `src/verif/vectores/rfc2217_pyserial.vec` | pySerial 3.5 contra pySerial 3.5: abrir, datos con `0xFF` y CR, 9600, 7E1, dos de parada, RTS y DTR, break y purga. 35 órdenes |
| `src/verif/vectores/rfc2217_pyserial_mcusim.vec` | La misma sesión, **pySerial contra el servidor de este proyecto** |
| `src/Makefile.mcu-sim` | `make rfc2217` |

**La política del servidor, y una decisión nueva (D-13).** Pasivo hasta que el
cliente negocia algo; entonces pide BINARY en los dos sentidos, una sola vez.
Acepta que el cliente haga BINARY, SGA y COM-PORT; hace él BINARY y SGA;
rechaza lo demás, ECHO incluido (el eco lo hace el firmware, si lo hace). No
contesta a lo que no cambia nada (RFC 1143), así que no hay bucles. La primera
captura es la que obligó a D-13: el servidor de pySerial **no es pasivo**
(ofrece ECHO, SGA, BINARY y COM-PORT nada más conectarse) y su cliente **no
ofrece BINARY por su cuenta**, solo lo acepta cuando se lo piden.

**Lo que se ha comprobado:**

* **Contra las RFC:** datos y `IAC IAC`; la regla `CR NUL` sin BINARY, en los
  dos sentidos; negociaciones y órdenes sueltas (`NOP`, `AYT`, un `SE` suelto);
  subopciones con `0xFF` dentro (65535 baudios); un flujo cortado en **cada uno
  de sus 24 puntos**, y byte a byte, da lo mismo que entero; un `SB` sin `SE` se
  da por cerrado sin perder lo de detrás, y uno de mil bytes se corta en 256 y
  se cuenta; los 256 valores de byte van y vuelven con BINARY y sin él; y la
  política del negociador, caso por caso.
* **Contra pySerial contra pySerial:** decodificado con nuestro negociador, lo
  que manda el cliente da exactamente los datos que mandó (`Hola\r\xFF\x00fin`,
  con el `NUL` detrás del CR intacto) y las 35 órdenes en su orden con sus
  valores. Y **las respuestas del servidor de pySerial son, byte a byte, las que
  compone este codificador** para las mismas órdenes.
* **Contra pySerial contra este código:** el cliente de pySerial se entiende con
  el servidor pasivo, acaba en RFC 2217 y en BINARY en los dos sentidos, y las
  35 órdenes dejan la línea en 9600 7E2. **La sesión grabada se reproduce
  entera**: con lo que mandó el cliente, el servidor contesta exactamente los
  292 bytes grabados, a trozos o de golpe.

**Dos cosas que las pruebas encontraron por el camino.** La primera versión del
servidor mínimo devolvía el eco de los datos al final de cada trozo, detrás de
las respuestas: con una sesión entera de golpe, el eco salía fuera de su sitio.
Lo cazó la comprobación «de un solo trozo, lo mismo», y el servidor contesta
ahora en el orden en que llegan las cosas, que es lo que tendrá que hacer el
canal de D5. La segunda: una prueba contaba mal a mano las posiciones de dos
órdenes (21 y 27, no 17 y 23) y comparaba una respuesta con el código 106 en vez
del 102; eran fallos de la prueba, no del códec.

**Cómo se ha comprobado que las pruebas pueden fallar:** cinco mutaciones del
códec (no doblar el `IAC` al codificar, no quitar el `NUL` del `CR NUL`,
contestar a un `WILL` repetido, no pedir BINARY, no entregar las subopciones):
4, 4, 1, 6 y 13 fallos. La última hacía que la prueba se cayera leyendo fuera de
una lista vacía; ahora lo dice y sigue.

**Y en Windows:** `prueba_rfc2217`, `prueba_serie` y `prueba_red` compilan con
MinGW-w64 (`x86_64-w64-mingw32-g++`) **sin un solo aviso**. Los nombres de
Telnet (`SE`, `DO`, `IP`, `EC`...) no chocan con ninguna macro de las cabeceras
de Windows. No se han ejecutado allí: eso lo hará el CI si se añade al trabajo
rápido.

**Lo que no se ha hecho, y era del plan:** la captura de `com2tcp-rfc2217`. Hace
falta Windows con com0com, así que pasa a la matriz manual de D6, que ya la
tenía.

**PENDIENTE A MANO:** añadir `rfc2217` a la línea `run:` del trabajo `rapidas`
(`.github/workflows/`).

---

#### D5 · RFC 2217 en la pieza — 30-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/parts/canal_host.h` | `LineaSerie`, la interfaz de lo que el terminal puede pedirle a la línea (cada `pide_*` devuelve lo **aplicado**, que es lo que se confirma). `CanalTcp` gana tres ganchos protegidos (`recibidos`, `al_conectar`, `puede_mandar`) sin cambiar lo que hace. Y `CanalRfc2217`, encima: el códec de la D4 **por conexión**, procesado byte a byte (una negociación cambia cómo se leen los datos que vienen detrás), la tabla del §10.4 entera, las máscaras, SUSPEND con retención de hasta 64 KiB y PURGE |
| `src/parts/puente_serie.h` | La pieza **es** la `LineaSerie`: decide qué se aplica según D-14. Break sostenido (`set_break`, SET-CONTROL 5/6), las líneas de módem que ve el anfitrión (CTS = el RTS del MCU; DSR y DCD siempre) y un hilo que avisa al canal cuando el MCU mueve su RTS. Los errores y breaks del MCU van al canal como LINESTATE |
| `src/parts/motor_uart.h` | `EmisorUart::a_cero()`, para el break sostenido |
| `src/parts/netlist_parts.h` | `rfc2217` deja de rechazarse, y el valor por omisión (`rfc2217:3355`) pasa a ser válido. Ficha actualizada |
| `src/verif/cliente_2217.h` | El terminal del banco: negocia como pySerial, manda datos con el `IAC` doblado y órdenes esperando su respuesta, **bombeando el canal en tiempo de pared** y **sincronizándose** con él (los bytes de cada lado, contados por los dos) para que el banco siga siendo determinista |
| `src/top/sc_main_serie.cpp` | Un tercer puente en los mismos pines, con `host="rfc2217:47357"`, `baudios="host"` y DTR en PB0. Grupos **P18 a P32** |
| `src/placas/vcp_rfc2217.xml` | La placa de ejemplo, con `baudios="host"` y qué poner al otro lado |

**Una decisión nueva (D-14):** `baudios="host"` entrega al terminal la
configuración entera de la línea -baudios, formato y control de flujo-, y unos
baudios fijos se la quitan entera. DTR, RTS y el break son señales, y esas las
mueve siempre el terminal. La obligó la captura de la D4: el cliente de pySerial
manda «sin control de flujo» cada vez que abre el puerto, y un `flujo="rtscts"`
del XML habría durado hasta la primera conexión.

**Dos correcciones del plan (§10.4).** PURGE-DATA estaba al revés: 1 es el búfer
de **recepción** del servidor (lo que ha llegado del MCU y no ha salido), 2 el de
**transmisión** (lo que espera para ir al MCU), como dicen la RFC y pySerial. Y
SUSPEND/RESUME **no se confirman** (el plan decía 108/109): la RFC no lo pide y
el servidor de pySerial no lo hace. Mientras dura un SUSPEND no sale nada, ni
datos ni respuestas; las respuestas esperan en la cola y salen al reanudar.

**Lo que se ha comprobado** (`testserie`: 101 → **189**, `400677589564 ps`,
determinista también con la CPU cargada), siempre mirando el efecto **en los
pines y en el firmware**, no en el protocolo:

* **P18-P20:** el servidor no manda nada al conectarse (D-13); la negociación
  acaba en COM-PORT y BINARY en los dos sentidos; llega un MODEMSTATE inicial
  `0xBB`; la firma; los 255 valores de byte más un `CR NUL` van y vuelven.
* **P21-P23:** SET-BAUDRATE 9600 **desde el terminal** hace que la USART levante
  errores; 0 es consulta; 20 Mbaudios se contestan con los que hay. 7E2 desde
  el terminal: una `C` llega al firmware (en 8N1) como `0xC3`, porque la paridad
  es su octavo bit. Un cambio de baudios y paridad mientras una `Z` está en la
  línea espera a la trama siguiente (D-4).
* **P24:** con los baudios fijos, el terminal recibe los del XML en baudios,
  formato y flujo, pero RTS sí se mueve.
* **P25-P27:** break sostenido → PA3 a cero → `LBD` en el firmware; DTR en PB0 y
  RTS en PA0, en voltios; RTS/CTS pedido por el terminal evita el desbordamiento
  que P4 provoca; XON/XOFF y el flujo de entrada se contestan como no admitidos.
* **P28:** una pausa del firmware (RXNE puesto, RTS por hardware arriba) llega al
  terminal como `0xA1` y la vuelta como `0xB1`; con la máscara a cero, nada.
* **P29:** con la máscara de LINESTATE a cero (la de omisión), los errores de
  trama no salen; con `0x0C`, llegan con el bit 3. Se provocan poniendo el
  puente a 230 400 con una `A` ya en la línea: la `A` llega bien (D-4) y su eco
  se lee al doble, con la parada cayendo en un cero.
* **P30-P31:** PURGE 2 tira 100 bytes antes de que lleguen al firmware; SUSPEND
  retiene el eco y RESUME lo entrega en orden; PURGE 1 tira lo retenido, y su
  confirmación no sale hasta el RESUME.
* **P32:** un cliente sin Telnet sustituye al terminal (D-5), su SET-BAUDRATE
  sin WILL COM-PORT se ignora sin contestar, la sesión empieza de cero, y sin
  BINARY el CR del eco sale como `CR NUL`.

**Y con `mcu-sim` y pySerial de verdad, a mano:** `placas/vcp_rfc2217.xml` con
`vcp_demo` y `--tiempo-real`, y un script con `rfc2217://localhost:3355`: eco a
115200 (con `0xFF` y `0x00`), a 9600 el firmware no devuelve nada (le llegan
tramas con error), de vuelta a 115200 eco otra vez, `cts`, `dsr` y `cd`
verdaderos, DTR y un `send_break` sin romper la línea. Es un adelanto de la D6,
no la D6: esto no está en el CI.

**Cómo se ha comprobado que las pruebas pueden fallar:** ocho mutaciones —PURGE 1
y 2 cambiados, la máscara de LINESTATE ignorada, el decodificador sin pasar a
BINARY, el terminal mandando aunque los baudios sean fijos, el break sostenido
ignorado, sin el delta de CTS, órdenes atendidas sin negociar COM-PORT y SUSPEND
sin retener las respuestas—: 2, 2, 2, 6, 2, 3, 3 y 2 fallos. La última
**sobrevivió** a la primera versión del banco (los datos se retenían, pero las
respuestas salían igual) y se añadió la comprobación que la caza.

**Las otras tres suites, intactas:** `test407` 2118 y `resto` 2240553274213 ps,
`test446` 204 y 1033367277932 ps, `test417` 165 y 718988288 ps. `make serie`
(114) y `make rfc2217` (64) siguen igual. `make asanserie` (AddressSanitizer y UBSan): 189 comprobaciones, el mismo tiempo al picosegundo y ni un aviso. Sin avisos nuevos con
clang, y `prueba_serie` sigue compilando con MinGW.

**El CI la tumbó en los dos macOS, y con razón.** Linux y Windows en verde;
en Apple Silicon e Intel, un fallo en P18 («y el puente lo sabe igual») y el
tiempo simulado desviado en 1 y 2 ms exactos. La causa era del banco, no del
puente: dos esperas daban por hecho lo que no estaba garantizado.

* `negocia()` terminaba en cuanto **el cliente** había visto lo que esperaba,
  pero su última respuesta (el `WILL BINARY` que contesta a un `DO`) podía
  seguir en el núcleo. En Linux el loopback es tan rápido que siempre había
  llegado; en macOS no, y el puente aún no la tenía al comprobarlo. Peor: esos
  bytes los recogía después el **sondeo de la pieza, en tiempo simulado**, que
  es justo lo que la regla de la D3 prohíbe.
* `eco()` decidía si seguir esperando medio milisegundo simulado más mirando
  lo que **ya había entregado el loopback**. Dos vueltas de más son el
  milisegundo de más.

El arreglo es contar los bytes de los dos lados: `CanalTcp` lleva lo que ha
recibido y mandado **con el cliente actual** (`recibidos_conexion()`,
`enviados_conexion()`), el terminal lo que ha mandado y recibido, y
`Cliente2217::sincroniza()` espera, bombeando, a que coincidan en los dos
sentidos. Todas las esperas de P18-P32 pasan por ahí: lo que el banco mira, y
lo que la simulación ve al seguir, es todo lo que se han dicho, no lo que el
núcleo haya tenido a bien entregar.

Y para no depender de tener un macOS a mano, **el banco sabe hacerse el lento**:
con `TESTSERIE_RUIDO` en el entorno, solo sondea el canal una vez de cada cuatro
y el terminal solo lee su socket una de cada cuatro. El banco de la primera
versión, con ruido, falla en cuatro comprobaciones y se desvía a
`402590784008 ps`; el arreglado da 189 y `400677589564 ps` con ruido y sin él,
que es la cifra del invariante y la que ya daba en Linux.
**Y el CI lo pasa con ruido en Linux:** el trabajo `linux` (las dos Ubuntu)
ejecuta `testserie` una segunda vez con `TESTSERIE_RUIDO=1` y la contrasta
contra la misma línea de `verif/invariantes.txt`. Una espera nueva mal
sincronizada se ve ahí, sin esperar a que la destape un macOS.

**Lo que no se ha hecho, dicho:** LINESTATE no notifica desbordamiento (bit 1)
cuando se llena la cola hacia el anfitrión: se cuenta en `descartados()`. La
firma no lleva versión, porque el proyecto no tiene número de versión. Un break
que manda el MCU va como LINESTATE bit 4, pero ningún banco lo provoca:
`vcp_demo` no manda breaks.

---

#### D6 · Interoperabilidad — 30-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/verif/serie/interop.py` | Arranca el **`mcu-sim` de verdad** —el ejecutable, con `placas/vcp_rfc2217.xml` y `vcp_demo`, `--tiempo-real` y `--serie`— en un puerto libre, y le conecta pySerial y socat. **30 comprobaciones**, grupos I1 a I9. Sin SystemC ni compilar nada: solo Python 3 y pySerial |
| `src/Makefile.mcu-sim` | `make interop` (construye `mcu-sim` si hace falta) |
| `.github/workflows/suites.yml` | Un paso de interoperabilidad en las cuatro plataformas: las dos Ubuntu (`python3-serial` y `socat` de apt, con `make interop`), los dos macOS (socat de Homebrew, pySerial con pip, detrás de construir `mcu-sim`) y Windows (el Python del runner en PowerShell contra el `.exe` autónomo, sin socat) |

**Lo que se comprueba**, siempre por lo que vería el alumno:

* **I1-I6, `rfc2217://`:** el eco, los 255 valores y un `CR NUL` (la sesión
  acaba en BINARY con el cliente de verdad); a **9600 desde pySerial** el
  firmware no devuelve nada y a 115200 vuelve el eco; en **8E1** una `A`
  (paridad 0, que la USART toma por una parada mala) no vuelve y una `C`
  (paridad 1) sí; CTS, DSR y DCD se leen activas; RTS, DTR, un break y las dos
  purgas no rompen la línea; una ráfaga de 40 durante una pausa del firmware
  **se pierde sin control de flujo** (vuelve 1) y **vuelve entera con
  `rtscts=True`**; cerrar y reabrir funciona, y un segundo terminal sustituye
  al primero.
* **I7, la línea fijada en el XML (D-14),** con dos placas que el script escribe
  al vuelo: pySerial abre con los valores del XML, y al pedir 9600 **lanza
  `ValueError: remote rejected value for option 'baudrate'`**: el puente
  contesta con los baudios que hay y pySerial no se deja engañar.
* **I8, `socket://`** contra el modo en crudo: eco y los 255 valores.
* **I9, socat** `pty` ↔ `tcp` (la receta del §7.4), con el pty abierto por
  pySerial como un puerto serie más: eco y los 255 valores.

**Lo que ha enseñado, y va a la receta de la D7.** Con la línea fijada en el
XML **y `flujo="rtscts"`**, pySerial **no abre** si no se le pide `rtscts=True`:
al abrir manda «sin control de flujo», el puente contesta con el que hay (3) y
pySerial lo toma por un rechazo. Es coherente con D-14 —el terminal no puede
cambiar lo que la placa fija— pero el mensaje (`remote rejected value for option
'control'`) no lo va a entender un alumno. I7 lo deja comprobado en las dos
direcciones, y la receta tendrá que decirlo. `vcp_tcp.xml`, que lleva ese
flujo, es para el modo en crudo, donde no pasa.

**Cómo se ha comprobado que puede fallar:** con `baudios="115200"` en vez de
`host` en la placa, la sesión de I1-I6 se corta en el cambio a 9600 con esa
misma excepción (ahora recogida como fallo, y el resto de grupos sigue). Y el
control de flujo se ha medido en las dos direcciones antes de escribir I5:
tres veces 1 de 40 sin él, tres veces 40 de 40 con él.

**La matriz manual (§10.6).** Hecha aquí, en Linux: la receta del §7.4 tal cual,
`socat pty,link=…,raw,echo=0 tcp:localhost:PUERTO` y `picocom -b 115200` sobre
el enlace, con eco (socat 1.8.0.0, picocom 3.1). **Lo demás necesita Windows o
un escritorio y queda PENDIENTE A MANO:** `com2tcp-rfc2217` + com0com + Tera
Term, HW VSP3 + PuTTY, HHD (si se aclara la licencia), CoolTerm y PuTTY *Raw*,
socat + picocom en macOS, y ttynvt (necesita CUSE, que este entorno no tiene).
La D4 ya había mandado aquí la captura de `com2tcp-rfc2217`. **Cómo hacerlas, una
a una, está justo detrás del registro de la D6** (M1 a M8).

**Y el riesgo 3 del §10.7** (`FLOWCONTROL-SUSPEND` hacia el cliente cuando se
llena la cola hacia el MCU) **no se ha hecho, y no hace falta**: cuando la cola
está llena el canal deja de leer el socket, y TCP para al cliente sin perder
nada (D3). Ningún cliente de la matriz automática lo echa de menos.

---

#### D6 · Las pruebas manuales pendientes, una a una

Lo que sigue es el guion para completar la matriz del §10.6. Cada prueba se
puede hacer sola, pero **conviene seguir el orden M1 → M8**: primero lo que no
instala nada, después un redirector cada vez. Los redirectores de Windows son
drivers, y dos a la vez peleándose por el mismo `COM20` dan fallos que no son
del puente. Por eso cada prueba de Windows usa su propio número de COM y dice
cómo desinstalar lo que ha instalado.

##### Lo común a todas

**El firmware y la placa.** Todas usan `vcp_demo` (`src/verif/fw/vcp_demo/vcp_demo.bin`,
versionado) y `src/placas/vcp_rfc2217.xml`: USART2 del F407 a 115200 8N1, puente
`VCP` con `host="rfc2217:3355"` y `baudios="host"`, `cts` en PA1 y `dtr` en PB0.
Lo que hace el firmware, y es lo único que hay que saber para leer los
resultados:

* al arrancar manda `vcp_demo listo\r\n`. **Sin `--espera-terminal` casi
  siempre se pierde**: sale a los pocos milisegundos, cuando todavía no hay
  nadie conectado, y el puente descarta lo que no tiene a quién dar (D-6).
  Entonces solo se ve en la consola de `mcu-sim`. **Con `--espera-terminal`**
  (D7, D-15) el MCU no arranca hasta que el terminal está conectado, y el
  saludo es lo primero que aparece en él: comprobar eso también es parte de
  cada prueba, sobre todo con los redirectores, que se conectan antes que el
  terminal;
* **devuelve cada byte que recibe sin error**, tal cual, sin añadir ni quitar
  finales de línea. Los que llegan con error de trama, de ruido o de paridad
  **no** los devuelve: por eso un desajuste de baudios se ve como «no vuelve
  nada», y no como basura;
* el byte `0x13` (**Ctrl-S**) no lo devuelve: es la orden de dejar de leer
  5 ms. Si el terminal manda Ctrl-S por su cuenta (XOFF), el firmware se para
  un momento, y ya está.

**La consola de `mcu-sim`** enseña, con `[VCP]` delante, lo que manda el MCU,
**línea a línea**: solo imprime cuando llega un `\n`. Si el terminal manda solo
CR al pulsar Intro, el eco se ve en el terminal pero no en la consola hasta que
llegue un LF. Para las pruebas conviene que Intro mande **CR+LF**.

**El ejecutable.**

* **Linux y macOS**: el del repositorio, `cd src && make -f Makefile.mcu-sim
  mcu-sim`, o el paquete de un CI verde (`mcu-sim-linux-x86_64.tar.gz`,
  `mcu-sim-macos-arm64.tar.gz`, `mcu-sim-macos-x86_64.tar.gz`; en macOS hace
  falta `brew install systemc` y quitar la cuarentena, `doc/ejecutables.md`
  §4.3).
* **Windows**: el artefacto `mcu-sim-windows-x86_64` de una ejecución verde de
  `suites` en GitHub (Actions → la ejecución → *Artifacts*), descomprimido en
  `C:\mcu-sim-win\`. No necesita MSYS2 ni ninguna DLL. Las pruebas de Windows
  se hacen desde `cmd` o PowerShell, **no** desde un shell de MSYS2, que es como
  lo usará un alumno.
* Y del repositorio, en todas: la carpeta `src` con `placas/` y
  `verif/fw/vcp_demo/`. En Windows se supone clonado en `C:\mcu-sim\`.

**El arranque, igual en todas**, desde `src` y **siempre antes que el
redirector o el terminal** (el puente es el servidor; D-1):

```
# Linux y macOS
./build/mcu-sim placas/vcp_rfc2217.xml verif/fw/vcp_demo/vcp_demo.bin --tiempo-real

# Windows (cmd)
cd C:\mcu-sim\src
C:\mcu-sim-win\mcu-sim.exe placas\vcp_rfc2217.xml verif\fw\vcp_demo\vcp_demo.bin --tiempo-real
```

Tiene que decir, en este orden:

```
  serie VCP: RFC 2217 en localhost:3355, 115200 8N1
  ...
esperando a los puentes serie; la simulacion no se detiene sola (Ctrl-C para salir)
  [VCP] vcp_demo listo
```

Para el modo en crudo, lo mismo con `--serie VCP=tcp:3355` al final (la
consola dirá `TCP en crudo en localhost:3355`). Se para con **Ctrl-C**. Si
Windows pregunta por el cortafuegos al arrancar, se puede cancelar: el puente
solo escucha en `127.0.0.1`, y el bucle local no pasa por el cortafuegos.

**La comprobación básica**, que se repite en todas y se llama **ECO** en lo que
sigue: se teclea `Hola` e Intro, y en el terminal aparece `Hola` (una vez, la
del firmware; si sale dos veces, el terminal tiene el eco local encendido y hay
que apagarlo) y en la consola de `mcu-sim` aparece `[VCP] Hola`.

**Lo que hay que anotar** al terminar cada una, en la columna *Estado* del
§10.6: la fecha, el sistema (con versión y arquitectura), la versión de cada
programa, y «bien» o qué falló. Si falla, conviene guardar la consola de
`mcu-sim` y, si el programa la tiene, su traza.

##### M1 · CoolTerm por TCP, sin puerto (D-c) — Windows, macOS y Linux

**Para qué:** es el camino que el §7.6 recomienda a la mayoría. No instala
drivers. Se hace en las tres plataformas.

**Programas:** CoolTerm de Roger Meier (freeware), la última versión de
<https://freeware.the-meiers.org/>. En Linux, el paquete para x86-64; en macOS,
el universal.

**Pasos:**

1. Arrancar `mcu-sim` en **modo crudo**: la orden común con `--serie VCP=tcp:3355`.
2. Abrir CoolTerm. *Connection → Options…* (o el botón *Options*):
   * *Serial Port → Port*: **TCP Connection**; *Mode*: **Client**; *IP
     Address*: `127.0.0.1`; *Port*: `3355`;
   * *Terminal → Enter Key Emulation*: **CR+LF**; *Local Echo*: **desmarcado**.
   * *OK*.
3. *Connect*. Hacer **ECO**.
4. Pegar en el terminal un texto de unas 200 letras (*Connection → Send
   String…*, o pegar con el portapapeles). Tiene que volver entero.
5. *Disconnect*, *Connect* otra vez, y **ECO**: la reconexión funciona (D-5).
6. Parar `mcu-sim` con Ctrl-C **con CoolTerm conectado**. CoolTerm tiene que
   enterarse (se desconecta o avisa). Volver a arrancar `mcu-sim`, *Connect*, y
   **ECO**.
7. *(Opcional)* Arrancar `mcu-sim` **en modo RFC 2217** (la orden común sin
   `--serie`) y conectar CoolTerm igual. CoolTerm no habla Telnet: el puente
   es pasivo hasta que alguien negocie (D-13), así que tiene que funcionar
   igual que en crudo, con dos diferencias: **un byte `0xFF` no llega bien**
   (sin Telnet, el puente lo toma por el principio de una orden), y cada CR del
   eco llega **seguido de un NUL** (la regla del NVT, D-8), que CoolTerm no
   pinta pero se ve en su vista hexadecimal (*View → View Hex*). Con texto
   normal no se nota. Esto confirma por qué D-2 separa los dos modos.

**Bien si:** 3 a 6 dan eco, sin eco doble. **Se anota:** versión de CoolTerm y
sistema, una vez por plataforma.

##### M2 · PuTTY en modo *Raw* (D-c) — Windows (Linux opcional)

**Programas:** PuTTY 0.8x de <https://www.chiark.greenend.org.uk/~sgtatham/putty/>
(`putty.exe` basta; no hace falta instalar).

**Pasos:**

1. `mcu-sim` en **modo crudo** (`--serie VCP=tcp:3355`).
2. Lanzar PuTTY **desde la línea de órdenes**, que fija todo sin tocar la
   configuración guardada:

   ```
   putty.exe -raw -P 3355 127.0.0.1
   ```

   Y, antes de teclear, en el menú de la ventana (clic en el icono → *Change
   Settings…*) → *Terminal*: *Local echo* **Force off** y *Local line editing*
   **Force off**. En modo *Raw*, PuTTY los deja en *Auto* y, con eso, suele
   mandar la línea entera al pulsar Intro y pintarla él: parecería eco doble.
   *Apply*.
3. **ECO**. Intro en PuTTY manda solo CR: el eco sale en PuTTY, pero la consola
   de `mcu-sim` no imprime la línea hasta un LF (Ctrl-J lo manda).
4. Cerrar PuTTY, volver a lanzarlo, **ECO** (reconexión).

**Bien si:** eco sin duplicar. **Se anota:** versión de PuTTY y de Windows. En
Linux, lo mismo con `putty -raw -P 3355 127.0.0.1` (paquete `putty`), opcional.

##### M3 · socat + picocom en macOS (D-b sin driver) — macOS, los dos

**Para qué:** la receta de `doc/puente_serie.md` §4.1 en macOS. En Linux está
hecha; el CI ya prueba socat en macOS con pySerial (I9 e I11), pero no con un
terminal.

**Programas:** `brew install socat picocom` (Homebrew).

**Pasos, en tres terminales:**

1. Terminal 1, desde `src`: `mcu-sim` en **modo crudo**
   (`./build/mcu-sim placas/vcp_rfc2217.xml verif/fw/vcp_demo/vcp_demo.bin --tiempo-real --serie VCP=tcp:3355`).
2. Terminal 2: `socat -d -d pty,link=$HOME/vcp,raw,echo=0,wait-slave tcp:127.0.0.1:3355`
   (la receta de `doc/puente_serie.md` §4.1). Tiene que decir `PTY is
   /dev/ttys00N`, y `starting data transfer loop` **solo cuando** picocom abra
   el puerto: es `wait-slave`. Si en el terminal 1 se añadió
   `--espera-terminal`, el saludo tiene que salir en picocom. **Usar
   `127.0.0.1`**: el puente solo escucha en IPv4, y según la herramienta y el
   sistema `localhost` puede resolverse antes a `::1`. Si con `localhost`
   funciona igual, anotarlo: las recetas lo usan.
3. Terminal 3: `picocom -b 115200 --omap crcrlf ~/vcp` (salir con **Ctrl-A
   Ctrl-X**). `--omap crcrlf` hace que Intro mande CR+LF (véase «lo común»).
   **ECO**.
4. En picocom, **Ctrl-A Ctrl-U** sube la velocidad del pty: en crudo **no pasa
   nada** (el pty no transmite baudios: es lo esperado, y la razón de RFC 2217).
5. Salir de picocom y volver a entrar: **ECO**.
6. Comprobar que el pty no aparece en `/dev/cu.*` (la advertencia del §5.2):
   `ls /dev/cu.*`. Las aplicaciones gráficas que solo listan `cu.*` no lo verán.

**Bien si:** 3 y 5 con eco. **Se anota:** macOS y arquitectura, versiones de
socat y picocom. Hacerlo en Apple Silicon **y** en Intel si se tiene.

##### M4 · ttynvt: un `/dev/tty` que habla RFC 2217 — Linux

**Para qué:** el equivalente Linux de HW VSP (§7.3), y la única forma en Linux
de que **`stty` o picocom cambien los baudios de la línea simulada** con un
puerto serie del sistema.

**Requisitos:** un Linux con CUSE (`sudo modprobe cuse`; tiene que existir
`/dev/cuse`), y para compilar `git autoconf automake make gcc pkg-config` y
`libfuse-dev` (FUSE 2; en Debian/Ubuntu, `sudo apt install libfuse-dev`). Hace
falta `sudo`. **No vale un contenedor** sin `/dev/cuse` (es por lo que no se ha
hecho aquí).

**Instalación:**

```
git clone https://github.com/lars-thrane-as/ttynvt.git
cd ttynvt
autoreconf -vif
./configure
make
```

**Pasos:**

1. Terminal 1: `mcu-sim` en **modo RFC 2217** (la orden común, sin `--serie`).
2. Terminal 2, con las opciones de la documentación de ci4rail (`-f`, en
   primer plano). El ejecutable es el `ttynvt` que deja `make` (`find . -name
   ttynvt -type f` si no está en `src/`):

   ```
   sudo ./src/ttynvt -f -E -M 199 -m 6 -n ttyNVT0 -S 127.0.0.1:3355
   ```

   `-M 199 -m 6` son el número mayor y menor del dispositivo; si 199 está
   ocupado en esa máquina (`grep 199 /proc/devices`), otro libre. Tiene que
   aparecer `/dev/ttyNVT0` (`ls -l /dev/ttyNVT0`; si hace falta, `sudo chmod
   666 /dev/ttyNVT0` para no usar sudo en picocom).
3. Terminal 3: `picocom -b 115200 --omap crcrlf /dev/ttyNVT0`. **ECO**.
4. En picocom, **Ctrl-A Ctrl-B**, escribir `9600` e Intro: picocom cambia la
   velocidad y ttynvt la manda por RFC 2217. Teclear `Hola`: **no vuelve
   nada** (el firmware sigue a 115200 y descarta las tramas con error).
5. **Ctrl-A Ctrl-B**, `115200`: **ECO** otra vez.
6. Salir de picocom. Con `stty -F /dev/ttyNVT0 9600` y después `stty -F
   /dev/ttyNVT0` tiene que leerse `speed 9600 baud`; volver con `stty -F
   /dev/ttyNVT0 115200`.
7. *(Opcional)* Paridad: `picocom -b 115200 -p e --omap crcrlf /dev/ttyNVT0`
   (8E1). `A` no vuelve (paridad 0: la USART la toma por una parada mala); `C`
   sí vuelve (paridad 1). Es lo mismo que comprueba I3 con pySerial.

**Bien si:** 3 y 5 con eco, 4 sin él, 6 con la velocidad leída. **Se anota:**
distribución y núcleo, *commit* de ttynvt, versión de picocom. Si en 4 **sí**
vuelve el eco, ttynvt no está mandando SET-BAUDRATE: la traza del puente no lo
dice, pero la consola de `mcu-sim` tampoco mostraría errores; mirar la
salida de ttynvt en el terminal 2 (va en primer plano por `-f`).

##### M5 · HW VSP3 Single + PuTTY (o Tera Term) — Windows 10 y 11, x64

**Para qué:** un COM de Windows con un solo programa, cliente TCP, con RFC 2217
(«NVT») y sin él. Su ficha solo cita hasta Windows 10: **la prueba es sobre
todo que se instale y funcione en Windows 10 y 11 x64 sin modo de prueba**.

**Programas:** HW VSP3 Single, `hw-vsp3s_3-1-2.exe`, de
<https://www.hw-group.com/software/hw-vsp3-virtual-serial-port> (freeware; para
una empresa, la condición de citar a HW group). PuTTY como en M2, o Tera Term 5.

**Instalación:** ejecutar `hw-vsp3s_3-1-2.exe` **como administrador**. Si
Windows pide aceptar un driver, aceptarlo. **Anotar** si pide desactivar Secure
Boot, activar el modo de prueba o si *Integridad de memoria* (Seguridad de
Windows → Seguridad del dispositivo → Aislamiento del núcleo) lo bloquea: eso
es el resultado de la prueba, no un obstáculo que sortear.

**Pasos — con RFC 2217:**

1. `mcu-sim` en **modo RFC 2217** (la orden común, sin `--serie`).
2. Abrir HW VSP3 (como administrador). Pestaña *Settings*: marcar **NVT
   Enable**, **Create VSP Port at HW VSP startup** y **Connect to device even if
   VSP Port is closed**; desmarcar **Strict Baudrate Emulation**. Pestaña
   *Virtual Serial Port*: *Port Name* `COM20`, *IP Address* `127.0.0.1`, *Port*
   `3355`. **Create COM**. El estado tiene que pasar a conectado.
3. Terminal sobre el COM:

   ```
   putty.exe -serial COM20 -sercfg 115200,8,n,1,N
   ```

   **ECO**. (PuTTY en serie no tiene eco local: no hace falta tocar nada.)
4. **Cambio de baudios desde el terminal.** Cerrar PuTTY y abrir
   `putty.exe -serial COM20 -sercfg 9600,8,n,1,N`. `Hola`: **no vuelve nada**.
   Cerrar y volver con 115200: **ECO**. (Con Tera Term se hace en caliente:
   `ttermpro.exe /C=20 /BAUD=115200`, y *Setup → Serial port… → Speed* 9600 →
   *New setting*; volver a 115200.)
5. **Reconexión al reiniciar `mcu-sim`:** con PuTTY abierto en COM20, parar
   `mcu-sim` (Ctrl-C) y volver a arrancarlo. HW VSP3 tiene que reconectar solo
   (el estado lo dice). **ECO** en el mismo PuTTY, sin cerrarlo.
6. *(Opcional)* Paridad: `-sercfg 115200,8,e,1,N`. `A` no vuelve, `C` sí.

**Pasos — en crudo:** parar todo; `mcu-sim` con `--serie VCP=tcp:3355`; en
HW VSP3, *Delete COM*, desmarcar **NVT Enable**, *Create COM* igual. Repetir 3
y 5. En 4, ahora **sí** vuelve el eco a 9600 (en crudo los baudios no llegan a
la línea: el pty de M3 hace lo mismo).

**Desinstalar** antes de M6: *Delete COM* y desinstalar HW VSP3 desde
*Aplicaciones*.

**Bien si:** instala sin modo de prueba; con NVT, 3 y 5 con eco y 4 sin él; en
crudo, eco siempre. **Se anota:** Windows 10 y 11 por separado (versión y
compilación, `winver`), versión de HW VSP3, y lo que pidió al instalar.

##### M6 · com0com + `com2tcp-rfc2217` + Tera Term — Windows 10 y 11, x64

**Para qué:** la opción libre (GPL) y la que el §7.4 da por más probada. Es
también la prueba de que el cliente RFC 2217 de hub4com se entiende con un
servidor **pasivo** (D-13), que la D4 no pudo capturar.

**Programas:**

* **com0com 2.2.2.0 firmado**, `com0com-2.2.2.0-x64-fre-signed.zip`, de
  <https://sourceforge.net/projects/com0com/files/com0com/2.2.2.0/>. **No la
  3.0.0.0**: esa va firmada solo para pruebas y exige `bcdedit -set TESTSIGNING
  ON`, que no se le puede pedir a un alumno.
* **hub4com 2.1.0.0**, `hub4com-2.1.0.0-386.zip`, de
  <https://sourceforge.net/projects/com0com/files/hub4com/>. Se descomprime en
  `C:\hub4com\`; trae `hub4com.exe` y los `.bat`, entre ellos
  `com2tcp-rfc2217.bat`.
* **Tera Term 5**, de <https://teratermproject.github.io/>.

**Instalación del par virtual:**

1. Descomprimir com0com y ejecutar `setup.exe` **como administrador**. Aceptar
   el driver. **Anotar** si Windows lo bloquea (Secure Boot, *Integridad de
   memoria*; §5.4).
2. Abrir *Setup Command Prompt* (en el menú de inicio, carpeta com0com) como
   administrador. Ver qué hay con `list`. Si el instalador ya creó el par
   `CNCA0`/`CNCB0`, quitarlo con `remove 0`.
3. Crear el par con el lado del alumno como `COM21` y la emulación de baudios:

   ```
   install PortName=COM21,EmuBR=yes -
   list
   ```

   `list` tiene que enseñar algo como `CNCA0 PortName=COM21,EmuBR=yes` y
   `CNCB0 PortName=-`. **El lado `CNCB0` es el que usa hub4com; el alumno abre
   `COM21`.** Si el número de par no es 0, usar `CNCBn` en lo que sigue.

**Pasos:**

1. `mcu-sim` en **modo RFC 2217** (la orden común, sin `--serie`).
2. En un `cmd` normal:

   ```
   cd C:\hub4com
   com2tcp-rfc2217.bat \\.\CNCB0 127.0.0.1 3355
   ```

   Se queda en primer plano. Con `127.0.0.1` y el puerto hace de **cliente**
   (con solo el puerto haría de servidor). Guardar lo que imprime: es la traza
   de la negociación.
3. Tera Term sobre el otro lado:

   ```
   "C:\Program Files (x86)\teraterm5\ttermpro.exe" /C=21 /BAUD=115200
   ```

   (la ruta es la de la instalación por omisión; si no está ahí, la del acceso
   directo del menú de inicio).

   *Setup → Terminal…*: *New-line* **Receive: AUTO**, **Transmit: CR+LF**;
   *Local echo* **desmarcado**. **ECO**.
4. **Cambio de baudios en caliente, que es el objeto de esta prueba:** *Setup →
   Serial port… → Speed* **9600** → *New setting*. `Hola`: **no vuelve nada**.
   Volver a **115200**: **ECO**, el texto sale limpio.
5. *Setup → Serial port…*: *Data* 8, *Parity* **even**, *Stop* 1 → *New
   setting*. `A` no vuelve; `C` sí. Volver a *none*.
6. *Control → Send break* (Alt+B): después, **ECO**. La línea sigue viva.
7. Cerrar Tera Term y volver a abrirlo con la misma orden: **ECO** (hub4com
   sigue conectado; el puente no ha visto nada).
8. Parar `mcu-sim` y volver a arrancarlo. Ver qué hace `com2tcp-rfc2217`: si
   reconecta solo o termina. **Anotarlo**; si termina, relanzarlo (paso 2) y
   **ECO**.

**Desinstalar** después: en *Setup Command Prompt*, `remove 0`; y desinstalar
com0com desde *Aplicaciones*.

**Bien si:** el paso 2 negocia sin quedarse esperando (si se queda colgado sin
datos, el cliente espera que el servidor hable primero: sería un choque con
D-13, y se anota tal cual con la traza); 3, 4, 5 y 6 como se describe. **Se
anota:** Windows 10 y 11, versiones de com0com, hub4com y Tera Term, lo que
pidió el driver, y el comportamiento del paso 8.

##### M7 · HHD Free Com Port Redirector + PuTTY — Windows 11 (x64 o ARM64)

**Antes de nada, la licencia.** La edición gratuita prohíbe el uso comercial,
gubernamental y militar (§7.3). **No se hace esta prueba** hasta que HHD
confirme por escrito si una universidad pública entra en «gubernamental». Si
la respuesta es que no se puede, la fila del §10.6 se cierra con «descartado
por licencia» y la fecha de la respuesta.

**Programas:** Free Com Port Redirector de <https://freecomportredirector.com/>
(instalador x86/x64; hay versión ARM64). PuTTY como en M2.

**Pasos, si la licencia lo permite:**

1. `mcu-sim` en **modo RFC 2217**.
2. Instalar como administrador y abrir el programa. Crear un puerto en modo
   **cliente TCP** (*Serial TCP/IP client*): *host* `127.0.0.1`, *puerto*
   `3355`, protocolo **RFC 2217**. La edición gratuita no deja elegir el nombre
   del COM: **anotar el que asigne**. Tampoco lo guarda: al reiniciar Windows
   hay que volver a crearlo.
3. `putty.exe -serial COMn -sercfg 115200,8,n,1,N` con el COM asignado. **ECO**.
4. Como en M5: 9600 sin eco, 115200 con eco, y reconexión al reiniciar
   `mcu-sim`.
5. *(Opcional)* Lo mismo con protocolo **Raw** y `mcu-sim --serie VCP=tcp:3355`.

**Se anota:** la respuesta de HHD sobre la licencia, versión del programa,
Windows y arquitectura, y el resultado.

##### M8 · Opcional: capturar la sesión de un redirector

La D4 dejó pendiente **grabar byte a byte** lo que manda `com2tcp-rfc2217`
(y, por extensión, HW VSP3 y ttynvt), para añadirlo a `verif/vectores/` como se
hizo con pySerial. `captura_rfc2217.py` no sirve tal cual: en modo `--contra`
lanza él mismo el cliente de pySerial. Hace falta un **proxy que solo grabe**,
y en Windows no hay socat. Si se quiere hacer, lo razonable es añadir a
`captura_rfc2217.py` un modo `--proxy ESCUCHA DESTINO` (es la función `proxy()`
que ya tiene, sin el cliente), arrancar `mcu-sim` en el 3355, el proxy en el
3356 hacia el 3355, y apuntar el redirector al **3356**. Queda anotado aquí; no
es condición para cerrar la D6.

##### Resumen del orden y de lo que ocupa cada una

| Orden | Prueba | Plataforma | `mcu-sim` | Instala | COM |
| :--- | :--- | :--- | :--- | :--- | :--- |
| M1 | CoolTerm por TCP | Windows, macOS, Linux | crudo (y RFC 2217, opcional) | nada (una aplicación) | — |
| M2 | PuTTY *Raw* | Windows | crudo | nada | — |
| M3 | socat + picocom | macOS | crudo | Homebrew | pty `~/vcp` |
| M4 | ttynvt + picocom | Linux | RFC 2217 | compilar ttynvt, CUSE | `/dev/ttyNVT0` |
| M5 | HW VSP3 + PuTTY | Windows 10 y 11 | RFC 2217 y crudo | driver | `COM20` |
| M6 | com0com + hub4com + Tera Term | Windows 10 y 11 | RFC 2217 | driver | `COM21` ↔ `CNCB0` |
| M7 | HHD + PuTTY | Windows 11 | RFC 2217 | driver, **licencia antes** | el que asigne |
| M8 | captura de un redirector | cualquiera | RFC 2217 | — | — |

---

#### D7 · Recetas y placas — 30-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `doc/puente_serie.md` | **La receta para el alumno**: qué es, cómo arrancar con el ejemplo y con su firmware (el `.bin` de CubeIDE, el `printf` por `huart2`), cómo elegir terminal según lo que quiera (en crudo, RFC 2217 o un puerto del sistema), cada sistema por separado, una tabla de problemas por síntoma y lo que conviene saber. Cada receta dice si está **comprobada** o no, y las que no remiten a su prueba manual (M4-M6) |
| `doc/ejecutables.md` | §6 nuevo, «Ver el `printf` de tu firmware»; el antiguo §6 pasa a §7 y dice que `ejemplos/` sí va |
| `src/placas/nucleo_f446re_vcp.xml` | La Nucleo-F446RE (LD2, B1) **con el VCP del ST-LINK** en PA2/PA3, sin RTS, CTS ni DTR (el ST-LINK no los tiene), `baudios="host"`. **Aparte** de `nucleo_f446re.xml`, que usa `test446` |
| `src/top/sim_main.cpp` | `--espera-terminal` (D-15). Y la ayuda de `--serie` ya no dice «hoy solo existe memoria» |
| `src/verif/serie/interop.py` | I10 (el saludo llega con `--espera-terminal`: la Nucleo-F446RE por `rfc2217://`, el F407 por `socket://`) e I11 (lo mismo por socat con `wait-slave` y el pty abierto con pySerial). **36 comprobaciones** |
| `.github/workflows/suites.yml` | `ejemplos/` en los paquetes de Linux, macOS y Windows (D-16) |
| `src/placas/vcp_rfc2217.xml`, `vcp_tcp.xml` | Sus cabeceras con `--espera-terminal`, `wait-slave` y `127.0.0.1` |

**El criterio de cierre obligó a dos decisiones.** «Ve el `printf` de
`vcp_demo`» no se cumplía: el saludo sale a los pocos milisegundos de simular
y, sin nadie conectado, el puente lo tira (D-6), como en una placa a la que se
le abre el terminal tarde. De ahí **D-15**, `--espera-terminal`. La primera
versión arrancaba el MCU en cuanto había conexión, y **pySerial seguía sin ver
el saludo**: al abrir configura el puerto orden a orden y termina **purgando lo
recibido**, y el saludo llegaba justo antes de la purga. Por eso la espera es a
que el terminal lleve 300 ms callado. Y con socat, lo mismo por otro lado: socat
se conecta al arrancar, el MCU saludaba a un pty que no había abierto nadie, y
picocom no lo veía. `wait-slave` (socat no se conecta hasta que alguien abre el
pty) lo arregla, y va en la receta. Y **D-16**: sin placa ni firmware en el
paquete, la receta no se podía seguir sin clonar el repositorio.

**`vcp_demo` vale para la Nucleo-F446RE** sin recompilarlo: solo toca el RCC
(HSI a 16 MHz), el GPIOA y la USART2, que en el F446 están donde en el F407.
Saluda y hace eco en el modelo del F446; no se ha versionado un `vcp_demo446`
porque no aportaría nada.

**Comprobado:**

* `make interop`: 36 de 36. **I10 e I11 fallan (3 comprobaciones) si se quita
  `--espera-terminal`**, que es lo que tienen que demostrar.
* La receta de Linux (`doc/puente_serie.md` §4.1) tal cual, con socat 1.8.0.0 y
  picocom 3.1: el saludo y el eco en picocom; al cerrar picocom, socat termina;
  relanzado, el eco sigue (el saludo, que salió una vez, no vuelve).
* El paquete, simulado: el ejecutable con `ejemplos/` al lado arranca la
  Nucleo con `vcp_demo` desde esa carpeta, con las órdenes de la receta.
* `--valida` de las cuatro placas con puente (`vcp_memoria`, `vcp_tcp`, `vcp_rfc2217`, `nucleo_f446re_vcp`): sin un aviso.
* Ninguna suite se mueve: `--espera-terminal` es de `sim_main.cpp`, que no
  entra en ningún banco.

**Lo que no se ha comprobado, y la receta lo dice:** las recetas de Windows con
`COM` (HW VSP3, com0com: pruebas M5 y M6), ttynvt (M4), CoolTerm y PuTTY (M1,
M2) y socat + picocom en macOS (M3). `miniterm` no se ha usado a mano (necesita
una consola), pero es el cliente de pySerial que el CI prueba en las cuatro
plataformas. El criterio «un alumno sin experiencia sigue la receta» lo tiene
que confirmar alguien que no la haya escrito.

---

#### D8 · Modo cliente — 30-09-2026, rama `puente-uart`

**Qué se ha hecho:**

| Fichero | Qué |
| :--- | :--- |
| `src/common/serie_destino.h` | Dos modos nuevos, `tcp-cliente:HOST:PUERTO` y `rfc2217-cliente:HOST:PUERTO`, con su host (un nombre o una IPv4; IPv6 se rechaza diciéndolo). Un cliente **no escucha**, así que no choca con otros puentes ni con GDB: dos puentes de la misma placa, uno escuchando y otro conectándose a él, es un montaje válido (las UART de dos MCU por un cable virtual). `tcp:localhost:3355` sigue rechazándose, pero el mensaje dice ahora que conectarse a ese host es `tcp-cliente:` |
| `src/common/red.h` | `direccion_ipv4`, `empieza_conexion` y `estado_conexion`: conectarse **sin bloquear**, empezando en un sondeo y mirando en los siguientes. Contra otra máquina un `connect` bloqueante pararía la simulación lo que tarde TCP en rendirse |
| `src/common/telnet2217.h` | La política RFC 1143 sale a una clase `Opciones`, que usan el negociador del servidor (sin cambiar lo que hace: las 64 comprobaciones de la D4 y la sesión de pySerial reproducida, igual) y el nuevo **`NegociadorCliente`**, que ofrece COM-PORT y pide BINARY y SGA al conectarse |
| `src/parts/canal_host.h` | `CanalTcp` gana un modo cliente: se conecta, y si no puede o se cae la conexión reintenta cada segundo, avisando una vez. Y `CanalRfc2217Cliente`: el códec del lado del terminal; en cuanto el servidor acepta COM-PORT, **configura el puerto remoto con la línea simulada** (D-17); respeta el SUSPEND del servidor y guarda sus respuestas |
| `src/parts/puente_serie.h` | Los dos destinos nuevos. La pieza sigue siendo la `LineaSerie`: el cliente le pregunta la configuración |
| `src/verif/prueba_serie.cpp`, `prueba_rfc2217.cpp` | `make serie` 114 → **138** (el parseo, las formas malas y los choques de puertos de los clientes); `make rfc2217` 64 → **74** (el negociador del cliente contra el del servidor hasta que se callan, sin bucle, y contra un servidor que ofrece de más) |
| `src/verif/serie/interop.py` | **I12**: `tcp-cliente` contra un servidor que aparece dos segundos y medio después, con el saludo, el eco y la reconexión cuando el servidor cierra. **I13**: `rfc2217-cliente` contra el `PortManager` de pySerial sobre un par de ptys de socat; el saludo y el eco llegan al «terminal» del otro pty, y el puerto remoto, que el servidor abrió a 9600, queda a **115200 8N1**. **47 comprobaciones** |
| `src/top/sim_main.cpp`, `parts/netlist_parts.h`, `doc/parts.md`, `doc/puente_serie.md` | La ayuda, la ficha, la tabla de atributos y la receta (§4.4, «Al revés: el simulador se conecta a un servidor») |

**Lo que ha enseñado.** El criterio decía «contra `rfc2217_server.py` sobre un
pty de socat», y **eso no funciona, y no por el puente**: el `PortManager` de
pySerial, al activarse COM-PORT, lee CTS, DSR, RI y DCD del puerto con
`TIOCMGET`, un pty contesta `ENOTTY` y la excepción se lleva el servidor por
delante. El banco usa el mismo `PortManager` con un puerto que devuelve «falso»
en esas líneas cuando el sistema no las tiene, y la receta lo avisa. Y la
primera versión del banco no lo vio porque el hilo del servidor moría sin
decir nada: ahora un fallo del servidor es una comprobación más.

**Cómo se ha comprobado que puede fallar:** sin mandar SET-BAUDRATE, I13 lo ve
(el puerto remoto se queda a 9600); sin reintento, I12 se queda esperando al
servidor y falla. Las cuatro suites, con sus cifras de siempre (`testserie`
189 y 400677589564 ps, también con `TESTSERIE_RUIDO`), porque el modo servidor
de `CanalTcp` hace lo mismo que antes. `prueba_red`, `prueba_rfc2217` y
`prueba_serie` compilan con MinGW sin avisos.

**Lo que no se ha hecho, dicho:** el cliente no mueve DTR ni RTS del puerto
remoto (D-17), no reenvía a la línea simulada las notificaciones del servidor
(las cuenta), y la resolución de un nombre de máquina **bloquea** —no hay
`getaddrinfo` sin bloqueo portable—: si no resuelve, se reintenta cada diez
segundos en vez de cada uno. Contra un `ser2net` de verdad o `tio --socket` no
se ha probado.

---

#### Cierre de P-14 — 30-09-2026

Las nueve fases están hechas y el CI está en verde en las cuatro plataformas
(las cuatro suites con sus invariantes, `testserie` también con
`TESTSERIE_RUIDO`, y `make interop` con 47 comprobaciones). **P-14 se cierra y
la rama `puente-uart` se integra en `main`.**

**La matriz manual del §10.6 se acepta a medias.** Está comprobado lo que usará
casi todo el mundo: pySerial y socat en el CI, la receta de Linux a mano, y
CoolTerm en Windows 10 Pro 22H2 (M1 entera). Faltan M1 en macOS y Linux, y M2 a
M8, que piden Windows con drivers o un escritorio; se irán anotando en el §10.6,
fila a fila, con versión y fecha. Hasta entonces las recetas de Windows con
`COM` siguen diciendo «sin comprobar». **D-16 queda confirmada.** Lo no hecho de
cada fase está dicho en su registro y resumido en P-14 (`doc/todo.md`).

---

## Fuentes

* Código y documentación de `mcu-sim` y `mcu-sim-gui` (ramas actuales en local):
  `src/parts/ext_parts.h`, `src/parts/netlist_parts.h`, `src/common/red.h`,
  `src/common/gdb_rsp.h`, `src/periph/usart.h`, `src/top/sim_main.cpp`,
  `src/README.md`, `doc/ejecutables.md`, `mcu-sim-gui/README.md`,
  `mcu-sim-gui/doc/protocolo.md`.
* [(Un)Suspend(able), M. Burton, Accellera](https://workspace.accellera.org/document/dl/11404):
  `async_attach_suspending`, desde SystemC 2.3.2.
* [sc_prim_channel.h, SystemC 2.3](https://github.com/systemc/systemc-2.3/blob/master/src/sysc/communication/sc_prim_channel.h)
* [com0com 2.2.2.0 x64 firmado](https://sourceforge.net/projects/com0com/files/com0com/2.2.2.0/com0com-2.2.2.0-x64-fre-signed.zip/download)
* [com0com y Secure Boot, solicitud de soporte #28](https://sourceforge.net/p/com0com/support-requests/28/) ·
  [Código 52 con Secure Boot](https://sourceforge.net/p/com0com/discussion/440109/thread/cc3d9e2b97/)
* [RFC 2217, Telnet Com Port Control Option](https://www.rfc-editor.org/rfc/rfc2217.txt): códigos de orden, máscaras iniciales y obligación de confirmar cada orden con el valor aplicado (§10.4).
* RFC 854 (Telnet), 855 (opciones), 856 (BINARY) y 1143 (negociación sin bucles): la base del códec de D4.
* [pySerial 3.5](https://pyserial.readthedocs.io/): su cliente `rfc2217://` y su `PortManager` son los dos extremos de las sesiones grabadas en `verif/vectores/rfc2217_pyserial*.vec`.
* Arquitectura D (§7), consultadas el 29-09-2026:
  * SerialTool (Duolabs): [FAQ y licencia](https://serialtool.com/_en/faq) ·
    [Serial packet to network, límites de la edición FREE](https://serialtool.com/_en/serial-port-packet-to-network) ·
    [comparativa FREE/PRO](https://www.serialtool.com/_en/serial-com-port-software-compare) ·
    [reseña con precios](https://www.itechguides.com/serialtool-serial-port-tcp-udp-debugging-tool-features-limits-and-alternatives/) ·
    [repositorio](https://github.com/Duolabs/SerialTool)
  * [SerialTool de heropml](https://github.com/heropml/SerialTool) ·
    [SerialTool de HoGC (LinuxLinks)](https://www.linuxlinks.com/serialtool-serial-port-tcp-udp-debugging-tool/)
  * [hub4com ReadMe (com0com)](https://com0com.sourceforge.net/hub4com/ReadMe.txt)
  * [HW VSP3, HW group](https://www.hw-group.com/software/hw-vsp3-virtual-serial-port)
  * [HHD Free Com Port Redirector](https://freecomportredirector.com/)
  * [VSPE, Eterlogic](https://eterlogic.com/Products.VSPE.html)
  * [pySerial, ejemplos](https://pyserial.readthedocs.io/en/latest/examples.html) ·
    [tcp_serial_redirect.py](https://github.com/pyserial/pyserial/blob/master/examples/tcp_serial_redirect.py)
  * [ser2tcp](https://github.com/cortexm/ser2tcp) ·
    [ser2net en Homebrew](https://formulae.brew.sh/formula/ser2net) ·
    [gensio](https://github.com/cminyard/gensio) ·
    [gensio(5)](https://www.mankier.com/5/gensio) ·
    [gensio-binary](https://pypi.org/project/gensio-binary/)
  * [socat para Windows, compilación no oficial](https://sourceforge.net/projects/unix-utils/files/socat/1.7.3.2/)
  * [CoolTerm, ayuda](https://freeware.the-meiers.org/CoolTermHelp/)
  * [ScriptCommunicator](https://github.com/szieke/ScriptCommunicator_serial-terminal) ·
    [licencia (nixpkgs #306982)](https://github.com/NixOS/nixpkgs/issues/306982)
  * [Serial Studio, FAQ y licencia](https://serial-studio.com/help/faq)
  * [YAT](https://sourceforge.net/projects/y-a-terminal/)
  * [tio(1)](https://hexmos.com/freedevtools/man-pages/user-commands/file-management/tio/)
  * [node-red-node-serialport](https://www.npmjs.com/package/node-red-node-serialport)
  * [Lantronix CPR](https://www.lantronix.com/products/com-port-redirector/) ·
    [Tibbo VSP](https://docs.tibbo.com/soism/vspd_vsp_manager)
