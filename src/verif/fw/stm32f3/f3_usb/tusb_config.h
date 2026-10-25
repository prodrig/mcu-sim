/* tusb_config.h — la configuracion de TinyUSB para f3_usb (S6): dispositivo
 * Full Speed en el FSDEV del STM32F303, sin sistema operativo, con un CDC-ACM
 * y un raton HID */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU              OPT_MCU_STM32F3
#define CFG_TUSB_OS               OPT_OS_NONE
#define CFG_TUSB_DEBUG            0
#define CFG_TUD_ENABLED           1
#define CFG_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUD_CDC               1
#define CFG_TUD_HID               1
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256
#define CFG_TUD_CDC_EP_BUFSIZE    64
#define CFG_TUD_HID_EP_BUFSIZE    8

#endif
