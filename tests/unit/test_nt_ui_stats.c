/* tests/unit/test_nt_ui_stats.c -- Plan 52-05
 *
 * Covers WALK-09 / D-52-20: ui_draw_calls + ui_element_count user counters
 * are routed through nt_stats_count at nt_ui_walk exit.
 *
 * nt_stats has no public read-back-by-name accessor for user counters --
 * verification goes through (a) nt_ui_test_last_walk_* probes (per-walk
 * statics that Plan 04 captures and Plan 05 routes to nt_stats_count) and
 * (b) nt_stats_format_lines substring match (covers the wiring through to
 * nt_stats' user-counter table).
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

/* ---- Shared test fixtures ---- */

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];
static minimal_ui_atlas_t s_atlas;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_ui_context_t *s_ctx;
static uint32_t s_vpack_counter;

#define MAX_TEST_CMDS 8
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

static nt_material_t make_test_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "stats_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "stats_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "stats_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "stats_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "stats_fs_%u", s_vpack_counter);
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
    desc.label = "stats_test_material";

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

    /* Begin a frame/pass so flush's draw_indexed doesn't trip the stub backend
     * "no active pass" guard. */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});
    nt_text_renderer_init();

    /* Stats module under test -- default capacity is plenty for ui_draw_calls
     * + ui_element_count (only 2 counters). */
    nt_stats_init(NULL);

    s_atlas = minimal_ui_atlas_create();
    s_sprite_material = make_test_material();
    s_text_material = make_test_material();

    s_ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(s_ctx);

    nt_ui_set_atlas_white_region(s_ctx, s_atlas.handle, s_atlas.white_region_idx);
    nt_ui_set_sprite_material(s_ctx, s_sprite_material);
    nt_ui_set_text_material(s_ctx, s_text_material);
    nt_ui_set_custom_handler(s_ctx, NULL, NULL);
}

void tearDown(void) {
    if (s_ctx != NULL) {
        nt_ui_destroy_context(s_ctx);
        s_ctx = NULL;
    }
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

/* WALK-09 / D-52-20: after a walk that emits a RECT, the walker's per-walk
 * draw-call delta probe is > 0 (at least the walker-exit flush ticked the
 * gfx draw-call counter) AND nt_stats_format_lines surfaces the
 * "ui_draw_calls" line (proving the value was routed into nt_stats). */
static void test_ui_draw_calls_counter_set(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 100.0f};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0f, .g = 0.0f, .b = 0.0f, .a = 255.0f};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* Per-walk delta probe: at least the walker-exit flush ticked one draw call. */
    const uint32_t delta = nt_ui_test_last_walk_draw_call_delta(s_ctx);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, delta);

    /* nt_stats wiring: the counter must surface in format_lines. */
    char buf[512];
    uint32_t n = nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, n);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_draw_calls:"), "nt_stats must contain ui_draw_calls counter after walk");

    /* The delta value must match what nt_stats holds -- verify via
     * substring of the formatted counter line. */
    char expected[64];
    (void)snprintf(expected, sizeof expected, "ui_draw_calls: %u", (unsigned)delta);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, expected), "ui_draw_calls value in nt_stats must match per-walk delta probe");
}

/* WALK-09 / D-52-20: ui_element_count equals frozen_cmds.length and is
 * routed into nt_stats. */
static void test_ui_element_count_counter_set(void) {
    /* 3 RECT commands -> walker iterates 3 elements. */
    for (int i = 0; i < 3; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 20), .y = 0.0f, .width = 10.0f, .height = 10.0f};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 0.0f, .g = 255.0f, .b = 0.0f, .a = 255.0f};
    }
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    /* Element count probe matches frozen_cmds.length exactly (no Clay
     * wrapper elements -- frozen_cmds is the injected synthetic array). */
    TEST_ASSERT_EQUAL_UINT32(3u, nt_ui_test_last_walk_element_count(s_ctx));

    /* nt_stats wiring. */
    char buf[512];
    (void)nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_element_count: 3"), "nt_stats ui_element_count must equal frozen_cmds.length");
}

/* WALK-09 / D-52-20: counters are SET per walk, not accumulated. Walk twice
 * with different command counts -- the second walk's counter must reflect
 * the second declaration. */
static void test_counters_reset_per_walk(void) {
    /* First walk: 2 RECT commands. */
    for (int i = 0; i < 2; ++i) {
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
        c->boundingBox = (Clay_BoundingBox){.x = 0.0f, .y = 0.0f, .width = 10.0f, .height = 10.0f};
        c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255.0f, .g = 255.0f, .b = 255.0f, .a = 255.0f};
    }
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0f, 0.0f, 800.0f, 600.0f}};
    nt_ui_walk(s_ctx, &target);

    const uint32_t count1 = nt_ui_test_last_walk_element_count(s_ctx);
    TEST_ASSERT_EQUAL_UINT32(2u, count1);

    /* Second walk: empty command array. */
    inject_frozen_cmds(0);
    nt_ui_walk(s_ctx, &target);

    const uint32_t count2 = nt_ui_test_last_walk_element_count(s_ctx);
    TEST_ASSERT_EQUAL_UINT32(0u, count2);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(count1, count2, "second walk's element count must reflect the second declaration, not accumulate");

    /* nt_stats also reflects the second walk's value, not the first. */
    char buf[512];
    (void)nt_stats_format_lines(buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ui_element_count: 0"), "nt_stats ui_element_count must reflect second walk (=0), not first (=2)");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "ui_element_count: 2"), "old (first-walk) value must be overwritten in nt_stats");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_draw_calls_counter_set);
    RUN_TEST(test_ui_element_count_counter_set);
    RUN_TEST(test_counters_reset_per_walk);
    return UNITY_END();
}
