/* blinky.c — E3: GPIO8 (el LED de la placa) cambia cada 500 us, diez veces,
 * con la CPU a 160 MHz y el SYSTIMER como reloj. Luego se queda encendido
 * parpadeando lento para siempre (para verlo en mcu-sim). */
#include "c3.h"
int main(void) {
    reloj_160();
    IO_MUX(8) = IOMUX_GPIO | IOMUX_FUN_IE | (2u << 10);
    GPIO_FUNC_OUT(8) = 0x80;                 /* GPIO_OUT */
    GPIO_ENABLE_W1TS = 1u << 8;
    buzon.magic = BUZON_MAGIC;
    for (int i = 0; i < 10; ++i) {
        GPIO_OUT ^= 1u << 8;
        buzon.res[0] = i + 1;
        buzon.res[1] = (GPIO_IN >> 8) & 1u;
        espera_us(500);
    }
    buzon.fin = 1;
    for (;;) { GPIO_OUT ^= 1u << 8; espera_us(250000); }
}
