/* nt_base64 encoder: known-vector round-trip incl. 0/1/2-byte padding edges + cap-reject + empty
 * input. Links the standalone nt_base64 module (no devapi). RFC 4648 vectors. */

#include "base64/nt_base64.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Encode `in` into a NUL-terminated `out`; returns the encoded byte count. */
static uint32_t encode_str(const char *in, char *out, uint32_t out_cap) {
    uint32_t n = nt_base64_encode((const uint8_t *)in, (uint32_t)strlen(in), out, out_cap);
    out[n] = '\0';
    return n;
}

/* 1 byte (%3==1) -> two '='. */
void test_base64_padding_two_equals(void) {
    char out[4];
    uint32_t n = nt_base64_encode((const uint8_t *)"M", 1U, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(4U, n);
    TEST_ASSERT_EQUAL_MEMORY("TQ==", out, sizeof(out));
}

/* 2 bytes (%3==2) -> one '='. */
void test_base64_padding_one_equal(void) {
    char out[4];
    uint32_t n = nt_base64_encode((const uint8_t *)"Ma", 2U, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(4U, n);
    TEST_ASSERT_EQUAL_MEMORY("TWE=", out, sizeof(out));
}

/* 3 bytes (%3==0) -> no padding. */
void test_base64_no_padding(void) {
    char out[4];
    uint32_t n = nt_base64_encode((const uint8_t *)"Man", 3U, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(4U, n);
    TEST_ASSERT_EQUAL_MEMORY("TWFu", out, sizeof(out));
}

/* A longer known vector round-trips to its canonical RFC base64. */
void test_base64_long_vector(void) {
    char out[64];
    const char *in = "Many hands make light work.";
    uint32_t n = encode_str(in, out, sizeof(out));
    /* len 27 (%3==0) -> 36 chars, no padding. */
    TEST_ASSERT_EQUAL_UINT32(36U, n);
    TEST_ASSERT_EQUAL_STRING("TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu", out);
}

/* out_cap one byte short of need -> returns 0, no write past cap
 * (the guard byte just past the intended output stays untouched). */
void test_base64_cap_reject(void) {
    const char *in = "Man"; /* need = ((3+2)/3)*4 = 4 */
    char buf[8];
    const char kGuard = (char)0x7F;
    memset(buf, kGuard, sizeof(buf));

    uint32_t n = nt_base64_encode((const uint8_t *)in, 3U, buf, 3U); /* cap 3 < need 4 */
    TEST_ASSERT_EQUAL_UINT32(0U, n);
    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_INT8(kGuard, buf[i]); /* nothing written */
    }
}

/* empty input -> 0 bytes written, returns 0. */
void test_base64_empty_input(void) {
    char buf[8];
    const char kGuard = (char)0x7F;
    memset(buf, kGuard, sizeof(buf));

    uint32_t n = nt_base64_encode((const uint8_t *)"", 0U, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(0U, n);
    TEST_ASSERT_EQUAL_INT8(kGuard, buf[0]); /* nothing written */
}

void test_base64_rejects_overflowing_output_size(void) {
    const uint32_t lengths[] = {UINT32_MAX, UINT32_MAX - 1U, 0xC0000000U, 0xBFFFFFFEU};
    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
        /* Impossible output sizes must reject before touching either buffer. */
        TEST_ASSERT_EQUAL_UINT32(0U, nt_base64_encode(NULL, lengths[i], NULL, UINT32_MAX));
    }
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_base64_padding_two_equals);
    RUN_TEST(test_base64_padding_one_equal);
    RUN_TEST(test_base64_no_padding);
    RUN_TEST(test_base64_long_vector);
    RUN_TEST(test_base64_cap_reject);
    RUN_TEST(test_base64_empty_input);
    RUN_TEST(test_base64_rejects_overflowing_output_size);
    return UNITY_END();
}
