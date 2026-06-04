#include "ui/nt_ui.h"

#include "atlas/nt_atlas.h"
#include "core/nt_builtins.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "time/nt_time.h"

/* Clay private surface lives in nt_ui_clay_impl.c. */

#if !defined(CLAY_PINNED_MAJOR) || !defined(CLAY_PINNED_MINOR)
#error "nt_ui: CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR must be defined by CMake"
#endif

#include "clay.h"

_Static_assert(CLAY_PINNED_MAJOR == 0 && CLAY_PINNED_MINOR == 14, "Clay v0.14 required");

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_align.h"
#include "core/nt_assert.h"
#include "core/nt_clamp.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

// #region module_state
/* Only one ctx may be in-frame at a time; nt_ui_begin asserts NULL on entry. */
static nt_ui_context_t *g_nt_ui_inframe_ctx = NULL;
static bool s_nt_ui_module_initialized = false;

/* Pre-built element_data for each layer (user_data=NULL) — avoids scratch alloc. */
static nt_ui_element_data_t s_default_element_data[256];
_Static_assert(sizeof(s_default_element_data) == 256 * sizeof(nt_ui_element_data_t), "s_default_element_data sized 256 x element_data");
// #endregion

// #region clay_error_handler
/* All Clay errors are fatal; assert compiles out in NT_ASSERT_OFF builds. */
static void nt_ui_clay_error_cb(Clay_ErrorData err) {
    /* errorText is .length + .chars, NOT NUL-terminated. */
    const int len = err.errorText.length;
    const char *const chars = (err.errorText.chars != NULL && len > 0) ? err.errorText.chars : "(no text)";
    const int safe_len = (err.errorText.chars != NULL && len > 0) ? len : 9;
    const int type = (int)err.errorType;
    NT_LOG_ERROR("clay error type=%d: %.*s", type, safe_len, chars);
    NT_ASSERT(false && "nt_ui: Clay reported a contract violation (see preceding log line)");
}
// #endregion

// #region measure_cb
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static Clay_Dimensions nt_ui_measure_text_cb(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data;
    NT_ASSERT(g_nt_ui_inframe_ctx != NULL && "measure_cb: Clay called outside begin/end");
    NT_ASSERT(config != NULL && "measure_cb: Clay passed NULL config");

    nt_ui_context_t *ctx = g_nt_ui_inframe_ctx;
    NT_ASSERT((uint32_t)config->fontId < NT_UI_MAX_FONTS && "nt_ui measure_cb: fontId out of range; check CLAY_TEXT_CONFIG vs NT_UI_MAX_FONTS");
    nt_font_t font = ctx->fonts[config->fontId];
    NT_ASSERT(nt_font_valid(font) && "nt_ui measure_cb: font slot empty; call nt_ui_set_font before declaring TEXT with this fontId");
    const float ls = (float)config->letterSpacing;
    nt_text_size_t s = nt_font_measure_n(font, text.chars, (size_t)text.length, (float)config->fontSize, ls);
    /* Clay's MeasureTextCached subtracts one trailing letterSpacing per line; add it back. */
    if (s.width > 0.0F && ls != 0.0F) {
        s.width += ls;
    }
    return (Clay_Dimensions){.width = s.width, .height = s.height};
}
// #endregion

// #region module_init
#define NT_UI_CORNER_SEGMENTS 6
_Static_assert(NT_UI_CORNER_SEGMENTS >= 2 && NT_UI_CORNER_SEGMENTS <= 16, "NT_UI_CORNER_SEGMENTS must be in [2, 16]");
#define NT_UI_PI_F 3.14159265358979323846F

/* Quadrant index: 0=BR, 1=BL, 2=TL, 3=TR (a_start = q * π/2). */
typedef struct {
    float cos;
    float sin;
} nt_ui_trig_pair_t;
static nt_ui_trig_pair_t s_arc_lut[4][NT_UI_CORNER_SEGMENTS + 1];

static void nt_ui_init_arc_lut(void) {
    for (uint32_t q = 0U; q < 4U; ++q) {
        const float a_start = NT_UI_PI_F * 0.5F * (float)q;
        for (uint32_t s = 0U; s <= NT_UI_CORNER_SEGMENTS; ++s) {
            const float t = (float)s / (float)NT_UI_CORNER_SEGMENTS;
            const float a = a_start + (NT_UI_PI_F * 0.5F * t);
            s_arc_lut[q][s].cos = cosf(a);
            s_arc_lut[q][s].sin = sinf(a);
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_module_init(void) {
    NT_ASSERT(!s_nt_ui_module_initialized && "nt_ui_module_init: already initialized; call nt_ui_module_shutdown first");
    nt_ui_clay_priv_set_measure_text_cb(nt_ui_measure_text_cb);
    g_nt_ui_inframe_ctx = NULL;
    Clay_SetCurrentContext(NULL);
    nt_ui_init_arc_lut();
    /* opacity=1.0F is the safe sentinel — stray read past the flag guard composes to no-op. */
    for (uint32_t i = 0; i < 256U; i++) {
        s_default_element_data[i] = (nt_ui_element_data_t){
            .layer = (nt_ui_layer_t)i,
            .flags = 0U,
            .opacity = 1.0F,
            .transform = nt_ui_transform_defaults(),
            .user_data = NULL,
        };
        NT_ASSERT(s_default_element_data[i].opacity == 1.0F);
        NT_ASSERT(s_default_element_data[i].flags == 0U);
    }
    s_nt_ui_module_initialized = true;
}
void nt_ui_module_shutdown(void) {
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_module_shutdown: not initialized");
    nt_ui_clay_priv_set_measure_text_cb(NULL);
    g_nt_ui_inframe_ctx = NULL;
    Clay_SetCurrentContext(NULL);
    s_nt_ui_module_initialized = false;
}
// #endregion

// #region create_destroy
/* ctx struct gets padded to cache line so Clay's arena starts on a clean boundary. */
#define NT_UI_CACHE_LINE ((size_t)64U)

#if NT_UI_DEBUG_TOOLS
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
/* Smallest power of 2 ≥ x (assumes x ≥ 1). */
static inline uint32_t nt_ui_next_pow2_u32(uint32_t x) {
    if (x <= 1U) {
        return 1U;
    }
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1U;
}
#endif

size_t nt_ui_min_arena_size(const nt_ui_create_desc_t *desc) {
    NT_ASSERT(desc != NULL && "nt_ui_min_arena_size: desc must be non-NULL");
    NT_ASSERT(desc->max_elements > 0U && "nt_ui_min_arena_size: desc->max_elements must be > 0");
    NT_ASSERT(desc->max_elements <= UINT16_MAX && "nt_ui_min_arena_size: desc->max_elements exceeds uint16 sorted-index range");
    /* SetMaxElementCount also writes word_cache_count = N*2; restore via the same call. */
    Clay_Context *saved_ctx = Clay_GetCurrentContext();
    const int32_t saved_default = nt_ui_clay_priv_default_max_element_count();
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount((int32_t)desc->max_elements);
    const size_t clay_bytes = (size_t)Clay_MinMemorySize();
    Clay_SetMaxElementCount(saved_default);
    Clay_SetCurrentContext(saved_ctx);
    const size_t tree_baked_bytes = NT_ALIGN_UP(sizeof(nt_ui_baked_xform_t) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t tree_root_bytes = NT_ALIGN_UP(sizeof(*((nt_ui_context_t *)0)->tree_root_for_elem) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t tree_dfs_bytes = NT_ALIGN_UP(sizeof(nt_ui_dfs_frame_t) * NT_UI_TREE_DFS_DEPTH_CAP, NT_UI_CACHE_LINE);
    const size_t hit_baked_bytes = NT_ALIGN_UP(sizeof(nt_ui_baked_xform_t) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t hit_clip_bytes = NT_ALIGN_UP(sizeof(*((nt_ui_context_t *)0)->hit_clip_parent_id) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t hit_gen_bytes = NT_ALIGN_UP(sizeof(*((nt_ui_context_t *)0)->hit_generation) * desc->max_elements, NT_UI_CACHE_LINE);
#if NT_UI_DEBUG_TOOLS
    const uint32_t widget_cap = nt_ui_next_pow2_u32(desc->max_elements * 2U);
    const size_t widget_registry_bytes = NT_ALIGN_UP(sizeof(nt_ui_widget_slot_t) * widget_cap, NT_UI_CACHE_LINE);
    const size_t debug_zones_bytes = NT_ALIGN_UP(sizeof(nt_ui_debug_zone_t) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t inspector_collapsed_bytes = NT_ALIGN_UP(sizeof(uint32_t) * desc->max_elements, NT_UI_CACHE_LINE);
#else
    const size_t widget_registry_bytes = 0U;
    const size_t debug_zones_bytes = 0U;
    const size_t inspector_collapsed_bytes = 0U;
#endif
    return NT_ALIGN_UP(sizeof(struct nt_ui_context), NT_UI_CACHE_LINE) + tree_baked_bytes + tree_root_bytes + tree_dfs_bytes + hit_baked_bytes + hit_clip_bytes + hit_gen_bytes +
           widget_registry_bytes + debug_zones_bytes + inspector_collapsed_bytes + clay_bytes;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size, const nt_ui_create_desc_t *desc) {
    NT_ASSERT(arena != NULL && "nt_ui_create_context: arena must be non-NULL");
    NT_ASSERT(desc != NULL && "nt_ui_create_context: desc must be non-NULL");
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_create_context: call nt_ui_module_init() once before any create_context");
    NT_ASSERT(((uintptr_t)arena & (NT_UI_ARENA_ALIGN - 1U)) == 0U && "nt_ui_create_context: arena must be NT_UI_ARENA_ALIGN-aligned (alignas(NT_UI_ARENA_ALIGN) static uint8_t arena[N])");
    NT_ASSERT(arena_size >= nt_ui_min_arena_size(desc) && "nt_ui_create_context: arena_size < nt_ui_min_arena_size(desc)");

    nt_ui_context_t *ctx = (nt_ui_context_t *)arena;
    memset(ctx, 0, sizeof(*ctx));

#if NT_UI_DEBUG_TOOLS
    /* memset zero above would leave panel_width=0; restore defaults. */
    ctx->inspector_metrics = NT_UI_INSPECTOR_METRICS_DEFAULT;
#endif

    const size_t ctx_size = NT_ALIGN_UP(sizeof(struct nt_ui_context), NT_UI_CACHE_LINE);
    ctx->max_elements = desc->max_elements;
    const size_t tree_baked_bytes = NT_ALIGN_UP(sizeof(nt_ui_baked_xform_t) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t tree_root_bytes = NT_ALIGN_UP(sizeof(*ctx->tree_root_for_elem) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t tree_dfs_bytes = NT_ALIGN_UP(sizeof(nt_ui_dfs_frame_t) * NT_UI_TREE_DFS_DEPTH_CAP, NT_UI_CACHE_LINE);
    const size_t hit_baked_bytes = NT_ALIGN_UP(sizeof(nt_ui_baked_xform_t) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t hit_clip_bytes = NT_ALIGN_UP(sizeof(*ctx->hit_clip_parent_id) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t hit_gen_bytes = NT_ALIGN_UP(sizeof(*ctx->hit_generation) * desc->max_elements, NT_UI_CACHE_LINE);
    ctx->tree_baked = (nt_ui_baked_xform_t *)((char *)arena + ctx_size);
    ctx->tree_root_for_elem = (int32_t *)((char *)arena + ctx_size + tree_baked_bytes);
    ctx->tree_dfs_stack = (nt_ui_dfs_frame_t *)((char *)arena + ctx_size + tree_baked_bytes + tree_root_bytes);
    ctx->hit_baked = (nt_ui_baked_xform_t *)((char *)arena + ctx_size + tree_baked_bytes + tree_root_bytes + tree_dfs_bytes);
    ctx->hit_clip_parent_id = (uint32_t *)((char *)arena + ctx_size + tree_baked_bytes + tree_root_bytes + tree_dfs_bytes + hit_baked_bytes);
    ctx->hit_generation = (uint32_t *)((char *)arena + ctx_size + tree_baked_bytes + tree_root_bytes + tree_dfs_bytes + hit_baked_bytes + hit_clip_bytes);
    ctx->current_generation = 0U;
    const size_t after_tree = ctx_size + tree_baked_bytes + tree_root_bytes + tree_dfs_bytes + hit_baked_bytes + hit_clip_bytes + hit_gen_bytes;
#if NT_UI_DEBUG_TOOLS
    ctx->widget_registry_cap = nt_ui_next_pow2_u32(desc->max_elements * 2U);
    ctx->widget_registry_mask = ctx->widget_registry_cap - 1U;
    ctx->debug_zone_cap = desc->max_elements;
    ctx->inspector_collapsed_cap = desc->max_elements;
    const size_t widget_registry_bytes = NT_ALIGN_UP(sizeof(nt_ui_widget_slot_t) * ctx->widget_registry_cap, NT_UI_CACHE_LINE);
    const size_t debug_zones_bytes = NT_ALIGN_UP(sizeof(nt_ui_debug_zone_t) * desc->max_elements, NT_UI_CACHE_LINE);
    const size_t inspector_collapsed_bytes = NT_ALIGN_UP(sizeof(uint32_t) * desc->max_elements, NT_UI_CACHE_LINE);
    ctx->widget_registry = (nt_ui_widget_slot_t *)((char *)arena + after_tree);
    ctx->debug_zones = (nt_ui_debug_zone_t *)((char *)arena + after_tree + widget_registry_bytes);
    ctx->inspector_collapsed_ids = (uint32_t *)((char *)arena + after_tree + widget_registry_bytes + debug_zones_bytes);
    const size_t after_debug = after_tree + widget_registry_bytes + debug_zones_bytes + inspector_collapsed_bytes;
#else
    const size_t after_debug = after_tree;
#endif
    void *clay_mem = (char *)arena + after_debug;
    const size_t clay_size = arena_size - after_debug;

    /* SetMaxElementCount writes per-ctx if current is non-NULL — null it first to stage the global. */
    Clay_Context *saved_ctx = Clay_GetCurrentContext();
    const int32_t saved_default = nt_ui_clay_priv_default_max_element_count();
    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount((int32_t)desc->max_elements);

    ctx->in_frame = false;
    ctx->clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_size, clay_mem);
    ctx->clay = Clay_Initialize(ctx->clay_arena, (Clay_Dimensions){.width = 1.0F, .height = 1.0F}, (Clay_ErrorHandler){.errorHandlerFunction = nt_ui_clay_error_cb, .userData = ctx});

    Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount(saved_default);
    Clay_SetCurrentContext(saved_ctx);

    return ctx;
}

void nt_ui_destroy_context(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_destroy_context: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_destroy_context: ctx is mid-frame (call nt_ui_end first)");
    /* Clay's current_ptr would dangle into the freshly memset arena. */
    if (Clay_GetCurrentContext() == ctx->clay) {
        Clay_SetCurrentContext(NULL);
    }
#if NT_UI_DEBUG_TOOLS
    nt_ui_internal_inspector_strings_release(ctx);
#endif
    memset(ctx, 0, sizeof(*ctx));
}
// #endregion

// #region font_registry
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_font: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_font: must be called outside begin/end");
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_set_font: font_id >= NT_UI_MAX_FONTS");
    ctx->fonts[font_id] = font;
}
// #endregion

// #region begin_end
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, float dt, const nt_pointer_t *pointers, uint32_t count) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(s_nt_ui_module_initialized && "nt_ui_begin: nt_ui_module_init() must be called before begin");
    NT_ASSERT(pointers != NULL && "nt_ui_begin: pointers must be non-NULL");
    NT_ASSERT(count > 0U && count <= NT_INPUT_MAX_POINTERS && "nt_ui_begin: count must be 1..NT_INPUT_MAX_POINTERS");
    /* isfinite() rejects NaN + +-inf which `>= 0.0F` alone lets through. */
    NT_ASSERT(isfinite(screen_w) && screen_w >= 0.0F && "nt_ui_begin: screen_w must be finite and non-negative");
    NT_ASSERT(isfinite(screen_h) && screen_h >= 0.0F && "nt_ui_begin: screen_h must be finite and non-negative");
    NT_ASSERT(isfinite(dt) && dt >= 0.0F && "nt_ui_begin: dt must be finite and non-negative");
    NT_ASSERT(g_nt_ui_inframe_ctx == NULL && "nt_ui_begin: another ctx is mid-frame");
    NT_ASSERT(!ctx->in_frame && "nt_ui_begin: ctx already in_frame");

    /* MUST be first so subsequent Clay calls operate on ctx->clay. */
    Clay_SetCurrentContext(ctx->clay);

    ctx->in_frame = true;
    g_nt_ui_inframe_ctx = ctx;

    /* Snapshot pointer list + dt for engine-owned hit-test + anim cache. */
    memcpy(ctx->frame_pointers, pointers, sizeof(nt_pointer_t) * count);
    ctx->frame_pointer_count = count;
    ctx->frame_dt = dt;

    /* Orphan cleanup — captures unqueried last frame would hold the pointer forever. */
    for (uint32_t i = 0; i < NT_INPUT_MAX_POINTERS; ++i) {
        if (ctx->captures[i].active_id != 0U && ctx->capture_seen[i] == 0U) {
            ctx->captures[i].active_id = 0U;
        }
        ctx->capture_seen[i] = 0U;
    }
    ctx->pointer_over_any = false;

#if NT_UI_DEBUG_TOOLS
    ctx->debug_zone_count = 0U;
#endif

    /* Reset so a button begin that asserted mid-flight can't wedge subsequent frames. */
    ctx->pending_button.active = false;

#if NT_UI_DEBUG_TOOLS
    memset(ctx->widget_registry, 0, sizeof(nt_ui_widget_slot_t) * ctx->widget_registry_cap);
    /* highlight_id is per-frame; selected_id persists across frames. */
    ctx->inspector_highlight_id = 0U;
#endif

    const nt_pointer_t *primary = &pointers[0];

#if NT_UI_DEBUG_TOOLS
    /* Pure coord check — frame-1 safe, no layout solve required. */
    ctx->inspector_pointer_consumed = false;
    if (ctx->inspector_active && primary->x >= (screen_w - ctx->inspector_metrics.panel_width)) {
        ctx->inspector_pointer_consumed = true;
    }
#endif

    /* nt_ui_inspector replaces Clay's built-in debug view. */
    Clay_SetDebugModeEnabled(false);
    Clay_SetLayoutDimensions((Clay_Dimensions){.width = screen_w, .height = screen_h});

    /* Clay v0.14 has no right/middle/wheel buttons; left only. */
    Clay_SetPointerState((Clay_Vector2){.x = primary->x, .y = primary->y}, primary->buttons[NT_BUTTON_LEFT].is_down);

    /* Y inverted: Clay scroll opposite of typical wheel_dy. */
    Clay_UpdateScrollContainers(true, (Clay_Vector2){.x = primary->wheel_dx, .y = -primary->wheel_dy}, dt);

    Clay_BeginLayout();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_end: ctx is not in_frame (begin was not called)");
    NT_ASSERT(ctx == g_nt_ui_inframe_ctx && "nt_ui_end: ctx mismatch with module in-frame ctx");
#if NT_UI_DEBUG_TOOLS
    if (ctx->inspector_active) {
        nt_ui_internal_emit_inspector_layout_extern(ctx);
    }
#endif

    /* Times the Clay layout solve only, not the begin->end span. */
    const double layout_t0 = nt_time_now();
    ctx->frozen_cmds = Clay_EndLayout();
    ctx->last_layout_ms = (float)((nt_time_now() - layout_t0) * 1000.0);

    const double build_t0 = nt_time_now();
    nt_ui_internal_build_tree(ctx);
    ctx->last_build_tree_ms = (float)((nt_time_now() - build_t0) * 1000.0);

    ctx->in_frame = false;
    g_nt_ui_inframe_ctx = NULL;
    /* Stray CLAY_* between end and next begin NULL-derefs instead of corrupting. */
    Clay_SetCurrentContext(NULL);
}
// #endregion

// #region helpers_color_pack
/* Clay's RGBA floats are 0..255 unclamped. */
static inline uint32_t nt_color_pack_clay(Clay_Color c) {
    uint32_t r = nt_clamp_f_to_u8(c.r);
    uint32_t g = nt_clamp_f_to_u8(c.g);
    uint32_t b = nt_clamp_f_to_u8(c.b);
    uint32_t a = nt_clamp_f_to_u8(c.a);
    return r | (g << 8) | (b << 16) | (a << 24);
}
// #endregion

// #region element_data_alloc
const nt_ui_element_data_t *nt_ui_make_element_data(nt_ui_layer_t layer, void *user_data) {
    if (user_data == NULL) {
        return &s_default_element_data[layer];
    }
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_make_element_data: scratch alloc failed");
    *d = (nt_ui_element_data_t){
        .layer = layer,
        .user_data = user_data,
        .flags = 0U,
        .transform = nt_ui_transform_defaults(),
        .opacity = 1.0F,
    };
    return d;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
const nt_ui_element_data_t *nt_ui_make_element_data_xform(nt_ui_layer_t layer, void *user_data, const nt_ui_transform_t *transform, float opacity) {
    NT_ASSERT(transform != NULL && "nt_ui_make_element_data_xform: transform must be non-NULL");
    NT_ASSERT(transform->scale_x > 0.0F && transform->scale_y > 0.0F && "nt_ui_make_element_data_xform: scale must be positive; use opacity=0 to hide");
    NT_ASSERT(isfinite(transform->scale_x) && isfinite(transform->scale_y) && isfinite(transform->rotation) && isfinite(transform->offset_x) && isfinite(transform->offset_y) &&
              "nt_ui_make_element_data_xform: transform fields must be finite");
    NT_ASSERT(isfinite(opacity) && opacity >= 0.0F && opacity <= 1.0F && "nt_ui_make_element_data_xform: opacity must be finite in [0,1]");
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_make_element_data_xform: scratch alloc failed");
    *d = (nt_ui_element_data_t){
        .layer = layer,
        .user_data = user_data,
        .flags = NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY,
        .transform = *transform,
        .opacity = opacity,
    };
    return d;
}
// #endregion

// #region inframe_ctx_getter
nt_ui_context_t *nt_ui_internal_get_inframe_ctx(void) { return g_nt_ui_inframe_ctx; }
// #endregion

// #region widget_registry
#if NT_UI_DEBUG_TOOLS
/* Linear probe at 50% load; returns slot for register (empty or matching) or
 * the same slot for lookup (caller checks slot->id == id). */
static inline uint32_t widget_probe_slot(const nt_ui_widget_slot_t *registry, uint32_t cap, uint32_t mask, uint32_t id) {
    const uint32_t start = id & mask;
    for (uint32_t step = 0U; step < cap; ++step) {
        const uint32_t slot = (start + step) & mask;
        const uint32_t s_id = registry[slot].id;
        if (s_id == 0U || s_id == id) {
            return slot;
        }
    }
    NT_ASSERT(0 && "widget_registry full — load factor exceeded (raise max_elements)");
    return 0U;
}

void nt_ui_widget_register(nt_ui_context_t *ctx, uint32_t id, const nt_ui_widget_def_t *def, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_widget_register: ctx must be non-NULL");
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_widget_register: pad_lrtb components must be >= 0");
    if (id == 0U || def == NULL) {
        return;
    }
    const uint32_t bucket = widget_probe_slot(ctx->widget_registry, ctx->widget_registry_cap, ctx->widget_registry_mask, id);
    nt_ui_widget_slot_t *s = &ctx->widget_registry[bucket];
    s->id = id;
    s->def = def;
    if (pad_lrtb != NULL) {
        s->has_padding = 1U;
        s->hit_padding_lrtb[0] = pad_lrtb[0];
        s->hit_padding_lrtb[1] = pad_lrtb[1];
        s->hit_padding_lrtb[2] = pad_lrtb[2];
        s->hit_padding_lrtb[3] = pad_lrtb[3];
    } else {
        s->has_padding = 0U;
        s->hit_padding_lrtb[0] = 0;
        s->hit_padding_lrtb[1] = 0;
        s->hit_padding_lrtb[2] = 0;
        s->hit_padding_lrtb[3] = 0;
    }
}
#endif

const nt_ui_widget_def_t *nt_ui_widget_lookup(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_widget_lookup: ctx must be non-NULL");
#if NT_UI_DEBUG_TOOLS
    if (id == 0U) {
        return NULL;
    }
    const uint32_t bucket = widget_probe_slot(ctx->widget_registry, ctx->widget_registry_cap, ctx->widget_registry_mask, id);
    const nt_ui_widget_slot_t *s = &ctx->widget_registry[bucket];
    return (s->id == id) ? s->def : NULL;
#else
    (void)ctx;
    (void)id;
    return NULL;
#endif
}

/* OFF body discards out_lrtb; signature is fixed by the header. */
// NOLINTNEXTLINE(readability-non-const-parameter)
bool nt_ui_widget_get_hit_padding(const nt_ui_context_t *ctx, uint32_t id, int16_t out_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_widget_get_hit_padding: ctx must be non-NULL");
    NT_ASSERT(out_lrtb != NULL && "nt_ui_widget_get_hit_padding: out_lrtb must be non-NULL");
#if NT_UI_DEBUG_TOOLS
    if (id == 0U) {
        return false;
    }
    const uint32_t bucket = widget_probe_slot(ctx->widget_registry, ctx->widget_registry_cap, ctx->widget_registry_mask, id);
    const nt_ui_widget_slot_t *s = &ctx->widget_registry[bucket];
    if (s->id != id || !s->has_padding) {
        return false;
    }
    out_lrtb[0] = s->hit_padding_lrtb[0];
    out_lrtb[1] = s->hit_padding_lrtb[1];
    out_lrtb[2] = s->hit_padding_lrtb[2];
    out_lrtb[3] = s->hit_padding_lrtb[3];
    return true;
#else
    (void)ctx;
    (void)id;
    (void)out_lrtb;
    return false;
#endif
}

// #endregion

// #region helper_emit_screen_rect
/* mat4 = aff · T(x, y+h) · S(w, -h); -h cancels the Y-flip baked into aff. */
static inline void build_quad_mat4(const float aff[6], float x, float y, float w, float h, float out_m[16]) {
    const float ox = x;
    const float oy = y + h;
    out_m[0] = aff[0] * w;
    out_m[1] = aff[2] * w;
    out_m[2] = 0.0F;
    out_m[3] = 0.0F;
    out_m[4] = -aff[1] * h;
    out_m[5] = -aff[3] * h;
    out_m[6] = 0.0F;
    out_m[7] = 0.0F;
    out_m[8] = 0.0F;
    out_m[9] = 0.0F;
    out_m[10] = 1.0F;
    out_m[11] = 0.0F;
    out_m[12] = (aff[0] * ox) + (aff[1] * oy) + aff[4];
    out_m[13] = (aff[2] * ox) + (aff[3] * oy) + aff[5];
    out_m[14] = 0.0F;
    out_m[15] = 1.0F;
}

/* Affine-only mat4 — for callers whose vertices are already in layout-space. */
static inline void build_affine_mat4(const float aff[6], float out_m[16]) {
    out_m[0] = aff[0];
    out_m[1] = aff[2];
    out_m[2] = 0.0F;
    out_m[3] = 0.0F;
    out_m[4] = aff[1];
    out_m[5] = aff[3];
    out_m[6] = 0.0F;
    out_m[7] = 0.0F;
    out_m[8] = 0.0F;
    out_m[9] = 0.0F;
    out_m[10] = 1.0F;
    out_m[11] = 0.0F;
    out_m[12] = aff[4];
    out_m[13] = aff[5];
    out_m[14] = 0.0F;
    out_m[15] = 1.0F;
}

static inline void emit_screen_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, uint32_t color_packed, const float aff[6]) {
    float m[16];
    build_quad_mat4(aff, x, y, w, h, m);
    nt_sprite_renderer_emit_region(atlas, region_index, m, 0.0F, 0.0F, color_packed, 0U);
}
// #endregion

// #region helper_clamp_radii_css3
/* CSS3 border-radius §5.5: scale all four by smallest factor so adjacent sums fit. */
static inline void clamp_radii_css3(float w, float h, float *tl, float *tr, float *bl, float *br) {
    *tl = (*tl > 0.0F) ? *tl : 0.0F;
    *tr = (*tr > 0.0F) ? *tr : 0.0F;
    *bl = (*bl > 0.0F) ? *bl : 0.0F;
    *br = (*br > 0.0F) ? *br : 0.0F;
    float factor = 1.0F;
    if (*tl + *tr > w) {
        factor = fminf(factor, w / (*tl + *tr));
    }
    if (*bl + *br > w) {
        factor = fminf(factor, w / (*bl + *br));
    }
    if (*tl + *bl > h) {
        factor = fminf(factor, h / (*tl + *bl));
    }
    if (*tr + *br > h) {
        factor = fminf(factor, h / (*tr + *br));
    }
    if (factor < 1.0F) {
        *tl *= factor;
        *tr *= factor;
        *bl *= factor;
        *br *= factor;
    }
}
// #endregion

// #region helper_emit_rounded_rect
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_rounded_rect(nt_resource_t atlas, uint32_t region_index, float x, float y, float w, float h, Clay_CornerRadius cr, uint32_t color_packed, const float aff[6]) {
    float tl = cr.topLeft;
    float tr = cr.topRight;
    float bl = cr.bottomLeft;
    float br = cr.bottomRight;
    clamp_radii_css3(w, h, &tl, &tr, &bl, &br);
    const float half_w = w * 0.5F;
    const float half_h = h * 0.5F;

    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_screen_rect(atlas, region_index, x, y, w, h, color_packed, aff);
        return;
    }

    float positions[1 + (4 * (NT_UI_CORNER_SEGMENTS + 1))][2];
    uint16_t indices[4 * (NT_UI_CORNER_SEGMENTS + 1) * 3];

    positions[0][0] = x + half_w;
    positions[0][1] = y + half_h;
    uint32_t vi = 1;

    /* LUT row per corner: TL=2, TR=3, BR=0, BL=1. */
    if (tl == 0.0F) {
        positions[vi][0] = x;
        positions[vi][1] = y;
        vi++;
    } else {
        const float cx = x + tl;
        const float cy = y + tl;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (tl * s_arc_lut[2][s].cos);
            positions[vi][1] = cy + (tl * s_arc_lut[2][s].sin);
            vi++;
        }
    }
    if (tr == 0.0F) {
        positions[vi][0] = x + w;
        positions[vi][1] = y;
        vi++;
    } else {
        const float cx = x + w - tr;
        const float cy = y + tr;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (tr * s_arc_lut[3][s].cos);
            positions[vi][1] = cy + (tr * s_arc_lut[3][s].sin);
            vi++;
        }
    }
    if (br == 0.0F) {
        positions[vi][0] = x + w;
        positions[vi][1] = y + h;
        vi++;
    } else {
        const float cx = x + w - br;
        const float cy = y + h - br;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (br * s_arc_lut[0][s].cos);
            positions[vi][1] = cy + (br * s_arc_lut[0][s].sin);
            vi++;
        }
    }
    if (bl == 0.0F) {
        positions[vi][0] = x;
        positions[vi][1] = y + h;
        vi++;
    } else {
        const float cx = x + bl;
        const float cy = y + h - bl;
        for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
            positions[vi][0] = cx + (bl * s_arc_lut[1][s].cos);
            positions[vi][1] = cy + (bl * s_arc_lut[1][s].sin);
            vi++;
        }
    }

    /* Triangle fan (center=0, i, i+1), wrap last to 1. */
    uint32_t ii = 0;
    for (uint32_t i = 1; i < vi; i++) {
        const uint16_t next = (uint16_t)((i + 1 < vi) ? (i + 1) : 1);
        indices[ii++] = 0U;
        indices[ii++] = (uint16_t)i;
        indices[ii++] = next;
    }

    float mat[16];
    build_affine_mat4(aff, mat);
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, mat, color_packed);
}
// #endregion

// #region helper_emit_border
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_square_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, uint32_t col, const float aff[6]) {
    // #region axis-aligned-fast-path
    const float top = (float)widths.top;
    const float bot = (float)widths.bottom;
    const float lft = (float)widths.left;
    const float rgt = (float)widths.right;
    /* Axis-aligned fast path: per-edge rect emit avoids the geometry mesh. */
    const bool axis_aligned = (aff[1] == 0.0F && aff[2] == 0.0F);
    if (axis_aligned) {
        if (widths.top) {
            emit_screen_rect(atlas, region_index, bb.x, bb.y, bb.width, top, col, aff);
        }
        if (widths.bottom) {
            emit_screen_rect(atlas, region_index, bb.x, bb.y + bb.height - bot, bb.width, bot, col, aff);
        }
        if (widths.left) {
            emit_screen_rect(atlas, region_index, bb.x, bb.y + top, lft, bb.height - top - bot, col, aff);
        }
        if (widths.right) {
            emit_screen_rect(atlas, region_index, bb.x + bb.width - rgt, bb.y + top, rgt, bb.height - top - bot, col, aff);
        }
        return;
    }
    // #endregion
    // #region mesh-build
    /* Build 4 quads as one geometry mesh through the affine. */
    float positions[16][2];
    uint16_t indices[24];
    uint32_t vi = 0;
    uint32_t ii = 0;
    if (widths.top) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + top;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (widths.bottom) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + bb.height;
        vi++;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + bb.height;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (widths.left) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + lft;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + lft;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (widths.right) {
        const uint16_t b0 = (uint16_t)vi;
        positions[vi][0] = bb.x + bb.width - rgt;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + top;
        vi++;
        positions[vi][0] = bb.x + bb.width;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        positions[vi][0] = bb.x + bb.width - rgt;
        positions[vi][1] = bb.y + bb.height - bot;
        vi++;
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 1);
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = b0;
        indices[ii++] = (uint16_t)(b0 + 2);
        indices[ii++] = (uint16_t)(b0 + 3);
    }
    if (vi == 0) {
        return;
    }
    float mat[16];
    build_affine_mat4(aff, mat);
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, mat, col);
    // #endregion
}

static uint32_t emit_corner_strip_pairs(float (*pos)[2], uint32_t vi, float radius, float cx, float cy, float w_perp_x, float w_perp_y, float sharp_x, float sharp_y, float sign_x, float sign_y,
                                        uint32_t quadrant) {
    if (radius == 0.0F) {
        pos[vi][0] = sharp_x;
        pos[vi][1] = sharp_y;
        vi++;
        pos[vi][0] = sharp_x + (sign_x * w_perp_x);
        pos[vi][1] = sharp_y + (sign_y * w_perp_y);
        vi++;
        return vi;
    }
    /* width > radius -> inner curve collapses to 0 on that axis (CSS parity). */
    const float irx = (radius > w_perp_x) ? (radius - w_perp_x) : 0.0F;
    const float iry = (radius > w_perp_y) ? (radius - w_perp_y) : 0.0F;
    for (uint32_t s = 0; s <= NT_UI_CORNER_SEGMENTS; s++) {
        const float cc = s_arc_lut[quadrant][s].cos;
        const float ss = s_arc_lut[quadrant][s].sin;
        pos[vi][0] = cx + (radius * cc);
        pos[vi][1] = cy + (radius * ss);
        vi++;
        pos[vi][0] = cx + (irx * cc);
        pos[vi][1] = cy + (iry * ss);
        vi++;
    }
    return vi;
}

/* Caller (emit_border) clamps radii and guarantees at least one is non-zero. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_rounded_border(nt_resource_t atlas, uint32_t region_index, Clay_BoundingBox bb, Clay_BorderWidth widths, float tl, float tr, float bl, float br, uint32_t color_packed,
                                const float aff[6]) {
    const float x = bb.x;
    const float y = bb.y;
    const float w = bb.width;
    const float h = bb.height;
    const float top = (float)widths.top;
    const float bot = (float)widths.bottom;
    const float lft = (float)widths.left;
    const float rgt = (float)widths.right;

    float positions[4 * (NT_UI_CORNER_SEGMENTS + 1) * 2][2];
    uint32_t vi = 0;
    vi = emit_corner_strip_pairs(positions, vi, tl, x + tl, y + tl, lft, top, x, y, 1.0F, 1.0F, 2U);
    vi = emit_corner_strip_pairs(positions, vi, tr, x + w - tr, y + tr, rgt, top, x + w, y, -1.0F, 1.0F, 3U);
    vi = emit_corner_strip_pairs(positions, vi, br, x + w - br, y + h - br, rgt, bot, x + w, y + h, -1.0F, -1.0F, 0U);
    vi = emit_corner_strip_pairs(positions, vi, bl, x + bl, y + h - bl, lft, bot, x, y + h, 1.0F, -1.0F, 1U);

    /* Triangle strip with wrap: pair k at (outer=2k, inner=2k+1). */
    const uint32_t pair_count = vi / 2;
    uint16_t indices[4 * (NT_UI_CORNER_SEGMENTS + 1) * 6];
    uint32_t ii = 0;
    for (uint32_t k = 0; k < pair_count; k++) {
        const uint32_t k_next = (k + 1 < pair_count) ? (k + 1) : 0;
        const uint16_t out_k = (uint16_t)(2 * k);
        const uint16_t in_k = (uint16_t)((2 * k) + 1);
        const uint16_t out_n = (uint16_t)(2 * k_next);
        const uint16_t in_n = (uint16_t)((2 * k_next) + 1);
        indices[ii++] = out_k;
        indices[ii++] = in_k;
        indices[ii++] = out_n;
        indices[ii++] = in_k;
        indices[ii++] = in_n;
        indices[ii++] = out_n;
    }

    float mat[16];
    build_affine_mat4(aff, mat);
    nt_sprite_renderer_emit_geometry(atlas, region_index, positions, vi, indices, ii, mat, color_packed);
}

static void emit_border(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, const float aff[6]) {
    const Clay_BorderRenderData *b = &c->renderData.border;
    const Clay_BoundingBox bb = c->boundingBox;
    const float top = (float)b->width.top;
    const float bot = (float)b->width.bottom;
    const float lft = (float)b->width.left;
    const float rgt = (float)b->width.right;
    NT_ASSERT(top + bot <= bb.height && "nt_ui BORDER: top+bottom widths exceed bbox.height");
    NT_ASSERT(lft + rgt <= bb.width && "nt_ui BORDER: left+right widths exceed bbox.width");

    const uint32_t col = nt_color_pack_clay(b->color);
    float tl = b->cornerRadius.topLeft;
    float tr = b->cornerRadius.topRight;
    float bl = b->cornerRadius.bottomLeft;
    float br = b->cornerRadius.bottomRight;
    clamp_radii_css3(bb.width, bb.height, &tl, &tr, &bl, &br);

    if (tl == 0.0F && tr == 0.0F && bl == 0.0F && br == 0.0F) {
        emit_square_border(ctx->atlas, ctx->white_region, bb, b->width, col, aff);
        return;
    }
    emit_rounded_border(ctx->atlas, ctx->white_region, bb, b->width, tl, tr, bl, br, col, aff);
}
// #endregion

// #region helper_emit_image
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_image(const Clay_RenderCommand *c, const float aff[6]) {
    const nt_ui_image_payload_t *p = (const nt_ui_image_payload_t *)c->renderData.image.imageData;
    NT_ASSERT(p != NULL && "nt_ui IMAGE: imageData must point to nt_ui_image_payload_t");
    NT_ASSERT(p->atlas.id != 0 && "nt_ui IMAGE payload: invalid atlas handle");
    const Clay_CornerRadius cr = c->renderData.image.cornerRadius;
    NT_ASSERT(cr.topLeft == 0.0F && cr.topRight == 0.0F && cr.bottomLeft == 0.0F && cr.bottomRight == 0.0F && "nt_ui IMAGE: cornerRadius unsupported; pre-bake into atlas");
    if (!nt_resource_is_ready(p->atlas)) {
        return;
    }

    const Clay_BoundingBox bb = c->boundingBox;

    /* Clay {0,0,0,0} backgroundColor means "untinted", not transparent. */
    Clay_Color tint = c->renderData.image.backgroundColor;
    const bool default_untinted = (tint.r == 0.0F && tint.g == 0.0F && tint.b == 0.0F && tint.a == 0.0F);
    const uint32_t col = default_untinted ? 0xFFFFFFFFU : nt_color_pack_clay(tint);

    const nt_texture_region_t *r = nt_atlas_get_region(p->atlas, p->region_index);
    if (r->vertex_count == 0U) {
        return;
    }

    /* Flag bit OR non-zero lrtb selects override; flag allows override with zeros (disable slice9). */
    const bool has_s9_override = (p->flags & NT_UI_IMAGE_SLICE9_OVERRIDE) || (p->slice9_override[0] | p->slice9_override[1] | p->slice9_override[2] | p->slice9_override[3]) != 0;
    const bool region_slice9 = (r->slice9_lrtb[0] | r->slice9_lrtb[1] | r->slice9_lrtb[2] | r->slice9_lrtb[3]) != 0;
    NT_ASSERT(isfinite(p->slice9_scale) && p->slice9_scale > 0.0F && "nt_ui walker: payload.slice9_scale must be finite > 0");
    const float s9_scale = p->slice9_scale;

    if (has_s9_override || region_slice9) {
        float m[16];
        build_affine_mat4(aff, m);
        const uint16_t *src = has_s9_override ? p->slice9_override : NULL;
        nt_sprite_renderer_emit_slice9(p->atlas, p->region_index, bb.x, bb.y, bb.width, bb.height, src, s9_scale, col, p->flip_bits, m);
        return;
    }

    const float ipu = nt_atlas_get_inverse_pixels_per_unit(p->atlas);
    const float src_w = (float)r->source_w * ipu;
    const float src_h = (float)r->source_h * ipu;
    NT_ASSERT(src_w > 0.0F && src_h > 0.0F && "nt_ui IMAGE: atlas region has zero source dimensions (broken atlas data)");
    const float sx_f = bb.width / src_w;
    const float sy_f = bb.height / src_h;

    /* Source's (origin, origin) point anchors at bbox center. */
    const float cx = bb.x + (bb.width * 0.5F);
    const float cy = bb.y + (bb.height * 0.5F);
    const float m[16] = {
        aff[0] * sx_f, aff[2] * sx_f, 0.0F, 0.0F, -aff[1] * sy_f, -aff[3] * sy_f, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, (aff[0] * cx) + (aff[1] * cy) + aff[4], (aff[2] * cx) + (aff[3] * cy) + aff[5],
        0.0F,          1.0F,
    };
    const float origin_x = (p->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) ? p->origin_x : r->origin_x;
    const float origin_y = (p->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) ? p->origin_y : r->origin_y;
    nt_sprite_renderer_emit_region(p->atlas, p->region_index, m, origin_x, origin_y, col, p->flip_bits);
}
// #endregion

// #region helper_emit_text
/* dispatch_command flushes sprite before and lazy-rebinds after. */
static void emit_text(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, float text_scale, const float aff[6]) {
    const Clay_TextRenderData *t = &c->renderData.text;
    NT_ASSERT((uint32_t)t->fontId < NT_UI_MAX_FONTS && "nt_ui TEXT: fontId >= NT_UI_MAX_FONTS");
    nt_font_t font = ctx->fonts[t->fontId];
    NT_ASSERT(nt_font_valid(font) && "nt_ui TEXT: font slot empty; call nt_ui_set_font first");

    nt_text_renderer_set_font(font);
    nt_text_renderer_set_material(ctx->text_material);

    const float font_size = (float)t->fontSize * text_scale;
    nt_font_metrics_t metrics = nt_font_get_metrics(font);
    const float scale = (metrics.units_per_em > 0) ? (font_size / (float)metrics.units_per_em) : 0.0F;
    const float text_h = (float)(metrics.ascent - metrics.descent) * scale;
    const float center_offset = (c->boundingBox.height - text_h) * 0.5F;
    /* Y-down: baseline = bbox.y(top) + center_offset + ascent*scale. */
    const float baseline_y = c->boundingBox.y + center_offset + ((float)metrics.ascent * scale);

    /* Text renderer local is Y-up; pre-flip Y to cancel world_aff's bake. */
    const float ox = c->boundingBox.x;
    const float oy = baseline_y;
    const float m[16] = {
        aff[0], aff[2], 0.0F, 0.0F, -aff[1], -aff[3], 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, (aff[0] * ox) + (aff[1] * oy) + aff[4], (aff[2] * ox) + (aff[3] * oy) + aff[5], 0.0F, 1.0F,
    };
    const float color[4] = {
        t->textColor.r / 255.0F,
        t->textColor.g / 255.0F,
        t->textColor.b / 255.0F,
        t->textColor.a / 255.0F,
    };
    nt_text_renderer_draw_n(t->stringContents.chars, (size_t)t->stringContents.length, m, font_size, color, (float)t->letterSpacing * text_scale, (float)t->lineHeight * text_scale);
}
// #endregion

// #region helper_scissor_stack
typedef struct {
    int x;
    int y;
    int w;
    int h;
} scissor_rect_t;

/* DIRECT mode: viewport is GL physical, Y-flip inside the viewport rect.
 * SCALED mode: viewport is logical; scale+shift to physical, Y-flip against fb height. */
void nt_ui_internal_apply_scissor_logical_to_physical(const nt_ui_target_t *target, int x, int y, int wp, int hp) {
    const float vx = target->viewport[0];
    const float vy = target->viewport[1];
    const float vw = target->viewport[2];
    const float vh = target->viewport[3];

    if (target->fb_size[0] <= 0.0F || target->fb_size[1] <= 0.0F) {
        nt_gfx_set_scissor((int)vx + x, (int)(vy + vh) - y - hp, wp, hp);
        return;
    }

    const float ox = target->fb_offset[0];
    const float oy = target->fb_offset[1];
    const float fbh = target->fb_size[1];
    const float sx = (vw > 0.0F) ? ((target->fb_size[0] - (2.0F * ox)) / vw) : 1.0F;
    const float sy = (vh > 0.0F) ? ((fbh - (2.0F * oy)) / vh) : 1.0F;
    const int phys_x = (int)floorf(ox + (sx * (vx + (float)x)));
    const int phys_y_top = (int)floorf(oy + (sy * (vy + (float)y)));
    const int phys_w = (int)ceilf(sx * (float)wp);
    const int phys_h = (int)ceilf(sy * (float)hp);
    const int phys_y_gl = (int)fbh - phys_y_top - phys_h;
    nt_gfx_set_scissor(phys_x, phys_y_gl, phys_w, phys_h);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scissor_push(const Clay_RenderCommand *c, scissor_rect_t *stack, int *depth, const nt_ui_target_t *target, bool *sprite_pipeline_dirty) {
    NT_ASSERT((uint32_t)*depth < NT_UI_WALKER_SCISSOR_DEPTH_CAP && "scissor stack overflow; restructure nested clip");
    /* Fail-closed in OFF builds — assert vanishes; the stack[(*depth)++] below would corrupt memory. */
    if ((uint32_t)*depth >= NT_UI_WALKER_SCISSOR_DEPTH_CAP) {
        return;
    }

    /* Both-axes-false is reserved for Clay's floating clipTo=ATTACHED_PARENT marker;
     * user code must always set at least one axis true (asserted below). */
    const Clay_ClipRenderData *clip = &c->renderData.clip;
    const bool both_false = !clip->horizontal && !clip->vertical;
    NT_ASSERT((!both_false || (c->boundingBox.width > 0.0F && c->boundingBox.height > 0.0F)) &&
              "nt_ui SCISSOR_START with both axes false requires non-empty bbox (reserved for Clay floating clipTo marker)");
    const int vw = (int)target->viewport[2];
    const int vh = (int)target->viewport[3];
    const bool clip_h = clip->horizontal || both_false;
    const bool clip_v = clip->vertical || both_false;
    int x;
    int y;
    int wp;
    int hp;
    if (clip_h) {
        const float bx = c->boundingBox.x;
        x = (int)floorf(bx);
        wp = (int)ceilf(bx + c->boundingBox.width) - x;
    } else {
        x = 0;
        wp = vw;
    }
    if (clip_v) {
        const float by = c->boundingBox.y;
        y = (int)floorf(by);
        hp = (int)ceilf(by + c->boundingBox.height) - y;
    } else {
        y = 0;
        hp = vh;
    }

    /* Intersect with parent so inner widgets cannot escape outer clip. */
    if (*depth > 0) {
        scissor_rect_t t = stack[*depth - 1];
        int x2 = (x > t.x) ? x : t.x;
        int y2 = (y > t.y) ? y : t.y;
        int r2 = ((x + wp) < (t.x + t.w)) ? (x + wp) : (t.x + t.w);
        int b2 = ((y + hp) < (t.y + t.h)) ? (y + hp) : (t.y + t.h);
        x = x2;
        y = y2;
        wp = (r2 > x2) ? (r2 - x2) : 0;
        hp = (b2 > y2) ? (b2 - y2) : 0;
    }

    /* Flush BEFORE scissor switch so staging keeps prior clip. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    *sprite_pipeline_dirty = true;

    stack[(*depth)++] = (scissor_rect_t){.x = x, .y = y, .w = wp, .h = hp};

    nt_ui_internal_apply_scissor_logical_to_physical(target, x, y, wp, hp);
    nt_gfx_set_scissor_enabled(true);
}

static void scissor_pop(scissor_rect_t *stack, int *depth, const nt_ui_target_t *target, bool *sprite_pipeline_dirty) {
    NT_ASSERT(*depth > 0 && "scissor underflow");
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    *sprite_pipeline_dirty = true;
    (*depth)--;
    if (*depth == 0) {
        nt_gfx_set_scissor_enabled(false);
    } else {
        scissor_rect_t r = stack[*depth - 1];
        nt_ui_internal_apply_scissor_logical_to_physical(target, r.x, r.y, r.w, r.h);
    }
}
// #endregion

// #region walker_state
typedef struct {
    float aff_a, aff_b, aff_c, aff_d, aff_tx, aff_ty;
    float accum_opacity;
} nt_ui_walker_state_t;

static void walker_state_init(nt_ui_walker_state_t *ws) {
    ws->aff_a = 1.0F;
    ws->aff_b = 0;
    ws->aff_c = 0;
    ws->aff_d = 1.0F;
    ws->aff_tx = 0;
    ws->aff_ty = 0;
    ws->accum_opacity = 1.0F;
}

/* Apply accumulated opacity to a packed AABBGGRR color. */
static inline uint32_t apply_opacity(uint32_t color_packed, float opacity) {
    if (opacity >= 1.0F) {
        return color_packed;
    }
    uint32_t a = (color_packed >> 24) & 0xFFU;
    /* lrintf rounds-to-nearest so 0.5 * 255 → 128, not truncate 127. */
    a = (uint32_t)lrintf((float)a * opacity);
    if (a > 255U) {
        a = 255U;
    }
    return (color_packed & 0x00FFFFFFU) | (a << 24);
}

/* Per-walk counters passed to dispatch helpers. */
typedef struct {
    uint32_t rect_command_count;
    uint32_t image_command_count;
    uint32_t text_command_count;
    uint32_t border_command_count;
    uint32_t scissor_command_count;
    uint32_t max_scissor_depth;
} nt_ui_walk_counters_t;

/* type=NONE = engine anchor (skip silently); type=GAME = invoke handler. */
static void emit_custom(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, const float world_aff[6], float opacity, bool *sprite_pipeline_dirty) {
    const nt_ui_custom_data_t *cd = (const nt_ui_custom_data_t *)c->renderData.custom.customData;
    NT_ASSERT(cd != NULL && "CUSTOM command must have nt_ui_custom_data_t");
    if (cd->type == NT_UI_CUSTOM_TYPE_NONE) {
        return;
    }
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    *sprite_pipeline_dirty = true;
    if (ctx->custom_fn != NULL) {
        const nt_ui_custom_frame_t frame = {
            .clay_cmd = (const void *)c,
            .world_aff = {world_aff[0], world_aff[1], world_aff[2], world_aff[3], world_aff[4], world_aff[5]},
            .opacity = opacity,
        };
        ctx->custom_fn(&frame, ctx->custom_user);
    }
}
// #endregion

// #region walk
/* SCISSOR/CUSTOM/NONE = hard barriers; never reordered. */
static bool is_segmentable(Clay_RenderCommandType cmd_type) {
    switch (cmd_type) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
    case CLAY_RENDER_COMMAND_TYPE_BORDER:
    case CLAY_RENDER_COMMAND_TYPE_IMAGE:
    case CLAY_RENDER_COMMAND_TYPE_TEXT:
        return true;
    default:
        return false;
    }
}

/* Drain pending text, lazy-rebind sprite if a prior text/scissor/custom closed it. */
static inline void prep_sprite_dispatch(const nt_ui_context_t *ctx, bool *sprite_pipeline_dirty) {
    nt_text_renderer_flush();
    if (*sprite_pipeline_dirty) {
        nt_sprite_renderer_set_material(ctx->sprite_material);
        *sprite_pipeline_dirty = false;
    }
}

/* Y-flip lives inside world_aff (d → -d, ty → vy+vh-ty); every emit_* applies it uniformly. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void dispatch_command(const nt_ui_context_t *ctx, const Clay_RenderCommand *c, scissor_rect_t *scissor_stack, int *depth, const nt_ui_target_t *target, bool *sprite_pipeline_dirty,
                             nt_ui_walker_state_t *ws, nt_ui_walk_counters_t *counters) {
    const float vy = target->viewport[1];
    const float vh = target->viewport[3];

    const float world_aff[6] = {
        ws->aff_a, ws->aff_b, -ws->aff_c, -ws->aff_d, ws->aff_tx, vy + vh - ws->aff_ty,
    };
    /* X-column magnitude; Y dropped on purpose so glyph atlas stays crisp on the X axis. */
    const float text_scale = sqrtf((ws->aff_a * ws->aff_a) + (ws->aff_c * ws->aff_c));

    switch (c->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_NONE:
        return;
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
        counters->rect_command_count++;
        prep_sprite_dispatch(ctx, sprite_pipeline_dirty);
        const Clay_RectangleRenderData *r = &c->renderData.rectangle;
        uint32_t col = nt_color_pack_clay(r->backgroundColor);
        col = apply_opacity(col, ws->accum_opacity);
        emit_rounded_rect(ctx->atlas, ctx->white_region, c->boundingBox.x, c->boundingBox.y, c->boundingBox.width, c->boundingBox.height, r->cornerRadius, col, world_aff);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
        counters->border_command_count++;
        prep_sprite_dispatch(ctx, sprite_pipeline_dirty);
        Clay_RenderCommand local = *c;
        /* Round-to-nearest to match RECT's apply_opacity. */
        local.renderData.border.color.a = (float)lrintf(local.renderData.border.color.a * ws->accum_opacity);
        emit_border(ctx, &local, world_aff);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
        counters->text_command_count++;
        nt_sprite_renderer_flush();
        *sprite_pipeline_dirty = true;
        Clay_RenderCommand local = *c;
        /* Round-to-nearest to match RECT's apply_opacity. */
        local.renderData.text.textColor.a = (float)lrintf(local.renderData.text.textColor.a * ws->accum_opacity);
        emit_text(ctx, &local, text_scale, world_aff);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
        counters->image_command_count++;
        prep_sprite_dispatch(ctx, sprite_pipeline_dirty);
        Clay_RenderCommand local = *c;
        if (ws->accum_opacity < 1.0F) {
            Clay_Color tint = local.renderData.image.backgroundColor;
            const bool untinted = (tint.r == 0.0F && tint.g == 0.0F && tint.b == 0.0F && tint.a == 0.0F);
            /* Round-to-nearest to match RECT's apply_opacity; truncation would
             * give image/rect a 1-LSB alpha mismatch at equal accum_opacity. */
            if (untinted) {
                local.renderData.image.backgroundColor = (Clay_Color){.r = 255.0F, .g = 255.0F, .b = 255.0F, .a = (float)lrintf(255.0F * ws->accum_opacity)};
            } else {
                local.renderData.image.backgroundColor.a = (float)lrintf(local.renderData.image.backgroundColor.a * ws->accum_opacity);
            }
        }
        emit_image(&local, world_aff);
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
        counters->scissor_command_count++;
        Clay_RenderCommand local = *c;
        /* GL scissor is axis-aligned in fb space; rotated clip emits the
         * bounding-AABB of the 4 transformed corners. */
        const float bx = c->boundingBox.x;
        const float by = c->boundingBox.y;
        const float bw = c->boundingBox.width;
        const float bh = c->boundingBox.height;
        const float corners[4][2] = {{bx, by}, {bx + bw, by}, {bx + bw, by + bh}, {bx, by + bh}};
        float wx[4];
        float wy[4];
        for (int ci = 0; ci < 4; ++ci) {
            wx[ci] = (world_aff[0] * corners[ci][0]) + (world_aff[1] * corners[ci][1]) + world_aff[4];
            wy[ci] = (world_aff[2] * corners[ci][0]) + (world_aff[3] * corners[ci][1]) + world_aff[5];
        }
        float mn_x = wx[0];
        float mx_x = wx[0];
        float mn_y = wy[0];
        float mx_y = wy[0];
        for (int ci = 1; ci < 4; ++ci) {
            mn_x = fminf(mn_x, wx[ci]);
            mx_x = fmaxf(mx_x, wx[ci]);
            mn_y = fminf(mn_y, wy[ci]);
            mx_y = fmaxf(mx_y, wy[ci]);
        }
        /* scissor_push reads Clay-Y-down and flips internally — convert back. */
        const float clay_top_y = vy + vh - mx_y;
        local.boundingBox = (Clay_BoundingBox){.x = mn_x, .y = clay_top_y, .width = mx_x - mn_x, .height = mx_y - mn_y};
        scissor_push(&local, scissor_stack, depth, target, sprite_pipeline_dirty);
        if ((uint32_t)*depth > counters->max_scissor_depth) {
            counters->max_scissor_depth = (uint32_t)*depth;
        }
        return;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        scissor_pop(scissor_stack, depth, target, sprite_pipeline_dirty);
        return;
    case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
        /* Handler owns the transform math (LAYOUT bbox + world_aff + opacity passed through). */
        emit_custom(ctx, c, world_aff, ws->accum_opacity, sprite_pipeline_dirty);
        return;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    // #region preconditions
    NT_ASSERT(ctx != NULL && "nt_ui_walk: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_walk: target must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_walk: ctx is mid-frame (call nt_ui_end first)");
    NT_ASSERT(ctx->frozen_cmds.internalArray != NULL && "nt_ui_walk: frozen_cmds not populated (call nt_ui_end before walk)");
    NT_ASSERT(isfinite(target->viewport[0]) && isfinite(target->viewport[1]) && isfinite(target->viewport[2]) && isfinite(target->viewport[3]) && "nt_ui_walk: target->viewport must be finite");
    NT_ASSERT(target->viewport[0] >= 0.0F && target->viewport[1] >= 0.0F && "nt_ui_walk: target->viewport origin must be non-negative");
    NT_ASSERT(target->viewport[2] >= 0.0F && target->viewport[3] >= 0.0F && "nt_ui_walk: target->viewport (w,h) must be non-negative");
    // #endregion

    // #region entry-flush
    /* Walker owns GL scissor across the call; drain BEFORE early returns so leaked staging dies with the frame. */
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    nt_gfx_set_scissor_enabled(false);
    // #endregion

    // #region degenerate-early-out
    /* Zero viewport or degenerate fb (minimized tab, orientation change): no-op. */
    const bool scaled = target->fb_size[0] > 0.0F;
    if (target->viewport[2] == 0.0F || target->viewport[3] == 0.0F || (scaled && target->fb_size[1] == 0.0F)) {
        ctx->last_walk_draw_call_delta = 0;
        ctx->last_walk_command_count = 0;
        ctx->last_walk_ms = 0.0F;
        ctx->last_walk_rect_command_count = 0;
        ctx->last_walk_image_command_count = 0;
        ctx->last_walk_text_command_count = 0;
        ctx->last_walk_border_command_count = 0;
        ctx->last_walk_scissor_command_count = 0;
        ctx->last_walk_max_scissor_depth = 0;
#ifdef NT_TEST_ACCESS
        ctx->test_last_walk_unlayered_count = 0;
#endif
#if NT_UI_DEBUG_TOOLS
        nt_ui_internal_inspector_strings_release(ctx);
#endif
        return;
    }
    // #endregion

    NT_ASSERT(ctx->sprite_material.id != 0 && "nt_ui_set_sprite_material(ctx,...) required before nt_ui_walk");
    NT_ASSERT(ctx->text_material.id != 0 && "nt_ui_set_text_material(ctx,...) required before nt_ui_walk");

    // #region atlas-not-ready-early-out
    /* Async-friendly: skip walk silently if atlas not yet bound or still loading. */
    if (ctx->atlas.id == 0 || !nt_resource_is_ready(ctx->atlas)) {
        ctx->last_walk_draw_call_delta = 0;
        ctx->last_walk_command_count = 0;
        ctx->last_walk_ms = 0.0F;
        ctx->last_walk_rect_command_count = 0;
        ctx->last_walk_image_command_count = 0;
        ctx->last_walk_text_command_count = 0;
        ctx->last_walk_border_command_count = 0;
        ctx->last_walk_scissor_command_count = 0;
        ctx->last_walk_max_scissor_depth = 0;
#ifdef NT_TEST_ACCESS
        ctx->test_last_walk_unlayered_count = 0;
#endif
#if NT_UI_DEBUG_TOOLS
        nt_ui_internal_inspector_strings_release(ctx);
#endif
        return;
    }
    // #endregion

    // #region walker-state-init
    /* After entry flush so walk_ms excludes draining the caller's pending geometry. */
    const double walk_t0 = nt_time_now();

    scissor_rect_t scissor_stack[NT_UI_WALKER_SCISSOR_DEPTH_CAP];
    int depth = 0;

    nt_ui_walker_state_t ws;
    walker_state_init(&ws);

    nt_ui_walk_counters_t counters = {0};

    /* AFTER entry flush so per-walk delta excludes caller's drained geometry. */
    const uint32_t calls_at_entry = nt_gfx_get_frame_draw_calls();
    // #endregion

    // #region viewport-bind
    /* glViewport needs PHYSICAL pixels. */
    if (scaled) {
        /* Derive width from int offset to avoid rounding asymmetry (1px bar). */
        const int ox = (int)roundf(target->fb_offset[0]);
        const int oy = (int)roundf(target->fb_offset[1]);
        nt_gfx_set_viewport(ox, oy, (int)target->fb_size[0] - (2 * ox), (int)target->fb_size[1] - (2 * oy));
    } else {
        nt_gfx_set_viewport((int)target->viewport[0], (int)target->viewport[1], (int)target->viewport[2], (int)target->viewport[3]);
    }

    /* Sprite material up-front; text binds lazily inside emit_text. */
    nt_sprite_renderer_set_material(ctx->sprite_material);

    bool sprite_pipeline_dirty = false;
    // #endregion

    // #region segment-scan + layer-dispatch
    /* Bitmask layer dispatch + ctz: O(L_active × N) per segment, 32 B stack vs ~2 KB counting sort. */
    const Clay_RenderCommandArray *arr = &ctx->frozen_cmds;
    const int32_t N_elements = nt_ui_clay_priv_layout_elements_length(ctx->clay);
#ifdef NT_TEST_ACCESS
    uint32_t unlayered_count = 0U;
#endif
    int32_t i = 0;
    while (i < arr->length) {
        const Clay_RenderCommand *c = &arr->internalArray[i];
        if (!is_segmentable(c->commandType)) {
            const nt_ui_baked_xform_t b = (c->nt_layout_index < 0 || c->nt_layout_index >= N_elements) ? nt_ui_internal_identity_baked() : ctx->tree_baked[c->nt_layout_index];
            ws.aff_a = b.a;
            ws.aff_b = b.b;
            ws.aff_c = b.c;
            ws.aff_d = b.d;
            ws.aff_tx = b.tx;
            ws.aff_ty = b.ty;
            ws.accum_opacity = b.opacity;
            dispatch_command(ctx, c, scissor_stack, &depth, target, &sprite_pipeline_dirty, &ws, &counters);
            ++i;
            continue;
        }
        const int16_t seg_z = c->zIndex;
        int32_t seg_end = i + 1;
        while (seg_end < arr->length) {
            const Clay_RenderCommand *next = &arr->internalArray[seg_end];
            if (next->zIndex != seg_z || !is_segmentable(next->commandType)) {
                break;
            }
            ++seg_end;
        }

        /* Active-layer bitmask scan over this segment. */
        uint32_t active_layers[8] = {0U};
        for (int32_t j = i; j < seg_end; ++j) {
            const Clay_RenderCommand *cc = &arr->internalArray[j];
            const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
            active_layers[layer >> 5U] |= (1U << (layer & 31U));
#ifdef NT_TEST_ACCESS
            if (cc->userData == NULL && cc->commandType != CLAY_RENDER_COMMAND_TYPE_CUSTOM) {
                ++unlayered_count;
            }
#endif
        }

        /* Layer passes ascending; counting-sort buys little when L_active < 10 in real UIs. */
        for (uint32_t word_idx = 0U; word_idx < 8U; ++word_idx) {
            uint32_t mask = active_layers[word_idx];
            while (mask != 0U) {
                const uint32_t bit_idx = nt_ctz32(mask);
                mask &= mask - 1U;
                const uint8_t current_layer = (uint8_t)((word_idx << 5U) | bit_idx);
                for (int32_t j = i; j < seg_end; ++j) {
                    const Clay_RenderCommand *cc = &arr->internalArray[j];
                    const uint8_t layer = cc->userData ? ((const nt_ui_element_data_t *)cc->userData)->layer : 0U;
                    if (layer == current_layer) {
                        const nt_ui_baked_xform_t b = (cc->nt_layout_index < 0 || cc->nt_layout_index >= N_elements) ? nt_ui_internal_identity_baked() : ctx->tree_baked[cc->nt_layout_index];
                        ws.aff_a = b.a;
                        ws.aff_b = b.b;
                        ws.aff_c = b.c;
                        ws.aff_d = b.d;
                        ws.aff_tx = b.tx;
                        ws.aff_ty = b.ty;
                        ws.accum_opacity = b.opacity;
                        dispatch_command(ctx, cc, scissor_stack, &depth, target, &sprite_pipeline_dirty, &ws, &counters);
                    }
                }
            }
        }
        i = seg_end;
    }
    // #endregion

    // #region exit-flush + metrics
    nt_sprite_renderer_flush();
    nt_text_renderer_flush();
    NT_ASSERT(depth == 0 && "unbalanced scissor stack at walk exit");
    nt_gfx_set_scissor_enabled(false);

    /* Guard against a CUSTOM handler resetting the gfx counter → unsigned wrap. */
    const uint32_t calls_after = nt_gfx_get_frame_draw_calls();
    NT_ASSERT(calls_after >= calls_at_entry && "nt_ui_walk: frame draw-call counter went backwards");
    ctx->last_walk_draw_call_delta = calls_after - calls_at_entry;
    ctx->last_walk_command_count = (uint32_t)arr->length;
    ctx->last_walk_rect_command_count = counters.rect_command_count;
    ctx->last_walk_image_command_count = counters.image_command_count;
    ctx->last_walk_text_command_count = counters.text_command_count;
    ctx->last_walk_border_command_count = counters.border_command_count;
    ctx->last_walk_scissor_command_count = counters.scissor_command_count;
    ctx->last_walk_max_scissor_depth = counters.max_scissor_depth;
    ctx->last_walk_ms = (float)((nt_time_now() - walk_t0) * 1000.0);
#ifdef NT_TEST_ACCESS
    ctx->test_last_walk_unlayered_count = unlayered_count;
#endif
#if NT_UI_DEBUG_TOOLS
    /* Inspector strings backed by module-level rings are now consumed; release ownership. */
    nt_ui_internal_inspector_strings_release(ctx);
#endif
    // #endregion
}
// #endregion

// #region setters
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_set_atlas_white_region(nt_ui_context_t *ctx, nt_resource_t atlas, uint32_t white_region_idx) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_atlas_white_region: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_atlas_white_region: must be called outside begin/end");
    NT_ASSERT(atlas.id != 0 && "nt_ui_set_atlas_white_region: invalid atlas handle");
    NT_ASSERT(nt_resource_is_ready(atlas) && "nt_ui_set_atlas_white_region: atlas must be READY");
    const nt_texture_region_t *r = nt_atlas_get_region(atlas, white_region_idx);
    NT_ASSERT(r->vertex_count > 0U && "nt_ui_set_atlas_white_region: white region tombstoned");
    /* mat4(w,h) needs cached_pos {0,1}x{0,1}: 1x1 source AND PPU=1. */
    NT_ASSERT(r->source_w == 1 && r->source_h == 1 && "nt_ui_set_atlas_white_region: white region must be 1x1 source");
    /* mat4(w,h) needs cached_pos {0,1}x{0,1}: 1x1 source AND PPU=1. */
    NT_ASSERT(nt_atlas_get_inverse_pixels_per_unit(atlas) == 1.0F && "nt_ui_set_atlas_white_region: atlas must have PPU=1");
    ctx->atlas = atlas;
    ctx->white_region = white_region_idx;
}

void nt_ui_set_sprite_material(nt_ui_context_t *ctx, nt_material_t sprite_material) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_sprite_material: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_sprite_material: must be called outside begin/end");
    NT_ASSERT(sprite_material.id != 0 && "nt_ui_set_sprite_material: invalid material handle");
    ctx->sprite_material = sprite_material;
}

void nt_ui_set_text_material(nt_ui_context_t *ctx, nt_material_t text_material) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_text_material: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_text_material: must be called outside begin/end");
    NT_ASSERT(text_material.id != 0 && "nt_ui_set_text_material: invalid material handle");
    ctx->text_material = text_material;
}

void nt_ui_set_custom_handler(nt_ui_context_t *ctx, nt_ui_custom_handler_t fn, void *userdata) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_custom_handler: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_set_custom_handler: must be called outside begin/end");
    ctx->custom_fn = fn;
    ctx->custom_user = userdata;
}
// #endregion

// #region nt_ui_custom
void nt_ui_custom(nt_ui_context_t *ctx, const nt_ui_element_data_t *elem_data, void *data) {
    NT_ASSERT(ctx != NULL);
    NT_ASSERT(ctx->in_frame);
    nt_ui_custom_data_t *cd = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_data_t);
    NT_ASSERT(cd != NULL);
    *cd = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_GAME, .data = data};
    CLAY({
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
        .custom = {.customData = cd},
        .userData = (void *)elem_data,
    });
}
// #endregion

// #region interaction_id_bbox_hittest
uint32_t nt_ui_id(const char *s) {
    NT_ASSERT(s != NULL && "nt_ui_id: string must be non-NULL");
    /* Must use Clay's hash — a different one would miss Clay's hashmap. */
    return Clay_GetElementId((Clay_String){.length = (int32_t)strlen(s), .chars = s}).id;
}

nt_ui_bbox_t nt_ui_get_bbox(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_bbox: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_get_bbox: id must be non-zero (0 = no widget)");
    /* Same staleness contract as ui_hit_test (hit_generation rejects ids not re-declared). */
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    Clay_SetCurrentContext(saved);
    if (!d.found) {
        return (nt_ui_bbox_t){0};
    }
    const int32_t slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, id);
    if (slot < 0 || slot >= (int32_t)ctx->max_elements || ctx->hit_generation[slot] != ctx->current_generation) {
        return (nt_ui_bbox_t){0};
    }
    return (nt_ui_bbox_t){.x = d.boundingBox.x, .y = d.boundingBox.y, .width = d.boundingBox.width, .height = d.boundingBox.height, .found = true};
}

/* Hit-test reads PREV-frame data via Clay's persistent hashmap slot (stable across frames). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool hit_clip_chain(const nt_ui_context_t *ctx, uint32_t start_clip_id, int32_t N, float px, float py) {
    uint32_t cur_id = start_clip_id;
    /* Cap iterations to scissor stack depth so a malformed parent_id cycle can't hang.
     * Decrement + bail are UNCONDITIONAL (NT_ASSERT vanishes in OFF builds). */
    uint32_t guard = NT_UI_WALKER_SCISSOR_DEPTH_CAP;
    while (cur_id != 0U) {
        NT_ASSERT(guard > 0U && "hit_clip_chain: parent chain exceeded scissor depth cap (cycle or runaway nesting)");
        if (guard == 0U) {
            return false;
        }
        guard--;
        float cx;
        float cy;
        float cw;
        float ch;
        /* Fail-closed on invariant violation rather than letting input through. */
        if (!nt_ui_clay_priv_bbox_for_id(ctx->clay, cur_id, &cx, &cy, &cw, &ch)) {
            NT_ASSERT(false && "hit_clip_chain: clip ancestor missing from Clay hashmap");
            return false;
        }
        const int32_t cur_slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, cur_id);
        if (cur_slot < 0 || cur_slot >= N) {
            NT_ASSERT(false && "hit_clip_chain: clip ancestor slot OOB");
            return false;
        }
        const nt_ui_baked_xform_t cb = ctx->hit_baked[cur_slot];
        const float cdet = (cb.a * cb.d) - (cb.b * cb.c);
        NT_ASSERT(cdet != 0.0F && "ui_hit_test: clip ancestor has singular affine");
        const float cinv_a = cb.d / cdet;
        const float cinv_b = -cb.b / cdet;
        const float cinv_c = -cb.c / cdet;
        const float cinv_d = cb.a / cdet;
        const float crx = px - cb.tx;
        const float cry = py - cb.ty;
        const float clx = (cinv_a * crx) + (cinv_b * cry);
        const float cly = (cinv_c * crx) + (cinv_d * cry);
        if (clx < cx || clx > cx + cw || cly < cy || cly > cy + ch) {
            return false;
        }
        cur_id = ctx->hit_clip_parent_id[cur_slot];
    }
    return true;
}

static bool ui_hit_test(const nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]) {
    if (id == 0U) {
        return false;
    }
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return false;
    }
    const int32_t slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, id);
    if (slot < 0 || slot >= (int32_t)ctx->max_elements) {
        return false;
    }
    /* Reject ids Clay still holds in hashmap but that weren't re-declared this frame. */
    if (ctx->hit_generation[slot] != ctx->current_generation) {
        return false;
    }

    if (!hit_clip_chain(ctx, ctx->hit_clip_parent_id[slot], (int32_t)ctx->max_elements, px, py)) {
        return false;
    }

    const Clay_BoundingBox box = d.boundingBox;
    const nt_ui_baked_xform_t b = ctx->hit_baked[slot];
    const float det = (b.a * b.d) - (b.b * b.c);
    NT_ASSERT(det != 0.0F && "ui_hit_test: element has singular affine");
    const float inv_a = b.d / det;
    const float inv_b = -b.b / det;
    const float inv_c = -b.c / det;
    const float inv_d = b.a / det;
    const float rx = px - b.tx;
    const float ry = py - b.ty;
    const float lx = (inv_a * rx) + (inv_b * ry);
    const float ly = (inv_c * rx) + (inv_d * ry);

    const float pl = (pad_lrtb != NULL) ? (float)pad_lrtb[0] : 0.0F;
    const float pr = (pad_lrtb != NULL) ? (float)pad_lrtb[1] : 0.0F;
    const float pt = (pad_lrtb != NULL) ? (float)pad_lrtb[2] : 0.0F;
    const float pb = (pad_lrtb != NULL) ? (float)pad_lrtb[3] : 0.0F;
    return (lx >= box.x - pl) && (lx <= box.x + box.width + pr) && (ly >= box.y - pt) && (ly <= box.y + box.height + pb);
}

/* Compute-pure: same returned struct N calls per frame. The only side effect
 * is an idempotent OR of pointer_over_any (observability for nt_ui_wants_pointer);
 * state-machine writes (capture, button edges) live in step_interaction_padded. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_interaction_t nt_ui_query_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_query_interaction_padded: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_query_interaction_padded: id must be non-zero (0 = no widget)");
    NT_ASSERT(ctx->frame_pointer_count > 0U && "nt_ui_query_interaction_padded: no frame pointer snapshot yet (call after first nt_ui_begin)");
    /* Negative padding is a use error — size the widget smaller instead. */
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_query_interaction_padded: pad_lrtb components must be >= 0");

    nt_ui_interaction_t out = {0};

#if NT_UI_DEBUG_TOOLS
    /* Sidebar consumes the pointer; widgets behind it report no interaction. */
    if (ctx->inspector_active && ctx->inspector_pointer_consumed) {
        return out;
    }
#endif

    /* Multi-ctx safety: caller may be outside frame or have a different Clay ctx current. */
    Clay_Context *saved_clay = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        Clay_SetCurrentContext(saved_clay);
        return out;
    }

    const uint32_t pidx = 0U;
    const nt_pointer_t *p = &ctx->frame_pointers[pidx];
    const nt_ui_capture_t *cap = &ctx->captures[pidx];
    const nt_button_state_t btn = p->buttons[NT_BUTTON_LEFT];

    /* Exclusive capture: another widget holding the pointer hides this one from input. */
    const bool exclusive_gated = (cap->active_id != 0U) && (cap->active_id != id);

    bool over = false;
    if (!exclusive_gated) {
        over = ui_hit_test(ctx, id, p->x, p->y, pad_lrtb);
        out.hovered = over;
        if (over && btn.is_pressed) {
            out.pressed_now = true;
        }
        /* wants_pointer observability — idempotent OR write so read-only games
         * (query-only, no step) still gate world input correctly. */
        if (over) {
            ctx->pointer_over_any = true;
        }
    }

    /* "This id owns capture after step commits" — already, or pressed_now will assign it. */
    const bool mine = !exclusive_gated && ((cap->active_id == id) || out.pressed_now);
    if (mine) {
        out.pressed = btn.is_down;
        out.released_now = btn.is_released;
        /* clicked = release OVER widget; off-widget release cancels. */
        out.clicked = btn.is_released && over;
        out.pointer_id = p->id;
        if (out.pressed_now) {
            out.press_pos[0] = p->x;
            out.press_pos[1] = p->y;
        } else {
            out.press_pos[0] = cap->press_pos[0];
            out.press_pos[1] = cap->press_pos[1];
        }
        out.pos[0] = p->x;
        out.pos[1] = p->y;
        out.drag_dx = p->x - out.press_pos[0];
        out.drag_dy = p->y - out.press_pos[1];
    } else {
        out.press_pos[0] = p->x;
        out.press_pos[1] = p->y;
        out.pos[0] = p->x;
        out.pos[1] = p->y;
        out.pointer_id = p->id;
    }

    Clay_SetCurrentContext(saved_clay);
    return out;
}

nt_ui_interaction_t nt_ui_query_interaction(nt_ui_context_t *ctx, uint32_t id) { return nt_ui_query_interaction_padded(ctx, id, NULL); }

/* Same compute as query_padded plus state-machine commits; ONCE per widget per frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_interaction_t nt_ui_step_interaction_padded(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_step_interaction_padded: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == g_nt_ui_inframe_ctx && "nt_ui_step_interaction_padded: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_step_interaction_padded: id must be non-zero (0 = no widget)");
    NT_ASSERT(ctx->frame_pointer_count > 0U && "nt_ui_step_interaction_padded: no frame pointer snapshot");
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_step_interaction_padded: pad_lrtb components must be >= 0");

    const nt_ui_interaction_t out = nt_ui_query_interaction_padded(ctx, id, pad_lrtb);

#if NT_UI_DEBUG_TOOLS
    /* capture_seen stays 0 so next begin's orphan cleanup wipes in-progress capture (no phantom drag). */
    if (ctx->inspector_active && ctx->inspector_pointer_consumed) {
        return out;
    }
#endif

    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return out;
    }

    const uint32_t pidx = 0U;
    const nt_pointer_t *p = &ctx->frame_pointers[pidx];
    nt_ui_capture_t *cap = &ctx->captures[pidx];
    const nt_button_state_t btn = p->buttons[NT_BUTTON_LEFT];

    const bool exclusive_gated = (cap->active_id != 0U) && (cap->active_id != id);

    if (!exclusive_gated) {
        if (out.hovered) {
            ctx->pointer_over_any = true;
        }
        if (out.pressed_now) {
            cap->active_id = id;
            cap->press_pos[0] = p->x;
            cap->press_pos[1] = p->y;
            cap->pos[0] = p->x;
            cap->pos[1] = p->y;
        }
    }

    const bool mine = !exclusive_gated && (cap->active_id == id);
    if (mine) {
        cap->pos[0] = p->x;
        cap->pos[1] = p->y;
        /* Marks not-orphan for next begin's cleanup. */
        ctx->capture_seen[pidx] = 1U;
        if (btn.is_released) {
            cap->active_id = 0U;
        }
    }

#if NT_UI_DEBUG_TOOLS
    /* inspector_active also enables recording so its post-walk overlay can project the snapshot. */
    if ((ctx->debug_recording || ctx->inspector_active) && ctx->debug_zone_count < ctx->debug_zone_cap) {
        nt_ui_debug_zone_t *z = &ctx->debug_zones[ctx->debug_zone_count++];
        const float pl = (pad_lrtb != NULL) ? (float)pad_lrtb[0] : 0.0F;
        const float pr = (pad_lrtb != NULL) ? (float)pad_lrtb[1] : 0.0F;
        const float pt = (pad_lrtb != NULL) ? (float)pad_lrtb[2] : 0.0F;
        const float pb = (pad_lrtb != NULL) ? (float)pad_lrtb[3] : 0.0F;
        z->id = id;
        z->visual_l = d.boundingBox.x;
        z->visual_t = d.boundingBox.y;
        z->visual_r = d.boundingBox.x + d.boundingBox.width;
        z->visual_b = d.boundingBox.y + d.boundingBox.height;
        z->layout_l = z->visual_l - pl;
        z->layout_t = z->visual_t - pt;
        z->layout_r = z->visual_r + pr;
        z->layout_b = z->visual_b + pb;
        z->center_x = d.boundingBox.x + (d.boundingBox.width * 0.5F);
        z->center_y = d.boundingBox.y + (d.boundingBox.height * 0.5F);
        /* Snapshot composed affine — same source as walker/hit-test. */
        const int32_t e_slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, id);
        if (e_slot >= 0 && e_slot < (int32_t)ctx->max_elements) {
            const nt_ui_baked_xform_t b = ctx->hit_baked[e_slot];
            z->aff_a = b.a;
            z->aff_b = b.b;
            z->aff_c = b.c;
            z->aff_d = b.d;
            z->aff_tx = b.tx;
            z->aff_ty = b.ty;
        } else {
            const nt_ui_baked_xform_t b = nt_ui_internal_identity_baked();
            z->aff_a = b.a;
            z->aff_b = b.b;
            z->aff_c = b.c;
            z->aff_d = b.d;
            z->aff_tx = b.tx;
            z->aff_ty = b.ty;
        }
        uint16_t flags = 0U;
        if (out.hovered) {
            flags |= (uint16_t)NT_UI_DEBUG_FLAG_HOVERED;
        }
        if (out.pressed) {
            flags |= (uint16_t)NT_UI_DEBUG_FLAG_PRESSED;
        }
        if (cap->active_id == id) {
            flags |= (uint16_t)NT_UI_DEBUG_FLAG_CAPTURED;
        }
        z->state_flags = flags;
    }
#endif

    return out;
}

nt_ui_interaction_t nt_ui_step_interaction(nt_ui_context_t *ctx, uint32_t id) { return nt_ui_step_interaction_padded(ctx, id, NULL); }

#if NT_UI_DEBUG_TOOLS
/* Record-only push for DISABLED widgets that skip hit-test; flag surfaces in mode=ALL. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_debug_record_disabled_zone(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_debug_record_disabled_zone: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_debug_record_disabled_zone: id must be non-zero (0 = no widget)");
    NT_ASSERT((pad_lrtb == NULL || (pad_lrtb[0] >= 0 && pad_lrtb[1] >= 0 && pad_lrtb[2] >= 0 && pad_lrtb[3] >= 0)) && "nt_ui_debug_record_disabled_zone: pad_lrtb components must be >= 0");

    /* Zero-overhead fast path. */
    if ((!ctx->debug_recording && !ctx->inspector_active) || ctx->debug_zone_count >= ctx->debug_zone_cap) {
        return;
    }
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        return;
    }

    nt_ui_debug_zone_t *z = &ctx->debug_zones[ctx->debug_zone_count++];
    const float pl = (pad_lrtb != NULL) ? (float)pad_lrtb[0] : 0.0F;
    const float pr = (pad_lrtb != NULL) ? (float)pad_lrtb[1] : 0.0F;
    const float pt = (pad_lrtb != NULL) ? (float)pad_lrtb[2] : 0.0F;
    const float pb = (pad_lrtb != NULL) ? (float)pad_lrtb[3] : 0.0F;
    z->id = id;
    z->visual_l = d.boundingBox.x;
    z->visual_t = d.boundingBox.y;
    z->visual_r = d.boundingBox.x + d.boundingBox.width;
    z->visual_b = d.boundingBox.y + d.boundingBox.height;
    z->layout_l = z->visual_l - pl;
    z->layout_t = z->visual_t - pt;
    z->layout_r = z->visual_r + pr;
    z->layout_b = z->visual_b + pb;
    z->center_x = d.boundingBox.x + (d.boundingBox.width * 0.5F);
    z->center_y = d.boundingBox.y + (d.boundingBox.height * 0.5F);
    const int32_t e_slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, id);
    if (e_slot >= 0 && e_slot < (int32_t)ctx->max_elements) {
        const nt_ui_baked_xform_t b = ctx->hit_baked[e_slot];
        z->aff_a = b.a;
        z->aff_b = b.b;
        z->aff_c = b.c;
        z->aff_d = b.d;
        z->aff_tx = b.tx;
        z->aff_ty = b.ty;
    } else {
        const nt_ui_baked_xform_t b = nt_ui_internal_identity_baked();
        z->aff_a = b.a;
        z->aff_b = b.b;
        z->aff_c = b.c;
        z->aff_d = b.d;
        z->aff_tx = b.tx;
        z->aff_ty = b.ty;
    }
    z->state_flags = (uint16_t)NT_UI_DEBUG_FLAG_DISABLED;
}
#endif /* NT_UI_DEBUG_TOOLS */

bool nt_ui_wants_pointer(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_wants_pointer: ctx must be non-NULL");
#if NT_UI_DEBUG_TOOLS
    /* Sidebar counts as "engine wants pointer" so game world input is suppressed. */
    if (ctx->inspector_active && ctx->inspector_pointer_consumed) {
        return true;
    }
#endif
    if (ctx->pointer_over_any) {
        return true;
    }
    for (uint32_t i = 0; i < NT_INPUT_MAX_POINTERS; ++i) {
        if (ctx->captures[i].active_id != 0U) {
            return true;
        }
    }
    return false;
}
// #endregion

// #region public_metrics
uint32_t nt_ui_get_last_walk_draw_calls(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_draw_calls: ctx must be non-NULL");
    return ctx->last_walk_draw_call_delta;
}

uint32_t nt_ui_get_last_walk_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_command_count: ctx must be non-NULL");
    return ctx->last_walk_command_count;
}

float nt_ui_get_last_layout_ms(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_layout_ms: ctx must be non-NULL");
    return ctx->last_layout_ms;
}

float nt_ui_get_last_build_tree_ms(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_build_tree_ms: ctx must be non-NULL");
    return ctx->last_build_tree_ms;
}

float nt_ui_get_last_walk_ms(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_ms: ctx must be non-NULL");
    return ctx->last_walk_ms;
}

uint32_t nt_ui_get_anim_collision_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_anim_collision_count: ctx must be non-NULL");
    return ctx->anim_collision_count;
}

uint32_t nt_ui_get_last_walk_rect_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_rect_command_count: ctx must be non-NULL");
    return ctx->last_walk_rect_command_count;
}

uint32_t nt_ui_get_last_walk_image_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_image_command_count: ctx must be non-NULL");
    return ctx->last_walk_image_command_count;
}

uint32_t nt_ui_get_last_walk_text_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_text_command_count: ctx must be non-NULL");
    return ctx->last_walk_text_command_count;
}

uint32_t nt_ui_get_last_walk_border_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_border_command_count: ctx must be non-NULL");
    return ctx->last_walk_border_command_count;
}

uint32_t nt_ui_get_last_walk_scissor_command_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_scissor_command_count: ctx must be non-NULL");
    return ctx->last_walk_scissor_command_count;
}

uint32_t nt_ui_get_last_walk_max_scissor_depth(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_get_last_walk_max_scissor_depth: ctx must be non-NULL");
    return ctx->last_walk_max_scissor_depth;
}

// #endregion

// #region test_access
#ifdef NT_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void) { return g_nt_ui_inframe_ctx; }

int32_t nt_ui_test_clay_default_max_element_count(void) { return nt_ui_clay_priv_default_max_element_count(); }
int32_t nt_ui_test_clay_default_max_measure_text_word_cache_count(void) { return nt_ui_clay_priv_default_max_measure_text_word_cache_count(); }

uint32_t nt_ui_test_last_walk_unlayered_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL);
    return ctx->test_last_walk_unlayered_count;
}

float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_x: ctx must be non-NULL");
    return nt_ui_clay_priv_pointer_x(ctx->clay);
}
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_y: ctx must be non-NULL");
    return nt_ui_clay_priv_pointer_y(ctx->clay);
}
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_down: ctx must be non-NULL");
    return nt_ui_clay_priv_pointer_pressed(ctx->clay);
}

uint32_t nt_ui_test_capture_active_id(const nt_ui_context_t *ctx, uint32_t pointer_index) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_capture_active_id: ctx must be non-NULL");
    NT_ASSERT(pointer_index < NT_INPUT_MAX_POINTERS && "nt_ui_test_capture_active_id: pointer_index out of range");
    return ctx->captures[pointer_index].active_id;
}

bool nt_ui_test_hit(nt_ui_context_t *ctx, uint32_t id, float px, float py) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_hit: ctx must be non-NULL");
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    const bool hit = ui_hit_test(ctx, id, px, py, NULL);
    Clay_SetCurrentContext(saved);
    return hit;
}

bool nt_ui_test_hit_padded(nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_hit_padded: ctx must be non-NULL");
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(ctx->clay);
    const bool hit = ui_hit_test(ctx, id, px, py, pad_lrtb);
    Clay_SetCurrentContext(saved);
    return hit;
}
#endif
// #endregion
