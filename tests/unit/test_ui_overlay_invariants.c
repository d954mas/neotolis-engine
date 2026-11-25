/* Randomized invariant checker for the overlay stack and the input router (spec chapter UI, "Input ownership",
 * "Popup-core", "Immediate menu + combo"). Fixed seeds drive opens, closes, reopens and nested opens made before
 * nt_ui_begin, between and inside the declarations, modals opened from menu, combo and popup bodies, popups and the
 * menu opened from a menu activation or a combo pick, paused (dt = 0) spells that pile up fades, a shuffled root
 * declaration order with units left out, pointer moves, left and right presses, releases and wheel, Esc, Down,
 * Right and Enter; every frame checks the stack, paint versus router order, catcher records, the deaf
 * OPENING/CLOSING bodies, the router answers, click-through, the gesture's pointer owner, the parent chain, the deaf
 * scroll and what a modal, an activation or a pick opened. It reads internals on purpose: it checks the engine's own
 * bookkeeping, not one game story. Every invariant counts the cases it checked, and a count of 0 fails the run. */

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
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

/* 1 prints every frame's operations as it runs: the trace to read when an engine assert stops the run. */
#ifndef UI_INV_TRACE
#define UI_INV_TRACE 0
#endif

#define SEEDS 48
#define FRAMES 1000
#define LOG_W 320
#define TRACE_BACK 16
#define OPENS_PER_FRAME 2 /* pacing; stack_room keeps the slots under NT_UI_OVERLAY_MAX */
#define ENGINE_OPENS 2    /* the combo list and the submenu the engine may still open in a frame */
#define FADE_N 6          /* slow popups a paused spell opens one after another, so their fades pile up */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

// #region ids
enum { OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_LIST, OV_MENU, OV_SUB, OV_N, OV_BASE = OV_N };
enum { PH_OPENING, PH_OPEN, PH_CLOSING };
enum { INV_STACK, INV_PAINT, INV_CATCHER, INV_OPENING, INV_ROUTER, INV_THROUGH, INV_GESTURE, INV_TREE, INV_WHEEL, INV_MODAL, INV_RESPONSE, INV_N };

static const char *const s_ov_name[] = {"P0", "P1", "M0", "M1", "P2", "LIST", "MENU", "SUB", "BASE"};
static const char *const s_ph_name[] = {"OPENING", "OPEN", "CLOSING"};
static const char *const s_inv_name[] = {"1 stack",
                                         "2 paint=router order",
                                         "3 closed leaves no catcher",
                                         "4 OPENING/CLOSING deaf",
                                         "5 router fixed at begin",
                                         "6 no click through",
                                         "7 gesture keeps its pointer owner",
                                         "8 open overlay has an open parent",
                                         "9 deaf scroll takes no wheel",
                                         "10 modal from a light overlay closes it",
                                         "11 an activation's or pick's open outlives its menu or list"};

#define ID_P0 0x6A0001U
#define ID_P1 0x6A0002U
#define ID_M0 0x6A0003U
#define ID_M1 0x6A0004U
#define ID_P2 0x6A0005U
#define ID_COMBO 0x6A0010U
#define ID_LIST ID_COMBO /* the list is the overlay under the combo id */
#define ID_MENU 0x6A0020U
#define ID_F(i) (0x6A0030U + (uint32_t)(i))
#define BOX_F(i) (0x6A0040U + (uint32_t)(i))
#define BTN_P0 0x6A0101U
#define BTN_P1 0x6A0102U
#define BTN_M0 0x6A0103U
#define BTN_M1 0x6A0104U
#define BTN_P2 0x6A0105U
#define BTN_MROW 0x6A0106U
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
/* One seed's world: zeroed by run_seed. Grouped by field size, so it packs. */
typedef struct {
    nt_ui_context_t *ctx;
    sc_t sc;
    nt_ui_menu_style_t menu_style;
    nt_ui_menu_t menu;
    nt_ui_dropdown_style_t dd_style;
    nt_ui_scroll_state_t list_scroll_before; /* the list scroll as nt_ui_begin left it */
    uint32_t rng;
    uint32_t seed;
    int frame;
    float ease;      /* P1, the modals and the list; a slow seed keeps their fades up for many frames */
    float snap_ease; /* P0 and P2: 0 snaps, else they fade too */
    int combo_sel;
    uint32_t sub_id; /* the menu's submenu level, found on the stack once it opens */
    /* What the test's own calls and looks predict. */
    uint32_t closed_since_end;  /* bit per OV_*: seen closed, or restarted, after the last nt_ui_end */
    uint32_t closed_since_decl; /* bit per OV_*: seen closed, or restarted, after its last open declaration */
    int chain_n;
    int opens_left;
    int spell; /* frame of a fade spell, 1-based (0 = none) */
    /* This frame, read right after nt_ui_begin. */
    uint32_t top;
    uint32_t allowed;       /* bit per OV_*: whose clicks this begin's top permits */
    uint32_t enter_row;     /* the menu row this frame's Enter activates (0 = none) */
    uint32_t press_allowed; /* `allowed` of the frame of the last left press; 0 when it joined the game's gesture */
    uint32_t aim_id;        /* the target the pointer was last aimed at */
    uint32_t held_btn;      /* the body button the held left press landed on (0 = none) */
    /* The previous frame's end and the ops since. */
    uint32_t open_end_bits; /* bit per OV_*: open at the last nt_ui_end */
    uint32_t pre_changed;   /* bit per OV_*: open at the last end, closed or restarted before this nt_ui_begin */
    uint32_t pre_closed;    /* closed_since_decl before this nt_ui_begin */
    uint32_t pre_catcher;   /* bit per OV_*: its catcher record was in the registry before this nt_ui_begin */
    uint32_t declared;      /* bit per OV_*: its begin ran this frame */
    uint32_t orphans;       /* bit per OV_*: open at the last end, declared, its open parent was not */
    float wheel_now;
    int list_phase; /* the list's phase this frame, -1 when it did not show */
    /* An open made from inside a light overlay or by a row's command this frame, until a later op touches the stack. */
    int from_opener;
    int from_modal;
    int resp_src;
    int resp_ov;
    int chain[NT_UI_OVERLAY_MAX];
    uint32_t open_end[NT_UI_OVERLAY_MAX];
    uint32_t opened_since[NT_UI_OVERLAY_MAX];
    uint32_t pre_fading[NT_UI_OVERLAY_MAX]; /* shown and closed before this nt_ui_begin, in slot order */
    uint8_t chain_phase[NT_UI_OVERLAY_MAX];
    bool pending[OV_N];  /* opened, or reopened in its fade, since its last declaration: the next is OPENING */
    bool was_open[OV_N]; /* nt_ui_is_open at the last look: an open the engine makes shows as a rise */
    uint8_t begin_count;
    uint8_t open_end_n;
    uint8_t opened_since_n;
    uint8_t pre_fading_n;
    bool quiet; /* the spell builds its chain: no random op, press or key breaks it */
    bool wants_pointer;
    bool wants_keyboard;
    bool held_before; /* a button was down before this frame's edges */
    bool press_any;
    bool press_left;
    bool release_left;
    bool enter_now;
    bool drag_active; /* a gesture runs from its first press to its last release */
    bool drag_ui;     /* the owner its press frame expected */
    bool wheel_on_list;
    bool list_rest_pre; /* the list scroll was at rest before nt_ui_begin: no drag, fling or spring moves it */
    bool from_armed;
    bool resp_armed;
    char log[FRAMES][LOG_W];
} world_t;

/* Kept across seeds: failures and coverage. */
typedef struct {
    int fail_count[INV_N];
    char fail_msg[INV_N][512];
    int n_top_frames, n_stack_checks, n_fade_cuts, n_paused_frames, n_paint_pairs, n_catcher_kept, n_catcher_drops, n_pre_close_drops;
    int n_opening, n_closing, n_deaf_releases, n_wants_pointer, n_wants_keyboard, n_clicks[OV_N + 1], n_enter_rows;
    int n_gesture_presses, n_free_checks, n_gesture_joins, n_gesture_holds, n_right_presses, n_game_release_over_ui;
    int n_orphans, n_open_in_closed, n_closing_wheels, n_open_wheel_moves, n_modal_from[OV_N], n_resp[OV_N];
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
static uint32_t bit(int ov) { return 1U << (uint32_t)ov; }

__attribute__((format(printf, 1, 2))) static void logf_frame(const char *fmt, ...) {
    char *line = w.log[w.frame];
    const size_t used = strlen(line);
    if (used + 2U >= LOG_W) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(line + used, LOG_W - used, fmt, ap);
    va_end(ap);
}

static uint32_t ov_id(int ov) {
    static const uint32_t ids[OV_N] = {ID_P0, ID_P1, ID_M0, ID_M1, ID_P2, ID_LIST, ID_MENU, 0U};
    return (ov == OV_SUB) ? w.sub_id : ids[ov];
}

static const char *id_name(uint32_t id) {
    static const char *const fades[FADE_N] = {"F0", "F1", "F2", "F3", "F4", "F5"};
    for (int ov = 0; ov < OV_N; ++ov) {
        if (id != 0U && ov_id(ov) == id) {
            return s_ov_name[ov];
        }
    }
    if (id >= ID_F(0) && id < ID_F(FADE_N)) {
        return fades[id - ID_F(0)];
    }
    return (id == 0U) ? "none" : "?";
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
    (void)snprintf(r.fail_msg[inv], sizeof r.fail_msg[inv], "inv %s: seed %u frame %d: %s", s_inv_name[inv], (unsigned)w.seed, w.frame, what);
    printf("FIRST FAILURE %s\n  trace (frame: pre-begin ops | input | in-frame ops and events):\n", r.fail_msg[inv]);
    for (int f = (w.frame > TRACE_BACK) ? w.frame - TRACE_BACK : 0; f <= w.frame; ++f) {
        printf("  f%03d %s\n", f, w.log[f]);
    }
}
// #endregion

// #region stack reads
static int32_t stack_index(uint32_t id) { return nt_ui_internal_overlay_index(w.ctx, id); }

/* `id` was opened inside `top`, directly or through the overlays it was opened inside (the S2 parent chain). */
static bool descends_from(uint32_t id, uint32_t top) {
    int32_t at = stack_index(id);
    for (int n = 0; at >= 0 && n < NT_UI_OVERLAY_MAX; ++n) {
        const uint32_t parent = w.ctx->overlays[at].parent;
        if (parent == top) {
            return parent != 0U;
        }
        at = stack_index(parent);
    }
    return false;
}

static bool in_list(const uint32_t *ids, uint8_t n, uint32_t id) {
    for (uint8_t i = 0U; i < n; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static void find_sub(void) {
    for (uint8_t i = 0U; w.sub_id == 0U && i < w.ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &w.ctx->overlays[i];
        if (o->id != 0U && o->parent == ID_MENU && o->opener != 0U) {
            w.sub_id = o->id;
        }
    }
}

static bool is_open(int ov) { return ov_id(ov) != 0U && nt_ui_is_open(w.ctx, ov_id(ov)); }

/* A look at the open state after anything that may change it: an overlay seen closed since the last end, and an
 * open the engine makes (the combo list, the submenu) as a rise, which marks it OPENING and ends an armed check
 * a later S2 close could break. */
static void look(void) {
    find_sub();
    for (int ov = 0; ov < OV_N; ++ov) {
        const bool open = is_open(ov);
        if (!open) {
            w.closed_since_end |= bit(ov);
            w.closed_since_decl |= bit(ov);
        }
        if ((ov == OV_LIST || ov == OV_SUB) && open && !w.was_open[ov]) {
            w.pending[ov] = true;
            w.from_armed = false;
            w.resp_armed = false;
        }
        w.was_open[ov] = open;
    }
}

static int32_t order_of(uint32_t id) {
    const int32_t slot = nt_ui_internal_live_slot(w.ctx, id);
    return (slot >= 0) ? (int32_t)w.ctx->hit_baked[slot].order : -1;
}

static int ov_of(uint32_t id) {
    for (int ov = 0; ov < OV_N; ++ov) {
        if (id != 0U && ov_id(ov) == id) {
            return ov;
        }
    }
    return -1;
}

static bool open_on_stack(uint32_t id) {
    const int32_t at = stack_index(id);
    return at >= 0 && w.ctx->overlays[at].open;
}

/* Closing an overlay closes the overlays opened inside it, wherever they are declared (S1, S2), and nothing opens
 * inside a closing one: an open overlay always has an open parent. */
static void check_tree(const char *when) {
    for (uint8_t i = 0U; i < w.ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &w.ctx->overlays[i];
        if (o->id != 0U && o->open && o->parent != 0U && !open_on_stack(o->parent)) {
            fail(INV_TREE, "%s: %s open although %s, the overlay it was opened inside, is not", when, id_name(o->id), id_name(o->parent));
        }
    }
}

static uint32_t list_scroll_id(void) { return nt_ui_dropdown_test_scroll_id(ID_COMBO); }

static bool list_scroll_read(nt_ui_scroll_state_t *out) {
    const nt_ui_scroll_state_t *st = (const nt_ui_scroll_state_t *)nt_ui_state_find(w.ctx, list_scroll_id());
    if (st != NULL) {
        *out = *st;
    }
    return st != NULL;
}

static bool has_record(const nt_ui_interactive_t *recs, uint32_t n, uint32_t id) {
    for (uint32_t k = 0U; k < n; ++k) {
        if (recs[k].id == id) {
            return true;
        }
    }
    return false;
}

static bool box_has(uint32_t id, float x, float y) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, id);
    return bb.found && x >= bb.x && x < bb.x + bb.width && y >= bb.y && y < bb.y + bb.height;
}

#define TARGETS_N 16U

typedef struct {
    uint32_t id;
    int ov; /* whose widget: an OV_* or OV_BASE */
} target_t;

/* What the pointer aims at: a widget of every overlay and of the base. */
static uint32_t pointer_targets(target_t out[TARGETS_N]) {
    const uint32_t sub_row = nt_ui_menu_test_item_id(ID_MENU, KEY_SUB);
    const target_t t[] = {{BTN_P0, OV_P0},
                          {BTN_P1, OV_P1},
                          {BTN_M0, OV_M0},
                          {BTN_M1, OV_M1},
                          {BTN_P2, OV_P2},
                          {BTN_MROW, OV_MENU},
                          {nt_ui_combo_trigger_id(ID_COMBO), OV_BASE},
                          {nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(0)), OV_LIST},
                          {nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(COMBO_CUSTOM_ROW)), OV_LIST},
                          {nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(2)), OV_LIST},
                          {nt_ui_menu_test_item_id(ID_MENU, KEY_EDIT), OV_MENU},
                          {sub_row, OV_MENU},
                          {nt_ui_menu_test_item_id(sub_row, KEY_L1), OV_SUB},
                          {nt_ui_menu_test_item_id(sub_row, KEY_L2), OV_SUB},
                          {BTN_BASE0, OV_BASE},
                          {BTN_BASE1, OV_BASE}};
    const uint32_t n = (uint32_t)(sizeof t / sizeof t[0]);
    memcpy(out, t, sizeof t);
    return n;
}

/* The pointer lies on a target last frame laid out. */
static bool pointer_over_ui(void) {
    target_t t[TARGETS_N];
    const uint32_t n = pointer_targets(t);
    for (uint32_t i = 0U; i < n; ++i) {
        if (box_has(t[i].id, w.sc.x, w.sc.y)) {
            return true;
        }
    }
    return false;
}

/* The overlay body button at (x, y) in last frame's layout, the aimed one first (0 = none). */
static uint32_t body_button_at(float x, float y) {
    static const uint32_t btns[] = {BTN_P0, BTN_P1, BTN_M0, BTN_M1, BTN_P2, BTN_MROW};
    for (uint32_t i = 0U; i < sizeof btns / sizeof btns[0]; ++i) {
        if (btns[i] == w.aim_id && box_has(btns[i], x, y)) {
            return btns[i];
        }
    }
    for (uint32_t i = 0U; i < sizeof btns / sizeof btns[0]; ++i) {
        if (box_has(btns[i], x, y)) {
            return btns[i];
        }
    }
    return 0U;
}
// #endregion

// #region checks at nt_ui_begin
/* S1 and S4: the prune leaves no dead entry, keeps only what was declared or opened since, and ends the oldest
 * fades past half the stack. */
static void check_stack_after_begin(void) {
    const nt_ui_context_t *ctx = w.ctx;
    if (ctx->overlay_count > NT_UI_OVERLAY_MAX) {
        fail(INV_STACK, "overlay_count %u after begin", (unsigned)ctx->overlay_count);
    }
    for (uint8_t i = 0U; i < ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &ctx->overlays[i];
        ++r.n_stack_checks;
        if (o->id == 0U) {
            fail(INV_STACK, "dead entry in slot %u after begin", (unsigned)i);
            continue;
        }
        const bool declared = nt_ui_internal_live_slot(ctx, o->id) >= 0;
        if (!declared && !in_list(w.opened_since, w.opened_since_n, o->id)) {
            fail(INV_STACK, "%s (slot %u, open %d) kept although neither declared last frame nor opened since the last begin", id_name(o->id), (unsigned)i, (int)o->open);
        }
    }
    uint8_t kept = 0U;
    for (uint8_t i = 0U; i < w.pre_fading_n; ++i) {
        const int32_t at = stack_index(w.pre_fading[i]);
        if (at >= 0 && !ctx->overlays[at].open) {
            ++kept;
        } else if (kept != 0U) {
            fail(INV_STACK, "the fade of %s ended although an older fade was kept", id_name(w.pre_fading[i]));
        }
    }
    if (kept > NT_UI_OVERLAY_MAX / 2U) {
        fail(INV_STACK, "%u fades kept after begin, over half the stack", (unsigned)kept);
    }
}

static void check_paint_order(void) {
    const uint32_t top = w.top;
    if (top == 0U) {
        return;
    }
    const int32_t catcher = order_of(nt_ui_internal_popup_catcher_id(top));
    if (catcher < 0) {
        fail(INV_PAINT, "top %s has no catcher declared last frame", id_name(top));
        return;
    }
    for (uint8_t i = 0U; i < w.ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &w.ctx->overlays[i];
        if (o->id == 0U || o->id == top || !o->live || !o->open || !in_list(w.open_end, w.open_end_n, o->id) || descends_from(o->id, top)) {
            continue;
        }
        const int32_t panel = order_of(o->id);
        r.n_paint_pairs += (panel >= 0) ? 1 : 0;
        if (panel >= 0 && catcher <= panel) {
            fail(INV_PAINT, "top %s catcher order %d is not above unrelated open %s panel order %d", id_name(top), (int)catcher, id_name(o->id), (int)panel);
        }
    }
}

/* R3: nt_ui_begin drops last frame's catcher record of each overlay closed since its declaration, mid-frame, between
 * frames or by S1 at that begin, before it resolves the pointer; an open overlay keeps it, and so does the top S3
 * closes after the drop. */
static void check_catchers(void) {
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        if (id == 0U) {
            continue;
        }
        const uint32_t catcher = nt_ui_internal_popup_catcher_id(id);
        const bool had = (w.pre_catcher & bit(ov)) != 0U;
        const bool has = has_record(w.ctx->interactive_prev, w.ctx->interactive_prev_count, catcher);
        const bool changed = (w.pre_changed & bit(ov)) != 0U;
        const bool keeps = (w.pre_closed & bit(ov)) == 0U && (is_open(ov) || id == w.top);
        if (had) {
            r.n_catcher_kept += keeps ? 1 : 0;
            r.n_catcher_drops += keeps ? 0 : 1;
            r.n_pre_close_drops += changed ? 1 : 0;
        }
        if (has && !keeps) {
            const char *why = ((w.pre_closed & bit(ov)) != 0U) ? "closed or restarted after its declaration" : "closed at nt_ui_begin";
            why = changed ? "closed or restarted before nt_ui_begin" : why;
            fail(INV_CATCHER, "%s %s, its catcher record reached this frame above top %s", s_ov_name[ov], why, id_name(w.top));
        } else if (had && keeps && !has) {
            fail(INV_CATCHER, "%s stayed open, nt_ui_begin dropped its catcher record", s_ov_name[ov]);
        }
    }
}

static uint32_t allowed_by_top(void) {
    if (w.top == 0U) {
        return 0xFFFFFFFFU;
    }
    uint32_t bits = 0U;
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        if (id != 0U && (id == w.top || descends_from(id, w.top))) {
            bits |= bit(ov);
        }
    }
    return bits;
}

/* An overlay declared last frame whose open parent was not closes at this begin with that parent (S1). */
static void check_orphans(void) {
    for (int ov = 0; ov < OV_N; ++ov) {
        if ((w.orphans & bit(ov)) == 0U) {
            continue;
        }
        ++r.n_orphans;
        if (is_open(ov) && !in_list(w.opened_since, w.opened_since_n, ov_id(ov))) {
            fail(INV_TREE, "%s stayed open at the begin after the overlay it was opened inside was left out", s_ov_name[ov]);
        }
    }
}

/* R2: the press that starts a gesture decides its owner, the UI's on a record or while a catcher overlay is the
 * top; a press while the other button is held, or released in that frame, joins it; the owner holds
 * nt_ui_wants_pointer to the last release; a free pointer is the UI's over a record. */
static void check_gesture(void) {
    const bool hot = nt_ui_pointer_hot(w.ctx, 0U).id != 0U;
    if (w.press_any && !w.held_before) {
        const bool expect = hot || w.top != 0U;
        ++r.n_gesture_presses;
        if (w.wants_pointer != expect) {
            fail(INV_GESTURE, "nt_ui_wants_pointer %d on the press that starts a gesture, expected %d (hot %d, top %s)", (int)w.wants_pointer, (int)expect, (int)hot, id_name(w.top));
        }
        w.drag_active = true;
        w.drag_ui = expect;
    } else if (w.drag_active) {
        r.n_gesture_joins += w.press_any ? 1 : 0;
        r.n_gesture_holds += w.press_any ? 0 : 1;
        if (w.wants_pointer != w.drag_ui) {
            fail(INV_GESTURE, "nt_ui_wants_pointer %d during a %s gesture%s", (int)w.wants_pointer, w.drag_ui ? "UI" : "game", w.press_any ? " on a joining press" : "");
        }
        r.n_game_release_over_ui += (!w.drag_ui && !w.sc.down[NT_BUTTON_LEFT] && !w.sc.down[NT_BUTTON_RIGHT] && pointer_over_ui()) ? 1 : 0;
    } else {
        ++r.n_free_checks;
        if (w.wants_pointer != hot) {
            fail(INV_GESTURE, "nt_ui_wants_pointer %d for a free pointer, hot %d", (int)w.wants_pointer, (int)hot);
        }
    }
    if (!w.sc.down[NT_BUTTON_LEFT] && !w.sc.down[NT_BUTTON_RIGHT]) {
        w.drag_active = false;
    }
}

static void after_begin(void) {
    w.begin_count = w.ctx->overlay_count;
    w.top = w.ctx->overlay_top;
    check_stack_after_begin();
    check_paint_order();
    check_catchers();
    check_orphans();
    check_tree("after begin");
    look();
    w.allowed = allowed_by_top();
    r.n_top_frames += (w.top != 0U) ? 1 : 0;
    w.wants_pointer = nt_ui_wants_pointer(w.ctx);
    w.wants_keyboard = nt_ui_wants_keyboard(w.ctx);
    r.n_wants_pointer += w.wants_pointer ? 1 : 0;
    r.n_wants_keyboard += w.wants_keyboard ? 1 : 0;
    check_gesture();
    if (w.press_left) {
        w.press_allowed = (w.held_before && !w.drag_ui) ? 0U : w.allowed; /* a press joining the game's gesture reaches no widget */
    }
    w.wheel_on_list = w.wheel_now != 0.0F && w.ctx->wheel_owner[0][1] == list_scroll_id() && list_scroll_read(&w.list_scroll_before);
    w.list_phase = -1;
    logf_frame(" | top %s", id_name(w.top));
}
// #endregion

// #region operations
static bool in_chain(int ov) {
    for (int i = 0; i < w.chain_n; ++i) {
        if (w.chain[i] == ov) {
            return true;
        }
    }
    return false;
}

/* Room for one more open and for the opens the engine may still make this frame: every open may add an entry. */
static bool stack_room(void) { return w.ctx->overlay_count + 1U + ENGINE_OPENS <= NT_UI_OVERLAY_MAX; }

/* Opens `ov` with the call a game makes. An open of an overlay that is not open, a reopen in its fade, and any menu
 * open (it restarts the menu) make the next declaration OPENING. */
static void open_now(int ov, const char *where) {
    look(); /* a close the engine made since the last look, as an activation's */
    const uint32_t id = ov_id(ov);
    const bool was = is_open(ov);
    const bool in_closed = w.chain_n > 0 && !is_open(w.chain[w.chain_n - 1]); /* the body being declared is closed */
    w.from_armed = false;
    w.resp_armed = false;
    switch (ov) {
    case OV_P0:
    case OV_P1:
    case OV_P2:
        nt_ui_popup_open(w.ctx, id);
        break;
    case OV_M0:
    case OV_M1:
        nt_ui_modal_open(w.ctx, id);
        break;
    default: {
        const float x = 40.0F + (float)rnd_below(520U);
        const float y = 40.0F + (float)rnd_below(380U);
        nt_ui_menu_open(&w.menu, w.ctx, ID_MENU, x, y);
        break;
    }
    }
    if (ov == OV_MENU || !was) {
        w.pending[ov] = true;
        w.closed_since_end |= was ? bit(ov) : 0U;
        w.closed_since_decl |= was ? bit(ov) : 0U;
    }
    if (in_closed) {
        ++r.n_open_in_closed;
        check_tree("open inside a closed body");
    }
    look();
    logf_frame(" %s:open %s", where, s_ov_name[ov]);
}

static void op_open(int ov, const char *where) {
    if (w.quiet || w.opens_left <= 0 || in_chain(ov) || !stack_room()) {
        return; /* an overlay is not opened inside its own declaration */
    }
    --w.opens_left;
    open_now(ov, where);
}

static void op_close(int ov, const char *where) {
    const uint32_t id = ov_id(ov);
    if (id != 0U) {
        nt_ui_close(w.ctx, id);
        w.from_armed = false;
        w.resp_armed = false;
        look();
        logf_frame(" %s:close %s", where, s_ov_name[ov]);
    }
}

/* A modal opened from inside the open light overlay `opener` (a menu level, the combo list, a popup) while it is
 * declared: the opener closes and the modal stays (S2, as showModal). */
static void op_modal_from(int opener, int modal, const char *where) {
    if (w.quiet || w.opens_left <= 0 || !stack_room() || is_open(modal) || !is_open(opener)) {
        return;
    }
    --w.opens_left;
    open_now(modal, where);
    w.from_armed = true;
    w.from_opener = opener;
    w.from_modal = modal;
}

/* A game command run on a menu activation or a combo pick: it opens a popup declared at the root, or from a pick
 * the menu; the menu or list closed before the row returned (M4), so what it opens outlives it. */
static void op_respond(int src, const char *where) {
    static const int from_menu[] = {OV_P0, OV_P1};
    static const int from_pick[] = {OV_P0, OV_P1, OV_MENU};
    if (w.quiet || !chance(40U) || !stack_room()) {
        return;
    }
    const int ov = (src == OV_LIST) ? from_pick[rnd_below(3U)] : from_menu[rnd_below(2U)];
    open_now(ov, where);
    w.resp_armed = true;
    w.resp_src = src;
    w.resp_ov = ov;
}

/* The overlay whose body button the held left press landed on, else a random openable one. */
static int held_or_random(const int *openable, uint32_t n) {
    static const uint32_t btns[] = {BTN_P0, BTN_P1, BTN_M0, BTN_M1, BTN_P2, BTN_MROW};
    static const int owners[] = {OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_MENU};
    for (uint32_t i = 0U; w.held_btn != 0U && i < sizeof btns / sizeof btns[0]; ++i) {
        if (btns[i] == w.held_btn) {
            return owners[i];
        }
    }
    return openable[rnd_below(n)];
}

static void maybe_op(uint32_t percent, const char *where) {
    if (w.quiet || !chance(percent)) {
        return;
    }
    static const int openable[] = {OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_MENU, OV_MENU}; /* the menu closes on any outside press */
    const uint32_t k = rnd_below(100U);
    if (k < 40U) {
        op_open(openable[rnd_below(7U)], where);
    } else if (k < 70U) {
        op_close((int)rnd_below(OV_N), where);
    } else {
        const int ov = held_or_random(openable, 6U); /* a reopen during the fade, often under a held press */
        if (!in_chain(ov)) {
            op_close(ov, where);
            op_open(ov, where);
        }
    }
}

/* A fade spell: modals and a chain of slow popups, each opened inside the one before (so none closes another),
 * rise undisturbed; then one close ends them all at once, more fades than nt_ui_begin keeps (S4), and a paused tail
 * (dt = 0) holds those fades over many begins. */
#define SPELL_BUILD 16
#define SPELL_LEN 32
static void spell_op(void) {
    if (w.spell == 1) {
        for (int ov = OV_M0; ov <= OV_M1; ++ov) {
            if (!is_open(ov) && stack_room()) {
                open_now(ov, "spell");
            }
        }
    } else if (w.spell == 2 && stack_room()) {
        nt_ui_popup_open(w.ctx, ID_F(0));
        look();
        logf_frame(" spell:open F0");
    } else if (w.spell == SPELL_BUILD + 1) {
        nt_ui_close(w.ctx, ID_F(0));
        op_close(OV_M0, "spell");
        op_close(OV_M1, "spell");
    }
}
// #endregion

// #region declarations
static void chain_push(int ov, uint8_t phase) {
    w.chain[w.chain_n] = ov;
    w.chain_phase[w.chain_n] = phase;
    ++w.chain_n;
}

/* The phase of a shown declaration (spec "Overlay lifecycle"), from whether it is open at its declaration: OPENING
 * on the first declaration since an open, else OPEN while open, else CLOSING. */
static uint8_t take_phase(int ov, bool open) {
    look();
    const bool first = w.pending[ov];
    w.pending[ov] = false;
    uint8_t phase = (uint8_t)PH_CLOSING;
    if (open) {
        phase = first ? (uint8_t)PH_OPENING : (uint8_t)PH_OPEN;
        w.closed_since_decl &= ~bit(ov); /* an open declaration registers a fresh catcher */
    }
    logf_frame(" %s(%s)", s_ov_name[ov], s_ph_name[phase]);
    r.n_opening += (phase == PH_OPENING) ? 1 : 0;
    r.n_closing += (phase == PH_CLOSING) ? 1 : 0;
    return phase;
}

static void on_click(int ov, uint32_t what, bool row) {
    logf_frame(" CLICK %s:%x", s_ov_name[ov], (unsigned)what);
    ++r.n_clicks[ov];
    for (int i = 0; i < w.chain_n; ++i) {
        if (w.chain_phase[i] != PH_OPEN) {
            fail(INV_OPENING, "%x in %s's body reports a click while %s is %s", (unsigned)what, s_ov_name[ov], s_ov_name[w.chain[i]], s_ph_name[w.chain_phase[i]]);
        }
    }
    if (row && what == w.enter_row) {
        ++r.n_enter_rows;
        return; /* Enter activates the focused row without a press */
    }
    if ((w.press_allowed & bit(ov)) == 0U) {
        fail(INV_THROUGH, "%x in %s clicked although its press reached no widget or the press frame's top was another overlay", (unsigned)what, s_ov_name[ov]);
    }
    if ((w.allowed & bit(ov)) == 0U) {
        fail(INV_THROUGH, "%x in %s clicked although this frame's top %s does not own it", (unsigned)what, s_ov_name[ov], id_name(w.top));
    }
}

static void button(uint32_t id, int ov) {
    CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(40)}}}) {}
    bool deaf = false;
    for (int i = 0; i < w.chain_n; ++i) {
        deaf = deaf || w.chain_phase[i] != PH_OPEN;
    }
    r.n_deaf_releases += (deaf && w.release_left && box_has(id, w.sc.x, w.sc.y)) ? 1 : 0; /* a click inv 4 had to stop */
    if (nt_ui_step(w.ctx, id, NULL).clicked) {
        on_click(ov, id, false);
    }
}

static void decl_base(uint32_t id, float x, float y) {
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(id, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = x, .y = y}}}) {
        CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(220)}}}) {}
        if (nt_ui_step(w.ctx, id, NULL).clicked) {
            on_click(OV_BASE, id, false);
        }
    }
}

static void decl_popup(int ov, uint32_t btn, float x, float y, float ease, void (*inner)(void)) {
    nt_ui_popup_style_t ps = nt_ui_popup_style_defaults();
    ps.ease_speed = ease;
    const nt_ui_popup_anchor_t anc = {.x = x, .y = y, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const bool open = is_open(ov);
    w.declared |= bit(ov);
    if (!nt_ui_popup_begin(w.ctx, ov_id(ov), &ps, &anc, NULL)) {
        return;
    }
    chain_push(ov, take_phase(ov, open));
    button(btn, ov);
    maybe_op(5U, s_ov_name[ov]);
    if (inner != NULL) {
        inner();
    }
    --w.chain_n;
    nt_ui_popup_end(w.ctx);
}

static void decl_modal(int ov, uint32_t btn, uint8_t flags, void (*inner)(void)) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = w.ease;
    ms.flags = flags;
    const bool open = is_open(ov);
    w.declared |= bit(ov);
    if (!nt_ui_modal_begin(w.ctx, ov_id(ov), &ms, NULL)) {
        return;
    }
    chain_push(ov, take_phase(ov, open));
    button(btn, ov);
    maybe_op(5U, s_ov_name[ov]);
    if (inner != NULL) {
        inner();
    }
    --w.chain_n;
    nt_ui_modal_end(w.ctx);
}

/* P1's body opens M1, declared at the root; M0's body holds P2 and opens P0, declared at the root, so each closes
 * with the overlay it was opened inside though declared elsewhere. */
static void inner_p1(void) {
    if (!chance(8U)) {
        return;
    }
    if (is_open(OV_P1)) {
        op_modal_from(OV_P1, OV_M1, "P1body");
    } else {
        op_open(OV_M1, "P1body");
    }
}

static void inner_p2(void) {
    if (chance(15U)) {
        op_open(OV_P2, "M0body");
    }
    if (chance(10U)) {
        op_open(OV_P0, "M0body");
    }
    decl_popup(OV_P2, BTN_P2, 420.0F, 330.0F, w.snap_ease, NULL);
}

static void decl_combo(void) {
    static const char *const rows[] = {"Alpha", "Beta", "Gamma"};
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(ID_COMBO, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 300.0F}}}) {
        w.declared |= bit(OV_LIST);
        if (nt_ui_combo_begin(w.ctx, NULL, 0U, ID_COMBO, "pick", &w.dd_style)) {
            const uint8_t phase = take_phase(OV_LIST, is_open(OV_LIST)); /* the trigger opens the list inside the begin */
            w.list_phase = phase;
            chain_push(OV_LIST, phase);
            for (int i = 0; i < COMBO_ROWS; ++i) {
                bool picked = false;
                if (i == COMBO_CUSTOM_ROW) {
                    nt_ui_combo_selectable_begin(w.ctx, ROW_KEY(i), w.combo_sel == i);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(90), CLAY_SIZING_FIXED(20)}}}) {}
                    if (chance(4U)) {
                        op_modal_from(OV_LIST, OV_M0, "LISTrow");
                    }
                    picked = nt_ui_combo_selectable_end(w.ctx);
                } else {
                    picked = nt_ui_combo_selectable(w.ctx, ROW_KEY(i), rows[i % 3], w.combo_sel == i);
                }
                if (picked) {
                    w.combo_sel = i;
                    on_click(OV_LIST, nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(i)), true);
                    op_respond(OV_LIST, "LISTpick");
                }
            }
            maybe_op(5U, "LIST");
            --w.chain_n;
            nt_ui_combo_end(w.ctx);
        }
    }
}

static void menu_leaf(int ov, uint32_t scope, uint32_t key, const char *label) {
    if (nt_ui_menu_item(&w.menu, key, label)) {
        on_click(ov, nt_ui_menu_test_item_id(scope, key), true);
        op_respond(OV_MENU, "item");
    }
}

static void decl_menu(void) {
    const bool open = is_open(OV_MENU);
    w.declared |= bit(OV_MENU);
    if (!nt_ui_menu_begin(&w.menu, w.ctx, NULL, 0U, &w.menu_style)) {
        return;
    }
    w.enter_row = (w.enter_now && w.menu.frame.key != 0U) ? w.menu.focus : 0U; /* one key per frame: the key is Enter */
    chain_push(OV_MENU, take_phase(OV_MENU, open));
    const uint32_t sub_row = nt_ui_menu_test_item_id(ID_MENU, KEY_SUB);
    w.declared |= bit(OV_SUB);
    if (nt_ui_menu_submenu_begin(&w.menu, KEY_SUB, "Sub")) {
        chain_push(OV_SUB, take_phase(OV_SUB, is_open(OV_SUB))); /* Right or Enter opens it inside the begin */
        if (chance(4U)) {
            op_modal_from(OV_SUB, OV_M0, "SUB");
        }
        menu_leaf(OV_SUB, sub_row, KEY_L1, "Leaf1");
        menu_leaf(OV_SUB, sub_row, KEY_L2, "Leaf2");
        --w.chain_n;
        nt_ui_menu_submenu_end(&w.menu);
    }
    menu_leaf(OV_MENU, ID_MENU, KEY_EDIT, "Edit");
    nt_ui_menu_item_begin(&w.menu, KEY_ROW, true, false);
    button(BTN_MROW, OV_MENU);
    if (chance(4U)) {
        op_modal_from(OV_MENU, OV_M0, "MROW");
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
        if (!nt_ui_popup_begin(w.ctx, ID_F(i), &ps, &anc, NULL)) {
            break;
        }
        ++shown;
        CLAY({.id = (Clay_ElementId){.id = BOX_F(i)}, .layout = {.sizing = {CLAY_SIZING_FIXED(30), CLAY_SIZING_FIXED(20)}}}) {}
        if (w.quiet && nt_ui_is_open(w.ctx, ID_F(i)) && stack_room()) {
            if (i + 1 < FADE_N && !nt_ui_is_open(w.ctx, ID_F(i + 1))) {
                nt_ui_popup_open(w.ctx, ID_F(i + 1));
                look();
                logf_frame(" F%d:open F%d", i, i + 1);
            } else if (i + 1 == FADE_N && !is_open(OV_P1)) {
                open_now(OV_P1, "Fbody");
            }
        }
    }
    for (; shown > 0; --shown) {
        nt_ui_popup_end(w.ctx);
    }
}

enum { U_BASE0, U_BASE1, U_P0, U_P1, U_M0, U_M1, U_COMBO, U_MENU, U_FADES, U_N };

static void decl_unit(int u) {
    switch (u) {
    case U_BASE0:
        decl_base(BTN_BASE0, 60.0F, 60.0F);
        break;
    case U_BASE1:
        decl_base(BTN_BASE1, 340.0F, 240.0F);
        break;
    case U_P0:
        decl_popup(OV_P0, BTN_P0, 100.0F, 100.0F, w.snap_ease, NULL);
        break;
    case U_P1:
        decl_popup(OV_P1, BTN_P1, 170.0F, 150.0F, w.ease, inner_p1);
        break;
    case U_M0:
        decl_modal(OV_M0, BTN_M0, (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP), inner_p2);
        break;
    case U_M1:
        decl_modal(OV_M1, BTN_M1, NT_UI_MODAL_LISTEN_ESC, NULL);
        break;
    case U_COMBO:
        decl_combo();
        break;
    case U_MENU:
        decl_menu();
        break;
    default:
        decl_fades();
        break;
    }
}

static void frame_fn(void *game) {
    (void)game;
    after_begin();
    int units[U_N];
    for (int i = 0; i < U_N; ++i) {
        units[i] = i;
    }
    for (int i = U_N - 1; i > 0; --i) {
        const int j = (int)rnd_below((uint32_t)i + 1U);
        const int t = units[i];
        units[i] = units[j];
        units[j] = t;
    }
    logf_frame(" | ");
    for (int i = 0; i < U_N; ++i) {
        maybe_op(2U, "mid");
        if (chance(4U)) {
            logf_frame(" skip%d", units[i]); /* the game leaves a unit out: its overlays close at the next begin */
            continue;
        }
        decl_unit(units[i]);
    }
    maybe_op(2U, "late");
    if (nt_ui_wants_pointer(w.ctx) != w.wants_pointer) {
        fail(INV_ROUTER, "nt_ui_wants_pointer was %d after begin, %d before end", (int)w.wants_pointer, !w.wants_pointer);
    }
    if (nt_ui_wants_keyboard(w.ctx) != w.wants_keyboard) {
        fail(INV_ROUTER, "nt_ui_wants_keyboard was %d after begin, %d before end", (int)w.wants_keyboard, !w.wants_keyboard);
    }
}
// #endregion

// #region driver
/* Aims at what the last frame laid out: half the time at a widget the top owns, so every overlay gets clicks. */
static void pick_pointer(float *x, float *y) {
    const uint32_t k = rnd_below(100U);
    const bool at_list = nt_ui_get_bbox(w.ctx, nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(0))).found && chance(30U); /* the wheel lands on a shown list */
    const bool at_top = w.top != 0U && chance(65U);
    const bool at_trigger = w.top == 0U && chance(12U); /* the list opens only from its trigger */
    const uint32_t sub_row = nt_ui_menu_test_item_id(ID_MENU, KEY_SUB);
    const uint32_t leaf = nt_ui_menu_test_item_id(sub_row, chance(50U) ? KEY_L1 : KEY_L2);
    const bool at_sub = w.top == ID_MENU && chance(40U); /* hover the parent, then a leaf of the level it opened */
    target_t t[TARGETS_N];
    const uint32_t n = pointer_targets(t);
    for (int tries = 0; k < 70U && tries < 8; ++tries) {
        const target_t *c = &t[rnd_below(n)];
        uint32_t id = c->id;
        if (at_sub) {
            id = nt_ui_get_bbox(w.ctx, leaf).found ? leaf : sub_row;
        } else if (at_list) {
            id = nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(rnd_below(3U)));
        } else if (at_trigger) {
            id = nt_ui_combo_trigger_id(ID_COMBO);
        } else if (at_top && (w.allowed & bit(c->ov)) == 0U) {
            continue;
        }
        const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, id);
        if (bb.found) {
            *x = bb.x + (bb.width * 0.5F) + (float)rnd_below(9U) - 4.0F;
            *y = bb.y + (bb.height * 0.5F) + (float)rnd_below(5U) - 2.0F;
            w.aim_id = id;
            logf_frame(" at %x", (unsigned)id);
            return;
        }
    }
    if (k < 85U) {
        *x = (float)rnd_below(800U);
        *y = (float)rnd_below(600U);
        w.aim_id = 0U;
        logf_frame(" at (%d,%d)", (int)*x, (int)*y);
    }
}

static const char *key_name(nt_key_t key) {
    switch (key) {
    case NT_KEY_ESCAPE:
        return " Esc";
    case NT_KEY_ARROW_DOWN:
        return " Down";
    case NT_KEY_ARROW_RIGHT:
        return " Right";
    case NT_KEY_ENTER:
        return " Enter";
    default:
        return "";
    }
}

static nt_key_t pick_key(void) {
    const uint32_t k = rnd_below(100U);
    if (k < 6U) {
        return NT_KEY_ESCAPE;
    }
    if (k < 12U) {
        return NT_KEY_ARROW_DOWN;
    }
    if (k < 18U) {
        return NT_KEY_ARROW_RIGHT;
    }
    return (k < 22U) ? NT_KEY_ENTER : NT_KEY_COUNT;
}

static void snapshot_end(void) {
    const nt_ui_context_t *ctx = w.ctx;
    if (ctx->overlay_depth != 0U || ctx->overlay_count > NT_UI_OVERLAY_MAX) {
        fail(INV_STACK, "after nt_ui_end overlay_depth %u, overlay_count %u", (unsigned)ctx->overlay_depth, (unsigned)ctx->overlay_count);
    }
    look();
    w.closed_since_end = 0U;
    w.open_end_n = 0U;
    w.open_end_bits = 0U;
    w.orphans = 0U;
    for (uint8_t i = 0U; i < ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &ctx->overlays[i];
        if (o->id == 0U || !o->open) {
            continue;
        }
        w.open_end[w.open_end_n++] = o->id;
        const int ov = ov_of(o->id);
        const int parent = ov_of(o->parent);
        w.open_end_bits |= (ov >= 0) ? bit(ov) : 0U;
        if (ov >= 0 && parent >= 0 && open_on_stack(o->parent) && (w.declared & bit(ov)) != 0U && (w.declared & bit(parent)) == 0U) {
            w.orphans |= bit(ov);
        }
    }
    check_tree("after end");
}

static bool scroll_at_rest(const nt_ui_scroll_state_t *b) { return b->vel[1] == 0.0F && b->pos[1] == b->target[1] && (b->flags & NT_UI_SCROLL_FLAG_SCROLL_TO) == 0U; }

/* A scroll in a deaf (OPENING or CLOSING) body takes no wheel of that frame; an open one does. A scroll at rest on
 * both sides of nt_ui_begin moves only by the wheel. */
static void check_wheel(void) {
    nt_ui_scroll_state_t now;
    const nt_ui_scroll_state_t *b = &w.list_scroll_before;
    if (!w.wheel_on_list || w.list_phase < 0 || !w.list_rest_pre || !scroll_at_rest(b) || !list_scroll_read(&now)) {
        return;
    }
    if (w.list_phase != PH_OPEN) {
        r.n_closing_wheels += (w.list_phase == PH_CLOSING) ? 1 : 0;
        if (now.pos[1] != b->pos[1]) {
            fail(INV_WHEEL, "the %s list scrolled from %.2f to %.2f on the frame's wheel", s_ph_name[w.list_phase], (double)b->pos[1], (double)now.pos[1]);
        }
    } else {
        r.n_open_wheel_moves += (now.pos[1] != b->pos[1]) ? 1 : 0;
    }
}

/* The light overlay a modal was opened from is closed after that frame, the modal open (S2). */
static void check_modal_from(void) {
    if (!w.from_armed) {
        return;
    }
    ++r.n_modal_from[w.from_opener];
    const bool opener_open = is_open(w.from_opener) || (w.from_opener == OV_SUB && is_open(OV_MENU));
    if (opener_open || !is_open(w.from_modal)) {
        fail(INV_MODAL, "%s opened from %s: %s open %d, %s open %d after the frame", s_ov_name[w.from_modal], s_ov_name[w.from_opener], s_ov_name[w.from_opener], (int)opener_open,
             s_ov_name[w.from_modal], (int)is_open(w.from_modal));
    }
}

/* What a menu activation or a combo pick opened is open after that frame, its menu or list closed (M4). */
static void check_response(void) {
    if (!w.resp_armed) {
        return;
    }
    ++r.n_resp[w.resp_src];
    if (is_open(w.resp_src) || !is_open(w.resp_ov)) {
        fail(INV_RESPONSE, "%s opened by a %s row: %s open %d, %s open %d after the frame", s_ov_name[w.resp_ov], s_ov_name[w.resp_src], s_ov_name[w.resp_src], (int)is_open(w.resp_src),
             s_ov_name[w.resp_ov], (int)is_open(w.resp_ov));
    }
}

/* What nt_ui_begin starts from: the entries appended since the last prune, the catcher records it may drop, and
 * the fades it may cut. */
static void snapshot_before_begin(void) {
    const nt_ui_context_t *ctx = w.ctx;
    w.opened_since_n = 0U;
    w.pre_fading_n = 0U;
    for (uint8_t i = 0U; i < ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &ctx->overlays[i];
        if (i >= w.begin_count && o->id != 0U) {
            w.opened_since[w.opened_since_n++] = o->id;
        }
        if (o->id != 0U && o->seen && !o->open) {
            w.pre_fading[w.pre_fading_n++] = o->id;
        }
    }
    r.n_fade_cuts += (w.pre_fading_n > NT_UI_OVERLAY_MAX / 2U) ? 1 : 0;
    nt_ui_scroll_state_t list = {0};
    w.list_rest_pre = list_scroll_read(&list) && scroll_at_rest(&list);
    w.pre_changed = w.closed_since_end & w.open_end_bits;
    w.pre_closed = w.closed_since_decl;
    w.pre_catcher = 0U;
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        if (id != 0U && has_record(ctx->interactive_cur, ctx->interactive_cur_count, nt_ui_internal_popup_catcher_id(id))) {
            w.pre_catcher |= bit(ov);
        }
    }
}

static void run_frame(void) {
    w.log[w.frame][0] = '\0';
    w.opens_left = OPENS_PER_FRAME;
    w.declared = 0U;
    w.from_armed = false;
    w.resp_armed = false;
    w.enter_row = 0U;
    w.spell = (w.spell == 0 && chance(1U)) ? 1 : w.spell;
    w.quiet = w.spell > 0 && w.spell <= SPELL_BUILD;
    const bool paused = w.spell > SPELL_BUILD;
    r.n_paused_frames += paused ? 1 : 0;
    if (w.spell > 0) {
        logf_frame("%s", paused ? " PAUSE" : " SPELL");
    }
    spell_op();
    w.spell = (w.spell > 0 && w.spell < SPELL_LEN) ? w.spell + 1 : 0;
    maybe_op(8U, "pre");
    maybe_op(3U, "pre");
    look();
    snapshot_before_begin();
    const bool left = w.sc.down[NT_BUTTON_LEFT];
    const bool right = w.sc.down[NT_BUTTON_RIGHT];
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
    const nt_key_t key = w.quiet ? NT_KEY_COUNT : pick_key();
    w.held_before = left || right;
    w.press_any = press >= 0;
    w.press_left = press == NT_BUTTON_LEFT;
    w.release_left = release == NT_BUTTON_LEFT;
    r.n_right_presses += (press == NT_BUTTON_RIGHT) ? 1 : 0;
    if (w.press_left) {
        w.held_btn = w.held_before ? 0U : body_button_at(x, y);
    } else if (w.release_left) {
        w.held_btn = 0U;
    }
    w.enter_now = key == NT_KEY_ENTER;
    w.wheel_now = 0.0F;
    if (chance(20U)) {
        w.wheel_now = chance(50U) ? 1.0F : -1.0F;
    }
    logf_frame("%s%s%s%s%s%s", (press == NT_BUTTON_LEFT) ? " PRESS" : "", (press == NT_BUTTON_RIGHT) ? " RPRESS" : "", (release == NT_BUTTON_LEFT) ? " RELEASE" : "",
               (release == NT_BUTTON_RIGHT) ? " RRELEASE" : "", key_name(key), (w.wheel_now != 0.0F) ? " WHEEL" : "");
    if (key != NT_KEY_COUNT) {
        nt_input_feed(nt_test_key(key, true));
    }
    sc_step_dt(&w.sc, paused ? 0.0F : UI_FX_DT, x, y, press, release, w.wheel_now);
    if (key != NT_KEY_COUNT) {
        nt_input_feed(nt_test_key(key, false));
    }
    snapshot_end();
    check_wheel();
    check_modal_from();
    check_response();
#if UI_INV_TRACE
    printf("s%u f%03d %s\n", (unsigned)w.seed, w.frame, w.log[w.frame]);
#endif
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
    w.dd_style.list_scroll.wheel_ease_speed = 0.0F;
    w.combo_sel = -1;
    w.press_allowed = 0xFFFFFFFFU;
    w.sc = (sc_t){.ctx = w.ctx, .frame = frame_fn, .game = NULL};
    for (w.frame = 0; w.frame < FRAMES; ++w.frame) {
        run_frame();
    }
    w.frame = FRAMES - 1;
    ui_walker_fixture_shutdown(&s_fx);
    nt_input_shutdown();
}
// #endregion

static void test_overlay_invariants_hold_over_random_frames(void) {
    for (uint32_t seed = 1U; seed <= SEEDS; ++seed) {
        run_seed(seed);
    }
    char summary[INV_N * 520];
    summary[0] = '\0';
    size_t used = 0U;
    for (int inv = 0; inv < INV_N; ++inv) {
        printf("inv %-28s %s (%d failing checks)\n", s_inv_name[inv], (r.fail_count[inv] == 0) ? "PASS" : "FAIL", r.fail_count[inv]);
        if (r.fail_count[inv] != 0 && used < sizeof summary) {
            const int n = snprintf(summary + used, sizeof summary - used, "%s; ", r.fail_msg[inv]);
            used += (n > 0) ? (size_t)n : 0U;
        }
    }
    /* What each invariant checked, by its number: one that never ran proves nothing. */
    const struct {
        const char *name;
        int n;
    } cov[] = {
        {"1 stack entries", r.n_stack_checks},
        {"1 begins that cut fades", r.n_fade_cuts},
        {"1 paused frames", r.n_paused_frames},
        {"2 frames with a top", r.n_top_frames},
        {"2 paint pairs", r.n_paint_pairs},
        {"3 catchers kept", r.n_catcher_kept},
        {"3 catchers dropped", r.n_catcher_drops},
        {"3 of them closed before begin", r.n_pre_close_drops},
        {"4 OPENING declarations", r.n_opening},
        {"4 CLOSING declarations", r.n_closing},
        {"4 releases on a deaf button", r.n_deaf_releases},
        {"5 frames wanting the pointer", r.n_wants_pointer},
        {"5 frames wanting the keyboard", r.n_wants_keyboard},
        {"6 clicks P0", r.n_clicks[OV_P0]},
        {"6 clicks P1", r.n_clicks[OV_P1]},
        {"6 clicks M0", r.n_clicks[OV_M0]},
        {"6 clicks M1", r.n_clicks[OV_M1]},
        {"6 clicks P2", r.n_clicks[OV_P2]},
        {"6 clicks LIST", r.n_clicks[OV_LIST]},
        {"6 clicks MENU", r.n_clicks[OV_MENU]},
        {"6 clicks SUB", r.n_clicks[OV_SUB]},
        {"6 clicks BASE", r.n_clicks[OV_BASE]},
        {"6 Enter rows", r.n_enter_rows},
        {"7 gesture presses", r.n_gesture_presses},
        {"7 joining presses", r.n_gesture_joins},
        {"7 held frames", r.n_gesture_holds},
        {"7 right presses", r.n_right_presses},
        {"7 game releases over the UI", r.n_game_release_over_ui},
        {"7 free pointers", r.n_free_checks},
        {"8 children of a left-out parent", r.n_orphans},
        {"8 opens inside a closed body", r.n_open_in_closed},
        {"9 wheels on a CLOSING list", r.n_closing_wheels},
        {"9 wheels moving an open list", r.n_open_wheel_moves},
        {"10 modal from LIST", r.n_modal_from[OV_LIST]},
        {"10 modal from MENU", r.n_modal_from[OV_MENU]},
        {"10 modal from SUB", r.n_modal_from[OV_SUB]},
        {"10 modal from P1", r.n_modal_from[OV_P1]},
        {"11 opens from a menu activation", r.n_resp[OV_MENU]},
        {"11 opens from a combo pick", r.n_resp[OV_LIST]},
    };
    char zero[512];
    zero[0] = '\0';
    size_t zused = 0U;
    printf("coverage:");
    for (size_t i = 0U; i < sizeof cov / sizeof cov[0]; ++i) {
        printf("%s %s %d", (i == 0U) ? "" : ",", cov[i].name, cov[i].n);
        if (cov[i].n == 0 && zused < sizeof zero) {
            const int n = snprintf(zero + zused, sizeof zero - zused, "%s; ", cov[i].name);
            zused += (n > 0) ? (size_t)n : 0U;
        }
    }
    printf("\n");
    if (used != 0U) {
        TEST_FAIL_MESSAGE(summary);
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", zero, "an invariant that never ran proves nothing: these coverage counts are 0");
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_overlay_invariants_hold_over_random_frames);
    return UNITY_END();
}
