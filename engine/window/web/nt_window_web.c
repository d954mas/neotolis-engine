#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include "window/nt_window.h"
#include <emscripten.h>
#include <math.h>

/* ---- EM_JS canvas bridge ---- */

/* clang-format off */
EM_JS(double, nt_window_js_get_dpr, (void), {
    return window.devicePixelRatio || 1.0;
})

EM_JS(double, nt_window_js_get_canvas_width, (void), {
    return Module.canvas.clientWidth;
})

EM_JS(double, nt_window_js_get_canvas_height, (void), {
    return Module.canvas.clientHeight;
})

EM_JS(void, nt_window_js_set_backing_size, (int w, int h), {
    Module.canvas.width = w;
    Module.canvas.height = h;
})
/* clang-format on */

/* ---- Lifecycle ---- */

void nt_window_init(void) { nt_window_poll(); }

void nt_window_poll(void) {
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

void nt_window_shutdown(void) { /* No-op on web */ }

#else
typedef int nt_window_web_unused;
#endif /* NT_PLATFORM_WEB */
