#include "core/nt_assert.h"

/* Default NULL -- falls through to __builtin_trap().
   Tests (or dev server) set this to intercept assertion failures. */
nt_assert_handler_t nt_assert_handler = NULL;
