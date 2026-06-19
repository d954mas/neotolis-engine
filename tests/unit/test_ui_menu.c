/* Context-menu + recursive submenu tests, re-expressed against the IMMEDIATE begin/end API (#236).
 * Covers the algorithm in isolation — the mouse-aim triangle hover-intent keeps an open submenu open
 * along a diagonal that crosses sibling items, the off-triangle dwell switches after AIM_FALLBACK, the
 * per-level edge-flip mirrors the aim corners near all 4 borders, depth-salted state cells never alias
 * across levels, and the depth cap asserts. The full-UI tests drive nt_ui_menu_begin/item/submenu/end
 * instead of an items[] array. Driven through the walker fixture + NT_TEST_ACCESS probes (no GL surface).
 * UNITY_EXCLUDE_FLOAT: compare floats via an eps helper.
 *
 * RED scaffold (Wave 0): the immediate begin/end symbols + nt_ui_menu_test_item_id / _focus_item_id
 * probes are defined by Plans 02-04, so this file COMPILES but link-FAILS until then. */

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

/* Grown style ABI (Plan 03): + shortcut_text color + checkmark ref/tint/size. 3 ref + 9 u32 + 4 float +
 * 8 u16 + 4 tail pad = 120. Mirrors the _Static_assert in nt_ui_menu.h (both must agree). */
#define EXPECTED_MENU_STYLE_ABI 120U

/* Sibling item keys for the immediate driver (unique among siblings; scope stack disambiguates depth). */
#define KEY_FILE 1U
#define KEY_EDIT 2U
#define KEY_NEW 10U
#define KEY_OPEN 11U
#define KEY_QUIT 12U
#define KEY_PROJECT 20U
#define KEY_FILE2 21U
#define KEY_TOOLS 30U
#define KEY_OPT 31U

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

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. The style line
 *      tracks the GROWN size symbolically via EXPECTED_MENU_STYLE_ABI. The data-form item struct's ABI
 *      line was dropped with the data form (Plan 05); only the immediate-API ABIs remain. ---- */
static void test_menu_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(EXPECTED_MENU_STYLE_ABI, (unsigned)sizeof(nt_ui_menu_style_t));
    TEST_ASSERT_EQUAL_UINT((unsigned)(16U + ((((sizeof(void *) + 4U) + 7U) / 8U) * 8U)), (unsigned)sizeof(nt_ui_menu_item_opts_t));
    TEST_ASSERT_EQUAL_UINT(16U, (unsigned)sizeof(nt_ui_menu_state_t));
}

static void test_menu_defaults_valid(void) {
    nt_ui_menu_style_t st = nt_ui_menu_style_defaults();
    TEST_ASSERT_TRUE(st.item_height > 0U);
    TEST_ASSERT_TRUE(st.min_width > 0U);
    TEST_ASSERT_TRUE(st.open_ease_speed == 0.0F); /* plumbed knob; default snaps (0) */
}

/* ---- Pure point_in_tri: inside, outside, on an edge. (KEEP — algorithm probe, paradigm-agnostic) ---- */
static void test_menu_point_in_tri(void) {
    /* triangle (0,0)-(10,0)-(0,10) */
    TEST_ASSERT_TRUE(nt_ui_menu_test_point_in_tri(2.0F, 2.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 10.0F));
    TEST_ASSERT_FALSE(nt_ui_menu_test_point_in_tri(9.0F, 9.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 10.0F));
    /* on the hypotenuse counts as inside (boundary inclusive) */
    TEST_ASSERT_TRUE(nt_ui_menu_test_point_in_tri(5.0F, 5.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 10.0F));
}

/* ---- Aim corners: submenu RIGHT => near edge is its LEFT; LEFT (edge-flipped) => its RIGHT. (KEEP) ---- */
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
 *      stays inside the triangle every frame -> the submenu stays open (aiming==true). (KEEP) ---- */
static void test_menu_aim_keeps_submenu_open_on_diagonal(void) {
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
    TEST_ASSERT_TRUE(nt_ui_menu_test_switch_timer(s_fx.ctx, MENU_A, 1U) < NT_UI_MENU_AIM_FALLBACK_SECS);
}

/* ---- Aim SWITCH: the cursor sits OFF the triangle; after AIM_FALLBACK the keep flips false. (KEEP) ---- */
static void test_menu_aim_switch_after_fallback(void) {
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    const float dt = 1.0F / 60.0F;

    fx_begin(dt);
    (void)nt_ui_menu_test_hover_intent(s_fx.ctx, MENU_A, 1U, 290.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, NT_UI_POPUP_RIGHT, dt);
    nt_ui_end(s_fx.ctx);

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

/* ---- Stuck-state regression: vertical travel along the parent panel toward a sibling leaves the
 *      narrow corridor, so the dwell races AIM_FALLBACK and a switch is allowed. (KEEP) ---- */
static void test_menu_hover_switch_to_sibling_releases(void) {
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
    TEST_ASSERT_TRUE_MESSAGE(frames_to_switch >= 1 && frames_to_switch <= 9, "sibling switch must free the user within the grace");
}

/* ---- Depth-salted cells: level 1 and level 2 must own distinct timers (no aliasing). (KEEP) ---- */
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

/* ---- Depth-cap constant exists and stays within the popup cap. (KEEP) ---- */
static void test_menu_max_depth_constant(void) {
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH >= 1);
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH <= NT_UI_MODAL_MAX_DEPTH);
}

/* ============================ immediate full menu UI ============================ */

/* Produce exactly ONE fresh pressed-key edge for this frame. nt_input_poll clears the sticky
 * pressed/released edges (tests have no platform poll to do it); clear_all_keys drops the held
 * current state; set_key(true) then raises a clean rising edge. */
static void menu_key(nt_key_t key) {
    nt_input_poll();
    nt_input_clear_all_keys();
    nt_input_set_key(key, true);
}

/* Immediate driver: open the menu at (anchor_x, anchor_y) by setting the game-owned open flag + anchor.
 * Mirrors what nt_ui_menu_open_trigger does (arm at the cursor) without a right-click. */
static void menu_im_open(nt_ui_menu_state_t *st, float anchor_x, float anchor_y) {
    st->open = true;
    st->anchor_x = anchor_x;
    st->anchor_y = anchor_y;
}

/* One immediate menu frame, tree: File > (New, Open > (Project, File2), Quit), Edit. Declares the tree
 * via begin/item/submenu_begin..submenu_end/menu_end. The body is identical every frame so the prev-frame
 * frame record (D-236-06, 1-frame nav latency) maps focus to a stable item id. The snapshot pointer is
 * positioned but the keyboard path is what most tests assert against (deterministic, no prev-frame bbox). */
static void menu_im_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_NEW, "New");
        if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_OPEN, "Open")) {
            (void)nt_ui_menu_item(s_fx.ctx, KEY_PROJECT, "Project");
            (void)nt_ui_menu_item(s_fx.ctx, KEY_FILE2, "File2");
            nt_ui_menu_submenu_end(s_fx.ctx);
        }
        (void)nt_ui_menu_item(s_fx.ctx, KEY_QUIT, "Quit");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    (void)nt_ui_menu_item(s_fx.ctx, KEY_EDIT, "Edit");
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Smoke: an open menu declares the root popup and balances the stack; a closed menu declares
 *      nothing (present-only). ---- */
static void test_menu_smoke_open_and_closed(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_popup_test_stack_depth(s_fx.ctx)); /* balanced */
    st.open = false;
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_popup_test_stack_depth(s_fx.ctx));
}

/* ---- Keyboard-nav reaches a nested leaf: Down focuses File, Right opens its submenu, Down to Open,
 *      Right opens the grandchild, Down to File2, Enter activates -> chosen_id is File2. The tree is
 *      built via the immediate begin/submenu/item calls across frames (prev-frame nav). ---- */
static void test_menu_kbd_nav_activates_nested_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Frame 1: focus root item 0 (File) via Down. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 2: Right opens File's submenu (depth 1). */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 3: Down moves focus within submenu to Open. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 4: Right opens Open's grandchild (depth 2), focus Project. */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 5: Down to File2. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 6: Enter activates File2 -> chosen_id, chain closes. */
    menu_key(NT_KEY_ENTER);
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    /* File2's fully scoped id: File(root idx 0) -> Open(File-submenu idx 1) -> File2(Open-submenu idx 1).
     * Each submenu pushes its own row id as the child scope (mix(scope,key,running_idx)). */
    const uint32_t file_scope = nt_ui_menu_test_item_id(MENU_A, KEY_FILE, 0U);
    const uint32_t open_scope = nt_ui_menu_test_item_id(file_scope, KEY_OPEN, 1U);
    const uint32_t file2_id = nt_ui_menu_test_item_id(open_scope, KEY_FILE2, 1U);
    TEST_ASSERT_EQUAL_UINT32(file2_id, st.chosen_id);
    TEST_ASSERT_FALSE(st.open);
}

/* ---- Nested dismiss: Esc on the deepest level closes it first (root stays open); a second Esc closes
 *      the root chain entirely. ---- */
static void test_menu_nested_dismiss_esc_deepest_first(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* open root->File submenu */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    /* Esc #1: close the deepest level (the File submenu) — root stays open. */
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "first Esc closes only the deepest submenu, root stays open");

    /* Esc #2: close the root chain. */
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "second Esc closes the root chain");
}

/* Drive a clean primary-button press edge into the GLOBAL input state at (px,py) — the menu's
 * outside-click dismiss reads nt_input_mouse_is_pressed (global), NOT the per-frame snapshot. */
static void menu_mouse_press(float px, float py) {
    nt_input_poll();               /* clear stale pressed/released edges */
    nt_input_clear_all_pointers(); /* drop any prior is_down so the next down is a clean rising edge */
    nt_input_pointer_down(0U, px, py, 1.0F, NT_POINTER_MOUSE, 1U);
}

/* ---- Outside-click dismiss: a left-click far outside every open panel closes the whole chain. ---- */
static void test_menu_outside_click_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the panel bbox exists next frame (no press) */
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: primary press far from the panel (bottom-right corner) -> menu-owned outside dismiss. */
    menu_mouse_press(780.0F, 580.0F);
    menu_im_frame(&st, &style, 780.0F, 580.0F);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "outside-click dismisses the whole chain");
}

/* ---- Inside-click does NOT dismiss: a primary press INSIDE the root panel is a row interaction. ---- */
static void test_menu_inside_click_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the root panel bbox exists next frame. Anchor at (120,80) -> panel grows below. */
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: press well inside the root panel body (just below the anchor) -> not a dismiss. */
    menu_mouse_press(140.0F, 100.0F);
    menu_im_frame(&st, &style, 140.0F, 100.0F);

    TEST_ASSERT_TRUE_MESSAGE(st.open, "a click inside an open panel must NOT dismiss the chain");
}

/* Drive a clean GLOBAL right-button press edge at (px,py): mask 2 = NT_BUTTON_RIGHT. */
static void menu_right_press_at(float px, float py) {
    nt_input_poll();
    nt_input_clear_all_pointers();
    nt_input_pointer_down(0U, px, py, 1.0F, NT_POINTER_MOUSE, 2U);
}

/* ---- Right-click outside (a frame AFTER opening) dismisses the chain, mirroring the left-click dismiss. ---- */
static void test_menu_right_click_outside_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the panel bbox exists next frame (no press) */
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: RIGHT press far from the panel -> menu-owned outside dismiss. The snapshot pointer carries
     * the right-button edge so the menu reads a consistent right-press this frame. */
    menu_right_press_at(780.0F, 580.0F);
    nt_pointer_t p = {.x = 780.0F, .y = 580.0F, .active = true};
    p.buttons[NT_BUTTON_RIGHT].is_down = true;
    p.buttons[NT_BUTTON_RIGHT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_NEW, "New");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    (void)nt_ui_menu_item(s_fx.ctx, KEY_EDIT, "Edit");
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "right-click outside (post-open) dismisses the whole chain");
}

/* Immediate frame for a two-branch root: File > (New, Open > (Project, File2), Quit) and Tools > (Opt).
 * Used by the branch-switch tests (the user must reach the OTHER root branch while one is open). */
static void menu_im_frame2(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py, bool press) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    if (press) {
        p.buttons[NT_BUTTON_LEFT].is_down = true;
        p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    }
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_NEW, "New");
        if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_OPEN, "Open")) {
            (void)nt_ui_menu_item(s_fx.ctx, KEY_PROJECT, "Project");
            (void)nt_ui_menu_item(s_fx.ctx, KEY_FILE2, "File2");
            nt_ui_menu_submenu_end(s_fx.ctx);
        }
        (void)nt_ui_menu_item(s_fx.ctx, KEY_QUIT, "Quit");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_TOOLS, "Tools")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_OPT, "Opt");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Switch root branch via keyboard while a submenu is open: open File submenu, Left collapses back
 *      to root, Down moves to Tools, Right opens ITS submenu, Enter activates Opt. ---- */
static void test_menu_switch_root_branch_while_submenu_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    menu_key(NT_KEY_ARROW_DOWN); /* focus File (root item 0) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT); /* open File submenu (depth 1) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    TEST_ASSERT_TRUE(st.open);

    menu_key(NT_KEY_ARROW_LEFT); /* collapse File submenu back to root */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "Left collapses the submenu but the root stays open");

    menu_key(NT_KEY_ARROW_DOWN); /* move to Tools (root item 1) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT); /* open Tools submenu */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ENTER); /* activate Opt */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS, 1U), KEY_OPT, 0U), st.chosen_id,
                                     "must reach the OTHER root branch's leaf while the first was open");
    TEST_ASSERT_FALSE(st.open);
}

/* ---- Ancestor root row is still HITTABLE while a submenu is open (no occluding per-level catcher). ---- */
static void test_menu_root_row_hittable_while_submenu_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* open File's submenu via keyboard so depth-1 panel is live */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    TEST_ASSERT_TRUE(st.open);

    /* one settle frame so the root Tools row bbox is queryable next frame (1-frame IM lag) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    const uint32_t tools_row = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS, 1U); /* root item 1 = Tools */
    const nt_ui_bbox_t rb = nt_ui_get_bbox(s_fx.ctx, tools_row);
    TEST_ASSERT_TRUE_MESSAGE(rb.found, "root Tools row must lay out while the submenu is open");

    /* hover the Tools root row center: with no occluding catcher it must report hovered. */
    const float cx = rb.x + (rb.width * 0.5F);
    const float cy = rb.y + (rb.height * 0.5F);
    menu_im_frame2(&st, &style, cx, cy, false);
    const nt_ui_interaction_t in = nt_ui_query_interaction(s_fx.ctx, tools_row);
    TEST_ASSERT_TRUE_MESSAGE(in.hovered, "ancestor root row must be hittable (not occluded by a submenu catcher)");
}

/* ---- Click an ancestor root parent while a submenu is open: the click reaches the root row and
 *      switches the open branch to it. ---- */
static void test_menu_click_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* open File's submenu (depth 1) */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    TEST_ASSERT_TRUE(st.open);

    /* settle so the Tools root row bbox is queryable */
    nt_input_clear_all_keys();
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    const uint32_t tools_row = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS, 1U);
    nt_ui_bbox_t rb = nt_ui_get_bbox(s_fx.ctx, tools_row);
    TEST_ASSERT_TRUE(rb.found);
    const float cx = rb.x + (rb.width * 0.5F);
    const float cy = rb.y + (rb.height * 0.5F);

    /* click the Tools root parent -> switches the open branch to Tools (a click INSIDE a panel is a row
     * interaction, never a dismiss). The chain stays open on the new branch. */
    menu_im_frame2(&st, &style, cx, cy, true);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "clicking a root parent must NOT dismiss the chain");

    /* the Tools submenu (depth 1) now lays out under the Tools row -> reachable */
    const uint32_t tools_panel = nt_ui_menu_test_panel_id(MENU_A, 1U);
    menu_im_frame2(&st, &style, cx, cy, false);
    const nt_ui_bbox_t pb = nt_ui_get_bbox(s_fx.ctx, tools_panel);
    TEST_ASSERT_TRUE_MESSAGE(pb.found, "clicking the Tools root parent must open its submenu (branch switched)");
}

/* ---- Depth cap: a self-referential immediate tree (a submenu_begin that recurses on itself) would
 *      nest forever; the NT_ASSERT at the depth guard must fire before exhausting the popup stack. The
 *      tree is built by recursing submenu_begin with the same key per level. ---- */
static void menu_im_self_ref(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    /* Open as deep as the runtime drives: each open level declares another self-keyed submenu. The cap
     * assert fires when submenu_begin would push past NT_UI_MENU_MAX_DEPTH. */
    int guard = 0;
    while (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "loop") && guard < NT_UI_MENU_MAX_DEPTH + 4) {
        ++guard;
    }
    for (int i = 0; i < guard; ++i) {
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}
static void test_menu_depth_cap_asserts(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* settle: lay out the root so kbd-nav has a level */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_self_ref(&st, &style);

    /* Open one deeper level per frame; after MAX_DEPTH Rights the next push trips the cap assert. */
    bool tripped = false;
    for (int i = 0; i < NT_UI_MENU_MAX_DEPTH + 4 && !tripped; ++i) {
        menu_key(NT_KEY_ARROW_RIGHT);
        nt_test_assert_armed = true;
        if (setjmp(nt_test_assert_jmp) == 0) {
            menu_im_self_ref(&st, &style);
        } else {
            tripped = true;
        }
        nt_test_assert_armed = false;
    }
    TEST_ASSERT_TRUE_MESSAGE(tripped, "self-referential submenu must trip the depth-cap NT_ASSERT");
}

/* ============================ icons / separator / occluder ============================ */

/* An icon ref carries a non-zero atlas.id so the gutter draws an image cell (no real atlas binds in the
 * fixture, so the resolve no-ops, but the gutter cell itself lays out + the label aligns). */
static const nt_atlas_region_ref_t s_icon_ref = {.atlas = {.id = 1U}, .region = NT_ATLAS_INVALID_REGION};

/* Immediate frame for the icon/separator vitrine: Iconed (item_ex with icon), a separator, Plain (no
 * icon), Parent (submenu marker). Mirrors the data-form s_root25 row set. */
static void menu_im_frame25(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    (void)nt_ui_menu_item_ex(s_fx.ctx, KEY_NEW, "Iconed", (nt_ui_menu_item_opts_t){.enabled = true, .icon = s_icon_ref});
    nt_ui_menu_separator(s_fx.ctx);
    (void)nt_ui_menu_item_ex(s_fx.ctx, KEY_QUIT, "Plain", (nt_ui_menu_item_opts_t){.enabled = true});
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_OPEN, "Parent")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_PROJECT, "ChildA");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Icon gutter: with icon_size > 0 the iconed AND non-iconed rows both reserve the leading gutter,
 *      so their labels start at the SAME x. Re-expressed against item_ex(opts{.icon=...}). ---- */
static void test_menu_icon_gutter_aligns_labels(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    menu_im_frame25(&st, &style, 0.0F, 0.0F); /* lay out so the gutter cell bbox exists next frame */
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
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
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(MENU_A, 0U, 0U));
    TEST_ASSERT_FALSE_MESSAGE(g0.found, "icon_size 0 must declare no gutter cell");
}

/* ---- Separator is non-interactive: a nt_ui_menu_separator declares NO queryable row id and keyboard
 *      Down skips it (focus jumps Iconed -> Plain, never landing on the separator). ---- */
static void test_menu_separator_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* the separator (index 1) never registers a row id -> its bbox is not found */
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t sep = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_row_id(MENU_A, 0U, 1U));
    TEST_ASSERT_FALSE_MESSAGE(sep.found, "a separator must not register an interactive row id");

    /* Down focuses Iconed, a second Down must SKIP the separator and land on Plain. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    /* settle + activate the focused item: it must be Plain (Down skipped the separator). */
    menu_key(NT_KEY_ENTER);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT, 2U), st.chosen_id, "Down must skip the separator (focus Iconed->Plain)");
}

/* ---- Submenu marker cell: a parent row declares the marker cell (arrow sprite or ">" fallback); a leaf
 *      row never does. ---- */
static void test_menu_arrow_marker_on_parent_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    /* set an arrow ref so the IMAGE marker cell (which carries the fmix id) is declared on the parent */
    style.arrow = (nt_atlas_region_ref_t){.atlas = {.id = 1U}, .region = 0U};
    style.arrow_size = 12U;

    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t parent_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(MENU_A, 0U, 3U)); /* Parent */
    const nt_ui_bbox_t leaf_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(MENU_A, 0U, 0U));   /* Iconed leaf */
    TEST_ASSERT_TRUE_MESSAGE(parent_arrow.found, "a parent row must declare the submenu marker cell");
    TEST_ASSERT_FALSE_MESSAGE(leaf_arrow.found, "a leaf row must NOT declare a submenu marker");
}

/* ---- Single root occluder: while the menu is open ONE full-viewport occluder lays out; a closed menu
 *      declares none (present-only). ---- */
static void test_menu_root_occluder_present_while_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t occ = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_occluder_id(MENU_A));
    TEST_ASSERT_TRUE_MESSAGE(occ.found, "open menu must declare a root occluder");
    TEST_ASSERT_TRUE_MESSAGE(float_near(occ.width, VIEW_W, 1.0F) && float_near(occ.height, VIEW_H, 1.0F), "occluder must be full-viewport");

    st.open = false;
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t occ2 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_occluder_id(MENU_A));
    TEST_ASSERT_FALSE_MESSAGE(occ2.found, "closed menu must declare no occluder (present-only)");
}

/* ============================ open_trigger binding (KEPT 1:1) ============================ */

#define TARGET_ID 0x7A19E7U /* the bound widget the trigger hover-tests */

/* Drive a clean GLOBAL right-button press edge at (px,py): mask 2 = NT_BUTTON_RIGHT. */
static void menu_right_press(float px, float py) {
    nt_input_poll();
    nt_input_clear_all_pointers();
    nt_input_pointer_down(0U, px, py, 1.0F, NT_POINTER_MOUSE, 2U);
}

/* One frame: declare a fixed target panel (registered for hover via block_pointer), run open_trigger
 * bound to it (KEPT 1:1 — paradigm-agnostic), then declare the immediate menu. */
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
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_NEW, "New");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    (void)nt_ui_menu_item(s_fx.ctx, KEY_EDIT, "Edit");
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Unbound (target_id == 0): a right-click ANYWHERE arms the menu. ---- */
static void test_menu_open_trigger_unbound_arms_anywhere(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    nt_input_clear_all_keys();
    menu_right_press(20.0F, 20.0F); /* far from the target panel */
    trigger_frame(&st, &style, 0U, 20.0F, 20.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "unbound trigger must arm on a right-click anywhere");
}

/* ---- The opening right-click does NOT self-close (opened_frame guard). ---- */
static void test_menu_opening_right_click_does_not_self_close(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    nt_input_clear_all_keys();
    menu_right_press(20.0F, 20.0F);
    trigger_frame(&st, &style, 0U, 20.0F, 20.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "the opening right-click arms and must NOT self-close the menu");
}

/* ---- Caller-supplied long-press arms the trigger (no right-click that frame). ---- */
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

/* ============================ NEW immediate-mode RED tests ============================ */

/* ---- NEW (REQ-236-02): scope-stack id distinctness — sibling keys at one scope and the SAME key under
 *      a pushed submenu scope all derive distinct, non-zero ids (mirrors the menu_hash_id non-alias
 *      guarantee). Drives nt_ui_menu_test_item_id(scope, key, idx). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat distinctness asserts, not real nesting
static void test_menu_item_id_distinct_siblings_and_scopes(void) {
    /* root scope = the menu id; two sibling keys at idx 0/1 */
    const uint32_t a = nt_ui_menu_test_item_id(MENU_A, KEY_FILE, 0U);
    const uint32_t b = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT, 1U);
    /* the SAME key reused under a pushed submenu scope (submenu_begin pushes its derived id as scope) */
    const uint32_t sub_scope = nt_ui_menu_test_item_id(MENU_A, KEY_FILE, 0U); /* the File submenu's own id == its row id */
    const uint32_t c = nt_ui_menu_test_item_id(sub_scope, KEY_FILE, 0U);      /* reuse KEY_FILE one level deeper */

    TEST_ASSERT_TRUE_MESSAGE(a != 0U && b != 0U && c != 0U, "no derived item id may be 0 (the no-widget sentinel)");
    TEST_ASSERT_TRUE_MESSAGE(a != b, "sibling keys at one scope must derive distinct ids");
    TEST_ASSERT_TRUE_MESSAGE(a != c, "the same key under a pushed submenu scope must NOT alias the root id");
    TEST_ASSERT_TRUE_MESSAGE(b != c, "cross-scope ids must stay distinct");
}

/* ---- NEW (REQ-236-05): prev-frame frame-record nav — across two frames build a 3-item level, press
 *      Down, and assert the frame-record introspection probe maps focus to the recorded item id with
 *      strictly 1-frame latency (D-236-06). Drives nt_ui_menu_test_focus_item_id(menu_id, depth). ---- */
static void test_menu_prevframe_nav_focuses_recorded_item(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Frame 1: lay out the root level (records {File, Edit, ...}); no key yet -> no focus committed. */
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    /* Frame 2: Down — nav runs against the PREVIOUS frame's record, focusing the first enabled item. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    /* Frame 3: settle so the focus committed in frame 2 is observable via the prev-frame record probe.
     * poll() consumes the frame-2 Down PRESSED edge (clear_all_keys alone leaves the sticky edge, which
     * would re-step focus this frame). */
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    const uint32_t focus_id = nt_ui_menu_test_focus_item_id(MENU_A, 0U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_FILE, 0U), focus_id, "Down must focus the first recorded item (prev-frame record, 1-frame latency)");
}

/* ---- NEW (REQ-236-08, Pitfall 4): a custom-content row with activatable=false lets an inner button own
 *      the click — the inner button reports clicked while the row id reports !clicked. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_item_begin_activatable_false_child_owns_click(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    const uint32_t row_key = KEY_NEW;
    const uint32_t inner_btn = 0x1B7701U;
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, row_key, 0U);

    /* Frames 0-1 bake the inner button bbox (1-frame IM lag); frame 2 PRESSES over it (capture); frame 3
     * RELEASES over it -> clicked = is_released && over fires on the inner child. The activatable=false
     * row must NOT steal that click (chosen_id stays unset). item_begin now returns DECLARE-BODY (true
     * while open) — the body is guarded by it; the click is owned by the inner child. */
    bool declared = false;
    bool btn_clicked = false;
    for (int frame = 0; frame < 4; ++frame) {
        const bool press = (frame == 2);   /* press began this frame -> capture */
        const bool release = (frame == 3); /* release over the widget -> click one-shot */
        nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, inner_btn);
        const float bx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
        const float by = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;

        nt_pointer_t p = {0};
        p.x = bx;
        p.y = by;
        p.active = true;
        if (press && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_down = true;
            p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        } else if (release && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_released = true; /* down -> up over the widget */
        }
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
        if (nt_ui_menu_item_begin(s_fx.ctx, row_key, (nt_ui_menu_item_opts_t){.enabled = true, .activatable = false})) {
            declared = true;
            /* Inner interactive child owns the click; lay out a fixed button-sized element + step it. */
            CLAY({.id = (Clay_ElementId){.id = inner_btn}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
            const nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, inner_btn);
            if (in.clicked) {
                btn_clicked = true;
            }
        }
        nt_ui_menu_item_end(s_fx.ctx);
        nt_ui_menu_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }

    TEST_ASSERT_TRUE_MESSAGE(declared, "an open menu must declare the custom-content body (item_begin returns true)");
    TEST_ASSERT_TRUE_MESSAGE(btn_clicked, "the inner child button must own the click");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.chosen_id, "an activatable=false row must NOT latch the click as an activation");
    (void)row_id;
}

/* ---- Bug 3 (custom row leak): a CLOSED (present-only) menu's item_begin returns false so the game skips
 *      the custom body — the inner child must NOT lay out (no bbox) when the menu is closed, so it can
 *      never leak onto the scene. ---- */
static void test_menu_item_begin_closed_skips_body(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0}; /* st.open stays false -> present-only */
    const uint32_t inner_btn = 0x1B7703U;

    bool declared = false;
    for (int frame = 0; frame < 2; ++frame) {
        nt_pointer_t p = {.x = 0.0F, .y = 0.0F, .active = true};
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
        if (nt_ui_menu_item_begin(s_fx.ctx, KEY_NEW, (nt_ui_menu_item_opts_t){.enabled = true, .activatable = false})) {
            declared = true; /* must stay false: the menu is closed */
            CLAY({.id = (Clay_ElementId){.id = inner_btn}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
        }
        nt_ui_menu_item_end(s_fx.ctx);
        nt_ui_menu_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(declared, "a closed (present-only) menu must NOT declare the custom-content body");
    const nt_ui_bbox_t child = nt_ui_get_bbox(s_fx.ctx, inner_btn);
    TEST_ASSERT_FALSE_MESSAGE(child.found, "a closed menu's custom child must not lay out anywhere (no scene leak)");
}

/* ============================ rich-row shortcut + checkmark cells ============================ */

/* Immediate frame: one rich row carrying a shortcut + selected check (Save), one plain row (Plain). The
 * shortcut/check CELL bbox is asserted (the rendered glyph is user-visual-QA only). */
static void menu_im_frame_rich(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    nt_ui_menu_item_opts_t save = nt_ui_menu_item_opts_defaults();
    save.shortcut = "Ctrl+S";
    save.selected = true;
    (void)nt_ui_menu_item_ex(s_fx.ctx, KEY_NEW, "Save", save);
    (void)nt_ui_menu_item_ex(s_fx.ctx, KEY_QUIT, "Plain", nt_ui_menu_item_opts_defaults());
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- A rich row with a shortcut declares the shortcut cell (own fmix id); a plain row does NOT. ---- */
static void test_menu_shortcut_cell_on_rich_row_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t sc = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_shortcut_id(MENU_A, 0U, 0U));
    const nt_ui_bbox_t plain_sc = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_shortcut_id(MENU_A, 0U, 1U));
    TEST_ASSERT_TRUE_MESSAGE(sc.found, "a row with opts.shortcut must declare the shortcut cell");
    TEST_ASSERT_FALSE_MESSAGE(plain_sc.found, "a row without a shortcut must NOT declare a shortcut cell");
}

/* ---- opts.selected declares the checkmark cell (own fmix id); an unselected row does NOT. ---- */
static void test_menu_check_cell_when_selected(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t ck = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(MENU_A, 0U, 0U));
    const nt_ui_bbox_t unchecked = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(MENU_A, 0U, 1U));
    TEST_ASSERT_TRUE_MESSAGE(ck.found, "a selected row must declare the checkmark cell");
    TEST_ASSERT_FALSE_MESSAGE(unchecked.found, "an unselected row must NOT declare a checkmark cell");
}

/* One menu frame: root = [File submenu (2 items), Edit leaf]. Cursor at (px,py). Used by the hover-open
 * corridor tests — File is the parent that flies out on hover; Edit is the sibling leaf that collapses it. */
static void menu_im_hover_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_NEW, "New");
        (void)nt_ui_menu_item(s_fx.ctx, KEY_PROJECT, "Project");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    (void)nt_ui_menu_item(s_fx.ctx, KEY_EDIT, "Edit");
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* Center of a root row's prev-frame bbox (0,0 if not yet laid out). */
static void menu_row_center(uint32_t row_id, float *cx, float *cy) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
    *cx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
    *cy = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
}

/* ---- Bug 1 (hover-open): hovering a parent ROW flies its submenu out (no click), with 1-frame IM lag. ---- */
static void test_menu_hover_opens_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    nt_input_clear_all_keys();

    const uint32_t file_row = nt_ui_menu_test_item_id(MENU_A, KEY_FILE, 0U);

    /* F1: lay out the root so the File row bbox exists next frame. */
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-1, nt_ui_menu_test_open_path(MENU_A, 0U), "nothing open before any hover");

    /* F2: hover the File parent row center -> records the hover; menu_end commits open_path (1-frame lag). */
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file_row, &fx, &fy);
    menu_im_hover_frame(&st, &style, fx, fy);

    /* F3: settle; open_path[0] now points at File (running idx 0), its submenu flies out. */
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, nt_ui_menu_test_open_path(MENU_A, 0U), "hovering the parent row must open its submenu (idx 0)");
    TEST_ASSERT_TRUE(st.open);
}

/* ---- Bug 1 (leave-close): with File's submenu open, hovering the sibling Edit LEAF collapses it once the
 *      mouse-aim corridor releases (the cursor is no longer aiming at the open child). ---- */
static void test_menu_hover_sibling_leaf_collapses_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    nt_input_clear_all_keys();

    const uint32_t file_row = nt_ui_menu_test_item_id(MENU_A, KEY_FILE, 0U);
    const uint32_t edit_row = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT, 1U);

    /* Open File's submenu via hover (settle a couple frames). */
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file_row, &fx, &fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, nt_ui_menu_test_open_path(MENU_A, 0U), "File submenu open before the sibling hover");

    /* Hover the Edit sibling leaf far from the open child corridor for enough frames that the dwell timer
     * crosses AIM_FALLBACK and the corridor releases -> the open child collapses. */
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit_row, &ex, &ey);
    bool collapsed = false;
    for (int f = 0; f < 30 && !collapsed; ++f) {
        menu_im_hover_frame(&st, &style, ex, ey);
        collapsed = (nt_ui_menu_test_open_path(MENU_A, 0U) < 0);
    }
    TEST_ASSERT_TRUE_MESSAGE(collapsed, "hovering a sibling leaf (corridor released) must collapse the open submenu");
}

/* One menu frame with a single File submenu (two child items), driven by the keyboard nav path. */
static void menu_im_submenu_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(s_fx.ctx, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(s_fx.ctx, KEY_NEW, "ChildItemLong");
        (void)nt_ui_menu_item(s_fx.ctx, KEY_QUIT, "ChildItem2");
        nt_ui_menu_submenu_end(s_fx.ctx);
    }
    nt_ui_menu_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Bug 2 (right-edge flip): a menu opened hard against the RIGHT screen edge must (a) clamp the ROOT
 *      panel back on-screen (BELOW only flips vertically, so a point-anchored root needs a horizontal
 *      clamp), and (b) flip its submenu LEFT so the whole stack stays visible. ---- */
static void test_menu_right_edge_root_clamp_and_submenu_flip(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 780.0F, 100.0F); /* open hard against the right edge (780 + min_width 160 > 800) */

    nt_input_clear_all_keys();
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F1: lay out root */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F2: focus File */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F3: open File submenu */
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F4-5: settle so widths measure + side resolves */
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F);

    /* (a) ROOT panel clamped on-screen: its right edge must not exceed the viewport width. */
    const nt_ui_bbox_t root = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 0U));
    TEST_ASSERT_TRUE(root.found);
    TEST_ASSERT_TRUE_MESSAGE(root.x + root.width <= VIEW_W + 0.5F, "root menu near the right edge must clamp on-screen (no right clip)");
    TEST_ASSERT_TRUE_MESSAGE(root.x >= -0.5F, "clamped root must not push off the left edge");

    /* (b) Submenu flipped LEFT and stays on-screen. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_UI_POPUP_LEFT, nt_ui_popup_test_last_side(), "submenu near the right edge must flip LEFT");
    const nt_ui_bbox_t sub = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U));
    TEST_ASSERT_TRUE(sub.found);
    TEST_ASSERT_TRUE_MESSAGE(sub.x + sub.width <= VIEW_W + 0.5F, "left-flipped submenu must stay on-screen (right edge within the viewport)");
    TEST_ASSERT_TRUE_MESSAGE(sub.x >= -0.5F, "left-flipped submenu must not clip off the left edge");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_right_edge_root_clamp_and_submenu_flip);
    RUN_TEST(test_menu_hover_opens_submenu);
    RUN_TEST(test_menu_hover_sibling_leaf_collapses_submenu);
    RUN_TEST(test_menu_abi_sizes);
    RUN_TEST(test_menu_defaults_valid);
    RUN_TEST(test_menu_point_in_tri);
    RUN_TEST(test_menu_aim_corners_mirror);
    RUN_TEST(test_menu_aim_keeps_submenu_open_on_diagonal);
    RUN_TEST(test_menu_aim_switch_after_fallback);
    RUN_TEST(test_menu_hover_switch_to_sibling_releases);
    RUN_TEST(test_menu_hover_cells_distinct_per_depth);
    RUN_TEST(test_menu_max_depth_constant);
    RUN_TEST(test_menu_smoke_open_and_closed);
    RUN_TEST(test_menu_kbd_nav_activates_nested_leaf);
    RUN_TEST(test_menu_nested_dismiss_esc_deepest_first);
    RUN_TEST(test_menu_switch_root_branch_while_submenu_open);
    RUN_TEST(test_menu_root_row_hittable_while_submenu_open);
    RUN_TEST(test_menu_click_root_parent_switches_branch);
    RUN_TEST(test_menu_outside_click_dismisses_chain);
    RUN_TEST(test_menu_right_click_outside_dismisses_chain);
    RUN_TEST(test_menu_inside_click_keeps_open);
    RUN_TEST(test_menu_depth_cap_asserts);
    RUN_TEST(test_menu_icon_gutter_aligns_labels);
    RUN_TEST(test_menu_icon_gutter_absent_when_zero);
    RUN_TEST(test_menu_separator_non_interactive);
    RUN_TEST(test_menu_arrow_marker_on_parent_only);
    RUN_TEST(test_menu_root_occluder_present_while_open);
    RUN_TEST(test_menu_open_trigger_unbound_arms_anywhere);
    RUN_TEST(test_menu_opening_right_click_does_not_self_close);
    RUN_TEST(test_menu_open_trigger_long_press_passed_in_arms);
    RUN_TEST(test_menu_open_trigger_bound_requires_target_hover);
    RUN_TEST(test_menu_item_id_distinct_siblings_and_scopes);
    RUN_TEST(test_menu_prevframe_nav_focuses_recorded_item);
    RUN_TEST(test_menu_item_begin_activatable_false_child_owns_click);
    RUN_TEST(test_menu_item_begin_closed_skips_body);
    RUN_TEST(test_menu_shortcut_cell_on_rich_row_only);
    RUN_TEST(test_menu_check_cell_when_selected);
    return UNITY_END();
}
