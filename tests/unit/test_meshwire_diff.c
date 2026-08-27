/* Byte-parity of the C port (engine/meshwire) against the vendored upstream
 * meshopt index codec (deps/meshoptimizer, compiled ONLY into this test):
 *   1. our encoder output is byte-identical to upstream's (format v1);
 *   2. our decoder and upstream's agree on both encoders' streams;
 *   3. decode(encode(x)) preserves the triangle multiset up to rotation;
 *   4. corrupt/truncated streams are rejected, never crash (sanitized build).
 * Mesh shapes cover the codec's paths: edge-fifo hits, vertex-fifo hits,
 * next-sequences, free indices (large jumps), strip codes (last+-1), resets
 * (literal 0,1,2 retriangle), degenerates, and varint width boundaries. */
#include <stdlib.h>
#include <string.h>

#include "meshoptimizer.h"
#include "meshwire/nt_meshwire.h"
#include "meshwire/nt_meshwire_encode.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Deterministic PRNG: no time seeds, same cases on every run/platform */
static uint32_t s_rng;
static uint32_t rng_next(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

#define MAX_INDICES 4096U

static uint32_t s_indices[MAX_INDICES];
static uint8_t s_ours[65536];
static uint8_t s_theirs[65536];
static uint32_t s_dec_ours[MAX_INDICES];
static uint32_t s_dec_theirs[MAX_INDICES];
static uint16_t s_dec16_ours[MAX_INDICES];
static uint16_t s_dec16_theirs[MAX_INDICES];

static bool all_in_range(const uint32_t *indices, uint32_t count, uint32_t vertex_count) {
    for (uint32_t i = 0; i < count; i++) {
        if (indices[i] >= vertex_count) {
            return false;
        }
    }
    return true;
}

/* One full parity pass over s_indices[0..index_count) with the given vertex bound */
static void check_parity(uint32_t index_count, uint32_t vertex_count) {
    uint32_t bound = nt_meshwire_encode_indices_bound(index_count, vertex_count);
    size_t ref_bound = meshopt_encodeIndexBufferBound(index_count, vertex_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)ref_bound, bound);
    TEST_ASSERT_TRUE(bound > 0 && bound <= sizeof(s_ours));

    uint32_t ours_size = nt_meshwire_encode_indices(s_ours, bound, s_indices, index_count);
    meshopt_encodeIndexVersion(1);
    size_t theirs_size = meshopt_encodeIndexBuffer(s_theirs, bound, s_indices, index_count);

    /* 1. byte-identical encode */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)theirs_size, ours_size);
    TEST_ASSERT_TRUE(ours_size > 0);
    TEST_ASSERT_EQUAL_MEMORY(s_theirs, s_ours, ours_size);

    /* 2. cross-decode agreement, u32 */
    TEST_ASSERT_TRUE(nt_meshwire_decode_indices(s_dec_ours, index_count, 4, s_theirs, (uint32_t)theirs_size, vertex_count));
    TEST_ASSERT_EQUAL(0, meshopt_decodeIndexBuffer(s_dec_theirs, index_count, 4, s_ours, ours_size));
    TEST_ASSERT_EQUAL_MEMORY(s_dec_theirs, s_dec_ours, (size_t)index_count * 4);

    /* 2b. u16 destination path when indices fit */
    if (vertex_count <= 65535) {
        TEST_ASSERT_TRUE(nt_meshwire_decode_indices(s_dec16_ours, index_count, 2, s_theirs, (uint32_t)theirs_size, vertex_count));
        TEST_ASSERT_EQUAL(0, meshopt_decodeIndexBuffer(s_dec16_theirs, index_count, 2, s_ours, ours_size));
        TEST_ASSERT_EQUAL_MEMORY(s_dec16_theirs, s_dec16_ours, (size_t)index_count * 2);
        for (uint32_t i = 0; i < index_count; i++) {
            TEST_ASSERT_EQUAL_UINT32(s_dec_ours[i], s_dec16_ours[i]);
        }
    }

    /* 3. triangle multiset preserved up to rotation (each decoded triple is a
     * rotation of the source triple at the same position after upstream's
     * in-order emission -- codec reorders within a triple, never across) */
    for (uint32_t t = 0; t + 2 < index_count; t += 3) {
        const uint32_t *src = s_indices + t;
        const uint32_t *dec = s_dec_ours + t;
        bool rot =
            (dec[0] == src[0] && dec[1] == src[1] && dec[2] == src[2]) || (dec[0] == src[1] && dec[1] == src[2] && dec[2] == src[0]) || (dec[0] == src[2] && dec[1] == src[0] && dec[2] == src[1]);
        TEST_ASSERT_TRUE_MESSAGE(rot, "decoded triangle is not a rotation of the source");
    }
}

static void test_parity_tiny_and_small(void) {
    /* 1 triangle */
    s_indices[0] = 0;
    s_indices[1] = 1;
    s_indices[2] = 2;
    check_parity(3, 3);
    /* non-{0,1,2} single triangle: free-index path */
    s_indices[0] = 5;
    s_indices[1] = 9;
    s_indices[2] = 2;
    check_parity(3, 10);
    /* degenerate triangles */
    s_indices[0] = 0;
    s_indices[1] = 0;
    s_indices[2] = 0;
    s_indices[3] = 1;
    s_indices[4] = 1;
    s_indices[5] = 2;
    check_parity(6, 3);
}

static void test_parity_grid_strip_fan(void) {
    /* grid: heavy edge-fifo reuse (the main compression path) */
    uint32_t n = 0;
    const uint32_t w = 15;
    for (uint32_t y = 0; y < 12; y++) {
        for (uint32_t x = 0; x < w - 1; x++) {
            uint32_t a = (y * w) + x;
            s_indices[n++] = a;
            s_indices[n++] = a + 1;
            s_indices[n++] = a + w;
            s_indices[n++] = a + 1;
            s_indices[n++] = a + w + 1;
            s_indices[n++] = a + w;
        }
    }
    check_parity(n, 13 * w);

    /* strip-like: exercises the last+-1 codes (fec 13/14) */
    n = 0;
    for (uint32_t i = 0; i < 200; i++) {
        s_indices[n++] = i;
        s_indices[n++] = i + 1;
        s_indices[n++] = i + 2;
    }
    check_parity(n, 202);

    /* fan: vertex-fifo hits on the shared apex */
    n = 0;
    for (uint32_t i = 1; i < 120; i++) {
        s_indices[n++] = 0;
        s_indices[n++] = i;
        s_indices[n++] = i + 1;
    }
    check_parity(n, 121);
}

static void test_parity_reset_and_jumps(void) {
    /* literal (0,1,2) after next>0: the reset path */
    uint32_t n = 0;
    s_indices[n++] = 0;
    s_indices[n++] = 1;
    s_indices[n++] = 2;
    s_indices[n++] = 3;
    s_indices[n++] = 4;
    s_indices[n++] = 5;
    s_indices[n++] = 0;
    s_indices[n++] = 1;
    s_indices[n++] = 2; /* reset */
    s_indices[n++] = 6;
    s_indices[n++] = 7;
    s_indices[n++] = 8;
    check_parity(n, 9);

    /* large forward/backward jumps: multi-group varints, both zigzag signs */
    n = 0;
    uint32_t jumps[] = {0, 100, 127, 128, 16383, 16384, 70000, 2, 1000000, 500};
    for (uint32_t t = 0; t < 3; t++) {
        for (uint32_t j = 0; j + 2 < sizeof(jumps) / sizeof(jumps[0]); j++) {
            s_indices[n++] = jumps[j] + t;
            s_indices[n++] = jumps[j + 1] + t;
            s_indices[n++] = jumps[j + 2] + t;
        }
    }
    check_parity(n, 1000001 + 3);
}

static void test_parity_random_soup(void) {
    /* random triangle soup across vertex-count scales, incl. varint boundaries */
    static const uint32_t vertex_counts[] = {4, 64, 127, 128, 255, 256, 65535, 65536, 1U << 20};
    for (uint32_t vc = 0; vc < sizeof(vertex_counts) / sizeof(vertex_counts[0]); vc++) {
        uint32_t vertex_count = vertex_counts[vc];
        s_rng = 0x12345678U + vc;
        for (uint32_t round = 0; round < 4; round++) {
            uint32_t tri_count = 1 + (rng_next() % (MAX_INDICES / 3));
            uint32_t index_count = tri_count * 3;
            for (uint32_t i = 0; i < index_count; i++) {
                s_indices[i] = rng_next() % vertex_count;
            }
            check_parity(index_count, vertex_count);
        }
    }
}

static void test_parity_random_locality(void) {
    /* random walks with locality: mixes fifo hits, next-runs and short jumps */
    s_rng = 0xC0FFEEU;
    for (uint32_t round = 0; round < 8; round++) {
        uint32_t vertex_count = 3 + (rng_next() % 3000);
        uint32_t tri_count = 1 + (rng_next() % (MAX_INDICES / 3));
        uint32_t index_count = tri_count * 3;
        uint32_t cursor = 0;
        for (uint32_t i = 0; i < index_count; i++) {
            cursor = (cursor + (rng_next() % 7)) % vertex_count;
            s_indices[i] = cursor;
        }
        check_parity(index_count, vertex_count);
    }
}

static void test_corrupt_streams_rejected(void) {
    /* valid stream, then flip/truncate: our decoder must fail or stay in
     * bounds -- under sanitizers any overread aborts the test */
    s_rng = 0xDEAD10CCU;
    uint32_t index_count = 3 * 200;
    for (uint32_t i = 0; i < index_count; i++) {
        s_indices[i] = rng_next() % 500;
    }
    uint32_t bound = nt_meshwire_encode_indices_bound(index_count, 500);
    uint32_t size = nt_meshwire_encode_indices(s_ours, bound, s_indices, index_count);
    TEST_ASSERT_TRUE(size > 0);

    /* truncations: our verdict must be upstream's AND the range gate (we are
     * strictly stricter: upstream has no vertex_count and can accept streams
     * decoding to out-of-range indices) */
    for (uint32_t cut = 0; cut < size; cut += 7) {
        bool ours_ok = nt_meshwire_decode_indices(s_dec_ours, index_count, 4, s_ours, cut, 500);
        bool theirs_ok = meshopt_decodeIndexBuffer(s_dec_theirs, index_count, 4, s_ours, cut) == 0;
        TEST_ASSERT_EQUAL(theirs_ok && all_in_range(s_dec_theirs, index_count, 500), ours_ok);
    }

    /* single-byte corruptions: verdicts AND decoded bytes (when accepted) match.
     * Byte 0 is exempt: upstream accepts a v0 header, our contract is v1-only
     * (covered by the explicit check below). */
    for (uint32_t pos = 1; pos < size; pos += 3) {
        memcpy(s_theirs, s_ours, size);
        s_theirs[pos] ^= (uint8_t)(1U << (pos % 8));
        bool ours_ok = nt_meshwire_decode_indices(s_dec_ours, index_count, 4, s_theirs, size, 500);
        bool theirs_ok = meshopt_decodeIndexBuffer(s_dec_theirs, index_count, 4, s_theirs, size) == 0;
        TEST_ASSERT_EQUAL(theirs_ok && all_in_range(s_dec_theirs, index_count, 500), ours_ok);
        if (ours_ok) {
            TEST_ASSERT_EQUAL_MEMORY(s_dec_theirs, s_dec_ours, (size_t)index_count * 4);
        }
    }

    /* wrong header / version: ours is v1-only by contract */
    memcpy(s_theirs, s_ours, size);
    s_theirs[0] = 0xE0; /* v0 stream header */
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(s_dec_ours, index_count, 4, s_theirs, size, 500));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parity_tiny_and_small);
    RUN_TEST(test_parity_grid_strip_fan);
    RUN_TEST(test_parity_reset_and_jumps);
    RUN_TEST(test_parity_random_soup);
    RUN_TEST(test_parity_random_locality);
    RUN_TEST(test_corrupt_streams_rejected);
    return UNITY_END();
}
