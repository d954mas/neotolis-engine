#ifndef NT_ASSERT_H
#define NT_ASSERT_H

#include <stddef.h>

#include "core/nt_platform.h"

/* ---- Hookable assert handler ----
   Default NULL -- __builtin_trap(). Tests or dev server can override.
   Handler receives stringified expression, file, and line.
   Handler MUST NOT return (use longjmp or abort). */
typedef void (*nt_assert_handler_t)(const char *expr, const char *file, int line);
extern nt_assert_handler_t nt_assert_handler;

/* NT_ASSERT: debug-only check.
   No CRT dependency (assert.h pulls _wassert on MSVC which breaks clang+asan). */
#ifdef NT_ENABLE_ASSERTS
#define NT_ASSERT(cond)                                                                                                                                                                                \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            if (nt_assert_handler)                                                                                                                                                                     \
                nt_assert_handler(#cond, __FILE__, __LINE__);                                                                                                                                          \
            else                                                                                                                                                                                       \
                __builtin_trap();                                                                                                                                                                      \
        }                                                                                                                                                                                              \
    } while (0)
#else
#define NT_ASSERT(cond) ((void)0)
#endif

/* NT_ASSERT_ALWAYS: fires in both debug and release builds.
   Used for critical invariants (stale entity handles, etc.). */
#define NT_ASSERT_ALWAYS(cond)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            if (nt_assert_handler)                                                                                                                                                                     \
                nt_assert_handler(#cond, __FILE__, __LINE__);                                                                                                                                          \
            else                                                                                                                                                                                       \
                __builtin_trap();                                                                                                                                                                      \
        }                                                                                                                                                                                              \
    } while (0)

#endif /* NT_ASSERT_H */
