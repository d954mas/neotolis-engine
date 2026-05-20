#ifndef NT_UI_H
#define NT_UI_H

/*
 * nt_ui -- Immediate-mode UI module built atop Clay v0.14.
 *
 * Frame lifecycle (Phase 52):
 *
 *   1) Once at boot:
 *        ctx = nt_ui_create_context(arena, sizeof arena);
 *        nt_ui_set_font(ctx, 0, font);
 *        nt_ui_set_atlas_white_region(atlas, white_region_idx);
 *        nt_ui_set_sprite_material(sprite_material);
 *        nt_ui_set_text_material(text_material);
 *
 *   2) Per frame, declaration phase (Phase 1):
 *        nt_pointer_t mouse = g_nt_input.pointers[0];
 *        nt_ui_begin(ctx, screen_w, screen_h, &mouse);
 *        // ... Clay declarations (CLAY({...}) { ... }) ...
 *        nt_ui_end(ctx);
 *
 *   3) Per frame, render phase (Phase 2) -- can be called N times against
 *      different targets (split-screen, screenshot FBO):
 *        nt_ui_target_t target = {.viewport = {0, 0, screen_w, screen_h}};
 *        nt_ui_walk(ctx, &target);
 *
 *   4) On shutdown:
 *        nt_ui_destroy_context(ctx);  // does NOT free arena bytes
 *
 * Multi-context invariant (D-52-12 / UI-08): only one context may be
 * in-frame at a time. nt_ui_begin asserts on stray nested begin.
 *
 * Globals UBO ownership (Drift 6): the caller owns the Globals UBO. The
 * walker only writes viewport + scissor state -- it does NOT update
 * Globals. See nt_ui_target_t for details.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "input/nt_input.h"
#include "material/nt_material.h"
#include "resource/nt_resource.h"

/* ---- Compile-time limits ---- */

#ifndef NT_UI_MAX_FONTS
#define NT_UI_MAX_FONTS 8
#endif

#ifndef NT_UI_WALKER_MAX_SCISSOR_DEPTH
#define NT_UI_WALKER_MAX_SCISSOR_DEPTH 8
#endif

/* Default arena size suitable for ~50k Clay elements + 8 fonts + scroll/
 * modal stacks. Caller pattern: static uint8_t g_ui_arena[NT_UI_DEFAULT_ARENA_SIZE]. */
#define NT_UI_DEFAULT_ARENA_SIZE (8u * 1024u * 1024u)

/* ---- Opaque context handle ---- */

typedef struct nt_ui_context nt_ui_context_t;

/* ---- Render target ---- */

/* nt_ui_target_t -- where the walker emits pixels.
 *
 * Phase 52 ships viewport-only support. fbo and projection slots are
 * reserved for future render-to-texture (v1.10+).
 *
 * Drift 6 (Globals UBO ownership): ALL-ZERO projection sentinel means
 * walker does NOT update Globals UBO -- caller must have uploaded a
 * screen-space ortho matching viewport BEFORE nt_ui_walk. Reference
 * examples/bunnymark/main.c lines 353-364 (build nt_frame_uniforms_t)
 * and line 506 (nt_gfx_register_global_block("Globals", 0)) for the
 * canonical caller pattern.
 *
 * viewport layout: {x, y, width, height} in framebuffer pixels.
 */
typedef struct {
    uint32_t fbo;         /* 0 = default framebuffer; non-zero reserved */
    float viewport[4];    /* {x, y, width, height} */
    float projection[16]; /* row-major; all-zero = caller owns Globals UBO */
} nt_ui_target_t;

/* ---- Image payload (D-52-07) ----
 *
 * Caller fills + assigns pointer to Clay_ImageElementConfig.imageData.
 * Lifetime: must live until the matching nt_ui_walk call returns.
 *
 * D-52-08: slice9_lrtb bytes are RESERVED in Phase 52 -- silently ignored
 * by the walker (plain-quad emit). Phase 54 will fill the slice9 emit path.
 */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    uint8_t flip_bits;
    uint8_t slice9_lrtb[4]; /* {left, right, top, bottom} -- reserved, Phase 54 */
} nt_ui_image_payload_t;

/* ---- CUSTOM-command handler (D-52-09, Option A) ----
 *
 * clay_cmd is opaque (const Clay_RenderCommand *) -- caller casts back
 * if needed. Option A keeps clay.h private to the nt_ui TU.
 */
typedef void (*nt_ui_custom_handler_t)(const void *clay_cmd, void *userdata);

/* ---- Global setters (implementations in Plan 04) ---- */

/* White-region & atlas setter -- one call between init and first walk.
 * Walker asserts atlas is valid + ready before each walk. */
void nt_ui_set_atlas_white_region(nt_resource_t atlas, uint32_t white_region_idx);

/* Material setters -- symmetric pair (Revision Issue 1, D-52-19).
 * Sprite material drives RECT/BORDER/IMAGE; text material drives TEXT.
 * Both must be set before first nt_ui_walk (walker asserts at entry).
 * Phase 53 theme sets both atomically; Phase 52 setters are the bootstrap.
 * Single-material reuse was rejected because sprite and text use different
 * nt_material_t (different shaders) and cannot share a pipeline slot. */
void nt_ui_set_sprite_material(nt_material_t sprite_material);
void nt_ui_set_text_material(nt_material_t text_material);

/* CUSTOM handler -- global, NULL = silent skip. */
void nt_ui_set_custom_handler(nt_ui_custom_handler_t fn, void *userdata);

/* ---- Lifecycle (D-52-10) ---- */

/* Returns the minimum arena size required by nt_ui_create_context.
 * Includes sizeof(nt_ui_context_t) + Clay_MinMemorySize() + alignment slack. */
size_t nt_ui_min_arena_size(void);

/* Creates a UI context in caller-provided arena memory.
 * Asserts: arena != NULL, arena_size >= nt_ui_min_arena_size(),
 *          ((uintptr_t)arena & 7u) == 0u (8-byte alignment).
 * Returns a pointer into the first ~256 bytes of arena. */
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size);

/* Tears down a context. Does NOT touch arena memory -- caller owns it. */
void nt_ui_destroy_context(nt_ui_context_t *ctx);

/* ---- Per-context font registry (D-52-15) ---- */

/* Registers a font handle at the given font_id slot. Clay's fontId field
 * indexes into this table when the measure callback fires. Phase 53 theme
 * will own a canonical font table -- Phase 52 setter is the bootstrap. */
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

/* ---- Per-frame lifecycle ---- */

/* Begin declaration phase.
 * - FIRST action: Clay_SetCurrentContext(ctx->clay) (UI-04 / CP-03 prevention)
 * - Sets ctx->in_frame, g_inframe_ctx
 * - Asserts !ctx->in_frame, g_inframe_ctx == NULL (D-52-12 multi-context invariant)
 * - Forwards pointer state to Clay (Phase 52: pointer body wired in Plan 03)
 * - Calls Clay_BeginLayout */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse);

/* End declaration phase. Calls Clay_EndLayout and freezes commands into ctx.
 * Asserts ctx->in_frame and ctx == g_inframe_ctx. */
void nt_ui_end(nt_ui_context_t *ctx);

/* Render phase -- iterates frozen commands and dispatches to renderers.
 * Read-only on ctx; can be called multiple times per frame against
 * different targets (split-screen / screenshot FBO).
 * Phase 52: body lands in Plan 04. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* ---- Test access (compiled only when NT_UI_TEST_ACCESS is defined) ---- */

#ifdef NT_UI_TEST_ACCESS
/* Module-level in-frame pointer (D-52-12). NULL when no context is mid-frame. */
nt_ui_context_t *nt_ui_test_inframe_ctx(void);

/* Plan 04 captures into static module state at walk exit; Plan 05 will
 * additionally route these through nt_stats_count. */
uint32_t nt_ui_test_last_walk_draw_call_delta(void);
uint32_t nt_ui_test_last_walk_element_count(void);

/* Plan 03: snapshot of the Clay-side pointer state for the given ctx,
 * intended for CLAY-04 / D-52-16 verification. Returns the values that
 * the most recent Clay_SetPointerState call (inside nt_ui_begin) wrote
 * to ctx->clay->pointerInfo. The struct stays Clay-opaque in the public
 * header -- the getters surface only the scalar fields tests need. */
float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx);
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx);
/* 0 = released-family, 1 = pressed-family. */
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx);

/* Plan 04: walker-setter introspection + reset.
 * Tests use these to verify what the setters wrote and to reset the
 * module-level globals for death-tests that need a "no setter called"
 * pre-state (e.g. walk_without_atlas_asserts). */
nt_resource_t nt_ui_test_atlas(void);
uint32_t nt_ui_test_white_region(void);
nt_material_t nt_ui_test_sprite_material(void);
nt_material_t nt_ui_test_text_material(void);
void nt_ui_test_reset_walker_globals(void);
#endif /* NT_UI_TEST_ACCESS */

#endif /* NT_UI_H */
