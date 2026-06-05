#include "core/nt_assert.h"

#include <stddef.h>
#include <stdio.h>

/* Loud default in FULL mode (debug builds): print expr + location to stderr before the
 * __builtin_trap fires. Without this, every assert ends in a bare illegal-instruction with
 * no clue WHICH invariant tripped — silent traps cost hours bisecting.
 * Tests override this with their setjmp/longjmp handler via nt_test_assert_install. */
static void nt_assert_default_handler(const char *expr, const char *file, int line) {
    (void)fprintf(stderr, "\nNT_ASSERT failed:\n  expr: %s\n  at:   %s:%d\n", expr ? expr : "(null)", file ? file : "(null)", line);
    (void)fflush(stderr);
}

/* Always defined so tests can link against it in any build mode.
 * FULL mode (debug) uses the default printer; TRAP/OFF (release) ignore the handler. */
nt_assert_handler_t nt_assert_handler = nt_assert_default_handler;
