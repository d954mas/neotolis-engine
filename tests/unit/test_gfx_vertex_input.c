#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "nt_mesh_format.h"
#include "nt_vertex_locations.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/nt_gfx_test_frame.h"
#include "unity.h"

#include <setjmp.h>
#include <string.h>

#define TEST_MAX_VERTEX_INPUTS 4

/* --- Assert catching (setjmp/longjmp via hookable handler) --- */

static jmp_buf s_assert_jmp;

static const char *s_last_assert_expr;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)file;
    (void)line;
    s_last_assert_expr = expr;
    longjmp(s_assert_jmp, 1);
}

/* Instance binds must point inside this frame's allocations: 64 bytes in every sized vertex stream. */
static void alloc_instance_bytes(void) {
    for (uint32_t s = NT_GFX_FRAME_VERTEX; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        uint32_t offset = 0;
        if (g_nt_gfx_frame_storage[s].capacity >= 64U) {
            memset(nt_gfx_frame_alloc(s, 64, 4, &offset), 0, 64);
        }
    }
}

/* Program links finish across frames, so the bytes are allocated in the frame of the pass. */
static void begin_test_pass(void) {
    alloc_instance_bytes();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

#define EXPECT_ASSERT(code)                                                                                                                                                                            \
    do {                                                                                                                                                                                               \
        nt_assert_handler = test_assert_handler;                                                                                                                                                       \
        if (setjmp(s_assert_jmp) == 0) {                                                                                                                                                               \
            code;                                                                                                                                                                                      \
            nt_assert_handler = NULL;                                                                                                                                                                  \
            TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire");                                                                                                                                           \
        }                                                                                                                                                                                              \
        nt_assert_handler = NULL;                                                                                                                                                                      \
    } while (0)

static nt_instance_layout_t s_inst; /* inst_layout(), registered per gfx init */

static nt_vertex_layout_t inst_layout(void) { return (nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4}}}; }

void setUp(void) {
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS,
                                  .max_render_targets = 4));
    const nt_vertex_layout_t inst = inst_layout();
    s_inst = nt_gfx_make_instance_layout(&inst);
    nt_gfx_begin_frame();
    nt_gfx_fake_reset();
}

void tearDown(void) {
    nt_assert_handler = NULL;
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

/* --- Fixtures --- */

static const float s_verts[9] = {0};
static const uint16_t s_indices[3] = {0, 1, 2};

static nt_buffer_t make_vbo(void) { return nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = s_verts, .size = sizeof(s_verts)}); }

static nt_buffer_t make_ibo(void) {
    return nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_indices, .size = sizeof(s_indices), .index_type = NT_INDEX_UINT16});
}

static nt_vertex_layout_t pos_layout(void) { return (nt_vertex_layout_t){.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}}; }

static nt_vertex_input_t make_vi(nt_buffer_t vbo, nt_buffer_t ibo) { return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = vbo, .index_buffer = ibo}); }

/* inst_layout() instances at offset of `buffer`. */
static void bind_inst(nt_vertex_input_t vi, nt_buffer_t buffer, uint32_t offset) { nt_gfx_bind_vertex_input_instanced(vi, s_inst, buffer, offset); }

static nt_buffer_t frame0(void) { return nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX); }

/* A caller-owned instance buffer of 64 bytes: four 16-byte instances. */
static nt_buffer_t make_instance_buffer(void) { return nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 64}); }

/* Re-inits gfx with `count` sized vertex streams and opens a frame, as setUp does. */
static void init_with_streams(uint32_t count) {
    nt_gfx_end_frame();
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = (uint16_t)(8 + count), .max_textures = 4, .max_meshes = 4,
                                          .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS, .max_render_targets = 4);
    for (uint32_t k = 1; k < count; k++) {
        desc.frame_capacity[NT_GFX_FRAME_VERTEX + k] = 1024;
    }
    nt_gfx_init(&desc);
    const nt_vertex_layout_t inst = inst_layout();
    s_inst = nt_gfx_make_instance_layout(&inst);
    nt_gfx_begin_frame();
    nt_gfx_fake_reset();
}

static nt_pipeline_t make_test_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t program = nt_gfx_make_program(vs, fs);
    nt_test_gfx_link_wait(program);
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
}

/* --- Lifecycle --- */

void test_vi_make_valid_destroy(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vi.id);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_vertex_input_create_count());
    nt_gfx_destroy_vertex_input(vi);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
}

void test_vi_destroy_invalid_and_stale_are_noops(void) {
    nt_gfx_destroy_vertex_input(NT_VERTEX_INPUT_INVALID); /* no trap, no log side effects to check */
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_destroy_vertex_input(vi);
    nt_gfx_destroy_vertex_input(vi); /* stale: second destroy is a no-op */
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
}

void test_vi_slot_reuse_bumps_generation(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t first = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_destroy_vertex_input(first);
    nt_vertex_input_t second = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, second.id);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(first));
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(second));
}

void test_vi_empty_layout_is_attributeless(void) {
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    nt_gfx_destroy_vertex_input(vi);
}

void test_vi_backend_failure_releases_reserved_slot(void) {
    nt_buffer_t vbo = make_vbo();
    nt_gfx_fake_fail_next_vertex_input_create();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_EQUAL_UINT32(0, vi.id);
    /* Filling the whole pool exposes even one leaked reservation. */
    for (int i = 0; i < TEST_MAX_VERTEX_INPUTS; i++) {
        TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(make_vi(vbo, (nt_buffer_t){0})));
    }
}

void test_vi_pool_exhaustion_asserts(void) {
    nt_buffer_t vbo = make_vbo();
    for (int i = 0; i < TEST_MAX_VERTEX_INPUTS; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0, make_vi(vbo, (nt_buffer_t){0}).id);
    }
    EXPECT_ASSERT(make_vi(vbo, (nt_buffer_t){0}));
}

/* --- Creation validation --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_vi_creation_asserts_on_caller_errors(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();

    /* Location used twice in the vertex layout. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 2, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}, {.location = 0, .type = NT_VERTEX_FLOAT, .count = 1}}},
        .vertex_buffer = vbo,
    }));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "used twice in the vertex layout"));
    /* Nonempty layout without a vertex buffer, and the reverse. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout()}));
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.vertex_buffer = vbo}));
    /* Wrong buffer types both ways. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = ibo}));
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = vbo, .index_buffer = vbo}));
}

/* The instance side is checked at the fused bind: overlap with the vertex locations, the buffer's
 * type and liveness, and the layout handle. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_instanced_bind_asserts_on_caller_errors(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    const nt_instance_layout_t at_zero = nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 4}}});
    nt_buffer_t gone = make_instance_buffer();
    nt_gfx_destroy_buffer(gone);
    begin_test_pass();
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, at_zero, frame0(), 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "share a location"));
    EXPECT_ASSERT(bind_inst(vi, ibo, 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instance buffer is not vertex type"));
    EXPECT_ASSERT(bind_inst(vi, gone, 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid or stale instance buffer"));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_INSTANCE_LAYOUT_INVALID, frame0(), 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid instance layout"));
    /* A zero tuple, or one matching a previous vertex input's buffer and offset, is no cache hit. */
    nt_vertex_input_t other = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_bind_vertex_input(vi);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_INSTANCE_LAYOUT_INVALID, (nt_buffer_t){0}, 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid instance layout"));
    bind_inst(other, frame0(), 16);
    nt_gfx_bind_vertex_input(make_vi(vbo, (nt_buffer_t){0}));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_INSTANCE_LAYOUT_INVALID, frame0(), 16));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid instance layout"));
    nt_gfx_end_pass();
}

/* Frame storage buffers bound by this frame's `used`; after a restore replaces them, a game
 * buffer that may reuse an old frame buffer's slot bounds by its size. */
void test_instance_range_follows_frame_buffer_replacement(void) {
    nt_gfx_end_frame();
    nt_gfx_fake_lose_and_restore_context();
    nt_gfx_begin_frame(); /* restores: the frame buffers are new */
    nt_vertex_input_t vi = make_vi(make_vbo(), (nt_buffer_t){0});
    nt_buffer_t own = make_instance_buffer();
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    bind_inst(vi, frame0(), 48);
    nt_gfx_draw_instanced(0, 3, 1); /* the new frame buffer holds 64 bytes this frame */
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 2));
    bind_inst(vi, own, 48);
    nt_gfx_draw_instanced(0, 3, 1);
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 2));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    nt_gfx_end_pass();
}

void test_vi_creation_asserts_on_untyped_index_buffer(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t untyped_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_indices, .size = sizeof(s_indices)});
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = vbo, .index_buffer = untyped_ibo}));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_vi_creation_asserts_webgl2_rules(void) {
    nt_buffer_t vbo = make_vbo();
    /* WebGL2 guarantees only 16 attribute locations; 16 is already out of range */
    EXPECT_ASSERT(
        nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 16, .type = NT_VERTEX_FLOAT, .count = 3}}}, .vertex_buffer = vbo}));
    /* GL ignores normalized on float types; the contract allows it on integer types only */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .normalized = true}}}, .vertex_buffer = vbo}));
    /* f32 attr at offset 2: WebGL2 requires offset % 4 == 0 */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 16, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 2}}}, .vertex_buffer = vbo}));
    /* stride 13 with an f32 attr: WebGL2 requires stride % 4 == 0 */
    EXPECT_ASSERT(
        nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 13, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}}, .vertex_buffer = vbo}));
    /* An instance layout goes through the same rules */
    EXPECT_ASSERT(nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 16, .type = NT_VERTEX_FLOAT, .count = 4}}}));
    EXPECT_ASSERT(nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 2}}}));
    EXPECT_ASSERT(nt_gfx_make_instance_layout(
        &(nt_vertex_layout_t){.attr_count = 2, .stride = 16, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 2}, {.location = 4, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 8}}}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "used twice in the instance layout"));
    /* Attr-count caps. The expr checks pin WHICH assert fired: without them
     * a removed cap would still trap downstream on the zeroed attrs. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = {.attr_count = NT_GFX_MAX_VERTEX_ATTRS + 1, .stride = 4}, .vertex_buffer = vbo}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "NT_GFX_MAX_VERTEX_ATTRS"));
    EXPECT_ASSERT(nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = NT_GFX_MAX_INSTANCE_ATTRS + 1, .stride = 4}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "NT_GFX_MAX_INSTANCE_ATTRS"));
    EXPECT_ASSERT(nt_gfx_make_instance_layout(&(nt_vertex_layout_t){0}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "NT_GFX_MAX_INSTANCE_ATTRS"));
    /* Exactly NT_GFX_MAX_INSTANCE_ATTRS is accepted and survives the backend's compact copy. */
    nt_vertex_layout_t inst_full = {.attr_count = NT_GFX_MAX_INSTANCE_ATTRS, .stride = (uint16_t)(4 * NT_GFX_MAX_INSTANCE_ATTRS)};
    for (uint8_t i = 0; i < NT_GFX_MAX_INSTANCE_ATTRS; i++) {
        inst_full.attrs[i] = (nt_vertex_attr_t){.location = i, .type = NT_VERTEX_FLOAT, .count = 1, .offset = (uint16_t)(i * 4)};
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_make_instance_layout(&inst_full).id);
}

/* An equal layout returns the registered handle, so a renderer re-init never grows the table;
 * a change of any one field registers a new one. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_instance_layout_dedupes_equal_layouts(void) {
    const nt_vertex_layout_t base = {
        .attr_count = 2, .stride = 32, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4}, {.location = 5, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16}}};
    const nt_vertex_layout_t inst = inst_layout();
    TEST_ASSERT_EQUAL_UINT32(s_inst.id, nt_gfx_make_instance_layout(&inst).id);
    const uint32_t id = nt_gfx_make_instance_layout(&base).id;
    TEST_ASSERT_NOT_EQUAL_UINT32(s_inst.id, id);
    TEST_ASSERT_EQUAL_UINT32(id, nt_gfx_make_instance_layout(&base).id);
    nt_vertex_layout_t changed[6];
    for (uint32_t i = 0; i < 6; i++) {
        changed[i] = base;
    }
    changed[0].stride = 36;
    changed[1].attrs[1].location = 6;
    changed[2].attrs[1].type = NT_VERTEX_INT8;
    changed[3].attrs[1].count = 2;
    changed[4].attrs[1].normalized = false;
    changed[5].attrs[1].offset = 20;
    uint32_t ids[6];
    for (uint32_t i = 0; i < 6; i++) {
        ids[i] = nt_gfx_make_instance_layout(&changed[i]).id;
        TEST_ASSERT_NOT_EQUAL_UINT32(id, ids[i]);
        for (uint32_t j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL_UINT32(ids[j], ids[i]);
        }
    }
    nt_vertex_layout_t one_attr = base;
    one_attr.attr_count = 1;
    TEST_ASSERT_NOT_EQUAL_UINT32(id, nt_gfx_make_instance_layout(&one_attr).id);
}

/* A layout is CPU data: registering it works on a lost context and the handle binds after the
 * restore; a handle past the registered ones asserts. */
void test_instance_layout_lifetime(void) {
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_end_frame();
    nt_gfx_begin_frame(); /* latches the loss */
    const nt_vertex_layout_t other = {.attr_count = 1, .stride = 8, .attrs = {{.location = 6, .type = NT_VERTEX_FLOAT, .count = 2}}};
    const nt_instance_layout_t lost = nt_gfx_make_instance_layout(&other);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, lost.id);
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_end_frame();
    nt_gfx_begin_frame(); /* recovery completes */
    nt_vertex_input_t vi = make_vi(make_vbo(), (nt_buffer_t){0});
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(vi, lost, frame0(), 0);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, (nt_instance_layout_t){lost.id + 1U}, frame0(), 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid instance layout"));
    nt_gfx_end_pass();
}

void test_vi_stride_255_boundary(void) {
    nt_buffer_t vbo = make_vbo();
    /* WebGL2 caps vertexAttribPointer stride at 255; u8 attr keeps 255 alignment-legal */
    nt_vertex_input_t ok = nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 255, .attrs = {{.location = 0, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true}}}, .vertex_buffer = vbo});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(ok));
    nt_gfx_destroy_vertex_input(ok);

    EXPECT_ASSERT(nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 256, .attrs = {{.location = 0, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true}}}, .vertex_buffer = vbo}));
}

/* --- Destroy cascade --- */

void test_destroy_vbo_cascades_to_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t other_vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t untouched = make_vi(other_vbo, (nt_buffer_t){0});
    nt_gfx_destroy_buffer(vbo);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(untouched));
}

void test_destroy_ibo_cascades_to_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();
    nt_vertex_input_t vi = make_vi(vbo, ibo);
    nt_gfx_destroy_buffer(ibo);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
    /* The vertex buffer is untouched and usable for a fresh vertex input. */
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(make_vi(vbo, (nt_buffer_t){0})));
}

/* Instances are not baked: destroying a buffer they came from leaves the vertex input usable
 * with another one (the backend detaches the old buffer from its VAO). */
void test_destroy_instance_buffer_keeps_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t instances = make_instance_buffer();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    bind_inst(vi, instances, 0);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_destroy_buffer(instances);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    begin_test_pass();
    bind_inst(vi, frame0(), 16);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_buffer_backend_id(frame0()), nt_gfx_fake_last_instance_buffer());
}

void test_deactivate_mesh_cascades_to_vi(void) {
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 12 + 6];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_count = 1;
    hdr->index_count = 3;
    hdr->vertex_data_size = 12;
    hdr->index_data_size = 6;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
    sd->location = NT_LOC_POSITION;

    uint32_t handle = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info((nt_mesh_t){handle});
    nt_vertex_input_t vi = make_vi(info->vbo, info->ibo);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    nt_gfx_deactivate_mesh(handle);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
}

/* A one-vertex, one-triangle RAW mesh with `streams` packed in order. */
static nt_mesh_t activate_test_mesh(const NtStreamDesc *streams, uint8_t stream_count, uint32_t stride) {
    static uint8_t blob[sizeof(NtMeshAssetHeader) + (8 * sizeof(NtStreamDesc)) + 64 + 6];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = stream_count;
    hdr->index_type = 1;
    hdr->vertex_count = 1;
    hdr->index_count = 3;
    hdr->vertex_data_size = stride;
    hdr->index_data_size = 6;
    memcpy(blob + sizeof(NtMeshAssetHeader), streams, stream_count * sizeof(NtStreamDesc));
    const uint32_t size = (uint32_t)(sizeof(NtMeshAssetHeader) + (stream_count * sizeof(NtStreamDesc)) + stride + 6);
    return (nt_mesh_t){nt_gfx_activate_mesh(blob, size)};
}

/* Each stream type maps to its vertex type at the pack's location, offsets packed in stream order. */
void test_mesh_vertex_input_maps_every_stream_type(void) {
    const NtStreamDesc streams[] = {
        {.type = NT_STREAM_FLOAT32, .count = 3, .location = 5},
        {.type = NT_STREAM_FLOAT16, .count = 2, .location = 0},
        {.type = NT_STREAM_INT16, .count = 4, .normalized = 1, .location = 7},
        {.type = NT_STREAM_UINT16, .count = 2, .normalized = 1, .location = 2},
        {.type = NT_STREAM_UINT8, .count = 4, .location = 6},
        {.type = NT_STREAM_INT8, .count = 4, .normalized = 1, .location = 1},
    };
    const nt_mesh_t mesh = activate_test_mesh(streams, 6, 36);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, mesh.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_mesh_vertex_input(mesh, frame0()).id);
    const nt_vertex_layout_t got = nt_gfx_fake_last_vertex_input_layout();
    const nt_vertex_attr_t want[] = {
        {.location = 5, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
        {.location = 0, .type = NT_VERTEX_HALF, .count = 2, .offset = 12},
        {.location = 7, .type = NT_VERTEX_INT16, .count = 4, .normalized = true, .offset = 16},
        {.location = 2, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 24},
        {.location = 6, .type = NT_VERTEX_UINT8, .count = 4, .offset = 28},
        {.location = 1, .type = NT_VERTEX_INT8, .count = 4, .normalized = true, .offset = 32},
    };
    TEST_ASSERT_EQUAL_UINT8(6, got.attr_count);
    TEST_ASSERT_EQUAL_UINT16(36, got.stride);
    for (uint32_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT8(want[i].location, got.attrs[i].location);
        TEST_ASSERT_EQUAL_INT(want[i].type, got.attrs[i].type);
        TEST_ASSERT_EQUAL_UINT8(want[i].count, got.attrs[i].count);
        TEST_ASSERT_EQUAL(want[i].normalized, got.attrs[i].normalized);
        TEST_ASSERT_EQUAL_UINT16(want[i].offset, got.attrs[i].offset);
    }
}

/* Every frame vertex stream, the last included, gets its own vertex input; index, uniform and game
 * buffers share the base one. */
void test_mesh_vertex_input_per_buffer(void) {
    init_with_streams(NT_GFX_MAX_VERTEX_STREAMS);
    const nt_buffer_t last_stream = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + NT_GFX_MAX_VERTEX_STREAMS - 1);
    const NtStreamDesc position = {.type = NT_STREAM_FLOAT32, .count = 3, .location = NT_LOC_POSITION};
    const nt_mesh_t mesh = activate_test_mesh(&position, 1, 12);
    const nt_vertex_input_t first = nt_gfx_mesh_vertex_input(mesh, frame0());
    const nt_vertex_input_t last = nt_gfx_mesh_vertex_input(mesh, last_stream);
    const nt_vertex_input_t base = nt_gfx_mesh_vertex_input(mesh, make_instance_buffer());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, first.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, last.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, base.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, last.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, base.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(last.id, base.id);
    TEST_ASSERT_EQUAL_UINT32(base.id, nt_gfx_mesh_vertex_input(mesh, make_instance_buffer()).id);
    TEST_ASSERT_EQUAL_UINT32(base.id, nt_gfx_mesh_vertex_input(mesh, nt_gfx_frame_buffer(NT_GFX_FRAME_INDEX)).id);
    TEST_ASSERT_EQUAL_UINT32(base.id, nt_gfx_mesh_vertex_input(mesh, nt_gfx_frame_buffer(NT_GFX_FRAME_UNIFORM)).id);
    TEST_ASSERT_EQUAL_UINT32(last.id, nt_gfx_mesh_vertex_input(mesh, last_stream).id);
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_vertex_input_create_count());
    nt_gfx_deactivate_mesh(mesh.id);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(first));
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(last));
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(base));
}

/* --- Binding and mirrors --- */

void test_bind_vi_reaches_backend(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_bound_vertex_input());
}

/* A destroyed vertex input traps in both bind forms; the bound one stays. */
void test_bind_of_a_stale_vi_asserts(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t stale_inst = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t live = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_destroy_vertex_input(vi);
    nt_gfx_destroy_vertex_input(stale_inst);
    begin_test_pass();
    nt_gfx_bind_vertex_input(live);
    const uint32_t bound = nt_gfx_test_bound_vertex_input();
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(vi));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid or stale vertex input"));
    EXPECT_ASSERT(bind_inst(stale_inst, frame0(), 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "invalid or stale vertex input"));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(NT_VERTEX_INPUT_INVALID));
    TEST_ASSERT_EQUAL_UINT32(bound, nt_gfx_test_bound_vertex_input());
    nt_gfx_end_pass();
}

void test_bind_pipeline_preserves_bound_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    bind_inst(vi, frame0(), 16);
    uint32_t bound = nt_gfx_test_bound_vertex_input();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, bound);
    nt_gfx_bind_pipeline(pip);
    /* Orthogonal state: a pipeline change must not disturb the geometry bind. */
    TEST_ASSERT_EQUAL_UINT32(bound, nt_gfx_test_bound_vertex_input());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_last_instance_offset());
}

/* Bound state is pass-scoped: the next pass starts with nothing bound. */
void test_begin_pass_clears_bound_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});

    begin_test_pass();
    bind_inst(vi, frame0(), 0);
    nt_gfx_end_pass();

    begin_test_pass();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    /* The equal bind of the previous pass records again. */
    bind_inst(vi, frame0(), 0);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    nt_gfx_end_pass();
}

/* --- Instanced binds --- */

void test_bind_vertex_input_instanced_reaches_backend(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    bind_inst(vi, frame0(), 16); /* no pipeline needed on this path */
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_last_instance_offset());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_buffer_backend_id(frame0()), nt_gfx_fake_last_instance_buffer());
    TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(s_inst.id), nt_gfx_fake_last_instance_layout());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_last_bound_vertex_input(), nt_gfx_fake_last_instance_vertex_input());
}

/* The bind names the instance buffer: one vertex input reads from whichever stream it is bound with. */
void test_instanced_bind_names_its_buffer(void) {
    init_with_streams(3);
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    bind_inst(vi, frame0(), 16);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    const uint32_t first = nt_gfx_fake_last_instance_buffer();
    nt_gfx_begin_frame();
    begin_test_pass();
    bind_inst(vi, nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 2), 32);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_buffer_backend_id(nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX)), first);
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_buffer_backend_id(nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 2)), nt_gfx_fake_last_instance_buffer());
    TEST_ASSERT_EQUAL_UINT32(32, nt_gfx_fake_last_instance_offset());
}

/* Once bound with instances, a vertex input keeps them enabled in its VAO: a plain bind asserts,
 * also right after the instanced bind cached it and in a later frame. A vertex input never bound
 * with instances still draws instanced reading only gl_InstanceID. */
void test_plain_bind_of_an_instanced_vi_asserts(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t plain = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t inst = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(plain);
    nt_gfx_draw_instanced(0, 3, 4);
    bind_inst(inst, frame0(), 0);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(inst));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "was bound with instances"));
    nt_gfx_bind_vertex_input(plain);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(inst));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    begin_test_pass();
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(inst));
    nt_gfx_end_pass();
}

/* Instanced draws check the whole instance range: against this frame's allocations for a frame
 * stream (64 bytes here), against the size for a caller-owned buffer (64 bytes). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_instanced_draw_asserts_its_instance_range(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_buffer_t own = make_instance_buffer();
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    bind_inst(vi, frame0(), 32);
    nt_gfx_draw_instanced(0, 3, 2); /* ends exactly at `used` */
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 3));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    EXPECT_ASSERT(bind_inst(vi, frame0(), 64)); /* past `used`: last frame's bytes */
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    bind_inst(vi, own, 48);
    nt_gfx_draw_instanced(0, 3, 1);
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 2));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_draw_calls(&g_nt_gfx.counters));
    nt_gfx_end_pass();
}

/* The range covers what one instance reads, not the stride: an attribute may sit past the stride.
 * Stride 0 is GL tight packing, which the range cannot bound, so registration rejects it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_instance_range_uses_the_attribute_extent(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t instances = make_instance_buffer();
    EXPECT_ASSERT(nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 0, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4}}}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "stride must be 1..255"));
    const nt_instance_layout_t past = nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32}}});
    nt_vertex_input_t past_vi = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input_instanced(past_vi, past, instances, 16);
    nt_gfx_draw_instanced(0, 3, 1); /* reads 48..64 */
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 2));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 0xFFFFFFFFU)); /* no 32-bit wrap */
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(past_vi, past, instances, 0xFFFFFFF0U)); /* nor at the bind */
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    nt_gfx_end_pass();
}

/* Each frame stream bounds by its own allocations (stream 1 holds 128 bytes, stream 0 64). The
 * bind checks the first instance, all a plain draw reads; instanced draws check the whole range. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_every_draw_form_checks_its_own_stream(void) {
    init_with_streams(2);
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();
    nt_vertex_input_t vi1 = make_vi(vbo, ibo);
    nt_vertex_input_t vi0 = make_vi(vbo, ibo);
    const nt_buffer_t stream1 = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1);
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX + 1, 64, 4, &offset), 0, 64);
    nt_gfx_bind_pipeline(pip);
    bind_inst(vi1, stream1, 96);
    nt_gfx_draw_indexed_instanced(0, 3, 3, 2); /* ends at 128 */
    EXPECT_ASSERT(nt_gfx_draw_indexed_instanced(0, 3, 3, 3));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    EXPECT_ASSERT(bind_inst(vi0, frame0(), 64)); /* stream 0 holds only 64 */
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    bind_inst(vi0, frame0(), 48);
    nt_gfx_draw_indexed(0, 3, 3);
    nt_gfx_draw(0, 3);
    EXPECT_ASSERT(nt_gfx_draw_indexed_instanced(0, 3, 3, 2));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "instances lie outside"));
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_draw_calls(&g_nt_gfx.counters));
    nt_gfx_end_pass();
}

/* An equal (vertex input, layout, buffer, offset) in the same pass ends CACHE; a change of any
 * one of them binds. */
void test_equal_instance_binding_is_dropped(void) {
    init_with_streams(2);
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t a = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t b = make_vi(vbo, (nt_buffer_t){0});
    const nt_instance_layout_t other = nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 5, .type = NT_VERTEX_FLOAT, .count = 4}}});

    begin_test_pass();
    bind_inst(a, frame0(), 0);
    bind_inst(a, frame0(), 0);
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    bind_inst(a, frame0(), 16);
    bind_inst(b, frame0(), 16);
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    bind_inst(b, nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1), 16);
    nt_gfx_bind_vertex_input_instanced(b, other, nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1), 16);
    TEST_ASSERT_EQUAL_UINT32(5, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    nt_gfx_end_pass();
}

/* A plain vertex input in between unbinds A's VAO, so binding A again records again. */
void test_instanced_bind_after_a_plain_vertex_input_records_again(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t a = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t plain = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();

    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    bind_inst(a, frame0(), 0);
    nt_gfx_bind_vertex_input(plain);
    nt_gfx_draw(0, 3);
    bind_inst(a, frame0(), 0);
    nt_gfx_draw_instanced(0, 3, 2);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_draw_calls(&g_nt_gfx.counters));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_last_bound_vertex_input(), nt_gfx_fake_last_instance_vertex_input());
}

/* --- Instance buffer via bound vertex input --- */

/* --- Draw invariants --- */

void test_draw_indexed_asserts_on_non_indexed_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();
    nt_vertex_input_t indexed = make_vi(vbo, ibo);
    nt_vertex_input_t non_indexed = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();

    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(indexed);
    nt_gfx_draw_indexed(0, 3, 3); /* index type captured from the IBO */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_draw_calls(&g_nt_gfx.counters));
    /* A non-indexed vertex input CLEARS the index type -- indexed draw traps
     * instead of silently reusing the previous binding's type. */
    nt_gfx_bind_vertex_input(non_indexed);
    EXPECT_ASSERT(nt_gfx_draw_indexed(0, 3, 3));
    nt_gfx_end_pass();
}

void test_attributeless_vi_draws(void) {
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3); /* gl_VertexID path: no buffers, no attribs */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_draw_calls(&g_nt_gfx.counters));
    nt_gfx_end_pass();
}

/* Pipelines and vertex inputs bind orthogonally: switching pipelines over one
 * vertex input and switching vertex inputs under one pipeline both draw. */
void test_pipeline_and_vertex_input_bind_orthogonally(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();
    nt_vertex_input_t vi_indexed = make_vi(vbo, ibo);
    nt_vertex_input_t vi_plain = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip_a = make_test_pipeline();
    nt_pipeline_t pip_b = make_test_pipeline();

    begin_test_pass();

    nt_gfx_bind_pipeline(pip_a);
    nt_gfx_bind_vertex_input(vi_indexed);
    nt_gfx_draw_indexed(0, 3, 3);

    /* Pipeline switch under the same geometry: no re-bind of the vertex input. */
    nt_gfx_bind_pipeline(pip_b);
    nt_gfx_draw_indexed(0, 3, 3);

    /* Geometry switch under the same pipeline. */
    nt_gfx_bind_vertex_input(vi_plain);
    nt_gfx_draw(0, 3);

    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_draw_calls(&g_nt_gfx.counters));
    nt_gfx_end_pass();
}

/* WebGL2 rejects unaligned attrib offsets; the byte offset is asserted. */
void test_instanced_bind_rejects_unaligned_offset(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    bind_inst(vi, frame0(), 4);
    EXPECT_ASSERT(bind_inst(vi, frame0(), 1));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_fake_last_instance_offset()); /* only the aligned offset reached the backend */
}
/* Every draw variant requires a bound vertex input. */
void test_draw_without_vertex_input_asserts(void) {
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    EXPECT_ASSERT(nt_gfx_draw(0, 3));
    EXPECT_ASSERT(nt_gfx_draw_indexed(0, 3, 3));
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 1));
    EXPECT_ASSERT(nt_gfx_draw_indexed_instanced(0, 3, 3, 1));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&g_nt_gfx.counters));
    nt_gfx_end_pass();
}

/* --- Context loss --- */

void test_vi_make_during_context_loss_returns_invalid(void) {
    nt_buffer_t vbo = make_vbo();
    nt_gfx_fake_set_context_lost(true);
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_EQUAL_UINT32(0, vi.id);
    nt_gfx_fake_set_context_lost(false);
}

/* A lost frame wipes vertex-input metadata; binds return before reading it, so neither form asserts. */
void test_bind_during_a_lost_frame_records_nothing(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t inst = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t plain = make_vi(vbo, (nt_buffer_t){0});
    const nt_buffer_t frame = frame0();
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_end_frame();
    nt_gfx_begin_frame(); /* latches the loss, frees vertex-input slots */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    bind_inst(inst, frame, 0);
    nt_gfx_bind_vertex_input(plain);
    nt_gfx_end_pass();
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    nt_gfx_fake_set_context_lost(false);
}

/* Loss frees vertex-input slots outright: the handle goes stale, binding it traps, and
 * every slot is allocatable again. */
void test_vi_slots_freed_by_context_loss(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_end_frame();
    nt_gfx_begin_frame(); /* latches the loss, frees vertex-input slots */
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_end_frame();
    nt_gfx_begin_frame(); /* recovery completes */

    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
    begin_test_pass();
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(vi)); /* stale: the owner recreates it first */
    nt_gfx_end_pass();
    nt_gfx_destroy_vertex_input(vi); /* stale: tolerated no-op, also after a pass */
    nt_gfx_end_frame();
    nt_gfx_begin_frame();

    /* Buffers survive as husks; recreate before baking new vertex inputs. */
    nt_gfx_destroy_buffer(vbo);
    nt_buffer_t fresh = make_vbo();
    nt_vertex_input_t vis[TEST_MAX_VERTEX_INPUTS];
    for (uint32_t i = 0; i < TEST_MAX_VERTEX_INPUTS; i++) {
        vis[i] = make_vi(fresh, (nt_buffer_t){0});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, vis[i].id);
    }
    for (uint32_t i = 0; i < TEST_MAX_VERTEX_INPUTS; i++) {
        nt_gfx_destroy_vertex_input(vis[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vi_make_valid_destroy);
    RUN_TEST(test_vi_destroy_invalid_and_stale_are_noops);
    RUN_TEST(test_vi_slot_reuse_bumps_generation);
    RUN_TEST(test_vi_empty_layout_is_attributeless);
    RUN_TEST(test_vi_backend_failure_releases_reserved_slot);
    RUN_TEST(test_vi_pool_exhaustion_asserts);
    RUN_TEST(test_vi_creation_asserts_on_caller_errors);
    RUN_TEST(test_instanced_bind_asserts_on_caller_errors);
    RUN_TEST(test_instance_range_follows_frame_buffer_replacement);
    RUN_TEST(test_vi_creation_asserts_on_untyped_index_buffer);
    RUN_TEST(test_vi_creation_asserts_webgl2_rules);
    RUN_TEST(test_vi_stride_255_boundary);
    RUN_TEST(test_instance_layout_dedupes_equal_layouts);
    RUN_TEST(test_instance_layout_lifetime);
    RUN_TEST(test_destroy_vbo_cascades_to_vi);
    RUN_TEST(test_destroy_ibo_cascades_to_vi);
    RUN_TEST(test_destroy_instance_buffer_keeps_vi);
    RUN_TEST(test_deactivate_mesh_cascades_to_vi);
    RUN_TEST(test_mesh_vertex_input_maps_every_stream_type);
    RUN_TEST(test_mesh_vertex_input_per_buffer);
    RUN_TEST(test_bind_vi_reaches_backend);
    RUN_TEST(test_bind_of_a_stale_vi_asserts);
    RUN_TEST(test_bind_pipeline_preserves_bound_vi);
    RUN_TEST(test_begin_pass_clears_bound_vi);
    RUN_TEST(test_bind_vertex_input_instanced_reaches_backend);
    RUN_TEST(test_instanced_bind_names_its_buffer);
    RUN_TEST(test_plain_bind_of_an_instanced_vi_asserts);
    RUN_TEST(test_instanced_draw_asserts_its_instance_range);
    RUN_TEST(test_instance_range_uses_the_attribute_extent);
    RUN_TEST(test_every_draw_form_checks_its_own_stream);
    RUN_TEST(test_draw_indexed_asserts_on_non_indexed_vi);
    RUN_TEST(test_equal_instance_binding_is_dropped);
    RUN_TEST(test_instanced_bind_after_a_plain_vertex_input_records_again);
    RUN_TEST(test_attributeless_vi_draws);
    RUN_TEST(test_pipeline_and_vertex_input_bind_orthogonally);
    RUN_TEST(test_instanced_bind_rejects_unaligned_offset);
    RUN_TEST(test_draw_without_vertex_input_asserts);
    RUN_TEST(test_vi_make_during_context_loss_returns_invalid);
    RUN_TEST(test_bind_during_a_lost_frame_records_nothing);
    RUN_TEST(test_vi_slots_freed_by_context_loss);
    return UNITY_END();
}
