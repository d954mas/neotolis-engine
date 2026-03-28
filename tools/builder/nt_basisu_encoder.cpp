#include "nt_basisu_encoder.h"

#include "basisu_comp.h"

#include <cstdlib>
#include <cstring>

extern "C" {

void nt_basisu_encoder_init(void) { basisu::basisu_encoder_init(); }

nt_basisu_encode_result_t nt_basisu_encode(uint32_t basis_threads, const uint8_t *rgba_pixels, uint32_t width, uint32_t height, bool has_alpha, bool uastc, uint32_t quality, float endpoint_rdo,
                                           float selector_rdo, bool gen_mipmaps) {
    nt_basisu_encode_result_t result = {};
    if (basis_threads < 1) {
        basis_threads = 1;
    }

    /* Per-call local job_pool — thread-safe, no shared state.
     * basis job_pool(N) = N total threads including caller (1 = caller only, no extra threads). */
    basisu::job_pool local_pool(basis_threads);

    basisu::basis_compressor_params params;
    params.m_pJob_pool = &local_pool;
    params.m_multithreading = (basis_threads > 1);

    basisu::image src_image(rgba_pixels, width, height, 4);
    params.m_source_images.push_back(src_image);

    if (uastc) {
        params.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
        params.m_pack_uastc_ldr_4x4_flags = quality;
        if (endpoint_rdo > 0.0F) {
            params.m_rdo_uastc_ldr_4x4 = true;
            params.m_rdo_uastc_ldr_4x4_quality_scalar = endpoint_rdo;
            params.m_rdo_uastc_ldr_4x4_dict_size = 32768;
        }
    } else {
        params.set_format_mode(basist::basis_tex_format::cETC1S);
        params.m_quality_level = static_cast<int>(quality);
        if (endpoint_rdo > 0.0F) {
            params.m_endpoint_rdo_thresh = endpoint_rdo;
        }
        if (selector_rdo > 0.0F) {
            params.m_selector_rdo_thresh = selector_rdo;
        }
    }

    params.m_create_ktx2_file = false;
    params.m_mip_gen = gen_mipmaps;
    params.m_check_for_alpha = has_alpha;
    if (!has_alpha) {
        params.m_force_alpha = false;
    }
    params.m_status_output = false;

    basisu::basis_compressor compressor;
    if (!compressor.init(params)) {
        return result;
    }

    const basisu::basis_compressor::error_code ec = compressor.process();
    if (ec != basisu::basis_compressor::cECSuccess) {
        return result;
    }

    const basisu::uint8_vec &output = compressor.get_output_basis_file();
    if (output.empty()) {
        return result;
    }

    result.data = static_cast<uint8_t *>(malloc(output.size()));
    if (!result.data) {
        return result;
    }
    memcpy(result.data, output.data(), output.size());
    result.size = static_cast<uint32_t>(output.size());

    basist::basisu_transcoder local_transcoder;
    if (local_transcoder.validate_header(result.data, result.size)) {
        result.mip_count = local_transcoder.get_total_image_levels(result.data, result.size, 0);
    } else {
        result.mip_count = 1;
    }

    return result;
}

void nt_basisu_encoder_shutdown(void) { /* no global state to clean up */ }

void nt_basisu_encode_free(nt_basisu_encode_result_t *result) {
    if (result && result->data) {
        free(result->data);
        result->data = nullptr;
        result->size = 0;
    }
}

} // extern "C"
