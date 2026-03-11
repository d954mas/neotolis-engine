#include "window/nt_window.h"

#include <math.h>

/* Single definition of global window state -- shared by all platform backends.
   Static storage: width, height, fb_width, fb_height, dpr are zero-initialized
   by C standard. */
nt_window_t g_nt_window = {.max_dpr = 2.0F};

void nt_window_apply_sizes(float canvas_w, float canvas_h, float device_dpr) {
    float effective_dpr = fminf(device_dpr, g_nt_window.max_dpr);
    if (effective_dpr < 1.0F) {
        effective_dpr = 1.0F; /* Safety: never go below 1x */
    }

    g_nt_window.width = (uint32_t)canvas_w;
    g_nt_window.height = (uint32_t)canvas_h;
    g_nt_window.dpr = effective_dpr;
    g_nt_window.fb_width = (uint32_t)roundf(canvas_w * effective_dpr);
    g_nt_window.fb_height = (uint32_t)roundf(canvas_h * effective_dpr);
}
