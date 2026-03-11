#include "window/nt_window.h"

#include <math.h>

/* Single definition of global window state -- shared by all platform backends.
   Static storage: width, height, fb_width, fb_height, dpr are zero-initialized
   by C standard. */
nt_window_t g_nt_window = {.max_dpr = 2.0F};

void nt_window_apply_sizes(float css_w, float css_h, float device_dpr) {
    /* STUB -- TDD RED phase: intentionally empty so tests fail */
    (void)css_w;
    (void)css_h;
    (void)device_dpr;
}
