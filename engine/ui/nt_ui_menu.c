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

const nt_ui_widget_def_t NT_UI_MENU_DEF = {
    .name = "nt_menu",
    .pill_color = 0xFF40C080U,
    ._reserved = 0U,
};

/* Kind tags folded into the id hash so a level/panel/occluder/row cell never collides with another.
 * nt_ui_derived_id (XOR) cannot be used to compose depth+kind because additive salts with a shared
 * stride XOR-cancel across (depth,kind) pairs. */
enum {
    NT_UI_MENU_KIND_LEVEL = 1,
    NT_UI_MENU_KIND_PANEL = 2,
    NT_UI_MENU_KIND_OCCLUDER = 5,
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
static inline uint32_t menu_occluder_id(uint32_t menu_id) { return menu_hash_id(menu_id, NT_UI_MENU_KIND_OCCLUDER, 0U); }
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

/* Aim-triangle near corners for an open submenu rect (the two base corners only; the apex is the stable
 * primed apex): the submenu edge that faces the apex, so a submenu edge-flipped to the LEFT of its parent
 * aims at its RIGHT edge. */
static void menu_aim_corners(float sub_x, float sub_y, float sub_w, float sub_h, float apex_x, float *bx, float *by, float *cx, float *cy) {
    const float near_x = (apex_x > sub_x + (sub_w * 0.5F)) ? (sub_x + sub_w) : sub_x;
    *bx = near_x;
    *by = sub_y;
    *cx = near_x;
    *cy = sub_y + sub_h;
}

/* true KEEPS the open child `child`. The apex stays fixed while the corridor lives: a per-frame apex ~1px
 * from the cursor makes a degenerate wedge that never releases. */
static bool menu_hover_intent(nt_ui_menu_aim_t *c, uint8_t child, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, float dt) {
    if (c->depth != child) {
        c->apex_x = mouse_x;
        c->apex_y = mouse_y;
        c->timer = 0.0F;
        c->depth = child;
        return true;
    }
    float bx = 0.0F;
    float by = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    menu_aim_corners(sub_x, sub_y, sub_w, sub_h, c->apex_x, &bx, &by, &cx, &cy);
    /* Over the child: keep and reset the timer. In the corridor: keep for at most 2x AIM_FALLBACK, so a parked
     * cursor still releases. Elsewhere: switch after AIM_FALLBACK. */
    const bool over_sub = (mouse_x >= sub_x) && (mouse_x <= sub_x + sub_w) && (mouse_y >= sub_y) && (mouse_y <= sub_y + sub_h);
    const bool in_corridor = menu_point_in_tri(mouse_x, mouse_y, c->apex_x, c->apex_y, bx, by, cx, cy);
    bool keep = true;
    if (over_sub) {
        c->timer = 0.0F;
    } else if (in_corridor && (c->timer < NT_UI_MENU_AIM_FALLBACK_SECS * 2.0F)) {
        c->timer += dt;
        keep = true;
    } else {
        c->timer += dt;
        keep = (c->timer < NT_UI_MENU_AIM_FALLBACK_SECS);
    }
    return keep;
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
        .font_id = 0U,
        .icon_size = 0U,        /* no icon gutter by default (text-only) */
        .arrow_size = 14U,      /* used only when the arrow ref is set */
        .check_size = 14U,      /* used only when the checkmark ref is set (mirror arrow_size) */
        .separator_height = 2U, /* NULL-label divider thickness */
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
    menu->anchor_x = x;
    menu->anchor_y = y;
    menu->open = true;
    menu->arming = true;
    menu->focus = 0U;
    menu->focus_depth = 0U;
    memset(menu->open_path, 0, sizeof menu->open_path); /* aim may stay stale: menu_set_open un-primes it before any read */
    menu->kbd_mode = false;
}

// #region menu UI declaration (shared row/marker/panel helpers fed by the immediate path)
/* A separator: a thin non-interactive divider (no row id), indented by the panel pad. */
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
    nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
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
        nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
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
        nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
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

/* True when the cursor is inside ANY panel shown last frame. A click inside a panel is a row interaction
 * (select/open/switch) and must NOT dismiss; a click outside all of them is an outside-click. Uses the
 * prev-frame bbox, so a level closed at the end of last frame still counts; an absent panel is skipped. */
static bool menu_cursor_over_any_panel(const nt_ui_context_t *ctx, uint32_t menu_id, float mx, float my) {
    for (uint8_t d = 0; d < NT_UI_MENU_MAX_DEPTH; ++d) {
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
    const int16_t occ_z = (int16_t)(ctx->modal_zband_stride - 1); /* one below the menu's own panel band, relative to whatever encloses the menu */
    const nt_ui_transform_t id_xf = nt_ui_transform_defaults();
    const nt_ui_element_data_t *occ_data = nt_ui_make_element_data_xform(fill_layer, NULL, &id_xf, 1.0F);
    CLAY({.id = (Clay_ElementId){.id = occ_id},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = occ_z},
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
          .userData = (void *)occ_data}) {}
    nt_ui_block_pointer(ctx, occ_id, NULL);
}

// #region immediate-mode menu (begin/item/submenu/separator/end)
/* Rows read the navigation as they declare: Enter and Right act in the row call, Up/Down collect the
 * focused row's neighbours, the pointer records one hover and one click candidate; menu_end resolves the
 * rest. */

/* Enter, or Right on a parent, acts on the focused row; the navigated level is the focused row's level. */
static bool menu_key_target(const nt_ui_menu_t *menu, uint32_t row_id) { return menu->frame.depth == menu->frame.nav_depth && row_id == menu->focus; }

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
static nt_ui_interaction_t menu_im_row(nt_ui_menu_t *menu, uint32_t key, const char *label, const nt_ui_menu_item_opts_t *opts, bool has_sub, bool open_element, uint32_t *out_id) {
    nt_ui_menu_frame_t *f = &menu->frame;
    NT_ASSERT(f->item_id == 0U && "nt_ui_menu: a row inside a custom row (missing nt_ui_menu_item_end)");
    nt_ui_context_t *ctx = f->ui;
    const uint8_t depth = f->depth;
    const uint32_t scope = (depth == 0U) ? f->menu_id : menu->open_path[depth - 1U];
    const uint32_t row_id = menu_item_id(scope, key);
    *out_id = row_id;
    nt_ui_menu_style_t *style = f->style;
    const bool enabled = !opts->disabled;

    const nt_ui_interaction_t in = enabled ? nt_ui_query_interaction(ctx, row_id) : (nt_ui_interaction_t){0};
    /* A custom row stays under the pointer over its own interactive child (hot, declared later), so
     * focus, highlight and the leaf collapse follow the row the pointer is in. */
    const bool pointed = enabled && nt_ui_hovered_within(ctx, row_id);
    if (enabled && f->focus_first && depth == f->nav_depth + 1U) {
        menu->focus = row_id;
        menu->focus_depth = depth;
        f->focus_first = false;
    }
    if (pointed) {
        f->hover_id = row_id; /* menu_end moves focus here unless the keyboard holds it */
        f->hover_depth = depth;
        f->hover_opens = has_sub;
    }
    if (depth == f->nav_depth) {
        menu_nav_track(f, menu->focus, row_id, enabled);
    }
    /* An open parent keeps its highlight while focus moves into its child. */
    const bool highlit = (pointed && !menu->kbd_mode) || (enabled && (row_id == menu->focus || (has_sub && row_id == menu->open_path[depth])));

    const nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = highlit ? 1.0F : 0.0F};
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, row_id, &tgt, 0.0F, style->state_speed);
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
        menu_declare_icon(ctx, f->fill_layer, menu_cell_id(row_id, NT_UI_MENU_KIND_ICON), &opts->icon, style);
        nt_ui_label(ctx, nt_ui_make_element_data(f->label_layer, NULL), label != NULL ? label : "", &lbl);
        /* One GROW spacer right-aligns the trailing cells (shortcut / checkmark / submenu marker). */
        if (opts->shortcut != NULL || opts->selected || has_sub) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        }
        if (opts->shortcut != NULL) {
            menu_declare_shortcut(ctx, f->label_layer, menu_cell_id(row_id, NT_UI_MENU_KIND_SHORTCUT), opts->shortcut, style);
        }
        if (opts->selected) {
            menu_declare_check(ctx, f->fill_layer, f->label_layer, menu_cell_id(row_id, NT_UI_MENU_KIND_CHECK), &lbl, style);
        }
        if (has_sub) {
            menu_declare_marker(ctx, f->fill_layer, f->label_layer, menu_cell_id(row_id, NT_UI_MENU_KIND_ARROW), &lbl, style);
        }
        nt_ui_clay_priv_close_element();
    }
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
        nt_ui_image_payload_t *p = NT_ARENA_ALLOC(&g_nt_mem_scratch, nt_ui_image_payload_t);
        *p = (nt_ui_image_payload_t){.atlas = style->panel_bg.atlas, .region_index = style->panel_bg.region, .slice9_scale = style->slice9_scale};
        panel.image = (Clay_ImageElementConfig){.imageData = p};
        panel.backgroundColor = nt_ui_unpack_tint(style->panel_tint);
    } else {
        panel.backgroundColor = nt_ui_unpack_abgr(style->bg_color);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel);
}

/* Open a level's popup at `anchor` and its panel (no per-level light-dismiss catcher — the menu owns its
 * own outside-click dismiss). Levels snap open: a closed level declares nothing, so there is no close frame
 * to tween. */
static void menu_open_level(const nt_ui_menu_t *menu, uint8_t depth, const nt_ui_popup_anchor_t *anchor) {
    const nt_ui_menu_frame_t *f = &menu->frame;
    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = 0.0F;
    pst.layer = f->fill_layer;
    pst.flags &= (uint8_t)~NT_UI_POPUP_LIGHT_DISMISS;
    (void)nt_ui_popup_begin(f->ui, menu_level_id(f->menu_id, depth), &pst, anchor, true);
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
    const uint8_t open_depth = menu_open_depth(menu);
    if (menu->focus_depth > open_depth) {
        menu->focus_depth = open_depth; /* the focused row's level closed: keys act on the deepest open one */
    }
    menu->frame = (nt_ui_menu_frame_t){
        .ui = ctx,
        .style = style,
        .menu_id = menu_id,
        .nav_depth = menu->focus_depth,
        .fill_layer = (data != NULL) ? data->layer : 0U, /* fills on data->layer, text on label_layer (game-controlled render order) */
        .label_layer = label_layer,
    };

    menu_declare_occluder(ctx, menu->frame.fill_layer, menu_id); /* single root occluder under the whole stack */

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

bool nt_ui_menu_item_ex(nt_ui_menu_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_item_ex: call between nt_ui_menu_begin/end");
    uint32_t row_id = 0U;
    const nt_ui_interaction_t in = menu_im_row(menu, key, label, &opts, false, false, &row_id);
    if (opts.disabled) {
        return false;
    }
    (void)nt_ui_step(menu->frame.ui, row_id, NULL);
    /* A frame activates one row, the first declared (a tap and Enter can land on two). */
    const bool activated = !menu->frame.activated && (in.clicked || (menu_key_target(menu, row_id) && nt_input_key_is_pressed(NT_KEY_ENTER)));
    if (activated) {
        menu->frame.activated = true;
    }
    return activated;
}

bool nt_ui_menu_item(nt_ui_menu_t *menu, uint32_t key, const char *label) { return nt_ui_menu_item_ex(menu, key, label, (nt_ui_menu_item_opts_t){0}); }

void nt_ui_menu_item_begin(nt_ui_menu_t *menu, uint32_t key, bool enabled, bool activatable) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_item_begin: call between nt_ui_menu_begin/end");
    const nt_ui_menu_item_opts_t opts = {.disabled = !enabled};
    (void)menu_im_row(menu, key, NULL, &opts, false, true, &menu->frame.item_id);
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
        /* No action: block instead of step, so the row is hot for its hover (menu_im_row's read-only query)
         * but a press beside the child never captures. The child, declared later, still wins its own area. */
        nt_ui_block_pointer(f->ui, row_id, NULL);
        return false;
    }
    const nt_ui_interaction_t in = nt_ui_step(f->ui, row_id, NULL); /* the ONE mutating step per id */
    const bool activated = !f->activated && (in.clicked || (menu_key_target(menu, row_id) && nt_input_key_is_pressed(NT_KEY_ENTER)));
    if (activated) {
        f->activated = true;
    }
    return activated;
}

/* Right or Enter opens the focused parent if needed and moves focus to its first enabled row. A sibling's
 * child may already be declared this frame (one level id per depth): then the parent opens in menu_end and
 * focus stays on it. */
static void menu_key_open(nt_ui_menu_t *menu, uint8_t depth, uint32_t row_id) {
    nt_ui_menu_frame_t *f = &menu->frame;
    if (menu->open_path[depth] == 0U || menu->open_path[depth] == row_id) {
        menu_set_open(menu, depth, row_id);
        f->focus_first = true;
    } else {
        f->click_id = row_id;
        f->click_depth = depth;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the leading validation assert chain inflates the count; control flow is flat
bool nt_ui_menu_submenu_begin_ex(nt_ui_menu_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_submenu_begin_ex: call between nt_ui_menu_begin/end");
    nt_ui_menu_frame_t *f = &menu->frame;
    const uint8_t depth = f->depth;
    NT_ASSERT((depth + 1U) < NT_UI_MENU_MAX_DEPTH && "nt_ui_menu: submenu nesting exceeds NT_UI_MENU_MAX_DEPTH");
    NT_ASSERT(!opts.selected && "nt_ui_menu_submenu_begin_ex: a parent row has no checkmark");
    uint32_t row_id = 0U;
    const nt_ui_interaction_t in = menu_im_row(menu, key, label, &opts, true, false, &row_id);
    if (opts.disabled) {
        return false; /* a disabled parent never steps, never flies out, never opens */
    }
    (void)nt_ui_step(f->ui, row_id, NULL);
    if (in.clicked) {
        f->click_id = row_id; /* committed in menu_end, over the aim corridor */
        f->click_depth = depth;
    }
    if (menu_key_target(menu, row_id) && (nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) || nt_input_key_is_pressed(NT_KEY_ENTER))) {
        menu_key_open(menu, depth, row_id);
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
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_separator: call between nt_ui_menu_begin/end");
    menu_declare_separator(menu->frame.ui, menu->frame.fill_layer, menu->frame.style); /* no id: nav skips it */
}

void nt_ui_menu_separator_text(nt_ui_menu_t *menu, const char *label) {
    NT_ASSERT(menu->frame.ui != NULL && "nt_ui_menu_separator_text: call between nt_ui_menu_begin/end");
    const nt_ui_menu_frame_t *f = &menu->frame;
    const nt_ui_menu_style_t *style = f->style;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = style->separator_color};
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.left = style->pad, .right = style->pad}}, .userData = (void *)nt_ui_make_element_data(f->fill_layer, NULL)}) {
        nt_ui_label(f->ui, nt_ui_make_element_data(f->label_layer, NULL), label != NULL ? label : "", &lbl);
    }
}

/* The hovered row switches its level's open child: a parent opens its own, a leaf collapses it. While the
 * cursor aims at (or sits over) the open child, the mouse-aim corridor KEEPS it, so a diagonal toward the
 * child never collapses it. */
static void menu_resolve_hover(nt_ui_menu_t *menu, float mx, float my, float dt) {
    const nt_ui_menu_frame_t *f = &menu->frame;
    const uint8_t d = f->hover_depth;
    const uint32_t cur = menu->open_path[d];
    const uint32_t want = f->hover_opens ? f->hover_id : 0U;
    if (want == cur) {
        menu->aim.depth = 0U; /* back on the open parent: leaving it again primes a fresh corridor */
        return;
    }
    if (cur != 0U) {
        const uint8_t child = (uint8_t)(d + 1U);
        const nt_ui_bbox_t cbb = nt_ui_get_bbox(f->ui, menu_level_id(f->menu_id, child));
        if (cbb.found && menu_hover_intent(&menu->aim, child, mx, my, cbb.x, cbb.y, cbb.width, cbb.height, dt)) {
            return; /* corridor holds: keep aiming at the open child */
        }
    }
    menu_set_open(menu, d, want);
}

/* After the declaration: close open parents this frame did not declare, apply Up/Down/Left/Esc, then the
 * mouse (a click, else the hover through the aim corridor), then the outside-press dismiss. Returns true if
 * the whole chain closes (an activation, Esc/Left at the root, or an outside press). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat key/mouse branches, not deep nesting
static bool menu_resolve(nt_ui_menu_t *menu) {
    const nt_ui_menu_frame_t *f = &menu->frame;
    nt_ui_context_t *ctx = f->ui;
    if (f->declared_depth < menu_open_depth(menu)) {
        menu_set_open(menu, f->declared_depth, 0U); /* a removed, disabled or leaf parent closes its branch */
    }
    bool close_chain = f->activated;
    const bool up = nt_input_key_is_pressed(NT_KEY_ARROW_UP);
    const bool down = nt_input_key_is_pressed(NT_KEY_ARROW_DOWN);
    const bool back = nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) || nt_input_key_is_pressed(NT_KEY_ESCAPE);
    const bool kbd_acted = up || down || back || nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) || nt_input_key_is_pressed(NT_KEY_ENTER);
    /* The rows tracked the focused row's level; a keyboard move into a child or a lost level this frame
     * ignores Up/Down/Left/Esc. */
    const uint8_t d = f->nav_depth;
    if (d <= f->declared_depth && menu->focus_depth == d) {
        uint32_t step = 0U;
        if (down) {
            step = (f->nav_next != 0U) ? f->nav_next : f->nav_first;
        } else if (up) {
            step = (f->nav_prev != 0U) ? f->nav_prev : f->nav_last;
        }
        if (step != 0U) {
            menu->focus = step;
            if (menu->open_path[d] != step) {
                menu_set_open(menu, d, 0U); /* focus leaving the open parent closes its child */
            }
        }
        if (back && d == 0U) {
            close_chain = true;
        } else if (back) {
            menu->focus = menu->open_path[d - 1U]; /* back on the parent it closes */
            menu->focus_depth = (uint8_t)(d - 1U);
            menu_set_open(menu, (uint8_t)(d - 1U), 0U);
        }
    }
    /* A key hands focus and the open chain to the keyboard; the pointer takes them back when it moves or
     * presses (a tap does not move), so a pointer resting on a row neither steals focus nor reopens what the
     * keyboard closed. */
    const nt_pointer_t *pp = &ctx->frame_pointers[ctx->frame_primary];
    if (kbd_acted) {
        menu->kbd_mode = true;
    } else if (menu->kbd_mode && pp->active && (pp->dx != 0.0F || pp->dy != 0.0F || pp->buttons[NT_BUTTON_LEFT].is_pressed)) {
        menu->kbd_mode = false;
        menu->aim.depth = 0U; /* the corridor primed before the keys is stale */
    }
    const float mx = pp->x;
    const float my = pp->y;
    if (!menu->kbd_mode && f->hover_id != 0U) {
        menu->focus = f->hover_id;
        menu->focus_depth = f->hover_depth;
    }
    /* A click or a deferred key opens its parent over a holding corridor; its level may have closed by Left
     * this frame. */
    if (f->click_id != 0U && f->click_depth <= menu_open_depth(menu)) {
        menu_set_open(menu, f->click_depth, f->click_id);
    } else if (!menu->kbd_mode && f->hover_id != 0U) {
        menu_resolve_hover(menu, mx, my, ctx->frame_dt);
    }
    /* Menu-owned outside-press dismiss (a click or a tap); the frame the menu opened skips it so the opening
     * press does not self-dismiss. */
    const bool outside_press = pp->active && (pp->buttons[NT_BUTTON_LEFT].is_pressed || pp->buttons[NT_BUTTON_RIGHT].is_pressed);
    if (outside_press && !menu->arming && !menu_cursor_over_any_panel(ctx, f->menu_id, mx, my)) {
        close_chain = true;
    }
    return close_chain;
}

void nt_ui_menu_end(nt_ui_menu_t *menu) {
    nt_ui_context_t *ctx = menu->frame.ui;
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_menu_end: call between nt_ui_begin/end after nt_ui_menu_begin");
    NT_ASSERT(menu->frame.depth == 0U && "nt_ui_menu_end with an open submenu (missing nt_ui_menu_submenu_end)");
    NT_ASSERT(menu->frame.item_id == 0U && "nt_ui_menu_end with an open custom row (missing nt_ui_menu_item_end)");
    nt_ui_clay_priv_close_element(); /* the root panel */
    const bool close_chain = menu_resolve(menu);
    nt_ui_popup_end(ctx); /* balance menu_begin's root popup_begin */
    menu->arming = false;
    if (close_chain) {
        menu->open = false; /* nt_ui_menu_open starts the next navigation fresh */
    }
    menu->frame.ui = NULL; /* clear the span handle: a between-frames inner call now traps on a NULL ctx */
}
// #endregion

#ifdef NT_TEST_ACCESS
bool nt_ui_menu_test_point_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) { return menu_point_in_tri(px, py, ax, ay, bx, by, cx, cy); }

void nt_ui_menu_test_aim_corners(float sub_x, float sub_y, float sub_w, float sub_h, float apex_x, float *bx, float *by, float *cx, float *cy) {
    menu_aim_corners(sub_x, sub_y, sub_w, sub_h, apex_x, bx, by, cx, cy);
}

bool nt_ui_menu_test_hover_intent(nt_ui_menu_aim_t *aim, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, float dt) {
    return menu_hover_intent(aim, 1U, mouse_x, mouse_y, sub_x, sub_y, sub_w, sub_h, dt);
}

uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key) { return menu_item_id(scope_id, key); }
uint32_t nt_ui_menu_test_panel_id(uint32_t menu_id, uint8_t depth) { return menu_panel_id(menu_id, depth); }
uint32_t nt_ui_menu_test_occluder_id(uint32_t menu_id) { return menu_occluder_id(menu_id); }
uint32_t nt_ui_menu_test_arrow_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_ARROW); }
uint32_t nt_ui_menu_test_icon_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_ICON); }
uint32_t nt_ui_menu_test_shortcut_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_SHORTCUT); }
uint32_t nt_ui_menu_test_check_id(uint32_t row_id) { return menu_cell_id(row_id, NT_UI_MENU_KIND_CHECK); }
#endif
