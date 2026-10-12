# El puerto serie del MCU en tu ordenador

*Cómo ver en tu ordenador lo que el firmware imprime por una USART, y teclearle
cosas, como harías con una Nucleo y su puerto COM virtual. Es la receta; el
porqué de cada decisión está en `doc/analisis_puente_serie.md`.*

---

## 1. Qué es, en dos párrafos

En una Nucleo, el ST-LINK que la programa lleva además un adaptador USB-serie
conectado a la USART2 del MCU (PA2 y PA3). Por eso el `printf` de un proyecto
de STM32CubeIDE aparece en un puerto COM del PC sin cablear nada.

En `mcu-sim` eso lo hace la pieza **`PuenteSerie`**: se cuelga de los pines de
una USART y lleva los bytes a un **puerto TCP de tu propia máquina**
(`localhost:3355` por omisión). Al otro lado pones lo que prefieras: un terminal
que hable TCP, un terminal de Python, o una herramienta que convierta ese
puerto TCP en un puerto serie del sistema. `mcu-sim` no abre puertos serie del
sistema: eso lo hacen esas herramientas, y cambia según el sistema (§4).

Lo que la hace parecida a la placa: **la USART del modelo ve la línea de
verdad**. Si tu terminal va a 9600 y el firmware a 115200, el firmware recibe
basura y levanta sus errores, igual que en el laboratorio.

---

## 2. Arrancar el simulador

### 2.1 Con el ejemplo, para comprobar el montaje

Los paquetes de `mcu-sim` (`doc/ejecutables.md`) traen las placas en
`placas/` y los firmwares en `verif/fw/`, como el repositorio. Para esto hacen
falta dos placas y un firmware de prueba, `vcp_demo.bin`, que saluda al
arrancar y **devuelve cada byte que recibe**:

| Fichero | Qué es |
| :--- | :--- |
| `placas/nucleo_f446re_vcp.xml` | Una Nucleo-F446RE con el VCP del ST-LINK en PA2/PA3. **Empieza por esta** |
| `placas/nucleo_f446re.xml` | La Nucleo-F446RE de siempre, que también lleva ya el VCP del ST-LINK, el mismo, con sus LEDs, sus pulsadores y sus conectores |
| `placas/vcp_rfc2217.xml` | Un F407 con el puente en la USART2, más RTS/CTS y DTR |
| `verif/fw/vcp_demo/vcp_demo.bin` | El firmware de prueba (vale para las dos placas) |

Desde la carpeta donde descomprimiste el paquete:

```bash
# Linux y macOS
./mcu-sim placas/nucleo_f446re_vcp.xml verif/fw/vcp_demo/vcp_demo.bin --tiempo-real --espera-terminal

# Windows (cmd o PowerShell)
mcu-sim.exe placas\nucleo_f446re_vcp.xml verif\fw\vcp_demo\vcp_demo.bin --tiempo-real --espera-terminal
```

(Si trabajas con el repositorio, es lo mismo desde `src/`, con
`./build/mcu-sim`; o desde la carpeta de compilación, si compilas con `B=`,
que lleva las mismas carpetas.)

Tiene que decir:

```
  serie VCP: RFC 2217 en localhost:3355, 115200 8N1
esperando a un terminal en RFC 2217 en localhost:3355 (Ctrl-C para salir)
```

y quedarse ahí. **Ahora abre el terminal** (§3). En cuanto se conecte:

```
terminal conectado: arranca el MCU
...
  [VCP] vcp_demo listo
```

y en el terminal aparece `vcp_demo listo`. Lo que teclees vuelve. Se sale con
**Ctrl-C** en la ventana de `mcu-sim`.

**Las dos opciones, y por qué van siempre:**

* `--tiempo-real` hace que la simulación vaya al ritmo del reloj. Sin ella va
  todo lo deprisa que puede, el procesador se pone al 100 % y un `printf` por
  segundo llega cien veces por segundo.
* `--espera-terminal` no deja arrancar el MCU hasta que el terminal está
  conectado. Sin ella, lo primero que imprime el firmware —el saludo— sale
  cuando todavía no hay nadie escuchando, y se pierde. Es lo mismo que en la
  placa si abres el terminal después de pulsar RESET.

La consola de `mcu-sim` también enseña, con `[VCP]` delante, **cada línea** que
manda el MCU. Si el terminal falla, mira ahí primero: si ahí sale, el problema
está del lado del terminal.

### 2.2 Con tu firmware

Igual, cambiando el `.bin`:

```bash
./mcu-sim placas/nucleo_f446re_vcp.xml MiProyecto.bin --tiempo-real --espera-terminal
```

* **El `.bin`**: en STM32CubeIDE, *Project → Properties → C/C++ Build →
  Settings → Tool Settings → MCU Post build outputs → Convert to binary file
  (-O binary)*. Sale
  junto al `.elf`, en `Debug/`.
* **El `printf` por la UART**: lo de siempre en un proyecto de CubeIDE para
  Nucleo, redirigir `__io_putchar` (o `_write`) a
  `HAL_UART_Transmit(&huart2, …)`. Si en la placa sale por el VCP, aquí también.
* **Otra USART u otros pines**: copia la placa y cambia los nodos de `rx` (el
  TX del MCU) y `tx` (el RX del MCU). `./mcu-sim --help PuenteSerie` explica
  todos los atributos.

La pieza en el XML, por si montas tu propia placa:

```xml
<componente tipo="PuenteSerie" id="VCP" host="rfc2217:3355"
            baudios="host" formato="8N1" muestra="si">
  <pin nombre="rx" nodo="PA2"/>   <!-- lee el TX del MCU -->
  <pin nombre="tx" nodo="PA3"/>   <!-- gobierna el RX del MCU -->
</componente>
```

---

## 3. Elegir el terminal

Primero decide **qué quieres**, porque cambia el modo del puente:

| Quiero… | Modo del puente | Qué uso |
| :--- | :--- | :--- |
| Ver el `printf` y teclear, sin instalar casi nada | **en crudo**: `--serie VCP=tcp:3355` | CoolTerm, PuTTY, `nc` (§3.1) |
| Además, que los **baudios del terminal** lleguen al MCU | **RFC 2217** (el de las placas de ejemplo) | `miniterm` de Python (§3.2) |
| Un **puerto serie del sistema** (`/dev/…`, `COMn`) para un programa que solo sabe abrir puertos | en crudo o RFC 2217, según la herramienta | §4 |

**En crudo** los bytes del socket son los de la línea y nada más: cualquier
programa que hable TCP sirve, pero los baudios que pongas en él no llegan a
ninguna parte. **Con RFC 2217** (Telnet con una opción para puertos serie), el
terminal puede fijar baudios, paridad, bits de parada y control de flujo de la
línea simulada, mover DTR y RTS y mandar un *break*. Los dos modos no se
mezclan: el puerto es de uno o de otro. `--serie VCP=…` cambia el de la placa
sin editarla.

### 3.1 Un terminal que hable TCP (sin puerto serie)

Arranca `mcu-sim` en crudo:

```bash
./mcu-sim placas/nucleo_f446re_vcp.xml verif/fw/vcp_demo/vcp_demo.bin --tiempo-real --espera-terminal --serie VCP=tcp:3355
```

**CoolTerm** (Windows, macOS y Linux; gratuito, <https://freeware.the-meiers.org/>).
Es la opción recomendada para la mayoría, y está **comprobada en Windows 10 Pro
22H2 de 64 bits con CoolTerm 2.4.0** (eco, reconexión y reinicio del simulador;
también contra el modo RFC 2217):

1. *Connection → Options…*: *Port* **TCP Connection**, *Mode* **Client**, *IP
   Address* `127.0.0.1`, *Port* `3355`.
2. *Terminal*: *Enter Key Emulation* **CR+LF**; *Local Echo* **desmarcado** (el
   eco lo hace el firmware).
3. *Connect*.

**PuTTY** (Windows; en Linux, paquete `putty`):

```
putty.exe -raw -P 3355 127.0.0.1
```

y después, en el menú de la ventana (*Change Settings… → Terminal*), *Local
echo* y *Local line editing* en **Force off** → *Apply*. Si no, PuTTY pinta lo
que tecleas y además el eco: todo sale doble.

**`nc`** (Linux y macOS), para mirar sin más:

```bash
nc 127.0.0.1 3355
```

Manda la línea al pulsar Intro y la pinta él, así que verás lo que tecleas dos
veces. Para teclear, mejor cualquiera de los anteriores.

### 3.2 `miniterm` de Python, con los baudios de verdad (RFC 2217)

Funciona igual en los tres sistemas y es el cliente con el que se prueba el
puente en cada cambio del proyecto. Necesita Python 3 y pySerial:

```bash
python3 -m pip install pyserial            # Windows: py -m pip install pyserial
```

Arranca `mcu-sim` con la placa tal cual (ya es RFC 2217) y:

```bash
python3 -m serial.tools.miniterm rfc2217://127.0.0.1:3355 115200
# Windows: py -m serial.tools.miniterm rfc2217://127.0.0.1:3355 115200
```

* **Ctrl-T B** cambia los baudios en caliente. Pon 9600: lo que teclees **ya
  no vuelve** (el firmware sigue a 115200 y las tramas le llegan rotas). Vuelve
  a 115200 y todo funciona. Es exactamente lo que pasa en la placa.
* **Ctrl-T Ctrl-H** enseña el resto de teclas; **Ctrl-]** sale.
* Intro manda CR+LF, y miniterm no pinta lo que tecleas: el eco que ves es el
  del firmware.

---

## 4. Un puerto serie del sistema

Hace falta cuando el programa que quieres usar solo sabe abrir puertos serie:
un terminal gráfico concreto, un script que abre `/dev/ttyUSB0` o `COM3`, la
herramienta de una asignatura.

### 4.1 Linux y macOS: socat

```bash
# Linux: sudo apt install socat picocom
# macOS: brew install socat picocom
```

1. `mcu-sim` **en crudo** y con `--espera-terminal`:

   ```bash
   ./mcu-sim placas/nucleo_f446re_vcp.xml verif/fw/vcp_demo/vcp_demo.bin --tiempo-real --espera-terminal --serie VCP=tcp:3355
   ```

2. En otra ventana, el puerto:

   ```bash
   socat -d -d pty,link=$HOME/vcp,raw,echo=0,wait-slave tcp:127.0.0.1:3355
   ```

   Crea `~/vcp`, un puerto serie de verdad para cualquier programa.
   **`wait-slave` importa**: socat no se conecta al simulador hasta que alguien
   abre el puerto, así que el MCU no arranca hasta que abras el terminal y el
   saludo no se pierde. Y **`127.0.0.1` mejor que `localhost`**: el puente solo
   escucha en IPv4.

3. En una tercera, el terminal:

   ```bash
   picocom -b 115200 --omap crcrlf ~/vcp      # salir: Ctrl-A Ctrl-X
   # o bien: screen ~/vcp 115200               # salir: Ctrl-A k
   ```

**Tres cosas que saber:**

* Con `wait-slave`, socat **termina cuando cierras el terminal**. Para volver a
  abrir, relanza el paso 2 (el simulador sigue corriendo y no hace falta
  reiniciarlo; eso sí, ya no volverás a ver el saludo, que salió una vez).
* Los baudios que pongas en picocom **no llegan al MCU**: el pty no los
  transmite. Si quieres eso en Linux, §4.3.
* En macOS, `~/vcp` apunta a un `/dev/ttys…`, y las aplicaciones gráficas que
  solo enseñan `/dev/cu.*` no lo verán en su lista. Si te deja escribir la
  ruta a mano, sirve.

### 4.2 Windows: un `COMn` necesita un driver

En Windows no hay forma de tener un `COM` que no sea físico sin instalar un
driver, y eso ningún programa gratuito lo evita. Si solo quieres ver y
teclear, **usa §3.1 o §3.2**, que no instalan nada.

Si de verdad necesitas un `COM`, hay dos caminos gratuitos. **Todavía no están
comprobados en este proyecto**: el guion para comprobarlos, con cada opción y
cada orden, está en `doc/analisis_puente_serie.md`, §10.8, pruebas **M5** y
**M6**. Resumidos:

* **HW VSP3 Single** (HW group, freeware). Crea un `COMn` que se conecta él
  solo a `127.0.0.1:3355`. En *Settings*, **NVT Enable** marcado para RFC 2217
  (la placa tal cual) o desmarcado para el modo en crudo. En *Virtual Serial
  Port*, *Port Name* `COM20`, *IP Address* `127.0.0.1`, *Port* `3355` → *Create
  COM*. Después, tu terminal en `COM20`.
* **com0com + hub4com** (libre). Un par de puertos virtuales (`COM21` ↔
  `CNCB0`) y `com2tcp-rfc2217.bat \\.\CNCB0 127.0.0.1 3355` uniendo uno de ellos
  al simulador. Hay que instalar **com0com 2.2.2.0 firmado**, no la 3.0.

Con los dos, arranca `mcu-sim` primero, luego el redirector, y el terminal el
último.

### 4.3 Linux, con los baudios de verdad: ttynvt

`ttynvt` crea un `/dev/ttyNVT0` que habla RFC 2217, así que `stty` y picocom sí
cambian los baudios de la línea simulada. Necesita compilarlo y un núcleo con
CUSE, y **tampoco está comprobado todavía**: prueba **M4** del mismo guion.

### 4.4 Al revés: el simulador se conecta a un servidor

Todo lo anterior deja al simulador **escuchando** y a la herramienta
conectándose. Algunas herramientas solo saben hacer lo contrario —escuchar—:
`ser2net` o `rfc2217_server.py` compartiendo un puerto serie de verdad,
`tio --socket`, `ser2tcp`, o un programa tuyo. Para esas, el puente se conecta
él:

```bash
# En crudo
./mcu-sim placa.xml fw.bin --tiempo-real --espera-terminal --serie VCP=tcp-cliente:127.0.0.1:2000
# Con RFC 2217
./mcu-sim placa.xml fw.bin --tiempo-real --espera-terminal --serie VCP=rfc2217-cliente:127.0.0.1:2217
```

* **Si el servidor no está todavía, o se cae**, `mcu-sim` lo dice una vez
  (`... no contesta; se reintenta cada segundo`) y se reconecta solo cuando
  aparece. Puedes arrancar las dos cosas en el orden que quieras.
* **Con `rfc2217-cliente` es el simulador quien configura el puerto remoto**:
  le manda los baudios, el formato y el control de flujo de la placa. Con
  `baudios="host"` no hay terminal que los elija, así que manda 115200.
* **Puede ser otra máquina** (`rfc2217-cliente:ser2net.lab:2001`), porque es
  el simulador el que sale; el puerto que escucha el simulador sigue siendo solo
  de la tuya. Mejor con una IP: si un nombre no se resuelve, se reintenta cada
  diez segundos y cada intento para la simulación lo que tarde el DNS.
* **`rfc2217_server.py` de pySerial no sirve tal cual sobre un pty de
  socat**: su `PortManager`, al activarse RFC 2217, lee CTS y DSR del puerto;
  un pty no los tiene y se cae (comprobado). Con un puerto de verdad, que sí
  tiene esas líneas, no debería pasar.

Con eso se pueden unir **las UART de dos simuladores**: uno con `tcp:4000` y el
otro con `tcp-cliente:127.0.0.1:4000`.

---

## 5. Problemas, por síntoma

| Lo que ves | Qué pasa | Qué hacer |
| :--- | :--- | :--- |
| `mcu-sim` dice `no se puede escuchar en localhost:3355` | El puerto lo tiene otro programa: otro `mcu-sim` que sigue abierto, un GDB | Cierra el otro, o usa otro puerto: `--serie VCP=rfc2217:3356` (y el mismo en el terminal) |
| El terminal no conecta | `mcu-sim` no está arrancado, o está en otro puerto | Arranca `mcu-sim` **antes** que el terminal y mira el puerto que dice en `serie VCP: …` |
| `... no contesta; se reintenta cada segundo` | Con `tcp-cliente` o `rfc2217-cliente`: el servidor al que se conecta el simulador no está, o no en ese puerto | Arráncalo; el simulador se conecta solo. Si no, revisa `HOST:PUERTO` |
| Conecta, pero no sale el saludo | Falta `--espera-terminal`, o socat sin `wait-slave` | §2.1 y §4.1. El saludo solo sale una vez: si ya salió, teclea algo para ver el eco |
| Todo sale **dos veces** | El terminal tiene el eco local encendido | Apágalo: *Local Echo* en CoolTerm, *Force off* en PuTTY |
| Lo que tecleo **no vuelve** | Los baudios o la paridad del terminal no son los del firmware (con RFC 2217 llegan a la línea) | Pon **115200 8N1**, que es lo que usa `vcp_demo`. Con tu firmware, lo que programe su `huart` |
| Sale basura | Lo mismo, visto desde el otro lado: el firmware imprime y el terminal lee a otra velocidad | Igual |
| Python dice `remote rejected value for option 'baudrate'` | La placa tiene los baudios **fijos** en el XML (`baudios="115200"`), y el puente no deja que el terminal los cambie | Abre con los baudios del XML, o pon `baudios="host"` en la placa |
| Python dice `remote rejected value for option 'control'` al abrir | La placa fija `flujo="rtscts"` en el XML y pySerial abre pidiendo «sin control de flujo» | Abre con `rtscts=True` (en miniterm, `--rtscts`), o quita el `flujo` del XML |
| Con CoolTerm, PuTTY o `nc`, un byte `0xFF` llega mal | El puente está en RFC 2217 y el terminal no habla Telnet | Usa el modo en crudo, `--serie VCP=tcp:3355` |
| La consola de `mcu-sim` no enseña lo que tecleo | Solo imprime al llegar un salto de línea, y tu terminal manda solo CR | Configura Intro como **CR+LF**. En el terminal se ve igual |
| El procesador al 100 % y los mensajes a toda velocidad | Falta `--tiempo-real` | Añádelo |
| No termina nunca | Es lo previsto: con un puente por red, `mcu-sim` espera a su terminal, como con un GDB | Ctrl-C |

---

## 6. Lo que conviene saber

* **Solo desde tu máquina.** El puente escucha en `127.0.0.1`: nadie de la red
  puede conectarse a tu simulador.
* **Un terminal a la vez.** Si se conecta otro, sustituye al anterior (que se
  queda desconectado). Es a propósito: los redirectores se reconectan solos, y
  a veces el viejo aún no se ha ido.
* **Sin terminal, lo que imprime el MCU se tira.** El simulador no se para
  nunca a esperar a que alguien lea, salvo al arrancar con `--espera-terminal`.
* **`baudios="host"` o un número.** Con `host`, el terminal manda en baudios,
  formato y control de flujo, como en un adaptador USB-serie. Con un número, lo
  fija la placa y el terminal no puede cambiarlo (lo intenta y se le contesta
  con lo que hay). DTR, RTS y el *break* los mueve siempre el terminal.
* Todo lo que la pieza admite: `./mcu-sim --help PuenteSerie`.
