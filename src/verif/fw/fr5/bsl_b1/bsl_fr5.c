/* =============================================================================
 * bsl_fr5.c — El BSL B1 de las FR5xx/FR6xx (FR5969, FR5994, FR6989): uno
 * propio (AGPLv3, de mcu-sim), con el protocolo y las órdenes de TI
 * [BSLFR SLAU550G §3-§4], en la ROM del BSL (0x1000-0x17FF).
 *
 * No lleva código de TI: el MSPBSL 1.02.00.01 trae su intérprete, su interfaz
 * eUSCI y su API de FRAM con la «Limited License» de TI (solo con
 * dispositivos de TI), que no deja ponerlos en el simulador.
 *
 *   La Z-Area (0x1000-0x100F): 0x1000 salta al BSL; 0x1002, la «BSL Action»
 *   (R12 = función, R13 = 0xDEAD, R14 = 0xBEEF: vuelve al BSL); lo demás,
 *   JMP $.
 *   El arranque: el perro parado, MCLK = SMCLK = DCO a 8 MHz, la RAM
 *   0x1C00-0x1FFF borrada (la pila y los búferes van ahí), la MPU abierta.
 *   La UART: eUSCI_A0 en P2.0/P2.1 (SEL 10 en FR5969/FR5994, 01 en FR6989),
 *   9600 8E1.
 *   La trama: 0x80, NL, NH, el núcleo (orden y datos) y el CRC-CCITT del
 *   núcleo (0xFFFF de partida), en little endian. Cada paquete se contesta con
 *   un byte (0x00 ACK; 0x51 cabecera, 0x52 CRC, 0x53 tamaño 0, 0x54 tamaño
 *   grande, 0x56 baudios) y, si toca, con un paquete: 0x3B y un mensaje, o
 *   0x3A y datos.
 *   Las órdenes: 0x10 RX Data Block, 0x11 RX Password, 0x15 Mass Erase, 0x16
 *   CRC Check, 0x17 Load PC, 0x18 TX Data Block, 0x19 TX BSL Version, 0x1B
 *   RX Data Block Fast, 0x52 Change Baud Rate; las demás, 0x3B 0x07.
 *   La contraseña: los 32 bytes de 0xFFE0-0xFFFF. Si no es, el borrado masivo
 *   (salvo con las firmas de 0xFF84/0xFF86 a 0xAAAA). El borrado masivo deja
 *   0xFF en la FRAM principal (0x4400-0xFFFF y lo de encima de 0x10000), no en
 *   la información, y vuelve a empezar el BSL (bloqueado y a 9600): es lo que
 *   hace el chip («resets the device», y la herramienta sigue hablando con el
 *   BSL tras un segundo).
 *   La versión: la del chip [BSLFRAM §7]: 00.07.34.B2 (FR5969, FR6989) y
 *   00.08.35.B3 (FR5994), por el ID de la TLV.
 * =============================================================================
 */
#include <msp430.h>
#include <stdint.h>

#define BUF 260u                        /* el búfer del BSL: 256 datos + orden y dirección */

static uint8_t rx[BUF + 4];
static uint16_t n_rx;
static uint8_t bloqueado;
static uint8_t es6989;

/* ---- La UART (eUSCI_A0) ---------------------------------------------------- */
static void uart_baudios(uint8_t d) {
    UCA0CTLW0 = UCSWRST | UCSSEL__SMCLK | UCPEN | UCPAR;          /* 8E1 */
    switch (d) {                                                   /* SMCLK = 8 MHz [UG FR5 tabla 30-5] */
    case 3: UCA0BRW = 26; UCA0MCTLW = (0xB6 << 8) | (0 << 4) | UCOS16; break;  /* 19200 */
    case 4: UCA0BRW = 13; UCA0MCTLW = (0x84 << 8) | (0 << 4) | UCOS16; break;  /* 38400 */
    case 5: UCA0BRW = 8;  UCA0MCTLW = (0xF7 << 8) | (10 << 4) | UCOS16; break; /* 57600 */
    case 6: UCA0BRW = 4;  UCA0MCTLW = (0x55 << 8) | (5 << 4) | UCOS16; break;  /* 115200 */
    default: UCA0BRW = 52; UCA0MCTLW = (0x49 << 8) | (1 << 4) | UCOS16; break; /* 9600 */
    }
    UCA0CTLW0 &= ~UCSWRST;
}
static void tx(uint8_t c) { while (!(UCA0IFG & UCTXIFG)) ; UCA0TXBUF = c; }
static uint8_t rxb(void) { while (!(UCA0IFG & UCRXIFG)) ; return UCA0RXBUF; }
static void fin_tx(void) { while (UCA0STATW & UCBUSY) ; }

/* ---- El CRC-CCITT [UG FR5 cap. 15: el del módulo CRC16] ------------------- */
static uint16_t crc_byte(uint16_t c, uint8_t b) {
    uint16_t x = (uint16_t)((c >> 8) ^ b);
    x ^= x >> 4;
    return (uint16_t)((c << 8) ^ (x << 12) ^ (x << 5) ^ x);
}

/* ---- Las respuestas ---------------------------------------------------------- */
static uint16_t crc_tx;
static void abre(uint16_t n) { tx(0x80); tx((uint8_t)n); tx((uint8_t)(n >> 8)); crc_tx = 0xFFFF; }
static void pon(uint8_t b) { tx(b); crc_tx = crc_byte(crc_tx, b); }
static void cierra(void) { tx((uint8_t)crc_tx); tx((uint8_t)(crc_tx >> 8)); }
static void mensaje(uint8_t m) { abre(2); pon(0x3B); pon(m); cierra(); }

/* Toda la memoria (20 bits: -mlarge) */
static uint8_t lee8(uint32_t a) { return *(volatile uint8_t*)(uintptr_t)a; }
static void escribe8(uint32_t a, uint8_t v) { *(volatile uint8_t*)(uintptr_t)a = v; }

/* ---- El borrado masivo y el arranque --------------------------------------- */
static void borra(void) {
    const uint16_t id = *(volatile uint16_t*)0x1A04;
    const uint32_t ini = id == 0x82A1 ? 0x4000 : 0x4400;                       /* FR5994: desde 0x4000 */
    const uint32_t fin = id == 0x82A1 ? 0x44000 : (id == 0x81A8 ? 0x24000 : 0x14000);
    for (uint32_t a = ini; a < fin; ++a) escribe8(a, 0xFF);
}
void bsl_inicio(void) __attribute__((noreturn));

static uint8_t version[4];

static void orden(void) {
    const uint8_t c = rx[0];
    const uint32_t a = rx[1] | ((uint32_t)rx[2] << 8) | ((uint32_t)rx[3] << 16);
    const uint16_t n = (uint16_t)(rx[4] | (rx[5] << 8));
    switch (c) {
    case 0x11: {                                                   /* RX Password */
        uint8_t mal = n_rx != 33;
        for (uint8_t i = 0; i < 32; ++i) mal |= lee8(0xFFE0 + i) ^ rx[1 + i];
        if (!mal) { bloqueado = 0; mensaje(0x00); return; }
        if (*(volatile uint16_t*)0xFF84 == 0xAAAA && *(volatile uint16_t*)0xFF86 == 0xAAAA) { mensaje(0x05); return; }
        fin_tx();
        borra();
        bsl_inicio();
    }
    case 0x15:                                                     /* Mass Erase */
        fin_tx();
        borra();
        bsl_inicio();
    case 0x19:                                                     /* TX BSL Version */
        abre(5); pon(0x3A);
        for (uint8_t i = 0; i < 4; ++i) pon(version[i]);
        cierra();
        return;
    default: break;
    }
    if (c != 0x10 && c != 0x1B && c != 0x16 && c != 0x17 && c != 0x18) { mensaje(0x07); return; }
    if (bloqueado) { if (c != 0x1B) mensaje(0x04); return; }
    switch (c) {
    case 0x10:                                                     /* RX Data Block */
    case 0x1B:
        for (uint16_t i = 4; i < n_rx; ++i) escribe8(a + i - 4, rx[i]);
        if (c == 0x10) mensaje(0x00);
        return;
    case 0x16: {                                                   /* CRC Check */
        uint16_t k = 0xFFFF;
        for (uint16_t i = 0; i < n; ++i) k = crc_byte(k, lee8(a + i));
        abre(3); pon(0x3A); pon((uint8_t)k); pon((uint8_t)(k >> 8)); cierra();
        return;
    }
    case 0x17:                                                     /* Load PC */
        fin_tx();
        ((void (*)(void))(uintptr_t)a)();
        return;
    case 0x18: {                                                   /* TX Data Block, en paquetes de 256 */
        uint32_t p = a;
        uint16_t q = n;
        while (q) {
            const uint16_t t = q > 256 ? 256 : q;
            abre((uint16_t)(t + 1)); pon(0x3A);
            for (uint16_t i = 0; i < t; ++i) pon(lee8(p + i));
            cierra();
            p += t; q -= t;
        }
        return;
    }
    }
}

/* Un paquete: devuelve el byte de respuesta (0x00 si está bien) */
static uint8_t paquete(void) {
    /* un byte con error de trama o paridad no llega (RXEIE = 0) [UG FR5 §30.3.6] */
    const uint8_t h = rxb();
    if (h != 0x80) return 0x51;
    const uint16_t n = rxb();
    const uint16_t nn = (uint16_t)(n | (rxb() << 8));
    if (nn == 0) return 0x53;
    if (nn > BUF) { for (uint16_t i = 0; i < nn + 2u; ++i) (void)rxb(); return 0x54; }
    uint16_t k = 0xFFFF;
    for (uint16_t i = 0; i < nn; ++i) { const uint8_t b = rxb(); rx[i] = b; k = crc_byte(k, b); }
    const uint16_t ck = (uint16_t)(rxb() | (rxb() << 8));
    n_rx = nn;
    return ck == k ? 0x00 : 0x52;
}

__attribute__((noreturn)) void bsl_principal(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
    MPUCTL0 = MPUPW;                                               /* la MPU, apagada */
    MPUCTL0_H = 0;
    CSCTL0_H = CSKEY_H;
    CSCTL1 = DCOFSEL_6;                                            /* 8 MHz */
    CSCTL2 = SELA__VLOCLK | SELS__DCOCLK | SELM__DCOCLK;
    CSCTL3 = DIVA__1 | DIVS__1 | DIVM__1;
    CSCTL0_H = 0;
    const uint16_t id = *(volatile uint16_t*)0x1A04;
    es6989 = id == 0x81A8;
    if (id == 0x82A1) { version[0] = 0x00; version[1] = 0x08; version[2] = 0x35; version[3] = 0xB3; }
    else { version[0] = 0x00; version[1] = 0x07; version[2] = 0x34; version[3] = 0xB2; }
    if (es6989) { P2SEL0 |= BIT0 | BIT1; P2SEL1 &= ~(BIT0 | BIT1); }
    else { P2SEL1 |= BIT0 | BIT1; P2SEL0 &= ~(BIT0 | BIT1); }
    bloqueado = 1;
    uart_baudios(2);
    for (;;) {
        const uint8_t r = paquete();
        if (r) { tx(r); continue; }
        if (rx[0] == 0x52) {                                       /* Change Baud Rate */
            if (n_rx != 2 || rx[1] < 2 || rx[1] > 6) { tx(0x56); continue; }
            tx(0x00);
            fin_tx();
            uart_baudios(rx[1]);
            continue;
        }
        tx(0x00);
        orden();
    }
}
