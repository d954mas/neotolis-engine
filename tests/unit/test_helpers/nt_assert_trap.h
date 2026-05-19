#ifndef NT_TEST_HELPER_ASSERT_TRAP_H
#define NT_TEST_HELPER_ASSERT_TRAP_H

#include <setjmp.h>
#include <stdbool.h>

#include "unity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* setjmp/longjmp-based assert trap for death-tests.
 * Usage:
 *   nt_test_assert_install();          // in setUp (or once globally)
 *   NT_TEST_EXPECT_ASSERT(stmt);       // wraps a statement expected to fire NT_ASSERT
 *
 * Requires NT_ASSERT_MODE == NT_ASSERT_FULL (default for NT_DEBUG builds, which
 * is the native-debug preset where ctest runs).
 *
 * NT_TEST_EXPECT_ASSERT contract:
 *   - arms the trap (sets nt_test_assert_armed = true)
 *   - setjmp baseline
 *   - executes stmt
 *   - if stmt returns without firing NT_ASSERT  -> TEST_FAIL_MESSAGE
 *   - if stmt fires NT_ASSERT                   -> handler longjmps back here; trap disarmed; test continues
 *   - __builtin_trap in nt_assert.h follows the handler call but is unreachable
 *     because the handler longjmps first.
 */

extern jmp_buf nt_test_assert_jmp;
extern bool nt_test_assert_armed;
extern char nt_test_assert_last_expr[256];

/* Install nt_test_assert_handler into nt_assert_handler. Idempotent. */
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
