#ifndef NT_FPNG_H
#define NT_FPNG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time CPU-feature detect (SSE4.1). Call once at startup before any encode. */
void nt_fpng_init(void);

/* Encode a tightly-packed RGB8 image (row pitch w*3, top-left origin) to a PNG
 * in the caller-provided buffer. Returns the PNG byte length, or 0 on encode
 * failure or when the result would exceed out_cap (no write past out_cap). */
uint32_t nt_fpng_encode_rgb(const void *rgb, uint32_t w, uint32_t h, uint8_t *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* NT_FPNG_H */
