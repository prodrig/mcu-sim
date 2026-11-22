/* fr5994_basico.c — testfr5 (M8): lo que es del FR5994 (registros; -mlarge):
 *
 *   arranque  el DCO a 8 MHz; la UCA0 (P2.0/P2.1) a 115200; el ID de la TLV.
 *   memoria   la RAM del LEA (0x2C00-0x3BFF), que la CPU usa como RAM; la
 *             FRAM de arriba hasta 0x43FFF.
 *   CRC32     el de Ethernet (reflejado) de «123456789»: 0xCBF43926; el
 *             CRC16 de la norma (CRC-CCITT, MSB primero): 0x29B1.
 *   RTC_C     con RTCKEY: el calendario en BCD, de 23:59:59 a 00:00:01.
 *   microSD   la tarjeta de la LaunchPad por la UCB0 en SPI (P1.6 MOSI, P1.7
 *             MISO, P2.2 CLK, P4.0 CS; P7.2 la detección): CMD0, CMD8,
 *             ACMD41, CMD58; escribe el bloque 5 y lo lee.
 *
 * Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }

static uint8_t spi(uint8_t b) {
    while (!(UCB0IFG & UCTXIFG)) ;
    UCB0TXBUF = b;
    while (!(UCB0IFG & UCRXIFG)) ;
    return UCB0RXBUF;
}
static uint8_t orden(uint8_t c, uint32_t arg, uint8_t crc) {
    spi(0xFF);
    spi(0x40 | c); spi(arg >> 24); spi(arg >> 16); spi(arg >> 8); spi(arg); spi(crc);
    uint8_t r = 0xFF;
    for (unsigned i = 0; i < 10 && (r & 0x80); ++i) r = spi(0xFF);
    return r;
}
#define CS_BAJA()  (P4OUT &= ~BIT0)
#define CS_ALTA()  do { P4OUT |= BIT0; spi(0xFF); } while (0)

static uint8_t blq[512];

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;
    CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
    P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1);
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 4;
    UCA0MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
    cad("hola FR5994 id "); hex(*(volatile uint16_t*)0x1A04, 4); pon('\n');

    /* La RAM del LEA y la FRAM de arriba */
    {
        volatile uint16_t* l = (volatile uint16_t*)0x3000;
        *l = 0x5994;
        volatile uint16_t* f = (volatile uint16_t*)(uintptr_t)0x43F00UL;
        *f = 0xF00D;
        cad("mem lea "); hex(*l, 4); cad(" fram "); hex(*f, 4); pon('\n');
    }

    /* CRC32 y CRC16 */
    {
        static const char m[] = "123456789";
        CRC32INIRESW0 = 0xFFFF; CRC32INIRESW1 = 0xFFFF;
        for (unsigned i = 0; i < 9; ++i) CRC32DIW0_L = (uint8_t)m[i];
        const uint32_t r = ~(((uint32_t)CRC32RESRW1 << 16) | CRC32RESRW0);
        CRC16INIRESW0 = 0xFFFF;
        for (unsigned i = 0; i < 9; ++i) CRC16DIRBW0_L = (uint8_t)m[i];
        cad("crc32 "); hex(r, 8); cad(" crc16 "); hex(CRC16INIRESW0, 4); pon('\n');
    }

    /* RTC_C, con su clave (cuenta el LFXT, en PJ.4/PJ.5) */
    PJSEL0 |= BIT4 | BIT5;
    CSCTL0_H = CSKEY_H;
    CSCTL4 &= ~LFXTOFF;
    do { CSCTL5 &= ~LFXTOFFG; SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
    CSCTL0_H = 0;
    RTCCTL0_H = RTCKEY_H;
    RTCCTL13 = RTCHOLD | RTCBCD | RTCMODE;
    RTCTIM1_L = 0x23; RTCTIM0_H = 0x59; RTCTIM0_L = 0x59;
    RTCCTL13 &= ~RTCHOLD;
    RTCCTL0_H = 0;
    while (RTCTIM0_L != 0x01) ;
    cad("rtc "); hex(RTCTIM1_L, 2); pon(0x3A); hex(RTCTIM0_H, 2); pon(0x3A); hex(RTCTIM0_L, 2); pon('\n');

    /* La microSD: UCB0 en SPI maestro, modo 0, a 400 kHz */
    P1SEL1 |= BIT6 | BIT7; P1SEL0 &= ~(BIT6 | BIT7);
    P2SEL1 |= BIT2; P2SEL0 &= ~BIT2;
    P4DIR |= BIT0; P4OUT |= BIT0;
    P7DIR &= ~BIT2; P7REN |= BIT2; P7OUT |= BIT2;
    UCB0CTLW0 = UCSWRST | UCCKPH | UCMSB | UCMST | UCSYNC | UCSSEL__SMCLK;
    UCB0BRW = 20;
    UCB0CTLW0 &= ~UCSWRST;
    cad("sd detecta "); dec(!(P7IN & BIT2)); pon('\n');
    for (unsigned i = 0; i < 10; ++i) spi(0xFF);                   /* 80 relojes con CS alto */
    CS_BAJA();
    const uint8_t r0 = orden(0, 0, 0x95);
    const uint8_t r8 = orden(8, 0x1AA, 0x87);
    uint8_t eco = 0;
    for (unsigned i = 0; i < 4; ++i) eco = spi(0xFF);
    uint8_t r41 = 0xFF;
    unsigned n41 = 0;
    do { orden(55, 0, 0x01); r41 = orden(41, 0x40000000UL, 0x01); ++n41; } while (r41 != 0 && n41 < 100);
    orden(58, 0, 0x01);
    const uint8_t ocr = spi(0xFF); spi(0xFF); spi(0xFF); spi(0xFF);
    CS_ALTA();
    cad("sd r0 "); hex(r0, 2); cad(" r8 "); hex(r8, 2); cad(" eco "); hex(eco, 2); cad(" acmd41 "); dec(n41);
    cad(" ocr "); hex(ocr, 2); pon('\n');

    for (unsigned i = 0; i < 512; ++i) blq[i] = (uint8_t)(i * 7 + 3);
    CS_BAJA();
    uint8_t rw = orden(24, 5, 0x01);
    spi(0xFF); spi(0xFE);
    for (unsigned i = 0; i < 512; ++i) spi(blq[i]);
    spi(0xFF); spi(0xFF);
    uint8_t dr = spi(0xFF);
    while (spi(0xFF) == 0x00) ;
    CS_ALTA();
    for (unsigned i = 0; i < 512; ++i) blq[i] = 0;
    CS_BAJA();
    uint8_t rr = orden(17, 5, 0x01);
    while (spi(0xFF) != 0xFE) ;
    for (unsigned i = 0; i < 512; ++i) blq[i] = spi(0xFF);
    spi(0xFF); spi(0xFF);
    CS_ALTA();
    {
        unsigned mal = 0;
        for (unsigned i = 0; i < 512; ++i) if (blq[i] != (uint8_t)(i * 7 + 3)) ++mal;
        cad("sd escribe "); hex(rw, 2); cad(" resp "); hex(dr & 0x1F, 2); cad(" lee "); hex(rr, 2); cad(" mal "); dec(mal); pon('\n');
    }
    cad("fin\n");
    for (;;) __bis_SR_register(LPM0_bits);
}
