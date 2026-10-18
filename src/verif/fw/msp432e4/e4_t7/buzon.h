/* buzon.h — el buzon de e4_t7 (en 0x2000_0000), compartido por main.c y can_e4.c */
#ifndef E4_T7_BUZON_H
#define E4_T7_BUZON_H
#include <stdint.h>

typedef struct {
    volatile uint32_t modo;          /* 0x00 */
    volatile uint32_t arranques;     /* 0x04 */
    volatile uint32_t estado;        /* 0x08  100: listo para ordenes */
    volatile uint32_t sysclk;        /* 0x0C */
    volatile uint32_t orden;         /* 0x10  la sube el banco */
    volatile uint32_t hecho;         /* 0x14  el firmware la iguala al acabar */
    volatile uint32_t op;            /* 0x18 */
    volatile uint32_t cfg;           /* 0x1C  CTRL de AES/DES, MODE de SHA, CRCCTRL */
    volatile uint32_t nclave;        /* 0x20  bytes */
    volatile uint32_t nent;          /* 0x24  bytes */
    volatile uint32_t naad;          /* 0x28  bytes */
    volatile uint32_t res;           /* 0x2C  el CRC */
    volatile uint32_t r[4];          /* 0x30  para lo que haga falta */
    uint32_t clave[64];              /* 0x40  hasta 256 bytes */
    uint32_t iv[4];                  /* 0x140 */
    uint32_t tag[4];                 /* 0x150 */
    uint32_t ivsal[4];               /* 0x160 */
    uint32_t pp[16];                 /* 0x170 la clave HMAC procesada */
    uint32_t aad[32];                /* 0x1B0 */
    uint32_t ent[256];               /* 0x230 hasta 1 KB */
    uint32_t sal[256];               /* 0x630 */
} buzon_t;

extern buzon_t buzon;
extern uint32_t g_clk;

void habilita(uint32_t periferico);
void can_local(void);       /* modo 1 */
void can_nodo(int b);       /* modos 2 (nodo A) y 3 (nodo B) */

#endif
