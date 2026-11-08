/* twai.c — E8: el TWAI del C3 a 500 kbit/s (como twai_network de ESP-IDF:
 * BRP 8, TSEG1 15, TSEG2 4, SJW 3) contra un nodo CAN del banco, por un
 * transceptor en TX = GPIO0 y RX = GPIO1.
 *   res[0]  STATUS tras mandar 0x123 [DE AD BE EF]   res[1] INT leído
 *   res[2..9]  DATA_0..7 del marco extendido 0x1ABCDE0 [11 22 33] que manda
 *              el banco   res[10]/res[11] RX_MESSAGE_CNT antes/después de soltarlo
 *   res[15] = 1: el filtro de un solo 0x555 puesto; el banco manda 0x100 y 0x555
 *   res[12] RX_MESSAGE_CNT   res[13] DATA_1:DATA_2   res[14] DATA_3
 *   res[15] = 2: listo para el error de asentimiento; espera res[20] del banco
 *   res[16] TX_ERR_CNT   res[17] los INT vistos   res[18] STATUS
 *   res[19] STATUS tras ABORT_TX */
#include "c3.h"
#define TW(o) R(0x6002B000u + (o))
#define MODE   TW(0x00)
#define CMD    TW(0x04)
#define STATUS TW(0x08)
#define INT    TW(0x0C)
#define DATA(i) TW(0x40 + 4 * (i))
#define RXCNT  TW(0x74)

static void espera_st(uint32_t bit, uint32_t us) {
    uint64_t f = systimer_ahora() + (uint64_t)us * 16u;
    while (!(STATUS & bit) && systimer_ahora() < f) {}
}
int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    /* TX: GPIO0 <- señal 74; RX: señal 74 <- GPIO1 */
    IO_MUX(0) = IOMUX_GPIO | (2u << 10);
    GPIO_FUNC_OUT(0) = 74;
    IO_MUX(1) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    GPIO_FUNC_IN(74) = 1 | (1u << 6);
    /* Configuración, en modo reset */
    MODE = 1;
    TW(0x18) = (8 / 2 - 1) | (2u << 14);         /* BRP 8 (APB/2 cuenta), SJW 3 */
    TW(0x1C) = (15 - 1) | ((4 - 1) << 4);        /* TSEG1 15, TSEG2 4: 20 tq = 2 us */
    TW(0x7C) = 0x80;
    for (unsigned i = 0; i < 4; ++i) { DATA(i) = 0; DATA(4 + i) = 0xFF; }   /* todo pasa */
    TW(0x10) = 0xFF;                              /* INT_ENA */
    MODE = 8;                                     /* un filtro; en marcha */
    /* Mandar 0x123 */
    DATA(0) = 4; DATA(1) = 0x123 >> 3; DATA(2) = (0x123 & 7) << 5;
    DATA(3) = 0xDE; DATA(4) = 0xAD; DATA(5) = 0xBE; DATA(6) = 0xEF;
    CMD = 1;
    espera_st(1u << 3, 5000);
    buzon.res[0] = STATUS;
    buzon.res[1] = INT;
    /* Recibir el del banco */
    espera_st(1u << 0, 5000);
    for (unsigned i = 0; i < 8; ++i) buzon.res[2 + i] = DATA(i);
    buzon.res[10] = RXCNT;
    CMD = 4;
    buzon.res[11] = RXCNT;
    /* El filtro: solo 0x555 de datos */
    MODE = 1;
    DATA(0) = 0x555 >> 3; DATA(1) = (0x555 & 7) << 5; DATA(2) = 0; DATA(3) = 0;
    DATA(4) = 0; DATA(5) = 0x0F; DATA(6) = 0xFF; DATA(7) = 0xFF;
    MODE = 8;
    buzon.res[15] = 1;
    espera_st(1u << 0, 20000);
    espera_us(1000);
    buzon.res[12] = RXCNT;
    buzon.res[13] = (DATA(1) << 8) | DATA(2);
    buzon.res[14] = DATA(3);
    CMD = 4;
    /* Sin asentimiento: el TEC sube hasta el error pasivo y ahí se queda */
    buzon.res[15] = 2;
    while (!buzon.res[20]) {}
    (void)INT;
    DATA(0) = 0; DATA(1) = 0xFF; DATA(2) = 0xE0;
    CMD = 1;
    uint32_t vistos = 0;
    uint64_t f = systimer_ahora() + 20000u * 16u;
    while (TW(0x3C) < 128 && systimer_ahora() < f) vistos |= INT;
    espera_us(500);
    vistos |= INT;
    buzon.res[16] = TW(0x3C);
    buzon.res[17] = vistos;
    buzon.res[18] = STATUS;
    CMD = 2;
    espera_us(1000);
    buzon.res[19] = STATUS;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
