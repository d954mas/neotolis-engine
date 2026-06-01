#include "ui/nt_ui.h"

#include "atlas/nt_atlas.h"
#include "core/nt_builtins.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "time/nt_time.h"

/* This is the single CLAY_IMPLEMENTATION TU. CMake parses deps/clay/VERSION
 * into CLAY_PINNED_*; the assert below catches version drift. */

#if !defined(CLAY_PINNED_MAJOR) || !defined(CLAY_PINNED_MINOR)
#error "nt_ui: CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR must be defined by CMake"
#endif

#define CLAY_IMPLEMENTATION
#include "clay.h"

_Static_assert(CLAY_PINNED_MAJOR == 0 && CLAY_PINNED_MINOR == 14, "Clay v0.14 required");

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_align.h"
#include "core/nt_assert.h"
#include "core/nt_clamp.h"
#include "input/nt_input.h" /* NT_INPUT_MAX_POINTERS, nt_pointer_t */
#include "log/nt_log.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_debug.h" /* nt_ui_debug_record_disabled_zone prototype */
#include "ui/nt_ui_image.h" /* NT_UI_IMAGE_*_OVERRIDE flags */
#include "ui/nt_ui_internal.h"

// #region marker_types
enum {
    NT_UI_MARKER_PUSH_TRANSFORM = 1,
    NT_UI_MARKER_POP_TRANSFORM = 2,
    NT_UI_MARKER_PUSH_OPACITY = 3,
    NT_UI_MARKER_POP_OPACITY = 4,
};
// #endregion

/* Inspector sidebar width (verbatim Clay debug-view port; clay.h:3113-3122).
 * Defined HERE rather than alongside the other CDV_* metrics (~L640) so
 * nt_ui_begin can use it for the inspector_pointer_consumed gate. The verbatim
 * port below references this same macro -- one source of truth. */
#define CDV_PANEL_WIDTH 400

// #region module_state
/* Only one ctx may be in-frame at a time; nt_ui_begin asserts NULL on entry. */
static nt_ui_context_t *g_nt_ui_inframe_ctx = NULL;
/* Set true between nt_ui_module_init / nt_ui_module_shutdown. */
static bool s_nt_ui_module_initialized = false;

/* Pre-built element_data for each layer (user_data=NULL). Avoids scratch alloc. */
static nt_ui_element_data_t s_default_element_data[256];
// #endregion

// #region clay_error_handler
/* All Clay errors are fatal -- assert compiles out in NT_ASSERT_OFF production. */
static void nt_ui_clay_error_cb(Clay_ErrorData err) {
    /* errorText is .length + .chars, NOT NUL-terminated. */
    const int len = err.errorText.length;
    const char *const chars = (err.errorText.chars != NULL && len > 0) ? err.errorText.chars : "(no text)";
    const int safe_len = (err.errorText.chars != NULL && len > 0) ? len : 9;
    const int type = (int)err.errorType;
    NT_LOG_ERROR("clay error type=%d: %.*s", type, safe_len, chars);
    NT_ASSERT(false && "nt_ui: Clay reported a contract violation (see preceding log line)");
}
// #endregion

// #region measure_cb
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static Clay_Dimensions nt_ui_measure_text_cb(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data;
    NT_ASSERT(g_nt_ui_inframe_ctx != NULL && "measure_cb: Clay called outside begin/end");
    NT_ASSERT(config != NULL && "measure_cb: Clay passed NULL config");

    nt_ui_context_t *ctx = g_nt_ui_inframe_ctx;
    NT_ASSERT((uint32_t)config->fontId < NT_UI_MAX_FONTS && "nt_ui measure_cb: fontId out of range; check CLAY_TEXT_CONFIG vs NT_UI_MAX_FONTS");
    nt_font_t font = ctx->fonts[config->fontId];
    NT_ASSERT(nt_font_valid(font) && "nt_ui measure_cb: font slot empty; call nt_ui_set_font before declaring TEXT with this fontId");
    const float ls = (float)config->letterSpacing;
    nt_text_size_t s = nt_font_measure_n(font, text.chars, (size_t)text.length, (float)config->fontSize, ls);
    /* Clay's MeasureTextCached subtracts one trailing letterSpacing per line
     * (clay.h:1677); add it back so bbox matches the renderer's (N-1)*ls width. */
    if (s.width > 0.0F && ls != 0.0F) {
        s.width += ls;
    }
    return (Clay_Dimensions){.width = s.width, .height = s.height};
}
// #endregion

// #region module_init
#define NT_UI_CORNER_SEGMENTS 6
_Static_assert(NT_UI_CORNER_SEGMENTS >= 2 && NT_UI_CORNER_SEGMENTS <= 16, "NT_UI_CORNER_SEGMENTS must be in [2, 16]");
#define NT_UI_PI_F 3.14159265358979323846F

/* Quadrant index: 0=BR, 1=BL, 2=TL, 3=TR (a_start = q * π/2). */
typedef struct {
    float cos;
    float sin;
} nt_ui_trig_pair_t;
static nt_ui_trig_pair_t s_arc_lut[4][NT_UI_CORNER_SEGMENTS + 1];

static void nt_ui_init_arc_lut(void) {
    for (uint32_t q = 0U; q < 4U; ++q) {
        const float a_start = NT_UI_PI_F * 0.5F * (float)q;
        for (uint32_t s = 0U; s <= NT_UI_CORNER_SEGMENTS; ++s) {
            const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
            const float a = a_start + (NT_UI_PI_F * 0.5F * t);
            s_arc_lut[q][s].cos = cosf(a);
            s_arc_lut[q][s].sin = sinf(a);
        }
    }
}

void nt_ui_module_init(void) {
    NT_ASSERT(!s_nt_ui_module_initialized && "nt_ui_module_init: already initialized; call nt_ui_module_shutdown first");
    Clay__MeasureText = nt_ui_measure_text_cb;
    g_nt_ui_inframe_ctx = NULL;
    Clay_SetCurrentContext(NULL);
    nt_ui_init_arc_lut();
    for (uint32_t i = 0; i < 256U; i++) {
        s_default_element_data[i].layer = (nt_ui_layer_t)i;
    }
    s_nt_ui_module_initialized = true;
}
void nt_ui_module_shutdown(void) {
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_module_shutdown: not initialized");
    Clay__MeasureText = NULL;
    g_nt_ui_inframe_ctx = NULL;
    Clay_SetCurrentContext(NULL);
    s_nt_ui_module_initialized = false;
}
// #endregion

// #region create_destroy
/* ctx struct gets padded to cache line so Clay's arena starts on a clean boundary. */
#define NT_UI_CACHE_LINE ((size_t)64U)

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc) {
    NT_ASSERT(desc != NULL && "nt_ui_min_arena_size: desc must be non-NULL");
    NT_ASSERT(desc->max_elements > 0U && "nt_ui_min_arena_size: desc->max_elements must be > 0");
    NT_ASSERT(desc->max_elements <= UINT16_MAX && "nt_ui_min_arena_size: desc->max_elements exceeds uint16 sorted-index range");
    /* Clay_SetMaxElementCount(N) also writes defaultMaxMeasureTextWordCacheCount
     * = N*2 (clay.h:4332); restore via the same call so both come back. */
    Clay_Context *saved_ctx = Clay_GetCurrentContext();
    const int32_t saved_default = Clay__defaultMaxElementCount;
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount((int32_t)desc->max_elements);
    const size_t clay_bytes = (size_t)Clay_MinMemorySize();
    Clay_SetMaxElementCount(saved_default);
    Clay_SetCurrentContext(saved_ctx);
    const uint32_t max_m = (desc->max_markers > 0U) ? desc->max_markers : desc->max_elements * 2U;
    const size_t marker_bytes = NT_ALIGN_UP(sizeof(nt_ui_marker_t) * max_m, NT_UI_CACHE_LINE);
    return NT_ALIGN_UP(sizeof(struct nt_ui_context), NT_UI_CACHE_LINE) + marker_bytes + clay_bytes;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc) {
    NT_ASSERT(arena != NULL && "nt_ui_create_context: arena must be non-NULL");
    NT_ASSERT(desc != NULL && "nt_ui_create_context: desc must be non-NULL");
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_create_context: call nt_ui_module_init() once before any create_context");
    NT_ASSERT(((uintptr_t)arena & (NT_UI_ARENA_ALIGN - 1U)) == 0U && "nt_ui_create_context: arena must be NT_UI_ARENA_ALIGN-aligned (alignas(NT_UI_ARENA_ALIGN) static uint8_t arena[N])");
    NT_ASSERT(arena_size >= nt_ui_min_arena_size(desc) && "nt_ui_create_context: arena_size < nt_ui_min_arena_size(desc)");

    nt_ui_context_t *ctx = (nt_ui_context_t *)arena;
    memset(ctx, 0, sizeof(*ctx));

    const size_t ctx_size = NT_ALIGN_UP(sizeof(struct nt_ui_context), NT_UI_CACHE_LINE);
    /* Layout: [ctx struct][markers][Clay arena]. */
    ctx->max_elements = desc->max_elements;
    const uint32_t max_m = (desc->max_markers > 0U) ? desc->max_markers : desc->max_elements * 2U;
    const size_t marker_bytes = NT_ALIGN_UP(sizeof(nt_ui_marker_t) * max_m, NT_UI_CACHE_LINE);
    ctx->markers = (nt_ui_marker_t *)((char *)arena + ctx_size);
    ctx->max_markers = max_m;
    ctx->marker_count = 0;
    void *clay_mem = (char *)arena + ctx_size + marker_bytes;
    const size_t clay_size = arena_size - ctx_size - marker_bytes;

    /* Stage max_elements into Clay's globals so Clay_Initialize inherits it;
     * re-null current before restore -- Clay_SetMaxElementCount writes
     * per-ctx when current is non-NULL. */
    Clay_Context *saved_ctx = Clay_GetCurrentContext();
    const int32_t saved_default = Clay__defaultMaxElementCount;
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount((int32_t)desc->max_elements);

    ctx->in_frame = false;
    ctx->clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_size, clay_mem);
    ctx->clay = Clay_Initialize(ctx->clay_arena, (Clay_Dimensions){.width = 1.0F, .height = 1.0F}, (Clay_ErrorHandler){.errorHandlerFunction = nt_ui_clay_error_cb, .userData = ctx});

    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount(saved_default);
    Clay_SetCurrentContext(saved_ctx);

    return ctx;
}

void nt_ui_destroy_context(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_destroy_context: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_destroy_context: ctx is mid-frame (call nt_ui_end first)");
    /* Clay's current_ptr would dangle into the freshly memset arena. */
    if (Clay_GetCurrentContext() == ctx->clay) {
        Clay_SetCurrentContext(NULL);
    }
    memset(ctx, 0, sizeof(*ctx));
}
// #endregion

// #region font_registry
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_font: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_font: must be called outside begin/end");
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_set_font: font_id >= NT_UI_MAX_FONTS");
    ctx->fonts[font_id] = font;
}
// #endregion

// #region begin_end
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, float dt, const nt_pointer_t *pointers, uint32_t count) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_begin: nt_ui_module_init() must be called before begin");
    NT_ASSERT(pointers != NULL && "nt_ui_begin: pointers must be non-NULL");
    NT_ASSERT(count > 0U && count <= NT_INPUT_MAX_POINTERS && "nt_ui_begin: count must be 1..NT_INPUT_MAX_POINTERS");
    /* isfinite() rejects NaN + +-inf which `>= 0.0F` alone lets through. */
    NT_ASSERT(isfinite(screen_w) && screen_w >= 0.0F && "nt_ui_begin: screen_w must be finite and non-negative");
    NT_ASSERT(isfinite(screen_h) && screen_h >= 0.0F && "nt_ui_begin: screen_h must be finite and non-negative");
    NT_ASSERT(isfinite(dt) && dt >= 0.0F && "nt_ui_begin: dt must be finite and non-negative");
    NT_ASSERT(g_nt_ui_inframe_ctx == NULL && "nt_ui_begin: another ctx is mid-frame");
    NT_ASSERT(!ctx->in_frame && "nt_ui_begin: ctx already in_frame");

    /* MUST be first so subsequent Clay calls operate on ctx->clay. */
    Clay_SetCurrentContext(ctx->clay);

    ctx->in_frame = true;
    g_nt_ui_inframe_ctx = ctx;
    ctx->marker_count = 0;
    ctx->accum_depth = 0; /* Phase 56: reset declaration-time transform stack. */

    /* Snapshot the frame pointer list + dt for the engine-owned hit-test
     * (Plan 03 reads frame_pointers; anim cache reads frame_dt, D-56-15/19). */
    memcpy(ctx->frame_pointers, pointers, sizeof(nt_pointer_t) * count);
    ctx->frame_pointer_count = count;
    ctx->frame_dt = dt;

    /* Phase 56 (D-56-06): orphaned-capture cleanup. A capture whose widget was
     * NOT re-queried last frame (capture_seen == 0) is abandoned -> clear it,
     * else it would hold the pointer forever. Then reset the per-frame flags. */
    for (uint32_t i = 0; i < NT_INPUT_MAX_POINTERS; ++i) {
        if (ctx->captures[i].active_id != 0U && ctx->capture_seen[i] == 0U) {
            ctx->captures[i].active_id = 0U;
        }
        ctx->capture_seen[i] = 0U;
    }
    ctx->pointer_over_any = false;

    /* Phase 56 ext: hit-zone debug overlay recording is per-frame; clear the
     * zone buffer each begin so stale zones never bleed across frames. The
     * debug_recording flag persists (it's a user toggle). */
    ctx->debug_zone_count = 0U;

    /* Dev-mode footgun guard: a button begin/end that asserted mid-flight in
     * NT_ASSERT_FULL would leave pending_button.active=true, wedging every
     * subsequent frame with "nested buttons unsupported". Release/TRAP dies
     * on the first assert so this only matters in dev. Reset is unconditional. */
    ctx->pending_button.active = false;

    /* Phase 56 ext (CHUNK E): widget tag registry is per-frame; clear every
     * slot (id=0 = empty). inspector_active is a user toggle (persists). */
    memset(ctx->widget_registry, 0, sizeof(ctx->widget_registry));

    /* Phase 56 ext inspector rework: inspector_highlight_id is per-frame
     * (cleared each begin, recomputed during emit_layout via hover detection).
     * inspector_selected_id PERSISTS across frames -- only the sidebar click
     * inside emit_layout (or an explicit unselect) modifies it. */
    ctx->inspector_highlight_id = 0U;

    /* v1.8 drives the primary pointer; Clay is fed only this one. */
    const nt_pointer_t *primary = &pointers[0];

    /* Phase 56 ext fix: per-frame "pointer is over the inspector sidebar" gate.
     * The sidebar is a right-attached floating panel CDV_PANEL_WIDTH wide; the
     * same coord check is what the inspector's emit_layout uses to decide
     * whether to highlight a sidebar row (line ~1005). Computing it here gates
     * nt_ui_get_interaction_padded so user widgets behind the sidebar do NOT
     * register hover/press/click when the sidebar visually consumes the click.
     * Frame-1 safe (no layout solve required -- pure coord check). */
    ctx->inspector_pointer_consumed = false;
    if (ctx->inspector_active && primary->x >= (screen_w - (float)CDV_PANEL_WIDTH)) {
        ctx->inspector_pointer_consumed = true;
    }

    /* Clay's built-in debug view is intentionally OFF -- the inspector
     * REPLACES it entirely (one debug system; verbatim port). */
    Clay_SetDebugModeEnabled(false);
    Clay_SetLayoutDimensions((Clay_Dimensions){.width = screen_w, .height = screen_h});

    /* Left-button only; Clay v0.14 has no right/middle/wheel buttons. */
    Clay_SetPointerState((Clay_Vector2){.x = primary->x, .y = primary->y}, primary->buttons[NT_BUTTON_LEFT].is_down);

    /* Forward wheel + enable touch/drag-scroll (mobile/web pointer drag inside
     * a Clay clip scrolls it). Y inverted: Clay scroll opposite of typical wheel_dy. */
    Clay_UpdateScrollContainers(true, (Clay_Vector2){.x = primary->wheel_dx, .y = -primary->wheel_dy}, dt);

    Clay_BeginLayout();
}

/* Forward declaration -- defined later in this TU (needs Clay private types). */
static void nt_ui_internal_emit_inspector_layout(nt_ui_context_t *ctx);

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_end: ctx is not in_frame (begin was not called)");
    NT_ASSERT(ctx == g_nt_ui_inframe_ctx && "nt_ui_end: ctx mismatch with module in-frame ctx");

    /* Phase 56 ext inspector rework: inject the verbatim Clay-debug-view port
     * AT ROOT SCOPE between the user's CLAY({...}) blocks and Clay_EndLayout.
     * Floating panels attached to the root keep the inspector OUT of the
     * user's layout tree -- it shares the same layout solve and renders via
     * nt_ui_walk through the same sprite/text path. */
    if (ctx->inspector_active) {
        nt_ui_internal_emit_inspector_layout(ctx);
    }

    /* layout_ms times the Clay layout solve (EndLayout), not the begin->end span. */
    const double layout_t0 = nt_time_now();
    ctx->frozen_cmds = Clay_EndLayout();
    ctx->last_layout_ms = (float)((nt_time_now() - layout_t0) * 1000.0);

    /* Markers keep layout-element indices (before_clay_idx). The walker
     * matches directly via nt_layout_index on each render command — no
     * O(M×R) remap needed. */

    ctx->in_frame = false;
    g_nt_ui_inframe_ctx = NULL;
    /* Stray CLAY_* between end and next begin NULL-derefs instead of corrupting. */
    Clay_SetCurrentContext(NULL);
}
// #endregion

// #region helpers_color_pack
/* Clay's RGBA floats are 0..255 unclamped; saturate via nt_clamp_f_to_u8. */
static inline uint32_t nt_color_pack_clay(Clay_Color c) {
    uint32_t r = nt_clamp_f_to_u8(c.r);
    uint32_t g = nt_clamp_f_to_u8(c.g);
    uint32_t b = nt_clamp_f_to_u8(c.b);
    uint32_t a = nt_clamp_f_to_u8(c.a);
    return r | (g << 8) | (b << 16) | (a << 24);
}
// #endregion

// #region element_data_alloc
const nt_ui_element_data_t *nt_ui_make_element_data(nt_ui_layer_t layer, void *user_data) {
    if (user_data == NULL) {
        return &s_default_element_data[layer];
    }
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    d->layer = layer;
    d->user_data = user_data;
    return d;
}
// #endregion

// #region inspector_internal_accessors (CHUNK E)
uint32_t nt_ui_internal_current_open_element_id(void) {
    if (g_nt_ui_inframe_ctx == NULL || g_nt_ui_inframe_ctx->clay == NULL) {
        return 0U;
    }
    /* Mirror Clay__GetOpenLayoutElement (clay.h:1325) without going through
     * its private prototype: top of openLayoutElementStack indexes into
     * layoutElements; ->id is the Clay-assigned id. */
    Clay_Context *cc = g_nt_ui_inframe_ctx->clay;
    if (cc->openLayoutElementStack.length <= 0) {
        return 0U;
    }
    const int32_t idx = Clay__int32_tArray_GetValue(&cc->openLayoutElementStack, cc->openLayoutElementStack.length - 1);
    Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&cc->layoutElements, idx);
    return el->id;
}

/* Inspector lives in a separate TU but needs to peek at the Clay
 * layoutElements array. The full Clay_Context type is only visible inside
 * this TU (CLAY_IMPLEMENTATION) -- expose two thin accessors so the
 * inspector stays Clay-agnostic. */
int32_t nt_ui_internal_get_layout_element_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_get_layout_element_count: ctx must be non-NULL");
    if (ctx->clay == NULL) {
        return 0;
    }
    return ctx->clay->layoutElements.length;
}

nt_ui_inspector_element_view_t nt_ui_internal_get_layout_element_view(const nt_ui_context_t *ctx, int32_t index) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_get_layout_element_view: ctx must be non-NULL");
    nt_ui_inspector_element_view_t v = {0};
    if (ctx->clay == NULL || index < 0 || index >= ctx->clay->layoutElements.length) {
        return v;
    }
    Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&ctx->clay->layoutElements, index);
    v.id = el->id;
    /* Position via Clay_GetElementData -- requires Clay's current context set
     * (Clay__GetHashMapItem dereferences it). Inspector is called AFTER
     * nt_ui_end (which clears current context), so set it for the lookup
     * and restore afterward to keep the post-end invariant. */
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    Clay_ElementData ed = Clay_GetElementData((Clay_ElementId){.id = el->id});
    Clay_SetCurrentContext(saved);
    if (ed.found) {
        v.x = ed.boundingBox.x;
        v.y = ed.boundingBox.y;
        v.w = ed.boundingBox.width;
        v.h = ed.boundingBox.height;
    } else {
        v.x = 0.0F;
        v.y = 0.0F;
        v.w = el->dimensions.width;
        v.h = el->dimensions.height;
    }
    return v;
}

/* Map Clay's CLAY__ELEMENT_CONFIG_TYPE_* bitmask (Clay__ElementConfigType) to
 * our exposed 8-bit mask. Order mirrors the Clay__DebugGetElementConfigTypeLabel
 * switch at clay.h:3130 (Shared / Text / Aspect / Image / Floating / Clip /
 * Border / Custom). */
static uint8_t inspector_element_config_mask(Clay_LayoutElement *el) {
    uint8_t mask = 0U;
    for (int32_t i = 0; i < el->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&el->elementConfigs, i);
        switch (cfg->type) {
        case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
            mask |= 1U << 0U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
            mask |= 1U << 1U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_ASPECT:
            mask |= 1U << 2U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_IMAGE:
            mask |= 1U << 3U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_FLOATING:
            mask |= 1U << 4U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_CLIP:
            mask |= 1U << 5U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_BORDER:
            mask |= 1U << 6U;
            break;
        case CLAY__ELEMENT_CONFIG_TYPE_CUSTOM:
            mask |= 1U << 7U;
            break;
        default:
            break;
        }
    }
    return mask;
}

/* DFS pre-order tree walk -- mirrors Clay__RenderDebugLayoutElementsList
 * (clay.h:3151) but writes flat rows to caller-owned storage instead of
 * emitting CLAY({...}) macros. Depth is tracked via a small explicit stack
 * (cap matches Clay's reusableElementIndexBuffer depth budget). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int32_t nt_ui_internal_collect_tree_rows(const nt_ui_context_t *ctx, nt_ui_inspector_tree_row_t *out, int32_t out_cap) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_collect_tree_rows: ctx must be non-NULL");
    NT_ASSERT(out != NULL && "nt_ui_internal_collect_tree_rows: out must be non-NULL");
    NT_ASSERT(out_cap >= 0 && "nt_ui_internal_collect_tree_rows: out_cap must be >= 0");
    if (ctx->clay == NULL || out_cap == 0) {
        return 0;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);

    int32_t written = 0;
    const int32_t roots = ctx->clay->layoutElementTreeRoots.length;

    /* Explicit DFS stack: (element_index, depth, child_cursor). Cap matches
     * Clay's reusableElementIndexBuffer headroom -- if it overflows we stop
     * walking gracefully (inspector is observability). */
    enum { STACK_CAP = 256 };
    struct {
        int32_t elem_idx;
        uint8_t depth;
        int32_t child_cursor;
    } stack[STACK_CAP];
    int32_t sp = 0;

    for (int32_t r = 0; r < roots && written < out_cap; ++r) {
        Clay__LayoutElementTreeRoot *root = Clay__LayoutElementTreeRootArray_Get(&ctx->clay->layoutElementTreeRoots, r);
        if (sp >= STACK_CAP) {
            break;
        }
        stack[sp].elem_idx = root->layoutElementIndex;
        stack[sp].depth = 0U;
        stack[sp].child_cursor = -1;
        sp++;

        while (sp > 0 && written < out_cap) {
            int32_t top = sp - 1;
            Clay_LayoutElement *el = Clay_LayoutElementArray_Get(&ctx->clay->layoutElements, stack[top].elem_idx);
            if (stack[top].child_cursor < 0) {
                /* First visit -- emit row. */
                nt_ui_inspector_tree_row_t *row = &out[written++];
                memset(row, 0, sizeof *row);
                row->id = el->id;
                row->depth = stack[top].depth;
                row->config_mask = inspector_element_config_mask(el);
                Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(el->id);
                if (item != NULL) {
                    row->bbox_x = item->boundingBox.x;
                    row->bbox_y = item->boundingBox.y;
                    row->bbox_w = item->boundingBox.width;
                    row->bbox_h = item->boundingBox.height;
                    row->offscreen = (uint8_t)Clay__ElementIsOffscreen(&item->boundingBox);
                }
                Clay_String idStr = ctx->clay->layoutElementIdStrings.internalArray[stack[top].elem_idx];
                row->id_string = idStr.chars;
                row->id_string_len = (uint16_t)((idStr.length < 0) ? 0 : idStr.length);
                row->is_text = (uint8_t)Clay__ElementHasConfig(el, CLAY__ELEMENT_CONFIG_TYPE_TEXT);
                if (row->is_text) {
                    Clay__TextElementData *td = el->childrenOrTextContent.textElementData;
                    if (td != NULL) {
                        row->text_chars = td->text.chars;
                        row->text_len = (uint16_t)((td->text.length < 0) ? 0 : td->text.length);
                    }
                }
                stack[top].child_cursor = 0;
                if (row->is_text) {
                    /* TEXT element has no recursable children -- pop now. */
                    sp--;
                    continue;
                }
            }
            /* Push next child or pop. */
            const int32_t childCount = el->childrenOrTextContent.children.length;
            if (stack[top].child_cursor < childCount) {
                int32_t child_idx = el->childrenOrTextContent.children.elements[stack[top].child_cursor];
                stack[top].child_cursor++;
                if (sp >= STACK_CAP) {
                    /* Stack overflow -- stop walking. */
                    sp = 0;
                    break;
                }
                if (stack[top].depth >= UINT8_MAX - 1U) {
                    /* Skip pushing -- depth would overflow. */
                    continue;
                }
                stack[sp].elem_idx = child_idx;
                stack[sp].depth = (uint8_t)(stack[top].depth + 1U);
                stack[sp].child_cursor = -1;
                sp++;
            } else {
                sp--;
            }
        }
    }

    Clay_SetCurrentContext(saved);
    return written;
}

nt_ui_inspector_element_info_t nt_ui_internal_get_element_info(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_internal_get_element_info: ctx must be non-NULL");
    nt_ui_inspector_element_info_t info = {0};
    if (ctx->clay == NULL || id == 0U) {
        return info;
    }
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    Clay_LayoutElementHashMapItem *item = Clay__GetHashMapItem(id);
    if (item == NULL || item->layoutElement == NULL) {
        Clay_SetCurrentContext(saved);
        return info;
    }
    info.found = true;
    info.bbox_x = item->boundingBox.x;
    info.bbox_y = item->boundingBox.y;
    info.bbox_w = item->boundingBox.width;
    info.bbox_h = item->boundingBox.height;
    info.id_string = item->elementId.stringId.chars;
    info.id_string_len = (uint16_t)((item->elementId.stringId.length < 0) ? 0 : item->elementId.stringId.length);
    Clay_LayoutConfig *lc = item->layoutElement->layoutConfig;
    info.layout_direction = (uint8_t)lc->layoutDirection;
    info.padding_l = lc->padding.left;
    info.padding_r = lc->padding.right;
    info.padding_t = lc->padding.top;
    info.padding_b = lc->padding.bottom;
    info.child_gap = lc->childGap;
    info.child_align_x = (uint8_t)lc->childAlignment.x;
    info.child_align_y = (uint8_t)lc->childAlignment.y;
    info.config_mask = inspector_element_config_mask(item->layoutElement);

    for (int32_t i = 0; i < item->layoutElement->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&item->layoutElement->elementConfigs, i);
        if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
            info.bg_r = cfg->config.sharedElementConfig->backgroundColor.r;
            info.bg_g = cfg->config.sharedElementConfig->backgroundColor.g;
            info.bg_b = cfg->config.sharedElementConfig->backgroundColor.b;
            info.bg_a = cfg->config.sharedElementConfig->backgroundColor.a;
            info.corner_tl = cfg->config.sharedElementConfig->cornerRadius.topLeft;
            info.corner_tr = cfg->config.sharedElementConfig->cornerRadius.topRight;
            info.corner_bl = cfg->config.sharedElementConfig->cornerRadius.bottomLeft;
            info.corner_br = cfg->config.sharedElementConfig->cornerRadius.bottomRight;
        } else if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_TEXT) {
            info.text_font_size = cfg->config.textElementConfig->fontSize;
            info.text_font_id = cfg->config.textElementConfig->fontId;
            info.text_color_r = cfg->config.textElementConfig->textColor.r;
            info.text_color_g = cfg->config.textElementConfig->textColor.g;
            info.text_color_b = cfg->config.textElementConfig->textColor.b;
            info.text_color_a = cfg->config.textElementConfig->textColor.a;
            info.text_align = (uint8_t)cfg->config.textElementConfig->textAlignment;
        }
    }
    Clay_SetCurrentContext(saved);
    return info;
}
// #endregion

// #region inspector_emit_layout (verbatim Clay debug view port)
/* Phase 56 ext rework: verbatim port of Clay__RenderDebugView (clay.h:3392-3800)
 * adapted to run inside the user's layout pass (between user CLAY blocks and
 * Clay_EndLayout in nt_ui_end). Lives in this TU because the body touches
 * Clay private types (Clay_Context fields, Clay__GetHashMapItem,
 * Clay__ElementIsOffscreen, layoutElements / layoutElementTreeRoots /
 * reusableElementIndexBuffer / pointerInfo / pointerOverIds / etc.) that are
 * file-static in clay.h.
 *
 * Lines 3113-3122 of clay.h define the palette + metrics (COLOR_1..4,
 * SELECTED_ROW, ROW_HEIGHT=30, OUTER_PADDING=10, INDENT_WIDTH=16). Reproduced
 * verbatim here as static const. */

static const Clay_Color CDV_COLOR_1 = {58, 56, 52, 255};
static const Clay_Color CDV_COLOR_2 = {62, 60, 58, 255};
static const Clay_Color CDV_COLOR_3 = {141, 133, 135, 255};
static const Clay_Color CDV_COLOR_4 = {238, 226, 231, 255};
static const Clay_Color CDV_COLOR_SELECTED_ROW = {102, 80, 78, 255};
static const Clay_Color CDV_HIGHLIGHT_COLOR = {168, 66, 28, 100}; /* Clay__debugViewHighlightColor */
#define CDV_ROW_HEIGHT 30
#define CDV_OUTER_PADDING 10
#define CDV_INDENT_WIDTH 16
/* CDV_PANEL_WIDTH moved to the top of the file so nt_ui_begin can read it
 * for the inspector_pointer_consumed gate (Phase 56 ext fix). */

/* Mirror of Clay__DebugGetElementConfigTypeLabel (clay.h:3130-3140) but
 * collapsed to just label + color (no inner struct). */
static const char *cdv_config_label(uint8_t type) {
    switch (type) {
    case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
        return "Shared";
    case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
        return "Text";
    case CLAY__ELEMENT_CONFIG_TYPE_ASPECT:
        return "Aspect";
    case CLAY__ELEMENT_CONFIG_TYPE_IMAGE:
        return "Image";
    case CLAY__ELEMENT_CONFIG_TYPE_FLOATING:
        return "Floating";
    case CLAY__ELEMENT_CONFIG_TYPE_CLIP:
        return "Scroll";
    case CLAY__ELEMENT_CONFIG_TYPE_BORDER:
        return "Border";
    case CLAY__ELEMENT_CONFIG_TYPE_CUSTOM:
        return "Custom";
    default:
        return "Error";
    }
}

static Clay_Color cdv_config_color(uint8_t type) {
    switch (type) {
    case CLAY__ELEMENT_CONFIG_TYPE_SHARED:
        return (Clay_Color){243, 134, 48, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_TEXT:
        return (Clay_Color){105, 210, 231, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_ASPECT:
        return (Clay_Color){101, 149, 194, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_IMAGE:
        return (Clay_Color){121, 189, 154, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_FLOATING:
        return (Clay_Color){250, 105, 0, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_CLIP:
        return (Clay_Color){242, 196, 90, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_BORDER:
        return (Clay_Color){108, 91, 123, 255};
    case CLAY__ELEMENT_CONFIG_TYPE_CUSTOM:
        return (Clay_Color){11, 72, 107, 255};
    default:
        return (Clay_Color){0, 0, 0, 255};
    }
}

/* Engine extension column: widget-type pill (button/image/label/panel/group).
 * Pulls from ctx->widget_registry. Plain Clay rows show "-". */
static const char *cdv_widget_tag(nt_ui_widget_type_t t) {
    switch (t) {
    case NT_UI_WIDGET_BUTTON:
        return "button";
    case NT_UI_WIDGET_IMAGE:
        return "image";
    case NT_UI_WIDGET_LABEL:
        return "label";
    case NT_UI_WIDGET_PANEL:
        return "panel";
    case NT_UI_WIDGET_GROUP:
        return "group";
    case NT_UI_WIDGET_NONE:
    default:
        return "-";
    }
}

static Clay_Color cdv_widget_color(nt_ui_widget_type_t t) {
    /* Distinct hues from Clay's config-type pills so the extension column
     * reads as a separate dimension. */
    switch (t) {
    case NT_UI_WIDGET_BUTTON:
        return (Clay_Color){70, 180, 90, 200};
    case NT_UI_WIDGET_IMAGE:
        return (Clay_Color){120, 90, 180, 200};
    case NT_UI_WIDGET_LABEL:
        return (Clay_Color){90, 140, 200, 200};
    case NT_UI_WIDGET_PANEL:
        return (Clay_Color){180, 120, 70, 200};
    case NT_UI_WIDGET_GROUP:
        return (Clay_Color){160, 160, 90, 200};
    case NT_UI_WIDGET_NONE:
    default:
        return (Clay_Color){80, 80, 80, 120};
    }
}

/* Engine extension column: layer number from nt_ui_element_data_t.layer (via
 * Clay's userData). Returns -1 if userData is NULL. */
static int32_t cdv_element_layer(Clay_LayoutElement *el) {
    for (int32_t i = 0; i < el->elementConfigs.length; ++i) {
        Clay_ElementConfig *cfg = Clay__ElementConfigArraySlice_Get(&el->elementConfigs, i);
        if (cfg->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
            void *u = cfg->config.sharedElementConfig->userData;
            if (u != NULL) {
                const nt_ui_element_data_t *d = (const nt_ui_element_data_t *)u;
                return (int32_t)d->layer;
            }
        }
    }
    return -1;
}

/* Clay__IntToString does int -> Clay_String. We can't see that file-static
 * symbol from this TU (it's also static), so reimplement using static buffers
 * cycled per call. The verbatim Clay uses static buffers internally too.
 *
 * Phase 56 ext bug-fix (layer column reported the LAST element's layer for
 * every row): the previous 16-slot ring buffer wrapped LONG before Clay's
 * layout solve consumed the Clay_String pointers. With ~9 int-to-string calls
 * in the info pane alone + 1 per tree row's "L:N" cell, a 30-row tree calls
 * this ~40 times per frame; slot N got overwritten 2-3 times before render,
 * so every CLAY_TEXT(cdv_int_to_string(layer)) pointer aliased the LAST
 * snprintf result. Bumping the ring to 512 slots covers any practical UI tree
 * + info pane in a single frame; the cursor still wraps but only once the
 * widget count exceeds the cap (the ring is cleared each frame implicitly
 * because all consumers run within the layout pass). 8 KB BSS cost is
 * negligible vs the visual-correctness gain. */
#ifndef NT_UI_INSPECTOR_INT_BUFS
#define NT_UI_INSPECTOR_INT_BUFS 512
#endif
_Static_assert((NT_UI_INSPECTOR_INT_BUFS & (NT_UI_INSPECTOR_INT_BUFS - 1)) == 0, "NT_UI_INSPECTOR_INT_BUFS must be a power of two");
static char cdv_int_bufs[NT_UI_INSPECTOR_INT_BUFS][16];
static uint32_t cdv_int_buf_cursor = 0U;
static Clay_String cdv_int_to_string(int32_t v) {
    char *buf = cdv_int_bufs[cdv_int_buf_cursor];
    cdv_int_buf_cursor = (cdv_int_buf_cursor + 1U) & (NT_UI_INSPECTOR_INT_BUFS - 1U);
    const int n = snprintf(buf, sizeof cdv_int_bufs[0], "%d", v);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Same ring strategy for hex IDs (string-id-empty fallback). 8-char hex +
 * "#" prefix + NUL fits in 16. Separate ring so hex and decimal don't
 * compete for slots. */
static char cdv_hex_bufs[NT_UI_INSPECTOR_INT_BUFS][16];
static uint32_t cdv_hex_buf_cursor = 0U;
static Clay_String cdv_hex_id_to_string(uint32_t v) {
    char *buf = cdv_hex_bufs[cdv_hex_buf_cursor];
    cdv_hex_buf_cursor = (cdv_hex_buf_cursor + 1U) & (NT_UI_INSPECTOR_INT_BUFS - 1U);
    const int n = snprintf(buf, sizeof cdv_hex_bufs[0], "#%08X", v);
    return (Clay_String){.length = (n > 0) ? n : 0, .chars = buf};
}

/* Forward declaration -- mutual recursion with the tree walk. */
typedef struct {
    int32_t row_count;
    int32_t selected_element_row_index;
} cdv_layout_data_t;

/* Verbatim port of Clay__RenderDebugLayoutElementsList (clay.h:3151-3308).
 * Adapted only for:
 *   - reuses ctx->treeNodeVisited if available; falls back to a private stack
 *     when Clay's `treeNodeVisited` isn't pre-sized.
 *   - hooks ctx->inspector_highlight_id when a row is hovered + ctx->
 *     inspector_selected_id on click (mirror of Clay's debugSelectedElementId).
 *   - emits the engine widget-tag pill + layer column at the tail of each row. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
static cdv_layout_data_t cdv_render_layout_elements_list(nt_ui_context_t *ctx, int32_t initial_roots_length, int32_t highlighted_row_index) {
    Clay_Context *context = ctx->clay;
    /* Private DFS stack -- avoids mutating Clay's reusableElementIndexBuffer
     * which is otherwise touched by Clay's own layout solve. Cap matches
     * Clay's headroom (max element depth = 256). At-cap the walk stops
     * gracefully; the inspector is observability, not correctness. */
    enum { CDV_DFS_CAP = 256 };
    int32_t dfs_elems[CDV_DFS_CAP];
    bool dfs_visited[CDV_DFS_CAP];
    /* Phase 56 ext fix: track whether this frame emitted indent wrappers (the
     * 3 anonymous Clay__OpenElement blocks at the children-recurse branch). If
     * a frame is filtered out (no identity), we did NOT open them and must NOT
     * close them on the second-visit pass either. Mirrors dfs_visited shape. */
    bool dfs_opened_wrappers[CDV_DFS_CAP];
    int32_t dfs_length = 0;
    Clay__DebugView_ScrollViewItemLayoutConfig = (Clay_LayoutConfig){.sizing = {.height = CLAY_SIZING_FIXED(CDV_ROW_HEIGHT)}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}};
    cdv_layout_data_t layoutData = {0};
    uint32_t highlightedElementId = 0U;

    for (int32_t rootIndex = 0; rootIndex < initial_roots_length; ++rootIndex) {
        dfs_length = 0;
        Clay__LayoutElementTreeRoot *root = Clay__LayoutElementTreeRootArray_Get(&context->layoutElementTreeRoots, rootIndex);
        if (dfs_length >= CDV_DFS_CAP) {
            break;
        }
        dfs_elems[dfs_length] = root->layoutElementIndex;
        dfs_visited[dfs_length] = false;
        dfs_opened_wrappers[dfs_length] = false;
        dfs_length++;
        if (rootIndex > 0) {
            CLAY({.id = CLAY_IDI("ntInsp_EmptyRowOuter", rootIndex), .layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = {CDV_INDENT_WIDTH / 2, 0, 0, 0}}}) {
                CLAY({.id = CLAY_IDI("ntInsp_EmptyRow", rootIndex),
                      .layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED((float)CDV_ROW_HEIGHT)}},
                      .border = {.color = CDV_COLOR_3, .width = {.top = 1}}}) {}
            }
            layoutData.row_count++;
        }
        while (dfs_length > 0) {
            int32_t currentElementIndex = dfs_elems[dfs_length - 1];
            Clay_LayoutElement *currentElement = Clay_LayoutElementArray_Get(&context->layoutElements, (int)currentElementIndex);
            if (dfs_visited[dfs_length - 1]) {
                /* Phase 56 ext fix: only close the 3 indent wrappers when this
                 * frame actually opened them (filtered frames did not). */
                if (dfs_opened_wrappers[dfs_length - 1]) {
                    Clay__CloseElement();
                    Clay__CloseElement();
                    Clay__CloseElement();
                }
                dfs_length--;
                continue;
            }
            dfs_visited[dfs_length - 1] = true;
            Clay_LayoutElementHashMapItem *currentElementData = Clay__GetHashMapItem(currentElement->id);
            bool offscreen = currentElementData != NULL && Clay__ElementIsOffscreen(&currentElementData->boundingBox);
            /* Phase 56 ext extension columns: widget-type pill + layer cell. */
            const int32_t layer = cdv_element_layer(currentElement);
            const nt_ui_widget_type_t wtype = nt_ui_widget_lookup(ctx, currentElement->id);
            Clay_String idString = context->layoutElementIdStrings.internalArray[currentElementIndex];
            /* Phase 56 ext fix: filter out the 3 anonymous indent wrappers we
             * open per child (lines below) plus any other Clay-auto-anonymous
             * container -- a row is emitted only when the element has a
             * meaningful identity:
             *   has_identity = (stringId.length > 0) || widget_lookup != NONE
             * Falls back to descending silently (no row, no wrappers) so the
             * tree shows only user-named/widget-tagged elements. The text
             * branch below is exempt -- text content is its own identity. */
            const bool has_identity = (idString.length > 0) || (wtype != NT_UI_WIDGET_NONE);
            if (has_identity) {
                if (highlighted_row_index == layoutData.row_count) {
                    if (context->pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
                        ctx->inspector_selected_id = currentElement->id;
                    }
                    highlightedElementId = currentElement->id;
                }
                if (ctx->inspector_selected_id == currentElement->id) {
                    layoutData.selected_element_row_index = layoutData.row_count;
                }
            }
            if (has_identity) {
                CLAY({.id = CLAY_IDI("ntInsp_ElementOuter", currentElement->id), .layout = Clay__DebugView_ScrollViewItemLayoutConfig}) {
                    /* Collapse icon / dot (verbatim shape but no debugData usage --
                     * we don't track collapse state in the engine; show the dot
                     * variant always so the layout cadence matches Clay's). */
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8)}}, .backgroundColor = CDV_COLOR_3, .cornerRadius = CLAY_CORNER_RADIUS(2)}) {}
                    }
                    if (offscreen) {
                        CLAY({.layout = {.padding = {8, 8, 2, 2}}, .border = {.color = CDV_COLOR_3, .width = {1, 1, 1, 1, 0}}}) {
                            CLAY_TEXT(CLAY_STRING("Offscreen"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = 16}));
                        }
                    }
                    /* Phase 56 ext fix: when stringId is empty (CLAY_IDI / no-id /
                     * Clay-auto-anonymous), Clay's debug view rendered nothing,
                     * leaving the row visually blank. Fall back to the element id
                     * as hex so unnamed elements are still identifiable in the
                     * tree. Buffered via cdv_hex_id_to_string for stable Clay_String
                     * pointers (same ring-buffer fix as the layer column). */
                    if (idString.length > 0) {
                        CLAY_TEXT(idString, offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = 16}) : &Clay__DebugView_TextNameConfig);
                    } else {
                        CLAY_TEXT(cdv_hex_id_to_string(currentElement->id), offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = 16}) : &Clay__DebugView_TextNameConfig);
                    }
                    for (int32_t elementConfigIndex = 0; elementConfigIndex < currentElement->elementConfigs.length; ++elementConfigIndex) {
                        Clay_ElementConfig *elementConfig = Clay__ElementConfigArraySlice_Get(&currentElement->elementConfigs, elementConfigIndex);
                        if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            const Clay_Color labelColor = {243, 134, 48, 90};
                            Clay_Color backgroundColor = elementConfig->config.sharedElementConfig->backgroundColor;
                            Clay_CornerRadius radius = elementConfig->config.sharedElementConfig->cornerRadius;
                            if (backgroundColor.a > 0) {
                                CLAY({.layout = {.padding = {8, 8, 2, 2}},
                                      .backgroundColor = labelColor,
                                      .cornerRadius = CLAY_CORNER_RADIUS(4),
                                      .border = {.color = labelColor, .width = {1, 1, 1, 1, 0}}}) {
                                    CLAY_TEXT(CLAY_STRING("Color"), CLAY_TEXT_CONFIG({.textColor = offscreen ? CDV_COLOR_3 : CDV_COLOR_4, .fontSize = 16}));
                                }
                            }
                            if (radius.bottomLeft > 0) {
                                CLAY({.layout = {.padding = {8, 8, 2, 2}},
                                      .backgroundColor = labelColor,
                                      .cornerRadius = CLAY_CORNER_RADIUS(4),
                                      .border = {.color = labelColor, .width = {1, 1, 1, 1, 0}}}) {
                                    CLAY_TEXT(CLAY_STRING("Radius"), CLAY_TEXT_CONFIG({.textColor = offscreen ? CDV_COLOR_3 : CDV_COLOR_4, .fontSize = 16}));
                                }
                            }
                            continue;
                        }
                        Clay_Color config_color = cdv_config_color((uint8_t)elementConfig->type);
                        Clay_Color backgroundColor = config_color;
                        backgroundColor.a = 90;
                        const char *labelStr = cdv_config_label((uint8_t)elementConfig->type);
                        CLAY({.layout = {.padding = {8, 8, 2, 2}},
                              .backgroundColor = backgroundColor,
                              .cornerRadius = CLAY_CORNER_RADIUS(4),
                              .border = {.color = config_color, .width = {1, 1, 1, 1, 0}}}) {
                            CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(labelStr), .chars = labelStr}), CLAY_TEXT_CONFIG({.textColor = offscreen ? CDV_COLOR_3 : CDV_COLOR_4, .fontSize = 16}));
                        }
                    }
                    /* Engine extension columns: widget-type pill + layer cell. */
                    if (wtype != NT_UI_WIDGET_NONE) {
                        Clay_Color wbg = cdv_widget_color(wtype);
                        const char *wlabel = cdv_widget_tag(wtype);
                        CLAY({.layout = {.padding = {8, 8, 2, 2}}, .backgroundColor = wbg, .cornerRadius = CLAY_CORNER_RADIUS(4), .border = {.color = wbg, .width = {1, 1, 1, 1, 0}}}) {
                            CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(wlabel), .chars = wlabel}), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = 16}));
                        }
                    }
                    /* Layer cell -- "L:N" or "-". */
                    if (layer >= 0) {
                        CLAY({.layout = {.padding = {6, 6, 2, 2}}, .border = {.color = CDV_COLOR_3, .width = {1, 1, 1, 1, 0}}, .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
                            CLAY_TEXT(CLAY_STRING("L:"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = 16}));
                            CLAY_TEXT(cdv_int_to_string(layer), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = 16}));
                        }
                    }
                }
            } /* if (has_identity) */

            /* Text-content row (verbatim from clay.h:3258-3270). */
            if (Clay__ElementHasConfig(currentElement, CLAY__ELEMENT_CONFIG_TYPE_TEXT)) {
                layoutData.row_count++;
                Clay__TextElementData *textElementData = currentElement->childrenOrTextContent.textElementData;
                Clay_TextElementConfig *rawTextConfig = offscreen ? CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = 16}) : &Clay__DebugView_TextNameConfig;
                CLAY({.layout = {.sizing = {.height = CLAY_SIZING_FIXED(CDV_ROW_HEIGHT)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
                    CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(CDV_INDENT_WIDTH + 16)}}}) {}
                    CLAY_TEXT(CLAY_STRING("\""), rawTextConfig);
                    if (textElementData != NULL) {
                        CLAY_TEXT(textElementData->text.length > 40 ? ((Clay_String){.length = 40, .chars = textElementData->text.chars}) : textElementData->text, rawTextConfig);
                        if (textElementData->text.length > 40) {
                            CLAY_TEXT(CLAY_STRING("..."), rawTextConfig);
                        }
                    }
                    CLAY_TEXT(CLAY_STRING("\""), rawTextConfig);
                }
            } else if (has_identity && currentElement->childrenOrTextContent.children.length > 0) {
                /* Only open the 3 indent wrappers when a row was emitted --
                 * filtered (no-identity) elements descend SILENTLY so anonymous
                 * Clay containers don't add visible indent steps. */
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.padding = {.left = 8}}});
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.padding = {.left = CDV_INDENT_WIDTH}}, .border = {.color = CDV_COLOR_3, .width = {.left = 1}}});
                Clay__OpenElement();
                Clay__ConfigureOpenElement((Clay_ElementDeclaration){.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}});
                dfs_opened_wrappers[dfs_length - 1] = true;
            }

            /* row_count tracks VISIBLE rows -- filtered frames do not advance it
             * so highlighted_row_index from the pointer-Y math keeps mapping to
             * the user-visible rows correctly. */
            if (has_identity) {
                layoutData.row_count++;
            }
            if (!Clay__ElementHasConfig(currentElement, CLAY__ELEMENT_CONFIG_TYPE_TEXT)) {
                const int32_t childLen = currentElement->childrenOrTextContent.children.length;
                int32_t *childElems = currentElement->childrenOrTextContent.children.elements;
                /* Clay's lifecycle: children.elements is assigned in Clay__CloseElement (clay.h:1828).
                 * Our verbatim port is invoked from nt_ui_end BEFORE Clay_EndLayout, so the
                 * auto-emitted Clay__RootContainer (always element 0) is still OPEN -- its
                 * children.elements is NULL even though children.length tracks how many user
                 * top-level CLAY blocks closed under it. For that single case the live child
                 * indices are at the BOTTOM of context->layoutElementChildrenBuffer (the buffer
                 * Clay uses for in-flight children; clay.h:1906). Every other element on the
                 * walk path either has children.elements already populated (closed normally)
                 * or is genuinely a leaf. */
                if (childLen > 0 && childElems == NULL && currentElementIndex == 0) {
                    childElems = context->layoutElementChildrenBuffer.internalArray;
                }
                if (childLen > 0 && childElems != NULL) {
                    for (int32_t i = childLen - 1; i >= 0; --i) {
                        if (dfs_length >= CDV_DFS_CAP) {
                            break;
                        }
                        dfs_elems[dfs_length] = childElems[i];
                        dfs_visited[dfs_length] = false;
                        dfs_opened_wrappers[dfs_length] = false;
                        dfs_length++;
                    }
                }
            }
        }
    }

    if (highlightedElementId) {
        /* Mirror clay.h:3303 -- floating highlight rectangle attached to the
         * hovered element. This is the IN-VIEWPORT highlight as the user moves
         * the pointer over the sidebar. */
        CLAY({.id = CLAY_ID("ntInsp_ElementHighlight"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
              .floating = {.parentId = highlightedElementId, .zIndex = 32767, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH, .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID}}) {
            CLAY({.id = CLAY_ID("ntInsp_ElementHighlightRectangle"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .backgroundColor = CDV_HIGHLIGHT_COLOR}) {}
        }
        /* Surface the hovered id to the post-walk overlay. */
        ctx->inspector_highlight_id = highlightedElementId;
    } else if (ctx->inspector_selected_id != 0U) {
        /* No hover -- fall back to persistent selection so the overlay still
         * focuses the last clicked sidebar row. */
        ctx->inspector_highlight_id = ctx->inspector_selected_id;
    }
    return layoutData;
}

/* Verbatim port of Clay__RenderDebugView (clay.h:3392-3800). Adapted:
 *   - close button: we still emit it (visual parity) but the click sets
 *     ctx->inspector_active = false on press inside its bounds, not Clay's
 *     debugModeEnabled (which is no longer wired).
 *   - pointer-in-debug-view check uses our panel width (CDV_PANEL_WIDTH) and
 *     the 300 px info-pane reservation, same as Clay's literal constants.
 *   - the info pane is a CONDENSED but faithful version of clay.h:3477-3800
 *     (Bounding Box, Layout Direction, Sizing, Padding, Child Gap, Child
 *     Alignment) + a single config-type header per element config + body for
 *     SHARED/TEXT/IMAGE/CLIP/BORDER (Floating/Custom/Aspect ports are
 *     headers-only to keep this TU finite -- the user explicitly accepted
 *     literal-where-possible; truly verbose configs degrade to header only).
 *   - warnings pane is dropped (engine doesn't read Clay warnings here). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_ui_internal_emit_inspector_layout(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(ctx->in_frame);
    NT_ASSERT(ctx->clay != NULL);

    /* Reset the int/hex string rings at the start of each emit so the cursor
     * always begins at 0 -- gives us a deterministic 512-slot window per
     * frame regardless of how many frames have run before. The Clay_String
     * pointers remain stable through the layout solve since no other code
     * writes into these buffers between emit_layout and Clay_EndLayout. */
    cdv_int_buf_cursor = 0U;
    cdv_hex_buf_cursor = 0U;

    Clay_Context *context = ctx->clay;
    Clay_ElementId closeButtonId = Clay__HashString(CLAY_STRING("ntInsp_CloseButton"), 0, 0);
    if (context->pointerInfo.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        for (int32_t i = 0; i < context->pointerOverIds.length; ++i) {
            Clay_ElementId *elementId = Clay_ElementIdArray_Get(&context->pointerOverIds, i);
            if (elementId->id == closeButtonId.id) {
                ctx->inspector_active = false;
                return;
            }
        }
    }

    uint32_t initialRootsLength = (uint32_t)context->layoutElementTreeRoots.length;
    uint32_t initialElementsLength = (uint32_t)context->layoutElements.length;
    Clay_TextElementConfig *infoTextConfig = CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = 16, .wrapMode = CLAY_TEXT_WRAP_NONE});
    Clay_TextElementConfig *infoTitleConfig = CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_3, .fontSize = 16, .wrapMode = CLAY_TEXT_WRAP_NONE});
    Clay_ElementId scrollId = Clay__HashString(CLAY_STRING("ntInsp_OuterScrollPane"), 0, 0);
    float scrollYOffset = 0;
    bool pointerInDebugView = context->pointerInfo.position.y < context->layoutDimensions.height - 300;
    for (int32_t i = 0; i < context->scrollContainerDatas.length; ++i) {
        Clay__ScrollContainerDataInternal *scrollContainerData = Clay__ScrollContainerDataInternalArray_Get(&context->scrollContainerDatas, i);
        if (scrollContainerData->elementId == scrollId.id) {
            if (!context->externalScrollHandlingEnabled) {
                scrollYOffset = scrollContainerData->scrollPosition.y;
            } else {
                pointerInDebugView = context->pointerInfo.position.y + scrollContainerData->scrollPosition.y < context->layoutDimensions.height - 300;
            }
            break;
        }
    }
    int32_t highlightedRow = pointerInDebugView ? (int32_t)((context->pointerInfo.position.y - scrollYOffset) / (float)CDV_ROW_HEIGHT) - 1 : -1;
    if (context->pointerInfo.position.x < context->layoutDimensions.width - (float)CDV_PANEL_WIDTH) {
        highlightedRow = -1;
    }
    cdv_layout_data_t layoutData = {0};
    /* RIGHT_CENTER/RIGHT_CENTER (overlay) instead of Clay's LEFT_CENTER/RIGHT_CENTER (side-by-side):
     * engine disables Clay debug mode so the root is full-width and the verbatim attach
     * would land at [screen.w, screen.w + panel_w] -- entirely off-screen. */
    CLAY({.id = CLAY_ID("ntInsp_Root"),
          .layout = {.sizing = {CLAY_SIZING_FIXED((float)CDV_PANEL_WIDTH), CLAY_SIZING_FIXED(context->layoutDimensions.height)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .floating = {.zIndex = 32765,
                       .attachPoints = {.element = CLAY_ATTACH_POINT_RIGHT_CENTER, .parent = CLAY_ATTACH_POINT_RIGHT_CENTER},
                       .attachTo = CLAY_ATTACH_TO_ROOT,
                       .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT},
          .border = {.color = CDV_COLOR_3, .width = {.bottom = 1}}}) {
        /* Header bar. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CDV_ROW_HEIGHT)}, .padding = {CDV_OUTER_PADDING, CDV_OUTER_PADDING, 0, 0}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = CDV_COLOR_2}) {
            CLAY_TEXT(CLAY_STRING("nt_ui_inspector (Clay debug view port)"), infoTextConfig);
            CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}}}) {}
            /* Close button (verbatim shape from clay.h:3439-3447). */
            CLAY({.id = closeButtonId,
                  .layout = {.sizing = {CLAY_SIZING_FIXED(CDV_ROW_HEIGHT - 10), CLAY_SIZING_FIXED(CDV_ROW_HEIGHT - 10)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = {217, 91, 67, 80},
                  .cornerRadius = CLAY_CORNER_RADIUS(4),
                  .border = {.color = {217, 91, 67, 255}, .width = {1, 1, 1, 1, 0}}}) {
                CLAY_TEXT(CLAY_STRING("x"), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = 16}));
            }
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}, .backgroundColor = CDV_COLOR_3}) {}
        CLAY({.id = scrollId, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .clip = {.horizontal = true, .vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
                  .backgroundColor = ((initialElementsLength + initialRootsLength) & 1) == 0 ? CDV_COLOR_2 : CDV_COLOR_1}) {
                Clay_ElementId panelContentsId = Clay__HashString(CLAY_STRING("ntInsp_PaneOuter"), 0, 0);
                CLAY({.id = panelContentsId,
                      .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                      .floating = {.zIndex = 32766, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH, .attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT}}) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = {CDV_OUTER_PADDING, CDV_OUTER_PADDING, 0, 0}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                        layoutData = cdv_render_layout_elements_list(ctx, (int32_t)initialRootsLength, highlightedRow);
                    }
                }
                Clay_LayoutElementHashMapItem *panelContentsItem = Clay__GetHashMapItem(panelContentsId.id);
                float contentWidth = panelContentsItem != NULL ? panelContentsItem->layoutElement->dimensions.width : 0.0F;
                CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(contentWidth)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {}
                for (int32_t i = 0; i < layoutData.row_count; i++) {
                    Clay_Color rowColor = (i & 1) == 0 ? CDV_COLOR_2 : CDV_COLOR_1;
                    if (i == layoutData.selected_element_row_index) {
                        rowColor = CDV_COLOR_SELECTED_ROW;
                    }
                    if (i == highlightedRow) {
                        rowColor.r *= 1.25F;
                        rowColor.g *= 1.25F;
                        rowColor.b *= 1.25F;
                    }
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CDV_ROW_HEIGHT)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}, .backgroundColor = rowColor}) {}
                }
            }
        }
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(1)}}, .backgroundColor = CDV_COLOR_3}) {}
        if (ctx->inspector_selected_id != 0U) {
            Clay_LayoutElementHashMapItem *selectedItem = Clay__GetHashMapItem(ctx->inspector_selected_id);
            if (selectedItem != NULL) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(300)}, .layoutDirection = CLAY_TOP_TO_BOTTOM},
                      .backgroundColor = CDV_COLOR_2,
                      .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()},
                      .border = {.color = CDV_COLOR_3, .width = {.betweenChildren = 1}}}) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(CDV_ROW_HEIGHT + 8)},
                                     .padding = {CDV_OUTER_PADDING, CDV_OUTER_PADDING, 0, 0},
                                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
                        CLAY_TEXT(CLAY_STRING("Layout Config"), infoTextConfig);
                        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}}}) {}
                        if (selectedItem->elementId.stringId.length != 0) {
                            CLAY_TEXT(selectedItem->elementId.stringId, infoTitleConfig);
                        }
                    }
                    Clay_Padding attributeConfigPadding = {CDV_OUTER_PADDING, CDV_OUTER_PADDING, 8, 8};
                    CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                        /* Engine extension rows -- Widget type + Layer.
                         * Surfaced at the TOP of the info pane so the user
                         * sees them first when inspecting an element. */
                        const nt_ui_widget_type_t sel_w = nt_ui_widget_lookup(ctx, ctx->inspector_selected_id);
                        const char *sel_w_tag = cdv_widget_tag(sel_w);
                        CLAY_TEXT(CLAY_STRING("Widget"), infoTitleConfig);
                        CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(sel_w_tag), .chars = sel_w_tag}), infoTextConfig);
                        CLAY_TEXT(CLAY_STRING("Layer"), infoTitleConfig);
                        const int32_t selLayer = cdv_element_layer(selectedItem->layoutElement);
                        if (selLayer >= 0) {
                            CLAY_TEXT(cdv_int_to_string(selLayer), infoTextConfig);
                        } else {
                            CLAY_TEXT(CLAY_STRING("(none)"), infoTextConfig);
                        }
                        CLAY_TEXT(CLAY_STRING("Bounding Box"), infoTitleConfig);
                        CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
                            CLAY_TEXT(CLAY_STRING("{ x: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.x), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", y: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.y), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", width: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.width), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", height: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string((int32_t)selectedItem->boundingBox.height), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                        }
                        Clay_LayoutConfig *layoutConfig = selectedItem->layoutElement->layoutConfig;
                        CLAY_TEXT(CLAY_STRING("Layout Direction"), infoTitleConfig);
                        CLAY_TEXT(layoutConfig->layoutDirection == CLAY_TOP_TO_BOTTOM ? CLAY_STRING("TOP_TO_BOTTOM") : CLAY_STRING("LEFT_TO_RIGHT"), infoTextConfig);
                        CLAY_TEXT(CLAY_STRING("Padding"), infoTitleConfig);
                        CLAY({.id = CLAY_ID("ntInsp_ElementInfoPadding")}) {
                            CLAY_TEXT(CLAY_STRING("{ left: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.left), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", right: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.right), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", top: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.top), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", bottom: "), infoTextConfig);
                            CLAY_TEXT(cdv_int_to_string(layoutConfig->padding.bottom), infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                        }
                        CLAY_TEXT(CLAY_STRING("Child Gap"), infoTitleConfig);
                        CLAY_TEXT(cdv_int_to_string(layoutConfig->childGap), infoTextConfig);
                        CLAY_TEXT(CLAY_STRING("Child Alignment"), infoTitleConfig);
                        CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
                            CLAY_TEXT(CLAY_STRING("{ x: "), infoTextConfig);
                            Clay_String alignX = CLAY_STRING("LEFT");
                            if (layoutConfig->childAlignment.x == CLAY_ALIGN_X_CENTER) {
                                alignX = CLAY_STRING("CENTER");
                            } else if (layoutConfig->childAlignment.x == CLAY_ALIGN_X_RIGHT) {
                                alignX = CLAY_STRING("RIGHT");
                            }
                            CLAY_TEXT(alignX, infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(", y: "), infoTextConfig);
                            Clay_String alignY = CLAY_STRING("TOP");
                            if (layoutConfig->childAlignment.y == CLAY_ALIGN_Y_CENTER) {
                                alignY = CLAY_STRING("CENTER");
                            } else if (layoutConfig->childAlignment.y == CLAY_ALIGN_Y_BOTTOM) {
                                alignY = CLAY_STRING("BOTTOM");
                            }
                            CLAY_TEXT(alignY, infoTextConfig);
                            CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                        }
                    }
                    /* Per-config-type headers + condensed bodies (one row per
                     * attached config). Full per-config bodies for SHARED +
                     * TEXT only; other types render header-only -- this is
                     * the literal-where-possible boundary documented at the
                     * function header. */
                    for (int32_t cfgIdx = 0; cfgIdx < selectedItem->layoutElement->elementConfigs.length; ++cfgIdx) {
                        Clay_ElementConfig *elementConfig = Clay__ElementConfigArraySlice_Get(&selectedItem->layoutElement->elementConfigs, cfgIdx);
                        /* Header pill (mirror of Clay__RenderDebugViewElementConfigHeader). */
                        Clay_Color hdr_color = cdv_config_color((uint8_t)elementConfig->type);
                        Clay_Color hdr_bg = hdr_color;
                        hdr_bg.a = 90;
                        const char *hdr_label = cdv_config_label((uint8_t)elementConfig->type);
                        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(CDV_OUTER_PADDING), .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
                            CLAY({.layout = {.padding = {8, 8, 2, 2}}, .backgroundColor = hdr_bg, .cornerRadius = CLAY_CORNER_RADIUS(4), .border = {.color = hdr_color, .width = {1, 1, 1, 1, 0}}}) {
                                CLAY_TEXT(((Clay_String){.length = (int32_t)strlen(hdr_label), .chars = hdr_label}), CLAY_TEXT_CONFIG({.textColor = CDV_COLOR_4, .fontSize = 16}));
                            }
                        }
                        if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_SHARED) {
                            Clay_SharedElementConfig *sharedConfig = elementConfig->config.sharedElementConfig;
                            CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                                CLAY_TEXT(CLAY_STRING("Background Color"), infoTitleConfig);
                                CLAY({.layout = {.childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
                                    CLAY_TEXT(CLAY_STRING("{ r: "), infoTextConfig);
                                    CLAY_TEXT(cdv_int_to_string((int32_t)sharedConfig->backgroundColor.r), infoTextConfig);
                                    CLAY_TEXT(CLAY_STRING(", g: "), infoTextConfig);
                                    CLAY_TEXT(cdv_int_to_string((int32_t)sharedConfig->backgroundColor.g), infoTextConfig);
                                    CLAY_TEXT(CLAY_STRING(", b: "), infoTextConfig);
                                    CLAY_TEXT(cdv_int_to_string((int32_t)sharedConfig->backgroundColor.b), infoTextConfig);
                                    CLAY_TEXT(CLAY_STRING(", a: "), infoTextConfig);
                                    CLAY_TEXT(cdv_int_to_string((int32_t)sharedConfig->backgroundColor.a), infoTextConfig);
                                    CLAY_TEXT(CLAY_STRING(" }"), infoTextConfig);
                                    CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(10)}}}) {}
                                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(CDV_ROW_HEIGHT - 8), CLAY_SIZING_FIXED(CDV_ROW_HEIGHT - 8)}},
                                          .backgroundColor = sharedConfig->backgroundColor,
                                          .cornerRadius = CLAY_CORNER_RADIUS(4),
                                          .border = {.color = CDV_COLOR_4, .width = {1, 1, 1, 1, 0}}}) {}
                                }
                            }
                        } else if (elementConfig->type == CLAY__ELEMENT_CONFIG_TYPE_TEXT) {
                            Clay_TextElementConfig *textConfig = elementConfig->config.textElementConfig;
                            CLAY({.layout = {.padding = attributeConfigPadding, .childGap = 8, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                                CLAY_TEXT(CLAY_STRING("Font Size"), infoTitleConfig);
                                CLAY_TEXT(cdv_int_to_string(textConfig->fontSize), infoTextConfig);
                                CLAY_TEXT(CLAY_STRING("Font ID"), infoTitleConfig);
                                CLAY_TEXT(cdv_int_to_string(textConfig->fontId), infoTextConfig);
                                CLAY_TEXT(CLAY_STRING("Letter Spacing"), infoTitleConfig);
                                CLAY_TEXT(cdv_int_to_string(textConfig->letterSpacing), infoTextConfig);
                            }
                        }
                    }
                }
            }
        }
    }
}

/* External entry point exposed in nt_ui_internal.h. Forwarded from
 * nt_ui_inspector.c (which can't see Clay private types). */
void nt_ui_internal_emit_inspector_layout_extern(nt_ui_context_t *ctx) { nt_ui_internal_emit_inspector_layout(ctx); }
// #endregion

// #region widget_registry (CHUNK E)
/* Direct-mapped per-frame widget tag table. Replace-on-collision because the
 * inspector is observability (not correctness): missing a single colliding
 * tag is acceptable, complex chaining would cost runtime + memory for no
 * real win at the inspector cap of ~128 widgets per frame. id 0 is the
 * no-widget sentinel; silently dropped. */
void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, nt_ui_widget_type_t type) { nt_ui_widget_register_padded(ctx, id, type, NULL); }

void nt_ui_widget_register_padded(nt_ui_context_t *ctx, uint32_t id, nt_ui_widget_type_t type, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_widget_register_padded: ctx must be non-NULL");
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_widget_register_padded: pad_lrtb components must be >= 0");
    if (id == 0U) {
        return; /* sentinel: never register the no-id slot */
    }
    const uint32_t bucket = id & (NT_UI_WIDGET_REGISTRY_CAP - 1U);
    nt_ui_widget_slot_t *s = &ctx->widget_registry[bucket];
    s->id = id;
    s->type = (uint8_t)type;
    if (pad_lrtb != NULL) {
        s->has_padding = 1U;
        s->hit_padding_lrtb[0] = pad_lrtb[0];
        s->hit_padding_lrtb[1] = pad_lrtb[1];
        s->hit_padding_lrtb[2] = pad_lrtb[2];
        s->hit_padding_lrtb[3] = pad_lrtb[3];
    } else {
        s->has_padding = 0U;
        s->hit_padding_lrtb[0] = 0;
        s->hit_padding_lrtb[1] = 0;
        s->hit_padding_lrtb[2] = 0;
        s->hit_padding_lrtb[3] = 0;
    }
}

nt_ui_widget_type_t nt_ui_widget_lookup(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_widget_lookup: ctx must be non-NULL");
    if (id == 0U) {
        return NT_UI_WIDGET_NONE;
    }
    const uint32_t bucket = id & (NT_UI_WIDGET_REGISTRY_CAP - 1U);
    const nt_ui_widget_slot_t *s = &ctx->widget_registry[bucket];
    return (s->id == id) ? (nt_ui_widget_type_t)s->type : NT_UI_WIDGET_NONE;
}

bool nt_ui_widget_get_hit_padding(const nt_ui_context_t *ctx, uint32_t id, int16_t out_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_widget_get_hit_padding: ctx must be non-NULL");
    NT_ASSERT(out_lrtb != NULL && "nt_ui_widget_get_hit_padding: out_lrtb must be non-NULL");
    if (id == 0U) {
        return false;
    }
    const uint32_t bucket = id & (NT_UI_WIDGET_REGISTRY_CAP - 1U);
    const nt_ui_widget_slot_t *s = &ctx->widget_registry[bucket];
    if (s->id != id || !s->has_padding) {
        return false;
    }
    out_lrtb[0] = s->hit_padding_lrtb[0];
    out_lrtb[1] = s->hit_padding_lrtb[1];
    out_lrtb[2] = s->hit_padding_lrtb[2];
    out_lrtb[3] = s->hit_padding_lrtb[3];
    return true;
}
// #endregion

// #region helper_emit_screen_rect
static inline void emit_screen_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, uint32_t color_packed, float rotation) {
    if (rotation == 0.0F) {
        const float m[16] = {
            w, 0.0F, 0.0F, 0.0F, 0.0F, h, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, x, y, 0.0F, 1.0F,
        };
        nt_sprite_renderer_emit_region(atlas, region_index, m, 0.0F, 0.0F, color_packed, 0U);
    } else {
        const float rcx = x + (w * 0.5F);
        const float rcy = y + (h * 0.5F);
        const float hw = w * 0.5F;
        const float hh = h * 0.5F;
        const float rc = cosf(rotation);
        const float rs = sinf(rotation);
        const float m[16] = {
            w * rc, w * rs, 0, 0, h * (-rs), h * rc, 0, 0, 0, 0, 1, 0, rcx - (rc * hw) + (rs * hh), rcy - (rs * hw) - (rc * hh), 0, 1,
        };
        nt_sprite_renderer_emit_region(atlas, region_index, m, 0.0F, 0.0F, color_packed, 0U);
    }
}
// #endregion

// #region helper_clamp_radii_css3
/* CSS3 border-radius §5.5: scale all four by smallest factor so adjacent sums fit. */
static inline void clamp_radii_css3(float w, float h, float *tl, float *tr, float *bl, float *br) {
    *tl = (*tl > 0.0F) ? *tl : 0.0F;
    *tr = (*tr > 0.0F) ? *tr : 0.0F;
    *bl = (*bl > 0.0F) ? *bl : 0.0F;
    *br = (*br > 0.0F) ? *br : 0.0F;
    float factor = 1.0F;
    if (*tl + *tr > w) {
        factor = fminf(factor, w / (*tl + *tr));
    }
    if (*bl + *br > w) {
        factor = fminf(factor, w / (*bl + *br));
    }
    if (*tl + *bl > h) {
        factor = fminf(factor, h / (*tl + *bl));
    }
    if (*tr + *br > h) {
        factor = fminf(factor, h / (*tr + *br));
    }
    if (factor < 1.0F) {
        *tl *= factor;
        *tr *= factor;
        *bl *= factor;
        *br *= factor;
    }
}
// #endregion

// #region helper_emit_rounded_rect
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_rounded_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, Clay_CornerRadius cr, uint32_t color_packed, float rotation) {
    float tl = cr.topLeft;
    float tr = cr.topRight;
    float bl = cr.bottomLeft;
    float br = cr.bottomRight;
    clamp_radii_css3(w, h, &tl, &tr, &bl, &br);
    const float half_w = w * 0.5F;
    const float half_h = h * 0.5F;

    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_screen_rect(atlas, region_index, x, y, w, h, color_packed, rotation);
        return;
    }

    float positions[1 + (4 * (NT_UI_CORNER_SEGMENTS + 1))][2];
    uint16_t indices[4 * (NT_UI_CORNER_SEGMENTS + 1) * 3];

    positions[0][0] = x + half_w;
    positions[0][1] = y + half_h;
    uint32_t vi = 1;

    /* LUT row per corner: TL=2, TR=3, BR=0, BL=1. */
    if (tl == 0.0F) {
        positions[vi][0] = x;
        positions[vi][1] = y;
        vi++;
    } else {
        const float cx = x + tl;
        const float cy = y + tl;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (tl * s_arc_lut[2][s].cos);
            positions[vi][1] = cy + (tl * s_arc_lut[2][s].sin);
            vi++;
        }
    }
    if (tr == 0.0F) {
        positions[vi][0] = x + w;
        positions[vi][1] = y;
        vi++;
    } else {
        const float cx = x + w - tr;
        const float cy = y + tr;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (tr * s_arc_lut[3][s].cos);
            positions[vi][1] = cy + (tr * s_arc_lut[3][s].sin);
            vi++;
        }
    }
    if (br == 0.0F) {
        positions[vi][0] = x + w;
        positions[vi][1] = y + h;
        vi++;
    } else {
        const float cx = x + w - br;
        const float cy = y + h - br;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (br * s_arc_lut[0][s].cos);
            positions[vi][1] = cy + (br * s_arc_lut[0][s].sin);
            vi++;
        }
    }
    if (bl == 0.0F) {
        positions[vi][0] = x;
        positions[vi][1] = y + h;
        vi++;
    } else {
        const float cx = x + bl;
        const float cy = y + h - bl;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (bl * s_arc_lut[1][s].cos);
            positions[vi][1] = cy + (bl * s_arc_lut[1][s].sin);
            vi++;
        }
    }

    /* Triangle fan (center=0, i, i+1), wrap last to 1. */
    uint32_t ii = 0;
    for (uint32_t i = 1; i < vi; i++) {
        const uint16_t next = (uint16_t)((i + 1 < vi) ? (i + 1) : 1);
        indices[ii++] = 0U;
        indices[ii++] = (uint16_t)i;
        indices[ii++] = next;
    }

    if (rotation == 0.0F) {
        const float identity[16] = {
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        };
        nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, identity, color_packed);
    } else {
        /* Rotate around rect center: T(cx,cy) * R(rot) * T(-cx,-cy) */
        const float rcx = x + half_w;
        const float rcy = y + half_h;
        const float rc = cosf(rotation);
        const float rs = sinf(rotation);
        const float mat[16] = {
            rc, rs, 0, 0, -rs, rc, 0, 0, 0, 0, 1, 0, rcx - (rc * rcx) + (rs * rcy), rcy - (rs * rcx) - (rc * rcy), 0, 1,
        };
        nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, mat, color_packed);
    }
}
// #endregion

// #region helper_emit_border
/* Top/bottom run full width; left/right inset to avoid corner overlap.
 * With rotation, emits all segments as one mesh rotated around border center. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_square_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, uint32_t col, float rotation) {
    const float top = (float)widths.top;
    const float bot = (float)widths.bottom;
    const float lft = (float)widths.left;
    const float rgt = (float)widths.right;
    if (rotation == 0.0F) {
        if (widths.top) {
            emit_screen_rect(atlas, region_index, bb.x, bb.y, bb.width, top, col, 0.0F);
        }
        if (widths.bottom) {
            emit_screen_rect(atlas, region_index, bb.x, bb.y + bb.height - bot, bb.width, bot, col, 0.0F);
        }
        if (widths.left) {
            emit_screen_rect(atlas, region_index, bb.x, bb.y + top, lft, bb.height - top - bot, col, 0.0F);
        }
        if (widths.right) {
            emit_screen_rect(atlas, region_index, bb.x + bb.width - rgt, bb.y + top, rgt, bb.height - top - bot, col, 0.0F);
        }
        return;
    }
    /* Build quads as geometry; rotate around border center. Max 4 quads = 16 verts, 24 indices. */
    float positions[16][2];
    uint16_t indices[24];
    uint32_t vi = 0;
    uint32_t ii = 0;
    if (widths.top) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + top;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (widths.bottom) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + bb.height;
        vi++;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + bb.height;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (widths.left) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + lft;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + lft;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (widths.right) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x + bb.width - rgt;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x + bb.width - rgt;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (vi == 0) {
        return;
    }
    const float rcx = bb.x + (bb.width * 0.5F);
    const float rcy = bb.y + (bb.height * 0.5F);
    const float rc = cosf(rotation);
    const float rs = sinf(rotation);
    const float mat[16] = {
        rc, rs, 0, 0, -rs, rc, 0, 0, 0, 0, 1, 0, rcx - (rc * rcx) + (rs * rcy), rcy - (rs * rcx) - (rc * rcy), 0, 1,
    };
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, mat, col);
}

static uint32_t emit_corner_strip_pairs(float (*pos)[2], uint32_t vi, float radius, float cx, float cy, float w_perp_x, float w_perp_y, float sharp_x, float sharp_y, float sign_x, float sign_y,
                                        uint32_t quadrant) {
    if (radius == 0.0F) {
        pos[vi][0] = sharp_x;
        pos[vi][1] = sharp_y;
        vi++;
        pos[vi][0] = sharp_x + (sign_x * w_perp_x);
        pos[vi][1] = sharp_y + (sign_y * w_perp_y);
        vi++;
        return vi;
    }
    /* width > radius -> inner curve collapses to 0 on that axis (CSS parity). */
    const float irx = (radius > w_perp_x) ? (radius - w_perp_x) : 0.0F;
    const float iry = (radius > w_perp_y) ? (radius - w_perp_y) : 0.0F;
    for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
        const float cc = s_arc_lut[quadrant][s].cos;
        const float ss = s_arc_lut[quadrant][s].sin;
        pos[vi][0] = cx + (radius * cc);
        pos[vi][1] = cy + (radius * ss);
        vi++;
        pos[vi][0] = cx + (irx * cc);
        pos[vi][1] = cy + (iry * ss);
        vi++;
    }
    return vi;
}

/* Caller (emit_border) clamps radii and guarantees at least one is non-zero. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_rounded_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, float tl, float tr, float bl, float br, uint32_t color_packed,
                                float rotation) {
    const float x = bb.x;
    const float y = bb.y;
    const float w = bb.width;
    const float h = bb.height;
    const float top = (float)widths.top;
    const float bot = (float)widths.bottom;
    const float lft = (float)widths.left;
    const float rgt = (float)widths.right;

    float positions[4 * (NT_UI_CORNER_SEGMENTS + 1) * 2][2];
    uint32_t vi = 0;
    vi = emit_corner_strip_pairs(positions, vi, tl, x + tl, y + tl, lft, top, x, y, 1.0F, 1.0F, 2U);
    vi = emit_corner_strip_pairs(positions, vi, tr, x + w - tr, y + tr, rgt, top, x + w, y, -1.0F, 1.0F, 3U);
    vi = emit_corner_strip_pairs(positions, vi, br, x + w - br, y + h - br, rgt, bot, x + w, y + h, -1.0F, -1.0F, 0U);
    vi = emit_corner_strip_pairs(positions, vi, bl, x + bl, y + h - bl, lft, bot, x, y + h, 1.0F, -1.0F, 1U);

    /* Triangle strip with wrap: pair k at (outer=2k, inner=2k+1). */
    const uint32_t pair_count = vi / 2;
    uint16_t indices[4 * (NT_UI_CORNER_SEGMENTS + 1) * 6];
    uint32_t ii = 0;
    for (uint32_t k = 0; k < pair_count; k++) {
        const uint32_t k_next = (k + 1 < pair_count) ? (k + 1) : 0;
        const uint16_t out_k = (uint16_t)(2 * k);
        const uint16_t in_k = (uint16_t)((2 * k) + 1);
        const uint16_t out_n = (uint16_t)(2 * k_next);
        const uint16_t in_n = (uint16_t)((2 * k_next) + 1);
        indices[ii++] = out_k;
        indices[ii++] = in_k;
        indices[ii++] = out_n;
        indices[ii++] = in_k;
        indices[ii++] = in_n;
        indices[ii++] = out_n;
    }

    if (rotation == 0.0F) {
        const float identity[16] = {
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        };
        nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, identity, color_packed);
    } else {
        /* Rotate around border center: T(cx,cy) * R(rot) * T(-cx,-cy) */
        const float rcx = bb.x + (bb.width * 0.5F);
        const float rcy = bb.y + (bb.height * 0.5F);
        const float rc = cosf(rotation);
        const float rs = sinf(rotation);
        const float mat[16] = {
            rc, rs, 0, 0, -rs, rc, 0, 0, 0, 0, 1, 0, rcx - (rc * rcx) + (rs * rcy), rcy - (rs * rcx) - (rc * rcy), 0, 1,
        };
        nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, mat, color_packed);
    }
}

static void emit_border(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, float rotation) {
    const Clay_BorderRenderData *b = &c->renderData.border;
    const Clay_BoundingBox bb = c->boundingBox;
    const float top = (float)b->width.top;
    const float bot = (float)b->width.bottom;
    const float lft = (float)b->width.left;
    const float rgt = (float)b->width.right;
    NT_ASSERT(top + bot <= bb.height && "nt_ui BORDER: top+bottom widths exceed bbox.height");
    NT_ASSERT(lft + rgt <= bb.width && "nt_ui BORDER: left+right widths exceed bbox.width");

    const uint32_t col = nt_color_pack_clay(b->color);
    float tl = b->cornerRadius.topLeft;
    float tr = b->cornerRadius.topRight;
    float bl = b->cornerRadius.bottomLeft;
    float br = b->cornerRadius.bottomRight;
    clamp_radii_css3(bb.width, bb.height, &tl, &tr, &bl, &br);

    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_square_border(ctx->atlas, ctx->white_region, bb, b->width, col, rotation);
        return;
    }
    emit_rounded_border(ctx->atlas, ctx->white_region, bb, b->width, tl, tr, bl, br, col, rotation);
}
// #endregion

// #region helper_emit_image
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_image(const Clay_RenderCommand *c, float rotation) {
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    NT_ASSERT(p != NULL && "nt_ui IMAGE: imageData must point to nt_ui_image_payload_t");
    NT_ASSERT(p->atlas.id != 0 && "nt_ui IMAGE payload: invalid atlas handle");
    const Clay_CornerRadius cr = c->renderData.image.cornerRadius;
    NT_ASSERT(cr.topLeft == 0.0F && cr.topRight == 0.0F && cr.bottomLeft == 0.0F && cr.bottomRight == 0.0F && "nt_ui IMAGE: cornerRadius unsupported; pre-bake into atlas");
    if (!nt_resource_is_ready(p->atlas)) {
        return; /* async-loading atlas */
    }

    const Clay_BoundingBox bb = c->boundingBox;

    /* Clay {0,0,0,0} backgroundColor means "untinted", not transparent. */
    Clay_Color tint = c->renderData.image.backgroundColor;
    const bool default_untinted = (tint.r == 0.0F && tint.g == 0.0F && tint.b == 0.0F && tint.a == 0.0F);
    const uint32_t col = default_untinted ? 0xFFFFFFFFU : nt_color_pack_clay(tint);

    const nt_texture_region_t *r = nt_atlas_get_region(p->atlas, p->region_index);
    if (r->vertex_count == 0U) {
        return; /* tombstone */
    }

    /* UI rotation center: default center (0.5), override from style flag. */
    const float ox = (p->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) ? p->origin_x : 0.5F;
    const float oy = (p->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) ? p->origin_y : 0.5F;

    /* Auto-slice9: flag OR non-zero lrtb = override; flag adds ability to
     * override with zeros (disable slice9). Backward compat: non-zero lrtb
     * works without flag. */
    const bool has_s9_override = (p->flags & NT_UI_IMAGE_SLICE9_OVERRIDE) || (p->slice9_override[0] | p->slice9_override[1] | p->slice9_override[2] | p->slice9_override[3]) != 0;
    const bool region_slice9 = (r->slice9_lrtb[0] | r->slice9_lrtb[1] | r->slice9_lrtb[2] | r->slice9_lrtb[3]) != 0;

    if (has_s9_override || region_slice9) {
        uint16_t sl;
        uint16_t sr;
        uint16_t st;
        uint16_t sb;
        if (has_s9_override) {
            sl = p->slice9_override[0];
            sr = p->slice9_override[1];
            st = p->slice9_override[2];
            sb = p->slice9_override[3];
        } else {
            sl = r->slice9_lrtb[0];
            sr = r->slice9_lrtb[1];
            st = r->slice9_lrtb[2];
            sb = r->slice9_lrtb[3];
        }
        nt_sprite_renderer_emit_slice9(p->atlas, p->region_index, bb.x, bb.y, bb.width, bb.height, sl, sr, st, sb, col, p->flip_bits, rotation);
        return;
    }

    const float ipu = nt_atlas_get_inverse_pixels_per_unit(p->atlas);
    const float src_w = (float)r->source_w * ipu;
    const float src_h = (float)r->source_h * ipu;
    NT_ASSERT(src_w > 0.0F && src_h > 0.0F && "nt_ui IMAGE: atlas region has zero source dimensions (broken atlas data)");
    const float sx_f = bb.width / src_w;
    const float sy_f = bb.height / src_h;

    /* UI images fill Clay bbox from top-left — origin (0,0) for positioning.
     * ox/oy from atlas/override only affects rotation center, not position. */
    if (rotation == 0.0F) {
        const float m[16] = {
            sx_f, 0.0F, 0.0F, 0.0F, 0.0F, sy_f, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, bb.x, bb.y, 0.0F, 1.0F,
        };
        nt_sprite_renderer_emit_region(p->atlas, p->region_index, m, 0.0F, 0.0F, col, p->flip_bits);
    } else {
        const float rcx = bb.x + (ox * bb.width);
        const float rcy = bb.y + (oy * bb.height);
        const float hw = ox * bb.width;
        const float hh = oy * bb.height;
        const float rc = cosf(rotation);
        const float rs = sinf(rotation);
        const float m[16] = {
            sx_f * rc, sx_f * rs, 0, 0, sy_f * (-rs), sy_f * rc, 0, 0, 0, 0, 1, 0, rcx - (rc * hw) + (rs * hh), rcy - (rs * hw) - (rc * hh), 0, 1,
        };
        nt_sprite_renderer_emit_region(p->atlas, p->region_index, m, 0.0F, 0.0F, col, p->flip_bits);
    }
}
// #endregion

// #region helper_emit_text
/* Pure text emit: no sprite renderer knowledge. dispatch_command handles
 * the sprite_flush before and the lazy sprite rebind after. */
static void emit_text(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, float text_scale, float text_rotation) {
    const Clay_TextRenderData *t = &c->renderData.text;
    NT_ASSERT((uint32_t)t->fontId < NT_UI_MAX_FONTS && "nt_ui TEXT: fontId >= NT_UI_MAX_FONTS");
    nt_font_t font = ctx->fonts[t->fontId];
    NT_ASSERT(nt_font_valid(font) && "nt_ui TEXT: font slot empty; call nt_ui_set_font first");

    nt_text_renderer_set_font(font);
    nt_text_renderer_set_material(ctx->text_material);

    const float font_size = (float)t->fontSize * text_scale;
    nt_font_metrics_t metrics = nt_font_get_metrics(font);
    const float scale = (metrics.units_per_em > 0) ? (font_size / (float)metrics.units_per_em) : 0.0F;
    const float text_h = (float)(metrics.ascent - metrics.descent) * scale;
    const float center_offset = (c->boundingBox.height - text_h) * 0.5F;
    const float baseline_y = c->boundingBox.y + center_offset + ((float)(-metrics.descent) * scale);

    float m[16];
    if (text_rotation == 0.0F) {
        const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, c->boundingBox.x, baseline_y, 0, 1};
        memcpy(m, id, sizeof m);
    } else {
        /* M = T(center) * R(θ) * T(text_origin - center)
         * text_origin = (bbox.x, baseline_y), center = bbox center */
        const float bcx = c->boundingBox.x + (c->boundingBox.width * 0.5F);
        const float bcy = c->boundingBox.y + (c->boundingBox.height * 0.5F);
        const float dx = c->boundingBox.x - bcx;
        const float dy = baseline_y - bcy;
        const float rc = cosf(text_rotation);
        const float rs = sinf(text_rotation);
        const float rot[16] = {
            rc, rs, 0, 0, -rs, rc, 0, 0, 0, 0, 1, 0, bcx + (rc * dx) - (rs * dy), bcy + (rs * dx) + (rc * dy), 0, 1,
        };
        memcpy(m, rot, sizeof m);
    }
    const float color[4] = {
        t->textColor.r / 255.0F,
        t->textColor.g / 255.0F,
        t->textColor.b / 255.0F,
        t->textColor.a / 255.0F,
    };
    nt_text_renderer_draw_n(t->stringContents.chars, (size_t)t->stringContents.length, m, font_size, color, (float)t->letterSpacing * text_scale, (float)t->lineHeight * text_scale);
}
// #endregion

// #region helper_scissor_stack
typedef struct {
    int x;
    int y;
    int w;
    int h;
} scissor_rect_t;

/* DIRECT: viewport is GL physical; Y-flip inside viewport rect.
 * SCALED: viewport is logical (Y top-down); scale+shift to physical; Y-flip
 * against fb height for GL. Floor/ceil avoid 1-px sliver clipping. */
static void apply_scissor_logical_to_physical(const nt_ui_target_t *target, int x, int y, int wp, int hp) {
    const float vx = target->viewport[0];
    const float vy = target->viewport[1];
    const float vw = target->viewport[2];
    const float vh = target->viewport[3];

    if (target->fb_size[0] <= 0.0F || target->fb_size[1] <= 0.0F) {
        nt_gfx_set_scissor((int)vx + x, (int)(vy + vh) - y - hp, wp, hp);
        return;
    }

    const float ox = target->fb_offset[0];
    const float oy = target->fb_offset[1];
    const float fbh = target->fb_size[1];
    const float sx = (vw > 0.0F) ? ((target->fb_size[0] - (2.0F * ox)) / vw) : 1.0F;
    const float sy = (vh > 0.0F) ? ((fbh - (2.0F * oy)) / vh) : 1.0F;
    const int phys_x = (int)floorf(ox + (sx * (vx + (float)x)));
    const int phys_y_top = (int)floorf(oy + (sy * (vy + (float)y)));
    const int phys_w = (int)ceilf(sx * (float)wp);
    const int phys_h = (int)ceilf(sy * (float)hp);
    const int phys_y_gl = (int)fbh - phys_y_top - phys_h;
    nt_gfx_set_scissor(phys_x, phys_y_gl, phys_w, phys_h);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scissor_push(const Clay_RenderCommand *c, scissor_rect_t *stack, int *depth, const nt_ui_target_t *target, bool *sprite_pipeline_dirty) {
    NT_ASSERT((uint32_t)*depth < NT_UI_WALKER_SCISSOR_DEPTH_CAP && "scissor stack overflow; restructure nested clip");

    /* Unclipped axis falls back to viewport; floor/ceil avoid 1px right/bottom bite.
     * Both axes false is RESERVED for Clay's floating clipTo=ATTACHED_PARENT marker
     * (clay.h:2695-2701) -- bbox is the parent's clip area, applied to both axes.
     * User code must always set at least one axis true; both-false with degenerate
     * bbox is invalid use and trips the assert below. */
    const Clay_ClipRenderData *clip = &c->renderData.clip;
    const bool both_false = !clip->horizontal && !clip->vertical;
    NT_ASSERT((!both_false || (c->boundingBox.width > 0.0F && c->boundingBox.height > 0.0F)) &&
              "nt_ui SCISSOR_START with both axes false requires non-empty bbox (reserved for Clay floating clipTo marker)");
    const int vw = (int)target->viewport[2];
    const int vh = (int)target->viewport[3];
    const bool clip_h = clip->horizontal || both_false;
    const bool clip_v = clip->vertical || both_false;
    int x;
    int y;
    int wp;
    int hp;
    if (clip_h) {
        const float bx = c->boundingBox.x;
        x = (int)floorf(bx);
        wp = (int)ceilf(bx + c->boundingBox.width) - x;
    } else {
        x = 0;
        wp = vw;
    }
    if (clip_v) {
        const float by = c->boundingBox.y;
        y = (int)floorf(by);
        hp = (int)ceilf(by + c->boundingBox.height) - y;
    } else {
        y = 0;
        hp = vh;
    }

    /* Intersect with parent so inner widgets can't escape outer clip. */
    if (*depth > 0) {
        scissor_rect_t t = stack[*depth - 1];
        int x2 = (x > t.x) ? x : t.x;
        int y2 = (y > t.y) ? y : t.y;
        int r2 = ((x + wp) < (t.x + t.w)) ? (x + wp) : (t.x + t.w);
        int b2 = ((y + hp) < (t.y + t.h)) ? (y + hp) : (t.y + t.h);
        x = x2;
        y = y2;
        wp = (r2 > x2) ? (r2 - x2) : 0;
        hp = (b2 > y2) ? (b2 - y2) : 0;
    }

    /* Flush BEFORE scissor switch so staging keeps prior clip. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    *sprite_pipeline_dirty = true;

    stack[(*depth)++] = (scissor_rect_t){.x = x, .y = y, .w = wp, .h = hp};

    apply_scissor_logical_to_physical(target, x, y, wp, hp);
    nt_gfx_set_scissor_enabled(true);
}

static void scissor_pop(scissor_rect_t *stack, int *depth, const nt_ui_target_t *target, bool *sprite_pipeline_dirty) {
    NT_ASSERT(*depth > 0 && "scissor underflow");
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    *sprite_pipeline_dirty = true;
    (*depth)--;
    if (*depth == 0) {
        nt_gfx_set_scissor_enabled(false);
    } else {
        scissor_rect_t r = stack[*depth - 1];
        apply_scissor_logical_to_physical(target, r.x, r.y, r.w, r.h);
    }
}
// #endregion

// #region helper_emit_custom
/* Walker-local transform/opacity state passed through dispatch_command.
 * Transform is a 2D affine: pos' = pos * aff_s + (aff_tx, aff_ty).
 * Scale center is deferred: CUSTOM push marker has a zero-size bbox
 * (it's a sibling before the panel), so we capture the center from the
 * first renderable element (IMAGE/RECT) after the push. */
typedef struct {
    nt_ui_transform_t transform_stack[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    float push_center_x[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    float push_center_y[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    int transform_depth;
    /* 2D affine: x'=x*a+y*b+tx, y'=x*c+y*d+ty */
    float aff_a, aff_b, aff_c, aff_d, aff_tx, aff_ty;
    float accum_scale_x;
    float accum_scale_y;
    float accum_rotation;
    int pending_center_stack[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    int pending_center_count;
    bool center_resolved[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    float opacity_stack[NT_UI_OPACITY_STACK_DEPTH_CAP];
    int opacity_depth;
    float accum_opacity;
} nt_ui_walker_state_t;

static void walker_state_init(nt_ui_walker_state_t *ws) {
    ws->transform_depth = 0;
    ws->aff_a = 1.0F;
    ws->aff_b = 0;
    ws->aff_c = 0;
    ws->aff_d = 1.0F;
    ws->aff_tx = 0;
    ws->aff_ty = 0;
    ws->accum_scale_x = 1.0F;
    ws->accum_scale_y = 1.0F;
    ws->accum_rotation = 0;
    ws->pending_center_count = 0;
    memset(ws->center_resolved, 0, sizeof(ws->center_resolved));
    ws->opacity_depth = 0;
    ws->accum_opacity = 1.0F;
}

/* Compose ONE transform level (scale S, rotation θ, center C, offset O) into
 * the accumulated affine (a,b,c,d,tx,ty). Local = T(O)*T(C)*R(θ)*S*T(-C),
 * then new = local * accumulated. Pure Clay Y-down math: NO Y-flip, NO rotation
 * negation -- those are render-only conversions applied in dispatch_command.
 * Shared by the walker (render) and ui_hit_test (interaction) so both agree. */
static void compose_transform_level(const nt_ui_transform_t *t, float cx, float cy, float *a, float *b, float *c, float *d, float *tx, float *ty) {
    const float sx = t->scale_x;
    const float sy = t->scale_y;
    const float cr = cosf(t->rotation);
    const float sr = sinf(t->rotation);
    /* Local 2x2: la=cr*sx, lb=-sr*sy, lc=sr*sx, ld=cr*sy */
    const float la = cr * sx;
    const float lb = -(sr * sy);
    const float lc = sr * sx;
    const float ld = cr * sy;
    /* Local translate about center C + offset O. */
    const float ltx = cx - (la * cx) - (lb * cy) + t->offset_x;
    const float lty = cy - (lc * cx) - (ld * cy) + t->offset_y;
    /* Compose: new = local * accumulated. */
    const float na = (la * *a) + (lb * *c);
    const float nb = (la * *b) + (lb * *d);
    const float nc = (lc * *a) + (ld * *c);
    const float nd = (lc * *b) + (ld * *d);
    const float ntx = (la * *tx) + (lb * *ty) + ltx;
    const float nty = (lc * *tx) + (ld * *ty) + lty;
    *a = na;
    *b = nb;
    *c = nc;
    *d = nd;
    *tx = ntx;
    *ty = nty;
}

/* Compose local transform (scale S, rotation θ, center C, offset O) onto
 * accumulated affine. Local = T(O) * T(C) * R(θ)*S * T(-C). */
static void walker_recompute_transform(nt_ui_walker_state_t *ws) {
    ws->aff_a = 1.0F;
    ws->aff_b = 0;
    ws->aff_c = 0;
    ws->aff_d = 1.0F;
    ws->aff_tx = 0;
    ws->aff_ty = 0;
    ws->accum_scale_x = 1.0F;
    ws->accum_scale_y = 1.0F;
    ws->accum_rotation = 0;
    for (int k = 0; k < ws->transform_depth; ++k) {
        compose_transform_level(&ws->transform_stack[k], ws->push_center_x[k], ws->push_center_y[k], &ws->aff_a, &ws->aff_b, &ws->aff_c, &ws->aff_d, &ws->aff_tx, &ws->aff_ty);
        ws->accum_scale_x *= ws->transform_stack[k].scale_x;
        ws->accum_scale_y *= ws->transform_stack[k].scale_y;
    }
    /* Extract actual rotation from composed affine matrix.
     * atan2(c,a) assumes positive scale; negative scale flips the angle. */
    NT_ASSERT(ws->accum_scale_x > 0.0F && ws->accum_scale_y > 0.0F && "negative UI scale breaks atan2 rotation extraction");
    ws->accum_rotation = atan2f(ws->aff_c, ws->aff_a);
}

static void walker_recompute_opacity(nt_ui_walker_state_t *ws) {
    ws->accum_opacity = 1.0F;
    for (int k = 0; k < ws->opacity_depth; ++k) {
        ws->accum_opacity *= ws->opacity_stack[k];
    }
}

/* Apply accumulated opacity to a packed AABBGGRR color. */
static inline uint32_t apply_opacity(uint32_t color_packed, float opacity) {
    if (opacity >= 1.0F) {
        return color_packed;
    }
    uint32_t a = (color_packed >> 24) & 0xFFU;
    a = (uint32_t)((float)a * opacity);
    if (a > 255U) {
        a = 255U;
    }
    return (color_packed & 0x00FFFFFFU) | (a << 24);
}

/* Phase 55: per-walk counters passed to dispatch helpers. */
typedef struct {
    uint32_t rect_command_count;
    uint32_t image_command_count;
    uint32_t text_command_count;
    uint32_t border_command_count;
    uint32_t scissor_command_count;
    uint32_t max_scissor_depth;
    uint32_t transform_pushes;
    uint32_t opacity_pushes;
} nt_ui_walk_counters_t;

/* Process a single side-channel marker into walker state. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void process_marker(const nt_ui_marker_t *marker, nt_ui_walker_state_t *ws, nt_ui_walk_counters_t *counters) {
    switch (marker->type) {
    case NT_UI_MARKER_PUSH_TRANSFORM: {
        counters->transform_pushes++;
        NT_ASSERT(ws->transform_depth < NT_UI_TRANSFORM_STACK_DEPTH_CAP && "transform stack overflow");
        const int d = ws->transform_depth;
        ws->transform_stack[d] = marker->transform;
        ws->push_center_x[d] = 0;
        ws->push_center_y[d] = 0;
        ws->center_resolved[d] = true;
        ws->transform_depth = d + 1;
        /* Offset applies immediately; scale/rotation center deferred. */
        ws->aff_tx += marker->transform.offset_x;
        ws->aff_ty += marker->transform.offset_y;
        if (marker->transform.scale_x != 1.0F || marker->transform.scale_y != 1.0F || marker->transform.rotation != 0.0F) {
            ws->center_resolved[d] = false;
            NT_ASSERT(ws->pending_center_count < NT_UI_TRANSFORM_STACK_DEPTH_CAP && "pending center stack overflow");
            ws->pending_center_stack[ws->pending_center_count++] = d;
        }
        return;
    }
    case NT_UI_MARKER_POP_TRANSFORM:
        NT_ASSERT(ws->transform_depth > 0 && "transform stack underflow");
        --ws->transform_depth;
        /* Unresolved center uses (0,0) — acceptable for scale=0 (hide) or offset-only transforms. */
        /* Remove any pending entries for this depth. */
        while (ws->pending_center_count > 0 && ws->pending_center_stack[ws->pending_center_count - 1] >= ws->transform_depth) {
            --ws->pending_center_count;
        }
        walker_recompute_transform(ws);
        return;
    case NT_UI_MARKER_PUSH_OPACITY:
        counters->opacity_pushes++;
        NT_ASSERT(ws->opacity_depth < NT_UI_OPACITY_STACK_DEPTH_CAP && "opacity stack overflow");
        ws->opacity_stack[ws->opacity_depth++] = marker->opacity;
        ws->accum_opacity *= marker->opacity;
        return;
    case NT_UI_MARKER_POP_OPACITY:
        NT_ASSERT(ws->opacity_depth > 0 && "opacity stack underflow");
        --ws->opacity_depth;
        walker_recompute_opacity(ws);
        return;
    default:
        NT_ASSERT(false && "unknown marker type");
        return;
    }
}

/* Typed CUSTOM dispatch: engine anchors (type=NONE) skip silently;
 * game handlers (type=GAME) flush and invoke the custom callback. */
static void emit_custom(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, bool *sprite_pipeline_dirty) {
    const nt_ui_custom_data_t *cd = (const nt_ui_custom_data_t *)c->renderData.custom.customData;
    NT_ASSERT(cd != NULL && "CUSTOM command must have nt_ui_custom_data_t");
    if (cd->type == NT_UI_CUSTOM_TYPE_NONE) {
        return;
    }
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    *sprite_pipeline_dirty = true;
    if (ctx->custom_fn != NULL) {
        ctx->custom_fn((const void *)c, ctx->custom_user);
    }
}
// #endregion

// #region walk
/* SCISSOR/CUSTOM/NONE = hard barriers; never reordered. */
static bool is_segmentable(Clay_RenderCommandType cmd_type) {
    switch (cmd_type) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
    case CLAY_RENDER_COMMAND_TYPE_BORDER:
    case CLAY_RENDER_COMMAND_TYPE_IMAGE:
    case CLAY_RENDER_COMMAND_TYPE_TEXT:
        return true;
    default:
        return false;
    }
}

/* Sprite-backed dispatch: drain pending text, lazy-rebind sprite cmd if a
 * prior text/scissor/custom closed it. */
static inline void prep_sprite_dispatch(const nt_ui_context_t *ctx, bool *sprite_pipeline_dirty) {
    nt_text_renderer_flush();
    if (*sprite_pipeline_dirty) {
        nt_sprite_renderer_set_material(ctx->sprite_material);
        *sprite_pipeline_dirty = false;
    }
}

/* Clay bbox is top-left Y-down; engine renders GL bottom-left Y-up. Local-copy
 * bbox.y rewrite + corner-radii/border-width top<->bottom swap mirrors the
 * scissor_push flip. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void dispatch_command(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, scissor_rect_t *scissor_stack, int *depth, const nt_ui_target_t *target, bool *sprite_pipeline_dirty,
                             nt_ui_walker_state_t *ws, nt_ui_walk_counters_t *counters) {
    const float vy = target->viewport[1];
    const float vh = target->viewport[3];

    /* Apply accumulated 2D affine: pos' = pos * M + T.
     * Position uses full affine; size uses accum_scale (uniform).
     * Rotation negated for renderers: affine is Clay Y-down, GL is Y-up. */
    const float scx = ws->accum_scale_x;
    const float scy = ws->accum_scale_y;
    const float orig_cx = c->boundingBox.x + (c->boundingBox.width * 0.5F);
    const float orig_cy = c->boundingBox.y + (c->boundingBox.height * 0.5F);
    const float tcx = (orig_cx * ws->aff_a) + (orig_cy * ws->aff_b) + ws->aff_tx;
    const float tcy = (orig_cx * ws->aff_c) + (orig_cy * ws->aff_d) + ws->aff_ty;
    const float sw = c->boundingBox.width * scx;
    const float sh = c->boundingBox.height * scy;
    Clay_BoundingBox sbb = {.x = tcx - (sw * 0.5F), .y = tcy - (sh * 0.5F), .width = sw, .height = sh};
    const float world_y = vy + vh - sbb.y - sbb.height;

    switch (c->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
        return;
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
        counters->rect_command_count++;
        prep_sprite_dispatch(ctx, sprite_pipeline_dirty);
        const Clay_RectangleRenderData *r = &c->renderData.rectangle;
        uint32_t col = nt_color_pack_clay(r->backgroundColor);
        col = apply_opacity(col, ws->accum_opacity);
        const Clay_CornerRadius cr = {
            .topLeft = r->cornerRadius.bottomLeft,
            .topRight = r->cornerRadius.bottomRight,
            .bottomLeft = r->cornerRadius.topLeft,
            .bottomRight = r->cornerRadius.topRight,
        };
        emit_rounded_rect(ctx->atlas, ctx->white_region, sbb.x, world_y, sbb.width, sbb.height, cr, col, -ws->accum_rotation);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
        counters->border_command_count++;
        prep_sprite_dispatch(ctx, sprite_pipeline_dirty);
        Clay_RenderCommand local = *c;
        local.boundingBox = (Clay_BoundingBox){.x = sbb.x, .y = world_y, .width = sbb.width, .height = sbb.height};
        Clay_BorderRenderData *b = &local.renderData.border;
        const Clay_BorderWidth wo = b->width;
        b->width.top = wo.bottom;
        b->width.bottom = wo.top;
        const Clay_CornerRadius cro = b->cornerRadius;
        b->cornerRadius.topLeft = cro.bottomLeft;
        b->cornerRadius.topRight = cro.bottomRight;
        b->cornerRadius.bottomLeft = cro.topLeft;
        b->cornerRadius.bottomRight = cro.topRight;
        /* Apply opacity to border color */
        b->color.a *= ws->accum_opacity;
        emit_border(ctx, &local, -ws->accum_rotation);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
        counters->text_command_count++;
        /* Drain sprite before switching to text pipeline; mark for lazy rebind. */
        nt_sprite_renderer_flush();
        *sprite_pipeline_dirty = true;
        Clay_RenderCommand local = *c;
        local.boundingBox = (Clay_BoundingBox){.x = sbb.x, .y = world_y, .width = sbb.width, .height = sbb.height};
        local.renderData.text.textColor.a *= ws->accum_opacity;
        emit_text(ctx, &local, fmaxf(scx, scy), -ws->accum_rotation);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
        counters->image_command_count++;
        prep_sprite_dispatch(ctx, sprite_pipeline_dirty);
        Clay_RenderCommand local = *c;
        local.boundingBox = (Clay_BoundingBox){.x = sbb.x, .y = world_y, .width = sbb.width, .height = sbb.height};
        /* Modify backgroundColor alpha for opacity -- emit_image reads it. */
        if (ws->accum_opacity < 1.0F) {
            Clay_Color tint = local.renderData.image.backgroundColor;
            const bool untinted = (tint.r == 0.0F && tint.g == 0.0F && tint.b == 0.0F && tint.a == 0.0F);
            if (untinted) {
                /* Default "untinted" maps to 0xFFFFFFFF; apply opacity. */
                local.renderData.image.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = 255.0F * ws->accum_opacity};
            } else {
                local.renderData.image.backgroundColor.a *= ws->accum_opacity;
            }
        }
        emit_image(&local, -ws->accum_rotation);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
        counters->scissor_command_count++;
        Clay_RenderCommand local = *c;
        if (ws->accum_rotation != 0.0F) {
            /* AABB of rotated scissor rect in Clay Y-down space */
            const float cx = sbb.x + (sbb.width * 0.5F);
            const float cy = sbb.y + (sbb.height * 0.5F);
            const float hw = sbb.width * 0.5F;
            const float hh = sbb.height * 0.5F;
            const float rot = ws->accum_rotation;
            const float rc = cosf(rot);
            const float rs = sinf(rot);
            const float corners[4][2] = {{cx - hw, cy - hh}, {cx + hw, cy - hh}, {cx + hw, cy + hh}, {cx - hw, cy + hh}};
            float mn_x = corners[0][0];
            float mn_y = corners[0][1];
            float mx_x = corners[0][0];
            float mx_y = corners[0][1];
            for (int ci = 0; ci < 4; ci++) {
                const float dx = corners[ci][0] - cx;
                const float dy = corners[ci][1] - cy;
                const float rx = (dx * rc) - (dy * rs) + cx;
                const float ry = (dx * rs) + (dy * rc) + cy;
                if (rx < mn_x) {
                    mn_x = rx;
                }
                if (ry < mn_y) {
                    mn_y = ry;
                }
                if (rx > mx_x) {
                    mx_x = rx;
                }
                if (ry > mx_y) {
                    mx_y = ry;
                }
            }
            local.boundingBox = (Clay_BoundingBox){.x = mn_x, .y = mn_y, .width = mx_x - mn_x, .height = mx_y - mn_y};
        } else if (scx != 1.0F || scy != 1.0F || ws->aff_tx != 0.0F || ws->aff_ty != 0.0F) {
            /* No rotation but has offset/scale: use transformed sbb directly */
            local.boundingBox = sbb;
        }
        scissor_push(&local, scissor_stack, depth, target, sprite_pipeline_dirty);
        if ((uint32_t)*depth > counters->max_scissor_depth) {
            counters->max_scissor_depth = (uint32_t)*depth;
        }
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        scissor_pop(scissor_stack, depth, target, sprite_pipeline_dirty);
        return;
    case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
        Clay_RenderCommand local = *c;
        local.boundingBox = (Clay_BoundingBox){.x = sbb.x, .y = world_y, .width = sbb.width, .height = sbb.height};
        emit_custom(ctx, &local, sprite_pipeline_dirty);
        return;
    }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    NT_ASSERT(ctx != NULL && "nt_ui_walk: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_walk: target must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_walk: ctx is mid-frame (call nt_ui_end first)");
    NT_ASSERT(ctx->frozen_cmds.internalArray != NULL && "nt_ui_walk: frozen_cmds not populated (call nt_ui_end before walk)");
    NT_ASSERT(isfinite(target->viewport[0]) && isfinite(target->viewport[1]) && isfinite(target->viewport[2]) && isfinite(target->viewport[3]) && "nt_ui_walk: target->viewport must be finite");
    NT_ASSERT(target->viewport[0] >= 0.0F && target->viewport[1] >= 0.0F && "nt_ui_walk: target->viewport origin must be non-negative");
    NT_ASSERT(target->viewport[2] >= 0.0F && target->viewport[3] >= 0.0F && "nt_ui_walk: target->viewport (w,h) must be non-negative");

    /* Walker owns GL scissor state: disables on entry, manages via
     * SCISSOR_START/END pushes, disables on exit. Caller's scissor is
     * not preserved across nt_ui_walk(). Drains BEFORE zero-viewport
     * early return so leaked staging doesn't survive a minimized frame. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    nt_gfx_set_scissor_enabled(false);

    /* Zero viewport or degenerate fb (minimized tab, orientation change): no-op. */
    const bool scaled = target->fb_size[0] > 0.0F;
    if (target->viewport[2] == 0.0F || target->viewport[3] == 0.0F || (scaled && target->fb_size[1] == 0.0F)) {
        ctx->last_walk_draw_call_delta = 0;
        ctx->last_walk_command_count = 0;
        ctx->last_walk_ms = 0.0F;
        ctx->last_walk_rect_command_count = 0;
        ctx->last_walk_image_command_count = 0;
        ctx->last_walk_text_command_count = 0;
        ctx->last_walk_border_command_count = 0;
        ctx->last_walk_scissor_command_count = 0;
        ctx->last_walk_max_scissor_depth = 0;
        ctx->last_walk_transform_pushes = 0;
        ctx->last_walk_opacity_pushes = 0;
#ifdef NT_TEST_ACCESS
        ctx->test_last_walk_unlayered_count = 0;
#endif
        return;
    }
    NT_ASSERT(ctx->sprite_material.id != 0 && "nt_ui_set_sprite_material(ctx,...) required before nt_ui_walk");
    NT_ASSERT(ctx->text_material.id != 0 && "nt_ui_set_text_material(ctx,...) required before nt_ui_walk");

    /* Async-friendly: skip the walk silently if the ctx atlas is not
     * yet bound or still loading. Same policy as IMAGE p->atlas. Game
     * can start the main loop immediately; bind atlas once it reaches
     * READY (set_atlas_white_region needs resolved region data), UI
     * starts drawing on the next walk. */
    if (ctx->atlas.id == 0 || !nt_resource_is_ready(ctx->atlas)) {
        ctx->last_walk_draw_call_delta = 0;
        ctx->last_walk_command_count = 0;
        ctx->last_walk_ms = 0.0F;
        ctx->last_walk_rect_command_count = 0;
        ctx->last_walk_image_command_count = 0;
        ctx->last_walk_text_command_count = 0;
        ctx->last_walk_border_command_count = 0;
        ctx->last_walk_scissor_command_count = 0;
        ctx->last_walk_max_scissor_depth = 0;
        ctx->last_walk_transform_pushes = 0;
        ctx->last_walk_opacity_pushes = 0;
#ifdef NT_TEST_ACCESS
        ctx->test_last_walk_unlayered_count = 0;
#endif
        return;
    }

    /* Timed from here -- after the entry flush -- so walk_ms covers the UI walk's
     * own dispatch, not draining the caller's pending geometry (same scope as
     * last_walk_draw_call_delta below). */
    const double walk_t0 = nt_time_now();

    scissor_rect_t scissor_stack[NT_UI_WALKER_SCISSOR_DEPTH_CAP];
    int depth = 0;

    nt_ui_walker_state_t ws;
    walker_state_init(&ws);

    nt_ui_walk_counters_t counters = {0};

    /* AFTER entry flush so per-walk delta excludes caller's drained geometry. */
    const uint32_t calls_at_entry = nt_gfx_get_frame_draw_calls();

    /* glViewport needs PHYSICAL pixels. SCALED mode reads fb_size + fb_offset;
     * DIRECT mode viewport[] is already in physical px. */
    if (scaled) {
        /* Derive width from int offset to avoid rounding asymmetry (1px bar). */
        const int ox = (int)roundf(target->fb_offset[0]);
        const int oy = (int)roundf(target->fb_offset[1]);
        nt_gfx_set_viewport(ox, oy, (int)target->fb_size[0] - (2 * ox), (int)target->fb_size[1] - (2 * oy));
    } else {
        nt_gfx_set_viewport((int)target->viewport[0], (int)target->viewport[1], (int)target->viewport[2], (int)target->viewport[3]);
    }

    /* Sprite material up-front; text binds lazily inside emit_text. */
    nt_sprite_renderer_set_material(ctx->sprite_material);

    /* Sprite cmd open after set_material above; clean → no rebind needed. */
    bool sprite_pipeline_dirty = false;

    /* Bitmask layer dispatch + ctz: O(L_active × N) per segment, only set
     * bits visited (32 B stack vs ~2 KB for counting sort). */
    const Clay_RenderCommandArray *arr = &ctx->frozen_cmds;
#ifdef NT_TEST_ACCESS
    uint32_t unlayered_count = 0U;
#endif
    uint32_t mcur = 0U; /* side-channel marker cursor */
    int32_t i = 0;
    while (i < arr->length) {
        const Clay_RenderCommand *c = &arr->internalArray[i];
        if (!is_segmentable(c->commandType)) {
            /* Match markers by nt_layout_index — no remap needed. */
            while (mcur < ctx->marker_count && (int32_t)ctx->markers[mcur].before_clay_idx <= c->nt_layout_index) {
                process_marker(&ctx->markers[mcur], &ws, &counters);
                ++mcur;
            }
            /* Resolve pending centers from non-segmentable commands too (SCISSOR_START has valid bbox). */
            if (ws.pending_center_count > 0 && c->boundingBox.width > 0 && c->commandType != CLAY_RENDER_COMMAND_TYPE_NONE && c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
                const float rcx = c->boundingBox.x + (c->boundingBox.width * 0.5F);
                const float rcy = c->boundingBox.y + (c->boundingBox.height * 0.5F);
                for (int pi = 0; pi < ws.pending_center_count; ++pi) {
                    const int pd = ws.pending_center_stack[pi];
                    ws.push_center_x[pd] = rcx;
                    ws.push_center_y[pd] = rcy;
                    ws.center_resolved[pd] = true;
                }
                ws.pending_center_count = 0;
                walker_recompute_transform(&ws);
            }
            dispatch_command(ctx, c, scissor_stack, &depth, target, &sprite_pipeline_dirty, &ws, &counters);
            ++i;
            continue;
        }
        const int16_t seg_z = c->zIndex;
        int32_t seg_end = i + 1;
        while (seg_end < arr->length) {
            const Clay_RenderCommand *next = &arr->internalArray[seg_end];
            if (next->zIndex != seg_z || !is_segmentable(next->commandType)) {
                break;
            }
            ++seg_end;
        }
        const int32_t seg_n = seg_end - i;
        NT_ASSERT((uint32_t)seg_n <= ctx->max_elements && "nt_ui_walk: segment size exceeds ctx->max_elements; raise desc->max_elements or split via SCISSOR");

        /* Pre-pass: interleave side-channel markers with Clay commands to
         * bake transform/opacity per render-command index. Markers fire
         * before the Clay command whose declaration index they precede.
         * Skipped entirely when marker_count == 0 (identity transform). */
        typedef struct {
            float a, b, c, d, tx, ty;
            float scale_x, scale_y, rotation, opacity;
        } baked_xform_t;
        baked_xform_t *baked = NULL;

        uint32_t active_layers[8] = {0U};
        if (ctx->marker_count > 0) {
            baked = (baked_xform_t *)nt_mem_scratch_alloc(sizeof(baked_xform_t) * (size_t)seg_n, _Alignof(baked_xform_t));
            /* Sort indices by nt_layout_index so marker drain sees declaration
             * order regardless of Clay's z-sort on the render command array. */
            int32_t *sorted = (int32_t *)nt_mem_scratch_alloc(sizeof(int32_t) * (size_t)seg_n, _Alignof(int32_t));
            for (int32_t k = 0; k < seg_n; ++k) {
                sorted[k] = k;
            }
            // #region insertion sort by nt_layout_index
            for (int32_t k = 1; k < seg_n; ++k) {
                const int32_t key = sorted[k];
                const int32_t key_li = arr->internalArray[i + key].nt_layout_index;
                int32_t p = k - 1;
                while (p >= 0 && arr->internalArray[i + sorted[p]].nt_layout_index > key_li) {
                    sorted[p + 1] = sorted[p];
                    --p;
                }
                sorted[p + 1] = key;
            }
            // #endregion
            for (int32_t k = 0; k < seg_n; ++k) {
                const int32_t orig_idx = sorted[k];
                const Clay_RenderCommand *cc = &arr->internalArray[i + orig_idx];
                /* Match markers by nt_layout_index — sorted order restores declaration sequence. */
                while (mcur < ctx->marker_count && (int32_t)ctx->markers[mcur].before_clay_idx <= cc->nt_layout_index) {
                    process_marker(&ctx->markers[mcur], &ws, &counters);
                    ++mcur;
                }
                /* Resolve ALL pending centers from first command with valid bbox.
                 * SCISSOR_START has a bbox -- include it so transforms wrapping a
                 * clip-only subtree still resolve their center. */
                if (ws.pending_center_count > 0 && cc->boundingBox.width > 0 && cc->commandType != CLAY_RENDER_COMMAND_TYPE_NONE && cc->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
                    const float rcx = cc->boundingBox.x + (cc->boundingBox.width * 0.5F);
                    const float rcy = cc->boundingBox.y + (cc->boundingBox.height * 0.5F);
                    for (int pi = 0; pi < ws.pending_center_count; ++pi) {
                        const int pd = ws.pending_center_stack[pi];
                        ws.push_center_x[pd] = rcx;
                        ws.push_center_y[pd] = rcy;
                        ws.center_resolved[pd] = true;
                    }
                    ws.pending_center_count = 0;
                    walker_recompute_transform(&ws);
                }
                /* baked[] indexed by original array position for layer dispatch. */
                baked[orig_idx] = (baked_xform_t){
                    .a = ws.aff_a,
                    .b = ws.aff_b,
                    .c = ws.aff_c,
                    .d = ws.aff_d,
                    .tx = ws.aff_tx,
                    .ty = ws.aff_ty,
                    .scale_x = ws.accum_scale_x,
                    .scale_y = ws.accum_scale_y,
                    .rotation = ws.accum_rotation,
                    .opacity = ws.accum_opacity,
                };
                const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
                active_layers[layer >> 5U] |= (1U << (layer & 31U));
#ifdef NT_TEST_ACCESS
                if (cc->userData == NULL && cc->commandType != CLAY_RENDER_COMMAND_TYPE_CUSTOM) {
                    ++unlayered_count;
                }
#endif
            }
        } else {
            /* No markers: identity transform/opacity. Just collect layers. */
            for (int32_t j = i; j < seg_end; ++j) {
                const Clay_RenderCommand *cc = &arr->internalArray[j];
                const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
                active_layers[layer >> 5U] |= (1U << (layer & 31U));
#ifdef NT_TEST_ACCESS
                if (cc->userData == NULL && cc->commandType != CLAY_RENDER_COMMAND_TYPE_CUSTOM) {
                    ++unlayered_count;
                }
#endif
            }
        }

        /* Save walker state after pre-pass so layer passes (which overwrite
         * ws fields with per-command baked state) don't leak into the next segment. */
        nt_ui_walker_state_t ws_after_prepass = ws;

        /* Layer passes: dispatch renderables with baked transform state. */
        for (uint32_t word_idx = 0U; word_idx < 8U; ++word_idx) {
            uint32_t mask = active_layers[word_idx];
            while (mask != 0U) {
                const uint32_t bit_idx = (uint32_t)__builtin_ctz(mask);
                mask &= mask - 1U;
                const uint8_t current_layer = (uint8_t)((word_idx << 5U) | bit_idx);
                for (int32_t j = i; j < seg_end; ++j) {
                    const Clay_RenderCommand *cc = &arr->internalArray[j];
                    if (cc->commandType == CLAY_RENDER_COMMAND_TYPE_CUSTOM) {
                        continue;
                    }
                    const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
                    if (layer == current_layer) {
                        if (baked != NULL) {
                            const int32_t bi = j - i;
                            ws.aff_a = baked[bi].a;
                            ws.aff_b = baked[bi].b;
                            ws.aff_c = baked[bi].c;
                            ws.aff_d = baked[bi].d;
                            ws.aff_tx = baked[bi].tx;
                            ws.aff_ty = baked[bi].ty;
                            ws.accum_scale_x = baked[bi].scale_x;
                            ws.accum_scale_y = baked[bi].scale_y;
                            ws.accum_rotation = baked[bi].rotation;
                            ws.accum_opacity = baked[bi].opacity;
                        }
                        dispatch_command(ctx, cc, scissor_stack, &depth, target, &sprite_pipeline_dirty, &ws, &counters);
                    }
                }
            }
        }
        /* Restore chronological state so next segment sees correct ws. */
        ws = ws_after_prepass;
        i = seg_end;
    }

    /* Drain remaining markers (pops at end of frame). */
    while (mcur < ctx->marker_count) {
        process_marker(&ctx->markers[mcur], &ws, &counters);
        ++mcur;
    }

    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    NT_ASSERT(depth == 0 && "unbalanced scissor stack at walk exit");
    NT_ASSERT(ws.transform_depth == 0 && "unbalanced transform stack at walk exit");
    NT_ASSERT(ws.opacity_depth == 0 && "unbalanced opacity stack at walk exit");
    nt_gfx_set_scissor_enabled(false);

    /* Guard against CUSTOM handler resetting gfx counter -> unsigned wrap. */
    const uint32_t calls_after = nt_gfx_get_frame_draw_calls();
    NT_ASSERT(calls_after >= calls_at_entry && "nt_ui_walk: frame draw-call counter went backwards");
    ctx->last_walk_draw_call_delta = calls_after - calls_at_entry;
    ctx->last_walk_command_count = (uint32_t)arr->length;
    ctx->last_walk_rect_command_count = counters.rect_command_count;
    ctx->last_walk_image_command_count = counters.image_command_count;
    ctx->last_walk_text_command_count = counters.text_command_count;
    ctx->last_walk_border_command_count = counters.border_command_count;
    ctx->last_walk_scissor_command_count = counters.scissor_command_count;
    ctx->last_walk_max_scissor_depth = counters.max_scissor_depth;
    ctx->last_walk_transform_pushes = counters.transform_pushes;
    ctx->last_walk_opacity_pushes = counters.opacity_pushes;
    ctx->last_walk_ms = (float)((nt_time_now() - walk_t0) * 1000.0);
#ifdef NT_TEST_ACCESS
    ctx->test_last_walk_unlayered_count = unlayered_count;
#endif
}
// #endregion

// #region setters
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_set_atlas_white_region(nt_ui_context_t *ctx, nt_resource_t atlas, uint32_t white_region_idx) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_atlas_white_region: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_atlas_white_region: must be called outside begin/end");
    NT_ASSERT(atlas.id != 0 && "nt_ui_set_atlas_white_region: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_ui_set_atlas_white_region: atlas must be READY");
    const nt_texture_region_t *r = nt_atlas_get_region(atlas, white_region_idx);
    NT_ASSERT(r->vertex_count > 0U && "nt_ui_set_atlas_white_region: white region tombstoned");
    /* mat4(w,h) needs cached_pos {0,1}x{0,1}: 1x1 source AND PPU=1. */
    NT_ASSERT(r->source_w == 1 && r->source_h == 1 && "nt_ui_set_atlas_white_region: white region must be 1x1 source");
    NT_ASSERT(nt_atlas_get_inverse_pixels_per_unit(atlas) == 1.0F && "nt_ui_set_atlas_white_region: atlas must have PPU=1");
    ctx->atlas = atlas;
    ctx->white_region = white_region_idx;
}

void nt_ui_set_sprite_material(nt_ui_context_t *ctx, nt_material_t sprite_material) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_sprite_material: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_sprite_material: must be called outside begin/end");
    NT_ASSERT(sprite_material.id != 0 && "nt_ui_set_sprite_material: invalid material handle");
    ctx->sprite_material = sprite_material;
}

void nt_ui_set_text_material(nt_ui_context_t *ctx, nt_material_t text_material) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_text_material: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_text_material: must be called outside begin/end");
    NT_ASSERT(text_material.id != 0 && "nt_ui_set_text_material: invalid material handle");
    ctx->text_material = text_material;
}

void nt_ui_set_custom_handler(nt_ui_context_t *ctx, nt_ui_custom_handler_t fn, void *userdata) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_custom_handler: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_custom_handler: must be called outside begin/end");
    /* NULL fn legal: reserved-slot pattern. */
    ctx->custom_fn = fn;
    ctx->custom_user = userdata;
}
// #endregion

// #region push_pop_transform_opacity
static nt_ui_marker_t *emit_marker_base(nt_ui_context_t *ctx, uint8_t marker_type) {
    NT_ASSERT(ctx->marker_count < ctx->max_markers && "marker array full; raise max_markers in nt_ui_create_desc_t");
    nt_ui_marker_t *m = &ctx->markers[ctx->marker_count++];
    m->type = marker_type;
    m->before_clay_idx = (uint32_t)ctx->clay->layoutElements.length;
    return m;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_push_transform(nt_ui_context_t *ctx, const nt_ui_transform_t *transform) {
    NT_ASSERT(ctx != NULL && "nt_ui_push_transform: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_push_transform: must be called inside begin/end");
    NT_ASSERT(transform != NULL && "nt_ui_push_transform: transform must be non-NULL");
    NT_ASSERT(transform->scale_x > 0.0F && transform->scale_y > 0.0F && "nt_ui_push_transform: scale must be positive; use opacity=0 to hide");
    NT_ASSERT(isfinite(transform->scale_x) && isfinite(transform->scale_y) && "nt_ui_push_transform: scale must be finite");
    NT_ASSERT(isfinite(transform->rotation) && "nt_ui_push_transform: rotation must be finite");
    NT_ASSERT(isfinite(transform->offset_x) && isfinite(transform->offset_y) && "nt_ui_push_transform: offset must be finite");
    emit_marker_base(ctx, NT_UI_MARKER_PUSH_TRANSFORM)->transform = *transform;
    /* Phase 56: mirror onto the live accum stack for the hit-test (Option A). */
    NT_ASSERT(ctx->accum_depth < NT_UI_TRANSFORM_STACK_DEPTH_CAP && "transform accum overflow");
    ctx->accum_stack[ctx->accum_depth++] = *transform;
}

void nt_ui_pop_transform(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_pop_transform: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_pop_transform: must be called inside begin/end");
    emit_marker_base(ctx, NT_UI_MARKER_POP_TRANSFORM);
    /* Phase 56: keep the live accum stack balanced with push. */
    NT_ASSERT(ctx->accum_depth > 0 && "transform accum underflow");
    ctx->accum_depth--;
}

void nt_ui_push_opacity(nt_ui_context_t *ctx, float opacity) {
    NT_ASSERT(ctx != NULL && "nt_ui_push_opacity: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_push_opacity: must be called inside begin/end");
    NT_ASSERT(isfinite(opacity) && opacity >= 0.0F && opacity <= 1.0F && "nt_ui_push_opacity: must be finite in [0,1]");
    emit_marker_base(ctx, NT_UI_MARKER_PUSH_OPACITY)->opacity = opacity;
}

void nt_ui_pop_opacity(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_pop_opacity: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_pop_opacity: must be called inside begin/end");
    emit_marker_base(ctx, NT_UI_MARKER_POP_OPACITY);
}
// #endregion

// #region nt_ui_custom
void nt_ui_custom(nt_ui_context_t *ctx, const nt_ui_element_data_t *elem_data, void *data) {
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(ctx->in_frame);
    nt_ui_custom_data_t *cd = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_data_t);
    NT_ASSERT(cd != NULL);
    *cd = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_GAME, .data = data};
    CLAY({
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
        .custom = {.customData = cd},
        .userData = (void *)elem_data,
    });
}
// #endregion

// #region interaction_id_bbox_hittest
uint32_t nt_ui_id(const char *s) {
    NT_ASSERT(s != NULL && "nt_ui_id: string must be non-NULL");
    /* Clay hashes the string with its own one-at-a-time hash and returns
     * hash+1, so the result is never 0 (the no-widget sentinel, D-56-05). A
     * different hash (nt_hash/FNV) would miss Clay's hashmap (Pitfall 4). */
    return Clay_GetElementId((Clay_String){.length = (int32_t)strlen(s), .chars = s}).id;
}

uint32_t nt_ui_id_str(const char *s) { return nt_ui_id(s); }

nt_ui_bbox_t nt_ui_get_bbox(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_bbox: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_get_bbox: id must be non-zero (0 = no widget)");
    /* Thin wrapper: raw prev-frame LAYOUT bbox (Y-down). On miss Clay returns a
     * zeroed box with found == false (D-56-09). */
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    return (nt_ui_bbox_t){.x = d.boundingBox.x, .y = d.boundingBox.y, .width = d.boundingBox.width, .height = d.boundingBox.height, .found = d.found};
}

/* Transform-aware hit-test (D-56-07). Build the declaration-time accumulated
 * affine from ctx->accum_stack using the widget's PREV-FRAME bbox center as the
 * center for ALL levels (accepted approximation: the common case is the widget
 * being the first renderable after its own push, so the deferred render center
 * resolves to this same point -- consistent to within the accepted 1-frame
 * transform lag). Then inverse-transform (px,py) and point-in-(layout)-bbox.
 * Stays in Clay Y-DOWN, NON-negated rotation (Pitfall 2).
 *
 * Phase 56 ext: pad_lrtb (NULL allowed = {0,0,0,0}) inflates the layout-space
 * bbox BEFORE the inverse-affine check, so the padded zone rotates with the
 * widget. pad_lrtb[i] >= 0 is asserted by the public _padded entry point. */
static bool ui_hit_test(const nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]) {
    if (id == 0U) {
        return false;
    }
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return false; /* first frame an id is seen -> not hovered (D-56-06). */
    }
    const Clay_BoundingBox box = d.boundingBox;
    /* Center is the GEOMETRIC center of the visual bbox (NOT the padded one) so
     * the same rotation pivot the renderer uses applies — padding is a hit-zone
     * inflation, not a position shift. */
    const float cx = box.x + (box.width * 0.5F);
    const float cy = box.y + (box.height * 0.5F);

    /* Accumulate the affine from the live declaration-time stack. */
    float a = 1.0F;
    float b = 0.0F;
    float c = 0.0F;
    float dd = 1.0F;
    float tx = 0.0F;
    float ty = 0.0F;
    for (uint32_t k = 0; k < ctx->accum_depth; ++k) {
        compose_transform_level(&ctx->accum_stack[k], cx, cy, &a, &b, &c, &dd, &tx, &ty);
    }

    /* Inverse 2x2 (det nonzero: push_transform asserts scale > 0). */
    const float det = (a * dd) - (b * c);
    const float inv_a = dd / det;
    const float inv_b = -b / det;
    const float inv_c = -c / det;
    const float inv_d = a / det;
    const float rx = px - tx;
    const float ry = py - ty;
    const float lx = (inv_a * rx) + (inv_b * ry); /* point in the untransformed layout frame */
    const float ly = (inv_c * rx) + (inv_d * ry);

    /* Inflate bbox in layout space (NULL pad = zero padding -> original bbox). */
    const float pl = (pad_lrtb != NULL) ? (float)pad_lrtb[0] : 0.0F;
    const float pr = (pad_lrtb != NULL) ? (float)pad_lrtb[1] : 0.0F;
    const float pt = (pad_lrtb != NULL) ? (float)pad_lrtb[2] : 0.0F;
    const float pb = (pad_lrtb != NULL) ? (float)pad_lrtb[3] : 0.0F;
    return (lx >= box.x - pl) && (lx <= box.x + box.width + pr) && (ly >= box.y - pt) && (ly <= box.y + box.height + pb);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_interaction_t nt_ui_get_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_interaction_padded: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_get_interaction_padded: id must be non-zero (0 = no widget)");
    NT_ASSERT(ctx->frame_pointer_count > 0U && "nt_ui_get_interaction_padded: no frame pointer snapshot (call inside begin/end)");
    /* Negative padding is a use error: the API shrinks-from-bbox use case is
     * better served by sizing the widget smaller, NOT a negative inflation. */
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_get_interaction_padded: pad_lrtb components must be >= 0");

    nt_ui_interaction_t out = {0};

    /* Phase 56 ext fix: when the inspector sidebar is consuming the pointer
     * (set in nt_ui_begin based on primary->x vs panel width), every user-
     * widget interaction query short-circuits to a zeroed result -- no hover,
     * no press, no clicked. Without this, clicking the visual sidebar would
     * ALSO fire any button geometrically behind it (the sidebar paints on top
     * but the hit-test is purely coord-vs-bbox). Returning zero here also
     * naturally cleans up captures: capture_seen stays 0 for this id, so the
     * next nt_ui_begin's orphan-cleanup wipes any in-progress capture instead
     * of letting it persist into a phantom drag. */
    if (ctx->inspector_active && ctx->inspector_pointer_consumed) {
        return out;
    }

    /* No prev-frame bbox yet (first frame an id is declared) -> not hovered,
     * no capture can have started against it (D-56-06). */
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return out;
    }

    /* v1.8 single-pointer: the primary pointer is index 0 (D-56-04). */
    const uint32_t pidx = 0U;
    const nt_pointer_t *p = &ctx->frame_pointers[pidx];
    nt_ui_capture_t *cap = &ctx->captures[pidx];
    const nt_button_state_t btn = p->buttons[NT_BUTTON_LEFT]; /* precomputed edges */

    const bool over = ui_hit_test(ctx, id, p->x, p->y, pad_lrtb);
    out.hovered = over;
    if (over) {
        ctx->pointer_over_any = true; /* feeds nt_ui_wants_pointer (D-56-08). */
    }

    /* Begin capture on press-over-widget. */
    if (over && btn.is_pressed) {
        cap->active_id = id;
        cap->press_pos[0] = p->x;
        cap->press_pos[1] = p->y;
        cap->pos[0] = p->x;
        cap->pos[1] = p->y;
        out.pressed_now = true;
    }

    const bool mine = (cap->active_id == id);
    if (mine) {
        cap->pos[0] = p->x;
        cap->pos[1] = p->y;
        out.pressed = btn.is_down;
        out.released_now = btn.is_released;
        /* clicked = release OVER the widget; off-widget release cancels
         * (released_now true, clicked false). */
        out.clicked = btn.is_released && over;
        out.pointer_id = p->id;
        out.press_pos[0] = cap->press_pos[0];
        out.press_pos[1] = cap->press_pos[1];
        out.pos[0] = cap->pos[0];
        out.pos[1] = cap->pos[1];
        out.drag_dx = cap->pos[0] - cap->press_pos[0];
        out.drag_dy = cap->pos[1] - cap->press_pos[1];
        /* This capture was queried this frame -> not an orphan. */
        ctx->capture_seen[pidx] = 1U;
        /* Release ends the capture (whether over or not). */
        if (btn.is_released) {
            cap->active_id = 0U;
        }
    } else {
        /* Not captured: pos reflects the current pointer; no drag. */
        out.press_pos[0] = p->x;
        out.press_pos[1] = p->y;
        out.pos[0] = p->x;
        out.pos[1] = p->y;
        out.pointer_id = p->id;
    }

    /* Phase 56 ext: hit-zone overlay recording. Gated by debug_recording so
     * production overhead is zero. At-cap is silently dropped (overlay is a
     * verification aid, not correctness). */
    if (ctx->debug_recording && ctx->debug_zone_count < NT_UI_DEBUG_ZONE_CAP) {
        nt_ui_debug_zone_t *z = &ctx->debug_zones[ctx->debug_zone_count++];
        const float pl = (pad_lrtb != NULL) ? (float)pad_lrtb[0] : 0.0F;
        const float pr = (pad_lrtb != NULL) ? (float)pad_lrtb[1] : 0.0F;
        const float pt = (pad_lrtb != NULL) ? (float)pad_lrtb[2] : 0.0F;
        const float pb = (pad_lrtb != NULL) ? (float)pad_lrtb[3] : 0.0F;
        z->id = id;
        z->visual_l = d.boundingBox.x;
        z->visual_t = d.boundingBox.y;
        z->visual_r = d.boundingBox.x + d.boundingBox.width;
        z->visual_b = d.boundingBox.y + d.boundingBox.height;
        z->layout_l = z->visual_l - pl;
        z->layout_t = z->visual_t - pt;
        z->layout_r = z->visual_r + pr;
        z->layout_b = z->visual_b + pb;
        z->center_x = d.boundingBox.x + (d.boundingBox.width * 0.5F);
        z->center_y = d.boundingBox.y + (d.boundingBox.height * 0.5F);
        const uint32_t depth = ctx->accum_depth;
        z->accum_depth = depth;
        for (uint32_t k = 0; k < depth; ++k) {
            z->accum[k] = ctx->accum_stack[k];
        }
        uint16_t flags = 0U;
        if (out.hovered) {
            flags |= (uint16_t)NT_UI_DEBUG_FLAG_HOVERED;
        }
        if (out.pressed) {
            flags |= (uint16_t)NT_UI_DEBUG_FLAG_PRESSED;
        }
        if (cap->active_id == id) {
            flags |= (uint16_t)NT_UI_DEBUG_FLAG_CAPTURED;
        }
        z->state_flags = flags;
    }

    return out;
}

/* Thin wrapper: zero-padding specialization of the padded variant. */
nt_ui_interaction_t nt_ui_get_interaction(nt_ui_context_t *ctx, uint32_t id) { return nt_ui_get_interaction_padded(ctx, id, NULL); }

/* Phase 56 ext: record-only push for DISABLED widgets that skip hit-test
 * entirely (e.g. nt_ui_button enabled=false). Mirrors the zone-fill block
 * inside nt_ui_get_interaction_padded but does NO hit-test / capture work
 * (the widget is non-interactive by contract). Recording is gated by
 * ctx->debug_recording (OFF default = zero overhead); at-cap silently
 * dropped. First-frame Clay_GetElementData miss -> no zone (NOT an assert).
 * The zone carries NT_UI_DEBUG_FLAG_DISABLED so mode=ALL surfaces it while
 * mode=HOVER/CAPTURED naturally hide it (those filters don't match it). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_debug_record_disabled_zone(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_debug_record_disabled_zone: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_debug_record_disabled_zone: id must be non-zero (0 = no widget)");
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_debug_record_disabled_zone: pad_lrtb components must be >= 0");

    /* Zero-overhead fast path. */
    if (!ctx->debug_recording || ctx->debug_zone_count >= NT_UI_DEBUG_ZONE_CAP) {
        return;
    }
    /* First frame an id is declared has no prev-frame bbox -> no zone to record. */
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return;
    }

    nt_ui_debug_zone_t *z = &ctx->debug_zones[ctx->debug_zone_count++];
    const float pl = (pad_lrtb != NULL) ? (float)pad_lrtb[0] : 0.0F;
    const float pr = (pad_lrtb != NULL) ? (float)pad_lrtb[1] : 0.0F;
    const float pt = (pad_lrtb != NULL) ? (float)pad_lrtb[2] : 0.0F;
    const float pb = (pad_lrtb != NULL) ? (float)pad_lrtb[3] : 0.0F;
    z->id = id;
    z->visual_l = d.boundingBox.x;
    z->visual_t = d.boundingBox.y;
    z->visual_r = d.boundingBox.x + d.boundingBox.width;
    z->visual_b = d.boundingBox.y + d.boundingBox.height;
    z->layout_l = z->visual_l - pl;
    z->layout_t = z->visual_t - pt;
    z->layout_r = z->visual_r + pr;
    z->layout_b = z->visual_b + pb;
    z->center_x = d.boundingBox.x + (d.boundingBox.width * 0.5F);
    z->center_y = d.boundingBox.y + (d.boundingBox.height * 0.5F);
    const uint32_t depth = ctx->accum_depth;
    z->accum_depth = depth;
    for (uint32_t k = 0; k < depth; ++k) {
        z->accum[k] = ctx->accum_stack[k];
    }
    z->state_flags = (uint16_t)NT_UI_DEBUG_FLAG_DISABLED;
}

bool nt_ui_wants_pointer(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_wants_pointer: ctx must be non-NULL");
    /* Phase 56 ext fix: the inspector sidebar counts as "engine wants the
     * pointer". Game world input (camera drag, etc.) is suppressed correctly
     * even when no user widget is under the pointer. */
    if (ctx->inspector_active && ctx->inspector_pointer_consumed) {
        return true;
    }
    if (ctx->pointer_over_any) {
        return true;
    }
    for (uint32_t i = 0; i < NT_INPUT_MAX_POINTERS; ++i) {
        if (ctx->captures[i].active_id != 0U) {
            return true;
        }
    }
    return false;
}
// #endregion

// #region public_metrics
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_draw_calls: ctx must be non-NULL");
    return ctx->last_walk_draw_call_delta;
}

uint32_t nt_ui_get_last_walk_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_command_count: ctx must be non-NULL");
    return ctx->last_walk_command_count;
}

float nt_ui_get_last_layout_ms(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_layout_ms: ctx must be non-NULL");
    return ctx->last_layout_ms;
}

float nt_ui_get_last_walk_ms(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_ms: ctx must be non-NULL");
    return ctx->last_walk_ms;
}

uint32_t nt_ui_get_last_walk_rect_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_rect_command_count: ctx must be non-NULL");
    return ctx->last_walk_rect_command_count;
}

uint32_t nt_ui_get_last_walk_image_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_image_command_count: ctx must be non-NULL");
    return ctx->last_walk_image_command_count;
}

uint32_t nt_ui_get_last_walk_text_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_text_command_count: ctx must be non-NULL");
    return ctx->last_walk_text_command_count;
}

uint32_t nt_ui_get_last_walk_border_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_border_command_count: ctx must be non-NULL");
    return ctx->last_walk_border_command_count;
}

uint32_t nt_ui_get_last_walk_scissor_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_scissor_command_count: ctx must be non-NULL");
    return ctx->last_walk_scissor_command_count;
}

uint32_t nt_ui_get_last_walk_max_scissor_depth(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_max_scissor_depth: ctx must be non-NULL");
    return ctx->last_walk_max_scissor_depth;
}

uint32_t nt_ui_get_last_walk_transform_pushes(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_transform_pushes: ctx must be non-NULL");
    return ctx->last_walk_transform_pushes;
}

uint32_t nt_ui_get_last_walk_opacity_pushes(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_opacity_pushes: ctx must be non-NULL");
    return ctx->last_walk_opacity_pushes;
}
// #endregion

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void) { return g_nt_ui_inframe_ctx; }

int32_t nt_ui_test_clay_default_max_element_count(void) { return Clay__defaultMaxElementCount; }
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void) { return Clay__defaultMaxMeasureTextWordCacheCount; }

uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return ctx->test_last_walk_unlayered_count;
}

/* Clay_Context is only defined inside this TU (CLAY_IMPLEMENTATION),
 * so tests need these to read pointerInfo. */
float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_x: ctx must be non-NULL");
    return ctx->clay->pointerInfo.position.x;
}
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_y: ctx must be non-NULL");
    return ctx->clay->pointerInfo.position.y;
}
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_down: ctx must be non-NULL");
    const Clay_PointerDataInteractionState s = ctx->clay->pointerInfo.state;
    return (s == CLAY_POINTER_DATA_PRESSED || s == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) ? 1 : 0;
}

uint32_t nt_ui_test_capture_active_id(const nt_ui_context_t *ctx, uint32_t pointer_index) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_capture_active_id: ctx must be non-NULL");
    NT_ASSERT(pointer_index < NT_INPUT_MAX_POINTERS && "nt_ui_test_capture_active_id: pointer_index out of range");
    return ctx->captures[pointer_index].active_id;
}

bool nt_ui_test_hit(nt_ui_context_t *ctx, uint32_t id, float px, float py) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_hit: ctx must be non-NULL");
    return ui_hit_test(ctx, id, px, py, NULL);
}

bool nt_ui_test_hit_padded(nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_hit_padded: ctx must be non-NULL");
    return ui_hit_test(ctx, id, px, py, pad_lrtb);
}
#endif
// #endregion
