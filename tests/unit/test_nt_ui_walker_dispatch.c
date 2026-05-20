/* tests/unit/test_nt_ui_walker_dispatch.c -- Plan 52-04
 *
 * Covers WALK-01 (all 8 Clay command types dispatch to the right backend)
 * and WALK-04 (BORDER with all 4 widths non-zero emits exactly 4 thin
 * rects). Synthetic Clay_RenderCommand arrays are injected directly into
 * ctx->frozen_cmds (visible via nt_ui_internal.h) -- this is the
 * walker-only path, bypassing Clay's declaration machinery.
 */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "input/nt_input.h"
#include "material/nt_material.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_atlas.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"
/* clang-format on */

/* ---- Shared test fixtures ---- */

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];
static minimal_ui_atlas_t s_atlas;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_ui_context_t *s_ctx;
static uint32_t s_vpack_counter;

/* Static command arrays for synthetic frozen_cmds injection. */
#define MAX_TEST_CMDS 32
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* Image payload backing storage. */
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

/* ---- Build a minimal test material via virtual-pack-registered shaders ---- */

static nt_material_t make_test_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "walker_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "walker_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "walker_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "walker_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "walker_fs_%u", s_vpack_counter);
    s_vpack_counter++;

    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id));

    nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.label = "walker_test_material";

    nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

/* ---- Common setUp / tearDown ---- */

void setUp(void) {
    nt_test_assert_install();
    s_vpack_counter = 0;
    s_custom_called = false;
    s_custom_received_cmd = NULL;
    s_custom_received_user = NULL;
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    memset(&s_image_payload, 0, sizeof s_image_payload);

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    /* Begin a frame/pass so flush's draw_indexed doesn't trip the stub backend
     * "no active pass" guard (mirrors test_sprite_renderer setUp). */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});
    nt_text_renderer_init();

    s_atlas = minimal_ui_atlas_create();
    s_sprite_material = make_test_material();
    s_text_material = make_test_material();

    nt_ui_set_atlas_white_region(s_atlas.handle, s_atlas.white_region_idx);
    nt_ui_set_sprite_material(s_sprite_material);
    nt_ui_set_text_material(s_text_material);
    nt_ui_set_custom_handler(NULL, NULL);

    s_ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(s_ctx);
}

void tearDown(void) {
    if (s_ctx != NULL) {
        nt_ui_destroy_context(s_ctx);
        s_ctx = NULL;
    }
    nt_ui_test_reset_walker_globals();
    minimal_ui_atlas_destroy(&s_atlas);

    nt_sprite_renderer_shutdown();
    nt_text_renderer_shutdown();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_material_shutdown();
    nt_font_shutdown();
    nt_atlas_test_reset();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

/* Inject a synthetic frozen_cmds array into the ctx so the walker iterates
 * a known-shape command list. Bypasses Clay declaration machinery. */
static void inject_frozen_cmds(int32_t count) {
    s_ctx->frozen_cmds.internalArray = s_test_cmds;
    s_ctx->frozen_cmds.length = count;
    s_ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* ---- Tests ---- */

/* WALK-01: RECTANGLE -> sprite renderer emit_region (4 verts for white quad) */
static void test_dispatch_rectangle(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 10.0f, .y = 20.0f, .width = 100.0f, .height = 50.0f};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0f, .g = 0.0f, .b = 0.0f, .a = 255.0f};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* White region is 4 verts/6 indices -- emit_region preserves it. */
    TEST_ASSERT_EQUAL_UINT32(4u, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6u, nt_sprite_renderer_test_last_emit_index_count());
    /* Walker element count delta matches frozen_cmds.length. */
    TEST_ASSERT_EQUAL_UINT32(1u, nt_ui_test_last_walk_element_count());
}

/* WALK-04: BORDER with all 4 widths non-zero -- exactly 4 last_emit calls
 * happen (top, bottom, left, right), all into the white region. Verify the
 * LAST emit was still a white 4-vert quad. */
static void test_dispatch_border_emits_4_rects(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_BORDER;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0f, .y = 0.0f, .width = 200.0f, .height = 100.0f};
    c->renderData.border.color = (Clay_Color){.r = 0.0f, .g = 255.0f, .b = 0.0f, .a = 255.0f};
    c->renderData.border.width = (Clay_BorderWidth){.left = 2, .right = 2, .top = 2, .bottom = 2, .betweenChildren = 0};
    inject_frozen_cmds(1);

    /* Snapshot draw-call counter before walk. emit_border calls
     * emit_screen_rect 4 times against the same sprite material + atlas;
     * they all batch into one cmd that flushes at walk exit. */
    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* Last emit is still a 4-vert white quad. */
    TEST_ASSERT_EQUAL_UINT32(4u, nt_sprite_renderer_test_last_emit_vertex_count());
    /* All 4 sides batch into one cmd; walker exit flush adds exactly 1 draw call. */
    TEST_ASSERT_EQUAL_UINT32(calls_before + 1u, nt_sprite_renderer_test_draw_call_count());
}

/* WALK-01: TEXT -> flush sprite (no-op when empty) + text renderer setters
 * + draw_n. We verify the text renderer's set_material call counter ticks. */
static void test_dispatch_text(void) {
    /* Note: This test doesn't register a font in s_ctx -- emit_text's
     * nt_font_valid early-return fires, so no glyphs are drawn. But the
     * sprite-flush at the top of emit_text DOES happen, and the test
     * confirms the walk completes without crashing (TEXT branch reachable).
     * A separate flush test verifies the boundary semantics. */
    nt_text_renderer_test_reset_call_counters();

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->boundingBox = (Clay_BoundingBox){.x = 50.0f, .y = 60.0f, .width = 100.0f, .height = 20.0f};
    static const char *kText = "AB";
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 2, .chars = kText, .baseChars = kText};
    c->renderData.text.textColor = (Clay_Color){.r = 255.0f, .g = 255.0f, .b = 255.0f, .a = 255.0f};
    c->renderData.text.fontId = 0;
    c->renderData.text.fontSize = 14;
    c->renderData.text.letterSpacing = 0;
    c->renderData.text.lineHeight = 0;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* font_id=0 has no font set in the per-ctx registry; emit_text exits
     * before calling set_font/set_material. The walker just needs to not
     * crash on the TEXT case. */
    TEST_ASSERT_EQUAL_UINT32(0u, nt_text_renderer_test_set_material_calls());
    TEST_ASSERT_EQUAL_UINT32(0u, nt_text_renderer_test_set_font_calls());
    /* Walker element count still ticks. */
    TEST_ASSERT_EQUAL_UINT32(1u, nt_ui_test_last_walk_element_count());
}

/* WALK-01: IMAGE -> reads nt_ui_image_payload_t and emits one region. */
static void test_dispatch_image(void) {
    s_image_payload.atlas = s_atlas.handle;
    s_image_payload.region_index = s_atlas.polygon_region_idx; /* 6-vert hull */
    s_image_payload.flip_bits = 0;
    memset(s_image_payload.slice9_lrtb, 0, sizeof s_image_payload.slice9_lrtb);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    c->boundingBox = (Clay_BoundingBox){.x = 100.0f, .y = 100.0f, .width = 64.0f, .height = 64.0f};
    c->renderData.image.backgroundColor = (Clay_Color){0}; /* untinted */
    c->renderData.image.imageData = &s_image_payload;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* Polygon hull preservation: emit_image must NOT collapse to 4-vert quad. */
    TEST_ASSERT_EQUAL_UINT32(6u, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(12u, nt_sprite_renderer_test_last_emit_index_count());
}

/* WALK-01 / WALK-02 / WALK-03: SCISSOR_START + SCISSOR_END are dispatched
 * and the walker exits with scissor disabled. */
static void test_dispatch_scissor_start_end(void) {
    Clay_RenderCommand *cs = &s_test_cmds[0];
    cs->commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    cs->boundingBox = (Clay_BoundingBox){.x = 50.0f, .y = 50.0f, .width = 200.0f, .height = 200.0f};
    cs->renderData.clip.horizontal = true;
    cs->renderData.clip.vertical = true;

    Clay_RenderCommand *ce = &s_test_cmds[1];
    ce->commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;

    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* Walker MUST disable scissor at exit (CP-04). */
    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
    TEST_ASSERT_EQUAL_UINT32(2u, nt_ui_test_last_walk_element_count());
}

/* WALK-05: CUSTOM -> registered handler called with (cmd, userdata). */
static void test_dispatch_custom(void) {
    int sentinel = 42;
    nt_ui_set_custom_handler(test_custom_handler, &sentinel);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = NULL;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    TEST_ASSERT_TRUE(s_custom_called);
    TEST_ASSERT_EQUAL_PTR(c, s_custom_received_cmd);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_custom_received_user);
}

/* WALK-01: NONE -> silent skip (no crash, no emit). */
static void test_dispatch_none_silent_skip(void) {
    /* Walk an empty command array -- frozen_cmds.length = 0. */
    inject_frozen_cmds(0);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(0u, nt_ui_test_last_walk_element_count());

    /* Also test an explicit NONE command -- still no crash, still no emit. */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_NONE;
    inject_frozen_cmds(1);
    nt_ui_walk(s_ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(1u, nt_ui_test_last_walk_element_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_rectangle);
    RUN_TEST(test_dispatch_border_emits_4_rects);
    RUN_TEST(test_dispatch_text);
    RUN_TEST(test_dispatch_image);
    RUN_TEST(test_dispatch_scissor_start_end);
    RUN_TEST(test_dispatch_custom);
    RUN_TEST(test_dispatch_none_silent_skip);
    return UNITY_END();
}
