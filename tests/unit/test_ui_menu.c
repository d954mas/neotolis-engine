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
#include "test_helpers/ui_fx_frame.h"
#include "test_helpers/ui_menu_tree.h"
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

/* The 60 Hz frame on which AIM_FALLBACK runs out. */
#define AIM_FALLBACK_FRAMES ((int)ceilf(NT_UI_MENU_AIM_FALLBACK_SECS / UI_FX_DT))

/* Row keys, unique among siblings; the parent's row id scopes each level. The other trees here reuse the shared
 * tree's keys. */
#define KEY_FILE UI_MT_KEY_FILE
#define KEY_EDIT UI_MT_KEY_EDIT
#define KEY_NEW UI_MT_KEY_NEW
#define KEY_OPEN UI_MT_KEY_OPEN
#define KEY_QUIT UI_MT_KEY_QUIT
#define KEY_PROJECT UI_MT_KEY_PROJECT
#define KEY_TOOLS UI_MT_KEY_TOOLS
#define KEY_OPT UI_MT_KEY_OPT

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
/* Whether menu_im_frame declares the test modal around the menu, kept open or closed by the game in this frame. */
typedef enum { MODAL_NONE, MODAL_HOLDS_MENU, MODAL_CLOSES_AROUND_MENU } menu_modal_t;
static menu_modal_t s_modal;
static int s_popup_depth;   /* menu_popup_frame: the menu depth whose custom row holds the popup (-1 = none) */
static bool s_popup_opened; /* that row opened its popup already: it opens once, as on a game's click */
static void menu_frame_begin(const nt_pointer_t *p) {
    nt_pointer_t q = *p;
    q.dx = s_ptr_seen ? q.x - s_ptr_x : 0.0F;
    q.dy = s_ptr_seen ? q.y - s_ptr_y : 0.0F;
    s_ptr_x = q.x;
    s_ptr_y = q.y;
    s_ptr_seen = true;
    nt_arena_reset(&g_nt_mem_scratch);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = UI_FX_DT, .pointers = &q, .pointer_count = 1});
}

void setUp(void) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_ptr_seen = false;
    s_modal = MODAL_NONE;
    s_popup_depth = -1;
    s_popup_opened = false;
    s_act_capture_id = 0U;
    s_act_hit = false;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* ---- The corridor aims at the submenu edge facing the apex: a submenu edge-flipped to the LEFT of its parent
 *      (x 200..350) is aimed at its RIGHT edge, so a cursor heading there keeps it past AIM_FALLBACK. ---- */
static void test_menu_aim_near_edge_faces_apex(void) {
    nt_ui_menu_aim_t aim = {0};
    const float dt = NT_UI_MENU_AIM_FALLBACK_SECS * 0.75F;
    TEST_ASSERT_TRUE(nt_ui_menu_test_hover_intent(&aim, 360.0F, 150.0F, 200.0F, 100.0F, 150.0F, 200.0F, 0.0F)); /* primes the apex */
    /* (352, 265) lies in the triangle toward the right edge, outside the one toward the left edge. */
    TEST_ASSERT_TRUE(nt_ui_menu_test_hover_intent(&aim, 352.0F, 265.0F, 200.0F, 100.0F, 150.0F, 200.0F, dt));
    TEST_ASSERT_TRUE(nt_ui_menu_test_hover_intent(&aim, 352.0F, 265.0F, 200.0F, 100.0F, 150.0F, 200.0F, dt));
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_menu_test_hover_intent(&aim, 352.0F, 265.0F, 200.0F, 100.0F, 150.0F, 200.0F, dt), "past AIM_FALLBACK only the corridor keeps it");
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
    TEST_ASSERT_INT_WITHIN(1, AIM_FALLBACK_FRAMES, frames_to_switch);
}

/* ============================ immediate full menu UI ============================ */

/* Produce exactly ONE fresh pressed-key edge for this frame. */
static void menu_key(nt_key_t key) { nt_input_test_key_press(key); }

/* True when the menu level `depth` was laid out last frame. */
static bool menu_level_found(uint8_t depth) { return nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, depth)).found; }

/* True when the row `row_id` was laid out last frame: a child level's row shows which parent holds it open. */
static bool menu_row_shown(uint32_t row_id) { return nt_ui_get_bbox(s_fx.ctx, row_id).found; }

/* The row `key` of the level scoped by `scope`: the parent row's id, or the menu id at the root. */
static uint32_t menu_row(uint32_t scope, uint32_t key) { return nt_ui_menu_test_item_id(scope, key); }

/* The shared tree: File > (New, Open > (Project, File2), Quit), Tools > (Opt), Edit; its activation goes to
 * act_capture. */
static void menu_im_tree(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    ui_menu_tree_t r;
    (void)ui_menu_tree(st, s_fx.ctx, style, &r);
    act_capture(r.activated, r.activated != 0U);
}

#define MODAL_ID 0x40DA1U

/* Places the test modal (menu_set_modal) and opens it, except for MODAL_CLOSES_AROUND_MENU: the game opens it
 * when it shows it first. */
static void menu_set_modal(menu_modal_t m) {
    s_modal = m;
    if (m == MODAL_HOLDS_MENU) {
        nt_ui_modal_open(s_fx.ctx, MODAL_ID);
    }
}

/* One frame of the menu tree, inside the test modal when s_modal places one. */
static void menu_im_frame_ptr(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    menu_frame_begin(p);
    if (s_modal == MODAL_NONE) {
        menu_im_tree(st, style);
    } else {
        nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
        if (s_modal == MODAL_CLOSES_AROUND_MENU) {
            nt_ui_close(s_fx.ctx, MODAL_ID);
        }
        if (nt_ui_modal_begin(s_fx.ctx, MODAL_ID, &ms, NULL)) {
            menu_im_tree(st, style);
            nt_ui_modal_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
}

static void menu_im_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) { menu_im_frame_ptr(st, style, &(nt_pointer_t){.x = px, .y = py, .active = true}); }

/* A pointer at (x, y) carrying the edge of `press` and of `release` (a button, or -1): a press holds its button
 * down, a tap carries both edges. */
static nt_pointer_t menu_ptr(float x, float y, int press, int release) {
    nt_pointer_t p = {.x = x, .y = y, .active = true};
    for (int b = 0; b < NT_BUTTON_MAX; ++b) {
        p.buttons[b] = (nt_button_state_t){.is_down = b == press && b != release, .is_pressed = b == press, .is_released = b == release};
    }
    return p;
}

/* A frame whose pointer presses the left button at (px,py). */
static void menu_im_press(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    const nt_pointer_t p = menu_ptr(px, py, NT_BUTTON_LEFT, -1);
    menu_im_frame_ptr(st, style, &p);
}

/* ---- Outside press: a left click, a tap or a right click far outside every open panel closes the whole
 *      chain before the rows declare, so the press frame declares no level. ---- */
static void test_menu_outside_press_dismisses_chain(void) {
    const uint8_t types[] = {NT_POINTER_MOUSE, NT_POINTER_TOUCH, NT_POINTER_MOUSE};
    const uint8_t buttons[] = {NT_BUTTON_LEFT, NT_BUTTON_LEFT, NT_BUTTON_RIGHT};
    for (int i = 0; i < 3; ++i) {
        nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 700.0F, 500.0F);
        const nt_ui_bbox_t root = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 0U));
        TEST_ASSERT_TRUE_MESSAGE(root.found && ui_fx_float_near(root.x, 120.0F, 0.5F) && ui_fx_float_near(root.y, 80.0F, 0.5F), "the root's top-left sits at the open point");
        nt_pointer_t p = menu_ptr(780.0F, 580.0F, buttons[i], -1);
        p.type = types[i];
        menu_im_frame_ptr(&st, &style, &p);
        TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "an outside press dismisses the whole chain");
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found(0U), "the press frame declares no level");
    }
}

/* Center of a row's prev-frame bbox (0,0 if not yet laid out). */
static void menu_row_center(uint32_t row_id, float *cx, float *cy) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, row_id);
    *cx = bb.found ? (bb.x + (bb.width * 0.5F)) : 0.0F;
    *cy = bb.found ? (bb.y + (bb.height * 0.5F)) : 0.0F;
}

typedef void (*menu_frame_fn)(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py);

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
static void menu_open_file_open(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    menu_im_frame(st, style, 0.0F, 0.0F); /* the menu reads keys from the frame after it opens */
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    for (int i = 0; i < 4; ++i) {
        menu_key(keys[i]);
        menu_im_frame(st, style, 0.0F, 0.0F);
    }
    nt_input_init();
    menu_im_frame(st, style, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(2U), "File > Open must be open");
}

/* Opens the menu with File > Open open, then clicks Tools (a press frame, then a release frame). */
static void menu_click_tools_over_file_open(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    nt_ui_menu_open(st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_open_file_open(st, style);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &cx, &cy);
    menu_im_press(st, style, cx, cy);
    const nt_pointer_t release = menu_ptr(cx, cy, -1, NT_BUTTON_LEFT);
    menu_im_frame_ptr(st, style, &release);
}

/* Right (into the focused open parent) and Enter; true when `leaf` activated. */
static bool menu_right_enter_activates(nt_ui_menu_t *st, nt_ui_menu_style_t *style, uint32_t leaf) {
    s_act_capture_id = leaf;
    s_act_hit = false;
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(st, style, 0.0F, 0.0F);
    menu_key(NT_KEY_ENTER);
    menu_im_frame(st, style, 0.0F, 0.0F);
    return s_act_hit;
}

/* ---- Clicking Tools while File > Open is open replaces the whole branch: no level 2 remains, and the
 *      keyboard acts on the Tools submenu. ---- */
static void test_menu_click_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_click_tools_over_file_open(&st, &style); /* two frames over Tools keep the corridor: only the click switches */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "clicking a root parent must NOT dismiss the chain");
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(menu_row(MENU_A, KEY_TOOLS), &cx, &cy);
    menu_im_frame(&st, &style, cx, cy); /* the click switched after the rows declared: this frame shows it */
    TEST_ASSERT_TRUE(menu_row_shown(menu_row(menu_row(MENU_A, KEY_TOOLS), KEY_OPT)));
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(2U), "switching the root branch closes File > Open");

    /* The click left focus on Tools: Right enters its submenu, Enter activates Opt. */
    const uint32_t opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    TEST_ASSERT_TRUE_MESSAGE(menu_right_enter_activates(&st, &style, opt), "Right + Enter must activate Tools > Opt");
}

/* ---- Hovering Tools while File > Open is open replaces the whole branch once the corridor releases. ---- */
static void test_menu_hover_root_parent_switches_branch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_open_file_open(&st, &style);

    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &cx, &cy);
    const uint32_t opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    const uint32_t tools = nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS);
    for (int f = 0; f < 30 && !menu_row_shown(opt); ++f) {
        menu_im_frame(&st, &style, cx, cy);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_row_shown(opt), "hovering Tools must open its submenu after the corridor releases");
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(2U), "switching the root branch closes File > Open with File's level");
    TEST_ASSERT_EQUAL_UINT32(tools, st.focus);
    TEST_ASSERT_TRUE_MESSAGE(menu_right_enter_activates(&st, &style, opt), "Right + Enter must activate Tools > Opt");
}

/* ---- Collapsing File by hovering the Edit leaf forgets File > Open: hovering File again opens only
 *      File's own submenu. ---- */
static void test_menu_hover_collapse_reopen_forgets_descendants(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_open_file_open(&st, &style);

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
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
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

/* A keyless frame (an opened menu reads keys from the frame after its first declaration), then one frame per
 * key; the keys are released after the last. */
static void menu_dyn_keys(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const menu_dyn_tree_t *t, const nt_key_t *keys, int count) {
    menu_dyn_frame(st, style, t);
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_dyn_keys(&st, &style, &full, focus_l2, 2);
    menu_dyn_frame(&st, &style, &lost);
    TEST_ASSERT_FALSE_MESSAGE(menu_dyn_enter_activates(&st, &style, &lost, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "Enter on a disabled focused row does nothing");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    const nt_key_t up = NT_KEY_ARROW_UP;
    menu_dyn_keys(&st, &style, &t, &up, 1);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L3)), "Up from no focus lands on the last row");
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, downs, 4);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Down past the last row wraps to the first");
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &mid_off, downs, 2);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &mid_off, nt_ui_menu_test_item_id(MENU_A, KEY_L3)), "Down from L1 skips the disabled L2");
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_dyn_keys(&st, &style, &first_off, downs, 1);
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &first_off, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "Down from no focus starts at the first enabled row");
}

/* ---- Removing the open parent, disabling it or turning it into a leaf closes its submenu by itself, and the
 *      keyboard then navigates the root; Esc in that frame closes the submenu level only. ---- */
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
    for (int c = 0; c < 6; ++c) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_dyn_keys(&st, &style, &open_tree, open_p2, 3);
        TEST_ASSERT_TRUE(menu_level_found(1U));
        if (c >= 3) {
            menu_key(NT_KEY_ESCAPE);
        }
        menu_dyn_frame(&st, &style, &lost[c % 3]);
        nt_input_init();
        menu_dyn_frame(&st, &style, &lost[c % 3]);
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "the menu stays open");
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "a lost open parent closes its branch");
        menu_dyn_keys(&st, &style, &lost[c % 3], open_first, 2);
        TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &lost[c % 3], p1_opt), "the keyboard must open P1 and activate P1 > Opt");
    }
}

/* ---- Right on a focused parent opens its child in the key's frame, focused on its first enabled row, as Dear
 *      ImGui's BeginMenu; Right on a leaf opens nothing. ---- */
static void test_menu_kbd_open_focuses_first_enabled_row(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    static const uint32_t keys[] = {KEY_P1, KEY_L1};
    const menu_dyn_tree_t t = {.keys = keys, .count = 2, .disabled_first_child = true};
    const nt_key_t down = NT_KEY_ARROW_DOWN;
    menu_dyn_keys(&st, &style, &t, &down, 1);
    TEST_ASSERT_FALSE(menu_level_found(1U));
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_dyn_frame(&st, &style, &t);
    nt_input_init();
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "P1's child declares in the frame Right is pressed");
    const uint32_t p1_opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_P1), KEY_OPT);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(p1_opt, st.focus, "its first enabled row has the focus after that frame");
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, p1_opt), "Enter must activate the first enabled child");

    const uint32_t l1 = nt_ui_menu_test_item_id(MENU_A, KEY_L1);
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    const nt_key_t up = NT_KEY_ARROW_UP; /* from no focus to the last row, the L1 leaf */
    menu_dyn_keys(&st, &style, &t, &up, 1);
    TEST_ASSERT_EQUAL_UINT32(l1, st.focus);
    s_act_capture_id = l1;
    s_act_hit = false;
    const nt_key_t right = NT_KEY_ARROW_RIGHT;
    menu_dyn_keys(&st, &style, &t, &right, 1);
    menu_dyn_frame(&st, &style, &t);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "Right on a leaf opens no level");
    TEST_ASSERT_EQUAL_UINT32(l1, st.focus);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "nor activates the leaf");
}

/* ---- Right opens an empty submenu and focus stays on its parent; Down then moves on in the parent's
 *      level and closes the empty submenu. ---- */
static void test_menu_kbd_empty_submenu(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    static const uint32_t keys[] = {KEY_P1, KEY_L1};
    const menu_dyn_tree_t t = {.keys = keys, .count = 2, .empty_children = true};
    const nt_key_t open_p1[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT};
    menu_dyn_keys(&st, &style, &t, open_p1, 2);
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "Right opens the empty submenu");
    const nt_key_t down = NT_KEY_ARROW_DOWN;
    menu_dyn_keys(&st, &style, &t, &down, 1);
    menu_dyn_frame(&st, &style, &t); /* Down steps after the rows declare */
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "Down closes the empty submenu");
    TEST_ASSERT_TRUE_MESSAGE(menu_dyn_enter_activates(&st, &style, &t, nt_ui_menu_test_item_id(MENU_A, KEY_L1)), "Down moves on in the parent's level");
}

/* ---- Depth cap: a self-referential immediate tree (a submenu_begin that recurses on itself) nests one
 *      level per keyboard open; a parent declared on the deepest level trips the depth NT_ASSERT before the
 *      popup stack is exhausted. ---- */
static void menu_im_self_ref(nt_ui_menu_t *st, nt_ui_menu_style_t *style, bool deepest_disabled) {
    menu_frame_begin(&(nt_pointer_t){.active = true});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
        int guard = 0; /* the depth of the level being declared */
        while (guard < NT_UI_MENU_MAX_DEPTH + 4 && nt_ui_menu_submenu_begin_ex(st, KEY_FILE, "loop", (nt_ui_menu_item_opts_t){.disabled = deepest_disabled && guard == NT_UI_MENU_MAX_DEPTH - 1})) {
            ++guard;
        }
        for (int i = 0; i < guard; ++i) {
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}
/* Opens one level per Right until the deepest level's parent trips the depth assert. */
static void menu_depth_cap_trips(bool deepest_disabled) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_self_ref(&st, &style, deepest_disabled); /* the menu reads keys from the frame after it opens */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_self_ref(&st, &style, deepest_disabled);

    volatile int opened = 0; /* read after longjmp */
    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        for (; opened < NT_UI_MENU_MAX_DEPTH + 2; ++opened) {
            menu_key(NT_KEY_ARROW_RIGHT);
            menu_im_self_ref(&st, &style, deepest_disabled);
        }
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    TEST_ASSERT_TRUE_MESSAGE(tripped, "a parent declared on the deepest level must trip the depth-cap NT_ASSERT");
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_UI_MENU_MAX_DEPTH"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_MENU_MAX_DEPTH - 2, opened, "every allowed level opens before the cap trips");
}
static void test_menu_depth_cap_asserts(void) { menu_depth_cap_trips(false); }
/* A disabled parent never opens, yet one declared on the deepest level asserts too (one per test: a trip leaves
 * the UI frame open). */
static void test_menu_depth_cap_asserts_on_a_disabled_parent(void) { menu_depth_cap_trips(true); }

/* ============================ row cells / separators ============================ */

/* An icon ref carries a non-zero atlas.id so the gutter draws an image cell (no real atlas binds in the
 * fixture, so the resolve no-ops, but the gutter cell itself lays out + the label aligns). */
static const nt_atlas_region_ref_t s_icon_ref = {.atlas = {.id = 1U}, .region = NT_ATLAS_INVALID_REGION};

/* One frame of the row-cell vitrine: Iconed (icon, shortcut, checkmark), a separator, Plain, a section header,
 * Bottom, Parent > ChildA, and Off (a disabled parent with an icon). */
static void menu_im_frame_rows(nt_ui_menu_t *st, nt_ui_menu_style_t *style, const nt_pointer_t *p) {
    menu_frame_begin(p);
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
        (void)nt_ui_menu_item_ex(st, KEY_NEW, "Iconed", (nt_ui_menu_item_opts_t){.icon = s_icon_ref, .shortcut = "Ctrl+S", .selected = true});
        nt_ui_menu_separator(st);
        act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT), nt_ui_menu_item_ex(st, KEY_QUIT, "Plain", (nt_ui_menu_item_opts_t){0}));
        nt_ui_menu_separator_text(st, "Section");
        act_capture(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), nt_ui_menu_item(st, KEY_EDIT, "Bottom"));
        if (nt_ui_menu_submenu_begin(st, KEY_OPEN, "Parent")) {
            (void)nt_ui_menu_item(st, KEY_PROJECT, "ChildA");
            nt_ui_menu_submenu_end(st);
        }
        if (nt_ui_menu_submenu_begin_ex(st, KEY_FILE, "Off", (nt_ui_menu_item_opts_t){.icon = s_icon_ref, .disabled = true})) {
            (void)nt_ui_menu_item(st, KEY_PROJECT, "Child");
            nt_ui_menu_submenu_end(st);
        }
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* Opens the vitrine and lays it out twice, so every cell bbox exists. */
static void menu_rows_settle(nt_ui_menu_t *st, nt_ui_menu_style_t *style) {
    nt_ui_menu_open(st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame_rows(st, style, &(nt_pointer_t){.active = true});
    menu_im_frame_rows(st, style, &(nt_pointer_t){.active = true});
}

/* ---- Icon gutter: with icon_size > 0 the iconed AND non-iconed rows both reserve the leading gutter,
 *      so their labels start at the SAME x. ---- */
static void test_menu_icon_gutter_aligns_labels(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_t st = {0};
    menu_rows_settle(&st, &style);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    const nt_ui_bbox_t g2 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT)));
    TEST_ASSERT_TRUE_MESSAGE(g0.found, "iconed row gutter cell must lay out");
    TEST_ASSERT_TRUE_MESSAGE(g2.found, "non-iconed row must STILL reserve an aligned-empty gutter");
    TEST_ASSERT_TRUE_MESSAGE(ui_fx_float_near(g0.x, g2.x, 0.5F), "both gutters must align (icon-column model)");
    TEST_ASSERT_TRUE_MESSAGE(ui_fx_float_near(g0.width, (float)style.icon_size, 0.5F), "gutter width == icon_size");
}

/* ---- icon_size == 0: no gutter at all (the cell is not declared). ---- */
static void test_menu_icon_gutter_absent_when_zero(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 0U;
    nt_ui_menu_t st = {0};
    menu_rows_settle(&st, &style);
    const nt_ui_bbox_t g0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    TEST_ASSERT_FALSE_MESSAGE(g0.found, "icon_size 0 must declare no gutter cell");
}

/* ---- A separator and a separator_text section header are non-interactive: keyboard Down skips each
 *      (Iconed -> Plain, then Plain -> Bottom). ---- */
static void test_menu_separators_non_interactive(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const uint32_t landing[] = {KEY_QUIT, KEY_EDIT};
    for (int c = 0; c < 2; ++c) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame_rows(&st, &style, &(nt_pointer_t){.active = true});
        s_act_capture_id = nt_ui_menu_test_item_id(MENU_A, landing[c]);
        s_act_hit = false;
        for (int i = 0; i < c + 3; ++i) {
            menu_key(i < c + 2 ? NT_KEY_ARROW_DOWN : NT_KEY_ENTER);
            menu_im_frame_rows(&st, &style, &(nt_pointer_t){.active = true});
        }
        TEST_ASSERT_TRUE_MESSAGE(s_act_hit, c == 0 ? "Down must skip the separator (Iconed -> Plain)" : "Down must skip the section header (Plain -> Bottom)");
    }
}

/* ---- The pointer over a separator takes no focus and starts no switch: Parent's submenu, hover-opened,
 *      stays open and focused past the corridor's twice AIM_FALLBACK. ---- */
static void test_menu_pointer_on_a_separator_is_inert(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_rows_settle(&st, &style);
    const uint32_t parent = nt_ui_menu_test_item_id(MENU_A, KEY_OPEN);
    float px = 0.0F;
    float py = 0.0F;
    menu_row_center(parent, &px, &py);
    for (int f = 0; f < 3 && !menu_level_found(1U); ++f) {
        menu_im_frame_rows(&st, &style, &(nt_pointer_t){.x = px, .y = py, .active = true});
    }
    TEST_ASSERT_TRUE(menu_level_found(1U));
    const nt_ui_bbox_t above = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_item_id(MENU_A, KEY_NEW));
    const nt_ui_bbox_t below = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_item_id(MENU_A, KEY_QUIT));
    const nt_pointer_t on_separator = {.x = px, .y = (above.y + above.height + below.y) * 0.5F, .active = true};
    for (int f = 0; f < (2 * AIM_FALLBACK_FRAMES) + 2; ++f) {
        menu_im_frame_rows(&st, &style, &on_separator);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(parent, st.focus, "a separator takes no focus");
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "a separator starts no switch");
}

/* ---- Submenu marker cell: a parent row declares the marker cell (arrow sprite or ">" fallback); a leaf
 *      row never does. ---- */
static void test_menu_arrow_marker_on_parent_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_rows_settle(&st, &style);
    const nt_ui_bbox_t parent_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(nt_ui_menu_test_item_id(MENU_A, KEY_OPEN))); /* Parent */
    const nt_ui_bbox_t leaf_arrow = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_arrow_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));    /* Iconed leaf */
    TEST_ASSERT_TRUE_MESSAGE(parent_arrow.found, "a parent row must declare the submenu marker cell");
    TEST_ASSERT_FALSE_MESSAGE(leaf_arrow.found, "a leaf row must NOT declare a submenu marker");
}

/* ---- A row with a shortcut declares the shortcut cell (own fmix id); a plain row does NOT. ---- */
static void test_menu_shortcut_cell_on_rich_row_only(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_rows_settle(&st, &style);
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
    menu_rows_settle(&st, &style);
    const nt_ui_bbox_t ck = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(nt_ui_menu_test_item_id(MENU_A, KEY_NEW)));
    const nt_ui_bbox_t unchecked = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_check_id(nt_ui_menu_test_item_id(MENU_A, KEY_QUIT)));
    TEST_ASSERT_TRUE_MESSAGE(ck.found, "a selected row must declare the checkmark cell");
    TEST_ASSERT_FALSE_MESSAGE(unchecked.found, "an unselected row must NOT declare a checkmark cell");
}

/* ---- submenu_begin_ex honors opts: the parent declares its icon gutter cell, and a disabled parent opens
 *      neither by hover nor by click. ---- */
static void test_menu_submenu_begin_ex_icon_disabled(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.icon_size = 20U;
    nt_ui_menu_t st = {0};
    menu_rows_settle(&st, &style);
    const uint32_t parent = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_icon_id(parent)).found, "submenu_begin_ex must declare the parent's icon gutter cell");

    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(parent, &cx, &cy);
    const nt_pointer_t frames[] = {menu_ptr(cx, cy, -1, -1), menu_ptr(cx, cy, -1, -1), menu_ptr(cx, cy, NT_BUTTON_LEFT, -1), menu_ptr(cx, cy, -1, NT_BUTTON_LEFT)};
    for (int f = 0; f < 4; ++f) {
        menu_im_frame_rows(&st, &style, &frames[f]);
    }
    menu_im_frame_rows(&st, &style, &(nt_pointer_t){.active = true});
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_EQUAL_UINT32(0U, st.focus);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "a disabled parent opens neither by hover nor by click");
}

/* ---- Esc or Left at the root closes the menu, and nt_ui_menu_begin declares nothing in that frame. ---- */
static void test_menu_left_at_root_closes(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const nt_key_t keys[] = {NT_KEY_ESCAPE, NT_KEY_ARROW_LEFT};
    for (int k = 0; k < 2; ++k) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        menu_key(keys[k]);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        nt_input_init();
        TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found(0U), "the closing frame declares no root");
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_wants_keyboard(s_fx.ctx), "the key that closed the menu was the menu's, not the game's");
    }
}

/* ---- Esc or Left with a child the pointer opened closes that child first, as Dear ImGui and Win32 close
 *      the top popup; the next one closes the menu. ---- */
static void test_menu_back_closes_a_hover_opened_child(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const nt_key_t keys[] = {NT_KEY_ESCAPE, NT_KEY_ARROW_LEFT};
    for (int k = 0; k < 2; ++k) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        menu_hover_open(&st, &style, file, 1U, menu_im_frame);
        float fx = 0.0F;
        float fy = 0.0F;
        menu_row_center(file, &fx, &fy);
        TEST_ASSERT_EQUAL_UINT32(file, st.focus);
        menu_key(keys[k]);
        menu_im_frame(&st, &style, fx, fy);
        nt_input_init();
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "the first Esc or Left closes only the open child");
        TEST_ASSERT_FALSE(menu_level_found(1U));
        TEST_ASSERT_EQUAL_UINT32(file, st.focus);
        menu_key(keys[k]);
        menu_im_frame(&st, &style, fx, fy);
        nt_input_init();
        TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
    }
}

/* ---- Right or Enter with no focused row is no key: a Down pressed with it lands on the first row. ---- */
static void test_menu_no_focus_right_leaves_down(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const nt_key_t keys[] = {NT_KEY_ENTER, NT_KEY_ARROW_RIGHT};
    for (int k = 0; k < 2; ++k) {
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 790.0F, 590.0F);
        menu_im_frame(&st, &style, 790.0F, 590.0F);
        TEST_ASSERT_EQUAL_UINT32(0U, st.focus);
        nt_input_init();
        nt_input_feed(nt_test_key(keys[k], true));
        nt_input_feed(nt_test_key(NT_KEY_ARROW_DOWN, true));
        nt_input_poll();
        menu_im_frame(&st, &style, 790.0F, 590.0F);
        nt_input_init();
        TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st.focus);
    }
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
        const float on_x = on_child ? (child.x + (child.width * 0.5F)) : (row.x + row.width - 2.0F);
        const float x = found ? on_x : 0.0F;
        const float y = found ? (child.y + (child.height * 0.5F)) : 0.0F;
        const nt_pointer_t p = menu_ptr(x, y, (frame == 2 && found) ? NT_BUTTON_LEFT : -1, (frame == 3 && found) ? NT_BUTTON_LEFT : -1);
        menu_frame_begin(&p);
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, &style)) {
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_row_child_result_t r = menu_row_child_frames(&st, false, true);
    TEST_ASSERT_TRUE(r.btn_clicked);
    TEST_ASSERT_FALSE(r.end_activated);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus);

    r = menu_row_child_frames(&st, true, true);
    TEST_ASSERT_TRUE(r.btn_clicked);
    TEST_ASSERT_FALSE(r.end_activated);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

/* ---- Beside the child a not-activatable row takes hover focus but never captures, activates or
 *      closes (first, on a fresh registry); an activatable row activates and closes the chain. ---- */
static void test_menu_item_begin_press_beside_child(void) {
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_row_child_result_t r = menu_row_child_frames(&st, true, false);
    TEST_ASSERT_FALSE(r.btn_clicked);
    TEST_ASSERT_FALSE(r.end_activated);
    TEST_ASSERT_FALSE(r.row_captured);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus);

    r = menu_row_child_frames(&st, false, false);
    TEST_ASSERT_FALSE(r.btn_clicked);
    TEST_ASSERT_TRUE(r.end_activated);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

/* ---- A drag captured by one row's control does not hover another row: pressing row A's control and
 *      dragging onto row B's control leaves hover focus on A, as every query ignores a pointer captured
 *      elsewhere. ---- */
#define MENU_ROW_B_CHILD_ID 0x1B7702U
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_item_begin_captured_drag_keeps_focus(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    for (int frame = 0; frame < 5; ++frame) {
        const nt_ui_bbox_t ca = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_CHILD_ID);
        const nt_ui_bbox_t cb = nt_ui_get_bbox(s_fx.ctx, MENU_ROW_B_CHILD_ID);
        const nt_ui_bbox_t c = (frame < 3) ? ca : cb; /* press on A's control, then drag onto B's */
        nt_pointer_t p = {.x = c.x + (c.width * 0.5F), .y = c.y + (c.height * 0.5F), .active = true};
        p.buttons[NT_BUTTON_LEFT].is_down = (frame >= 2);
        p.buttons[NT_BUTTON_LEFT].is_pressed = (frame == 2);
        menu_frame_begin(&p);
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, &style)) {
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

/* ---- Enter on the focused custom row activates it through item_end, like a click, and closes the menu; on
 *      a row that is not activatable it does nothing. ---- */
static void test_menu_item_begin_enter_activates(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    const nt_key_t keys[] = {NT_KEY_ARROW_DOWN, NT_KEY_ENTER};
    for (int a = 0; a < 2; ++a) {
        const bool activatable = (a == 0);
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        bool activated = false;
        for (int frame = 0; frame < 3; ++frame) {
            if (frame > 0) {
                menu_key(keys[frame - 1]); /* the menu reads keys from the frame after it opens */
            }
            menu_frame_begin(&(nt_pointer_t){.active = true});
            if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, &style)) {
                nt_ui_menu_item_begin(&st, KEY_NEW, true, activatable);
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(20)}}}) {}
                activated |= nt_ui_menu_item_end(&st);
                nt_ui_menu_end(&st);
            }
            nt_ui_end(s_fx.ctx);
            if (frame == 1 || (frame == 2 && !activatable)) {
                TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_NEW), st.focus); /* Down, then Enter, land on the row */
            }
        }
        TEST_ASSERT_EQUAL_MESSAGE(activatable, activated, "Enter on the focused custom row must make item_end return true exactly when it is activatable");
        TEST_ASSERT_EQUAL(!activatable, nt_ui_is_open(s_fx.ctx, MENU_A));
    }
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
        const nt_pointer_t p = menu_ptr(bx, by, (frame == 2) ? NT_BUTTON_LEFT : -1, (frame == 3) ? NT_BUTTON_LEFT : -1);
        menu_frame_begin(&p);
        if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
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
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        TEST_ASSERT_EQUAL_MESSAGE(enabled, menu_click_row(&st, &style, kinds[k]), "a click activates exactly the enabled rows");
        TEST_ASSERT_EQUAL_MESSAGE(!enabled, nt_ui_is_open(s_fx.ctx, MENU_A), "an activation closes the menu");
    }
}

typedef void (*menu_body_fn)(nt_ui_menu_t *st);

/* Declares `body` in an open menu's frame; true when it trips an NT_ASSERT whose expression holds `msg`. */
static bool menu_body_asserts(menu_body_fn body, const char *msg) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        menu_frame_begin(&(nt_pointer_t){.active = true});
        if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, &style)) {
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

static void menu_body_duplicate_key(nt_ui_menu_t *st) {
    (void)nt_ui_menu_item(st, KEY_NEW, "First");
    (void)nt_ui_menu_item(st, KEY_NEW, "Dup"); /* same key in the same level -> aliased id */
    nt_ui_menu_end(st);
    nt_ui_end(s_fx.ctx);
}

/* ---- A row or a separator inside an open custom row, and a checked parent row, assert (one per test: a
 *      trip leaves the UI frame open). ---- */
static void test_menu_row_in_custom_row_asserts(void) { TEST_ASSERT_TRUE(menu_body_asserts(menu_body_row_in_custom_row, "inside a custom row")); }
static void test_menu_separator_in_custom_row_asserts(void) { TEST_ASSERT_TRUE(menu_body_asserts(menu_body_separator_in_custom_row, "outside a custom row")); }
static void test_menu_checked_parent_asserts(void) { TEST_ASSERT_TRUE(menu_body_asserts(menu_body_checked_parent, "no checkmark")); }

/* ---- Duplicate sibling key: two nt_ui_menu_item calls sharing a key in ONE open level share a row id ->
 *      Clay's duplicate-id error asserts. ---- */
static void test_menu_duplicate_sibling_key_asserts(void) {
    TEST_ASSERT_TRUE_MESSAGE(menu_body_asserts(menu_body_duplicate_key, "Clay"), "two sibling items sharing a key must trip the duplicate-id NT_ASSERT");
}

/* ---- Back on the open parent, leaving it again starts a fresh corridor: the one kept while the pointer
 *      crossed the Edit leaf does not run on. ---- */
static void test_menu_back_on_the_parent_restarts_the_corridor(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    menu_im_frame(&st, &style, fx, fy);
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_TRUE(menu_level_found(1U));

    const int corridor_frames = (3 * AIM_FALLBACK_FRAMES) / 2; /* past AIM_FALLBACK, short of twice it */
    for (int f = 0; f < corridor_frames; ++f) {
        menu_im_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the corridor keeps File's submenu while the pointer is on Edit");
    menu_im_frame(&st, &style, fx, fy);
    for (int f = 0; f < AIM_FALLBACK_FRAMES - 2; ++f) { /* the first corridor, kept stale, would pass twice AIM_FALLBACK */
        menu_im_frame(&st, &style, ex, ey);
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "leaving File again starts a fresh corridor");
}

/* ---- Right-edge flip: a menu opened hard against the RIGHT screen edge must (a) clamp the ROOT
 *      panel back on-screen (BELOW only flips vertically, so a point-anchored root needs a horizontal
 *      clamp), and (b) flip its submenu LEFT so the whole stack stays visible. ---- */
static void test_menu_right_edge_root_clamp_and_submenu_flip(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 780.0F, 100.0F); /* open hard against the right edge (780 + min_width 160 > 800) */

    menu_im_frame(&st, &style, 0.0F, 0.0F); /* F1: lay out root */
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, 0.0F, 0.0F); /* F2: focus File */
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, 0.0F, 0.0F); /* F3: open File submenu */
    nt_input_init();
    menu_im_frame(&st, &style, 0.0F, 0.0F); /* F4-5: settle so widths measure + side resolves */
    menu_im_frame(&st, &style, 0.0F, 0.0F);

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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);

    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_frame(&st, &style, fx, fy);
    const uint32_t file_new = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_NEW);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(file_new, st.focus, "Right enters File's submenu while the pointer rests on File");

    menu_key(NT_KEY_ARROW_LEFT);
    menu_im_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_frame(&st, &style, fx, fy);
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "Left closes File although the pointer rests on it");

    menu_im_frame(&st, &style, fx + 2.0F, fy);
    menu_im_frame(&st, &style, fx + 2.0F, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st.focus, "moving within File takes focus back to the pointer");
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "moving within File hands it the chain again: File reopens");
}

/* ---- After hovering File open, keys act on File's level: Down moves to Tools and closes File's submenu. ---- */
static void test_menu_keys_act_on_the_focused_level(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), &fx, &fy);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);
    menu_key(NT_KEY_ARROW_DOWN);
    menu_im_frame(&st, &style, fx, fy);
    nt_input_init();
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), st.focus, "Down moves to the next root row");
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "focus leaving File closes its submenu");
}

/* ---- A press inside a panel shown last frame is not an outside press, even when that level closed at the
 *      end of last frame: a click on Tools closes File's level after it declared. ---- */
static void test_menu_press_in_just_closed_panel_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_click_tools_over_file_open(&st, &style);
    float nx = 0.0F;
    float ny = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), KEY_QUIT), &nx, &ny);
    menu_im_press(&st, &style, nx, ny);
    TEST_ASSERT_TRUE_MESSAGE(menu_row_shown(menu_row(menu_row(MENU_A, KEY_TOOLS), KEY_OPT)), "the click had switched to Tools: File's level is gone");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "a press on a panel shown last frame must not dismiss the menu");
}

/* ---- A frame activates one row, the first declared: a tap on one row (press and release in one frame)
 *      together with Enter on the other focused row activates only the row declared first. ---- */
static void menu_tap_and_enter(int focus_downs, uint32_t tap_key, bool *l1, bool *l2) {
    *l1 = false;
    *l2 = false;
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    static const uint32_t l12[] = {KEY_L1, KEY_L2};
    const menu_dyn_tree_t t = {.keys = l12, .count = 2};
    const nt_key_t downs[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &t, downs, focus_downs);
    menu_dyn_frame(&st, &style, &t);
    float cx = 0.0F;
    float cy = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, tap_key), &cx, &cy);
    const nt_pointer_t p = menu_ptr(cx, cy, NT_BUTTON_LEFT, NT_BUTTON_LEFT);
    menu_key(NT_KEY_ENTER);
    menu_frame_begin(&p);
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, &style)) {
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
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 780.0F, 100.0F);
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
 * frame. */
static void menu_keys_beside_open_sibling(nt_ui_menu_t *st, const nt_key_t *keys, int count) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_open(st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(st, &style, 0.0F, 0.0F);
    menu_hover_open(st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);
    float tx = 0.0F;
    float ty = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &tx, &ty);
    menu_im_frame(st, &style, tx, ty);
    menu_im_frame(st, &style, tx, ty);
    TEST_ASSERT_TRUE(menu_row_shown(menu_row(menu_row(MENU_A, KEY_FILE), KEY_NEW)));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), st->focus);
    nt_input_init();
    for (int i = 0; i < count; ++i) {
        nt_input_feed(nt_test_key(keys[i], true));
    }
    nt_input_poll(); /* every key fresh in one frame */
    menu_im_frame(st, &style, tx, ty);
    nt_input_init();
}

/* ---- Right next to a sibling's open child opens the focused parent in the same frame and focuses its first
 *      row. A frame acts on one key, so a Down in the same frame does nothing. ---- */
static void test_menu_right_beside_open_sibling_opens_at_once(void) {
    const uint32_t opt = nt_ui_menu_test_item_id(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), KEY_OPT);
    const nt_key_t right_down[] = {NT_KEY_ARROW_RIGHT, NT_KEY_ARROW_DOWN};
    nt_ui_menu_t st = {0};
    menu_keys_beside_open_sibling(&st, right_down, 2);
    TEST_ASSERT_TRUE_MESSAGE(menu_row_shown(opt), "Tools' child declares in the Right frame");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(opt, st.focus, "one key per frame: the Down does not step past the first row");
}

/* ---- The switch the pointer leaves on a sibling runs off the rows too: after Edit takes focus beside File's
 *      open submenu, the pointer leaving the rows, or the window, still collapses it. ---- */
static void test_menu_pending_switch_lands_off_the_rows(void) {
    for (int c = 0; c < 2; ++c) {
        nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
        nt_ui_menu_t st = {0};
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);
        const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
        float ex = 0.0F;
        float ey = 0.0F;
        menu_row_center(edit, &ex, &ey);
        menu_im_frame(&st, &style, ex, ey);
        menu_im_frame(&st, &style, ex, ey);
        TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
        TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the corridor holds File's submenu");
        /* Off the rows and off the corridor, or an inactive pointer left inside the corridor. */
        const nt_pointer_t away = (c == 0) ? (nt_pointer_t){.x = 20.0F, .y = 580.0F, .active = true} : (nt_pointer_t){.x = ex + 1.0F, .y = ey + 1.0F, .active = false};
        int frames = 0;
        while (menu_level_found(1U) && frames < 30) {
            menu_im_frame_ptr(&st, &style, &away);
            ++frames;
        }
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), c == 0 ? "off the rows the switch still lands" : "outside the window the switch still lands");
        TEST_ASSERT_TRUE_MESSAGE(frames <= AIM_FALLBACK_FRAMES + 1, "the switch lands after AIM_FALLBACK, not the corridor's 2x");
        TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    }
}

/* The alpha of a row's highlight fill last frame (0 = unlit). */
static float menu_row_lit(uint32_t row_id) {
    const Clay_RenderCommand *c = ui_fx_find_rect(s_fx.ctx, row_id);
    return c != NULL ? c->renderData.rectangle.backgroundColor.a : 0.0F;
}

/* ---- One highlighted row per level: the focused row on its level, the open parent on every other level. ---- */
static void test_menu_one_highlight_per_level(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.state_speed = 0.0F; /* highlights snap */
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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

    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_frame(&st, &style, ex, ey);
    menu_im_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    const nt_ui_bbox_t sub = nt_ui_get_bbox(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 1U));
    for (int f = 0; f < 30; ++f) {
        menu_im_frame(&st, &style, sub.x + 2.0F, sub.y + 2.0F); /* the child panel's padding */
    }
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the pointer over the open child holds it");
}

/* ---- The opening frame takes no input. A reopen forgets the chain, and its frame takes neither the hover at the
 *      old place nor an Esc (the reopened menu reads keys from its next frame). A release over a row's old rect
 *      after a reopen elsewhere, the release of the press that reopened the menu over a row, and a tap in the
 *      frame the menu reopens at the same place click nothing (the same tap a frame later does); a reopening
 *      press outside the menu, with either button, does not dismiss it. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_opening_frame_takes_no_input(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    menu_hover_open(&st, &style, file, 1U, menu_im_frame);
    float fx = 0.0F;
    float fy = 0.0F;
    menu_row_center(file, &fx, &fy);
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 400.0F, 300.0F);
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "the reopened menu reads keys from its next frame");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, st.focus, "the opening frame takes no focus from the old place");
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "a reopened menu starts at the root");
    nt_input_init();
    menu_im_frame(&st, &style, fx, fy);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_found(1U), "the opening frame opens nothing from the old place");
    menu_key(NT_KEY_ESCAPE);
    menu_im_frame(&st, &style, fx, fy);
    nt_input_init();
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));

    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    s_act_capture_id = edit;
    static const char *const msgs[] = {"the release at the old place activates nothing", "the reopening press is not a click on the row under it", "the frame the menu reopens takes no tap"};
    for (int c = 0; c < 3; ++c) {
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        float ex = 0.0F;
        float ey = 0.0F;
        menu_row_center(edit, &ex, &ey);
        menu_im_frame(&st, &style, ex, ey);
        const nt_pointer_t tap = menu_ptr(ex, ey, NT_BUTTON_LEFT, NT_BUTTON_LEFT);
        const nt_pointer_t release = menu_ptr(ex, ey, -1, NT_BUTTON_LEFT);
        if (c == 0) { /* pressed on Edit, released on the frame the menu reopens elsewhere */
            menu_im_press(&st, &style, ex, ey);
            nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 400.0F, 300.0F);
        } else { /* the game reopens the menu at the same place with this press, or with a tap */
            nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        }
        if (c == 1) {
            menu_im_press(&st, &style, ex, ey);
        }
        menu_im_frame_ptr(&st, &style, (c == 2) ? &tap : &release);
        TEST_ASSERT_FALSE_MESSAGE(s_act_hit, msgs[c]);
        TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
        if (c == 2) {
            menu_im_frame_ptr(&st, &style, &tap);
            TEST_ASSERT_TRUE_MESSAGE(s_act_hit, "the same tap a frame later activates the row");
        }
    }
    const nt_button_t buttons[] = {NT_BUTTON_LEFT, NT_BUTTON_RIGHT};
    for (int b = 0; b < 2; ++b) {
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        menu_im_frame(&st, &style, 0.0F, 0.0F);
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 400.0F, 300.0F);
        const nt_pointer_t p = menu_ptr(780.0F, 580.0F, (int)buttons[b], -1);
        menu_im_frame_ptr(&st, &style, &p);
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "the reopening press is not an outside press");
        menu_im_frame(&st, &style, 780.0F, 580.0F);
    }
}

/* ---- The outside press belongs to the pointer that pressed: a second finger resting on the menu does not
 *      keep it open. ---- */
static void test_menu_outside_press_ignores_other_pointers(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_EDIT), &ex, &ey);
    nt_pointer_t two[2] = {menu_ptr(780.0F, 580.0F, NT_BUTTON_LEFT, -1), menu_ptr(ex, ey, -1, -1)};
    two[0].type = NT_POINTER_TOUCH;
    two[1].type = NT_POINTER_TOUCH;
    nt_arena_reset(&g_nt_mem_scratch);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = VIEW_W, .layout_h = VIEW_H, .dt = UI_FX_DT, .pointers = two, .pointer_count = 2, .primary = 0});
    if (nt_ui_menu_begin(&st, s_fx.ctx, NULL, 0U, &style)) {
        (void)nt_ui_menu_item(&st, KEY_EDIT, "Edit");
        nt_ui_menu_end(&st);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "the primary pointer's outside press dismisses the menu");
}

/* ---- A child opened last frame has no rect yet: the pointer flicking on to a sibling keeps it (the
 *      corridor decides from the next frame). ---- */
static void test_menu_fresh_child_survives_a_flick(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    float fx = 0.0F;
    float fy = 0.0F;
    float tx = 0.0F;
    float ty = 0.0F;
    menu_row_center(file, &fx, &fy);
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_TOOLS), &tx, &ty);
    menu_im_frame(&st, &style, fx, fy); /* the hover opens File's child after the rows declare */
    menu_im_frame(&st, &style, tx, ty); /* the flick: the child has no rect yet */
    menu_im_frame(&st, &style, tx, ty);
    TEST_ASSERT_TRUE_MESSAGE(menu_row_shown(menu_row(file, KEY_NEW)), "File's child, opened the frame before, survives the flick");
    TEST_ASSERT_FALSE(menu_row_shown(menu_row(menu_row(MENU_A, KEY_TOOLS), KEY_OPT)));
}

/* ---- A key ends a pending switch: Right on a leaf beside a held sibling's child closes that child. ---- */
static void test_menu_key_ends_pending_switch(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    menu_hover_open(&st, &style, nt_ui_menu_test_item_id(MENU_A, KEY_FILE), 1U, menu_im_frame);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_frame(&st, &style, ex, ey);
    menu_im_frame(&st, &style, ex, ey);
    TEST_ASSERT_TRUE(menu_level_found(1U));
    menu_key(NT_KEY_ARROW_RIGHT);
    menu_im_frame(&st, &style, ex, ey);
    nt_input_init();
    menu_im_frame(&st, &style, ex, ey);
    TEST_ASSERT_FALSE(menu_level_found(1U));
    TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

#define ROW_POPUP_ID 0x9091U
#define ROW_POPUP_BOX 0x9092U

/* A custom row L1 that opens a popup, as a combo list, once s_popup_depth names its menu depth, and declares it. */
static void menu_popup_row(nt_ui_menu_t *st, int depth) {
    nt_ui_menu_item_begin(st, KEY_L1, true, false);
    if (s_popup_depth == depth) {
        nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
        ps.ease_speed = 0.0F;
        const nt_ui_popup_anchor_t anchor = {.x = 600.0F, .y = 450.0F, .prefer_side = NT_UI_POPUP_BELOW};
        if (!s_popup_opened) {
            s_popup_opened = true;
            nt_ui_popup_open(s_fx.ctx, ROW_POPUP_ID);
        }
        if (nt_ui_popup_begin(s_fx.ctx, ROW_POPUP_ID, &ps, &anchor, NULL)) {
            CLAY({.id = (Clay_ElementId){.id = ROW_POPUP_BOX}, .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(40)}}}){}(void)nt_ui_step(s_fx.ctx, ROW_POPUP_BOX, NULL);
            nt_ui_popup_end(s_fx.ctx);
        }
    }
    (void)nt_ui_menu_item_end(st);
}

/* One frame: root = [L1, File > (L1, Open > (Project))]; either L1 may hold the popup. */
static void menu_popup_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, float px, float py) {
    menu_frame_begin(&(nt_pointer_t){.x = px, .y = py, .active = true});
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
        menu_popup_row(st, 0);
        if (nt_ui_menu_submenu_begin(st, KEY_FILE, "File")) {
            menu_popup_row(st, 1);
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

/* ---- A popup a row opens closes the level beside it (S2). A root row's popup closes File's open level at once,
 *      and the pointer resting on File does not open it again under the popup in that frame: the menu's pointer
 *      waits while something opened this frame lies over it. From the next frame the popup is the top. A level-1
 *      row's popup closes only the branch below that row: File stays open, Open closes. ---- */
static void test_menu_row_popup_takes_the_pointer_in_its_frame(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t open = nt_ui_menu_test_item_id(file, KEY_OPEN);
    for (int depth = 0; depth < 2; ++depth) {
        s_popup_depth = -1;
        s_popup_opened = false;
        nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        menu_popup_frame(&st, &style, 0.0F, 0.0F);
        menu_popup_frame(&st, &style, 0.0F, 0.0F);
        menu_hover_open(&st, &style, file, 1U, menu_popup_frame);
        if (depth == 1) {
            menu_hover_open(&st, &style, open, 2U, menu_popup_frame);
        }
        float px = 0.0F;
        float py = 0.0F;
        menu_row_center((depth == 0) ? file : open, &px, &py);
        menu_popup_frame(&st, &style, px, py);
        s_popup_depth = depth; /* L1, declared before File or Open, opens its popup */
        menu_popup_frame(&st, &style, px, py);
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found((uint8_t)(depth + 1)), "the frame the popup opens declares no level over it");
        menu_popup_frame(&st, &style, px, py);
        TEST_ASSERT_FALSE_MESSAGE(menu_level_found((uint8_t)(depth + 1)), "the hover did not reopen the level under the popup");
        TEST_ASSERT_TRUE_MESSAGE(depth == 0 || menu_level_found(1U), "a level-1 row's popup keeps its own level");
    }
    menu_key(NT_KEY_ESCAPE);
    menu_popup_frame(&st, &style, 0.0F, 0.0F);
    nt_input_init();
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, ROW_POPUP_ID), "the popup is the top: Esc closes it");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

/* A press of `button` without movement after a key: focus returns to the row under the pointer. */
static void menu_still_press_ends_keyboard_mode(nt_button_t button) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    float ex = 0.0F;
    float ey = 0.0F;
    menu_row_center(edit, &ex, &ey);
    menu_im_frame(&st, &style, ex, ey);
    menu_im_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(edit, st.focus);
    menu_key(NT_KEY_ARROW_DOWN); /* wraps to File */
    menu_im_frame(&st, &style, ex, ey);
    nt_input_init();
    menu_im_frame(&st, &style, ex, ey);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_FILE), st.focus);
    const nt_pointer_t p = menu_ptr(ex, ey, (int)button, -1);
    menu_im_frame_ptr(&st, &style, &p);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(edit, st.focus, "a still press hands focus back to the pointer");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

static void test_menu_still_press_ends_keyboard_mode(void) { menu_still_press_ends_keyboard_mode(NT_BUTTON_LEFT); }
static void test_menu_still_right_press_ends_keyboard_mode(void) { menu_still_press_ends_keyboard_mode(NT_BUTTON_RIGHT); }

/* ---- A popup a File row opens covers the menu without closing it (it is declared inside) and stops its
 *      pending switch: the root row the pointer rests on does not take the branch while the popup is up. ---- */
static void test_menu_covered_switch_waits(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_popup_frame(&st, &style, 0.0F, 0.0F);
    menu_popup_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    menu_hover_open(&st, &style, file, 1U, menu_popup_frame);
    float lx = 0.0F;
    float ly = 0.0F;
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_L1), &lx, &ly);
    menu_popup_frame(&st, &style, lx, ly); /* the switch to L1 is pending: File's child stays */
    TEST_ASSERT_TRUE(menu_level_found(1U));
    s_popup_depth = 1;
    for (int f = 0; f < (int)(4.0F * NT_UI_MENU_AIM_FALLBACK_SECS / UI_FX_DT); ++f) {
        menu_popup_frame(&st, &style, lx, ly);
    }
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_TRUE_MESSAGE(menu_level_found(1U), "the covered menu's switch waits for the popup");
}

/* ---- A right press while the left drags across the rows joins the left's gesture: it is no press, so neither over
 *      a row nor off the menu does it close the menu. ---- */
static void test_menu_right_press_during_a_drag_keeps_open(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "a press over a row is not an outside press, whatever the left holds");
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = false, .is_released = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.x = 780.0F;
    p.y = 580.0F;
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){0};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    menu_dyn_frame_ptr(&st, &style, &t, &p);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "a joined press off the menu is no outside press");
}

/* ---- Right and Esc in one frame: the frame acts on one key, and Esc comes first, so the menu closes. ---- */
static void test_menu_right_and_esc_in_one_frame(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    static const uint32_t p1[] = {KEY_P1};
    const menu_dyn_tree_t t = {.keys = p1, .count = 1};
    const nt_key_t down = NT_KEY_ARROW_DOWN;
    menu_dyn_keys(&st, &style, &t, &down, 1);
    nt_input_init();
    nt_input_feed(nt_test_key(NT_KEY_ARROW_RIGHT, true));
    nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    nt_input_poll();
    menu_dyn_frame(&st, &style, &t);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "Esc at the root closes the menu; the Right does nothing");
}

/* ---- A focused row removed for one frame loses focus: when it returns, Enter does not activate it. ---- */
static void test_menu_returned_row_is_not_focused(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t l123[] = {KEY_L1, KEY_L2, KEY_L3};
    static const uint32_t l13[] = {KEY_L1, KEY_L3};
    const menu_dyn_tree_t full = {.keys = l123, .count = 3};
    const menu_dyn_tree_t lost = {.keys = l13, .count = 2};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    const nt_key_t focus_l2[] = {NT_KEY_ARROW_DOWN, NT_KEY_ARROW_DOWN};
    menu_dyn_keys(&st, &style, &full, focus_l2, 2);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_menu_test_item_id(MENU_A, KEY_L2), st.focus);
    menu_dyn_frame(&st, &style, &lost);
    menu_dyn_frame(&st, &style, &full);
    TEST_ASSERT_FALSE_MESSAGE(menu_dyn_enter_activates(&st, &style, &full, nt_ui_menu_test_item_id(MENU_A, KEY_L2)), "a removed row loses focus");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

/* ---- A tap on one parent and Right on a later focused parent in one frame, with a third parent's child
 *      pending: the tap opens its parent. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_menu_click_beats_a_same_frame_key(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    static const uint32_t parents[] = {KEY_PARENT, KEY_PARENT + 1U, KEY_PARENT + 2U};
    const menu_dyn_tree_t t = {.keys = parents, .count = 3};
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_dyn_frame(&st, &style, &t);
    menu_dyn_frame(&st, &style, &t);
    const uint32_t b = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT);
    const uint32_t c = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT + 1U);
    const uint32_t a = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT + 2U);
    float x = 0.0F;
    float y = 0.0F;
    menu_row_center(c, &x, &y);
    for (int f = 0; f < 3 && !menu_row_shown(menu_row(c, KEY_OPT)); ++f) {
        menu_dyn_frame_ptr(&st, &style, &t, &(nt_pointer_t){.x = x, .y = y, .active = true});
    }
    TEST_ASSERT_TRUE(menu_row_shown(menu_row(c, KEY_OPT)));
    menu_row_center(a, &x, &y);
    menu_dyn_frame_ptr(&st, &style, &t, &(nt_pointer_t){.x = x, .y = y, .active = true}); /* focus A, C's child pending */
    TEST_ASSERT_EQUAL_UINT32(a, st.focus);
    TEST_ASSERT_TRUE(menu_row_shown(menu_row(c, KEY_OPT)));
    menu_row_center(b, &x, &y);
    nt_pointer_t tap = menu_ptr(x, y, NT_BUTTON_LEFT, NT_BUTTON_LEFT);
    tap.type = NT_POINTER_TOUCH;
    menu_key(NT_KEY_ARROW_RIGHT); /* on A */
    menu_dyn_frame_ptr(&st, &style, &t, &tap);
    nt_input_init();
    menu_dyn_frame(&st, &style, &t); /* the click acted after the rows declared */
    TEST_ASSERT_TRUE_MESSAGE(menu_row_shown(menu_row(b, KEY_OPT)), "the tap on B opens B over the Right on A");
    TEST_ASSERT_FALSE(menu_row_shown(menu_row(a, KEY_OPT)));
}

/* ---- Absence: a level opened the frame before shows fully (levels snap), rows with an instant highlight
 *      (state_speed 0) take no anim slot, and neither the open menu nor a popup, snap or eased, keeps a state-pool
 *      cell. ---- */
static void test_menu_and_popup_keep_no_state(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    style.state_speed = 0.0F;
    nt_ui_menu_t st = {0};
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_open_file_open(&st, &style);
    TEST_ASSERT_TRUE_MESSAGE(ui_fx_float_near(ui_fx_rendered_opacity(s_fx.ctx, nt_ui_menu_test_panel_id(MENU_A, 2U)), 1.0F, 0.0001F), "a level takes no open tween");
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
    const uint32_t file = nt_ui_menu_test_item_id(MENU_A, KEY_FILE);
    const uint32_t edit = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    for (uint32_t i = 0; i < NT_UI_ANIM_SLOTS; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(s_fx.ctx->anim[i].id != file && s_fx.ctx->anim[i].id != edit, "an instant row holds no anim slot");
    }
    nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
    ps.ease_speed = 8.0F;
    const nt_ui_popup_anchor_t anchor = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    for (int f = 0; f < 2; ++f) {
        if (f == 0) {
            nt_ui_popup_open(s_fx.ctx, ROW_POPUP_ID);
        } else {
            nt_ui_close(s_fx.ctx, ROW_POPUP_ID);
        }
        menu_frame_begin(&(nt_pointer_t){.active = true});
        if (nt_ui_popup_begin(s_fx.ctx, ROW_POPUP_ID, &ps, &anchor, NULL)) {
            nt_ui_popup_end(s_fx.ctx);
        }
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, nt_ui_state_used_slots(s_fx.ctx), "an eased popup keeps no state cell");
}

/* ---- A menu in a modal the game closes is inert in that frame: Enter on its focused leaf activates
 *      nothing. ---- */
static void test_menu_in_a_closing_modal_takes_no_key(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    menu_set_modal(MODAL_HOLDS_MENU);
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    for (int f = 0; f < 3; ++f) {
        menu_im_frame(&st, &style, 0.0F, 0.0F);
    }
    menu_key(NT_KEY_ARROW_UP); /* from no focus to the last row, the Edit leaf */
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    const uint32_t row = nt_ui_menu_test_item_id(MENU_A, KEY_EDIT);
    TEST_ASSERT_EQUAL_UINT32(row, st.focus);
    s_act_capture_id = row;
    s_act_hit = false;
    menu_key(NT_KEY_ENTER);
    menu_set_modal(MODAL_CLOSES_AROUND_MENU); /* the game closes the modal in the Enter frame */
    menu_im_frame(&st, &style, 0.0F, 0.0F);
    TEST_ASSERT_FALSE_MESSAGE(s_act_hit, "a menu in a closing modal activates nothing");
}

#define MENU_FIELD 0x4E50F1U
static char s_field_buf[16];

static void menu_field(void) {
    nt_ui_input_style_t is = nt_ui_input_style_defaults();
    is.text.font_size = 16.0F;
    is.placeholder.font_size = 16.0F;
    is.caret_blink_rate = 0.0F;
    const nt_ui_input_props_t props = {0};
    const Clay_ElementDeclaration field = {.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(20)}}};
    (void)nt_ui_input_text(s_fx.ctx, NULL, 0, MENU_FIELD, s_field_buf, sizeof s_field_buf, &props, &is, &field, true, NULL);
}

/* One frame: a text field in the base UI, or in the menu's custom row L1 when `in_menu`; then leaf L2. */
static void menu_field_frame(nt_ui_menu_t *st, nt_ui_menu_style_t *style, bool in_menu, const nt_pointer_t *p) {
    menu_frame_begin(p);
    if (!in_menu) {
        menu_field();
    }
    if (nt_ui_menu_begin(st, s_fx.ctx, NULL, 0U, style)) {
        nt_ui_menu_item_begin(st, KEY_L1, true, false);
        if (in_menu) {
            menu_field();
        }
        nt_ui_menu_item_end(st);
        (void)nt_ui_menu_item(st, KEY_L2, "Leaf");
        nt_ui_menu_end(st);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A text field focused inside the menu owns the keys: Left is the field's, the menu stays open. ---- */
static void test_menu_focused_field_owns_keys(void) {
    nt_ui_menu_style_t style = nt_ui_menu_style_defaults();
    nt_ui_menu_t st = {0};
    memset(s_field_buf, 0, sizeof s_field_buf);
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_field_frame(&st, &style, true, &(nt_pointer_t){.active = true});
    menu_field_frame(&st, &style, true, &(nt_pointer_t){.active = true});
    const nt_ui_bbox_t fb = nt_ui_get_bbox(s_fx.ctx, MENU_FIELD);
    TEST_ASSERT_TRUE(fb.found);
    nt_pointer_t tap = menu_ptr(fb.x + 10.0F, fb.y + 10.0F, NT_BUTTON_LEFT, NT_BUTTON_LEFT);
    tap.type = NT_POINTER_TOUCH;
    menu_field_frame(&st, &style, true, &tap);
    const nt_pointer_t rest = {.x = fb.x + 10.0F, .y = fb.y + 10.0F, .active = true};
    menu_field_frame(&st, &style, true, &rest);
    TEST_ASSERT_TRUE(nt_ui_input_focused(s_fx.ctx, MENU_FIELD));
    menu_key(NT_KEY_ARROW_LEFT);
    menu_field_frame(&st, &style, true, &rest);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "Left belongs to the focused field");
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
    nt_ui_menu_open(&st, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    menu_dyn_frame(&st, &style, &full);
    menu_dyn_frame(&st, &style, &full);
    const uint32_t p = nt_ui_menu_test_item_id(MENU_A, KEY_PARENT);
    float x = 0.0F;
    float y = 0.0F;
    menu_row_center(p, &x, &y);
    for (int f = 0; f < 3 && !menu_row_shown(menu_row(p, KEY_OPT)); ++f) {
        menu_dyn_frame_ptr(&st, &style, &full, &(nt_pointer_t){.x = x, .y = y, .active = true});
    }
    menu_row_center(nt_ui_menu_test_item_id(MENU_A, KEY_L1), &x, &y);
    menu_dyn_frame_ptr(&st, &style, &full, &(nt_pointer_t){.x = x, .y = y, .active = true}); /* focus L1, switch pending */
    menu_dyn_frame_ptr(&st, &style, &lost, &(nt_pointer_t){.x = 790.0F, .y = 590.0F, .active = true});
    menu_dyn_frame_ptr(&st, &style, &lost, &(nt_pointer_t){.x = 790.0F, .y = 590.0F, .active = true});
    TEST_ASSERT_TRUE(menu_row_shown(menu_row(p, KEY_OPT)));
    TEST_ASSERT_TRUE_MESSAGE(menu_row_lit(p) > 0.0F, "the open parent is lit when its level has no focus");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_menu_one_activation_per_frame);
    RUN_TEST(test_menu_panel_blocks_pointer_under_it);
    RUN_TEST(test_menu_right_into_open_parent_closes_grandchild);
    RUN_TEST(test_menu_right_beside_open_sibling_opens_at_once);
    RUN_TEST(test_menu_pending_switch_lands_off_the_rows);
    RUN_TEST(test_menu_one_highlight_per_level);
    RUN_TEST(test_menu_reshown_row_does_not_flash);
    RUN_TEST(test_menu_pointer_over_child_holds_it);
    RUN_TEST(test_menu_outside_press_ignores_other_pointers);
    RUN_TEST(test_menu_fresh_child_survives_a_flick);
    RUN_TEST(test_menu_key_ends_pending_switch);
    RUN_TEST(test_menu_still_press_ends_keyboard_mode);
    RUN_TEST(test_menu_still_right_press_ends_keyboard_mode);
    RUN_TEST(test_menu_covered_switch_waits);
    RUN_TEST(test_menu_right_press_during_a_drag_keeps_open);
    RUN_TEST(test_menu_returned_row_is_not_focused);
    RUN_TEST(test_menu_row_popup_takes_the_pointer_in_its_frame);
    RUN_TEST(test_menu_in_a_closing_modal_takes_no_key);
    RUN_TEST(test_menu_click_beats_a_same_frame_key);
    RUN_TEST(test_menu_and_popup_keep_no_state);
    RUN_TEST(test_menu_focused_field_owns_keys);
    RUN_TEST(test_menu_lost_focus_lights_the_open_parent);
    RUN_TEST(test_menu_right_and_esc_in_one_frame);
    RUN_TEST(test_menu_resting_pointer_yields_to_keyboard);
    RUN_TEST(test_menu_keys_act_on_the_focused_level);
    RUN_TEST(test_menu_press_in_just_closed_panel_keeps_open);
    RUN_TEST(test_menu_right_edge_root_clamp_and_submenu_flip);
    RUN_TEST(test_menu_back_on_the_parent_restarts_the_corridor);
    RUN_TEST(test_menu_aim_near_edge_faces_apex);
    RUN_TEST(test_menu_aim_keeps_submenu_open_on_diagonal);
    RUN_TEST(test_menu_aim_switch_after_fallback);
    RUN_TEST(test_menu_click_root_parent_switches_branch);
    RUN_TEST(test_menu_hover_root_parent_switches_branch);
    RUN_TEST(test_menu_hover_collapse_reopen_forgets_descendants);
    RUN_TEST(test_menu_insert_before_open_parent_keeps_branch);
    RUN_TEST(test_menu_focus_follows_key_across_edits);
    RUN_TEST(test_menu_lost_open_parent_closes_branch);
    RUN_TEST(test_menu_kbd_open_focuses_first_enabled_row);
    RUN_TEST(test_menu_kbd_empty_submenu);
    RUN_TEST(test_menu_outside_press_dismisses_chain);
    RUN_TEST(test_menu_depth_cap_asserts);
    RUN_TEST(test_menu_depth_cap_asserts_on_a_disabled_parent);
    RUN_TEST(test_menu_icon_gutter_aligns_labels);
    RUN_TEST(test_menu_icon_gutter_absent_when_zero);
    RUN_TEST(test_menu_separators_non_interactive);
    RUN_TEST(test_menu_pointer_on_a_separator_is_inert);
    RUN_TEST(test_menu_arrow_marker_on_parent_only);
    RUN_TEST(test_menu_left_at_root_closes);
    RUN_TEST(test_menu_back_closes_a_hover_opened_child);
    RUN_TEST(test_menu_no_focus_right_leaves_down);
    RUN_TEST(test_menu_opening_frame_takes_no_input);
    RUN_TEST(test_menu_disabled_focused_row_keeps_its_place);
    RUN_TEST(test_menu_up_down_wrap);
    RUN_TEST(test_menu_down_skips_disabled);
    RUN_TEST(test_menu_item_begin_child_owns_its_press);
    RUN_TEST(test_menu_item_begin_press_beside_child);
    RUN_TEST(test_menu_item_begin_captured_drag_keeps_focus);
    RUN_TEST(test_menu_item_begin_enter_activates);
    RUN_TEST(test_menu_click_activates_enabled_rows_only);
    RUN_TEST(test_menu_row_in_custom_row_asserts);
    RUN_TEST(test_menu_separator_in_custom_row_asserts);
    RUN_TEST(test_menu_checked_parent_asserts);
    RUN_TEST(test_menu_shortcut_cell_on_rich_row_only);
    RUN_TEST(test_menu_check_cell_when_selected);
    RUN_TEST(test_menu_duplicate_sibling_key_asserts);
    RUN_TEST(test_menu_submenu_begin_ex_icon_disabled);
    return UNITY_END();
}
