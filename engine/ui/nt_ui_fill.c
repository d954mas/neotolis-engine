#include "ui/nt_ui_fill.h"

#include <math.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

/* Scratch element_data carrying layer + opacity (no transform). */
static nt_ui_element_data_t *fill_make_data(uint8_t layer, float opacity) {
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_fill: scratch alloc failed (element_data)");
    *d = (nt_ui_element_data_t){.user_data = NULL, .layer = layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_OPACITY, .transform = nt_ui_transform_defaults(), .opacity = opacity};
    return d;
}

static inline bool fill_is_horizontal(nt_ui_fill_direction_t d) { return d == NT_UI_FILL_LTR || d == NT_UI_FILL_RTL; }

/* Growing-edge alignment: LTR/TOP_DOWN anchor at the origin edge, RTL/BOTTOM_UP at the far edge. */
static Clay_ChildAlignment fill_alignment(nt_ui_fill_direction_t d) {
    Clay_ChildAlignment a = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP};
    switch (d) {
    case NT_UI_FILL_LTR:
        a = (Clay_ChildAlignment){CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER};
        break;
    case NT_UI_FILL_RTL:
        a = (Clay_ChildAlignment){CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER};
        break;
    case NT_UI_FILL_BOTTOM_UP:
        a = (Clay_ChildAlignment){CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_BOTTOM};
        break;
    case NT_UI_FILL_TOP_DOWN:
        a = (Clay_ChildAlignment){CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP};
        break;
    default:
        break;
    }
    return a;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_fill_emit(nt_ui_context_t *ctx, uint8_t layer, nt_atlas_region_ref_t *fill_ref, uint32_t tint, float ratio, float track_w, float track_h, nt_ui_fill_mode_t mode,
                     nt_ui_fill_direction_t direction, float opacity) {
    NT_ASSERT(ctx != NULL && "nt_ui_fill_emit: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_fill_emit: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(fill_ref != NULL && "nt_ui_fill_emit: fill_ref must be non-NULL");
    NT_ASSERT(isfinite(ratio) && "nt_ui_fill_emit: ratio must be finite");
    NT_ASSERT(isfinite(track_w) && track_w > 0.0F && isfinite(track_h) && track_h > 0.0F && "nt_ui_fill_emit: track_w/h must be finite > 0");
    NT_ASSERT(isfinite(opacity) && opacity >= 0.0F && opacity <= 1.0F && "nt_ui_fill_emit: opacity must be finite in [0,1]");

    const float r = nt_ui_clampf(ratio, 0.0F, 1.0F);

    /* No-art terminal: resolve in place; skip the whole emit when there is no region. */
    nt_atlas_resolve_ref(fill_ref);
    if (fill_ref->atlas.id == 0U || fill_ref->region == NT_ATLAS_INVALID_REGION) {
        return;
    }

    const bool horizontal = fill_is_horizontal(direction);
    const float fill_w = horizontal ? (r * track_w) : track_w;
    const float fill_h = horizontal ? track_h : (r * track_h);
    if (fill_w <= 0.0F || fill_h <= 0.0F) {
        return; /* ratio 0 — nothing visible */
    }

    nt_ui_image_style_t img_style = nt_ui_image_style_defaults();
    img_style.color_packed = tint;

    if (mode == NT_UI_FILL_STRETCH) {
        /* Edge-anchored container at the revealed size; the region stretches to it (slice9 honored). */
        const Clay_ElementDeclaration img_decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(fill_w), CLAY_SIZING_FIXED(fill_h)}},
        };
        const Clay_ElementDeclaration box = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(track_w), CLAY_SIZING_FIXED(track_h)}, .childAlignment = fill_alignment(direction)},
        };
        nt_ui_clay_priv_open_element();
        nt_ui_clay_priv_configure_open_element(box);
        nt_ui_image(ctx, fill_make_data(layer, opacity), fill_ref, &img_style, &img_decl);
        nt_ui_clay_priv_close_element();
        return;
    }

    /* CROP: a full-size region revealed by `ratio` via a Clay .clip scissor (reuses the
     * walker scissor path); slice9 IGNORED (a partial reveal can't honor stretched borders). */
    NT_ASSERT((img_style.flags & NT_UI_IMAGE_SLICE9_OVERRIDE) == 0U && "nt_ui_fill_emit: CROP mode does not support slice9 borders (D-59-23)");
    const Clay_ElementDeclaration full_img = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(track_w), CLAY_SIZING_FIXED(track_h)}},
    };
    /* Clip child carries the full-size image; its own size = the revealed fraction. The
     * childOffset shifts the full image so the growing edge stays anchored under the scissor. */
    Clay_Vector2 child_off = {0.0F, 0.0F};
    if (direction == NT_UI_FILL_RTL) {
        child_off.x = -(track_w - fill_w); /* reveal the right end */
    } else if (direction == NT_UI_FILL_BOTTOM_UP) {
        child_off.y = -(track_h - fill_h); /* reveal the bottom end */
    }
    const Clay_ElementDeclaration clip = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(fill_w), CLAY_SIZING_FIXED(fill_h)}},
        .clip = {.horizontal = true, .vertical = true, .childOffset = child_off},
    };
    /* Track-sized wrapper anchors the reveal window to the growing edge (RTL right,
     * BOTTOM_UP bottom, ...); without it the clip sits top-left and reveals the wrong end. */
    const Clay_ElementDeclaration box = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(track_w), CLAY_SIZING_FIXED(track_h)}, .childAlignment = fill_alignment(direction)},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(box);
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(clip);
    nt_ui_image(ctx, fill_make_data(layer, opacity), fill_ref, &img_style, &full_img);
    nt_ui_clay_priv_close_element();
    nt_ui_clay_priv_close_element();
}
