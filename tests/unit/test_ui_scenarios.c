/* UI scenario tests: scripted pointer and key actions over a game-like frame, one story per input-ownership
 * rule (spec chapter UI, "Interaction model", "Input ownership") and per key menu or popup behavior. They
 * assert only what a game observes: the clicks it logged, nt_ui_is_open, what its overlays' begins returned and
 * reported, the field buffers, nt_ui_wants_pointer / nt_ui_wants_keyboard, what the menu rows returned and the
 * stacking of what the frame painted. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_menu_tree.h"
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
#define TIP_TARGET(i) (0x5C0200U + (uint32_t)(i))
#define MODAL_X 0x5C0014U
#define MODAL_Y 0x5C0015U
#define MODAL_X_BTN 0x5C0016U
#define MODAL_Y_BTN 0x5C0017U
#define MENU_ROW_BTN 0x5C0060U
#define CHAIN_POP(i) (0x5C0300U + (uint32_t)(i))
#define CHAIN_BTN(i) (0x5C0320U + (uint32_t)(i))
#define FIELD_DEEP 0x5C0340U
#define KEY_BUTTON_ROW 41U
#define KEY_PREFS 42U
#define LONG_COMBO_ROWS 12

#define ROW_KEY(i) (0x5C0100U + (uint32_t)(i))
#define KEY_FILE UI_MT_KEY_FILE
#define KEY_TOOLS UI_MT_KEY_TOOLS
#define KEY_EDIT UI_MT_KEY_EDIT
#define KEY_NEW UI_MT_KEY_NEW
#define KEY_OPEN UI_MT_KEY_OPEN
#define KEY_QUIT UI_MT_KEY_QUIT
#define KEY_FILE2 UI_MT_KEY_FILE2
#define KEY_COMBO_ROW 40U

/* The rows a game logs when they activate, by row id. */
#define ROW_FILE menu_row(MENU_A, KEY_FILE)
#define ACT_NEW menu_row(ROW_FILE, KEY_NEW)
#define ACT_QUIT menu_row(ROW_FILE, KEY_QUIT)
#define ACT_FILE2 menu_row(menu_row(ROW_FILE, KEY_OPEN), KEY_FILE2)
#define ACT_EDIT menu_row(MENU_A, KEY_EDIT)
#define ACT_PREFS menu_row(MENU_A, KEY_PREFS)

/* The 60 Hz frame on which the menu's aim fallback runs out. */
#define AIM_FALLBACK_FRAMES ((int)ceilf(NT_UI_MENU_AIM_FALLBACK_SECS / UI_FX_DT))

/* More tooltip targets than the overlay stack holds. */
#define TIP_N (NT_UI_OVERLAY_MAX + 2)

/* What a game keeps: its click log, buffers, what its overlays reported and what it asks the game's code to do
 * in the next frame. */
typedef struct {
    uint32_t log[16];
    int n;
    bool combo; /* the combo's list showed this frame */
    bool pop_a_shown;
    bool skip_pop_a; /* frame_popup_button leaves popup A out */
    uint8_t modal_flags;
    float modal_ease;
    uint8_t reason_modal_a, reason_pop_a, reason_pop_b; /* the last close each reported */
    uint8_t reason_pop_a_now;                           /* what popup A's begin reported this frame */
    int combo_sel;
    bool tip_seen;
    char buf_base[32], buf_m1[32], buf_m2[32];
    nt_ui_menu_t menu;
    nt_ui_menu_style_t menu_style;
    bool file_body, open_body;
    int frames;
    bool open_two_popups;  /* frame_two_popups_reversed opens A, then B, before declaring them */
    bool open_confirm;     /* frame_combo_confirm opens the confirm from its custom row */
    bool open_pop_in_list; /* frame_combo_confirm opens popup A from its custom row */
    bool open_nested;      /* frame_nested_modals opens modal B from inside modal A's body */
    bool reopen_menu;      /* frame_menu reopens the menu after nt_ui_begin */
    bool pop_a_seen_open;  /* nt_ui_is_open(POP_A) read before anything of the frame declared */
    bool hover_trigger;    /* nt_ui_hovered_within the combo trigger */
    bool hover_box;        /* nt_ui_hovered_within the container around the combo */
    bool open_chain;       /* frame_popup_chain opens every level inside the one under it */
    bool wants_at_begin;   /* nt_ui_wants_pointer read before anything of the frame declared */
    bool skip_modal_a;     /* frame_modal_opens_root_modal leaves modal A out */
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
    g.combo = nt_ui_combo_begin(s.ctx, NULL, 0U, COMBO_A, "pick", &g.dd_style);
    if (g.combo) {
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

static uint32_t combo_trigger(void) { return nt_ui_combo_trigger_id(COMBO_A); }

static bool modal_begin(uint32_t id, uint8_t *reason) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = g.modal_ease;
    ms.flags = g.modal_flags;
    nt_ui_popup_info_t info = {0};
    const bool shown = nt_ui_modal_begin(s.ctx, id, &ms, &info);
    if (reason != NULL && info.reason != NT_UI_POPUP_CLOSE_NONE) {
        *reason = info.reason;
    }
    return shown;
}

static void modal_with_button(uint32_t id, uint32_t btn) {
    if (modal_begin(id, NULL)) {
        button(btn, 80.0F, 30.0F);
        nt_ui_modal_end(s.ctx);
    }
}

/* A popup below (x, y) holding one button; ease 0 snaps. Keeps the last close it reported in `reason` and
 * this frame's report in `reason_now` (both nullable). Returns whether it shows. */
static bool popup(uint32_t id, uint32_t btn, float x, float y, float ease, uint8_t *reason, uint8_t *reason_now) {
    nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
    ps.ease_speed = ease;
    const nt_ui_popup_anchor_t anc = {.x = x, .y = y, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_popup_info_t info = {0};
    const bool shown = nt_ui_popup_begin(s.ctx, id, &ps, &anc, &info);
    if (reason != NULL && info.reason != NT_UI_POPUP_CLOSE_NONE) {
        *reason = info.reason;
    }
    if (reason_now != NULL) {
        *reason_now = info.reason;
    }
    if (shown) {
        button(btn, 120.0F, 40.0F);
        nt_ui_popup_end(s.ctx);
    }
    return shown;
}

static uint32_t menu_row(uint32_t scope, uint32_t key) { return nt_ui_menu_test_item_id(scope, key); }

/* The shared menu tree; logs every activation by row id. */
static void menu_tree(void) {
    ui_menu_tree_t r;
    (void)ui_menu_tree(&g.menu, s.ctx, &g.menu_style, &r);
    g.file_body = r.file_body;
    g.open_body = r.open_body;
    log_click(r.activated, r.activated != 0U);
}

static bool menu_level_shown(uint8_t depth) { return nt_ui_get_bbox(s.ctx, nt_ui_menu_test_panel_id(MENU_A, depth)).found; }

/* TEXT commands the last frame painted on `band`. */
static int texts_on_band(int32_t band) {
    int n = 0;
    for (int32_t i = 0; i < s.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s.ctx->frozen_cmds.internalArray[i];
        n += (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT && c->zIndex == band) ? 1 : 0;
    }
    return n;
}

/* The band the last frame painted the backdrop of modal `id` on (INT32_MIN when it painted none). */
static int32_t backdrop_band(uint32_t id) {
    const uint32_t catcher = nt_ui_internal_popup_catcher_id(id);
    for (int32_t i = 0; i < s.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s.ctx->frozen_cmds.internalArray[i];
        if (c->id == catcher) {
            return c->zIndex;
        }
    }
    return INT32_MIN;
}

/* One frame at the pointer's place, no button edge, with a wheel delta and optionally Esc pressed. */
static void wheel_frame(float wheel_dy, bool esc) {
    if (esc) {
        nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    }
    sc_step_wheel(&s, s.x, s.y, -1, -1, wheel_dy);
    if (esc) {
        nt_input_feed(nt_test_key(NT_KEY_ESCAPE, false));
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
    if (modal_begin(MODAL_A, &g.reason_modal_a)) {
        button(MODAL_A_BTN, 80.0F, 30.0F);
        nt_ui_modal_end(s.ctx);
    }
}

static void frame_field_modal(void *game) {
    (void)game;
    base_field(FIELD_BASE, g.buf_base, 50.0F, 50.0F);
    if (modal_begin(MODAL_A, &g.reason_modal_a)) {
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
    if (modal_begin(MODAL_B, NULL)) {
        CLAY({.id = CLAY_ID("sc_panel_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(60)}}}) { button(MODAL_B_BTN, 80.0F, 30.0F); }
        nt_ui_modal_end(s.ctx);
    }
    if (modal_begin(MODAL_A, NULL)) {
        CLAY({.id = CLAY_ID("sc_panel_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(200)}}}) { button(MODAL_A_BTN, 80.0F, 30.0F); }
        nt_ui_modal_end(s.ctx);
    }
}

/* Four centered modals with one button each, declared A, X, Y, B. */
static void frame_four_modals(void *game) {
    (void)game;
    modal_with_button(MODAL_A, MODAL_A_BTN);
    modal_with_button(MODAL_X, MODAL_X_BTN);
    modal_with_button(MODAL_Y, MODAL_Y_BTN);
    modal_with_button(MODAL_B, MODAL_B_BTN);
}

/* Modal A holding a button and modal B, which A's body opens on request. */
static void frame_nested_modals(void *game) {
    (void)game;
    if (modal_begin(MODAL_A, &g.reason_modal_a)) {
        button(MODAL_A_BTN, 80.0F, 30.0F);
        if (g.open_nested) {
            g.open_nested = false;
            nt_ui_modal_open(s.ctx, MODAL_B);
        }
        if (modal_begin(MODAL_B, NULL)) {
            button(MODAL_B_BTN, 80.0F, 30.0F);
            nt_ui_modal_end(s.ctx);
        }
        nt_ui_modal_end(s.ctx);
    }
}

/* Modal A opens modal B from its body on request; B is declared at the root, after A. */
static void frame_modal_opens_root_modal(void *game) {
    (void)game;
    if (!g.skip_modal_a && modal_begin(MODAL_A, NULL)) {
        button(MODAL_A_BTN, 80.0F, 30.0F);
        if (g.open_nested) {
            g.open_nested = false;
            nt_ui_modal_open(s.ctx, MODAL_B);
        }
        nt_ui_modal_end(s.ctx);
    }
    modal_with_button(MODAL_B, MODAL_B_BTN);
}

/* A modal holding a combo with a button declared after it, right under the combo's list. */
static void frame_modal_combo(void *game) {
    (void)game;
    if (modal_begin(MODAL_A, &g.reason_modal_a)) {
        CLAY({.id = CLAY_ID("sc_modal_combo_col"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            combo();
            button(MODAL_A_BTN, 200.0F, 120.0F);
        }
        nt_ui_modal_end(s.ctx);
    }
}

/* A combo at the base, then a modal declared after it. */
static void frame_combo_modal(void *game) {
    (void)game;
    base_combo(30.0F, 30.0F);
    if (modal_begin(MODAL_A, &g.reason_modal_a)) {
        button(MODAL_A_BTN, 80.0F, 30.0F);
        nt_ui_modal_end(s.ctx);
    }
}

/* A combo whose custom row opens, on request, a confirm modal declared at the root after the combo, or popup A
 * declared in the list. */
static void frame_combo_confirm(void *game) {
    (void)game;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(COMBO_A, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        g.combo = nt_ui_combo_begin(s.ctx, NULL, 0U, COMBO_A, "pick", &g.dd_style);
        if (g.combo) {
            for (int i = 0; i < 3; ++i) {
                (void)nt_ui_combo_selectable(s.ctx, ROW_KEY(i), s_rows[i], false);
            }
            nt_ui_combo_selectable_begin(s.ctx, ROW_KEY(3), false);
            CLAY({.id = CLAY_ID("sc_custom_row"), .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(20)}}}) {}
            if (g.open_confirm) {
                g.open_confirm = false;
                nt_ui_modal_open(s.ctx, MODAL_B);
            }
            if (g.open_pop_in_list) {
                g.open_pop_in_list = false;
                nt_ui_popup_open(s.ctx, POP_A);
            }
            (void)nt_ui_combo_selectable_end(s.ctx);
            g.pop_a_shown = popup(POP_A, POP_A_BTN, 300.0F, 300.0F, 0.0F, &g.reason_pop_a, NULL);
            nt_ui_combo_end(s.ctx);
        }
    }
    modal_with_button(MODAL_B, MODAL_B_BTN);
}

/* A combo at (30, 30) whose list holds more rows than it shows, so it scrolls. */
static void frame_long_combo(void *game) {
    (void)game;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(COMBO_A, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        g.combo = nt_ui_combo_begin(s.ctx, NULL, 0U, COMBO_A, "pick", &g.dd_style);
        if (g.combo) {
            for (int i = 0; i < LONG_COMBO_ROWS; ++i) {
                (void)nt_ui_combo_selectable(s.ctx, ROW_KEY(i), s_rows[i % 3], false);
            }
            nt_ui_combo_end(s.ctx);
        }
    }
}

static void frame_inert_popup(void *game) {
    (void)game;
    base_button(BTN_UNDER, 60.0F, 60.0F, 300.0F, 200.0F);
    g.pop_a_shown = popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 4.0F, &g.reason_pop_a, &g.reason_pop_a_now);
}

static void frame_two_popups(void *game) {
    (void)game;
    (void)popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 0.0F, &g.reason_pop_a, NULL);
    (void)popup(POP_B, BTN_B, 400.0F, 100.0F, 0.0F, NULL, NULL);
}

/* Popups A and B, opened in that order when asked, declared B first. */
static void frame_two_popups_reversed(void *game) {
    (void)game;
    if (g.open_two_popups) {
        g.open_two_popups = false;
        nt_ui_popup_open(s.ctx, POP_A);
        nt_ui_popup_open(s.ctx, POP_B);
    }
    (void)popup(POP_B, BTN_B, 400.0F, 100.0F, 0.0F, &g.reason_pop_b, NULL);
    g.pop_a_shown = popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 0.0F, &g.reason_pop_a, NULL);
}

static void frame_popup_button(void *game) {
    (void)game;
    g.pop_a_shown = !g.skip_pop_a && popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 0.0F, &g.reason_pop_a, NULL);
    base_button(BTN_A, 400.0F, 400.0F, 150.0F, 60.0F);
}

/* A game widget that reads popup A's open state before anything else declares, then a button and popup A. */
static void frame_probe_popup(void *game) {
    (void)game;
    g.pop_a_seen_open = nt_ui_is_open(s.ctx, POP_A);
    base_button(BTN_A, 400.0F, 400.0F, 150.0F, 60.0F);
    g.pop_a_shown = popup(POP_A, POP_A_BTN, 100.0F, 100.0F, 0.0F, &g.reason_pop_a, NULL);
}

/* A game widget that reads nt_ui_wants_pointer first, then a button. */
static void frame_probe_pointer(void *game) {
    (void)game;
    g.wants_at_begin = nt_ui_wants_pointer(s.ctx);
    base_button(BTN_A, 400.0F, 400.0F, 150.0F, 60.0F);
}

static void frame_combo_menu(void *game) {
    (void)game;
    base_combo(30.0F, 30.0F);
    menu_tree();
}

/* A combo inside a container that carries a tooltip; reads both hovers after the declaration. */
static void frame_tooltip_combo(void *game) {
    (void)game;
    CLAY({.id = (Clay_ElementId){.id = TIP_BOX}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        nt_ui_block_pointer(s.ctx, TIP_BOX, NULL);
        combo();
    }
    g.tip_seen |= nt_ui_tooltip(s.ctx, NULL, 0U, TIP_BOX, "hint", &g.tip_style);
    g.hover_trigger = nt_ui_hovered_within(s.ctx, combo_trigger());
    g.hover_box = nt_ui_hovered_within(s.ctx, TIP_BOX);
}

/* A row of TIP_N buttons, each with a tooltip. */
static void frame_many_tips(void *game) {
    (void)game;
    for (int i = 0; i < TIP_N; ++i) {
        base_button(TIP_TARGET(i), 20.0F + ((float)i * 40.0F), 100.0F, 30.0F, 30.0F);
    }
    for (int i = 0; i < TIP_N; ++i) {
        g.tip_seen |= nt_ui_tooltip(s.ctx, NULL, 0U, TIP_TARGET(i), "tip", &g.tip_style);
    }
}

/* Modal A holding a button with a tooltip. */
static void frame_modal_tip(void *game) {
    (void)game;
    if (modal_begin(MODAL_A, &g.reason_modal_a)) {
        button(MODAL_A_BTN, 80.0F, 30.0F);
        g.tip_seen |= nt_ui_tooltip(s.ctx, NULL, 0U, MODAL_A_BTN, "tip", &g.tip_style);
        nt_ui_modal_end(s.ctx);
    }
}

/* A popup chain NT_UI_OVERLAY_MAX deep, each level opened inside the one under it; the deepest holds a field and
 * a button with a tooltip. */
static void chain_levels(void) {
    nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
    ps.ease_speed = 0.0F;
    int shown = 0;
    for (int d = 0; d < NT_UI_OVERLAY_MAX; ++d) {
        const float at = 20.0F + ((float)d * 28.0F);
        const nt_ui_popup_anchor_t anc = {.x = at, .y = at, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
        if (!nt_ui_popup_begin(s.ctx, CHAIN_POP(d), &ps, &anc, NULL)) {
            break;
        }
        shown = d + 1;
        button(CHAIN_BTN(d), 40.0F, 20.0F);
        if (d + 1 < NT_UI_OVERLAY_MAX && g.open_chain) {
            nt_ui_popup_open(s.ctx, CHAIN_POP(d + 1));
        } else if (d + 1 == NT_UI_OVERLAY_MAX) {
            field(FIELD_DEEP, g.buf_m1);
            g.tip_seen |= nt_ui_tooltip(s.ctx, NULL, 0U, CHAIN_BTN(d), "tip", &g.tip_style);
        }
    }
    for (; shown > 0; --shown) {
        nt_ui_popup_end(s.ctx);
    }
}

static void frame_popup_chain(void *game) {
    (void)game;
    if (g.open_chain) {
        nt_ui_popup_open(s.ctx, CHAIN_POP(0));
    }
    chain_levels();
    g.open_chain = false;
}

static void frame_menu(void *game) {
    (void)game;
    if (g.reopen_menu) {
        g.reopen_menu = false;
        nt_ui_menu_open(&g.menu, s.ctx, MENU_A, 120.0F, 80.0F); /* after nt_ui_begin chose the menu as the top */
    }
    menu_tree();
}

/* Root: File > (New), a custom row hosting the combo, Edit. */
static void frame_menu_combo_row(void *game) {
    (void)game;
    ++g.frames;
    g.file_body = false;
    if (nt_ui_menu_begin(&g.menu, s.ctx, NULL, 0U, &g.menu_style)) {
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

/* Root: Edit, a custom row hosting a game button; the game reopens the menu elsewhere before declaring it. */
static void frame_menu_button_row(void *game) {
    (void)game;
    if (g.reopen_menu) {
        g.reopen_menu = false;
        nt_ui_menu_open(&g.menu, s.ctx, MENU_A, 300.0F, 200.0F);
    }
    if (nt_ui_menu_begin(&g.menu, s.ctx, NULL, 0U, &g.menu_style)) {
        log_click(ACT_EDIT, nt_ui_menu_item(&g.menu, KEY_EDIT, "Edit"));
        nt_ui_menu_item_begin(&g.menu, KEY_BUTTON_ROW, true, false);
        button(MENU_ROW_BTN, 80.0F, 24.0F);
        (void)nt_ui_menu_item_end(&g.menu);
        nt_ui_menu_end(&g.menu);
    }
}

/* Root menu: Prefs, whose activation opens a modal, and Edit; the modal is declared at the root after it. */
static void frame_menu_prefs(void *game) {
    (void)game;
    if (nt_ui_menu_begin(&g.menu, s.ctx, NULL, 0U, &g.menu_style)) {
        if (nt_ui_menu_item(&g.menu, KEY_PREFS, "Prefs")) {
            log_click(ACT_PREFS, true);
            nt_ui_modal_open(s.ctx, MODAL_A);
        }
        log_click(ACT_EDIT, nt_ui_menu_item(&g.menu, KEY_EDIT, "Edit"));
        nt_ui_menu_end(&g.menu);
    }
    modal_with_button(MODAL_A, MODAL_A_BTN);
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

/* Pointer: input is decided at nt_ui_begin, so right after it a pointer resting on last frame's button is the
 * UI's. */
static void test_pointer_wants_pointer_right_after_begin_over_a_widget(void) {
    scenario(frame_probe_pointer);
    sc_move_to(&s, BTN_A);
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE_MESSAGE(s.wants_pointer, "after nt_ui_end the pointer over the button is the UI's");
    TEST_ASSERT_TRUE_MESSAGE(g.wants_at_begin, "read right after nt_ui_begin it is the UI's as well");
}

/* Gesture: the press that dismisses a combo is the UI's to its release, on empty space too. */
static void test_gesture_combo_dismiss_press_stays_with_the_ui(void) {
    scenario(frame_combo_button);
    sc_click_id(&s, combo_trigger());
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

/* Gesture: a press on empty space is the game's to its release, so the UI wants no pointer while that drag
 * crosses a button or ends over it; a free pointer resting there is the UI's. */
static void test_gesture_game_drag_keeps_the_pointer_through_its_release(void) {
    const nt_button_t buttons[2] = {NT_BUTTON_RIGHT, NT_BUTTON_LEFT};
    scenario(frame_probe_pointer);
    for (int k = 0; k < 2; ++k) {
        sc_move(&s, 700.0F, 100.0F);
        sc_press(&s, buttons[k]);
        TEST_ASSERT_FALSE_MESSAGE(g.wants_at_begin, "the press on nothing starts the game's drag");
        sc_move_to(&s, BTN_A);
        TEST_ASSERT_FALSE_MESSAGE(g.wants_at_begin, "the game's drag moving onto the button");
        sc_frames(&s, 1);
        TEST_ASSERT_FALSE_MESSAGE(g.wants_at_begin, "the game's drag held over the button");
        sc_release(&s, buttons[k]);
        TEST_ASSERT_FALSE_MESSAGE(g.wants_at_begin, "the game's drag released over the button");
        sc_frames(&s, 1);
        TEST_ASSERT_TRUE_MESSAGE(g.wants_at_begin, "a free pointer resting on the button is the UI's");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, clicks(BTN_A), "the game's release clicks nothing");
}
// #endregion

// #region Router: Live
/* Live: a modal opened while a button is held ends the press; the release clicks nothing. */
static void test_live_modal_over_held_press_cancels_the_click(void) {
    scenario(frame_button_modal);
    sc_move_to(&s, BTN_A);
    sc_press(&s, NT_BUTTON_LEFT);
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    sc_frames(&s, 1);
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT(0, g.n);
    nt_ui_close(s_fx.ctx, MODAL_A);
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
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    sc_frames(&s, 1);
    sc_char(&s, 'b');
    TEST_ASSERT_EQUAL_STRING("a", g.buf_base);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s.ctx, FIELD_BASE));
}

/* Live: Tab inside a modal cycles its own fields and never lands on the field under it. */
static void test_live_tab_in_modal_cycles_only_its_fields(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
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

/* Live: a pointer on an open combo's catcher, off the list, hovers neither the trigger nor the container around
 * the combo; on a row of the list the container is hovered within. */
static void test_live_pointer_on_the_catcher_hovers_nothing(void) {
    scenario(frame_tooltip_combo);
    sc_move_to(&s, combo_trigger());
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(g.hover_trigger, "the closed combo's trigger is hovered");
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(g.hover_trigger, "the trigger under the open list's catcher is not hovered");
    TEST_ASSERT_FALSE_MESSAGE(g.hover_box, "nor the container around it");
    sc_move_to(&s, combo_row(1));
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(g.hover_box, "a row of the list is inside the container");
    TEST_ASSERT_TRUE(g.combo);
}
// #endregion

// #region Router: Keys
/* Keys: a focused field in a modal owns the keys; its Esc unfocuses it, the next Esc closes the modal. */
static void test_keys_esc_unfocuses_field_then_closes_modal(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    scenario(frame_field_modal);
    sc_frames(&s, 1);
    sc_click_id(&s, FIELD_M1);
    TEST_ASSERT_TRUE(s.wants_keyboard);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_input_focused(s.ctx, FIELD_M1));
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_A), "the field's Esc does not reach the modal");
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(s.wants_keyboard, "the modal owns the keys now");
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_modal_a);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE(s.wants_keyboard);
}

/* Keys: the Esc that closes a menu was the menu's, so the game sees wants_keyboard in that frame. */
static void test_keys_menu_closing_esc_still_wants_keyboard(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(s.wants_keyboard);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_TRUE_MESSAGE(s.wants_keyboard, "the closing Esc is not the game's");
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE(s.wants_keyboard);
}
// #endregion

// #region Router: Inert
/* Inert: a popup fading out takes no click; the button under its panel does. */
static void test_inert_fading_popup_passes_the_click_through(void) {
    nt_ui_popup_open(s_fx.ctx, POP_A);
    scenario(frame_inert_popup);
    sc_frames(&s, 1);
    sc_click_id(&s, POP_A_BTN);
    TEST_ASSERT_EQUAL_INT(1, clicks(POP_A_BTN));
    TEST_ASSERT_EQUAL_INT(0, clicks(BTN_UNDER));
    nt_ui_close(s_fx.ctx, POP_A);
    sc_click_id(&s, POP_A_BTN);
    TEST_ASSERT_TRUE_MESSAGE(g.pop_a_shown, "the popup still fades out");
    TEST_ASSERT_EQUAL_INT(1, clicks(POP_A_BTN));
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_UNDER));
}

/* Inert: a list that Esc closes in nt_ui_begin is closing that whole frame, so the frame's wheel over it does
 * not scroll it. */
static void test_inert_closing_list_takes_no_new_wheel(void) {
    g.dd_style.max_visible_rows = 3U;
    g.dd_style.open_ease_speed = 4.0F; /* the closing list fades, so its rows stay laid out */
    g.dd_style.list_scroll.wheel_ease_speed = 0.0F;
    scenario(frame_long_combo);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 30); /* the opening tween settles */
    sc_move_to(&s, combo_row(1));
    sc_frames(&s, 1);
    const float y0 = sc_box(&s, combo_row(0)).y;
    wheel_frame(1.0F, false);
    const float y1 = sc_box(&s, combo_row(0)).y;
    TEST_ASSERT_TRUE_MESSAGE(y1 < y0 - 1.0F, "the open list scrolls on the wheel");
    wheel_frame(1.0F, true);
    TEST_ASSERT_TRUE_MESSAGE(g.combo, "the closing list still fades");
    TEST_ASSERT_TRUE_MESSAGE(ui_fx_float_near(sc_box(&s, combo_row(0)).y, y1, 0.5F), "the Esc frame's wheel does not scroll the closing list");
}
// #endregion

// #region Overlay stack: Stack, Opening closes popups, Own close
/* Stack: the modal opened last is on top though declared first. */
static void test_stack_last_opened_modal_is_on_top(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    scenario(frame_two_modals);
    sc_frames(&s, 1);
    nt_ui_modal_open(s_fx.ctx, MODAL_B);
    sc_frames(&s, 2);
    sc_click_id(&s, MODAL_A_BTN);
    TEST_ASSERT_EQUAL_INT(0, g.n);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_A), "the covered modal stays open");
    sc_click_id(&s, MODAL_B_BTN);
    TEST_ASSERT_EQUAL_INT(1, g.n);
    TEST_ASSERT_EQUAL_UINT32(MODAL_B_BTN, g.log[0]);
}

/* Stack: a combo opened inside a modal lies above it: its row wins over the modal's button under the list. */
static void test_stack_combo_in_modal_stays_above_it(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    scenario(frame_modal_combo);
    sc_frames(&s, 1);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    const nt_ui_bbox_t row = sc_box(&s, combo_row(1));
    const nt_ui_bbox_t btn = sc_box(&s, MODAL_A_BTN);
    const float rx = row.x + (row.width * 0.5F);
    const float ry = row.y + (row.height * 0.5F);
    TEST_ASSERT_TRUE_MESSAGE(rx > btn.x && rx < btn.x + btn.width && ry > btn.y && ry < btn.y + btn.height, "the row lies over the modal's button");
    sc_click(&s, rx, ry);
    TEST_ASSERT_EQUAL_INT(1, g.combo_sel);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(g.combo, "the pick closed the list");
    TEST_ASSERT_EQUAL_INT(0, clicks(MODAL_A_BTN));
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_A));
}

/* Stack: a modal reopened during its fade comes back alone: the combo that was open inside it stays closed, and
 * the modal is the top that takes Esc. */
static void test_stack_modal_reopened_in_its_fade_keeps_its_combo_closed(void) {
    g.modal_ease = 8.0F;
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    scenario(frame_modal_combo);
    sc_frames(&s, 60); /* the eased panel settles where it stays */
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    nt_ui_close(s_fx.ctx, MODAL_A);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    nt_ui_modal_open(s_fx.ctx, MODAL_A); /* the fade is still running */
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    TEST_ASSERT_FALSE_MESSAGE(g.combo, "the combo opened inside the old modal stays closed");
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_A), "the reopened modal is the top");
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_modal_a);
}

/* Stack: closing a modal closes the modal opened inside its body at once. */
static void test_stack_close_takes_the_nested_modal(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    scenario(frame_nested_modals);
    g.open_nested = true;
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_B));
    nt_ui_close(s_fx.ctx, MODAL_A);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_B), "the nested modal closes with the one it was opened inside");
    sc_frames(&s, 2);
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_B), "reopening the outer modal does not bring it back");
}

/* Stack: an overlay the game stops declaring closes with the overlays opened inside it, wherever they are
 * declared, so a confirm opened from a settings modal does not outlive it and keep the keys. */
static void test_stack_undeclared_parent_closes_its_children(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    scenario(frame_modal_opens_root_modal);
    sc_frames(&s, 1);
    g.open_nested = true;
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_B));
    TEST_ASSERT_TRUE_MESSAGE(s.wants_keyboard, "the confirm owns the keys");
    g.skip_modal_a = true; /* screen change: the settings modal is no longer declared */
    sc_frames(&s, 3);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_B), "the confirm opened inside the settings closes with it");
    TEST_ASSERT_FALSE_MESSAGE(s.wants_keyboard, "no closed overlay keeps the keys");
}

/* Stack: overlays under the top that leave the stack mid-frame do not drop the top's band under an overlay
 * declared before them: B, opened last, keeps the click on the shared center. */
static void test_stack_top_stays_above_after_lower_overlays_leave_mid_frame(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_X);
    nt_ui_modal_open(s_fx.ctx, MODAL_Y);
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    nt_ui_modal_open(s_fx.ctx, MODAL_B);
    scenario(frame_four_modals);
    sc_frames(&s, 1);
    sc_move_to(&s, MODAL_B_BTN);
    sc_press(&s, NT_BUTTON_LEFT);
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, clicks(MODAL_B_BTN), "with all four open, B is on top");
    g.n = 0;
    nt_ui_close(s_fx.ctx, MODAL_X);
    nt_ui_close(s_fx.ctx, MODAL_Y);
    sc_frames(&s, 1); /* X and Y snap out in their begins, between A's and B's declarations */
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MODAL_X));
    TEST_ASSERT_TRUE_MESSAGE(backdrop_band(MODAL_B) > backdrop_band(MODAL_A), "B paints above A");
    sc_press(&s, NT_BUTTON_LEFT);
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, clicks(MODAL_A_BTN), "A, under the top, takes no click");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, clicks(MODAL_B_BTN), "B, the top, keeps the click");
}

/* Stack: the combo list is the overlay under the combo id, so nt_ui_is_open and nt_ui_close take it. */
static void test_stack_combo_list_answers_by_the_combo_id(void) {
    scenario(frame_combo_button);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, COMBO_A), "the open list answers by the combo's id");
    nt_ui_close(s_fx.ctx, COMBO_A);
    sc_frames(&s, 2);
    TEST_ASSERT_FALSE_MESSAGE(g.combo, "nt_ui_close on the combo's id closes its list");
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, COMBO_A));
}

/* Opening closes popups: a second popup opened at the base closes the first at once; the first reports REPLACED
 * at its next begin. */
static void test_opening_second_popup_closes_the_first(void) {
    nt_ui_popup_open(s_fx.ctx, POP_A);
    scenario(frame_two_popups);
    sc_frames(&s, 1);
    nt_ui_popup_open(s_fx.ctx, POP_B);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, POP_A), "opening B closes A");
    sc_frames(&s, 1);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_REPLACED, g.reason_pop_a);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, POP_B));
}

/* Opening closes popups: the open order decides, not the declaration order: of two popups opened in one frame
 * and declared in reverse, the last opened stays and is the top. */
static void test_opening_two_popups_in_one_frame_keeps_the_last(void) {
    scenario(frame_two_popups_reversed);
    g.open_two_popups = true;
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, POP_A));
    TEST_ASSERT_FALSE(g.pop_a_shown);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_REPLACED, g.reason_pop_a);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, POP_B));
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, POP_B), "B is the top");
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_pop_b);
}

/* Opening closes popups: a modal opened next to an open combo closes it before the next frame declares it, and
 * takes Esc. */
static void test_opening_modal_closes_open_combo(void) {
    scenario(frame_combo_modal);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(g.combo, "the opening modal closes the combo");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MODAL_A));
}

/* Opening closes popups: a confirm modal a combo row opens closes the list, as showModal closes the open
 * popovers, and stays open on top: its button takes the click. */
static void test_opening_confirm_from_combo_row_closes_the_list_and_stays(void) {
    scenario(frame_combo_confirm);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    g.open_confirm = true;
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, COMBO_A), "the confirm closes the list its row opened it from");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_B));
    sc_frames(&s, 2);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_B), "the confirm outlives the list");
    sc_click_id(&s, MODAL_B_BTN);
    TEST_ASSERT_EQUAL_INT(1, clicks(MODAL_B_BTN));
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_B));
}

/* Opening closes popups: a popup a combo row opens is the list's child: the list stays open under it, the popup
 * takes the click, and Esc closes the popup, the top, first. */
static void test_opening_popup_from_combo_row_keeps_the_list(void) {
    scenario(frame_combo_confirm);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    g.open_pop_in_list = true;
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE_MESSAGE(g.combo, "the popup the row opened does not close the list");
    TEST_ASSERT_TRUE(g.pop_a_shown);
    sc_click_id(&s, POP_A_BTN);
    TEST_ASSERT_EQUAL_INT(1, clicks(POP_A_BTN));
    TEST_ASSERT_TRUE(g.combo);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, POP_A));
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, COMBO_A), "the list under it stays open");
}

/* Opening closes popups: a modal a menu item's activation opens is no child of the menu, so the menu closes and
 * the modal stays open on top. */
static void test_opening_modal_from_menu_item_closes_the_menu_and_stays(void) {
    scenario(frame_menu_prefs);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 2);
    sc_click_id(&s, ACT_PREFS);
    TEST_ASSERT_EQUAL_INT(1, clicks(ACT_PREFS));
    sc_frames(&s, 3);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MENU_A), "the activation closes the menu");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_A), "the modal the activation opened stays open");
    sc_click_id(&s, MODAL_A_BTN);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, clicks(MODAL_A_BTN), "the modal takes input");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_A));
}

/* Opening closes popups: opening a menu closes an open combo. */
static void test_opening_menu_closes_open_combo(void) {
    scenario(frame_combo_menu);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 400.0F, 300.0F);
    sc_frames(&s, 2);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

/* Tooltips: a tooltip is no overlay, so showing one next to an open combo keeps it open, and the combo stays the
 * top that takes Esc. */
static void test_tooltip_keeps_open_combo(void) {
    scenario(frame_tooltip_combo);
    sc_click_id(&s, combo_trigger());
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

/* Tooltips: more fading tooltips than the overlay stack holds take no stack slot and no top. */
static void test_tooltip_takes_no_stack_slot(void) {
    g.tip_style.open_ease_speed = 2.0F; /* slow: every tooltip still fades when the last one shows */
    scenario(frame_many_tips);
    for (int i = 0; i < TIP_N; ++i) {
        sc_move_to(&s, TIP_TARGET(i));
        sc_frames(&s, 3);
    }
    TEST_ASSERT_TRUE(g.tip_seen);
    const int32_t tip_band = s_fx.ctx->overlay_zband_stride + (NT_UI_OVERLAY_MAX * NT_UI_OVERLAY_MAX);
    TEST_ASSERT_TRUE_MESSAGE(texts_on_band(tip_band) > NT_UI_OVERLAY_MAX, "more tooltips than stack slots show at once");
    TEST_ASSERT_FALSE_MESSAGE(s.wants_keyboard, "a tooltip is never the top");
}

/* Tooltips: a tooltip in a modal draws on the band above every stack slot, and the modal stays the top. */
static void test_tooltip_over_a_modal_draws_above_it(void) {
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    scenario(frame_modal_tip);
    sc_frames(&s, 1);
    sc_move_to(&s, MODAL_A_BTN);
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(g.tip_seen);
    const int32_t tip_band = s_fx.ctx->overlay_zband_stride + (NT_UI_OVERLAY_MAX * NT_UI_OVERLAY_MAX);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, texts_on_band(tip_band), "the tooltip's text paints above every overlay");
    TEST_ASSERT_TRUE_MESSAGE(s.wants_keyboard, "the modal owns the keys");
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, MODAL_A), "Esc closes the modal under the tooltip");
}

/* A tooltip in the deepest of NT_UI_OVERLAY_MAX nested overlays writes no declaration slot past the array: the
 * field after overlay_decl (focused_input_id) keeps the focus of the field declared before the tooltip. */
static void test_tooltip_in_deepest_overlay_keeps_the_focused_field(void) {
    scenario(frame_popup_chain);
    g.open_chain = true;
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(nt_ui_is_open(s.ctx, CHAIN_POP(NT_UI_OVERLAY_MAX - 1)));
    sc_click_id(&s, FIELD_DEEP);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_input_focused(s.ctx, FIELD_DEEP), "the press focuses the field in the deepest popup");
    sc_move_to(&s, CHAIN_BTN(NT_UI_OVERLAY_MAX - 1));
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(g.tip_seen, "the tooltip shows in the deepest popup");
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_input_focused(s.ctx, FIELD_DEEP), "the tooltip declaration leaves focused_input_id alone");
    TEST_ASSERT_TRUE(nt_ui_is_open(s.ctx, CHAIN_POP(NT_UI_OVERLAY_MAX - 1)));
}

/* Own close: an outside press closes a popup, clicks nothing under it, and the next press reaches it. */
static void test_own_close_outside_click_closes_popup(void) {
    nt_ui_popup_open(s_fx.ctx, POP_A);
    scenario(frame_popup_button);
    sc_frames(&s, 1);
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, POP_A));
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_OUTSIDE, g.reason_pop_a);
    TEST_ASSERT_EQUAL_INT(0, clicks(BTN_A));
    sc_click_id(&s, BTN_A);
    TEST_ASSERT_EQUAL_INT(1, clicks(BTN_A));
}

/* Own close: the outside press and Esc close the top in nt_ui_begin, so a widget declared before the popup in
 * that frame already reads it closed. */
static void test_own_close_runs_before_the_frame_declares(void) {
    nt_ui_popup_open(s_fx.ctx, POP_A);
    scenario(frame_probe_popup);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE(g.pop_a_seen_open);
    sc_move_to(&s, BTN_A);
    TEST_ASSERT_TRUE(g.pop_a_seen_open);
    sc_press(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_FALSE_MESSAGE(g.pop_a_seen_open, "the press frame's first widget reads the popup closed");
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_OUTSIDE, g.reason_pop_a);
    sc_release(&s, NT_BUTTON_LEFT);

    nt_ui_popup_open(s_fx.ctx, POP_A);
    sc_frames(&s, 2);
    TEST_ASSERT_TRUE(g.pop_a_seen_open);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE_MESSAGE(g.pop_a_seen_open, "the Esc frame's first widget reads the popup closed");
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_pop_a);
}

/* Own close: Esc closes the top overlay only, the combo inside the modal; the next Esc closes the modal. */
static void test_own_close_esc_closes_top_combo_only(void) {
    nt_ui_modal_open(s_fx.ctx, MODAL_A);
    g.modal_flags = NT_UI_MODAL_LISTEN_ESC;
    scenario(frame_modal_combo);
    sc_frames(&s, 1);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(g.combo);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MODAL_A));
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_ESC, g.reason_modal_a);
}

/* Own close: a combo list a pick closed registers no catcher in the next frame, so the press made on the frame
 * the pointer reaches a button next to the combo clicks it. */
static void test_own_close_picked_combo_list_passes_the_next_press_through(void) {
    scenario(frame_combo_button);
    sc_click_id(&s, combo_trigger());
    TEST_ASSERT_TRUE(g.combo);
    sc_frames(&s, 1);
    sc_click_id(&s, combo_row(1));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.combo_sel, "the pick closes the list");
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, COMBO_A));
    const nt_ui_bbox_t b = sc_box(&s, BTN_A);
    sc_step(&s, b.x + (b.width * 0.5F), b.y + (b.height * 0.5F), NT_BUTTON_LEFT, -1); /* moves there and presses in one frame */
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, clicks(BTN_A), "the first press after the pick reaches the button");
}

/* Stack: an open between frames (after nt_ui_end, before the next nt_ui_begin) shows the overlay at its next
 * declaration. */
static void test_stack_open_between_frames_shows_next_frame(void) {
    scenario(frame_popup_button);
    TEST_ASSERT_FALSE(g.pop_a_shown);
    nt_ui_popup_open(s_fx.ctx, POP_A);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(g.pop_a_shown, "the popup opened between frames shows at its declaration");
    sc_click_id(&s, POP_A_BTN);
    TEST_ASSERT_EQUAL_INT(1, clicks(POP_A_BTN));
}

/* Stack: an overlay opened but never declared waits one begin, then the next begin closes it. */
static void test_stack_undeclared_open_closes_after_one_begin(void) {
    scenario(frame_overlap);
    nt_ui_popup_open(s_fx.ctx, POP_B);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_is_open(s_fx.ctx, POP_B), "one opened since the last begin waits one more");
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, POP_B), "the second begin closes the overlay no frame declared");
}

/* Stack: an overlay the game leaves out for one frame is closed; declared again without an open, it stays
 * hidden. */
static void test_stack_overlay_left_out_for_a_frame_closes(void) {
    nt_ui_popup_open(s_fx.ctx, POP_A);
    scenario(frame_popup_button);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE(g.pop_a_shown);
    g.skip_pop_a = true;
    sc_frames(&s, 1);
    g.skip_pop_a = false;
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(g.pop_a_shown, "a popup left out for a frame does not come back by itself");
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, POP_A));
}

/* Own close: nt_ui_close reports GAME once, at the next begin, while the popup still fades. */
static void test_own_close_game_close_reports_once(void) {
    nt_ui_popup_open(s_fx.ctx, POP_A);
    scenario(frame_inert_popup);
    sc_frames(&s, 1);
    nt_ui_close(s_fx.ctx, POP_A);
    sc_frames(&s, 1);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CLOSE_GAME, g.reason_pop_a_now);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(g.pop_a_shown, "the popup still fades");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_UI_POPUP_CLOSE_NONE, g.reason_pop_a_now, "the close is reported once");
}
// #endregion

// #region Menu
/* Down, Right, Down, Enter, Down, Enter reach File > Open > File2, which reports its activation once. */
static void test_menu_keys_activate_nested_leaf_once(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
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
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
    sc_frames(&s, 2);
    TEST_ASSERT_EQUAL_INT(1, g.n);
}

/* Right on a focused parent opens its child before the rows declare, so the child shows in the key's frame with
 * focus on its first row; Right on a leaf opens nothing. */
static void test_menu_right_opens_the_child_in_its_frame(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ARROW_DOWN);
    TEST_ASSERT_FALSE(g.file_body);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "File's child declares in the frame Right is pressed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ACT_NEW, g.menu.focus, "its first row has the focus after that frame");

    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ARROW_UP); /* from no focus to the last row, the Edit leaf */
    TEST_ASSERT_EQUAL_UINT32(ACT_EDIT, g.menu.focus);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    sc_frames(&s, 1);
    TEST_ASSERT_FALSE_MESSAGE(menu_level_shown(1U), "Right on a leaf opens no level");
    TEST_ASSERT_EQUAL_UINT32(ACT_EDIT, g.menu.focus);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_EQUAL_INT(0, g.n);
}

/* A menu reopened in a frame after nt_ui_begin takes no key in that frame; the next frame's key acts. */
static void test_menu_reopened_mid_frame_takes_no_key(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 2);
    g.reopen_menu = true;
    sc_key(&s, NT_KEY_ARROW_DOWN);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, g.menu.focus, "the reopening frame's Down is not the menu's");
    sc_key(&s, NT_KEY_ARROW_DOWN);
    TEST_ASSERT_EQUAL_UINT32(ROW_FILE, g.menu.focus);
}

/* A menu reopened mid-frame takes no input in that frame: a game button in its custom row reports no click on
 * the release its reopening frame carries. */
static void test_menu_reopened_mid_frame_custom_row_button_takes_no_click(void) {
    scenario(frame_menu_button_row);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 2);
    sc_click_id(&s, MENU_ROW_BTN);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, clicks(MENU_ROW_BTN), "the button in the custom row clicks while the menu is the top");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    sc_press(&s, NT_BUTTON_LEFT);
    g.reopen_menu = true;
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, clicks(MENU_ROW_BTN), "the reopening frame's release clicks nothing in the menu");
}

/* Closing the menu closes its open levels with it, on an activation and on an outside press: opened again at
 * once, it shows only the root. */
static void test_menu_close_takes_its_levels(void) {
    scenario(frame_menu);
    for (int c = 0; c < 2; ++c) {
        nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
        sc_frames(&s, 1);
        sc_key(&s, NT_KEY_ARROW_DOWN);
        sc_key(&s, NT_KEY_ARROW_RIGHT);
        TEST_ASSERT_TRUE(g.file_body);
        if (c == 0) {
            sc_key(&s, NT_KEY_ENTER); /* activates File > New */
            TEST_ASSERT_EQUAL_UINT32(ACT_NEW, g.log[g.n - 1]);
            TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
            nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
            sc_frames(&s, 1);
        } else {
            sc_move(&s, 780.0F, 580.0F);
            g.reopen_menu = true; /* the game reopens it in the frame the outside press closes it */
            sc_press(&s, NT_BUTTON_LEFT);
            sc_release(&s, NT_BUTTON_LEFT);
        }
        TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
        TEST_ASSERT_FALSE_MESSAGE(g.file_body, c == 0 ? "the activation closed File's level" : "the outside press closed File's level");
        TEST_ASSERT_FALSE(menu_level_shown(1U));
        nt_ui_close(s_fx.ctx, MENU_A);
        sc_frames(&s, 1);
    }
}

/* Esc closes the deepest level first and focuses its parent (Right opens it again), then the menu. */
static void test_menu_esc_closes_deepest_level_first(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_key(&s, NT_KEY_ARROW_DOWN);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    TEST_ASSERT_TRUE(g.file_body);
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_FALSE(g.file_body);
    sc_key(&s, NT_KEY_ARROW_RIGHT);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "the closed level's parent keeps the focus");
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
    sc_key(&s, NT_KEY_ESCAPE);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
    TEST_ASSERT_EQUAL_INT(0, g.n);
}

/* A hover-opened submenu stays open while the pointer crosses sibling rows diagonally toward it for longer than
 * the aim fallback (and under twice it); a leaf there then activates. */
static void test_menu_diagonal_travel_keeps_hovered_submenu(void) {
    scenario(frame_menu);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_move_to(&s, ROW_FILE);
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
    sc_click_id(&s, ACT_QUIT);
    TEST_ASSERT_EQUAL_INT(1, g.n);
    TEST_ASSERT_EQUAL_UINT32(ACT_QUIT, g.log[0]);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, MENU_A));
}

/* A combo opened from a custom menu row closes the sibling submenu, well before any hover switch could. */
static void test_menu_combo_from_custom_row_closes_sibling_submenu(void) {
    scenario(frame_menu_combo_row);
    nt_ui_menu_open(&g.menu, s_fx.ctx, MENU_A, 120.0F, 80.0F);
    sc_frames(&s, 1);
    sc_move_to(&s, ROW_FILE);
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE(g.file_body);
    const int left_row = g.frames;
    sc_move_to(&s, combo_trigger());
    sc_press(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "the submenu is still open when the combo's trigger is pressed");
    sc_release(&s, NT_BUTTON_LEFT);
    TEST_ASSERT_TRUE(g.combo);
    TEST_ASSERT_TRUE_MESSAGE(g.file_body, "the submenu, declared before the combo, still showed in that frame");
    sc_frames(&s, 1);
    TEST_ASSERT_TRUE_MESSAGE(g.frames - left_row < AIM_FALLBACK_FRAMES, "the frames since the pointer left the row stay under the aim fallback");
    TEST_ASSERT_FALSE_MESSAGE(g.file_body, "the combo replaces the sibling submenu");
    TEST_ASSERT_TRUE(g.combo);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, MENU_A));
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pointer_front_most_of_two_buttons_takes_the_click);
    RUN_TEST(test_pointer_wants_pointer_right_after_begin_over_a_widget);
    RUN_TEST(test_gesture_combo_dismiss_press_stays_with_the_ui);
    RUN_TEST(test_gesture_left_press_during_game_right_drag_clicks_nothing);
    RUN_TEST(test_gesture_game_drag_keeps_the_pointer_through_its_release);
    RUN_TEST(test_live_modal_over_held_press_cancels_the_click);
    RUN_TEST(test_live_field_under_modal_stops_typing);
    RUN_TEST(test_live_tab_in_modal_cycles_only_its_fields);
    RUN_TEST(test_live_pointer_on_the_catcher_hovers_nothing);
    RUN_TEST(test_keys_esc_unfocuses_field_then_closes_modal);
    RUN_TEST(test_keys_menu_closing_esc_still_wants_keyboard);
    RUN_TEST(test_inert_fading_popup_passes_the_click_through);
    RUN_TEST(test_inert_closing_list_takes_no_new_wheel);
    RUN_TEST(test_stack_last_opened_modal_is_on_top);
    RUN_TEST(test_stack_combo_in_modal_stays_above_it);
    RUN_TEST(test_stack_modal_reopened_in_its_fade_keeps_its_combo_closed);
    RUN_TEST(test_stack_close_takes_the_nested_modal);
    RUN_TEST(test_stack_undeclared_parent_closes_its_children);
    RUN_TEST(test_stack_top_stays_above_after_lower_overlays_leave_mid_frame);
    RUN_TEST(test_stack_combo_list_answers_by_the_combo_id);
    RUN_TEST(test_opening_second_popup_closes_the_first);
    RUN_TEST(test_opening_two_popups_in_one_frame_keeps_the_last);
    RUN_TEST(test_opening_modal_closes_open_combo);
    RUN_TEST(test_opening_confirm_from_combo_row_closes_the_list_and_stays);
    RUN_TEST(test_opening_popup_from_combo_row_keeps_the_list);
    RUN_TEST(test_opening_modal_from_menu_item_closes_the_menu_and_stays);
    RUN_TEST(test_opening_menu_closes_open_combo);
    RUN_TEST(test_tooltip_keeps_open_combo);
    RUN_TEST(test_tooltip_takes_no_stack_slot);
    RUN_TEST(test_tooltip_over_a_modal_draws_above_it);
    RUN_TEST(test_tooltip_in_deepest_overlay_keeps_the_focused_field);
    RUN_TEST(test_own_close_outside_click_closes_popup);
    RUN_TEST(test_own_close_runs_before_the_frame_declares);
    RUN_TEST(test_own_close_esc_closes_top_combo_only);
    RUN_TEST(test_own_close_picked_combo_list_passes_the_next_press_through);
    RUN_TEST(test_stack_open_between_frames_shows_next_frame);
    RUN_TEST(test_stack_undeclared_open_closes_after_one_begin);
    RUN_TEST(test_stack_overlay_left_out_for_a_frame_closes);
    RUN_TEST(test_own_close_game_close_reports_once);
    RUN_TEST(test_menu_keys_activate_nested_leaf_once);
    RUN_TEST(test_menu_right_opens_the_child_in_its_frame);
    RUN_TEST(test_menu_reopened_mid_frame_takes_no_key);
    RUN_TEST(test_menu_reopened_mid_frame_custom_row_button_takes_no_click);
    RUN_TEST(test_menu_close_takes_its_levels);
    RUN_TEST(test_menu_esc_closes_deepest_level_first);
    RUN_TEST(test_menu_diagonal_travel_keeps_hovered_submenu);
    RUN_TEST(test_menu_combo_from_custom_row_closes_sibling_submenu);
    return UNITY_END();
}
