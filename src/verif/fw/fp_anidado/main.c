/* ===========================================================================
 * main.c — Una interrupción anidada no puede borrar los registros FP del hilo
 *          principal (el contexto FP diferido de las excepciones)
 *
 * La prueba de regresión de un fallo del núcleo que destapó el firmware de la
 * fresadora (verif/fw/cnc, fase F3 de doc/analisis_cnc.md): al entrar en una
 * excepción, el modelo no ponía CONTROL.FPCA a cero [ARMv7-M, B1.5.6,
 * ExceptionTaken]. Una interrupción que se anidaba en un manejador SIN FP abría
 * entonces otra trama FP diferida encima de la de fuera; su vuelta borraba
 * LSPACT, y la vuelta de fuera leía S0-S15 de una trama que nunca se había
 * escrito. El hilo principal perdía sus registros FP, a ratos.
 *
 * Lo que hace, cien veces: carga S0-S15 con valores conocidos, pide la
 * interrupción de fuera (EXTI0, prioridad 2), cuyo manejador -sin FP- pide la
 * de dentro (EXTI1, prioridad 1), que se anida; y a la vuelta mira que S0-S15
 * sigan igual. Dice el resultado por la USART2 (el VCP, 115200 8N1, con el
 * HSI de 16 MHz):
 *
 *     FP anidado: 100 de 100 bien
 *
 * La comprueba `make gui-cnc` (C6).
 * ===========================================================================*/
#include "stm32f446xx.h"
#include <stdint.h>

static volatile uint32_t g_fuera, g_dentro;

void EXTI0_IRQHandler(void)
{
    ++g_fuera;
    NVIC_SetPendingIRQ(EXTI1_IRQn);     /* la de dentro se anida aquí mismo */
    __DSB();
    __ISB();
}
void EXTI1_IRQHandler(void) { ++g_dentro; }

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

int main(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
    GPIOA->MODER = (GPIOA->MODER & ~(0xFu << 4)) | (0xAu << 4);   /* PA2 y PA3, AF */
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFFu << 8)) | (0x77u << 8);
    USART2->BRR = 139;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;

    NVIC_SetPriority(EXTI0_IRQn, 2);
    NVIC_SetPriority(EXTI1_IRQn, 1);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);

    static float antes[16], despues[16];
    uint32_t bien = 0;
    for (uint32_t n = 0; n < 100u; ++n) {
        for (int k = 0; k < 16; ++k) antes[k] = (float)(n * 16u + (uint32_t)k) + 0.5f;
        __asm__ volatile(
            "vldmia %[a], {s0-s15}\n\t"
            "str %[uno], [%[ispr]]\n\t"     /* NVIC->ISPR[0] = EXTI0: la de fuera */
            "dsb\n\t"
            "isb\n\t"
            "nop\n\t"
            "nop\n\t"
            "vstmia %[d], {s0-s15}\n\t"
            :
            : [a] "r"(antes), [d] "r"(despues), [uno] "r"(1u << EXTI0_IRQn),
              [ispr] "r"(&NVIC->ISPR[0])
            : "memory", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10",
              "s11", "s12", "s13", "s14", "s15");
        int ok = 1;
        for (int k = 0; k < 16; ++k) if (despues[k] != antes[k]) ok = 0;
        bien += (uint32_t)ok;
    }
    envia("FP anidado: ");
    envia_n(bien);
    envia(" de 100 bien (");
    envia_n(g_fuera);
    envia(" fuera, ");
    envia_n(g_dentro);
    envia(" dentro)\r\n");
    for (;;) __WFI();
}

void __libc_init_array(void) { }
