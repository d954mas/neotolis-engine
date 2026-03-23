#ifndef NT_BASISU_TRANSCODER_H
#define NT_BASISU_TRANSCODER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transcode target formats (mirrors basist::transcoder_texture_format subset) */
typedef enum {
    NT_BASISU_FORMAT_BC7_RGBA = 0,      /* GL_COMPRESSED_RGBA_BPTC_UNORM (0x8E8C) */
    NT_BASISU_FORMAT_ASTC_4x4_RGBA = 1, /* GL_COMPRESSED_RGBA_ASTC_4x4_KHR (0x93B0) */
    NT_BASISU_FORMAT_ETC2_RGBA = 2,     /* GL_COMPRESSED_RGBA8_ETC2_EAC (0x9278) */
    NT_BASISU_FORMAT_ETC1_RGB = 3,      /* GL_COMPRESSED_RGB8_ETC2 (0x9274) -- ETC1 subset */
    NT_BASISU_FORMAT_RGBA32 = 4,        /* uncompressed RGBA8 fallback */
} nt_basisu_format_t;

/* One-time transcoder init (call at startup before any transcode) */
void nt_basisu_transcoder_global_init(void);

/* Validate a Basis file header. Returns true if valid. */
bool nt_basisu_validate_header(const void *basis_data, uint32_t basis_size);

/* Get number of mip levels for image 0 in the Basis file */
uint32_t nt_basisu_get_level_count(const void *basis_data, uint32_t basis_size);

/* Get dimensions and block count for a specific mip level.
 * Returns false if level_index is out of range or data is invalid. */
bool nt_basisu_get_level_desc(const void *basis_data, uint32_t basis_size, uint32_t level_index, uint32_t *out_width, uint32_t *out_height, uint32_t *out_total_blocks);

/* Transcode a single mip level to the specified GPU format.
 * output: buffer to receive transcoded data (caller allocates)
 * output_blocks: number of blocks that fit in output buffer
 * format: target GPU format
 * Returns true on success. */
bool nt_basisu_transcode_level(const void *basis_data, uint32_t basis_size, uint32_t level_index, void *output, uint32_t output_blocks, nt_basisu_format_t format);

/* Bytes per block for a given format */
uint32_t nt_basisu_bytes_per_block(nt_basisu_format_t format);

/* GL internal format constant for a given basisu format.
 * Returns 0 for RGBA32 (not a compressed format). */
uint32_t nt_basisu_gl_internal_format(nt_basisu_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* NT_BASISU_TRANSCODER_H */
