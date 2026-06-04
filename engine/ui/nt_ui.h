#ifndef NT_UI_H
#define NT_UI_H

/* Immediate-mode UI bridge over Clay v0.14. Only one ctx may be in-frame at a time. */

/* Must come from the nt_ui target compile-defines; consumers without the link see stubs that mismatch ABI. */
#ifndef NT_UI_DEBUG_TOOLS
#error "NT_UI_DEBUG_TOOLS not defined — link against nt_ui via target_link_libraries(<target> PUBLIC|PRIVATE nt_ui)"
#endif

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

/* Walker scissor stack cap; real UIs nest 1-4 levels, asserts on overflow. */
#define NT_UI_WALKER_SCISSOR_DEPTH_CAP 64

/* Clay places its Clay_Context at the arena head via raw cast. */
#define NT_UI_ARENA_ALIGN _Alignof(max_align_t)

/* ---- Tunables ----
 *
 * `max_elements` (in nt_ui_create_desc_t) is the single per-ctx knob that
 * sizes all layout-bound storage:
 *   - Clay arena (sized via Clay's own formula)
 *   - tree/hit baked-xform + index arrays (production)
 *   - debug_zones, widget_registry, inspector_collapsed_ids (DEBUG_TOOLS only)
 *
 * Memory cost per ctx (production-only / +debug):
 *   max_elements=1024  → ~110 KB / ~270 KB
 *   max_elements=4096  → ~430 KB / ~1.0 MB
 *   max_elements=8192  → ~940 KB / ~2.0 MB
 *
 * Override the default via `target_compile_definitions(my_game PRIVATE
 * NT_UI_DEFAULT_MAX_ELEMENT_COUNT=4096)` or set `desc.max_elements` directly.
 *
 * Other compile-time caps (rare to need overriding):
 *   NT_UI_TREE_DFS_DEPTH_CAP (256) — max UI nesting depth (independent of
 *                                    element count). Set via -D if you nest
 *                                    deeper than ~50 levels in one frame.
 *   NT_UI_DEFAULT_MAX_ELEMENT_COUNT — default for desc.max_elements. */
#ifndef NT_UI_DEFAULT_MAX_ELEMENT_COUNT
#define NT_UI_DEFAULT_MAX_ELEMENT_COUNT 1024
#endif

/* Bare uint8_t[N] is 1-byte aligned; create_context asserts otherwise. */
#define NT_UI_DECLARE_ARENA(name, size) alignas(NT_UI_ARENA_ALIGN) uint8_t name[(size)]

typedef struct nt_ui_context nt_ui_context_t;

/* Walker writes only viewport + scissor; caller owns framebuffer binding.
 *   fb_size[0] == 0: viewport[] is GL physical px (direct mode).
 *   fb_size[0]  > 0: viewport[] is LOGICAL Clay-space; fb_size + fb_offset are PHYSICAL. */
typedef struct {
    float viewport[4];
    float fb_size[2];
    float fb_offset[2];
} nt_ui_target_t;

/* Pointed to by Clay_ImageElementConfig.imageData; must outlive the matching nt_ui_walk.
 * cornerRadius must be 0 (pre-bake into atlas). */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    uint16_t slice9_override[4]; /* {0,0,0,0} + no flag = use atlas default */
    float origin_x;
    float origin_y;
    float slice9_scale; /* multiplies atlas/override slice9 borders; MUST be finite > 0 (walker asserts). */
    uint8_t flip_bits;
    uint8_t flags; /* NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_ORIGIN_OVERRIDE */
    uint8_t _reserved[2];
} nt_ui_image_payload_t;
_Static_assert(sizeof(nt_ui_image_payload_t) == 32, "nt_ui_image_payload_t stable ABI");

/* Typed wrapper for Clay CUSTOM element data. Allocate from nt_mem_scratch (frame arena). */
typedef struct {
    uint8_t type;
    void *data;
} nt_ui_custom_data_t;

#define NT_UI_CUSTOM_TYPE_NONE 0 /* engine anchor: skip, bbox only */
#define NT_UI_CUSTOM_TYPE_GAME 1 /* game handler */

/* Frame snapshot passed to the CUSTOM handler.
 *   clay_cmd  — opaque Clay_RenderCommand*; boundingBox is in LAYOUT (Y-down).
 *   world_aff — 2x3 affine taking LAYOUT point → GL world (parent chain + Y-flip baked in).
 *   opacity   — accumulated [0..1]; multiply into alpha. */
typedef struct {
    const void *clay_cmd;
    float world_aff[6];
    float opacity;
} nt_ui_custom_frame_t;

/* Handler owns any GL state it touches; walker only rebinds sprite material on return. */
typedef void (*nt_ui_custom_handler_t)(const nt_ui_custom_frame_t *frame, void *userdata);

/* Render-time transform — no layout effect.
 * Text caveat: glyph atlas font size is picked from the X-column magnitude
 * (sqrtf(a²+c²)); under sx ≠ sy the text quad still stretches via the matrix,
 * but rasterisation samples at the X-derived em-size — Y axis blurs. Use
 * uniform scale or rotation for crisp text. */
typedef struct {
    float offset_x; /* slide-in/out, additive */
    float offset_y;
    float rotation; /* radians, additive */
    float scale_x;  /* 1.0 = default, multiplicative */
    float scale_y;  /* 1.0 = default, multiplicative */
} nt_ui_transform_t;

/* MUST use this, not zero-init — scale must be positive; use opacity=0 to hide. */
static inline nt_ui_transform_t nt_ui_transform_defaults(void) { return (nt_ui_transform_t){.offset_x = 0, .offset_y = 0, .rotation = 0, .scale_x = 1.0F, .scale_y = 1.0F}; }

/* Cleared bit = "ignore the field" — build pass skips composition; walker reads identity. */
#define NT_UI_ELEM_FLAG_HAS_TRANSFORM (1U << 0)
#define NT_UI_ELEM_FLAG_HAS_OPACITY (1U << 1)

/* Attached via Clay's userData slot. ANY non-NULL userData MUST be nt_ui_element_data_t*
 * (walker casts blindly); game pointers go into the user_data field.
 *
 * Composition is scene-graph standard: world_point = accum_parent · L_child · local_point.
 *
 *   CLAY({ .userData = NT_UI_DATA_LAYER(LAYER_BG), ... })
 *   CLAY({ .userData = NT_UI_DATA_FULL(LAYER_HUD, &my_button), ... })
 *   CLAY({ .userData = NT_UI_DATA_XFORM(LAYER_HUD, &t, 0.8F), ... }) */
typedef uint8_t nt_ui_layer_t;
typedef struct {
    void *user_data;
    nt_ui_layer_t layer; /* 0..255; lower draws first */
    uint8_t flags;       /* NT_UI_ELEM_FLAG_HAS_TRANSFORM | _HAS_OPACITY */
    uint8_t _reserved[2];
    nt_ui_transform_t transform; /* identity when !HAS_TRANSFORM */
    float opacity;               /* 1.0F when !HAS_OPACITY */
} nt_ui_element_data_t;
/* 40B on 64-bit (alignof(void*)=8 → trailing pad), 32B on 32-bit/WASM. */
_Static_assert(sizeof(nt_ui_element_data_t) == (sizeof(void *) == 8 ? 40 : 32), "nt_ui_element_data_t stable ABI");
_Static_assert(sizeof(nt_ui_transform_t) == 20, "nt_ui_transform_t — fail loudly if extended");

#if NT_UI_DEBUG_TOOLS
/* Reserved 240-255 when tools ON; full 0..255 is free for game code when OFF.
 * BG/TEXT split keeps the walker batching one BG->TEXT boundary per segment. */
#define NT_UI_LAYER_DEBUG_HIGHLIGHT ((nt_ui_layer_t)240)
#define NT_UI_LAYER_DEBUG_PANEL_BG ((nt_ui_layer_t)250)
#define NT_UI_LAYER_DEBUG_PANEL_TEXT ((nt_ui_layer_t)251)
_Static_assert(NT_UI_LAYER_DEBUG_HIGHLIGHT >= 240 && NT_UI_LAYER_DEBUG_PANEL_BG >= 240 && NT_UI_LAYER_DEBUG_PANEL_TEXT >= 240, "debug layers must be in engine-reserved range 240-255");
#endif /* NT_UI_DEBUG_TOOLS */

/* Allocates from nt_mem_scratch — game MUST init scratch before any CLAY({...}) declaration. */
const nt_ui_element_data_t *nt_ui_make_element_data(nt_ui_layer_t layer, void *user_data);

/* Scratch-alloc + sets HAS_TRANSFORM | HAS_OPACITY. Transform copied by value. */
const nt_ui_element_data_t *nt_ui_make_element_data_xform(nt_ui_layer_t layer, void *user_data, const nt_ui_transform_t *transform, float opacity);

/* For Clay's .userData (void*), use NT_UI_CLAY_DATA() wrapper. */
#define NT_UI_DATA_LAYER(layer_value) nt_ui_make_element_data((layer_value), NULL)
#define NT_UI_DATA_FULL(layer_value, user_ptr) nt_ui_make_element_data((layer_value), (user_ptr))
#define NT_UI_DATA_XFORM(layer_value, t_ptr, opacity_value) nt_ui_make_element_data_xform((layer_value), NULL, (t_ptr), (opacity_value))
#define NT_UI_DATA_XFORM_FULL(layer_value, user_ptr, t_ptr, opacity_value) nt_ui_make_element_data_xform((layer_value), (user_ptr), (t_ptr), (opacity_value))
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

/* pointers[0..count) is multitouch-ready; v1.8 drives only pointers[0]. */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, float dt, const nt_pointer_t *pointers, uint32_t count);
void nt_ui_end(nt_ui_context_t *ctx);

/* Extensible widget descriptor for the inspector. Descriptors must be pointer-stable
 * for the registering module's lifetime (static const is the canonical pattern).
 *
 *   static const nt_ui_widget_def_t INV_SLOT_DEF = {
 *       .name = "inv_slot", .pill_color = 0xFFB060A0,
 *   };
 *   nt_ui_widget_register(ctx, id, &INV_SLOT_DEF, NULL); */
typedef struct nt_ui_widget_def_t {
    const char *name;    /* shown in inspector pill; e.g. "button" */
    uint32_t pill_color; /* 0xAABBGGRR */
    /* Reserved for future extension (icon region, tooltip, ...). */
    uint32_t _reserved;
} nt_ui_widget_def_t;
_Static_assert(sizeof(nt_ui_widget_def_t) == (sizeof(void *) == 8 ? 16 : 12), "nt_ui_widget_def_t stable ABI (ptr + 2 u32, padded to ptr alignment)");

/* `pad_lrtb` (optional) is the touch-target inflation; pass NULL for none.
 * Storage is direct-mapped, replace-on-collision (observability-only). */
#if NT_UI_DEBUG_TOOLS
void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, const nt_ui_widget_def_t *def, const int16_t pad_lrtb[4]);
#else
static inline void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, const nt_ui_widget_def_t *def, const int16_t pad_lrtb[4]) {
    (void)ctx;
    (void)id;
    (void)def;
    (void)pad_lrtb;
}
#endif

/* Returns NULL when no widget is registered at that id; pointer is caller-owned. */
const nt_ui_widget_def_t *nt_ui_widget_lookup(const nt_ui_context_t *ctx, uint32_t id);

/* Returns false (out untouched) when the id is unregistered or has no recorded padding. */
bool nt_ui_widget_get_hit_padding(const nt_ui_context_t *ctx, uint32_t id, int16_t out_lrtb[4]);

/* Order: zIndex asc, then layer asc, then declaration. SCISSOR/CUSTOM are hard barriers. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* Window delta over the walk; includes CUSTOM-handler draws. */
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx);
/* Total Clay commands incl. SCISSOR/CUSTOM/NONE. */
uint32_t nt_ui_get_last_walk_command_count(const nt_ui_context_t *ctx);
/* CPU timings (ms); each reset to 0 on early-out walks. */
float nt_ui_get_last_layout_ms(const nt_ui_context_t *ctx);
float nt_ui_get_last_build_tree_ms(const nt_ui_context_t *ctx);
/* Excludes draining the caller's pending geometry (same scope as draw_calls). */
float nt_ui_get_last_walk_ms(const nt_ui_context_t *ctx);
/* Monotonic; nonzero delta across frames means raise NT_UI_ANIM_SLOTS. */
uint32_t nt_ui_get_anim_collision_count(const nt_ui_context_t *ctx);
/* Per-type command counts (pre-emit; use draw_calls for GPU cost). */
uint32_t nt_ui_get_last_walk_rect_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_image_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_text_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_border_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_scissor_command_count(const nt_ui_context_t *ctx);
uint32_t nt_ui_get_last_walk_max_scissor_depth(const nt_ui_context_t *ctx);

// #region transform_opacity_api
/* Transforms/opacity attach via Clay's userData (NT_UI_DATA_XFORM / _FULL);
 * build_tree composes them into tree_baked. Clipping uses Clay's `.clip = {...}`. */

/* data is passed through to the handler registered via nt_ui_set_custom_handler. */
void nt_ui_custom(nt_ui_context_t *ctx, const nt_ui_element_data_t *elem_data, void *data);

// #region interaction_api
/* Engine-owned, transform-aware: inverse-affine via prev-frame tree_baked,
 * then point-in-bbox against Clay's persistent layout bbox. */

/* Wraps Clay_GetElementId; result is never 0. Asserts s != NULL. */
uint32_t nt_ui_id(const char *s);

/* `found == false` if id was not declared the immediately preceding frame. */
typedef struct {
    float x, y, width, height;
    bool found;
} nt_ui_bbox_t;
nt_ui_bbox_t nt_ui_get_bbox(const nt_ui_context_t *ctx, uint32_t id);

/* Multitouch contract is RESERVED, not implemented. v1.8 drives only frame_pointers[0]:
 * step/query/hit-test all hard-code pidx=0; captures[1..] storage exists but is never
 * read or written by the engine, and active_id is not yet keyed by pointer_id (so the
 * same finger lifting and pressing again on a different OS pointer slot would not be
 * recognized as the same gesture). The 4-element capture array shape is preserved so
 * future multitouch can land without ABI break. Games doing per-pointer logic must
 * call nt_ui only for pointer 0 today. */
typedef struct {
    uint32_t active_id; /* widget this pointer captured; 0 = none */
    float press_pos[2]; /* UI-space press origin */
    float pos[2];       /* current UI-space pos; drag = pos - press_pos */
} nt_ui_capture_t;

/* Computed lazily: this-frame pointer vs PREVIOUS-frame bbox (1-frame IM lag). */
typedef struct {
    bool hovered;           /* pointer over bbox (transform-aware) */
    bool pressed;           /* currently captured (held) */
    bool pressed_now;       /* press began this frame */
    bool released_now;      /* released this frame (even off-widget = cancel) */
    bool clicked;           /* released OVER the widget -> one-shot */
    float press_pos[2];     /* where press began (UI-space) */
    float pos[2];           /* current pointer pos (UI-space) */
    float drag_dx, drag_dy; /* = pos - press_pos (convenience) */
    uint32_t pointer_id;    /* which pointer captured (multitouch) */
} nt_ui_interaction_t;

/* PURE query (no mutation). Safe to call N times per frame; safe outside begin/end. */
nt_ui_interaction_t nt_ui_query_interaction(nt_ui_context_t *ctx, uint32_t id);

/* Inflates bbox BEFORE the inverse-affine for mobile touch-friendly hit areas.
 * Asserts each component >= 0; NULL = unpadded. */
nt_ui_interaction_t nt_ui_query_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* MUTATING — call ONCE per widget per frame from the widget's begin.
 * Sidebar-consumed pointer returns zeroed struct (see nt_ui_inspector_pointer_consumed). */
nt_ui_interaction_t nt_ui_step_interaction(nt_ui_context_t *ctx, uint32_t id);
nt_ui_interaction_t nt_ui_step_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* True when any capture is active OR a pointer is over a widget this frame. */
bool nt_ui_wants_pointer(const nt_ui_context_t *ctx);
// #endregion

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void);

float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx);
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx);
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx); /* 0 released, 1 pressed */

/* Capture state read from test TU. active_id 0 = no capture. */
uint32_t nt_ui_test_capture_active_id(const nt_ui_context_t *ctx, uint32_t pointer_index);

/* Transform-aware hit-test: inverse-affine vs prev-frame layout bbox. */
bool nt_ui_test_hit(nt_ui_context_t *ctx, uint32_t id, float px, float py);

/* Padded variant — inflation is in layout space (rotates with the widget). */
bool nt_ui_test_hit_padded(nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]);

/* Count of segmentable cmds with NULL userData (= implicit layer-0 fallback). */
uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx);

int32_t nt_ui_test_clay_default_max_element_count(void);
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void);
#endif
// #endregion

/* Widget headers NOT re-exported — keeps clay.h out of the public umbrella. */

#endif /* NT_UI_H */
