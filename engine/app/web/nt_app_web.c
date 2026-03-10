#include "app/nt_app.h"
#include <emscripten/html5.h>

/* ---- Global frame state ---- */

nt_app_t g_nt_app = {.max_dt = 0.1F};

/* ---- File-scope statics ---- */

static nt_app_frame_fn s_frame_fn;
static double s_prev_time_ms;
static bool s_first_frame;

/* ---- RAF callback ---- */

static EM_BOOL nt_app_web_frame(double time_ms, void *user_data) {
    (void)user_data;

    if (s_first_frame) {
        s_prev_time_ms = time_ms;
        s_first_frame = false;
        g_nt_app.dt = 0.0F;
    } else {
        float dt = (float)((time_ms - s_prev_time_ms) / 1000.0);
        s_prev_time_ms = time_ms;
        if (dt > g_nt_app.max_dt) {
            dt = g_nt_app.max_dt;
        }
        g_nt_app.dt = dt;
    }

    g_nt_app.time += g_nt_app.dt;
    g_nt_app.frame++;
    s_frame_fn();

    return EM_TRUE;
}

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_first_frame = true;

    g_nt_app.dt = 0.0F;
    g_nt_app.time = 0.0F;
    g_nt_app.frame = 0;
    /* Preserve max_dt (game may have configured it before nt_app_run) */

    emscripten_request_animation_frame_loop(nt_app_web_frame, NULL);
}

void nt_app_quit(void) {
    /* No-op on web -- browser manages lifecycle */
}

void nt_app_on_shutdown(nt_app_shutdown_fn fn) {
    /* No-op on web -- browser cleans up; pagehide deferred to future phase */
    (void)fn;
}
