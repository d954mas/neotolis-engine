#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "nt_font_format.h"
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

static uint8_t *s_font_blob;

void setUp(void) {
    nt_test_assert_install();
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

/* nt_font_shutdown in the fixture destroys the real font outside the pass; its blob outlives it. */
void tearDown(void) {
    ui_walker_fixture_shutdown(&s_fx);
    free(s_font_blob);
    s_font_blob = NULL;
}

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

/* One triangle glyph 'X', so TEXT records a real draw (the fixture's stub font draws nothing). */
static void bind_real_font(void) {
    const uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    /* contour_count 1, point_count 3, all on-curve, first point (0,0), deltas (50,0) (-50,50). */
    static const uint8_t contour[14] = {1, 0, 3, 0, 0x07, 0x00, 0, 0, 0, 0, 50, 0, (uint8_t)(int8_t)-50, 50};
    const uint32_t total_size = header_size + (uint32_t)sizeof(NtFontGlyphEntry) + (uint32_t)sizeof contour;
    s_font_blob = (uint8_t *)calloc(total_size, 1);
    TEST_ASSERT_NOT_NULL(s_font_blob);

    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof hdr);
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = 1;
    hdr.units_per_em = 1000;
    hdr.ascent = 800;
    hdr.descent = -200;
    memcpy(s_font_blob, &hdr, sizeof hdr);

    NtFontGlyphEntry entry;
    memset(&entry, 0, sizeof entry);
    entry.codepoint = 'X';
    entry.data_offset = header_size + (uint32_t)sizeof(NtFontGlyphEntry);
    entry.advance = 500;
    entry.bbox_y0 = -200;
    entry.bbox_x1 = 400;
    entry.bbox_y1 = 800;
    entry.curve_count = 3;
    memcpy(s_font_blob + header_size, &entry, sizeof entry);
    memcpy(s_font_blob + entry.data_offset, contour, sizeof contour);

    const nt_font_t font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = 16});
    nt_font_add(font, nt_font_test_resource(nt_font_test_register_data(s_font_blob, total_size)));
    nt_resource_step();
    nt_font_step();
    nt_ui_set_font(s_fx.ctx, 0U, font);
}

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
    const uint32_t binds_first = nt_gfx_fake_bind_pipeline_count();
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    walk();
    const uint32_t binds_second = nt_gfx_fake_bind_pipeline_count();

    TEST_ASSERT_EQUAL_UINT32(binds_before + 1U, binds_first);
    TEST_ASSERT_EQUAL_UINT32(binds_first + 1U, binds_second);
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
    RUN_TEST(test_compatible_sprites_merge);
    RUN_TEST(test_one_material_two_passes_rebinds);
    return UNITY_END();
}
