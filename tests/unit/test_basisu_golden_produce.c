/* Golden producer: encodes fixed synthetic fixtures with the builder's encoder
 * and decodes them with the full native transcoder to all five targets.
 * These reference bytes are pinned against the committed SHA-256 list. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basisu_fixtures.h"
#include "nt_basisu_encoder.h"
#include "nt_basisu_transcoder.h"
#include "nt_texture_format.h"
#include "ntpack_parse.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define GOLDEN_SHA_FILE "tests/fixtures/basisu_golden.sha256"

/* One hashed file per fixture and per produced target. */
#define MAX_GOLDEN_FILES (FIXTURE_COUNT * (1U + TARGET_COUNT))
#define MAX_PATH_BYTES 512

static char s_file_names[MAX_GOLDEN_FILES][64];
static uint32_t s_file_count;

static uint32_t pixel_hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = (x * 73856093U) ^ (y * 19349663U) ^ (seed * 83492791U);
    h ^= h >> 13U;
    h *= 0x5bd1e995U;
    h ^= h >> 15U;
    return h;
}

/* A flat corner block, ramps xor noise elsewhere, a sparse alpha pattern: the
 * ETC1S palette and the UASTC block modes both get exercised. */
static void generate_pixels(uint8_t *px, uint32_t w, uint32_t h, bool alpha, uint32_t seed) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *p = &px[(((size_t)y * w) + x) * 4];
            const uint32_t n = pixel_hash(x, y, seed);
            if (x < w / 4 && y < h / 4) {
                p[0] = 200;
                p[1] = 100;
                p[2] = 50;
            } else {
                p[0] = (uint8_t)((x * 255U / (w > 1 ? w - 1 : 1)) ^ (n & 0x3fU));
                p[1] = (uint8_t)((y * 255U / (h > 1 ? h - 1 : 1)) ^ ((n >> 6U) & 0x1fU));
                p[2] = (uint8_t)((n >> 8U) & 0xffU);
            }
            p[3] = (alpha && ((x + y) % 3 == 0)) ? (uint8_t)((n >> 16U) & 0xffU) : 255;
        }
    }
}

static void write_golden(const char *name, const void *data, size_t size) {
    char path[MAX_PATH_BYTES];
    (void)snprintf(path, sizeof(path), "%s/%s", NT_BASISU_GOLDEN_DIR, name);
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path);
    const bool written = fwrite(data, 1, size, file) == size;
    (void)fclose(file);
    TEST_ASSERT_TRUE_MESSAGE(written, path);
    TEST_ASSERT_LESS_THAN_UINT32(MAX_GOLDEN_FILES, s_file_count);
    (void)snprintf(s_file_names[s_file_count++], sizeof(s_file_names[0]), "%s", name);
}

static uint64_t chain_bytes(nt_texture_format_t format, const nt_basisu_info_t *info) {
    uint64_t total = 0;
    for (uint32_t level = 0; level < info->level_count; level++) {
        total += nt_texture_level_bytes(format, nt_texture_level_extent(info->width, level), nt_texture_level_extent(info->height, level));
    }
    return total;
}

static void produce_fixture(const fixture_t *fx) {
    uint8_t *pixels = (uint8_t *)malloc((size_t)fx->w * fx->h * 4);
    TEST_ASSERT_NOT_NULL(pixels);
    generate_pixels(pixels, fx->w, fx->h, fx->alpha, (fx->w * 31U) + fx->h);
    nt_basisu_encode_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.codec = fx->codec;
    if (fx->codec == NT_BASISU_CODEC_ETC1S) {
        opts.etc1s.quality = 128;
        opts.etc1s.endpoint_rdo_threshold = 1.5F;
        opts.etc1s.selector_rdo_threshold = 1.25F;
    } else {
        opts.uastc.pack_level = 2;
        opts.uastc.rdo_lambda = 1.0F;
    }
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, fx->w, fx->h, fx->alpha, &opts);
    free(pixels);
    TEST_ASSERT_NOT_NULL_MESSAGE(enc.data, fx->name);

    char file_name[64];
    (void)snprintf(file_name, sizeof(file_name), "%s.basis", fx->name);
    write_golden(file_name, enc.data, enc.size);

    nt_basisu_info_t info = {0};
    TEST_ASSERT_TRUE_MESSAGE(nt_basisu_info(enc.data, enc.size, &info), fx->name);
    TEST_ASSERT_EQUAL_INT(fx->codec, info.codec);
    TEST_ASSERT_EQUAL_UINT32(fx->w, info.width);
    TEST_ASSERT_EQUAL_UINT32(fx->h, info.height);
    TEST_ASSERT_EQUAL(fx->alpha, info.has_alpha);
    TEST_ASSERT_EQUAL_UINT32(enc.mip_count, info.level_count);
    TEST_ASSERT_EQUAL_UINT32(fx->levels, info.level_count);

    for (size_t t = 0; t < TARGET_COUNT; t++) {
        const uint64_t bytes = chain_bytes(s_targets[t], &info);
        if (bytes == 0) {
            TEST_FAIL_MESSAGE(fx->name);
            return;
        }
        uint8_t *out = (uint8_t *)malloc((size_t)bytes);
        TEST_ASSERT_NOT_NULL(out);
        memset(out, 0xCD, (size_t)bytes);
        const bool ok = nt_basisu_transcode_chain(enc.data, enc.size, &info, s_targets[t], out, (uint32_t)bytes);
        TEST_ASSERT_TRUE_MESSAGE(ok, fx->name);
        (void)snprintf(file_name, sizeof(file_name), "%s.%s.bin", fx->name, s_target_names[t]);
        write_golden(file_name, out, (size_t)bytes);
        free(out);
    }
    nt_basisu_encode_free(&enc);
}

static int compare_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* sha256sum's own line format, sorted by file name (regeneration: docs/build.md). */
static void check_baseline(void) {
    qsort(s_file_names, s_file_count, sizeof(s_file_names[0]), compare_names);
    FILE *baseline = fopen(GOLDEN_SHA_FILE, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(baseline, GOLDEN_SHA_FILE);
    char line[160];
    uint32_t matched = 0;
    for (uint32_t i = 0; i < s_file_count; i++) {
        char path[MAX_PATH_BYTES];
        (void)snprintf(path, sizeof(path), "%s/%s", NT_BASISU_GOLDEN_DIR, s_file_names[i]);
        char hex[65];
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(path, hex), path);
        char expected[160];
        (void)snprintf(expected, sizeof(expected), "%s  %s", hex, s_file_names[i]);
        if (!fgets(line, sizeof(line), baseline)) {
            (void)fclose(baseline);
            TEST_FAIL_MESSAGE(s_file_names[i]);
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, expected) != 0) {
            (void)fclose(baseline);
            char message[400];
            (void)snprintf(message, sizeof(message), "golden %s differs from the committed baseline: produced %s, expected line '%s'", s_file_names[i], hex, line);
            TEST_FAIL_MESSAGE(message);
        }
        matched++;
    }
    const bool trailing = fgets(line, sizeof(line), baseline) != NULL;
    (void)fclose(baseline);
    TEST_ASSERT_FALSE_MESSAGE(trailing, "baseline lists more files than the producer wrote");
    TEST_ASSERT_EQUAL_UINT32(MAX_GOLDEN_FILES, matched);
}

void test_goldens_are_produced_and_pinned(void) {
    for (size_t i = 0; i < FIXTURE_COUNT; i++) {
        produce_fixture(&s_fixtures[i]);
    }
    check_baseline();
}

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    nt_basisu_encoder_init();
    RUN_TEST(test_goldens_are_produced_and_pinned);
    return UNITY_END();
}
