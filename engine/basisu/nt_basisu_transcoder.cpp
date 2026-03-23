#include "nt_basisu_transcoder.h"

#include "basisu_transcoder.h"

/* ---- Format mapping ---- */

static basist::transcoder_texture_format to_basist_format(nt_basisu_format_t fmt) {
    switch (fmt) {
    case NT_BASISU_FORMAT_BC7_RGBA:
        return basist::transcoder_texture_format::cTFBC7_RGBA;
    case NT_BASISU_FORMAT_ASTC_4x4_RGBA:
        return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
    case NT_BASISU_FORMAT_ETC2_RGBA:
        return basist::transcoder_texture_format::cTFETC2_RGBA;
    case NT_BASISU_FORMAT_ETC1_RGB:
        return basist::transcoder_texture_format::cTFETC1_RGB;
    case NT_BASISU_FORMAT_RGBA32:
        return basist::transcoder_texture_format::cTFRGBA32;
    default:
        return basist::transcoder_texture_format::cTFRGBA32;
    }
}

/* ---- Static transcoder instance ---- */

static basist::basisu_transcoder s_transcoder;

/* ---- Public API ---- */

void nt_basisu_transcoder_global_init(void) { basist::basisu_transcoder_init(); }

bool nt_basisu_validate_header(const void *basis_data, uint32_t basis_size) {
    return s_transcoder.validate_header(basis_data, basis_size);
}

uint32_t nt_basisu_get_level_count(const void *basis_data, uint32_t basis_size) {
    return s_transcoder.get_total_image_levels(basis_data, basis_size, 0);
}

bool nt_basisu_get_level_desc(const void *basis_data, uint32_t basis_size, uint32_t level_index,
                              uint32_t *out_width, uint32_t *out_height, uint32_t *out_total_blocks) {
    return s_transcoder.get_image_level_desc(basis_data, basis_size, 0, level_index, *out_width, *out_height,
                                             *out_total_blocks);
}

bool nt_basisu_transcode_level(const void *basis_data, uint32_t basis_size, uint32_t level_index, void *output,
                               uint32_t output_blocks, nt_basisu_format_t format) {
    if (!s_transcoder.start_transcoding(basis_data, basis_size)) {
        return false;
    }

    bool ok = s_transcoder.transcode_image_level(basis_data, basis_size, 0, level_index, output, output_blocks,
                                                 to_basist_format(format));

    s_transcoder.stop_transcoding();
    return ok;
}

uint32_t nt_basisu_bytes_per_block(nt_basisu_format_t format) {
    switch (format) {
    case NT_BASISU_FORMAT_BC7_RGBA:
        return 16;
    case NT_BASISU_FORMAT_ASTC_4x4_RGBA:
        return 16;
    case NT_BASISU_FORMAT_ETC2_RGBA:
        return 16;
    case NT_BASISU_FORMAT_ETC1_RGB:
        return 8;
    case NT_BASISU_FORMAT_RGBA32:
        return 4; /* per pixel, not per block */
    default:
        return 4;
    }
}

uint32_t nt_basisu_gl_internal_format(nt_basisu_format_t format) {
    switch (format) {
    case NT_BASISU_FORMAT_BC7_RGBA:
        return 0x8E8C; /* GL_COMPRESSED_RGBA_BPTC_UNORM */
    case NT_BASISU_FORMAT_ASTC_4x4_RGBA:
        return 0x93B0; /* GL_COMPRESSED_RGBA_ASTC_4x4_KHR */
    case NT_BASISU_FORMAT_ETC2_RGBA:
        return 0x9278; /* GL_COMPRESSED_RGBA8_ETC2_EAC */
    case NT_BASISU_FORMAT_ETC1_RGB:
        return 0x9274; /* GL_COMPRESSED_RGB8_ETC2 */
    case NT_BASISU_FORMAT_RGBA32:
        return 0; /* not a compressed format */
    default:
        return 0;
    }
}
