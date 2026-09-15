#include "nt_half.h"

uint16_t nt_f32_to_f16(float value) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value;

    uint32_t sign = (conv.u >> 16) & 0x8000U;
    uint32_t exponent = (conv.u >> 23) & 0xFFU;
    uint32_t mantissa = conv.u & 0x007FFFFFU;

    if (exponent == 0xFFU) {
        return (uint16_t)(sign | 0x7C00U | ((mantissa != 0U) ? 0x0200U : 0U));
    }

    int32_t exp16 = (int32_t)exponent - 127 + 15;
    if (exp16 >= 31) {
        return (uint16_t)(sign | 0x7C00U);
    }
    if (exp16 <= 0) {
        /* Half subnormal: restore the implicit bit and round the shifted-out tail.
         * Below 2^-25 even the round bit is gone, so the result is a signed zero. */
        if (exp16 < -10) {
            return (uint16_t)sign;
        }
        uint32_t full = mantissa | 0x00800000U;
        uint32_t shift = (uint32_t)(14 - exp16); /* 14..24 */
        uint32_t result = full >> shift;
        uint32_t tail = full & ((1U << shift) - 1U);
        uint32_t halfway = 1U << (shift - 1U);
        if ((tail > halfway) || ((tail == halfway) && ((result & 1U) != 0U))) {
            result++;
        }
        return (uint16_t)(sign | result);
    }

    /* Normal half. A mantissa carry propagates into the exponent, and an
     * exponent carry out of 30 lands on 0x7C00 -- the correct overflow. */
    uint32_t result = ((uint32_t)exp16 << 10) | (mantissa >> 13);
    uint32_t tail = mantissa & 0x1FFFU;
    if ((tail > 0x1000U) || ((tail == 0x1000U) && ((result & 1U) != 0U))) {
        result++;
    }
    return (uint16_t)(sign | result);
}

float nt_f16_to_f32(uint16_t h) {
    uint32_t sign = ((uint32_t)h & 0x8000U) << 16;
    uint32_t exponent = ((uint32_t)h >> 10) & 0x1FU;
    uint32_t mantissa = (uint32_t)h & 0x03FFU;

    union {
        float f;
        uint32_t u;
    } conv;

    if (exponent == 0U) {
        if (mantissa == 0U) {
            conv.u = sign;
        } else {
            /* Normalize the subnormal: each left shift costs one exponent step,
             * starting from the 2^-14 that an exponent field of 1 would encode. */
            uint32_t shift = 0U;
            while ((mantissa & 0x0400U) == 0U) {
                mantissa <<= 1U;
                shift++;
            }
            conv.u = sign | ((113U - shift) << 23) | ((mantissa & 0x03FFU) << 13);
        }
    } else if (exponent == 0x1FU) {
        /* A signaling half NaN must not widen into a signaling float NaN: set the
         * quiet bit on any payload. Inf (mantissa 0) stays exact. */
        conv.u = sign | 0x7F800000U | (mantissa << 13) | ((mantissa != 0U) ? 0x00400000U : 0U);
    } else {
        conv.u = sign | ((exponent + 112U) << 23) | (mantissa << 13);
    }
    return conv.f;
}
