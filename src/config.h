#ifndef WIIMOTE_PICO_MOUSE_CONFIG_H
#define WIIMOTE_PICO_MOUSE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Human-readable firmware version. Injected by CMake from the VERSION file;
 * the fallback is only used for host unit-test builds. */
#ifndef FIRMWARE_VERSION_STRING
#define FIRMWARE_VERSION_STRING "dev"
#endif

/* ============================================================================
 * Hardware GPIO & IR LED Power Control
 * ============================================================================ */
/* GP15 drives the base of an NPN transistor (2N2222/BC337) or gate of a
 * logic-level N-MOSFET switching the 4-LED IR diamond (5V VBUS). */
#define IR_LED_GPIO_PIN                 15

/* ============================================================================
 * Wiimote PixArt IR Camera Parameters
 * ============================================================================ */
#define WIIMOTE_IR_WIDTH                1024
#define WIIMOTE_IR_HEIGHT               768
#define WIIMOTE_OPTICAL_CENTER_X        512.0f
#define WIIMOTE_OPTICAL_CENTER_Y        384.0f

/* Raw PixArt coordinates are converted into camera-centered (u, v) where
 * +u points RIGHT and +v points DOWN relative to the TV image.
 * Set to 1 to mirror an axis (512 - raw_x) or 0 to keep (raw_y - 384). */
#define WIIMOTE_IR_INVERT_X             1
#define WIIMOTE_IR_INVERT_Y             0

/* Multiply accelerometer roll atan2(ax, az) by +1 or -1 if needed. */
#define WIIMOTE_ACCEL_ROLL_SIGN         1

/* PixArt IR sensitivity block preset (1 = low, 2 = high/recommended for 2.5m+,
 * 3 = max, 4 = wii-mode max). Level 2 matches Wii sensor bar at 2-4 m. */
#define WIIMOTE_IR_SENSITIVITY_LEVEL    2

/* ============================================================================
 * Composite USB HID Parameters (TinyUSB)
 * ============================================================================ */
#define ITF_NUM_HID                     0
#define ITF_NUM_HID_CFG                 1

#define USB_HID_ABS_MIN                 0
#define USB_HID_ABS_MAX                 32767

#define REPORT_ID_ABS_MOUSE             1
#define REPORT_ID_KEYBOARD              2
#define REPORT_ID_CONSUMER              3
#define REPORT_ID_WEBHID_CFG            4

#define WEBHID_CFG_REPORT_SIZE          64
#define WEBHID_FIRMWARE_VERSION         0x0102U

/* ============================================================================
 * 1-Euro Adaptive Low-Pass Filter Tuning
 * ============================================================================ */
#define ONE_EURO_FREQ_HZ                100.0f  /* Wiimote 0x33 report rate */
#define ONE_EURO_MIN_CUTOFF             1.2f    /* Hz: lower = smoother steady aim */
#define ONE_EURO_BETA                   2.5f    /* speed coeff for [0..1] norm coords */
#define ONE_EURO_DERIV_CUTOFF           1.0f    /* Hz: derivative filter cutoff */

#define ONE_EURO_MIN_CUTOFF_MIN         0.05f
#define ONE_EURO_MIN_CUTOFF_MAX         20.0f
#define ONE_EURO_BETA_MIN               0.0f
#define ONE_EURO_BETA_MAX               50.0f

/* ============================================================================
 * On-Controller Calibration (Stored in Internal Flash)
 * ============================================================================ */
#define CALIB_HOLD_TIME_MS              3000    /* Hold '+' and '-' for 3 seconds */
#define CALIB_TARGET_1_X                0.10f   /* Top-Left target (10%, 10%) */
#define CALIB_TARGET_1_Y                0.10f
#define CALIB_TARGET_2_X                0.90f   /* Bottom-Right target (90%, 90%) */
#define CALIB_TARGET_2_Y                0.90f

/* ============================================================================
 * Button Mapping & Scroll Tuning
 * ============================================================================ */
#define SCROLL_INTERVAL_MS              40      /* ~25 ticks/s while holding Up/Down */
#define SCROLL_INTERVAL_MIN_MS          5U
#define SCROLL_INTERVAL_MAX_MS          1000U
#define SCROLL_STEP_AMOUNT              1
#define SHORT_PRESS_MAX_MS              1000    /* '+' / '-' release < 1s -> volume */

/* Keycode for Wiimote button '2': 0xE3 = HID Left GUI (Super / GNOME Activities) */
#define WIIMOTE_BTN_2_KEYCODE           0xE3

/* Consumer Control Usage Codes (USB HID Usage Page 0x0C) */
#define HID_CONSUMER_PLAY_PAUSE         0x00CD
#define HID_CONSUMER_VOLUME_INCREMENT   0x00E9
#define HID_CONSUMER_VOLUME_DECREMENT   0x00EA

/* ============================================================================
 * Configurable Keymap Schema (WebHID + Flash Persistence)
 * ============================================================================ */
typedef enum {
    WIIMOTE_IDX_B         = 0,
    WIIMOTE_IDX_A         = 1,
    WIIMOTE_IDX_ONE       = 2,
    WIIMOTE_IDX_TWO       = 3,
    WIIMOTE_IDX_UP        = 4,
    WIIMOTE_IDX_DOWN      = 5,
    WIIMOTE_IDX_LEFT      = 6,
    WIIMOTE_IDX_RIGHT     = 7,
    WIIMOTE_IDX_PLUS      = 8,
    WIIMOTE_IDX_MINUS     = 9,
    WIIMOTE_IDX_HOME      = 10,
    WIIMOTE_MAP_BTN_COUNT = 11
} wiimote_btn_index_t;

typedef enum {
    ACTION_NONE           = 0,
    ACTION_MOUSE_BTN      = 1,
    ACTION_MOUSE_WHEEL    = 2,
    ACTION_KEYBOARD_KEY   = 3,
    ACTION_CONSUMER_MEDIA = 4,
    ACTION_TYPE_COUNT     = 5
} hid_action_type_t;

#define BINDING_FLAG_NONE        0x00U
#define BINDING_FLAG_REPEAT      0x01U
#define BINDING_FLAG_HOLD        0x02U
#define BINDING_FLAG_SHORT_PRESS 0x04U

typedef struct __attribute__((packed)) {
    uint8_t  action_type;
    uint8_t  flags;
    uint16_t code;
} button_binding_t;

typedef struct __attribute__((packed)) {
    button_binding_t bindings[WIIMOTE_MAP_BTN_COUNT]; /* 44 bytes */
    float            min_cutoff;                      /* 4 bytes  */
    float            beta;                            /* 4 bytes  */
    uint16_t         scroll_interval_ms;              /* 2 bytes  */
    uint16_t         reserved;                        /* 2 bytes  */
} keymap_config_t;                                    /* 56 bytes */

/* ============================================================================
 * Tracker Degradation Safety Limits
 * ============================================================================ */
#define DIAMOND_MIN_SPAN_PX             12.0f   /* Minimum pixel span for valid diamond */
#define DIAMOND_DEFAULT_HALF_W_PX       160.0f
#define DIAMOND_DEFAULT_HALF_H_PX       120.0f

#endif /* WIIMOTE_PICO_MOUSE_CONFIG_H */
