/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "unity.h"
/* clang-format on */

/* ---- Virtual pack ID counter ---- */

static uint32_t s_vpack_counter;

/* ---- Test blob builder (identical to test_font.c) ---- */

static uint8_t *build_test_font_blob(uint32_t *out_size) {
    uint32_t contour_size = 14; /* v4: cc(2)+pc(2)+flags(2)+first(4)+deltas(4) */
    uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyphs_size = 3 * (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t total_size = header_size + glyphs_size + (3 * contour_size);

    uint8_t *blob = (uint8_t *)calloc(total_size, 1);
    NT_ASSERT(blob);

    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = 3;
    hdr.units_per_em = 1000;
    hdr.ascent = 800;
    hdr.descent = -200;
    hdr.line_gap = 0;
    memcpy(blob, &hdr, sizeof(hdr));

    uint32_t data_base = header_size + glyphs_size;
    uint32_t codepoints[3] = {65, 66, 67}; /* 'A', 'B', 'C' */

    for (int g = 0; g < 3; g++) {
        NtFontGlyphEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.codepoint = codepoints[g];
        entry.data_offset = data_base + ((uint32_t)g * contour_size);
        entry.advance = 500;
        entry.bbox_x0 = 0;
        entry.bbox_y0 = -200;
        entry.bbox_x1 = 400;
        entry.bbox_y1 = 800;
        entry.curve_count = 3; /* 3 on-curve points in closed contour → 3 line segments */
        entry.kern_count = 0;
        memcpy(blob + header_size + ((size_t)g * sizeof(NtFontGlyphEntry)), &entry, sizeof(entry));
    }

    /* Write v4 contour data per glyph: contour_count, then per-contour:
     * point_count, flags_bitmask, first_point(abs), varlen_deltas */
    for (int g = 0; g < 3; g++) {
        uint8_t *wp = blob + data_base + ((size_t)g * contour_size);
        /* contour_count = 1 */
        uint16_t cc = 1;
        memcpy(wp, &cc, 2);
        wp += 2;
        /* point_count = 3 (triangle: 3 on-curve points → 3 line-segment curves) */
        uint16_t pc = 3;
        memcpy(wp, &pc, 2);
        wp += 2;
        /* flags bitmask: NT_FONT_BITMASK_BYTES(3) = 2, all on-curve → bits 0,1,2 set */
        wp[0] = 0x07;
        wp[1] = 0x00;
        wp += 2;
        /* first point absolute (0, 0) */
        int16_t fx = 0;
        int16_t fy = 0;
        memcpy(wp, &fx, 2);
        wp += 2;
        memcpy(wp, &fy, 2);
        wp += 2;
        /* delta point 1: (50, 0) — fits in int8, single byte each */
        *wp++ = 50;
        *wp++ = 0;
        /* delta point 2: (-50, 50) — fits in int8 */
        *wp++ = (uint8_t)(int8_t)-50;
        *wp++ = 50;
    }

    *out_size = total_size;
    return blob;
}

/* ---- Helper: register font blob as test resource ---- */

static nt_resource_t register_font_resource(const char *name, const uint8_t *blob, uint32_t blob_size) {
    uint32_t data_handle = nt_font_test_register_data(blob, blob_size);

    char pack_name[64];
    (void)snprintf(pack_name, sizeof(pack_name), "fp_%s_%u", name, s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t rid = nt_hash64_str(name);

    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, rid, NT_ASSET_FONT, data_handle);

    return nt_resource_request(rid, NT_ASSET_FONT);
}

/* ---- Shared test state ---- */

static uint8_t *s_blob;
static uint32_t s_blob_size;
static nt_font_t s_font;

/* ---- Default identity matrix ---- */

static const float s_identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static const float s_white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

/* ---- Unity setUp / tearDown ---- */

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)fprintf(stderr, "ASSERT FAILED: %s at %s:%d\n", expr, file, line);
    (void)fflush(stderr);
}

void setUp(void) {
    nt_assert_handler = test_assert_handler;
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 16, .max_textures = 32, .max_meshes = 8});
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    s_vpack_counter = 0;

    /* Build font and create handle */
    s_blob = build_test_font_blob(&s_blob_size);
    nt_font_create_desc_t desc = {
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 16,
        .band_count = 4,
    };
    s_font = nt_font_create(&desc);
    nt_resource_t res = register_font_resource("test_text_font", s_blob, s_blob_size);
    nt_font_add(s_font, res);
    nt_resource_step();
    nt_font_step();

    /* Init text renderer */
    nt_text_renderer_init();
    nt_text_renderer_set_font(s_font);
}

void tearDown(void) {
    nt_text_renderer_shutdown();
    nt_font_destroy(s_font);
    free(s_blob);
    s_blob = NULL;
    nt_font_shutdown();
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
    nt_gfx_shutdown();
}

/* ---- Test 1: UTF-8 decode ASCII (TEXT-03) ---- */

void test_utf8_decode_ascii(void) {
    nt_text_renderer_draw("ABC", s_identity, 32.0f, s_white);
    TEST_ASSERT_EQUAL_UINT32(3, nt_text_renderer_test_glyph_count());
}

/* ---- Test 2: UTF-8 decode Cyrillic (TEXT-03) ---- */

void test_utf8_decode_cyrillic(void) {
    /* "При" in Russian = 3 codepoints, 6 bytes */
    /* These are non-ASCII, so they won't be in our test font -> tofu glyphs */
    nt_text_renderer_draw("\xd0\x9f\xd1\x80\xd0\xb8", s_identity, 32.0f, s_white);
    /* Tofu glyphs still produce quads (has visible bbox) */
    TEST_ASSERT_EQUAL_UINT32(3, nt_text_renderer_test_glyph_count());
}

/* ---- Test 3: UTF-8 decode CJK (TEXT-03) ---- */

void test_utf8_decode_cjk(void) {
    /* "你好" = 2 codepoints, 6 bytes */
    nt_text_renderer_draw("\xe4\xbd\xa0\xe5\xa5\xbd", s_identity, 32.0f, s_white);
    TEST_ASSERT_EQUAL_UINT32(2, nt_text_renderer_test_glyph_count());
}

/* ---- Test 4: Measure returns nonzero (TEXT-02) ---- */

void test_measure_returns_nonzero(void) {
    /* "ABC" -> all in test font with advance=500, units_per_em=1000 */
    nt_text_size_t sz = nt_font_measure(s_font, "ABC", 32.0f);
    TEST_ASSERT_TRUE(sz.width > 0.0f);
    TEST_ASSERT_TRUE(sz.height > 0.0f);
}

/* ---- Test 5: Measure empty string (TEXT-02 edge) ---- */

void test_measure_empty_string(void) {
    nt_text_size_t sz = nt_font_measure(s_font, "", 32.0f);
    TEST_ASSERT_TRUE(sz.width == 0.0f);
    TEST_ASSERT_TRUE(sz.height == 0.0f);
}

/* ---- Test 6: Measure NULL string (TEXT-02 edge) ---- */

void test_measure_null_string(void) {
    nt_text_size_t sz = nt_font_measure(s_font, NULL, 32.0f);
    TEST_ASSERT_TRUE(sz.width == 0.0f);
    TEST_ASSERT_TRUE(sz.height == 0.0f);
}

/* ---- Test 7: Vertex stride is 64 bytes (TEXT-01) ---- */

void test_vertex_stride_64(void) {
    nt_text_renderer_draw("A", s_identity, 32.0f, s_white);
    TEST_ASSERT_EQUAL_UINT32(1, nt_text_renderer_test_glyph_count());

    /* 4 vertices for one glyph, at 64 bytes stride */
    const uint8_t *verts = (const uint8_t *)nt_text_renderer_test_vertices();
    TEST_ASSERT_NOT_NULL(verts);

    /* Vertex 0 and vertex 1 should be at offsets 0 and 64 */
    /* They represent different quad corners, so position data differs */
    TEST_ASSERT_FALSE(memcmp(verts, verts + 64, 64) == 0);
}

/* ---- Test 8: 4 vertices per glyph (TEXT-01) ---- */

void test_vertex_count_4_per_glyph(void) {
    nt_text_renderer_draw("AB", s_identity, 32.0f, s_white);
    /* 2 visible glyphs -> 8 vertices */
    TEST_ASSERT_EQUAL_UINT32(8, nt_text_renderer_test_vertex_count());
}

/* ---- Test 9: Flush resets counts (TEXT-05) ---- */

void test_flush_resets_counts(void) {
    nt_text_renderer_draw("A", s_identity, 32.0f, s_white);
    TEST_ASSERT_GREATER_THAN(0U, nt_text_renderer_test_glyph_count());

    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_text_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_text_renderer_test_glyph_count());
}

/* ---- Test 10: Measure width increases with more chars (TEXT-04) ---- */

void test_measure_width_increases(void) {
    nt_text_size_t sz_a = nt_font_measure(s_font, "A", 32.0f);
    nt_text_size_t sz_ab = nt_font_measure(s_font, "AB", 32.0f);
    TEST_ASSERT_TRUE(sz_ab.width > sz_a.width);
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_utf8_decode_ascii);
    RUN_TEST(test_utf8_decode_cyrillic);
    RUN_TEST(test_utf8_decode_cjk);
    RUN_TEST(test_measure_returns_nonzero);
    RUN_TEST(test_measure_empty_string);
    RUN_TEST(test_measure_null_string);
    RUN_TEST(test_vertex_stride_64);
    RUN_TEST(test_vertex_count_4_per_glyph);
    RUN_TEST(test_flush_resets_counts);
    RUN_TEST(test_measure_width_increases);
    return UNITY_END();
}
