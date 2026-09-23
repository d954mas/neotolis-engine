/* Real-GL proof for the skinned renderer and the shipped skin.glsl contract. */

#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "renderers/nt_mesh_renderer.h"
#include "renderers/nt_skinned_mesh_renderer.h"
#include "resource/nt_resource.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"
#include "window/nt_window.h"

#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "test_helpers/nt_gfx_test_tick.h"
#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

enum { RT_W = 96, RT_H = 96, VERTEX_COUNT = 4, FRAME_BYTES = RT_W * RT_H * 4 };

typedef struct {
    float position[3];
    float normal[3];
    float tangent[4];
    uint8_t joints[4];
    uint8_t weights[4];
} test_vertex_t;

_Static_assert(sizeof(test_vertex_t) == 48, "native skin fixture vertex must stay tightly packed");

static const uint16_t k_indices[6] = {0, 1, 2, 0, 2, 3};

static const test_vertex_t k_bar[VERTEX_COUNT] = {
    {{-0.72F, -0.16F, 0.0F}, {0.36F, 0.48F, 0.8F}, {0.8F, -0.6F, 0.0F, 1.0F}, {0, 1, 0, 0}, {255, 0, 0, 0}},
    {{0.72F, -0.16F, 0.0F}, {0.36F, 0.48F, 0.8F}, {0.8F, -0.6F, 0.0F, 1.0F}, {0, 1, 0, 0}, {64, 191, 0, 0}},
    {{0.72F, 0.16F, 0.0F}, {0.36F, 0.48F, 0.8F}, {0.8F, -0.6F, 0.0F, 1.0F}, {0, 1, 0, 0}, {64, 191, 0, 0}},
    {{-0.72F, 0.16F, 0.0F}, {0.36F, 0.48F, 0.8F}, {0.8F, -0.6F, 0.0F, 1.0F}, {0, 1, 0, 0}, {255, 0, 0, 0}},
};

static const test_vertex_t k_guard_bar[VERTEX_COUNT] = {
    {{-0.72F, -0.16F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0, 0, 0, 0}, {255, 0, 0, 0}},
    {{0.72F, -0.16F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0, 0, 0, 0}, {255, 0, 0, 0}},
    {{0.72F, 0.16F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0, 0, 0, 0}, {255, 0, 0, 0}},
    {{-0.72F, 0.16F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0, 0, 0, 0}, {255, 0, 0, 0}},
};

/* Two frames, two joints, three RGBA rows per joint. */
static const float k_palette_texels[96] = {
    /* row 0: frame A at x=0 */
    1.0F,
    0.0F,
    0.0F,
    -0.10F,
    0.0F,
    1.0F,
    0.0F,
    -0.04F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
    /* frame A, joint 1 */
    0.93969262F,
    -0.34202015F,
    0.0F,
    0.10F,
    0.34202015F,
    0.93969262F,
    0.0F,
    0.12F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
    /* row 0 padding */
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    /* row 1: padding, then frame B at x=3 */
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    /* frame B, joint 0 */
    0.96592581F,
    0.25881904F,
    0.0F,
    0.06F,
    -0.25881904F,
    0.96592581F,
    0.0F,
    0.04F,
    0.0F,
    0.0F,
    1.0F,
    0.0F,
    /* frame B, joint 1 */
    0.77942288F,
    -0.45F,
    0.0F,
    -0.08F,
    0.45F,
    0.77942288F,
    0.0F,
    -0.10F,
    0.0F,
    0.0F,
    0.9F,
    0.0F,
    /* row 1 padding */
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
    0.0F,
};

static nt_render_target_t s_target;
static nt_texture_t s_palette;
static nt_shader_t s_skin_vs;
static nt_shader_t s_reference_vs;
static nt_shader_t s_fragment_shader;
static nt_program_t s_skin_program;
static nt_program_t s_reference_program;
static uint8_t s_actual[FRAME_BYTES];
static uint8_t s_expected[FRAME_BYTES];
static bool s_initialized;

static bool read_text(const char *path, char **out_text) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    const long end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return false;
    }
    const size_t size = (size_t)end;
    char *text = (char *)malloc(size + 1U);
    if (text == NULL) {
        (void)fclose(file);
        return false;
    }
    const bool ok = fread(text, 1, size, file) == size;
    (void)fclose(file);
    if (!ok) {
        free(text);
        return false;
    }
    text[size] = '\0';
    *out_text = text;
    return true;
}

static bool compose_skin_vertex_source(char **out_source) {
    static const char marker[] = "#include \"../../assets/shaders/common/skin.glsl\"";
    static const char pragma[] = "#pragma once";
    char *fixture = NULL;
    char *skin = NULL;
    if (!read_text("tests/fixtures/skinned_mesh_renderer_native.vert", &fixture) || !read_text("assets/shaders/common/skin.glsl", &skin)) {
        free(fixture);
        free(skin);
        return false;
    }
    const char *insert = strstr(fixture, marker);
    const char *line_end = strchr(skin, '\n');
    const bool valid = insert != NULL && strstr(insert + sizeof(marker) - 1U, marker) == NULL && strncmp(skin, pragma, sizeof(pragma) - 1U) == 0 && line_end != NULL;
    if (!valid) {
        free(fixture);
        free(skin);
        return false;
    }
    const char *body = line_end + 1;
    const size_t prefix_size = (size_t)(insert - fixture);
    const size_t suffix_size = strlen(insert + sizeof(marker) - 1U);
    const size_t body_size = strlen(body);
    char *combined = (char *)malloc(prefix_size + body_size + suffix_size + 1U);
    if (combined == NULL) {
        free(fixture);
        free(skin);
        return false;
    }
    memcpy(combined, fixture, prefix_size);
    /* The suffix copy below writes the only terminator after the inserted body. */
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
    memcpy(combined + prefix_size, body, body_size);
    memcpy(combined + prefix_size + body_size, insert + sizeof(marker) - 1U, suffix_size + 1U);
    free(fixture);
    free(skin);
    *out_source = combined;
    return true;
}

static nt_mesh_t make_mesh(const test_vertex_t vertices[VERTEX_COUNT]) {
    enum { STREAM_COUNT = 5, VERTEX_BYTES = VERTEX_COUNT * (int)sizeof(test_vertex_t), INDEX_BYTES = (int)sizeof(k_indices) };
    uint8_t blob[sizeof(NtMeshAssetHeader) + (STREAM_COUNT * sizeof(NtStreamDesc)) + VERTEX_BYTES + INDEX_BYTES];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *header = (NtMeshAssetHeader *)blob;
    header->magic = NT_MESH_MAGIC;
    header->version = NT_MESH_VERSION;
    header->stream_count = STREAM_COUNT;
    header->index_type = 1;
    header->vertex_count = VERTEX_COUNT;
    header->index_count = 6;
    header->vertex_data_size = VERTEX_BYTES;
    header->index_data_size = INDEX_BYTES;

    NtStreamDesc *streams = (NtStreamDesc *)(blob + sizeof(*header));
    streams[0] = (NtStreamDesc){.name_hash = nt_hash32_str("position").value, .type = NT_STREAM_FLOAT32, .count = 3};
    streams[1] = (NtStreamDesc){.name_hash = nt_hash32_str("normal").value, .type = NT_STREAM_FLOAT32, .count = 3};
    streams[2] = (NtStreamDesc){.name_hash = nt_hash32_str("tangent").value, .type = NT_STREAM_FLOAT32, .count = 4};
    streams[3] = (NtStreamDesc){.name_hash = nt_hash32_str("joints").value, .type = NT_STREAM_UINT8, .count = 4};
    streams[4] = (NtStreamDesc){.name_hash = nt_hash32_str("weights").value, .type = NT_STREAM_UINT8, .count = 4, .normalized = 1};
    uint8_t *vertex_bytes = blob + sizeof(*header) + (STREAM_COUNT * sizeof(*streams));
    memcpy(vertex_bytes, vertices, VERTEX_BYTES);
    memcpy(vertex_bytes + VERTEX_BYTES, k_indices, INDEX_BYTES);
    return (nt_mesh_t){.id = nt_gfx_activate_mesh(blob, sizeof(blob))};
}

static nt_material_t make_skinned_material(float probe_mode, nt_color_mode_t color_mode) {
    return nt_material_create(&(nt_material_create_desc_t){
        .program = s_skin_program,
        .textures = {{.name = "u_skin_matrices"}},
        .texture_count = 1,
        .params = {{.name = "u_probe_mode", .value = {probe_mode, 0.0F, 0.0F, 0.0F}}},
        .param_count = 1,
        .attr_map =
            {
                {.stream_name = "position", .location = 0},
                {.stream_name = "normal", .location = 1},
                {.stream_name = "tangent", .location = 2},
                {.stream_name = "joints", .location = 8},
                {.stream_name = "weights", .location = 9},
            },
        .attr_map_count = 5,
        .cull_mode = NT_CULL_NONE,
        .color_mode = color_mode,
        .label = "native_skinned_probe",
    });
}

static nt_material_t make_reference_material(float probe_mode) {
    return nt_material_create(&(nt_material_create_desc_t){
        .program = s_reference_program,
        .params = {{.name = "u_probe_mode", .value = {probe_mode, 0.0F, 0.0F, 0.0F}}},
        .param_count = 1,
        .attr_map =
            {
                {.stream_name = "position", .location = 0},
                {.stream_name = "normal", .location = 1},
                {.stream_name = "tangent", .location = 2},
            },
        .attr_map_count = 3,
        .cull_mode = NT_CULL_NONE,
        .color_mode = NT_COLOR_MODE_NONE,
        .label = "native_skin_cpu_reference",
    });
}

static nt_entity_t make_entity(nt_mesh_t mesh, nt_material_t material, const nt_deformation_binding_t *binding) {
    nt_entity_t entity = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(entity));
    TEST_ASSERT_TRUE(nt_mesh_comp_add(entity));
    TEST_ASSERT_TRUE(nt_material_comp_add(entity));
    TEST_ASSERT_TRUE(nt_drawable_comp_add(entity));
    *nt_mesh_comp_handle(entity) = mesh;
    *nt_material_comp_handle(entity) = material;
    nt_drawable_comp_set_color(entity, 1.0F, 1.0F, 1.0F, 1.0F);
    if (binding != NULL) {
        TEST_ASSERT_TRUE(nt_skin_comp_add(entity));
        *nt_skin_comp_handle(entity) = *binding;
    }
    nt_transform_comp_update();
    return entity;
}

static void render_entity(nt_entity_t entity, nt_material_t material, nt_mesh_t mesh, bool skinned, uint8_t out[FRAME_BYTES]) {
    const nt_render_item_t item = {.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(material, mesh)};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0.0F, 0.0F, 0.0F, 0.0F}, .clear_depth = 1.0F});
    if (skinned) {
        nt_skinned_mesh_renderer_draw_list(&item, 1);
    } else {
        nt_mesh_renderer_draw_list(&item, 1);
    }
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, RT_W, RT_H, out, FRAME_BYTES));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void render_skinned_list(const nt_render_item_t *items, uint32_t count, uint8_t out[FRAME_BYTES]) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0.0F, 0.0F, 0.0F, 0.0F}, .clear_depth = 1.0F});
    nt_skinned_mesh_renderer_draw_list(items, count);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, RT_W, RT_H, out, FRAME_BYTES));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static const float *palette_row(uint16_t origin_x, uint16_t origin_y, uint8_t joint, uint8_t row) {
    const size_t texel = ((size_t)origin_y * 12U) + origin_x + ((size_t)joint * 3U) + row;
    return &k_palette_texels[texel * 4U];
}

static float dot3(const float a[3], const float b[3]) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }

static void normalize3(float v[3]) {
    const float inverse_length = 1.0F / sqrtf(dot3(v, v));
    for (uint8_t i = 0; i < 3; i++) {
        v[i] *= inverse_length;
    }
}

static void world_vector(const float in[3], float out[3]) {
    out[0] = -0.75F * in[1];
    out[1] = 0.75F * in[0];
    out[2] = 0.75F * in[2];
}

static void deform_vertices(nt_deformation_binding_t binding, bool apply_world, test_vertex_t out[VERTEX_COUNT]) {
    memcpy(out, k_bar, sizeof(k_bar));
    for (uint32_t vertex = 0; vertex < VERTEX_COUNT; vertex++) {
        float matrix[3][4] = {{0}};
        for (uint8_t lane = 0; lane < 4; lane++) {
            const float weight = (float)k_bar[vertex].weights[lane] / 255.0F;
            const uint8_t joint = k_bar[vertex].joints[lane];
            for (uint8_t row = 0; row < 3; row++) {
                const float *a = palette_row(binding.x0, binding.y0, joint, row);
                const float *b = palette_row(binding.x1, binding.y1, joint, row);
                for (uint8_t column = 0; column < 4; column++) {
                    matrix[row][column] += weight * ((a[column] * (1.0F - binding.alpha)) + (b[column] * binding.alpha));
                }
            }
        }
        const float *position = k_bar[vertex].position;
        float skinned_position[3];
        for (uint8_t row = 0; row < 3; row++) {
            skinned_position[row] = (matrix[row][0] * position[0]) + (matrix[row][1] * position[1]) + (matrix[row][2] * position[2]) + matrix[row][3];
        }
        if (apply_world) {
            world_vector(skinned_position, out[vertex].position);
            out[vertex].position[0] += 0.08F;
            out[vertex].position[1] -= 0.06F;
        } else {
            memcpy(out[vertex].position, skinned_position, sizeof(skinned_position));
        }

        float skin_normal[3];
        float skin_tangent[3];
        for (uint8_t row = 0; row < 3; row++) {
            skin_normal[row] = (matrix[row][0] * k_bar[vertex].normal[0]) + (matrix[row][1] * k_bar[vertex].normal[1]) + (matrix[row][2] * k_bar[vertex].normal[2]);
            skin_tangent[row] = (matrix[row][0] * k_bar[vertex].tangent[0]) + (matrix[row][1] * k_bar[vertex].tangent[1]) + (matrix[row][2] * k_bar[vertex].tangent[2]);
        }
        if (apply_world) {
            world_vector(skin_normal, out[vertex].normal);
            world_vector(skin_tangent, out[vertex].tangent);
        } else {
            memcpy(out[vertex].normal, skin_normal, sizeof(skin_normal));
            memcpy(out[vertex].tangent, skin_tangent, sizeof(skin_tangent));
        }
        normalize3(out[vertex].normal);
        const float projection = dot3(out[vertex].normal, out[vertex].tangent);
        for (uint8_t axis = 0; axis < 3; axis++) {
            out[vertex].tangent[axis] -= out[vertex].normal[axis] * projection;
        }
        normalize3(out[vertex].tangent);
    }
}

static uint8_t channel_delta(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }

static void assert_cpu_gpu_frames_agree(void) {
    uint32_t covered = 0;
    uint32_t mismatched = 0;
    for (uint32_t pixel = 0; pixel < RT_W * RT_H; pixel++) {
        const size_t byte_offset = (size_t)pixel * 4U;
        const uint8_t *actual = &s_actual[byte_offset];
        const uint8_t *expected = &s_expected[byte_offset];
        if (actual[3] == 0 && expected[3] == 0) {
            continue;
        }
        covered++;
        if (channel_delta(actual[0], expected[0]) > 2 || channel_delta(actual[1], expected[1]) > 2 || channel_delta(actual[2], expected[2]) > 2 || channel_delta(actual[3], expected[3]) > 2) {
            mismatched++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, covered, "empty render cannot prove skinning");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(covered / 200U, mismatched, "GPU skinning differs from the independent CPU reference");
}

static void assert_probe_matches_mask(const uint8_t frame[FRAME_BYTES], uint8_t r, uint8_t g, uint8_t b) {
    uint32_t covered = 0;
    for (uint32_t pixel = 0; pixel < RT_W * RT_H; pixel++) {
        const size_t byte_offset = (size_t)pixel * 4U;
        const uint8_t *mask = &s_expected[byte_offset];
        const uint8_t *actual = &frame[byte_offset];
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(mask[3] == 0 ? 0 : 255, actual[3], "normal/tangent guard changed bar coverage");
        if (mask[3] == 0) {
            continue;
        }
        covered++;
        TEST_ASSERT_UINT8_WITHIN(2, r, actual[0]);
        TEST_ASSERT_UINT8_WITHIN(2, g, actual[1]);
        TEST_ASSERT_UINT8_WITHIN(2, b, actual[2]);
    }
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, covered, "guard probe rendered no covered pixels");
}

void setUp(void) {
    char *skin_source = NULL;
    char *reference_source = NULL;
    char *fragment_source = NULL;
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = RT_W, .height = RT_H};
    nt_window_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_test_init(&(nt_gfx_desc_t){
        .max_shaders = 8,
        .max_programs = 4,
        .max_pipelines = 8,
        .max_buffers = 32,
        .max_textures = 8,
        .max_meshes = 16,
        .max_vertex_inputs = 64,
        .max_render_targets = 4,
    });
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 32});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 32});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 32});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 32});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 32});
    nt_skin_comp_init(&(nt_skin_comp_desc_t){.capacity = 32});
    nt_material_init(&(nt_material_desc_t){.max_materials = 16});
    s_initialized = true;
    TEST_ASSERT_EQUAL(NT_OK, nt_mesh_renderer_init(&(nt_mesh_renderer_desc_t){.max_instances = 8, .max_pipelines = 4, .max_mesh_layouts = 4}));
    TEST_ASSERT_EQUAL(NT_OK, nt_skinned_mesh_renderer_init(&(nt_skinned_mesh_renderer_desc_t){.max_instances = 8, .max_pipelines = 4, .max_mesh_layouts = 4}));

    const bool sources_ready = compose_skin_vertex_source(&skin_source) && read_text("tests/fixtures/skinned_mesh_renderer_reference_native.vert", &reference_source) &&
                               read_text("tests/fixtures/skinned_mesh_renderer_native.frag", &fragment_source);
    if (!sources_ready) {
        free(skin_source);
        free(reference_source);
        free(fragment_source);
    }
    TEST_ASSERT_TRUE_MESSAGE(sources_ready, "failed to load native skinned-renderer shader fixtures");

    s_skin_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = skin_source, .label = "native_skin_vs"});
    s_reference_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = reference_source, .label = "native_skin_reference_vs"});
    s_fragment_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source, .label = "native_skin_fs"});
    s_skin_program = nt_gfx_make_program(s_skin_vs, s_fragment_shader);
    s_reference_program = nt_gfx_make_program(s_reference_vs, s_fragment_shader);
    free(skin_source);
    free(reference_source);
    free(fragment_source);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_skin_program.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_reference_program.id);

    s_target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = RT_W,
        .height = RT_H,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
        .label = "native_skin_target",
    });
    s_palette = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 12,
        .height = 2,
        .data = k_palette_texels,
        .format = NT_TEXTURE_FORMAT_RGBA32F,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "native_skin_palette",
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_target.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_palette.id);
}

void tearDown(void) {
    if (!s_initialized) {
        return;
    }
    nt_skinned_mesh_renderer_shutdown();
    nt_mesh_renderer_shutdown();
    nt_material_shutdown();
    nt_skin_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_resource_shutdown();
    if (s_palette.id != 0) {
        nt_gfx_destroy_texture(s_palette);
    }
    if (s_target.id != 0) {
        nt_gfx_destroy_render_target(s_target);
    }
    nt_gfx_destroy_program(s_reference_program);
    nt_gfx_destroy_program(s_skin_program);
    nt_gfx_destroy_shader(s_fragment_shader);
    nt_gfx_destroy_shader(s_reference_vs);
    nt_gfx_destroy_shader(s_skin_vs);
    s_palette = (nt_texture_t){0};
    s_target = (nt_render_target_t){0};
    s_reference_program = (nt_program_t){0};
    s_skin_program = (nt_program_t){0};
    s_fragment_shader = (nt_shader_t){0};
    s_reference_vs = (nt_shader_t){0};
    s_skin_vs = (nt_shader_t){0};
    nt_gfx_shutdown();
    nt_hash_shutdown();
    nt_window_shutdown();
    s_initialized = false;
}

static void test_palette_frames_and_interpolation_match_cpu_reference(void) {
    const nt_deformation_binding_t cases[3] = {
        {.texture = {0}, .x0 = 0, .y0 = 0, .x1 = 0, .y1 = 0, .alpha = 0.0F},
        {.texture = {0}, .x0 = 3, .y0 = 1, .x1 = 3, .y1 = 1, .alpha = 0.0F},
        {.texture = {0}, .x0 = 0, .y0 = 0, .x1 = 3, .y1 = 1, .alpha = 0.25F},
    };
    nt_mesh_t skinned_mesh = make_mesh(k_bar);
    nt_material_t skinned_material[3];
    nt_material_t reference_material[3];
    for (uint8_t mode = 0; mode < 3; mode++) {
        skinned_material[mode] = make_skinned_material((float)mode, NT_COLOR_MODE_NONE);
        reference_material[mode] = make_reference_material((float)mode);
    }
    nt_deformation_binding_t initial = cases[0];
    initial.texture = s_palette;
    nt_entity_t skinned_entity = make_entity(skinned_mesh, skinned_material[0], &initial);
    const float rotation[4] = {0.0F, 0.0F, 0.70710678F, 0.70710678F};
    nt_transform_comp_set_position(skinned_entity, 0.08F, -0.06F, 0.0F);
    nt_transform_comp_set_rotation(skinned_entity, rotation);
    nt_transform_comp_set_scale(skinned_entity, 0.75F, 0.75F, 0.75F);
    nt_transform_comp_update();

    for (uint8_t i = 0; i < 3; i++) {
        nt_deformation_binding_t binding = cases[i];
        binding.texture = s_palette;
        *nt_skin_comp_handle(skinned_entity) = binding;
        test_vertex_t reference_vertices[VERTEX_COUNT];
        deform_vertices(binding, true, reference_vertices);
        nt_mesh_t reference_mesh = make_mesh(reference_vertices);
        nt_entity_t reference_entity = make_entity(reference_mesh, reference_material[0], NULL);
        for (uint8_t mode = 0; mode < 3; mode++) {
            *nt_material_comp_handle(skinned_entity) = skinned_material[mode];
            *nt_material_comp_handle(reference_entity) = reference_material[mode];
            render_entity(skinned_entity, skinned_material[mode], skinned_mesh, true, s_actual);
            render_entity(reference_entity, reference_material[mode], reference_mesh, false, s_expected);
            assert_cpu_gpu_frames_agree();
        }
    }
}

static void test_degenerate_normal_and_tangent_guards_are_finite_and_deterministic(void) {
    const nt_deformation_binding_t binding = {.texture = s_palette, .x0 = 0, .y0 = 0, .x1 = 0, .y1 = 0, .alpha = 0.0F};
    nt_mesh_t mesh = make_mesh(k_guard_bar);
    nt_material_t position_material = make_skinned_material(0.0F, NT_COLOR_MODE_NONE);
    nt_material_t normal_material = make_skinned_material(1.0F, NT_COLOR_MODE_NONE);
    nt_material_t tangent_material = make_skinned_material(2.0F, NT_COLOR_MODE_NONE);
    nt_entity_t entity = make_entity(mesh, position_material, &binding);

    render_entity(entity, position_material, mesh, true, s_expected);
    *nt_material_comp_handle(entity) = normal_material;
    render_entity(entity, normal_material, mesh, true, s_actual);
    assert_probe_matches_mask(s_actual, 128, 191, 128); /* fixed Y-up normal */

    *nt_material_comp_handle(entity) = tangent_material;
    render_entity(entity, tangent_material, mesh, true, s_actual);
    assert_probe_matches_mask(s_actual, 128, 128, 191); /* deterministic +Z tangent */
}

static void test_colored_then_none_restores_white_for_both_color_layouts(void) {
    const nt_deformation_binding_t binding = {.texture = s_palette, .x0 = 0, .y0 = 0, .x1 = 3, .y1 = 1, .alpha = 0.25F};
    test_vertex_t reference_vertices[VERTEX_COUNT];
    deform_vertices(binding, false, reference_vertices);
    nt_mesh_t reference_mesh = make_mesh(reference_vertices);
    nt_material_t reference_material = make_reference_material(3.0F);
    nt_entity_t reference_entity = make_entity(reference_mesh, reference_material, NULL);
    render_entity(reference_entity, reference_material, reference_mesh, false, s_expected);

    const nt_color_mode_t modes[2] = {NT_COLOR_MODE_RGBA8, NT_COLOR_MODE_FLOAT4};
    for (uint8_t i = 0; i < 2; i++) {
        nt_mesh_t mesh = make_mesh(k_bar);
        nt_material_t colored = make_skinned_material(3.0F, modes[i]);
        nt_material_t none = make_skinned_material(3.0F, NT_COLOR_MODE_NONE);
        nt_entity_t colored_entity = make_entity(mesh, colored, &binding);
        nt_entity_t none_entity = make_entity(mesh, none, &binding);
        nt_drawable_comp_set_color(colored_entity, 0.1F, 0.2F, 0.3F, 1.0F);
        render_entity(colored_entity, colored, mesh, true, s_actual);
        assert_probe_matches_mask(s_actual, 26, 51, 77);
        const nt_render_item_t items[2] = {
            {.entity = colored_entity.id, .batch_key = nt_mesh_renderer_batch_key(colored, mesh)},
            {.entity = none_entity.id, .batch_key = nt_mesh_renderer_batch_key(none, mesh)},
        };

        render_skinned_list(items, 2, s_actual);
        assert_cpu_gpu_frames_agree();
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_palette_frames_and_interpolation_match_cpu_reference);
    RUN_TEST(test_degenerate_normal_and_tangent_guards_are_finite_and_deterministic);
    RUN_TEST(test_colored_then_none_restores_white_for_both_color_layouts);
    return UNITY_END();
}
