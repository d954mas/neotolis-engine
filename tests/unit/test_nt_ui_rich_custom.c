#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "color/nt_color.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static uint32_t s_effect_calls;
static uint32_t s_draw_calls;
static float s_draw_y[2];
static float s_draw_x;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_effect_calls = 0U;
    s_draw_calls = 0U;
    s_draw_y[0] = 0.0F;
    s_draw_y[1] = 0.0F;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_ui_rich_fx_result_t custom_effect(uint32_t index, nt_rich_atom_kind_t kind, const float xy[2], const float wh[2], uint32_t color, float time, bool hovered, void *user) {
    (void)index;
    (void)kind;
    (void)xy;
    (void)wh;
    (void)hovered;
    s_effect_calls++;
    nt_ui_rich_fx_result_t result = nt_ui_rich_fx_identity(color);
    result.offset_y = *(const float *)user * time;
    return result;
}

static const nt_ui_rich_object_size_t s_obj_size = {.width = 20.0F, .height = 12.0F, .ascent = 12.0F};

static void draw(void *user, float x, float y, float w, float h, uint32_t color, const float world[16]) {
    (void)user;
    s_draw_x = x;
    (void)w;
    (void)h;
    (void)color;
    (void)world;
    TEST_ASSERT_TRUE(s_draw_calls < 2U);
    s_draw_y[s_draw_calls] = y;
    s_draw_calls++;
}

static void test_custom_effect_reaches_object_without_stock_module(void) {
    nt_pointer_t mouse = {0};
    float magnitude = 8.0F;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1, 0);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(60)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_object(s_fx.ctx, s_obj_size, draw, NULL);
        nt_ui_rich_push_effect_fn(s_fx.ctx, custom_effect, &magnitude);
        nt_ui_rich_object(s_fx.ctx, s_obj_size, draw, NULL);
        nt_ui_rich_pop(s_fx.ctx);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich").id, NULL, &base, 100.0F, NT_RICH_ALIGN_LEFT, 0.5F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(1U, s_effect_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, s_draw_calls);
    TEST_ASSERT_TRUE(s_draw_y[1] - s_draw_y[0] == 4.0F);
}

typedef struct {
    uint32_t draw_calls;
    float draw_x;
    float draw_y;
    uint32_t spans;
} rich_frame_result_t;

/* TEXT + LINK + OBJECT block (the link styles its span); scribble=true overwrites every byte above the head after the
 * frame's solve, i.e. the range the solve rewound, before the walk reads the block. */
static rich_frame_result_t run_rich_frame(bool scribble) {
    nt_pointer_t mouse = {0};
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    s_draw_calls = 0U;
    nt_arena_reset(&g_nt_mem_scratch);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1, 0);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_text_n(s_fx.ctx, "ab ", 3);
        nt_ui_rich_link(s_fx.ctx, 7U);
        nt_ui_rich_text_n(s_fx.ctx, "go", 2);
        nt_ui_rich_link(s_fx.ctx, 0U);
        nt_ui_rich_object(s_fx.ctx, s_obj_size, draw, NULL);
        nt_ui_rich_end(s_fx.ctx);
        nt_ui_rich_text(s_fx.ctx, CLAY_ID("rich").id, NULL, &base, 200.0F, NT_RICH_ALIGN_LEFT, 0.5F, NULL);
    }
    nt_ui_end(s_fx.ctx);
    if (scribble) {
        const size_t reuse = g_nt_mem_scratch.high_water - g_nt_mem_scratch.used;
        TEST_ASSERT_TRUE(reuse > 0U);
        memset(nt_arena_alloc(&g_nt_mem_scratch, reuse, 1U), 0xFF, reuse);
    }
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    return (rich_frame_result_t){s_draw_calls, s_draw_x, s_draw_y[0], nt_ui_rich_test_emit_span_count(s_fx.ctx)};
}

/* Walk reads only what the solve kept below its rewind mark. */
static void test_walk_after_reusing_rewound_scratch(void) {
    const rich_frame_result_t clean = run_rich_frame(false);
    const rich_frame_result_t reused = run_rich_frame(true);
    TEST_ASSERT_EQUAL_UINT32(1U, clean.draw_calls);
    TEST_ASSERT_TRUE(clean.spans > 0U);
    TEST_ASSERT_EQUAL_UINT32(clean.draw_calls, reused.draw_calls);
    TEST_ASSERT_EQUAL_UINT32(clean.spans, reused.spans);
    TEST_ASSERT_TRUE(clean.draw_x == reused.draw_x && clean.draw_y == reused.draw_y);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_effect_reaches_object_without_stock_module);
    RUN_TEST(test_walk_after_reusing_rewound_scratch);
    return UNITY_END();
}
