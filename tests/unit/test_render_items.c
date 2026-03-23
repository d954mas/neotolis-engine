#include <math.h>

#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "hash/nt_hash.h"
#include "render/nt_render_defs.h"
#include "render/nt_render_items.h"
#include "render/nt_render_util.h"
#include "transform_comp/nt_transform_comp.h"
#include "unity.h"

#define ASSERT_FLOAT_NEAR(expected, actual) TEST_ASSERT_TRUE_MESSAGE(fabsf((float)(expected) - (float)(actual)) < 0.001F, "float not within tolerance")

/* ---- setUp / tearDown ---- */

void setUp(void) {
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 16});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 16});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 16});
}

void tearDown(void) {
    nt_drawable_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
}

/* ---- Static assert tests (compile-time verified, runtime just confirms inclusion) ---- */

void test_globals_size(void) {
    /* _Static_assert in nt_render_defs.h guarantees 256 bytes at compile time */
    TEST_ASSERT_EQUAL_UINT32(256, sizeof(nt_frame_uniforms_t));
}

void test_instance_size(void) {
    /* _Static_assert in nt_render_defs.h guarantees 80 bytes at compile time */
    TEST_ASSERT_EQUAL_UINT32(80, sizeof(nt_mesh_instance_t));
}

void test_render_item_size(void) {
    /* _Static_assert in nt_render_defs.h guarantees 16 bytes at compile time */
    TEST_ASSERT_EQUAL_UINT32(16, sizeof(nt_render_item_t));
}

/* ---- Sort key opaque ---- */

void test_sort_key_opaque_encoding(void) {
    uint64_t key = nt_sort_key_opaque(1, 2);
    TEST_ASSERT_EQUAL_UINT64(((uint64_t)1 << 32) | 2, key);
}

void test_sort_key_opaque_material_primary(void) {
    uint64_t a = nt_sort_key_opaque(5, 3);
    uint64_t b = nt_sort_key_opaque(4, 999);
    TEST_ASSERT_TRUE(a > b);
}

/* ---- Depth to u16 ---- */

void test_depth_to_u16_boundaries(void) {
    TEST_ASSERT_EQUAL_UINT16(0, nt_depth_to_u16(0.1F, 0.1F, 100.0F));
    TEST_ASSERT_EQUAL_UINT16(65535, nt_depth_to_u16(100.0F, 0.1F, 100.0F));
}

void test_depth_to_u16_clamp(void) {
    TEST_ASSERT_EQUAL_UINT16(0, nt_depth_to_u16(-1.0F, 0.1F, 100.0F));
    TEST_ASSERT_EQUAL_UINT16(65535, nt_depth_to_u16(200.0F, 0.1F, 100.0F));
}

/* ---- Depth back to front ---- */

void test_sort_key_depth_back_to_front_ordering(void) {
    /* Far object should produce LOWER sort_key than near object (back-to-front) */
    uint64_t far_key = nt_sort_key_depth_back_to_front(90.0F, 0.1F, 100.0F);
    uint64_t near_key = nt_sort_key_depth_back_to_front(1.0F, 0.1F, 100.0F);
    TEST_ASSERT_TRUE(far_key < near_key);
}

/* ---- Sort key z ---- */

void test_sort_key_z_ordering(void) {
    uint64_t key_a = nt_sort_key_z(0.0F);
    uint64_t key_b = nt_sort_key_z(10.0F);
    TEST_ASSERT_TRUE(key_a < key_b);
}

/* ---- Sort by key ---- */

void test_sort_by_key_ascending(void) {
    nt_render_item_t items[5] = {
        {.sort_key = 50, .entity = 1}, {.sort_key = 10, .entity = 2}, {.sort_key = 40, .entity = 3}, {.sort_key = 20, .entity = 4}, {.sort_key = 30, .entity = 5},
    };
    nt_render_item_t scratch[5];
    nt_sort_by_key(items, 5, scratch);
    TEST_ASSERT_EQUAL_UINT64(10, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(20, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(30, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(40, items[3].sort_key);
    TEST_ASSERT_EQUAL_UINT64(50, items[4].sort_key);
}

void test_sort_by_key_empty(void) {
    /* Should not crash */
    nt_sort_by_key(NULL, 0, NULL);
}

void test_sort_by_key_single(void) {
    nt_render_item_t item = {.sort_key = 42, .entity = 1};
    nt_render_item_t scratch[1];
    nt_sort_by_key(&item, 1, scratch);
    TEST_ASSERT_EQUAL_UINT64(42, item.sort_key);
}

/* ---- Calc view depth ---- */

void test_calc_view_depth_positive(void) {
    nt_entity_t e = nt_entity_create();
    nt_transform_comp_add(e);

    /* Place entity at (0, 0, -5) */
    float *pos = nt_transform_comp_position(e);
    pos[0] = 0.0F;
    pos[1] = 0.0F;
    pos[2] = -5.0F;
    *nt_transform_comp_dirty(e) = true;
    nt_transform_comp_update();

    /* Camera at origin, looking down -Z */
    float view_pos[3] = {0.0F, 0.0F, 0.0F};
    float view_fwd[3] = {0.0F, 0.0F, -1.0F};

    float depth = nt_calc_view_depth(e.id, view_pos, view_fwd);
    TEST_ASSERT_TRUE(depth > 0.0F);
    ASSERT_FLOAT_NEAR(5.0F, depth);
}

/* ---- Visibility checks ---- */

void test_is_visible_dead_entity(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    nt_entity_destroy(e);
    TEST_ASSERT_FALSE(nt_render_is_visible(e));
}

void test_is_visible_disabled_entity(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    nt_entity_set_enabled(e, false);
    TEST_ASSERT_FALSE(nt_render_is_visible(e));
}

void test_is_visible_no_drawable(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_FALSE(nt_render_is_visible(e));
}

void test_is_visible_not_visible(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    *nt_drawable_comp_visible(e) = false;
    TEST_ASSERT_FALSE(nt_render_is_visible(e));
}

void test_is_visible_zero_alpha(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    float *color = nt_drawable_comp_color(e);
    color[3] = 0.0F;
    TEST_ASSERT_FALSE(nt_render_is_visible(e));
}

void test_is_visible_valid(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    /* Defaults: visible=true, color=(1,1,1,1), entity alive+enabled */
    TEST_ASSERT_TRUE(nt_render_is_visible(e));
}

/* ---- Drawable tag migration (nt_hash32_t) ---- */

void test_drawable_tag_default_hash(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    TEST_ASSERT_EQUAL_UINT32(0, nt_drawable_comp_tag(e)->value);
}

void test_drawable_tag_set_get_hash(void) {
    nt_entity_t e = nt_entity_create();
    nt_drawable_comp_add(e);
    *nt_drawable_comp_tag(e) = nt_hash32_str("world");

    nt_hash32_t expected = nt_hash32_str("world");
    TEST_ASSERT_EQUAL_UINT32(expected.value, nt_drawable_comp_tag(e)->value);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    /* Struct sizes */
    RUN_TEST(test_globals_size);
    RUN_TEST(test_instance_size);
    RUN_TEST(test_render_item_size);
    /* Sort key opaque */
    RUN_TEST(test_sort_key_opaque_encoding);
    RUN_TEST(test_sort_key_opaque_material_primary);
    /* Depth to u16 */
    RUN_TEST(test_depth_to_u16_boundaries);
    RUN_TEST(test_depth_to_u16_clamp);
    /* Depth back to front */
    RUN_TEST(test_sort_key_depth_back_to_front_ordering);
    /* Sort key z */
    RUN_TEST(test_sort_key_z_ordering);
    /* Sort by key */
    RUN_TEST(test_sort_by_key_ascending);
    RUN_TEST(test_sort_by_key_empty);
    RUN_TEST(test_sort_by_key_single);
    /* View depth */
    RUN_TEST(test_calc_view_depth_positive);
    /* Visibility */
    RUN_TEST(test_is_visible_dead_entity);
    RUN_TEST(test_is_visible_disabled_entity);
    RUN_TEST(test_is_visible_no_drawable);
    RUN_TEST(test_is_visible_not_visible);
    RUN_TEST(test_is_visible_zero_alpha);
    RUN_TEST(test_is_visible_valid);
    /* Drawable tag migration */
    RUN_TEST(test_drawable_tag_default_hash);
    RUN_TEST(test_drawable_tag_set_get_hash);
    return UNITY_END();
}
