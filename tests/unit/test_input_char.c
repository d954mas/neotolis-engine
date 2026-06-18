#include "input/nt_input_internal.h" /* nt_input_buffer_char (direct feed) */
#include "unity.h"

void setUp(void) { nt_input_init(); }

void tearDown(void) { nt_input_shutdown(); }

/* ---- FIFO order ---- */

void test_pop_char_fifo_order(void) {
    nt_input_buffer_char(0x41U); /* 'A' */
    nt_input_buffer_char(0x42U); /* 'B' */
    nt_input_buffer_char(0x43U); /* 'C' */

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
    nt_input_buffer_char(0x0410U); /* Cyrillic 'А' */
    uint32_t cp = 0;
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x0410U, cp); /* full uint32, not truncated */
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* ---- Overflow drops safely, never corrupts buffered chars ---- */

void test_pop_char_overflow_drop(void) {
    /* Ring capacity is NT_INPUT_CHAR_RING (32). Drop-when-full keeps the earliest
       unread chars. Push 0..49: codepoints 100..131 buffer, 132..149 are dropped. */
    for (uint32_t i = 0; i < 50U; i++) {
        nt_input_buffer_char(100U + i);
    }

    /* Pop a consistent prefix: the first 32 pushed codepoints, in order. */
    uint32_t cp = 0;
    for (uint32_t i = 0; i < 32U; i++) {
        TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
        TEST_ASSERT_EQUAL_UINT32(100U + i, cp); /* never garbage */
    }
    /* Everything past capacity was dropped — ring is empty now. */
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* ---- Empty pop returns false and leaves *out untouched ---- */

void test_pop_char_empty_leaves_out_untouched(void) {
    uint32_t cp = 0xDEADBEEFU;
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, cp);
}

/* ---- Frame-local: an unconsumed char is dropped at the next poll (no leak into a later field) ---- */

void test_poll_clears_unconsumed_chars(void) {
    nt_input_buffer_char(0x41U); /* 'A' typed but never popped this frame */
    nt_input_buffer_char(0x42U); /* 'B' too */

    nt_input_poll(1U); /* frame boundary: typed text is frame-local, like key edges */

    /* The ring is empty -- the stale chars cannot leak into a field focused next frame. */
    uint32_t cp = 0xDEADBEEFU;
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, cp);

    /* A char buffered AFTER the poll is still readable this frame (same-frame typing survives). */
    nt_input_buffer_char(0x43U); /* 'C' */
    TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
    TEST_ASSERT_EQUAL_UINT32(0x43U, cp);
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

/* ---- Physical index wraparound: interleaved pop+push crossing the 32-slot seam keeps FIFO order ---- */

void test_pop_char_index_wraparound(void) {
    uint32_t cp = 0;
    /* Push 20, pop 20 -> head and tail counters both at 20 (physical index 20), ring empty. */
    for (uint32_t i = 0; i < 20U; i++) {
        nt_input_buffer_char(200U + i);
    }
    for (uint32_t i = 0; i < 20U; i++) {
        TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
        TEST_ASSERT_EQUAL_UINT32(200U + i, cp);
    }
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));

    /* Push 20 more: the write index walks 20..31 then wraps to 0..7 mid-fill (<=32 live, no drops).
       A correct (head/tail & (RING-1)) mask reads them back in FIFO order across the physical seam. */
    for (uint32_t i = 0; i < 20U; i++) {
        nt_input_buffer_char(300U + i);
    }
    for (uint32_t i = 0; i < 20U; i++) {
        TEST_ASSERT_TRUE(nt_input_pop_char(&cp));
        TEST_ASSERT_EQUAL_UINT32(300U + i, cp); /* order preserved across the wrap */
    }
    TEST_ASSERT_FALSE(nt_input_pop_char(&cp));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pop_char_fifo_order);
    RUN_TEST(test_pop_char_utf32_roundtrip);
    RUN_TEST(test_pop_char_overflow_drop);
    RUN_TEST(test_pop_char_empty_leaves_out_untouched);
    RUN_TEST(test_poll_clears_unconsumed_chars);
    RUN_TEST(test_pop_char_index_wraparound);
    return UNITY_END();
}
