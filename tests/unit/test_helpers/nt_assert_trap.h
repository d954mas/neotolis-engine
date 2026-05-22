#ifndef NT_TEST_HELPER_ASSERT_TRAP_H
#define NT_TEST_HELPER_ASSERT_TRAP_H

#include <setjmp.h>
#include <stdbool.h>

#include "unity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* setjmp/longjmp-based assert trap for death-tests.
 *
 *   nt_test_assert_install();    // setUp (or once globally)
 *   NT_TEST_EXPECT_ASSERT(stmt); // stmt MUST fire NT_ASSERT, else FAIL
 *
 * Requires NT_ASSERT_MODE == NT_ASSERT_FULL (default in NT_DEBUG builds).
 * The macro arms the trap, setjmp baseline, runs stmt; if NT_ASSERT
 * fires the installed handler longjmps back and the test continues.
 * If stmt returns normally, TEST_FAIL_MESSAGE. */

extern jmp_buf nt_test_assert_jmp;
extern bool nt_test_assert_armed;
extern char nt_test_assert_last_expr[256];

/* Idempotent: installs the trap into nt_assert_handler. */
void nt_test_assert_install(void);

#define NT_TEST_EXPECT_ASSERT(stmt)                                                                                                                                                                    \
    do {                                                                                                                                                                                               \
        nt_test_assert_install();                                                                                                                                                                      \
        nt_test_assert_armed = true;                                                                                                                                                                   \
        if (setjmp(nt_test_assert_jmp) == 0) {                                                                                                                                                         \
            stmt;                                                                                                                                                                                      \
            nt_test_assert_armed = false;                                                                                                                                                              \
            TEST_FAIL_MESSAGE("expected NT_ASSERT did not fire: " #stmt);                                                                                                                              \
        }                                                                                                                                                                                              \
        nt_test_assert_armed = false;                                                                                                                                                                  \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_ASSERT_TRAP_H */
