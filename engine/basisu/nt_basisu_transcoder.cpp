#include "nt_basisu_transcoder.h"

#include "basisu_transcoder.h"

#include "core/nt_assert.h"

/* ---- Static transcoder instance ---- */

static basist::basisu_transcoder s_transcoder;

/* ---- Public API ---- */

void nt_basisu_transcoder_global_init(void) { basist::basisu_transcoder_init(); }

bool nt_basisu_info(const void *basis_data, uint32_t basis_size, nt_basisu_info_t *out_info) {
    NT_ASSERT(out_info != nullptr);
    /* get_basis_tex_format reads the header without validating it and answers
       cETC1S for garbage, so the header check has to come first. */
    if (!s_transcoder.validate_header(basis_data, basis_size)) {
        return false;
    }
    nt_basisu_codec_t codec;
    switch (s_transcoder.get_basis_tex_format(basis_data, basis_size)) {
    case basist::basis_tex_format::cETC1S:
        codec = NT_BASISU_CODEC_ETC1S;
        break;
    case basist::basis_tex_format::cUASTC_LDR_4x4:
        codec = NT_BASISU_CODEC_UASTC_LDR;
        break;
    default:
        return false;
    }

    basist::basisu_image_info image_info;
    if (!s_transcoder.get_image_info(basis_data, basis_size, image_info, 0)) {
        return false;
    }
    out_info->codec = codec;
    out_info->width = image_info.m_orig_width;
    out_info->height = image_info.m_orig_height;
    /* Upstream accepts a level whose width shrank within the same block count and
       then writes it with its own row stride; the activator derives sizes from
       level 0, so every level must be exactly the halved chain. */
    for (uint32_t level = 1; level < image_info.m_total_levels; level++) {
        uint32_t level_w = 0;
        uint32_t level_h = 0;
        uint32_t level_blocks = 0;
        if (!s_transcoder.get_image_level_desc(basis_data, basis_size, 0, level, level_w, level_h, level_blocks)) {
            return false;
        }
        if (level_w != nt_texture_level_extent(image_info.m_orig_width, level) || level_h != nt_texture_level_extent(image_info.m_orig_height, level)) {
            return false;
        }
    }
    out_info->level_count = image_info.m_total_levels;
    out_info->has_alpha = image_info.m_alpha_flag;
    return true;
}

bool nt_basisu_start_transcoding(const void *basis_data, uint32_t basis_size) { return s_transcoder.start_transcoding(basis_data, basis_size); }

void nt_basisu_stop_transcoding(void) { s_transcoder.stop_transcoding(); }

bool nt_basisu_transcode_level(const void *basis_data, uint32_t basis_size, uint32_t level_index, void *output, uint32_t capacity_bytes, nt_texture_format_t format) {
    basist::transcoder_texture_format target;
    uint32_t unit_bytes; /* bytes per 4x4 block, or per pixel for RGBA8 */
    switch (format) {
    case NT_TEXTURE_FORMAT_ETC2_RGB8:
        /* Upstream has no ETC2_RGB target; an ETC1 payload is a legal GL_COMPRESSED_RGB8_ETC2 block. */
        target = basist::transcoder_texture_format::cTFETC1_RGB;
        unit_bytes = 8;
        break;
    case NT_TEXTURE_FORMAT_ETC2_RGBA8:
        target = basist::transcoder_texture_format::cTFETC2_RGBA;
        unit_bytes = 16;
        break;
    case NT_TEXTURE_FORMAT_BC7_RGBA:
        target = basist::transcoder_texture_format::cTFBC7_RGBA;
        unit_bytes = 16;
        break;
    case NT_TEXTURE_FORMAT_ASTC_4x4_RGBA:
        target = basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        unit_bytes = 16;
        break;
    case NT_TEXTURE_FORMAT_RGBA8:
        target = basist::transcoder_texture_format::cTFRGBA32;
        unit_bytes = 4;
        break;
    default:
        NT_ASSERT(0 && "transcode_level: format is not a Basis transcode target");
        return false;
    }

    NT_ASSERT(capacity_bytes != 0 && capacity_bytes % unit_bytes == 0 && "transcode_level: capacity must be whole blocks/pixels");

    /* Upstream counts blocks (pixels for RGBA32) and rejects a short buffer
       before writing anything. */
    return s_transcoder.transcode_image_level(basis_data, basis_size, 0, level_index, output, capacity_bytes / unit_bytes, target);
}
