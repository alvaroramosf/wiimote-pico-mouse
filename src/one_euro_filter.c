#include "one_euro_filter.h"
#include "config.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float smoothing_factor(float dt, float cutoff) {
    float r = 2.0f * (float)M_PI * cutoff * dt;
    return r / (r + 1.0f);
}

static float low_pass_filter(float alpha, float x, float x_prev) {
    return alpha * x + (1.0f - alpha) * x_prev;
}

void one_euro_1d_init(one_euro_1d_t *f, float min_cutoff, float beta, float d_cutoff) {
    if (!f) {
        return;
    }
    f->min_cutoff = min_cutoff;
    f->beta = beta;
    f->d_cutoff = d_cutoff;
    one_euro_1d_reset(f);
}

void one_euro_1d_reset(one_euro_1d_t *f) {
    if (!f) {
        return;
    }
    f->x_prev = 0.0f;
    f->dx_prev = 0.0f;
    f->initialized = false;
}

float one_euro_1d_filter(one_euro_1d_t *f, float x, float dt) {
    if (!f) {
        return x;
    }

    if (!f->initialized) {
        f->x_prev = x;
        f->dx_prev = 0.0f;
        f->initialized = true;
        return x;
    }

    if (dt <= 1e-5f) {
        dt = 1.0f / ONE_EURO_FREQ_HZ;
    }

    float a_d = smoothing_factor(dt, f->d_cutoff);
    float dx = (x - f->x_prev) / dt;
    float dx_hat = low_pass_filter(a_d, dx, f->dx_prev);

    float cutoff = f->min_cutoff + f->beta * fabsf(dx_hat);
    float a = smoothing_factor(dt, cutoff);
    float x_hat = low_pass_filter(a, x, f->x_prev);

    f->x_prev = x_hat;
    f->dx_prev = dx_hat;

    return x_hat;
}

void one_euro_2d_init(one_euro_2d_t *f, float min_cutoff, float beta, float d_cutoff) {
    if (!f) {
        return;
    }
    one_euro_1d_init(&f->x, min_cutoff, beta, d_cutoff);
    one_euro_1d_init(&f->y, min_cutoff, beta, d_cutoff);
}

void one_euro_2d_set_params(one_euro_2d_t *f, float min_cutoff, float beta) {
    if (!f) {
        return;
    }
    f->x.min_cutoff = min_cutoff;
    f->x.beta = beta;
    f->y.min_cutoff = min_cutoff;
    f->y.beta = beta;
}

void one_euro_2d_reset(one_euro_2d_t *f) {
    if (!f) {
        return;
    }
    one_euro_1d_reset(&f->x);
    one_euro_1d_reset(&f->y);
}

void one_euro_2d_filter(one_euro_2d_t *f, float x, float y, float dt,
                        float *out_x, float *out_y) {
    if (!f) {
        if (out_x) {
            *out_x = x;
        }
        if (out_y) {
            *out_y = y;
        }
        return;
    }
    float fx = one_euro_1d_filter(&f->x, x, dt);
    float fy = one_euro_1d_filter(&f->y, y, dt);
    if (out_x) {
        *out_x = fx;
    }
    if (out_y) {
        *out_y = fy;
    }
}
