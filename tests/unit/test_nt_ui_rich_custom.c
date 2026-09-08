#include <stddef.h>
#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
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

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_effect_calls = 0U;
    s_draw_calls = 0U;
    s_draw_y[0] = 0.0F;
    s_draw_y[1] = 0.0F;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_ui_rich_fx_result_t custom_effect(uint32_t index, nt_rich_atom_kind_t kind, const float xy[2], const float wh[2], const float color[4], float time, bool hovered, void *user) {
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

static nt_ui_rich_object_measure_t measure(void *user) {
    (void)user;
    return (nt_ui_rich_object_measure_t){.width = 20.0F, .height = 12.0F, .ascent = 12.0F};
}

static void draw(void *user, float x, float y, float w, float h, const float color[4], const float world[16]) {
    (void)user;
    (void)x;
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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(60)}}}) {
        nt_ui_rich_begin(s_fx.ctx, &base);
        nt_ui_rich_object(s_fx.ctx, measure, draw, NULL);
        nt_ui_rich_push_effect_fn(s_fx.ctx, custom_effect, &magnitude);
        nt_ui_rich_object(s_fx.ctx, measure, draw, NULL);
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_effect_reaches_object_without_stock_module);
    return UNITY_END();
}
