#ifndef NT_BUILTINS_H
#define NT_BUILTINS_H

/*
 * Compiler builtin redirects for standard math functions.
 *
 * Windows clang + ASan: sinf/cosf/fminf/etc. fail to link as DLL imports.
 * Redirecting to __builtin_* maps them to CPU instructions, bypassing CRT.
 *
 * Include this instead of nt_math.h when you need math but not cglm.
 */

#include <math.h>

#define sinf(x) __builtin_sinf(x)
#define cosf(x) __builtin_cosf(x)
#define tanf(x) __builtin_tanf(x)
#define sqrtf(x) __builtin_sqrtf(x)
#define fabsf(x) __builtin_fabsf(x)
#define acosf(x) __builtin_acosf(x)
#define powf(x, y) __builtin_powf(x, y)
#define fminf(x, y) __builtin_fminf(x, y)
#define fmaxf(x, y) __builtin_fmaxf(x, y)

#endif /* NT_BUILTINS_H */
