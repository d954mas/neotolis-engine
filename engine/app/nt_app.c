#include "app/nt_app.h"

#include <limits.h>
#include <math.h>

#include "core/nt_assert.h"

/* Single definition of global frame state -- shared by all platform backends.
   Static storage: dt, time, frame, paused, mode, pending_steps are zero-initialized by C
   standard (mode RUN = 0, paused = false). Only non-zero defaults are listed here. */
nt_app_t g_nt_app = {.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};

/* ---- Time-control mutators (backend-agnostic; applied by nt_app_run) ---- */

void nt_app_pause(void) { g_nt_app.paused = true; }
void nt_app_resume(void) { g_nt_app.paused = false; }
/* L1 invariants: callers are trusted game code (bot input is range-checked at L2 → bad_params).
   A NaN/negative scale or non-positive step_dt poisons dt/time or stalls the loop — fail early. */
void nt_app_set_scale(float scale) {
    NT_ASSERT(isfinite(scale) && scale >= 0.0F);
    g_nt_app.scale = scale;
}
void nt_app_set_mode(nt_app_mode_t mode) {
    NT_ASSERT(mode == NT_APP_MODE_RUN || mode == NT_APP_MODE_MANUAL);
    g_nt_app.mode = mode;
}
void nt_app_set_step_dt(float step_dt) {
    NT_ASSERT(isfinite(step_dt) && step_dt > 0.0F);
    g_nt_app.step_dt = step_dt;
}

void nt_app_step(int count) {
    NT_ASSERT(count >= 0); /* engine invariant: callers (L2 range-checks bot input) pass count >= 0 */
    /* Saturate instead of overflowing: pending_steps is drained one per advance, so an absurd
       backlog only delays — it must never invoke signed-overflow UB. pending_steps >= 0 and
       count >= 0, so INT_MAX - pending_steps is a safe non-negative bound to test against. */
    if (count > INT_MAX - g_nt_app.pending_steps) {
        g_nt_app.pending_steps = INT_MAX;
    } else {
        g_nt_app.pending_steps += count;
    }
}

void nt_app_set_render_enabled(bool enabled) { g_nt_app.render_enabled = enabled; }
bool nt_app_render_enabled(void) { return g_nt_app.render_enabled; }
