/* ===========================================================================
 * cargador.c — El cargador de arranque de la ROM R1 del MSP432E4 (T6)
 *
 * Copyright (C) 2026 Pedro Rodriguez y colaboradores — AGPLv3, como el resto
 * del simulador. Escrito desde cero a partir de la DESCRIPCION del protocolo
 * (SPMU301, «TivaWare Boot Loader User's Guide», capitulo del protocolo
 * serie): NO lleva codigo del boot_loader de TivaWare, cuya licencia no se
 * puede combinar con la AGPLv3 [verif/fw/msp432e4/sdk/LICENSE-TI.md].
 *
 * CUANDO CORRE. Con la Flash en blanco (la palabra de 0x4 a 0xFFFFFFFF) o con
 * BOOTCFG pidiendolo por un pin, el chip arranca con la ROM en 0x0 (RMCTL.BA)
 * y su vector de reset es cargador_reset. Una aplicacion tambien puede entrar
 * con ROM_UpdateUART() (ROM_UARTTABLE[21]), con la UART0 ya configurada: se
 * salta la deteccion de baudios.
 *
 * LO QUE HACE. Solo la UART0, en PA0 (U0RX) y PA1 (U0TX), 8N1, con el reloj de
 * arranque (PIOSC, 16 MHz). Los baudios los saca de dos 0x55 seguidos que
 * manda el PC: mide con SysTick, sondeando PA0, del primer flanco de bajada al
 * decimo, que son 18 bits. Luego el protocolo de las herramientas de TI
 * (sflash, LM Flash Programmer, Uniflash):
 *
 *   paquete   [tamano][suma][datos...]: tamano = datos + 2, suma = los datos
 *             sumados en 8 bits; los ceros delante del tamano se saltan
 *   ACK 0x00 0xCC, NAK 0x00 0x33 (paquete mal recibido: el PC lo repite)
 *
 *   0x20 PING          nada
 *   0x21 DOWNLOAD      direccion y tamano (32 bits, big-endian): borra los
 *                      sectores de 16 KB que toca la descarga
 *   0x22 RUN           direccion: si alli hay una tabla de vectores
 *                      (alineada a 1 KB, con un reset Thumb) pone VTOR, la
 *                      pila y salta a su reset; si no, salta a la direccion
 *   0x23 GET_STATUS    contesta con un paquete de un byte: el estado
 *   0x24 SEND_DATA     datos para la descarga en curso (multiplo de 4, salvo
 *                      el ultimo, que se rellena con 0xFF)
 *   0x25 RESET         reset del sistema (SYSRESETREQ)
 *
 *   estados: 0x40 SUCCESS, 0x41 UNKNOWN_CMD, 0x42 INVALID_CMD,
 *            0x43 INVALID_ADR, 0x44 FLASH_FAIL
 *
 * LO QUE NO HACE el de la ROM de TI y este no [P-17 b]: los cargadores por
 * SSI0, I2C0, USB (DFU) y Ethernet (BOOTP/TFTP), y comprobar el CRC de la
 * imagen. Antes de saltar borra RMCTL.BA, para que la Flash vuelva a 0x0.
 *
 * Sin variables globales: la ROM no tiene RAM (rom_r1.ld lo comprueba). Todo
 * vive en la pila, que el vector 0 pone en 0x2000_4000.
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include <ti/devices/msp432e4/inc/msp432e401y.h>
#include <ti/devices/msp432e4/driverlib/types.h>
#include <ti/devices/msp432e4/driverlib/flash.h>

#define RCGCGPIO  0x400FE608u
#define RCGCUART  0x400FE618u
#define PRGPIO    0x400FEA08u
#define PRUART    0x400FEA18u
#define RMCTL     0x400FE0F0u
#define PA_BASE     0x40058000u          /* AHB: el E4 no tiene la ventana APB */
#define G_DATA_PA0 (PA_BASE + 0x004u)    /* DATA con la mascara de PA0 */
#define G_DIR     (PA_BASE + 0x400u)
#define G_AFSEL   (PA_BASE + 0x420u)
#define G_PUR     (PA_BASE + 0x510u)
#define G_DEN     (PA_BASE + 0x51Cu)
#define G_PCTL    (PA_BASE + 0x52Cu)
#define U_DR      (UART0_BASE + 0x000u)
#define U_FR      (UART0_BASE + 0x018u)
#define U_IBRD    (UART0_BASE + 0x024u)
#define U_FBRD    (UART0_BASE + 0x028u)
#define U_LCRH    (UART0_BASE + 0x02Cu)
#define U_CTL     (UART0_BASE + 0x030u)
#define U_CC      (UART0_BASE + 0xFC8u)
#define FR_BUSY   0x08u
#define FR_RXFE   0x10u
#define FR_TXFF   0x20u
#define FR_TXFE   0x80u
#define ST_CTRL   0xE000E010u
#define ST_RELOAD 0xE000E014u
#define ST_CURRENT 0xE000E018u
#define VTOR      0xE000ED08u
#define APINT     0xE000ED0Cu

#define FLASH_TAM   0x100000u          /* 1 MB */
#define SECTOR      0x4000u            /* 16 KB */

enum { CMD_PING = 0x20, CMD_DOWNLOAD = 0x21, CMD_RUN = 0x22, CMD_GET_STATUS = 0x23,
       CMD_SEND_DATA = 0x24, CMD_RESET = 0x25 };
enum { ST_SUCCESS = 0x40, ST_UNKNOWN_CMD = 0x41, ST_INVALID_CMD = 0x42,
       ST_INVALID_ADR = 0x43, ST_FLASH_FAIL = 0x44 };
enum { ACK = 0xCC, NAK = 0x33 };

void cargador_reset(void) __attribute__((noreturn, used));
void UpdateUART(void) __attribute__((noreturn, used));

static uint8_t rx(void) {
    while (HWREG(U_FR) & FR_RXFE) {}
    return (uint8_t)HWREG(U_DR);
}
static void tx(uint8_t b) {
    while (HWREG(U_FR) & FR_TXFF) {}
    HWREG(U_DR) = b;
}
static void tx_fin(void) {
    while (!(HWREG(U_FR) & FR_TXFE) || (HWREG(U_FR) & FR_BUSY)) {}
}
static void ack(void) { tx(0); tx(ACK); }
static void nak(void) { tx(0); tx(NAK); }

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Un paquete del PC. Devuelve el numero de datos, o -1 si la suma no cuadra
 * (y ya ha mandado el NAK). */
static int recibe(uint8_t* d) {
    uint8_t n;
    do { n = rx(); } while (n == 0);
    const uint8_t suma = rx();
    if (n < 3) { nak(); return -1; }
    uint8_t s = 0;
    for (unsigned i = 0; i < (unsigned)(n - 2); ++i) { d[i] = rx(); s = (uint8_t)(s + d[i]); }
    if (s != suma) { nak(); return -1; }
    return n - 2;
}

/* Un paquete al PC; lo repite mientras le conteste NAK. */
static void envia(const uint8_t* d, unsigned n) {
    for (;;) {
        uint8_t s = 0;
        for (unsigned i = 0; i < n; ++i) s = (uint8_t)(s + d[i]);
        tx((uint8_t)(n + 2)); tx(s);
        for (unsigned i = 0; i < n; ++i) tx(d[i]);
        uint8_t r;
        do { r = rx(); } while (r == 0);
        if (r == ACK) return;
    }
}

static void salta(uint32_t dir) __attribute__((noreturn));
static void salta(uint32_t dir) {
    tx_fin();
    HWREG(RMCTL) = 1u;                         /* BA: la Flash vuelve a 0x0 */
    const uint32_t* v = (const uint32_t*)dir;
    if ((dir & 0x3FFu) == 0 && dir < FLASH_TAM && (v[1] & 1u) && v[1] != 0xFFFFFFFFu) {
        HWREG(VTOR) = dir;
        __asm volatile("msr msp, %0\n\tbx %1" :: "r"(v[0]), "r"(v[1]));
    } else {
        __asm volatile("bx %0" :: "r"(dir | 1u));
    }
    __builtin_unreachable();
}

static void actualiza(void) __attribute__((noreturn));
static void actualiza(void) {
    uint8_t d[256];
    uint32_t dir = 0, queda = 0;
    uint8_t estado = ST_SUCCESS;
    for (;;) {
        const int n = recibe(d);
        if (n < 0) continue;
        switch (d[0]) {
            case CMD_PING:
                estado = ST_SUCCESS;
                ack();
                break;
            case CMD_DOWNLOAD: {
                estado = ST_SUCCESS;
                queda = 0;
                if (n != 9) { estado = ST_INVALID_CMD; ack(); break; }
                const uint32_t a = be32(d + 1), t = be32(d + 5);
                if ((a & 3u) || a >= FLASH_TAM || t > FLASH_TAM - a) {
                    estado = ST_INVALID_ADR; ack(); break;
                }
                for (uint32_t s = a & ~(SECTOR - 1u); s < a + t; s += SECTOR)
                    if (FlashErase(s) != 0) { estado = ST_FLASH_FAIL; break; }
                if (estado == ST_SUCCESS) { dir = a; queda = t; }
                ack();
                break;
            }
            case CMD_SEND_DATA: {
                ack();
                unsigned m = (unsigned)(n - 1);
                if (queda == 0 || m > queda || ((m & 3u) && m != queda)) {
                    estado = ST_INVALID_CMD; queda = 0; break;
                }
                uint8_t* p = d + 1;
                unsigned mm = (m + 3u) & ~3u;
                for (unsigned i = m; i < mm; ++i) p[i] = 0xFF;
                /* FlashProgram quiere palabras alineadas: d+1 no lo esta */
                uint32_t w[64];
                for (unsigned i = 0; i < mm / 4u; ++i)
                    w[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) |
                           ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);
                if (FlashProgram(w, dir, mm) != 0) { estado = ST_FLASH_FAIL; queda = 0; break; }
                estado = ST_SUCCESS;
                dir += mm;
                queda -= m;
                break;
            }
            case CMD_GET_STATUS: {
                ack();
                const uint8_t r = estado;
                envia(&r, 1);
                break;
            }
            case CMD_RUN:
                if (n != 5) { estado = ST_INVALID_CMD; ack(); break; }
                ack();
                salta(be32(d + 1));
            case CMD_RESET:
                ack();
                tx_fin();
                HWREG(APINT) = 0x05FA0004u;    /* SYSRESETREQ */
                for (;;) {}
            default:
                ack();
                estado = ST_UNKNOWN_CMD;
                break;
        }
    }
}

/* Espera un flanco de bajada en PA0 y devuelve SysTick en ese momento */
static uint32_t flanco(void) {
    while (!HWREG(G_DATA_PA0)) {}
    while (HWREG(G_DATA_PA0)) {}
    return HWREG(ST_CURRENT);
}

void cargador_reset(void) {
    __asm volatile("cpsid i");
    HWREG(RCGCGPIO) |= 1u;
    HWREG(RCGCUART) |= 1u;
    while (!(HWREG(PRGPIO) & 1u) || !(HWREG(PRUART) & 1u)) {}
    /* PA0 como entrada digital con pull-up, para medir; PA1 aun sin tocar */
    HWREG(G_DIR) &= ~3u;
    HWREG(G_PUR) |= 1u;
    HWREG(G_DEN) |= 3u;
    HWREG(ST_RELOAD) = 0x00FFFFFFu;
    HWREG(ST_CURRENT) = 0;
    HWREG(ST_CTRL) = 5u;                       /* reloj del nucleo, sin IRQ */
    uint32_t t18;
    for (;;) {
        uint32_t t[10];
        for (unsigned i = 0; i < 10; ++i) t[i] = flanco();
        const uint32_t a = (t[0] - t[4]) & 0xFFFFFFu, b = (t[5] - t[9]) & 0xFFFFFFu;
        t18 = (t[0] - t[9]) & 0xFFFFFFu;
        /* Las dos mitades (8 bits cada una) tienen que parecerse: si no, no
         * eran dos 0x55 */
        const uint32_t dif = a > b ? a - b : b - a;
        if (a >= 64u && dif <= a / 8u) break;
    }
    HWREG(ST_CTRL) = 0;
    while (!HWREG(G_DATA_PA0)) {}              /* el bit de parada */
    /* BRD = f / (16 baudios) = ciclos por bit / 16, en 1/64: ciclos*4/18 */
    const uint32_t brd = (t18 * 4u + 9u) / 18u;
    HWREG(U_CTL) = 0;
    HWREG(U_IBRD) = brd >> 6;
    HWREG(U_FBRD) = brd & 63u;
    HWREG(U_LCRH) = 0x70u;                     /* 8 bits, FIFO */
    HWREG(U_CC) = 0;
    HWREG(G_PCTL) = (HWREG(G_PCTL) & ~0xFFu) | 0x11u;
    HWREG(G_AFSEL) |= 3u;
    HWREG(U_CTL) = 0x301u;                     /* UARTEN, TXE, RXE */
    ack();
    actualiza();
}

void UpdateUART(void) {
    __asm volatile("cpsid i");
    actualiza();
}
