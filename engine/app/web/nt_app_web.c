#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include "app/nt_app.h"
#include "app/nt_app_pace_internal.h"
#include "core/nt_assert.h"
#include <emscripten/html5.h>
#include <math.h>

/* ---- File-scope statics (zero-initialized by C standard) ---- */

static nt_app_frame_fn s_frame_fn;
static double s_prev_time_ms;
static double s_next_time_ms; /* frame-cap deadline, see nt_app_pace_tick */

/* ---- RAF callback ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static EM_BOOL nt_app_web_frame(double time_ms, void *user_data) {
    (void)user_data;

    /* Frame-rate cap (wall-time pacing): skipped while a MANUAL crunch is draining so lockstep
       advances at the RAF rate, not throttled to target_dt. */
    if (!(g_nt_app.mode == NT_APP_MODE_MANUAL && g_nt_app.pending_steps > 0) && !nt_app_pace_tick(&s_next_time_ms, s_prev_time_ms, time_ms, (double)g_nt_app.target_dt * 1000.0)) {
        return EM_TRUE;
    }

    float wall_dt = fminf((float)((time_ms - s_prev_time_ms) / 1000.0), g_nt_app.max_dt);
    s_prev_time_ms = time_ms;

    float dt = 0.0F;
    bool sim_advanced = false;
    switch (g_nt_app.mode) {
    case NT_APP_MODE_RUN:
        if (!g_nt_app.paused) {
            dt = wall_dt * g_nt_app.scale; /* scale: observation only, not a determinism primitive */
            sim_advanced = true;
        }
        break;
    case NT_APP_MODE_MANUAL:
        if (g_nt_app.pending_steps > 0) {
            dt = g_nt_app.step_dt; /* exact step_dt: no wall clock, no max_dt clamp (lockstep determinism) */
            g_nt_app.pending_steps--;
            sim_advanced = true;
        }
        break;
    }

    g_nt_app.dt = dt;
    g_nt_app.time += (double)dt;
    if (sim_advanced) {
        g_nt_app.frame++; /* PAUSE / MANUAL-idle freeze the counter */
    }
    s_frame_fn();

    return EM_TRUE;
}

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    emscripten_request_animation_frame_loop(nt_app_web_frame, NULL);
}

void nt_app_quit(void) { NT_ASSERT(0 && "nt_app_quit not supported on web"); }

#else
/* Ensure non-empty translation unit on non-web platforms (clang-tidy) */
typedef int nt_app_web_unused;
#endif /* NT_PLATFORM_WEB */
