#ifndef DIAMOND_TRACKER_H
#define DIAMOND_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  size;
    bool     valid;
} ir_point_t;

typedef struct {
    float u;
    float v;
} vec2_t;

typedef enum {
    VERTEX_TOP = 0,
    VERTEX_RIGHT = 1,
    VERTEX_BOTTOM = 2,
    VERTEX_LEFT = 3
} diamond_vertex_id_t;

typedef enum {
    CALIB_IDLE = 0,
    CALIB_STEP1_TOP_LEFT = 1,
    CALIB_STEP2_BOTTOM_RIGHT = 2
} calib_state_t;

typedef struct {
    /* Live diamond geometry state */
    vec2_t last_vertices[4];
    vec2_t ref_vertices[4];
    bool   has_live_geometry;
    bool   has_ref_geometry;
    float  diag_h;
    float  diag_v;
    float  aspect_ratio;
    float  persp_alpha_h;
    float  persp_alpha_v;
    float  last_roll;
    float  ref_roll;

    /* Cursor state */
    float    raw_norm_x;
    float    raw_norm_y;
    float    cal_norm_x;
    float    cal_norm_y;
    uint16_t usb_x;
    uint16_t usb_y;
    uint8_t  visible_leds;
    bool     has_valid_cursor;

    /* On-controller affine calibration state */
    calib_state_t calib_state;
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;
    float calib_p1_x;
    float calib_p1_y;
} diamond_tracker_t;

/**
 * @brief Initialize diamond tracker with default identity calibration.
 */
void diamond_tracker_init(diamond_tracker_t *t);

/**
 * @brief Apply affine calibration parameters (loaded from Flash or calibration).
 */
void diamond_tracker_set_calibration(diamond_tracker_t *t,
                                     float scale_x, float scale_y,
                                     float offset_x, float offset_y);

/**
 * @brief Read current affine calibration parameters.
 */
void diamond_tracker_get_calibration(const diamond_tracker_t *t,
                                     float *scale_x, float *scale_y,
                                     float *offset_x, float *offset_y);

/**
 * @brief Compute 3x3 2D projective homography mapping 4 source points src[0..3]
 *        to 4 destination points dst[0..3]. H is stored row-major with H[8] = 1.0f.
 */
bool diamond_tracker_compute_homography(const vec2_t src[4], const vec2_t dst[4], float H[9]);

/**
 * @brief Process one frame of up to 4 IR dots and 3-axis accelerometer readings.
 *        Automatically handles 4-LED full homography, 3-LED reconstruction,
 *        2-LED similarity fallback, and 1/0-LED freeze.
 */
void diamond_tracker_update(diamond_tracker_t *t,
                            const ir_point_t ir[4],
                            int16_t accel_x, int16_t accel_y, int16_t accel_z);

/**
 * @brief Enter 2-point on-controller calibration mode (places cursor at 10%, 10%).
 */
void diamond_tracker_start_calibration(diamond_tracker_t *t);

/**
 * @brief Cancel calibration mode and return to normal tracking.
 */
void diamond_tracker_cancel_calibration(diamond_tracker_t *t);

/**
 * @brief Confirm current calibration target when user presses 'A'.
 *        Step 1 moves cursor to (90%, 90%); Step 2 computes affine scale/offset.
 *
 * @param out_finished Set to true when Step 2 completes and new calibration is active.
 * @return true if step succeeded, false if sample was degenerate (cancelled).
 */
bool diamond_tracker_calibration_confirm_step(diamond_tracker_t *t, bool *out_finished);

#endif /* DIAMOND_TRACKER_H */
