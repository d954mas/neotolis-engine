#include "input/nt_input.h"

/* No-op platform backend for headless builds and testing. */

void nt_input_platform_init(void) {}

void nt_input_platform_poll(void) {}

void nt_input_platform_shutdown(void) {}
