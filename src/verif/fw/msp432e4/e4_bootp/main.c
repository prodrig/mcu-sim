/* ===========================================================================
 * e4_bootp — Una aplicacion que se actualiza por Ethernet con el cargador de
 *            la ROM: ROM_UpdateEMAC(reloj) (T7)
 *
 * Como el ejemplo «boot_demo_emac» de TivaWare: pone el reloj a 120 MHz,
 * enciende el LED D1 (PN1) para que se vea que esta viva, y pasa el control
 * al cargador por Ethernet de la ROM, que pide la IP y el fichero por BOOTP,
 * lo trae por TFTP, lo graba desde 0x0 y resetea. Lo que arranca despues es
 * lo que haya servido el servidor TFTP (verif/eth/bootp_e4.py sirve e4_eth).
 *
 * El buzon: 0x08 estado = 50 justo antes de saltar a la ROM.
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/rom.h"
#include "ti/devices/msp432e4/driverlib/rom_map.h"

typedef struct { volatile uint32_t modo, arranques, estado, sysclk; } buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

int main(void) {
    buzon.arranques++;
    const uint32_t clk = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL |
                                            SYSCTL_CFG_VCO_480, 120000000);
    buzon.sysclk = clk;
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
    while (!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPION)) { }
    MAP_GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_1);
    MAP_GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);
    buzon.estado = 50;
    ROM_UpdateEMAC(clk);              /* no vuelve */
    for (;;) { }
}
