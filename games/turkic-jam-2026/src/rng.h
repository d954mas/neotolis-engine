#ifndef TJ_RNG_H
#define TJ_RNG_H

/* Tiny deterministic RNG (xorshift32). Global stream for convenience plus an
 * explicit-state variant for independent streams (e.g. screen shake). */

#include <stdint.h>

void rng_seed(uint32_t seed);
uint32_t rng_u32(void);
float rng_float(void); /* [0, 1) */
float rng_range(float lo, float hi);
int rng_range_int(int lo, int hi); /* inclusive both ends */
int rng_chance(float p);           /* 1 with probability p */

/* Advance an explicit state and return the next value (never touches global). */
uint32_t rng_next(uint32_t *state);

#endif /* TJ_RNG_H */
