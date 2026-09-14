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
 * outside this build's NT_BASISU_CODECS (NT_BASISU_HAS_ETC1S/UASTC), or a mip
 * chain whose level L is not exactly max(1, width >> L) x max(1, height >> L). */
bool nt_basisu_info(const void *basis_data, uint32_t basis_size, nt_basisu_info_t *out_info);

/* Transcode levels 0..N-1 of image 0 into `output` back to back, no padding
 * (level L is max(1, w >> L) x max(1, h >> L)), N and dims from `info`, which
 * must come from nt_basisu_info of this build. Returns false for a codec
 * outside NT_BASISU_CODECS or a compressed format outside NT_BASISU_TARGETS
 * (RGBA8 is always available); a format that is no Basis target asserts. A
 * capacity short of the whole chain is rejected before anything is written. */
bool nt_basisu_transcode_chain(const void *basis_data, uint32_t basis_size, const nt_basisu_info_t *info, nt_texture_format_t format, void *output, uint32_t capacity_bytes);

#ifdef __cplusplus
}
#endif

#endif /* NT_BASISU_TRANSCODER_H */
