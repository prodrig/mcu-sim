/* ===========================================================================
 * e4_retardo — Lo que tarda una vuelta de SysCtlDelay a 120 MHz
 *
 * Con FWS = 5, los ciclos de SYSCLK de una vuelta del bucle de SysCtlDelay
 * (subs/bne), medidos con el TIMER2 de 32 bits: alineado en una linea de
 * 256 bits de la Flash, cruzando una linea (por la busqueda adelantada del
 * nucleo o por el propio bucle), desde la SRAM, el de la ROM y el de
 * DriverLib. Lo saca por la UART0 (el backchannel) y acaba en "fin".
 *
 * En el chip los cuatro dan lo mismo: dos juegos de 2 x 256 bits de
 * prebusqueda [DS] tienen la linea en curso y la siguiente. Un modelo con una
 * sola linea de prebusqueda cobraba los FWS dos veces por vuelta al cruzar
 * una linea (14 ciclos en vez de 4): `make e4-retardo` lo vigila.
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/rom.h"
#include "ti/devices/msp432e4/driverlib/rom_map.h"

extern void d_alineado(uint32_t), d_cruza28(uint32_t), d_cruza30(uint32_t), d_ram(uint32_t);

static void dice(const char *s) { while (*s) MAP_UARTCharPut(UART0_BASE, *s++); }
static void num(uint32_t v) {
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    dice(&b[i]);
}
static uint32_t mide(void (*fn)(uint32_t), uint32_t n) {
    const uint32_t a = TIMER2->TAV;            /* cuenta hacia abajo */
    fn(n);
    return a - TIMER2->TAV;
}
/* Ciclos por vuelta, en centesimas: la diferencia entre 101000 y 1000
 * vueltas quita la llamada y la medida */
static void prueba(const char *nom, void (*fn)(uint32_t)) {
    const uint32_t c1 = mide(fn, 1000u), c2 = mide(fn, 101000u);
    const uint32_t x100 = (c2 - c1) / 1000u;
    dice(nom); dice(": "); num(x100 / 100u); dice(".");
    num((x100 % 100u) / 10u); num(x100 % 10u); dice(" ciclos/vuelta\r\n");
}

int main(void) {
    const uint32_t f = MAP_SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN |
                                              SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480, 120000000);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER2);
    while (!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_UART0) ||
           !MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER2) ||
           !MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) { }
    MAP_GPIOPinConfigure(GPIO_PA0_U0RX);
    MAP_GPIOPinConfigure(GPIO_PA1_U0TX);
    MAP_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    MAP_UARTConfigSetExpClk(UART0_BASE, f, 115200,
                            UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    MAP_TimerConfigure(TIMER2_BASE, TIMER_CFG_PERIODIC);
    MAP_TimerLoadSet(TIMER2_BASE, TIMER_A, 0xFFFFFFFFu);
    MAP_TimerEnable(TIMER2_BASE, TIMER_A);

    dice("e4_retardo: f="); num(f); dice(" FWS="); num(SYSCTL->MEMTIM0 & 0xFu); dice("\r\n");
    prueba("flash alineado ", d_alineado);
    prueba("flash cruza 28 ", d_cruza28);
    prueba("flash cruza 30 ", d_cruza30);
    prueba("sram           ", d_ram);
    prueba("ROM            ", ROM_SysCtlDelay);
    prueba("DriverLib      ", SysCtlDelay);
    dice("fin\r\n");
    for (;;) { }
}
