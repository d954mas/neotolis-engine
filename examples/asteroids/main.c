/******************************************************************************
Copyright 2019-2021 Evgeny Gorodetskiy
Copyright 2026 Neotolis Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    https://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.

Modified: C17 integration of Methane Asteroids App/AsteroidsApp.cpp,
AsteroidsArray.cpp, Planet.cpp and MethaneKit Camera/ArcBallCamera. Source
procedural content is generated offline; the runtime uses Neotolis pack, input,
text and GL/WebGL interfaces. See CREDITS.md for pinned source revisions and
README.md for compatibility limits.
******************************************************************************/
#include "../shared/nt_example_frames.h"
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "color/nt_color.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "font/nt_font.h"
#include "generated/asteroids_ui.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material/nt_program_ref.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"
#include "reference_scene_data/scene_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "sort/nt_sort.h"
#include "time/nt_time.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_panel.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_slider.h"
#include "ui/nt_ui_tabbar.h"
#include "window/nt_window.h"
#ifndef NT_PLATFORM_WEB
#include "fs/nt_fs.h"
#else
#include "platform/web/nt_platform_web.h"
#endif
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AST_MAX 50000U
#define AST_MAX_CHUNKS 28U
#define AST_SUBDIVISIONS 4U
#define AST_MAX_TEXTURES 50U
#define AST_PI 3.14159265358979323846

typedef struct {
    uint32_t instances, unique_meshes, textures;
} complexity_t;

static const complexity_t s_complexities[10] = {{1000, 35, 10},   {2000, 50, 10},   {3000, 75, 20},   {4000, 100, 20},  {5000, 200, 30},
                                                {10000, 300, 30}, {15000, 400, 40}, {20000, 500, 40}, {35000, 750, 50}, {50000, 1000, 50}};

typedef struct {
    uint32_t magic, version, unique_count, chunk_count, subset_count;
} geometry_header_t;
typedef struct {
    uint32_t subdivision, first_variant, variant_count, vertex_count, index_count;
} geometry_chunk_t;
typedef struct {
    uint32_t chunk_index, first_index, index_count, first_vertex, vertex_count, random_seed;
    float depth_min, depth_max;
} geometry_subset_t;
typedef struct {
    uint32_t magic, version, texture_count, layer_count, width, height;
} noise_header_t;
typedef struct {
    float world_rows[12];
    float deep[4];
    float shallow[4];
} asteroid_instance_t;
typedef struct {
    uint64_t sort_key;
    uint32_t source_index;
} asteroid_draw_item_t;

typedef struct {
    nt_resource_t resource;
    nt_mesh_t mesh;
    nt_vertex_input_t input;
} mesh_binding_t;
typedef struct {
    float eye[3], aim[3], up[3];
} camera_orientation_t;
typedef struct {
    camera_orientation_t orientation;
    float sphere[3];
    bool inside;
} camera_drag_t;

_Static_assert(sizeof(geometry_header_t) == 20, "Geometry header ABI");
_Static_assert(sizeof(geometry_chunk_t) == 20, "Geometry chunk ABI");
_Static_assert(sizeof(geometry_subset_t) == 32, "Geometry subset ABI");
_Static_assert(sizeof(noise_header_t) == 24, "Noise header ABI");
_Static_assert(sizeof(asteroid_instance_t) == 80, "Asteroid vertex attributes ABI");

static const camera_orientation_t s_initial_camera = {{-110.0F, 75.0F, 210.0F}, {0.0F, -60.0F, 25.0F}, {0.0F, 1.0F, 0.0F}};
static const camera_orientation_t s_initial_light = {{-100.0F, 120.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
static camera_orientation_t s_camera, s_light;
static camera_drag_t s_camera_drag[NT_INPUT_MAX_POINTERS], s_light_drag[NT_INPUT_MAX_POINTERS];
static float s_key_elapsed[NT_KEY_COUNT];
static bool s_key_active[NT_KEY_COUNT];
static float s_pan_pressed_world[NT_INPUT_MAX_POINTERS][3];
static uint8_t s_pointer_owner[NT_INPUT_MAX_POINTERS][NT_BUTTON_MAX];
static struct {
    uint32_t slots[2], ids[2], count;
    float span;
} s_touch;
static float s_zoom_elapsed, s_zoom_until, s_zoom_factor = 1.0F;
static int s_zoom_direction;
static bool s_eye_pivot;
static uint32_t s_level = 1, s_requested_level = 1;
static bool s_paused, s_lod_colors, s_hide_hud, s_show_help, s_ready, s_bad_data, s_fullscreen, s_hide_parameters, s_show_command_line;
static float s_min_screen_size = 0.06F;
static double s_elapsed, s_last_tick, s_frame_ms, s_cpu_ms, s_update_ms, s_render_ms, s_gpu_ms = -1.0;
static uint64_t s_mem_used, s_triangles;
static bool s_memory_sampled;
static uint32_t s_lod_counts[AST_SUBDIVISIONS], s_selected_subsets[AST_MAX];
static bool s_optimized;
static const char *const s_render_mode_names[2] = {"reference", "optimized"};
static asteroid_instance_t s_instance_staging[AST_MAX];
static asteroid_draw_item_t s_draw_items[AST_MAX], s_draw_scratch[AST_MAX];
static float s_lod_deep[AST_SUBDIVISIONS][3], s_lod_shallow[AST_SUBDIVISIONS][3];
static const ast_reference_runtime_instance *s_asteroids;
static const geometry_chunk_t *s_geometry_chunks;
static const geometry_subset_t *s_subsets;
static uint32_t s_chunk_count;
static mesh_binding_t s_rock_meshes[AST_MAX_CHUNKS], s_planet_mesh, s_sky_mesh;
static nt_resource_t s_instances_blob, s_geometry_blob, s_noise_blob, s_mars, s_sky_faces[6];
static nt_texture_t s_noise[AST_MAX_TEXTURES][3];
static nt_program_ref_t s_programs[4];
static nt_pipeline_t s_pipelines[3];
static nt_material_t s_hud_material;
static nt_font_t s_font;
static nt_hash32_t s_core_pack, s_space_pack, s_level_pack, s_noise_pack;
static nt_hash32_t s_noise_names[3], s_sky_names[6], s_diffuse_name, s_light_name;
static char s_notice[160];

static bool s_use_ui = true, s_ui_panel_open, s_ui_built, s_ui_atlas_bound;
static bool s_ui_escape_consumed, s_ui_keyboard_blocked;
static int s_ui_staged_level, s_ui_tab;
static void *s_ui_arena;
static size_t s_ui_arena_size;
static nt_ui_context_t *s_ui;
static nt_ui_scale_t s_ui_scale;
static nt_resource_t s_ui_atlas;
static nt_program_ref_t s_ui_program;
static nt_material_t s_ui_material;
static nt_atlas_region_ref_t s_ui_panel_art;
static nt_ui_button_style_t s_ui_button, s_ui_primary;
static nt_ui_checkbox_style_t s_ui_toggle;
static nt_ui_slider_style_t s_ui_slider;
static nt_ui_tabbar_style_t s_ui_tabs;
static nt_ui_scroll_style_t s_ui_scroll;
static nt_ui_modal_style_t s_ui_modal;
static const nt_ui_label_style_t s_ui_title = {.font_size = 19, .color = NT_RGBA8(237, 250, 255, 255)};
static const nt_ui_label_style_t s_ui_body = {.font_size = 14, .color = NT_RGBA8(224, 239, 247, 255)};
static const nt_ui_label_style_t s_ui_caption = {.font_size = 12, .color = NT_RGBA8(154, 181, 198, 255)};
static const nt_ui_label_style_t s_ui_section = {.font_size = 12, .color = NT_RGBA8(78, 213, 229, 255)};
static void init_ui_styles(void);

static void sort_asteroid_items(asteroid_draw_item_t *items, uint32_t count, asteroid_draw_item_t *scratch);
// NOLINTNEXTLINE(readability-function-cognitive-complexity): engine-generated radix sort includes assert expansion.
NT_SORT_DEFINE(sort_asteroid_items, asteroid_draw_item_t)

// #region source matrix and LOD math
/* Matrices here retain HLSL++'s left-handed row-vector, row-major convention.
 * Their bytes are also the column-major transpose consumed by GLSL. */
static void matrix_multiply(float out[16], const float left[16], const float right[16]) {
    float result[16];
    for (uint32_t row = 0; row < 4; row++) {
        for (uint32_t col = 0; col < 4; col++) {
            result[(row * 4U) + col] =
                left[(size_t)row * 4U] * right[col] + left[(row * 4U) + 1U] * right[4U + col] + left[(row * 4U) + 2U] * right[8U + col] + left[(row * 4U) + 3U] * right[12U + col];
        }
    }
    memcpy(out, result, sizeof(result));
}

static void matrix_rotation_axis(float out[16], const float axis[3], float angle) {
    const float s = sinf(angle);
    const float c = cosf(angle);
    const float ax = axis[0] * s;
    const float ay = axis[1] * s;
    const float az = axis[2] * s;
    const float bx = axis[0] * (1.0F - c);
    const float by = axis[1] * (1.0F - c);
    const float bz = axis[2] * (1.0F - c);
    const float xy = axis[0] * by;
    const float yz = axis[1] * bz;
    const float zx = axis[2] * bx;
    const float rotation[16] = {(axis[0] * bx) + c, xy + az, zx - ay, 0, xy - az, (axis[1] * by) + c, yz + ax, 0, zx + ay, yz - ax, (axis[2] * bz) + c, 0, 0, 0, 0, 1};
    memcpy(out, rotation, sizeof(rotation));
}

static void asteroid_world_matrix(float out[16], const ast_reference_runtime_instance *asteroid, double elapsed_seconds) {
    const float elapsed_radians = (float)(AST_PI * elapsed_seconds);
    float spin[16];
    float orbit[16];
    float spin_scale[16];
    matrix_rotation_axis(spin, asteroid->spin_axis, asteroid->spin_angle + (asteroid->spin_speed * elapsed_radians));
    matrix_rotation_axis(orbit, (float[3]){0, 1, 0}, asteroid->orbit_angle - (asteroid->orbit_speed * elapsed_radians));
    matrix_multiply(spin_scale, spin, asteroid->scale_translate);
    matrix_multiply(out, spin_scale, orbit);
}

static uint32_t asteroid_lod_index(float scale, float distance, float min_screen_size, uint32_t subdivisions) {
    const float relative_screen_size_log_2 = log2f(scale / sqrtf(distance));
    const float subdivision = roundf(relative_screen_size_log_2 - log2f(min_screen_size));
    return (uint32_t)fminf((float)(subdivisions - 1U), fmaxf(0.0F, subdivision));
}

static void camera_basis(const camera_orientation_t *camera, float right[3], float up[3], float look[3]) {
    glm_vec3_sub((float *)camera->aim, (float *)camera->eye, look);
    glm_vec3_normalize(look);
    glm_vec3_cross((float *)camera->up, look, right);
    glm_vec3_normalize(right);
    glm_vec3_cross(look, right, up);
}

static void camera_matrices(const camera_orientation_t *camera, float aspect, float view[16], float projection[16], float view_projection[16]) {
    float right[3];
    float up[3];
    float look[3];
    camera_basis(camera, right, up, look);
    const float v[16] = {right[0],
                         up[0],
                         look[0],
                         0,
                         right[1],
                         up[1],
                         look[1],
                         0,
                         right[2],
                         up[2],
                         look[2],
                         0,
                         -glm_vec3_dot((float *)camera->eye, right),
                         -glm_vec3_dot((float *)camera->eye, up),
                         -glm_vec3_dot((float *)camera->eye, look),
                         1};
    memcpy(view, v, sizeof(v));
    float fov = (float)AST_PI / 2.0F;
    if (aspect < 1.0F) {
        fov /= 0.5F + aspect / 2.0F;
    }
    const float near_depth = 600.0F;
    const float far_depth = 0.01F;
    const float height = 2.0F * near_depth * tanf(fov / 2.0F);
    const float p[16] = {2.0F * near_depth / (height * aspect),
                         0,
                         0,
                         0,
                         0,
                         2.0F * near_depth / height,
                         0,
                         0,
                         0,
                         0,
                         far_depth / (far_depth - near_depth),
                         1,
                         0,
                         0,
                         -near_depth * far_depth / (far_depth - near_depth),
                         0};
    memcpy(projection, p, sizeof(p));
    matrix_multiply(view_projection, view, projection);
}

static void instance_world_rows(asteroid_instance_t *instance, const float world[16]) {
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 4; col++) {
            instance->world_rows[(row * 4U) + col] = world[(col * 4U) + row];
        }
    }
}
// #endregion

// #region pack and GPU lifetimes
static void drop_mesh_binding(mesh_binding_t *binding) {
    nt_gfx_destroy_vertex_input(binding->input);
    binding->input = NT_VERTEX_INPUT_INVALID;
    binding->mesh = NT_MESH_INVALID;
}

static void drop_noise(void) {
    for (uint32_t t = 0; t < AST_MAX_TEXTURES; t++) {
        for (uint32_t layer = 0; layer < 3; layer++) {
            nt_gfx_destroy_texture(s_noise[t][layer]);
            s_noise[t][layer] = (nt_texture_t){0};
        }
    }
}

static void load_pack(nt_hash32_t pack, const char *filename) {
    char path[256];
#ifdef NT_CDN_URL
    (void)snprintf(path, sizeof(path), NT_CDN_URL "/asteroids/%s", filename);
#else
    (void)snprintf(path, sizeof(path), "assets/%s", filename);
#endif
    nt_result_t result = nt_resource_mount(pack, 10);
    NT_ASSERT(result == NT_OK);
    result = nt_resource_load_auto(pack, path);
    if (result != NT_OK) {
        nt_log_error("Unable to start Asteroids pack load: %s", path);
    }
}

static void load_complexity(uint32_t level, bool replace) {
    s_ready = false;
    s_bad_data = false;
    s_asteroids = NULL;
    s_geometry_chunks = NULL;
    s_subsets = NULL;
    s_chunk_count = 0;
    if (replace) {
        for (uint32_t i = 0; i < AST_MAX_CHUNKS; i++) {
            drop_mesh_binding(&s_rock_meshes[i]);
        }
        drop_noise();
        nt_resource_unmount(s_level_pack);
        nt_resource_unmount(s_noise_pack);
    }
    s_level = level;
    char filename[64];
    (void)snprintf(filename, sizeof(filename), "asteroids_level_%u.ntpack", level);
    load_pack(s_level_pack, filename);
    (void)snprintf(filename, sizeof(filename), "asteroids_noise_%u.ntpack", s_complexities[level].textures);
    load_pack(s_noise_pack, filename);
    (void)snprintf(s_notice, sizeof(s_notice), "Loading complexity %u...", level);
}

static bool invalid_content(const char *description) {
    s_bad_data = true;
    (void)snprintf(s_notice, sizeof(s_notice), "Invalid Asteroids %s; rebuild the reference packs.", description);
    nt_log_error("%s", s_notice);
    return false;
}

/* Only structural wire checks belong here: the offline generator owns numeric
 * validation and the pack CRC protects the generated float payloads. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool resolve_scene_data(void) {
    if (s_asteroids != NULL) {
        return true;
    }
    uint32_t instance_bytes = 0;
    uint32_t geometry_bytes = 0;
    const uint8_t *instances = nt_resource_get_blob(s_instances_blob, &instance_bytes);
    const uint8_t *geometry = nt_resource_get_blob(s_geometry_blob, &geometry_bytes);
    if (instances == NULL || geometry == NULL) {
        return false;
    }
    if (instance_bytes < sizeof(ast_reference_runtime_header) || geometry_bytes < sizeof(geometry_header_t)) {
        return invalid_content("headers");
    }
    ast_reference_runtime_header ih;
    geometry_header_t gh;
    memcpy(&ih, instances, sizeof(ih));
    memcpy(&gh, geometry, sizeof(gh));
    const complexity_t *complexity = &s_complexities[s_level];
    if (ih.magic != AST_REFERENCE_RUNTIME_MAGIC || ih.version != 1 || ih.complexity != s_level || ih.instance_count != complexity->instances ||
        instance_bytes != sizeof(ih) + (uint64_t)ih.instance_count * sizeof(ast_reference_runtime_instance) || gh.magic != 0x4F454741U || gh.version != 1 ||
        gh.unique_count != complexity->unique_meshes || gh.chunk_count == 0 || gh.chunk_count > AST_MAX_CHUNKS || gh.subset_count != gh.unique_count * AST_SUBDIVISIONS ||
        geometry_bytes != sizeof(gh) + (uint64_t)gh.chunk_count * sizeof(geometry_chunk_t) + (uint64_t)gh.subset_count * sizeof(geometry_subset_t)) {
        return invalid_content("record layout");
    }
    const ast_reference_runtime_instance *records = (const ast_reference_runtime_instance *)(instances + sizeof(ih));
    const geometry_chunk_t *chunks = (const geometry_chunk_t *)(geometry + sizeof(gh));
    const geometry_subset_t *subsets = (const geometry_subset_t *)(geometry + sizeof(gh) + (gh.chunk_count * sizeof(geometry_chunk_t)));
    for (uint32_t i = 0; i < gh.chunk_count; i++) {
        if (chunks[i].subdivision >= AST_SUBDIVISIONS || chunks[i].first_variant >= gh.unique_count || chunks[i].variant_count == 0 ||
            chunks[i].variant_count > gh.unique_count - chunks[i].first_variant || chunks[i].vertex_count == 0 || chunks[i].index_count == 0) {
            return invalid_content("chunk ranges");
        }
    }
    for (uint32_t i = 0; i < gh.subset_count; i++) {
        const geometry_subset_t *subset = &subsets[i];
        if (subset->chunk_index >= gh.chunk_count) {
            return invalid_content("subset chunk index");
        }
        const geometry_chunk_t *chunk = &chunks[subset->chunk_index];
        if (chunk->subdivision != i / gh.unique_count || i % gh.unique_count < chunk->first_variant || i % gh.unique_count - chunk->first_variant >= chunk->variant_count ||
            subset->first_index > chunk->index_count || subset->index_count > chunk->index_count - subset->first_index || subset->index_count == 0 || subset->index_count % 3U != 0 ||
            subset->first_vertex > chunk->vertex_count || subset->vertex_count > chunk->vertex_count - subset->first_vertex || subset->vertex_count == 0) {
            return invalid_content("subset ranges");
        }
    }
    for (uint32_t i = 0; i < ih.instance_count; i++) {
        if (records[i].mesh_index >= gh.unique_count || records[i].texture_index >= complexity->textures) {
            return invalid_content("instance indices");
        }
    }
    s_asteroids = records;
    s_geometry_chunks = chunks;
    s_subsets = subsets;
    s_chunk_count = gh.chunk_count;
    return true;
}

static bool resolve_noise(void) {
    /* Creation stops at the first missing layer, so a populated last slot
     * proves that every preceding layer exists. Restore drops the full set. */
    if (s_noise[s_complexities[s_level].textures - 1U][2].id != 0) {
        return true;
    }
    uint32_t size = 0;
    const uint8_t *blob = nt_resource_get_blob(s_noise_blob, &size);
    if (blob == NULL) {
        return false;
    }
    noise_header_t header;
    if (size < sizeof(header)) {
        return invalid_content("noise header");
    }
    memcpy(&header, blob, sizeof(header));
    const uint32_t layer_bytes = 256U * 256U * 4U;
    if (header.magic != 0x4E545341U || header.version != 1 || header.texture_count != s_complexities[s_level].textures || header.layer_count != 3 || header.width != 256 || header.height != 256 ||
        size != sizeof(header) + (uint64_t)header.texture_count * 3U * layer_bytes) {
        return invalid_content("noise layout");
    }
    /* Distinct source array layers keep distinct owned GPU storage even when
     * their bytes match. The shader reconstructs the source black border. */
    for (uint32_t t = 0; t < header.texture_count; t++) {
        for (uint32_t layer = 0; layer < 3; layer++) {
            if (s_noise[t][layer].id == 0) {
                s_noise[t][layer] = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 256,
                                                                             .height = 256,
                                                                             .data = blob + sizeof(header) + (((size_t)t * 3U + layer) * layer_bytes),
                                                                             .format = NT_TEXTURE_FORMAT_RGBA8,
                                                                             .min_filter = NT_FILTER_LINEAR_MIPMAP_NEAREST,
                                                                             .mag_filter = NT_FILTER_LINEAR,
                                                                             .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
                                                                             .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
                                                                             .gen_mipmaps = true,
                                                                             .label = "asteroid_noise_layer"});
                if (s_noise[t][layer].id == 0) {
                    return false;
                }
            }
        }
    }
    return !g_nt_gfx.context_lost && s_noise[header.texture_count - 1U][2].id != 0;
}

static bool resolve_mesh_binding(mesh_binding_t *binding, bool instanced) {
    const nt_mesh_t mesh = {.id = nt_resource_get(binding->resource)};
    if (mesh.id == 0) {
        return false;
    }
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info(mesh);
    if (info == NULL) {
        return false;
    }
    if (mesh.id == binding->mesh.id && nt_gfx_vertex_input_valid(binding->input)) {
        return true;
    }
    drop_mesh_binding(binding);
    NT_ASSERT(info->stride == 32 && info->index_type == NT_INDEX_UINT16);
    nt_vertex_input_desc_t desc = {.layout = {.attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                        {.location = 1, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 12},
                                                        {.location = 2, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 24}},
                                              .attr_count = 3,
                                              .stride = 32},
                                   .vertex_buffer = info->vbo,
                                   .index_buffer = info->ibo,
                                   .label = "asteroids_mesh_input"};
    if (instanced) {
        desc.instance_layout = (nt_vertex_layout_t){.attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
                                                              {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
                                                              {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
                                                              {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 48},
                                                              {.location = 8, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 64}},
                                                    .attr_count = 5,
                                                    .stride = sizeof(asteroid_instance_t)};
    }
    binding->input = nt_gfx_make_vertex_input(&desc);
    binding->mesh = mesh;
    return binding->input.id != 0;
}

static void restore_gpu(void) {
    s_ready = false;
    s_gpu_ms = -1;
    drop_noise();
    for (uint32_t i = 0; i < AST_MAX_CHUNKS; i++) {
        drop_mesh_binding(&s_rock_meshes[i]);
    }
    drop_mesh_binding(&s_planet_mesh);
    drop_mesh_binding(&s_sky_mesh);
    nt_text_renderer_shutdown();
    if (s_use_ui) {
        nt_sprite_renderer_shutdown();
        nt_program_ref_drop(&s_ui_program);
        s_ui_atlas_bound = false;
        init_ui_styles();
    }
    for (uint32_t i = 0; i < 4; i++) {
        nt_program_ref_drop(&s_programs[i]);
    }
    memset(s_pipelines, 0, sizeof(s_pipelines));
    nt_resource_invalidate(NT_ASSET_MESH);
    nt_resource_invalidate(NT_ASSET_TEXTURE);
    nt_resource_invalidate(NT_ASSET_SHADER_CODE);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- independent resource readiness gates
static bool prepare_gpu(void) {
    bool ready = !g_nt_gfx.context_lost && !s_bad_data;
    for (uint32_t i = 0; i < 4; i++) {
        if (nt_program_ref_update(&s_programs[i]) && i == 3) {
            nt_material_set_program(s_hud_material, s_programs[i].program);
        }
        ready = nt_gfx_program_ready(s_programs[i].program) && ready;
    }
    if (!ready || !resolve_scene_data() || !resolve_noise()) {
        return false;
    }
    ready = resolve_mesh_binding(&s_planet_mesh, true);
    ready = resolve_mesh_binding(&s_sky_mesh, false) && ready;
    for (uint32_t i = 0; i < s_chunk_count; i++) {
        ready = resolve_mesh_binding(&s_rock_meshes[i], true) && ready;
        const nt_gfx_mesh_info_t *mesh = nt_gfx_get_mesh_info(s_rock_meshes[i].mesh);
        if (mesh != NULL && (mesh->vertex_count != s_geometry_chunks[i].vertex_count || mesh->index_count != s_geometry_chunks[i].index_count)) {
            return invalid_content("mesh counts");
        }
    }
    ready = nt_resource_is_ready(s_mars) && ready;
    for (uint32_t i = 0; i < 6; i++) {
        ready = nt_resource_is_ready(s_sky_faces[i]) && ready;
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (!nt_gfx_pipeline_valid(s_pipelines[i])) {
            /* Source front faces are clockwise after its LH projection. */
            s_pipelines[i] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = s_programs[i].program,
                                                                        .depth_test = true,
                                                                        .depth_write = i != 2,
                                                                        .depth_func = NT_DEPTH_GEQUAL,
                                                                        .cull_mode = i == 2 ? NT_CULL_BACK : NT_CULL_FRONT,
                                                                        .label = "asteroids_source_pass"});
        }
        ready = s_pipelines[i].id != 0 && ready;
    }
    return ready && !g_nt_gfx.context_lost;
}
// #endregion

// #region native controls
static void format_memory(char *text, size_t capacity) {
    if (nt_example_frames_on()) {
        (void)snprintf(text, capacity, "not sampled");
    } else if (!s_memory_sampled) {
        (void)snprintf(text, capacity, "pending");
    } else if (s_mem_used == 0) {
        (void)snprintf(text, capacity, "unavailable");
    } else {
        (void)snprintf(text, capacity, "%.1f MiB", (double)s_mem_used / 1048576.0);
    }
}

static uint64_t gl_call_count(const nt_gfx_counters_t *counters) {
    uint64_t count = 0;
    for (uint32_t call = 1; call < NT_GFX_GL_COUNT; call++) {
        count += counters->gl[call];
    }
    return count;
}

static void format_gpu_time(char *text, size_t capacity) {
#if !NT_GFX_GPU_TIMING_ENABLED
    (void)snprintf(text, capacity, "disabled");
#else
    if (!nt_gfx_is_gpu_timing_supported()) {
        (void)snprintf(text, capacity, "unavailable");
    } else if (s_gpu_ms < 0) {
        (void)snprintf(text, capacity, "pending");
    } else {
        (void)snprintf(text, capacity, "%.2f ms", s_gpu_ms);
    }
#endif
}

static void reset_camera_input(bool reset_light) {
    memset(&s_touch, 0, sizeof(s_touch));
    memset(s_key_active, 0, sizeof(s_key_active));
    memset(s_key_elapsed, 0, sizeof(s_key_elapsed));
    s_zoom_elapsed = 0;
    s_zoom_until = 0;
    s_zoom_factor = 1;
    s_zoom_direction = 0;
    for (uint32_t p = 0; p < NT_INPUT_MAX_POINTERS; p++) {
        for (uint32_t b = 0; b < NT_BUTTON_MAX; b++) {
            s_pointer_owner[p][b] = g_nt_input.pointers[p].buttons[b].is_down ? 2U : 0U;
        }
    }
    if (reset_light) {
        s_light = s_initial_light;
    } else {
        s_camera = s_initial_camera;
    }
}

static void init_ui_styles(void) {
    const nt_atlas_region_ref_t rounded = nt_atlas_ref(s_ui_atlas, ASSET_ATLAS_REGION_ASTEROIDS_UI_ATLAS_ROUNDED.value);
    const nt_atlas_region_ref_t pill = nt_atlas_ref(s_ui_atlas, ASSET_ATLAS_REGION_ASTEROIDS_UI_ATLAS_PILL.value);
    const nt_atlas_region_ref_t thumb = nt_atlas_ref(s_ui_atlas, ASSET_ATLAS_REGION_ASTEROIDS_UI_ATLAS_THUMB.value);
    s_ui_panel_art = rounded;
    s_ui_button = (nt_ui_button_style_t){.idle = {.bg = rounded, .bg_tint = NT_RGBA8(37, 58, 73, 255), .scale = 1, .opacity = 1},
                                         .hover = {.bg_tint = NT_RGBA8(50, 84, 103, 255), .scale = 1, .opacity = 1},
                                         .pressed = {.bg_tint = NT_RGBA8(24, 110, 128, 255), .scale = 1, .opacity = 1},
                                         .disabled = {.bg_tint = NT_RGBA8(28, 40, 49, 255), .scale = 1, .opacity = 0.45F},
                                         .transition_speed = 18,
                                         .slice9_scale = 1};
    s_ui_primary = s_ui_button;
    s_ui_primary.idle.bg_tint = NT_RGBA8(20, 111, 130, 255);
    s_ui_primary.hover.bg_tint = NT_RGBA8(28, 142, 160, 255);
    s_ui_primary.pressed.bg_tint = NT_RGBA8(15, 88, 106, 255);
    s_ui_toggle = nt_ui_checkbox_style_defaults();
    s_ui_toggle.box_w = 44;
    s_ui_toggle.box_h = 24;
    s_ui_toggle.overlay_w = 18;
    s_ui_toggle.overlay_h = 18;
    s_ui_toggle.thumb_pad = 3;
    s_ui_toggle.gap = 12;
    s_ui_toggle.text_base = s_ui_body;
    for (uint32_t i = 0; i < 4; i++) {
        s_ui_toggle.unchecked[i].box = pill;
        s_ui_toggle.unchecked[i].check = thumb;
        s_ui_toggle.unchecked[i].box_tint = NT_RGBA8(54, 70, 84, 255);
        s_ui_toggle.unchecked[i].check_tint = NT_RGBA8(218, 233, 240, 255);
        s_ui_toggle.checked[i].box = pill;
        s_ui_toggle.checked[i].check = thumb;
        s_ui_toggle.checked[i].box_tint = NT_RGBA8(19, 151, 168, 255);
        s_ui_toggle.checked[i].check_tint = UINT32_MAX;
    }
    s_ui_slider = nt_ui_slider_style_defaults();
    s_ui_slider.track_h = 6;
    s_ui_slider.thumb_w = 24;
    s_ui_slider.thumb_h = 24;
    s_ui_slider.hit_padding_lrtb[2] = 10;
    s_ui_slider.hit_padding_lrtb[3] = 10;
    for (uint32_t i = 0; i < 4; i++) {
        s_ui_slider.states[i].track = rounded;
        s_ui_slider.states[i].fill = rounded;
        s_ui_slider.states[i].thumb = thumb;
        s_ui_slider.states[i].track_tint = NT_RGBA8(53, 73, 87, 255);
        s_ui_slider.states[i].fill_tint = NT_RGBA8(32, 183, 201, 255);
        s_ui_slider.states[i].thumb_tint = NT_RGBA8(228, 251, 255, 255);
    }
    s_ui_tabs = nt_ui_tabbar_style_defaults();
    s_ui_tabs.dir = NT_UI_TABBAR_HORIZONTAL;
    s_ui_tabs.accent_side = NT_UI_TABBAR_ACCENT_BOTTOM;
    s_ui_tabs.pad = 0;
    s_ui_tabs.gap = 0;
    s_ui_tabs.font_size = 13;
    s_ui_tabs.accent = NT_RGBA8(67, 214, 230, 255);
    s_ui_tabs.bar_bg = NT_RGBA8(13, 24, 34, 255);
    s_ui_tabs.idle.fill = 0;
    s_ui_tabs.hover.fill = NT_RGBA8(35, 54, 68, 255);
    s_ui_tabs.selected.fill = NT_RGBA8(28, 53, 66, 255);
    s_ui_tabs.selected.scale = 1;
    s_ui_tabs.text = NT_RGBA8(157, 179, 194, 255);
    s_ui_tabs.text_selected = NT_RGBA8(238, 252, 255, 255);
    s_ui_scroll = nt_ui_scroll_style_defaults();
    s_ui_scroll.track_ref = rounded;
    s_ui_scroll.thumb_ref = rounded;
    s_ui_scroll.track_tint = NT_RGBA8(24, 37, 47, 160);
    s_ui_scroll.thumb_tint = NT_RGBA8(82, 122, 139, 230);
    s_ui_scroll.bar_visibility = NT_UI_SCROLLBAR_AUTO;
    s_ui_scroll.bar_thickness = 5;
    s_ui_modal = nt_ui_modal_style_defaults();
    s_ui_modal.open.type = NT_UI_MODAL_ANIM_FADE;
    s_ui_modal.close.type = NT_UI_MODAL_ANIM_FADE;
    s_ui_modal.backdrop_alpha = 0.6F;
    s_ui_modal.ease_speed = 18;
}

static void init_ui(void) {
    nt_mem_scratch_init((size_t)128U * 1024U);
    const nt_result_t atlas_result = nt_atlas_init();
    NT_ASSERT(atlas_result == NT_OK);
    nt_ui_module_init();
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.max_elements = 384;
    desc.state_slots = 64;
    desc.state_probe_max = 16;
    s_ui_arena_size = nt_ui_min_arena_size(&desc);
    s_ui_arena = malloc(s_ui_arena_size);
    NT_ASSERT(s_ui_arena != NULL);
    s_ui = nt_ui_create_context(s_ui_arena, s_ui_arena_size, &desc);
    NT_ASSERT(s_ui != NULL);
    s_ui_atlas = nt_resource_request(ASSET_ATLAS_ASTEROIDS_UI_ATLAS, NT_ASSET_ATLAS);
    const nt_resource_t texture = nt_resource_request(ASSET_TEXTURE_ASTEROIDS_UI_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_ui_program.vs = nt_resource_request(nt_hash64_str("assets/shaders/sprite.vert"), NT_ASSET_SHADER_CODE);
    s_ui_program.fs = nt_resource_request(nt_hash64_str("assets/shaders/sprite.frag"), NT_ASSET_SHADER_CODE);
    s_ui_material = nt_material_create(&(nt_material_create_desc_t){.textures = {{.name = "u_texture", .resource = texture}},
                                                                    .texture_count = 1,
                                                                    .blend = nt_blend_alpha_premultiplied(),
                                                                    .depth_test = false,
                                                                    .depth_write = false,
                                                                    .cull_mode = NT_CULL_NONE,
                                                                    .label = "asteroids_controls"});
    nt_ui_set_sprite_material(s_ui, s_ui_material);
    nt_ui_set_text_material(s_ui, s_hud_material, 0);
    nt_ui_set_font(s_ui, 0, s_font);
    init_ui_styles();
    nt_log_info("Asteroids UI: context=%zu bytes, scratch=131072 bytes, elements=384; --ui 0 selects source HUD", s_ui_arena_size);
}

/* Each action is a native button containing a native label; its label is not a separate hit target. */
static bool ui_action(const char *id, const char *label, bool primary, bool enabled, float width) {
    nt_ui_button_begin(s_ui, NT_UI_DATA_LAYER(1), nt_hash32_str(id).value, primary ? &s_ui_primary : &s_ui_button,
                       &(Clay_ElementDeclaration){.layout = {.sizing = {width > 0 ? CLAY_SIZING_FIXED(width) : CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)},
                                                             .padding = CLAY_PADDING_ALL(8),
                                                             .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
                       enabled, NULL);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), label, &s_ui_body);
    return nt_ui_button_end(s_ui);
}

static void ui_metric(const char *label, const char *value) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 10, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), label, &s_ui_caption); }
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), value, &s_ui_body);
    }
}

static void ui_settings(float width) {
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Mouse: drag to orbit, wheel to zoom.\nTouch: one finger orbit, pinch zoom.", &s_ui_caption);
    char text[128];
    const complexity_t *preview = &s_complexities[s_ui_staged_level];
    (void)snprintf(text, sizeof(text), "COMPLEXITY  %d / 9", s_ui_staged_level);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), text, &s_ui_section);
    (void)snprintf(text, sizeof(text), "%u asteroids\n%u unique meshes / %u textures", preview->instances, preview->unique_meshes, preview->textures);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), text, &s_ui_body);
    s_ui_slider.track_w = width - 48;
    (void)nt_ui_slider_int(s_ui, NT_UI_DATA_LAYER(1), 2, CLAY_ID("asteroids.complexity").id, NULL, &s_ui_staged_level, 0, 9, 1, &s_ui_slider,
                           &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}, true);
    const bool changed = (uint32_t)s_ui_staged_level != s_level;
    (void)snprintf(text, sizeof(text), changed ? "Apply complexity %d" : "Complexity %d active", s_ui_staged_level);
    if (ui_action("asteroids.apply", s_ready ? text : "Loading scene...", true, changed && s_ready, 0)) {
        s_requested_level = (uint32_t)s_ui_staged_level;
    }
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Preview freely. Apply loads one preset.", &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "SIMULATION", &s_ui_section);
    const Clay_ElementDeclaration toggle = {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)}}};
    (void)nt_ui_toggle(s_ui, NT_UI_DATA_LAYER(1), 2, CLAY_ID("asteroids.optimized").id, "Optimized rendering", &s_optimized, &s_ui_toggle, &toggle, true);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), s_optimized ? "Grouped draws. Same asteroids and LODs." : "Reference: one draw per asteroid.", &s_ui_caption);
    (void)nt_ui_toggle(s_ui, NT_UI_DATA_LAYER(1), 2, CLAY_ID("asteroids.pause").id, "Pause animations", &s_paused, &s_ui_toggle, &toggle, true);
    (void)nt_ui_toggle(s_ui, NT_UI_DATA_LAYER(1), 2, CLAY_ID("asteroids.lod_colors").id, "Color by mesh LOD", &s_lod_colors, &s_ui_toggle, &toggle, true);
    (void)snprintf(text, sizeof(text), "%.5g", (double)s_min_screen_size);
    ui_metric("Min. screen size", text);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 8}}) {
        if (ui_action("asteroids.lod_half", "/ 2", false, true, 0)) {
            s_min_screen_size = fmaxf(0.00001F, s_min_screen_size / 2.0F);
        }
        if (ui_action("asteroids.lod_double", "x 2", false, true, 0)) {
            s_min_screen_size = fminf(1000.0F, s_min_screen_size * 2.0F);
        }
        if (ui_action("asteroids.lod_reset", "Reset", false, true, 0)) {
            s_min_screen_size = 0.06F;
        }
    }
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Smaller threshold selects finer meshes.", &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "CAMERA & LIGHT", &s_ui_section);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 8}}) {
        if (ui_action("asteroids.camera_reset", "Reset view", false, true, 0)) {
            reset_camera_input(false);
        }
        if (ui_action("asteroids.light_reset", "Reset light", false, true, 0)) {
            reset_camera_input(true);
        }
    }
    (void)nt_ui_toggle(s_ui, NT_UI_DATA_LAYER(1), 2, CLAY_ID("asteroids.pivot").id, "Eye-centered pivot", &s_eye_pivot, &s_ui_toggle, &toggle, true);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Middle drag: pan. Right drag: light.\nF4 hides every overlay.", &s_ui_caption);
}

static void ui_statistics(void) {
    char value[96];
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "FRAME TIMINGS & COMMANDS", &s_ui_section);
    (void)snprintf(value, sizeof(value), "%.2f ms", s_frame_ms);
    ui_metric("Frame interval", value);
    (void)snprintf(value, sizeof(value), "%.2f ms", s_cpu_ms);
    ui_metric("CPU to end-frame", value);
    (void)snprintf(value, sizeof(value), "%.2f ms", s_update_ms);
    ui_metric("Instance update", value);
    (void)snprintf(value, sizeof(value), "%.2f ms", s_render_ms);
    ui_metric("Render submit + HUD", value);
    format_gpu_time(value, sizeof(value));
    ui_metric("GPU scene (delayed)", value);
    (void)snprintf(value, sizeof(value), "%u", nt_gfx_draw_calls(&g_nt_gfx.last_frame));
    ui_metric("DC: recorded draws", value);
    (void)snprintf(value, sizeof(value), "%" PRIu64, gl_call_count(&g_nt_gfx.last_frame));
    ui_metric("GL: issued calls", value);
    const nt_gfx_counters_t *counters = &g_nt_gfx.last_frame;
    const uint64_t gl_draws =
        (uint64_t)counters->gl[NT_GFX_GL_glDrawArrays] + counters->gl[NT_GFX_GL_glDrawArraysInstanced] + counters->gl[NT_GFX_GL_glDrawElements] + counters->gl[NT_GFX_GL_glDrawElementsInstanced];
    (void)snprintf(value, sizeof(value), "%" PRIu64, gl_draws);
    ui_metric("GL draw calls", value);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "DC and GL are prior-frame totals including the HUD. DC counts recorded gfx draws. GL counts actual backend calls, including binds, uploads and queries.",
                &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                "CPU is callback wall time through end-frame, before timing/memory polling and buffer swap. Frame interval includes presentation. GPU is the latest asynchronous scene timer, "
                "excluding the HUD.",
                &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), s_ready ? "ACTIVE SOURCE WORKLOAD" : "LOADING SOURCE WORKLOAD", &s_ui_section);
    (void)snprintf(value, sizeof(value), "%u", s_complexities[s_level].instances);
    ui_metric("Asteroids", value);
    (void)snprintf(value, sizeof(value), "%u / %u", s_complexities[s_level].unique_meshes, s_complexities[s_level].textures);
    ui_metric("Meshes / textures", value);
    ui_metric("Mesh subdivisions", "4");
    for (uint32_t i = 0; i < AST_SUBDIVISIONS; i++) {
        char label[16];
        (void)snprintf(label, sizeof(label), "LOD %u", i);
        (void)snprintf(value, sizeof(value), "%u", s_lod_counts[i]);
        ui_metric(label, s_ready ? value : "pending");
    }
    (void)snprintf(value, sizeof(value), "%" PRIu64, s_triangles);
    ui_metric("Asteroid triangles", s_ready ? value : "pending");
    (void)snprintf(value, sizeof(value), "%u x %u", g_nt_window.fb_width, g_nt_window.fb_height);
    ui_metric("Framebuffer", value);
    format_memory(value, sizeof(value));
#ifdef NT_PLATFORM_WEB
    ui_metric("WASM allocator used", value);
#else
    ui_metric("Process RSS", value);
#endif
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                s_optimized ? "Indexed instances grouped by mesh and texture.\nNo frustum culling. Same source LODs.\nTextures: 256 x 256, 3 sampled layers."
                            : "One indexed draw per asteroid.\nNo frustum culling or draw merging.\nTextures: 256 x 256, 3 sampled layers.",
                &s_ui_caption);
}

static void ui_help(void) {
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Mouse: drag to orbit, wheel to zoom.\nTouch: one finger orbit, pinch zoom.\nSettings: pause, quality, reset view.", &s_ui_body);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "SCENE CONTROLS", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                "Left drag   Orbit camera\nMiddle drag Pan camera\nRight drag  Rotate light\nWheel       Zoom\nW A S D     Move camera\nPage Up/Dn  Move vertically\nArrows      Rotate camera\nAlt+R  "
                "     Reset view\nCtrl+L      Reset light\nAlt+P       Change pivot",
                &s_ui_body);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "KEYBOARD SHORTCUTS", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                "0-9 / [ ]   Load complexity\nCtrl+P      Pause animations\nL           LOD colors\n; / '       Coarser / finer LOD\nF1 / F2     Help / CLI options\nF3          Collapse controls\nF4 "
                "         Hide all overlays\nCtrl+F      Fullscreen\nCtrl+Q      Quit (native)\nEscape      Close modal / quit",
                &s_ui_body);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "COMMAND LINE", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "--complexity 0..9 (or -c)\n--optimized 0|1\n--paused 0|1\n--ui 0|1 (source HUD / native UI)\n--hide-hud 0|1\n--frames N (native diagnostics)",
                &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "COMPATIBILITY", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                "Source scene, camera and LOD math preserved. GL/WebGL uses three 2D samplers per asteroid and six for the sky. Parallel command lists, device selection and swapchain-buffer controls "
                "are unavailable.",
                &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "CREDITS", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                "Methane Asteroids\nEvgeny Gorodetskiy / Apache 2.0\n\nMars: Solar System Scope / INOVE\nCC BY 4.0. Original JPG unchanged.\n\nGalaxy: exact upstream faces.\nPanorama: ESO/S. Brunier, CC BY 4.0; "
                "adapted as a cubemap in Methane Asteroids.\n\nUI: native Neotolis components.\nFont: DejaVu Sans Mono.\nFull sources and licenses: CREDITS.md",
                &s_ui_caption);
}

static void ui_panel(float width, float height) {
    s_ui_tabs.tab_extent = (uint16_t)((width - 28) / 3);
    CLAY({.id = CLAY_ID("asteroids.panel"), .layout = {.sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(height)}}}) {
        nt_ui_block_pointer(s_ui, CLAY_ID("asteroids.panel").id, NULL);
        nt_ui_panel_begin(s_ui, NT_UI_DATA_LAYER(0), &s_ui_panel_art, &(nt_ui_image_style_t){.color_packed = NT_RGBA8(12, 22, 32, 238), .slice9_scale = 1},
                          &(Clay_ElementDeclaration){
                              .layout = {.sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(height)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(14), .childGap = 12}});

        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)}, .childGap = 8, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Controls", &s_ui_title); }
            if (ui_action("asteroids.close_panel", "Close", false, true, 72)) {
                s_ui_panel_open = false;
            }
        }
        const char *tabs[] = {"Settings", "Statistics", "Help"};
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)}}}) {
            (void)nt_ui_tabbar(s_ui, NT_UI_DATA_LAYER(1), 2, CLAY_ID("asteroids.tabs").id, tabs, NULL, 3, &s_ui_tab, &s_ui_tabs);
        }
        const char *scroll_ids[] = {"asteroids.settings_scroll", "asteroids.stats_scroll", "asteroids.help_scroll"};
        nt_ui_scroll_begin(
            s_ui, NT_UI_DATA_LAYER(1), nt_hash32_str(scroll_ids[s_ui_tab]).value, &s_ui_scroll,
            &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = {.right = 10}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 12}});
        if (s_ui_tab == 0) {
            ui_settings(width - 28);
        } else if (s_ui_tab == 1) {
            ui_statistics();
        } else {
            ui_help();
        }
        nt_ui_scroll_end(s_ui);
        nt_ui_panel_end(s_ui);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- declarative Clay scopes expand to loops
static void build_ui(void) {
    s_ui_built = false;
    s_ui_escape_consumed = false;
    s_ui_keyboard_blocked = false;
    if (nt_program_ref_update(&s_ui_program)) {
        nt_material_set_program(s_ui_material, s_ui_program.program);
    }
    if (!nt_resource_is_ready(s_ui_atlas) || !nt_gfx_program_ready(s_ui_program.program) || !nt_gfx_program_ready(s_programs[3].program) || nt_font_get_metrics(s_font).units_per_em == 0) {
        return;
    }
    if (!s_ui_atlas_bound) {
        const uint32_t white = nt_atlas_find_region(s_ui_atlas, ASSET_ATLAS_REGION_ASTEROIDS_UI_ATLAS__WHITE.value);
        NT_ASSERT(white != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ui, s_ui_atlas, white);
        s_ui_atlas_bound = true;
    }
    if (g_nt_window.width == 0 || g_nt_window.height == 0 || g_nt_window.fb_width == 0 || g_nt_window.fb_height == 0) {
        return;
    }
    const float fit = fminf(1, fminf((float)g_nt_window.width / 320.0F, (float)g_nt_window.height / 360.0F));
    const float width = (float)g_nt_window.width / fit;
    const float height = (float)g_nt_window.height / fit;
    s_ui_scale = (nt_ui_scale_t){.logical_w = width,
                                 .logical_h = height,
                                 .scale_x = (float)g_nt_window.fb_width / width,
                                 .scale_y = (float)g_nt_window.fb_height / height,
                                 .fb_w = (float)g_nt_window.fb_width,
                                 .fb_h = (float)g_nt_window.fb_height};
    const bool narrow = width < 720;
    const bool compact_status = width < 900;
    const float status_height = compact_status ? 76.0F : 56.0F;
    nt_ui_begin(s_ui, width, height, g_nt_app.dt, g_nt_input.pointers, NT_INPUT_MAX_POINTERS);
    nt_ui_set_viewport(s_ui, nt_ui_viewport_from_scale(&s_ui_scale));
    CLAY({.id = CLAY_ID("asteroids.ui_root"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(12), .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        if (!s_hide_hud) {
            CLAY({.id = CLAY_ID("asteroids.status"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(status_height)}}}) {
                nt_ui_block_pointer(s_ui, CLAY_ID("asteroids.status").id, NULL);
                nt_ui_panel_begin(
                    s_ui, NT_UI_DATA_LAYER(0), &s_ui_panel_art, &(nt_ui_image_style_t){.color_packed = NT_RGBA8(12, 22, 32, 210), .slice9_scale = 1},
                    &(Clay_ElementDeclaration){
                        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(status_height)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(6), .childGap = 4}});
                char status[160];
                char gpu[32];
                format_gpu_time(gpu, sizeof(gpu));
                const double fps = s_frame_ms > 0 ? 1000.0 / s_frame_ms : 0;
                const uint32_t draws = nt_gfx_draw_calls(&g_nt_gfx.last_frame);
                const uint64_t gl_calls = gl_call_count(&g_nt_gfx.last_frame);
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)}, .childGap = 8, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {
                        if (compact_status) {
                            (void)snprintf(status, sizeof(status), "%.0f FPS | CPU %.1f ms", fps, s_cpu_ms);
                        } else {
                            (void)snprintf(status, sizeof(status), "%.0f FPS | CPU %.1f ms | GPU %s | DC %u | GL %" PRIu64, fps, s_cpu_ms, gpu, draws, gl_calls);
                        }
                        nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), status, &s_ui_caption);
                    }
                    if (ui_action("asteroids.open_controls", s_ui_panel_open ? "Close" : "Settings", false, true, 88)) {
                        s_ui_panel_open = !s_ui_panel_open;
                    }
                }
                if (compact_status) {
                    (void)snprintf(status, sizeof(status), "GPU %s  DC %u  GL %" PRIu64, gpu, draws, gl_calls);
                    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), status, &s_ui_caption);
                }
                nt_ui_panel_end(s_ui);
            }
            if (s_notice[0] != '\0') {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(10)}, .backgroundColor = {21, 33, 43, 230}, .userData = NT_UI_CLAY_DATA(1)}) {
                    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), s_notice, &s_ui_caption);
                }
            }
            if (!narrow && s_ui_panel_open && height > status_height + 116) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(352), CLAY_SIZING_FIXED(height - status_height - 36)}},
                      .floating = {.attachTo = CLAY_ATTACH_TO_ROOT,
                                   .offset = {-12, status_height + 24},
                                   .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP},
                                   .zIndex = 10}}) {
                    ui_panel(352, height - status_height - 36);
                }
            }
            if (narrow) {
                const nt_ui_modal_result_t modal = nt_ui_modal_begin(s_ui, CLAY_ID("asteroids.compact_modal").id, &s_ui_modal, s_ui_panel_open);
                s_ui_keyboard_blocked = modal.visible || s_ui_panel_open;
                if (modal.close_requested) {
                    s_ui_escape_consumed = modal.reason == NT_UI_MODAL_CLOSE_ESC;
                    s_ui_panel_open = false;
                }
                if (modal.visible && height > 180) {
                    ui_panel(fminf(352, width - 24), fminf(680, height - 100));
                }
                nt_ui_modal_end(s_ui);
            }
        }
    }
    nt_ui_end(s_ui);
    s_ui_built = !s_hide_hud;
}

static void draw_ui(void) {
    if (!s_ui_built || s_hide_hud) {
        return;
    }
    nt_frame_uniforms_t globals = {0};
    nt_ui_make_screen_view_proj(s_ui_scale.logical_w, s_ui_scale.logical_h, globals.view_proj);
    nt_gfx_bind_uniform_block(0, &globals, sizeof(globals));
    const nt_ui_target_t target = nt_ui_scale_make_target(&s_ui_scale);
    nt_ui_walk(s_ui, &target);
}

/* A press owns its complete drag. UI-origin drags cannot enter the scene after leaving a panel. */
static nt_pointer_t scene_pointer(uint32_t index) {
    nt_pointer_t pointer = g_nt_input.pointers[index];
    if (!s_use_ui) {
        return pointer;
    }
    bool over_ui = false;
    if (s_use_ui && s_ui_built) {
        bool owned_by_ui = false;
        for (uint32_t b = 0; b < NT_BUTTON_MAX; b++) {
            owned_by_ui = owned_by_ui || s_pointer_owner[index][b] == 2U;
        }
        over_ui = nt_ui_pointer_hot(s_ui, index).id != 0 || s_ui_keyboard_blocked || (owned_by_ui && nt_ui_wants_pointer(s_ui));
    }
    for (uint32_t b = 0; b < NT_BUTTON_MAX; b++) {
        if (pointer.buttons[b].is_pressed) {
            s_pointer_owner[index][b] = over_ui ? 2U : 1U;
        }
        if (!pointer.active || !pointer.buttons[b].is_down) {
            s_pointer_owner[index][b] = 0;
        }
        if (s_pointer_owner[index][b] != 1U) {
            pointer.buttons[b] = (nt_button_state_t){0};
        }
    }
    if (over_ui) {
        pointer.wheel_dx = 0;
        pointer.wheel_dy = 0;
    }
    return pointer;
}
// #endregion

// #region camera and controls
static void rotate_camera(camera_orientation_t *camera, const camera_orientation_t *base, const camera_orientation_t *view_camera, const float axis[3], float angle) {
    float right[3];
    float up[3];
    float look[3];
    float world_axis[3];
    float rotation[16];
    camera_basis(view_camera, right, up, look);
    for (uint32_t i = 0; i < 3; i++) {
        world_axis[i] = right[i] * axis[0] + up[i] * axis[1] + look[i] * axis[2];
    }
    matrix_rotation_axis(rotation, world_axis, -angle);
    float direction[3];
    float rotated[3];
    float rotated_up[3];
    glm_vec3_sub((float *)base->aim, (float *)base->eye, direction);
    for (uint32_t i = 0; i < 3; i++) {
        rotated[i] = direction[0] * rotation[i] + direction[1] * rotation[4U + i] + direction[2] * rotation[8U + i];
        rotated_up[i] = base->up[0] * rotation[i] + base->up[1] * rotation[4U + i] + base->up[2] * rotation[8U + i];
    }
    memcpy(camera->up, rotated_up, sizeof(rotated_up));
    if (camera == &s_camera && s_eye_pivot) {
        glm_vec3_add((float *)base->eye, rotated, camera->aim);
    } else {
        glm_vec3_sub((float *)base->aim, rotated, camera->eye);
    }
}

static void sphere_projection(float out[3], float x, float y, bool primary, camera_drag_t *drag, bool light) {
    const float radius = (float)(g_nt_window.fb_width < g_nt_window.fb_height ? g_nt_window.fb_width : g_nt_window.fb_height) * 0.45F;
    x -= (float)g_nt_window.fb_width / 2.0F;
    y -= (float)g_nt_window.fb_height / 2.0F;
    const float distance = sqrtf((x * x) + (y * y));
    if (primary) {
        drag->inside = distance <= radius;
    }
    if (light) {
        float light_look[3];
        float camera_look[3];
        glm_vec3_sub(drag->orientation.aim, drag->orientation.eye, light_look);
        glm_vec3_sub(s_camera.aim, s_camera.eye, camera_look);
        const float direction = glm_vec3_dot(light_look, camera_look) >= 0 ? 1.0F : -1.0F;
        x *= (drag->inside ? 1.0F : -1.0F) * direction;
        y *= -direction;
    } else {
        x = -x;
    }
    float z_sign = 1.0F;
    if (!primary && drag->inside && distance > radius) {
        if (distance < 2.0F * radius) {
            const float reflection = (2.0F * radius - distance) / distance;
            x *= reflection;
            y *= reflection;
        } else {
            x = 0;
            y = 0;
        }
        z_sign = -1.0F;
    }
    out[0] = x;
    out[1] = y;
    out[2] = drag->inside ? z_sign * sqrtf(fmaxf(0, (radius * radius) - (x * x) - (y * y))) : 0;
    glm_vec3_normalize(out);
}

static void drag_camera(camera_orientation_t *camera, camera_drag_t *drag, const nt_pointer_t *pointer, nt_button_t button) {
    if (pointer->buttons[button].is_pressed) {
        drag->orientation = *camera;
        sphere_projection(drag->sphere, pointer->x, pointer->y, true, drag, camera == &s_light);
    }
    if (!pointer->buttons[button].is_down || (pointer->dx == 0 && pointer->dy == 0)) {
        return;
    }
    float sphere[3];
    float cross[3];
    sphere_projection(sphere, pointer->x, pointer->y, false, drag, camera == &s_light);
    glm_vec3_cross(drag->sphere, sphere, cross);
    const float sine = glm_vec3_norm(cross);
    if (sine <= 1E-7F) {
        if (drag->inside) {
            glm_vec3_cross(drag->sphere, (float[3]){0, 0, 1}, cross);
        } else {
            memcpy(cross, (float[3]){0, 0, 1}, sizeof(cross));
        }
    }
    if (glm_vec3_norm2(cross) <= 1E-14F) {
        return;
    }
    glm_vec3_normalize(cross);
    const float angle = atan2f(sine, glm_vec3_dot(drag->sphere, sphere));
    rotate_camera(camera, &drag->orientation, camera == &s_light ? &s_camera : &drag->orientation, cross, angle);
    if (fabsf(angle) > (float)AST_PI / 2.0F) {
        drag->orientation = *camera;
        memcpy(drag->sphere, sphere, sizeof(sphere));
    }
}

static void zoom_camera(float factor) {
    float direction[3];
    glm_vec3_sub(s_camera.aim, s_camera.eye, direction);
    const float distance = glm_vec3_norm(direction);
    glm_vec3_scale(direction, glm_clamp(distance * factor, 60.0F, 400.0F) / distance, direction);
    if (s_eye_pivot) {
        glm_vec3_add(s_camera.eye, direction, s_camera.aim);
    } else {
        glm_vec3_sub(s_camera.aim, direction, s_camera.eye);
    }
}

/* Rebase when fingers change so a pinch never resumes a stale orbit snapshot. */
static void touch_camera(const nt_pointer_t pointers[NT_INPUT_MAX_POINTERS]) {
    uint32_t slots[2] = {0};
    uint32_t ids[2] = {0};
    uint32_t count = 0;
    bool pressed = false;
    for (uint32_t p = 0; p < NT_INPUT_MAX_POINTERS; p++) {
        const nt_pointer_t *pointer = &pointers[p];
        if (!pointer->active || pointer->type != NT_POINTER_TOUCH || !pointer->buttons[NT_BUTTON_LEFT].is_down) {
            continue;
        }
        if (count < 2) {
            slots[count] = p;
            ids[count] = pointer->id;
            pressed = pressed || pointer->buttons[NT_BUTTON_LEFT].is_pressed;
        }
        count++;
    }
    const bool changed = pressed || count != s_touch.count || memcmp(slots, s_touch.slots, sizeof(slots)) != 0 || memcmp(ids, s_touch.ids, sizeof(ids)) != 0;
    if (count == 1) {
        nt_pointer_t pointer = pointers[slots[0]];
        if (changed) {
            pointer.buttons[NT_BUTTON_LEFT].is_pressed = true;
            pointer.dx = 0;
            pointer.dy = 0;
        }
        drag_camera(&s_camera, &s_camera_drag[slots[0]], &pointer, NT_BUTTON_LEFT);
    }
    float span = 0;
    if (count == 2) {
        span = hypotf(pointers[slots[0]].x - pointers[slots[1]].x, pointers[slots[0]].y - pointers[slots[1]].y);
        if (!changed && span > 1 && s_touch.span > 1 && span != s_touch.span) {
            zoom_camera(s_touch.span / span);
        }
    }
    memcpy(s_touch.slots, slots, sizeof(slots));
    memcpy(s_touch.ids, ids, sizeof(ids));
    s_touch.count = count;
    s_touch.span = span;
    if (count > 0) {
        s_zoom_until = 0;
        s_zoom_elapsed = 0;
    }
}

/* The source's inverse-projection times column-vector operation returns a
 * direction-like point; view translation is deliberately absent here. */
static void camera_screen_world(float out[3], float x, float y) {
    const float aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    float view[16];
    float projection[16];
    float view_projection[16];
    camera_matrices(&s_camera, aspect, view, projection, view_projection);
    const float screen_x = (2.0F * x / (float)g_nt_window.fb_width - 1.0F) / projection[0];
    const float screen_y = (1.0F - 2.0F * y / (float)g_nt_window.fb_height) / projection[5];
    const float screen_z = 1.0F / projection[14];
    for (uint32_t i = 0; i < 3; i++) {
        out[i] = view[(size_t)i * 4U] * screen_x + view[(i * 4U) + 1U] * screen_y + view[(i * 4U) + 2U] * screen_z;
    }
}

static float key_motion(nt_key_t key, float dt) {
    const bool down = nt_input_key_is_down(key);
    if (nt_input_key_is_pressed(key)) {
        s_key_active[key] = true;
        s_key_elapsed[key] = 0;
    }
    if (!s_key_active[key] || s_paused) {
        return 0;
    }
    s_key_elapsed[key] += dt;
    /* A tap runs for the source's minimum animation duration; a longer hold
     * stops immediately on release, because SetDuration is absolute. */
    if (!down && s_key_elapsed[key] >= 0.3F) {
        s_key_active[key] = false;
        return 0;
    }
    return dt * fmaxf(1.0F, s_key_elapsed[key] / 0.3F);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_input(float dt) {
    if (nt_example_frames_on()) {
        return;
    }
    nt_pointer_t scene_pointers[NT_INPUT_MAX_POINTERS];
    const bool scene_blocked = s_use_ui && (s_ui_keyboard_blocked || s_ui_escape_consumed || g_nt_window.fb_width == 0 || g_nt_window.fb_height == 0);
    for (uint32_t p = 0; p < NT_INPUT_MAX_POINTERS; p++) {
        scene_pointers[p] = scene_pointer(p);
        if (scene_blocked) {
            for (uint32_t b = 0; b < NT_BUTTON_MAX; b++) {
                s_pointer_owner[p][b] = g_nt_input.pointers[p].buttons[b].is_down ? 2U : 0U;
                scene_pointers[p].buttons[b] = (nt_button_state_t){0};
            }
        }
    }
    if (scene_blocked) {
        memset(&s_touch, 0, sizeof(s_touch));
        memset(s_key_active, 0, sizeof(s_key_active));
        memset(s_key_elapsed, 0, sizeof(s_key_elapsed));
        s_zoom_until = 0;
        s_zoom_elapsed = 0;
    }
    const bool ctrl = nt_input_key_is_down(NT_KEY_LCTRL) || nt_input_key_is_down(NT_KEY_RCTRL);
    const bool alt = nt_input_key_is_down(NT_KEY_LALT) || nt_input_key_is_down(NT_KEY_RALT);
    if (s_use_ui) {
        if (nt_input_key_is_pressed(NT_KEY_F1) || nt_input_key_is_pressed(NT_KEY_F2)) {
            s_ui_panel_open = !(s_ui_panel_open && s_ui_tab == 2 && nt_input_key_is_pressed(NT_KEY_F1));
            s_ui_tab = 2;
            s_hide_hud = false;
        }
        if (nt_input_key_is_pressed(NT_KEY_F3)) {
            s_ui_panel_open = !s_ui_panel_open;
        }
        if (nt_input_key_is_pressed(NT_KEY_F4)) {
            s_hide_hud = !s_hide_hud;
        }
        if (s_ui_keyboard_blocked || s_ui_escape_consumed) {
            return;
        }
    }
    for (uint32_t i = 0; i < 10; i++) {
        if (!ctrl && !alt && nt_input_key_is_pressed((nt_key_t)(NT_KEY_0 + (int)i))) {
            s_requested_level = i;
        }
    }
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        if (ctrl) {
            s_paused = !s_paused;
        } else if (alt) {
            s_eye_pivot = !s_eye_pivot;
        } else {
            (void)snprintf(s_notice, sizeof(s_notice), "Parallel command-list rendering is unavailable in this GL/WebGL backend.");
        }
    }
    if (nt_input_key_is_pressed(NT_KEY_L)) {
        if (ctrl) {
            reset_camera_input(true);
        } else if (!alt) {
            s_lod_colors = !s_lod_colors;
        }
    }
    if (alt && nt_input_key_is_pressed(NT_KEY_R)) {
        reset_camera_input(false);
    }
    if (!s_use_ui && nt_input_key_is_pressed(NT_KEY_F1)) {
        s_show_help = !s_show_help;
    }
    if (!s_use_ui && nt_input_key_is_pressed(NT_KEY_F2)) {
        s_show_command_line = !s_show_command_line;
    }
    if (!s_use_ui && nt_input_key_is_pressed(NT_KEY_F3)) {
        s_hide_parameters = !s_hide_parameters;
    }
    if (!s_use_ui && nt_input_key_is_pressed(NT_KEY_F4)) {
        s_hide_hud = !s_hide_hud;
    }
    if (ctrl && nt_input_key_is_pressed(NT_KEY_F)) {
        s_fullscreen = !s_fullscreen;
        nt_window_set_fullscreen(s_fullscreen);
    }
#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE) || (ctrl && nt_input_key_is_pressed(NT_KEY_Q))) {
        nt_app_quit();
    }
#endif
    /* A reset wins over drag/key motion already sampled for this frame. */
    if ((ctrl && nt_input_key_is_pressed(NT_KEY_L)) || (alt && nt_input_key_is_pressed(NT_KEY_R))) {
        return;
    }
    /* Typed punctuation is layout-dependent because physical punctuation keys
     * are not exposed by nt_input. It preserves these commands, not key holds. */
    uint32_t codepoint = 0;
    while (nt_input_pop_char(&codepoint)) {
        switch (codepoint) {
        case '[':
            s_requested_level = s_requested_level > 0 ? s_requested_level - 1U : 0;
            break;
        case ']':
            s_requested_level = s_requested_level < 9 ? s_requested_level + 1U : 9;
            break;
        case ';':
            s_min_screen_size *= 2.0F;
            break;
        case '\'':
            s_min_screen_size /= 2.0F;
            break;
        case '-':
            zoom_camera(1.1F);
            break;
        case '=':
            zoom_camera(0.9F);
            break;
        case ',':
            rotate_camera(&s_camera, &s_camera, &s_camera, (float[3]){0, 0, 1}, (float)AST_PI / 180.0F);
            break;
        case '.':
            rotate_camera(&s_camera, &s_camera, &s_camera, (float[3]){0, 0, -1}, (float)AST_PI / 180.0F);
            break;
        default:
            break;
        }
    }
    s_min_screen_size = glm_clamp(s_min_screen_size, 0.00001F, 1000.0F);
    if (g_nt_window.fb_width == 0 || g_nt_window.fb_height == 0) {
        return;
    }
    float movement[3] = {0};
    float wheel = 0;
    if (s_use_ui) {
        touch_camera(scene_pointers);
    }
    const uint32_t pointer_count = s_use_ui ? NT_INPUT_MAX_POINTERS : 1U;
    for (uint32_t p = 0; p < pointer_count; p++) {
        const nt_pointer_t pointer = scene_pointers[p];
        if (s_use_ui && pointer.type == NT_POINTER_TOUCH) {
            continue;
        }
        drag_camera(&s_camera, &s_camera_drag[p], &pointer, NT_BUTTON_LEFT);
        drag_camera(&s_light, &s_light_drag[p], &pointer, NT_BUTTON_RIGHT);
        if (pointer.buttons[NT_BUTTON_MIDDLE].is_pressed) {
            camera_screen_world(s_pan_pressed_world[p], pointer.x, pointer.y);
        }
        if (pointer.buttons[NT_BUTTON_MIDDLE].is_down && (pointer.dx != 0 || pointer.dy != 0)) {
            float current[3];
            float delta[3];
            camera_screen_world(current, pointer.x, pointer.y);
            /* Source pan adds the offset from the original press to the current orientation. */
            glm_vec3_sub(current, s_pan_pressed_world[p], delta);
            glm_vec3_add(movement, delta, movement);
        }
        wheel += pointer.wheel_dy;
    }
    float right[3];
    float up[3];
    float look[3];
    camera_basis(&s_camera, right, up, look);
    const float sideways = (key_motion(NT_KEY_D, dt) - key_motion(NT_KEY_A, dt)) * 5.0F;
    const float forward = (key_motion(NT_KEY_W, dt) - key_motion(NT_KEY_S, dt)) * 5.0F;
    const float vertical = (key_motion(NT_KEY_PAGE_UP, dt) - key_motion(NT_KEY_PAGE_DOWN, dt)) * 5.0F;
    for (uint32_t i = 0; i < 3; i++) {
        movement[i] += right[i] * sideways + up[i] * vertical + look[i] * forward;
        s_camera.eye[i] += movement[i];
        s_camera.aim[i] += movement[i];
    }
    const float yaw = (key_motion(NT_KEY_ARROW_RIGHT, dt) - key_motion(NT_KEY_ARROW_LEFT, dt)) * (s_eye_pivot ? -1.0F : 1.0F);
    const float pitch = (key_motion(NT_KEY_ARROW_DOWN, dt) - key_motion(NT_KEY_ARROW_UP, dt)) * (s_eye_pivot ? -1.0F : 1.0F);
    if (yaw != 0) {
        rotate_camera(&s_camera, &s_camera, &s_camera, (float[3]){0, 1, 0}, yaw * (float)AST_PI / 12.0F);
    }
    if (pitch != 0) {
        rotate_camera(&s_camera, &s_camera, &s_camera, (float[3]){1, 0, 0}, pitch * (float)AST_PI / 12.0F);
    }
    if (wheel != 0) {
        const float scroll = -wheel;
        const int direction = scroll > 0 ? 1 : -1;
        if (direction != s_zoom_direction || s_zoom_elapsed >= s_zoom_until) {
            s_zoom_elapsed = 0;
            s_zoom_factor = scroll > 0 ? 1.0F - (scroll / 3.0F) : 1.0F / (1.0F + scroll / 3.0F);
        }
        s_zoom_direction = direction;
        s_zoom_until = s_zoom_elapsed + 0.3F;
    }
    if (!s_paused && s_zoom_elapsed < s_zoom_until) {
        s_zoom_elapsed += dt;
        if (s_zoom_elapsed < s_zoom_until) {
            zoom_camera(1.0F - ((1.0F - s_zoom_factor) * dt * fmaxf(1.0F, s_zoom_elapsed / 0.3F)));
        }
    }
}
// #endregion

// #region scene submission
static uint32_t prepare_instances(void) {
    uint32_t base = 0;
    const complexity_t *complexity = &s_complexities[s_level];
    asteroid_instance_t *frame_instances = nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, (complexity->instances + 1U) * (uint32_t)sizeof(asteroid_instance_t), 4, &base);
    asteroid_instance_t *instances = s_optimized ? s_instance_staging : frame_instances;
    memset(s_lod_counts, 0, sizeof(s_lod_counts));
    s_triangles = 0;
    for (uint32_t i = 0; i < complexity->instances; i++) {
        const ast_reference_runtime_instance *asteroid = &s_asteroids[i];
        float world[16];
        float difference[3];
        asteroid_world_matrix(world, asteroid, s_elapsed);
        glm_vec3_sub(s_camera.eye, &world[12], difference);
        const uint32_t subdivision = asteroid_lod_index(asteroid->scale, glm_vec3_norm(difference), s_min_screen_size, AST_SUBDIVISIONS);
        const uint32_t subset_index = (subdivision * complexity->unique_meshes) + asteroid->mesh_index;
        const geometry_subset_t *subset = &s_subsets[subset_index];
        s_selected_subsets[i] = subset_index;
        s_lod_counts[subdivision]++;
        s_triangles += subset->index_count / 3U;
        instance_world_rows(&instances[i], world);
        memcpy(instances[i].deep, s_lod_colors ? s_lod_deep[subdivision] : asteroid->deep, sizeof(asteroid->deep));
        memcpy(instances[i].shallow, s_lod_colors ? s_lod_shallow[subdivision] : asteroid->shallow, sizeof(asteroid->shallow));
        instances[i].deep[3] = subset->depth_min;
        instances[i].shallow[3] = subset->depth_max;
        if (s_optimized) {
            s_draw_items[i] = (asteroid_draw_item_t){.sort_key = ((uint64_t)asteroid->texture_index << 32U) | subset_index, .source_index = i};
        }
    }
    if (s_optimized) {
        sort_asteroid_items(s_draw_items, complexity->instances, s_draw_scratch);
        for (uint32_t i = 0; i < complexity->instances; i++) {
            frame_instances[i] = instances[s_draw_items[i].source_index];
        }
    }
    float planet[16];
    matrix_rotation_axis(planet, (float[3]){0, 1, 0}, (float)(-0.1 * s_elapsed));
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 3; col++) {
            planet[(row * 4U) + col] *= 45.0F;
        }
    }
    memset(&frame_instances[complexity->instances], 0, sizeof(asteroid_instance_t));
    instance_world_rows(&frame_instances[complexity->instances], planet);
    return base;
}

static void draw_world(uint32_t base, const nt_frame_uniforms_t *globals) {
    nt_gfx_bind_uniform_block(0, globals, sizeof(*globals));
    nt_gfx_bind_pipeline(s_pipelines[0]);
    const float light[4] = {s_light.eye[0], s_light.eye[1], s_light.eye[2], 1};
    nt_gfx_set_uniform_vec4(s_light_name, light);
    const nt_buffer_t instance_buffer = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX);
    uint32_t previous_texture = UINT32_MAX;
    for (uint32_t i = 0; i < s_complexities[s_level].instances;) {
        const uint32_t source_index = s_optimized ? s_draw_items[i].source_index : i;
        const geometry_subset_t *subset = &s_subsets[s_selected_subsets[source_index]];
        const uint32_t texture = s_asteroids[source_index].texture_index;
        uint32_t end = i + 1U;
        if (s_optimized) {
            while (end < s_complexities[s_level].instances && s_draw_items[end].sort_key == s_draw_items[i].sort_key) {
                end++;
            }
        }
        const mesh_binding_t *binding = &s_rock_meshes[subset->chunk_index];
        nt_gfx_bind_vertex_input(binding->input);
        nt_gfx_bind_instance_buffer(instance_buffer, base + (i * (uint32_t)sizeof(asteroid_instance_t)));
        if (!s_optimized || texture != previous_texture) {
            nt_gfx_texture_binding_t textures[3];
            for (uint32_t layer = 0; layer < 3; layer++) {
                textures[layer] = (nt_gfx_texture_binding_t){.name = s_noise_names[layer], .texture = s_noise[texture][layer]};
            }
            nt_gfx_apply_texture_bindings(textures, 3);
            previous_texture = texture;
        }
        /* The reference path keeps count=1; only equal geometry and texture sets share a run. */
        nt_gfx_draw_indexed_instanced(subset->first_index, subset->index_count, subset->vertex_count, end - i);
        i = end;
    }
    nt_gfx_bind_pipeline(s_pipelines[1]);
    nt_gfx_set_uniform_vec4(s_light_name, light);
    nt_gfx_bind_vertex_input(s_planet_mesh.input);
    nt_gfx_bind_instance_buffer(instance_buffer, base + (s_complexities[s_level].instances * (uint32_t)sizeof(asteroid_instance_t)));
    const nt_gfx_texture_binding_t mars = {.name = s_diffuse_name, .texture = {.id = nt_resource_get(s_mars)}};
    nt_gfx_apply_texture_bindings(&mars, 1);
    const nt_gfx_mesh_info_t *planet = nt_gfx_get_mesh_info(s_planet_mesh.mesh);
    nt_gfx_draw_indexed_instanced(0, planet->index_count, planet->vertex_count, 1);
    nt_gfx_bind_pipeline(s_pipelines[2]);
    nt_gfx_bind_vertex_input(s_sky_mesh.input);
    nt_gfx_texture_binding_t sky[6];
    for (uint32_t i = 0; i < 6; i++) {
        sky[i] = (nt_gfx_texture_binding_t){.name = s_sky_names[i], .texture = {.id = nt_resource_get(s_sky_faces[i])}};
    }
    nt_gfx_apply_texture_bindings(sky, 6);
    const nt_gfx_mesh_info_t *cube = nt_gfx_get_mesh_info(s_sky_mesh.mesh);
    nt_gfx_draw_indexed(0, cube->index_count, cube->vertex_count);
}

static void draw_hud(void) {
    if (s_hide_hud || !nt_gfx_program_ready(s_programs[3].program)) {
        return;
    }
    nt_frame_uniforms_t globals = {0};
    mat4 projection;
    glm_ortho(0, (float)g_nt_window.fb_width, 0, (float)g_nt_window.fb_height, -1, 1, projection);
    memcpy(globals.view_proj, projection, sizeof(projection));
    nt_gfx_bind_uniform_block(0, &globals, sizeof(globals));
    nt_text_renderer_set_material(s_hud_material);
    const float size = glm_clamp((float)g_nt_window.fb_height / 52.0F, 11.0F, 18.0F);
    nt_text_style_t style = {.font = s_font, .size = size, .color = UINT32_MAX, .line_leading = 3, .shadow_dx = 0.07F, .shadow_dy = -0.07F, .shadow_color = NT_RGBA8(0, 0, 0, 255)};
    mat4 model = GLM_MAT4_IDENTITY_INIT;
    model[3][0] = 22;
    model[3][1] = (float)g_nt_window.fb_height - 30;
    char text[1600];
    char gpu[32];
    char memory[32];
    format_memory(memory, sizeof(memory));
    if (s_gpu_ms < 0) {
        (void)snprintf(gpu, sizeof(gpu), "unavailable");
    } else {
        (void)snprintf(gpu, sizeof(gpu), "%.2f ms", s_gpu_ms);
    }
    (void)snprintf(text, sizeof(text), "F1 Help    Methane Asteroids / Neotolis [%s]\n%.0f FPS   frame %.2f ms   CPU %.2f ms\nGPU scene %s   %u x %u   %u draws", s_render_mode_names[s_optimized],
                   s_frame_ms > 0 ? 1000.0 / s_frame_ms : 0, s_frame_ms, s_cpu_ms, gpu, g_nt_window.fb_width, g_nt_window.fb_height, nt_gfx_draw_calls(&g_nt_gfx.last_frame));
    nt_text_renderer_draw(&style, (const float *)model, text);
    const complexity_t *complexity = &s_complexities[s_level];
    (void)snprintf(text, sizeof(text),
                   "Asteroids simulation parameters:\n"
                   "  simulation complexity [0..9]: %u\n"
                   "  asteroid instances count: %u\n"
                   "  unique meshes count: %u\n"
                   "  mesh subdivisions count: 4\n"
                   "  unique textures count: %u x 3 layers\n"
                   "  asteroid textures size: 256 x 256\n"
                   "  texture array binding: 3 x sampler2D\n"
                   "  parallel rendering: unavailable\n"
                   "  asteroid animations: %s\n"
                   "  LOD 0/1/2/3: %u / %u / %u / %u\n"
                   "  process memory: %s",
                   s_level, complexity->instances, complexity->unique_meshes, complexity->textures, s_paused ? "OFF" : "ON", s_lod_counts[0], s_lod_counts[1], s_lod_counts[2], s_lod_counts[3],
                   memory);
    const nt_font_metrics_t metrics = nt_font_get_metrics(s_font);
    const float font_scale = metrics.units_per_em != 0 ? size / (float)metrics.units_per_em : 1.0F;
    const float line_advance = (metrics.line_height != 0 ? (float)metrics.line_height * font_scale : size) + style.line_leading;
    float text_width = 0;
    uint32_t newlines = 0;
    const char *line = text;
    for (const char *cursor = text;; cursor++) {
        if (*cursor != '\n' && *cursor != '\0') {
            continue;
        }
        const nt_text_size_t extent = nt_font_measure_n(s_font, line, (size_t)(cursor - line), size, 0);
        text_width = fmaxf(text_width, extent.width);
        if (*cursor == '\0') {
            break;
        }
        newlines++;
        line = cursor + 1;
    }
    model[3][0] = fmaxf(22, (float)g_nt_window.fb_width - 22.0F - text_width);
    model[3][1] = 22.0F - ((float)metrics.descent * font_scale) + ((float)newlines * line_advance);
    if (!s_hide_parameters) {
        nt_text_renderer_draw(&style, (const float *)model, text);
    }
    if (s_show_command_line) {
        model[3][0] = 22;
        model[3][1] = (float)g_nt_window.fb_height - (size * 7.0F);
        nt_text_renderer_draw(&style, (const float *)model,
                              "Command line:\n"
                              "  -c / --complexity 0..9\n"
                              "  --optimized 0|1: reference / grouped instances\n"
                              "  --frames N: native-only fixed-step diagnostic\n"
                              "  --paused 1: fixed initial scene\n"
                              "  --hide-hud 1: scene-only diagnostic capture\n"
                              "Prebuilt scene uses four source subdivisions.\n"
                              "Parallel command lists and descriptor arrays are unavailable.");
    }
    if (s_show_help) {
        model[3][0] = 22;
        model[3][1] = (float)g_nt_window.fb_height - size * 7.0F;
        nt_text_renderer_draw(&style, (const float *)model,
                              "0-9 / [ ]: complexity   L: LOD colors\n"
                              "; / ': decrease / increase mesh LOD\n"
                              "Ctrl+P: animations   P: parallel availability\n"
                              "Left drag: orbit   Middle drag: pan   Wheel: zoom\n"
                              "WASD / Page Up / Page Down: move   Arrows: rotate\n"
                              "Alt+R: reset view   Alt+P: change camera pivot\n"
                              "Right drag: light   Ctrl+L: reset light\n"
                              "F2: CLI help   F3: parameters   F4: HUD on/off\n"
                              "Ctrl+F: fullscreen   Ctrl+Q / Escape: close\n"
                              "Device selection / swapchain count: unavailable");
    }
    if (s_notice[0] != '\0') {
        model[3][0] = 22;
        model[3][1] = size * 1.5F;
        style.color = NT_RGBA8(255, 224, 96, 255);
        nt_text_renderer_draw(&style, (const float *)model, s_notice);
    }
}
// #endregion

static void frame(void) {
    const double begin = nt_time_now();
    const double interval = s_last_tick > 0 ? begin - s_last_tick : 0;
    s_last_tick = begin;
    s_frame_ms += (interval * 1000.0 - s_frame_ms) * 0.05;
    nt_window_poll();
    nt_example_frames_begin();
    nt_gfx_begin_frame();
    if (g_nt_gfx.context_restored) {
        restore_gpu();
    }
    if (!nt_example_frames_on()) {
        nt_input_poll();
    }
    if (s_use_ui) {
        nt_mem_scratch_reset();
    } else {
        handle_input(g_nt_app.dt);
    }
    if (s_requested_level != s_level) {
        load_complexity(s_requested_level, true);
        s_ui_staged_level = (int)s_level;
    }
    nt_resource_step();
    nt_font_step();
    const bool ready = prepare_gpu();
    if (ready && !s_ready) {
        s_notice[0] = '\0';
        nt_log_info("Methane Asteroids ready: complexity=%u instances=%u unique_meshes=%u textures=%u subdivisions=4 chunks=%u seed=1123; render=%s", s_level, s_complexities[s_level].instances,
                    s_complexities[s_level].unique_meshes, s_complexities[s_level].textures, s_chunk_count, s_render_mode_names[s_optimized]);
#ifdef NT_PLATFORM_WEB
        nt_platform_web_loading_complete();
#endif
    }
    if (s_use_ui) {
        build_ui();
        handle_input(g_nt_app.dt);
    }
    if (ready && s_ready && !s_paused) {
        s_elapsed += (double)g_nt_app.dt;
    }
    s_ready = ready;
    if (g_nt_window.fb_width == 0 || g_nt_window.fb_height == 0) {
        nt_gfx_end_frame();
        nt_example_frames_end(false);
        nt_window_swap_buffers();
        return;
    }
    nt_frame_uniforms_t globals = {0};
    const float aspect = g_nt_window.fb_height > 0 ? (float)g_nt_window.fb_width / (float)g_nt_window.fb_height : 1.0F;
    camera_matrices(&s_camera, aspect, globals.view, globals.proj, globals.view_proj);
    memcpy(globals.camera_pos, s_camera.eye, sizeof(s_camera.eye));
    globals.time[0] = (float)s_elapsed;
    const double update_begin = nt_time_now();
    const uint32_t base = ready ? prepare_instances() : 0;
    s_update_ms += ((nt_time_now() - update_begin) * 1000.0 - s_update_ms) * 0.05;
    const double render_begin = nt_time_now();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 1}, .clear_depth = 0, .discard_depth = true});
    nt_gfx_begin_segment("asteroids");
    if (ready) {
        draw_world(base, &globals);
    }
    nt_gfx_end_segment();
    if (s_use_ui) {
        draw_ui();
    } else {
        draw_hud();
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    s_render_ms += ((nt_time_now() - render_begin) * 1000.0 - s_render_ms) * 0.05;
    s_cpu_ms += ((nt_time_now() - begin) * 1000.0 - s_cpu_ms) * 0.05;
    nt_example_frames_end(ready);
    uint64_t gpu_ns = 0;
    if (nt_gfx_poll_segment_time_ns("asteroids", &gpu_ns)) {
        s_gpu_ms = (double)gpu_ns / 1000000.0;
    }
    if (!nt_gfx_is_gpu_timing_supported()) {
        s_gpu_ms = -1;
    }
    static double report_time;
    if (!nt_example_frames_on() && begin - report_time > 1.0) {
        report_time = begin;
        s_mem_used = nt_platform_memory_usage().used;
        s_memory_sampled = true;
        if (ready) {
            nt_log_info("Asteroids render=%s complexity=%u count=%u lod=%u/%u/%u/%u triangles=%" PRIu64 " draws=%u update_ms=%.3f render_submit_ms=%.3f cpu_frame_ms=%.3f gpu_ms=%.3f memory=%" PRIu64,
                        s_render_mode_names[s_optimized], s_level, s_complexities[s_level].instances, s_lod_counts[0], s_lod_counts[1], s_lod_counts[2], s_lod_counts[3], s_triangles,
                        nt_gfx_draw_calls(&g_nt_gfx.counters), s_update_ms, s_render_ms, s_cpu_ms, s_gpu_ms, s_mem_used);
        }
    }
    nt_window_swap_buffers();
}

/* Unlike the native-only frame harness, scene options also consume the
 * Emscripten Module.arguments passed to main. */
static bool scene_arg_u32(int argc, char **argv, const char *name, uint32_t maximum, uint32_t *value) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) != 0) {
            continue;
        }
        if (i + 1 == argc || argv[i + 1][0] == '\0') {
            return false;
        }
        uint32_t parsed = 0;
        for (const char *digit = argv[++i]; *digit != '\0'; digit++) {
            if (*digit < '0' || *digit > '9') {
                return false;
            }
            const uint32_t component = (uint32_t)(*digit - '0');
            if (parsed > maximum / 10U || (parsed == maximum / 10U && component > maximum % 10U)) {
                return false;
            }
            parsed = parsed * 10U + component;
        }
        *value = parsed;
    }
    return true;
}

int main(int argc, char **argv) {
    uint32_t hide_hud = 0;
    uint32_t paused = 0;
    uint32_t ui = 1;
    uint32_t optimized = 0;
    if (!scene_arg_u32(argc, argv, "--complexity", 9, &s_level) || !scene_arg_u32(argc, argv, "-c", 9, &s_level) || !scene_arg_u32(argc, argv, "--hide-hud", 1, &hide_hud) ||
        !scene_arg_u32(argc, argv, "--paused", 1, &paused) || !scene_arg_u32(argc, argv, "--ui", 1, &ui) || !scene_arg_u32(argc, argv, "--optimized", 1, &optimized)) {
        (void)fprintf(stderr, "Asteroids: --complexity / -c expects 0..9; --hide-hud, --paused, --ui and --optimized expect 0 or 1.\n");
        return 1;
    }
    s_requested_level = s_level;
    s_ui_staged_level = (int)s_level;
    s_use_ui = ui != 0;
    s_optimized = optimized != 0;
    s_hide_hud = hide_hud != 0;
    s_paused = paused != 0;
    s_camera = s_initial_camera;
    s_light = s_initial_light;
    if (nt_engine_init(&(nt_engine_config_t){.app_name = "asteroids", .version = 1}) != NT_OK) {
        return 1;
    }
    g_nt_window.width = 1280;
    g_nt_window.height = 800;
    g_nt_window.title = "Methane Asteroids - Neotolis C17 port";
    nt_window_init();
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_example_frames_init(argc, argv);
    nt_input_init();
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.max_textures = 192;
    gfx.stream_capacity = 16U * 1024U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_VERTEX] = (AST_MAX + 1U) * (uint32_t)sizeof(asteroid_instance_t) + 512U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_INDEX] = 64U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_UNIFORM] = 2U * 512U;
    gfx.global_blocks[0] = (nt_global_block_t){"Globals", 0};
    nt_gfx_init(&gfx);
    nt_http_init();
#ifndef NT_PLATFORM_WEB
    nt_fs_init();
#endif
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_resource_register_type(NT_ASSET_TEXTURE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_texture, .deactivate = nt_gfx_deactivate_texture});
    nt_resource_register_type(NT_ASSET_MESH, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_mesh, .deactivate = nt_gfx_deactivate_mesh});
    nt_resource_register_type(NT_ASSET_SHADER_CODE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_shader, .deactivate = nt_gfx_deactivate_shader});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 1});
    s_font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = 128, .measure_cache_size = 128});
    nt_font_add(s_font, nt_resource_request(nt_hash64_str("asteroids/font"), NT_ASSET_FONT));
    nt_material_init(&(nt_material_desc_t){.max_materials = s_use_ui ? 2 : 1});
    s_hud_material = nt_material_create(&(nt_material_create_desc_t){.blend = nt_blend_alpha_premultiplied(), .cull_mode = NT_CULL_NONE, .label = "asteroids_hud"});
    if (s_use_ui) {
        init_ui();
    }
    const char *vertices[] = {"scene", "planet", "sky"};
    const char *fragments[] = {"rock", "planet", "sky"};
    for (uint32_t i = 0; i < 3; i++) {
        char path[128];
        (void)snprintf(path, sizeof(path), "examples/asteroids/shaders/%s.vert", vertices[i]);
        s_programs[i].vs = nt_resource_request(nt_hash64_str(path), NT_ASSET_SHADER_CODE);
        (void)snprintf(path, sizeof(path), "examples/asteroids/shaders/%s.frag", fragments[i]);
        s_programs[i].fs = nt_resource_request(nt_hash64_str(path), NT_ASSET_SHADER_CODE);
    }
    s_programs[3].vs = nt_resource_request(nt_hash64_str("assets/shaders/slug_text.vert"), NT_ASSET_SHADER_CODE);
    s_programs[3].fs = nt_resource_request(nt_hash64_str("assets/shaders/slug_text.frag"), NT_ASSET_SHADER_CODE);
    for (uint32_t i = 0; i < AST_MAX_CHUNKS; i++) {
        char path[64];
        (void)snprintf(path, sizeof(path), "asteroids/rock_chunk_%u", i);
        s_rock_meshes[i].resource = nt_resource_request(nt_hash64_str(path), NT_ASSET_MESH);
    }
    s_planet_mesh.resource = nt_resource_request(nt_hash64_str("asteroids/planet"), NT_ASSET_MESH);
    s_sky_mesh.resource = nt_resource_request(nt_hash64_str("asteroids/sky_cube"), NT_ASSET_MESH);
    s_mars = nt_resource_request(nt_hash64_str("asteroids/mars"), NT_ASSET_TEXTURE);
    const char *sky_names[] = {"u_sky_px", "u_sky_nx", "u_sky_py", "u_sky_ny", "u_sky_pz", "u_sky_nz"};
    for (uint32_t i = 0; i < 6; i++) {
        char path[64];
        (void)snprintf(path, sizeof(path), "asteroids/sky/%u", i);
        s_sky_faces[i] = nt_resource_request(nt_hash64_str(path), NT_ASSET_TEXTURE);
        s_sky_names[i] = nt_hash32_str(sky_names[i]);
    }
    s_noise_names[0] = nt_hash32_str("u_noise_x");
    s_noise_names[1] = nt_hash32_str("u_noise_y");
    s_noise_names[2] = nt_hash32_str("u_noise_z");
    s_diffuse_name = nt_hash32_str("u_diffuse");
    s_light_name = nt_hash32_str("u_light_position");
    s_instances_blob = nt_resource_request(nt_hash64_str("asteroids/instances"), NT_ASSET_BLOB);
    s_geometry_blob = nt_resource_request(nt_hash64_str("asteroids/geometry"), NT_ASSET_BLOB);
    s_noise_blob = nt_resource_request(nt_hash64_str("asteroids/noise"), NT_ASSET_BLOB);
    s_core_pack = nt_hash32_str("asteroids_core");
    s_space_pack = nt_hash32_str("asteroids_space");
    s_level_pack = nt_hash32_str("asteroids_level");
    s_noise_pack = nt_hash32_str("asteroids_noise");
    if (s_use_ui) {
        load_pack(nt_hash32_str("asteroids_ui"), "asteroids_ui.ntpack");
    }
    load_pack(s_core_pack, "asteroids_core.ntpack");
    load_pack(s_space_pack, "asteroids_space.ntpack");
    load_complexity(s_level, false);
    nt_resource_set_activate_time_budget(0);
    const uint8_t deep[AST_SUBDIVISIONS][3] = {{0, 128, 0}, {0, 64, 128}, {96, 0, 128}, {128, 0, 0}};
    const uint8_t shallow[AST_SUBDIVISIONS][3] = {{0, 255, 0}, {0, 128, 255}, {196, 0, 255}, {255, 0, 0}};
    for (uint32_t i = 0; i < AST_SUBDIVISIONS; i++) {
        for (uint32_t component = 0; component < 3; component++) {
            s_lod_deep[i][component] = powf((float)deep[i][component] / 255.0F, 2.233333333F);
            s_lod_shallow[i][component] = powf((float)shallow[i][component] / 255.0F, 2.233333333F);
        }
    }
    /* Source animation follows wall time even when a benchmark frame exceeds 100 ms. */
    g_nt_app.max_dt = FLT_MAX;
    nt_app_run(frame);
#ifndef NT_PLATFORM_WEB
    for (uint32_t i = 0; i < AST_MAX_CHUNKS; i++) {
        drop_mesh_binding(&s_rock_meshes[i]);
    }
    drop_mesh_binding(&s_planet_mesh);
    drop_mesh_binding(&s_sky_mesh);
    drop_noise();
    if (s_use_ui) {
        nt_log_info("Asteroids UI: scratch peak=%zu bytes", nt_mem_scratch_high_water_mark());
        nt_ui_destroy_context(s_ui);
        nt_ui_module_shutdown();
        nt_sprite_renderer_shutdown();
        nt_material_destroy(s_ui_material);
        nt_program_ref_drop(&s_ui_program);
        nt_mem_scratch_shutdown();
        free(s_ui_arena);
    }
    nt_text_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_hud_material);
    for (uint32_t i = 0; i < 4; i++) {
        nt_program_ref_drop(&s_programs[i]);
    }
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
