/* =============================================================================
 * bsl_g2.c — El BSL B1 del MSP430G2553: uno propio (AGPLv3, de mcu-sim) con el
 * protocolo de 1xx/2xx/4xx [BSL SLAU319 cap. 2], en la ROM del BSL
 * (0x0C00-0x0FFF, 1 KB). No lleva código de TI.
 *
 *   La UART: por software con Timer0_A (SMCLK = DCO a 8 MHz, de la
 *   calibración de la información A), 9600 8E1, semidúplex: TX en P1.1 y RX
 *   en P1.5, los pines del BSL de los G2xx3.
 *   La trama: SYNC (0x80) -> DATA_ACK (0x90); luego HDR (0x80), CMD, L1, L2
 *   (iguales: los bytes de AL a Dn), AL, AH, LL, LH, D1..Dn, CKL, CKH: el XOR
 *   por palabras de HDR a Dn, invertido. Contesta DATA_ACK (0x90), DATA_NAK
 *   (0xA0) o una trama de datos (0x80, 0, L1, L2, datos y su suma).
 *   Las órdenes: 0x10 la contraseña (los 32 bytes de 0xFFE0); 0x12 escribir;
 *   0x14 leer; 0x16 borrar un segmento (o la principal con 0xA504); 0x18 el
 *   borrado masivo; 0x1A cargar el PC; 0x1C comprobar el borrado; 0x1E la
 *   versión (los 16 bytes de 0x0FF0); 0x20 cambiar los baudios y 0x21 el
 *   desplazamiento de memoria (aceptadas sin efecto: el G2553 va a 9600 y no
 *   pasa de 64 KB).
 *   La contraseña mal: DATA_NAK y el borrado masivo, salvo con BSLSKEY
 *   (0xFFDE) = 0xAA55 [UG2 §1.2: «no se borra»]; con BSLSKEY = 0x0000 el BSL
 *   no arranca y salta a la aplicación.
 * =============================================================================
 */
#include <msp430.h>
#include <stdint.h>

#define BIT_TX BIT1
#define BIT_RX BIT5
#define T_BIT  833                      /* 8 MHz / 9600 */

static uint8_t buf[256];
static uint8_t abierto;

static uint16_t tt;                     /* el instante del siguiente bit, en TA0R */
static void paso(void) { tt += T_BIT; while ((int16_t)(TA0R - tt) < 0) ; }
static void tx(uint8_t c) {
    uint16_t w = (uint16_t)c << 1 | 0x400;                     /* inicio, datos, paridad y parada */
    for (uint8_t i = 0; i < 8; ++i) if ((c >> i) & 1) w ^= 0x200; /* la paridad par */
    tt = TA0R;
    for (uint8_t i = 0; i < 11; ++i) {
        if (w & 1) P1OUT |= BIT_TX; else P1OUT &= ~BIT_TX;
        w >>= 1;
        paso();
    }
}
static uint8_t rx(void) {
    while (P1IN & BIT_RX) ;
    tt = TA0R - T_BIT / 2;
    uint8_t c = 0;
    for (uint8_t i = 0; i < 10; ++i) {                         /* el centro de cada bit: inicio, 8 datos y la paridad */
        paso();
        if (i < 9) { c >>= 1; if (P1IN & BIT_RX) c |= 0x80; }
    }
    while (!(P1IN & BIT_RX)) ;                                 /* la parada */
    return c;
}
/* La suma: el XOR por palabras de la trama (s[0] los bytes pares, s[1] los impares) */
static uint8_t s[2];
static uint8_t k;
static void suma(uint8_t b) { s[k & 1] ^= b; ++k; }
static void txs(uint8_t b) { tx(b); suma(b); }

static void flash(uint16_t f1, uint16_t a, uint8_t v) {
    FCTL3 = FWKEY;                                             /* LOCK = 0 (LOCKA no cambia) */
    FCTL1 = f1;
    *(volatile uint8_t*)a = v;
    while (FCTL3 & BUSY) ;
    FCTL1 = FWKEY;
    FCTL3 = FWKEY | LOCK;
}
static void borrado_masivo(void) { flash(FWKEY | MERAS | ERASE, 0xFFFE, 0); }

static void datos(uint8_t n, const volatile uint8_t* p) {
    s[0] = s[1] = 0; k = 0;
    txs(0x80); txs(0x00); txs(n); txs(n);
    for (uint8_t i = 0; i < n; ++i) txs(p[i]);
    tx((uint8_t)(s[0] ^ 0xFF)); tx((uint8_t)(s[1] ^ 0xFF));
}

__attribute__((noreturn)) void bsl_principal(void) {
    WDTCTL = WDTPW | WDTHOLD;
    if (*(volatile uint16_t*)0xFFDE == 0x0000) ((void (*)(void))(*(volatile uint16_t*)0xFFFE))();
    if (CALBC1_8MHZ != 0xFF) { BCSCTL1 = CALBC1_8MHZ; DCOCTL = CALDCO_8MHZ; }
    FCTL2 = FWKEY | FSSEL_1 | 19;                              /* 8 MHz / 20 = 400 kHz */
    P1OUT |= BIT_TX; P1DIR |= BIT_TX; P1DIR &= ~BIT_RX;
    TA0CTL = TASSEL_2 | MC_2 | TACLR;
    abierto = 0;
    for (;;) {
        if (rx() != 0x80) continue;                            /* SYNC */
        tx(0x90);
        if (rx() != 0x80) continue;                            /* HDR */
        s[0] = 0x80; s[1] = 0; k = 1;
        uint8_t cmd, l1, l2;
        suma(cmd = rx()); suma(l1 = rx()); suma(l2 = rx());
        for (uint8_t i = 0; i < l1; ++i) suma(buf[i] = rx());
        suma(rx()); suma(rx());                                /* la suma: con ella, todo da 0xFF */
        if (l1 != l2 || l1 < 4 || (s[0] & s[1]) != 0xFF) { tx(0xA0); continue; }
        const uint16_t a = buf[0] | (buf[1] << 8), n = buf[2] | (buf[3] << 8);
        uint8_t r = 0x90;
        switch (cmd) {
        case 0x10: {                                           /* la contraseña */
            uint8_t mal = l1 != 36;
            for (uint8_t i = 0; i < 32; ++i) mal |= buf[4 + i] ^ *(volatile uint8_t*)(0xFFE0 + i);
            if (!mal) abierto = 1;
            else { r = 0xA0; if (*(volatile uint16_t*)0xFFDE != 0xAA55) borrado_masivo(); }
            break;
        }
        case 0x18: borrado_masivo(); break;                    /* el borrado masivo */
        case 0x1E: datos(16, (const volatile uint8_t*)0x0FF0); continue;
        case 0x20: case 0x21: break;
        default:
            if (!abierto) { r = 0xA0; break; }
            switch (cmd) {
            case 0x12:                                         /* escribir: RAM, periféricos o Flash */
                for (uint8_t i = 0; i < l1 - 4; ++i) {
                    const uint16_t d = a + i;
                    if (d >= 0x1000) flash(FWKEY | WRT, d, buf[4 + i]);
                    else *(volatile uint8_t*)d = buf[4 + i];
                }
                break;
            case 0x14: datos((uint8_t)n, (const volatile uint8_t*)a); continue;
            case 0x16: flash(n == 0xA504 ? (FWKEY | MERAS) : (FWKEY | ERASE), a, 0); break;
            case 0x1A: tx(0x90); ((void (*)(void))a)(); for (;;) ;
            case 0x1C:
                for (uint16_t i = 0; i < n; ++i) if (*(volatile uint8_t*)(a + i) != 0xFF) { r = 0xA0; break; }
                break;
            default: r = 0xA0; break;
            }
        }
        tx(r);
    }
}
