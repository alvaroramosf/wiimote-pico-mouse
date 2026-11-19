#include "usb_hid.h"
#include "config.h"
#include "flash_storage.h"
#include "tusb.h"
#include "wiimote_bt.h"

#include <stddef.h>
#include <string.h>

#ifdef PICO_ON_DEVICE
#include "pico/time.h"
#endif

_Static_assert(sizeof(button_binding_t) == 4U,
               "button_binding_t must be 4 bytes");
_Static_assert(sizeof(keymap_config_t) == 56U,
               "keymap_config_t must be 56 bytes");
_Static_assert(sizeof(webhid_cfg_report_t) == WEBHID_CFG_REPORT_SIZE,
               "webhid_cfg_report_t must be 64 bytes");

#define CONSUMER_QUEUE_LEN 16U
#define KEYBOARD_QUEUE_LEN 16U

static const uint16_t k_wiimote_btn_masks[WIIMOTE_MAP_BTN_COUNT] = {
    [WIIMOTE_IDX_B]     = WIIMOTE_BTN_B,
    [WIIMOTE_IDX_A]     = WIIMOTE_BTN_A,
    [WIIMOTE_IDX_ONE]   = WIIMOTE_BTN_ONE,
    [WIIMOTE_IDX_TWO]   = WIIMOTE_BTN_TWO,
    [WIIMOTE_IDX_UP]    = WIIMOTE_BTN_UP,
    [WIIMOTE_IDX_DOWN]  = WIIMOTE_BTN_DOWN,
    [WIIMOTE_IDX_LEFT]  = WIIMOTE_BTN_LEFT,
    [WIIMOTE_IDX_RIGHT] = WIIMOTE_BTN_RIGHT,
    [WIIMOTE_IDX_PLUS]  = WIIMOTE_BTN_PLUS,
    [WIIMOTE_IDX_MINUS] = WIIMOTE_BTN_MINUS,
    [WIIMOTE_IDX_HOME]  = WIIMOTE_BTN_HOME,
};

static wiimote_abs_mouse_report_t s_mouse_state;
static bool s_mouse_dirty = false;
static int16_t s_pending_wheel = 0;

/* Active runtime keymap + filter handle */
static keymap_config_t s_active_keymap;

/* Deferred WebHID Flash save: applied live in the callback, written later from
 * the main loop so a Flash erase never stalls USB/BT mid-callback or mid-click. */
static keymap_config_t s_pending_km;
static bool            s_save_pending = false;
#ifdef PICO_ON_DEVICE
static uint32_t        s_save_requested_ms = 0;
#define WEBHID_SAVE_MAX_DEFER_MS 3000U
#endif
static uint8_t         s_cfg_status = WEBHID_STATUS_OK;
static one_euro_2d_t *s_filter_ptr = NULL;

/* Scroll repeat timing */
static uint32_t s_last_scroll_ms = 0;

/* Per-button short-press tracking (disarmed when '+' and '-' held together) */
static uint32_t s_btn_down_ms[WIIMOTE_MAP_BTN_COUNT];
static bool s_btn_tap_armed[WIIMOTE_MAP_BTN_COUNT];

/* Held keyboard / consumer state */
static wiimote_keyboard_report_t s_last_held_kb;
static uint16_t s_last_held_consumer = 0;

/* Consumer Control ring buffer */
static uint16_t s_consumer_queue[CONSUMER_QUEUE_LEN];
static uint8_t s_cons_head = 0;
static uint8_t s_cons_tail = 0;

/* Keyboard ring buffer */
static wiimote_keyboard_report_t s_kb_queue[KEYBOARD_QUEUE_LEN];
static uint8_t s_kb_head = 0;
static uint8_t s_kb_tail = 0;

static void queue_consumer_report(uint16_t usage_code) {
    uint8_t next = (uint8_t)((s_cons_head + 1U) % CONSUMER_QUEUE_LEN);
    if (next != s_cons_tail) {
        s_consumer_queue[s_cons_head] = usage_code;
        s_cons_head = next;
    }
}

static void queue_consumer_tap(uint16_t usage_code) {
    queue_consumer_report(usage_code);
    queue_consumer_report(s_last_held_consumer);
}

static void add_keycode_to_report(wiimote_keyboard_report_t *rep, uint8_t hid_keycode) {
    if (hid_keycode == 0U) {
        return;
    }
    if (hid_keycode >= 0xE0U && hid_keycode <= 0xE7U) {
        rep->modifier |= (uint8_t)(1U << (hid_keycode - 0xE0U));
        return;
    }
    for (size_t i = 0; i < 6U; ++i) {
        if (rep->keycode[i] == hid_keycode) {
            return;
        }
        if (rep->keycode[i] == 0U) {
            rep->keycode[i] = hid_keycode;
            return;
        }
    }
}

static void queue_keyboard_report(const wiimote_keyboard_report_t *kb) {
    uint8_t next = (uint8_t)((s_kb_head + 1U) % KEYBOARD_QUEUE_LEN);
    if (next != s_kb_tail) {
        s_kb_queue[s_kb_head] = *kb;
        s_kb_head = next;
    }
}

static void queue_keyboard_tap(uint8_t hid_keycode) {
    wiimote_keyboard_report_t press = s_last_held_kb;
    wiimote_keyboard_report_t release = s_last_held_kb;
    add_keycode_to_report(&press, hid_keycode);
    queue_keyboard_report(&press);
    queue_keyboard_report(&release);
}

void usb_hid_init(void) {
    memset(&s_mouse_state, 0, sizeof(s_mouse_state));
    s_mouse_state.x = USB_HID_ABS_MAX / 2U;
    s_mouse_state.y = USB_HID_ABS_MAX / 2U;
    s_mouse_dirty = false;
    s_pending_wheel = 0;
    s_last_scroll_ms = 0;

    s_filter_ptr = NULL;
    flash_keymap_get_defaults(&s_active_keymap);

    memset(s_btn_down_ms, 0, sizeof(s_btn_down_ms));
    memset(s_btn_tap_armed, 0, sizeof(s_btn_tap_armed));
    memset(&s_last_held_kb, 0, sizeof(s_last_held_kb));
    s_last_held_consumer = 0;

    s_cons_head = 0;
    s_cons_tail = 0;
    s_kb_head = 0;
    s_kb_tail = 0;

    s_save_pending = false;
    s_cfg_status = WEBHID_STATUS_OK;
}

void usb_hid_set_filter_instance(one_euro_2d_t *filter) {
    s_filter_ptr = filter;
    if (s_filter_ptr != NULL) {
        float min_cutoff = 0.0f;
        float beta = 0.0f;
        memcpy(&min_cutoff, &s_active_keymap.min_cutoff, sizeof(min_cutoff));
        memcpy(&beta, &s_active_keymap.beta, sizeof(beta));
        one_euro_2d_set_params(s_filter_ptr, min_cutoff, beta);
    }
}

bool usb_hid_set_keymap(const keymap_config_t *km) {
    if (!flash_keymap_validate(km)) {
        return false;
    }

    memcpy(&s_active_keymap, km, sizeof(s_active_keymap));
    memset(s_btn_tap_armed, 0, sizeof(s_btn_tap_armed));

    if (s_filter_ptr != NULL) {
        float min_cutoff = 0.0f;
        float beta = 0.0f;
        memcpy(&min_cutoff, &s_active_keymap.min_cutoff, sizeof(min_cutoff));
        memcpy(&beta, &s_active_keymap.beta, sizeof(beta));
        one_euro_2d_set_params(s_filter_ptr, min_cutoff, beta);
    }

    return true;
}

void usb_hid_get_keymap(keymap_config_t *out) {
    if (out != NULL) {
        memcpy(out, &s_active_keymap, sizeof(*out));
    }
}

void usb_hid_process_wiimote(uint16_t buttons, uint16_t prev_buttons,
                             uint16_t abs_x, uint16_t abs_y,
                             bool in_calibration, uint32_t now_ms) {
    if (abs_x > USB_HID_ABS_MAX) {
        abs_x = USB_HID_ABS_MAX;
    }
    if (abs_y > USB_HID_ABS_MAX) {
        abs_y = USB_HID_ABS_MAX;
    }

    bool both_pm = ((buttons & WIIMOTE_BTN_PLUS) != 0U) &&
                   ((buttons & WIIMOTE_BTN_MINUS) != 0U);
    if (both_pm) {
        /* Disarm taps on '+' and '-' so the 3s calibration combo stays clean */
        s_btn_tap_armed[WIIMOTE_IDX_PLUS] = false;
        s_btn_tap_armed[WIIMOTE_IDX_MINUS] = false;
    }

    uint16_t scroll_interval = SCROLL_INTERVAL_MS;
    memcpy(&scroll_interval, &s_active_keymap.scroll_interval_ms, sizeof(scroll_interval));
    if (scroll_interval < SCROLL_INTERVAL_MIN_MS) {
        scroll_interval = SCROLL_INTERVAL_MS;
    }
    bool scroll_repeat_due = (now_ms - s_last_scroll_ms) >= scroll_interval;
    bool wheel_fired = false;

    uint8_t mouse_btns = 0;
    wiimote_keyboard_report_t held_kb;
    memset(&held_kb, 0, sizeof(held_kb));
    uint16_t held_consumer = 0;

    uint8_t kb_taps[WIIMOTE_MAP_BTN_COUNT];
    uint8_t kb_tap_count = 0;
    uint16_t cons_taps[WIIMOTE_MAP_BTN_COUNT];
    uint8_t cons_tap_count = 0;

    for (size_t i = 0; i < WIIMOTE_MAP_BTN_COUNT; ++i) {
        uint16_t mask = k_wiimote_btn_masks[i];
        bool now_pressed = (buttons & mask) != 0U;
        bool prev_pressed = (prev_buttons & mask) != 0U;

        button_binding_t b;
        memcpy(&b, &s_active_keymap.bindings[i], sizeof(b));

        /* Preserve calibration 'A' confirmation safety */
        if (in_calibration && i == WIIMOTE_IDX_A) {
            s_btn_tap_armed[i] = false;
            continue;
        }

        /* Suppress '+' and '-' actions while both are held for calibration combo */
        if (both_pm && (i == WIIMOTE_IDX_PLUS || i == WIIMOTE_IDX_MINUS)) {
            s_btn_tap_armed[i] = false;
            continue;
        }

        /* Wheel, keyboard, and consumer actions are disabled while calibrating */
        if (in_calibration && b.action_type != ACTION_MOUSE_BTN) {
            s_btn_tap_armed[i] = false;
            continue;
        }

        switch (b.action_type) {
        case ACTION_MOUSE_BTN:
            if (now_pressed) {
                mouse_btns |= (uint8_t)(b.code & 0x1FU);
            }
            break;

        case ACTION_MOUSE_WHEEL:
            if (now_pressed) {
                bool just_pressed = !prev_pressed;
                bool repeat_enabled = (b.flags & BINDING_FLAG_REPEAT) != 0U;
                if (just_pressed || (repeat_enabled && scroll_repeat_due)) {
                    int16_t delta = (int16_t)b.code;
                    s_pending_wheel = (int16_t)(s_pending_wheel + delta);
                    if (s_pending_wheel > 127) {
                        s_pending_wheel = 127;
                    } else if (s_pending_wheel < -127) {
                        s_pending_wheel = -127;
                    }
                    wheel_fired = true;
                }
            }
            break;

        case ACTION_KEYBOARD_KEY:
            if ((b.flags & BINDING_FLAG_HOLD) != 0U) {
                if (now_pressed) {
                    add_keycode_to_report(&held_kb, (uint8_t)b.code);
                }
            } else if ((b.flags & BINDING_FLAG_SHORT_PRESS) != 0U) {
                if (now_pressed && !prev_pressed) {
                    s_btn_down_ms[i] = now_ms;
                    s_btn_tap_armed[i] = true;
                } else if (!now_pressed && prev_pressed && s_btn_tap_armed[i]) {
                    if ((now_ms - s_btn_down_ms[i]) < SHORT_PRESS_MAX_MS) {
                        kb_taps[kb_tap_count++] = (uint8_t)b.code;
                    }
                    s_btn_tap_armed[i] = false;
                }
            } else {
                if (now_pressed && !prev_pressed) {
                    kb_taps[kb_tap_count++] = (uint8_t)b.code;
                }
            }
            break;

        case ACTION_CONSUMER_MEDIA:
            if ((b.flags & BINDING_FLAG_HOLD) != 0U) {
                if (now_pressed && held_consumer == 0U) {
                    held_consumer = b.code;
                }
            } else if ((b.flags & BINDING_FLAG_SHORT_PRESS) != 0U ||
                       i == WIIMOTE_IDX_PLUS ||
                       i == WIIMOTE_IDX_MINUS) {
                if (now_pressed && !prev_pressed) {
                    s_btn_down_ms[i] = now_ms;
                    s_btn_tap_armed[i] = true;
                } else if (!now_pressed && prev_pressed && s_btn_tap_armed[i]) {
                    if ((now_ms - s_btn_down_ms[i]) < SHORT_PRESS_MAX_MS) {
                        cons_taps[cons_tap_count++] = b.code;
                    }
                    s_btn_tap_armed[i] = false;
                }
            } else {
                if (now_pressed && !prev_pressed) {
                    cons_taps[cons_tap_count++] = b.code;
                }
            }
            break;

        case ACTION_NONE:
        default:
            break;
        }
    }

    if (wheel_fired) {
        s_last_scroll_ms = now_ms;
    }

    if (mouse_btns != s_mouse_state.buttons ||
        abs_x != s_mouse_state.x ||
        abs_y != s_mouse_state.y ||
        s_pending_wheel != 0) {
        s_mouse_state.buttons = mouse_btns;
        s_mouse_state.x = abs_x;
        s_mouse_state.y = abs_y;
        s_mouse_dirty = true;
    }

    if (memcmp(&held_kb, &s_last_held_kb, sizeof(held_kb)) != 0) {
        queue_keyboard_report(&held_kb);
        s_last_held_kb = held_kb;
    }

    for (uint8_t k = 0; k < kb_tap_count; ++k) {
        queue_keyboard_tap(kb_taps[k]);
    }

    if (held_consumer != s_last_held_consumer) {
        queue_consumer_report(held_consumer);
        s_last_held_consumer = held_consumer;
    }

    for (uint8_t k = 0; k < cons_tap_count; ++k) {
        queue_consumer_tap(cons_taps[k]);
    }
}

static void usb_hid_flush_reports(void) {
#ifdef PICO_ON_DEVICE
    if (!tud_hid_ready()) {
        return;
    }

    if (s_cons_head != s_cons_tail) {
        uint16_t usage = s_consumer_queue[s_cons_tail];
        if (tud_hid_report(REPORT_ID_CONSUMER, &usage, sizeof(usage))) {
            s_cons_tail = (uint8_t)((s_cons_tail + 1U) % CONSUMER_QUEUE_LEN);
        }
        return;
    }

    if (s_kb_head != s_kb_tail) {
        const wiimote_keyboard_report_t *kb = &s_kb_queue[s_kb_tail];
        if (tud_hid_report(REPORT_ID_KEYBOARD, kb, sizeof(*kb))) {
            s_kb_tail = (uint8_t)((s_kb_tail + 1U) % KEYBOARD_QUEUE_LEN);
        }
        return;
    }

    if (s_mouse_dirty) {
        s_mouse_state.wheel = (int8_t)s_pending_wheel;
        if (tud_hid_report(REPORT_ID_ABS_MOUSE, &s_mouse_state, sizeof(s_mouse_state))) {
            s_pending_wheel = 0;
            s_mouse_state.wheel = 0;
            s_mouse_dirty = false;
        }
    }
#endif
}

void usb_hid_task(void) {
    usb_hid_flush_reports();
    usb_hid_service_pending_save();
}

void usb_hid_service_pending_save(void) {
    if (!s_save_pending) {
        return;
    }
    bool queues_idle = (s_cons_head == s_cons_tail) && (s_kb_head == s_kb_tail);
    bool idle = queues_idle && (s_mouse_state.buttons == 0U) && !s_mouse_dirty;
#ifdef PICO_ON_DEVICE
    /* Don't let a stuck button defer the write forever. */
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool overdue = (now - s_save_requested_ms) >= WEBHID_SAVE_MAX_DEFER_MS;
#else
    bool overdue = false;
#endif
    if (!idle && !overdue) {
        return;
    }
    keymap_config_t km = s_pending_km;
    s_save_pending = false;
    s_cfg_status = flash_storage_save_keymap(&km) ? WEBHID_STATUS_OK
                                                  : WEBHID_STATUS_FLASH_ERROR;
}

uint8_t usb_hid_get_cfg_status(void) {
    return s_cfg_status;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                 hid_report_type_t report_type,
                                 uint8_t *buffer, uint16_t reqlen) {
    if (instance != ITF_NUM_HID_CFG ||
        report_type != HID_REPORT_TYPE_FEATURE ||
        report_id != REPORT_ID_WEBHID_CFG ||
        buffer == NULL ||
        reqlen < sizeof(webhid_cfg_report_t)) {
        return 0;
    }

    webhid_cfg_report_t rep;
    memset(&rep, 0, sizeof(rep));
    rep.magic = WEBHID_CFG_MAGIC;
    rep.cmd = s_save_pending ? WEBHID_STATUS_SAVE_PENDING : s_cfg_status;
    rep.fw_version = WEBHID_FIRMWARE_VERSION;
    memcpy(&rep.keymap, &s_active_keymap, sizeof(rep.keymap));
    rep.crc32 = flash_compute_crc32(&rep, offsetof(webhid_cfg_report_t, crc32));

    memcpy(buffer, &rep, sizeof(rep));
    return (uint16_t)sizeof(rep);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    if (instance != ITF_NUM_HID_CFG ||
        report_type != HID_REPORT_TYPE_FEATURE ||
        buffer == NULL) {
        return;
    }

    if (report_id != 0U && report_id != REPORT_ID_WEBHID_CFG) {
        return;
    }

    const uint8_t *payload = buffer;
    uint16_t payload_len = bufsize;
    if (payload_len == (uint16_t)(sizeof(webhid_cfg_report_t) + 1U) &&
        payload[0] == REPORT_ID_WEBHID_CFG) {
        payload++;
        payload_len--;
    }

    if (payload_len != sizeof(webhid_cfg_report_t)) {
        s_cfg_status = WEBHID_STATUS_BAD_FRAME;
        return;
    }

    webhid_cfg_report_t rep;
    memcpy(&rep, payload, sizeof(rep));

    if (rep.magic != WEBHID_CFG_MAGIC) {
        s_cfg_status = WEBHID_STATUS_BAD_FRAME;
        return;
    }

    uint32_t expected_crc = flash_compute_crc32(&rep, offsetof(webhid_cfg_report_t, crc32));
    if (rep.crc32 != expected_crc) {
        s_cfg_status = WEBHID_STATUS_BAD_CRC;
        return;
    }

    keymap_config_t km;
    if (rep.cmd == WEBHID_CMD_SET_KEYMAP) {
        memcpy(&km, &rep.keymap, sizeof(km));
    } else if (rep.cmd == WEBHID_CMD_RESET_DEF) {
        flash_keymap_get_defaults(&km);
    } else {
        return; /* GET or unknown command: leave status untouched */
    }

    if (!usb_hid_set_keymap(&km)) {
        s_cfg_status = WEBHID_STATUS_INVALID_CONFIG;
        return;
    }

    /* Applied live; the Flash write is deferred to usb_hid_service_pending_save(). */
    s_pending_km = km;
    s_save_pending = true;
#ifdef PICO_ON_DEVICE
    s_save_requested_ms = to_ms_since_boot(get_absolute_time());
#endif
    s_cfg_status = WEBHID_STATUS_SAVE_PENDING;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void)instance;
    (void)report;
    (void)len;
}

#ifndef PICO_ON_DEVICE
wiimote_abs_mouse_report_t usb_hid_test_get_last_mouse(void) {
    wiimote_abs_mouse_report_t out = s_mouse_state;
    out.wheel = (int8_t)s_pending_wheel;
    s_pending_wheel = 0;
    s_mouse_dirty = false;
    return out;
}

uint16_t usb_hid_test_pop_consumer(void) {
    if (s_cons_head == s_cons_tail) {
        return 0xFFFFU;
    }
    uint16_t u = s_consumer_queue[s_cons_tail];
    s_cons_tail = (uint8_t)((s_cons_tail + 1U) % CONSUMER_QUEUE_LEN);
    return u;
}

wiimote_keyboard_report_t usb_hid_test_pop_keyboard(bool *out_valid) {
    wiimote_keyboard_report_t kb;
    memset(&kb, 0, sizeof(kb));
    if (s_kb_head == s_kb_tail) {
        if (out_valid) *out_valid = false;
        return kb;
    }
    kb = s_kb_queue[s_kb_tail];
    s_kb_tail = (uint8_t)((s_kb_tail + 1U) % KEYBOARD_QUEUE_LEN);
    if (out_valid) *out_valid = true;
    return kb;
}
#endif
