#ifndef NT_TEST_HELPER_BUILD_ASSERT_TRAP_H
#define NT_TEST_HELPER_BUILD_ASSERT_TRAP_H

#include <setjmp.h>
#include <string.h>

/* clang-format off */
#include "nt_builder.h"
#include "unity.h"
/* clang-format on */

/* setjmp/longjmp trap for NT_BUILD_ASSERT death tests in the builder suites.
 *
 *   EXPECT_BUILD_ASSERT_MATCH(stmt, "assert text");
 *
 * Which rule fired is the claim: a death test that only sees "some assert"
 * passes on an unrelated precondition too, so the expression text is matched.
 * A death test longjmps out of the builder past its live allocations, which
 * are abandoned on purpose; LSan (Debug preset) must not read them as builder
 * leaks, while the happy paths stay under its watch. Header-only: each test
 * binary owns one trap. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#define NT_TEST_LSAN_DISABLE() __lsan_disable()
#define NT_TEST_LSAN_ENABLE() __lsan_enable()
#endif
#elif defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define NT_TEST_LSAN_DISABLE() __lsan_disable()
#define NT_TEST_LSAN_ENABLE() __lsan_enable()
#endif
#ifndef NT_TEST_LSAN_DISABLE
#define NT_TEST_LSAN_DISABLE() ((void)0)
#define NT_TEST_LSAN_ENABLE() ((void)0)
#endif

static jmp_buf s_build_assert_jmp;
static const char *s_build_assert_expr;

static void test_build_assert_handler(const char *expr, const char *file, int line) {
    s_build_assert_expr = expr;
    (void)file;
    (void)line;
    longjmp(s_build_assert_jmp, 1);
}

#define EXPECT_BUILD_ASSERT_MATCH(code, expected)                                                                                                                                                      \
    do {                                                                                                                                                                                               \
        s_build_assert_expr = NULL;                                                                                                                                                                    \
        nt_build_assert_handler = test_build_assert_handler;                                                                                                                                           \
        NT_TEST_LSAN_DISABLE();                                                                                                                                                                        \
        if (setjmp(s_build_assert_jmp) == 0) {                                                                                                                                                         \
            code;                                                                                                                                                                                      \
            NT_TEST_LSAN_ENABLE();                                                                                                                                                                     \
            nt_build_assert_handler = NULL;                                                                                                                                                            \
            TEST_FAIL_MESSAGE("expected NT_BUILD_ASSERT to fire: " expected);                                                                                                                          \
        }                                                                                                                                                                                              \
        NT_TEST_LSAN_ENABLE();                                                                                                                                                                         \
        nt_build_assert_handler = NULL;                                                                                                                                                                \
        TEST_ASSERT_TRUE_MESSAGE(s_build_assert_expr &&strstr(s_build_assert_expr, (expected)), "a different NT_BUILD_ASSERT fired: " expected);                                                       \
    } while (0)

#endif /* NT_TEST_HELPER_BUILD_ASSERT_TRAP_H */
