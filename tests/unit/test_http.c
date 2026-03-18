/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "http/nt_http.h"
#include "unity.h"
/* clang-format on */

void setUp(void) { TEST_ASSERT_EQUAL(NT_OK, nt_http_init()); }

void tearDown(void) { nt_http_shutdown(); }

/* ---- Tests ---- */

static void test_init_shutdown(void) {
    /* setUp already called init successfully.
       Double init should fail. */
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_http_init());
    /* tearDown will call shutdown */
}

static void test_request_returns_handle(void) {
    nt_http_request_t req = nt_http_request("http://example.com/test.bin");
    TEST_ASSERT_NOT_EQUAL(0, req.id);
}

static void test_stub_immediately_fails(void) {
    nt_http_request_t req = nt_http_request("http://example.com/test.bin");
    TEST_ASSERT_NOT_EQUAL(0, req.id);
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_FAILED, nt_http_state(req));
}

static void test_state_invalid_handle(void) { TEST_ASSERT_EQUAL(NT_HTTP_STATE_NONE, nt_http_state(NT_HTTP_REQUEST_INVALID)); }

static void test_take_data_on_failed(void) {
    nt_http_request_t req = nt_http_request("http://example.com/test.bin");
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_FAILED, nt_http_state(req));

    uint32_t size = 999;
    uint8_t *data = nt_http_take_data(req, &size);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL(0, size);
}

static void test_free_releases_slot(void) {
    nt_http_request_t first = nt_http_request("http://example.com/a.bin");
    TEST_ASSERT_NOT_EQUAL(0, first.id);

    nt_http_free(first);

    /* After free, we should be able to allocate again */
    nt_http_request_t second = nt_http_request("http://example.com/b.bin");
    TEST_ASSERT_NOT_EQUAL(0, second.id);

    /* Generation should have changed (handles are different) */
    TEST_ASSERT_NOT_EQUAL(first.id, second.id);
}

static void test_stale_handle_rejected(void) {
    nt_http_request_t req = nt_http_request("http://example.com/a.bin");
    TEST_ASSERT_NOT_EQUAL(0, req.id);

    nt_http_free(req);

    /* Stale handle should return NONE */
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_NONE, nt_http_state(req));
}

static void test_max_requests_exhausted(void) {
    nt_http_request_t handles[NT_HTTP_MAX_REQUESTS];

    /* Allocate all available slots */
    for (int i = 0; i < NT_HTTP_MAX_REQUESTS; i++) {
        char url[64];
        (void)snprintf(url, sizeof(url), "http://example.com/%d.bin", i);
        handles[i] = nt_http_request(url);
        TEST_ASSERT_NOT_EQUAL(0, handles[i].id);
    }

    /* Next request should return INVALID */
    nt_http_request_t overflow = nt_http_request("http://example.com/overflow.bin");
    TEST_ASSERT_EQUAL(0, overflow.id);

    /* Free all */
    for (int i = 0; i < NT_HTTP_MAX_REQUESTS; i++) {
        nt_http_free(handles[i]);
    }
}

static void test_progress_invalid_handle(void) {
    uint32_t received = 999;
    uint32_t total = 999;
    nt_http_progress(NT_HTTP_REQUEST_INVALID, &received, &total);
    TEST_ASSERT_EQUAL(0, received);
    TEST_ASSERT_EQUAL(0, total);
}

static void test_progress_null_pointers(void) {
    nt_http_request_t req = nt_http_request("http://example.com/test.bin");
    /* Should not crash when passing NULL pointers */
    nt_http_progress(req, NULL, NULL);
    nt_http_free(req);
}

static void test_request_null_url(void) {
    nt_http_request_t req = nt_http_request(NULL);
    TEST_ASSERT_EQUAL(0, req.id);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_request_returns_handle);
    RUN_TEST(test_stub_immediately_fails);
    RUN_TEST(test_state_invalid_handle);
    RUN_TEST(test_take_data_on_failed);
    RUN_TEST(test_free_releases_slot);
    RUN_TEST(test_stale_handle_rejected);
    RUN_TEST(test_max_requests_exhausted);
    RUN_TEST(test_progress_invalid_handle);
    RUN_TEST(test_progress_null_pointers);
    RUN_TEST(test_request_null_url);
    return UNITY_END();
}
