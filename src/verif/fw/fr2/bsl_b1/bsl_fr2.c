/* =============================================================================
 * bsl_fr2.c — El BSL B1 de las FR2xx/FR4xx (M10): uno propio (AGPLv3, de
 * mcu-sim), con el protocolo y las órdenes de TI [BSLFRAM SLAU550AB §3-§4,
 * §7]. No lleva código de TI.
 *
 * Una fuente, dos imágenes (Makefile):
 *   FR23xx-FR26xx (FR2311, FR2433, FR2355, FR2476; sin BSL_FR4): 3 KB, el BSL1
 *     en 0x1000-0x17FF y el BSL2 en 0xFFC00-0xFFFFF (aquí, la interfaz I2C y
 *     la configuración de cada chip). UART (eUSCI_A0, 9600 8E1, puede subir) e
 *     I2C (eUSCI_B0 de esclava en 0x48), con detección automática: la que
 *     hable primero. Sin comunicación en 10 s, a LPM4 [§3.4]. En FR2355 y
 *     FR2476 la configuración sale de la TLV (etiqueta 0xB5) y de la del
 *     usuario (0xFF88, firma 0x695A: la interfaz, no borrar la RAM, la
 *     dirección I2C), con unos 300 ms de arranque [§3.6].
 *   FR4xx (FR4133; BSL_FR4): 1 KB en 0x1000-0x13FF. Solo UART y solo a
 *     9600; solo RX Data Block, RX Password, Load PC y TX Data Block (las
 *     demás, 0x3B 0x07); el borrado, por contraseña mala [§4.1.5, nota 1].
 *
 *   La Z-Area (crt_fr2.S): 0x1000 entra, 0x1002 la «BSL Action».
 *   El reloj: MCLK = SMCLK = 8 MHz del FLL con REFO [§7: «Runs at 8 MHz»].
 *   La RAM: se borra la de la tabla de §7 (0x2000-0x23FF en FR2311/FR2433,
 *     0x2000-0x21FF en las demás), salvo que la configuración diga que no.
 *   La contraseña mala: el borrado masivo de la FRAM de código Y de la
 *     información [§4.1.5.2], y la respuesta 0x3B 0x05 (las FR2/FR4 sí
 *     contestan); con las firmas de 0xFF84/0xFF86 a 0xAAAA, sin borrar.
 *   La versión: 00.08.35.B3 (FR2311, FR2433), 00.09.36.B4 (FR2355),
 *     00.09.36.B5 (FR2476) [§7.1]; el FR4133 no tiene TX BSL Version.
 * =============================================================================
 */
#include <msp430.h>
#include <stdint.h>

#define R8(a)  (*(volatile uint8_t*)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t*)(uintptr_t)(a))

#define BUF 260u                        /* el búfer de las órdenes [§7] */

/* eUSCI_A (UART) y eUSCI_B (I2C), por su base */
#define A_CTLW0 0x00
#define A_BRW   0x06
#define A_MCTLW 0x08
#define A_STATW 0x0A
#define A_RXBUF 0x0C
#define A_TXBUF 0x0E
#define A_IFG   0x1C
#define B_CTLW0 0x00
#define B_RXBUF 0x0C
#define B_TXBUF 0x0E
#define B_OA0   0x14
#define B_IFG   0x2C
/* Los Timer_A/Timer_B, por su base */
#define T_CTL   0x00
#define T_CCTL0 0x02
#define T_CCR0  0x12
#define T_EX0   0x20

#ifdef BSL_FR4
#define BSL2
typedef uint16_t dir_t;
#else
#define BSL2 __attribute__((section(".bsl2"), noinline))
typedef uint32_t dir_t;
#endif

static uint8_t rx[BUF + 4];
static uint16_t n_rx;
static uint8_t bloqueado;

/* La configuración (de la tabla del chip, o de la TLV y de 0xFF88) */
struct Conf {
    uint16_t ua, puart;                 /* la eUSCI_A y el puerto de sus pines */
    uint8_t urx, utx;                   /* sus máscaras (PxSEL0 = 1) */
#ifndef BSL_FR4
    uint16_t timer, ub, pi2c;
    uint8_t isda, iscl, dir_i2c, interfaz;   /* interfaz: 0 las dos, 1 UART, 2 I2C */
    uint16_t usuario;                   /* la configuración del usuario (0: no hay) */
    uint16_t diminuta, n_diminuta;      /* la RAM diminuta que se borra (0: ninguna) */
    uint8_t ver[4];
#endif
    uint16_t ram_fin;                   /* la RAM que se borra: 0x2000 a esta */
};
static struct Conf c;

/* ---- La UART ------------------------------------------------------------- */
static void uart_baudios(uint8_t d) {
    const uint16_t a = c.ua;
    R16(a + A_CTLW0) = UCSWRST | UCSSEL__SMCLK | UCPEN | UCPAR;          /* 8E1 */
#ifndef BSL_FR4
    switch (d) {                                                          /* SMCLK = 8 MHz */
    case 3: R16(a + A_BRW) = 26; R16(a + A_MCTLW) = (0xB6 << 8) | (0 << 4) | UCOS16; break;   /* 19200 */
    case 4: R16(a + A_BRW) = 13; R16(a + A_MCTLW) = (0x84 << 8) | (0 << 4) | UCOS16; break;   /* 38400 */
    case 5: R16(a + A_BRW) = 8;  R16(a + A_MCTLW) = (0xF7 << 8) | (10 << 4) | UCOS16; break;  /* 57600 */
    case 6: R16(a + A_BRW) = 4;  R16(a + A_MCTLW) = (0x55 << 8) | (5 << 4) | UCOS16; break;   /* 115200 */
    default:
#else
    (void)d;
#endif
        R16(a + A_BRW) = 52; R16(a + A_MCTLW) = (0x49 << 8) | (1 << 4) | UCOS16;            /* 9600 */
#ifndef BSL_FR4
        break;
    }
#endif
    R16(a + A_CTLW0) &= ~UCSWRST;
}
static uint8_t uart_rx(void) { while (!(R16(c.ua + A_IFG) & UCRXIFG)) ; return (uint8_t)R16(c.ua + A_RXBUF); }
/* El fin de lo que se manda; UCBUSY también es «recibiendo»: si ya llega lo
 * siguiente, lo que se mandaba ha acabado (semidúplex) */
static void uart_fin(void) { while ((R16(c.ua + A_STATW) & UCBUSY) && !(R16(c.ua + A_IFG) & UCRXIFG)) ; }

/* ---- La salida: por la UART, o a un búfer que se sirve por I2C ------------ */
#ifndef BSL_FR4
/* Por I2C, la respuesta se guarda hasta que el maestro la lee: el ACK aparte
 * y el paquete en el mismo búfer de la orden (ya no se necesita: las órdenes
 * leen lo suyo antes de contestar). Así el BSL cabe en 0x2200-0x23FF */
static uint8_t es_i2c, hay_ack, ack;
static uint16_t n_sal;
#endif
static void tx(uint8_t b) {
#ifndef BSL_FR4
    if (es_i2c) {
        if (!hay_ack) { ack = b; hay_ack = 1; }
        else if (n_sal < sizeof rx) rx[n_sal++] = b;
        return;
    }
#endif
    while (!(R16(c.ua + A_IFG) & UCTXIFG)) ;
    R16(c.ua + A_TXBUF) = b;
}

/* ---- El CRC-CCITT [UG FR2 cap. 13] -------------------------------------- */
static uint16_t crc_byte(uint16_t k, uint8_t b) {
    uint16_t x = (uint16_t)((k >> 8) ^ b);
    x ^= x >> 4;
    return (uint16_t)((k << 8) ^ (x << 12) ^ (x << 5) ^ x);
}
static uint16_t crc_tx;
static void abre(uint16_t n) { tx(0x80); tx((uint8_t)n); tx((uint8_t)(n >> 8)); crc_tx = 0xFFFF; }
static void pon(uint8_t b) { tx(b); crc_tx = crc_byte(crc_tx, b); }
static void cierra(void) { tx((uint8_t)crc_tx); tx((uint8_t)(crc_tx >> 8)); }
static void mensaje(uint8_t m) { abre(2); pon(0x3B); pon(m); cierra(); }

static uint8_t lee8(dir_t a) { return *(volatile uint8_t*)(uintptr_t)a; }
static void escribe8(dir_t a, uint8_t v) { *(volatile uint8_t*)(uintptr_t)a = v; }

/* ---- El borrado masivo: la FRAM de código y la información ----------------- */
static void borra_rango(dir_t i, dir_t ultimo) { for (;;) { escribe8(i, 0xFF); if (i == ultimo) break; ++i; } }
static void borra(void) {
    const uint16_t id = R16(0x1A04);
#ifdef BSL_FR4
    (void)id;
    borra_rango(0xC400, 0xFFFF);
    borra_rango(0x1800, 0x19FF);
#else
    if (id == 0x82F0) { borra_rango(0xF100, 0xFFFF); return; }                   /* FR2311: sin información */
    borra_rango(id == 0x8240 ? 0xC400 : 0x8000, 0xFFFF);
    if (id == 0x832A) borra_rango(0x10000ul, 0x17FFFul);                         /* FR2476: la de arriba */
    borra_rango(0x1800, 0x19FF);
#endif
}
static uint8_t sin_borrado(void) { return R16(0xFF84) == 0xAAAA && R16(0xFF86) == 0xAAAA; }

/* ---- Las órdenes --------------------------------------------------------- */
static dir_t pc_pendiente;              /* Load PC: se salta cuando ha salido el ACK */
#ifndef BSL_FR4
static uint8_t baudios_pendientes;
#endif

__attribute__((noinline)) static void orden(void) {
    const uint8_t k = rx[0];
    const dir_t a = rx[1] | ((dir_t)rx[2] << 8)
#ifndef BSL_FR4
                    | ((dir_t)rx[3] << 16)
#endif
        ;
    const uint16_t n = (uint16_t)(rx[4] | (rx[5] << 8));
    if (k == 0x11) {                                                     /* RX Password */
        uint8_t mal = n_rx != 33;
        for (uint8_t i = 0; i < 32; ++i) mal |= lee8(0xFFE0 + i) ^ rx[1 + i];
        if (!mal) { bloqueado = 0; mensaje(0x00); return; }
        if (!sin_borrado()) borra();
        bloqueado = 1;
        mensaje(0x05);
        return;
    }
#ifndef BSL_FR4
    if (k == 0x15) { borra(); bloqueado = 1; mensaje(0x00); return; }     /* Mass Erase */
    if (k != 0x10 && k != 0x1B && k != 0x16 && k != 0x17 && k != 0x18 && k != 0x19) { mensaje(0x07); return; }
#else
    if (k != 0x10 && k != 0x17 && k != 0x18) { mensaje(0x07); return; }
#endif
    if (bloqueado) {
#ifndef BSL_FR4
        if (k != 0x1B)
#endif
            mensaje(0x04);
        return;
    }
    switch (k) {
    case 0x10:                                                           /* RX Data Block */
#ifndef BSL_FR4
    case 0x1B:
#endif
        for (uint16_t i = 4; i < n_rx; ++i) escribe8(a + i - 4, rx[i]);
        if (k == 0x10) mensaje(0x00);
        return;
#ifndef BSL_FR4
    case 0x16: {                                                         /* CRC Check */
        uint16_t s = 0xFFFF;
        for (uint16_t i = 0; i < n; ++i) s = crc_byte(s, lee8(a + i));
        abre(3); pon(0x3A); pon((uint8_t)s); pon((uint8_t)(s >> 8)); cierra();
        return;
    }
    case 0x19:                                                           /* TX BSL Version */
        abre(5); pon(0x3A);
        for (uint8_t i = 0; i < 4; ++i) pon(c.ver[i]);
        cierra();
        return;
#endif
    case 0x17:                                                           /* Load PC */
        pc_pendiente = a;
        return;
    case 0x18: {                                                         /* TX Data Block */
        dir_t p = a;
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
static uint8_t (*rxb)(void);
__attribute__((noinline)) static uint8_t paquete(uint8_t primero) {
    if (primero != 0x80) return 0x51;
    const uint16_t nn = (uint16_t)(rxb() | (rxb() << 8));
#ifdef BSL_FR4
    if (nn == 0 || nn > BUF) return 0x57;
#else
    if (nn == 0) return 0x53;
    if (nn > BUF) { for (uint16_t i = 0; i < nn + 2u; ++i) (void)rxb(); return 0x54; }
#endif
    uint16_t k = 0xFFFF;
    for (uint16_t i = 0; i < nn; ++i) { const uint8_t b = rxb(); rx[i] = b; k = crc_byte(k, b); }
    const uint16_t ck = (uint16_t)(rxb() | (rxb() << 8));
    n_rx = nn;
    return ck == k ? 0x00 : 0x52;
}

/* Un paquete entero: la respuesta de la interfaz y, si toca, la del núcleo */
__attribute__((noinline)) static void atiende(uint8_t primero) {
    const uint8_t r = paquete(primero);
    if (r) { tx(r); return; }
#ifndef BSL_FR4
    if (rx[0] == 0x52) {                                                 /* Change Baud Rate */
        if (es_i2c) { tx(0x00); mensaje(0x07); return; }                 /* [§4.1.5: solo UART] */
        if (n_rx != 2 || rx[1] < 2 || rx[1] > 6) { tx(0x56); return; }
        tx(0x00);
        baudios_pendientes = rx[1];
        return;
    }
#endif
    tx(0x00);
    orden();
}

/* =============================================================================
 * El BSL2 (0xFFC00): la I2C, la configuración del chip y la espera de 10 s
 * ========================================================================== */
#ifndef BSL_FR4
BSL2 static uint8_t i2c_rx(void) {
    while (!(R16(c.ub + B_IFG) & UCRXIFG0)) ;
    return (uint8_t)R16(c.ub + B_RXBUF);
}
/* Sirve lo que hay en `sal` a las lecturas del maestro, hasta la parada de la
 * que lo acaba [§4.1.2: S/A/R, el ACK, la respuesta, STOP] */
BSL2 static void i2c_sirve(void) {
    uint16_t i = 0;
    const uint16_t n = (uint16_t)(n_sal + 1);                            /* el ACK y el paquete */
    for (;;) {
        const uint16_t f = R16(c.ub + B_IFG);
        if (f & UCTXIFG0) { R16(c.ub + B_TXBUF) = i == 0 ? ack : i < n ? rx[i - 1] : 0xFF; ++i; }
        if (f & UCSTPIFG) {
            R16(c.ub + B_IFG) = (uint16_t)(f & ~(UCSTPIFG | UCSTTIFG));
            if (i >= n) break;
        }
    }
    /* Lo que quedó en TXBUF tras el NACK del maestro no es de la siguiente */
    R16(c.ub + B_CTLW0) |= UCSWRST;
    R16(c.ub + B_CTLW0) &= ~UCSWRST;
    n_sal = 0; hay_ack = 0;
}
BSL2 static void i2c_abre(uint8_t dir7) {
    R16(c.ub + B_CTLW0) = UCSWRST | UCMODE_3 | UCSYNC;                   /* esclava I2C */
    R16(c.ub + B_OA0) = (uint16_t)(UCOAEN | dir7);
    R16(c.ub + B_CTLW0) &= ~UCSWRST;
}

/* La TLV: la etiqueta 0xB5 [§3.6, tabla 3-1], o 0 */
BSL2 static uint16_t tlv_bsl(void) {
    uint16_t p = 0x1A08;
    while (p < 0x1AF8) {
        const uint8_t t = R8(p), l = R8(p + 1);
        if (t == 0xB5) return p + 2;
        if (t == 0xFF) break;
        p = (uint16_t)(p + 2 + l);
    }
    return 0;
}

/* Espera `ticks` de ACLK/64 (512 Hz) con el temporizador del chip; devuelve 1
 * si en ese tiempo ha empezado a hablar una de las interfaces */
BSL2 static uint8_t habla(void) {
    if (c.interfaz != 2 && (R16(c.ua + A_IFG) & UCRXIFG)) { es_i2c = 0; return 1; }
    if (c.interfaz != 1 && (R16(c.ub + B_IFG) & (UCSTTIFG | UCRXIFG0))) { es_i2c = 1; return 1; }
    return 0;
}
BSL2 static uint8_t espera(uint16_t ticks, uint8_t mira) {
    const uint16_t t = c.timer;
    R16(t + T_CTL) = TASSEL_1 | ID_3 | TACLR;                            /* ACLK / 8 / 8 */
    R16(t + T_EX0) = 7;
    R16(t + T_CCR0) = (uint16_t)(ticks - 1);
    R16(t + T_CCTL0) = 0;
    R16(t + T_CTL) |= MC_1;
    for (;;) {
        if (mira && habla()) break;
        if (R16(t + T_CCTL0) & CCIFG) break;
    }
    R16(t + T_CTL) = 0;
    return mira ? habla() : 0;
}

/* La configuración de cada chip [DS, «Bootloader (BSL)»; BSLFRAM §7] */
BSL2 static void configura(void) {
    const uint16_t id = R16(0x1A04);
    c.ua = 0x0500; c.ub = 0x0540; c.puart = c.pi2c = 0x0200;
    c.isda = 0x04; c.iscl = 0x08; c.dir_i2c = 0x48; c.interfaz = 0; c.usuario = 0;
    c.ram_fin = 0x2200;
    c.ver[0] = 0x00; c.ver[1] = 0x08; c.ver[2] = 0x35; c.ver[3] = 0xB3;
    c.urx = 0x20; c.utx = 0x10; c.timer = 0x0380;                        /* FR2433: P1.5/P1.4, Timer0_A3 */
    if (id == 0x82F0) { c.urx = 0x40; c.utx = 0x80; }                    /* FR2311: P1.6/P1.7, Timer0_B3 */
    if (id == 0x82F0 || id == 0x8240) { c.ram_fin = 0x2400; return; }
    /* FR2355 y FR2476: la TLV y la configuración del usuario [§3.6] */
    c.ver[1] = 0x09; c.ver[2] = 0x36; c.ver[3] = id == 0x830C ? 0xB4 : 0xB5;
    const uint16_t t = tlv_bsl();
    if (!t) return;
    c.timer = R16(t + 0x00); c.ua = R16(t + 0x02); c.ub = R16(t + 0x04);
    c.puart = R16(t + 0x06); c.urx = R8(t + 0x0A); c.utx = R8(t + 0x0B);
    c.pi2c = R16(t + 0x0C); c.isda = R8(t + 0x10); c.iscl = R8(t + 0x11);
    c.dir_i2c = R8(t + 0x16);
    c.n_diminuta = R8(t + 0x17); c.diminuta = R16(t + 0x18);
    c.usuario = R16(t + 0x1A);
    if (R16(c.usuario) == 0x695A && R8(c.usuario + 3) == 0x5A) {
        const uint8_t u = R8(c.usuario + 2);
        if ((u & 3) != 3) c.interfaz = u & 3;
        if (u & 0x04) c.ram_fin = 0;                                     /* no se borra */
        if (u & 0x08) c.n_diminuta = 0;
        c.dir_i2c = R8(c.usuario + 0x18) & 0x7F;
    }
}
#endif

/* ---- El principal --------------------------------------------------------- */
__attribute__((noreturn)) void bsl_principal(void) {
    WDTCTL = WDTPW | WDTHOLD;
    PM5CTL0 &= ~LOCKLPM5;
#ifdef BSL_FR4
    SYSCFG0 = 0;                                                         /* sin protección (sin clave en las FR413x) */
#else
    SYSCFG0 = FRWPPW;                                                    /* sin protección de la FRAM */
#endif
    /* 8 MHz con el FLL y REFO [UG FR2 §3.2.11]; un estado de espera de la
     * FRAM mientras engancha (el DCO puede pasar de 8 MHz) */
    FRCTL0 = FRCTLPW | NWAITS_1;
    __bis_SR_register(SCG0);
    CSCTL3 = SELREF__REFOCLK;
    CSCTL1 = DCORSEL_3;
    CSCTL2 = FLLD_0 + 243;
    __delay_cycles(3);
    __bic_SR_register(SCG0);
    while (CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)) ;
    CSCTL4 = SELMS__DCOCLKDIV | SELA__REFOCLK;
#ifdef BSL_FR4
    c.ua = 0x0500; c.puart = 0x0200; c.urx = 0x02; c.utx = 0x01;         /* P1.1/P1.0 */
    c.ram_fin = 0x2200;
#else
    configura();
    if (c.usuario && R16(0x1A04) != 0x82F0 && R16(0x1A04) != 0x8240) espera(154, 0);   /* ~300 ms [§3.6] */
#endif
    /* La RAM de la tabla de §7 que no es la del BSL (sus variables y su pila,
     * en 0x2200-0x23FF, que el arranque ya ha borrado) */
    for (uint16_t p = 0x2000; p < c.ram_fin && p < 0x2200; p += 2) R16(p) = 0;
#ifndef BSL_FR4
    for (uint16_t i = 0; i < c.n_diminuta; ++i) R8(c.diminuta + i) = 0;
#endif
    R8(c.puart + 0x0A) |= (uint8_t)(c.urx | c.utx);                     /* PxSEL0 */
    R8(c.puart + 0x0C) &= (uint8_t)~(c.urx | c.utx);
    uart_baudios(2);
    bloqueado = 1;
#ifndef BSL_FR4
    if (c.interfaz != 1) {
        R8(c.pi2c + 0x0A) |= (uint8_t)(c.isda | c.iscl);
        R8(c.pi2c + 0x0C) &= (uint8_t)~(c.isda | c.iscl);
        i2c_abre(c.dir_i2c);
    }
    /* La detección: la que hable primero; nadie en 10 s, LPM4 [§3.4] */
    if (!espera(5120, 1)) { __bis_SR_register(LPM4_bits); for (;;) ; }
    rxb = es_i2c ? i2c_rx : uart_rx;
    if (es_i2c) {
        R16(c.ua + A_CTLW0) = UCSWRST;                                   /* la otra, fuera */
        R16(c.ub + B_IFG) = 0;
    }
#else
    rxb = uart_rx;
#endif
    for (;;) {
        atiende(rxb());
#ifndef BSL_FR4
        if (es_i2c) i2c_sirve();
        else
#endif
            uart_fin();
#ifndef BSL_FR4
        if (baudios_pendientes) { uart_baudios(baudios_pendientes); baudios_pendientes = 0; }
#endif
        if (pc_pendiente) ((void (*)(void))(uintptr_t)pc_pendiente)();
    }
}
