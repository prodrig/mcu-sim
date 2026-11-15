/* rsa.c — E8: el acelerador RSA con una clave RSA-1024 (rsa_vec.h, de
 * gen_rsa.py: los resultados los calcula Python, no el modelo).
 *   res[0]  bits de lo que sale bien:
 *           0 la firma S = X^d mod M (sin acelerar, tiempo constante)
 *           1 la comprobación S^e mod M = X (SEARCH en el bit 16 y
 *             CONSTANT_TIME a 0)
 *           2 A·B mod M (MODMULT)          3 P1·P2 (MULT, 512 x 512)
 *           4 tras MODEXP, M, Y, M' y MODE siguen como estaban
 *           5 IDLE vale 0 justo después de arrancar
 *           6 la fuente 47 del INTC sube al acabar y baja con CLEAR_INTERRUPT
 *           7 con INTERRUPT_ENA a 0 la fuente no sube
 *   res[1..3]  lo que tardan la firma, la comprobación y MODMULT, en tics del
 *              SYSTIMER (16 MHz)
 *   res[4]  RSA_DATE              res[5]  RSA_CLEAN
 *   res[6]  la palabra baja de la firma */
#include "c3.h"
#include "rsa_vec.h"
#define RSA(o)   R(0x6003C000u + (o))
#define M_MEM    0x000u
#define Z_MEM    0x200u
#define Y_MEM    0x400u
#define X_MEM    0x600u
#define INT_STATUS1 R(0x600C20FCu)
#define SRC_RSA_BIT (1u << (47 - 32))

static void pon(uint32_t off, const uint32_t *v, unsigned n) { for (unsigned i = 0; i < n; ++i) RSA(off + 4 * i) = v[i]; }
static int igual(uint32_t off, const uint32_t *v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) if (RSA(off + 4 * i) != v[i]) return 0;
    return 1;
}
static uint32_t ciclos(void) { return (uint32_t)systimer_ahora(); }   /* 16 MHz */
/* Arranca y espera; devuelve los tics. *ocupado: IDLE justo después */
static uint32_t lanza(uint32_t reg, int *ocupado) {
    uint32_t t0 = ciclos();
    RSA(reg) = 1;
    if (ocupado) *ocupado = RSA(0x818) == 0;
    while (!RSA(0x818)) {}
    return ciclos() - t0;
}

int main(void) {
    reloj_160();
    buzon.magic = BUZON_MAGIC;
    uint32_t ok = 0;
    buzon.res[4] = RSA(0x830);
    buzon.res[5] = RSA(0x808);
    /* --- La firma: S = X^d mod M, sin acelerar --- */
    RSA(0x804) = RSA_N - 1;
    RSA(0x800) = RSA_MP;
    pon(M_MEM, RSA_M, RSA_N);
    pon(Y_MEM, RSA_D, RSA_N);
    pon(X_MEM, RSA_X, RSA_N);
    pon(Z_MEM, RSA_R, RSA_N);
    int ocup = 0;
    buzon.res[1] = lanza(0x80C, &ocup);
    if (ocup) ok |= 1u << 5;
    if (igual(Z_MEM, RSA_S, RSA_N)) ok |= 1u << 0;
    buzon.res[6] = RSA(Z_MEM);
    if ((INT_STATUS1 & SRC_RSA_BIT)) {
        RSA(0x81C) = 1;
        if (!(INT_STATUS1 & SRC_RSA_BIT)) ok |= 1u << 6;
    }
    if (igual(M_MEM, RSA_M, RSA_N) && igual(Y_MEM, RSA_D, RSA_N) && RSA(0x800) == RSA_MP && RSA(0x804) == RSA_N - 1)
        ok |= 1u << 4;
    /* --- La comprobación: S^e mod M, con SEARCH (bit 16) y CONSTANT_TIME a 0;
     *     sin interrupción --- */
    RSA(0x82C) = 0;
    pon(Y_MEM, RSA_E, RSA_N);
    pon(X_MEM, RSA_S, RSA_N);
    pon(Z_MEM, RSA_R, RSA_N);
    RSA(0x824) = 1;
    RSA(0x828) = 16;
    RSA(0x820) = 0;
    buzon.res[2] = lanza(0x80C, 0);
    if (igual(Z_MEM, RSA_X, RSA_N)) ok |= 1u << 1;
    if (!(INT_STATUS1 & SRC_RSA_BIT)) ok |= 1u << 7;
    RSA(0x81C) = 1;
    RSA(0x82C) = 1;
    RSA(0x824) = 0;
    RSA(0x820) = 1;
    /* --- MODMULT: A·B mod M --- */
    pon(X_MEM, RSA_A, RSA_N);
    pon(Y_MEM, RSA_B, RSA_N);
    pon(Z_MEM, RSA_R, RSA_N);
    buzon.res[3] = lanza(0x810, 0);
    if (igual(Z_MEM, RSA_AB, RSA_N)) ok |= 1u << 2;
    RSA(0x81C) = 1;
    /* --- MULT: P1·P2, 512 x 512 bits: MODE = 2n - 1, P2 en Z_MEM[n..] --- */
    RSA(0x804) = 2 * 16 - 1;
    pon(X_MEM, RSA_P1, 16);
    pon(Z_MEM + 4 * 16, RSA_P2, 16);
    lanza(0x814, 0);
    if (igual(Z_MEM, RSA_PR, 32)) ok |= 1u << 3;
    RSA(0x81C) = 1;
    buzon.res[0] = ok;
    buzon.fin = 1;
    for (;;) __asm__ volatile("wfi");
}
