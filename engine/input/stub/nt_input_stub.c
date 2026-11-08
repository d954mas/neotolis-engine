#include "input/nt_input_internal.h"

/* No on-screen keyboard: native, headless builds and tests. */

void nt_input_set_text_input_mode(nt_text_input_mode_t mode) { (void)mode; }
