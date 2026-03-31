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

#include <stdint.h>

#include "core/nt_builtins.h"

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

/* ---- Float16 conversion ---- */

static inline uint16_t nt_float32_to_float16(float value) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value;
    uint32_t sign = (conv.u >> 16) & 0x8000U;
    int32_t exponent = (int32_t)((conv.u >> 23) & 0xFFU) - 127 + 15;
    uint32_t mantissa = conv.u & 0x007FFFFFU;
    if (exponent <= 0) {
        return (uint16_t)sign;
    }
    if (exponent >= 31) {
        return (uint16_t)(sign | 0x7C00U);
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}

#endif /* NT_MATH_H */
