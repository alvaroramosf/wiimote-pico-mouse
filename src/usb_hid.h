#ifndef USB_HID_H
#define USB_HID_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "one_euro_filter.h"

/* Mouse button bitmasks in Report ID 1 */
#define HID_MOUSE_BTN_LEFT      0x01U
#define HID_MOUSE_BTN_RIGHT     0x02U
#define HID_MOUSE_BTN_MIDDLE    0x04U
#define HID_MOUSE_BTN_BACK      0x08U
#define HID_MOUSE_BTN_FORWARD   0x10U

/* WebHID Feature Report ID 4 protocol (64-byte payload) */
#define WEBHID_CFG_MAGIC        0x57U   /* 'W' */
#define WEBHID_CMD_GET          0x00U
#define WEBHID_CMD_SET_KEYMAP   0x01U
#define WEBHID_CMD_RESET_DEF    0x02U

/* Status codes: in GET responses the `cmd` byte carries the result of the most
 * recent SET/RESET request (WEBHID_STATUS_OK when idle). */
#define WEBHID_STATUS_OK             0x00U
#define WEBHID_STATUS_BAD_FRAME      0x01U  /* wrong length or magic */
#define WEBHID_STATUS_BAD_CRC        0x02U
#define WEBHID_STATUS_INVALID_CONFIG 0x03U  /* values rejected by validation */
#define WEBHID_STATUS_FLASH_ERROR    0x04U  /* applied live but Flash write failed */
#define WEBHID_STATUS_SAVE_PENDING   0x05U  /* applied live, Flash write queued */

typedef struct __attribute__((packed)) {
    uint8_t         magic;
    uint8_t         cmd;
    uint16_t        fw_version;
    keymap_config_t keymap;
    uint32_t        crc32;
} webhid_cfg_report_t;

typedef struct __attribute__((packed)) {
    uint8_t  buttons;
    uint16_t x;
    uint16_t y;
    int8_t   wheel;
} wiimote_abs_mouse_report_t;

typedef struct __attribute__((packed)) {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} wiimote_keyboard_report_t;

/**
 * @brief Initialize USB HID report state, queues, and default keymap.
 */
void usb_hid_init(void);

/**
 * @brief Register the live 1-Euro filter instance for WebHID tuning updates.
 */
void usb_hid_set_filter_instance(one_euro_2d_t *filter);

/**
 * @brief Validate and apply a new keymap & sensitivity configuration live.
 */
bool usb_hid_set_keymap(const keymap_config_t *km);

/**
 * @brief Copy the currently active keymap & sensitivity configuration.
 */
void usb_hid_get_keymap(keymap_config_t *out);

/**
 * @brief Translate Wiimote buttons and filtered absolute pointer coordinates
 *        into queued USB HID Mouse, Keyboard, and Consumer Control reports.
 */
void usb_hid_process_wiimote(uint16_t buttons, uint16_t prev_buttons,
                             uint16_t abs_x, uint16_t abs_y,
                             bool in_calibration, uint32_t now_ms);

/**
 * @brief Flush pending USB HID reports whenever TinyUSB endpoint is ready.
 */
void usb_hid_task(void);

/**
 * @brief Perform a queued WebHID Flash save once the HID queues are idle and
 *        no mouse button is held. Called from usb_hid_task().
 */
void usb_hid_service_pending_save(void);

/**
 * @brief Result of the most recent WebHID request (WEBHID_STATUS_*).
 */
uint8_t usb_hid_get_cfg_status(void);

#ifndef PICO_ON_DEVICE
/* Host inspection helpers for unit tests */
wiimote_abs_mouse_report_t usb_hid_test_get_last_mouse(void);
uint16_t usb_hid_test_pop_consumer(void);
wiimote_keyboard_report_t usb_hid_test_pop_keyboard(bool *out_valid);
#endif

#endif /* USB_HID_H */
