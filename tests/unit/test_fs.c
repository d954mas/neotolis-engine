/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "fs/nt_fs.h"
#include "unity.h"
/* clang-format on */

void setUp(void) { TEST_ASSERT_EQUAL(NT_OK, nt_fs_init()); }

void tearDown(void) { nt_fs_shutdown(); }

/* ---- Tests ---- */

static void test_init_shutdown(void) {
    /* setUp already called init successfully.
       Double init should fail. */
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_fs_init());
    /* tearDown will call shutdown */
}

static void test_read_existing_file(void) {
    nt_fs_request_t req = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_NOT_EQUAL(0, req.id);
    TEST_ASSERT_EQUAL(NT_FS_STATE_DONE, nt_fs_state(req));
    nt_fs_free(req);
}

static void test_read_nonexistent_file(void) {
    nt_fs_request_t req = nt_fs_read_file("nonexistent_file_12345.bin");
    TEST_ASSERT_NOT_EQUAL(0, req.id);
    TEST_ASSERT_EQUAL(NT_FS_STATE_FAILED, nt_fs_state(req));
    nt_fs_free(req);
}

static void test_take_data_returns_content(void) {
    nt_fs_request_t req = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_EQUAL(NT_FS_STATE_DONE, nt_fs_state(req));

    uint32_t size = 0;
    uint8_t *data = nt_fs_take_data(req, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN(0, size);

    /* CMakeLists.txt should start with something reasonable */
    TEST_ASSERT_TRUE(size > 5);

    free(data);
    nt_fs_free(req);
}

static void test_take_data_transfers_ownership(void) {
    nt_fs_request_t req = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_EQUAL(NT_FS_STATE_DONE, nt_fs_state(req));

    uint32_t size1 = 0;
    uint8_t *data1 = nt_fs_take_data(req, &size1);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_GREATER_THAN(0, size1);

    /* Second take should return NULL (data was transferred) */
    uint32_t size2 = 999;
    uint8_t *data2 = nt_fs_take_data(req, &size2);
    TEST_ASSERT_NULL(data2);
    TEST_ASSERT_EQUAL(0, size2);

    free(data1);
    nt_fs_free(req);
}

static void test_free_without_take(void) {
    /* Request a file, then free without taking data.
       This should free the data internally without leaking. */
    nt_fs_request_t req = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_EQUAL(NT_FS_STATE_DONE, nt_fs_state(req));
    nt_fs_free(req);
    /* If ASan is enabled, a leak here would be caught */
}

static void test_free_after_take(void) {
    nt_fs_request_t req = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_EQUAL(NT_FS_STATE_DONE, nt_fs_state(req));

    uint32_t size = 0;
    uint8_t *data = nt_fs_take_data(req, &size);
    TEST_ASSERT_NOT_NULL(data);

    /* Free the request (data already taken, should not double-free) */
    nt_fs_free(req);

    /* Free the data we own */
    free(data);
}

static void test_stale_handle_rejected(void) {
    nt_fs_request_t req = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_NOT_EQUAL(0, req.id);

    nt_fs_free(req);

    /* Stale handle should return NONE */
    TEST_ASSERT_EQUAL(NT_FS_STATE_NONE, nt_fs_state(req));
}

static void test_max_requests_exhausted(void) {
    nt_fs_request_t handles[NT_FS_MAX_REQUESTS];

    /* Allocate all available slots */
    for (int i = 0; i < NT_FS_MAX_REQUESTS; i++) {
        handles[i] = nt_fs_read_file("CMakeLists.txt");
        TEST_ASSERT_NOT_EQUAL(0, handles[i].id);
    }

    /* Next request should return INVALID */
    nt_fs_request_t overflow = nt_fs_read_file("CMakeLists.txt");
    TEST_ASSERT_EQUAL(0, overflow.id);

    /* Free all */
    for (int i = 0; i < NT_FS_MAX_REQUESTS; i++) {
        nt_fs_free(handles[i]);
    }
}

static void test_state_invalid_handle(void) { TEST_ASSERT_EQUAL(NT_FS_STATE_NONE, nt_fs_state(NT_FS_REQUEST_INVALID)); }

static void test_read_null_path(void) {
    nt_fs_request_t req = nt_fs_read_file(NULL);
    TEST_ASSERT_EQUAL(0, req.id);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_read_existing_file);
    RUN_TEST(test_read_nonexistent_file);
    RUN_TEST(test_take_data_returns_content);
    RUN_TEST(test_take_data_transfers_ownership);
    RUN_TEST(test_free_without_take);
    RUN_TEST(test_free_after_take);
    RUN_TEST(test_stale_handle_rejected);
    RUN_TEST(test_max_requests_exhausted);
    RUN_TEST(test_state_invalid_handle);
    RUN_TEST(test_read_null_path);
    return UNITY_END();
}
