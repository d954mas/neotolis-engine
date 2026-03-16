#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include "window/nt_window.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <math.h>

/* ---- EM_JS canvas bridge ---- */

/* clang-format off */
EM_JS(double, nt_window_js_get_dpr, (void), {
    return window.devicePixelRatio || 1.0;
})

EM_JS(double, nt_window_js_get_canvas_width, (void), {
    return Module['canvas'].clientWidth;
})

EM_JS(double, nt_window_js_get_canvas_height, (void), {
    return Module['canvas'].clientHeight;
})

EM_JS(void, nt_window_js_set_backing_size, (int w, int h), {
    Module['canvas'].width = w;
    Module['canvas'].height = h;
})
/* clang-format on */

/* ---- Helpers ---- */

static void sync_sizes(void) {
    float canvas_w = (float)nt_window_js_get_canvas_width();
    float canvas_h = (float)nt_window_js_get_canvas_height();
    float device_dpr = (float)nt_window_js_get_dpr();

    float effective_dpr = fminf(device_dpr, g_nt_window.max_dpr);
    if (effective_dpr < 1.0F) {
        effective_dpr = 1.0F;
    }

    uint32_t w = (uint32_t)canvas_w;
    uint32_t h = (uint32_t)canvas_h;

    if (w == g_nt_window.width && h == g_nt_window.height && effective_dpr == g_nt_window.dpr) {
        return;
    }

    nt_window_apply_sizes(canvas_w, canvas_h, device_dpr);
    nt_window_js_set_backing_size((int)g_nt_window.fb_width, (int)g_nt_window.fb_height);
}

/* ---- Lifecycle ---- */

void nt_window_init(void) { sync_sizes(); }

void nt_window_poll(void) { sync_sizes(); }

void nt_window_shutdown(void) { /* No-op on web */ }

void nt_window_set_fullscreen(bool fullscreen) {
    /* Browser requires a user gesture (click/key) on the call stack for this
       to succeed. Calling outside a gesture handler is silently ignored. */
    if (fullscreen) {
        emscripten_request_fullscreen("#canvas", true);
    } else {
        emscripten_exit_fullscreen();
    }
}

/* ---- Presentation ---- */

void nt_window_swap_buffers(void) { /* No-op: browser swaps after rAF return */ }

void nt_window_set_vsync(nt_vsync_t mode) { (void)mode; /* No-op: browser controls vsync */ }

/* ---- Close management ---- */

bool nt_window_should_close(void) { return false; /* Web apps don't close via window API */ }

void nt_window_request_close(void) { /* No-op on web */ }

#else
typedef int nt_window_web_unused;
#endif /* NT_PLATFORM_WEB */
