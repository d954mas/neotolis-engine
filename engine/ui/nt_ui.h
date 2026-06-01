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

/* Phase 56 ext: well-known debug layers for nt_ui_inspector emit. Games
 * typically use 0..~10 for normal UI (BG/IMG/TEXT/HUD); the inspector floats
 * its sidebar + highlight overlay above ALL game UI by tagging its Clay
 * elements with NT_UI_DATA_LAYER(NT_UI_LAYER_DEBUG_*).
 *
 * Three distinct constants:
 *   - HIGHLIGHT  = 240 -- element highlight float + post-walk hit-zone
 *                         overlay polygons. Above game UI (10), below the
 *                         panel (250/251) so highlights for widgets near
 *                         the right edge of the screen stay strictly under
 *                         the sidebar where they geometrically overlap.
 *   - PANEL_BG   = 250 -- every inspector-owned CLAY rect/border/image
 *                         (row backgrounds, dot, swatches, separators, etc.).
 *   - PANEL_TEXT = 251 -- every inspector-owned CLAY_TEXT config (row labels,
 *                         id text, pill labels, hex strings, info-pane text).
 *
 * Why split BG and TEXT into two layers (Phase 56 ext perf):
 *   The walker dispatches commands in (zIndex asc, layer asc, declaration)
 *   order. Within a single layer rect/text alternation forces sprite-then-
 *   text-then-sprite pipeline flushes -- each pair costs ~2 draw calls.
 *   By placing ALL inspector rects on PANEL_BG (250) and ALL inspector
 *   texts on PANEL_TEXT (251), the walker processes ALL inspector RECT
 *   commands first (one sprite batch) then ALL inspector TEXT commands
 *   (one text batch) per zIndex/scissor segment -- collapsing the inner
 *   alternation count from O(rows) to ~2 (BG -> TEXT boundary once).
 *   The sort key (zIndex, layer, declaration) and the walker's ascending
 *   layer iteration (active_layers bitmask drained LSB-first via
 *   __builtin_ctz) preserve the BG-before-TEXT painter's order: row
 *   backgrounds still paint before their labels. Layer 251 is reserved
 *   so future engine overlays should pick 252..255.
 *
 * Layer is uint8_t (0..255). NT_UI_LAYER_DEBUG / NT_UI_LAYER_DEBUG_PANEL are
 * kept as aliases for NT_UI_LAYER_DEBUG_PANEL_BG so existing callers
 * (engine tests, the legacy name "PANEL", any out-of-tree code) keep
 * resolving to a valid BG slot; new code should pick the explicit
 * BG/TEXT variant. */
#define NT_UI_LAYER_DEBUG_HIGHLIGHT ((nt_ui_layer_t)240)
#define NT_UI_LAYER_DEBUG_PANEL_BG ((nt_ui_layer_t)250)
#define NT_UI_LAYER_DEBUG_PANEL_TEXT ((nt_ui_layer_t)251)
/* Legacy aliases -- PANEL == PANEL_BG so any existing call site that wants
 * "the inspector panel layer" still resolves to a valid (rect-bearing) slot.
 * New code should use the BG/TEXT split explicitly. */
#define NT_UI_LAYER_DEBUG_PANEL NT_UI_LAYER_DEBUG_PANEL_BG
#define NT_UI_LAYER_DEBUG NT_UI_LAYER_DEBUG_PANEL_BG

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

/* NOTE: nt_ui_set_debug_overlay / nt_ui_get_debug_overlay were REMOVED in the
 * Phase 56 ext inspector rework. Clay's built-in debug view is no longer wired
 * by the engine. The replacement is nt_ui_inspector_set_active /
 * nt_ui_inspector_is_active (engine/ui/nt_ui_inspector.h) -- ONE debug system,
 * verbatim port of Clay__RenderDebugView injected into the layout pass via
 * nt_ui_end. */

/* Phase 56 ext (CHUNK E, refactor): extensible widget descriptor for
 * nt_ui_inspector. Every engine widget (button/image/label/panel/group) AND
 * any GAME widget (inventory_slot, dialogue_choice, ...) records its element
 * id + descriptor pointer in a per-frame direct-mapped registry inside ctx
 * so the inspector can show the widget's name + pill color next to each
 * entry in the element tree. Plain Clay elements (no widget call) fall
 * through to NULL (rendered as plain Clay). Registry resets each
 * nt_ui_begin. id 0 is silently dropped (sentinel).
 *
 * Descriptors are static const, pointer-stable for the lifetime of the
 * registering module. Engine descriptors are exported by each widget
 * header (NT_UI_BUTTON_DEF / NT_UI_IMAGE_DEF / NT_UI_LABEL_DEF /
 * NT_UI_PANEL_DEF / NT_UI_GROUP_DEF). Games declare their own:
 *
 *   static const nt_ui_widget_def_t INV_SLOT_DEF = {
 *       .name = "inv_slot", .pill_color = 0xFFB060A0,
 *   };
 *   nt_ui_widget_register(ctx, id, &INV_SLOT_DEF, NULL);
 */
typedef struct nt_ui_widget_def_t {
    const char *name;    /* shown in inspector pill; e.g. "button" */
    uint32_t pill_color; /* 0xAABBGGRR */
    /* Reserved for future extension (icon region, tooltip, ...). */
    uint32_t _reserved;
} nt_ui_widget_def_t;

/* Register the widget at `id` with the descriptor `def`. Optional
 * `pad_lrtb` records the touch-target inflation so the inspector overlay can
 * outline the padded hit zone distinctly; pass NULL for none. def must outlive
 * the frame (static const is the canonical pattern). id 0 is silently dropped
 * (sentinel). def NULL is silently dropped. */
void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, const nt_ui_widget_def_t *def, const int16_t pad_lrtb[4]);

/* Return the descriptor registered for `id` this frame, or NULL when no
 * widget is registered at that id. The returned pointer is the same one
 * passed to nt_ui_widget_register (caller-owned lifetime). */
const nt_ui_widget_def_t *nt_ui_widget_lookup(const nt_ui_context_t *ctx, uint32_t id);

/* Read back the registered hit-zone padding for id. Returns true and writes
 * {l,r,t,b} into out_lrtb when the id has a recorded padding; returns false
 * (out untouched) when the id is not registered OR was registered with a
 * NULL hit_padding_lrtb. */
bool nt_ui_widget_get_hit_padding(const nt_ui_context_t *ctx, uint32_t id, int16_t out_lrtb[4]);

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

/* Per-pointer capture state (D-56-04). v1.8 iterates the primary pointer
 * (index 0); the array in the ctx is multitouch-ready for v1.9. */
typedef struct {
    uint32_t active_id; /* widget this pointer captured; 0 = none */
    float press_pos[2]; /* UI-space press origin */
    float pos[2];       /* current UI-space pos; drag = pos - press_pos */
} nt_ui_capture_t;

/* Full per-widget interaction state (D-56-06). Returned by value, computed
 * lazily: this-frame primary pointer vs PREVIOUS-frame bbox (1-frame IM lag). */
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

/* THE foundation (D-56-21). The button (Plan 04) and every custom widget query
 * this. id from nt_ui_id("..."). Drives the per-pointer capture state machine
 * off the precomputed nt_button_state_t edges + the transform-aware hit-test. */
nt_ui_interaction_t nt_ui_get_interaction(nt_ui_context_t *ctx, uint32_t id);

/* Padded variant (Phase 56 ext, touch-target inflation). Inflates the
 * widget's layout-space bbox by pad_lrtb = {left, right, top, bottom} in
 * LAYOUT pixels BEFORE the inverse-affine transform check. Use for mobile
 * touch-friendly hit areas without changing visual size. Asserts each
 * component >= 0 (negative padding is a use error -- use a smaller widget).
 * pad_lrtb may be NULL (treated as {0,0,0,0}; equivalent to the unpadded
 * call). nt_ui_get_interaction(ctx, id) is implemented as the {0,0,0,0}
 * specialization of this. */
nt_ui_interaction_t nt_ui_get_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* True when any capture is active OR a pointer is over a widget this frame
 * (~ ImGui io.WantCaptureMouse). The game gates its own world input on this. */
bool nt_ui_wants_pointer(const nt_ui_context_t *ctx);
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

/* Phase 56 ext: drive the padded hit-test (touch-target inflation) directly
 * from the test TU. pad_lrtb is {left, right, top, bottom} layout pixels;
 * NULL = unpadded. Inflated bbox is checked AFTER the inverse-affine, so the
 * padding lives in layout space (rotates with the widget). */
bool nt_ui_test_hit_padded(nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]);

/* Count of segmentable cmds with NULL userData (= implicit layer-0 fallback). */
uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx);

int32_t nt_ui_test_clay_default_max_element_count(void);
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void);
#endif
// #endregion

/* Widget headers NOT re-exported: keep clay.h out of public umbrella to
 * avoid breaking Windows ucrt stdlib.h __declspec(noreturn) in release. */

#endif /* NT_UI_H */
