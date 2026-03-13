#include "window/nt_window.h"

void nt_window_init(void) { nt_window_apply_sizes(800.0F, 600.0F, 1.0F); }

void nt_window_poll(void) { /* No-op on native: size doesn't change */ }

void nt_window_shutdown(void) { /* Future: GLFW/SDL window cleanup */ }

void nt_window_set_fullscreen(bool fullscreen) {
    (void)fullscreen;
    /* Stub -- real GLFW implementation in Plan 02 */
}
