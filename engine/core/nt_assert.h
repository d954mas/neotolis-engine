#ifndef NT_ASSERT_H
#define NT_ASSERT_H

#include "core/nt_platform.h"

/* Assert mode constants (for readability in headers, not needed in CMake). */
#define NT_ASSERT_OFF 0
#define NT_ASSERT_TRAP 1
#define NT_ASSERT_FULL 2

/* NT_ASSERT_MODE levels:
   0 (OFF)  — ((void)0), no checks, minimal binary.
   1 (TRAP) — __builtin_trap() on failure, no strings.
   2 (FULL) — hookable handler with expr/file/line strings (tests). */

#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* Handler type: receives stringified expression, file, and line.
   Handler MUST NOT return (use longjmp or abort). */
typedef void (*nt_assert_handler_t)(const char *expr, const char *file, int line);
extern nt_assert_handler_t nt_assert_handler;

#define NT_ASSERT(cond)                                                                                                                                                                                \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            if (nt_assert_handler)                                                                                                                                                                     \
                nt_assert_handler(#cond, __FILE__, __LINE__);                                                                                                                                          \
            __builtin_trap();                                                                                                                                                                          \
        }                                                                                                                                                                                              \
    } while (0)

#elif NT_ASSERT_MODE == NT_ASSERT_TRAP

#define NT_ASSERT(cond)                                                                                                                                                                                \
    do {                                                                                                                                                                                               \
        if (!(cond))                                                                                                                                                                                   \
            __builtin_trap();                                                                                                                                                                          \
    } while (0)

#else /* NT_ASSERT_OFF */

#define NT_ASSERT(cond) ((void)0)

#endif

#endif /* NT_ASSERT_H */
