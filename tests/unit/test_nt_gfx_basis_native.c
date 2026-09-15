/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "basisu/nt_basisu_transcoder.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "nt_basisu_encoder.h"
#include "nt_builder.h"
#include "nt_texture_format.h"
#include "unity.h"
#include "window/nt_window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#define SRC_W 96U
#define SRC_H 64U
#define SRC_LEVELS 7U /* 96x64 -> 48x32 -> 24x16 -> 12x8 -> 6x4 -> 3x2 -> 1x1 */

/* Tolerances vs the transcoder's own RGBA32 decode of the same level, in 0..255
 * steps: UASTC round-trips a ramp tightly, ETC1S quantizes to a 4-colour block
 * palette. Loose for the codec, tight enough that a channel swap, a flip or a wrong level fails. */
#define TOL_UASTC 24
#define TOL_ETC1S 44
#define TOL_EXACT 2

static uint8_t s_src[SRC_W * SRC_H * 4];
static uint8_t s_blob[sizeof(NtTextureAssetHeader) + ((size_t)SRC_W * SRC_H * 4)];
static uint8_t s_chain[SRC_W * SRC_H * 8];
static uint8_t s_reference[SRC_W * SRC_H * 8]; /* the whole RGBA8 chain */
static const uint8_t *s_reference_level;
static uint8_t s_readback[SRC_W * SRC_H * 4];
static uint32_t s_blob_size;
static nt_basisu_info_t s_info;

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = 128,
        .height = 128,
    };
    nt_window_init();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 16, .max_programs = 8, .max_pipelines = 8, .max_buffers = 8, .max_textures = 16, .max_meshes = 4, .max_vertex_inputs = 8, .max_render_targets = 4});
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_window_shutdown();
}

// #region fixture

/* RGB ramps over x, alpha over y: an orientation error cannot pass. */
static void fill_source(void) {
    for (uint32_t y = 0; y < SRC_H; y++) {
        for (uint32_t x = 0; x < SRC_W; x++) {
            uint8_t *px = &s_src[(((size_t)y * SRC_W) + x) * 4];
            px[0] = (uint8_t)(32U + (x * 160U / SRC_W));
            px[1] = (uint8_t)(200U - (x * 150U / SRC_W));
            px[2] = 64;
            px[3] = (uint8_t)(40U + (y * 200U / SRC_H));
        }
    }
}

/* A TTEX V3 asset with the builder's own field values, so the activator sees
 * production bytes. */
static void build_fixture(nt_basisu_codec_t codec) {
    fill_source();
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, s_src, SRC_W, SRC_H, true, &opts);
    TEST_ASSERT_NOT_NULL(enc.data);
    TEST_ASSERT_EQUAL_UINT32(SRC_LEVELS, enc.mip_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(s_blob) - sizeof(NtTextureAssetHeader), enc.size);

    NtTextureAssetHeader *hdr = (NtTextureAssetHeader *)s_blob;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = NT_TEXTURE_MAGIC;
    hdr->version = NT_TEXTURE_VERSION;
    hdr->format = NT_TEXTURE_FORMAT_RGBA8;
    hdr->width = SRC_W;
    hdr->height = SRC_H;
    hdr->mip_count = (uint16_t)enc.mip_count;
    hdr->compression = NT_TEXTURE_COMPRESSION_BASIS;
    hdr->default_min_filter = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR;
    hdr->default_mag_filter = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    hdr->default_wrap_u = NT_TEXTURE_DEFAULT_WRAP_REPEAT;
    hdr->default_wrap_v = NT_TEXTURE_DEFAULT_WRAP_REPEAT;
    hdr->data_size = enc.size;
    memcpy(s_blob + sizeof(*hdr), enc.data, enc.size);
    s_blob_size = (uint32_t)sizeof(*hdr) + enc.size;
    uint32_t payload_size = enc.size;
    nt_basisu_encode_free(&enc);

    TEST_ASSERT_TRUE(nt_basisu_info(s_blob + sizeof(*hdr), payload_size, &s_info));
    TEST_ASSERT_EQUAL_UINT32(SRC_LEVELS, s_info.level_count);
}

static const uint8_t *fixture_payload(void) { return s_blob + sizeof(NtTextureAssetHeader); }
static uint32_t fixture_payload_size(void) { return s_blob_size - (uint32_t)sizeof(NtTextureAssetHeader); }

static uint16_t level_dim(uint32_t base, uint32_t level) {
    uint32_t value = base >> level;
    return (uint16_t)(value > 0 ? value : 1);
}

/* Bytes of the whole chain in `format`, levels back to back with no padding. */
static uint32_t chain_bytes(nt_texture_format_t format) {
    uint32_t total = 0;
    for (uint32_t level = 0; level < s_info.level_count; level++) {
        total += (uint32_t)nt_texture_level_bytes(format, level_dim(SRC_W, level), level_dim(SRC_H, level));
    }
    return total;
}

/* What the GPU should be showing: the transcoder's own RGBA32 decode of the
 * level under test, so the comparison isolates upload and sampling. */
static void decode_reference(uint32_t level) {
    TEST_ASSERT_TRUE(nt_basisu_transcode_chain(fixture_payload(), fixture_payload_size(), &s_info, NT_TEXTURE_FORMAT_RGBA8, s_reference, (uint32_t)sizeof(s_reference)));
    uint32_t offset = 0;
    for (uint32_t below = 0; below < level; below++) {
        offset += (uint32_t)nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA8, level_dim(SRC_W, below), level_dim(SRC_H, below));
    }
    s_reference_level = s_reference + offset;
}

/* Transcodes the whole chain into one contiguous KTX/DDS-style buffer. */
static uint32_t transcode_chain(nt_texture_format_t format) {
    const uint32_t total = chain_bytes(format);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(s_chain), total);
    TEST_ASSERT_TRUE_MESSAGE(nt_basisu_transcode_chain(fixture_payload(), fixture_payload_size(), &s_info, format, s_chain, total), "transcode failed for a caps-supported format");
    return total;
}

// #endregion

// #region draw and readback

static const char *s_fullscreen_vs = "out vec2 v_uv;\n"
                                     "void main() {\n"
                                     "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                     "    v_uv = p;\n"
                                     "    gl_Position = vec4((p * 2.0) - 1.0, 0.0, 1.0);\n"
                                     "}\n";
static const char *s_sample_fs = "precision highp float;\n"
                                 "uniform sampler2D u_tex;\n"
                                 "in vec2 v_uv;\n"
                                 "out vec4 frag_color;\n"
                                 "void main() { frag_color = texture(u_tex, v_uv); }\n";

/* Draws the texture over `vp_w x vp_h` of a `rt_w x rt_h` colour target and
 * reads the viewport back. Straight alpha survives: no blending is enabled. */
static void render_sampled(nt_texture_t tex, nt_sampler_t sampler, uint16_t rt_w, uint16_t rt_h, int vp_w, int vp_h) {
    nt_render_target_t rt = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = rt_w,
        .height = rt_h,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_NONE,
    });
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_fullscreen_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_sample_fs});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});

    memset(s_readback, 0, sizeof(s_readback));
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_color = {0.0F, 0.0F, 0.0F, 0.0F}});
    nt_gfx_set_viewport(0, 0, vp_w, vp_h);
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = tex, .sampler = sampler};
    nt_gfx_apply_texture_bindings(&binding, 1);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, vp_w, vp_h, s_readback, (uint32_t)sizeof(s_readback)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(rt);
    nt_gfx_destroy_vertex_input(vi);
    nt_gfx_destroy_pipeline(pipeline);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* read_pixels hands back top-left rows; GL row 0 is the source's row 0. */
static void check_against_reference(uint32_t level, uint8_t tolerance, bool opaque_storage) {
    uint32_t w = level_dim(SRC_W, level);
    uint32_t h = level_dim(SRC_H, level);
    for (uint32_t ry = 0; ry < h; ry++) {
        for (uint32_t rx = 0; rx < w; rx++) {
            const uint8_t *got = &s_readback[(((size_t)ry * w) + rx) * 4];
            const uint8_t *want = &s_reference_level[((((size_t)h - 1U - ry) * w) + rx) * 4];
            TEST_ASSERT_UINT8_WITHIN(tolerance, want[0], got[0]);
            TEST_ASSERT_UINT8_WITHIN(tolerance, want[1], got[1]);
            TEST_ASSERT_UINT8_WITHIN(tolerance, want[2], got[2]);
            TEST_ASSERT_UINT8_WITHIN(tolerance, opaque_storage ? 255 : want[3], got[3]);
        }
    }
}

/* The selector's contract: the first of BC7 -> ASTC -> ETC2 the GPU reports
 * and NT_BASISU_HAS_* admits, RGBA8 otherwise. */
static nt_texture_format_t expected_target(void) {
    const nt_gfx_gpu_caps_t *caps = nt_gfx_gpu_caps();
    if (NT_BASISU_HAS_BC7 && caps->has_bc7) {
        return NT_TEXTURE_FORMAT_BC7_RGBA;
    }
    if (NT_BASISU_HAS_ASTC && caps->has_astc) {
        return NT_TEXTURE_FORMAT_ASTC_4x4_RGBA;
    }
    if (NT_BASISU_HAS_ETC2 && caps->has_etc2) {
        return NT_TEXTURE_FORMAT_ETC2_RGBA8;
    }
    return NT_TEXTURE_FORMAT_RGBA8;
}

/* An admitted codec for the tests that are not about the codec. */
#define ANY_CODEC (NT_BASISU_HAS_UASTC ? NT_BASISU_CODEC_UASTC_LDR : NT_BASISU_CODEC_ETC1S)

static GLint texture_max_level(nt_texture_t tex) {
    nt_gfx_backend_bind_texture(nt_gfx_test_texture_backend_id(tex), 0);
    GLint value = -1;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &value);
    return value;
}

// #endregion

// #region (a) activation on the detected caps

static void check_activation(nt_basisu_codec_t codec, uint8_t tolerance) {
    build_fixture(codec);
    uint32_t handle = nt_gfx_activate_texture(s_blob, s_blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_texture_t tex = {.id = handle};
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_INT(expected_target(), nt_gfx_texture_format(tex));
    TEST_ASSERT_EQUAL_INT((int)s_info.level_count - 1, texture_max_level(tex));

    /* Level 0 through the asset's own LINEAR_MIPMAP_LINEAR default sampler. */
    decode_reference(0);
    render_sampled(tex, NT_SAMPLER_DEFAULT, SRC_W, SRC_H, SRC_W, SRC_H);
    check_against_reference(0, tolerance, false);

    /* One pixel of viewport forces the LOD past the end of the chain, so
     * NEAREST_MIPMAP_NEAREST clamps to MAX_LEVEL: the 1x1 tail. */
    nt_sampler_t tail = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_NEAREST_MIPMAP_NEAREST, .mag_filter = NT_FILTER_NEAREST});
    decode_reference(s_info.level_count - 1U);
    render_sampled(tex, tail, SRC_W, SRC_H, 1, 1);
    check_against_reference(s_info.level_count - 1U, tolerance, false);

    nt_gfx_deactivate_texture(handle);
}

#if NT_BASISU_HAS_ETC1S
void test_activate_etc1s_blob_uploads_and_samples(void) { check_activation(NT_BASISU_CODEC_ETC1S, TOL_ETC1S); }
#endif
#if NT_BASISU_HAS_UASTC
void test_activate_uastc_blob_uploads_and_samples(void) { check_activation(NT_BASISU_CODEC_UASTC_LDR, TOL_UASTC); }
#endif

// #endregion

// #region (b) prepared upload of a contiguous chain

static void check_prepared_upload(nt_texture_format_t format, uint8_t tolerance) {
    build_fixture(ANY_CODEC);
    uint32_t total = transcode_chain(format);
    TEST_ASSERT_GREATER_THAN_UINT32(0, total);

    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = SRC_W,
        .height = SRC_H,
        .data = s_chain,
        .format = format,
        .min_filter = NT_FILTER_NEAREST_MIPMAP_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .level_count = (uint8_t)s_info.level_count,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_INT(format, nt_gfx_texture_format(tex));
    TEST_ASSERT_EQUAL_INT((int)s_info.level_count - 1, texture_max_level(tex));

    decode_reference(0);
    render_sampled(tex, NT_SAMPLER_DEFAULT, SRC_W, SRC_H, SRC_W, SRC_H);
    check_against_reference(0, tolerance, format == NT_TEXTURE_FORMAT_ETC2_RGB8);

    /* The declared chain really reaches GL: the tail level is sampled too. */
    decode_reference(s_info.level_count - 1U);
    render_sampled(tex, NT_SAMPLER_DEFAULT, SRC_W, SRC_H, 1, 1);
    check_against_reference(s_info.level_count - 1U, tolerance, format == NT_TEXTURE_FORMAT_ETC2_RGB8);

    nt_gfx_destroy_texture(tex);
}

void test_prepared_upload_rgba8(void) { check_prepared_upload(NT_TEXTURE_FORMAT_RGBA8, TOL_EXACT); }

void test_prepared_upload_bc7(void) {
    if (!nt_gfx_gpu_caps()->has_bc7) {
        TEST_IGNORE_MESSAGE("BC7_RGBA unverified on this host -- needs a GPU with has_bc7");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_BC7_RGBA, TOL_UASTC);
}

void test_prepared_upload_astc(void) {
    if (!nt_gfx_gpu_caps()->has_astc) {
        TEST_IGNORE_MESSAGE("ASTC_4x4_RGBA unverified on this host -- needs a GPU with has_astc");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_ASTC_4x4_RGBA, TOL_UASTC);
}

void test_prepared_upload_etc2_rgba8(void) {
    if (!nt_gfx_gpu_caps()->has_etc2) {
        TEST_IGNORE_MESSAGE("ETC2_RGBA8 unverified on this host -- needs a GPU with has_etc2");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_ETC2_RGBA8, TOL_ETC1S);
}

void test_prepared_upload_etc2_rgb8(void) {
    if (!nt_gfx_gpu_caps()->has_etc2) {
        TEST_IGNORE_MESSAGE("ETC2_RGB8 unverified on this host -- needs a GPU with has_etc2");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_ETC2_RGB8, TOL_ETC1S);
}

// #endregion

// #region (c) GL_TEXTURE_MAX_LEVEL

void test_single_level_texture_caps_max_level_and_still_samples(void) {
    static const uint8_t texel[4] = {200, 100, 50, 180};
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = texel,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_INT(0, texture_max_level(tex));

    /* A mip filter over a one-level texture is complete GL, not a black draw. */
    render_sampled(tex, NT_SAMPLER_DEFAULT, 4, 4, 4, 4);
    for (uint32_t i = 0; i < 16; i++) {
        const uint8_t *got = &s_readback[(size_t)i * 4U];
        TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, texel[0], got[0]);
        TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, texel[1], got[1]);
        TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, texel[2], got[2]);
        TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, texel[3], got[3]);
    }
    nt_gfx_destroy_texture(tex);
}

void test_partial_chain_caps_max_level_and_samples_its_last_level(void) {
    /* 16x16 + 8x8 + 4x4, one flat colour per level. */
    static const uint8_t level_color[3][4] = {{240, 20, 20, 255}, {20, 240, 20, 255}, {20, 20, 240, 255}};
    uint32_t offset = 0;
    for (uint32_t level = 0; level < 3; level++) {
        uint32_t dim = 16U >> level;
        for (uint32_t i = 0; i < dim * dim; i++) {
            memcpy(&s_chain[offset + (i * 4U)], level_color[level], 4);
        }
        offset += dim * dim * 4U;
    }
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 16,
        .height = 16,
        .data = s_chain,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .min_filter = NT_FILTER_NEAREST_MIPMAP_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .level_count = 3,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_INT(2, texture_max_level(tex));

    render_sampled(tex, NT_SAMPLER_DEFAULT, 16, 16, 1, 1);
    TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, level_color[2][0], s_readback[0]);
    TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, level_color[2][1], s_readback[1]);
    TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, level_color[2][2], s_readback[2]);
    nt_gfx_destroy_texture(tex);
}

/* The resize path builds its descriptor inside the backend, bypassing
 * make_texture; the recreated attachment must still cap at one level. */
void test_resized_render_target_color_caps_max_level(void) {
    nt_render_target_t rt = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 16,
        .height = 16,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_NONE,
    });
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(rt));
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(rt, 32, 24));
    TEST_ASSERT_EQUAL_INT(0, texture_max_level(nt_gfx_render_target_color(rt)));
    nt_gfx_destroy_render_target(rt);
}

// #endregion

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    nt_basisu_encoder_init();
#if NT_BASISU_HAS_ETC1S
    RUN_TEST(test_activate_etc1s_blob_uploads_and_samples);
#endif
#if NT_BASISU_HAS_UASTC
    RUN_TEST(test_activate_uastc_blob_uploads_and_samples);
#endif
    RUN_TEST(test_prepared_upload_rgba8);
    /* A target whose option is OFF has no transcoded chain to upload. */
#if NT_BASISU_HAS_BC7
    RUN_TEST(test_prepared_upload_bc7);
#endif
#if NT_BASISU_HAS_ASTC
    RUN_TEST(test_prepared_upload_astc);
#endif
#if NT_BASISU_HAS_ETC2
    RUN_TEST(test_prepared_upload_etc2_rgba8);
    RUN_TEST(test_prepared_upload_etc2_rgb8);
#endif
    RUN_TEST(test_single_level_texture_caps_max_level_and_still_samples);
    RUN_TEST(test_partial_chain_caps_max_level_and_samples_its_last_level);
    RUN_TEST(test_resized_render_target_color_caps_max_level);
    return UNITY_END();
}
