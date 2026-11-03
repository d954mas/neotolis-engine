#ifndef NT_BASISU_ENCODER_H
#define NT_BASISU_ENCODER_H

#include "nt_basisu_codec.h"
#include "nt_texture_format.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time encoder initialization (call before first encode) */
void nt_basisu_encoder_init(void);

/* Caller frees data with nt_basisu_encode_free(). */
typedef struct {
    uint8_t *data; /* Basis file data (caller frees via nt_basisu_encode_free) */
    uint32_t size; /* size of Basis data in bytes */
} nt_basisu_encode_result_t;

/* Compresses the caller's RGBA8 levels as they are: level l is
 * nt_texture_level_extent(width, l) x nt_texture_level_extent(height, l), and
 * Basis builds no mips of its own. `format` is the pack format (RGBA8, RGB8,
 * SRGB8_ALPHA8 or SRGB8); for a format without alpha every level must be opaque.
 * `perceptual` picks the photo metric over the linear one. Any encoder failure is
 * a build assert. Thread-safe after init; basis_threads >= 1 counts the caller. */
nt_basisu_encode_result_t nt_basisu_encode(uint32_t basis_threads, const uint8_t *const *levels, uint32_t level_count, uint32_t width, uint32_t height, nt_texture_pixel_format_t format,
                                           const nt_basisu_encode_opts_t *opts, bool perceptual);

/* Free encoder output data */
void nt_basisu_encode_free(nt_basisu_encode_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* NT_BASISU_ENCODER_H */
