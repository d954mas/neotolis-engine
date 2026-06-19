/* Phase 66 — walker → custom-material binding for radial emits (INT-66-06).
 *
 * The phase's one genuine open integration risk: how a custom-material radial
 * emit reaches the UI walker. The walker binds ONE ctx->sprite_material per pass
 * and (before this plan) had no per-element material override on the image path.
 *
 * Route A (prototype): a CUSTOM-command handler binds the radial material and
 * runs the plan-01 custom-attr emit. It proves the renderer hook end-to-end
 * through the walker, but CUSTOM is a hard barrier (is_segmentable false) so it
 * flushes per radial — it does NOT meet the D-66-07 thousands-in-one-draw scale
 * target. Superseded by Route B below.
 *
 * Route B (shipped): an optional material handle on nt_ui_image_payload_t; the
 * walker image dispatch binds it via set_material (auto-flush on .id change) when
 * it differs from the bound base. N radials sharing ONE radial material batch to
 * one flush + one draw (D-66-07).
 *
 * The widget math/ABI for nt_ui_radial / nt_ui_radial_image lands in 66-03/04. */

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
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_radial.h"
#include "ui/nt_ui_radial_image.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 64
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* One FLOAT4 a_radial block (matches the plan-01 v1 payload). */
static const float k_radial_attrs[4] = {0.25F, 1.75F, 0.5F, 1.0F};

static uint32_t s_vpack_counter;

/* Radial material: shares the fixture's vs/fs role but declares a_radial @ loc 4
 * so build_sprite_layout produces the extended 36 B stride (distinct pipeline). */
static nt_material_t make_radial_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "radial_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "radial_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "radial_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "radial_test_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "radial_test_fs_%u", s_vpack_counter);
    s_vpack_counter++;

    const nt_hash32_t pid = nt_hash32_str(pack_name);
    const nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    const nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id));
    const nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    const nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    desc.attr_map_count = 1;
    desc.label = "radial_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

void setUp(void) {
    s_vpack_counter = 0;
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

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
     * ctx->sprite_material), set the per-widget block, emit a quad. */
    nt_sprite_renderer_set_material(rc->radial_mat);
    nt_sprite_renderer_set_custom_attrs(k_radial_attrs, (uint8_t)sizeof k_radial_attrs);

    const float positions[4][2] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(rc->atlas, rc->region_index, positions, 4, idx, 6, NT_MATH_MAT4_IDENTITY, 0xFFFFFFFFU);
}

/* Route A proves the renderer hook end-to-end through the walker: a radial
 * CUSTOM element binds the radial material and produces one extended-stride emit
 * with the per-vertex a_radial block baked in. */
static void test_route_a_custom_binds_radial_material(void) {
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
        nt_sprite_renderer_test_last_emit_radial(v, out, 4);
        for (uint8_t k = 0; k < 4; k++) {
            TEST_ASSERT_TRUE_MESSAGE(out[k] == k_radial_attrs[k], "Route A: a_radial block not baked per-vertex");
        }
    }
}

/* Route A is a HARD BARRIER: CUSTOM is not segmentable, so emit_custom flushes
 * before AND the radial emit's own boundary flushes — N radials via CUSTOM scale
 * the draw-call count linearly. This is the documented prototype-only limitation
 * that fails D-66-07; contrast test_route_b_*_batches below. */
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
    /* Each CUSTOM is its own draw — linear in N, NOT batched (Pitfall 3). */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

/* ---- Route B: per-element material on the walker IMAGE path ---- */

/* File-scope so payloads/layer data outlive the walk. */
static nt_ui_image_payload_t s_img_payloads[16];
static const nt_ui_element_data_t k_layer0 = {.layer = 0U};

static void make_image(int idx, float x, nt_resource_t atlas, uint32_t region_index, nt_material_t material) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->zIndex = 0;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 32, .height = 32};
    c->renderData.image.backgroundColor = (Clay_Color){0}; /* untinted */
    s_img_payloads[idx] = (nt_ui_image_payload_t){.atlas = atlas, .region_index = region_index, .slice9_scale = 1.0F, .material = material};
    c->renderData.image.imageData = &s_img_payloads[idx];
    c->userData = (void *)&k_layer0;
}

/* N IMAGE radials sharing ONE material flush+draw together: only the
 * base<->radial boundary flushes (set_material no-ops on same .id). The
 * draw-call count is CONSTANT in N (D-66-07), unlike Route A's per-radial flush. */
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

/* ===== WGT-66-04: nt_ui_radial widget math + ABI + emit payload ===== */

#define WGT_PI 3.14159265358979323846F

static bool approx(float a, float b) { return fabsf(a - b) < 1e-5F; }

/* (a) fill→angle: angle_end = angle_start + clamp(fill,0,1)*sweep_total. */
static void test_radial_fill_to_angle(void) {
    const float start = 0.5F;
    const float sweep = 2.0F * WGT_PI;
    TEST_ASSERT_TRUE(approx(nt_ui_radial_fill_to_end(start, 0.0F, sweep), start));
    TEST_ASSERT_TRUE(approx(nt_ui_radial_fill_to_end(start, 0.5F, sweep), start + (0.5F * sweep)));
    TEST_ASSERT_TRUE(approx(nt_ui_radial_fill_to_end(start, 1.0F, sweep), start + sweep));
    /* Clamp below 0 → start; above 1 → start+sweep. */
    TEST_ASSERT_TRUE(approx(nt_ui_radial_fill_to_end(start, -0.5F, sweep), start));
    TEST_ASSERT_TRUE(approx(nt_ui_radial_fill_to_end(start, 1.5F, sweep), start + sweep));
}

/* (b) two-angle swap reverses the sweep direction (flip is API-layer, no shader
 * branch): the start→end span and end→start span are complementary modulo TAU. */
static void test_radial_two_angle_swap(void) {
    const float a = 0.25F;
    const float b = 1.75F;
    const float tau = 2.0F * WGT_PI;
    const float fwd = fmodf((b - a) + tau, tau);
    const float rev = fmodf((a - b) + tau, tau);
    TEST_ASSERT_TRUE(approx(fwd + rev, tau)); /* swapping start/end complements the span */
    TEST_ASSERT_FALSE(approx(fwd, rev));      /* and it is genuinely a different sweep */
}

/* (d) style ABI guard present + defaults sane. */
static void test_radial_style_abi(void) {
    TEST_ASSERT_EQUAL_UINT32(16U, (uint32_t)sizeof(nt_ui_radial_style_t));
    nt_ui_radial_style_t d = nt_ui_radial_style_defaults();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.color_packed);
    TEST_ASSERT_TRUE(approx(d.inner_radius_norm, 0.0F));
    TEST_ASSERT_TRUE(approx(d.aa_softness, 1.0F));
    TEST_ASSERT_EQUAL_UINT32(0U, d.material.id);
}

/* (c) the a_radial FLOAT4 payload is baked per-vertex when nt_ui_radial is driven
 * through the walker: read back via the plan-01 test_last_emit_radial accessor. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_emit_bakes_payload(void) {
    nt_ui_radial_style_t style = nt_ui_radial_style_defaults();
    style.material = make_radial_material();
    style.inner_radius_norm = 0.5F;

    const float start = 0.25F;
    const float end = 1.75F;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(32)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(32)}}};
        nt_ui_radial(s_fx.ctx, NULL, start, end, &style, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    /* A bbox quad: 4 vertices, each carrying the same a_radial block. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 64.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 4);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], start), "a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], end), "a_radial.y == angle_end");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 0.5F), "a_radial.z == inner_radius_norm");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], expect_aspect), "a_radial.w == aspect (w/h)");
    }
}

/* fill convenience drives the same emit path: angle_end follows fill→angle. */
static void test_radial_fill_emit_payload(void) {
    nt_ui_radial_style_t style = nt_ui_radial_style_defaults();
    style.material = make_radial_material();

    const float start = 0.0F;
    const float sweep = 2.0F * WGT_PI;
    const float fill = 0.25F;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}};
        nt_ui_radial_fill(s_fx.ctx, NULL, start, fill, sweep, &style, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    float out[4] = {0};
    nt_sprite_renderer_test_last_emit_radial(0, out, 4);
    TEST_ASSERT_TRUE(approx(out[0], start));
    TEST_ASSERT_TRUE(approx(out[1], start + (fill * sweep))); /* fill→angle */
    TEST_ASSERT_TRUE(approx(out[3], 1.0F));                   /* square bbox → aspect 1 */
}

/* ===== WGT-66-05: nt_ui_radial_image — textured reveal widget ===== */

/* Radial-IMAGE material: a_radial @ loc 4 (extended layout) PLUS a u_reveal_mode
 * vec4 param so the reveal-mode plumbing is observable via nt_material_get_info. */
static nt_material_t make_radial_image_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "ri_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "ri_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "ri_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "ri_test_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "ri_test_fs_%u", s_vpack_counter);
    s_vpack_counter++;

    const nt_hash32_t pid = nt_hash32_str(pack_name);
    const nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    const nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id));
    const nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    const nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    desc.attr_map_count = 1;
    desc.params[0].name = NT_UI_RADIAL_IMAGE_PARAM_MODE;
    desc.params[0].value[0] = -1.0F; /* sentinel: widget must overwrite */
    desc.param_count = 1;
    desc.label = "radial_image_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

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

    /* White region = 4 verts; every vert carries the same a_radial block. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 64.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 4);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 0.25F), "a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 1.75F), "a_radial.y == angle_end");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 0.5F), "a_radial.z == inner_radius_norm");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], expect_aspect), "a_radial.w == aspect (w/h)");
    }
}

/* (b) slice9 path: a slice9 override routes through emit_slice9; the a_radial
 * block is duplicated across all 16 slice9 verts. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_slice9_bakes_payload(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    style.flags |= NT_UI_IMAGE_SLICE9_OVERRIDE;
    style.slice9_lrtb[0] = 4;
    style.slice9_lrtb[1] = 4;
    style.slice9_lrtb[2] = 4;
    style.slice9_lrtb[3] = 4;

    /* Region 1 is a 16x16 untrimmed region (white 1x1 cannot fit slice9 borders). */
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.polygon_region_idx);
    radial_image_walk(&ref, &style, 64.0F, 64.0F);

    /* Slice9 = 16 verts; each carries the same a_radial block. */
    TEST_ASSERT_EQUAL_UINT32(16U, nt_sprite_renderer_test_last_slice9_vertex_count());
    for (uint32_t v = 0; v < 16U; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 4);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 0.25F), "slice9 a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 1.75F), "slice9 a_radial.y == angle_end");
    }
}

/* (c) reveal mode plumbed to the material as u_reveal_mode.x = mode. */
static void test_radial_image_reveal_mode_plumbed(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    style.mode = (uint8_t)NT_UI_RADIAL_REVEAL_HIDE; /* == 2 */

    /* Before the walk the sentinel is -1 (widget must overwrite). */
    TEST_ASSERT_TRUE(approx(reveal_mode_param_x(style.material), -1.0F));

    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    radial_image_walk(&ref, &style, 40.0F, 40.0F);

    TEST_ASSERT_TRUE_MESSAGE(approx(reveal_mode_param_x(style.material), (float)NT_UI_RADIAL_REVEAL_HIDE), "u_reveal_mode.x == reveal mode");
}

/* (d) style ABI guard + defaults sane + all four reveal modes distinct. */
static void test_radial_image_style_abi(void) {
    TEST_ASSERT_EQUAL_UINT32(48U, (uint32_t)sizeof(nt_ui_radial_image_style_t));
    nt_ui_radial_image_style_t d = nt_ui_radial_image_style_defaults();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.color_packed);
    TEST_ASSERT_TRUE(approx(d.slice9_scale, 1.0F));
    TEST_ASSERT_EQUAL_UINT32(0U, d.material.id);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NT_UI_RADIAL_REVEAL_DESATURATE, d.mode);
    /* Four distinct reveal modes. */
    TEST_ASSERT_EQUAL_INT(0, (int)NT_UI_RADIAL_REVEAL_DESATURATE);
    TEST_ASSERT_EQUAL_INT(1, (int)NT_UI_RADIAL_REVEAL_DIM);
    TEST_ASSERT_EQUAL_INT(2, (int)NT_UI_RADIAL_REVEAL_HIDE);
    TEST_ASSERT_EQUAL_INT(3, (int)NT_UI_RADIAL_REVEAL_TINT);
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
    nt_sprite_renderer_test_last_emit_radial(0, out, 4);
    TEST_ASSERT_TRUE(approx(out[0], start));
    TEST_ASSERT_TRUE(approx(out[1], start + (fill * sweep))); /* fill->angle */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_route_a_custom_binds_radial_material);
    RUN_TEST(test_route_a_custom_does_not_batch);
    RUN_TEST(test_route_b_shared_material_batches);
    RUN_TEST(test_route_b_distinct_material_switches);
    RUN_TEST(test_route_b_zero_material_uses_base);
    RUN_TEST(test_radial_fill_to_angle);
    RUN_TEST(test_radial_two_angle_swap);
    RUN_TEST(test_radial_style_abi);
    RUN_TEST(test_radial_emit_bakes_payload);
    RUN_TEST(test_radial_fill_emit_payload);
    RUN_TEST(test_radial_image_region_bakes_payload);
    RUN_TEST(test_radial_image_slice9_bakes_payload);
    RUN_TEST(test_radial_image_reveal_mode_plumbed);
    RUN_TEST(test_radial_image_style_abi);
    RUN_TEST(test_radial_image_fill_emit);
    return UNITY_END();
}
