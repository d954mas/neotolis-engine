#ifndef NT_UI_DEBUG_H
#define NT_UI_DEBUG_H

/* Hit-zone recording + draw helper. Gated by NT_UI_DEBUG_TOOLS (CMake option, default OFF). */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h"

typedef struct nt_ui_context nt_ui_context_t;

/* Visible in both modes so callers keep typed locals even when stubs are no-op. */
typedef enum {
    NT_UI_DEBUG_HIT_OFF = 0,      /* draw nothing */
    NT_UI_DEBUG_HIT_HOVER = 1,    /* only zones the primary pointer is currently over */
    NT_UI_DEBUG_HIT_CAPTURED = 2, /* only zones currently captured by a pointer */
    NT_UI_DEBUG_HIT_ALL = 3,      /* every recorded zone */
} nt_ui_debug_hit_mode_t;

#if NT_UI_DEBUG_TOOLS

void nt_ui_debug_set_recording(nt_ui_context_t *ctx, bool on);
bool nt_ui_debug_get_recording(const nt_ui_context_t *ctx);

uint32_t nt_ui_debug_get_zone_count(const nt_ui_context_t *ctx);

/* First-frame Clay miss → no zone recorded (not an assert). pad_lrtb NULL = zero padding. */
void nt_ui_debug_record_disabled_zone(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]);

/* `target` MUST match the nt_ui_walk target. label_size 0 skips labels. */
void nt_ui_debug_draw_hit_zones(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_ui_debug_hit_mode_t mode, nt_font_t font, float label_size);

#else /* NT_UI_DEBUG_TOOLS */

static inline void nt_ui_debug_set_recording(nt_ui_context_t *ctx, bool on) {
    (void)ctx;
    (void)on;
}
static inline bool nt_ui_debug_get_recording(const nt_ui_context_t *ctx) {
    (void)ctx;
    return false;
}
static inline uint32_t nt_ui_debug_get_zone_count(const nt_ui_context_t *ctx) {
    (void)ctx;
    return 0U;
}
static inline void nt_ui_debug_record_disabled_zone(nt_ui_context_t *ctx, uint32_t id, const int16_t pad_lrtb[4]) {
    (void)ctx;
    (void)id;
    (void)pad_lrtb;
}
static inline void nt_ui_debug_draw_hit_zones(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_ui_debug_hit_mode_t mode, nt_font_t font, float label_size) {
    (void)ctx;
    (void)target;
    (void)mode;
    (void)font;
    (void)label_size;
}

#endif /* NT_UI_DEBUG_TOOLS */

#endif /* NT_UI_DEBUG_H */
