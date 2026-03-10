/* TDD RED stub -- intentionally wrong implementations to verify tests fail */
#include "time/nt_time.h"

void nt_accumulator_init(nt_accumulator_t *acc, float fixed_dt,
                          int max_steps) {
    (void)acc;
    (void)fixed_dt;
    (void)max_steps;
}

void nt_accumulator_add(nt_accumulator_t *acc, float dt) {
    (void)acc;
    (void)dt;
}

bool nt_accumulator_step(nt_accumulator_t *acc) {
    (void)acc;
    return false;
}

float nt_accumulator_alpha(const nt_accumulator_t *acc) {
    (void)acc;
    return 0.0f;
}

double nt_time_now(void) {
    return 0.0;
}

uint64_t nt_time_nanos(void) {
    return 0;
}
