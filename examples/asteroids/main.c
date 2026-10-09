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

Modified: C17 port of Methane Asteroids App/AsteroidsApp.cpp, Asteroid.cpp,
AsteroidsArray.cpp, Planet.cpp and MethaneKit Camera/ArcBallCamera. Asteroid
meshes, noise textures and the scene are generated at startup from a seed, as
in the source; the sky and Mars images come from packs. See CREDITS.md.
******************************************************************************/
#include "../shared/nt_example_frames.h"
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "clipboard/nt_clipboard.h"
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
#define AST_SUBDIVISIONS 4U
#define AST_MAX_TEXTURES 50U
/* The noise textures are tiles of one atlas; rock.frag hardcodes this grid. */
#define AST_NOISE_COLUMNS 8U
#define AST_NOISE_ROWS 8U
#define AST_MAX_SHAPES 1000U
/* Draw key = lod * AST_MAX_SHAPES + shape. */
#define AST_KEY_COUNT (AST_SUBDIVISIONS * AST_MAX_SHAPES)
/* Worst case of the stable instance layout: every key padded by a quarter, rounded up. */
#define AST_MAX_SLOTS (AST_MAX + (AST_MAX / 4U) + (2U * AST_KEY_COUNT))
#define AST_NOISE_SIZE 256U
#define AST_PI 3.14159265358979323846
#define AST_SEED 1123U

/* Four float lanes through clang/gcc vector extensions (wasm simd128, SSE2,
 * NEON) for the hot startup noise and per-frame trigonometry; other builds run
 * the same math in scalar code. */
#if (defined(__clang__) || defined(__GNUC__)) && (defined(__wasm_simd128__) || defined(__SSE2__) || defined(__ARM_NEON))
#define AST_SIMD 1
typedef float ast_f4 __attribute__((vector_size(16)));
typedef int32_t ast_i4 __attribute__((vector_size(16)));

static ast_f4 f4_splat(float v) { return (ast_f4){v, v, v, v}; }

static ast_f4 f4_select(ast_i4 mask, ast_f4 a, ast_f4 b) { return (ast_f4)(((ast_i4)a & mask) | ((ast_i4)b & ~mask)); }

/* sin and cos of four angles: Cody-Waite reduction by pi/2 in three parts, then
 * the Cephes minimax polynomials on [-pi/4, pi/4]; within ~1e-6 of sinf/cosf
 * for |x| < 1e5, the range the asteroid angles reach in a long session. */
static void sincos_x4(ast_f4 x, ast_f4 *sin_out, ast_f4 *cos_out) {
    const ast_i4 quadrant = __builtin_convertvector((x * f4_splat(0.636619772F)) + f4_select(x >= f4_splat(0.0F), f4_splat(0.5F), f4_splat(-0.5F)), ast_i4);
    const ast_f4 q = __builtin_convertvector(quadrant, ast_f4);
    const ast_f4 r = ((x - (q * f4_splat(1.5703125F))) - (q * f4_splat(4.837512969970703125e-4F))) - (q * f4_splat(7.549789948768648e-8F));
    const ast_f4 r2 = r * r;
    const ast_f4 sin_r = r + (r * r2 * (f4_splat(-1.6666654611e-1F) + (r2 * (f4_splat(8.3321608736e-3F) + (r2 * f4_splat(-1.9515295891e-4F))))));
    const ast_f4 cos_r = f4_splat(1.0F) - (f4_splat(0.5F) * r2) + (r2 * r2 * (f4_splat(4.166664568298827e-2F) + (r2 * (f4_splat(-1.388731625493765e-3F) + (r2 * f4_splat(2.443315711809948e-5F))))));
    const ast_i4 odd = (quadrant & 1) != 0;
    const ast_f4 s = f4_select(odd, cos_r, sin_r);
    const ast_f4 c = f4_select(odd, sin_r, cos_r);
    const ast_i4 sin_sign = (quadrant & 2) << 30;
    const ast_i4 cos_sign = ((quadrant + 1) & 2) << 30;
    *sin_out = (ast_f4)((ast_i4)s ^ sin_sign);
    *cos_out = (ast_f4)((ast_i4)c ^ cos_sign);
}

#endif

/* Asteroid instances get their own frame vertex stream, so their offsets stay
 * put while the UI and text change the general stream. */
enum { AST_STREAM_INSTANCES = NT_GFX_FRAME_VERTEX + 1 };

typedef struct {
    uint32_t instances, unique_meshes, textures;
    float scale_ratio;
} complexity_t;

static const complexity_t s_complexities[10] = {{1000, 35, 10, 0.6F},   {2000, 50, 10, 0.5F},    {3000, 75, 20, 0.45F},   {4000, 100, 20, 0.4F},  {5000, 200, 30, 0.33F},
                                                {10000, 300, 30, 0.3F}, {15000, 400, 40, 0.27F}, {20000, 500, 40, 0.23F}, {35000, 750, 50, 0.2F}, {50000, 1000, 50, 0.17F}};

typedef struct {
    float scale_translate[16];
    float spin_axis[3];
    float scale;
    float deep[3];
    float spin_speed;
    float shallow[3];
    float orbit_speed;
    float spin_angle;
    float orbit_angle;
    uint32_t mesh_index;
    uint32_t texture_index;
} asteroid_t;
typedef struct {
    int16_t position[4];
    int8_t normal[4];
} rock_vertex_t;
typedef struct {
    float world_rows[12];
    float deep[4];
    float shallow[4];
} asteroid_instance_t;
typedef struct {
    uint64_t sort_key;
    uint32_t source_index;
} asteroid_draw_item_t;

/* One generated mesh: immutable buffers plus the vertex input that binds them. */
typedef struct {
    nt_buffer_t vbo, ibo;
    nt_vertex_input_t input;
    uint32_t index_count, vertex_count;
} mesh_binding_t;
typedef struct {
    float eye[3], aim[3], up[3];
} camera_orientation_t;
typedef struct {
    camera_orientation_t orientation;
    float sphere[3];
    bool inside;
} camera_drag_t;

_Static_assert(sizeof(rock_vertex_t) == 12, "Rock vertex ABI");
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
static bool s_paused, s_lod_colors, s_hide_hud, s_ready, s_generated;
#ifndef NT_PLATFORM_WEB
static bool s_fullscreen;
#endif
static float s_min_screen_size = 0.06F;
static double s_elapsed, s_last_tick, s_frame_ms, s_cpu_ms, s_update_ms, s_render_ms, s_generate_ms, s_gpu_ms = -1.0;
static uint64_t s_mem_used, s_triangles;
static bool s_memory_sampled;
static uint32_t s_lod_counts[AST_SUBDIVISIONS];
static asteroid_t s_asteroids[AST_MAX];
static asteroid_instance_t s_instance_staging[AST_MAX];
static asteroid_draw_item_t s_draw_items[AST_MAX], s_draw_scratch[AST_MAX];
static float s_lod_deep[AST_SUBDIVISIONS][3], s_lod_shallow[AST_SUBDIVISIONS][3];
/* All variants of one subdivision are contiguous in one shared rock mesh. */
static uint32_t s_lod_first_index[AST_SUBDIVISIONS];
static mesh_binding_t s_rocks, s_planet_mesh, s_sky_mesh;
/* One vertex input per draw key over the shared rock buffers, as a game has one per mesh. */
static nt_vertex_input_t s_rock_inputs[AST_KEY_COUNT];
/* Each key owns a fixed slot range of the instance stream; see place_runs. */
static uint32_t s_key_slot[AST_KEY_COUNT], s_key_capacity[AST_KEY_COUNT], s_key_count[AST_KEY_COUNT];
static uint32_t s_slot_count;
static nt_resource_t s_mars, s_sky_faces[6];
static nt_texture_t s_noise;
/* rock, planet, sky, text, noise */
static nt_program_ref_t s_programs[5];
static nt_pipeline_t s_pipelines[3];
static nt_material_t s_text_material;
static nt_font_t s_font;
static nt_hash32_t s_noise_name, s_sky_names[6], s_diffuse_name, s_light_name;
static char s_notice[160];
static const char s_upstream_url[] = "https://github.com/MethanePowered/MethaneAsteroids";

static bool s_ui_panel_open, s_ui_built, s_ui_atlas_bound;
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

static void matrix_rotation_sincos(float out[16], const float axis[3], float s, float c) {
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

static void matrix_rotation_axis(float out[16], const float axis[3], float angle) { matrix_rotation_sincos(out, axis, sinf(angle), cosf(angle)); }

/* world = spin * scale_translate * orbit (row vectors), written as the three
 * transposed instance rows. scale_translate is a diagonal scale plus a ring
 * translation (r, h, 0), and the orbit turns about +Y, mixing only x and z. */
static float asteroid_spin_angle(const asteroid_t *asteroid, float elapsed_radians) { return asteroid->spin_angle + (asteroid->spin_speed * elapsed_radians); }

static float asteroid_orbit_angle(const asteroid_t *asteroid, float elapsed_radians) { return asteroid->orbit_angle - (asteroid->orbit_speed * elapsed_radians); }

/* sincos holds sin and cos of the spin angle, then of the orbit angle. */
static void asteroid_world_rows_sincos(float rows[12], const asteroid_t *asteroid, const float sincos[4]) {
    float spin[16];
    matrix_rotation_sincos(spin, asteroid->spin_axis, sincos[0], sincos[1]);
    const float s = sincos[2];
    const float c = sincos[3];
    const float *st = asteroid->scale_translate;
    for (uint32_t r = 0; r < 3; r++) {
        const float *spin_row = &spin[(size_t)r * 4U];
        const float x = spin_row[0] * st[0];
        const float z = spin_row[2] * st[10];
        rows[r] = (x * c) + (z * s);
        rows[4U + r] = spin_row[1] * st[5];
        rows[8U + r] = (z * c) - (x * s);
    }
    rows[3] = st[12] * c;
    rows[7] = st[13];
    rows[11] = -st[12] * s;
}

/* Spin and orbit sin/cos for up to four consecutive asteroids; trigonometry
 * dominates the per-frame update, so a full group runs as four SIMD lanes. */
static void asteroid_sincos4(float out[4][4], const asteroid_t *asteroids, uint32_t count, float elapsed_radians) {
#ifdef AST_SIMD
    if (count == 4) {
        ast_f4 spin;
        ast_f4 orbit;
        for (uint32_t k = 0; k < 4; k++) {
            spin[k] = asteroid_spin_angle(&asteroids[k], elapsed_radians);
            orbit[k] = asteroid_orbit_angle(&asteroids[k], elapsed_radians);
        }
        ast_f4 values[4];
        sincos_x4(spin, &values[0], &values[1]);
        sincos_x4(orbit, &values[2], &values[3]);
        for (uint32_t k = 0; k < 4; k++) {
            for (uint32_t v = 0; v < 4; v++) {
                out[k][v] = values[v][k];
            }
        }
        return;
    }
#endif
    for (uint32_t k = 0; k < count; k++) {
        const float spin = asteroid_spin_angle(&asteroids[k], elapsed_radians);
        const float orbit = asteroid_orbit_angle(&asteroids[k], elapsed_radians);
        out[k][0] = sinf(spin);
        out[k][1] = cosf(spin);
        out[k][2] = sinf(orbit);
        out[k][3] = cosf(orbit);
    }
}

/* The source picks round(log2(scale / sqrt(distance)) - log2(min_screen_size)),
 * clamped to the subdivisions. Level k >= 1 is reached when distance <=
 * (scale / min_screen_size)^2 * 2^(1 - 2k), so three compares replace the logs. */
static uint32_t asteroid_lod(float scale, float distance, float min_screen_size) {
    const float ratio = scale / min_screen_size;
    const float q = ratio * ratio;
    return (distance <= q * 0.5F ? 1U : 0U) + (distance <= q * 0.125F ? 1U : 0U) + (distance <= q * 0.03125F ? 1U : 0U);
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

// #region procedural content
/* PCG32: identical sequences on every platform, so a seed names one scene. */
typedef struct {
    uint64_t state;
} ast_rng_t;

static uint32_t rng_u32(ast_rng_t *rng) {
    const uint64_t old = rng->state;
    rng->state = (old * 6364136223846793005ULL) + 1442695040888963407ULL;
    const uint32_t xorshifted = (uint32_t)(((old >> 18U) ^ old) >> 27U);
    const uint32_t rot = (uint32_t)(old >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((32U - rot) & 31U));
}

static ast_rng_t rng_make(uint32_t seed, uint32_t stream) {
    ast_rng_t rng = {.state = ((uint64_t)seed << 32U) ^ ((uint64_t)stream * 0x9E3779B97F4A7C15ULL)};
    (void)rng_u32(&rng);
    rng.state += 0x853C49E6748FEA9BULL;
    (void)rng_u32(&rng);
    return rng;
}

static float rng_uniform(ast_rng_t *rng, float low, float high) { return low + ((high - low) * ((float)(rng_u32(rng) >> 8U) * (1.0F / 16777216.0F))); }

static uint32_t rng_index(ast_rng_t *rng, uint32_t count) { return (uint32_t)(((uint64_t)rng_u32(rng) * count) >> 32U); }

static float rng_normal(ast_rng_t *rng, float mean, float sigma) {
    const float u1 = ((float)(rng_u32(rng) >> 8U) + 1.0F) * (1.0F / 16777216.0F);
    const float u2 = rng_uniform(rng, 0.0F, 1.0F);
    return mean + (sigma * sqrtf(-2.0F * logf(u1)) * cosf(2.0F * (float)AST_PI * u2));
}

static uint8_t s_perm[512];

static void noise_init(void) {
    ast_rng_t rng = rng_make(AST_SEED, 0);
    for (uint32_t i = 0; i < 256; i++) {
        s_perm[i] = (uint8_t)i;
    }
    for (uint32_t i = 255; i > 0; i--) {
        const uint32_t j = rng_index(&rng, i + 1U);
        const uint8_t swap = s_perm[i];
        s_perm[i] = s_perm[j];
        s_perm[j] = swap;
    }
    memcpy(&s_perm[256], s_perm, 256);
}

static float simplex_corner(uint32_t hash, float x, float y, float z) {
    const float t = 0.5F - (x * x) - (y * y) - (z * z);
    if (t <= 0.0F) {
        return 0.0F;
    }
    const uint32_t h = hash & 15U;
    const float u = h < 8U ? x : y;
    float v = z;
    if (h < 4U) {
        v = y;
    } else if (h == 12U || h == 14U) {
        v = x;
    }
    const float gradient = ((h & 1U) != 0 ? -u : u) + ((h & 2U) != 0 ? -v : v);
    return t * t * t * t * gradient;
}

/* 3D simplex noise (Gustavson), roughly in [-1, 1]. */
static float simplex3(float x, float y, float z) {
    const float skew = (x + y + z) * (1.0F / 3.0F);
    const float fi = floorf(x + skew);
    const float fj = floorf(y + skew);
    const float fk = floorf(z + skew);
    const float unskew = (fi + fj + fk) * (1.0F / 6.0F);
    const float x0 = x - (fi - unskew);
    const float y0 = y - (fj - unskew);
    const float z0 = z - (fk - unskew);
    uint32_t i1 = 0;
    uint32_t j1 = 0;
    uint32_t k1 = 0;
    uint32_t i2 = 0;
    uint32_t j2 = 0;
    uint32_t k2 = 0;
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1;
            i2 = j2 = 1;
        } else if (x0 >= z0) {
            i1 = 1;
            i2 = k2 = 1;
        } else {
            k1 = 1;
            i2 = k2 = 1;
        }
    } else {
        if (y0 < z0) {
            k1 = 1;
            j2 = k2 = 1;
        } else if (x0 < z0) {
            j1 = 1;
            j2 = k2 = 1;
        } else {
            j1 = 1;
            i2 = j2 = 1;
        }
    }
    const uint32_t i = (uint32_t)(int32_t)fi & 255U;
    const uint32_t j = (uint32_t)(int32_t)fj & 255U;
    const uint32_t k = (uint32_t)(int32_t)fk & 255U;
    const float g = 1.0F / 6.0F;
    float sum = simplex_corner(s_perm[i + s_perm[j + s_perm[k]]], x0, y0, z0);
    sum += simplex_corner(s_perm[i + i1 + s_perm[j + j1 + s_perm[k + k1]]], x0 - (float)i1 + g, y0 - (float)j1 + g, z0 - (float)k1 + g);
    sum += simplex_corner(s_perm[i + i2 + s_perm[j + j2 + s_perm[k + k2]]], x0 - (float)i2 + (2.0F * g), y0 - (float)j2 + (2.0F * g), z0 - (float)k2 + (2.0F * g));
    sum += simplex_corner(s_perm[i + 1U + s_perm[j + 1U + s_perm[k + 1U]]], x0 - 1.0F + (3.0F * g), y0 - 1.0F + (3.0F * g), z0 - 1.0F + (3.0F * g));
    return 32.0F * sum;
}

static float fbm3(float x, float y, float z, float lacunarity, float gain) {
    float sum = 0.0F;
    float amplitude = 1.0F;
    for (uint32_t octave = 0; octave < 4; octave++) {
        sum += amplitude * simplex3(x, y, z);
        x *= lacunarity;
        y *= lacunarity;
        z *= lacunarity;
        amplitude *= gain;
    }
    return sum;
}

/* Midpoint subdivision only appends vertices, so every coarser subdivision is a
 * vertex prefix of the finest one: a rock's LODs share one noise evaluation. */
#define AST_FINE_VERTICES 642U
static const uint32_t s_lod_vertices[AST_SUBDIVISIONS] = {12, 42, 162, 642};
static const uint32_t s_lod_indices[AST_SUBDIVISIONS] = {60, 240, 960, 3840};
static float s_sphere[AST_FINE_VERTICES][3];
#ifdef AST_SIMD
static float s_sphere_soa[3][AST_FINE_VERTICES];
#endif
static uint16_t s_sphere_indices[AST_SUBDIVISIONS][3840];

static uint16_t sphere_midpoint(uint32_t *keys, uint16_t *values, uint32_t mask, uint32_t *vertex_count, uint16_t a, uint16_t b) {
    const uint32_t key = a < b ? ((uint32_t)a << 16U) | b : ((uint32_t)b << 16U) | a;
    uint32_t slot = (key * 2654435761U) & mask;
    while (keys[slot] != 0) {
        if (keys[slot] == key + 1U) {
            return values[slot];
        }
        slot = (slot + 1U) & mask;
    }
    const uint16_t index = (uint16_t)(*vertex_count)++;
    glm_vec3_add(s_sphere[a], s_sphere[b], s_sphere[index]);
    glm_vec3_normalize(s_sphere[index]);
    keys[slot] = key + 1U;
    values[slot] = index;
    return index;
}

static void sphere_init(void) {
    const float t = (1.0F + sqrtf(5.0F)) / 2.0F;
    const float corners[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    static const uint16_t faces[60] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7, 6, 7, 1, 8,
                                       3, 9,  4, 3, 4, 2, 3, 2, 6, 3, 6, 8,  3, 8,  9,  4, 9, 5, 2, 4,  11, 6,  2,  10, 8,  6, 7, 9, 8, 1};
    for (uint32_t i = 0; i < 12; i++) {
        glm_vec3_normalize_to((float *)corners[i], s_sphere[i]);
    }
    memcpy(s_sphere_indices[0], faces, sizeof(faces));
    uint32_t vertex_count = 12;
    uint32_t keys[1024];
    uint16_t values[1024];
    for (uint32_t level = 1; level < AST_SUBDIVISIONS; level++) {
        memset(keys, 0, sizeof(keys));
        const uint16_t *source = s_sphere_indices[level - 1U];
        uint16_t *target = s_sphere_indices[level];
        for (uint32_t i = 0; i < s_lod_indices[level - 1U]; i += 3) {
            const uint16_t a = source[i];
            const uint16_t b = source[i + 1U];
            const uint16_t c = source[i + 2U];
            const uint16_t ab = sphere_midpoint(keys, values, 1023U, &vertex_count, a, b);
            const uint16_t bc = sphere_midpoint(keys, values, 1023U, &vertex_count, b, c);
            const uint16_t ca = sphere_midpoint(keys, values, 1023U, &vertex_count, c, a);
            const uint16_t split[12] = {a, ab, ca, ab, b, bc, ab, bc, ca, ca, bc, c};
            memcpy(&target[(size_t)i * 4U], split, sizeof(split));
        }
        NT_ASSERT(vertex_count == s_lod_vertices[level]);
    }
#ifdef AST_SIMD
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        for (uint32_t axis = 0; axis < 3; axis++) {
            s_sphere_soa[axis][i] = s_sphere[i][axis];
        }
    }
#endif
}

/* Round half to even without a libm call; exact for |v| < 2^22. */
static int32_t round_even(float v) { return (int32_t)((v + 12582912.0F) - 12582912.0F); }

#ifdef AST_SIMD
/* simplex3 four points at a time; same operations as the scalar version, lane by lane. */
static ast_i4 perm4(ast_i4 index) { return (ast_i4){s_perm[index[0]], s_perm[index[1]], s_perm[index[2]], s_perm[index[3]]}; }

static ast_f4 simplex_corner4(ast_i4 hash, ast_f4 x, ast_f4 y, ast_f4 z) {
    ast_f4 t = f4_splat(0.5F) - (x * x) - (y * y) - (z * z);
    t = f4_select(t > f4_splat(0.0F), t, f4_splat(0.0F));
    const ast_i4 h = hash & 15;
    const ast_f4 u = f4_select(h < 8, x, y);
    const ast_f4 v = f4_select(h < 4, y, f4_select((h == 12) | (h == 14), x, z));
    const ast_f4 gradient = (ast_f4)((ast_i4)u ^ ((h & 1) << 31)) + (ast_f4)((ast_i4)v ^ ((h & 2) << 30));
    return t * t * t * t * gradient;
}

static ast_f4 simplex3_x4(ast_f4 x, ast_f4 y, ast_f4 z) {
    const ast_f4 skew = (x + y + z) * f4_splat(1.0F / 3.0F);
    const ast_f4 sx = x + skew;
    const ast_f4 sy = y + skew;
    const ast_f4 sz = z + skew;
    ast_i4 ci = __builtin_convertvector(sx, ast_i4);
    ast_i4 cj = __builtin_convertvector(sy, ast_i4);
    ast_i4 ck = __builtin_convertvector(sz, ast_i4);
    /* Truncation to floor: a true comparison mask is -1. */
    ci += sx < __builtin_convertvector(ci, ast_f4);
    cj += sy < __builtin_convertvector(cj, ast_f4);
    ck += sz < __builtin_convertvector(ck, ast_f4);
    const ast_f4 fi = __builtin_convertvector(ci, ast_f4);
    const ast_f4 fj = __builtin_convertvector(cj, ast_f4);
    const ast_f4 fk = __builtin_convertvector(ck, ast_f4);
    const ast_f4 unskew = (fi + fj + fk) * f4_splat(1.0F / 6.0F);
    const ast_f4 x0 = x - (fi - unskew);
    const ast_f4 y0 = y - (fj - unskew);
    const ast_f4 z0 = z - (fk - unskew);
    const ast_i4 xy = x0 >= y0;
    const ast_i4 xz = x0 >= z0;
    const ast_i4 yz = y0 >= z0;
    const ast_i4 i1 = -(xy & xz);
    const ast_i4 j1 = -(~xy & yz);
    const ast_i4 k1 = -(~xz & ~yz);
    const ast_i4 i2 = -(xy | xz);
    const ast_i4 j2 = -(~xy | yz);
    const ast_i4 k2 = -(~xz | ~yz);
    const ast_i4 i = ci & 255;
    const ast_i4 j = cj & 255;
    const ast_i4 k = ck & 255;
    const ast_f4 g1 = f4_splat(1.0F / 6.0F);
    const ast_f4 g2 = f4_splat(2.0F * (1.0F / 6.0F));
    const ast_f4 g3 = f4_splat(3.0F * (1.0F / 6.0F));
    const ast_f4 one = f4_splat(1.0F);
    ast_f4 sum = simplex_corner4(perm4(i + perm4(j + perm4(k))), x0, y0, z0);
    sum += simplex_corner4(perm4(i + i1 + perm4(j + j1 + perm4(k + k1))), x0 - __builtin_convertvector(i1, ast_f4) + g1, y0 - __builtin_convertvector(j1, ast_f4) + g1,
                           z0 - __builtin_convertvector(k1, ast_f4) + g1);
    sum += simplex_corner4(perm4(i + i2 + perm4(j + j2 + perm4(k + k2))), x0 - __builtin_convertvector(i2, ast_f4) + g2, y0 - __builtin_convertvector(j2, ast_f4) + g2,
                           z0 - __builtin_convertvector(k2, ast_f4) + g2);
    sum += simplex_corner4(perm4(i + 1 + perm4(j + 1 + perm4(k + 1))), x0 - one + g3, y0 - one + g3, z0 - one + g3);
    return f4_splat(32.0F) * sum;
}
#endif

/* Fractal noise of one shape at the finest LOD's vertices (radius-0.5 sphere). */
static void rock_noise(uint32_t variant, float noise[AST_FINE_VERTICES]) {
    ast_rng_t rng = rng_make(AST_SEED, 0x524F434BU + variant);
    const float gain = rng_normal(&rng, 0.95F, 0.04F);
    const float offset[3] = {rng_uniform(&rng, 0, 100), rng_uniform(&rng, 0, 100), rng_uniform(&rng, 0, 100)};
    uint32_t i = 0;
#ifdef AST_SIMD
    for (; i + 4U <= AST_FINE_VERTICES; i += 4U) {
        ast_f4 p[3];
        for (uint32_t axis = 0; axis < 3; axis++) {
            memcpy(&p[axis], &s_sphere_soa[axis][i], sizeof(p[axis]));
            p[axis] = (p[axis] * f4_splat(0.5F)) + f4_splat(offset[axis]);
        }
        ast_f4 sum = f4_splat(0.0F);
        float amplitude = 1.0F;
        for (uint32_t octave = 0; octave < 4; octave++) {
            sum += f4_splat(amplitude) * simplex3_x4(p[0], p[1], p[2]);
            p[0] *= f4_splat(2.0F);
            p[1] *= f4_splat(2.0F);
            p[2] *= f4_splat(2.0F);
            amplitude *= gain;
        }
        memcpy(&noise[i], &sum, sizeof(sum));
    }
#endif
    for (; i < AST_FINE_VERTICES; i++) {
        noise[i] = fbm3((s_sphere[i][0] * 0.5F) + offset[0], (s_sphere[i][1] * 0.5F) + offset[1], (s_sphere[i][2] * 0.5F) + offset[2], 2.0F, gain);
    }
}

/* Displaces a sphere of radius 0.5 to radii [0.525, 0.9], the range the rock
 * shader maps to its deep/shallow gradient, and writes every LOD of the shape:
 * positions are a shared prefix, normals are averaged over each LOD's faces. */
static void rock_build(uint32_t variant, rock_vertex_t *const lods[AST_SUBDIVISIONS]) {
    float noise[AST_FINE_VERTICES];
    rock_noise(variant, noise);
    float low = FLT_MAX;
    float high = -FLT_MAX;
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        low = fminf(low, noise[i]);
        high = fmaxf(high, noise[i]);
    }
    const float scale = high > low ? 0.5F / (high - low) : 0.0F;
    float positions[AST_FINE_VERTICES][3];
    int16_t quantized[AST_FINE_VERTICES][4];
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        const float radius = 0.5F * ((0.5F + ((noise[i] - low) * scale)) * 1.5F + 0.3F);
        for (uint32_t axis = 0; axis < 3; axis++) {
            positions[i][axis] = s_sphere[i][axis] * radius;
            quantized[i][axis] = (int16_t)round_even(positions[i][axis] * 32767.0F);
        }
        quantized[i][3] = 0;
    }
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        float normals[AST_FINE_VERTICES][3];
        memset(normals, 0, (size_t)s_lod_vertices[lod] * sizeof(normals[0]));
        const uint16_t *indices = s_sphere_indices[lod];
        for (uint32_t t = 0; t < s_lod_indices[lod]; t += 3) {
            float u[3];
            float v[3];
            float n[3];
            glm_vec3_sub(positions[indices[t + 1U]], positions[indices[t]], u);
            glm_vec3_sub(positions[indices[t + 2U]], positions[indices[t]], v);
            glm_vec3_cross(u, v, n);
            for (uint32_t corner = 0; corner < 3; corner++) {
                glm_vec3_add(normals[indices[t + corner]], n, normals[indices[t + corner]]);
            }
        }
        rock_vertex_t *out = lods[lod];
        for (uint32_t i = 0; i < s_lod_vertices[lod]; i++) {
            glm_vec3_normalize(normals[i]);
            memcpy(out[i].position, quantized[i], sizeof(out[i].position));
            for (uint32_t axis = 0; axis < 3; axis++) {
                out[i].normal[axis] = (int8_t)round_even(glm_clamp(normals[i][axis], -1.0F, 1.0F) * 127.0F);
            }
            out[i].normal[3] = 0;
        }
    }
}

static void destroy_mesh(mesh_binding_t *mesh) {
    nt_gfx_destroy_vertex_input(mesh->input);
    nt_gfx_destroy_buffer(mesh->vbo);
    nt_gfx_destroy_buffer(mesh->ibo);
    *mesh = (mesh_binding_t){0};
}

static const nt_vertex_layout_t s_instance_layout = {.attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
                                                               {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
                                                               {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
                                                               {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 48},
                                                               {.location = 8, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 64}},
                                                     .attr_count = 5,
                                                     .stride = sizeof(asteroid_instance_t)};

static void upload_mesh(mesh_binding_t *mesh, const void *vertices, uint32_t vertex_bytes, const uint32_t *indices, uint32_t index_count, const nt_vertex_layout_t *layout, bool instanced,
                        const char *label) {
    mesh->vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = vertices, .size = vertex_bytes, .label = label});
    mesh->ibo = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = indices, .size = index_count * (uint32_t)sizeof(uint32_t), .index_type = NT_INDEX_UINT32, .label = label});
    mesh->input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = *layout, .instance_layout = instanced ? s_instance_layout : (nt_vertex_layout_t){0}, .vertex_buffer = mesh->vbo, .index_buffer = mesh->ibo, .label = label});
    mesh->index_count = index_count;
    mesh->vertex_count = vertex_bytes / layout->stride;
}

/* Every variant's LODs share one noise field; the buffer is LOD-major so a
 * draw addresses (lod, variant) as s_lod_first_index[lod] + variant * indices.
 * Shapes depend only on their index, so a level uses the first unique_meshes. */
static void generate_rocks(void) {
    const uint32_t unique = AST_MAX_SHAPES;
    uint32_t vertex_total = 0;
    uint32_t index_total = 0;
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        vertex_total += unique * s_lod_vertices[lod];
        index_total += unique * s_lod_indices[lod];
    }
    rock_vertex_t *vertices = malloc((size_t)vertex_total * sizeof(rock_vertex_t));
    uint32_t *indices = malloc((size_t)index_total * sizeof(uint32_t));
    NT_ASSERT(vertices != NULL && indices != NULL);
    uint32_t lod_first_vertex[AST_SUBDIVISIONS];
    uint32_t vertex_cursor = 0;
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        lod_first_vertex[lod] = vertex_cursor;
        vertex_cursor += unique * s_lod_vertices[lod];
    }
    for (uint32_t variant = 0; variant < unique; variant++) {
        rock_vertex_t *lods[AST_SUBDIVISIONS];
        for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
            lods[lod] = &vertices[lod_first_vertex[lod] + (variant * s_lod_vertices[lod])];
        }
        rock_build(variant, lods);
    }
    vertex_cursor = 0;
    uint32_t index_cursor = 0;
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        s_lod_first_index[lod] = index_cursor;
        for (uint32_t variant = 0; variant < unique; variant++) {
            for (uint32_t i = 0; i < s_lod_indices[lod]; i++) {
                indices[index_cursor + i] = vertex_cursor + s_sphere_indices[lod][i];
            }
            vertex_cursor += s_lod_vertices[lod];
            index_cursor += s_lod_indices[lod];
        }
    }
    const nt_vertex_layout_t layout = {
        .attrs = {{.location = 0, .type = NT_VERTEX_INT16, .count = 3, .normalized = true, .offset = 0}, {.location = 1, .type = NT_VERTEX_INT8, .count = 3, .normalized = true, .offset = 8}},
        .attr_count = 2,
        .stride = sizeof(rock_vertex_t)};
    upload_mesh(&s_rocks, vertices, vertex_total * (uint32_t)sizeof(rock_vertex_t), indices, index_total, &layout, true, "asteroid_rocks");
    for (uint32_t key = 0; key < AST_KEY_COUNT; key++) {
        s_rock_inputs[key] = nt_gfx_make_vertex_input(
            &(nt_vertex_input_desc_t){.layout = layout, .instance_layout = s_instance_layout, .vertex_buffer = s_rocks.vbo, .index_buffer = s_rocks.ibo, .label = "asteroid_rock_key"});
    }
    free(indices);
    free(vertices);
}

typedef struct {
    float frequency, lacunarity, gain, offset_x, offset_y;
} noise_params_t;

static noise_params_t noise_params(uint32_t texture) {
    ast_rng_t rng = rng_make(AST_SEED, 0x54455854U + texture);
    noise_params_t params;
    params.gain = rng_uniform(&rng, 0.2F, 0.8F);
    params.lacunarity = rng_uniform(&rng, 1.5F, 2.5F);
    params.frequency = 1.0F / rng_uniform(&rng, 5.0F, 20.0F);
    params.offset_x = rng_uniform(&rng, 0.0F, 256.0F);
    params.offset_y = rng_uniform(&rng, 0.0F, 256.0F);
    return params;
}

/* Each texture is stretched to its own full byte range, as in the source, and
 * written to its tile of the R8 atlas (rows bottom-up, as GL uploads them). */
static void write_noise_tile(uint8_t *atlas, uint32_t texture, const float *values) {
    float low = FLT_MAX;
    float high = -FLT_MAX;
    for (uint32_t i = 0; i < AST_NOISE_SIZE * AST_NOISE_SIZE; i++) {
        low = fminf(low, values[i]);
        high = fmaxf(high, values[i]);
    }
    const float scale = high > low ? 255.0F / (high - low) : 0.0F;
    const uint32_t x0 = (texture % AST_NOISE_COLUMNS) * AST_NOISE_SIZE;
    const uint32_t y0 = (texture / AST_NOISE_COLUMNS) * AST_NOISE_SIZE;
    for (uint32_t y = 0; y < AST_NOISE_SIZE; y++) {
        uint8_t *row = atlas + (((size_t)(y0 + y) * ((size_t)AST_NOISE_COLUMNS * AST_NOISE_SIZE)) + x0);
        for (uint32_t x = 0; x < AST_NOISE_SIZE; x++) {
            row[x] = (uint8_t)fminf(255.0F, (values[(y * AST_NOISE_SIZE) + x] - low) * scale);
        }
    }
}

/* GPU noise: one pass renders every texture as a tile of an RGBA8 atlas (16-bit
 * value in R and G); after end_frame the tiles are read back, normalized and
 * uploaded as one R8 atlas with mipmaps, which a render target cannot carry.
 * Tiles are power-of-two aligned, so 2x2 mip reduction never mixes tiles. */
static nt_texture_t s_noise_atlas_color;
static nt_render_target_t s_noise_atlas;
static nt_vertex_input_t s_empty_input;

static void record_noise_atlas(nt_program_t program) {
    _Static_assert(AST_NOISE_COLUMNS * AST_NOISE_ROWS >= AST_MAX_TEXTURES, "noise atlas grid holds every texture");
    s_noise_atlas_color = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = (uint16_t)(AST_NOISE_COLUMNS * AST_NOISE_SIZE), .height = (uint16_t)(AST_NOISE_ROWS * AST_NOISE_SIZE), .format = NT_TEXTURE_FORMAT_RGBA8, .label = "asteroid_noise_render"});
    s_noise_atlas = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = s_noise_atlas_color, .label = "asteroid_noise_atlas"});
    if (!nt_gfx_vertex_input_valid(s_empty_input)) {
        s_empty_input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.label = "asteroid_noise"});
    }
    const nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program, .cull_mode = NT_CULL_NONE, .label = "asteroid_noise"});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_noise_atlas});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(s_empty_input);
    const nt_hash32_t params_name = nt_hash32_str("u_noise_params");
    const nt_hash32_t offset_name = nt_hash32_str("u_noise_offset");
    for (uint32_t t = 0; t < AST_MAX_TEXTURES; t++) {
        const noise_params_t params = noise_params(t);
        const uint32_t column = t % AST_NOISE_COLUMNS;
        const uint32_t row = t / AST_NOISE_COLUMNS;
        const float x = (float)(column * AST_NOISE_SIZE);
        const float y = (float)(row * AST_NOISE_SIZE);
        const float amplitude = 1.0F + params.gain + (params.gain * params.gain) + (params.gain * params.gain * params.gain);
        const float uniforms[2][4] = {{params.frequency, params.lacunarity, params.gain, 1.0F / amplitude}, {params.offset_x, params.offset_y, x, y}};
        nt_gfx_set_viewport((int)x, (int)y, (int)AST_NOISE_SIZE, (int)AST_NOISE_SIZE);
        nt_gfx_set_uniform_vec4(params_name, uniforms[0]);
        nt_gfx_set_uniform_vec4(offset_name, uniforms[1]);
        nt_gfx_draw(0, 3);
    }
    nt_gfx_end_pass();
}

/* Runs between end_frame and the next begin_frame, where readback is allowed.
 * One readback of the whole atlas: on WebGL each readPixels waits for the GPU. */
static void collect_noise_atlas(void) {
    const uint32_t width = AST_NOISE_COLUMNS * AST_NOISE_SIZE;
    const uint32_t height = AST_NOISE_ROWS * AST_NOISE_SIZE;
    const uint32_t bytes = width * height * 4U;
    uint8_t *rgba = malloc(bytes);
    uint8_t *atlas = calloc((size_t)width * height, 1);
    NT_ASSERT(rgba != NULL && atlas != NULL);
    if (nt_gfx_read_pixels(s_noise_atlas, 0, 0, (int)width, (int)height, rgba, bytes)) {
        static float values[AST_NOISE_SIZE * AST_NOISE_SIZE];
        for (uint32_t t = 0; t < AST_MAX_TEXTURES; t++) {
            /* The readback is top-row first: GL tile row r starts at buffer row height - (r + 1) * size. */
            const uint32_t x0 = (t % AST_NOISE_COLUMNS) * AST_NOISE_SIZE;
            const uint32_t y0 = height - (((t / AST_NOISE_COLUMNS) + 1U) * AST_NOISE_SIZE);
            for (uint32_t y = 0; y < AST_NOISE_SIZE; y++) {
                const uint8_t *row = rgba + ((((size_t)(y0 + y) * width) + x0) * 4U);
                for (uint32_t x = 0; x < AST_NOISE_SIZE; x++) {
                    values[(y * AST_NOISE_SIZE) + x] = (float)(((uint32_t)row[(size_t)x * 4U] << 8U) | row[((size_t)x * 4U) + 1U]);
                }
            }
            write_noise_tile(atlas, t, values);
        }
        s_noise = nt_gfx_make_texture(&(nt_texture_desc_t){.width = (uint16_t)width,
                                                           .height = (uint16_t)height,
                                                           .data = atlas,
                                                           .format = NT_TEXTURE_FORMAT_R8,
                                                           .min_filter = NT_FILTER_LINEAR_MIPMAP_NEAREST,
                                                           .mag_filter = NT_FILTER_LINEAR,
                                                           .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
                                                           .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
                                                           .gen_mipmaps = true,
                                                           .label = "asteroid_noise"});
    }
    free(atlas);
    free(rgba);
    nt_gfx_destroy_render_target(s_noise_atlas);
    nt_gfx_destroy_texture(s_noise_atlas_color);
    s_noise_atlas = (nt_render_target_t){0};
    s_noise_atlas_color = (nt_texture_t){0};
}

static void srgb_palette_color(float out[3], const uint8_t rgb[3]) {
    for (uint32_t c = 0; c < 3; c++) {
        out[c] = powf((float)rgb[c] / 255.0F, 2.233333333F);
    }
}

/* The source scene distributions: a ring of radius 195 around the planet. */
static void generate_instances(const complexity_t *complexity) {
    static const uint8_t deep_rock[6][3] = {{55, 49, 40}, {58, 38, 14}, {98, 101, 104}, {172, 158, 122}, {88, 88, 88}, {148, 108, 102}};
    static const uint8_t shallow_rock[6][3] = {{140, 109, 61}, {172, 154, 58}, {204, 177, 119}, {204, 164, 136}, {130, 117, 98}, {160, 145, 114}};
    static const uint8_t deep_ice[6][3] = {{22, 51, 59}, {45, 72, 93}, {14, 25, 27}, {68, 103, 129}, {29, 59, 59}, {59, 92, 118}};
    static const uint8_t shallow_ice[6][3] = {{144, 163, 188}, {133, 179, 189}, {74, 135, 178}, {69, 143, 177}, {104, 168, 185}, {140, 170, 186}};
    const float scene_scale = 15.0F;
    const float orbit_radius = 13.0F * scene_scale;
    const float disc_radius = 4.0F * scene_scale;
    ast_rng_t rng = rng_make(AST_SEED, 0x494E5354U);
    for (uint32_t i = 0; i < complexity->instances; i++) {
        asteroid_t *asteroid = &s_asteroids[i];
        memset(asteroid, 0, sizeof(*asteroid));
        asteroid->mesh_index = rng_index(&rng, complexity->unique_meshes);
        asteroid->texture_index = rng_index(&rng, complexity->textures);
        const float radius = rng_normal(&rng, orbit_radius, 0.6F * disc_radius);
        const float height = rng_normal(&rng, 0.0F, 0.4F * disc_radius);
        const float ratio = rng_uniform(&rng, complexity->scale_ratio / 10.0F, complexity->scale_ratio);
        for (uint32_t axis = 0; axis < 3; axis++) {
            asteroid->scale_translate[(size_t)axis * 5U] = rng_uniform(&rng, 0.8F, 1.2F) * ratio * scene_scale;
        }
        asteroid->scale_translate[12] = radius;
        asteroid->scale_translate[13] = height;
        asteroid->scale_translate[15] = 1.0F;
        asteroid->scale = ratio * scene_scale;
        const bool ice = rng_normal(&rng, 0.0F, 1.0F) <= 1.0F;
        srgb_palette_color(asteroid->deep, (ice ? deep_ice : deep_rock)[rng_index(&rng, 6)]);
        srgb_palette_color(asteroid->shallow, (ice ? shallow_ice : shallow_rock)[rng_index(&rng, 6)]);
        float axis[3] = {0};
        while (glm_vec3_norm2(axis) <= FLT_MIN) {
            axis[0] = rng_normal(&rng, 0.0F, 1.0F);
            axis[1] = rng_normal(&rng, 0.0F, 1.0F);
            axis[2] = rng_normal(&rng, 0.0F, 1.0F);
        }
        glm_vec3_normalize_to(axis, asteroid->spin_axis);
        asteroid->orbit_speed = rng_uniform(&rng, 1.5F, 5.0F) / (asteroid->scale * radius);
        asteroid->spin_speed = rng_uniform(&rng, -1.7F, 1.7F) / asteroid->scale;
        asteroid->spin_angle = (float)AST_PI * rng_normal(&rng, 0.0F, 1.0F);
        asteroid->orbit_angle = (float)AST_PI * rng_normal(&rng, 0.0F, 1.0F) * 2.0F;
    }
}

static void generate_environment(void) {
    enum { LAT = 32, LON = 33 };
    static float planet[LAT * LON][8];
    static uint32_t planet_indices[(LAT - 1) * (LON - 1) * 6];
    for (uint32_t lat = 0; lat < LAT; lat++) {
        const float lat_angle = (float)AST_PI * (float)lat / (float)(LAT - 1);
        for (uint32_t lon = 0; lon < LON; lon++) {
            const float lon_angle = 2.0F * (float)AST_PI * (float)lon / (float)(LON - 1);
            float *v = planet[(lat * LON) + lon];
            v[0] = v[3] = sinf(lat_angle) * cosf(lon_angle);
            v[1] = v[4] = cosf(lat_angle);
            v[2] = v[5] = sinf(lat_angle) * sinf(lon_angle);
            v[6] = (float)lon / (float)(LON - 1);
            v[7] = (float)lat / (float)(LAT + 1);
        }
    }
    uint32_t cursor = 0;
    for (uint32_t lat = 0; lat + 1U < LAT; lat++) {
        for (uint32_t lon = 0; lon + 1U < LON; lon++) {
            const uint32_t a = (lat * LON) + lon;
            const uint32_t b = a + LON;
            const uint32_t quad[6] = {a, a + 1U, b, b, a + 1U, b + 1U};
            memcpy(&planet_indices[cursor], quad, sizeof(quad));
            cursor += 6;
        }
    }
    const nt_vertex_layout_t planet_layout = {.attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                        {.location = 1, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 12},
                                                        {.location = 2, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 24}},
                                              .attr_count = 3,
                                              .stride = 32};
    upload_mesh(&s_planet_mesh, planet, sizeof(planet), planet_indices, cursor, &planet_layout, true, "asteroids_planet");
    static const float cube[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    static const uint32_t cube_indices[36] = {0, 1, 2, 0, 2, 3, 5, 4, 7, 5, 7, 6, 4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2, 3, 2, 6, 3, 6, 7, 4, 5, 1, 4, 1, 0};
    const nt_vertex_layout_t sky_layout = {.attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0}}, .attr_count = 1, .stride = 12};
    upload_mesh(&s_sky_mesh, cube, sizeof(cube), cube_indices, 36, &sky_layout, false, "asteroids_sky");
}

static void drop_generated(void) {
    /* Destroying the shared rock buffers destroys every key's vertex input. */
    destroy_mesh(&s_rocks);
    memset(s_rock_inputs, 0, sizeof(s_rock_inputs));
    destroy_mesh(&s_planet_mesh);
    destroy_mesh(&s_sky_mesh);
    nt_gfx_destroy_texture(s_noise);
    s_noise = (nt_texture_t){0};
    s_generated = false;
}

/* Blocking by design, once: every shape and texture of the largest level, so a
 * level change only regenerates instances. The time is reported as the startup cost. */
static double s_generate_begin;
static bool s_noise_pending;

/* Meshes are built now; the noise textures are rendered by this frame and
 * collected after its end_frame. */
static void start_content(nt_program_t noise_program) {
    s_generate_begin = nt_time_now();
    generate_environment();
    generate_rocks();
    record_noise_atlas(noise_program);
    s_noise_pending = true;
}

static void select_level(uint32_t level) {
    generate_instances(&s_complexities[level]);
    s_slot_count = 0;
    s_level = level;
    s_ui_staged_level = (int)level;
}
// #endregion

// #region pack and GPU lifetimes
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

static void restore_gpu(void) {
    s_ready = false;
    s_gpu_ms = -1;
    drop_generated();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_program_ref_drop(&s_ui_program);
    s_ui_atlas_bound = false;
    init_ui_styles();
    for (uint32_t i = 0; i < 5; i++) {
        nt_program_ref_drop(&s_programs[i]);
    }
    memset(s_pipelines, 0, sizeof(s_pipelines));
    nt_resource_invalidate(NT_ASSET_TEXTURE);
    nt_resource_invalidate(NT_ASSET_SHADER_CODE);
}

static bool prepare_gpu(void) {
    bool ready = !g_nt_gfx.context_lost && s_generated;
    for (uint32_t i = 0; i < 5; i++) {
        if (nt_program_ref_update(&s_programs[i]) && i == 3) {
            nt_material_set_program(s_text_material, s_programs[i].program);
        }
        ready = nt_gfx_program_ready(s_programs[i].program) && ready;
    }
    ready = nt_resource_is_ready(s_mars) && ready;
    for (uint32_t i = 0; i < 6; i++) {
        ready = nt_resource_is_ready(s_sky_faces[i]) && ready;
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (!nt_gfx_pipeline_valid(s_pipelines[i]) && nt_gfx_program_ready(s_programs[i].program)) {
            /* Rock and planet fronts are clockwise after the LH projection; the camera sits inside the sky. */
            s_pipelines[i] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = s_programs[i].program,
                                                                        .depth_test = true,
                                                                        .depth_write = i != 2,
                                                                        .depth_func = NT_DEPTH_GEQUAL,
                                                                        .cull_mode = i == 2 ? NT_CULL_NONE : NT_CULL_FRONT,
                                                                        .label = "asteroids_scene"});
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
    nt_ui_set_text_material(s_ui, s_text_material, 0);
    nt_ui_set_font(s_ui, 0, s_font);
    init_ui_styles();
    nt_log_info("Asteroids UI: context=%zu bytes, scratch=131072 bytes, elements=384", s_ui_arena_size);
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
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "AUTHORS & SOURCE", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Methane Asteroids\nOriginal: Evgeny Gorodetskiy\nNeotolis C17 port", &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Galaxy panorama: ESO/S. Brunier, CC BY 4.0; adapted as a cubemap in Methane Asteroids.", &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "https://github.com/\nMethanePowered/MethaneAsteroids", &s_ui_caption);
    if (ui_action("asteroids.copy_upstream", "Copy upstream link", false, nt_clipboard_available(), 0)) {
        nt_clipboard_set_text(s_upstream_url);
#ifdef NT_PLATFORM_WEB
        (void)snprintf(s_notice, sizeof(s_notice), "Copy requested. Browser clipboard permissions may block it.");
#else
        (void)snprintf(s_notice, sizeof(s_notice), "%s", strcmp(nt_clipboard_get_text(), s_upstream_url) == 0 ? "Repository URL copied." : "Clipboard unavailable. Use the URL above.");
#endif
    }
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
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), s_ready ? "ACTIVE WORKLOAD" : "GENERATING WORKLOAD", &s_ui_section);
    (void)snprintf(value, sizeof(value), "%.1f ms", s_generate_ms);
    ui_metric("Generation (startup)", s_generated ? value : "pending");
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
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "Indexed instances grouped by LOD and shape.\nNo frustum culling. Noise: 50 R8 tiles in one atlas.", &s_ui_caption);
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
                "         Hide all overlays\nCtrl+F      Fullscreen (native)\nWeb         Fullscreen button\nCtrl+Q      Quit (native)\nEscape      Close modal / quit",
                &s_ui_body);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "COMMAND LINE", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "--complexity 0..9 (or -c)\n--paused 0|1\n--hide-hud 0|1\n--frames N (native diagnostics)", &s_ui_caption);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2), "CREDITS", &s_ui_section);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(2),
                "Methane Asteroids\nEvgeny Gorodetskiy / Apache 2.0\n\nMars: Solar System Scope / INOVE\nCC BY 4.0. Original JPG unchanged.\n\nGalaxy panorama: ESO/S. Brunier, CC BY 4.0; "
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
    bool over_ui = false;
    if (s_ui_built) {
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
    const bool scene_blocked = (s_ui_keyboard_blocked || s_ui_escape_consumed || g_nt_window.fb_width == 0 || g_nt_window.fb_height == 0);
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
#ifndef NT_PLATFORM_WEB
    if (ctrl && nt_input_key_is_pressed(NT_KEY_F)) {
        s_fullscreen = !s_fullscreen;
        nt_window_set_fullscreen(s_fullscreen);
    }
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
    touch_camera(scene_pointers);
    for (uint32_t p = 0; p < NT_INPUT_MAX_POINTERS; p++) {
        const nt_pointer_t pointer = scene_pointers[p];
        if (pointer.type == NT_POINTER_TOUCH) {
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
/* Each draw key owns a fixed slot range of the instance stream with a quarter
 * spare, so a key whose count changes (an asteroid crossing an LOD threshold)
 * does not move the other runs. Each key's vertex input then keeps its instance
 * pointers frame to frame, and WebGL skips the per-draw attribute re-pointing.
 * The layout is rebuilt only when a key outgrows its range. */
static void place_runs(uint32_t count) {
    memset(s_key_count, 0, sizeof(s_key_count));
    for (uint32_t i = 0; i < count; i++) {
        s_key_count[s_draw_items[i].sort_key]++;
    }
    bool fits = s_slot_count != 0;
    for (uint32_t key = 0; key < AST_KEY_COUNT && fits; key++) {
        fits = s_key_count[key] <= s_key_capacity[key];
    }
    if (fits) {
        return;
    }
    uint32_t slot = 0;
    for (uint32_t key = 0; key < AST_KEY_COUNT; key++) {
        const uint32_t n = s_key_count[key];
        s_key_slot[key] = slot;
        s_key_capacity[key] = n + ((n + 3U) / 4U) + 1U;
        slot += s_key_capacity[key];
    }
    s_slot_count = slot;
}

static uint32_t prepare_instances(void) {
    uint32_t base = 0;
    const complexity_t *complexity = &s_complexities[s_level];
    asteroid_instance_t *instances = s_instance_staging;
    memset(s_lod_counts, 0, sizeof(s_lod_counts));
    s_triangles = 0;
    const float elapsed_radians = (float)(AST_PI * s_elapsed);
    float sincos[4][4];
    for (uint32_t i = 0; i < complexity->instances; i++) {
        const asteroid_t *asteroid = &s_asteroids[i];
        if ((i & 3U) == 0) {
            asteroid_sincos4(sincos, asteroid, complexity->instances - i < 4U ? complexity->instances - i : 4U, elapsed_radians);
        }
        asteroid_world_rows_sincos(instances[i].world_rows, asteroid, sincos[i & 3U]);
        const float difference[3] = {s_camera.eye[0] - instances[i].world_rows[3], s_camera.eye[1] - instances[i].world_rows[7], s_camera.eye[2] - instances[i].world_rows[11]};
        const uint32_t lod = asteroid_lod(asteroid->scale, glm_vec3_norm((float *)difference), s_min_screen_size);
        s_lod_counts[lod]++;
        s_triangles += s_lod_indices[lod] / 3U;
        memcpy(instances[i].deep, s_lod_colors ? s_lod_deep[lod] : asteroid->deep, sizeof(asteroid->deep));
        memcpy(instances[i].shallow, s_lod_colors ? s_lod_shallow[lod] : asteroid->shallow, sizeof(asteroid->shallow));
        instances[i].deep[3] = (float)asteroid->texture_index;
        instances[i].shallow[3] = 0;
        /* The texture is an atlas tile chosen per instance, so only the mesh splits draws. */
        s_draw_items[i] = (asteroid_draw_item_t){.sort_key = (lod * AST_MAX_SHAPES) + asteroid->mesh_index, .source_index = i};
    }
    sort_asteroid_items(s_draw_items, complexity->instances, s_draw_scratch);
    place_runs(complexity->instances);
    asteroid_instance_t *frame_instances = nt_gfx_frame_alloc(AST_STREAM_INSTANCES, (s_slot_count + 1U) * (uint32_t)sizeof(asteroid_instance_t), 4, &base);
    for (uint32_t i = 0; i < complexity->instances;) {
        const uint64_t key = s_draw_items[i].sort_key;
        asteroid_instance_t *run = &frame_instances[s_key_slot[key]];
        for (uint32_t j = 0; i < complexity->instances && s_draw_items[i].sort_key == key; i++, j++) {
            run[j] = instances[s_draw_items[i].source_index];
        }
    }
    float planet[16];
    matrix_rotation_axis(planet, (float[3]){0, 1, 0}, (float)(-0.1 * s_elapsed));
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 3; col++) {
            planet[(row * 4U) + col] *= 45.0F;
        }
    }
    memset(&frame_instances[s_slot_count], 0, sizeof(asteroid_instance_t));
    instance_world_rows(&frame_instances[s_slot_count], planet);
    return base;
}

static void draw_world(uint32_t base, const nt_frame_uniforms_t *globals) {
    const complexity_t *complexity = &s_complexities[s_level];
    nt_gfx_bind_uniform_block(0, globals, sizeof(*globals));
    nt_gfx_bind_pipeline(s_pipelines[0]);
    const float light[4] = {s_light.eye[0], s_light.eye[1], s_light.eye[2], 1};
    nt_gfx_set_uniform_vec4(s_light_name, light);
    const nt_gfx_texture_binding_t noise = {.name = s_noise_name, .texture = s_noise};
    nt_gfx_apply_texture_bindings(&noise, 1);
    for (uint32_t i = 0; i < complexity->instances;) {
        const uint64_t key = s_draw_items[i].sort_key;
        const uint32_t subset = (uint32_t)key;
        const uint32_t lod = subset / AST_MAX_SHAPES;
        const uint32_t mesh = subset % AST_MAX_SHAPES;
        uint32_t end = i + 1U;
        while (end < complexity->instances && s_draw_items[end].sort_key == key) {
            end++;
        }
        nt_gfx_bind_vertex_input_instanced(s_rock_inputs[subset], AST_STREAM_INSTANCES, base + (s_key_slot[subset] * (uint32_t)sizeof(asteroid_instance_t)));
        nt_gfx_draw_indexed_instanced(s_lod_first_index[lod] + (mesh * s_lod_indices[lod]), s_lod_indices[lod], s_lod_vertices[lod], end - i);
        i = end;
    }
    nt_gfx_bind_pipeline(s_pipelines[1]);
    nt_gfx_set_uniform_vec4(s_light_name, light);
    nt_gfx_bind_vertex_input_instanced(s_planet_mesh.input, AST_STREAM_INSTANCES, base + (s_slot_count * (uint32_t)sizeof(asteroid_instance_t)));
    const nt_gfx_texture_binding_t mars = {.name = s_diffuse_name, .texture = {.id = nt_resource_get(s_mars)}};
    nt_gfx_apply_texture_bindings(&mars, 1);
    nt_gfx_draw_indexed_instanced(0, s_planet_mesh.index_count, s_planet_mesh.vertex_count, 1);
    nt_gfx_bind_pipeline(s_pipelines[2]);
    nt_gfx_bind_vertex_input(s_sky_mesh.input);
    nt_gfx_texture_binding_t sky[6];
    for (uint32_t i = 0; i < 6; i++) {
        sky[i] = (nt_gfx_texture_binding_t){.name = s_sky_names[i], .texture = {.id = nt_resource_get(s_sky_faces[i])}};
    }
    nt_gfx_apply_texture_bindings(sky, 6);
    nt_gfx_draw_indexed(0, s_sky_mesh.index_count, s_sky_mesh.vertex_count);
}
// #endregion

static void collect_pending_noise(void) {
    if (!s_noise_pending) {
        return;
    }
    collect_noise_atlas();
    s_noise_pending = false;
    s_generated = true;
    s_generate_ms = (nt_time_now() - s_generate_begin) * 1000.0;
    nt_log_info("Asteroids content generated in %.1f ms: %u shapes x %u LODs, %u textures", s_generate_ms, AST_MAX_SHAPES, AST_SUBDIVISIONS, AST_MAX_TEXTURES);
    select_level(s_requested_level);
}

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
    nt_mem_scratch_reset();
    /* The notice frame is presented before the blocking generation starts. */
    if (!s_generated && !g_nt_gfx.context_lost) {
        if (s_notice[0] == '\0') {
            (void)snprintf(s_notice, sizeof(s_notice), "Generating asteroids...");
        } else if (!s_noise_pending && nt_gfx_program_ready(s_programs[4].program)) {
            start_content(s_programs[4].program);
        }
    } else if (s_requested_level != s_level) {
        select_level(s_requested_level);
    }
    nt_resource_step();
    nt_font_step();
    const bool ready = prepare_gpu();
    if (ready && !s_ready) {
        s_notice[0] = '\0';
        nt_log_info("Methane Asteroids ready: complexity=%u instances=%u unique_meshes=%u textures=%u subdivisions=4 seed=%u", s_level, s_complexities[s_level].instances,
                    s_complexities[s_level].unique_meshes, s_complexities[s_level].textures, AST_SEED);
#ifdef NT_PLATFORM_WEB
        nt_platform_web_loading_complete();
#endif
    }
    build_ui();
    handle_input(g_nt_app.dt);
    if (ready && s_ready && !s_paused) {
        s_elapsed += (double)g_nt_app.dt;
    }
    s_ready = ready;
    if (g_nt_window.fb_width == 0 || g_nt_window.fb_height == 0) {
        nt_gfx_end_frame();
        collect_pending_noise();
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
    draw_ui();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    collect_pending_noise();
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
            nt_log_info("Asteroids complexity=%u count=%u lod=%u/%u/%u/%u triangles=%" PRIu64 " draws=%u update_ms=%.3f render_submit_ms=%.3f cpu_frame_ms=%.3f gpu_ms=%.3f memory=%" PRIu64, s_level,
                        s_complexities[s_level].instances, s_lod_counts[0], s_lod_counts[1], s_lod_counts[2], s_lod_counts[3], s_triangles, nt_gfx_draw_calls(&g_nt_gfx.counters), s_update_ms,
                        s_render_ms, s_cpu_ms, s_gpu_ms, s_mem_used);
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
    if (!scene_arg_u32(argc, argv, "--complexity", 9, &s_requested_level) || !scene_arg_u32(argc, argv, "-c", 9, &s_requested_level) || !scene_arg_u32(argc, argv, "--hide-hud", 1, &hide_hud) ||
        !scene_arg_u32(argc, argv, "--paused", 1, &paused)) {
        (void)fprintf(stderr, "Asteroids: --complexity / -c expects 0..9; --hide-hud and --paused expect 0 or 1.\n");
        return 1;
    }
    s_level = s_requested_level;
    s_ui_staged_level = (int)s_level;
    s_hide_hud = hide_hud != 0;
    s_paused = paused != 0;
    s_camera = s_initial_camera;
    s_light = s_initial_light;
    noise_init();
    sphere_init();
    if (nt_engine_init(&(nt_engine_config_t){.app_name = "asteroids", .version = 1}) != NT_OK) {
        return 1;
    }
    g_nt_window.width = 1280;
    g_nt_window.height = 800;
    g_nt_window.title = "Methane Asteroids - Neotolis";
    nt_window_init();
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_example_frames_init(argc, argv);
    nt_input_init();
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.max_textures = 128;
    gfx.stream_capacity = 16U * 1024U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_VERTEX] = 512U * 1024U;
    gfx.frame_capacity[AST_STREAM_INSTANCES] = (AST_MAX_SLOTS + 1U) * (uint32_t)sizeof(asteroid_instance_t);
    gfx.max_vertex_inputs = AST_KEY_COUNT + 32U;
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
    nt_resource_register_type(NT_ASSET_SHADER_CODE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_shader, .deactivate = nt_gfx_deactivate_shader});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 1});
    s_font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = 128, .measure_cache_size = 128});
    nt_font_add(s_font, nt_resource_request(nt_hash64_str("asteroids/font"), NT_ASSET_FONT));
    nt_material_init(&(nt_material_desc_t){.max_materials = 2});
    s_text_material = nt_material_create(&(nt_material_create_desc_t){.blend = nt_blend_alpha_premultiplied(), .cull_mode = NT_CULL_NONE, .label = "asteroids_text"});
    init_ui();
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
    s_programs[4].vs = nt_resource_request(nt_hash64_str("examples/asteroids/shaders/noise.vert"), NT_ASSET_SHADER_CODE);
    s_programs[4].fs = nt_resource_request(nt_hash64_str("examples/asteroids/shaders/noise.frag"), NT_ASSET_SHADER_CODE);
    s_mars = nt_resource_request(nt_hash64_str("asteroids/mars"), NT_ASSET_TEXTURE);
    const char *sky_names[] = {"u_sky_px", "u_sky_nx", "u_sky_py", "u_sky_ny", "u_sky_pz", "u_sky_nz"};
    for (uint32_t i = 0; i < 6; i++) {
        char path[64];
        (void)snprintf(path, sizeof(path), "asteroids/sky/%u", i);
        s_sky_faces[i] = nt_resource_request(nt_hash64_str(path), NT_ASSET_TEXTURE);
        s_sky_names[i] = nt_hash32_str(sky_names[i]);
    }
    s_noise_name = nt_hash32_str("u_noise");
    s_diffuse_name = nt_hash32_str("u_diffuse");
    s_light_name = nt_hash32_str("u_light_position");
    load_pack(nt_hash32_str("asteroids_ui"), "asteroids_ui.ntpack");
    load_pack(nt_hash32_str("asteroids_core"), "asteroids_core.ntpack");
    load_pack(nt_hash32_str("asteroids_space"), "asteroids_space.ntpack");
    nt_resource_set_activate_time_budget(0);
    const uint8_t deep[AST_SUBDIVISIONS][3] = {{0, 128, 0}, {0, 64, 128}, {96, 0, 128}, {128, 0, 0}};
    const uint8_t shallow[AST_SUBDIVISIONS][3] = {{0, 255, 0}, {0, 128, 255}, {196, 0, 255}, {255, 0, 0}};
    for (uint32_t i = 0; i < AST_SUBDIVISIONS; i++) {
        srgb_palette_color(s_lod_deep[i], deep[i]);
        srgb_palette_color(s_lod_shallow[i], shallow[i]);
    }
    /* Source animation follows wall time even when a benchmark frame exceeds 100 ms. */
    g_nt_app.max_dt = FLT_MAX;
    nt_app_run(frame);
#ifndef NT_PLATFORM_WEB
    drop_generated();
    nt_log_info("Asteroids UI: scratch peak=%zu bytes", nt_mem_scratch_high_water_mark());
    nt_ui_destroy_context(s_ui);
    nt_ui_module_shutdown();
    nt_sprite_renderer_shutdown();
    nt_material_destroy(s_ui_material);
    nt_program_ref_drop(&s_ui_program);
    nt_mem_scratch_shutdown();
    free(s_ui_arena);
    nt_text_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_text_material);
    for (uint32_t i = 0; i < 5; i++) {
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
