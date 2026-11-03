#include "nt_basisu_encoder.h"

#include "basisu_comp.h"
#include "nt_build_assert.h"

#include <cstdlib>
#include <cstring>

extern "C" {

void nt_basisu_encoder_init(void) { basisu::basisu_encoder_init(); }

nt_basisu_encode_result_t nt_basisu_encode(uint32_t basis_threads, const uint8_t *const *levels, uint32_t level_count, uint32_t width, uint32_t height, nt_texture_pixel_format_t format,
                                           const nt_basisu_encode_opts_t *opts, bool perceptual) {
    NT_BUILD_ASSERT(basis_threads >= 1 && "basisu encode: basis_threads counts the caller");
    NT_BUILD_ASSERT(level_count >= 1 && level_count <= nt_texture_full_chain_levels(width, height) && "basisu encode: level_count outside 1..full chain");
    const bool has_alpha = nt_texture_pixel_format_has_alpha(format);

    /* Per-call local job_pool — thread-safe, no shared state.
     * basis job_pool(N) = N total threads including caller (1 = caller only, no extra threads). */
    basisu::job_pool local_pool(basis_threads);

    basisu::basis_compressor_params params;
    params.m_pJob_pool = &local_pool;
    params.m_multithreading = (basis_threads > 1);

    params.m_source_images.push_back(basisu::image(levels[0], width, height, 4));
    if (level_count > 1) {
        params.m_source_mipmap_images.resize(1);
        for (uint32_t level = 1; level < level_count; level++) {
            params.m_source_mipmap_images[0].push_back(basisu::image(levels[level], nt_texture_level_extent(width, level), nt_texture_level_extent(height, level), 4));
        }
    }

    switch (opts->codec) {
    case NT_BASISU_CODEC_UASTC_LDR:
        params.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
        params.m_pack_uastc_ldr_4x4_flags = opts->uastc.pack_level;
        params.m_rdo_uastc_ldr_4x4 = opts->uastc.rdo_lambda > 0.0F;
        if (opts->uastc.rdo_lambda > 0.0F) {
            params.m_rdo_uastc_ldr_4x4_quality_scalar = opts->uastc.rdo_lambda;
            params.m_rdo_uastc_ldr_4x4_dict_size = 32768;
        }
        break;
    case NT_BASISU_CODEC_ETC1S:
        params.set_format_mode(basist::basis_tex_format::cETC1S);
        params.m_quality_level = static_cast<int>(opts->etc1s.quality);
        params.m_endpoint_rdo_thresh = opts->etc1s.endpoint_rdo_threshold;
        params.m_selector_rdo_thresh = opts->etc1s.selector_rdo_threshold;
        params.m_no_endpoint_rdo = opts->etc1s.endpoint_rdo_threshold == 0.0F;
        params.m_no_selector_rdo = opts->etc1s.selector_rdo_threshold == 0.0F;
        break;
    default:
        NT_BUILD_ASSERT(0 && "basisu encode: codec is not a Basis codec");
    }

    params.m_create_ktx2_file = false;
    params.m_perceptual = perceptual;
    /* User levels are scanned for alpha even with the check off, hence opaque levels. */
    params.m_check_for_alpha = has_alpha;
    if (!has_alpha) {
        params.m_force_alpha = false;
    }
    params.m_status_output = false;

    basisu::basis_compressor compressor;
    const bool init_ok = compressor.init(params);
    NT_BUILD_ASSERT(init_ok && "basisu encode: compressor init failed");
    const basisu::basis_compressor::error_code ec = compressor.process();
    NT_BUILD_ASSERT(ec == basisu::basis_compressor::cECSuccess && "basisu encode: compressor process failed");

    const basisu::uint8_vec &output = compressor.get_output_basis_file();
    NT_BUILD_ASSERT(!output.empty() && "basisu encode: empty output");

    nt_basisu_encode_result_t result = {};
    result.data = static_cast<uint8_t *>(malloc(output.size()));
    NT_BUILD_ASSERT(result.data && "basisu encode: malloc failed");
    memcpy(result.data, output.data(), output.size());
    result.size = static_cast<uint32_t>(output.size());

    basist::basisu_transcoder local_transcoder;
    const bool header_ok = local_transcoder.validate_header(result.data, result.size);
    NT_BUILD_ASSERT(header_ok && "basisu encode: output header invalid");
    NT_BUILD_ASSERT(local_transcoder.get_total_image_levels(result.data, result.size, 0) == level_count && "basisu encode: output level count differs from the input");
    return result;
}

void nt_basisu_encode_free(nt_basisu_encode_result_t *result) {
    if (result && result->data) {
        free(result->data);
        result->data = nullptr;
        result->size = 0;
    }
}

} // extern "C"
