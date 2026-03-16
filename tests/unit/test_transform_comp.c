#include "entity/nt_entity.h"
#include "transform_comp/nt_transform_comp.h"
#include "unity.h"

#include <math.h>

/* Float comparison helper (Unity float assertions disabled globally) */
#define ASSERT_FLOAT_NEAR(expected, actual) TEST_ASSERT_TRUE_MESSAGE(fabsf((float)(expected) - (float)(actual)) < 0.001F, "float not within tolerance")

/* ---- setUp / tearDown ---- */

void setUp(void) {
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 8});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 8});
}

void tearDown(void) {
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
}

/* ---- Tests ---- */

void test_add_returns_true(void) {
    nt_entity_t e = nt_entity_create();
    bool ok = nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(ok);
}

void test_add_defaults_position(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    float *pos = nt_transform_comp_position(e);
    ASSERT_FLOAT_NEAR(0.0F, pos[0]);
    ASSERT_FLOAT_NEAR(0.0F, pos[1]);
    ASSERT_FLOAT_NEAR(0.0F, pos[2]);
}

void test_add_defaults_rotation(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    float *rot = nt_transform_comp_rotation(e);
    /* Identity quaternion: (0, 0, 0, 1) -- w at index 3 in cglm */
    ASSERT_FLOAT_NEAR(0.0F, rot[0]);
    ASSERT_FLOAT_NEAR(0.0F, rot[1]);
    ASSERT_FLOAT_NEAR(0.0F, rot[2]);
    ASSERT_FLOAT_NEAR(1.0F, rot[3]);
}

void test_add_defaults_scale(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    float *scl = nt_transform_comp_scale(e);
    ASSERT_FLOAT_NEAR(1.0F, scl[0]);
    ASSERT_FLOAT_NEAR(1.0F, scl[1]);
    ASSERT_FLOAT_NEAR(1.0F, scl[2]);
}

void test_add_defaults_dirty(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(*nt_transform_comp_dirty(e));
}

void test_has_true_after_add(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(nt_transform_comp_has(e));
}

void test_has_false_before_add(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_FALSE(nt_transform_comp_has(e));
}

void test_remove_makes_has_false(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    nt_transform_comp_remove(e);
    TEST_ASSERT_FALSE(nt_transform_comp_has(e));
}

void test_swap_and_pop_preserves_data(void) {
    nt_entity_t e1 = nt_entity_create();
    nt_entity_t e2 = nt_entity_create();
    nt_entity_t e3 = nt_entity_create();

    nt_transform_comp_add(e1);
    nt_transform_comp_position(e1)[0] = 1.0F;
    nt_transform_comp_add(e2);
    nt_transform_comp_add(e3);
    nt_transform_comp_position(e3)[0] = 3.0F;

    /* Remove middle element */
    nt_transform_comp_remove(e2);

    /* Verify e1 and e3 still accessible with correct data */
    TEST_ASSERT_TRUE(nt_transform_comp_has(e1));
    TEST_ASSERT_TRUE(nt_transform_comp_has(e3));
    ASSERT_FLOAT_NEAR(1.0F, nt_transform_comp_position(e1)[0]);
    ASSERT_FLOAT_NEAR(3.0F, nt_transform_comp_position(e3)[0]);
}

void test_sparse_init_invalid(void) {
    /* Indirectly verify sparse array initialized to UINT16_MAX via has() returning false */
    nt_entity_t e1 = nt_entity_create();
    nt_entity_t e2 = nt_entity_create();
    nt_entity_t e3 = nt_entity_create();
    TEST_ASSERT_FALSE(nt_transform_comp_has(e1));
    TEST_ASSERT_FALSE(nt_transform_comp_has(e2));
    TEST_ASSERT_FALSE(nt_transform_comp_has(e3));
}

void test_update_computes_world_matrix(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);

    /* Set position to (1, 2, 3), identity rotation, scale (1,1,1) */
    nt_transform_comp_position(e)[0] = 1.0F;
    nt_transform_comp_position(e)[1] = 2.0F;
    nt_transform_comp_position(e)[2] = 3.0F;
    *nt_transform_comp_dirty(e) = true;

    nt_transform_comp_update();

    /* cglm column-major: translation is in column 3 */
    const float *wm = nt_transform_comp_world_matrix(e);
    ASSERT_FLOAT_NEAR(1.0F, wm[12]); /* column 3, row 0 */
    ASSERT_FLOAT_NEAR(2.0F, wm[13]); /* column 3, row 1 */
    ASSERT_FLOAT_NEAR(3.0F, wm[14]); /* column 3, row 2 */
}

void test_update_clears_dirty(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(*nt_transform_comp_dirty(e));

    nt_transform_comp_update();
    TEST_ASSERT_FALSE(*nt_transform_comp_dirty(e));
}

void test_update_skips_clean(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);

    /* First update to clear dirty and set identity world matrix */
    nt_transform_comp_update();
    TEST_ASSERT_FALSE(*nt_transform_comp_dirty(e));

    /* Manually set dirty=false, change position -- update should skip */
    nt_transform_comp_position(e)[0] = 5.0F;
    nt_transform_comp_position(e)[1] = 5.0F;
    nt_transform_comp_position(e)[2] = 5.0F;
    *nt_transform_comp_dirty(e) = false;

    nt_transform_comp_update();

    /* World matrix should still be identity (translation column = 0) */
    const float *wm = nt_transform_comp_world_matrix(e);
    ASSERT_FLOAT_NEAR(0.0F, wm[12]);
    ASSERT_FLOAT_NEAR(0.0F, wm[13]);
    ASSERT_FLOAT_NEAR(0.0F, wm[14]);
}

void test_entity_destroy_auto_removes_component(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(nt_transform_comp_has(e));

    nt_entity_destroy(e);

    /* After destroy, the storage count should have decreased.
       Verify by creating max entities and adding transforms to all without
       hitting capacity -- proves the slot was freed. */
    nt_entity_t entities[8];
    for (int i = 0; i < 8; i++) {
        entities[i] = nt_entity_create();
        bool ok = nt_transform_comp_add(entities[i]);
        TEST_ASSERT_TRUE(ok);
    }
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_add_returns_true);
    RUN_TEST(test_add_defaults_position);
    RUN_TEST(test_add_defaults_rotation);
    RUN_TEST(test_add_defaults_scale);
    RUN_TEST(test_add_defaults_dirty);
    RUN_TEST(test_has_true_after_add);
    RUN_TEST(test_has_false_before_add);
    RUN_TEST(test_remove_makes_has_false);
    RUN_TEST(test_swap_and_pop_preserves_data);
    RUN_TEST(test_sparse_init_invalid);
    RUN_TEST(test_update_computes_world_matrix);
    RUN_TEST(test_update_clears_dirty);
    RUN_TEST(test_update_skips_clean);
    RUN_TEST(test_entity_destroy_auto_removes_component);
    return UNITY_END();
}
