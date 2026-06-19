#include "ui/nt_ui_menu.h"

#include <math.h>
#include <string.h>

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
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

/* Mouse-aim hover-intent corridor: drives hover-open + corridor-hold + leave-close in the immediate
 * fly-out (nt_ui_menu_submenu_begin records the hover; menu_end resolves it through this corridor). */
/* Depth-salted hover-intent cell id: each open submenu level gets a distinct state cell so level N's
 * prev-mouse / switch-timer never aliases level N+1's. */
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
 * still reads "aiming", so the corridor never releases and the user is trapped. A fixed apex makes the
 * corridor a real narrow wedge that the cursor leaves when it moves sibling-ward.
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
    uint32_t opened_frame;  /* ctx->frame_counter at arm; the right-click dismiss skips this frame so the opening click never self-closes */
    uint32_t kbd_activated; /* row id a keyboard Enter activated in the PREVIOUS frame's menu_end; the NEXT frame's row call fires its inline return for it, then menu_end clears it (1-frame latency,
                               fires once) */
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
enum {
    NT_UI_MENU_KIND_LEVEL = 1,
    NT_UI_MENU_KIND_PANEL = 2,
    NT_UI_MENU_KIND_ROW = 3,
    NT_UI_MENU_KIND_RUNTIME = 4,
    NT_UI_MENU_KIND_OCCLUDER = 5,
    NT_UI_MENU_KIND_ARROW = 6,
    NT_UI_MENU_KIND_ICON = 7,
    NT_UI_MENU_KIND_SHORTCUT = 8,
    NT_UI_MENU_KIND_CHECK = 9,
};

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

/* Scope-stack item id: fmix-fold scope_id + key ONLY (NOT XOR, NOT base+index — same reason as the warning
 * above). The id is POSITION-STABLE: a conditional sibling appearing/disappearing must NOT shift a later
 * row's id (that would reset its anim/Clay identity AND re-scope its submenu). The running index drives
 * layout order / focus / the frame record, but NEVER the identity. submenu_begin pushes THIS id as the
 * child scope, so a key need only be unique among siblings (asserted in DEBUG). Folds 0 to 1. */
static inline uint32_t menu_item_id(uint32_t scope_id, uint32_t key) {
    uint32_t h = scope_id * 0x9E3779B1U;
    h = (h ^ ((key + 1U) * 0x85EBCA6BU));
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = (h ^ (h >> 15)) * 0x165667B1U;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}

static inline uint32_t menu_runtime_id(uint32_t menu_id) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_RUNTIME, 0U, 0U); }
static inline uint32_t menu_level_id(uint32_t menu_id, uint8_t depth) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_LEVEL, depth, 0U); }
static inline uint32_t menu_panel_id(uint32_t menu_id, uint8_t depth) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_PANEL, depth, 0U); }
static inline uint32_t menu_occluder_id(uint32_t menu_id) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_OCCLUDER, 0U, 0U); }
#ifdef NT_TEST_ACCESS
/* KIND_ROW id reproduced for the nt_ui_menu_test_row_id probe (test-only). */
static inline uint32_t menu_row_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_ROW, depth, item_idx); }
#endif
/* Per-row sub-element ids (arrow marker / icon cell): fmix-derived so consecutive rows' children never
 * collide (Clay anonymous-child ids are additive; explicit fmix ids dodge the DUPLICATE_ID crash). */
static inline uint32_t menu_arrow_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_ARROW, depth, item_idx); }
static inline uint32_t menu_icon_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_ICON, depth, item_idx); }
static inline uint32_t menu_shortcut_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_SHORTCUT, depth, item_idx); }
static inline uint32_t menu_check_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_CHECK, depth, item_idx); }

// #region pure hover-intent algorithm (no GL, unit-tested; drives the immediate fly-out corridor)
/* Barycentric sign test: a point is inside the triangle iff all three edge cross-products share a sign
 * (or are zero). */
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
     * inside the STABLE {apex, near corners} corridor (genuinely traveling toward the open child). Off
     * both, the dwell timer races AIM_FALLBACK so a quick sibling pass-over has a short
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
    /* Polished flat baseline (no atlas art: panel_bg/arrow refs stay 0 -> flat bg + ">" text marker),
     * but wire-ready — set panel_bg/arrow and the icon gutter opens. */
    return (nt_ui_menu_style_t){
        .bg_color = 0xF02A2A2AU,
        .item_hover_color = 0xFF4A6A8AU,
        .text_color = 0xFFEEEEEEU,
        .text_disabled = 0xFF888888U,
        .shortcut_text = 0xFF999999U, /* muted vs text_color so the shortcut column reads as secondary */
        .panel_tint = 0xFFFFFFFFU,
        .arrow_tint = 0xFFC8C8C8U,
        .check_tint = 0xFFFFFFFFU, /* no tint; checkmark ref stays 0 -> "✓" text fallback */
        .separator_color = 0xFF505050U,
        .font_size = 16.0F,
        .slice9_scale = 1.0F,
        .state_speed = 16.0F,
        .open_ease_speed = 0.0F, /* snap-open by default; the open delay handles flicker, not a tween */
        .item_height = 26U,
        .min_width = 160U,
        .pad = 6U,
        .font_id = 0U,
        .icon_size = 0U,        /* no icon gutter by default (text-only) */
        .arrow_size = 14U,      /* used only when the arrow ref is set */
        .check_size = 14U,      /* used only when the checkmark ref is set (mirror arrow_size) */
        .separator_height = 2U, /* NULL-label divider thickness */
    };
}

void nt_ui_menu_init(nt_ui_menu_ctx_t *menu) {
    NT_ASSERT(menu != NULL && "nt_ui_menu_init: menu must be non-NULL");
    memset(menu, 0, sizeof *menu); /* zeroed scratch is correct at rest: only frame_record_count==0 matters */
}

/* Reset the open chain + focus to "nothing open". A fresh (zeroed) cell would otherwise read
 * open_path[0]==0 as "item 0's submenu is open", auto-flying-out the first parent. */
static void menu_runtime_reset(nt_ui_menu_runtime_t *rt) {
    for (uint32_t d = 0; d < NT_UI_MENU_MAX_DEPTH; ++d) {
        rt->open_path[d] = -1;
        rt->focus[d] = -1;
    }
    rt->active_depth = 0U;
    rt->opened_frame = 0U;
    rt->kbd_activated = 0U;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the leading validation assert chain inflates the count; control flow is flat (arm gate)
bool nt_ui_menu_open_trigger(nt_ui_context_t *ctx, uint32_t menu_id, uint32_t target_id, bool long_pressed, nt_ui_menu_state_t *st) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu_open_trigger: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_open_trigger: call between nt_ui_begin/end");
    NT_ASSERT(menu_id != 0U && st != NULL && "nt_ui_menu_open_trigger: menu_id non-zero, st non-NULL");

    /* Idempotent reads only — no mutating step here (nt_ui_events is the caller's one canonical step per
     * widget). Right-click is the desktop trigger: bound (target_id != 0) it arms over the target; target_id
     * == 0 arms anywhere. long_pressed is the touch trigger, supplied by the caller from its target widget's
     * own events gesture step. */
    bool right_armed = nt_input_mouse_is_pressed(NT_BUTTON_RIGHT);
    if (right_armed && target_id != 0U) {
        /* Geometry hit-test (not arbitrated hover) so a right-click RE-opens the menu through its own root
         * occluder: once open, the occluder sits above the target so the arbitrated hover is always false.
         * Trade-off: the trigger no longer respects front-most z-order (acceptable for a context-menu target). */
        const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, target_id);
        float mx = 0.0F;
        float my = 0.0F;
        menu_mouse_pos(ctx, &mx, &my);
        right_armed = bb.found && mx >= bb.x && mx <= bb.x + bb.width && my >= bb.y && my <= bb.y + bb.height;
    }
    const bool armed = right_armed || long_pressed;
    if (armed) {
        float mx = 0.0F;
        float my = 0.0F;
        menu_mouse_pos(ctx, &mx, &my);
        st->anchor_x = mx;
        st->anchor_y = my;
        st->open = true;
        nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu_id);
        menu_runtime_reset(rt);
        rt->opened_frame = ctx->frame_counter; /* skip the dismiss on the opening frame (right-click is also the open trigger) */
    }
    return armed;
}

// #region menu UI declaration (shared row/marker/panel helpers fed by the immediate path)
/* A NULL-label item renders as a thin, non-interactive divider (no row id, no hover/click/nav). A short
 * muted rect, indented by the panel pad so it reads as a group separator. */
static void menu_declare_separator(nt_ui_context_t *ctx, uint8_t fill_layer, const nt_ui_menu_style_t *style) {
    const float h = (style->separator_height > 0U) ? (float)style->separator_height : 1.0F;
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.left = style->pad, .right = style->pad}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h)}},
              .backgroundColor = nt_ui_unpack_abgr(style->separator_color),
              .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {}
    }
}

/* Draw the resolved per-item icon image filling the gutter cell (no-op if the ref is unset/unresolved).
 * Split out so the gutter cell stays a flat declaration (cognitive-complexity). */
static void menu_draw_icon_image(nt_ui_context_t *ctx, uint8_t fill_layer, const nt_atlas_region_ref_t *icon) {
    if (icon == NULL || icon->atlas.id == 0U) {
        return;
    }
    nt_atlas_region_ref_t ic = *icon; /* per-item ref (not style-owned): resolve by-value, never memoize */
    nt_atlas_resolve_ref(&ic);
    if (ic.region == NT_ATLAS_INVALID_REGION) {
        return;
    }
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_menu: scratch alloc failed (icon payload)");
    *p = (nt_ui_image_payload_t){.atlas = ic.atlas, .region_index = ic.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .image = (Clay_ImageElementConfig){.imageData = p}, .userData = NT_UI_CLAY_DATA(fill_layer)}) {}
}

/* The leading icon gutter (unified icon model): reserve icon_size px so labels stay aligned across iconed
 * and non-iconed rows; draw the per-item icon if set, else leave the gutter empty. icon_size==0 -> no
 * gutter. */
static void menu_declare_icon(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t icon_id, const nt_atlas_region_ref_t *icon, const nt_ui_menu_style_t *style) {
    if (style->icon_size == 0U) {
        return;
    }
    const float gut = (float)style->icon_size;
    CLAY({.id = (Clay_ElementId){.id = icon_id}, .layout = {.sizing = {CLAY_SIZING_FIXED(gut), CLAY_SIZING_FIXED(gut)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        menu_draw_icon_image(ctx, fill_layer, icon);
    }
}

/* The submenu marker on a parent row: the arrow sprite (tinted, right-aligned) when a ref is set, else
 * the ">" text fallback. The arrow cell carries an explicit fmix id so consecutive parent rows' markers
 * never collide (Clay anonymous-child ids are additive). */
static void menu_declare_marker(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t arrow_id, const nt_ui_label_style_t *lbl, nt_ui_menu_style_t *style) {
    nt_atlas_resolve_ref(&style->arrow);
    const bool has_art = (style->arrow.atlas.id != 0U && style->arrow.region != NT_ATLAS_INVALID_REGION);
    if (has_art) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_menu: scratch alloc failed (arrow payload)");
        *p = (nt_ui_image_payload_t){.atlas = style->arrow.atlas, .region_index = style->arrow.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
        const float sz = (style->arrow_size > 0U) ? (float)style->arrow_size : (float)style->item_height * 0.5F;
        CLAY({.id = (Clay_ElementId){.id = arrow_id},
              .layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}},
              .image = (Clay_ImageElementConfig){.imageData = p},
              .backgroundColor = nt_ui_unpack_tint(style->arrow_tint),
              .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {}
    } else {
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), ">", lbl);
    }
}

/* The selected checkmark on a row: the checkmark sprite (tinted) when a ref is set, else the "✓" text
 * fallback. Mirrors menu_declare_marker; the cell carries an explicit fmix id so sibling rows' checkmarks
 * never collide (Clay anonymous-child ids are additive). */
static void menu_declare_check(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t check_id, const nt_ui_label_style_t *lbl, nt_ui_menu_style_t *style) {
    nt_atlas_resolve_ref(&style->checkmark);
    const bool has_art = (style->checkmark.atlas.id != 0U && style->checkmark.region != NT_ATLAS_INVALID_REGION);
    if (has_art) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_menu: scratch alloc failed (checkmark payload)");
        *p = (nt_ui_image_payload_t){.atlas = style->checkmark.atlas, .region_index = style->checkmark.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
        const float sz = (style->check_size > 0U) ? (float)style->check_size : (float)style->item_height * 0.5F;
        CLAY({.id = (Clay_ElementId){.id = check_id},
              .layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}},
              .image = (Clay_ImageElementConfig){.imageData = p},
              .backgroundColor = nt_ui_unpack_tint(style->check_tint),
              .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {}
    } else {
        CLAY({.id = (Clay_ElementId){.id = check_id}, .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) {
            nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), "\xE2\x9C\x93", lbl);
        }
    }
}

/* The right-aligned shortcut text cell (e.g. "Ctrl+S") drawn in style->shortcut_text. The cell carries an
 * explicit fmix id so sibling rows' shortcut cells never collide. */
static void menu_declare_shortcut(nt_ui_context_t *ctx, uint8_t label_layer, uint32_t shortcut_id, const char *shortcut, nt_ui_menu_style_t *style) {
    const nt_ui_label_style_t sc = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->shortcut_text)};
    CLAY({.id = (Clay_ElementId){.id = shortcut_id}, .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) { nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), shortcut, &sc); }
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

/* ONE root-level full-viewport occluder UNDER the whole menu stack: sits just below the root panel
 * z-band (above base UI) so it absorbs the dismiss click (it wins topmost-z over base UI, the click
 * can't fall through) and block_pointers base UI while the menu is open. NOT a per-level catcher (those
 * sit ABOVE ancestor panels and trap the user); this single one is always under the stack, so every open
 * level's panel + rows stay hittable above it. */
static void menu_declare_occluder(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t menu_id) {
    const uint32_t occ_id = menu_occluder_id(menu_id);
    const int16_t occ_z = (int16_t)(ctx->modal_zband_stride - 1); /* just below the root panel band (stride*1) */
    const nt_ui_transform_t id_xf = nt_ui_transform_defaults();
    const nt_ui_element_data_t *occ_data = nt_ui_make_element_data_xform(fill_layer, NULL, &id_xf, 1.0F);
    CLAY({.id = (Clay_ElementId){.id = occ_id},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = occ_z},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
          .userData = (void *)occ_data}) {}
    /* Inert gate: wins next-frame topmost-z over base UI so the dismiss click is absorbed (never leaks
     * through) and base UI is non-interactive while the menu is open. The menu-owned outside-click
     * dismiss (global press + !over-any-panel) remains the dismiss source. */
    nt_ui_block_pointer(ctx, occ_id, NULL);
}

// #region immediate-mode menu (begin/item/submenu/separator/end)
/* The immediate API discovers the tree DURING the frame (begin/item/submenu_begin..end/menu_end) instead
 * of recursing over an items[] array. It FEEDS the KEPT runtime cell (open_path/focus/active_depth/
 * opened_frame), the single root occluder, the per-level popup, mouse-aim, and outside-click dismiss
 * per-call. Keyboard nav runs in menu_end against the PREVIOUS frame's per-level record (strictly
 * 1-frame latency, never same-frame). The scope stack derives each row id via
 * mix(scope_id[depth], key) (position-stable); submenu_begin pushes its row id as the child scope. */

/* Append a recorded row into THIS frame's per-level frame record (fail-early assert on overflow).
 * DEBUG-only: ids are now position-stable (mix(scope,key)), so two siblings sharing a key derive the SAME
 * id -> aliased anim/Clay/submenu identity. Assert sibling-key uniqueness (ImGui's duplicate-id contract);
 * the linear scan is O(items/level) over the small cap and compiles out with NT_ASSERT in OFF builds. */
static void menu_record_append(nt_ui_menu_ctx_t *menu, uint8_t depth, uint32_t id, uint16_t idx, bool enabled, bool has_sub) {
    NT_ASSERT(menu->frame_record_count[depth] < NT_UI_MENU_MAX_ITEMS_PER_LEVEL && "nt_ui_menu: per-level item count exceeds NT_UI_MENU_MAX_ITEMS_PER_LEVEL");
#if NT_ASSERT_MODE != NT_ASSERT_OFF
    for (uint16_t k = 0; k < menu->frame_record_count[depth]; ++k) {
        NT_ASSERT(menu->frame_record[depth][k].id != id && "nt_ui_menu: duplicate sibling key (item keys must be unique among siblings)");
    }
#endif
    const uint16_t n = menu->frame_record_count[depth]++;
    menu->frame_record[depth][n] = (nt_ui_menu_record_t){.id = id, .idx = idx, .enabled = enabled ? 1U : 0U, .has_sub = has_sub ? 1U : 0U};
}

/* Declare one immediate row (icon gutter + label + optional submenu marker) carrying the scope-stack id,
 * register + record it, move keyboard focus on hover. Driven by opts (the immediate row codepath).
 * Leaves the row CLOSED (single-shot rows); item_begin opens its own custom-content element.
 * The row interaction is returned; the caller does the ONE step_interaction (so item_begin can defer it
 * to item_end). Returns the row id via *out_id. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — icon + eased hover + marker branch, not deep nesting
static nt_ui_interaction_t menu_im_row(nt_ui_menu_ctx_t *menu, uint32_t key, const char *label, const nt_ui_menu_item_opts_t *opts, bool has_sub, bool open_element, uint32_t *out_id) {
    nt_ui_context_t *ctx = menu->ui;
    const uint8_t depth = menu->pending_menu.depth;
    const uint16_t running_idx = menu->pending_menu.item_idx[depth];
    const uint32_t row_id = menu_item_id(menu->pending_menu.scope_id[depth], key);
    *out_id = row_id;
    const uint8_t fill_layer = menu->pending_menu.fill_layer;
    const uint8_t label_layer = menu->pending_menu.label_layer;
    nt_ui_menu_style_t *style = (nt_ui_menu_style_t *)menu->pending_menu.style;
    nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu->pending_menu.menu_id);
    const bool enabled = opts->enabled;

    const nt_ui_interaction_t in = enabled ? nt_ui_query_interaction(ctx, row_id) : (nt_ui_interaction_t){0};
    const bool focused = (rt->focus[depth] == (int16_t)running_idx);
    const bool highlit = enabled && (in.hovered || focused);

    const nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = highlit ? 1.0F : 0.0F};
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, row_id, &tgt, 0.0F, style->state_speed);
    Clay_Color bg = {0};
    if (style->item_hover_color != 0U) {
        bg = nt_ui_unpack_abgr(style->item_hover_color);
        bg.a = bg.a * a->value_t;
    }
    const uint32_t txt = enabled ? style->text_color : style->text_disabled;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(txt)};
    const Clay_ElementDeclaration row = {
        .id = (Clay_ElementId){.id = row_id},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->item_height)},
                   .padding = {.left = style->pad, .right = style->pad},
                   .childGap = style->pad,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = bg,
        .userData = (void *)nt_ui_make_element_data(fill_layer, NULL),
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(row);
    if (!open_element) {
        const uint32_t menu_id = menu->pending_menu.menu_id;
        menu_declare_icon(ctx, fill_layer, menu_icon_id(menu_id, depth, running_idx), &opts->icon, style);
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), label != NULL ? label : "", &lbl);
        /* One GROW spacer right-aligns the trailing cells (shortcut / checkmark / submenu marker). */
        if (opts->shortcut != NULL || opts->selected || has_sub) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        }
        if (opts->shortcut != NULL) {
            menu_declare_shortcut(ctx, label_layer, menu_shortcut_id(menu_id, depth, running_idx), opts->shortcut, style);
        }
        if (opts->selected) {
            menu_declare_check(ctx, fill_layer, label_layer, menu_check_id(menu_id, depth, running_idx), &lbl, style);
        }
        if (has_sub) {
            menu_declare_marker(ctx, fill_layer, label_layer, menu_arrow_id(menu_id, depth, running_idx), &lbl, style);
        }
        nt_ui_clay_priv_close_element();
    }
    /* open_element=true (item_begin): leave the element OPEN for the game's custom content; the gutter/
     * label are the game's responsibility there. */

    if (enabled && in.hovered) {
        rt->focus[depth] = (int16_t)running_idx; /* hover moves keyboard focus too */
        /* Hover-open: record the hovered row so menu_end can drive the mouse-aim corridor. A
         * hovered PARENT is a candidate to fly out; a hovered LEAF requests collapsing the open child
         * (gated by the corridor so a diagonal cursor toward the open child does not collapse it). */
        if (has_sub) {
            menu->pending_menu.hover_parent[depth] = (int16_t)running_idx;
        } else {
            menu->pending_menu.hover_leaf[depth] = 1U;
        }
    }
    menu_record_append(menu, depth, row_id, running_idx, enabled, has_sub);
    menu->pending_menu.item_idx[depth] = (uint16_t)(running_idx + 1U);
    return in;
}

/* Open a level's PANEL element (art-or-flat slice9) and LEAVE it open for the level's item children. The
 * caller (menu_begin / submenu_begin) balances it with nt_ui_clay_priv_close_element. */
static void menu_open_panel(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t menu_id, uint8_t depth, nt_ui_menu_style_t *style) {
    nt_atlas_resolve_ref(&style->panel_bg);
    const bool panel_art = (style->panel_bg.atlas.id != 0U && style->panel_bg.region != NT_ATLAS_INVALID_REGION);
    Clay_ElementDeclaration panel = {.id = (Clay_ElementId){.id = menu_panel_id(menu_id, depth)},
                                     .layout = {.sizing = {.width = CLAY_SIZING_FIT(.min = (float)style->min_width), .height = CLAY_SIZING_FIT(0)},
                                                .padding = CLAY_PADDING_ALL(style->pad),
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                .childGap = 2},
                                     .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)};
    if (panel_art) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_menu: scratch alloc failed (panel payload)");
        *p = (nt_ui_image_payload_t){.atlas = style->panel_bg.atlas, .region_index = style->panel_bg.region, .slice9_scale = style->slice9_scale};
        panel.image = (Clay_ImageElementConfig){.imageData = p};
        panel.backgroundColor = nt_ui_unpack_tint(style->panel_tint);
    } else {
        panel.backgroundColor = nt_ui_unpack_abgr(style->bg_color);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel);
}

/* Open a level's popup at `anchor` (no per-level light-dismiss catcher — the menu owns its own
 * outside-click dismiss). Stashes the resolved side per depth so the mouse-aim corridor picks the right
 * near-edge (the submenu prefers RIGHT but edge-flips LEFT near the screen edge). */
static void menu_open_popup(nt_ui_menu_ctx_t *menu, uint32_t menu_id, uint8_t depth, uint8_t fill_layer, const nt_ui_menu_style_t *style, const nt_ui_popup_anchor_t *anchor) {
    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = style->open_ease_speed;
    pst.layer = fill_layer;
    pst.flags &= (uint8_t)~NT_UI_POPUP_LIGHT_DISMISS;
    const nt_ui_popup_result_t r = nt_ui_popup_begin(menu->ui, menu_level_id(menu_id, depth), &pst, anchor, true);
    menu->pending_menu.level_side[depth] = r.side;
}

/* Present-only state for a CLOSED menu: declare nothing, but the game still calls item/submenu/end every
 * frame (immediate mode) — so stay `active` (those calls assert it) yet flag open_frame=false to no-op. */
static void menu_begin_present_only(nt_ui_menu_ctx_t *menu, uint32_t menu_id, nt_ui_menu_state_t *st) {
    menu_runtime_reset(menu_runtime(menu->ui, menu_id)); /* clean chain for a later reopen */
    menu->pending_menu.st = st;
    menu->pending_menu.menu_id = menu_id;
    menu->pending_menu.depth = 0U;
    menu->pending_menu.chosen = 0U;
    menu->pending_menu.active = true;
    menu->pending_menu.open_frame = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the leading validation assert chain inflates the count; control flow is flat (one open/closed branch)
void nt_ui_menu_begin(nt_ui_menu_ctx_t *menu, nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t menu_id, nt_ui_menu_state_t *st, nt_ui_menu_style_t *style) {
    NT_ASSERT(menu != NULL && "nt_ui_menu_begin: menu must be non-NULL");
    NT_ASSERT(ctx != NULL && "nt_ui_menu_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_begin: call between nt_ui_begin/end");
    NT_ASSERT(menu_id != 0U && st != NULL && style != NULL && "nt_ui_menu_begin: menu_id non-zero, st + style non-NULL");
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_menu_begin: style->font_size must be > 0");
    NT_ASSERT(!menu->pending_menu.active && "nt_ui_menu_begin: a menu does not re-enter at depth 0");

    menu->ui = ctx; /* stash the core ctx for the begin/end span; cleared in menu_end */

    if (!st->open) {
        menu_begin_present_only(menu, menu_id, st);
        return;
    }
    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* fills on data->layer, text on label_layer (game-controlled render order) */

    menu_declare_occluder(ctx, fill_layer, menu_id); /* single root occluder under the whole stack */

    menu->pending_menu.style = style;
    menu->pending_menu.st = st;
    menu->pending_menu.menu_id = menu_id;
    menu->pending_menu.scope_id[0] = menu_id;
    menu->pending_menu.item_idx[0] = 0U;
    menu->pending_menu.pending_open[0] = -1;
    menu->pending_menu.hover_parent[0] = -1;
    menu->pending_menu.hover_leaf[0] = 0U;
    menu->pending_menu.chosen = 0U;
    menu->pending_menu.fill_layer = fill_layer;
    menu->pending_menu.label_layer = label_layer;
    menu->pending_menu.depth = 0U;
    menu->pending_menu.active = true;
    menu->pending_menu.open_frame = true;
    menu->frame_record_count[0] = 0U;

    /* Root opens BELOW the cursor point; BELOW only edge-flips on the VERTICAL axis, so a cursor near the
     * RIGHT screen edge would grow the root panel off-screen horizontally (and drag the whole submenu stack
     * with it). Clamp the anchor x left by the measured prev-frame panel width so the root (and every level
     * anchored under it) stays on-screen — mirrors the OS context-menu shift. 1-frame IM lag on the width;
     * the open point is unclamped the first frame, then settles. */
    float root_x = st->anchor_x;
    const nt_ui_bbox_t root_bb = nt_ui_get_bbox(ctx, menu_level_id(menu_id, 0U));
    if (root_bb.found) {
        const float screen_w = nt_ui_clay_priv_layout_width(ctx->clay);
        if (root_x + root_bb.width > screen_w) {
            root_x = screen_w - root_bb.width;
        }
        if (root_x < 0.0F) {
            root_x = 0.0F;
        }
    }
    const nt_ui_popup_anchor_t root_anchor = {.x = root_x, .y = st->anchor_y, .w = 0.0F, .h = 0.0F, .prefer_side = NT_UI_POPUP_BELOW};
    menu_open_popup(menu, menu_id, 0U, fill_layer, style, &root_anchor);
    menu_open_panel(ctx, fill_layer, menu_id, 0U, style); /* LEFT open for the item children (closed in menu_end) */
}

bool nt_ui_menu_item_ex(nt_ui_menu_ctx_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts) {
    NT_ASSERT(menu->pending_menu.active && "nt_ui_menu_item_ex: call between nt_ui_menu_begin/end");
    if (!menu->pending_menu.open_frame) {
        return false; /* present-only menu: no-op */
    }
    nt_ui_context_t *ctx = menu->ui;
    uint32_t row_id = 0U;
    const nt_ui_interaction_t in = menu_im_row(menu, key, label, &opts, false, false, &row_id);
    if (opts.enabled) {
        (void)nt_ui_step_interaction(ctx, row_id);
    }
    /* The inline bool is the SINGLE activation idiom: mouse clicks same-frame; a keyboard Enter arrives via
     * rt->kbd_activated (set in the PREVIOUS frame's menu_end, 1-frame latency). Both latch pending_menu.chosen
     * (the internal close-chain signal). */
    const nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu->pending_menu.menu_id);
    const bool activated = opts.enabled && (in.clicked || (row_id == rt->kbd_activated));
    if (activated) {
        menu->pending_menu.chosen = row_id;
    }
    return activated;
}

bool nt_ui_menu_item(nt_ui_menu_ctx_t *menu, uint32_t key, const char *label) { return nt_ui_menu_item_ex(menu, key, label, nt_ui_menu_item_opts_defaults()); }

bool nt_ui_menu_item_begin(nt_ui_menu_ctx_t *menu, uint32_t key, nt_ui_menu_item_opts_t opts) {
    NT_ASSERT(menu->pending_menu.active && "nt_ui_menu_item_begin: call between nt_ui_menu_begin/end");
    NT_ASSERT(!menu->pending_menu_item.active && "nt_ui_menu_item_begin: a custom row is already open (missing nt_ui_menu_item_end)");
    /* Returns DECLARE-BODY (true only when the menu is open this frame), NOT clicked: a present-only
     * (closed) menu opens no row element, so the game MUST guard the custom-content body with the return —
     * otherwise the body's children leak into the scene's open element. An activatable row reports
     * its ACTIVATION via item_end (parallel to item/item_ex returning clicked) — the two bools never alias. */
    if (!menu->pending_menu.open_frame) {
        menu->pending_menu_item.active = true; /* still balance item_end; the body is skipped by the guard */
        menu->pending_menu_item.enabled = false;
        menu->pending_menu_item.activatable = false;
        menu->pending_menu_item.id = 0U;
        return false; /* closed: do NOT declare the body */
    }
    uint32_t row_id = 0U;
    (void)menu_im_row(menu, key, NULL, &opts, false, true, &row_id);
    menu->pending_menu_item.id = row_id;
    menu->pending_menu_item.enabled = opts.enabled;
    menu->pending_menu_item.activatable = opts.activatable;
    menu->pending_menu_item.active = true;
    return true; /* open: declare the body */
}

bool nt_ui_menu_item_end(nt_ui_menu_ctx_t *menu) {
    NT_ASSERT(menu->pending_menu_item.active && "nt_ui_menu_item_end without nt_ui_menu_item_begin");
    const uint32_t row_id = menu->pending_menu_item.id;
    menu->pending_menu_item.active = false;
    if (!menu->pending_menu.open_frame) {
        return false; /* present-only: item_begin opened no element, so close/step nothing (never activated) */
    }
    nt_ui_clay_priv_close_element();
    /* A disabled row never steps/activates — mirror item_ex's enabled gate (a disabled custom row must not
     * capture the pointer nor fire on a stashed keyboard activation). */
    if (!menu->pending_menu_item.enabled || !menu->pending_menu_item.activatable) {
        /* activatable=false: the inner interactive child owns the click. The row must NOT step_interaction —
         * a row step would capture the pointer and starve the child. Hover highlight still works via the
         * read-only query_interaction in menu_im_row (prev-frame geometry, no capture). Never activates. */
        return false;
    }
    nt_ui_context_t *ctx = menu->ui;
    const nt_ui_interaction_t in = nt_ui_step_interaction(ctx, row_id); /* the ONE mutating step per id */
    /* Same single-idiom activation as item_ex: mouse same-frame, keyboard via rt->kbd_activated (1-frame).
     * Returned to the caller (item_end == "activated"); also latches the internal close-chain signal. */
    const nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu->pending_menu.menu_id);
    const bool activated = in.clicked || (row_id == rt->kbd_activated);
    if (activated) {
        menu->pending_menu.chosen = row_id;
    }
    return activated;
}

bool nt_ui_menu_submenu_begin(nt_ui_menu_ctx_t *menu, uint32_t key, const char *label) {
    NT_ASSERT(menu->pending_menu.active && "nt_ui_menu_submenu_begin: call between nt_ui_menu_begin/end");
    if (!menu->pending_menu.open_frame) {
        return false; /* present-only menu: declare nothing, game skips the body */
    }
    nt_ui_context_t *ctx = menu->ui;
    const uint8_t depth = menu->pending_menu.depth;
    const uint16_t running_idx = menu->pending_menu.item_idx[depth]; /* before menu_im_row advances it */
    uint32_t row_id = 0U;
    const nt_ui_interaction_t in = menu_im_row(menu, key, label, &(nt_ui_menu_item_opts_t){.enabled = true}, true, false, &row_id);
    (void)nt_ui_step_interaction(ctx, row_id);
    if (in.clicked) {
        menu->pending_menu.pending_open[depth] = (int16_t)running_idx; /* mouse-open this parent (committed in menu_end) */
    }

    nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu->pending_menu.menu_id);
    if (rt->open_path[depth] != (int16_t)running_idx) {
        return false; /* this submenu is not the open one — game skips its body */
    }

    /* Open: push a new level scoped to THIS row's id (ImGui PushID) and fly its panel out. */
    NT_ASSERT((depth + 1U) < NT_UI_MENU_MAX_DEPTH && "nt_ui_menu: submenu nesting exceeds NT_UI_MENU_MAX_DEPTH");
    const uint8_t child = (uint8_t)(depth + 1U);
    menu->pending_menu.scope_id[child] = row_id;
    menu->pending_menu.item_idx[child] = 0U;
    menu->pending_menu.pending_open[child] = -1;
    menu->pending_menu.hover_parent[child] = -1;
    menu->pending_menu.hover_leaf[child] = 0U;
    menu->pending_menu.depth = child;
    menu->frame_record_count[child] = 0U;

    nt_ui_menu_style_t *style = (nt_ui_menu_style_t *)menu->pending_menu.style;
    const uint8_t fill_layer = menu->pending_menu.fill_layer;
    const nt_ui_popup_anchor_t sub_anchor = menu_submenu_anchor(ctx, row_id);
    menu_open_popup(menu, menu->pending_menu.menu_id, child, fill_layer, style, &sub_anchor);
    menu_open_panel(ctx, fill_layer, menu->pending_menu.menu_id, child, style); /* LEFT open for the submenu's children (closed in submenu_end) */
    return true;
}

void nt_ui_menu_submenu_end(nt_ui_menu_ctx_t *menu) {
    NT_ASSERT(menu->pending_menu.active && menu->pending_menu.depth > 0U && "nt_ui_menu_submenu_end without an open submenu");
    nt_ui_clay_priv_close_element(); /* the child panel */
    nt_ui_popup_end(menu->ui);       /* balance the level's popup_begin */
    menu->pending_menu.depth = (uint8_t)(menu->pending_menu.depth - 1U);
}

void nt_ui_menu_separator(nt_ui_menu_ctx_t *menu) {
    NT_ASSERT(menu->pending_menu.active && "nt_ui_menu_separator: call between nt_ui_menu_begin/end");
    if (!menu->pending_menu.open_frame) {
        return;
    }
    nt_ui_menu_style_t *style = (nt_ui_menu_style_t *)menu->pending_menu.style;
    menu_declare_separator(menu->ui, menu->pending_menu.fill_layer, style);
    /* A separator has NO id and is NOT recorded (nav skips it), but it DOES advance the running layout
     * index so sibling ids stay positionally stable. */
    const uint8_t depth = menu->pending_menu.depth;
    menu->pending_menu.item_idx[depth] = (uint16_t)(menu->pending_menu.item_idx[depth] + 1U);
}

void nt_ui_menu_separator_text(nt_ui_menu_ctx_t *menu, const char *label) {
    NT_ASSERT(menu->pending_menu.active && "nt_ui_menu_separator_text: call between nt_ui_menu_begin/end");
    if (!menu->pending_menu.open_frame) {
        return;
    }
    nt_ui_context_t *ctx = menu->ui;
    nt_ui_menu_style_t *style = (nt_ui_menu_style_t *)menu->pending_menu.style;
    const uint8_t fill_layer = menu->pending_menu.fill_layer;
    const uint8_t label_layer = menu->pending_menu.label_layer;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->separator_color)};
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.left = style->pad, .right = style->pad}}, .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), label != NULL ? label : "", &lbl);
    }
    const uint8_t depth = menu->pending_menu.depth;
    menu->pending_menu.item_idx[depth] = (uint16_t)(menu->pending_menu.item_idx[depth] + 1U);
}

/* Step focus over THIS-frame-recorded items at a level (prev-frame record), skipping disabled records.
 * focus is a layout index; find its position in the recorded array, step ±1, return the new layout idx.
 * From "no focus" (-1) Down lands on the first enabled record, Up on the last. */
static int16_t menu_im_focus_step(const nt_ui_menu_record_t *recs, uint16_t count, int16_t focus, int step) {
    if (count == 0U) {
        return focus;
    }
    int pos = -1;
    for (uint16_t k = 0; k < count; ++k) {
        if (recs[k].idx == focus) {
            pos = (int)k;
            break;
        }
    }
    for (uint16_t guard = 0; guard < count; ++guard) {
        if (step > 0) {
            pos = (pos + 1) % (int)count;
        } else {
            pos = (pos <= 0) ? (int)count - 1 : pos - 1;
        }
        if (recs[pos].enabled != 0U) {
            return (int16_t)recs[pos].idx;
        }
    }
    return focus;
}

/* Keyboard nav at nav_depth against the per-level record built THIS frame (the deepest open level was
 * declared this frame, so its record is complete by menu_end). Up/Down move focus; Right/Enter open a
 * focused parent (which is declared + navigable NEXT frame — the 1-frame latency); Enter
 * activates a focused leaf; Left/Esc collapse the level. Mutates rt->focus/open_path/active_depth + the
 * menu's chosen sink. Returns true if the level should close. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat key branch over the recorded slice, not deep nesting
static bool menu_im_keyboard_nav(nt_ui_menu_ctx_t *menu, uint8_t depth, nt_ui_menu_runtime_t *rt) {
    const nt_ui_menu_record_t *recs = menu->frame_record[depth];
    const uint16_t count = menu->frame_record_count[depth];
    int16_t f = rt->focus[depth];
    if (nt_input_key_is_pressed(NT_KEY_ARROW_DOWN)) {
        f = menu_im_focus_step(recs, count, f, +1);
    } else if (nt_input_key_is_pressed(NT_KEY_ARROW_UP)) {
        f = menu_im_focus_step(recs, count, f, -1);
    }
    rt->focus[depth] = f;

    const nt_ui_menu_record_t *cur = NULL;
    for (uint16_t k = 0; k < count; ++k) {
        if (recs[k].idx == f && recs[k].enabled != 0U) {
            cur = &recs[k];
            break;
        }
    }
    if (cur != NULL) {
        const bool open_key = nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) || nt_input_key_is_pressed(NT_KEY_ENTER);
        const bool activate_key = nt_input_key_is_pressed(NT_KEY_ENTER);
        if (cur->has_sub != 0U && open_key) {
            /* The deepest allowable level (depth == MAX_DEPTH-1) cannot open a child: open_path/active_depth/
             * focus[depth+1] would all index == MAX_DEPTH (OOB). Right/Enter on a parent there is a no-op. */
            if (depth + 1U < NT_UI_MENU_MAX_DEPTH) {
                rt->open_path[depth] = f;
                if (depth + 1U > rt->active_depth) {
                    rt->active_depth = (uint8_t)(depth + 1U);
                }
                rt->focus[depth + 1U] = 0; /* focus the child's first row (layout idx 0) */
            }
        } else if (cur->has_sub == 0U && activate_key) {
            /* Right on a leaf is a no-op; only Enter activates. Stash the leaf id for the NEXT frame's row
             * call to fire its inline return (1-frame latency); do NOT latch chosen here, so the chain stays
             * open one more frame and the inline return can fire before the close. */
            rt->kbd_activated = cur->id;
        }
    }
    return nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) || nt_input_key_is_pressed(NT_KEY_ESCAPE);
}

/* Get-or-create the depth-salted mouse-aim hover-intent cell (the corridor apex + dwell timer per open
 * submenu level). Salted by depth so level N never aliases N+1. */
static nt_ui_menu_hover_t *menu_hover_cell(nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth) {
    return nt_ui_state(ctx, menu_hover_id(menu_id, depth), sizeof(nt_ui_menu_hover_t), NT_UI_MENU_TAG);
}

/* Hover-open + mouse-aim corridor + leave-close for the immediate fly-out. For each open level
 * d (0..active_depth), drive the corridor against the OPEN child's prev-frame bbox: while the cursor aims
 * at (or sits over) the open child, KEEP it (a diagonal toward the child never collapses it). Once the
 * corridor releases, a hovered SIBLING parent flies out instead, or a hovered leaf collapses the child.
 * A keyboard-opened submenu is authoritative — this only governs MOUSE switching, never a blanket close.
 * Mutates rt->open_path/active_depth; re-primes the corridor cell on an open-child change (1-frame lag:
 * the new child is declared next frame). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat per-depth corridor branch, not deep nesting
static void menu_resolve_hover_open(nt_ui_menu_ctx_t *menu, uint32_t menu_id, nt_ui_menu_runtime_t *rt, float mx, float my, float dt) {
    nt_ui_context_t *ctx = menu->ui;
    for (uint8_t d = 0; d <= rt->active_depth && d < NT_UI_MENU_MAX_DEPTH; ++d) {
        const int16_t cur_open = rt->open_path[d];
        const int16_t hov_parent = menu->pending_menu.hover_parent[d];
        const bool hov_leaf = menu->pending_menu.hover_leaf[d] != 0U;

        /* The deepest allowable level cannot open a child (d+1 would index == MAX_DEPTH, OOB on
         * level_side/the child-level cells below). A hovered parent there can only collapse, never fly out. */
        const bool can_open_child = (d + 1U) < NT_UI_MENU_MAX_DEPTH;

        /* No switch requested (no hovered sibling parent, no hovered leaf): leave the level as-is. */
        if (hov_parent < 0 && !hov_leaf) {
            continue;
        }
        /* The hovered parent is already the open one: nothing to switch. */
        if (hov_parent >= 0 && hov_parent == cur_open) {
            continue;
        }

        /* If a child is open at this level, gate the switch on the mouse-aim corridor: while the cursor is
         * aiming at / over the open child, KEEP it (do not switch/collapse). */
        bool may_switch = true;
        if (cur_open >= 0 && can_open_child) {
            const nt_ui_bbox_t cbb = nt_ui_get_bbox(ctx, menu_level_id(menu_id, (uint8_t)(d + 1U)));
            if (cbb.found) {
                nt_ui_menu_hover_t *hc = menu_hover_cell(ctx, menu_id, (uint8_t)(d + 1U));
                const uint8_t side = menu->pending_menu.level_side[d + 1U];
                may_switch = !menu_hover_intent(hc, mx, my, cbb.x, cbb.y, cbb.width, cbb.height, side, dt);
            }
        }
        if (!may_switch) {
            continue; /* corridor holds: keep aiming at the open child */
        }

        int16_t new_open = cur_open;
        if (hov_parent >= 0 && can_open_child) {
            new_open = hov_parent; /* fly out the hovered sibling parent (never past the depth cap) */
        } else if (hov_leaf) {
            new_open = -1; /* a hovered leaf with the corridor released collapses the open child */
        }
        if (new_open == cur_open) {
            continue;
        }
        rt->open_path[d] = new_open;
        if (new_open >= 0) {
            if (d + 1U > rt->active_depth) {
                rt->active_depth = (uint8_t)(d + 1U);
            }
        } else if (rt->active_depth > d) {
            rt->active_depth = d; /* collapsed: this level no longer has an open child */
        }
        /* The open child changed: clear the stale aim apex/timer so menu_hover_intent re-primes next frame
         * against the new child rect (without this the new child reuses the prior corridor). */
        nt_ui_state_clear(ctx, menu_hover_id(menu_id, (uint8_t)(d + 1U)));
    }
}

/* Resolve this frame's keyboard nav + mouse-open commit + outside-click dismiss against the runtime cell.
 * Returns true if the whole chain should close (Esc at root / leaf activate / outside-click). */
static bool menu_resolve_nav_and_dismiss(nt_ui_menu_ctx_t *menu, uint32_t menu_id, nt_ui_menu_runtime_t *rt) {
    nt_ui_context_t *ctx = menu->ui;
    bool close_chain = false;
    /* Defense-in-depth: clamp below the cap so menu_im_keyboard_nav never indexes frame_record[MAX_DEPTH]
     * even if some other path leaves active_depth at the cap (the open paths already guard against it). */
    const uint8_t nav_depth = (rt->active_depth < NT_UI_MENU_MAX_DEPTH) ? rt->active_depth : (uint8_t)(NT_UI_MENU_MAX_DEPTH - 1U);
    const bool kbd_acted = nt_input_key_is_pressed(NT_KEY_ARROW_UP) || nt_input_key_is_pressed(NT_KEY_ARROW_DOWN) || nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) ||
                           nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) || nt_input_key_is_pressed(NT_KEY_ENTER) || nt_input_key_is_pressed(NT_KEY_ESCAPE);
    if (menu_im_keyboard_nav(menu, nav_depth, rt)) {
        if (nav_depth == 0U) {
            close_chain = true;
        } else {
            rt->open_path[nav_depth - 1U] = -1; /* collapse this level back into the parent */
            rt->active_depth = (uint8_t)(nav_depth - 1U);
        }
    }
    float mx = 0.0F;
    float my = 0.0F;
    menu_mouse_pos(ctx, &mx, &my);
    /* Hover-open: the mouse-aim corridor drives the open chain. Skipped on a keyboard-nav frame so a
     * keyboard-opened submenu is authoritative and never collapses from cursor distance the same frame. */
    if (!kbd_acted) {
        menu_resolve_hover_open(menu, menu_id, rt, mx, my, ctx->frame_dt);
    }
    /* Commit a mouse-opened root parent (an explicit click this frame, e.g. touch) so submenu_begin returns
     * true next frame. Hover-open already handles the common pointer case above. */
    const int16_t mouse_open = menu->pending_menu.pending_open[0];
    if (mouse_open >= 0) {
        rt->open_path[0] = mouse_open;
        if (rt->active_depth < 1U) {
            rt->active_depth = 1U;
        }
    }
    /* Menu-owned outside-click dismiss; skip the opening frame so the arming click does not self-dismiss. */
    const bool outside_press = nt_input_mouse_is_pressed(NT_BUTTON_LEFT) || nt_input_mouse_is_pressed(NT_BUTTON_RIGHT);
    if (outside_press && !menu_cursor_over_any_panel(ctx, menu_id, rt, mx, my) && ctx->frame_counter != rt->opened_frame) {
        close_chain = true;
    }
    return close_chain;
}

/* Resolve nav/dismiss + fold the activation signals into the close decision. The kbd_activated set in the
 * PREVIOUS frame's menu_end was already consumed by THIS frame's row calls (they ran before menu_end), so
 * snapshot it before kbd-nav (which may set a NEW activation for the next frame) and clear ONLY that
 * consumed value — so each keyboard activation fires its inline return exactly once. */
static bool menu_resolve_and_consume_kbd(nt_ui_menu_ctx_t *menu, uint32_t menu_id, nt_ui_menu_runtime_t *rt) {
    const uint32_t consumed_kbd = rt->kbd_activated;
    bool close_chain = menu_resolve_nav_and_dismiss(menu, menu_id, rt);
    if (menu->pending_menu.chosen != 0U) {
        close_chain = true; /* an activation (mouse this frame, or keyboard consumed via the inline return) closes the chain */
    }
    if (rt->kbd_activated == consumed_kbd) {
        rt->kbd_activated = 0U; /* a fresh value set by kbd-nav this frame survives for the next frame's row call */
    }
    return close_chain;
}

void nt_ui_menu_end(nt_ui_menu_ctx_t *menu) {
    nt_ui_context_t *ctx = menu->ui;
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_end: call between nt_ui_begin/end");
    if (!menu->pending_menu.active) {
        menu->ui = NULL; /* defensive: menu_end without a begin — drop the span handle */
        return;
    }
    if (!menu->pending_menu.open_frame) {
        menu->pending_menu.active = false; /* present-only: begin declared nothing, nothing to balance */
        menu->ui = NULL;
        return;
    }
    NT_ASSERT(menu->pending_menu.depth == 0U && "nt_ui_menu_end with an open submenu (missing nt_ui_menu_submenu_end)");
    NT_ASSERT(!menu->pending_menu_item.active && "nt_ui_menu_end with an open custom row (missing nt_ui_menu_item_end)");

    nt_ui_clay_priv_close_element(); /* the root panel */

    const uint32_t menu_id = menu->pending_menu.menu_id;
    nt_ui_menu_state_t *st = menu->pending_menu.st;
    nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu_id);

    const bool close_chain = menu_resolve_and_consume_kbd(menu, menu_id, rt);

    nt_ui_popup_end(ctx); /* balance menu_begin's root popup_begin */

    if (close_chain) {
        st->open = false;
        menu_runtime_reset(rt);
    }
    menu->pending_menu.active = false;
    menu->ui = NULL; /* clear the span handle: a between-frames inner call now traps on a NULL ctx */
}
// #endregion

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

uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key) { return menu_item_id(scope_id, key); }

/* The most-recently-rendered frame's recorded item id at rt->focus[depth]. 0 if none / no record. The
 * runtime cell lives in ctx's state pool (persists across frames); the per-level record lives on the
 * game-owned menu — both outlive the in-frame ctx span, so the post-nt_ui_end read is clean. */
uint32_t nt_ui_menu_test_focus_item_id(const nt_ui_context_t *ctx, const nt_ui_menu_ctx_t *menu, uint32_t menu_id, uint8_t depth) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu_test_focus_item_id: ctx must be non-NULL");
    const nt_ui_menu_runtime_t *rt = nt_ui_state_find((nt_ui_context_t *)ctx, menu_runtime_id(menu_id));
    if (rt == NULL || depth >= NT_UI_MENU_MAX_DEPTH) {
        return 0U;
    }
    const int16_t focus = rt->focus[depth];
    const uint16_t count = menu->frame_record_count[depth];
    for (uint16_t k = 0; k < count; ++k) {
        if (menu->frame_record[depth][k].idx == focus) {
            return menu->frame_record[depth][k].id;
        }
    }
    return 0U;
}

uint32_t nt_ui_menu_test_panel_id(uint32_t menu_id, uint8_t depth) { return menu_panel_id(menu_id, depth); }
uint32_t nt_ui_menu_test_row_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_row_id(menu_id, depth, item_idx); }
uint32_t nt_ui_menu_test_arrow_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_arrow_id(menu_id, depth, item_idx); }
uint32_t nt_ui_menu_test_icon_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_icon_id(menu_id, depth, item_idx); }
uint32_t nt_ui_menu_test_occluder_id(uint32_t menu_id) { return menu_occluder_id(menu_id); }
uint32_t nt_ui_menu_test_shortcut_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_shortcut_id(menu_id, depth, item_idx); }
uint32_t nt_ui_menu_test_check_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx) { return menu_check_id(menu_id, depth, item_idx); }

/* The retained open-chain index at a level (rt->open_path[depth]): -1 = no submenu open at that depth.
 * Drives the immediate hover-open probe (hover a parent row -> open_path opens it). The runtime cell lives
 * in ctx's state pool, so the explicit ctx replaces the old post-end file-static fallback. */
int16_t nt_ui_menu_test_open_path(const nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu_test_open_path: ctx must be non-NULL");
    const nt_ui_menu_runtime_t *rt = nt_ui_state_find((nt_ui_context_t *)ctx, menu_runtime_id(menu_id));
    if (rt == NULL || depth >= NT_UI_MENU_MAX_DEPTH) {
        return -1;
    }
    return rt->open_path[depth];
}

/* The deepest currently-open level (rt->active_depth). 0 = only the root open / no runtime cell yet. */
uint8_t nt_ui_menu_test_active_depth(const nt_ui_context_t *ctx, uint32_t menu_id) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu_test_active_depth: ctx must be non-NULL");
    const nt_ui_menu_runtime_t *rt = nt_ui_state_find((nt_ui_context_t *)ctx, menu_runtime_id(menu_id));
    return (rt != NULL) ? rt->active_depth : 0U;
}

/* Force the open chain to a target depth (self-keyed: every level opens item 0). The nav guard now caps
 * keyboard-open at the cap, so the decl-time backstop assert in submenu_begin is only reachable when the
 * chain is forced open this deep — the death test drives it directly via this setter. */
void nt_ui_menu_test_force_open_to(nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth) {
    NT_ASSERT(ctx != NULL && "nt_ui_menu_test_force_open_to: ctx must be non-NULL");
    nt_ui_menu_runtime_t *rt = menu_runtime(ctx, menu_id);
    /* Mark every level's item-0 submenu open up to `depth` (clamped to the last valid array index). Setting
     * open_path at index MAX_DEPTH-1 makes submenu_begin ENTER its body there and try to push past the cap —
     * the decl-time backstop the death test verifies. active_depth itself stays within the cap. */
    const uint8_t last = (depth < NT_UI_MENU_MAX_DEPTH) ? depth : (uint8_t)(NT_UI_MENU_MAX_DEPTH - 1U);
    for (uint8_t d = 0; d <= last; ++d) {
        rt->open_path[d] = 0; /* level d's item 0 (the self-keyed submenu) is open */
        rt->focus[d] = 0;
    }
    rt->active_depth = (last < NT_UI_MENU_MAX_DEPTH - 1U) ? last : (uint8_t)(NT_UI_MENU_MAX_DEPTH - 1U);
}
#endif
