#ifndef ONE_EURO_FILTER_H
#define ONE_EURO_FILTER_H

#include <stdbool.h>

typedef struct {
    float min_cutoff;
    float beta;
    float d_cutoff;
    float x_prev;
    float dx_prev;
    bool  initialized;
} one_euro_1d_t;

typedef struct {
    one_euro_1d_t x;
    one_euro_1d_t y;
} one_euro_2d_t;

void one_euro_1d_init(one_euro_1d_t *f, float min_cutoff, float beta, float d_cutoff);
void one_euro_1d_reset(one_euro_1d_t *f);
float one_euro_1d_filter(one_euro_1d_t *f, float x, float dt);

void one_euro_2d_init(one_euro_2d_t *f, float min_cutoff, float beta, float d_cutoff);
void one_euro_2d_set_params(one_euro_2d_t *f, float min_cutoff, float beta);
void one_euro_2d_reset(one_euro_2d_t *f);
void one_euro_2d_filter(one_euro_2d_t *f, float x, float y, float dt,
                        float *out_x, float *out_y);

#endif /* ONE_EURO_FILTER_H */
