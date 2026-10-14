/* ===========================================================================
 * system_msp432p401r.h — Lo minimo que pide msp432p401r.h para compilar
 *
 * La cabecera de TI incluye este fichero. El de TI declara SystemInit() y
 * SystemCoreClock; los firmwares de verificacion de mcu-sim no usan ni uno
 * ni otro -cada uno programa su reloj a mano, que es lo que se prueba-, asi
 * que basta con declararlos. Es de mcu-sim (AGPLv3), no de TI.
 * ===========================================================================*/
#ifndef SYSTEM_MSP432P401R_H
#define SYSTEM_MSP432P401R_H
#include <stdint.h>
extern uint32_t SystemCoreClock;
void SystemInit(void);
#endif
