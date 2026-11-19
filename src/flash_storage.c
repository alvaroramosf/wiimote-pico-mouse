#include "flash_storage.h"
#include "config.h"
#include "usb_hid.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t  has_calibration;
    uint8_t  has_paired_device;
    uint8_t  paired_bd_addr[6];
    flash_calibration_t calib;
    uint32_t crc32;
} persistent_storage_v1_t;

#ifdef PICO_ON_DEVICE
#include "hardware/flash.h"
#include "pico/flash.h"

/* Reserve the 4th sector from the end of Flash (16 KiB below top):
 * - Last sector on RP2350 may be reserved for RP2350-E10 boot metadata.
 * - Previous 2 sectors are used by BTstack TLV link-key bank.
 * Using -4 * FLASH_SECTOR_SIZE guarantees zero collision on Pico W & Pico 2 W. */
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - (4U * FLASH_SECTOR_SIZE))
#define FLASH_WRITE_TIMEOUT_MS 1000U

typedef struct {
    uint32_t offset;
    uint8_t page_buf[FLASH_PAGE_SIZE];
} flash_write_ctx_t;

static void flash_erase_and_program_cb(void *param) {
    const flash_write_ctx_t *ctx = (const flash_write_ctx_t *)param;
    flash_range_erase(ctx->offset, FLASH_SECTOR_SIZE);
    flash_range_program(ctx->offset, ctx->page_buf, FLASH_PAGE_SIZE);
}
#else
/* Host simulation backing store for unit tests */
static persistent_storage_t s_host_flash_sim;
static bool s_host_flash_programmed = false;
#endif

void flash_keymap_get_defaults(keymap_config_t *out) {
    if (!out) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->bindings[WIIMOTE_IDX_B] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_LEFT
    };
    out->bindings[WIIMOTE_IDX_A] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_RIGHT
    };
    out->bindings[WIIMOTE_IDX_ONE] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_MIDDLE
    };
    out->bindings[WIIMOTE_IDX_TWO] = (button_binding_t){
        ACTION_KEYBOARD_KEY, BINDING_FLAG_NONE, WIIMOTE_BTN_2_KEYCODE
    };
    out->bindings[WIIMOTE_IDX_UP] = (button_binding_t){
        ACTION_MOUSE_WHEEL, BINDING_FLAG_REPEAT, (uint16_t)(int16_t)(SCROLL_STEP_AMOUNT)
    };
    out->bindings[WIIMOTE_IDX_DOWN] = (button_binding_t){
        ACTION_MOUSE_WHEEL, BINDING_FLAG_REPEAT, (uint16_t)(int16_t)(-SCROLL_STEP_AMOUNT)
    };
    out->bindings[WIIMOTE_IDX_LEFT] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_BACK
    };
    out->bindings[WIIMOTE_IDX_RIGHT] = (button_binding_t){
        ACTION_MOUSE_BTN, BINDING_FLAG_HOLD, HID_MOUSE_BTN_FORWARD
    };
    out->bindings[WIIMOTE_IDX_PLUS] = (button_binding_t){
        ACTION_CONSUMER_MEDIA, BINDING_FLAG_SHORT_PRESS, HID_CONSUMER_VOLUME_INCREMENT
    };
    out->bindings[WIIMOTE_IDX_MINUS] = (button_binding_t){
        ACTION_CONSUMER_MEDIA, BINDING_FLAG_SHORT_PRESS, HID_CONSUMER_VOLUME_DECREMENT
    };
    out->bindings[WIIMOTE_IDX_HOME] = (button_binding_t){
        ACTION_CONSUMER_MEDIA, BINDING_FLAG_NONE, HID_CONSUMER_PLAY_PAUSE
    };

    out->min_cutoff = ONE_EURO_MIN_CUTOFF;
    out->beta = ONE_EURO_BETA;
    out->scroll_interval_ms = SCROLL_INTERVAL_MS;
    out->reserved = 0U;
}

bool flash_keymap_validate(const keymap_config_t *km) {
    if (!km) {
        return false;
    }

    float min_cutoff = 0.0f;
    float beta = 0.0f;
    uint16_t scroll_interval_ms = 0U;
    memcpy(&min_cutoff, &km->min_cutoff, sizeof(min_cutoff));
    memcpy(&beta, &km->beta, sizeof(beta));
    memcpy(&scroll_interval_ms, &km->scroll_interval_ms, sizeof(scroll_interval_ms));

    if (!isfinite(min_cutoff) ||
        min_cutoff < ONE_EURO_MIN_CUTOFF_MIN ||
        min_cutoff > ONE_EURO_MIN_CUTOFF_MAX) {
        return false;
    }
    if (!isfinite(beta) ||
        beta < ONE_EURO_BETA_MIN ||
        beta > ONE_EURO_BETA_MAX) {
        return false;
    }
    if (scroll_interval_ms < SCROLL_INTERVAL_MIN_MS ||
        scroll_interval_ms > SCROLL_INTERVAL_MAX_MS) {
        return false;
    }

    const uint8_t valid_flags = (uint8_t)(BINDING_FLAG_REPEAT |
                                          BINDING_FLAG_HOLD |
                                          BINDING_FLAG_SHORT_PRESS);

    for (size_t i = 0; i < WIIMOTE_MAP_BTN_COUNT; ++i) {
        button_binding_t b;
        memcpy(&b, &km->bindings[i], sizeof(b));

        if (b.action_type >= ACTION_TYPE_COUNT) {
            return false;
        }
        if ((b.flags & (uint8_t)(~valid_flags)) != 0U) {
            return false;
        }

        switch (b.action_type) {
        case ACTION_NONE:
            break;
        case ACTION_MOUSE_BTN:
            if (b.code == 0U || (b.code & ~0x1FU) != 0U) {
                return false;
            }
            break;
        case ACTION_MOUSE_WHEEL: {
            int16_t delta = (int16_t)b.code;
            if (delta == 0 || delta < -127 || delta > 127) {
                return false;
            }
            break;
        }
        case ACTION_KEYBOARD_KEY:
            if (b.code < 0x0004U || b.code > 0x00E7U) {
                return false;
            }
            break;
        case ACTION_CONSUMER_MEDIA:
            if (b.code == 0U || b.code > 0x03FFU) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    return true;
}

static void populate_defaults(persistent_storage_t *out) {
    memset(out, 0, sizeof(*out));
    out->magic = FLASH_STORAGE_MAGIC;
    out->version = FLASH_STORAGE_VERSION;
    out->has_calibration = 0;
    out->has_paired_device = 0;
    out->calib.scale_x = 1.0f;
    out->calib.scale_y = 1.0f;
    out->calib.offset_x = 0.0f;
    out->calib.offset_y = 0.0f;
    out->calib.ref_diag_h = DIAMOND_DEFAULT_HALF_W_PX * 2.0f;
    out->calib.ref_diag_v = DIAMOND_DEFAULT_HALF_H_PX * 2.0f;
    out->has_keymap = 0;
    flash_keymap_get_defaults(&out->keymap);
}

uint32_t flash_compute_crc32(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)bytes[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }

    return ~crc;
}

void flash_storage_init(void) {
#ifndef PICO_ON_DEVICE
    /* Host simulator preserves RAM state across load/save calls unless reset */
    if (!s_host_flash_programmed) {
        memset(&s_host_flash_sim, 0xFF, sizeof(s_host_flash_sim));
    }
#endif
}

bool flash_storage_load(persistent_storage_t *out) {
    if (!out) {
        return false;
    }

    persistent_storage_t raw;
#ifdef PICO_ON_DEVICE
    const void *flash_addr = (const void *)(XIP_BASE + FLASH_TARGET_OFFSET);
    memcpy(&raw, flash_addr, sizeof(raw));
#else
    if (!s_host_flash_programmed) {
        populate_defaults(out);
        return false;
    }
    memcpy(&raw, &s_host_flash_sim, sizeof(raw));
#endif

    if (raw.magic != FLASH_STORAGE_MAGIC) {
        populate_defaults(out);
        return false;
    }

    if (raw.version == FLASH_STORAGE_VERSION_V1) {
        persistent_storage_v1_t v1;
        memcpy(&v1, &raw, sizeof(v1));
        uint32_t expected_v1_crc = flash_compute_crc32(&v1, offsetof(persistent_storage_v1_t, crc32));
        if (v1.crc32 != expected_v1_crc) {
            populate_defaults(out);
            return false;
        }
        populate_defaults(out);
        out->has_calibration = v1.has_calibration;
        out->has_paired_device = v1.has_paired_device;
        memcpy(out->paired_bd_addr, v1.paired_bd_addr, sizeof(out->paired_bd_addr));
        out->calib = v1.calib;
        out->has_keymap = 0;
        return true;
    }

    if (raw.version != FLASH_STORAGE_VERSION) {
        populate_defaults(out);
        return false;
    }

    uint32_t expected_crc = flash_compute_crc32(&raw, offsetof(persistent_storage_t, crc32));
    if (raw.crc32 != expected_crc) {
        populate_defaults(out);
        return false;
    }

    if (!raw.has_keymap || !flash_keymap_validate(&raw.keymap)) {
        raw.has_keymap = 0;
        flash_keymap_get_defaults(&raw.keymap);
    }

    memcpy(out, &raw, sizeof(raw));
    return true;
}

bool flash_storage_save(const persistent_storage_t *in) {
    if (!in) {
        return false;
    }

    persistent_storage_t rec;
    memset(&rec, 0, sizeof(rec));
    memcpy(&rec, in, sizeof(rec));
    rec.magic = FLASH_STORAGE_MAGIC;
    rec.version = FLASH_STORAGE_VERSION;
    rec.crc32 = flash_compute_crc32(&rec, offsetof(persistent_storage_t, crc32));

#ifdef PICO_ON_DEVICE
    flash_write_ctx_t ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    ctx.offset = FLASH_TARGET_OFFSET;
    memcpy(ctx.page_buf, &rec, sizeof(rec));
    return flash_safe_execute(flash_erase_and_program_cb, &ctx, FLASH_WRITE_TIMEOUT_MS) == PICO_OK;
#else
    memcpy(&s_host_flash_sim, &rec, sizeof(rec));
    s_host_flash_programmed = true;
    return true;
#endif
}

bool flash_storage_save_calibration(const flash_calibration_t *calib) {
    if (!calib) {
        return false;
    }

    persistent_storage_t rec;
    (void)flash_storage_load(&rec);
    rec.has_calibration = 1;
    rec.calib = *calib;
    return flash_storage_save(&rec);
}

bool flash_storage_save_paired_addr(const uint8_t bd_addr[6]) {
    if (!bd_addr) {
        return false;
    }

    persistent_storage_t rec;
    (void)flash_storage_load(&rec);
    if (rec.has_paired_device && memcmp(rec.paired_bd_addr, bd_addr, 6) == 0) {
        return true; /* Already stored; avoid unnecessary Flash wear */
    }
    rec.has_paired_device = 1;
    memcpy(rec.paired_bd_addr, bd_addr, 6);
    return flash_storage_save(&rec);
}

bool flash_storage_save_keymap(const keymap_config_t *km) {
    if (!flash_keymap_validate(km)) {
        return false;
    }

    persistent_storage_t rec;
    (void)flash_storage_load(&rec);
    rec.has_keymap = 1;
    memcpy(&rec.keymap, km, sizeof(rec.keymap));
    return flash_storage_save(&rec);
}

#ifndef PICO_ON_DEVICE
void flash_storage_test_reset(void) {
    s_host_flash_programmed = false;
    memset(&s_host_flash_sim, 0xFF, sizeof(s_host_flash_sim));
}

void flash_storage_test_write_raw(const void *data, size_t len) {
    memset(&s_host_flash_sim, 0xFF, sizeof(s_host_flash_sim));
    if (data && len > 0U) {
        size_t copy_len = (len > sizeof(s_host_flash_sim)) ? sizeof(s_host_flash_sim) : len;
        memcpy(&s_host_flash_sim, data, copy_len);
    }
    s_host_flash_programmed = true;
}
#endif
