/* ===========================================================================
 * bsl_b1.c — El BSL sustituto del MSP432P401R (B1; T5, D4)
 *
 * Copyright (C) 2026 Pedro Rodriguez y colaboradores — AGPLv3, como el resto
 * del simulador. Escrito desde la DESCRIPCION del protocolo (SLAU622, «MSP432
 * Bootloader User's Guide») y de lo que espera BSL Scripter (su codigo, BSD):
 * NO lleva codigo del BSL de TI, cuya licencia («TI Text File License») no se
 * puede combinar con la AGPLv3 ni permite usarlo fuera de un chip de TI.
 *
 * DONDE: el sector del BSL de la Flash de informacion, 0x0020_2000-
 * 0x0020_3FFF. En 0x0020_2000, la direccion de la funcion de entrada; en
 * 0x0020_2010, diez bytes de version. La llama el codigo de arranque (con la
 * Flash en blanco) o una aplicacion:
 *
 *     ((void (*)(uint32_t))(*(uint32_t *)0x00202000))(0xFC48FFFF);
 *
 * QUE: solo la UART, la eUSCI_A0 en P1.2 (RXD) y P1.3 (TXD) -el backchannel
 * de la LaunchPad-, 8E1. Los baudios los mide del 0xFF de sincronia, como el
 * de TI: del final del bit de arranque al bit de paridad (que es 0) van 8
 * bits; 9600, 19200, 38400, 57600 o 115200, y luego CHANGE_BAUD_RATE. MCLK y
 * SMCLK a 12 MHz del DCO.
 *
 *   paquete    0x80, longitud (LE), datos, CRC-CCITT de los datos (LE,
 *              semilla 0xFFFF)
 *   ACK        0x00 al recibir bien un paquete; 0x51 cabecera, 0x52 CRC,
 *              0x53 longitud cero, 0x54 demasiado largo
 *   respuesta  0x3B y un codigo (0x00 bien, 0x04 cerrado, 0x05 contrasena
 *              mala, 0x07 orden desconocida, 0x01 la escritura no se
 *              comprueba), o 0x3A y datos
 *
 *   0x21 RX_PASSWORD        los 256 bytes de los vectores (0x0-0xFF)
 *   0x10/0x20 RX_DATA_BLOCK(_32)   a la Flash principal o a la SRAM
 *   0x12/0x22 ERASE_SECTOR(_32)    un sector de 4 KB de la principal
 *   0x15 MASS_ERASE         toda la principal
 *   0x16/0x26 CRC_CHECK(_32)       el CRC-CCITT de una zona
 *   0x17/0x27 LOAD_PC(_32)         salta (sin respuesta)
 *   0x18/0x28 TX_DATA_BLOCK(_32)   lee, en paquetes de 256
 *   0x19 TX_BSL_VERSION     los diez bytes de 0x0020_2010
 *   0x25 REBOOT_RESET       SYSCTL_REBOOT_CTL
 *   0x52 CHANGE_BAUD_RATE   2..6: 9600..115200 (solo el ACK, y cambia)
 *
 * LO QUE NO HACE el de TI y este no [P-16 bb]: SPI e I2C, el tiempo limite
 * que lo apaga (LPM4.5), FACTORY_RESET (0x30: «orden desconocida»), escribir
 * en la Flash de informacion, y las zonas IP.
 *
 * Sin variables globales: no hay arranque de C. Todo vive en la pila. Las
 * interrupciones quedan enmascaradas (PRIMASK) y el nucleo duerme en WFI
 * entre byte y byte: la peticion de la eUSCI lo despierta igual.
 * ===========================================================================*/
#include <stdint.h>
#include "msp432p401r.h"

#define F_SMCLK       12000000u
#define BUF           300u

void bsl_entrada(uint32_t params) __attribute__((noreturn, used));

__attribute__((section(".entrada"), used))
void (* const bsl_tabla)(uint32_t) = bsl_entrada;

/* Vendedor ("MS": mcu-sim, no TI), interprete, API, periferico, compilacion */
__attribute__((section(".version"), used))
const uint8_t bsl_version[10] = { 0x4D, 0x53, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01 };

/* Los divisores de la eUSCI a 12 MHz con UCOS16 [TRM, 24.3.10]:
 * {BRW, BRF, BRS} para 9600, 19200, 38400, 57600 y 115200 */
static const uint16_t BAUDIOS[5][3] = {
    { 78, 2, 0x00 }, { 39, 1, 0x00 }, { 19, 8, 0xAA }, { 13, 0, 0x49 }, { 6, 8, 0x11 }
};
/* Ciclos de 12 MHz por bit */
static const uint16_t CPB[5] = { 1250, 625, 313, 208, 104 };

static void uart(unsigned i) {
    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_PEN | EUSCI_A_CTLW0_PAR |
                      EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW = BAUDIOS[i][0];
    EUSCI_A0->MCTLW = (uint16_t)((BAUDIOS[i][2] << 8) | (BAUDIOS[i][1] << 4) | EUSCI_A_MCTLW_OS16);
    P1->SEL0 |= BIT2 | BIT3;
    P1->SEL1 &= (uint8_t)~(BIT2 | BIT3);
    EUSCI_A0->CTLW0 &= (uint16_t)~EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->IE = EUSCI_A_IE_RXIE;          /* solo para despertar del WFI */
}

static uint8_t rx(void) {
    for (;;) {
        if (EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG) return (uint8_t)EUSCI_A0->RXBUF;
        NVIC->ICPR[0] = 1u << EUSCIA0_IRQn;
        if (EUSCI_A0->IFG & EUSCI_A_IFG_RXIFG) continue;
        __WFI();
    }
}
static void tx(uint8_t b) {
    while (!(EUSCI_A0->IFG & EUSCI_A_IFG_TXIFG)) { }
    EUSCI_A0->TXBUF = b;
}
static void tx_fin(void) {
    while (EUSCI_A0->STATW & EUSCI_A_STATW_BUSY) { }
}

static uint16_t crc_byte(uint16_t crc, uint8_t d) {
    uint8_t x = (uint8_t)((crc >> 8) ^ d);
    x ^= (uint8_t)(x >> 4);
    return (uint16_t)((crc << 8) ^ ((uint16_t)x << 12) ^ ((uint16_t)x << 5) ^ x);
}

/* Un paquete de respuesta: 0x80, longitud, datos, CRC */
static void responde(const uint8_t* d, unsigned n) {
    uint16_t crc = 0xFFFF;
    tx(0x80); tx((uint8_t)n); tx((uint8_t)(n >> 8));
    for (unsigned i = 0; i < n; ++i) { tx(d[i]); crc = crc_byte(crc, d[i]); }
    tx((uint8_t)crc); tx((uint8_t)(crc >> 8));
}
static void mensaje(uint8_t c) {
    const uint8_t d[2] = { 0x3B, c };
    responde(d, 2);
}

/* Espera el 0xFF de sincronia y devuelve el indice de los baudios */
static unsigned autobaudios(void) {
    P1->SEL0 &= (uint8_t)~BIT2;
    P1->SEL1 &= (uint8_t)~BIT2;
    P1->DIR &= (uint8_t)~BIT2;
    P1->REN |= BIT2;
    P1->OUT |= BIT2;                          /* pull-up */
    P1->IES |= BIT2;                          /* flanco de bajada */
    SysTick->LOAD = 0x00FFFFFFu;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    for (;;) {
        P1->IFG &= (uint8_t)~BIT2;
        P1->IE |= BIT2;
        for (;;) {
            if (P1->IFG & BIT2) break;
            NVIC->ICPR[1] = 1u << (PORT1_IRQn - 32);
            if (P1->IFG & BIT2) break;
            __WFI();
        }
        P1->IE &= (uint8_t)~BIT2;
        /* Dentro del bit de arranque: hasta que suba, y de ahi hasta que baje
         * (la paridad de 0xFF es 0): ocho bits */
        uint32_t t0 = SysTick->VAL, t;
        int ok = 1;
        while (!(P1->IN & BIT2)) {
            if (((t0 - SysTick->VAL) & 0x00FFFFFFu) > 2u * 1400u) { ok = 0; break; }
        }
        if (!ok) continue;
        t0 = SysTick->VAL;
        while (P1->IN & BIT2) {
            if (((t0 - SysTick->VAL) & 0x00FFFFFFu) > 9u * 1400u) { ok = 0; break; }
        }
        if (!ok) continue;
        t = (t0 - SysTick->VAL) & 0x00FFFFFFu;
        for (unsigned i = 0; i < 5; ++i) {
            const uint32_t n = 8u * CPB[i];
            if (t > n - n / 10u && t < n + n / 10u) {
                while (!(P1->IN & BIT2)) { }  /* la paridad, y luego la parada */
                SysTick->CTRL = 0;
                return i;
            }
        }
    }
}

static int en_principal(uint32_t a, uint32_t n) {
    return a < 0x00040000u && n <= 0x00040000u - a;
}
static int en_sram(uint32_t a, uint32_t n) {
    return a >= 0x20000000u && a < 0x20010000u && n <= 0x20010000u - a;
}

static void flash_abre(void) {
    FLCTL->BANK0_MAIN_WEPROT = 0;
    FLCTL->BANK1_MAIN_WEPROT = 0;
    FLCTL->CLRIFG = 0xFFFFFFFFu;
}
static int flash_borra(uint32_t a, int masivo) {
    flash_abre();
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_CLR_STAT;
    FLCTL->ERASE_SECTADDR = a;
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_START |
                           (masivo ? FLCTL_ERASE_CTLSTAT_MODE : 0u);
    while ((FLCTL->ERASE_CTLSTAT & FLCTL_ERASE_CTLSTAT_STATUS_MASK) !=
           FLCTL_ERASE_CTLSTAT_STATUS_3) {
        if (FLCTL->ERASE_CTLSTAT & FLCTL_ERASE_CTLSTAT_ADDR_ERR) break;
    }
    const int ok = !(FLCTL->ERASE_CTLSTAT & FLCTL_ERASE_CTLSTAT_ADDR_ERR);
    FLCTL->ERASE_CTLSTAT = FLCTL_ERASE_CTLSTAT_CLR_STAT;
    return ok;
}
/* Programa n bytes: palabras donde se puede, bytes en los bordes */
static int flash_escribe(uint32_t a, const uint8_t* d, uint32_t n) {
    flash_abre();
    FLCTL->PRG_CTLSTAT = FLCTL_PRG_CTLSTAT_ENABLE;
    while (n) {
        if (!(a & 3u) && n >= 4u) {
            const uint32_t w = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                               ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
            *(volatile uint32_t*)a = w;
            a += 4; d += 4; n -= 4;
        } else {
            *(volatile uint8_t*)a = *d;
            a += 1; d += 1; n -= 1;
        }
        while (FLCTL->PRG_CTLSTAT & FLCTL_PRG_CTLSTAT_STATUS_MASK) { }
    }
    FLCTL->PRG_CTLSTAT = 0;
    return !(FLCTL->IFG & FLCTL_IFG_PRG_ERR);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void reloj(void) {
    FLCTL->BANK0_RDCTL = (FLCTL->BANK0_RDCTL & ~FLCTL_BANK0_RDCTL_WAIT_MASK) | FLCTL_BANK0_RDCTL_WAIT_1;
    FLCTL->BANK1_RDCTL = (FLCTL->BANK1_RDCTL & ~FLCTL_BANK1_RDCTL_WAIT_MASK) | FLCTL_BANK1_RDCTL_WAIT_1;
    CS->KEY = CS_KEY_VAL;
    CS->CTL0 = CS_CTL0_DCORSEL_3;                         /* 12 MHz */
    CS->CTL1 = CS_CTL1_SELM__DCOCLK | CS_CTL1_SELS__DCOCLK; /* MCLK = SMCLK = DCO */
    CS->KEY = 0;
}

void bsl_entrada(uint32_t params) {
    (void)params;                             /* solo hay UART */
    __disable_irq();
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;
    reloj();
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICER[1] = 0xFFFFFFFFu;
    NVIC->ISER[0] = 1u << EUSCIA0_IRQn;       /* para despertar del WFI */
    NVIC->ISER[1] = 1u << (PORT1_IRQn - 32);
    uart(autobaudios());
    tx(0x00);                                 /* la sincronia */

    uint8_t b[BUF];
    int abierto = 0;
    for (;;) {
        /* --- Un paquete ---------------------------------------------------- */
        uint8_t h = rx();
        if (h != 0x80) { tx(0x51); continue; }
        const unsigned n = (unsigned)rx() | ((unsigned)rx() << 8);
        if (n == 0) { (void)rx(); (void)rx(); tx(0x53); continue; }
        if (n > BUF - 4u) { tx(0x54); continue; }
        uint16_t crc = 0xFFFF;
        for (unsigned i = 0; i < n; ++i) { b[i] = rx(); crc = crc_byte(crc, b[i]); }
        const uint16_t crc_pc = (uint16_t)rx() | (uint16_t)((uint16_t)rx() << 8);
        if (crc != crc_pc) { tx(0x52); continue; }
        const uint8_t orden = b[0];
        if (orden == 0x52) {                  /* CHANGE_BAUD_RATE: solo el ACK */
            if (n < 2 || b[1] < 2 || b[1] > 6) { tx(0x56); continue; }
            tx(0x00);
            tx_fin();
            uart((unsigned)b[1] - 2u);
            continue;
        }
        tx(0x00);                             /* ACK: el paquete ha llegado bien */

        /* --- La orden ------------------------------------------------------ */
        const int de32 = orden >= 0x20 && orden < 0x30;
        const unsigned na = de32 ? 4u : 3u;
        const uint32_t a = n > na ? (de32 ? le32(b + 1) : (le32(b + 1) & 0x00FFFFFFu)) : 0u;
        const uint8_t* datos = b + 1 + na;
        const unsigned nd = n > 1u + na ? n - 1u - na : 0u;
        switch (orden) {
            case 0x21: {                      /* RX_PASSWORD */
                int bien = n == 257;
                for (unsigned i = 0; bien && i < 256; ++i)
                    bien = b[1 + i] == *(const volatile uint8_t*)i;
                abierto = bien;
                mensaje(bien ? 0x00 : 0x05);
                break;
            }
            case 0x10: case 0x20: {           /* RX_DATA_BLOCK */
                if (!abierto) { mensaje(0x04); break; }
                if (en_sram(a, nd)) {
                    for (unsigned i = 0; i < nd; ++i) ((volatile uint8_t*)a)[i] = datos[i];
                    mensaje(0x00);
                } else if (en_principal(a, nd)) {
                    int ok = flash_escribe(a, datos, nd);
                    for (unsigned i = 0; ok && i < nd; ++i)
                        ok = ((const volatile uint8_t*)a)[i] == datos[i];
                    mensaje(ok ? 0x00 : 0x01);
                } else {
                    mensaje(0x01);
                }
                break;
            }
            case 0x12: case 0x22:             /* ERASE_SECTOR */
                if (!abierto) { mensaje(0x04); break; }
                mensaje(en_principal(a, 1) && flash_borra(a & ~0xFFFu, 0) ? 0x00 : 0x02);
                break;
            case 0x15:                        /* MASS_ERASE */
                if (!abierto) { mensaje(0x04); break; }
                mensaje(flash_borra(0, 1) ? 0x00 : 0x02);
                break;
            case 0x16: case 0x26: {           /* CRC_CHECK */
                if (!abierto) { mensaje(0x04); break; }
                const unsigned len = nd >= 2 ? (unsigned)datos[0] | ((unsigned)datos[1] << 8) : 0u;
                uint16_t c = 0xFFFF;
                for (unsigned i = 0; i < len; ++i) c = crc_byte(c, ((const volatile uint8_t*)a)[i]);
                const uint8_t r[3] = { 0x3A, (uint8_t)c, (uint8_t)(c >> 8) };
                responde(r, 3);
                break;
            }
            case 0x17: case 0x27:             /* LOAD_PC */
                if (!abierto) { mensaje(0x04); break; }
                tx_fin();
                EUSCI_A0->IE = 0;
                NVIC->ICER[0] = 0xFFFFFFFFu;
                NVIC->ICER[1] = 0xFFFFFFFFu;
                __enable_irq();
                ((void (*)(void))(a | 1u))();
                for (;;) { }
            case 0x18: case 0x28: {           /* TX_DATA_BLOCK */
                if (!abierto) { mensaje(0x04); break; }
                const unsigned len = nd >= 2 ? (unsigned)datos[0] | ((unsigned)datos[1] << 8) : 0u;
                uint8_t r[257];
                for (unsigned hecho = 0; hecho < len;) {
                    const unsigned k = len - hecho > 256u ? 256u : len - hecho;
                    r[0] = 0x3A;
                    for (unsigned i = 0; i < k; ++i) r[1 + i] = ((const volatile uint8_t*)(a + hecho))[i];
                    responde(r, k + 1);
                    hecho += k;
                }
                break;
            }
            case 0x19: {                      /* TX_BSL_VERSION */
                if (!abierto) { mensaje(0x04); break; }
                uint8_t r[11];
                r[0] = 0x3A;
                for (unsigned i = 0; i < 10; ++i) r[1 + i] = bsl_version[i];
                responde(r, 11);
                break;
            }
            case 0x25:                        /* REBOOT_RESET */
                tx_fin();
                SYSCTL->REBOOT_CTL = SYSCTL_REBOOT_CTL_WKEY_VAL | SYSCTL_REBOOT_CTL_REBOOT;
                for (;;) { }
            default:
                mensaje(0x07);
                break;
        }
    }
}
