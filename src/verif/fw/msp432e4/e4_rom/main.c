/* ===========================================================================
 * e4_rom — Un firmware que SOLO usa la DriverLib de la ROM (T6, entrega 4)
 *
 * Todo lo de DriverLib va por MAP_xxx(), como en los ejemplos de TI:
 * rom_map.h lo convierte en ROM_xxx() -un salto por la tabla de la ROM-
 * siempre que rom.h tenga la funcion. Solo una no la tiene,
 * SysCtlClockFreqSet (TI la quito del rom.h del E4): esa, y solo esa, se
 * enlaza de DriverLib (sysctl.o). Si la ROM R1 no tuviera alguna de las demas
 * en su casilla, el firmware no funcionaria.
 *
 *   0  120 MHz con SysCtlClockFreqSet (cristal de 25 MHz y PLL), la UART0
 *      a 115200 con UARTConfigSetExpClk, un saludo, y PN0 parpadeando con
 *      GPIOPinWrite y SysCtlDelay, de la ROM
 *   1  igual hasta el saludo, y luego ROM_UpdateUART(): el cargador de la ROM
 *      con la UART0 ya configurada (sin autobaudios)
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/rom.h"
#include "ti/devices/msp432e4/driverlib/rom_map.h"

typedef struct {
    volatile uint32_t modo;          /* 0x00 lo pone el banco                 */
    volatile uint32_t arranques;     /* 0x04                                  */
    volatile uint32_t estado;        /* 0x08                                  */
    volatile uint32_t version;       /* 0x0C ROM_APITABLE[0]                  */
    volatile uint32_t sysclk;        /* 0x10 lo que devuelve SysCtlClockFreqSet */
    volatile uint32_t resc;          /* 0x14                                  */
    volatile uint32_t vueltas;       /* 0x18                                  */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static void dice(const char *s) {
    while (*s) MAP_UARTCharPut(UART0_BASE, *s++);
}

int main(void) {
    buzon.arranques++;
    buzon.resc = MAP_SysCtlResetCauseGet();
    MAP_SysCtlResetCauseClear(buzon.resc);
    buzon.version = ROM_VERSION;
    buzon.estado = 1;
    const uint32_t f = MAP_SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                                              SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480, 120000000);
    buzon.sysclk = f;
    buzon.estado = 2;
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    while (!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_UART0) ||
           !MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA) ||
           !MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPION)) { }
    MAP_GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    MAP_GPIOPinConfigure(GPIO_PA0_U0RX);
    MAP_GPIOPinConfigure(GPIO_PA1_U0TX);
    MAP_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    MAP_UARTConfigSetExpClk(UART0_BASE, f, 115200,
                            UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    dice("ROM R1\r\n");
    buzon.estado = 3;
    if (buzon.modo == 1) {
        MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);    /* D1: en el cargador */
        buzon.estado = 10;
        ROM_UpdateUART();                        /* esta no tiene MAP_ */
        buzon.estado = 99;                                         /* no vuelve */
        for (;;) { }
    }
    buzon.estado = 100;
    for (uint32_t k = 0;; ++k) {
        MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, (k & 1u) ? GPIO_PIN_0 : 0);
        buzon.vueltas = k;
        MAP_SysCtlDelay(f / 3000u);                   /* ~1 ms: 3 ciclos por vuelta */
    }
}
