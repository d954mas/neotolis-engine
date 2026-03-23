#include "core/nt_assert.h"

#include <stddef.h>

/* Handler variable — defined only in FULL mode (tests set this to intercept
   assert failures via longjmp). NULL by default → falls through to trap. */
#if NT_ASSERT_MODE == NT_ASSERT_FULL
nt_assert_handler_t nt_assert_handler = NULL;
#endif
