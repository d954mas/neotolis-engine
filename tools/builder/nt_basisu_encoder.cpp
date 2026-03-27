#include "nt_basisu_encoder.h"

// Basis Universal encoder headers
#include "basisu_comp.h"

#include <cstdlib>
#include <cstring>
#include <thread>

static basisu::job_pool *s_job_pool = nullptr;

extern "C" {

void nt_basisu_encoder_init(void) {
    basisu::basisu_encoder_init();
    if (!s_job_pool) {
        uint32_t threads = std::thread::hardware_concurrency();
        /* Default: reserve 1 core for interactive use.
         * NT_BUILDER_ALL_CORES=1: use all cores (CI, dedicated build machines). */
        if (threads > 1 && !std::getenv("NT_BUILDER_ALL_CORES")) { // NOLINT(concurrency-mt-unsafe)
            threads--;
        }
        if (threads < 1) {
            threads = 1;
        }
        s_job_pool = new basisu::job_pool(threads);
    }
}

nt_basisu_encode_result_t nt_basisu_encode(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, bool has_alpha, bool uastc, uint32_t quality, float endpoint_rdo, float selector_rdo,
                                           bool gen_mipmaps) {
    nt_basisu_encode_result_t result = {};

    basisu::basis_compressor_params params;
    params.m_pJob_pool = s_job_pool;

    // Create source image from RGBA pixels (constructor: pImage, width, height, comps)
    basisu::image src_image(rgba_pixels, width, height, 4);
    params.m_source_images.push_back(src_image);

    // Configure format mode
    if (uastc) {
        params.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
        params.m_pack_uastc_ldr_4x4_flags = quality; // 0-4 pack level
        if (endpoint_rdo > 0.0f) {
            params.m_rdo_uastc_ldr_4x4 = true;
            params.m_rdo_uastc_ldr_4x4_quality_scalar = endpoint_rdo;
            params.m_rdo_uastc_ldr_4x4_dict_size = 32768;
        }
    } else {
        params.set_format_mode(basist::basis_tex_format::cETC1S);
        params.m_quality_level = static_cast<int>(quality); // 1-255
        if (endpoint_rdo > 0.0f) {
            params.m_endpoint_rdo_thresh = endpoint_rdo;
        }
        if (selector_rdo > 0.0f) {
            params.m_selector_rdo_thresh = selector_rdo;
        }
    }

    params.m_create_ktx2_file = false;
    params.m_mip_gen = gen_mipmaps;
    params.m_check_for_alpha = has_alpha;
    if (!has_alpha) {
        params.m_force_alpha = false;
    }

    // Silence encoder status output
    params.m_status_output = false;

    basisu::basis_compressor compressor;
    if (!compressor.init(params)) {
        return result; // data=NULL signals failure
    }

    basisu::basis_compressor::error_code ec = compressor.process();
    if (ec != basisu::basis_compressor::cECSuccess) {
        return result;
    }

    const basisu::uint8_vec &output = compressor.get_output_basis_file();
    if (output.empty()) {
        return result;
    }

    // Deep copy output (compressor owns the data)
    result.data = static_cast<uint8_t *>(malloc(output.size()));
    if (!result.data) {
        return result;
    }
    memcpy(result.data, output.data(), output.size());
    result.size = static_cast<uint32_t>(output.size());

    // Get mip count from encoded .basis file via transcoder (authoritative source)
    static basist::basisu_transcoder transcoder;
    if (transcoder.validate_header(result.data, result.size)) {
        result.mip_count = transcoder.get_total_image_levels(result.data, result.size, 0);
    } else {
        result.mip_count = 1;
    }

    return result;
}

void nt_basisu_encoder_shutdown(void) {
    delete s_job_pool;
    s_job_pool = nullptr;
}

void nt_basisu_encode_free(nt_basisu_encode_result_t *result) {
    if (result && result->data) {
        free(result->data);
        result->data = nullptr;
        result->size = 0;
    }
}

} // extern "C"
