#include "config.h"
#include "tusb.h"

#include <stdio.h>
#include <string.h>

#ifdef PICO_ON_DEVICE
#include "pico/unique_id.h"
#endif

/* ============================================================================
 * Composite USB HID Report Descriptor:
 *   Report ID 1: 5-Button Absolute Mouse (X, Y in 0..32767) + Relative Wheel
 *   Report ID 2: Standard Keyboard (for Super / Activities or configurable key)
 *   Report ID 3: Consumer Control (Volume Up, Volume Down, Play/Pause)
 * ============================================================================ */
static const uint8_t desc_hid_report[] = {
    /* Report ID 1: Absolute Mouse / Pointer + Vertical Scroll Wheel */
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x02,        /* Usage (Mouse) */
    0xA1, 0x01,        /* Collection (Application) */
    0x85, REPORT_ID_ABS_MOUSE, /* Report ID (1) */
    0x09, 0x01,        /*   Usage (Pointer) */
    0xA1, 0x00,        /*   Collection (Physical) */
    /* 5 Mouse Buttons: Left, Right, Middle, Back, Forward */
    0x05, 0x09,        /*     Usage Page (Button) */
    0x19, 0x01,        /*     Usage Minimum (Button 1) */
    0x29, 0x05,        /*     Usage Maximum (Button 5) */
    0x15, 0x00,        /*     Logical Minimum (0) */
    0x25, 0x01,        /*     Logical Maximum (1) */
    0x95, 0x05,        /*     Report Count (5) */
    0x75, 0x01,        /*     Report Size (1) */
    0x81, 0x02,        /*     Input (Data, Variable, Absolute) */
    /* 3 bits constant padding */
    0x95, 0x01,        /*     Report Count (1) */
    0x75, 0x03,        /*     Report Size (3) */
    0x81, 0x03,        /*     Input (Constant, Variable, Absolute) */
    /* Absolute X and Y axes: 0..32767 (16-bit) */
    0x05, 0x01,        /*     Usage Page (Generic Desktop) */
    0x09, 0x30,        /*     Usage (X) */
    0x09, 0x31,        /*     Usage (Y) */
    0x15, 0x00,        /*     Logical Minimum (0) */
    0x26, 0xFF, 0x7F,  /*     Logical Maximum (32767) */
    0x35, 0x00,        /*     Physical Minimum (0) */
    0x46, 0xFF, 0x7F,  /*     Physical Maximum (32767) */
    0x75, 0x10,        /*     Report Size (16) */
    0x95, 0x02,        /*     Report Count (2) */
    0x81, 0x02,        /*     Input (Data, Variable, Absolute) */
    /* Relative Vertical Wheel: -127..127 (8-bit) */
    0x09, 0x38,        /*     Usage (Wheel) */
    0x15, 0x81,        /*     Logical Minimum (-127) */
    0x25, 0x7F,        /*     Logical Maximum (127) */
    0x35, 0x00,        /*     Physical Minimum (0) - reset physical range */
    0x45, 0x00,        /*     Physical Maximum (0) */
    0x75, 0x08,        /*     Report Size (8) */
    0x95, 0x01,        /*     Report Count (1) */
    0x81, 0x06,        /*     Input (Data, Variable, Relative) */
    0xC0,              /*   End Collection (Physical) */
    0xC0,              /* End Collection (Application) */

    /* Report ID 2: Keyboard */
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),

    /* Report ID 3: Consumer Control (Media & Volume) */
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER))
};

/* ============================================================================
 * Vendor-Defined WebHID Configurator Report Descriptor (Interface 1):
 *   Usage Page 0xFF00, Usage 0x01, Report ID 4 (64-byte Feature Report)
 * ============================================================================ */
static const uint8_t desc_hid_cfg_report[] = {
    0x06, 0x00, 0xFF,              /* Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,                    /* Usage (0x01) */
    0xA1, 0x01,                    /* Collection (Application) */
    0x85, REPORT_ID_WEBHID_CFG,    /*   Report ID (4) */
    0x09, 0x02,                    /*   Usage (0x02) */
    0x15, 0x00,                    /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,              /*   Logical Maximum (255) */
    0x75, 0x08,                    /*   Report Size (8) */
    0x95, WEBHID_CFG_REPORT_SIZE,  /*   Report Count (64) */
    0xB1, 0x02,                    /*   Feature (Data, Variable, Absolute) */
    0xC0                           /* End Collection (Application) */
};

/* ============================================================================
 * Device Descriptor
 * ============================================================================ */
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCAFE,
    .idProduct          = 0x4010,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (instance == ITF_NUM_HID_CFG) {
        return desc_hid_cfg_report;
    }
    return desc_hid_report;
}

/* ============================================================================
 * Configuration Descriptor
 * ============================================================================ */
#define ITF_NUM_TOTAL    2
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID        0x81
#define EPNUM_HID_CFG    0x82

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 150),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       64, 5),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_CFG, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_cfg_report), EPNUM_HID_CFG,
                       64, 10)
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* ============================================================================
 * String Descriptors
 * ============================================================================ */
static const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},          /* 0: English (0x0409) */
    "Raspberry Pi Pico DIY",             /* 1: Manufacturer */
    "Wiimote IR Absolute Pointer + Media", /* 2: Product */
    NULL                                 /* 3: Serial (unique Pico flash ID) */
};

static uint16_t s_desc_str[32];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count = 0;

    if (index == 0U) {
        memcpy(&s_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1U;
    } else {
        char serial_buf[24];
        const char *str = NULL;

        if (index == 3U) {
#ifdef PICO_ON_DEVICE
            pico_get_unique_board_id_string(serial_buf, sizeof(serial_buf));
#else
            snprintf(serial_buf, sizeof(serial_buf), "WIIMOTE-PICO-0001");
#endif
            str = serial_buf;
        } else if (index < (uint8_t)(sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            str = string_desc_arr[index];
        } else {
            return NULL;
        }

        size_t len = strlen(str);
        if (len > 31U) {
            len = 31U;
        }
        chr_count = (uint8_t)len;
        for (uint8_t i = 0; i < chr_count; ++i) {
            s_desc_str[1U + i] = (uint16_t)str[i];
        }
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2U * chr_count + 2U));
    return s_desc_str;
}
