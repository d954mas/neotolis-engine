#include "input/nt_input_internal.h" /* nt_input_char (direct feed) */
#include "unity.h"

void setUp(void) { nt_input_init(); }

void tearDown(void) { nt_input_shutdown(); }

/* ---- FIFO order ---- */

void test_pop_char_fifo_order(void) {
    nt_input_char(0x41U); /* 'A' */
    nt_input_char(0x42U); /* 'B' */
    nt_input_char(0x43U); /* 'C' */
    nt_input_poll();

    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x41U, cp);
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x42U, cp);
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x43U, cp);
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* ---- Multi-byte codepoint survives intact ---- */

void test_pop_char_utf32_roundtrip(void) {
    nt_input_char(0x0410U); /* Cyrillic 'А' */
    nt_input_poll();
    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x0410U, cp); /* full uint32, not truncated */
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* ---- Overflow drops the newest, never corrupts earlier typing ---- */

void test_pop_char_overflow_drop(void) {
    for (uint32_t i = 0; i < 50U; i++) {
        nt_input_char(100U + i);
    }
    nt_input_poll();

    uint32_t cp = 0;
    for (uint32_t i = 0; i < NT_INPUT_CHAR_MAX; i++) {
        TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
        TEST_ASSERT_EQUAL_UINT32(100U + i, cp); /* never garbage */
    }
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* ---- Empty pop returns false and leaves *out untouched ---- */

void test_pop_char_empty_leaves_out_untouched(void) {
    uint32_t cp = 0xDEADBEEFU;
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, cp);
}

/* ---- Frame-local: a char typed before a poll is readable after it, for that frame only ---- */

void test_chars_published_for_one_frame(void) {
    nt_input_char(0x41U);
    uint32_t cp = 0xDEADBEEFU;
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp)); /* not published yet */
    nt_input_poll();
    nt_input_char(0x42U); /* typed during the frame: next frame's */
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x41U, cp);
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
    nt_input_poll();
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x42U, cp);
    nt_input_poll();
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp)); /* unread text never leaks into a later frame */
}

/* A frame of unread text never takes capacity from the next frame's typing. */
void test_unread_frame_keeps_next_frame_capacity(void) {
    for (uint32_t i = 0; i < NT_INPUT_CHAR_MAX; i++) {
        nt_input_char(100U + i);
    }
    nt_input_poll();
    nt_input_char(0x41U);
    nt_input_poll();
    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x41U, cp);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pop_char_fifo_order);
    RUN_TEST(test_pop_char_utf32_roundtrip);
    RUN_TEST(test_pop_char_overflow_drop);
    RUN_TEST(test_pop_char_empty_leaves_out_untouched);
    RUN_TEST(test_chars_published_for_one_frame);
    RUN_TEST(test_unread_frame_keeps_next_frame_capacity);
    return UNITY_END();
}
