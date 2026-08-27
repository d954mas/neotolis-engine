/* Real nt_meshwire impl: SOA re-interleave against hand-computed bytes
 * (asymmetric streams, odd counts, unaligned plane starts) and index-decode
 * rejection of garbage streams. The builder->runtime round-trip lives in
 * test_builder (it needs the encoder). */
#include <string.h>

#include "meshwire/nt_meshwire.h"
#include "meshwire/nt_meshwire_encode.h"
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

static void test_reinterleave_rejects_overlap(void) {
    /* The permutation cannot run in place: overlapping buffers must be
     * rejected, not silently corrupted */
    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint32_t elem_sizes[2] = {1, 1};
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf, buf, 2, elem_sizes, 2));
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf + 1, buf, 2, elem_sizes, 2));
    /* disjoint halves of one array are fine */
    TEST_ASSERT_TRUE(nt_meshwire_reinterleave(buf + 4, buf, 2, elem_sizes, 2));
}

static void test_reinterleave_rejects_bad_args(void) {
    uint32_t elem_sizes[2] = {4, 0}; /* zero element size */
    uint8_t buf[16] = {0};
    uint8_t buf2[16] = {0};
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf, buf2, 2, elem_sizes, 2));
    /* summed element sizes overflowing the u32 stride must be rejected */
    uint32_t huge[2] = {0xFFFFFFFFU, 2};
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf, buf2, 1, huge, 2));
    elem_sizes[1] = 4;
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(buf, buf, 2, elem_sizes, 0));
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(NULL, buf, 2, elem_sizes, 2));
}

static void test_decode_indices_rejects_out_of_range(void) {
    /* Structurally valid v1 stream (header + zero codes + zero tail) whose
     * edge codes hit unseeded FIFO slots (UINT32_MAX): the decoder must
     * reject it instead of publishing out-of-range indices. */
    uint8_t wire[1 + 4 + 16] = {0xE1};
    uint16_t dst[12] = {0};
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(dst, 12, 2, wire, sizeof(wire), 100));
}

static void test_decode_indices_unaligned_dst(void) {
    /* dst is a public byte span: an odd offset must produce correct bytes
     * (typed stores would be misaligned UB -- UBSan flags them here) */
    uint32_t idx[3] = {0, 2, 1};
    uint8_t wire[64];
    uint32_t bound = nt_meshwire_encode_indices_bound(3, 3);
    uint32_t size = nt_meshwire_encode_indices(wire, bound, idx, 3);
    TEST_ASSERT_TRUE(size > 0);
    uint8_t buf16[1 + 6];
    TEST_ASSERT_TRUE(nt_meshwire_decode_indices(buf16 + 1, 3, 2, wire, size, 3));
    uint16_t got16[3];
    memcpy(got16, buf16 + 1, sizeof(got16));
    TEST_ASSERT_EQUAL_UINT16(0, got16[0]);
    TEST_ASSERT_EQUAL_UINT16(2, got16[1]);
    TEST_ASSERT_EQUAL_UINT16(1, got16[2]);
    uint8_t buf32[1 + 12];
    TEST_ASSERT_TRUE(nt_meshwire_decode_indices(buf32 + 1, 3, 4, wire, size, 3));
    uint32_t got32[3];
    memcpy(got32, buf32 + 1, sizeof(got32));
    TEST_ASSERT_EQUAL_UINT32(2, got32[1]);
}

static void test_decode_indices_rejects_u16_overflow(void) {
    /* index 70000 passes the vertex_count range gate but cannot fit a u16
     * destination -- must reject instead of truncating to 4464 */
    uint32_t idx[3] = {0, 70000, 1};
    uint8_t wire[64];
    uint32_t bound = nt_meshwire_encode_indices_bound(3, 70001);
    TEST_ASSERT_TRUE(bound <= sizeof(wire));
    uint32_t size = nt_meshwire_encode_indices(wire, bound, idx, 3);
    TEST_ASSERT_TRUE(size > 0);
    uint16_t dst16[3];
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(dst16, 3, 2, wire, size, 70001));
    /* the same stream is fine into a u32 destination */
    uint32_t dst32[3];
    TEST_ASSERT_TRUE(nt_meshwire_decode_indices(dst32, 3, 4, wire, size, 70001));
    TEST_ASSERT_EQUAL_UINT32(0, dst32[0]); /* a == next keeps the rotation identical */
    TEST_ASSERT_EQUAL_UINT32(70000, dst32[1]);
    TEST_ASSERT_EQUAL_UINT32(1, dst32[2]);
}

static void test_decode_indices_rejects_garbage(void) {
    /* No valid meshopt header byte -- the decoder must fail, not crash */
    const uint8_t garbage[16] = {0x00, 0xFF, 0x13, 0x37};
    uint16_t dst[3] = {0};
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(dst, 3, 2, garbage, sizeof(garbage), 3));
}

static void test_decode_indices_rejects_bad_elem_size(void) {
    const uint8_t wire[4] = {0xE1, 0x00, 0x00, 0x00};
    uint8_t dst[16];
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(dst, 3, 3, wire, sizeof(wire), 3));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reinterleave_mixed_elem_sizes);
    RUN_TEST(test_reinterleave_single_stream_is_identity);
    RUN_TEST(test_reinterleave_rejects_overlap);
    RUN_TEST(test_reinterleave_rejects_bad_args);
    RUN_TEST(test_decode_indices_rejects_out_of_range);
    RUN_TEST(test_decode_indices_unaligned_dst);
    RUN_TEST(test_decode_indices_rejects_u16_overflow);
    RUN_TEST(test_decode_indices_rejects_garbage);
    RUN_TEST(test_decode_indices_rejects_bad_elem_size);
    return UNITY_END();
}
