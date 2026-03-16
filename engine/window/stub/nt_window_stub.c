#include "window/nt_window.h"

/* No-op platform backend for headless builds and testing.
   Sets default 800x600 at 1x DPR. No GLFW dependency. */

void nt_window_init(void) { nt_window_apply_sizes(800.0F, 600.0F, 1.0F); }

void nt_window_poll(void) { /* No-op */ }

void nt_window_shutdown(void) { /* No-op */ }

void nt_window_set_fullscreen(bool fullscreen) { (void)fullscreen; }
