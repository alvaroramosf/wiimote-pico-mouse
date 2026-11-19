#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "diamond_tracker.h"
#include "flash_storage.h"
#include "ir_leds.h"
#include "one_euro_filter.h"
#include "tusb.h"
#include "usb_hid.h"
#include "wiimote_bt.h"

/* Declarations from usb_descriptors.c */
const uint8_t *tud_descriptor_device_cb(void);
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance);
const uint8_t *tud_descriptor_configuration_cb(uint8_t index);
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid);

#define ASSERT_TRUE(cond)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            return 1;                                                            \
        }                                                                        \
    } while (0)

#define ASSERT_NEAR(actual, expected, tol)                                       \
    do {                                                                         \
        float _a = (actual);                                                     \
        float _e = (expected);                                                   \
        if (fabsf(_a - _e) > (tol)) {                                            \
            fprintf(stderr, "FAIL %s:%d: got %.5f, expected %.5f (tol %.5f)\n",  \
                    __FILE__, __LINE__, _a, _e, (tol));                          \
            return 1;                                                            \
        }                                                                        \
    } while (0)

/* Simulate a pinhole camera pointing at a 4-LED diamond on a TV at (aim_x, aim_y)
 * with camera roll (radians) and optional vertical perspective foreshortening. */
static void simulate_diamond_camera(float aim_x, float aim_y,
                                    float roll_rad,
                                    float half_w_px, float half_h_px,
                                    float persp_yaw,
                                    ir_point_t out_ir[4],
                                    int16_t *out_ax, int16_t *out_az) {
    const float vx[4] = {0.5f, 1.0f, 0.5f, 0.0f}; /* Top, Right, Bottom, Left */
    const float vy[4] = {0.0f, 0.5f, 1.0f, 0.5f};

    float cr = cosf(-roll_rad);
    float sr = sinf(-roll_rad);

    for (int i = 0; i < 4; ++i) {
        /* World-aligned screen offset from aim point */
        float dx = (vx[i] - aim_x) * (2.0f * half_w_px);
        float dy = (vy[i] - aim_y) * (2.0f * half_h_px);

        /* Simulate mild 3D projective foreshortening (yaw tilt) */
        float depth = 1.0f + persp_yaw * (vx[i] - 0.5f);
        float px = dx / depth;
        float py = dy / depth;

        /* Rotate into rolled camera sensor space */
        float u = px * cr - py * sr;
        float v = px * sr + py * cr;

        float raw_x = WIIMOTE_IR_INVERT_X ? (WIIMOTE_OPTICAL_CENTER_X - u)
                                          : (WIIMOTE_OPTICAL_CENTER_X + u);
        float raw_y = WIIMOTE_IR_INVERT_Y ? (WIIMOTE_OPTICAL_CENTER_Y - v)
                                          : (WIIMOTE_OPTICAL_CENTER_Y + v);

        if (raw_x >= 0.0f && raw_x < (float)WIIMOTE_IR_WIDTH &&
            raw_y >= 0.0f && raw_y < (float)WIIMOTE_IR_HEIGHT) {
            out_ir[i].x = (uint16_t)lroundf(raw_x);
            out_ir[i].y = (uint16_t)lroundf(raw_y);
            out_ir[i].size = 3;
            out_ir[i].valid = true;
        } else {
            out_ir[i].x = 1023;
            out_ir[i].y = 1023;
            out_ir[i].size = 15;
            out_ir[i].valid = false;
        }
    }

    *out_ax = (int16_t)lroundf(100.0f * sinf(roll_rad));
    *out_az = (int16_t)lroundf(100.0f * cosf(roll_rad));
}

static int test_4_led_homography(void) {
    diamond_tracker_t t;
    diamond_tracker_init(&t);

    ir_point_t ir[4];
    int16_t ax = 0, az = 100;

    const float poses[][3] = {
        {0.50f, 0.50f,  0.00f},
        {0.20f, 0.30f,  0.00f},
        {0.85f, 0.75f,  0.35f},
        {0.15f, 0.80f, -0.45f},
        {0.50f, 0.15f,  0.20f}
    };

    for (size_t i = 0; i < sizeof(poses) / sizeof(poses[0]); ++i) {
        simulate_diamond_camera(poses[i][0], poses[i][1], poses[i][2],
                                220.0f, 130.0f, 0.15f, ir, &ax, &az);
        /* Permute dot order as PixArt camera can reorder blobs */
        ir_point_t shuffled[4] = {ir[2], ir[0], ir[3], ir[1]};
        diamond_tracker_update(&t, shuffled, ax, 0, az);

        ASSERT_TRUE(t.visible_leds == 4);
        ASSERT_NEAR(t.raw_norm_x, poses[i][0], 0.01f);
        ASSERT_NEAR(t.raw_norm_y, poses[i][1], 0.01f);
    }

    printf("[PASS] test_4_led_homography\n");
    return 0;
}

static int test_3_led_reconstruction(void) {
    diamond_tracker_t t;
    diamond_tracker_init(&t);

    ir_point_t ir[4];
    int16_t ax = 0, az = 100;

    /* Prime live geometry at center with perspective yaw */
    simulate_diamond_camera(0.50f, 0.50f, 0.10f, 220.0f, 140.0f, 0.18f, ir, &ax, &az);
    diamond_tracker_update(&t, ir, ax, 0, az);
    ASSERT_TRUE(t.visible_leds == 4);

    /* Move toward each edge so 1 of the 4 LEDs drops out of sensor FOV */
    const float edge_poses[4][2] = {
        {0.50f, 0.92f}, /* TOP leaves sensor (or moves far away) */
        {0.08f, 0.50f}, /* RIGHT leaves sensor */
        {0.50f, 0.08f}, /* BOTTOM leaves sensor */
        {0.92f, 0.50f}  /* LEFT leaves sensor */
    };

    for (int drop = 0; drop < 4; ++drop) {
        /* Re-prime near center first */
        simulate_diamond_camera(0.50f, 0.50f, 0.10f, 340.0f, 260.0f, 0.10f, ir, &ax, &az);
        diamond_tracker_update(&t, ir, ax, 0, az);

        /* Step toward edge so dropped LED naturally exits sensor FOV */
        float ex = edge_poses[drop][0];
        float ey = edge_poses[drop][1];
        simulate_diamond_camera(0.5f * (0.5f + ex), 0.5f * (0.5f + ey), 0.10f,
                                340.0f, 260.0f, 0.10f, ir, &ax, &az);
        diamond_tracker_update(&t, ir, ax, 0, az);

        simulate_diamond_camera(ex, ey, 0.10f, 340.0f, 260.0f, 0.10f, ir, &ax, &az);
        ir[drop].valid = false;
        diamond_tracker_update(&t, ir, ax, 0, az);

        ASSERT_TRUE(t.visible_leds == 3);
        ASSERT_NEAR(t.raw_norm_x, ex, 0.04f);
        ASSERT_NEAR(t.raw_norm_y, ey, 0.04f);
    }

    printf("[PASS] test_3_led_reconstruction\n");
    return 0;
}

static int test_2_led_similarity_corner(void) {
    diamond_tracker_t t;
    diamond_tracker_init(&t);

    ir_point_t ir[4];
    int16_t ax = 0, az = 100;
    float roll = 0.25f; /* ~14 deg tilt */

    /* Prime with 4 LEDs at center */
    simulate_diamond_camera(0.50f, 0.50f, roll, 420.0f, 320.0f, 0.0f, ir, &ax, &az);
    diamond_tracker_update(&t, ir, ax, 0, az);
    ASSERT_TRUE(t.visible_leds == 4);

    /* Walk smoothly toward Top-Right corner (0.90, 0.10) where Bottom & Left exit FOV */
    for (int step = 1; step <= 8; ++step) {
        float frac = (float)step / 8.0f;
        float aim_x = 0.50f + frac * 0.40f;
        float aim_y = 0.50f - frac * 0.40f;
        simulate_diamond_camera(aim_x, aim_y, roll, 420.0f, 320.0f, 0.0f, ir, &ax, &az);
        diamond_tracker_update(&t, ir, ax, 0, az);
    }

    ASSERT_TRUE(t.visible_leds == 2);
    ASSERT_NEAR(t.raw_norm_x, 0.90f, 0.03f);
    ASSERT_NEAR(t.raw_norm_y, 0.10f, 0.03f);

    printf("[PASS] test_2_led_similarity_corner\n");
    return 0;
}

static int test_1_and_0_led_freeze(void) {
    diamond_tracker_t t;
    diamond_tracker_init(&t);

    ir_point_t ir[4];
    int16_t ax = 0, az = 100;

    simulate_diamond_camera(0.72f, 0.31f, 0.0f, 200.0f, 120.0f, 0.0f, ir, &ax, &az);
    diamond_tracker_update(&t, ir, ax, 0, az);
    uint16_t saved_x = t.usb_x;
    uint16_t saved_y = t.usb_y;
    ASSERT_TRUE(t.visible_leds == 4);

    /* Leave only 1 LED visible */
    ir[0].valid = false;
    ir[1].valid = false;
    ir[2].valid = false;
    diamond_tracker_update(&t, ir, ax, 0, az);
    ASSERT_TRUE(t.visible_leds == 1);
    ASSERT_TRUE(t.usb_x == saved_x && t.usb_y == saved_y);

    /* Leave 0 LEDs visible */
    ir[3].valid = false;
    diamond_tracker_update(&t, ir, ax, 0, az);
    ASSERT_TRUE(t.visible_leds == 0);
    ASSERT_TRUE(t.usb_x == saved_x && t.usb_y == saved_y);

    printf("[PASS] test_1_and_0_led_freeze\n");
    return 0;
}

static int test_calibration_and_flash(void) {
    diamond_tracker_t t;
    diamond_tracker_init(&t);
    flash_storage_init();

    ir_point_t ir[4];
    int16_t ax = 0, az = 100;

    /* Start calibration -> cursor pinned at (10%, 10%) */
    diamond_tracker_start_calibration(&t);
    ASSERT_TRUE(t.calib_state == CALIB_STEP1_TOP_LEFT);
    ASSERT_TRUE(t.usb_x == 3277 && t.usb_y == 3277);

    /* User aims at raw (0.18, 0.20) for Top-Left 10%, 10% */
    simulate_diamond_camera(0.18f, 0.20f, 0.0f, 200.0f, 120.0f, 0.0f, ir, &ax, &az);
    diamond_tracker_update(&t, ir, ax, 0, az);
    bool finished = false;
    ASSERT_TRUE(diamond_tracker_calibration_confirm_step(&t, &finished));
    ASSERT_TRUE(!finished);
    ASSERT_TRUE(t.calib_state == CALIB_STEP2_BOTTOM_RIGHT);
    ASSERT_TRUE(t.usb_x == 29490 && t.usb_y == 29490);

    /* User aims at raw (0.82, 0.80) for Bottom-Right 90%, 90% */
    simulate_diamond_camera(0.82f, 0.80f, 0.0f, 200.0f, 120.0f, 0.0f, ir, &ax, &az);
    diamond_tracker_update(&t, ir, ax, 0, az);
    ASSERT_TRUE(diamond_tracker_calibration_confirm_step(&t, &finished));
    ASSERT_TRUE(finished);
    ASSERT_TRUE(t.calib_state == CALIB_IDLE);

    /* Persist paired MAC and calibration to Flash, then reload */
    const uint8_t mac[6] = {0x00, 0x1E, 0x35, 0xAA, 0xBB, 0xCC};
    ASSERT_TRUE(flash_storage_save_paired_addr(mac));

    flash_calibration_t cal;
    diamond_tracker_get_calibration(&t, &cal.scale_x, &cal.scale_y,
                                    &cal.offset_x, &cal.offset_y);
    cal.ref_diag_h = t.diag_h;
    cal.ref_diag_v = t.diag_v;
    ASSERT_TRUE(flash_storage_save_calibration(&cal));

    persistent_storage_t loaded;
    ASSERT_TRUE(flash_storage_load(&loaded));
    ASSERT_TRUE(loaded.has_calibration == 1);
    ASSERT_TRUE(loaded.has_paired_device == 1);
    ASSERT_TRUE(memcmp(loaded.paired_bd_addr, mac, 6) == 0);
    ASSERT_NEAR(loaded.calib.scale_x, cal.scale_x, 1e-5f);
    ASSERT_NEAR(loaded.calib.offset_y, cal.offset_y, 1e-5f);

    /* Verify calibrated tracker maps raw (0.18, 0.20) -> (0.10, 0.10) */
    diamond_tracker_t t2;
    diamond_tracker_init(&t2);
    diamond_tracker_set_calibration(&t2, loaded.calib.scale_x, loaded.calib.scale_y,
                                    loaded.calib.offset_x, loaded.calib.offset_y);
    simulate_diamond_camera(0.18f, 0.20f, 0.0f, 200.0f, 120.0f, 0.0f, ir, &ax, &az);
    diamond_tracker_update(&t2, ir, ax, 0, az);
    ASSERT_NEAR(t2.cal_norm_x, 0.10f, 0.01f);
    ASSERT_NEAR(t2.cal_norm_y, 0.10f, 0.01f);

    /* Verify CRC-32 detects tampering */
    persistent_storage_t bad = loaded;
    bad.calib.scale_x += 0.1f;
    ASSERT_TRUE(flash_compute_crc32(&bad, offsetof(persistent_storage_t, crc32)) != loaded.crc32);

    printf("[PASS] test_calibration_and_flash\n");
    return 0;
}

static int test_one_euro_filter(void) {
    one_euro_2d_t f;
    one_euro_2d_init(&f, ONE_EURO_MIN_CUTOFF, ONE_EURO_BETA, ONE_EURO_DERIV_CUTOFF);

    float fx = 0.0f, fy = 0.0f;
    float max_dev = 0.0f;

    /* Inject +/-0.015 alternating 100 Hz tremor around 0.5 */
    for (int i = 0; i < 100; ++i) {
        float jitter = (i % 2 == 0) ? 0.015f : -0.015f;
        one_euro_2d_filter(&f, 0.5f + jitter, 0.5f - jitter, 0.01f, &fx, &fy);
        if (i >= 50) {
            float dev = fabsf(fx - 0.5f);
            if (dev > max_dev) max_dev = dev;
        }
    }
    ASSERT_TRUE(max_dev < 0.004f); /* >70% jitter attenuation */

    /* Rapid sweep from 0.5 to 0.9 over 10 frames (100 ms) -> adaptive cutoff tracks quickly */
    for (int i = 1; i <= 10; ++i) {
        float target = 0.5f + 0.04f * (float)i;
        one_euro_2d_filter(&f, target, target, 0.01f, &fx, &fy);
    }
    ASSERT_TRUE(fx > 0.80f);

    printf("[PASS] test_one_euro_filter\n");
    return 0;
}

static int test_report_0x33_parser(void) {
    /* Build 19-byte 0x33 packet:
     * Buttons: B (0x04 in byte 3), A (0x08 in byte 3), PLUS (0x10 in byte 2)
     * Accel: ax = 662 (+150), ay = 492 (-20), az = 612 (+100)
     * IR0 = (312, 412, size=5), IR1 = (800, 120, size=4), IR2 = invalid, IR3 = (512, 700, size=2)
     */
    uint16_t raw_ax = 512 + 150; /* 662 = 0x296 -> >>2 = 0xA5, lsb = 2 */
    uint16_t raw_ay = 512 - 20;  /* 492 = 0x1EC -> >>2 = 0x7B, lsb bit1 = 0 */
    uint16_t raw_az = 512 + 100; /* 612 = 0x264 -> >>2 = 0x99, lsb bit1 = 0 */

    uint8_t pkt[19];
    memset(pkt, 0xFF, sizeof(pkt));
    pkt[0] = 0xA1;
    pkt[1] = 0x33;
    pkt[2] = (uint8_t)(0x10U | ((raw_ax & 0x03U) << 5));
    pkt[3] = (uint8_t)(0x04U | 0x08U | ((raw_ay & 0x02U) << 5) | ((raw_az & 0x02U) << 4));
    pkt[4] = (uint8_t)(raw_ax >> 2);
    pkt[5] = (uint8_t)(raw_ay >> 2);
    pkt[6] = (uint8_t)(raw_az >> 2);

    /* IR0: (312, 412, size=5) */
    pkt[7]  = (uint8_t)(312 & 0xFFU);
    pkt[8]  = (uint8_t)(412 & 0xFFU);
    pkt[9]  = (uint8_t)(((312 >> 4) & 0x30U) | ((412 >> 2) & 0xC0U) | 5U);

    /* IR1: (800, 120, size=4) */
    pkt[10] = (uint8_t)(800 & 0xFFU);
    pkt[11] = (uint8_t)(120 & 0xFFU);
    pkt[12] = (uint8_t)(((800 >> 4) & 0x30U) | ((120 >> 2) & 0xC0U) | 4U);

    /* IR2: empty (0xFF, 0xFF, 0xFF) */
    pkt[13] = 0xFF;
    pkt[14] = 0xFF;
    pkt[15] = 0xFF;

    /* IR3: (512, 700, size=2) */
    pkt[16] = (uint8_t)(512 & 0xFFU);
    pkt[17] = (uint8_t)(700 & 0xFFU);
    pkt[18] = (uint8_t)(((512 >> 4) & 0x30U) | ((700 >> 2) & 0xC0U) | 2U);

    wiimote_report_t rep;
    ASSERT_TRUE(wiimote_parse_report_0x33(pkt, sizeof(pkt), &rep));
    ASSERT_TRUE(rep.buttons == (WIIMOTE_BTN_PLUS | WIIMOTE_BTN_B | WIIMOTE_BTN_A));
    ASSERT_TRUE(rep.accel_x == 150);
    ASSERT_TRUE(rep.accel_y == -20);
    ASSERT_TRUE(rep.accel_z == 100);
    ASSERT_TRUE(rep.visible_ir_count == 3);
    ASSERT_TRUE(rep.ir[0].valid && rep.ir[0].x == 312 && rep.ir[0].y == 412 && rep.ir[0].size == 5);
    ASSERT_TRUE(rep.ir[1].valid && rep.ir[1].x == 800 && rep.ir[1].y == 120 && rep.ir[1].size == 4);
    ASSERT_TRUE(!rep.ir[2].valid);
    ASSERT_TRUE(rep.ir[3].valid && rep.ir[3].x == 512 && rep.ir[3].y == 700 && rep.ir[3].size == 2);

    /* Reject truncated or wrong report ID */
    ASSERT_TRUE(!wiimote_parse_report_0x33(pkt, 18, &rep));
    pkt[1] = 0x30;
    ASSERT_TRUE(!wiimote_parse_report_0x33(pkt, 19, &rep));

    printf("[PASS] test_report_0x33_parser\n");
    return 0;
}

static int test_usb_hid_and_ir_leds(void) {
    ir_leds_init();
    ASSERT_TRUE(!ir_leds_is_enabled());
    ir_leds_set_enabled(true);
    ASSERT_TRUE(ir_leds_is_enabled());
    ir_leds_set_enabled(false);
    ASSERT_TRUE(!ir_leds_is_enabled());

    usb_hid_init();

    /* B + Up + Home + 2 */
    uint16_t btns = WIIMOTE_BTN_B | WIIMOTE_BTN_UP | WIIMOTE_BTN_HOME | WIIMOTE_BTN_TWO;
    usb_hid_process_wiimote(btns, 0, 12345, 22222, false, 100);
    wiimote_abs_mouse_report_t m = usb_hid_test_get_last_mouse();
    ASSERT_TRUE(m.buttons == HID_MOUSE_BTN_LEFT);
    ASSERT_TRUE(m.x == 12345 && m.y == 22222);
    ASSERT_TRUE(m.wheel == 1);

    /* Consumer Play/Pause + release */
    ASSERT_TRUE(usb_hid_test_pop_consumer() == HID_CONSUMER_PLAY_PAUSE);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == 0x0000U);

    /* Keyboard Super/GUI key + release */
    bool kb_ok = false;
    wiimote_keyboard_report_t kb = usb_hid_test_pop_keyboard(&kb_ok);
    ASSERT_TRUE(kb_ok && kb.modifier == 0x08U);
    kb = usb_hid_test_pop_keyboard(&kb_ok);
    ASSERT_TRUE(kb_ok && kb.modifier == 0x00U);

    /* Short '+' tap -> Volume Increment */
    usb_hid_process_wiimote(WIIMOTE_BTN_PLUS, 0, 1000, 1000, false, 200);
    usb_hid_process_wiimote(0, WIIMOTE_BTN_PLUS, 1000, 1000, false, 350);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == HID_CONSUMER_VOLUME_INCREMENT);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == 0x0000U);

    /* Holding '+' and '-' together for calibration must NOT emit volume events */
    usb_hid_process_wiimote(WIIMOTE_BTN_PLUS | WIIMOTE_BTN_MINUS, 0, 1000, 1000, false, 500);
    usb_hid_process_wiimote(0, WIIMOTE_BTN_PLUS | WIIMOTE_BTN_MINUS, 1000, 1000, false, 3600);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == 0xFFFFU);

    /* Verify USB descriptors are valid */
    ASSERT_TRUE(tud_descriptor_device_cb()[0] == sizeof(tusb_desc_device_t));
    ASSERT_TRUE(tud_hid_descriptor_report_cb(0)[0] == 0x05);
    ASSERT_TRUE(tud_descriptor_configuration_cb(0)[1] == TUSB_DESC_CONFIGURATION);
    ASSERT_TRUE((tud_descriptor_string_cb(2, 0x0409)[0] & 0xFFU) > 2U);

    printf("[PASS] test_usb_hid_and_ir_leds\n");
    return 0;
}

static int test_webhid_configurator_and_keymap(void) {
    flash_storage_test_reset();
    flash_storage_init();
    usb_hid_init();

    one_euro_2d_t filter;
    one_euro_2d_init(&filter, ONE_EURO_MIN_CUTOFF, ONE_EURO_BETA, ONE_EURO_DERIV_CUTOFF);
    usb_hid_set_filter_instance(&filter);

    /* 1. Verify dual HID descriptors (ITF 0 Composite + ITF 1 Vendor WebHID 0xFF00) */
    const uint8_t *cfg_desc = tud_descriptor_configuration_cb(0);
    uint16_t total_len = (uint16_t)(cfg_desc[2] | ((uint16_t)cfg_desc[3] << 8));
    ASSERT_TRUE(total_len == (TUD_CONFIG_DESC_LEN + 2U * TUD_HID_DESC_LEN));
    ASSERT_TRUE(cfg_desc[4] == 2U); /* bNumInterfaces == 2 */

    const uint8_t *rep0 = tud_hid_descriptor_report_cb(ITF_NUM_HID);
    const uint8_t *rep1 = tud_hid_descriptor_report_cb(ITF_NUM_HID_CFG);
    ASSERT_TRUE(rep0[0] == 0x05U); /* Generic Desktop */
    ASSERT_TRUE(rep1[0] == 0x06U && rep1[1] == 0x00U && rep1[2] == 0xFFU); /* Usage Page 0xFF00 */
    ASSERT_TRUE(rep1[3] == 0x09U && rep1[4] == 0x01U); /* Usage 0x01 */
    ASSERT_TRUE(rep1[7] == 0x85U && rep1[8] == REPORT_ID_WEBHID_CFG); /* Report ID 4 */

    /* 2. Seed calibration and paired MAC to verify keymap saves preserve them */
    const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    flash_calibration_t cal = {1.25f, 1.35f, -0.10f, 0.05f, 300.0f, 220.0f};
    ASSERT_TRUE(flash_storage_save_paired_addr(mac));
    ASSERT_TRUE(flash_storage_save_calibration(&cal));

    /* 3. Verify GET_REPORT on ITF_NUM_HID_CFG, Report ID 4 */
    uint8_t get_buf[WEBHID_CFG_REPORT_SIZE];
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID, REPORT_ID_WEBHID_CFG,
                                      HID_REPORT_TYPE_FEATURE, get_buf, sizeof(get_buf)) == 0U);
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_ABS_MOUSE,
                                      HID_REPORT_TYPE_FEATURE, get_buf, sizeof(get_buf)) == 0U);
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                                      HID_REPORT_TYPE_INPUT, get_buf, sizeof(get_buf)) == 0U);
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                                      HID_REPORT_TYPE_FEATURE, get_buf, 63U) == 0U);

    uint16_t got_len = tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                                             HID_REPORT_TYPE_FEATURE, get_buf, sizeof(get_buf));
    ASSERT_TRUE(got_len == WEBHID_CFG_REPORT_SIZE);

    webhid_cfg_report_t rx_rep;
    memcpy(&rx_rep, get_buf, sizeof(rx_rep));
    ASSERT_TRUE(rx_rep.magic == WEBHID_CFG_MAGIC);
    ASSERT_TRUE(rx_rep.fw_version == WEBHID_FIRMWARE_VERSION);
    ASSERT_TRUE(rx_rep.crc32 == flash_compute_crc32(&rx_rep, offsetof(webhid_cfg_report_t, crc32)));
    ASSERT_NEAR(rx_rep.keymap.min_cutoff, ONE_EURO_MIN_CUTOFF, 1e-5f);
    ASSERT_NEAR(rx_rep.keymap.beta, ONE_EURO_BETA, 1e-5f);
    ASSERT_TRUE(rx_rep.keymap.scroll_interval_ms == SCROLL_INTERVAL_MS);

    /* 4. Remap keymap + filter parameters via SET_REPORT (65-byte buffer with leading Report ID) */
    webhid_cfg_report_t tx_rep = rx_rep;
    tx_rep.cmd = WEBHID_CMD_SET_KEYMAP;
    tx_rep.keymap.bindings[WIIMOTE_IDX_B] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_RIGHT
    };
    tx_rep.keymap.bindings[WIIMOTE_IDX_A] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_LEFT
    };
    tx_rep.keymap.bindings[WIIMOTE_IDX_ONE] = (button_binding_t){
        ACTION_KEYBOARD_KEY, BINDING_FLAG_HOLD, 0xE1U /* Left Shift held */
    };
    tx_rep.keymap.bindings[WIIMOTE_IDX_TWO] = (button_binding_t){
        ACTION_KEYBOARD_KEY, BINDING_FLAG_NONE, 0x04U /* 'A' tap */
    };
    tx_rep.keymap.bindings[WIIMOTE_IDX_UP] = (button_binding_t){
        ACTION_MOUSE_WHEEL, BINDING_FLAG_REPEAT, 2U
    };
    tx_rep.keymap.bindings[WIIMOTE_IDX_DOWN] = (button_binding_t){
        ACTION_NONE, BINDING_FLAG_NONE, 0U
    };
    tx_rep.keymap.bindings[WIIMOTE_IDX_HOME] = (button_binding_t){
        ACTION_CONSUMER_MEDIA, BINDING_FLAG_NONE, 0x00E2U /* Mute */
    };
    tx_rep.keymap.min_cutoff = 0.75f;
    tx_rep.keymap.beta = 6.50f;
    tx_rep.keymap.scroll_interval_ms = 20U;
    tx_rep.crc32 = flash_compute_crc32(&tx_rep, offsetof(webhid_cfg_report_t, crc32));

    uint8_t prefixed_buf[WEBHID_CFG_REPORT_SIZE + 1U];
    prefixed_buf[0] = REPORT_ID_WEBHID_CFG;
    memcpy(&prefixed_buf[1], &tx_rep, sizeof(tx_rep));
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, prefixed_buf, sizeof(prefixed_buf));

    /* Request reports "save pending" until the main loop services it */
    ASSERT_TRUE(usb_hid_get_cfg_status() == WEBHID_STATUS_SAVE_PENDING);
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                                      HID_REPORT_TYPE_FEATURE, get_buf, sizeof(get_buf)) == WEBHID_CFG_REPORT_SIZE);
    ASSERT_TRUE(get_buf[1] == WEBHID_STATUS_SAVE_PENDING);

    /* Live 1-Euro filter update check */
    ASSERT_NEAR(filter.x.min_cutoff, 0.75f, 1e-5f);
    ASSERT_NEAR(filter.y.min_cutoff, 0.75f, 1e-5f);
    ASSERT_NEAR(filter.x.beta, 6.50f, 1e-5f);
    ASSERT_NEAR(filter.y.beta, 6.50f, 1e-5f);

    /* Flash write is deferred: not persisted yet, then persisted after servicing */
    persistent_storage_t persisted;
    ASSERT_TRUE(flash_storage_load(&persisted));
    ASSERT_TRUE(persisted.has_keymap == 0U);
    usb_hid_task();
    ASSERT_TRUE(usb_hid_get_cfg_status() == WEBHID_STATUS_OK);
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                                      HID_REPORT_TYPE_FEATURE, get_buf, sizeof(get_buf)) == WEBHID_CFG_REPORT_SIZE);
    ASSERT_TRUE(get_buf[1] == WEBHID_STATUS_OK);

    /* Flash persistence check + preserved calibration/MAC */
    ASSERT_TRUE(flash_storage_load(&persisted));
    ASSERT_TRUE(persisted.has_keymap == 1U);
    ASSERT_TRUE(persisted.has_calibration == 1U);
    ASSERT_TRUE(persisted.has_paired_device == 1U);
    ASSERT_TRUE(memcmp(persisted.paired_bd_addr, mac, 6) == 0);
    ASSERT_NEAR(persisted.calib.scale_x, 1.25f, 1e-5f);
    ASSERT_NEAR(persisted.keymap.min_cutoff, 0.75f, 1e-5f);
    ASSERT_NEAR(persisted.keymap.beta, 6.50f, 1e-5f);
    ASSERT_TRUE(persisted.keymap.scroll_interval_ms == 20U);

    /* 4b. Error reporting: bad length, bad magic, bad CRC, invalid values */
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, prefixed_buf, 10U);
    ASSERT_TRUE(usb_hid_get_cfg_status() == WEBHID_STATUS_BAD_FRAME);

    webhid_cfg_report_t bad = tx_rep;
    bad.magic = 0x00U;
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, (const uint8_t *)&bad, sizeof(bad));
    ASSERT_TRUE(usb_hid_get_cfg_status() == WEBHID_STATUS_BAD_FRAME);

    bad = tx_rep;
    bad.crc32 ^= 1U;
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, (const uint8_t *)&bad, sizeof(bad));
    ASSERT_TRUE(usb_hid_get_cfg_status() == WEBHID_STATUS_BAD_CRC);

    bad = tx_rep;
    bad.keymap.min_cutoff = 9999.0f;
    bad.crc32 = flash_compute_crc32(&bad, offsetof(webhid_cfg_report_t, crc32));
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, (const uint8_t *)&bad, sizeof(bad));
    ASSERT_TRUE(usb_hid_get_cfg_status() == WEBHID_STATUS_INVALID_CONFIG);
    ASSERT_NEAR(filter.x.min_cutoff, 0.75f, 1e-5f); /* rejected config not applied */

    /* 5. Verify remapped runtime actions */
    /* B -> Right Click; A -> Left Click; UP -> +2 wheel; HOME -> Mute */
    usb_hid_process_wiimote(WIIMOTE_BTN_B | WIIMOTE_BTN_UP | WIIMOTE_BTN_HOME,
                            0, 4000, 5000, false, 100);
    wiimote_abs_mouse_report_t m = usb_hid_test_get_last_mouse();
    ASSERT_TRUE(m.buttons == HID_MOUSE_BTN_RIGHT);
    ASSERT_TRUE(m.wheel == 2);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == 0x00E2U);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == 0x0000U);

    /* Custom scroll interval (20 ms): no tick at +10ms, tick at +20ms */
    usb_hid_process_wiimote(WIIMOTE_BTN_UP | WIIMOTE_BTN_DOWN, WIIMOTE_BTN_B | WIIMOTE_BTN_UP | WIIMOTE_BTN_HOME,
                            4000, 5000, false, 110);
    m = usb_hid_test_get_last_mouse();
    ASSERT_TRUE(m.wheel == 0);
    usb_hid_process_wiimote(WIIMOTE_BTN_UP | WIIMOTE_BTN_DOWN, WIIMOTE_BTN_UP | WIIMOTE_BTN_DOWN,
                            4000, 5000, false, 120);
    m = usb_hid_test_get_last_mouse();
    ASSERT_TRUE(m.wheel == 2);

    /* Release UP/DOWN, test A normal click vs calibration A suppression */
    usb_hid_process_wiimote(WIIMOTE_BTN_A, WIIMOTE_BTN_UP | WIIMOTE_BTN_DOWN, 4000, 5000, false, 130);
    m = usb_hid_test_get_last_mouse();
    ASSERT_TRUE(m.buttons == HID_MOUSE_BTN_LEFT);

    usb_hid_process_wiimote(WIIMOTE_BTN_A, 0, 4000, 5000, true, 140);
    m = usb_hid_test_get_last_mouse();
    ASSERT_TRUE(m.buttons == 0U);

    /* Held keyboard key (1 -> Left Shift) + tap key (2 -> 'A') */
    usb_hid_process_wiimote(WIIMOTE_BTN_ONE, 0, 4000, 5000, false, 200);
    usb_hid_process_wiimote(WIIMOTE_BTN_ONE | WIIMOTE_BTN_TWO, WIIMOTE_BTN_ONE, 4000, 5000, false, 210);
    usb_hid_process_wiimote(0, WIIMOTE_BTN_ONE | WIIMOTE_BTN_TWO, 4000, 5000, false, 220);

    bool kb_ok = false;
    wiimote_keyboard_report_t kb = usb_hid_test_pop_keyboard(&kb_ok);
    ASSERT_TRUE(kb_ok && kb.modifier == 0x02U && kb.keycode[0] == 0x00U); /* Hold Shift */
    kb = usb_hid_test_pop_keyboard(&kb_ok);
    ASSERT_TRUE(kb_ok && kb.modifier == 0x02U && kb.keycode[0] == 0x04U); /* Press A while Shift held */
    kb = usb_hid_test_pop_keyboard(&kb_ok);
    ASSERT_TRUE(kb_ok && kb.modifier == 0x02U && kb.keycode[0] == 0x00U); /* Release A, keep Shift */
    kb = usb_hid_test_pop_keyboard(&kb_ok);
    ASSERT_TRUE(kb_ok && kb.modifier == 0x00U && kb.keycode[0] == 0x00U); /* Release Shift */

    /* '+' and '-' calibration combo still suppresses volume taps */
    usb_hid_process_wiimote(WIIMOTE_BTN_PLUS | WIIMOTE_BTN_MINUS, 0, 4000, 5000, false, 300);
    usb_hid_process_wiimote(0, WIIMOTE_BTN_PLUS | WIIMOTE_BTN_MINUS, 4000, 5000, false, 3400);
    ASSERT_TRUE(usb_hid_test_pop_consumer() == 0xFFFFU);

    /* 6. Reject corrupted CRC-32 and out-of-bounds values */
    webhid_cfg_report_t bad_rep = tx_rep;
    bad_rep.keymap.min_cutoff = 99.0f; /* out of bounds [0.05..20.0] */
    bad_rep.crc32 = flash_compute_crc32(&bad_rep, offsetof(webhid_cfg_report_t, crc32));
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, (const uint8_t *)&bad_rep, sizeof(bad_rep));

    bad_rep = tx_rep;
    bad_rep.crc32 ^= 0x12345678U; /* bad CRC */
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, (const uint8_t *)&bad_rep, sizeof(bad_rep));

    keymap_config_t cur_km;
    usb_hid_get_keymap(&cur_km);
    ASSERT_NEAR(cur_km.min_cutoff, 0.75f, 1e-5f); /* Unchanged */

    /* 7. Reset to Defaults via WEBHID_CMD_RESET_DEF */
    webhid_cfg_report_t rst_rep;
    memset(&rst_rep, 0, sizeof(rst_rep));
    rst_rep.magic = WEBHID_CFG_MAGIC;
    rst_rep.cmd = WEBHID_CMD_RESET_DEF;
    rst_rep.fw_version = WEBHID_FIRMWARE_VERSION;
    rst_rep.crc32 = flash_compute_crc32(&rst_rep, offsetof(webhid_cfg_report_t, crc32));
    tud_hid_set_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                          HID_REPORT_TYPE_FEATURE, (const uint8_t *)&rst_rep, sizeof(rst_rep));
    usb_hid_task(); /* services the deferred Flash save */

    ASSERT_NEAR(filter.x.min_cutoff, ONE_EURO_MIN_CUTOFF, 1e-5f);
    ASSERT_NEAR(filter.x.beta, ONE_EURO_BETA, 1e-5f);
    ASSERT_TRUE(tud_hid_get_report_cb(ITF_NUM_HID_CFG, REPORT_ID_WEBHID_CFG,
                                      HID_REPORT_TYPE_FEATURE, get_buf, sizeof(get_buf)) == WEBHID_CFG_REPORT_SIZE);
    memcpy(&rx_rep, get_buf, sizeof(rx_rep));
    ASSERT_TRUE(rx_rep.keymap.bindings[WIIMOTE_IDX_B].code == HID_MOUSE_BTN_LEFT);
    ASSERT_TRUE(rx_rep.keymap.scroll_interval_ms == SCROLL_INTERVAL_MS);

    ASSERT_TRUE(flash_storage_load(&persisted));
    ASSERT_TRUE(persisted.has_calibration == 1U);
    ASSERT_TRUE(persisted.has_paired_device == 1U);
    ASSERT_NEAR(persisted.keymap.min_cutoff, ONE_EURO_MIN_CUTOFF, 1e-5f);

    /* 8. Verify Flash v1 -> v2 migration preserves calibration & MAC with default keymap */
    typedef struct {
        uint32_t magic;
        uint32_t version;
        uint8_t  has_calibration;
        uint8_t  has_paired_device;
        uint8_t  paired_bd_addr[6];
        flash_calibration_t calib;
        uint32_t crc32;
    } test_v1_storage_t;

    test_v1_storage_t v1;
    memset(&v1, 0, sizeof(v1));
    v1.magic = FLASH_STORAGE_MAGIC;
    v1.version = FLASH_STORAGE_VERSION_V1;
    v1.has_calibration = 1U;
    v1.has_paired_device = 1U;
    memcpy(v1.paired_bd_addr, mac, 6);
    v1.calib = cal;
    v1.crc32 = flash_compute_crc32(&v1, offsetof(test_v1_storage_t, crc32));
    flash_storage_test_write_raw(&v1, sizeof(v1));

    persistent_storage_t migrated;
    ASSERT_TRUE(flash_storage_load(&migrated));
    ASSERT_TRUE(migrated.version == FLASH_STORAGE_VERSION);
    ASSERT_TRUE(migrated.has_calibration == 1U);
    ASSERT_TRUE(migrated.has_paired_device == 1U);
    ASSERT_TRUE(memcmp(migrated.paired_bd_addr, mac, 6) == 0);
    ASSERT_NEAR(migrated.calib.scale_y, 1.35f, 1e-5f);
    ASSERT_TRUE(migrated.has_keymap == 0U);
    ASSERT_NEAR(migrated.keymap.min_cutoff, ONE_EURO_MIN_CUTOFF, 1e-5f);
    ASSERT_TRUE(migrated.keymap.bindings[WIIMOTE_IDX_B].code == HID_MOUSE_BTN_LEFT);

    printf("[PASS] test_webhid_configurator_and_keymap\n");
    return 0;
}

int main(void) {
    if (test_4_led_homography() != 0) return 1;
    if (test_3_led_reconstruction() != 0) return 1;
    if (test_2_led_similarity_corner() != 0) return 1;
    if (test_1_and_0_led_freeze() != 0) return 1;
    if (test_calibration_and_flash() != 0) return 1;
    if (test_one_euro_filter() != 0) return 1;
    if (test_report_0x33_parser() != 0) return 1;
    if (test_usb_hid_and_ir_leds() != 0) return 1;
    if (test_webhid_configurator_and_keymap() != 0) return 1;

    printf("All 9 host verification suites PASSED.\n");
    return 0;
}
