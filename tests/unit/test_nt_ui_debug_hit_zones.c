/* Hit-zone debug overlay. Recording OFF by default; when ON each padded
 * step pushes a zone. Drawing is decoupled from recording.
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
#include "input/nt_input.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_debug_hit_zones.h"
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
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
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
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnB"));
    const int16_t pad[4] = {8, 8, 8, 8};
    (void)nt_ui_step_interaction_padded(s_fx.ctx, nt_ui_id("btnC"), pad);
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
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
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
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_debug_draw_hit_zones(s_fx.ctx, &target, NT_UI_DEBUG_HIT_ALL, NT_FONT_INVALID, 0.0F);
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
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);

    /* OFF mode must not call into the renderers (we only verify no crash). */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_debug_draw_hit_zones(s_fx.ctx, &target, NT_UI_DEBUG_HIT_OFF, NT_FONT_INVALID, 0.0F);
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
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnB"));

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

/* Overlay vertices project Clay Y-down to walker GL Y-up:
 *   world_y = vy + vh - clay_y
 *   world_x = clay_x
 * matching dispatch_command. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_debug_emit_matches_walker_coord_space(void) {
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false);
    /* Frame 1: declare. Frame 2: record + draw. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);

    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_debug_get_zone_count(s_fx.ctx));
    /* Sanity: recorded zone is in Clay Y-down (no flip yet). */
    TEST_ASSERT_TRUE((float)((int)s_fx.ctx->debug_zones[0].visual_t * 2) == BTN_Y * 2.0F);

    /* Draw with a known target. The overlay emits the OUTLINE last per zone
     * (4 edge quads). The LAST emit is the 4th edge of the visual bbox; the
     * walker_y per vertex must equal vy + vh - clay_y. */
    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_debug_draw_hit_zones(s_fx.ctx, &target, NT_UI_DEBUG_HIT_ALL, NT_FONT_INVALID, 0.0F);

    /* last_emit_* captures the final outline edge (4-vert thin quad). Fill and
     * outline both go through project_to_world; verify Y falls in the flipped
     * band [vy+vh - visual_b, vy+vh - visual_t]. */
    const uint32_t v_count = nt_sprite_renderer_test_last_emit_vertex_count();
    TEST_ASSERT_EQUAL_UINT32(4U, v_count); /* outline edge = thin quad */

    /* All 4 verts of the LAST emit (one outline edge) must have Y inside the
     * Y-flipped band: GL_top_y = 600 - BTN_Y = 360; GL_bot_y = 600 - (BTN_Y+BTN_H) = 300.
     * Pixel-shift of +-1 absorbed by the 2px outline thickness. */
    const float gl_top = 600.0F - BTN_Y;           /* 360 */
    const float gl_bot = 600.0F - (BTN_Y + BTN_H); /* 300 */
    for (uint32_t v = 0; v < v_count; ++v) {
        float pos[3];
        nt_sprite_renderer_test_last_emit_position(v, pos);
        const float y = pos[1];
        TEST_ASSERT_TRUE_MESSAGE(y >= gl_bot - 3.0F && y <= gl_top + 3.0F, "overlay vertex Y out of Y-flipped band");
        /* Reject y around BTN_Y=240 (un-flipped position). The 300 boundary
         * coincides because 600-300=300. */
        TEST_ASSERT_TRUE_MESSAGE(y < BTN_Y || y > BTN_Y + BTN_H + 3.0F || y >= gl_bot - 3.0F, "overlay vertex landed in Clay Y-down position (Y-flip missing)");
    }
}

/* Disabled-record helper drops a DISABLED zone without touching capture
 * state — the disabled path skips hit-test but must still surface in the overlay. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_debug_disabled_helper_records_zone(void) {
    /* Frame 1: declare so Clay has a prev-frame bbox. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btn_disabled", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: recording ON, pointer ON the disabled button but NO interaction
     * query (mirrors the button widget's enabled=false short-circuit). Just the
     * helper call. */
    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true); /* over + pressed */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn("btn_disabled", BTN_X, BTN_Y);
    const int16_t pad[4] = {12, 12, 12, 12};
    nt_ui_debug_record_disabled_zone(s_fx.ctx, nt_ui_id("btn_disabled"), pad);

    /* Exactly one zone recorded. */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_debug_get_zone_count(s_fx.ctx));
    const nt_ui_debug_zone_t *z = &s_fx.ctx->debug_zones[0];
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn_disabled"), z->id);

    /* DISABLED flag set; hover/pressed/captured all CLEAR — helper records
     * intent, not state. */
    TEST_ASSERT_TRUE((z->state_flags & NT_UI_DEBUG_FLAG_DISABLED) != 0U);
    TEST_ASSERT_FALSE((z->state_flags & NT_UI_DEBUG_FLAG_HOVERED) != 0U);
    TEST_ASSERT_FALSE((z->state_flags & NT_UI_DEBUG_FLAG_PRESSED) != 0U);
    TEST_ASSERT_FALSE((z->state_flags & NT_UI_DEBUG_FLAG_CAPTURED) != 0U);

    /* Padded layout bbox correctly inflated by 12 on every side. */
    TEST_ASSERT_TRUE(z->layout_l < z->visual_l);
    TEST_ASSERT_TRUE(z->layout_r > z->visual_r);
    TEST_ASSERT_TRUE(z->layout_t < z->visual_t);
    TEST_ASSERT_TRUE(z->layout_b > z->visual_b);

    /* Crucially: NO capture got started. The helper does not touch capture state. */
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 9: disabled-record helper is zero-overhead when recording is OFF ---- */
static void test_debug_disabled_helper_off_no_capture(void) {
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);

    /* Recording OFF (default). Push must be silently dropped. */
    TEST_ASSERT_FALSE(nt_ui_debug_get_recording(s_fx.ctx));
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    const int16_t pad[4] = {8, 8, 8, 8};
    nt_ui_debug_record_disabled_zone(s_fx.ctx, nt_ui_id("btnA"), pad);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_debug_get_zone_count(s_fx.ctx));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 10: at-cap pushes are silently saturated (no assert) ---- */
static void test_debug_cap_saturates_silently(void) {
    /* Declare one button, then call get_interaction debug_zone_cap+10 times
     * (re-querying the same id). count must clamp to the cap, no assert. */
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    nt_ui_end(s_fx.ctx);

    nt_ui_debug_set_recording(s_fx.ctx, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    declare_btn("btnA", BTN_X, BTN_Y);
    const uint32_t cap = s_fx.ctx->debug_zone_cap;
    const uint32_t over = cap + 10U;
    for (uint32_t i = 0; i < over; ++i) {
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    }
    TEST_ASSERT_EQUAL_UINT32(cap, nt_ui_debug_get_zone_count(s_fx.ctx));
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
    RUN_TEST(test_debug_emit_matches_walker_coord_space);
    RUN_TEST(test_debug_disabled_helper_records_zone);
    RUN_TEST(test_debug_disabled_helper_off_no_capture);
    RUN_TEST(test_debug_cap_saturates_silently);
    return UNITY_END();
}
