#include "ui/nt_ui_menu.h"

#include <math.h>

#include <stdint.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_internal.h"
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

/* Per-open-submenu hover-intent retained state. prev_mouse seeds the aim triangle apex next frame;
 * switch_timer accumulates dwell OFF the triangle and crosses AIM_FALLBACK to allow a sibling switch. */
typedef struct {
    float prev_mouse_x, prev_mouse_y;
    float switch_timer;
    bool primed; /* false on a fresh cell -> prev_mouse not yet meaningful (keep this frame) */
    uint8_t _pad[3];
} nt_ui_menu_hover_t;

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

/* Per-frame decision: while the cursor sits inside {prev_mouse, near_top, near_bottom} the user is
 * aiming at the open submenu -> KEEP it open + reset the dwell timer. Otherwise accumulate dwell; once
 * it passes AIM_FALLBACK allow the hovered sibling to win. A fresh (unprimed) cell keeps this frame so
 * the very first frame after opening never instantly switches. */
static bool menu_hover_intent(nt_ui_menu_hover_t *c, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float dt) {
    float bx = 0.0F;
    float by = 0.0F;
    float cx = 0.0F;
    float cy = 0.0F;
    menu_aim_corners(sub_x, sub_y, sub_w, sub_h, side, &bx, &by, &cx, &cy);
    bool keep = true;
    if (c->primed) {
        /* Keep the submenu open if the cursor is parked OVER it (the common stationary case), else if it
         * is actively AIMING at it (moving inside the prev->near-corners triangle — Pitfall 3). The
         * triangle apex is the PREVIOUS cursor pos; a stationary cursor makes it degenerate (point ==
         * apex reports "inside" forever), so aim-keep requires actual travel past a small epsilon. A
         * cursor that is neither over the submenu nor aiming runs the dwell timer toward AIM_FALLBACK. */
        const bool over_sub = (mouse_x >= sub_x) && (mouse_x <= sub_x + sub_w) && (mouse_y >= sub_y) && (mouse_y <= sub_y + sub_h);
        const float mdx = mouse_x - c->prev_mouse_x;
        const float mdy = mouse_y - c->prev_mouse_y;
        const bool moved = ((mdx * mdx) + (mdy * mdy)) > 1.0F; /* > 1px travel */
        const bool aiming = moved && menu_point_in_tri(mouse_x, mouse_y, c->prev_mouse_x, c->prev_mouse_y, bx, by, cx, cy);
        if (over_sub || aiming) {
            c->switch_timer = 0.0F;
            keep = true;
        } else {
            c->switch_timer += dt;
            keep = (c->switch_timer < NT_UI_MENU_AIM_FALLBACK_SECS);
        }
    }
    c->prev_mouse_x = mouse_x;
    c->prev_mouse_y = mouse_y;
    c->primed = true;
    return keep;
}
// #endregion

nt_ui_menu_style_t nt_ui_menu_style_defaults(void) {
    return (nt_ui_menu_style_t){
        .bg_color = 0xF02A2A2AU,
        .item_hover_color = 0xFF4A6A8AU,
        .text_color = 0xFFEEEEEEU,
        .text_disabled = 0xFF888888U,
        .item_height = 26U,
        .min_width = 160U,
        .pad = 6U,
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

bool nt_ui_menu_open_trigger(nt_ui_context_t *ctx, uint32_t id, nt_ui_menu_state_t *st, float long_press_secs) {
    /* Task-2 fills the full right-click / long-press arming; Task-1 keeps the contract + asserts. */
    NT_ASSERT(ctx != NULL && id != 0U && st != NULL);
    (void)long_press_secs;
    return false;
}

void nt_ui_menu(nt_ui_context_t *ctx, uint32_t id, const nt_ui_menu_item_t *items, uint32_t count, nt_ui_menu_state_t *st, const nt_ui_menu_style_t *style) {
    /* Task-2 fills the recursive popup-core declaration; Task-1 keeps the contract + tree validation. */
    NT_ASSERT(ctx != NULL && id != 0U && st != NULL && style != NULL);
    menu_assert_items(items, count);
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
#endif
