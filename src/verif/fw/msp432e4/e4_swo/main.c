/* ===========================================================================
 * e4_swo — La traza SWO del MSP432E401Y, hasta la sonda XDS110 (fase X1 de
 * doc/analisis_xds110.md)
 *
 * Como p4_swo: el ITM por el puerto 0, el TPIU en NRZ a 1 Mbit/s y TDO/SWO
 * en PC3, que ya es su funcion al salir del reset (los pines de JTAG/SWD de
 * PC0-PC3 salen con AFSEL a 1). A 16 MHz (el PIOSC), ACPR = 15.
 *
 *   ./build/mcu-sim placas/msp_exp432e401y.xml verif/fw/msp432e4/e4_swo/e4_swo.out \
 *       --swo memoria --ms=20
 * ===========================================================================*/
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"

static void itm_putc(char c) {
    while (ITM->PORT[0].u32 == 0) {}
    ITM->PORT[0].u8 = (uint8_t)c;
}
static void itm_puts(const char* s) { while (*s) itm_putc(*s++); }
static void itm_dec(unsigned v) {
    char b[11]; int n = 0;
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) itm_putc(b[--n]);
}

int main(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    TPIU->SPPR = 2;                                 /* NRZ (UART)              */
    TPIU->ACPR = 16000000u / 1000000u - 1u;         /* 1 Mbit/s a 16 MHz       */
    TPIU->FFCR = 0x100;
    ITM->LAR = 0xC5ACCE55u;
    ITM->TCR = (1u << 16) | ITM_TCR_ITMENA_Msk;
    ITM->TPR = 0;
    ITM->TER = 1u;
    for (unsigned i = 1; i <= 3; ++i) {
        itm_puts("hola desde el MSP432E401Y por SWO, linea ");
        itm_dec(i);
        itm_putc('\n');
    }
    for (;;) __WFI();
}
