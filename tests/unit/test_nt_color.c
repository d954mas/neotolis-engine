/* nt_color.h unit tests: pack/unpack round-trips, sRGB<->linear transfer, OKLab round-trip,
 * the perceptual lerp, and a BYTE-IDENTITY guard pinning the migrated input fade path. This
 * TU includes ONLY color/nt_color.h (plus stdint/stdbool/math) -- proving the header pulls in
 * nothing UI/Clay. UNITY_EXCLUDE_FLOAT is set engine-wide, so floats are compared via fabsf
 * epsilon / bit helpers, never TEST_ASSERT_EQUAL_FLOAT. */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "color/nt_color.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static bool approx(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* ---- pack / unpack round-trip ---- */

void test_pack_unpack_roundtrip(void) {
    const uint32_t samples[] = {
        0x00000000U, 0xFFFFFFFFU, 0xFF000000U, 0x000000FFU, 0xFF202020U, 0xFF80B0E0U, 0x8040C0FFU, 0x12345678U, 0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU, 0xFFD8D8D8U,
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        float rgba[4];
        nt_color_unpack(samples[i], rgba);
        /* every lane must land in [0,1] */
        for (int k = 0; k < 4; k++) {
            TEST_ASSERT_TRUE(rgba[k] >= 0.0F && rgba[k] <= 1.0F);
        }
        const uint32_t back = nt_color_pack(rgba);
        TEST_ASSERT_EQUAL_HEX32(samples[i], back);
    }
}

/* unpack lane order: 0xAABBGGRR -> R,G,B,A normalized */
void test_unpack_lane_order(void) {
    float rgba[4];
    nt_color_unpack(0x80402010U, rgba); /* A=0x80 B=0x40 G=0x20 R=0x10 */
    TEST_ASSERT_TRUE(approx(rgba[0], 0x10 / 255.0F, 1e-6F));
    TEST_ASSERT_TRUE(approx(rgba[1], 0x20 / 255.0F, 1e-6F));
    TEST_ASSERT_TRUE(approx(rgba[2], 0x40 / 255.0F, 1e-6F));
    TEST_ASSERT_TRUE(approx(rgba[3], 0x80 / 255.0F, 1e-6F));
}

/* pack clamps out-of-range channels */
void test_pack_clamps(void) {
    const float over[4] = {2.0F, -1.0F, 0.5F, 1.0F};
    const uint32_t p = nt_color_pack(over);
    TEST_ASSERT_EQUAL_HEX32(0xFFU, p & 0xFFU);         /* R clamped hi -> 255 */
    TEST_ASSERT_EQUAL_HEX32(0x00U, (p >> 8) & 0xFFU);  /* G clamped lo -> 0 */
    TEST_ASSERT_EQUAL_HEX32(128U, (p >> 16) & 0xFFU);  /* B 0.5 -> round(127.5)=128 */
    TEST_ASSERT_EQUAL_HEX32(0xFFU, (p >> 24) & 0xFFU); /* A 1.0 -> 255 */
}

/* ---- sRGB <-> linear ---- */

void test_srgb_linear_endpoints_exact(void) {
    TEST_ASSERT_TRUE(approx(nt_color_srgb_to_linear(0.0F), 0.0F, 0.0F));
    TEST_ASSERT_TRUE(approx(nt_color_srgb_to_linear(1.0F), 1.0F, 1e-6F));
    TEST_ASSERT_TRUE(approx(nt_color_linear_to_srgb(0.0F), 0.0F, 0.0F));
    TEST_ASSERT_TRUE(approx(nt_color_linear_to_srgb(1.0F), 1.0F, 1e-6F));
}

void test_srgb_linear_roundtrip(void) {
    for (int i = 0; i <= 20; i++) {
        const float c = (float)i / 20.0F;
        const float rt = nt_color_linear_to_srgb(nt_color_srgb_to_linear(c));
        TEST_ASSERT_TRUE(approx(rt, c, 1e-4F));
    }
}

/* ---- OKLab round-trip (packed -> oklab -> packed) ---- */

void test_oklab_packed_roundtrip(void) {
    const uint32_t samples[] = {
        0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU, /* pure R/G/B */
        0xFF808080U,                           /* gray */
        0xFF000000U, 0xFFFFFFFFU,              /* black / white */
        0xFF202020U, 0xFF80B0E0U, 0xFFD8D8D8U, /* skin samples */
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        const nt_oklab_t o = nt_color_packed_to_oklab(samples[i], true);
        const uint32_t back = nt_color_oklab_to_packed(o);
        /* opaque round-trip is byte-exact in practice; allow 1 LSB per RGB lane for safety */
        for (int sh = 0; sh < 24; sh += 8) {
            const int a = (int)((samples[i] >> (unsigned)sh) & 0xFFU);
            const int b = (int)((back >> (unsigned)sh) & 0xFFU);
            TEST_ASSERT_INT_WITHIN(1, a, b);
        }
        /* alpha lane must be preserved exactly */
        TEST_ASSERT_EQUAL_HEX32(samples[i] & 0xFF000000U, back & 0xFF000000U);
    }
}

/* with_alpha == false yields alpha 0 (the appear/disappear fade) */
void test_packed_to_oklab_alpha_gate(void) {
    const nt_oklab_t with = nt_color_packed_to_oklab(0xFF202020U, true);
    const nt_oklab_t without = nt_color_packed_to_oklab(0xFF202020U, false);
    TEST_ASSERT_TRUE(approx(with.alpha, 1.0F, 1e-6F));
    TEST_ASSERT_TRUE(approx(without.alpha, 0.0F, 0.0F));
    /* L,a,b identical regardless of the alpha gate */
    TEST_ASSERT_TRUE(approx(with.l, without.l, 1e-6F));
    TEST_ASSERT_TRUE(approx(with.a, without.a, 1e-6F));
    TEST_ASSERT_TRUE(approx(with.b, without.b, 1e-6F));
}

/* ---- perceptual lerp ---- */

/* Compare all four oklab lanes against expected within eps (keeps the per-t tests below
 * under the cognitive-complexity threshold). */
static void assert_oklab_eq(nt_oklab_t got, nt_oklab_t want, float eps) {
    TEST_ASSERT_TRUE(approx(got.l, want.l, eps));
    TEST_ASSERT_TRUE(approx(got.a, want.a, eps));
    TEST_ASSERT_TRUE(approx(got.b, want.b, eps));
    TEST_ASSERT_TRUE(approx(got.alpha, want.alpha, eps));
}

static const nt_oklab_t LERP_TGT = {.l = 0.8F, .a = 0.1F, .b = -0.2F, .alpha = 1.0F};
static const nt_oklab_t LERP_CUR = {.l = 0.2F, .a = -0.05F, .b = 0.3F, .alpha = 0.0F};

void test_oklab_lerp_t0_unchanged(void) {
    nt_oklab_t c = LERP_CUR;
    nt_color_oklab_lerp(&c, LERP_TGT, 0.0F);
    assert_oklab_eq(c, LERP_CUR, 0.0F);
}

void test_oklab_lerp_t1_reaches_target(void) {
    nt_oklab_t c = LERP_CUR;
    nt_color_oklab_lerp(&c, LERP_TGT, 1.0F);
    assert_oklab_eq(c, LERP_TGT, 1e-6F);
}

void test_oklab_lerp_t_half_midpoint(void) {
    nt_oklab_t c = LERP_CUR;
    nt_color_oklab_lerp(&c, LERP_TGT, 0.5F);
    const nt_oklab_t mid = {
        .l = 0.5F * (LERP_CUR.l + LERP_TGT.l),
        .a = 0.5F * (LERP_CUR.a + LERP_TGT.a),
        .b = 0.5F * (LERP_CUR.b + LERP_TGT.b),
        .alpha = 0.5F * (LERP_CUR.alpha + LERP_TGT.alpha),
    };
    assert_oklab_eq(c, mid, 1e-6F);
}

/* ---- BYTE-IDENTITY guard ----
 * Pins the migrated input fade path: packed -> oklab(present) -> packed must reproduce the
 * EXACT packed values the pre-migration nt_ui_input.c color_to_oklab/oklab_to_color produced.
 * Expected values captured from the pre-migration code BEFORE the refactor (opaque skin
 * colours round-trip byte-exact through the perceptual transform). A regression here means a
 * coefficient or rounding drift broke behavior preservation. */
void test_byte_identity_input_fade(void) {
    const struct {
        uint32_t in;
        uint32_t expect;
    } pins[] = {
        {0xFF202020U, 0xFF202020U}, /* idle bg (dark theme) */
        {0xFF303030U, 0xFF303030U}, /* focused bg */
        {0xFF505050U, 0xFF505050U}, /* idle border */
        {0xFF80B0E0U, 0xFF80B0E0U}, /* focused border (cool blue) */
        {0xFFF0F0F0U, 0xFFF0F0F0U}, /* light-theme idle bg */
        {0xFFFFFFFFU, 0xFFFFFFFFU}, /* white */
        {0xFF000000U, 0xFF000000U}, /* black */
        {0xFFFF0000U, 0xFFFF0000U}, /* pure red */
        {0xFF00FF00U, 0xFF00FF00U}, /* pure green */
        {0xFF0000FFU, 0xFF0000FFU}, /* pure blue */
        {0xFFD8D8D8U, 0xFFD8D8D8U}, /* disabled bg (light) */
        {0xFFB0B0B0U, 0xFFB0B0B0U}, /* disabled border (light) */
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        const uint32_t out = nt_color_oklab_to_packed(nt_color_packed_to_oklab(pins[i].in, true));
        TEST_ASSERT_EQUAL_HEX32(pins[i].expect, out);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pack_unpack_roundtrip);
    RUN_TEST(test_unpack_lane_order);
    RUN_TEST(test_pack_clamps);
    RUN_TEST(test_srgb_linear_endpoints_exact);
    RUN_TEST(test_srgb_linear_roundtrip);
    RUN_TEST(test_oklab_packed_roundtrip);
    RUN_TEST(test_packed_to_oklab_alpha_gate);
    RUN_TEST(test_oklab_lerp_t0_unchanged);
    RUN_TEST(test_oklab_lerp_t1_reaches_target);
    RUN_TEST(test_oklab_lerp_t_half_midpoint);
    RUN_TEST(test_byte_identity_input_fade);
    return UNITY_END();
}
