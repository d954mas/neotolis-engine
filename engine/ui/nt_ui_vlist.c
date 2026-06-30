#include "ui/nt_ui_vlist.h"

#include <math.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_state.h"

// #region window_math
/* HARD guards (not asserts — those vanish in OFF): a bad count/extent must never over-read the
 * spacer/state index. double avoids float blow-up on huge counts. pos_on_axis is Clay's childOffset
 * (negative going down/right), so scrolled distance = -pos. */
static nt_ui_vlist_range_t vlist_window(float pos_on_axis, float viewport, float item_extent, uint32_t count, int overscan) {
    nt_ui_vlist_range_t r = {1U, 0U}; /* empty sentinel: first > last */
    if (count == 0U) {
        return r;
    }
    const int64_t maxidx = (int64_t)count - 1; /* int64: long is 32-bit on Windows/WASM, so a near-UINT32_MAX count would wrap */
    /* !(x > 0) also rejects NaN -> degenerate path, no div-by-zero. Render only item 0 (in range). */
    if (!(item_extent > 0.0F) || !(viewport > 0.0F)) {
        r.first = 0U;
        r.last = 0U;
        return r;
    }
    if (overscan < 0) {
        overscan = 0;
    }
    float scrolled = -pos_on_axis;
    if (!(scrolled > 0.0F)) {
        scrolled = 0.0F; /* coerces a negative offset (rubber-band) or NaN to the top */
    }
    int64_t first_l = (int64_t)floor((double)scrolled / (double)item_extent) - (int64_t)overscan;
    if (first_l < 0) {
        first_l = 0;
    }
    int64_t visible = (int64_t)ceil((double)viewport / (double)item_extent) + 1 + (2 * (int64_t)overscan);
    if (visible < 1) {
        visible = 1;
    }
    int64_t last_l = first_l + visible - 1;
    if (first_l > maxidx) {
        first_l = maxidx;
    }
    if (last_l > maxidx) {
        last_l = maxidx;
    }
    if (last_l < first_l) {
        last_l = first_l;
    }
    r.first = (uint32_t)first_l;
    r.last = (uint32_t)last_l;
    return r;
}

/* Spacer with a FIXED extent on the scroll axis and GROW(0) on the cross axis. Skips emit at extent
 * <= 0: a zero-size spacer would still trip the container's childGap and shift the visible rows. */
static void vlist_emit_spacer(uint8_t axis, float extent) {
    if (!(extent > 0.0F)) {
        return;
    }
    const Clay_Sizing sz = (axis == NT_UI_AXIS_X) ? (Clay_Sizing){CLAY_SIZING_FIXED(extent), CLAY_SIZING_GROW(0)} : (Clay_Sizing){CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(extent)};
    CLAY({.layout = {.sizing = sz}}) {}
}
// #endregion

nt_ui_vlist_style_t nt_ui_vlist_style_defaults(void) {
    nt_ui_vlist_style_t s;
    memset(&s, 0, sizeof s); /* not = {0}: emscripten -Werror rejects {0} on this aggregate-first struct */
    s.scroll = nt_ui_scroll_style_defaults();
    s.overscan = 2; /* a couple of rows each side hides recycle pop on a fling */
    s.gap = 0.0F;
    s.id_ring = 256U; /* recycle ids over 256 slots -> ~256 hashmap slots/list, never saturates */
    return s;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — inflated by the NT_ASSERT macro expansion (see nt_ui_scroll_begin)
nt_ui_vlist_range_t nt_ui_vlist_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, uint32_t count, float item_extent, nt_ui_axis_t axis, const nt_ui_vlist_style_t *style,
                                      const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_vlist_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_vlist_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_vlist_begin: id must be non-zero");
    NT_ASSERT(!ctx->pending_vlist.active && "nt_ui_vlist_begin: vlists do not nest");
    NT_ASSERT((axis == NT_UI_AXIS_Y || axis == NT_UI_AXIS_X) && "nt_ui_vlist_begin: axis must be NT_UI_AXIS_Y or NT_UI_AXIS_X");
    NT_ASSERT(isfinite(item_extent) && "nt_ui_vlist_begin: item_extent must be finite");

    const nt_ui_vlist_style_t st = (style != NULL) ? *style : nt_ui_vlist_style_defaults();
    NT_ASSERT(isfinite(item_extent) && item_extent > 0.0F && "nt_ui_vlist_begin: item_extent must be finite and > 0");
    NT_ASSERT(st.overscan >= 0 && "nt_ui_vlist_begin: style.overscan must be >= 0");
    NT_ASSERT(isfinite(st.gap) && st.gap >= 0.0F && "nt_ui_vlist_begin: style.gap must be finite and >= 0");
    /* gap is the inter-row spacing: applied as the scroll container's childGap (renders between rows) AND
     * folded into the per-row stride so the window + spacers reserve it. Clay childGap is integer px, so
     * round ONCE and use that same value everywhere (stride, spacers, childGap) — no float/int drift.
     * HARD guards (NT_ASSERT vanishes in OFF): a bad item_extent/gap must never feed a negative/NaN FIXED
     * spacer or a negative childGap. safe_extent/gap_px are the real guards. */
    const uint16_t gap_px = (isfinite(st.gap) && st.gap > 0.0F) ? (uint16_t)st.gap : 0U;
    const float gap = (float)gap_px;
    const float safe_extent = (isfinite(item_extent) && item_extent > 0.0F) ? (item_extent + gap) : 0.0F;

    /* Exactly ONE scroll (one Clay clip) per vlist — never one clip per row (each Clay clip is a
     * persistent pool slot). Axis selects the scroll axis + the inner layoutDirection (an X-axis list
     * needs LEFT_TO_RIGHT inner layout). */
    nt_ui_scroll_style_t ss = st.scroll;
    ss.scroll_x = (axis == NT_UI_AXIS_X);
    ss.scroll_y = (axis == NT_UI_AXIS_Y);
    Clay_ElementDeclaration sd = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    sd.layout.layoutDirection = (axis == NT_UI_AXIS_X) ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM;
    sd.layout.childGap = gap_px; /* vlist owns inter-row spacing — renders the gap the stride alone can't */
    nt_ui_scroll_begin(ctx, data, id, &ss, &sd);

    /* Viewport at a 1-frame lag (prev-frame bbox); 0 on frame 1 -> a safe small default window. The
     * scroll pos is THIS frame's (post-integrate) so the window tracks the clip childOffset. */
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    const float viewport = (axis == NT_UI_AXIS_X) ? bb.width : bb.height;
    const nt_ui_scroll_state_t *s = (const nt_ui_scroll_state_t *)nt_ui_state_find(ctx, id);
    const float pos = (s != NULL) ? s->pos[(axis == NT_UI_AXIS_X) ? 0 : 1] : 0.0F;

    nt_ui_vlist_range_t r = vlist_window(pos, viewport, safe_extent, count, st.overscan);

    /* A window >= id_ring maps two SIMULTANEOUSLY-visible rows to one ring slot -> same Clay id ->
     * DUPLICATE_ID, i.e. id_ring too small (dev misconfig). Assert on the UNCLAMPED count so debug
     * points at the cause, not at the silently-shrunk window the clamp below leaves. */
    const uint64_t want = (r.first <= r.last) ? ((uint64_t)r.last - r.first + 1U) : 0U;
    NT_ASSERT((st.id_ring <= 1U || want < (uint64_t)st.id_ring) && "vlist_begin: visible window (viewport/item_extent + 2*overscan) must stay below style.id_ring — raise id_ring");
    /* Release/OFF fallback: NT_ASSERT vanishes in NT_ASSERT_MODE=OFF, so a REAL clamp must still keep
     * two visible rows off the same slot. Only shrinks `last` (stays <= count-1 and >= first); the
     * trailing spacer (vlist_end) recomputes from this clamped last, keeping content == count*extent. */
    if (st.id_ring > 1U && want > (uint64_t)(st.id_ring - 1U)) {
        r.last = r.first + (st.id_ring - 2U); /* window size = ring-1 (<= ring-1, > 0 since ring>1) */
    }

    const bool empty = (r.first > r.last);

    /* Leading spacer puts row `first` at first*stride: size = first*stride - gap, because the childGap
     * between the spacer and row `first` supplies the boundary gap. -gap at first==0 skips the spacer. */
    const float lead = empty ? 0.0F : (((float)r.first * safe_extent) - gap);
    vlist_emit_spacer((uint8_t)axis, lead);

    ctx->pending_vlist.base_id = id;
    ctx->pending_vlist.count = count;
    ctx->pending_vlist.last = r.last;
    ctx->pending_vlist.ring = st.id_ring;
    ctx->pending_vlist.extent = safe_extent;
    ctx->pending_vlist.gap = gap;
    ctx->pending_vlist.axis = (uint8_t)axis;
    ctx->pending_vlist.empty = empty;
    ctx->pending_vlist.active = true;
    return r;
}

void nt_ui_vlist_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_vlist_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_vlist_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(ctx->pending_vlist.active && "nt_ui_vlist_end: unbalanced (no open vlist)");

    float trail = 0.0F;
    if (!ctx->pending_vlist.empty && ctx->pending_vlist.count > 0U && ctx->pending_vlist.last < (ctx->pending_vlist.count - 1U)) {
        /* Symmetric to the leading spacer: (count-1-last)*stride - gap; the childGap before it supplies
         * the boundary gap, so total content == count*item_extent + (count-1)*gap. */
        trail = ((float)(ctx->pending_vlist.count - 1U - ctx->pending_vlist.last) * ctx->pending_vlist.extent) - ctx->pending_vlist.gap;
    }
    vlist_emit_spacer(ctx->pending_vlist.axis, trail);

    nt_ui_scroll_end(ctx);
    ctx->pending_vlist.active = false;
}

uint32_t nt_ui_vlist_item_id(nt_ui_context_t *ctx, uint32_t index) {
    NT_ASSERT(ctx != NULL && "nt_ui_vlist_item_id: ctx must be non-NULL");
    NT_ASSERT(ctx->pending_vlist.active && "nt_ui_vlist_item_id: no open vlist (call between begin/end)");
    return nt_ui_vlist_item_id_of(ctx->pending_vlist.base_id, index, ctx->pending_vlist.ring);
}

#ifdef NT_TEST_ACCESS
nt_ui_vlist_range_t nt_ui_vlist_test_window(float pos_on_axis, float viewport, float item_extent, uint32_t count, int overscan) {
    return vlist_window(pos_on_axis, viewport, item_extent, count, overscan);
}
#endif
