#ifndef MOCK_TUSB_H
#define MOCK_TUSB_H

#include <stdint.h>
#include <stdbool.h>
#include "tusb_config.h"

#define TUSB_DESC_DEVICE                0x01
#define TUSB_DESC_CONFIGURATION         0x02
#define TUSB_DESC_STRING                0x03
#define TUSB_DESC_INTERFACE             0x04
#define TUSB_DESC_ENDPOINT              0x05

#define TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP 0x20
#define HID_ITF_PROTOCOL_NONE           0

#define HID_REPORT_ID(id) 0x85, (id)

#define TUD_HID_REPORT_DESC_KEYBOARD(...) \
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, __VA_ARGS__, 0xC0

#define TUD_HID_REPORT_DESC_CONSUMER(...) \
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, __VA_ARGS__, 0xC0

#define TUD_CONFIG_DESC_LEN 9
#define TUD_HID_DESC_LEN    25

typedef enum {
    HID_REPORT_TYPE_INVALID = 0,
    HID_REPORT_TYPE_INPUT,
    HID_REPORT_TYPE_OUTPUT,
    HID_REPORT_TYPE_FEATURE
} hid_report_type_t;

#define TUD_CONFIG_DESCRIPTOR(config_num, itf_count, stridx, total_len, attr, power_ma) \
    9, TUSB_DESC_CONFIGURATION, (uint8_t)((total_len) & 0xFF), (uint8_t)(((total_len) >> 8) & 0xFF), \
    (itf_count), (config_num), (stridx), (uint8_t)(0x80 | (attr)), (uint8_t)((power_ma) / 2)

#define TUD_HID_DESCRIPTOR(itfnum, stridx, boot_protocol, report_desc_len, epin, epsize, ep_interval) \
    9, TUSB_DESC_INTERFACE, (itfnum), 0, 1, 3, 0, (boot_protocol), (stridx), \
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, (uint8_t)((report_desc_len) & 0xFF), (uint8_t)(((report_desc_len) >> 8) & 0xFF), \
    7, TUSB_DESC_ENDPOINT, (epin), 0x03, (uint8_t)((epsize) & 0xFF), (uint8_t)(((epsize) >> 8) & 0xFF), (ep_interval)

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} tusb_desc_device_t;

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                 hid_report_type_t report_type,
                                 uint8_t *buffer, uint16_t reqlen);
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize);
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len);

#endif /* MOCK_TUSB_H */
