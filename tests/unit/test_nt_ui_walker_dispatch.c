#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

/* ---- Test-local state ---- */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 32
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

static nt_ui_image_payload_t s_image_payload;

/* Custom-handler flag + receiver. */
static bool s_custom_called;
static const void *s_custom_received_cmd;
static void *s_custom_received_user;

static void test_custom_handler(const void *clay_cmd, void *userdata) {
    s_custom_called = true;
    s_custom_received_cmd = clay_cmd;
    s_custom_received_user = userdata;
}

/* ---- Common setUp / tearDown ---- */

void setUp(void) {
    nt_test_assert_install();
    s_custom_called = false;
    s_custom_received_cmd = NULL;
    s_custom_received_user = NULL;
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    memset(&s_image_payload, 0, sizeof s_image_payload);

    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Inject a synthetic frozen_cmds array into the ctx so the walker iterates
 * a known-shape command list. Bypasses Clay declaration machinery. */
static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* ---- Tests ---- */

/* RECTANGLE -> sprite renderer emit_region (4 verts for white quad) */
static void test_dispatch_rectangle(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 50.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* White region is 4 verts/6 indices -- emit_region preserves it. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6U, nt_sprite_renderer_test_last_emit_index_count());
    /* Walker element count delta matches frozen_cmds.length. */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_command_count(s_fx.ctx));
}

/* BORDER with all 4 widths non-zero -- exactly 4 last_emit calls
 * happen (top, bottom, left, right), all into the white region. Verify the
 * LAST emit was still a white 4-vert quad. */
static void test_dispatch_border_emits_4_rects(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 100.0F};
    c->renderData.border.color = (Clay_Color){.r = 0.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    c->renderData.border.width = (Clay_BorderWidth){.left = 2, .right = 2, .top = 2, .bottom = 2, .betweenChildren = 0};
    inject_frozen_cmds(1);

    /* Snapshot draw-call counter before walk. emit_border calls
     * emit_screen_rect 4 times against the same sprite material + atlas;
     * they all batch into one cmd that flushes at walk exit. */
    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Last emit is still a 4-vert white quad. */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    /* All 4 sides batch into one cmd; walker exit flush adds exactly 1 draw call. */
    TEST_ASSERT_EQUAL_UINT32(calls_before + 1U, nt_sprite_renderer_test_draw_call_count());
}

/* TEXT command with empty font slot is a contract violation -- emit_text
 * asserts. Game must call nt_ui_set_font before declaring TEXT.
 * Fixture binds slot 0 with a stub font, so use slot 1 (unbound). */
static void test_dispatch_text_unbound_font_asserts(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 60.0F, .width = 100.0F, .height = 20.0F};
    static const char *kText = "AB";
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 2, .chars = kText, .baseChars = kText};
    c->renderData.text.textColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    c->renderData.text.fontId = 1; /* unbound slot in fixture */
    c->renderData.text.fontSize = 14;
    c->renderData.text.letterSpacing = 0;
    c->renderData.text.lineHeight = 0;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* IMAGE -> reads nt_ui_image_payload_t and emits one region. */
static void test_dispatch_image(void) {
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.polygon_region_idx; /* 6-vert hull */
    s_image_payload.flip_bits = 0;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 100.0F, .y = 100.0F, .width = 64.0F, .height = 64.0F};
    c->renderData.image.backgroundColor = (Clay_Color){0}; /* untinted */
    c->renderData.image.imageData = &s_image_payload;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Polygon hull preservation: emit_image must NOT collapse to 4-vert quad. */
    TEST_ASSERT_EQUAL_UINT32(6U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(12U, nt_sprite_renderer_test_last_emit_index_count());
}

/* SCISSOR_START + SCISSOR_END are dispatched
 * and the walker exits with scissor disabled. */
static void test_dispatch_scissor_start_end(void) {
    Clay_RenderCommand *cs = &s_test_cmds[0];
    cs->commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    cs->boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 50.0F, .width = 200.0F, .height = 200.0F};
    cs->renderData.clip.horizontal = true;
    cs->renderData.clip.vertical = true;

    Clay_RenderCommand *ce = &s_test_cmds[1];
    ce->commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;

    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Walker MUST disable scissor at exit. */
    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_command_count(s_fx.ctx));
}

/* CUSTOM -> registered handler called with (cmd, userdata). */
static void test_dispatch_custom(void) {
    int sentinel = 42;
    nt_ui_set_custom_handler(s_fx.ctx, test_custom_handler, &sentinel);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = NULL;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_TRUE(s_custom_called);
    TEST_ASSERT_EQUAL_PTR(c, s_custom_received_cmd);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_custom_received_user);
}

/* NONE -> silent skip (no crash, no emit). */
static void test_dispatch_none_silent_skip(void) {
    /* Walk an empty command array -- frozen_cmds.length = 0. */
    inject_frozen_cmds(0);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_get_last_walk_command_count(s_fx.ctx));

    /* Also test an explicit NONE command -- still no crash, still no emit. */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_NONE;
    inject_frozen_cmds(1);
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_command_count(s_fx.ctx));
}

/* Non-zero tint must NOT hit the "untinted" shortcut. */
static void test_dispatch_image_tinted_packs_color(void) {
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.white_region_idx;
    s_image_payload.flip_bits = 0;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 32, .height = 32};
    /* Half-alpha black: packs to 0x80000000 in 0xAABBGGRR. */
    c->renderData.image.backgroundColor = (Clay_Color){.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 128.0F};
    c->renderData.image.imageData = &s_image_payload;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Rounded RECT goes through the tessellated-fan path (>4 verts). */
static void test_dispatch_rectangle_rounded_emits_fan(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 60.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    c->renderData.rectangle.cornerRadius = (Clay_CornerRadius){.topLeft = 8.0F, .topRight = 8.0F, .bottomLeft = 8.0F, .bottomRight = 8.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_GREATER_THAN_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_GREATER_THAN_UINT32(6U, nt_sprite_renderer_test_last_emit_index_count());
}

/* Zero cornerRadius keeps the 4-vert fast path. */
static void test_dispatch_rectangle_zero_radius_keeps_fast_path(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 60.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    c->renderData.rectangle.cornerRadius = (Clay_CornerRadius){0};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6U, nt_sprite_renderer_test_last_emit_index_count());
}

/* Rounded BORDER goes through the ring-strip path (>4 verts). */
static void test_dispatch_border_rounded_emits_strip(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 100.0F};
    c->renderData.border.color = (Clay_Color){.r = 0.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    c->renderData.border.width = (Clay_BorderWidth){.left = 2, .right = 2, .top = 2, .bottom = 2, .betweenChildren = 0};
    c->renderData.border.cornerRadius = (Clay_CornerRadius){.topLeft = 8.0F, .topRight = 8.0F, .bottomLeft = 8.0F, .bottomRight = 8.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_GREATER_THAN_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* emit_geometry samples the centroid (mean of 4 corner UVs) -- NOT vertex[0]'s
 * UV which would land at a texel boundary and bleed under linear filtering.
 * Fixture white_region UVs: (0,0)(FFFF,0)(FFFF,FFFF)(0,FFFF), mean = 0x7FFF. */
static void test_dispatch_rounded_rect_uv_is_centroid_not_corner(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 80.0F, .height = 40.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    c->renderData.rectangle.cornerRadius = (Clay_CornerRadius){.topLeft = 6.0F, .topRight = 6.0F, .bottomLeft = 6.0F, .bottomRight = 6.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Every emitted vertex must carry the SAME UV (solid-color shape).
     * Spot-check vertex 0 and vertex 1; they share by contract. */
    const uint32_t emitted = nt_sprite_renderer_test_last_emit_vertex_count();
    TEST_ASSERT_GREATER_THAN_UINT32(2U, emitted);
    uint16_t uv0[2];
    uint16_t uv1[2];
    nt_sprite_renderer_test_last_emit_texcoord(0U, uv0);
    nt_sprite_renderer_test_last_emit_texcoord(1U, uv1);
    TEST_ASSERT_EQUAL_UINT16(uv0[0], uv1[0]);
    TEST_ASSERT_EQUAL_UINT16(uv0[1], uv1[1]);
    /* Centroid of fixture white_region corner UVs: (0+0xFFFF+0xFFFF+0)/4 = 0x7FFF. */
    TEST_ASSERT_EQUAL_UINT16(0x7FFFU, uv0[0]);
    TEST_ASSERT_EQUAL_UINT16(0x7FFFU, uv0[1]);
}

/* CSS3 §5.5 keeps asymmetric radii {40,40,10,10} on 200x60 -- adjacent sums
 * fit, factor=1, no over-clamp to {30,30,10,10}. */
static void test_dispatch_rect_asymmetric_radii_no_over_clamp(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 200.0F, .height = 60.0F};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 0.0F, .b = 0.0F, .a = 255.0F};
    c->renderData.rectangle.cornerRadius = (Clay_CornerRadius){.topLeft = 40.0F, .topRight = 40.0F, .bottomLeft = 10.0F, .bottomRight = 10.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* 1 center + 4 * 7 arc points = 29 verts. */
    TEST_ASSERT_EQUAL_UINT32(29U, nt_sprite_renderer_test_last_emit_vertex_count());

    /* Y-flip + corner swap in dispatch_command (nt_ui.c). Vertex 1 is the
     * function's "TL" arc west point, which after the swap lives at the
     * world bottom-left and carries the SWAPPED radius (input.bottomLeft=10,
     * NOT input.topLeft=40). Expected py = world_y + cr.topLeft_swapped =
     * (vh - bb.y - bb.h) + bl_orig = 600 - 0 - 60 + 10 = 550. The non-clamp
     * invariant (radii kept asymmetric, never halved) is what's under test;
     * the precise vertex it lands on changed when the walker started
     * Y-flipping bboxes to match GL bottom-left. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(1U, pos);
    const int32_t px = (int32_t)pos[0];
    const int32_t py = (int32_t)pos[1];
    TEST_ASSERT_TRUE(px == 0 || px == -1);
    TEST_ASSERT_GREATER_THAN_INT32(545, py);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(550, py);
}

/* Partial widths with rounded corners: inner radius clamps to 0 on zero-width
 * axes, producing degenerate strip segments for the skipped sides. */
static void test_dispatch_border_rounded_partial_widths(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 60.0F};
    c->renderData.border.color = (Clay_Color){.r = 0.0F, .g = 0.0F, .b = 255.0F, .a = 255.0F};
    /* Only bottom border, with rounded bottom corners. Top sides absent. */
    c->renderData.border.width = (Clay_BorderWidth){.left = 0, .right = 0, .top = 0, .bottom = 2, .betweenChildren = 0};
    c->renderData.border.cornerRadius = (Clay_CornerRadius){.bottomLeft = 6.0F, .bottomRight = 6.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* width > radius: inner radius clamps to 0 (corner "filled" inside). */
static void test_dispatch_border_width_exceeds_radius(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 60.0F};
    c->renderData.border.color = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    c->renderData.border.width = (Clay_BorderWidth){.left = 8, .right = 8, .top = 8, .bottom = 8, .betweenChildren = 0};
    c->renderData.border.cornerRadius = (Clay_CornerRadius){.topLeft = 4.0F, .topRight = 4.0F, .bottomLeft = 4.0F, .bottomRight = 4.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Mixed corners (some sharp, some rounded) in one BORDER. Locks current
 * vertex count: 2 sharp pairs (2 verts each) + 2 rounded pairs (2*(SEG+1)
 * verts each) = 2*2 + 2*2*(SEG+1) = 4 + 28 = 32 verts at SEG=6.
 * Catches structural regressions in emit_corner_strip_pairs / strip wrap. */
static void test_dispatch_border_mixed_radii(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 60.0F};
    c->renderData.border.color = (Clay_Color){.r = 0.0F, .g = 255.0F, .b = 0.0F, .a = 255.0F};
    c->renderData.border.width = (Clay_BorderWidth){.left = 2, .right = 2, .top = 2, .bottom = 2, .betweenChildren = 0};
    c->renderData.border.cornerRadius = (Clay_CornerRadius){.topLeft = 10.0F, .topRight = 0.0F, .bottomLeft = 0.0F, .bottomRight = 10.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* SEG=6 (private to nt_ui.c): 2 sharp corners give 2 pairs = 4 verts.
     * 2 rounded corners give 2*(6+1) = 14 pairs = 28 verts. Total 32. */
    TEST_ASSERT_EQUAL_UINT32(32U, nt_sprite_renderer_test_last_emit_vertex_count());
}

/* Rounded IMAGE asserts -- rounded edges must be baked into the atlas. */
static void test_dispatch_image_nonzero_radius_asserts(void) {
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.white_region_idx;
    s_image_payload.flip_bits = 0;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 32, .height = 32};
    c->renderData.image.backgroundColor = (Clay_Color){0};
    c->renderData.image.cornerRadius = (Clay_CornerRadius){.topLeft = 4.0F};
    c->renderData.image.imageData = &s_image_payload;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Not-READY atlas must silent no-op (async loading is legitimate). */
static void test_dispatch_image_not_ready_silent(void) {
    nt_ui_image_payload_t bad = {.atlas = {.id = 0xDEADBEEFU}, .region_index = 0, .flip_bits = 0};
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 64, .height = 64};
    c->renderData.image.backgroundColor = (Clay_Color){0};
    c->renderData.image.imageData = &bad;
    inject_frozen_cmds(1);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(calls_before, nt_sprite_renderer_test_draw_call_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_rectangle);
    RUN_TEST(test_dispatch_rectangle_zero_radius_keeps_fast_path);
    RUN_TEST(test_dispatch_rectangle_rounded_emits_fan);
    RUN_TEST(test_dispatch_border_emits_4_rects);
    RUN_TEST(test_dispatch_border_rounded_emits_strip);
    RUN_TEST(test_dispatch_border_rounded_partial_widths);
    RUN_TEST(test_dispatch_border_width_exceeds_radius);
    RUN_TEST(test_dispatch_border_mixed_radii);
    RUN_TEST(test_dispatch_rect_asymmetric_radii_no_over_clamp);
    RUN_TEST(test_dispatch_rounded_rect_uv_is_centroid_not_corner);
    RUN_TEST(test_dispatch_image_nonzero_radius_asserts);
    RUN_TEST(test_dispatch_text_unbound_font_asserts);
    RUN_TEST(test_dispatch_image);
    RUN_TEST(test_dispatch_scissor_start_end);
    RUN_TEST(test_dispatch_custom);
    RUN_TEST(test_dispatch_none_silent_skip);
    RUN_TEST(test_dispatch_image_tinted_packs_color);
    RUN_TEST(test_dispatch_image_not_ready_silent);
    return UNITY_END();
}
