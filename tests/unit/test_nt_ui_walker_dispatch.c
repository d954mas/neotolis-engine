#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include <math.h>

#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "unity.h"

/* ---- Test-local state ---- */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 32
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

static nt_ui_image_payload_t s_image_payload;

/* Custom-handler flag + receiver. */
static bool s_custom_called;
static Clay_BoundingBox s_custom_received_bbox;
static float s_custom_received_m[16];
static float s_custom_received_opacity;
static void *s_custom_received_user;

static void test_custom_handler(const nt_ui_custom_frame_t *frame, void *userdata) {
    s_custom_called = true;
    s_custom_received_bbox = ((const Clay_RenderCommand *)frame->clay_cmd)->boundingBox;
    memcpy(s_custom_received_m, frame->world_mat4, sizeof s_custom_received_m);
    s_custom_received_opacity = frame->opacity;
    s_custom_received_user = userdata;
}

/* ---- Common setUp / tearDown ---- */

void setUp(void) {
    nt_test_assert_install();
    s_custom_called = false;
    s_custom_received_bbox = (Clay_BoundingBox){0};
    memset(s_custom_received_m, 0, sizeof s_custom_received_m);
    s_custom_received_opacity = 0.0F;
    s_custom_received_user = NULL;
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    memset(&s_image_payload, 0, sizeof s_image_payload);
    s_image_payload.slice9_scale = 1.0F;

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
    TEST_ASSERT_FALSE(nt_gfx_scissor_enabled());
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_command_count(s_fx.ctx));
}

/* CUSTOM -> registered handler called with (cmd, userdata). */
static void test_dispatch_custom(void) {
    int sentinel = 42;
    nt_ui_set_custom_handler(s_fx.ctx, test_custom_handler, &sentinel);

    nt_ui_custom_data_t cd = {.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = &cd;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_TRUE(s_custom_called);
    /* bbox stays in LAYOUT (Y-down) space; handler transforms via world_mat4. */
    TEST_ASSERT_EQUAL_INT(0, (int)s_custom_received_bbox.x);
    TEST_ASSERT_EQUAL_INT(0, (int)s_custom_received_bbox.y);
    TEST_ASSERT_EQUAL_INT(10, (int)s_custom_received_bbox.width);
    TEST_ASSERT_EQUAL_INT(10, (int)s_custom_received_bbox.height);
    /* Identity world_mat4 with Y-flip: col0=(1,0,0,0), col1=(0,-1,0,0), col2=(0,0,1,0), col3=(0,vy+vh=600,0,1). */
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(s_custom_received_m[0] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)lrintf(s_custom_received_m[1] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)lrintf(s_custom_received_m[4] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(-1000, (int)lrintf(s_custom_received_m[5] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(s_custom_received_m[10] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)lrintf(s_custom_received_m[12]));
    TEST_ASSERT_EQUAL_INT(600, (int)lrintf(s_custom_received_m[13]));
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(s_custom_received_m[15] * 1000.0F));
    /* Opacity is 1.0 for an element with no XFORM userData. */
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(s_custom_received_opacity * 1000.0F));
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
 * Fixture white_region UVs: (0,FFFF)(FFFF,FFFF)(FFFF,0)(0,0), mean = 0x7FFF. */
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

    /* Vertex 1 = TL arc west point with the unswapped TL radius (input=40);
     * py = vh - (y + tl) = 600 - 0 - 40 = 560. Pins both the non-clamp
     * invariant and which radius landed where. */
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(1U, pos);
    const int32_t px = (int32_t)pos[0];
    const int32_t py = (int32_t)pos[1];
    TEST_ASSERT_TRUE(px == 0 || px == -1);
    TEST_ASSERT_GREATER_THAN_INT32(555, py);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(560, py);
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

/* Default origin (atlas 0.5,0.5) -> bbox-fill: atlas (0,8) at bbox left-mid -> GL (100,468). */
static void test_dispatch_image_non_slice9_vertex_positions(void) {
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.polygon_region_idx;
    s_image_payload.flip_bits = 0;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 100.0F, .y = 100.0F, .width = 64.0F, .height = 64.0F};
    c->renderData.image.backgroundColor = (Clay_Color){0};
    c->renderData.image.imageData = &s_image_payload;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    float v0[3];
    nt_sprite_renderer_test_last_emit_position(0U, v0);
    TEST_ASSERT_EQUAL_INT(100, (int)v0[0]);
    TEST_ASSERT_EQUAL_INT(468, (int)v0[1]);

    float v3[3];
    nt_sprite_renderer_test_last_emit_position(3U, v3);
    TEST_ASSERT_EQUAL_INT(164, (int)v3[0]);
    TEST_ASSERT_EQUAL_INT(500, (int)v3[1]);
}

/* ORIGIN_OVERRIDE(0.25, 0.75): pivot_src=(4, 12). m_lin sx_f=sy_f=4 with Y-flip
 * baked twice -> m[5]=+4. emit_region: m_translate' = (132-16, 468-48) = (116, 420).
 * v0 (0, 8): (116, 4*8+420) = (116, 452). v3 (16, 16): (180, 484). */
static void test_dispatch_image_origin_override_shifts_anchor(void) {
    s_image_payload.atlas = s_fx.atlas.handle;
    s_image_payload.region_index = s_fx.atlas.polygon_region_idx;
    s_image_payload.flip_bits = 0;
    s_image_payload.flags = NT_UI_IMAGE_ORIGIN_OVERRIDE;
    s_image_payload.origin_x = 0.25F;
    s_image_payload.origin_y = 0.75F;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 100.0F, .y = 100.0F, .width = 64.0F, .height = 64.0F};
    c->renderData.image.backgroundColor = (Clay_Color){0};
    c->renderData.image.imageData = &s_image_payload;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    float v0[3];
    nt_sprite_renderer_test_last_emit_position(0U, v0);
    TEST_ASSERT_EQUAL_INT(116, (int)v0[0]);
    TEST_ASSERT_EQUAL_INT(452, (int)v0[1]);

    float v3[3];
    nt_sprite_renderer_test_last_emit_position(3U, v3);
    TEST_ASSERT_EQUAL_INT(180, (int)v3[0]);
    TEST_ASSERT_EQUAL_INT(484, (int)v3[1]);
}

/* Stub font has units_per_em=0, so draw_n captures the model matrix without
 * emitting glyphs. m[5] = +1 (local Y-flip composes with world_aff's Y-flip);
 * m[13] = vy+vh - baseline_y_layout puts baseline at the right GL coord. */
static void test_dispatch_text_model_matrix_preserves_y_up(void) {
    nt_text_renderer_test_reset_call_counters();

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 60.0F, .width = 100.0F, .height = 20.0F};
    static const char *kText = "AB";
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 2, .chars = kText, .baseChars = kText};
    c->renderData.text.textColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    c->renderData.text.fontId = 0; /* bound stub: units_per_em=0 → silent skip after capture */
    c->renderData.text.fontSize = 14;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_draw_n_calls());
    float m[16];
    memcpy(m, nt_text_renderer_test_last_model(), sizeof m);
    /* Linear part: m[0]=1, m[5]=+1 (Y-up preserved); scaled-int compare since
     * Unity's float asserts are excluded. */
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(m[0] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)lrintf(m[1] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)lrintf(m[4] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(m[5] * 1000.0F));
    /* Translate part: ox = bbox.x = 50; baseline_y (layout) = bbox.y +
     * (bbox.h - text_h)/2 - descent*scale = 60 + 10 + 0 = 70 (stub font has
     * scale=0). GL baseline = vy+vh - 70 = 530. */
    TEST_ASSERT_EQUAL_INT(50, (int)lrintf(m[12]));
    TEST_ASSERT_EQUAL_INT(530, (int)lrintf(m[13]));
}

#if NT_UI_DEBUG_TOOLS
static void test_dispatch_3d_main_walk_skips_debug_layer(void) {
    s_fx.ctx->use_raycast_input = true;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 50.0F};
    c->userData = (void *)NT_UI_DATA_LAYER(NT_UI_LAYER_DEBUG_PANEL_BG);
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    inject_frozen_cmds(1);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(calls_before, nt_sprite_renderer_test_draw_call_count());
}

static void test_dispatch_3d_debug_layer_draws_in_screen_space_debug_walk(void) {
    s_fx.ctx->use_raycast_input = true;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 50.0F};
    c->userData = (void *)NT_UI_DATA_LAYER(NT_UI_LAYER_DEBUG_PANEL_BG);
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_debug_inspector_walk(s_fx.ctx, &target);

    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_EQUAL_INT(10, (int)lrintf(pos[0]));
    TEST_ASSERT_EQUAL_INT(530, (int)lrintf(pos[1]));
}

static void test_dispatch_3d_debug_text_uses_screen_space_orientation(void) {
    s_fx.ctx->use_raycast_input = true;

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 60.0F, .width = 100.0F, .height = 20.0F};
    static const char *kText = "AB";
    c->userData = (void *)NT_UI_DATA_LAYER(NT_UI_LAYER_DEBUG_PANEL_TEXT);
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 2, .chars = kText, .baseChars = kText};
    c->renderData.text.textColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F};
    c->renderData.text.fontId = 0;
    c->renderData.text.fontSize = 14;
    c->renderData.text.letterSpacing = 0;
    c->renderData.text.lineHeight = 0;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_debug_inspector_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_draw_n_calls());
    float m[16];
    memcpy(m, nt_text_renderer_test_last_model(), sizeof m);
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(m[0] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(1000, (int)lrintf(m[5] * 1000.0F));
    TEST_ASSERT_EQUAL_INT(50, (int)lrintf(m[12]));
    TEST_ASSERT_EQUAL_INT(530, (int)lrintf(m[13]));
}

static void test_3d_debug_inspector_walk_draws_real_tree_text(void) {
    s_fx.ctx->use_raycast_input = true;
    nt_ui_inspector_set_active(s_fx.ctx, true);
    const float identity_vp[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, identity_vp);
    CLAY({.id = CLAY_ID("debug_tree_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(120.0F), CLAY_SIZING_FIXED(40.0F)}}}) {
        nt_ui_label(s_fx.ctx, NT_UI_DATA_LAYER((nt_ui_layer_t)5), "TreeText", &(nt_ui_label_style_t){.font_id = 0, .font_size = 14, .color = {255, 255, 255, 255}});
    }
    nt_ui_end(s_fx.ctx);

    uint32_t frozen_debug_text_count = 0U;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT && c->userData != NULL && ((const nt_ui_element_data_t *)c->userData)->layer == NT_UI_LAYER_DEBUG_PANEL_TEXT) {
            TEST_ASSERT_GREATER_THAN_INT32(0, c->renderData.text.stringContents.length);
            frozen_debug_text_count++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0U, frozen_debug_text_count);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    const uint32_t calls_before = nt_text_renderer_test_draw_n_calls();
    nt_ui_debug_inspector_walk(s_fx.ctx, &target);

    TEST_ASSERT_GREATER_THAN_UINT32(calls_before, nt_text_renderer_test_draw_n_calls());
}
#endif

static void test_dispatch_3d_element_depth_bias_uses_hierarchy_depth(void) {
    s_fx.ctx->use_raycast_input = true;
    nt_ui_set_element_depth_bias(s_fx.ctx, 0.001F);
    const float identity_vp[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_set_view_proj(s_fx.ctx, identity_vp);
    CLAY({.id = CLAY_ID("bias_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(20.0F), CLAY_SIZING_FIXED(20.0F)}}, .backgroundColor = {255.0F, 255.0F, 255.0F, 255.0F}}) {}
    CLAY({.id = CLAY_ID("bias_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(20.0F), CLAY_SIZING_FIXED(20.0F)}}, .backgroundColor = {255.0F, 255.0F, 255.0F, 255.0F}}) {}
    CLAY({.id = CLAY_ID("bias_c"), .layout = {.sizing = {CLAY_SIZING_FIXED(20.0F), CLAY_SIZING_FIXED(20.0F)}}, .backgroundColor = {255.0F, 255.0F, 255.0F, 255.0F}}) {}
    CLAY({.id = CLAY_ID("bias_d"), .layout = {.sizing = {CLAY_SIZING_FIXED(20.0F), CLAY_SIZING_FIXED(20.0F)}}, .backgroundColor = {255.0F, 255.0F, 255.0F, 255.0F}}) {}
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    TEST_ASSERT_EQUAL_INT(-1, (int)lrintf(pos[2] * 1000.0F));
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
    RUN_TEST(test_dispatch_image_non_slice9_vertex_positions);
    RUN_TEST(test_dispatch_image_origin_override_shifts_anchor);
    RUN_TEST(test_dispatch_text_model_matrix_preserves_y_up);
#if NT_UI_DEBUG_TOOLS
    RUN_TEST(test_dispatch_3d_main_walk_skips_debug_layer);
    RUN_TEST(test_dispatch_3d_debug_layer_draws_in_screen_space_debug_walk);
    RUN_TEST(test_dispatch_3d_debug_text_uses_screen_space_orientation);
    RUN_TEST(test_3d_debug_inspector_walk_draws_real_tree_text);
#endif
    RUN_TEST(test_dispatch_3d_element_depth_bias_uses_hierarchy_depth);
    RUN_TEST(test_dispatch_image_not_ready_silent);
    return UNITY_END();
}
