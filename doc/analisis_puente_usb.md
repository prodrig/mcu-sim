# Puente entre el USB del MCU y el ordenador

**Análisis de opciones para `mcu-sim`** · 30-09-2026

La pregunta: qué opciones hay para construir, como se hizo con el puerto serie
(`doc/analisis_puente_serie.md`, P-14), un elemento **externo al MCU** que
conecte el USB OTG del chip simulado con el ordenador donde corre `mcu-sim`.
Hay dos direcciones:

* **el MCU como dispositivo**: que el ordenador vea lo que el firmware hace con
  el USB (el puerto COM virtual CDC que genera CubeIDE, un teclado HID, un
  disco) como si la placa estuviera enchufada;
* **el MCU como anfitrión**: que el firmware de *USB Host* encuentre al otro
  lado algo que enumerar, sea un pendrive, un teclado o un dispositivo real del
  ordenador.

Y qué cambia en cada plataforma que el proyecto soporta (Linux x86-64, Windows
x86-64 con MinGW, macOS arm64 y x86-64).

---

## 0. Resumen

* **El modelo ya tiene la frontera correcta.** El OTG va en los pines para lo
  eléctrico (conexión, velocidad, reset, VBUS, ID) y **por paquetes** para los
  datos: una transacción (testigo, datos, respuesta) es una llamada a
  `usb_dev_if::transaccion()`. Sin NRZI ni relleno de bits, y por eso barato:
  al contrario que el Ethernet, **el USB puede ir en tiempo real sin tocar el
  modelo** (§1).
* **Lo que falta en `mcu-sim` es poder enchufarlo desde la placa.** Esa
  interfaz de paquetes solo se conecta desde C++, en los bancos de pruebas: una
  pieza del XML no puede llegar al OTG del chip. Es el primer paso de cualquier
  puente (§3.3) y el mismo problema que tiene el puente Ethernet.
* **En el lado del host, el USB es lo menos portátil de todo.** Que un
  dispositivo simulado aparezca en el sistema como uno de verdad exige un
  controlador virtual en el núcleo: **USB/IP** en Linux (en el núcleo desde
  hace años) y en Windows (**usbip-win2**, BSD-2, con drivers firmados, desde
  Windows 10 1903). **En macOS no hay nada maduro**: el único cliente USB/IP
  conocido es experimental y pide un *entitlement* de Apple o desactivar SIP
  (§5).
* **Pero el caso que más se usa en clase no lo necesita.** El puerto COM
  virtual por USB (CDC-ACM) se puede resolver **dentro del puente**: el puente
  hace de anfitrión, enumera el dispositivo del MCU y saca sus datos por TCP con
  RFC 2217, reutilizando entero el puente serie. Los baudios que el alumno pone
  en su terminal llegan al firmware como `SET_LINE_CODING`, y DTR/RTS como
  `SET_CONTROL_LINE_STATE`, que es exactamente lo que pasa con una placa de
  verdad. **Funciona igual en las tres plataformas y sin drivers** (§6.2).
* **Recomendación (§8):**
  1. La conexión de piezas a periféricos del MCU, compartida con Ethernet.
  2. Un pequeño **controlador de anfitrión dentro del puente**: transferencias
     de control, *bulk* e interrupción sobre las transacciones del modelo.
  3. **El puente de clase CDC → RFC 2217**, que es lo que el alumno usa.
  4. **Un servidor USB/IP** para el caso general —HID, MSC, clases propias—
     en Linux y Windows.
  5. Para el MCU como anfitrión, **dispositivos virtuales en el puente**: un
     pendrive respaldado por un fichero de imagen y un teclado alimentado por
     TCP.

---

## 1. Qué hay en `mcu-sim` que condiciona el diseño

### 1.1 El OTG

`periph/otg.h` modela el OTG_FS y el OTG_HS (`OtgFs`, `OtgHs`), los dos con
papel de anfitrión y de dispositivo. Están en **todos** los chips del catálogo
(F405, F407, F415, F417 y F446).

* **Lo eléctrico va en los pines**, con tensiones: las resistencias de 1,5 kΩ
  del dispositivo y de 15 kΩ del anfitrión, la decodificación J/K/SE0, la
  detección de conexión y velocidad, el reset de bus (SE0 de más de 2,5 µs), el
  *resume*, los comparadores de VBUS (más de 4,4 V es válido) y el pin ID. En el
  OTG_FS: PA11 (D−), PA12 (D+), PA9 (VBUS) y PA10 (ID).
* **Los paquetes van como paquetes**, por una interfaz abstracta:

  ```cpp
  class usb_dev_if {
      virtual uint8_t transaccion(uint8_t pid_token, uint8_t addr, uint8_t ep,
                                  const std::vector<uint8_t>& salida,
                                  std::vector<uint8_t>& entrada) = 0;   // ACK, NAK, STALL...
      virtual void sof(uint16_t trama) {}
  };
  ```

  En **modo dispositivo el OTG es un `usb_dev_if`**: el anfitrión lo llama, y él
  contesta ACK, NAK o STALL según sus registros de *endpoint* y llena su FIFO de
  recepción, como el silicio. En **modo anfitrión el OTG llama** a un
  `usb_dev_if` que se le conecta con `conectar_dispositivo()`.
* **El tiempo:** un motor de tramas de 1 ms (125 µs en HS con ULPI). En modo
  anfitrión cada trama manda un SOF; en modo dispositivo, 3 ms sin actividad son
  un *suspend*. Nada corre si el transceptor está apagado.

Lo que no está (`doc/todo.md`):

| | Qué | Qué supone para un puente |
| :--- | :--- | :--- |
| T-17 | NRZI, relleno de bits, CRC y reintentos por CRC | Nada: el puente habla por paquetes |
| F-37 | Protocolo ULPI | Nada |
| F-38 | Alta velocidad de verdad (*chirp*, *split*) | **Todo va a velocidad completa, 12 Mbit/s**. Un firmware HS funciona, pero a FS |
| F-39 | SRP y HNP | Sin cambio de papel en caliente |
| F-40 | Planificador de transferencias isócronas y de interrupción | En modo anfitrión todos los canales se atienden en cada trama: HID funciona, audio isócrono no |
| F-41 | Concentradores | Un dispositivo por puerto |

### 1.2 Los aparejos

`parts/ext_parts.h` tiene los dos extremos del cable, que usan los bancos:

* **`UsbHostRig`**: un PC. Da VBUS, pone las resistencias de anfitrión, hace el
  reset de bus y manda testigos: `setup(addr, 8 bytes)`, `in(addr, ep, datos)`,
  `out(addr, ep, datos)`, `sofs(n)`. Cada testigo llama a `transaccion()` del
  dispositivo y espera 1 µs.
* **`UsbDeviceRig`**: un pendrive mínimo. Contesta a `GET_DESCRIPTOR` (un
  descriptor de dispositivo de 18 bytes) y a `SET_ADDRESS`, y se entera del
  reset por el cable.

Con ellos `test407` enumera el OTG_FS como dispositivo (T115) y usa el OTG_HS
como anfitrión (T116). **Pero la interfaz de paquetes solo se conecta a mano, en
C++**: en `sim_main.cpp`, con una placa XML, los aparejos solo hacen su parte
eléctrica y cada testigo acabaría sin respuesta.

### 1.3 Lo que cuesta

`doc/coste_simulacion.md`: el motor de tramas del OTG_FS despierta mil veces por
segundo simulado, y el del OTG_HS ocho mil; poca cosa. Cada transacción es una
llamada de función. **El coste de un puente USB es el del firmware**, no el del
modelo: un firmware que no duerme va unas cinco veces más lento que el tiempo
real (`doc/chat.md`), y la pila USB de ST, dirigida por interrupciones, deja al
núcleo en su bucle principal. Hay que medirlo con un ejemplo de CubeIDE (§10,
U0).

---

## 2. Qué se quiere, en clase

| Caso | Papel del MCU | Frecuencia en prácticas | Qué necesita el puente |
| :--- | :--- | :--- | :--- |
| **Puerto COM virtual** (CDC-ACM, «USB_DEVICE → CDC» en CubeIDE) | Dispositivo | **La más alta**: es el `printf` por USB | Que el ordenador tenga un terminal conectado a los *endpoints* CDC |
| **Teclado o ratón** (HID) | Dispositivo | Media | Que el sistema reciba las pulsaciones, o al menos verlas |
| **Disco** (MSC, sobre la Flash o una SD) | Dispositivo | Media | Que el sistema monte el disco, o poder leerlo |
| **Clase propia** (*vendor*, WinUSB) | Dispositivo | Baja | Que un programa del ordenador (libusb, pyusb) hable con él |
| **Leer un pendrive** («USB_HOST → MSC») | Anfitrión | Media | Un disco al otro lado, con un sistema de ficheros |
| **Leer un teclado** («USB_HOST → HID») | Anfitrión | Baja | Un teclado al otro lado |

---

## 3. Lado MCU: dónde se engancha el puente

### 3.1 La frontera

La de paquetes, que ya existe. **No hace falta tocar el OTG.** El puente es, en
modo dispositivo, un anfitrión que llama a `transaccion()`; en modo anfitrión,
un `usb_dev_if` que el OTG llama.

### 3.2 Lo que el puente tiene que hacer en modo dispositivo

El sistema operativo (o el puente, en §6.2) habla en **transferencias**, no en
transacciones: «lee hasta 64 bytes del *endpoint* 0x81», «manda este
`SET_LINE_CODING`». El puente tiene que partirlas como lo haría un controlador
de anfitrión:

* **Control:** SETUP con los 8 bytes; las fases de datos IN u OUT, paquete a
  paquete con el tamaño máximo del *endpoint* 0; y la fase de estado.
* ***Bulk*:** IN u OUT repetidos hasta completar la longitud o recibir un
  paquete corto.
* **Interrupción:** un IN cada `bInterval` tramas.
* **NAK** quiere decir «ahora no»: se reintenta en la trama siguiente, **en
  tiempo simulado**. Un firmware que tarda en llenar su FIFO recibe NAK hasta
  que lo hace, como en la placa.
* **SOF cada milisegundo** mientras haya algo conectado, lo pida o no el
  sistema: sin él, el OTG entra en *suspend* a los 3 ms.
* **Reset, dirección y configuración** al enchufar: el puente puede hacer él la
  enumeración (§6.2) o dejar que la haga el sistema (§6.3).

Es un «controlador de anfitrión» pequeño, unas pocas centenas de líneas, que
`UsbHostRig` ya tiene a medias (`setup`, `in`, `out`, `sofs`). Se prueba solo,
contra un dispositivo de pega, sin sistema operativo por medio.

### 3.3 El nuevo problema de la placa

Una pieza del XML no puede agarrar un periférico del MCU (`parts/part_factory.h`:
el creador recibe nodos, parámetros y otras piezas). El puente necesita el
`usb_dev_if*` del OTG del chip. Es el mismo problema que el puente Ethernet
(`doc/analisis_puente_ethernet.md` §3.3) y conviene resolverlo una vez para los
dos. Por ejemplo, un atributo en la pieza que nombre el periférico (`otg="fs"`,
y el MCU si hay varios) y que `sim_main.cpp` resuelva después de montar la
placa, como ya hace con los `PuenteSerie`.

La parte eléctrica sigue en los pines: el puente pone VBUS, las resistencias
del papel que haga y el reset de bus. Lo más sencillo es que **el puente
contenga un `UsbHostRig`** (o un `UsbDeviceRig`) para esa parte.

---

## 4. Acoplamiento de tiempos

* **Los plazos del sistema operativo son generosos**: una transferencia de
  control tiene 5 s en Linux (`USB_CTRL_GET_TIMEOUT`) y la enumeración espera
  cientos de milisegundos entre pasos. Un simulador cinco veces más lento que el
  tiempo real entra dentro de eso; uno ochocientas veces más lento, no (es el
  caso del Ethernet en los pines, y el USB no lo tiene).
* **`--tiempo-real`**, como siempre, para que los plazos del firmware
  (`HAL_Delay`, los de la pila de ST) sean los de la placa.
* **Sondeo en tiempo simulado:** las transferencias que llegan del sistema se
  convierten en transacciones en el ritmo de tramas simuladas. Un IN que recibe
  NAK no bloquea: se reintenta en la trama siguiente.
* **El *suspend* del sistema.** Linux suspende los dispositivos USB inactivos
  (*autosuspend*). Por USB/IP eso llega como una petición más, no como falta de
  SOF, y el puente decide: lo más sencillo es no suspender nunca (SOF siempre)
  y documentarlo.
* **Determinismo:** el banco del puente espera en tiempo de pared a que cada
  transferencia esté dentro antes de dejar avanzar la simulación, como
  `testserie`.

---

## 5. Lado host: los mecanismos, plataforma por plataforma

### 5.1 Que el dispositivo simulado aparezca en el sistema

Para que el sistema cargue su *driver* de clase (`cdc_acm`, `usbser.sys`,
`usbhid`, `usb-storage`) hace falta un **controlador de anfitrión virtual** en el
núcleo, que reciba las peticiones del sistema y se las dé a un programa.

| Sistema | Mecanismo | Precio |
| :--- | :--- | :--- |
| **Linux** | **USB/IP**: el módulo `vhci-hcd` y la orden `usbip attach` (paquetes `linux-tools` o `usbip`) | En el núcleo. `modprobe vhci-hcd` y `usbip attach -r 127.0.0.1 -b BUSID` como `root`. El dispositivo aparece como uno más: `/dev/ttyACM0`, un teclado, `/dev/sdX` |
| **Linux** | `dummy_hcd` + `raw-gadget` | Módulos que la mayoría de distribuciones no traen compilados; `root`. Más cerca de un *gadget* que de un dispositivo |
| **Windows** | **USB/IP con usbip-win2** | Cliente libre (BSD-2), para Windows 10 1903 x64 o posterior y Windows 11 ARM64, con **drivers firmados** (WHLK o por atestación): no pide modo de prueba. Instalador de administrador; `usbip.exe attach -r 127.0.0.1 -b BUSID`. Un CDC aparece como `COMn` **sin com0com** |
| **macOS** | **Nada maduro.** El único cliente USB/IP conocido (usbip-macos) es experimental y exige el *entitlement* `com.apple.developer.usb.host-controller-interface` o `root` con SIP desactivado | Fuera de lo que se le puede pedir a un alumno |

**USB/IP** es un protocolo por TCP, sin dependencias: primero se pide la lista
de dispositivos exportados y se importa uno; después, sobre la misma conexión,
el cliente manda peticiones (`USBIP_CMD_SUBMIT`: *endpoint*, dirección, longitud
y, en control, los 8 bytes de SETUP) y el servidor contesta cada una
(`USBIP_RET_SUBMIT`), o la cancela (`UNLINK`). Todo en orden de red. El
**servidor** es el que tiene el dispositivo; **en esta dirección, `mcu-sim` es
el servidor**, igual que en el puente serie (D-1).

### 5.2 Que un dispositivo del ordenador llegue al MCU (el MCU como anfitrión)

| Mecanismo | Plataformas | Precio |
| :--- | :--- | :--- |
| **USB/IP al revés**: `mcu-sim` como cliente, importando un dispositivo que exporta el ordenador | Linux (`usbipd`, en el núcleo), Windows (**usbipd-win**, libre, el que se usa para pasar USB a WSL), macOS (experimental) | El dispositivo deja de estar disponible para el sistema mientras lo usa el MCU |
| **libusb** directamente | Las tres (LGPL-2.1) | En Windows hay que cambiarle el *driver* al dispositivo por WinUSB (Zadig); en Linux, permisos (`udev`). Invasivo |
| **Dispositivos virtuales en el propio puente** | Las tres, sin nada | Solo lo que el puente sepa imitar: un disco sobre un fichero de imagen, un teclado |

---

## 6. Dónde vive el lado host: las arquitecturas

### 6.1 U1 · Servidor USB/IP (el MCU como dispositivo, caso general)

El puente exporta el OTG del MCU por USB/IP en `localhost:3240`. El sistema lo
importa y lo usa con sus propios *drivers*.

* **A favor:** cualquier clase, sin que el puente sepa nada de ella: CDC, HID,
  MSC, una clase propia con libusb. Es el MCU de verdad para el sistema.
* **En contra:** Linux y Windows solamente; el `attach` es de administrador cada
  vez; y el puente tiene que hacer bien el papel de controlador de anfitrión
  para todas las transferencias que mande el sistema, cancelaciones incluidas.

### 6.2 U2 · Puente de clase (el MCU como dispositivo, sin sistema)

El puente hace de anfitrión completo: enumera el dispositivo del MCU, reconoce
la clase y la traduce a algo que ya se sabe usar sin drivers.

| Clase | Se traduce a | Cómo |
| :--- | :--- | :--- |
| **CDC-ACM** | **El canal del puente serie**: TCP en crudo o RFC 2217 | Los datos, por los *endpoints bulk*. **`SET-BAUDRATE`, el formato y DTR/RTS de RFC 2217 se convierten en `SET_LINE_CODING` y `SET_CONTROL_LINE_STATE`**: el firmware recibe en `CDC_Control_FS` lo que eligió el terminal, como en una placa. Todas las recetas de `doc/puente_serie.md` valen tal cual |
| **HID** | Informes por TCP, en crudo o como texto | Para ver lo que manda un teclado o un ratón; y, si se quiere, un programa pequeño que los convierta en pulsaciones del sistema (fuera de `mcu-sim`) |
| **MSC** | Un fichero de imagen, o un servidor NBD | El puente lee los bloques con SCSI (`READ(10)`) y los vuelca, o los sirve por NBD (Linux lo monta con `nbd-client`) |

* **A favor:** **las tres plataformas, sin drivers ni privilegios**, y el caso
  más común (CDC) queda resuelto con código que ya existe y está probado. Se
  prueba en CI de punta a punta.
* **En contra:** una traducción por clase; lo que el puente no conoce no pasa.

### 6.3 U3 · Dispositivos virtuales (el MCU como anfitrión)

El puente es un `usb_dev_if` que el OTG enumera:

* **Pendrive:** MSC con *Bulk-Only Transport* y los comandos SCSI mínimos
  (`INQUIRY`, `READ CAPACITY`, `READ(10)`, `WRITE(10)`, `TEST UNIT READY`,
  `REQUEST SENSE`) sobre un **fichero de imagen** del ordenador (una FAT32 que
  el alumno prepara con `mkfs.fat` o que se da hecha). El firmware de CubeIDE
  con FatFs la monta; después, el alumno ve en el fichero lo que escribió su
  MCU.
* **Teclado:** HID de arranque (*boot protocol*), con las pulsaciones que llegan
  por TCP.

`UsbDeviceRig` es el punto de partida.

### 6.4 U4 · Cliente USB/IP (el MCU como anfitrión, dispositivo real)

`mcu-sim` importa un dispositivo exportado por `usbipd` (Linux) o usbipd-win
(Windows) y se lo presenta al OTG: el MCU simulado lee **el pendrive o el
teclado de verdad** del alumno. Potente, pero raro en clase y con la
complicación de ceder el dispositivo; queda como opcional.

---

## 7. Herramientas gratuitas para uso docente

Consultado el 30-09-2026.

| Herramienta | Qué da | Linux | Windows | macOS | Licencia |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **usbip** (`vhci-hcd`, `usbip`, `usbipd`) | Cliente y servidor USB/IP | ✅ (núcleo) | — | — | GPL-2.0 |
| **usbip-win2** | Cliente USB/IP con drivers firmados | — | ✅ 10 1903+, 11 | — | BSD-2 |
| **usbipd-win** | Servidor USB/IP (exporta dispositivos de Windows) | — | ✅ | — | Libre (**comprobar la licencia**) |
| **usbip-macos** | Cliente USB/IP | — | — | ⚠ experimental, *entitlement* o SIP desactivado | — |
| **libusb** / **pyusb** | Hablar con un dispositivo desde un programa | ✅ | ✅ (WinUSB) | ✅ | LGPL-2.1 / BSD |
| **Wireshark** + **usbmon** | Capturar USB | ✅ | ✅ (USBPcap) | ⚠ | GPL-2.0 |

---

## 8. Recomendación

1. **La conexión de piezas a periféricos del MCU** (§3.3), una vez para USB y
   Ethernet.
2. **El controlador de anfitrión del puente** (§3.2), probado solo, contra
   dispositivos de pega y contra el OTG del modelo.
3. **U2 para CDC-ACM → RFC 2217**, lo primero que ve el alumno: el ejemplo
   «USB_DEVICE → CDC» de CubeIDE se abre con `miniterm`, CoolTerm o cualquier
   receta de `doc/puente_serie.md`, y los baudios del terminal llegan a
   `CDC_Control_FS`. Las tres plataformas, sin drivers.
4. **U1, el servidor USB/IP**, para todo lo demás en Linux y Windows. En Windows
   da además un `COMn` real para el CDC sin com0com.
5. **U3, el pendrive sobre fichero de imagen**, para las prácticas de *USB Host*.
6. U4 y los puentes de clase HID y MSC de U2, solo si se piden.

---

## 9. Riesgos y preguntas abiertas

1. **La velocidad del firmware.** Una pila USB que no duerme puede ir cinco
   veces más lenta que el tiempo real. Para CDC es aceptable; para MSC por
   USB/IP, con el sistema leyendo miles de bloques al montar, puede ser
   desesperante. Se mide en U0.
2. **Solo velocidad completa (F-38).** El firmware HS funciona a 12 Mbit/s. Hay
   que decirlo.
3. **Nada isócrono (F-40, y USB/IP lo complica aún más).** Audio USB, fuera.
4. **El conmutador de datos, el CRC y los reintentos no están modelados (T-17).**
   Un firmware con un fallo de conmutador DATA0/DATA1 funcionará aquí y fallará
   en la placa. Es la frontera deliberada del modelo y hay que recordarlo.
5. **Descriptores que los sistemas piden y los ejemplos no siempre dan**
   (`DEVICE_QUALIFIER`, descriptores de Microsoft OS, cadenas en otros
   idiomas): el firmware contesta STALL y el sistema sigue, como con la placa;
   pero hay que probarlo con el código tal como lo genera CubeIDE.
6. **USB/IP pide `root` o administrador** para el `attach`, cada vez. Aceptable
   para el profesor, pesado para el alumno: por eso U2 va primero.
7. **macOS se queda sin U1.** Lo que haga macOS tiene que pasar por U2.
8. **La placa.** La F4 Discovery lleva el OTG_FS en un micro-AB (PA9 a PA12);
   las Nucleo-64 como la F446RE no tienen USB de usuario, solo el del ST-LINK.
   El ejemplo natural es la Discovery.

---

## 10. Plan de implementación (propuesta)

Mismo método que el puente serie: fases pequeñas con su criterio de cierre, un
banco propio (`testusb`) con su invariante, y **sin mover los invariantes de
`test407`, `test446` y `test417`**.

| Fase | Contenido | Criterio de cierre |
| :--- | :--- | :--- |
| **U0 · Medir** | El ejemplo «USB_DEVICE → CDC» de CubeIDE para la F4 Discovery, compilado tal cual, enumerado desde un banco en C++ con `UsbHostRig` | Cuánto va la CPU frente al tiempo real durante la enumeración y con datos, y la lista de peticiones que el firmware contesta y cuáles con STALL |
| **U1 · La pieza llega al OTG** | La conexión pieza → periférico del MCU (compartida con Ethernet); la parte eléctrica con `UsbHostRig` dentro | Una placa XML con la pieza enumera el OTG del MCU desde `sim_main`; `test407` idéntico |
| **U2 · Controlador de anfitrión** | Transferencias de control, *bulk* e interrupción sobre `transaccion()`, con NAK, paquetes cortos, SOF y reset | Pruebas contra un dispositivo de pega y contra el OTG con un firmware de prueba versionado, como `vcp_demo` |
| **U3 · CDC → RFC 2217** | El puente de clase CDC-ACM sobre el canal del puente serie (`LineaSerie` → `SET_LINE_CODING` / `SET_CONTROL_LINE_STATE`) | Eco del ejemplo de CubeIDE con pySerial `rfc2217://` en el CI de las cuatro plataformas; el cambio de baudios del terminal llega a `CDC_Control_FS` |
| **U4 · Servidor USB/IP** | Lista, importación, `SUBMIT`/`RET_SUBMIT`/`UNLINK` sobre el controlador de U2 | En el CI de Linux (con `vhci-hcd`): `usbip attach` y eco por `/dev/ttyACM0`. A mano: usbip-win2 en Windows 10 y 11 da un `COMn` |
| **U5 · Pendrive virtual** | MSC sobre un fichero de imagen, con los comandos SCSI mínimos | El ejemplo «USB_HOST → MSC» con FatFs escribe un fichero que se lee después en la imagen, desde el ordenador |
| **U6 · Recetas y placas** | `placas/discovery_usb.xml`, `doc/puente_usb.md` | Un alumno sigue la receta y ve el `printf` por USB de su firmware |
| **U7 · Opcionales** | Puentes de clase HID y MSC, teclado virtual, cliente USB/IP | Según se pidan |

Orden: U0 → U1 → U2 → U3 → (U4, U5 en paralelo) → U6. U1 se hace a la vez que
la E1 del puente Ethernet, o antes, porque es el mismo mecanismo.

**Tamaño estimado:** algo menos que P-14 para U0-U3 y U6 (el modelo no cambia y
el canal RFC 2217 ya existe); U4 es un protocolo nuevo, pero pequeño y bien
documentado; U5 es un subconjunto de SCSI.

---

## Fuentes

* Código y documentación de `mcu-sim`: `src/periph/otg.h`,
  `src/parts/ext_parts.h` (`UsbHostRig`, `UsbDeviceRig`),
  `src/parts/part_factory.h`, `src/top/sc_main.cpp` (T115, T116),
  `src/top/mcu_caps.h`, `doc/stm32f4xx/stm32f407vg_fase7_otg.md`,
  `doc/todo.md` (F-37 a F-41, T-17), `doc/coste_simulacion.md`,
  `doc/analisis_puente_serie.md`.
* [USB/IP protocol](https://docs.kernel.org/usb/usbip_protocol.html) (documentación del núcleo de Linux).
* [usbip-win2](https://github.com/vadimgrn/usbip-win2).
* [usbip-macos](https://github.com/carlossless/usbip-macos).
