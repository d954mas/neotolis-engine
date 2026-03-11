#ifndef NT_TIME_H
#define NT_TIME_H

#include "core/nt_types.h"

/* ---- Accumulator-based fixed timestep ---- */

typedef struct nt_accumulator_t {
    float fixed_dt;    /* Fixed timestep interval (seconds) */
    float accumulator; /* Accumulated time debt (seconds) */
    int max_steps;     /* Spiral-of-death clamp */
} nt_accumulator_t;

void nt_accumulator_init(nt_accumulator_t *acc, float fixed_dt, int max_steps);
int nt_accumulator_update(nt_accumulator_t *acc, float dt);

/* ---- High-resolution monotonic clock ---- */

double nt_time_now(void);     /* Monotonic seconds */
uint64_t nt_time_nanos(void); /* Monotonic nanoseconds */

#endif /* NT_TIME_H */
