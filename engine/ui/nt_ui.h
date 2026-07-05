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
 *   - debug_zones, widget_registry, inspector_collapsed_ids, probe_scratch (DEBUG_TOOLS only)
 *
 * Memory cost per ctx (production / production+debug; post-3D-refactor, mat4 baked storage):
 *   max_elements=1024  → ~210 KB / ~650 KB
 *   max_elements=4096  → ~820 KB / ~2.6 MB
 *   max_elements=8192  → ~1.7 MB / ~5.1 MB
 * The DEBUG_TOOLS delta is now dominated by probe_scratch (~290 B/element — owned-string POD nodes sized
 * to max_elements so the devapi reads the FULL tree). tree_baked + hit_baked grow at 160 B/element.
 *
 * Override the default via game's compile defs:
 *   target_compile_definitions(my_game PRIVATE NT_UI_DEFAULT_MAX_ELEMENT_COUNT=4096)
 * Game's `-D` propagates because this header's #ifndef picks it up when the
 * game includes nt_ui.h. Alternatively set desc.max_elements at runtime.
 *
 * 3D ctx (use_raycast_input=true) hot-path costs (native-release bench, post-refactor):
 *   - hit-test raycast: 6.8 ns/op (vs 2D inverse-affine 3.4 ns/op; 2× slower)
 *   - clip-chain depth=4: 6.1 ns/op (vs 2D 4.3 ns/op; ~40% slower)
 *   - compose mat4×mat4: 15.2 ns/op (vs 2x3 affine 6.5 ns/op; 2.3× slower)
 * All well under any frame budget at typical UI scale (≤1024 elements, ≤10 hit-tested widgets).
 *
 * NT_UI_TREE_DFS_DEPTH_CAP (256, in nt_ui_internal.h) caps UI nesting depth
 * — independent of max_elements. Overriding requires patching nt_ui's own
 * compile defs (target_compile_definitions on the nt_ui target itself);
 * game's `-D` won't reach internal.h. */
#ifndef NT_UI_DEFAULT_MAX_ELEMENT_COUNT
#define NT_UI_DEFAULT_MAX_ELEMENT_COUNT 1024
#endif

/* Default per-depth modal z-band stride; each nested modal sits panel_z = stride*(depth+1).
 * Per-context override via nt_ui_create_desc_t.modal_zband_stride (seeded from this in
 * nt_ui_create_desc_defaults; must be > 0). */
#define NT_UI_MODAL_ZBAND_STRIDE 1000

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

/* Per-widget custom per-vertex block — scratch-allocated, referenced by pointer only
 * for custom-attr widgets so a plain image keeps the payload small. Untyped: the bound
 * material's attr_map names the floats (walker injects a_layout/a_uvrect by name). Sized
 * for four FLOAT4 attrs (NT_SPRITE_CUSTOM_STRIDE_MAX = 64).
 * injection vocabulary: docs/neotolis_engine_spec_1.md
 * "Radial widgets & the custom-attr image path" */
typedef struct {
    float custom_attrs[16];
    uint8_t custom_bytes; /* > 0; the material declares this many per-vertex custom bytes. */
    uint8_t geom_mode;    /* NT_UI_IMAGE_GEOM_* — bbox rasterization strategy (not widget identity) */
} nt_ui_image_custom_block_t;

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
    /* Optional per-element material override. .id==0 = use the walker's bound base
     * material; re-bound only when .id differs, so same-material elements batch. */
    nt_material_t material;
    /* NULL = plain image (common case). Non-NULL = custom-attr widget; the block
     * (payload-lifetime scratch) carries the per-vertex attrs. */
    const nt_ui_image_custom_block_t *custom;
} nt_ui_image_payload_t;
/* Non-pointer prefix is 36 B; `custom` is pointer-aligned and adds sizeof(void*). */
_Static_assert(sizeof(nt_ui_image_payload_t) == ((36U + (sizeof(void *) - 1U)) & ~(sizeof(void *) - 1U)) + sizeof(void *), "nt_ui_image_payload_t stable ABI (40 B wasm / 48 B native; was 100 B)");

/* geom_mode: bbox rasterization when custom_bytes > 0. REGION = textured emit (real
 * atlas art). GEOMETRY = clean white-region bbox quad for SDF shaders that derive a
 * [-1,1] coord from gl_VertexID&3. See spec "Radial widgets & the custom-attr image path". */
#define NT_UI_IMAGE_GEOM_REGION 0U
#define NT_UI_IMAGE_GEOM_GEOMETRY 1U

/* Typed wrapper for Clay CUSTOM element data. Allocate from nt_mem_scratch (frame arena). */
typedef struct {
    uint8_t type;
    void *data;
} nt_ui_custom_data_t;

#define NT_UI_CUSTOM_TYPE_NONE 0      /* engine anchor: skip, bbox only */
#define NT_UI_CUSTOM_TYPE_GAME 1      /* game handler */
#define NT_UI_CUSTOM_TYPE_RICH_TEXT 2 /* rich-text widget self-emit (solved span draw_n) */

/* Frame snapshot passed to the CUSTOM handler.
 *   ctx        — the UI context (read-only): lets a handler read state/material defaults + viewport
 *                without closing over it. Opaque to game handlers (the full struct is engine-internal).
 *   clay_cmd   — opaque Clay_RenderCommand*; boundingBox is in LAYOUT (Y-down).
 *   world_mat4 — column-major mat4 taking LAYOUT point → world (parent chain).
 *                Default 2D ctx bakes the screen Y-flip (LAYOUT Y-down → GL Y-up) into world_mat4;
 *                3D ctx (use_raycast_input) leaves it unflipped — the handler composes view_proj as needed.
 *   opacity    — accumulated [0..1]; multiply into alpha. */
typedef struct {
    const nt_ui_context_t *ctx;
    const void *clay_cmd;
    float world_mat4[16];
    float opacity;
} nt_ui_custom_frame_t;

/* Handler owns any GL state it touches; walker only rebinds sprite material on return. */
typedef void (*nt_ui_custom_handler_t)(const nt_ui_custom_frame_t *frame, void *userdata);

/* Render-time transform — no layout effect.
 * 3D-capable: offset_z + rotation_x/y for card-flip and world-space UI; 2D paths
 * ignore z (rotation_z is the legacy 2D Z-rotation).
 * Text caveat: glyph atlas font size is picked from the X-column magnitude;
 * under sx ≠ sy the text quad still stretches, but rasterisation samples at
 * the X-derived em-size — Y axis blurs. Use uniform scale or Z-only rotation for crisp text. */
typedef struct {
    float offset_x; /* slide-in/out, additive */
    float offset_y;
    float offset_z;
    float rotation_x; /* radians; Euler XYZ order */
    float rotation_y;
    float rotation_z;
    float scale_x; /* 1.0 = default, multiplicative */
    float scale_y;
    float scale_z;
} nt_ui_transform_t;

/* MUST use this, not zero-init — scale must be positive; use opacity=0 to hide. */
static inline nt_ui_transform_t nt_ui_transform_defaults(void) {
    return (nt_ui_transform_t){.offset_x = 0, .offset_y = 0, .offset_z = 0, .rotation_x = 0, .rotation_y = 0, .rotation_z = 0, .scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F};
}

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

/* Optional per-element payload tagged by special_kind. The union holds a POINTER into frame scratch, so
 * element_data grows by exactly one pointer no matter how many kinds are added, and this header needs only
 * forward decls. NONE (0) = a plain element with no special data (shares the layer singleton). */
typedef struct nt_ui_label_deco nt_ui_label_deco_t; /* full definition in nt_ui_internal.h */
typedef enum {
    NT_UI_SPECIAL_NONE = 0,
    NT_UI_SPECIAL_TEXT_DECO, /* special.text_deco valid */
} nt_ui_special_kind_t;

typedef struct {
    void *user_data;
    nt_ui_layer_t layer;  /* 0..255; lower draws first */
    uint8_t flags;        /* NT_UI_ELEM_FLAG_HAS_TRANSFORM | _HAS_OPACITY */
    uint8_t special_kind; /* nt_ui_special_kind_t; NONE unless this element carries special data */
    uint8_t _reserved;
    nt_ui_transform_t transform; /* identity when !HAS_TRANSFORM */
    float opacity;               /* 1.0F when !HAS_OPACITY */
    union {
        const nt_ui_label_deco_t *text_deco; /* NT_UI_SPECIAL_TEXT_DECO */
    } special;
} nt_ui_element_data_t;
/* 64B on 64-bit, 52B on 32-bit/WASM. */
_Static_assert(sizeof(nt_ui_element_data_t) == (sizeof(void *) == 8 ? 64 : 52), "nt_ui_element_data_t stable ABI");
_Static_assert(sizeof(nt_ui_transform_t) == 36, "nt_ui_transform_t — fail loudly if extended");

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
/* void*-returning xform variant so Clay's .userData slot needs no const-cast. */
#define NT_UI_CLAY_DATA_XFORM(layer_value, t_ptr, opacity_value) ((void *)nt_ui_make_element_data_xform((layer_value), NULL, (t_ptr), (opacity_value)))

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
    /* true = raycast hit-test against game's view_proj (3D ctx); the game must populate view_proj
     * via nt_ui_set_view_proj after each nt_ui_begin (per-frame reset). false (default) =
     * inverse-affine 2D hit-test against baked screen-space transform. */
    bool use_raycast_input;
    /* 3D ctx render-only depth bias. 0 = off. Positive values draw deeper hierarchy levels
     * slightly closer in NDC Z; hit-test keeps using the unbiased widget plane. */
    float element_depth_bias_ndc;
    /* Per-depth modal z-band stride; 0 = default NT_UI_MODAL_ZBAND_STRIDE. */
    int16_t modal_zband_stride;
    /* Per-id retained-state pool size; MUST be power-of-2. 0 = default NT_UI_STATE_SLOTS. */
    uint32_t state_slots;
    /* Linear-probe window for the state pool; 1..state_slots. 0 = default NT_UI_STATE_PROBE_MAX. */
    uint32_t state_probe_max;
    /* Per-rich-BLOCK frame-scratch caps; each 0 = compile-time NT_UI_RICH_MAX_* default. Rich blocks
     * coexist (deferred Clay-walker emit) so the cost is per-block-cap x blocks-per-frame -- lower
     * them for a memory-constrained / labels-only context. */
    uint32_t rich_max_runs;
    uint32_t rich_max_styles;
    uint32_t rich_max_text_bytes;
} nt_ui_create_desc_t;

static inline nt_ui_create_desc_t nt_ui_create_desc_defaults(void) {
    return (nt_ui_create_desc_t){
        .max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT,
        .use_raycast_input = false,
        .element_depth_bias_ndc = 0.0F,
        .modal_zband_stride = NT_UI_MODAL_ZBAND_STRIDE,
        .state_slots = 0U,         /* 0 = compile-time NT_UI_STATE_SLOTS default */
        .state_probe_max = 0U,     /* 0 = compile-time NT_UI_STATE_PROBE_MAX default */
        .rich_max_runs = 0U,       /* 0 = compile-time NT_UI_RICH_MAX_RUNS default */
        .rich_max_styles = 0U,     /* 0 = compile-time NT_UI_RICH_MAX_STYLES default */
        .rich_max_text_bytes = 0U, /* 0 = compile-time NT_UI_RICH_MAX_TEXT_BYTES default */
    };
}

size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc);
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc);
void nt_ui_destroy_context(nt_ui_context_t *ctx);

/* The ctx's 3D-mode flag (the create-desc use_raycast_input). Lets read-only consumers (e.g. the
   probe / devapi) report a 2D-vs-3D projection tag without reaching into the opaque ctx. */
bool nt_ui_context_uses_raycast(const nt_ui_context_t *ctx);

/* The ctx's last-frame Clay LAYOUT dimensions — the SAME space the probe bounds + the Y-up flip use
   (nt_ui_scale apps make these differ from raw g_nt_window.fb_*). Read-only consumers (probe / devapi
   coord metadata) report THESE as the coordinate-space dims so read==write stays exact. Writes 0,0 on
   a NULL clay (pre-first-frame). */
void nt_ui_context_layout_size(const nt_ui_context_t *ctx, float *out_w, float *out_h);

/* True once the ctx has completed at least one nt_ui_begin (begin_w > 0). Coordinate conversion +
   bounds are only valid then — a never-begun ctx has degenerate (0) dims that trap the converters. */
bool nt_ui_context_has_frame(const nt_ui_context_t *ctx);

/* REQUIRED for ctx with use_raycast_input=true. Call IMMEDIATELY after nt_ui_begin and BEFORE
 * any widget hit-test (nt_ui_button_begin, nt_ui_step_interaction*, nt_ui_test_hit etc.). The
 * setter copies into ctx and pre-computes the inverse for raycast; `nt_ui_begin` resets the
 * view_proj_set flag so a forgotten setter trips the per-frame assert instead of silently
 * raycasting through last frame's stale camera. ctx is non-NULL, use_raycast_input is true,
 * view_proj is finite + non-singular.
 *
 * Coord convention: baked.m for 3D ctx carries NO implicit Y-flip; game's view_proj must map
 * LAYOUT-space input (Y-down, matching Clay) → clip space directly. For screen-fit 3D UI use
 * `glm_ortho(0, w, h, 0, near, far)` (flipped top/bottom). For perspective 3D world UI, set up
 * lookAt with up = (0, -1, 0) so screen-down aligns with Clay layout-down.
 *
 * Render-side contract: this setter only feeds the hit-test path. The sprite/text materials
 * must receive the SAME view_proj via the game's frame-uniforms UBO before nt_ui_walk, or the
 * rendered geometry will not match where hit-test thinks the widgets are. */
void nt_ui_set_view_proj(nt_ui_context_t *ctx, const float view_proj[16]);
/* Optional for 3D ctx. Positive values draw deeper hierarchy levels closer by
 * hierarchy_depth * ndc_per_element after projection; 0 disables the render bias. */
void nt_ui_set_element_depth_bias(nt_ui_context_t *ctx, float ndc_per_element);

void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font);

/* Device->layout viewport, the ctx's single device<->layout mapping.
 *   (x, y) — device-space offset of the layout content rect (LETTERBOX/CROP margins; 0 for unscaled)
 *   (w, h) — device-space size of that content rect (logical_dim * scale), must be > 0. */
typedef struct {
    float x, y, w, h;
} nt_ui_viewport_t;

/* Overrides this frame's viewport (begin reset it to the identity {0,0,screen_w,screen_h}).
 * Call AFTER nt_ui_begin and BEFORE the first hit-test resolve (step/query/pointer_hot) — same
 * lazy window as nt_ui_set_view_proj. Scaled apps feed nt_ui_viewport_from_scale here. */
void nt_ui_set_viewport(nt_ui_context_t *ctx, nt_ui_viewport_t vp);

/* Bidirectional device<->layout converters through the ctx viewport (cglm vec2 = float[2], passed
 * float[16]-style so the public header stays cglm-free). screen = device px; layout = Clay px.
 * No Y-flip (device and Clay layout are both Y-down). */
void nt_ui_screen_to_layout(const nt_ui_context_t *ctx, const float screen[2], float out_layout[2]);
void nt_ui_layout_to_screen(const nt_ui_context_t *ctx, const float layout[2], float out_screen[2]);

/* pointers[0..count) drive multitouch under α-semantics — see nt_ui_capture_t doc. pointers carry
 * RAW DEVICE coords; the ctx converts them to layout space via the viewport at the lazy hit resolve. */
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, float dt, const nt_pointer_t *pointers, uint32_t count);
void nt_ui_end(nt_ui_context_t *ctx);

/* Extensible widget descriptor for the inspector. Descriptors must be pointer-stable
 * for the registering module's lifetime (static const is the canonical pattern).
 *
 *   static const nt_ui_widget_def_t INV_SLOT_DEF = {
 *       .name = "inv_slot", .pill_color = 0xFFB060A0,
 *   };
 *   nt_ui_widget_register(ctx, id, &INV_SLOT_DEF, NULL, true); */
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
void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, const nt_ui_widget_def_t *def, const int16_t pad_lrtb[4], bool enabled);
#else
static inline void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, const nt_ui_widget_def_t *def, const int16_t pad_lrtb[4], bool enabled) {
    (void)ctx;
    (void)id;
    (void)def;
    (void)pad_lrtb;
    (void)enabled;
}
#endif

/* Returns NULL when no widget is registered at that id; pointer is caller-owned. */
const nt_ui_widget_def_t *nt_ui_widget_lookup(const nt_ui_context_t *ctx, uint32_t id);

/* Returns false (out untouched) when the id is unregistered or has no recorded padding. */
bool nt_ui_widget_get_hit_padding(const nt_ui_context_t *ctx, uint32_t id, int16_t out_lrtb[4]);

// #region probe
/* Flat POD tree extraction for a bot / smoke-test. Clay-free by contract: no Clay type may appear
 * in the node struct or the collect signature. id_string is COPIED into a node-owned fixed-cap
 * buffer on collect (the borrowed Clay pointer dies at the next nt_ui_begin). DEBUG_TOOLS-gated. */

#ifndef NT_UI_PROBE_ID_CAP
#define NT_UI_PROBE_ID_CAP 64 /* owned id_string copy cap incl. NUL */
#endif
#ifndef NT_UI_PROBE_TEXT_CAP
#define NT_UI_PROBE_TEXT_CAP 64 /* owned text / label copy cap incl. NUL */
#endif
/* Fallback cap for the caller-buffer nt_ui_probe_collect form (tests / general callers). The devapi
   path uses nt_ui_probe_collect_owned, whose arena scratch is sized to the ctx RUNTIME max_elements, so
   it never count-truncates regardless of this compile-time default. */
#ifndef NT_UI_PROBE_MAX_NODES
#define NT_UI_PROBE_MAX_NODES NT_UI_DEFAULT_MAX_ELEMENT_COUNT
#endif

/* Coarse role tag. Registered widgets resolve to their def->name; unregistered elements fall
 * back to the Clay config-mask kind (box/text/image/floating). */
typedef enum nt_ui_probe_role_t {
    NT_UI_PROBE_ROLE_BOX = 0, /* default / unregistered container */
    NT_UI_PROBE_ROLE_TEXT,
    NT_UI_PROBE_ROLE_IMAGE,
    NT_UI_PROBE_ROLE_FLOATING,
    NT_UI_PROBE_ROLE_WIDGET, /* a registered widget; name carries the specific kind (def->name) */
} nt_ui_probe_role_t;

/* Flat node, NO Clay types. parent is emitted from the DFS stack (NT_UI_PROBE_NO_PARENT for roots);
 * bounds = {x, y, w, h} framebuffer px (Y-up). collect emits nodes incl. invisible/offscreen/disabled
 * so the bot decides what to filter; up to the caller's cap (NT_UI_PROBE_MAX_NODES static form; ctx
 * max_elements arena form), then truncated (see collect). */
#define NT_UI_PROBE_NO_PARENT 0U /* root nodes have id != 0, so 0 is an unambiguous "no parent" */

typedef struct nt_ui_probe_node_t {
    uint32_t id;     /* Clay element id */
    uint32_t parent; /* parent id, or NT_UI_PROBE_NO_PARENT for a root */
    nt_ui_probe_role_t role;
    char id_string[NT_UI_PROBE_ID_CAP]; /* owned copy, NUL-terminated */
    char text[NT_UI_PROBE_TEXT_CAP];    /* own text content (text leaves only), NUL-terminated */
    char label[NT_UI_PROBE_TEXT_CAP];   /* first text-child caption, distinct from text */
    char role_name[NT_UI_PROBE_ID_CAP]; /* registered def->name when role==WIDGET, else "" */
    float bounds[4];                    /* x, y, w, h in framebuffer px (Y-up) */
    uint16_t id_string_len;
    uint16_t text_len;
    uint16_t label_len;
    uint16_t child_count; /* number of direct children emitted in this collect */
    bool visible;         /* !offscreen AND not ancestor-clipped AND composed opacity > threshold */
    bool enabled;         /* per-id slot signal; unregistered -> true */
} nt_ui_probe_node_t;

#if NT_UI_DEBUG_TOOLS
/* Collects the LAST completed frame's tree into `out` (up to `cap` nodes). Writes the node count to
 * *out_count (nullable). Sets *out_truncated (nullable) true when the walk stopped early — the `cap`
 * node limit or the internal DFS-stack limit was hit, so the tree is partial. Returns the count.
 * ctx must have completed nt_ui_end. Stops at `cap`; never writes past out[cap-1]. */
uint32_t nt_ui_probe_collect(const nt_ui_context_t *ctx, nt_ui_probe_node_t *out, uint32_t cap, uint32_t *out_count, bool *out_truncated);

/* Owned-scratch collect: walks into the ctx's arena probe_scratch (cap = max_elements) and returns it.
 * Because the scratch tracks the ctx element budget, a within-capacity tree never count-truncates —
 * *out_truncated then reflects ONLY the internal DFS-stack depth limit, not an element-count cap.
 * The returned pointer is owned by the ctx and stays valid until the next collect on this ctx. */
const nt_ui_probe_node_t *nt_ui_probe_collect_owned(const nt_ui_context_t *ctx, uint32_t *out_count, bool *out_truncated);
#else
static inline uint32_t nt_ui_probe_collect(const nt_ui_context_t *ctx, nt_ui_probe_node_t *out, uint32_t cap, uint32_t *out_count, bool *out_truncated) {
    (void)ctx;
    (void)out;
    (void)cap;
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (out_truncated != NULL) {
        *out_truncated = false;
    }
    return 0U;
}

static inline const nt_ui_probe_node_t *nt_ui_probe_collect_owned(const nt_ui_context_t *ctx, uint32_t *out_count, bool *out_truncated) {
    (void)ctx;
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (out_truncated != NULL) {
        *out_truncated = false;
    }
    return NULL;
}
#endif
// #endregion

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
/* Counts Clay IMAGE render-commands (nt_ui_image UI images) only. Inline rich-text images emit inside the
 * rich block's coalesced sprite batch during its CUSTOM self-emit -- they are NOT Clay commands and do NOT
 * count here (see nt_ui_rich_test_image_emit_count for the rich-image probe). */
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

/* Collision-safe 32-bit id fold (Knuth/Murmur mix): spreads (base, key) far apart so consecutive
 * seeds' Clay anon-child spaces never overlap (an additive base+key would alias -> DUPLICATE_ID).
 * Pure; folds 0 to 1 (the no-widget sentinel). Shared by child ids, vlist rows, tabbar tabs. */
static inline uint32_t nt_ui_fmix_id(uint32_t base, uint32_t key) {
    uint32_t h = base * 0x9E3779B1U;
    h = h ^ (key * 0x85EBCA6BU);
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}

/* Child id from a parent scope + a string label (fmix, never 0) — game code derives per-widget
 * ids without inventing numeric salts: nt_ui_child_id(row_id, "btn") recycles with row_id. */
uint32_t nt_ui_child_id(uint32_t parent_id, const char *label);

/* `found == false` if id was not declared the immediately preceding frame. */
typedef struct {
    float x, y, width, height;
    bool found;
} nt_ui_bbox_t;
nt_ui_bbox_t nt_ui_get_bbox(const nt_ui_context_t *ctx, uint32_t id);

/* Multitouch (α-semantics: single capture per widget). step/query iterate all
 * frame_pointers[0..count) and resolve the pointer that "owns" each widget:
 *   - if a pidx is already holding the id, it stays the owner
 *   - else the first pressed_now pointer over the widget captures it
 *   - additional fingers landing on a captured widget are silently ignored
 *   - a pointer bound to one widget is invisible to all other widgets
 * Aggregated interaction_t.hovered = any pointer over; pressed/released/clicked
 * track the single owner pidx. interaction_t.pointer_id holds the OS id of the
 * owning (or first hovering) pointer. */
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
    float distance;         /* 3D ctx: world distance (near-plane-relative) to this widget; 0 in 2D */
} nt_ui_interaction_t;

/* Front-most interactive widget under a pointer, resolved per frame (3D: nearest; 2D: top zIndex). */
typedef struct {
    uint32_t id;    /* 0 = none under the pointer */
    float distance; /* world distance in 3D ctx; 0 in 2D */
} nt_ui_hot_t;

/* Idempotent query: same result N times per frame; safe outside begin/end. Side effects are
 * frame-scoped only (hover observability + the once-per-frame arbitration resolve), never capture or
 * button-edge state — those live in step_interaction. */
nt_ui_interaction_t nt_ui_query_interaction(nt_ui_context_t *ctx, uint32_t id);

/* Inflates bbox BEFORE the inverse-affine for mobile touch-friendly hit areas.
 * Asserts each component >= 0; NULL = unpadded. */
nt_ui_interaction_t nt_ui_query_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* MUTATING — call ONCE per widget per frame from the widget's begin.
 * Sidebar-consumed pointer returns zeroed struct (see nt_ui_inspector_pointer_consumed). */
nt_ui_interaction_t nt_ui_step_interaction(nt_ui_context_t *ctx, uint32_t id);
nt_ui_interaction_t nt_ui_step_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* Consolidated interaction result: the base capture edges PLUS the cfg-gated gesture cell.
 * hovered/pressed/released/clicked/held + pos/drag/pointer_id mirror nt_ui_interaction_t; the gesture
 * fields (double_clicked / long_pressed / hold_progress) stay zero unless nt_ui_events got a cfg that
 * requests them. hold_progress is a linear 0..1 ramp toward long_press_secs while held && hovered. */
typedef struct {
    bool hovered;           /* pointer over bbox (transform-aware) */
    bool pressed;           /* currently captured (held) */
    bool released;          /* released this frame (even off-widget = cancel) */
    bool held;              /* pressed && hovered (down AND over the widget) */
    bool clicked;           /* released OVER the widget -> one-shot */
    bool pressed_now;       /* press began this frame -> one-shot (survives a same-frame release; pressed does not) */
    bool double_clicked;    /* gesture: 2nd press within the dbl window+radius (cfg->double_click) */
    bool long_pressed;      /* gesture: held past long_press_secs without moving -> one-shot per hold */
    float hold_progress;    /* gesture: linear 0..1 toward long_press_secs while held && hovered; 0 otherwise */
    float pos[2];           /* current pointer pos (UI-space) */
    float drag_dx, drag_dy; /* = pos - press_pos (convenience) */
    uint32_t pointer_id;    /* which pointer captured (multitouch) */
} nt_ui_events_t;
_Static_assert(sizeof(nt_ui_events_t) == 32, "nt_ui_events_t stable ABI (8 bool + 5 float + 1 u32)");

/* Per-widget gesture knobs. long_press_secs <= 0 disables BOTH long-press AND hold_progress;
 * double_click is opt-in. The app-wide dbl window + move radius live on the context
 * (nt_ui_set_gesture_constants), not here. cfg==NULL on nt_ui_events = base path only, zero gesture
 * alloc. */
typedef struct {
    float long_press_secs; /* > 0 enables long-press + hold_progress; <= 0 disables both */
    bool double_click;     /* opt-in double-click detection */
} nt_ui_events_cfg_t;
_Static_assert(sizeof(nt_ui_events_cfg_t) == 8, "nt_ui_events_cfg_t stable ABI (1 float + 1 bool + 3 pad)");

/* MUTATING — the single canonical interaction step. Call ONCE per widget per frame from the
 * widget's begin, in place of nt_ui_step_interaction. Runs the base capture/edge machine; when cfg
 * requests a gesture (cfg != NULL && (cfg->double_click || cfg->long_press_secs > 0)) it also advances
 * the per-id gesture cell. cfg==NULL grows the state pool by zero. Returns the full result directly. */
nt_ui_events_t nt_ui_events(nt_ui_context_t *ctx, uint32_t id, const nt_ui_events_cfg_t *cfg);

/* Same single mutating step, but the base capture/hit-test honors an inflated hit zone (pad_lrtb,
 * left/right/top/bottom px, NULL = none). For widgets with a hit_padding style (e.g. nt_ui_button). */
nt_ui_events_t nt_ui_events_padded(nt_ui_context_t *ctx, uint32_t id, const nt_ui_events_cfg_t *cfg, const int16_t pad_lrtb[4]);

/* Idempotent read: same result N times per frame, advancing NOTHING (no capture commit, no gesture
 * timer). Reads the latched gesture cell if one exists (else gesture fields stay zero). Safe to call
 * after the first nt_ui_begin. Mirror of nt_ui_query_interaction for the consolidated result.
 * NOTE: the base edges are computed UNPADDED — for a widget stepped via nt_ui_events_padded the
 * hovered/clicked here won't include hit padding (the latched gesture fields are unaffected). */
nt_ui_events_t nt_ui_query_events(nt_ui_context_t *ctx, uint32_t id);

/* App-wide gesture constants: the double-click window (secs) and the move radius (px) within
 * which a second press still counts as a double-click. Hold/long-press is NOT cancelled by
 * movement — only by leaving the widget's hitbox. Asserts each is finite and >= 0 (fail-early,
 * no silent clamp). Defaults: NT_UI_GESTURE_DBL_WINDOW_SECS / _MOVE_RADIUS_PX. */
void nt_ui_set_gesture_constants(nt_ui_context_t *ctx, float dbl_window_secs, float move_radius_px);

/* Inert occluder: enters id into the interactive registry so it wins next-frame topmost-z
 * arbitration over widgets behind it (the pointer can't leak through), but never captures,
 * clicks, or reports hover. Disabled widgets call this so a modal/overlay blocks input to
 * whatever sits underneath. pad_lrtb inflates the block area (NULL = none, asserts >= 0). */
void nt_ui_block_pointer(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* True when any capture is active OR a pointer is over a widget this frame. */
bool nt_ui_wants_pointer(const nt_ui_context_t *ctx);

/* 3D ctx only (asserts use_raycast_input): max world distance the pointer's ray may travel before UI
 * stops registering. Feed the nearest world-geometry hit (so the player can't click UI through a
 * wall) and/or an interaction range. Default +inf = no cutoff. Reset each nt_ui_begin; feed it BEFORE
 * this frame's first step_interaction / query / nt_ui_pointer_hot (the per-frame resolve latches once). */
void nt_ui_set_pointer_occlusion(nt_ui_context_t *ctx, uint32_t pointer_index, float max_world_distance);

/* Front-most interactive widget under the pointer this frame (3D: nearest within the cutoff; 2D:
 * highest effective zIndex, tie → last-registered = later step_interaction call, which equals
 * declaration/paint order when widgets step in declaration order). Resolved lazily from the prev-frame registry on the
 * first step/query (mutating). Only widgets that called step_interaction last frame are candidates. In 3D ctx call
 * nt_ui_set_view_proj first — the lazy resolve hit-tests, which asserts view_proj is set. id 0 = none. */
nt_ui_hot_t nt_ui_pointer_hot(nt_ui_context_t *ctx, uint32_t pointer_index);
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

/* Number of TEXT commands that matched a decorated label this walk (wrapped lines count each) —
 * proves decoration reaches every wrapped line (uniform element_data), not just the first. Process-global; reset per test. */
uint32_t nt_ui_test_deco_applied_count(void);
void nt_ui_test_reset_deco_applied_count(void);

int32_t nt_ui_test_clay_default_max_element_count(void);
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void);
#endif
// #endregion

/* Widget headers NOT re-exported — keeps clay.h out of the public umbrella. */

#endif /* NT_UI_H */
