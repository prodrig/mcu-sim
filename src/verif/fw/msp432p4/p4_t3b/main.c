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

int main(void) {
    para_perro();
    buzon.arranques++;
    switch (buzon.modo) {
    case 30: m30_simple(); break;
    case 31: m31_secuencia(); break;
    case 32: m32_ref(); break;
    case 33: m33_timer(); break;
    case 34: m34_ventana(); break;
    default: break;
    }
    for (;;) { }
}
