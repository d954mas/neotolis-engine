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

/* Custom block for the flat-radial material, attr_map order [a_radial, a_layout]:
 * a_radial @ 0..3 + a_layout @ 4..7 (walker fills a_layout by name; placeholders here). */
static const float k_radial_attrs[8] = {0.25F, 1.75F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};

/* Radial material: shares the fixture's vs/fs role but declares a_radial @ loc 4 +
 * a_layout @ loc 7 (walker-filled by name) with an explicit 52 B full vertex layout. */
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
     * ctx->sprite_material), set the per-widget block, emit a quad. */
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

/* ===== Angular convention and generic custom-image payload ===== */

#define WGT_PI 3.14159265358979323846F

static bool approx(float a, float b) { return fabsf(a - b) < 1e-5F; }

/* Generic injection proof: nt_ui_image_custom with a [a_radial, a_layout] material bakes
 * a_radial verbatim and fills a_layout BY NAME — aspect in a_layout.x (out[4]), bbox px
 * in .yz, a_radial untouched. Radial image rides this path, exercised directly through
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
        nt_sprite_renderer_test_last_emit_attrs(v, out, 32);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 7.0F), "a_radial.x verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 8.0F), "a_radial.y verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 9.0F), "a_radial.z verbatim");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], 0.0F), "a_radial.w untouched");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[4], expect_aspect), "a_layout.x == bbox w/h");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[5], 96.0F), "a_layout.y == bbox width px");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[6], 32.0F), "a_layout.z == bbox height px");
    }
}

/* Semantic declaration order is independent of physical offsets. */
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
    desc.vertex_layout = (nt_vertex_layout_t){.stride = 52,
                                              .attr_count = 5,
                                              .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                        {.location = 3, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
                                                        {.location = 2, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
                                                        {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20},
                                                        {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 36}}};
    desc.label = "perm_test_material";
    const nt_material_t mat = nt_material_create(&desc);

    /* Physical layout places a_layout at tail 0 and a_radial at tail 16. */
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
        nt_sprite_renderer_test_last_emit_attrs(v, out, 32);
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

/* ===== nt_ui_radial_image — textured reveal widget ===== */

/* Radial-IMAGE material: a_radial @ loc 4 + a_tint @ loc 5 + a_source_uv @ loc 6 +
 * a_aspect @ loc 7. The renderer fills source UV; the walker fills aspect. A
 * u_reveal_mode vec4 param is baked
 * at creation so the reveal-mode
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
    desc.attr_map[2].stream_name = "a_source_uv";
    desc.attr_map[2].location = 6;
    desc.attr_map[3].stream_name = "a_aspect";
    desc.attr_map[3].location = 7;
    desc.attr_map_count = 4;
    desc.vertex_layout = (nt_vertex_layout_t){.stride = 64,
                                              .attr_count = 7,
                                              .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                        {.location = 3, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
                                                        {.location = 2, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
                                                        {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20},
                                                        {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 36},
                                                        {.location = 7, .type = NT_VERTEX_FLOAT, .count = 1, .offset = 52},
                                                        {.location = 6, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 56}}};
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

    /* White region = 4 verts; aspect is uniform, source UV follows each corner. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    const float expect_aspect = 64.0F / 32.0F;
    for (uint32_t v = 0; v < 4U; v++) {
        float out[11] = {0};
        nt_sprite_renderer_test_last_emit_attrs(v, out, sizeof out);
        TEST_ASSERT_TRUE_MESSAGE(approx(out[0], 0.25F), "a_radial.x == angle_start");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[1], 1.75F), "a_radial.y == angle_end");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[2], 0.5F), "a_radial.z == inner_radius_norm");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[3], 0.0F), "a_radial.w remains unused");
        TEST_ASSERT_TRUE_MESSAGE(approx(out[8], expect_aspect), "a_aspect == bbox w/h");
    }
    float corner[11] = {0};
    nt_sprite_renderer_test_last_emit_attrs(0, corner, sizeof corner);
    TEST_ASSERT_TRUE(approx(corner[9], 0.0F) && approx(corner[10], 1.0F));
    nt_sprite_renderer_test_last_emit_attrs(2, corner, sizeof corner);
    TEST_ASSERT_TRUE(approx(corner[9], 1.0F) && approx(corner[10], 0.0F));
}

/* Source coordinates are independent of packed atlas UV, D4 and explicit
 * flips; the sprite renderer mirrors positions and reveal together. */
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
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    for (uint8_t transform = 0; transform < 8U; ++transform) {
        region->transform = transform;
        for (uint8_t flip = 0; flip < 4U; ++flip) {
            style.flip_bits = flip;
            radial_image_walk(&ref, &style, 64.0F, 64.0F);
            for (uint32_t vertex = 0; vertex < 4U; ++vertex) {
                float out[11] = {0};
                nt_sprite_renderer_test_last_emit_attrs(vertex, out, sizeof out);
                TEST_ASSERT_TRUE(approx(out[9], source_xy[vertex][0] / 20.0F));
                TEST_ASSERT_TRUE(approx(out[10], 1.0F - (source_xy[vertex][1] / 16.0F)));
            }
        }
    }
}

/* (c) mode stays a MATERIAL-level look (u_reveal_mode.x == mode), but the TINT is now
 * PER-WIDGET: tint_color_packed + tint_strength bake into the a_tint block (floats 4..7
 * of the 44 B custom block). Verify both: material mode intact + per-vertex tint baked. */
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

static void test_radial_image_rejects_baked_slice9(void) {
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = make_radial_image_material();
    nt_texture_region_t *region = (nt_texture_region_t *)nt_atlas_get_region(s_fx.atlas.handle, s_fx.atlas.packed_region_idx);
    nt_atlas_region_ref_t ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    const nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
    region->slice9_lrtb[0] = 1;
    NT_TEST_EXPECT_ASSERT(nt_ui_radial_image(s_fx.ctx, NULL, &ref, 0.0F, 1.0F, &style, NULL));
    region->slice9_lrtb[0] = 0;
    nt_ui_end(s_fx.ctx);
}

static void test_source_uv_material_rejects_geometry_emit(void) {
    const nt_material_t material = make_radial_image_material();
    const float block[11] = {0};
    const nt_ui_image_custom_t img = {.atlas = s_fx.atlas.handle,
                                      .region_index = s_fx.atlas.white_region_idx,
                                      .material = material,
                                      .custom_attrs = block,
                                      .custom_bytes = (uint8_t)sizeof block,
                                      .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY,
                                      .slice9_scale = 1.0F,
                                      .color_packed = 0xFFFFFFFFU};
    const nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
    CLAY({.id = CLAY_ID("source_uv_geometry_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32)}}}) {
        nt_ui_image_custom(s_fx.ctx, NULL, &img, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32)}});
    }
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
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
    float out[11] = {0};
    nt_sprite_renderer_test_last_emit_attrs(0, out, sizeof out);
    TEST_ASSERT_TRUE(approx(out[8], 1.0F) && approx(out[9], 0.0F) && approx(out[10], 1.0F));
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_custom_semantic_subset_order_and_byte_payload(void) {
    const nt_material_t radial = make_radial_material();
    nt_material_create_desc_t desc = {.program = nt_material_get_info(radial)->program,
                                      .vertex_layout = nt_material_get_info(radial)->vertex_layout,
                                      .attr_map = {{.stream_name = "a_layout", .location = 7}, {.stream_name = "a_radial", .location = 4}},
                                      .attr_map_count = 2};
    const nt_material_t mat = nt_material_create(&desc);
    uint8_t bytes[32];
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)(255U - i);
    }
    const uint8_t expected[16] = {255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240};
    const char *subset[] = {"a_layout", NULL};
    const char *reversed[] = {"a_radial", "a_layout", NULL};
    const char *unknown[] = {"missing", NULL};
    for (uint8_t variant = 0; variant < 2U; ++variant) {
        const nt_pointer_t mouse = {0};
        nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
        nt_ui_image_custom_t img = {.atlas = s_fx.atlas.handle,
                                    .region_index = s_fx.atlas.white_region_idx,
                                    .material = mat,
                                    .custom_attrs = bytes,
                                    .custom_bytes = sizeof(bytes),
                                    .attr_names = variant == 0U ? subset : reversed,
                                    .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY,
                                    .slice9_scale = 1,
                                    .color_packed = UINT32_MAX};
        const Clay_ElementDeclaration decl = {.layout.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(32)}};
        nt_ui_image_custom(s_fx.ctx, NULL, &img, &decl);
        img.attr_names = unknown;
        NT_TEST_EXPECT_ASSERT(nt_ui_image_custom(s_fx.ctx, NULL, &img, &decl));
        nt_ui_end(s_fx.ctx);
        const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
        nt_ui_walk(s_fx.ctx, &target);
        uint8_t actual[32];
        nt_sprite_renderer_test_last_emit_attrs(0, actual, sizeof(actual));
        TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(expected));
        float layout[4];
        memcpy(layout, actual + 16, sizeof(layout));
        TEST_ASSERT_TRUE(layout[0] == 3.0F && layout[1] == 96.0F && layout[2] == 32.0F);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_known_injection_semantic_requires_compatible_physical_field(void) {
    const nt_material_t radial = make_radial_material();
    const nt_vertex_layout_t layout = nt_material_get_info(radial)->vertex_layout;
    for (uint8_t variant = 0; variant < 4U; ++variant) {
        nt_material_create_desc_t desc = {.program = nt_material_get_info(radial)->program, .vertex_layout = layout, .attr_map = {{.stream_name = "a_layout", .location = 7}}, .attr_map_count = 1};
        nt_vertex_attr_t *attr = &desc.vertex_layout.attrs[4];
        if (variant == 0U) {
            attr->location = 6;
        } else if (variant == 1U) {
            attr->type = NT_VERTEX_UINT8;
            attr->normalized = true;
        } else if (variant == 2U) {
            attr->count = 3;
        } else {
            attr->offset = 0;
        }
        const nt_material_t material = nt_material_create(&desc);
        const nt_ui_image_custom_block_t block = {.custom_bytes = 32, .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY};
        const nt_ui_image_payload_t payload = {.atlas = s_fx.atlas.handle, .region_index = s_fx.atlas.white_region_idx, .material = material, .slice9_scale = 1, .custom = &block};
        Clay_RenderCommand command = {.commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE, .boundingBox = {0, 0, 96, 32}, .renderData.image.imageData = (void *)&payload};
        ui_walker_fixture_inject_cmds(&s_fx, &command, 1, 1);
        const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
        NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_radial_image_validates_fixed_float4_offsets(void) {
    const nt_material_t base = make_radial_image_material();
    const nt_material_info_t *info = nt_material_get_info(base);
    static const char *const names[] = {"a_radial", "a_tint", "a_source_uv", "a_aspect"};
    for (uint8_t variant = 0; variant < 3U; ++variant) {
        nt_material_create_desc_t desc = {.program = info->program, .vertex_layout = info->vertex_layout, .attr_map_count = info->attr_map_count};
        for (uint8_t i = 0; i < info->attr_map_count; ++i) {
            desc.attr_map[i] = (nt_material_attr_desc_t){.stream_name = names[i], .location = info->attr_map_locations[i]};
        }
        if (variant == 0U) {
            const uint16_t offset = desc.vertex_layout.attrs[3].offset;
            desc.vertex_layout.attrs[3].offset = desc.vertex_layout.attrs[4].offset;
            desc.vertex_layout.attrs[4].offset = offset;
        } else if (variant == 1U) {
            desc.vertex_layout.attrs[4].type = NT_VERTEX_UINT8;
        } else {
            desc.vertex_layout.attrs[3].count = 3;
        }
        const nt_material_t material = nt_material_create(&desc);
        const nt_pointer_t mouse = {0};
        nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
        nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
        style.material = material;
        nt_atlas_region_ref_t region = {.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx};
        NT_TEST_EXPECT_ASSERT(nt_ui_radial_image(s_fx.ctx, NULL, &region, 0.2F, 1.7F, &style, NULL));
        nt_ui_end(s_fx.ctx);
    }
}

static void test_radial_image_accepts_reordered_semantic_map(void) {
    const nt_material_t base = make_radial_image_material();
    const nt_material_create_desc_t desc = {
        .program = nt_material_get_info(base)->program,
        .vertex_layout = nt_material_get_info(base)->vertex_layout,
        .attr_map = {{.stream_name = "a_aspect", .location = 7}, {.stream_name = "a_source_uv", .location = 6}, {.stream_name = "a_tint", .location = 5}, {.stream_name = "a_radial", .location = 4}},
        .attr_map_count = 4};
    nt_ui_radial_image_style_t style = nt_ui_radial_image_style_defaults();
    style.material = nt_material_create(&desc);
    nt_atlas_region_ref_t region = {.atlas = s_fx.atlas.handle, .region = s_fx.atlas.white_region_idx};
    const nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800, 600, 0, &mouse, 1);
    nt_ui_radial_image(s_fx.ctx, NULL, &region, 0.2F, 1.7F, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(32)}});
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    float actual[11];
    nt_sprite_renderer_test_last_emit_attrs(0, actual, sizeof(actual));
    TEST_ASSERT_TRUE(actual[0] == 0.2F && actual[1] == 1.7F && actual[8] == 3.0F);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_radial_image_validates_fixed_float4_offsets);
    RUN_TEST(test_radial_image_accepts_reordered_semantic_map);
    RUN_TEST(test_custom_semantic_subset_order_and_byte_payload);
    RUN_TEST(test_known_injection_semantic_requires_compatible_physical_field);
    RUN_TEST(test_custom_handler_binds_extended_material);
    RUN_TEST(test_route_a_custom_does_not_batch);
    RUN_TEST(test_route_b_shared_material_batches);
    RUN_TEST(test_route_b_distinct_material_switches);
    RUN_TEST(test_route_b_zero_material_uses_base);
    RUN_TEST(test_walker_material_cache_batches_plain);
    RUN_TEST(test_walker_material_cache_resets_on_text_barrier);
    RUN_TEST(test_image_custom_injects_aspect);
    RUN_TEST(test_image_custom_name_bound_reorder_safe);
    RUN_TEST(test_radial_image_region_bakes_payload);
    RUN_TEST(test_radial_image_source_uv_ignores_atlas_d4_and_explicit_flips);
    RUN_TEST(test_radial_image_reveal_mode_plumbed);
    RUN_TEST(test_radial_image_packed_region_uses_source_uv);
    RUN_TEST(test_radial_image_style_abi);
    RUN_TEST(test_radial_image_rejects_baked_slice9);
    RUN_TEST(test_source_uv_material_rejects_geometry_emit);
    RUN_TEST(test_radial_image_fill_emit);
    RUN_TEST(test_radial_image_opacity_preserves_tint_strength);
    return UNITY_END();
}
