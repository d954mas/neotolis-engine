#include "debug_overlay/nt_debug_overlay.h"

#include "core/nt_assert.h"
#include "metrics/nt_metrics.h"
#include "renderers/nt_text_renderer.h"

#include <stdio.h>
#include <string.h>

/* Pure consumer of nt_metrics: holds only display config; all stats are read from nt_metrics. */
static struct {
    bool initialized;
} s_overlay;

// #region Lifecycle
nt_result_t nt_debug_overlay_init(const nt_debug_overlay_desc_t *desc) {
    NT_ASSERT(!s_overlay.initialized);
    /* desc carries no live knobs after the nt_metrics inversion; accepted for API stability. */
    (void)desc;

    memset(&s_overlay, 0, sizeof(s_overlay));
    s_overlay.initialized = true;
    return NT_OK;
}

void nt_debug_overlay_shutdown(void) { s_overlay.initialized = false; }
// #endregion

// #region Format & draw
/* Format one user counter line from nt_metrics' EXACT stored value: an int counter prints as an
   integer (full uint64 precision), a float counter with decimals. Returns snprintf's length. */
static int format_user_line(char *buf, uint32_t cap, uint16_t i) {
    const char *name = NULL;
    uint64_t u = 0;
    double f = 0.0;
    bool is_float = false;
    nt_metrics_user_get(i, &name, &u, &f, &is_float);
    if (name == NULL) {
        return 0;
    }
    if (is_float) {
        return snprintf(buf, cap, "%s: %.3f\n", name, f);
    }
    return snprintf(buf, cap, "%s: %llu\n", name, (unsigned long long)u);
}

uint32_t nt_debug_overlay_format_lines(char *buf, uint32_t size) {
    NT_ASSERT(buf && size > 0);

    nt_metrics_frame_t last;
    nt_metrics_last(&last);

    /* GPU slot is "N/A" if the timer is unsupported (the host pushed the < 0 sentinel). */
    char gpu_buf[32];
    if (last.gpu_ms < 0.0F) {
        (void)snprintf(gpu_buf, sizeof(gpu_buf), "N/A");
    } else {
        (void)snprintf(gpu_buf, sizeof(gpu_buf), "%.2f ms", (double)last.gpu_ms);
    }

    int n = snprintf(buf, size, "FPS: %.1f\nCPU: %.2f ms\nGPU: %s\nDraws: %u\n", (double)nt_metrics_fps(), (double)last.cpu_ms, gpu_buf, last.draw_calls);
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    uint32_t written = (uint32_t)n;
    if (written >= size) {
        /* Truncated — buf is already NUL-terminated by snprintf */
        return size - 1;
    }

    uint16_t uc = nt_metrics_user_count();
    for (uint16_t i = 0; i < uc; i++) {
        int m = format_user_line(buf + written, size - written, i);
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
       change-detection early-out so the overlay always binds correctly
       regardless of prior frame state. */
    nt_text_renderer_set_material(material);
    nt_text_renderer_set_font(font);
    nt_text_renderer_draw(buf, model, size, color, 0.0F, 0.0F);
}
// #endregion
