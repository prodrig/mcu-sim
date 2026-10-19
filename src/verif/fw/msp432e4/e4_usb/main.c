/* ===========================================================================
 * e4_usb — Un puerto serie por USB (CDC-ACM) en el MSP432E401Y, con DriverLib
 *          (T7)
 *
 * El «usb_dev_serial» de TivaWare reducido a lo esencial y escrito aqui sobre
 * usb.c de DriverLib, sin usblib: la maquina del EP0 (las peticiones estandar
 * y las de la clase CDC) en la interrupcion del USB0, y un eco en los
 * endpoints de datos: lo que llega por el OUT de bulk vuelve por el IN en
 * mayusculas.
 *
 *   EP0          control, 64 bytes
 *   EP1 IN/OUT   datos (bulk, 64 bytes)
 *   EP2 IN       notificaciones de la clase (interrupcion, 16 bytes; no se usa)
 *
 * VID:PID 1209:0001, el de pruebas de pid.codes. Las peticiones de la clase:
 * SET_LINE_CODING, GET_LINE_CODING y SET_CONTROL_LINE_STATE; lo que mande el
 * ordenador queda en el buzon, y un "?" solo en un paquete lo devuelve en
 * texto ("9600 8E2 3": baudios, formato y DTR|RTS), para verlo desde el
 * terminal al otro lado del puente.
 *
 * El buzon (0x2000_0000):
 *   0x08 estado     10 conectado (SOFTCONN), 20 direccion puesta,
 *                   100 configurado
 *   0x0C sysclk
 *   0x10 resets de bus     0x14 direccion       0x18 baudios (SET_LINE_CODING)
 *   0x1C formato: paridad << 16 | datos << 8 | parada
 *   0x20 lineas (DTR bit 0, RTS bit 1)          0x24 bytes de eco
 *   0x28 peticiones con STALL
 * ===========================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include "ti/devices/msp432e4/inc/msp432e401y.h"
#include "ti/devices/msp432e4/driverlib/driverlib.h"
#include "ti/devices/msp432e4/driverlib/inc/hw_usb.h"

typedef struct {
    volatile uint32_t modo, arranques, estado, sysclk;
    volatile uint32_t resets, direccion, baudios, formato, lineas, bytes, stalls;
} buzon_t;
buzon_t buzon __attribute__((section(".mailbox")));

static const uint8_t DISPOSITIVO[18] = {
    18, 1, 0x00, 0x02, 0x02, 0x00, 0x00, 64,     /* USB 2.0, clase CDC, EP0 de 64 */
    0x09, 0x12, 0x01, 0x00, 0x00, 0x01,          /* VID 1209, PID 0001, version 1.00 */
    1, 2, 3, 1                                   /* cadenas, una configuracion */
};
static const uint8_t CONFIGURACION[67] = {
    9, 2, 67, 0, 2, 1, 0, 0xC0, 50,              /* 2 interfaces, propia, 100 mA */
    /* Interfaz 0: comunicaciones (CDC, ACM, AT) */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,                   /* cabecera CDC 1.10 */
    5, 0x24, 0x01, 0x00, 1,                      /* gestion de llamadas */
    4, 0x24, 0x02, 0x02,                         /* ACM: line coding y line state */
    5, 0x24, 0x06, 0, 1,                         /* union 0 -> 1 */
    7, 5, 0x82, 0x03, 16, 0, 10,                 /* EP2 IN, interrupcion */
    /* Interfaz 1: datos */
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 5, 0x01, 0x02, 64, 0, 0,                  /* EP1 OUT, bulk */
    7, 5, 0x81, 0x02, 64, 0, 0                   /* EP1 IN, bulk */
};
static const uint8_t IDIOMA[4] = {4, 3, 0x09, 0x04};

/* Las cadenas, de ASCII a UTF-16LE al vuelo */
static const char *const CADENAS[4] = {0, "mcu-sim", "MSP432E401Y CDC de prueba", "E4-T7-0001"};

/* El estado del EP0 */
static uint8_t g_buf[128];
static const uint8_t *g_tx;
static uint32_t g_tx_n;
static bool g_tx_cero;             /* falta un paquete corto (de 0) al final */
static enum { REPOSO, ENVIANDO, RECIBIENDO, DIRECCION } g_ep0;
static uint8_t g_dir_pendiente;
static uint8_t g_line[7] = {0x00, 0xC2, 0x01, 0x00, 0, 0, 8};   /* 115200 8N1 */
static uint8_t g_eco[64];
static uint32_t g_eco_n;

static void manda_ep0(void) {
    const uint32_t n = g_tx_n > 64 ? 64 : g_tx_n;
    USBEndpointDataPut(USB0_BASE, USB_EP_0, (uint8_t *)g_tx, n);
    g_tx += n;
    g_tx_n -= n;
    const bool ultimo = g_tx_n == 0 && !(n == 64 && g_tx_cero);
    if (n == 64 && g_tx_n == 0 && g_tx_cero) g_tx_cero = false;   /* el corto, en el siguiente */
    USBEndpointDataSend(USB0_BASE, USB_EP_0, ultimo ? USB_TRANS_IN_LAST : USB_TRANS_IN);
    g_ep0 = ultimo ? REPOSO : ENVIANDO;
}
static void contesta(const uint8_t *d, uint32_t n, uint16_t pedido) {
    g_tx = d;
    g_tx_n = n < pedido ? n : pedido;
    g_tx_cero = (g_tx_n < pedido) && (g_tx_n % 64 == 0);
    USBDevEndpointDataAck(USB0_BASE, USB_EP_0, false);
    manda_ep0();
}
static void stall(void) {
    buzon.stalls++;
    USBDevEndpointStall(USB0_BASE, USB_EP_0, USB_EP_DEV_OUT);
    g_ep0 = REPOSO;
}

static void setup(const uint8_t *s) {
    const uint8_t tipo = s[0], pet = s[1];
    const uint16_t valor = (uint16_t)(s[2] | (s[3] << 8));
    const uint16_t largo = (uint16_t)(s[6] | (s[7] << 8));
    if (tipo == 0x80 && pet == 6) {                         /* GET_DESCRIPTOR */
        const uint8_t t = (uint8_t)(valor >> 8), i = (uint8_t)valor;
        if (t == 1) { contesta(DISPOSITIVO, sizeof DISPOSITIVO, largo); return; }
        if (t == 2) { contesta(CONFIGURACION, sizeof CONFIGURACION, largo); return; }
        if (t == 3 && i == 0) { contesta(IDIOMA, sizeof IDIOMA, largo); return; }
        if (t == 3 && i < 4) {
            uint32_t n = 2;
            for (const char *c = CADENAS[i]; *c && n < sizeof g_buf; ++c) { g_buf[n++] = (uint8_t)*c; g_buf[n++] = 0; }
            g_buf[0] = (uint8_t)n; g_buf[1] = 3;
            contesta(g_buf, n, largo);
            return;
        }
        stall();
        return;
    }
    if (tipo == 0x00 && pet == 5) {                         /* SET_ADDRESS */
        g_dir_pendiente = (uint8_t)valor;
        USBDevEndpointDataAck(USB0_BASE, USB_EP_0, true);
        g_ep0 = DIRECCION;
        return;
    }
    if (tipo == 0x00 && pet == 9) {                         /* SET_CONFIGURATION */
        USBDevEndpointDataAck(USB0_BASE, USB_EP_0, true);
        if (valor == 1) {
            USBDevEndpointConfigSet(USB0_BASE, USB_EP_1, 64, USB_EP_MODE_BULK | USB_EP_DEV_IN);
            USBDevEndpointConfigSet(USB0_BASE, USB_EP_1, 64, USB_EP_MODE_BULK | USB_EP_DEV_OUT);
            USBDevEndpointConfigSet(USB0_BASE, USB_EP_2, 16, USB_EP_MODE_INT | USB_EP_DEV_IN);
            buzon.estado = 100;
        }
        return;
    }
    if (tipo == 0x80 && pet == 8) {                         /* GET_CONFIGURATION */
        g_buf[0] = buzon.estado == 100 ? 1 : 0;
        contesta(g_buf, 1, largo);
        return;
    }
    if (tipo == 0x80 && pet == 0) {                         /* GET_STATUS */
        g_buf[0] = 1; g_buf[1] = 0;                         /* autoalimentado */
        contesta(g_buf, 2, largo);
        return;
    }
    if (tipo == 0x21 && pet == 0x20 && largo == 7) {        /* SET_LINE_CODING */
        USBDevEndpointDataAck(USB0_BASE, USB_EP_0, false);
        g_ep0 = RECIBIENDO;
        return;
    }
    if (tipo == 0xA1 && pet == 0x21) {                      /* GET_LINE_CODING */
        contesta(g_line, 7, largo);
        return;
    }
    if (tipo == 0x21 && pet == 0x22) {                      /* SET_CONTROL_LINE_STATE */
        buzon.lineas = valor & 3u;
        USBDevEndpointDataAck(USB0_BASE, USB_EP_0, true);
        return;
    }
    stall();
}

static void ep0(void) {
    const uint32_t st = USBEndpointStatus(USB0_BASE, USB_EP_0);
    if (st & USB_DEV_EP0_SENT_STALL) {
        USBDevEndpointStatusClear(USB0_BASE, USB_EP_0, USB_DEV_EP0_SENT_STALL);
        g_ep0 = REPOSO;
        return;
    }
    if (st & USB_DEV_EP0_SETUP_END) {
        USBDevEndpointStatusClear(USB0_BASE, USB_EP_0, USB_DEV_EP0_SETUP_END);
        g_ep0 = REPOSO;
    }
    if (g_ep0 == DIRECCION) {                               /* el estado de SET_ADDRESS */
        USBDevAddrSet(USB0_BASE, g_dir_pendiente);
        buzon.direccion = g_dir_pendiente;
        if (buzon.estado < 20) buzon.estado = 20;
        g_ep0 = REPOSO;
    }
    if (g_ep0 == ENVIANDO) { manda_ep0(); return; }
    if (!(st & USB_DEV_EP0_OUT_PKTRDY)) return;
    uint8_t s[8];
    uint32_t n = 8;
    USBEndpointDataGet(USB0_BASE, USB_EP_0, s, &n);
    if (g_ep0 == RECIBIENDO) {                              /* los 7 de SET_LINE_CODING */
        for (unsigned i = 0; i < 7 && i < n; ++i) g_line[i] = s[i];
        buzon.baudios = g_line[0] | (g_line[1] << 8) | (g_line[2] << 16) | ((uint32_t)g_line[3] << 24);
        buzon.formato = ((uint32_t)g_line[5] << 16) | ((uint32_t)g_line[6] << 8) | g_line[4];
        USBDevEndpointDataAck(USB0_BASE, USB_EP_0, true);
        g_ep0 = REPOSO;
        return;
    }
    if (n == 8) setup(s);
}

/* "?" solo, en un paquete: la linea que ha pedido el ordenador, en texto
 * ("9600 8E2 3\r\n": baudios, formato y lineas DTR|RTS) */
static uint32_t pon_num(uint8_t *d, uint32_t v) {
    uint8_t t[10];
    uint32_t k = 0, n = 0;
    do { t[k++] = (uint8_t)('0' + v % 10); v /= 10; } while (v);
    while (k) d[n++] = t[--k];
    return n;
}
static uint32_t linea_en_texto(uint8_t *d) {
    static const char PAR[5] = {'N', 'O', 'E', 'M', 'S'};
    uint32_t n = pon_num(d, buzon.baudios ? buzon.baudios : 115200);
    d[n++] = ' ';
    d[n++] = (uint8_t)('0' + g_line[6]);
    d[n++] = (uint8_t)(g_line[5] < 5 ? PAR[g_line[5]] : '?');
    d[n++] = g_line[4] == 0 ? '1' : g_line[4] == 2 ? '2' : '5';
    d[n++] = ' ';
    n += pon_num(d + n, buzon.lineas);
    d[n++] = '\r'; d[n++] = '\n';
    return n;
}

static void eco(void) {
    /* Lo que llega por el OUT de bulk, en mayusculas, al IN, si el IN esta libre */
    if (g_eco_n == 0 && (USBEndpointStatus(USB0_BASE, USB_EP_1) & USB_DEV_RX_PKT_RDY)) {
        uint32_t n = sizeof g_eco;
        USBEndpointDataGet(USB0_BASE, USB_EP_1, g_eco, &n);
        USBDevEndpointDataAck(USB0_BASE, USB_EP_1, true);
        for (uint32_t i = 0; i < n; ++i) if (g_eco[i] >= 'a' && g_eco[i] <= 'z') g_eco[i] -= 32;
        if (n == 1 && g_eco[0] == '?') n = linea_en_texto(g_eco);
        g_eco_n = n;
    }
    if (g_eco_n && !(USBEndpointStatus(USB0_BASE, USB_EP_1) & USB_DEV_TX_TXPKTRDY)) {
        USBEndpointDataPut(USB0_BASE, USB_EP_1, g_eco, g_eco_n);
        USBEndpointDataSend(USB0_BASE, USB_EP_1, USB_TRANS_IN);
        buzon.bytes += g_eco_n;
        g_eco_n = 0;
    }
}

void USB0_IRQHandler(void) {
    const uint32_t c = USBIntStatusControl(USB0_BASE);
    const uint32_t e = USBIntStatusEndpoint(USB0_BASE);
    if (c & USB_INTCTRL_RESET) {
        buzon.resets++;
        g_ep0 = REPOSO;
        g_eco_n = 0;
        if (buzon.estado > 10) buzon.estado = 10;
    }
    if (e & USB_INTEP_0) ep0();
    if (e & (USB_INTEP_DEV_OUT_1 | USB_INTEP_DEV_IN_1)) eco();
}

int main(void) {
    buzon.arranques++;
    const uint32_t clk = SysCtlClockFreqSet(SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL |
                                            SYSCTL_CFG_VCO_480, 120000000);
    buzon.sysclk = clk;
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOL);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_USB0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_USB0)) { }
    GPIOPinTypeUSBAnalog(GPIO_PORTL_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    USBClockEnable(USB0_BASE, 8, USB_CLOCK_INTERNAL);       /* 480 MHz / 8 = 60 MHz */
    USBModeConfig(USB0_BASE, USB_GPCS_DEVMOD_DEV);           /* dispositivo, sin VBUS ni ID */
    USBIntEnableControl(USB0_BASE, USB_INTCTRL_RESET);
    USBIntEnableEndpoint(USB0_BASE, USB_INTEP_0 | USB_INTEP_DEV_OUT_1 | USB_INTEP_DEV_IN_1);
    IntEnable(INT_USB0);
    IntMasterEnable();
    USBDevConnect(USB0_BASE);
    buzon.estado = 10;
    for (;;) __asm volatile ("wfi");
}
