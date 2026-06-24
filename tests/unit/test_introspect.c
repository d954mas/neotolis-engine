/* Entity introspection facility: the nt_entity_introspect walk + the text sink, exercised through
   nt_entity_to_string with synthetic components that emit every sink primitive. Built only under
   NT_INTROSPECT_ENABLED (the OFF mirror compiles stubs that would fail these assertions). */
#include "entity/nt_entity.h"
#include "introspect/nt_introspect.h"
#include "unity.h"

#include <string.h>

/* ---- Synthetic components ---- */

#define CAP 9
static bool s_alpha[CAP];
static bool s_beta[CAP];

static bool alpha_has(nt_entity_t e) {
    uint16_t i = nt_entity_index(e);
    return nt_entity_is_alive(e) && i < CAP && s_alpha[i];
}
static bool beta_has(nt_entity_t e) {
    uint16_t i = nt_entity_index(e);
    return nt_entity_is_alive(e) && i < CAP && s_beta[i];
}
static void noop_destroy(nt_entity_t e) { (void)e; }

/* alpha exercises the whole sink vocabulary: scalars, bool, floats, str, enum, ref, nested group. */
static void alpha_describe(nt_entity_t e, nt_introspect_sink *s) {
    (void)e;
    s->field_u64(s, "u", 42);
    s->field_i64(s, "i", -7);
    s->field_f32(s, "f", 1.5F);
    s->field_bool(s, "b", true);
    float v[3] = {1.0F, 2.0F, 3.0F};
    s->field_floats(s, "vec", v, 3);
    s->field_str(s, "name", "hi");
    s->field_enum(s, "state", "ready");
    s->field_ref(s, "tex", NT_REF_RESOURCE, 0x9AF3);
    s->field_asset(s, 1, 7);
    s->field_u64_hex(s, "h", 0xFEEDFACECAFEBEEFULL); /* 64-bit opaque id: exact hex, not the lossy double channel */
    s->begin_group(s, "nested");
    s->field_u64(s, "inner", 1);
    s->end_group(s);
}
static void beta_describe(nt_entity_t e, nt_introspect_sink *s) {
    (void)e;
    s->field_bool(s, "flag", false);
}

/* gamma: a marker component with NO describe — the walk must still emit it as an empty group. */
static bool s_gamma[CAP];
static bool gamma_has(nt_entity_t e) {
    uint16_t i = nt_entity_index(e);
    return nt_entity_is_alive(e) && i < CAP && s_gamma[i];
}

#if NT_INTROSPECT_WRITE_ENABLED
static float s_alpha_u; /* alpha_apply writes here so the test can confirm the write path ran */
static bool alpha_apply(nt_entity_t e, const char *key, const nt_write_value *v, bool dry_run, const char **err_msg) {
    (void)e;
    if (strcmp(key, "u") == 0) {
        if (v->kind != NT_WV_F32) {
            *err_msg = "u expects a number";
            return false;
        }
        if (!dry_run) {
            s_alpha_u = v->as.f32;
        }
        return true;
    }
    *err_msg = "unknown or read-only field";
    return false;
}
#endif

void setUp(void) {
    nt_entity_init(&(nt_entity_desc_t){.max_entities = CAP - 1});
    memset(s_alpha, 0, sizeof(s_alpha));
    memset(s_beta, 0, sizeof(s_beta));
    memset(s_gamma, 0, sizeof(s_gamma));
    nt_entity_register_storage(&(nt_comp_storage_reg_t){.name = "alpha",
                                                        .has = alpha_has,
                                                        .on_destroy = noop_destroy,
                                                        .describe = alpha_describe,
#if NT_INTROSPECT_WRITE_ENABLED
                                                        .apply = alpha_apply
#endif
    });
    nt_entity_register_storage(&(nt_comp_storage_reg_t){.name = "beta", .has = beta_has, .on_destroy = noop_destroy, .describe = beta_describe});
    nt_entity_register_storage(&(nt_comp_storage_reg_t){.name = "gamma", .has = gamma_has, .on_destroy = noop_destroy}); /* no describe */
}
void tearDown(void) { nt_entity_shutdown(); }

/* ---- Tests ---- */

void test_introspect_core_fields_no_components(void) {
    nt_entity_t e = nt_entity_create();
    char buf[256];
    nt_entity_to_string(e, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "id="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "index="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "generation="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "enabled=1"));
    /* No present component -> no group emitted (sparse by omission). */
    TEST_ASSERT_NULL(strstr(buf, "alpha{"));
    TEST_ASSERT_NULL(strstr(buf, "beta{"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_introspect_all_primitives(void) {
    nt_entity_t e = nt_entity_create();
    s_alpha[nt_entity_index(e)] = true;
    char buf[256];
    nt_entity_to_string(e, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "alpha{"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "u=42"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "i=-7"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "f=1.5"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "b=1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "vec=[1,2,3]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "name=hi"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "state=ready"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "tex=resource#0x9af3"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "handle=7 "));            /* field_asset: handle only (type drives the JSON sink's reverse-map, not emitted) */
    TEST_ASSERT_NULL(strstr(buf, "type="));                    /* the undocumented type= field is gone */
    TEST_ASSERT_NOT_NULL(strstr(buf, "h=0xfeedfacecafebeef")); /* field_u64_hex: exact 64-bit hex, no precision loss */
    TEST_ASSERT_NOT_NULL(strstr(buf, "nested{"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "inner=1"));
}

void test_introspect_empty_group_for_describeless(void) {
    nt_entity_t e = nt_entity_create();
    s_gamma[nt_entity_index(e)] = true;
    char buf[256];
    nt_entity_to_string(e, buf, sizeof(buf));
    /* gamma has no describe -> still emitted as an empty group, so a marker component's presence shows. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "gamma{"));
    TEST_ASSERT_NULL(strstr(buf, "alpha{"));
}

void test_introspect_only_present_components(void) {
    nt_entity_t e = nt_entity_create();
    s_beta[nt_entity_index(e)] = true; /* beta present, alpha absent */
    char buf[256];
    nt_entity_to_string(e, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "beta{"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "flag=0"));
    TEST_ASSERT_NULL(strstr(buf, "alpha{"));
}

void test_introspect_dead_entity(void) {
    nt_entity_t e = nt_entity_create();
    s_alpha[nt_entity_index(e)] = true;
    nt_entity_destroy(e);
    char buf[128];
    nt_entity_to_string(e, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "alive=0"));
    TEST_ASSERT_NULL(strstr(buf, "alpha{")); /* no component groups for a dead entity */
}

void test_introspect_truncation_safe(void) {
    nt_entity_t e = nt_entity_create();
    s_alpha[nt_entity_index(e)] = true;
    char small[16];
    nt_entity_to_string(e, small, sizeof(small));
    /* Always NUL-terminated within cap, never overflows. */
    TEST_ASSERT_TRUE(strlen(small) < sizeof(small));
}

void test_introspect_log_entity_smoke(void) {
    nt_entity_t e = nt_entity_create();
    s_alpha[nt_entity_index(e)] = true;
    /* Routes the text repr to the log sink (nt_log_stub) without crashing. */
    nt_log_entity(NT_LOG_LEVEL_INFO, e);
}

#if NT_INTROSPECT_WRITE_ENABLED
void test_introspect_apply_mechanism(void) {
    nt_entity_t e = nt_entity_create();
    s_alpha[nt_entity_index(e)] = true;
    const nt_comp_storage_reg_t *r = nt_entity_storage_at(nt_entity_storage_find("alpha"));
    TEST_ASSERT_NOT_NULL(r->apply);

    const char *err = NULL;
    nt_write_value wv = {.kind = NT_WV_F32, .as.f32 = 9.0F};
    /* dry_run validates but does not write. */
    s_alpha_u = 0.0F;
    TEST_ASSERT_TRUE(r->apply(e, "u", &wv, true, &err));
    TEST_ASSERT_EQUAL_INT(0, (int)s_alpha_u);
    /* real apply writes. */
    TEST_ASSERT_TRUE(r->apply(e, "u", &wv, false, &err));
    TEST_ASSERT_EQUAL_INT(9, (int)s_alpha_u);

    err = NULL;
    TEST_ASSERT_FALSE(r->apply(e, "nope", &wv, false, &err)); /* unknown key -> false + message */
    TEST_ASSERT_NOT_NULL(err);

    /* writable 'u' is also readable (appears in describe) — writable is a subset of readable. */
    char buf[256];
    nt_entity_to_string(e, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "u="));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_introspect_core_fields_no_components);
    RUN_TEST(test_introspect_all_primitives);
    RUN_TEST(test_introspect_only_present_components);
    RUN_TEST(test_introspect_empty_group_for_describeless);
    RUN_TEST(test_introspect_dead_entity);
    RUN_TEST(test_introspect_truncation_safe);
    RUN_TEST(test_introspect_log_entity_smoke);
#if NT_INTROSPECT_WRITE_ENABLED
    RUN_TEST(test_introspect_apply_mechanism);
#endif
    return UNITY_END();
}
