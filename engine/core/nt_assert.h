#ifndef NT_ASSERT_H
#define NT_ASSERT_H

#include "core/nt_platform.h"

/* Asserts are contracts, not error handling.
   A failed assert means the program is broken — continuing would mask bugs.
   Release default is TRAP (immediate crash, no strings, minimal overhead).
   OFF mode available via -DNT_ASSERT_MODE=0 for final production builds.
   Never use asserts for conditions that can legitimately occur at runtime
   (missing files, user input, etc) — those are error handling. */

/* Assert mode constants (for readability in headers, not needed in CMake). */
#define NT_ASSERT_OFF 0
#define NT_ASSERT_TRAP 1
#define NT_ASSERT_FULL 2

/* NT_ASSERT_MODE default: debug → FULL, release → TRAP.
   CMake can override via -DNT_ASSERT_MODE=<level>. */
#ifndef NT_ASSERT_MODE
#ifdef NT_DEBUG
#define NT_ASSERT_MODE NT_ASSERT_FULL
#else
#define NT_ASSERT_MODE NT_ASSERT_TRAP
#endif
#endif

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
