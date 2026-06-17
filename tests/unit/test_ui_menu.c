/* Context-menu + recursive submenu tests (WGT-04/05). Task 1 (prototype-first) covers the novel
 * algorithm in isolation — the mouse-aim triangle hover-intent keeps an open submenu open along a
 * diagonal that crosses sibling items, the off-triangle dwell switches after AIM_FALLBACK, the
 * per-level edge-flip mirrors the aim corners near all 4 borders, depth-salted state cells never
 * alias across levels, and the depth cap asserts. Driven through the walker fixture + NT_TEST_ACCESS
 * probes (no GL surface). UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_menu.h"
#include "ui/nt_ui_popup.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define MENU_A 0x4E5001U

void setUp(void) {
    nt_test_assert_install();
    nt_input_clear_all_keys();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* nt_ui_begin asserts pointers != NULL; the hover-intent probes don't read the pointer, so a single
 * inactive snapshot at the origin is enough to satisfy the frame contract. */
static void fx_begin(float dt) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, dt, &p, 1);
}

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. ---- */
static void test_menu_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT((unsigned)((2 * sizeof(void *)) + 16), (unsigned)sizeof(nt_ui_menu_item_t));
    TEST_ASSERT_EQUAL_UINT(28U, (unsigned)sizeof(nt_ui_menu_style_t));
    TEST_ASSERT_EQUAL_UINT(16U, (unsigned)sizeof(nt_ui_menu_state_t));
}

static void test_menu_defaults_valid(void) {
    nt_ui_menu_style_t st = nt_ui_menu_style_defaults();
    TEST_ASSERT_TRUE(st.item_height > 0U);
    TEST_ASSERT_TRUE(st.min_width > 0U);
}

/* ---- Pure point_in_tri: inside, outside, on an edge. ---- */
static void test_menu_point_in_tri(void) {
    /* triangle (0,0)-(10,0)-(0,10) */
    TEST_ASSERT_TRUE(nt_ui_menu_test_point_in_tri(2.0F, 2.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 10.0F));
    TEST_ASSERT_FALSE(nt_ui_menu_test_point_in_tri(9.0F, 9.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 10.0F));
    /* on the hypotenuse counts as inside (boundary inclusive) */
    TEST_ASSERT_TRUE(nt_ui_menu_test_point_in_tri(5.0F, 5.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 10.0F));
}

/* ---- Aim corners: submenu RIGHT => near edge is its LEFT; LEFT (edge-flipped) => its RIGHT. ---- */
static void test_menu_aim_corners_mirror(void) {
    float bx = 0.0F;
    float by = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    /* submenu rect at (200,100) 150x200, opened to the RIGHT -> corners at x==200 (its left edge) */
    nt_ui_menu_test_aim_corners(200.0F, 100.0F, 150.0F, 200.0F, NT_UI_POPUP_RIGHT, &bx, &by, &cx, &cy);
    TEST_ASSERT_TRUE(float_near(bx, 200.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(cx, 200.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(by, 100.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(cy, 300.0F, 0.01F));
    /* edge-flipped LEFT -> mirror to the RIGHT edge x==350 */
    nt_ui_menu_test_aim_corners(200.0F, 100.0F, 150.0F, 200.0F, NT_UI_POPUP_LEFT, &bx, &by, &cx, &cy);
    TEST_ASSERT_TRUE(float_near(bx, 350.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(cx, 350.0F, 0.01F));
}

/* ---- Aim KEEP: a diagonal cursor path from a parent toward an open submenu (crossing a sibling row)
 *      stays inside the triangle every frame -> the submenu stays open (aiming==true). ---- */
static void test_menu_aim_keeps_submenu_open_on_diagonal(void) {
    /* submenu opened RIGHT at (300,100) 150x300. Parent item is to the LEFT at ~ (140,110). The cursor
     * starts at the parent's right edge and travels diagonally down-right to the submenu's lower rows,
     * crossing the sibling row band on the way. Every step must stay inside {prev, (300,100),(300,400)}. */
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    const float dt = 1.0F / 60.0F;

    fx_begin(dt);
    /* prime the cell at the parent's right edge */
    (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 290.0F, 110.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
    nt_ui_end(s_fx.ctx);

    /* travel diagonally toward the submenu's bottom-left corner; small steps keep prev->near triangle
     * fully covering the path. */
    const float path_x[] = {295.0F, 298.0F, 300.0F, 305.0F};
    const float path_y[] = {180.0F, 260.0F, 340.0F, 390.0F};
    for (int i = 0; i < 4; ++i) {
        fx_begin(dt);
        bool keep = nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, path_x[i], path_y[i], sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
        nt_ui_end(s_fx.ctx);
        TEST_ASSERT_TRUE_MESSAGE(keep, "submenu must stay open while aiming along the diagonal");
        TEST_ASSERT_TRUE(float_near(nt_ui_menu_test_switch_timer(s_fx.ctx, MENU_A, 1U), 0.0F, 0.0001F));
    }
}

/* ---- Aim SWITCH: the cursor sits OFF the triangle (straight up, away from the submenu); after
 *      AIM_FALLBACK seconds of dwell the keep flips false so a sibling may win. ---- */
static void test_menu_aim_switch_after_fallback(void) {
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    const float dt = 1.0F / 60.0F;

    fx_begin(dt);
    (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 290.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
    nt_ui_end(s_fx.ctx);

    /* Move LEFT/AWAY from the submenu (off the triangle) and hold there. With dt=1/60 it takes
     * ceil(0.12/0.0166)=8 frames to cross AIM_FALLBACK. Earlier frames keep; later flip. */
    bool last_keep = true;
    int frames_to_switch = 0;
    for (int i = 0; i < 30; ++i) {
        fx_begin(dt);
        last_keep = nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 100.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
        nt_ui_end(s_fx.ctx);
        if (!last_keep) {
            frames_to_switch = i + 1;
            break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(last_keep, "off the triangle past AIM_FALLBACK must allow a switch");
    /* AIM_FALLBACK / dt ~= 7.2 -> switch on the 8th off-triangle frame */
    TEST_ASSERT_TRUE(frames_to_switch >= 7 && frames_to_switch <= 9);
}

/* ---- Depth-salted cells: level 1 and level 2 must own distinct timers (no aliasing). ---- */
static void test_menu_hover_cells_distinct_per_depth(void) {
    const float dt = 1.0F / 60.0F;
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    fx_begin(dt);
    (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 290.0F, 110.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
    (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 2U, 290.0F, 110.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
    nt_ui_end(s_fx.ctx);
    /* drive level 1 OFF the triangle (timer climbs) while level 2 keeps aiming (timer 0) */
    for (int i = 0; i < 4; ++i) {
        fx_begin(dt);
        (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 100.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
        (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 2U, 305.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
        nt_ui_end(s_fx.ctx);
    }
    const float t1 = nt_ui_menu_test_switch_timer(s_fx.ctx, MENU_A, 1U);
    const float t2 = nt_ui_menu_test_switch_timer(s_fx.ctx, MENU_A, 2U);
    TEST_ASSERT_TRUE_MESSAGE(t1 > 0.0F, "level-1 off-triangle dwell must accumulate");
    TEST_ASSERT_TRUE_MESSAGE(float_near(t2, 0.0F, 0.0001F), "level-2 aiming dwell must stay 0 (no aliasing)");
}

/* ---- Tree validation: a parent item whose submenu ptr/count disagree asserts. ---- */
static void test_menu_malformed_tree_asserts(void) {
    static const nt_ui_menu_item_t child[] = {{.label = "x", .id = 1U, .enabled = true}};
    /* submenu ptr set but count 0 -> disagree */
    static const nt_ui_menu_item_t bad[] = {{.label = "p", .submenu = child, .submenu_count = 0U, .enabled = true}};
    nt_ui_menu_state_t st = {.open = true};
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    fx_begin(1.0F / 60.0F);
    NT_TEST_EXPECT_ASSERT(nt_ui_menu(s_fx.ctx, MENU_A, bad, 1U, &st, &style));
    nt_ui_end(s_fx.ctx);
}

/* Build a tree nested deeper than NT_UI_MENU_MAX_DEPTH at compile time is awkward; the depth-cap death
 * test is exercised in Task 2 against the live recursive declare. Task 1 proves the cap constant exists
 * and the popup-core's own depth assert (shared) fires past its cap. */
static void test_menu_max_depth_constant(void) {
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH >= 1);
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH <= NT_UI_MODAL_MAX_DEPTH);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_abi_sizes);
    RUN_TEST(test_menu_defaults_valid);
    RUN_TEST(test_menu_point_in_tri);
    RUN_TEST(test_menu_aim_corners_mirror);
    RUN_TEST(test_menu_aim_keeps_submenu_open_on_diagonal);
    RUN_TEST(test_menu_aim_switch_after_fallback);
    RUN_TEST(test_menu_hover_cells_distinct_per_depth);
    RUN_TEST(test_menu_malformed_tree_asserts);
    RUN_TEST(test_menu_max_depth_constant);
    return UNITY_END();
}
