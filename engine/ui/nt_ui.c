#include "ui/nt_ui.h"

/*
 * Clay v0.14 implementation TU.
 *
 * One TU in the entire build defines CLAY_IMPLEMENTATION. This is it.
 *
 * Version pin (per Drift 1 Option D):
 *   - deps/clay/VERSION is the single source of truth.
 *   - engine/ui/CMakeLists.txt parses major/minor and passes them as
 *     CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR via target_compile_definitions.
 *   - The _Static_assert below catches accidental dev-time drift (e.g.,
 *     someone hand-edits deps/clay/VERSION to 0.13 without re-vendoring).
 *   - CLAY_VERSION_MAJOR / CLAY_VERSION_MINOR macros do NOT exist in Clay
 *     v0.14 upstream -- verified by direct read of the v0.14 header.
 */

#if !defined(CLAY_PINNED_MAJOR) || !defined(CLAY_PINNED_MINOR)
#error "nt_ui: CLAY_PINNED_MAJOR / CLAY_PINNED_MINOR must be defined by CMake (engine/ui/CMakeLists.txt parses deps/clay/VERSION)"
#endif

#define CLAY_IMPLEMENTATION
#include "clay.h"

_Static_assert(CLAY_PINNED_MAJOR == 0 && CLAY_PINNED_MINOR == 14, "Clay v0.14 required -- deps/clay/VERSION disagrees with the engine pin");

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "ui/nt_ui_internal.h"

// #region module_state
/* g_nt_ui_inframe_ctx -- module-level pointer enforcing the multi-context
 * invariant (D-52-12). Exactly one ctx may be in-frame at a time; nt_ui_begin
 * asserts this is NULL on entry and assigns ctx; nt_ui_end clears it. */
static nt_ui_context_t *g_nt_ui_inframe_ctx = NULL;

/* g_nt_ui_measure_wired -- Clay_SetMeasureTextFunction is a global hook.
 * We wire it lazily on first create_context. Plan 03 fills the body. */
static bool g_nt_ui_measure_wired = false;
// #endregion

// #region clay_error_handler
/* Forward a Clay error to NT_LOG_WARN. errorText is a Clay_String with
 * .length + .chars (not necessarily NUL-terminated). */
static void nt_ui_clay_error_cb(Clay_ErrorData err) {
    int len = err.errorText.length;
    const char *chars = err.errorText.chars;
    if (chars == NULL || len <= 0) {
        NT_LOG_WARN("clay error type=%d (no text)", (int)err.errorType);
        return;
    }
    NT_LOG_WARN("clay error type=%d: %.*s", (int)err.errorType, len, chars);
}
// #endregion

// #region measure_cb
/* Clay measure callback (CLAY-03 / D-52-14).
 *
 * Forwards to the Phase 51 length-aware measure path through the per-ctx
 * font registry. Phase 51's 256-entry xxHash measure cache (37x hit speedup)
 * is the MP-07 amplification mitigation.
 *
 * Returns {0,0} (no crash) when ctx is not mid-frame, config is NULL, the
 * font slot is out of range, or the slot holds an invalid/destroyed handle.
 *
 * user_data is reserved (NULL in Phase 52); D-52-14 keeps it for future
 * per-context font tables that bypass g_nt_ui_inframe_ctx. */
static Clay_Dimensions nt_ui_measure_text_cb(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    (void)user_data; /* Phase 52 reserves user_data; Plan 03 ships NULL */

    nt_ui_context_t *ctx = g_nt_ui_inframe_ctx;
    if (ctx == NULL || config == NULL || config->fontId >= NT_UI_MAX_FONTS) {
        return (Clay_Dimensions){0};
    }
    nt_font_t font = ctx->fonts[config->fontId];
    if (!nt_font_valid(font)) {
        return (Clay_Dimensions){0};
    }
    nt_text_size_t s = nt_font_measure_n(font, text.chars, (size_t)text.length, (float)config->fontSize);
    return (Clay_Dimensions){.width = s.width, .height = s.height};
}
// #endregion

// #region create_destroy
/* Pattern 2 from 52-RESEARCH.md: caller-owned arena, ctx struct lives in
 * the first ~256 bytes (cache-line aligned), Clay arena takes the rest. */
static size_t nt_ui_ctx_size_aligned(void) { return (sizeof(struct nt_ui_context) + 63u) & ~(size_t)63u; }

size_t nt_ui_min_arena_size(void) { return nt_ui_ctx_size_aligned() + (size_t)Clay_MinMemorySize(); }

nt_ui_context_t *nt_ui_create_context(void *arena, size_t arena_size) {
    NT_ASSERT(arena != NULL && "nt_ui_create_context: arena must be non-NULL");
    NT_ASSERT(((uintptr_t)arena & 7u) == 0u && "nt_ui_create_context: arena must be 8-byte aligned");
    NT_ASSERT(arena_size >= nt_ui_min_arena_size() && "nt_ui_create_context: arena_size < nt_ui_min_arena_size()");

    nt_ui_context_t *ctx = (nt_ui_context_t *)arena;
    memset(ctx, 0, sizeof(*ctx));

    const size_t ctx_size = nt_ui_ctx_size_aligned();
    void *clay_mem = (char *)arena + ctx_size;
    const size_t clay_size = arena_size - ctx_size;

    ctx->arena_base = arena;
    ctx->arena_size = arena_size;
    ctx->in_frame = false;
    ctx->clay_arena = Clay_CreateArenaWithCapacityAndMemory(clay_size, clay_mem);
    ctx->clay = Clay_Initialize(ctx->clay_arena, (Clay_Dimensions){.width = 1.0f, .height = 1.0f}, (Clay_ErrorHandler){.errorHandlerFunction = nt_ui_clay_error_cb, .userData = ctx});

    /* Clay_SetMeasureTextFunction is a global hook -- wire it once. The
     * callback (nt_ui_measure_text_cb above) handles font lookup + measure. */
    if (!g_nt_ui_measure_wired) {
        Clay_SetMeasureTextFunction(nt_ui_measure_text_cb, NULL);
        g_nt_ui_measure_wired = true;
    }

    return ctx;
}

void nt_ui_destroy_context(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_destroy_context: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_destroy_context: ctx is mid-frame (call nt_ui_end first)");

    /* If this context is the in-frame ctx for any reason, clear the global
     * (defensive -- assert above should prevent this). */
    if (g_nt_ui_inframe_ctx == ctx) {
        g_nt_ui_inframe_ctx = NULL;
    }

    /* Caller owns the arena memory -- only zero the ctx struct portion. */
    memset(ctx, 0, sizeof(*ctx));
}
// #endregion

// #region font_registry
void nt_ui_set_font(nt_ui_context_t *ctx, uint16_t font_id, nt_font_t font) {
    NT_ASSERT(ctx != NULL && "nt_ui_set_font: ctx must be non-NULL");
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_set_font: font_id out of range (raise NT_UI_MAX_FONTS)");
    ctx->fonts[font_id] = font;
}
// #endregion

// #region begin_end
void nt_ui_begin(nt_ui_context_t *ctx, float screen_w, float screen_h, const nt_pointer_t *mouse) {
    NT_ASSERT(ctx != NULL && "nt_ui_begin: ctx must be non-NULL");
    NT_ASSERT(mouse != NULL && "nt_ui_begin: mouse must be non-NULL");
    NT_ASSERT(g_nt_ui_inframe_ctx == NULL && "nt_ui_begin: another ctx is mid-frame (D-52-12 violation)");
    NT_ASSERT(!ctx->in_frame && "nt_ui_begin: ctx already in_frame (CP-03 footgun)");

    /* FIRST executable: switch Clay's global current-context pointer to
     * this ctx's Clay context (UI-04 / CP-03 prevention). Every Clay call
     * below operates on ctx->clay. */
    Clay_SetCurrentContext(ctx->clay);

    ctx->in_frame = true;
    g_nt_ui_inframe_ctx = ctx;

    Clay_SetLayoutDimensions((Clay_Dimensions){.width = screen_w, .height = screen_h});

    /* Pointer state (CLAY-04 / D-52-16). Left-button only is intentional in
     * v1.8 — right/middle/wheel are not consumed by Clay v0.14. Multi-pointer
     * is deferred: the game caller chooses which pointer to feed (typically
     * pointers[0]; split-screen uses pointers[N]). */
    Clay_SetPointerState((Clay_Vector2){.x = mouse->x, .y = mouse->y}, mouse->buttons[NT_BUTTON_LEFT].is_down);

    Clay_BeginLayout();
}

void nt_ui_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && "nt_ui_end: ctx is not in_frame (begin was not called)");
    NT_ASSERT(ctx == g_nt_ui_inframe_ctx && "nt_ui_end: ctx mismatch with module in-frame ctx");

    ctx->frozen_cmds = Clay_EndLayout();
    ctx->in_frame = false;
    g_nt_ui_inframe_ctx = NULL;
}
// #endregion

// #region walk_stub
/* Walker body lives in Plan 04. Phase 52 ships entry-guard asserts only. */
void nt_ui_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    NT_ASSERT(ctx != NULL && "nt_ui_walk: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_walk: target must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_walk: ctx is mid-frame (call nt_ui_end first)");
    /* Plan 04 fills: viewport setup, frozen_cmds iteration, dispatch. */
}
// #endregion

// #region setters_stub
/* Setter bodies land in Plan 04 next to the walker code that consumes
 * them. Phase 52 declares them in the public header (Revision Issue 1
 * locks symmetric sprite + text material setters). */
void nt_ui_set_atlas_white_region(nt_resource_t atlas, uint32_t white_region_idx) {
    (void)atlas;
    (void)white_region_idx;
}

void nt_ui_set_sprite_material(nt_material_t sprite_material) { (void)sprite_material; }

void nt_ui_set_text_material(nt_material_t text_material) { (void)text_material; }

void nt_ui_set_custom_handler(nt_ui_custom_handler_t fn, void *userdata) {
    (void)fn;
    (void)userdata;
}
// #endregion

// #region test_access
#ifdef NT_UI_TEST_ACCESS
nt_ui_context_t *nt_ui_test_inframe_ctx(void) { return g_nt_ui_inframe_ctx; }

/* Plan 05 fills the body. */
uint32_t nt_ui_test_last_walk_draw_call_delta(void) { return 0u; }
uint32_t nt_ui_test_last_walk_element_count(void) { return 0u; }

/* CLAY-04 / D-52-16 verification probes. ctx->clay is a Clay_Context whose
 * struct definition is only visible to this TU (CLAY_IMPLEMENTATION above);
 * tests cannot read pointerInfo directly. These getters bridge that gap. */
float nt_ui_test_clay_pointer_x(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_x: ctx must be non-NULL");
    return ctx->clay->pointerInfo.position.x;
}
float nt_ui_test_clay_pointer_y(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_y: ctx must be non-NULL");
    return ctx->clay->pointerInfo.position.y;
}
int nt_ui_test_clay_pointer_down(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_test_clay_pointer_down: ctx must be non-NULL");
    const Clay_PointerDataInteractionState s = ctx->clay->pointerInfo.state;
    return (s == CLAY_POINTER_DATA_PRESSED || s == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) ? 1 : 0;
}
#endif /* NT_UI_TEST_ACCESS */
// #endregion
