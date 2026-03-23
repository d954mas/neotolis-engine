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
        /* Reserve 1 core for OS/user — standard practice for offline tools */
        if (threads > 1) {
            threads--;
        }
        s_job_pool = new basisu::job_pool(threads);
    }
}

nt_basisu_encode_result_t nt_basisu_encode(const uint8_t *rgba_pixels, uint32_t width, uint32_t height, bool has_alpha,
                                           bool uastc, uint32_t quality, float rdo_quality, bool gen_mipmaps) {
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
    } else {
        params.set_format_mode(basist::basis_tex_format::cETC1S);
        params.m_quality_level = static_cast<int>(quality); // 1-255
        if (rdo_quality > 0.0f) {
            params.m_endpoint_rdo_thresh = rdo_quality;
            params.m_selector_rdo_thresh = rdo_quality;
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

    // Get mip count from the basis file info
    basist::basisu_transcoder_init();
    basist::basisu_transcoder transcoder;
    if (transcoder.validate_header(result.data, result.size)) {
        basist::basisu_image_info image_info;
        if (transcoder.get_image_info(result.data, result.size, image_info, 0)) {
            result.mip_count = image_info.m_total_levels;
        } else {
            result.mip_count = 1;
        }
    } else {
        result.mip_count = 1;
    }

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
