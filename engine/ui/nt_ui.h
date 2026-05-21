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
 *   nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
 *   ctx = nt_ui_create_context(arena, sizeof arena, &desc);
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

/* Clay places its Clay_Context at the arena head via raw cast. */
#define NT_UI_ARENA_ALIGN _Alignof(max_align_t)

/* Override at compile time + raise arena if your layout has more elements. */
#ifndef NT_UI_DEFAULT_MAX_ELEMENT_COUNT
#define NT_UI_DEFAULT_MAX_ELEMENT_COUNT 1024
#endif
#define NT_UI_DEFAULT_ARENA_SIZE (1U * 1024U * 1024U)

/* Bare uint8_t[N] has 1-byte alignment and trips the create_context assert. */
#define NT_UI_DECLARE_ARENA(name, size) alignas(NT_UI_ARENA_ALIGN) uint8_t name[(size)]

typedef struct nt_ui_context nt_ui_context_t;

/* Caller owns framebuffer binding -- bind your FBO before nt_ui_walk
 * if rendering to texture. Walker only touches viewport and scissor. */
typedef struct {
    float viewport[4]; /* {x, y, w, h} in framebuffer pixels */
} nt_ui_target_t;

/* Pointed to by Clay_ImageElementConfig.imageData. Lifetime must extend
 * through the matching nt_ui_walk. IMAGE elements assert cornerRadius
 * is all-zero -- pre-bake rounded edges into the atlas region. */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    uint8_t flip_bits;
} nt_ui_image_payload_t;

/* clay_cmd is an opaque const Clay_RenderCommand * (cast back if needed).
 * Keeping it opaque lets clay.h stay private to the nt_ui TU. */
typedef void (*nt_ui_custom_handler_t)(const void *clay_cmd, void *userdata);

/* ---- Layer-based painter order ----
 * Walker sorts RECT/BORDER/IMAGE/TEXT commands within a segment (between
 * SCISSOR/CUSTOM barriers) by layer ascending, then by declaration order
 * within a layer. Lower layer renders first.
 *
 * Attach via Clay_ElementDeclaration.userData (or
 * Clay_TextElementConfig.userData for TEXT). Pointer must outlive the
 * matching nt_ui_walk -- use file-scope static const or a compound literal
 * inside the function that calls walk (not in a deeper block scope).
 *
 * NULL userData -> layer 0 (default). Use NT_UI_DATA(N) for an inline
 * compound literal:
 *
 *   CLAY({ .backgroundColor = c, .userData = NT_UI_DATA(LAYER_BG) }) {
 *       CLAY_TEXT(s, CLAY_TEXT_CONFIG({ .userData = NT_UI_DATA(LAYER_FG) }));
 *   }
 *
 * For game-specific layer names, define your own enum:
 *   enum { LAYER_BG = 0, LAYER_HUD = 1, LAYER_OVERLAY = 2 };
 *
 * Note: layer sort applies ONLY to segmentable commands. SCISSOR_START/END
 * and CUSTOM are hard barriers -- their position in the command stream is
 * never reordered. */
typedef uint8_t nt_ui_layer_t;
typedef struct {
    nt_ui_layer_t layer;  /* 0..255; lower draws first */
    uint8_t _reserved[3]; /* padding for future per-element fields */
} nt_ui_element_data_t;
_Static_assert(sizeof(nt_ui_element_data_t) == 4, "nt_ui_element_data_t must be 4 bytes (stable ABI)");

#define NT_UI_DATA(layer_value) ((void *)&(const nt_ui_element_data_t){.layer = (layer_value)})

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

/* ---- Module init ----
 * Wires nt_ui's measure callback into Clay's global function pointer.
 * MUST be called once before any nt_ui_create_context. Idempotent.
 * Pair with nt_ui_module_shutdown for symmetry; not required for correctness. */
void nt_ui_module_init(void);
void nt_ui_module_shutdown(void);

/* Per-context Clay capacity. Use nt_ui_create_desc_defaults() for a sane
 * starting point and adjust fields explicitly when the layout grows beyond
 * default limits. */
typedef struct {
    uint32_t max_elements; /* Max layout elements Clay can hold in this ctx. */
} nt_ui_create_desc_t;

static inline nt_ui_create_desc_t nt_ui_create_desc_defaults(void) {
    return (nt_ui_create_desc_t){
        .max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT,
    };
}

/* Pure query: bytes required to host one nt_ui_context for the given desc.
 * Saves/restores Clay's current-context so concurrent contexts are not
 * mutated. Asserts desc != NULL. */
size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc);

/* Arena is caller-owned; ctx lives in the first ~256 bytes. Asserts
 * arena != NULL, size >= nt_ui_min_arena_size(desc), NT_UI_ARENA_ALIGN-aligned,
 * desc != NULL. */
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc);

/* Tears down ctx state; caller still owns the arena memory. */
void nt_ui_destroy_context(nt_ui_context_t *ctx);

/* Clay's fontId indexes this per-ctx table when the measure callback fires. */
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

/* First call inside switches Clay's current-context to this ctx so
 * subsequent CLAY({...}) macros operate on it. */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse);

/* Freezes Clay's command array into ctx for subsequent walk(s). */
void nt_ui_end(nt_ui_context_t *ctx);

/* Read-only on ctx; safe to call N times against different targets per
 * frame. Render order:
 *  - Different zIndex (Clay floating only): strict ascending z.
 *  - Same z, different layer (nt_ui_element_data_t): ascending layer.
 *  - Same z, same layer: declaration order.
 *  - SCISSOR_START/END and CUSTOM are hard barriers -- never reordered.
 *
 * For tight batching, group elements sharing a material into the same
 * layer (sprites vs text on different layers naturally minimizes material
 * switches). Layer sort runs over segments bounded by barriers above. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* draw_calls is a window delta over the walk -- includes CUSTOM-handler
 * draws. Snapshot sprite/text renderer stats yourself for UI-only counts. */
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

/* Diagnostic: count of segmentable commands in last walk whose userData
 * was NULL (i.e. fell back to layer 0 implicitly). Useful to detect
 * "forgot to set layer on critical element" during development. */
uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx);
#endif
// #endregion

#endif /* NT_UI_H */
