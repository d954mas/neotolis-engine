#include "nt_basisu_transcoder.h"

#include "basisu_transcoder.h"

#include "core/nt_assert.h"

/* ---- Build-set cross-check ---- */

/* engine/basisu/CMakeLists.txt sets every BASISD_SUPPORT_* checked here. Trimmed
   (WASM, test mirror): exactly the decoders of NT_BASISU_CODECS plus the ETC1S->X
   tables of NT_BASISU_TARGETS. Native: everything stays on for the shared encoder TU. */
#if NT_BASISU_PROFILE_TRIMMED
static_assert(BASISD_SUPPORT_ETC1S == NT_BASISU_HAS_ETC1S, "trimmed transcoder: BASISD_SUPPORT_ETC1S must follow NT_BASISU_CODECS");
static_assert(BASISD_SUPPORT_UASTC == NT_BASISU_HAS_UASTC, "trimmed transcoder: BASISD_SUPPORT_UASTC must follow NT_BASISU_CODECS");
static_assert(BASISD_SUPPORT_BC7 == (NT_BASISU_HAS_BC7 && NT_BASISU_HAS_ETC1S), "trimmed transcoder: BASISD_SUPPORT_BC7 must follow NT_BASISU_TARGETS && ETC1S");
static_assert(BASISD_SUPPORT_ASTC == (NT_BASISU_HAS_ASTC && NT_BASISU_HAS_ETC1S), "trimmed transcoder: BASISD_SUPPORT_ASTC must follow NT_BASISU_TARGETS && ETC1S");
static_assert(BASISD_SUPPORT_ETC2_EAC_A8 == (NT_BASISU_HAS_ETC2 && NT_BASISU_HAS_ETC1S), "trimmed transcoder: BASISD_SUPPORT_ETC2_EAC_A8 must follow NT_BASISU_TARGETS && ETC1S");
#else
static_assert(BASISD_SUPPORT_ETC1S == 1 && BASISD_SUPPORT_UASTC == 1 && BASISD_SUPPORT_BC7 == 1 && BASISD_SUPPORT_ASTC == 1 && BASISD_SUPPORT_ETC2_EAC_A8 == 1,
              "native transcoder: both decoders and every engine target stay compiled for the shared encoder TU");
#endif

/* ---- Static transcoder instance ---- */

static basist::basisu_transcoder s_transcoder;

static bool codec_enabled(nt_basisu_codec_t codec) {
    switch (codec) {
    case NT_BASISU_CODEC_ETC1S:
        return NT_BASISU_HAS_ETC1S != 0;
    case NT_BASISU_CODEC_UASTC_LDR:
        return NT_BASISU_HAS_UASTC != 0;
    default:
        return false;
    }
}

/* ---- Public API ---- */

void nt_basisu_transcoder_global_init(void) { basist::basisu_transcoder_init(); }

bool nt_basisu_info(const void *basis_data, uint32_t basis_size, nt_basisu_info_t *out_info) {
    NT_ASSERT(out_info != nullptr);
    /* get_basis_tex_format answers cETC1S for a header it rejects, indistinguishable
       from a real ETC1S file, so the full header check has to come first. */
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
    /* Not in NT_BASISU_CODECS: the trimmed profile has no decoder for it, the
       native superset refuses it too so both answer alike. */
    if (!codec_enabled(codec)) {
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

bool nt_basisu_transcode_chain(const void *basis_data, uint32_t basis_size, const nt_basisu_info_t *info, nt_texture_format_t format, void *output, uint32_t capacity_bytes) {
    NT_ASSERT(info != nullptr);
    /* A caller-built info for a codec outside NT_BASISU_CODECS would otherwise
       reach the native superset decoder; the web build has no such path. */
    if (!codec_enabled(info->codec)) {
        return false;
    }
    basist::transcoder_texture_format target;
    uint32_t unit_bytes; /* bytes per 4x4 block, or per pixel for RGBA8 */
    bool target_enabled; /* NT_BASISU_TARGETS; RGBA8 is always available */
    switch (format) {
    case NT_TEXTURE_FORMAT_ETC2_RGB8:
        /* Upstream has no ETC2_RGB target; an ETC1 payload is a legal GL_COMPRESSED_RGB8_ETC2 block. */
        target = basist::transcoder_texture_format::cTFETC1_RGB;
        unit_bytes = 8;
        target_enabled = NT_BASISU_HAS_ETC2 != 0;
        break;
    case NT_TEXTURE_FORMAT_ETC2_RGBA8:
        target = basist::transcoder_texture_format::cTFETC2_RGBA;
        unit_bytes = 16;
        target_enabled = NT_BASISU_HAS_ETC2 != 0;
        break;
    case NT_TEXTURE_FORMAT_BC7_RGBA:
        target = basist::transcoder_texture_format::cTFBC7_RGBA;
        unit_bytes = 16;
        target_enabled = NT_BASISU_HAS_BC7 != 0;
        break;
    case NT_TEXTURE_FORMAT_ASTC_4x4_RGBA:
        target = basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        unit_bytes = 16;
        target_enabled = NT_BASISU_HAS_ASTC != 0;
        break;
    case NT_TEXTURE_FORMAT_RGBA8:
        target = basist::transcoder_texture_format::cTFRGBA32;
        unit_bytes = 4;
        target_enabled = true;
        break;
    default:
        NT_ASSERT(0 && "transcode_chain: format is not a Basis transcode target");
        return false;
    }
    if (!target_enabled) {
        return false;
    }

    /* The short-buffer contract is recoverable and covers the whole chain, so
       the total has to be known before the first level is written. */
    uint64_t chain_bytes = 0;
    for (uint32_t level = 0; level < info->level_count; level++) {
        chain_bytes += nt_texture_level_bytes(format, nt_texture_level_extent(info->width, level), nt_texture_level_extent(info->height, level));
    }
    if (chain_bytes > capacity_bytes) {
        return false;
    }

    if (!s_transcoder.start_transcoding(basis_data, basis_size)) {
        return false;
    }
    bool ok = true;
    uint32_t offset = 0;
    for (uint32_t level = 0; level < info->level_count && ok; level++) {
        const uint32_t level_bytes = (uint32_t)nt_texture_level_bytes(format, nt_texture_level_extent(info->width, level), nt_texture_level_extent(info->height, level));
        /* Upstream counts blocks (pixels for RGBA32). */
        ok = s_transcoder.transcode_image_level(basis_data, basis_size, 0, level, (uint8_t *)output + offset, level_bytes / unit_bytes, target);
        offset += level_bytes;
    }
    s_transcoder.stop_transcoding();
    return ok;
}
