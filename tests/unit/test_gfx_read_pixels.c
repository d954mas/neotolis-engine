/* CAP-01: nt_gfx_read_pixels contract — cap-reject / top-left orientation /
 * channel layout / bad-size reject. Links nt_gfx_stub (deterministic synthetic
 * gradient, no GL context — the only L1 readback layer CTest can prove headless). */

#include "graphics/nt_gfx.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Stub gradient (see stub/nt_gfx_stub.c): at GL row r, col c (bottom-left),
 *   R = r & 0xFF, G = c & 0xFF, B = 0x40, A = 0xFF.
 * After the shared-layer Y-flip, out row 0 must carry GL row (h-1). */

/* Test 1 (69-L1-RD): out_cap < w*h*4 -> false, no write past the cap. */
void test_read_pixels_rejects_too_small_cap(void) {
    enum { W = 4, H = 4 };
    uint8_t buf[(W * H * 4) + 1];
    const uint8_t kGuard = 0xABU;
    memset(buf, kGuard, sizeof(buf));

    /* Cap one byte short of the required w*h*4. */
    bool ok = nt_gfx_read_pixels(0, 0, W, H, buf, (uint32_t)(W * H * 4) - 1U);
    TEST_ASSERT_FALSE(ok);
    /* Nothing was written — every byte still the guard value. */
    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_UINT8(kGuard, buf[i]);
    }
}

/* Test 2 (69-L1-OR): with a sufficient buffer the gradient comes back TOP-LEFT
 * oriented — out row 0 equals what the stub placed at GL row (h-1). */
void test_read_pixels_top_left_orientation(void) {
    enum { W = 4, H = 4 };
    uint8_t buf[W * H * 4];
    memset(buf, 0, sizeof(buf));

    bool ok = nt_gfx_read_pixels(0, 0, W, H, buf, (uint32_t)sizeof(buf));
    TEST_ASSERT_TRUE(ok);

    /* out row 0 R-channel must be GL row (h-1) = 3. out last row must be GL row 0. */
    for (int col = 0; col < W; col++) {
        size_t top = ((size_t)0 * W + (size_t)col) * 4U;
        size_t bottom = ((size_t)(H - 1) * W + (size_t)col) * 4U;
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(H - 1), buf[top + 0U]); /* top row carries GL row h-1 */
        TEST_ASSERT_EQUAL_UINT8(0U, buf[bottom + 0U]);            /* bottom row carries GL row 0 */
        /* Column index preserved by the flip (x unchanged). */
        TEST_ASSERT_EQUAL_UINT8((uint8_t)col, buf[top + 1U]);
    }
}

/* Test 3 (channel contract): returned layout is 4-channel rgba8 — B marker and
 * opaque alpha present at every pixel. */
void test_read_pixels_channel_layout_rgba8(void) {
    enum { W = 3, H = 2 };
    uint8_t buf[W * H * 4];
    memset(buf, 0, sizeof(buf));

    bool ok = nt_gfx_read_pixels(0, 0, W, H, buf, (uint32_t)sizeof(buf));
    TEST_ASSERT_TRUE(ok);

    for (int i = 0; i < W * H; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x40U, buf[(i * 4) + 2]); /* B marker (4-channel stride) */
        TEST_ASSERT_EQUAL_UINT8(0xFFU, buf[(i * 4) + 3]); /* straight, opaque alpha */
    }
}

/* Test 4 (69-L1-BAD): w<=0 or h<=0 -> false; no overflow, no write. */
void test_read_pixels_rejects_bad_size(void) {
    uint8_t buf[16];
    const uint8_t kGuard = 0xCDU;
    memset(buf, kGuard, sizeof(buf));

    TEST_ASSERT_FALSE(nt_gfx_read_pixels(0, 0, 0, 4, buf, (uint32_t)sizeof(buf)));
    TEST_ASSERT_FALSE(nt_gfx_read_pixels(0, 0, 4, 0, buf, (uint32_t)sizeof(buf)));
    TEST_ASSERT_FALSE(nt_gfx_read_pixels(0, 0, -1, 4, buf, (uint32_t)sizeof(buf)));
    TEST_ASSERT_FALSE(nt_gfx_read_pixels(0, 0, 4, -1, buf, (uint32_t)sizeof(buf)));
    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_UINT8(kGuard, buf[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_pixels_rejects_too_small_cap);
    RUN_TEST(test_read_pixels_top_left_orientation);
    RUN_TEST(test_read_pixels_channel_layout_rgba8);
    RUN_TEST(test_read_pixels_rejects_bad_size);
    return UNITY_END();
}
