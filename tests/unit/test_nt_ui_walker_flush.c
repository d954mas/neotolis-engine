/* tests/unit/test_nt_ui_walker_flush.c -- Plan 52-04
 *
 * Covers WALK-06 / D-52-18: walker flushes both sprite + text renderers
 * at scissor / text boundaries and at walk exit. Probes the renderer's
 * test-access draw_call_count / vertex_count counters.
 */

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
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "stats/nt_stats.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_atlas.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"
/* clang-format on */

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];
static minimal_ui_atlas_t s_atlas;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_ui_context_t *s_ctx;
static uint32_t s_vpack_counter;

#define MAX_TEST_CMDS 8
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

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

void setUp(void) {
    nt_test_assert_install();
    s_vpack_counter = 0;
    memset(s_test_cmds, 0, sizeof s_test_cmds);

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});
    nt_text_renderer_init();

    /* nt_stats_init required: Plan 52-05 wired nt_stats_count("ui_draw_calls", ...)
     * + nt_stats_count("ui_element_count", ...) at nt_ui_walk exit. Without init,
     * nt_stats_count's NT_ASSERT(s_stats.initialized) trips on every walk. */
    nt_stats_init(NULL);

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
    nt_stats_shutdown();
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

static void inject_frozen_cmds(int32_t count) {
    s_ctx->frozen_cmds.internalArray = s_test_cmds;
    s_ctx->frozen_cmds.length = count;
    s_ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* WALK-06: walker exit flushes both sprite and text renderers. After
 * emitting 1 RECT into the sprite renderer's staging, the staging vertex
 * count is non-zero. After nt_ui_walk returns, the walker's exit-flush
 * MUST drain that staging back to 0. */
static void test_walker_exit_flushes_sprite_and_text(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 50, .height = 50};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 128, .g = 128, .b = 128, .a = 255};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_ctx, &target);

    /* After flush, staging vertex_count resets to 0. */
    TEST_ASSERT_EQUAL_UINT32(0u, nt_sprite_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0u, nt_text_renderer_test_vertex_count());
}

/* WALK-06: SCISSOR_START/END flushes both renderers before changing
 * scissor state. Sequence: RECT (accumulates) -> SCISSOR_START (must
 * flush sprite) -> RECT (accumulates again) -> SCISSOR_END (must flush
 * sprite) -> walk-exit flush. */
static void test_flush_on_scissor_transition(void) {
    Clay_RenderCommand *cmds = s_test_cmds;
    cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 0, .b = 0, .a = 255};
    cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    cmds[1].renderData.clip.horizontal = true;
    cmds[1].renderData.clip.vertical = true;
    cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    cmds[2].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    cmds[2].renderData.rectangle.backgroundColor = (Clay_Color){.r = 0, .g = 255, .b = 0, .a = 255};
    cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(4);

    const uint32_t calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_ctx, &target);

    /* scissor_push flushes the first RECT (1 draw call), then scissor_pop
     * flushes the second RECT (1 more). The walker-exit flush adds 0 more
     * because pop already drained staging. So delta == 2. */
    TEST_ASSERT_EQUAL_UINT32(calls_before + 2u, nt_sprite_renderer_test_draw_call_count());
}

/* WALK-06: RECT -> TEXT transition flushes sprite renderer before text
 * emit begins. We verify by checking that one draw call happened mid-
 * walk (sprite flush at TEXT boundary), even though the test font is
 * NOT registered (so the actual text path early-returns). The sprite
 * flush is unconditional at the top of emit_text. */
static void test_flush_on_rect_to_text_transition(void) {
    Clay_RenderCommand *cmds = s_test_cmds;
    cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    cmds[0].renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};

    static const char *kText = "X";
    cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    cmds[1].boundingBox = (Clay_BoundingBox){.x = 20, .y = 20, .width = 10, .height = 10};
    cmds[1].renderData.text.stringContents = (Clay_StringSlice){.length = 1, .chars = kText, .baseChars = kText};
    cmds[1].renderData.text.textColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    cmds[1].renderData.text.fontId = 0;
    cmds[1].renderData.text.fontSize = 14;
    inject_frozen_cmds(2);

    const uint32_t sprite_calls_before = nt_sprite_renderer_test_draw_call_count();
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_ctx, &target);

    /* emit_text always flushes sprite at the top. That flush drains the
     * RECT staging into one draw call. Walker-exit flush adds nothing
     * (already drained). So delta == 1. */
    TEST_ASSERT_EQUAL_UINT32(sprite_calls_before + 1u, nt_sprite_renderer_test_draw_call_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_walker_exit_flushes_sprite_and_text);
    RUN_TEST(test_flush_on_scissor_transition);
    RUN_TEST(test_flush_on_rect_to_text_transition);
    return UNITY_END();
}
