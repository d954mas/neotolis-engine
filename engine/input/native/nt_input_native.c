#include "input/nt_input.h"

/* No-op platform backend for native desktop builds.
   Future integration point for GLFW/SDL event handling. */

void nt_input_platform_init(void) {}

void nt_input_platform_poll(void) {}

void nt_input_platform_shutdown(void) {}
