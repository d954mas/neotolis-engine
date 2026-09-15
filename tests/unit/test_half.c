#include "unity.h"

#include "nt_half.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

void setUp(void) {}

void tearDown(void) {}

static uint32_t float_bits(float f) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = f;
    return conv.u;
}

static float float_from_bits(uint32_t u) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.u = u;
    return conv.f;
}

/* ---- float32 -> float16 known vectors ---- */

void test_f32_to_f16_zero_and_one(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0000, nt_f32_to_f16(0.0F));
    TEST_ASSERT_EQUAL_HEX16(0x8000, nt_f32_to_f16(-0.0F));
    TEST_ASSERT_EQUAL_HEX16(0x3C00, nt_f32_to_f16(1.0F));
    TEST_ASSERT_EQUAL_HEX16(0xBE00, nt_f32_to_f16(-1.5F));
    TEST_ASSERT_EQUAL_HEX16(0x2E66, nt_f32_to_f16(0.1F));
}

void test_f32_to_f16_overflow_saturates_to_inf(void) {
    TEST_ASSERT_EQUAL_HEX16(0x7BFF, nt_f32_to_f16(65504.0F));
    TEST_ASSERT_EQUAL_HEX16(0x7BFF, nt_f32_to_f16(65519.99F));
    TEST_ASSERT_EQUAL_HEX16(0x7C00, nt_f32_to_f16(65520.0F));
    TEST_ASSERT_EQUAL_HEX16(0xFC00, nt_f32_to_f16(-65520.0F));
}

void test_f32_to_f16_subnormal_and_underflow(void) {
    /* 2^-25 is exactly halfway to zero: ties to even keeps zero. */
    TEST_ASSERT_EQUAL_HEX16(0x0000, nt_f32_to_f16(ldexpf(1.0F, -25)));
    TEST_ASSERT_EQUAL_HEX16(0x8000, nt_f32_to_f16(ldexpf(-1.0F, -25)));
    TEST_ASSERT_EQUAL_HEX16(0x0001, nt_f32_to_f16(ldexpf(1.0F, -24)));
    TEST_ASSERT_EQUAL_HEX16(0x03FF, nt_f32_to_f16(ldexpf(1023.0F, -24)));
}

/* The subnormal step is 2^-24, so these land on and between subnormal ties. */
void test_f32_to_f16_subnormal_rounding(void) {
    /* 1.5 steps: halfway between 0x0001 and 0x0002, ties to the even one up. */
    TEST_ASSERT_EQUAL_HEX16(0x0002, nt_f32_to_f16(ldexpf(1.5F, -24)));
    TEST_ASSERT_EQUAL_HEX16(0x8002, nt_f32_to_f16(ldexpf(-1.5F, -24)));
    /* 1.25 steps: nearer 0x0001, no tie. */
    TEST_ASSERT_EQUAL_HEX16(0x0001, nt_f32_to_f16(ldexpf(1.25F, -24)));
    /* 1023.5 steps: the tie at the top of the subnormals carries into 2^-14. */
    TEST_ASSERT_EQUAL_HEX16(0x0400, nt_f32_to_f16(ldexpf(2047.0F, -25)));
}

/* 2^-14 is the smallest normal half, not a subnormal: the boundary both
 * directions must agree on. */
void test_f32_to_f16_smallest_normal(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0400, nt_f32_to_f16(ldexpf(1.0F, -14)));
    TEST_ASSERT_EQUAL_HEX32(float_bits(ldexpf(1.0F, -14)), float_bits(nt_f16_to_f32(0x0400)));
}

void test_f32_to_f16_ties_to_even(void) {
    /* 1 + 2^-11 sits between 0x3C00 and 0x3C01; the even neighbour wins. */
    TEST_ASSERT_EQUAL_HEX16(0x3C00, nt_f32_to_f16(1.0F + ldexpf(1.0F, -11)));
    /* 1 + 3*2^-11 sits between 0x3C01 and 0x3C02; the even neighbour wins. */
    TEST_ASSERT_EQUAL_HEX16(0x3C02, nt_f32_to_f16(1.0F + ldexpf(3.0F, -11)));
}

void test_f32_to_f16_infinity_and_nan(void) {
    TEST_ASSERT_EQUAL_HEX16(0x7C00, nt_f32_to_f16(INFINITY));
    TEST_ASSERT_EQUAL_HEX16(0xFC00, nt_f32_to_f16(-INFINITY));

    uint16_t pos = nt_f32_to_f16(NAN);
    TEST_ASSERT_EQUAL_HEX16(0x7C00, pos & 0x7C00);
    TEST_ASSERT_NOT_EQUAL(0, pos & 0x03FF);
    TEST_ASSERT_EQUAL_HEX16(0x0000, pos & 0x8000);

    uint16_t neg = nt_f32_to_f16(-NAN);
    TEST_ASSERT_EQUAL_HEX16(0x7C00, neg & 0x7C00);
    TEST_ASSERT_NOT_EQUAL(0, neg & 0x03FF);
    TEST_ASSERT_EQUAL_HEX16(0x8000, neg & 0x8000);
}

/* The exact NaN and extreme-subnormal encodings the bank may hand the codec. */
void test_f32_to_f16_exact_edge_encodings(void) {
    /* A signalling NaN: quieted with the canonical payload, sign kept. */
    TEST_ASSERT_EQUAL_HEX16(0x7E00, nt_f32_to_f16(float_from_bits(0x7F800001U)));
    TEST_ASSERT_EQUAL_HEX16(0xFE00, nt_f32_to_f16(-NAN));
    /* The smallest float32 subnormal is far below half range: signed zero. */
    TEST_ASSERT_EQUAL_HEX16(0x8000, nt_f32_to_f16(-FLT_TRUE_MIN));
}

void test_f16_to_f32_infinity_is_exact(void) {
    TEST_ASSERT_EQUAL_HEX32(0x7F800000U, float_bits(nt_f16_to_f32(0x7C00)));
    TEST_ASSERT_EQUAL_HEX32(0xFF800000U, float_bits(nt_f16_to_f32(0xFC00)));
}

/* A signaling half NaN must widen into a quiet float NaN: a signaling one
 * raises an invalid-operation trap on the first arithmetic that touches it. */
void test_f16_to_f32_quiets_signaling_nan(void) {
    const float f = nt_f16_to_f32(0x7C01);
    TEST_ASSERT_TRUE(isnan(f));
    TEST_ASSERT_EQUAL_HEX32(0x00400000U, float_bits(f) & 0x00400000U);

    const float n = nt_f16_to_f32(0xFC01);
    TEST_ASSERT_TRUE(isnan(n));
    TEST_ASSERT_EQUAL_HEX32(0x00400000U, float_bits(n) & 0x00400000U);
    TEST_ASSERT_EQUAL_HEX32(0x80000000U, float_bits(n) & 0x80000000U);
}

/* ---- float16 -> float32 known vectors ---- */

/* Unity is built with floating-point asserts excluded, and every value below is
 * exact in binary32 anyway, so the comparison is on the bit pattern. */
void test_f16_to_f32_known_vectors(void) {
    TEST_ASSERT_EQUAL_HEX32(float_bits(ldexpf(1.0F, -24)), float_bits(nt_f16_to_f32(0x0001)));
    TEST_ASSERT_EQUAL_HEX32(float_bits(ldexpf(1023.0F, -24)), float_bits(nt_f16_to_f32(0x03FF)));
    TEST_ASSERT_EQUAL_HEX32(float_bits(ldexpf(1.0F, -14)), float_bits(nt_f16_to_f32(0x0400)));
    TEST_ASSERT_EQUAL_HEX32(float_bits(65504.0F), float_bits(nt_f16_to_f32(0x7BFF)));
    TEST_ASSERT_EQUAL_HEX32(float_bits(0.333251953125F), float_bits(nt_f16_to_f32(0x3555)));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, float_bits(nt_f16_to_f32(0x0000)));
    TEST_ASSERT_EQUAL_HEX32(0x80000000U, float_bits(nt_f16_to_f32(0x8000)));
}

/* ---- Round-trip over every encoding ---- */

void test_round_trip_all_encodings(void) {
    for (uint32_t i = 0; i <= 0xFFFFU; i++) {
        uint16_t h = (uint16_t)i;
        float f = nt_f16_to_f32(h);
        bool is_nan = ((h & 0x7C00U) == 0x7C00U) && ((h & 0x03FFU) != 0U);
        if (is_nan) {
            TEST_ASSERT_TRUE(isnan(f));
            TEST_ASSERT_EQUAL_HEX32((uint32_t)(h & 0x8000U) << 16, float_bits(f) & 0x80000000U);
        } else {
            TEST_ASSERT_EQUAL_HEX16(h, nt_f32_to_f16(f));
        }
    }
}

/* ---- Rounding direction over a deterministic sample ---- */

/* Maps a half encoding onto a value-ordered index, so +-1 on the index is the
 * adjacent representable half across the sign and subnormal boundaries. */
static int32_t half_order(uint16_t h) {
    if ((h & 0x8000U) != 0U) {
        return -(int32_t)(uint32_t)(h & 0x7FFFU);
    }
    return (int32_t)(uint32_t)h;
}

static uint16_t half_from_order(int32_t order) {
    if (order < 0) {
        return (uint16_t)(0x8000U | (uint32_t)(-order));
    }
    return (uint16_t)(uint32_t)order;
}

/* Sweeps [-span, +span] and checks every result is the nearest half, ties even. */
static void assert_nearest_even_over(float span) {
    uint32_t lcg = 0x12345678U;
    for (uint32_t n = 0; n < 4096U; n++) {
        lcg = (lcg * 1664525U) + 1013904223U;
        float unit = (float)(lcg >> 8U) / 16777216.0F;
        float x = ((unit * 2.0F) - 1.0F) * span;

        uint16_t h = nt_f32_to_f16(x);
        float r = nt_f16_to_f32(h);
        if ((h & 0x7C00U) == 0x7C00U) {
            /* Only an overflow can land on Inf; 65520 is the first float that does. */
            TEST_ASSERT_TRUE(fabsf(x) >= 65520.0F);
            continue;
        }

        int32_t order = half_order(h);
        float down = nt_f16_to_f32(half_from_order(order - 1));
        float up = nt_f16_to_f32(half_from_order(order + 1));
        float err = fabsf(x - r);

        TEST_ASSERT_TRUE(err <= fabsf(x - down));
        TEST_ASSERT_TRUE(err <= fabsf(x - up));
        if ((err == fabsf(x - down)) || (err == fabsf(x - up))) {
            TEST_ASSERT_EQUAL_HEX16(0x0000, h & 0x0001U);
        }
    }
}

void test_rounding_is_nearest_even(void) { assert_nearest_even_over(70000.0F); }

/* The wide sweep almost never produces a subnormal; this range is nothing but
 * subnormals, both signs and zero. */
void test_rounding_is_nearest_even_near_zero(void) { assert_nearest_even_over(1e-4F); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_f32_to_f16_zero_and_one);
    RUN_TEST(test_f32_to_f16_overflow_saturates_to_inf);
    RUN_TEST(test_f32_to_f16_subnormal_and_underflow);
    RUN_TEST(test_f32_to_f16_subnormal_rounding);
    RUN_TEST(test_f32_to_f16_smallest_normal);
    RUN_TEST(test_f32_to_f16_ties_to_even);
    RUN_TEST(test_f32_to_f16_infinity_and_nan);
    RUN_TEST(test_f32_to_f16_exact_edge_encodings);
    RUN_TEST(test_f16_to_f32_known_vectors);
    RUN_TEST(test_f16_to_f32_infinity_is_exact);
    RUN_TEST(test_f16_to_f32_quiets_signaling_nan);
    RUN_TEST(test_round_trip_all_encodings);
    RUN_TEST(test_rounding_is_nearest_even);
    RUN_TEST(test_rounding_is_nearest_even_near_zero);
    return UNITY_END();
}
