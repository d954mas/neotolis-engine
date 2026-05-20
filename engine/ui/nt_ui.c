#include "ui/nt_ui.h"

#include "atlas/nt_atlas.h"
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
/* Returns {0,0} on every defensive path: no ctx mid-frame, NULL config,
 * out-of-range fontId, invalid font handle. Clay tolerates zero dims. */
static Clay_Dimensions nt_ui_measure_text_cb(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data;

    nt_ui_context_t *ctx = g_nt_ui_inframe_ctx;
    if (ctx == NULL || config == NULL || config->fontId >= NT_UI_MAX_FONTS) {
        return (Clay_Dimensions){0};
    }
    nt_font_t font = ctx->fonts[config->fontId];
    if (!nt_font_valid(font)) {
        return (Clay_Dimensions){0};
    }
    nt_text_size_t s = nt_font_measure_n(font, text.chars, (size_t)text.length, (float)config->fontSize);
    return (Clay_Dimensions){.width = s.width, .height = s.height};
}
// #endregion

// #region create_destroy
/* ctx struct rounded up to cache-line so Clay's arena starts aligned. */
static size_t nt_ui_ctx_size_aligned(void) { return (sizeof(struct nt_ui_context) + 63U) & ~(size_t)63U; }

size_t nt_ui_min_arena_size(void) { return nt_ui_ctx_size_aligned() + (size_t)Clay_MinMemorySize(); }

nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size) {
    NT_ASSERT(arena != NULL && "nt_ui_create_context: arena must be non-NULL");
    NT_ASSERT(((uintptr_t)arena & (NT_UI_ARENA_ALIGN - 1U)) == 0U && "nt_ui_create_context: arena must be NT_UI_ARENA_ALIGN-aligned (alignas(NT_UI_ARENA_ALIGN) static uint8_t arena[N])");
    NT_ASSERT(arena_size >= nt_ui_min_arena_size() && "nt_ui_create_context: arena_size < nt_ui_min_arena_size()");

    nt_ui_context_t *ctx = (nt_ui_context_t *)arena;
    memset(ctx, 0, sizeof(*ctx));

    const size_t ctx_size = nt_ui_ctx_size_aligned();
    void *clay_mem = (char *)arena + ctx_size;
    const size_t clay_size = arena_size - ctx_size;

    ctx->arena_base = arena;
    ctx->arena_size = arena_size;
    ctx->in_frame = false;
    ctx->clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_size, clay_mem);
    ctx->clay = Clay_Initialize(ctx->clay_arena, (Clay_Dimensions){.width = 1.0F, .height = 1.0F}, (Clay_ErrorHandler){.errorHandlerFunction = nt_ui_clay_error_cb, .userData = ctx});

    /* Clay_SetMeasureTextFunction is idempotent; wire unconditionally. */
    Clay_SetMeasureTextFunction(nt_ui_measure_text_cb, NULL);

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
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_set_font: font_id out of range (raise NT_UI_MAX_FONTS)");
    ctx->fonts[font_id] = font;
}
// #endregion

// #region begin_end
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(mouse != NULL && "nt_ui_begin: mouse must be non-NULL");
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

// #region helper_emit_border
/* Up to 4 thin RECT quads. Top/bottom run the full width; left/right are
 * inset by top/bottom heights so corners don't double-blend. */
static void emit_border(const nt_ui_context_t *ctx, const Clay_RenderCommand *c) {
    const Clay_BorderRenderData *b = &c->renderData.border;
    const Clay_BoundingBox bb = c->boundingBox;
    const uint32_t col = nt_color_pack_clay(b->color);
    const nt_resource_t atlas = ctx->atlas;
    const uint32_t wr = ctx->white_region;

    if (b->width.top) {
        emit_screen_rect(atlas, wr, bb.x, bb.y, bb.width, (float)b->width.top, col);
    }
    if (b->width.bottom) {
        emit_screen_rect(atlas, wr, bb.x, bb.y + bb.height - (float)b->width.bottom, bb.width, (float)b->width.bottom, col);
    }
    if (b->width.left) {
        emit_screen_rect(atlas, wr, bb.x, bb.y + (float)b->width.top, (float)b->width.left, bb.height - (float)b->width.top - (float)b->width.bottom, col);
    }
    if (b->width.right) {
        emit_screen_rect(atlas, wr, bb.x + bb.width - (float)b->width.right, bb.y + (float)b->width.top, (float)b->width.right, bb.height - (float)b->width.top - (float)b->width.bottom, col);
    }
}
// #endregion

// #region helper_emit_image
/* IMAGE uses the PAYLOAD's atlas, not ctx->atlas (the latter is for
 * RECT/BORDER's white region). Different atlas pages auto-flush via the
 * sprite renderer's ensure_current_cmd_page_texture path. */
static void emit_image(const Clay_RenderCommand *c) {
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    if (p == NULL) {
        return; /* Clay does not enforce non-NULL imageData */
    }

    /* id == 0 is a caller bug. not-READY is a legitimate transient state
     * (async load), so silently skip this frame -- next frame will draw. */
    NT_ASSERT(p->atlas.id != 0 && "nt_ui IMAGE payload: invalid atlas handle (zero id)");
    if (!nt_resource_is_ready(p->atlas)) {
        return;
    }

    const Clay_BoundingBox bb = c->boundingBox;

    /* Clay default backgroundColor for IMAGE is {0,0,0,0} == untinted. Map
     * to 0xFFFFFFFF so the per-vertex tint multiply is a no-op. */
    Clay_Color tint = c->renderData.image.backgroundColor;
    const uint32_t col = (tint.r == 0.0F && tint.g == 0.0F && tint.b == 0.0F && tint.a == 0.0F) ? 0xFFFFFFFFU : nt_color_pack_clay(tint);

    const nt_texture_region_t *r = nt_atlas_get_region(p->atlas, p->region_index);
    if (r == NULL || r->vertex_count == 0U) {
        return; /* tombstone or out-of-range */
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
    if ((uint32_t)t->fontId >= NT_UI_MAX_FONTS) {
        rebind_sprite_after_flush(ctx);
        return;
    }
    nt_font_t font = ctx->fonts[t->fontId];
    if (!nt_font_valid(font)) {
        rebind_sprite_after_flush(ctx);
        return;
    }

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
    nt_text_renderer_draw_n(t->stringContents.chars, (size_t)t->stringContents.length, m, (float)t->fontSize, color);
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

    /* floor min / ceil max: a (int)truncate would clip subpixel-aligned
     * UI by 1px at the right/bottom edge. */
    const float bx = c->boundingBox.x;
    const float by = c->boundingBox.y;
    int x = (int)floorf(bx);
    int y = (int)floorf(by);
    int wp = (int)ceilf(bx + c->boundingBox.width) - x;
    int hp = (int)ceilf(by + c->boundingBox.height) - y;

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
    const int vx = (int)target->viewport[0];
    const int vy = (int)target->viewport[1];
    const int vh = (int)target->viewport[3];
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
/* Extracted from nt_ui_walk so the loop body stays under clang-tidy's
 * cognitive-complexity threshold. */
static void dispatch_command(nt_ui_context_t *ctx, const Clay_RenderCommand *c, sscissor_rect_t *scissor_stack, int *depth, const nt_ui_target_t *target) {
    switch (c->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
        return;
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
        const Clay_RectangleRenderData *r = &c->renderData.rectangle;
        const uint32_t col = nt_color_pack_clay(r->backgroundColor);
        emit_screen_rect(ctx->atlas, ctx->white_region, c->boundingBox.x, c->boundingBox.y, c->boundingBox.width, c->boundingBox.height, col);
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

/* Depth test/write must be baked into the UI material's pipeline
 * (depth_test=false, depth_write=false). nt_gfx has no per-frame depth
 * API, so the walker can't enforce this -- it's a caller contract. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    NT_ASSERT(ctx != NULL && "nt_ui_walk: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_walk: target must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_walk: ctx is mid-frame (call nt_ui_end first)");
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

    /* Clay's frozen array is already zIndex-ascending sorted. */
    const Clay_RenderCommandArray *arr = &ctx->frozen_cmds;
    for (int32_t i = 0; i < arr->length; ++i) {
        dispatch_command(ctx, &arr->internalArray[i], scissor_stack, &depth, target);
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
}
// #endregion

// #region setters
/* Atlas setter asserts the white region resolves with vertex_count > 0
 * so a mis-baked atlas surfaces here, not one frame later in the walker.
 * Material setters only check .id != 0 -- pipeline readiness is checked
 * later when the renderer's set_material actually consumes the handle. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_set_atlas_white_region(nt_ui_context_t *ctx, nt_resource_t atlas, uint32_t white_region_idx) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_atlas_white_region: ctx must be non-NULL");
    NT_ASSERT(atlas.id != 0 && "nt_ui_set_atlas_white_region: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_ui_set_atlas_white_region: atlas must be READY");
    const nt_texture_region_t *r = nt_atlas_get_region(atlas, white_region_idx);
    NT_ASSERT(r != NULL && r->vertex_count > 0U && "nt_ui_set_atlas_white_region: white region missing / tombstoned (mis-baked atlas)");
    ctx->atlas = atlas;
    ctx->white_region = white_region_idx;
}

void nt_ui_set_sprite_material(nt_ui_context_t *ctx, nt_material_t sprite_material) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_sprite_material: ctx must be non-NULL");
    NT_ASSERT(sprite_material.id != 0 && "nt_ui_set_sprite_material: invalid material handle");
    ctx->sprite_material = sprite_material;
}

void nt_ui_set_text_material(nt_ui_context_t *ctx, nt_material_t text_material) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_text_material: ctx must be non-NULL");
    NT_ASSERT(text_material.id != 0 && "nt_ui_set_text_material: invalid material handle");
    ctx->text_material = text_material;
}

void nt_ui_set_custom_handler(nt_ui_context_t *ctx, nt_ui_custom_handler_t fn, void *userdata) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_custom_handler: ctx must be non-NULL");
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
#ifdef NT_UI_TEST_ACCESS
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
