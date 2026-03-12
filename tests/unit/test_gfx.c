#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "unity.h"

void setUp(void) { nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_pipelines = 4, .max_buffers = 4}); }

void tearDown(void) { nt_gfx_shutdown(); }

/* ---- Pool: alloc returns nonzero ---- */

void test_gfx_pool_alloc_returns_nonzero(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t id = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: two allocs return different ids ---- */

void test_gfx_pool_alloc_unique(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t a = nt_gfx_pool_alloc(&pool);
    uint32_t b = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL_UINT32(a, b);
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: free then realloc gives new generation ---- */

void test_gfx_pool_free_and_realloc(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t first = nt_gfx_pool_alloc(&pool);
    nt_gfx_pool_free(&pool, first);
    uint32_t second = nt_gfx_pool_alloc(&pool);
    /* Same slot index, different generation -> different id */
    TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
    /* Same slot index */
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_pool_slot_index(first), nt_gfx_pool_slot_index(second));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: valid accepts live handle ---- */

void test_gfx_pool_valid_accepts_live(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t id = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_TRUE(nt_gfx_pool_valid(&pool, id));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: valid rejects zero ---- */

void test_gfx_pool_valid_rejects_zero(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    TEST_ASSERT_FALSE(nt_gfx_pool_valid(&pool, 0));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: valid rejects stale handle ---- */

void test_gfx_pool_valid_rejects_stale(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t id = nt_gfx_pool_alloc(&pool);
    nt_gfx_pool_free(&pool, id);
    TEST_ASSERT_FALSE(nt_gfx_pool_valid(&pool, id));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: full pool returns zero ---- */

void test_gfx_pool_full_returns_zero(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 2);
    nt_gfx_pool_alloc(&pool);
    nt_gfx_pool_alloc(&pool);
    uint32_t third = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_EQUAL_UINT32(0, third);
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: slot_index extracts correctly ---- */

void test_gfx_slot_index_extracts_correctly(void) {
    /* Construct a known handle: generation=3, slot=5 */
    uint32_t id = (3U << NT_GFX_SLOT_SHIFT) | 5U;
    TEST_ASSERT_EQUAL_UINT32(5, nt_gfx_pool_slot_index(id));
}

/* ---- High-level: init/shutdown transitions initialized flag ---- */

void test_gfx_init_shutdown(void) {
    /* setUp already called nt_gfx_init, so initialized should be true */
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
    nt_gfx_shutdown();
    TEST_ASSERT_FALSE(g_nt_gfx.initialized);
    /* Re-init for tearDown */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_pipelines = 4, .max_buffers = 4});
}

/* ---- High-level: make/destroy shader ---- */

void test_gfx_make_destroy_shader(void) {
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "test"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shd.id);
    nt_gfx_destroy_shader(shd);
    /* After destroy, making a new one should still work */
    nt_shader_t shd2 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "test2"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shd2.id);
}

/* ---- High-level: make/destroy buffer ---- */

void test_gfx_make_destroy_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    nt_gfx_destroy_buffer(buf);
}

/* ---- High-level: default pool sizes applied when desc is zeroed ---- */

void test_gfx_defaults_applied(void) {
    /* Shutdown current, re-init with zeroed desc */
    nt_gfx_shutdown();
    nt_gfx_init(&(nt_gfx_desc_t){0});
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    /* Verify we can allocate more than 4 shaders (proves defaults applied, not 0) */
    nt_shader_t shaders[10];
    for (int i = 0; i < 10; i++) {
        shaders[i] = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, shaders[i].id);
    }

    /* Re-init for tearDown */
    nt_gfx_shutdown();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_pipelines = 4, .max_buffers = 4});
}

/* ---- Pipeline: create with valid shaders, destroy ---- */

void test_gfx_make_destroy_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vs.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, fs.id);

    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .format = NT_FORMAT_FLOAT3}}},
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    nt_gfx_destroy_pipeline(pip);
}

/* ---- Pipeline: survives shader destroy ---- */

void test_gfx_pipeline_survives_shader_destroy(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .format = NT_FORMAT_FLOAT3}}},
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    /* Destroy shaders — pipeline should still be bindable */
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(pip);
}

/* ---- State machine: valid frame cycle ---- */

void test_gfx_state_machine_valid_cycle(void) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    /* Reaching here without crash means state machine accepted the sequence */
}

/* ---- Double destroy: shader ---- */

void test_gfx_double_destroy_shader(void) {
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "test"});
    nt_gfx_destroy_shader(shd);
    /* Second destroy on stale handle — must be no-op */
    nt_gfx_destroy_shader(shd);
}

/* ---- Double destroy: buffer ---- */

void test_gfx_double_destroy_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    nt_gfx_destroy_buffer(buf);
    nt_gfx_destroy_buffer(buf);
}

/* ---- Pipeline: rejected with invalid shaders ---- */

void test_gfx_pipeline_rejects_invalid_shaders(void) {
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = {.id = 0},
        .fragment_shader = {.id = 0},
    });
    TEST_ASSERT_EQUAL_UINT32(0, pip.id);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gfx_pool_alloc_returns_nonzero);
    RUN_TEST(test_gfx_pool_alloc_unique);
    RUN_TEST(test_gfx_pool_free_and_realloc);
    RUN_TEST(test_gfx_pool_valid_accepts_live);
    RUN_TEST(test_gfx_pool_valid_rejects_zero);
    RUN_TEST(test_gfx_pool_valid_rejects_stale);
    RUN_TEST(test_gfx_pool_full_returns_zero);
    RUN_TEST(test_gfx_slot_index_extracts_correctly);
    RUN_TEST(test_gfx_init_shutdown);
    RUN_TEST(test_gfx_make_destroy_shader);
    RUN_TEST(test_gfx_make_destroy_buffer);
    RUN_TEST(test_gfx_defaults_applied);
    RUN_TEST(test_gfx_make_destroy_pipeline);
    RUN_TEST(test_gfx_pipeline_survives_shader_destroy);
    RUN_TEST(test_gfx_state_machine_valid_cycle);
    RUN_TEST(test_gfx_double_destroy_shader);
    RUN_TEST(test_gfx_double_destroy_buffer);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_shaders);
    return UNITY_END();
}
