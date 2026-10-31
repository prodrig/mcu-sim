/* ===========================================================================
 * main.c — Las sumas y restas paralelas (SIMD) y sus banderas GE, con SEL,
 *          y el strlen de newlib que las usa
 *
 * La prueba de regresión de un fallo del núcleo que destapó grblHAL en la
 * fresadora (fase F3 de doc/analisis_cnc.md): UADD8 ponía GE[i] cuando la suma
 * del byte era >= 0, cosa que una suma sin signo cumple siempre, en vez de
 * cuando hay acarreo, >= 0x100 [ARMv7-M, A7.7.176]; UASX y USAX, igual en su
 * mitad que suma. El strlen de newlib para ARMv7-M busca el cero del final con
 * `uadd8` y `sel`: con GE siempre a uno no lo veía nunca, seguía leyendo hasta
 * el final de la RAM y acababa en un fallo de bus.
 *
 * Lo que hace: las doce operaciones con GE (U/S ADD8, SUB8, ADD16, SUB16, ASX
 * y SAX) sobre seis pares de operandos, comparando el resultado y GE -leído
 * con SEL y con MRS APSR- con la tabla de casos.h, que sale del pseudocódigo
 * de la arquitectura; y strlen sobre cadenas de 0 a 40 caracteres con todas
 * las alineaciones. Dice el resultado por la USART2 (el VCP, 115200 8N1, con
 * el HSI de 16 MHz):
 *
 *     SIMD GE: 72 de 72 bien
 *     strlen: 164 de 164 bien
 *
 * Sin el arreglo, la primera línea dice los que fallan y la segunda no llega:
 * strlen acaba en un fallo de bus.
 *
 * La comprueba `make gui-cnc` (C7).
 * ===========================================================================*/
#include "stm32f446xx.h"
#include <stdint.h>
#include <string.h>
#include "casos.h"

/* El startup de ST llama a __libc_init_array, que con newlib enlazada (por
 * strlen) pide _init: aquí no hay nada que construir */
void _init(void) {}

static void envia(const char* s)
{
    while (*s) {
        while (!(USART2->SR & USART_SR_TXE)) { }
        USART2->DR = (uint8_t)*s++;
    }
}
static void envia_n(uint32_t v)
{
    char b[12];
    int i = 0;
    do { b[i++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (i) { char c[2] = {b[--i], 0}; envia(c); }
}

/* Una operación: el resultado, GE por SEL (0xFF en los bytes con GE) y GE
 * por el APSR */
#define PRUEBA(op, a, b, r, g)                                                 \
    {                                                                          \
        uint32_t res, sel, apsr;                                               \
        __asm__ volatile(#op " %0, %3, %4\n\t"                                 \
                         "sel %1, %5, %6\n\t"                                  \
                         "mrs %2, apsr\n\t"                                    \
                         : "=&r"(res), "=&r"(sel), "=&r"(apsr)                 \
                         : "r"(a), "r"(b), "r"(0xFFFFFFFFu), "r"(0u));         \
        uint32_t esp_sel = 0;                                                  \
        for (int i = 0; i < 4; ++i) if ((g) & (1u << i)) esp_sel |= 0xFFu << (8 * i); \
        ++total;                                                               \
        if (res == (r) && sel == esp_sel && ((apsr >> 16) & 0xFu) == (g)) ++bien; \
    }

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    GPIOA->MODER = (GPIOA->MODER & ~(0xFu << 4)) | (0xAu << 4);   /* PA2 y PA3, AF */
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFFu << 8)) | (0x77u << 8);
    USART2->BRR = 139;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;

    uint32_t bien = 0, total = 0;
    CASOS(PRUEBA)

    envia("SIMD GE: ");
    envia_n(bien);
    envia(" de ");
    envia_n(total);
    envia(" bien\r\n");

    /* strlen con 0 a 40 caracteres y las cuatro alineaciones, con basura
     * distinta de cero detrás del cero */
    static char buf[64] __attribute__((aligned(8)));
    uint32_t s_bien = 0, s_total = 0;
    for (uint32_t al = 0; al < 4u; ++al)
        for (uint32_t n = 0; n <= 40u; ++n) {
            for (uint32_t k = 0; k < sizeof buf; ++k) buf[k] = (char)('A' + k % 26u);
            buf[al + n] = 0;
            ++s_total;
            if (strlen(buf + al) == n) ++s_bien;
        }

    envia("strlen: ");
    envia_n(s_bien);
    envia(" de ");
    envia_n(s_total);
    envia(" bien\r\n");
    for (;;) { }
}
