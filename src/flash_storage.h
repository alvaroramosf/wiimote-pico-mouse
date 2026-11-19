#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define FLASH_STORAGE_MAGIC      0x5749494DU /* "WIIM" */
#define FLASH_STORAGE_VERSION_V1 1U
#define FLASH_STORAGE_VERSION    2U

typedef struct {
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;
    float ref_diag_h;
    float ref_diag_v;
} flash_calibration_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t  has_calibration;
    uint8_t  has_paired_device;
    uint8_t  paired_bd_addr[6];
    flash_calibration_t calib;
    uint8_t  has_keymap;
    uint8_t  reserved[3];
    keymap_config_t keymap;
    uint32_t crc32;
} persistent_storage_t;

_Static_assert(sizeof(persistent_storage_t) <= 256U,
               "persistent_storage_t must fit in a single 256-byte Flash page");

/**
 * @brief Compute IEEE 802.3 CRC-32 checksum.
 */
uint32_t flash_compute_crc32(const void *data, size_t len);

/**
 * @brief Initialize persistent storage subsystem.
 */
void flash_storage_init(void);

/**
 * @brief Load and validate persistent storage record from internal Flash.
 *        Validates magic, version, and CRC-32.
 *
 * @param out Destination structure. If invalid, initialized to defaults.
 * @return true if valid record was loaded from Flash, false otherwise.
 */
bool flash_storage_load(persistent_storage_t *out);

/**
 * @brief Save persistent storage record to internal Flash with CRC-32.
 *
 * @param in Record to persist (magic, version, and crc32 are populated).
 * @return true on success.
 */
bool flash_storage_save(const persistent_storage_t *in);

/**
 * @brief Convenience helper to update and save calibration parameters while
 *        preserving paired Wiimote Bluetooth address.
 */
bool flash_storage_save_calibration(const flash_calibration_t *calib);

/**
 * @brief Convenience helper to update and save the paired Wiimote BD_ADDR
 *        while preserving existing calibration parameters.
 */
bool flash_storage_save_paired_addr(const uint8_t bd_addr[6]);

/**
 * @brief Populate keymap_config_t with compiled-in factory defaults.
 */
void flash_keymap_get_defaults(keymap_config_t *out);

/**
 * @brief Validate all button bindings, filter parameters, and scroll interval.
 */
bool flash_keymap_validate(const keymap_config_t *km);

/**
 * @brief Save a validated keymap configuration to Flash while preserving
 *        existing calibration and paired Wiimote BD_ADDR.
 */
bool flash_storage_save_keymap(const keymap_config_t *km);

#ifndef PICO_ON_DEVICE
void flash_storage_test_reset(void);
void flash_storage_test_write_raw(const void *data, size_t len);
#endif

#endif /* FLASH_STORAGE_H */
