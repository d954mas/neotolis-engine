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
    char *text = (char *)malloc((size_t)length + 1U);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_size_t((size_t)length, fread(text, 1, (size_t)length, file));
    (void)fclose(file);
    text[length] = '\0';
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

    TEST_ASSERT_EQUAL_INT(2, NT_BENCH_JSON_SCHEMA_VERSION);
    TEST_ASSERT_EQUAL_INT(0, nt_bench_write_json(path, &run));
    char *json = read_text(path);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"schema_version\": 2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"allowed_transforms\": 33"));
    TEST_ASSERT_NULL(strstr(json, "\"allow_transform\""));
    free(json);
    (void)remove(path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_writer_versions_numeric_transform_schema);
    return UNITY_END();
}
