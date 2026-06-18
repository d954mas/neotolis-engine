/* Context-menu + recursive submenu tests. Covers the
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
    TEST_ASSERT_EQUAL_UINT((unsigned)((2 * sizeof(void *)) + 32), (unsigned)sizeof(nt_ui_menu_item_t));
    TEST_ASSERT_EQUAL_UINT(96U, (unsigned)sizeof(nt_ui_menu_style_t));
    TEST_ASSERT_EQUAL_UINT(16U, (unsigned)sizeof(nt_ui_menu_state_t));
}

static void test_menu_defaults_valid(void) {
    nt_ui_menu_style_t st = nt_ui_menu_style_defaults();
    TEST_ASSERT_TRUE(st.item_height > 0U);
    TEST_ASSERT_TRUE(st.min_width > 0U);
    TEST_ASSERT_TRUE(st.open_ease_speed == 0.0F); /* plumbed knob; default snaps (0) */
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
    }
    /* The corridor times out even while aimed-in (spec: never trap): the dwell stays UNDER the grace for
     * this short diagonal so keep held, but the timer is allowed to climb (it no longer freezes at 0). */
    TEST_ASSERT_TRUE(nt_ui_menu_test_switch_timer(s_fx.ctx, MENU_A, 1U) < NT_UI_MENU_AIM_FALLBACK_SECS);
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

/* ---- Stuck-state regression: with a STABLE apex, vertical travel along the parent panel
 *      toward a sibling LEAVES the narrow corridor, so the dwell races AIM_FALLBACK and a switch is
 *      allowed. The old per-frame apex made the wedge near-degenerate-huge and trapped this move. ---- */
static void test_menu_hover_switch_to_sibling_releases(void) {
    /* submenu opened RIGHT at (300,100) 150x300; its left edge is the parent panel's right edge x~300.
     * The cursor leaves the parent row at (295,150) and travels straight DOWN the parent panel (x stays
     * ~150, well LEFT of the wedge) onto a lower sibling — it must release within AIM_FALLBACK. */
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    const float dt = 1.0F / 60.0F;

    fx_begin(dt);
    (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 295.0F, 150.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
    nt_ui_end(s_fx.ctx);

    bool last_keep = true;
    int frames_to_switch = 0;
    for (int i = 0; i < 30; ++i) {
        /* move down the parent column, far left of the submenu edge -> outside the stable wedge */
        const float my = 200.0F + ((float)i * 6.0F);
        fx_begin(dt);
        last_keep = nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 150.0F, my, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
        nt_ui_end(s_fx.ctx);
        if (!last_keep) {
            frames_to_switch = i + 1;
            break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(last_keep, "moving onto a sibling column must release the corridor (no trap)");
    /* released within the dwell grace (~8 frames at dt=1/60), never trapped indefinitely */
    TEST_ASSERT_TRUE_MESSAGE(frames_to_switch >= 1 && frames_to_switch <= 9, "sibling switch must free the user within the grace");
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
    NT_TEST_EXPECT_ASSERT(nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, bad, 1U, &st, &style));
    nt_ui_end(s_fx.ctx);
}

/* Building a tree deeper than NT_UI_MENU_MAX_DEPTH at compile time is awkward; the depth-cap death
 * test runs against the live recursive declare below. This proves the cap constant exists
 * and the popup-core's own depth assert (shared) fires past its cap. */
static void test_menu_max_depth_constant(void) {
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH >= 1);
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH <= NT_UI_MODAL_MAX_DEPTH);
}

/* ============================ full menu UI ============================ */

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
    nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_root, 2U, st, style);
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
 *      Right opens the grandchild, Down to File2, Enter activates id 102. ---- */
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

/* Drive a clean primary-button press edge into the GLOBAL input state at (px,py) — the menu's
 * outside-click dismiss reads nt_input_mouse_is_pressed (global), NOT the per-frame snapshot. A poll
 * clears stale edges, then pointer_down with mask 1 (LEFT) raises is_pressed on the 0->1 transition. */
static void menu_mouse_press(float px, float py) {
    nt_input_poll();               /* clear stale pressed/released edges */
    nt_input_clear_all_pointers(); /* drop any prior is_down so the next down is a clean rising edge */
    nt_input_pointer_down(0U, px, py, 1.0F, NT_POINTER_MOUSE, 1U);
}

/* ---- Outside-click dismiss: a left-click far outside every open panel closes the whole chain
 *      (menu-owned dismiss; no light-dismiss catchers). ---- */
static void test_menu_outside_click_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* frame 1: render so the panel bbox exists next frame (no press) */
    nt_input_clear_all_keys();
    menu_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: primary press far from the panel (bottom-right corner) -> menu-owned outside dismiss.
     * The snapshot pointer carries the same pos so the menu reads a consistent cursor. */
    menu_mouse_press(780.0F, 580.0F);
    menu_frame(&st, &style, 780.0F, 580.0F);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "outside-click dismisses the whole chain");
}

/* ---- Inside-click does NOT dismiss: a primary press INSIDE the root panel is a row interaction, never
 *      a chain dismiss. The menu stays open. ---- */
static void test_menu_inside_click_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* frame 1: render so the root panel bbox exists next frame. Anchor at (120,80) -> panel grows below. */
    nt_input_clear_all_keys();
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: press well inside the root panel body (just below the anchor) -> not a dismiss. */
    menu_mouse_press(140.0F, 100.0F);
    menu_frame(&st, &style, 140.0F, 100.0F);

    TEST_ASSERT_TRUE_MESSAGE(st.open, "a click inside an open panel must NOT dismiss the chain");
}

/* ---- Switch root branch via keyboard while a submenu is open (stuck-state guard): open the
 *      File submenu, then Left collapses back to root, Down moves to another root parent (Tools), Right
 *      opens ITS submenu. The user is never locked into the first open branch. ---- */
static const nt_ui_menu_item_t s_tools_sub[] = {
    {.label = "Opt", .id = 301U, .enabled = true},
};
static const nt_ui_menu_item_t s_root2[] = {
    {.label = "File", .submenu = s_file_sub, .submenu_count = 3U, .enabled = true},
    {.label = "Tools", .submenu = s_tools_sub, .submenu_count = 1U, .enabled = true},
};
static void menu_frame2(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_root2, 2U, st, style);
    nt_ui_end(s_fx.ctx);
}
static void test_menu_switch_root_branch_while_submenu_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    menu_key(NT_KEY_ARROW_DOWN); /* focus File (root item 0) */
    menu_frame2(&st, &style);
    menu_key(NT_KEY_ARROW_RIGHT); /* open File submenu (depth 1) */
    menu_frame2(&st, &style);
    TEST_ASSERT_TRUE(st.open);

    menu_key(NT_KEY_ARROW_LEFT); /* collapse File submenu back to root */
    menu_frame2(&st, &style);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "Left collapses the submenu but the root stays open");

    menu_key(NT_KEY_ARROW_DOWN); /* move to Tools (root item 1) */
    menu_frame2(&st, &style);
    menu_key(NT_KEY_ARROW_RIGHT); /* open Tools submenu */
    menu_frame2(&st, &style);
    menu_key(NT_KEY_ENTER); /* activate Opt -> id 301 */
    menu_frame2(&st, &style);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(301U, st.chosen_id, "must reach the OTHER root branch's leaf while the first was open");
    TEST_ASSERT_FALSE(st.open);
}

/* Drive one s_root2 menu frame with a positioned snapshot pointer (mouse hover/click path). The menu's
 * row hover/click reads ctx->frame_pointers; the press-bit drives those rows. No global press here. */
static void menu_frame2_at(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py, bool press) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    if (press) {
        p.buttons[NT_BUTTON_LEFT].is_down = true;
        p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    }
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_root2, 2U, st, style);
    nt_ui_end(s_fx.ctx);
}

/* ---- Occlusion-trap regression: with a submenu open, the ANCESTOR
 *      (root) sibling row is still HITTABLE by the mouse. A submenu full-viewport
 *      light-dismiss catcher above the root panel would occlude it, so the root row
 *      never reported hovered and the user was trapped in the deepest level. ---- */
static void test_menu_root_row_hittable_while_submenu_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* open File's submenu via keyboard so depth-1 panel (with its old catcher) is live */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame2(&st, &style);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_frame2(&st, &style);
    TEST_ASSERT_TRUE(st.open);

    /* one settle frame so the root row bbox is queryable next frame (1-frame IM lag) */
    menu_frame2_at(&st, &style, 0.0F, 0.0F, false);
    const uint32_t tools_row = nt_ui_menu_test_row_id(MENU_A, 0U, 1U); /* root item 1 = Tools */
    const nt_ui_bbox_t rb = nt_ui_get_bbox(s_fx.ctx, tools_row);
    TEST_ASSERT_TRUE_MESSAGE(rb.found, "root Tools row must lay out while the submenu is open");

    /* hover the Tools root row center: with the catcher removed it must report hovered (was occluded). */
    const float cx = rb.x + (rb.width * 0.5F);
    const float cy = rb.y + (rb.height * 0.5F);
    menu_frame2_at(&st, &style, cx, cy, false);
    const nt_ui_interaction_t in = nt_ui_query_interaction(s_fx.ctx, tools_row);
    TEST_ASSERT_TRUE_MESSAGE(in.hovered, "ancestor root row must be hittable (not occluded by a submenu catcher)");
}

/* ---- Click an ancestor root parent while a submenu is open: the click reaches the root row (no
 *      catcher occlusion) and switches the open branch to it. ---- */
static void test_menu_click_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* open File's submenu (depth 1) */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame2(&st, &style);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_frame2(&st, &style);
    TEST_ASSERT_TRUE(st.open);

    /* settle so the Tools root row bbox is queryable */
    nt_input_clear_all_keys();
    menu_frame2_at(&st, &style, 0.0F, 0.0F, false);
    const uint32_t tools_row = nt_ui_menu_test_row_id(MENU_A, 0U, 1U);
    nt_ui_bbox_t rb = nt_ui_get_bbox(s_fx.ctx, tools_row);
    TEST_ASSERT_TRUE(rb.found);
    const float cx = rb.x + (rb.width * 0.5F);
    const float cy = rb.y + (rb.height * 0.5F);

    /* click the Tools root parent -> switches the open branch to Tools (a click INSIDE a panel is a row
     * interaction, never a dismiss). The chain stays open on the new branch. */
    menu_frame2_at(&st, &style, cx, cy, true);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "clicking a root parent must NOT dismiss the chain");

    /* the Tools submenu (depth 1) now lays out under the Tools row -> reachable */
    const uint32_t tools_panel = nt_ui_menu_test_panel_id(MENU_A, 1U);
    menu_frame2_at(&st, &style, cx, cy, false);
    const nt_ui_bbox_t pb = nt_ui_get_bbox(s_fx.ctx, tools_panel);
    TEST_ASSERT_TRUE_MESSAGE(pb.found, "clicking the Tools root parent must open its submenu (branch switched)");
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
    nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_cyclic, 1U, &st, &style);
    nt_ui_end(s_fx.ctx);

    /* Open one deeper level per frame; after MAX_DEPTH Rights the next push trips the cap assert. */
    bool tripped = false;
    for (int i = 0; i < NT_UI_MENU_MAX_DEPTH + 4 && !tripped; ++i) {
        menu_key(NT_KEY_ARROW_RIGHT);
        nt_test_assert_armed = true;
        if (setjmp(nt_test_assert_jmp) == 0) {
            nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
            nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_cyclic, 1U, &st, &style);
            nt_ui_end(s_fx.ctx);
        } else {
            tripped = true;
        }
        nt_test_assert_armed = false;
    }
    TEST_ASSERT_TRUE_MESSAGE(tripped, "self-referential submenu must trip the depth-cap NT_ASSERT");
}

/* ============================ sprites / icons / separator / occluder ============================ */

/* A tree with an icon gutter + a separator + a parent (arrow marker) for the visual-parity probes. The
 * icon ref carries a non-zero atlas.id so the gutter draws an image cell (no real atlas binds in the
 * fixture, so the image resolve no-ops, but the gutter cell itself still lays out + the label aligns). */
static const nt_ui_menu_item_t s_sub25[] = {
    {.label = "ChildA", .id = 501U, .enabled = true},
};
static nt_ui_menu_item_t s_root25[] = {
    {.label = "Iconed", .id = 401U, .enabled = true},                                        /* icon set at runtime */
    {.label = NULL, .enabled = false},                                                       /* separator */
    {.label = "Plain", .id = 402U, .enabled = true},                                         /* no icon -> aligned-empty gutter */
    {.label = "Parent", .submenu = s_sub25, .submenu_count = 1U, .id = 0U, .enabled = true}, /* arrow marker */
};
static void menu_frame25(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_root25, 4U, st, style);
    nt_ui_end(s_fx.ctx);
}

/* ---- Icon gutter: with icon_size > 0 the iconed AND non-iconed rows both reserve the leading gutter, so
 *      their labels start at the SAME x (aligned empty space on the iconless row). ---- */
static void test_menu_icon_gutter_aligns_labels(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    s_root25[0].icon = (nt_atlas_region_ref_t){.atlas = {.id = 1U}, .region = NT_ATLAS_INVALID_REGION};
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    menu_frame25(&st, &style, 0.0F, 0.0F); /* lay out so the gutter cell bbox exists next frame */
    menu_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(MENU_A, 0U, 0U));
    const nt_ui_bbox_t g2 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(MENU_A, 0U, 2U));
    TEST_ASSERT_TRUE_MESSAGE(g0.found, "iconed row gutter cell must lay out");
    TEST_ASSERT_TRUE_MESSAGE(g2.found, "non-iconed row must STILL reserve an aligned-empty gutter");
    TEST_ASSERT_TRUE_MESSAGE(float_near(g0.x, g2.x, 0.5F), "both gutters must align (icon-column model)");
    TEST_ASSERT_TRUE_MESSAGE(float_near(g0.width, (float)style.icon_size, 0.5F), "gutter width == icon_size");
}

/* ---- icon_size == 0: no gutter at all (the cell is not declared). ---- */
static void test_menu_icon_gutter_absent_when_zero(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 0U;
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};
    menu_frame25(&st, &style, 0.0F, 0.0F);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(MENU_A, 0U, 0U));
    TEST_ASSERT_FALSE_MESSAGE(g0.found, "icon_size 0 must declare no gutter cell");
}

/* ---- Separator is non-interactive: a NULL-label item has NO row id (its bbox is never registered) and
 *      keyboard Down skips it (focus jumps Iconed(0) -> Plain(2), never landing on the separator). ---- */
static void test_menu_separator_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* the separator (index 1) never registers a row id -> its bbox is not found */
    menu_frame25(&st, &style, 0.0F, 0.0F);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t sep = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_row_id(MENU_A, 0U, 1U));
    TEST_ASSERT_FALSE_MESSAGE(sep.found, "a separator must not register an interactive row id");

    /* Down focuses item 0 (Iconed), a second Down must SKIP the separator and land on item 2 (Plain). */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    /* settle + activate the focused item: it must be Plain (402), proving the separator was skipped. */
    menu_key(NT_KEY_ENTER);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(402U, st.chosen_id, "Down must skip the separator (focus Iconed->Plain)");
}

/* ---- Submenu marker cell: a parent row declares the marker cell (arrow sprite or ">" fallback) with a
 *      stable fmix id; a leaf row never does. ---- */
static void test_menu_arrow_marker_on_parent_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};
    /* set an arrow ref so the IMAGE marker cell (which carries the fmix id) is declared on the parent */
    style.arrow = (nt_atlas_region_ref_t){.atlas = {.id = 1U}, .region = 0U};
    style.arrow_size = 12U;

    menu_frame25(&st, &style, 0.0F, 0.0F);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t parent_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(MENU_A, 0U, 3U)); /* Parent */
    const nt_ui_bbox_t leaf_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(MENU_A, 0U, 0U));   /* Iconed leaf */
    TEST_ASSERT_TRUE_MESSAGE(parent_arrow.found, "a parent row must declare the submenu marker cell");
    TEST_ASSERT_FALSE_MESSAGE(leaf_arrow.found, "a leaf row must NOT declare a submenu marker");
}

/* ---- Single root occluder: while the menu is open ONE full-viewport occluder lays out under the stack;
 *      a closed menu declares none (present-only). ---- */
static void test_menu_root_occluder_present_while_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};
    menu_frame25(&st, &style, 0.0F, 0.0F);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t occ = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_occluder_id(MENU_A));
    TEST_ASSERT_TRUE_MESSAGE(occ.found, "open menu must declare a root occluder");
    TEST_ASSERT_TRUE_MESSAGE(float_near(occ.width, VIEW_W, 1.0F) && float_near(occ.height, VIEW_H, 1.0F), "occluder must be full-viewport");

    st.open = false;
    menu_frame25(&st, &style, 0.0F, 0.0F);
    menu_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t occ2 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_occluder_id(MENU_A));
    TEST_ASSERT_FALSE_MESSAGE(occ2.found, "closed menu must declare no occluder (present-only)");
}

/* ============================ open_trigger binding + reopen reset ============================ */

#define TARGET_ID 0x7A19E7U /* the bound widget the trigger hover-tests */

/* Drive a clean GLOBAL right-button press edge at (px,py): the trigger reads nt_input_mouse_is_pressed
 * (global), the hover gate reads the per-frame snapshot pointer separately. */
static void menu_right_press(float px, float py) {
    nt_input_poll();
    nt_input_clear_all_pointers();
    nt_input_pointer_down(0U, px, py, 1.0F, NT_POINTER_MOUSE, 2U); /* mask 2 = NT_BUTTON_RIGHT */
}

/* One frame: declare a fixed target panel (registered for hover via block_pointer), run open_trigger
 * bound to it, then declare the menu. The snapshot pointer at (px,py) drives the hover gate. long_pressed
 * is the caller-supplied touch trigger (the helper does no mutating gesture step of its own). */
static void trigger_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, uint32_t target_id, float px, float py, bool long_pressed) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    if (nt_input_mouse_is_pressed(NT_BUTTON_RIGHT)) {
        p.buttons[NT_BUTTON_RIGHT].is_down = true;
        p.buttons[NT_BUTTON_RIGHT].is_pressed = true;
    }
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    /* Target panel at (200,150) 160x120 so the hover-test has a concrete bbox. */
    CLAY({.id = (Clay_ElementId){.id = TARGET_ID}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {200.0F, 150.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(120)}}}) {
    }
    nt_ui_block_pointer(s_fx.ctx, TARGET_ID, NULL);
    (void)nt_ui_menu_open_trigger(s_fx.ctx, MENU_A, target_id, long_pressed, st);
    nt_ui_menu(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, MENU_A, s_root, 2U, st, style);
    nt_ui_end(s_fx.ctx);
}

/* ---- Unbound (target_id == 0): a right-click ANYWHERE arms the menu (preserves the global trigger). ---- */
static void test_menu_open_trigger_unbound_arms_anywhere(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    nt_input_clear_all_keys();
    menu_right_press(20.0F, 20.0F); /* far from the target panel */
    trigger_frame(&st, &style, 0U, 20.0F, 20.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "unbound trigger must arm on a right-click anywhere");
}

/* ---- Caller-supplied long-press arms the trigger: the helper does NO mutating gesture step itself; the
 *      caller passes long_pressed from ITS widget's events step. A right-click absent + long_pressed=true
 *      must arm (and bind to the menu anchor at the cursor). ---- */
static void test_menu_open_trigger_long_press_passed_in_arms(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    nt_input_clear_all_keys();
    nt_input_clear_all_pointers(); /* no right-click this frame; arming comes solely from long_pressed */
    trigger_frame(&st, &style, 0U, 40.0F, 40.0F, true);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "caller-supplied long_pressed must arm the trigger");
    TEST_ASSERT_TRUE_MESSAGE(float_near(st.anchor_x, 40.0F, 0.5F) && float_near(st.anchor_y, 40.0F, 0.5F), "long-press arms the menu at the cursor");
}

/* ---- Bound (target_id != 0): a right-click OFF the target does NOT arm; ON the target DOES. ---- */
static void test_menu_open_trigger_bound_requires_target_hover(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};

    /* Settle one frame so the target panel is registered for next-frame hover arbitration. */
    nt_input_clear_all_keys();
    nt_input_clear_all_pointers();
    trigger_frame(&st, &style, TARGET_ID, 280.0F, 210.0F, false);
    TEST_ASSERT_FALSE(st.open);

    /* Right-click far OFF the target -> bound trigger must NOT arm. */
    menu_right_press(20.0F, 20.0F);
    trigger_frame(&st, &style, TARGET_ID, 20.0F, 20.0F, false);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "bound trigger must not arm when the pointer is off the target");

    /* Right-click ON the target center (200..360, 150..270) -> arms. */
    menu_right_press(280.0F, 210.0F);
    trigger_frame(&st, &style, TARGET_ID, 280.0F, 210.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "bound trigger must arm when the pointer is over the target");
}

/* ---- Reopen reset: open -> descend into a submenu -> st->open=false (closed frame) -> st->open=true
 *      must start with a clean chain (no stale open_path/focus from the prior session). ---- */
static void test_menu_reopen_after_external_close_is_clean(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {.open = true, .anchor_x = 120.0F, .anchor_y = 80.0F};

    /* Open the File submenu (depth 1) via keyboard. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);
    /* The depth-1 submenu panel must be live now. */
    nt_input_clear_all_keys();
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U)).found, "submenu must be open before the external close");

    /* Game closes the menu directly (not via the helper / internal dismiss), skip a frame. */
    st.open = false;
    menu_frame(&st, &style, 0.0F, 0.0F); /* closed frame resets the runtime chain */

    /* Direct reopen: the prior submenu must NOT still be open. */
    st.open = true;
    menu_frame(&st, &style, 0.0F, 0.0F); /* settle */
    menu_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U)).found, "a direct reopen must start with no stale submenu open");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_abi_sizes);
    RUN_TEST(test_menu_defaults_valid);
    RUN_TEST(test_menu_point_in_tri);
    RUN_TEST(test_menu_aim_corners_mirror);
    RUN_TEST(test_menu_aim_keeps_submenu_open_on_diagonal);
    RUN_TEST(test_menu_aim_switch_after_fallback);
    RUN_TEST(test_menu_hover_switch_to_sibling_releases);
    RUN_TEST(test_menu_hover_cells_distinct_per_depth);
    RUN_TEST(test_menu_malformed_tree_asserts);
    RUN_TEST(test_menu_max_depth_constant);
    RUN_TEST(test_menu_smoke_open_and_closed);
    RUN_TEST(test_menu_kbd_nav_activates_nested_leaf);
    RUN_TEST(test_menu_nested_dismiss_esc_deepest_first);
    RUN_TEST(test_menu_switch_root_branch_while_submenu_open);
    RUN_TEST(test_menu_root_row_hittable_while_submenu_open);
    RUN_TEST(test_menu_click_root_parent_switches_branch);
    RUN_TEST(test_menu_outside_click_dismisses_chain);
    RUN_TEST(test_menu_inside_click_keeps_open);
    RUN_TEST(test_menu_depth_cap_asserts);
    RUN_TEST(test_menu_icon_gutter_aligns_labels);
    RUN_TEST(test_menu_icon_gutter_absent_when_zero);
    RUN_TEST(test_menu_separator_non_interactive);
    RUN_TEST(test_menu_arrow_marker_on_parent_only);
    RUN_TEST(test_menu_root_occluder_present_while_open);
    RUN_TEST(test_menu_open_trigger_unbound_arms_anywhere);
    RUN_TEST(test_menu_open_trigger_long_press_passed_in_arms);
    RUN_TEST(test_menu_open_trigger_bound_requires_target_hover);
    RUN_TEST(test_menu_reopen_after_external_close_is_clean);
    return UNITY_END();
}
