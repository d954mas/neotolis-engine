#ifndef NT_BASISU_TRANSCODER_H
#define NT_BASISU_TRANSCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "nt_basisu_codec.h"
#include "nt_texture_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Image 0 of a Basis file, as the blob itself describes it. */
typedef struct {
    nt_basisu_codec_t codec; /* ETC1S or UASTC_LDR; any other upstream format makes nt_basisu_info fail */
    uint32_t width;          /* level 0, unpadded */
    uint32_t height;
    uint32_t level_count;
    bool has_alpha;
} nt_basisu_info_t;

/* One-time transcoder init (call at startup before any transcode) */
void nt_basisu_transcoder_global_init(void);

/* Describe image 0 without allocating. Returns false for a bad header, a codec
 * this engine does not decode, or a mip chain whose level L is not exactly
 * max(1, width >> L) x max(1, height >> L). */
bool nt_basisu_info(const void *basis_data, uint32_t basis_size, nt_basisu_info_t *out_info);

/* Begin/end a transcoding session. Call start once, transcode levels, then stop. */
bool nt_basisu_start_transcoding(const void *basis_data, uint32_t basis_size);
void nt_basisu_stop_transcoding(void);

/* Transcode one mip level of image 0 into `output`, which must hold
 * capacity_bytes; short capacity is rejected before anything is written.
 * `format` must be one of the four compressed outputs or RGBA8. */
bool nt_basisu_transcode_level(const void *basis_data, uint32_t basis_size, uint32_t level_index, void *output, uint32_t capacity_bytes, nt_texture_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* NT_BASISU_TRANSCODER_H */
