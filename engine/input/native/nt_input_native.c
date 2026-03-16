#include "input/nt_input_internal.h"

/* GLFW callbacks moved to nt_window_native.c.
   Native input platform functions are no-ops -- nt_window drives input. */

void nt_input_platform_init(void) {}
void nt_input_platform_poll(void) {}
void nt_input_platform_shutdown(void) {}
