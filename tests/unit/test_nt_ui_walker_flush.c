#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

/* Painter order of the walker: sprites and text record their draws in command order, and
 * clip changes land between the draws they separate. */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 8
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

void setUp(void) {
    nt_test_assert_install();
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) { ui_walker_fixture_inject_cmds(&s_fx, s_test_cmds, count, MAX_TEST_CMDS); }

static void make_rect(int idx, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
}

static const char s_text[] = "X";

static void make_text(int idx, float x) {
    Clay_RenderCommand *c = &s_test_cmds[idx];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_TEXT;
    c->boundingBox = (Clay_BoundingBox){.x = x, .y = 0, .width = 10, .height = 10};
    c->renderData.text.stringContents = (Clay_StringSlice){.length = 1, .chars = s_text, .baseChars = s_text};
    c->renderData.text.textColor = (Clay_Color){.r = 255, .g = 255, .b = 255, .a = 255};
    c->renderData.text.fontId = 0;
    c->renderData.text.fontSize = 14;
}

/* The fixture's stub font draws nothing; TEXT needs a font with glyphs. */
static void bind_real_font(void) { nt_ui_set_font(s_fx.ctx, 0U, ui_walker_fixture_make_real_font(&s_fx)); }

static uint32_t material_program_id(nt_material_t mat) { return nt_material_get_info(mat)->program.id; }

static void walk(void) {
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
}

/* SPRITE -> TEXT -> SPRITE: the text draw sits between the two sprite draws, so the sprites do not
 * merge across it and the second sprite's indices follow the first's. */
static void test_sprite_text_sprite_records_in_command_order(void) {
    bind_real_font();
    make_rect(0, 0);
    make_text(1, 20);
    make_rect(2, 40);
    inject_frozen_cmds(3);

    nt_gfx_fake_draw_trace_reset(true);
    walk();
    ui_walker_fixture_end_frame(&s_fx);

    TEST_ASSERT_EQUAL_UINT32(3U, nt_gfx_fake_draw_trace_count());
    const nt_gfx_fake_draw_t first = nt_gfx_fake_draw_trace_at(0);
    const nt_gfx_fake_draw_t text = nt_gfx_fake_draw_trace_at(1);
    const nt_gfx_fake_draw_t second = nt_gfx_fake_draw_trace_at(2);
    const uint32_t sprite_program = material_program_id(s_fx.sprite_material);
    TEST_ASSERT_EQUAL_UINT32(sprite_program, first.program.id);
    TEST_ASSERT_EQUAL_UINT32(material_program_id(s_fx.text_material), text.program.id);
    TEST_ASSERT_EQUAL_UINT32(sprite_program, second.program.id);
    TEST_ASSERT_EQUAL_UINT32(6U, first.num_indices);
    TEST_ASSERT_EQUAL_UINT32(6U, second.num_indices);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(first.first_index + first.num_indices, second.first_index);
}

/* Probes run at CUSTOM commands around SCISSOR_START; the callback records nothing. */
static uint32_t s_probe_draws[2];
static uint32_t s_probe_scissor_enables[2];
static uint32_t s_probe_count;

static void probe_cb(const nt_ui_custom_frame_t *frame, void *user) {
    (void)frame;
    (void)user;
    TEST_ASSERT_TRUE(s_probe_count < 2U);
    s_probe_draws[s_probe_count] = nt_gfx_draw_calls(&g_nt_gfx.counters);
    s_probe_scissor_enables[s_probe_count] = g_nt_gfx.counters.accepted[NT_GFX_OP_SCISSOR_ENABLE];
    ++s_probe_count;
}

/* RECT, SCISSOR_START, RECT, SCISSOR_END with one material and one page: only the clip change splits
 * the rects, and the scissor enable is recorded after the first draw and before the second. */
static void test_scissor_change_splits_sprite_draws(void) {
    static nt_ui_custom_data_t probe = {.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    s_probe_count = 0;
    nt_ui_set_custom_handler(s_fx.ctx, probe_cb, NULL);

    make_rect(0, 0);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    s_test_cmds[1].renderData.custom.customData = &probe;
    s_test_cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[2].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    s_test_cmds[2].renderData.clip.horizontal = true;
    s_test_cmds[2].renderData.clip.vertical = true;
    s_test_cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    s_test_cmds[3].renderData.custom.customData = &probe;
    make_rect(4, 20);
    s_test_cmds[5].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(6);

    const uint32_t draws_before = nt_gfx_draw_calls(&g_nt_gfx.counters);
    walk();

    TEST_ASSERT_EQUAL_UINT32(2U, s_probe_count);
    TEST_ASSERT_EQUAL_UINT32(draws_before + 1U, s_probe_draws[0]);
    TEST_ASSERT_EQUAL_UINT32(draws_before + 1U, s_probe_draws[1]);
    TEST_ASSERT_EQUAL_UINT32(s_probe_scissor_enables[0] + 1U, s_probe_scissor_enables[1]);
    TEST_ASSERT_EQUAL_UINT32(draws_before + 2U, nt_gfx_draw_calls(&g_nt_gfx.counters));
}

/* Text on both sides of a clip change: each keeps its own clip, so the two runs do not merge. */
static void test_scissor_change_splits_text_draws(void) {
    bind_real_font();
    make_text(0, 0);
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    s_test_cmds[1].renderData.clip.horizontal = true;
    s_test_cmds[1].renderData.clip.vertical = true;
    make_text(2, 20);
    s_test_cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(4);

    const uint32_t draws_before = nt_gfx_draw_calls(&g_nt_gfx.counters);
    walk();

    TEST_ASSERT_EQUAL_UINT32(draws_before + 2U, nt_gfx_draw_calls(&g_nt_gfx.counters));
}

/* The same two texts without a clip change merge into one draw: the split above is the scissor's. */
static void test_compatible_texts_merge(void) {
    bind_real_font();
    make_text(0, 0);
    make_text(1, 20);
    inject_frozen_cmds(2);

    nt_gfx_fake_draw_trace_reset(true);
    walk();
    ui_walker_fixture_end_frame(&s_fx);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(2U, ui_walker_fx_quads(nt_gfx_fake_draw_trace_at(0)));
}

static const char s_text_two[] = "XY";

/* The ctx text depth bias reaches the renderer per glyph: the second glyph sits one bias step nearer. */
static void test_text_material_depth_bias_reaches_glyphs(void) {
    bind_real_font();
    nt_ui_set_text_material(s_fx.ctx, s_fx.text_material, 0.25F);
    make_text(0, 0);
    s_test_cmds[0].renderData.text.stringContents = (Clay_StringSlice){.length = 2, .chars = s_text_two, .baseChars = s_text_two};
    inject_frozen_cmds(1);

    nt_gfx_fake_draw_trace_reset(true);
    walk();
    ui_walker_fixture_end_frame(&s_fx);

    const nt_gfx_fake_draw_t d = nt_gfx_fake_draw_trace_at(0);
    TEST_ASSERT_EQUAL_UINT32(2U, ui_walker_fx_quads(d));
    for (uint32_t corner = 0; corner < 4U; corner++) {
        TEST_ASSERT_TRUE(ui_walker_fx_vertex_float(ui_walker_fx_text_vertex(d, 0, corner), UI_WALKER_FX_TEXT_DEPTH_BIAS) == 0.0F);
        TEST_ASSERT_TRUE(ui_walker_fx_vertex_float(ui_walker_fx_text_vertex(d, 1, corner), UI_WALKER_FX_TEXT_DEPTH_BIAS) == 0.25F);
    }
}

/* The same two rects without a clip change merge into one draw: the split above is the scissor's. */
static void test_compatible_sprites_merge(void) {
    make_rect(0, 0);
    make_rect(1, 20);
    inject_frozen_cmds(2);

    const uint32_t draws_before = nt_gfx_draw_calls(&g_nt_gfx.counters);
    walk();

    TEST_ASSERT_EQUAL_UINT32(draws_before + 1U, nt_gfx_draw_calls(&g_nt_gfx.counters));
}

/* One material walked in two passes of one frame: a pass resets the bound state, so the second
 * pass binds the pipeline again and draws with it. */
static void test_one_material_two_passes_rebinds(void) {
    make_rect(0, 0);
    inject_frozen_cmds(1);

    nt_gfx_fake_draw_trace_reset(true);
    const uint32_t binds_before = nt_gfx_fake_bind_pipeline_count();
    walk();
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    walk();
    ui_walker_fixture_end_frame(&s_fx);

    TEST_ASSERT_EQUAL_UINT32(binds_before + 2U, nt_gfx_fake_bind_pipeline_count());
    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_draw_trace_count());
    const nt_gfx_fake_draw_t first = nt_gfx_fake_draw_trace_at(0);
    const nt_gfx_fake_draw_t second = nt_gfx_fake_draw_trace_at(1);
    TEST_ASSERT_EQUAL_UINT32(first.pipeline.id, second.pipeline.id);
    TEST_ASSERT_EQUAL_UINT32(material_program_id(s_fx.sprite_material), second.program.id);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sprite_text_sprite_records_in_command_order);
    RUN_TEST(test_scissor_change_splits_sprite_draws);
    RUN_TEST(test_scissor_change_splits_text_draws);
    RUN_TEST(test_compatible_texts_merge);
    RUN_TEST(test_text_material_depth_bias_reaches_glyphs);
    RUN_TEST(test_compatible_sprites_merge);
    RUN_TEST(test_one_material_two_passes_rebinds);
    return UNITY_END();
}
