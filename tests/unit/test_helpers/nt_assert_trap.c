#include "test_helpers/nt_assert_trap.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"

jmp_buf nt_test_assert_jmp;
bool nt_test_assert_armed = false;
char nt_test_assert_last_expr[256] = {0};

static void nt_test_assert_handler(const char *expr, const char *file, int line) {
    (void)file;
    (void)line;
    if (nt_test_assert_armed) {
        if (expr) {
            (void)snprintf(nt_test_assert_last_expr, sizeof(nt_test_assert_last_expr), "%s", expr);
        }
        longjmp(nt_test_assert_jmp, 1);
    }
    /* Not armed: let __builtin_trap downstream of the handler crash the test
     * loudly — this is an unexpected assert and should fail the run. */
}

void nt_test_assert_install(void) {
    if (nt_assert_handler != nt_test_assert_handler) {
        nt_assert_handler = nt_test_assert_handler;
    }
}
