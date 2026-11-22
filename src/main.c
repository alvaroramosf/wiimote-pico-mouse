#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "config.h"
#include "diamond_tracker.h"
#include "flash_storage.h"
#include "ir_leds.h"
#include "one_euro_filter.h"
#include "usb_hid.h"
#include "wiimote_bt.h"

static uint16_t clamp_norm_to_usb(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    } else if (v > 1.0f) {
        v = 1.0f;
    }
    long out = lroundf(v * (float)USB_HID_ABS_MAX);
    if (out < USB_HID_ABS_MIN) {
        return USB_HID_ABS_MIN;
    }
    if (out > USB_HID_ABS_MAX) {
        return USB_HID_ABS_MAX;
    }
    return (uint16_t)out;
}

/* ---- Non-blocking Wiimote LED / rumble feedback for calibration ---------- */
typedef enum {
    FB_NONE = 0,
    FB_STEP_OK,   /* step 1 confirmed: short buzz                              */
    FB_SUCCESS,   /* calibration saved: all LEDs solid + buzz                  */
    FB_ERROR,     /* bad sample or Flash failure: alternating LEDs + long buzz */
    FB_CANCEL     /* calibration cancelled: LED1 + very short buzz             */
} feedback_kind_t;

typedef struct {
    feedback_kind_t kind;
    uint32_t        start_ms;
    uint8_t         last_mask;
    bool            last_rumble;
} feedback_t;

static void feedback_apply(feedback_t *fb, uint8_t mask, bool rumble) {
    if (mask != fb->last_mask || rumble != fb->last_rumble) {
        fb->last_mask = mask;
        fb->last_rumble = rumble;
        wiimote_bt_set_leds(mask, rumble);
    }
}

static void feedback_start(feedback_t *fb, feedback_kind_t kind, uint32_t now_ms) {
    fb->kind = kind;
    fb->start_ms = now_ms;
    fb->last_mask = 0xFFU;   /* force the first update through */
    fb->last_rumble = false;
}

/* Advance the active feedback pattern. Returns true while it owns the LEDs. */
static bool feedback_tick(feedback_t *fb, uint32_t now_ms) {
    if (fb->kind == FB_NONE) {
        return false;
    }
    uint32_t t = now_ms - fb->start_ms;
    switch (fb->kind) {
    case FB_STEP_OK:
        if (t >= 200U) {
            feedback_apply(fb, WIIMOTE_LED_4, false);
            fb->kind = FB_NONE;
            fb->last_mask = 0xFFU;
            return false;
        }
        feedback_apply(fb, WIIMOTE_LED_ALL, true);
        break;
    case FB_SUCCESS:
        if (t >= 1200U) { feedback_apply(fb, WIIMOTE_LED_1, false); fb->kind = FB_NONE; return false; }
        feedback_apply(fb, WIIMOTE_LED_ALL, t < 300U);
        break;
    case FB_ERROR:
        if (t >= 1200U) { feedback_apply(fb, WIIMOTE_LED_1, false); fb->kind = FB_NONE; return false; }
        feedback_apply(fb, ((t / 150U) % 2U == 0U) ? (WIIMOTE_LED_1 | WIIMOTE_LED_3)
                                                  : (WIIMOTE_LED_2 | WIIMOTE_LED_4),
                       t < 500U);
        break;
    case FB_CANCEL:
        if (t >= 400U) { fb->kind = FB_NONE; return false; }
        feedback_apply(fb, WIIMOTE_LED_1, t < 120U);
        break;
    default:
        fb->kind = FB_NONE;
        return false;
    }
    return true;
}

/* Blink LED1 while waiting for the top-left sample, LED4 for bottom-right. */
static uint8_t calib_step_led(calib_state_t state) {
    return (state == CALIB_STEP2_BOTTOM_RIGHT) ? WIIMOTE_LED_4 : WIIMOTE_LED_1;
}

int main(void) {
    stdio_init_all();
    printf("wiimote-pico-mouse firmware %s (WebHID protocol 0x%04X)\n",
           FIRMWARE_VERSION_STRING, (unsigned)WEBHID_FIRMWARE_VERSION);

    if (cyw43_arch_init() != 0) {
        printf("ERROR: cyw43_arch_init() failed!\n");
        return -1;
    }

    ir_leds_init();
    flash_storage_init();
    tusb_init();

    diamond_tracker_t tracker;
    diamond_tracker_init(&tracker);

    persistent_storage_t storage;
    bool flash_ok = flash_storage_load(&storage);
    if (flash_ok && storage.has_calibration) {
        if (isfinite(storage.calib.scale_x) && isfinite(storage.calib.scale_y) &&
            isfinite(storage.calib.offset_x) && isfinite(storage.calib.offset_y) &&
            storage.calib.scale_x > 0.0f && storage.calib.scale_y > 0.0f) {
            diamond_tracker_set_calibration(&tracker,
                                            storage.calib.scale_x,
                                            storage.calib.scale_y,
                                            storage.calib.offset_x,
                                            storage.calib.offset_y);
            if (storage.calib.ref_diag_h > DIAMOND_MIN_SPAN_PX &&
                storage.calib.ref_diag_v > DIAMOND_MIN_SPAN_PX) {
                tracker.diag_h = storage.calib.ref_diag_h;
                tracker.diag_v = storage.calib.ref_diag_v;
            }
            printf("Loaded saved calibration from Flash.\n");
        } else {
            printf("Ignored invalid/negative saved calibration from Flash; using identity.\n");
        }
    }

    one_euro_2d_t filter;
    one_euro_2d_init(&filter, ONE_EURO_MIN_CUTOFF, ONE_EURO_BETA, ONE_EURO_DERIV_CUTOFF);

    usb_hid_init();
    usb_hid_set_filter_instance(&filter);
    if (flash_ok && storage.has_keymap) {
        if (usb_hid_set_keymap(&storage.keymap)) {
            printf("Loaded saved keymap & filter parameters from Flash.\n");
        }
    }

    wiimote_bt_init();

    bool was_connected = false;
    uint16_t prev_buttons = 0;
    uint32_t last_report_ms = 0;

    bool calib_combo_active = false;
    bool calib_combo_triggered = false;
    uint32_t calib_combo_start_ms = 0;

    bool calib_blink_on = false;
    feedback_t fb = { FB_NONE, 0U, 0xFFU, false };
    uint32_t last_blink_ms = 0;

    uint16_t last_usb_x = USB_HID_ABS_MAX / 2U;
    uint16_t last_usb_y = USB_HID_ABS_MAX / 2U;

    watchdog_enable(2000, 1);

    while (true) {
        watchdog_update();
        tud_task();
        wiimote_bt_task();

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        bool connected = wiimote_bt_is_connected();

        if (!connected && was_connected) {
            usb_hid_process_wiimote(0, prev_buttons, last_usb_x, last_usb_y, false, now_ms);
            prev_buttons = 0;
            last_report_ms = 0;
            calib_combo_active = false;
            calib_combo_triggered = false;
            diamond_tracker_cancel_calibration(&tracker);
            one_euro_2d_reset(&filter);
            fb.kind = FB_NONE;
        }
        was_connected = connected;

        /* Keep calibration LED blink ticking even between IR packets */
        bool fb_active = connected && feedback_tick(&fb, now_ms);
        if (connected && !fb_active && tracker.calib_state != CALIB_IDLE) {
            if ((now_ms - last_blink_ms) >= 250U) {
                last_blink_ms = now_ms;
                calib_blink_on = !calib_blink_on;
                wiimote_bt_set_leds(calib_blink_on ? calib_step_led(tracker.calib_state)
                                                   : WIIMOTE_LED_NONE, false);
            }
        }

        wiimote_report_t rep;
        while (wiimote_bt_pop_report(&rep)) {
            uint32_t rep_ms = (rep.timestamp_ms != 0U) ? rep.timestamp_ms : now_ms;

            /* 1. Detect '+' and '-' held simultaneously for 3 seconds */
            bool both_pm = ((rep.buttons & WIIMOTE_BTN_PLUS) != 0U) &&
                           ((rep.buttons & WIIMOTE_BTN_MINUS) != 0U);
            if (both_pm) {
                if (!calib_combo_active) {
                    calib_combo_active = true;
                    calib_combo_start_ms = rep_ms;
                } else if (!calib_combo_triggered &&
                           (rep_ms - calib_combo_start_ms) >= CALIB_HOLD_TIME_MS) {
                    calib_combo_triggered = true;
                    if (tracker.calib_state == CALIB_IDLE) {
                        diamond_tracker_start_calibration(&tracker);
                        calib_blink_on = true;
                        last_blink_ms = now_ms;
                        wiimote_bt_set_leds(calib_step_led(tracker.calib_state), false);
                        printf("Entered 2-point calibration mode (Step 1: Top-Left).\n");
                    } else {
                        diamond_tracker_cancel_calibration(&tracker);
                        feedback_start(&fb, FB_CANCEL, now_ms);
                        printf("Calibration cancelled.\n");
                    }
                }
            } else {
                calib_combo_active = false;
                calib_combo_triggered = false;
            }

            /* 2. Run 4->3->2->1/0 diamond tracking algorithm */
            diamond_tracker_update(&tracker, rep.ir,
                                   rep.accel_x, rep.accel_y, rep.accel_z);

            bool in_calib = (tracker.calib_state != CALIB_IDLE);
            uint16_t usb_x = last_usb_x;
            uint16_t usb_y = last_usb_y;

            if (in_calib) {
                bool a_rising = ((rep.buttons & WIIMOTE_BTN_A) != 0U) &&
                                ((prev_buttons & WIIMOTE_BTN_A) == 0U);
                if (a_rising && rep.visible_ir_count >= 2U) {
                    bool finished = false;
                    bool ok = diamond_tracker_calibration_confirm_step(&tracker, &finished);
                    if (finished && ok) {
                        flash_calibration_t cal;
                        diamond_tracker_get_calibration(&tracker,
                                                        &cal.scale_x, &cal.scale_y,
                                                        &cal.offset_x, &cal.offset_y);
                        cal.ref_diag_h = tracker.diag_h;
                        cal.ref_diag_v = tracker.diag_v;
                        bool saved = flash_storage_save_calibration(&cal);

                        one_euro_2d_reset(&filter);
                        in_calib = false;
                        if (saved) {
                            feedback_start(&fb, FB_SUCCESS, now_ms);
                            printf("Calibration complete and saved to Flash.\n");
                        } else {
                            /* Active for this session, but lost on reboot: warn the user. */
                            feedback_start(&fb, FB_ERROR, now_ms);
                            printf("Calibration applied but Flash write FAILED.\n");
                        }
                    } else if (!ok) {
                        feedback_start(&fb, FB_ERROR, now_ms);
                        in_calib = false;
                        printf("Calibration sample degenerate; restored previous settings.\n");
                    } else {
                        feedback_start(&fb, FB_STEP_OK, now_ms);
                        calib_blink_on = true;
                        last_blink_ms = now_ms;
                        printf("Calibration Step 1 confirmed (Step 2: Bottom-Right).\n");
                    }
                }
                usb_x = tracker.usb_x;
                usb_y = tracker.usb_y;
            } else {
                float dt = 1.0f / ONE_EURO_FREQ_HZ;
                if (last_report_ms != 0U && rep_ms > last_report_ms) {
                    uint32_t delta_ms = rep_ms - last_report_ms;
                    if (delta_ms > 250U) {
                        one_euro_2d_reset(&filter);
                    } else {
                        dt = (float)delta_ms * 0.001f;
                        if (dt < 0.002f) {
                            dt = 0.002f;
                        }
                    }
                }

                float fx = tracker.cal_norm_x;
                float fy = tracker.cal_norm_y;
                if (tracker.visible_leds >= 2U) {
                    one_euro_2d_filter(&filter, tracker.cal_norm_x, tracker.cal_norm_y,
                                       dt, &fx, &fy);
                    usb_x = clamp_norm_to_usb(fx);
                    usb_y = clamp_norm_to_usb(fy);
                } else {
                    /* Freeze cursor cleanly when <2 IR LEDs are visible */
                    usb_x = last_usb_x;
                    usb_y = last_usb_y;
                }
            }

            last_report_ms = rep_ms;
            last_usb_x = usb_x;
            last_usb_y = usb_y;

            usb_hid_process_wiimote(rep.buttons, prev_buttons,
                                    usb_x, usb_y, in_calib, rep_ms);
            prev_buttons = rep.buttons;
        }

        usb_hid_task();
    }

    return 0;
}
