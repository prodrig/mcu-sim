# e4_eth — eco TCP/UDP con lwIP sobre el EMAC del MSP432E401Y (T7)

El ejemplo `enet_lwip` de TivaWare reducido a lo que se puede comprobar
desde un banco: **lwIP 2.2.1** sin sistema operativo (`NO_SYS`), el EMAC con
su PHY interno por DriverLib y dos servicios en el puerto 7, el eco TCP
(RFC 862) y el eco UDP. Contesta a `ping`. IP fija **192.168.127.2/24**,
puerta 192.168.127.1: la subred de gvproxy. MAC 02:00:00:E4:00:01.

* `main.c` — la aplicación: los dos ecos con la API cruda de lwIP, el
  SysTick de 1 ms para `sys_now()`, y el buzón en 0x2000_0000.
* `emac_lwip.c` — el EMAC como `netif`, escrito para este ejemplo (no es el
  `tiva-tm4c129.c` de TI): descriptores encadenados de 8 palabras, 8 de
  recepción y 4 de transmisión, y las sumas de salida por el EMAC
  (`DES0_TX_CTRL_IP_ALL_CKHSUMS`); el enlace, de `EPHY_BMSR` cada 100 ms.
* `lwipopts.h`, `arch/cc.h` — la configuración y el puerto; `libc_min.c`,
  las cinco funciones de C que pide lwIP (los firmwares van sin newlib).

Lo prueban `teste4` (grupo E33, con el otro extremo del cable en el banco) y
`make eth-e4` (`verif/eth/eco_e4.py`, por el puente Ethernet con el protocolo
de QEMU, contra el `mcu-sim` de verdad).

## lwIP

`lwip/` es un subconjunto de **lwIP 2.2.1** (`STABLE-2_2_1_RELEASE`, commit
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`, de
<https://github.com/lwip-tcpip/lwip>): `src/core` sin IPv6, `src/include`
(sin `lwip/apps` ni `compat`) y `src/netif/ethernet.c`, sin tocar. Licencia
**BSD de tres cláusulas**, en `lwip/COPYING` y en la cabecera de cada fichero.
