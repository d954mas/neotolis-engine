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

    nt_text_size_t a = nt_font_measure(font, "ABC", 14.0F);
    nt_text_size_t b = nt_font_measure_n(font, "ABC", 3U, 14.0F);
    assert_text_size_equal(a, b);

    /* Empty input contract */
    nt_text_size_t zero = {0.0F, 0.0F};
    nt_text_size_t e = nt_font_measure_n(font, "ABC", 0U, 14.0F);
    assert_text_size_equal(zero, e);

    /* NULL guard */
    nt_text_size_t n = nt_font_measure_n(font, NULL, 4U, 14.0F);
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
    nt_text_size_t bounded = nt_font_measure_n(font, buf, 3U, 14.0F);
    nt_text_size_t reference = nt_font_measure(font, "ABC", 14.0F);

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
    nt_text_size_t bounded = nt_font_measure_n(font, partial, 2U, 14.0F);
    nt_text_size_t reference = nt_font_measure(font, "A", 14.0F);

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
    nt_text_size_t n_full = nt_font_measure_n(font, with_nul, 3U, 14.0F);
    nt_text_size_t wrapper_truncated = nt_font_measure(font, with_nul, 14.0F);
    nt_text_size_t reference_a = nt_font_measure(font, "A", 14.0F);

    /* Wrapper stopped at the NUL: width matches a bare "A". */
    assert_text_size_equal(reference_a, wrapper_truncated);

    /* _n consumed all 3 bytes: width must be strictly greater than "A" alone
     * (added at least the 'B' glyph's advance + the NUL's tofu advance). */
    TEST_ASSERT_TRUE(n_full.width > reference_a.width);

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02: 200 identical calls produce 199 hits + 1 miss ---- */

void test_measure_n_cache_hits_on_repeat(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_resolved_test_font("font_hits", &blob);

    nt_font_measure_invalidate(font);
    nt_font_test_reset_measure_counters();

    for (int i = 0; i < 200; i++) {
        (void)nt_font_measure_n(font, "ABC", 3U, 14.0F);
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

    (void)nt_font_measure_n(font, "AB", 2U, 14.0F); /* miss */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F); /* hit */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    nt_font_measure_invalidate_cache();
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F);                        /* miss again after invalidate */
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
    (void)nt_font_measure_n(font_a, "ABC", 3U, 14.0F); /* warm slot */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font_a));

    /* Destroy releases pool slot via memset(slot, 0, sizeof(*slot)); next
     * create may reuse the same physical slot index. */
    nt_font_destroy(font_a);
    free(blob_a);

    uint8_t *blob_b = NULL;
    nt_font_t font_b = make_resolved_test_font("font_d_b", &blob_b);

    nt_font_test_reset_measure_counters();
    (void)nt_font_measure_n(font_b, "ABC", 3U, 14.0F);
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
    nt_text_size_t first = nt_font_measure_n(font, "ABC", 3U, 14.0F);
    for (int i = 0; i < 99; i++) {
        nt_text_size_t r = nt_font_measure_n(font, "ABC", 3U, 14.0F);
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
 * Driven by slot_indices_dirty in nt_font_step. */
void test_measure_n_invalidates_on_resource_change(void) {
    uint8_t *blob_a = NULL;
    nt_font_t font = make_resolved_test_font("font_async_a", &blob_a);

    nt_font_test_reset_measure_counters();

    /* Warm the cache for the same string twice — first miss, then hit. */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F);
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    /* Attach a second resource (independent blob). nt_font_step will see a
     * new resource handle and mark slot_indices_dirty — which must also
     * clear the measure cache. */
    uint32_t blob_size_b = 0;
    uint8_t *blob_b = build_test_font_blob(&blob_size_b);
    nt_resource_t res_b = register_font_resource("font_async_b", blob_b, blob_size_b);
    nt_font_add(font, res_b);
    nt_resource_step();
    nt_font_step();

    /* Re-measure the same string — without the cache invalidation fix this
     * is a HIT (counter unchanged). With the fix it's a MISS (counter +1). */
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));   /* unchanged */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_font_test_measure_cache_misses(font)); /* +1 */

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
        (void)nt_font_measure_n(font, "AB", 2U, 14.0F);
    }
    TEST_ASSERT_EQUAL_UINT32(199U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));

    nt_font_destroy(font);
    free(blob);
}

/* ---- FONT-02g: full pack unmount resets state cleanly ----
 *
 * Covers two coupled fixes triggered by nt_resource_get → 0 transition:
 *
 *  (1) Stale-handle data corruption window: when a resource went from
 *      loaded (handle = N) to unloaded, the old code path simply
 *      `continue`d, leaving slot->resource_handles[ri] at stale N. If
 *      activate_font later recycled data-table slot N for a different
 *      font's pack, get_font_data(N) would silently return the new
 *      pack's bytes — wrong-font glyph metrics via the ASCII fast-path
 *      index. nt_font_step now clears resource_handles[ri] and triggers
 *      slot_indices_dirty so the ASCII index + measure cache rebuild.
 *
 *  (2) metrics_set sticky after full unmount: with all resources gone,
 *      slot->metrics_set stayed true with stale ascent/descent/UPEM.
 *      Renderers' `units_per_em != 0` guard would pass and emit tofu
 *      using wrong-font scale. nt_font_step now resets metrics + flag
 *      when no resource handle remains.
 *
 * Combined effect: nt_font_measure_n short-circuits at `!metrics_set`
 * and returns {0,0}. The cache-check path is never reached, so counters
 * stay frozen — that's the signal that the short-circuit fired. */
void test_measure_n_invalidates_on_resource_unload(void) {
    nt_font_create_desc_t desc = test_font_desc();
    nt_font_t font = nt_font_create(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    /* Register inline so we keep pack_id for unmount (the shared helper
     * uses a counter-suffixed pack name and doesn't expose the id). */
    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    uint32_t data_handle = nt_font_test_register_data(blob, blob_size);
    char pack_name[64];
    (void)snprintf(pack_name, sizeof(pack_name), "fp_unload_%u", s_vpack_counter++);
    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t rid = nt_hash64_str("font_unload");
    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, rid, NT_ASSET_FONT, data_handle);
    nt_resource_t res = nt_resource_request(rid, NT_ASSET_FONT);

    nt_font_add(font, res);
    nt_resource_step();
    nt_font_step();

    nt_font_test_reset_measure_counters();

    /* Warm cache: measure twice → 1 miss + 1 hit. */
    nt_text_size_t live = nt_font_measure_n(font, "AB", 2U, 14.0F);
    (void)nt_font_measure_n(font, "AB", 2U, 14.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font));
    TEST_ASSERT_TRUE(live.width > 0.0F); /* sanity — non-empty measurement before unmount */

    /* Metrics should be live before unmount. */
    nt_font_metrics_t pre = nt_font_get_metrics(font);
    TEST_ASSERT_NOT_EQUAL_UINT16(0U, pre.units_per_em);

    /* Unmount the only pack carrying this resource. After resolve, the
     * asset slot has runtime_handle = 0, so nt_resource_get returns 0. */
    nt_resource_unmount(pid);
    nt_resource_step();
    nt_font_step();

    /* Metrics must reset to {0} on full unmount (fix #2). */
    nt_font_metrics_t post = nt_font_get_metrics(font);
    TEST_ASSERT_EQUAL_UINT16(0U, post.units_per_em);
    TEST_ASSERT_EQUAL_INT16(0, post.ascent);
    TEST_ASSERT_EQUAL_INT16(0, post.descent);

    /* measure_n short-circuits at !metrics_set → returns {0,0}. The cache
     * lookup is never reached, so counters stay frozen — that's the
     * combined signature of both fixes. (UNITY_EXCLUDE_FLOAT in this build,
     * so use TEST_ASSERT_TRUE for the zero check.) */
    nt_text_size_t after = nt_font_measure_n(font, "AB", 2U, 14.0F);
    TEST_ASSERT_TRUE(after.width == 0.0F);
    TEST_ASSERT_TRUE(after.height == 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_hits(font));   /* unchanged */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_font_test_measure_cache_misses(font)); /* unchanged */

    nt_font_destroy(font);
    free(blob);
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
    /* Phase 51 / Plan 04 — FONT-01 + FONT-02 (length-aware measure + direct-mapped cache) */
    RUN_TEST(test_measure_n_matches_measure);
    RUN_TEST(test_measure_n_does_not_over_read);
    RUN_TEST(test_measure_n_drops_partial_utf8);
    RUN_TEST(test_measure_n_embedded_nul_is_codepoint);
    RUN_TEST(test_measure_n_cache_hits_on_repeat);
    RUN_TEST(test_measure_n_invalidate_forces_miss);
    RUN_TEST(test_measure_n_destroy_clears_cache);
    RUN_TEST(test_measure_n_cache_disabled);
    RUN_TEST(test_measure_n_cache_custom_size);
    RUN_TEST(test_measure_n_invalidates_on_resource_change);
    RUN_TEST(test_measure_n_invalidates_on_resource_unload);
    return UNITY_END();
}
