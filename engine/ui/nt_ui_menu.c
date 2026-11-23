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
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_popup_internal.h"

_Static_assert(NT_UI_MENU_MAX_DEPTH <= NT_UI_MODAL_MAX_DEPTH, "a menu level is a popup: its depth must fit the overlay stack");

const nt_ui_widget_def_t NT_UI_MENU_DEF = {
    .name = "nt_menu",
    .pill_color = 0xFF40C080U,
    ._reserved = 0U,
};

/* Kind tags folded into the id hash so a level/panel/row cell never collides with another.
 * nt_ui_derived_id (XOR) cannot be used to compose depth+kind because additive salts with a shared
 * stride XOR-cancel across (depth,kind) pairs. */
enum {
    NT_UI_MENU_KIND_LEVEL = 1,
    NT_UI_MENU_KIND_PANEL = 2,
    NT_UI_MENU_KIND_ARROW = 6,
    NT_UI_MENU_KIND_ICON = 7,
    NT_UI_MENU_KIND_SHORTCUT = 8,
    NT_UI_MENU_KIND_CHECK = 9,
};

/* fmix-style 32-bit hash combining a base id + kind + depth. Non-XOR-symmetric, so distinct
 * (kind,depth) pairs never alias. Folds 0 to 1 (id 0 is the empty-slot sentinel). */
static inline uint32_t menu_hash_id(uint32_t base_id, uint32_t kind, uint32_t depth) {
    uint32_t h = base_id * 0x9E3779B1U;
    h = (h ^ ((kind + 1U) * 0x85EBCA6BU));
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = (h ^ ((depth + 1U) * 0x27D4EB2FU));
    h = (h ^ (h >> 15)) * 0x165667B1U;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}

/* Row id: fmix of scope + key only, so a sibling appearing or disappearing never shifts a later row's id
 * (its anim/Clay identity and its child scope). Folds 0 to 1. */
static inline uint32_t menu_item_id(uint32_t scope_id, uint32_t key) {
    uint32_t h = scope_id * 0x9E3779B1U;
    h = (h ^ ((key + 1U) * 0x85EBCA6BU));
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = (h ^ (h >> 15)) * 0x165667B1U;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}

static inline uint32_t menu_level_id(uint32_t menu_id, uint8_t depth) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_LEVEL, depth); }
static inline uint32_t menu_panel_id(uint32_t menu_id, uint8_t depth) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_PANEL, depth); }
/* A row's sub-element cells (arrow marker / icon / shortcut / check) carry explicit ids derived from the row
 * id: Clay's anonymous child ids are additive, so consecutive rows' children would collide. */
static inline uint32_t menu_cell_id(uint32_t row_id, uint32_t kind) { return menu_hash_id(row_id, kind, 0U); }

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

/* The submenu edge the corridor aims at: the one facing the apex, so a submenu edge-flipped to the LEFT of
 * its parent aims at its RIGHT edge. */
static float menu_aim_near_x(float sub_x, float sub_w, float apex_x) { return (apex_x > sub_x + (sub_w * 0.5F)) ? (sub_x + sub_w) : sub_x; }

/* true KEEPS the open child `child`. The apex stays fixed while the corridor lives: a per-frame apex ~1px
 * from the cursor makes a degenerate wedge that never releases. Over the child: keep and reset the timer;
 * in the corridor: keep for at most 2x AIM_FALLBACK, so a parked cursor still releases; elsewhere, or with a
 * pointer that is not the menu's (outside the window, on the inspector): switch after AIM_FALLBACK. */
static bool menu_hover_intent(nt_ui_menu_aim_t *c, uint8_t child, bool ours, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, float dt) {
    if (c->depth != child) {
        c->apex_x = mouse_x;
        c->apex_y = mouse_y;
        c->timer = 0.0F;
        c->depth = child;
        return true;
    }
    if (ours && mouse_x >= sub_x && mouse_x <= sub_x + sub_w && mouse_y >= sub_y && mouse_y <= sub_y + sub_h) {
        c->timer = 0.0F;
        return true;
    }
    const float near_x = menu_aim_near_x(sub_x, sub_w, c->apex_x);
    const bool aiming = ours && menu_point_in_tri(mouse_x, mouse_y, c->apex_x, c->apex_y, near_x, sub_y, near_x, sub_y + sub_h) && (c->timer < NT_UI_MENU_AIM_FALLBACK_SECS * 2.0F);
    c->timer += dt;
    return aiming || (c->timer < NT_UI_MENU_AIM_FALLBACK_SECS);
}
// #endregion

nt_ui_menu_style_t nt_ui_menu_style_defaults(void) {
    /* Flat baseline: no atlas art, so a flat background and a ">" text marker. */
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
        .item_height = 26U,
        .min_width = 160U,
        .pad = 6U,
        .icon_size = 0U,   /* no icon gutter by default (text-only) */
        .arrow_size = 14U, /* used only when the arrow ref is set */
        .check_size = 14U, /* used only when the checkmark ref is set (mirror arrow_size) */
        .separator_height = 2U,
    };
}

// #region navigation state (open chain + focus, owned by nt_ui_menu_t)
static uint8_t menu_open_depth(const nt_ui_menu_t *menu) {
    uint8_t d = 0U;
    while (d + 1U < NT_UI_MENU_MAX_DEPTH && menu->open_path[d] != 0U) {
        ++d;
    }
    return d;
}

/* Opening or closing a level's child replaces the whole branch below it. Callers stay below the depth cap. */
static void menu_set_open(nt_ui_menu_t *menu, uint8_t depth, uint32_t id) {
    if (menu->open_path[depth] == id) {
        return;
    }
    memset(&menu->open_path[depth], 0, (size_t)(NT_UI_MENU_MAX_DEPTH - depth) * sizeof menu->open_path[0]);
    menu->open_path[depth] = id;
    menu->aim.depth = 0U; /* a changed chain primes a fresh corridor */
}

// #endregion

void nt_ui_menu_open(nt_ui_menu_t *menu, float x, float y) {
    NT_ASSERT(menu != NULL && menu->frame.ui == NULL && "nt_ui_menu_open: call outside nt_ui_menu_begin/end (it resets the chain the rows declare)");
    NT_ASSERT(isfinite(x) && isfinite(y) && "nt_ui_menu_open: the anchor point must be finite");
    *menu = (nt_ui_menu_t){.anchor_x = x, .anchor_y = y, .open = true, .arming = true};
}

// #region menu UI declaration (shared row/marker/panel helpers fed by the immediate path)
/* A separator: a thin non-interactive divider (no row id), indented by the panel pad. */
static void menu_declare_separator(uint8_t fill_layer, const nt_ui_menu_style_t *style) {
    const float h = (style->separator_height > 0U) ? (float)style->separator_height : 1.0F;
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.left = style->pad, .right = style->pad}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h)}},
              .backgroundColor = nt_ui_unpack_abgr(style->separator_color),
              .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {}
    }
}

/* Draw the resolved per-item icon image filling the gutter cell (no-op if the ref is unset/unresolved).
 * Split out so the gutter cell stays a flat declaration (cognitive-complexity). */
static void menu_draw_icon_image(uint8_t fill_layer, const nt_atlas_region_ref_t *icon) {
    if (icon->atlas.id == 0U) {
        return;
    }
    nt_atlas_region_ref_t ic = *icon; /* per-item ref (not style-owned): resolve by-value, never memoize */
    nt_atlas_resolve_ref(&ic);
    if (ic.region == NT_ATLAS_INVALID_REGION) {
        return;
    }
    nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
    *p = (nt_ui_image_payload_t){.atlas = ic.atlas, .region_index = ic.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .image = (Clay_ImageElementConfig){.imageData = p}, .userData = NT_UI_CLAY_DATA(fill_layer)}) {}
}

/* The leading icon gutter (unified icon model): reserve icon_size px so labels stay aligned across iconed
 * and non-iconed rows; draw the per-item icon if set, else leave the gutter empty. icon_size==0 -> no
 * gutter. */
static void menu_declare_icon(uint8_t fill_layer, uint32_t icon_id, const nt_atlas_region_ref_t *icon, const nt_ui_menu_style_t *style) {
    if (style->icon_size == 0U) {
        return;
    }
    const float gut = (float)style->icon_size;
    CLAY({.id = (Clay_ElementId){.id = icon_id}, .layout = {.sizing = {CLAY_SIZING_FIXED(gut), CLAY_SIZING_FIXED(gut)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        menu_draw_icon_image(fill_layer, icon);
    }
}

/* A trailing mark (submenu arrow, checkmark): `ref`'s sprite when it resolves, else `text`. The cell id keeps
 * consecutive rows' marks apart (Clay anonymous child ids are additive). */
static void menu_declare_mark(const nt_ui_menu_frame_t *f, uint32_t cell_id, nt_atlas_region_ref_t *ref, uint32_t tint, uint16_t size, const char *text, const nt_ui_label_style_t *lbl) {
    nt_atlas_resolve_ref(ref);
    if (ref->atlas.id != 0U && ref->region != NT_ATLAS_INVALID_REGION) {
        nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
        *p = (nt_ui_image_payload_t){.atlas = ref->atlas, .region_index = ref->region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
        const float sz = (size > 0U) ? (float)size : (float)f->style->item_height * 0.5F;
        CLAY({.id = (Clay_ElementId){.id = cell_id},
              .layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}},
              .image = (Clay_ImageElementConfig){.imageData = p},
              .backgroundColor = nt_ui_unpack_tint(tint),
              .userData = (void *)nt_ui_make_element_data(f->fill_layer, NULL)}) {}
    } else {
        CLAY({.id = (Clay_ElementId){.id = cell_id}, .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) { nt_ui_label(f->ui, nt_ui_make_element_data(f->label_layer, NULL), text, lbl); }
    }
}

/* The right-aligned shortcut text cell (e.g. "Ctrl+S") drawn in style->shortcut_text. The cell carries an
 * explicit fmix id so sibling rows' shortcut cells never collide. */
static void menu_declare_shortcut(nt_ui_context_t *ctx, uint8_t label_layer, uint32_t shortcut_id, const char *shortcut, nt_ui_menu_style_t *style) {
    const nt_ui_label_style_t sc = {.font_id = style->font_id, .font_size = style->font_size, .color = style->shortcut_text};
    CLAY({.id = (Clay_ElementId){.id = shortcut_id}, .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}}) { nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), shortcut, &sc); }
}

/* Resolve the anchor rect for a parent item's submenu = the item row's bbox; the submenu defaults to
 * RIGHT of the row and edge-flips per level via popup-core. found==false (the row's first frame) anchors at the
 * origin for that one frame. */
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

// #region immediate-mode menu (begin/item/submenu/separator/end)
/* nt_ui_menu_begin reads the frame's one key and applies Left/Esc; Enter and Right act in the call of the
 * row focused at begin; Up/Down step in menu_end over the neighbours the rows collect, and the pointer
 * records one hover and one click candidate there. */
enum { MENU_KEY_NONE = 0, MENU_KEY_BACK, MENU_KEY_RIGHT, MENU_KEY_ENTER, MENU_KEY_UP, MENU_KEY_DOWN };

/* The first pressed key decides the frame, Esc and Left first; Right and Enter need a focused row. */
static uint8_t menu_read_key(const nt_ui_menu_t *menu) {
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE) || nt_input_key_is_pressed(NT_KEY_ARROW_LEFT)) {
        return MENU_KEY_BACK;
    }
    if (nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) || nt_input_key_is_pressed(NT_KEY_ENTER)) {
        if (menu->focus == 0U) {
            return MENU_KEY_NONE;
        }
        return nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) ? MENU_KEY_RIGHT : MENU_KEY_ENTER;
    }
    if (nt_input_key_is_pressed(NT_KEY_ARROW_UP)) {
        return MENU_KEY_UP;
    }
    return nt_input_key_is_pressed(NT_KEY_ARROW_DOWN) ? MENU_KEY_DOWN : MENU_KEY_NONE;
}

static bool menu_key_on(const nt_ui_menu_t *menu, uint32_t row_id, uint8_t key) { return menu->frame.key == key && row_id == menu->frame.key_row; }

/* Everything the frame decides from last frame's records and this frame's input runs here, before the rows
 * declare: the branch a row's popup closes, the outside press, the one key and the end of keyboard mode.
 * Returns false when the menu closes (an outside press, Esc or Left at the root). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat rule list in frame order
static bool menu_frame_input(nt_ui_menu_t *menu, nt_ui_context_t *ctx, uint32_t menu_id, uint8_t *out_key) {
    /* A popup a row opened that is on top is that row's level's child: it closes the open branch below the
     * row, so each level keeps one open child and no level lies above the popup. */
    const uint32_t root_id = menu_level_id(menu_id, 0U);
    const uint32_t top_id = ctx->overlay_top_id_prev;
    if (top_id != root_id && nt_ui_internal_declared_within(ctx, top_id, root_id)) {
        uint8_t level = menu_open_depth(menu);
        while (level > 0U && !nt_ui_internal_declared_within(ctx, top_id, menu_level_id(menu_id, level))) {
            --level;
        }
        menu_set_open(menu, level, 0U);
    }
    const uint8_t open_depth = menu_open_depth(menu);
    if (menu->focus_depth > open_depth) {
        menu->focus = 0U; /* the focused row's level closed: keys act on the deepest open one, unfocused */
        menu->focus_depth = open_depth;
    }
    /* One owner: an opening menu reads input from its next frame, a menu under another overlay none. */
    const bool top = !menu->arming && top_id == root_id;
    const nt_pointer_t *pp = &ctx->frame_pointers[ctx->frame_primary];
    const bool ours = top && pp->active && !nt_ui_inspector_pointer_consumed(ctx);
    const bool pressed = pp->buttons[NT_BUTTON_LEFT].is_pressed || pp->buttons[NT_BUTTON_RIGHT].is_pressed;
    /* The outside press: a click or a tap off everything declared inside the menu last frame (its levels and a
     * popup a custom row opened), read from the element under the pointer, not a capture holder. */
    if (ours && pressed && !nt_ui_internal_declared_within(ctx, ctx->pointer_hot[ctx->frame_primary].id, root_id)) {
        return false;
    }
    /* A text field focused inside the menu owns the keys (the top takes the focus of a field under it). */
    const uint8_t key = (top && ctx->focused_input_id == 0U) ? menu_read_key(menu) : MENU_KEY_NONE;
    *out_key = key;
    const uint8_t d = menu->focus_depth;
    if (key != MENU_KEY_NONE) {
        menu->kbd_mode = true;
        if (menu->open_path[d] != menu->focus) {
            menu_set_open(menu, d, 0U); /* the keyboard takes no pending switch: a sibling's child closes */
        }
    } else if (menu->kbd_mode && ours && (pp->dx != 0.0F || pp->dy != 0.0F || pressed)) {
        /* The pointer takes focus and the open chain back when it moves or presses (a tap does not move), so a
         * pointer resting on a row neither steals focus nor reopens what the keyboard closed. */
        menu->kbd_mode = false;
        menu->aim.depth = 0U; /* the corridor primed before the keys is stale */
    }
    if (key == MENU_KEY_BACK && d == 0U) {
        return false;
    }
    if (key == MENU_KEY_BACK) {
        menu->focus = menu->open_path[d - 1U]; /* back on the parent it closes */
        menu->focus_depth = (uint8_t)(d - 1U);
        menu_set_open(menu, (uint8_t)(d - 1U), 0U);
    }
    return true;
}

/* Up/Down in menu_end step from the focused row to its enabled neighbours on the navigated level and wrap
 * at the ends; with the focused row absent, Down lands on the first enabled row and Up on the last. */
static void menu_nav_track(nt_ui_menu_frame_t *f, uint32_t focus, uint32_t row_id, bool enabled) {
    if (row_id == focus) {
        f->nav_prev = f->nav_last;
        f->nav_passed = true;
    } else if (enabled && f->nav_passed && f->nav_next == 0U) {
        f->nav_next = row_id;
    }
    if (enabled) {
        if (f->nav_first == 0U) {
            f->nav_first = row_id;
        }
        f->nav_last = row_id;
    }
}

/* Declares one row and tracks it for keyboard nav and hover. With open_element the row stays open for
 * item_begin's body. The caller does the row's one nt_ui_step (item_begin defers it to item_end). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — icon + eased hover + marker branch, not deep nesting
static uint32_t menu_im_row(nt_ui_menu_t *menu, uint32_t key, const char *label, const nt_ui_menu_item_opts_t *opts, bool has_sub, bool open_element) {
    nt_ui_menu_frame_t *f = &menu->frame;
    NT_ASSERT(f->item_id == 0U && "nt_ui_menu: a row inside a custom row (missing nt_ui_menu_item_end)");
    nt_ui_context_t *ctx = f->ui;
    const uint8_t depth = f->depth;
    const uint32_t scope = (depth == 0U) ? f->menu_id : menu->open_path[depth - 1U];
    const uint32_t row_id = menu_item_id(scope, key);
    nt_ui_menu_style_t *style = f->style;
    const bool enabled = !opts->disabled;

    /* A custom row stays under the pointer over its own interactive child (hot, declared later), so
     * focus, highlight and the leaf collapse follow the row the pointer is in. The opening frame's hits are
     * last frame's, at the old place. */
    const bool pointed = enabled && menu->frame.top && nt_ui_hovered_within(ctx, row_id);
    if (enabled && f->focus_first && depth == menu->focus_depth + 1U) {
        menu->focus = row_id;
        menu->focus_depth = depth;
        f->focus_first = false;
        if (menu->open_path[depth] != row_id) {
            menu_set_open(menu, depth, 0U); /* focus lands beside this level's open parent: its child closes */
        }
    }
    if (enabled && row_id == menu->focus) {
        f->focus_seen = true;
        f->focus_opens = has_sub;
    }
    if (pointed) {
        f->hover_id = row_id; /* menu_end moves focus here unless the keyboard holds it */
        f->hover_depth = depth;
        f->hover_opens = has_sub;
    }
    if (depth == menu->focus_depth) {
        menu_nav_track(f, menu->focus, row_id, enabled);
    }
    /* One row per level: the focused one on its level, the open parent on every other level. */
    const bool highlit = enabled && (row_id == menu->focus || (has_sub && row_id == menu->open_path[depth] && (depth != menu->focus_depth || menu->focus == 0U)));

    const nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = highlit ? 1.0F : 0.0F};
    /* A row's highlight ease outlives its closed level, so a row shown again snaps it. */
    const bool shown_again = style->state_speed > 0.0F && (menu->arming || !nt_ui_get_bbox(ctx, row_id).found);
    const nt_ui_anim_interaction_t *a = shown_again ? nt_ui_anim_seed(ctx, row_id, &tgt) : nt_ui_anim(ctx, row_id, &tgt, 0.0F, style->state_speed);
    Clay_Color bg = {0};
    if (style->item_hover_color != 0U) {
        bg = nt_ui_unpack_abgr(style->item_hover_color);
        bg.a = bg.a * a->value_t;
    }
    const uint32_t txt = enabled ? style->text_color : style->text_disabled;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = txt};
    const Clay_ElementDeclaration row = {
        .id = (Clay_ElementId){.id = row_id},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->item_height)},
                   .padding = {.left = style->pad, .right = style->pad},
                   .childGap = style->pad,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = bg,
        .userData = (void *)nt_ui_make_element_data(f->fill_layer, NULL),
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(row);
    if (!open_element) {
        menu_declare_icon(f->fill_layer, menu_cell_id(row_id, NT_UI_MENU_KIND_ICON), &opts->icon, style);
        nt_ui_label(ctx, nt_ui_make_element_data(f->label_layer, NULL), label != NULL ? label : "", &lbl);
        /* One GROW spacer right-aligns the trailing cells (shortcut / checkmark / submenu marker). */
        if (opts->shortcut != NULL || opts->selected || has_sub) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        }
        if (opts->shortcut != NULL) {
            menu_declare_shortcut(ctx, f->label_layer, menu_cell_id(row_id, NT_UI_MENU_KIND_SHORTCUT), opts->shortcut, style);
        }
        if (opts->selected) {
            menu_declare_mark(f, menu_cell_id(row_id, NT_UI_MENU_KIND_CHECK), &style->checkmark, style->check_tint, style->check_size, "\xE2\x9C\x93", &lbl);
        }
        if (has_sub) {
            menu_declare_mark(f, menu_cell_id(row_id, NT_UI_MENU_KIND_ARROW), &style->arrow, style->arrow_tint, style->arrow_size, ">", &lbl);
        }
        nt_ui_clay_priv_close_element();
    }
    return row_id;
}

/* Open a level's PANEL element (art-or-flat slice9) and LEAVE it open for the level's item children. The
 * caller (menu_begin / submenu_begin) balances it with nt_ui_clay_priv_close_element. */
static void menu_open_panel(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t menu_id, uint8_t depth, nt_ui_menu_style_t *style) {
    nt_atlas_resolve_ref(&style->panel_bg);
    const bool panel_art = (style->panel_bg.atlas.id != 0U && style->panel_bg.region != NT_ATLAS_INVALID_REGION);
    const uint32_t panel_id = menu_panel_id(menu_id, depth);
    Clay_ElementDeclaration panel = {.id = (Clay_ElementId){.id = panel_id},
                                     .layout = {.sizing = {.width = CLAY_SIZING_FIT(.min = (float)style->min_width), .height = CLAY_SIZING_FIT(0)},
                                                .padding = CLAY_PADDING_ALL(style->pad),
                                                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                .childGap = 2},
                                     .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)};
    if (panel_art) {
        nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
        *p = (nt_ui_image_payload_t){.atlas = style->panel_bg.atlas, .region_index = style->panel_bg.region, .slice9_scale = style->slice9_scale};
        panel.image = (Clay_ImageElementConfig){.imageData = p};
        panel.backgroundColor = nt_ui_unpack_tint(style->panel_tint);
    } else {
        panel.backgroundColor = nt_ui_unpack_abgr(style->bg_color);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel);
    nt_ui_block_pointer(ctx, panel_id, NULL); /* the rows, declared later, win; nothing under a panel is hit */
}

/* Open a level's popup at `anchor` and its panel. The root's catcher keeps the pointer off the UI under the
 * menu and claims the modal top (nt_ui_overlay_active, Esc); the menu owns its outside-press dismiss. Levels
 * snap open: a closed level declares nothing, so there is no close to tween. */
static void menu_open_level(const nt_ui_menu_t *menu, uint8_t depth, const nt_ui_popup_anchor_t *anchor) {
    const nt_ui_menu_frame_t *f = &menu->frame;
    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = 0.0F;
    pst.layer = f->fill_layer;
    const nt_ui_popup_ext_t ext = {.def = &NT_UI_MENU_DEF, .force_catcher = (depth == 0U)};
    bool level_open = true; /* a declared level is open: the menu closes by not declaring it */
    (void)nt_ui_popup_begin_internal(f->ui, menu_level_id(f->menu_id, depth), &pst, anchor, &level_open, &ext, NULL, NULL);
    menu_open_panel(f->ui, f->fill_layer, f->menu_id, depth, f->style); /* LEFT open for the level's rows */
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the leading validation assert chain inflates the count; control flow is flat (one open/closed branch)
bool nt_ui_menu_begin(nt_ui_menu_t *menu, nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t menu_id, nt_ui_menu_style_t *style) {
    NT_ASSERT(menu != NULL && "nt_ui_menu_begin: menu must be non-NULL");
    NT_ASSERT(ctx != NULL && "nt_ui_menu_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_begin: call between nt_ui_begin/end");
    NT_ASSERT(menu_id != 0U && style != NULL && "nt_ui_menu_begin: menu_id non-zero, style non-NULL");
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_menu_begin: style->font_size must be > 0");
    NT_ASSERT(menu->frame.ui == NULL && "nt_ui_menu_begin: the previous nt_ui_menu_begin has no nt_ui_menu_end");

    if (!menu->open) {
        return false;
    }
    uint8_t key = MENU_KEY_NONE;
    if (!menu_frame_input(menu, ctx, menu_id, &key)) {
        menu->open = false;
        return false;
    }
    menu->frame = (nt_ui_menu_frame_t){
        .ui = ctx,
        .style = style,
        .menu_id = menu_id,
        .key_row = menu->focus,
        .key = key,
        .fill_layer = (data != NULL) ? data->layer : 0U,
        .label_layer = label_layer,
        .top = !menu->arming && ctx->overlay_top_id_prev == menu_level_id(menu_id, 0U),
    };

    /* BELOW flips only vertically: shift the root left by last frame's width to stay on screen (unclamped on
     * the first frame). */
    float root_x = menu->anchor_x;
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
    const nt_ui_popup_anchor_t root_anchor = {.x = root_x, .y = menu->anchor_y, .w = 0.0F, .h = 0.0F, .prefer_side = NT_UI_POPUP_BELOW};
    menu_open_level(menu, 0U, &root_anchor); /* the root panel stays open until menu_end */
    return true;
}

/* The row's one mutating step. A menu that is not on top (its opening frame, whose hits are last frame's at
 * the old place, or under another overlay) only registers the row: its press is not captured and its
 * release clicks nothing. */
static bool menu_step_clicked(const nt_ui_menu_t *menu, uint32_t row_id) {
    if (!menu->frame.top) {
        nt_ui_block_pointer(menu->frame.ui, row_id, NULL);
        return false;
    }
    return nt_ui_step(menu->frame.ui, row_id, NULL).clicked;
}

/* A row activates on its click or on Enter while focused; a frame activates one row, the first declared (a
 * tap and Enter can land on two). */
static bool menu_activate(nt_ui_menu_t *menu, uint32_t row_id, bool clicked) {
    if (menu->frame.activated || !(clicked || menu_key_on(menu, row_id, MENU_KEY_ENTER))) {
        return false;
    }
    menu->frame.activated = true;
    return true;
}

bool nt_ui_menu_item_ex(nt_ui_menu_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_item_ex: call between nt_ui_menu_begin/end");
    const uint32_t row_id = menu_im_row(menu, key, label, &opts, false, false);
    if (opts.disabled) {
        return false;
    }
    return menu_activate(menu, row_id, menu_step_clicked(menu, row_id));
}

bool nt_ui_menu_item(nt_ui_menu_t *menu, uint32_t key, const char *label) { return nt_ui_menu_item_ex(menu, key, label, (nt_ui_menu_item_opts_t){0}); }

void nt_ui_menu_item_begin(nt_ui_menu_t *menu, uint32_t key, bool enabled, bool activatable) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_item_begin: call between nt_ui_menu_begin/end");
    const nt_ui_menu_item_opts_t opts = {.disabled = !enabled};
    menu->frame.item_id = menu_im_row(menu, key, NULL, &opts, false, true);
    menu->frame.item_enabled = enabled;
    menu->frame.item_activatable = activatable;
}

bool nt_ui_menu_item_end(nt_ui_menu_t *menu) {
    nt_ui_menu_frame_t *f = &menu->frame;
    const uint32_t row_id = f->item_id;
    NT_ASSERT(row_id != 0U && "nt_ui_menu_item_end without nt_ui_menu_item_begin");
    f->item_id = 0U;
    nt_ui_clay_priv_close_element();
    /* A disabled row never steps/activates — mirror item_ex's enabled gate (a disabled custom row must not
     * capture the pointer nor fire on Enter). */
    if (!f->item_enabled) {
        return false;
    }
    if (!f->item_activatable) {
        /* No action: block instead of step, so the row is hovered within but a press beside the child never
         * captures. The child, declared later, still wins its own area. */
        nt_ui_block_pointer(f->ui, row_id, NULL);
        return false;
    }
    return menu_activate(menu, row_id, menu_step_clicked(menu, row_id));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the leading validation assert chain inflates the count; control flow is flat
bool nt_ui_menu_submenu_begin_ex(nt_ui_menu_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_submenu_begin_ex: call between nt_ui_menu_begin/end");
    nt_ui_menu_frame_t *f = &menu->frame;
    const uint8_t depth = f->depth;
    NT_ASSERT((depth + 1U) < NT_UI_MENU_MAX_DEPTH && "nt_ui_menu: submenu nesting exceeds NT_UI_MENU_MAX_DEPTH");
    NT_ASSERT(!opts.selected && "nt_ui_menu_submenu_begin_ex: a parent row has no checkmark");
    const uint32_t row_id = menu_im_row(menu, key, label, &opts, true, false);
    if (opts.disabled) {
        return false; /* a disabled parent never steps, never flies out, never opens */
    }
    if (menu_step_clicked(menu, row_id)) {
        f->click_id = row_id; /* committed in menu_end, over the aim corridor */
        f->click_depth = depth;
    }
    if (menu_key_on(menu, row_id, MENU_KEY_RIGHT) || menu_key_on(menu, row_id, MENU_KEY_ENTER)) {
        menu_set_open(menu, depth, row_id); /* the key closed any sibling's child at begin */
        f->focus_first = true;              /* the child's first enabled row takes focus */
    }
    if (menu->open_path[depth] != row_id) {
        return false; /* this submenu is not the open one — game skips its body */
    }

    /* Open: a new level scoped to THIS row's id (ImGui PushID) flies its panel out. */
    const uint8_t child = (uint8_t)(depth + 1U);
    f->depth = child;
    f->declared_depth = child;
    const nt_ui_popup_anchor_t sub_anchor = menu_submenu_anchor(f->ui, row_id);
    menu_open_level(menu, child, &sub_anchor); /* closed in submenu_end */
    return true;
}

bool nt_ui_menu_submenu_begin(nt_ui_menu_t *menu, uint32_t key, const char *label) { return nt_ui_menu_submenu_begin_ex(menu, key, label, (nt_ui_menu_item_opts_t){0}); }

void nt_ui_menu_submenu_end(nt_ui_menu_t *menu) {
    NT_ASSERT(menu->frame.ui != NULL && menu->frame.depth > 0U && "nt_ui_menu_submenu_end without an open submenu");
    NT_ASSERT(menu->frame.item_id == 0U && "nt_ui_menu_submenu_end with an open custom row (missing nt_ui_menu_item_end)");
    nt_ui_clay_priv_close_element(); /* the child panel */
    nt_ui_popup_end(menu->frame.ui); /* balance the level's popup_begin */
    menu->frame.depth = (uint8_t)(menu->frame.depth - 1U);
}

void nt_ui_menu_separator(nt_ui_menu_t *menu) {
    NT_ASSERT(menu->frame.ui != NULL && menu->frame.item_id == 0U && "nt_ui_menu_separator: call between nt_ui_menu_begin/end, outside a custom row");
    menu_declare_separator(menu->frame.fill_layer, menu->frame.style); /* no id: nav skips it */
}

void nt_ui_menu_separator_text(nt_ui_menu_t *menu, const char *label) {
    NT_ASSERT(menu->frame.ui != NULL && menu->frame.item_id == 0U && "nt_ui_menu_separator_text: call between nt_ui_menu_begin/end, outside a custom row");
    const nt_ui_menu_frame_t *f = &menu->frame;
    const nt_ui_menu_style_t *style = f->style;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = style->separator_color};
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.left = style->pad, .right = style->pad}}, .userData = (void *)nt_ui_make_element_data(f->fill_layer, NULL)}) {
        nt_ui_label(f->ui, nt_ui_make_element_data(f->label_layer, NULL), label != NULL ? label : "", &lbl);
    }
}

/* The pointer's row `row_id` on level `d` switches that level's open child: a parent opens its own, a leaf
 * collapses it. While the cursor aims at (or sits over) the open child, the mouse-aim corridor KEEPS it, so a
 * diagonal toward the child never collapses it. */
static void menu_resolve_hover(nt_ui_menu_t *menu, uint8_t d, uint32_t row_id, bool opens, bool ours, float mx, float my, float dt) {
    const nt_ui_menu_frame_t *f = &menu->frame;
    const uint32_t cur = menu->open_path[d];
    const uint32_t want = opens ? row_id : 0U;
    if (want == cur) {
        menu->aim.depth = 0U; /* back on the open parent: leaving it again primes a fresh corridor */
        return;
    }
    if (cur != 0U) {
        const uint8_t child = (uint8_t)(d + 1U);
        const nt_ui_bbox_t cbb = nt_ui_get_bbox(f->ui, menu_level_id(f->menu_id, child));
        if (!cbb.found || menu_hover_intent(&menu->aim, child, ours, mx, my, cbb.x, cbb.y, cbb.width, cbb.height, dt)) {
            return; /* the corridor holds, or the child opened last frame and has no rect yet */
        }
    }
    menu_set_open(menu, d, want);
}

/* After the declaration, only what needs this frame's rows: close open parents the frame did not declare,
 * step Up/Down over the focused level's neighbours, then the pointer (a click, else the hover through the
 * aim corridor). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat key/mouse branches, not deep nesting
static void menu_resolve(nt_ui_menu_t *menu) {
    const nt_ui_menu_frame_t *f = &menu->frame;
    nt_ui_context_t *ctx = f->ui;
    // #region reconcile the kept state with this frame's declaration, before any input
    if (f->declared_depth < menu_open_depth(menu)) {
        menu_set_open(menu, f->declared_depth, 0U); /* a removed, disabled or leaf parent closes its branch */
    }
    if (!f->nav_passed) {
        menu->focus = 0U; /* the focused row is gone from its level; a disabled one still declares and stays */
    }
    // #endregion
    /* Up/Down step over the focused row's level, which the rows tracked. */
    const uint8_t d = menu->focus_depth;
    uint32_t step = 0U;
    if (f->key == MENU_KEY_DOWN) {
        step = (f->nav_next != 0U) ? f->nav_next : f->nav_first;
    } else if (f->key == MENU_KEY_UP) {
        step = (f->nav_prev != 0U) ? f->nav_prev : f->nav_last;
    }
    if (step != 0U) {
        menu->focus = step;
        if (menu->open_path[d] != step) {
            menu_set_open(menu, d, 0U); /* focus leaving the open parent closes its child */
        }
    }
    const nt_pointer_t *pp = &ctx->frame_pointers[ctx->frame_primary];
    const bool ours = f->top && pp->active && !nt_ui_inspector_pointer_consumed(ctx);
    const float mx = pp->x;
    const float my = pp->y;
    if (!menu->kbd_mode && f->hover_id != 0U) {
        menu->focus = f->hover_id;
        menu->focus_depth = f->hover_depth;
    }
    /* A click opens its parent over a holding corridor, after the key; a same-frame Up/Down may have closed
     * its level. */
    if (f->click_id != 0U && f->click_depth <= menu_open_depth(menu)) {
        menu_set_open(menu, f->click_depth, f->click_id);
        menu->focus = f->click_id; /* focus follows the parent it opens */
        menu->focus_depth = f->click_depth;
    } else if (!menu->kbd_mode && f->hover_id != 0U) {
        menu_resolve_hover(menu, f->hover_depth, f->hover_id, f->hover_opens, ours, mx, my, ctx->frame_dt);
    } else if (f->top && !menu->kbd_mode && f->focus_seen && menu->open_path[menu->focus_depth] != 0U && menu->open_path[menu->focus_depth] != menu->focus) {
        /* Off the rows, the switch the pointer left on a sibling keeps running, so the frame never freezes
         * with a sibling's child open; under another overlay it waits, as the pointer is that overlay's. */
        menu_resolve_hover(menu, menu->focus_depth, menu->focus, f->focus_opens, ours, mx, my, ctx->frame_dt);
    }
}

void nt_ui_menu_end(nt_ui_menu_t *menu) {
    nt_ui_context_t *ctx = menu->frame.ui;
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_end: call between nt_ui_begin/end after nt_ui_menu_begin");
    NT_ASSERT(menu->frame.depth == 0U && "nt_ui_menu_end with an open submenu (missing nt_ui_menu_submenu_end)");
    NT_ASSERT(menu->frame.item_id == 0U && "nt_ui_menu_end with an open custom row (missing nt_ui_menu_item_end)");
    nt_ui_clay_priv_close_element(); /* the root panel */
    menu_resolve(menu);
    nt_ui_popup_end(ctx); /* balance menu_begin's root popup_begin */
    menu->arming = false;
    if (menu->frame.activated) {
        menu->open = false; /* nt_ui_menu_open starts the next navigation fresh */
    }
    menu->frame.ui = NULL; /* clear the span handle: a between-frames inner call now traps on a NULL ctx */
}
// #endregion

#ifdef NT_TEST_ACCESS
float nt_ui_menu_test_aim_near_x(float sub_x, float sub_w, float apex_x) { return menu_aim_near_x(sub_x, sub_w, apex_x); }

bool nt_ui_menu_test_hover_intent(nt_ui_menu_aim_t *aim, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, float dt) {
    return menu_hover_intent(aim, 1U, true, mouse_x, mouse_y, sub_x, sub_y, sub_w, sub_h, dt);
}

uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key) { return menu_item_id(scope_id, key); }
uint32_t nt_ui_menu_test_panel_id(uint32_t menu_id, uint8_t depth) { return menu_panel_id(menu_id, depth); }
uint32_t nt_ui_menu_test_arrow_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_ARROW); }
uint32_t nt_ui_menu_test_icon_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_ICON); }
uint32_t nt_ui_menu_test_shortcut_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_SHORTCUT); }
uint32_t nt_ui_menu_test_check_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_CHECK); }
#endif
