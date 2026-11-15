/* intc.c — E3: las reglas del INTC del C3 [TRM-C3 §1.5.2] y las fuentes de
 * tiempo (SYSTIMER en modo periodo y la alarma del T0 de TIMG0).
 *
 * Usa las cuatro interrupciones por software (SYSTEM_CPU_INTR_FROM_CPU_0-3,
 * fuentes 50-53), que se levantan y bajan escribiendo un registro. Cada
 * prueba deja su resultado en buzon.res[k]; el banco los comprueba. */
#include "c3.h"
static volatile uint32_t orden[8], n_orden, cuenta[32];
static volatile uint32_t edge_borra;
static uint64_t t_st[8];
static volatile uint32_t n_st;

static void anota(uint32_t id) { if (n_orden < 8) orden[n_orden++] = id; ++cuenta[id]; }
/* Las de software: se bajan en la fuente (son de nivel) */
void isr_1(void)  { anota(1); SYSTEM_FROM_CPU(0) = 0; }
void isr_2(void)  { anota(2); SYSTEM_FROM_CPU(0) = 0; }
void isr_4(void)  { anota(4); SYSTEM_FROM_CPU(1) = 0; }
void isr_6(void)  { anota(6); SYSTEM_FROM_CPU(2) = 0; }
void isr_7(void)  { anota(7); SYSTEM_FROM_CPU(1) = 0; }
void isr_9(void)  { anota(9); SYSTEM_FROM_CPU(2) = 0; }
void isr_10(void) {
    /* De flanco: la primera vez NO se borra (vuelve a entrar), la segunda se
     * borra con el pulso en CPU_INT_CLEAR */
    anota(10);
    if (cuenta[10] >= 2) { INT_CLEAR |= 1u << 10; INT_CLEAR &= ~(1u << 10); }
}
void isr_12(void) { anota(12); SYSTEM_FROM_CPU(3) = 0; }
void isr_13(void) {               /* SYSTIMER, periodo */
    ST_INT_CLR = 1u;
    if (n_st < 8) t_st[n_st] = systimer_ahora();
    ++n_st;
    if (n_st >= 5) ST_CONF &= ~(1u << 24);
}
void isr_14(void) { anota(14); TG0_INT_CLR = 1u; }

static void configura(uint32_t src, uint32_t linea, uint32_t prio, int flanco) {
    irq_off();
    INT_MAP(src) = linea;
    INT_PRI(linea) = prio;
    if (flanco) INT_TYPE |= 1u << linea; else INT_TYPE &= ~(1u << linea);
    INT_ENABLE |= 1u << linea;
    fence();
}
static void reinicia(void) { n_orden = 0; for (int i = 0; i < 8; ++i) orden[i] = 0; }
static void deja(void) { for (volatile int i = 0; i < 50; ++i) {} }

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    INT_THRESH = 1;
    /* 1. Prioridad 0: enmascarada; EIP no la muestra */
    configura(SRC_SW0, 1, 0, 0);
    SYSTEM_FROM_CPU(0) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[0] = cuenta[1] | ((INT_EIP >> 1 & 1u) << 8);
    /* 2. Umbral: prioridad 2 con umbral 3 no entra; con umbral 2, sí */
    INT_MAP(SRC_SW0) = 2; INT_PRI(2) = 2; INT_ENABLE |= 1u << 2;
    INT_THRESH = 3; fence();
    irq_on(); deja(); irq_off();
    buzon.res[1] = cuenta[2];
    INT_THRESH = 2; fence();
    irq_on(); deja(); irq_off();
    buzon.res[2] = cuenta[2];
    INT_THRESH = 1;
    /* 3. Empate de prioridad: gana el ID más bajo (4 antes que 6) */
    reinicia();
    configura(SRC_SW0 + 1, 4, 5, 0);
    configura(SRC_SW0 + 2, 6, 5, 0);
    SYSTEM_FROM_CPU(2) = 1; SYSTEM_FROM_CPU(1) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[3] = orden[0] | (orden[1] << 8) | (n_orden << 16);
    INT_ENABLE &= ~((1u << 4) | (1u << 6));
    /* 4. Más prioridad, primero: la 9 (7) antes que la 7 (6) */
    reinicia();
    configura(SRC_SW0 + 1, 7, 6, 0);
    configura(SRC_SW0 + 2, 9, 7, 0);
    SYSTEM_FROM_CPU(1) = 1; SYSTEM_FROM_CPU(2) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[4] = orden[0] | (orden[1] << 8) | (n_orden << 16);
    INT_ENABLE &= ~((1u << 7) | (1u << 9));
    /* 5. Flanco: se engancha aunque la fuente baje; sin borrar vuelve a entrar */
    configura(SRC_SW0 + 3, 10, 4, 1);
    SYSTEM_FROM_CPU(3) = 1; SYSTEM_FROM_CPU(3) = 0;
    buzon.res[5] = (INT_EIP >> 10) & 1u;
    irq_on(); deja(); irq_off();
    buzon.res[6] = cuenta[10] | (((INT_EIP >> 10) & 1u) << 8);
    /* 6. Flanco sin reclamar y deshabilitada: el pulso de CLEAR la vacía */
    SYSTEM_FROM_CPU(3) = 1; SYSTEM_FROM_CPU(3) = 0;
    INT_ENABLE &= ~(1u << 10);
    INT_CLEAR |= 1u << 10; INT_CLEAR &= ~(1u << 10);
    INT_ENABLE |= 1u << 10; fence();
    irq_on(); deja(); irq_off();
    buzon.res[7] = cuenta[10];
    INT_ENABLE &= ~(1u << 10);
    /* 7. Nivel: la fuente de nivel no se borra con CLEAR */
    configura(SRC_SW0 + 3, 12, 3, 0);
    SYSTEM_FROM_CPU(3) = 1;
    INT_CLEAR |= 1u << 12; INT_CLEAR &= ~(1u << 12);
    buzon.res[8] = (INT_EIP >> 12) & 1u;
    irq_on(); deja(); irq_off();
    buzon.res[9] = cuenta[12];
    /* 8. SYSTIMER en modo periodo: 5 alarmas cada 100 us (1600 cuentas) */
    configura(SRC_SYSTIMER0, 13, 6, 0);
    ST_TARGET0_CONF = (1u << 30) | 1600u;
    ST_COMP0_LOAD = 1;
    ST_INT_ENA = 1;
    ST_CONF |= 1u << 24;
    irq_on();
    while (n_st < 5) __asm__ volatile("wfi");
    irq_off();
    buzon.res[10] = n_st;
    buzon.res[11] = (uint32_t)(t_st[4] - t_st[0]);
    /* 9. TIMG0 T0: alarma a 2000 tics de APB/80 (2 ms), una vez */
    configura(SRC_TG0_T0, 14, 6, 0);
    TG0_T0CONFIG = (1u << 30) | (80u << 13);
    TG0_T0LOADLO = 0; TG0_T0LOAD = 1;
    TG0_T0ALARMLO = 2000; TG0_T0ALARMHI = 0;
    TG0_INT_ENA = 1;
    uint64_t t0 = systimer_ahora();
    TG0_T0CONFIG |= (1u << 31) | (1u << 10);
    irq_on();
    while (!cuenta[14]) __asm__ volatile("wfi");
    irq_off();
    buzon.res[12] = (uint32_t)(systimer_ahora() - t0);
    buzon.res[13] = (TG0_T0CONFIG >> 10) & 1u;      /* ALARM_EN se baja sola */
    /* 10. mpccr cuenta ciclos */
    CSR_W(0x7E0, 1u); CSR_W(0x7E2, 0u); CSR_W(0x7E1, 1u);
    for (volatile int i = 0; i < 100; ++i) {}
    buzon.res[14] = CSR_R(0x7E2);
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
