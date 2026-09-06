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

/* Custom block for the flat-radial material, attr_map order [a_radial, a_layout]:
 * a_radial @ 0..3 + a_layout @ 4..7 (walker fills a_layout by name; placeholders here). */
static const float k_radial_attrs[8] = {0.25F, 1.75F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};

/* Radial material: shares the fixture's vs/fs role but declares a_radial @ loc 4 +
 * a_layout @ loc 7 (walker-filled by name) so build_sprite_layout produces the
 * extended 32 B custom block (distinct pipeline). */
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
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    desc.attr_map[1].stream_name = "a_layout";
    desc.attr_map[1].location = 7;
    desc.attr_map_count = 2;
    desc.label = "radial_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    return mat;
}

void setUp(void) {
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
static nt_ui_image_custom_block_t s_img_blocks[16];
static const nt_ui_element_data_t k_layer0 = {.layer = 0U};

/* A flat-radial IMAGE payload: custom_bytes>0 + GEOMETRY mode routes it through the
 * walker's generic custom-emit branch (emit_custom_geometry), which bakes the 32 B
 * [a_radial, a_layout] block matching the radial material's attr_map — so a custom-attr
 * material is never bound to a plain (no-custom-attr) emit. The batching semantics under
 * test (one material .id = one draw) are unchanged. A .id==0 material is a plain image
 * (custom_bytes 0). */
static void make_image(int idx, float x, nt_resource_t atlas, uint32_t region_index, nt_material_t material) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->zIndex = 0;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 32, .height = 32};
    c->renderData.image.backgroundColor = (Clay_Color){0}; /* untinted */
    const bool custom = (material.id != 0);
    s_img_payloads[idx] = (nt_ui_image_payload_t){
        .atlas = atlas,
        .region_index = region_index,
        .slice9_scale = 1.0F,
        .material = material,
        .custom = NULL,
    };
    if (custom) {
        s_img_blocks[idx] = (nt_ui_image_custom_block_t){.custom_bytes = (uint8_t)sizeof k_radial_attrs, .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY};
        memcpy(s_img_blocks[idx].custom_attrs, k_radial_attrs, sizeof k_radial_attrs);
        s_img_payloads[idx].custom = &s_img_blocks[idx];
    }
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

/* ===== nt_ui_radial widget math + ABI + emit payload ===== */

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
    TEST_ASSERT_EQUAL_UINT32(12U, (uint32_t)sizeof(nt_ui_radial_style_t));
    nt_ui_radial_style_t d = nt_ui_radial_style_defaults();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.color_packed);
    TEST_ASSERT_TRUE(approx(d.inner_radius_norm, 0.0F));
    TEST_ASSERT_EQUAL_UINT32(0U, d.material.id);
}

/* (c) the [a_radial, a_layout] block is baked per-vertex when nt_ui_radial is driven
 * through the walker: a_radial verbatim (w now 0), a_layout filled BY NAME — aspect in
 * a_layout.x (out[4]), bbox px in .yz (out[5..6]). */
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

    /* A bbox quad: 4 vertices, each carrying the same 32 B custom block. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 64.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[8] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 8);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], start), "a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], end), "a_radial.y == angle_end");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 0.5F), "a_radial.z == inner_radius_norm");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], 0.0F), "a_radial.w == 0 (aspect moved to a_layout)");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[4], expect_aspect), "a_layout.x == aspect (w/h)");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[5], 64.0F), "a_layout.y == bbox width px");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[6], 32.0F), "a_layout.z == bbox height px");
    }
}

/* Generic injection proof: nt_ui_image_custom with a [a_radial, a_layout] material bakes
 * a_radial verbatim and fills a_layout BY NAME — aspect in a_layout.x (out[4]), bbox px
 * in .yz, a_radial untouched. This is the path radial rides, exercised directly through
 * the public custom API. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_image_custom_injects_aspect(void) {
    const nt_material_t mat = make_radial_material();
    const float block[8] = {7.0F, 8.0F, 9.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("custom_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(32)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(32)}}};
        const nt_ui_image_custom_t img = {
            .atlas = s_fx.atlas.handle,
            .region_index = s_fx.atlas.white_region_idx,
            .material = mat,
            .custom_attrs = block,
            .custom_bytes = (uint8_t)sizeof block,
            .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY,
            .slice9_scale = 1.0F,
            .color_packed = 0xFFFFFFFFU,
        };
        nt_ui_image_custom(s_fx.ctx, NULL, &img, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 96.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[8] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 8);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 7.0F), "a_radial.x verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 8.0F), "a_radial.y verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 9.0F), "a_radial.z verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], 0.0F), "a_radial.w untouched");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[4], expect_aspect), "a_layout.x == bbox w/h");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[5], 96.0F), "a_layout.y == bbox width px");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[6], 32.0F), "a_layout.z == bbox height px");
    }
}

/* Reorder-safety: a material whose attr_map is PERMUTED to [a_layout, a_radial] (a_layout
 * FIRST) must still get aspect written at a_layout's offset (block floats 0..3), proving the
 * walker resolves the injection BY NAME from attr_map, not by a fixed slot. This is the whole
 * point of the name-bound design — the block offset follows attr_map declaration order. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_image_custom_name_bound_reorder_safe(void) {
    /* Build a material with the attr_map order reversed vs make_radial_material. */
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "perm_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "perm_fs"});
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_make_program(vs, fs);
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.attr_map[0].stream_name = "a_layout"; /* a_layout FIRST (offset 0..3) */
    desc.attr_map[0].location = 7;
    desc.attr_map[1].stream_name = "a_radial";
    desc.attr_map[1].location = 4;
    desc.attr_map_count = 2;
    desc.label = "perm_test_material";
    const nt_material_t mat = nt_material_create(&desc);

    /* Block in attr_map order: a_layout placeholders @0..3, a_radial data @4..7. */
    const float block[8] = {0.0F, 0.0F, 0.0F, 0.0F, 7.0F, 8.0F, 9.0F, 0.0F};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("perm_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(32)}}}) {
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(32)}}};
        const nt_ui_image_custom_t img = {
            .atlas = s_fx.atlas.handle,
            .region_index = s_fx.atlas.white_region_idx,
            .material = mat,
            .custom_attrs = block,
            .custom_bytes = (uint8_t)sizeof block,
            .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY,
            .slice9_scale = 1.0F,
            .color_packed = 0xFFFFFFFFU,
        };
        nt_ui_image_custom(s_fx.ctx, NULL, &img, &decl);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 96.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[8] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 8);
        /* a_layout is FIRST now: aspect at out[0], px at out[1..2]. */
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], expect_aspect), "permuted: a_layout.x at offset 0 == aspect");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 96.0F), "permuted: a_layout.y == bbox width px");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 32.0F), "permuted: a_layout.z == bbox height px");
        /* a_radial verbatim at out[4..7]. */
        TEST_ASSERT_TRUE_MESSAGE(approx(out[4], 7.0F), "permuted: a_radial.x verbatim at offset 4");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[5], 8.0F), "permuted: a_radial.y verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[6], 9.0F), "permuted: a_radial.z verbatim");
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
    float out[8] = {0};
    nt_sprite_renderer_test_last_emit_radial(0, out, 8);
    TEST_ASSERT_TRUE(approx(out[0], start));
    TEST_ASSERT_TRUE(approx(out[1], start + (fill * sweep))); /* fill→angle */
    TEST_ASSERT_TRUE(approx(out[4], 1.0F));                   /* square bbox → a_layout.x aspect 1 */
}

/* ===== nt_ui_radial_image — textured reveal widget ===== */

/* Radial-IMAGE material: a_radial @ loc 4 + a_tint @ loc 5 + a_uvrect @ loc 6 +
 * a_layout @ loc 7 (the full 64 B extended layout the walker bakes; a_uvrect + a_layout
 * filled by name) PLUS a u_reveal_mode vec4 param baked at creation so the reveal-mode
 * look is observable via nt_material_get_info. */
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
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    desc.attr_map[1].stream_name = "a_tint";
    desc.attr_map[1].location = 5;
    desc.attr_map[2].stream_name = "a_uvrect";
    desc.attr_map[2].location = 6;
    desc.attr_map[3].stream_name = "a_layout";
    desc.attr_map[3].location = 7;
    desc.attr_map_count = 4;
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

    /* White region = 4 verts; every vert carries the same 64 B block. aspect is now in
     * a_layout.x (out[12]); a_radial.w is freed (0). */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 64.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[16] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 16);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 0.25F), "a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 1.75F), "a_radial.y == angle_end");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 0.5F), "a_radial.z == inner_radius_norm");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], 0.0F), "a_radial.w == 0 (aspect moved to a_layout)");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[12], expect_aspect), "a_layout.x == aspect (w/h)");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[13], 64.0F), "a_layout.y == bbox width px");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[14], 32.0F), "a_layout.z == bbox height px");
    }
}

/* (c) mode stays a MATERIAL-level look (u_reveal_mode.x == mode), but the TINT is now
 * PER-WIDGET: tint_color_packed + tint_strength bake into the a_tint block (floats 4..7
 * of the 64 B custom block). Verify both: material mode intact + per-vertex tint baked. */
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
        nt_sprite_renderer_test_last_emit_radial(v, out, 8);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[4], 1.0F), "a_tint.r == 255/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[5], 128.0F / 255.0F), "a_tint.g == 128/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[6], 0.0F), "a_tint.b == 0/255");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[7], 0.75F), "a_tint.w == tint_strength");
    }
}

/* (d) style ABI guard + defaults sane + all four reveal modes distinct. */
static void test_radial_image_style_abi(void) {
    TEST_ASSERT_EQUAL_UINT32(44U, (uint32_t)sizeof(nt_ui_radial_image_style_t));
    nt_ui_radial_image_style_t d = nt_ui_radial_image_style_defaults();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.color_packed);
    TEST_ASSERT_TRUE(approx(d.slice9_scale, 1.0F));
    TEST_ASSERT_EQUAL_UINT32(0U, d.material.id);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, d.tint_color_packed);
    TEST_ASSERT_TRUE(approx(d.tint_strength, 0.6F));
    /* Four distinct reveal modes (material-level look). */
    TEST_ASSERT_EQUAL_INT(0, (int)NT_UI_RADIAL_REVEAL_DESATURATE);
    TEST_ASSERT_EQUAL_INT(1, (int)NT_UI_RADIAL_REVEAL_DIM);
    TEST_ASSERT_EQUAL_INT(2, (int)NT_UI_RADIAL_REVEAL_HIDE);
    TEST_ASSERT_EQUAL_INT(3, (int)NT_UI_RADIAL_REVEAL_TINT);
}

/* (e) PACKED-region path: a radial_image over a region whose atlas UV does NOT span
 * [0,1] bakes a_uvrect (custom floats 8..11) = the region's actual min/max atlas UV,
 * so the reveal fs can re-center the wedge. The 64 B custom block carries radial(0..3)
 * + tint(4..7) + uvrect(8..11) + layout(12..15) — both uvrect AND layout walker-filled. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_packed_region_bakes_uvrect(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();

    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    radial_image_walk(&ref, &style, 64.0F, 64.0F);

    /* Packed quad = 4 verts; every vert carries the same a_uvrect = the region UV bounds. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; v++) {
        float out[16] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 16); /* full 64 B block */
        TEST_ASSERT_TRUE_MESSAGE(approx(out[8], MINIMAL_UI_ATLAS_PACKED_U0), "a_uvrect.x == region u0");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[9], MINIMAL_UI_ATLAS_PACKED_V0), "a_uvrect.y == region v0");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[10], MINIMAL_UI_ATLAS_PACKED_U1), "a_uvrect.z == region u1");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[11], MINIMAL_UI_ATLAS_PACKED_V1), "a_uvrect.w == region v1");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[12], 1.0F), "a_layout.x == aspect (square bbox)");
    }
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
        nt_sprite_renderer_test_last_emit_radial(v, out, 8);
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
    RUN_TEST(test_route_a_custom_binds_radial_material);
    RUN_TEST(test_route_a_custom_does_not_batch);
    RUN_TEST(test_route_b_shared_material_batches);
    RUN_TEST(test_route_b_distinct_material_switches);
    RUN_TEST(test_route_b_zero_material_uses_base);
    RUN_TEST(test_walker_material_cache_batches_plain);
    RUN_TEST(test_walker_material_cache_resets_on_text_barrier);
    RUN_TEST(test_radial_fill_to_angle);
    RUN_TEST(test_radial_two_angle_swap);
    RUN_TEST(test_radial_style_abi);
    RUN_TEST(test_radial_emit_bakes_payload);
    RUN_TEST(test_image_custom_injects_aspect);
    RUN_TEST(test_image_custom_name_bound_reorder_safe);
    RUN_TEST(test_radial_fill_emit_payload);
    RUN_TEST(test_radial_image_region_bakes_payload);
    RUN_TEST(test_radial_image_reveal_mode_plumbed);
    RUN_TEST(test_radial_image_packed_region_bakes_uvrect);
    RUN_TEST(test_radial_image_style_abi);
    RUN_TEST(test_radial_image_fill_emit);
    RUN_TEST(test_radial_image_opacity_preserves_tint_strength);
    return UNITY_END();
}
