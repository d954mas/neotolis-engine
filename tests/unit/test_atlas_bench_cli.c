/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC. */
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "atlas_bench_cli.h"
#include "unity.h"

#define TMP_PREFIX "build/tests/tmp/atlas_bench_cli"

void setUp(void) {}

void tearDown(void) {
    (void)remove(TMP_PREFIX ".json");
    (void)remove(TMP_PREFIX ".json.ntpack");
    (void)remove(TMP_PREFIX ".json.h");
    (void)remove(TMP_PREFIX ".json.baseline.ntpack");
    (void)remove(TMP_PREFIX ".json.baseline.h");
}

static void write_marker(const char *path) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT(0, fputs("marker", file) < 0 ? -1 : 0);
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
}

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

void test_parse_u32_strict_accepts_decimal_range(void) {
    uint32_t value = 0U;
    TEST_ASSERT_TRUE(atlas_bench_parse_u32_strict("0", &value));
    TEST_ASSERT_EQUAL_UINT32(0U, value);
    TEST_ASSERT_TRUE(atlas_bench_parse_u32_strict("4294967295", &value));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
}

void test_parse_u32_strict_rejects_partial_signed_and_overflow(void) {
    uint32_t value = 7U;
    const char *invalid[] = {NULL, "", "1px", " 1", "+1", "-1", "4294967296"};
    for (uint32_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        TEST_ASSERT_FALSE(atlas_bench_parse_u32_strict(invalid[i], &value));
    }
    TEST_ASSERT_FALSE(atlas_bench_parse_u32_strict("1", NULL));
}

void test_parse_max_size_enforces_builder_range(void) {
    uint32_t value = 0U;
    TEST_ASSERT_FALSE(atlas_bench_parse_max_size("0", &value));
    TEST_ASSERT_TRUE(atlas_bench_parse_max_size("1", &value));
    TEST_ASSERT_EQUAL_UINT32(1U, value);
    TEST_ASSERT_TRUE(atlas_bench_parse_max_size("16384", &value));
    TEST_ASSERT_EQUAL_UINT32(16384U, value);
    TEST_ASSERT_FALSE(atlas_bench_parse_max_size("16385", &value));
    TEST_ASSERT_FALSE(atlas_bench_parse_max_size("16384px", &value));
}

void test_pre_write_failure_preserves_existing_json_and_removes_both_pack_pairs(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR("build/tests/tmp");
    write_marker(TMP_PREFIX ".json");
    write_marker(TMP_PREFIX ".json.ntpack");
    write_marker(TMP_PREFIX ".json.h");
    write_marker(TMP_PREFIX ".json.baseline.ntpack");
    write_marker(TMP_PREFIX ".json.baseline.h");

    atlas_bench_cleanup_outputs(TMP_PREFIX ".json", TMP_PREFIX ".json.baseline.ntpack", TMP_PREFIX ".json.ntpack", false, false);

    TEST_ASSERT_TRUE(file_exists(TMP_PREFIX ".json"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.ntpack"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.h"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.baseline.ntpack"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.baseline.h"));
}

void test_failed_json_write_cleanup_removes_partial_json_and_both_pack_pairs(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR("build/tests/tmp");
    write_marker(TMP_PREFIX ".json");
    write_marker(TMP_PREFIX ".json.ntpack");
    write_marker(TMP_PREFIX ".json.h");
    write_marker(TMP_PREFIX ".json.baseline.ntpack");
    write_marker(TMP_PREFIX ".json.baseline.h");

    atlas_bench_cleanup_outputs(TMP_PREFIX ".json", TMP_PREFIX ".json.baseline.ntpack", TMP_PREFIX ".json.ntpack", false, true);

    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.ntpack"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.h"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.baseline.ntpack"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.baseline.h"));
}

void test_success_cleanup_keeps_selected_pack_pair(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR("build/tests/tmp");
    write_marker(TMP_PREFIX ".json");
    write_marker(TMP_PREFIX ".json.ntpack");
    write_marker(TMP_PREFIX ".json.h");
    write_marker(TMP_PREFIX ".json.baseline.ntpack");
    write_marker(TMP_PREFIX ".json.baseline.h");

    atlas_bench_cleanup_outputs(TMP_PREFIX ".json", TMP_PREFIX ".json.baseline.ntpack", TMP_PREFIX ".json.ntpack", true, false);

    TEST_ASSERT_TRUE(file_exists(TMP_PREFIX ".json"));
    TEST_ASSERT_TRUE(file_exists(TMP_PREFIX ".json.ntpack"));
    TEST_ASSERT_TRUE(file_exists(TMP_PREFIX ".json.h"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.baseline.ntpack"));
    TEST_ASSERT_FALSE(file_exists(TMP_PREFIX ".json.baseline.h"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_u32_strict_accepts_decimal_range);
    RUN_TEST(test_parse_u32_strict_rejects_partial_signed_and_overflow);
    RUN_TEST(test_parse_max_size_enforces_builder_range);
    RUN_TEST(test_pre_write_failure_preserves_existing_json_and_removes_both_pack_pairs);
    RUN_TEST(test_failed_json_write_cleanup_removes_partial_json_and_both_pack_pairs);
    RUN_TEST(test_success_cleanup_keeps_selected_pack_pair);
    return UNITY_END();
}
