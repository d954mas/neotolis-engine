/* record.* group via submit() against a fake nt_recorder: a start while the recorder is not ready
   answers not_ready, a refusal once ready answers failed, and record.stop reports the stats. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "devapi/nt_devapi_groups.h"
#include "devapi/nt_devapi_internal.h"
#include "recorder/nt_recorder.h"
#include "unity.h"
/* clang-format on */

static struct {
    bool ready;
    bool refuse;
    bool active;
    uint32_t starts;
    nt_recorder_stats_t stats;
} s_fake;

bool nt_recorder_ready(void) { return s_fake.ready; }

bool nt_recorder_start(const char *name, uint32_t fps) {
    (void)name;
    (void)fps;
    s_fake.starts++;
    s_fake.active = !s_fake.refuse;
    return s_fake.active;
}

nt_recorder_stats_t nt_recorder_stop(void) {
    s_fake.active = false;
    return s_fake.stats;
}

bool nt_recorder_active(void) { return s_fake.active; }

void setUp(void) {
    memset(&s_fake, 0, sizeof s_fake);
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_recorder();
}

void tearDown(void) { nt_devapi_shutdown(); }

static cJSON *submit(const char *line) {
    const char *resp = nt_devapi_submit(line);
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    return root;
}

static void assert_error(const char *line, const char *code) {
    cJSON *root = submit(line);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    TEST_ASSERT_EQUAL_STRING(code, cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "error"), "code")->valuestring);
    cJSON_Delete(root);
}

#define START_LINE "{\"method\":\"record.start\",\"request_id\":1,\"params\":{\"name\":\"a.mp4\",\"fps\":30}}"

static void test_start_while_not_ready_answers_not_ready(void) {
    assert_error(START_LINE, "not_ready");
    TEST_ASSERT_EQUAL_UINT32(0, s_fake.starts);
}

static void test_refusal_once_ready_answers_failed(void) {
    s_fake.ready = true;
    s_fake.refuse = true;
    assert_error(START_LINE, "failed");
    TEST_ASSERT_EQUAL_UINT32(1, s_fake.starts);
}

static void test_recording_answers_before_not_ready(void) {
    s_fake.active = true;
    assert_error(START_LINE, "recording");
    TEST_ASSERT_EQUAL_UINT32(0, s_fake.starts);
}

static void test_stop_reports_frames_and_drift_ms(void) {
    s_fake.ready = true;
    cJSON_Delete(submit(START_LINE));
    s_fake.stats = (nt_recorder_stats_t){.frames = 42, .max_drift = -0.125};
    cJSON *root = submit("{\"method\":\"record.stop\",\"request_id\":2}");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    TEST_ASSERT_EQUAL_INT(42, cJSON_GetObjectItemCaseSensitive(result, "frames")->valueint);
    TEST_ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(result, "max_drift_ms")->valuedouble == -125.0);
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_start_while_not_ready_answers_not_ready);
    RUN_TEST(test_refusal_once_ready_answers_failed);
    RUN_TEST(test_recording_answers_before_not_ready);
    RUN_TEST(test_stop_reports_frames_and_drift_ms);
    return UNITY_END();
}
