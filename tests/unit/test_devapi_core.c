/* Core command group — ping / engine.info / view via submit(). */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "window/nt_window.h"
#include "unity.h"
/* clang-format on */

/* nt_window.h declares g_nt_window extern; provide the definition for the test. */
nt_window_t g_nt_window;

void setUp(void) {
    /* Known window state so cmd_view assertions are deterministic. */
    g_nt_window = (nt_window_t){0};
    g_nt_window.fb_width = 1920;
    g_nt_window.fb_height = 1080;
    g_nt_window.width = 960;
    g_nt_window.height = 540;
    g_nt_window.dpr = 2.5F; /* fractional on purpose: catches int-truncation of dpr. */

    /* init auto-registers the core group (NT_DEVAPI_REGISTER_core). */
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
}

void tearDown(void) { nt_devapi_shutdown(); }

static void test_ping(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"ping\"}");
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "pong")));
    cJSON_Delete(root);
}

static void test_view_reflects_window(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"view\"}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));
    TEST_ASSERT_EQUAL_INT(1920, cJSON_GetObjectItemCaseSensitive(result, "fb_width")->valueint);
    TEST_ASSERT_EQUAL_INT(1080, cJSON_GetObjectItemCaseSensitive(result, "fb_height")->valueint);
    TEST_ASSERT_EQUAL_INT(960, cJSON_GetObjectItemCaseSensitive(result, "width")->valueint);
    TEST_ASSERT_EQUAL_INT(540, cJSON_GetObjectItemCaseSensitive(result, "height")->valueint);
    /* dpr must be the full fractional value, not int-truncated to 2 (valueint would pass 2.5). */
    double dpr = cJSON_GetObjectItemCaseSensitive(result, "dpr")->valuedouble;
    TEST_ASSERT_TRUE(dpr > 2.49 && dpr < 2.51);
    cJSON_Delete(root);
}

static bool array_has_string(const cJSON *arr, const char *want) {
    const cJSON *m = NULL;
    cJSON_ArrayForEach(m, arr) {
        if (cJSON_IsString(m) && strcmp(m->valuestring, want) == 0) {
            return true;
        }
    }
    return false;
}

static void test_engine_info(void) {
    const char *resp = nt_devapi_submit("{\"method\":\"engine.info\"}");
    cJSON *root = cJSON_Parse(resp);
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_TRUE(cJSON_IsObject(result));

    /* version comes from the central nt_engine_version_string(). */
    TEST_ASSERT_EQUAL_STRING(nt_engine_version_string(), cJSON_GetObjectItemCaseSensitive(result, "version")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(result, "build")));
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(result, "preset")));

    /* "modules" is the active compiled-group list — must contain "core". */
    cJSON *modules = cJSON_GetObjectItemCaseSensitive(result, "modules");
    TEST_ASSERT_TRUE(cJSON_IsArray(modules));
    TEST_ASSERT_TRUE(array_has_string(modules, "core"));
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ping);
    RUN_TEST(test_view_reflects_window);
    RUN_TEST(test_engine_info);
    return UNITY_END();
}
