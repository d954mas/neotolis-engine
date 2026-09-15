#ifndef NT_HALF_H
#define NT_HALF_H

/* IEEE 754 binary16 conversion, round-to-nearest-even, pure bit arithmetic.
 * Builder FLOAT16 streams and the runtime animation bank both convert through
 * this header so a value baked offline and a value computed at runtime round to
 * the same bits. Out of line on purpose: both callers are cold (glyph-cache
 * miss, bank bake). */

#include <stdint.h>

/* Overflow saturates to +-Inf; inputs below half-subnormal range round to +-0;
 * NaN keeps its sign and becomes a quiet NaN with a canonical payload. */
uint16_t nt_f32_to_f16(float value);

/* Exact for every finite and infinite encoding: every such binary16 value is
 * representable in binary32. NaN payloads are widened and quieted. */
float nt_f16_to_f32(uint16_t h);

#endif /* NT_HALF_H */
