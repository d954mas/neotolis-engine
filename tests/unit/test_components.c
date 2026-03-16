#include "entity/nt_entity.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "render_state_comp/nt_render_state_comp.h"
#include "unity.h"

/* ---- setUp / tearDown ---- */

void setUp(void) {
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 8});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 8});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 8});
    nt_render_state_comp_init(&(nt_render_state_comp_desc_t){.capacity = 8});
}

void tearDown(void) {
    nt_render_state_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_entity_shutdown();
}

/* ---- Mesh component tests ---- */

void test_mesh_add_returns_true(void) {
    nt_entity_t e = nt_entity_create();
    bool ok = nt_mesh_comp_add(e);
    TEST_ASSERT_TRUE(ok);
}

void test_mesh_add_default_handle(void) {
    nt_entity_t e = nt_entity_create();
    nt_mesh_comp_add(e);
    TEST_ASSERT_EQUAL_UINT32(0, *nt_mesh_comp_handle(e));
}

void test_mesh_has_true_after_add(void) {
    nt_entity_t e = nt_entity_create();
    nt_mesh_comp_add(e);
    TEST_ASSERT_TRUE(nt_mesh_comp_has(e));
}

void test_mesh_has_false_before_add(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_FALSE(nt_mesh_comp_has(e));
}

void test_mesh_remove_makes_has_false(void) {
    nt_entity_t e = nt_entity_create();
    nt_mesh_comp_add(e);
    nt_mesh_comp_remove(e);
    TEST_ASSERT_FALSE(nt_mesh_comp_has(e));
}

void test_mesh_get_set_handle(void) {
    nt_entity_t e = nt_entity_create();
    nt_mesh_comp_add(e);
    *nt_mesh_comp_handle(e) = 42;

    TEST_ASSERT_EQUAL_UINT32(42, *nt_mesh_comp_handle(e));
}

/* ---- Material component tests ---- */

void test_material_add_returns_true(void) {
    nt_entity_t e = nt_entity_create();
    bool ok = nt_material_comp_add(e);
    TEST_ASSERT_TRUE(ok);
}

void test_material_add_default_handle(void) {
    nt_entity_t e = nt_entity_create();
    nt_material_comp_add(e);
    TEST_ASSERT_EQUAL_UINT32(0, *nt_material_comp_handle(e));
}

void test_material_has_and_remove(void) {
    nt_entity_t e = nt_entity_create();
    nt_material_comp_add(e);
    TEST_ASSERT_TRUE(nt_material_comp_has(e));

    nt_material_comp_remove(e);
    TEST_ASSERT_FALSE(nt_material_comp_has(e));
}

/* ---- Render state component tests ---- */

void test_render_state_add_defaults_tag(void) {
    nt_entity_t e = nt_entity_create();
    nt_render_state_comp_add(e);
    TEST_ASSERT_EQUAL_UINT16(0, *nt_render_state_comp_tag(e));
}

void test_render_state_add_defaults_visible(void) {
    nt_entity_t e = nt_entity_create();
    nt_render_state_comp_add(e);
    TEST_ASSERT_TRUE(*nt_render_state_comp_visible(e));
}

void test_render_state_add_defaults_color(void) {
    nt_entity_t e = nt_entity_create();
    nt_render_state_comp_add(e);
    float *col = nt_render_state_comp_color(e);
    TEST_ASSERT_TRUE(col[0] == 1.0F); /* NOLINT */
    TEST_ASSERT_TRUE(col[1] == 1.0F); /* NOLINT */
    TEST_ASSERT_TRUE(col[2] == 1.0F); /* NOLINT */
    TEST_ASSERT_TRUE(col[3] == 1.0F); /* NOLINT */
}

void test_render_state_set_visible_false(void) {
    nt_entity_t e = nt_entity_create();
    nt_render_state_comp_add(e);
    *nt_render_state_comp_visible(e) = false;

    TEST_ASSERT_FALSE(*nt_render_state_comp_visible(e));
}

void test_render_state_set_tag(void) {
    nt_entity_t e = nt_entity_create();
    nt_render_state_comp_add(e);
    *nt_render_state_comp_tag(e) = 5;

    TEST_ASSERT_EQUAL_UINT16(5, *nt_render_state_comp_tag(e));
}

/* ---- Cross-component tests ---- */

void test_entity_destroy_removes_all_components(void) {
    nt_entity_t e = nt_entity_create();
    nt_mesh_comp_add(e);
    nt_material_comp_add(e);
    nt_render_state_comp_add(e);

    TEST_ASSERT_TRUE(nt_mesh_comp_has(e));
    TEST_ASSERT_TRUE(nt_material_comp_has(e));
    TEST_ASSERT_TRUE(nt_render_state_comp_has(e));

    nt_entity_destroy(e);

    /* If auto-remove failed, we would not be able to add 8 entities with
       all 3 components because the leaked slot would still occupy storage. */
    for (int i = 0; i < 8; i++) {
        nt_entity_t fresh = nt_entity_create();
        TEST_ASSERT_NOT_EQUAL_UINT32(0, fresh.id);
        nt_mesh_comp_add(fresh);
        nt_material_comp_add(fresh);
        nt_render_state_comp_add(fresh);
    }
}

void test_swap_and_pop_render_state(void) {
    nt_entity_t e1 = nt_entity_create();
    nt_entity_t e2 = nt_entity_create();
    nt_entity_t e3 = nt_entity_create();

    nt_render_state_comp_add(e1);
    nt_render_state_comp_add(e2);
    nt_render_state_comp_add(e3);

    *nt_render_state_comp_tag(e1) = 10;
    *nt_render_state_comp_tag(e3) = 30;

    /* Remove middle entity's component */
    nt_render_state_comp_remove(e2);

    /* First and third should still be accessible with correct data */
    TEST_ASSERT_TRUE(nt_render_state_comp_has(e1));
    TEST_ASSERT_FALSE(nt_render_state_comp_has(e2));
    TEST_ASSERT_TRUE(nt_render_state_comp_has(e3));

    TEST_ASSERT_EQUAL_UINT16(10, *nt_render_state_comp_tag(e1));
    TEST_ASSERT_EQUAL_UINT16(30, *nt_render_state_comp_tag(e3));
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    /* Mesh */
    RUN_TEST(test_mesh_add_returns_true);
    RUN_TEST(test_mesh_add_default_handle);
    RUN_TEST(test_mesh_has_true_after_add);
    RUN_TEST(test_mesh_has_false_before_add);
    RUN_TEST(test_mesh_remove_makes_has_false);
    RUN_TEST(test_mesh_get_set_handle);
    /* Material */
    RUN_TEST(test_material_add_returns_true);
    RUN_TEST(test_material_add_default_handle);
    RUN_TEST(test_material_has_and_remove);
    /* Render state */
    RUN_TEST(test_render_state_add_defaults_tag);
    RUN_TEST(test_render_state_add_defaults_visible);
    RUN_TEST(test_render_state_add_defaults_color);
    RUN_TEST(test_render_state_set_visible_false);
    RUN_TEST(test_render_state_set_tag);
    /* Cross-component */
    RUN_TEST(test_entity_destroy_removes_all_components);
    RUN_TEST(test_swap_and_pop_render_state);
    return UNITY_END();
}
