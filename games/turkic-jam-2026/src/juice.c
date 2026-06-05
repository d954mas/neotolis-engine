#include "juice.h"

#include <math.h>

#include "rng.h"

float tj_ease_out_quad(float t) { return 1.0F - ((1.0F - t) * (1.0F - t)); }

float tj_ease_in_out_quad(float t) { return t < 0.5F ? (2.0F * t * t) : 1.0F - (powf((-2.0F * t) + 2.0F, 2.0F) / 2.0F); }

float tj_ease_out_back(float t) {
    const float c1 = 1.70158F;
    const float c3 = c1 + 1.0F;
    const float u = t - 1.0F;
    return 1.0F + (c3 * u * u * u) + (c1 * u * u);
}

void tj_shake_add(tj_shake_t *s, float amount) {
    s->trauma += amount;
    if (s->trauma > 1.0F) {
        s->trauma = 1.0F;
    }
    if (s->noise == 0U) {
        s->noise = 0x1234567U;
    }
}

void tj_shake_update(tj_shake_t *s, float dt) {
    s->trauma -= 1.5F * dt; /* full decay in ~0.66 s */
    if (s->trauma < 0.0F) {
        s->trauma = 0.0F;
    }
}

static float signed_unit(uint32_t *state) { return (float)(int32_t)rng_next(state) / 2147483648.0F; /* [-1, 1) */ }

void tj_shake_sample(tj_shake_t *s, float max_px, float max_deg, float *dx, float *dy, float *deg) {
    const float intensity = s->trauma * s->trauma;
    *dx = max_px * intensity * signed_unit(&s->noise);
    *dy = max_px * intensity * signed_unit(&s->noise);
    *deg = max_deg * intensity * signed_unit(&s->noise);
}
