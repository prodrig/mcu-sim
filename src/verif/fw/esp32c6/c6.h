/* c6.h — Los registros del ESP32-C6 que usan las pruebas de E9
 * [TRM-C6 §1, §7, §8, §10, §12, §14, §27; reg_base.h de ESP-IDF] */
#ifndef C6_H
#define C6_H
#include <stdint.h>
#define R(a) (*(volatile uint32_t *)(a))
/* PCR: los relojes */
#define PCR_UART_SCLK(n)         R(0x60096004 + 0xC * (n))
#define PCR_SYSCLK_CONF          R(0x60096110)
#define PCR_CPU_FREQ_CONF        R(0x60096118)
#define PCR_AHB_FREQ_CONF        R(0x6009611C)
#define PCR_APB_FREQ_CONF        R(0x60096120)
/* LP_AON y LP_CLKRST */
#define LP_AON_STORE(n)          R(0x600B1000 + 4 * (n))
#define LP_AON_SYS_CFG           R(0x600B1034)
#define LP_AON_CPUCORE0_CFG      R(0x600B1038)
#define LP_CLKRST_RESET_CAUSE    R(0x600B0410)
/* La matriz, el INTPRI y el CLINT */
#define INT_MAP(src)             R(0x60010000 + 4 * (src))
#define INT_ENABLE               R(0x600C5000)
#define INT_TYPE                 R(0x600C5004)
#define INT_EIP                  R(0x600C5008)
#define INT_PRI(n)               R(0x600C500C + 4 * (n))
#define INT_THRESH               R(0x600C508C)
#define INT_FROM_CPU(n)          R(0x600C5090 + 4 * (n))
#define INT_CLEAR                R(0x600C50A8)
#define CLINT_MSIP               R(0x20001800)
#define CLINT_MTIMECTL           R(0x20001804)
#define CLINT_MTIME_LO           R(0x20001808)
#define CLINT_MTIME_HI           R(0x2000180C)
#define CLINT_MTIMECMP_LO        R(0x20001810)
#define CLINT_MTIMECMP_HI        R(0x20001814)
#define CLINT_USIP               R(0x20001C00)
#define SRC_GPIO 30
#define SRC_FROM_CPU0 22
#define SRC_UART0 43
#define SRC_UART1 44
#define SRC_TG0_T0 51
#define SRC_SYSTIMER0 57
/* GPIO e IO_MUX */
#define GPIO_OUT                 R(0x60091004)
#define GPIO_OUT_W1TS            R(0x60091008)
#define GPIO_OUT_W1TC            R(0x6009100C)
#define GPIO_ENABLE_W1TS         R(0x60091024)
#define GPIO_ENABLE_W1TC         R(0x60091028)
#define GPIO_STRAP               R(0x60091038)
#define GPIO_IN                  R(0x6009103C)
#define GPIO_STATUS              R(0x60091044)
#define GPIO_STATUS_W1TC         R(0x6009104C)
#define GPIO_PIN(n)              R(0x60091074 + 4 * (n))
#define GPIO_FUNC_IN(s)          R(0x60091154 + 4 * (s))
#define GPIO_FUNC_OUT(n)         R(0x60091554 + 4 * (n))
#define IO_MUX(n)                R(0x60090004 + 4 * (n))
#define IOMUX_FUN_IE  (1u << 9)
#define IOMUX_FUN_WPU (1u << 8)
#define IOMUX_FUN_WPD (1u << 7)
#define IOMUX_GPIO    (1u << 12)       /* MCU_SEL = 1 */
/* UART (la disposición del C6) */
#define UART_BASE(n)             ((n) ? 0x60001000u : 0x60000000u)
#define UART_FIFO(n)             R(UART_BASE(n) + 0x00)
#define UART_INT_RAW(n)          R(UART_BASE(n) + 0x04)
#define UART_INT_ST(n)           R(UART_BASE(n) + 0x08)
#define UART_INT_ENA(n)          R(UART_BASE(n) + 0x0C)
#define UART_INT_CLR(n)          R(UART_BASE(n) + 0x10)
#define UART_CLKDIV(n)           R(UART_BASE(n) + 0x14)
#define UART_STATUS(n)           R(UART_BASE(n) + 0x1C)
#define UART_CONF0(n)            R(UART_BASE(n) + 0x20)
#define UART_CONF1(n)            R(UART_BASE(n) + 0x24)
#define UART_TOUT_CONF(n)        R(UART_BASE(n) + 0x64)
#define UART_CLK_CONF(n)         R(UART_BASE(n) + 0x88)
#define UART_DATE(n)             R(UART_BASE(n) + 0x8C)
#define UART_REG_UPDATE(n)       R(UART_BASE(n) + 0x98)
#define UART_ID(n)               R(UART_BASE(n) + 0x9C)
/* SYSTIMER */
#define ST_CONF                  R(0x6000A000)
#define ST_UNIT0_OP              R(0x6000A004)
#define ST_TARGET0_HI            R(0x6000A01C)
#define ST_TARGET0_LO            R(0x6000A020)
#define ST_TARGET0_CONF          R(0x6000A034)
#define ST_UNIT0_VALUE_HI        R(0x6000A040)
#define ST_UNIT0_VALUE_LO        R(0x6000A044)
#define ST_COMP0_LOAD            R(0x6000A050)
#define ST_INT_ENA               R(0x6000A064)
#define ST_INT_RAW               R(0x6000A068)
#define ST_INT_CLR               R(0x6000A06C)
/* El buzón: lo que el banco lee al acabar */
#define BUZON_MAGIC 0xC6C6B00Cu
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
/* El PLL: la CPU a 160 MHz (HS_DIV 0), el AHB y el APB a 40 (lo de ESP-IDF) */
static inline void reloj_160(void) {
    PCR_CPU_FREQ_CONF = 0;                        /* CPU_HS_DIV_NUM 0 */
    PCR_AHB_FREQ_CONF = 3u << 8;                  /* AHB_HS_DIV_NUM 3 */
    PCR_APB_FREQ_CONF = 0;
    PCR_SYSCLK_CONF = (PCR_SYSCLK_CONF & ~(3u << 16)) | (1u << 16);
}
/* 115200 8N1 con SCLK = PLL_F80M (sel 1 en el PCR) */
static inline void uart_init(int n) {
    PCR_UART_SCLK(n) = (PCR_UART_SCLK(n) & ~((3u << 20) | (0xFFu << 12))) | (1u << 20) | (1u << 22);
    UART_CLKDIV(n) = 694u | (7u << 20);           /* 80e6 / 694,4375 = 115204 */
    UART_CONF0(n) = (3u << 2) | (1u << 4);        /* 8 bits, 1 de parada */
    UART_CONF0(n) |= (1u << 22) | (1u << 23);     /* RXFIFO_RST, TXFIFO_RST */
    UART_CONF0(n) &= ~((1u << 22) | (1u << 23));
    UART_REG_UPDATE(n) = 1;
    while (UART_REG_UPDATE(n) & 1u) {}
}
static inline void uart_putc(int n, char c) {
    while (((UART_STATUS(n) >> 16) & 0xFFu) >= 126) {}
    UART_FIFO(n) = (uint8_t)c;
}
static inline void uart_puts(int n, const char *s) { while (*s) uart_putc(n, *s++); }
static inline void uart_flush(int n) {
    while ((UART_STATUS(n) >> 16) & 0xFFu) {}
    espera_us(100);
}
#define CSR_W(csr, v) __asm__ volatile("csrw " #csr ", %0" :: "r"(v))
#define CSR_R(csr) ({ uint32_t _v; __asm__ volatile("csrr %0, " #csr : "=r"(_v)); _v; })
#define CSR_S(csr, v) __asm__ volatile("csrs " #csr ", %0" :: "r"(v))
#define CSR_C(csr, v) __asm__ volatile("csrc " #csr ", %0" :: "r"(v))
static inline void irq_on(void)  { __asm__ volatile("csrsi mstatus, 8"); }
static inline void irq_off(void) { __asm__ volatile("csrci mstatus, 8"); }
static inline void fence(void)   { __asm__ volatile("fence"); }
#endif
