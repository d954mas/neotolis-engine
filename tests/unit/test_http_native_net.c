/* Real-network (loopback) acceptance for the native (libcurl) nt_http backend.
 *
 * Under ctest, tests/tools/run_http_net_test.py starts the echo server around the
 * exe. Manual run:
 *   python tests/tools/http_echo_server.py           (port 8124)
 *   build/tests/<preset>/test_http_native_net
 * Base URL override: NT_HTTP_TEST_BASE (default http://127.0.0.1:8124). */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void net_sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void net_sleep_ms(unsigned ms) {
    struct timespec ts = {.tv_sec = ms / 1000U, .tv_nsec = (long)(ms % 1000U) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

/* clang-format off */
#include "http/nt_http.h"
#include "unity.h"
/* clang-format on */

static char s_url[512];

static const char *base_url(void) {
    const char *env = getenv("NT_HTTP_TEST_BASE"); /* NOLINT(concurrency-mt-unsafe) — single-threaded test */
    return env != NULL ? env : "http://127.0.0.1:8124";
}

static const char *make_url(const char *path) {
    (void)snprintf(s_url, sizeof(s_url), "%s%s", base_url(), path);
    return s_url;
}

/* Pumps until the request leaves PENDING/DOWNLOADING or ~10 s pass. */
static nt_http_state_t pump_to_completion(nt_http_request_t req) {
    for (int i = 0; i < 1000; i++) {
        nt_http_update();
        nt_http_state_t st = nt_http_state(req);
        if (st == NT_HTTP_STATE_DONE || st == NT_HTTP_STATE_FAILED) {
            return st;
        }
        net_sleep_ms(10);
    }
    return nt_http_state(req);
}

void setUp(void) { TEST_ASSERT_EQUAL(NT_OK, nt_http_init()); }

void tearDown(void) { nt_http_shutdown(); }

/* ---- Tests ---- */

static void test_get_hello(void) {
    nt_http_request_t req = nt_http_request(make_url("/hello"));
    TEST_ASSERT_NOT_EQUAL(0, req.id);
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_DONE, pump_to_completion(req));
    TEST_ASSERT_EQUAL(200, nt_http_status(req));

    const char *headers = nt_http_response_headers(req);
    TEST_ASSERT_NOT_NULL(headers);
    TEST_ASSERT_NOT_NULL(strstr(headers, "x-nt-server: echo"));
    TEST_ASSERT_NOT_NULL(strstr(headers, "content-type: text/plain"));

    uint32_t size = 0;
    uint8_t *data = nt_http_take_data(req, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL(strlen("hello-neotolis"), size);
    TEST_ASSERT_EQUAL(0, memcmp(data, "hello-neotolis", size));
    free(data);
    nt_http_free(req);
}

static void test_post_echo_binary_body(void) {
    uint8_t body[300];
    for (uint32_t i = 0; i < sizeof(body); i++) {
        body[i] = (uint8_t)(i * 7U); /* includes 0x00 bytes — catches string-truncating paths */
    }
    const char *hdrs[] = {"X-NT-Test", "neotolis"};
    nt_http_options_t opts = {
        .method = "POST",
        .body = body,
        .body_size = (uint32_t)sizeof(body),
        .content_type = "application/json",
        .headers = hdrs,
        .header_count = 1,
    };
    nt_http_request_t req = nt_http_request_ex(make_url("/echo"), &opts);
    TEST_ASSERT_NOT_EQUAL(0, req.id);
    /* The module copied the body at call time — clobber the caller's buffer */
    memset(body, 0xAA, sizeof(body));

    TEST_ASSERT_EQUAL(NT_HTTP_STATE_DONE, pump_to_completion(req));
    TEST_ASSERT_EQUAL(200, nt_http_status(req));

    const char *headers = nt_http_response_headers(req);
    TEST_ASSERT_NOT_NULL(headers);
    TEST_ASSERT_NOT_NULL(strstr(headers, "x-echo-content-type: application/json"));
    TEST_ASSERT_NOT_NULL(strstr(headers, "x-echo-x-nt-test: neotolis"));

    uint32_t size = 0;
    uint8_t *data = nt_http_take_data(req, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL(300, (int32_t)size);
    for (uint32_t i = 0; i < size; i++) {
        TEST_ASSERT_EQUAL((uint8_t)(i * 7U), data[i]);
    }
    free(data);
    nt_http_free(req);
}

static void test_non_2xx_is_done_with_status(void) {
    nt_http_request_t req = nt_http_request(make_url("/status404"));
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_DONE, pump_to_completion(req));
    TEST_ASSERT_EQUAL(404, nt_http_status(req));

    uint32_t size = 0;
    uint8_t *data = nt_http_take_data(req, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL(strlen("missing"), size);
    free(data);
    nt_http_free(req);
}

static void test_timeout_fails(void) {
    nt_http_options_t opts = {.timeout_ms = 300};
    nt_http_request_t req = nt_http_request_ex(make_url("/slow"), &opts);
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_FAILED, pump_to_completion(req));
    nt_http_free(req);
}

static void test_connection_refused_fails(void) {
    /* Port 1 is reliably closed on loopback */
    nt_http_request_t req = nt_http_request("http://127.0.0.1:1/nope");
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_FAILED, pump_to_completion(req));
    TEST_ASSERT_EQUAL(0, nt_http_status(req));
    nt_http_free(req);
}

static void test_cancel_in_flight(void) {
    nt_http_request_t req = nt_http_request(make_url("/slow"));
    TEST_ASSERT_NOT_EQUAL(0, req.id);
    nt_http_update();
    nt_http_free(req); /* cancels the transfer; slot reusable */

    nt_http_request_t again = nt_http_request(make_url("/hello"));
    TEST_ASSERT_EQUAL(NT_HTTP_STATE_DONE, pump_to_completion(again));
    nt_http_free(again);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_hello);
    RUN_TEST(test_post_echo_binary_body);
    RUN_TEST(test_non_2xx_is_done_with_status);
    RUN_TEST(test_timeout_fails);
    RUN_TEST(test_connection_refused_fails);
    RUN_TEST(test_cancel_in_flight);
    return UNITY_END();
}
