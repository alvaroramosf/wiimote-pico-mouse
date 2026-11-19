#include "diamond_tracker.h"
#include "config.h"

#include <math.h>
#include <string.h>

static const vec2_t CANONICAL_DIAMOND_DST[4] = {
    {0.5f, 0.0f}, /* VERTEX_TOP */
    {1.0f, 0.5f}, /* VERTEX_RIGHT */
    {0.5f, 1.0f}, /* VERTEX_BOTTOM */
    {0.0f, 0.5f}  /* VERTEX_LEFT */
};

static float clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static uint16_t norm_to_usb(float norm) {
    float c = clamp01(norm);
    long val = lroundf(c * (float)USB_HID_ABS_MAX);
    if (val < USB_HID_ABS_MIN) {
        return USB_HID_ABS_MIN;
    }
    if (val > USB_HID_ABS_MAX) {
        return USB_HID_ABS_MAX;
    }
    return (uint16_t)val;
}

static vec2_t raw_to_camera(const ir_point_t *pt) {
    vec2_t p;
#if WIIMOTE_IR_INVERT_X
    p.u = WIIMOTE_OPTICAL_CENTER_X - (float)pt->x;
#else
    p.u = (float)pt->x - WIIMOTE_OPTICAL_CENTER_X;
#endif
#if WIIMOTE_IR_INVERT_Y
    p.v = WIIMOTE_OPTICAL_CENTER_Y - (float)pt->y;
#else
    p.v = (float)pt->y - WIIMOTE_OPTICAL_CENTER_Y;
#endif
    return p;
}

static vec2_t vec_add(vec2_t a, vec2_t b) {
    vec2_t r = {a.u + b.u, a.v + b.v};
    return r;
}

static vec2_t vec_sub(vec2_t a, vec2_t b) {
    vec2_t r = {a.u - b.u, a.v - b.v};
    return r;
}

static vec2_t vec_scale(vec2_t a, float s) {
    vec2_t r = {a.u * s, a.v * s};
    return r;
}

static float vec_dot(vec2_t a, vec2_t b) {
    return a.u * b.u + a.v * b.v;
}

static float vec_cross(vec2_t a, vec2_t b) {
    return a.u * b.v - a.v * b.u;
}

static float vec_norm2(vec2_t a) {
    return a.u * a.u + a.v * a.v;
}

static float vec_norm(vec2_t a) {
    return sqrtf(vec_norm2(a));
}

static vec2_t rotate_vec(vec2_t p, float angle_rad) {
    float c = cosf(angle_rad);
    float s = sinf(angle_rad);
    vec2_t r = {
        p.u * c - p.v * s,
        p.u * s + p.v * c
    };
    return r;
}

static float compute_roll_angle(const diamond_tracker_t *t, int16_t accel_x, int16_t accel_z) {
    if (accel_x == 0 && accel_z == 0) {
        return t->last_roll;
    }
    return (float)WIIMOTE_ACCEL_ROLL_SIGN * atan2f((float)accel_x, (float)accel_z);
}

static bool point_inside_sensor_uv(vec2_t p) {
    float x = WIIMOTE_IR_INVERT_X ? (WIIMOTE_OPTICAL_CENTER_X - p.u) : (p.u + WIIMOTE_OPTICAL_CENTER_X);
    float y = WIIMOTE_IR_INVERT_Y ? (WIIMOTE_OPTICAL_CENTER_Y - p.v) : (p.v + WIIMOTE_OPTICAL_CENTER_Y);
    return (x >= 0.0f && x < (float)WIIMOTE_IR_WIDTH &&
            y >= 0.0f && y < (float)WIIMOTE_IR_HEIGHT);
}

static void build_default_diamond(float roll, vec2_t out[4]) {
    const vec2_t upright[4] = {
        {0.0f, -DIAMOND_DEFAULT_HALF_H_PX}, /* TOP */
        {DIAMOND_DEFAULT_HALF_W_PX, 0.0f},  /* RIGHT */
        {0.0f, DIAMOND_DEFAULT_HALF_H_PX},  /* BOTTOM */
        {-DIAMOND_DEFAULT_HALF_W_PX, 0.0f}  /* LEFT */
    };
    for (int i = 0; i < 4; ++i) {
        out[i] = rotate_vec(upright[i], -roll);
    }
}

static void get_rotated_reference(const diamond_tracker_t *t, float roll, vec2_t out[4]) {
    if (t->has_ref_geometry) {
        vec2_t c = {0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            c = vec_add(c, t->ref_vertices[i]);
        }
        c = vec_scale(c, 0.25f);
        float droll = roll - t->ref_roll;
        for (int i = 0; i < 4; ++i) {
            out[i] = rotate_vec(vec_sub(t->ref_vertices[i], c), -droll);
        }
    } else {
        build_default_diamond(roll, out);
    }
}

void diamond_tracker_init(diamond_tracker_t *t) {
    if (!t) {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->aspect_ratio = (float)WIIMOTE_IR_WIDTH / (float)WIIMOTE_IR_HEIGHT;
    t->diag_h = DIAMOND_DEFAULT_HALF_W_PX * 2.0f;
    t->diag_v = DIAMOND_DEFAULT_HALF_H_PX * 2.0f;
    t->raw_norm_x = 0.5f;
    t->raw_norm_y = 0.5f;
    t->cal_norm_x = 0.5f;
    t->cal_norm_y = 0.5f;
    t->usb_x = norm_to_usb(0.5f);
    t->usb_y = norm_to_usb(0.5f);
    t->scale_x = 1.0f;
    t->scale_y = 1.0f;
    t->offset_x = 0.0f;
    t->offset_y = 0.0f;
    t->calib_state = CALIB_IDLE;
}

void diamond_tracker_set_calibration(diamond_tracker_t *t,
                                     float scale_x, float scale_y,
                                     float offset_x, float offset_y) {
    if (!t) {
        return;
    }
    t->scale_x = scale_x;
    t->scale_y = scale_y;
    t->offset_x = offset_x;
    t->offset_y = offset_y;
}

void diamond_tracker_get_calibration(const diamond_tracker_t *t,
                                     float *scale_x, float *scale_y,
                                     float *offset_x, float *offset_y) {
    if (!t) {
        return;
    }
    if (scale_x) {
        *scale_x = t->scale_x;
    }
    if (scale_y) {
        *scale_y = t->scale_y;
    }
    if (offset_x) {
        *offset_x = t->offset_x;
    }
    if (offset_y) {
        *offset_y = t->offset_y;
    }
}

bool diamond_tracker_compute_homography(const vec2_t src[4], const vec2_t dst[4], float H[9]) {
    if (!src || !dst || !H) {
        return false;
    }

    /* Scale source coordinates for well-conditioned Gaussian elimination */
    double scale = 1.0;
    for (int i = 0; i < 4; ++i) {
        double au = fabs((double)src[i].u);
        double av = fabs((double)src[i].v);
        if (au > scale) {
            scale = au;
        }
        if (av > scale) {
            scale = av;
        }
    }
    double inv_s = 1.0 / scale;

    double A[8][9];
    memset(A, 0, sizeof(A));

    for (int i = 0; i < 4; ++i) {
        double u = (double)src[i].u * inv_s;
        double v = (double)src[i].v * inv_s;
        double x = (double)dst[i].u;
        double y = (double)dst[i].v;

        int r0 = 2 * i;
        A[r0][0] = u;
        A[r0][1] = v;
        A[r0][2] = 1.0;
        A[r0][6] = -x * u;
        A[r0][7] = -x * v;
        A[r0][8] = x;

        int r1 = 2 * i + 1;
        A[r1][3] = u;
        A[r1][4] = v;
        A[r1][5] = 1.0;
        A[r1][6] = -y * u;
        A[r1][7] = -y * v;
        A[r1][8] = y;
    }

    /* Gaussian elimination with partial pivoting */
    for (int col = 0; col < 8; ++col) {
        int pivot_row = col;
        double max_val = fabs(A[col][col]);
        for (int row = col + 1; row < 8; ++row) {
            double val = fabs(A[row][col]);
            if (val > max_val) {
                max_val = val;
                pivot_row = row;
            }
        }
        if (max_val < 1e-9) {
            return false;
        }
        if (pivot_row != col) {
            for (int k = col; k < 9; ++k) {
                double tmp = A[col][k];
                A[col][k] = A[pivot_row][k];
                A[pivot_row][k] = tmp;
            }
        }
        double inv_piv = 1.0 / A[col][col];
        for (int row = col + 1; row < 8; ++row) {
            double factor = A[row][col] * inv_piv;
            if (factor == 0.0) {
                continue;
            }
            for (int k = col; k < 9; ++k) {
                A[row][k] -= factor * A[col][k];
            }
        }
    }

    double h[8];
    for (int row = 7; row >= 0; --row) {
        double sum = A[row][8];
        for (int col = row + 1; col < 8; ++col) {
            sum -= A[row][col] * h[col];
        }
        h[row] = sum / A[row][row];
    }

    /* Unscale columns multiplying u and v */
    H[0] = (float)(h[0] * inv_s);
    H[1] = (float)(h[1] * inv_s);
    H[2] = (float)h[2];
    H[3] = (float)(h[3] * inv_s);
    H[4] = (float)(h[4] * inv_s);
    H[5] = (float)h[5];
    H[6] = (float)(h[6] * inv_s);
    H[7] = (float)(h[7] * inv_s);
    H[8] = 1.0f;

    return true;
}

static bool evaluate_homography_center(const vec2_t vertices[4], float *out_x, float *out_y) {
    float H[9];
    if (!diamond_tracker_compute_homography(vertices, CANONICAL_DIAMOND_DST, H)) {
        return false;
    }
    if (fabsf(H[8]) < 1e-6f) {
        return false;
    }
    float x = H[2] / H[8];
    float y = H[5] / H[8];
    if (!isfinite(x) || !isfinite(y) || x < -1.5f || x > 2.5f || y < -1.5f || y > 2.5f) {
        return false;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static void identify_4_vertices(const diamond_tracker_t *t,
                                const vec2_t pts[4],
                                float roll,
                                vec2_t out_vertices[4]) {
    vec2_t centroid = {0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        centroid = vec_add(centroid, pts[i]);
    }
    centroid = vec_scale(centroid, 0.25f);

    vec2_t derot[4];
    for (int i = 0; i < 4; ++i) {
        derot[i] = rotate_vec(vec_sub(pts[i], centroid), roll);
    }

    vec2_t prev_rot[4];
    if (t->has_live_geometry) {
        vec2_t prev_c = {0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            prev_c = vec_add(prev_c, t->last_vertices[i]);
        }
        prev_c = vec_scale(prev_c, 0.25f);
        float droll = roll - t->last_roll;
        for (int i = 0; i < 4; ++i) {
            prev_rot[i] = rotate_vec(vec_sub(t->last_vertices[i], prev_c), -droll);
        }
    }

    const vec2_t expected_dir[4] = {
        {0.0f, -1.0f}, /* TOP */
        {1.0f, 0.0f},  /* RIGHT */
        {0.0f, 1.0f},  /* BOTTOM */
        {-1.0f, 0.0f}  /* LEFT */
    };

    float best_cost = 1e30f;
    int best_perm[4] = {0, 1, 2, 3};

    for (int i0 = 0; i0 < 4; ++i0) {
        for (int i1 = 0; i1 < 4; ++i1) {
            if (i1 == i0) continue;
            for (int i2 = 0; i2 < 4; ++i2) {
                if (i2 == i0 || i2 == i1) continue;
                int i3 = 6 - i0 - i1 - i2;
                int perm[4] = {i0, i1, i2, i3};

                /* Enforce positive orientation of (Right - Left) x (Bottom - Top) */
                vec2_t d_h = vec_sub(pts[i1], pts[i3]);
                vec2_t d_v = vec_sub(pts[i2], pts[i0]);
                if (vec_cross(d_h, d_v) <= 0.0f) {
                    continue;
                }

                /* Roll alignment cost (0 = perfect alignment, up to 8) */
                float align_score = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    vec2_t q = derot[perm[k]];
                    float n = vec_norm(q);
                    if (n > 1e-3f) {
                        align_score += vec_dot(q, expected_dir[k]) / n;
                    }
                }
                float cost = (4.0f - align_score) * 10000.0f;

                /* Temporal continuity cost relative to previous frame */
                if (t->has_live_geometry) {
                    float temp_err = 0.0f;
                    for (int k = 0; k < 4; ++k) {
                        vec2_t d_rel = vec_sub(vec_sub(pts[perm[k]], centroid), prev_rot[k]);
                        vec2_t d_abs = vec_sub(pts[perm[k]], t->last_vertices[k]);
                        temp_err += 0.7f * vec_norm2(d_rel) + 0.3f * vec_norm2(d_abs);
                    }
                    cost += temp_err * 0.01f;
                }

                if (cost < best_cost) {
                    best_cost = cost;
                    memcpy(best_perm, perm, sizeof(best_perm));
                }
            }
        }
    }

    for (int k = 0; k < 4; ++k) {
        out_vertices[k] = pts[best_perm[k]];
    }
}

static void update_live_geometry_from_4(diamond_tracker_t *t, const vec2_t v[4], float roll) {
    vec2_t d_h = vec_sub(v[VERTEX_RIGHT], v[VERTEX_LEFT]);
    vec2_t d_v = vec_sub(v[VERTEX_BOTTOM], v[VERTEX_TOP]);
    float len_h = vec_norm(d_h);
    float len_v = vec_norm(d_v);

    if (len_h > DIAMOND_MIN_SPAN_PX && len_v > DIAMOND_MIN_SPAN_PX) {
        t->diag_h = len_h;
        t->diag_v = len_v;
        t->aspect_ratio = len_h / len_v;

        vec2_t m_h = vec_scale(vec_add(v[VERTEX_LEFT], v[VERTEX_RIGHT]), 0.5f);
        vec2_t m_v = vec_scale(vec_add(v[VERTEX_TOP], v[VERTEX_BOTTOM]), 0.5f);
        vec2_t delta = vec_sub(m_v, m_h);

        float det = vec_cross(d_h, d_v);
        if (fabsf(det) > 1e-3f) {
            t->persp_alpha_h = vec_cross(delta, d_v) / det;
            t->persp_alpha_v = vec_cross(d_h, delta) / det;
            /* Clamp perspective offsets to physically plausible bounds */
            if (t->persp_alpha_h < -0.45f) t->persp_alpha_h = -0.45f;
            if (t->persp_alpha_h >  0.45f) t->persp_alpha_h =  0.45f;
            if (t->persp_alpha_v < -0.45f) t->persp_alpha_v = -0.45f;
            if (t->persp_alpha_v >  0.45f) t->persp_alpha_v =  0.45f;
        }
    }

    memcpy(t->last_vertices, v, sizeof(t->last_vertices));
    memcpy(t->ref_vertices, v, sizeof(t->ref_vertices));
    t->last_roll = roll;
    t->ref_roll = roll;
    t->has_live_geometry = true;
    t->has_ref_geometry = true;
}

static bool reconstruct_4th_vertex(const diamond_tracker_t *t,
                                   int missing_id,
                                   vec2_t v[4]) {
    float aspect = (t->aspect_ratio > 0.2f && t->aspect_ratio < 5.0f)
                       ? t->aspect_ratio
                       : ((float)WIIMOTE_IR_WIDTH / (float)WIIMOTE_IR_HEIGHT);
    float ah = t->has_live_geometry ? t->persp_alpha_h : 0.0f;
    float av = t->has_live_geometry ? t->persp_alpha_v : 0.0f;

    if (missing_id == VERTEX_TOP || missing_id == VERTEX_BOTTOM) {
        vec2_t d_h = vec_sub(v[VERTEX_RIGHT], v[VERTEX_LEFT]);
        if (vec_norm(d_h) < DIAMOND_MIN_SPAN_PX) {
            return false;
        }
        vec2_t m_h = vec_scale(vec_add(v[VERTEX_LEFT], v[VERTEX_RIGHT]), 0.5f);
        /* +90 deg in (+u right, +v down): (u, v) -> (-v, u) */
        vec2_t d_v_approx = {-d_h.v / aspect, d_h.u / aspect};
        vec2_t m_v = vec_add(m_h, vec_add(vec_scale(d_h, ah), vec_scale(d_v_approx, av)));
        if (missing_id == VERTEX_TOP) {
            v[VERTEX_TOP] = vec_sub(vec_scale(m_v, 2.0f), v[VERTEX_BOTTOM]);
        } else {
            v[VERTEX_BOTTOM] = vec_sub(vec_scale(m_v, 2.0f), v[VERTEX_TOP]);
        }
    } else {
        vec2_t d_v = vec_sub(v[VERTEX_BOTTOM], v[VERTEX_TOP]);
        if (vec_norm(d_v) < DIAMOND_MIN_SPAN_PX) {
            return false;
        }
        vec2_t m_v = vec_scale(vec_add(v[VERTEX_TOP], v[VERTEX_BOTTOM]), 0.5f);
        /* -90 deg in (+u right, +v down): (u, v) -> (v, -u) */
        vec2_t d_h_approx = {d_v.v * aspect, -d_v.u * aspect};
        vec2_t m_h = vec_sub(m_v, vec_add(vec_scale(d_h_approx, ah), vec_scale(d_v, av)));
        if (missing_id == VERTEX_LEFT) {
            v[VERTEX_LEFT] = vec_sub(vec_scale(m_h, 2.0f), v[VERTEX_RIGHT]);
        } else {
            v[VERTEX_RIGHT] = vec_sub(vec_scale(m_h, 2.0f), v[VERTEX_LEFT]);
        }
    }
    return true;
}

static bool track_3_leds(diamond_tracker_t *t,
                         const vec2_t obs[3],
                         float roll,
                         vec2_t out_vertices[4]) {
    vec2_t ref_rot[4];
    get_rotated_reference(t, roll, ref_rot);

    vec2_t prev_rot[4];
    if (t->has_live_geometry) {
        vec2_t prev_c = {0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            prev_c = vec_add(prev_c, t->last_vertices[i]);
        }
        prev_c = vec_scale(prev_c, 0.25f);
        float droll = roll - t->last_roll;
        for (int i = 0; i < 4; ++i) {
            prev_rot[i] = rotate_vec(vec_sub(t->last_vertices[i], prev_c), -droll);
        }
    } else {
        memcpy(prev_rot, ref_rot, sizeof(prev_rot));
    }

    float best_cost = 1e30f;
    vec2_t best_vertices[4];
    int best_missing = -1;

    static const int PERMS3[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
    };

    for (int missing = 0; missing < 4; ++missing) {
        int slots[3];
        int idx = 0;
        for (int s = 0; s < 4; ++s) {
            if (s != missing) {
                slots[idx++] = s;
            }
        }

        for (int p = 0; p < 6; ++p) {
            vec2_t cand[4];
            memset(cand, 0, sizeof(cand));
            for (int k = 0; k < 3; ++k) {
                cand[slots[k]] = obs[PERMS3[p][k]];
            }

            if (!reconstruct_4th_vertex(t, missing, cand)) {
                continue;
            }

            /* Reject inverted / crossed quads */
            vec2_t d_h = vec_sub(cand[VERTEX_RIGHT], cand[VERTEX_LEFT]);
            vec2_t d_v = vec_sub(cand[VERTEX_BOTTOM], cand[VERTEX_TOP]);
            if (vec_cross(d_h, d_v) <= 0.0f) {
                continue;
            }

            /* Pairwise translation-invariant shape error vs rotated reference */
            float shape_err = 0.0f;
            for (int a = 0; a < 3; ++a) {
                for (int b = a + 1; b < 3; ++b) {
                    int sa = slots[a];
                    int sb = slots[b];
                    vec2_t obs_edge = vec_sub(cand[sa], cand[sb]);
                    vec2_t ref_edge = vec_sub(prev_rot[sa], prev_rot[sb]);
                    shape_err += vec_norm2(vec_sub(obs_edge, ref_edge));
                }
            }

            /* Temporal position continuity error */
            float pos_err = 0.0f;
            if (t->has_live_geometry) {
                for (int k = 0; k < 3; ++k) {
                    int s = slots[k];
                    pos_err += vec_norm2(vec_sub(cand[s], t->last_vertices[s]));
                }
            } else {
                /* When starting cold at 3 LEDs, enforce alignment with accel roll */
                vec2_t c_cand = vec_scale(vec_add(vec_add(cand[0], cand[1]),
                                                  vec_add(cand[2], cand[3])), 0.25f);
                const vec2_t expected_dir[4] = {
                    {0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}
                };
                float align = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    vec2_t q = rotate_vec(vec_sub(cand[k], c_cand), roll);
                    float n = vec_norm(q);
                    if (n > 1e-3f) {
                        align += vec_dot(q, expected_dir[k]) / n;
                    }
                }
                pos_err = (4.0f - align) * 50000.0f;
            }

            /* Soft bonus when reconstructed missing LED lies outside sensor bounds */
            float oob_penalty = point_inside_sensor_uv(cand[missing]) ? 2000.0f : 0.0f;

            float total_cost = shape_err + 0.5f * pos_err + oob_penalty;
            if (total_cost < best_cost) {
                best_cost = total_cost;
                best_missing = missing;
                memcpy(best_vertices, cand, sizeof(best_vertices));
            }
        }
    }

    if (best_missing < 0) {
        return false;
    }

    memcpy(out_vertices, best_vertices, sizeof(best_vertices));

    /* Update live diagonal length on the complete diagonal to track distance smoothly */
    if (best_missing == VERTEX_TOP || best_missing == VERTEX_BOTTOM) {
        float new_dh = vec_norm(vec_sub(out_vertices[VERTEX_RIGHT], out_vertices[VERTEX_LEFT]));
        if (new_dh > DIAMOND_MIN_SPAN_PX) {
            t->diag_h = new_dh;
        }
    } else {
        float new_dv = vec_norm(vec_sub(out_vertices[VERTEX_BOTTOM], out_vertices[VERTEX_TOP]));
        if (new_dv > DIAMOND_MIN_SPAN_PX) {
            t->diag_v = new_dv;
        }
    }

    memcpy(t->last_vertices, out_vertices, sizeof(t->last_vertices));
    t->last_roll = roll;
    t->has_live_geometry = true;
    return true;
}

static bool track_2_leds(diamond_tracker_t *t,
                         const vec2_t obs[2],
                         float roll,
                         vec2_t out_vertices[4]) {
    vec2_t o = vec_sub(obs[1], obs[0]);
    float len_o = vec_norm(o);
    if (len_o < DIAMOND_MIN_SPAN_PX) {
        return false;
    }

    vec2_t ref[4];
    get_rotated_reference(t, roll, ref);

    float best_cost = 1e30f;
    int best_a = -1;
    int best_b = -1;

    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            if (a == b) {
                continue;
            }
            vec2_t e = vec_sub(ref[b], ref[a]);
            float len_e = vec_norm(e);
            if (len_e < DIAMOND_MIN_SPAN_PX) {
                continue;
            }

            float scale = len_o / len_e;
            if (scale < 0.35f || scale > 2.8f) {
                continue;
            }

            float cos_theta = vec_dot(o, e) / (len_o * len_e);
            if (cos_theta < 0.50f) {
                continue;
            }

            float ang_cost = (1.0f - cos_theta) * 40000.0f;
            float scale_cost = (scale - 1.0f) * (scale - 1.0f) * 15000.0f;

            float pos_cost = 0.0f;
            if (t->has_live_geometry) {
                pos_cost = vec_norm2(vec_sub(obs[0], t->last_vertices[a])) +
                           vec_norm2(vec_sub(obs[1], t->last_vertices[b]));
            }

            float total_cost = pos_cost + ang_cost + scale_cost;
            if (total_cost < best_cost) {
                best_cost = total_cost;
                best_a = a;
                best_b = b;
            }
        }
    }

    if (best_a < 0 || best_b < 0) {
        return false;
    }

    /* 2D similarity transform: V_k = M_obs + R_scaled * (ref[k] - M_ref) */
    vec2_t e = vec_sub(ref[best_b], ref[best_a]);
    float e2 = vec_norm2(e);
    if (e2 < 1e-3f) {
        return false;
    }

    float c = (o.u * e.u + o.v * e.v) / e2;
    float d = (o.v * e.u - o.u * e.v) / e2;

    vec2_t m_obs = vec_scale(vec_add(obs[0], obs[1]), 0.5f);
    vec2_t m_ref = vec_scale(vec_add(ref[best_a], ref[best_b]), 0.5f);

    for (int k = 0; k < 4; ++k) {
        vec2_t rel = vec_sub(ref[k], m_ref);
        out_vertices[k].u = m_obs.u + c * rel.u - d * rel.v;
        out_vertices[k].v = m_obs.v + d * rel.u + c * rel.v;
    }
    /* Ensure exact match on the two observed vertices */
    out_vertices[best_a] = obs[0];
    out_vertices[best_b] = obs[1];

    memcpy(t->last_vertices, out_vertices, sizeof(t->last_vertices));
    t->last_roll = roll;
    t->has_live_geometry = true;
    return true;
}

void diamond_tracker_update(diamond_tracker_t *t,
                            const ir_point_t ir[4],
                            int16_t accel_x, int16_t accel_y, int16_t accel_z) {
    (void)accel_y;
    if (!t || !ir) {
        return;
    }

    vec2_t pts[4];
    uint8_t count = 0;
    for (int i = 0; i < 4; ++i) {
        if (ir[i].valid && ir[i].x < WIIMOTE_IR_WIDTH && ir[i].y < WIIMOTE_IR_HEIGHT) {
            pts[count++] = raw_to_camera(&ir[i]);
        }
    }
    t->visible_leds = count;

    float roll = compute_roll_angle(t, accel_x, accel_z);
    vec2_t vertices[4];
    bool ok = false;

    if (count == 4) {
        identify_4_vertices(t, pts, roll, vertices);
        update_live_geometry_from_4(t, vertices, roll);
        ok = true;
    } else if (count == 3) {
        ok = track_3_leds(t, pts, roll, vertices);
    } else if (count == 2) {
        ok = track_2_leds(t, pts, roll, vertices);
    } else {
        /* 0 or 1 LED visible: freeze cursor smoothly at last valid position */
        ok = false;
    }

    if (ok) {
        float hx = 0.0f;
        float hy = 0.0f;
        if (evaluate_homography_center(vertices, &hx, &hy)) {
            t->raw_norm_x = hx;
            t->raw_norm_y = hy;
            t->cal_norm_x = clamp01(t->scale_x * hx + t->offset_x);
            t->cal_norm_y = clamp01(t->scale_y * hy + t->offset_y);
            t->has_valid_cursor = true;
        }
    }

    if (t->calib_state == CALIB_STEP1_TOP_LEFT) {
        t->usb_x = norm_to_usb(CALIB_TARGET_1_X);
        t->usb_y = norm_to_usb(CALIB_TARGET_1_Y);
    } else if (t->calib_state == CALIB_STEP2_BOTTOM_RIGHT) {
        t->usb_x = norm_to_usb(CALIB_TARGET_2_X);
        t->usb_y = norm_to_usb(CALIB_TARGET_2_Y);
    } else {
        t->usb_x = norm_to_usb(t->cal_norm_x);
        t->usb_y = norm_to_usb(t->cal_norm_y);
    }
}

void diamond_tracker_start_calibration(diamond_tracker_t *t) {
    if (!t) {
        return;
    }
    t->calib_state = CALIB_STEP1_TOP_LEFT;
    t->usb_x = norm_to_usb(CALIB_TARGET_1_X);
    t->usb_y = norm_to_usb(CALIB_TARGET_1_Y);
}

void diamond_tracker_cancel_calibration(diamond_tracker_t *t) {
    if (!t) {
        return;
    }
    t->calib_state = CALIB_IDLE;
    t->usb_x = norm_to_usb(t->cal_norm_x);
    t->usb_y = norm_to_usb(t->cal_norm_y);
}

bool diamond_tracker_calibration_confirm_step(diamond_tracker_t *t, bool *out_finished) {
    if (out_finished) {
        *out_finished = false;
    }
    if (!t) {
        return false;
    }

    if (t->calib_state == CALIB_STEP1_TOP_LEFT) {
        t->calib_p1_x = t->raw_norm_x;
        t->calib_p1_y = t->raw_norm_y;
        t->calib_state = CALIB_STEP2_BOTTOM_RIGHT;
        t->usb_x = norm_to_usb(CALIB_TARGET_2_X);
        t->usb_y = norm_to_usb(CALIB_TARGET_2_Y);
        return true;
    }

    if (t->calib_state == CALIB_STEP2_BOTTOM_RIGHT) {
        float dx = t->raw_norm_x - t->calib_p1_x;
        float dy = t->raw_norm_y - t->calib_p1_y;
        if (fabsf(dx) < 0.05f || fabsf(dy) < 0.05f) {
            diamond_tracker_cancel_calibration(t);
            return false;
        }

        t->scale_x = (CALIB_TARGET_2_X - CALIB_TARGET_1_X) / dx;
        t->offset_x = CALIB_TARGET_1_X - t->scale_x * t->calib_p1_x;
        t->scale_y = (CALIB_TARGET_2_Y - CALIB_TARGET_1_Y) / dy;
        t->offset_y = CALIB_TARGET_1_Y - t->scale_y * t->calib_p1_y;

        t->cal_norm_x = clamp01(t->scale_x * t->raw_norm_x + t->offset_x);
        t->cal_norm_y = clamp01(t->scale_y * t->raw_norm_y + t->offset_y);
        t->calib_state = CALIB_IDLE;
        t->usb_x = norm_to_usb(t->cal_norm_x);
        t->usb_y = norm_to_usb(t->cal_norm_y);

        if (out_finished) {
            *out_finished = true;
        }
        return true;
    }

    return false;
}
