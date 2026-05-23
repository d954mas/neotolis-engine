#ifndef NT_UI_H
#define NT_UI_H

/* Immediate-mode UI bridge over Clay v0.14. Only one ctx may be in-frame at
 * a time; caller owns the Globals UBO -- walker writes viewport + scissor. */

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

/* Walker's scissor stack capacity. Asserts on overflow. Real UIs nest 1-4
 * levels; cap is 16x that headroom. */
#define NT_UI_WALKER_SCISSOR_DEPTH_CAP 64

/* Clay places its Clay_Context at the arena head via raw cast. */
#define NT_UI_ARENA_ALIGN _Alignof(max_align_t)

#ifndef NT_UI_DEFAULT_MAX_ELEMENT_COUNT
#define NT_UI_DEFAULT_MAX_ELEMENT_COUNT 1024
#endif

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

/* clay_cmd is opaque const Clay_RenderCommand * (cast back inside handler).
 * Handler owns the GL state it touches: if you change viewport or scissor,
 * restore them before returning. Walker only rebinds the sprite material
 * (its own flush invariant) -- everything else is the handler's mess. */
typedef void (*nt_ui_custom_handler_t)(const void *clay_cmd, void *userdata);

/* Attached via Clay's userData slot. Engine owns the wire format -- ANY
 * non-NULL userData MUST be nt_ui_element_data_t* (walker casts blindly).
 * Game pointers go into the user_data field, not directly into Clay's slot.
 *
 * Layer sort applies to RECT/BORDER/IMAGE/TEXT; SCISSOR/CUSTOM are barriers.
 *
 *   CLAY({ .userData = NT_UI_DATA_LAYER(LAYER_BG), ... })
 *   CLAY({ .userData = NT_UI_DATA_FULL(LAYER_HUD, &my_button), ... }) */
typedef uint8_t nt_ui_layer_t;
typedef struct {
    void *user_data;
    nt_ui_layer_t layer; /* 0..255; lower draws first */
    uint8_t _reserved[3];
} nt_ui_element_data_t;

/* Macros allocate from nt_mem_scratch (frame arena) so the pointer stays valid
 * across helper-function returns until the next nt_mem_scratch_reset. Game
 * MUST init scratch before any CLAY({...}) declaration. */
void *nt_ui_make_element_data(nt_ui_layer_t layer, void *user_data);

#define NT_UI_DATA_LAYER(layer_value) nt_ui_make_element_data((layer_value), NULL)
#define NT_UI_DATA_FULL(layer_value, user_ptr) nt_ui_make_element_data((layer_value), (user_ptr))

/* All four setters required per-context before first walk. */
void nt_ui_set_atlas_white_region(nt_ui_context_t *ctx, nt_resource_t atlas, uint32_t white_region_idx);
void nt_ui_set_sprite_material(nt_ui_context_t *ctx, nt_material_t sprite_material);
void nt_ui_set_text_material(nt_ui_context_t *ctx, nt_material_t text_material);
/* NULL fn silently skips CUSTOM commands. */
void nt_ui_set_custom_handler(nt_ui_context_t *ctx, nt_ui_custom_handler_t fn, void *userdata);

void nt_ui_module_init(void);
void nt_ui_module_shutdown(void);

typedef struct {
    uint32_t max_elements; /* Clay layout-element cap. */
} nt_ui_create_desc_t;

static inline nt_ui_create_desc_t nt_ui_create_desc_defaults(void) {
    return (nt_ui_create_desc_t){
        .max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT,
    };
}

size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc);
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc);
void nt_ui_destroy_context(nt_ui_context_t *ctx);

void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse);
void nt_ui_end(nt_ui_context_t *ctx);

/* Read-only on frozen_cmds + bindings; per-walk stats reflect the latest call.
 * Order: zIndex asc, then layer asc, then declaration. SCISSOR/CUSTOM are
 * hard barriers and never reordered. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* Window delta over the walk; includes CUSTOM-handler draws. */
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx);
/* Total Clay commands incl. SCISSOR/CUSTOM/NONE (non-drawing barriers). */
uint32_t nt_ui_get_last_walk_command_count(const nt_ui_context_t *ctx);

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void);

float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx);
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx);
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx); /* 0 released, 1 pressed */

/* Count of segmentable cmds with NULL userData (= implicit layer-0 fallback). */
uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx);

int32_t nt_ui_test_clay_default_max_element_count(void);
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void);
#endif
// #endregion

/* Widget umbrella re-exports. Phase 54+ extends with panel, button, etc. */
#include "ui/nt_ui_label.h"

#endif /* NT_UI_H */
