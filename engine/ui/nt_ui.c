#include "ui/nt_ui.h"

#include "atlas/nt_atlas.h"
#include "core/nt_builtins.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"

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
#include <string.h>

#include "core/nt_align.h"
#include "core/nt_assert.h"
#include "core/nt_clamp.h"
#include "log/nt_log.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_internal.h"

// #region custom_markers
/* Magic value to identify engine-internal CUSTOM markers.
 * Checked before reading marker_type -- game CUSTOM commands
 * without this sentinel are forwarded to custom_fn as usual. */
#define NT_UI_CUSTOM_MARKER_MAGIC 0xC1A4FEEDU

enum {
    NT_UI_CUSTOM_PUSH_TRANSFORM = 1,
    NT_UI_CUSTOM_POP_TRANSFORM = 2,
    NT_UI_CUSTOM_PUSH_OPACITY = 3,
    NT_UI_CUSTOM_POP_OPACITY = 4,
};

typedef struct {
    uint32_t magic;              /* MUST be NT_UI_CUSTOM_MARKER_MAGIC */
    uint8_t marker_type;         /* NT_UI_CUSTOM_PUSH_TRANSFORM etc. */
    nt_ui_transform_t transform; /* only for PUSH_TRANSFORM */
    float opacity;               /* only for PUSH_OPACITY */
} nt_ui_custom_marker_t;
// #endregion

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
    return NT_ALIGN_UP(sizeof(struct nt_ui_context), NT_UI_CACHE_LINE) + clay_bytes;
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
    /* Layout: [ctx struct][Clay arena], both NT_UI_ARENA_ALIGN aligned. */
    ctx->max_elements = desc->max_elements;
    void *clay_mem = (char *)arena + ctx_size;
    const size_t clay_size = arena_size - ctx_size;

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
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, float dt, const nt_pointer_t *mouse) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_begin: nt_ui_module_init() must be called before begin");
    NT_ASSERT(mouse != NULL && "nt_ui_begin: mouse must be non-NULL");
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

    /* Must run before BeginLayout: Clay reserves debug panel width up-front. */
    Clay_SetDebugModeEnabled(ctx->debug_overlay);
    Clay_SetLayoutDimensions((Clay_Dimensions){.width = screen_w, .height = screen_h});

    /* Left-button only; Clay v0.14 has no right/middle/wheel buttons. */
    Clay_SetPointerState((Clay_Vector2){.x = mouse->x, .y = mouse->y}, mouse->buttons[NT_BUTTON_LEFT].is_down);

    /* Forward wheel + enable touch/drag-scroll (mobile/web pointer drag inside
     * a Clay clip scrolls it). Y inverted: Clay scroll opposite of typical wheel_dy. */
    Clay_UpdateScrollContainers(true, (Clay_Vector2){.x = mouse->wheel_dx, .y = -mouse->wheel_dy}, dt);

    Clay_BeginLayout();
}

/* Takes effect on next nt_ui_begin (Clay needs the flag before BeginLayout). */
void nt_ui_set_debug_overlay(nt_ui_context_t *ctx, bool enabled) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_debug_overlay: ctx must be non-NULL");
    ctx->debug_overlay = enabled;
}

bool nt_ui_get_debug_overlay(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_debug_overlay: ctx must be non-NULL");
    return ctx->debug_overlay;
}

void nt_ui_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_end: ctx is not in_frame (begin was not called)");
    NT_ASSERT(ctx == g_nt_ui_inframe_ctx && "nt_ui_end: ctx mismatch with module in-frame ctx");

    ctx->frozen_cmds = Clay_EndLayout();
    /* Clay's debug overlay close button ("x") flips internal debugModeEnabled.
     * Sync back so caller's flag stays in lock-step (next begin re-applies). */
    ctx->debug_overlay = Clay_IsDebugModeEnabled();
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

// #region helper_emit_screen_rect
static inline void emit_screen_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, uint32_t color_packed) {
    const float m[16] = {
        w, 0.0F, 0.0F, 0.0F, 0.0F, h, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, x, y, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_region(atlas, region_index, m, 0.0F, 0.0F, color_packed, 0U);
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
static void emit_rounded_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, Clay_CornerRadius cr, uint32_t color_packed) {
    float tl = cr.topLeft;
    float tr = cr.topRight;
    float bl = cr.bottomLeft;
    float br = cr.bottomRight;
    clamp_radii_css3(w, h, &tl, &tr, &bl, &br);
    const float half_w = w * 0.5F;
    const float half_h = h * 0.5F;

    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_screen_rect(atlas, region_index, x, y, w, h, color_packed);
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

    const float identity[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, identity, color_packed);
}
// #endregion

// #region helper_emit_border
/* Top/bottom run full width; left/right inset to avoid corner overlap. */
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
static void emit_rounded_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, float tl, float tr, float bl, float br, uint32_t color_packed) {
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

    const float identity[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, identity, color_packed);
}

static void emit_border(const nt_ui_context_t *ctx, const Clay_RenderCommand *c) {
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
        emit_square_border(ctx->atlas, ctx->white_region, bb, b->width, col);
        return;
    }
    emit_rounded_border(ctx->atlas, ctx->white_region, bb, b->width, tl, tr, bl, br, col);
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

    /* Auto-slice9: per-widget override > atlas default > regular quad */
    const bool has_override = (p->slice9_override[0] | p->slice9_override[1] | p->slice9_override[2] | p->slice9_override[3]) != 0;
    const bool region_slice9 = (r->flags & NT_ATLAS_REGION_FLAG_SLICE9) != 0;

    if (has_override || region_slice9) {
        uint8_t sl;
        uint8_t sr;
        uint8_t st;
        uint8_t sb;
        if (has_override) {
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

    if (rotation == 0.0F) {
        const float m[16] = {
            sx_f, 0.0F, 0.0F, 0.0F, 0.0F, sy_f, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, bb.x, bb.y, 0.0F, 1.0F,
        };
        nt_sprite_renderer_emit_region(p->atlas, p->region_index, m, 0.0F, 0.0F, col, p->flip_bits);
    } else {
        const float rcx = bb.x + (bb.width * 0.5F);
        const float rcy = bb.y + (bb.height * 0.5F);
        const float rc = cosf(rotation);
        const float rs = sinf(rotation);
        const float m[16] = {
            sx_f * rc, sx_f * rs, 0, 0,
            sy_f * (-rs), sy_f * rc, 0, 0,
            0, 0, 1, 0,
            rcx - (rc * rcx) + (rs * rcy), rcy - (rs * rcx) - (rc * rcy), 0, 1,
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
            rc, rs, 0, 0, -rs, rc, 0, 0, 0, 0, 1, 0,
            bcx + (rc * dx) - (rs * dy), bcy + (rs * dx) + (rc * dy), 0, 1,
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
    float accum_scale;
    float accum_rotation;
    int pending_center_depth;
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
    ws->accum_scale = 1.0F;
    ws->accum_rotation = 0;
    ws->pending_center_depth = -1;
    ws->opacity_depth = 0;
    ws->accum_opacity = 1.0F;
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
    ws->accum_scale = 1.0F;
    ws->accum_rotation = 0;
    for (int k = 0; k < ws->transform_depth; ++k) {
        const float s = ws->transform_stack[k].scale;
        const float r = ws->transform_stack[k].rotation;
        const float cx = ws->push_center_x[k];
        const float cy = ws->push_center_y[k];
        const float ox = ws->transform_stack[k].offset_x;
        const float oy = ws->transform_stack[k].offset_y;
        const float cs = cosf(r) * s;
        const float sn = sinf(r) * s;
        /* Local 2x2: la=cs, lb=-sn, lc=sn, ld=cs */
        /* Local translate: ltx = cx - cs*cx + sn*cy + ox
         *                  lty = cy - sn*cx - cs*cy + oy */
        const float ltx = cx - (cs * cx) + (sn * cy) + ox;
        const float lty = cy - (sn * cx) - (cs * cy) + oy;
        /* Compose: new = local * accumulated */
        const float na = (cs * ws->aff_a) + ((-sn) * ws->aff_c);
        const float nb = (cs * ws->aff_b) + ((-sn) * ws->aff_d);
        const float nc = (sn * ws->aff_a) + (cs * ws->aff_c);
        const float nd = (sn * ws->aff_b) + (cs * ws->aff_d);
        const float ntx = (cs * ws->aff_tx) + ((-sn) * ws->aff_ty) + ltx;
        const float nty = (sn * ws->aff_tx) + (cs * ws->aff_ty) + lty;
        ws->aff_a = na;
        ws->aff_b = nb;
        ws->aff_c = nc;
        ws->aff_d = nd;
        ws->aff_tx = ntx;
        ws->aff_ty = nty;
        ws->accum_scale *= s;
        ws->accum_rotation += r;
    }
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

/* Returns true if marker was handled (engine internal); false = forward to game. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool try_handle_custom_marker(const Clay_RenderCommand *c, nt_ui_walker_state_t *ws) {
    const void *data = c->renderData.custom.customData;
    if (data == NULL) {
        return false;
    }
    const nt_ui_custom_marker_t *marker = (const nt_ui_custom_marker_t *)data;
    if (marker->magic != NT_UI_CUSTOM_MARKER_MAGIC) {
        return false;
    }
    switch (marker->marker_type) {
    case NT_UI_CUSTOM_PUSH_TRANSFORM: {
        NT_ASSERT(ws->transform_depth < NT_UI_TRANSFORM_STACK_DEPTH_CAP && "transform stack overflow");
        const int d = ws->transform_depth;
        ws->transform_stack[d] = marker->transform;
        ws->push_center_x[d] = 0;
        ws->push_center_y[d] = 0;
        ws->transform_depth = d + 1;
        /* Offset applies immediately; scale/rotation center deferred. */
        ws->aff_tx += marker->transform.offset_x;
        ws->aff_ty += marker->transform.offset_y;
        if (marker->transform.scale != 1.0F || marker->transform.rotation != 0.0F) {
            ws->pending_center_depth = d;
        }
        return true;
    }
    case NT_UI_CUSTOM_POP_TRANSFORM:
        NT_ASSERT(ws->transform_depth > 0 && "transform stack underflow");
        --ws->transform_depth;
        walker_recompute_transform(ws);
        return true;
    case NT_UI_CUSTOM_PUSH_OPACITY:
        NT_ASSERT(ws->opacity_depth < NT_UI_OPACITY_STACK_DEPTH_CAP && "opacity stack overflow");
        ws->opacity_stack[ws->opacity_depth++] = marker->opacity;
        ws->accum_opacity *= marker->opacity;
        return true;
    case NT_UI_CUSTOM_POP_OPACITY:
        NT_ASSERT(ws->opacity_depth > 0 && "opacity stack underflow");
        --ws->opacity_depth;
        walker_recompute_opacity(ws);
        return true;
    default:
        return false;
    }
}

/* Handler may bind its own pipeline; flush both. Sprite rebind is lazy
 * (deferred to first sprite-backed emit after). */
static void emit_custom(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, bool *sprite_pipeline_dirty, nt_ui_walker_state_t *ws) {
    /* Check magic BEFORE reading marker_type -- game data without the
     * sentinel is forwarded to custom_fn as usual. */
    if (try_handle_custom_marker(c, ws)) {
        return; /* engine marker consumed */
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
                             nt_ui_walker_state_t *ws) {
    const float vy = target->viewport[1];
    const float vh = target->viewport[3];

    /* Deferred scale center: capture from first renderable after push. */
    if (ws->pending_center_depth >= 0 && c->boundingBox.width > 0 && c->commandType != CLAY_RENDER_COMMAND_TYPE_CUSTOM && c->commandType != CLAY_RENDER_COMMAND_TYPE_NONE &&
        c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_START && c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
        const int d = ws->pending_center_depth;
        ws->push_center_x[d] = c->boundingBox.x + (c->boundingBox.width * 0.5F);
        ws->push_center_y[d] = c->boundingBox.y + (c->boundingBox.height * 0.5F);
        ws->pending_center_depth = -1;
        walker_recompute_transform(ws);
    }

    /* Apply accumulated 2D affine: pos' = pos * M + T.
     * Position uses full affine; size uses accum_scale (uniform). */
    const float sc = ws->accum_scale;
    const float orig_cx = c->boundingBox.x + (c->boundingBox.width * 0.5F);
    const float orig_cy = c->boundingBox.y + (c->boundingBox.height * 0.5F);
    const float tcx = (orig_cx * ws->aff_a) + (orig_cy * ws->aff_b) + ws->aff_tx;
    const float tcy = (orig_cx * ws->aff_c) + (orig_cy * ws->aff_d) + ws->aff_ty;
    const float sw = c->boundingBox.width * sc;
    const float sh = c->boundingBox.height * sc;
    Clay_BoundingBox sbb = {.x = tcx - (sw * 0.5F), .y = tcy - (sh * 0.5F), .width = sw, .height = sh};
    const float world_y = vy + vh - sbb.y - sbb.height;

    switch (c->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
        return;
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
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
        emit_rounded_rect(ctx->atlas, ctx->white_region, sbb.x, world_y, sbb.width, sbb.height, cr, col);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
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
        emit_border(ctx, &local);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
        /* Drain sprite before switching to text pipeline; mark for lazy rebind. */
        nt_sprite_renderer_flush();
        *sprite_pipeline_dirty = true;
        Clay_RenderCommand local = *c;
        local.boundingBox = (Clay_BoundingBox){.x = sbb.x, .y = world_y, .width = sbb.width, .height = sbb.height};
        local.renderData.text.textColor.a *= ws->accum_opacity;
        emit_text(ctx, &local, sc, ws->accum_rotation);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
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
        emit_image(&local, ws->accum_rotation);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
        scissor_push(c, scissor_stack, depth, target, sprite_pipeline_dirty);
        return;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        scissor_pop(scissor_stack, depth, target, sprite_pipeline_dirty);
        return;
    case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
        Clay_RenderCommand local = *c;
        local.boundingBox = (Clay_BoundingBox){.x = sbb.x, .y = world_y, .width = sbb.width, .height = sbb.height};
        emit_custom(ctx, &local, sprite_pipeline_dirty, ws);
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
#ifdef NT_TEST_ACCESS
        ctx->test_last_walk_unlayered_count = 0;
#endif
        return;
    }

    scissor_rect_t scissor_stack[NT_UI_WALKER_SCISSOR_DEPTH_CAP];
    int depth = 0;

    nt_ui_walker_state_t ws;
    walker_state_init(&ws);

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
    int32_t i = 0;
    while (i < arr->length) {
        const Clay_RenderCommand *c = &arr->internalArray[i];
        if (!is_segmentable(c->commandType)) {
            dispatch_command(ctx, c, scissor_stack, &depth, target, &sprite_pipeline_dirty, &ws);
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

        uint32_t active_layers[8] = {0U};
        for (int32_t j = i; j < seg_end; ++j) {
            const Clay_RenderCommand *cc = &arr->internalArray[j];
            const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
            active_layers[layer >> 5U] |= (1U << (layer & 31U));
#ifdef NT_TEST_ACCESS
            if (cc->userData == NULL) {
                ++unlayered_count;
            }
#endif
        }

        /* Iterate set bits via ctz instead of 256-bit linear scan. */
        for (uint32_t word_idx = 0U; word_idx < 8U; ++word_idx) {
            uint32_t mask = active_layers[word_idx];
            while (mask != 0U) {
                const uint32_t bit_idx = (uint32_t)__builtin_ctz(mask);
                mask &= mask - 1U;
                const uint8_t current_layer = (uint8_t)((word_idx << 5U) | bit_idx);
                for (int32_t j = i; j < seg_end; ++j) {
                    const Clay_RenderCommand *cc = &arr->internalArray[j];
                    const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
                    if (layer == current_layer) {
                        dispatch_command(ctx, cc, scissor_stack, &depth, target, &sprite_pipeline_dirty, &ws);
                    }
                }
            }
        }
        i = seg_end;
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
static void emit_marker(uint8_t marker_type, const nt_ui_transform_t *transform, float opacity) {
    nt_ui_custom_marker_t *m = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_marker_t);
    m->magic = NT_UI_CUSTOM_MARKER_MAGIC;
    m->marker_type = marker_type;
    if (transform != NULL) {
        m->transform = *transform;
    } else {
        m->transform = nt_ui_transform_defaults();
    }
    m->opacity = opacity;
    /* Zero-size invisible element carrying the marker as customData. */
    CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(0), .height = CLAY_SIZING_FIXED(0)}}, .custom = {.customData = m}});
}

void nt_ui_push_transform(nt_ui_context_t *ctx, const nt_ui_transform_t *transform) {
    NT_ASSERT(ctx != NULL && "nt_ui_push_transform: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_push_transform: must be called inside begin/end");
    NT_ASSERT(transform != NULL && "nt_ui_push_transform: transform must be non-NULL");
    emit_marker(NT_UI_CUSTOM_PUSH_TRANSFORM, transform, 1.0F);
}

void nt_ui_pop_transform(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_pop_transform: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_pop_transform: must be called inside begin/end");
    emit_marker(NT_UI_CUSTOM_POP_TRANSFORM, NULL, 1.0F);
}

void nt_ui_push_opacity(nt_ui_context_t *ctx, float opacity) {
    NT_ASSERT(ctx != NULL && "nt_ui_push_opacity: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_push_opacity: must be called inside begin/end");
    emit_marker(NT_UI_CUSTOM_PUSH_OPACITY, NULL, opacity);
}

void nt_ui_pop_opacity(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_pop_opacity: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_pop_opacity: must be called inside begin/end");
    emit_marker(NT_UI_CUSTOM_POP_OPACITY, NULL, 1.0F);
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
#endif
// #endregion
