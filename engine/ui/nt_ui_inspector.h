#ifndef NT_UI_INSPECTOR_H
#define NT_UI_INSPECTOR_H

/* Post-walk overlay + sidebar (Clay debug view port). Gated by NT_UI_DEBUG_TOOLS. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h"

typedef struct nt_ui_context nt_ui_context_t;

/* Visible in both modes so callers keep a local instance even when stubs are no-op. */
typedef struct nt_ui_inspector_metrics_t {
    float panel_width;     /* default 400 */
    float row_height;      /* default 30 */
    uint16_t font_size;    /* default 16 */
    uint8_t outer_padding; /* default 10 */
    uint8_t indent_width;  /* default 16 */
} nt_ui_inspector_metrics_t;

#if NT_UI_DEBUG_TOOLS

void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on);
bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx);

/* True when active AND pointer is inside the sidebar. step_interaction honors this internally. */
bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx);

extern const nt_ui_inspector_metrics_t NT_UI_INSPECTOR_METRICS_DEFAULT;

void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics);

/* Called by nt_ui_end if active; not for game code. */
void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx);

/* Call AFTER nt_ui_walk, BEFORE nt_gfx_end_pass. label_size <= 0 skips label. */
void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size);

#else /* NT_UI_DEBUG_TOOLS */

#include "log/nt_log.h"

/* set_active(true) warns once so release-build users see why the toggle does nothing. */
static inline void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on) {
    (void)ctx;
    if (on) {
        static bool s_warned = false;
        if (!s_warned) {
            nt_log_warn("nt_ui_inspector: NT_UI_DEBUG_TOOLS=OFF in this build — inspector excluded; toggle is a no-op. Rebuild with -DNT_UI_DEBUG_TOOLS=ON.");
            s_warned = true;
        }
    }
}
static inline bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx) {
    (void)ctx;
    return false;
}
static inline bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx) {
    (void)ctx;
    return false;
}
static inline void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics) {
    (void)ctx;
    (void)metrics;
    static bool s_metrics_warned = false;
    if (!s_metrics_warned) {
        nt_log_warn("nt_ui_inspector: NT_UI_DEBUG_TOOLS=OFF in this build — metrics ignored. Rebuild with -DNT_UI_DEBUG_TOOLS=ON.");
        s_metrics_warned = true;
    }
}
static inline void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx) { (void)ctx; }
static inline void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size) {
    (void)ctx;
    (void)target;
    (void)font;
    (void)label_size;
}

#endif /* NT_UI_DEBUG_TOOLS */

#endif /* NT_UI_INSPECTOR_H */
