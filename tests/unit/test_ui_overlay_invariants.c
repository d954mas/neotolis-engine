/* Randomized check of the overlay rules a game observes (spec chapter UI, "Input ownership", "Immediate menu +
 * combo"). Fixed seeds drive opens, closes, reopens and nested opens made before nt_ui_begin, between and inside
 * the declarations, modals opened from menu, combo and popup bodies, popups and the menu opened from a menu
 * activation or a combo pick, paused (dt = 0) spells that pile up more fades than nt_ui_begin keeps, a shuffled
 * root declaration order with units left out, pointer moves, left, right and middle presses, releases and wheel, Esc, Down,
 * Right and Enter; the engine's own asserts (stack limits) fire in the driver. Every frame checks, through
 * nt_ui_is_open, the clicks and the router answers: OPENING and CLOSING bodies take no click, nothing clicks through
 * to the base UI under an overlay, an open overlay has an open parent, the gesture keeps its pointer owner, the
 * router answers stay fixed for the frame, and what a modal, an activation or a pick opened outlives what opened
 * it. The stack is read only to pace opens and to learn the submenu level's id. Every invariant counts the cases it
 * checked, and a count of 0 fails the run. */

#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_scenario.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_dropdown.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_menu.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_popup.h"
#include "unity.h"

#define SEEDS 48
#define FRAMES 1000
#define OPENS_PER_FRAME 2 /* pacing; stack_room keeps the slots under NT_UI_OVERLAY_MAX */
#define ENGINE_OPENS 2    /* the combo list and the submenu the engine may still open in a frame */
#define FADE_N 6          /* slow popups a spell opens one inside another, so their fades pile up */
#define SPELL_BUILD 16
#define SPELL_LEN 32

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

// #region ids
enum { OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_LIST, OV_MENU, OV_SUB, OV_F0, OV_N = OV_F0 + FADE_N, OV_BASE = OV_N };
enum { INV_DEAF, INV_THROUGH, INV_TREE, INV_GESTURE, INV_ROUTER, INV_OUTLIVES, INV_N };

static const char *const s_inv_name[] = {"OPENING/CLOSING body takes no click", "no click through to the base UI", "open overlay has an open parent",
                                         "gesture keeps its pointer owner",     "router fixed at begin",           "an overlay opened from a light one outlives it"};

#define CATCHERS (((1U << OV_N) - 1U) & ~(1U << OV_SUB)) /* every overlay but a submenu level has a catcher */

#define ID_COMBO 0x6A0010U /* the list is the overlay under the combo id */
#define ID_MENU 0x6A0020U
#define ID_F(i) (0x6A0030U + (uint32_t)(i))
#define BOX_F(i) (0x6A0040U + (uint32_t)(i))
#define BTN(ov) (0x6A0100U + (uint32_t)(ov)) /* the body button of P0..P2 and the menu's custom row */
#define BTN_BASE0 0x6A0200U
#define BTN_BASE1 0x6A0201U
#define ROW_KEY(i) (0x6A0300U + (uint32_t)(i))
#define KEY_SUB 1U
#define KEY_EDIT 2U
#define KEY_ROW 3U
#define KEY_L1 10U
#define KEY_L2 11U
#define COMBO_ROWS 6       /* over max_visible_rows, so the list scrolls */
#define COMBO_CUSTOM_ROW 1 /* the row whose body may open a modal */
// #endregion

// #region world
typedef struct {
    uint32_t id;
    bool modal;
    bool closing; /* it or a body around it declared closed: nothing in it is live */
    bool deaf;    /* its body takes no input: not OPEN, or inside a body that is not */
} decl_t;

/* One seed's world: zeroed by run_seed. */
typedef struct {
    nt_ui_context_t *ctx;
    sc_t sc;
    nt_ui_menu_style_t menu_style;
    nt_ui_menu_t menu;
    nt_ui_dropdown_style_t dd_style;
    uint32_t rng, seed;
    int frame;
    float ease;      /* P1, the modals and the list; a slow seed keeps their fades up for many frames */
    float snap_ease; /* P0 and P2: 0 snaps, else they fade too */
    int combo_sel;
    uint32_t sub_id;       /* the menu's submenu level, found on the stack once it opens */
    uint32_t parent[OV_N]; /* the overlay each was opened inside, as its open call saw the declarations (0 = root) */
    uint32_t live_prev;    /* bit per OV_*: declared live last frame (open, outside a closed body), not closed or restarted since */
    uint32_t live_now;     /* the same for this frame's declarations */
    int top;               /* nt_ui_begin chose a top: 1 yes, 0 no, -1 unknown */
    int press_top;         /* `top` of the frame of the last left press */
    int drag_ui;           /* the owner of the running gesture: 1 the UI, 0 the game, -1 unknown */
    uint32_t held_btn;     /* the body button the held left press landed on (0 = none) */
    int chain_n, opens_left, spell;
    int armed_src, armed_ov; /* an open from inside a light overlay or by a row's command, until a later op touches the stack */
    bool armed_by_row;
    decl_t chain[NT_UI_OVERLAY_MAX];
    bool was_open[OV_N];
    bool shown_prev[OV_N]; /* its begin returned true last frame, and it was not opened since */
    bool shown_now[OV_N];  /* the same for this frame's declarations */
    bool quiet;            /* the spell builds its chain: no random op, press or key breaks it */
    bool armed, wants_pointer, wants_keyboard, held_before, press_any, press_left, release_left, drag_active;
} world_t;

/* Kept across seeds: the first failure of each invariant and what each checked. */
typedef struct {
    int fail_count[INV_N];
    char fail_msg[INV_N][320];
    int deaf_releases, overlay_clicks, base_clicks, base_releases_under_top, cascades, opens_in_closed, top_presses, free_presses, joins, holds, free_pointers;
    int wants_pointer, wants_keyboard, modal_from[OV_N], activation_opens, pick_opens, fade_cuts;
} results_t;

static world_t w;
static results_t r;

static uint32_t rnd(void) {
    w.rng ^= w.rng << 13U;
    w.rng ^= w.rng >> 17U;
    w.rng ^= w.rng << 5U;
    return w.rng;
}

static uint32_t rnd_below(uint32_t n) { return rnd() % n; }
static bool chance(uint32_t percent) { return rnd_below(100U) < percent; }
static uint32_t bit(int ov) {
    TEST_ASSERT_TRUE(ov >= 0 && ov < OV_N); /* bounds ov for clang-analyzer, which loses it on some paths */
    return 1U << (uint32_t)ov;
}
static bool is_modal(int ov) { return ov == OV_M0 || ov == OV_M1; }

static uint32_t ov_id(int ov) {
    if (ov == OV_SUB) {
        return w.sub_id;
    }
    if (ov == OV_LIST) {
        return ID_COMBO;
    }
    if (ov == OV_MENU) {
        return ID_MENU;
    }
    return (ov >= OV_F0) ? ID_F(ov - OV_F0) : 0x6A0001U + (uint32_t)ov;
}

__attribute__((format(printf, 2, 3))) static void fail(int inv, const char *fmt, ...) {
    if (r.fail_count[inv]++ != 0) {
        return;
    }
    char what[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    (void)snprintf(r.fail_msg[inv], sizeof r.fail_msg[inv], "%s: seed %u frame %d: %s", s_inv_name[inv], (unsigned)w.seed, w.frame, what);
}
// #endregion

// #region open state
static bool is_open(int ov) { return ov_id(ov) != 0U && nt_ui_is_open(w.ctx, ov_id(ov)); }

static void find_sub(void) {
    for (uint8_t i = 0U; w.sub_id == 0U && i < w.ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &w.ctx->overlays[i];
        if (o->id != 0U && o->parent == ID_MENU && o->opener != 0U) {
            w.sub_id = o->id;
        }
    }
}

/* A look at the open state after anything that may change it. A closed overlay loses its live declaration, so its
 * reopen is OPENING; an open the engine makes (the combo list, the submenu) takes its parent and ends an armed check a
 * later S2 close could break; every open overlay's parent is open. */
static void look(void) {
    find_sub();
    for (int ov = 0; ov < OV_N; ++ov) {
        const bool open = is_open(ov);
        if (!open) {
            w.live_prev &= ~bit(ov);
            w.live_now &= ~bit(ov);
            r.cascades += (w.was_open[ov] && w.parent[ov] != 0U && !nt_ui_is_open(w.ctx, w.parent[ov])) ? 1 : 0;
        } else if (!w.was_open[ov] && (ov == OV_LIST || ov == OV_SUB)) {
            w.parent[ov] = (ov == OV_SUB) ? ID_MENU : 0U;
            w.armed = false;
            w.shown_prev[ov] = false; /* the engine opened it again: a new stack entry */
            w.shown_now[ov] = false;
        }
        w.was_open[ov] = open;
    }
    for (int ov = 0; ov < OV_N; ++ov) {
        if (w.was_open[ov] && w.parent[ov] != 0U && !nt_ui_is_open(w.ctx, w.parent[ov])) {
            fail(INV_TREE, "overlay %d open although %x, the overlay it was opened inside, is not", ov, (unsigned)w.parent[ov]);
        }
    }
}

/* Where an open made now lands: inside the innermost open overlay being declared, for a modal the innermost open
 * modal (spec "Input ownership", rule 3). */
static uint32_t open_here(bool modal) {
    for (int i = w.chain_n - 1; i >= 0; --i) {
        if (nt_ui_is_open(w.ctx, w.chain[i].id) && (!modal || w.chain[i].modal)) {
            return w.chain[i].id;
        }
    }
    return 0U;
}

static bool in_chain(int ov) {
    for (int i = 0; i < w.chain_n; ++i) {
        if (w.chain[i].id == ov_id(ov)) {
            return true;
        }
    }
    return false;
}

static bool box_has(uint32_t id, float x, float y) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, id);
    return bb.found && x >= bb.x && x < bb.x + bb.width && y >= bb.y && y < bb.y + bb.height;
}
// #endregion

// #region checks at nt_ui_begin
/* The press that starts a gesture decides its owner, the UI's when it has a target (a record, or the top's
 * catcher, which covers the whole window in 2D); a press of any button while another one is held, or released in
 * that frame, joins it; the owner holds nt_ui_wants_pointer to the last release, and the game's gesture has no
 * target; a free pointer is the UI's when it has one. */
static void check_gesture(void) {
    const bool hot = nt_ui_pointer_hot(w.ctx, 0U).id != 0U;
    if (w.press_any && !w.held_before) {
        w.drag_active = true;
        w.drag_ui = hot ? 1 : 0;
        r.top_presses += (w.top == 1) ? 1 : 0;
        r.free_presses += (w.top == 0) ? 1 : 0;
        if (w.top == 1 && !hot) {
            fail(INV_GESTURE, "a press under a top overlay has no target: the catcher covers the window");
        }
        if (w.wants_pointer != hot) {
            fail(INV_GESTURE, "nt_ui_wants_pointer %d on the press that starts a gesture (hot %d, top %d)", (int)w.wants_pointer, (int)hot, w.top);
        }
    } else if (w.drag_active) {
        r.joins += w.press_any ? 1 : 0;
        r.holds += w.press_any ? 0 : 1;
        if (w.wants_pointer != (w.drag_ui == 1)) {
            fail(INV_GESTURE, "nt_ui_wants_pointer %d during a gesture owned by the %s", (int)w.wants_pointer, (w.drag_ui == 1) ? "UI" : "game");
        }
        if (w.drag_ui == 0 && hot) {
            fail(INV_GESTURE, "the game's gesture has a target");
        }
    } else {
        ++r.free_pointers;
        if (w.wants_pointer != hot) {
            fail(INV_GESTURE, "nt_ui_wants_pointer %d for a free pointer, hot %d", (int)w.wants_pointer, (int)hot);
        }
    }
    if (!w.sc.down[NT_BUTTON_LEFT] && !w.sc.down[NT_BUTTON_RIGHT] && !w.sc.down[NT_BUTTON_MIDDLE]) {
        w.drag_active = false;
    }
}

/* The top is the highest open overlay with a catcher declared live last frame (S1): one of those still open after
 * nt_ui_begin is the top or lies under it; with none there is no top; when nt_ui_begin closed them all, S3 may have
 * closed the top or S1 its parent, so the top is unknown. */
static void after_begin(void) {
    const uint32_t candidates = w.live_prev & CATCHERS;
    look();
    w.top = (candidates == 0U) ? 0 : -1;
    if ((w.live_prev & CATCHERS) != 0U) {
        w.top = 1;
    }
    w.wants_pointer = nt_ui_wants_pointer(w.ctx, 0U);
    w.wants_keyboard = nt_ui_wants_keyboard(w.ctx);
    r.wants_pointer += w.wants_pointer ? 1 : 0;
    r.wants_keyboard += w.wants_keyboard ? 1 : 0;
    check_gesture();
    w.press_top = w.press_left ? w.top : w.press_top;
}
// #endregion

// #region operations
/* Room for one more open and for the opens the engine may still make this frame: every open may add an entry. */
static bool stack_room(void) { return w.ctx->overlay_count + 1U + ENGINE_OPENS <= NT_UI_OVERLAY_MAX; }

/* Opens `ov` with the call a game makes. An open of an overlay that is not open, a reopen in its fade, and any menu
 * open (it restarts the menu) take a new parent and make the next declaration OPENING. */
static void open_now(int ov) {
    look(); /* a close the engine made since the last look, as an activation's */
    const bool was = is_open(ov);
    const uint32_t parent = open_here(is_modal(ov));
    r.opens_in_closed += (w.chain_n > 0 && !nt_ui_is_open(w.ctx, w.chain[w.chain_n - 1].id)) ? 1 : 0;
    w.armed = false;
    if (is_modal(ov)) {
        nt_ui_modal_open(w.ctx, ov_id(ov));
    } else if (ov == OV_MENU) {
        const float x = 40.0F + (float)rnd_below(520U);
        const float y = 40.0F + (float)rnd_below(380U);
        nt_ui_menu_open(&w.menu, w.ctx, ID_MENU, x, y);
    } else {
        nt_ui_popup_open(w.ctx, ov_id(ov));
    }
    if (ov == OV_MENU || !was) {
        w.parent[ov] = parent;
        w.live_prev &= ~bit(ov);
        w.live_now &= ~bit(ov);
        w.shown_prev[ov] = false; /* a new stack entry: the shown one is dead or gone */
        w.shown_now[ov] = false;
    }
    look();
}

static void op_open(int ov) {
    if (w.quiet || w.opens_left <= 0 || in_chain(ov) || !stack_room()) {
        return; /* an overlay is not opened inside its own declaration */
    }
    --w.opens_left;
    open_now(ov);
}

static void op_close(int ov) {
    if (ov_id(ov) != 0U) {
        nt_ui_close(w.ctx, ov_id(ov));
        w.armed = false;
        look();
    }
}

/* Arms the check that `ov`, just opened from inside the light overlay `src` or by its row's command, is open after
 * the frame and `src` closed: a modal closes the light overlay it was opened from (S2, as showModal); an activated
 * menu item or a picked combo row closes its menu or list before it returns (M4). */
static void arm(int src, int ov, bool by_row) {
    w.armed = true;
    w.armed_by_row = by_row;
    w.armed_src = src;
    w.armed_ov = ov;
}

/* A modal opened from inside the open light overlay `opener` (a menu level, the combo list, a popup) while it is
 * declared. */
static void op_modal_from(int opener, int modal) {
    if (w.quiet || w.opens_left <= 0 || !stack_room() || is_open(modal) || !is_open(opener)) {
        return;
    }
    --w.opens_left;
    open_now(modal);
    arm(opener, modal, false);
}

/* A game command run on a menu activation or a combo pick: it opens a popup declared at the root, or from a pick
 * the menu. */
static void op_respond(int src) {
    static const int from_menu[] = {OV_P0, OV_P1};
    static const int from_pick[] = {OV_P0, OV_P1, OV_MENU};
    if (w.quiet || !chance(40U) || !stack_room()) {
        return;
    }
    const int ov = (src == OV_LIST) ? from_pick[rnd_below(3U)] : from_menu[rnd_below(2U)];
    open_now(ov);
    arm(src, ov, true);
}

static void maybe_op(uint32_t percent) {
    if (w.quiet || !chance(percent)) {
        return;
    }
    static const int openable[] = {OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_MENU, OV_MENU}; /* the menu closes on any outside press */
    const uint32_t k = rnd_below(100U);
    if (k < 40U) {
        op_open(openable[rnd_below(7U)]);
    } else if (k < 70U) {
        op_close((int)rnd_below(OV_F0));
    } else {
        /* A reopen during the fade, often of the overlay whose body button the held left press landed on. */
        const int ov = (w.held_btn != 0U) ? (int)(w.held_btn - BTN(0)) : openable[rnd_below(6U)];
        if (!in_chain(ov)) {
            op_close(ov);
            op_open(ov);
        }
    }
}

/* A fade spell: modals and a chain of slow popups, each opened inside the one before (so none closes another),
 * rise undisturbed; then one close ends them all at once, more fades than nt_ui_begin keeps (S4), and a paused tail
 * (dt = 0) holds those fades over many begins. */
static void spell_op(void) {
    if (w.spell == 1) {
        for (int ov = OV_M0; ov <= OV_M1; ++ov) {
            if (!is_open(ov) && stack_room()) {
                open_now(ov);
            }
        }
    } else if (w.spell == 2 && stack_room()) {
        open_now(OV_F0);
    } else if (w.spell == SPELL_BUILD + 1) {
        op_close(OV_F0);
        op_close(OV_M0);
        op_close(OV_M1);
    }
}
// #endregion

// #region declarations
/* A shown declaration. Its body takes input only when it is open and was declared live last frame (OPEN, spec
 * "Overlay lifecycle"), inside a body that takes input; it is live unless it or a body around it is closed. */
static void decl_push(int ov, bool open) {
    look();
    const decl_t *out = (w.chain_n > 0) ? &w.chain[w.chain_n - 1] : NULL;
    const bool closing = !open || (out != NULL && out->closing);
    const bool deaf = !open || (w.live_prev & bit(ov)) == 0U || (out != NULL && out->deaf);
    w.chain[w.chain_n++] = (decl_t){.id = ov_id(ov), .modal = is_modal(ov), .closing = closing, .deaf = deaf};
    w.live_now |= closing ? 0U : bit(ov);
    w.shown_now[ov] = true;
}

static bool body_deaf(void) { return w.chain_n > 0 && w.chain[w.chain_n - 1].deaf; }

static void on_click(int ov, uint32_t what) {
    if (body_deaf()) {
        fail(INV_DEAF, "%x of overlay %d reports a click in a body that takes no input", (unsigned)what, ov);
    }
    if (ov != OV_BASE) {
        ++r.overlay_clicks;
    } else if (w.top == 1 || w.press_top == 1) {
        fail(INV_THROUGH, "base %x clicked although an overlay was the top on its press frame (%d) or now (%d)", (unsigned)what, w.press_top, w.top);
    } else {
        ++r.base_clicks;
    }
}

static void button(uint32_t id, int ov, float wd, float ht) {
    CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_FIXED(wd), CLAY_SIZING_FIXED(ht)}}}) {}
    const bool released_on = w.release_left && box_has(id, w.sc.x, w.sc.y);
    r.deaf_releases += (body_deaf() && released_on) ? 1 : 0;
    r.base_releases_under_top += (ov == OV_BASE && (w.top == 1 || w.press_top == 1) && released_on) ? 1 : 0;
    if (nt_ui_step(w.ctx, id, NULL).clicked) {
        on_click(ov, id);
    }
}

static void decl_base(uint32_t id, float x, float y) {
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(id, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = x, .y = y}}}) { button(id, OV_BASE, 320.0F, 220.0F); }
}

/* A popup (`modal_flags` 0) or a modal holding its body button; `inner` declares the rest of its body. */
static void decl_overlay(int ov, float x, float y, float ease, uint8_t modal_flags, void (*inner)(void)) {
    const bool open = is_open(ov);
    if (is_modal(ov)) {
        nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
        ms.ease_speed = ease;
        ms.flags = modal_flags;
        if (!nt_ui_modal_begin(w.ctx, ov_id(ov), &ms, NULL)) {
            return;
        }
    } else {
        nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
        ps.ease_speed = ease;
        const nt_ui_popup_anchor_t anc = {.x = x, .y = y, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
        if (!nt_ui_popup_begin(w.ctx, ov_id(ov), &ps, &anc, NULL)) {
            return;
        }
    }
    decl_push(ov, open);
    button(BTN(ov), ov, 120.0F, 40.0F);
    maybe_op(5U);
    if (inner != NULL) {
        inner();
    }
    --w.chain_n;
    if (is_modal(ov)) {
        nt_ui_modal_end(w.ctx);
    } else {
        nt_ui_popup_end(w.ctx);
    }
}

/* P1's body opens M1, declared at the root; M0's body holds P2 and opens P0, declared at the root, so each closes
 * with the overlay it was opened inside though declared elsewhere. */
static void inner_p1(void) {
    if (!chance(8U)) {
        return;
    }
    if (is_open(OV_P1)) {
        op_modal_from(OV_P1, OV_M1);
    } else {
        op_open(OV_M1);
    }
}

static void inner_m0(void) {
    if (chance(15U)) {
        op_open(OV_P2);
    }
    if (chance(10U)) {
        op_open(OV_P0);
    }
    decl_overlay(OV_P2, 420.0F, 330.0F, w.snap_ease, 0U, NULL);
}

static void decl_combo(void) {
    static const char *const rows[] = {"Alpha", "Beta", "Gamma"};
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(ID_COMBO, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 300.0F}}}) {
        if (nt_ui_combo_begin(w.ctx, NULL, 0U, ID_COMBO, "pick", &w.dd_style)) {
            decl_push(OV_LIST, is_open(OV_LIST)); /* the trigger opens the list inside the begin */
            for (int i = 0; i < COMBO_ROWS; ++i) {
                bool picked = false;
                if (i == COMBO_CUSTOM_ROW) {
                    nt_ui_combo_selectable_begin(w.ctx, ROW_KEY(i), w.combo_sel == i);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(90), CLAY_SIZING_FIXED(20)}}}) {}
                    if (chance(4U)) {
                        op_modal_from(OV_LIST, OV_M0);
                    }
                    picked = nt_ui_combo_selectable_end(w.ctx);
                } else {
                    picked = nt_ui_combo_selectable(w.ctx, ROW_KEY(i), rows[i % 3], w.combo_sel == i);
                }
                if (picked) {
                    w.combo_sel = i;
                    on_click(OV_LIST, ROW_KEY(i));
                    op_respond(OV_LIST);
                }
            }
            maybe_op(5U);
            --w.chain_n;
            nt_ui_combo_end(w.ctx);
        }
    }
}

static void menu_leaf(int ov, uint32_t key, const char *label) {
    if (nt_ui_menu_item(&w.menu, key, label)) {
        on_click(ov, key);
        op_respond(OV_MENU);
    }
}

static void decl_menu(void) {
    const bool open = is_open(OV_MENU);
    if (!nt_ui_menu_begin(&w.menu, w.ctx, NULL, 0U, &w.menu_style)) {
        return;
    }
    decl_push(OV_MENU, open);
    if (nt_ui_menu_submenu_begin(&w.menu, KEY_SUB, "Sub")) {
        find_sub();
        decl_push(OV_SUB, is_open(OV_SUB)); /* Right or Enter opens it inside the begin */
        if (chance(4U)) {
            op_modal_from(OV_SUB, OV_M0);
        }
        menu_leaf(OV_SUB, KEY_L1, "Leaf1");
        menu_leaf(OV_SUB, KEY_L2, "Leaf2");
        --w.chain_n;
        nt_ui_menu_submenu_end(&w.menu);
    }
    menu_leaf(OV_MENU, KEY_EDIT, "Edit");
    nt_ui_menu_item_begin(&w.menu, KEY_ROW, true, false);
    button(BTN(OV_MENU), OV_MENU, 120.0F, 40.0F);
    if (chance(4U)) {
        op_modal_from(OV_MENU, OV_M0);
    }
    (void)nt_ui_menu_item_end(&w.menu);
    --w.chain_n;
    nt_ui_menu_end(&w.menu);
}

/* The spell's chain: slow popups with nothing to click, each declared inside the one before; while the spell
 * builds, each open level opens the next, and the deepest opens P1 (declared at the root). */
static void decl_fades(void) {
    nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
    ps.ease_speed = 0.7F;
    int shown = 0;
    for (int i = 0; i < FADE_N; ++i) {
        const nt_ui_popup_anchor_t anc = {.x = 480.0F + ((float)i * 40.0F), .y = 40.0F + ((float)i * 30.0F), .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
        const bool open = is_open(OV_F0 + i);
        if (!nt_ui_popup_begin(w.ctx, ID_F(i), &ps, &anc, NULL)) {
            break;
        }
        ++shown;
        decl_push(OV_F0 + i, open);
        CLAY({.id = (Clay_ElementId){.id = BOX_F(i)}, .layout = {.sizing = {CLAY_SIZING_FIXED(30), CLAY_SIZING_FIXED(20)}}}) {}
        const int next = (i + 1 < FADE_N) ? OV_F0 + i + 1 : OV_P1;
        if (w.quiet && open && stack_room() && !is_open(next)) {
            open_now(next);
        }
    }
    for (; shown > 0; --shown) {
        --w.chain_n;
        nt_ui_popup_end(w.ctx);
    }
}

static void unit_base0(void) { decl_base(BTN_BASE0, 60.0F, 60.0F); }
static void unit_base1(void) { decl_base(BTN_BASE1, 340.0F, 240.0F); }
static void unit_p0(void) { decl_overlay(OV_P0, 100.0F, 100.0F, w.snap_ease, 0U, NULL); }
static void unit_p1(void) { decl_overlay(OV_P1, 170.0F, 150.0F, w.ease, 0U, inner_p1); }
static void unit_m0(void) { decl_overlay(OV_M0, 0.0F, 0.0F, w.ease, (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP), inner_m0); }
static void unit_m1(void) { decl_overlay(OV_M1, 0.0F, 0.0F, w.ease, NT_UI_MODAL_LISTEN_ESC, NULL); }

static void frame_fn(void *game) {
    (void)game;
    after_begin();
    void (*units[])(void) = {unit_base0, unit_base1, unit_p0, unit_p1, unit_m0, unit_m1, decl_combo, decl_menu, decl_fades};
    const int n = (int)(sizeof units / sizeof units[0]);
    for (int i = n - 1; i > 0; --i) {
        const int j = (int)rnd_below((uint32_t)i + 1U);
        void (*t)(void) = units[i];
        units[i] = units[j];
        units[j] = t;
    }
    for (int i = 0; i < n; ++i) {
        maybe_op(2U);
        if (!chance(4U)) { /* else the game leaves the unit out: its overlays close at the next begin */
            units[i]();
        }
    }
    maybe_op(2U);
    if (nt_ui_wants_pointer(w.ctx, 0U) != w.wants_pointer || nt_ui_wants_keyboard(w.ctx) != w.wants_keyboard) {
        fail(INV_ROUTER, "pointer %d keyboard %d after begin, changed before end", (int)w.wants_pointer, (int)w.wants_keyboard);
    }
}
// #endregion

// #region driver
/* Aims at what the last frame laid out, so every shown overlay gets clicks: the submenu's parent row and then a
 * leaf of the level it opened while the menu is open, a row while the list is, the combo trigger while no overlay
 * is up. */
static void pick_pointer(float *x, float *y) {
    const uint32_t sub_row = nt_ui_menu_test_item_id(ID_MENU, KEY_SUB);
    const uint32_t leaf = nt_ui_menu_test_item_id(sub_row, chance(50U) ? KEY_L1 : KEY_L2);
    const uint32_t others[] = {nt_ui_menu_test_item_id(ID_MENU, KEY_EDIT), sub_row, leaf, BTN_BASE0, BTN_BASE1};
    const uint32_t k = rnd_below(100U);
    const bool at_sub = is_open(OV_MENU) && chance(40U);
    const bool at_list = is_open(OV_LIST) && chance(40U);
    const bool at_trigger = w.top == 0 && chance(12U); /* the list opens only from its trigger */
    for (int tries = 0; k < 70U && tries < 8; ++tries) {
        uint32_t id = chance(50U) ? BTN(rnd_below(OV_MENU + 1U)) : others[rnd_below(5U)]; /* the body buttons */
        if (at_sub) {
            id = nt_ui_get_bbox(w.ctx, leaf).found ? leaf : sub_row;
        } else if (at_list) {
            id = nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(rnd_below(3U)));
        } else if (at_trigger) {
            id = nt_ui_combo_trigger_id(ID_COMBO);
        }
        const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, id);
        if (bb.found) {
            *x = bb.x + (bb.width * 0.5F) + (float)rnd_below(9U) - 4.0F;
            *y = bb.y + (bb.height * 0.5F) + (float)rnd_below(5U) - 2.0F;
            return;
        }
    }
    if (k < 85U) {
        *x = (float)rnd_below(800U);
        *y = (float)rnd_below(600U);
    }
}

/* The body button of P0..P2, the modals or the menu row under (x, y) in last frame's layout (0 = none). */
static uint32_t body_button_at(float x, float y) {
    for (int ov = OV_P0; ov <= OV_MENU; ++ov) {
        if (ov != OV_LIST && box_has(BTN(ov), x, y)) {
            return BTN(ov);
        }
    }
    return 0U;
}

static void check_outlives(void) {
    if (!w.armed) {
        return;
    }
    if (!w.armed_by_row) {
        ++r.modal_from[w.armed_src];
    } else {
        ++*((w.armed_src == OV_LIST) ? &r.pick_opens : &r.activation_opens);
    }
    const bool src_open = is_open(w.armed_src) || (w.armed_src == OV_SUB && is_open(OV_MENU));
    if (src_open || !is_open(w.armed_ov)) {
        fail(INV_OUTLIVES, "overlay %d opened from %d: %d open %d, %d open %d after the frame", w.armed_ov, w.armed_src, w.armed_src, (int)src_open, w.armed_ov, (int)is_open(w.armed_ov));
    }
}

static void run_frame(void) {
    static const nt_key_t keys[] = {NT_KEY_ESCAPE, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_RIGHT, NT_KEY_ENTER};
    w.opens_left = OPENS_PER_FRAME;
    w.live_prev = w.live_now;
    w.live_now = 0U;
    memcpy(w.shown_prev, w.shown_now, sizeof w.shown_prev);
    memset(w.shown_now, 0, sizeof w.shown_now);
    w.armed = false;
    w.spell = (w.spell == 0 && chance(1U)) ? 1 : w.spell;
    w.quiet = w.spell > 0 && w.spell <= SPELL_BUILD;
    const bool paused = w.spell > SPELL_BUILD;
    spell_op();
    w.spell = (w.spell > 0 && w.spell < SPELL_LEN) ? w.spell + 1 : 0;
    maybe_op(8U);
    maybe_op(3U);
    look();
    const bool left = w.sc.down[NT_BUTTON_LEFT];
    const bool right = w.sc.down[NT_BUTTON_RIGHT];
    const bool middle = w.sc.down[NT_BUTTON_MIDDLE];
    float x = w.sc.x;
    float y = w.sc.y;
    if (!(left || right) || chance(15U)) {
        pick_pointer(&x, &y); /* a held button mostly stays put, so presses become clicks */
    }
    int press = -1;
    int release = -1;
    if (!w.quiet && !left && chance(30U)) { /* a building spell takes no press: it would close the chain's top */
        press = NT_BUTTON_LEFT;
    } else if (!w.quiet && !right && chance(6U)) {
        press = NT_BUTTON_RIGHT;
    }
    if (left && chance(55U)) {
        release = NT_BUTTON_LEFT;
    } else if (right && chance(45U)) {
        release = NT_BUTTON_RIGHT;
    }
    const uint32_t k = rnd_below(100U);
    /* The middle button reuses the key draw, so it leaves the seeds' streams as they were until it presses. */
    if (!w.quiet && press < 0 && !middle && k >= 22U && k < 25U) {
        press = NT_BUTTON_MIDDLE;
    } else if (middle && release < 0 && k >= 50U) {
        release = NT_BUTTON_MIDDLE;
    }
    const nt_key_t key = (w.quiet || k >= 22U) ? NT_KEY_COUNT : keys[k / 6U];
    w.held_before = left || right || middle;
    w.press_any = press >= 0;
    w.press_left = press == NT_BUTTON_LEFT;
    w.release_left = release == NT_BUTTON_LEFT;
    if (w.press_left) {
        w.held_btn = w.held_before ? 0U : body_button_at(x, y);
    } else if (w.release_left) {
        w.held_btn = 0U;
    }
    float wheel = 0.0F;
    if (chance(20U)) {
        wheel = chance(50U) ? 1.0F : -1.0F;
    }
    if (key != NT_KEY_COUNT) {
        nt_input_feed(nt_test_key(key, true));
    }
    int fading = 0; /* shown last frame and closed since: each is a fading stack entry at this begin */
    for (int ov = 0; ov < OV_N; ++ov) {
        fading += (w.shown_prev[ov] && !is_open(ov)) ? 1 : 0;
    }
    r.fade_cuts += (fading > NT_UI_OVERLAY_MAX / 2) ? 1 : 0;
    sc_step_dt(&w.sc, paused ? 0.0F : UI_FX_DT, x, y, press, release, wheel);
    if (key != NT_KEY_COUNT) {
        nt_input_feed(nt_test_key(key, false));
    }
    look();
    check_outlives();
}

static void run_seed(uint32_t seed) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    ui_walker_fixture_fill_stub_font(&s_fx);
    memset(&w, 0, sizeof w);
    w.ctx = s_fx.ctx;
    w.seed = seed;
    w.rng = (seed * 2654435761U) | 1U;
    const bool slow = (seed % 3U) == 0U;
    w.ease = slow ? 1.5F : 14.0F;
    w.snap_ease = slow ? 2.0F : 0.0F;
    w.menu_style = nt_ui_menu_style_defaults();
    w.dd_style = nt_ui_dropdown_style_defaults();
    w.dd_style.max_visible_rows = 3U;
    w.dd_style.open_ease_speed = w.ease; /* the list fades, so a closing list still declares its scroll */
    w.combo_sel = -1;
    w.sc = (sc_t){.ctx = w.ctx, .frame = frame_fn, .game = NULL};
    for (w.frame = 0; w.frame < FRAMES; ++w.frame) {
        run_frame();
    }
    ui_walker_fixture_shutdown(&s_fx);
    nt_input_shutdown();
}
// #endregion

static void test_overlay_invariants_hold_over_random_frames(void) {
    for (uint32_t seed = 1U; seed <= SEEDS; ++seed) {
        run_seed(seed);
    }
    for (int inv = 0; inv < INV_N; ++inv) {
        if (r.fail_count[inv] != 0) {
            TEST_FAIL_MESSAGE(r.fail_msg[inv]);
        }
    }
    const struct {
        const char *name;
        int n;
    } cov[] = {
        {"releases on a deaf button", r.deaf_releases},
        {"overlay clicks", r.overlay_clicks},
        {"base clicks", r.base_clicks},
        {"base releases under a top", r.base_releases_under_top},
        {"children closed with their parent", r.cascades},
        {"opens inside a closed body", r.opens_in_closed},
        {"presses under a top", r.top_presses},
        {"presses with no top", r.free_presses},
        {"joining presses", r.joins},
        {"held frames", r.holds},
        {"free pointers", r.free_pointers},
        {"frames wanting the pointer", r.wants_pointer},
        {"frames wanting the keyboard", r.wants_keyboard},
        {"modal from LIST", r.modal_from[OV_LIST]},
        {"modal from MENU", r.modal_from[OV_MENU]},
        {"modal from SUB", r.modal_from[OV_SUB]},
        {"modal from P1", r.modal_from[OV_P1]},
        {"opens from a menu activation", r.activation_opens},
        {"opens from a combo pick", r.pick_opens},
        {"begins that cut piled fades (S4)", r.fade_cuts},
    };
    for (size_t i = 0U; i < sizeof cov / sizeof cov[0]; ++i) {
        printf("%s %d\n", cov[i].name, cov[i].n);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, cov[i].n, cov[i].name); /* an invariant that never ran proves nothing */
    }
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_overlay_invariants_hold_over_random_frames);
    return UNITY_END();
}
