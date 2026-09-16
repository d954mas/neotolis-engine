#ifndef NT_HALF_H
#define NT_HALF_H

/* Shared bit arithmetic keeps builder and runtime binary16 rounding identical.
 * Out of line because conversion runs during asset preparation and cache misses. */

#include <stdint.h>

/* Round to nearest, ties to even. Finite |value| <= 2^-25 rounds to signed zero;
 * |value| >= 65520 rounds to signed Inf. NaN keeps its sign and becomes quiet
 * with a canonical payload. */
uint16_t nt_f32_to_f16(float value);

/* Exact for every finite and infinite encoding: every such binary16 value is
 * representable in binary32. NaN payloads are widened and quieted. */
float nt_f16_to_f32(uint16_t h);

#endif /* NT_HALF_H */
