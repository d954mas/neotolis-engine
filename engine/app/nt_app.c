#include "app/nt_app.h"

#include "core/nt_assert.h"

/* Single definition of global frame state -- shared by all platform backends.
   Static storage: dt, time, frame, paused, mode, pending_steps are zero-initialized by C
   standard (mode RUN = 0, paused = false). Only non-zero defaults are listed here. */
nt_app_t g_nt_app = {.max_dt = 0.1F, .scale = 1.0F, .step_dt = 1.0F / 60.0F, .render_enabled = true};

/* ---- Time-control mutators (backend-agnostic; applied by nt_app_run_managed) ---- */

void nt_app_pause(void) { g_nt_app.paused = true; }
void nt_app_resume(void) { g_nt_app.paused = false; }
void nt_app_set_scale(float scale) { g_nt_app.scale = scale; }
void nt_app_set_mode(nt_app_mode_t mode) { g_nt_app.mode = mode; }
void nt_app_set_step_dt(float step_dt) { g_nt_app.step_dt = step_dt; }

void nt_app_step(int count) {
    NT_ASSERT(count >= 0); /* engine invariant: callers (L2 range-checks bot input) pass count >= 0 */
    g_nt_app.pending_steps += count;
}

void nt_app_set_render_enabled(bool enabled) { g_nt_app.render_enabled = enabled; }
bool nt_app_render_enabled(void) { return g_nt_app.render_enabled; }
