#ifndef NT_APP_PACE_INTERNAL_H
#define NT_APP_PACE_INTERNAL_H

#include <stdbool.h>

/* RAF timestamps jitter around vsync; a tick this much before its deadline still runs. */
#define NT_APP_PACE_TOLERANCE_MS 2.0

/* Frame-cap decision for a display-paced loop (web RAF). *next_ms is the deadline of the next
   frame. Deadlines advance by whole target periods, so a display whose period does not divide
   target_ms still averages 1 / target_ms; a tick more than one period late (hidden tab, stall)
   restarts the schedule at now instead of running catch-up frames. target_ms <= 0 is uncapped. */
static inline bool nt_app_pace_tick(double *next_ms, double last_ms, double now_ms, double target_ms) {
    if (target_ms <= 0.0) {
        return true;
    }
    /* A shorter live cap must not wait for a deadline set by the prior cap. */
    if (*next_ms - last_ms > target_ms + NT_APP_PACE_TOLERANCE_MS) {
        *next_ms = last_ms + target_ms;
    }
    if (now_ms < *next_ms - NT_APP_PACE_TOLERANCE_MS) {
        return false;
    }
    *next_ms = (now_ms - *next_ms > target_ms) ? now_ms + target_ms : *next_ms + target_ms;
    return true;
}

#endif /* NT_APP_PACE_INTERNAL_H */
