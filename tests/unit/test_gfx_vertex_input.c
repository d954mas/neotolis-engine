#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "nt_mesh_format.h"
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

void setUp(void) {
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS,
                                  .max_render_targets = 4));
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

static nt_vertex_layout_t inst_layout(void) { return (nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4}}}; }

static nt_vertex_input_t make_vi(nt_buffer_t vbo, nt_buffer_t ibo) { return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .vertex_buffer = vbo, .index_buffer = ibo}); }

static nt_vertex_input_t make_inst_vi(nt_buffer_t vbo) { return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos_layout(), .instance_layout = inst_layout(), .vertex_buffer = vbo}); }

/* Re-inits gfx with `count` sized vertex streams and opens a frame, as setUp does. */
static void init_with_streams(uint32_t count) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = TEST_MAX_VERTEX_INPUTS,
                                          .max_render_targets = 4);
    for (uint32_t k = 1; k < count; k++) {
        desc.frame_capacity[NT_GFX_FRAME_VERTEX + k] = 1024;
    }
    nt_gfx_init(&desc);
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
    nt_vertex_layout_t inst_full = {.attr_count = NT_GFX_MAX_INSTANCE_ATTRS, .stride = (uint16_t)(4 * NT_GFX_MAX_INSTANCE_ATTRS)};
    for (uint8_t i = 0; i < NT_GFX_MAX_INSTANCE_ATTRS; i++) {
        inst_full.attrs[i] = (nt_vertex_attr_t){.location = i, .type = NT_VERTEX_FLOAT, .count = 1, .offset = (uint16_t)(i * 4)};
    }
    nt_vertex_input_t max_inst = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.instance_layout = inst_full});
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
    begin_test_pass();
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_vertex_input_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_bound_vertex_input());
}

void test_bind_invalid_vi_clears_mirror(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t live = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_destroy_vertex_input(vi);
    begin_test_pass();
    nt_gfx_bind_vertex_input(live);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    nt_gfx_bind_vertex_input(vi); /* stale: clears the mirror instead of trapping */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bind_vertex_input_count());
}

void test_bind_pipeline_preserves_bound_vi(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_inst_vi(vbo);
    nt_pipeline_t pip = make_test_pipeline();
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX, 16);
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
    nt_vertex_input_t vi = make_inst_vi(vbo);

    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX, 0);
    nt_gfx_end_pass();

    begin_test_pass();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    /* The equal bind of the previous pass records again. */
    nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX, 0);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    nt_gfx_end_pass();
}

/* --- Instanced binds --- */

void test_bind_vertex_input_instanced_reaches_backend(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_inst_vi(vbo);
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX, 16); /* no pipeline needed on this path */
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_last_instance_offset());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_last_instance_clone());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_fake_last_bound_vertex_input(), nt_gfx_fake_last_instance_vertex_input());
}

/* The backend VAO follows the stream: vertex stream k selects clone k. */
void test_instanced_bind_selects_the_stream_clone(void) {
    init_with_streams(3);
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_inst_vi(vbo);
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX + 2, 32);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_last_instance_clone());
    TEST_ASSERT_EQUAL_UINT32(32, nt_gfx_fake_last_instance_offset());
}

/* Each bind form asserts the layout it binds, also right after the other form cached the vertex input. */
void test_bind_forms_assert_the_instance_layout(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t plain = make_vi(vbo, (nt_buffer_t){0});
    nt_vertex_input_t inst = make_inst_vi(vbo);
    begin_test_pass();
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(plain, NT_GFX_FRAME_VERTEX, 0));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(inst));
    nt_gfx_bind_vertex_input_instanced(inst, NT_GFX_FRAME_VERTEX, 0);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(inst));
    nt_gfx_end_pass();
}

/* An instanced bind points inside this frame's allocations of the stream; a plain-bound vertex
 * input without an instance layout still asserts in the instanced form. */
void test_instanced_bind_asserts_outside_allocations_and_after_a_plain_bind(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t inst = make_inst_vi(vbo);
    nt_vertex_input_t plain = make_vi(vbo, (nt_buffer_t){0});
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(inst, NT_GFX_FRAME_VERTEX, 60);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(inst, NT_GFX_FRAME_VERTEX, 64));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "outside this frame's allocations"));
    nt_gfx_bind_vertex_input(plain);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(plain, NT_GFX_FRAME_VERTEX, 0));
    TEST_ASSERT_NOT_NULL(strstr(s_last_assert_expr, "declares no instance layout"));
    nt_gfx_end_pass();
}

/* Only a sized frame vertex stream holds instances. */
void test_instanced_bind_asserts_on_a_bad_stream(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t vi = make_inst_vi(vbo);
    begin_test_pass();
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_INDEX, 0));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_UNIFORM, 0));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX + 2, 0)); /* zero frame_capacity */
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_STREAM_COUNT, 0));
    nt_gfx_end_pass();
}

/* An equal binding in the same pass ends CACHE; another offset, stream or vertex input binds. */
void test_equal_instance_binding_is_dropped(void) {
    init_with_streams(2);
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t a = make_inst_vi(vbo);
    nt_vertex_input_t b = make_inst_vi(vbo);

    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(a, NT_GFX_FRAME_VERTEX, 0);
    nt_gfx_bind_vertex_input_instanced(a, NT_GFX_FRAME_VERTEX, 0);
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    nt_gfx_bind_vertex_input_instanced(a, NT_GFX_FRAME_VERTEX, 16);
    nt_gfx_bind_vertex_input_instanced(a, NT_GFX_FRAME_VERTEX + 1, 16);
    nt_gfx_bind_vertex_input_instanced(b, NT_GFX_FRAME_VERTEX + 1, 16);
    TEST_ASSERT_EQUAL_UINT32(4, g_nt_gfx.counters.accepted[NT_GFX_OP_VERTEX_INPUT]);
    nt_gfx_end_pass();
}

/* A plain vertex input in between unbinds A's VAO, so binding A again records again. */
void test_instanced_bind_after_a_plain_vertex_input_records_again(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t a = make_inst_vi(vbo);
    nt_vertex_input_t plain = make_vi(vbo, (nt_buffer_t){0});
    nt_pipeline_t pip = make_test_pipeline();

    begin_test_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input_instanced(a, NT_GFX_FRAME_VERTEX, 0);
    nt_gfx_bind_vertex_input(plain);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_vertex_input_instanced(a, NT_GFX_FRAME_VERTEX, 0);
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
    nt_vertex_input_t vi = make_inst_vi(vbo);
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX, 4);
    EXPECT_ASSERT(nt_gfx_bind_vertex_input_instanced(vi, NT_GFX_FRAME_VERTEX, 1));
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

/* A stale vertex input clears the mirror in the instanced bind too, instead of trapping. */
void test_instanced_bind_of_a_stale_vi_clears_mirror(void) {
    nt_buffer_t vbo = make_vbo();
    nt_vertex_input_t stale = make_inst_vi(vbo);
    nt_vertex_input_t live = make_inst_vi(vbo);
    nt_gfx_destroy_vertex_input(stale);
    begin_test_pass();
    nt_gfx_bind_vertex_input_instanced(live, NT_GFX_FRAME_VERTEX, 0);
    nt_gfx_bind_vertex_input_instanced(stale, NT_GFX_FRAME_VERTEX, 0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bind_vertex_input_count());
}
/* Loss frees vertex-input slots outright: the handle goes stale, the bind is
 * the ordinary invalid-handle path, and every slot is allocatable again. */
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
    nt_gfx_bind_vertex_input(vi); /* stale: ordinary invalid path, no trap */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
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
    RUN_TEST(test_vi_creation_asserts_on_untyped_index_buffer);
    RUN_TEST(test_vi_creation_asserts_webgl2_rules);
    RUN_TEST(test_vi_stride_255_boundary);
    RUN_TEST(test_destroy_vbo_cascades_to_vi);
    RUN_TEST(test_destroy_ibo_cascades_to_vi);
    RUN_TEST(test_deactivate_mesh_cascades_to_vi);
    RUN_TEST(test_bind_vi_reaches_backend);
    RUN_TEST(test_bind_invalid_vi_clears_mirror);
    RUN_TEST(test_bind_pipeline_preserves_bound_vi);
    RUN_TEST(test_begin_pass_clears_bound_vi);
    RUN_TEST(test_bind_vertex_input_instanced_reaches_backend);
    RUN_TEST(test_instanced_bind_selects_the_stream_clone);
    RUN_TEST(test_bind_forms_assert_the_instance_layout);
    RUN_TEST(test_instanced_bind_asserts_on_a_bad_stream);
    RUN_TEST(test_instanced_bind_asserts_outside_allocations_and_after_a_plain_bind);
    RUN_TEST(test_draw_indexed_asserts_on_non_indexed_vi);
    RUN_TEST(test_equal_instance_binding_is_dropped);
    RUN_TEST(test_instanced_bind_after_a_plain_vertex_input_records_again);
    RUN_TEST(test_attributeless_vi_draws);
    RUN_TEST(test_pipeline_and_vertex_input_bind_orthogonally);
    RUN_TEST(test_instanced_bind_rejects_unaligned_offset);
    RUN_TEST(test_draw_without_vertex_input_asserts);
    RUN_TEST(test_vi_make_during_context_loss_returns_invalid);
    RUN_TEST(test_instanced_bind_of_a_stale_vi_clears_mirror);
    RUN_TEST(test_vi_slots_freed_by_context_loss);
    return UNITY_END();
}
