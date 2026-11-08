#include "input/nt_input_internal.h"

/* Backend with nothing to stage: native (GLFW callbacks call the entry points directly), headless
   builds and tests. */

void nt_input_platform_init(void) {}

void nt_input_platform_poll(void) {}

void nt_input_set_text_input_mode(nt_text_input_mode_t mode) { (void)mode; }
