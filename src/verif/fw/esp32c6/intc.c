/* intc.c — E9: el INTPRI y el CLINT del C6 [TRM-C6 §1.6, §1.7] y la
 * delegación de una interrupción a U [§1.6.1, la extensión N].
 *
 * Usa las cuatro interrupciones por software del INTPRI
 * (INTPRI_CPU_INTR_FROM_CPU_0-3, fuentes 22-25 de la matriz), que se
 * levantan y bajan escribiendo un registro. Cada prueba deja su resultado en
 * buzon.res[k]; el banco los comprueba. mideleg sale del reset con 0x111: la
 * línea 8 es de U, así que aquí no se usa en M. */
#include "c6.h"
static volatile uint32_t orden[8], n_orden, cuenta[32], ucausa, n_u;
static volatile uint64_t t_mti;
static volatile uint32_t n_exc, causa_exc;
static volatile uint32_t zona[64] __attribute__((aligned(256)));

static void anota(uint32_t id) { if (n_orden < 8) orden[n_orden++] = id; ++cuenta[id]; }
/* Las de software: se bajan en la fuente (son de nivel) */
void isr_1(void)  { anota(1);  INT_FROM_CPU(0) = 0; }
void isr_2(void)  { anota(2);  INT_FROM_CPU(0) = 0; }
void isr_9(void)  { anota(9);  INT_FROM_CPU(1) = 0; }
void isr_10(void) { anota(10); INT_FROM_CPU(2) = 0; }
void isr_11(void) { anota(11); INT_FROM_CPU(1) = 0; }
void isr_12(void) { anota(12); INT_FROM_CPU(2) = 0; }
void isr_13(void) { anota(13); INT_FROM_CPU(3) = 0; }
void isr_15(void) {
    /* De flanco: la primera vez NO se borra (vuelve a entrar), la segunda se
     * borra con el pulso en CPU_INT_CLEAR */
    anota(15);
    if (cuenta[15] >= 2) { INT_CLEAR |= 1u << 15; INT_CLEAR &= ~(1u << 15); }
}
void isr_16(void) { anota(16); INT_FROM_CPU(3) = 0; }
/* El CLINT: el MSI (3) y el MTI (7) */
void isr_3(void)  { anota(3); CLINT_MSIP = 0; }
void isr_7(void)  {
    anota(7);
    t_mti = ((uint64_t)CLINT_MTIME_HI << 32) | CLINT_MTIME_LO;
    CLINT_MTIMECTL = 1u;                       /* MTCE, sin MTIE */
}
/* Las excepciones: la ecall de U vuelve a M en la instrucción siguiente */
void excepcion(void) {
    uint32_t c = CSR_R(mcause);
    if (c == 8) {
        CSR_W(mepc, CSR_R(mepc) + 4);
        CSR_S(mstatus, 0x1800u);               /* MPP = M */
        return;
    }
    /* Los fallos de acceso que provoca la prueba de la PMA: se anotan y se
     * salta la instrucción (2 o 4 bytes) */
    if (c == 5 || c == 7) {
        uint32_t pc = CSR_R(mepc);
        uint16_t ins = *(volatile uint16_t *)pc;
        CSR_W(mepc, pc + ((ins & 3u) == 3u ? 4u : 2u));
        causa_exc = c;
        ++n_exc;
        return;
    }
    buzon.res[29] = c;
    for (;;) {}
}
/* La de U, por utvec: la línea 14 delegada */
void isr_u14(void) {
    ucausa = CSR_R(0x042);                      /* ucause */
    ++n_u;
    INT_FROM_CPU(0) = 0;
}
/* Vuelve en U a quien la llama (mret con MPP = U) */
void a_u(void);
__asm__(
    "    .text\n"
    "    .globl a_u\n"
    "a_u:\n"
    "    li    t1, 0x1800\n"
    "    csrc  mstatus, t1\n"
    "    csrw  mepc, ra\n"
    "    mret\n"
    /* La tabla de U: excepciones en la base (no se usan), la 14 en +56 */
    "    .section .vectores_u, \"ax\", @progbits\n"
    "    .option push\n"
    "    .option norvc\n"
    "    .balign 256\n"
    "    .globl vectores_u\n"
    "vectores_u:\n"
    "    j     vectores_u\n"
    "    .rept 13\n"
    "    j     vectores_u\n"
    "    .endr\n"
    "    j     ent_u14\n"
    "    .option pop\n"
    "    .text\n"
    "ent_u14:\n"
    "    addi  sp, sp, -64\n"
    "    sw    ra, 0(sp)\n"
    "    sw    t0, 4(sp)\n"
    "    sw    t1, 8(sp)\n"
    "    sw    t2, 12(sp)\n"
    "    sw    a0, 16(sp)\n"
    "    sw    a1, 20(sp)\n"
    "    sw    a2, 24(sp)\n"
    "    sw    a3, 28(sp)\n"
    "    sw    a4, 32(sp)\n"
    "    sw    a5, 36(sp)\n"
    "    call  isr_u14\n"
    "    lw    ra, 0(sp)\n"
    "    lw    t0, 4(sp)\n"
    "    lw    t1, 8(sp)\n"
    "    lw    t2, 12(sp)\n"
    "    lw    a0, 16(sp)\n"
    "    lw    a1, 20(sp)\n"
    "    lw    a2, 24(sp)\n"
    "    lw    a3, 28(sp)\n"
    "    lw    a4, 32(sp)\n"
    "    lw    a5, 36(sp)\n"
    "    addi  sp, sp, 64\n"
    "    uret\n");
extern char vectores_u[];

static void configura(uint32_t src, uint32_t linea, uint32_t prio, int flanco) {
    irq_off();
    INT_MAP(src) = linea;
    INT_PRI(linea) = prio;
    if (flanco) INT_TYPE |= 1u << linea; else INT_TYPE &= ~(1u << linea);
    INT_ENABLE |= 1u << linea;
    CSR_S(mie, 1u << linea);
    fence();
}
static void reinicia(void) { n_orden = 0; for (int i = 0; i < 8; ++i) orden[i] = 0; }
static void deja(void) { for (volatile int i = 0; i < 50; ++i) {} }
static uint64_t mtime(void) { return ((uint64_t)CLINT_MTIME_HI << 32) | CLINT_MTIME_LO; }

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    buzon.res[20] = CSR_R(mideleg);
    INT_THRESH = 1;
    /* 1. Prioridad 0 con umbral 1: enmascarada; EIP no la muestra */
    configura(SRC_FROM_CPU0, 1, 0, 0);
    INT_FROM_CPU(0) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[0] = cuenta[1] | ((INT_EIP >> 1 & 1u) << 8);
    /* 2. Umbral: prioridad 2 con umbral 3 no entra; con umbral 2, sí */
    INT_MAP(SRC_FROM_CPU0) = 2; INT_PRI(2) = 2; INT_ENABLE |= 1u << 2; CSR_S(mie, 1u << 2);
    INT_THRESH = 3; fence();
    irq_on(); deja(); irq_off();
    buzon.res[1] = cuenta[2];
    INT_THRESH = 2; fence();
    irq_on(); deja(); irq_off();
    buzon.res[2] = cuenta[2];
    INT_THRESH = 1;
    INT_ENABLE &= ~((1u << 1) | (1u << 2));
    /* 3. Empate de prioridad: gana el ID más bajo (9 antes que 10) */
    reinicia();
    configura(SRC_FROM_CPU0 + 1, 9, 5, 0);
    configura(SRC_FROM_CPU0 + 2, 10, 5, 0);
    INT_FROM_CPU(2) = 1; INT_FROM_CPU(1) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[3] = orden[0] | (orden[1] << 8) | (n_orden << 16);
    INT_ENABLE &= ~((1u << 9) | (1u << 10));
    /* 4. Más prioridad, primero: la 12 (7) antes que la 11 (6) */
    reinicia();
    configura(SRC_FROM_CPU0 + 1, 11, 6, 0);
    configura(SRC_FROM_CPU0 + 2, 12, 7, 0);
    INT_FROM_CPU(1) = 1; INT_FROM_CPU(2) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[4] = orden[0] | (orden[1] << 8) | (n_orden << 16);
    INT_ENABLE &= ~((1u << 11) | (1u << 12));
    /* 5. mie: con su bit a 0 no entra, aunque mip la muestre */
    configura(SRC_FROM_CPU0 + 3, 13, 4, 0);
    CSR_C(mie, 1u << 13);
    INT_FROM_CPU(3) = 1;
    irq_on(); deja(); irq_off();
    buzon.res[5] = cuenta[13] | (((CSR_R(mip) >> 13) & 1u) << 8);
    CSR_S(mie, 1u << 13);
    irq_on(); deja(); irq_off();
    buzon.res[6] = cuenta[13];
    INT_ENABLE &= ~(1u << 13);
    /* 6. Flanco: se engancha aunque la fuente baje; sin borrar vuelve a entrar */
    configura(SRC_FROM_CPU0 + 3, 15, 4, 1);
    INT_FROM_CPU(3) = 1; INT_FROM_CPU(3) = 0;
    buzon.res[7] = (INT_EIP >> 15) & 1u;
    irq_on(); deja(); irq_off();
    buzon.res[8] = cuenta[15] | (((INT_EIP >> 15) & 1u) << 8);
    /* 7. Nivel: no se borra con CLEAR */
    configura(SRC_FROM_CPU0 + 3, 16, 3, 0);
    INT_FROM_CPU(3) = 1;
    INT_CLEAR |= 1u << 16; INT_CLEAR &= ~(1u << 16);
    buzon.res[9] = (INT_EIP >> 16) & 1u;
    irq_on(); deja(); irq_off();
    buzon.res[10] = cuenta[16];
    INT_ENABLE &= ~((1u << 15) | (1u << 16));
    /* 8. El CLINT: MTI a 1000 cuentas de MTIME (MTCE y MTIE), y MSI */
    CSR_S(mie, (1u << 7) | (1u << 3));
    CLINT_MTIMECTL = 1u;
    uint64_t t0 = mtime();
    CLINT_MTIMECMP_HI = (uint32_t)((t0 + 1000) >> 32);
    CLINT_MTIMECMP_LO = (uint32_t)(t0 + 1000);
    CLINT_MTIMECTL = 3u;
    irq_on();
    while (!cuenta[7]) __asm__ volatile("wfi");
    irq_off();
    buzon.res[11] = (uint32_t)(t_mti - t0);
    /* El umbral no enmascara las del CLINT: MSI con umbral 15 */
    INT_THRESH = 15; fence();
    CLINT_MSIP = 1;
    irq_on(); deja(); irq_off();
    buzon.res[12] = cuenta[3];
    INT_THRESH = 1;
    /* 9. Delegada a U: la línea 14, con utvec, uie y ustatus.UIE; uret */
    CSR_W(pmpaddr15, 0xFFFFFFFFu);
    CSR_W(pmpcfg3, 0x1F000000u);               /* NAPOT, RWX: todo para U */
    configura(SRC_FROM_CPU0, 14, 5, 0);
    CSR_S(mideleg, 1u << 14);
    CSR_W(0x005, (uint32_t)vectores_u | 1u);   /* utvec, vectorizado */
    CSR_S(0x000, 1u);                          /* ustatus.UIE */
    INT_FROM_CPU(0) = 1;
    fence();
    irq_on(); deja();                          /* en M no entra: es de U */
    buzon.res[13] = n_u | (((CSR_R(0x044) >> 14) & 1u) << 8);    /* uip */
    a_u();                                     /* a U: entra en el acto */
    deja();
    __asm__ volatile("ecall");                 /* y de vuelta a M */
    irq_off();
    buzon.res[14] = n_u;
    buzon.res[15] = ucausa;
    buzon.res[16] = cuenta[14];
    /* 10. mpccr cuenta ciclos */
    CSR_W(0x7E0, 1u); CSR_W(0x7E2, 0u); CSR_W(0x7E1, 1u);
    for (volatile int i = 0; i < 100; ++i) {}
    buzon.res[17] = CSR_R(0x7E2);
    /* 11. La PMA [TRM-C6 §1.9]: de reset, la palabra 0 no es válida (NA4,
     *     TYPE 0): leer un puntero nulo da el fallo de carga (5) */
    n_exc = 0;
    buzon.res[18] = CSR_R(0xBC0);
    { uint32_t v; __asm__ volatile("lw %0, 0(zero)" : "=r"(v)); (void)v; }   /* sin la UB de C */
    buzon.res[19] = n_exc | (causa_exc << 8);
    /*     La entrada 1, NAPOT sobre `zona` (256 bytes), válida y solo de
     *     lectura: escribir da el fallo de almacenamiento (7), leer no */
    n_exc = 0; causa_exc = 0;
    CSR_W(0xBD1, ((uint32_t)zona >> 2) | 0x1Fu);
    CSR_W(0xBC1, (3u << 30) | (1u << 4) | 1u);
    zona[3] = 0x1234u;
    uint32_t leido = zona[3];
    buzon.res[21] = n_exc | (causa_exc << 8) | (leido << 16);
    CSR_W(0xBC1, 0u);
    zona[3] = 0x5678u;
    buzon.res[22] = zona[3];
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
