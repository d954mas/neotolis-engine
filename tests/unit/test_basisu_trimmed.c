/* Trimmed-transcoder consumer (native mirror library, or the production library
 * under Node): every (codec, target) pair inside the admission set must match the
 * native goldens byte-for-byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basisu_fixtures.h"
#include "nt_texture_format.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define MAX_PATH_BYTES 512

static const bool s_target_enabled[] = {NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_BC7 != 0, NT_BASISU_HAS_ASTC != 0, true};

static uint32_t s_identical;

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

static uint64_t chain_bytes(nt_texture_format_t format, const nt_basisu_info_t *info) {
    uint64_t total = 0;
    for (uint32_t level = 0; level < info->level_count; level++) {
        total += nt_texture_level_bytes(format, nt_texture_level_extent(info->width, level), nt_texture_level_extent(info->height, level));
    }
    return total;
}

static void check_pair(const char *name, const uint8_t *basis, uint32_t basis_size, const nt_basisu_info_t *info, size_t t) {
    char label[96];
    (void)snprintf(label, sizeof(label), "%s -> %s", name, s_target_names[t]);
    const uint64_t bytes = chain_bytes(s_targets[t], info);
    if (bytes == 0) {
        TEST_FAIL_MESSAGE(label);
        return;
    }
    if (!s_target_enabled[t]) {
        return; /* target option OFF: not a legal argument in this build */
    }
    uint8_t *out = (uint8_t *)malloc((size_t)bytes);
    TEST_ASSERT_NOT_NULL(out);
    memset(out, 0xCD, (size_t)bytes);
    const bool ok = nt_basisu_transcode_chain(basis, basis_size, info, s_targets[t], out, (uint32_t)bytes);
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

static void check_fixture(const fixture_t *fx) {
    const char *name = fx->name;
    /* A codec this build does not decode is an asserted developer error, not a
       transcoder outcome: those fixtures are simply not part of this build's set. */
    if (!s_codec_enabled[fx->codec]) {
        return;
    }
    char file_name[96];
    (void)snprintf(file_name, sizeof(file_name), "%s.basis", name);
    uint32_t basis_size = 0;
    uint8_t *basis = read_file(file_name, &basis_size);

    nt_basisu_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE_MESSAGE(nt_basisu_info(basis, basis_size, &info), name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(fx->codec, info.codec, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx->w, info.width, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx->h, info.height, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx->levels, info.level_count, name);
    TEST_ASSERT_EQUAL_MESSAGE(fx->alpha, info.has_alpha, name);
    for (size_t t = 0; t < TARGET_COUNT; t++) {
        check_pair(name, basis, basis_size, &info, t);
    }
    free(basis);
}

void test_every_admitted_pair_matches_the_golden(void) {
    for (size_t i = 0; i < FIXTURE_COUNT; i++) {
        check_fixture(&s_fixtures[i]);
    }
    (void)printf("basisu trimmed: %u byte-identical (ETC1S=%d UASTC=%d ETC2=%d BC7=%d ASTC=%d)\n", s_identical, NT_BASISU_HAS_ETC1S, NT_BASISU_HAS_UASTC, NT_BASISU_HAS_ETC2, NT_BASISU_HAS_BC7,
                 NT_BASISU_HAS_ASTC);
}

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    RUN_TEST(test_every_admitted_pair_matches_the_golden);
    return UNITY_END();
}
