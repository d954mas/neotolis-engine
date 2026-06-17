#include "ui/nt_ui_menu.h"

#include <math.h>

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_MENU_DEF = {
    .name = "nt_menu",
    .pill_color = 0xFF40C080U,
    ._reserved = 0U,
};

/* Depth-salted hover-intent cell id: each open submenu level gets a distinct state cell so level N's
 * prev-mouse / switch-timer never aliases level N+1's (T-65-13). */
#define NT_UI_MENU_HOVER_SALT 0x4E550000U
static inline uint32_t menu_hover_id(uint32_t menu_id, uint8_t depth) {
    /* fold depth into the salt's low byte so distinct levels of one menu never collide */
    const uint32_t salt = NT_UI_MENU_HOVER_SALT | (uint32_t)depth;
    return nt_ui_derived_id(menu_id, salt);
}

/* Per-open-submenu hover-intent retained state. apex is the STABLE triangle apex (the cursor pos when
 * the corridor was first primed, i.e. where the user left the parent row) — NOT the per-frame previous
 * cursor. A per-frame apex sits ~1px from the cursor, making the triangle a huge near-degenerate wedge
 * that contains almost the whole half-plane along the submenu edge: any vertical travel toward a sibling
 * still reads "aiming", so the corridor never released and the user was trapped (Symptom A/B). A fixed
 * apex makes the corridor a real narrow wedge that the cursor leaves when it moves sibling-ward.
 * switch_timer accumulates dwell OFF the corridor and crosses AIM_FALLBACK to allow a sibling switch. */
typedef struct {
    float apex_x, apex_y;
    float switch_timer;
    bool primed; /* false on a fresh cell -> apex not yet meaningful (latch it this frame, keep open) */
    uint8_t _pad[3];
} nt_ui_menu_hover_t;

/* Per-menu retained runtime cell (keyed by the menu id). open_path[d] is the index of the item whose
 * submenu is open at level d (-1 = none open at that level); focus[d] is the keyboard-focused item at
 * level d. active_depth is the deepest currently-open level (0 = only the root). The recursion reads/
 * writes this so the open chain survives across frames without the game tracking per-level state. */
typedef struct {
    int16_t open_path[NT_UI_MENU_MAX_DEPTH];
    int16_t focus[NT_UI_MENU_MAX_DEPTH];
    uint8_t active_depth;
    bool primed; /* a zeroed fresh cell would read open_path[0]==0 (= item 0 open); init to -1 once */
    uint8_t _pad[2];
} nt_ui_menu_runtime_t;
_Static_assert(sizeof(nt_ui_menu_runtime_t) <= NT_UI_STATE_PAYLOAD_MAX, "menu runtime cell must fit the state pool payload");

#define NT_UI_MENU_RUNTIME_SALT 0x4D52E700U /* 'MR' runtime cell */
#define NT_UI_MENU_TAG NT_UI_STATE_TAG('m', 'e', 'n', 'u')

/* Kind tags folded into the id hash so a level/panel/row/catcher at one (depth,index) never collides
 * with another. nt_ui_derived_id (XOR) cannot be used to compose depth+index because additive salts
 * with a shared stride XOR-cancel across (depth,index) pairs (e.g. (d=1,i=2) aliased (d=2,i=1)). */
enum { NT_UI_MENU_KIND_LEVEL = 1, NT_UI_MENU_KIND_PANEL = 2, NT_UI_MENU_KIND_ROW = 3, NT_UI_MENU_KIND_RUNTIME = 4 };

/* fmix-style 32-bit hash combining menu_id + kind + depth + index. Non-XOR-symmetric, so distinct
 * (kind,depth,index) tuples never alias. Folds 0 to 1 (id 0 is the empty-slot sentinel). */
static inline uint32_t menu_hash_id(uint32_t menu_id, uint32_t kind, uint32_t depth, uint32_t idx) {
    uint32_t h = menu_id * 0x9E3779B1U;
    h = (h ^ ((kind + 1U) * 0x85EBCA6BU));
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = (h ^ ((depth + 1U) * 0x27D4EB2FU));
    h = (h ^ (h >> 15)) * 0x165667B1U;
    h = (h ^ ((idx + 1U) * 0x9E3779B1U));
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}

static inline uint32_t menu_runtime_id(uint32_t menu_id) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_RUNTIME, 0U, 0U); }
static inline uint32_t menu_level_id(uint32_t menu_id, uint8_t depth) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_LEVEL, depth, 0U); }
static inline uint32_t menu_panel_id(uint32_t menu_id, uint8_t depth) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_PANEL, depth, 0U); }
static inline uint32_t menu_row_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_ROW, depth, item_idx); }

// #region pure hover-intent algorithm (no GL, unit-tested)
/* Barycentric sign test: a point is inside the triangle iff all three edge cross-products share a sign
 * (or are zero). CITED RESEARCH §submenu. */
static bool menu_point_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    const float d1 = ((px - bx) * (ay - by)) - ((ax - bx) * (py - by));
    const float d2 = ((px - cx) * (by - cy)) - ((bx - cx) * (py - cy));
    const float d3 = ((px - ax) * (cy - ay)) - ((cx - ax) * (py - ay));
    const bool neg = (d1 < 0.0F) || (d2 < 0.0F) || (d3 < 0.0F);
    const bool pos = (d1 > 0.0F) || (d2 > 0.0F) || (d3 > 0.0F);
    return !(neg && pos);
}

/* Aim-triangle near corners for an open submenu rect. A submenu opens to the RIGHT of its parent, so
 * the two corners the cursor aims between are the submenu's LEFT edge (top + bottom). When edge-flipped
 * to the LEFT, mirror to the submenu's RIGHT edge. The triangle apex is the cursor's PREVIOUS position
 * (supplied by the caller); these are the base. */
static void menu_aim_corners(float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float *bx, float *by, float *cx, float *cy) {
    const float near_x = (side == NT_UI_POPUP_LEFT) ? (sub_x + sub_w) : sub_x;
    *bx = near_x;
    *by = sub_y;
    *cx = near_x;
    *cy = sub_y + sub_h;
}

/* Per-frame decision: while the cursor sits inside {apex, near_top, near_bottom} the user is aiming at
 * the open submenu -> KEEP it open + reset the dwell timer. Otherwise accumulate dwell; once it passes
 * AIM_FALLBACK allow the hovered sibling to win. A fresh (unprimed) cell latches the apex at the current
 * cursor and keeps this frame so the very first frame after opening never instantly switches. */
static bool menu_hover_intent(nt_ui_menu_hover_t *c, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float dt) {
    if (!c->primed) {
        /* Latch the corridor apex once: the cursor pos when the submenu was first armed (where the user
         * left the parent row). It is the FIXED triangle tip for the whole corridor lifetime. */
        c->apex_x = mouse_x;
        c->apex_y = mouse_y;
        c->switch_timer = 0.0F;
        c->primed = true;
        return true;
    }
    float bx = 0.0F;
    float by = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    menu_aim_corners(sub_x, sub_y, sub_w, sub_h, side, &bx, &by, &cx, &cy);
    /* Keep the submenu open if the cursor is parked OVER it (the common stationary case), else if it is
     * inside the STABLE {apex, near corners} corridor (genuinely traveling toward the open child —
     * Pitfall 3). Off both, the dwell timer races AIM_FALLBACK so a quick sibling pass-over has a short
     * grace before the switch. The corridor itself is HARD-capped at AIM_FALLBACK*2 of wall-clock since
     * priming so a cursor that parks inside the wedge still releases (spec: never trap the user). */
    const bool over_sub = (mouse_x >= sub_x) && (mouse_x <= sub_x + sub_w) && (mouse_y >= sub_y) && (mouse_y <= sub_y + sub_h);
    const bool in_corridor = menu_point_in_tri(mouse_x, mouse_y, c->apex_x, c->apex_y, bx, by, cx, cy);
    bool keep = true;
    if (over_sub) {
        c->switch_timer = 0.0F; /* parked over the child: always keep, dwell grace resets */
    } else if (in_corridor && (c->switch_timer < NT_UI_MENU_AIM_FALLBACK_SECS * 2.0F)) {
        c->switch_timer += dt; /* aiming through the wedge: keep, but the corridor still ages (hard cap) */
        keep = true;
    } else {
        c->switch_timer += dt;
        keep = (c->switch_timer < NT_UI_MENU_AIM_FALLBACK_SECS);
    }
    return keep;
}
// #endregion

nt_ui_menu_style_t nt_ui_menu_style_defaults(void) {
    return (nt_ui_menu_style_t){
        .bg_color = 0xF02A2A2AU,
        .item_hover_color = 0xFF4A6A8AU,
        .text_color = 0xFFEEEEEEU,
        .text_disabled = 0xFF888888U,
        .font_size = 16.0F,
        .item_height = 26U,
        .min_width = 160U,
        .pad = 6U,
        .font_id = 0U,
        .layer = 0U,
    };
}

/* Validate one item (fail-early, T-65-11): a submenu ptr and its count must agree, and a parent item
 * (one with a submenu) must carry a label. */
static void menu_assert_item(const nt_ui_menu_item_t *it) {
    NT_ASSERT((it->submenu == NULL) == (it->submenu_count == 0U) && "menu item: submenu ptr and count must agree");
    NT_ASSERT((it->submenu == NULL || it->label != NULL) && "menu item: a parent item must have a label");
}

/* Validate one level's item array. */
static void menu_assert_items(const nt_ui_menu_item_t *items, uint32_t count) {
    NT_ASSERT(items != NULL || count == 0U);
    for (uint32_t i = 0; i < count; ++i) {
        menu_assert_item(&items[i]);
    }
}

/* Reset the open chain + focus to "nothing open". A fresh (zeroed) cell would otherwise read
 * open_path[0]==0 as "item 0's submenu is open", auto-flying-out the first parent. */
static void menu_runtime_reset(nt_ui_menu_runtime_t *rt) {
    for (uint32_t d = 0; d < NT_UI_MENU_MAX_DEPTH; ++d) {
        rt->open_path[d] = -1;
        rt->focus[d] = -1;
    }
    rt->active_depth = 0U;
    rt->primed = true;
}

/* Get-or-create the runtime cell, resetting it on first creation. */
static nt_ui_menu_runtime_t *menu_runtime(nt_ui_context_t *ctx, uint32_t menu_id) {
    nt_ui_menu_runtime_t *rt = nt_ui_state(ctx, menu_runtime_id(menu_id), sizeof *rt, NT_UI_MENU_TAG);
    if (!rt->primed) {
        menu_runtime_reset(rt);
    }
    return rt;
}

/* Current pointer pos (UI space) from the active frame snapshot — the menu reads the cursor directly
 * for the open-point anchor and the hover-intent apex. */
static void menu_mouse_pos(const nt_ui_context_t *ctx, float *mx, float *my) {
    if (ctx->frame_pointer_count > 0U) {
        *mx = ctx->frame_pointers[0].x;
        *my = ctx->frame_pointers[0].y;
    } else {
        *mx = 0.0F;
        *my = 0.0F;
    }
}

bool nt_ui_menu_open_trigger(nt_ui_context_t *ctx, uint32_t id, nt_ui_menu_state_t *st, float long_press_secs) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu_open_trigger: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_open_trigger: call between nt_ui_begin/end");
    NT_ASSERT(id != 0U && st != NULL && "nt_ui_menu_open_trigger: id non-zero, st non-NULL");

    /* Right-click is the desktop trigger; a long-press (events gesture) is the touch trigger. Both arm
     * the root at the current cursor and reset any stale open chain. */
    bool armed = nt_input_mouse_is_pressed(NT_BUTTON_RIGHT);
    if (!armed && long_press_secs > 0.0F) {
        const nt_ui_events_cfg_t cfg = {.long_press_secs = long_press_secs, .double_click = false};
        const nt_ui_events_t ev = nt_ui_events(ctx, id, &cfg);
        armed = ev.long_pressed;
    }
    if (armed) {
        float mx = 0.0F;
        float my = 0.0F;
        menu_mouse_pos(ctx, &mx, &my);
        st->anchor_x = mx;
        st->anchor_y = my;
        st->open = true;
        st->chosen_id = 0U;
        menu_runtime_reset(menu_runtime(ctx, id));
    }
    return armed;
}

// #region menu UI declaration (recursive popup-core fly-outs)
/* Declared-but-not-defined yet: the recursion is mutual (a level declares its open child level). */
static void menu_declare_level(nt_ui_context_t *ctx, uint32_t menu_id, const nt_ui_menu_item_t *items, uint32_t count, uint8_t depth, uint8_t nav_depth, const nt_ui_popup_anchor_t *anchor,
                               nt_ui_menu_runtime_t *rt, const nt_ui_menu_style_t *style, float mx, float my, float dt, uint32_t *out_chosen, bool *out_close_chain);

/* One item row: a fixed-height rect with a label, plus a ">" affordance for a parent. Returns the
 * interaction so the caller drives hover/click. The row id is registered so its bbox is queryable. */
static nt_ui_interaction_t menu_declare_row(nt_ui_context_t *ctx, uint32_t row_id, const nt_ui_menu_item_t *it, bool focused, const nt_ui_menu_style_t *style) {
    const bool is_parent = it->submenu != NULL;
    const nt_ui_interaction_t in = it->enabled ? nt_ui_query_interaction(ctx, row_id) : (nt_ui_interaction_t){0};
    const bool highlit = it->enabled && (in.hovered || focused);
    const uint32_t row_bg = highlit ? style->item_hover_color : 0U;
    Clay_Color bg = (row_bg != 0U) ? nt_ui_unpack_abgr(row_bg) : (Clay_Color){0};
    const uint32_t txt = it->enabled ? style->text_color : style->text_disabled;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(txt)};
    CLAY({
        .id = (Clay_ElementId){.id = row_id},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->item_height)},
                   .padding = {.left = style->pad, .right = style->pad},
                   .childGap = style->pad,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = bg,
        .userData = (void *)nt_ui_make_element_data(style->layer, NULL),
    }) {
        nt_ui_label(ctx, nt_ui_make_element_data(style->layer, NULL), it->label != NULL ? it->label : "", &lbl);
        if (is_parent) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
            nt_ui_label(ctx, nt_ui_make_element_data(style->layer, NULL), ">", &lbl);
        }
    }
    /* Register the row's interaction (step) so capture/hover edges advance once per frame. */
    if (it->enabled) {
        (void)nt_ui_step_interaction(ctx, row_id);
    }
    return in;
}

/* Resolve the anchor rect for a parent item's submenu = the item row's bbox; the submenu defaults to
 * RIGHT of the row and edge-flips per level via popup-core. found==false (first frame) falls back to the
 * supplied row screen estimate. */
static nt_ui_popup_anchor_t menu_submenu_anchor(const nt_ui_context_t *ctx, uint32_t row_id) {
    nt_ui_popup_anchor_t a = {.prefer_side = NT_UI_POPUP_RIGHT};
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, row_id);
    if (bb.found) {
        a.x = bb.x;
        a.y = bb.y;
        a.w = bb.width;
        a.h = bb.height;
    }
    return a;
}

/* Keyboard-nav for the deepest open level (Open Q3): Up/Down move focus within the level; Right opens
 * the focused parent's submenu (no-op on a leaf); Enter activates a focused leaf OR opens a focused
 * parent; Left/Esc close the level. Mutates rt->focus/open_path. Returns true if the level should
 * close (Left/Esc). */
static bool menu_keyboard_nav(const nt_ui_menu_item_t *items, uint32_t count, uint8_t depth, nt_ui_menu_runtime_t *rt, uint32_t *out_chosen) {
    int16_t f = rt->focus[depth];
    if (nt_input_key_is_pressed(NT_KEY_ARROW_DOWN)) {
        f = (int16_t)((f + 1) % (int)count);
    } else if (nt_input_key_is_pressed(NT_KEY_ARROW_UP)) {
        f = (int16_t)((f <= 0) ? (int)count - 1 : f - 1);
    }
    rt->focus[depth] = f;
    if (f >= 0 && (uint32_t)f < count) {
        const nt_ui_menu_item_t *it = &items[f];
        const bool open_key = nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) || nt_input_key_is_pressed(NT_KEY_ENTER);
        const bool activate_key = nt_input_key_is_pressed(NT_KEY_ENTER);
        if (it->enabled) {
            if (it->submenu != NULL && open_key) {
                rt->open_path[depth] = f;
                if (depth + 1U > rt->active_depth) {
                    rt->active_depth = (uint8_t)(depth + 1U);
                }
                rt->focus[depth + 1U] = 0;
            } else if (it->submenu == NULL && activate_key) {
                *out_chosen = it->id; /* Right on a leaf is a no-op; only Enter activates */
            }
        }
    }
    return nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) || nt_input_key_is_pressed(NT_KEY_ESCAPE);
}

/* Declare the panel + item rows. Updates focus on hover, latches a leaf click into *out_chosen and a
 * parent click into the returned open_idx, and reports a hovered "switch request" (parent to open OR a
 * leaf when a sibling submenu is open) in *out_hovered. Returns the open child index after clicks. */
static int16_t menu_declare_panel(nt_ui_context_t *ctx, uint32_t menu_id, const nt_ui_menu_item_t *items, uint32_t count, uint8_t depth, nt_ui_menu_runtime_t *rt, const nt_ui_menu_style_t *style,
                                  int16_t open_idx, int16_t *out_hovered, uint32_t *out_chosen) {
    int16_t hovered = -1;
    CLAY({.id = (Clay_ElementId){.id = menu_panel_id(menu_id, depth)},
          .layout = {.sizing = {.width = CLAY_SIZING_FIT(.min = (float)style->min_width), .height = CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(style->pad),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 2},
          .backgroundColor = nt_ui_unpack_abgr(style->bg_color),
          .userData = (void *)nt_ui_make_element_data(style->layer, NULL)}) {
        for (uint32_t i = 0; i < count; ++i) {
            const nt_ui_menu_item_t *it = &items[i];
            const bool focused = (rt->focus[depth] == (int16_t)i);
            const nt_ui_interaction_t in = menu_declare_row(ctx, menu_row_id(menu_id, depth, i), it, focused, style);
            if (!it->enabled) {
                continue;
            }
            if (in.hovered) {
                rt->focus[depth] = (int16_t)i;              /* hover moves keyboard focus too */
                if (it->submenu != NULL || open_idx >= 0) { /* parent open OR leaf-closes-sibling */
                    hovered = (int16_t)i;
                }
            }
            if (in.clicked) {
                if (it->submenu != NULL) {
                    open_idx = (int16_t)i;
                } else {
                    *out_chosen = it->id; /* Model D: latch the activated leaf id */
                }
            }
        }
    }
    *out_hovered = hovered;
    return open_idx;
}

/* Aim-gated mouse switch: a sibling switch request only replaces the open child once the cursor is no
 * longer aiming at (nor parked over) the currently-open child (Pitfall 3). Returns the new open child. */
static int16_t menu_resolve_hover_switch(nt_ui_context_t *ctx, uint32_t menu_id, const nt_ui_menu_item_t *items, uint32_t count, uint8_t depth, int16_t open_idx, int16_t hovered, float mx, float my,
                                         uint8_t cur_side, float dt) {
    if (hovered < 0 || hovered == open_idx) {
        return open_idx;
    }
    bool may_switch = true;
    if (open_idx >= 0 && (uint32_t)open_idx < count && items[open_idx].submenu != NULL) {
        const nt_ui_bbox_t cbb = nt_ui_get_bbox(ctx, menu_level_id(menu_id, (uint8_t)(depth + 1U)));
        if (cbb.found) {
            nt_ui_menu_hover_t *hc = nt_ui_state(ctx, menu_hover_id(menu_id, (uint8_t)(depth + 1U)), sizeof *hc, NT_UI_MENU_TAG);
            may_switch = !menu_hover_intent(hc, mx, my, cbb.x, cbb.y, cbb.width, cbb.height, cur_side, dt);
        }
    }
    if (!may_switch) {
        return open_idx;
    }
    if (items[hovered].submenu != NULL) {
        return hovered;
    }
    return -1; /* leaf hover collapses the open child */
}

/* Commit the resolved open child into the runtime cell + maintain active_depth, then run keyboard-nav
 * for the deepest-at-frame-start level (which may open/close a level or activate a leaf). Returns the
 * final open child index after nav. */
static int16_t menu_commit_and_nav(const nt_ui_menu_item_t *items, uint32_t count, uint8_t depth, uint8_t nav_depth, nt_ui_menu_runtime_t *rt, int16_t open_idx, uint32_t *out_chosen,
                                   bool *out_close_chain) {
    rt->open_path[depth] = open_idx;
    if (open_idx >= 0) {
        if (depth + 1U > rt->active_depth) {
            rt->active_depth = (uint8_t)(depth + 1U);
        }
    } else if (rt->active_depth > depth) {
        rt->active_depth = depth; /* this level no longer has an open child */
    }
    if (depth != nav_depth) {
        return open_idx;
    }
    if (menu_keyboard_nav(items, count, depth, rt, out_chosen)) {
        if (depth == 0U) {
            *out_close_chain = true;
        } else {
            rt->open_path[depth - 1U] = -1; /* collapse this level back into the parent */
            rt->active_depth = (uint8_t)(depth - 1U);
        }
    }
    return rt->open_path[depth];
}

// NOLINTNEXTLINE(misc-no-recursion): bounded recursion — depth capped by NT_UI_MENU_MAX_DEPTH (asserted before each push, T-65-10)
static void menu_declare_level(nt_ui_context_t *ctx, uint32_t menu_id, const nt_ui_menu_item_t *items, uint32_t count, uint8_t depth, uint8_t nav_depth, const nt_ui_popup_anchor_t *anchor,
                               nt_ui_menu_runtime_t *rt, const nt_ui_menu_style_t *style, float mx, float my, float dt, uint32_t *out_chosen, bool *out_close_chain) {
    NT_ASSERT(depth < NT_UI_MENU_MAX_DEPTH && "nt_ui_menu: submenu nesting exceeds NT_UI_MENU_MAX_DEPTH");
    menu_assert_items(items, count);

    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = 0.0F; /* menus snap; the open delay handles flicker, not a tween */
    pst.layer = style->layer;
    /* Clear light-dismiss so popup-core emits NO catcher per level: a submenu's full-viewport catcher
     * sits at a HIGHER z than ancestor panels (catcher_z(d+1) > panel_z(d)) and would occlude them,
     * trapping the user in the deepest level (hover/click never reach ancestors). The menu owns its own
     * outside-click dismiss in nt_ui_menu against all open panels instead. */
    pst.flags &= (uint8_t)~NT_UI_POPUP_LIGHT_DISMISS;
    const nt_ui_popup_result_t r = nt_ui_popup_begin(ctx, menu_level_id(menu_id, depth), &pst, anchor, true);

    /* The currently-open child (authoritative across frames); mouse hover may switch it, gated below. */
    int16_t hovered = -1;
    int16_t open_idx = menu_declare_panel(ctx, menu_id, items, count, depth, rt, style, rt->open_path[depth], &hovered, out_chosen);
    open_idx = menu_resolve_hover_switch(ctx, menu_id, items, count, depth, open_idx, hovered, mx, my, r.side, dt);
    open_idx = menu_commit_and_nav(items, count, depth, nav_depth, rt, open_idx, out_chosen, out_close_chain);

    /* Recurse into the open submenu (if any). The open_path is authoritative — keyboard-opened menus
     * stay open regardless of cursor position; the hover-intent above only governs MOUSE sibling
     * switching, never a blanket collapse (a menu closes on click-away / Esc / sibling-open). */
    if (open_idx >= 0 && (uint32_t)open_idx < count && items[open_idx].submenu != NULL) {
        /* Assert BEFORE the push — a parent opening past the cap is a runaway tree (T-65-10), fail-early
         * with no fallback rather than silently truncating the chain. */
        NT_ASSERT((depth + 1U) < NT_UI_MENU_MAX_DEPTH && "nt_ui_menu: submenu nesting exceeds NT_UI_MENU_MAX_DEPTH");
        const nt_ui_popup_anchor_t sub_anchor = menu_submenu_anchor(ctx, menu_row_id(menu_id, depth, (uint32_t)open_idx));
        menu_declare_level(ctx, menu_id, items[open_idx].submenu, items[open_idx].submenu_count, (uint8_t)(depth + 1U), nav_depth, &sub_anchor, rt, style, mx, my, dt, out_chosen, out_close_chain);
    }

    nt_ui_popup_end(ctx);
}
// #endregion

/* True when the cursor is inside ANY open level's panel rect (depths 0..active_depth). A click inside
 * a panel is a row interaction (select/open/switch) and must NOT dismiss; a click outside all of them
 * is an outside-click. Uses the prev-frame bbox (1-frame IM lag); a not-yet-found panel is skipped. */
static bool menu_cursor_over_any_panel(const nt_ui_context_t *ctx, uint32_t menu_id, const nt_ui_menu_runtime_t *rt, float mx, float my) {
    for (uint8_t d = 0; d <= rt->active_depth && d < NT_UI_MENU_MAX_DEPTH; ++d) {
        const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, menu_panel_id(menu_id, d));
        if (bb.found && mx >= bb.x && mx <= bb.x + bb.width && my >= bb.y && my <= bb.y + bb.height) {
            return true;
        }
    }
    return false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the leading validation assert chain inflates the count; control flow is flat (same pattern as nt_ui_modal_begin)
void nt_ui_menu(nt_ui_context_t *ctx, uint32_t id, const nt_ui_menu_item_t *items, uint32_t count, nt_ui_menu_state_t *st, const nt_ui_menu_style_t *style) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu: call between nt_ui_begin/end");
    NT_ASSERT(id != 0U && st != NULL && style != NULL && "nt_ui_menu: id non-zero, st + style non-NULL");
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_menu: style->font_size must be > 0");
    menu_assert_items(items, count);

    if (!st->open) {
        return; /* present-only: a closed menu declares nothing so base UI stays clickable (T-65-13) */
    }

    nt_ui_menu_runtime_t *rt = menu_runtime(ctx, id);
    float mx = 0.0F;
    float my = 0.0F;
    menu_mouse_pos(ctx, &mx, &my);

    const nt_ui_popup_anchor_t root_anchor = {.x = st->anchor_x, .y = st->anchor_y, .w = 0.0F, .h = 0.0F, .prefer_side = NT_UI_POPUP_BELOW};
    uint32_t chosen = 0U;
    bool close_chain = false;
    menu_declare_level(ctx, id, items, count, 0U, rt->active_depth, &root_anchor, rt, style, mx, my, ctx->frame_dt, &chosen, &close_chain);

    /* Menu-owned outside-click dismiss (replaces the removed per-level catcher): a primary press this
     * frame OUTSIDE every open panel closes the whole chain. A press inside a panel is a row interaction
     * (handled by the rows), never a dismiss. */
    if (nt_input_mouse_is_pressed(NT_BUTTON_LEFT) && !menu_cursor_over_any_panel(ctx, id, rt, mx, my)) {
        close_chain = true;
    }

    if (chosen != 0U) {
        st->chosen_id = chosen; /* Model D: latch the activated leaf id; the game reads + clears it */
        close_chain = true;
    }
    if (close_chain) {
        st->open = false;
        menu_runtime_reset(rt);
    }
}

#ifdef NT_TEST_ACCESS
bool nt_ui_menu_test_point_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) { return menu_point_in_tri(px, py, ax, ay, bx, by, cx, cy); }

void nt_ui_menu_test_aim_corners(float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float *bx, float *by, float *cx, float *cy) {
    menu_aim_corners(sub_x, sub_y, sub_w, sub_h, side, bx, by, cx, cy);
}

bool nt_ui_menu_test_hover_intent(nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float dt) {
    NT_ASSERT(ctx != NULL && menu_id != 0U);
    nt_ui_menu_hover_t *c = nt_ui_state(ctx, menu_hover_id(menu_id, depth), sizeof *c, NT_UI_STATE_TAG('m', 'e', 'n', 'u'));
    return menu_hover_intent(c, mouse_x, mouse_y, sub_x, sub_y, sub_w, sub_h, side, dt);
}

float nt_ui_menu_test_switch_timer(const nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth) {
    NT_ASSERT(ctx != NULL && menu_id != 0U);
    const nt_ui_menu_hover_t *c = nt_ui_state_find((nt_ui_context_t *)ctx, menu_hover_id(menu_id, depth));
    return (c != NULL) ? c->switch_timer : 0.0F;
}

uint32_t nt_ui_menu_test_panel_id(uint32_t menu_id, uint8_t depth) { return menu_panel_id(menu_id, depth); }
uint32_t nt_ui_menu_test_row_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_row_id(menu_id, depth, item_idx); }
#endif
