# Puentes Wi-Fi y Bluetooth

**Análisis de opciones para `mcu-sim`** · 04-11-2026

> **Estado: solo análisis.** No hay nada hecho. Ningún MCU modelado hoy tiene
> radio: los STM32F4 y F3 y los dos MSP432 no la llevan. El ESP32-C3 de
> `doc/analisis_esp32_riscv.md` la declara sin modelarla.

La pregunta tiene dos partes:

* **¿Cómo se haría una pieza «puente Wi-Fi» o «puente Bluetooth»?** Igual que
  el puente serie (`doc/analisis_puente_serie.md`, P-14), el de Ethernet
  (`doc/analisis_puente_ethernet.md`) y el de USB
  (`doc/analisis_puente_usb.md`), sería un elemento **externo al MCU** que
  lleva lo que el firmware manda por radio al ordenador donde corre
  `mcu-sim`, y lo que llega del ordenador de vuelta.
* **¿Qué posibilidades hay de que un MCU con Wi-Fi o con Bluetooth se
  comunique por esos canales?** Para el Wi-Fi, como estación (cliente) o como
  punto de acceso (AP), e incluso **cambiando de uno a otro durante la
  simulación**. La comunicación puede ser con otro MCU simulado, con
  programas del ordenador o con aparatos de verdad, como el móvil del alumno.

Y, como en los otros tres, qué cambia en cada plataforma que el proyecto
soporta: Linux x86-64, Windows x86-64 con MinGW, y macOS arm64 y x86-64.

---

## 0. Resumen

* **Una radio no es un cable.** Los otros tres puentes tienen un extremo en el
  MCU y otro en el ordenador. Una radio es un **medio compartido**: hay redes
  que se ven o no, estaciones que se asocian, anuncios que oye quien esté
  cerca y modos que cambian en marcha. Por eso todos los caminos de abajo
  necesitan lo mismo en el centro: **un «aire» simulado** (`MedioRadio`, §2),
  donde están las redes, los AP, las estaciones y los periféricos BLE de la
  simulación. Los puentes al ordenador son un participante más de ese aire.
* **Hay tres sitios donde cortar** (§1.1), y la viabilidad depende de cuál:

  | Corte | Qué ve el firmware | Ejemplos | Qué hay que modelar | Viabilidad |
  | :--- | :--- | :--- | :--- | :--- |
  | **A · El módulo** | Una UART con comandos AT | ESP-01 con ESP-AT, HC-05, HC-06, HM-10 | El intérprete de comandos y los sockets del módulo | **Alta**. Casi todo existe ya |
  | **B · El procesador de red o el controlador** | Un protocolo de mensajes por SPI, UART o un buzón | CC3100 / CC3120, CYW43439 (Pico W), BlueNRG, STM32WB, CC2564 por HCI | El protocolo de mensajes y, detrás, un controlador virtual | **Media**. Depende de que el protocolo esté documentado o tenga un *driver* abierto |
  | **C · La radio del chip** | Registros de la radio | ESP32 (Wi-Fi y BLE), nRF52 (BLE) | El MAC y la radio, trama a trama | **Baja** en el ESP32 (registros sin documentar y bibliotecas binarias). **Razonable** en el nRF52 (la radio está documentada) |

* **El Wi-Fi como cliente y como AP, y el cambio de modo en marcha**, se
  consigue entero en el **corte A** con un módulo ESP-AT (§3.1).
  * `AT+CWMODE=1|2|3`, `AT+CWJAP` y `AT+CWSAP` son órdenes del firmware al
    módulo, y el modelo las obedece cuando llegan.
  * Como el módulo lleva dentro su pila TCP/IP, **sus sockets son sockets del
    ordenador**. Un `AT+CIPSTART="TCP",…` en una red que «sale al ordenador»
    es un `connect()` de verdad.
  * Un `AT+CIPSERVER` en modo AP se ve desde el navegador del alumno por un
    puerto **reenviado**.
  * No hace falta ninguna pila TCP/IP en `mcu-sim`, ni gvproxy, ni
    privilegios, y funciona igual en las tres plataformas.
* **Con el Bluetooth, en el corte A, el canal es una línea serie.**
  * El HC-05 (Bluetooth clásico, SPP) y el HM-10 (BLE con su característica
    FFE1) son un puerto serie transparente.
  * El otro extremo puede ser otro módulo simulado, un terminal por TCP o
    RFC 2217 con los canales del puente serie, o un **ayudante fuera de
    `mcu-sim`** que lo publica por el Bluetooth de verdad del ordenador. Con
    ese ayudante, el móvil del alumno se conecta al PC como si fuera el
    módulo (§4.1, §4.2).
* **Lo que no es portátil:** que el ordenador **se asocie** al AP simulado
  como a una Wi-Fi más, o que su pila Bluetooth vea al MCU como un adaptador
  más.
  * En Linux sí se puede: `mac80211_hwsim` para el Wi-Fi, y `/dev/vhci` con
    un controlador virtual para el Bluetooth. Ambos piden privilegios.
  * En Windows y macOS no hay nada equivalente (§5). Es la misma frontera que
    el TAP del puente Ethernet.
* **El MCU con la radio integrada** (corte C) es lo que de verdad se pregunta
  para el ESP32, y aquí la respuesta honesta es que **modelar su radio no es
  realista**.
  * Los registros de la radio no están documentados y ESP-IDF la maneja con
    bibliotecas binarias.
  * El QEMU de Espressif tampoco la tiene: ofrece una Ethernet que no existe
    en el chip, la de OpenCores.
  * El *fork* de QEMU de PICSimLab sí la tiene, por ingeniería inversa. Pero
    solo ofrece dos redes abiertas fijas y solo funciona con IDF 4.4.
  * El camino serio es **paravirtual** (§3.3): un dispositivo inventado y
    documentado por nosotros, y un componente de ESP-IDF que lo usa debajo de
    `esp_wifi`. El programa del alumno no cambia, pero hay que compilarlo con
    otra opción.
  * El nRF52, en cambio, tiene una radio documentada a nivel de registro, y es
    el único MCU con radio integrada cuyo modelo **fiel** es posible. Lo
    demuestran los modelos del nRF52 de BabbleSim, de Nordic, que se usan con
    Zephyr.
* **Recomendación (§7):**
  1. Primero el `MedioRadio` y la pieza **`ModuloWifiAt`** (ESP-AT).
  2. Después `Hc05` y `Hm10`.
  3. Después los ayudantes por el Bluetooth del ordenador.
  4. Después el CC3100 en la LaunchPad del MSP432, porque ya hay
     BoosterPacks en el catálogo.
  5. Y solo después, con un MCU con radio en marcha (ESP32-C3 o nRF52), el
     corte C.

---

## 1. En qué se diferencia de los puentes con cable

### 1.1 Los tres cortes

En un producto con radio siempre hay una frontera entre **lo que programa el
alumno** y **lo que hace el fabricante de la radio**. El corte del modelo debe
caer en esa frontera, porque es lo que el firmware del alumno ve y lo que está
documentado.

* **Corte A: un módulo con comandos AT.** La radio, su pila y a menudo la pila
  TCP/IP entera están dentro de un módulo. El MCU le habla por una UART con
  órdenes de texto. Es lo habitual en clase con STM32, MSP432 y Arduino: un
  ESP-01 con el firmware ESP-AT, un HC-05, un HM-10. **El firmware del alumno
  solo ve la UART**, así que el modelo solo tiene que contestar como el módulo.
  El aire y la red quedan detrás, dentro de la pieza.
* **Corte B: un procesador de red o un controlador por un protocolo de
  mensajes.** Ejemplos:
  * El CC3100 de TI: un procesador de red Wi-Fi con TCP/IP dentro, por SPI o
    UART.
  * El CYW43439 de la Pico W: un chip Wi-Fi que entrega tramas Ethernet por un
    SPI propio.
  * El BlueNRG de ST.
  * El coprocesador del STM32WB, por un buzón (IPCC) y memoria compartida.
  * Los controladores Bluetooth con interfaz **HCI**, la frontera que define
    la propia especificación de Bluetooth.

  Aquí el firmware del alumno lleva el *driver* del fabricante, y el modelo
  tiene que hablar el protocolo de mensajes de ese *driver*.
* **Corte C: la radio del chip.** El firmware escribe registros de la radio
  —la frecuencia, el puntero al paquete, «transmite»— y espera eventos. El
  modelo tiene que ser el MAC y la radio, trama a trama, con el tiempo de aire
  calculado. Es lo que haría falta para el ESP32 o el nRF52.

### 1.2 Lo que ya hay en `mcu-sim` y se aprovecha

| Qué | Dónde | Para qué aquí |
| :--- | :--- | :--- |
| El receptor y el emisor UART de placa, con sus errores de trama | `parts/motor_uart.h` (`ReceptorUart`, `EmisorUart`) | El lado MCU de todas las piezas del corte A: una pieza con `rx`/`tx`, como el `PuenteSerie` |
| Los canales hacia el ordenador, que no bloquean nunca | `parts/canal_host.h` (`CanalMemoria`, `CanalTcp`, `CanalRfc2217` y sus clientes) | El otro extremo de un SPP o de un HM-10: un terminal por TCP o RFC 2217 |
| Los sockets, en un solo fichero que sabe de sistemas operativos | `common/red.h` | Los sockets de un módulo ESP-AT: `connect`, `listen` y UDP |
| `guion=` y `memoria` | `PuenteSerie` | Las pruebas: lo que «escribe el móvil», con su tiempo |
| Las tramas por TCP con el protocolo de QEMU, y gvproxy | `parts/puente_ethernet.h` | El enlace de salida de un AP virtual en los cortes B (CYW43439) y C (§3.3) |
| La conexión a un periférico del MCU, no a sus pines | `eth=` y `usb=` en el `<mcu>` (`doc/parts.md` §2.0) | El corte C, donde el puente engancha la radio del chip, como el de Ethernet engancha el PHY interno del E4 |
| Las BoosterPacks | `parts/boosterpack.h`, `placas/boostxl_edumkii.xml` | El CC3100BOOST encima de una LaunchPad (§3.2) |
| Varias placas en un `<sistema>`, con posición en el dibujo | `doc/parts.md` §2.5 y §2.6 | Varias placas con radio en el mismo aire; la distancia, si se quiere (§2) |
| La memoria no volátil, con `contenido` | `common/memoria_nv.h`, `doc/parts.md` §2.7 | Lo que el módulo recuerda: la última red de `AT+CWJAP` con `AT+SYSSTORE=1`, el nombre y el PIN del HC-05 |
| `--tiempo-real` | `top/sim_main.cpp` | Obligatorio con el ordenador al otro lado, como en el puente serie por TCP, que ya avisa si falta |

### 1.3 Qué radios hay, y qué se sabe de cada una

| Chip o módulo | Radio | Cómo le habla el MCU | Documentación | Corte |
| :--- | :--- | :--- | :--- | :--- |
| **ESP-01 / ESP-01S** (ESP8266) y módulos ESP32-C3 con **ESP-AT** | Wi-Fi 4 | UART, AT | El juego de órdenes de ESP-AT está publicado, con ejemplos | **A** |
| **HC-05** / **HC-06** | BT clásico, SPP | UART, AT en modo órdenes; transparente conectado | Hojas de clones; las órdenes cambian según la versión del firmware | **A** |
| **HM-10** (CC2541) y clones (JDY-08, AT-09…) | BLE 4.x | UART, AT; datos por la característica FFE1 | Hoja del fabricante; los clones difieren | **A** |
| **CC3100 / CC3120** (CC3100BOOST) | Wi-Fi 4 con TCP/IP y TLS dentro | SPI o UART, protocolo SimpleLink | **El *driver* del anfitrión es código fuente de TI**: el protocolo se lee en él | **B** |
| **CYW43439** (Raspberry Pi Pico W) | Wi-Fi 4 y BT | SPI propio; firmware binario que carga el anfitrión | Hoja de Infineon para el SPI; el resto, en el `cyw43-driver` abierto | **B** |
| **BlueNRG-M0 / -2** (X-NUCLEO-IDB05A2…) | BLE | SPI, ACI y HCI | Documentación de ST | **B** |
| **STM32WB55** | BLE, 802.15.4 | Buzón IPCC y SRAM compartida; el núcleo M0+ ejecuta una pila binaria de ST | Las órdenes son HCI y ACI, y el transporte está en el código de STM32CubeWB | **B** |
| **ESP32 / ESP32-C3** | Wi-Fi 4, BLE 5 | Registros sin documentar; bibliotecas binarias (`libnet80211`, `libpp`, `libphy`, el controlador BT) | **No hay** | **C** (ver §3.3) |
| **nRF52832 / nRF52840** | BLE 5, 802.15.4 | El periférico `RADIO` con EasyDMA | **Documentado a nivel de registro**; modelos abiertos de Nordic en BabbleSim | **C** |
| **CC3220** | Wi-Fi 4 | Un núcleo M4 de aplicación y un procesador de red interno | El mismo SimpleLink del CC3100; **hay que comprobar** que por dentro es el mismo protocolo | **B** |

Dos observaciones:

* **El Cortex-M4F del nRF52 es el núcleo que ya tiene `mcu-sim`.** Un nRF52
  sería «otra familia» como lo fueron el MSP432 y el F303: el mismo núcleo,
  otros periféricos.
* **El CC3100 encaja con el MSP432 que ya está modelado.** Las notas de su SDK
  dan como plataformas de referencia las LaunchPad de MSP430 y Tiva, y la
  STM32F4 Discovery, con SPI a 20 MHz o UART a 3 MHz. El MSP432 no aparece en
  ellas, aunque la BoosterPack encaja en su LaunchPad. **Hay que comprobar**
  qué SDK de TI la soporta en el MSP432.

---

## 2. El aire: un medio radio dentro de la simulación

Cualquiera de los tres cortes necesita saber **quién oye a quién**: qué redes
ve un `AT+CWLAP`, a qué AP se asocia una estación, qué periféricos BLE
aparecen al escanear y quién recibe una trama. Eso no es de ninguna pieza, es
del entorno. Se propone un objeto de la simulación, **`MedioRadio`**, uno por
banda (2,4 GHz; el de 5 GHz cuando haga falta), con:

* **Los participantes:** AP, estaciones, periféricos y centrales BLE, módulos
  SPP y los puentes al ordenador. Cada uno con su dirección (MAC o BD_ADDR),
  su canal y, si se quiere, su posición.
* **Lo que se anuncia:** los *beacons* de cada AP (SSID, seguridad, canal) y
  los anuncios BLE (nombre, UUID de servicios), con su periodo. En los cortes
  A y B basta con una **lista viva** que se consulta. En el C son tramas de
  verdad en el aire, cada una con su instante.
* **La entrega:** en A y B, mensajes ya hechos (una conexión, unos bytes). En
  C, tramas con su **tiempo de aire** calculado —preámbulo, cabecera y datos a
  la velocidad del modo—, como la frontera de tramas del Ethernet (E3 de su
  análisis). Así no se simula bit a bit, y el coste es un suceso por trama.
* **El alcance (opcional):** por omisión todos oyen a todos. Con posición,
  una pérdida por distancia simple que da el RSSI que contestan `AT+CWLAP` y
  los escaneos BLE, y un umbral por debajo del cual no hay enlace.

  La posición puede salir del dibujo del sistema (`x`, `y` de cada `<placa
  id>`, `doc/parts.md` §2.6), pero esas son coordenadas de lienzo en mm.
  Conviene un atributo propio en metros, `pos="x,y"`. Las colisiones y las
  interferencias no hacen falta para enseñar, y se dejan fuera.
* **El determinismo:** dentro de un proceso, el medio es un objeto SystemC
  más, con sus sucesos en tiempo simulado. Dos simulaciones iguales dan lo
  mismo al picosegundo, y las pruebas pueden tener su invariante, como
  `testserie`.

**¿Y entre dos `mcu-sim`?** Con Ethernet es fácil: un cable cruzado por TCP. La
radio necesita un **servidor de aire** al que se conecten todos, que es lo que
hacen `vwifi-server` (tramas 802.11 entre máquinas virtuales Linux con
`mac80211_hwsim`, por VSOCK o TCP) y el proceso de PHY de BabbleSim (BLE y
802.15.4 entre dispositivos simulados, en sincronía estricta). Hay dos
niveles:

* **Cortes A y B, en tiempo real:** dos `mcu-sim` con `--tiempo-real` y sus
  módulos en la «misma red» se hablan por sockets del ordenador (§3.1), sin
  servidor de aire. Basta con que la red sea la misma y que los puertos estén
  reenviados.
* **Corte C, con sincronía:** BLE tiene plazos de microsegundos (T_IFS, el
  hueco entre tramas, es de 150 µs). Dos procesos solo los cumplen si avanzan
  a la par, como BabbleSim. Es una simulación distribuida con su protocolo de
  tiempo, grande y fuera de este análisis. **Se recomienda que el corte C
  empiece con todas las radios en el mismo proceso**: un `<sistema>` con dos
  placas.

---

## 3. Wi-Fi

### 3.1 Corte A: un módulo ESP-AT (`ModuloWifiAt`)

Es la que más se usa en clase con un STM32 o un MSP432, y la que resuelve
entera la pregunta del modo cliente, del modo AP y del cambio de modo en
marcha.

**La pieza.**
* **Por el lado MCU**, los pines del ESP-01: `rx` y `tx`, con el motor UART
  del `PuenteSerie`; `en` (CH_PD) y `rst`, que si están bajos lo apagan; y
  `gpio0`, que bajo en el arranque es el modo de descarga (fuera de alcance).
* **Por dentro**, un intérprete de ESP-AT con su estado:
  * el modo: estación, AP o los dos (`AT+CWMODE=1|2|3`);
  * la red a la que está asociado;
  * su AP, si lo tiene;
  * las conexiones abiertas, con su id (`AT+CIPMUX`);
  * el modo transparente (`AT+CIPMODE`).
* Las órdenes y respuestas básicas, según los ejemplos de Espressif:

  | Qué | Orden | Lo que contesta o avisa |
  | :--- | :--- | :--- |
  | Modo | `AT+CWMODE=1` (estación), `2` (AP), `3` (los dos) | `OK` |
  | Buscar redes | `AT+CWLAP` | `+CWLAP:(…)` por cada red del aire, con su RSSI |
  | Asociarse | `AT+CWJAP="ssid","clave"` | `WIFI CONNECTED`, `WIFI GOT IP`, `OK`; o `+CWJAP:<error>` y `FAIL` |
  | Ser AP | `AT+CWSAP="ssid","clave",canal,cifrado` | `OK`, y el AP aparece en el aire |
  | Conectar | `AT+CIPSTART="TCP","192.168.3.102",8080` (también `"UDP"`) | `CONNECT`, `OK` |
  | Servir | `AT+CIPMUX=1` y `AT+CIPSERVER=1[,puerto]` (333 por omisión) | `0,CONNECT` al llegar un cliente |
  | Mandar | `AT+CIPSEND=[id,]n` y los `n` bytes | `>`, `SEND OK` |
  | Recibir | — | `+IPD,n:datos` o `+IPD,id,n:datos` |
  | Transparente | `AT+CIPMODE=1`, `AT+CIPSEND`, y `+++` para salir (con un segundo de silencio después) | — |

  Cada respuesta llega con un **retardo en tiempo simulado**, configurable:
  asociarse cuesta segundos y una conexión, milisegundos. Así el firmware del
  alumno se enfrenta a lo mismo que con el módulo. Un `AT+CWJAP` que no espera
  el `WIFI GOT IP` falla aquí igual que en la placa.

**Las redes son piezas.** Una red no es del módulo, es del entorno. Se propone
una pieza sin pines, `PuntoAcceso`, que vive en una placa o directamente en el
sistema:

```xml
<!-- El router del aula: sale al ordenador -->
<componente tipo="PuntoAcceso" id="AP1" ssid="aula" clave="12345678"
            seguridad="wpa2" canal="6" salida="ordenador"
            subred="192.168.4.0/24"/>

<!-- El ESP-01 de la placa -->
<componente tipo="ModuloWifiAt" id="U3" firmware="esp-at-2"
            reenvia="8080:80" contenido="u3_nvs.hex">
  <pin nombre="rx" nodo="PA2"/>   <!-- USART2_TX del MCU -->
  <pin nombre="tx" nodo="PA3"/>   <!-- USART2_RX del MCU -->
  <pin nombre="en" nodo="+3V3"/>
</componente>
```

*(Boceto: los nombres hay que ajustarlos cuando se escriba la ayuda de la
pieza, como el resto del catálogo.)*

**Adónde van los sockets.** Esta es la clave del corte A. El módulo tiene la
pila TCP/IP dentro, así que su modelo no simula TCP: **traduce cada socket del
firmware a un socket del ordenador** o a uno de otro módulo de la simulación.

* **Estación en una red con `salida="ordenador"`.** `AT+CIPSTART` hacia una
  dirección que no es de la subred del AP es un `connect()` del ordenador, por
  `common/red.h`, sin bloquear, como el modo cliente del puente serie (D8).
  Para el firmware, el módulo tiene la IP que le dio el «DHCP» del AP
  (`AT+CIFSR`). Para internet, la conexión sale del ordenador. Es un NAT sin
  pila. Los nombres (`AT+CIPDOMAIN`, o un `CIPSTART` con nombre) los resuelve
  el ordenador. **No hace falta gvproxy ni libslirp.**
* **Entre dos módulos de la simulación**, en la misma red o uno en el AP del
  otro: la conexión va por el `MedioRadio`, dentro del proceso, en tiempo
  simulado y determinista.
* **AP con `AT+CIPSERVER=1,80`.** Con `reenvia="8080:80"` (o `--reenvia
  U3=8080:80`), el módulo escucha en `localhost:8080` del ordenador, y cada
  conexión que llega allí es para el firmware un cliente nuevo, `0,CONNECT`,
  desde 192.168.4.2. **El alumno abre `http://localhost:8080` y ve la página
  de su MCU**, sin que su ordenador se asocie a nada. Es lo mismo que el
  `hostfwd` de la red de usuario de QEMU y que el reenvío de gvproxy.
* **UDP** igual. La difusión (192.168.4.255) llega a los módulos de la misma
  red; al ordenador, solo si se reenvía.
* **TLS** (`AT+CIPSTART="SSL"`): el módulo hace TLS por dentro. Llevarlo al
  ordenador pediría una biblioteca TLS en `mcu-sim`, una dependencia nueva. Se
  deja fuera de la primera entrega y se dice al recibir la orden: `ERROR` y un
  aviso con nombre.

**El cambio de modo en marcha** no tiene nada de especial:

* **`AT+CWMODE=2` estando asociado:** el módulo se desasocia, como el de
  verdad, y sus conexiones de estación se cierran con `CLOSED`.
* **`AT+CWSAP`:** su AP aparece en el aire, y una estación de otra placa puede
  encontrarlo en el siguiente `AT+CWLAP`.
* **`AT+CWMODE=3`:** los dos a la vez, que es justo lo que hace un ESP32 que
  configura la Wi-Fi desde una página propia y después se une a la red de
  casa.
* **Lo que vean los demás de ese cambio** (la desasociación, la red nueva)
  pasa por el `MedioRadio` en el instante simulado en que ocurre.

**Lo que el módulo recuerda.** Con `AT+SYSSTORE=1`, ESP-AT guarda la red y el
modo en su Flash, y al encenderse **se reasocia solo**. Muchos firmwares
cuentan con ello. Es exactamente el atributo `contenido` de las memorias no
volátiles (`doc/parts.md` §2.7): un `ModuloWifiAt` con `contenido` guarda su
NVS de una simulación a la siguiente.

**Lo que hay que fijar:** ESP-AT ha cambiado de versión en versión, y el
firmware AT antiguo del ESP8266 contesta distinto. La pieza tiene que decir a
qué versión imita (`firmware="esp-at-2"`) y probarse contra las bibliotecas
que usan los alumnos. Para eso, `guion=` del `PuenteSerie` permite reproducir
una sesión grabada con un módulo de verdad.

### 3.2 Corte B: un procesador de red (CC3100) o un chip Wi-Fi de tramas (CYW43439)

**El CC3100 con TCP/IP dentro.** Para el alumno es lo mismo que el corte A
—sockets, `sl_Connect`, `sl_WlanConnect`—, pero por SPI con el protocolo
SimpleLink, en binario.

* El *driver* del anfitrión que viene en el SDK es **código fuente**, y en él
  están los mensajes, sus códigos y sus respuestas. El modelo se escribe
  leyéndolo, igual que el XDS110 se escribió leyendo OpenOCD.
* **Detrás, lo mismo de §3.1**: los sockets del CC3100 son sockets del
  ordenador, con el mismo `PuntoAcceso` y el mismo reenvío. Solo cambia la
  entrada: SPI con su línea de interrupción (`IRQ`) y sus pines de `nHIB` y
  `nRESET`, en vez de UART y texto.
* La pieza va en una **BoosterPack**, `CC3100BOOST`, sobre la LaunchPad del
  MSP432P401R o del E401Y, con `parts/boosterpack.h`.
* Es la vía para que los ejemplos de TI de Wi-Fi corran en un MSP432
  simulado. Su tamaño depende de cuánto del protocolo use un ejemplo típico,
  y **hay que medirlo antes**, como la E0 del Ethernet.

**El CYW43439 de la Pico W.**
* La pila TCP/IP está en el MCU (lwIP) y el chip entrega **tramas Ethernet**
  al anfitrión, con mensajes de control para buscar redes, asociarse y
  montar un AP. Todo eso se lee en el `cyw43-driver`, que es abierto.
* El modelo sería el protocolo de control, con un AP virtual, y las tramas
  irían al **puente Ethernet** que ya existe (gvproxy, `qemu:PUERTO`, pcap).
  Así se aprovecha la red entera, DHCP incluido.
* Pide el RP2040, que no está modelado: Cortex-M0+ con dos núcleos y PIO.
  Queda aquí como ejemplo de que el corte B con tramas Ethernet es la forma
  barata de dar Wi-Fi a un MCU cuya pila corre en el propio MCU.

### 3.3 Corte C: la radio dentro del chip (ESP32)

El ESP32 no tiene Flash programable por registros, ni ROM pública, ni radio
documentada (`doc/analisis_esp32_riscv.md`). ESP-IDF maneja el Wi-Fi con
**bibliotecas binarias** que escriben registros sin documentar del MAC, del
PHY y de la RF. Hay tres caminos:

1. **Ingeniería inversa del MAC.** Es lo que hizo el *fork* de QEMU que usa
   PICSimLab:
   * el ESP32 simulado ve dos redes abiertas fijas («PICSimLabWifi» y
     «Espressif»);
   * se asocia, recibe una IP de la 192.168.4.x y sale al ordenador por la
     red de usuario de QEMU (slirp), con reenvío de puertos;
   * funciona con arduino-esp32 2.x (IDF 4.4) y **no con la 3.x (IDF 5.x)**:
     ahí el Wi-Fi emulado acaba en un error de caché.

   Es la prueba de que se puede, y también de lo frágil que es: cada versión
   de las bibliotecas binarias puede tocar registros distintos. **No se
   recomienda** para un proyecto que tiene que funcionar con lo que el alumno
   tenga instalado.
2. **Paravirtual.** Un periférico que **no existe en el chip**, inventado y
   documentado por nosotros (`VirtWifi`), y un componente de ESP-IDF que lo
   usa por debajo de la API de `esp_wifi`.
   * El periférico tiene colas de tramas 802.3 en la memoria del chip, como
     un *virtio*, y órdenes de buscar, asociarse, ser AP o cambiar de modo,
     con sus eventos.
   * El precedente es exacto: el QEMU de Espressif no tiene Wi-Fi ni
     Bluetooth, y para dar red añade una **Ethernet de OpenCores** que no
     existe en el chip, con su *driver* en ESP-IDF.
   * Con `VirtWifi`, el programa del alumno, que llama a `esp_wifi_connect()`
     y recibe `IP_EVENT_STA_GOT_IP`, no cambia. Solo se compila con una
     opción más.
   * El cambio de modo en marcha (`esp_wifi_set_mode`) es una orden del
     periférico.
   * Las tramas van al `MedioRadio` o, en la red que sale al ordenador, al
     **puente Ethernet** (gvproxy), con el AP virtual de §3.4.
3. **El MAC de verdad, trama a trama.** Solo si algún día se documenta. Queda
   anotado para no volver a estudiarlo.

**Para el Bluetooth del ESP32** pasa lo mismo, con un matiz útil: la pila
anfitriona (NimBLE o Bluedroid) es **abierta** y habla con el controlador
binario por una interfaz HCI interna (VHCI). Un componente que sustituya al
controlador y saque esa HCI por un periférico virtual convierte el BLE del
ESP32 en un **corte B por HCI** (§4.3), con todo lo de allí.

**Sin nada de esto, el ESP32 del análisis de RISC-V seguirá como dice ese
análisis:** la radio declarada y no modelada, y `limitaciones()` lo dirá al
montar la placa.

### 3.4 El AP virtual y la salida al ordenador (cortes B y C)

Cuando el MCU lleva la pila TCP/IP —CYW43439 o `VirtWifi`—, ya no se pueden
traducir sockets: lo que sale son tramas. Entonces el `PuntoAcceso` con
`salida="ordenador"` es un **AP virtual** que hace de puente, igual que un
router doméstico en modo *bridge*:

* gestiona la asociación (abierta o WPA2-PSK, ver §6) y convierte las
  tramas de datos 802.11 en 802.3 y al revés;
* su enlace de subida es el **puente Ethernet** ya hecho, con gvproxy al otro
  lado, que da DHCP, DNS, NAT y reenvío de puertos. Desde el ordenador, la
  página del MCU se abre por un puerto reenviado de gvproxy, como en el
  puente Ethernet.

### 3.5 La dirección difícil: el ordenador como cliente del AP del MCU

Que el **portátil del alumno se una de verdad** a la Wi-Fi que crea el MCU, y
la vea en su lista de redes, no tiene solución portátil:

* **En los cortes A y B no hace falta.** El reenvío de §3.1 da lo que se
  quiere en clase, que es abrir la página que sirve el MCU.
* **Linux, con privilegios:** `mac80211_hwsim` crea radios Wi-Fi virtuales en
  el núcleo, y NetworkManager o `wpa_supplicant` las usan como tarjetas de
  verdad. Un puente que lleve las tramas del `MedioRadio` a una de esas radios
  hace que **el AP del MCU aparezca en la lista de redes de Linux**. vwifi
  hace eso entre máquinas virtuales. Su protocolo no está documentado y su
  cliente depende de `mac80211_hwsim`, así que sirve de referencia, no de
  pieza.
* **Windows y macOS:** no hay forma de crear una tarjeta Wi-Fi virtual sin un
  *driver* propio. Se dice en la receta y se ofrece el reenvío.

---

## 4. Bluetooth

### 4.1 Corte A, clásico: HC-05 y HC-06 (`Hc05`)

El HC-05 es un SPP: un **puerto serie inalámbrico**.

* **Pines:** `rx`, `tx`; `key` o `en`, que alto al encender lo pone en modo
  órdenes AT a 38 400 baudios; y `state`, alto mientras está conectado (el LED
  de la placa).
* **Órdenes:** las básicas son `AT+NAME`, `AT+PSWD` (el PIN), `AT+ROLE`
  (esclavo o maestro), `AT+BIND` (con quién se empareja el maestro) y
  `AT+INQ`. El HC-06 es más simple: solo esclavo, órdenes sin fin de línea y a
  9 600 baudios por omisión. Como con ESP-AT, las versiones de los clones
  difieren, y la pieza dice a cuál imita.
* **Conectado, es transparente:** cada byte que entra por `rx` sale por el
  otro extremo, con el ritmo de un SPP, no el de la UART.

**El otro extremo** puede ser tres cosas:

1. **Otro `Hc05` de la simulación.** El maestro con `AT+BIND` a la dirección
   del esclavo se conecta por el `MedioRadio` al encenderse. Es el montaje
   clásico de dos Arduinos «hablándose por Bluetooth», con dos MCUs.
2. **Un canal del puente serie** (`host="tcp:4000"`, `rfc2217:…`, `memoria`,
   `guion=`). El terminal del ordenador hace de móvil. **Es lo mismo que un
   `PuenteSerie`, solo que sin cable y con el estado de conexión.** Abrir y
   cerrar el terminal es emparejar y desemparejar, y `state` lo sigue.
3. **El Bluetooth de verdad del ordenador**, para que el móvil del alumno
   (una app de «terminal Bluetooth serie») se conecte al PC como si fuera el
   módulo. Es un servidor RFCOMM:
   * **Linux:** sockets `AF_BLUETOOTH` de BlueZ, más el registro SDP del
     servicio. Factible.
   * **Windows:** Winsock con `AF_BTH` y el registro del servicio. Está en la
     API de Win32, aunque **hay que comprobar** que MinGW trae las cabeceras
     y la biblioteca.
   * **macOS:** IOBluetooth, en Objective-C. Lo más costoso.

   Para no meter tres API de Bluetooth en `mcu-sim`, se propone un
   **ayudante aparte** que hable con la pieza por TCP (el canal 2) y con el
   sistema por su API. Es la misma filosofía que dejar la red a gvproxy.

### 4.2 Corte A, BLE: HM-10 (`Hm10`)

El HM-10 y sus clones son un «puerto serie por BLE».

* Hacen de periférico con un servicio **FFE0** y una característica **FFE1**:
  lo que entra por la UART sale como notificación de FFE1, y lo que el
  central escribe en FFE1 sale por la UART.
* En modo central (`AT+ROLE1`), se conectan a otro HM-10.

**El otro extremo**, como en el SPP:

1. **Otro `Hm10` de la simulación**, por el `MedioRadio`.
2. **Un canal del puente serie**, con el estado de conexión.
3. **El BLE de verdad del ordenador**, con un **servidor GATT** que publica
   FFE0 y FFE1. El móvil del alumno, con nRF Connect, un terminal BLE o una
   página con Web Bluetooth, encuentra «el HM-10», que en realidad es el PC:
   * **Linux:** BlueZ por D-Bus.
   * **macOS:** CoreBluetooth (`CBPeripheralManager`).
   * **Windows:** `GattServiceProvider` de WinRT, que anuncia y acepta
     conexiones. Hay que declarar la capacidad `bluetooth`, y **hay que
     comprobar** que se puede usar desde un programa de escritorio sin
     empaquetar y qué adaptadores admiten el papel de periférico.

   En Python ya está hecho casi todo: la biblioteca **Bless** da un servidor
   GATT sobre BlueZ y CoreBluetooth, y para Windows hay ejemplos con WinRT.
   El ayudante es un guion de Python de unas pocas decenas de líneas, fuera
   de `mcu-sim`, como `verif/serie/interop.py`. Al revés, con el HM-10 como
   central que busca un periférico de verdad, el ayudante usa **Bleak**, el
   cliente BLE de Python para las tres plataformas.

Lo que **no** se consigue así: que el MCU haga su propio GATT, con servicios y
características a su gusto. Eso es el corte B.

### 4.3 Corte B: HCI y un controlador virtual

Bluetooth tiene una frontera estándar entre la **pila anfitriona** (GAP,
GATT, L2CAP, SMP) y el **controlador** (el *link layer* y la radio): **HCI**.
Muchas placas la exponen tal cual, y entonces la pila anfitriona corre en el
MCU del alumno:

* un controlador por UART con HCI (el transporte H4), como el CC2564 o un
  nRF52 con el ejemplo `hci_uart` de Zephyr;
* el BlueNRG por SPI, con su HCI y su ACI;
* el STM32WB, donde el núcleo M4 manda órdenes HCI y ACI al M0+ por el buzón
  **IPCC** y una memoria compartida. ST da la pila del M0+ como binario,
  incluida una versión «solo HCI»;
* el ESP32 con un controlador paravirtual (§3.3).

**La pieza** habla el transporte —H4 por UART, el SPI del BlueNRG o el IPCC—
y lleva los paquetes HCI a un **controlador virtual**. No hay que escribirlo,
porque los hay hechos:

* **RootCanal**, de Google: el controlador virtual del emulador de Android.
  Tiene licencia Apache-2.0 y escucha **HCI por TCP en el puerto 6402**, con
  otros puertos para enlazar controladores entre sí (6403 clásico, 6404 LE).
  Varios controladores en el mismo RootCanal se ven entre ellos: dos MCUs
  simulados con su pila, conectándose de verdad. Su paquete de Python se
  publica para Linux x86-64 y macOS arm64, no para Windows.
* **Bumble**, también de Google, en Python. Es una pila Bluetooth con
  controladores virtuales y muchos transportes: TCP, el `/dev/vhci` de Linux
  y adaptadores USB por libusb.

Y **para que el ordenador vea al MCU** con su pila de verdad:

* **Linux:** un controlador virtual en `/dev/vhci` aparece en BlueZ como un
  adaptador más, y si está enlazado con el del MCU en el mismo aire virtual,
  `bluetoothctl` y cualquier programa del ordenador ven y conectan con el
  periférico simulado. Pide permiso sobre `/dev/vhci` (una regla de udev o
  `root`).
* **Windows y macOS:** no hay un `vhci`. Queda el ayudante GATT de §4.2 en
  versión general: lee la base de datos GATT que publica el MCU por HCI y la
  vuelve a publicar con la API del sistema. Es posible, pero la latencia y
  los permisos cambian lo que el MCU ve, y es un trabajo propio. Se deja como
  opción avanzada.

### 4.4 Corte C: la radio BLE del nRF52

Es el único MCU con radio integrada cuyo modelo fiel es realista:

* el periférico `RADIO` está documentado registro a registro: frecuencia,
  modo (BLE a 1 o 2 Mbit/s), dirección de acceso, `PACKETPTR` con EasyDMA, y
  las tareas y eventos `TXEN`, `RXEN`, `READY`, `ADDRESS`, `END`;
* el controlador abierto de Zephyr y el SoftDevice de Nordic (binario, pero
  sobre hardware documentado) corren encima sin trucos;
* **Nordic ya lo ha hecho**: los modelos del nRF52 de BabbleSim (`RADIO`,
  `TIMER`, `RTC`, `PPI`, `AAR`, `CCM`, `ECB`…) son los que usa Zephyr para
  probar su pila BLE sin hardware. El PHY de BabbleSim es determinista y va
  desacoplado del tiempo real. Sirven de **referencia de comportamiento** y,
  quizá, de vectores de prueba. Su forma de conectar procesos (FIFOs, solo
  Linux) no encaja con las tres plataformas de `mcu-sim`.

En `mcu-sim`, el `RADIO` mandaría PDUs del *link layer* al `MedioRadio` con su
tiempo de aire (8 µs por byte a 1 Mbit/s). Los plazos de microsegundos se
cumplen solos, porque todo va en tiempo simulado. **Llevar eso al ordenador**
es otra cosa: el ordenador habla HCI, no *link layer*. Habría que poner del
otro lado un controlador virtual enlazado (RootCanal o Bumble con
`/dev/vhci`, solo Linux) o el ayudante GATT. En clase, lo que más rinde es
**dos nRF52 en el mismo `<sistema>`**, uno central y otro periférico, con todo
dentro del proceso.

### 4.5 El STM32WB, en particular

Por cercanía con lo que ya hay, merece una línea propia. Su núcleo de
aplicación es un M4 con periféricos de la casa, como los de la familia L4. El
M0+ con la radio ejecuta una pila binaria cifrada que se instala con el FUS de
ST. **No hay que modelar el M0+:** se modela el IPCC y la memoria compartida
con el formato de su transporte, legible en STM32CubeWB, y detrás va un
controlador virtual (§4.3). El firmware del alumno, generado por CubeMX con
STM32_WPAN, no notaría la diferencia mientras se use la variante de la pila
cuyas órdenes se modelen.

---

## 5. Plataformas

| Qué | Linux | Windows | macOS |
| :--- | :--- | :--- | :--- |
| **Corte A**: `ModuloWifiAt`, `Hc05`, `Hm10` con TCP o RFC 2217, sockets reenviados | Sí | Sí | Sí |
| **CC3100** (corte B con sockets) | Sí | Sí | Sí |
| **AP virtual** con salida por el puente Ethernet y gvproxy | Sí | Sí | Sí |
| **El ordenador se une al AP del MCU** (`mac80211_hwsim`) | Sí, con privilegios | No | No |
| **El móvil se conecta a un HC-05 simulado** (RFCOMM, ayudante) | Sí (BlueZ) | Probable (Winsock `AF_BTH`, por comprobar con MinGW) | Costoso (IOBluetooth) |
| **El móvil se conecta a un HM-10 simulado** (GATT, ayudante) | Sí (Bless sobre BlueZ) | Sí, con adaptador que admita periférico (WinRT) | Sí (Bless sobre CoreBluetooth) |
| **El HM-10 simulado busca un periférico de verdad** (Bleak) | Sí | Sí | Sí |
| **HCI a RootCanal** | Sí | Por TCP a un RootCanal en otra máquina, o compilándolo | Sí (arm64) |
| **El ordenador ve al MCU como periférico BLE** (`/dev/vhci`) | Sí, con permiso | No | No |

El patrón es el de los otros tres puentes: **lo que va por TCP funciona en
todas; lo que se mete en el sistema operativo, solo en Linux y con
privilegios**. Por eso, lo que toca la API de Bluetooth del sistema va en
ayudantes fuera de `mcu-sim`, que no añade dependencias.

---

## 6. Tiempo, seguridad y otras cosas que hay que decidir

* **`--tiempo-real` con el ordenador al otro lado**, por la misma razón que
  con el puerto serie y el Ethernet. TCP, DHCP, las conexiones BLE y las apps
  del móvil tienen plazos de reloj de pared. Un aviso al montar, como el que
  ya da el puente serie por TCP.
* **Sin el ordenador, tiempo simulado:** dos módulos o dos nRF52 en el mismo
  proceso son deterministas, y sus pruebas pueden tener invariante.
* **WPA2-PSK.** En el corte A es una comparación de la clave, sin más. En los
  cortes B con tramas y C, el AP virtual tiene que hacer el intercambio de
  cuatro mensajes (EAPOL) y cifrar con CCMP (AES-CCM). Es aritmética conocida,
  de la misma clase que la de `periph/hash_algo.h`, con vectores de la norma.
  La primera entrega puede ser con redes abiertas, avisando si el firmware
  pide WPA2.
* **El emparejamiento BLE** (SMP) es de la pila anfitriona en el corte B. En
  el A, el HM-10 tiene su PIN fijo.
* **Nada sale al aire.** El simulador no emite radio. El ayudante usa la radio
  del ordenador con su propia pila, como cualquier programa. Conviene decirlo
  en la receta, porque es lo primero que se pregunta en un aula.
* **Seguridad de los puertos:** como en el puente serie (D-7), todo escucha en
  `localhost` por omisión. Un reenvío a todas las interfaces se pide aparte.
* **La GUI:** los observables de las piezas de radio (asociado, modo, RSSI,
  conectado, bytes en cada sentido) llegan a `mcu-sim-gui` sin cambiar el
  protocolo. Los dibujos (un ESP-01, un HC-05, una CC3100BOOST) son trabajo
  de ilustración, como cualquier pieza nueva.

---

## 7. Recomendación y plan por fases

Mismo método de siempre: fases pequeñas, cada una con su cierre y **sin mover
los invariantes de las ocho suites**. Ninguna de estas piezas entra en ellas.
Las pruebas, en un banco propio (`testradio`) con su invariante y en Python
contra el `mcu-sim` de verdad, como `gui-marcha` e `interop`.

| Fase | Qué | Cierre |
| :--- | :--- | :--- |
| **R0 · Medir y elegir** | Qué bibliotecas usan los alumnos con un ESP-01 y un HC-05 desde un STM32 y un MSP432, y qué versión de ESP-AT imitar. Grabar sesiones reales con un módulo de verdad | Una tabla en `doc/` con las órdenes que usa cada biblioteca, y las sesiones grabadas en `verif/radio/` |
| **R1 · `MedioRadio` y `PuntoAcceso`** | El aire en un proceso: participantes, anuncios, alcance opcional; el AP con su SSID, su clave y su subred | Dos piezas de prueba se ven, se asocian y se desasocian en el instante simulado esperado; invariante en `testradio` |
| **R2 · `ModuloWifiAt`** | El intérprete ESP-AT; estación, AP y los dos; sockets al ordenador por `red.h` y entre módulos por el aire; `reenvia=`; NVS con `contenido` | Con el firmware de un F446: `AT+CWJAP`, un GET a un servidor HTTP en Python del ordenador, y un servidor en modo AP que `curl localhost:8080` lee. Las sesiones grabadas de R0 se reproducen. En las cuatro plataformas del CI |
| **R3 · El cambio de modo y dos placas** | `CWMODE` en marcha; dos placas en un `<sistema>`, una de AP y otra de estación | El firmware de configuración por página web: arranca en AP, recibe la red por un formulario, pasa a estación y se asocia |
| **R4 · `Hc05` y `Hm10`** | Los dos módulos con su modo de órdenes y su pin de estado; extremo en otro módulo, en un canal del puente serie o en `guion=` | Dos MCUs hablándose por SPP; un terminal por RFC 2217 hace de móvil; el pin `state` sigue a la conexión |
| **R5 · Los ayudantes** | En Python, fuera de `mcu-sim`: GATT con Bless o WinRT para el HM-10, RFCOMM con BlueZ para el HC-05, y Bleak para el HM-10 como central | El móvil, con nRF Connect, escribe en FFE1 y el MCU simulado lo recibe. Probado a mano y anotado en una matriz por plataforma, como la §10.6 del puente serie |
| **R6 · CC3100BOOST** | El protocolo SimpleLink por SPI, con el mismo fondo de R2, en una BoosterPack | Un ejemplo de TI de Wi-Fi en la LaunchPad del MSP432 simulada se asocia y hace un GET |
| **R7 · HCI** | Una pieza HCI por UART (H4) hacia RootCanal por TCP; en Linux, Bumble con `/dev/vhci` | Un firmware con pila BLE en el MCU (un controlador HCI en la placa) anuncia y `bluetoothctl` lo ve en Linux |
| **R8 · Una radio integrada** | Cuando haya un MCU con radio: el nRF52 con su `RADIO` (corte C) o el ESP32-C3 con `VirtWifi` (paravirtual) | Dos nRF52 en un `<sistema>` con el ejemplo de periférico y central de Zephyr; o el `station_example` de ESP-IDF con la opción paravirtual, asociado al AP virtual y saliendo por gvproxy |

**Orden:** R0 → R1 → R2 → R3, que ya contesta entera la pregunta del Wi-Fi
como cliente y como AP. R4 y R5 se pueden hacer en paralelo con R3. R6 y R7
dependen de lo que se quiera enseñar. R8 depende de que exista el MCU.

**Tamaño estimado:**
* R1 y R2 son el grueso: un intérprete de órdenes con estado, y los sockets
  sin bloquear, ya hechos en el puente serie. Algo menos que el puente USB.
* R4 es pequeña: casi un `PuenteSerie` con estado de conexión.
* R5 está fuera de `mcu-sim` y se mide en guiones de Python.
* R6 es mediana, según cuánto protocolo pida el ejemplo.
* R7 es mediana.
* R8 es grande, como cualquier MCU nuevo.

---

## 8. Riesgos y preguntas abiertas

1. **Las versiones de los módulos.** ESP-AT, el AT antiguo del ESP8266 y los
   clones del HC-05 y del HM-10 contestan distinto. Si la pieza imita una
   versión que no es la de la biblioteca del alumno, «no funciona» sin que su
   código esté mal. R0 existe para eso, y la pieza tiene que decir a qué
   versión imita.
2. **TLS en el módulo.** Las prácticas con la nube (MQTT con TLS, HTTPS) lo
   piden. Llevarlo al ordenador exige una biblioteca TLS en `mcu-sim`, o un
   ayudante que termine TLS fuera. Hay que decidirlo antes de prometer esas
   prácticas.
3. **El papel de periférico en Windows.** No todos los adaptadores Bluetooth
   lo admiten, y la API de WinRT puede pedir un programa empaquetado. Si es
   así, el ayudante de Windows es un programa aparte, no un guion de Python.
4. **El ESP32.** Sin el paravirtual, sus prácticas de Wi-Fi y BLE no se
   pueden hacer en `mcu-sim`. Con él, pide compilar con una opción; hay que
   ver si eso es aceptable para un curso que usa Arduino-ESP32 tal cual.
5. **Los plazos con el ordenador.** Si la CPU simulada va más despacio que el
   reloj, una app del móvil puede dar por muerta la conexión BLE. Se mide en
   R5, con `--tiempo-real` y sin él.
6. **Las direcciones.** La MAC del módulo, la BD_ADDR del HC-05 y las IP del
   AP virtual tienen que ser únicas en el aire y estables entre ejecuciones:
   por omisión, derivadas del id de la pieza, y configurables.
7. **El servidor de aire entre procesos** (§2) queda fuera. Si algún día hace
   falta para el corte C, BabbleSim es el modelo a estudiar.

---

## Fuentes

* Código y documentación de `mcu-sim`: `parts/motor_uart.h`,
  `parts/canal_host.h`, `parts/puente_serie.h`, `parts/puente_ethernet.h`,
  `parts/puente_usb.h`, `parts/boosterpack.h`, `common/red.h`,
  `common/memoria_nv.h`, `doc/parts.md` (§2.0, §2.5 a §2.7),
  `doc/analisis_puente_serie.md`, `doc/analisis_puente_ethernet.md`,
  `doc/analisis_puente_usb.md`, `doc/analisis_esp32_riscv.md`.
* Espressif, ESP-AT, [TCP/IP AT Examples](https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp32c3/AT_Command_Examples/TCP-IP_AT_Examples.html)
  (órdenes de modo, asociación, AP, sockets, `+IPD` y modo transparente),
  consultado el 04-11-2026.
* Espressif, [QEMU: periféricos admitidos](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md)
  (sin Wi-Fi ni Bluetooth en el ESP32 ni en el C3; Ethernet de OpenCores
  para la red), consultado el 04-11-2026.
* [Emulación del ESP32 con el QEMU de lcgamboa](https://git.manakin-gentoo.ts.net/a2nr/velxio/raw/branch/master/docs/ESP32_EMULATION.md)
  (Wi-Fi con dos redes fijas, slirp, IDF 4.4 sí y 5.x no), y el
  [QEMU de lcgamboa](https://github.com/lcgamboa/qemu) usado por
  [PICSimLab](https://github.com/lcgamboa/picsimlab).
* [vwifi](https://github.com/Raizo62/vwifi): Wi-Fi entre máquinas virtuales
  Linux con `mac80211_hwsim`, por VSOCK o TCP.
* [RootCanal en PyPI](https://pypi.org/project/rootcanal) (controlador
  Bluetooth virtual, Apache-2.0, puertos 6401 a 6404, Linux x86-64 y macOS
  arm64) y [su código](https://android.googlesource.com/platform/packages/modules/Bluetooth/+/57b823f4f758e2ef530909da07552b5aa80c6a7d/tools/rootcanal/).
* Bumble, [transporte VHCI](https://google.github.io/bumble/transports/vhci.html);
  BlueZ, [`emulator/vhci.c`](https://sources.debian.org/src/bluez/5.83-1~exp1/emulator/vhci.c).
* Zephyr, [nrf52_bsim](https://docs.zephyrproject.org/latest/boards/native/nrf_bsim/doc/nrf52_bsim.html)
  (periféricos modelados, PHY de BabbleSim) y
  [los modelos del nRF52 de BabbleSim](https://github.com/BabbleSim/ext_NRF52_hw_models).
* TI, [notas de la versión 1.3.0 del CC3100 SDK](https://software-dl.ti.com/ecs/CC3100SDK/1_3_0/exports/cc3100-sdk_1_3_0_release_notes.html)
  (plataformas de referencia, SPI a 20 MHz y UART a 3 MHz, *driver* del
  anfitrión en código fuente) y [CC3100SDK](https://ti.com/tool/CC3100SDK).
* [`cyw43-driver`](https://github.com/georgerobotics/cyw43-driver) y
  [picowi](https://iosoft.blog/picowi) (el SPI del CYW43439 de la Pico W).
* [embassy-stm32-wpan](https://docs.embassy.dev/embassy-stm32-wpan/git)
  (canales IPCC y transporte del STM32WB) y la wiki de ST sobre la
  [pila BLE del STM32WB](https://wiki.st.com/stm32mcu/wiki/Connectivity:STM32WB_BLE_Wireless_Stack).
* Microsoft, [Bluetooth GATT Server](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-server)
  y [`GattServiceProvider`](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattserviceprovider).
* [Bless](https://github.com/kevincar/bless) (servidor GATT en Python, BlueZ y
  CoreBluetooth), [un ejemplo con Bless y WinRT](https://github.com/postry/ble-gatt-server)
  y [Bleak](https://pypi.org/project/bleak) (cliente BLE en Python para las
  tres plataformas).
* *Sin consultar en esta versión del análisis, y pendientes de incorporar a
  `doc/fuentes.md`:* las hojas del HC-05, HC-06 y HM-10 (sus órdenes AT se
  citan de memoria y hay que comprobarlas en R0), la especificación de
  Bluetooth Core (HCI, T_IFS), IEEE 802.11-2020 (EAPOL, CCMP), la nRF52840
  Product Specification (`RADIO`) y la guía de programación del CC3100.
