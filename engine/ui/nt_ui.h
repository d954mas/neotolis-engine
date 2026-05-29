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

/* Caller owns framebuffer binding; walker writes only viewport + scissor.
 *   fb_size[0] == 0: viewport[] = GL physical px {x, y, w, h}. Direct mode.
 *   fb_size[0] > 0:  viewport[] = LOGICAL Clay-space; fb_size = PHYSICAL fb;
 *                     fb_offset = PHYSICAL letterbox margin (negative in CROP).
 * Build via nt_ui_scale_make_target() or zero-init for direct mode. */
typedef struct {
    float viewport[4];
    float fb_size[2];
    float fb_offset[2];
} nt_ui_target_t;

/* Pointed to by Clay_ImageElementConfig.imageData; lifetime must extend
 * through the matching nt_ui_walk. cornerRadius must be 0 (pre-bake into atlas). */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    uint16_t slice9_override[4]; /* {0,0,0,0} + no flag = use atlas default */
    float origin_x;
    float origin_y;
    uint8_t flip_bits;
    uint8_t flags; /* copied from style (NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_ORIGIN_OVERRIDE) */
    uint8_t _reserved[2];
} nt_ui_image_payload_t;
_Static_assert(sizeof(nt_ui_image_payload_t) == 28, "nt_ui_image_payload_t stable ABI");

/* Typed wrapper for Clay CUSTOM element data. Engine and game share the
 * same customData slot; type tag distinguishes engine anchors from game
 * handlers. Allocate from nt_mem_scratch (frame arena). */
typedef struct {
    uint8_t type;
    void *data;
} nt_ui_custom_data_t;

#define NT_UI_CUSTOM_TYPE_NONE 0 /* engine anchor: skip, bbox only */
#define NT_UI_CUSTOM_TYPE_GAME 1 /* game handler */

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
 * MUST init scratch before any CLAY({...}) declaration.
 * Typed return; auto-converts to void* for Clay's .userData slot. */
const nt_ui_element_data_t *nt_ui_make_element_data(nt_ui_layer_t layer, void *user_data);

/* Returns const — element_data is immutable after creation.
 * For Clay's .userData (void*), use NT_UI_CLAY_DATA() wrapper. */
#define NT_UI_DATA_LAYER(layer_value) nt_ui_make_element_data((layer_value), NULL)
#define NT_UI_DATA_FULL(layer_value, user_ptr) nt_ui_make_element_data((layer_value), (user_ptr))
#define NT_UI_CLAY_DATA(layer_value) ((void *)nt_ui_make_element_data((layer_value), NULL))

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
    uint32_t max_markers;  /* side-channel transform/opacity markers; 0 = max_elements*2 */
} nt_ui_create_desc_t;

static inline nt_ui_create_desc_t nt_ui_create_desc_defaults(void) {
    return (nt_ui_create_desc_t){
        .max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT,
        .max_markers = 0,
    };
}

size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc);
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc);
void nt_ui_destroy_context(nt_ui_context_t *ctx);

void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

/* dt drives Clay scroll-container momentum; pass g_nt_app.dt or test value.
 * pointers[0..count) is the full per-frame pointer list (multitouch-ready,
 * D-56-19); v1.8 drives the primary pointer (pointers[0]); Clay is still fed
 * only the primary pointer. */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, float dt, const nt_pointer_t *pointers, uint32_t count);
void nt_ui_end(nt_ui_context_t *ctx);

/* Toggle Clay's debug overlay (element tree + bbox). Applied at next begin.
 * Getter reflects Clay state -- updated each end (close-button "x" turns off). */
void nt_ui_set_debug_overlay(nt_ui_context_t *ctx, bool enabled);
bool nt_ui_get_debug_overlay(const nt_ui_context_t *ctx);

/* Read-only on frozen_cmds + bindings; per-walk stats reflect the latest call.
 * Order: zIndex asc, then layer asc, then declaration. SCISSOR/CUSTOM are
 * hard barriers and never reordered. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* Window delta over the walk; includes CUSTOM-handler draws. */
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx);
/* Total Clay commands incl. SCISSOR/CUSTOM/NONE (non-drawing barriers). */
uint32_t nt_ui_get_last_walk_command_count(const nt_ui_context_t *ctx);
/* Phase 55 CPU timing (ms); both reset to 0 on early-out walks. */
/* layout_ms = the Clay_EndLayout solve only, not the whole begin->end span. */
float nt_ui_get_last_layout_ms(const nt_ui_context_t *ctx);
/* walk_ms = walk dispatch, timed from AFTER the entry flush -- excludes
 * draining the caller's pending geometry (same scope as draw_calls). */
float nt_ui_get_last_walk_ms(const nt_ui_context_t *ctx);
/* Phase 55: per-type render-command counts, counted at dispatch (pre-emit, not
 * pixels drawn -- use draw_calls for GPU cost). Reset each walk. */
uint32_t nt_ui_get_last_walk_rect_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_image_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_text_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_border_command_count(const nt_ui_context_t *ctx);
/* Phase 55: scissor command count + marker push counts. Reset each walk. */
uint32_t nt_ui_get_last_walk_scissor_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_max_scissor_depth(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_transform_pushes(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_opacity_pushes(const nt_ui_context_t *ctx);

// #region transform_opacity_api
/* Render-time transform -- no layout effect (D-54-09). */
typedef struct {
    float offset_x; /* slide-in/out, additive */
    float offset_y;
    float rotation; /* radians, additive */
    float scale_x;  /* 1.0 = default, multiplicative */
    float scale_y;  /* 1.0 = default, multiplicative */
} nt_ui_transform_t;

/* Identity transform (MUST use this, not zero-init -- scale must be positive; use opacity=0 to hide). */
static inline nt_ui_transform_t nt_ui_transform_defaults(void) { return (nt_ui_transform_t){.offset_x = 0, .offset_y = 0, .rotation = 0, .scale_x = 1.0F, .scale_y = 1.0F}; }

#ifndef NT_UI_TRANSFORM_STACK_DEPTH_CAP
#define NT_UI_TRANSFORM_STACK_DEPTH_CAP 16
#endif
#ifndef NT_UI_OPACITY_STACK_DEPTH_CAP
#define NT_UI_OPACITY_STACK_DEPTH_CAP 16
#endif

/* Push/pop during declaration phase (between begin/end). Stack depth <= NT_UI_TRANSFORM_STACK_DEPTH_CAP.
 * Offset: applies to all element types (position shift).
 * Scale: applies to all element types (position + size).
 * Rotation: applies to all element types. SCISSOR uses AABB approximation
 *   of the rotated clip rect (conservative: slightly larger than exact).
 * Opacity (via push_opacity): applies to all element types. */
void nt_ui_push_transform(nt_ui_context_t *ctx, const nt_ui_transform_t *transform);
void nt_ui_pop_transform(nt_ui_context_t *ctx);

/* Opacity inheritance: multiplied into alpha of all children. 1.0 = opaque. */
void nt_ui_push_opacity(nt_ui_context_t *ctx, float opacity);
void nt_ui_pop_opacity(nt_ui_context_t *ctx);
// #endregion

/* Emit a game CUSTOM element. data is passed through to the custom handler
 * registered via nt_ui_set_custom_handler. Allocates nt_ui_custom_data_t
 * wrapper from scratch arena. */
void nt_ui_custom(nt_ui_context_t *ctx, const nt_ui_element_data_t *elem_data, void *data);

// #region interaction_api
/* Phase 56 engine-owned interaction service (D-56-21). The hit-test is
 * transform-aware: the pointer is inverse-transformed by the declaration-time
 * accumulated transform stack, then tested against Clay's stable prev-frame
 * layout bbox. Clay is layout-only; this is NOT Clay_Hovered (D-56-03). */

/* Precompute once per id (game caches): wraps Clay_GetElementId, returns the
 * uint32 hash (never 0 -- Clay returns hash+1). Asserts s != NULL. */
uint32_t nt_ui_id(const char *s);
/* Per-frame string convenience (hashes each call; also names the Clay debug
 * overlay). Identical result to nt_ui_id; kept distinct for intent. */
uint32_t nt_ui_id_str(const char *s);

/* Prev-frame LAYOUT bbox (thin Clay_GetElementData wrapper; raw layout space,
 * Y-down). found == false on the first frame an id is seen (D-56-09). */
typedef struct {
    float x, y, width, height;
    bool found;
} nt_ui_bbox_t;
nt_ui_bbox_t nt_ui_get_bbox(const nt_ui_context_t *ctx, uint32_t id);
// #endregion

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void);

float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx);
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx);
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx); /* 0 released, 1 pressed */

/* Phase 56: read engine-owned capture state from the test TU (captures[]
 * is private; Plan 03 fills the bodies). active_id 0 = no capture. */
uint32_t nt_ui_test_capture_active_id(const nt_ui_context_t *ctx, uint32_t pointer_index);

/* Phase 56: drive the transform-aware hit-test directly from the test TU
 * (inverse-affine vs prev-frame layout bbox). Returns true iff (px,py) in
 * Clay Y-down space lands inside the widget after inverse-transform. */
bool nt_ui_test_hit(nt_ui_context_t *ctx, uint32_t id, float px, float py);

/* Count of segmentable cmds with NULL userData (= implicit layer-0 fallback). */
uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx);

int32_t nt_ui_test_clay_default_max_element_count(void);
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void);
#endif
// #endregion

/* Widget headers NOT re-exported: keep clay.h out of public umbrella to
 * avoid breaking Windows ucrt stdlib.h __declspec(noreturn) in release. */

#endif /* NT_UI_H */
