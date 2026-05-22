#ifndef NT_UI_H
#define NT_UI_H

/* Immediate-mode UI bridge over Clay v0.14.
 *
 * Lifecycle: nt_ui_module_init -> nt_ui_create_context -> per-ctx setters
 * (font, atlas/white-region, sprite/text material). Per frame: begin /
 * Clay declarations / end / walk (N targets per frame OK).
 *
 * Only one ctx may be in-frame at a time. Caller owns the Globals UBO;
 * walker writes viewport and scissor only. */

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

#ifndef NT_UI_DEFAULT_MAX_ELEMENT_COUNT
#define NT_UI_DEFAULT_MAX_ELEMENT_COUNT 1024
#endif
#define NT_UI_DEFAULT_ARENA_SIZE (1U * 1024U * 1024U)

/* Bare uint8_t[N] has 1-byte alignment -- create_context would assert. */
#define NT_UI_DECLARE_ARENA(name, size) alignas(NT_UI_ARENA_ALIGN) uint8_t name[(size)]

typedef struct nt_ui_context nt_ui_context_t;

/* Caller owns framebuffer binding; walker writes only viewport + scissor. */
typedef struct {
    float viewport[4]; /* {x, y, w, h} in framebuffer pixels */
} nt_ui_target_t;

/* Pointed to by Clay_ImageElementConfig.imageData; lifetime must extend
 * through the matching nt_ui_walk. cornerRadius must be 0 (pre-bake into atlas). */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    uint8_t flip_bits;
    uint8_t _reserved[3];
} nt_ui_image_payload_t;
_Static_assert(sizeof(nt_ui_image_payload_t) == 12, "nt_ui_image_payload_t stable ABI");

/* clay_cmd is opaque const Clay_RenderCommand * (cast back inside handler). */
typedef void (*nt_ui_custom_handler_t)(const void *clay_cmd, void *userdata);

/* Layer-based painter order. Attach to Clay_ElementDeclaration.userData or
 * Clay_TextElementConfig.userData. Lower layer draws first; within a layer
 * declaration order is preserved. NULL userData -> layer 0.
 *
 * Pointer must outlive nt_ui_walk -- use file-scope static const or a
 * compound literal in the function that calls walk:
 *   CLAY({ .userData = NT_UI_DATA(LAYER_BG), ... })
 *
 * Layer sort applies to RECT/BORDER/IMAGE/TEXT only. SCISSOR/CUSTOM are
 * hard barriers and never reordered. */
typedef uint8_t nt_ui_layer_t;
typedef struct {
    nt_ui_layer_t layer;  /* 0..255; lower draws first */
    uint8_t _reserved[3]; /* future per-element fields */
} nt_ui_element_data_t;
_Static_assert(sizeof(nt_ui_element_data_t) == 4, "nt_ui_element_data_t stable ABI");

#define NT_UI_DATA(layer_value) ((void *)&(const nt_ui_element_data_t){.layer = (layer_value)})

/* All four setters must be called per-context before first walk. */

/* Atlas + white-region for RECT/BORDER. IMAGE uses its own payload atlas. */
void nt_ui_set_atlas_white_region(nt_ui_context_t *ctx, nt_resource_t atlas, uint32_t white_region_idx);

/* Sprite for RECT/BORDER/IMAGE; text for TEXT -- different pipelines. */
void nt_ui_set_sprite_material(nt_ui_context_t *ctx, nt_material_t sprite_material);
void nt_ui_set_text_material(nt_ui_context_t *ctx, nt_material_t text_material);

/* NULL fn silently skips CUSTOM commands. */
void nt_ui_set_custom_handler(nt_ui_context_t *ctx, nt_ui_custom_handler_t fn, void *userdata);

/* Wires Clay's measure callback. MUST be called once before any
 * nt_ui_create_context. Idempotent. */
void nt_ui_module_init(void);
void nt_ui_module_shutdown(void);

typedef struct {
    uint32_t max_elements;      /* Clay layout-element cap. */
    uint32_t max_scissor_depth; /* Walker scissor-stack VLA size. */
} nt_ui_create_desc_t;

static inline nt_ui_create_desc_t nt_ui_create_desc_defaults(void) {
    return (nt_ui_create_desc_t){
        .max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT,
        .max_scissor_depth = NT_UI_WALKER_MAX_SCISSOR_DEPTH,
    };
}

/* Pure query: byte count required for one ctx with this desc. */
size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc);

/* Arena is caller-owned; ctx lives in the first ~256 bytes. */
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc);

/* Tears down ctx state; caller still owns the arena memory. */
void nt_ui_destroy_context(nt_ui_context_t *ctx);

void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

/* Switches Clay's current-context to this ctx for the CLAY({...}) macros. */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse);

/* Freezes Clay's command array into ctx for subsequent walk(s). */
void nt_ui_end(nt_ui_context_t *ctx);

/* Does not mutate frozen_cmds or bindings; per-walk stats reflect the most
 * recent call. Safe to call N times against different targets per frame.
 * Render order:
 *  - Different zIndex (Clay floating only): ascending z.
 *  - Same z, different layer: ascending layer.
 *  - Same z, same layer: declaration order.
 *  - SCISSOR/CUSTOM are hard barriers (never reordered). */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* Window delta over the walk; includes CUSTOM-handler draws. */
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_command_count(const nt_ui_context_t *ctx);

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void);

/* Clay_Context lives in the impl TU only; expose ctx->clay->pointerInfo. */
float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx);
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx);
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx); /* 0 released, 1 pressed */

nt_resource_t nt_ui_test_atlas(const nt_ui_context_t *ctx);
uint32_t nt_ui_test_white_region(const nt_ui_context_t *ctx);
nt_material_t nt_ui_test_sprite_material(const nt_ui_context_t *ctx);
nt_material_t nt_ui_test_text_material(const nt_ui_context_t *ctx);

/* Count of segmentable cmds in last walk with NULL userData (layer 0 fallback). */
uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx);

/* Clay globals are impl-internal; expose for regression tests. */
int32_t nt_ui_test_clay_default_max_element_count(void);
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void);
#endif
// #endregion

#endif /* NT_UI_H */
