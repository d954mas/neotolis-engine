/* Front-most input arbitration (2D ctx).
 *
 * Among overlapping interactive widgets only the front-most (highest effective Clay zIndex; equal
 * zIndex → last-declared) may hover/press a free pointer; those behind it are gated off. Arbitration
 * uses the PREVIOUS frame's interactive registry, so frame 1 (empty registry) falls back to the raw
 * hit — both react — and arbitration kicks in from frame 2. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Two overlapping floating boxes; the pointer sits in the shared region (275,275). */
#define BOT_X 100.0F
#define BOT_Y 100.0F
#define TOP_X 200.0F
#define TOP_Y 200.0F
#define BOX_W 250.0F
#define BOX_H 250.0F
#define OVER_CX 275.0F
#define OVER_CY 275.0F

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_pointer_t make_pointer(float x, float y, bool is_down, bool is_pressed) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    return p;
}

/* Two overlapping floating widgets; "top" declared last → drawn on top in 2D. */
static void declare_two(void) {
    CLAY({.id = CLAY_ID("bottom"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BOT_X, .y = BOT_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BOX_W), CLAY_SIZING_FIXED(BOX_H)}}}) {}
    CLAY({.id = CLAY_ID("top"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = TOP_X, .y = TOP_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BOX_W), CLAY_SIZING_FIXED(BOX_H)}}}) {}
}

/* Frame 1 helper: declare only (no step) so Clay bakes the bbox/transform for next frame's hit-test.
 * Stepping (hit-testing) on the very first frame is invalid — the transform isn't baked until end. */
static void declare_only(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_two();
    nt_ui_end(s_fx.ctx);
}

/* Declare both + step both (order: bottom, then top); returns both interactions. Also records both in
 * this frame's interactive registry, consumed by NEXT frame's resolve. */
static void step_two(const nt_pointer_t *p, nt_ui_interaction_t *bottom, nt_ui_interaction_t *top) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_two();
    *bottom = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("bottom"));
    *top = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("top"));
    nt_ui_end(s_fx.ctx);
}

/* Registry empty (nothing stepped last frame) → hot==0 fallback → both overlapping widgets hover. */
void test_empty_registry_fallback_both_hover(void) {
    nt_pointer_t over = make_pointer(OVER_CX, OVER_CY, false, false);
    nt_ui_interaction_t bottom;
    nt_ui_interaction_t top;
    declare_only(&over);            /* frame 1: bake bboxes, no registry */
    step_two(&over, &bottom, &top); /* frame 2: resolve sees empty registry → fallback */
    TEST_ASSERT_TRUE(bottom.hovered);
    TEST_ASSERT_TRUE(top.hovered);
}

/* Once both are in the registry, the resolve makes "top" (last-declared) hot; "bottom" is gated off. */
void test_overlap_top_wins_hover(void) {
    nt_pointer_t over = make_pointer(OVER_CX, OVER_CY, false, false);
    nt_ui_interaction_t bottom;
    nt_ui_interaction_t top;
    declare_only(&over);            /* frame 1: bake bboxes */
    step_two(&over, &bottom, &top); /* frame 2: register both (still fallback) */
    step_two(&over, &bottom, &top); /* frame 3: arbitrated from frame-2 registry */
    TEST_ASSERT_TRUE(top.hovered);
    TEST_ASSERT_FALSE(bottom.hovered);
}

/* Two overlapping floating widgets where the HIGHER zIndex is declared FIRST: true z-order must pick
 * it (the old "last-declared wins" would wrongly pick the later, lower-z one). */
static void declare_zindexed(void) {
    CLAY({.id = CLAY_ID("hiz"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10, .offset = {.x = BOT_X, .y = BOT_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BOX_W), CLAY_SIZING_FIXED(BOX_H)}}}) {}
    CLAY({.id = CLAY_ID("lowz"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = TOP_X, .y = TOP_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BOX_W), CLAY_SIZING_FIXED(BOX_H)}}}) {}
}

static void declare_zindexed_only(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_zindexed();
    nt_ui_end(s_fx.ctx);
}

static void step_zindexed(const nt_pointer_t *p, nt_ui_interaction_t *hiz, nt_ui_interaction_t *lowz) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_zindexed();
    *hiz = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("hiz"));
    *lowz = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("lowz"));
    nt_ui_end(s_fx.ctx);
}

void test_higher_zindex_wins_despite_earlier_declaration(void) {
    nt_pointer_t over = make_pointer(OVER_CX, OVER_CY, false, false);
    nt_ui_interaction_t hiz;
    nt_ui_interaction_t lowz;
    declare_zindexed_only(&over);      /* frame 1: bake */
    step_zindexed(&over, &hiz, &lowz); /* frame 2: register (still fallback) */
    step_zindexed(&over, &hiz, &lowz); /* frame 3: arbitrated by zIndex */
    TEST_ASSERT_TRUE(hiz.hovered);     /* z=10 wins even though declared first */
    TEST_ASSERT_FALSE(lowz.hovered);
}

/* Press lands on the front-most widget only; the one behind never captures. */
void test_overlap_top_wins_press(void) {
    nt_pointer_t over = make_pointer(OVER_CX, OVER_CY, false, false);
    nt_ui_interaction_t bottom;
    nt_ui_interaction_t top;
    declare_only(&over);            /* frame 1: bake bboxes */
    step_two(&over, &bottom, &top); /* frame 2: register both */

    nt_pointer_t press = make_pointer(OVER_CX, OVER_CY, true, true);
    step_two(&press, &bottom, &top); /* frame 3: press arbitrated */
    TEST_ASSERT_TRUE(top.pressed_now);
    TEST_ASSERT_FALSE(bottom.pressed_now);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_registry_fallback_both_hover);
    RUN_TEST(test_overlap_top_wins_hover);
    RUN_TEST(test_higher_zindex_wins_despite_earlier_declaration);
    RUN_TEST(test_overlap_top_wins_press);
    return UNITY_END();
}
