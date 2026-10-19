/* ===========================================================================
 * can_e4.c — Los dos C_CAN del MSP432E401Y con DriverLib (e4_t7, modos 1-3)
 *
 * Como en los ejemplos de CAN de TivaWare (simple_tx, simple_rx, multi_rx):
 * CANInit, CANBitRateSet a 500 kbit/s, CANMessageSet con los objetos de
 * mensaje, y la interrupcion que mira CANIntStatus(CAN_INT_STS_CAUSE): 0x8000
 * es el estado (se borra leyendo CANSTS con CANStatusGet), 1..32 el objeto
 * (se borra con CANIntClear).
 *
 * modo 1, un solo chip: CAN0 (PA0/PA1) y CAN1 (PB0/PB1) con sus
 * transceptores en el mismo hilo. CAN1 tiene:
 *   objeto 1     0x123 exacto
 *   objetos 2-4  una FIFO para 0x200-0x20F (MSG_OBJ_FIFO en 2 y 3; el 4 cierra)
 *   objeto 5     extendidos 0x1ABCDExx (filtro de 29 bits y de IDE)
 *   objeto 6     contesta solo a una remota de 0x300 (RXTX_REMOTE)
 * y CAN0 manda 0x123, tres marcos a la FIFO, un extendido, una remota de
 * 0x300 (cuya respuesta le llega en el mismo objeto) y uno que no casa con
 * nada (0x7FF): CAN1 lo asiente (RXOK) pero no lo guarda.
 *
 * modos 2 y 3, dos chips en la misma placa, cada uno con su CAN0 (PA0/PA1):
 * el nodo A manda cinco marcos 0x100 y espera cada respuesta 0x180; el nodo B
 * devuelve cada uno con el primer byte mas uno.
 *
 * Los resultados van a `sal` del buzon (buzon.h):
 *   sal[0], sal[1]  lo que devuelve CANBitRateSet (CAN0 y CAN1)
 *   sal[2]          interrupciones de objeto de transmision en CAN0
 *   sal[3], sal[4]  interrupciones de estado en CAN0 y CAN1
 *   sal[5]          el ultimo CANSTS de CAN1
 *   sal[6]          CANERR de CAN0 al final
 *   sal[7]          marcos recibidos (cuantos de 8.. hay)
 *   sal[8 + 4k]     el marco k: identificador, flags, longitud y objeto, y
 *                   sus ocho bytes en las dos palabras siguientes
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "buzon.h"

#define PLAZO 3000000u
/* `sal` lo escriben las interrupciones y lo mira el bucle: volatil */
#define SAL ((volatile uint32_t *)buzon.sal)

static volatile uint32_t pend[2];          /* objetos con interrupcion, por CAN */

static void isr(uint32_t base, unsigned n) {
    const uint32_t c = CANIntStatus(base, CAN_INT_STS_CAUSE);
    if (c == CAN_INT_INTID_STATUS) {
        const uint32_t s = CANStatusGet(base, CAN_STS_CONTROL);
        SAL[3 + n]++;
        if (n == 1) SAL[5] = s;
    } else if (c >= 1 && c <= 32) {
        CANIntClear(base, c);
        pend[n] |= 1u << (c - 1);
        if (n == 0 && c <= 5) SAL[2]++;     /* los de transmision de CAN0 */
    }
}
void CAN0_IRQHandler(void) { isr(CAN0_BASE, 0); }
void CAN1_IRQHandler(void) { isr(CAN1_BASE, 1); }

static bool espera(unsigned n, uint32_t bits) {
    for (uint32_t t = 0; t < PLAZO; ++t)
        if ((pend[n] & bits) == bits) { pend[n] &= ~bits; return true; }
    return false;
}

static void guarda(uint32_t base, uint32_t obj) {
    uint8_t d[8] = {0};
    tCANMsgObject m;
    m.pui8MsgData = d;
    CANMessageGet(base, obj, &m, false);
    const uint32_t k = SAL[7]++;
    if (k >= 30) return;
    SAL[8 + 4 * k] = m.ui32MsgID;
    SAL[9 + 4 * k] = (m.ui32Flags & 0xFFFFu) | (m.ui32MsgLen << 16) | (obj << 24);
    SAL[10 + 4 * k] = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24);
    SAL[11 + 4 * k] = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t)d[7] << 24);
}

static void manda(uint32_t base, uint32_t obj, uint32_t id, uint32_t flags, const uint8_t *d,
                  uint32_t n, tMsgObjType tipo) {
    tCANMsgObject m;
    m.ui32MsgID = id;
    m.ui32MsgIDMask = 0;
    m.ui32Flags = flags;
    m.ui32MsgLen = n;
    m.pui8MsgData = (uint8_t *)d;
    CANMessageSet(base, obj, &m, tipo);
}

static void recibe(uint32_t base, uint32_t obj, uint32_t id, uint32_t mascara, uint32_t flags) {
    tCANMsgObject m;
    m.ui32MsgID = id;
    m.ui32MsgIDMask = mascara;
    m.ui32Flags = flags | MSG_OBJ_RX_INT_ENABLE;
    m.ui32MsgLen = 8;
    m.pui8MsgData = 0;
    CANMessageSet(base, obj, &m, MSG_OBJ_TYPE_RX);
}

static void arranca(uint32_t base, uint32_t irq, unsigned n) {
    CANInit(base);
    SAL[n] = CANBitRateSet(base, g_clk, 500000);
    CANIntEnable(base, CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS);
    IntEnable(irq);
}

static void pines_can0(void) {
    habilita(SYSCTL_PERIPH_GPIOA);
    habilita(SYSCTL_PERIPH_CAN0);
    GPIOPinConfigure(GPIO_PA0_CAN0RX);
    GPIOPinConfigure(GPIO_PA1_CAN0TX);
    GPIOPinTypeCAN(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
}

void can_local(void) {
    static const uint8_t D8[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    static const uint8_t RESPUESTA[8] = {'R', 'E', 'M', 'O', 'T', 'A', '!', '!'};
    static const uint8_t F1[1] = {0xA1}, F2[2] = {0xB1, 0xB2}, F3[3] = {0xC1, 0xC2, 0xC3};
    static const uint8_t EXT[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    pines_can0();
    habilita(SYSCTL_PERIPH_GPIOB);
    habilita(SYSCTL_PERIPH_CAN1);
    GPIOPinConfigure(GPIO_PB0_CAN1RX);
    GPIOPinConfigure(GPIO_PB1_CAN1TX);
    GPIOPinTypeCAN(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    arranca(CAN0_BASE, INT_CAN0, 0);
    arranca(CAN1_BASE, INT_CAN1, 1);

    /* Los objetos de recepcion de CAN1 */
    recibe(CAN1_BASE, 1, 0x123, 0, 0);
    recibe(CAN1_BASE, 2, 0x200, 0x7F0, MSG_OBJ_USE_ID_FILTER | MSG_OBJ_FIFO);
    recibe(CAN1_BASE, 3, 0x200, 0x7F0, MSG_OBJ_USE_ID_FILTER | MSG_OBJ_FIFO);
    recibe(CAN1_BASE, 4, 0x200, 0x7F0, MSG_OBJ_USE_ID_FILTER);
    recibe(CAN1_BASE, 5, 0x1ABCDE00, 0x1FFFFF00, MSG_OBJ_EXTENDED_ID | MSG_OBJ_USE_EXT_FILTER);
    manda(CAN1_BASE, 6, 0x300, 0, RESPUESTA, 8, MSG_OBJ_TYPE_RXTX_REMOTE);
    IntMasterEnable();
    CANEnable(CAN0_BASE);
    CANEnable(CAN1_BASE);
    buzon.estado = 10;

    /* 1. Un marco estandar de ocho bytes */
    manda(CAN0_BASE, 1, 0x123, MSG_OBJ_TX_INT_ENABLE, D8, 8, MSG_OBJ_TYPE_TX);
    if (!espera(1, 1u << 0)) { buzon.estado = 91; for (;;) { } }
    guarda(CAN1_BASE, 1);
    buzon.estado = 20;

    /* 2. Tres a la FIFO, pedidos a la vez: salen por orden de objeto */
    manda(CAN0_BASE, 2, 0x201, MSG_OBJ_TX_INT_ENABLE, F1, 1, MSG_OBJ_TYPE_TX);
    manda(CAN0_BASE, 3, 0x202, MSG_OBJ_TX_INT_ENABLE, F2, 2, MSG_OBJ_TYPE_TX);
    manda(CAN0_BASE, 4, 0x203, MSG_OBJ_TX_INT_ENABLE, F3, 3, MSG_OBJ_TYPE_TX);
    if (!espera(1, 0x7u << 1)) { buzon.estado = 92; for (;;) { } }
    guarda(CAN1_BASE, 2); guarda(CAN1_BASE, 3); guarda(CAN1_BASE, 4);
    buzon.estado = 30;

    /* 3. Un extendido */
    manda(CAN0_BASE, 5, 0x1ABCDE05, MSG_OBJ_TX_INT_ENABLE | MSG_OBJ_EXTENDED_ID, EXT, 4,
          MSG_OBJ_TYPE_TX);
    if (!espera(1, 1u << 4)) { buzon.estado = 93; for (;;) { } }
    guarda(CAN1_BASE, 5);
    buzon.estado = 40;

    /* 4. Una remota: CAN1 contesta sola con su objeto 6 */
    manda(CAN0_BASE, 10, 0x300, MSG_OBJ_RX_INT_ENABLE, 0, 8, MSG_OBJ_TYPE_TX_REMOTE);
    if (!espera(0, 1u << 9)) { buzon.estado = 94; for (;;) { } }
    guarda(CAN0_BASE, 10);
    buzon.estado = 50;

    /* 5. Uno que no casa con nada: asentido, y en ningun objeto. Se espera a
     * que CAN0 acabe de mandarlo (su objeto 11) y al RXOK de CAN1, no a que
     * cambie la cuenta de interrupciones de estado: la del TXOK de la
     * respuesta del paso 4 puede llegar despues de leerla, y entonces se
     * acababa antes de que el marco saliera al hilo */
    SAL[5] = 0;
    manda(CAN0_BASE, 11, 0x7FF, MSG_OBJ_TX_INT_ENABLE, D8, 2, MSG_OBJ_TYPE_TX);
    if (!espera(0, 1u << 10)) { buzon.estado = 95; for (;;) { } }
    for (uint32_t t = 0; t < PLAZO && !(SAL[5] & CAN_STATUS_RXOK); ++t) { }
    buzon.r[0] = CANStatusGet(CAN1_BASE, CAN_STS_NEWDAT);
    SAL[6] = HWREG(CAN0_BASE + 0x08);          /* CANERR */
    buzon.estado = 100;
    for (;;) { }
}

void can_nodo(int b) {
    pines_can0();
    arranca(CAN0_BASE, INT_CAN0, 0);
    recibe(CAN0_BASE, 2, b ? 0x100 : 0x180, 0, 0);
    IntMasterEnable();
    CANEnable(CAN0_BASE);
    buzon.estado = 10;
    uint8_t d[4] = {0, 0x5A, 0xA5, 0};
    if (!b) {
        /* El nodo A: cinco idas y vueltas */
        for (unsigned i = 0; i < 5; ++i) {
            d[0] = (uint8_t)(10 * i); d[3] = (uint8_t)i;
            manda(CAN0_BASE, 1, 0x100, 0, d, 4, MSG_OBJ_TYPE_TX);
            if (!espera(0, 1u << 1)) { buzon.estado = 90 + i; for (;;) { } }
            guarda(CAN0_BASE, 2);
            const uint32_t k = SAL[7] - 1;
            if ((SAL[10 + 4 * k] & 0xFFu) == (uint32_t)(10 * i + 1)) buzon.r[0]++;
        }
        buzon.estado = 100;
        for (;;) { }
    }
    /* El nodo B: devuelve lo que le llega, con el primer byte mas uno */
    for (;;) {
        if (!espera(0, 1u << 1)) continue;
        uint8_t r[8] = {0};
        tCANMsgObject m;
        m.pui8MsgData = r;
        CANMessageGet(CAN0_BASE, 2, &m, false);
        r[0]++;
        manda(CAN0_BASE, 1, 0x180, 0, r, m.ui32MsgLen, MSG_OBJ_TYPE_TX);
        buzon.r[0]++;
        buzon.estado = 100;
    }
}
