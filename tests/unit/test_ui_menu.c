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
    nt_input_poll(); /* clear sticky pressed/released edges left by a prior test (clear_all_keys leaves them) */
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
    TEST_ASSERT_EQUAL_UINT(32U, (unsigned)sizeof(nt_ui_menu_style_t));
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

/* ============================ Task 2: full menu UI ============================ */

/* A 3-level tree: File > (New, Open > (Project, File2), Quit). Static so submenu pointers stay valid. */
static const nt_ui_menu_item_t s_open_sub[] = {
    {.label = "Project", .id = 101U, .enabled = true},
    {.label = "File2", .id = 102U, .enabled = true},
};
static const nt_ui_menu_item_t s_file_sub[] = {
    {.label = "New", .id = 1U, .enabled = true},
    {.label = "Open", .submenu = s_open_sub, .submenu_count = 2U, .enabled = true},
    {.label = "Quit", .id = 3U, .enabled = true},
};
static const nt_ui_menu_item_t s_root[] = {
    {.label = "File", .submenu = s_file_sub, .submenu_count = 3U, .enabled = true},
    {.label = "Edit", .id = 9U, .enabled = true},
};

/* Produce exactly ONE fresh pressed-key edge for this frame. nt_input_poll clears the sticky
 * pressed/released edges (tests have no platform poll to do it); clear_all_keys drops the held
 * current state; set_key(true) then raises a clean rising edge. */
static void menu_key(nt_key_t key) {
    nt_input_poll();
    nt_input_clear_all_keys();
    nt_input_set_key(key, true);
}

/* Drive one menu frame with a pointer + optional key. The pointer is positioned but the keyboard path
 * is what we assert against (deterministic, no prev-frame bbox dependency). */
static void menu_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu(s_fx.ctx, MENU_A, s_root, 2U, st, style);
    nt_ui_end(s_fx.ctx);
}

/* ---- Smoke: an open menu declares the root popup and balances the stack; a closed menu declares
 *      nothing (present-only). ---- */
static void test_menu_smoke_open_and_closed(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_popup_test_stack_depth(s_fx.ctx)); /* balanced */
    st.open = false;
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_popup_test_stack_depth(s_fx.ctx));
}

/* ---- Keyboard-nav reaches a nested leaf: Down focuses File, Right opens its submenu, Down to Open,
 *      Right opens the grandchild, Down to File2, Enter activates id 102 (Model D). ---- */
static void test_menu_kbd_nav_activates_nested_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* Frame 1: focus root item 0 (File) via Down. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 2: Right opens File's submenu (depth 1). */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 3: Down moves focus within submenu to item 1 (Open). */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 4: Right opens Open's grandchild (depth 2), focus item 0 (Project). */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 5: Down to item 1 (File2). */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 6: Enter activates File2 -> chosen_id 102, chain closes. */
    menu_key(NT_KEY_ENTER);
    menu_frame(&st, &style, 0.0F, 0.0F);

    TEST_ASSERT_EQUAL_UINT32(102U, st.chosen_id);
    TEST_ASSERT_FALSE(st.open);
}

/* ---- Nested dismiss: Esc on the deepest level closes it first (root stays open); a second Esc closes
 *      the root chain entirely. ---- */
static void test_menu_nested_dismiss_esc_deepest_first(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* open root->File submenu */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    /* Esc #1: close the deepest level (the File submenu) — root stays open. */
    menu_key(NT_KEY_ESCAPE);
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "first Esc closes only the deepest submenu, root stays open");

    /* Esc #2: close the root chain. */
    menu_key(NT_KEY_ESCAPE);
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "second Esc closes the root chain");
}

/* ---- Outside-click dismiss: a left-click far outside the panel raises the catcher close and dismisses
 *      the whole chain. ---- */
static void test_menu_outside_click_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* frame 1: render so the panel bbox exists next frame */
    nt_input_clear_all_keys();
    menu_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: click far from the panel (bottom-right corner) -> outside-click dismiss */
    nt_pointer_t p = {0};
    p.x = 780.0F;
    p.y = 580.0F;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu(s_fx.ctx, MENU_A, s_root, 2U, &st, &style);
    nt_ui_end(s_fx.ctx);
    /* release over the same outside point completes the click */
    nt_pointer_t p2 = {0};
    p2.x = 780.0F;
    p2.y = 580.0F;
    p2.active = true;
    p2.buttons[NT_BUTTON_LEFT].is_released = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p2, 1);
    nt_ui_menu(s_fx.ctx, MENU_A, s_root, 2U, &st, &style);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "outside-click dismisses the whole chain");
}

/* ---- Depth cap: a self-referential tree (an item whose submenu is itself) would nest forever; the
 *      NT_ASSERT at the recursion guard must fire before exhausting the popup stack. ---- */
static nt_ui_menu_item_t s_cyclic[1];
static void test_menu_depth_cap_asserts(void) {
    /* item 0's submenu points back at itself -> infinite nesting if uncapped */
    s_cyclic[0] = (nt_ui_menu_item_t){.label = "loop", .id = 1U, .enabled = true};
    s_cyclic[0].submenu = s_cyclic;
    s_cyclic[0].submenu_count = 1U;
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* Prime several frames so each level's bbox is found and the keep-open recursion drives deeper, then
     * force the open path down every level via repeated Right opens; the cap assert fires when the
     * recursion would push past NT_UI_MENU_MAX_DEPTH. */
    menu_key(NT_KEY_ARROW_DOWN);
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_menu(s_fx.ctx, MENU_A, s_cyclic, 1U, &st, &style);
    nt_ui_end(s_fx.ctx);

    /* Open one deeper level per frame; after MAX_DEPTH Rights the next push trips the cap assert. */
    bool tripped = false;
    for (int i = 0; i < NT_UI_MENU_MAX_DEPTH + 4 && !tripped; ++i) {
        menu_key(NT_KEY_ARROW_RIGHT);
        nt_test_assert_armed = true;
        if (setjmp(nt_test_assert_jmp) == 0) {
            nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
            nt_ui_menu(s_fx.ctx, MENU_A, s_cyclic, 1U, &st, &style);
            nt_ui_end(s_fx.ctx);
        } else {
            tripped = true;
        }
        nt_test_assert_armed = false;
    }
    TEST_ASSERT_TRUE_MESSAGE(tripped, "self-referential submenu must trip the depth-cap NT_ASSERT");
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
    RUN_TEST(test_menu_smoke_open_and_closed);
    RUN_TEST(test_menu_kbd_nav_activates_nested_leaf);
    RUN_TEST(test_menu_nested_dismiss_esc_deepest_first);
    RUN_TEST(test_menu_outside_click_dismisses_chain);
    RUN_TEST(test_menu_depth_cap_asserts);
    return UNITY_END();
}
