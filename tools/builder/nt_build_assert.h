#ifndef NT_BUILD_ASSERT_H
#define NT_BUILD_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Always-on fatal assert for builder (never compiled out by NDEBUG).
 * The test hook may non-locally observe a failure; the builder context is not
 * reusable afterward. A returning handler is still followed by abort(). */
typedef void (*nt_build_assert_handler_t)(const char *expr, const char *file, int line);
extern nt_build_assert_handler_t nt_build_assert_handler;

#ifdef __cplusplus
}
#endif

#ifndef NT_BUILD_ASSERT
#define NT_BUILD_ASSERT(cond)                                                                                                                                                                          \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            if (nt_build_assert_handler)                                                                                                                                                               \
                nt_build_assert_handler(#cond, __FILE__, __LINE__);                                                                                                                                    \
            (void)fprintf(stderr, "FATAL: %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond);                                                                                                  \
            abort();                                                                                                                                                                                   \
        }                                                                                                                                                                                              \
    } while (0)
#endif

#endif /* NT_BUILD_ASSERT_H */
