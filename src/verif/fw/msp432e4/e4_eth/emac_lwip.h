/* emac_lwip.h — el EMAC como netif de lwIP (e4_eth) */
#ifndef E4_ETH_EMAC_LWIP_H
#define E4_ETH_EMAC_LWIP_H
#include <stdbool.h>
#include <stdint.h>
#include "lwip/netif.h"

void     emac_lwip_reloj(uint32_t clk);
err_t    emac_lwip_init(struct netif *netif);
void     emac_lwip_sondea(struct netif *netif);
bool     emac_lwip_pendiente(void);
uint32_t emac_lwip_rx(void);
uint32_t emac_lwip_tx(void);
uint32_t emac_lwip_phy_id(void);
#endif
