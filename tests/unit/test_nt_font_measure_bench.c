/* nt_font_measure_n direct-mapped cache benchmark.
 *
 * Workloads (printed as [BENCH] lines to stdout):
 *   - short_hit:   1000× "Hello, world" (12 chars, cache warm) → hash + return
 *   - short_miss:  1000× unique "Item N" strings (forces cache miss every call)
 *   - long_hit:    1000× same ~2 KB ASCII paragraph (large hash, cache hit)
 *   - long_miss:   1000× unique ~2 KB strings (large hash + full measure)
 *   - mixed_ui:    typical UI frame — 80 hot short labels × 12 frames + 20 fresh
 *                  unique labels, simulating buttons + status text
 *
 * Captures both cache and raw measure cost. Re-run after optimization passes
 * to compare ns/call.
 */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
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
#include "time/nt_time.h"
#include "unity.h"
/* clang-format on */

/* ---- Virtual pack ID counter (unique per bench) ---- */

static uint32_t s_vpack_counter;

/* ---- Test blob builder (mirrors tests/unit/test_font.c) ----
 *
 * Build a minimal valid NT_ASSET_FONT binary blob in memory.
 * Contains 3 glyphs ('A'=65, 'B'=66, 'C'=67), each with 2 line segments.
 */
static uint8_t *build_test_font_blob(uint32_t *out_size) {
    uint32_t contour_size = 18;
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
    uint32_t codepoints[3] = {65, 66, 67};

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

    for (int g = 0; g < 3; g++) {
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

static nt_font_create_desc_t bench_font_desc(void) {
    return (nt_font_create_desc_t){
        .curve_texture_width = 64, .curve_texture_height = 64, .band_texture_height = 16, .band_count = 4, .measure_cache_size = 256, /* benchmark measures cache hit/miss — explicit opt-in */
    };
}

/* Build a fully-resolved test font (cache + metrics ready). Caller frees blob. */
static nt_font_t make_bench_font(const char *name, uint8_t **out_blob) {
    nt_font_create_desc_t desc = bench_font_desc();
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

/* ---- Bench 1: cache-hit steady state ---- */

static void bench_cache_hit_steady_state(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_bench_font("bench_hit", &blob);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    nt_font_measure_invalidate(font);

    /* Warm-up: one call to populate the slot. */
    (void)nt_font_measure_n(font, "Hello, world", 12U, 14.0F, 0.0F);

    const int n_calls = 1000;
    const uint64_t t0 = nt_time_nanos();
    nt_text_size_t sink = {0.0F, 0.0F};
    for (int i = 0; i < n_calls; i++) {
        sink = nt_font_measure_n(font, "Hello, world", 12U, 14.0F, 0.0F);
    }
    const uint64_t t1 = nt_time_nanos();
    (void)sink; /* prevent dead-code elimination */

    const double total_ns = (double)(t1 - t0);
    const double per_call_ns = total_ns / (double)n_calls;
    (void)printf("[BENCH] cache_hit_steady_state: %.2f ns/call (%d calls in %.0f ns)\n", per_call_ns, n_calls, total_ns);
    (void)fflush(stdout);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Bench 2: cache-miss unique strings ---- */

static void bench_cache_miss_unique(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_bench_font("bench_miss", &blob);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    nt_font_measure_invalidate(font);

    /* 1000 unique strings — each one collides with whatever is in its slot,
     * so it is effectively a miss every time. The 256-entry direct-mapped
     * cache means ~3-4 unique strings per slot on average → ~99.6% miss rate;
     * close enough to "no cache" cost for the threshold check. */
    const int n_calls = 1000;
    static char labels[1000][16];
    for (int i = 0; i < n_calls; i++) {
        (void)snprintf(labels[i], sizeof(labels[i]), "Item %d", i);
    }

    const uint64_t t0 = nt_time_nanos();
    nt_text_size_t sink = {0.0F, 0.0F};
    for (int i = 0; i < n_calls; i++) {
        sink = nt_font_measure_n(font, labels[i], strlen(labels[i]), 14.0F, 0.0F);
    }
    const uint64_t t1 = nt_time_nanos();
    (void)sink;

    const double total_ns = (double)(t1 - t0);
    const double per_call_ns = total_ns / (double)n_calls;
    (void)printf("[BENCH] cache_miss_unique: %.2f ns/call (%d calls in %.0f ns)\n", per_call_ns, n_calls, total_ns);
    (void)fflush(stdout);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Bench 3: long-string cache HIT (large hash, repeated content) ---- */

static void bench_long_string_hit(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_bench_font("bench_lhit", &blob);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    nt_font_measure_invalidate(font);

    /* ~2 KB ASCII paragraph (lorem-ipsum-style filler). Same content every
     * call → cache hit. Measures: (xxHash64 over 2 KB) + entry compare cost. */
    static char paragraph[2048];
    for (size_t i = 0; i < sizeof(paragraph) - 1U; i++) {
        paragraph[i] = (char)('A' + (int)(i % 26));
    }
    paragraph[sizeof(paragraph) - 1U] = '\0';
    const size_t plen = sizeof(paragraph) - 1U;

    /* Warm-up populates the slot. */
    (void)nt_font_measure_n(font, paragraph, plen, 14.0F, 0.0F);

    const int n_calls = 1000;
    const uint64_t t0 = nt_time_nanos();
    nt_text_size_t sink = {0.0F, 0.0F};
    for (int i = 0; i < n_calls; i++) {
        sink = nt_font_measure_n(font, paragraph, plen, 14.0F, 0.0F);
    }
    const uint64_t t1 = nt_time_nanos();
    (void)sink;

    const double per_call_ns = (double)(t1 - t0) / (double)n_calls;
    (void)printf("[BENCH] long_string_hit (%zu B): %.2f ns/call\n", plen, per_call_ns);
    (void)fflush(stdout);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Bench 4: long-string cache MISS (large hash + full measure) ---- */

static void bench_long_string_miss(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_bench_font("bench_lmiss", &blob);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    nt_font_measure_invalidate(font);

    /* 1000 unique ~2 KB strings (the leading 8 chars vary, rest is filler).
     * Each call: full xxHash64 over 2 KB + full UTF-8 measure pass. The cache
     * does write but is overwritten next call. Worst case for big-text. */
    const int n_calls = 1000;
    static char texts[1000][2048];
    for (int i = 0; i < n_calls; i++) {
        (void)snprintf(texts[i], 32U, "uniq_%04d_", i); /* unique prefix */
        for (size_t j = 10U; j < sizeof(texts[i]) - 1U; j++) {
            texts[i][j] = (char)('A' + (int)((j + (size_t)i) % 26));
        }
        texts[i][sizeof(texts[i]) - 1U] = '\0';
    }

    const uint64_t t0 = nt_time_nanos();
    nt_text_size_t sink = {0.0F, 0.0F};
    for (int i = 0; i < n_calls; i++) {
        sink = nt_font_measure_n(font, texts[i], sizeof(texts[i]) - 1U, 14.0F, 0.0F);
    }
    const uint64_t t1 = nt_time_nanos();
    (void)sink;

    const double per_call_ns = (double)(t1 - t0) / (double)n_calls;
    (void)printf("[BENCH] long_string_miss (~2 KB): %.2f ns/call\n", per_call_ns);
    (void)fflush(stdout);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Bench 5: mixed UI workload — 80 hot labels + 20 fresh per "frame" ---- */

static void bench_mixed_ui(void) {
    uint8_t *blob = NULL;
    nt_font_t font = make_bench_font("bench_mix", &blob);
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, font.id);

    nt_font_measure_invalidate(font);

    /* 80 short hot labels (typical button text), reused every "frame" */
    const int hot_count = 80;
    static char hot[80][32];
    for (int i = 0; i < hot_count; i++) {
        (void)snprintf(hot[i], sizeof(hot[i]), "Button_%02d", i);
    }
    /* Pre-warm the cache for hot labels (simulates frame N>1 of stable UI) */
    for (int i = 0; i < hot_count; i++) {
        (void)nt_font_measure_n(font, hot[i], strlen(hot[i]), 14.0F, 0.0F);
    }

    /* 240 fresh status labels (12 frames × 20 unique per frame) */
    const int fresh_per_frame = 20;
    const int frames = 12;
    static char fresh[240][32];
    for (int i = 0; i < fresh_per_frame * frames; i++) {
        (void)snprintf(fresh[i], sizeof(fresh[i]), "Status %d ms", i);
    }

    const uint64_t t0 = nt_time_nanos();
    nt_text_size_t sink = {0.0F, 0.0F};
    int fresh_idx = 0;
    for (int frame = 0; frame < frames; frame++) {
        for (int i = 0; i < hot_count; i++) {
            sink = nt_font_measure_n(font, hot[i], strlen(hot[i]), 14.0F, 0.0F);
        }
        for (int i = 0; i < fresh_per_frame; i++) {
            const char *s = fresh[fresh_idx++];
            sink = nt_font_measure_n(font, s, strlen(s), 14.0F, 0.0F);
        }
    }
    const uint64_t t1 = nt_time_nanos();
    (void)sink;

    const int total_calls = frames * (hot_count + fresh_per_frame);
    const double per_call_ns = (double)(t1 - t0) / (double)total_calls;
    (void)printf("[BENCH] mixed_ui (80 hot × 12 frames + 240 fresh): %.2f ns/call (%d calls)\n", per_call_ns, total_calls);
    (void)fflush(stdout);

    nt_font_destroy(font);
    free(blob);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(bench_cache_hit_steady_state);
    RUN_TEST(bench_cache_miss_unique);
    RUN_TEST(bench_long_string_hit);
    RUN_TEST(bench_long_string_miss);
    RUN_TEST(bench_mixed_ui);
    return UNITY_END();
}
