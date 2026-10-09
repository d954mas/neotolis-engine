#include "color/nt_color.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "nt_mesh_format.h"
#include "renderers/nt_mesh_renderer.h"
#include "renderers/nt_skinned_mesh_renderer.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define NT_TEST_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define NT_TEST_KEEPALIVE
#endif

/* WebGL2 proof of the mesh renderers' instance layouts: per-instance RGBA8 color at the
 * struct offsets, stride stepping inside one run, and a shader that does not read color. */

enum { RT_W = 64, RT_H = 64 };

uint32_t nt_test_mesh_color_probe(void);

#define MESH_PROBE_NOT_READY 0x80000000U /* programs still linking: the spec polls again */

#define MESH_PROBE_VS_HEAD                                                                                                                                                                             \
    "precision highp float;\n"                                                                                                                                                                         \
    "layout(location = 0) in vec3 a_position;\n"
#define MESH_PROBE_WORLD(r0, r1, r2)                                                                                                                                                                   \
    "layout(location = " #r0 ") in vec4 a_world_row0;\n"                                                                                                                                               \
    "layout(location = " #r1 ") in vec4 a_world_row1;\n"                                                                                                                                               \
    "layout(location = " #r2 ") in vec4 a_world_row2;\n"                                                                                                                                               \
    "out mediump vec4 v_color;\n"                                                                                                                                                                      \
    "vec4 world_position() { vec4 p = vec4(a_position, 1.0); return vec4(dot(a_world_row0, p), dot(a_world_row1, p), dot(a_world_row2, p), 1.0); }\n"

static const char *s_mesh_vs_src = MESH_PROBE_VS_HEAD MESH_PROBE_WORLD(4, 5, 6) "layout(location = 7) in vec4 a_color;\n"
                                                                                "void main() { gl_Position = world_position(); v_color = a_color; }\n";
static const char *s_colorless_vs_src = MESH_PROBE_VS_HEAD MESH_PROBE_WORLD(4, 5, 6) "void main() { gl_Position = world_position(); v_color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
static const char *s_skinned_vs_src = MESH_PROBE_VS_HEAD MESH_PROBE_WORLD(10, 11, 12) "layout(location = 13) in vec4 a_color;\n"
                                                                                      "void main() { gl_Position = world_position(); v_color = a_color; }\n";
static const char *s_fs_src = "precision mediump float;\n"
                              "in vec4 v_color;\n"
                              "out vec4 frag_color;\n"
                              "void main() { frag_color = v_color; }\n";

enum { PROBE_MESH, PROBE_COLORLESS, PROBE_SKINNED, PROBE_PROGRAM_COUNT };
static const char *const *const s_vs_src[PROBE_PROGRAM_COUNT] = {&s_mesh_vs_src, &s_colorless_vs_src, &s_skinned_vs_src};

/* Linking is asynchronous: programs persist across calls and the spec polls until they are ready. */
static nt_shader_t s_vs[PROBE_PROGRAM_COUNT];
static nt_shader_t s_fs;
static nt_program_t s_program[PROBE_PROGRAM_COUNT];

static bool programs_ready(void) {
    if (s_fs.id == 0) {
        s_fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src, .label = "mesh_color_probe_fs"});
        for (uint32_t i = 0; i < PROBE_PROGRAM_COUNT; i++) {
            s_vs[i] = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = *s_vs_src[i], .label = "mesh_color_probe_vs"});
            s_program[i] = nt_gfx_make_program(s_vs[i], s_fs);
        }
    }
    bool ready = true;
    for (uint32_t i = 0; i < PROBE_PROGRAM_COUNT; i++) {
        ready = ready && nt_gfx_program_ready(s_program[i]);
    }
    return ready;
}

/* A 0.4 x 0.4 quad around the origin, one float3 position stream. */
static nt_mesh_t make_quad(void) {
    static const float verts[12] = {-0.2F, -0.2F, 0.0F, 0.2F, -0.2F, 0.0F, 0.2F, 0.2F, 0.0F, -0.2F, 0.2F, 0.0F};
    static const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + sizeof(verts) + sizeof(indices)];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = NT_INDEX_UINT16;
    hdr->vertex_count = 4;
    hdr->index_count = 6;
    hdr->vertex_data_size = sizeof(verts);
    hdr->index_data_size = sizeof(indices);
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = nt_hash32_str("position").value;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
    memcpy(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc), verts, sizeof(verts));
    memcpy(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + sizeof(verts), indices, sizeof(indices));
    return (nt_mesh_t){.id = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob))};
}

static nt_material_t make_material(nt_program_t program, bool skinned) {
    nt_material_create_desc_t desc = {
        .program = program,
        .attr_map = {{.stream_name = "position", .location = 0}},
        .attr_map_count = 1,
        .cull_mode = NT_CULL_NONE,
        .label = "mesh_color_probe",
    };
    if (skinned) {
        /* The run supplies the deformation texture; this program samples nothing. */
        desc.textures[0] = (nt_material_texture_desc_t){.name = "u_skin_matrices"};
        desc.texture_count = 1;
    }
    return nt_material_create(&desc);
}

static nt_render_item_t make_item(nt_mesh_t mesh, nt_material_t material, float x, float y, uint32_t color, const nt_deformation_binding_t *binding) {
    nt_entity_t entity = nt_entity_create();
    nt_transform_comp_add(entity);
    nt_mesh_comp_add(entity);
    nt_material_comp_add(entity);
    nt_drawable_comp_add(entity);
    *nt_mesh_comp_handle(entity) = mesh;
    *nt_material_comp_handle(entity) = material;
    nt_drawable_comp_set_color(entity, color);
    nt_transform_comp_set_position(entity, x, y, 0.0F);
    if (binding != NULL) {
        nt_skin_comp_add(entity);
        *nt_skin_comp_handle(entity) = *binding;
    }
    return (nt_render_item_t){.entity = entity.id, .batch_key = nt_mesh_renderer_batch_key(material, mesh)};
}

/* nt_gfx_read_pixels rows start at the top: ndc (x, y) maps to pixel ((x + 1) * 32, (1 - y) * 32). */
static bool pixel_is(const uint8_t *frame, float x, float y, uint32_t rgba) {
    const size_t row = (size_t)((1.0F - y) * (0.5F * (float)RT_H));
    const size_t column = (size_t)((x + 1.0F) * (0.5F * (float)RT_W));
    const uint8_t *pixel = &frame[((row * RT_W) + column) * 4U];
    for (uint32_t c = 0; c < 4; c++) {
        const int delta = (int)pixel[c] - (int)((rgba >> (8U * c)) & 0xFFU);
        if (delta < -1 || delta > 1) {
            return false;
        }
    }
    return true;
}

static void start_modules(void) {
    (void)nt_entity_init(&(nt_entity_desc_t){.max_entities = 16});
    (void)nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 16});
    (void)nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 16});
    (void)nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 16});
    (void)nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 16});
    (void)nt_skin_comp_init(&(nt_skin_comp_desc_t){.capacity = 16});
    (void)nt_mesh_renderer_init(&(nt_mesh_renderer_desc_t){.max_pipelines = 4, .max_mesh_vertex_inputs = 2});
    (void)nt_skinned_mesh_renderer_init(&(nt_skinned_mesh_renderer_desc_t){.max_pipelines = 4, .max_mesh_vertex_inputs = 2});
}

static void stop_modules(void) {
    nt_skinned_mesh_renderer_shutdown();
    nt_mesh_renderer_shutdown();
    nt_skin_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
}

/* Bit 0: tinted mesh instance; 1: white mesh instance of the same run; 2: mesh shader without
 * a_color; 3: tinted skinned instance (another tint, so the rows cannot stand in for each other);
 * 4: white skinned instance of the same run; 5: each list drew as one instanced draw. */
static uint32_t draw_and_check(nt_render_target_t target, nt_texture_t deformation) {
    const nt_deformation_binding_t binding = {.texture = deformation};
    const uint32_t tint = nt_color_pack((const float[4]){0.1F, 0.2F, 0.3F, 0.5F}); /* alpha 128: blending is off, so it reaches the target */
    const uint32_t skinned_tint = nt_color_pack((const float[4]){0.3F, 0.2F, 0.1F, 0.5F});
    nt_mesh_t quad = make_quad();
    nt_material_t mesh_material = make_material(s_program[PROBE_MESH], false);
    nt_material_t colorless_material = make_material(s_program[PROBE_COLORLESS], false);
    nt_material_t skinned_material = make_material(s_program[PROBE_SKINNED], true);
    const nt_render_item_t mesh_items[2] = {make_item(quad, mesh_material, -0.5F, 0.5F, tint, NULL), make_item(quad, mesh_material, 0.5F, 0.5F, 0xFFFFFFFFU, NULL)};
    const nt_render_item_t colorless_item = make_item(quad, colorless_material, 0.0F, 0.0F, tint, NULL);
    const nt_render_item_t skinned_items[2] = {make_item(quad, skinned_material, -0.5F, -0.5F, skinned_tint, &binding), make_item(quad, skinned_material, 0.5F, -0.5F, 0xFFFFFFFFU, &binding)};
    nt_transform_comp_update();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    const uint32_t draws = nt_gfx_draw_calls(&g_nt_gfx.counters);
    const uint64_t instances = g_nt_gfx.counters.instances;
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, mesh_items, 2);
    nt_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, &colorless_item, 1);
    nt_skinned_mesh_renderer_draw_list(NT_GFX_FRAME_VERTEX, skinned_items, 2);
    /* One instanced draw per list: the two mesh and two skinned instances each share a run. */
    const bool batched = nt_gfx_draw_calls(&g_nt_gfx.counters) == draws + 3 && g_nt_gfx.counters.instances == instances + 5;
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    const bool read = nt_gfx_read_pixels(target, 0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_begin_frame();

    uint32_t mask = batched ? 1U << 5U : 0;
    if (read) {
        mask |= pixel_is(frame, -0.5F, 0.5F, tint) ? 1U << 0U : 0;
        mask |= pixel_is(frame, 0.5F, 0.5F, 0xFFFFFFFFU) ? 1U << 1U : 0;
        mask |= pixel_is(frame, 0.0F, 0.0F, 0xFF00FF00U) ? 1U << 2U : 0;
        mask |= pixel_is(frame, -0.5F, -0.5F, skinned_tint) ? 1U << 3U : 0;
        mask |= pixel_is(frame, 0.5F, -0.5F, 0xFFFFFFFFU) ? 1U << 4U : 0;
    }
    nt_material_destroy(skinned_material);
    nt_material_destroy(colorless_material);
    nt_material_destroy(mesh_material);
    nt_gfx_deactivate_mesh(quad.id);
    return mask;
}

NT_TEST_KEEPALIVE uint32_t nt_test_mesh_color_probe(void) {
    nt_gfx_begin_frame();
    if (!programs_ready()) {
        nt_gfx_end_frame();
        return MESH_PROBE_NOT_READY;
    }
    nt_texture_t color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_texture_t depth = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_DEPTH24});
    nt_texture_t deformation = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color, .depth = depth, .label = "mesh_color_probe"});
    uint32_t mask = 0;
    if (nt_gfx_render_target_valid(target)) {
        start_modules();
        mask = draw_and_check(target, deformation);
        stop_modules();
    }
    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_texture(deformation);
    nt_gfx_destroy_texture(depth);
    nt_gfx_destroy_texture(color);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    return mask;
}
