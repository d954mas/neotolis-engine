/* base64 util: known-vector round-trip incl. 0/1/2-byte padding edges +
 * cap-reject + empty input. Pure — nt_devapi_base64.c is compiled directly into
 * this target (no devapi core, no NT_DEVAPI_ENABLED gate). RFC 4648 vectors. */

#include "devapi/nt_devapi_base64.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Encode `in` into a NUL-terminated `out`; returns the encoded byte count. */
static uint32_t encode_str(const char *in, char *out, uint32_t out_cap) {
    uint32_t n = nt_devapi_base64_encode((const uint8_t *)in, (uint32_t)strlen(in), out, out_cap);
    out[n] = '\0';
    return n;
}

/* Test 1: 1 byte (%3==1) -> two '='. */
void test_base64_padding_two_equals(void) {
    char out[16];
    uint32_t n = encode_str("M", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(4U, n);
    TEST_ASSERT_EQUAL_STRING("TQ==", out);
}

/* Test 2: 2 bytes (%3==2) -> one '='. */
void test_base64_padding_one_equal(void) {
    char out[16];
    uint32_t n = encode_str("Ma", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(4U, n);
    TEST_ASSERT_EQUAL_STRING("TWE=", out);
}

/* Test 3: 3 bytes (%3==0) -> no padding. */
void test_base64_no_padding(void) {
    char out[16];
    uint32_t n = encode_str("Man", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(4U, n);
    TEST_ASSERT_EQUAL_STRING("TWFu", out);
}

/* Test 4: a longer known vector round-trips to its canonical RFC base64. */
void test_base64_long_vector(void) {
    char out[64];
    const char *in = "Many hands make light work.";
    uint32_t n = encode_str(in, out, sizeof(out));
    /* len 27 (%3==0) -> 36 chars, no padding. */
    TEST_ASSERT_EQUAL_UINT32(36U, n);
    TEST_ASSERT_EQUAL_STRING("TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu", out);
}

/* Test 5: out_cap one byte short of need -> returns 0, no write
 * past cap (the guard byte just past the intended output stays untouched). */
void test_base64_cap_reject(void) {
    const char *in = "Man"; /* need = ((3+2)/3)*4 = 4 */
    char buf[8];
    const char kGuard = (char)0x7F;
    memset(buf, kGuard, sizeof(buf));

    uint32_t n = nt_devapi_base64_encode((const uint8_t *)in, 3U, buf, 3U); /* cap 3 < need 4 */
    TEST_ASSERT_EQUAL_UINT32(0U, n);
    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_INT8(kGuard, buf[i]); /* nothing written */
    }
}

/* Test 6: empty input -> 0 bytes written, returns 0. */
void test_base64_empty_input(void) {
    char buf[8];
    const char kGuard = (char)0x7F;
    memset(buf, kGuard, sizeof(buf));

    uint32_t n = nt_devapi_base64_encode((const uint8_t *)"", 0U, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(0U, n);
    TEST_ASSERT_EQUAL_INT8(kGuard, buf[0]); /* nothing written */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_base64_padding_two_equals);
    RUN_TEST(test_base64_padding_one_equal);
    RUN_TEST(test_base64_no_padding);
    RUN_TEST(test_base64_long_vector);
    RUN_TEST(test_base64_cap_reject);
    RUN_TEST(test_base64_empty_input);
    return UNITY_END();
}
