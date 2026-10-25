/* ===========================================================================
 * f3_usb — el USB de usuario del STM32F303VC con TinyUSB (fase S6)
 *
 * TinyUSB 0.18.0 (MIT, ../sdk/tinyusb) sobre el FSDEV del F303, sin sistema
 * operativo, con la cabecera de dispositivo de ST. Un dispositivo compuesto:
 *
 *   CDC-ACM  el eco en mayusculas de lo que llega; un "?" solo, en un
 *            paquete, contesta con la linea que ha pedido el ordenador:
 *            "9600 8E2 3\r\n" (baudios, formato y DTR|RTS), como e4_usb, para
 *            que verif/usb/cdc_e4.py valga para los dos;
 *   HID      un raton de arranque: cada pulsacion de B1 (PA0) lo mueve 10 a
 *            la derecha y 5 abajo, en dos informes.
 *
 * Reloj: los 8 MHz del ST-LINK (HSE en bypass), PLL x9 = 72 MHz, y el USB a
 * 72 / 1,5 = 48. D- en PA11 y D+ en PA12, en AF14; la resistencia de subida de
 * D+ es de la placa (R16, 1,5 kohm a 3 V).
 *
 * La primera palabra del buzon (0x2000_0000), al arrancar: con el bit 0, el
 * firmware pone SYSCFG_CFGR1.USB_IT_RMP y el USB va por las IRQ 74-76; TinyUSB
 * lo mira y enciende las que tocan. Lo demas del buzon es lo que pasa.
 * ===========================================================================*/
#include "stm32f303xc.h"
#include "tusb.h"

typedef struct {
    volatile uint32_t modo, estado, montajes, suspensiones;
    volatile uint32_t baudios, lineas, bytes, informes;
    volatile uint32_t pulsaciones, rmp, irq_usb, irq_rmp;
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static volatile uint32_t ms;
void SysTick_Handler(void) { ++ms; }
uint32_t tusb_time_millis_api(void) { return ms; }

static void reloj(void) {
    RCC->CR |= RCC_CR_HSEBYP | RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1;
    /* PLL x9, APB1 a 36 MHz, USBPRE = 0: el USB a PLL / 1,5 */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL | RCC_CFGR_PPRE1 | RCC_CFGR_USBPRE)) |
                RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL9 | RCC_CFGR_PPRE1_DIV2;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SysTick->LOAD = 72000u - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

static void placa(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOEEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;
    /* PA11 y PA12 en AF14, a alta velocidad */
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 22) | (3u << 24))) | (2u << 22) | (2u << 24);
    GPIOA->OSPEEDR |= (3u << 22) | (3u << 24);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 12) | (0xFu << 16))) | (14u << 12) | (14u << 16);
    /* LD3 (PE9): montado; LD10 (PE13): suspendido */
    GPIOE->MODER = (GPIOE->MODER & ~((3u << 18) | (3u << 26))) | (1u << 18) | (1u << 26);
    if (buzon.modo & 1u) {
        SYSCFG->CFGR1 |= SYSCFG_CFGR1_USB_IT_RMP;
        buzon.rmp = 1;
    }
}

/* Los vectores: los compartidos con el CAN y los del remapeo */
/* stm32f303xc.h hace de USBWakeUp_IRQHandler un alias de USBWakeUp_RMP_IRQHandler
 * (y de USBWakeUp_IRQn, la 76): para el vector 42 hace falta el nombre de verdad */
#undef USBWakeUp_IRQHandler
void USB_HP_CAN_TX_IRQHandler(void) { ++buzon.irq_usb; tud_int_handler(0); }
void USB_LP_CAN_RX0_IRQHandler(void) { ++buzon.irq_usb; tud_int_handler(0); }
void USBWakeUp_IRQHandler(void) { ++buzon.irq_usb; tud_int_handler(0); }
void USB_HP_IRQHandler(void) { ++buzon.irq_rmp; tud_int_handler(0); }
void USB_LP_IRQHandler(void) { ++buzon.irq_rmp; tud_int_handler(0); }
void USBWakeUp_RMP_IRQHandler(void) { ++buzon.irq_rmp; tud_int_handler(0); }

/* --- Lo que avisa TinyUSB ------------------------------------------------- */
void tud_mount_cb(void) { ++buzon.montajes; GPIOE->BSRR = 1u << 9; }
void tud_umount_cb(void) { GPIOE->BRR = 1u << 9; }
void tud_suspend_cb(bool remoto) { (void)remoto; ++buzon.suspensiones; GPIOE->BSRR = 1u << 13; }
void tud_resume_cb(void) { GPIOE->BRR = 1u << 13; }

/* --- El CDC ------------------------------------------------------------- */
static uint32_t pon_num(char *d, uint32_t v) {
    char t[10];
    uint32_t k = 0, n = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) d[n++] = t[--k];
    return n;
}
static uint32_t linea_en_texto(char *d) {
    static const char PAR[5] = {'N', 'O', 'E', 'M', 'S'};
    cdc_line_coding_t c;
    tud_cdc_get_line_coding(&c);
    buzon.baudios = c.bit_rate;
    uint32_t n = pon_num(d, c.bit_rate);
    d[n++] = ' ';
    d[n++] = (char)('0' + c.data_bits);
    d[n++] = c.parity < 5 ? PAR[c.parity] : '?';
    d[n++] = c.stop_bits == 0 ? '1' : c.stop_bits == 2 ? '2' : '5';
    d[n++] = ' ';
    n += pon_num(d + n, buzon.lineas);
    d[n++] = '\r';
    d[n++] = '\n';
    return n;
}
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    buzon.lineas = (dtr ? 1u : 0u) | (rts ? 2u : 0u);
}
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *c) {
    (void)itf;
    buzon.baudios = c->bit_rate;
}
static void eco(void) {
    if (!tud_cdc_available()) return;
    char b[64];
    uint32_t n = tud_cdc_read(b, sizeof b);
    if (n == 1 && b[0] == '?') {
        n = linea_en_texto(b);
    } else {
        for (uint32_t i = 0; i < n; ++i) if (b[i] >= 'a' && b[i] <= 'z') b[i] = (char)(b[i] - 32);
    }
    uint32_t hecho = 0;
    while (hecho < n) {
        const uint32_t k = tud_cdc_write(b + hecho, n - hecho);
        hecho += k;
        tud_cdc_write_flush();
        if (!k) tud_task();
    }
    buzon.bytes += n;
}

/* --- El raton ------------------------------------------------------------- */
static uint32_t paso_raton;      /* 0 quieto; 1 a la derecha; 2 abajo */
uint16_t tud_hid_get_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t *b, uint16_t n) {
    (void)i; (void)id; (void)t; (void)b; (void)n;
    return 0;
}
void tud_hid_set_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t const *b, uint16_t n) {
    (void)i; (void)id; (void)t; (void)b; (void)n;
}
static void raton(void) {
    static uint32_t t_b1, antes;
    const uint32_t b1 = GPIOA->IDR & 1u;
    if (b1 && !antes && ms - t_b1 > 20) { ++buzon.pulsaciones; t_b1 = ms; if (!paso_raton) paso_raton = 1; }
    antes = b1;
    if (!paso_raton || !tud_hid_ready()) return;
    if (paso_raton == 1) { tud_hid_mouse_report(0, 0, 10, 0, 0, 0); paso_raton = 2; }
    else                 { tud_hid_mouse_report(0, 0, 0, 5, 0, 0); paso_raton = 0; }
    ++buzon.informes;
}

int main(void) {
    reloj();
    placa();
    tusb_rhport_init_t dev = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &dev);
    buzon.estado = 1;
    for (;;) {
        tud_task();
        eco();
        raton();
    }
}
