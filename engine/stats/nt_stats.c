#include "stats/nt_stats.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "renderers/nt_text_renderer.h"
#include "time/nt_time.h"

#include <stdio.h>
#include <string.h>

// #region Module state
static struct {
    bool initialized;
    uint16_t fps_window;
    uint16_t fps_count;
    uint16_t fps_head;
    float fps_ring[NT_STATS_FPS_WINDOW_MAX]; /* seconds per frame */

    double frame_begin_t;
    float last_cpu_ms;
    float last_gpu_ms;
    uint32_t last_draw_calls;
    uint64_t frame_index;
    uint16_t throughput_period;
    bool log_enabled;

    /* User counters — flat parallel arrays (Open Q5) */
    uint16_t user_capacity;
    uint16_t user_count;
    uint64_t user_name_hashes[NT_STATS_MAX_USER_COUNTERS];
    uint64_t user_values[NT_STATS_MAX_USER_COUNTERS];
    char user_names[NT_STATS_MAX_USER_COUNTERS][NT_STATS_USER_COUNTER_NAME_MAX];
} s_stats;
// #endregion

// #region Lifecycle
nt_result_t nt_stats_init(const nt_stats_desc_t *desc) {
    NT_ASSERT(!s_stats.initialized);
    nt_stats_desc_t d = (desc != NULL) ? *desc : nt_stats_desc_defaults();
    NT_ASSERT(d.fps_window > 0 && d.fps_window <= NT_STATS_FPS_WINDOW_MAX);
    NT_ASSERT(d.user_counter_capacity <= NT_STATS_MAX_USER_COUNTERS);

    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.fps_window = d.fps_window;
    s_stats.throughput_period = d.throughput_log_period;
    s_stats.log_enabled = d.enable_throughput_log;
    s_stats.user_capacity = d.user_counter_capacity;
    s_stats.last_gpu_ms = -1.0F; /* GPU timer not yet wired (Pitfall 5) */
    s_stats.initialized = true;

    NT_LOG_INFO("nt_stats: GPU timer not yet exposed via nt_gfx; gpu_ms = -1.0 always");
    return NT_OK;
}

void nt_stats_shutdown(void) { s_stats.initialized = false; }
// #endregion

// #region Frame brackets
void nt_stats_frame_begin(void) {
    NT_ASSERT(s_stats.initialized);
    s_stats.frame_begin_t = nt_time_now();
}

void nt_stats_frame_end(void) {
    NT_ASSERT(s_stats.initialized);
    // #region timing + draw count
    double now = nt_time_now();
    float dt_s = (float)(now - s_stats.frame_begin_t);
    s_stats.last_cpu_ms = dt_s * 1000.0F;
    s_stats.last_draw_calls = nt_gfx_get_frame_draw_calls();
    /* GPU timer: Pitfall 5 — not yet wired through nt_gfx; remain -1.0F */
    // #endregion

    // #region DEMO-05 rolling FPS ring
    /* Ring stores per-frame seconds. Rolling avg FPS = count / sum(ring[0..count]).
     * This intentionally lags a discrete window — instantaneous FPS is NOT used. */
    s_stats.fps_ring[s_stats.fps_head] = dt_s;
    s_stats.fps_head = (uint16_t)((s_stats.fps_head + 1) % s_stats.fps_window);
    if (s_stats.fps_count < s_stats.fps_window) {
        s_stats.fps_count++;
    }
    // #endregion

    // #region DEMO-06 throughput log
    s_stats.frame_index++;
    if (s_stats.log_enabled && s_stats.throughput_period > 0 && (s_stats.frame_index % s_stats.throughput_period) == 0) {
        /* Find well-known user counters by hashed name */
        const uint64_t k_bunnies = nt_hash64_str("bunnies").value;
        const uint64_t k_quality = nt_hash64_str("atlas_quality").value;
        uint64_t bunnies = 0;
        uint64_t quality = 0;
        for (uint16_t i = 0; i < s_stats.user_count; i++) {
            if (s_stats.user_name_hashes[i] == k_bunnies) {
                bunnies = s_stats.user_values[i];
            }
            if (s_stats.user_name_hashes[i] == k_quality) {
                quality = s_stats.user_values[i];
            }
        }
        const char *atlas_str = (quality == 0) ? "SD" : "HD";
        if (s_stats.last_gpu_ms < 0.0F) {
            NT_LOG_INFO("frame=%llu fps=%.1f cpu=%.2f ms gpu=N/A draws=%u bunnies=%llu atlas=%s", (unsigned long long)s_stats.frame_index, (double)nt_stats_get_fps(), (double)s_stats.last_cpu_ms,
                        s_stats.last_draw_calls, (unsigned long long)bunnies, atlas_str);
        } else {
            NT_LOG_INFO("frame=%llu fps=%.1f cpu=%.2f ms gpu=%.2f ms draws=%u bunnies=%llu atlas=%s", (unsigned long long)s_stats.frame_index, (double)nt_stats_get_fps(), (double)s_stats.last_cpu_ms,
                        (double)s_stats.last_gpu_ms, s_stats.last_draw_calls, (unsigned long long)bunnies, atlas_str);
        }
    }
    // #endregion
}
// #endregion

// #region Read accessors
float nt_stats_get_fps(void) {
    if (s_stats.fps_count == 0) {
        return 0.0F;
    }
    float sum_s = 0.0F;
    for (uint16_t i = 0; i < s_stats.fps_count; i++) {
        sum_s += s_stats.fps_ring[i];
    }
    return (sum_s > 0.0F) ? ((float)s_stats.fps_count / sum_s) : 0.0F;
}

float nt_stats_get_cpu_ms(void) { return s_stats.last_cpu_ms; }
float nt_stats_get_gpu_ms(void) { return s_stats.last_gpu_ms; }
uint32_t nt_stats_get_draw_calls(void) { return s_stats.last_draw_calls; }
// #endregion

// #region User counters
void nt_stats_count(const char *name, uint64_t value) {
    NT_ASSERT(s_stats.initialized);
    NT_ASSERT(name != NULL);
    uint64_t h = nt_hash64_str(name).value;
    for (uint16_t i = 0; i < s_stats.user_count; i++) {
        if (s_stats.user_name_hashes[i] == h) {
            s_stats.user_values[i] = value;
            return;
        }
    }
    NT_ASSERT(s_stats.user_count < s_stats.user_capacity && "nt_stats user-counter capacity exceeded; raise nt_stats_desc_t.user_counter_capacity");
    s_stats.user_name_hashes[s_stats.user_count] = h;
    s_stats.user_values[s_stats.user_count] = value;
    /* Stash readable name (truncated) for format_lines */
    size_t n = strlen(name);
    if (n >= NT_STATS_USER_COUNTER_NAME_MAX) {
        n = NT_STATS_USER_COUNTER_NAME_MAX - 1;
    }
    memcpy(s_stats.user_names[s_stats.user_count], name, n);
    s_stats.user_names[s_stats.user_count][n] = '\0';
    s_stats.user_count++;
}
// #endregion

// #region Format & draw
uint32_t nt_stats_format_lines(char *buf, uint32_t size) {
    NT_ASSERT(buf && size > 0);

    /* GPU slot is "N/A" until Pitfall 5 GPU timer wiring lands */
    char gpu_buf[32];
    if (s_stats.last_gpu_ms < 0.0F) {
        (void)snprintf(gpu_buf, sizeof(gpu_buf), "N/A");
    } else {
        (void)snprintf(gpu_buf, sizeof(gpu_buf), "%.2f ms", (double)s_stats.last_gpu_ms);
    }

    int n = snprintf(buf, size, "FPS: %.1f\nCPU: %.2f ms\nGPU: %s\nDraws: %u\n", (double)nt_stats_get_fps(), (double)s_stats.last_cpu_ms, gpu_buf, s_stats.last_draw_calls);
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    uint32_t written = (uint32_t)n;
    if (written >= size) {
        /* Truncated — buf is already NUL-terminated by snprintf */
        return size - 1;
    }

    /* User counters in insertion order, one line each: "name: value" */
    for (uint16_t i = 0; i < s_stats.user_count; i++) {
        int m = snprintf(buf + written, size - written, "%s: %llu\n", s_stats.user_names[i], (unsigned long long)s_stats.user_values[i]);
        if (m < 0) {
            break;
        }
        if ((uint32_t)m >= size - written) {
            /* Truncated — return available */
            return size - 1;
        }
        written += (uint32_t)m;
    }

    return written;
}

void nt_stats_draw(nt_material_t material, nt_font_t font, const float model[16], float size, const float color[4]) {
    NT_ASSERT(s_stats.initialized);
    char buf[512];
    (void)nt_stats_format_lines(buf, sizeof(buf));
    /* Pitfall 9 (Issue 2 fix): explicit set_material AND set_font defeat
     * nt_text_renderer's change-detection early-out so the overlay always
     * binds correctly regardless of prior frame state. */
    nt_text_renderer_set_material(material);
    nt_text_renderer_set_font(font);
    nt_text_renderer_draw(buf, model, size, color);
}
// #endregion
