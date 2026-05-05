#ifndef BUNNY_PHYSICS_H
#define BUNNY_PHYSICS_H

/* Pure-C bunnymark physics — testable in isolation (libm + stdint only,
 * no engine deps). dt-scaled (frame-rate independent) physics calibrated
 * against britzl/defold-bunnymark @60Hz baseline:
 *
 *   britzl per-frame value × 60Hz → our per-second constant
 *   GRAVITY     0.5  px/frame  →   30.0 px/s²
 *   VX_MAX     10.0  px/frame  →  600.0 px/s
 *   VY_RANGE    5.0  px/frame  →  300.0 px/s
 *   BOUNCE_KICK 6.0  px/frame  →  360.0 px/s (instant velocity boost on bounce)
 *
 * Coordinate convention is y-up, bottom-left origin (D-25): gravity DECREMENTS vy,
 * bottom edge is y < 0, top edge is y > h. */

#include <stdint.h>

#define BUNNY_GRAVITY 30.0F      /* px/s² downward (britzl 0.5 px/frame × 60Hz) */
#define BUNNY_VX_MAX 600.0F      /* vx0 in [0, 600) px/s */
#define BUNNY_VY_RANGE 300.0F    /* vy0 in [-300, +300) px/s */
#define BUNNY_BOUNCE_BOT -0.85F  /* dimensionless bottom bounce coefficient */
#define BUNNY_BOUNCE_KICK 360.0F /* extra random upward kick on bottom hit (px/s, 50% chance) */

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    uint8_t variant; /* 0..4 */
} nt_bunny_t;

/* Deterministic PRNG state — xorshift64* (good distribution, no heap, fast). */
typedef struct {
    uint64_t state;
} nt_bunny_rng_t;

/* xorshift64* — must be seeded with a non-zero state. */
static inline uint32_t nt_bunny_rng_next(nt_bunny_rng_t *r) {
    r->state ^= r->state >> 12;
    r->state ^= r->state << 25;
    r->state ^= r->state >> 27;
    return (uint32_t)((r->state * 0x2545F4914F6CDD1DULL) >> 32);
}

/* Uniform float in [0, 1). */
static inline float nt_bunny_rng_unit(nt_bunny_rng_t *r) { return (float)nt_bunny_rng_next(r) / 4294967296.0F; }

/* Initialize a bunny at a spawn position. vx in [0, BUNNY_VX_MAX),
 * vy in [-BUNNY_VY_RANGE, +BUNNY_VY_RANGE), variant in [0, 5). */
static inline void nt_bunny_init(nt_bunny_t *b, float spawn_x, float spawn_y, nt_bunny_rng_t *rng) {
    b->x = spawn_x;
    b->y = spawn_y;
    b->vx = nt_bunny_rng_unit(rng) * BUNNY_VX_MAX;
    b->vy = (nt_bunny_rng_unit(rng) * (2.0F * BUNNY_VY_RANGE)) - BUNNY_VY_RANGE;
    b->variant = (uint8_t)(nt_bunny_rng_next(rng) % 5U);
}

/* dt-scaled integrator. Position and gravity scale by dt; bounce coefficient
 * is dimensionless (energy ratio); bounce kick is an instantaneous velocity
 * boost (px/s) — independent of dt. Side bounce inverts vx and clamps to edge.
 * Bottom bounce inverts vy with -0.85 coefficient and adds a 50%-chance random
 * kick of [0, BUNNY_BOUNCE_KICK) px/s. Top edge: clamp vy to 0 (no bounce). */
static inline void nt_bunny_step(nt_bunny_t *b, float w, float h, float dt, nt_bunny_rng_t *rng) {
    b->x += b->vx * dt;
    b->y += b->vy * dt;
    b->vy -= BUNNY_GRAVITY * dt;

    /* sides — invert + clamp to edge */
    if (b->x > w) {
        b->vx *= -1.0F;
        b->x = w;
    } else if (b->x < 0.0F) {
        b->vx *= -1.0F;
        b->x = 0.0F;
    }

    /* bottom — invert + clamp + 50% kick */
    if (b->y < 0.0F) {
        b->vy *= BUNNY_BOUNCE_BOT;
        b->y = 0.0F;
        if (nt_bunny_rng_unit(rng) > 0.5F) {
            b->vy += nt_bunny_rng_unit(rng) * BUNNY_BOUNCE_KICK;
        }
    } else if (b->y > h) {
        /* top — clamp (no bounce) */
        b->vy = 0.0F;
        b->y = h;
    }
}

#endif /* BUNNY_PHYSICS_H */
