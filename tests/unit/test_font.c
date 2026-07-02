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
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"
#include "unity.h"
/* clang-format on */

/* ---- Test blob builder ---- */

/*
 * Build a minimal valid NT_ASSET_FONT binary blob in memory.
 * Contains 3 glyphs ('A'=65, 'B'=66, 'C'=67), each with 2 line segments.
 * No kern entries. All coordinates in font design units (units_per_em=1000).
 *
 * Binary layout per font_format.h:
 *   NtFontAssetHeader (16 bytes)
 *   NtFontGlyphEntry[3] (24 bytes each = 72 bytes)
 *   Per-glyph contour data blocks (variable)
 */
static uint8_t *build_test_font_blob_with_metrics(uint16_t units_per_em, int16_t ascent, int16_t descent, int16_t line_gap, uint32_t *out_size);

static uint8_t *build_test_font_blob(uint32_t *out_size) { return build_test_font_blob_with_metrics(1000, 800, -200, 0, out_size); }

static uint8_t *build_test_font_blob_with_metrics(uint16_t units_per_em, int16_t ascent, int16_t descent, int16_t line_gap, uint32_t *out_size) {
    /* Pre-calculate contour data size per glyph:
     * 1 contour with 2 line segments each
     *   contour_count = 1 (2 bytes)
     *   segment_count = 2 (2 bytes)
     *   start_x, start_y (4 bytes)
     *   type_bits: ceil(2/8) = 1 byte, padded to 2 bytes
     *   2 line segments: 4 bytes each = 8 bytes
     *   Total per glyph = 2 + 2 + 4 + 2 + 8 = 18 bytes
     */
    uint32_t contour_size = 18;
    uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyphs_size = 3 * (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t total_size = header_size + glyphs_size + (3 * contour_size);

    uint8_t *blob = (uint8_t *)calloc(total_size, 1);
    NT_ASSERT(blob);

    /* Write header */
    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = 3;
    hdr.units_per_em = units_per_em;
    hdr.ascent = ascent;
    hdr.descent = descent;
    hdr.line_gap = line_gap;
    memcpy(blob, &hdr, sizeof(hdr));

    /* Glyph entries (sorted by codepoint) */
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
        entry.curve_count = 2;
        entry.kern_count = 0;
        memcpy(blob + header_size + ((size_t)g * sizeof(NtFontGlyphEntry)), &entry, sizeof(entry));
    }

    /* Per-glyph contour data: 1 contour with 2 line segments forming a simple shape */
    for (int g = 0; g < 3; g++) {
        uint8_t *wp = blob + data_base + ((size_t)g * contour_size);

        /* contour_count = 1 */
        uint16_t cc = 1;
        memcpy(wp, &cc, 2);
        wp += 2;

        /* segment_count = 2 */
        uint16_t sc = 2;
        memcpy(wp, &sc, 2);
        wp += 2;

        /* start_x, start_y (bottom-left corner) */
        int16_t sx = 0;
        int16_t sy = 0;
        memcpy(wp, &sx, 2);
        wp += 2;
        memcpy(wp, &sy, 2);
        wp += 2;

        /* type_bits: 2 lines (bit=0), padded to 2 bytes */
        wp[0] = 0;
        wp[1] = 0;
        wp += 2;

        /* Line segment 1: delta to (400, 0) -> dp2x=400, dp2y=0 */
        int16_t d1x = 400;
        int16_t d1y = 0;
        memcpy(wp, &d1x, 2);
        wp += 2;
        memcpy(wp, &d1y, 2);
        wp += 2;

        /* Line segment 2: delta to (400, 800) from (400, 0) -> dp2x=0, dp2y=800 */
        int16_t d2x = 0;
        int16_t d2y = 800;
        memcpy(wp, &d2x, 2);
        wp += 2;
        memcpy(wp, &d2y, 2);
    }

    *out_size = total_size;
    return blob;
}

/* Build a font blob over a SORTED codepoint set (bsearch precondition). Every glyph shares `advance`,
 * so a resolved glyph's advance identifies WHICH resource won a shared codepoint (first-wins). */
static uint8_t *build_font_blob_codepoints(uint16_t units_per_em, int16_t ascent, int16_t descent, int16_t line_gap, const uint32_t *codepoints, uint16_t glyph_count, int16_t advance,
                                           uint32_t *out_size) {
    const uint32_t contour_size = 18U; /* 1 contour, 2 line segments — matches build_test_font_blob_with_metrics */
    uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyphs_size = (uint32_t)glyph_count * (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t total_size = header_size + glyphs_size + ((uint32_t)glyph_count * contour_size);

    uint8_t *blob = (uint8_t *)calloc(total_size, 1);
    NT_ASSERT(blob);

    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = glyph_count;
    hdr.units_per_em = units_per_em;
    hdr.ascent = ascent;
    hdr.descent = descent;
    hdr.line_gap = line_gap;
    memcpy(blob, &hdr, sizeof(hdr));

    uint32_t data_base = header_size + glyphs_size;
    for (uint16_t g = 0; g < glyph_count; g++) {
        NtFontGlyphEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.codepoint = codepoints[g];
        entry.data_offset = data_base + ((uint32_t)g * contour_size);
        entry.advance = advance;
        entry.bbox_x0 = 0;
        entry.bbox_y0 = -200;
        entry.bbox_x1 = 400;
        entry.bbox_y1 = 800;
        entry.curve_count = 2;
        entry.kern_count = 0;
        memcpy(blob + header_size + ((size_t)g * sizeof(NtFontGlyphEntry)), &entry, sizeof(entry));
    }

    for (uint16_t g = 0; g < glyph_count; g++) {
        uint8_t *wp = blob + data_base + ((size_t)g * contour_size);
        uint16_t cc = 1;
        memcpy(wp, &cc, 2);
        wp += 2;
        uint16_t sc = 2;
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

/* ---- Helper: register a font blob as a test resource ----
 * Fonts read zero-copy from a resident pack blob (virtual packs carry none), so register_data wraps
 * the blob in a real parsed pack (freed on shutdown) and returns a token; resource() requests it. */
static nt_resource_t register_font_resource(const char *name, const uint8_t *blob, uint32_t blob_size) {
    (void)name;
    return nt_font_test_resource(nt_font_test_register_data(blob, blob_size));
}

/* ---- Default create descriptor for tests ---- */

static nt_font_create_desc_t test_font_desc(void) {
    return (nt_font_create_desc_t){
        .curve_texture_width = 64, .curve_texture_height = 64, .band_texture_height = 16, .band_count = 4, .measure_cache_size = 256, /* match v1.7 default; FONT-02 cases assert against this */
    };
}

/* ---- Unity setUp / tearDown ---- */

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)fprintf(stderr, "ASSERT FAILED: %s at %s:%d\n", expr, file, line);
    (void)fflush(stderr);
}

void setUp(void) {
    nt_assert_handler = test_assert_handler;
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 8, .max_textures = 32, .max_meshes = 8});
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
}

void tearDown(void) {
    nt_font_shutdown(); /* frees the test-only pack wrappers it owns */
    nt_resource_shutdown();
    nt_hash_shutdown();
    nt_gfx_shutdown();
}

/* ---- Test 0: Blob self-test ---- */

void test_font_blob_valid(void) {
    uint32_t size = 0;
    uint8_t *blob = build_test_font_blob(&size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_GREATER_THAN(0U, size);

    /* Verify magic */
    uint32_t magic = 0;
    memcpy(&magic, blob, 4);
    TEST_ASSERT_EQUAL_UINT32(NT_FONT_MAGIC, magic);

    /* Verify version */
    uint16_t version = 0;
    memcpy(&version, blob + 4, 2);
    TEST_ASSERT_EQUAL_UINT16(NT_FONT_VERSION, version);

    /* Verify glyph count */
    uint16_t glyph_count = 0;
    memcpy(&glyph_count, blob + 6, 2);
    TEST_ASSERT_EQUAL_UINT16(3, glyph_count);

    free(blob);
}

/* ---- Test 1: Init / Shutdown lifecycle (FONT-01) ---- */

void test_font_init_shutdown(void) {
    /* setUp already called init -- shutdown and re-init */
    nt_font_shutdown();
    nt_result_t r = nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    TEST_ASSERT_EQUAL(NT_OK, r);
    /* tearDown will call shutdown */
}

/* ---- Test 2: Create / Destroy / Valid (FONT-01) ---- */

void test_font_create_destroy_valid(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL(0U, font.id);
    TEST_ASSERT_TRUE(nt_font_valid(font));

    nt_font_destroy(font);
    TEST_ASSERT_FALSE(nt_font_valid(font));
}

/* ---- Test 3: Add resource (FONT-04) ---- */

void test_font_add_resource(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("test_font_add", blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step(); /* trigger resolve so nt_resource_get returns handle */
    nt_font_step();     /* resolve resource and parse metrics */

    nt_font_metrics_t m = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_INT16(800, m.ascent);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Test 4: Get metrics (FONT-07) ---- */

void test_font_get_metrics(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("test_font_metrics", blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_metrics_t m = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_INT16(800, m.ascent);
    TEST_ASSERT_EQUAL_INT16(-200, m.descent);
    TEST_ASSERT_EQUAL_INT16(0, m.line_gap);
    TEST_ASSERT_EQUAL_UINT16(1000, m.units_per_em);
    TEST_ASSERT_EQUAL_INT16(1000, m.line_height); /* 800 - (-200) + 0 = 1000 */

    nt_font_destroy(font);
    free(blob);
}

/* ---- Test 5: Lookup glyph hit (FONT-05) ---- */

void test_font_lookup_glyph_hit(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("test_font_lookup", blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    const nt_glyph_cache_entry_t *e = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32('A', e->codepoint);
    TEST_ASSERT_EQUAL_INT16(500, e->advance);
    TEST_ASSERT_FALSE(e->is_tofu);

    /* Second lookup should hit cache */
    const nt_glyph_cache_entry_t *e2 = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(e2);
    TEST_ASSERT_EQUAL_UINT32('A', e2->codepoint);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Test 6: Lookup glyph miss returns tofu (FONT-06) ---- */

void test_font_lookup_glyph_miss_tofu(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("test_font_tofu", blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* 'Z' is not in our test blob (only A, B, C) */
    const nt_glyph_cache_entry_t *e = nt_font_lookup_glyph(font, 'Z');
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_TRUE(e->is_tofu);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Test 7: Get stats (FONT-02, FONT-03) ---- */

void test_font_get_stats(void) {
    nt_font_create_desc_t desc = {
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 8,
        .band_count = 4,
    };
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("test_font_stats", blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* Lookup 2 glyphs to populate cache */
    nt_font_lookup_glyph(font, 'A');
    nt_font_lookup_glyph(font, 'B');

    nt_font_stats_t s = nt_font_get_stats(font);
    /* 'A' + 'B' + tofu (generated on first miss check before 'A') = varies */
    TEST_ASSERT_GREATER_OR_EQUAL(2, s.glyphs_cached);
    TEST_ASSERT_EQUAL_UINT16(8, s.max_glyphs);
    TEST_ASSERT_GREATER_THAN(0U, s.curve_texels_used);
    TEST_ASSERT_EQUAL_UINT32(64 * 64, s.curve_texels_total);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Test 8: LRU eviction (FONT-02) ---- */

void test_font_lru_eviction(void) {
    /* Small cache: max_glyphs = 4, so A + B + C + tofu = full */
    nt_font_create_desc_t desc = {
        .curve_texture_width = 128,
        .curve_texture_height = 128,
        .band_texture_height = 4,
        .band_count = 4,
    };
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("test_font_evict", blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* Fill cache: A, B, C (plus tofu auto-generated) = 4 slots */
    nt_font_lookup_glyph(font, 'A');
    nt_font_lookup_glyph(font, 'B');
    nt_font_lookup_glyph(font, 'C');

    nt_font_stats_t s1 = nt_font_get_stats(font);
    TEST_ASSERT_LESS_OR_EQUAL(4, s1.glyphs_cached);

    /* Build second blob with glyph 'D' */
    /* Re-use the same blob but with a different glyph -- build a minimal blob manually */
    uint32_t contour_size = 18;
    uint32_t hdr_sz = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyph_sz = (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t total = hdr_sz + glyph_sz + contour_size;
    uint8_t *blob2 = (uint8_t *)calloc(total, 1);

    NtFontAssetHeader hdr2;
    memset(&hdr2, 0, sizeof(hdr2));
    hdr2.magic = NT_FONT_MAGIC;
    hdr2.version = NT_FONT_VERSION;
    hdr2.glyph_count = 1;
    hdr2.units_per_em = 1000;
    hdr2.ascent = 800;
    hdr2.descent = -200;
    hdr2.line_gap = 0;
    memcpy(blob2, &hdr2, sizeof(hdr2));

    NtFontGlyphEntry entry_d;
    memset(&entry_d, 0, sizeof(entry_d));
    entry_d.codepoint = 68; /* 'D' */
    entry_d.data_offset = hdr_sz + glyph_sz;
    entry_d.advance = 550;
    entry_d.bbox_x0 = 0;
    entry_d.bbox_y0 = -200;
    entry_d.bbox_x1 = 450;
    entry_d.bbox_y1 = 800;
    entry_d.curve_count = 2;
    entry_d.kern_count = 0;
    memcpy(blob2 + hdr_sz, &entry_d, sizeof(entry_d));

    /* Contour data for 'D' (same structure as test blob) */
    uint8_t *wp2 = blob2 + hdr_sz + glyph_sz;
    uint16_t cc2 = 1;
    memcpy(wp2, &cc2, 2);
    wp2 += 2;
    uint16_t sc2 = 2;
    memcpy(wp2, &sc2, 2);
    wp2 += 2;
    int16_t sx2 = 0;
    int16_t sy2 = 0;
    memcpy(wp2, &sx2, 2);
    wp2 += 2;
    memcpy(wp2, &sy2, 2);
    wp2 += 2;
    wp2[0] = 0;
    wp2[1] = 0;
    wp2 += 2;
    int16_t dd1x = 450;
    int16_t dd1y = 0;
    memcpy(wp2, &dd1x, 2);
    wp2 += 2;
    memcpy(wp2, &dd1y, 2);
    wp2 += 2;
    int16_t dd2x = 0;
    int16_t dd2y = 800;
    memcpy(wp2, &dd2x, 2);
    wp2 += 2;
    memcpy(wp2, &dd2y, 2);

    nt_resource_t res2 = register_font_resource("test_font_evict_d", blob2, total);
    nt_font_add(font, res2);
    nt_resource_step();
    nt_font_step();

    /* Lookup 'D' should trigger eviction */
    const nt_glyph_cache_entry_t *e = nt_font_lookup_glyph(font, 'D');
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32('D', e->codepoint);
    TEST_ASSERT_FALSE(e->is_tofu);

    nt_font_stats_t s2 = nt_font_get_stats(font);
    TEST_ASSERT_LESS_OR_EQUAL(4, s2.glyphs_cached);

    nt_font_destroy(font);
    free(blob);
    free(blob2);
}

/* ---- Helpers for FONT-01 / FONT-02 tests ----
 *
 * Unity's TEST_ASSERT_EQUAL_FLOAT is compiled out via UNITY_EXCLUDE_FLOAT
 * (matches test_stats.c pattern). Identity-comparing nt_text_size_t is safe
 * because:
 *   - cache hits return the EXACT bytes stored on miss → bit-identical
 *   - miss-path on identical input is deterministic (same float math, same order)
 * We use bit-exact memcmp on the struct. */
static void assert_text_size_equal(nt_text_size_t expected, nt_text_size_t actual) { TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(expected)); }

/* Build a fully-resolved test font (cache + metrics ready). Caller frees blob. */
static nt_font_t make_resolved_test_font(const char *name, uint8_t **out_blob) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource(name, blob, blob_size);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    *out_blob = blob;
    return font;
}

/* ---- FONT-01: nt_font_measure_n matches nt_font_measure on NUL-equivalent input ---- */

void test_measure_n_matches_measure(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_eq", &blob);

    nt_text_size_t a = nt_font_measure(font, "ABC", 14.0F, 0.0F);
    nt_text_size_t b = nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
    assert_text_size_equal(a, b);

    /* Empty input contract */
    nt_text_size_t zero = {0.0F, 0.0F};
    nt_text_size_t e = nt_font_measure_n(font, "ABC", 0U, 14.0F, 0.0F);
    assert_text_size_equal(zero, e);

    /* NULL guard */
    nt_text_size_t n = nt_font_measure_n(font, NULL, 4U, 14.0F, 0.0F);
    assert_text_size_equal(zero, n);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-01b: nt_font_measure_n bounded by len even with poisoned byte at utf8[len] ---- */

void test_measure_n_does_not_over_read(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_bound", &blob);

    /* "ABC" + poison 'B' + filler; bounded measure must ignore the poison.
       Intentionally NOT NUL-terminated — tests that _n stops at len, not at NUL. */
    const char buf[8] = {'A', 'B', 'C', 'B', 'X', 'X', 'X', 'X'};
    nt_text_size_t bounded = nt_font_measure_n(font, buf, 3U, 14.0F, 0.0F);
    nt_text_size_t reference = nt_font_measure(font, "ABC", 14.0F, 0.0F);

    /* If _n over-read into the poison 'B', bounded.width would exceed reference.width. */
    assert_text_size_equal(reference, bounded);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-01c: UTF-8 multibyte sequence cut by len → dropped via NT_UTF8_REJECT ---- */

void test_measure_n_drops_partial_utf8(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_utf8", &blob);

    /* "A" + 0xC3 (first byte of a 2-byte UTF-8 sequence) — len = 2 stops
     * mid-multibyte. The UTF-8 state machine recovers via NT_UTF8_REJECT
     * and the loop exits with only 'A' measured. */
    const char partial[] = {'A', (char)0xC3, 0};
    nt_text_size_t bounded = nt_font_measure_n(font, partial, 2U, 14.0F, 0.0F);
    nt_text_size_t reference = nt_font_measure(font, "A", 14.0F, 0.0F);

    assert_text_size_equal(reference, bounded);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-01d: embedded NUL inside [utf8, utf8+len) is NOT a terminator ----
 *
 * Documents and locks the semantic divergence between nt_font_measure_n
 * (length-bounded, embedded NUL = tofu glyph) and nt_font_measure (NUL-
 * terminated wrapper, stops at first NUL via strlen). Migrating callers
 * to _n with sizeof(literal) MUST be aware of this. */
void test_measure_n_embedded_nul_is_codepoint(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_nul", &blob);

    /* "A\0B" — 3 bytes. _n should measure A + tofu(0) + B; _measure (wrapper)
     * stops at the embedded NUL and measures only A. The two MUST differ. */
    const char with_nul[3] = {'A', '\0', 'B'};
    nt_text_size_t n_full = nt_font_measure_n(font, with_nul, 3U, 14.0F, 0.0F);
    nt_text_size_t wrapper_truncated = nt_font_measure(font, with_nul, 14.0F, 0.0F);
    nt_text_size_t reference_a = nt_font_measure(font, "A", 14.0F, 0.0F);

    /* Wrapper stopped at the NUL: width matches a bare "A". */
    assert_text_size_equal(reference_a, wrapper_truncated);

    /* _n consumed all 3 bytes: width must be strictly greater than "A" alone
     * (added at least the 'B' glyph's advance + the NUL's tofu advance). */
    TEST_ASSERT_TRUE(n_full.width > reference_a.width);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02: 200 identical calls produce 199 hits + 1 miss ---- */

/* letter_spacing adds (N-1) * spacing pixels for N visible glyphs.
 * 0 spacing must match the baseline; non-zero spacing widens the result by
 * the expected amount; bypassing the measure cache when spacing != 0.
 * Integer-truncated compare keeps the test free of UNITY float macros
 * (UNITY_EXCLUDE_FLOAT is set repo-wide). */
void test_measure_n_letter_spacing_grows_width(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_ls", &blob);

    /* 3 glyphs ABC: 2 gaps between them -> +10px width with spacing=5. */
    nt_text_size_t baseline = nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
    nt_text_size_t spaced = nt_font_measure_n(font, "ABC", 3U, 14.0F, 5.0F);
    const int32_t base_w = (int32_t)baseline.width;
    const int32_t sp_w = (int32_t)spaced.width;
    TEST_ASSERT_EQUAL_INT32(base_w + 10, sp_w);
    /* Height unaffected by horizontal spacing. */
    TEST_ASSERT_EQUAL_INT32((int32_t)baseline.height, (int32_t)spaced.height);

    /* Single-glyph: 0 gaps -- letter_spacing has no effect. */
    nt_text_size_t one_base = nt_font_measure_n(font, "A", 1U, 14.0F, 0.0F);
    nt_text_size_t one_spaced = nt_font_measure_n(font, "A", 1U, 14.0F, 5.0F);
    TEST_ASSERT_EQUAL_INT32((int32_t)one_base.width, (int32_t)one_spaced.width);

    nt_font_destroy(font);
    free(blob);
}

/* \r must be skipped (matches nt_text_renderer_draw_n). CRLF text from
 * Clay tokenization can leave \r in slices; measure must not add tofu width. */
void test_measure_n_skips_carriage_return(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_cr", &blob);

    nt_text_size_t plain = nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
    nt_text_size_t with_cr = nt_font_measure_n(font, "ABC\r", 4U, 14.0F, 0.0F);

    TEST_ASSERT_EQUAL_INT32((int32_t)plain.width, (int32_t)with_cr.width);

    nt_font_destroy(font);
    free(blob);
}

void test_measure_n_cache_hits_on_repeat(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_hits", &blob);

    nt_font_measure_invalidate(font);
    nt_font_test_reset_measure_counters();

    for (int i = 0; i < 200; i++) {
        (void)nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
    }

    TEST_ASSERT_EQUAL_UINT32(199U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02b: invalidate_cache resets — next call is a miss ---- */

void test_measure_n_invalidate_forces_miss(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_inv", &blob);

    nt_font_measure_invalidate(font);
    nt_font_test_reset_measure_counters();

    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F); /* miss */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F); /* hit */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    nt_font_measure_invalidate_cache();
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);                  /* miss again after invalidate */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));   /* unchanged */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_font_test_measure_cache_misses(font)); /* +1 */

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02c: nt_font_destroy(font) clears cache (slot recycled = empty cache) ---- */

void test_measure_n_destroy_clears_cache(void) {
    uint8_t *blob_a = NULL;
    nt_font_t font_a = make_resolved_test_font("font_d_a", &blob_a);

    nt_font_test_reset_measure_counters();
    (void)nt_font_measure_n(font_a, "ABC", 3U, 14.0F, 0.0F); /* warm slot */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font_a));

    /* Destroy releases pool slot via memset(slot, 0, sizeof(*slot)); next
     * create may reuse the same physical slot index. */
    nt_font_destroy(font_a);
    free(blob_a);

    uint8_t *blob_b = NULL;
    nt_font_t font_b = make_resolved_test_font("font_d_b", &blob_b);

    nt_font_test_reset_measure_counters();
    (void)nt_font_measure_n(font_b, "ABC", 3U, 14.0F, 0.0F);
    /* Must be a miss — slot's cache was cleared on destroy. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_font_test_measure_cache_hits(font_b));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font_b));

    nt_font_destroy(font_b);
    free(blob_b);
}

/* ---- FONT-02d: measure_cache_size = 0 disables cache entirely ---- */

void test_measure_n_cache_disabled(void) {
    nt_font_create_desc_t desc = test_font_desc();
    desc.measure_cache_size = 0; /* explicit: cache disabled */
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("font_cache_off", blob, blob_size);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_test_reset_measure_counters();

    /* 100 identical calls. With cache disabled, every call is a full measure
     * AND neither hit nor miss counter is incremented (counters track cache
     * activity, not raw measure calls). Result must still be correct. */
    nt_text_size_t first = nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
    for (int i = 0; i < 99; i++) {
        nt_text_size_t r = nt_font_measure_n(font, "ABC", 3U, 14.0F, 0.0F);
        assert_text_size_equal(first, r);
    }
    TEST_ASSERT_EQUAL_UINT32(0U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_font_test_measure_cache_misses(font));

    /* Invalidate is a no-op when disabled — must not crash. */
    nt_font_measure_invalidate(font);
    nt_font_measure_invalidate_cache();

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02f: adding a font resource invalidates the measure cache ----
 *
 * Regression for the async fallback-chain bug: if a font handle measures a
 * string before all resources are attached (so some glyphs hit the tofu
 * fallback), the result gets cached. Once a real resource arrives, the
 * cache MUST be cleared so the next measure picks up the real glyph metrics.
 * Driven by ascii_index_dirty in nt_font_step. */
void test_measure_n_invalidates_on_resource_change(void) {
    uint8_t *blob_a = NULL;
    nt_font_t font = make_resolved_test_font("font_async_a", &blob_a);

    nt_font_test_reset_measure_counters();

    /* Warm the cache for the same string twice — first miss, then hit. */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    /* Attach a second resource (independent blob). nt_font_step will see a
     * new resource handle and mark ascii_index_dirty — which must also
     * clear the measure cache. */
    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_test_font_blob(&blob_size_b);
    nt_resource_t res_b = register_font_resource("font_async_b", blob_b, blob_size_b);
    nt_font_add(font, res_b);
    nt_resource_step();
    nt_font_step();

    /* Re-measure the same string — without the cache invalidation fix this
     * is a HIT (counter unchanged). With the fix it's a MISS (counter +1). */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));   /* unchanged */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_font_test_measure_cache_misses(font)); /* +1 */

    nt_font_destroy(font);
    free(blob_a);
    free(blob_b);
}

/* ---- Adding an ALREADY-published resource forces a rescan ----
 * Its epoch was already consumed, so the epoch gate stays closed; only the resource-set
 * dirty flag can force the rescan that flushes the measure cache (next measure MISSes). */
void test_font_add_already_published_forces_rescan(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t size_a = 0;
    uint8_t *blob_a = build_test_font_blob(&size_a);
    nt_resource_t res_a = register_font_resource("font_pub_a", blob_a, size_a);
    nt_font_add(font, res_a);
    nt_resource_step();
    nt_font_step();

    /* Publish res_b AND let the font module consume that epoch WITHOUT adding
     * res_b to the font — now epoch == last_resolve_epoch. */
    uint32_t size_b = 0;
    uint8_t *blob_b = build_test_font_blob(&size_b);
    nt_resource_t res_b = register_font_resource("font_pub_b", blob_b, size_b);
    nt_resource_step();
    nt_font_step();

    nt_font_test_reset_measure_counters();
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    /* Add the already-published res_b with NO intervening nt_resource_step, so
     * the publication epoch does not move. Only the rescan flag can pick it up. */
    nt_font_add(font, res_b);
    nt_font_step();

    /* rescan flushes the measure cache -> the next measure MISSes (the publication
     * epoch did not move, so only the rescan flag can invalidate the cache) */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(2U, nt_font_test_measure_cache_misses(font));

    nt_font_destroy(font);
    free(blob_a);
    free(blob_b);
}

/* ---- FONT-02e: custom measure_cache_size (64 entries) ---- */

void test_measure_n_cache_custom_size(void) {
    nt_font_create_desc_t desc = test_font_desc();
    desc.measure_cache_size = 64; /* non-default, still POT */
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    nt_resource_t res = register_font_resource("font_cache_64", blob, blob_size);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_test_reset_measure_counters();

    /* 200 identical calls → 1 miss + 199 hits, just like the default-size test.
     * Verifies that a non-default POT size still produces correct counters. */
    for (int i = 0; i < 200; i++) {
        (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    }
    TEST_ASSERT_EQUAL_UINT32(199U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02g: full pack unmount clears stale handle + metrics ----
 *
 * After unmount, measure_n short-circuits at !metrics_set and returns
 * {0,0}. The cache lookup is never reached, so counters stay frozen —
 * that's the signature of both the resource_handles cleanup and the
 * metrics reset firing. */
void test_measure_n_invalidates_on_resource_unload(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    /* Keep the token so we can unmount the pack. */
    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    uint32_t tok = nt_font_test_register_data(blob, blob_size);
    nt_resource_t res = nt_font_test_resource(tok);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_test_reset_measure_counters();

    /* Warm cache: measure twice → 1 miss + 1 hit. */
    nt_text_size_t live = nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));
    TEST_ASSERT_TRUE(live.width > 0.0F);

    nt_font_metrics_t pre = nt_font_get_metrics(font);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, pre.units_per_em);

    nt_font_test_deactivate(tok);
    nt_resource_step();
    nt_font_step();

    nt_font_metrics_t post = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_UINT16(0U, post.units_per_em);
    TEST_ASSERT_EQUAL_INT16(0, post.ascent);
    TEST_ASSERT_EQUAL_INT16(0, post.descent);

    /* Counters frozen — measure_n hit the !metrics_set short-circuit
     * before the cache lookup. (UNITY_EXCLUDE_FLOAT, so TEST_ASSERT_TRUE
     * for the zero check.) */
    nt_text_size_t after = nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_TRUE(after.width == 0.0F);
    TEST_ASSERT_TRUE(after.height == 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02h: remount after full unmount recovers a working font ----
 *
 * Guards the invariant that nt_font_add stores resources[ri] permanently
 * (it survives unmount), so a later remount under the same pid+rid
 * re-resolves through the same slot without re-adding. */
void test_font_recovers_after_remount(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    uint32_t tok = nt_font_test_register_data(blob, blob_size);
    nt_resource_t res = nt_font_test_resource(tok);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);

    nt_font_test_deactivate(tok);
    nt_resource_step();
    nt_font_step();
    TEST_ASSERT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);

    /* Remount under the SAME (pid, rid) without re-adding to the font — the
     * epoch-gated step must re-resolve the provider through slot->resources[ri]. */
    nt_font_test_reregister(tok, blob, blob_size);
    nt_resource_step();
    nt_font_step();

    nt_font_metrics_t recovered = nt_font_get_metrics(font);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, recovered.units_per_em);
    nt_text_size_t m = nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_TRUE(m.width > 0.0F);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02i: single-step hot-swap with different metrics ----
 *
 * Unmount + register-different + step, with no intervening font_step.
 * From step's view this is a plain reload (handle transition, not
 * 0→N); single-provider mismatch must accept the new metrics rather
 * than trip the shared-metrics assert. */
void test_font_hotswap_replaces_metrics_in_one_step(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t size_a = 0;
    uint8_t *blob_a = build_test_font_blob(&size_a);
    uint32_t tok = nt_font_test_register_data(blob_a, size_a);
    nt_resource_t res = nt_font_test_resource(tok);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_metrics_t m_a = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_UINT16(1000U, m_a.units_per_em);
    TEST_ASSERT_EQUAL_INT16(800, m_a.ascent);

    uint32_t gen_before = nt_font_get_cache_generation(font);

    /* reregister = unmount old + mount new under the same (pid, rid) with NO
     * intervening font_step. Single-provider metrics mismatch -> hot-swap: accept
     * the new metrics + flush, no shared-metrics assert. */
    uint32_t size_b = 0;
    uint8_t *blob_b = build_test_font_blob_with_metrics(2048, 1600, -400, 0, &size_b);
    nt_font_test_reregister(tok, blob_b, size_b);
    nt_resource_step();
    nt_font_step();

    nt_font_metrics_t m_b = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_UINT16(2048U, m_b.units_per_em);
    TEST_ASSERT_EQUAL_INT16(1600, m_b.ascent);
    TEST_ASSERT_EQUAL_INT16(-400, m_b.descent);
    TEST_ASSERT_TRUE(nt_font_get_cache_generation(font) > gen_before);

    nt_font_destroy(font);
    free(blob_a);
    free(blob_b);
}

/* ---- FONT-02j: same-metrics winner-swap flushes glyph/measure caches + ASCII index ----
 *
 * A hot-reload (or higher-priority override) to a DIFFERENT blob with IDENTICAL header metrics
 * repoints the provider and bumps the resolve epoch, but presence and metrics are unchanged. Without
 * winner-identity tracking, font_step's presence + metrics checks both pass and the glyph/measure
 * caches keep serving the OLD blob's advances (and the ASCII index maps to the old glyph table).
 * The published winner handle (unique per font_activate) is the identity token that catches this. */
void test_font_flushes_on_same_metrics_winner_swap(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    const uint32_t cps[] = {(uint32_t)'A'};
    uint32_t size_a = 0;
    uint8_t *blob_a = build_font_blob_codepoints(1000, 800, -200, 0, cps, 1U, 500, &size_a);
    uint32_t tok = nt_font_test_register_data(blob_a, size_a);
    nt_resource_t res = nt_font_test_resource(tok);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* Cache 'A' (glyph + measure) against the first winner: advance 500. */
    const nt_glyph_cache_entry_t *a1 = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(a1);
    TEST_ASSERT_EQUAL_INT16(500, a1->advance);
    nt_text_size_t m1 = nt_font_measure(font, "A", 1000.0F, 0.0F);
    uint32_t gen_before = nt_font_get_cache_generation(font);

    /* Hot-reload the SAME (pid, rid) with IDENTICAL metrics but advance 700 — one step, no intervening
     * font_step. Presence stays true and metrics match; only the winner handle changes. */
    uint32_t size_b = 0;
    uint8_t *blob_b = build_font_blob_codepoints(1000, 800, -200, 0, cps, 1U, 700, &size_b);
    nt_font_test_reregister(tok, blob_b, size_b);
    nt_resource_step();
    nt_font_step();

    /* Metrics unchanged -> the swap is invisible to the presence + metrics checks. */
    TEST_ASSERT_EQUAL_UINT16(1000U, nt_font_get_metrics(font).units_per_em);

    /* Identity-driven flush: glyph cache re-decodes (ASCII fast-path -> new glyph table) and the
     * measure cache is cleared, so both report the new advance. Without the fix these stay 500. */
    TEST_ASSERT_TRUE(nt_font_get_cache_generation(font) > gen_before);
    const nt_glyph_cache_entry_t *a2 = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(a2);
    TEST_ASSERT_EQUAL_INT16(700, a2->advance);
    nt_text_size_t m2 = nt_font_measure(font, "A", 1000.0F, 0.0F);
    TEST_ASSERT_TRUE(m2.width != m1.width);

    nt_font_destroy(font);
    free(blob_a);
    free(blob_b);
}

/* ---- FONT-02k: pack unmount clears stale provider + metrics ----
 *
 * Unmount drops the winner; the resolve pass fires font_on_cleanup
 * (frees the {blob,size} holder) and the epoch-gated font_step then flushes the
 * glyph cache and resets metrics. After unmount + a resource/font step the font
 * has no provider and measure_n short-circuits at !metrics_set. */
void test_font_file_pack_unmount_cleans_state(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    uint32_t tok = nt_font_test_register_data(blob, blob_size);
    nt_resource_t res = nt_font_test_resource(tok);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_test_reset_measure_counters();
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    uint32_t gen_before = nt_font_get_cache_generation(font);

    nt_font_test_deactivate(tok);
    nt_resource_step(); /* resolve -> font_on_cleanup, provider lost */
    nt_font_step();     /* epoch changed -> flush + clear metrics */

    TEST_ASSERT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);
    TEST_ASSERT_TRUE(nt_font_get_cache_generation(font) > gen_before);

    nt_text_size_t after = nt_font_measure_n(font, "AB", 2U, 14.0F, 0.0F);
    TEST_ASSERT_TRUE(after.width == 0.0F);
    TEST_ASSERT_TRUE(after.height == 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));   /* unchanged */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font)); /* unchanged */

    nt_font_destroy(font);
    free(blob);
}

/* ---- Present-but-truncated winner swap clears the provider (no stale blob read) ----
 * A swap to a resident-but-truncated blob (< header) moved the pin off the old pack, so keeping the
 * old provider reads now-unpinned bytes. font_on_resolve must clear it (not return like evicted-blob).
 * The old pack stays mounted so the stale read is deterministic: without the fix units_per_em stays. */
void test_font_truncated_winner_swap_clears_provider(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t size_a = 0;
    uint8_t *blob_a = build_test_font_blob(&size_a);
    uint32_t tok_a = nt_font_test_register_data(blob_a, size_a);
    nt_resource_t res = nt_font_test_resource(tok_a);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* Warm a lookup against the valid winner. */
    const nt_glyph_cache_entry_t *a1 = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(a1);
    TEST_ASSERT_FALSE(a1->is_tofu);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);

    /* Swap the winner to a resident TRUNCATED blob (< header) in a SECOND pack sharing the rid; the
     * later mount wins the tiebreak and the pin moves to it. Pack A stays mounted (its blob alive), so
     * a stale provider would still read valid old bytes — the fix must clear it regardless. */
    uint8_t truncated[sizeof(NtFontAssetHeader) - 1] = {0};
    uint32_t tok_b = nt_font_test_register_shared_rid(tok_a, truncated, (uint32_t)sizeof(truncated));
    (void)tok_b;
    nt_resource_step();
    nt_font_step();

    /* Provider cleared -> no active source -> metrics reset (mirrors the unmount path). Without the fix
     * the stale provider keeps viewing pack A's live blob and units_per_em stays non-zero. */
    TEST_ASSERT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);

    nt_font_destroy(font);
    free(blob_a);
}

/* ---- FONT-03: unmount a font's pack WHILE it is referenced ----
 *
 * The font reads glyph bytes zero-copy from the pinned pack blob, then the pack
 * is unmounted while the pin is held. The unmount overrides the pin, the resolve
 * pass frees the holder exactly once (no double-free), and the font degrades
 * cleanly — no dangling read. */
void test_font_unmount_while_referenced_renders_tofu(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    uint32_t tok = nt_font_test_register_data(blob, blob_size);
    nt_resource_t res = nt_font_test_resource(tok);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* Reference the blob: 'A' is a real glyph (zero-copy decode through the pin);
     * 'Z' is absent -> the single font-level tofu sentinel (0xFFFFFFFF). */
    const nt_glyph_cache_entry_t *a = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_FALSE(a->is_tofu);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);

    const nt_glyph_cache_entry_t *z = nt_font_lookup_glyph(font, 'Z');
    TEST_ASSERT_NOT_NULL(z);
    TEST_ASSERT_TRUE(z->is_tofu);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, z->codepoint);

    /* Unmount while referenced: no crash, holder freed once (no double-free). */
    nt_font_test_deactivate(tok);
    nt_resource_step();
    nt_font_step();

    /* Sole provider gone -> metrics cleared; a previously-present codepoint now
     * degrades to no-glyph (NULL) instead of dereferencing the freed provider. */
    TEST_ASSERT_EQUAL_UINT16(0U, nt_font_get_metrics(font).units_per_em);
    const nt_glyph_cache_entry_t *a2 = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NULL(a2);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-04: fallback resolution ORDER (first-wins) + tofu terminal ----
 *
 * Two resources merged into ONE nt_font_t (base first, fallback second) with
 * overlapping coverage. Pins that:
 *   (a) a codepoint present in BOTH resolves to the FIRST-added resource — via
 *       glyph identity (advance), not merely "non-tofu";
 *   (b) a codepoint present only in the second resolves to the second;
 *   (c) a codepoint in NEITHER resolves to the single font-level tofu (sentinel
 *       0xFFFFFFFF) and that tofu is TERMINAL — a second distinct miss returns
 *       the SAME tofu entry, so no intermediate resource emits its own tofu. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_font_fallback_order_first_wins(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    /* Base owns 'A' + shared 'M' + shared CJK 0x4E2D (advance 500);
     * fallback owns shared 'M' + 'Z' + shared CJK 0x4E2D (advance 700).
     * Codepoints are pre-sorted per blob (bsearch precondition). */
    const uint32_t base_cps[3] = {'A', 'M', 0x4E2DU};
    const uint32_t fb_cps[3] = {'M', 'Z', 0x4E2DU};
    uint32_t base_sz = 0;
    uint32_t fb_sz = 0;
    uint8_t *base = build_font_blob_codepoints(1000, 800, -200, 0, base_cps, 3, 500, &base_sz);
    uint8_t *fb = build_font_blob_codepoints(1000, 800, -200, 0, fb_cps, 3, 700, &fb_sz);

    nt_resource_t base_res = register_font_resource("fb_base", base, base_sz);
    nt_resource_t fb_res = register_font_resource("fb_fallback", fb, fb_sz);

    /* Fixed order: base FIRST, fallback SECOND → base wins shared codepoints. */
    nt_font_add(font, base_res);
    nt_font_add(font, fb_res);
    nt_resource_step();
    nt_font_step();

    /* (a) shared ASCII 'M' → base (advance 500), NOT fallback (700).
     * ASCII exercises the ascii_glyph_idx fast path. */
    const nt_glyph_cache_entry_t *m = nt_font_lookup_glyph(font, 'M');
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_FALSE(m->is_tofu);
    TEST_ASSERT_EQUAL_UINT32('M', m->codepoint);
    TEST_ASSERT_EQUAL_INT16(500, m->advance);

    /* (a') shared non-ASCII 0x4E2D → base (500) via the bsearch first-wins loop
     * (codepoint >= 128 bypasses the ascii fast path). */
    const nt_glyph_cache_entry_t *cjk = nt_font_lookup_glyph(font, 0x4E2DU);
    TEST_ASSERT_NOT_NULL(cjk);
    TEST_ASSERT_FALSE(cjk->is_tofu);
    TEST_ASSERT_EQUAL_UINT32(0x4E2DU, cjk->codepoint);
    TEST_ASSERT_EQUAL_INT16(500, cjk->advance);

    /* (b) 'A' only in base → base (500). */
    const nt_glyph_cache_entry_t *a = nt_font_lookup_glyph(font, 'A');
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_FALSE(a->is_tofu);
    TEST_ASSERT_EQUAL_INT16(500, a->advance);

    /* (b') 'Z' only in fallback → second resource (700). */
    const nt_glyph_cache_entry_t *z = nt_font_lookup_glyph(font, 'Z');
    TEST_ASSERT_NOT_NULL(z);
    TEST_ASSERT_FALSE(z->is_tofu);
    TEST_ASSERT_EQUAL_UINT32('Z', z->codepoint);
    TEST_ASSERT_EQUAL_INT16(700, z->advance);

    /* (c) '#' in NEITHER → single font-level tofu, sentinel 0xFFFFFFFF. */
    const nt_glyph_cache_entry_t *miss1 = nt_font_lookup_glyph(font, '#');
    TEST_ASSERT_NOT_NULL(miss1);
    TEST_ASSERT_TRUE(miss1->is_tofu);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, miss1->codepoint);

    /* Terminal: a DIFFERENT miss returns the SAME tofu entry — proves one
     * font-level tofu, not a per-resource tofu leaking through the cascade. */
    const nt_glyph_cache_entry_t *miss2 = nt_font_lookup_glyph(font, '@');
    TEST_ASSERT_NOT_NULL(miss2);
    TEST_ASSERT_TRUE(miss2->is_tofu);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, miss2->codepoint);
    TEST_ASSERT_EQUAL_PTR(miss1, miss2);

    nt_font_destroy(font);
    free(base);
    free(fb);
}

/* ---- FONT-02: prebaked-cmap lookup is bounded + no-parse (bsearch) ----
 *
 * The glyph table is prebaked SORTED by codepoint and resolved via bsearch
 * (find_glyph_in_pack) — heap-free, no parse step, bounded. This confirms that
 * without adding any new cmap/bitmap structure. Uses nt_font_lookup_metrics
 * (CPU-only: no GPU upload, no glyph cache touch) so the assertion exercises
 * the bare bsearch path, and covers its boundaries: first, last, interior hit,
 * below-first / interior-gap / above-last misses. Non-ASCII codepoints force
 * the bsearch loop (ASCII would take the ascii_glyph_idx fast path instead). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_font_cmap_bounded_no_parse(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    /* Sparse, SORTED, all >= 128 → every lookup goes through find_glyph_in_pack. */
    const uint32_t cps[4] = {0x100U, 0x200U, 0x20ACU, 0x4E2DU};
    uint32_t sz = 0;
    uint8_t *blob = build_font_blob_codepoints(1000, 800, -200, 0, cps, 4, 600, &sz);
    nt_resource_t res = register_font_resource("cmap_bounded", blob, sz);
    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    /* Present — first, interior, interior, last all resolve to the real glyph. */
    for (int i = 0; i < 4; i++) {
        nt_glyph_metrics_t hit = nt_font_lookup_metrics(font, cps[i]);
        TEST_ASSERT_TRUE(hit.found);
        TEST_ASSERT_EQUAL_INT16(600, hit.advance);
    }

    /* Absent — bsearch not-found boundaries: below first, gap between entries,
     * above last. Each returns the tofu fallback metrics (found = false). */
    const uint32_t absent[3] = {0x0FFU, 0x150U, 0x30000U};
    for (int i = 0; i < 3; i++) {
        nt_glyph_metrics_t miss = nt_font_lookup_metrics(font, absent[i]);
        TEST_ASSERT_FALSE(miss.found);
    }

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-01: two resources at a COMMON (builder-normalized) UPM merge
 * into one font without tripping the shared-metrics assert ----
 *
 * Two source fonts of different NATURAL UPM (latin ~1000, CJK 2048) that the
 * builder (Plan 01) normalized to a COMMON units_per_em with the primary
 * driving vmetrics. At runtime both headers carry identical metrics, so adding
 * BOTH into one nt_font_t simultaneously must NOT fire the multi-resource
 * mismatch assert (nt_font.c:1087), and nt_font_get_metrics reports the common
 * UPM. Both resources also resolve their own glyphs within the one font. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_font_merged_different_upm_no_metrics_assert(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    const uint16_t common_upm = 2048U; /* = max(1000, 2048), scaled toward max */
    const int16_t asc = 1600;
    const int16_t descent = -400;
    const int16_t lg = 0;
    const uint32_t latin_cps[2] = {'A', 'M'};       /* base: half-em advance */
    const uint32_t cjk_cps[2] = {0x4E2DU, 0x4E8CU}; /* CJK: full-width advance */
    uint32_t latin_sz = 0;
    uint32_t cjk_sz = 0;
    uint8_t *latin = build_font_blob_codepoints(common_upm, asc, descent, lg, latin_cps, 2, 1024, &latin_sz);
    uint8_t *cjk = build_font_blob_codepoints(common_upm, asc, descent, lg, cjk_cps, 2, 2048, &cjk_sz);

    nt_resource_t latin_res = register_font_resource("merge_latin", latin, latin_sz);
    nt_resource_t cjk_res = register_font_resource("merge_cjk", cjk, cjk_sz);

    /* Primary (latin) FIRST drives line height; CJK second. Simultaneous merge. */
    nt_font_add(font, latin_res);
    nt_font_add(font, cjk_res);
    nt_resource_step();
    nt_font_step(); /* must NOT abort on the shared-metrics assert */

    nt_font_metrics_t m = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_UINT16(common_upm, m.units_per_em);
    TEST_ASSERT_EQUAL_INT16(asc, m.ascent);
    TEST_ASSERT_EQUAL_INT16(descent, m.descent);
    TEST_ASSERT_EQUAL_INT16((int16_t)(asc - descent + lg), m.line_height);

    /* Both resources resolve within the one font (base + CJK). */
    nt_glyph_metrics_t latin_g = nt_font_lookup_metrics(font, 'A');
    TEST_ASSERT_TRUE(latin_g.found);
    TEST_ASSERT_EQUAL_INT16(1024, latin_g.advance);
    nt_glyph_metrics_t cjk_g = nt_font_lookup_metrics(font, 0x4E2DU);
    TEST_ASSERT_TRUE(cjk_g.found);
    TEST_ASSERT_EQUAL_INT16(2048, cjk_g.advance);

    nt_font_destroy(font);
    free(latin);
    free(cjk);
}

/* ---- Test 9: GPU texture handles (FONT-03) ---- */

void test_font_gpu_textures(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);

    nt_texture_t ct = nt_font_get_curve_texture(font);
    nt_texture_t bt = nt_font_get_band_texture(font);
    TEST_ASSERT_NOT_EQUAL(0U, ct.id);
    TEST_ASSERT_NOT_EQUAL(0U, bt.id);

    TEST_ASSERT_EQUAL_UINT8(4, nt_font_get_band_count(font));
    TEST_ASSERT_EQUAL_UINT16(64, nt_font_get_curve_texture_width(font));

    nt_font_destroy(font);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_font_blob_valid);
    RUN_TEST(test_font_init_shutdown);
    RUN_TEST(test_font_create_destroy_valid);
    RUN_TEST(test_font_add_resource);
    RUN_TEST(test_font_get_metrics);
    RUN_TEST(test_font_lookup_glyph_hit);
    RUN_TEST(test_font_lookup_glyph_miss_tofu);
    RUN_TEST(test_font_fallback_order_first_wins);
    RUN_TEST(test_font_cmap_bounded_no_parse);
    RUN_TEST(test_font_merged_different_upm_no_metrics_assert);
    RUN_TEST(test_font_get_stats);
    RUN_TEST(test_font_lru_eviction);
    RUN_TEST(test_font_gpu_textures);
    /* FONT-01 + FONT-02: length-aware measure + direct-mapped cache */
    RUN_TEST(test_measure_n_matches_measure);
    RUN_TEST(test_measure_n_does_not_over_read);
    RUN_TEST(test_measure_n_drops_partial_utf8);
    RUN_TEST(test_measure_n_embedded_nul_is_codepoint);
    RUN_TEST(test_measure_n_letter_spacing_grows_width);
    RUN_TEST(test_measure_n_skips_carriage_return);
    RUN_TEST(test_measure_n_cache_hits_on_repeat);
    RUN_TEST(test_measure_n_invalidate_forces_miss);
    RUN_TEST(test_measure_n_destroy_clears_cache);
    RUN_TEST(test_measure_n_cache_disabled);
    RUN_TEST(test_measure_n_cache_custom_size);
    RUN_TEST(test_measure_n_invalidates_on_resource_change);
    RUN_TEST(test_font_add_already_published_forces_rescan);
    RUN_TEST(test_measure_n_invalidates_on_resource_unload);
    RUN_TEST(test_font_recovers_after_remount);
    RUN_TEST(test_font_flushes_on_same_metrics_winner_swap);
    RUN_TEST(test_font_hotswap_replaces_metrics_in_one_step);
    RUN_TEST(test_font_file_pack_unmount_cleans_state);
    RUN_TEST(test_font_truncated_winner_swap_clears_provider);
    RUN_TEST(test_font_unmount_while_referenced_renders_tofu);
    return UNITY_END();
}
