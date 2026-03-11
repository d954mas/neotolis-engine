#include "app/nt_app.h"
#include "time/nt_time.h"
#include <math.h>

/* ---- File-scope statics ---- */

static nt_app_frame_fn s_frame_fn;
static nt_app_shutdown_fn s_shutdown_fn;
static bool s_should_quit;

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_should_quit = false;

    g_nt_app.dt = 0.0F;
    g_nt_app.time = 0.0F;
    g_nt_app.frame = 0;

    double prev_time = nt_time_now();

    while (!s_should_quit) {
        double now = nt_time_now();
        float dt = fminf((float)(now - prev_time), g_nt_app.max_dt);
        prev_time = now;

        g_nt_app.dt = dt;
        g_nt_app.time += dt;
        g_nt_app.frame++;
        s_frame_fn();
    }

    /* Shutdown callback (if registered) */
    if (s_shutdown_fn != NULL) {
        s_shutdown_fn();
    }
}

void nt_app_quit(void) { s_should_quit = true; }

void nt_app_on_shutdown(nt_app_shutdown_fn fn) { s_shutdown_fn = fn; }
