/* Phase 56 ext: hit-zone debug overlay.
 *
 * Recording is OFF by default (zero overhead). When ON, each call to
 * nt_ui_get_interaction_padded pushes a zone record into the ctx ring.
 * Drawing is decoupled from recording.
 *
 * Tests cover:
 *   1) recording off -> debug_zone_count stays 0 even after queries
 *   2) recording on, N queries -> count==N, ids/bboxes/transforms correct
 *   3) mode filter (HOVER returns only the zone under pointer, etc.)
 *   4) drawing zero zones / max-cap (silently saturated) does not crash
 *
 * Pixel output of the overlay is intentionally NOT unit-tested -- visual
 * verification happens in the ui_buttons_demo. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_debug.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define BTN_X 120.0F
#define BTN_Y 240.0F
#define BTN_W 180.0F
#define BTN_H 60.0F
#define BTN_CX (BTN_X + (BTN_W * 0.5F))
#define BTN_CY (BTN_Y + (BTN_H * 0.5F))

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

static void declare_btn(const char *name, float x, float y) {
    /* Declared as a fresh element id each call -- callers pass distinct names. */
    Clay_ElementId eid = Clay_GetElementId((Clay_String){.length = (int32_t)strlen(name), .chars = name});
    CLAY({.id = eid, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = x, .y = y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
}

/* Sets up frame 1 (declare only) then opens frame 2 ready for queries. */
static void open_query_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
}

/* ---- Test 1: recording OFF -> zero overhead, count stays 0 ---- */
static void test_debug_recording_off_no_capture(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false);
    open_query_frame(&f1);
    TEST_ASSERT_FALSE(nt_ui_debug_get_recording(s_fx.ctx));
    /* Many queries; none should be recorded. */
    for (uint32_t i = 0; i < 5U; ++i) {
        (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    }
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 2: recording ON, 3 distinct queries -> count==3, ids match ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_debug_recording_on_records_zones(void) {
    /* Frame 1: declare 3 elements at distinct positions. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    declare_btn("btnB", BTN_X + 200.0F, BTN_Y);
    declare_btn("btnC", BTN_X, BTN_Y + 100.0F);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: turn recording on, query all 3. */
    nt_ui_debug_set_recording(s_fx.ctx, true);
    TEST_ASSERT_TRUE(nt_ui_debug_get_recording(s_fx.ctx));
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, false, false); /* hovers btnA */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    declare_btn("btnB", BTN_X + 200.0F, BTN_Y);
    declare_btn("btnC", BTN_X, BTN_Y + 100.0F);
    (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnB"));
    const int16_t pad[4] = {8, 8, 8, 8};
    (void)nt_ui_get_interaction_padded(s_fx.ctx, nt_ui_id("btnC"), pad);
    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_debug_get_zone_count(s_fx.ctx));

    /* IDs come back in query order. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btnA"), s_fx.ctx->debug_zones[0].id);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btnB"), s_fx.ctx->debug_zones[1].id);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btnC"), s_fx.ctx->debug_zones[2].id);

    /* btnA visual bbox matches its declared position. */
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->debug_zones[0].visual_l - BTN_X) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->debug_zones[0].visual_t - BTN_Y) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->debug_zones[0].visual_r - (BTN_X + BTN_W)) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->debug_zones[0].visual_b - (BTN_Y + BTN_H)) <= 0.5F);

    /* btnC padded zone is inflated by 8 on every side. */
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->debug_zones[2].layout_l - (BTN_X - 8.0F)) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(s_fx.ctx->debug_zones[2].layout_r - (BTN_X + BTN_W + 8.0F)) <= 0.5F);

    /* btnA is under the pointer -> HOVERED flag set; btnB and btnC are not. */
    TEST_ASSERT_TRUE((s_fx.ctx->debug_zones[0].state_flags & NT_UI_DEBUG_FLAG_HOVERED) != 0U);
    TEST_ASSERT_FALSE((s_fx.ctx->debug_zones[1].state_flags & NT_UI_DEBUG_FLAG_HOVERED) != 0U);
    TEST_ASSERT_FALSE((s_fx.ctx->debug_zones[2].state_flags & NT_UI_DEBUG_FLAG_HOVERED) != 0U);

    nt_ui_end(s_fx.ctx);
}

/* ---- Test 3: recording auto-clears each nt_ui_begin ---- */
static void test_debug_recording_clears_each_begin(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);

    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);

    /* Next begin must reset the zone count to 0. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4: drawing with 0 zones is a silent no-op (no crash) ---- */
static void test_debug_draw_zero_zones_safe(void) {
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    nt_ui_end(s_fx.ctx);
    /* No begin in flight, no recorded zones. Drawing must not assert/crash. */
    nt_ui_debug_draw_hit_zones(s_fx.ctx, NT_UI_DEBUG_HIT_ALL, NT_FONT_INVALID, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_debug_get_zone_count(s_fx.ctx));
}

/* ---- Test 5: drawing in OFF mode emits nothing (early return) ---- */
static void test_debug_draw_off_mode_silent(void) {
    /* Even with a recorded zone, mode==OFF returns immediately. */
    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);

    /* OFF mode must not call into the renderers (we only verify no crash). */
    nt_ui_debug_draw_hit_zones(s_fx.ctx, NT_UI_DEBUG_HIT_OFF, NT_FONT_INVALID, 0.0F);
}

/* ---- Test 6: mode filter HOVER returns only zones with HOVERED flag ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_debug_mode_filter(void) {
    /* Two buttons. Pointer hovers only btnA. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    declare_btn("btnB", BTN_X + 300.0F, BTN_Y);
    nt_ui_end(s_fx.ctx);

    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, false, false); /* hovers btnA only */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    declare_btn("btnB", BTN_X + 300.0F, BTN_Y);
    (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnB"));

    /* Inspect the recorded flags directly (drawing is a sink, not a return). */
    uint32_t hover_match = 0U;
    uint32_t total = nt_ui_debug_get_zone_count(s_fx.ctx);
    for (uint32_t i = 0; i < total; ++i) {
        if ((s_fx.ctx->debug_zones[i].state_flags & NT_UI_DEBUG_FLAG_HOVERED) != 0U) {
            hover_match++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(2U, total);
    TEST_ASSERT_EQUAL_UINT32(1U, hover_match); /* only btnA hovered */
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 7: at-cap pushes are silently saturated (no assert) ---- */
static void test_debug_cap_saturates_silently(void) {
    /* Declare one button, then call get_interaction NT_UI_DEBUG_ZONE_CAP+10 times
     * (re-querying the same id). count must clamp to the cap, no assert. */
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);

    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    const uint32_t over = NT_UI_DEBUG_ZONE_CAP + 10U;
    for (uint32_t i = 0; i < over; ++i) {
        (void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_UI_DEBUG_ZONE_CAP, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_debug_recording_off_no_capture);
    RUN_TEST(test_debug_recording_on_records_zones);
    RUN_TEST(test_debug_recording_clears_each_begin);
    RUN_TEST(test_debug_draw_zero_zones_safe);
    RUN_TEST(test_debug_draw_off_mode_silent);
    RUN_TEST(test_debug_mode_filter);
    RUN_TEST(test_debug_cap_saturates_silently);
    return UNITY_END();
}
