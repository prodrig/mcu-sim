/* ===========================================================================
 * cargador_eth.c — El cargador por Ethernet de la ROM R1 del MSP432E4 (T7):
 *                  ROM_UpdateEMAC(ui32Clock), con BOOTP y TFTP
 *
 * Copyright (C) 2026 Pedro Rodriguez y colaboradores — AGPLv3, como el resto
 * del simulador. Escrito desde cero a partir de la DESCRIPCION del cargador
 * por Ethernet de las herramientas de TI (SPMU301, «TivaWare Boot Loader
 * User's Guide»: BOOTP para la IP, el servidor y el nombre del fichero, y
 * TFTP para traerlo) y de las RFC 951 (BOOTP), 1350 (TFTP), 826 (ARP) y 768
 * (UDP): NO lleva codigo del boot_loader de TivaWare [cargador.c].
 *
 * CUANDO CORRE. Cuando una aplicacion llama a ROM_UpdateEMAC(reloj) con el
 * reloj del sistema ya puesto (el de la aplicacion: aqui no se toca). La
 * pila pasa a ser la del cargador serie, los primeros 16 KB de la SRAM, y ahi
 * viven tambien los descriptores y los buferes del EMAC: la ROM no tiene RAM.
 *
 * LO QUE HACE.
 *   1. El EMAC con su PHY interno, por la DriverLib de esta misma ROM, y la
 *      MAC de FLASH_USERREG0/1 (los 24 bits de abajo de cada uno, como en las
 *      placas de TI); si estan en blanco, 02:00:00:E4:00:00.
 *   2. BOOTP: una peticion por difusion cada segundo (del 68 al 67) hasta que
 *      un servidor contesta con la IP (yiaddr), la del servidor TFTP (siaddr)
 *      y el nombre del fichero.
 *   3. TFTP en modo octet desde el servidor: cada bloque de 512 bytes se
 *      graba en la Flash desde 0x0, borrando cada sector de 16 KB al llegar a
 *      el, y se asiente. Un bloque de menos de 512 es el ultimo. Sin
 *      respuesta en un segundo, se repite el ultimo paquete; a los cinco, se
 *      vuelve a empezar por el BOOTP.
 *   4. Reset del sistema (SYSRESETREQ): arranca lo que se ha grabado.
 *   Contesta a ARP por su IP en cuanto la tiene. Las sumas de IP y UDP las
 *   pone el EMAC (DES0_TX_CTRL_IP_ALL_CKHSUMS).
 *
 * LO QUE NO HACE [P-17 q]: DHCP (solo BOOTP), opciones de TFTP (blksize),
 * comprobar el CRC de la imagen, ni entrar por Ethernet con la Flash en
 * blanco (el arranque solo escucha por la UART0).
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include <ti/devices/msp432e4/inc/msp432e401y.h>
#include <ti/devices/msp432e4/driverlib/types.h>
#include <ti/devices/msp432e4/driverlib/flash.h>
#include <ti/devices/msp432e4/driverlib/sysctl.h>
#include <ti/devices/msp432e4/driverlib/emac.h>
#include <ti/devices/msp432e4/driverlib/inc/hw_emac.h>

#define ST_CTRL    0xE000E010u
#define ST_RELOAD  0xE000E014u
#define ST_CURRENT 0xE000E018u
#define APINT      0xE000ED0Cu
#define USERREG0   0x400FE1E0u
#define USERREG1   0x400FE1E4u
#define SECTOR     0x4000u
#define FLASH_TAM  0x100000u
#define TAM_BUF    1536u
#define N_RX       4u

typedef struct {
    tEMACDMADescriptor rx[N_RX], tx;
    uint8_t rxbuf[N_RX][TAM_BUF] __attribute__((aligned(4)));
    uint8_t txbuf[TAM_BUF] __attribute__((aligned(4)));
    unsigned rxi;
    uint8_t mac[6], srv_mac[6];
    uint8_t ip[4], srv_ip[4];
    uint32_t xid;
    uint16_t ident;
} ctx_t;

void UpdateEMAC(uint32_t ui32Clock) __attribute__((naked, noreturn, used));
void cargador_eth(uint32_t clk) __attribute__((noreturn, used));

/* --- El tiempo: el SysTick a 1 ms, sin interrupcion --------------------- */
static void ms_arranca(uint32_t clk) {
    HWREG(ST_CTRL) = 0;
    HWREG(ST_RELOAD) = clk / 1000u - 1u;
    HWREG(ST_CURRENT) = 0;
    HWREG(ST_CTRL) = 5u;
}
static bool ms_paso(void) { return (HWREG(ST_CTRL) & 0x10000u) != 0; }   /* COUNTFLAG */

static void pon16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t de16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void copia(uint8_t* d, const uint8_t* s, unsigned n) { while (n--) *d++ = *s++; }
static bool igual(const uint8_t* a, const uint8_t* b, unsigned n) {
    while (n--) if (*a++ != *b++) return false;
    return true;
}
static void ceros(uint8_t* d, unsigned n) { while (n--) *d++ = 0; }

/* --- Una trama sale --------------------------------------------------------- */
static void sale(ctx_t* c, unsigned n) {
    while (c->tx.ui32CtrlStatus & DES0_TX_CTRL_OWN) {}
    if (n < 60) { ceros(c->txbuf + n, 60 - n); n = 60; }
    c->tx.ui32Count = n << DES1_TX_CTRL_BUFF1_SIZE_S;
    c->tx.ui32CtrlStatus = DES0_TX_CTRL_OWN | DES0_TX_CTRL_FIRST_SEG | DES0_TX_CTRL_LAST_SEG |
                           DES0_TX_CTRL_CHAINED | DES0_TX_CTRL_IP_ALL_CKHSUMS;
    EMACTxDMAPollDemand(EMAC0_BASE);
}

/* Un datagrama UDP de `n` bytes que ya estan en txbuf + 42 */
static void sale_udp(ctx_t* c, const uint8_t* mac, const uint8_t* ip, uint16_t sp, uint16_t dp,
                     unsigned n) {
    uint8_t* t = c->txbuf;
    copia(t, mac, 6); copia(t + 6, c->mac, 6); pon16(t + 12, 0x0800);
    uint8_t* h = t + 14;
    h[0] = 0x45; h[1] = 0; pon16(h + 2, (uint16_t)(28 + n)); pon16(h + 4, c->ident++);
    pon16(h + 6, 0); h[8] = 64; h[9] = 17; pon16(h + 10, 0);
    copia(h + 12, c->ip, 4); copia(h + 16, ip, 4);
    uint8_t* u = h + 20;
    pon16(u, sp); pon16(u + 2, dp); pon16(u + 4, (uint16_t)(8 + n)); pon16(u + 6, 0);
    sale(c, 42 + n);
}

/* --- Lo que llega. Contesta a ARP; devuelve la trama UDP para `puerto` --- */
static const uint8_t* llega(ctx_t* c, uint16_t puerto, unsigned* n) {
    tEMACDMADescriptor* d = &c->rx[c->rxi];
    if (d->ui32CtrlStatus & DES0_RX_CTRL_OWN) return 0;
    const uint32_t s = d->ui32CtrlStatus;
    const uint8_t* t = c->rxbuf[c->rxi];
    const unsigned len = ((s & DES0_RX_STAT_FRAME_LENGTH_M) >> DES0_RX_STAT_FRAME_LENGTH_S) - 4u;
    const uint8_t* r = 0;
    if ((s & DES0_RX_STAT_LAST_DESC) && !(s & DES0_RX_STAT_ERR) && len >= 42) {
        const uint16_t tipo = de16(t + 12);
        if (tipo == 0x0806 && de16(t + 20) == 1 && igual(t + 38, c->ip, 4) && c->ip[0]) {
            /* ARP: quien tiene mi IP. Se contesta */
            uint8_t* o = c->txbuf;
            copia(o, t + 6, 6); copia(o + 6, c->mac, 6); pon16(o + 12, 0x0806);
            pon16(o + 14, 1); pon16(o + 16, 0x0800); o[18] = 6; o[19] = 4; pon16(o + 20, 2);
            copia(o + 22, c->mac, 6); copia(o + 28, c->ip, 4);
            copia(o + 32, t + 22, 6); copia(o + 38, t + 28, 4);
            sale(c, 42);
        } else if (tipo == 0x0800 && t[23] == 17 && (t[14] & 0x0F) == 5 &&
                   de16(t + 36) == puerto) {
            *n = len;
            r = t;
        }
    }
    if (r) return r;                           /* el que llama lo devuelve */
    d->ui32CtrlStatus = DES0_RX_CTRL_OWN;
    c->rxi = (c->rxi + 1u) % N_RX;
    EMACRxDMAPollDemand(EMAC0_BASE);
    return 0;
}
static void devuelve(ctx_t* c) {
    c->rx[c->rxi].ui32CtrlStatus = DES0_RX_CTRL_OWN;
    c->rxi = (c->rxi + 1u) % N_RX;
    EMACRxDMAPollDemand(EMAC0_BASE);
}

/* --- BOOTP: la IP, el servidor y el fichero ------------------------------- */
static void bootp(ctx_t* c, char* fichero) {
    static const uint8_t DIF[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t TODOS[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    ceros(c->ip, 4);
    for (uint16_t seg = 0;; ++seg) {
        uint8_t* b = c->txbuf + 42;
        ceros(b, 300);
        b[0] = 1; b[1] = 1; b[2] = 6;                      /* BOOTREQUEST, Ethernet */
        b[4] = (uint8_t)(c->xid >> 24); b[5] = (uint8_t)(c->xid >> 16);
        b[6] = (uint8_t)(c->xid >> 8); b[7] = (uint8_t)c->xid;
        pon16(b + 8, seg);
        copia(b + 28, c->mac, 6);                          /* chaddr */
        b[236] = 99; b[237] = 130; b[238] = 83; b[239] = 99; b[240] = 255;   /* vendor: fin */
        sale_udp(c, DIF, TODOS, 68, 67, 300);
        for (unsigned ms = 0; ms < 1000u;) {
            if (ms_paso()) ++ms;
            unsigned n;
            const uint8_t* t = llega(c, 68, &n);
            if (!t) continue;
            const uint8_t* r = t + 42;
            const bool mio = n >= 42 + 236 && r[0] == 2 && r[4] == b[4] && r[5] == b[5] &&
                             r[6] == b[6] && r[7] == b[7] && igual(r + 28, c->mac, 6);
            if (mio) {
                copia(c->ip, r + 16, 4);                   /* yiaddr */
                copia(c->srv_ip, r + 20, 4);               /* siaddr */
                if (!c->srv_ip[0]) copia(c->srv_ip, t + 26, 4);   /* el que contesta */
                copia(c->srv_mac, t + 6, 6);
                for (unsigned i = 0; i < 127; ++i) fichero[i] = (char)r[108 + i];
                fichero[127] = 0;
            }
            devuelve(c);
            if (mio) return;
        }
    }
}

/* --- TFTP: el fichero, a la Flash desde 0x0 ---------------------------- */
static bool tftp(ctx_t* c, const char* fichero) {
    const uint16_t mio = (uint16_t)(1024u + (c->xid & 0x3FFFu));
    uint16_t srv = 69, esperado = 1;
    unsigned n_ult = 0;
    /* RRQ */
    uint8_t* b = c->txbuf + 42;
    pon16(b, 1);
    unsigned k = 2;
    for (unsigned i = 0; fichero[i] && k < 2 + 120; ++i) b[k++] = (uint8_t)fichero[i];
    b[k++] = 0;
    const char* modo = "octet";
    for (unsigned i = 0; modo[i]; ++i) b[k++] = (uint8_t)modo[i];
    b[k++] = 0;
    n_ult = k;
    sale_udp(c, c->srv_mac, c->srv_ip, mio, srv, n_ult);
    uint32_t dir = 0;
    for (unsigned intentos = 0; intentos < 5;) {
        bool hay = false;
        for (unsigned ms = 0; ms < 1000u && !hay;) {
            if (ms_paso()) ++ms;
            unsigned n;
            const uint8_t* t = llega(c, mio, &n);
            if (!t) continue;
            const uint8_t* u = t + 34;
            const uint8_t* d = u + 8;
            const unsigned dn = de16(u + 4) - 8u;
            if (de16(d) == 3 && de16(d + 2) == esperado && dn >= 4 && dn <= 516) {
                srv = de16(u);                             /* el TID del servidor */
                const unsigned m = dn - 4u;
                if (m) {
                    if ((dir & (SECTOR - 1u)) == 0 && dir < FLASH_TAM) FlashErase(dir);
                    uint32_t w[128];
                    uint8_t* wb = (uint8_t*)w;
                    for (unsigned i = 0; i < 512; ++i) wb[i] = i < m ? d[4 + i] : 0xFF;
                    if (dir + 512u <= FLASH_TAM) FlashProgram(w, dir, (m + 3u) & ~3u);
                    dir += m;
                }
                /* ACK */
                uint8_t* a = c->txbuf + 42;
                pon16(a, 4); pon16(a + 2, esperado);
                n_ult = 4;
                devuelve(c);
                sale_udp(c, c->srv_mac, c->srv_ip, mio, srv, n_ult);
                if (m < 512u) return true;
                ++esperado;
                hay = true;
                intentos = 0;
            } else {
                devuelve(c);
            }
        }
        if (!hay) {
            ++intentos;
            /* Se repite el ultimo: el RRQ o el ACK (siguen en txbuf + 42) */
            if (esperado == 1) {
                pon16(b, 1);
                k = 2;
                for (unsigned i = 0; fichero[i] && k < 2 + 120; ++i) b[k++] = (uint8_t)fichero[i];
                b[k++] = 0;
                for (unsigned i = 0; modo[i]; ++i) b[k++] = (uint8_t)modo[i];
                b[k++] = 0;
                n_ult = k;
            } else {
                pon16(b, 4); pon16(b + 2, (uint16_t)(esperado - 1u));
                n_ult = 4;
            }
            sale_udp(c, c->srv_mac, c->srv_ip, mio, srv, n_ult);
        }
    }
    return false;
}

void cargador_eth(uint32_t clk) {
    ctx_t ctx;
    ctx_t* c = &ctx;
    char fichero[128];
    /* La MAC: de USER0/USER1, 24 bits cada uno; si no, la de modelo */
    const uint32_t u0 = HWREG(USERREG0), u1 = HWREG(USERREG1);
    if (u0 != 0xFFFFFFFFu && u1 != 0xFFFFFFFFu) {
        c->mac[0] = (uint8_t)u0; c->mac[1] = (uint8_t)(u0 >> 8); c->mac[2] = (uint8_t)(u0 >> 16);
        c->mac[3] = (uint8_t)u1; c->mac[4] = (uint8_t)(u1 >> 8); c->mac[5] = (uint8_t)(u1 >> 16);
    } else {
        static const uint8_t M[6] = {0x02, 0x00, 0x00, 0xE4, 0x00, 0x00};
        copia(c->mac, M, 6);
    }
    c->xid = 0x4534B000u ^ ((uint32_t)c->mac[4] << 8) ^ c->mac[5];
    c->ident = 1;
    c->rxi = 0;

    SysCtlPeripheralEnable(SYSCTL_PERIPH_EMAC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_EPHY0);
    SysCtlPeripheralReset(SYSCTL_PERIPH_EMAC0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_EMAC0)) {}
    EMACPHYConfigSet(EMAC0_BASE, EMAC_PHY_TYPE_INTERNAL | EMAC_PHY_INT_MDIX_EN |
                                 EMAC_PHY_AN_100B_T_FULL_DUPLEX);
    EMACInit(EMAC0_BASE, clk, EMAC_BCONFIG_MIXED_BURST | EMAC_BCONFIG_PRIORITY_FIXED, 4, 4, 0);
    EMACConfigSet(EMAC0_BASE, EMAC_CONFIG_FULL_DUPLEX | EMAC_CONFIG_CHECKSUM_OFFLOAD |
                              EMAC_CONFIG_7BYTE_PREAMBLE | EMAC_CONFIG_IF_GAP_96BITS |
                              EMAC_CONFIG_USE_MACADDR0 | EMAC_CONFIG_SA_FROM_DESCRIPTOR |
                              EMAC_CONFIG_BO_LIMIT_1024,
                  EMAC_MODE_RX_STORE_FORWARD | EMAC_MODE_TX_STORE_FORWARD, 0);
    EMACAddrSet(EMAC0_BASE, 0, c->mac);
    for (unsigned i = 0; i < N_RX; ++i) {
        c->rx[i].ui32CtrlStatus = DES0_RX_CTRL_OWN;
        c->rx[i].ui32Count = DES1_RX_CTRL_CHAINED | (TAM_BUF << DES1_RX_CTRL_BUFF1_SIZE_S);
        c->rx[i].pvBuffer1 = c->rxbuf[i];
        c->rx[i].DES3.pLink = &c->rx[(i + 1u) % N_RX];
    }
    c->tx.ui32CtrlStatus = DES0_TX_CTRL_CHAINED;
    c->tx.pvBuffer1 = c->txbuf;
    c->tx.DES3.pLink = &c->tx;
    EMACRxDMADescriptorListSet(EMAC0_BASE, c->rx);
    EMACTxDMADescriptorListSet(EMAC0_BASE, &c->tx);
    EMACFrameFilterSet(EMAC0_BASE, EMAC_FRMFILTER_SADDR | EMAC_FRMFILTER_PASS_NO_CTRL);
    EMACTxEnable(EMAC0_BASE);
    EMACRxEnable(EMAC0_BASE);
    ms_arranca(clk);
    /* El enlace, hasta 5 s */
    for (unsigned ms = 0; ms < 5000u;) {
        if (ms_paso()) ++ms;
        if (EMACPHYRead(EMAC0_BASE, 0, EPHY_BMSR) & EPHY_BMSR_LINKSTAT) break;
    }
    for (;;) {
        bootp(c, fichero);
        if (tftp(c, fichero)) break;
    }
    /* Que salga el ultimo ACK, y a arrancar lo grabado */
    while (c->tx.ui32CtrlStatus & DES0_TX_CTRL_OWN) {}
    for (unsigned ms = 0; ms < 2u;) if (ms_paso()) ++ms;
    HWREG(APINT) = 0x05FA0004u;                    /* SYSRESETREQ */
    for (;;) {}
}

/* La entrada de la tabla: la pila del cargador, y adelante */
void UpdateEMAC(uint32_t ui32Clock) {
    (void)ui32Clock;
    __asm volatile("cpsid i\n\t"
                   "ldr r1, =0x20004000\n\t"
                   "msr msp, r1\n\t"
                   "b cargador_eth");
}
