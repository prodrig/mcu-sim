/* ===========================================================================
 * emac_lwip.c — El EMAC del MSP432E401Y como `netif` de lwIP (e4_eth)
 *
 * Escrito para este ejemplo con DriverLib (emac.c), sin el puerto
 * tiva-tm4c129.c de TI: descriptores encadenados de 8 palabras (EMACInit pone
 * siempre ATDS), ocho de recepcion con su buffer de 1536 bytes y cuatro de
 * transmision a los que se copia la cadena de pbufs. La suma de comprobacion
 * de salida la calcula el EMAC (DES0_TX_CTRL_IP_ALL_CKHSUMS).
 *
 * El PHY es el interno: EMACPHYConfigSet con EMAC_PHY_TYPE_INTERNAL lo
 * resetea, y el enlace se mira en EPHY_BMSR cada 100 ms.
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/inc/hw_emac.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/sys.h"
#include "emac_lwip.h"

#define N_RX 8
#define N_TX 4
#define TAM_BUF 1536

static tEMACDMADescriptor g_rx[N_RX], g_tx[N_TX];
static uint8_t g_rxbuf[N_RX][TAM_BUF] __attribute__((aligned(4)));
static uint8_t g_txbuf[N_TX][TAM_BUF] __attribute__((aligned(4)));
static unsigned g_rxi, g_txi;
static uint32_t g_clk, g_n_rx, g_n_tx, g_ultimo_enlace;
static volatile bool g_aviso;
static const uint8_t MAC[6] = {0x02, 0x00, 0x00, 0xE4, 0x00, 0x01};

void EMAC0_IRQHandler(void) {
    const uint32_t s = EMACIntStatus(EMAC0_BASE, true);
    EMACIntClear(EMAC0_BASE, s);
    g_aviso = true;
}

void emac_lwip_reloj(uint32_t clk) { g_clk = clk; }
uint32_t emac_lwip_rx(void) { return g_n_rx; }
uint32_t emac_lwip_tx(void) { return g_n_tx; }
bool emac_lwip_pendiente(void) { return g_aviso; }
uint32_t emac_lwip_phy_id(void) {
    return ((uint32_t)EMACPHYRead(EMAC0_BASE, 0, EPHY_ID1) << 16) | EMACPHYRead(EMAC0_BASE, 0, EPHY_ID2);
}

static void descriptores(void) {
    for (unsigned i = 0; i < N_RX; ++i) {
        g_rx[i].ui32CtrlStatus = DES0_RX_CTRL_OWN;
        g_rx[i].ui32Count = DES1_RX_CTRL_CHAINED | (TAM_BUF << DES1_RX_CTRL_BUFF1_SIZE_S);
        g_rx[i].pvBuffer1 = g_rxbuf[i];
        g_rx[i].DES3.pLink = &g_rx[(i + 1) % N_RX];
    }
    for (unsigned i = 0; i < N_TX; ++i) {
        g_tx[i].ui32CtrlStatus = DES0_TX_CTRL_CHAINED;
        g_tx[i].ui32Count = 0;
        g_tx[i].pvBuffer1 = g_txbuf[i];
        g_tx[i].DES3.pLink = &g_tx[(i + 1) % N_TX];
    }
    g_rxi = g_txi = 0;
    EMACRxDMADescriptorListSet(EMAC0_BASE, g_rx);
    EMACTxDMADescriptorListSet(EMAC0_BASE, g_tx);
}

static err_t salida(struct netif *netif, struct pbuf *p) {
    (void)netif;
    tEMACDMADescriptor *d = &g_tx[g_txi];
    for (uint32_t t = 0; d->ui32CtrlStatus & DES0_TX_CTRL_OWN; ++t)
        if (t > 1000000u) return ERR_TIMEOUT;           /* el anillo, lleno */
    const uint16_t n = (uint16_t)pbuf_copy_partial(p, g_txbuf[g_txi], TAM_BUF, 0);
    d->ui32Count = (uint32_t)n << DES1_TX_CTRL_BUFF1_SIZE_S;
    d->ui32CtrlStatus = DES0_TX_CTRL_OWN | DES0_TX_CTRL_FIRST_SEG | DES0_TX_CTRL_LAST_SEG |
                        DES0_TX_CTRL_INTERRUPT | DES0_TX_CTRL_CHAINED |
                        DES0_TX_CTRL_IP_ALL_CKHSUMS;
    EMACTxDMAPollDemand(EMAC0_BASE);
    g_txi = (g_txi + 1) % N_TX;
    g_n_tx++;
    return ERR_OK;
}

err_t emac_lwip_init(struct netif *netif) {
    netif->name[0] = 'e'; netif->name[1] = '0';
    netif->output = etharp_output;
    netif->linkoutput = salida;
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, MAC, 6);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

    SysCtlPeripheralEnable(SYSCTL_PERIPH_EMAC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_EPHY0);
    SysCtlPeripheralReset(SYSCTL_PERIPH_EMAC0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_EMAC0)) { }
    EMACPHYConfigSet(EMAC0_BASE, EMAC_PHY_TYPE_INTERNAL | EMAC_PHY_INT_MDIX_EN |
                                 EMAC_PHY_AN_100B_T_FULL_DUPLEX);
    EMACInit(EMAC0_BASE, g_clk, EMAC_BCONFIG_MIXED_BURST | EMAC_BCONFIG_PRIORITY_FIXED, 4, 4, 0);
    EMACConfigSet(EMAC0_BASE,
                  EMAC_CONFIG_FULL_DUPLEX | EMAC_CONFIG_CHECKSUM_OFFLOAD | EMAC_CONFIG_7BYTE_PREAMBLE |
                  EMAC_CONFIG_IF_GAP_96BITS | EMAC_CONFIG_USE_MACADDR0 |
                  EMAC_CONFIG_SA_FROM_DESCRIPTOR | EMAC_CONFIG_BO_LIMIT_1024,
                  EMAC_MODE_RX_STORE_FORWARD | EMAC_MODE_TX_STORE_FORWARD, 0);
    EMACAddrSet(EMAC0_BASE, 0, MAC);
    descriptores();
    EMACFrameFilterSet(EMAC0_BASE, EMAC_FRMFILTER_SADDR | EMAC_FRMFILTER_PASS_MULTICAST |
                                   EMAC_FRMFILTER_PASS_NO_CTRL);
    EMACIntClear(EMAC0_BASE, EMACIntStatus(EMAC0_BASE, false));
    EMACTxEnable(EMAC0_BASE);
    EMACRxEnable(EMAC0_BASE);
    EMACIntEnable(EMAC0_BASE, EMAC_INT_RECEIVE | EMAC_INT_TRANSMIT);
    IntEnable(INT_EMAC0);
    return ERR_OK;
}

void emac_lwip_sondea(struct netif *netif) {
    g_aviso = false;
    /* Lo que ha llegado: cada descriptor que ya no es del DMA es una trama */
    while (!(g_rx[g_rxi].ui32CtrlStatus & DES0_RX_CTRL_OWN)) {
        tEMACDMADescriptor *d = &g_rx[g_rxi];
        const uint32_t s = d->ui32CtrlStatus;
        if (!(s & DES0_RX_STAT_ERR) && (s & DES0_RX_STAT_LAST_DESC)) {
            /* La longitud lleva la FCS: se quita */
            const uint32_t n = ((s & DES0_RX_STAT_FRAME_LENGTH_M) >> DES0_RX_STAT_FRAME_LENGTH_S) - 4;
            struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);
            if (p) {
                pbuf_take(p, g_rxbuf[g_rxi], (u16_t)n);
                if (netif->input(p, netif) != ERR_OK) pbuf_free(p);
                g_n_rx++;
            }
        }
        d->ui32CtrlStatus = DES0_RX_CTRL_OWN;
        g_rxi = (g_rxi + 1) % N_RX;
        EMACRxDMAPollDemand(EMAC0_BASE);
    }
    /* El enlace, cada 100 ms, del BMSR del PHY interno */
    if (sys_now() - g_ultimo_enlace >= 100u) {
        g_ultimo_enlace = sys_now();
        const bool sube = (EMACPHYRead(EMAC0_BASE, 0, EPHY_BMSR) & EPHY_BMSR_LINKSTAT) != 0;
        if (sube && !netif_is_link_up(netif)) netif_set_link_up(netif);
        if (!sube && netif_is_link_up(netif)) netif_set_link_down(netif);
    }
}
