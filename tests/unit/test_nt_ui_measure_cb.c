/* Floats compared via memcmp/integer (UNITY_EXCLUDE_FLOAT). */

/* System headers before Unity -- avoids __declspec(noreturn) clash on MSVC. */
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "input/nt_input.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"
/* clang-format on */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_DEFAULT_ARENA_SIZE];
static const nt_ui_create_desc_t s_ui_desc = {.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT};

/* Per-test counter so virtual-pack ids stay unique across the binary. */
static uint32_t s_vpack_counter;

/* ---- Test font blob builder (mirrors test_font.c with 3 glyphs A/B/C) ---- */

static uint8_t *build_test_font_blob(uint32_t *out_size) {
    /* 1 contour with 2 line segments per glyph = 18 bytes per glyph. */
    const uint32_t contour_size = 18U;
    const uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    const uint32_t glyphs_size = 3U * (uint32_t)sizeof(NtFontGlyphEntry);
    const uint32_t total_size = header_size + glyphs_size + (3U * contour_size);

    uint8_t *blob = (uint8_t *)calloc(total_size, 1U);
    NT_ASSERT(blob);

    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = 3;
    hdr.units_per_em = 1000;
    hdr.ascent = 800;
    hdr.descent = -200;
    hdr.line_gap = 0;
    memcpy(blob, &hdr, sizeof hdr);

    const uint32_t data_base = header_size + glyphs_size;
    const uint32_t cp[3] = {'A', 'B', 'C'};
    for (int g = 0; g < 3; ++g) {
        NtFontGlyphEntry e;
        memset(&e, 0, sizeof e);
        e.codepoint = cp[g];
        e.data_offset = data_base + ((uint32_t)g * contour_size);
        e.advance = 500;
        e.bbox_x0 = 0;
        e.bbox_y0 = -200;
        e.bbox_x1 = 400;
        e.bbox_y1 = 800;
        e.curve_count = 2;
        e.kern_count = 0;
        memcpy(blob + header_size + ((size_t)g * sizeof(NtFontGlyphEntry)), &e, sizeof e);
    }

    for (int g = 0; g < 3; ++g) {
        uint8_t *wp = blob + data_base + ((size_t)g * contour_size);
        uint16_t cc = 1U;
        uint16_t sc = 2U;
        memcpy(wp, &cc, 2);
        wp += 2;
        memcpy(wp, &sc, 2);
        wp += 2;
        int16_t sx = 0;
        int16_t sy = 0;
        memcpy(wp, &sx, 2);
        wp += 2;
        memcpy(wp, &sy, 2);
        wp += 2;
        wp[0] = 0;
        wp[1] = 0;
        wp += 2;
        int16_t d1x = 400;
        int16_t d1y = 0;
        memcpy(wp, &d1x, 2);
        wp += 2;
        memcpy(wp, &d1y, 2);
        wp += 2;
        int16_t d2x = 0;
        int16_t d2y = 800;
        memcpy(wp, &d2x, 2);
        wp += 2;
        memcpy(wp, &d2y, 2);
    }

    *out_size = total_size;
    return blob;
}

static nt_resource_t register_font_resource(const char *name, const uint8_t *blob, uint32_t blob_size) {
    uint32_t data_handle = nt_font_test_register_data(blob, blob_size);

    char pack_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "fp_%s_%u", name, s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t rid = nt_hash64_str(name);
    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, rid, NT_ASSET_FONT, data_handle);
    return nt_resource_request(rid, NT_ASSET_FONT);
}

static nt_font_t make_resolved_test_font(const char *name, uint8_t **out_blob) {
    nt_font_create_desc_t fd = {
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 16,
        .band_count = 4,
        .measure_cache_size = 256,
    };
    nt_font_t font = nt_font_create(&fd);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource(name, blob, blob_size);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    *out_blob = blob;
    return font;
}

/* ---- Unity setUp / tearDown ---- */

void setUp(void) {
    nt_test_assert_install();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 8, .max_textures = 32, .max_meshes = 8});
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    nt_ui_module_init();
    s_vpack_counter = 0;
}

void tearDown(void) {
    nt_ui_module_shutdown();
    nt_font_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
    nt_gfx_shutdown();
}

/* ---- Helpers ---- */

/* Accept +0 / -0. */
static bool float_is_zero_bits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return (b == 0U) || (b == 0x80000000U);
}

/* Returns the bbox of the emitted TEXT cmd; {0,0,0,0} if none emitted. */
static Clay_BoundingBox declare_and_measure_text(nt_ui_context_t *ctx, const char *utf8, uint16_t font_id, uint16_t font_size) {
    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(ctx, 800.0F, 600.0F, &mouse);

    /* Build a Clay_String wrapper from the literal. CLAY_TEXT's first arg
     * is a Clay_String value (not a pointer). */
    Clay_String s = {.length = (int32_t)strlen(utf8), .chars = utf8};
    CLAY_TEXT(s, CLAY_TEXT_CONFIG({.fontId = font_id, .fontSize = font_size}));
    nt_ui_end(ctx);

    Clay_BoundingBox empty = {0};
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            return c->boundingBox;
        }
    }
    return empty;
}

/* ---- Tests ---- */

/* Clay_SetMeasureTextFunction is wired during nt_ui_create_context.
 * Covariance check: with no font registered the callback returns {0,0} via
 * the nt_font_valid branch. The fact that Clay_EndLayout completes without
 * firing CLAY_ERROR_TYPE_TEXT_MEASUREMENT_FUNCTION_NOT_PROVIDED is itself
 * evidence that the measure callback is wired. */
/* measure_cb runs DURING Clay layout (nt_ui_end). Unbound font slot is a
 * contract violation symmetric with emit_text -- asserts here (one frame
 * earlier than the walker would catch it).
 *
 * Death-test caveat: assert handler longjmps OUT of nt_ui_end, leaving
 * ctx->in_frame=true and g_nt_ui_inframe_ctx dangling. We skip destroy
 * (would trip its own in_frame guard with armed=false handler -> trap).
 * Static arena gets reused next test; module_init clears the globals. */
static void test_measure_callback_unbound_font_asserts(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);
    /* No font set at slot 0 -> measure_cb fires during nt_ui_end and asserts. */
    NT_TEST_EXPECT_ASSERT(declare_and_measure_text(ctx, "ABC", 0U, 14U));
}

/* callback forwards Clay_StringSlice -> nt_font_measure_n with
 * proper font_id resolution. Compares the TEXT command bounding box
 * against an independent direct nt_font_measure_n call. */
static void test_measure_callback_forwards_to_font_measure_n(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("measure_cb_forward", &blob);
    TEST_ASSERT_TRUE(nt_font_valid(font));

    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_ui_set_font(ctx, 0U, font);

    /* Independent ground truth: invoke the exact same measure path the
     * callback takes (with the size cast to float matching uint16_t->float). */
    nt_text_size_t expected = nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
    /* Sanity: the resolved test font must produce positive dimensions for
     * a 3-glyph "ABC" measurement. If this fails the test_font setup is
     * broken, not the callback. */
    TEST_ASSERT_FALSE(float_is_zero_bits(expected.width));
    TEST_ASSERT_FALSE(float_is_zero_bits(expected.height));

    Clay_BoundingBox bb = declare_and_measure_text(ctx, "ABC", 0U, 14U);

    /* Clay can round/quantize the bbox slightly during layout; compare
     * via integer truncation with a 1 px tolerance window. The truncation
     * keeps the comparison free of UNITY_EXCLUDE_FLOAT macros. */
    const int32_t exp_w = (int32_t)expected.width;
    const int32_t exp_h = (int32_t)expected.height;
    const int32_t got_w = (int32_t)bb.width;
    const int32_t got_h = (int32_t)bb.height;
    const int32_t diff_w = (got_w > exp_w) ? (got_w - exp_w) : (exp_w - got_w);
    const int32_t diff_h = (got_h > exp_h) ? (got_h - exp_h) : (exp_h - got_h);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(1, diff_w);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(1, diff_h);
    /* And that we did get a positive measurement back -- if Clay culled
     * the element or the callback returned {0,0} unexpectedly, both
     * dimensions would be zero. */
    TEST_ASSERT_GREATER_THAN_INT32(0, got_w);
    TEST_ASSERT_GREATER_THAN_INT32(0, got_h);

    nt_ui_destroy_context(ctx);
    nt_font_destroy(font);
    free(blob);
}

/* Variant with explicit letterSpacing on the text config. */
static Clay_BoundingBox declare_and_measure_text_ls(nt_ui_context_t *ctx, const char *utf8, uint16_t font_id, uint16_t font_size, uint16_t letter_spacing) {
    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(ctx, 800.0F, 600.0F, &mouse);
    Clay_String s = {.length = (int32_t)strlen(utf8), .chars = utf8};
    CLAY_TEXT(s, CLAY_TEXT_CONFIG({.fontId = font_id, .fontSize = font_size, .letterSpacing = letter_spacing}));
    nt_ui_end(ctx);

    Clay_BoundingBox empty = {0};
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            return c->boundingBox;
        }
    }
    return empty;
}

/* Clay's MeasureTextCached subtracts one letterSpacing slot from the
 * line width (clay.h line 1677). The bridge has to compensate by adding
 * one trailing slot so Clay's final bbox matches the renderer's CSS-style
 * (N-1)*ls width. Without the bridge fix the bbox is short by exactly
 * one letterSpacing. */
static void test_measure_callback_letter_spacing_matches_render_width(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("measure_cb_ls", &blob);
    TEST_ASSERT_TRUE(nt_font_valid(font));

    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_ui_set_font(ctx, 0U, font);

    /* CSS-style render width is exactly what nt_font_measure_n returns. */
    nt_text_size_t expected = nt_font_measure_n(font, "ABC", 3U, 14.0F, 5.0F);
    TEST_ASSERT_FALSE(float_is_zero_bits(expected.width));

    Clay_BoundingBox bb = declare_and_measure_text_ls(ctx, "ABC", 0U, 14U, 5U);

    /* Clay quantizes layout to int pixels in some paths; allow 1 px tolerance.
     * Without the bridge fix this would be off by exactly letterSpacing=5. */
    const int32_t exp_w = (int32_t)expected.width;
    const int32_t got_w = (int32_t)bb.width;
    const int32_t diff_w = (got_w > exp_w) ? (got_w - exp_w) : (exp_w - got_w);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(1, diff_w);
    TEST_ASSERT_GREATER_THAN_INT32(0, got_w);

    nt_ui_destroy_context(ctx);
    nt_font_destroy(font);
    free(blob);
}

/* with no font slot populated the callback returns {0,0} via the
 * nt_font_valid early-return path -- proves the guard branch fires before
 * the (would-be-crashing) nt_font_measure_n call. */
/* fontId >= NT_UI_MAX_FONTS is a contract violation in CLAY_TEXT_CONFIG --
 * measure_cb asserts symmetrically with emit_text. Destroy skipped for the
 * same reason as test_measure_callback_unbound_font_asserts above. */
static void test_measure_callback_out_of_range_font_asserts(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena, sizeof s_arena, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);
    NT_TEST_EXPECT_ASSERT(declare_and_measure_text(ctx, "ABC", NT_UI_MAX_FONTS, 14U));
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    UNITY_BEGIN();
    /* Happy-path tests first so a crashing death-test doesn't hide them. */
    RUN_TEST(test_measure_callback_forwards_to_font_measure_n);
    RUN_TEST(test_measure_callback_letter_spacing_matches_render_width);
    RUN_TEST(test_measure_callback_unbound_font_asserts);
    RUN_TEST(test_measure_callback_out_of_range_font_asserts);
    return UNITY_END();
}
