/* f5_usb_hw.c — testf5, F12 (M5): lo del módulo USB que no usa la pila de TI
 * del simpleUsbBackchannel, a nivel de registros [UG5 cap. 42]:
 *   1. Port U: PU.0/PU.1 como salidas (del carril VUSB) y como entradas;
 *   2. el temporizador de USB (UTSEL 5: 128 us) con UTIFG por USBVECINT, y la
 *      marca de tiempo (TSE3 por programa y el disparo del canal 0 del DMA);
 *   3. la suspensión y la reanudación remota (RWUP);
 *   4. la sobrecarga del LDO (VUOVLIFG, con OVLAOFF).
 * El DCO de fábrica (SMCLK = 1 MHz), la UCA1 a 9600; la PLL de XT2 (4 MHz). */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void dec(unsigned v) { char b[6]; int i = 0; do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void hex2(unsigned v) { pon("0123456789ABCDEF"[(v >> 4) & 15]); pon("0123456789ABCDEF"[v & 15]); }

volatile unsigned n_ut, susr, resr, ovl;

int main(void) {
    unsigned t1, t2, i;
    WDTCTL = WDTPW | WDTHOLD;
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 = UCSWRST | UCSSEL_2;
    UCA1BR0 = 109; UCA1BR1 = 0; UCA1MCTL = UCBRS_2;
    UCA1CTL1 &= ~UCSWRST;

    /* 1. Port U */
    USBKEYPID = USBKEY;
    USBPHYCTL = PUOPE | PUOUT0;                        /* PU.0 arriba, PU.1 abajo */
    cad("pu sale\n");
    __delay_cycles(20000);
    USBPHYCTL = PUIPE;                                 /* entradas */
    cad("pu entra\n");
    for (i = 0; i < 20000 && !(USBPHYCTL & PUIN1); ++i) ;
    cad("puin "); hex2(USBPHYCTL & (PUIN0 | PUIN1)); pon('\n');
    USBPHYCTL = PUSEL;

    /* La PLL desde XT2 y el módulo */
    P5SEL |= BIT2 | BIT3;
    UCSCTL6 &= ~XT2OFF;
    while (UCSCTL7 & XT2OFFG) { UCSCTL7 &= ~XT2OFFG; SFRIFG1 &= ~OFIFG; }
    USBPLLDIVB = USBPLL_SETCLK_4_0;
    USBPLLCTL = UPFDEN | UPLLEN;
    __delay_cycles(1000);
    USBPLLIR = 0;
    __delay_cycles(1000);
    cad("pll "); hex2(USBPLLIR); pon('\n');
    USBCNF = USB_EN;
    USBCTL = FEN;
    __enable_interrupt();

    /* 2. El temporizador y la marca de tiempo */
    USBMAINT = UTSEL_5 | UTIE | TSGEN | TSESEL_3;     /* 128 us: a 1 MHz, la RSI cabe */
    __delay_cycles(2000);
    USBMAINT = UTSEL_5 | TSGEN | TSESEL_3;
    cad("ut "); dec(n_ut); pon('\n');
    USBMAINT |= TSE3;
    t1 = USBTSREG;
    __delay_cycles(100);
    USBMAINT |= TSE3;
    t2 = USBTSREG;
    cad("ts "); dec(t2 - t1); pon('\n');
    USBMAINT = TSGEN | TSESEL_3;
    USBMAINT |= TSE3;
    t1 = USBTSREG;
    USBMAINT = TSGEN | TSESEL_0;                       /* el canal 0 del DMA */
    __delay_cycles(200);
    DMA0SA = (unsigned)&t1; DMA0DA = (unsigned)&t2; DMA0SZ = 1;
    DMA0CTL = DMAEN | DMAREQ;
    __delay_cycles(10);
    t2 = USBTSREG;
    cad("tsdma "); dec(t2 - t1); pon('\n');

    /* 3. La suspensión y RWUP */
    USBCNF |= PUR_EN;
    USBIE = SUSRIE | RESRIE;
    cad("susp listo\n");
    while (!resr) ;
    cad("susr "); dec(susr); cad(" resr "); dec(resr); pon('\n');

    /* 4. La sobrecarga del LDO */
    USBPWRCTL |= VUOVLIE | OVLAOFF;
    cad("ovl listo\n");
    while (!ovl) ;
    cad("ovl vusben "); pon((USBPWRCTL & VUSBEN) ? '1' : '0'); pon('\n');
    for (;;) ;
}

__attribute__((interrupt(USB_UBM_VECTOR))) void usb(void) {
    switch (USBVECINT) {
    case USBVECINT_PWR_DROP: ++ovl; break;
    case USBVECINT_USB_TIMESTAMP: ++n_ut; break;
    case USBVECINT_RSTR: break;
    case USBVECINT_SUSR: ++susr; USBCTL |= RWUP; break;
    case USBVECINT_RESR: ++resr; break;
    default: break;
    }
}
