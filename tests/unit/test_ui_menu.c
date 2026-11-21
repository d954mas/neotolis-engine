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
#include "input/nt_input_feed.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_input_test_frame.h"
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

/* Menu style ABI: 3 ref + 9 u32 + 3 float + 8 u16 = 112. Mirrors the _Static_assert in nt_ui_menu.h
 * (both must agree). */
#define EXPECTED_MENU_STYLE_ABI 112U

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

/* The frame helpers fill the pointer's movement since their last frame, as nt_input does: a pointer that
 * moves takes focus back from the keyboard. */
static float s_ptr_x;
static float s_ptr_y;
static nt_pointer_t menu_moved(const nt_pointer_t *p) {
    nt_pointer_t q = *p;
    q.dx = q.x - s_ptr_x;
    q.dy = q.y - s_ptr_y;
    s_ptr_x = q.x;
    s_ptr_y = q.y;
    return q;
}

void setUp(void) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_ptr_x = 0.0F;
    s_ptr_y = 0.0F;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* Inline-activation capture: activation is reported by the row's inline
 * bool for BOTH mouse and keyboard. The immediate drivers OR each target row's return into s_act_hit so a
 * test can assert the leaf's inline return fired. Reset per test. */
static uint32_t s_act_capture_id; /* the fully-scoped row id a test wants to watch (0 = watch nothing) */
static bool s_act_hit;            /* set true the frame the watched row's inline return goes true */
/* OR a row's inline return into the capture if its scoped id matches the watched id. */
static void act_capture(uint32_t row_scoped_id, bool clicked) {
    if (clicked && row_scoped_id == s_act_capture_id) {
        s_act_hit = true;
    }
}

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. The style line
 *      tracks the size symbolically via EXPECTED_MENU_STYLE_ABI. ---- */
static void test_menu_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(EXPECTED_MENU_STYLE_ABI, (unsigned)sizeof(nt_ui_menu_style_t));
    TEST_ASSERT_EQUAL_UINT((unsigned)(16U + ((((sizeof(void *) + 4U) + 7U) / 8U) * 8U)), (unsigned)sizeof(nt_ui_menu_item_opts_t));
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

/* ---- Aim corners: the near edge faces the apex — the LEFT edge of a submenu opened to the right, the
 *      RIGHT edge of one edge-flipped to the left. ---- */
static void test_menu_aim_corners_mirror(void) {
    float bx = 0.0F;
    float by = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    /* submenu rect at (200,100) 150x200, apex on its left -> corners at x==200 (its left edge) */
    nt_ui_menu_test_aim_corners(200.0F, 100.0F, 150.0F, 200.0F, 190.0F, &bx, &by, &cx, &cy);
    TEST_ASSERT_TRUE(float_near(bx, 200.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(cx, 200.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(by, 100.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(cy, 300.0F, 0.01F));
    /* apex on its right (edge-flipped LEFT) -> mirror to the RIGHT edge x==350 */
    nt_ui_menu_test_aim_corners(200.0F, 100.0F, 150.0F, 200.0F, 360.0F, &bx, &by, &cx, &cy);
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

    /* prime the corridor at the parent's right edge */
    nt_ui_menu_aim_t aim = {0};
    (void)nt_ui_menu_test_hover_intent(&aim, 290.0F, 110.0F, sub_x, sub_y, sub_w, sub_h, dt);

    /* travel diagonally toward the submenu's bottom-left corner; small steps keep prev->near triangle
     * fully covering the path. */
    const float path_x[] = {295.0F, 298.0F, 300.0F, 305.0F};
    const float path_y[] = {180.0F, 260.0F, 340.0F, 390.0F};
    for (int i = 0; i < 4; ++i) {
        bool keep = nt_ui_menu_test_hover_intent(&aim, path_x[i], path_y[i], sub_x, sub_y, sub_w, sub_h, dt);
        TEST_ASSERT_TRUE_MESSAGE(keep, "submenu must stay open while aiming along the diagonal");
    }
    TEST_ASSERT_TRUE(aim.timer < NT_UI_MENU_AIM_FALLBACK_SECS);
}

/* ---- Aim SWITCH: the cursor sits OFF the triangle; after AIM_FALLBACK the keep flips false. ---- */
static void test_menu_aim_switch_after_fallback(void) {
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    const float dt = 1.0F / 60.0F;

    nt_ui_menu_aim_t aim = {0};
    (void)nt_ui_menu_test_hover_intent(&aim, 290.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, dt);

    bool last_keep = true;
    int frames_to_switch = 0;
    for (int i = 0; i < 30; ++i) {
        last_keep = nt_ui_menu_test_hover_intent(&aim, 100.0F, 250.0F, sub_x, sub_y, sub_w, sub_h, dt);
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

    nt_ui_menu_aim_t aim = {0};
    (void)nt_ui_menu_test_hover_intent(&aim, 295.0F, 150.0F, sub_x, sub_y, sub_w, sub_h, dt);

    bool last_keep = true;
    int frames_to_switch = 0;
    for (int i = 0; i < 30; ++i) {
        const float my = 200.0F + ((float)i * 6.0F);
        last_keep = nt_ui_menu_test_hover_intent(&aim, 150.0F, my, sub_x, sub_y, sub_w, sub_h, dt);
        if (!last_keep) {
            frames_to_switch = i + 1;
            break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(last_keep, "moving onto a sibling column must release the corridor (no trap)");
    TEST_ASSERT_TRUE_MESSAGE(frames_to_switch >= 1 && frames_to_switch <= 9, "sibling switch must free the user within the grace");
}

/* ---- Depth-cap constant exists and stays within the popup cap. ---- */
static void test_menu_max_depth_constant(void) {
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH >= 1);
    TEST_ASSERT_TRUE(NT_UI_MENU_MAX_DEPTH <= NT_UI_MODAL_MAX_DEPTH);
}

/* ============================ immediate full menu UI ============================ */

/* Produce exactly ONE fresh pressed-key edge for this frame. */
static void menu_key(nt_key_t key) { nt_input_test_key_press(key); }

/* True when the menu level `depth` was laid out last frame. */
static bool menu_level_found(uint8_t depth) { return nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, depth)).found; }

/* Immediate driver: open the menu at (anchor_x, anchor_y) by setting the game-owned open flag + anchor.
 * Mirrors what nt_ui_menu_open_trigger does (arm at the cursor) without a right-click. */
static void menu_im_open(nt_ui_menu_t *st, float anchor_x, float anchor_y) {
    st->open = true;
    st->anchor_x = anchor_x;
    st->anchor_y = anchor_y;
}

/* One immediate menu frame, tree: File > (New, Open > (Project, File2), Quit), Edit. Declares the tree
 * via begin/item/submenu_begin..submenu_end/menu_end. The snapshot pointer is positioned but the keyboard
 * path is what most tests assert against (deterministic, no prev-frame bbox). */
static void menu_im_frame_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    const nt_pointer_t moved = menu_moved(p);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &moved, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        const uint32_t file_scope = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
        const uint32_t open_scope = nt_ui_menu_test_item_id(file_scope, KEY_OPEN);
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "New");
            if (nt_ui_menu_submenu_begin(st, KEY_OPEN, "Open")) {
                (void)nt_ui_menu_item(st, KEY_PROJECT, "Project");
                act_capture(nt_ui_menu_test_item_id(open_scope, KEY_FILE2), nt_ui_menu_item(st, KEY_FILE2, "File2"));
                nt_ui_menu_submenu_end(st);
            }
            (void)nt_ui_menu_item(st, KEY_QUIT, "Quit");
            nt_ui_menu_submenu_end(st);
        }
        (void)nt_ui_menu_item(st, KEY_EDIT, "Edit");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

static void menu_im_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    menu_im_frame_ptr(st, style, &p);
}

/* A frame whose pointer presses the left button at (px,py): a mouse click or a touch tap. */
static void menu_im_press(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py, uint8_t type) {
    nt_pointer_t p = {.x = px, .y = py, .type = type, .active = true};
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    menu_im_frame_ptr(st, style, &p);
}

/* ---- Smoke: an open menu declares the root popup and balances the stack; a closed menu declares
 *      nothing (present-only). ---- */
static void test_menu_smoke_open_and_closed(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth); /* balanced */
    st.open = false;
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
}

/* ---- Keyboard-nav reaches a nested leaf: Down focuses File, Right opens its submenu, Down to Open,
 *      Enter opens the grandchild, Down to File2, Enter activates -> File2's INLINE return fires on the
 *      Enter frame, like a click, and the chain closes. ---- */
static void test_menu_kbd_nav_activates_nested_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Watch File2's inline return: keyboard Enter must fire it. */
    const uint32_t file_scope0 = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t open_scope0 = nt_ui_menu_test_item_id(file_scope0, KEY_OPEN);
    s_act_capture_id = nt_ui_menu_test_item_id(open_scope0, KEY_FILE2);
    s_act_hit = false;

    /* Frame 1: focus root item 0 (File) via Down. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 2: Right opens File's submenu (depth 1) in this frame. */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "Right opens the submenu in its own frame");
    /* Frame 3: Down moves focus within submenu to Open. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 4: Enter on the Open parent opens its grandchild (depth 2) in this frame, focus Project. */
    menu_key(NT_KEY_ENTER);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(2U), "Enter on a parent opens it in its own frame");
    /* Frame 5: Down to File2. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    /* Frame 6: Enter on File2 fires its inline return and closes the chain. */
    menu_key(NT_KEY_ENTER);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "keyboard Enter must fire the leaf's INLINE return on the Enter frame");
    TEST_ASSERT_FALSE(st.open);
}

/* ---- Nested dismiss: Esc on the deepest level closes it first (root stays open); a second Esc closes
 *      the root chain entirely. ---- */
static void test_menu_nested_dismiss_esc_deepest_first(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
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

/* ---- Outside-click dismiss: a left-click far outside every open panel closes the whole chain. ---- */
static void test_menu_outside_click_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the panel bbox exists next frame (no press) */
    menu_im_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: primary press far from the panel (bottom-right corner) -> menu-owned outside dismiss. */
    menu_im_press(&st, &style, 780.0F, 580.0F, NT_POINTER_MOUSE);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "outside-click dismisses the whole chain");
}

/* A touch tap outside dismisses like a click: the menu reads the UI's pointer, not the mouse. */
static void test_menu_outside_tap_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    menu_im_press(&st, &style, 780.0F, 580.0F, NT_POINTER_TOUCH);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "an outside tap dismisses the whole chain");
}

/* ---- Inside-click does NOT dismiss: a primary press INSIDE the root panel is a row interaction. ---- */
static void test_menu_inside_click_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the root panel bbox exists next frame. Anchor at (120,80) -> panel grows below. */
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: press well inside the root panel body (just below the anchor) -> not a dismiss. */
    menu_im_press(&st, &style, 140.0F, 100.0F, NT_POINTER_MOUSE);

    TEST_ASSERT_TRUE_MESSAGE(st.open, "a click inside an open panel must NOT dismiss the chain");
}

/* ---- Right-click outside (a frame AFTER opening) dismisses the chain, mirroring the left-click dismiss. ---- */
static void test_menu_right_click_outside_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the panel bbox exists next frame (no press) */
    menu_im_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: RIGHT press far from the panel -> menu-owned outside dismiss. The snapshot pointer carries
     * the right-button edge so the menu reads a consistent right-press this frame. */
    nt_pointer_t p = {.x = 780.0F, .y = 580.0F, .active = true};
    p.buttons[NT_BUTTON_RIGHT].is_down = true;
    p.buttons[NT_BUTTON_RIGHT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
        if (nt_ui_menu_submenu_begin(&st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(&st, KEY_NEW, "New");
            nt_ui_menu_submenu_end(&st);
        }
        (void)nt_ui_menu_item(&st, KEY_EDIT, "Edit");
        nt_ui_menu_end(&st);
    }
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_FALSE_MESSAGE(st.open, "right-click outside (post-open) dismisses the whole chain");
}

/* Immediate frame for a two-branch root: File > (New, Open > (Project, File2), Quit) and Tools > (Opt).
 * Used by the branch-switch tests (the user must reach the OTHER root branch while one is open). */
static void menu_im_frame2_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    const nt_pointer_t moved = menu_moved(p);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &moved, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        const uint32_t tools_scope = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS);
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "New");
            if (nt_ui_menu_submenu_begin(st, KEY_OPEN, "Open")) {
                (void)nt_ui_menu_item(st, KEY_PROJECT, "Project");
                (void)nt_ui_menu_item(st, KEY_FILE2, "File2");
                nt_ui_menu_submenu_end(st);
            }
            (void)nt_ui_menu_item(st, KEY_QUIT, "Quit");
            nt_ui_menu_submenu_end(st);
        }
        if (nt_ui_menu_submenu_begin(st, KEY_TOOLS, "Tools")) {
            act_capture(nt_ui_menu_test_item_id(tools_scope, KEY_OPT), nt_ui_menu_item(st, KEY_OPT, "Opt"));
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

static void menu_im_frame2(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py, bool press) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    p.buttons[NT_BUTTON_LEFT].is_down = press;
    p.buttons[NT_BUTTON_LEFT].is_pressed = press;
    menu_im_frame2_ptr(st, style, &p);
}

/* Center of a row's prev-frame bbox (0,0 if not yet laid out). */
static void menu_row_center(uint32_t row_id, float *cx, float *cy) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
    *cx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
    *cy = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
}

/* ---- Switch root branch via keyboard while a submenu is open: open File submenu, Left collapses back
 *      to root, Down moves to Tools, Right opens ITS submenu, Enter activates Opt. ---- */
static void test_menu_switch_root_branch_while_submenu_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    menu_key(NT_KEY_ARROW_DOWN); /* focus File (root item 0) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT); /* open File submenu (depth 1) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    TEST_ASSERT_TRUE(st.open);

    menu_key(NT_KEY_ARROW_LEFT); /* collapse File submenu back to root */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "Left collapses the submenu but the root stays open");

    /* Watch the Tools>Opt leaf's inline return. */
    s_act_capture_id = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    s_act_hit = false;

    menu_key(NT_KEY_ARROW_DOWN); /* move to Tools (root item 1) */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ARROW_RIGHT); /* open Tools submenu */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);
    menu_key(NT_KEY_ENTER); /* activate Opt */
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false);

    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "must reach the OTHER root branch's leaf (inline return) while the first was open");
    TEST_ASSERT_FALSE(st.open);
}

/* ---- Ancestor root row is still HITTABLE while a submenu is open (no occluding per-level catcher). ---- */
static void test_menu_root_row_hittable_while_submenu_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
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

typedef void (*menu_frame_fn)(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py);

static void menu_im_frame2_idle(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) { menu_im_frame2(st, style, px, py, false); }

/* Opens File > Open (depth 2) through the keyboard, one key per frame, with the pointer off the menu. */
static void menu_open_file_open(nt_ui_menu_t *st, nt_ui_menu_style_t *style, menu_frame_fn frame) {
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    for (int i = 0; i < 4; ++i) {
        menu_key(keys[i]);
        frame(st, style, 0.0F, 0.0F);
    }
    nt_input_init();
    frame(st, style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(2U), "File > Open must be open");
}

/* Down and Enter on the two-branch tree with the pointer off the menu; true when `leaf` activated. */
static bool menu_down_enter_activates(nt_ui_menu_t *st, nt_ui_menu_style_t *style, uint32_t leaf) {
    s_act_capture_id = leaf;
    s_act_hit = false;
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame2_idle(st, style, 0.0F, 0.0F);
    menu_key(NT_KEY_ENTER);
    menu_im_frame2_idle(st, style, 0.0F, 0.0F);
    return s_act_hit;
}

/* ---- Clicking Tools while File > Open is open replaces the whole branch: no level 2 remains, and the
 *      keyboard acts on the Tools submenu. ---- */
static void test_menu_click_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_open_file_open(&st, &style, menu_im_frame2_idle);

    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &cx, &cy);
    /* Two frames over Tools keep the aim corridor, so only the click can switch the branch. */
    menu_im_frame2(&st, &style, cx, cy, true);
    nt_pointer_t release = {.x = cx, .y = cy, .active = true};
    release.buttons[NT_BUTTON_LEFT].is_released = true;
    menu_im_frame2_ptr(&st, &style, &release);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "clicking a root parent must NOT dismiss the chain");

    /* Down in the very next frame: a kept File > Open would leave the keyboard on a lost level. */
    const uint32_t opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    TEST_ASSERT_TRUE_MESSAGE(menu_down_enter_activates(&st, &style, opt), "Down + Enter must activate Tools > Opt");
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(2U), "switching the root branch must close File > Open");
}

/* ---- Hovering Tools while File > Open is open replaces the whole branch once the corridor releases. ---- */
static void test_menu_hover_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_open_file_open(&st, &style, menu_im_frame2_idle);

    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &cx, &cy);
    const uint32_t opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    const uint32_t tools = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS);
    for (int f = 0; f < 30 && st.open_path[0] != tools; ++f) {
        menu_im_frame2_idle(&st, &style, cx, cy);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(tools, st.open_path[0], "hovering Tools must open its submenu after the corridor releases");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[1], "switching the root branch must close File > Open in the same frame");
    TEST_ASSERT_TRUE_MESSAGE(menu_down_enter_activates(&st, &style, opt), "Down + Enter must activate Tools > Opt");
}

/* ---- Collapsing File by hovering the Edit leaf forgets File > Open: hovering File again opens only
 *      File's own submenu. ---- */
static void test_menu_hover_collapse_reopen_forgets_descendants(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_open_file_open(&st, &style, menu_im_frame);

    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    for (int f = 0; f < 30 && menu_level_found(1U); ++f) {
        menu_im_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "hovering the Edit leaf must collapse File");

    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    for (int f = 0; f < 3 && !menu_level_found(1U); ++f) {
        menu_im_frame(&st, &style, fx, fy);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "hovering File must reopen its submenu");
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(2U), "a collapsed branch must not reopen File > Open");
}

/* ============================ keyed selection in a changing tree ============================ */

/* Keys from KEY_PARENT up declare submenus with one leaf KEY_OPT (after a disabled leaf when
 * disabled_first_child, none when empty_children). disabled_key is declared disabled; leaf_key turns
 * that parent into a plain leaf; separator_first puts a separator before the rows. Every leaf reports
 * activation through act_capture. */
#define KEY_PARENT 100U
#define KEY_P1 100U
#define KEY_P2 101U
#define KEY_L0 40U
#define KEY_L1 41U
#define KEY_L2 42U
#define KEY_L3 43U
typedef struct {
    const uint32_t *keys;
    int count;
    uint32_t disabled_key;
    uint32_t leaf_key;
    bool disabled_first_child;
    bool empty_children;
    bool separator_first;
} menu_dyn_tree_t;

static void menu_dyn_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &(nt_pointer_t){.active = true}, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (t->separator_first) {
            nt_ui_menu_separator(st);
        }
        for (int i = 0; i < t->count; ++i) {
            const uint32_t key = t->keys[i];
            const uint32_t id = nt_ui_menu_test_item_id(MENU_A, key);
            const nt_ui_menu_item_opts_t opts = {.disabled = (key == t->disabled_key)};
            if (key < KEY_PARENT || key == t->leaf_key) {
                act_capture(id, nt_ui_menu_item_ex(st, key, "Leaf", opts));
            } else if (nt_ui_menu_submenu_begin_ex(st, key, "Parent", opts)) {
                if (t->disabled_first_child) {
                    (void)nt_ui_menu_item_ex(st, KEY_L0, "Off", (nt_ui_menu_item_opts_t){.disabled = true});
                }
                if (!t->empty_children) {
                    act_capture(nt_ui_menu_test_item_id(id, KEY_OPT), nt_ui_menu_item(st, KEY_OPT, "Opt"));
                }
                nt_ui_menu_submenu_end(st);
            }
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* One frame per key, then one idle frame. */
static void menu_dyn_keys(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t, const nt_key_t *keys, int count) {
    for (int i = 0; i < count; ++i) {
        menu_key(keys[i]);
        menu_dyn_frame(st, style, t);
    }
    nt_input_init();
    menu_dyn_frame(st, style, t);
}

/* One Enter frame; true when `leaf` activated. */
static bool menu_dyn_enter_activates(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t, uint32_t leaf) {
    s_act_capture_id = leaf;
    s_act_hit = false;
    menu_key(NT_KEY_ENTER);
    menu_dyn_frame(st, style, t);
    return s_act_hit;
}

/* ---- Inserting a row before the open parent keeps that parent open. ---- */
static void test_menu_insert_before_open_parent_keeps_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    static const uint32_t before[] = {KEY_P1, KEY_P2};
    static const uint32_t after[] = {KEY_L0, KEY_P1, KEY_P2};
    const menu_dyn_tree_t t0 = {.keys = before, .count = 2};
    const menu_dyn_tree_t t1 = {.keys = after, .count = 3};
    const nt_key_t open_p2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    menu_dyn_keys(&st, &style, &t0, open_p2, 3);
    menu_dyn_frame(&st, &style, &t1);
    const uint32_t p2_opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_P2), KEY_OPT);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t1, p2_opt), "Enter must activate P2 > Opt after a row is inserted before P2");
}

/* ---- Inserting, deleting or reordering rows, or adding a separator, keeps keyboard focus on the same key. ---- */
static void test_menu_focus_follows_key_across_edits(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    static const uint32_t l012[] = {KEY_L0, KEY_L1, KEY_L2};
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    static const uint32_t l23[] = {KEY_L2, KEY_L3};
    static const uint32_t l21[] = {KEY_L2, KEY_L1};
    const struct {
        menu_dyn_tree_t before;
        menu_dyn_tree_t after;
        uint32_t focus_key;
    } cases[] = {
        {{.keys = l12, .count = 2}, {.keys = l012, .count = 3}, KEY_L2},                         /* insert */
        {{.keys = l123, .count = 3}, {.keys = l23, .count = 2}, KEY_L3},                         /* delete */
        {{.keys = l12, .count = 2}, {.keys = l21, .count = 2}, KEY_L2},                          /* reorder */
        {{.keys = l12, .count = 2}, {.keys = l12, .count = 2, .separator_first = true}, KEY_L2}, /* separator */
    };
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    for (int c = 0; c < 4; ++c) {
        nt_ui_menu_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        menu_dyn_keys(&st, &style, &cases[c].before, downs, cases[c].before.count);
        menu_dyn_frame(&st, &style, &cases[c].after);
        TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &cases[c].after, nt_ui_menu_test_item_id(MENU_A, cases[c].focus_key)), "Enter must activate the focused key after the edit");
    }
}

/* ---- Removing the focused row leaves no focus: Enter does nothing and Down starts at the first row. A
 *      disabled focused row keeps its place: Enter does nothing and Down steps to the row after it. ---- */
static void test_menu_lost_focused_row(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    static const uint32_t l13[] = {KEY_L1, KEY_L3};
    const menu_dyn_tree_t full = {.keys = l123, .count = 3};
    const struct {
        menu_dyn_tree_t lost;
        uint32_t after_down;
    } cases[] = {
        {{.keys = l13, .count = 2}, KEY_L1},                          /* removed */
        {{.keys = l123, .count = 3, .disabled_key = KEY_L2}, KEY_L3}, /* disabled */
    };
    const nt_key_t focus_l2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    for (int c = 0; c < 2; ++c) {
        nt_ui_menu_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        menu_dyn_keys(&st, &style, &full, focus_l2, 2);
        menu_dyn_frame(&st, &style, &cases[c].lost);
        TEST_ASSERT_FALSE_MESSAGE(menu_dyn_enter_activates(&st, &style, &cases[c].lost, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "Enter on a lost focused row does nothing");
        TEST_ASSERT_TRUE(st.open);
        const nt_key_t down = NT_KEY_ARROW_DOWN;
        menu_dyn_keys(&st, &style, &cases[c].lost, &down, 1);
        TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &cases[c].lost, nt_ui_menu_test_item_id(MENU_A, cases[c].after_down)), "Down after a lost focused row");
    }
}

/* ---- Up from no focus lands on the last row; Down past the last row wraps to the first. ---- */
static void test_menu_up_down_wrap(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    const menu_dyn_tree_t t = {.keys = l123, .count = 3};
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    const nt_key_t up = NT_KEY_ARROW_UP;
    menu_dyn_keys(&st, &style, &t, &up, 1);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L3)), "Up from no focus lands on the last row");
    menu_im_open(&st, 120.0F, 80.0F);
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, downs, 4);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Down past the last row wraps to the first");
}

/* ---- Removing the open parent, disabling it or turning it into a leaf closes its submenu; keys in that
 *      frame do nothing, and the keyboard then navigates the root. ---- */
static void test_menu_lost_open_parent_closes_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t p12[] = {KEY_P1, KEY_P2};
    static const uint32_t p1[] = {KEY_P1};
    const menu_dyn_tree_t open_tree = {.keys = p12, .count = 2};
    const menu_dyn_tree_t lost[] = {
        {.keys = p1, .count = 1},                          /* removed */
        {.keys = p12, .count = 2, .disabled_key = KEY_P2}, /* disabled */
        {.keys = p12, .count = 2, .leaf_key = KEY_P2},     /* now a leaf */
    };
    const nt_key_t open_p2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    const nt_key_t open_first[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    const uint32_t p1_opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_P1), KEY_OPT);
    for (int c = 0; c < 3; ++c) {
        nt_ui_menu_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        menu_dyn_keys(&st, &style, &open_tree, open_p2, 3);
        TEST_ASSERT_TRUE(menu_level_found(1U));
        menu_key(NT_KEY_ESCAPE);
        menu_dyn_frame(&st, &style, &lost[c]);
        TEST_ASSERT_TRUE_MESSAGE(st.open, "Esc in the frame that loses the open parent does nothing");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[0], "a lost open parent closes its branch");
        menu_dyn_keys(&st, &style, &lost[c], open_first, 2);
        TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &lost[c], p1_opt), "the keyboard must open P1 and activate P1 > Opt");
    }
}

/* ---- Opening a submenu from the keyboard focuses its first enabled row. ---- */
static void test_menu_kbd_open_focuses_first_enabled_row(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    static const uint32_t p1[] = {KEY_P1};
    const menu_dyn_tree_t t = {.keys = p1, .count = 1, .disabled_first_child = true};
    const nt_key_t open_p1[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    menu_dyn_keys(&st, &style, &t, open_p1, 2);
    const uint32_t p1_opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_P1), KEY_OPT);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, p1_opt), "Enter must activate the first enabled child");
}

/* ---- An empty submenu opens without focus; Left returns to its parent. ---- */
static void test_menu_kbd_empty_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    static const uint32_t keys[] = {KEY_P1, KEY_L1};
    const menu_dyn_tree_t t = {.keys = keys, .count = 2, .empty_children = true};
    const nt_key_t walk[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN, NT_KEY_ENTER, NT_KEY_ARROW_LEFT, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, walk, 6);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_FALSE(menu_level_found(1U));
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Down after Left must reach the next root row");
}

/* ---- Depth cap: a self-referential immediate tree (a submenu_begin that recurses on itself) nests one
 *      level per keyboard open; a parent declared on the deepest level trips the depth NT_ASSERT before the
 *      popup stack is exhausted. ---- */
static void menu_im_self_ref(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &(nt_pointer_t){.active = true}, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        int guard = 0;
        while (nt_ui_menu_submenu_begin(st, KEY_FILE, "loop") && guard < NT_UI_MENU_MAX_DEPTH + 4) {
            ++guard;
        }
        for (int i = 0; i < guard; ++i) {
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}
static void test_menu_depth_cap_asserts(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_self_ref(&st, &style);

    volatile int opened = 0; /* read after longjmp */
    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        for (; opened < NT_UI_MENU_MAX_DEPTH + 2; ++opened) {
            menu_key(NT_KEY_ARROW_RIGHT);
            menu_im_self_ref(&st, &style);
        }
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    TEST_ASSERT_TRUE_MESSAGE(tripped, "a parent declared on the deepest level must trip the depth-cap NT_ASSERT");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_MENU_MAX_DEPTH - 2, opened, "every allowed level opens before the cap trips");
}

/* ============================ icons / separator / occluder ============================ */

/* An icon ref carries a non-zero atlas.id so the gutter draws an image cell (no real atlas binds in the
 * fixture, so the resolve no-ops, but the gutter cell itself lays out + the label aligns). */
static const nt_atlas_region_ref_t s_icon_ref = {.atlas = {.id = 1U}, .region = NT_ATLAS_INVALID_REGION};

/* Immediate frame for the icon/separator vitrine: Iconed (item_ex with icon), a separator, Plain (no
 * icon), Parent (submenu marker). */
static void menu_im_frame25(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        (void)nt_ui_menu_item_ex(st, KEY_NEW, "Iconed", (nt_ui_menu_item_opts_t){.icon = s_icon_ref});
        nt_ui_menu_separator(st);
        act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT), nt_ui_menu_item_ex(st, KEY_QUIT, "Plain", (nt_ui_menu_item_opts_t){0}));
        if (nt_ui_menu_submenu_begin(st, KEY_OPEN, "Parent")) {
            (void)nt_ui_menu_item(st, KEY_PROJECT, "ChildA");
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Icon gutter: with icon_size > 0 the iconed AND non-iconed rows both reserve the leading gutter,
 *      so their labels start at the SAME x. Re-expressed against item_ex(opts{.icon=...}). ---- */
static void test_menu_icon_gutter_aligns_labels(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    menu_im_frame25(&st, &style, 0.0F, 0.0F); /* lay out so the gutter cell bbox exists next frame */
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    const nt_ui_bbox_t g2 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT)));
    TEST_ASSERT_TRUE_MESSAGE(g0.found, "iconed row gutter cell must lay out");
    TEST_ASSERT_TRUE_MESSAGE(g2.found, "non-iconed row must STILL reserve an aligned-empty gutter");
    TEST_ASSERT_TRUE_MESSAGE(float_near(g0.x, g2.x, 0.5F), "both gutters must align (icon-column model)");
    TEST_ASSERT_TRUE_MESSAGE(float_near(g0.width, (float)style.icon_size, 0.5F), "gutter width == icon_size");
}

/* ---- icon_size == 0: no gutter at all (the cell is not declared). ---- */
static void test_menu_icon_gutter_absent_when_zero(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 0U;
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    TEST_ASSERT_FALSE_MESSAGE(g0.found, "icon_size 0 must declare no gutter cell");
}

/* ---- Separator is non-interactive: keyboard Down skips it (focus jumps Iconed -> Plain, never landing
 *      on the separator). ---- */
static void test_menu_separator_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    /* Watch the Plain row's inline return. */
    s_act_capture_id = nt_ui_menu_test_item_id(MENU_A, KEY_QUIT);
    s_act_hit = false;

    /* Down focuses Iconed, a second Down must SKIP the separator and land on Plain. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ENTER);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "Down must skip the separator (focus Iconed->Plain); Plain's inline return fires on Enter");
}

/* ---- Submenu marker cell: a parent row declares the marker cell (arrow sprite or ">" fallback); a leaf
 *      row never does. ---- */
static void test_menu_arrow_marker_on_parent_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    /* set an arrow ref so the IMAGE marker cell (which carries the fmix id) is declared on the parent */
    style.arrow = (nt_atlas_region_ref_t){.atlas = {.id = 1U}, .region = 0U};
    style.arrow_size = 12U;

    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    menu_im_frame25(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t parent_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(nt_ui_menu_test_item_id(MENU_A, KEY_OPEN))); /* Parent */
    const nt_ui_bbox_t leaf_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));    /* Iconed leaf */
    TEST_ASSERT_TRUE_MESSAGE(parent_arrow.found, "a parent row must declare the submenu marker cell");
    TEST_ASSERT_FALSE_MESSAGE(leaf_arrow.found, "a leaf row must NOT declare a submenu marker");
}

/* ---- Single root occluder: while the menu is open ONE full-viewport occluder lays out; a closed menu
 *      declares none (present-only). ---- */
static void test_menu_root_occluder_present_while_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
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
    nt_input_init();
    nt_input_feed(nt_test_mouse(px, py, 2U));
    nt_input_poll();
}

/* One frame: declare a fixed target panel (registered for hover via block_pointer), run open_trigger
 * bound to it (KEPT 1:1 — paradigm-agnostic), then declare the immediate menu. */
static void trigger_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, uint32_t target_id, float px, float py, bool long_pressed) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    if (nt_input_mouse_is_pressed(NT_BUTTON_RIGHT)) {
        p.buttons[NT_BUTTON_RIGHT].is_down = true;
        p.buttons[NT_BUTTON_RIGHT].is_pressed = true;
    }
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    /* Target panel at (200,150) 160x120 so the hover-test has a concrete bbox. */
    CLAY({.id = (Clay_ElementId){.id = TARGET_ID}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {200.0F, 150.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(120)}}}) {
    }
    nt_ui_block_pointer(s_fx.ctx, TARGET_ID, NULL);
    (void)nt_ui_menu_open_trigger(st, s_fx.ctx, target_id, long_pressed);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "New");
            nt_ui_menu_submenu_end(st);
        }
        (void)nt_ui_menu_item(st, KEY_EDIT, "Edit");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Unbound (target_id == 0): a right-click ANYWHERE arms the menu. ---- */
static void test_menu_open_trigger_unbound_arms_anywhere(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_right_press(20.0F, 20.0F); /* far from the target panel */
    trigger_frame(&st, &style, 0U, 20.0F, 20.0F, false);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "unbound trigger must arm on a right-click anywhere (and the arming press must not self-close)");
}

/* ---- Caller-supplied long-press arms the trigger (no right-click that frame). ---- */
static void test_menu_open_trigger_long_press_passed_in_arms(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_input_init(); /* no right-click this frame; arming comes solely from long_pressed */
    trigger_frame(&st, &style, 0U, 40.0F, 40.0F, true);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "caller-supplied long_pressed must arm the trigger");
    TEST_ASSERT_TRUE_MESSAGE(float_near(st.anchor_x, 40.0F, 0.5F) && float_near(st.anchor_y, 40.0F, 0.5F), "long-press arms the menu at the cursor");
}

/* ---- Bound (target_id != 0): a right-click OFF the target does NOT arm; ON the target DOES. ---- */
static void test_menu_open_trigger_bound_requires_target_hover(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};

    /* Settle one frame so the target panel is registered for next-frame hover arbitration. */
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

/* ---- Left at the root closes the menu. ---- */
static void test_menu_left_at_root_closes(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_LEFT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE(st.open);
}

/* ---- A closed frame resets the navigation: reopening shows only the root. ---- */
static void test_menu_closed_frame_resets_navigation(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_input_init();
    st.open = false;
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    st.open = true;
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "a reopened menu starts at the root");
}

/* Four frames of one custom row (key KEY_NEW) hosting a 60x20 inner button: frames 0-1 bake the bboxes
 * (1-frame IM lag), frame 2 presses and frame 3 releases at the inner button's center (on_child) or at
 * the row's right edge, beside the child. */
#define MENU_ROW_CHILD_ID 0x1B7701U
typedef struct {
    bool btn_clicked;
    bool end_activated;
    bool row_captured;
} menu_row_child_result_t;
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static menu_row_child_result_t menu_row_child_frames(nt_ui_menu_t *st, bool non_activatable, bool on_child) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    menu_row_child_result_t r = {0};
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, KEY_NEW);
    for (int frame = 0; frame < 4; ++frame) {
        const nt_ui_bbox_t child = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_CHILD_ID);
        const nt_ui_bbox_t row = nt_ui_get_bbox(s_fx.ctx, row_id);
        const bool found = child.found && row.found;
        nt_pointer_t p = {.active = true};
        if (found) {
            p.x = on_child ? (child.x + (child.width * 0.5F)) : (row.x + row.width - 2.0F);
            p.y = child.y + (child.height * 0.5F);
        }
        if (frame == 2 && found) {
            p.buttons[NT_BUTTON_LEFT].is_down = true;
            p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        } else if (frame == 3 && found) {
            p.buttons[NT_BUTTON_LEFT].is_released = true;
        }
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(st, KEY_NEW, (nt_ui_menu_item_opts_t){.non_activatable = non_activatable});
            CLAY({.id = (Clay_ElementId){.id = MENU_ROW_CHILD_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
            r.btn_clicked |= nt_ui_step(s_fx.ctx, MENU_ROW_CHILD_ID, NULL).clicked;
            r.end_activated |= nt_ui_menu_item_end(st);
            nt_ui_menu_end(st);
        }
        nt_ui_end(s_fx.ctx);
        r.row_captured |= (nt_ui_test_capture_active_id(s_fx.ctx, 0U) == row_id);
    }
    return r;
}

/* ---- An inner button owns its press in a custom-content row, with or without the row's own action:
 *      the row steps in item_end, after the child, but the child is declared later. The row is still
 *      the one under the pointer, so it takes hover focus. ---- */
static void test_menu_item_begin_child_owns_its_press(void) {
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_row_child_result_t r = menu_row_child_frames(&st, false, true);
    TEST_ASSERT_TRUE(r.btn_clicked);
    TEST_ASSERT_FALSE(r.end_activated);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus);

    r = menu_row_child_frames(&st, true, true);
    TEST_ASSERT_TRUE(r.btn_clicked);
    TEST_ASSERT_FALSE(r.end_activated);
    TEST_ASSERT_TRUE(st.open);
}

/* ---- Beside the child a non_activatable row takes hover focus but never captures, activates or
 *      closes (first, on a fresh registry); an activatable row activates and closes the chain. ---- */
static void test_menu_item_begin_press_beside_child(void) {
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_row_child_result_t r = menu_row_child_frames(&st, true, false);
    TEST_ASSERT_FALSE(r.btn_clicked);
    TEST_ASSERT_FALSE(r.end_activated);
    TEST_ASSERT_FALSE(r.row_captured);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus);

    r = menu_row_child_frames(&st, false, false);
    TEST_ASSERT_FALSE(r.btn_clicked);
    TEST_ASSERT_TRUE(r.end_activated);
    TEST_ASSERT_FALSE(st.open);
}

/* ---- A drag captured by one row's control does not hover another row: pressing row A's control and
 *      dragging onto row B's control leaves hover focus on A, as every query ignores a pointer captured
 *      elsewhere. ---- */
#define MENU_ROW_B_CHILD_ID 0x1B7702U
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_item_begin_captured_drag_keeps_focus(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    for (int frame = 0; frame < 5; ++frame) {
        const nt_ui_bbox_t ca = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_CHILD_ID);
        const nt_ui_bbox_t cb = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_B_CHILD_ID);
        const nt_ui_bbox_t c = (frame < 3) ? ca : cb; /* press on A's control, then drag onto B's */
        nt_pointer_t p = {.x = c.x + (c.width * 0.5F), .y = c.y + (c.height * 0.5F), .active = true};
        p.buttons[NT_BUTTON_LEFT].is_down = (frame >= 2);
        p.buttons[NT_BUTTON_LEFT].is_pressed = (frame == 2);
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(&st, KEY_NEW, (nt_ui_menu_item_opts_t){0});
            CLAY({.id = (Clay_ElementId){.id = MENU_ROW_CHILD_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) { (void)nt_ui_step(s_fx.ctx, MENU_ROW_CHILD_ID, NULL); }
            (void)nt_ui_menu_item_end(&st);
            nt_ui_menu_item_begin(&st, KEY_OPEN, (nt_ui_menu_item_opts_t){0});
            CLAY({.id = (Clay_ElementId){.id = MENU_ROW_B_CHILD_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) { (void)nt_ui_step(s_fx.ctx, MENU_ROW_B_CHILD_ID, NULL); }
            (void)nt_ui_menu_item_end(&st);
            nt_ui_menu_end(&st);
        }
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_EQUAL_UINT32(MENU_ROW_CHILD_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus);
}

/* ---- Disabled custom row never activates: item_begin(opts{.disabled=true}) + a click over the row must
 *      make item_end return false (mirror item_ex's enabled gate). The enabled + activatable case still
 *      activates on the same click. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool menu_item_begin_click_activates(nt_ui_menu_t *st, nt_ui_menu_style_t *style, bool enabled) {
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
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
            nt_ui_menu_item_begin(st, row_key, (nt_ui_menu_item_opts_t){.disabled = !enabled});
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {} /* game body */
            if (nt_ui_menu_item_end(st)) {
                end_activated = true;
            }
            nt_ui_menu_end(st);
        }
        nt_ui_end(s_fx.ctx);
    }
    return end_activated;
}
/* ---- Enter activates the focused custom row: item_end reports it like a click. ---- */
static void test_menu_item_begin_enter_activates(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    bool activated = false;
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ENTER};
    for (int frame = 0; frame < 2; ++frame) {
        menu_key(keys[frame]);
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &(nt_pointer_t){.active = true}, .pointer_count = 1});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(&st, KEY_NEW, (nt_ui_menu_item_opts_t){0});
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
            activated |= nt_ui_menu_item_end(&st);
            nt_ui_menu_end(&st);
        }
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_TRUE_MESSAGE(activated, "Enter on the focused custom row must make item_end return true");
    TEST_ASSERT_FALSE(st.open);
}

static void test_menu_item_begin_disabled_never_activates(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    {
        nt_ui_menu_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        const bool act = menu_item_begin_click_activates(&st, &style, false);
        TEST_ASSERT_FALSE_MESSAGE(act, "a disabled (opts.disabled=true) custom row must NOT activate via item_end");
    }
    {
        nt_ui_menu_t st = {0};
        menu_im_open(&st, 120.0F, 80.0F);
        const bool act = menu_item_begin_click_activates(&st, &style, true);
        TEST_ASSERT_TRUE_MESSAGE(act, "an enabled activatable custom row must still activate on a click");
    }
}

/* Drive a plain nt_ui_menu_item leaf and report whether its INLINE return fired on the activation frame.
 * Mirrors menu_item_begin_click_activates but for the item (not item_begin) path: frames 0-1 bake the row
 * bbox (1-frame IM lag), frame 2 presses over its center, frame 3 releases -> the item returns clicked. */
static bool menu_item_click_activates(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
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
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
            if (nt_ui_menu_item(st, row_key, "Leaf")) {
                activated = true;
            }
            nt_ui_menu_end(st);
        }
        nt_ui_end(s_fx.ctx);
    }
    return activated;
}

/* ---- The primary same-frame mouse idiom `if (menu_item()) act()` activates a plain leaf: the inline
 *      return goes true on the click frame AND the activation closes the chain (st.open -> false). ---- */
static void test_menu_item_click_activates_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    const bool act = menu_item_click_activates(&st, &style);
    TEST_ASSERT_TRUE_MESSAGE(act, "a plain menu_item leaf must return clicked on the mouse activation frame");
    TEST_ASSERT_FALSE_MESSAGE(st.open, "activating a leaf must close the chain (st.open -> false)");
}

/* ---- Duplicate sibling key: two nt_ui_menu_item calls sharing a key in ONE open level alias the same
 *      scope-stack id -> Clay's duplicate-id error asserts. ---- */
static void test_menu_duplicate_sibling_key_asserts(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &(nt_pointer_t){.active = true}, .pointer_count = 1});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            (void)nt_ui_menu_item(&st, KEY_NEW, "First");
            (void)nt_ui_menu_item(&st, KEY_NEW, "Dup"); /* same key in the same level -> aliased id */
            nt_ui_menu_end(&st);
        }
        nt_ui_end(s_fx.ctx);
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    TEST_ASSERT_TRUE_MESSAGE(tripped, "two sibling items sharing a key must trip the duplicate-id NT_ASSERT");
}

/* One frame: a section header (separator_text) sits between two leaves. Used to assert Down focus skips
 * the header, exactly like a plain separator. */
static void menu_im_frame_sep_text(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        (void)nt_ui_menu_item(st, KEY_NEW, "Top");
        nt_ui_menu_separator_text(st, "Section");
        act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT), nt_ui_menu_item(st, KEY_QUIT, "Bottom"));
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A separator_text section header is non-interactive: Down focus skips it (Top -> Bottom), exactly
 *      like a plain separator. ---- */
static void test_menu_separator_text_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    menu_im_frame_sep_text(&st, &style);
    /* Watch Bottom's inline return. */
    s_act_capture_id = nt_ui_menu_test_item_id(MENU_A, KEY_QUIT);
    s_act_hit = false;

    /* Down focuses Top, a second Down must SKIP the header and land on Bottom. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame_sep_text(&st, &style);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame_sep_text(&st, &style);
    menu_key(NT_KEY_ENTER);
    menu_im_frame_sep_text(&st, &style);
    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "Down must skip the section header (focus Top->Bottom); Bottom's inline return fires on Enter");
}

/* Drive a disabled item_ex row with a mouse click; report whether its inline return fired. Mirrors
 * menu_item_click_activates but on the item_ex path with opts.disabled=true. */
static bool menu_item_ex_disabled_click(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
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
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
            if (nt_ui_menu_item_ex(st, row_key, "Disabled", (nt_ui_menu_item_opts_t){.disabled = true})) {
                activated = true;
            }
            nt_ui_menu_end(st);
        }
        nt_ui_end(s_fx.ctx);
    }
    return activated;
}

/* ---- A disabled item_ex row never activates on a mouse click and the chain stays open (the disabled
 *      gate covers the item_ex path, not only item_begin). ---- */
static void test_menu_item_ex_disabled_no_activate(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    const bool act = menu_item_ex_disabled_click(&st, &style);
    TEST_ASSERT_FALSE_MESSAGE(act, "a disabled item_ex row must NOT activate on a click");
    TEST_ASSERT_TRUE_MESSAGE(st.open, "a disabled-row click must leave the chain open (no activation)");
}

/* One frame: a submenu parent declared via submenu_begin_ex (icon + caller-controlled enabled). The body
 * declares one child leaf when the parent opens. Used to assert the icon gutter cell exists and a disabled
 * parent never flies out. */
static void menu_im_frame_submenu_ex(nt_ui_menu_t *st, nt_ui_menu_style_t *style, bool enabled) {
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        nt_ui_menu_item_opts_t opts = {0};
        opts.icon = s_icon_ref;
        opts.disabled = !enabled;
        if (nt_ui_menu_submenu_begin_ex(st, KEY_OPEN, "Parent", opts)) {
            (void)nt_ui_menu_item(st, KEY_PROJECT, "Child");
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- submenu_begin_ex honors opts.icon (the parent declares an icon gutter cell) and opts.disabled=true
 *      (a disabled parent returns false / never flies out, even when the open chain points at it). ---- */
static void test_menu_submenu_begin_ex_icon_disabled(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    /* Enabled parent: two warm frames so the parent's icon gutter cell bakes. */
    menu_im_frame_submenu_ex(&st, &style, true);
    menu_im_frame_submenu_ex(&st, &style, true);
    const nt_ui_bbox_t icon = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_OPEN)));
    TEST_ASSERT_TRUE_MESSAGE(icon.found, "submenu_begin_ex must declare the parent's icon gutter cell");

    /* Point the open chain at the parent, then redeclare it DISABLED: it must not fly out (the child panel
     * never lays out) and its branch closes. */
    st.open_path[0] = nt_ui_menu_test_item_id(MENU_A, KEY_OPEN);
    menu_im_frame_submenu_ex(&st, &style, false);
    menu_im_frame_submenu_ex(&st, &style, false);
    const nt_ui_bbox_t child_panel = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U));
    TEST_ASSERT_FALSE_MESSAGE(child_panel.found, "a disabled submenu parent must NOT fly out (no child panel) even when open_path targets it");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[0], "a disabled open parent closes its branch");
}

/* ============================ rich-row shortcut + checkmark cells ============================ */

/* Immediate frame: one rich row carrying a shortcut + selected check (Save), one plain row (Plain). The
 * shortcut/check CELL bbox is asserted (the rendered glyph is user-visual-QA only). */
static void menu_im_frame_rich(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        nt_ui_menu_item_opts_t save = {.shortcut = "Ctrl+S", .selected = true};
        (void)nt_ui_menu_item_ex(st, KEY_NEW, "Save", save);
        (void)nt_ui_menu_item_ex(st, KEY_QUIT, "Plain", (nt_ui_menu_item_opts_t){0});
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A rich row with a shortcut declares the shortcut cell (own fmix id); a plain row does NOT. ---- */
static void test_menu_shortcut_cell_on_rich_row_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t sc = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_shortcut_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    const nt_ui_bbox_t plain_sc = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_shortcut_id(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT)));
    TEST_ASSERT_TRUE_MESSAGE(sc.found, "a row with opts.shortcut must declare the shortcut cell");
    TEST_ASSERT_FALSE_MESSAGE(plain_sc.found, "a row without a shortcut must NOT declare a shortcut cell");
    /* Position lock: the GROW spacer right-aligns the shortcut, so its cell sits in the row's RIGHT half
     * (right of the leading label region), not just present somewhere. The interactive row element uses the
     * row id (mix(scope,key)). */
    const nt_ui_bbox_t row = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_item_id(MENU_A, KEY_NEW));
    TEST_ASSERT_TRUE_MESSAGE(row.found, "the rich row must register an interactive row bbox");
    TEST_ASSERT_TRUE_MESSAGE(sc.x > row.x + (row.width * 0.5F), "the shortcut cell must be right-aligned (right half of the row), not at the label x");
}

/* ---- opts.selected declares the checkmark cell (own fmix id); an unselected row does NOT. ---- */
static void test_menu_check_cell_when_selected(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t ck = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    const nt_ui_bbox_t unchecked = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT)));
    TEST_ASSERT_TRUE_MESSAGE(ck.found, "a selected row must declare the checkmark cell");
    TEST_ASSERT_FALSE_MESSAGE(unchecked.found, "an unselected row must NOT declare a checkmark cell");
}

/* One menu frame: root = [File submenu (2 items), Edit leaf]. Cursor at (px,py). Used by the hover-open
 * corridor tests — File is the parent that flies out on hover; Edit is the sibling leaf that collapses it. */
static void menu_im_hover_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    const nt_pointer_t p = menu_moved(&(nt_pointer_t){.x = px, .y = py, .active = true});
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "New");
            (void)nt_ui_menu_item(st, KEY_PROJECT, "Project");
            nt_ui_menu_submenu_end(st);
        }
        (void)nt_ui_menu_item(st, KEY_EDIT, "Edit");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Hover-open: hovering a parent ROW flies its submenu out (no click), with 1-frame IM lag. ---- */
static void test_menu_hover_opens_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    const uint32_t file_row = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);

    /* F1: lay out the root so the File row bbox exists next frame. */
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[0], "nothing open before any hover");

    /* F2: hover the File parent row center -> records the hover; menu_end commits open_path (1-frame lag). */
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file_row, &fx, &fy);
    menu_im_hover_frame(&st, &style, fx, fy);

    /* F3: settle; open_path[0] now points at File, its submenu flies out. */
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file_row, st.open_path[0], "hovering the parent row must open its submenu");
    TEST_ASSERT_TRUE(st.open);
}

/* ---- Leave-close: with File's submenu open, hovering the sibling Edit LEAF collapses it once the
 *      mouse-aim corridor releases (the cursor is no longer aiming at the open child). ---- */
static void test_menu_hover_sibling_leaf_collapses_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);

    const uint32_t file_row = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t edit_row = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);

    /* Open File's submenu via hover (settle a couple frames). */
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file_row, &fx, &fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file_row, st.open_path[0], "File submenu open before the sibling hover");

    /* Hover the Edit sibling leaf far from the open child corridor for enough frames that the dwell timer
     * crosses AIM_FALLBACK and the corridor releases -> the open child collapses. */
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit_row, &ex, &ey);
    bool collapsed = false;
    for (int f = 0; f < 30 && !collapsed; ++f) {
        menu_im_hover_frame(&st, &style, ex, ey);
        collapsed = (st.open_path[0] == 0U);
    }
    TEST_ASSERT_TRUE_MESSAGE(collapsed, "hovering a sibling leaf (corridor released) must collapse the open submenu");
}

/* One menu frame with a single File submenu (two child items), driven by the keyboard nav path. */
static void menu_im_submenu_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "ChildItemLong");
            (void)nt_ui_menu_item(st, KEY_QUIT, "ChildItem2");
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Right-edge flip: a menu opened hard against the RIGHT screen edge must (a) clamp the ROOT
 *      panel back on-screen (BELOW only flips vertically, so a point-anchored root needs a horizontal
 *      clamp), and (b) flip its submenu LEFT so the whole stack stays visible. ---- */
static void test_menu_right_edge_root_clamp_and_submenu_flip(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 780.0F, 100.0F); /* open hard against the right edge (780 + min_width 160 > 800) */

    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F1: lay out root */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F2: focus File */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_submenu_frame(&st, &style, 0.0F, 0.0F); /* F3: open File submenu */
    nt_input_init();
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

/* ---- A pointer resting on a row hands focus and the chain to the keyboard: with File hover-opened and
 *      the pointer still on File, Down focuses New and Left closes File; moving the pointer, even within
 *      File, takes focus back and reopens File. ---- */
static void test_menu_resting_pointer_yields_to_keyboard(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    for (int f = 0; f < 3 && !menu_level_found(1U); ++f) {
        menu_im_hover_frame(&st, &style, fx, fy);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "hovering File opens it");

    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_hover_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_hover_frame(&st, &style, fx, fy);
    const uint32_t file_new = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_NEW);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file_new, st.focus, "Down focuses New while the pointer rests on File");

    menu_key(NT_KEY_ARROW_LEFT);
    menu_im_hover_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_hover_frame(&st, &style, fx, fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "Left closes File although the pointer rests on it");

    menu_im_hover_frame(&st, &style, fx + 2.0F, fy);
    menu_im_hover_frame(&st, &style, fx + 2.0F, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st.focus, "moving within File takes focus back to the pointer");
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "moving within File hands it the chain again: File reopens");
}

/* ---- A press inside a panel shown last frame is not an outside press, even when that level closed at the
 *      end of last frame. ---- */
static void test_menu_press_in_just_closed_panel_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_input_init();
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(menu_level_found(1U));
    menu_key(NT_KEY_ARROW_LEFT); /* closes File in this frame's menu_end; its panel was still shown */
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_input_init();
    float nx = 0.0F;
    float ny = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_QUIT), &nx, &ny);
    menu_im_press(&st, &style, nx, ny, NT_POINTER_MOUSE);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "a press on a panel shown last frame must not dismiss the menu");
}

/* ---- A frame activates one row: a tap on L1 (press and release in one frame) together with Enter on the
 *      focused L2 activates only L1, the first declared. ---- */
static void test_menu_one_activation_per_frame(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_im_open(&st, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    const nt_key_t focus_l2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, focus_l2, 2);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_L1), &cx, &cy);
    nt_pointer_t p = {.x = cx, .y = cy, .active = true};
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    menu_key(NT_KEY_ENTER);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    bool l1 = false;
    bool l2 = false;
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
        l1 = nt_ui_menu_item(&st, KEY_L1, "L1");
        l2 = nt_ui_menu_item(&st, KEY_L2, "L2");
        nt_ui_menu_end(&st);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(l1, "the tapped row activates");
    TEST_ASSERT_FALSE_MESSAGE(l2, "the focused row does not activate in the same frame");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_one_activation_per_frame);
    RUN_TEST(test_menu_resting_pointer_yields_to_keyboard);
    RUN_TEST(test_menu_press_in_just_closed_panel_keeps_open);
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
    RUN_TEST(test_menu_max_depth_constant);
    RUN_TEST(test_menu_smoke_open_and_closed);
    RUN_TEST(test_menu_kbd_nav_activates_nested_leaf);
    RUN_TEST(test_menu_nested_dismiss_esc_deepest_first);
    RUN_TEST(test_menu_switch_root_branch_while_submenu_open);
    RUN_TEST(test_menu_root_row_hittable_while_submenu_open);
    RUN_TEST(test_menu_click_root_parent_switches_branch);
    RUN_TEST(test_menu_hover_root_parent_switches_branch);
    RUN_TEST(test_menu_hover_collapse_reopen_forgets_descendants);
    RUN_TEST(test_menu_insert_before_open_parent_keeps_branch);
    RUN_TEST(test_menu_focus_follows_key_across_edits);
    RUN_TEST(test_menu_lost_open_parent_closes_branch);
    RUN_TEST(test_menu_kbd_open_focuses_first_enabled_row);
    RUN_TEST(test_menu_kbd_empty_submenu);
    RUN_TEST(test_menu_outside_click_dismisses_chain);
    RUN_TEST(test_menu_outside_tap_dismisses_chain);
    RUN_TEST(test_menu_right_click_outside_dismisses_chain);
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
    RUN_TEST(test_menu_item_id_distinct_siblings_and_scopes);
    RUN_TEST(test_menu_left_at_root_closes);
    RUN_TEST(test_menu_closed_frame_resets_navigation);
    RUN_TEST(test_menu_lost_focused_row);
    RUN_TEST(test_menu_up_down_wrap);
    RUN_TEST(test_menu_item_begin_child_owns_its_press);
    RUN_TEST(test_menu_item_begin_press_beside_child);
    RUN_TEST(test_menu_item_begin_captured_drag_keeps_focus);
    RUN_TEST(test_menu_item_begin_disabled_never_activates);
    RUN_TEST(test_menu_item_begin_enter_activates);
    RUN_TEST(test_menu_shortcut_cell_on_rich_row_only);
    RUN_TEST(test_menu_check_cell_when_selected);
    RUN_TEST(test_menu_item_click_activates_leaf);
    RUN_TEST(test_menu_duplicate_sibling_key_asserts);
    RUN_TEST(test_menu_separator_text_non_interactive);
    RUN_TEST(test_menu_item_ex_disabled_no_activate);
    RUN_TEST(test_menu_submenu_begin_ex_icon_disabled);
    return UNITY_END();
}
