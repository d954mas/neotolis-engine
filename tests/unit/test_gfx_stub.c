#include "graphics/nt_gfx.h"
#include "unity.h"

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
}

void tearDown(void) { nt_gfx_shutdown(); }

static void test_stub_has_no_graphics_resources(void) {
    nt_shader_t shader = nt_gfx_make_shader(&(nt_shader_desc_t){.source = "void main(){}"});
    TEST_ASSERT_EQUAL_UINT32(0, shader.id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_program(shader, shader).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(NULL).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_vertex_input(NULL).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_buffer(NULL).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_texture(NULL).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_sampler(NULL).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_render_target(NULL).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_shader(NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_texture(NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(NULL, 0));
    TEST_ASSERT_FALSE(nt_gfx_program_ready((nt_program_t){1}));
    TEST_ASSERT_FALSE(nt_gfx_program_valid((nt_program_t){1}));
    TEST_ASSERT_FALSE(nt_gfx_texture_ready((nt_texture_t){1}));
    TEST_ASSERT_FALSE(nt_gfx_render_target_ready((nt_render_target_t){1}));
    TEST_ASSERT_FALSE(nt_gfx_apply_texture_bindings(NULL, 0));
    TEST_ASSERT_NULL(nt_gfx_get_mesh_info((nt_mesh_t){1}));
    TEST_ASSERT_EQUAL_UINT16(0, nt_gfx_max_meshes());
}

static void test_stub_returns_empty_queries_without_fabricating_pixels(void) {
    uint8_t pixels[4] = {1, 2, 3, 4};
    const uint8_t expected[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(nt_gfx_read_pixels(0, 0, 1, 1, pixels, sizeof(pixels)));
    TEST_ASSERT_EQUAL_MEMORY(expected, pixels, sizeof(pixels));
    uint16_t width = 10;
    uint16_t height = 20;
    TEST_ASSERT_FALSE(nt_gfx_texture_size((nt_texture_t){1}, &width, &height));
    TEST_ASSERT_EQUAL_UINT16(0, width);
    TEST_ASSERT_EQUAL_UINT16(0, height);
    uint64_t time_ns = 123;
    TEST_ASSERT_FALSE(nt_gfx_poll_segment_time_ns("frame", &time_ns));
    TEST_ASSERT_EQUAL_UINT64(0, time_ns);
    TEST_ASSERT_FALSE(nt_gfx_is_gpu_timing_supported());
    const nt_global_block_t *blocks = NULL;
    uint32_t count = 1;
    nt_gfx_register_global_block("Frame", 0);
    nt_gfx_get_global_blocks(&blocks, &count);
    TEST_ASSERT_NULL(blocks);
    TEST_ASSERT_EQUAL_UINT32(0, count);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gpu_caps()->max_texture_size);
}

static void test_stub_drops_draws_and_state_changes(void) {
    TEST_ASSERT_EQUAL_UINT16(0, nt_gfx_max_meshes());
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(NULL);
    nt_gfx_bind_pipeline((nt_pipeline_t){1});
    nt_gfx_bind_vertex_input((nt_vertex_input_t){1});
    TEST_ASSERT_FALSE(nt_gfx_apply_texture_bindings(NULL, 0));
    nt_gfx_set_uniform_int((nt_hash32_t){1}, 7);
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_draw(0, 3);
    nt_gfx_draw_instanced(0, 3, 4);
    nt_gfx_draw_indexed(0, 3, 3);
    nt_gfx_draw_indexed_instanced(0, 3, 3, 4);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_FALSE(nt_gfx_scissor_enabled());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_get_frame_draw_calls());
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.frame_stats.vertices);
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
}

/* The packer is pure and part of the gfx surface, so the stub links and runs it. */
static void test_stub_packs_pipeline_keys(void) {
    nt_pipeline_desc_t a = {.program.id = 5, .cull_mode = 1};
    nt_pipeline_desc_t b = {.program.id = 5, .cull_mode = 2};
    const nt_gfx_pipeline_key_t ka = nt_gfx_pipeline_key(&a);
    const nt_gfx_pipeline_key_t kb = nt_gfx_pipeline_key(&b);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_key_equal(&ka, &kb));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_has_no_graphics_resources);
    RUN_TEST(test_stub_returns_empty_queries_without_fabricating_pixels);
    RUN_TEST(test_stub_drops_draws_and_state_changes);
    RUN_TEST(test_stub_packs_pipeline_keys);
    return UNITY_END();
}
