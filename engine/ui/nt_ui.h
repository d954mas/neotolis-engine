#ifndef NT_UI_H
#define NT_UI_H

/*
 * nt_ui -- Immediate-mode UI built on Clay v0.14.
 *
 * Engine init sequence required before nt_ui_walk:
 *   nt_gfx_init(...);
 *   nt_resource_init(...);
 *   nt_atlas_init();
 *   nt_font_init(...);
 *   nt_material_init(...);
 *   nt_sprite_renderer_init(...);
 *   nt_text_renderer_init();
 *
 * Per-context setup (once at boot):
 *   ctx = nt_ui_create_context(arena, sizeof arena);
 *   nt_ui_set_font(ctx, 0, font);
 *   nt_ui_set_atlas_white_region(ctx, atlas, white_region_idx);
 *   nt_ui_set_sprite_material(ctx, sprite_material);
 *   nt_ui_set_text_material(ctx, text_material);
 *
 * Per frame:
 *   nt_ui_begin(ctx, screen_w, screen_h, &mouse);
 *   // ... Clay declarations ...
 *   nt_ui_end(ctx);
 *   nt_ui_walk(ctx, &target);  // may be called N times against different targets
 *
 * Multi-context invariant: only one context may be in-frame at a time.
 * The caller owns the Globals UBO -- the walker writes only viewport
 * and scissor state.
 */

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "input/nt_input.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"

#ifndef NT_UI_MAX_FONTS
#define NT_UI_MAX_FONTS 8
#endif

#ifndef NT_UI_WALKER_MAX_SCISSOR_DEPTH
#define NT_UI_WALKER_MAX_SCISSOR_DEPTH 8
#endif

/* Clay places its Clay_Context at the head of the arena via raw cast;
 * the strictest field there needs max_align_t. A bare `static uint8_t
 * arena[N]` has 1-byte alignment in C, so callers MUST use alignas. */
#define NT_UI_ARENA_ALIGN _Alignof(max_align_t)

#define NT_UI_DEFAULT_ARENA_SIZE (8U * 1024U * 1024U)

/* Declares a correctly-aligned arena. The bare alternative
 *
 *     static uint8_t arena[NT_UI_DEFAULT_ARENA_SIZE];
 *
 * has only 1-byte alignment per the C spec and trips the runtime
 * assert in nt_ui_create_context. This macro produces a definition
 * compatible with file-scope, function-static, and (compilers that
 * accept _Alignas on auto) block-scope. Usage:
 *
 *     NT_UI_DECLARE_ARENA(g_ui_arena, NT_UI_DEFAULT_ARENA_SIZE);
 *     ctx = nt_ui_create_context(g_ui_arena, sizeof g_ui_arena); */
#define NT_UI_DECLARE_ARENA(name, size) alignas(NT_UI_ARENA_ALIGN) uint8_t name[(size)]

typedef struct nt_ui_context nt_ui_context_t;

/* All-zero projection signals "caller owns Globals UBO" -- walker skips
 * the upload and uses whatever ortho the caller wrote before walk. fbo
 * and projection are reserved for future render-to-texture. */
typedef struct {
    uint32_t fbo;         /* 0 = default framebuffer; non-zero reserved */
    float viewport[4];    /* {x, y, w, h} in framebuffer pixels */
    float projection[16]; /* column-major (cglm); all-zero = caller owns Globals UBO */
} nt_ui_target_t;

/* Pointed to by Clay_ImageElementConfig.imageData. Lifetime must extend
 * through the matching nt_ui_walk. slice9_lrtb is reserved (currently
 * ignored, walker emits a plain quad). */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    uint8_t flip_bits;
    uint8_t slice9_lrtb[4]; /* reserved */
} nt_ui_image_payload_t;

/* clay_cmd is an opaque const Clay_RenderCommand * (cast back if needed).
 * Keeping it opaque lets clay.h stay private to the nt_ui TU. */
typedef void (*nt_ui_custom_handler_t)(const void *clay_cmd, void *userdata);

/* All four setters below must be called per-context before its first walk.
 * Per-context (not module-global) so multi-context UIs can carry separate
 * atlases and themes without state swaps between walks. */

/* Atlas + white-region used for RECT and BORDER. IMAGE uses the payload
 * atlas independently. */
void nt_ui_set_atlas_white_region(nt_ui_context_t *ctx, nt_resource_t atlas, uint32_t white_region_idx);

/* Sprite material drives RECT/BORDER/IMAGE; text material drives TEXT.
 * They cannot share a slot -- different shaders, different pipelines. */
void nt_ui_set_sprite_material(nt_ui_context_t *ctx, nt_material_t sprite_material);
void nt_ui_set_text_material(nt_ui_context_t *ctx, nt_material_t text_material);

/* NULL fn silently skips CUSTOM commands. */
void nt_ui_set_custom_handler(nt_ui_context_t *ctx, nt_ui_custom_handler_t fn, void *userdata);

size_t nt_ui_min_arena_size(void);

/* Arena is caller-owned; ctx lives in the first ~256 bytes. Asserts
 * arena != NULL, size >= nt_ui_min_arena_size(), NT_UI_ARENA_ALIGN-aligned. */
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size);

/* Tears down ctx state; caller still owns the arena memory. */
void nt_ui_destroy_context(nt_ui_context_t *ctx);

/* Clay's fontId indexes this per-ctx table when the measure callback fires. */
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

/* First call inside switches Clay's current-context to this ctx so
 * subsequent CLAY({...}) macros operate on it. */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse);

/* Freezes Clay's command array into ctx for subsequent walk(s). */
void nt_ui_end(nt_ui_context_t *ctx);

/* Read-only on ctx; safe to call N times against different targets in
 * the same frame. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* Per-walk metrics captured into ctx at every nt_ui_walk exit. Apps that
 * want them in the debug overlay forward themselves:
 *
 *     nt_ui_walk(ctx, &target);
 *     nt_stats_count("ui_draw_calls",    nt_ui_get_last_walk_draw_calls(ctx));
 *     nt_stats_count("ui_element_count", nt_ui_get_last_walk_element_count(ctx));
 *
 * draw_calls counts physical glDrawElements (one per renderer flush) --
 * the metric reflects batching efficiency, not Clay command count. */
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_element_count(const nt_ui_context_t *ctx);

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void);

/* Clay_Context isn't visible outside the nt_ui TU, so tests need these
 * to read ctx->clay->pointerInfo. */
float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx);
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx);
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx); /* 0 released, 1 pressed */

nt_resource_t nt_ui_test_atlas(const nt_ui_context_t *ctx);
uint32_t nt_ui_test_white_region(const nt_ui_context_t *ctx);
nt_material_t nt_ui_test_sprite_material(const nt_ui_context_t *ctx);
nt_material_t nt_ui_test_text_material(const nt_ui_context_t *ctx);
#endif
// #endregion

#endif /* NT_UI_H */
