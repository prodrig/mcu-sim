/* ===========================================================================
 * p4_t3b — El segundo firmware de verificacion de la fase T3 del MSP432P401R
 *
 * Como p4_t3, y aparte de el para que sus modos no muevan el codigo de los de
 * p4_t3 (y con el, el tiempo simulado de sus grupos): la cabecera de TI y nada
 * mas, y MODOS que el banco escribe en el buzon antes de soltar el reset.
 *
 *   30-34  ADC14 y REF_A
 *    30  una conversion de A0 (P5.5) con AVCC en 14, 12, 10 y 8 bits; en
 *        complemento a 2; y A0-A1 en diferencial
 *    31  una secuencia A0..A3 con MSC, por interrupcion y ADC14IV, a 93,75
 *        kHz (SMCLK / 32): el Timer32 mide lo que tarda
 *    32  REF_A a 2,5 V: el sensor de temperatura (TCMAP) con la calibracion
 *        del TLV, AVCC/2 (BATMAP), A0 contra 1,2 V y VREF+ por P5.6
 *    33  disparo por TA0.1 a 1 kHz, uno repetido: cuantas conversiones y que
 *    34  el comparador de ventana (LO, IN, HI), el desbordamiento y el IV
 *
 *   40-43  uDMA
 *    40  memoria a memoria en modo automatico, por software, en tandas de 4;
 *        DMA_INT0 y su flag; y un error de bus (DMA_ERR)
 *    41  la UCA0 en UCLISTEN a 1 Mbaudio: TX por el canal 0 y RX por el 1,
 *        con DMA_INT1 al acabar la recepcion
 *    42  el ADC14 en canal unico repetido con MSC: 16 resultados al canal 7
 *    43  ping-pong en el canal 7 con el ADC disparado por TA0.1 a 1 kHz: el
 *        firmware rearma cada mitad en DMA_INT1
 *
 *   50-53  CRC32, AES256, RTC_C y COMP_E
 *    50  CRC-CCITT y CRC32 de "123456789", por DI y por DIRB; y una palabra de
 *        32 bits igual que cuatro bytes
 *    51  AES-128 y AES-256 con los vectores de la FIPS-197, por la secuencia
 *        de DriverLib: cifrar, generar la clave de descifrado y descifrar
 *    52  RTC_C en BCD con BCLK = REFO: de las 23:59:58 a medianoche, con
 *        RTCRDY, el evento de medianoche, la alarma de las 00:00 y RT0PS a
 *        128 Hz; durmiendo en WFI entre interrupciones
 *    53  COMP_E0: C0.2 (P7.7) contra la escalera de VCC con histeresis
 *        (16/32 y 20/32), por interrupcion; y C0OUT por P7.1
 *
 *   60-62  Los modos de bajo consumo
 *    60  activo; LPM0 con TA1 en ACLK despertando cada 10 ms; LPM3 con el
 *        RTC despertando a 64 Hz (TA1 en ACLK y el Timer32 en MCLK, parados);
 *        y LPM4 hasta que se pulsa S1
 *    61  LPM3.5 despertando por el preescalador del RTC: sale por un POR con
 *        RSTCTL_PCMRESET_STAT, la SRAM y el RTC conservados y P1.0 congelado
 *        hasta borrar LOCKLPM5
 *    62  LPM4.5 despertando por S1 (P1.1): la SRAM se pierde y el IFG de P1.1
 *        dice quien desperto
 *
 *   70-71  Las piezas de la BOOSTXL-EDUMKII
 *    70  OPT3001 (0x44) y TMP006 (0x40) por la UCB1 (P6.4/P6.5): sus
 *        identificadores, una conversion de cada uno y la patilla DRDY
 *    71  el zumbador en TA0.4 (P2.7) a 2 kHz y el eje X del joystick (A15)
 *
 * El buzon: modo, arranques, estado, senal (la escribe el banco) y r[].
 * ===========================================================================*/
#ifndef NO_MSP_CLASSIC_DEFINES
#define NO_MSP_CLASSIC_DEFINES
#endif
#include "msp432p401r.h"

uint32_t SystemCoreClock = 3000000;
void SystemInit(void) {}

typedef struct {
    volatile uint32_t modo;              /* 0x00 lo pone el banco             */
    volatile uint32_t arranques;         /* 0x04                              */
    volatile uint32_t estado;            /* 0x08 hasta donde se ha llegado    */
    volatile uint32_t senal;             /* 0x0C lo escribe el banco          */
    volatile uint32_t r[60];             /* 0x10 resultados                   */
    volatile uint8_t  buf[256];          /* 0x100 datos                       */
} buzon_t;

buzon_t buzon __attribute__((section(".mailbox")));

static void para_perro(void) { WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD; }
static void espera_senal(uint32_t v) { while (buzon.senal != v) { } }

/* El Timer32_1 libre, a MCLK: para medir */
static void t32_libre(void) {
    TIMER32_1->LOAD = 0xFFFFFFFFu;
    TIMER32_1->CONTROL = TIMER32_CONTROL_SIZE | TIMER32_CONTROL_ENABLE;
}
static uint32_t t32(void) { return TIMER32_1->VALUE; }

/* --- ADC14 ------------------------------------------------------------------ */

static void analogico(DIO_PORT_Odd_Interruptable_Type* p, uint8_t m) {
    p->SEL0 |= m; p->SEL1 |= m;
}
/* Una conversion de MEM0, a mano */
static uint32_t convierte(void) {
    ADC14->CLRIFGR0 = ADC14_CLRIFGR0_CLRIFG0;
    ADC14->CTL0 |= ADC14_CTL0_ENC | ADC14_CTL0_SC;
    while (!(ADC14->IFGR0 & ADC14_IFGR0_IFG0)) { }
    return ADC14->MEM[0];
}
static void adc_base(uint32_t ctl1) {
    ADC14->CTL0 = 0;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_2 |
                  ADC14_CTL0_SSEL__MODCLK;
    ADC14->CTL1 = ctl1;
}

static void m30_simple(void) {
    analogico(P5, BIT5 | BIT4);                  /* A0, A1 */
    static const uint32_t res[4] = { ADC14_CTL1_RES__14BIT, ADC14_CTL1_RES__12BIT,
                                     ADC14_CTL1_RES__10BIT, ADC14_CTL1_RES__8BIT };
    for (int i = 0; i < 4; ++i) {
        adc_base(res[i]);
        ADC14->MCTL[0] = ADC14_MCTLN_INCH_0 | ADC14_MCTLN_VRSEL_0;
        buzon.r[i] = convierte();
    }
    adc_base(ADC14_CTL1_RES__14BIT | ADC14_CTL1_DF);
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0;
    buzon.r[4] = (uint32_t)(int32_t)(int16_t)convierte();
    adc_base(ADC14_CTL1_RES__14BIT);
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0 | ADC14_MCTLN_DIF;
    buzon.r[5] = convierte();
    /* Con ENC = 1 el MCTL no se deja tocar */
    ADC14->CTL0 |= ADC14_CTL0_ENC;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_5;
    buzon.r[6] = ADC14->MCTL[0] & 0x1F;
    buzon.estado = 1;
    for (;;) { }
}

static volatile uint32_t m31_fin = 0, m31_iv = 0;
static volatile uint32_t m31_res[4];
void ADC14_IRQHandler(void) {
    const uint32_t iv = ADC14->IV;
    if (buzon.modo == 31) {
        m31_iv = iv;
        for (int i = 0; i < 4; ++i) m31_res[i] = ADC14->MEM[i];
        m31_fin = t32();
    } else if (buzon.modo == 33) {
        const uint32_t n = buzon.r[0];
        if (n < 16) buzon.r[1 + n] = ADC14->MEM[0];
        else (void)ADC14->MEM[0];
        buzon.r[0] = n + 1;
    }
}
static void m31_secuencia(void) {
    t32_libre();
    analogico(P5, BIT5 | BIT4 | BIT3 | BIT2);    /* A0..A3 */
    ADC14->CTL0 = 0;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_2 | ADC14_CTL0_MSC |
                  ADC14_CTL0_CONSEQ_1 | ADC14_CTL0_SSEL__SMCLK | ADC14_CTL0_PDIV__32;
    ADC14->CTL1 = ADC14_CTL1_RES__14BIT;         /* CSTARTADD = 0 */
    for (int i = 0; i < 4; ++i) ADC14->MCTL[i] = (uint32_t)i;
    ADC14->MCTL[3] |= ADC14_MCTLN_EOS;
    ADC14->IER0 = ADC14_IER0_IE3;
    NVIC->ISER[0] = 1u << ADC14_IRQn;
    const uint32_t t0 = t32();
    ADC14->CTL0 |= ADC14_CTL0_ENC | ADC14_CTL0_SC;
    while (!m31_fin) { }
    buzon.r[0] = t0 - m31_fin;                   /* el Timer32 cuenta hacia abajo */
    for (int i = 0; i < 4; ++i) buzon.r[1 + i] = m31_res[i];
    buzon.r[5] = m31_iv;
    buzon.r[6] = ADC14->IFGR0;                    /* leer MEMn los ha bajado */
    buzon.estado = 1;
    for (;;) { }
}

static void m32_ref(void) {
    analogico(P5, BIT5 | BIT6);                  /* A0 y VREF+ */
    /* La referencia, a 2,5 V y por el pin; cuantas vueltas hasta que asienta */
    REF_A->CTL0 = REF_A_CTL0_VSEL_3 | REF_A_CTL0_ON | REF_A_CTL0_OUT;
    uint32_t n = 0;
    while (!(REF_A->CTL0 & REF_A_CTL0_GENRDY)) ++n;
    buzon.r[0] = n;
    buzon.r[1] = REF_A->CTL0;
    /* El sensor: A22 con TCMAP, contra la referencia; muestreo largo */
    ADC14->CTL0 = 0;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_6 |
                  ADC14_CTL0_SSEL__MODCLK;
    ADC14->CTL1 = ADC14_CTL1_RES__14BIT | ADC14_CTL1_TCMAP;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_22 | ADC14_MCTLN_VRSEL_1;
    REF_A->CTL0 &= ~REF_A_CTL0_TCOFF;
    const uint32_t raw = convierte();
    buzon.r[2] = raw;
    /* La cuenta de los ejemplos de TI, en centesimas de grado */
    const int32_t c30 = (int32_t)TLV->ADC14_REF2P5V_TS30C;
    const int32_t c85 = (int32_t)TLV->ADC14_REF2P5V_TS85C;
    buzon.r[3] = (uint32_t)(((int32_t)raw - c30) * 5500 / (c85 - c30) + 3000);
    /* AVCC/2 con BATMAP, contra AVCC */
    ADC14->CTL0 &= ~ADC14_CTL0_ENC;
    ADC14->CTL1 = ADC14_CTL1_RES__14BIT | ADC14_CTL1_BATMAP;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_23 | ADC14_MCTLN_VRSEL_0;
    buzon.r[4] = convierte();
    /* A0 contra 1,2 V */
    ADC14->CTL0 &= ~ADC14_CTL0_ENC;
    REF_A->CTL0 = REF_A_CTL0_VSEL_0 | REF_A_CTL0_ON;
    while (!(REF_A->CTL0 & REF_A_CTL0_GENRDY)) { }
    ADC14->CTL1 = ADC14_CTL1_RES__14BIT;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0 | ADC14_MCTLN_VRSEL_1;
    buzon.r[5] = convierte();
    buzon.estado = 1;
    /* Y la referencia, otra vez a 2,5 V y por el pin, para que la mida el banco */
    ADC14->CTL0 &= ~ADC14_CTL0_ENC;
    REF_A->CTL0 = REF_A_CTL0_VSEL_3 | REF_A_CTL0_ON | REF_A_CTL0_OUT;
    while (!(REF_A->CTL0 & REF_A_CTL0_GENRDY)) { }
    buzon.estado = 2;
    for (;;) { }
}

static void m33_timer(void) {
    analogico(P5, BIT5);
    /* TA0 a 1 kHz con SMCLK = 3 MHz; TA0.1 reset/set al 50 %: el flanco de
       subida, en cada periodo */
    TIMER_A0->CCR[0] = 2999;
    TIMER_A0->CCR[1] = 1500;
    TIMER_A0->CCTL[1] = TIMER_A_CCTLN_OUTMOD_7;
    ADC14->CTL0 = 0;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_2 |
                  ADC14_CTL0_CONSEQ_2 | ADC14_CTL0_SHS_1 | ADC14_CTL0_SSEL__MODCLK;
    ADC14->CTL1 = ADC14_CTL1_RES__12BIT;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0;
    ADC14->IER0 = ADC14_IER0_IE0;
    NVIC->ISER[0] = 1u << ADC14_IRQn;
    ADC14->CTL0 |= ADC14_CTL0_ENC;
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR;
    buzon.estado = 1;
    for (;;) { }
}

static void m34_ventana(void) {
    analogico(P5, BIT5);
    adc_base(ADC14_CTL1_RES__14BIT);
    ADC14->LO0 = 4000;
    ADC14->HI0 = 6000;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0 | ADC14_MCTLN_WINC;
    for (uint32_t k = 0; k < 3; ++k) {
        espera_senal(k + 1);
        ADC14->CLRIFGR1 = 0x7E;
        buzon.r[2 * k] = convierte();
        buzon.r[2 * k + 1] = ADC14->IFGR1 & 0x0E;
        buzon.estado = k + 1;
    }
    /* El desbordamiento: dos conversiones sin leer MEM0 */
    ADC14->CLRIFGR1 = 0x7E;
    ADC14->CLRIFGR0 = 1;
    ADC14->CTL0 |= ADC14_CTL0_ENC | ADC14_CTL0_SC;
    while (!(ADC14->IFGR0 & 1)) { }
    ADC14->CTL0 |= ADC14_CTL0_SC;
    while (ADC14->CTL0 & ADC14_CTL0_BUSY) { }
    while (ADC14->CTL0 & ADC14_CTL0_SC) { }
    for (volatile int i = 0; i < 100; ++i) { }
    buzon.r[6] = ADC14->IFGR1 & ADC14_IFGR1_OVIFG;
    /* El IV, con todo habilitado: OV primero, luego HI, luego IFG0 */
    ADC14->IER1 = ADC14_IER1_OVIE | ADC14_IER1_HIIE;
    ADC14->IER0 = 1;
    buzon.r[7] = ADC14->IV;
    buzon.r[8] = ADC14->IV;
    buzon.r[9] = ADC14->IV;
    buzon.r[10] = ADC14->IV;
    buzon.estado = 4;
    for (;;) { }
}


/* --- uDMA -------------------------------------------------------------------- */

/* La tabla de control: 8 canales, primaria y alternativa */
static volatile uint32_t tabla[64] __attribute__((aligned(256)));
#define CTL_CICLO(c)   ((uint32_t)(c))
#define CTL_N(n)       ((uint32_t)((n) - 1) << 4)
#define CTL_R(r)       ((uint32_t)(r) << 14)
#define CTL_SRC(sz, inc) (((uint32_t)(sz) << 24) | ((uint32_t)(inc) << 26))
#define CTL_DST(sz, inc) (((uint32_t)(sz) << 28) | ((uint32_t)(inc) << 30))
static void estructura(unsigned ch, int alt, const volatile void* src_fin,
                       volatile void* dst_fin, uint32_t ctl) {
    volatile uint32_t* e = &tabla[(alt ? 32 : 0) + 4 * ch];
    e[0] = (uint32_t)src_fin; e[1] = (uint32_t)dst_fin; e[2] = ctl; e[3] = 0;
}
static void dma_on(void) {
    DMA_Control->CFG = 1;                        /* master enable */
    DMA_Control->CTLBASE = (uint32_t)tabla;
}
static volatile uint32_t int0_n = 0, int0_flg = 0, int1_n = 0, err_n = 0;
void DMA_INT0_IRQHandler(void) {
    int0_flg |= DMA_Channel->INT0_SRCFLG;
    DMA_Channel->INT0_CLRFLG = DMA_Channel->INT0_SRCFLG;
    int0_n++;
}
void DMA_ERR_IRQHandler(void) { DMA_Control->ERRCLR = 1; err_n++; }

static uint32_t m40_a[64], m40_b[64];
static void m40_memoria(void) {
    for (int i = 0; i < 64; ++i) { m40_a[i] = 0xC0DE0000u + (uint32_t)i; m40_b[i] = 0; }
    dma_on();
    NVIC->ISER[1] = 1u << (DMA_INT0_IRQn - 32);
    NVIC->ISER[0] = 1u << DMA_ERR_IRQn;
    estructura(3, 0, &m40_a[63], &m40_b[63],
               CTL_CICLO(2) | CTL_N(64) | CTL_R(2) | CTL_SRC(2, 2) | CTL_DST(2, 2));
    DMA_Control->ENASET = 1u << 3;
    DMA_Channel->SW_CHTRIG = 1u << 3;
    while (!int0_n) { }
    uint32_t bien = 0;
    for (int i = 0; i < 64; ++i) bien += (m40_b[i] == 0xC0DE0000u + (uint32_t)i);
    buzon.r[0] = bien;
    buzon.r[1] = int0_flg;
    buzon.r[2] = tabla[4 * 3 + 2] & 0x3FF7u;     /* cycle_ctrl y n_minus_1, a 0 */
    buzon.r[3] = DMA_Control->ENASET & (1u << 3);
    /* Un error: la tabla en una direccion donde no hay nada */
    DMA_Control->CTLBASE = 0x30000000u;
    DMA_Control->ENASET = 1u << 2;
    DMA_Channel->SW_CHTRIG = 1u << 2;
    while (!err_n) { }
    buzon.r[4] = err_n;
    buzon.r[5] = DMA_Control->ENASET & (1u << 2);
    buzon.estado = 1;
    for (;;) { }
}

static volatile uint16_t m43_a[4], m43_b[4];
static volatile uint32_t m43_n = 0;
static void m43_rearma(int alt) {
    estructura(7, alt, &ADC14->MEM[0], alt ? &m43_b[3] : &m43_a[3],
               CTL_CICLO(3) | CTL_N(4) | CTL_R(0) | CTL_SRC(1, 3) | CTL_DST(1, 1));
}

static const char m41_tx[32] = "La uDMA del MSP432, por la UCA0.";
static volatile uint8_t m41_rx[32];
void DMA_INT1_IRQHandler(void) {
    int1_n++;
    if (buzon.modo == 43) {
        /* La mitad que acaba de llenarse es la que NO esta activa ahora */
        const int alt = (DMA_Control->ALTSET >> 7) & 1;
        const int hecha = !alt;
        const volatile uint16_t* b = hecha ? m43_b : m43_a;
        const uint32_t n = m43_n;
        if (n < 6) for (int i = 0; i < 4; ++i) buzon.r[1 + 4 * n + i] = b[i];
        m43_n = n + 1;
        buzon.r[0] = m43_n;
        buzon.r[30 + (n < 6 ? n : 5)] = (uint32_t)hecha;
        m43_rearma(hecha);
    }
}
static void m41_uart(void) {
    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST;
    EUSCI_A0->CTLW0 = EUSCI_A_CTLW0_SWRST | EUSCI_A_CTLW0_SSEL__SMCLK;
    EUSCI_A0->BRW = 3;                           /* 3 MHz / 3 = 1 Mbaudio */
    EUSCI_A0->MCTLW = 0;
    EUSCI_A0->STATW = EUSCI_A_STATW_LISTEN;
    EUSCI_A0->CTLW0 &= ~EUSCI_A_CTLW0_SWRST;
    dma_on();
    DMA_Channel->CH_SRCCFG[0] = 1;               /* EUSCIA0TX */
    DMA_Channel->CH_SRCCFG[1] = 1;               /* EUSCIA0RX */
    DMA_Channel->INT1_SRCCFG = DMA_INT1_SRCCFG_EN | 1;
    NVIC->ISER[1] = 1u << (DMA_INT1_IRQn - 32);
    estructura(1, 0, &EUSCI_A0->RXBUF, &m41_rx[31],
               CTL_CICLO(1) | CTL_N(32) | CTL_R(0) | CTL_SRC(0, 3) | CTL_DST(0, 0));
    estructura(0, 0, &m41_tx[31], &EUSCI_A0->TXBUF,
               CTL_CICLO(1) | CTL_N(32) | CTL_R(0) | CTL_SRC(0, 0) | CTL_DST(0, 3));
    DMA_Control->ENASET = 3;
    while (!int1_n) { }
    uint32_t bien = 0;
    for (int i = 0; i < 32; ++i) bien += (m41_rx[i] == (uint8_t)m41_tx[i]);
    buzon.r[0] = bien;
    buzon.r[1] = int0_flg | DMA_Channel->INT0_SRCFLG;   /* el canal 0, por INT0 */
    buzon.r[2] = DMA_Control->ENASET & 3;
    buzon.estado = 1;
    for (;;) { }
}

static volatile uint16_t m42_buf[16];
static void m42_adc(void) {
    analogico(P5, BIT5);
    dma_on();
    DMA_Channel->CH_SRCCFG[7] = 7;               /* ADC14 */
    estructura(7, 0, &ADC14->MEM[0], &m42_buf[15],
               CTL_CICLO(1) | CTL_N(16) | CTL_R(0) | CTL_SRC(1, 3) | CTL_DST(1, 1));
    DMA_Control->ENASET = 1u << 7;
    ADC14->CTL0 = 0;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_3 | ADC14_CTL0_MSC |
                  ADC14_CTL0_CONSEQ_2 | ADC14_CTL0_SSEL__MODCLK;
    ADC14->CTL1 = ADC14_CTL1_RES__12BIT;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0;
    ADC14->CTL0 |= ADC14_CTL0_ENC | ADC14_CTL0_SC;
    while (DMA_Control->ENASET & (1u << 7)) { }
    ADC14->CTL0 &= ~ADC14_CTL0_ENC;
    for (int i = 0; i < 16; ++i) buzon.r[i] = m42_buf[i];
    buzon.r[16] = DMA_Channel->INT0_SRCFLG;
    buzon.estado = 1;
    for (;;) { }
}

static void m43_pingpong(void) {
    analogico(P5, BIT5);
    dma_on();
    DMA_Channel->CH_SRCCFG[7] = 7;
    DMA_Channel->INT1_SRCCFG = DMA_INT1_SRCCFG_EN | 7;
    NVIC->ISER[1] = 1u << (DMA_INT1_IRQn - 32);
    m43_rearma(0); m43_rearma(1);
    DMA_Control->ALTCLR = 1u << 7;
    DMA_Control->ENASET = 1u << 7;
    TIMER_A0->CCR[0] = 2999;
    TIMER_A0->CCR[1] = 1500;
    TIMER_A0->CCTL[1] = TIMER_A_CCTLN_OUTMOD_7;
    ADC14->CTL0 = 0;
    ADC14->CTL0 = ADC14_CTL0_ON | ADC14_CTL0_SHP | ADC14_CTL0_SHT0_2 |
                  ADC14_CTL0_CONSEQ_2 | ADC14_CTL0_SHS_1 | ADC14_CTL0_SSEL__MODCLK;
    ADC14->CTL1 = ADC14_CTL1_RES__12BIT;
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_0;
    ADC14->CTL0 |= ADC14_CTL0_ENC;
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR;
    buzon.estado = 1;
    for (;;) { }
}

/* --- CRC32 ------------------------------------------------------------------- */

static const uint8_t m50_txt[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
static void m50_crc(void) {
    CRC32->INIRES16 = 0xFFFF;
    for (int i = 0; i < 9; ++i) *(volatile uint8_t*)&CRC32->DIRB16 = m50_txt[i];
    buzon.r[0] = CRC32->INIRES16;                 /* CRC-CCITT (MSB primero) */
    CRC32->INIRES16 = 0xFFFF;
    for (int i = 0; i < 9; ++i) *(volatile uint8_t*)&CRC32->DI16 = m50_txt[i];
    buzon.r[1] = CRC32->RESR16;                   /* reflejado, sin XOR final */
    CRC32->INIRES32_HI = 0xFFFF; CRC32->INIRES32_LO = 0xFFFF;
    for (int i = 0; i < 9; ++i) *(volatile uint8_t*)&CRC32->DI32 = m50_txt[i];
    buzon.r[2] = ~(((uint32_t)CRC32->RESR32_HI << 16) | CRC32->RESR32_LO);
    /* Ocho bytes de uno en uno, y como dos palabras de 32 bits (dos medias) */
    CRC32->INIRES32_HI = 0xFFFF; CRC32->INIRES32_LO = 0xFFFF;
    for (int i = 0; i < 8; ++i) *(volatile uint8_t*)&CRC32->DI32 = m50_txt[i];
    buzon.r[3] = ((uint32_t)CRC32->INIRES32_HI << 16) | CRC32->INIRES32_LO;
    CRC32->INIRES32_HI = 0xFFFF; CRC32->INIRES32_LO = 0xFFFF;
    for (int i = 0; i < 8; i += 4) {
        const uint32_t w = (uint32_t)m50_txt[i] | ((uint32_t)m50_txt[i + 1] << 8) |
                           ((uint32_t)m50_txt[i + 2] << 16) | ((uint32_t)m50_txt[i + 3] << 24);
        CRC32->DI32 = (uint16_t)w;
        CRC32->DI32 = (uint16_t)(w >> 16);
    }
    buzon.r[4] = ((uint32_t)CRC32->INIRES32_HI << 16) | CRC32->INIRES32_LO;
    buzon.estado = 1;
    for (;;) { }
}

/* --- AES256 ------------------------------------------------------------------ */

static volatile uint32_t aes_irq = 0;
void AES256_IRQHandler(void) { AES256->CTL0 &= ~AES256_CTL0_RDYIFG; aes_irq++; }
static void aes_clave(const uint8_t* k, int n, uint16_t kl) {
    AES256->CTL0 = (AES256->CTL0 & ~AES256_CTL0_KL_MASK) | kl;
    for (int i = 0; i < n; i += 2) AES256->KEY = (uint16_t)(k[i] | (k[i + 1] << 8));
    while (!(AES256->STAT & AES256_STAT_KEYWR)) { }
}
static uint32_t aes_bloque(const uint8_t* in, uint8_t* out) {
    for (int i = 0; i < 16; i += 2) AES256->DIN = (uint16_t)(in[i] | (in[i + 1] << 8));
    AES256->STAT |= AES256_STAT_KEYWR;
    uint32_t n = 0;
    while (AES256->STAT & AES256_STAT_BUSY) ++n;
    for (int i = 0; i < 16; i += 2) {
        const uint16_t v = AES256->DOUT;
        out[i] = (uint8_t)v; out[i + 1] = (uint8_t)(v >> 8);
    }
    return n;
}
static void m51_aes(void) {
    uint8_t k[32], pt[16], ct[16], dt[16];
    for (int i = 0; i < 32; ++i) k[i] = (uint8_t)i;
    for (int i = 0; i < 16; ++i) pt[i] = (uint8_t)(0x11 * i);
    AES256->CTL0 = AES256_CTL0_SWRST;
    AES256->CTL0 = AES256_CTL0_RDYIE;
    NVIC->ISER[0] = 1u << AES256_IRQn;
    /* AES-128: cifrar */
    AES256->CTL0 &= ~AES256_CTL0_OP_MASK;
    aes_clave(k, 16, AES256_CTL0_KL__128BIT);
    buzon.r[0] = aes_bloque(pt, ct);
    for (int i = 0; i < 16; ++i) buzon.buf[i] = ct[i];
    /* Generar la clave de descifrado (OP = 2) y descifrar con ella (OP = 3) */
    AES256->CTL0 = (AES256->CTL0 & ~AES256_CTL0_OP_MASK) | AES256_CTL0_OP_2;
    for (int i = 0; i < 16; i += 2) AES256->KEY = (uint16_t)(k[i] | (k[i + 1] << 8));
    uint32_t n = 0;
    while (AES256->STAT & AES256_STAT_BUSY) ++n;
    buzon.r[1] = n;
    AES256->CTL0 |= AES256_CTL0_OP_3;
    aes_bloque(ct, dt);
    for (int i = 0; i < 16; ++i) buzon.buf[16 + i] = dt[i];
    /* Cifrar con OP = 0 sin volver a cargar la clave: el registro tiene la de
       descifrado, y sale otra cosa */
    AES256->CTL0 &= ~AES256_CTL0_OP_MASK;
    aes_bloque(pt, dt);
    buzon.r[2] = (dt[0] == ct[0] && dt[1] == ct[1] && dt[15] == ct[15]);
    /* AES-256 */
    aes_clave(k, 32, AES256_CTL0_KL__256BIT);
    buzon.r[3] = aes_bloque(pt, ct);
    for (int i = 0; i < 16; ++i) buzon.buf[32 + i] = ct[i];
    /* Escribir DIN con BUSY levanta ERRFG */
    for (int i = 0; i < 16; i += 2) AES256->DIN = 0;
    AES256->DIN = 0x1234;
    buzon.r[4] = AES256->CTL0 & AES256_CTL0_ERRFG;
    while (AES256->STAT & AES256_STAT_BUSY) { }
    buzon.r[5] = aes_irq;
    buzon.estado = 1;
    for (;;) { }
}

/* --- RTC_C ------------------------------------------------------------------- */

static volatile uint32_t rtc_rdy = 0, rtc_tev = 0, rtc_al = 0, rtc_ps0 = 0;
static volatile uint32_t rtc_n = 0;
void RTC_C_IRQHandler(void) {
    switch (RTC_C->IV) {
    case 0x04:
        if (rtc_rdy < 4) {
            buzon.r[4 + 3 * rtc_rdy] = RTC_C->TIM0;
            buzon.r[5 + 3 * rtc_rdy] = RTC_C->TIM1;
            buzon.r[6 + 3 * rtc_rdy] = RTC_C->DATE;
        }
        rtc_rdy++;
        break;
    case 0x06: rtc_tev++; break;
    case 0x08: rtc_al++; break;
    case 0x0A: rtc_ps0++; break;
    case 0x0C: rtc_n++; break;
    default: break;
    }
}
static void m52_rtc(void) {
    CS->KEY = CS_KEY_VAL;
    CS->CTL1 |= CS_CTL1_SELB;                    /* BCLK = REFO, 32 768 Hz */
    CS->KEY = 0;
    RTC_C->CTL0 = RTC_C_KEY;                     /* abierto */
    RTC_C->CTL13 = RTC_C_CTL13_HOLD | RTC_C_CTL13_BCD | RTC_C_CTL13_TEV_2;  /* medianoche */
    RTC_C->TIM0 = (0x59 << 8) | 0x58;            /* 23:59:58, jueves */
    RTC_C->TIM1 = (4 << 8) | 0x23;
    RTC_C->DATE = (0x10 << 8) | 0x15;            /* 15 de octubre */
    RTC_C->YEAR = 0x2026;
    RTC_C->AMINHR = (uint16_t)((RTC_C_AMINHR_HOURAE | 0x0000) | RTC_C_AMINHR_MINAE);  /* 00:00 */
    RTC_C->ADOWDAY = 0;
    RTC_C->PS0CTL = RTC_C_PS0CTL_RT0IP_7 | RTC_C_PS0CTL_RT0PSIE;   /* 32768/256 */
    RTC_C->CTL0 = RTC_C_KEY | RTC_C_CTL0_RDYIE | RTC_C_CTL0_TEVIE | RTC_C_CTL0_AIE;
    RTC_C->CTL13 &= ~RTC_C_CTL13_HOLD;
    RTC_C->CTL0 = (RTC_C->CTL0 & 0xFF) | 0x0000; /* cerrado */
    buzon.r[0] = RTC_C->CTL0 >> 8;               /* 0x96 */
    /* Cerrado, CTL13 no se deja tocar */
    RTC_C->CTL13 |= RTC_C_CTL13_HOLD;
    buzon.r[1] = RTC_C->CTL13 & RTC_C_CTL13_HOLD;
    /* Los conversores */
    RTC_C->BIN2BCD = 1234;
    buzon.r[2] = RTC_C->BIN2BCD;
    RTC_C->BCD2BIN = 0x0987;
    buzon.r[3] = RTC_C->BCD2BIN;
    NVIC->ISER[0] = 1u << RTC_C_IRQn;
    buzon.estado = 1;
    while (rtc_rdy < 3) __WFI();
    buzon.r[16] = rtc_tev;
    buzon.r[17] = rtc_al;
    buzon.r[18] = rtc_ps0;
    buzon.r[19] = RTC_C->YEAR;
    buzon.estado = 2;
    for (;;) __WFI();
}

/* --- COMP_E ------------------------------------------------------------------ */

static volatile uint32_t ce_sube = 0, ce_baja = 0;
void COMP_E0_IRQHandler(void) {
    switch (COMP_E0->IV) {
    case 0x02: ce_sube++; break;
    case 0x04: ce_baja++; break;
    default: break;
    }
}
static void m53_comp(void) {
    P7->SEL0 |= BIT7; P7->SEL1 |= BIT7;          /* C0.2 */
    P7->SEL0 |= BIT1; P7->DIR |= BIT1;           /* C0OUT, por el PMAP de reset */
    COMP_E0->CTL0 = COMP_E_CTL0_IPEN | 2;        /* V+ = C0.2 */
    COMP_E0->CTL2 = COMP_E_CTL2_RSEL | COMP_E_CTL2_RS_1 | (19 << 8) | 15;   /* V- = escalera */
    COMP_E0->CTL3 = 1u << 2;
    COMP_E0->CTL1 = COMP_E_CTL1_ON | COMP_E_CTL1_PWRMD_1;
    COMP_E0->INT = COMP_E_INT_IE | COMP_E_INT_IIE;
    NVIC->ISER[0] = 1u << COMP_E0_IRQn;
    buzon.estado = 1;
    for (uint32_t k = 0; k < 5; ++k) {
        espera_senal(k + 1);
        for (volatile int i = 0; i < 50; ++i) { }
        buzon.r[3 * k] = COMP_E0->CTL1 & COMP_E_CTL1_OUT;
        buzon.r[3 * k + 1] = ce_sube;
        buzon.r[3 * k + 2] = ce_baja;
        buzon.estado = k + 2;
    }
    for (;;) { }
}

/* --- Bajo consumo ------------------------------------------------------------- */

static void pcm_lpmr(uint32_t lpmr) {
    const uint32_t amr = (PCM->CTL0 & PCM_CTL0_CPM_MASK) >> PCM_CTL0_CPM_OFS;
    PCM->CTL0 = PCM_CTL0_KEY_VAL | lpmr | amr;
}
static volatile uint32_t ta1_n = 0, p1_n = 0;
void TA1_0_IRQHandler(void) { TIMER_A1->CCTL[0] &= ~TIMER_A_CCTLN_CCIFG; ta1_n++; }
void PORT1_IRQHandler(void) { (void)P1->IV; p1_n++; }
static void m60_lpm(void) {
    buzon.estado = 1;                            /* activo, a 3 MHz */
    espera_senal(1);
    /* TA1 en ACLK (REFO, 32 768 Hz): una interrupcion cada 10 ms */
    TIMER_A1->CCR[0] = 327;
    TIMER_A1->CCTL[0] = TIMER_A_CCTLN_CCIE;
    TIMER_A1->CTL = TIMER_A_CTL_SSEL__ACLK | TIMER_A_CTL_MC__CONTINUOUS | TIMER_A_CTL_CLR;
    TIMER_A1->CTL = TIMER_A_CTL_SSEL__ACLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR;
    NVIC->ISER[0] = 1u << TA1_0_IRQn;
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;          /* LPM0 */
    buzon.estado = 2;
    while (ta1_n < 3) __WFI();
    /* LPM3: SLEEPDEEP con LPMR = 0. Solo el RTC y el perro funcionan: TA1,
       en ACLK, se para (sin su interrupcion, se mira su contador); y el
       Timer32, en MCLK, tambien. Despierta el preescalador del RTC */
    TIMER_A1->CCTL[0] = 0;
    TIMER_A1->CTL = TIMER_A_CTL_SSEL__ACLK | TIMER_A_CTL_MC__CONTINUOUS;
    CS->KEY = CS_KEY_VAL; CS->CTL1 |= CS_CTL1_SELB; CS->KEY = 0;   /* BCLK = REFO */
    RTC_C->CTL0 = RTC_C_KEY;
    RTC_C->CTL13 &= ~RTC_C_CTL13_HOLD;
    RTC_C->CTL0 = 0;
    RTC_C->PS1CTL = RTC_C_PS1CTL_RT1IP_0 | RTC_C_PS1CTL_RT1PSIE;  /* 64 Hz */
    NVIC->ISER[0] = 1u << RTC_C_IRQn;
    t32_libre();
    const uint32_t t0 = t32();
    const uint16_t a0 = TIMER_A1->R;
    pcm_lpmr(PCM_CTL0_LPMR__LPM3);
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    buzon.estado = 3;
    while (rtc_n < 3) __WFI();
    buzon.r[0] = t0 - t32();
    buzon.r[1] = rtc_n;
    buzon.r[3] = (uint16_t)(TIMER_A1->R - a0);
    /* LPM4: sin RTC ni perro; despierta S1 */
    RTC_C->PS1CTL = 0;
    RTC_C->CTL0 = RTC_C_KEY; RTC_C->CTL13 |= RTC_C_CTL13_HOLD; RTC_C->CTL0 = 0;
    TIMER_A1->CTL = 0;
    P1->DIR &= ~BIT1; P1->REN |= BIT1; P1->OUT |= BIT1;
    P1->IES |= BIT1; P1->IFG = 0; P1->IE |= BIT1;
    NVIC->ISER[1] = 1u << (PORT1_IRQn - 32);
    buzon.estado = 4;
    while (!p1_n) __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    buzon.r[2] = p1_n;
    buzon.estado = 5;
    for (;;) { }
}

/* LPM3.5: P1.0 a 1, el RTC con RT0PS cada 256 tics, y a dormir */
static void m61_lpm35(void) {
    P1->DIR |= BIT0; P1->OUT |= BIT0;
    CS->KEY = CS_KEY_VAL; CS->CTL1 |= CS_CTL1_SELB; CS->KEY = 0;   /* BCLK = REFO */
    RTC_C->CTL0 = RTC_C_KEY;
    RTC_C->CTL13 &= ~RTC_C_CTL13_HOLD;
    RTC_C->CTL0 = 0;
    RTC_C->PS0CTL = RTC_C_PS0CTL_RT0IP_7 | RTC_C_PS0CTL_RT0PSIE;  /* 7,8 ms */
    buzon.r[0] = RTC_C->TIM0;
    pcm_lpmr(PCM_CTL0_LPMR__LPM35);
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    buzon.estado = 1;
    __WFI();
    for (;;) { }                                 /* no se llega: se sale por un POR */
}
/* Lo primero tras salir de LPMx.5 */
static void despierta_lpm5(uint32_t stat) {
    buzon.r[10] = stat;
    buzon.r[11] = PCM->CTL1 & PCM_CTL1_LOCKLPM5;
    buzon.r[12] = P1->IFG;
    buzon.r[13] = RTC_C->PS0CTL & RTC_C_PS0CTL_RT0PSIFG;
    buzon.r[14] = buzon.arranques;
    buzon.estado = 10;
    espera_senal(10);
    /* Rehacer la configuracion de las E/S y soltar el cerrojo */
    P1->OUT &= ~BIT0; P1->DIR |= BIT0;
    PCM->CTL1 = PCM_CTL1_KEY_VAL;                /* LOCKLPM5 = 0 */
    RSTCTL->PCMRESET_CLR = RSTCTL_PCMRESET_CLR_CLR;
    buzon.r[15] = RSTCTL->PCMRESET_STAT;
    buzon.estado = 11;
    for (;;) { }
}
static void m62_lpm45(void) {
    P1->DIR &= ~BIT1; P1->REN |= BIT1; P1->OUT |= BIT1;
    P1->IES |= BIT1; P1->IFG = 0; P1->IE |= BIT1;
    pcm_lpmr(PCM_CTL0_LPMR__LPM45);
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    buzon.estado = 1;
    __WFI();
    for (;;) { }
}

/* --- BOOSTXL-EDUMKII ---------------------------------------------------------- */

static void ucb1_init(void) {
    P6->SEL0 |= BIT4 | BIT5;                     /* UCB1SDA, UCB1SCL */
    EUSCI_B1->CTLW0 = EUSCI_B_CTLW0_SWRST;
    EUSCI_B1->CTLW0 = EUSCI_B_CTLW0_SWRST | EUSCI_B_CTLW0_MODE_3 | EUSCI_B_CTLW0_MST |
                      EUSCI_B_CTLW0_SYNC | EUSCI_B_CTLW0_SSEL__SMCLK;
    EUSCI_B1->BRW = 30;                          /* 100 kHz */
    EUSCI_B1->CTLW0 &= ~EUSCI_B_CTLW0_SWRST;
}
static void i2c_reg_esc(uint8_t a, uint8_t r, uint16_t v) {
    EUSCI_B_Type* b = EUSCI_B1;
    b->I2CSA = a;
    b->IFG = 0;
    b->CTLW0 |= EUSCI_B_CTLW0_TR | EUSCI_B_CTLW0_TXSTT;
    const uint8_t d[3] = { r, (uint8_t)(v >> 8), (uint8_t)v };
    for (int i = 0; i < 3; ++i) {
        while (!(b->IFG & EUSCI_B_IFG_TXIFG0)) { }
        b->TXBUF = d[i];
    }
    while (!(b->IFG & EUSCI_B_IFG_TXIFG0)) { }
    b->CTLW0 |= EUSCI_B_CTLW0_TXSTP;
    while (b->CTLW0 & EUSCI_B_CTLW0_TXSTP) { }
}
static uint16_t i2c_reg_lee(uint8_t a, uint8_t r) {
    EUSCI_B_Type* b = EUSCI_B1;
    b->I2CSA = a;
    b->IFG = 0;
    b->CTLW0 |= EUSCI_B_CTLW0_TR | EUSCI_B_CTLW0_TXSTT;
    while (!(b->IFG & EUSCI_B_IFG_TXIFG0)) { }
    b->TXBUF = r;
    while (b->CTLW0 & EUSCI_B_CTLW0_TXSTT) { }
    while (!(b->IFG & EUSCI_B_IFG_TXIFG0)) { }
    b->CTLW0 &= ~EUSCI_B_CTLW0_TR;
    b->CTLW0 |= EUSCI_B_CTLW0_TXSTT;
    while (b->CTLW0 & EUSCI_B_CTLW0_TXSTT) { }
    while (!(b->IFG & EUSCI_B_IFG_RXIFG0)) { }
    const uint8_t hi = (uint8_t)b->RXBUF;
    b->CTLW0 |= EUSCI_B_CTLW0_TXSTP;
    while (!(b->IFG & EUSCI_B_IFG_RXIFG0)) { }
    const uint8_t lo = (uint8_t)b->RXBUF;
    while (b->CTLW0 & EUSCI_B_CTLW0_TXSTP) { }
    return (uint16_t)((hi << 8) | lo);
}
static void m70_sensores(void) {
    P3->DIR &= ~BIT6; P3->REN |= BIT6; P3->OUT |= BIT6;   /* DRDY: pull-up */
    ucb1_init();
    buzon.r[0] = i2c_reg_lee(0x44, 0x7E);
    buzon.r[1] = i2c_reg_lee(0x44, 0x7F);
    buzon.r[2] = i2c_reg_lee(0x44, 0x01);        /* 0xC810 */
    i2c_reg_esc(0x44, 0x01, 0xC610);             /* auto, 100 ms, continuo, L */
    uint32_t n = 0;
    while (!(i2c_reg_lee(0x44, 0x01) & 0x0080)) ++n;    /* CRF */
    const uint16_t res = i2c_reg_lee(0x44, 0x00);
    buzon.r[3] = res;
    buzon.r[4] = (uint32_t)(1u << (res >> 12)) * (res & 0xFFF);   /* centilux */
    buzon.r[5] = n;
    buzon.r[6] = i2c_reg_lee(0x40, 0xFE);
    buzon.r[7] = i2c_reg_lee(0x40, 0xFF);
    i2c_reg_esc(0x40, 0x02, 0x7100);             /* continuo, 4/s, DRDY */
    (void)i2c_reg_lee(0x40, 0x00);               /* lo que hubiera de antes */
    buzon.r[8] = (P3->IN & BIT6) != 0;           /* DRDY alta: nada listo */
    while (P3->IN & BIT6) { }                    /* DRDY baja: hay dato */
    buzon.r[9] = i2c_reg_lee(0x40, 0x00);
    buzon.r[10] = i2c_reg_lee(0x40, 0x01);
    buzon.r[11] = (P3->IN & BIT6) != 0;          /* leerlo la sube */
    buzon.estado = 1;
    for (;;) { }
}
static void m71_zumbador(void) {
    /* TA0.4 en P2.7 (PMAP de reset), 2 kHz al 50 % con SMCLK = 3 MHz */
    P2->SEL0 |= BIT7; P2->DIR |= BIT7;
    TIMER_A0->CCR[0] = 1499;
    TIMER_A0->CCR[4] = 750;
    TIMER_A0->CCTL[4] = TIMER_A_CCTLN_OUTMOD_7;
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR;
    /* El eje X del joystick, A15 en P6.0 */
    P6->SEL0 |= BIT0; P6->SEL1 |= BIT0;
    adc_base(ADC14_CTL1_RES__12BIT);
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_15;
    buzon.r[0] = convierte();
    buzon.estado = 1;
    for (;;) { }
}

int main(void) {
    para_perro();
    buzon.arranques++;
    {
        const uint32_t st = RSTCTL->PCMRESET_STAT;
        if (st & (RSTCTL_PCMRESET_STAT_LPM35 | RSTCTL_PCMRESET_STAT_LPM45)) despierta_lpm5(st);
    }
    switch (buzon.modo) {
    case 30: m30_simple(); break;
    case 31: m31_secuencia(); break;
    case 32: m32_ref(); break;
    case 33: m33_timer(); break;
    case 34: m34_ventana(); break;
    case 40: m40_memoria(); break;
    case 41: m41_uart(); break;
    case 42: m42_adc(); break;
    case 43: m43_pingpong(); break;
    case 50: m50_crc(); break;
    case 51: m51_aes(); break;
    case 52: m52_rtc(); break;
    case 53: m53_comp(); break;
    case 60: m60_lpm(); break;
    case 61: m61_lpm35(); break;
    case 62: m62_lpm45(); break;
    case 70: m70_sensores(); break;
    case 71: m71_zumbador(); break;
    default: break;
    }
    for (;;) { }
}
