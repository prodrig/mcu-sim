/* ===========================================================================
 * p4_bsl — Una aplicacion que entra en el BSL del MSP432P401R (T5, D4)
 *
 * Lo que TI documenta para entrar en el BSL desde una aplicacion: llamar a
 * la funcion cuya direccion esta en 0x0020_2000 con los parametros en r0
 * (0xFC48FFFF: interfaz automatica, I2C en 0x48) [el BSL a medida de TI,
 * BSL432_BSLentry]. La primera vez que arranca, entra en el BSL; las demas
 * -despues de un LOAD_PC_32 o un REBOOT_RESET- se queda en un bucle, para
 * que el banco vea que ha vuelto.
 * ===========================================================================*/
#include <stdint.h>
#include "msp432p401r.h"

uint32_t SystemCoreClock = 3000000;
void SystemInit(void) {}

typedef struct {
    volatile uint32_t modo;              /* 0x00 */
    volatile uint32_t arranques;         /* 0x04 */
    volatile uint32_t estado;            /* 0x08 */
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

#define BSL_ENTRADA  (*((uint32_t *)0x00202000u))
#define BSL_PARAMS   0xFC48FFFFu

int main(void) {
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;
    buzon.arranques++;
    if (buzon.arranques == 1) {
        buzon.estado = 90;
        __disable_irq();
        ((void (*)(uint32_t))BSL_ENTRADA)(BSL_PARAMS);
    }
    buzon.estado = 100;
    for (;;) { }
}
