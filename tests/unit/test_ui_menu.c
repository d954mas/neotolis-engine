/* Context-menu + recursive submenu tests against the IMMEDIATE begin/end API.
 * The pure tests cover the mouse-aim corridor (keep along a diagonal, switch after AIM_FALLBACK, the near
 * edge facing the apex); the full-UI tests drive nt_ui_menu_begin/item/submenu/end through the walker
 * fixture + NT_TEST_ACCESS probes (no GL surface).
 * UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input_feed.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_input_test_frame.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_menu.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_popup.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define MENU_A 0x4E5001U

/* Row keys, unique among siblings; the parent's row id scopes each level. */
#define KEY_FILE 1U
#define KEY_EDIT 2U
#define KEY_NEW 10U
#define KEY_OPEN 11U
#define KEY_QUIT 12U
#define KEY_PROJECT 20U
#define KEY_FILE2 21U
#define KEY_TOOLS 30U
#define KEY_OPT 31U

/* Inline-activation capture: the drivers OR a watched row's inline return into s_act_hit. */
static uint32_t s_act_capture_id; /* the fully-scoped row id a test wants to watch (0 = watch nothing) */
static bool s_act_hit;            /* set true the frame the watched row's inline return goes true */
static void act_capture(uint32_t row_scoped_id, bool clicked) {
    if (clicked && row_scoped_id == s_act_capture_id) {
        s_act_hit = true;
    }
}

/* Every frame begins here with the pointer's movement since the last frame, as nt_input publishes it (a
 * test's first frame has none): a pointer that moves takes focus back from the keyboard. */
static float s_ptr_x;
static float s_ptr_y;
static bool s_ptr_seen;
static bool s_row_popup_catcher; /* menu_row_popup_frame: the row's popup has a catcher */
static bool s_row_popup;         /* menu_row_popup_branch_frame: the custom row's popup is open */
static bool s_modal_over;        /* menu_im_hover_frame declares a modal after the menu */
static bool s_deep_popup;        /* menu_deep_popup_frame: the level-1 custom row's popup is open */
static void menu_frame_begin(const nt_pointer_t *p) {
    nt_pointer_t q = *p;
    q.dx = s_ptr_seen ? q.x - s_ptr_x : 0.0F;
    q.dy = s_ptr_seen ? q.y - s_ptr_y : 0.0F;
    s_ptr_x = q.x;
    s_ptr_y = q.y;
    s_ptr_seen = true;
    nt_arena_reset(&g_nt_mem_scratch);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = &q, .pointer_count = 1});
}

void setUp(void) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_ptr_seen = false;
    s_modal_over = false;
    s_row_popup = false;
    s_row_popup_catcher = false;
    s_deep_popup = false;
    s_act_capture_id = 0U;
    s_act_hit = false;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* ---- The corridor aims at the submenu edge facing the apex: the LEFT edge of a submenu opened to the
 *      right, the RIGHT edge of one edge-flipped to the left. ---- */
static void test_menu_aim_near_edge_faces_apex(void) {
    TEST_ASSERT_TRUE(float_near(nt_ui_menu_test_aim_near_x(200.0F, 150.0F, 190.0F), 200.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(nt_ui_menu_test_aim_near_x(200.0F, 150.0F, 360.0F), 350.0F, 0.01F));
}

/* ---- Aim KEEP: a diagonal from the parent toward the open submenu stays inside the fixed-apex triangle,
 *      so it keeps the submenu past AIM_FALLBACK, up to twice AIM_FALLBACK. ---- */
static void test_menu_aim_keeps_submenu_open_on_diagonal(void) {
    const float sub_x = 300.0F;
    const float sub_y = 100.0F;
    const float sub_w = 150.0F;
    const float sub_h = 300.0F;
    const float dt = 1.0F / 60.0F;

    nt_ui_menu_aim_t aim = {0};
    (void)nt_ui_menu_test_hover_intent(&aim, 290.0F, 110.0F, sub_x, sub_y, sub_w, sub_h, dt); /* primes the apex */
    for (int k = 1; k <= 10; ++k) {
        const float x = 290.0F + ((9.0F * (float)k) / 11.0F); /* toward (299, 300), short of the submenu */
        const float y = 110.0F + ((190.0F * (float)k) / 11.0F);
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_menu_test_hover_intent(&aim, x, y, sub_x, sub_y, sub_w, sub_h, dt), "the corridor keeps the submenu along the diagonal");
    }
    TEST_ASSERT_TRUE_MESSAGE(aim.timer > NT_UI_MENU_AIM_FALLBACK_SECS, "the corridor, not the fallback, kept it");
    int frames = 0;
    while (nt_ui_menu_test_hover_intent(&aim, 299.0F, 300.0F, sub_x, sub_y, sub_w, sub_h, dt) && frames < 30) {
        ++frames;
    }
    TEST_ASSERT_TRUE_MESSAGE(aim.timer >= NT_UI_MENU_AIM_FALLBACK_SECS * 2.0F && frames < 30, "a parked cursor in the corridor releases after twice AIM_FALLBACK");
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

/* ============================ immediate full menu UI ============================ */

/* Produce exactly ONE fresh pressed-key edge for this frame. */
static void menu_key(nt_key_t key) { nt_input_test_key_press(key); }

/* True when the menu level `depth` was laid out last frame. */
static bool menu_level_found(uint8_t depth) { return nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, depth)).found; }

/* One menu frame, tree: File > (New, Open > (Project, File2), Quit), Edit. */
static void menu_im_frame_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    menu_frame_begin(p);
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

/* ---- Keyboard-nav reaches a nested leaf: Down focuses File, Right opens its submenu, Down to Open,
 *      Enter opens the grandchild, Down to File2, Enter activates -> File2's INLINE return fires on the
 *      Enter frame, like a click, and the chain closes. ---- */
static void test_menu_kbd_nav_activates_nested_leaf(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F); /* the menu reads keys from the frame after it opens */

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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F); /* the menu reads keys from the frame after it opens */

    /* open root->File submenu */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    /* Esc #1: close the deepest level (the File submenu) and focus File — root stays open. */
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "first Esc closes only the deepest submenu, root stays open");
    TEST_ASSERT_EQUAL_UINT32(0U, st.open_path[0]);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st.focus);

    /* Esc #2: close the root chain. */
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "second Esc closes the root chain");
}

/* ---- Outside press: a click or a tap far outside every open panel closes the whole chain. ---- */
static void test_menu_outside_click_dismisses_chain(void) {
    const uint8_t types[] = {NT_POINTER_MOUSE, NT_POINTER_TOUCH};
    for (int i = 0; i < 2; ++i) {
        nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 700.0F, 500.0F);
        TEST_ASSERT_TRUE(st.open);
        menu_im_press(&st, &style, 780.0F, 580.0F, types[i]);
        TEST_ASSERT_FALSE_MESSAGE(st.open, "an outside press dismisses the whole chain");
    }
}

/* ---- Inside-click does NOT dismiss: a primary press INSIDE the root panel is a row interaction. ---- */
static void test_menu_inside_click_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the root panel bbox exists next frame. Anchor at (120,80) -> panel grows below. */
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(st.open);

    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t root = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 0U));
    TEST_ASSERT_TRUE_MESSAGE(float_near(root.x, 120.0F, 0.5F) && float_near(root.y, 80.0F, 0.5F), "the root's top-left sits at the open point");
    /* frame 3: press well inside the root panel body (just below the anchor) -> not a dismiss. */
    menu_im_press(&st, &style, 140.0F, 100.0F, NT_POINTER_MOUSE);

    TEST_ASSERT_TRUE_MESSAGE(st.open, "a click inside an open panel must NOT dismiss the chain");
}

/* ---- Right-click outside (a frame AFTER opening) dismisses the chain, mirroring the left-click dismiss. ---- */
static void test_menu_right_click_outside_dismisses_chain(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);

    /* frame 1: render so the panel bbox exists next frame (no press) */
    menu_im_frame(&st, &style, 700.0F, 500.0F);
    TEST_ASSERT_TRUE(st.open);

    /* frame 2: RIGHT press far from the panel -> menu-owned outside dismiss. The snapshot pointer carries
     * the right-button edge so the menu reads a consistent right-press this frame. */
    nt_pointer_t p = {.x = 780.0F, .y = 580.0F, .active = true};
    p.buttons[NT_BUTTON_RIGHT].is_down = true;
    p.buttons[NT_BUTTON_RIGHT].is_pressed = true;
    menu_im_frame_ptr(&st, &style, &p);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "right-click outside (post-open) dismisses the whole chain");
}

/* Immediate frame for a two-branch root: File > (New, Open > (Project, File2), Quit) and Tools > (Opt).
 * Used by the branch-switch tests (the user must reach the OTHER root branch while one is open). */
static void menu_im_frame2_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    menu_frame_begin(p);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame2(&st, &style, 0.0F, 0.0F, false); /* the menu reads keys from the frame after it opens */

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

typedef void (*menu_frame_fn)(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py);

static void menu_im_frame2_idle(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) { menu_im_frame2(st, style, px, py, false); }

/* Hovers `row_id` (a parent) until the level below it lays out. */
static void menu_hover_open(nt_ui_menu_t *st, nt_ui_menu_style_t *style, uint32_t row_id, uint8_t child, menu_frame_fn frame) {
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(row_id, &cx, &cy);
    for (int f = 0; f < 3 && !menu_level_found(child); ++f) {
        frame(st, style, cx, cy);
    }
    TEST_ASSERT_TRUE(menu_level_found(child));
}

/* Opens File > Open (depth 2) through the keyboard, one key per frame, with the pointer off the menu. */
static void menu_open_file_open(nt_ui_menu_t *st, nt_ui_menu_style_t *style, menu_frame_fn frame) {
    frame(st, style, 0.0F, 0.0F); /* the menu reads keys from the frame after it opens */
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    for (int i = 0; i < 4; ++i) {
        menu_key(keys[i]);
        frame(st, style, 0.0F, 0.0F);
    }
    nt_input_init();
    frame(st, style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(2U), "File > Open must be open");
}

/* Right (into the focused open parent) and Enter on the two-branch tree; true when `leaf` activated. */
static bool menu_right_enter_activates(nt_ui_menu_t *st, nt_ui_menu_style_t *style, uint32_t leaf) {
    s_act_capture_id = leaf;
    s_act_hit = false;
    menu_key(NT_KEY_ARROW_RIGHT);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[1], "switching the root branch closes File > Open");

    /* The click left focus on Tools: Right enters its submenu, Enter activates Opt. */
    const uint32_t opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    TEST_ASSERT_TRUE_MESSAGE(menu_right_enter_activates(&st, &style, opt), "Right + Enter must activate Tools > Opt");
}

/* ---- Hovering Tools while File > Open is open replaces the whole branch once the corridor releases. ---- */
static void test_menu_hover_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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
    TEST_ASSERT_TRUE_MESSAGE(menu_right_enter_activates(&st, &style, opt), "Right + Enter must activate Tools > Opt");
}

/* ---- Collapsing File by hovering the Edit leaf forgets File > Open: hovering File again opens only
 *      File's own submenu. ---- */
static void test_menu_hover_collapse_reopen_forgets_descendants(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_open_file_open(&st, &style, menu_im_frame);

    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    for (int f = 0; f < 30 && menu_level_found(1U); ++f) {
        menu_im_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "hovering the Edit leaf must collapse File");

    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);
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

static void menu_dyn_frame_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t, const nt_pointer_t *p) {
    menu_frame_begin(p);
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

static void menu_dyn_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t) { menu_dyn_frame_ptr(st, style, t, &(nt_pointer_t){.active = true}); }

/* One frame per key; the keys are released after the last. */
static void menu_dyn_keys(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t, const nt_key_t *keys, int count) {
    if (st->arming) {
        menu_dyn_frame(st, style, t); /* the menu reads keys from the frame after it opens */
    }
    for (int i = 0; i < count; ++i) {
        menu_key(keys[i]);
        menu_dyn_frame(st, style, t);
    }
    nt_input_init();
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        menu_dyn_keys(&st, &style, &cases[c].before, downs, cases[c].before.count);
        menu_dyn_frame(&st, &style, &cases[c].after);
        TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &cases[c].after, nt_ui_menu_test_item_id(MENU_A, cases[c].focus_key)), "Enter must activate the focused key after the edit");
    }
}

/* ---- A disabled focused row keeps its place: Enter does nothing and Down steps to the row after it. ---- */
static void test_menu_disabled_focused_row_keeps_its_place(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    const menu_dyn_tree_t full = {.keys = l123, .count = 3};
    const menu_dyn_tree_t lost = {.keys = l123, .count = 3, .disabled_key = KEY_L2};
    const nt_key_t focus_l2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_dyn_keys(&st, &style, &full, focus_l2, 2);
    menu_dyn_frame(&st, &style, &lost);
    TEST_ASSERT_FALSE_MESSAGE(menu_dyn_enter_activates(&st, &style, &lost, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "Enter on a disabled focused row does nothing");
    TEST_ASSERT_TRUE(st.open);
    const nt_key_t down = NT_KEY_ARROW_DOWN;
    menu_dyn_keys(&st, &style, &lost, &down, 1);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &lost, nt_ui_menu_test_item_id(MENU_A, KEY_L3)), "Down steps from the disabled focused row");
}

/* ---- Up from no focus lands on the last row; Down past the last row wraps to the first. ---- */
static void test_menu_up_down_wrap(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    const menu_dyn_tree_t t = {.keys = l123, .count = 3};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    const nt_key_t up = NT_KEY_ARROW_UP;
    menu_dyn_keys(&st, &style, &t, &up, 1);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L3)), "Up from no focus lands on the last row");
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, downs, 4);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Down past the last row wraps to the first");
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    const nt_key_t down_down_up[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_UP};
    menu_dyn_keys(&st, &style, &t, down_down_up, 3);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Up steps back from the focused row");
}

/* ---- Down skips a disabled row, and from no focus starts at the first ENABLED row. ---- */
static void test_menu_down_skips_disabled(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    const menu_dyn_tree_t mid_off = {.keys = l123, .count = 3, .disabled_key = KEY_L2};
    const menu_dyn_tree_t first_off = {.keys = l123, .count = 3, .disabled_key = KEY_L1};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &mid_off, downs, 2);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &mid_off, nt_ui_menu_test_item_id(MENU_A, KEY_L3)), "Down from L1 skips the disabled L2");
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_dyn_keys(&st, &style, &first_off, downs, 1);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &first_off, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "Down from no focus starts at the first enabled row");
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
        nt_ui_menu_open(&st, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t p1[] = {KEY_P1};
    const menu_dyn_tree_t t = {.keys = p1, .count = 1, .disabled_first_child = true};
    const nt_key_t open_p1[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    menu_dyn_keys(&st, &style, &t, open_p1, 2);
    const uint32_t p1_opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_P1), KEY_OPT);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, p1_opt), "Enter must activate the first enabled child");
}

/* ---- Right opens an empty submenu and focus stays on its parent; Down then moves on in the parent's
 *      level and closes the empty submenu. ---- */
static void test_menu_kbd_empty_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t keys[] = {KEY_P1, KEY_L1};
    const menu_dyn_tree_t t = {.keys = keys, .count = 2, .empty_children = true};
    const nt_key_t open_p1[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    menu_dyn_keys(&st, &style, &t, open_p1, 2);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "Right opens the empty submenu");
    const nt_key_t down = NT_KEY_ARROW_DOWN;
    menu_dyn_keys(&st, &style, &t, &down, 1);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[0], "Down closes the empty submenu");
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Down moves on in the parent's level");
}

/* ---- Depth cap: a self-referential immediate tree (a submenu_begin that recurses on itself) nests one
 *      level per keyboard open; a parent declared on the deepest level trips the depth NT_ASSERT before the
 *      popup stack is exhausted. ---- */
static void menu_im_self_ref(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    menu_frame_begin(&(nt_pointer_t){.active = true});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        int guard = 0;
        while (guard < NT_UI_MENU_MAX_DEPTH + 4 && nt_ui_menu_submenu_begin(st, KEY_FILE, "loop")) {
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_self_ref(&st, &style); /* the menu reads keys from the frame after it opens */
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
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_UI_MENU_MAX_DEPTH"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_MENU_MAX_DEPTH - 2, opened, "every allowed level opens before the cap trips");
}

/* ============================ icons / separator / occluder ============================ */

/* An icon ref carries a non-zero atlas.id so the gutter draws an image cell (no real atlas binds in the
 * fixture, so the resolve no-ops, but the gutter cell itself lays out + the label aligns). */
static const nt_atlas_region_ref_t s_icon_ref = {.atlas = {.id = 1U}, .region = NT_ATLAS_INVALID_REGION};

/* Immediate frame for the icon/separator vitrine: Iconed (item_ex with icon), a separator, Plain (no
 * icon), Parent (submenu marker). */
static void menu_im_frame_rows(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    menu_frame_begin(&p);
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
 *      so their labels start at the SAME x. ---- */
static void test_menu_icon_gutter_aligns_labels(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);

    menu_im_frame_rows(&st, &style, 0.0F, 0.0F); /* lay out so the gutter cell bbox exists next frame */
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    TEST_ASSERT_FALSE_MESSAGE(g0.found, "icon_size 0 must declare no gutter cell");
}

/* ---- Separator is non-interactive: keyboard Down skips it (focus jumps Iconed -> Plain, never landing
 *      on the separator). ---- */
static void test_menu_separator_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);

    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    /* Watch the Plain row's inline return. */
    s_act_capture_id = nt_ui_menu_test_item_id(MENU_A, KEY_QUIT);
    s_act_hit = false;

    /* Down focuses Iconed, a second Down must SKIP the separator and land on Plain. */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ENTER);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "Down must skip the separator (focus Iconed->Plain); Plain's inline return fires on Enter");
}

/* ---- Submenu marker cell: a parent row declares the marker cell (arrow sprite or ">" fallback); a leaf
 *      row never does. ---- */
static void test_menu_arrow_marker_on_parent_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);

    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t parent_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(nt_ui_menu_test_item_id(MENU_A, KEY_OPEN))); /* Parent */
    const nt_ui_bbox_t leaf_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));    /* Iconed leaf */
    TEST_ASSERT_TRUE_MESSAGE(parent_arrow.found, "a parent row must declare the submenu marker cell");
    TEST_ASSERT_FALSE_MESSAGE(leaf_arrow.found, "a leaf row must NOT declare a submenu marker");
}

#define MODAL_ID 0x40DA1U

/* One frame: a modal that listens for Esc, then the menu declared over it; reports the modal's close. */
static bool menu_over_modal_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.flags |= NT_UI_MODAL_LISTEN_ESC;
    menu_frame_begin(&(nt_pointer_t){.active = true});
    const bool modal_close = nt_ui_modal_begin(s_fx.ctx, MODAL_ID, &ms, true).close_requested;
    nt_ui_modal_end(s_fx.ctx);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        (void)nt_ui_menu_item(st, KEY_NEW, "New");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
    return modal_close;
}

/* ---- An open menu is a modal overlay: nt_ui_overlay_active reports it, and Esc over a modal closes only
 *      the menu. ---- */
static void test_menu_is_modal_overlay(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_overlay_active(s_fx.ctx), "an open menu counts as an overlay");
    st.open = false;
    menu_im_frame_rows(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE(nt_ui_overlay_active(s_fx.ctx));

    nt_ui_menu_open(&st, 120.0F, 80.0F);
    TEST_ASSERT_FALSE(menu_over_modal_frame(&st, &style));
    menu_key(NT_KEY_ESCAPE);
    const bool modal_close = menu_over_modal_frame(&st, &style);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "Esc closes the menu");
    TEST_ASSERT_FALSE_MESSAGE(modal_close, "Esc over a modal closes only the menu on top of it");
}

/* ---- nt_ui_pointer_pos is the primary pointer in UI space. ---- */
static void test_menu_pointer_pos(void) {
    menu_frame_begin(&(nt_pointer_t){.x = 37.0F, .y = 41.0F, .active = true});
    float x = 0.0F;
    float y = 0.0F;
    nt_ui_pointer_pos(s_fx.ctx, &x, &y);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(float_near(x, 37.0F, 0.01F) && float_near(y, 41.0F, 0.01F));
}

/* ============================ immediate-mode full menu ============================ */

/* ---- Row ids: sibling keys at one scope and the SAME key under a parent row's scope all derive
 *      distinct, non-zero ids. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat distinctness asserts, not real nesting
static void test_menu_item_id_distinct_siblings_and_scopes(void) {
    /* root scope = the menu id; two sibling keys at idx 0/1 */
    const uint32_t a = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t b = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    /* the SAME key reused under the File row's scope */
    const uint32_t sub_scope = nt_ui_menu_test_item_id(MENU_A, KEY_FILE); /* the File submenu's own id == its row id */
    const uint32_t c = nt_ui_menu_test_item_id(sub_scope, KEY_FILE);      /* reuse KEY_FILE one level deeper */

    TEST_ASSERT_TRUE_MESSAGE(a != 0U && b != 0U && c != 0U, "no derived item id may be 0 (the no-widget sentinel)");
    TEST_ASSERT_TRUE_MESSAGE(a != b, "sibling keys at one scope must derive distinct ids");
    TEST_ASSERT_TRUE_MESSAGE(a != c, "the same key under a parent row's scope must NOT alias the root id");
    TEST_ASSERT_TRUE_MESSAGE(b != c, "cross-scope ids must stay distinct");
}

/* ---- Left at the root closes the menu. ---- */
static void test_menu_left_at_root_closes(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_LEFT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE(st.open);
}

/* ---- Reopening starts at the root: nt_ui_menu_open forgets the last chain. ---- */
static void test_menu_reopen_starts_at_root(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F); /* the menu reads keys from the frame after it opens */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_input_init();
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(menu_level_found(1U));
    st.open = false;
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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
        menu_frame_begin(&p);
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(st, KEY_NEW, true, !non_activatable);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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

/* ---- Beside the child a not-activatable row takes hover focus but never captures, activates or
 *      closes (first, on a fresh registry); an activatable row activates and closes the chain. ---- */
static void test_menu_item_begin_press_beside_child(void) {
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    for (int frame = 0; frame < 5; ++frame) {
        const nt_ui_bbox_t ca = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_CHILD_ID);
        const nt_ui_bbox_t cb = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_B_CHILD_ID);
        const nt_ui_bbox_t c = (frame < 3) ? ca : cb; /* press on A's control, then drag onto B's */
        nt_pointer_t p = {.x = c.x + (c.width * 0.5F), .y = c.y + (c.height * 0.5F), .active = true};
        p.buttons[NT_BUTTON_LEFT].is_down = (frame >= 2);
        p.buttons[NT_BUTTON_LEFT].is_pressed = (frame == 2);
        menu_frame_begin(&p);
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(&st, KEY_NEW, true, true);
            CLAY({.id = (Clay_ElementId){.id = MENU_ROW_CHILD_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) { (void)nt_ui_step(s_fx.ctx, MENU_ROW_CHILD_ID, NULL); }
            (void)nt_ui_menu_item_end(&st);
            nt_ui_menu_item_begin(&st, KEY_OPEN, true, true);
            CLAY({.id = (Clay_ElementId){.id = MENU_ROW_B_CHILD_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) { (void)nt_ui_step(s_fx.ctx, MENU_ROW_B_CHILD_ID, NULL); }
            (void)nt_ui_menu_item_end(&st);
            nt_ui_menu_end(&st);
        }
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_EQUAL_UINT32(MENU_ROW_CHILD_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus);
}

/* ---- Enter activates the focused custom row: item_end reports it like a click. ---- */
static void test_menu_item_begin_enter_activates(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    bool activated = false;
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ENTER};
    for (int frame = 0; frame < 3; ++frame) {
        if (frame > 0) {
            menu_key(keys[frame - 1]); /* the menu reads keys from the frame after it opens */
        }
        menu_frame_begin(&(nt_pointer_t){.active = true});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(&st, KEY_NEW, true, true);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
            activated |= nt_ui_menu_item_end(&st);
            nt_ui_menu_end(&st);
        }
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_TRUE_MESSAGE(activated, "Enter on the focused custom row must make item_end return true");
    TEST_ASSERT_FALSE(st.open);
}

/* The clicked row KEY_NEW, declared as a plain or disabled item or as an enabled or disabled custom row;
 * returns the row's activation. */
typedef enum { MENU_ROW_ITEM, MENU_ROW_ITEM_DISABLED, MENU_ROW_CUSTOM, MENU_ROW_CUSTOM_DISABLED } menu_row_kind_t;
static bool menu_declare_row(nt_ui_menu_t *st, menu_row_kind_t kind) {
    if (kind == MENU_ROW_ITEM || kind == MENU_ROW_ITEM_DISABLED) {
        return nt_ui_menu_item_ex(st, KEY_NEW, "Leaf", (nt_ui_menu_item_opts_t){.disabled = (kind == MENU_ROW_ITEM_DISABLED)});
    }
    nt_ui_menu_item_begin(st, KEY_NEW, kind == MENU_ROW_CUSTOM, true);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {} /* game body */
    return nt_ui_menu_item_end(st);
}

/* Frames 0-1 bake the row bbox (1-frame IM lag), frame 2 presses over its center, frame 3 releases. */
static bool menu_click_row(nt_ui_menu_t *st, nt_ui_menu_style_t *style, menu_row_kind_t kind) {
    const uint32_t row_id = nt_ui_menu_test_item_id(MENU_A, KEY_NEW);
    bool activated = false;
    for (int frame = 0; frame < 4; ++frame) {
        float bx = 0.0F;
        float by = 0.0F;
        menu_row_center(row_id, &bx, &by);
        nt_pointer_t p = {.x = bx, .y = by, .active = true};
        p.buttons[NT_BUTTON_LEFT].is_down = (frame == 2);
        p.buttons[NT_BUTTON_LEFT].is_pressed = (frame == 2);
        p.buttons[NT_BUTTON_LEFT].is_released = (frame == 3);
        menu_frame_begin(&p);
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
            activated |= menu_declare_row(st, kind);
            nt_ui_menu_end(st);
        }
        nt_ui_end(s_fx.ctx);
    }
    return activated;
}

/* ---- A click activates an enabled plain or custom row and closes the menu; a disabled one does neither. ---- */
static void test_menu_click_activates_enabled_rows_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const menu_row_kind_t kinds[] = {MENU_ROW_ITEM, MENU_ROW_ITEM_DISABLED, MENU_ROW_CUSTOM, MENU_ROW_CUSTOM_DISABLED};
    for (int k = 0; k < 4; ++k) {
        const bool enabled = (kinds[k] == MENU_ROW_ITEM || kinds[k] == MENU_ROW_CUSTOM);
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        TEST_ASSERT_EQUAL_MESSAGE(enabled, menu_click_row(&st, &style, kinds[k]), "a click activates exactly the enabled rows");
        TEST_ASSERT_EQUAL_MESSAGE(!enabled, st.open, "an activation closes the menu");
    }
}

/* ---- Enter on a focused custom row that is not activatable does nothing. ---- */
static void test_menu_item_begin_not_activatable_ignores_enter(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    bool activated = false;
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ENTER};
    for (int frame = 0; frame < 3; ++frame) {
        if (frame > 0) {
            menu_key(keys[frame - 1]); /* the menu reads keys from the frame after it opens */
        }
        menu_frame_begin(&(nt_pointer_t){.active = true});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            nt_ui_menu_item_begin(&st, KEY_NEW, true, false);
            activated |= nt_ui_menu_item_end(&st);
            nt_ui_menu_end(&st);
        }
        nt_ui_end(s_fx.ctx);
        if (frame > 0) {
            TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus); /* Enter lands on the row */
        }
    }
    TEST_ASSERT_FALSE(activated);
    TEST_ASSERT_TRUE(st.open);
}

typedef void (*menu_body_fn)(nt_ui_menu_t *st);

/* Declares `body` in an open menu's frame; true when it trips an NT_ASSERT whose expression holds `msg`. */
static bool menu_body_asserts(menu_body_fn body, const char *msg) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        menu_frame_begin(&(nt_pointer_t){.active = true});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            body(&st);
        }
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    return tripped && strstr(nt_test_assert_last_expr, msg) != NULL;
}

static void menu_body_row_in_custom_row(nt_ui_menu_t *st) {
    nt_ui_menu_item_begin(st, KEY_NEW, true, true);
    (void)nt_ui_menu_item(st, KEY_QUIT, "inside");
}
static void menu_body_separator_in_custom_row(nt_ui_menu_t *st) {
    nt_ui_menu_item_begin(st, KEY_NEW, true, true);
    nt_ui_menu_separator(st);
}
static void menu_body_checked_parent(nt_ui_menu_t *st) { (void)nt_ui_menu_submenu_begin_ex(st, KEY_OPEN, "Parent", (nt_ui_menu_item_opts_t){.selected = true}); }

/* ---- A row or a separator inside an open custom row, and a checked parent row, assert (one per test: a
 *      trip leaves the UI frame open). ---- */
static void test_menu_row_in_custom_row_asserts(void) { TEST_ASSERT_TRUE(menu_body_asserts(menu_body_row_in_custom_row, "inside a custom row")); }
static void test_menu_separator_in_custom_row_asserts(void) { TEST_ASSERT_TRUE(menu_body_asserts(menu_body_separator_in_custom_row, "outside a custom row")); }
static void test_menu_checked_parent_asserts(void) { TEST_ASSERT_TRUE(menu_body_asserts(menu_body_checked_parent, "no checkmark")); }

/* ---- Duplicate sibling key: two nt_ui_menu_item calls sharing a key in ONE open level share a row id ->
 *      Clay's duplicate-id error asserts. ---- */
static void test_menu_duplicate_sibling_key_asserts(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);

    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        menu_frame_begin(&(nt_pointer_t){.active = true});
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
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "Clay"));
}

/* One frame: a section header (separator_text) sits between two leaves. Used to assert Down focus skips
 * the header, exactly like a plain separator. */
static void menu_im_frame_sep_text(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    nt_pointer_t p = {.active = true};
    menu_frame_begin(&p);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);

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

/* One frame: a submenu parent declared via submenu_begin_ex with an icon; its body declares one leaf. */
static void menu_im_frame_submenu_ex(nt_ui_menu_t *st, nt_ui_menu_style_t *style, bool disabled, const nt_pointer_t *p) {
    menu_frame_begin(p);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        const nt_ui_menu_item_opts_t opts = {.icon = s_icon_ref, .disabled = disabled};
        if (nt_ui_menu_submenu_begin_ex(st, KEY_OPEN, "Parent", opts)) {
            (void)nt_ui_menu_item(st, KEY_PROJECT, "Child");
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- submenu_begin_ex honors opts: the parent declares its icon gutter cell, and a disabled parent opens
 *      neither by hover nor by click. ---- */
static void test_menu_submenu_begin_ex_icon_disabled(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    const nt_pointer_t idle = {.active = true};
    menu_im_frame_submenu_ex(&st, &style, true, &idle);
    menu_im_frame_submenu_ex(&st, &style, true, &idle);
    const uint32_t parent = nt_ui_menu_test_item_id(MENU_A, KEY_OPEN);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(parent)).found, "submenu_begin_ex must declare the parent's icon gutter cell");

    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(parent, &cx, &cy);
    nt_pointer_t p = {.x = cx, .y = cy, .active = true};
    menu_im_frame_submenu_ex(&st, &style, true, &p);
    menu_im_frame_submenu_ex(&st, &style, true, &p);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    menu_im_frame_submenu_ex(&st, &style, true, &p);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    menu_im_frame_submenu_ex(&st, &style, true, &p);
    menu_im_frame_submenu_ex(&st, &style, true, &idle);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_EQUAL_UINT32(0U, st.focus);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "a disabled parent opens neither by hover nor by click");
}

/* ============================ rich-row shortcut + checkmark cells ============================ */

/* Immediate frame: one rich row carrying a shortcut + selected check (Save), one plain row (Plain). The
 * shortcut/check CELL bbox is asserted (the rendered glyph is user-visual-QA only). */
static void menu_im_frame_rich(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {0};
    p.x = px;
    p.y = py;
    p.active = true;
    menu_frame_begin(&p);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    menu_im_frame_rich(&st, &style, 0.0F, 0.0F);
    const nt_ui_bbox_t ck = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    const nt_ui_bbox_t unchecked = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT)));
    TEST_ASSERT_TRUE_MESSAGE(ck.found, "a selected row must declare the checkmark cell");
    TEST_ASSERT_FALSE_MESSAGE(unchecked.found, "an unselected row must NOT declare a checkmark cell");
}

/* One menu frame: root = [File submenu (2 items), Edit leaf]. Cursor at (px,py). Used by the hover-open
 * corridor tests — File is the parent that flies out on hover; Edit is the sibling leaf that collapses it. */
static void menu_im_hover_frame_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    menu_frame_begin(p);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "New");
            (void)nt_ui_menu_item(st, KEY_PROJECT, "Project");
            nt_ui_menu_submenu_end(st);
        }
        (void)nt_ui_menu_item(st, KEY_EDIT, "Edit");
        nt_ui_menu_end(st);
    }
    if (s_modal_over) {
        const nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
        (void)nt_ui_modal_begin(s_fx.ctx, MODAL_ID, &ms, true);
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

static void menu_im_hover_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    const nt_pointer_t p = {.x = px, .y = py, .active = true};
    menu_im_hover_frame_ptr(st, style, &p);
}

/* ---- The mouse-aim corridor keeps File's submenu open while the pointer crosses the Edit leaf, then
 *      collapses it after the dwell; back on File, leaving again starts a fresh corridor. ---- */
static void test_menu_hover_corridor_keeps_then_releases(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    menu_im_hover_frame(&st, &style, fx, fy);
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_TRUE(menu_level_found(1U));

    for (int f = 0; f < 12; ++f) {
        menu_im_hover_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the corridor keeps File's submenu while the pointer is on Edit");
    menu_im_hover_frame(&st, &style, fx, fy);
    for (int f = 0; f < 6; ++f) { /* the 12-frame-old corridor, kept stale, would release on the 5th */
        menu_im_hover_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "leaving File again starts a fresh corridor");
    for (int f = 0; f < 30 && menu_level_found(1U); ++f) {
        menu_im_hover_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "the dwell on Edit collapses File's submenu");
}

/* One menu frame with a single File submenu (two child items), driven by the keyboard nav path. */
static void menu_im_submenu_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    nt_pointer_t p = {.x = px, .y = py, .active = true};
    menu_frame_begin(&p);
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
    nt_ui_menu_open(&st, 780.0F, 100.0F); /* open hard against the right edge (780 + min_width 160 > 800) */

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
 *      the pointer still on File, Right enters File's submenu (New) and Left closes it; moving the pointer,
 *      even within File, takes focus back and reopens File. ---- */
static void test_menu_resting_pointer_yields_to_keyboard(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_hover_frame);

    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_hover_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_hover_frame(&st, &style, fx, fy);
    const uint32_t file_new = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_NEW);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file_new, st.focus, "Right enters File's submenu while the pointer rests on File");

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

/* ---- After hovering File open, keys act on File's level: Down moves to Edit and closes File's submenu. ---- */
static void test_menu_keys_act_on_the_focused_level(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_hover_frame);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_hover_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_hover_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), st.focus, "Down moves to the next root row");
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "focus leaving File closes its submenu");
}

/* ---- A press inside a panel shown last frame is not an outside press, even when that level closed at the
 *      end of last frame: a click on Tools closes File's level after it declared. ---- */
static void test_menu_press_in_just_closed_panel_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_open_file_open(&st, &style, menu_im_frame2_idle);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &cx, &cy);
    menu_im_frame2(&st, &style, cx, cy, true);
    nt_pointer_t release = {.x = cx, .y = cy, .active = true};
    release.buttons[NT_BUTTON_LEFT].is_released = true;
    menu_im_frame2_ptr(&st, &style, &release);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), st.open_path[0]);
    float nx = 0.0F;
    float ny = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_QUIT), &nx, &ny);
    menu_im_frame2(&st, &style, nx, ny, true);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "a press on a panel shown last frame must not dismiss the menu");
}

/* ---- A frame activates one row, the first declared: a tap on one row (press and release in one frame)
 *      together with Enter on the other focused row activates only the row declared first. ---- */
static void menu_tap_and_enter(int focus_downs, uint32_t tap_key, bool *l1, bool *l2) {
    *l1 = false;
    *l2 = false;
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, downs, focus_downs);
    menu_dyn_frame(&st, &style, &t);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, tap_key), &cx, &cy);
    nt_pointer_t p = {.x = cx, .y = cy, .active = true};
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    menu_key(NT_KEY_ENTER);
    menu_frame_begin(&p);
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
        *l1 = nt_ui_menu_item(&st, KEY_L1, "L1");
        *l2 = nt_ui_menu_item(&st, KEY_L2, "L2");
        nt_ui_menu_end(&st);
    }
    nt_ui_end(s_fx.ctx);
}
static void test_menu_one_activation_per_frame(void) {
    bool l1 = false;
    bool l2 = false;
    menu_tap_and_enter(2, KEY_L1, &l1, &l2); /* L2 focused, L1 tapped */
    TEST_ASSERT_TRUE_MESSAGE(l1 && !l2, "the tapped L1, declared first, activates alone");
    menu_tap_and_enter(1, KEY_L2, &l1, &l2); /* L1 focused, L2 tapped */
    TEST_ASSERT_TRUE_MESSAGE(l1 && !l2, "Enter on L1, declared first, activates alone");
}

/* Root: File > (New, Open > (Off disabled, Project)) and three long leaves, so a level two deep opened at
 * the right screen edge lies over the root's rows. */
static void menu_im_overlap_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    menu_frame_begin(&(nt_pointer_t){.x = px, .y = py, .active = true});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            (void)nt_ui_menu_item(st, KEY_NEW, "New");
            if (nt_ui_menu_submenu_begin(st, KEY_OPEN, "Open")) {
                (void)nt_ui_menu_item_ex(st, KEY_L0, "Off", (nt_ui_menu_item_opts_t){.disabled = true});
                (void)nt_ui_menu_item(st, KEY_PROJECT, "Project");
                nt_ui_menu_submenu_end(st);
            }
            nt_ui_menu_submenu_end(st);
        }
        for (uint32_t k = KEY_L1; k <= KEY_L3; ++k) {
            (void)nt_ui_menu_item(st, k, "A long root row label");
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A panel blocks the pointer: a disabled row of a level opened over the root's rows hovers nothing,
 *      so focus stays where the keyboard left it. ---- */
static void test_menu_panel_blocks_pointer_under_it(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 780.0F, 100.0F);
    menu_im_overlap_frame(&st, &style, 0.0F, 0.0F);
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    for (int i = 0; i < 4; ++i) {
        menu_key(keys[i]);
        menu_im_overlap_frame(&st, &style, 0.0F, 0.0F);
    }
    nt_input_init();
    menu_im_overlap_frame(&st, &style, 0.0F, 0.0F);
    menu_im_overlap_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t open_scope = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_OPEN);
    const uint32_t project = nt_ui_menu_test_item_id(open_scope, KEY_PROJECT);
    TEST_ASSERT_EQUAL_UINT32(project, st.focus);
    float ox = 0.0F;
    float oy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(open_scope, KEY_L0), &ox, &oy);
    const nt_ui_bbox_t root = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 0U));
    bool over_root_row = false;
    for (uint32_t k = KEY_L1; k <= KEY_L3; ++k) {
        over_root_row = over_root_row || nt_ui_test_hit(s_fx.ctx, nt_ui_menu_test_item_id(MENU_A, k), ox, oy);
    }
    TEST_ASSERT_TRUE_MESSAGE(root.found && over_root_row, "the disabled row must lie over a root row");
    menu_im_overlap_frame(&st, &style, ox, oy); /* the pointer moves: keyboard mode ends */
    menu_im_overlap_frame(&st, &style, ox, oy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(project, st.focus, "a disabled row over another level's row hovers nothing");
    TEST_ASSERT_TRUE(menu_level_found(2U));
}

/* ---- Right on an open parent moves focus to its first row and closes a grandchild opened beside it. ---- */
static void test_menu_right_into_open_parent_closes_grandchild(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    menu_hover_open(&st, &style, file, 1U, menu_im_frame);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(file, KEY_OPEN), 2U, menu_im_frame);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file, &fx, &fy);
    menu_im_frame(&st, &style, fx, fy);
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32(file, st.focus);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(2U), "back on File, File > Open stays open");
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(file, KEY_NEW), st.focus);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(2U), "focus on New, beside the open Open, closes File > Open");
}

/* Hovers File open, holds it over the corridor from Tools (focus on Tools), then presses `keys` in one
 * frame on the two-branch tree. */
static void menu_keys_beside_open_sibling(nt_ui_menu_t *st, const nt_key_t *keys, int count) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_open(st, 120.0F, 80.0F);
    menu_im_frame2_idle(st, &style, 0.0F, 0.0F);
    menu_hover_open(st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame2_idle);
    float tx = 0.0F;
    float ty = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &tx, &ty);
    menu_im_frame2_idle(st, &style, tx, ty);
    menu_im_frame2_idle(st, &style, tx, ty);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st->open_path[0]);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), st->focus);
    nt_input_init();
    for (int i = 0; i < count; ++i) {
        nt_input_feed(nt_test_key(keys[i], true));
    }
    nt_input_poll(); /* every key fresh in one frame */
    menu_im_frame2_idle(st, &style, tx, ty);
    nt_input_init();
}

/* ---- Right next to a sibling's open child opens the focused parent in the same frame and focuses its first
 *      row: the key closes the pending switch before the rows declare. A frame acts on one key, so a Down in
 *      the same frame does nothing. ---- */
static void test_menu_right_beside_open_sibling_opens_at_once(void) {
    const uint32_t tools = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS);
    const uint32_t opt = nt_ui_menu_test_item_id(tools, KEY_OPT);
    const nt_key_t right = NT_KEY_ARROW_RIGHT;
    nt_ui_menu_t st = {0};
    menu_keys_beside_open_sibling(&st, &right, 1);
    TEST_ASSERT_EQUAL_UINT32(tools, st.open_path[0]);
    TEST_ASSERT_EQUAL_UINT32(opt, st.focus);

    const nt_key_t right_down[] = {NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN};
    nt_ui_menu_t st2 = {0};
    menu_keys_beside_open_sibling(&st2, right_down, 2);
    TEST_ASSERT_EQUAL_UINT32(tools, st2.open_path[0]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(opt, st2.focus, "one key per frame: the Down does not step past the first row");
}

/* ---- Reopening elsewhere ignores last frame's hover at the old place. ---- */
static void test_menu_reopen_ignores_last_frame_hits(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    menu_hover_open(&st, &style, file, 1U, menu_im_frame);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file, &fx, &fy);
    nt_ui_menu_open(&st, 400.0F, 300.0F);
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.focus, "the opening frame takes no focus from the old place");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[0], "the opening frame opens nothing from the old place");
}

/* ---- The switch the pointer leaves on a sibling runs off the rows too: after Edit takes focus beside File's
 *      open submenu, the pointer leaving the rows, or the window, still collapses it. ---- */
static void test_menu_pending_switch_lands_off_the_rows(void) {
    for (int c = 0; c < 2; ++c) {
        nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
        menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_hover_frame);
        const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
        float ex = 0.0F;
        float ey = 0.0F;
        menu_row_center(edit, &ex, &ey);
        menu_im_hover_frame(&st, &style, ex, ey);
        menu_im_hover_frame(&st, &style, ex, ey);
        TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
        TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the corridor holds File's submenu");
        /* Off the rows and off the corridor, or an inactive pointer left inside the corridor. */
        const nt_pointer_t away = (c == 0) ? (nt_pointer_t){.x = 20.0F, .y = 580.0F, .active = true} : (nt_pointer_t){.x = ex + 1.0F, .y = ey + 1.0F, .active = false};
        int frames = 0;
        while (menu_level_found(1U) && frames < 30) {
            menu_im_hover_frame_ptr(&st, &style, &away);
            ++frames;
        }
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), c == 0 ? "off the rows the switch still lands" : "outside the window the switch still lands");
        TEST_ASSERT_TRUE_MESSAGE(frames <= 9, "the switch lands after AIM_FALLBACK, not the corridor's 2x");
        TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    }
}

/* The alpha of a row's highlight fill last frame (0 = unlit). */
static float menu_row_lit(uint32_t row_id) {
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->id == row_id && c->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            return c->renderData.rectangle.backgroundColor.a;
        }
    }
    return 0.0F;
}

/* ---- One highlighted row per level: the focused row on its level, the open parent on every other level. ---- */
static void test_menu_one_highlight_per_level(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.state_speed = 0.0F; /* highlights snap */
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    const uint32_t open = nt_ui_menu_test_item_id(file, KEY_OPEN);
    menu_hover_open(&st, &style, file, 1U, menu_im_frame);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(menu_row_lit(file) > 0.0F && menu_row_lit(edit) == 0.0F);

    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_frame(&st, &style, ex, ey);
    menu_im_frame(&st, &style, ex, ey);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the corridor holds File's submenu");
    TEST_ASSERT_TRUE_MESSAGE(menu_row_lit(edit) > 0.0F && menu_row_lit(file) == 0.0F, "the pointer's row alone is lit on its level");

    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_hover_open(&st, &style, file, 1U, menu_im_frame);
    menu_hover_open(&st, &style, open, 2U, menu_im_frame);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file, &fx, &fy);
    menu_im_frame(&st, &style, fx, fy);
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_TRUE(menu_level_found(2U));
    TEST_ASSERT_TRUE_MESSAGE(menu_row_lit(file) > 0.0F && menu_row_lit(open) > 0.0F, "back on File, Open stays lit as its level's open parent");
}

/* ---- A row shown again snaps its highlight: a child row lit before its level closed does not flash when
 *      the level opens again. ---- */
static void test_menu_reshown_row_does_not_flash(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t file_new = nt_ui_menu_test_item_id(file, KEY_NEW);
    menu_hover_open(&st, &style, file, 1U, menu_im_frame);
    float nx = 0.0F;
    float ny = 0.0F;
    menu_row_center(file_new, &nx, &ny);
    for (int f = 0; f < 30; ++f) {
        menu_im_frame(&st, &style, nx, ny);
    }
    TEST_ASSERT_TRUE(menu_row_lit(file_new) > 0.5F);
    menu_key(NT_KEY_ARROW_LEFT); /* closes File's level; New keeps its eased highlight */
    menu_im_frame(&st, &style, nx, ny);
    nt_input_init();
    menu_im_frame(&st, &style, nx, ny);
    TEST_ASSERT_FALSE(menu_level_found(1U));
    menu_hover_open(&st, &style, file, 1U, menu_im_frame); /* the pointer opens File again: focus stays on File */
    TEST_ASSERT_TRUE_MESSAGE(menu_row_lit(file_new) < 0.5F, "New, shown again unfocused, does not ease out from lit");
}

/* ---- The pointer over the open child holds it even while focus is on a sibling. ---- */
static void test_menu_pointer_over_child_holds_it(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_hover_frame);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_hover_frame(&st, &style, ex, ey);
    menu_im_hover_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    const nt_ui_bbox_t sub = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U));
    for (int f = 0; f < 30; ++f) {
        menu_im_hover_frame(&st, &style, sub.x + 2.0F, sub.y + 2.0F); /* the child panel's padding */
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the pointer over the open child holds it");
}

/* ---- The opening frame ignores a click at the old place: a release over a row's last-frame rect after the
 *      menu reopened elsewhere activates nothing. ---- */
static void test_menu_reopen_ignores_last_frame_click(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    menu_dyn_frame(&st, &style, &t);
    const uint32_t l1 = nt_ui_menu_test_item_id(MENU_A, KEY_L1);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(l1, &cx, &cy);
    nt_pointer_t p = {.x = cx, .y = cy, .active = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    nt_ui_menu_open(&st, 400.0F, 300.0F);
    s_act_capture_id = l1;
    s_act_hit = false;
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "the release at the old place activates nothing");
    TEST_ASSERT_TRUE(st.open);
}

/* One frame: the menu, then a modal declared after it (the modal is on top); reports the modal's close. */
static bool menu_under_modal_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.flags |= NT_UI_MODAL_LISTEN_ESC;
    ms.flags &= (uint8_t)~NT_UI_MODAL_CLOSE_ON_BACKDROP; /* the press on the menu must not close the modal */
    menu_frame_begin(p);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), nt_ui_menu_item(st, KEY_NEW, "New"));
        nt_ui_menu_end(st);
    }
    const bool modal_close = nt_ui_modal_begin(s_fx.ctx, MODAL_ID, &ms, true).close_requested;
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    return modal_close;
}

/* ---- A modal opened over the menu owns the keys and the pointer: a full click on a menu row activates
 *      nothing, the menu stays open under the modal, and Esc closes only the modal. ---- */
static void test_menu_under_modal_is_inert(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    (void)menu_under_modal_frame(&st, &style, &(nt_pointer_t){.active = true});
    (void)menu_under_modal_frame(&st, &style, &(nt_pointer_t){.active = true});
    const uint32_t row = nt_ui_menu_test_item_id(MENU_A, KEY_NEW);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(row, &cx, &cy);
    s_act_capture_id = row;
    s_act_hit = false;
    nt_pointer_t p = {.x = cx, .y = cy, .active = true};
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    (void)menu_under_modal_frame(&st, &style, &p);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    (void)menu_under_modal_frame(&st, &style, &p);
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "a menu row under the modal clicks nothing");
    TEST_ASSERT_TRUE_MESSAGE(st.open, "the menu stays open under the modal");
    menu_key(NT_KEY_ESCAPE);
    TEST_ASSERT_TRUE_MESSAGE(menu_under_modal_frame(&st, &style, &(nt_pointer_t){.active = true}), "Esc closes the modal");
    TEST_ASSERT_TRUE_MESSAGE(st.open, "and only the modal");
}

#define MODAL_BTN 0x40DA2U

/* One frame: a modal holding a button, then the menu declared over it (the menu is on top); returns whether
 * the button clicked. */
static bool menu_over_modal_button_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    menu_frame_begin(p);
    (void)nt_ui_modal_begin(s_fx.ctx, MODAL_ID, &ms, true);
    CLAY({.id = (Clay_ElementId){.id = MODAL_BTN}, .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(30)}}}) {}
    const bool clicked = nt_ui_step(s_fx.ctx, MODAL_BTN, NULL).clicked;
    nt_ui_modal_end(s_fx.ctx);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        (void)nt_ui_menu_item(st, KEY_NEW, "New");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
    return clicked;
}

/* ---- A menu over a modal owns the pointer: a full click on the modal's button outside the menu closes the
 *      menu and clicks nothing. ---- */
static void test_menu_over_modal_blocks_its_button(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    const nt_pointer_t idle = {.active = true};
    (void)menu_over_modal_button_frame(&st, &style, &idle);
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    (void)menu_over_modal_button_frame(&st, &style, &idle);
    (void)menu_over_modal_button_frame(&st, &style, &idle);
    const nt_ui_bbox_t btn = nt_ui_get_bbox(s_fx.ctx, MODAL_BTN);
    TEST_ASSERT_TRUE(btn.found);
    nt_pointer_t p = {.x = btn.x + 40.0F, .y = btn.y + 15.0F, .active = true};
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    bool clicked = menu_over_modal_button_frame(&st, &style, &p);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "the press outside the menu closes it");
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    clicked = menu_over_modal_button_frame(&st, &style, &p) || clicked;
    TEST_ASSERT_FALSE_MESSAGE(clicked, "the press that closed the menu does not click the modal's button");
}

#define MENU_B 0x4E5002U

/* ---- Two menus opened in one frame: a key has one owner. On the opening frame neither reads Esc; later
 *      one Esc closes exactly one of them. ---- */
static void test_menu_one_esc_for_two_menus(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t a = {0};
    nt_ui_menu_t b = {0};
    nt_ui_menu_open(&a, 120.0F, 80.0F);
    nt_ui_menu_open(&b, 400.0F, 300.0F);
    for (int f = 0; f < 4; ++f) {
        if (f == 0 || f == 3) {
            menu_key(NT_KEY_ESCAPE);
        } else {
            nt_input_init();
        }
        menu_frame_begin(&(nt_pointer_t){.active = true});
        if (nt_ui_menu_begin(&a, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
            (void)nt_ui_menu_item(&a, KEY_NEW, "A");
            nt_ui_menu_end(&a);
        }
        if (nt_ui_menu_begin(&b, s_fx.ctx, NULL, 0U, MENU_B, &style)) {
            (void)nt_ui_menu_item(&b, KEY_NEW, "B");
            nt_ui_menu_end(&b);
        }
        nt_ui_end(s_fx.ctx);
        if (f == 0) {
            TEST_ASSERT_TRUE_MESSAGE(a.open && b.open, "an Esc on the opening frame has no owner yet");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(a.open != b.open, "one Esc closes exactly one menu");
}

/* ---- The press that reopens the menu over one of its rows clicks nothing on release. ---- */
static void test_menu_reopening_press_clicks_nothing(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    menu_dyn_frame(&st, &style, &t);
    menu_dyn_frame(&st, &style, &t);
    const uint32_t l1 = nt_ui_menu_test_item_id(MENU_A, KEY_L1);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(l1, &cx, &cy);
    nt_pointer_t p = {.x = cx, .y = cy, .active = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    nt_ui_menu_open(&st, 120.0F, 80.0F); /* the game reopens the menu with this press, at the same place */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    s_act_capture_id = l1;
    s_act_hit = false;
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "the reopening press is not a click on the row under it");
    TEST_ASSERT_TRUE(st.open);
}

/* ---- The outside press belongs to the pointer that pressed: a second finger resting on the menu does not
 *      keep it open. ---- */
static void test_menu_outside_press_ignores_other_pointers(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    nt_pointer_t two[2] = {{.x = 780.0F, .y = 580.0F, .active = true, .type = NT_POINTER_TOUCH}, {.x = ex, .y = ey, .active = true, .type = NT_POINTER_TOUCH}};
    two[0].buttons[NT_BUTTON_LEFT].is_down = true;
    two[0].buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_arena_reset(&g_nt_mem_scratch);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = 1.0F / 60.0F, .pointers = two, .pointer_count = 2, .primary = 0});
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
        (void)nt_ui_menu_item(&st, KEY_EDIT, "Edit");
        nt_ui_menu_end(&st);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "the primary pointer's outside press dismisses the menu");
}

/* ---- A child opened last frame has no rect yet: the pointer flicking on to a sibling keeps it (the
 *      corridor decides from the next frame). ---- */
static void test_menu_fresh_child_survives_a_flick(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame2_idle(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    float fx = 0.0F;
    float fy = 0.0F;
    float tx = 0.0F;
    float ty = 0.0F;
    menu_row_center(file, &fx, &fy);
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &tx, &ty);
    menu_im_frame2_idle(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32(file, st.open_path[0]);
    menu_im_frame2_idle(&st, &style, tx, ty);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file, st.open_path[0], "File's child, opened last frame, survives the flick");
}

/* ---- A key ends a pending switch: Right on a leaf beside a held sibling's child closes that child. ---- */
static void test_menu_key_ends_pending_switch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_hover_frame);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_hover_frame(&st, &style, ex, ey);
    menu_im_hover_frame(&st, &style, ex, ey);
    TEST_ASSERT_TRUE(menu_level_found(1U));
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_hover_frame(&st, &style, ex, ey);
    nt_input_init();
    menu_im_hover_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(0U, st.open_path[0]);
    TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    TEST_ASSERT_TRUE(st.open);
}

/* ---- A press outside the panels on a popup a custom row opened is inside the menu. ---- */
#define ROW_POPUP_ID 0x9091U
#define ROW_POPUP_BOX 0x9092U
static void menu_row_popup_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    menu_frame_begin(p);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        nt_ui_menu_item_begin(st, KEY_NEW, true, false);
        nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
        pst.flags = s_row_popup_catcher ? NT_UI_POPUP_LIGHT_DISMISS : 0U; /* a combo list, or a tooltip-like one */
        pst.ease_speed = 0.0F;
        const nt_ui_popup_anchor_t anchor = {.x = 600.0F, .y = 450.0F, .prefer_side = NT_UI_POPUP_BELOW};
        bool open = true;
        if (nt_ui_popup_visible(s_fx.ctx, ROW_POPUP_ID, &pst, &anchor, &open)) {
            CLAY({.id = (Clay_ElementId){.id = ROW_POPUP_BOX}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(40)}}}){}(void)nt_ui_step(s_fx.ctx, ROW_POPUP_BOX, NULL);
            nt_ui_popup_end(s_fx.ctx);
        }
        (void)nt_ui_menu_item_end(st);
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}
static void test_menu_press_on_row_popup_keeps_open(void) {
    for (int c = 0; c < 2; ++c) {
        s_row_popup_catcher = (c == 1);
        nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        const nt_pointer_t idle = {.active = true};
        menu_row_popup_frame(&st, &style, &idle);
        menu_row_popup_frame(&st, &style, &idle);
        const nt_ui_bbox_t box = nt_ui_get_bbox(s_fx.ctx, ROW_POPUP_BOX);
        TEST_ASSERT_TRUE(box.found);
        nt_pointer_t p = {.x = box.x + 30.0F, .y = box.y + 20.0F, .active = true};
        menu_row_popup_frame(&st, &style, &p);
        p.buttons[NT_BUTTON_LEFT].is_down = true;
        p.buttons[NT_BUTTON_LEFT].is_pressed = true;
        menu_row_popup_frame(&st, &style, &p);
        TEST_ASSERT_TRUE_MESSAGE(st.open, "a press on a popup inside the menu is not an outside press, nor does that popup supersede it");
    }
}

/* ---- Right on a focused leaf does nothing. ---- */
static void test_menu_right_on_leaf_does_nothing(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    const uint32_t l1 = nt_ui_menu_test_item_id(MENU_A, KEY_L1);
    s_act_capture_id = l1;
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    menu_dyn_keys(&st, &style, &t, keys, 2);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_EQUAL_UINT32(l1, st.focus);
    TEST_ASSERT_FALSE(s_act_hit);
}

/* A press of `button` without movement after a key: focus returns to the row under the pointer. */
static void menu_still_press_ends_keyboard_mode(nt_button_t button) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_hover_frame(&st, &style, ex, ey);
    menu_im_hover_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    menu_key(NT_KEY_ARROW_DOWN); /* wraps to File */
    menu_im_hover_frame(&st, &style, ex, ey);
    nt_input_init();
    menu_im_hover_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st.focus);
    nt_pointer_t p = {.x = ex, .y = ey, .active = true};
    p.buttons[button].is_down = true;
    p.buttons[button].is_pressed = true;
    menu_im_hover_frame_ptr(&st, &style, &p);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(edit, st.focus, "a still press hands focus back to the pointer");
    TEST_ASSERT_TRUE(st.open);
}

static void test_menu_still_press_ends_keyboard_mode(void) { menu_still_press_ends_keyboard_mode(NT_BUTTON_LEFT); }
static void test_menu_still_right_press_ends_keyboard_mode(void) { menu_still_press_ends_keyboard_mode(NT_BUTTON_RIGHT); }

/* ---- Reopening the menu on top starts it fresh: an Esc on the reopening frame has no owner yet. ---- */
static void test_menu_reopen_on_top_takes_no_same_frame_esc(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_ui_menu_open(&st, 300.0F, 200.0F);
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "the reopened menu reads keys from its next frame");
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE(st.open);
}

/* ---- A modal over the menu stops its pending switch: the sibling the pointer rests on does not take
 *      the branch while the modal is up. ---- */
static void test_menu_covered_switch_waits(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    menu_im_hover_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    menu_hover_open(&st, &style, file, 1U, menu_im_hover_frame);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    menu_im_hover_frame(&st, &style, ex, ey); /* the switch to Edit is pending: File's child stays */
    TEST_ASSERT_EQUAL_UINT32(file, st.open_path[0]);
    s_modal_over = true;
    for (int f = 0; f < (int)(4.0F * NT_UI_MENU_AIM_FALLBACK_SECS * 60.0F); ++f) {
        menu_im_hover_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file, st.open_path[0], "the covered menu's switch waits for the modal");
}

/* ---- A right press while the left drags across the rows is inside the menu: it stays open. ---- */
static void test_menu_right_press_during_a_drag_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    menu_dyn_frame(&st, &style, &t);
    menu_dyn_frame(&st, &style, &t);
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_L1), &x1, &y1);
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_L2), &x2, &y2);
    nt_pointer_t p = {.x = x1, .y = y1, .active = true};
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.x = x2;
    p.y = y2;
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "a press over a row is not an outside press, whatever the left holds");
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = false, .is_released = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.x = 780.0F;
    p.y = 580.0F;
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){0};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "a press off the menu dismisses it while the left still holds a row");
}

/* ---- Right and Esc in one frame: the frame acts on one key, and Esc comes first, so the menu closes. ---- */
static void test_menu_right_and_esc_in_one_frame(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    static const uint32_t p1[] = {KEY_P1};
    const menu_dyn_tree_t t = {.keys = p1, .count = 1};
    const nt_key_t down = NT_KEY_ARROW_DOWN;
    menu_dyn_keys(&st, &style, &t, &down, 1);
    nt_input_init();
    nt_input_feed(nt_test_key(NT_KEY_ARROW_RIGHT, true));
    nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    nt_input_poll();
    menu_dyn_frame(&st, &style, &t);
    TEST_ASSERT_FALSE_MESSAGE(st.open, "Esc at the root closes the menu; the Right does nothing");
}

/* ---- A focused row removed for one frame loses focus: when it returns, Enter does not activate it. ---- */
static void test_menu_returned_row_is_not_focused(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    static const uint32_t l13[] = {KEY_L1, KEY_L3};
    const menu_dyn_tree_t full = {.keys = l123, .count = 3};
    const menu_dyn_tree_t lost = {.keys = l13, .count = 2};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    const nt_key_t focus_l2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &full, focus_l2, 2);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_L2), st.focus);
    menu_dyn_frame(&st, &style, &lost);
    menu_dyn_frame(&st, &style, &full);
    TEST_ASSERT_FALSE_MESSAGE(menu_dyn_enter_activates(&st, &style, &full, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "a removed row loses focus");
    TEST_ASSERT_TRUE(st.open);
}

#define ROW_POPUP 0x4E50A1U

/* One frame: root = [custom row L1 that may hold an open popup, parent P1 > Opt]. */
static void menu_row_popup_branch_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    menu_frame_begin(&(nt_pointer_t){.x = px, .y = py, .active = true});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        nt_ui_menu_item_begin(st, KEY_L1, true, false);
        if (s_row_popup) {
            nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
            ps.ease_speed = 0.0F;
            const nt_ui_popup_anchor_t anc = {.x = 500.0F, .y = 350.0F, .prefer_side = NT_UI_POPUP_BELOW};
            (void)nt_ui_popup_begin(s_fx.ctx, ROW_POPUP, &ps, &anc, true);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(30)}}}) {}
            nt_ui_popup_end(s_fx.ctx);
        }
        nt_ui_menu_item_end(st);
        if (nt_ui_menu_submenu_begin(st, KEY_P1, "Parent")) {
            act_capture(nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_P1), KEY_OPT), nt_ui_menu_item(st, KEY_OPT, "Opt"));
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A popup a row opens is that level's child: on top, it closes the open branch beside it, so no
 *      sibling's level stays above it. ---- */
static void test_menu_row_popup_closes_deeper_levels(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    s_row_popup = false;
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_row_popup_branch_frame(&st, &style, 0.0F, 0.0F);
    menu_row_popup_branch_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t p1 = nt_ui_menu_test_item_id(MENU_A, KEY_P1);
    menu_hover_open(&st, &style, p1, 1U, menu_row_popup_branch_frame);
    float px = 0.0F;
    float py = 0.0F;
    menu_row_center(p1, &px, &py);
    s_row_popup = true; /* the game opens it, e.g. from a control in the row */
    for (int f = 0; f < 3; ++f) {
        menu_row_popup_branch_frame(&st, &style, px, py);
    }
    TEST_ASSERT_EQUAL_UINT32(ROW_POPUP, s_fx.ctx->overlay_top_id_prev);
    TEST_ASSERT_TRUE(st.open);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[0], "the row's popup closes the sibling's open level");
    TEST_ASSERT_FALSE(menu_level_found(1U));
}

/* ---- The row's popup on top closes the sibling's open level before the rows declare: the frame after the
 *      popup first claims the top declares no level above it, so no control there takes a press. ---- */
static void test_menu_row_popup_closes_the_branch_before_the_rows(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_row_popup_branch_frame(&st, &style, 0.0F, 0.0F);
    menu_row_popup_branch_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t p1 = nt_ui_menu_test_item_id(MENU_A, KEY_P1);
    menu_hover_open(&st, &style, p1, 1U, menu_row_popup_branch_frame);
    float px = 0.0F;
    float py = 0.0F;
    menu_row_center(p1, &px, &py);
    s_row_popup = true;
    menu_row_popup_branch_frame(&st, &style, px, py); /* the popup claims the top */
    TEST_ASSERT_TRUE(menu_level_found(1U));
    menu_row_popup_branch_frame(&st, &style, px, py);
    TEST_ASSERT_EQUAL_UINT32(ROW_POPUP, s_fx.ctx->overlay_top_id_prev);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "the sibling's level is not declared over the row's popup");
}

/* ---- An outside press closes the menu before its rows declare: the press frame declares no level. ---- */
static void test_menu_outside_press_closes_before_the_rows(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 780.0F, 580.0F);
    menu_im_frame(&st, &style, 780.0F, 580.0F);
    TEST_ASSERT_TRUE(menu_level_found(0U));
    menu_im_press(&st, &style, 780.0F, 580.0F, NT_POINTER_MOUSE);
    TEST_ASSERT_FALSE(st.open);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(0U), "the press frame declares no level");
}

/* ---- A tap on one parent and Right on a later focused parent in one frame, with a third parent's child
 *      pending: the tap opens its parent. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_click_beats_a_same_frame_key(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t parents[] = {KEY_PARENT, KEY_PARENT + 1U, KEY_PARENT + 2U};
    const menu_dyn_tree_t t = {.keys = parents, .count = 3};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_dyn_frame(&st, &style, &t);
    menu_dyn_frame(&st, &style, &t);
    const uint32_t b = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT);
    const uint32_t c = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT + 1U);
    const uint32_t a = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT + 2U);
    float x = 0.0F;
    float y = 0.0F;
    menu_row_center(c, &x, &y);
    for (int f = 0; f < 3 && st.open_path[0] != c; ++f) {
        menu_dyn_frame_ptr(&st, &style, &t, &(nt_pointer_t){.x = x, .y = y, .active = true});
    }
    TEST_ASSERT_EQUAL_UINT32(c, st.open_path[0]);
    menu_row_center(a, &x, &y);
    menu_dyn_frame_ptr(&st, &style, &t, &(nt_pointer_t){.x = x, .y = y, .active = true}); /* focus A, C's child pending */
    TEST_ASSERT_EQUAL_UINT32(a, st.focus);
    TEST_ASSERT_EQUAL_UINT32(c, st.open_path[0]);
    menu_row_center(b, &x, &y);
    nt_pointer_t tap = {.x = x, .y = y, .active = true, .type = NT_POINTER_TOUCH};
    tap.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_pressed = true, .is_released = true};
    menu_key(NT_KEY_ARROW_RIGHT); /* on A */
    menu_dyn_frame_ptr(&st, &style, &t, &tap);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b, st.open_path[0], "the tap on B opens B over the Right on A");
}

/* ---- An open menu with a submenu keeps no state-pool cell: its levels are snap popups. ---- */
static void test_menu_keeps_no_state_cells(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_open_file_open(&st, &style, menu_im_frame2_idle);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
}

/* ---- Rows with an instant highlight (state_speed 0) take no anim slot, even on the frame they show. ---- */
static void test_menu_instant_rows_take_no_anim_slot(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.state_speed = 0.0F;
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    for (uint32_t i = 0; i < NT_UI_ANIM_SLOTS; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(s_fx.ctx->anim[i].id != file && s_fx.ctx->anim[i].id != edit, "an instant row holds no anim slot");
    }
}

/* ---- The frame after a row's popup takes the top, the sibling level still declared above it is inert:
 *      a tap on its row activates nothing. ---- */
static void test_menu_covered_rows_take_no_tap(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_row_popup_branch_frame(&st, &style, 0.0F, 0.0F);
    menu_row_popup_branch_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t p1 = nt_ui_menu_test_item_id(MENU_A, KEY_P1);
    menu_hover_open(&st, &style, p1, 1U, menu_row_popup_branch_frame);
    const uint32_t opt = nt_ui_menu_test_item_id(p1, KEY_OPT);
    float ox = 0.0F;
    float oy = 0.0F;
    menu_row_center(opt, &ox, &oy);
    s_row_popup = true;
    menu_row_popup_branch_frame(&st, &style, 790.0F, 590.0F); /* the popup claims the top */
    s_act_capture_id = opt;
    s_act_hit = false;
    nt_pointer_t tap = {.x = ox, .y = oy, .active = true, .type = NT_POINTER_TOUCH};
    tap.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_pressed = true, .is_released = true};
    menu_frame_begin(&tap);
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, MENU_A, &style)) {
        nt_ui_menu_item_begin(&st, KEY_L1, true, false);
        nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
        ps.ease_speed = 0.0F;
        const nt_ui_popup_anchor_t anc = {.x = 500.0F, .y = 350.0F, .prefer_side = NT_UI_POPUP_BELOW};
        (void)nt_ui_popup_begin(s_fx.ctx, ROW_POPUP, &ps, &anc, true);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(30)}}}) {}
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_menu_item_end(&st);
        if (nt_ui_menu_submenu_begin(&st, KEY_P1, "Parent")) {
            act_capture(opt, nt_ui_menu_item(&st, KEY_OPT, "Opt"));
            nt_ui_menu_submenu_end(&st);
        }
        nt_ui_menu_end(&st);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "a covered menu's row takes no tap");
    TEST_ASSERT_TRUE_MESSAGE(st.focus != opt, "nor focus from the pointer over it");
}

#define MENU_FIELD 0x4E50F1U
static char s_field_buf[16];

/* One frame: root = [custom row L1 holding a text field, leaf L2]. */
static void menu_field_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    nt_ui_input_style_t is = nt_ui_input_style_defaults();
    is.text.font_size = 16.0F;
    is.placeholder.font_size = 16.0F;
    is.caret_blink_rate = 0.0F;
    const nt_ui_input_props_t props = {0};
    const Clay_ElementDeclaration field = {.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(20)}}};
    menu_frame_begin(p);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        nt_ui_menu_item_begin(st, KEY_L1, true, false);
        (void)nt_ui_input_text(s_fx.ctx, NULL, 0, MENU_FIELD, s_field_buf, sizeof s_field_buf, &props, &is, &field, true, NULL);
        nt_ui_menu_item_end(st);
        (void)nt_ui_menu_item(st, KEY_L2, "Leaf");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A text field focused inside the menu owns the keys: Left moves its caret, the menu stays open. ---- */
static void test_menu_focused_field_owns_keys(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    memset(s_field_buf, 0, sizeof s_field_buf);
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_field_frame(&st, &style, &(nt_pointer_t){.active = true});
    menu_field_frame(&st, &style, &(nt_pointer_t){.active = true});
    const nt_ui_bbox_t fb = nt_ui_get_bbox(s_fx.ctx, MENU_FIELD);
    TEST_ASSERT_TRUE(fb.found);
    nt_pointer_t tap = {.x = fb.x + 10.0F, .y = fb.y + 10.0F, .active = true, .type = NT_POINTER_TOUCH};
    tap.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_pressed = true, .is_released = true};
    menu_field_frame(&st, &style, &tap);
    const nt_pointer_t rest = {.x = fb.x + 10.0F, .y = fb.y + 10.0F, .active = true};
    menu_field_frame(&st, &style, &rest);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, MENU_FIELD));
    menu_key(NT_KEY_ARROW_LEFT);
    menu_field_frame(&st, &style, &rest);
    TEST_ASSERT_TRUE_MESSAGE(st.open, "Left belongs to the focused field");
    menu_key(NT_KEY_ESCAPE);
    menu_field_frame(&st, &style, &rest);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s_fx.ctx, MENU_FIELD));
    TEST_ASSERT_TRUE_MESSAGE(st.open, "Esc unfocuses the field and does not also close the menu");
}

/* One frame: a text field in the base UI, then the menu (open or not). */
static void menu_base_field_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    nt_ui_input_style_t is = nt_ui_input_style_defaults();
    is.text.font_size = 16.0F;
    is.placeholder.font_size = 16.0F;
    is.caret_blink_rate = 0.0F;
    const nt_ui_input_props_t props = {0};
    const Clay_ElementDeclaration field = {.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(20)}}};
    menu_frame_begin(p);
    (void)nt_ui_input_text(s_fx.ctx, NULL, 0, MENU_FIELD, s_field_buf, sizeof s_field_buf, &props, &is, &field, true, NULL);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        (void)nt_ui_menu_item(st, KEY_L1, "Leaf");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A field focused under the menu loses focus once the menu is on top: the menu's keys are its own. ---- */
static void test_menu_takes_focus_from_a_field_under_it(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    memset(s_field_buf, 0, sizeof s_field_buf);
    const nt_pointer_t idle = {.x = 790.0F, .y = 590.0F, .active = true};
    menu_base_field_frame(&st, &style, &idle);
    menu_base_field_frame(&st, &style, &idle);
    const nt_ui_bbox_t fb = nt_ui_get_bbox(s_fx.ctx, MENU_FIELD);
    TEST_ASSERT_TRUE(fb.found);
    nt_pointer_t tap = {.x = fb.x + 10.0F, .y = fb.y + 10.0F, .active = true, .type = NT_POINTER_TOUCH};
    tap.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_pressed = true, .is_released = true};
    menu_base_field_frame(&st, &style, &tap);
    menu_base_field_frame(&st, &style, &idle);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, MENU_FIELD));
    nt_ui_menu_open(&st, 300.0F, 200.0F);
    menu_base_field_frame(&st, &style, &idle);
    menu_base_field_frame(&st, &style, &idle);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_input_focused(s_fx.ctx, MENU_FIELD), "the menu on top takes the focus of a field under it");
}

/* ---- Enter or Right with no focused row has nothing to act on: the pointer keeps its mode. ---- */
static void test_menu_key_without_focus_keeps_pointer_mode(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const nt_key_t keys[] = {NT_KEY_ENTER, NT_KEY_ARROW_RIGHT};
    for (int k = 0; k < 2; ++k) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 790.0F, 590.0F);
        menu_im_frame(&st, &style, 790.0F, 590.0F);
        TEST_ASSERT_EQUAL_UINT32(0U, st.focus);
        menu_key(keys[k]);
        menu_im_frame(&st, &style, 790.0F, 590.0F);
        nt_input_init();
        TEST_ASSERT_TRUE(st.open);
        TEST_ASSERT_FALSE_MESSAGE(st.kbd_mode, "a key with no focused row leaves the pointer in charge");
    }
}

/* ---- A level whose focused row is gone still highlights one row: its open parent. ---- */
static void test_menu_lost_focus_lights_the_open_parent(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.state_speed = 0.0F; /* highlights snap */
    static const uint32_t full_keys[] = {KEY_PARENT, KEY_L1};
    static const uint32_t lost_keys[] = {KEY_PARENT};
    const menu_dyn_tree_t full = {.keys = full_keys, .count = 2};
    const menu_dyn_tree_t lost = {.keys = lost_keys, .count = 1};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_dyn_frame(&st, &style, &full);
    menu_dyn_frame(&st, &style, &full);
    const uint32_t p = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT);
    float x = 0.0F;
    float y = 0.0F;
    menu_row_center(p, &x, &y);
    for (int f = 0; f < 3 && st.open_path[0] != p; ++f) {
        menu_dyn_frame_ptr(&st, &style, &full, &(nt_pointer_t){.x = x, .y = y, .active = true});
    }
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_L1), &x, &y);
    menu_dyn_frame_ptr(&st, &style, &full, &(nt_pointer_t){.x = x, .y = y, .active = true}); /* focus L1, switch pending */
    menu_dyn_frame_ptr(&st, &style, &lost, &(nt_pointer_t){.x = 790.0F, .y = 590.0F, .active = true});
    menu_dyn_frame_ptr(&st, &style, &lost, &(nt_pointer_t){.x = 790.0F, .y = 590.0F, .active = true});
    TEST_ASSERT_EQUAL_UINT32(p, st.open_path[0]);
    TEST_ASSERT_TRUE_MESSAGE(menu_row_lit(p) > 0.0F, "the open parent is lit when its level has no focus");
}

/* ---- A menu on top reopened elsewhere with a press outside it stays open: the press belongs to the
 *      reopening, for either button. ---- */
static void test_menu_reopening_press_outside_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const nt_button_t buttons[] = {NT_BUTTON_LEFT, NT_BUTTON_RIGHT};
    for (int b = 0; b < 2; ++b) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        nt_ui_menu_open(&st, 400.0F, 300.0F);
        nt_pointer_t p = {.x = 780.0F, .y = 580.0F, .active = true};
        p.buttons[buttons[b]] = (nt_button_state_t){.is_down = true, .is_pressed = true};
        menu_im_frame_ptr(&st, &style, &p);
        TEST_ASSERT_TRUE_MESSAGE(st.open, "the reopening press is not an outside press");
        menu_im_frame(&st, &style, 780.0F, 580.0F);
    }
}

/* One frame: root = [File > (custom row L1 that may hold an open popup, Open > (Project))]. */
static void menu_deep_popup_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    menu_frame_begin(&(nt_pointer_t){.x = px, .y = py, .active = true});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, MENU_A, style)) {
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            nt_ui_menu_item_begin(st, KEY_L1, true, false);
            if (s_deep_popup) {
                nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
                ps.ease_speed = 0.0F;
                const nt_ui_popup_anchor_t anc = {.x = 600.0F, .y = 450.0F, .prefer_side = NT_UI_POPUP_BELOW};
                (void)nt_ui_popup_begin(s_fx.ctx, ROW_POPUP, &ps, &anc, true);
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(30)}}}) {}
                nt_ui_popup_end(s_fx.ctx);
            }
            nt_ui_menu_item_end(st);
            if (nt_ui_menu_submenu_begin(st, KEY_OPEN, "Open")) {
                (void)nt_ui_menu_item(st, KEY_PROJECT, "Project");
                nt_ui_menu_submenu_end(st);
            }
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A popup a level-1 row opens closes only the branch below that row: File stays open, Open closes. ---- */
static void test_menu_deep_row_popup_closes_its_level_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, 120.0F, 80.0F);
    menu_deep_popup_frame(&st, &style, 0.0F, 0.0F);
    menu_deep_popup_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    menu_hover_open(&st, &style, file, 1U, menu_deep_popup_frame);
    const uint32_t open = nt_ui_menu_test_item_id(file, KEY_OPEN);
    menu_hover_open(&st, &style, open, 2U, menu_deep_popup_frame);
    float x = 0.0F;
    float y = 0.0F;
    menu_row_center(open, &x, &y);
    s_deep_popup = true;
    for (int f = 0; f < 3; ++f) {
        menu_deep_popup_frame(&st, &style, x, y);
    }
    TEST_ASSERT_EQUAL_UINT32(file, st.open_path[0]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.open_path[1], "the popup closes the branch beside its row");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_one_activation_per_frame);
    RUN_TEST(test_menu_panel_blocks_pointer_under_it);
    RUN_TEST(test_menu_right_into_open_parent_closes_grandchild);
    RUN_TEST(test_menu_right_beside_open_sibling_opens_at_once);
    RUN_TEST(test_menu_reopen_ignores_last_frame_hits);
    RUN_TEST(test_menu_pending_switch_lands_off_the_rows);
    RUN_TEST(test_menu_one_highlight_per_level);
    RUN_TEST(test_menu_reshown_row_does_not_flash);
    RUN_TEST(test_menu_pointer_over_child_holds_it);
    RUN_TEST(test_menu_reopen_ignores_last_frame_click);
    RUN_TEST(test_menu_under_modal_is_inert);
    RUN_TEST(test_menu_over_modal_blocks_its_button);
    RUN_TEST(test_menu_one_esc_for_two_menus);
    RUN_TEST(test_menu_reopening_press_clicks_nothing);
    RUN_TEST(test_menu_outside_press_ignores_other_pointers);
    RUN_TEST(test_menu_fresh_child_survives_a_flick);
    RUN_TEST(test_menu_key_ends_pending_switch);
    RUN_TEST(test_menu_press_on_row_popup_keeps_open);
    RUN_TEST(test_menu_right_on_leaf_does_nothing);
    RUN_TEST(test_menu_still_press_ends_keyboard_mode);
    RUN_TEST(test_menu_still_right_press_ends_keyboard_mode);
    RUN_TEST(test_menu_reopen_on_top_takes_no_same_frame_esc);
    RUN_TEST(test_menu_covered_switch_waits);
    RUN_TEST(test_menu_right_press_during_a_drag_keeps_open);
    RUN_TEST(test_menu_returned_row_is_not_focused);
    RUN_TEST(test_menu_row_popup_closes_deeper_levels);
    RUN_TEST(test_menu_row_popup_closes_the_branch_before_the_rows);
    RUN_TEST(test_menu_outside_press_closes_before_the_rows);
    RUN_TEST(test_menu_click_beats_a_same_frame_key);
    RUN_TEST(test_menu_instant_rows_take_no_anim_slot);
    RUN_TEST(test_menu_keeps_no_state_cells);
    RUN_TEST(test_menu_covered_rows_take_no_tap);
    RUN_TEST(test_menu_focused_field_owns_keys);
    RUN_TEST(test_menu_takes_focus_from_a_field_under_it);
    RUN_TEST(test_menu_key_without_focus_keeps_pointer_mode);
    RUN_TEST(test_menu_lost_focus_lights_the_open_parent);
    RUN_TEST(test_menu_reopening_press_outside_keeps_open);
    RUN_TEST(test_menu_deep_row_popup_closes_its_level_branch);
    RUN_TEST(test_menu_right_and_esc_in_one_frame);
    RUN_TEST(test_menu_is_modal_overlay);
    RUN_TEST(test_menu_resting_pointer_yields_to_keyboard);
    RUN_TEST(test_menu_keys_act_on_the_focused_level);
    RUN_TEST(test_menu_press_in_just_closed_panel_keeps_open);
    RUN_TEST(test_menu_right_edge_root_clamp_and_submenu_flip);
    RUN_TEST(test_menu_hover_corridor_keeps_then_releases);
    RUN_TEST(test_menu_aim_near_edge_faces_apex);
    RUN_TEST(test_menu_aim_keeps_submenu_open_on_diagonal);
    RUN_TEST(test_menu_aim_switch_after_fallback);
    RUN_TEST(test_menu_kbd_nav_activates_nested_leaf);
    RUN_TEST(test_menu_nested_dismiss_esc_deepest_first);
    RUN_TEST(test_menu_switch_root_branch_while_submenu_open);
    RUN_TEST(test_menu_click_root_parent_switches_branch);
    RUN_TEST(test_menu_hover_root_parent_switches_branch);
    RUN_TEST(test_menu_hover_collapse_reopen_forgets_descendants);
    RUN_TEST(test_menu_insert_before_open_parent_keeps_branch);
    RUN_TEST(test_menu_focus_follows_key_across_edits);
    RUN_TEST(test_menu_lost_open_parent_closes_branch);
    RUN_TEST(test_menu_kbd_open_focuses_first_enabled_row);
    RUN_TEST(test_menu_kbd_empty_submenu);
    RUN_TEST(test_menu_outside_click_dismisses_chain);
    RUN_TEST(test_menu_right_click_outside_dismisses_chain);
    RUN_TEST(test_menu_inside_click_keeps_open);
    RUN_TEST(test_menu_depth_cap_asserts);
    RUN_TEST(test_menu_icon_gutter_aligns_labels);
    RUN_TEST(test_menu_icon_gutter_absent_when_zero);
    RUN_TEST(test_menu_separator_non_interactive);
    RUN_TEST(test_menu_arrow_marker_on_parent_only);
    RUN_TEST(test_menu_pointer_pos);
    RUN_TEST(test_menu_item_id_distinct_siblings_and_scopes);
    RUN_TEST(test_menu_left_at_root_closes);
    RUN_TEST(test_menu_reopen_starts_at_root);
    RUN_TEST(test_menu_disabled_focused_row_keeps_its_place);
    RUN_TEST(test_menu_up_down_wrap);
    RUN_TEST(test_menu_down_skips_disabled);
    RUN_TEST(test_menu_item_begin_child_owns_its_press);
    RUN_TEST(test_menu_item_begin_press_beside_child);
    RUN_TEST(test_menu_item_begin_captured_drag_keeps_focus);
    RUN_TEST(test_menu_item_begin_enter_activates);
    RUN_TEST(test_menu_click_activates_enabled_rows_only);
    RUN_TEST(test_menu_item_begin_not_activatable_ignores_enter);
    RUN_TEST(test_menu_row_in_custom_row_asserts);
    RUN_TEST(test_menu_separator_in_custom_row_asserts);
    RUN_TEST(test_menu_checked_parent_asserts);
    RUN_TEST(test_menu_shortcut_cell_on_rich_row_only);
    RUN_TEST(test_menu_check_cell_when_selected);
    RUN_TEST(test_menu_duplicate_sibling_key_asserts);
    RUN_TEST(test_menu_separator_text_non_interactive);
    RUN_TEST(test_menu_submenu_begin_ex_icon_disabled);
    return UNITY_END();
}
