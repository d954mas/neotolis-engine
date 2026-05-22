#ifndef NT_CLAMP_H
#define NT_CLAMP_H

#include <stdint.h>

/* Saturate float to 0..255 and round-to-nearest. Input is already in
 * 0..255 range; for 0..1 normalized input, scale first:
 *   nt_clamp_f_to_u8(value * 255.0F).
 * NaN propagates through the comparisons and produces 0 (safe default). */
static inline uint8_t nt_clamp_f_to_u8(float v) {
    if (v <= 0.0F) {
        return 0U;
    }
    if (v >= 255.0F) {
        return 255U;
    }
    return (uint8_t)(v + 0.5F);
}

#endif /* NT_CLAMP_H */
