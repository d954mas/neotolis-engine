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

/* ---- Virtual pack ID counter (unique per test) ---- */

static uint32_t s_vpack_counter;

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
static uint8_t *build_test_font_blob(uint32_t *out_size) {
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
    hdr.units_per_em = 1000;
    hdr.ascent = 800;
    hdr.descent = -200;
    hdr.line_gap = 0;
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

/* ---- Helper: register font blob as test resource ---- */

static nt_resource_t register_font_resource(const char *name, const uint8_t *blob, uint32_t blob_size) {
    /* Register blob into font data side table to get a handle */
    uint32_t data_handle = nt_font_test_register_data(blob, blob_size);

    /* Create virtual pack and register resource */
    char pack_name[64];
    (void)snprintf(pack_name, sizeof(pack_name), "fp_%s_%u", name, s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t rid = nt_hash64_str(name);

    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, rid, NT_ASSET_FONT, data_handle);

    return nt_resource_request(rid, NT_ASSET_FONT);
}

/* ---- Default create descriptor for tests ---- */

static nt_font_create_desc_t test_font_desc(void) {
    return (nt_font_create_desc_t){
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 16,
        .band_count = 4,
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
    s_vpack_counter = 0;
}

void tearDown(void) {
    nt_font_shutdown();
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
    RUN_TEST(test_font_get_stats);
    RUN_TEST(test_font_lru_eviction);
    RUN_TEST(test_font_gpu_textures);
    return UNITY_END();
}
