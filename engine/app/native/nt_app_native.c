#include "app/nt_app.h"
#include "time/nt_time.h"
#include <math.h>

/* ---- File-scope statics (zero-initialized by C standard) ---- */

static nt_app_frame_fn s_frame_fn;
static bool s_should_quit;

/* Spin-wait margin: sleep the bulk, spin-wait the last 2ms for precision */
#define NT_SPIN_MARGIN 0.002

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_should_quit = false;

    double prev_time = nt_time_now();

    while (!s_should_quit) {
        double now = nt_time_now();
        float dt = fminf((float)(now - prev_time), g_nt_app.max_dt);
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
            while (nt_time_now() < target) { /* spin */ }
        }
    }
}

void nt_app_quit(void) { s_should_quit = true; }
