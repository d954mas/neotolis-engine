#include "app/nt_app.h"
#include "time/nt_time.h"

/* ---- Global frame state ---- */

nt_app_t g_nt_app = {.max_dt = 0.1F};

/* ---- File-scope statics ---- */

static nt_app_frame_fn s_frame_fn;
static nt_app_shutdown_fn s_shutdown_fn;
static bool s_should_quit;

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    /* Stub -- RED phase: intentionally does nothing */
    s_frame_fn = fn;
    s_should_quit = false;
}

void nt_app_quit(void) { s_should_quit = true; }

void nt_app_on_shutdown(nt_app_shutdown_fn fn) { s_shutdown_fn = fn; }
