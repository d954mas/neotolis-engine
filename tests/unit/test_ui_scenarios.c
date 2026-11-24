/* UI scenario tests: scripted pointer and key actions over a game-like frame, one story per input-ownership
 * rule (spec chapter UI, "Interaction model", "Input ownership") and per key menu or popup behavior. They
 * assert only what a game observes: the clicks it logged, its open bools and close reasons, the field
 * buffers, nt_ui_wants_pointer / nt_ui_wants_keyboard and what the menu rows returned. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_scenario.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_dropdown.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_menu.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_tooltip.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define BTN_A 0x5C0001U
#define BTN_B 0x5C0002U
#define BTN_UNDER 0x5C0003U
#define MODAL_A 0x5C0010U
#define MODAL_B 0x5C0011U
#define MODAL_A_BTN 0x5C0012U
#define MODAL_B_BTN 0x5C0013U
#define POP_A 0x5C0020U
#define POP_B 0x5C0021U
#define POP_A_BTN 0x5C0022U
#define COMBO_A 0x5C0030U
#define TIP_BOX 0x5C0031U
#define FIELD_BASE 0x5C0040U
#define FIELD_M1 0x5C0041U
#define FIELD_M2 0x5C0042U
#define MENU_A 0x5C0050U

#define ROW_KEY(i) (0x5C0100U + (uint32_t)(i))
#define KEY_FILE 1U
#define KEY_TOOLS 2U
#define KEY_EDIT 3U
#define KEY_NEW 10U
#define KEY_OPEN 11U
#define KEY_QUIT 12U
#define KEY_PROJECT 20U
#define KEY_FILE2 21U
#define KEY_OPT 30U
#define KEY_COMBO_ROW 40U

/* The game's own action ids for menu rows it logs. */
#define ACT_NEW 0xAC01U
#define ACT_QUIT 0xAC02U
#define ACT_FILE2 0xAC03U
#define ACT_OPT 0xAC04U
#define ACT_EDIT 0xAC05U

/* The 60 Hz frame on which the menu's aim fallback runs out. */
#define AIM_FALLBACK_FRAMES ((int)ceilf(NT_UI_MENU_AIM_FALLBACK_SECS / UI_FX_DT))

/* What a game keeps: its click log, open bools, buffers and what its overlays reported. */
typedef struct {
    uint32_t log[16];
    int n;
    bool modal_a, modal_b, pop_a, pop_b, combo;
    bool pop_a_shown;
    uint8_t modal_flags;
    uint8_t reason_modal_a, reason_pop_a;
    int combo_sel;
    bool tip_seen;
    char buf_base[32], buf_m1[32], buf_m2[32];
    nt_ui_menu_t menu;
    nt_ui_menu_style_t menu_style;
    bool file_body, open_body;
    nt_ui_dropdown_style_t dd_style;
    nt_ui_tooltip_style_t tip_style;
    nt_ui_input_style_t in_style;
} game_t;

static game_t g;
static sc_t s;

static const char *const s_rows[] = {"Alpha", "Beta", "Gamma"};
static const Clay_ElementDeclaration s_field_decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(28)}}};
static const nt_ui_input_props_t s_props = {0};

static void scenario(sc_frame_fn frame) {
    s = (sc_t){.ctx = s_fx.ctx, .frame = frame, .game = &g};
    sc_frames(&s, 1); /* lays the UI out: a record takes input from the frame after it is first declared */
}

void setUp(void) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    ui_walker_fixture_fill_stub_font(&s_fx);
    memset(&g, 0, sizeof g);
    g.combo_sel = -1;
    g.menu_style = nt_ui_menu_style_defaults();
    g.dd_style = nt_ui_dropdown_style_defaults();
    g.tip_style = nt_ui_tooltip_style_defaults();
    g.tip_style.delay_secs = 0.0F;
    g.in_style = nt_ui_input_style_defaults();
    g.in_style.text.font_size = 20.0F;
    g.in_style.placeholder.font_size = 20.0F;
    g.in_style.caret_blink_rate = 0.0F;
}

void tearDown(void) {
    ui_walker_fixture_shutdown(&s_fx);
    nt_input_shutdown();
}

// #region game widgets
static void log_click(uint32_t id, bool clicked) {
    if (clicked && g.n < 16) {
        g.log[g.n++] = id;
    }
}

static int clicks(uint32_t id) {
    int c = 0;
    for (int i = 0; i < g.n; ++i) {
        c += (g.log[i] == id) ? 1 : 0;
    }
    return c;
}

static void button(uint32_t id, float w, float h) {
    CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}}) {}
    log_click(id, nt_ui_step(s.ctx, id, NULL).clicked);
}

static void base_button(uint32_t id, float x, float y, float w, float h) {
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(id, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = x, .y = y}}}) { button(id, w, h); }
}

static void field(uint32_t id, char *buf) { (void)nt_ui_input_text(s.ctx, NULL, 0U, id, buf, 32U, &s_props, &g.in_style, &s_field_decl, true, NULL); }

static void base_field(uint32_t id, char *buf, float x, float y) {
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(id, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = x, .y = y}}}) { field(id, buf); }
}

static void combo(void) {
    if (nt_ui_combo_begin(s.ctx, NULL, 0U, COMBO_A, "pick", &g.dd_style, &g.combo)) {
        for (int i = 0; i < 3; ++i) {
            if (nt_ui_combo_selectable(s.ctx, ROW_KEY(i), s_rows[i], g.combo_sel == i)) {
                g.combo_sel = i;
            }
        }
        nt_ui_combo_end(s.ctx);
    }
}

static void base_combo(float x, float y) {
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(COMBO_A, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = x, .y = y}}}) { combo(); }
}

static uint32_t combo_row(int i) { return nt_ui_dropdown_test_combo_row_id(COMBO_A, ROW_KEY(i)); }

static bool modal_begin(uint32_t id, bool *open, uint8_t *reason) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    ms.flags = g.modal_flags;
    nt_ui_popup_info_t info = {0};
    const bool shown = nt_ui_modal_begin(s.ctx, id, &ms, open, &info);
    if (reason != NULL && info.reason != NT_UI_POPUP_CLOSE_NONE) {
        *reason = info.reason;
    }
    return shown;
}

/* A light-dismiss popup below (x, y) holding one button; ease 0 snaps. Returns whether it shows. */
static bool popup(uint32_t id, uint32_t btn, float x, float y, float ease, bool *open, uint8_t *reason) {
    nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
    ps.ease_speed = ease;
    const nt_ui_popup_anchor_t anc = {.x = x, .y = y, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_popup_info_t info = {0};
    const bool shown = nt_ui_popup_begin(s.ctx, id, &ps, &anc, open, &info);
    if (reason != NULL && info.reason != NT_UI_POPUP_CLOSE_NONE) {
        *reason = info.reason;
    }
    if (shown) {
        button(btn, 120.0F, 40.0F);
        nt_ui_popup_end(s.ctx);
    }
    return shown;
}

static uint32_t menu_row(uint32_t scope, uint32_t key) { return nt_ui_menu_test_item_id(scope, key); }

/* File > (New, Open > (Project, File2), Quit), Tools > (Opt), Edit; logs every activation. */
static void menu_tree(void) {
    g.file_body = false;
    g.open_body = false;
    if (nt_ui_menu_begin(&g.menu, s.ctx, NULL, 0U, MENU_A, &g.menu_style)) {
        if (nt_ui_menu_submenu_begin(&g.menu, KEY_FILE, "File")) {
            g.file_body = true;
            log_click(ACT_NEW, nt_ui_menu_item(&g.menu, KEY_NEW, "New"));
            if (nt_ui_menu_submenu_begin(&g.menu, KEY_OPEN, "Open")) {
                g.open_body = true;
                (void)nt_ui_menu_item(&g.menu, KEY_PROJECT, "Project");
                log_click(ACT_FILE2, nt_ui_menu_item(&g.menu, KEY_FILE2, "File2"));
                nt_ui_menu_submenu_end(&g.menu);
            }
            log_click(ACT_QUIT, nt_ui_menu_item(&g.menu, KEY_QUIT, "Quit"));
            nt_ui_menu_submenu_end(&g.menu);
        }
        if (nt_ui_menu_submenu_begin(&g.menu, KEY_TOOLS, "Tools")) {
            log_click(ACT_OPT, nt_ui_menu_item(&g.menu, KEY_OPT, "Opt"));
            nt_ui_menu_submenu_end(&g.menu);
        }
        log_click(ACT_EDIT, nt_ui_menu_item(&g.menu, KEY_EDIT, "Edit"));
        nt_ui_menu_end(&g.menu);
    }
}
// #endregion

// #region game frames
static void frame_overlap(void *game) {
    (void)game;
    base_button(BTN_A, 100.0F, 100.0F, 200.0F, 100.0F);
    base_button(BTN_B, 200.0F, 150.0F, 200.0F, 100.0F);
}

static void frame_combo_button(void *game) {
    (void)game;
    base_combo(30.0F, 30.0F);
    base_button(BTN_A, 400.0F, 400.0F, 150.0F, 60.0F);
}

static void frame_button_modal(void *game) {
    (void)game;
    base_button(BTN_A, 50.0F, 50.0F, 120.0F, 50.0F);
    if (modal_begin(MODAL_A, &g.modal_a, &g.reason_modal_a)) {
        button(MODAL_A_BTN, 80.0F, 30.0F);
        nt_ui_modal_end(s.ctx);
    }
}

static void frame_field_modal(void *game) {
    (void)game;
    base_field(FIELD_BASE, g.buf_base, 50.0F, 50.0F);
    if (modal_begin(MODAL_A, &g.modal_a, &g.reason_modal_a)) {
        CLAY({.id = CLAY_ID("sc_modal_col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
            field(FIELD_M1, g.buf_m1);
            field(FIELD_M2, g.buf_m2);
        }
        nt_ui_modal_end(s.ctx);
    }
}

/* Modal B is declared before modal A: only the open order may put it on top. */
static void frame_two_modals(void *game) {
    (void)game;
    if (modal_begin(MODAL_B, &g.modal_b, NULL)) {
        CLAY({.id = CLAY_ID("sc_panel_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(60)}}}) { button(MODAL_B_BTN, 80.0F, 30.0F); }
        nt_ui_modal_end(s.ctx);
    }
    if (modal_begin(MODAL_A, &g.modal_a, NULL)) {
        CLAY({.id = CLAY_ID("sc_panel_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(200)}}}) { button(MODAL_A_BTN, 80.0F, 30.0F); }
        nt_ui_modal_end(s.ctx);
    }
}

/* A modal holding a combo with a button declared after it, right under the combo's list. */
static void frame_modal_combo(void *game) {
    (void)game;
    if (modal_begin(MODAL_A, &g.modal_a, &g.reason_modal_a)) {
        CLAY({.id = CLAY_ID("sc_modal_combo_col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            combo();
            button(MODAL_A_BTN, 200.0F, 120.0F);
        }
        nt_ui_modal_end(s.ctx);
    }
}

static void frame_inert_popup(void *game) {
    (void)game;
    base_button(BTN_UNDER, 60.0F, 60.0F, 300.0F, 200.0F);
    g.pop_a_shown = popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 4.0F, &g.pop_a, &g.reason_pop_a);
}

static void frame_two_popups(void *game) {
    (void)game;
    (void)popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 0.0F, &g.pop_a, &g.reason_pop_a);
    (void)popup(POP_B, BTN_B, 400.0F, 100.0F, 0.0F, &g.pop_b, NULL);
}

static void frame_popup_button(void *game) {
    (void)game;
    (void)popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 0.0F, &g.pop_a, &g.reason_pop_a);
    base_button(BTN_A, 400.0F, 400.0F, 150.0F, 60.0F);
}

static void frame_combo_menu(void *game) {
    (void)game;
    base_combo(30.0F, 30.0F);
    menu_tree();
}

/* A combo inside a container that carries a tooltip. */
static void frame_tooltip_combo(void *game) {
    (void)game;
    CLAY({.id = (Clay_ElementId){.id = TIP_BOX}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        nt_ui_block_pointer(s.ctx, TIP_BOX, NULL);
        combo();
    }
    g.tip_seen |= nt_ui_tooltip(s.ctx, NULL, 0U, TIP_BOX, "hint", &g.tip_style);
}

static void frame_menu(void *game) {
    (void)game;
    menu_tree();
}

/* Root: File > (New), a custom row hosting the combo, Edit. */
static void frame_menu_combo_row(void *game) {
    (void)game;
    g.file_body = false;
    if (nt_ui_menu_begin(&g.menu, s.ctx, NULL, 0U, MENU_A, &g.menu_style)) {
        if (nt_ui_menu_submenu_begin(&g.menu, KEY_FILE, "File")) {
            g.file_body = true;
            log_click(ACT_NEW, nt_ui_menu_item(&g.menu, KEY_NEW, "New"));
            nt_ui_menu_submenu_end(&g.menu);
        }
        nt_ui_menu_item_begin(&g.menu, KEY_COMBO_ROW, true, false);
        combo();
        (void)nt_ui_menu_item_end(&g.menu);
        log_click(ACT_EDIT, nt_ui_menu_item(&g.menu, KEY_EDIT, "Edit"));
        nt_ui_menu_end(&g.menu);
    }
}
// #endregion

// #region Router: Pointer, Gesture
/* Pointer: where two buttons overlap, the later-declared one takes the click; outside the overlap the other
 * still works. */
static void test_pointer_front_most_of_two_buttons_takes_the_click(void) {
    scenario(frame_overlap);
    const nt_ui_bbox_t a = sc_box(&s, BTN_A);
    const nt_ui_bbox_t b = sc_box(&s, BTN_B);
    const float ox = (fmaxf(a.x, b.x) + fminf(a.x + a.width, b.x + b.width)) * 0.5F;
    const float oy = (fmaxf(a.y, b.y) + fminf(a.y + a.height, b.y + b.height)) * 0.5F;
    sc_click(&s, ox, oy);
    TEST_ASSERT_EQUAL_INT(1, g.n);
    TEST_ASSERT_EQUAL_UINT32(BTN_B, g.log[0]);
    sc_click(&s, a.x + 10.0F, a.y + 10.0F);
    TEST_ASSERT_EQUAL_INT(2, g.n);
    TEST_ASSERT_EQUAL_UINT32(BTN_A, g.log[1]);
}

/* Gesture: the press that dismisses a combo is the UI's to its release, on empty space too; one over a
 * button clicks nothing, and the next click on it works. */
static void test_gesture_combo_dismiss_press_stays_with_the_ui(void) {
    scenario(frame_combo_button);
    sc_click_id(&s, COMBO_A);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);

    sc_move(&s, 700.0F, 100.0F);
    sc_press(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_FALSE_MESSAGE(g.combo, "the outside press closes the combo");
    TEST_ASSERT_TRUE_MESSAGE(s.wants_pointer, "the dismissing press is the UI's");
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE_MESSAGE(s.wants_pointer, "while it is held, over nothing");
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_TRUE_MESSAGE(s.wants_pointer, "up to its release");
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(s.wants_pointer, "and no longer after it");

    sc_click_id(&s, COMBO_A);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, clicks(BTN_A), "the dismissing click does not click the button under it");
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_A));
}

/* Gesture: a left press while the game's right-drag is held joins the game's gesture and clicks nothing. */
static void test_gesture_left_press_during_game_right_drag_clicks_nothing(void) {
    scenario(frame_combo_button);
    sc_move(&s, 700.0F, 100.0F);
    sc_press(&s, NT_BUTTON_RIGHT);
    TEST_ASSERT_FALSE_MESSAGE(s.wants_pointer, "a right press on nothing starts the game's drag");
    sc_move_to(&s, BTN_A);
    sc_press(&s, NT_BUTTON_LEFT);
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, clicks(BTN_A), "the joining press belongs to the game's drag");
    sc_release(&s, NT_BUTTON_RIGHT);
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_A));
}
// #endregion

// #region Router: Live
/* Live: a modal opened while a button is held ends the press; the release clicks nothing. */
static void test_live_modal_over_held_press_cancels_the_click(void) {
    scenario(frame_button_modal);
    sc_move_to(&s, BTN_A);
    sc_press(&s, NT_BUTTON_LEFT);
    g.modal_a = true;
    sc_frames(&s, 2);
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT(0, g.n);
    g.modal_a = false;
    sc_frames(&s, 1);
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_A));
}

/* Live: a focused field under a modal the game opens loses its focus and its typed chars. */
static void test_live_field_under_modal_stops_typing(void) {
    scenario(frame_field_modal);
    sc_click_id(&s, FIELD_BASE);
    sc_char(&s, 'a');
    TEST_ASSERT_EQUAL_STRING("a", g.buf_base);
    g.modal_a = true;
    sc_frames(&s, 1);
    sc_char(&s, 'b');
    TEST_ASSERT_EQUAL_STRING("a", g.buf_base);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s.ctx, FIELD_BASE));
}

/* Live: Tab inside a modal cycles its own fields and never lands on the field under it. */
static void test_live_tab_in_modal_cycles_only_its_fields(void) {
    g.modal_a = true;
    scenario(frame_field_modal);
    sc_frames(&s, 1);
    sc_click_id(&s, FIELD_M1);
    sc_char(&s, 'a');
    sc_key(&s, NT_KEY_TAB);
    sc_char(&s, 'b');
    sc_key(&s, NT_KEY_TAB);
    sc_char(&s, 'c');
    TEST_ASSERT_EQUAL_STRING("ac", g.buf_m1);
    TEST_ASSERT_EQUAL_STRING("b", g.buf_m2);
    TEST_ASSERT_EQUAL_STRING("", g.buf_base);
}
// #endregion

// #region Router: Keys
/* Keys: a focused field in a modal owns the keys; its Esc unfocuses it, the next Esc closes the modal. */
static void test_keys_esc_unfocuses_field_then_closes_modal(void) {
    g.modal_a = true;
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    scenario(frame_field_modal);
    sc_frames(&s, 1);
    sc_click_id(&s, FIELD_M1);
    TEST_ASSERT_TRUE(s.wants_keyboard);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s.ctx, FIELD_M1));
    TEST_ASSERT_TRUE_MESSAGE(g.modal_a, "the field's Esc does not reach the modal");
    TEST_ASSERT_TRUE_MESSAGE(s.wants_keyboard, "the modal owns the keys now");
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.modal_a);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_modal_a);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE(s.wants_keyboard);
}

/* Keys: the Esc that closes a menu was the menu's, so the game sees wants_keyboard in that frame. */
static void test_keys_menu_closing_esc_still_wants_keyboard(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, 120.0F, 80.0F);
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(s.wants_keyboard);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.menu.open);
    TEST_ASSERT_TRUE_MESSAGE(s.wants_keyboard, "the closing Esc is not the game's");
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE(s.wants_keyboard);
}
// #endregion

// #region Router: Inert
/* Inert: a popup fading out takes no click; the button under its panel does. */
static void test_inert_fading_popup_passes_the_click_through(void) {
    g.pop_a = true;
    scenario(frame_inert_popup);
    sc_frames(&s, 1);
    sc_click_id(&s, POP_A_BTN);
    TEST_ASSERT_EQUAL_INT(1, clicks(POP_A_BTN));
    TEST_ASSERT_EQUAL_INT(0, clicks(BTN_UNDER));
    g.pop_a = false;
    sc_click_id(&s, POP_A_BTN);
    TEST_ASSERT_TRUE_MESSAGE(g.pop_a_shown, "the popup still fades out");
    TEST_ASSERT_EQUAL_INT(1, clicks(POP_A_BTN));
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_UNDER));
}
// #endregion

// #region Overlay stack: Stack, One transient child, Own close
/* Stack: the modal opened last is on top though declared first. */
static void test_stack_last_opened_modal_is_on_top(void) {
    g.modal_a = true;
    scenario(frame_two_modals);
    sc_frames(&s, 1);
    g.modal_b = true;
    sc_frames(&s, 2);
    sc_click_id(&s, MODAL_A_BTN);
    TEST_ASSERT_EQUAL_INT(0, g.n);
    sc_click_id(&s, MODAL_B_BTN);
    TEST_ASSERT_EQUAL_INT(1, g.n);
    TEST_ASSERT_EQUAL_UINT32(MODAL_B_BTN, g.log[0]);
}

/* Stack: a combo opened inside a modal lies above it: its row wins over the modal's button under the list. */
static void test_stack_combo_in_modal_stays_above_it(void) {
    g.modal_a = true;
    scenario(frame_modal_combo);
    sc_frames(&s, 1);
    sc_click_id(&s, COMBO_A);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    const nt_ui_bbox_t row = sc_box(&s, combo_row(1));
    const nt_ui_bbox_t btn = sc_box(&s, MODAL_A_BTN);
    const float rx = row.x + (row.width * 0.5F);
    const float ry = row.y + (row.height * 0.5F);
    TEST_ASSERT_TRUE_MESSAGE(rx > btn.x && rx < btn.x + btn.width && ry > btn.y && ry < btn.y + btn.height, "the row lies over the modal's button");
    sc_click(&s, rx, ry);
    TEST_ASSERT_EQUAL_INT(1, g.combo_sel);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_EQUAL_INT(0, clicks(MODAL_A_BTN));
    TEST_ASSERT_TRUE(g.modal_a);
}

/* One transient child: a second light-dismiss popup opened at the base closes the first one. */
static void test_transient_second_popup_replaces_the_first(void) {
    g.pop_a = true;
    scenario(frame_two_popups);
    sc_frames(&s, 1);
    g.pop_b = true;
    sc_frames(&s, 2);
    TEST_ASSERT_FALSE(g.pop_a);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_REPLACED, g.reason_pop_a);
    TEST_ASSERT_TRUE(g.pop_b);
}

/* One transient child: opening a menu closes an open combo. */
static void test_transient_menu_closes_open_combo(void) {
    scenario(frame_combo_menu);
    sc_click_id(&s, COMBO_A);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    nt_ui_menu_open(&g.menu, 400.0F, 300.0F);
    sc_frames(&s, 2);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_TRUE(g.menu.open);
}

/* One transient child: a tooltip is not transient, so showing one next to an open combo keeps it open, and the
 * combo stays the top that takes Esc. */
static void test_transient_tooltip_keeps_open_combo(void) {
    scenario(frame_tooltip_combo);
    sc_click_id(&s, COMBO_A);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    g.tip_seen = false;
    sc_move_to(&s, combo_row(0));
    sc_frames(&s, 3);
    TEST_ASSERT_TRUE_MESSAGE(g.tip_seen, "the tooltip shows over the open combo");
    TEST_ASSERT_TRUE(g.combo);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.combo);
}

/* Own close: an outside press closes a popup, clicks nothing under it, and the next press reaches it. */
static void test_own_close_outside_click_closes_popup(void) {
    g.pop_a = true;
    scenario(frame_popup_button);
    sc_frames(&s, 1);
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_FALSE(g.pop_a);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_OUTSIDE, g.reason_pop_a);
    TEST_ASSERT_EQUAL_INT(0, clicks(BTN_A));
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_A));
}

/* Own close: Esc closes the top overlay only, the combo inside the modal; the next Esc closes the modal. */
static void test_own_close_esc_closes_top_combo_only(void) {
    g.modal_a = true;
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    scenario(frame_modal_combo);
    sc_frames(&s, 1);
    sc_click_id(&s, COMBO_A);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_TRUE(g.modal_a);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.modal_a);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_modal_a);
}
// #endregion

// #region Menu
/* Down, Right, Down, Enter, Down, Enter reach File > Open > File2, which reports its activation once. */
static void test_menu_keys_activate_nested_leaf_once(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ARROW_DOWN);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    TEST_ASSERT_TRUE(g.file_body);
    sc_key(&s, NT_KEY_ARROW_DOWN);
    sc_key(&s, NT_KEY_ENTER);
    TEST_ASSERT_TRUE(g.open_body);
    sc_key(&s, NT_KEY_ARROW_DOWN);
    sc_key(&s, NT_KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(1, g.n);
    TEST_ASSERT_EQUAL_UINT32(ACT_FILE2, g.log[0]);
    TEST_ASSERT_FALSE(g.menu.open);
    sc_frames(&s, 2);
    TEST_ASSERT_EQUAL_INT(1, g.n);
}

/* Esc closes the deepest level first and focuses its parent (Right opens it again), then the menu. */
static void test_menu_esc_closes_deepest_level_first(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ARROW_DOWN);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    TEST_ASSERT_TRUE(g.file_body);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_TRUE(g.menu.open);
    TEST_ASSERT_FALSE(g.file_body);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "the closed level's parent keeps the focus");
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_TRUE(g.menu.open);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.menu.open);
    TEST_ASSERT_EQUAL_INT(0, g.n);
}

/* A hover-opened submenu stays open while the pointer crosses sibling rows diagonally toward it for longer than
 * the aim fallback (and under twice it); a leaf there then activates. */
static void test_menu_diagonal_travel_keeps_hovered_submenu(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_move_to(&s, menu_row(MENU_A, KEY_FILE));
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE(g.file_body);

    const nt_ui_bbox_t tools = sc_box(&s, menu_row(MENU_A, KEY_TOOLS));
    const nt_ui_bbox_t sub = sc_box(&s, nt_ui_menu_test_panel_id(MENU_A, 1U));
    const float ax = tools.x + (tools.width * 0.5F);
    const float ay = tools.y + (tools.height * 0.5F);
    const float tx = sub.x + 4.0F;
    const float ty = sub.y + sub.height - 8.0F;
    const int steps = AIM_FALLBACK_FRAMES + 3; /* over the siblings longer than the fallback, under twice it */
    TEST_ASSERT_TRUE((float)steps * UI_FX_DT < NT_UI_MENU_AIM_FALLBACK_SECS * 2.0F);
    for (int k = 0; k <= steps; ++k) {
        const float f = (float)k / (float)steps;
        sc_move(&s, ax + ((tx - ax) * f), ay + ((ty - ay) * f));
        TEST_ASSERT_TRUE_MESSAGE(g.file_body, "the corridor keeps the File submenu");
    }
    sc_frames(&s, AIM_FALLBACK_FRAMES * 3);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "over the submenu it stays");
    sc_click_id(&s, menu_row(menu_row(MENU_A, KEY_FILE), KEY_QUIT));
    TEST_ASSERT_EQUAL_INT(1, g.n);
    TEST_ASSERT_EQUAL_UINT32(ACT_QUIT, g.log[0]);
    TEST_ASSERT_FALSE(g.menu.open);
}

/* A combo opened from a custom menu row closes the sibling submenu, well before any hover switch could. */
static void test_menu_combo_from_custom_row_closes_sibling_submenu(void) {
    scenario(frame_menu_combo_row);
    nt_ui_menu_open(&g.menu, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_move_to(&s, menu_row(MENU_A, KEY_FILE));
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE(g.file_body);
    sc_move_to(&s, COMBO_A);
    sc_press(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "the submenu is still open when the combo's trigger is pressed");
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(4 < AIM_FALLBACK_FRAMES, "the four frames since the row was hovered stay under the aim fallback");
    TEST_ASSERT_FALSE_MESSAGE(g.file_body, "the combo replaces the sibling submenu");
    TEST_ASSERT_TRUE(g.combo);
    TEST_ASSERT_TRUE(g.menu.open);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pointer_front_most_of_two_buttons_takes_the_click);
    RUN_TEST(test_gesture_combo_dismiss_press_stays_with_the_ui);
    RUN_TEST(test_gesture_left_press_during_game_right_drag_clicks_nothing);
    RUN_TEST(test_live_modal_over_held_press_cancels_the_click);
    RUN_TEST(test_live_field_under_modal_stops_typing);
    RUN_TEST(test_live_tab_in_modal_cycles_only_its_fields);
    RUN_TEST(test_keys_esc_unfocuses_field_then_closes_modal);
    RUN_TEST(test_keys_menu_closing_esc_still_wants_keyboard);
    RUN_TEST(test_inert_fading_popup_passes_the_click_through);
    RUN_TEST(test_stack_last_opened_modal_is_on_top);
    RUN_TEST(test_stack_combo_in_modal_stays_above_it);
    RUN_TEST(test_transient_second_popup_replaces_the_first);
    RUN_TEST(test_transient_menu_closes_open_combo);
    RUN_TEST(test_transient_tooltip_keeps_open_combo);
    RUN_TEST(test_own_close_outside_click_closes_popup);
    RUN_TEST(test_own_close_esc_closes_top_combo_only);
    RUN_TEST(test_menu_keys_activate_nested_leaf_once);
    RUN_TEST(test_menu_esc_closes_deepest_level_first);
    RUN_TEST(test_menu_diagonal_travel_keeps_hovered_submenu);
    RUN_TEST(test_menu_combo_from_custom_row_closes_sibling_submenu);
    return UNITY_END();
}
