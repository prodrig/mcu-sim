/* lwipopts.h — lwIP sin sistema operativo (NO_SYS), solo IPv4, para e4_eth
 *
 * Las sumas de comprobacion de SALIDA las pone el EMAC (el descriptor lleva
 * DES0_TX_CTRL_IP_ALL_CKHSUMS): lwIP las deja a cero, como con los
 * ejemplos de TI para la TM4C129. Las de ENTRADA las comprueba lwIP. */
#ifndef E4_ETH_LWIPOPTS_H
#define E4_ETH_LWIPOPTS_H

#define NO_SYS                     1
#define SYS_LIGHTWEIGHT_PROT       0
#define LWIP_NETCONN               0
#define LWIP_SOCKET                0
#define LWIP_IPV4                  1
#define LWIP_IPV6                  0
#define LWIP_ARP                   1
#define LWIP_ETHERNET              1
#define LWIP_ICMP                  1
#define LWIP_UDP                   1
#define LWIP_TCP                   1
#define LWIP_DHCP                  0
#define LWIP_AUTOIP                0
#define LWIP_DNS                   0
#define LWIP_IGMP                  0
#define LWIP_RAW                   0
#define LWIP_STATS                 0
#define LWIP_NETIF_LINK_CALLBACK   1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_SINGLE_NETIF          1

#define MEM_ALIGNMENT              4
#define MEM_SIZE                   (16 * 1024)
#define MEMP_NUM_PBUF              16
#define MEMP_NUM_TCP_PCB           4
#define MEMP_NUM_TCP_SEG           16
#define PBUF_POOL_SIZE             12
#define PBUF_POOL_BUFSIZE          1536
#define TCP_MSS                    1460
#define TCP_WND                    (4 * TCP_MSS)
#define TCP_SND_BUF                (4 * TCP_MSS)
#define ETH_PAD_SIZE               0

#define CHECKSUM_GEN_IP            0
#define CHECKSUM_GEN_UDP           0
#define CHECKSUM_GEN_TCP           0
#define CHECKSUM_GEN_ICMP          0
#define CHECKSUM_CHECK_IP          1
#define CHECKSUM_CHECK_UDP         1
#define CHECKSUM_CHECK_TCP         1
#define CHECKSUM_CHECK_ICMP        1

#define LWIP_NOASSERT_ON_ERROR     1
/* DriverLib (emac.h) ya define htons y compania */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1
#endif
