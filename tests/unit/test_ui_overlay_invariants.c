/* Randomized invariant checker for the overlay stack and the input router (spec chapter UI, "Input ownership",
 * "Popup-core", "Immediate menu + combo"). Fixed seeds drive opens, closes, reopens and nested opens made before
 * nt_ui_begin, between and inside the declarations, modals opened from menu and combo rows, a shuffled root
 * declaration order with units left out, pointer moves, presses, releases and wheel, Esc, Down, Right and Enter;
 * every frame checks the stack, paint versus router order, catcher records, the deaf OPENING/CLOSING bodies, the
 * router answers, click-through, the gesture's pointer owner, the parent chain, the deaf scroll and the modal
 * opened from a light overlay. It reads internals on purpose: it checks the engine's own bookkeeping, not one
 * game story. */

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

#define SEEDS 32
#define FRAMES 500
#define LOG_W 320
#define TRACE_BACK 16
#define OPENS_PER_FRAME 2 /* keeps live + dead entries under NT_UI_OVERLAY_MAX */

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

// #region ids
enum { OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_LIST, OV_MENU, OV_SUB, OV_N, OV_BASE = OV_N };
enum { PH_OPENING, PH_OPEN, PH_CLOSING };
enum { INV_STACK, INV_PAINT, INV_CATCHER, INV_OPENING, INV_ROUTER, INV_THROUGH, INV_GESTURE, INV_TREE, INV_WHEEL, INV_MODAL, INV_N };

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
                                         "10 modal from a light overlay closes it"};

#define ID_P0 0x6A0001U
#define ID_P1 0x6A0002U
#define ID_M0 0x6A0003U
#define ID_M1 0x6A0004U
#define ID_P2 0x6A0005U
#define ID_COMBO 0x6A0010U
#define ID_LIST ID_COMBO /* the list is the overlay under the combo id */
#define ID_MENU 0x6A0020U
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
typedef struct {
    nt_ui_context_t *ctx;
    sc_t sc;
    uint32_t rng;
    uint32_t seed;
    int frame;
    nt_ui_menu_t menu;
    nt_ui_menu_style_t menu_style;
    nt_ui_dropdown_style_t dd_style;
    int combo_sel;
    uint32_t sub_id; /* the menu's submenu level, found on the stack once it opens */
    /* (Re)open detection: within a frame stack entries are only appended, so a changed slot is a new entry. */
    int32_t ov_index[OV_N];
    bool pending[OV_N]; /* (re)opened since its last declaration: the next one is OPENING */
    int chain[NT_UI_OVERLAY_MAX];
    uint8_t chain_phase[NT_UI_OVERLAY_MAX];
    int chain_n;
    int opens_left;
    /* This frame, read right after nt_ui_begin. */
    uint8_t begin_count;
    uint32_t top;
    uint32_t allowed; /* bit per OV_*: whose clicks this begin's top permits */
    bool wants_pointer;
    bool wants_keyboard;
    bool press_now;
    bool release_now;
    bool enter_now;
    uint32_t press_allowed; /* `allowed` of the frame of the last press */
    /* The previous frame's end and the ops since. */
    uint32_t open_end[NT_UI_OVERLAY_MAX];
    uint8_t open_end_n;
    uint32_t opened_since[NT_UI_OVERLAY_MAX];
    uint8_t opened_since_n;
    uint32_t pre_changed; /* bit per OV_*: closed or (re)opened after the last nt_ui_end, before this nt_ui_begin */
    uint32_t declared;    /* bit per OV_*: its begin ran this frame */
    uint32_t orphans;     /* bit per OV_*: open at the last end, declared, its open parent was not */
    uint32_t born_orphan; /* bit per OV_*: last opened inside a body that was already closing */
    /* The gesture from the last press up to its release, and the owner its press frame reported. */
    bool drag_active;
    bool drag_ui;
    /* The wheel of this frame and the list scroll as nt_ui_begin left it. */
    float wheel_now;
    bool wheel_on_list;
    nt_ui_scroll_state_t list_scroll_before;
    int list_phase; /* the list's phase this frame, -1 when it did not show */
    /* A modal opened from inside a light overlay this frame, until a later op in the frame touches the stack. */
    bool from_armed;
    int from_opener;
    char log[FRAMES][LOG_W];
    int fail_count[INV_N];
    char fail_msg[INV_N][512];
    int note_count; /* records of overlays closed or reopened before nt_ui_begin (no spec rule drops them) */
    char note_msg[512];
    /* Coverage, printed at the end: an invariant that never ran proves nothing. */
    int n_top_frames, n_paint_pairs, n_opening, n_closing, n_deaf_releases, n_clicks[OV_N + 1];
    int n_born_orphans, n_game_drag_over_ui, n_game_release_over_ui, n_orphans, n_deaf_wheels, n_closing_wheels, n_open_wheel_moves, n_modal_from[OV_N];
} world_t;

static world_t w;

static uint32_t rnd(void) {
    w.rng ^= w.rng << 13U;
    w.rng ^= w.rng >> 17U;
    w.rng ^= w.rng << 5U;
    return w.rng;
}

static uint32_t rnd_below(uint32_t n) { return rnd() % n; }
static bool chance(uint32_t percent) { return rnd_below(100U) < percent; }

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
    for (int ov = 0; ov < OV_N; ++ov) {
        if (id != 0U && ov_id(ov) == id) {
            return s_ov_name[ov];
        }
    }
    return (id == 0U) ? "none" : "?";
}

__attribute__((format(printf, 2, 3))) static void fail(int inv, const char *fmt, ...) {
    if (w.fail_count[inv]++ != 0) {
        return;
    }
    char what[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    (void)snprintf(w.fail_msg[inv], sizeof w.fail_msg[inv], "inv %s: seed %u frame %d: %s", s_inv_name[inv], (unsigned)w.seed, w.frame, what);
    printf("FIRST FAILURE %s\n  trace (frame: pre-begin ops | input | in-frame ops and events):\n", w.fail_msg[inv]);
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

/* Marks every overlay whose stack entry changed since the last look as (re)opened; returns them as bits. */
static uint32_t observe(void) {
    find_sub();
    uint32_t opened = 0U;
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        const int32_t at = (id != 0U) ? stack_index(id) : -1;
        if (at >= 0 && at != w.ov_index[ov]) {
            w.pending[ov] = true;
            opened |= 1U << (uint32_t)ov;
        }
        w.ov_index[ov] = at;
    }
    return opened;
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

#define TARGETS_N 16U

/* What the pointer aims at: a widget of every overlay and of the base. */
static uint32_t pointer_targets(uint32_t out[TARGETS_N]) {
    const uint32_t sub_row = nt_ui_menu_test_item_id(ID_MENU, KEY_SUB);
    const uint32_t ids[] = {BTN_P0,
                            BTN_P1,
                            BTN_M0,
                            BTN_M1,
                            BTN_P2,
                            BTN_MROW,
                            nt_ui_combo_trigger_id(ID_COMBO),
                            nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(0)),
                            nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(COMBO_CUSTOM_ROW)),
                            nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(2)),
                            nt_ui_menu_test_item_id(ID_MENU, KEY_EDIT),
                            sub_row,
                            nt_ui_menu_test_item_id(sub_row, KEY_L1),
                            BTN_BASE0,
                            BTN_BASE1};
    const uint32_t n = (uint32_t)(sizeof ids / sizeof ids[0]);
    memcpy(out, ids, sizeof ids);
    return n;
}

/* The pointer lies on a target last frame laid out. */
static bool pointer_over_ui(void) {
    uint32_t ids[TARGETS_N];
    const uint32_t n = pointer_targets(ids);
    for (uint32_t i = 0U; i < n; ++i) {
        const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, ids[i]);
        if (bb.found && w.sc.x >= bb.x && w.sc.x < bb.x + bb.width && w.sc.y >= bb.y && w.sc.y < bb.y + bb.height) {
            return true;
        }
    }
    return false;
}
// #endregion

// #region checks at nt_ui_begin
static void check_stack_after_begin(void) {
    const nt_ui_context_t *ctx = w.ctx;
    if (ctx->overlay_count > NT_UI_OVERLAY_MAX) {
        fail(INV_STACK, "overlay_count %u after begin", (unsigned)ctx->overlay_count);
    }
    for (uint8_t i = 0U; i < ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &ctx->overlays[i];
        if (o->id == 0U) {
            fail(INV_STACK, "dead entry in slot %u after begin", (unsigned)i);
            continue;
        }
        const bool declared = nt_ui_internal_live_slot(ctx, o->id) >= 0;
        if (!declared && !in_list(w.opened_since, w.opened_since_n, o->id)) {
            fail(INV_STACK, "%s (slot %u, open %d) kept although neither declared last frame nor opened since the last begin", id_name(o->id), (unsigned)i, (int)o->open);
        }
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
        /* Open now: one closed before this begin keeps last frame's records (see check_catchers). */
        if (o->id == 0U || o->id == top || !o->live || !o->open || !in_list(w.open_end, w.open_end_n, o->id) || descends_from(o->id, top)) {
            continue;
        }
        const int32_t panel = order_of(o->id);
        w.n_paint_pairs += (panel >= 0) ? 1 : 0;
        if (panel >= 0 && catcher <= panel) {
            fail(INV_PAINT, "top %s catcher order %d is not above unrelated open %s panel order %d", id_name(top), (int)catcher, id_name(o->id), (int)panel);
        }
    }
}

static bool has_prev_record(uint32_t id) {
    for (uint32_t k = 0U; k < w.ctx->interactive_prev_count; ++k) {
        if (w.ctx->interactive_prev[k].id == id) {
            return true;
        }
    }
    return false;
}

/* A catcher record that reaches this frame belongs to an overlay still open at the last nt_ui_end (R3: nt_ui_end
 * drops the catcher of each overlay closed by then). A close or reopen made after nt_ui_end and before this
 * nt_ui_begin has no dropping rule: noted, not failed. */
static void check_catchers(void) {
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        if (id == 0U || !has_prev_record(nt_ui_internal_popup_catcher_id(id))) {
            continue;
        }
        const int32_t at = stack_index(id);
        if (!in_list(w.open_end, w.open_end_n, id)) {
            fail(INV_CATCHER, "%s was closed at the end of the last frame, its catcher record reached this frame", s_ov_name[ov]);
        } else if ((w.pre_changed & (1U << (uint32_t)ov)) != 0U && (at < 0 || !w.ctx->overlays[at].open || !w.ctx->overlays[at].live) && w.note_count++ == 0) {
            (void)snprintf(w.note_msg, sizeof w.note_msg, "seed %u frame %d: %s closed or reopened before nt_ui_begin, its catcher record reached this frame above top %s", (unsigned)w.seed, w.frame,
                           s_ov_name[ov], id_name(w.top));
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
            bits |= 1U << (uint32_t)ov;
        }
    }
    return bits;
}

/* An overlay declared last frame whose open parent was not closes at this begin with that parent (S1). */
static void check_orphans(void) {
    for (int ov = 0; ov < OV_N; ++ov) {
        if ((w.orphans & (1U << (uint32_t)ov)) == 0U) {
            continue;
        }
        ++w.n_orphans;
        const uint32_t id = ov_id(ov);
        if (nt_ui_is_open(w.ctx, id) && !in_list(w.opened_since, w.opened_since_n, id)) {
            fail(INV_TREE, "%s stayed open at the begin after the overlay it was opened inside was left out", s_ov_name[ov]);
        }
    }
}

/* A gesture's owner keeps nt_ui_wants_pointer from its press to its release, over the UI too (R2). */
static void check_gesture(void) {
    if (w.press_now) {
        w.drag_active = true;
        w.drag_ui = w.wants_pointer;
    } else if (w.drag_active && w.wants_pointer != w.drag_ui) {
        fail(INV_GESTURE, "nt_ui_wants_pointer %d during a %s gesture%s", (int)w.wants_pointer, w.drag_ui ? "UI" : "game", w.release_now ? " on its release" : "");
    }
    if (w.drag_active && !w.drag_ui && pointer_over_ui()) {
        w.n_game_drag_over_ui += 1;
        w.n_game_release_over_ui += w.release_now ? 1 : 0;
    }
    if (w.release_now) {
        w.drag_active = false;
    }
}

static void after_begin(void) {
    w.begin_count = w.ctx->overlay_count;
    w.top = w.ctx->overlay_top;
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        w.ov_index[ov] = (id != 0U) ? stack_index(id) : -1; /* the prune moved the slots: no reopen */
    }
    check_stack_after_begin();
    check_paint_order();
    check_catchers();
    check_orphans();
    check_tree("after begin");
    w.allowed = allowed_by_top();
    w.n_top_frames += (w.top != 0U) ? 1 : 0;
    if (w.press_now) {
        w.press_allowed = w.allowed;
    }
    w.wants_pointer = nt_ui_wants_pointer(w.ctx);
    w.wants_keyboard = nt_ui_wants_keyboard(w.ctx);
    check_gesture();
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

static void op_open(int ov, const char *where) {
    if (w.opens_left <= 0 || in_chain(ov)) {
        return; /* an overlay is not opened inside its own declaration */
    }
    --w.opens_left;
    w.from_armed = false;
    const int32_t before = stack_index(ov_id(ov)); /* a (re)open appends a new entry; opening an open popup does nothing */
    switch (ov) {
    case OV_P0:
    case OV_P1:
    case OV_P2:
        nt_ui_popup_open(w.ctx, ov_id(ov));
        break;
    case OV_M0:
    case OV_M1:
        nt_ui_modal_open(w.ctx, ov_id(ov));
        break;
    default: {
        const float x = 40.0F + (float)rnd_below(520U);
        const float y = 40.0F + (float)rnd_below(380U);
        nt_ui_menu_open(&w.menu, w.ctx, ID_MENU, x, y);
        break;
    }
    }
    const int32_t at = stack_index(ov_id(ov));
    const uint32_t parent = (at >= 0) ? w.ctx->overlays[at].parent : 0U;
    const bool orphan = parent != 0U && !open_on_stack(parent);
    const uint32_t bit = 1U << (uint32_t)ov;
    if (at != before) {
        w.born_orphan = orphan ? (w.born_orphan | bit) : (w.born_orphan & ~bit);
        w.n_born_orphans += orphan ? 1 : 0;
    }
    logf_frame(" %s:open %s", where, s_ov_name[ov]);
}

static void op_close(int ov, const char *where) {
    const uint32_t id = ov_id(ov);
    if (id != 0U) {
        nt_ui_close(w.ctx, id);
        w.from_armed = false;
        logf_frame(" %s:close %s", where, s_ov_name[ov]);
    }
}

/* A modal opened from inside the open light overlay `opener` (a menu level, the combo list) while it is declared:
 * the opener closes and the modal stays (S2, as showModal). */
static void op_modal_from(int opener, const char *where) {
    if (w.opens_left <= 0 || nt_ui_is_open(w.ctx, ID_M0) || !nt_ui_is_open(w.ctx, ov_id(opener))) {
        return;
    }
    --w.opens_left;
    nt_ui_modal_open(w.ctx, ID_M0);
    w.from_armed = true;
    w.from_opener = opener;
    logf_frame(" %s:modal M0", where);
}

/* The overlay whose body button the primary pointer holds, else a random openable one. */
static int held_or_random(const int *openable, uint32_t n) {
    static const uint32_t btns[] = {BTN_P0, BTN_P1, BTN_M0, BTN_M1, BTN_P2, 0U, BTN_MROW};
    static const int owners[] = {OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_LIST, OV_MENU};
    const uint32_t held = w.ctx->captures[0].active_id;
    for (uint32_t i = 0U; held != 0U && i < sizeof btns / sizeof btns[0]; ++i) {
        if (btns[i] == held) {
            return owners[i];
        }
    }
    return openable[rnd_below(n)];
}

static void maybe_op(uint32_t percent, const char *where) {
    if (!chance(percent)) {
        return;
    }
    static const int openable[] = {OV_P0, OV_P1, OV_M0, OV_M1, OV_P2, OV_MENU};
    const uint32_t r = rnd_below(100U);
    if (r < 40U) {
        op_open(openable[rnd_below(6U)], where);
    } else if (r < 70U) {
        op_close((int)rnd_below(OV_N), where);
    } else {
        const int ov = held_or_random(openable, 6U); /* a reopen during the fade, often under a held press */
        if (!in_chain(ov)) {
            op_close(ov, where);
            op_open(ov, where);
        }
    }
}
// #endregion

// #region declarations
static void chain_push(int ov, uint8_t phase) {
    w.chain[w.chain_n] = ov;
    w.chain_phase[w.chain_n] = phase;
    ++w.chain_n;
}

/* The phase of a shown declaration (spec "Overlay lifecycle"): OPENING on the first declaration since a
 * (re)open, else OPEN while open, else CLOSING. */
static uint8_t take_phase(int ov, bool open_before) {
    (void)observe();
    const bool first = w.pending[ov];
    w.pending[ov] = false;
    uint8_t phase = open_before ? (uint8_t)PH_OPEN : (uint8_t)PH_CLOSING;
    if (first) {
        phase = PH_OPENING;
    }
    logf_frame(" %s(%s)", s_ov_name[ov], s_ph_name[phase]);
    w.n_opening += (phase == PH_OPENING) ? 1 : 0;
    w.n_closing += (phase == PH_CLOSING) ? 1 : 0;
    return phase;
}

static void on_click(int ov, uint32_t what, bool row) {
    logf_frame(" CLICK %s:%x", s_ov_name[ov], (unsigned)what);
    ++w.n_clicks[ov];
    for (int i = 0; i < w.chain_n; ++i) {
        if (w.chain_phase[i] != PH_OPEN) {
            fail(INV_OPENING, "%x in %s's body reports a click while %s is %s", (unsigned)what, s_ov_name[ov], s_ov_name[w.chain[i]], s_ph_name[w.chain_phase[i]]);
        }
    }
    if (row && w.enter_now) {
        return; /* Enter activates without a press */
    }
    const uint32_t bit = 1U << (uint32_t)ov;
    if ((w.press_allowed & bit) == 0U) {
        fail(INV_THROUGH, "%x in %s clicked although the press frame's top was another overlay", (unsigned)what, s_ov_name[ov]);
    }
    if ((w.allowed & bit) == 0U) {
        fail(INV_THROUGH, "%x in %s clicked although this frame's top %s does not own it", (unsigned)what, s_ov_name[ov], id_name(w.top));
    }
}

static void button(uint32_t id, int ov) {
    CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(40)}}}) {}
    bool deaf = false;
    for (int i = 0; i < w.chain_n; ++i) {
        deaf = deaf || w.chain_phase[i] != PH_OPEN;
    }
    const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, id);
    const bool over = bb.found && w.sc.x >= bb.x && w.sc.x < bb.x + bb.width && w.sc.y >= bb.y && w.sc.y < bb.y + bb.height;
    w.n_deaf_releases += (deaf && w.release_now && over) ? 1 : 0; /* a click inv 4 had to stop */
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
    const bool open_before = nt_ui_is_open(w.ctx, ov_id(ov));
    w.declared |= 1U << (uint32_t)ov;
    if (!nt_ui_popup_begin(w.ctx, ov_id(ov), &ps, &anc, NULL)) {
        return;
    }
    chain_push(ov, take_phase(ov, open_before));
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
    ms.ease_speed = 14.0F;
    ms.flags = flags;
    const bool open_before = nt_ui_is_open(w.ctx, ov_id(ov));
    w.declared |= 1U << (uint32_t)ov;
    if (!nt_ui_modal_begin(w.ctx, ov_id(ov), &ms, NULL)) {
        return;
    }
    chain_push(ov, take_phase(ov, open_before));
    button(btn, ov);
    maybe_op(5U, s_ov_name[ov]);
    if (inner != NULL) {
        inner();
    }
    --w.chain_n;
    nt_ui_modal_end(w.ctx);
}

/* P1's body holds M1, M0's body holds P2: each is opened inside its parent's body on some frames. M0's body also
 * opens P0, declared at the root. */
static void inner_m1(void) {
    if (chance(15U)) {
        op_open(OV_M1, "P1body");
    }
    decl_modal(OV_M1, BTN_M1, NT_UI_MODAL_LISTEN_ESC, NULL);
}

static void inner_p2(void) {
    if (chance(15U)) {
        op_open(OV_P2, "M0body");
    }
    if (chance(10U)) {
        op_open(OV_P0, "M0body"); /* declared at the root: closes with M0 though declared elsewhere */
    }
    decl_popup(OV_P2, BTN_P2, 420.0F, 330.0F, 0.0F, NULL);
}

static void decl_combo(void) {
    static const char *const rows[] = {"Alpha", "Beta", "Gamma"};
    CLAY({.id = (Clay_ElementId){.id = nt_ui_fmix_id(ID_COMBO, 1U)}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 260.0F}}}) {
        const bool open_before = nt_ui_is_open(w.ctx, ID_LIST);
        w.declared |= 1U << (uint32_t)OV_LIST;
        if (nt_ui_combo_begin(w.ctx, NULL, 0U, ID_COMBO, "pick", &w.dd_style)) {
            const uint8_t phase = take_phase(OV_LIST, open_before);
            w.list_phase = phase;
            chain_push(OV_LIST, phase);
            for (int i = 0; i < COMBO_ROWS; ++i) {
                bool picked = false;
                if (i == COMBO_CUSTOM_ROW) {
                    nt_ui_combo_selectable_begin(w.ctx, ROW_KEY(i), w.combo_sel == i);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(90), CLAY_SIZING_FIXED(20)}}}) {}
                    if (chance(4U)) {
                        op_modal_from(OV_LIST, "LISTrow");
                    }
                    picked = nt_ui_combo_selectable_end(w.ctx);
                } else {
                    picked = nt_ui_combo_selectable(w.ctx, ROW_KEY(i), rows[i % 3], w.combo_sel == i);
                }
                if (picked) {
                    w.combo_sel = i;
                    on_click(OV_LIST, nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(i)), true);
                }
            }
            maybe_op(5U, "LIST");
            --w.chain_n;
            nt_ui_combo_end(w.ctx);
        }
    }
}

static void decl_menu(void) {
    const bool open_before = nt_ui_is_open(w.ctx, ID_MENU);
    w.declared |= 1U << (uint32_t)OV_MENU;
    if (!nt_ui_menu_begin(&w.menu, w.ctx, NULL, 0U, &w.menu_style)) {
        return;
    }
    chain_push(OV_MENU, take_phase(OV_MENU, open_before));
    const uint32_t sub_row = nt_ui_menu_test_item_id(ID_MENU, KEY_SUB);
    const bool sub_before = w.sub_id != 0U && nt_ui_is_open(w.ctx, w.sub_id);
    w.declared |= 1U << (uint32_t)OV_SUB;
    if (nt_ui_menu_submenu_begin(&w.menu, KEY_SUB, "Sub")) {
        chain_push(OV_SUB, take_phase(OV_SUB, sub_before));
        if (chance(4U)) {
            op_modal_from(OV_SUB, "SUB");
        }
        if (nt_ui_menu_item(&w.menu, KEY_L1, "Leaf1")) {
            on_click(OV_SUB, nt_ui_menu_test_item_id(sub_row, KEY_L1), true);
        }
        if (nt_ui_menu_item(&w.menu, KEY_L2, "Leaf2")) {
            on_click(OV_SUB, nt_ui_menu_test_item_id(sub_row, KEY_L2), true);
        }
        --w.chain_n;
        nt_ui_menu_submenu_end(&w.menu);
    }
    if (nt_ui_menu_item(&w.menu, KEY_EDIT, "Edit")) {
        on_click(OV_MENU, nt_ui_menu_test_item_id(ID_MENU, KEY_EDIT), true);
    }
    nt_ui_menu_item_begin(&w.menu, KEY_ROW, true, false);
    button(BTN_MROW, OV_MENU);
    if (chance(4U)) {
        op_modal_from(OV_MENU, "MROW");
    }
    (void)nt_ui_menu_item_end(&w.menu);
    --w.chain_n;
    nt_ui_menu_end(&w.menu);
}

enum { U_BASE0, U_BASE1, U_P0, U_P1, U_M0, U_COMBO, U_MENU, U_N };

static void decl_unit(int u) {
    switch (u) {
    case U_BASE0:
        decl_base(BTN_BASE0, 60.0F, 60.0F);
        break;
    case U_BASE1:
        decl_base(BTN_BASE1, 340.0F, 240.0F);
        break;
    case U_P0:
        decl_popup(OV_P0, BTN_P0, 100.0F, 100.0F, 0.0F, NULL);
        break;
    case U_P1:
        decl_popup(OV_P1, BTN_P1, 170.0F, 150.0F, 14.0F, inner_m1);
        break;
    case U_M0:
        decl_modal(OV_M0, BTN_M0, (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP), inner_p2);
        break;
    case U_COMBO:
        decl_combo();
        break;
    default:
        decl_menu();
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
static uint32_t pointer_target(void) {
    uint32_t ids[TARGETS_N];
    return ids[rnd_below(pointer_targets(ids))];
}

static void pick_pointer(float *x, float *y) {
    const uint32_t r = rnd_below(100U);
    const bool at_list = nt_ui_is_open(w.ctx, ID_LIST) && chance(50U); /* so the wheel lands on its scroll */
    for (int tries = 0; r < 70U && tries < 6; ++tries) {               /* aim at what the last frame laid out */
        const uint32_t id = at_list ? nt_ui_dropdown_test_combo_row_id(ID_COMBO, ROW_KEY(rnd_below(3U))) : pointer_target();
        const nt_ui_bbox_t bb = nt_ui_get_bbox(w.ctx, id);
        if (bb.found) {
            *x = bb.x + (bb.width * 0.5F) + (float)rnd_below(9U) - 4.0F;
            *y = bb.y + (bb.height * 0.5F) + (float)rnd_below(5U) - 2.0F;
            logf_frame(" at %x", (unsigned)id);
            return;
        }
    }
    if (r < 85U) {
        *x = (float)rnd_below(800U);
        *y = (float)rnd_below(600U);
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
    const uint32_t r = rnd_below(100U);
    if (r < 6U) {
        return NT_KEY_ESCAPE;
    }
    if (r < 12U) {
        return NT_KEY_ARROW_DOWN;
    }
    if (r < 18U) {
        return NT_KEY_ARROW_RIGHT;
    }
    return (r < 22U) ? NT_KEY_ENTER : NT_KEY_COUNT;
}

static void snapshot_end(void) {
    const nt_ui_context_t *ctx = w.ctx;
    if (ctx->overlay_depth != 0U || ctx->overlay_count > NT_UI_OVERLAY_MAX) {
        fail(INV_STACK, "after nt_ui_end overlay_depth %u, overlay_count %u", (unsigned)ctx->overlay_depth, (unsigned)ctx->overlay_count);
    }
    w.open_end_n = 0U;
    w.orphans = 0U;
    for (uint8_t i = 0U; i < ctx->overlay_count; ++i) {
        const nt_ui_overlay_t *o = &ctx->overlays[i];
        if (o->id == 0U || !o->open) {
            continue;
        }
        w.open_end[w.open_end_n++] = o->id;
        const int ov = ov_of(o->id);
        const int parent = ov_of(o->parent);
        if (ov >= 0 && parent >= 0 && open_on_stack(o->parent) && (w.declared & (1U << (uint32_t)ov)) != 0U && (w.declared & (1U << (uint32_t)parent)) == 0U) {
            w.orphans |= 1U << (uint32_t)ov;
        }
    }
    check_tree("after end");
}

/* A scroll in a deaf (OPENING or CLOSING) body takes no wheel of that frame; an open one does. */
static void check_wheel(void) {
    nt_ui_scroll_state_t now;
    const nt_ui_scroll_state_t *b = &w.list_scroll_before;
    const bool settled = b->vel[1] == 0.0F && b->pos[1] == b->target[1] && (b->flags & NT_UI_SCROLL_FLAG_SCROLL_TO) == 0U;
    if (!w.wheel_on_list || w.list_phase < 0 || !settled || !list_scroll_read(&now)) {
        return;
    }
    if (w.list_phase != PH_OPEN) {
        ++w.n_deaf_wheels;
        w.n_closing_wheels += (w.list_phase == PH_CLOSING) ? 1 : 0;
        if (now.pos[1] != b->pos[1]) {
            fail(INV_WHEEL, "the %s list scrolled from %.2f to %.2f on the frame's wheel", s_ph_name[w.list_phase], (double)b->pos[1], (double)now.pos[1]);
        }
    } else {
        w.n_open_wheel_moves += (now.pos[1] != b->pos[1]) ? 1 : 0;
    }
}

/* The light overlay a modal was opened from is closed after that frame, the modal open (S2). */
static void check_modal_from(void) {
    if (!w.from_armed) {
        return;
    }
    ++w.n_modal_from[w.from_opener];
    const bool opener_open = nt_ui_is_open(w.ctx, ov_id(w.from_opener)) || (w.from_opener == OV_SUB && nt_ui_is_open(w.ctx, ID_MENU));
    if (opener_open || !nt_ui_is_open(w.ctx, ID_M0)) {
        fail(INV_MODAL, "M0 opened from %s: %s open %d, M0 open %d after the frame", s_ov_name[w.from_opener], s_ov_name[w.from_opener], (int)opener_open, (int)nt_ui_is_open(w.ctx, ID_M0));
    }
}

/* Entries appended since the last nt_ui_begin's prune were opened since it. */
static void snapshot_opened_since(void) {
    w.opened_since_n = 0U;
    for (uint8_t i = w.begin_count; i < w.ctx->overlay_count; ++i) {
        if (w.ctx->overlays[i].id != 0U) {
            w.opened_since[w.opened_since_n++] = w.ctx->overlays[i].id;
        }
    }
}

static void run_frame(void) {
    w.log[w.frame][0] = '\0';
    w.opens_left = OPENS_PER_FRAME;
    w.declared = 0U;
    w.from_armed = false;
    maybe_op(8U, "pre");
    maybe_op(3U, "pre");
    w.pre_changed = observe();
    for (int ov = 0; ov < OV_N; ++ov) {
        const uint32_t id = ov_id(ov);
        if (id != 0U && in_list(w.open_end, w.open_end_n, id) && !nt_ui_is_open(w.ctx, id)) {
            w.pre_changed |= 1U << (uint32_t)ov;
        }
    }
    snapshot_opened_since();
    float x = w.sc.x;
    float y = w.sc.y;
    if (!w.sc.down[NT_BUTTON_LEFT] || chance(15U)) {
        pick_pointer(&x, &y); /* a held button mostly stays put, so presses become clicks */
    }
    int press = -1;
    int release = -1;
    if (!w.sc.down[NT_BUTTON_LEFT] && chance(35U)) {
        press = NT_BUTTON_LEFT;
    } else if (w.sc.down[NT_BUTTON_LEFT] && chance(60U)) {
        release = NT_BUTTON_LEFT;
    }
    const nt_key_t key = pick_key();
    w.press_now = press >= 0;
    w.release_now = release >= 0;
    w.enter_now = key == NT_KEY_ENTER;
    w.wheel_now = 0.0F;
    if (chance(20U)) {
        w.wheel_now = chance(50U) ? 1.0F : -1.0F;
    }
    logf_frame("%s%s%s%s", (press >= 0) ? " PRESS" : "", (release >= 0) ? " RELEASE" : "", key_name(key), (w.wheel_now != 0.0F) ? " WHEEL" : "");
    if (key != NT_KEY_COUNT) {
        nt_input_feed(nt_test_key(key, true));
    }
    sc_step_wheel(&w.sc, x, y, press, release, w.wheel_now);
    if (key != NT_KEY_COUNT) {
        nt_input_feed(nt_test_key(key, false));
    }
    snapshot_end();
    check_wheel();
    check_modal_from();
    (void)observe();
#if UI_INV_TRACE
    printf("s%u f%03d %s\n", (unsigned)w.seed, w.frame, w.log[w.frame]);
#endif
}

static void run_seed(uint32_t seed) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    ui_walker_fixture_fill_stub_font(&s_fx);
    w.ctx = s_fx.ctx;
    w.seed = seed;
    w.rng = (seed * 2654435761U) | 1U;
    w.menu = (nt_ui_menu_t){0};
    w.menu_style = nt_ui_menu_style_defaults();
    w.dd_style = nt_ui_dropdown_style_defaults();
    w.dd_style.max_visible_rows = 3U;
    w.dd_style.open_ease_speed = 14.0F; /* the list fades, so a closing list still declares its scroll */
    w.dd_style.list_scroll.wheel_ease_speed = 0.0F;
    w.combo_sel = -1;
    w.drag_active = false;
    w.born_orphan = 0U;
    w.sub_id = 0U;
    for (int ov = 0; ov < OV_N; ++ov) {
        w.ov_index[ov] = -1;
        w.pending[ov] = false;
    }
    w.chain_n = 0;
    w.begin_count = 0U;
    w.press_allowed = 0xFFFFFFFFU;
    w.open_end_n = 0U;
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
        printf("inv %-28s %s (%d failing checks)\n", s_inv_name[inv], (w.fail_count[inv] == 0) ? "PASS" : "FAIL", w.fail_count[inv]);
        if (w.fail_count[inv] != 0 && used < sizeof summary) {
            const int n = snprintf(summary + used, sizeof summary - used, "%s; ", w.fail_msg[inv]);
            used += (n > 0) ? (size_t)n : 0U;
        }
    }
    printf("note: %d frames with a catcher record of an overlay closed or reopened before nt_ui_begin%s%s\n", w.note_count, (w.note_count != 0) ? "; first: " : "", w.note_msg);
    printf("coverage: %d frames with a top, %d paint pairs, %d OPENING and %d CLOSING declarations, %d releases over a deaf body button; clicks", w.n_top_frames, w.n_paint_pairs, w.n_opening,
           w.n_closing, w.n_deaf_releases);
    for (int ov = 0; ov <= OV_N; ++ov) {
        printf(" %s %d", s_ov_name[ov], w.n_clicks[ov]);
    }
    printf("\n");
    printf("coverage: game drag over the UI %d frames (%d releases), %d children of a left-out parent (%d opened in a closing body), %d wheels on a deaf list (%d CLOSING), %d on an open one "
           "moved it, modal opened from LIST %d "
           "MENU %d SUB %d\n",
           w.n_game_drag_over_ui, w.n_game_release_over_ui, w.n_orphans, w.n_born_orphans, w.n_deaf_wheels, w.n_closing_wheels, w.n_open_wheel_moves, w.n_modal_from[OV_LIST], w.n_modal_from[OV_MENU],
           w.n_modal_from[OV_SUB]);
    const bool ran = w.n_game_release_over_ui > 0 && w.n_orphans > 0 && w.n_closing_wheels > 0 && w.n_open_wheel_moves > 0 && w.n_modal_from[OV_LIST] > 0 && w.n_modal_from[OV_MENU] > 0 &&
                     w.n_modal_from[OV_SUB] > 0;
    if (used != 0U) {
        TEST_FAIL_MESSAGE(summary);
    }
    TEST_ASSERT_TRUE_MESSAGE(ran, "an invariant that never ran proves nothing: a coverage count above is 0");
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_overlay_invariants_hold_over_random_frames);
    return UNITY_END();
}
