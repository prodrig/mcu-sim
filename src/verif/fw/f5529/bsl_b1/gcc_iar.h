/* gcc_iar.h — lo que el BSL USB de TI (escrito para IAR) pide a msp430-elf-gcc
 * (mcu-sim, M9). Se incluye antes de cada fuente (-include). */
#ifndef GCC_IAR_H
#define GCC_IAR_H
#include <msp430.h>
#include <stdint.h>
#define __VER__ 0
/* __data20_read_char y compañía, __delay_cycles, __disable_interrupt: los de in430.h */
#endif
