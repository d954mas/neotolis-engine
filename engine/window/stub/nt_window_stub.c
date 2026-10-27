#include "window/nt_window.h"

/* No-op platform backend for headless builds and testing.
   Sets default 800x600 at 1x DPR. No GLFW dependency. */

void nt_window_init(void) { nt_window_apply_sizes(800.0F, 600.0F, 1.0F); }

void nt_window_poll(void) { /* No-op */ }

void nt_window_shutdown(void) { /* No-op */ }

void nt_window_set_fullscreen(bool fullscreen) { (void)fullscreen; }

/* ---- Presentation ---- */

void nt_window_swap_buffers(void) { /* No-op */ }

void nt_window_set_vsync(nt_vsync_t mode) { (void)mode; }

/* ---- Close management ---- */

bool nt_window_should_close(void) { return false; }

void nt_window_request_close(void) { /* No-op */ }
