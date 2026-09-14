/* Trimmed-transcoder consumer (native mirror library, or the production library
 * under Node): every (codec, target) pair inside the admission set must match the
 * native goldens byte-for-byte, every pair outside it must be refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nt_basisu_transcoder.h"
#include "nt_texture_format.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define EXPECTED_FIXTURES 12U
#define MAX_PATH_BYTES 512

static const nt_texture_format_t s_targets[] = {NT_TEXTURE_FORMAT_ETC2_RGB8, NT_TEXTURE_FORMAT_ETC2_RGBA8, NT_TEXTURE_FORMAT_BC7_RGBA, NT_TEXTURE_FORMAT_ASTC_4x4_RGBA, NT_TEXTURE_FORMAT_RGBA8};
static const char *const s_target_names[] = {"etc2_rgb8", "etc2_rgba8", "bc7", "astc", "rgba8"};
static const bool s_target_enabled[] = {NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_BC7 != 0, NT_BASISU_HAS_ASTC != 0, true};
#define TARGET_COUNT (sizeof(s_targets) / sizeof(s_targets[0]))

static uint32_t s_fixtures;
static uint32_t s_etc1s_fixtures;
static uint32_t s_uastc_fixtures;
static uint32_t s_identical;
static uint32_t s_refused;

static uint8_t *read_file(const char *name, uint32_t *out_size) {
    char path[MAX_PATH_BYTES];
    (void)snprintf(path, sizeof(path), "%s/%s", NT_BASISU_GOLDEN_DIR, name);
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path);
    (void)fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    (void)fseek(file, 0, SEEK_SET);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)size);
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    TEST_ASSERT_NOT_NULL(data);
    const bool ok = fread(data, 1, (size_t)size, file) == (size_t)size;
    (void)fclose(file);
    TEST_ASSERT_TRUE_MESSAGE(ok, path);
    *out_size = (uint32_t)size;
    return data;
}

/* Indexed by nt_basisu_codec_t: NONE, ETC1S, UASTC_LDR. */
static const bool s_codec_enabled[] = {false, NT_BASISU_HAS_ETC1S != 0, NT_BASISU_HAS_UASTC != 0};

/* Space-separated tokens; NULL at the end of the line. */
static char *next_token(char **cursor) {
    char *token = *cursor;
    if (token == NULL || *token == '\0') {
        return NULL;
    }
    char *space = strchr(token, ' ');
    if (space != NULL) {
        *space = '\0';
        *cursor = space + 1;
    } else {
        *cursor = NULL;
    }
    return token;
}

/* Manifest fields are unsigned decimals. */
static uint32_t next_field(char **cursor, const char *line) {
    const char *token = next_token(cursor);
    TEST_ASSERT_NOT_NULL_MESSAGE(token, line);
    char *end = NULL;
    const unsigned long value = strtoul(token, &end, 10);
    TEST_ASSERT_TRUE_MESSAGE(end != token && *end == '\0' && value <= UINT32_MAX, line);
    return (uint32_t)value;
}

/* The produced targets that follow the seven fields. */
static void parse_listed_targets(char **cursor, bool listed[TARGET_COUNT], const char *line) {
    for (const char *token = next_token(cursor); token != NULL; token = next_token(cursor)) {
        bool known = false;
        for (size_t t = 0; t < TARGET_COUNT; t++) {
            if (strcmp(token, s_target_names[t]) == 0) {
                listed[t] = true;
                known = true;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(known, line);
    }
}

static uint64_t chain_bytes(nt_texture_format_t format, const nt_basisu_info_t *info) {
    uint64_t total = 0;
    for (uint32_t level = 0; level < info->level_count; level++) {
        total += nt_texture_level_bytes(format, nt_texture_level_extent(info->width, level), nt_texture_level_extent(info->height, level));
    }
    return total;
}

static void check_pair(const char *name, const uint8_t *basis, uint32_t basis_size, const nt_basisu_info_t *info, size_t t, bool golden_listed) {
    char label[96];
    (void)snprintf(label, sizeof(label), "%s -> %s", name, s_target_names[t]);
    const uint64_t bytes = chain_bytes(s_targets[t], info);
    if (bytes == 0) {
        TEST_FAIL_MESSAGE(label);
        return;
    }
    uint8_t *out = (uint8_t *)malloc((size_t)bytes);
    TEST_ASSERT_NOT_NULL(out);
    memset(out, 0xCD, (size_t)bytes);
    const bool ok = nt_basisu_transcode_chain(basis, basis_size, info, s_targets[t], out, (uint32_t)bytes);
    if (!s_target_enabled[t]) {
        TEST_ASSERT_FALSE_MESSAGE(ok, label);
        s_refused++;
        free(out);
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(golden_listed, label);
    TEST_ASSERT_TRUE_MESSAGE(ok, label);
    char file_name[96];
    (void)snprintf(file_name, sizeof(file_name), "%s.%s.bin", name, s_target_names[t]);
    uint32_t golden_size = 0;
    uint8_t *golden = read_file(file_name, &golden_size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)bytes, golden_size, label);
    if (memcmp(golden, out, (size_t)bytes) != 0) {
        size_t first = 0;
        while (first < bytes && golden[first] == out[first]) {
            first++;
        }
        char message[160];
        (void)snprintf(message, sizeof(message), "%s: bytes differ from the golden at offset %zu of %zu", label, first, (size_t)bytes);
        free(golden);
        free(out);
        TEST_FAIL_MESSAGE(message);
    }
    free(golden);
    free(out);
    s_identical++;
}

/* Manifest line: name codec width height has_alpha level_count basis_size [produced targets...] */
static void check_fixture(char *line) {
    char original[256];
    (void)snprintf(original, sizeof(original), "%s", line);
    char *cursor = line;
    const char *name = next_token(&cursor);
    TEST_ASSERT_NOT_NULL_MESSAGE(name, original);
    const uint32_t codec = next_field(&cursor, original);
    const uint32_t w = next_field(&cursor, original);
    const uint32_t h = next_field(&cursor, original);
    const uint32_t alpha = next_field(&cursor, original);
    const uint32_t levels = next_field(&cursor, original);
    const uint32_t size = next_field(&cursor, original);
    bool golden_listed[TARGET_COUNT] = {false};
    parse_listed_targets(&cursor, golden_listed, original);
    s_fixtures++;
    if (codec == NT_BASISU_CODEC_ETC1S) {
        s_etc1s_fixtures++;
    } else {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_BASISU_CODEC_UASTC_LDR, codec, name);
        s_uastc_fixtures++;
    }
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, levels, name);

    char file_name[96];
    (void)snprintf(file_name, sizeof(file_name), "%s.basis", name);
    uint32_t basis_size = 0;
    uint8_t *basis = read_file(file_name, &basis_size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(size, basis_size, name);

    nt_basisu_info_t info;
    memset(&info, 0, sizeof(info));
    const bool info_ok = nt_basisu_info(basis, basis_size, &info);
    const bool allowed = s_codec_enabled[codec];
    TEST_ASSERT_EQUAL_MESSAGE(allowed, info_ok, name);
    if (!allowed) {
        /* A caller-built info must not open a path around the input admission. */
        info.codec = (nt_basisu_codec_t)codec;
        info.width = w;
        info.height = h;
        info.level_count = levels;
        info.has_alpha = alpha != 0;
        for (size_t t = 0; t < TARGET_COUNT; t++) {
            const uint64_t bytes = chain_bytes(s_targets[t], &info);
            /* The manifest's shape is untrusted input; the largest fixture is 128x128 RGBA8. */
            if (bytes == 0 || bytes > (1U << 20)) {
                TEST_FAIL_MESSAGE(name);
                return;
            }
            uint8_t *out = (uint8_t *)malloc((size_t)bytes);
            TEST_ASSERT_NOT_NULL(out);
            const bool ok = nt_basisu_transcode_chain(basis, basis_size, &info, s_targets[t], out, (uint32_t)bytes);
            free(out);
            TEST_ASSERT_FALSE_MESSAGE(ok, name);
            s_refused++;
        }
        free(basis);
        return;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(w, info.width, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(h, info.height, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(levels, info.level_count, name);
    TEST_ASSERT_EQUAL_MESSAGE(alpha != 0, info.has_alpha, name);
    for (size_t t = 0; t < TARGET_COUNT; t++) {
        check_pair(name, basis, basis_size, &info, t, golden_listed[t]);
    }
    free(basis);
}

void test_every_admitted_pair_matches_the_golden_and_every_other_is_refused(void) {
    uint32_t manifest_size = 0;
    uint8_t *manifest = read_file("manifest.txt", &manifest_size);
    if (manifest_size >= 65536U) {
        TEST_FAIL_MESSAGE("manifest.txt is not a manifest");
        return;
    }
    char *text = (char *)malloc((size_t)manifest_size + 1);
    TEST_ASSERT_NOT_NULL(text);
    memcpy(text, manifest, manifest_size);
    text[manifest_size] = '\0';
    free(manifest);

    char *cursor = text;
    while (*cursor) {
        char *end = strpbrk(cursor, "\r\n");
        if (end) {
            *end = '\0';
        }
        if (*cursor) {
            check_fixture(cursor);
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    free(text);

    /* The manifest itself is under test: a producer that dropped a codec or a
     * fixture would otherwise pass by checking less. */
    TEST_ASSERT_EQUAL_UINT32(EXPECTED_FIXTURES, s_fixtures);
    TEST_ASSERT_EQUAL_UINT32(EXPECTED_FIXTURES / 2, s_etc1s_fixtures);
    TEST_ASSERT_EQUAL_UINT32(EXPECTED_FIXTURES / 2, s_uastc_fixtures);
    TEST_ASSERT_EQUAL_UINT32(EXPECTED_FIXTURES * TARGET_COUNT, s_identical + s_refused);
    (void)printf("basisu trimmed: %u byte-identical, %u refused (ETC1S=%d UASTC=%d ETC2=%d BC7=%d ASTC=%d)\n", s_identical, s_refused, NT_BASISU_HAS_ETC1S, NT_BASISU_HAS_UASTC, NT_BASISU_HAS_ETC2,
                 NT_BASISU_HAS_BC7, NT_BASISU_HAS_ASTC);
}

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    RUN_TEST(test_every_admitted_pair_matches_the_golden_and_every_other_is_refused);
    return UNITY_END();
}
