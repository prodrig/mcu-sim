/* ledc.c — E8: el LEDC del C3 [TRM-C3 §31], como lo pone ledc_basic de
 * ESP-IDF: el temporizador 0 a 4 kHz con 13 bits (APB 80 MHz / 2,44 / 8192)
 * y el canal 0 en GPIO8 por la matriz (LEDC_LS_SIG_OUT0, señal 45).
 *   1. ciclo del 50 % (4096 de 8192)
 *   2. el 25 % (2048), con DUTY_START: entra al empezar el periodo
 *   3. un fundido: +512 cada 2 periodos, 4 veces (2048 -> 4096), y la
 *      interrupción DUTY_CHNG_END del canal 0
 * El banco mide el pin; el firmware deja en el buzón lo que lee. */
#include "c3.h"
#define LEDC(o) R(0x60019000u + (o))

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    IO_MUX(8) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    GPIO_FUNC_OUT(8) = 45;                         /* LEDC_LS_SIG_OUT0, OE del periférico */
    LEDC(0xD0) = (1u << 31) | 1u;                  /* CLK_EN, APB */
    LEDC(0xA0) = 13u | (625u << 4) | (1u << 25);   /* 13 bits, 625/256, PARA_UP */
    LEDC(0x04) = 0;                                /* HPOINT */
    LEDC(0x08) = 4096u << 4;                       /* 50 % */
    LEDC(0x0C) = 1u << 31;                         /* DUTY_START */
    LEDC(0x00) = (1u << 2) | (1u << 4);            /* temporizador 0, SIG_OUT_EN, PARA_UP */
    buzon.res[0] = 1;
    espera_us(5000);
    LEDC(0x08) = 2048u << 4;                       /* 25 % */
    LEDC(0x0C) = 1u << 31;
    LEDC(0x00) |= 1u << 4;
    buzon.res[0] = 2;
    espera_us(5000);
    LEDC(0xC8) = 1u << 4;                          /* DUTY_CHNG_END_LSCH0 */
    LEDC(0x0C) = (1u << 31) | (1u << 30) | (4u << 20) | (2u << 10) | 512u;
    LEDC(0x00) |= 1u << 4;
    buzon.res[0] = 3;
    while (!(LEDC(0xC4) & (1u << 4))) {}
    buzon.res[1] = LEDC(0x10) >> 4;                /* DUTY_R: 4096 */
    buzon.res[2] = LEDC(0xA4);                     /* el contador del temporizador */
    LEDC(0xCC) = 1u << 4;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
