#include "ui/nt_ui_tooltip.h"

#include <math.h>
#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "sprite_comp/nt_sprite_comp.h" /* NT_SPRITE_FLAG_FLIP_Y */
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_TOOLTIP_DEF = {
    .name = "nt_tooltip",
    .pill_color = 0xFF80C0FFU,
    ._reserved = 0U,
};

/* Sibling cell ids derived from the target id: the hover-delay timer + the popup panel each need a
 * stable non-aliasing id. Sparse high-bit salts (never a real low id), distinct from the target so the
 * timer cell never aliases the target's own state. */
#define NT_UI_TOOLTIP_TIMER_SALT 0x70900000U
#define NT_UI_TOOLTIP_POPUP_SALT 0x70910000U
#define NT_UI_TOOLTIP_PANEL_SALT 0x70920000U

static inline uint32_t tooltip_timer_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_TOOLTIP_TIMER_SALT); }
static inline uint32_t tooltip_popup_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_TOOLTIP_POPUP_SALT); }
static inline uint32_t tooltip_panel_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_TOOLTIP_PANEL_SALT); }

/* The engine-owned hover-delay cell (not a game bool). Zeroed on create -> hover starts at 0. */
typedef struct {
    float hover; /* accumulated seconds the cursor has been over the target */
} nt_ui_tooltip_cell_t;

#ifdef NT_TEST_ACCESS
static uint8_t s_last_side;              /* popup side chosen this frame (edge-flip + caret-flip probe) */
static bool s_last_caret_present;        /* did the last shown tooltip declare a caret? */
static uint8_t s_last_caret_flip;        /* caret flip_bits the last shown tooltip used */
static bool s_last_panel_art;            /* did the last shown panel use slice9 art? */
static float s_last_panel_corner_radius; /* the cornerRadius the last shown panel applied (0 under art) */
#endif

nt_ui_tooltip_style_t nt_ui_tooltip_style_defaults(void) {
    /* Polished flat baseline: no atlas art (panel_art / caret atlas.id stay 0), no border/shadow. Wire
     * panel_art + caret + border/shadow to opt into the sprite look (parity with dropdown/menu). */
    return (nt_ui_tooltip_style_t){
        .panel_bg = 0xFF202020U,
        .text_color = 0xFFE8E8E8U,
        .panel_tint = 0xFFFFFFFFU,
        .caret_tint = 0xFFFFFFFFU,
        .border_color = 0U, /* no border by default */
        .shadow_color = 0U, /* no shadow by default */
        .delay_secs = 0.5F,
        .font_size = 13.0F,
        .open_ease_speed = 0.0F, /* snap-open by default; the game opts into a tween */
        .slice9_scale = 1.0F,
        .shadow_offset_px = 0,
        .max_width = 240U,
        .pad = 6U,
        .font_id = 0U,
        .caret_size = 0U, /* drawn only when style->caret has a ref */
        .border_px = 0U,  /* drawn only when border_color set */
        .corner_radius = 4U,
    };
}

/* The drop-shadow: a translucent rect the panel's size, attached to the panel and pushed +offset, drawn
 * UNDER the panel (declared first inside the panel scope at a child-z below the panel body). Sized from
 * the panel's prev-frame bbox (A3: skipped the first frame until the bbox exists; cosmetic 1-frame lag). */
static void tooltip_declare_shadow(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t panel_id, const nt_ui_tooltip_style_t *style) {
    if (style->shadow_offset_px == 0 || style->shadow_color == 0U) {
        return;
    }
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, panel_id);
    if (!bb.found || bb.width <= 0.0F || bb.height <= 0.0F) {
        return;
    }
    const float off = (float)style->shadow_offset_px;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(bb.width), CLAY_SIZING_FIXED(bb.height)}},
          .floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                       .zIndex = -1, /* below the panel body so the panel paints over the shadow */
                       .offset = {.x = off, .y = off},
                       .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
          .backgroundColor = nt_ui_unpack_abgr(style->shadow_color),
          .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {}
}

/* The caret: a sprite on the panel edge facing the target, flipped per the popup side. The art points UP
 * (apex at top); BELOW the target -> caret on the panel TOP edge pointing up (no flip); ABOVE the target
 * (edge-flip) -> caret on the panel BOTTOM edge pointing down (flip Y). Centered horizontally on the
 * panel. No-op when no ref / caret_size 0. */
static void tooltip_declare_caret(nt_ui_context_t *ctx, uint8_t fill_layer, nt_ui_tooltip_style_t *style, uint8_t side) {
    if (style->caret_size == 0U) {
        return;
    }
    nt_atlas_resolve_ref(&style->caret);
    if (style->caret.atlas.id == 0U || style->caret.region == NT_ATLAS_INVALID_REGION) {
        return;
    }
    const bool above = (side == NT_UI_POPUP_ABOVE); /* tooltip above target -> caret points DOWN */
    const uint8_t flip = above ? NT_SPRITE_FLAG_FLIP_Y : 0U;
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_tooltip: scratch alloc failed (caret payload)");
    *p = (nt_ui_image_payload_t){.atlas = style->caret.atlas, .region_index = style->caret.region, .slice9_scale = 1.0F, .flip_bits = flip};
    /* Attach the caret outside the matching panel edge: its base touches the panel, the apex points at
     * the target. The y-offset pushes it fully clear of the panel by its own height. */
    const float sz = (float)style->caret_size;
    const Clay_FloatingAttachPoints ap = above ? (Clay_FloatingAttachPoints){.element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM}
                                               : (Clay_FloatingAttachPoints){.element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(sz), CLAY_SIZING_FIXED(sz)}},
          .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 1, .attachPoints = ap},
          .image = (Clay_ImageElementConfig){.imageData = p},
          .backgroundColor = nt_ui_unpack_tint(style->caret_tint),
          .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {}
#ifdef NT_TEST_ACCESS
    s_last_caret_present = true;
    s_last_caret_flip = flip;
#endif
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — complexity is the validation assert chain, not control flow
bool nt_ui_tooltip(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t target_id, const char *content, nt_ui_tooltip_style_t *style) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_tooltip: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(target_id != 0U && content != NULL && style != NULL && "nt_ui_tooltip: target_id non-zero, pointers non-NULL");
    NT_ASSERT(isfinite(style->delay_secs) && style->delay_secs >= 0.0F && "nt_ui_tooltip: delay_secs finite && >= 0");
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_tooltip: font_size > 0");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_tooltip: slice9_scale must be finite > 0");

#ifdef NT_TEST_ACCESS
    s_last_caret_present = false;
    s_last_caret_flip = 0U;
    s_last_panel_art = false;
    s_last_panel_corner_radius = 0.0F;
#endif

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* panel fill on data->layer, label on label_layer */

    /* Engine-tracked hover-delay timer in the state pool. */
    nt_ui_tooltip_cell_t *c = nt_ui_state(ctx, tooltip_timer_id(target_id), sizeof *c, NT_UI_STATE_TAG('t', 'i', 'p', ' '));

    /* IDEMPOTENT read of the target's hover — must NOT become a second mutating step on the target id.
     * The game's own step_interaction stays the sole capture mutator. */
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, target_id);
    if (in.hovered) {
        c->hover += ctx->frame_dt;
    } else {
        c->hover = 0.0F;
    }

    const bool open = (c->hover >= style->delay_secs);

    /* Anchor below the target; popup-core edge-flips ABOVE near the bottom border. */
    nt_ui_popup_anchor_t anc = {.prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, target_id);
    if (bb.found) {
        anc.x = bb.x;
        anc.y = bb.y;
        anc.w = bb.width;
        anc.h = bb.height;
    }

    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = style->open_ease_speed; /* game-controlled open tween (0 = snap) */
    pst.flags = 0U;                          /* no light-dismiss catcher: hover-driven, must not gate base UI */
    pst.layer = fill_layer;                  /* widget-owned: the popup panel sits on the fill layer */

    /* Resolve the panel slice9 once: art-or-flat decides whether the panel can round (IMAGE bg can't). */
    nt_atlas_resolve_ref(&style->panel_art);
    const bool panel_art = (style->panel_art.atlas.id != 0U && style->panel_art.region != NT_ATLAS_INVALID_REGION);

    bool shown = false;
    /* Low-level begin/end (no one-bool wrapper): the open state is engine-derived from the hover timer,
     * not a game bool, and there is no close_requested path to harvest. */
    const nt_ui_popup_result_t r = nt_ui_popup_begin(ctx, tooltip_popup_id(target_id), &pst, &anc, open);
#ifdef NT_TEST_ACCESS
    s_last_side = r.side; /* the side popup-core chose this frame (drives the caret flip) */
#endif
    if (r.visible) {
        const uint32_t panel_id = tooltip_panel_id(target_id);
        const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->text_color)};
        Clay_SizingAxis w = (style->max_width > 0U) ? CLAY_SIZING_FIT(.max = (float)style->max_width) : CLAY_SIZING_FIT(0);

        Clay_ElementDeclaration panel = {.id = (Clay_ElementId){.id = panel_id},
                                         .layout = {.sizing = {.width = w, .height = CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(style->pad)},
                                         .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)};
        if (panel_art) {
            /* slice9 panel art: tint multiplies the sprite; IMAGE bg can't round, so cornerRadius is dropped
             * (rounding is baked into the sprite corners) — same rule as dropdown/menu/tabbar. */
            nt_ui_image_payload_t *pp = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
            NT_ASSERT(pp != NULL && "nt_ui_tooltip: scratch alloc failed (panel payload)");
            *pp = (nt_ui_image_payload_t){.atlas = style->panel_art.atlas, .region_index = style->panel_art.region, .slice9_scale = style->slice9_scale};
            panel.image = (Clay_ImageElementConfig){.imageData = pp};
            panel.backgroundColor = nt_ui_unpack_tint(style->panel_tint);
        } else {
            panel.backgroundColor = nt_ui_unpack_abgr(style->panel_bg);
            panel.cornerRadius = CLAY_CORNER_RADIUS((float)style->corner_radius);
        }
#ifdef NT_TEST_ACCESS
        s_last_panel_art = panel_art;
        s_last_panel_corner_radius = panel.cornerRadius.topLeft; /* 0 under art (cornerRadius never set) */
#endif
        if (style->border_px > 0U && style->border_color != 0U) {
            const uint16_t bw = style->border_px;
            panel.border = (Clay_BorderElementConfig){.color = nt_ui_unpack_abgr(style->border_color), .width = {.left = bw, .right = bw, .top = bw, .bottom = bw}};
        }

        nt_ui_clay_priv_open_element();
        nt_ui_clay_priv_configure_open_element(panel);
        tooltip_declare_shadow(ctx, fill_layer, panel_id, style); /* first child: drops UNDER the panel body */
        tooltip_declare_caret(ctx, fill_layer, style, r.side);
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), content, &lbl);
        nt_ui_clay_priv_close_element();
        shown = true;
    }
    nt_ui_popup_end(ctx);
    return shown;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_ui_tooltip_test_timer_id(uint32_t target_id) { return tooltip_timer_id(target_id); }
float nt_ui_tooltip_test_hover_secs(nt_ui_context_t *ctx, uint32_t target_id) {
    const nt_ui_tooltip_cell_t *c = (const nt_ui_tooltip_cell_t *)nt_ui_state_find(ctx, tooltip_timer_id(target_id));
    return (c != NULL) ? c->hover : 0.0F;
}
uint8_t nt_ui_tooltip_test_last_side(void) { return s_last_side; }
bool nt_ui_tooltip_test_last_caret_present(void) { return s_last_caret_present; }
uint8_t nt_ui_tooltip_test_last_caret_flip(void) { return s_last_caret_flip; }
bool nt_ui_tooltip_test_last_panel_art(void) { return s_last_panel_art; }
float nt_ui_tooltip_test_last_panel_corner_radius(void) { return s_last_panel_corner_radius; }
#endif
