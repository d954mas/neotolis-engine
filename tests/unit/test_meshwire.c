/* Real nt_meshwire impl: SOA re-interleave against hand-computed bytes
 * (asymmetric streams, odd counts, unaligned plane starts) and index-decode
 * rejection of garbage streams. The builder->runtime round-trip lives in
 * test_builder (it needs the encoder). */
#include <string.h>

#include "meshwire/nt_meshwire.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_reinterleave_mixed_elem_sizes(void) {
    /* 3 vertices, streams of 1/2/4 bytes: byte plane first, so the u16 and
     * u32 planes start at odd offsets (3 and 9) -- per-element memcpy must
     * not assume alignment. */
    enum { VERTS = 3 };
    const uint32_t elem_sizes[3] = {1, 2, 4};
    /* Wire planes: a0 a1 a2 | b0 b1 b2 | c0 c1 c2 */
    const uint8_t soa[VERTS * 7] = {
        0xA0, 0xA1, 0xA2,                               /* u8 plane */
        0x10, 0x11, 0x20, 0x21, 0x30, 0x31,             /* u16 plane */
        0x50, 0x51, 0x52, 0x53, 0x60, 0x61, 0x62, 0x63, /* u32 plane */
        0x70, 0x71, 0x72, 0x73,
    };
    const uint8_t expected[VERTS * 7] = {
        0xA0, 0x10, 0x11, 0x50, 0x51, 0x52, 0x53, /* v0 */
        0xA1, 0x20, 0x21, 0x60, 0x61, 0x62, 0x63, /* v1 */
        0xA2, 0x30, 0x31, 0x70, 0x71, 0x72, 0x73, /* v2 */
    };
    uint8_t dst[VERTS * 7];
    memset(dst, 0, sizeof(dst));
    TEST_ASSERT_TRUE(nt_meshwire_reinterleave(dst, soa, VERTS, elem_sizes, 3));
    TEST_ASSERT_EQUAL_MEMORY(expected, dst, sizeof(expected));
}

static void test_reinterleave_single_stream_is_identity(void) {
    const uint32_t elem_sizes[1] = {4};
    const uint8_t soa[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dst[8];
    memset(dst, 0, sizeof(dst));
    TEST_ASSERT_TRUE(nt_meshwire_reinterleave(dst, soa, 2, elem_sizes, 1));
    TEST_ASSERT_EQUAL_MEMORY(soa, dst, sizeof(soa));
}

static void test_reinterleave_rejects_bad_args(void) {
    uint32_t elem_sizes[2] = {4, 0}; /* zero element size */
    uint8_t buf[16] = {0};
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf, buf, 2, elem_sizes, 2));
    elem_sizes[1] = 4;
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf, buf, 2, elem_sizes, 0));
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(NULL, buf, 2, elem_sizes, 2));
}

static void test_decode_indices_rejects_garbage(void) {
    /* No valid meshopt header byte -- the decoder must fail, not crash */
    const uint8_t garbage[16] = {0x00, 0xFF, 0x13, 0x37};
    uint16_t dst[3] = {0};
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(dst, 3, 2, garbage, sizeof(garbage)));
}

static void test_decode_indices_rejects_bad_elem_size(void) {
    const uint8_t wire[4] = {0xE1, 0x00, 0x00, 0x00};
    uint8_t dst[16];
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(dst, 3, 3, wire, sizeof(wire)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reinterleave_mixed_elem_sizes);
    RUN_TEST(test_reinterleave_single_stream_is_identity);
    RUN_TEST(test_reinterleave_rejects_bad_args);
    RUN_TEST(test_decode_indices_rejects_garbage);
    RUN_TEST(test_decode_indices_rejects_bad_elem_size);
    return UNITY_END();
}
