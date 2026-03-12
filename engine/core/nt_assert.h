#ifndef NT_ASSERT_H
#define NT_ASSERT_H

#include "core/nt_platform.h"

/* NT_ASSERT: debug-only check via __builtin_trap().
   No CRT dependency (assert.h pulls _wassert on MSVC which breaks clang+asan).
   Swap to assert() if CRT linkage is fixed later. */
#ifdef NT_ENABLE_ASSERTS
#define NT_ASSERT(cond)                                                                                                                                                                                \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            __builtin_trap();                                                                                                                                                                          \
        }                                                                                                                                                                                              \
    } while (0)
#else
#define NT_ASSERT(cond) ((void)0)
#endif

#endif /* NT_ASSERT_H */
