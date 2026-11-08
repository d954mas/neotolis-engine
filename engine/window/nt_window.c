#include "window/nt_window.h"

#include <math.h>

/* Global window state, shared by all platform backends. */
nt_window_t g_nt_window = {.max_dpr = 2.0F, .resizable = true};

float nt_window_effective_dpr(float device_dpr) {
    const float dpr = fminf(device_dpr, g_nt_window.max_dpr);
    return dpr < 1.0F ? 1.0F : dpr; /* Safety: never go below 1x */
}

void nt_window_apply_sizes(float canvas_w, float canvas_h, float device_dpr) {
    const float effective_dpr = nt_window_effective_dpr(device_dpr);

    g_nt_window.width = (uint32_t)canvas_w;
    g_nt_window.height = (uint32_t)canvas_h;
    g_nt_window.dpr = effective_dpr;
    g_nt_window.fb_width = (uint32_t)roundf(canvas_w * effective_dpr);
    g_nt_window.fb_height = (uint32_t)roundf(canvas_h * effective_dpr);
}
