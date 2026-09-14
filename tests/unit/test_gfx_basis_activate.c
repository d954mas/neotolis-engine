/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "basisu/nt_basisu_transcoder.h"
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "nt_basisu_encoder.h"
#include "nt_builder.h"
#include "nt_mesh_format.h"
#include "nt_texture_format.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

/* --- Assert catching (same hookable-handler longjmp as test_gfx.c) --- */

static jmp_buf s_assert_jmp;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

#define EXPECT_ASSERT(code)                                                                                                                                                                            \
    do {                                                                                                                                                                                               \
        nt_assert_handler = test_assert_handler;                                                                                                                                                       \
        if (setjmp(s_assert_jmp) == 0) {                                                                                                                                                               \
            code;                                                                                                                                                                                      \
            nt_assert_handler = NULL;                                                                                                                                                                  \
            TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire");                                                                                                                                           \
        }                                                                                                                                                                                              \
        nt_assert_handler = NULL;                                                                                                                                                                      \
    } while (0)

#define TEST_MAX_TEXTURES 8

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 4, .max_programs = 2, .max_pipelines = 2, .max_buffers = 8, .max_textures = TEST_MAX_TEXTURES, .max_meshes = 4, .max_vertex_inputs = 4, .max_render_targets = 2});
}

void tearDown(void) {
    nt_assert_handler = NULL;
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

// #region fixtures

/* A TTEX V3 asset built exactly the way nt_builder_texture.c writes one for a
 * Basis payload, so the activator sees production bytes. */
typedef struct {
    uint8_t *blob;
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint8_t mip_count;
} basis_fixture_t;

static uint8_t full_chain_levels(uint32_t w, uint32_t h) {
    uint8_t levels = 1;
    for (uint32_t size = w > h ? w : h; size > 1; size >>= 1U) {
        levels++;
    }
    return levels;
}

/* RGB ramps along x, alpha along y: an asymmetric source separates a channel
 * swap from a transpose in any downstream readback. */
static void fill_source(uint8_t *pixels, uint32_t w, uint32_t h, bool alpha) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *px = &pixels[(((size_t)y * w) + x) * 4];
            px[0] = (uint8_t)(32U + (x * 160U / w));
            px[1] = (uint8_t)(200U - (x * 150U / w));
            px[2] = 64;
            px[3] = alpha ? (uint8_t)(40U + (y * 200U / h)) : 255;
        }
    }
}

/* hdr_format doubles as the encoder's has_alpha argument, as the builder does:
 * an RGBA8 header over opaque pixels still lets m_check_for_alpha drop the
 * alpha slices. */
static basis_fixture_t fixture_encode(uint16_t w, uint16_t h, nt_basisu_codec_t codec, bool source_alpha, uint16_t hdr_format) {
    uint8_t *src = (uint8_t *)malloc((size_t)w * h * 4);
    TEST_ASSERT_NOT_NULL(src);
    fill_source(src, w, h, source_alpha);
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, src, w, h, hdr_format == NT_TEXTURE_FORMAT_RGBA8, &opts);
    free(src);
    TEST_ASSERT_NOT_NULL(enc.data);
    TEST_ASSERT_EQUAL_UINT32(full_chain_levels(w, h), enc.mip_count);

    basis_fixture_t fixture = {0};
    fixture.size = (uint32_t)sizeof(NtTextureAssetHeader) + enc.size;
    fixture.blob = (uint8_t *)malloc(fixture.size);
    TEST_ASSERT_NOT_NULL(fixture.blob);
    NtTextureAssetHeader *hdr = (NtTextureAssetHeader *)fixture.blob;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = NT_TEXTURE_MAGIC;
    hdr->version = NT_TEXTURE_VERSION;
    hdr->format = hdr_format;
    hdr->width = w;
    hdr->height = h;
    hdr->mip_count = (uint16_t)enc.mip_count;
    hdr->compression = NT_TEXTURE_COMPRESSION_BASIS;
    hdr->flags = 0;
    hdr->default_min_filter = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR;
    hdr->default_mag_filter = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    hdr->default_wrap_u = NT_TEXTURE_DEFAULT_WRAP_REPEAT;
    hdr->default_wrap_v = NT_TEXTURE_DEFAULT_WRAP_REPEAT;
    hdr->data_size = enc.size;
    memcpy(fixture.blob + sizeof(*hdr), enc.data, enc.size);
    nt_basisu_encode_free(&enc);

    fixture.width = w;
    fixture.height = h;
    fixture.mip_count = (uint8_t)hdr->mip_count;
    return fixture;
}

static void fixture_free(basis_fixture_t *fixture) {
    free(fixture->blob);
    fixture->blob = NULL;
}

static NtTextureAssetHeader *fixture_header(basis_fixture_t *fixture) { return (NtTextureAssetHeader *)fixture->blob; }

// #endregion

// #region probes

/* The selector's contract: the first of BC7 -> ASTC -> ETC2 that the GPU
 * reports and NT_BASISU_TARGETS admits, RGBA8 otherwise. */
static nt_texture_format_t first_admitted(bool bc7, bool astc, bool etc2, bool alpha) {
    (void)bc7;
    (void)astc;
    (void)etc2;
    (void)alpha;
#if NT_BASISU_HAS_BC7
    if (bc7) {
        return NT_TEXTURE_FORMAT_BC7_RGBA;
    }
#endif
#if NT_BASISU_HAS_ASTC
    if (astc) {
        return NT_TEXTURE_FORMAT_ASTC_4x4_RGBA;
    }
#endif
#if NT_BASISU_HAS_ETC2
    if (etc2) {
        return alpha ? NT_TEXTURE_FORMAT_ETC2_RGBA8 : NT_TEXTURE_FORMAT_ETC2_RGB8;
    }
#endif
    return NT_TEXTURE_FORMAT_RGBA8;
}

/* A codec inside NT_BASISU_CODECS for the tests that are not about the codec. */
#define ANY_CODEC (NT_BASISU_HAS_UASTC ? NT_BASISU_CODEC_UASTC_LDR : NT_BASISU_CODEC_ETC1S)

static void set_caps(bool bc7, bool astc, bool etc2) {
    g_nt_gfx.gpu_caps.has_bc7 = bc7;
    g_nt_gfx.gpu_caps.has_astc = astc;
    g_nt_gfx.gpu_caps.has_etc2 = etc2;
}

/* Fills the texture pool to capacity and empties it again. A slot leaked by a
 * rejected activation shows up as a pool-full assert on the last create. */
static void expect_full_pool_available(void) {
    static const uint8_t texel[4] = {1, 2, 3, 4};
    nt_texture_t probes[TEST_MAX_TEXTURES];
    for (uint32_t i = 0; i < TEST_MAX_TEXTURES; i++) {
        probes[i] = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8, .data = texel});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, probes[i].id);
    }
    for (uint32_t i = 0; i < TEST_MAX_TEXTURES; i++) {
        nt_gfx_destroy_texture(probes[i]);
    }
}

static void expect_rejected(basis_fixture_t *fixture, uint32_t size, const char *what) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_gfx_activate_texture(fixture->blob, size), what);
    expect_full_pool_available();
}

/* Bytes the activator stages: the whole transcoded chain, back to back. */
static uint32_t chain_bytes(nt_texture_format_t format, uint16_t w, uint16_t h) {
    uint32_t total = 0;
    for (uint8_t level = 0; level < full_chain_levels(w, h); level++) {
        total += (uint32_t)nt_texture_level_bytes(format, nt_texture_level_extent(w, level), nt_texture_level_extent(h, level));
    }
    return total;
}

/* A rejected activation left no transcoder session open: a whole fresh chain
 * still transcodes. */
static void expect_transcoder_reusable(basis_fixture_t *fixture) {
    const uint8_t *payload = fixture->blob + sizeof(NtTextureAssetHeader);
    const uint32_t payload_size = fixture->size - (uint32_t)sizeof(NtTextureAssetHeader);
    nt_basisu_info_t info = {0};
    TEST_ASSERT_TRUE(nt_basisu_info(payload, payload_size, &info));
    const uint32_t bytes = chain_bytes(NT_TEXTURE_FORMAT_RGBA8, fixture->width, fixture->height);
    uint8_t *out = (uint8_t *)malloc(bytes);
    TEST_ASSERT_NOT_NULL(out);
    bool transcoded = nt_basisu_transcode_chain(payload, payload_size, &info, NT_TEXTURE_FORMAT_RGBA8, out, bytes);
    free(out);
    TEST_ASSERT_TRUE(transcoded);
}

/* The header's sampler defaults, resolved through the deduplicating cache: an
 * equal id means the activator baked exactly these filters and wraps. */
static nt_sampler_t header_default_sampler(void) {
    return nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
    });
}

static uint32_t activate_expecting(basis_fixture_t *fixture, nt_texture_format_t expect) {
    nt_gfx_fake_reset();
    uint32_t handle = nt_gfx_activate_texture(fixture->blob, fixture->size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_texture_t tex = {.id = handle};
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_INT(expect, nt_gfx_texture_format(tex));

    nt_texture_desc_t seen = nt_gfx_fake_last_texture_desc();
    TEST_ASSERT_EQUAL_UINT8(fixture->mip_count, seen.level_count);
    TEST_ASSERT_EQUAL_INT(expect, seen.format);
    TEST_ASSERT_EQUAL_UINT16(fixture->width, seen.width);
    TEST_ASSERT_EQUAL_UINT16(fixture->height, seen.height);
    TEST_ASSERT_FALSE(seen.gen_mipmaps);
    /* The whole chain arrived in one create, out of the shared staging buffer. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_texture_create_count());
    TEST_ASSERT_EQUAL_PTR(nt_gfx_test_stage_ptr(), seen.data);
    TEST_ASSERT_EQUAL_UINT32(header_default_sampler().id, nt_gfx_get_texture_default_sampler(tex).id);
    return handle;
}

static void check_target(basis_fixture_t *fixture, nt_texture_format_t expect) {
    uint32_t handle = activate_expecting(fixture, expect);
    nt_gfx_deactivate_texture(handle);
}

// #endregion

// #region caps matrix

static void caps_matrix_for_codec(nt_basisu_codec_t codec) {
    basis_fixture_t alpha = fixture_encode(16, 8, codec, true, NT_TEXTURE_FORMAT_RGBA8);
    basis_fixture_t rgb = fixture_encode(16, 8, codec, false, NT_TEXTURE_FORMAT_RGB8);
    /* An RGBA8 header over opaque pixels: the encoder drops the alpha slices,
     * so an ETC2-only device gets the 8-byte-per-block ETC2_RGB8 storage. */
    basis_fixture_t opaque_rgba = fixture_encode(16, 8, codec, false, NT_TEXTURE_FORMAT_RGBA8);

    set_caps(true, true, true);
    check_target(&alpha, first_admitted(true, true, true, true));
    set_caps(false, true, true);
    check_target(&alpha, first_admitted(false, true, true, true));
    set_caps(false, false, true);
    check_target(&alpha, first_admitted(false, false, true, true));
    check_target(&rgb, first_admitted(false, false, true, false));
    check_target(&opaque_rgba, first_admitted(false, false, true, false));
    set_caps(false, false, false);
    check_target(&alpha, NT_TEXTURE_FORMAT_RGBA8);

    fixture_free(&alpha);
    fixture_free(&rgb);
    fixture_free(&opaque_rgba);
}

#if NT_BASISU_HAS_ETC1S
void test_etc1s_caps_matrix_picks_the_first_supported_target(void) { caps_matrix_for_codec(NT_BASISU_CODEC_ETC1S); }
#endif
#if NT_BASISU_HAS_UASTC
void test_uastc_caps_matrix_picks_the_first_supported_target(void) { caps_matrix_for_codec(NT_BASISU_CODEC_UASTC_LDR); }
#endif

static void unaligned_caps_matrix_for_codec(nt_basisu_codec_t codec) {
    static const uint16_t dimensions[][2] = {{13, 7}, {1, 1}, {8, 2}, {2, 8}};
    for (size_t i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++) {
        basis_fixture_t alpha = fixture_encode(dimensions[i][0], dimensions[i][1], codec, true, NT_TEXTURE_FORMAT_RGBA8);
        basis_fixture_t rgb = fixture_encode(dimensions[i][0], dimensions[i][1], codec, false, NT_TEXTURE_FORMAT_RGB8);
        basis_fixture_t opaque_rgba = fixture_encode(dimensions[i][0], dimensions[i][1], codec, false, NT_TEXTURE_FORMAT_RGBA8);

        /* WebGL BPTC requires both base dimensions to be multiples of four. */
        set_caps(true, true, true);
        check_target(&alpha, first_admitted(false, true, true, true));
        check_target(&rgb, first_admitted(false, true, true, false));
        set_caps(true, false, true);
        check_target(&alpha, first_admitted(false, false, true, true));
        check_target(&rgb, first_admitted(false, false, true, false));
        check_target(&opaque_rgba, first_admitted(false, false, true, false));
        set_caps(true, false, false);
        check_target(&alpha, NT_TEXTURE_FORMAT_RGBA8);
        check_target(&rgb, NT_TEXTURE_FORMAT_RGBA8);

        fixture_free(&alpha);
        fixture_free(&rgb);
        fixture_free(&opaque_rgba);
    }
}

#if NT_BASISU_HAS_ETC1S
void test_etc1s_unaligned_dimensions_skip_bc7(void) { unaligned_caps_matrix_for_codec(NT_BASISU_CODEC_ETC1S); }
#endif
#if NT_BASISU_HAS_UASTC
void test_uastc_unaligned_dimensions_skip_bc7(void) { unaligned_caps_matrix_for_codec(NT_BASISU_CODEC_UASTC_LDR); }
#endif

#if !NT_BASISU_HAS_ETC1S || !NT_BASISU_HAS_UASTC
/* The native encoder still emits the codec outside NT_BASISU_CODECS; the
 * activator must fail the asset at the blob check, before any staging. */
void test_blob_of_a_codec_outside_the_set_fails_before_staging(void) {
    const nt_basisu_codec_t outside = NT_BASISU_HAS_ETC1S ? NT_BASISU_CODEC_UASTC_LDR : NT_BASISU_CODEC_ETC1S;
    basis_fixture_t alpha = fixture_encode(16, 8, outside, true, NT_TEXTURE_FORMAT_RGBA8);
    set_caps(true, true, true);
    nt_gfx_fake_reset();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_texture(alpha.blob, alpha.size));
    TEST_ASSERT_NULL(nt_gfx_test_stage_ptr());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_texture_create_count());
    expect_full_pool_available();
    fixture_free(&alpha);
}
#endif

void test_single_pixel_blob_activates_as_one_level(void) {
    basis_fixture_t one = fixture_encode(1, 1, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    TEST_ASSERT_EQUAL_UINT8(1, one.mip_count);
    set_caps(true, false, false);
    check_target(&one, NT_TEXTURE_FORMAT_RGBA8);
    fixture_free(&one);
}

void test_asymmetric_blob_activates_with_a_full_chain(void) {
    basis_fixture_t big = fixture_encode(96, 64, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    TEST_ASSERT_EQUAL_UINT8(7, big.mip_count);
    set_caps(true, false, false);
    check_target(&big, first_admitted(true, false, false, true));
    fixture_free(&big);
}

// #endregion

// #region uncompressed fallback contract

void test_rgba8_fallback_rejects_sub_updates(void) {
    basis_fixture_t alpha = fixture_encode(13, 7, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    set_caps(false, false, false);
    uint32_t handle = activate_expecting(&alpha, NT_TEXTURE_FORMAT_RGBA8);
    nt_texture_t tex = {.id = handle};
    static const uint8_t texel[4] = {9, 8, 7, 6};
    /* Basis -> RGBA8 is an ordinary RGBA8 texture that happens to be mipped. */
    EXPECT_ASSERT(nt_gfx_update_texture(tex, 0, 0, 1, 1, texel));
    nt_gfx_deactivate_texture(handle);
    fixture_free(&alpha);
}

// #endregion

// #region boundary rejections

void test_header_boundaries_reject_without_touching_the_pool(void) {
    set_caps(true, false, false);
    basis_fixture_t alpha = fixture_encode(13, 7, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    const NtTextureAssetHeader good = *fixture_header(&alpha);

    fixture_header(&alpha)->mip_count = (uint16_t)(good.mip_count + 1);
    expect_rejected(&alpha, alpha.size, "mip_count above the Basis chain");
    fixture_header(&alpha)->mip_count = (uint16_t)(good.mip_count - 1);
    expect_rejected(&alpha, alpha.size, "mip_count below the Basis chain");
    *fixture_header(&alpha) = good;

    fixture_header(&alpha)->data_size = good.data_size + 1;
    expect_rejected(&alpha, alpha.size, "data_size past the end of the blob");
    *fixture_header(&alpha) = good;
    expect_rejected(&alpha, alpha.size - 1, "buffer shorter than data_size");

    fixture_header(&alpha)->width = 12;
    expect_rejected(&alpha, alpha.size, "header width the blob disagrees with");
    *fixture_header(&alpha) = good;
    fixture_header(&alpha)->height = 8;
    expect_rejected(&alpha, alpha.size, "header height the blob disagrees with");
    *fixture_header(&alpha) = good;

    fixture_header(&alpha)->default_min_filter = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR + 1;
    expect_rejected(&alpha, alpha.size, "min_filter out of range");
    *fixture_header(&alpha) = good;
    fixture_header(&alpha)->default_mag_filter = NT_TEXTURE_DEFAULT_FILTER_LINEAR + 1;
    expect_rejected(&alpha, alpha.size, "mag_filter out of range");
    *fixture_header(&alpha) = good;
    fixture_header(&alpha)->default_wrap_u = NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT + 1;
    expect_rejected(&alpha, alpha.size, "wrap_u out of range");
    *fixture_header(&alpha) = good;

    /* 13 is a real nt_texture_format_t (BC7) but not a packed-asset pixel format. */
    fixture_header(&alpha)->format = NT_TEXTURE_FORMAT_BC7_RGBA;
    expect_rejected(&alpha, alpha.size, "compressed storage format in the header");
    *fixture_header(&alpha) = good;

    fixture_header(&alpha)->compression = NT_TEXTURE_COMPRESSION_BASIS + 1;
    expect_rejected(&alpha, alpha.size, "unknown compression");
    *fixture_header(&alpha) = good;

    /* The unmutated fixture still activates: the rejections above were the mutations. */
    check_target(&alpha, NT_TEXTURE_FORMAT_RGBA8);
    fixture_free(&alpha);
}

#if NT_BASISU_HAS_ETC1S
void test_corrupted_payload_is_rejected_and_a_good_blob_still_activates(void) {
    set_caps(true, false, false);
    basis_fixture_t alpha = fixture_encode(96, 64, NT_BASISU_CODEC_ETC1S, true, NT_TEXTURE_FORMAT_RGBA8);
    uint32_t payload = alpha.size - (uint32_t)sizeof(NtTextureAssetHeader);
    /* Past the header and slice descriptors, inside the ETC1S codebooks. */
    for (uint32_t i = payload / 4U; i < (payload * 3U) / 4U; i++) {
        alpha.blob[sizeof(NtTextureAssetHeader) + i] ^= 0xA5U;
    }
    expect_rejected(&alpha, alpha.size, "corrupted Basis payload");
    fixture_free(&alpha);

    basis_fixture_t good = fixture_encode(16, 8, NT_BASISU_CODEC_ETC1S, true, NT_TEXTURE_FORMAT_RGBA8);
    check_target(&good, first_admitted(true, false, false, true));
    fixture_free(&good);
}
#endif

// #endregion

// #region backend failures

void test_backend_failure_leaves_no_texture_and_allows_a_retry(void) {
    set_caps(true, false, false);
    basis_fixture_t alpha = fixture_encode(96, 64, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    TEST_ASSERT_EQUAL_UINT8(7, alpha.mip_count);

    nt_gfx_fake_reset();
    nt_gfx_fake_fail_texture_creates(1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_texture(alpha.blob, alpha.size));
    /* A failed create never minted a name, so there is nothing to destroy. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_texture_create_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_texture_destroy_count());
    expect_transcoder_reusable(&alpha);
    expect_full_pool_available();

    nt_gfx_fake_fail_texture_creates(0);
    check_target(&alpha, first_admitted(true, false, false, true));
    fixture_free(&alpha);
}

void test_sampler_failure_leaves_no_texture_and_allows_a_retry(void) {
    set_caps(true, false, false);
    basis_fixture_t alpha = fixture_encode(16, 8, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    nt_gfx_fake_reset();
    nt_gfx_fake_fail_next_sampler_create();
    nt_assert_handler = test_assert_handler;
    if (setjmp(s_assert_jmp) != 0) {
        nt_assert_handler = NULL;
        fixture_free(&alpha);
        TEST_FAIL_MESSAGE("Sampler backend failure must reject activation without asserting");
    }
    uint32_t handle = nt_gfx_activate_texture(alpha.blob, alpha.size);
    nt_assert_handler = NULL;
    TEST_ASSERT_EQUAL_UINT32(0, handle);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_texture_create_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_texture_destroy_count());
    /* The attempt's own GL name, not handle 0 -- the storage really went back. */
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_destroyed_texture());
    expect_transcoder_reusable(&alpha);
    expect_full_pool_available();

    check_target(&alpha, first_admitted(true, false, false, true));
    fixture_free(&alpha);
}

// #endregion

// #region context loss

void test_reactivation_after_context_restore_yields_the_same_storage(void) {
    set_caps(true, false, false);
    basis_fixture_t alpha = fixture_encode(16, 8, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    const nt_texture_format_t target = first_admitted(true, false, false, true);
    uint32_t first = activate_expecting(&alpha, target);
    nt_gfx_deactivate_texture(first);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame(); /* latches the loss and drops the backend tables */
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame(); /* restore completes; caps are re-probed */
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    nt_gfx_end_frame();
    /* The re-probe wiped the caps this test injected; the game re-activates. */
    set_caps(true, false, false);

    uint32_t second = activate_expecting(&alpha, target);
    nt_gfx_deactivate_texture(second);
    fixture_free(&alpha);
}

// #endregion

// #region staging lifecycle

/* The mesh size is tuned against the BC7 chain, so the staging tests need
 * BC7 in NT_BASISU_TARGETS. */
#if NT_BASISU_HAS_BC7
static void idle_frames(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        nt_gfx_begin_frame();
        nt_gfx_end_frame();
    }
}

/* One float3 stream: the SOA plane layout is the interleaved layout, so the
 * re-interleave through staging is a pure copy of MESH_VERTEX_BYTES. The count
 * is chosen to outgrow the 96x64 BC7 chain (8224 bytes). */
#define MESH_VERTEX_COUNT 700U
#define MESH_VERTEX_BYTES ((size_t)MESH_VERTEX_COUNT * 12U)

static void fill_valid_mesh_blob(uint8_t *blob) {
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_wire = NT_MESH_WIRE_VTX_SOA;
    hdr->vertex_count = MESH_VERTEX_COUNT;
    hdr->index_count = 3;
    hdr->vertex_data_size = MESH_VERTEX_BYTES;
    hdr->index_data_size = 6;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = 0x12345678;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
}

#define MESH_BLOB_BYTES (sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + MESH_VERTEX_BYTES + 6)

void test_staging_is_shared_grown_and_evicted(void) {
    set_caps(true, false, false);
    TEST_ASSERT_NULL(nt_gfx_test_stage_ptr());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_stage_size());

    basis_fixture_t small = fixture_encode(96, 64, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    uint32_t handle = activate_expecting(&small, NT_TEXTURE_FORMAT_BC7_RGBA);
    const void *first_ptr = nt_gfx_test_stage_ptr();
    uint32_t first_size = nt_gfx_test_stage_size();
    TEST_ASSERT_NOT_NULL(first_ptr);
    TEST_ASSERT_EQUAL_UINT32(chain_bytes(NT_TEXTURE_FORMAT_BC7_RGBA, 96, 64), first_size);

    /* Mesh SOA decode takes the same buffer and outgrows that whole chain. */
    uint8_t mesh_blob[MESH_BLOB_BYTES];
    memset(mesh_blob, 0, sizeof(mesh_blob));
    fill_valid_mesh_blob(mesh_blob);
    uint32_t mesh = nt_gfx_activate_mesh(mesh_blob, (uint32_t)sizeof(mesh_blob));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, mesh);
    const void *mesh_ptr = nt_gfx_test_stage_ptr();
    uint32_t mesh_size = nt_gfx_test_stage_size();
    TEST_ASSERT_EQUAL_UINT32((uint32_t)MESH_VERTEX_BYTES, mesh_size);
    TEST_ASSERT_GREATER_THAN_UINT32(first_size, mesh_size);
    nt_gfx_deactivate_mesh(mesh);

    /* The texture then transcodes into the buffer the mesh grew. */
    nt_gfx_deactivate_texture(handle);
    handle = activate_expecting(&small, NT_TEXTURE_FORMAT_BC7_RGBA);
    TEST_ASSERT_EQUAL_PTR(mesh_ptr, nt_gfx_test_stage_ptr());
    TEST_ASSERT_EQUAL_UINT32(mesh_size, nt_gfx_test_stage_size());
    nt_gfx_deactivate_texture(handle);

    basis_fixture_t large = fixture_encode(128, 128, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    handle = activate_expecting(&large, NT_TEXTURE_FORMAT_BC7_RGBA);
    TEST_ASSERT_EQUAL_UINT32(chain_bytes(NT_TEXTURE_FORMAT_BC7_RGBA, 128, 128), nt_gfx_test_stage_size());
    TEST_ASSERT_GREATER_THAN_UINT32(first_size, nt_gfx_test_stage_size());
    uint32_t grown_size = nt_gfx_test_stage_size();
    nt_gfx_deactivate_texture(handle);

    /* NT_STAGE_IDLE_FRAMES frames keep it; the next one frees it. */
    idle_frames(60);
    TEST_ASSERT_NOT_NULL(nt_gfx_test_stage_ptr());
    TEST_ASSERT_EQUAL_UINT32(grown_size, nt_gfx_test_stage_size());
    idle_frames(1);
    TEST_ASSERT_NULL(nt_gfx_test_stage_ptr());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_stage_size());

    /* A failed activation leaves staging behind, and idling still evicts it. */
    nt_gfx_fake_fail_texture_creates(1);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_texture(small.blob, small.size));
    nt_gfx_fake_fail_texture_creates(0);
    TEST_ASSERT_NOT_NULL(nt_gfx_test_stage_ptr());
    idle_frames(61);
    TEST_ASSERT_NULL(nt_gfx_test_stage_ptr());

    fixture_free(&small);
    fixture_free(&large);
}

void test_shutdown_releases_a_live_staging_buffer(void) {
    set_caps(true, false, false);
    basis_fixture_t small = fixture_encode(96, 64, ANY_CODEC, true, NT_TEXTURE_FORMAT_RGBA8);
    uint32_t handle = activate_expecting(&small, NT_TEXTURE_FORMAT_BC7_RGBA);
    TEST_ASSERT_NOT_NULL(nt_gfx_test_stage_ptr());
    nt_gfx_deactivate_texture(handle);

    nt_gfx_shutdown();
    TEST_ASSERT_NULL(nt_gfx_test_stage_ptr());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_stage_size());
    fixture_free(&small);
    setUp(); /* tearDown's shutdown must be the only one left to run */
}
#endif

// #endregion

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    nt_basisu_encoder_init();
#if NT_BASISU_HAS_ETC1S
    RUN_TEST(test_etc1s_caps_matrix_picks_the_first_supported_target);
    RUN_TEST(test_etc1s_unaligned_dimensions_skip_bc7);
    RUN_TEST(test_corrupted_payload_is_rejected_and_a_good_blob_still_activates);
#endif
#if NT_BASISU_HAS_UASTC
    RUN_TEST(test_uastc_caps_matrix_picks_the_first_supported_target);
    RUN_TEST(test_uastc_unaligned_dimensions_skip_bc7);
#endif
#if !NT_BASISU_HAS_ETC1S || !NT_BASISU_HAS_UASTC
    RUN_TEST(test_blob_of_a_codec_outside_the_set_fails_before_staging);
#endif
    RUN_TEST(test_single_pixel_blob_activates_as_one_level);
    RUN_TEST(test_asymmetric_blob_activates_with_a_full_chain);
    RUN_TEST(test_rgba8_fallback_rejects_sub_updates);
    RUN_TEST(test_header_boundaries_reject_without_touching_the_pool);
    RUN_TEST(test_backend_failure_leaves_no_texture_and_allows_a_retry);
    RUN_TEST(test_sampler_failure_leaves_no_texture_and_allows_a_retry);
    RUN_TEST(test_reactivation_after_context_restore_yields_the_same_storage);
#if NT_BASISU_HAS_BC7
    RUN_TEST(test_staging_is_shared_grown_and_evicted);
    RUN_TEST(test_shutdown_releases_a_live_staging_buffer);
#endif
    return UNITY_END();
}
