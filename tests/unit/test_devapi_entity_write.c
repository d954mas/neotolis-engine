/* L2 devapi entity-write group (entity.set) via submit() (no socket): set writable component fields
   through the component apply() hooks. Asserts the happy single + batch paths land through the real
   setters (dirty / packed mirror / quaternion normalize) and every bad_params path (stale handle,
   unknown component/field, read-only field, arity/range/finite/degenerate, batch whole-or-nothing). */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdio.h>

/* clang-format off */
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "transform_comp/nt_transform_comp.h"
#include "devapi/nt_devapi_internal.h"
#include "unity.h"
/* clang-format on */

void setUp(void) {
    nt_entity_desc_t edesc = nt_entity_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_entity_init(&edesc));
    nt_transform_comp_desc_t tdesc = nt_transform_comp_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_transform_comp_init(&tdesc));
    nt_drawable_comp_desc_t ddesc = nt_drawable_comp_desc_defaults();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_drawable_comp_init(&ddesc));
    TEST_ASSERT_EQUAL(NT_OK, nt_devapi_init());
    nt_devapi_register_default();
}

void tearDown(void) {
    nt_devapi_shutdown();
    nt_drawable_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
}

/* ---- helpers ---- */

static const char *set_submit(uint32_t id, const char *rest) {
    static char buf[256];
    (void)snprintf(buf, sizeof(buf), "{\"method\":\"entity.set\",\"params\":{\"id\":%u,%s}}", id, rest);
    return nt_devapi_submit(buf);
}

static cJSON *parse_ok(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    return root;
}

static cJSON *result_of(cJSON *root) { return cJSON_GetObjectItemCaseSensitive(root, "result"); }

static void assert_bad_params(const char *resp) {
    TEST_ASSERT_NOT_NULL(resp);
    cJSON *root = cJSON_Parse(resp);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    TEST_ASSERT_EQUAL_STRING("bad_params", cJSON_GetObjectItemCaseSensitive(err, "code")->valuestring);
    cJSON_Delete(root);
}

static nt_entity_t make_transform_entity(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(e));
    nt_transform_comp_update(); /* clear the add-dirty so a write's dirty is observable */
    return e;
}

/* ---- happy single-field ---- */

static void test_set_position_lands_and_dirties(void) {
    nt_entity_t e = make_transform_entity();
    cJSON *root = parse_ok(set_submit(e.id, "\"component\":\"transform\",\"field\":\"position\",\"value\":[4,5,6]"));
    cJSON *r = result_of(root);
    TEST_ASSERT_EQUAL_STRING("transform", cJSON_GetObjectItemCaseSensitive(r, "component")->valuestring);
    TEST_ASSERT_EQUAL_STRING("position", cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r, "fields"), 0)->valuestring);
    cJSON_Delete(root);
    /* landed through the setter -> values written AND dirty set (the invariant a raw poke would skip). */
    TEST_ASSERT_EQUAL_INT(4, (int)nt_transform_comp_position(e)[0]);
    TEST_ASSERT_EQUAL_INT(6, (int)nt_transform_comp_position(e)[2]);
    TEST_ASSERT_TRUE(*nt_transform_comp_dirty(e));
}

static void test_set_rotation_normalized(void) {
    nt_entity_t e = make_transform_entity();
    /* non-unit quaternion is normalized by the setter; readback proves it. */
    cJSON *root = parse_ok(set_submit(e.id, "\"component\":\"transform\",\"field\":\"rotation\",\"value\":[0,0,0,2]"));
    cJSON_Delete(root);
    const float *q = nt_transform_comp_rotation(e);
    TEST_ASSERT_TRUE(fabsf(q[3] - 1.0F) < 0.001F); /* (0,0,0,2) -> (0,0,0,1) */
}

static void test_set_color_and_visible(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_drawable_comp_add(e));
    cJSON_Delete(parse_ok(set_submit(e.id, "\"component\":\"drawable\",\"field\":\"color\",\"value\":[0.5,0.25,0,1]")));
    const float *c = nt_drawable_comp_color(e);
    TEST_ASSERT_TRUE(fabsf(c[0] - 0.5F) < 0.001F);
    cJSON_Delete(parse_ok(set_submit(e.id, "\"component\":\"drawable\",\"field\":\"visible\",\"value\":false")));
    TEST_ASSERT_FALSE(*nt_drawable_comp_visible(e));
}

/* ---- bad_params paths ---- */

static void test_stale_handle_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    uint32_t id = e.id;
    nt_entity_destroy(e);
    assert_bad_params(set_submit(id, "\"component\":\"transform\",\"field\":\"position\",\"value\":[1,2,3]"));
}

static void test_unknown_component_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    assert_bad_params(set_submit(e.id, "\"component\":\"nope\",\"field\":\"position\",\"value\":[1,2,3]"));
}

static void test_entity_lacks_component_bad_params(void) {
    nt_entity_t e = nt_entity_create(); /* no transform added */
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"position\",\"value\":[1,2,3]"));
}

static void test_world_matrix_read_only_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"world_matrix\",\"value\":[1,2,3]"));
}

static void test_arity_mismatch_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    /* position is vec3; 4 numbers -> VEC4 -> kind mismatch. */
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"position\",\"value\":[1,2,3,4]"));
    /* rotation is vec4; 3 numbers -> VEC3 -> kind mismatch. */
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"rotation\",\"value\":[1,2,3]"));
}

static void test_non_finite_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    /* 1e400 overflows to inf on parse -> rejected. */
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"position\",\"value\":[1e400,0,0]"));
}

static void test_degenerate_quaternion_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"rotation\",\"value\":[0,0,0,0]"));
    /* Overflow magnitude: ||q||^2 -> +Inf in float would pass the <= gate and silently zero the quat;
       the !isfinite guard must reject it instead of returning a false ok:true. */
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"rotation\",\"value\":[1e38,1e38,1e38,1e38]"));
}

static void test_color_out_of_range_rejected_no_desync(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_drawable_comp_add(e));
    /* color 2.0 would desync the raw float vs the clamped packed byte -> rejected at the wire. */
    assert_bad_params(set_submit(e.id, "\"component\":\"drawable\",\"field\":\"color\",\"value\":[2,0,0,1]"));
    /* unchanged: still the default white. */
    TEST_ASSERT_TRUE(fabsf(nt_drawable_comp_color(e)[0] - 1.0F) < 0.001F);
}

/* ---- batch (whole-or-nothing) ---- */

static void test_batch_applies_all(void) {
    nt_entity_t e = make_transform_entity();
    cJSON_Delete(parse_ok(set_submit(e.id, "\"component\":\"transform\",\"fields\":{\"position\":[1,2,3],\"scale\":[2,2,2]}")));
    TEST_ASSERT_EQUAL_INT(1, (int)nt_transform_comp_position(e)[0]);
    TEST_ASSERT_EQUAL_INT(2, (int)nt_transform_comp_scale(e)[0]);
}

static void test_batch_atomic_rejects_all_on_one_bad(void) {
    nt_entity_t e = make_transform_entity();
    nt_transform_comp_set_position(e, 9.0F, 9.0F, 9.0F); /* known prior state */
    /* position valid, rotation degenerate -> whole batch rejected, position NOT moved. */
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"fields\":{\"position\":[1,2,3],\"rotation\":[0,0,0,0]}"));
    TEST_ASSERT_EQUAL_INT(9, (int)nt_transform_comp_position(e)[0]); /* unchanged */
}

static void test_field_and_fields_both_bad_params(void) {
    nt_entity_t e = make_transform_entity();
    assert_bad_params(set_submit(e.id, "\"component\":\"transform\",\"field\":\"position\",\"value\":[1,2,3],\"fields\":{\"scale\":[2,2,2]}"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_position_lands_and_dirties);
    RUN_TEST(test_set_rotation_normalized);
    RUN_TEST(test_set_color_and_visible);
    RUN_TEST(test_stale_handle_bad_params);
    RUN_TEST(test_unknown_component_bad_params);
    RUN_TEST(test_entity_lacks_component_bad_params);
    RUN_TEST(test_world_matrix_read_only_bad_params);
    RUN_TEST(test_arity_mismatch_bad_params);
    RUN_TEST(test_non_finite_bad_params);
    RUN_TEST(test_degenerate_quaternion_bad_params);
    RUN_TEST(test_color_out_of_range_rejected_no_desync);
    RUN_TEST(test_batch_applies_all);
    RUN_TEST(test_batch_atomic_rejects_all_on_one_bad);
    RUN_TEST(test_field_and_fields_both_bad_params);
    return UNITY_END();
}
