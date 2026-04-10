#include "unity.h"

#include "hash/nt_hash.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {
    nt_hash_desc_t desc = {0};
    nt_hash_init(&desc);
}

void tearDown(void) { nt_hash_shutdown(); }

/* ---- Known vector tests ---- */

void test_hash32_known_vector(void) {
    nt_hash32_t h = nt_hash32("hello", 5);
    TEST_ASSERT_EQUAL_HEX32(0xFB0077F9, h.value);
}

void test_hash64_known_vector(void) {
    nt_hash64_t h = nt_hash64("hello", 5);
    TEST_ASSERT_EQUAL_HEX64(0x26C7827D889F6DA3ULL, h.value);
}

/* ---- String wrapper tests ---- */

void test_hash32_str_wrapper(void) {
    nt_hash32_t h_str = nt_hash32_str("test");
    nt_hash32_t h_bin = nt_hash32("test", 4);
    TEST_ASSERT_EQUAL_HEX32(h_bin.value, h_str.value);
}

void test_hash64_str_wrapper(void) {
    nt_hash64_t h_str = nt_hash64_str("test");
    nt_hash64_t h_bin = nt_hash64("test", 4);
    TEST_ASSERT_EQUAL_HEX64(h_bin.value, h_str.value);
}

/* ---- Empty string tests ---- */

void test_hash32_empty(void) {
    nt_hash32_t h = nt_hash32_str("");
    TEST_ASSERT_EQUAL_HEX32(0x02CC5D05U, h.value);
}

void test_hash64_empty(void) {
    nt_hash64_t h = nt_hash64_str("");
    TEST_ASSERT_EQUAL_HEX64(0xEF46DB3751D8E999ULL, h.value);
}

/* ---- Determinism tests ---- */

void test_hash32_deterministic(void) {
    nt_hash32_t a = nt_hash32_str("determinism");
    nt_hash32_t b = nt_hash32_str("determinism");
    TEST_ASSERT_EQUAL_HEX32(a.value, b.value);
}

void test_hash64_deterministic(void) {
    nt_hash64_t a = nt_hash64_str("determinism");
    nt_hash64_t b = nt_hash64_str("determinism");
    TEST_ASSERT_EQUAL_HEX64(a.value, b.value);
}

/* ---- Distribution tests ---- */

void test_hash32_distribution(void) {
    nt_hash32_t ha = nt_hash32_str("a");
    nt_hash32_t hb = nt_hash32_str("b");
    nt_hash32_t hc = nt_hash32_str("c");
    TEST_ASSERT_NOT_EQUAL(ha.value, hb.value);
    TEST_ASSERT_NOT_EQUAL(hb.value, hc.value);
    TEST_ASSERT_NOT_EQUAL(ha.value, hc.value);
}

void test_hash64_distribution(void) {
    nt_hash64_t ha = nt_hash64_str("a");
    nt_hash64_t hb = nt_hash64_str("b");
    nt_hash64_t hc = nt_hash64_str("c");
    TEST_ASSERT(ha.value != hb.value);
    TEST_ASSERT(hb.value != hc.value);
    TEST_ASSERT(ha.value != hc.value);
}

/* ---- Binary data test ---- */

void test_hash_binary_data(void) {
    uint8_t data[] = {0x00, 0xFF, 0x80, 0x01, 0xFE};
    nt_hash32_t h = nt_hash32(data, sizeof(data));
    /* Just verify it does not crash and produces a non-zero-basis value */
    TEST_ASSERT_NOT_EQUAL(0x02CC5D05U, h.value);
}

/* ---- Label tests (compiled with NT_HASH_LABELS=1) ---- */

void test_label_register_and_lookup(void) {
#if NT_HASH_LABELS
    nt_hash32_t h32 = nt_hash32_str("label_test_32");
    const char *label32 = nt_hash32_label(h32);
    TEST_ASSERT_NOT_NULL(label32);
    TEST_ASSERT_EQUAL_STRING("label_test_32", label32);

    nt_hash64_t h64 = nt_hash64_str("label_test_64");
    const char *label64 = nt_hash64_label(h64);
    TEST_ASSERT_NOT_NULL(label64);
    TEST_ASSERT_EQUAL_STRING("label_test_64", label64);
#else
    TEST_IGNORE_MESSAGE("NT_HASH_LABELS not enabled");
#endif
}

void test_label_disabled_returns_null(void) {
#if !NT_HASH_LABELS
    nt_hash32_t h32 = nt_hash32_str("disabled_label");
    TEST_ASSERT_NULL(nt_hash32_label(h32));

    nt_hash64_t h64 = nt_hash64_str("disabled_label");
    TEST_ASSERT_NULL(nt_hash64_label(h64));
#else
    /* With labels enabled, test explicit register + lookup instead */
    nt_hash32_t h32 = nt_hash32("manual", 6);
    nt_hash_register_label32(h32, "manual_label");
    const char *label = nt_hash32_label(h32);
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_EQUAL_STRING("manual_label", label);
#endif
}

void test_label_table_fills(void) {
#if NT_HASH_LABELS
    enum { LABEL_COUNT = NT_HASH_MAX_LABELS + 1024 };
    nt_hash32_t first32 = {0};
    nt_hash32_t last32 = {0};
    nt_hash64_t first64 = {0};
    nt_hash64_t last64 = {0};
    char last_label[32] = {0};

    for (uint32_t i = 0; i < LABEL_COUNT; i++) {
        char label[32];
        (void)snprintf(label, sizeof(label), "grow_label_%u", i);

        nt_hash32_t h32 = nt_hash32_str(label);
        nt_hash64_t h64 = nt_hash64_str(label);
        if (i == 0) {
            first32 = h32;
            first64 = h64;
        }
        if (i == LABEL_COUNT - 1U) {
            last32 = h32;
            last64 = h64;
            (void)snprintf(last_label, sizeof(last_label), "%s", label);
        }
    }

    TEST_ASSERT_EQUAL_STRING("grow_label_0", nt_hash32_label(first32));
    TEST_ASSERT_EQUAL_STRING("grow_label_0", nt_hash64_label(first64));
    TEST_ASSERT_EQUAL_STRING(last_label, nt_hash32_label(last32));
    TEST_ASSERT_EQUAL_STRING(last_label, nt_hash64_label(last64));
#else
    TEST_IGNORE_MESSAGE("NT_HASH_LABELS not enabled");
#endif
}

/* ---- Lifecycle test ---- */

void test_init_shutdown(void) {
    /* setUp already called init, so double-init should fail */
    nt_hash_desc_t desc = {0};
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_hash_init(&desc));

    /* After shutdown, init should succeed again */
    nt_hash_shutdown();
    TEST_ASSERT_EQUAL(NT_OK, nt_hash_init(&desc));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hash32_known_vector);
    RUN_TEST(test_hash64_known_vector);
    RUN_TEST(test_hash32_str_wrapper);
    RUN_TEST(test_hash64_str_wrapper);
    RUN_TEST(test_hash32_empty);
    RUN_TEST(test_hash64_empty);
    RUN_TEST(test_hash32_deterministic);
    RUN_TEST(test_hash64_deterministic);
    RUN_TEST(test_hash32_distribution);
    RUN_TEST(test_hash64_distribution);
    RUN_TEST(test_hash_binary_data);
    RUN_TEST(test_label_register_and_lookup);
    RUN_TEST(test_label_table_fills);
    RUN_TEST(test_label_disabled_returns_null);
    RUN_TEST(test_init_shutdown);
    return UNITY_END();
}
