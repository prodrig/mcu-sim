/* fr59_basico.c — testfr5 (M8): lo que hace un programa del FR5969
 * (registros, sin DriverLib; -mlarge):
 *
 *   arranque  SYSRSTIV y la etapa (en FRAM: lo que sigue a un PUC o a
 *             LPM3.5); el DCO a 8 MHz; la UCA0 (P2.0/P2.1) a 115200; el ID
 *             de la TLV; el LFXT en PJ.4/PJ.5.
 *   ADC12_B   el sensor (A30 con TCMAP) con la referencia de 2,0 V y su
 *             calibración de la TLV, y AVCC/2 (A31 con BATMAP).
 *   Comp_E    C2 (P1.2) contra la referencia de 1,2 V: 0 y, cuando el banco
 *             sube P1.2, 1.
 *   AES256    el vector de FIPS-197 (AES-128).
 *   DMA       un bloque de 16 palabras por DMAREQ.
 *   RTC_B     el calendario con el LFXT: de 12:59:58 a 13:00:00.
 *   TB0       TB0.3 en P3.4 al 25 %.
 *   MPU       el segmento 2 (0x8000-0xBFFF) de solo lectura: escribir da la
 *             NMI (SYSSNIV = 0x0E); con MPUSEG2VS, un PUC (SYSRSTIV = 0x2C).
 *   IPE       la FRAM de arriba (0x10000-0x13FFF) encapsulada: leerla desde
 *             fuera da 0x3FFF y la NMI (SYSSNIV = 0x08); desde dentro, el
 *             dato.
 *   LPM3.5    el RTC_B con RTCRDYIE: despierta con SYSRSTIV = 0x08.
 *
 * Todo sale por la UART en texto. */
#include <msp430.h>
#include <stdint.h>

static void pon(char c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void hex(uint32_t v, int n) { while (n--) pon("0123456789ABCDEF"[(v >> (4 * n)) & 15]); }
static void dec(long v) { char b[12]; int i = 0; if (v < 0) { pon('-'); v = -v; } do { b[i++] = '0' + v % 10; v /= 10; } while (v); while (i) pon(b[--i]); }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }
static void espera_ms(unsigned ms) { while (ms--) __delay_cycles(8000); }

/* La etapa sobrevive al PUC y a LPM3.5 (FRAM) */
__attribute__((persistent)) volatile uint16_t etapa = 0;
volatile unsigned n_nmi;
volatile uint16_t sniv;

__attribute__((section(".upper.rodata"))) const volatile uint16_t secreto[4] = {0xCAFE, 0xBEEF, 0x1234, 0x5678};
__attribute__((section(".upper.text"), noinline)) uint16_t lee_secreto(unsigned i) { return secreto[i]; }

static void reloj(void) {
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                    /* 8 MHz */
    CSCTL2 = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
    P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1);
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK;
    UCA0BRW = 4;
    UCA0MCTLW = (0x55 << 8) | (5 << 4) | UCOS16;
    UCA0CTLW0 &= ~UCSWRST;
}
static void lfxt(void) {
    PJSEL0 |= BIT4 | BIT5;
    CSCTL0_H = CSKEY_H;
    CSCTL4 &= ~LFXTOFF;
    do { CSCTL5 &= ~LFXTOFFG; SFRIFG1 &= ~OFIFG; } while (SFRIFG1 & OFIFG);
    CSCTL0_H = 0;
}
static unsigned adc(unsigned canal) {
    ADC12CTL0 &= ~ADC12ENC;
    ADC12CTL0 = ADC12SHT0_8 | ADC12ON;
    ADC12CTL1 = ADC12SHP;
    ADC12CTL2 = ADC12RES_2;
    ADC12CTL3 = ADC12TCMAP | ADC12BATMAP;
    ADC12MCTL0 = ADC12VRSEL_1 | canal;
    ADC12IFGR0 = 0;
    ADC12CTL0 |= ADC12ENC | ADC12SC;
    while (!(ADC12IFGR0 & ADC12IFG0)) ;
    const unsigned r = ADC12MEM0;
    ADC12CTL0 &= ~(ADC12ENC | ADC12ON);
    return r;
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    const uint16_t rst = SYSRSTIV;
    PM5CTL0 &= ~LOCKLPM5;
    reloj();
    if (etapa == 2) {                                      /* tras el PUC de la MPU */
        cad("mpu puc rstiv "); hex(rst, 2); pon('\n');
        MPUCTL0 = MPUPW; MPUCTL0_H = 0;                     /* la MPU, apagada */
        etapa = 3;
    } else if (etapa == 4) {                               /* tras LPM3.5 */
        cad("lpm35 despierta rstiv "); hex(rst, 2); cad(" rtc "); dec(RTCCTL01_L & RTCRDYIFG); pon('\n');
        etapa = 0;
        cad("fin\n");
        for (;;) __bis_SR_register(LPM0_bits);
    } else {
        etapa = 0;
        cad("hola FR5969 id "); hex(*(volatile uint16_t*)0x1A04, 4); cad(" rst "); hex(rst, 2); pon('\n');
    }
    if (etapa == 0) {
        lfxt();
        cad("lfxt ok\n");

        /* ADC12_B: el sensor y AVCC/2, con la referencia de 2,0 V */
        REFCTL0 = REFVSEL_1 | REFON;
        while (!(REFCTL0 & REFGENRDY)) ;
        {
            const long t = adc(ADC12INCH_30), b = adc(ADC12INCH_31);
            const long c30 = *(volatile uint16_t*)0x1A1E, c85 = *(volatile uint16_t*)0x1A20;
            const long dt = (t - c30) * 550 / (c85 - c30) + 300;      /* décimas de °C */
            cad("adc t "); dec(t); cad(" c30 "); dec(c30); cad(" temp "); dec(dt); cad(" bat "); dec(b); pon('\n');
        }

        /* Comp_E: C2 contra 1,2 V */
        P1SEL0 |= BIT2; P1SEL1 |= BIT2;
        CECTL0 = CEIPEN | CEIPSEL_2;
        CECTL2 = CEREFL_1 | CERS_3 | CERSEL;
        CECTL1 = CEON;
        __delay_cycles(800);
        cad("ce "); dec(CECTL1 & CEOUT); cad(" espera\n");
        while (!(CECTL1 & CEOUT)) ;
        cad("ce sube\n");
        CECTL1 = 0;
        REFCTL0 = 0;

        /* AES256: FIPS-197, C.1 */
        {
            static const uint8_t k[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
            static const uint8_t p[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
            uint8_t c[16];
            AESACTL0 = AESOP_0 | AESKL_0;
            for (unsigned i = 0; i < 16; i += 2) AESAKEY = (uint16_t)(k[i] | (k[i + 1] << 8));
            for (unsigned i = 0; i < 16; i += 2) AESADIN = (uint16_t)(p[i] | (p[i + 1] << 8));
            while (AESASTAT & AESBUSY) ;
            for (unsigned i = 0; i < 16; i += 2) { const uint16_t w = AESADOUT; c[i] = (uint8_t)w; c[i + 1] = (uint8_t)(w >> 8); }
            cad("aes ");
            for (unsigned i = 0; i < 16; ++i) hex(c[i], 2);
            pon('\n');
        }

        /* DMA: 16 palabras por DMAREQ, en bloque */
        {
            static uint16_t org[16], dst[16];
            uint16_t s = 0;
            for (unsigned i = 0; i < 16; ++i) org[i] = (uint16_t)(i * 1000 + 7);
            DMACTL0 = DMA0TSEL_0;
            __data16_write_addr((unsigned short)(uintptr_t)&DMA0SA, (uintptr_t)org);
            __data16_write_addr((unsigned short)(uintptr_t)&DMA0DA, (uintptr_t)dst);
            DMA0SZ = 16;
            DMA0CTL = DMADT_1 | DMASRCINCR_3 | DMADSTINCR_3 | DMAEN;
            DMA0CTL |= DMAREQ;
            __no_operation();
            for (unsigned i = 0; i < 16; ++i) s = (uint16_t)(s + dst[i]);
            cad("dma suma "); dec(s); cad(" ifg "); dec((DMA0CTL & DMAIFG) != 0); pon('\n');
        }

        /* RTC_B: de 12:59:58 a 13:00:00 */
        RTCCTL01 = RTCHOLD | RTCBCD;
        RTCHOUR = 0x12; RTCMIN = 0x59; RTCSEC = 0x58;
        RTCCTL01 &= ~RTCHOLD;
        while (RTCSEC != 0x00) ;
        cad("rtc "); hex(RTCHOUR, 2); pon(':'); hex(RTCMIN, 2); pon(':'); hex(RTCSEC, 2); pon('\n');

        /* TB0.3 en P3.4: 25 % */
        P3DIR |= BIT4; P3SEL0 |= BIT4; P3SEL1 &= ~BIT4;
        TB0CCR0 = 8000 - 1; TB0CCR3 = 2000; TB0CCTL3 = OUTMOD_7;
        TB0CTL = TBSSEL__SMCLK | MC__UP | TBCLR;
        cad("tb0\n");
        espera_ms(10);
        TB0CTL = 0;

        /* La IPE: 0x10000-0x13FFF */
        __bis_SR_register(GIE);
        MPUCTL0 = MPUPW;
        MPUIPSEGB1 = 0x1000; MPUIPSEGB2 = 0x1400;
        MPUIPC0 = MPUIPENA;
        MPUCTL0 = MPUPW | MPUSEGIE;
        MPUCTL0_H = 0;
        {
            const uint16_t fuera = secreto[0];
            const uint16_t dentro = lee_secreto(1);
            cad("ipe fuera "); hex(fuera, 4); cad(" dentro "); hex(dentro, 4); cad(" nmi "); dec(n_nmi); cad(" sniv "); hex(sniv, 2); pon('\n');
        }
        n_nmi = 0; sniv = 0;

        /* La MPU: el segmento 2 (0x8000-0xBFFF), de solo lectura */
        MPUCTL0 = MPUPW;
        MPUSEGB1 = 0x0800; MPUSEGB2 = 0x0C00;
        MPUSAM = MPUSEG1RE | MPUSEG1WE | MPUSEG1XE | MPUSEG2RE | MPUSEG3RE | MPUSEG3WE | MPUSEG3XE | MPUSEGIRE | MPUSEGIWE;
        MPUCTL0 = MPUPW | MPUENA | MPUSEGIE;
        MPUCTL0_H = 0;
        {
            volatile uint16_t* p = (volatile uint16_t*)0x9000;
            const uint16_t antes = *p;
            *p = 0x1234;
            cad("mpu nmi "); dec(n_nmi); cad(" sniv "); hex(sniv, 2); cad(" antes "); hex(antes, 4); cad(" ahora "); hex(*p, 4); pon('\n');
        }
        /* y con MPUSEG2VS, un PUC */
        etapa = 2;
        fin_tx();
        MPUCTL0_H = MPUPW_H;                               /* solo la contraseña: MPUENA sigue */
        MPUSAM |= MPUSEG2VS;
        MPUCTL0_H = 0;
        *(volatile uint16_t*)0x9000 = 0x5678;
        cad("mpu sin puc\n");
        for (;;) ;
    }

    /* LPM3.5 con el RTC_B */
    lfxt();
    RTCCTL01 = RTCHOLD | RTCBCD;
    RTCCTL01 = RTCBCD | RTCRDYIE;
    etapa = 4;
    cad("lpm35\n");
    fin_tx();
    PMMCTL0_H = PMMPW_H;
    PMMCTL0_L |= PMMREGOFF;
    __bis_SR_register(LPM3_bits);
    cad("lpm35 no entra\n");
    for (;;) ;
}

__attribute__((interrupt(SYSNMI_VECTOR))) void nmi(void) { sniv = SYSSNIV; ++n_nmi; }
