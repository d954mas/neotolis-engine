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
#include "nt_builder_internal.h"
#include "nt_texture_format.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/nt_gfx_test_frame.h"
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
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 16, .max_programs = 8, .max_pipelines = 8, .max_buffers = 8, .max_textures = 16, .max_meshes = 4, .max_vertex_inputs = 8, .max_render_targets = 4));
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) { nt_gfx_shutdown(); }

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
static void build_fixture(nt_basisu_codec_t codec, nt_texture_pixel_format_t format) {
    fill_source();
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, s_src, SRC_W, SRC_H, format, &opts);
    TEST_ASSERT_NOT_NULL(enc.data);
    TEST_ASSERT_EQUAL_UINT32(SRC_LEVELS, enc.mip_count);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(s_blob) - sizeof(NtTextureAssetHeader), enc.size);

    NtTextureAssetHeader *hdr = (NtTextureAssetHeader *)s_blob;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = NT_TEXTURE_MAGIC;
    hdr->version = NT_TEXTURE_VERSION;
    hdr->format = (uint16_t)format;
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

    s_info = nt_basisu_info(s_blob + sizeof(*hdr), payload_size);
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
    nt_basisu_transcode_chain(fixture_payload(), fixture_payload_size(), &s_info, NT_TEXTURE_FORMAT_RGBA8, s_reference);
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
    nt_basisu_transcode_chain(fixture_payload(), fixture_payload_size(), &s_info, format, s_chain);
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
/* Samples an sRGB texture (linear values) and encodes once with the shipped
 * common/color.glsl: the readback then compares with the stored bytes. */
static char s_encode_fs[2048];

static void load_encode_fs(void) {
    char color_glsl[1024] = {0};
    FILE *file = fopen(NT_COLOR_GLSL_PATH, "rb");
    TEST_ASSERT_NOT_NULL(file);
    const size_t read = fread(color_glsl, 1, sizeof(color_glsl) - 1, file);
    (void)fclose(file);
    TEST_ASSERT_TRUE(read > 0 && read < sizeof(color_glsl) - 1);
    (void)snprintf(s_encode_fs, sizeof(s_encode_fs),
                   "precision highp float;\n%s\nuniform sampler2D u_tex;\nin vec2 v_uv;\nout vec4 frag_color;\n"
                   "void main() { vec4 c = texture(u_tex, v_uv); frag_color = vec4(nt_linear_to_srgb(c.rgb), c.a); }\n",
                   color_glsl);
}

/* Draws the texture over `vp_w x vp_h` of a `rt_w x rt_h` colour target and
 * reads the viewport back. Straight alpha survives: no blending is enabled. */
static void render_sampled(nt_texture_t tex, nt_sampler_t sampler, uint16_t rt_w, uint16_t rt_h, int vp_w, int vp_h, const char *fs_source) {
    nt_texture_t color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = rt_w, .height = rt_h, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_render_target_t rt = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color});
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_fullscreen_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});

    memset(s_readback, 0, sizeof(s_readback));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = rt, .clear_color = {0.0F, 0.0F, 0.0F, 0.0F}});
    nt_gfx_set_viewport(0, 0, vp_w, vp_h);
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = tex, .sampler = sampler};
    nt_gfx_apply_texture_bindings(&binding, 1);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    const bool read = nt_gfx_read_pixels(rt, 0, 0, vp_w, vp_h, s_readback, (uint32_t)sizeof(s_readback));

    nt_gfx_destroy_texture(color);
    nt_gfx_destroy_vertex_input(vi);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(read);
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

/* The selector's contract: the first of the per-codec order (ETC1S: ETC2 -> BC7 ->
 * ASTC; UASTC: ASTC -> BC7 -> ETC2) the GPU reports and NT_BASISU_HAS_* admits,
 * RGBA8 otherwise. The fixture carries alpha, so ETC2 means ETC2_RGBA8. */
static nt_texture_format_t expected_target(nt_basisu_codec_t codec) {
    const nt_gfx_gpu_caps_t *caps = nt_gfx_gpu_caps();
    const bool bc7 = NT_BASISU_HAS_BC7 && caps->has_bc7;
    const bool astc = NT_BASISU_HAS_ASTC && caps->has_astc;
    const bool etc2 = NT_BASISU_HAS_ETC2 && caps->has_etc2;
    const nt_texture_format_t etc1s_order[] = {etc2 ? NT_TEXTURE_FORMAT_ETC2_RGBA8 : NT_TEXTURE_FORMAT_INVALID, bc7 ? NT_TEXTURE_FORMAT_BC7_RGBA : NT_TEXTURE_FORMAT_INVALID,
                                               astc ? NT_TEXTURE_FORMAT_ASTC_4x4_RGBA : NT_TEXTURE_FORMAT_INVALID};
    const nt_texture_format_t uastc_order[] = {astc ? NT_TEXTURE_FORMAT_ASTC_4x4_RGBA : NT_TEXTURE_FORMAT_INVALID, bc7 ? NT_TEXTURE_FORMAT_BC7_RGBA : NT_TEXTURE_FORMAT_INVALID,
                                               etc2 ? NT_TEXTURE_FORMAT_ETC2_RGBA8 : NT_TEXTURE_FORMAT_INVALID};
    const nt_texture_format_t *order = codec == NT_BASISU_CODEC_ETC1S ? etc1s_order : uastc_order;
    for (size_t i = 0; i < 3; i++) {
        if (order[i] != NT_TEXTURE_FORMAT_INVALID) {
            return order[i];
        }
    }
    return NT_TEXTURE_FORMAT_RGBA8;
}

/* An admitted codec for the tests that are not about the codec. */
#define ANY_CODEC (NT_BASISU_HAS_UASTC ? NT_BASISU_CODEC_UASTC_LDR : NT_BASISU_CODEC_ETC1S)

static GLint texture_max_level(nt_texture_t tex) {
    nt_gfx_backend_bind_texture_unit(nt_gfx_test_texture_backend_id(tex), nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(tex)), 0);
    GLint value = -1;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &value);
    return value;
}

// #endregion

// #region (a) activation on the detected caps

/* The caps matrix pins the exact sRGB twin; here the real GPU must decode it. */
static void check_activation(nt_basisu_codec_t codec, uint8_t tolerance, bool srgb) {
    build_fixture(codec, srgb ? NT_TEXTURE_FORMAT_SRGB8_ALPHA8 : NT_TEXTURE_FORMAT_RGBA8);
    const char *fs = srgb ? s_encode_fs : s_sample_fs;
    uint32_t handle = nt_gfx_activate_texture(s_blob, s_blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_texture_t tex = {.id = handle};
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    const nt_texture_format_t format = nt_gfx_texture_format(tex);
    if (srgb) {
        TEST_ASSERT_TRUE(nt_texture_format_is_srgb(format) && nt_texture_format_is_compressed(format) == nt_texture_format_is_compressed(expected_target(codec)));
    } else {
        TEST_ASSERT_EQUAL_INT(expected_target(codec), format);
    }
    TEST_ASSERT_EQUAL_INT((int)s_info.level_count - 1, texture_max_level(tex));

    /* Level 0 through the asset's own LINEAR_MIPMAP_LINEAR default sampler. */
    decode_reference(0);
    render_sampled(tex, NT_SAMPLER_DEFAULT, SRC_W, SRC_H, SRC_W, SRC_H, fs);
    check_against_reference(0, tolerance, false);

    /* One pixel of viewport forces the LOD past the end of the chain, so
     * NEAREST_MIPMAP_NEAREST clamps to MAX_LEVEL: the 1x1 tail. */
    nt_sampler_t tail = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_NEAREST_MIPMAP_NEAREST, .mag_filter = NT_FILTER_NEAREST});
    decode_reference(s_info.level_count - 1U);
    render_sampled(tex, tail, SRC_W, SRC_H, 1, 1, fs);
    check_against_reference(s_info.level_count - 1U, tolerance, false);

    nt_gfx_deactivate_texture(handle);
}

#if NT_BASISU_HAS_ETC1S
void test_activate_etc1s_blob_uploads_and_samples(void) { check_activation(NT_BASISU_CODEC_ETC1S, TOL_ETC1S, false); }
void test_activate_etc1s_srgb_blob_takes_the_srgb_target(void) { check_activation(NT_BASISU_CODEC_ETC1S, TOL_ETC1S, true); }
#endif
#if NT_BASISU_HAS_UASTC
void test_activate_uastc_blob_uploads_and_samples(void) { check_activation(NT_BASISU_CODEC_UASTC_LDR, TOL_UASTC, false); }
void test_activate_uastc_srgb_blob_takes_the_srgb_target(void) { check_activation(NT_BASISU_CODEC_UASTC_LDR, TOL_UASTC, true); }
#endif

// #endregion

// #region (b) prepared upload of a contiguous chain

static void check_prepared_upload(nt_texture_format_t format, uint8_t tolerance) {
    build_fixture(ANY_CODEC, NT_TEXTURE_FORMAT_RGBA8);
    const char *fs = nt_texture_format_is_srgb(format) ? s_encode_fs : s_sample_fs;
    const bool opaque_storage = format == NT_TEXTURE_FORMAT_ETC2_RGB8 || format == NT_TEXTURE_FORMAT_ETC2_SRGB8;
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
    render_sampled(tex, NT_SAMPLER_DEFAULT, SRC_W, SRC_H, SRC_W, SRC_H, fs);
    check_against_reference(0, tolerance, opaque_storage);

    /* The declared chain really reaches GL: the tail level is sampled too. */
    decode_reference(s_info.level_count - 1U);
    render_sampled(tex, NT_SAMPLER_DEFAULT, SRC_W, SRC_H, 1, 1, fs);
    check_against_reference(s_info.level_count - 1U, tolerance, opaque_storage);

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

/* The sRGB twins upload the same blocks: decode on sampling, encode on output, back at the reference. */
void test_prepared_upload_srgb8_alpha8(void) { check_prepared_upload(NT_TEXTURE_FORMAT_SRGB8_ALPHA8, TOL_EXACT); }

void test_prepared_upload_bc7_srgb(void) {
    if (!nt_gfx_gpu_caps()->has_bc7) {
        TEST_IGNORE_MESSAGE("BC7_SRGB_ALPHA unverified on this host -- needs a GPU with has_bc7");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_BC7_SRGB_ALPHA, TOL_UASTC);
}

void test_prepared_upload_astc_srgb(void) {
    if (!nt_gfx_gpu_caps()->has_astc) {
        TEST_IGNORE_MESSAGE("ASTC_4x4_SRGB8_ALPHA8 unverified on this host -- needs a GPU with has_astc");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_ASTC_4x4_SRGB8_ALPHA8, TOL_UASTC);
}

void test_prepared_upload_etc2_srgb(void) {
    if (!nt_gfx_gpu_caps()->has_etc2) {
        TEST_IGNORE_MESSAGE("ETC2 sRGB unverified on this host -- needs a GPU with has_etc2");
    }
    check_prepared_upload(NT_TEXTURE_FORMAT_ETC2_SRGB8_ALPHA8, TOL_ETC1S);
    check_prepared_upload(NT_TEXTURE_FORMAT_ETC2_SRGB8, TOL_ETC1S);
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
    render_sampled(tex, NT_SAMPLER_DEFAULT, 4, 4, 4, 4, s_sample_fs);
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

    render_sampled(tex, NT_SAMPLER_DEFAULT, 16, 16, 1, 1, s_sample_fs);
    TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, level_color[2][0], s_readback[0]);
    TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, level_color[2][1], s_readback[1]);
    TEST_ASSERT_UINT8_WITHIN(TOL_EXACT, level_color[2][2], s_readback[2]);
    nt_gfx_destroy_texture(tex);
}

/* Attachment textures usually carry the zero level_count spelling; they must still cap at one level. */
void test_render_target_color_caps_max_level(void) {
    nt_texture_t color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 32, .height = 24, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_render_target_t rt = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color});
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(rt));
    TEST_ASSERT_EQUAL_INT(0, texture_max_level(color));
    nt_gfx_destroy_texture(color);
}

// #endregion

// #region (d) RAW sRGB decode, round trip and updates

#define RAMP_W 64U
#define RAMP_H 4U

static nt_texture_t make_nearest(nt_texture_format_t format, uint16_t w, uint16_t h, const void *data) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = w,
        .height = h,
        .data = data,
        .format = format,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_INT(format, nt_gfx_texture_format(tex));
    return tex;
}

/* Readback rows are top-left first; GL row 0 is the source's row 0. */
static void check_readback_rgba(const uint8_t *want_rgba, uint32_t w, uint32_t h, uint8_t tolerance) {
    for (uint32_t ry = 0; ry < h; ry++) {
        for (uint32_t rx = 0; rx < w; rx++) {
            const uint8_t *got = &s_readback[(((size_t)ry * w) + rx) * 4];
            const uint8_t *want = &want_rgba[((((size_t)h - 1U - ry) * w) + rx) * 4];
            for (uint32_t c = 0; c < 4; c++) {
                TEST_ASSERT_UINT8_WITHIN(tolerance, want[c], got[c]);
            }
        }
    }
}

void test_srgb_texel_128_samples_as_linear_with_linear_alpha(void) {
    static const uint8_t texel[4] = {128, 128, 128, 128};
    nt_texture_t srgb = make_nearest(NT_TEXTURE_FORMAT_SRGB8_ALPHA8, 1, 1, texel);
    render_sampled(srgb, NT_SAMPLER_DEFAULT, 1, 1, 1, 1, s_sample_fs);
    /* 128 decodes to 0.21586 linear (55.04 / 255); alpha stays 128 / 255. */
    static const uint8_t decoded[4] = {55, 55, 55, 128};
    check_readback_rgba(decoded, 1, 1, 1);
    nt_gfx_destroy_texture(srgb);

    nt_texture_t plain = make_nearest(NT_TEXTURE_FORMAT_RGBA8, 1, 1, texel);
    render_sampled(plain, NT_SAMPLER_DEFAULT, 1, 1, 1, 1, s_sample_fs);
    check_readback_rgba(texel, 1, 1, 0);
    nt_gfx_destroy_texture(plain);
}

/* Every byte value in each channel, channels out of phase: decode then a single
 * encode gives the source back within one quantization step. */
void test_srgb_ramp_decodes_and_encodes_back_to_the_source(void) {
    static uint8_t ramp[RAMP_W * RAMP_H * 4];
    static uint8_t ramp_rgb[RAMP_W * RAMP_H * 3];
    for (uint32_t i = 0; i < RAMP_W * RAMP_H; i++) {
        ramp[(i * 4U) + 0] = (uint8_t)i;
        ramp[(i * 4U) + 1] = (uint8_t)(255U - i);
        ramp[(i * 4U) + 2] = (uint8_t)(i * 7U);
        ramp[(i * 4U) + 3] = (uint8_t)(i ^ 0x5AU);
        memcpy(&ramp_rgb[(size_t)i * 3U], &ramp[(size_t)i * 4U], 3);
    }
    nt_texture_t srgb = make_nearest(NT_TEXTURE_FORMAT_SRGB8_ALPHA8, RAMP_W, RAMP_H, ramp);
    render_sampled(srgb, NT_SAMPLER_DEFAULT, RAMP_W, RAMP_H, RAMP_W, RAMP_H, s_encode_fs);
    check_readback_rgba(ramp, RAMP_W, RAMP_H, 1);
    nt_gfx_destroy_texture(srgb);

    /* SRGB8 has no alpha: it samples as 1.0. */
    nt_texture_t srgb_rgb = make_nearest(NT_TEXTURE_FORMAT_SRGB8, RAMP_W, RAMP_H, ramp_rgb);
    render_sampled(srgb_rgb, NT_SAMPLER_DEFAULT, RAMP_W, RAMP_H, RAMP_W, RAMP_H, s_encode_fs);
    for (uint32_t i = 0; i < RAMP_W * RAMP_H; i++) {
        ramp[(i * 4U) + 3] = 255;
    }
    check_readback_rgba(ramp, RAMP_W, RAMP_H, 1);
    nt_gfx_destroy_texture(srgb_rgb);
}

/* A sub-update keeps the stored sRGB format; 3-texel SRGB8 rows (9 bytes) also exercise the unpack alignment. */
void test_srgb_updates_keep_the_interpretation(void) {
    static uint8_t zeros[5 * 3 * 3];
    static const uint8_t patch_rgb[3 * 3 * 3] = {10, 60, 128, 200, 30, 90, 255, 0, 17, 128, 128, 128, 1, 2, 3, 250, 240, 230, 77, 88, 99, 33, 44, 55, 128, 64, 32};
    uint8_t want[5 * 3 * 4];
    for (uint32_t i = 0; i < 5 * 3; i++) {
        want[(i * 4U) + 0] = 0;
        want[(i * 4U) + 1] = 0;
        want[(i * 4U) + 2] = 0;
        want[(i * 4U) + 3] = 255;
    }
    for (uint32_t y = 0; y < 3; y++) {
        for (uint32_t x = 0; x < 3; x++) {
            memcpy(&want[(((size_t)y * 5U) + 1U + x) * 4U], &patch_rgb[(((size_t)y * 3U) + x) * 3U], 3);
        }
    }
    nt_texture_t rgb = make_nearest(NT_TEXTURE_FORMAT_SRGB8, 5, 3, zeros);
    nt_gfx_update_texture(rgb, 1, 0, 3, 3, patch_rgb);
    render_sampled(rgb, NT_SAMPLER_DEFAULT, 5, 3, 5, 3, s_encode_fs);
    check_readback_rgba(want, 5, 3, 1);
    nt_gfx_destroy_texture(rgb);
}

// #endregion

// #region RAW RGB9E5 (HDR)

#define HDR_W 8U
#define HDR_H 4U

/* Scaled by 1/16 into RGBA8 with a step at 1 in alpha: values above 1 must reach the
 * shader unclamped, and no float framebuffer is involved. */
static const char *s_hdr_fs = "precision highp float;\n"
                              "uniform highp sampler2D u_tex;\n"
                              "in vec2 v_uv;\n"
                              "out vec4 frag_color;\n"
                              "void main() { vec3 c = texture(u_tex, v_uv).rgb; frag_color = vec4(c / 16.0, step(1.0, c.g)); }\n";

static uint8_t hdr_byte(float v) { return (uint8_t)((v / 16.0F * 255.0F) + 0.5F); }

/* Left half (0.25, 2, 8), right half (8, 0.25, 2): a channel swap, horizontal flip or clamp fails. */
void test_raw_rgb9e5_chain_samples_above_one(void) {
    static float rgb[HDR_W * HDR_H * 3];
    for (uint32_t y = 0; y < HDR_H; y++) {
        for (uint32_t x = 0; x < HDR_W; x++) {
            float *px = &rgb[(((size_t)y * HDR_W) + x) * 3];
            const bool left = x < HDR_W / 2;
            px[0] = left ? 0.25F : 8.0F;
            px[1] = left ? 2.0F : 0.25F;
            px[2] = left ? 8.0F : 2.0F;
        }
    }
    nt_tex_opts_t opts = nt_tex_opts_defaults();
    opts.format = NT_TEXTURE_FORMAT_RGB9E5;
    opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_NEAREST_MIPMAP_NEAREST;
    opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_NEAREST;
    opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    opts.wrap_v = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
    uint8_t *asset = NULL;
    uint32_t asset_size = 0;
    nt_builder_encode_texture_to_buf(rgb, HDR_W, HDR_H, &opts, 1, &asset, &asset_size);

    const uint32_t handle = nt_gfx_activate_texture(asset, asset_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_texture_t tex = {.id = handle};
    TEST_ASSERT_EQUAL_INT(NT_TEXTURE_FORMAT_RGB9E5, nt_gfx_texture_format(tex));
    TEST_ASSERT_EQUAL_INT(3, texture_max_level(tex)); /* 8x4 -> 4x2 -> 2x1 -> 1x1 */

    uint8_t want[HDR_W * HDR_H * 4];
    for (uint32_t i = 0; i < HDR_W * HDR_H; i++) {
        const bool left = (i % HDR_W) < HDR_W / 2;
        want[(i * 4U) + 0] = hdr_byte(left ? 0.25F : 8.0F);
        want[(i * 4U) + 1] = hdr_byte(left ? 2.0F : 0.25F);
        want[(i * 4U) + 2] = hdr_byte(left ? 8.0F : 2.0F);
        want[(i * 4U) + 3] = left ? 255 : 0;
    }
    render_sampled(tex, NT_SAMPLER_DEFAULT, HDR_W, HDR_H, HDR_W, HDR_H, s_hdr_fs);
    check_readback_rgba(want, HDR_W, HDR_H, 1);

    /* The 1x1 tail is the builder's box average of both halves. */
    const uint8_t tail[4] = {hdr_byte(4.125F), hdr_byte(1.125F), hdr_byte(5.0F), 255};
    render_sampled(tex, NT_SAMPLER_DEFAULT, HDR_W, HDR_H, 1, 1, s_hdr_fs);
    check_readback_rgba(tail, 1, 1, 1);

    nt_gfx_deactivate_texture(handle);
    free(asset);
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = 128,
        .height = 128,
    };
    nt_window_init();
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    nt_basisu_encoder_init();
    load_encode_fs();
#if NT_BASISU_HAS_ETC1S
    RUN_TEST(test_activate_etc1s_blob_uploads_and_samples);
    RUN_TEST(test_activate_etc1s_srgb_blob_takes_the_srgb_target);
#endif
#if NT_BASISU_HAS_UASTC
    RUN_TEST(test_activate_uastc_blob_uploads_and_samples);
    RUN_TEST(test_activate_uastc_srgb_blob_takes_the_srgb_target);
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
    RUN_TEST(test_render_target_color_caps_max_level);
    RUN_TEST(test_prepared_upload_srgb8_alpha8);
#if NT_BASISU_HAS_BC7
    RUN_TEST(test_prepared_upload_bc7_srgb);
#endif
#if NT_BASISU_HAS_ASTC
    RUN_TEST(test_prepared_upload_astc_srgb);
#endif
#if NT_BASISU_HAS_ETC2
    RUN_TEST(test_prepared_upload_etc2_srgb);
#endif
    RUN_TEST(test_srgb_texel_128_samples_as_linear_with_linear_alpha);
    RUN_TEST(test_srgb_ramp_decodes_and_encodes_back_to_the_source);
    RUN_TEST(test_srgb_updates_keep_the_interpretation);
    RUN_TEST(test_raw_rgb9e5_chain_samples_above_one);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
