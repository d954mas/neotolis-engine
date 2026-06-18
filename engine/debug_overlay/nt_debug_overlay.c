#include "debug_overlay/nt_debug_overlay.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
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
    float fps_ring[NT_DEBUG_OVERLAY_FPS_WINDOW_MAX]; /* seconds per frame */

    double frame_begin_t;
    double last_frame_begin_t;
    float last_cpu_ms;
    float last_gpu_ms;
    uint32_t last_draw_calls;
    uint64_t frame_index;

    /* User counters — flat parallel arrays */
    uint16_t user_capacity;
    uint16_t user_count;
    uint64_t user_name_hashes[NT_DEBUG_OVERLAY_MAX_USER_COUNTERS];
    uint64_t user_values[NT_DEBUG_OVERLAY_MAX_USER_COUNTERS];
    char user_names[NT_DEBUG_OVERLAY_MAX_USER_COUNTERS][NT_DEBUG_OVERLAY_USER_COUNTER_NAME_MAX];
} s_overlay;
// #endregion

// #region Lifecycle
nt_result_t nt_debug_overlay_init(const nt_debug_overlay_desc_t *desc) {
    NT_ASSERT(!s_overlay.initialized);
    nt_debug_overlay_desc_t d = (desc != NULL) ? *desc : nt_debug_overlay_desc_defaults();
    NT_ASSERT(d.fps_window > 0 && d.fps_window <= NT_DEBUG_OVERLAY_FPS_WINDOW_MAX);
    NT_ASSERT(d.user_counter_capacity <= NT_DEBUG_OVERLAY_MAX_USER_COUNTERS);

    memset(&s_overlay, 0, sizeof(s_overlay));
    s_overlay.fps_window = d.fps_window;
    s_overlay.user_capacity = d.user_counter_capacity;
    s_overlay.last_gpu_ms = -1.0F; /* until first poll succeeds */
    s_overlay.initialized = true;
    return NT_OK;
}

void nt_debug_overlay_shutdown(void) { s_overlay.initialized = false; }
// #endregion

// #region Frame brackets
void nt_debug_overlay_frame_begin(void) {
    NT_ASSERT(s_overlay.initialized);
    double now = nt_time_now();
    if (s_overlay.last_frame_begin_t > 0.0) {
        float frame_dt_s = (float)(now - s_overlay.last_frame_begin_t);
        s_overlay.fps_ring[s_overlay.fps_head] = frame_dt_s;
        s_overlay.fps_head = (uint16_t)((s_overlay.fps_head + 1) % s_overlay.fps_window);
        if (s_overlay.fps_count < s_overlay.fps_window) {
            s_overlay.fps_count++;
        }
    }
    s_overlay.last_frame_begin_t = now;
    s_overlay.frame_begin_t = now;
}

void nt_debug_overlay_frame_end(void) {
    NT_ASSERT(s_overlay.initialized);
    // #region timing + draw count
    double now = nt_time_now();
    float dt_s = (float)(now - s_overlay.frame_begin_t);
    s_overlay.last_cpu_ms = dt_s * 1000.0F;
    s_overlay.last_draw_calls = nt_gfx_get_frame_draw_calls();

    /* GPU frame total via the conventional "frame" segment. Drain ready
     * results (1-2 frames old) and keep the freshest. If no segment was
     * opened or the driver doesn't support timer queries, last_gpu_ms
     * stays at -1.0F and format_lines prints "N/A". */
    uint64_t gpu_ns = 0;
    bool gpu_ready = false;
    while (nt_gfx_poll_segment_time_ns("frame", &gpu_ns)) {
        gpu_ready = true;
    }
    if (gpu_ready) {
        s_overlay.last_gpu_ms = (float)((double)gpu_ns / 1.0e6);
    }
    // #endregion

    s_overlay.frame_index++;
}
// #endregion

// #region Read accessors
float nt_debug_overlay_get_fps(void) {
    if (s_overlay.fps_count == 0) {
        return 0.0F;
    }
    float sum_s = 0.0F;
    for (uint16_t i = 0; i < s_overlay.fps_count; i++) {
        sum_s += s_overlay.fps_ring[i];
    }
    return (sum_s > 0.0F) ? ((float)s_overlay.fps_count / sum_s) : 0.0F;
}

float nt_debug_overlay_get_cpu_ms(void) { return s_overlay.last_cpu_ms; }
float nt_debug_overlay_get_gpu_ms(void) { return s_overlay.last_gpu_ms; }
uint32_t nt_debug_overlay_get_draw_calls(void) { return s_overlay.last_draw_calls; }
// #endregion

// #region User counters
void nt_debug_overlay_count(const char *name, uint64_t value) {
    NT_ASSERT(s_overlay.initialized);
    NT_ASSERT(name != NULL);
    uint64_t h = nt_hash64_str(name).value;
    for (uint16_t i = 0; i < s_overlay.user_count; i++) {
        if (s_overlay.user_name_hashes[i] == h) {
            s_overlay.user_values[i] = value;
            return;
        }
    }
    NT_ASSERT(s_overlay.user_count < s_overlay.user_capacity && "nt_debug_overlay user-counter capacity exceeded; raise nt_debug_overlay_desc_t.user_counter_capacity");
    s_overlay.user_name_hashes[s_overlay.user_count] = h;
    s_overlay.user_values[s_overlay.user_count] = value;
    /* Stash readable name (truncated) for format_lines */
    size_t n = strlen(name);
    if (n >= NT_DEBUG_OVERLAY_USER_COUNTER_NAME_MAX) {
        n = NT_DEBUG_OVERLAY_USER_COUNTER_NAME_MAX - 1;
    }
    memcpy(s_overlay.user_names[s_overlay.user_count], name, n);
    s_overlay.user_names[s_overlay.user_count][n] = '\0';
    s_overlay.user_count++;
}
// #endregion

// #region Format & draw
uint32_t nt_debug_overlay_format_lines(char *buf, uint32_t size) {
    NT_ASSERT(buf && size > 0);

    /* GPU slot is "N/A" if timer queries unsupported or no segment polled yet */
    char gpu_buf[32];
    if (s_overlay.last_gpu_ms < 0.0F) {
        (void)snprintf(gpu_buf, sizeof(gpu_buf), "N/A");
    } else {
        (void)snprintf(gpu_buf, sizeof(gpu_buf), "%.2f ms", (double)s_overlay.last_gpu_ms);
    }

    int n = snprintf(buf, size, "FPS: %.1f\nCPU: %.2f ms\nGPU: %s\nDraws: %u\n", (double)nt_debug_overlay_get_fps(), (double)s_overlay.last_cpu_ms, gpu_buf, s_overlay.last_draw_calls);
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
    for (uint16_t i = 0; i < s_overlay.user_count; i++) {
        int m = snprintf(buf + written, size - written, "%s: %llu\n", s_overlay.user_names[i], (unsigned long long)s_overlay.user_values[i]);
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

void nt_debug_overlay_draw(nt_material_t material, nt_font_t font, const float model[16], float size, const float color[4]) {
    NT_ASSERT(s_overlay.initialized);
    char buf[512];
    (void)nt_debug_overlay_format_lines(buf, sizeof(buf));
    /* Explicit set_material AND set_font defeat nt_text_renderer's
     * change-detection early-out so the overlay always binds correctly
     * regardless of prior frame state. */
    nt_text_renderer_set_material(material);
    nt_text_renderer_set_font(font);
    nt_text_renderer_draw(buf, model, size, color, 0.0F, 0.0F);
}
// #endregion

// #region Test access
#ifdef NT_TEST_ACCESS
void nt_debug_overlay_test_inject_frame(float dt_seconds) {
    NT_ASSERT(s_overlay.initialized);
    s_overlay.last_cpu_ms = dt_seconds * 1000.0F;
    s_overlay.last_draw_calls = nt_gfx_get_frame_draw_calls();

    s_overlay.fps_ring[s_overlay.fps_head] = dt_seconds;
    s_overlay.fps_head = (uint16_t)((s_overlay.fps_head + 1) % s_overlay.fps_window);
    if (s_overlay.fps_count < s_overlay.fps_window) {
        s_overlay.fps_count++;
    }

    s_overlay.frame_index++;
}
#endif
// #endregion
