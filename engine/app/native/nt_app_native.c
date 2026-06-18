#include "app/nt_app.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include "core/nt_builtins.h"

/* ---- File-scope statics (zero-initialized by C standard) ---- */

static nt_app_frame_fn s_frame_fn;

/* Spin-wait margin: sleep the bulk, spin-wait the last 2ms for precision */
#define NT_SPIN_MARGIN 0.002

/* ---- API ---- */

/* Single frame loop: dt is the one scalar, computed per mode. Default state (RUN, scale 1,
   unpaused) is a plain wall-clock advance; pause/manual-idle freeze frame (it advances only on
   a real sim-advance). Time control comes from the nt_app_* mutators (set via game code / devapi). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;

    double prev_time = nt_time_now();

    while (!nt_window_should_close()) {
        double now = nt_time_now();
        float wall_dt = fminf((float)(now - prev_time), g_nt_app.max_dt);
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
        g_nt_app.time += (double)dt;
        if (sim_advanced) {
            g_nt_app.frame++; /* PAUSE / MANUAL-idle freeze the counter */
        }
        s_frame_fn();

        /* Frame-rate cap (wall-time pacing): single sleep + spin-wait. Skipped while a MANUAL
           crunch is draining so lockstep runs full speed, not throttled to target_dt. */
        if (g_nt_app.target_dt > 0.0F && !(g_nt_app.mode == NT_APP_MODE_MANUAL && g_nt_app.pending_steps > 0)) {
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

void nt_app_quit(void) { nt_window_request_close(); }
