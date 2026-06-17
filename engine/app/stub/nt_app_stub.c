#include "app/nt_app.h"
#include "time/nt_time.h"

/* D-11 tick seam — devapi is dev-only; include only when enabled so the stub backend gains zero
   nt_devapi_* symbols by default. The L1 test_app_managed does NOT define NT_DEVAPI_ENABLED, so it
   stays pure L1; the seam is exercised by the L2 test (Plan 02) which defines it. */
#ifdef NT_DEVAPI_ENABLED
#include "devapi/nt_devapi_internal.h"
#endif

/* No-op platform backend for headless builds and testing.
   Simple frame loop without GLFW dependency. */

static nt_app_frame_fn s_frame_fn;
static bool s_should_quit;

#define NT_SPIN_MARGIN 0.002

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_should_quit = false;

    double prev_time = nt_time_now();

    while (!s_should_quit) {
        double now = nt_time_now();
        float dt = (float)(now - prev_time);
        if (dt > g_nt_app.max_dt) {
            dt = g_nt_app.max_dt;
        }
        prev_time = now;

        g_nt_app.dt = dt;
        g_nt_app.time += dt;
        g_nt_app.frame++;
        s_frame_fn();

        /* Frame rate cap: single sleep + spin-wait */
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

/* Managed sibling of nt_app_run (window-free): the L1 determinism-test vehicle. Owns the single
   g_nt_app.dt scalar per mode (D-07); bit-exact step_dt feed proves byte-identical runs (D-12).
   Raw nt_app_run above is untouched (TIME-06). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_app_run_managed(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_should_quit = false;

    double prev_time = nt_time_now();

#ifdef NT_DEVAPI_ENABLED
    nt_devapi_set_managed_tick(true); /* managed loop owns the deferred tick (D-11) */
#endif

    while (!s_should_quit) {
        double now = nt_time_now();
        float wall_dt = (float)(now - prev_time);
        if (wall_dt > g_nt_app.max_dt) {
            wall_dt = g_nt_app.max_dt;
        }
        prev_time = now;

        float dt = 0.0F;
        bool sim_advanced = false;
        // #region dt per mode (D-07/D-10)
        switch (g_nt_app.mode) {
        case NT_APP_MODE_RUN:
            if (!g_nt_app.paused) {
                dt = wall_dt * g_nt_app.scale; /* scale: observation only (D-13) */
                sim_advanced = true;
            }
            break;
        case NT_APP_MODE_MANUAL:
            if (g_nt_app.pending_steps > 0) {
                dt = g_nt_app.step_dt; /* EXACT: no wall clock, no max_dt clamp (D-10/D-12) */
                g_nt_app.pending_steps--;
                sim_advanced = true;
            }
            break;
        }
        // #endregion

        g_nt_app.dt = dt;
        g_nt_app.time += dt;
        if (sim_advanced) {
            g_nt_app.frame++; /* PAUSE / MANUAL-idle freeze the counter (D-09) */
#ifdef NT_DEVAPI_ENABLED
            /* Tick the deferred queue once per sim-advance, BEFORE the frame fn enqueues new
               commands (mirrors net_poll's "tick before commands enqueue" invariant). */
            nt_devapi_managed_sim_tick();
#endif
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

#ifdef NT_DEVAPI_ENABLED
    nt_devapi_set_managed_tick(false); /* relinquish tick ownership on loop exit (D-11) */
#endif
}

void nt_app_quit(void) { s_should_quit = true; }
