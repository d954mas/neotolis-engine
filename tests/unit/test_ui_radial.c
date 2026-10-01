/* walker → custom-material binding for radial emits. The walker binds ONE
 * ctx->sprite_material per pass; radials need a per-element material override on
 * the image path. Covers two binding routes: Route A (a CUSTOM-command handler)
 * and Route B (an optional material handle on nt_ui_image_payload_t). */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "math/nt_math.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_radial_image.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 64
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* Custom block for the radial test material: a_radial @ tail 0..3 + a second FLOAT4 @ 4..7. */
static const float k_radial_attrs[8] = {0.25F, 1.75F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};

/* Radial material: shares the fixture's vs/fs role but declares a_radial @ loc 4 +
 * a FLOAT4 @ loc 7 with an explicit 52 B full vertex layout. */
static nt_material_t make_radial_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "radial_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "radial_fs"});

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_make_program(vs, fs);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.vertex_layout = (nt_vertex_layout_t){.stride = 52,
                                              .attr_count = 5,
                                              .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                        {.location = 3, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
                                                        {.location = 2, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
                                                        {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20},
                                                        {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 36}}};
    desc.label = "radial_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    return mat;
}

void setUp(void) {
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) { ui_walker_fixture_inject_cmds(&s_fx, s_test_cmds, count, MAX_TEST_CMDS); }

/* ---- Route A: CUSTOM handler binds the radial material + emits ---- */

typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    nt_material_t radial_mat;
    int calls;
} route_a_ctx_t;

static void route_a_handler(const nt_ui_custom_frame_t *frame, void *userdata) {
    (void)frame;
    route_a_ctx_t *rc = (route_a_ctx_t *)userdata;
    rc->calls++;

    /* Bind the radial material (different fs + extended layout than the base
     * ctx->sprite_material), emit a quad with the per-widget block. */
    nt_sprite_renderer_set_material(rc->radial_mat);

    const float positions[4][2] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(rc->atlas, rc->region_index, positions, 4, idx, 6, NT_MATH_MAT4_IDENTITY, 0xFFFFFFFFU, k_radial_attrs, (uint8_t)sizeof k_radial_attrs);
}

/* Route A proves the renderer hook end-to-end through the walker: a radial
 * CUSTOM element binds the radial material and produces one extended-stride emit
 * with the per-vertex a_radial block baked in. */
static void test_custom_handler_binds_extended_material(void) {
    route_a_ctx_t rc = {.atlas = s_fx.atlas.handle, .region_index = s_fx.atlas.white_region_idx, .radial_mat = make_radial_material(), .calls = 0};
    nt_ui_set_custom_handler(s_fx.ctx, route_a_handler, &rc);

    static nt_ui_custom_data_t cd;
    cd = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 32, .height = 32};
    c->renderData.custom.customData = &cd;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, rc.calls);

    /* Extended-stride emit occurred: the a_radial block is baked into every
     * vertex (renderer-hook end-to-end proof). */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_attrs(v, out, 16);
        for (uint8_t k = 0; k < 4; k++) {
            TEST_ASSERT_TRUE_MESSAGE(out[k] == k_radial_attrs[k], "Route A: a_radial block not baked per-vertex");
        }
    }
}

/* Route A is a HARD BARRIER: CUSTOM is not segmentable, so emit_custom flushes
 * before AND the radial emit's own boundary flushes — N radials via CUSTOM scale
 * the draw-call count linearly. This is the documented prototype-only limitation
 * that fails the batch-scale target; contrast test_route_b_*_batches below. */
static void test_route_a_custom_does_not_batch(void) {
    route_a_ctx_t rc = {.atlas = s_fx.atlas.handle, .region_index = s_fx.atlas.white_region_idx, .radial_mat = make_radial_material(), .calls = 0};
    nt_ui_set_custom_handler(s_fx.ctx, route_a_handler, &rc);

    static nt_ui_custom_data_t cd[4];
    const int n = 4;
    for (int i = 0; i < n; i++) {
        cd[i] = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 40), .y = 10, .width = 32, .height = 32};
        c->renderData.custom.customData = &cd[i];
    }
    inject_frozen_cmds(n);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(n, rc.calls);
    /* Each CUSTOM is its own draw — linear in N, NOT batched. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* ---- Route B: per-element material on the walker IMAGE path ---- */

/* File-scope so payloads/layer data outlive the walk. */
static nt_ui_image_payload_t s_img_payloads[16];
static const nt_ui_element_data_t k_layer0 = {.layer = 0U};

/* A material override draws through the walker's IMAGE path; a tail material gets a zero
 * tail from plain emits. A .id==0 material is the walker's base material. */
static void make_image(int idx, float x, nt_resource_t atlas, uint32_t region_index, nt_material_t material) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->zIndex = 0;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 32, .height = 32};
    c->renderData.image.backgroundColor = (Clay_Color){0}; /* untinted */
    s_img_payloads[idx] = (nt_ui_image_payload_t){
        .atlas = atlas,
        .region_index = region_index,
        .slice9_scale = 1.0F,
        .material = material,
    };
    c->renderData.image.imageData = &s_img_payloads[idx];
    c->userData = (void *)&k_layer0;
}

/* N IMAGE radials sharing ONE material flush+draw together: only the
 * base<->radial boundary flushes (set_material no-ops on same .id). The
 * draw-call count is CONSTANT in N, unlike Route A's per-radial flush. */
static void test_route_b_shared_material_batches(void) {
    const nt_material_t radial = make_radial_material();
    const uint32_t region = s_fx.atlas.white_region_idx;

    /* Two radials sharing one material. */
    make_image(0, 0.0F, s_fx.atlas.handle, region, radial);
    make_image(1, 40.0F, s_fx.atlas.handle, region, radial);
    inject_frozen_cmds(2);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    const uint32_t calls_2 = nt_ui_get_last_walk_draw_calls(s_fx.ctx);

    /* Five radials sharing the SAME material. If batching holds, the draw-call
     * count does NOT grow with N. */
    for (int i = 0; i < 5; i++) {
        make_image(i, (float)(i * 40), s_fx.atlas.handle, region, radial);
    }
    inject_frozen_cmds(5);
    nt_ui_walk(s_fx.ctx, &target);
    const uint32_t calls_5 = nt_ui_get_last_walk_draw_calls(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(calls_2, calls_5); /* constant in N — batched */
    TEST_ASSERT_EQUAL_UINT32(1U, calls_5);      /* one radial material = one draw */
}

/* A single radial IMAGE with a material distinct from the bound base binds
 * correctly: base (RECT) + radial (IMAGE) = a material switch (2 draws). */
static void test_route_b_distinct_material_switches(void) {
    const nt_material_t radial = make_radial_material();

    /* RECT on base material, then a radial IMAGE with a distinct material. */
    Clay_RenderCommand *r = &s_test_cmds[0];
    r->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    r->zIndex = 0;
    r->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    r->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    r->userData = (void *)&k_layer0;
    make_image(1, 40.0F, s_fx.atlas.handle, s_fx.atlas.white_region_idx, radial);
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* base RECT flushes when the radial material binds → 2 draws. */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* A .id==0 payload material uses the base — a plain IMAGE batches with a RECT
 * on the same base material (1 draw), proving the sentinel path. */
static void test_route_b_zero_material_uses_base(void) {
    Clay_RenderCommand *r = &s_test_cmds[0];
    r->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    r->zIndex = 0;
    r->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    r->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    r->userData = (void *)&k_layer0;
    make_image(1, 40.0F, s_fx.atlas.handle, s_fx.atlas.white_region_idx, NT_MATERIAL_INVALID);
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* DRAW-CALL PARITY gate for the walker material cache (Part B). A run of plain
 * same-base-material IMAGEs batches to ONE draw; the cache only SKIPS the redundant
 * set_material — it must never change the draw count. */
static void test_walker_material_cache_batches_plain(void) {
    const uint32_t region = s_fx.atlas.white_region_idx;
    /* Five plain images (material.id==0 → walker base) all share one material. */
    for (int i = 0; i < 5; i++) {
        make_image(i, (float)(i * 40), s_fx.atlas.handle, region, NT_MATERIAL_INVALID);
    }
    inject_frozen_cmds(5);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    /* One base material, no barrier between → one draw (cache skipped 4 set_materials). */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* Cache-reset regression: a TEXT barrier between two same-base-material IMAGE runs
 * closes the sprite cmd. The cache MUST reset there so the second run rebinds — else
 * emit hits a closed cmd (no open cmd) or silently drops geometry. Two runs split by a
 * barrier = TWO draws, not one. Proves last_sprite_mat_id is reset on the text barrier. */
static void test_walker_material_cache_resets_on_text_barrier(void) {
    const uint32_t region = s_fx.atlas.white_region_idx;
    /* image(base) @0, TEXT @1, image(base) @2 — all layer 0, same z-segment order. */
    make_image(0, 0.0F, s_fx.atlas.handle, region, NT_MATERIAL_INVALID);

    Clay_RenderCommand *t = &s_test_cmds[1];
    t->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    t->zIndex = 0;
    t->boundingBox = (Clay_BoundingBox){.x = 40, .y = 0, .width = 32, .height = 16};
    t->renderData.text.fontId = 0; /* fixture stub font: emit silently skips, barrier still flushes */
    t->renderData.text.fontSize = 16;
    t->renderData.text.textColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    t->renderData.text.stringContents.chars = "x";
    t->renderData.text.stringContents.length = 1;
    t->userData = (void *)&k_layer0;

    make_image(2, 80.0F, s_fx.atlas.handle, region, NT_MATERIAL_INVALID);
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* TEXT splits the two image runs → 2 sprite draws (cache correctly rebound run 2). */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* ===== Angular convention and generic custom-image payload ===== */

#define WGT_PI 3.14159265358979323846F

static bool approx(float a, float b) { return fabsf(a - b) < 1e-5F; }

/* ===== nt_ui_radial_image — textured reveal widget ===== */

/* Radial-IMAGE material with the engine layout. A u_reveal_mode vec4 param is baked at
 * creation so the reveal-mode look is observable via nt_material_get_info. */
static nt_material_t make_radial_image_material_mode(nt_ui_radial_reveal_mode_t mode) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "ri_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "ri_fs"});

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_make_program(vs, fs);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.vertex_layout = NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT;
    desc.params[0].name = NT_UI_RADIAL_IMAGE_PARAM_MODE;
    desc.params[0].value[0] = (float)mode; /* reveal look baked at creation */
    desc.param_count = 1;
    desc.label = "radial_image_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    return mat;
}

/* Default test material: DESATURATE mode baked at creation. */
static nt_material_t make_radial_image_material(void) { return make_radial_image_material_mode(NT_UI_RADIAL_REVEAL_DESATURATE); }

/* Read back the u_reveal_mode param's .x from the material info (-1 if absent). */
static float reveal_mode_param_x(nt_material_t mat) {
    const nt_material_info_t *info = nt_material_get_info(mat);
    const uint32_t want = nt_hash32_str(NT_UI_RADIAL_IMAGE_PARAM_MODE).value;
    for (uint8_t i = 0; i < info->param_count; i++) {
        if (info->param_name_hashes[i] == want) {
            return info->params[i][0];
        }
    }
    return -1.0F;
}

static nt_ui_radial_image_tail_t emitted_tail(uint32_t vertex) {
    nt_ui_radial_image_tail_t tail;
    nt_sprite_renderer_test_last_emit_attrs(vertex, &tail, sizeof tail);
    return tail;
}

/* Source-image coordinates the vertex shader derives from the vertex's atlas UV. */
static void source_at(uint32_t vertex, float out[2]) {
    const nt_ui_radial_image_tail_t tail = emitted_tail(vertex);
    uint16_t uv[2];
    nt_sprite_renderer_test_last_emit_texcoord(vertex, uv);
    const float u = (float)uv[0] / 65535.0F;
    const float v = (float)uv[1] / 65535.0F;
    out[0] = (tail.source_u[0] * u) + (tail.source_u[1] * v) + tail.source_u[2];
    out[1] = (tail.source_v[0] * u) + (tail.source_v[1] * v) + tail.source_v[2];
}

static bool approx_source(uint32_t vertex, float x, float y) {
    float actual[2];
    source_at(vertex, actual);
    return fabsf(actual[0] - x) < 1e-4F && fabsf(actual[1] - y) < 1e-4F;
}

/* Drive a textured radial_image through the walker against a real atlas region. */
static void radial_image_walk(nt_atlas_region_ref_t *ref, nt_ui_radial_image_style_t *style, float fixed_w, float fixed_h) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("ri_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(fixed_w), CLAY_SIZING_FIXED(fixed_h)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(fixed_w), CLAY_SIZING_FIXED(fixed_h)}}};
        nt_ui_radial_image(s_fx.ctx, NULL, ref, 0.25F, 1.75F, style, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (a) region path: a textured radial_image emits via emit_region; the a_radial
 * FLOAT4 is baked into every vertex of the region quad (4 verts on the white
 * region). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_region_bakes_payload(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    style.inner_radius_norm = 0.5F;

    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    radial_image_walk(&ref, &style, 64.0F, 32.0F);

    /* White region = 4 verts; one tail per emit, the source map follows each corner. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; v++) {
        const nt_ui_radial_image_tail_t tail = emitted_tail(v);
        TEST_ASSERT_TRUE_MESSAGE(approx(tail.radial[0], 0.25F), "a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(tail.radial[1], 1.75F), "a_radial.y == angle_end");
        TEST_ASSERT_TRUE_MESSAGE(approx(tail.radial[2], 0.5F), "a_radial.z == inner_radius_norm");
        TEST_ASSERT_TRUE_MESSAGE(approx(tail.radial[3], 64.0F / 32.0F), "a_radial.w == bbox w/h");
    }
    TEST_ASSERT_TRUE(approx_source(0, 0.0F, 1.0F));
    TEST_ASSERT_TRUE(approx_source(2, 1.0F, 0.0F));
}

/* Source coordinates are independent of the packed atlas UV orientation, D4 and explicit
 * flips; the sprite renderer mirrors positions and reveal together. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_source_uv_ignores_atlas_d4_and_explicit_flips(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    nt_texture_region_t *region = (nt_texture_region_t *)nt_atlas_get_region(s_fx.atlas.handle, s_fx.atlas.packed_region_idx);
    float(*positions)[2] = (float(*)[2])nt_atlas_get_region_positions(s_fx.atlas.handle, s_fx.atlas.packed_region_idx);
    region->source_w = 20;
    region->source_h = 16;
    region->trim_offset_x = 3;
    region->trim_offset_y = 2;
    const float source_xy[4][2] = {{3, 2}, {11, 2}, {11, 10}, {3, 10}};
    memcpy(positions, source_xy, sizeof source_xy);
    nt_atlas_uv_t *uvs = (nt_atlas_uv_t *)nt_atlas_get_region_uvs(s_fx.atlas.handle, s_fx.atlas.packed_region_idx);
    nt_atlas_uv_t packed[4];
    memcpy(packed, uvs, sizeof packed);
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    for (uint8_t transform = 0; transform < 8U; ++transform) {
        region->transform = transform;
        /* D4 packing rotates (and for odd transforms mirrors) which atlas corner each source corner samples. */
        for (uint32_t vertex = 0; vertex < 4U; ++vertex) {
            const uint32_t turned = (vertex + transform) & 3U;
            uvs[vertex] = packed[(transform & 4U) != 0U ? (3U - turned) : turned];
        }
        for (uint8_t flip = 0; flip < 4U; ++flip) {
            style.flip_bits = flip;
            radial_image_walk(&ref, &style, 64.0F, 64.0F);
            for (uint32_t vertex = 0; vertex < 4U; ++vertex) {
                TEST_ASSERT_TRUE(approx_source(vertex, source_xy[vertex][0] / 20.0F, 1.0F - (source_xy[vertex][1] / 16.0F)));
            }
        }
    }
    memcpy(uvs, packed, sizeof packed);
}

/* Every vertex of a polygon hull maps to its own source point, not only the fitted triangle's. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_polygon_region_maps_every_vertex(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    const uint32_t index = s_fx.atlas.polygon_region_idx;
    nt_texture_region_t *region = (nt_texture_region_t *)nt_atlas_get_region(s_fx.atlas.handle, index);
    TEST_ASSERT_EQUAL_UINT8(6, region->vertex_count);
    region->source_w = 40;
    region->source_h = 30;
    float(*positions)[2] = (float(*)[2])nt_atlas_get_region_positions(s_fx.atlas.handle, index);
    nt_atlas_uv_t *uvs = (nt_atlas_uv_t *)nt_atlas_get_region_uvs(s_fx.atlas.handle, index);
    const float hull[6][2] = {{4, 2}, {30, 1}, {38, 12}, {33, 28}, {9, 29}, {1, 15}};
    for (uint32_t k = 0; k < 6U; ++k) {
        positions[k][0] = hull[k][0];
        positions[k][1] = hull[k][1];
        /* Packed rotated a quarter turn into a sub-rectangle of the page. */
        uvs[k].atlas_u = (uint16_t)(8192U + ((uint32_t)hull[k][1] * 512U));
        uvs[k].atlas_v = (uint16_t)(4096U + ((uint32_t)hull[k][0] * 256U));
    }
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, index);
    radial_image_walk(&ref, &style, 64.0F, 48.0F);
    TEST_ASSERT_EQUAL_UINT32(6U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t k = 0; k < 6U; ++k) {
        TEST_ASSERT_TRUE(approx_source(k, hull[k][0] / 40.0F, 1.0F - (hull[k][1] / 30.0F)));
    }
}

/* Positions carry the atlas intrinsic scale; the source map divides it back out. */
static void test_radial_image_source_map_divides_atlas_scale(void) {
    minimal_ui_atlas_t scaled = minimal_ui_atlas_create_ipu(0.5F);
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    nt_texture_region_t *region = (nt_texture_region_t *)nt_atlas_get_region(scaled.handle, scaled.packed_region_idx);
    region->source_w = 20;
    region->source_h = 16;
    float(*positions)[2] = (float(*)[2])nt_atlas_get_region_positions(scaled.handle, scaled.packed_region_idx);
    const float source_xy[4][2] = {{3, 2}, {11, 2}, {11, 10}, {3, 10}};
    for (uint32_t k = 0; k < 4U; ++k) {
        positions[k][0] = source_xy[k][0] * 0.5F;
        positions[k][1] = source_xy[k][1] * 0.5F;
    }
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(scaled.handle, 0, scaled.packed_region_idx);
    radial_image_walk(&ref, &style, 64.0F, 64.0F);
    for (uint32_t k = 0; k < 4U; ++k) {
        TEST_ASSERT_TRUE(approx_source(k, source_xy[k][0] / 20.0F, 1.0F - (source_xy[k][1] / 16.0F)));
    }
    /* Texture destruction is pass-forbidden, as in the fixture teardown. */
    nt_gfx_end_pass();
    minimal_ui_atlas_destroy(&scaled);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

/* (c) mode stays a MATERIAL-level look (u_reveal_mode.x == mode), but the TINT is now
 * PER-WIDGET: tint_color_packed + tint_strength bake into a_tint (`nt_ui_radial_image_tail_t.tint`). Verify both: material mode intact + per-vertex tint baked. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_reveal_mode_plumbed(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material_mode(NT_UI_RADIAL_REVEAL_HIDE); /* == 2 */
    style.tint_color_packed = 0xFF0080FFU;                                      /* 0xAABBGGRR: r=255, g=128, b=0 -> orange */
    style.tint_strength = 0.75F;

    /* The material carries the baked mode before any walk. */
    TEST_ASSERT_TRUE_MESSAGE(approx(reveal_mode_param_x(style.material), (float)NT_UI_RADIAL_REVEAL_HIDE), "material carries baked u_reveal_mode.x");

    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    radial_image_walk(&ref, &style, 40.0F, 40.0F);

    /* The walk must NOT overwrite the material-level reveal mode. */
    TEST_ASSERT_TRUE_MESSAGE(approx(reveal_mode_param_x(style.material), (float)NT_UI_RADIAL_REVEAL_HIDE), "walk leaves material u_reveal_mode.x intact");

    /* a_tint (floats 4..7 of the custom block) baked per-vertex: rgb 0..1 + strength. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; v++) {
        float out[8] = {0};
        nt_sprite_renderer_test_last_emit_attrs(v, out, 32);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[4], 1.0F), "a_tint.r == 255/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[5], 128.0F / 255.0F), "a_tint.g == 128/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[6], 0.0F), "a_tint.b == 0/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[7], 0.75F), "a_tint.w == tint_strength");
    }
}

/* Compact style, usable defaults and four distinct reveal modes. */
static void test_radial_image_style_defaults(void) {
    TEST_ASSERT_EQUAL_UINT32(32U, (uint32_t)sizeof(nt_ui_radial_image_style_t));
    nt_ui_radial_image_style_t d = nt_ui_radial_image_style_defaults();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.color_packed);
    TEST_ASSERT_TRUE(approx(d.inner_radius_norm, 0.0F));
    TEST_ASSERT_TRUE(approx(d.origin_x, 0.5F) && approx(d.origin_y, 0.5F));
    TEST_ASSERT_EQUAL_UINT8(0U, d.flags);
    TEST_ASSERT_EQUAL_UINT8(0U, d.flip_bits);
    TEST_ASSERT_EQUAL_UINT32(0U, d.material.id);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.tint_color_packed);
    TEST_ASSERT_TRUE(approx(d.tint_strength, 0.6F));
    /* Four distinct reveal modes (material-level look). */
    TEST_ASSERT_EQUAL_INT(0, (int)NT_UI_RADIAL_REVEAL_DESATURATE);
    TEST_ASSERT_EQUAL_INT(1, (int)NT_UI_RADIAL_REVEAL_DIM);
    TEST_ASSERT_EQUAL_INT(2, (int)NT_UI_RADIAL_REVEAL_HIDE);
    TEST_ASSERT_EQUAL_INT(3, (int)NT_UI_RADIAL_REVEAL_TINT);
}

/* Slice9 and the engine-owned payload bits; ANALYTIC_SHAPE would read the reveal as a shape style. */
static void test_radial_image_rejects_slice9_and_engine_flags(void) {
    static const uint8_t k_flags[] = {NT_UI_IMAGE_SLICE9_OVERRIDE, NT_UI_IMAGE_ANALYTIC_SHAPE, NT_UI_IMAGE_RADIAL_REVEAL};
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    const nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
    for (uint32_t i = 0; i < sizeof k_flags; ++i) {
        style.flags = (uint8_t)(k_flags[i] | NT_UI_IMAGE_ORIGIN_OVERRIDE);
        NT_TEST_EXPECT_ASSERT(nt_ui_radial_image(s_fx.ctx, NULL, &ref, 0.0F, 1.0F, &style, NULL));
        TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_UI_IMAGE_ORIGIN_OVERRIDE"));
    }
    nt_ui_end(s_fx.ctx);
}

static void test_radial_image_draws_baked_slice9_as_plain_quad(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    nt_texture_region_t *region = (nt_texture_region_t *)nt_atlas_get_region(s_fx.atlas.handle, s_fx.atlas.packed_region_idx);
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    const nt_pointer_t mouse = {0};
    region->slice9_lrtb[0] = 1;
    nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
    nt_ui_radial_image(s_fx.ctx, NULL, &ref, 0.0F, 1.0F, &style, NULL);
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    region->slice9_lrtb[0] = 0;
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Packed-region UV stays packed for sampling; source UV spans the untrimmed
 * image domain independently. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_packed_region_uses_source_uv(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();

    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    radial_image_walk(&ref, &style, 64.0F, 64.0F);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_TRUE(approx(emitted_tail(0).radial[3], 1.0F) && approx_source(0, 0.0F, 1.0F));
    uint16_t atlas_uv[2] = {0};
    nt_sprite_renderer_test_last_emit_texcoord(0, atlas_uv);
    TEST_ASSERT_EQUAL_UINT16(0x4000U, atlas_uv[0]);
    TEST_ASSERT_EQUAL_UINT16(0xC000U, atlas_uv[1]);
}

/* fill convenience drives the same textured emit; angle_end follows fill->angle. */
static void test_radial_image_fill_emit(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();

    const float start = 0.0F;
    const float sweep = 2.0F * WGT_PI;
    const float fill = 0.25F;

    nt_pointer_t mouse = {0};
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("ri_fill_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}};
        nt_ui_radial_image_fill(s_fx.ctx, NULL, &ref, start, fill, sweep, &style, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    float out[4] = {0};
    nt_sprite_renderer_test_last_emit_attrs(0, out, 16);
    TEST_ASSERT_TRUE(approx(out[0], start));
    TEST_ASSERT_TRUE(approx(out[1], start + (fill * sweep))); /* fill->angle */

    /* Fill clamps to [0, 1]: below empties the wedge, above closes the full sweep. */
    const float fills[2] = {-0.5F, 1.7F};
    const float ends[2] = {start, start + sweep};
    for (uint32_t i = 0; i < 2U; ++i) {
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {
            const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}};
            nt_ui_radial_image_fill(s_fx.ctx, NULL, &ref, start, fills[i], sweep, &style, &decl);
        }
        nt_ui_end(s_fx.ctx);
        nt_ui_walk(s_fx.ctx, &target);
        nt_sprite_renderer_test_last_emit_attrs(0, out, 16);
        TEST_ASSERT_TRUE(approx(out[1], ends[i]));
    }
}

/* Drive a radial_image nested under a parent CLAY carrying `opacity`, so the walker's accum_opacity
 * reaches build_custom_block. Same shape as radial_image_walk but with an opacity-bearing wrapper. */
static void radial_image_walk_under_opacity(nt_atlas_region_ref_t *ref, nt_ui_radial_image_style_t *style, float opacity) {
    nt_ui_transform_t identity = nt_ui_transform_defaults();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("ri_op_root"), .userData = (void *)NT_UI_DATA_XFORM(0U, &identity, opacity), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}};
        nt_ui_radial_image(s_fx.ctx, NULL, ref, 0.25F, 1.75F, style, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* (f) REGRESSION: parent opacity must NOT corrupt radial_image's a_tint.w (the TINT reveal strength,
 * not alpha). Under a 0.5-opacity parent the reveal strength stays intact; the REAL alpha fades via
 * color_packed -> a_color (~128). Mirrors the rich inline-image opacity fade, but radial opts OUT of
 * the a_tint fold so its reveal-mix amount is preserved. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_opacity_preserves_tint_strength(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material_mode(NT_UI_RADIAL_REVEAL_TINT);
    style.tint_strength = 0.6F; /* defaults to 0.6, asserted below as the un-faded value */

    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    radial_image_walk_under_opacity(&ref, &style, 0.5F);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; v++) {
        float out[8] = {0};
        nt_sprite_renderer_test_last_emit_attrs(v, out, 32);
        /* a_tint.w (float 7) = tint_strength: UNCHANGED by parent opacity (no fold for radial). */
        TEST_ASSERT_TRUE_MESSAGE(approx(out[7], 0.6F), "a_tint.w (tint_strength) UNCHANGED under 0.5 opacity");

        /* The REAL alpha fades via color_packed -> a_color (0xFF default -> ~128 at 0.5). */
        uint8_t col[4] = {0};
        nt_sprite_renderer_test_last_emit_color(v, col);
        TEST_ASSERT_TRUE_MESSAGE(col[3] >= 126 && col[3] <= 130, "a_color.a halved (~128) under 0.5 opacity");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_handler_binds_extended_material);
    RUN_TEST(test_route_a_custom_does_not_batch);
    RUN_TEST(test_route_b_shared_material_batches);
    RUN_TEST(test_route_b_distinct_material_switches);
    RUN_TEST(test_route_b_zero_material_uses_base);
    RUN_TEST(test_walker_material_cache_batches_plain);
    RUN_TEST(test_walker_material_cache_resets_on_text_barrier);
    RUN_TEST(test_radial_image_region_bakes_payload);
    RUN_TEST(test_radial_image_source_uv_ignores_atlas_d4_and_explicit_flips);
    RUN_TEST(test_radial_image_polygon_region_maps_every_vertex);
    RUN_TEST(test_radial_image_source_map_divides_atlas_scale);
    RUN_TEST(test_radial_image_reveal_mode_plumbed);
    RUN_TEST(test_radial_image_packed_region_uses_source_uv);
    RUN_TEST(test_radial_image_style_defaults);
    RUN_TEST(test_radial_image_rejects_slice9_and_engine_flags);
    RUN_TEST(test_radial_image_draws_baked_slice9_as_plain_quad);
    RUN_TEST(test_radial_image_fill_emit);
    RUN_TEST(test_radial_image_opacity_preserves_tint_strength);
    return UNITY_END();
}
