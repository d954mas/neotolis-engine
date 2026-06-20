#ifndef NT_DEBUG_OVERLAY_H
#define NT_DEBUG_OVERLAY_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

#ifndef NT_DEBUG_OVERLAY_MAX_USER_COUNTERS
#define NT_DEBUG_OVERLAY_MAX_USER_COUNTERS 16
#endif

#ifndef NT_DEBUG_OVERLAY_FPS_WINDOW_MAX
#define NT_DEBUG_OVERLAY_FPS_WINDOW_MAX 240
#endif

#ifndef NT_DEBUG_OVERLAY_USER_COUNTER_NAME_MAX
#define NT_DEBUG_OVERLAY_USER_COUNTER_NAME_MAX 32
#endif

typedef struct {
    uint16_t fps_window;            /* default 60 */
    uint16_t user_counter_capacity; /* default 16 */
} nt_debug_overlay_desc_t;

static inline nt_debug_overlay_desc_t nt_debug_overlay_desc_defaults(void) {
    return (nt_debug_overlay_desc_t){
        .fps_window = 60,
        .user_counter_capacity = 16,
    };
}

/* ---- Lifecycle ---- */

nt_result_t nt_debug_overlay_init(const nt_debug_overlay_desc_t *desc);
void nt_debug_overlay_shutdown(void);

/* ---- Frame brackets ---- */

void nt_debug_overlay_frame_begin(void);
void nt_debug_overlay_frame_end(void);

/* ---- User counters ---- */

/* Set or update a user counter by name. Linear-scan on flat array.
 * Name is hashed once per call (xxh64-ish via nt_hash64_str). Capacity is
 * configured at init; runtime cap-overflow trips NT_ASSERT (configuration bug,
 * raise capacity). Tags the counter as an integer value. */
void nt_debug_overlay_count(const char *name, uint64_t value);

/* Float variant of nt_debug_overlay_count. Same lookup/store; tags the counter
 * as a float value so it survives without truncation (e.g. frame time). Writing
 * an existing name with the other variant flips its tag (last write wins). */
void nt_debug_overlay_count_f(const char *name, double value);

/* ---- User counter enumeration (D-08) ----
 * nt_metrics reads all user counters generically by index. Enumeration order is
 * the flat-array insertion order, stable across calls within a frame. */
uint16_t nt_debug_overlay_user_count(void);

/* Read counter `i` in [0, nt_debug_overlay_user_count()). Out-params may be NULL
 * to skip. `value` is the stored measurement as a double (int counters widened),
 * `is_float` reports the tag. `i` out of range trips NT_ASSERT (host-call bug). */
void nt_debug_overlay_user_get(uint16_t i, const char **name, double *value, bool *is_float);

/* ---- Read accessors ---- */

float nt_debug_overlay_get_fps(void);           /* rolling avg over fps_window frames; 0.0F until ring filled */
float nt_debug_overlay_get_cpu_ms(void);        /* last frame */
float nt_debug_overlay_get_gpu_ms(void);        /* -1.0F when extension absent / disjoint */
uint32_t nt_debug_overlay_get_draw_calls(void); /* last frame, from nt_gfx_get_frame_draw_calls */

/* ---- Format multi-line stats string ----
 * Returns bytes written (not including trailing NUL). buf is NUL-terminated.
 * If `size` is too small, output is truncated (snprintf-style) and the
 * returned value is the bytes actually written.
 */
uint32_t nt_debug_overlay_format_lines(char *buf, uint32_t size);

/* ---- Convenience: format + draw via nt_text_renderer ----
 * Explicitly calls nt_text_renderer_set_material AND nt_text_renderer_set_font
 * before draw to defeat the change-detection early-out so the overlay always
 * binds correctly regardless of prior frame state. */
void nt_debug_overlay_draw(nt_material_t material, nt_font_t font, const float model[16], float size, const float color[4]);

// #region test_access
/* Inject a synthetic per-frame seconds value directly into the FPS ring +
 * cpu/draw bookkeeping, bypassing the real wall clock. Used by unit tests
 * to assert the rolling-avg formula deterministically. */
#ifdef NT_TEST_ACCESS
void nt_debug_overlay_test_inject_frame(float dt_seconds);
#endif
// #endregion

#endif /* NT_DEBUG_OVERLAY_H */
