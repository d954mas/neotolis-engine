/* D4 group arithmetic contract: the composition and inverse tables must satisfy
 * the group axioms AND agree with transform_point_texel, the routine they model. */

#include <stdint.h>

/* clang-format off */
#include "nt_atlas_format.h"
#include "nt_builder_atlas_geometry.h"
#include "unity.h"
/* clang-format on */

/* Deliberately asymmetric so a wrong dims-swap cannot hide. */
#define BOX_W 5
#define BOX_H 3

void setUp(void) {}
void tearDown(void) {}

void test_d4_closure(void) {
    for (uint8_t a = 0; a < 8; a++) {
        for (uint8_t b = 0; b < 8; b++) {
            TEST_ASSERT_LESS_THAN_UINT8(8, d4_compose(a, b));
        }
    }
}

void test_d4_identity(void) {
    for (uint8_t a = 0; a < 8; a++) {
        TEST_ASSERT_EQUAL_UINT8(a, d4_compose(NT_ATLAS_XFORM_IDENTITY, a));
        TEST_ASSERT_EQUAL_UINT8(a, d4_compose(a, NT_ATLAS_XFORM_IDENTITY));
    }
}

void test_d4_inverse(void) {
    for (uint8_t a = 0; a < 8; a++) {
        TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_IDENTITY, d4_compose(a, d4_inverse(a)));
        TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_IDENTITY, d4_compose(d4_inverse(a), a));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_d4_associativity(void) {
    for (uint8_t a = 0; a < 8; a++) {
        for (uint8_t b = 0; b < 8; b++) {
            for (uint8_t c = 0; c < 8; c++) {
                TEST_ASSERT_EQUAL_UINT8(d4_compose(a, d4_compose(b, c)), d4_compose(d4_compose(a, b), c));
            }
        }
    }
}

/* The check that catches a transposed table: two passes through
 * transform_point_texel must equal one pass through the composed value. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_d4_matches_transform_point(void) {
    for (uint8_t a = 0; a < 8; a++) {
        for (uint8_t b = 0; b < 8; b++) {
            uint32_t mid_w = 0;
            uint32_t mid_h = 0;
            d4_dims_after(b, BOX_W, BOX_H, &mid_w, &mid_h);
            for (int32_t y = 0; y < BOX_H; y++) {
                for (int32_t x = 0; x < BOX_W; x++) {
                    int32_t mx = 0;
                    int32_t my = 0;
                    transform_point_texel(x, y, b, BOX_W, BOX_H, &mx, &my);
                    int32_t two_x = 0;
                    int32_t two_y = 0;
                    transform_point_texel(mx, my, a, (int32_t)mid_w, (int32_t)mid_h, &two_x, &two_y);
                    int32_t one_x = 0;
                    int32_t one_y = 0;
                    transform_point_texel(x, y, d4_compose(a, b), BOX_W, BOX_H, &one_x, &one_y);
                    TEST_ASSERT_EQUAL_INT32(one_x, two_x);
                    TEST_ASSERT_EQUAL_INT32(one_y, two_y);
                }
            }
        }
    }
}

void test_d4_composed_dims_agree(void) {
    for (uint8_t a = 0; a < 8; a++) {
        for (uint8_t b = 0; b < 8; b++) {
            uint32_t mid_w = 0;
            uint32_t mid_h = 0;
            d4_dims_after(b, BOX_W, BOX_H, &mid_w, &mid_h);
            uint32_t two_w = 0;
            uint32_t two_h = 0;
            d4_dims_after(a, mid_w, mid_h, &two_w, &two_h);
            uint32_t one_w = 0;
            uint32_t one_h = 0;
            d4_dims_after(d4_compose(a, b), BOX_W, BOX_H, &one_w, &one_h);
            TEST_ASSERT_EQUAL_UINT32(one_w, two_w);
            TEST_ASSERT_EQUAL_UINT32(one_h, two_h);
        }
    }
}

void test_d4_spot_checks(void) {
    TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_ROT180, d4_compose(NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT90));
    TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_IDENTITY, d4_compose(NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT270));
    TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_ROT180, d4_compose(NT_ATLAS_XFORM_FLIP_H, NT_ATLAS_XFORM_FLIP_V));
    TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_ROT270, d4_compose(NT_ATLAS_XFORM_TRANSPOSE, NT_ATLAS_XFORM_FLIP_H));
    /* D4 is non-abelian — this is why the anchor rule avoids composing masks. */
    TEST_ASSERT_NOT_EQUAL_UINT8(d4_compose(NT_ATLAS_XFORM_FLIP_H, NT_ATLAS_XFORM_TRANSPOSE), d4_compose(NT_ATLAS_XFORM_TRANSPOSE, NT_ATLAS_XFORM_FLIP_H));
}

void test_d4_dims_after(void) {
    static const uint8_t diagonal[4] = {NT_ATLAS_XFORM_TRANSPOSE, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT270, NT_ATLAS_XFORM_ANTITRANSPOSE};
    static const uint8_t straight[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_FLIP_H, NT_ATLAS_XFORM_FLIP_V, NT_ATLAS_XFORM_ROT180};
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t w = 0;
        uint32_t h = 0;
        d4_dims_after(diagonal[i], 7, 2, &w, &h);
        TEST_ASSERT_EQUAL_UINT32(2, w);
        TEST_ASSERT_EQUAL_UINT32(7, h);
        d4_dims_after(straight[i], 7, 2, &w, &h);
        TEST_ASSERT_EQUAL_UINT32(7, w);
        TEST_ASSERT_EQUAL_UINT32(2, h);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_d4_closure);
    RUN_TEST(test_d4_identity);
    RUN_TEST(test_d4_inverse);
    RUN_TEST(test_d4_associativity);
    RUN_TEST(test_d4_matches_transform_point);
    RUN_TEST(test_d4_composed_dims_agree);
    RUN_TEST(test_d4_spot_checks);
    RUN_TEST(test_d4_dims_after);
    return UNITY_END();
}
