#ifndef WIIMOTE_BT_H
#define WIIMOTE_BT_H

#include <stdbool.h>
#include <stdint.h>

#include "diamond_tracker.h"

/* Wiimote Core Button Bitmasks (normalized across bytes 0 & 1 of report 0x33) */
#define WIIMOTE_BTN_LEFT    0x0001U
#define WIIMOTE_BTN_RIGHT   0x0002U
#define WIIMOTE_BTN_DOWN    0x0004U
#define WIIMOTE_BTN_UP      0x0008U
#define WIIMOTE_BTN_PLUS    0x0010U
#define WIIMOTE_BTN_TWO     0x0100U
#define WIIMOTE_BTN_ONE     0x0200U
#define WIIMOTE_BTN_B       0x0400U
#define WIIMOTE_BTN_A       0x0800U
#define WIIMOTE_BTN_MINUS   0x1000U
#define WIIMOTE_BTN_HOME    0x8000U

/* Wiimote Player LED Bitmasks (upper nibble of report 0x11 payload) */
#define WIIMOTE_LED_NONE    0x00U
#define WIIMOTE_LED_1       0x10U
#define WIIMOTE_LED_2       0x20U
#define WIIMOTE_LED_3       0x40U
#define WIIMOTE_LED_4       0x80U
#define WIIMOTE_LED_ALL     0xF0U

typedef struct {
    uint16_t   buttons;
    int16_t    accel_x;
    int16_t    accel_y;
    int16_t    accel_z;
    ir_point_t ir[4];
    uint8_t    visible_ir_count;
    uint32_t   timestamp_ms;
} wiimote_report_t;

/**
 * @brief Parse an incoming L2CAP HID Input report 0x33 packet
 *        (Core Buttons + 3-Axis Accelerometer + 12-byte Extended IR).
 *
 * @param packet Raw L2CAP payload starting with 0xA1, 0x33, ...
 * @param len    Packet length in bytes (must be >= 19).
 * @param out    Parsed output structure.
 * @return true if valid 0x33 report parsed, false otherwise.
 */
bool wiimote_parse_report_0x33(const uint8_t *packet, uint16_t len, wiimote_report_t *out);

/**
 * @brief Initialize BTstack Bluetooth Classic L2CAP HID Host.
 */
void wiimote_bt_init(void);

/**
 * @brief Poll CYW43/BTstack events and advance Wiimote initialization sequence.
 */
void wiimote_bt_task(void);

/**
 * @brief Check whether a Wiimote is connected and initialized.
 */
bool wiimote_bt_is_connected(void);

/**
 * @brief Pop the oldest queued Wiimote 0x33 report.
 */
bool wiimote_bt_pop_report(wiimote_report_t *out);

/**
 * @brief Update Wiimote player LEDs (0x10..0xF0) and optional rumble.
 */
void wiimote_bt_set_leds(uint8_t led_mask, bool rumble);

#endif /* WIIMOTE_BT_H */
