#ifndef TJ_JUICE_H
#define TJ_JUICE_H

/* Game-feel helpers: easing curves + trauma-based screen shake.
 * Shake follows the Squirrel Eiserloh model (intensity = trauma^2). */

#include <stdint.h>

float tj_ease_out_quad(float t);
float tj_ease_in_out_quad(float t);
float tj_ease_out_back(float t); /* overshoots past 1.0 then settles */

typedef struct {
    float trauma;   /* 0..1, decays to 0 each second */
    uint32_t noise; /* internal rng state */
} tj_shake_t;

void tj_shake_add(tj_shake_t *s, float amount);
void tj_shake_update(tj_shake_t *s, float dt);
/* Per-frame offset (px) + rotation (deg) for the current trauma. */
void tj_shake_sample(tj_shake_t *s, float max_px, float max_deg, float *dx, float *dy, float *deg);

#endif /* TJ_JUICE_H */
