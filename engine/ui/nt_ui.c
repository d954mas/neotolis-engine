#include "ui/nt_ui.h"

#include "atlas/nt_atlas.h"
#include "core/nt_builtins.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"

/*
 * Exactly one TU in the build defines CLAY_IMPLEMENTATION -- this one.
 *
 * Clay version is pinned via deps/clay/VERSION; CMake parses major.minor
 * and passes CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR. The _Static_assert
 * below catches dev-time drift where deps/clay/VERSION is hand-edited
 * without re-vendoring (Clay itself has no CLAY_VERSION_* macros).
 */

#if !defined(CLAY_PINNED_MAJOR) || !defined(CLAY_PINNED_MINOR)
#error "nt_ui: CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR must be defined by CMake (engine/ui/CMakeLists.txt parses deps/clay/VERSION)"
#endif

#define CLAY_IMPLEMENTATION
#include "clay.h"

_Static_assert(CLAY_PINNED_MAJOR == 0 && CLAY_PINNED_MINOR == 14, "Clay v0.14 required -- deps/clay/VERSION disagrees with the engine pin");

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "ui/nt_ui_internal.h"

// #region module_state
/* Only one ctx may be in-frame at a time; nt_ui_begin asserts NULL on
 * entry and assigns, nt_ui_end clears. All other walker state lives in
 * nt_ui_context_t -- this is the single piece of module-level state. */
static nt_ui_context_t *g_nt_ui_inframe_ctx = NULL;
// #endregion

// #region clay_error_handler
/* Recoverable: Clay safely no-ops the error and the rest of the UI still
 * renders correctly. Fatal: capacity overflow silently truncates the UI,
 * missing measure callback or internal Clay errors mean broken state.
 * Unknown defaults to fatal so a Clay version bump can't silently demote
 * a new invariant violation. */
static bool nt_ui_clay_error_is_recoverable(Clay_ErrorType type) {
    switch (type) {
    case CLAY_ERROR_TYPE_DUPLICATE_ID:
    case CLAY_ERROR_TYPE_FLOATING_CONTAINER_PARENT_NOT_FOUND:
    case CLAY_ERROR_TYPE_PERCENTAGE_OVER_1:
        return true;
    case CLAY_ERROR_TYPE_TEXT_MEASUREMENT_FUNCTION_NOT_PROVIDED:
    case CLAY_ERROR_TYPE_ARENA_CAPACITY_EXCEEDED:
    case CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED:
    case CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED:
    case CLAY_ERROR_TYPE_INTERNAL_ERROR:
        return false;
    }
    return false;
}

/* errorText is a Clay_String with .length + .chars -- not NUL-terminated. */
static void nt_ui_clay_error_cb(Clay_ErrorData err) {
    const int len = err.errorText.length;
    const char *const chars = (err.errorText.chars != NULL && len > 0) ? err.errorText.chars : "(no text)";
    const int safe_len = (err.errorText.chars != NULL && len > 0) ? len : 9;
    const int type = (int)err.errorType;

    if (nt_ui_clay_error_is_recoverable(err.errorType)) {
        NT_LOG_WARN("clay error type=%d: %.*s", type, safe_len, chars);
        return;
    }
    NT_LOG_ERROR("clay fatal error type=%d: %.*s", type, safe_len, chars);
    NT_ASSERT(false && "nt_ui: Clay reported a fatal invariant violation (see preceding log line)");
}
// #endregion

// #region measure_cb
static Clay_Dimensions nt_ui_measure_text_cb(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data;
    NT_ASSERT(g_nt_ui_inframe_ctx != NULL && "measure_cb: Clay called outside begin/end");
    NT_ASSERT(config != NULL && "measure_cb: Clay passed NULL config");

    nt_ui_context_t *ctx = g_nt_ui_inframe_ctx;
    if (config->fontId >= NT_UI_MAX_FONTS) {
        return (Clay_Dimensions){0};
    }
    nt_font_t font = ctx->fonts[config->fontId];
    if (!nt_font_valid(font)) {
        return (Clay_Dimensions){0};
    }
    const float ls = (float)config->letterSpacing;
    nt_text_size_t s = nt_font_measure_n(font, text.chars, (size_t)text.length, (float)config->fontSize, ls);
    /* Clay convention: callback returns word width WITH one trailing
     * letterSpacing slot, then Clay subtracts a single trailing slot at
     * end-of-line (clay.h Clay__MeasureTextCached line 1677). Our
     * nt_font_measure_n returns the CSS-style (N-1) gap width, so add one
     * trailing slot here when the measurement produced any width. Without
     * this, Clay's bbox is short by exactly one letterSpacing relative to
     * the rendered text. */
    if (s.width > 0.0F && ls != 0.0F) {
        s.width += ls;
    }
    return (Clay_Dimensions){.width = s.width, .height = s.height};
}
// #endregion

// #region module_init
/* Wire measure callback into Clay's global function pointer. Bypasses
 * Clay_SetMeasureTextFunction (which requires an active Clay context) by
 * writing directly to the global -- legal because CLAY_IMPLEMENTATION
 * places Clay's internals in this TU. */
void nt_ui_module_init(void) { Clay__MeasureText = nt_ui_measure_text_cb; }
void nt_ui_module_shutdown(void) { Clay__MeasureText = NULL; }
// #endregion

// #region create_destroy
/* ctx struct rounded up to cache-line so Clay's arena starts aligned. */
static size_t nt_ui_ctx_size_aligned(void) { return (sizeof(struct nt_ui_context) + 63U) & ~(size_t)63U; }

size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc) {
    NT_ASSERT(desc != NULL && "nt_ui_min_arena_size: desc must be non-NULL");
    NT_ASSERT(desc->max_elements > 0U && "nt_ui_min_arena_size: desc->max_elements must be > 0");
    /* Clay_SetMaxElementCount writes to GLOBAL Clay__defaultMaxElementCount
     * when no current context exists, or to current context otherwise. We
     * want to size against desc-> max_elements WITHOUT mutating any active
     * ctx -- save current, null it, set, query, restore. */
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount((int32_t)desc->max_elements);
    const size_t clay_bytes = (size_t)Clay_MinMemorySize();
    Clay_SetCurrentContext(saved);
    return nt_ui_ctx_size_aligned() + clay_bytes;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc) {
    NT_ASSERT(arena != NULL && "nt_ui_create_context: arena must be non-NULL");
    NT_ASSERT(desc != NULL && "nt_ui_create_context: desc must be non-NULL");
    NT_ASSERT(Clay__MeasureText == nt_ui_measure_text_cb && "nt_ui_create_context: call nt_ui_module_init() once before any create_context");
    NT_ASSERT(((uintptr_t)arena & (NT_UI_ARENA_ALIGN - 1U)) == 0U && "nt_ui_create_context: arena must be NT_UI_ARENA_ALIGN-aligned (alignas(NT_UI_ARENA_ALIGN) static uint8_t arena[N])");
    NT_ASSERT(arena_size >= nt_ui_min_arena_size(desc) && "nt_ui_create_context: arena_size < nt_ui_min_arena_size(desc)");

    nt_ui_context_t *ctx = (nt_ui_context_t *)arena;
    memset(ctx, 0, sizeof(*ctx));

    const size_t ctx_size = nt_ui_ctx_size_aligned();
    void *clay_mem = (char *)arena + ctx_size;
    const size_t clay_size = arena_size - ctx_size;

    /* Stage desc->max_elements into Clay's global default so Clay_Initialize
     * inherits it for this new context. Save/restore current ctx so this
     * does not bleed into an active sibling. */
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount((int32_t)desc->max_elements);

    ctx->in_frame = false;
    ctx->clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_size, clay_mem);
    ctx->clay = Clay_Initialize(ctx->clay_arena, (Clay_Dimensions){.width = 1.0F, .height = 1.0F}, (Clay_ErrorHandler){.errorHandlerFunction = nt_ui_clay_error_cb, .userData = ctx});

    /* Restore the previously-current context (if any). The caller picks
     * which ctx is "current" via nt_ui_begin for each frame anyway. */
    if (saved != NULL) {
        Clay_SetCurrentContext(saved);
    }

    return ctx;
}

void nt_ui_destroy_context(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_destroy_context: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_destroy_context: ctx is mid-frame (call nt_ui_end first)");
    memset(ctx, 0, sizeof(*ctx));
}
// #endregion

// #region font_registry
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_font: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_font: must be called outside begin/end (mid-frame mutation would split layout vs walker view)");
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_set_font: font_id out of range (raise NT_UI_MAX_FONTS)");
    ctx->fonts[font_id] = font;
}
// #endregion

// #region begin_end
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(mouse != NULL && "nt_ui_begin: mouse must be non-NULL");
    /* isfinite() rejects NaN + +-inf; raw `>= 0.0F` lets +inf through. */
    NT_ASSERT(isfinite(screen_w) && screen_w >= 0.0F && "nt_ui_begin: screen_w must be finite and non-negative");
    NT_ASSERT(isfinite(screen_h) && screen_h >= 0.0F && "nt_ui_begin: screen_h must be finite and non-negative");
    NT_ASSERT(g_nt_ui_inframe_ctx == NULL && "nt_ui_begin: another ctx is mid-frame");
    NT_ASSERT(!ctx->in_frame && "nt_ui_begin: ctx already in_frame");

    /* MUST be first: switches Clay's current-context global so every
     * subsequent Clay call below operates on ctx->clay. */
    Clay_SetCurrentContext(ctx->clay);

    ctx->in_frame = true;
    g_nt_ui_inframe_ctx = ctx;

    Clay_SetLayoutDimensions((Clay_Dimensions){.width = screen_w, .height = screen_h});

    /* Left-button only; Clay v0.14 does not consume right/middle/wheel.
     * Caller picks which pointer to feed -- typically pointers[0]. */
    Clay_SetPointerState((Clay_Vector2){.x = mouse->x, .y = mouse->y}, mouse->buttons[NT_BUTTON_LEFT].is_down);

    Clay_BeginLayout();
}

void nt_ui_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_end: ctx is not in_frame (begin was not called)");
    NT_ASSERT(ctx == g_nt_ui_inframe_ctx && "nt_ui_end: ctx mismatch with module in-frame ctx");

    ctx->frozen_cmds = Clay_EndLayout();
    ctx->in_frame = false;
    g_nt_ui_inframe_ctx = NULL;
}
// #endregion

// #region helpers_color_pack
/* Clay stores RGBA as 0..255 floats with no clamping; sprite vertex
 * format is 0xAABBGGRR uint32. Saturate so a theme that wrote 256 or -1
 * doesn't wrap around the uint8 cast. */
static inline uint8_t clamp_u8(float v) {
    if (v <= 0.0F) {
        return 0U;
    }
    if (v >= 255.0F) {
        return 255U;
    }
    return (uint8_t)v;
}

static inline uint32_t nt_color_pack_clay(Clay_Color c) {
    uint32_t r = clamp_u8(c.r);
    uint32_t g = clamp_u8(c.g);
    uint32_t b = clamp_u8(c.b);
    uint32_t a = clamp_u8(c.a);
    return r | (g << 8) | (b << 16) | (a << 24);
}
// #endregion

// #region helper_emit_screen_rect
/* Builds a column-major 2D-ortho mat4 (scale by w,h; translate to x,y)
 * directly onto the renderer. cached_pos for a 1x1 white region is the
 * unit square in source space, so m[0]=w / m[5]=h folds straight into
 * the target rect with no per-corner work. */
static inline void emit_screen_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, uint32_t color_packed) {
    const float m[16] = {
        w,    0.0F, 0.0F, 0.0F, /* col0: scale x */
        0.0F, h,    0.0F, 0.0F, /* col1: scale y */
        0.0F, 0.0F, 1.0F, 0.0F, /* col2 */
        x,    y,    0.0F, 1.0F, /* col3: translate */
    };
    nt_sprite_renderer_emit_region(atlas, region_index, m, 0.0F, 0.0F, color_packed, 0U);
}
// #endregion

// #region helper_emit_rounded_rect
/* Per-quarter-arc segment count. 6 keeps the triangle count low (28 tris
 * for a fully rounded rect) while staying smooth at typical UI radii.
 *
 * Hot-path note: trig (cosf/sinf) dominates here -- ~28 calls per rounded
 * rect, ~56 per rounded border. If profiling shows this in the top hot
 * path, precompute a (SEG+1) x 4 sin/cos LUT at startup and table-lookup
 * here instead of computing each frame. UV centroid in emit_geometry is
 * orders of magnitude cheaper and not worth caching first. */
#define NT_UI_CORNER_SEGMENTS 6
_Static_assert(NT_UI_CORNER_SEGMENTS >= 2 && NT_UI_CORNER_SEGMENTS <= 32, "NT_UI_CORNER_SEGMENTS must be in [2, 32]: lower degenerate, higher wastes stack and staging");
#define NT_UI_PI_F 3.14159265358979323846F

/* Triangle fan from rect center to the boundary. Each corner contributes
 * either 1 vertex (radius 0) or NT_UI_CORNER_SEGMENTS+1 arc vertices.
 * Boundary order is screen-CW starting at TL arc entry on the left edge. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_rounded_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, Clay_CornerRadius cr, uint32_t color_packed) {
    /* Clamp radii so opposite corners can't overlap. Negative input is
     * treated as zero. */
    const float half_w = w * 0.5F;
    const float half_h = h * 0.5F;
    const float clamp = (half_w < half_h) ? half_w : half_h;
    float tl = (cr.topLeft > 0.0F) ? cr.topLeft : 0.0F;
    float tr = (cr.topRight > 0.0F) ? cr.topRight : 0.0F;
    float bl = (cr.bottomLeft > 0.0F) ? cr.bottomLeft : 0.0F;
    float br = (cr.bottomRight > 0.0F) ? cr.bottomRight : 0.0F;
    if (tl > clamp) {
        tl = clamp;
    }
    if (tr > clamp) {
        tr = clamp;
    }
    if (bl > clamp) {
        bl = clamp;
    }
    if (br > clamp) {
        br = clamp;
    }

    /* All-square fast path stays on emit_screen_rect (1 quad, 4 verts). */
    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_screen_rect(atlas, region_index, x, y, w, h, color_packed);
        return;
    }

    /* Worst case: center + 4 * (segments+1) boundary verts. */
    float positions[1 + (4 * (NT_UI_CORNER_SEGMENTS + 1))][2];
    uint16_t indices[4 * (NT_UI_CORNER_SEGMENTS + 1) * 3];

    positions[0][0] = x + half_w;
    positions[0][1] = y + half_h;
    uint32_t vi = 1;

    /* TL arc: math angle π → 1.5π (sweeps from west to north of corner center). */
    if (tl == 0.0F) {
        positions[vi][0] = x;
        positions[vi][1] = y;
        vi++;
    } else {
        const float cx = x + tl;
        const float cy = y + tl;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
            const float a = NT_UI_PI_F * (1.0F + 0.5F * t);
            positions[vi][0] = cx + tl * cosf(a);
            positions[vi][1] = cy + tl * sinf(a);
            vi++;
        }
    }
    /* TR arc: 1.5π → 2π. */
    if (tr == 0.0F) {
        positions[vi][0] = x + w;
        positions[vi][1] = y;
        vi++;
    } else {
        const float cx = x + w - tr;
        const float cy = y + tr;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
            const float a = NT_UI_PI_F * (1.5F + 0.5F * t);
            positions[vi][0] = cx + tr * cosf(a);
            positions[vi][1] = cy + tr * sinf(a);
            vi++;
        }
    }
    /* BR arc: 0 → 0.5π. */
    if (br == 0.0F) {
        positions[vi][0] = x + w;
        positions[vi][1] = y + h;
        vi++;
    } else {
        const float cx = x + w - br;
        const float cy = y + h - br;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
            const float a = NT_UI_PI_F * (0.5F * t);
            positions[vi][0] = cx + br * cosf(a);
            positions[vi][1] = cy + br * sinf(a);
            vi++;
        }
    }
    /* BL arc: 0.5π → π. */
    if (bl == 0.0F) {
        positions[vi][0] = x;
        positions[vi][1] = y + h;
        vi++;
    } else {
        const float cx = x + bl;
        const float cy = y + h - bl;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
            const float a = NT_UI_PI_F * (0.5F + 0.5F * t);
            positions[vi][0] = cx + bl * cosf(a);
            positions[vi][1] = cy + bl * sinf(a);
            vi++;
        }
    }

    /* Triangle fan: (center=0, i, i+1) for i in [1..vi-1], wrap last to 1. */
    uint32_t ii = 0;
    for (uint32_t i = 1; i < vi; i++) {
        const uint16_t next = (uint16_t)((i + 1 < vi) ? (i + 1) : 1);
        indices[ii++] = 0U;
        indices[ii++] = (uint16_t)i;
        indices[ii++] = next;
    }

    const float identity[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, identity, color_packed);
}
// #endregion

// #region helper_emit_border
/* Square-corner fast path: up to 4 thin RECT quads. Top/bottom run the
 * full width; left/right are inset by top/bottom so corners don't blend. */
static void emit_square_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, uint32_t col) {
    const float top = (float)widths.top;
    const float bot = (float)widths.bottom;
    const float lft = (float)widths.left;
    const float rgt = (float)widths.right;
    if (widths.top) {
        emit_screen_rect(atlas, region_index, bb.x, bb.y, bb.width, top, col);
    }
    if (widths.bottom) {
        emit_screen_rect(atlas, region_index, bb.x, bb.y + bb.height - bot, bb.width, bot, col);
    }
    if (widths.left) {
        emit_screen_rect(atlas, region_index, bb.x, bb.y + top, lft, bb.height - top - bot, col);
    }
    if (widths.right) {
        emit_screen_rect(atlas, region_index, bb.x + bb.width - rgt, bb.y + top, rgt, bb.height - top - bot, col);
    }
}

/* Append one corner's (outer,inner) vertex pair(s) into the strip array.
 * a_start_factor scales by π for the arc start (1.0=TL/π, 1.5=TR/3π/2,
 * 0.0=BR/0, 0.5=BL/π/2). a_start_factor sweep is always +0.5π. radius==0
 * collapses to a single sharp pair at (sharp_x, sharp_y) (outer) and
 * (sharp_x + sx*wx, sharp_y + sy*wy) (inner). */
static uint32_t emit_corner_strip_pairs(float (*pos)[2], uint32_t vi, float radius, float cx, float cy, float w_perp_x, float w_perp_y, float sharp_x, float sharp_y, float sign_x, float sign_y,
                                        float a_start_factor) {
    if (radius == 0.0F) {
        pos[vi][0] = sharp_x;
        pos[vi][1] = sharp_y;
        vi++;
        pos[vi][0] = sharp_x + (sign_x * w_perp_x);
        pos[vi][1] = sharp_y + (sign_y * w_perp_y);
        vi++;
        return vi;
    }
    /* Clamp inner radii to 0 -- when border width exceeds the corner radius,
     * the inner curve degenerates to a flat segment through the corner
     * center on that axis. Matches CSS/Godot/Skia behaviour. */
    const float irx = (radius > w_perp_x) ? (radius - w_perp_x) : 0.0F;
    const float iry = (radius > w_perp_y) ? (radius - w_perp_y) : 0.0F;
    const float a_start = NT_UI_PI_F * a_start_factor;
    for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
        const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
        const float a = a_start + (NT_UI_PI_F * 0.5F * t);
        const float cc = cosf(a);
        const float ss = sinf(a);
        pos[vi][0] = cx + (radius * cc);
        pos[vi][1] = cy + (radius * ss);
        vi++;
        pos[vi][0] = cx + (irx * cc);
        pos[vi][1] = cy + (iry * ss);
        vi++;
    }
    return vi;
}

/* Tessellated ring between the outer rounded-rect boundary and the inner
 * boundary (inset by per-side widths). Inner radius on each axis clamps
 * to max(0, radius - adjacent_width); a zero-width side produces a
 * zero-area strip segment (visually skipped). Matches CSS/Godot/Skia.
 * Top+bottom widths must still fit bbox height (and left+right the width). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_rounded_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, Clay_CornerRadius cr_in, uint32_t color_packed) {
    const float x = bb.x;
    const float y = bb.y;
    const float w = bb.width;
    const float h = bb.height;
    const float top = (float)widths.top;
    const float bot = (float)widths.bottom;
    const float lft = (float)widths.left;
    const float rgt = (float)widths.right;
    NT_ASSERT(top + bot <= h && "nt_ui BORDER: top+bottom widths exceed bbox.height");
    NT_ASSERT(lft + rgt <= w && "nt_ui BORDER: left+right widths exceed bbox.width");

    const float half_w = w * 0.5F;
    const float half_h = h * 0.5F;
    const float clamp_r = (half_w < half_h) ? half_w : half_h;
    float tl = (cr_in.topLeft > 0.0F) ? cr_in.topLeft : 0.0F;
    float tr = (cr_in.topRight > 0.0F) ? cr_in.topRight : 0.0F;
    float bl = (cr_in.bottomLeft > 0.0F) ? cr_in.bottomLeft : 0.0F;
    float br = (cr_in.bottomRight > 0.0F) ? cr_in.bottomRight : 0.0F;
    if (tl > clamp_r) {
        tl = clamp_r;
    }
    if (tr > clamp_r) {
        tr = clamp_r;
    }
    if (bl > clamp_r) {
        bl = clamp_r;
    }
    if (br > clamp_r) {
        br = clamp_r;
    }

    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_square_border(atlas, region_index, bb, widths, color_packed);
        return;
    }

    /* Rounded path supports partial borders: a zero side width collapses
     * its strip to zero-area triangles (no visible stroke). A side width
     * larger than the corner radius clamps the inner curve to 0 on that
     * axis (corner becomes "filled" inside). CSS/Godot/Skia parity. */

    /* Worst case: 4 * (SEG+1) pairs * 2 verts = 56 for SEG=6. */
    float positions[4 * (NT_UI_CORNER_SEGMENTS + 1) * 2][2];
    uint32_t vi = 0;

    vi = emit_corner_strip_pairs(positions, vi, tl, x + tl, y + tl, lft, top, x, y, 1.0F, 1.0F, 1.0F);
    vi = emit_corner_strip_pairs(positions, vi, tr, x + w - tr, y + tr, rgt, top, x + w, y, -1.0F, 1.0F, 1.5F);
    vi = emit_corner_strip_pairs(positions, vi, br, x + w - br, y + h - br, rgt, bot, x + w, y + h, -1.0F, -1.0F, 0.0F);
    vi = emit_corner_strip_pairs(positions, vi, bl, x + bl, y + h - bl, lft, bot, x, y + h, 1.0F, -1.0F, 0.5F);

    /* Triangle strip with wrap. Each pair k: outer at 2k, inner at 2k+1. */
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

    const float identity[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, identity, color_packed);
}

static void emit_border(const nt_ui_context_t *ctx, const Clay_RenderCommand *c) {
    const Clay_BorderRenderData *b = &c->renderData.border;
    const uint32_t col = nt_color_pack_clay(b->color);
    emit_rounded_border(ctx->atlas, ctx->white_region, c->boundingBox, b->width, b->cornerRadius, col);
}
// #endregion

// #region helper_emit_image
/* IMAGE uses payload atlas, not ctx->atlas. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_image(const Clay_RenderCommand *c) {
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    NT_ASSERT(p != NULL && "nt_ui IMAGE: imageData must point to nt_ui_image_payload_t");
    NT_ASSERT(p->atlas.id != 0 && "nt_ui IMAGE payload: invalid atlas handle (zero id)");
    /* IMAGE corner radius is not supported -- bake rounded edges into the
     * atlas instead. Polygon atlas regions would already escape any
     * bbox-aligned corner clip. */
    const Clay_CornerRadius cr = c->renderData.image.cornerRadius;
    NT_ASSERT(cr.topLeft == 0.0F && cr.topRight == 0.0F && cr.bottomLeft == 0.0F && cr.bottomRight == 0.0F && "nt_ui IMAGE: cornerRadius unsupported; pre-bake into atlas");
    /* Async-loading atlas: skip this frame. */
    if (!nt_resource_is_ready(p->atlas)) {
        return;
    }

    const Clay_BoundingBox bb = c->boundingBox;

    /* Clay defaults backgroundColor to {0,0,0,0} = "untinted", not "draw
     * nothing". Alpha-only check would clobber legitimate transparent tints. */
    Clay_Color tint = c->renderData.image.backgroundColor;
    const bool default_untinted = (tint.r == 0.0F && tint.g == 0.0F && tint.b == 0.0F && tint.a == 0.0F);
    const uint32_t col = default_untinted ? 0xFFFFFFFFU : nt_color_pack_clay(tint);

    const nt_texture_region_t *r = nt_atlas_get_region(p->atlas, p->region_index);
    if (r->vertex_count == 0U) {
        return; /* tombstone */
    }
    const float ipu = nt_atlas_get_inverse_pixels_per_unit(p->atlas);
    const float src_w = (float)r->source_w * ipu;
    const float src_h = (float)r->source_h * ipu;
    /* Guard against degenerate src dim (tombstoned region slipping the
     * vertex_count gate) -- div-by-zero protection. */
    const float sx = (src_w > 0.0F) ? (bb.width / src_w) : bb.width;
    const float sy = (src_h > 0.0F) ? (bb.height / src_h) : bb.height;

    const float m[16] = {
        sx, 0.0F, 0.0F, 0.0F, 0.0F, sy, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, bb.x, bb.y, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_region(p->atlas, p->region_index, m, 0.0F, 0.0F, col, p->flip_bits);
}
// #endregion

static void rebind_sprite_after_flush(const nt_ui_context_t *ctx);

// #region helper_emit_text
static void emit_text(const nt_ui_context_t *ctx, const Clay_RenderCommand *c) {
    /* Sprite cmd is open at entry; text uses a different pipeline. Drain
     * sprite staging first; rebind at the tail so subsequent RECT/BORDER
     * /IMAGE has a live cmd. */
    nt_sprite_renderer_flush();

    const Clay_TextRenderData *t = &c->renderData.text;
    NT_ASSERT((uint32_t)t->fontId < NT_UI_MAX_FONTS && "nt_ui TEXT: fontId out of range; check CLAY_TEXT_CONFIG vs NT_UI_MAX_FONTS");
    nt_font_t font = ctx->fonts[t->fontId];
    NT_ASSERT(nt_font_valid(font) && "nt_ui TEXT: font slot empty; call nt_ui_set_font(ctx, fontId, font) before declaring TEXT with this fontId");

    nt_text_renderer_set_font(font);
    nt_text_renderer_set_material(ctx->text_material);

    /* Translation-only model -- font size is the scale arg to draw_n. */
    const float m[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, c->boundingBox.x, c->boundingBox.y, 0.0F, 1.0F,
    };
    /* nt_text_renderer expects color in 0..1; Clay stores 0..255. */
    const float color[4] = {
        t->textColor.r / 255.0F,
        t->textColor.g / 255.0F,
        t->textColor.b / 255.0F,
        t->textColor.a / 255.0F,
    };
    nt_text_renderer_draw_n(t->stringContents.chars, (size_t)t->stringContents.length, m, (float)t->fontSize, color, (float)t->letterSpacing);
    rebind_sprite_after_flush(ctx);
}
// #endregion

// #region helper_scissor_stack
typedef struct {
    int x;
    int y;
    int w;
    int h;
} sscissor_rect_t;

/* nt_sprite_renderer_flush closes the current cmd; the next emit would
 * otherwise trip "no open cmd". Same-handle re-open is cheap (caches
 * pipeline + mat_info). */
static void rebind_sprite_after_flush(const nt_ui_context_t *ctx) { nt_sprite_renderer_set_material(ctx->sprite_material); }

static void scissor_push(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, sscissor_rect_t *stack, int *depth, const nt_ui_target_t *target) {
    NT_ASSERT(*depth < NT_UI_WALKER_MAX_SCISSOR_DEPTH && "scissor stack overflow");

    /* clip.horizontal / .vertical flag the axes that should be clipped.
     * On an unclipped axis we use the target viewport extent so the GL
     * scissor still has well-defined bounds; the nested-intersect step
     * below will narrow it to the parent on that axis if there is one.
     *
     * floor min / ceil max on the clipped axis: a (int)truncate would
     * clip subpixel-aligned UI by 1px at the right/bottom edge. */
    const Clay_ClipRenderData *clip = &c->renderData.clip;
    const int vx = (int)target->viewport[0];
    const int vy = (int)target->viewport[1];
    const int vw = (int)target->viewport[2];
    const int vh = (int)target->viewport[3];
    int x;
    int y;
    int wp;
    int hp;
    if (clip->horizontal) {
        const float bx = c->boundingBox.x;
        x = (int)floorf(bx);
        wp = (int)ceilf(bx + c->boundingBox.width) - x;
    } else {
        x = 0;
        wp = vw;
    }
    if (clip->vertical) {
        const float by = c->boundingBox.y;
        y = (int)floorf(by);
        hp = (int)ceilf(by + c->boundingBox.height) - y;
    } else {
        y = 0;
        hp = vh;
    }

    /* Nested scissor intersects with stack top -- REPLACE semantics would
     * let an inner widget paint outside its parent's clip (e.g. scroll
     * content leaking past a modal backdrop). */
    if (*depth > 0) {
        sscissor_rect_t t = stack[*depth - 1];
        int x2 = (x > t.x) ? x : t.x;
        int y2 = (y > t.y) ? y : t.y;
        int r2 = ((x + wp) < (t.x + t.w)) ? (x + wp) : (t.x + t.w);
        int b2 = ((y + hp) < (t.y + t.h)) ? (y + hp) : (t.y + t.h);
        x = x2;
        y = y2;
        wp = (r2 > x2) ? (r2 - x2) : 0;
        hp = (b2 > y2) ? (b2 - y2) : 0;
    }

    /* MUST flush before changing GL scissor state -- pending staging
     * written under the prior scissor would otherwise be drawn under
     * the new one. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();

    stack[(*depth)++] = (sscissor_rect_t){.x = x, .y = y, .w = wp, .h = hp};

    /* Clay's bbox is target-local; shift by viewport.{x,y} and Y-flip
     * against viewport.h, NOT raw framebuffer height. Without the offset,
     * split-screen panes and sub-FBO targets clip the wrong pixels. */
    nt_gfx_set_scissor(vx + x, vy + vh - y - hp, wp, hp);
    nt_gfx_set_scissor_enabled(true);

    rebind_sprite_after_flush(ctx);
}

static void scissor_pop(const nt_ui_context_t *ctx, sscissor_rect_t *stack, int *depth, const nt_ui_target_t *target) {
    NT_ASSERT(*depth > 0 && "scissor underflow");
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    (*depth)--;
    if (*depth == 0) {
        nt_gfx_set_scissor_enabled(false);
    } else {
        sscissor_rect_t r = stack[*depth - 1];
        const int vx = (int)target->viewport[0];
        const int vy = (int)target->viewport[1];
        const int vh = (int)target->viewport[3];
        nt_gfx_set_scissor(vx + r.x, vy + vh - r.y - r.h, r.w, r.h);
    }
    rebind_sprite_after_flush(ctx);
}
// #endregion

// #region helper_emit_custom
/* Flush both renderers so the handler sees clean state; it may bind its
 * own pipeline. On return, re-bind sprite so the next RECT/BORDER/IMAGE
 * has a live cmd. */
static void emit_custom(const nt_ui_context_t *ctx, const Clay_RenderCommand *c) {
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    if (ctx->custom_fn != NULL) {
        ctx->custom_fn((const void *)c, ctx->custom_user);
    }
    rebind_sprite_after_flush(ctx);
}
// #endregion

// #region walk
/* True iff the command participates in same-zIndex batch reordering.
 * SCISSOR_START/END, CUSTOM, and NONE are hard barriers / no-ops -- they
 * always emit in original sequence and never go through the bucket pass. */
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

static void dispatch_command(nt_ui_context_t *ctx, const Clay_RenderCommand *c, sscissor_rect_t *scissor_stack, int *depth, const nt_ui_target_t *target) {
    switch (c->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
        return;
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
        const Clay_RectangleRenderData *r = &c->renderData.rectangle;
        const uint32_t col = nt_color_pack_clay(r->backgroundColor);
        emit_rounded_rect(ctx->atlas, ctx->white_region, c->boundingBox.x, c->boundingBox.y, c->boundingBox.width, c->boundingBox.height, r->cornerRadius, col);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_BORDER:
        emit_border(ctx, c);
        return;
    case CLAY_RENDER_COMMAND_TYPE_TEXT:
        emit_text(ctx, c);
        return;
    case CLAY_RENDER_COMMAND_TYPE_IMAGE:
        emit_image(c);
        return;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
        scissor_push(ctx, c, scissor_stack, depth, target);
        return;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        scissor_pop(ctx, scissor_stack, depth, target);
        return;
    case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
        emit_custom(ctx, c);
        return;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    NT_ASSERT(ctx != NULL && "nt_ui_walk: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_walk: target must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_walk: ctx is mid-frame (call nt_ui_end first)");
    NT_ASSERT(ctx->frozen_cmds.internalArray != NULL && "nt_ui_walk: frozen_cmds not populated (call nt_ui_end before walk)");
    /* w/h > 0: scissor math `vh - y - hp` underflows negative when
     * vh==0 (GL undefined). Origin x/y may be zero. */
    NT_ASSERT(isfinite(target->viewport[0]) && isfinite(target->viewport[1]) && isfinite(target->viewport[2]) && isfinite(target->viewport[3]) && "nt_ui_walk: target->viewport must be finite");
    NT_ASSERT(target->viewport[0] >= 0.0F && target->viewport[1] >= 0.0F && "nt_ui_walk: target->viewport origin (x,y) must be non-negative");
    /* Zero-size viewport is a legitimate runtime state (minimized browser
     * tab, mobile orientation transition). Silent no-op rather than assert. */
    if (target->viewport[2] <= 0.0F || target->viewport[3] <= 0.0F) {
        return;
    }
    NT_ASSERT(ctx->atlas.id != 0 && "nt_ui_set_atlas_white_region(ctx,...) required before nt_ui_walk");
    NT_ASSERT(nt_resource_is_ready(ctx->atlas) && "nt_ui_walk: ctx atlas must be READY");
    NT_ASSERT(ctx->sprite_material.id != 0 && "nt_ui_set_sprite_material(ctx,...) required before nt_ui_walk");
    NT_ASSERT(ctx->text_material.id != 0 && "nt_ui_set_text_material(ctx,...) required before nt_ui_walk");

    /* On C stack, NOT in ctx, so multi-walk against the same ctx doesn't
     * share state. */
    sscissor_rect_t scissor_stack[NT_UI_WALKER_MAX_SCISSOR_DEPTH];
    int depth = 0;

    /* Drain caller-side pending geometry BEFORE mutating GL state, so
     * leftover staging from a prior scene isn't rendered under our
     * viewport. Both flushes early-out on empty staging. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();

    /* Snapshot AFTER the entry flushes so the per-walk delta excludes the
     * caller's drained geometry. */
    const uint32_t calls_at_entry = nt_gfx_get_frame_draw_calls();

    nt_gfx_set_viewport((int)target->viewport[0], (int)target->viewport[1], (int)target->viewport[2], (int)target->viewport[3]);
    nt_gfx_set_scissor_enabled(false);

    /* Sprite material bound up-front (most commands need it); text binds
     * lazily inside emit_text just before draw_n. */
    nt_sprite_renderer_set_material(ctx->sprite_material);

    /* Segment-aware dispatch -- see nt_ui_walk header doc for contract.
     * Within a segment (same z, no SCISSOR/CUSTOM barrier crossed):
     *   pass 1: scan for layer range [min..max] used in this segment.
     *   pass 2: for each layer in that range, emit commands matching it
     *           in declaration order. NULL userData -> layer 0.
     * For the common single-layer case (all userData NULL), min==max so
     * pass 2 runs once -- O(N) total. */
    const Clay_RenderCommandArray *arr = &ctx->frozen_cmds;
#ifdef NT_TEST_ACCESS
    uint32_t unlayered_count = 0U;
#endif
    int32_t i = 0;
    while (i < arr->length) {
        const Clay_RenderCommand *c = &arr->internalArray[i];
        if (!is_segmentable(c->commandType)) {
            dispatch_command(ctx, c, scissor_stack, &depth, target);
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
        uint8_t min_layer = 255U;
        uint8_t max_layer = 0U;
        for (int32_t j = i; j < seg_end; ++j) {
            const Clay_RenderCommand *cc = &arr->internalArray[j];
            const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
#ifdef NT_TEST_ACCESS
            if (cc->userData == NULL) {
                ++unlayered_count;
            }
#endif
            if (layer < min_layer) {
                min_layer = layer;
            }
            if (layer > max_layer) {
                max_layer = layer;
            }
        }
        for (uint32_t layer = (uint32_t)min_layer; layer <= (uint32_t)max_layer; ++layer) {
            for (int32_t j = i; j < seg_end; ++j) {
                const Clay_RenderCommand *cc = &arr->internalArray[j];
                const uint8_t cmd_layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
                if ((uint32_t)cmd_layer == layer) {
                    dispatch_command(ctx, cc, scissor_stack, &depth, target);
                }
            }
        }
        i = seg_end;
    }

    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    NT_ASSERT(depth == 0 && "unbalanced scissor stack at walk exit");
    nt_gfx_set_scissor_enabled(false);

    /* Guard underflow if a CUSTOM handler reset frame stats mid-walk --
     * unsigned wrap would produce a spurious huge delta. */
    const uint32_t calls_after = nt_gfx_get_frame_draw_calls();
    NT_ASSERT(calls_after >= calls_at_entry && "nt_ui_walk: frame draw-call counter went backwards (CUSTOM handler reset stats?)");
    ctx->last_walk_draw_call_delta = calls_after - calls_at_entry;
    ctx->last_walk_element_count = (uint32_t)arr->length;
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
    NT_ASSERT(r != NULL && r->vertex_count > 0U && "nt_ui_set_atlas_white_region: white region missing / tombstoned (mis-baked atlas)");
    /* emit_screen_rect scales by mat4(w,h) on cached_pos {0,1}x{0,1}. */
    NT_ASSERT(r->source_w == 1 && r->source_h == 1 && "nt_ui_set_atlas_white_region: white region must be 1x1 source");
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
    /* NULL fn is allowed -- legitimate "reserved space" pattern. */
    ctx->custom_fn = fn;
    ctx->custom_user = userdata;
}
// #endregion

// #region public_metrics
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_draw_calls: ctx must be non-NULL");
    return ctx->last_walk_draw_call_delta;
}

uint32_t nt_ui_get_last_walk_element_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_element_count: ctx must be non-NULL");
    return ctx->last_walk_element_count;
}
// #endregion

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void) { return g_nt_ui_inframe_ctx; }

nt_resource_t nt_ui_test_atlas(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return ctx->atlas;
}
uint32_t nt_ui_test_white_region(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return ctx->white_region;
}
nt_material_t nt_ui_test_sprite_material(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return ctx->sprite_material;
}
nt_material_t nt_ui_test_text_material(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return ctx->text_material;
}

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
#endif
// #endregion
