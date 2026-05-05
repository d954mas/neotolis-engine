#ifndef NT_STATS_H
#define NT_STATS_H

#include "core/nt_types.h"
#include "font/nt_font.h"
#include "material/nt_material.h"

#ifndef NT_STATS_MAX_USER_COUNTERS
#define NT_STATS_MAX_USER_COUNTERS 16
#endif

#ifndef NT_STATS_FPS_WINDOW_MAX
#define NT_STATS_FPS_WINDOW_MAX 240
#endif

#ifndef NT_STATS_USER_COUNTER_NAME_MAX
#define NT_STATS_USER_COUNTER_NAME_MAX 32
#endif

typedef struct {
    uint16_t fps_window;            /* default 60 (D-37) */
    uint16_t throughput_log_period; /* default 60 frames; 0 = no log */
    bool enable_throughput_log;     /* default true */
    uint16_t user_counter_capacity; /* default 16 (D-37) */
} nt_stats_desc_t;

static inline nt_stats_desc_t nt_stats_desc_defaults(void) {
    return (nt_stats_desc_t){
        .fps_window = 60,
        .throughput_log_period = 60,
        .enable_throughput_log = true,
        .user_counter_capacity = 16,
    };
}

/* ---- Lifecycle ---- */

nt_result_t nt_stats_init(const nt_stats_desc_t *desc);
void nt_stats_shutdown(void);

/* ---- Frame brackets ---- */

void nt_stats_frame_begin(void);
void nt_stats_frame_end(void);

/* ---- User counters ---- */

/* Set or update a user counter by name. Linear-scan on flat array (Open Q5).
 * Name is hashed once per call (xxh64-ish via nt_hash64_str). Capacity is
 * configured at init; runtime cap-overflow trips NT_ASSERT (configuration bug,
 * raise capacity). */
void nt_stats_count(const char *name, uint64_t value);

/* ---- Read accessors ---- */

float nt_stats_get_fps(void);           /* rolling avg over fps_window frames; 0.0F until ring filled */
float nt_stats_get_cpu_ms(void);        /* last frame */
float nt_stats_get_gpu_ms(void);        /* -1.0F when extension absent / disjoint (Pitfall 5) */
uint32_t nt_stats_get_draw_calls(void); /* last frame, from nt_gfx_get_frame_draw_calls */

/* ---- Format multi-line stats string ----
 * Returns bytes written (not including trailing NUL). buf is NUL-terminated.
 * If `size` is too small, output is truncated (snprintf-style) and the
 * returned value is the bytes actually written.
 */
uint32_t nt_stats_format_lines(char *buf, uint32_t size);

/* ---- Convenience: format + draw via nt_text_renderer ----
 * Pitfall 9 (Issue 2 fix): explicitly calls nt_text_renderer_set_material AND
 * nt_text_renderer_set_font before draw to defeat the change-detection
 * early-out so the overlay always binds correctly regardless of prior frame
 * state. */
void nt_stats_draw(nt_material_t material, nt_font_t font, const float model[16], float size, const float color[4]);

#endif /* NT_STATS_H */
