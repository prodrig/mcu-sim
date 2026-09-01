/* ===========================================================================
 * main.c — Firmware de demostración del ADC, compilado con CMSIS
 *
 * Usa la cabecera de dispositivo de ST (stm32f407xx.h) y el CMSIS-Core sin
 * ninguna adaptación al modelo: el mismo código que se grabaría en la placa.
 *
 * El montaje es el de cualquier tarjeta con dos sensores analógicos colgados
 * de PA1 (ADC123_IN1) y PA2 (ADC123_IN2). El firmware:
 *
 *   1. Sube el reloj a 168 MHz con el PLL sobre el HSE de 8 MHz, dejando
 *      PCLK2 = 84 MHz (APB2 = HCLK/2).
 *   2. Programa ADCPRE = /4, es decir ADCCLK = 21 MHz, por debajo del máximo
 *      de 36 MHz que admite el bloque.
 *   3. Pone PA1 y PA2 en MODO ANALÓGICO. Es lo que desconecta el buffer de
 *      entrada, el trigger de Schmitt y las resistencias de pull: si se
 *      olvidara, el pad cargaría el nodo y la medida saldría falseada, igual
 *      que en la placa.
 *   4. Convierte los dos canales de uno en uno y pasa el primero a milivoltios
 *      con la regla de tres del manual: mV = codigo * VREF / 4095.
 *   5. Mide además la referencia interna VREFINT, que SOLO existe en el ADC1
 *      (canal 17) y solo si se conecta con TSVREFE en el registro común.
 *
 * El buzón en 0x2000 0000 permite al banco de pruebas comprobar el resultado.
 * ===========================================================================*/
#include "stm32f407xx.h"

#define VREF_MV   3300u        /* VREF+ de la placa, en milivoltios          */
#define TMO       200000u      /* vueltas máximas de espera de una bandera   */

volatile struct {
    volatile uint32_t done;    /* 1 cuando el firmware ha terminado          */
    volatile uint32_t in1;     /* código convertido del canal 1 (PA1)        */
    volatile uint32_t in2;     /* código convertido del canal 2 (PA2)        */
    volatile uint32_t mv;      /* el canal 1 pasado a milivoltios            */
    volatile uint32_t vrefint; /* código de la referencia interna (canal 17)  */
    volatile uint32_t pclk2;   /* PCLK2 con el que ha trabajado              */
} mbox __attribute__((section(".mailbox")));

static void clock_init(void)
{
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
                 FLASH_ACR_LATENCY_5WS;
    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) { }
    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
    RCC->PLLCFGR = (8u << RCC_PLLCFGR_PLLM_Pos) | (336u << RCC_PLLCFGR_PLLN_Pos) |
                   (0u << RCC_PLLCFGR_PLLP_Pos) | (7u << RCC_PLLCFGR_PLLQ_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SystemCoreClockUpdate();
}

/* Pin en MODO ANALÓGICO: MODER = 11 y sin pull. Es lo que hace que el pad se
 * aparte del nodo y el ADC vea la tensión del sensor y no la del MCU. */
static void pin_analog(GPIO_TypeDef *g, uint32_t pin)
{
    g->MODER |= (3u << (2 * pin));
    g->PUPDR &= ~(3u << (2 * pin));
}

/* Tiempo de muestreo de un canal: 3 bits por canal, SMPR2 para 0..9 y SMPR1
 * para 10..18 [IR, §12.13-D]. */
static void adc_smp(uint32_t ch, uint32_t smp)
{
    if (ch < 10u) ADC1->SMPR2 = (ADC1->SMPR2 & ~(7u << (3 * ch))) |
                                (smp << (3 * ch));
    else          ADC1->SMPR1 = (ADC1->SMPR1 & ~(7u << (3 * (ch - 10u)))) |
                                (smp << (3 * (ch - 10u)));
}

/* Una conversión suelta del canal indicado. Devuelve el código, o 0xFFFFFFFF
 * si el fin de conversión no llega. */
static uint32_t adc_read(uint32_t ch)
{
    ADC1->SQR1 = 0u;                     /* L = 0: un solo rango             */
    ADC1->SQR3 = ch;                     /* SQ1 = canal pedido               */
    adc_smp(ch, 7u);                     /* 480 ciclos: lo más lento y seguro */
    ADC1->SR  = 0u;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    for (uint32_t i = 0; i < TMO; ++i)
        if (ADC1->SR & ADC_SR_EOC) return ADC1->DR;   /* leer DR borra EOC   */
    return 0xFFFFFFFFu;
}

int main(void)
{
    mbox.done = 0u; mbox.in1 = 0u; mbox.in2 = 0u;
    mbox.mv = 0u; mbox.vrefint = 0u; mbox.pclk2 = 0u;

    clock_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    pin_analog(GPIOA, 1u);               /* PA1 = ADC123_IN1 */
    pin_analog(GPIOA, 2u);               /* PA2 = ADC123_IN2 */

    const uint32_t pclk2 = SystemCoreClock / 2u;        /* APB2 = HCLK/2 */
    mbox.pclk2 = pclk2;

    /* Registro COMÚN a los tres convertidores: prescalador del ADCCLK y el
     * interruptor que conecta las entradas internas del ADC1 [IR, §12.13-E].
     * ADCCLK = PCLK2 / 4 = 21 MHz, por debajo de los 36 MHz del limite. */
    ADC->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;

    ADC1->CR1 = 0u;                      /* 12 bits, sin scan, sin watchdog  */
    ADC1->CR2 = ADC_CR2_ADON;            /* despertar del power-down         */
    for (volatile uint32_t i = 0; i < 2000u; ++i) { }   /* t_STAB */

    mbox.in1 = adc_read(1u);
    mbox.in2 = adc_read(2u);
    mbox.vrefint = adc_read(17u);        /* solo el ADC1 tiene este canal    */

    /* Del código a milivoltios, como en cualquier driver de verdad */
    if (mbox.in1 != 0xFFFFFFFFu)
        mbox.mv = (mbox.in1 * VREF_MV) / 4095u;

    ADC1->CR2 = 0u;                      /* de vuelta al power-down          */

    mbox.done = 1u;
    for (;;) { __WFI(); }
}

/* Con -nostdlib no hay biblioteca C: el startup de ST llama a los
 * constructores estáticos, que en C no existen. */
void __libc_init_array(void) { }
