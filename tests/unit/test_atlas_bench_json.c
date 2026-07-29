#include "bench_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "unity.h"

#define TMP_DIR "build/tests/tmp"

void setUp(void) {}
void tearDown(void) {}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, 0, SEEK_END));
    const long length = ftell(file);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, 0, SEEK_SET));
    /* Hard bound, not the assert above: a Unity macro is opaque to the analyzer. */
    const size_t size = (length > 0) ? (size_t)length : 0U;
    char *text = (char *)malloc(size + 1U);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_size_t(size, fread(text, 1, size, file));
    (void)fclose(file);
    text[size] = '\0';
    return text;
}

void test_writer_versions_numeric_transform_schema(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_bench_json_contract.json";
    nt_bench_run_t run = {0};
    (void)snprintf(run.tool_version, sizeof(run.tool_version), "2.0.0");
    (void)snprintf(run.opts_shape, sizeof(run.opts_shape), "concave");
    run.opts_allowed_transforms = 0x21U;

    TEST_ASSERT_EQUAL_INT(3, NT_BENCH_JSON_SCHEMA_VERSION);
    TEST_ASSERT_EQUAL_INT(0, nt_bench_write_json(path, &run));
    char *json = read_text(path);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"schema_version\": 3"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"allowed_transforms\": 33"));
    TEST_ASSERT_NULL(strstr(json, "\"allow_transform\""));
    free(json);
    (void)remove(path);
}

/* Distinct values per field, so a copy-paste in the emitter shows up as a value
 * mismatch rather than a present-key pass. */
void test_writer_emits_dedup_stats_fields(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_bench_json_stats.json";
    nt_bench_run_t run = {0};
    run.atlas_count = 1U;
    nt_bench_atlas_result_t *a = &run.atlases[0];
    (void)snprintf(a->name, sizeof(a->name), "stats_probe");
    a->sprites = 30U;
    a->unique = 18U;
    a->folds_exact = 12U;
    a->folds_d4 = 3U;
    a->area_saved_px = 1152U;
    a->vertex_blocks_shared = 7U;

    TEST_ASSERT_EQUAL_INT(0, nt_bench_write_json(path, &run));
    char *json = read_text(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "\"unique\": 18"), "unique must be emitted from the run, not hard-coded");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "\"folds_exact\": 12"), "folds_exact must be emitted");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "\"folds_d4\": 3"), "folds_d4 must be emitted");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "\"area_saved_px\": 1152"), "area_saved_px must be emitted");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, "\"vertex_blocks_shared\": 7"), "vertex_blocks_shared must be emitted");
    free(json);
    (void)remove(path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_writer_versions_numeric_transform_schema);
    RUN_TEST(test_writer_emits_dedup_stats_fields);
    return UNITY_END();
}
