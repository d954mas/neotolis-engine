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

void test_add_returns_nonnull(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);
    TEST_ASSERT_NOT_NULL(t);
}

void test_add_defaults_position(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);
    ASSERT_FLOAT_NEAR(0.0F, t->local_position[0]);
    ASSERT_FLOAT_NEAR(0.0F, t->local_position[1]);
    ASSERT_FLOAT_NEAR(0.0F, t->local_position[2]);
}

void test_add_defaults_rotation(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);
    /* Identity quaternion: (0, 0, 0, 1) -- w at index 3 in cglm */
    ASSERT_FLOAT_NEAR(0.0F, t->local_rotation[0]);
    ASSERT_FLOAT_NEAR(0.0F, t->local_rotation[1]);
    ASSERT_FLOAT_NEAR(0.0F, t->local_rotation[2]);
    ASSERT_FLOAT_NEAR(1.0F, t->local_rotation[3]);
}

void test_add_defaults_scale(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);
    ASSERT_FLOAT_NEAR(1.0F, t->local_scale[0]);
    ASSERT_FLOAT_NEAR(1.0F, t->local_scale[1]);
    ASSERT_FLOAT_NEAR(1.0F, t->local_scale[2]);
}

void test_add_defaults_dirty(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(t->dirty);
}

void test_get_returns_same_as_add(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *added = nt_transform_comp_add(e);
    nt_transform_comp_t *got = nt_transform_comp_get(e);
    TEST_ASSERT_EQUAL_PTR(added, got);
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

    nt_transform_comp_t *t1 = nt_transform_comp_add(e1);
    t1->local_position[0] = 1.0F;
    nt_transform_comp_add(e2);
    nt_transform_comp_t *t3 = nt_transform_comp_add(e3);
    t3->local_position[0] = 3.0F;

    /* Remove middle element */
    nt_transform_comp_remove(e2);

    /* Verify e1 and e3 still accessible with correct data */
    TEST_ASSERT_TRUE(nt_transform_comp_has(e1));
    TEST_ASSERT_TRUE(nt_transform_comp_has(e3));
    ASSERT_FLOAT_NEAR(1.0F, nt_transform_comp_get(e1)->local_position[0]);
    ASSERT_FLOAT_NEAR(3.0F, nt_transform_comp_get(e3)->local_position[0]);
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
    nt_transform_comp_t *t = nt_transform_comp_add(e);

    /* Set position to (1, 2, 3), identity rotation, scale (1,1,1) */
    t->local_position[0] = 1.0F;
    t->local_position[1] = 2.0F;
    t->local_position[2] = 3.0F;
    t->dirty = true;

    nt_transform_comp_update();

    /* cglm column-major: translation is in column 3 */
    ASSERT_FLOAT_NEAR(1.0F, t->world_matrix[3][0]);
    ASSERT_FLOAT_NEAR(2.0F, t->world_matrix[3][1]);
    ASSERT_FLOAT_NEAR(3.0F, t->world_matrix[3][2]);
}

void test_update_clears_dirty(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);
    TEST_ASSERT_TRUE(t->dirty);

    nt_transform_comp_update();
    TEST_ASSERT_FALSE(t->dirty);
}

void test_update_skips_clean(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_t *t = nt_transform_comp_add(e);

    /* First update to clear dirty and set identity world matrix */
    nt_transform_comp_update();
    TEST_ASSERT_FALSE(t->dirty);

    /* Manually set dirty=false, change position -- update should skip */
    t->local_position[0] = 5.0F;
    t->local_position[1] = 5.0F;
    t->local_position[2] = 5.0F;
    t->dirty = false;

    nt_transform_comp_update();

    /* World matrix should still be identity (translation column = 0) */
    ASSERT_FLOAT_NEAR(0.0F, t->world_matrix[3][0]);
    ASSERT_FLOAT_NEAR(0.0F, t->world_matrix[3][1]);
    ASSERT_FLOAT_NEAR(0.0F, t->world_matrix[3][2]);
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
        nt_transform_comp_t *t = nt_transform_comp_add(entities[i]);
        TEST_ASSERT_NOT_NULL(t);
    }
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_add_returns_nonnull);
    RUN_TEST(test_add_defaults_position);
    RUN_TEST(test_add_defaults_rotation);
    RUN_TEST(test_add_defaults_scale);
    RUN_TEST(test_add_defaults_dirty);
    RUN_TEST(test_get_returns_same_as_add);
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
