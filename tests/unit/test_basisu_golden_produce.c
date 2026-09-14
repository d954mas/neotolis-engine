/* Golden producer: encodes fixed synthetic fixtures with the builder's encoder
 * and decodes each one to every engine target with the native transcoder. The
 * files land in NT_BASISU_GOLDEN_DIR for test_basisu_trimmed (native mirror and
 * WASM) and are pinned by SHA-256 against tests/fixtures/basisu_golden.sha256,
 * the baseline taken before the vendored transcoder patch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nt_basisu_encoder.h"
#include "nt_basisu_transcoder.h"
#include "nt_texture_format.h"
#include "ntpack_parse.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define GOLDEN_SHA_FILE "tests/fixtures/basisu_golden.sha256"

typedef struct {
    const char *name;
    uint32_t w;
    uint32_t h;
    bool alpha;
    nt_basisu_codec_t codec;
} fixture_t;

/* Both codecs always: the native encoder is a superset, and the baseline must
 * cover every codec a restricted consumer has to reject. */
static const fixture_t s_fixtures[] = {
    {"etc1s_96x64_rgba", 96, 64, true, NT_BASISU_CODEC_ETC1S},     {"etc1s_96x64_rgb", 96, 64, false, NT_BASISU_CODEC_ETC1S},         {"etc1s_13x7_rgba", 13, 7, true, NT_BASISU_CODEC_ETC1S},
    {"etc1s_1x1_rgb", 1, 1, false, NT_BASISU_CODEC_ETC1S},         {"etc1s_128x128_rgba", 128, 128, true, NT_BASISU_CODEC_ETC1S},     {"etc1s_4x4_rgba", 4, 4, true, NT_BASISU_CODEC_ETC1S},
    {"uastc_96x64_rgba", 96, 64, true, NT_BASISU_CODEC_UASTC_LDR}, {"uastc_96x64_rgb", 96, 64, false, NT_BASISU_CODEC_UASTC_LDR},     {"uastc_13x7_rgba", 13, 7, true, NT_BASISU_CODEC_UASTC_LDR},
    {"uastc_1x1_rgb", 1, 1, false, NT_BASISU_CODEC_UASTC_LDR},     {"uastc_128x128_rgba", 128, 128, true, NT_BASISU_CODEC_UASTC_LDR}, {"uastc_4x4_rgba", 4, 4, true, NT_BASISU_CODEC_UASTC_LDR},
};
#define FIXTURE_COUNT (sizeof(s_fixtures) / sizeof(s_fixtures[0]))

static const nt_texture_format_t s_targets[] = {NT_TEXTURE_FORMAT_ETC2_RGB8, NT_TEXTURE_FORMAT_ETC2_RGBA8, NT_TEXTURE_FORMAT_BC7_RGBA, NT_TEXTURE_FORMAT_ASTC_4x4_RGBA, NT_TEXTURE_FORMAT_RGBA8};
static const char *const s_target_names[] = {"etc2_rgb8", "etc2_rgba8", "bc7", "astc", "rgba8"};
static const bool s_target_enabled[] = {NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_BC7 != 0, NT_BASISU_HAS_ASTC != 0, true};
#define TARGET_COUNT (sizeof(s_targets) / sizeof(s_targets[0]))
/* Indexed by nt_basisu_codec_t: NONE, ETC1S, UASTC_LDR. */
static const bool s_codec_enabled[] = {false, NT_BASISU_HAS_ETC1S != 0, NT_BASISU_HAS_UASTC != 0};

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

static void produce_fixture(const fixture_t *fx, FILE *manifest) {
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
    const bool codec_enabled = s_codec_enabled[fx->codec];
    TEST_ASSERT_EQUAL_MESSAGE(codec_enabled, nt_basisu_info(enc.data, enc.size, &info), fx->name);
    if (!codec_enabled) {
        /* Outside NT_BASISU_CODECS the wrapper refuses the blob; the consumer's
         * negative checks need only the file and its shape. */
        (void)fprintf(manifest, "%s %d %u %u %d %u %u\n", fx->name, (int)fx->codec, fx->w, fx->h, fx->alpha ? 1 : 0, enc.mip_count, enc.size);
        nt_basisu_encode_free(&enc);
        return;
    }
    TEST_ASSERT_EQUAL_INT(fx->codec, info.codec);
    TEST_ASSERT_EQUAL_UINT32(fx->w, info.width);
    TEST_ASSERT_EQUAL_UINT32(fx->h, info.height);
    TEST_ASSERT_EQUAL(fx->alpha, info.has_alpha);
    TEST_ASSERT_EQUAL_UINT32(enc.mip_count, info.level_count);
    TEST_ASSERT_GREATER_THAN_UINT32(0, info.level_count);
    (void)fprintf(manifest, "%s %d %u %u %d %u %u", fx->name, (int)info.codec, info.width, info.height, info.has_alpha ? 1 : 0, info.level_count, enc.size);

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
        /* A target outside NT_BASISU_TARGETS has no golden in this configure. */
        TEST_ASSERT_EQUAL_MESSAGE(s_target_enabled[t], ok, fx->name);
        if (ok) {
            (void)snprintf(file_name, sizeof(file_name), "%s.%s.bin", fx->name, s_target_names[t]);
            write_golden(file_name, out, (size_t)bytes);
            (void)fprintf(manifest, " %s", s_target_names[t]);
        }
        free(out);
    }
    (void)fputc('\n', manifest);
    nt_basisu_encode_free(&enc);
}

#if NT_BASISU_HAS_ETC1S && NT_BASISU_HAS_UASTC && NT_BASISU_HAS_ETC2 && NT_BASISU_HAS_BC7 && NT_BASISU_HAS_ASTC
static int compare_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* sha256sum's own line format, sorted by file name, so the committed baseline
 * can be regenerated from any tree with `sha256sum *.basis *.bin | sort -k2`. */
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
#endif

void test_goldens_are_produced_and_pinned(void) {
    char path[MAX_PATH_BYTES];
    (void)snprintf(path, sizeof(path), "%s/manifest.txt", NT_BASISU_GOLDEN_DIR);
    FILE *manifest = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(manifest, path);
    for (size_t i = 0; i < FIXTURE_COUNT; i++) {
        produce_fixture(&s_fixtures[i], manifest);
    }
    (void)fclose(manifest);
    /* The pinned baseline is the default set; a restricted configure produces
     * fewer files and is checked by test_basisu_trimmed against its own run. */
#if NT_BASISU_HAS_ETC1S && NT_BASISU_HAS_UASTC && NT_BASISU_HAS_ETC2 && NT_BASISU_HAS_BC7 && NT_BASISU_HAS_ASTC
    check_baseline();
#else
    TEST_ASSERT_LESS_THAN_UINT32(MAX_GOLDEN_FILES, s_file_count);
#endif
}

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    nt_basisu_encoder_init();
    RUN_TEST(test_goldens_are_produced_and_pinned);
    return UNITY_END();
}
