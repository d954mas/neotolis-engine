/* Context-menu + recursive submenu tests against the IMMEDIATE begin/end API.
 * Covers the algorithm in isolation — the mouse-aim triangle hover-intent keeps an open submenu open
 * along a diagonal that crosses sibling items, the off-triangle dwell switches after AIM_FALLBACK, the
 * per-level edge-flip mirrors the aim corners near all 4 borders, depth-salted state cells never alias
 * across levels, and the depth cap asserts. The full-UI tests drive nt_ui_menu_begin/item/submenu/end.
 * Driven through the walker fixture + NT_TEST_ACCESS probes (no GL surface).
 * UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

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
/* Fixture-owned game menu scratch: one per logical menu, reused across the test's frames (holds the
 * per-level nav record). Re-zeroed each setUp so tests don't bleed nav state into each other. */
static nt_ui_menu_ctx_t s_menu;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define MENU_A 0x4E5001U

/* Menu style ABI: 3 ref + 9 u32 + 4 float + 8 u16 + 4 tail pad = 120. Mirrors the _Static_assert in
 * nt_ui_menu.h (both must agree). */
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
    nt_ui_menu_init(&s_menu); /* zero the game menu scratch: fresh nav record per test */
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* Inline-activation capture: activation is reported by the row's inline
 * bool for BOTH mouse and keyboard. The immediate drivers OR each target row's return into s_act_hit so a
 * test can assert the leaf's inline return fired (keyboard arrives 1 frame after Enter). Reset per test. */
static uint32_t s_act_capture_id; /* the fully-scoped row id a test wants to watch (0 = watch nothing) */
static bool s_act_hit;            /* set true the frame the watched row's inline return goes true */
/* OR a row's inline return into the capture if its scoped id matches the watched id. */
static void act_capture(uint32_t row_scoped_id, bool clicked) {
    if (clicked && row_scoped_id == s_act_capture_id) {
        s_act_hit = true;
    }
}

/* nt_ui_begin asserts pointers != NULL; the hover-intent probes don't read the pointer, so a single
 * inactive snapshot at the origin is enough to satisfy the frame contract. */
static void fx_begin(float dt) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, dt, &p, 1);
}

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. The style line
 *      tracks the size symbolically via EXPECTED_MENU_STYLE_ABI. ---- */
static void test_menu_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(EXPECTED_MENU_STYLE_ABI, (unsigned)sizeof(nt_ui_menu_style_t));
    TEST_ASSERT_EQUAL_UINT((unsigned)(16U + ((((sizeof(void *) + 4U) + 7U) / 8U) * 8U)), (unsigned)sizeof(nt_ui_menu_item_opts_t));
    TEST_ASSERT_EQUAL_UINT(12U, (unsigned)sizeof(nt_ui_menu_state_t)); /* 2 float + 1 bool + 3 pad */
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

/* ---- Aim SWITCH: the cursor sits OFF the triangle; after AIM_FALLBACK the keep flips false. ---- */
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
 *      narrow corridor, so the dwell races AIM_FALLBACK and a switch is allowed. ---- */
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

/* ---- Depth-cap constant exists and stays within the popup cap. ---- */
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
 * via begin/item/submenu_begin..submenu_end/menu_end. The body is identical every frame so this frame's
 * frame record maps focus to a stable item id (focus committed in menu_end is observable next frame —
 * a 1-frame effect). The snapshot pointer is positioned but the keyboard path is what most tests assert
 * against (deterministic, no prev-frame bbox). */
static void menu_im_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    const uint32_t file_scope = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t open_scope = nt_ui_menu_test_item_id(file_scope, KEY_OPEN);
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "New");
        if (nt_ui_menu_submenu_begin(&s_menu, KEY_OPEN, "Open")) {
            (void)nt_ui_menu_item(&s_menu, KEY_PROJECT, "Project");
            act_capture(nt_ui_menu_test_item_id(open_scope, KEY_FILE2), nt_ui_menu_item(&s_menu, KEY_FILE2, "File2"));
            nt_ui_menu_submenu_end(&s_menu);
        }
        (void)nt_ui_menu_item(&s_menu, KEY_QUIT, "Quit");
        nt_ui_menu_submenu_end(&s_menu);
    }
    (void)nt_ui_menu_item(&s_menu, KEY_EDIT, "Edit");
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}

/* ---- Smoke: an open menu declares the root popup and balances the stack; a closed menu declares
 *      nothing (present-only). ---- */
static void test_menu_smoke_open_and_closed(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth); /* balanced */
    st.open = false;
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
}

/* ---- Keyboard-nav reaches a nested leaf: Down focuses File, Right opens its submenu, Down to Open,
 *      Right opens the grandchild, Down to File2, Enter activates -> File2's INLINE return fires 1 frame
 *      later. Tree built via the immediate calls across frames (1-frame-effect nav). ---- */
static void test_menu_kbd_nav_activates_nested_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Watch File2's inline return: keyboard Enter must fire it (1 frame after Enter). */
    const uint32_t file_scope0 = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t open_scope0 = nt_ui_menu_test_item_id(file_scope0, KEY_OPEN);
    s_act_capture_id = nt_ui_menu_test_item_id(open_scope0, KEY_FILE2);
    s_act_hit = false;

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
    /* Frame 6: Enter on File2 -> menu_end stashes kbd_activated for next frame (does NOT close yet). */
    menu_key(NT_KEY_ENTER);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "keyboard activation is deferred 1 frame; the inline return must NOT fire on the Enter frame");
    TEST_ASSERT_TRUE_MESSAGE(st.open, "the chain stays open one more frame so the inline return can fire");

    /* Frame 7: File2's row call consumes kbd_activated -> its inline return goes true; the chain closes. */
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "keyboard Enter must fire the leaf's INLINE return (1-frame latency)");
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
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "New");
        nt_ui_menu_submenu_end(&s_menu);
    }
    (void)nt_ui_menu_item(&s_menu, KEY_EDIT, "Edit");
    nt_ui_menu_end(&s_menu);
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
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    const uint32_t tools_scope = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS);
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "New");
        if (nt_ui_menu_submenu_begin(&s_menu, KEY_OPEN, "Open")) {
            (void)nt_ui_menu_item(&s_menu, KEY_PROJECT, "Project");
            (void)nt_ui_menu_item(&s_menu, KEY_FILE2, "File2");
            nt_ui_menu_submenu_end(&s_menu);
        }
        (void)nt_ui_menu_item(&s_menu, KEY_QUIT, "Quit");
        nt_ui_menu_submenu_end(&s_menu);
    }
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_TOOLS, "Tools")) {
        act_capture(nt_ui_menu_test_item_id(tools_scope, KEY_OPT), nt_ui_menu_item(&s_menu, KEY_OPT, "Opt"));
        nt_ui_menu_submenu_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu);
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

    /* Watch the Tools>Opt leaf's inline return (keyboard, 1-frame latency). */
    s_act_capture_id = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    s_act_hit = false;

    menu_key(NT_KEY_ARROW_DOWN); /* move to Tools (root item 1) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT); /* open Tools submenu */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ENTER); /* activate Opt -> stashes kbd_activated for next frame */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);

    /* Settle one frame: Opt's row call consumes kbd_activated -> its inline return fires; the chain closes. */
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);

    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "must reach the OTHER root branch's leaf (inline return) while the first was open");
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
    const uint32_t tools_row = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS); /* root item 1 = Tools */
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
    const uint32_t tools_row = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS);
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
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    /* Open as deep as the runtime drives: each open level declares another self-keyed submenu. The cap
     * assert fires when submenu_begin would push past NT_UI_MENU_MAX_DEPTH. */
    int guard = 0;
    while (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "loop") && guard < NT_UI_MENU_MAX_DEPTH + 4) {
        ++guard;
    }
    for (int i = 0; i < guard; ++i) {
        nt_ui_menu_submenu_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}
static void test_menu_depth_cap_asserts(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* settle: lay out the root so the runtime cell exists */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_self_ref(&st, &style);

    /* The nav-open path caps at the deepest level (no OOB), so it cannot itself drive submenu_begin past
     * the cap. Force the open chain to the last valid level so the deepest submenu_begin ENTERS its body and
     * tries to push child == MAX_DEPTH — the decl-time backstop NT_ASSERT must still fire (different path). */
    nt_ui_menu_test_force_open_to(s_fx.ctx, MENU_A, (uint8_t)(NT_UI_MENU_MAX_DEPTH - 1U));
    menu_key(NT_KEY_ARROW_DOWN); /* a non-open key so the frame just declares the forced-open chain */

    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        menu_im_self_ref(&st, &style);
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    TEST_ASSERT_TRUE_MESSAGE(tripped, "submenu_begin at the deepest level must trip the depth-cap NT_ASSERT (decl-time backstop)");
}

/* ---- Depth-cap OOB regression: a self-referential tree opened to NT_UI_MENU_MAX_DEPTH then driven with
 *      more Right/Enter at the deepest allowable level must NOT bump active_depth past the cap. At the cap
 *      the kbd-open path is a no-op (open_path/active_depth/focus[depth+1] would index == MAX_DEPTH, OOB —
 *      the CI UBSan that flagged frame_record_count[8]). This drives the SAME self-ref driver but only opens
 *      to MAX_DEPTH-1 (its own submenu_begin asserts before pushing the cap), so it stops short of the
 *      decl-time assert and exercises the nav-side guard instead. ---- */
static void menu_im_self_ref_capped(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    /* Open only while the next push stays within the cap (guard < MAX_DEPTH-1): submenu_begin asserts at
     * the cap, so we stop one short and let the kbd-nav open path try (and be a no-op) at the deepest level. */
    int guard = 0;
    while (guard < (NT_UI_MENU_MAX_DEPTH - 1) && nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "loop")) {
        ++guard;
    }
    for (int i = 0; i < guard; ++i) {
        nt_ui_menu_submenu_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}
static void test_menu_kbd_open_at_depth_cap_is_noop(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Drive enough Right presses to open every allowable level AND keep pressing at the deepest one. Each
     * frame opens one deeper level (1-frame latency); extra presses past the cap must be no-ops. */
    for (int i = 0; i < NT_UI_MENU_MAX_DEPTH + 6; ++i) {
        menu_key(NT_KEY_ARROW_RIGHT);
        menu_im_self_ref_capped(&st, &style);
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_menu_test_active_depth(s_fx.ctx, MENU_A) <= NT_UI_MENU_MAX_DEPTH - 1, "kbd Right/Enter open must never push active_depth past the depth cap (UBSan OOB)");
    }
    /* At the deepest allowable level Enter on a parent stays a no-op (no crash, depth held). The chain is
     * still open (no dismiss), so the menu survived the cap drive without tripping the decl-time assert. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_menu_test_active_depth(s_fx.ctx, MENU_A) <= NT_UI_MENU_MAX_DEPTH - 1, "active_depth held within the cap after the open drive");
    TEST_ASSERT_TRUE_MESSAGE(st.open, "the menu chain stays open across the cap drive (no spurious dismiss)");
}

/* ============================ icons / separator / occluder ============================ */

/* An icon ref carries a non-zero atlas.id so the gutter draws an image cell (no real atlas binds in the
 * fixture, so the resolve no-ops, but the gutter cell itself lays out + the label aligns). */
static const nt_atlas_region_ref_t s_icon_ref = {.atlas = {.id = 1U}, .region = NT_ATLAS_INVALID_REGION};

/* Immediate frame for the icon/separator vitrine: Iconed (item_ex with icon), a separator, Plain (no
 * icon), Parent (submenu marker). */
static void menu_im_frame25(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    (void)nt_ui_menu_item_ex(&s_menu, KEY_NEW, "Iconed", (nt_ui_menu_item_opts_t){.icon = s_icon_ref});
    nt_ui_menu_separator(&s_menu);
    act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT), nt_ui_menu_item_ex(&s_menu, KEY_QUIT, "Plain", (nt_ui_menu_item_opts_t){0}));
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_OPEN, "Parent")) {
        (void)nt_ui_menu_item(&s_menu, KEY_PROJECT, "ChildA");
        nt_ui_menu_submenu_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu);
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

    /* Watch the Plain row's inline return (keyboard, 1-frame latency). */
    s_act_capture_id = nt_ui_menu_test_item_id(MENU_A, KEY_QUIT);
    s_act_hit = false;

    /* Down focuses Iconed, a second Down must SKIP the separator and land on Plain. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    /* Enter on the focused item -> stashes kbd_activated; the next frame's row call fires the inline return. */
    menu_key(NT_KEY_ENTER);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "Down must skip the separator (focus Iconed->Plain); Plain's inline return fires on Enter");
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
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "New");
        nt_ui_menu_submenu_end(&s_menu);
    }
    (void)nt_ui_menu_item(&s_menu, KEY_EDIT, "Edit");
    nt_ui_menu_end(&s_menu);
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

/* ============================ immediate-mode full menu ============================ */

/* ---- Scope-stack id distinctness — sibling keys at one scope and the SAME key under a pushed submenu
 *      scope all derive distinct, non-zero ids (mirrors the menu_hash_id non-alias guarantee). Drives
 *      nt_ui_menu_test_item_id(scope, key) — position-stable, no idx folded in. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat distinctness asserts, not real nesting
static void test_menu_item_id_distinct_siblings_and_scopes(void) {
    /* root scope = the menu id; two sibling keys at idx 0/1 */
    const uint32_t a = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t b = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    /* the SAME key reused under a pushed submenu scope (submenu_begin pushes its derived id as scope) */
    const uint32_t sub_scope = nt_ui_menu_test_item_id(MENU_A, KEY_FILE); /* the File submenu's own id == its row id */
    const uint32_t c = nt_ui_menu_test_item_id(sub_scope, KEY_FILE);      /* reuse KEY_FILE one level deeper */

    TEST_ASSERT_TRUE_MESSAGE(a != 0U && b != 0U && c != 0U, "no derived item id may be 0 (the no-widget sentinel)");
    TEST_ASSERT_TRUE_MESSAGE(a != b, "sibling keys at one scope must derive distinct ids");
    TEST_ASSERT_TRUE_MESSAGE(a != c, "the same key under a pushed submenu scope must NOT alias the root id");
    TEST_ASSERT_TRUE_MESSAGE(b != c, "cross-scope ids must stay distinct");
}

/* ---- Frame-record nav — across two frames build a 3-item level, press Down, and assert the frame-record
 *      introspection probe maps focus to the recorded item id (focus committed in menu_end is observable
 *      the following frame — a 1-frame effect). Drives nt_ui_menu_test_focus_item_id(menu_id, depth). ---- */
static void test_menu_record_nav_focuses_recorded_item(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Frame 1: lay out the root level (records {File, Edit, ...}); no key yet -> no focus committed. */
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    /* Frame 2: Down — nav runs in menu_end against THIS frame's record, focusing the first enabled item. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    /* Frame 3: settle so the focus committed in frame 2 is observable via the record probe (1-frame effect).
     * poll() consumes the frame-2 Down PRESSED edge (clear_all_keys alone leaves the sticky edge, which
     * would re-step focus this frame). */
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_frame(&st, &style, 0.0F, 0.0F);

    const uint32_t focus_id = nt_ui_menu_test_focus_item_id(s_fx.ctx, &s_menu, MENU_A, 0U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), focus_id, "Down must focus the first recorded item (this-frame record, 1-frame effect)");
}

/* ---- A custom-content row with non_activatable lets an inner button own the click — the inner button
 *      reports clicked while the row id reports !clicked. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_item_begin_activatable_false_child_owns_click(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    const uint32_t row_key = KEY_NEW;
    const uint32_t inner_btn = 0x1B7701U;
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, row_key);

    /* Frames 0-1 bake the inner button bbox (1-frame IM lag); frame 2 PRESSES over it (capture); frame 3
     * RELEASES over it -> clicked = is_released && over fires on the inner child. The non_activatable
     * row must NOT steal that click (the chain stays open — no activation). item_begin returns DECLARE-BODY
     * (true while open) — the body is guarded by it; the click is owned by the inner child. */
    bool declared = false;
    bool btn_clicked = false;
    bool end_activated = false;
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
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
        if (nt_ui_menu_item_begin(&s_menu, row_key, (nt_ui_menu_item_opts_t){.non_activatable = true})) {
            declared = true;
            /* Inner interactive child owns the click; lay out a fixed button-sized element + step it. */
            CLAY({.id = (Clay_ElementId){.id = inner_btn}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
            const nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, inner_btn);
            if (in.clicked) {
                btn_clicked = true;
            }
        }
        /* item_end reports activation for an ACTIVATABLE row; a non_activatable row never activates. */
        if (nt_ui_menu_item_end(&s_menu)) {
            end_activated = true;
        }
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    }

    TEST_ASSERT_TRUE_MESSAGE(declared, "an open menu must declare the custom-content body (item_begin returns true)");
    TEST_ASSERT_TRUE_MESSAGE(btn_clicked, "the inner child button must own the click");
    /* A non_activatable row that latched the click as an activation would
     * close the chain. The chain staying OPEN proves the row did NOT steal the inner child's click. */
    TEST_ASSERT_TRUE_MESSAGE(st.open, "a non_activatable row must NOT latch the click as an activation (chain stays open)");
    TEST_ASSERT_FALSE_MESSAGE(end_activated, "item_end must return false for a non_activatable row (child owns the click)");
    (void)row_id;

    /* POSITIVE CONTROL: the same click geometry on a {.non_activatable = false} custom row with NO inner
     * child (nothing steals the click) MUST make item_end return true. Without this control the FALSE
     * assertion above could pass for the wrong reason (an item_end that always returns false). */
    nt_ui_menu_state_t st2 = {0};
    menu_im_open(&st2, 120.0F, 80.0F);
    const uint32_t ctrl_id = nt_ui_menu_test_item_id(MENU_A, row_key);
    bool ctrl_activated = false;
    for (int frame = 0; frame < 4; ++frame) {
        const bool press = (frame == 2);
        const bool release = (frame == 3);
        const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, ctrl_id);
        const float bx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
        const float by = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
        nt_pointer_t p = {.x = bx, .y = by, .active = true};
        if (press && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_down = true;
            p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        } else if (release && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_released = true;
        }
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st2, &style);
        if (nt_ui_menu_item_begin(&s_menu, row_key, (nt_ui_menu_item_opts_t){.non_activatable = false})) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {} /* no interactive child */
        }
        if (nt_ui_menu_item_end(&s_menu)) {
            ctrl_activated = true;
        }
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_TRUE_MESSAGE(ctrl_activated, "an activatable (non_activatable=false) custom row with no inner child MUST activate via item_end");
    TEST_ASSERT_FALSE_MESSAGE(st2.open, "activating the control row must close the chain (st.open -> false)");
}

/* ---- Custom row leak: a CLOSED (present-only) menu's item_begin returns false so the game skips
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
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
        if (nt_ui_menu_item_begin(&s_menu, KEY_NEW, (nt_ui_menu_item_opts_t){.non_activatable = true})) {
            declared = true; /* must stay false: the menu is closed */
            CLAY({.id = (Clay_ElementId){.id = inner_btn}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
        }
        nt_ui_menu_item_end(&s_menu);
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(declared, "a closed (present-only) menu must NOT declare the custom-content body");
    const nt_ui_bbox_t child = nt_ui_get_bbox(s_fx.ctx, inner_btn);
    TEST_ASSERT_FALSE_MESSAGE(child.found, "a closed menu's custom child must not lay out anywhere (no scene leak)");
}

/* ---- GUARDED pattern on a CLOSED menu must not poison the item scratch: item_end placed INSIDE the guard is
 *      skipped when item_begin returns false, so pending_menu_item.active must stay clear (a true no-op). The
 *      next frame OPENS the menu and declares a real custom row through the SAME guarded pattern — it must
 *      complete with no assert/poison from the prior closed frame. ---- */
static void test_menu_item_begin_closed_guarded_no_poison(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0}; /* frame 0: present-only (closed) */

    /* Frame 0: CLOSED menu, guarded pattern with item_end INSIDE the if -> begin false, body+item_end skipped. */
    nt_pointer_t p0 = {.x = 0.0F, .y = 0.0F, .active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p0, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
    bool closed_declared = false;
    if (nt_ui_menu_item_begin(&s_menu, KEY_NEW, (nt_ui_menu_item_opts_t){0})) {
        closed_declared = true; /* skipped: menu closed */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        nt_ui_menu_item_end(&s_menu); /* item_end INSIDE the guard -> skipped on a closed menu */
    }
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(closed_declared, "guarded closed menu must not declare the body");

    /* Frame 1: OPEN the menu and declare a real custom row through the same guarded pattern. If the closed frame
     * had poisoned pending_menu_item.active, item_begin's NT_ASSERT(!active) here would have fired. */
    st.open = true;
    nt_pointer_t p1 = {.x = 0.0F, .y = 0.0F, .active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p1, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
    bool open_declared = false;
    if (nt_ui_menu_item_begin(&s_menu, KEY_NEW, (nt_ui_menu_item_opts_t){0})) {
        open_declared = true;
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        nt_ui_menu_item_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu); /* must not assert "open custom row" -> scratch was clean */
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(open_declared, "an OPEN menu's guarded item_begin must declare the body (scratch clean after a closed frame)");
}

/* ---- Disabled custom row never activates: item_begin(opts{.disabled=true}) + a click over the row must
 *      make item_end return false (mirror item_ex's enabled gate). The enabled + activatable case still
 *      activates on the same click. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool menu_item_begin_click_activates(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, bool enabled) {
    const uint32_t row_key = KEY_NEW;
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, row_key);
    bool end_activated = false;
    /* Frames 0-1 bake the row bbox (1-frame IM lag); frame 2 presses over it; frame 3 releases -> click. */
    for (int frame = 0; frame < 4; ++frame) {
        const bool press = (frame == 2);
        const bool release = (frame == 3);
        const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
        const float bx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
        const float by = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
        nt_pointer_t p = {.x = bx, .y = by, .active = true};
        if (press && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_down = true;
            p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        } else if (release && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_released = true;
        }
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
        if (nt_ui_menu_item_begin(&s_menu, row_key, (nt_ui_menu_item_opts_t){.disabled = !enabled})) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {} /* game body */
        }
        if (nt_ui_menu_item_end(&s_menu)) {
            end_activated = true;
        }
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    }
    return end_activated;
}
static void test_menu_item_begin_disabled_never_activates(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    {
        nt_ui_menu_state_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        const bool act = menu_item_begin_click_activates(&st, &style, false);
        TEST_ASSERT_FALSE_MESSAGE(act, "a disabled (opts.disabled=true) custom row must NOT activate via item_end");
    }
    {
        nt_ui_menu_state_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        const bool act = menu_item_begin_click_activates(&st, &style, true);
        TEST_ASSERT_TRUE_MESSAGE(act, "an enabled activatable custom row must still activate on a click");
    }
}

/* Drive a plain nt_ui_menu_item leaf and report whether its INLINE return fired on the activation frame.
 * Mirrors menu_item_begin_click_activates but for the item (not item_begin) path: frames 0-1 bake the row
 * bbox (1-frame IM lag), frame 2 presses over its center, frame 3 releases -> the item returns clicked. */
static bool menu_item_click_activates(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    const uint32_t row_key = KEY_NEW;
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, row_key);
    bool activated = false;
    for (int frame = 0; frame < 4; ++frame) {
        const bool press = (frame == 2);
        const bool release = (frame == 3);
        const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
        const float bx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
        const float by = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
        nt_pointer_t p = {.x = bx, .y = by, .active = true};
        if (press && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_down = true;
            p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        } else if (release && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_released = true;
        }
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
        if (nt_ui_menu_item(&s_menu, row_key, "Leaf")) {
            activated = true;
        }
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    }
    return activated;
}

/* ---- The primary same-frame mouse idiom `if (menu_item()) act()` activates a plain leaf: the inline
 *      return goes true on the click frame AND the activation closes the chain (st.open -> false). ---- */
static void test_menu_item_click_activates_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    const bool act = menu_item_click_activates(&st, &style);
    TEST_ASSERT_TRUE_MESSAGE(act, "a plain menu_item leaf must return clicked on the mouse activation frame");
    TEST_ASSERT_FALSE_MESSAGE(st.open, "activating a leaf must close the chain (st.open -> false)");
}

/* ---- Duplicate sibling key: two nt_ui_menu_item calls sharing a key in ONE open level alias the same
 *      scope-stack id -> the per-level duplicate-sibling-key NT_ASSERT must fire. ---- */
static void test_menu_duplicate_sibling_key_asserts(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, &st, &style);
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "First");
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "Dup"); /* same key in the same level -> aliased id */
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    TEST_ASSERT_TRUE_MESSAGE(tripped, "two sibling items sharing a key must trip the duplicate-sibling-key NT_ASSERT");
}

/* One frame: a section header (separator_text) sits between two leaves. Used to assert Down focus skips
 * the header (it advances item_idx without recording a nav entry, exactly like a plain separator). */
static void menu_im_frame_sep_text(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    (void)nt_ui_menu_item(&s_menu, KEY_NEW, "Top");
    nt_ui_menu_separator_text(&s_menu, "Section");
    act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT), nt_ui_menu_item(&s_menu, KEY_QUIT, "Bottom"));
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}

/* ---- A separator_text section header is non-interactive: it registers NO row id and Down focus skips it
 *      (Top -> Bottom), exactly like a plain separator. ---- */
static void test_menu_separator_text_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* the header (index 1) never registers a row id -> its bbox is not found */
    menu_im_frame_sep_text(&st, &style);
    menu_im_frame_sep_text(&st, &style);
    const nt_ui_bbox_t hdr = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_row_id(MENU_A, 0U, 1U));
    TEST_ASSERT_FALSE_MESSAGE(hdr.found, "a separator_text header must not register an interactive row id");

    /* Watch Bottom's inline return (keyboard, 1-frame latency). */
    s_act_capture_id = nt_ui_menu_test_item_id(MENU_A, KEY_QUIT);
    s_act_hit = false;

    /* Down focuses Top, a second Down must SKIP the header and land on Bottom. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame_sep_text(&st, &style);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame_sep_text(&st, &style);
    menu_key(NT_KEY_ENTER);
    menu_im_frame_sep_text(&st, &style);
    nt_input_poll();
    nt_input_clear_all_keys();
    menu_im_frame_sep_text(&st, &style);
    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "Down must skip the section header (focus Top->Bottom); Bottom's inline return fires on Enter");
}

/* Drive a disabled item_ex row with a mouse click; report whether its inline return fired. Mirrors
 * menu_item_click_activates but on the item_ex path with opts.disabled=true. */
static bool menu_item_ex_disabled_click(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    const uint32_t row_key = KEY_NEW;
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, row_key);
    bool activated = false;
    for (int frame = 0; frame < 4; ++frame) {
        const bool press = (frame == 2);
        const bool release = (frame == 3);
        const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
        const float bx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
        const float by = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
        nt_pointer_t p = {.x = bx, .y = by, .active = true};
        if (press && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_down = true;
            p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        } else if (release && bb.found) {
            p.buttons[NT_BUTTON_LEFT].is_released = true;
        }
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
        if (nt_ui_menu_item_ex(&s_menu, row_key, "Disabled", (nt_ui_menu_item_opts_t){.disabled = true})) {
            activated = true;
        }
        nt_ui_menu_end(&s_menu);
        nt_ui_end(s_fx.ctx);
    }
    return activated;
}

/* ---- A disabled item_ex row never activates on a mouse click and the chain stays open (the disabled
 *      gate covers the item_ex path, not only item_begin). ---- */
static void test_menu_item_ex_disabled_no_activate(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    const bool act = menu_item_ex_disabled_click(&st, &style);
    TEST_ASSERT_FALSE_MESSAGE(act, "a disabled item_ex row must NOT activate on a click");
    TEST_ASSERT_TRUE_MESSAGE(st.open, "a disabled-row click must leave the chain open (no activation)");
}

/* One frame: a submenu parent declared via submenu_begin_ex (icon + caller-controlled enabled). The body
 * declares one child leaf when the parent opens. Used to assert the icon gutter cell exists and a disabled
 * parent never flies out. */
static void menu_im_frame_submenu_ex(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, bool enabled) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    nt_ui_menu_item_opts_t opts = {0};
    opts.icon = s_icon_ref;
    opts.disabled = !enabled;
    if (nt_ui_menu_submenu_begin_ex(&s_menu, KEY_OPEN, "Parent", opts)) {
        (void)nt_ui_menu_item(&s_menu, KEY_PROJECT, "Child");
        nt_ui_menu_submenu_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}

/* ---- submenu_begin_ex honors opts.icon (the parent declares an icon gutter cell) and opts.disabled=true
 *      (a disabled parent returns false / never flies out, even when the open chain points at it). ---- */
static void test_menu_submenu_begin_ex_icon_disabled(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Enabled parent: two warm frames so the icon gutter cell (index 0) bakes. */
    menu_im_frame_submenu_ex(&st, &style, true);
    menu_im_frame_submenu_ex(&st, &style, true);
    const nt_ui_bbox_t icon = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(MENU_A, 0U, 0U));
    TEST_ASSERT_TRUE_MESSAGE(icon.found, "submenu_begin_ex must declare the parent's icon gutter cell");

    /* Force the open chain to point at the parent (depth-0 item 0), then redeclare it DISABLED: it must not
     * fly out (the child panel never lays out) even though open_path targets it. */
    nt_ui_menu_test_force_open_to(s_fx.ctx, MENU_A, 0U);
    menu_im_frame_submenu_ex(&st, &style, false);
    menu_im_frame_submenu_ex(&st, &style, false);
    const nt_ui_bbox_t child_panel = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U));
    TEST_ASSERT_FALSE_MESSAGE(child_panel.found, "a disabled submenu parent must NOT fly out (no child panel) even when open_path targets it");
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
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    nt_ui_menu_item_opts_t save = {.shortcut = "Ctrl+S", .selected = true};
    (void)nt_ui_menu_item_ex(&s_menu, KEY_NEW, "Save", save);
    (void)nt_ui_menu_item_ex(&s_menu, KEY_QUIT, "Plain", (nt_ui_menu_item_opts_t){0});
    nt_ui_menu_end(&s_menu);
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
    /* Position lock: the GROW spacer right-aligns the shortcut, so its cell sits in the row's RIGHT half
     * (right of the leading label region), not just present somewhere. The interactive row element uses the
     * scope-stack id (mix(scope,key)), not the KIND_ROW probe id. */
    const nt_ui_bbox_t row = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_item_id(MENU_A, KEY_NEW));
    TEST_ASSERT_TRUE_MESSAGE(row.found, "the rich row must register an interactive row bbox");
    TEST_ASSERT_TRUE_MESSAGE(sc.x > row.x + (row.width * 0.5F), "the shortcut cell must be right-aligned (right half of the row), not at the label x");
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
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "New");
        (void)nt_ui_menu_item(&s_menu, KEY_PROJECT, "Project");
        nt_ui_menu_submenu_end(&s_menu);
    }
    (void)nt_ui_menu_item(&s_menu, KEY_EDIT, "Edit");
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}

/* Center of a root row's prev-frame bbox (0,0 if not yet laid out). */
static void menu_row_center(uint32_t row_id, float *cx, float *cy) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
    *cx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
    *cy = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
}

/* ---- Hover-open: hovering a parent ROW flies its submenu out (no click), with 1-frame IM lag. ---- */
static void test_menu_hover_opens_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    nt_input_clear_all_keys();

    const uint32_t file_row = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);

    /* F1: lay out the root so the File row bbox exists next frame. */
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-1, nt_ui_menu_test_open_path(s_fx.ctx, MENU_A, 0U), "nothing open before any hover");

    /* F2: hover the File parent row center -> records the hover; menu_end commits open_path (1-frame lag). */
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file_row, &fx, &fy);
    menu_im_hover_frame(&st, &style, fx, fy);

    /* F3: settle; open_path[0] now points at File (running idx 0), its submenu flies out. */
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, nt_ui_menu_test_open_path(s_fx.ctx, MENU_A, 0U), "hovering the parent row must open its submenu (idx 0)");
    TEST_ASSERT_TRUE(st.open);
}

/* ---- Leave-close: with File's submenu open, hovering the sibling Edit LEAF collapses it once the
 *      mouse-aim corridor releases (the cursor is no longer aiming at the open child). ---- */
static void test_menu_hover_sibling_leaf_collapses_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_state_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    nt_input_clear_all_keys();

    const uint32_t file_row = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t edit_row = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);

    /* Open File's submenu via hover (settle a couple frames). */
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file_row, &fx, &fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, nt_ui_menu_test_open_path(s_fx.ctx, MENU_A, 0U), "File submenu open before the sibling hover");

    /* Hover the Edit sibling leaf far from the open child corridor for enough frames that the dwell timer
     * crosses AIM_FALLBACK and the corridor releases -> the open child collapses. */
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit_row, &ex, &ey);
    bool collapsed = false;
    for (int f = 0; f < 30 && !collapsed; ++f) {
        menu_im_hover_frame(&st, &style, ex, ey);
        collapsed = (nt_ui_menu_test_open_path(s_fx.ctx, MENU_A, 0U) < 0);
    }
    TEST_ASSERT_TRUE_MESSAGE(collapsed, "hovering a sibling leaf (corridor released) must collapse the open submenu");
}

/* One menu frame with a single File submenu (two child items), driven by the keyboard nav path. */
static void menu_im_submenu_frame(nt_ui_menu_state_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_menu_begin(&s_menu, s_fx.ctx, NULL, 0U, MENU_A, st, style);
    if (nt_ui_menu_submenu_begin(&s_menu, KEY_FILE, "File")) {
        (void)nt_ui_menu_item(&s_menu, KEY_NEW, "ChildItemLong");
        (void)nt_ui_menu_item(&s_menu, KEY_QUIT, "ChildItem2");
        nt_ui_menu_submenu_end(&s_menu);
    }
    nt_ui_menu_end(&s_menu);
    nt_ui_end(s_fx.ctx);
}

/* ---- Right-edge flip: a menu opened hard against the RIGHT screen edge must (a) clamp the ROOT
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

    /* (b) Submenu flipped LEFT: popup-core attaches a LEFT-side panel's RIGHT edge to the anchor's left
     *      edge, and the anchor is the parent row — so the flip is an exact geometric identity, not a
     *      "somewhere to the left" (CENTER or a vertical side would also land left of the panel). */
    const nt_ui_bbox_t sub = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U));
    TEST_ASSERT_TRUE(sub.found);
    const nt_ui_bbox_t file_row = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_item_id(MENU_A, KEY_FILE));
    TEST_ASSERT_TRUE(file_row.found);
    TEST_ASSERT_TRUE_MESSAGE(sub.width > 0.0F, "the submenu panel must have measured a size");
    TEST_ASSERT_TRUE_MESSAGE(fabsf((sub.x + sub.width) - file_row.x) <= 0.5F, "a LEFT flip puts the submenu's right edge exactly on its anchor row's left edge");
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
    RUN_TEST(test_menu_kbd_open_at_depth_cap_is_noop);
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
    RUN_TEST(test_menu_record_nav_focuses_recorded_item);
    RUN_TEST(test_menu_item_begin_activatable_false_child_owns_click);
    RUN_TEST(test_menu_item_begin_closed_skips_body);
    RUN_TEST(test_menu_item_begin_closed_guarded_no_poison);
    RUN_TEST(test_menu_item_begin_disabled_never_activates);
    RUN_TEST(test_menu_shortcut_cell_on_rich_row_only);
    RUN_TEST(test_menu_check_cell_when_selected);
    RUN_TEST(test_menu_item_click_activates_leaf);
    RUN_TEST(test_menu_duplicate_sibling_key_asserts);
    RUN_TEST(test_menu_separator_text_non_interactive);
    RUN_TEST(test_menu_item_ex_disabled_no_activate);
    RUN_TEST(test_menu_submenu_begin_ex_icon_disabled);
    return UNITY_END();
}
