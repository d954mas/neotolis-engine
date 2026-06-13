/* SC1 / PROTO-01: cJSON is usable standalone, WITHOUT devapi.
   Includes cJSON.h directly — no nt_devapi.h. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "cJSON.h"
#include "unity.h"
/* clang-format on */

void setUp(void) {}

void tearDown(void) {}

static void test_version_is_1_7_19(void) {
    TEST_ASSERT_EQUAL_INT(1, CJSON_VERSION_MAJOR);
    TEST_ASSERT_EQUAL_INT(7, CJSON_VERSION_MINOR);
    TEST_ASSERT_EQUAL_INT(19, CJSON_VERSION_PATCH);
}

static void test_parse_read_field(void) {
    cJSON *root = cJSON_Parse("{\"name\":\"neotolis\",\"count\":42,\"ok\":true}");
    TEST_ASSERT_NOT_NULL(root);

    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    TEST_ASSERT_TRUE(cJSON_IsString(name));
    TEST_ASSERT_EQUAL_STRING("neotolis", name->valuestring);

    cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
    TEST_ASSERT_TRUE(cJSON_IsNumber(count));
    TEST_ASSERT_EQUAL_INT(42, count->valueint);

    cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    TEST_ASSERT_TRUE(cJSON_IsBool(ok));
    TEST_ASSERT_TRUE(cJSON_IsTrue(ok));

    cJSON_Delete(root);
}

static void test_print_reparse_round_trip(void) {
    cJSON *root = cJSON_Parse("{\"name\":\"neotolis\",\"count\":42}");
    TEST_ASSERT_NOT_NULL(root);

    char *printed = cJSON_PrintUnformatted(root);
    TEST_ASSERT_NOT_NULL(printed);

    cJSON *reparsed = cJSON_Parse(printed);
    TEST_ASSERT_NOT_NULL(reparsed);

    cJSON *name = cJSON_GetObjectItemCaseSensitive(reparsed, "name");
    TEST_ASSERT_EQUAL_STRING("neotolis", name->valuestring);
    cJSON *count = cJSON_GetObjectItemCaseSensitive(reparsed, "count");
    TEST_ASSERT_EQUAL_INT(42, count->valueint);

    cJSON_free(printed);
    cJSON_Delete(reparsed);
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_version_is_1_7_19);
    RUN_TEST(test_parse_read_field);
    RUN_TEST(test_print_reparse_round_trip);
    return UNITY_END();
}
