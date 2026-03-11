#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include "app/nt_app.h"
#include <emscripten/html5.h>
#include <math.h>

/* ---- File-scope statics ---- */

static nt_app_frame_fn s_frame_fn;
static double s_prev_time_ms;

/* ---- RAF callback ---- */

static EM_BOOL nt_app_web_frame(double time_ms, void *user_data) {
    (void)user_data;

    float dt = fminf((float)((time_ms - s_prev_time_ms) / 1000.0), g_nt_app.max_dt);
    s_prev_time_ms = time_ms;

    g_nt_app.dt = dt;
    g_nt_app.time += dt;
    g_nt_app.frame++;
    s_frame_fn();

    return EM_TRUE;
}

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_prev_time_ms = 0.0;

    emscripten_request_animation_frame_loop(nt_app_web_frame, NULL);
}

void nt_app_quit(void) { /* No-op on web -- browser manages lifecycle */ }

#else
/* Ensure non-empty translation unit on non-web platforms (clang-tidy) */
typedef int nt_app_web_unused;
#endif /* NT_PLATFORM_WEB */
