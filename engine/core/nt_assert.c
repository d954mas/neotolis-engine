#include "core/nt_assert.h"

#include <stddef.h>

/* Always defined so tests can link against it in any build mode.
   Only FULL mode macro actually calls the handler; TRAP/OFF ignore it. */
nt_assert_handler_t nt_assert_handler = NULL;
