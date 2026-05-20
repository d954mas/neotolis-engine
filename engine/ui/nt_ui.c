#include "ui/nt_ui.h"

#include "atlas/nt_atlas.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "stats/nt_stats.h"

/*
 * Clay v0.14 implementation TU.
 *
 * One TU in the entire build defines CLAY_IMPLEMENTATION. This is it.
 *
 * Version pin (per Drift 1 Option D):
 *   - deps/clay/VERSION is the single source of truth.
 *   - engine/ui/CMakeLists.txt parses major/minor and passes them as
 *     CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR via target_compile_definitions.
 *   - The _Static_assert below catches accidental dev-time drift (e.g.,
 *     someone hand-edits deps/clay/VERSION to 0.13 without re-vendoring).
 *   - CLAY_VERSION_MAJOR / CLAY_VERSION_MINOR macros do NOT exist in Clay
 *     v0.14 upstream -- verified by direct read of the v0.14 header.
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
/* g_nt_ui_inframe_ctx -- module-level pointer enforcing the multi-context
 * invariant (D-52-12). Exactly one ctx may be in-frame at a time; nt_ui_begin
 * asserts this is NULL on entry and assigns ctx; nt_ui_end clears it. */
static nt_ui_context_t *g_nt_ui_inframe_ctx = NULL;

/* Plan 04 / Revision Issue 1: walker globals set by nt_ui_set_* APIs.
 *
 * Sprite and text materials are SEPARATE globals -- they map to different
 * nt_material_t handles (different shaders / pipelines), so a single shared
 * slot would force a flush+rebind on every TEXT command. Walker asserts
 * BOTH are valid at entry.
 *
 * g_nt_ui_atlas + g_nt_ui_white_region drive RECT/BORDER emit (and IMAGE's
 * fallback when the payload has no atlas of its own -- D-52-06). */
static nt_resource_t g_nt_ui_atlas;
static uint32_t g_nt_ui_white_region;
static nt_material_t g_nt_ui_sprite_material;
static nt_material_t g_nt_ui_text_material;
static nt_ui_custom_handler_t g_nt_ui_custom_fn;
static void *g_nt_ui_custom_user;

/* Walker stats (D-52-20 / WALK-09). Plan 05 wires them into nt_stats; Plan
 * 04 captures the values into BSS so the test probes can read them. */
static uint32_t s_last_walk_draw_call_delta;
static uint32_t s_last_walk_element_count;
// #endregion

// #region clay_error_handler
/* Forward a Clay error to NT_LOG_WARN. errorText is a Clay_String with
 * .length + .chars (not necessarily NUL-terminated). */
static void nt_ui_clay_error_cb(Clay_ErrorData err) {
    int len = err.errorText.length;
    const char *chars = err.errorText.chars;
    if (chars == NULL || len <= 0) {
        NT_LOG_WARN("clay error type=%d (no text)", (int)err.errorType);
        return;
    }
    NT_LOG_WARN("clay error type=%d: %.*s", (int)err.errorType, len, chars);
}
// #endregion

// #region measure_cb
/* Clay measure callback (CLAY-03 / D-52-14).
 *
 * Forwards to the Phase 51 length-aware measure path through the per-ctx
 * font registry. Phase 51's 256-entry xxHash measure cache (37x hit speedup)
 * is the MP-07 amplification mitigation.
 *
 * Returns {0,0} (no crash) when ctx is not mid-frame, config is NULL, the
 * font slot is out of range, or the slot holds an invalid/destroyed handle.
 *
 * user_data is reserved (NULL in Phase 52); D-52-14 keeps it for future
 * per-context font tables that bypass g_nt_ui_inframe_ctx. */
static Clay_Dimensions nt_ui_measure_text_cb(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data; /* Phase 52 reserves user_data; Plan 03 ships NULL */

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
/* Pattern 2 from 52-RESEARCH.md: caller-owned arena, ctx struct lives in
 * the first ~256 bytes (cache-line aligned), Clay arena takes the rest. */
static size_t nt_ui_ctx_size_aligned(void) { return (sizeof(struct nt_ui_context) + 63u) & ~(size_t)63u; }

size_t nt_ui_min_arena_size(void) { return nt_ui_ctx_size_aligned() + (size_t)Clay_MinMemorySize(); }

nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size) {
    NT_ASSERT(arena != NULL && "nt_ui_create_context: arena must be non-NULL");
    NT_ASSERT(((uintptr_t)arena & 7u) == 0u && "nt_ui_create_context: arena must be 8-byte aligned");
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
    ctx->clay = Clay_Initialize(ctx->clay_arena, (Clay_Dimensions){.width = 1.0f, .height = 1.0f}, (Clay_ErrorHandler){.errorHandlerFunction = nt_ui_clay_error_cb, .userData = ctx});

    /* Clay_SetMeasureTextFunction is idempotent -- always wire (cheap pointer
     * assignment in Clay v0.14). Avoids any lazy-init state of our own. */
    Clay_SetMeasureTextFunction(nt_ui_measure_text_cb, NULL);

    return ctx;
}

void nt_ui_destroy_context(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_destroy_context: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_destroy_context: ctx is mid-frame (call nt_ui_end first)");

    /* Caller owns the arena memory -- only zero the ctx struct portion. */
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
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(mouse != NULL && "nt_ui_begin: mouse must be non-NULL");
    NT_ASSERT(g_nt_ui_inframe_ctx == NULL && "nt_ui_begin: another ctx is mid-frame (D-52-12 violation)");
    NT_ASSERT(!ctx->in_frame && "nt_ui_begin: ctx already in_frame (CP-03 footgun)");

    /* FIRST executable: switch Clay's global current-context pointer to
     * this ctx's Clay context (UI-04 / CP-03 prevention). Every Clay call
     * below operates on ctx->clay. */
    Clay_SetCurrentContext(ctx->clay);

    ctx->in_frame = true;
    g_nt_ui_inframe_ctx = ctx;

    Clay_SetLayoutDimensions((Clay_Dimensions){.width = screen_w, .height = screen_h});

    /* Pointer state (CLAY-04 / D-52-16). Left-button only is intentional in
     * v1.8 — right/middle/wheel are not consumed by Clay v0.14. Multi-pointer
     * is deferred: the game caller chooses which pointer to feed (typically
     * pointers[0]; split-screen uses pointers[N]). */
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
/* Clay packs RGBA as 0..255 floats (clay.h:481). Walker emits as 0xAABBGGRR
 * uint32 (sprite-renderer vertex format -- nt_sprite_vertex_t.color[4]).
 * Saturate at uint8 because Clay does not clamp -- a theme that wrote
 * 256.0f or -1.0f would otherwise wrap around. */
static inline uint8_t clamp_u8(float v) {
    if (v <= 0.0f) {
        return 0u;
    }
    if (v >= 255.0f) {
        return 255u;
    }
    return (uint8_t)v;
}

static inline uint32_t nt_color_pack_clay(Clay_Color c) {
    uint32_t r = clamp_u8(c.r);
    uint32_t g = clamp_u8(c.g);
    uint32_t b = clamp_u8(c.b);
    uint32_t a = clamp_u8(c.a);
    return r | (g << 8) | (b << 16) | (a << 24); /* 0xAABBGGRR */
}
// #endregion

// #region helper_emit_screen_rect
/* D-52-03: 2D-ortho mat4 (scale x,y by w,h; translate to x,y) onto the sprite
 * renderer. Origin {0,0} and flip 0 -- the rect already covers (x,y..x+w,y+h)
 * directly because cached_pos for a 1x1 white region is (0,0)..(1,1) in
 * source space, and the scale m[0]=w / m[5]=h folds that into the target rect. */
static inline void emit_screen_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, uint32_t color_packed) {
    /* Column-major layout: m[0..3]=col0, m[4..7]=col1, m[8..11]=col2, m[12..15]=col3. */
    const float m[16] = {
        w,    0.0f, 0.0f, 0.0f, /* col0: scale x */
        0.0f, h,    0.0f, 0.0f, /* col1: scale y */
        0.0f, 0.0f, 1.0f, 0.0f, /* col2 */
        x,    y,    0.0f, 1.0f  /* col3: translate */
    };
    nt_sprite_renderer_emit_region(atlas, region_index, m, 0.0f, 0.0f, color_packed, 0u);
}
// #endregion

// #region helper_emit_border
/* WALK-04 / D-52-03: BORDER -> up to 4 thin RECT quads via emit_screen_rect.
 * Top/bottom run the full width; left/right are inset by top/bottom heights
 * to avoid double-blending corners (matches 52-RESEARCH.md §BORDER emit). */
static void emit_border(const Clay_RenderCommand *c) {
    const Clay_BorderRenderData *b = &c->renderData.border;
    const Clay_BoundingBox bb = c->boundingBox;
    const uint32_t col = nt_color_pack_clay(b->color);

    if (b->width.top) {
        emit_screen_rect(g_nt_ui_atlas, g_nt_ui_white_region, bb.x, bb.y, bb.width, (float)b->width.top, col);
    }
    if (b->width.bottom) {
        emit_screen_rect(g_nt_ui_atlas, g_nt_ui_white_region, bb.x, bb.y + bb.height - (float)b->width.bottom, bb.width, (float)b->width.bottom, col);
    }
    if (b->width.left) {
        emit_screen_rect(g_nt_ui_atlas, g_nt_ui_white_region, bb.x, bb.y + (float)b->width.top, (float)b->width.left, bb.height - (float)b->width.top - (float)b->width.bottom, col);
    }
    if (b->width.right) {
        emit_screen_rect(g_nt_ui_atlas, g_nt_ui_white_region, bb.x + bb.width - (float)b->width.right, bb.y + (float)b->width.top, (float)b->width.right,
                         bb.height - (float)b->width.top - (float)b->width.bottom, col);
    }
}
// #endregion

// #region helper_emit_image
/* D-52-07 + D-52-08: IMAGE -> read nt_ui_image_payload_t* from imageData,
 * emit one region at bbox.
 *
 * IMPORTANT: image uses the PAYLOAD's atlas, not g_nt_ui_atlas. Different
 * atlas pages auto-flush via the sprite renderer's
 * ensure_current_cmd_page_texture path.
 *
 * slice9_lrtb is reserved in Phase 52 and silently ignored (plain quad emit)
 * until Phase 54 adds the slice9 path. The header documents this contract. */
static void emit_image(const Clay_RenderCommand *c) {
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    if (p == NULL) {
        return; /* tolerate missing payload -- Clay does not enforce */
    }

    /* Payload's atlas is independent of the walker's white-region atlas.
     * Validate it here so the failure points at the IMAGE call site, not
     * deep inside nt_atlas_get_region or the sprite renderer's emit. */
    NT_ASSERT(p->atlas.id != 0 && "nt_ui IMAGE payload: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(p->atlas) && "nt_ui IMAGE payload: atlas not READY");

    const Clay_BoundingBox bb = c->boundingBox;

    /* Clay's default backgroundColor for IMAGE is {0,0,0,0} == untinted
     * (clay.h:481 comment). Map that to 0xFFFFFFFF so the sprite shader's
     * per-vertex tint is a no-op. */
    Clay_Color tint = c->renderData.image.backgroundColor;
    const uint32_t col = (tint.r == 0.0f && tint.g == 0.0f && tint.b == 0.0f && tint.a == 0.0f) ? 0xFFFFFFFFu : nt_color_pack_clay(tint);

    /* Resolve region via the payload's atlas. Tombstones / out-of-range
     * silently no-op via the sprite renderer's emit_region_resolved. */
    const nt_texture_region_t *r = nt_atlas_get_region(p->atlas, p->region_index);
    if (r == NULL || r->vertex_count == 0u) {
        return;
    }
    const float ipu = nt_atlas_get_inverse_pixels_per_unit(p->atlas);
    const float src_w = (float)r->source_w * ipu;
    const float src_h = (float)r->source_h * ipu;
    /* Guard against degenerate source dim (avoid div-by-zero on tombstoned
     * regions that slipped through the vertex_count gate). */
    const float sx = (src_w > 0.0f) ? (bb.width / src_w) : bb.width;
    const float sy = (src_h > 0.0f) ? (bb.height / src_h) : bb.height;

    const float m[16] = {
        sx, 0.0f, 0.0f, 0.0f, 0.0f, sy, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, bb.x, bb.y, 0.0f, 1.0f,
    };
    nt_sprite_renderer_emit_region(p->atlas, p->region_index, m, 0.0f, 0.0f, col, p->flip_bits);
}
// #endregion

/* Forward declaration -- defined alongside the scissor stack helpers. */
static void rebind_sprite_after_flush(void);

// #region helper_emit_text
/* D-52-18: TEXT command emit. Flushes sprite renderer first (different
 * material/pipeline boundary), then binds the text font + text material
 * and forwards to nt_text_renderer_draw_n (Phase 51 length-aware API). */
static void emit_text(nt_ui_context_t *ctx, const Clay_RenderCommand *c) {
    /* State boundary: sprite material/pipeline must flush before text
     * binds its own pipeline. Auto-flush on font/material change happens
     * inside the text renderer, but the sprite renderer's own staging
     * needs to drain here. The matching sprite-rebind happens at the
     * tail so the next RECT/BORDER/IMAGE has a live cmd. */
    nt_sprite_renderer_flush();

    const Clay_TextRenderData *t = &c->renderData.text;
    if ((uint32_t)t->fontId >= NT_UI_MAX_FONTS) {
        rebind_sprite_after_flush();
        return;
    }
    nt_font_t font = ctx->fonts[t->fontId];
    if (!nt_font_valid(font)) {
        rebind_sprite_after_flush();
        return;
    }

    nt_text_renderer_set_font(font);
    nt_text_renderer_set_material(g_nt_ui_text_material);

    /* Translation-only model mat4 -- font size is the scale argument
     * to draw_n, not folded into the model. */
    const float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, c->boundingBox.x, c->boundingBox.y, 0.0f, 1.0f,
    };
    /* Text renderer color contract: float[4] in 0..1 range (engine/renderers
     * /nt_text_renderer.c writes it straight into vertex color via memcpy at
     * line 281). Clay stores 0..255 floats -- divide. */
    const float color[4] = {
        t->textColor.r / 255.0f,
        t->textColor.g / 255.0f,
        t->textColor.b / 255.0f,
        t->textColor.a / 255.0f,
    };
    nt_text_renderer_draw_n(t->stringContents.chars, (size_t)t->stringContents.length, m, (float)t->fontSize, color);
    rebind_sprite_after_flush();
}
// #endregion

// #region helper_scissor_stack
typedef struct {
    int x;
    int y;
    int w;
    int h;
} sscissor_rect_t;

/* Re-bind the sprite material after a flush. nt_sprite_renderer_flush closes
 * the current cmd (cmd_count -> 0), so the next nt_sprite_renderer_emit_region
 * would trip "call nt_sprite_renderer_set_material first" without this. The
 * sprite renderer's set_material is cheap when cmd_count == 0 with same
 * handle: it just re-opens the cmd from the cached pipeline + mat_info. */
static void rebind_sprite_after_flush(void) { nt_sprite_renderer_set_material(g_nt_ui_sprite_material); }

static void scissor_push(const Clay_RenderCommand *c, sscissor_rect_t *stack, int *depth, const nt_ui_target_t *target) {
    NT_ASSERT(*depth < NT_UI_WALKER_MAX_SCISSOR_DEPTH && "scissor stack overflow");

    /* Conservative integer rectangle: floor the min corner, ceil the max
     * corner. (int)truncate would clip subpixel-aligned UI by 1px at the
     * right/bottom edge. */
    const float bx = c->boundingBox.x;
    const float by = c->boundingBox.y;
    int x = (int)floorf(bx);
    int y = (int)floorf(by);
    int wp = (int)ceilf(bx + c->boundingBox.width) - x;
    int hp = (int)ceilf(by + c->boundingBox.height) - y;

    /* Nested scissor -> intersect with top (D-52-17). REPLACE would let an
     * inner widget paint outside its parent's clip, e.g. scroll-content
     * leaking past a modal backdrop. */
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

    /* FLUSH both renderers BEFORE changing GL scissor state. Pending
     * staging written under the prior scissor would otherwise be drawn
     * under the new one. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();

    stack[(*depth)++] = (sscissor_rect_t){.x = x, .y = y, .w = wp, .h = hp};

    /* Y-flip top-left -> GL bottom-left (PP-03 / D-51-04). */
    const int fb_h = (int)target->viewport[3];
    nt_gfx_set_scissor(x, fb_h - y - hp, wp, hp);
    nt_gfx_set_scissor_enabled(true);

    /* Re-open sprite cmd so the next RECT/BORDER/IMAGE can emit. */
    rebind_sprite_after_flush();
}

static void scissor_pop(sscissor_rect_t *stack, int *depth, const nt_ui_target_t *target) {
    NT_ASSERT(*depth > 0 && "scissor underflow");
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    (*depth)--;
    if (*depth == 0) {
        nt_gfx_set_scissor_enabled(false);
    } else {
        sscissor_rect_t r = stack[*depth - 1];
        const int fb_h = (int)target->viewport[3];
        nt_gfx_set_scissor(r.x, fb_h - r.y - r.h, r.w, r.h);
    }
    /* Re-open sprite cmd so the next RECT/BORDER/IMAGE can emit. */
    rebind_sprite_after_flush();
}
// #endregion

// #region helper_emit_custom
/* WALK-05 / D-52-09: CUSTOM -> flush both renderers then invoke the
 * registered handler (NULL handler == silent skip). The handler may bind
 * its own pipeline; on return, we re-bind the sprite material so the next
 * RECT/BORDER/IMAGE has a live cmd. */
static void emit_custom(const Clay_RenderCommand *c) {
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    if (g_nt_ui_custom_fn != NULL) {
        g_nt_ui_custom_fn((const void *)c, g_nt_ui_custom_user);
    }
    rebind_sprite_after_flush();
}
// #endregion

// #region walk
/* D-52-05 / Revision Issue 2: depth test/write are pipeline-baked.
 * UI materials must use pipelines with depth_test=false, depth_write=false.
 * Walker takes no per-frame depth action — nt_gfx has no such API
 * (verified against engine/graphics/nt_gfx.h at plan-write time). */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    NT_ASSERT(ctx != NULL && "nt_ui_walk: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_walk: target must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_walk: ctx is mid-frame (call nt_ui_end first)");
    NT_ASSERT(g_nt_ui_atlas.id != 0 && "nt_ui_set_atlas_white_region required before nt_ui_walk");
    NT_ASSERT(nt_resource_is_ready(g_nt_ui_atlas) && "nt_ui_walk: UI atlas must be READY");
    /* Revision Issue 1: sprite + text materials are separate; assert BOTH. */
    NT_ASSERT(g_nt_ui_sprite_material.id != 0 && "nt_ui_set_sprite_material required before nt_ui_walk");
    NT_ASSERT(g_nt_ui_text_material.id != 0 && "nt_ui_set_text_material required before nt_ui_walk");

    /* Walker-local scissor stack -- D-52-17 (on C stack, NOT in ctx, so
     * multiple walks against the same ctx don't share state). */
    sscissor_rect_t scissor_stack[NT_UI_WALKER_MAX_SCISSOR_DEPTH];
    int depth = 0;

    /* Snapshot draw-call counter for the delta (Plan 05 will route to
     * nt_stats_count; Plan 04 captures into static module state). */
    const uint32_t calls_at_entry = nt_gfx_get_frame_draw_calls();

    /* Apply viewport from target (UI-07). Bottom-left GL convention. */
    nt_gfx_set_viewport((int)target->viewport[0], (int)target->viewport[1], (int)target->viewport[2], (int)target->viewport[3]);

    /* Defensive scissor disable at entry (CP-04 prevention). */
    nt_gfx_set_scissor_enabled(false);

    /* Revision Issue 1: bind SPRITE material for RECT/BORDER/IMAGE emits.
     * TEXT material binds lazily inside emit_text just before draw_n
     * (auto-flush handles the boundary). */
    nt_sprite_renderer_set_material(g_nt_ui_sprite_material);

    /* Iterate Clay's frozen command array -- already zIndex-ascending sorted. */
    const Clay_RenderCommandArray *arr = &ctx->frozen_cmds;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *c = &arr->internalArray[i];
        switch (c->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_NONE:
            break; /* silent skip — Clay sentinel */
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            const Clay_RectangleRenderData *r = &c->renderData.rectangle;
            const uint32_t col = nt_color_pack_clay(r->backgroundColor);
            emit_screen_rect(g_nt_ui_atlas, g_nt_ui_white_region, c->boundingBox.x, c->boundingBox.y, c->boundingBox.width, c->boundingBox.height, col);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
            emit_border(c);
            break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
            emit_text(ctx, c);
            break;
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            emit_image(c);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            scissor_push(c, scissor_stack, &depth, target);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            scissor_pop(scissor_stack, &depth, target);
            break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            emit_custom(c);
            break;
        }
    }

    /* Final flushes + invariant check (WALK-06 + CP-04). */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    NT_ASSERT(depth == 0 && "unbalanced scissor stack at walk exit");
    nt_gfx_set_scissor_enabled(false);

    /* Stats (D-52-20 / WALK-09). Plan 04 captures the per-walk deltas
     * into BSS for test probes; Plan 05 routes them through nt_stats so
     * the overlay surfaces walker overhead alongside FPS/CPU/GPU/Draws.
     * Counter names are conventional strings; nt_stats default capacity
     * (16 slots) is plenty for these two. */
    const uint32_t delta = nt_gfx_get_frame_draw_calls() - calls_at_entry;
    s_last_walk_draw_call_delta = delta;
    s_last_walk_element_count = (uint32_t)arr->length;
    nt_stats_count("ui_draw_calls", (uint64_t)delta);
    nt_stats_count("ui_element_count", (uint64_t)arr->length);
}
// #endregion

// #region setters
/* Walker entry asserts these were set; setters validate at call time so the
 * developer sees the bad input at the call site, not at first walk.
 *
 * Atlas setter: requires non-zero, READY (Drift 5 -- nt_resource_is_ready),
 * AND the named region resolves with vertex_count > 0 (Q4 -- catches a
 * mis-baked atlas one frame earlier than the walker would).
 *
 * Material setters: only check .id != 0 -- Phase 52 doesn't synchronously
 * resolve material pipelines (nt_material_step happens elsewhere); the
 * sprite/text renderers' own set_material asserts material.ready when
 * actually consumed inside the walker. */
void nt_ui_set_atlas_white_region(nt_resource_t atlas, uint32_t white_region_idx) {
    NT_ASSERT(atlas.id != 0 && "nt_ui_set_atlas_white_region: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_ui_set_atlas_white_region: atlas must be READY");
    const nt_texture_region_t *r = nt_atlas_get_region(atlas, white_region_idx);
    NT_ASSERT(r != NULL && r->vertex_count > 0u && "nt_ui_set_atlas_white_region: white region missing / tombstoned (mis-baked atlas)");
    g_nt_ui_atlas = atlas;
    g_nt_ui_white_region = white_region_idx;
}

void nt_ui_set_sprite_material(nt_material_t sprite_material) {
    NT_ASSERT(sprite_material.id != 0 && "nt_ui_set_sprite_material: invalid material handle");
    g_nt_ui_sprite_material = sprite_material;
}

void nt_ui_set_text_material(nt_material_t text_material) {
    NT_ASSERT(text_material.id != 0 && "nt_ui_set_text_material: invalid material handle");
    g_nt_ui_text_material = text_material;
}

void nt_ui_set_custom_handler(nt_ui_custom_handler_t fn, void *userdata) {
    /* NULL fn is allowed -- D-52-09 says CUSTOM with no handler is a
     * silent skip (legitimate "reserved space" pattern). */
    g_nt_ui_custom_fn = fn;
    g_nt_ui_custom_user = userdata;
}
// #endregion

// #region test_access
#ifdef NT_UI_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void) { return g_nt_ui_inframe_ctx; }

/* Plan 04 captures the per-walk deltas; Plan 05 will route them to nt_stats. */
uint32_t nt_ui_test_last_walk_draw_call_delta(void) { return s_last_walk_draw_call_delta; }
uint32_t nt_ui_test_last_walk_element_count(void) { return s_last_walk_element_count; }

/* Plan 04 probes: walker setter state introspection. Tests need these to
 * verify (a) reset semantics for walk_without_* death tests and (b) that
 * setters actually wrote what was passed in. */
nt_resource_t nt_ui_test_atlas(void) { return g_nt_ui_atlas; }
uint32_t nt_ui_test_white_region(void) { return g_nt_ui_white_region; }
nt_material_t nt_ui_test_sprite_material(void) { return g_nt_ui_sprite_material; }
nt_material_t nt_ui_test_text_material(void) { return g_nt_ui_text_material; }

/* Plan 04 probe: clear all walker globals so death-tests can re-setUp from
 * scratch. Setter validation prevents writing back invalid values, so a
 * reset has to go through this probe. */
void nt_ui_test_reset_walker_globals(void) {
    g_nt_ui_atlas = (nt_resource_t){0};
    g_nt_ui_white_region = 0u;
    g_nt_ui_sprite_material = (nt_material_t){0};
    g_nt_ui_text_material = (nt_material_t){0};
    g_nt_ui_custom_fn = NULL;
    g_nt_ui_custom_user = NULL;
    s_last_walk_draw_call_delta = 0u;
    s_last_walk_element_count = 0u;
}

/* CLAY-04 / D-52-16 verification probes. ctx->clay is a Clay_Context whose
 * struct definition is only visible to this TU (CLAY_IMPLEMENTATION above);
 * tests cannot read pointerInfo directly. These getters bridge that gap. */
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
#endif /* NT_UI_TEST_ACCESS */
// #endregion
