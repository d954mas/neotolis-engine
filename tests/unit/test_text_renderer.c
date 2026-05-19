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
#include "time/nt_time.h"
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
static const float s_white[4] = {1.0F, 1.0F, 1.0F, 1.0F};

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
    nt_text_renderer_draw("ABC", s_identity, 32.0F, s_white);
    TEST_ASSERT_EQUAL_UINT32(3, nt_text_renderer_test_glyph_count());
}

/* ---- Test 2: UTF-8 decode Cyrillic (TEXT-03) ---- */

void test_utf8_decode_cyrillic(void) {
    /* "При" in Russian = 3 codepoints, 6 bytes */
    /* These are non-ASCII, so they won't be in our test font -> tofu glyphs */
    nt_text_renderer_draw("\xd0\x9f\xd1\x80\xd0\xb8", s_identity, 32.0F, s_white);
    /* Tofu glyphs still produce quads (has visible bbox) */
    TEST_ASSERT_EQUAL_UINT32(3, nt_text_renderer_test_glyph_count());
}

/* ---- Test 3: UTF-8 decode CJK (TEXT-03) ---- */

void test_utf8_decode_cjk(void) {
    /* "你好" = 2 codepoints, 6 bytes */
    nt_text_renderer_draw("\xe4\xbd\xa0\xe5\xa5\xbd", s_identity, 32.0F, s_white);
    TEST_ASSERT_EQUAL_UINT32(2, nt_text_renderer_test_glyph_count());
}

/* ---- Test 4: Measure returns nonzero (TEXT-02) ---- */

void test_measure_returns_nonzero(void) {
    /* "ABC" -> all in test font with advance=500, units_per_em=1000 */
    nt_text_size_t sz = nt_font_measure(s_font, "ABC", 32.0F);
    TEST_ASSERT_TRUE(sz.width > 0.0F);
    TEST_ASSERT_TRUE(sz.height > 0.0F);
}

/* ---- Test 5: Measure empty string (TEXT-02 edge) ---- */

void test_measure_empty_string(void) {
    nt_text_size_t sz = nt_font_measure(s_font, "", 32.0F);
    TEST_ASSERT_TRUE(sz.width == 0.0F);
    TEST_ASSERT_TRUE(sz.height == 0.0F);
}

/* ---- Test 6: Measure NULL string (TEXT-02 edge) ---- */

void test_measure_null_string(void) {
    nt_text_size_t sz = nt_font_measure(s_font, NULL, 32.0F);
    TEST_ASSERT_TRUE(sz.width == 0.0F);
    TEST_ASSERT_TRUE(sz.height == 0.0F);
}

/* ---- Test 7: Vertex stride is 68 bytes (TEXT-01) ---- */

void test_vertex_stride_68(void) {
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white);
    TEST_ASSERT_EQUAL_UINT32(1, nt_text_renderer_test_glyph_count());

    /* 4 vertices for one glyph, at 68 bytes stride */
    const uint8_t *verts = (const uint8_t *)nt_text_renderer_test_vertices();
    TEST_ASSERT_NOT_NULL(verts);

    /* Vertex 0 and vertex 1 should be at offsets 0 and 68 */
    /* They represent different quad corners, so position data differs */
    TEST_ASSERT_FALSE(memcmp(verts, verts + 68, 68) == 0);
}

/* ---- Test 8: 4 vertices per glyph (TEXT-01) ---- */

void test_vertex_count_4_per_glyph(void) {
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white);
    /* 2 visible glyphs -> 8 vertices */
    TEST_ASSERT_EQUAL_UINT32(8, nt_text_renderer_test_vertex_count());
}

/* ---- Test 9: Flush resets counts (TEXT-05) ---- */

void test_flush_resets_counts(void) {
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white);
    TEST_ASSERT_GREATER_THAN(0U, nt_text_renderer_test_glyph_count());

    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_text_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_text_renderer_test_glyph_count());
}

/* ---- Test 10: Measure width increases with more chars (TEXT-04) ---- */

void test_measure_width_increases(void) {
    nt_text_size_t sz_a = nt_font_measure(s_font, "A", 32.0F);
    nt_text_size_t sz_ab = nt_font_measure(s_font, "AB", 32.0F);
    TEST_ASSERT_TRUE(sz_ab.width > sz_a.width);
}

/* ---- Test 11: Newlines reset x and advance y ---- */

void test_draw_newline_advances_to_next_line(void) {
    nt_text_renderer_draw("A\nB", s_identity, 32.0F, s_white);
    TEST_ASSERT_EQUAL_UINT32(2, nt_text_renderer_test_glyph_count());

    const uint8_t *verts = (const uint8_t *)nt_text_renderer_test_vertices();
    TEST_ASSERT_NOT_NULL(verts);

    float first_x = 0.0F;
    float first_y = 0.0F;
    float second_x = 0.0F;
    float second_y = 0.0F;
    memcpy(&first_x, verts + 0, sizeof(float));
    memcpy(&first_y, verts + 4, sizeof(float));
    memcpy(&second_x, verts + ((size_t)4U * 68U), sizeof(float));
    memcpy(&second_y, verts + ((size_t)4U * 68U) + 4U, sizeof(float));

    TEST_ASSERT_TRUE(first_x == second_x);
    TEST_ASSERT_TRUE(second_y < first_y);
}

/* ---- Test 12: TEXT-01 — _draw_n produces byte-identical vertex stream to _draw ---- */

void test_draw_n_matches_draw(void) {
    /* Capture vertex stream from existing _draw on NUL-terminated "AB" */
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white);
    const uint32_t draw_vcount = nt_text_renderer_test_vertex_count();
    const uint32_t draw_gcount = nt_text_renderer_test_glyph_count();
    TEST_ASSERT_EQUAL_UINT32(8U, draw_vcount); /* 2 visible glyphs × 4 verts */
    TEST_ASSERT_EQUAL_UINT32(2U, draw_gcount);

    /* Snapshot vertex bytes — flush will zero the staging buffer counters next,
     * so we copy out before reset. Stride is 68 bytes per nt_text_vertex_t. */
    const size_t bytes_to_copy = (size_t)draw_vcount * 68U;
    uint8_t buf_draw[8U * 68U];
    memcpy(buf_draw, nt_text_renderer_test_vertices(), bytes_to_copy);

    /* Reset staging counters (no pipeline → flush warns + zeros counters). */
    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_vertex_count());

    /* Call length-aware variant with matching length */
    nt_text_renderer_draw_n("AB", 2U, s_identity, 32.0F, s_white);
    const uint32_t draw_n_vcount = nt_text_renderer_test_vertex_count();
    const uint32_t draw_n_gcount = nt_text_renderer_test_glyph_count();

    TEST_ASSERT_EQUAL_UINT32(draw_vcount, draw_n_vcount);
    TEST_ASSERT_EQUAL_UINT32(draw_gcount, draw_n_gcount);
    TEST_ASSERT_EQUAL_MEMORY(buf_draw, nt_text_renderer_test_vertices(), bytes_to_copy);
}

/* ---- Test 13: TEXT-01b — poisoned byte at utf8[len] does NOT contribute (no over-read) ---- */

void test_draw_n_does_not_over_read(void) {
    /* Reference: _draw on the clean "AB" string */
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white);
    const uint32_t ref_vcount = nt_text_renderer_test_vertex_count();
    TEST_ASSERT_EQUAL_UINT32(8U, ref_vcount);

    const size_t bytes_to_copy = (size_t)ref_vcount * 68U;
    uint8_t buf_ref[8U * 68U];
    memcpy(buf_ref, nt_text_renderer_test_vertices(), bytes_to_copy);

    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_vertex_count());

    /* Stack buffer with poisoned bytes at and past index 2. Stack-array literal
     * expresses "intentionally not NUL-terminated" without tripping
     * bugprone-not-null-terminated-result (matches test_font.c precedent). */
    const char buf[8] = {'A', 'B', 'X', 'X', 'X', 'X', 'X', 'X'};

    nt_text_renderer_draw_n(buf, 2U, s_identity, 32.0F, s_white);
    const uint32_t bounded_vcount = nt_text_renderer_test_vertex_count();

    TEST_ASSERT_EQUAL_UINT32(ref_vcount, bounded_vcount);
    TEST_ASSERT_EQUAL_MEMORY(buf_ref, nt_text_renderer_test_vertices(), bytes_to_copy);
}

/* ---- Benchmark cases (printed as [BENCH] lines; cover draw hot-loop perf) ---- */

static void bench_draw_short_warm(void) {
    /* GPU-side glyph cache and font ASCII fast-path warm via setup. We flush
     * once before timing so the renderer buffer starts empty, and flush per
     * loop iteration to keep glyph_count from saturating across 1000 draws. */
    nt_text_renderer_flush();

    const int n_calls = 1000;
    const uint64_t t0 = nt_time_nanos();
    for (int i = 0; i < n_calls; i++) {
        nt_text_renderer_draw_n("ABC", 3U, s_identity, 32.0F, s_white);
        nt_text_renderer_flush(); /* exclude buffer-overflow path from timing */
    }
    const uint64_t t1 = nt_time_nanos();

    const double per_call_ns = (double)(t1 - t0) / (double)n_calls;
    (void)printf("[BENCH] draw_short_warm (3 chars + flush): %.2f ns/call\n", per_call_ns);
    (void)fflush(stdout);
}

static void bench_draw_mixed_ui(void) {
    /* 6 hot labels each redrawn over 200 "frames" with a flush between frames.
     * Approximates a stable UI re-render path where the GPU glyph cache and
     * the ASCII fast-path are both warm. */
    const char *const labels[] = {"OK", "AB", "BC", "CA", "ABC", "BCA"};
    const size_t lens[] = {2U, 2U, 2U, 2U, 3U, 3U};
    const int label_count = (int)(sizeof(labels) / sizeof(labels[0]));

    /* Warm up the glyph cache once. */
    for (int i = 0; i < label_count; i++) {
        nt_text_renderer_draw_n(labels[i], lens[i], s_identity, 32.0F, s_white);
    }
    nt_text_renderer_flush();

    const int frames = 200;
    int total_calls = 0;
    const uint64_t t0 = nt_time_nanos();
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < label_count; i++) {
            nt_text_renderer_draw_n(labels[i], lens[i], s_identity, 32.0F, s_white);
            total_calls++;
        }
        nt_text_renderer_flush();
    }
    const uint64_t t1 = nt_time_nanos();

    const double per_call_ns = (double)(t1 - t0) / (double)total_calls;
    (void)printf("[BENCH] draw_mixed_ui (6 labels × 200 frames): %.2f ns/call (%d calls)\n", per_call_ns, total_calls);
    (void)fflush(stdout);
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
    RUN_TEST(test_vertex_stride_68);
    RUN_TEST(test_vertex_count_4_per_glyph);
    RUN_TEST(test_flush_resets_counts);
    RUN_TEST(test_measure_width_increases);
    RUN_TEST(test_draw_newline_advances_to_next_line);
    RUN_TEST(test_draw_n_matches_draw);
    RUN_TEST(test_draw_n_does_not_over_read);
    RUN_TEST(bench_draw_short_warm);
    RUN_TEST(bench_draw_mixed_ui);
    return UNITY_END();
}
