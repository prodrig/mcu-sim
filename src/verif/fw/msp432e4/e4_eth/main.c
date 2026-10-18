/* ===========================================================================
 * e4_eth — Un eco TCP y UDP con lwIP sobre el EMAC del MSP432E401Y (T7)
 *
 * El ejemplo «enet_lwip» de TivaWare reducido a lo que se puede comprobar:
 * lwIP 2.2.1 sin sistema operativo (NO_SYS), el EMAC con su PHY interno
 * (DriverLib: EMACPHYConfigSet, EMACInit, EMACConfigSet...), y dos servicios
 * en el puerto 7: el eco TCP (RFC 862) y el eco UDP. Contesta tambien a ping.
 *
 * La direccion es FIJA: 192.168.127.2/24, puerta 192.168.127.1, que es la
 * que da gvproxy (doc/analisis_puente_ethernet.md, §5.2) y la que usa la
 * prueba de verif/eth/eco_e4.py. La MAC es 02:00:00:E4:00:01 (administrada
 * localmente: el chip de verdad la lee de USER0/USER1).
 *
 * El buzon (0x2000_0000):
 *   0x08 estado     10 EMAC en marcha, 100 con enlace (y la IP fija)
 *   0x0C sysclk
 *   0x10 conexiones TCP aceptadas      0x14 bytes devueltos por TCP
 *   0x18 datagramas UDP devueltos      0x1C tramas recibidas
 *   0x20 tramas enviadas               0x24 linea de un LWIP_ASSERT, si salta
 *   0x28 PHY: EPHY_ID1 << 16 | EPHY_ID2
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"
#include "emac_lwip.h"

typedef struct {
    volatile uint32_t modo, arranques, estado, sysclk;
    volatile uint32_t conexiones, bytes_tcp, udp, rx, tx, fallo, phy_id;
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static volatile uint32_t g_ms;
void SysTick_Handler(void) { g_ms++; }
uint32_t sys_now(void) { return g_ms; }

void e4_eth_fallo(int linea) { buzon.fallo = (uint32_t)linea; for (;;) { } }
uint32_t e4_eth_azar(void) {
    static uint32_t x = 0x12345678u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x ^ g_ms;
}

/* --- El eco TCP (RFC 862), con la API cruda ------------------------------ */
static err_t eco_recibe(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (p == NULL) { tcp_close(pcb); return ERR_OK; }
    if (err != ERR_OK) { pbuf_free(p); return err; }
    /* Lo que llega se devuelve tal cual, si cabe en la ventana de envio */
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (tcp_write(pcb, q->payload, q->len, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        buzon.bytes_tcp += q->len;
    }
    tcp_output(pcb);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static err_t eco_acepta(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg; (void)err;
    buzon.conexiones++;
    tcp_recv(pcb, eco_recibe);
    return ERR_OK;
}

/* --- El eco UDP ------------------------------------------------------------ */
static void eco_udp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                    u16_t port) {
    (void)arg;
    udp_sendto(pcb, p, addr, port);
    buzon.udp++;
    pbuf_free(p);
}

static struct netif g_netif;

int main(void) {
    buzon.arranques++;
    const uint32_t clk = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL |
                                            SYSCTL_CFG_VCO_480, 120000000);
    buzon.sysclk = clk;
    SysTickPeriodSet(clk / 1000);
    SysTickIntEnable();
    SysTickEnable();
    IntMasterEnable();

    lwip_init();
    ip4_addr_t ip, mascara, puerta;
    IP4_ADDR(&ip, 192, 168, 127, 2);
    IP4_ADDR(&mascara, 255, 255, 255, 0);
    IP4_ADDR(&puerta, 192, 168, 127, 1);
    emac_lwip_reloj(clk);
    netif_add(&g_netif, &ip, &mascara, &puerta, NULL, emac_lwip_init, ethernet_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);
    buzon.estado = 10;
    buzon.phy_id = emac_lwip_phy_id();

    struct tcp_pcb *t = tcp_new();
    tcp_bind(t, IP_ADDR_ANY, 7);
    t = tcp_listen(t);
    tcp_accept(t, eco_acepta);
    struct udp_pcb *u = udp_new();
    udp_bind(u, IP_ADDR_ANY, 7);
    udp_recv(u, eco_udp, NULL);

    for (;;) {
        emac_lwip_sondea(&g_netif);
        sys_check_timeouts();
        buzon.rx = emac_lwip_rx();
        buzon.tx = emac_lwip_tx();
        if (netif_is_link_up(&g_netif)) buzon.estado = 100;
        else if (buzon.estado == 100) buzon.estado = 10;
        /* A dormir hasta la siguiente interrupcion: el EMAC o el SysTick */
        if (!emac_lwip_pendiente()) __asm volatile ("wfi");
    }
}
