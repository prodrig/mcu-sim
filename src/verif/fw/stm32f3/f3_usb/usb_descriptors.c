/* usb_descriptors.c — los descriptores de f3_usb (S6): un dispositivo compuesto
 * con IAD, un CDC-ACM (interfaces 0 y 1: notificacion en EP1 IN, datos en EP2)
 * y un raton HID de arranque (interfaz 2, EP3 IN cada 10 ms). VID:PID
 * 1209:0003, de los de pruebas de pid.codes */
#include "tusb.h"

#define VID 0x1209
#define PID 0x0003

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = VID,
    .idProduct = PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

static uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_MOUSE() };
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

enum { ITF_CDC = 0, ITF_CDC_DATOS, ITF_HID, ITF_TOTAL };
#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT   0x02
#define EP_CDC_IN    0x82
#define EP_HID       0x83
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_HID, 5, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_report), EP_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 10),
};
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

static char const *const cadenas[] = {
    NULL, "mcu-sim", "f3_usb (CDC + HID)", "F303VC-S6", "f3_usb CDC", "f3_usb raton",
};
static uint16_t desc_str[32];
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t n;
    if (index == 0) {
        desc_str[1] = 0x0409;
        n = 1;
    } else {
        if (index >= sizeof cadenas / sizeof cadenas[0]) return NULL;
        const char *c = cadenas[index];
        for (n = 0; c[n] && n < 31; ++n) desc_str[1 + n] = (uint16_t)c[n];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
