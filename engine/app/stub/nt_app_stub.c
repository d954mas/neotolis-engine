#include "app/nt_app.h"
#include "time/nt_time.h"

/* No-op platform backend for headless builds and testing.
   Simple frame loop without GLFW dependency. */

static nt_app_frame_fn s_frame_fn;
static bool s_should_quit;

#define NT_SPIN_MARGIN 0.002

/* Single frame loop (window-free stub): dt computed per mode. Default (RUN, scale 1, unpaused) is a
   plain wall-clock advance; MANUAL feeds an exact step_dt per queued step (byte-identical runs).
   frame advances only on a sim-advance, so pause/manual-idle freeze it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_should_quit = false;

    double prev_time = nt_time_now();

    while (!s_should_quit) {
        double now = nt_time_now();
        float wall_dt = (float)(now - prev_time);
        if (wall_dt > g_nt_app.max_dt) {
            wall_dt = g_nt_app.max_dt;
        }
        prev_time = now;

        float dt = 0.0F;
        bool sim_advanced = false;
        // #region dt per mode
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
        // #endregion

        g_nt_app.dt = dt;
        g_nt_app.time += dt;
        if (sim_advanced) {
            g_nt_app.frame++; /* PAUSE / MANUAL-idle freeze the counter */
        }
        s_frame_fn();

        /* Frame rate cap: single sleep + spin-wait (verbatim from nt_app_run) */
        if (g_nt_app.target_dt > 0.0F) {
            double target = prev_time + (double)g_nt_app.target_dt;
            double remaining = target - nt_time_now();
            if (remaining > NT_SPIN_MARGIN) {
                nt_time_sleep(remaining - NT_SPIN_MARGIN);
            }
            while (nt_time_now() < target) { /* spin */
            }
        }
    }
}

void nt_app_quit(void) { s_should_quit = true; }
