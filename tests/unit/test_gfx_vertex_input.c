#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "nt_mesh_format.h"
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

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){
        .max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS, .max_render_targets = 4});
    nt_gfx_stub_test_reset();
}

void tearDown(void) {
    nt_assert_handler = NULL;
    nt_gfx_shutdown();
    nt_gfx_stub_test_reset();
}

/* --- Fixtures --- */

static const float s_verts[9] = {0};
static const uint16_t s_indices[3] = {0, 1, 2};

static nt_buffer_t make_vbo(void) { return nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = s_verts, .size = sizeof(s_verts)}); }

static nt_buffer_t make_ibo(void) {
    return nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_indices, .size = sizeof(s_indices), .index_type = NT_INDEX_UINT16});
}

static nt_vertex_layout_t pos_layout(void) { return (nt_vertex_layout_t){.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}}; }

static nt_vertex_layout_t inst_layout(void) { return (nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4}}}; }

static nt_vertex_input_t make_vi(nt_buffer_t vbo, nt_buffer_t ibo) { return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = vbo, .index_buffer = ibo}); }

static nt_pipeline_t make_test_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
}

/* --- Lifecycle --- */

void test_vi_make_valid_destroy(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vi.id);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_stub_test_vertex_input_create_count());
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
    nt_gfx_stub_test_fail_next_vertex_input_create();
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

void test_vi_creation_asserts_on_caller_errors(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();

    /* Location used twice across vertex/instance layouts. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = pos_layout(),
        .instance_layout = (nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 4}}},
        .vertex_buffer = vbo,
    }));
    /* Nonempty layout without a vertex buffer, and the reverse. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout()}));
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.vertex_buffer = vbo}));
    /* Wrong buffer types both ways. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = ibo}));
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = vbo, .index_buffer = vbo}));
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
    /* The instance layout goes through the same rules */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = pos_layout(),
        .instance_layout = {.attr_count = 1, .stride = 16, .attrs = {{.location = 16, .type = NT_VERTEX_FLOAT, .count = 4}}},
        .vertex_buffer = vbo,
    }));
    /* Attr-count caps. The expr checks pin WHICH assert fired: without them
     * a removed cap would still trap downstream on the zeroed attrs. */
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = {.attr_count = NT_GFX_MAX_VERTEX_ATTRS + 1, .stride = 4}, .vertex_buffer = vbo}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "NT_GFX_MAX_VERTEX_ATTRS"));
    EXPECT_ASSERT(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.instance_layout = {.attr_count = NT_GFX_MAX_INSTANCE_ATTRS + 1, .stride = 4}}));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "NT_GFX_MAX_INSTANCE_ATTRS"));
    /* Exactly NT_GFX_MAX_INSTANCE_ATTRS is accepted and survives the
     * backend's compact per-slot copy. */
    nt_vertex_layout_t inst_full = {.attr_count = NT_GFX_MAX_INSTANCE_ATTRS, .stride = 32};
    for (uint8_t i = 0; i < NT_GFX_MAX_INSTANCE_ATTRS; i++) {
        inst_full.attrs[i] = (nt_vertex_attr_t){.location = (uint8_t)(8 + i), .type = NT_VERTEX_FLOAT, .count = 1, .offset = (uint16_t)(i * 4)};
    }
    nt_vertex_input_t max_inst = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_full, .vertex_buffer = vbo});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(max_inst));
    nt_gfx_destroy_vertex_input(max_inst);
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
    sd->name_hash = 0x12345678;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info((nt_mesh_t){handle});
    nt_vertex_input_t vi = make_vi(info->vbo, info->ibo);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    nt_gfx_deactivate_mesh(handle);
    TEST_ASSERT_FALSE(nt_gfx_vertex_input_valid(vi));
}

/* --- Binding and mirrors --- */

void test_bind_vi_reaches_backend(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_bind_vertex_input(vi);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_stub_test_bind_vertex_input_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_stub_test_bound_vertex_input());
}

void test_bind_invalid_vi_unbinds(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_destroy_vertex_input(vi);
    nt_gfx_bind_vertex_input(vi); /* stale: clears the binding instead of trapping */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stub_test_bound_vertex_input());
}

void test_bind_pipeline_preserves_bound_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_pipeline_t pip = make_test_pipeline();
    nt_gfx_bind_vertex_input(vi);
    uint32_t bound = nt_gfx_stub_test_bound_vertex_input();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, bound);
    nt_gfx_bind_pipeline(pip);
    /* Orthogonal state: a pipeline change must not disturb the geometry bind. */
    TEST_ASSERT_EQUAL_UINT32(bound, nt_gfx_stub_test_bound_vertex_input());
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    nt_gfx_bind_instance_buffer(stream, 16); /* still points into the bound vertex input */
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_stub_test_last_instance_offset());
}

void test_destroy_while_bound_clears_mirrors(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_destroy_vertex_input(vi);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stub_test_bound_vertex_input());
    /* With the vertex-input mirror cleared and no pipeline, the instance
     * bind has nothing to point with -- it must trap, not use stale state. */
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    EXPECT_ASSERT(nt_gfx_bind_instance_buffer(stream, 0));
}

void test_begin_frame_clears_bound_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});

    nt_gfx_begin_frame();
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_instance_buffer(stream, 0); /* bound this frame: passes */
    nt_gfx_end_frame();

    nt_gfx_begin_frame();
    EXPECT_ASSERT(nt_gfx_bind_instance_buffer(stream, 0)); /* mirror cleared, no pipeline */
    nt_gfx_end_frame();
}

/* --- Instance buffer via bound vertex input --- */

void test_bind_instance_buffer_uses_bound_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_instance_buffer(stream, 16); /* no pipeline needed on this path */
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_stub_test_last_instance_offset());
}

void test_bind_instance_buffer_asserts_without_instance_layout(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    nt_gfx_bind_vertex_input(vi);
    EXPECT_ASSERT(nt_gfx_bind_instance_buffer(stream, 0));
}

/* --- Draw invariants --- */

void test_draw_indexed_asserts_on_non_indexed_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_buffer_t ibo = make_ibo();
    nt_vertex_input_t indexed = make_vi(vbo, ibo);
    nt_vertex_input_t non_indexed = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(indexed);
    nt_gfx_draw_indexed(0, 3, 3); /* index type captured from the IBO */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());
    /* A non-indexed vertex input CLEARS the index type -- indexed draw traps
     * instead of silently reusing the previous binding's type. */
    nt_gfx_bind_vertex_input(non_indexed);
    EXPECT_ASSERT(nt_gfx_draw_indexed(0, 3, 3));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

void test_instanced_draw_asserts_before_instance_pointing(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    nt_pipeline_t pip = make_test_pipeline();

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    /* Enabled-but-unpointed instance attribs would fail silently in GL --
     * for ANY draw with this VAO, not only instanced ones. */
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 2));
    EXPECT_ASSERT(nt_gfx_draw(0, 3));
    nt_gfx_bind_instance_buffer(stream, 0);
    nt_gfx_draw_instanced(0, 3, 2);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

void test_attributeless_vi_draws(void) {
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_pipeline_t pip = make_test_pipeline();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3); /* gl_VertexID path: no buffers, no attribs */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_gfx_bind_pipeline(pip_a);
    nt_gfx_bind_vertex_input(vi_indexed);
    nt_gfx_draw_indexed(0, 3, 3);

    /* Pipeline switch under the same geometry: no re-bind of the vertex input. */
    nt_gfx_bind_pipeline(pip_b);
    nt_gfx_draw_indexed(0, 3, 3);

    /* Geometry switch under the same pipeline. */
    nt_gfx_bind_vertex_input(vi_plain);
    nt_gfx_draw(0, 3);

    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* WebGL2 rejects unaligned attrib offsets; the byte offset is asserted. */
void test_bind_instance_buffer_rejects_unaligned_offset(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_instance_buffer(stream, 4);
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_stub_test_last_instance_offset()); /* aligned offset reached the backend */
    EXPECT_ASSERT(nt_gfx_bind_instance_buffer(stream, 1));
}

/* Every draw variant requires a bound vertex input. */
void test_draw_without_vertex_input_asserts(void) {
    nt_pipeline_t pip = make_test_pipeline();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    EXPECT_ASSERT(nt_gfx_draw(0, 3));
    EXPECT_ASSERT(nt_gfx_draw_indexed(0, 3, 3));
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 1));
    EXPECT_ASSERT(nt_gfx_draw_indexed_instanced(0, 3, 3, 1));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* --- Context loss --- */

void test_vi_make_during_context_loss_returns_invalid(void) {
    nt_buffer_t vbo = make_vbo();
    nt_gfx_stub_test_set_context_lost(true);
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_EQUAL_UINT32(0, vi.id);
    nt_gfx_stub_test_set_context_lost(false);
}

/* Destroying a pointed instance buffer unpoints dependents: the next
 * instanced draw without a re-point traps instead of silently reading the
 * dead buffer through the VAO's dangling attachment. */
void test_destroying_instance_buffer_unpoints_dependents(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_buffer_t stream = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
    nt_pipeline_t pip = make_test_pipeline();

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_instance_buffer(stream, 0);
    nt_gfx_draw_instanced(0, 3, 2);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());

    nt_gfx_destroy_buffer(stream);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi)); /* instance buffers do not cascade-destroy */
    EXPECT_ASSERT(nt_gfx_draw_instanced(0, 3, 2));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Pool slots survive context loss: a stale instance-buffer handle must trap,
 * not silently point the VAO at a zeroed backend. */
void test_bind_instance_buffer_asserts_on_stale_buffer(void) {
    nt_buffer_t stale = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});

    nt_gfx_stub_test_set_context_lost(true);
    nt_gfx_begin_frame(); /* latches the loss, wipes backend tables */
    nt_gfx_stub_test_set_context_lost(false);
    nt_gfx_begin_frame(); /* recovery completes */
    nt_gfx_end_frame();
    nt_gfx_begin_frame();

    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo});
    nt_gfx_bind_vertex_input(vi);
    /* The stale handle is still pool-valid; only its backend is gone. */
    EXPECT_ASSERT(nt_gfx_bind_instance_buffer(stale, 0));
    nt_gfx_end_frame();
}

void test_vi_bind_after_context_loss_asserts(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});

    nt_gfx_stub_test_set_context_lost(true);
    nt_gfx_begin_frame(); /* latches the loss, wipes backend tables */
    nt_gfx_stub_test_set_context_lost(false);
    nt_gfx_begin_frame(); /* recovery completes */

    /* The pool slot survives the loss but its GPU object is gone. */
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(vi));
    nt_gfx_end_frame();
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
    RUN_TEST(test_vi_creation_asserts_on_untyped_index_buffer);
    RUN_TEST(test_vi_creation_asserts_webgl2_rules);
    RUN_TEST(test_vi_stride_255_boundary);
    RUN_TEST(test_destroy_vbo_cascades_to_vi);
    RUN_TEST(test_destroy_ibo_cascades_to_vi);
    RUN_TEST(test_deactivate_mesh_cascades_to_vi);
    RUN_TEST(test_bind_vi_reaches_backend);
    RUN_TEST(test_bind_invalid_vi_unbinds);
    RUN_TEST(test_bind_pipeline_preserves_bound_vi);
    RUN_TEST(test_destroy_while_bound_clears_mirrors);
    RUN_TEST(test_begin_frame_clears_bound_vi);
    RUN_TEST(test_bind_instance_buffer_uses_bound_vi);
    RUN_TEST(test_bind_instance_buffer_asserts_without_instance_layout);
    RUN_TEST(test_draw_indexed_asserts_on_non_indexed_vi);
    RUN_TEST(test_instanced_draw_asserts_before_instance_pointing);
    RUN_TEST(test_attributeless_vi_draws);
    RUN_TEST(test_pipeline_and_vertex_input_bind_orthogonally);
    RUN_TEST(test_bind_instance_buffer_rejects_unaligned_offset);
    RUN_TEST(test_draw_without_vertex_input_asserts);
    RUN_TEST(test_vi_make_during_context_loss_returns_invalid);
    RUN_TEST(test_destroying_instance_buffer_unpoints_dependents);
    RUN_TEST(test_bind_instance_buffer_asserts_on_stale_buffer);
    RUN_TEST(test_vi_bind_after_context_loss_asserts);
    return UNITY_END();
}
