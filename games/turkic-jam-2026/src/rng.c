#include "rng.h"

static uint32_t s_state = 0x9E3779B9U;

uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void rng_seed(uint32_t seed) { s_state = seed ? seed : 1U; /* xorshift must never sit at zero */ }

uint32_t rng_u32(void) { return rng_next(&s_state); }

float rng_float(void) { return (float)(rng_u32() >> 8) * (1.0F / 16777216.0F); /* 24-bit mantissa */ }

float rng_range(float lo, float hi) { return lo + ((hi - lo) * rng_float()); }

int rng_range_int(int lo, int hi) {
    if (hi < lo) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    uint32_t span = (uint32_t)(hi - lo) + 1U;
    return lo + (int)(rng_u32() % span);
}

int rng_chance(float p) { return rng_float() < p ? 1 : 0; }
