#include "core/nt_assert.h"

/* Default handler exists only in FULL mode so TRAP/OFF builds don't drag in <stdio.h>
 * (release contract: "TRAP immediate crash, no strings"). Tests can still install their
 * own handler at any mode via nt_assert_handler. */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

#include <stdio.h>

static void nt_assert_default_handler(const char *expr, const char *file, int line) {
    (void)fprintf(stderr, "\nNT_ASSERT failed:\n  expr: %s\n  at:   %s:%d\n", expr ? expr : "(null)", file ? file : "(null)", line);
    (void)fflush(stderr);
}

nt_assert_handler_t nt_assert_handler = nt_assert_default_handler;

#else

nt_assert_handler_t nt_assert_handler = NULL;

#endif
