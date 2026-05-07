#ifndef BUNNY_PHYSICS_H
#define BUNNY_PHYSICS_H

/* Pure-C bunnymark physics, testable in isolation (stdint only, no engine
 * deps). The main reference is britzl/defold-bunnymark:
 * bunnymark/update_native_position_velocity.
 *
 * Reference constants:
 *   position = (random(640), random(930, 1030), 0)
 *   velocity = -random(200)
 *   velocity -= 1200 * dt
 *   y += velocity * dt
 *   if y < 50: y = 50; velocity = -velocity
 *
 * Coordinate convention is y-up, bottom-left origin (D-25). */

#include <stdint.h>

#define BUNNY_DEFOLD_REFERENCE_WIDTH 640.0F
#define BUNNY_DEFOLD_SPAWN_Y_MIN 930.0F
#define BUNNY_DEFOLD_SPAWN_Y_MAX 1030.0F
#define BUNNY_DEFOLD_VELOCITY_MAX 200.0F
#define BUNNY_DEFOLD_ACCEL 1200.0F
#define BUNNY_DEFOLD_FLOOR_Y 50.0F

typedef struct {
    float x;
    float y;
    float vy;
    uint8_t variant; /* 0..4 */
} nt_bunny_t;

/* Deterministic PRNG state -- xorshift64* (good distribution, no heap, fast). */
typedef struct {
    uint64_t state;
} nt_bunny_rng_t;

/* xorshift64* -- must be seeded with a non-zero state. */
static inline uint32_t nt_bunny_rng_next(nt_bunny_rng_t *r) {
    r->state ^= r->state >> 12;
    r->state ^= r->state << 25;
    r->state ^= r->state >> 27;
    return (uint32_t)((r->state * 0x2545F4914F6CDD1DULL) >> 32);
}

/* Uniform float in [0, 1). */
static inline float nt_bunny_rng_unit(nt_bunny_rng_t *r) { return (float)nt_bunny_rng_next(r) / 4294967296.0F; }

/* Initialize from Defold's random ranges, scaled horizontally to the current
 * viewport. The vertical spawn range intentionally stays in reference pixels,
 * so on 800x600 bunnies enter from above the visible screen like Defold. */
static inline void nt_bunny_init_defold(nt_bunny_t *b, float viewport_w, nt_bunny_rng_t *rng) {
    b->x = nt_bunny_rng_unit(rng) * viewport_w;
    b->y = BUNNY_DEFOLD_SPAWN_Y_MIN + (nt_bunny_rng_unit(rng) * (BUNNY_DEFOLD_SPAWN_Y_MAX - BUNNY_DEFOLD_SPAWN_Y_MIN));
    b->vy = -(nt_bunny_rng_unit(rng) * BUNNY_DEFOLD_VELOCITY_MAX);
    b->variant = (uint8_t)(nt_bunny_rng_next(rng) % 5U);
}

/* Backward-compatible wrapper for tests/callers that already pass spawn coords. */
static inline void nt_bunny_init(nt_bunny_t *b, float spawn_x, float spawn_y, nt_bunny_rng_t *rng) {
    b->x = spawn_x;
    b->y = spawn_y;
    b->vy = -(nt_bunny_rng_unit(rng) * BUNNY_DEFOLD_VELOCITY_MAX);
    b->variant = (uint8_t)(nt_bunny_rng_next(rng) % 5U);
}

static inline void nt_bunny_step(nt_bunny_t *b, float dt) {
    b->vy -= BUNNY_DEFOLD_ACCEL * dt;

    float new_y = b->y + (b->vy * dt);
    if (new_y < BUNNY_DEFOLD_FLOOR_Y) {
        new_y = BUNNY_DEFOLD_FLOOR_Y + (BUNNY_DEFOLD_FLOOR_Y - new_y);
        b->vy = -b->vy;
    }
    b->y = new_y;
}

#endif /* BUNNY_PHYSICS_H */
