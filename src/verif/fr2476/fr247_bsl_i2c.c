/* fr247_bsl_i2c.c — blink en P1.0 con la configuración del BSL del usuario
 * (M10) [BSLFRAM SLAU550AB §3.6.1.2, tabla 3-2]: en 0xFF88 la firma 0x695A;
 * en 0xFF8A, 0x5A06: solo I2C y sin borrar la RAM; en 0xFFA0, la dirección
 * I2C 0x60. Las secciones son las del guion de enlazado de TI. AGPLv3. */
#include <msp430.h>
#include <stdint.h>
__attribute__((section(".bslconfigsignature"), used)) const uint16_t firma_bsl = 0x695A;
__attribute__((section(".bslconfig"), used)) const uint16_t config_bsl = 0x5A00 | 0x0004 | 0x0002;
__attribute__((section(".bsli2caddress"), used)) const uint16_t dir_i2c_bsl = 0xFF00 | 0x60;
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    /* que --gc-sections no se las lleve: el guion de TI no las guarda */
    (void)*(volatile const uint16_t*)&firma_bsl; (void)*(volatile const uint16_t*)&config_bsl;
    (void)*(volatile const uint16_t*)&dir_i2c_bsl;
    PM5CTL0 &= ~LOCKLPM5;
    P1OUT &= ~BIT0;
    P1DIR |= BIT0;
    for (;;) {
        P1OUT ^= BIT0;
        __delay_cycles(20000);
    }
}
