# Puente entre el Ethernet del MCU y la red del ordenador

**Análisis de opciones para `mcu-sim`** · 30-09-2026

La pregunta: qué opciones hay para construir, como se hizo con el puerto serie
(`doc/analisis_puente_serie.md`, P-14), un elemento **externo al MCU** que
conecte el Ethernet del chip simulado con la red del ordenador donde corre
`mcu-sim`. Que el alumno pueda hacer `ping` a su placa, abrir en el navegador la
página que sirve lwIP, recibir su telemetría por UDP o enchufar dos simuladores
entre sí. Y qué cambia en cada plataforma que el proyecto soporta (Linux x86-64,
Windows x86-64 con MinGW, macOS arm64 y x86-64).

---

## 0. Resumen

* **El obstáculo principal no es el sistema operativo, es el coste del
  modelo.** Hoy el MAC habla con el PHY por los pines, bit a bit, con los
  relojes del camino de datos: el REF_CLK de 50 MHz de RMII. **Medido**: con el
  `EthPhy` conectado, 10 ms simulados tardan **8,1 s** de anfitrión (sin él,
  0,002 s); es **unas 800 veces más lento que el tiempo real**, y eso solo por
  el reloj, sin tráfico. Un puente en los pines nunca podría contestar a un
  `ping` en su segundo de plazo. **Hace falta una frontera de tramas**
  («PHY transaccional») entre el MAC y el puente, con el tiempo de cable
  calculado y no simulado bit a bit (§3).
* **En el lado del host no hay nada portátil a nivel 2.** Un TAP exige
  privilegios en Linux, un driver en Windows (TAP-Windows6, o Npcap con su
  licencia de cinco equipos) y en macOS no existe desde que Apple retiró las
  extensiones de núcleo: vmnet necesita `root` o un *entitlement* de Apple
  (§5). Lo que sí es portátil es, otra vez, **TCP**.
* **Hay un protocolo de facto para llevar tramas Ethernet por TCP**: el de
  `-netdev stream` de QEMU, una trama por mensaje con su longitud delante en
  32 bits *big-endian*. Lo hablan herramientas libres que ponen al otro lado una
  red entera en espacio de usuario, **sin privilegios**, con NAT, DHCP, DNS y
  reenvío de puertos: **gvproxy** (gvisor-tap-vsock, Apache-2.0, Linux, macOS y
  Windows) y **passt** (solo Linux). Es la arquitectura D del puente serie
  trasladada a Ethernet (§6).
* **Recomendación (§8):** `mcu-sim` expone el Ethernet del MCU como tramas por
  TCP con el protocolo de QEMU —servidor y cliente, como `tcp:` y
  `tcp-cliente:` del puente serie— y la red la pone gvproxy. Con eso, en las
  tres plataformas y sin instalar drivers, el firmware obtiene IP por DHCP, sale
  a internet y el alumno abre su página web por un puerto reenviado. Y **una
  captura para Wireshark** desde el primer día, en fichero y en directo
  (*PCAP-over-IP*), que no depende de nada.
* **Lo que esta vía no da:** hacer `ping` **desde** el ordenador a la IP de la
  placa. Con una red de usuario el ordenador solo llega a la placa por puertos
  reenviados; para verla como una máquina más de la red hace falta un TAP
  (Linux) o vmnet (macOS, como `root`), y eso queda como opción avanzada.
* **Dos simuladores conectados** salen gratis con el mismo protocolo: uno
  escucha y el otro se conecta, y es un cable cruzado entre los dos MACs.

---

## 1. Qué hay en `mcu-sim` que condiciona el diseño

### 1.1 El MAC

`periph/eth_mac.h` modela el MAC y su DMA del F407/F417 (`EthBase`, alias
`EthMac`). Es un modelo **a nivel de pines**:

* **Todas las señales del camino de datos son pines**, por el mux de funciones
  alternativas (AF11, `soc/f4_mapa_af.h`): TX_EN, TXD0..3, RXD0..3, RX_DV, los
  relojes, MDC y MDIO. Solo se cablean si el chip tiene Ethernet: el F407 y el
  F417 sí; el F405, el F415 y todo el F446 no.
* **Los relojes los pone el PHY**, como en la placa: el MAC no genera ninguno.
  Transmite en el flanco de bajada de TX_CLK (MII) o REF_CLK (RMII) y recibe en
  el de subida. MII o RMII se elige en vivo con SYSCFG_PMC.
* **Transmitir:** el DMA recorre los descriptores por su maestro AHB, la trama
  sale con preámbulo, SFD y FCS, nibble a nibble o dibit a dibit, y detrás van
  96 tiempos de bit de hueco. Sin reloj del PHY, el DMA marca *underflow*.
* **Recibir:** busca el SFD mientras RX_DV está alto, comprueba y quita el FCS,
  pasa los filtros (promiscuo, difusión, *hash*, cuatro direcciones exactas) y
  llena los descriptores de recepción.
* **MDIO** va de verdad, 32 bits por los pines.
* **La velocidad no es un registro, es el reloj:** `MACCR.FES` y `DM` se
  guardan, y el MAC sigue el reloj que le den.

Lo que no está modelado (`doc/todo.md`): PAUSE (F-42), el *checksum offload*
(F-43), semidúplex y colisiones (F-44), descriptores de 8 palabras (F-45), PPS
y alarmas de PTP (F-46), VLAN (F-47), la mayoría de los contadores MMC (F-48) y
la capa eléctrica y la autonegociación (T-18). Ninguno impide un puente. El
*checksum offload* sí importa en la práctica: lwIP de CubeIDE lo usa si está
activado en `lwipopts.h`, y entonces las tramas saldrían con las sumas de IP,
UDP y TCP a cero (§9).

### 1.2 El PHY

`EthPhy` (`parts/ext_parts.h`) es un PHY genérico, sin marca (ni LAN8742 ni
DP83848): registros de la norma por MDIO, identificador 0x0007/0xC0F1, y el
camino de datos. **Es un «buzón de tramas»**: `enviar(trama)` pone una en la
cola de recepción del MAC, con preámbulo y FCS, y `recibidas()` devuelve las que
ha transmitido el MAC. Es exactamente la interfaz que un puente necesitaría.

El problema es cómo lo hace: **un hilo de SystemC mueve los dos relojes todo el
rato**, a 10 ns de semiperiodo en RMII a 100 Mbit/s, esté o no pasando algo. Y
cada flanco toca nodos analógicos, que despiertan al MAC.

### 1.3 Lo que cuesta, medido

Con el mismo firmware (`vcp_demo`, que duerme en `WFI` casi todo el tiempo) y
la misma placa, con y sin el `EthPhy` puesto, en la máquina de desarrollo:

| Montaje | 10 ms simulados tardan | Deltas | Frente al tiempo real |
| :--- | ---: | ---: | ---: |
| Sin PHY | 0,002 s | 2 546 | 5 000 veces más rápido |
| Con `EthPhy` (RMII, 100 Mbit/s), **sin tráfico** | **8,111 s** | 5 201 250 | **811 veces más lento** |

Ninguna pila de red aguanta eso. Un `ping` espera un segundo por respuesta; a
este ritmo, el MCU tarda trece minutos en vivir ese segundo. En 10 Mbit/s el
reloj va diez veces más despacio, y seguiría siendo unas ochenta veces más lento
que el tiempo real. **El puente no puede colgarse de los pines.**

(Tampoco es un defecto del modelo: `EthPhy` existe para verificar el MAC bit a
bit, como hacen T117-T120 en `test407`, y ahí ese detalle es el objetivo.)

### 1.4 La CPU

El resto del coste lo pone el firmware. `doc/chat.md` tiene medido que un
firmware que no duerme nunca va **unas cinco veces más lento** que el tiempo
real (el núcleo se sincroniza cada µs), y uno que duerme en `WFI`, decenas de
veces más rápido. Una pila lwIP con FreeRTOS pasa casi todo el tiempo esperando,
así que es razonable esperar que vaya al ritmo del reloj con `--tiempo-real`;
**hay que medirlo** con un ejemplo de CubeIDE antes de prometerlo (§10, E0).

### 1.5 Sockets

`common/red.h` sigue siendo el único fichero que sabe de sistemas operativos.
Tiene lo que haría falta para un puente por TCP —escuchar en `localhost`,
conectar sin bloquear (D8 del puente serie)— y nada de TAP, pcap ni vmnet.

---

## 2. Qué se quiere, en clase

| Caso | Qué hace el firmware | Qué necesita el puente |
| :--- | :--- | :--- |
| **Salir a la red** | DHCP, DNS, un cliente HTTP o MQTT, NTP | Una red con DHCP y NAT hacia internet |
| **Servir algo** | El servidor web de lwIP, un eco TCP o UDP | Que el ordenador llegue a un puerto del MCU |
| **`ping` a la placa** | Solo contestar a ICMP | Que el ordenador vea la IP del MCU: nivel 2 en el host |
| **Dos placas** | Cliente y servidor, o dos nodos de un protocolo | Un cable entre dos simuladores |
| **Ver el tráfico** | Cualquiera | Wireshark |

Los cuatro primeros son **conectividad**, y el último **observación**. Conviene
tratarlos por separado: la observación no depende de nada del sistema y es la
que más enseña (§6.4).

---

## 3. Lado MCU: dónde se engancha el puente

### 3.1 Las tres fronteras posibles

| | Frontera | Cómo | Coste | Veredicto |
| :--- | :--- | :--- | :--- | :--- |
| **E1** | Los pines, con el `EthPhy` | El puente es un `EthPhy` con un canal | 800 veces el tiempo real (§1.3) | **No** |
| **E2** | Un PHY que pone el reloj **solo cuando hay algo** | El PHY para los relojes en reposo y los arranca cuando llega una trama o cuando el MAC va a transmitir | El MAC espera el reloj 4 µs y, si no llega, da *underflow*; habría que avisar al PHY desde el MAC | Frágil: cambia la semántica de los pines para ganar solo en reposo |
| **E3** | **Tramas entre el MAC y el PHY**, con el tiempo de cable calculado | El MAC gana un «puerto de tramas»: `emite_trama()` y `recoge_trama()` lo usan en vez de los pines cuando no hay PHY en los pines sino un PHY de tramas | Una espera por trama (123 µs para 1518 bytes a 100 Mbit/s), no una por bit | **Sí** |

**E3 es la única que puede ir en tiempo real.** No es un atajo nuevo en el
proyecto. Es la misma frontera que ya tiene el USB (`periph/otg.h`: lo eléctrico
en los pines, los paquetes como paquetes, sin NRZI ni *bit stuffing*) y la que
tiene el propio MAC con su bucle interno (`MACCR.LM`, que llama a `dma_recibe()`
sin pasar por los pines).

### 3.2 Cómo sería E3

* **Una interfaz pequeña**, como `usb_dev_if` en el USB:

  ```cpp
  class eth_phy_if {                       // lo que el MAC ve del otro lado
  public:
      virtual void trama_del_mac(const std::vector<uint8_t>& t) = 0;  // sin FCS
      virtual bool enlace() const = 0;
      virtual bool cien() const = 0;
  };
  // y el MAC gana: void conectar_phy(eth_phy_if*); void trama_al_mac(t);
  ```

* **El MAC decide por dónde sale**: si hay un `eth_phy_if` conectado, la trama
  va por él y se espera su tiempo de cable (preámbulo, trama, FCS y hueco a 10 o
  100 Mbit/s); si no, por los pines, como hoy. **Con nada conectado el MAC hace
  exactamente lo mismo que ahora**, que es la condición para que `test407` no
  mueva su invariante (la misma disciplina que la D1 del puente serie).
* **MDIO:** el firmware lo usa al arrancar para leer el identificador del PHY y
  el estado del enlace. Tiene dos salidas: dejarlo en los pines (el PHY de
  tramas contesta MDIO por MDC/MDIO, que solo corren cuando el firmware
  pregunta, y cuesta poco), o llevar también los registros por la interfaz. La
  primera no toca el MAC y es la recomendable.
* **El identificador del PHY** importa más de lo que parece: el código que
  genera CubeIDE trae un *driver* para un PHY concreto (el LAN8742A de las
  Nucleo-144 con Ethernet; la base STM32F4DIS-BB de la Discovery lleva un
  LAN8720A, según sus esquemas) y puede **comprobar su identificador**. El PHY
  de tramas debería poder presentarse como uno de ellos (los valores de
  PHYID1/PHYID2, de sus hojas de datos; **hay que comprobarlos**) o seguir
  siendo el genérico.
* **Las tramas se entregan sin FCS**: el puente no tiene por qué calcular
  CRC-32 si nadie va a mirarlo. Una opción de `fcs_mala` para las pruebas, como
  hoy en `EthPhy::enviar()`, sigue siendo útil.

### 3.3 El nuevo problema de la placa

Hoy una pieza del XML solo puede agarrar **nodos** de la placa y **otras
piezas**; no puede agarrar un periférico del MCU (lo dice la factoría,
`parts/part_factory.h`). Un PHY de tramas tiene que llegar al `EthMac` del chip.
Es el mismo problema que el puente USB (`doc/analisis_puente_usb.md` §3.3), y
conviene resolverlo una vez para los dos: por ejemplo, que `sim_main.cpp`, tras
montar la placa, busque las piezas que declaran «quiero el periférico X del MCU
Y» y se lo dé, como ya hace con los `PuenteSerie` para su canal.

---

## 4. Acoplamiento de tiempos

* **El cable a 100 Mbit/s** son **123 µs** por trama de 1 518 bytes (con
  preámbulo, SFD y hueco, 1 538 bytes de línea): como mucho unas 8 100 tramas
  grandes por segundo.
  La red de un portátil va, en ráfagas, mucho más deprisa que eso. Así que el
  puente necesita **una cola hacia el MCU con límite**, y lo que no quepa se
  **descarta**, como hace un PHY de verdad cuando el MAC no tiene descriptores
  libres (el MAC ya cuenta esas pérdidas).
* **Hacia el host no hay que frenar nada:** el MCU no llega a llenar un socket.
* **`--tiempo-real` es obligatorio**, por lo mismo que en el puerto serie, y
  aquí con más razón: TCP mide tiempos de ida y vuelta y retransmite; ARP y DHCP
  tienen plazos. Si el simulador va más deprisa que el reloj, los plazos del
  firmware vencen antes; si va más despacio (una CPU muy ocupada), los del
  ordenador. **Los dos casos existen**, y el segundo es el que da sorpresas:
  lwIP funcionando bien en la placa y perdiendo conexiones aquí.
* **Determinismo:** como en `testserie`, el banco del puente espera en tiempo de
  pared a que el canal tenga dentro lo que hizo el cliente antes de dejar
  avanzar la simulación. El modelo de sincronización de la D5 (contar los bytes
  de los dos lados) se reutiliza tal cual.

---

## 5. Lado host: los mecanismos, plataforma por plataforma

### 5.1 Nivel 2 en el propio sistema (TAP y compañía)

Es lo que usan QEMU, VirtualBox o un contenedor para dar a una máquina virtual
una tarjeta de red «de verdad»: el sistema ve una interfaz más, con su MAC, a la
que se puede hacer `ping`, poner en un puente o capturar.

| Sistema | Mecanismo | Precio |
| :--- | :--- | :--- |
| **Linux** | `/dev/net/tun` en modo TAP | Crear la interfaz necesita `CAP_NET_ADMIN`; se puede hacer una vez como `root` y dejarla para el usuario (`ip tuntap add dev tap0 mode tap user $USER`). Configurarla (IP, puente) también es de administrador |
| **Linux** | Socket `AF_PACKET` sobre una interfaz existente | `CAP_NET_RAW`; y compartir la tarjeta del portátil con el MCU es mala idea en una red de campus |
| **Windows** | **TAP-Windows6** (el de OpenVPN) | Un driver firmado; instalarlo es cosa de administrador. Libre |
| **Windows** | **Npcap** | Driver de captura e inyección. Gratis en **hasta 5 equipos** y sin redistribución (salvo con Wireshark o Nmap); un aula grande queda fuera sin licencia OEM |
| **Windows** | Wintun | Solo nivel 3 (sin Ethernet): no sirve para un MAC |
| **macOS** | **Nada nativo a nivel 2 para un usuario**. Los `utun` son de nivel 3; los TAP de terceros eran extensiones de núcleo que Apple ha dejado sin sitio | — |
| **macOS** | **vmnet.framework** | Exige ejecutarse como `root` o el *entitlement* `com.apple.vm.networking`, que Apple solo concede por contrato. **socket_vmnet** (Apache-2.0, `brew install socket_vmnet`) es un demonio de `root` que lo expone por un socket UNIX |

**Conclusión:** nivel 2 en el host es posible en las tres plataformas, pero en
las tres pide **administrador** una vez, y en macOS un demonio de `root`
permanente. Para una asignatura con portátiles de alumnos es una barrera de
entrada alta, y cada sistema necesita su propio código en `red.h`.

### 5.2 Una red entera en espacio de usuario

La otra forma, la de las máquinas virtuales «sin privilegios» (el `-netdev
user` de QEMU, Podman en macOS y Windows): un proceso implementa una red
completa —puerta de enlace, DHCP, DNS y NAT— y traduce las conexiones del
invitado a sockets normales del sistema. **No necesita privilegios ni drivers.**

| Herramienta | Plataformas | Licencia | Cómo se habla con ella | Notas |
| :--- | :--- | :--- | :--- | :--- |
| **gvproxy** (gvisor-tap-vsock) | Linux, macOS, Windows | Apache-2.0 | `-listen-qemu`: tramas por TCP o socket UNIX con el protocolo de QEMU | La pila de red de gVisor. Subred 192.168.127.0/24, puerta de enlace .1, **DHCP** da la .2, DNS. **Reenvío de puertos** por una API HTTP (`/services/forwarder/expose`). Captura a pcap (`-pcap`). Es la red de Podman en macOS y Windows |
| **passt** | Linux | GPL-2.0+ | Socket UNIX con el protocolo de QEMU | Muy ligero; copia la configuración de red del anfitrión |
| **libslirp** | Las tres (como biblioteca) | BSD-3 | Se enlaza dentro del programa | La red de usuario de QEMU. **Depende de GLib**: meterla en el ejecutable estático de Windows arrastra GLib entera |

### 5.3 El protocolo de QEMU

`-netdev stream` (y el antiguo `-netdev socket`): sobre un socket de flujo,
**cada trama va precedida de su longitud en 32 bits *big-endian***; sobre
datagramas, sin longitud. Sin negociación, sin opciones, sin estado. Es el
Ethernet equivalente del «TCP en crudo» del puente serie, y lo hablan gvproxy,
passt, socket_vmnet y el propio QEMU.

### 5.4 Observar: pcap y Wireshark

* **Un fichero pcap** es una cabecera de 24 bytes y, por trama, 16 bytes de
  cabecera y los datos. Escribirlo no depende de ningún sistema.
* **En directo**: Wireshark sabe conectarse a un servidor que le va dando un
  flujo pcap por TCP (*PCAP-over-IP*): de interfaz se escribe
  `TCP@127.0.0.1:PUERTO`. Funciona en Windows, Linux y macOS sin instalar Npcap
  para eso (**comprobar** en Windows que no lo pida el instalador para esa vía).
  Con eso el alumno ve en Wireshark, en tiempo real, lo que manda y recibe su
  MCU, con el tiempo simulado en la marca de tiempo.

---

## 6. Dónde vive el lado host: cuatro arquitecturas

### 6.1 A · Red de usuario dentro de `mcu-sim` (libslirp)

`mcu-sim` enlaza libslirp y ofrece `host="nat"`: el MCU tiene DHCP e internet y
el reenvío de puertos se da en el XML o con `--eth-puerto 8080:80`.

* **A favor:** un solo programa; nada que instalar.
* **En contra:** GLib dentro de un ejecutable que hoy solo depende de SystemC;
  el proyecto pasa a mantener la integración de una pila de red; y lo que haga
  libslirp con los tiempos (sus temporizadores van en tiempo de pared) hay que
  casarlo con el simulado.

### 6.2 B · Tramas por TCP (protocolo de QEMU) y la red fuera

La arquitectura D del puente serie. `mcu-sim` solo expone el Ethernet del MCU
como tramas por TCP, con el protocolo de QEMU, **como servidor o como cliente**
(`qemu:PUERTO`, `qemu-cliente:HOST:PUERTO`, con el mismo código de conexión que
`tcp-cliente` de la D8). Lo que hay al otro lado lo decide el alumno:

| Al otro lado | Resultado | Plataformas |
| :--- | :--- | :--- |
| **gvproxy** | DHCP, DNS, internet por NAT, puertos reenviados hacia el MCU | Las tres |
| **passt** | Lo mismo, más ligero | Linux |
| **socket_vmnet** | El MCU en una red de vmnet, visible desde el Mac | macOS, con el demonio de `root` |
| **otro `mcu-sim`** | Un cable cruzado entre dos MCUs | Las tres |
| **QEMU** (una máquina virtual Linux) | El MCU y una máquina Linux en la misma red: un servidor MQTT, un `tcpdump` de verdad | Las tres |
| **un programa del banco** | Tramas inyectadas y recogidas en las pruebas | Las tres (CI) |

* **A favor:** ni una línea de código de SO nueva; todo es TCP en `localhost`,
  que ya está en `red.h`. Lo más difícil (NAT, DHCP, DNS) lo hace un programa
  mantenido por otros y usado por mucha gente. Se prueba en CI en las cuatro
  plataformas, porque gvproxy tiene binarios para las tres.
* **En contra:** un proceso más que arrancar, y un binario de Go de unos 20 MB
  que descargar. Y **sin `ping` desde el ordenador**: con NAT el ordenador solo
  llega al MCU por los puertos reenviados.

### 6.3 C · TAP directo

`mcu-sim` abre un TAP en Linux, TAP-Windows6 en Windows y vmnet en macOS.

* **A favor:** el MCU es una máquina más de la red del ordenador; `ping`,
  `arp -a` y Wireshark sobre la interfaz, como con una placa de verdad.
* **En contra:** tres implementaciones con código de sistema operativo, las tres
  con privilegios, y en macOS `mcu-sim` tendría que correr como `root`. Rompe el
  principio de que `red.h` sea el único fichero que sabe del sistema y de que
  `mcu-sim` no necesite permisos.

Con B, el caso «TAP» se cubre **fuera** de `mcu-sim` cuando alguien lo necesite
de verdad: en Linux, `qemu-bridge-helper` o un `socat` que lleve el protocolo de
QEMU a un TAP; en macOS, socket_vmnet. Son recetas, no código del proyecto.

### 6.4 D · Solo observar: pcap

Independiente de las tres anteriores y útil con cualquiera de ellas: el puente
escribe lo que pasa por él a un fichero pcap, o lo sirve en directo por
PCAP-over-IP. **También sirve sin red ninguna**: con el puente en modo
`memoria` (las tramas que el MCU manda se guardan y se ven en Wireshark), el
alumno puede estudiar el arranque de lwIP —el DHCP DISCOVER, los ARP, los
anuncios— sin conectar nada.

---

## 7. Herramientas gratuitas para uso docente

Consultado el 30-09-2026; las licencias cambian y conviene revisarlas antes de
cada curso.

| Herramienta | Qué da | Linux | Windows | macOS | Licencia | ¿Gratis en docencia? |
| :--- | :--- | :---: | :---: | :---: | :--- | :--- |
| **gvproxy** | Red de usuario: DHCP, DNS, NAT, reenvío de puertos, pcap | ✅ | ✅ | ✅ (y MacPorts) | Apache-2.0 | Sí |
| **passt** | Red de usuario ligera | ✅ | — | — | GPL-2.0+ | Sí |
| **libslirp** | Red de usuario como biblioteca | ✅ | ✅ | ✅ | BSD-3 (+ GLib, LGPL) | Sí |
| **QEMU** | Máquinas virtuales en la misma red | ✅ | ✅ | ✅ | GPL-2.0 | Sí |
| **socket_vmnet** | vmnet para usuarios, con un demonio de `root` | — | — | ✅ | Apache-2.0 | Sí |
| **TAP-Windows6** | TAP en Windows | — | ✅ | — | Libre | Sí |
| **Npcap** | Captura e inyección en Windows | — | ✅ | — | Propietaria | **Solo hasta 5 equipos**, sin redistribución |
| **Wireshark** | Captura y análisis; PCAP-over-IP | ✅ | ✅ | ✅ | GPL-2.0 | Sí |

---

## 8. Recomendación

1. **E3, la frontera de tramas**, como requisito: sin ella no hay puente que
   vaya en tiempo real. Con el MAC sin nada conectado, `test407` idéntico al
   picosegundo.
2. **B con el protocolo de QEMU**, servidor y cliente, reutilizando `CanalTcp`
   y la conexión sin bloquear de la D8. Es el único camino que funciona igual en
   las tres plataformas sin privilegios.
3. **gvproxy como la red recomendada**, con recetas por plataforma, y otro
   `mcu-sim` como el segundo caso (dos placas).
4. **pcap y PCAP-over-IP desde la primera fase**, porque no dependen de nada y
   son lo que más enseña.
5. **Sin TAP ni libslirp en `mcu-sim`.** Se documentan como recetas externas
   para quien necesite `ping` desde el ordenador.

---

## 9. Riesgos y preguntas abiertas

1. **El *checksum offload* (F-43).** Si el `lwipopts.h` del alumno lo activa
   (`CHECKSUM_BY_HARDWARE`, lo habitual en los ejemplos de ST), lwIP deja las
   sumas de IP, UDP, TCP e ICMP a cero y espera que el MAC las ponga. El modelo
   no las pone: gvproxy descartaría todo. **O se modela el *offload* antes que
   el puente, o el puente lo hace por el MAC**; lo primero es lo correcto, y
   con la frontera de tramas es sencillo (el MAC tiene la trama entera en la
   mano antes de mandarla).
2. **El identificador y los registros del PHY** que espera el código de CubeIDE
   (el *driver* del LAN8742 en los ejemplos más comunes): si no coinciden, el *driver* no arranca y
   el enlace «no sube». Hay que probar con el código tal como lo genera CubeIDE,
   no con uno hecho a medida.
3. **La velocidad de la CPU simulada con lwIP.** Si la pila no duerme lo
   suficiente, el MCU va más despacio que el reloj y el ordenador ve
   retransmisiones. Se mide en E0, antes de construir nada.
4. **El `ping` desde el ordenador** no existe con red de usuario (§6.2). Hay que
   decirlo en la receta, o el alumno pensará que su firmware está mal.
5. **La IP fija.** Muchos ejemplos usan una IP estática (192.168.0.10, por
   ejemplo). Con gvproxy la subred es la 192.168.127.0/24: el firmware tiene que
   usar DHCP o una IP de esa subred (**comprobar** si gvproxy deja cambiar la
   subred desde la línea de órdenes).
6. **La dirección MAC.** El firmware la pone en `MACA0HR/LR`. Dos simuladores
   con la misma MAC en la misma red (el ejemplo de CubeIDE trae una fija) se
   pisan. Receta y aviso.
7. **Seguridad.** El puente escucha en `localhost`, como el serie (D-7). Con
   gvproxy, el MCU sale a internet a través del ordenador; eso no abre nada
   hacia dentro, pero conviene decirlo.

---

## 10. Plan de implementación (propuesta)

Mismo método que el puente serie: fases pequeñas, cada una con su criterio de
cierre y **sin mover los invariantes de `test407`, `test446` y `test417`**. Las
pruebas con simulación van en un banco propio (`testeth`), con su invariante.

| Fase | Contenido | Criterio de cierre |
| :--- | :--- | :--- |
| **E0 · Medir** | El ejemplo lwIP de CubeIDE para una placa con F407 (servidor web o eco), compilado tal cual, con el MAC en bucle (`MACCR.LM`) o con el `EthPhy`: cuánto va la CPU frente al tiempo real y qué pide del PHY | Una tabla con las medidas en `doc/` y la lista de lo que el firmware toca (PHY, *offload*, MMC) |
| **E1 · Frontera de tramas** | `eth_phy_if` en el MAC y un `EthPhyTramas` (MDIO en los pines, datos por la interfaz, tiempo de cable calculado, 10/100) | `test407` idéntico al picosegundo; un banco nuevo con tramas de ida y vuelta y su invariante; un ping entre dos MACs en el mismo simulador, en tiempo real |
| **E2 · *Checksum offload*** | F-43: el MAC rellena las sumas de IP, UDP, TCP e ICMP según los bits del descriptor | Vectores de referencia de tramas con las sumas calculadas por dos vías, como con CRYP y HASH |
| **E3 · pcap** | Fichero pcap y PCAP-over-IP desde el puente | La captura abre en Wireshark; `tshark` en CI la lee y cuenta las tramas |
| **E4 · Tramas por TCP** | El canal con el protocolo de QEMU, servidor y cliente; `host="qemu:PUERTO"` y `qemu-cliente:HOST:PUERTO`; `--eth ID=DESTINO` | Dos `mcu-sim` conectados con un ping entre sus MCUs; interoperabilidad con un programa de prueba que hable el protocolo |
| **E5 · gvproxy** | Recetas por plataforma; el reenvío de puertos; la prueba de interoperabilidad en CI | En las cuatro plataformas del CI: el ejemplo de CubeIDE obtiene IP por DHCP de gvproxy y un `curl` al puerto reenviado devuelve su página |
| **E6 · Placas y receta** | `placas/` con un F407 y su PHY de tramas (el catálogo no tiene el F429 de las Nucleo-144; lo cercano es la Discovery con su base de Ethernet), `doc/puente_ethernet.md` | Un alumno sigue la receta y abre la página de su MCU en el navegador |
| **E7 · Opcional: nivel 2** | Recetas para TAP en Linux y socket_vmnet en macOS, fuera de `mcu-sim` | Un `ping` desde el ordenador a la IP del MCU, a mano, anotado en una matriz como la del §10.6 del puente serie |

Orden: E0 → E1 → E2 → (E3, E4 en paralelo) → E5 → E6. E3 da valor desde el
primer día y no depende de la red. La parte de la placa (§3.3) se decide junto
con el puente USB, porque es el mismo problema.

**Tamaño estimado:** E1 y E2 son el grueso (el MAC cambia de frontera y gana el
*offload*); E3 y E4 reutilizan casi todo lo del puente serie. En conjunto, algo
más que la mitad de lo que costó P-14.

---

## Fuentes

* Código y documentación de `mcu-sim`: `src/periph/eth_mac.h`,
  `src/parts/ext_parts.h` (`EthPhy`), `src/soc/f4_mapa_af.h`,
  `src/top/mcu_caps.h`, `doc/stm32f4xx/stm32f407vg_fase7_eth.md`,
  `doc/todo.md` (F-42 a F-48, T-18), `doc/coste_simulacion.md`,
  `doc/analisis_puente_serie.md`. La medida del §1.3 se hizo el 30-09-2026 con
  `mcu-sim` a partir de `main` (`b875399`).
* [gvisor-tap-vsock / gvproxy](https://github.com/containers/gvisor-tap-vsock)
  y su [referencia de gvproxy](https://deepwiki.com/containers/gvisor-tap-vsock/2.1-gvproxy).
* [Improved UNIX socket networking in QEMU 7.2](https://john-millikin.com/improved-unix-socket-networking-in-qemu-7.2)
  (formato de `-netdev stream` y `dgram`).
* [socket_vmnet](https://github.com/lima-vm/socket_vmnet) y el
  [debate sobre el entitlement `com.apple.vm.networking`](https://github.com/orgs/Homebrew/discussions/5744).
* [Npcap](https://npcap.com/) (licencia de la edición gratuita).
* [Real-time PCAP-over-IP in Wireshark](https://www.netresec.com/?page=Blog&month=2022-05&post=Real-time-PCAP-over-IP-in-Wireshark).
