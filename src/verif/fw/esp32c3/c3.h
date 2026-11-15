/* c3.h — Los registros del ESP32-C3 que usan las pruebas de E3
 * [TRM-C3 §3 tabla 3.3-3, §5, §6, §8, §10, §11, §16, §26] */
#ifndef C3_H
#define C3_H
#include <stdint.h>
#define R(a) (*(volatile uint32_t *)(a))

/* SYSTEM */
#define SYSTEM_CPU_PER_CONF      R(0x600C0008)
#define SYSTEM_SYSCLK_CONF       R(0x600C0058)
#define SYSTEM_FROM_CPU(n)       R(0x600C0028 + 4 * (n))
/* RTC_CNTL */
#define RTC_OPTIONS0             R(0x60008000)
#define RTC_RESET_STATE          R(0x60008038)
#define RTC_STORE0               R(0x60008050)
/* Matriz de interrupciones e INTC */
#define INT_MAP(src)             R(0x600C2000 + 4 * (src))
#define INT_ENABLE               R(0x600C2104)
#define INT_TYPE                 R(0x600C2108)
#define INT_CLEAR                R(0x600C210C)
#define INT_EIP                  R(0x600C2110)
#define INT_PRI(n)               R(0x600C2114 + 4 * (n))
#define INT_THRESH               R(0x600C2194)
#define SRC_GPIO 16
#define SRC_UART0 21
#define SRC_UART1 22
#define SRC_TG0_T0 32
#define SRC_SYSTIMER0 37
#define SRC_SW0 50
/* GPIO e IO_MUX */
#define GPIO_OUT                 R(0x60004004)
#define GPIO_OUT_W1TS            R(0x60004008)
#define GPIO_OUT_W1TC            R(0x6000400C)
#define GPIO_ENABLE_W1TS         R(0x60004024)
#define GPIO_ENABLE_W1TC         R(0x60004028)
#define GPIO_STRAP               R(0x60004038)
#define GPIO_IN                  R(0x6000403C)
#define GPIO_STATUS              R(0x60004044)
#define GPIO_STATUS_W1TC         R(0x6000404C)
#define GPIO_PIN(n)              R(0x60004074 + 4 * (n))
#define GPIO_FUNC_IN(s)          R(0x60004154 + 4 * (s))
#define GPIO_FUNC_OUT(n)         R(0x60004554 + 4 * (n))
#define IO_MUX(n)                R(0x60009004 + 4 * (n))
#define IOMUX_FUN_IE  (1u << 9)
#define IOMUX_FUN_WPU (1u << 8)
#define IOMUX_FUN_WPD (1u << 7)
#define IOMUX_GPIO    (1u << 12)       /* MCU_SEL = 1 */
/* UART */
#define UART_BASE(n)             ((n) ? 0x60010000u : 0x60000000u)
#define UART_FIFO(n)             R(UART_BASE(n) + 0x00)
#define UART_INT_RAW(n)          R(UART_BASE(n) + 0x04)
#define UART_INT_ST(n)           R(UART_BASE(n) + 0x08)
#define UART_INT_ENA(n)          R(UART_BASE(n) + 0x0C)
#define UART_INT_CLR(n)          R(UART_BASE(n) + 0x10)
#define UART_CLKDIV(n)           R(UART_BASE(n) + 0x14)
#define UART_STATUS(n)           R(UART_BASE(n) + 0x1C)
#define UART_CONF0(n)            R(UART_BASE(n) + 0x20)
#define UART_CONF1(n)            R(UART_BASE(n) + 0x24)
#define UART_CLK_CONF(n)         R(UART_BASE(n) + 0x78)
#define UART_ID(n)               R(UART_BASE(n) + 0x80)
/* SYSTIMER */
#define ST_CONF                  R(0x60023000)
#define ST_UNIT0_OP              R(0x60023004)
#define ST_TARGET0_HI            R(0x6002301C)
#define ST_TARGET0_LO            R(0x60023020)
#define ST_TARGET0_CONF          R(0x60023034)
#define ST_UNIT0_VALUE_HI        R(0x60023040)
#define ST_UNIT0_VALUE_LO        R(0x60023044)
#define ST_COMP0_LOAD            R(0x60023050)
#define ST_INT_ENA               R(0x60023064)
#define ST_INT_RAW               R(0x60023068)
#define ST_INT_CLR               R(0x6002306C)
/* TIMG0 */
#define TG0_T0CONFIG             R(0x6001F000)
#define TG0_T0LO                 R(0x6001F004)
#define TG0_T0UPDATE             R(0x6001F00C)
#define TG0_T0ALARMLO            R(0x6001F010)
#define TG0_T0ALARMHI            R(0x6001F014)
#define TG0_T0LOADLO             R(0x6001F018)
#define TG0_T0LOAD               R(0x6001F020)
#define TG0_INT_ENA              R(0x6001F070)
#define TG0_INT_RAW              R(0x6001F074)
#define TG0_INT_CLR              R(0x6001F07C)

/* El buzón: lo que el banco lee al acabar */
#define BUZON_MAGIC 0xC3C3B00Cu
typedef struct { volatile uint32_t magic, fin, res[30]; } buzon_t;
extern buzon_t buzon;

static inline uint64_t systimer_ahora(void) {
    ST_UNIT0_OP = 1u << 30;
    while (!(ST_UNIT0_OP & (1u << 29))) {}
    uint32_t hi = ST_UNIT0_VALUE_HI, lo = ST_UNIT0_VALUE_LO;
    return ((uint64_t)hi << 32) | lo;
}
static inline void espera_us(uint32_t us) {
    uint64_t f = systimer_ahora() + (uint64_t)us * 16u;
    while (systimer_ahora() < f) {}
}
/* El PLL a 480 MHz, la CPU a 160 y el APB a 80 */
static inline void reloj_160(void) {
    SYSTEM_CPU_PER_CONF = (SYSTEM_CPU_PER_CONF & ~3u) | (1u << 2) | 1u;
    SYSTEM_SYSCLK_CONF = (SYSTEM_SYSCLK_CONF & ~(3u << 10)) | (1u << 10);
}
/* 115200 8N1 con SCLK = APB (80 MHz) */
static inline void uart_init(int n) {
    UART_CLK_CONF(n) = (UART_CLK_CONF(n) & ~((3u << 20) | (0xFFu << 12))) | (1u << 20);
    UART_CLKDIV(n) = 694u | (7u << 20);           /* 80e6 / 694,4375 = 115204 */
    UART_CONF0(n) = (3u << 2) | (1u << 4);        /* 8 bits, 1 de parada */
    UART_CONF0(n) |= (1u << 17) | (1u << 18);
    UART_CONF0(n) &= ~((1u << 17) | (1u << 18));
    UART_ID(n) |= 1u << 31;                       /* REG_UPDATE */
    while (UART_ID(n) & (1u << 31)) {}
}
static inline void uart_putc(int n, char c) {
    while (((UART_STATUS(n) >> 16) & 0x3FFu) >= 126) {}
    UART_FIFO(n) = (uint8_t)c;
}
static inline void uart_puts(int n, const char *s) { while (*s) uart_putc(n, *s++); }
/* Hasta que salga el último carácter: la FIFO vacía y una trama más */
static inline void uart_flush(int n) {
    while ((UART_STATUS(n) >> 16) & 0x3FFu) {}
    espera_us(100);
}
#define CSR_W(csr, v) __asm__ volatile("csrw " #csr ", %0" :: "r"(v))
#define CSR_R(csr) ({ uint32_t _v; __asm__ volatile("csrr %0, " #csr : "=r"(_v)); _v; })
static inline void irq_on(void)  { __asm__ volatile("csrsi mstatus, 8"); }
static inline void irq_off(void) { __asm__ volatile("csrci mstatus, 8"); }
static inline void fence(void)   { __asm__ volatile("fence"); }
#endif
