/* ===========================================================================
 * main.c — UN MAESTRO CANOPEN PEQUEÑO: lleva un eje por el bus de campo desde
 *          una NUCLEO-F446RE, con un transceptor SN65HVD230 en su CAN1
 *
 * El firmware de la fase F6 de doc/analisis_cnc.md (§5.5, la segunda capa):
 * lo que hace un control numérico con un accionamiento CANopen en vez de con
 * un driver de pasos. Ni STEP ni DIR: estados, consignas y perfiles por el
 * bus, con CiA 301 (NMT, SDO, PDO, SYNC, heartbeat, EMCY) y CiA 402 (la
 * máquina de estados del accionamiento, el perfil de posición, el de velocidad
 * y el homing). Es el de placas/eje_canopen.xml.
 *
 * AL ARRANCAR, solo:
 *   1. el CAN1 a 500 kbit/s en PB8 (RX, D15) y PB9 (TX, D14), y espera el
 *      BOOT-UP del nodo 1; si en medio segundo no llega, le manda un NMT de
 *      reinicio de la comunicación, y si nadie lo asiente, lo dice: TEC, REC
 *      y si el CAN1 se ha quedado en error pasivo
 *   2. por SDO, lee el tipo de aparato (0x1000) y lo configura: su heartbeat
 *      cada 100 ms (0x1017), su TPDO1 -la palabra de estado y la posición- en
 *      cada SYNC (0x1800:2 = 1), el error de seguimiento (0x6065), el perfil
 *      (0x6081, 0x6083 y 0x6084), la parada rapida (0x6085) y el homing
 *      (0x6098, 0x6099 y 0x609A)
 *   3. lo pone en OPERACIONAL (NMT 01) y empieza la SYNC, cada 5 ms
 *   4. lo habilita: 0x06, 0x07 y 0x0F en la palabra de control (0x6040), y
 *      espera cada estado en la palabra de estado que le llega en el TPDO1
 *
 * LO QUE ENTIENDE, línea a línea por la USART2 (el VCP, 115200 8N1):
 *
 *     H          homing por el final de abajo (método 17)
 *     G mm       a esa posición, con el perfil de posición: por el RPDO1, la
 *                palabra de control con el bit 4 y la consigna
 *     F mm/s     la velocidad del perfil (0x6081)
 *     V mm/s     a esa velocidad, con el perfil de velocidad, por el RPDO2;
 *                V 0 lo para
 *     P ms       espera
 *     Q          parada rápida
 *     R          quita el fallo y lo vuelve a habilitar
 *     E  D       habilitar y deshabilitar
 *     ?          el estado y la posición
 *
 * Cada estado nuevo que dice el accionamiento se escribe, y cada EMCY.
 * ===========================================================================*/
#include "stm32f446xx.h"
#include <stdint.h>

#define HCLK   84000000u
#define PCLK1  42000000u

#define NODO      1u
#define PASOS_MM  400.0f           /* 2000 pasos por vuelta y 5 mm por vuelta */
#define SYNC_MS   5u

/* --- La USART2 ------------------------------------------------------------ */
#define RXN 1024u
#define TXN 2048u
static volatile uint8_t g_rx[RXN];
static volatile uint32_t g_rx_w, g_rx_r;
static volatile uint8_t g_tx[TXN];
static volatile uint32_t g_tx_w, g_tx_r;

void USART2_IRQHandler(void)
{
    const uint32_t sr = USART2->SR;
    if (sr & USART_SR_RXNE) {
        const uint8_t c = (uint8_t)USART2->DR;
        if (((g_rx_w + 1u) % RXN) != g_rx_r) {
            g_rx[g_rx_w] = c;
            g_rx_w = (g_rx_w + 1u) % RXN;
        }
    } else if (sr & USART_SR_ORE) {
        (void)USART2->DR;
    }
    if ((sr & USART_SR_TXE) && (USART2->CR1 & USART_CR1_TXEIE)) {
        if (g_tx_r != g_tx_w) {
            USART2->DR = g_tx[g_tx_r];
            g_tx_r = (g_tx_r + 1u) % TXN;
        } else {
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}
static void envia_c(char c)
{
    while (((g_tx_w + 1u) % TXN) == g_tx_r) __WFI();
    g_tx[g_tx_w] = (uint8_t)c;
    g_tx_w = (g_tx_w + 1u) % TXN;
    USART2->CR1 |= USART_CR1_TXEIE;
}
static void envia(const char* s) { while (*s) envia_c(*s++); }
static void envia_u(uint32_t x)
{
    char b[12];
    int i = 0;
    do { b[i++] = (char)('0' + x % 10u); x /= 10u; } while (x);
    while (i) envia_c(b[--i]);
}
static void envia_i(int32_t v)
{
    if (v < 0) { envia_c('-'); envia_u((uint32_t)(-v)); }
    else envia_u((uint32_t)v);
}
static void envia_h(uint32_t x, int n)
{
    envia("0x");
    for (int k = n - 1; k >= 0; --k) envia_c("0123456789ABCDEF"[(x >> (4 * k)) & 15u]);
}
static void envia_mm(int32_t pasos)              /* con dos decimales */
{
    int32_t c = (int32_t)((float)pasos * 100.0f / PASOS_MM + (pasos >= 0 ? 0.5f : -0.5f));
    if (c < 0) { envia_c('-'); c = -c; }
    envia_u((uint32_t)c / 100u);
    envia_c('.');
    envia_c((char)('0' + (c / 10) % 10));
    envia_c((char)('0' + c % 10));
}

/* --- El reloj de milisegundos y la SYNC ----------------------------------- */
static volatile uint32_t g_ms;
static volatile int g_sync;                    /* la SYNC en marcha */
static volatile uint32_t g_syncs;

static void can_sync(void)
{
    /* La SYNC, por el buzón 2: es solo suyo */
    if (CAN1->TSR & CAN_TSR_TME2) {
        CAN1->sTxMailBox[2].TDTR = 0;
        CAN1->sTxMailBox[2].TIR = (0x080u << 21) | CAN_TI2R_TXRQ;
        ++g_syncs;
    }
}
void SysTick_Handler(void)
{
    ++g_ms;
    if (g_sync && (g_ms % SYNC_MS) == 0u) can_sync();
}

/* --- El CAN1: lo que llega, a un anillo por su interrupción --------------- */
typedef struct { uint16_t id; uint8_t dlc; uint8_t d[8]; } Marco;
#define CANN 32u
static volatile Marco g_can[CANN];
static volatile uint32_t g_can_w, g_can_r;
static volatile uint32_t g_can_perdidos;

void CAN1_RX0_IRQHandler(void)
{
    while (CAN1->RF0R & CAN_RF0R_FMP0) {
        const uint32_t rir = CAN1->sFIFOMailBox[0].RIR;
        const uint32_t rdt = CAN1->sFIFOMailBox[0].RDTR;
        const uint32_t lo = CAN1->sFIFOMailBox[0].RDLR, hi = CAN1->sFIFOMailBox[0].RDHR;
        CAN1->RF0R = CAN_RF0R_RFOM0;
        if (((g_can_w + 1u) % CANN) == g_can_r) { ++g_can_perdidos; continue; }
        volatile Marco* m = &g_can[g_can_w];
        m->id = (uint16_t)(rir >> 21);
        m->dlc = (uint8_t)(rdt & 15u);
        for (int k = 0; k < 4; ++k) {
            m->d[k] = (uint8_t)(lo >> (8 * k));
            m->d[4 + k] = (uint8_t)(hi >> (8 * k));
        }
        g_can_w = (g_can_w + 1u) % CANN;
    }
}

static void can_inicia(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    (void)RCC->APB1ENR;
    /* PB8 CAN1_RX y PB9 CAN1_TX, AF9 */
    GPIOB->MODER = (GPIOB->MODER & ~(0xFu << 16)) | (0xAu << 16);
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~0xFFu) | 0x99u;
    GPIOB->OSPEEDR |= 0xFu << 16;
    /* A inicialización: fuera del sueño */
    CAN1->MCR = CAN_MCR_INRQ;
    while (!(CAN1->MSR & CAN_MSR_INAK)) { }
    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_TXFP;      /* sin ABOM: un bus-off se queda */
    /* 500 kbit/s con 42 MHz: 6 de preescalado, 14 cuantos (1 + 11 + 2), al 86 % */
    CAN1->BTR = (5u << CAN_BTR_BRP_Pos) | (10u << CAN_BTR_TS1_Pos) | (1u << CAN_BTR_TS2_Pos);
    /* Un filtro que lo deja pasar todo, al FIFO 0 */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1u;
    CAN1->FS1R |= 1u;
    CAN1->FM1R &= ~1u;
    CAN1->FFA1R &= ~1u;
    CAN1->sFilterRegister[0].FR1 = 0;
    CAN1->sFilterRegister[0].FR2 = 0;
    CAN1->FA1R |= 1u;
    CAN1->FMR &= ~CAN_FMR_FINIT;
    /* En marcha */
    CAN1->MCR &= ~CAN_MCR_INRQ;
    while (CAN1->MSR & CAN_MSR_INAK) { }
    CAN1->IER = CAN_IER_FMPIE0;
    NVIC_SetPriority(CAN1_RX0_IRQn, 1);
    NVIC_EnableIRQ(CAN1_RX0_IRQn);
}

/* Un marco por el buzón 0 o el 1, el que esté libre */
static int can_envia(uint16_t id, uint8_t dlc, const uint8_t* d)
{
    const uint32_t t0 = g_ms;
    for (;;) {
        for (int b = 0; b < 2; ++b) {
            if (CAN1->TSR & (CAN_TSR_TME0 << b)) {
                uint32_t lo = 0, hi = 0;
                for (int k = 0; k < 4; ++k) {
                    if (k < dlc) lo |= (uint32_t)d[k] << (8 * k);
                    if (k + 4 < dlc) hi |= (uint32_t)d[k + 4] << (8 * k);
                }
                CAN1->sTxMailBox[b].TDLR = lo;
                CAN1->sTxMailBox[b].TDHR = hi;
                CAN1->sTxMailBox[b].TDTR = dlc;
                CAN1->sTxMailBox[b].TIR = ((uint32_t)id << 21) | CAN_TI0R_TXRQ;
                return 1;
            }
        }
        if (g_ms - t0 > 100u) return 0;
        __WFI();
    }
}

/* --- Lo que se sabe del nodo ----------------------------------------------- */
static volatile int g_bootup, g_hb_estado = -1;
static uint32_t g_hb_ms;
static uint16_t g_sw, g_sw_dicha = 0xFFFFu;
static int32_t g_pos;
static uint32_t g_tpdos;
static int g_emcy;
static uint16_t g_emcy_codigo;
/* La respuesta del último SDO */
static int g_sdo_listo;
static uint8_t g_sdo[8];

static const char* texto_estado(uint16_t sw)
{
    if ((sw & 0x4F) == 0x00) return "Not ready to switch on";
    if ((sw & 0x4F) == 0x40) return "Switch on disabled";
    if ((sw & 0x6F) == 0x21) return "Ready to switch on";
    if ((sw & 0x6F) == 0x23) return "Switched on";
    if ((sw & 0x6F) == 0x27) return "Operation enabled";
    if ((sw & 0x6F) == 0x07) return "Quick stop active";
    if ((sw & 0x4F) == 0x0F) return "Fault reaction active";
    if ((sw & 0x4F) == 0x08) return "Fault";
    return "?";
}

static void atiende(void)
{
    while (g_can_r != g_can_w) {
        const Marco m = *(const Marco*)&g_can[g_can_r];
        g_can_r = (g_can_r + 1u) % CANN;
        if (m.id == 0x700u + NODO && m.dlc >= 1) {
            if (m.d[0] == 0) {
                g_bootup = 1;
                envia("boot-up del nodo ");
                envia_u(NODO);
                envia("\r\n");
            }
            g_hb_estado = m.d[0];
            g_hb_ms = g_ms;
        } else if (m.id == 0x080u + NODO && m.dlc >= 3) {
            g_emcy_codigo = (uint16_t)(m.d[0] | (m.d[1] << 8));
            g_emcy = 1;
            envia("EMCY del nodo ");
            envia_u(NODO);
            envia(": ");
            envia_h(g_emcy_codigo, 4);
            envia(g_emcy_codigo == 0x8611u ? " error de seguimiento"
                  : g_emcy_codigo == 0x3210u ? " sobretension"
                  : g_emcy_codigo == 0x0000u ? " se ha quitado" : "");
            envia(", registro ");
            envia_h(m.d[2], 2);
            envia("\r\n");
        } else if (m.id == 0x180u + NODO && m.dlc >= 6) {
            g_sw = (uint16_t)(m.d[0] | (m.d[1] << 8));
            g_pos = (int32_t)(m.d[2] | (m.d[3] << 8) | (m.d[4] << 16) | ((uint32_t)m.d[5] << 24));
            ++g_tpdos;
            if ((g_sw & 0x6F) != (g_sw_dicha & 0x6F) || ((g_sw ^ g_sw_dicha) & 0x08)) {
                g_sw_dicha = g_sw;
                envia("estado: ");
                envia(texto_estado(g_sw));
                envia(" (");
                envia_h(g_sw, 4);
                envia(")\r\n");
            }
        } else if (m.id == 0x580u + NODO && m.dlc == 8) {
            for (int k = 0; k < 8; ++k) g_sdo[k] = m.d[k];
            g_sdo_listo = 1;
        }
    }
}

/* Espera `ms` atendiendo el bus */
static void espera(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < ms) { atiende(); __WFI(); }
    atiende();
}

/* --- El cliente de SDO, expedito ------------------------------------------ */
static uint32_t sdo(uint8_t cs, uint16_t idx, uint8_t sub, uint32_t v, uint32_t* leido)
{
    uint8_t d[8] = {cs, (uint8_t)idx, (uint8_t)(idx >> 8), sub,
                    (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    g_sdo_listo = 0;
    if (!can_envia((uint16_t)(0x600u + NODO), 8, d)) return 0xFFFFFFFFu;
    const uint32_t t0 = g_ms;
    for (;;) {
        atiende();
        if (g_sdo_listo && g_sdo[1] == d[1] && g_sdo[2] == d[2] && g_sdo[3] == sub) break;
        if (g_ms - t0 > 200u) {
            envia("error: el SDO a ");
            envia_h(idx, 4);
            envia(" no contesta\r\n");
            return 0xFFFFFFFFu;
        }
        __WFI();
    }
    const uint32_t r = (uint32_t)g_sdo[4] | ((uint32_t)g_sdo[5] << 8) | ((uint32_t)g_sdo[6] << 16) |
                       ((uint32_t)g_sdo[7] << 24);
    if (g_sdo[0] == 0x80u) {
        envia("error: SDO ");
        envia_h(idx, 4);
        envia(":");
        envia_h(sub, 2);
        envia(" abortado con ");
        envia_h(r, 8);
        envia("\r\n");
        return r;
    }
    if (leido) *leido = r;
    return 0;
}
static uint32_t lee(uint16_t idx, uint8_t sub, uint32_t* v) { return sdo(0x40, idx, sub, 0, v); }
static uint32_t escribe(uint16_t idx, uint8_t sub, uint32_t v, int n)
{
    static const uint8_t cs[5] = {0x23, 0x2F, 0x2B, 0x27, 0x23};
    return sdo(cs[n], idx, sub, v, 0);
}

/* --- Los PDO del maestro ---------------------------------------------------- */
static void rpdo1(uint16_t cw, int32_t objetivo)
{
    const uint8_t d[6] = {(uint8_t)cw, (uint8_t)(cw >> 8), (uint8_t)objetivo,
                          (uint8_t)(objetivo >> 8), (uint8_t)(objetivo >> 16),
                          (uint8_t)(objetivo >> 24)};
    can_envia((uint16_t)(0x200u + NODO), 6, d);
}
static void rpdo2(uint16_t cw, int32_t vel)
{
    const uint8_t d[6] = {(uint8_t)cw, (uint8_t)(cw >> 8), (uint8_t)vel, (uint8_t)(vel >> 8),
                          (uint8_t)(vel >> 16), (uint8_t)(vel >> 24)};
    can_envia((uint16_t)(0x300u + NODO), 6, d);
}
static void nmt(uint8_t cs)
{
    const uint8_t d[2] = {cs, (uint8_t)NODO};
    can_envia(0x000, 2, d);
}

/* Espera a que la palabra de estado, con `mascara`, valga `valor`: por el
 * TPDO1 de cada SYNC. 0 si llega un fallo, si se para en un final de carrera
 * (el bit 11) o si se acaba el plazo */
static int espera_sw(uint16_t mascara, uint16_t valor, uint32_t plazo_ms)
{
    const uint32_t t0 = g_ms;
    const uint32_t n0 = g_tpdos;
    const uint16_t lim0 = g_sw & 0x0800;           /* un final pisado ya: se sale de el */
    for (;;) {
        atiende();
        if (g_tpdos != n0 && (g_sw & mascara) == valor) return 1;
        if (g_tpdos != n0 && (g_sw & 0x08) && !(valor & 0x08)) return 0;       /* un fallo */
        if (g_tpdos != n0 && (g_sw & 0x0800) && !lim0 && !(mascara & 0x0800)) return 0;  /* un final */
        if (g_ms - t0 > plazo_ms) return 0;
        __WFI();
    }
}

static int g_modo = -1;
static int modo(int m)
{
    if (g_modo == m) return 1;
    if (escribe(0x6060, 0, (uint32_t)(uint8_t)m, 1)) return 0;
    g_modo = m;
    return 1;
}

static int habilita(void)
{
    escribe(0x6040, 0, 0x06, 2);
    if (!espera_sw(0x6F, 0x21, 100)) { envia("error: no llega a Ready to switch on\r\n"); return 0; }
    escribe(0x6040, 0, 0x07, 2);
    if (!espera_sw(0x6F, 0x23, 100)) { envia("error: no llega a Switched on\r\n"); return 0; }
    escribe(0x6040, 0, 0x0F, 2);
    if (!espera_sw(0x6F, 0x27, 300)) { envia("error: no llega a Operation enabled\r\n"); return 0; }
    return 1;
}

static void informa(void)
{
    envia("pos ");
    envia_mm(g_pos);
    envia(" mm (");
    envia_i(g_pos);
    envia(" pasos), ");
    envia(texto_estado(g_sw));
    envia(" ");
    envia_h(g_sw, 4);
    envia("\r\n");
}

/* --- Las órdenes ------------------------------------------------------------ */
static int32_t numero(const char* s, float escala)
{
    while (*s == ' ') ++s;
    int neg = 0;
    if (*s == '-') { neg = 1; ++s; }
    float v = 0.0f, d = 0.0f;
    for (; *s; ++s) {
        if (*s >= '0' && *s <= '9') {
            if (d > 0.0f) { v += (float)(*s - '0') * d; d *= 0.1f; }
            else v = v * 10.0f + (float)(*s - '0');
        } else if (*s == '.' && d == 0.0f) d = 0.1f;
        else break;
    }
    v *= escala;
    return (int32_t)(neg ? -v - 0.5f : v + 0.5f);
}

static void homing(void)
{
    const uint32_t t0 = g_ms;
    if (!modo(6)) return;
    escribe(0x6040, 0, 0x0F, 2);
    escribe(0x6040, 0, 0x1F, 2);
    /* Homing hecho y objetivo alcanzado (bits 12 y 10), o un error (13) */
    const int ok = espera_sw(0x3400, 0x1400, 30000);
    escribe(0x6040, 0, 0x0F, 2);
    if (!ok || (g_sw & 0x2000)) { envia("error: el homing no termina\r\n"); return; }
    espera(SYNC_MS * 2u);
    envia("homing hecho en ");
    envia_u(g_ms - t0);
    envia(" ms: ");
    informa();
}

static void mueve(int32_t objetivo)
{
    const uint32_t t0 = g_ms;
    if (!modo(1)) return;
    /* La consigna con el bit 4 -y el 5, en el acto-; el saludo, en el bit 12 */
    rpdo1(0x0F, objetivo);
    rpdo1(0x3F, objetivo);
    if (!espera_sw(0x1000, 0x1000, 200)) { envia("error: no acepta la consigna\r\n"); return; }
    rpdo1(0x0F, objetivo);
    if (!espera_sw(0x1400, 0x0400, 60000)) {
        espera(300);                               /* a que pare */
        envia("error: no llega: ");
        informa();
        return;
    }
    envia("en ");
    envia_mm(g_pos);
    envia(" mm a los ");
    envia_u(g_ms - t0);
    envia(" ms\r\n");
}

static void ejecuta(char* l)
{
    while (*l == ' ') ++l;
    const char c = l[0] >= 'a' && l[0] <= 'z' ? (char)(l[0] - 32) : l[0];
    if (!c) return;
    g_emcy = 0;
    switch (c) {
        case 'H': homing(); break;
        case 'G': mueve(numero(l + 1, PASOS_MM)); break;
        case 'F': escribe(0x6081, 0, (uint32_t)numero(l + 1, PASOS_MM), 4); break;
        case 'V':
            if (!modo(3)) break;
            rpdo2(0x0F, numero(l + 1, PASOS_MM));
            break;
        case 'P': espera((uint32_t)numero(l + 1, 1.0f)); break;
        case 'Q':
            escribe(0x6040, 0, 0x0B, 2);
            espera_sw(0x4F, 0x40, 2000);
            /* La velocidad pedida se queda: a cero, para que no arranque solo
             * al volver a habilitarlo */
            escribe(0x60FF, 0, 0, 4);
            break;
        case 'R':
            /* El reinicio del fallo vale en Fault, no mientras reacciona */
            if (!espera_sw(0x4F, 0x08, 1000)) { envia("error: no esta en Fault\r\n"); break; }
            escribe(0x6040, 0, 0x80, 2);
            if (!espera_sw(0x4F, 0x40, 200)) { envia("error: no quita el fallo\r\n"); break; }
            habilita();
            break;
        case 'E': habilita(); break;
        case 'D':
            escribe(0x6040, 0, 0x06, 2);
            espera_sw(0x6F, 0x21, 300);
            break;
        case '?': informa(); break;
        default:
            envia("error: no entiendo '");
            envia(l);
            envia("'\r\n");
            return;
    }
    envia("ok\r\n");
}

/* --- El arranque ----------------------------------------------------------- */
static void reloj(void)
{
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = (16u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (1u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   (2u << RCC_PLLCFGR_PLLR_Pos);              /* PLLP = 4; fuente, HSI */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW)) |
                RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
}

static int espera_bootup(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while (!g_bootup && g_ms - t0 < ms) { atiende(); __WFI(); }
    return g_bootup;
}

int main(void)
{
    reloj();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    /* La USART2 del VCP: PA2 y PA3, AF7, 115200 */
    GPIOA->MODER = (GPIOA->MODER & ~(0xFu << 4)) | (0xAu << 4);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFFu << 8)) | (0x77u << 8);
    USART2->BRR = (PCLK1 + 57600u) / 115200u;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, 2);
    NVIC_EnableIRQ(USART2_IRQn);
    /* El SysTick, a 1 kHz */
    SysTick->LOAD = HCLK / 1000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

    envia("\r\nmaestro CANopen (F6): un eje por el bus, nodo 1, CAN1 a 500 kbit/s\r\n");
    can_inicia();

    /* 1. El boot-up; si no llega, se le pide */
    if (!espera_bootup(500)) {
        envia("no llega el boot-up: NMT de reinicio de la comunicacion\r\n");
        nmt(0x82);
        if (!espera_bootup(500)) {
            const uint32_t esr = CAN1->ESR;
            envia("nadie contesta: TEC=");
            envia_u((esr >> 16) & 0xFFu);
            envia(", REC=");
            envia_u((esr >> 24) & 0xFFu);
            envia((esr & CAN_ESR_EPVF) ? ", error pasivo" : "");
            envia((esr & CAN_ESR_BOFF) ? ", bus off" : "");
            envia(": nadie asiente el NMT\r\n");
            CAN1->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1;
            for (;;) __WFI();
        }
    }
    /* 2. Quién es, y su configuración */
    uint32_t v = 0;
    if (!lee(0x1000, 0, &v)) {
        envia("tipo de aparato ");
        envia_h(v, 8);
        envia((v & 0xFFFFu) == 402u ? ": CiA 402, un accionamiento\r\n" : "\r\n");
    }
    escribe(0x1017, 0, 100, 2);                  /* heartbeat, cada 100 ms */
    escribe(0x1800, 2, 1, 1);                    /* TPDO1 en cada SYNC */
    escribe(0x6065, 0, 2000, 4);                 /* error de seguimiento: 5 mm */
    escribe(0x6081, 0, (uint32_t)(50.0f * PASOS_MM), 4);   /* 50 mm/s */
    escribe(0x6083, 0, (uint32_t)(500.0f * PASOS_MM), 4);  /* 500 mm/s² */
    escribe(0x6084, 0, (uint32_t)(500.0f * PASOS_MM), 4);
    /* La parada rapida, la de los finales: a 50 mm/s, en 1,25 mm. El final de
     * abajo esta a 5 mm del tope: con la de omision -250 mm/s², 5 mm- llegaria */
    escribe(0x6085, 0, (uint32_t)(1000.0f * PASOS_MM), 4);
    escribe(0x6098, 0, 17, 1);                   /* homing por el final de abajo */
    escribe(0x6099, 1, (uint32_t)(20.0f * PASOS_MM), 4);   /* lo busca a 20 mm/s */
    escribe(0x6099, 2, (uint32_t)(1.0f * PASOS_MM), 4);    /* y sale a 1 mm/s */
    escribe(0x609A, 0, (uint32_t)(200.0f * PASOS_MM), 4);
    /* 3. Operacional, y la SYNC */
    nmt(0x01);
    g_sync = 1;
    espera(SYNC_MS * 2u);
    /* 4. Habilitado */
    if (habilita()) {
        envia("listo: ");
        informa();
    }

    static char lin[96];
    unsigned n = 0;
    for (;;) {
        atiende();
        if (g_rx_r == g_rx_w) { __WFI(); continue; }
        const char c = (char)g_rx[g_rx_r];
        g_rx_r = (g_rx_r + 1u) % RXN;
        if (c == '\r' || c == '\n') {
            lin[n] = 0;
            n = 0;
            ejecuta(lin);
        } else if (n + 1u < sizeof lin) {
            lin[n++] = c;
        }
    }
}

/* El startup de ST llama a los constructores de C++: aqui no hay ninguno */
void __libc_init_array(void) { }
