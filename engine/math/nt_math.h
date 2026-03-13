#ifndef NT_MATH_H
#define NT_MATH_H

/*
 * Engine math header — wraps cglm with platform workarounds.
 *
 * Consumers must link `cglm_headers` in their CMakeLists.txt.
 * This header handles:
 *   1. Windows clang + ASan: sinf/cosf/etc. fail to link as DLL imports.
 *      Redirecting to __builtin_* avoids the UCRT DLL issue entirely.
 *   2. cglm strict-warning suppressions (-Wundef, -Wstatic-in-inline)
 *      that fire under -Werror.
 */

#include <math.h>

/* Redirect standard math to compiler builtins.
   Must come after <math.h> (so declarations exist) but before cglm
   (so cglm's inline code uses builtins instead of DLL-imported symbols). */
#define sinf(x) __builtin_sinf(x)
#define cosf(x) __builtin_cosf(x)
#define tanf(x) __builtin_tanf(x)
#define sqrtf(x) __builtin_sqrtf(x)
#define fabsf(x) __builtin_fabsf(x)
#define acosf(x) __builtin_acosf(x)
#define powf(x, y) __builtin_powf(x, y)

/* Suppress cglm header warnings under -Werror */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstatic-in-inline"
#endif

#include <cglm/cglm.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#pragma GCC diagnostic pop

#define NT_PI GLM_PIf

#endif /* NT_MATH_H */
