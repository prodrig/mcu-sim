/* ===========================================================================
 * p4_swo — La traza SWO del MSP432P401R, hasta la sonda XDS110 (fase X1 de
 * doc/analisis_xds110.md)
 *
 * El printf de toda la vida por el ITM: el puerto de estimulo 0, el TPIU en
 * NRZ (el unico formato del XDS110) a 1 Mbit/s y TDO/SWO en PJ.5, su funcion
 * primaria. A 3 MHz, que es como sale el chip del reset, ACPR = 2. La pieza
 * Xds110 de la placa la recoge por J101 y la lleva a un canal:
 *
 *   ./build/mcu-sim placas/msp_exp432p401r.xml verif/fw/msp432p4/p4_swo/p4_swo.out \
 *       --swo memoria --ms=20
 *
 * Escribe tres lineas y se para (en un `wfi` sin interrupciones).
 * ===========================================================================*/
#ifndef NO_MSP_CLASSIC_DEFINES
#define NO_MSP_CLASSIC_DEFINES
#endif
#include "msp432p401r.h"

uint32_t SystemCoreClock = 3000000;
void SystemInit(void) {}

static void itm_putc(char c) {
    while (ITM->PORT[0].u32 == 0) {}             /* la FIFO del estimulo, libre */
    ITM->PORT[0].u8 = (uint8_t)c;
}
static void itm_puts(const char* s) { while (*s) itm_putc(*s++); }
static void itm_dec(unsigned v) {
    char b[11]; int n = 0;
    do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) itm_putc(b[--n]);
}

int main(void) {
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;
    /* TDO/SWO en PJ.5: la funcion primaria */
    PJ->SEL1 &= (uint8_t)~BIT5;
    PJ->SEL0 |= BIT5;
    /* La traza: TRCENA, el TPIU en NRZ a HCLK/(ACPR+1) y el ITM con el
     * puerto 0 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    TPIU->SPPR = 2;                                 /* NRZ (UART)              */
    TPIU->ACPR = 3000000u / 1000000u - 1u;          /* 1 Mbit/s a 3 MHz        */
    TPIU->FFCR = 0x100;                             /* sin formateador         */
    ITM->LAR = 0xC5ACCE55u;
    ITM->TCR = (1u << 16) | ITM_TCR_ITMENA_Msk;    /* TraceBusID 1, ITMENA    */
    ITM->TPR = 0;
    ITM->TER = 1u;
    for (unsigned i = 1; i <= 3; ++i) {
        itm_puts("hola desde el MSP432P401R por SWO, linea ");
        itm_dec(i);
        itm_putc('\n');
    }
    for (;;) __WFI();
}
