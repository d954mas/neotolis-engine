#ifndef NT_ASSERT_H
#define NT_ASSERT_H

#include "core/nt_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Asserts are contracts, not error handling.
   A failed assert means the program is broken — continuing would mask bugs.
   Release presets select TRAP (immediate crash, no strings, minimal overhead).
   A failed assert never continues in either mode; do not add recovery after it.
   Assert expressions must be side-effect-free.
   Never use asserts for conditions that can legitimately occur at runtime
   (missing files, user input, etc) — those are error handling. */

/* Assert mode constants (for readability in headers, not needed in CMake). */
#define NT_ASSERT_TRAP 1
#define NT_ASSERT_FULL 2

#ifndef NT_ASSERT_MODE
#error "NT_ASSERT_MODE must be defined by the nt_core target (1..2)"
#elif NT_ASSERT_MODE != NT_ASSERT_TRAP && NT_ASSERT_MODE != NT_ASSERT_FULL
#error "NT_ASSERT_MODE must be 1 (TRAP) or 2 (FULL)"
#endif

/* Handler type: receives stringified expression, file, and line.
   Handler MUST NOT return (use longjmp or abort).
   Always defined so tests can link against it in any build mode. */
typedef void (*nt_assert_handler_t)(const char *expr, const char *file, int line);
extern nt_assert_handler_t nt_assert_handler;

/* NT_ASSERT_MODE levels:
   1 (TRAP) — __builtin_trap() on failure, no strings.
   2 (FULL) — hookable handler with expr/file/line strings (tests). */

#if NT_ASSERT_MODE == NT_ASSERT_FULL

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

#endif

#ifdef __cplusplus
}
#endif

#endif /* NT_ASSERT_H */
